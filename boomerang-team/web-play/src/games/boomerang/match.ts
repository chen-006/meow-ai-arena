// 在浏览器里跑一局第 11–12 期《回旋镖大作战》：
//   - 裁判 WASM（原版 dart.hpp）在主线程；每个 AI 座位一个独立 Web Worker（StdioBotHost / Asyncify）
//   - 1v1（0）、4 人混战（1）、4v4（2）、5v5 歼灭 / 占点 / 刺王（3 / 4 / 5）；真人可以顶任何座位
//
// 节拍（为了手感重写）：
//   - 固定节拍时钟 MatchClock：游戏时间 g = 锚点 + (performance.now() − 锚点时刻) × 20 步/秒 × 倍速，不随 AI 耗时累积漂移；
//     画面（view.ts）每帧读 g，在 t → t+1 两个状态之间插值，所以是连续的 60 fps+。
//   - 引擎总比画面领先一步：时钟走到第 t 步的那一刻，立刻采样真人当下的输入、eng.step()、eng.turn() 得到 t+1 的状态，
//     画面接着从 t 往 t+1 插值——真人按键到画面开始反应只差"离下一拍还有多久"（0～50 ms）。
//   - AI 的思考和节拍重叠：t+1 的观察一出来就发给各 Worker，AI 在画面插值的这 50 ms 里想；时钟走到 t+1 时答复早就到了。
//     万一 AI 还没答完，时钟停在当前状态上等（画面不会冲过头再跳回来）。
//   - 顿帧：clock.hold(ms) 让时钟原地停一会儿（出局、挡镖时由页面调用），引擎同样等着，不会错位。
import { StdioBotHost } from '../../common/stdio-host';
import {
  BoomerangEngine,
  encodeBoomerangReplay,
  FIRST_MS,
  parseEventLine,
  parseGhostLine,
  parseReplayText,
  parseStateLine,
  STEP_MS,
  type ShareReplayHeader,
} from './engine';
import { avatarForName, charKeyForName, colorForName } from './data';
import type { FrameEvent, GameMode, ParsedReplay, ReplayFrame } from './types';

export interface SeatSpec {
  bot?: string;
  human?: boolean;
  name: string;
  charKey?: string;
  color?: string;
  avatar?: string;
}

export interface HumanController {
  /** 时钟走到这一拍的那一刻采样："MX MY ACT AX AY MSG" */
  sampleAction(frame: ReplayFrame, humanSeat: number): string;
  /** 这一步结算完：引擎实际接受了哪些动作（清掉动作缓冲） */
  confirm?(events: FrameEvent[], humanSeat: number): void;
}

export interface MatchConfig {
  mode: GameMode;
  mapId: string;
  mapText: string;
  seed: number;
  steps?: number;
  delay?: number;
  seats: SeatSpec[];
  kings?: [number, number];
  spawns?: [number, number][];
  plans?: [string, string];
  budgetScale: number;
  /** 倍速：1 = 实时 20 步/秒；0 = 极速（算完就走） */
  speed: number;
  /** 开局入场动画占多少步（时钟从 −intro 开始走，走到 0 才算第一步） */
  intro: number;
  human?: HumanController;
  /** 第 0 步状态出来以后、时钟开走以前（页面在这里把 3D 场景准备好） */
  beforeStart?(replay: ParsedReplay): Promise<void>;
  onFrame?(replay: ParsedReplay, frame: ReplayFrame): void;
  onStatus?(text: string): void;
  onEnd?(replay: ParsedReplay): void;
}

export interface SeatRuntimeStats {
  firstMs: number | null;
  avgMs: number | null;
  maxMs: number | null;
  timeouts: number;
  invalid: number;
  status: string;
}

/** 固定节拍时钟（游戏时间，单位：步） */
export class MatchClock {
  private g0 = 0;
  private n0 = performance.now();
  rate = 20;
  paused = false;
  /** 引擎已经算出来的最新一步（时钟不会超过它） */
  maxG = 0;
  fast = false;
  start(g: number) { this.g0 = g; this.n0 = performance.now(); }
  now(): number {
    if (this.fast) return this.maxG;
    const t = performance.now();
    let g = this.paused ? this.g0 : this.g0 + Math.max(0, t - this.n0) * this.rate / 1000;
    if (g > this.maxG) { this.g0 = this.maxG; this.n0 = t; g = this.maxG; }
    return g;
  }
  private anchor() { const g = this.now(); this.g0 = g; this.n0 = performance.now(); return g; }
  setRate(r: number) { this.anchor(); this.rate = r; }
  setPaused(v: boolean) { this.anchor(); this.paused = v; }
  /** 原地停 ms 毫秒（顿帧） */
  hold(ms: number) { if (this.fast || this.paused) return; const g = this.anchor(); this.g0 = g; this.n0 = performance.now() + ms; }
  /** 到游戏时间 g 还要多少毫秒（暂停 = 无穷） */
  msUntil(g: number): number {
    if (this.fast) return 0;
    if (this.paused) return Infinity;
    const t = performance.now();
    return Math.max(0, (g - this.g0) * 1000 / this.rate + this.n0 - t);
  }
  jump(g: number) { this.g0 = g; this.n0 = performance.now(); }
}

const sleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));

export class BoomerangMatch {
  replay: ParsedReplay;
  rows: number[][][] = [];
  t = 0;
  readonly clock = new MatchClock();
  readonly humanSeat: number;
  readonly firstMs: number;
  readonly stepMs: number;
  stats: SeatRuntimeStats[];
  loaded = false;

  private hosts: (StdioBotHost | null)[] = [];
  private stopped = false;
  private stepOnceFlag = false;
  private wake: (() => void) | null = null;
  private sumMs: number[] = [];
  private cntMs: number[] = [];

  constructor(private eng: BoomerangEngine, readonly cfg: MatchConfig) {
    this.firstMs = FIRST_MS * cfg.budgetScale;
    this.stepMs = STEP_MS * cfg.budgetScale;
    this.humanSeat = cfg.seats.findIndex((s) => s.human);
    const n = cfg.seats.length;
    this.hosts = new Array(n).fill(null);
    this.sumMs = new Array(n).fill(0);
    this.cntMs = new Array(n).fill(0);
    this.stats = cfg.seats.map((s) => ({ firstMs: null, avgMs: null, maxMs: null, timeouts: 0, invalid: 0, status: s.human ? 'human' : 'ok' }));
    const ng = eng.newGame({
      mode: cfg.mode, seed: cfg.seed, mapText: cfg.mapText, steps: cfg.steps, delay: cfg.delay ?? 4,
      names: cfg.seats.map((s) => s.name), kings: cfg.kings, spawns: cfg.spawns, plans: cfg.plans,
    });
    this.replay = parseReplayText(ng.headerText);
    this.replay.live = true;
    this.replay.finished = false;
    this.replay.humanSeat = this.humanSeat;
    this.replay.status = '正在载入 AI 程序…';
    this.replay.players.forEach((p, idx) => {
      const sp = cfg.seats[idx];
      if (!sp) return;
      p.name = sp.name;
      p.botId = sp.bot;
      p.charKey = sp.charKey || charKeyForName(sp.bot || sp.name);
      p.color = sp.color || colorForName(sp.bot || sp.name);
      p.avatar = sp.avatar || avatarForName(sp.bot || sp.name);
    });
    this.clock.fast = cfg.speed <= 0;
    this.clock.rate = 20 * (cfg.speed > 0 ? cfg.speed : 1);
    this.clock.start(-cfg.intro);
    this.clock.maxG = -cfg.intro;
  }

  get paused() { return this.clock.paused; }
  set paused(v: boolean) { this.clock.setPaused(v); this.kick(); }
  setSpeed(s: number) {
    this.clock.fast = s <= 0;
    if (s > 0) this.clock.setRate(20 * s);
    this.kick();
  }
  stepOnce() { this.stepOnceFlag = true; this.kick(); }
  stop() { this.stopped = true; for (const h of this.hosts) h?.terminate(); this.kick(); }
  private kick() { const w = this.wake; this.wake = null; w?.(); }

  /** 等到时钟走到 g（暂停时一直等；单步、停止、改倍速会叫醒） */
  private async waitClock(g: number) {
    for (;;) {
      if (this.stopped) return;
      if (this.stepOnceFlag) { this.stepOnceFlag = false; this.clock.jump(g); return; }
      const ms = this.clock.msUntil(g);
      if (ms <= 0.5) return;
      await new Promise<void>((r) => { this.wake = r; if (ms !== Infinity) setTimeout(r, Math.min(ms, 250)); });
      this.wake = null;
    }
  }

  async run(): Promise<ParsedReplay> {
    const { cfg, eng, replay } = this;
    const setStatus = (txt: string) => { replay.status = txt; cfg.onStatus?.(txt); };
    try {
      setStatus(`正在载入 ${cfg.seats.filter((s) => !s.human && s.bot).length} 个 AI 程序…`);
      const indices = cfg.seats.map((s, i) => (!s.human && s.bot ? i : -1)).filter((i) => i >= 0);
      for (let i = 0; i < indices.length; i += 4) {
        if (this.stopped) break;
        const batch = indices.slice(i, i + 4);
        await Promise.all(batch.map(async (idx) => { this.hosts[idx] = await StdioBotHost.create('ep11', cfg.seats[idx].bot!); }));
      }
    } catch (e: any) {
      this.stop();
      setStatus('载入 AI 失败：' + (e?.message || e));
      throw e;
    }
    if (this.stopped) { this.stop(); return replay; }
    this.loaded = true;
    setStatus('');

    // 第 0 步的状态：先拿到、先发给 AI（入场动画的两秒正好给 AI 做开局准备）
    const n = cfg.seats.length;
    let tr = eng.turn();
    const pushFrame = (sLine: string, gLine: string) => {
      const frame = parseStateLine(sLine, n, replay.mode, replay.kings);
      const gh = parseGhostLine(gLine, n);
      frame.ghostT = gh.ghostT; frame.ghosts = gh.ghosts;
      replay.frames.push(frame);
      return frame;
    };
    let frame = pushFrame(tr.sLine, tr.gLine);
    this.clock.maxG = 0;
    if (cfg.beforeStart) await cfg.beforeStart(replay);
    if (this.stopped) { for (const h of this.hosts) h?.terminate(); return replay; }
    this.clock.start(-cfg.intro);
    cfg.onFrame?.(replay, frame);
    const ask = (t: number, obs: string[]) => cfg.seats.map((_, p) => {
      const host = this.hosts[p];
      if (!host) return Promise.resolve(null);
      const first = t === 0;
      const text = (first ? eng.header(p, this.stepMs, this.firstMs) : '') + (obs[p] || '');
      const hard = first ? Math.max(25000, this.firstMs * 8) : Math.max(4000, this.stepMs * 50);
      return host.ask(text, 1, hard);
    });
    let pending = ask(0, tr.obs);
    let over = false;

    while (!this.stopped && !over) {
      const t = tr.t;
      this.t = t;
      const rs = await Promise.all(pending);
      if (this.stopped) break;
      await this.waitClock(t);
      if (this.stopped) break;
      const first = t === 0, limit = first ? this.firstMs : this.stepMs;
      const lines: string[] = [];
      for (let p = 0; p < n; p++) {
        const st = this.stats[p];
        if (p === this.humanSeat) { lines.push(cfg.human ? cfg.human.sampleAction(frame, p) : '0 0 0 0 0 0'); continue; }
        const r = rs[p];
        if (!r) { lines.push('-'); continue; }
        if (!r.ok) {
          st.status = r.kind === 'timeout' ? 'timeout' : 'crash';
          this.hosts[p]?.terminate(r.message);
          this.hosts[p] = null;
          eng.retire(p);
          lines.push('-');
          continue;
        }
        if (first) st.firstMs = r.ms;
        else {
          this.sumMs[p] += r.ms; this.cntMs[p]++;
          st.avgMs = this.sumMs[p] / this.cntMs[p];
          st.maxMs = Math.max(st.maxMs ?? 0, r.ms);
        }
        if (r.ms > limit) st.timeouts++;
        lines.push(r.lines[0] ?? '-');
      }
      const stepRes = eng.step(lines);
      stepRes.ok.forEach((ok, p) => { if (!ok && p !== this.humanSeat && lines[p] !== '-') this.stats[p].invalid++; });
      this.rows.push(stepRes.actions);
      for (const el of stepRes.evLines) { const ev = parseEventLine(el); if (ev) frame.events.push(ev); }
      if (this.humanSeat >= 0) cfg.human?.confirm?.(frame.events, this.humanSeat);
      over = stepRes.over;
      if (over) break;
      tr = eng.turn();
      frame = pushFrame(tr.sLine, tr.gLine);
      this.clock.maxG = tr.t;
      if (this.clock.paused) this.clock.jump(Math.min(this.clock.now(), tr.t));
      pending = ask(tr.t, tr.obs);
      cfg.onFrame?.(replay, frame);
    }
    for (const h of this.hosts) h?.close();
    if (this.stopped && !over) { setStatus('对局已中止'); return replay; }

    // 终局帧 + END / RESULT
    const endLines = eng.end().split(/\r?\n/).filter(Boolean);
    let finalFrame: ReplayFrame | null = null;
    for (const ln of endLines) {
      if (ln.startsWith('S ')) {
        finalFrame = parseStateLine(ln, n, replay.mode, replay.kings);
        const delay = replay.cfg.DELAY ?? 4;
        const gf = replay.frames[Math.max(0, replay.frames.length - delay)] || finalFrame;
        finalFrame.ghostT = gf.t;
        finalFrame.ghosts = gf.players.map((p, k) => ({ alive: p.alive, x: p.x, y: p.y, face: p.face, st: p.st, dartSt: gf.darts[k]?.st ?? 0, dartX: gf.darts[k]?.x ?? p.x, dartY: gf.darts[k]?.y ?? p.y }));
        replay.frames.push(finalFrame);
      } else if (ln.startsWith('END ')) {
        const w = ln.split(/\s+/);
        replay.end = { steps: +w[1], winner: +w[2], digest: w[3] || '', reason: w[4] || '', results: [] };
      } else if (ln.startsWith('RESULT ') && replay.end) {
        const w = ln.split(/\s+/).map(Number);
        replay.end.results.push({ seat: w[1], rank: w[2], deadT: w[3], cause: w[4], killer: w[5], kills: w[6] });
      }
    }
    this.clock.maxG = replay.frames.length - 1;
    replay.live = false;
    replay.finished = true;
    setStatus('');
    if (finalFrame) cfg.onFrame?.(replay, finalFrame);
    cfg.onEnd?.(replay);
    return replay;
  }

  async shareCode(): Promise<string> {
    const h: ShareReplayHeader = {
      v: 1, g: 'boomerang', mode: this.cfg.mode, map: this.cfg.mapId, seed: this.cfg.seed, steps: this.cfg.steps, delay: this.cfg.delay ?? 4,
      bots: this.cfg.seats.map((s) => s.bot ?? null), names: this.cfg.seats.map((s) => s.name), kings: this.cfg.kings, spawns: this.cfg.spawns, plans: this.cfg.plans,
    };
    if (this.humanSeat >= 0) h.human = this.humanSeat;
    return encodeBoomerangReplay(h, this.rows);
  }
}

export { sleep };
