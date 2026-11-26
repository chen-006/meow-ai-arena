// 给"完整程序"型 bot（自己写 main、从标准输入读局面、往标准输出写命令）用的 Emscripten JS 库。
//
// 浏览器里没有管道。这里把两个系统调用换掉：
//   fd_read（读标准输入）：异步。缓冲区里有数据就直接给；没有就挂起整个 wasm（Asyncify），
//                         等网页送来下一回合的输入（Module.arenaFeed）再接着跑。网页送来空串表示输入结束（EOF）。
//   fd_write（写标准输出 / 标准错误）：交给 Module.arenaOut(fd, 文本)，由 Worker 按行收集。
// 这样选手的代码一个字都不用改：cin、scanf、getline、getchar 最后都落到 fd_read 上。
addToLibrary({
  fd_read__async: 'auto',
  fd_read: async (fd, iov, iovcnt, pnum) => {
    if (fd !== 0) { HEAPU32[pnum >> 2] = 0; return 0; }
    const io = Module.arenaIO;
    if (!io.buf || io.pos >= io.buf.length) {
      io.buf = await new Promise((resolve) => { io.wake = resolve; Module.arenaWaiting?.(); });
      io.pos = 0;
    }
    let num = 0;
    for (let i = 0; i < iovcnt && io.pos < io.buf.length; i++) {
      const ptr = HEAPU32[(iov + i * 8) >> 2], len = HEAPU32[(iov + i * 8 + 4) >> 2];
      const n = Math.min(len, io.buf.length - io.pos);
      HEAPU8.set(io.buf.subarray(io.pos, io.pos + n), ptr);
      io.pos += n;
      num += n;
    }
    HEAPU32[pnum >> 2] = num;
    return 0;
  },
  fd_write: (fd, iov, iovcnt, pnum) => {
    let num = 0;
    const parts = [];
    for (let i = 0; i < iovcnt; i++) {
      const ptr = HEAPU32[(iov + i * 8) >> 2], len = HEAPU32[(iov + i * 8 + 4) >> 2];
      parts.push(HEAPU8.slice(ptr, ptr + len));
      num += len;
    }
    if (num) Module.arenaOut?.(fd, parts);
    HEAPU32[pnum >> 2] = num;
    return 0;
  },
});
