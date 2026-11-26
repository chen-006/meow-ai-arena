// 第 11–12 期《回旋镖大作战》裁判接口（封装 public/gen/ep11/wasm/engine.mjs）+ 完整回放解析与重算
import { genUrl, loadModule, type EmModule } from '../../common/wasm';
import { decodeReplay, encodeReplay } from '../../common/replay-codec.js';
import { avatarForName, charKeyForName, colorForName, loadMapText } from './data';
import {
  THEME_BY_MAP,
  type ControlState,
  type DartState,
  type FrameEvent,
  type GameMode,
  type GhostPlayer,
  type ParsedReplay,
  type PlayerState,
  type ReplayFrame,
} from './types';

export const FIRST_MS = 1000;
export const STEP_MS = 10;

export interface NewGameConfig {
  mode: GameMode;
  seed: number;
  mapText: string;
  steps?: number;
  delay?: number;
  names?: string[];
  kings?: [number, number];
  spawns?: [number, number][]; // 毫米坐标
  plans?: [string, string];
}

export interface TurnData {
  t: number;
  over: boolean;
  winner: number;
  sLine: string;
  gLine: string;
  obs: string[];
}

export interface StepData {
  over: boolean;
  winner: number;
  ok: boolean[];
  aLine: string;
  evLines: string[];
  actions: number[][];
}

function toHex(str: string): string {
  const bytes = new TextEncoder().encode(str);
  let out = '';
  for (let i = 0; i < bytes.length; i++) out += bytes[i].toString(16).padStart(2, '0');
  return out;
}

export class BoomerangEngine {
  private constructor(private m: EmModule, readonly version: string) {}

  static async load(): Promise<BoomerangEngine> {
    const m = await loadModule(genUrl('ep11/wasm/engine.mjs'));
    const eng = new BoomerangEngine(m, '');
    const r = eng.call('HELLO').trim().split(/\s+/);
    if (r[0] !== 'OK') throw new Error('引擎初始化失败：' + r.join(' '));
    return new BoomerangEngine(m, r[1] || 'dartfu-v3');
  }

  call(req: string): string {
    const p = this.m.stringToNewUTF8(req);
    try {
      return this.m.UTF8ToString(this.m._eng_call(p));
    } finally {
      this.m._free(p);
    }
  }

  private assertOk(r: string, tag: string): string {
    if (!r.startsWith('OK')) throw new Error(`引擎 ${tag} 出错：${r.replace(/^ERR\s*/, '').trim()}`);
    return r;
  }

  newGame(cfg: NewGameConfig): { n: number; nx: number; ny: number; nwave: number; steps: number; delay: number; headerText: string } {
    const lines: string[] = [
      `NEW ${cfg.mode} ${Math.max(0, Math.floor(cfg.seed))} ${cfg.steps ?? -1} ${cfg.delay ?? 4}`,
    ];
    if (cfg.names?.length) {
      lines.push('NAMES ' + cfg.names.map(n => n.replace(/\s+/g, '_')).join(' '));
    }
    if (cfg.kings) {
      lines.push(`KING ${cfg.kings[0]} ${cfg.kings[1]}`);
    }
    if (cfg.spawns?.length) {
      lines.push('SPAWN ' + cfg.spawns.map(([x, y]) => `${Math.round(x)} ${Math.round(y)}`).join(' '));
    }
    if (cfg.plans?.[0]) lines.push('PLAN0 ' + toHex(cfg.plans[0]));
    if (cfg.plans?.[1]) lines.push('PLAN1 ' + toHex(cfg.plans[1]));
    lines.push('MAP');
    lines.push(cfg.mapText.trimEnd());

    const r = this.assertOk(this.call(lines.join('\n')), 'NEW');
    const nl = r.indexOf('\n');
    const first = (nl < 0 ? r : r.slice(0, nl)).split(' ');
    return {
      n: +first[1],
      nx: +first[2],
      ny: +first[3],
      nwave: +first[4],
      steps: +first[5],
      delay: +first[6],
      headerText: nl < 0 ? '' : r.slice(nl + 1),
    };
  }

  header(seat: number, stepMs = STEP_MS, firstMs = FIRST_MS): string {
    return this.call(`HEADER ${seat} ${Math.round(stepMs)} ${Math.round(firstMs)}`);
  }

  turn(): TurnData {
    const r = this.assertOk(this.call('TURN'), 'TURN');
    const lines = r.split('\n');
    const h = lines[0].split(' ');
    const sLine = lines[1] || '';
    const gLine = lines[2] || '';
    const obs: string[] = [];
    let i = 3;
    while (i < lines.length) {
      const ln = lines[i];
      if (!ln) { i++; continue; }
      if (ln.startsWith('OBS ')) {
        const parts = ln.split(' ');
        const cnt = +parts[2];
        obs.push(lines.slice(i + 1, i + 1 + cnt).join('\n') + '\n');
        i += 1 + cnt;
      } else {
        i++;
      }
    }
    return { t: +h[1], over: h[2] === '1', winner: +h[3], sLine, gLine, obs };
  }

  step(replies: string[]): StepData {
    const clean = replies.map(s => (s ?? '-').replace(/[\r\n]/g, ' ').trim() || '-');
    const r = this.assertOk(this.call('STEP\n' + clean.join('\n')), 'STEP');
    const lines = r.split('\n');
    const h = lines[0].split(' ');
    const aLine = lines[1] || '';
    const evLines = lines.slice(2).filter(l => l.startsWith('E '));
    const actions: number[][] = [];
    if (aLine.startsWith('A ')) {
      const segs = aLine.split('|').slice(1);
      for (const seg of segs) {
        const nums = seg.trim().split(/\s+/).map(Number);
        actions.push(nums.length === 6 ? nums : [0, 0, 0, 0, 0, 0]);
      }
    }
    return {
      over: h[1] === '1',
      winner: +h[2],
      ok: h.slice(3).map(x => x === '1'),
      aLine,
      evLines,
      actions,
    };
  }

  retire(seat: number) {
    this.call(`RETIRE ${seat}`);
  }

  end(): string {
    return this.call('END');
  }

  ranks(): { seat: number; rank: number; alive: number; deadT: number; cause: number; killer: number; kills: number; lives: number }[] {
    const r = this.assertOk(this.call('RANKS'), 'RANKS');
    return r.split('\n').slice(1).filter(Boolean).map(ln => {
      const w = ln.trim().split(/\s+/).map(Number);
      return { seat: w[0], rank: w[1], alive: w[2], deadT: w[3], cause: w[4], killer: w[5], kills: w[6], lives: w[7] };
    });
  }
}

// ============================================================================
// 回放文本解析（兼容原版 arena.exe 的 DART-REPLAY 2 格式 + 实时追加帧）
// ============================================================================

function waveVal(ch: string): number {
  const c = ch.charCodeAt(0);
  if (c >= 48 && c <= 57) return c - 48;
  if (c >= 97 && c <= 122) return 10 + (c - 97);
  return -1;
}

export function parseStateLine(line: string, n: number, mode: GameMode, kings: [number, number]): ReplayFrame {
  const parts = line.split('|').map(s => s.trim());
  const head = parts[0].split(/\s+/);
  const t = +head[1];
  const players: PlayerState[] = [];
  for (let k = 0; k < n; k++) {
    const v = (parts[1 + k] || '').split(/\s+/).map(Number);
    players.push({
      alive: v[0] || 0,
      x: (v[1] || 0) / 1000,
      y: (v[2] || 0) / 1000,
      vx: (v[3] || 0) / 1000,
      vy: (v[4] || 0) / 1000,
      face: ((v[5] || 0) / 36000) * Math.PI * 2,
      st: v[6] || 0,
      timer: v[7] || 0,
      ax: v[8] || 0,
      ay: v[9] || 0,
      cd: v[10] || 0,
      expose: v[11] || 0,
      kills: v[12] || 0,
      msg: v[13] || 0,
      visMask: v[14] || 0,
      deadT: v[15] || 0,
      cause: v[16] || 0,
      killer: v[17] ?? -1,
      lives: 0,
      inv: 0,
    });
  }
  const darts: DartState[] = [];
  for (let k = 0; k < n; k++) {
    const v = (parts[1 + n + k] || '').split(/\s+/).map(Number);
    darts.push({
      st: v[0] || 0,
      x: (v[1] || 0) / 1000,
      y: (v[2] || 0) / 1000,
      vx: (v[3] || 0) / 1000,
      vy: (v[4] || 0) / 1000,
      left: v[5] || 0,
      age: v[6] || 0,
    });
  }
  const col = (parts[1 + 2 * n] || '0 -1').split(/\s+/).map(Number);
  const frame: ReplayFrame = {
    t,
    players,
    darts,
    fallen: col[0] || 0,
    warn: col[1] ?? -1,
    events: [],
  };
  const extra = parts[2 + 2 * n];
  if (mode === 4 && extra) {
    const v = extra.split(/\s+/).map(Number);
    frame.control = {
      owner: [v[0] ?? -1, v[3] ?? -1, v[6] ?? -1],
      taking: [v[1] ?? -1, v[4] ?? -1, v[7] ?? -1],
      prog: [v[2] || 0, v[5] || 0, v[8] || 0],
      score: [v[9] || 0, v[10] || 0],
    };
  } else if (mode === 5 && extra) {
    const v = extra.split(/\s+/).map(Number);
    if (kings[0] >= 0 && kings[0] < n) {
      players[kings[0]].lives = v[0] || 0;
      players[kings[0]].inv = v[1] || 0;
    }
    if (kings[1] >= 0 && kings[1] < n) {
      players[kings[1]].lives = v[2] || 0;
      players[kings[1]].inv = v[3] || 0;
    }
  }
  return frame;
}

export function parseGhostLine(line: string, n: number): { ghostT: number; ghosts: GhostPlayer[] } {
  const parts = line.split('|').map(s => s.trim());
  const head = parts[0].split(/\s+/);
  const ghostT = +head[1] || 0;
  const ghosts: GhostPlayer[] = [];
  for (let k = 0; k < n; k++) {
    const v = (parts[1 + k] || '').split(/\s+/).map(Number);
    ghosts.push({
      alive: v[0] || 0,
      x: (v[1] || 0) / 1000,
      y: (v[2] || 0) / 1000,
      face: ((v[3] || 0) / 36000) * Math.PI * 2,
      st: v[4] || 0,
      dartSt: v[5] || 0,
      dartX: (v[6] || 0) / 1000,
      dartY: (v[7] || 0) / 1000,
    });
  }
  return { ghostT, ghosts };
}

export function parseEventLine(line: string): FrameEvent | null {
  const w = line.trim().split(/\s+/);
  if (w[0] !== 'E' || w.length < 7) return null;
  return { t: +w[1], type: w[2], a: +w[3], b: +w[4], c: +w[5], d: +w[6] };
}

export function parseReplayText(text: string): ParsedReplay {
  const lines = text.split(/\r?\n/);
  const cfg: Record<string, number> = {};
  let mode: GameMode = 0;
  let modeName = 'duel';
  let mapName = 'twinlakes';
  let seed = 0;
  let nx = 32, ny = 32, cell = 0.5;
  const grid: number[] = [];
  const wave: number[] = [];
  let waveSteps: number[] = [];
  const points: { name: string; x: number; y: number; r: number }[] = [];
  let kings: [number, number] = [-1, -1];
  const players: ParsedReplay['players'] = [];
  const frames: ReplayFrame[] = [];
  const pendingEvents = new Map<number, FrameEvent[]>();
  let end: ParsedReplay['end'];

  let i = 0;
  while (i < lines.length) {
    const ln = lines[i++].trim();
    if (!ln) continue;
    if (ln.startsWith('CFG ')) {
      for (const kv of ln.slice(4).split(/\s+/)) {
        const eq = kv.indexOf('=');
        if (eq > 0) cfg[kv.slice(0, eq)] = +kv.slice(eq + 1);
      }
    } else if (ln.startsWith('SEED ')) {
      seed = +ln.slice(5);
    } else if (ln.startsWith('MODE ')) {
      const w = ln.split(/\s+/);
      mode = +w[1] as GameMode;
      modeName = w[2] || 'duel';
    } else if (ln.startsWith('MAPNAME ')) {
      mapName = ln.slice(8).trim();
    } else if (ln.startsWith('MAP ')) {
      const w = ln.split(/\s+/);
      nx = +w[1]; ny = +w[2]; cell = (+w[3] || 500) / 1000;
      for (let y = 0; y < ny && i < lines.length; y++) {
        const row = lines[i++].trim();
        for (let x = 0; x < nx; x++) grid.push(row.charCodeAt(x) - 48);
      }
    } else if (ln === 'WAVE') {
      for (let y = 0; y < ny && i < lines.length; y++) {
        const row = lines[i++].trim();
        for (let x = 0; x < nx; x++) wave.push(waveVal(row[x] || '.'));
      }
    } else if (ln.startsWith('WAVES ')) {
      const w = ln.split(/\s+/);
      waveSteps = w.slice(2).map(Number);
    } else if (ln.startsWith('POINTS ')) {
      const w = ln.split(/\s+/);
      const cnt = +w[1];
      for (let k = 0; k < cnt; k++) {
        const b = 2 + k * 4;
        points.push({ name: w[b], x: +w[b + 1] / 1000, y: +w[b + 2] / 1000, r: +w[b + 3] / 1000 });
      }
    } else if (ln.startsWith('KING ')) {
      const w = ln.split(/\s+/);
      kings = [+w[1], +w[2]];
    } else if (ln.startsWith('PLAYER ')) {
      const w = ln.split(/\s+/);
      const seat = +w[1], team = +w[2], name = w.slice(3).join(' ');
      players.push({
        seat, team, name,
        charKey: charKeyForName(name),
        color: colorForName(name),
        avatar: avatarForName(name),
      });
    } else if (ln.startsWith('S ')) {
      const fr = parseStateLine(ln, players.length, mode, kings);
      frames.push(fr);
    } else if (ln.startsWith('E ')) {
      const ev = parseEventLine(ln);
      if (ev) {
        const arr = pendingEvents.get(ev.t) || [];
        arr.push(ev);
        pendingEvents.set(ev.t, arr);
      }
    } else if (ln.startsWith('END ')) {
      const w = ln.split(/\s+/);
      end = { steps: +w[1], winner: +w[2], digest: w[3] || '', reason: w[4] || '', results: [] };
    } else if (ln.startsWith('RESULT ') && end) {
      const w = ln.split(/\s+/).map(Number);
      end.results.push({ seat: w[1], rank: w[2], deadT: w[3], cause: w[4], killer: w[5], kills: w[6] });
    }
  }

  // 把事件挂到对应帧，同时补全 0.2s（delay=4 步前）残影快照！
  const delay = cfg.DELAY ?? 4;
  for (let idx = 0; idx < frames.length; idx++) {
    const f = frames[idx];
    const evs = pendingEvents.get(f.t);
    if (evs) f.events = evs;
    const gIdx = Math.max(0, idx - delay);
    const gf = frames[gIdx];
    f.ghostT = gf.t;
    f.ghosts = gf.players.map((p, k) => ({
      alive: p.alive,
      x: p.x,
      y: p.y,
      face: p.face,
      st: p.st,
      dartSt: gf.darts[k]?.st ?? 0,
      dartX: gf.darts[k]?.x ?? p.x,
      dartY: gf.darts[k]?.y ?? p.y,
    }));
  }

  return {
    mode, modeName, mapName,
    theme: THEME_BY_MAP[mapName] || 'meadow',
    seed, nx, ny, cell, grid, wave, waveSteps, cfg, points, kings, players, frames, end,
    finished: !!end,
  };
}

// ============================================================================
// 分享链接编解码（replay.html#r=...）：种子 + 地图 + 模式 + 逐步动作字节 -> 重算整局
// ============================================================================

export interface ShareReplayHeader {
  v: 1;
  g: 'boomerang';
  mode: GameMode;
  map: string;
  seed: number;
  steps?: number;
  delay?: number;
  bots: (string | null)[];
  names: string[];
  human?: number;
  kings?: [number, number];
  spawns?: [number, number][];
  plans?: [string, string];
}

function packActions(rows: number[][][]): Uint8Array {
  // 每步每人 6 个整数（mx, my, act, ax, ay, msg）用 Int32LE 打包（经 deflate-raw 压缩后极小）
  const n = rows[0]?.length || 0;
  const buf = new ArrayBuffer(rows.length * n * 6 * 4);
  const dv = new DataView(buf);
  let off = 0;
  for (const row of rows) {
    for (let k = 0; k < n; k++) {
      const a = row[k] || [0, 0, 0, 0, 0, 0];
      for (let j = 0; j < 6; j++) {
        dv.setInt32(off, a[j] | 0, true);
        off += 4;
      }
    }
  }
  return new Uint8Array(buf);
}

function unpackActions(bytes: Uint8Array, n: number): number[][][] {
  const stride = n * 6 * 4;
  if (!stride) return [];
  const count = Math.floor(bytes.byteLength / stride);
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const rows: number[][][] = [];
  let off = 0;
  for (let t = 0; t < count; t++) {
    const row: number[][] = [];
    for (let k = 0; k < n; k++) {
      const a: number[] = [];
      for (let j = 0; j < 6; j++) {
        a.push(dv.getInt32(off, true));
        off += 4;
      }
      row.push(a);
    }
    rows.push(row);
  }
  return rows;
}

export async function encodeBoomerangReplay(header: ShareReplayHeader, rows: number[][][]): Promise<string> {
  return encodeReplay(header, packActions(rows));
}

export async function recomputeSharedReplay(eng: BoomerangEngine, code: string): Promise<{ replay: ParsedReplay; header: ShareReplayHeader }> {
  const { header, body: bytes } = await decodeReplay(code);
  const h = header as ShareReplayHeader;
  if (h.g !== 'boomerang') throw new Error('这不是第 11–12 期回旋镖大作战的回放链接');
  const mapText = await loadMapText(h.map);
  const ng = eng.newGame({
    mode: h.mode,
    seed: h.seed,
    mapText,
    steps: h.steps,
    delay: h.delay,
    names: h.names,
    kings: h.kings,
    spawns: h.spawns,
    plans: h.plans,
  });
  let fullText = ng.headerText;
  const rows = unpackActions(bytes, ng.n);
  let over = false;
  for (const row of rows) {
    if (over) break;
    const tr = eng.turn();
    const st = eng.step(row.map(a => a.join(' ')));
    fullText += tr.sLine + '\n' + st.aLine + '\n' + st.evLines.join('\n') + (st.evLines.length ? '\n' : '');
    over = st.over;
  }
  if (over) fullText += eng.end();
  else fullText += eng.turn().sLine + '\n';
  const replay = parseReplayText(fullText);
  if (h.human != null && h.human >= 0) replay.humanSeat = h.human;
  replay.players.forEach((p, idx) => { p.botId = h.bots[idx] ?? undefined; });
  return { replay, header: h };
}
