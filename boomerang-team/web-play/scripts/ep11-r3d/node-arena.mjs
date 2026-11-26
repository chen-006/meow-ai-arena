// 第 11–12 期：在 Node 里用网站同一份 WASM 引擎和 WASM 选手程序跑整局（scripts/verify/ep11.mjs 的宿主代码抽出来复用）。
// runMatch() 返回和网站 / 原版 arena 同格式的 DART-REPLAY 2 回放全文。
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const GEN = path.join(ROOT, 'public', 'gen', 'ep11');
const WASM = path.join(GEN, 'wasm');
const enc = new TextEncoder();
const dec = new TextDecoder();

// ---- Helper: Engine WASM wrapper ----
const engFactory = (await import(pathToFileURL(path.join(WASM, 'engine.mjs')).href)).default;
const engWasm = fs.readFileSync(path.join(WASM, 'engine.wasm'));
const M = await engFactory({
  instantiateWasm(imports, okCb) {
    WebAssembly.instantiate(engWasm, imports).then(r => okCb(r.instance, r.module));
    return {};
  },
});
function engCall(req) {
  const p = M.stringToNewUTF8(req);
  try {
    return M.UTF8ToString(M._eng_call(p));
  } finally {
    M._free(p);
  }
}
function toHex(str) {
  const bytes = enc.encode(str);
  let out = '';
  for (let i = 0; i < bytes.length; i++) out += bytes[i].toString(16).padStart(2, '0');
  return out;
}
function engNewGame({ mode, seed, steps = -1, delay = 4, map, names, kings, spawns, plans }) {
  const lines = [`NEW ${mode} ${seed} ${steps} ${delay}`];
  if (names?.length) lines.push('NAMES ' + names.map(n => n.replace(/\s+/g, '_')).join(' '));
  if (kings) lines.push(`KING ${kings[0]} ${kings[1]}`);
  if (spawns?.length) lines.push('SPAWN ' + spawns.map(([x, y]) => `${Math.round(x)} ${Math.round(y)}`).join(' '));
  if (plans?.[0]) lines.push('PLAN0 ' + toHex(plans[0]));
  if (plans?.[1]) lines.push('PLAN1 ' + toHex(plans[1]));
  lines.push('MAP', map.trimEnd());
  const r = engCall(lines.join('\n'));
  if (!r.startsWith('OK')) throw new Error('NEW failed: ' + r);
  const first = r.split('\n')[0].split(' ');
  const nl = r.indexOf('\n');
  return { n: +first[1], nx: +first[2], ny: +first[3], nwave: +first[4], steps: +first[5], delay: +first[6], headerText: nl < 0 ? '' : r.slice(nl + 1) };
}
function engHeader(seat) {
  return engCall(`HEADER ${seat} 10 1000`);
}
function engTurn() {
  const r = engCall('TURN');
  if (!r.startsWith('OK')) throw new Error('TURN failed: ' + r);
  const lines = r.split('\n');
  const h = lines[0].split(' ');
  const s = lines[1] || '';
  const g = lines[2] || '';
  const inputs = [];
  let i = 3;
  while (i < lines.length) {
    const ln = lines[i];
    if (!ln) { i++; continue; }
    if (ln.startsWith('OBS ')) {
      const cnt = +ln.split(' ')[2];
      inputs.push(lines.slice(i + 1, i + 1 + cnt).join('\n') + '\n');
      i += 1 + cnt;
    } else i++;
  }
  return { t: +h[1], over: h[2] === '1', winner: +h[3], s, g, inputs };
}
function engStep(replies) {
  const r = engCall('STEP\n' + replies.map(x => (x || '-').replace(/[\r\n]/g, ' ').trim() || '-').join('\n'));
  if (!r.startsWith('OK')) throw new Error('STEP failed: ' + r);
  const lines = r.split('\n');
  const h = lines[0].split(' ');
  return {
    over: h[1] === '1',
    winner: +h[2],
    a: lines[1] || '',
    ev: lines.slice(2).filter(l => l.startsWith('E ')),
  };
}
function engEnd() {
  return engCall('END');
}

// ---- Helper: Node Asyncify stdio bot host (mirrors src/common/stdio-worker.ts) ----
const botCache = new Map();
async function loadBotFactory(id) {
  if (!botCache.has(id)) {
    const f = (await import(pathToFileURL(path.join(WASM, `${id}.mjs`)).href)).default;
    const mod = await WebAssembly.compile(fs.readFileSync(path.join(WASM, `${id}.wasm`)));
    botCache.set(id, [f, mod]);
  }
  return botCache.get(id);
}

class NodeStdioBot {
  constructor(id) {
    this.id = id;
    this.M = null;
    this.io = { buf: null, pos: 0, wake: null };
    this.outBuf = '';
    this.outLines = [];
    this.errLines = [];
    this.waiting = false;
    this.exited = null;
    this.waiters = [];
  }

  async init() {
    const [factory, wasmMod] = await loadBotFactory(this.id);
    let readyResolve;
    const readyP = new Promise(r => { readyResolve = r; });
    this.M = await factory({
      instantiateWasm(imports, okCb) {
        WebAssembly.instantiate(wasmMod, imports).then(inst => okCb(inst, wasmMod));
        return {};
      },
      arenaIO: this.io,
      arenaOut: (fd, parts) => {
        const text = parts.map(p => dec.decode(p)).join('');
        if (fd === 2) {
          this.errLines.push(text);
          return;
        }
        this.outBuf += text;
        let idx;
        while ((idx = this.outBuf.indexOf('\n')) >= 0) {
          this.outLines.push(this.outBuf.slice(0, idx).replace(/\r$/, ''));
          this.outBuf = this.outBuf.slice(idx + 1);
        }
        this._flush();
      },
      arenaWaiting: () => {
        this.waiting = true;
        readyResolve();
        this._flush();
      },
      print: (t) => { this.outLines.push(String(t)); this._flush(); },
      printErr: (t) => { this.errLines.push(String(t)); },
      onExit: (c) => { this.exited = `exit(${c})`; readyResolve(); this._flush(); },
      onAbort: (w) => { this.exited = `abort(${w})`; readyResolve(); this._flush(); },
      quit: (c) => { this.exited = `quit(${c})`; readyResolve(); this._flush(); },
    });
    try { this.M.callMain([]); } catch (e) {
      if (!String(e).includes('unwind')) throw e;
    }
    await readyP;
    if (this.exited) throw new Error(`${this.id} exited before input: ${this.exited}`);
  }

  _flush() {
    while (this.waiters.length) {
      const w = this.waiters[0];
      if (this.outLines.length >= w.want || this.exited || (w.want === 0 && this.waiting)) {
        this.waiters.shift();
        w.resolve(this.outLines.splice(0, w.want || this.outLines.length));
      } else break;
    }
  }

  async send(text, wantLines = 0) {
    if (this.exited) return { lines: [], ms: 0, exited: this.exited };
    const t0 = performance.now();
    const data = enc.encode(text);
    const waitP = wantLines > 0
      ? new Promise((resolve) => {
          this.waiters.push({ want: wantLines, resolve });
        })
      : Promise.resolve([]);
    if (this.waiting && this.io.wake) {
      this.waiting = false;
      const wake = this.io.wake;
      this.io.wake = null;
      wake(data);
    } else {
      const old = this.io.buf && this.io.pos < this.io.buf.length ? this.io.buf.subarray(this.io.pos) : new Uint8Array(0);
      const all = new Uint8Array(old.length + data.length);
      all.set(old);
      all.set(data, old.length);
      this.io.buf = all;
      this.io.pos = 0;
    }
    this._flush();
    const lines = await waitP;
    return { lines, ms: performance.now() - t0, exited: this.exited };
  }
}


export { engNewGame, engHeader, engTurn, engStep, engEnd, NodeStdioBot };

export async function runMatch({ mode, mapText, seed, bots, names, kings, spawns, plans, steps = -1 }) {
  const ng = engNewGame({ mode, seed, steps, delay: 4, map: mapText, names, kings, spawns, plans });
  const hosts = [];
  for (const id of bots) { const b = new NodeStdioBot(id); await b.init(); hosts.push(b); }
  let text = ng.headerText;
  let over = false, t = 0;
  while (!over) {
    const tr = engTurn();
    t = tr.t;
    const replies = [];
    for (let p = 0; p < hosts.length; p++) {
      const r = await hosts[p].send((t === 0 ? engHeader(p) : '') + (tr.inputs[p] || ''), 1);
      replies.push(r.lines[0] || '-');
    }
    const st = engStep(replies);
    text += tr.s + '\n' + st.a + '\n' + st.ev.join('\n') + (st.ev.length ? '\n' : '');
    over = st.over;
  }
  text += engEnd();
  return text;
}
