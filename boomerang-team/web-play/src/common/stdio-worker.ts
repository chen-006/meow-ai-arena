// 跑一个"完整程序"型 bot 的 Web Worker（第 1、2、3 期：选手自己写 main，从标准输入读、往标准输出写）。
//
// bot 用 Asyncify 编译，读标准输入时如果没有数据就挂起（见 native/common/stdio_bot.js）。这个 Worker：
//   → { type: 'load', url }                       载入并启动 main，跑到第一次等输入为止 → { type: 'ready' }
//   → { type: 'input', text, lines, id }          送一段输入；lines > 0 时等 bot 写出这么多行（或者它又开始等输入）
//                                                 → { type: 'reply', id, lines: [...], ms, stderr: [...] }
//   bot 退出或崩溃 → { type: 'exit', message, stderr }
// 每个 Worker 只跑一个 bot、只打一局，和正式赛每局重新启动程序一样。
import R from '@i18n/runtime';

export {};   // 让 TS 把这个文件当模块（不然顶层变量会和别的 Worker 脚本冲突）

const enc = new TextEncoder();
const dec = new TextDecoder();
const MAX_ERR_LINES = 40;

let M: any = null;
let outBuf = '';           // 标准输出里还没凑成一整行的部分
let outLines: string[] = [];
let errBuf = '';
let errLines: string[] = [];
let want = 0, reqId = 0, t0 = 0, pending = false, waiting = false, readySent = false;

function noteErr(text: string) {
  errBuf += text;
  let i;
  while ((i = errBuf.indexOf('\n')) >= 0) {
    const line = errBuf.slice(0, i).replace(/\r$/, '');
    errBuf = errBuf.slice(i + 1);
    if (errLines.length < MAX_ERR_LINES) errLines.push(line.length > 500 ? line.slice(0, 500) + '…' : line);
    else if (errLines.length === MAX_ERR_LINES) errLines.push(R.stderrTruncated);
  }
}

function finish() {
  if (!pending) return;
  pending = false;
  const lines = outLines.splice(0, want || outLines.length);
  self.postMessage({ type: 'reply', id: reqId, lines, ms: performance.now() - t0, stderr: errLines });
  errLines = [];
}

function onOut(fd: number, parts: Uint8Array[]) {
  const text = parts.map(p => dec.decode(p)).join('');
  if (fd === 2) { noteErr(text); return; }
  outBuf += text;
  let i;
  while ((i = outBuf.indexOf('\n')) >= 0) {
    outLines.push(outBuf.slice(0, i).replace(/\r$/, ''));
    outBuf = outBuf.slice(i + 1);
  }
  if (pending && want > 0 && outLines.length >= want) finish();
}

function onExit(message: string) {
  if (outBuf) { outLines.push(outBuf); outBuf = ''; }
  if (pending && outLines.length >= want && want > 0) finish();
  self.postMessage({ type: 'exit', message, stderr: errLines });
  M = null;
}

self.onmessage = async (ev: MessageEvent) => {
  const msg = ev.data;
  try {
    if (msg.type === 'load') {
      const mod = await import(/* @vite-ignore */ msg.url);
      const io = { buf: null as Uint8Array | null, pos: 0, wake: null as ((b: Uint8Array) => void) | null };
      M = await mod.default({
        arenaIO: io,
        arenaOut: onOut,
        arenaWaiting: () => {
          waiting = true;
          // bot 又在等输入了：这一回合它该写的都写完了
          if (pending) finish();
          if (!readySent) { readySent = true; self.postMessage({ type: 'ready' }); }
        },
        print: (t: string) => onOut(1, [enc.encode(t + '\n')]),
        printErr: (t: string) => noteErr(t + '\n'),
        onExit: (code: number) => onExit(R.exitCode(code)),
        onAbort: (what: unknown) => onExit(R.crashed(String(what))),
        quit: (code: number) => onExit(R.exitCode(code)),
      });
      (M as any).__io = io;
      try { M.callMain([]); } catch (e: any) { if (!String(e).includes('unwind')) throw e; }
      // 有的 bot 开头不读输入就先算点东西；等它第一次等输入时才算就绪（arenaWaiting 里发）
    } else if (msg.type === 'input') {
      if (!M) { self.postMessage({ type: 'exit', message: R.exited, stderr: [] }); return; }
      const io = (M as any).__io;
      reqId = msg.id; want = msg.lines | 0; pending = want > 0; t0 = performance.now();
      const data = enc.encode(msg.text);
      if (waiting && io.wake) {
        waiting = false;
        const w = io.wake; io.wake = null;
        w(data);
      } else {
        // bot 还没开始等（不太可能）：接在缓冲区后面
        const old = io.buf && io.pos < io.buf.length ? io.buf.subarray(io.pos) : new Uint8Array(0);
        const all = new Uint8Array(old.length + data.length);
        all.set(old); all.set(data, old.length);
        io.buf = all; io.pos = 0;
      }
      if (!pending) self.postMessage({ type: 'reply', id: reqId, lines: [], ms: 0, stderr: [] });
      else if (outLines.length >= want) finish();
    }
  } catch (e: any) {
    onExit(R.crashed(String(e?.message || e)));
  }
};
