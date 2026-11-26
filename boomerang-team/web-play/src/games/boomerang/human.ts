// 真人操控（为手感重写）：
//   WASD / 方向键 8 向移动；鼠标在飞镖飞行高度的平面上瞄准（无视差）；
//   左键扔镖（按住 = 回手即扔）；右键 / F 近战（持镖挥砍、空手飞踢，引擎按手里有没有镖自己判）；
//   空格 / Shift 冲刺（有移动方向就朝移动方向，站着不动就朝准星）；E / Q 召回掉在地上的镖（自动松开移动，免得一动就打断）。
//   动作缓冲 150 ms（3 步）：按早了（冷却还差一两步、蓄势还没完）不会被吞；引擎真正接受了才从队列里删；
//   冲刺和扔镖 / 近战各占一格队列，不互相覆盖（冲刺移动中接近战照样能出）。
//   触屏：左边摇杆、右边四个大按钮（pointerdown 瞬发）；没有鼠标时自动瞄准最近的对手。
// 采样时机由 match.ts 决定：时钟走到这一拍的那一刻才读当下的按键，不会晚 50～100 ms。
import type { HumanController } from './match';
import type { FrameEvent, ReplayFrame } from './types';

export interface WorldAimProvider {
  screenToWorld(clientX: number, clientY: number): { x: number; y: number } | null;
}

type Act = 1 | 2 | 3 | 4;
const ACT_EVENT: Record<string, Act> = { THROW: 1, SLASH: 2, KICK: 2, DASH: 3, RECALL: 4 };
const BUFFER_STEPS = 3;
const GAME_KEYS = new Set(['w', 'a', 's', 'd', 'arrowup', 'arrowdown', 'arrowleft', 'arrowright', ' ', 'shift', 'e', 'q', 'f', 'j', 'k']);

export class BoomerangHuman implements HumanController {
  private keys = new Set<string>();
  private muted = new Set<string>();          // 按召回时还按着的移动键：松开前不算（免得一按 E 就被自己打断）
  private joyX = 0;
  private joyY = 0;
  private mouse: { x: number; y: number } | null = null;   // 屏幕坐标（每次采样时重新投影，镜头动了也准）
  private aimWorld: { x: number; y: number } | null = null;
  private lastDir: [number, number] = [1000, 0];
  private queue: { act: Act; left: number }[] = [];
  private fireHeld = false;
  private touchMode = false;
  active = false;
  /** 防坠保护：普通走路靠近坑/塌陷边缘时自动沿边滑行，不误踩掉坑（冲刺不受限） */
  edgeGuard = true;
  /** 地图安全判定回调（由页面注入当前对局地形） */
  isSafePos: ((x: number, y: number, frame: ReplayFrame) => boolean) | null = null;
  /** 最近一次采样的结果（技能栏显示"这一下会是挥砍还是飞踢"等用） */
  last = { act: 0, mx: 0, my: 0 };

  private onKeyDown: (e: KeyboardEvent) => void;
  private onKeyUp: (e: KeyboardEvent) => void;
  private onMouseMove: (e: PointerEvent) => void;
  private onDown: (e: PointerEvent) => void;
  private onUp: (e: PointerEvent) => void;
  private onContextMenu: (e: MouseEvent) => void;
  private onBlur: () => void;

  constructor(private stageEl: HTMLElement, private aimProvider: WorldAimProvider) {
    const isTyping = (e: Event) => { const t = (e.target as HTMLElement)?.tagName; return t === 'INPUT' || t === 'TEXTAREA' || t === 'SELECT'; };
    this.onKeyDown = (e) => {
      if (!this.active || isTyping(e)) return;
      const k = e.key.toLowerCase();
      if (!GAME_KEYS.has(k)) return;
      e.preventDefault();
      if (e.repeat) return;
      this.keys.add(k);
      if (k === ' ' || k === 'shift') this.trigger(3);
      else if (k === 'f' || k === 'k') this.trigger(2);
      else if (k === 'j') this.trigger(1);
      else if (k === 'e' || k === 'q') this.trigger(4);
    };
    this.onKeyUp = (e) => {
      const k = e.key.toLowerCase();
      this.keys.delete(k); this.muted.delete(k);
      if (this.active && GAME_KEYS.has(k) && !isTyping(e)) e.preventDefault();
    };
    this.onMouseMove = (e) => {
      if (e.pointerType === 'touch') return;
      this.mouse = { x: e.clientX, y: e.clientY };
      this.touchMode = false;
    };
    this.onDown = (e) => {
      if (!this.active || e.pointerType === 'touch') return;
      const t = e.target as HTMLElement | null;
      if (!t || !this.stageEl.contains(t)) return;
      if (t.closest('.bm-corner, .bm-ov, .bm-touch, button')) return;
      e.preventDefault();
      this.stageEl.focus({ preventScroll: true });
      this.mouse = { x: e.clientX, y: e.clientY };
      this.touchMode = false;
      if (e.button === 0) { this.fireHeld = true; this.trigger(1); }
      else if (e.button === 2) this.trigger(2);
    };
    this.onUp = (e) => { if (e.button === 0) this.fireHeld = false; };
    this.onContextMenu = (e) => { if (this.stageEl.contains(e.target as Node)) e.preventDefault(); };
    this.onBlur = () => { this.keys.clear(); this.muted.clear(); this.joyX = 0; this.joyY = 0; this.fireHeld = false; };
    window.addEventListener('keydown', this.onKeyDown, true);
    window.addEventListener('keyup', this.onKeyUp, true);
    window.addEventListener('pointermove', this.onMouseMove);
    this.stageEl.addEventListener('pointerdown', this.onDown);
    window.addEventListener('pointerup', this.onUp);
    this.stageEl.addEventListener('contextmenu', this.onContextMenu);
    window.addEventListener('blur', this.onBlur);
  }

  /** 1 扔镖 2 近战 3 冲刺 4 召回 */
  trigger(act: Act) {
    if (!this.active) return;
    if (act === 4) {
      for (const k of this.keys) if (['w', 'a', 's', 'd', 'arrowup', 'arrowdown', 'arrowleft', 'arrowright'].includes(k)) this.muted.add(k);
      this.joyHold = true;
    }
    // 冲刺单独一格；扔、砍、召回共用一格（后按的替换先按的）
    const slot = (a: Act) => (a === 3 ? 'd' : 'x');
    this.queue = this.queue.filter((q) => slot(q.act) !== slot(act));
    this.queue.push({ act, left: BUFFER_STEPS });
  }
  private joyHold = false;

  setJoystick(x: number, y: number) {
    this.joyX = Math.max(-1, Math.min(1, x));
    this.joyY = Math.max(-1, Math.min(1, y));
    this.touchMode = true;
    if (Math.hypot(this.joyX, this.joyY) < 0.12) this.joyHold = false;
  }
  setTouchMode() { this.touchMode = true; }
  syncPointer(x: number, y: number) { this.mouse = { x, y }; this.touchMode = false; }
  getAimWorld() { return this.touchMode ? null : this.aimWorld; }
  hasMouse() { return !!this.mouse && !this.touchMode; }

  setEnabled(on: boolean) {
    this.active = on;
    this.keys.clear(); this.muted.clear(); this.queue = []; this.fireHeld = false;
    this.joyX = 0; this.joyY = 0;
    if (on) this.stageEl.focus({ preventScroll: true });
  }

  destroy() {
    window.removeEventListener('keydown', this.onKeyDown, true);
    window.removeEventListener('keyup', this.onKeyUp, true);
    window.removeEventListener('pointermove', this.onMouseMove);
    this.stageEl.removeEventListener('pointerdown', this.onDown);
    window.removeEventListener('pointerup', this.onUp);
    this.stageEl.removeEventListener('contextmenu', this.onContextMenu);
    window.removeEventListener('blur', this.onBlur);
  }

  /** 每帧刷新鼠标对应的地图坐标（镜头跟随时也准） */
  refreshAim() {
    if (this.mouse && !this.touchMode) this.aimWorld = this.aimProvider.screenToWorld(this.mouse.x, this.mouse.y) || this.aimWorld;
  }

  /** 移动方向（-1..1） */
  moveVec(): [number, number] {
    let mx = 0, my = 0;
    const k = (n: string) => this.keys.has(n) && !this.muted.has(n);
    if (k('a') || k('arrowleft')) mx -= 1;
    if (k('d') || k('arrowright')) mx += 1;
    if (k('w') || k('arrowup')) my -= 1;
    if (k('s') || k('arrowdown')) my += 1;
    if (Math.hypot(this.joyX, this.joyY) > 0.12 && !this.joyHold) { mx = this.joyX; my = this.joyY; }
    return [mx, my];
  }

  sampleAction(frame: ReplayFrame, me: number): string {
    const p = frame.players[me];
    if (!p || !p.alive) { this.queue = []; return '0 0 0 0 0 0'; }
    this.refreshAim();
    const [mx, my] = this.moveVec();
    const mLen = Math.hypot(mx, my);
    const imx = mLen > 0.01 ? Math.round((mx / mLen) * 1000) : 0;
    const imy = mLen > 0.01 ? Math.round((my / mLen) * 1000) : 0;
    if (mLen > 0.05) this.lastDir = [imx, imy];
    // 瞄准：鼠标 > （触屏）最近的活着的对手 > 移动方向 > 上次方向
    let [ax, ay] = this.lastDir;
    let aimed = false;
    if (this.aimWorld && !this.touchMode) {
      const dx = (this.aimWorld.x - p.x) * 1000, dy = (this.aimWorld.y - p.y) * 1000, L = Math.hypot(dx, dy);
      if (L > 20) { ax = Math.round(dx / L * 1000); ay = Math.round(dy / L * 1000); aimed = true; }
    }
    if (!aimed && this.touchMode) {
      let best = 1e9;
      frame.players.forEach((q, k) => {
        if (k === me || !q.alive) return;
        if (this.teamOf && this.teamOf(k) === this.teamOf(me)) return;
        const d = Math.hypot(q.x - p.x, q.y - p.y);
        if (d < best && d < 9) { best = d; const L = d || 1; ax = Math.round((q.x - p.x) / L * 1000); ay = Math.round((q.y - p.y) / L * 1000); }
      });
    }
    // 按住左键：手里有镖就扔（回手即扔）
    const dartInHand = frame.darts[me]?.st === 0;
    if (this.fireHeld && dartInHand && !this.queue.some((q) => q.act === 1)) this.trigger(1);
    let act = 0, outMx = imx, outMy = imy;
    // 召回引导中：除非玩家重新按了方向，否则保持站定
    if (p.st === 9 && this.queue.every((q) => q.act === 4)) { outMx = mLen > 0.05 ? imx : 0; outMy = mLen > 0.05 ? imy : 0; }
    if (this.queue.length) {
      // 冲刺移动中（timer 3～6）优先出近战（冲刺接近战）；其余按先来后到
      const q = this.queue.find((x) => x.act === 2 && p.st === 7 && p.timer >= 3 && p.timer <= 6) || this.queue[0];
      act = q.act;
      if (act === 3 && mLen > 0.05) { ax = imx; ay = imy; }
      if (act === 4) { outMx = 0; outMy = 0; }
      for (const x of this.queue) x.left--;
      this.queue = this.queue.filter((x) => x.left > 0);
    }
    // 防坠保护：普通步行靠近坑/塌陷边缘时自动沿切向滑行，避免轻轻一偏就掉下去（冲刺越坑、被踢硬直、已站危险格时不拦）
    if (this.edgeGuard && this.isSafePos && (outMx !== 0 || outMy !== 0) && act !== 3 && p.st !== 7 && p.st !== 8 && p.st !== 9) {
      const safe = this.isSafePos;
      if (safe(p.x, p.y, frame)) {
        const L = Math.hypot(outMx, outMy) || 1;
        const dx = outMx / L, dy = outMy / L;
        const dirOk = (ux: number, uy: number) =>
          safe(p.x + ux * 0.28, p.y + uy * 0.28, frame) && safe(p.x + ux * 0.46, p.y + uy * 0.46, frame);
        if (!dirOk(dx, dy)) {
          const cands: [number, number][] = [];
          if (Math.abs(dx) > 0.15) cands.push([Math.sign(dx), 0]);
          if (Math.abs(dy) > 0.15) cands.push([0, Math.sign(dy)]);
          for (const deg of [30, -30, 45, -45, 60, -60, 75, -75]) {
            const rad = (deg * Math.PI) / 180, c = Math.cos(rad), s = Math.sin(rad);
            cands.push([dx * c - dy * s, dx * s + dy * c]);
          }
          let bestDot = 0.15, bestU: [number, number] | null = null;
          for (const [ux, uy] of cands) {
            const dot = ux * dx + uy * dy;
            if (dot > bestDot && dirOk(ux, uy)) { bestDot = dot; bestU = [ux, uy]; }
          }
          if (bestU) {
            outMx = Math.round(bestU[0] * 1000);
            outMy = Math.round(bestU[1] * 1000);
          } else {
            outMx = 0; outMy = 0;
          }
        }
      }
    }
    this.last = { act, mx: outMx, my: outMy };
    return `${outMx} ${outMy} ${act} ${ax} ${ay} 0`;
  }

  /** 引擎接受了的动作从缓冲里删掉 */
  confirm(events: FrameEvent[], me: number) {
    for (const e of events) {
      if (e.a !== me) continue;
      const a = ACT_EVENT[e.type];
      if (a) this.queue = this.queue.filter((q) => q.act !== a);
    }
  }

  /** 团队局：座位 -> 队号（自动瞄准时不瞄队友） */
  teamOf: ((k: number) => number) | null = null;
}
