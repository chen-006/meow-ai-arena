// 运行时（bot 的 Worker、宿主、回放编解码）的提示。单独一个模块，Worker 包里只带这一小份。
export default {
  workerError: 'Worker 出错',
  loadFailed: (bot: string, msg: string) => `bot ${bot} 载入失败：${msg}`,
  abiMismatch: (bot: string, mine: string, theirs: string) => `bot ${bot} 和裁判模块不是同一份引擎编出来的（${mine} 对 ${theirs}）`,
  noReply: (sec: number) => `超过 ${sec} 秒没有回话`,
  exited: '程序已经退出',
  loadTimeout: '载入超时',
  ended: '已结束',
  stderrTruncated: '……（这一回合的调试输出太多，后面的省略）',
  notLoaded: 'bot 还没载入',
  exitCode: (code: number) => `程序退出（返回值 ${code}）`,
  crashed: (what: string) => `程序崩溃：${what}`,
  replayIncomplete: '回放数据不完整',
  replayForeign: '不是本站的回放数据',
};
