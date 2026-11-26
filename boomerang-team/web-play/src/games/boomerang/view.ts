// 第 11–12 期对战画面主控：正片 1:1 场景（r3d/scene.js）+ 正片动作（r3d/pose.js、stage.js）+ 正片顶栏（r3d/hud.js），
// 用连续的小数游戏时间 gt 以显示器帧率（60 fps+）驱动一切——位置、朝向、四肢、镖、拖尾、草弯、粒子都在相邻两步之间连续插值，
// 不随 20 Hz 的引擎步跳帧。两种时钟：
//   - 回放 / 看录像：自己推进 gt（暂停、单步、倍速、拖动；有人出局顿一下、终结一击前后慢放，同正片 timeline.py）
//   - 实时对局：gt 由 match.ts 的固定节拍时钟给（引擎总比画面领先一步，画面在 t → t+1 之间插值），新帧一来就增量重建动作表
// 机位照正片：俯角 58°、50 mm 镜头、36 mm 底片（水平视角固定），整张图放进顶栏以下的安全框。
import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { Replay, D_HAND, D_GROUND, D_RISE, D_GONE } from './r3d/rp.js';
import { Stage, INTRO, OUTRO } from './r3d/stage.js';
import { SceneTerrain, loadScenePack } from './r3d/scene.js';
import { loadVox } from './r3d/vox.js';
import { Hud } from './r3d/hud.js';
import { FLY_Z, ang_lerp } from './r3d/pose.js';
import { sfx } from './sfx';
import type { ParsedReplay } from './types';

export { INTRO, OUTRO };

export interface ViewSeat {
  key: string;        // 体素造型
  color: string;      // 主色（镖、色环）
  label: string;      // 显示名
  avatar: string;     // 头像图片地址
}
export interface ViewLoadOpts {
  human?: number;           // 真人座位（-1 = 没有）
  teamHex: [string, string];   // 显示位 0 左 / 1 右 的队伍色
  slotOfTeam: [number, number]; // 原始队号 -> 显示位
  teamNames: [string, string];  // 按显示位
  startGt?: number;
}
export type AimState = 'ready' | 'out' | 'ground' | 'none';

const clamp = (x: number, a = 0, b = 1) => (x < a ? a : x > b ? b : x);
const easeF = (t: number) => { t = clamp(t); return t * t * (3 - 2 * t); };
const SMOOTH_KEYS = ['hand_a', 'hand_b', 'foot_a', 'foot_b', 'head', 'hair', 'twist', 'lean', 'sx', 'sy', 'sz', 'body_dz', 'roll'];

export class BoomerangView {
  readonly el: HTMLElement;
  readonly canvas: HTMLCanvasElement;
  private hudCv: HTMLCanvasElement;
  private youEl: HTMLElement;
  private renderer: THREE.WebGLRenderer;
  private camera = new THREE.PerspectiveCamera(32, 1, 0.1, 400);
  private controls: OrbitControls;
  private scene: THREE.Scene | null = null;
  rp: any = null;
  stage: any = null;
  terrain: any = null;
  hud: any = null;
  P: ParsedReplay | null = null;
  seats: ViewSeat[] = [];
  human = -1;
  ready = false;
  private loadSeq = 0;

  // 时间
  gt = 0;
  playing = false;
  speed = 1;
  private liveClock: (() => number) | null = null;
  private holdLeft = 0;
  private stops: [number, number][] = [];
  private finalHit: [number, number] | null = null;
  private lastTs: number | null = null;
  private cueT = -1e9;
  private endNotified = false;

  // 选项
  ghost = false;
  delayView = false;
  follow = false;
  interactive = true;
  private aim: { x: number; y: number } | null = null;
  private aimLine: THREE.Line;
  private aimRing: THREE.Mesh;
  private smooth: Map<number, any> = new Map();
  private home = { tgt: new THREE.Vector3(), dist: 20 };
  private camTgt = new THREE.Vector3();
  private fpsAcc = { n: 0, t: 0, fps: 60, slow: 0 };
  private dpr = Math.min(window.devicePixelRatio || 1, 2);

  /** 每帧回调：gt、是否在播放（给进度条、技能栏刷新） */
  onFrame: ((gt: number) => void) | null = null;
  /** 一局（实时）演完庆祝以后调用一次 */
  onLiveEnd: (() => void) | null = null;
  /** 事件提示（音效、顿帧）：kind，是否和真人有关 */
  onCue: ((kind: string, mine: boolean) => void) | null = null;

  constructor(host: HTMLElement) {
    this.el = host;
    host.classList.add('bmv');
    this.canvas = document.createElement('canvas');
    this.canvas.className = 'bmv-3d';
    this.hudCv = document.createElement('canvas');
    this.hudCv.className = 'bmv-hud';
    this.youEl = document.createElement('div');
    this.youEl.className = 'bmv-you';
    this.youEl.textContent = '你';
    const vig = document.createElement('div');
    vig.className = 'bmv-vig';
    host.append(this.canvas, vig, this.hudCv, this.youEl);
    this.renderer = new THREE.WebGLRenderer({ canvas: this.canvas, antialias: true, preserveDrawingBuffer: false, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(this.dpr);
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.controls = new OrbitControls(this.camera, this.canvas);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.12;
    this.controls.maxPolarAngle = Math.PI * 0.49;
    this.controls.minDistance = 3; this.controls.maxDistance = 120;
    this.controls.zoomToCursor = true;
    // 瞄准：地上一条引导虚线 + 落点小圈（Blender 坐标 → three：(x, h, y)）
    const lg = new THREE.BufferGeometry(); lg.setAttribute('position', new THREE.BufferAttribute(new Float32Array(6), 3));
    this.aimLine = new THREE.Line(lg, new THREE.LineDashedMaterial({ color: '#FFFFFF', dashSize: 0.22, gapSize: 0.16, transparent: true, opacity: 0.8, depthTest: false, depthWrite: false }));
    this.aimLine.frustumCulled = false; this.aimLine.renderOrder = 20;
    this.aimRing = new THREE.Mesh(new THREE.RingGeometry(0.2, 0.27, 40), new THREE.MeshBasicMaterial({ color: '#FFFFFF', transparent: true, opacity: 0.7, depthTest: false, depthWrite: false, side: THREE.DoubleSide }));
    this.aimRing.rotation.x = -Math.PI / 2; this.aimRing.renderOrder = 20;
    new ResizeObserver(() => this.resize()).observe(host);
    this.resize();
    const loop = (ts: number) => { requestAnimationFrame(loop); this.frame(ts); };
    requestAnimationFrame(loop);
  }

  // ---------------------------------------------------------------- 载入
  async load(P: ParsedReplay, seats: ViewSeat[], opts: ViewLoadOpts): Promise<boolean> {
    const seq = ++this.loadSeq;
    this.ready = false;
    const keys = seats.map((s) => s.key);
    const [pack] = await Promise.all([loadScenePack(P.mapName), loadVox(keys)]);
    if (seq !== this.loadSeq) return false;
    this.dispose();
    const rp = new Replay(P);
    const scene = new THREE.Scene();
    const raw = opts.slotOfTeam.map((s) => opts.teamHex[s]);
    // 双堡：两侧城堡的旗、幡、塔顶按"这一局谁在那一侧"的队伍色换回去
    let leftTeam = 0;
    if (P.mode >= 2 && P.frames[0]) {
      let sx = 0, n = 0;
      P.frames[0].players.forEach((p, k) => { if (P.players[k].team === 0) { sx += p.x; n++; } });
      leftTeam = n && sx / n > (P.nx * P.cell) / 2 ? 1 : 0;
    }
    const terrain = new SceneTerrain(rp, pack, scene, { side: { left: raw[leftTeam], right: raw[1 - leftTeam] } });
    const Wm = rp.Wm, Hm = rp.Hm;
    terrain.addLights(scene, Wm / 2, Hm / 2, Math.max(Wm, Hm));
    const stage = new Stage(scene, rp, keys, seats.map((s) => s.color), raw);
    scene.add(this.aimLine, this.aimRing);
    this.aimLine.visible = this.aimRing.visible = false;
    this.scene = scene; this.rp = rp; this.stage = stage; this.terrain = terrain; this.P = P; this.seats = seats;
    this.human = opts.human ?? -1;
    this.hud = new Hud(this.hudCv, {
      mode: P.mode, n: rp.n, labels: seats.map((s) => s.label), avatars: seats.map((s) => s.avatar), colors: seats.map((s) => s.color),
      slots: P.players.map((p) => opts.slotOfTeam[p.team & 1]), slotOfTeam: opts.slotOfTeam, teamHex: opts.teamHex, teamNames: opts.teamNames, you: this.human,
    });
    this.smooth.clear();
    if (this.ghost) stage.setGhosts(true);
    this.buildStops();
    this.gt = opts.startGt ?? -INTRO;
    this.cueT = this.gt;
    this.endNotified = false;
    this.holdLeft = 0;
    this.homeCamera();
    this.ready = true;
    this.renderNow();
    return true;
  }

  dispose() {
    if (this.stage && this.scene) this.stage.dispose(this.scene);
    if (this.terrain) this.terrain.dispose();
    if (this.scene) this.scene.traverse((o: any) => { if (o.geometry && o !== this.aimLine && o !== this.aimRing) o.geometry.dispose?.(); });
    this.scene = null; this.stage = null; this.terrain = null; this.rp = null; this.ready = false;
  }

  // ---------------------------------------------------------------- 时间
  /** 实时对局：画面时间由外部时钟给；null = 回到自己推进 */
  setLive(clock: (() => number) | null) {
    this.liveClock = clock;
    this.playing = !clock ? this.playing : false;
    this.endNotified = false;
  }
  get live() { return !!this.liveClock; }
  get minT() { return -INTRO; }
  get maxT() { return this.rp ? (this.rp.over ? this.rp.T + OUTRO : this.rp.T) : 0; }
  seek(t: number) { if (!this.rp) return; this.gt = clamp(t, this.minT, this.maxT); this.cueT = this.gt; this.holdLeft = 0; }
  play(v = true) {
    this.playing = v && !!this.rp;
    if (this.playing && this.gt >= this.maxT - 0.01) this.seek(-INTRO);
    if (this.playing) sfx.resume();
  }
  stepBy(d: number) {
    if (!this.rp) return;
    this.playing = false;
    const isInt = Math.abs(this.gt - Math.round(this.gt)) < 1e-6;
    this.seek(isInt ? Math.round(this.gt) + d : (d > 0 ? Math.ceil(this.gt) : Math.floor(this.gt)));
  }

  // 照 timeline.py：顿帧（击杀停 3 帧、终结一击 5 帧，镖撞镖 / 踢中 2 帧，按 30 帧/秒）和终结一击前后的慢放
  private buildStops() {
    this.stops = []; this.finalHit = null;
    const rp = this.rp;
    if (!rp || !rp.over) return;
    const A = this.stage.A, T = rp.T;
    const deaths = A.all_deaths();
    if (rp.reason === 'ko' && deaths.length) {
      let best: [number, number] | null = null;
      for (const [B, k] of deaths) { const td = B.death_info(k)[0]; if (!best || td > best[0] || (td === best[0] && k > best[1])) best = [td, k]; }
      if (best && best[0] >= T - 1) this.finalHit = best;
    }
    const st = new Map<number, number>();
    const kfr = rp.mode === 4 ? 2 : 3;
    for (const [B, k] of deaths) {
      const c = B.deaths[k][1];
      if (c === 1 || c === 2) { const td = B.death_info(k)[0]; st.set(td + 0.6, this.finalHit && this.finalHit[1] === k && this.finalHit[0] === td ? 5 : kfr); }
    }
    for (const c of rp.clashes) if (!st.has(c[0] + 1.5)) st.set(c[0] + 1.5, 2);
    for (const c of rp.kickhits) if (!st.has(c[0] + 1.5)) st.set(c[0] + 1.5, 2);
    for (const c of rp.lifeloss) if (!st.has(c[0] + 1.5)) st.set(c[0] + 1.5, 3);
    this.stops = [...st.entries()].sort((a, b) => a[0] - b[0]);
  }
  private hitStop(t: number) {
    if (!this.finalHit) return 1;
    const td = this.finalHit[0];
    if (td - 6 < t && t < td + 8) return 0.3 + 0.7 * Math.max(easeF((td - 2 - t) / 4), easeF((t - td - 3.5) / 4.5));
    return 1;
  }
  private advance(dtm: number) {
    if (this.holdLeft > 0) { this.holdLeft -= dtm; return; }
    const g0 = this.gt, g1 = this.gt + dtm * 20 * this.speed * this.hitStop(this.gt);
    for (const [ts, n] of this.stops) if (ts > g0 && ts <= g1) { this.gt = ts; this.holdLeft = n / 30; return; }
    this.gt = g1;
  }

  // ---------------------------------------------------------------- 镜头
  resize() {
    const w = this.el.clientWidth || 640, h = this.el.clientHeight || 360;
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / Math.max(1, h);
    this.camera.fov = 2 * Math.atan(18 / 50 / this.camera.aspect) * 180 / Math.PI;     // 50 mm、36 mm 宽的底片
    this.camera.updateProjectionMatrix();
    if (this.rp) this.homeCamera(true);
  }
  /** 整张图放进顶栏以下的安全框（照正片 timeline 的全场取景：俯角 58°） */
  homeCamera(keepUser = false) {
    if (!this.rp) return;
    const pitch = 58 * Math.PI / 180;
    const Wm = this.rp.Wm, Hm = this.rp.Hm;
    const w = this.el.clientWidth || 640, h = this.el.clientHeight || 360;
    const top = (this.hud ? this.hud.barBottom() : Math.round(w / 1920 * 118)) + 6;
    const box = { l: 8, r: w - 8, t: top, b: h - 6 };
    const tgt = new THREE.Vector3(Wm / 2, 0, Hm / 2);
    const pts: THREE.Vector3[] = [];
    for (const [x, z] of [[0, 0], [Wm, 0], [0, Hm], [Wm, Hm], [Wm / 2, 0], [Wm / 2, Hm], [0, Hm / 2], [Wm, Hm / 2]]) pts.push(new THREE.Vector3(x, 0, z), new THREE.Vector3(x, 1.3, z));
    const v = new THREE.Vector3(), cam = this.camera;
    // 先按中心粗找距离，再把目标点挪到安全框正中
    let d = 6;
    for (let it = 0; it < 2; it++) {
      for (d = 4; d < 200; d *= 1.025) {
        cam.position.set(tgt.x, tgt.y + Math.sin(pitch) * d, tgt.z + Math.cos(pitch) * d);
        cam.lookAt(tgt); cam.updateMatrixWorld();
        let ok = true;
        for (const q of pts) {
          v.copy(q).project(cam);
          const sx = (v.x * 0.5 + 0.5) * w, sy = (-v.y * 0.5 + 0.5) * h;
          if (sx < box.l || sx > box.r || sy < box.t || sy > box.b) { ok = false; break; }
        }
        if (ok) break;
      }
      // 安全框中心对应的地面点：让地图中心落在框中心（顶栏占掉的那条不算）
      let mnY = 1e9, mxY = -1e9;
      for (const q of pts) { v.copy(q).project(cam); const sy = (-v.y * 0.5 + 0.5) * h; mnY = Math.min(mnY, sy); mxY = Math.max(mxY, sy); }
      const want = (box.t + box.b) / 2, have = (mnY + mxY) / 2, px = (want - have);
      const k = (2 * Math.tan(cam.fov * Math.PI / 360) * d) / h;          // 每像素多少米（在目标点处，近似）
      tgt.z += -px * k / Math.sin(pitch);
    }
    this.home.tgt.copy(tgt); this.home.dist = d;
    if (keepUser && this.camMoved()) return;
    this.applyCam(tgt, d);
  }
  private camMoved() { return this.controls.target.distanceTo(this.home.tgt) > 0.05 && !this.follow && this.interactive; }
  private applyCam(tgt: THREE.Vector3, d: number) {
    const pitch = 58 * Math.PI / 180;
    this.camera.position.set(tgt.x, tgt.y + Math.sin(pitch) * d, tgt.z + Math.cos(pitch) * d);
    this.controls.target.copy(tgt);
    this.camera.lookAt(tgt);
    this.controls.update();
    this.camTgt.copy(tgt);
  }
  resetCamera() { if (this.rp) this.applyCam(this.home.tgt.clone(), this.home.dist); }
  setInteractive(v: boolean) { this.interactive = v; this.controls.enabled = v; if (!v) this.resetCamera(); }

  /** 屏幕坐标 -> 地图坐标（米）：射线和飞镖飞行高度那一层平面求交（瞄准没有视差） */
  screenToWorld(cx: number, cy: number): { x: number; y: number } | null {
    if (!this.rp) return null;
    const r = this.canvas.getBoundingClientRect();
    const nx = ((cx - r.left) / r.width) * 2 - 1, ny = -((cy - r.top) / r.height) * 2 + 1;
    const ray = new THREE.Raycaster();
    ray.setFromCamera(new THREE.Vector2(nx, ny), this.camera);
    const p = new THREE.Vector3();
    if (!ray.ray.intersectPlane(new THREE.Plane(new THREE.Vector3(0, 1, 0), -FLY_Z), p)) return null;
    return { x: p.x, y: p.z };
  }
  /** 地图坐标 -> 画面 CSS 像素 */
  project(x: number, y: number, z: number): [number, number] | null {
    const v = new THREE.Vector3(x, z, y).project(this.camera);
    if (v.z > 1) return null;
    return [(v.x * 0.5 + 0.5) * this.el.clientWidth, (-v.y * 0.5 + 0.5) * this.el.clientHeight];
  }

  setAim(p: { x: number; y: number } | null) { this.aim = p; }
  setGhost(on: boolean) { this.ghost = on; this.stage?.setGhosts(on); }
  setDelayView(on: boolean) { this.delayView = on; }
  setFollow(on: boolean) { this.follow = on; if (!on) this.resetCamera(); }

  /** 真人此刻的状态（技能栏、准星用） */
  humanState() {
    const rp = this.rp;
    if (!rp || this.human < 0) return null;
    const s = rp.states[rp.T];
    const c = s.chars[this.human], d = s.darts[this.human];
    const dart: AimState = d.st === D_HAND ? 'ready' : d.st === D_GROUND || d.st === D_RISE ? 'ground' : d.st === D_GONE ? 'none' : 'out';
    return { alive: !!c.alive, st: c.st, timer: c.timer, cd: c.cd, dart, dartSt: d.st, x: c.x / 1000, y: c.y / 1000, lives: rp.life_index(this.human, rp.T) };
  }

  // ---------------------------------------------------------------- 每帧
  private frame(ts: number) {
    const dtm = this.lastTs == null ? 0 : Math.min(0.1, (ts - this.lastTs) / 1000);
    this.lastTs = ts;
    this.measure(dtm);
    if (!this.ready || !this.rp || !this.scene) return;
    const rp = this.rp;
    if (this.liveClock) {
      if (rp.sync()) { this.stage.refresh(); this.cueScan(); }
      const c = this.liveClock();
      if (rp.over && c >= rp.T - 0.001) {
        // 对局结束：接着自己往下演庆祝（胜者跳、彩纸、横幅）
        this.liveClock = null; this.gt = rp.T; this.playing = true; this.speed = 1;
        this.buildStops(); this.stops = []; this.finalHit = null;
      } else this.gt = clamp(c, -INTRO, rp.T);
    } else if (this.playing) {
      this.advance(dtm);
      if (this.gt >= this.maxT) { this.gt = this.maxT; this.playing = false; }
    }
    if (!this.liveClock && rp.over && !this.endNotified && this.gt >= rp.T + 30) { this.endNotified = true; this.onLiveEnd?.(); }
    this.cueScan();
    this.renderNow(dtm);
    this.onFrame?.(this.gt);
  }

  private measure(dtm: number) {
    const f = this.fpsAcc;
    if (dtm <= 0) return;
    f.n++; f.t += dtm;
    if (f.t >= 1) {
      f.fps = f.n / f.t; f.n = 0; f.t = 0;
      // 自适应清晰度：连续两秒掉到 40 帧以下就降一点像素密度
      if (f.fps < 40 && this.dpr > 1) { if (++f.slow >= 2) { this.dpr = Math.max(1, this.dpr - 0.25); this.renderer.setPixelRatio(this.dpr); this.resize(); f.slow = 0; } } else f.slow = 0;
    }
  }
  get fps() { return this.fpsAcc.fps; }

  /** 实时对局里四肢、朝向用 35 ms 的平滑接住"动作一来就跳到蓄势"的那一下（位置不平滑，照插值走） */
  private filterPose = (k: number, p: any) => {
    if (!this.liveClock) return;
    const s = this.smooth.get(k);
    const ok = p.visible !== false && !p.slice && p.flip == null && (p.ground ?? 1) > 0.5;
    if (!s || !ok || s.life !== p.life || Math.abs(s.gt - this.gt) > 3) { this.smooth.set(k, { ...p, gt: this.gt, life: p.life }); return; }
    const dt = Math.max(0, this.gt - s.gt) / 20, a = 1 - Math.exp(-dt / 0.035), ah = 1 - Math.exp(-dt / 0.045);
    for (const key of SMOOTH_KEYS) {
      const v = p[key], o = s[key];
      if (v == null || o == null) { s[key] = v; continue; }
      if (Array.isArray(v)) { for (let i = 0; i < v.length; i++) v[i] = o[i] + (v[i] - o[i]) * a; s[key] = v.slice(); }
      else { p[key] = o + (v - o) * a; s[key] = p[key]; }
    }
    p.heading = ang_lerp(s.heading, p.heading, ah); s.heading = p.heading;
    s.gt = this.gt;
  };

  renderNow(dtm = 0) {
    if (!this.ready || !this.scene) return;
    const rp = this.rp, gt = this.gt, delay = rp.cfg.DELAY ?? 4, me = this.human;
    const myTeam = me >= 0 ? rp.players[me][1] : -1;
    const opts: any = { filterPose: this.filterPose };
    if (this.delayView && me >= 0) opts.seatT = (k: number, t: number) => (k === me || (rp.mode >= 2 && rp.players[k][1] === myTeam) ? t : Math.max(0, t - delay));
    if (this.ghost) opts.ghostSeats = (k: number) => k !== me;
    const { poses, movers } = this.stage.apply(gt, this.terrain.pieceList, opts);
    this.terrain.update(gt, movers);
    // 瞄准引导线、"你"的标记
    const mp = me >= 0 ? poses[me] : null;
    const alive = mp && mp.visible !== false && !mp.slice && (mp.ground ?? 1) > 0.3;
    if (this.aim && alive && this.liveClock) {
      const P = this.aimLine.geometry.attributes.position.array as Float32Array;
      P[0] = mp.x; P[1] = 0.05; P[2] = mp.y; P[3] = this.aim.x; P[4] = 0.05; P[5] = this.aim.y;
      this.aimLine.geometry.attributes.position.needsUpdate = true;
      this.aimLine.computeLineDistances();
      this.aimRing.position.set(this.aim.x, 0.05, this.aim.y);
      const hs = this.humanState(), col = hs?.dart === 'ready' ? '#FFFFFF' : hs?.dart === 'ground' ? '#FFB36B' : '#9FD8FF';
      (this.aimLine.material as THREE.LineDashedMaterial).color.set(col);
      (this.aimRing.material as THREE.MeshBasicMaterial).color.set(col);
      this.aimLine.visible = this.aimRing.visible = true;
    } else this.aimLine.visible = this.aimRing.visible = false;
    if (mp && alive && gt > -12) {
      const q = this.project(mp.x, mp.y, (mp.z || 0) + 2.05);
      if (q) { this.youEl.style.display = 'block'; this.youEl.style.transform = `translate(${q[0].toFixed(1)}px,${q[1].toFixed(1)}px) translate(-50%,-100%)`; }
      else this.youEl.style.display = 'none';
    } else this.youEl.style.display = 'none';
    // 镜头：跟随（真人局可选）或者正片全景；有人出局轻微震动
    if (this.follow && mp && this.liveClock !== undefined) {
      const tgt = new THREE.Vector3(clamp(mp.x, 0, rp.Wm), 0, clamp(mp.y, 0, rp.Hm));
      const k = 1 - Math.exp(-dtm / 0.25);
      this.camTgt.lerp(tgt, dtm > 0 ? k : 1);
      const pitch = 58 * Math.PI / 180, d = Math.min(this.home.dist, Math.max(13, this.home.dist * 0.6));
      this.camera.position.set(this.camTgt.x, Math.sin(pitch) * d, this.camTgt.z + Math.cos(pitch) * d);
      this.camera.lookAt(this.camTgt);
    } else if (this.interactive) this.controls.update();
    const sh = this.shakeAt(gt);
    this.camera.position.add(sh);
    this.renderer.render(this.scene, this.camera);
    this.camera.position.sub(sh);
    this.hud.draw(rp, this.stage.A, gt, (x: number, y: number, z: number) => this.project(x, y, z));
  }

  private shake = new THREE.Vector3();
  private shakeAt(t: number) {
    const s = this.shake.set(0, 0, 0);
    for (const [B, k] of this.stage.A.all_deaths()) {
      const [Td, cause] = B.death_info(k), killer = B.deaths[k][2];
      const u = t - Td; if (u < 0 || u > 7) continue;
      const mine = k === this.human || killer === this.human;
      const kk = 0.07 * Math.exp(-u / 1.8) * (cause === 3 && killer < 0 ? 0.5 : 1) * (mine ? 1.8 : 1);
      s.x += Math.sin(u * 9.1 + k) * kk; s.y += Math.sin(u * 11.3 + 2 * k) * kk * 0.7; s.z += Math.cos(u * 8.3 + k) * kk;
    }
    for (const [tt, k] of this.rp.lifeloss) { const u = t - tt - 1; if (u >= 0 && u < 7) { const kk = 0.09 * Math.exp(-u / 1.8); s.x += Math.sin(u * 9.7 + k) * kk; s.z += Math.cos(u * 8.1) * kk; } }
    return s;
  }

  /** 时间往前走时，把这一段里发生的事件变成音效 / 顿帧提示 */
  private cueScan() {
    const rp = this.rp, gt = this.gt;
    if (gt < this.cueT || gt - this.cueT > 8) { this.cueT = gt; return; }
    if (!this.onCue) { this.cueT = gt; return; }
    const t0 = Math.floor(this.cueT - 1) + 1, t1 = Math.floor(gt - 1);
    for (let t = Math.max(0, t0); t <= t1 && t <= rp.T; t++) {
      const f = this.P!.frames[t];
      if (!f) continue;
      for (const e of f.events) {
        const me = this.human;
        const mine = me >= 0 && (e.a === me || (['HIT', 'CUT', 'KICKHIT', 'CLASH', 'BLOCK'].includes(e.type) && e.b === me) || (e.type === 'DEATH' && e.c === me));
        const k = ({ RELEASE: 'throw', SLASH: 'slash', KICK: 'kick', KICKHIT: 'kickhit', CLASH: 'clash', BLOCK: 'clash', DEATH: 'ko', CAPTURE: 'capture', LIFE: 'king', DASH: 'dash', CATCH: 'catch', DROP: 'drop' } as Record<string, string>)[e.type];
        if (k) this.onCue(k, mine);
      }
    }
    this.cueT = gt;
  }

  /** 截图自测：座位 k 的脸朝哪（地图坐标里的单位向量）和 heading 比对 */
  faceProbe(k: number) {
    const rig = this.stage?.rigs[k];
    if (!rig) return null;
    // 体素的正面朝 -y：把 (0, -1, 0) 按角色根节点的世界姿态转出来；再用眼睛那块方块（face_open）相对头中心的位置复核一次
    rig.root.updateWorldMatrix(true, true);
    const q = new THREE.Quaternion();
    rig.root.getWorldQuaternion(q);
    const f = new THREE.Vector3(0, -1, 0).applyQuaternion(q);
    const L = Math.hypot(f.x, f.z) || 1;
    const eyes = rig.parts.face_open, head = rig.parts.head;
    let eyeDot = 1;
    if (eyes && head) {
      const a = new THREE.Vector3(), b = new THREE.Vector3();
      const ge = eyes.children[0].geometry, gh = head.children[0].geometry;
      ge.computeBoundingBox(); gh.computeBoundingBox();
      ge.boundingBox.getCenter(a); eyes.children[0].localToWorld(a);
      gh.boundingBox.getCenter(b); head.children[0].localToWorld(b);
      const ex = a.x - b.x, ey = a.z - b.z, eL = Math.hypot(ex, ey) || 1;
      eyeDot = (ex / eL) * (f.x / L) + (ey / eL) * (f.z / L);
    }
    const pose = this.stage.A.char_pose(k, this.gt);
    const hr = pose.heading * Math.PI / 180;
    return { face: [f.x / L, f.z / L], heading: pose.heading, dot: (f.x / L) * Math.cos(hr) + (f.z / L) * Math.sin(hr), eyeDot };
  }
}
