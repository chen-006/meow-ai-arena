// 主线程这一侧：管一个跑"完整程序"型 bot 的 Worker（stdio-worker.ts）。所有用标准输入输出协议的期通用。
//
//   const bot = await StdioBotHost.create('ep3', 'gpt-6-astra');
//   const r = await bot.ask(本回合的输入文本, 期望的行数, 硬上限毫秒);
//   r.ok ? r.lines : r.message
//
// 超过硬上限还没写完就结束这个 Worker（算程序卡死），之后每次 ask 都直接返回失败。
import R from '@i18n/runtime';
import { genUrl } from './wasm';

export type StdioReply =
  | { ok: true; lines: string[]; ms: number; stderr: string[] }
  | { ok: false; kind: 'exit' | 'timeout'; message: string; stderr: string[]; lines: string[] };

export class StdioBotHost {
  private w: Worker;
  private waiters = new Map<number, (m: any) => void>();
  private nextId = 1;
  private readyResolve: ((m: any) => void) | null = null;
  dead: string | null = null;
  lastStderr: string[] = [];

  private constructor() {
    this.w = new Worker(new URL('./stdio-worker.ts', import.meta.url), { type: 'module' });
    this.w.onmessage = (ev) => this.onMessage(ev.data);
    this.w.onerror = (ev) => this.onMessage({ type: 'exit', message: ev.message || R.workerError, stderr: [] });
  }

  private onMessage(m: any) {
    if (m.type === 'ready') { this.readyResolve?.(m); this.readyResolve = null; return; }
    if (m.type === 'reply') { const f = this.waiters.get(m.id); this.waiters.delete(m.id); f?.(m); return; }
    if (m.type === 'exit') {
      this.dead = m.message;
      this.lastStderr = m.stderr || [];
      this.readyResolve?.(m); this.readyResolve = null;
      for (const f of this.waiters.values()) f(m);
      this.waiters.clear();
    }
  }

  /** 载入 public/gen/<期>/wasm/<id>.mjs，启动程序，等它第一次读标准输入 */
  static async create(episode: string, botId: string, timeoutMs = 60000): Promise<StdioBotHost> {
    const h = new StdioBotHost();
    const r = await new Promise<any>((resolve) => {
      h.readyResolve = resolve;
      setTimeout(() => resolve({ type: 'exit', message: R.loadTimeout }), timeoutMs);
      h.w.postMessage({ type: 'load', url: genUrl(`${episode}/wasm/${botId}.mjs`) });
    });
    if (r.type !== 'ready') { h.terminate(); throw new Error(R.loadFailed(botId, r.message)); }
    return h;
  }

  /** 送一段输入，等 bot 写出 lines 行（lines = 0 表示不等回复）。 */
  ask(text: string, lines: number, hardMs: number): Promise<StdioReply> {
    if (this.dead) return Promise.resolve({ ok: false, kind: 'exit', message: this.dead, stderr: [], lines: [] });
    const id = this.nextId++;
    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        this.waiters.delete(id);
        this.terminate(R.noReply(Math.round(hardMs / 1000)));
        resolve({ ok: false, kind: 'timeout', message: this.dead!, stderr: [], lines: [] });
      }, hardMs);
      this.waiters.set(id, (m) => {
        clearTimeout(timer);
        if (m.type === 'reply') resolve({ ok: true, lines: m.lines, ms: m.ms, stderr: m.stderr || [] });
        else resolve({ ok: false, kind: 'exit', message: m.message, stderr: m.stderr || [], lines: [] });
      });
      this.w.postMessage({ type: 'input', id, text, lines });
    });
  }

  /** 送输入结束（EOF），让程序自己退出 */
  close() {
    if (!this.dead) this.w.postMessage({ type: 'input', id: 0, text: '', lines: 0 });
    setTimeout(() => this.terminate(), 500);
  }

  terminate(why = R.ended) {
    if (!this.dead) this.dead = why;
    this.w.terminate();
  }
}
