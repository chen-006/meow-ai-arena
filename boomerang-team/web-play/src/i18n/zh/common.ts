// 公共文案：页头页脚（common/shell.ts）、普通页面小工具（site.ts、ui.ts）、对局页外壳（arena-kit.ts）、幽默榜联动（bench.ts）。
export default {
  siteName: 'AI 算法对抗大赛',
  /** 共用外壳的站点切换菜单里本站的名字 */
  shortName: '算法对抗赛',
  // ---- 页头 ----
  themeBtn: '切换主题',
  /** 主题按钮的读屏标签（中文版由共用的 meow-shell.js 写，这里只给英文版用） */
  themeName: { system: '跟随系统', light: '浅色', dark: '深色' },
  themeLabel: (name: string) => `主题：${name}，点击切换`,
  /** 语言切换按钮：显示的文字、读屏标签（指向另一种语言） */
  langBtn: 'EN',
  langBtnLabel: 'English version',
  siteNav: '本站导航',
  switchLabel: (cur: string) => `切换 meowllm 站点（当前：${cur}）`,
  switchMenu: 'meowllm 系列站点',
  current: '当前',
  sites: {
    detector: ['模型检测', '看看 API 背后是不是你要的模型'],
    bench: ['幽默榜', '24 个模型比谁更有梗，人人可投票'],
    nav: ['网址导航', 'AI 服务状态、开源工具、社区与榜单'],
    arena: ['算法对抗赛', 'AI 写的 bot 互相对打，你也能下场'],
  },
  // ---- 页脚 ----
  footerTag: 'meow-ai-arena · 第 11–12 期网页可玩版',
  footerFamily: 'meowllm 全家',
  footerMeta: 'AI 算法对抗大赛 · 第 11–12 期回旋镖大作战 · 所有 bot 均由 AI 编写 · 对局在浏览器 WebAssembly 中实时运行 · MIT 许可',
  // 第 10 期起的新形象的署名（主办人 11-12 定的原话，改成只说第 10 期起：往期页面继续用旧形象，不是这个出处）。许可条款等主办人确认推送后再写
  footerArt: '第 10 期起的新角色形象：设计参考自 B 站 @小叶子XIYZ，由节目组制作；美术和制作：喵了个猫了个咪了个猫（往期页面沿用原来的形象）',
  // ---- 对局页顶栏 ----
  crumbsHomeTitle: '算法对抗赛首页',
  crumbsHomeSr: 'meowllm 算法对抗赛首页',
  crumbsLabel: '当前位置',
  crumbsHome: '算法对抗赛',
  mode: { play: '人机对战', watch: 'bot 对打', replay: '回放' },
  // ---- 状态 ----
  loading: '正在载入…',
  error: '出错了',
  reload: '重新载入',
  backTo: (ep: string) => `返回${ep}`,
  thisEpisode: '本期',
  showAll: (n: number) => `展开全部 ${n} 项`,
  // ---- 开局设置 ----
  advanced: '高级设置',
  listSep: '、',
  /** 各期开局设置里"地图与节奏"小标题；只剩节奏一项时改成 pace */
  mapAndPace: '地图与节奏',
  pace: '节奏',
  // ---- 看源码页：长文档的译文说明（中文版不显示，留空） ----
  docTranslated: '',
  docOriginal: '',
  // ---- 幽默榜联动 ----
  benchRank: (rank: number, long: boolean) => `${long ? '这个模型在幽默榜' : '幽默榜'}第 ${rank} 名`,
};
