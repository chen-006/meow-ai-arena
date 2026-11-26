// 照正片 src\render_bl.py 的 Stage 移植：角色（VoxRig）、场上的镖、旋转残影、镖的拖尾、冲刺气流、特效，按游戏时间 gt 摆好。
// 第 12 集加：国王王冠、占点圈、每条命各一套被切开的两截。网站加：0.2 秒 AI 延迟残影（ghost）、"和 AI 一样晚看到对手"的逐座位时差。
import * as THREE from 'three';
import { Animator, INTRO, OUTRO } from './pose.js';
import { VoxRig, makeDart, setObjColor } from './char2.js';
import { FX } from './fx2.js';
import { Points } from './modes.js';
import { D_OUT } from './rp.js';

export { INTRO, OUTRO };

// 镖的拖尾 / 冲刺气流（render_bl.Ribbon）
class Ribbon {
  constructor(root, n = 17) {
    this.n = n;
    const ROWS = this.ROWS = [[-1, 0], [-0.45, 1], [0, 1], [0.45, 1], [1, 0]];
    const m = ROWS.length;
    const idx = [];
    for (let i = 0; i < n - 1; i++) for (let j = 0; j < m - 1; j++) { const a = i * m + j; idx.push(a, a + 1, a + 1 + m, a, a + 1 + m, a + m); }
    const g = this.g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(n * m * 3), 3));
    g.setAttribute('color', new THREE.BufferAttribute(new Float32Array(n * m * 4), 4));
    g.setIndex(idx);
    const mat = new THREE.MeshBasicMaterial({ vertexColors: true, transparent: true, depthWrite: false, side: THREE.DoubleSide, color: new THREE.Color(1.35, 1.35, 1.35) });
    this.ob = new THREE.Mesh(g, mat); this.ob.frustumCulled = false; this.ob.renderOrder = 4; this.ob.visible = false;
    root.add(this.ob);
  }
  update(pts, col, style) {
    if (pts.length < 2) { this.ob.visible = false; return; }
    this.ob.visible = true;
    const n = this.n, R = this.ROWS, m = R.length, k = pts.length;
    const co = this.g.attributes.position.array, cl = this.g.attributes.color.array;
    for (let i = 0; i < n; i++) {
      const j = Math.min(i, k - 1);
      const [x, y, z, st] = pts[j], a = pts[Math.max(j - 1, 0)], b = pts[Math.min(j + 1, k - 1)];
      const dx = a[0] - b[0], dy = -(a[1] - b[1]), L = Math.hypot(dx, dy);
      const [px, py] = L < 1e-6 ? [0, 0] : [-dy / L, dx / L];
      const t = j / Math.max(1, k - 1);
      const [hw, al, core] = i < k ? style(j, t, st) : [0, 0, 0];
      for (let r = 0; r < m; r++) {
        const [u, e] = R[r], idx = i * m + r;
        co[idx * 3] = x + px * hw * u; co[idx * 3 + 1] = -y + py * hw * u; co[idx * 3 + 2] = z + (r === 2 ? 0.004 : 0);
        if (r === 2) { cl[idx * 4] = col[0] + (1 - col[0]) * core; cl[idx * 4 + 1] = col[1] + (1 - col[1]) * core; cl[idx * 4 + 2] = col[2] + (1 - col[2]) * core; cl[idx * 4 + 3] = al; }
        else { cl[idx * 4] = col[0]; cl[idx * 4 + 1] = col[1]; cl[idx * 4 + 2] = col[2]; cl[idx * 4 + 3] = al * e * 0.8; }
      }
    }
    this.g.attributes.position.needsUpdate = true; this.g.attributes.color.needsUpdate = true;
  }
}
function trailStyle(j, t, st) {
  const fade = (1 - t) ** 1.25;
  if (st === D_OUT) return [0.27 * (0.25 + 0.75 * (1 - t) ** 0.8), 0.92 * fade, 0.7];
  const dash = 0.45 + 0.55 * (Math.floor(j / 2) % 2 === 0 ? 1 : 0.25);
  return [0.15 * (0.3 + 0.7 * (1 - t) ** 0.8), 0.75 * fade * dash, 0.15];
}
const dashStyle = (j, t) => [0.3 * (1 - t) ** 0.6, 0.5 * (1 - t) ** 1.4, 0.9];

// 旋转残影（chars.make_blur_disc）
function blurDisc(hex, r = 0.56) {
  const seg = 60, col = new THREE.Color(hex), c2 = [col.r, col.g, col.b].map((v) => Math.min(1, v * 0.55 + 0.45));
  const radii = [[0.14, 0], [0.5, 0.26], [0.85, 0.32], [1, 0]], V = [], C = [], idx = [];
  for (const [rr, a] of radii) for (let k = 0; k < seg; k++) {
    const ang = 2 * Math.PI * k / seg; V.push(r * rr * Math.cos(ang), r * rr * Math.sin(ang), 0);
    C.push(...c2, a * (0.45 + 0.55 * Math.max(0, Math.cos(ang * 3)) ** 2));
  }
  for (let j = 0; j < radii.length - 1; j++) for (let k = 0; k < seg; k++) { const k2 = (k + 1) % seg; idx.push(j * seg + k, j * seg + k2, (j + 1) * seg + k2, j * seg + k, (j + 1) * seg + k2, (j + 1) * seg + k); }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(V, 3)); g.setAttribute('color', new THREE.Float32BufferAttribute(C, 4)); g.setIndex(idx);
  const m = new THREE.Mesh(g, new THREE.MeshBasicMaterial({ vertexColors: true, transparent: true, depthWrite: false, side: THREE.DoubleSide, color: new THREE.Color(1.25, 1.25, 1.25) }));
  m.renderOrder = 3;
  return m;
}

// 残影和本体之间的虚线牵引线
function ghostLine() {
  const g = new THREE.BufferGeometry(); g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(6), 3));
  const l = new THREE.Line(g, new THREE.LineDashedMaterial({ color: '#25D9F2', dashSize: 0.12, gapSize: 0.09, transparent: true, opacity: 0.85, depthWrite: false }));
  l.frustumCulled = false; l.renderOrder = 6;
  return l;
}

export class Stage {
  /** rp：rp.js 的 Replay；keys：每个座位的造型；colors：每个座位的主色；teamHex：原始队号 0/1 的显示色 */
  constructor(scene, rp, keys, colors, teamHex) {
    this.rp = rp; this.keys = keys.slice(); this.teamHex = teamHex;
    const root = this.root = new THREE.Group(); root.rotation.x = -Math.PI / 2; scene.add(root);
    this.A = new Animator(rp, keys);
    const team = rp.mode >= 2;
    this.rigs = []; this.colors = colors.slice();
    for (let k = 0; k < rp.n; k++) {
      const tc = team ? teamHex[rp.players[k][1]] || null : null;
      const crown = rp.mode === 5 && rp.kings.includes(k);
      this.rigs.push(new VoxRig(root, keys[k], this.colors[k], tc, tc, { crown }));
    }
    this.darts = []; this.blurs = []; this.trails = []; this.dashes = [];
    for (let k = 0; k < rp.n; k++) {
      const d = makeDart(this.colors[k], 1.42, 0.016); root.add(d); this.darts.push(d);
      const b = blurDisc(this.colors[k]); root.add(b); this.blurs.push(b);
      this.trails.push(new Ribbon(root)); this.dashes.push(new Ribbon(root, 11));
    }
    this.lin = this.colors.map((c) => { const q = new THREE.Color(c); return [q.r, q.g, q.b]; });
    this.fx = new FX(root, this.colors, teamHex);
    this.points = rp.mode === 4 && rp.points.length ? new Points(root, rp, teamHex) : null;
    this.ghosts = null;
    this.buildSlices();
  }

  /** 回放长了（实时对局每步）：重建动作表；新出现的"被切开"建两截 */
  refresh() { this.A.refresh(); this.buildSlices(); }

  buildSlices() {
    for (const [B, k] of this.A.all_deaths()) {
      if (!B.sliced.has(k) || this.rigs[k].hasSlice(B.life)) continue;
      const si = B.slice_info(k);
      if (si) this.rigs[k].buildSlice(si);
    }
  }

  setGhosts(on) {
    if (on && !this.ghosts) {
      this.ghosts = this.rp.players.map((_, k) => ({ rig: new VoxRig(this.root, this.keys[k], this.colors[k], null, null, { ghost: true, crown: this.rp.mode === 5 && this.rp.kings.includes(k) }), line: ghostLine() }));
      for (const g of this.ghosts) this.root.add(g.line);
    } else if (!on && this.ghosts) {
      for (const g of this.ghosts) { g.rig.dispose(); this.root.remove(g.line); }
      this.ghosts = null;
    }
  }

  dispose(scene) {
    for (const r of this.rigs) r.dispose();
    this.setGhosts(false);
    this.root.traverse((o) => { if (o.geometry) o.geometry.dispose(); });
    scene.remove(this.root);
  }

  /** opts.seatT(k)：这个座位画哪一刻（"和 AI 一样晚看到对手"时对手比自己晚 4 步）；opts.ghostSeats：哪些座位画 0.2 秒残影 */
  apply(gt, pieces, opts = {}) {
    const A = this.A, rp = this.rp, poses = [], movers = [];
    const delay = rp.cfg.DELAY ?? 4;
    for (let k = 0; k < rp.n; k++) {
      const tk = opts.seatT ? opts.seatT(k, gt) : gt;
      const pose = A.char_pose(k, tk);
      if (opts.filterPose) opts.filterPose(k, pose);
      this.rigs[k].apply(pose);
      poses.push(pose);
      if (pose.visible !== false && Math.abs(pose.z || 0) < 0.5 && (pose.ground ?? 1) > 0.5) movers.push([pose.x, pose.y, 0 < tk && tk < rp.T ? A.speed(k, tk) : 0]);
      const d = A.dart_state(k, tk), ob = this.darts[k], bl = this.blurs[k];
      if (d && d.mode === 'fly' && d.z < 0.9) movers.push([d.x, d.y, 0.3]);
      if (!d) { ob.visible = false; bl.visible = false; }
      else {
        ob.visible = true;
        const s = d.scale * (d.mode === 'fly' ? 1 : 0.78);
        ob.position.set(d.x, -d.y, d.z);
        ob.rotation.set(d.tilt, d.mode === 'fly' ? 0.12 : d.tilt * 0.6, d.spin);
        ob.scale.setScalar(s);
        setObjColor(ob, 1, 1, 1, 1 - d.glow * 0.6);
        const fly = d.mode === 'fly';
        bl.visible = fly;
        if (fly) { bl.position.set(d.x, -d.y, d.z - 0.01); bl.rotation.set(0, 0, d.spin * 0.37); }
      }
      this.trails[k].update(A.trail(k, tk), this.lin[k], trailStyle);
      const [pts, w] = A.dash_trail(k, tk);
      this.dashes[k].update(w > 0.02 ? pts : [], [1, 1, 1], (j, t, st) => { const v = dashStyle(j, t, st); v[1] *= w; return v; });
      if (this.ghosts) {
        const G = this.ghosts[k];
        const show = gt > 0 && (!opts.ghostSeats || opts.ghostSeats(k));
        if (show) {
          const gp = A.char_pose(k, Math.max(0, gt - delay));
          if (gp.slice || gp.visible === false || (gp.ground ?? 1) < 0.01) { G.rig.setVisible(false); G.line.visible = false; }
          else {
            G.rig.apply({ ...gp, flash: 0, dart: null });
            const P = G.line.geometry.attributes.position.array;
            P[0] = gp.x; P[1] = -gp.y; P[2] = 0.06; P[3] = pose.x; P[4] = -pose.y; P[5] = 0.06;
            G.line.geometry.attributes.position.needsUpdate = true;
            G.line.computeLineDistances();
            G.line.visible = pose.visible !== false && !pose.slice && Math.hypot(gp.x - pose.x, gp.y - pose.y) > 0.08;
          }
        } else { G.rig.setVisible(false); G.line.visible = false; }
      }
    }
    if (this.points) this.points.apply(gt);
    this.fx.apply(A, gt, pieces);
    return { poses, movers };
  }
}
