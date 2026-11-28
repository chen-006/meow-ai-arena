/* =====================================================================
 * rules.js — 裁判规则引擎（UMD，无 DOM / 无第三方依赖）
 * 接口对齐主仓库 arena/core.py 的 Game 类约定：
 *   Game(n, seed) / done / turn / encode(i) / decode(i,line) / step(orders)
 *   header() / frame() / result()
 * 协议 v2（参照 MicroRTS 的三段式 hello / tick / end）：
 *   · 静态全图只在 hello 给一次，之后每回合只发动态状态
 *   · 每回合记录归一化 orders 与帧哈希，支持确定性重放
 *   · 非法动作分两层：结构性错误 → 判故障；语义非法 → 丢弃并记账
 *
 * 本文件是「Bot 对抗」用的完整规则，母舰四项分配是核心决策：
 *   produce / attack / research / move 四个百分比（整数，和为 100），
 *   分别决定：生产槽位数、母舰武器数、研究槽位数、母舰移动速度。
 *   分配按 ALLOC_STEP 逐 tick 过渡，Bot 必须提前规划，不能瞬间切换。
 * 其他单位（LAV/AAV/Railgun/Artillery/Salvager）是执行者：
 *   Salivager 采资源、战斗单位执行 move/attack，能力由科技加成决定。
 * ===================================================================== */
(function (root, factory) {
  const isNode = typeof module === 'object' && module.exports;
  const B = isNode ? require('./ballistics.js') : (root.WoDRef && root.WoDRef.Ballistics);
  const M = isNode ? require('./mapgen.js') : (root.WoDRef && root.WoDRef.MapGen);
  const MC = isNode ? require('./mapgen_mc.js') : (root.WoDRef && root.WoDRef.MapGenMC);
  if (!B) throw new Error('ballistics.js 必须先于 rules.js 加载（<script src="ballistics.js">）');
  if (!M) throw new Error('mapgen.js 必须先于 rules.js 加载（<script src="mapgen.js">）');
  if (!MC) throw new Error('mapgen_mc.js 必须先于 rules.js 加载（<script src="mapgen_mc.js">）');
  const m = factory(B, M, MC);
  if (typeof module === 'object' && module.exports) module.exports = m;
  else { root.WoDRef = root.WoDRef || {}; root.WoDRef.Rules = m; }
})(typeof globalThis !== 'undefined' ? globalThis : this, function (B, M, MC) {
  'use strict';

  /* 网格常数由 mapgen 提供：生成器与规则必须用同一套（曾经各自写死，
   * 改一个忘一个就会让「地图 64×64 / 协议 64×64」对不上）。 */
  const GRID = M.GRID, CELL = M.CELL, WORLD = M.WORLD, N = M.N;
  /* S = 网格密度倍数（256/64 = 4）。规则里所有以「格」为单位的量——
   * 速度、射程、视野、可爬坡差、VP/修理/采集半径——都乘 SCALE，
   * 以保持**物理尺度不变**：地图还是 2.56 km，射程还是 128 m，
   * 只是每格从 40 m 细到 10 m。不乘就会出现「射程砍到 1/4」的荒诞结果。 */
  const SCALE = M.S;
  const SURF = M.SURF, SURF_NAMES = M.SURF_NAMES;
  const TICK_LIMIT = 600;

  /* ---------------------- 时序（引擎节奏 vs 决策预算） ----------------------
   * 决策与 tick 解耦：引擎按自己的节奏推进，Bot 在自己的预算里思考，
   * 决策完成后落到「下一个 tick 边界」执行 —— 也就是说 Bot 在第 T tick 看到的局面，
   * 它的指令最早在第 T+1 tick 才生效，任何情况下都不会「看到就立刻改」。
   *
   * 为什么 tick 取 50ms：600 tick 一局 = 30 秒真实时间，正好是社区赛单局能接受的长度；
   * 20 tick/s 下单位速度 3.2 格/s ≈ 0.16 格/tick，肉眼可跟、日志也可读。
   *
   * 为什么决策预算取 120ms：它比一个 tick（50ms）宽，所以 Bot 允许「跨 tick 思考」，
   * 引擎绝不能因为等 Bot 而掉帧 —— 这正是「解耦」的实质。代价是漏预算即判超时，
   * 因为裁判不能无限等；生产版（Plan_Beta 的 Go driver）是独立进程 + 邮箱，
   * 超预算的指令直接丢弃，不会阻塞引擎。 */
  const TICK_MS = 50;            // 引擎节奏：20 tick/s
  const DECIDE_MS = 120;         // 单次决策硬上限（含编码/解码，与主仓库 0.05s→0.12s 同义）
  const INIT_MS = 2000;          // 开局分析（init）预算：要读完 64×64 地图，给宽一点
  const ORDER_LATENCY = 1;       // 决策在「完成后的下一个 tick」生效，固定 1 tick

  const UNIT_SLOPE = 26 * SCALE, CARRIER_SLOPE = 10 * SCALE;
  /* 平台/工程地面允许的相邻高差（旧代码里到处写死 55，现在统一到这里） */
  const PLATFORM_SLOPE = 55 * SCALE;
  const VP_RADIUS = 4.2 * CELL * SCALE;
  const MAX_LINE = 65536;
  const WEIGHTS = [0.25, 0.15, 0.15, 0.15, 0.10, 0.05, 0.05, 0.10];
  const FIELD_NAMES = ['VP 控制', '歼敌', '保存兵力', '母舰完整度', 'KD 差', '战场控制', '推进效率', '算力表现'];
  const DX = [1, -1, 0, 0, 1, 1, -1, -1];
  const DY = [0, 0, 1, -1, 1, -1, 1, -1];

  /* ---------------------- 单位表 ----------------------
   * rof 单位 tick；cost 是造价（CU）；build 是生产耗时（tick）；pop 是人口。
   * MOTHERSHIP（母舰）是旗舰：四项分配、被摧毁即判负，血量最高。
   * CARRIER（运载舰）是可制造的前线单位：更快、火力更弱、能修理周围单位、能造小单位；
   * 它不参与四项分配，被摧毁也不判负。两者的 dmg 都是「单门武器」伤害。 */
  const TYPES = {
    LAV:        { hp: 90,   speed: 3.2,  range: 3.2, dmg: 8,  rof: 5,  vision: 9,  arc: 'direct', armor: 'light', v0: 265, elev: [-10, 28],  cost: 100, build: 25, pop: 2 },
    AAV:        { hp: 260,  speed: 1.9,  range: 3.0, dmg: 14, rof: 6,  vision: 8,  arc: 'direct', armor: 'heavy', v0: 256, elev: [-10, 28],  cost: 240, build: 45, pop: 3 },
    RAILGUN:    { hp: 110,  speed: 1.6,  range: 9.5, dmg: 60, rof: 20, vision: 10, arc: 'direct', armor: 'light', v0: 490, elev: [-8, 28],  cost: 320, build: 60, pop: 3 },
    ARTILLERY:  { hp: 150,  speed: 1.4,  range: 14,  dmg: 45, rof: 30, vision: 7,  arc: 'lob',    armor: 'light', v0: 628, elev: [-5, 75], cost: 950, build: 90, pop: 5 },
    SALVAGER:   { hp: 130,  speed: 2.2,  range: 0,   dmg: 0,  rof: 0,  vision: 7,  arc: 'none',   armor: 'light', v0: 0,   elev: [0, 0],   cost: 100, build: 20, pop: 1 },
    /* SCOUT：陆地单位。四条属性差异是它的全部设计意图 ——
     * 血最少（40）、跑最快（4.6 格/s）、看得最远（16 格）、一门炮都没有（arc:none）。
     * 它不参与任何火力交换，只负责「把雾掀开」，所以视野是它唯一的产出。 */
    SCOUT:      { hp: 40,   speed: 4.6,  range: 0,   dmg: 0,  rof: 0,  vision: 16, arc: 'none',   armor: 'light', v0: 0,   elev: [0, 0],   cost: 80,  build: 15, pop: 1 },
    MOTHERSHIP: { hp: 3000, speed: 0.55, range: 7,   dmg: 22, rof: 12, vision: 14, arc: 'direct', armor: 'heavy', v0: 408, elev: [-8, 30],  cost: 0,   build: 0,  pop: 0 },
    CARRIER:    { hp: 800,  speed: 1.6,  range: 5,   dmg: 10, rof: 15, vision: 12, arc: 'direct', armor: 'heavy', v0: 337, elev: [-10, 28],  cost: 300, build: 50, pop: 4 }
  };
  /* speed（格/s）、range（格）、vision（格）都是**格**单位，乘 SCALE 之后
   * 物理含义不变（3.2 格/s 在 40 m/格 下是 128 m/s，在 10 m/格 下要写 12.8）。
   * hp/dmg/cost 与网格无关，保持字面量。 */
  for (const k of Object.keys(TYPES)) {
    const t = TYPES[k];
    t.speed *= SCALE; t.range *= SCALE; t.vision *= SCALE;
  }
  const PRODUCIBLE = ['LAV', 'AAV', 'RAILGUN', 'ARTILLERY', 'SALVAGER', 'SCOUT', 'CARRIER'];
  /* 运载舰只能造小单位：它给的是「前线补充」，不是第二个母舰 */
  const SMALL_PRODUCIBLE = ['LAV', 'SALVAGER', 'SCOUT'];
  const RPS = {
    LAV:        { LAV: 1.0, AAV: 0.6, RAILGUN: 1.8, ARTILLERY: 1.6, SALVAGER: 1.3, SCOUT: 1.2, MOTHERSHIP: 0.8, CARRIER: 1.0 },
    AAV:        { LAV: 1.8, AAV: 1.0, RAILGUN: 0.7, ARTILLERY: 0.9, SALVAGER: 1.6, SCOUT: 1.4, MOTHERSHIP: 0.8, CARRIER: 1.2 },
    RAILGUN:    { LAV: 0.6, AAV: 1.8, RAILGUN: 1.0, ARTILLERY: 1.2, SALVAGER: 0.9, SCOUT: 0.8, MOTHERSHIP: 0.8, CARRIER: 1.3 },
    ARTILLERY:  { LAV: 0.8, AAV: 1.1, RAILGUN: 1.3, ARTILLERY: 1.0, SALVAGER: 1.0, SCOUT: 1.4, MOTHERSHIP: 0.8, CARRIER: 1.1 },
    SALVAGER:   { LAV: 0, AAV: 0, RAILGUN: 0, ARTILLERY: 0, SALVAGER: 0, SCOUT: 0, MOTHERSHIP: 0, CARRIER: 0 },
    /* SCOUT 没有任何武器：这一行全 0，写在表里而不是靠 `|| 1` 兜底。
     * 表里显式给 0，`hitUnit` 里 `RPS[src][target] || 1` 会把它变回 1 —— 所以下面
     * 用 `hasOwnProperty` 判断，避免「写了 0 却按 1 算」的静默错误。 */
    SCOUT:      { LAV: 0, AAV: 0, RAILGUN: 0, ARTILLERY: 0, SALVAGER: 0, SCOUT: 0, MOTHERSHIP: 0, CARRIER: 0 },
    MOTHERSHIP: { LAV: 1.2, AAV: 1.0, RAILGUN: 1.0, ARTILLERY: 1.0, SALVAGER: 1.2, SCOUT: 1.3, MOTHERSHIP: 1.0, CARRIER: 1.1 },
    CARRIER:    { LAV: 0.8, AAV: 0.6, RAILGUN: 0.7, ARTILLERY: 0.8, SALVAGER: 1.0, SCOUT: 1.0, MOTHERSHIP: 0.7, CARRIER: 1.0 }
  };

  /* ---------------------- 单位行动模式 ----------------------
   * Bot 只负责调度（去哪、打谁、要不要自己动手），单位的基础逻辑在引擎里：
   *
   *   obey      服从：完全执行 Bot 的 move / attack。视野里出现敌人也不会自己离开位置
   *             （开火不算移动，所以射程内照样还击）。
   *   neutral   中间：按 Bot 的宏观意图走，但途中有限自治 —— 射程内有敌人就停步开火；
   *             自己站得比对手低就被压着打，往后退一段脱离；威胁消失后自动回到原指令。
   *   aggressive 激进：Bot 只给方向，索敌/追击/占位交给引擎。看见敌人就压上去打，
   *             打不到就追，跟出视野就放弃。
   *
   * 三种模式都只能通过 applyMove / fire 行动，所以坡度、山脉碰撞、射程、弹道、视线
   * 这些物理定律永远优先于模式（模式只能决定「想做什么」，不能决定「能做什么」）。
   */
  const MODES = ['obey', 'neutral', 'aggressive'];
  const DEFAULT_MODE = 'obey';          // 默认 = v0.8 行为：只执行 Bot 指令
  const MODE_RETREAT_CELLS = 2 * SCALE;         // neutral 被高差压制时后撤的距离（格）
  const MODE_DESC = {
    obey: '服从：只执行 Bot 的 move/attack，不因视野内出现敌人而自行离开位置',
    neutral: '中间：按 Bot 意图行动，途中遇敌停步开火；被高差压制时后撤脱离，威胁消失后回归',
    aggressive: '激进：Bot 只给方向，索敌/追击/占位由引擎接管，看得见就压上去打'
  };

  /* ---------------------- 母舰四项分配 ----------------------
   * 整数百分比，和恒为 100。
   * 生产 / 研究：**整数槽位**，每 SLOT_STEP(5)% 一档，最多 3 槽
   *   → 0-4%→0，5-9%→1，10-14%→2，≥15%→3（再往上投不增加槽位，属于浪费）
   * 攻击：**武器槽位上限允许小数**（attack/100 × 5，0~5），实际炮数取整；
   * 移动：速度倍率 0.4×~1.6× 连续。
   * 也就是说只有生产/研究是「跳档」的，另外两项是连续的。 */
  const ALLOC_KEYS = ['produce', 'attack', 'research', 'move'];
  const ALLOC_STEP = 5;                 // 每 tick 最多移动 5 个百分点（0→100 需 20 tick）
  const SLOT_STEP = 5;                  // 每 5% 一档
  const MAX_SLOTS = 3;                  // 生产 / 研究各自最多 3 槽
  const MAX_WEAPONS = 5;                // 母舰武器槽位上限（可小数）
  const START_ALLOC = { produce: 15, attack: 35, research: 15, move: 35 };
  const MOVE_MUL_MIN = 0.4, MOVE_MUL_MAX = 1.6;

  /* ---------------------- 经济与人口 ---------------------- */
  const START_CU = 500;
  const BASE_INCOME = 1.5;              // 每 tick 的基础后勤
  const SALVAGE_RATE = 2.0;             // 每台采集车每 tick
  const RES_AMOUNT = 60;                // 每个资源点可采次数
  const RES_RADIUS = 2.4 * CELL * SCALE;
  const POP_MAX = 24;
  /* 武器：走独立队列（不占生产槽），造好一门才计入火力 */
  const WEAPON_COST = 120;
  const WEAPON_BUILD = 20;
  const WEAPON_QUEUE_MAX = 5;
  /* 运载舰的前线修理：半径与速率 */
  const REPAIR_RADIUS = 4 * CELL * SCALE;
  const REPAIR_RATE = 2;                // HP/tick

  /* ---------------------- 科技树 ----------------------
   * 两条线性（fire / armor，各 3 级，单链）
   * 两条交叉（maneuver / industry，各 5 级：2 级二选一分叉 → 4 级汇合 → 5 级终极）
   * 每个节点：tick 研究耗时、cu 研究花费、req 前置、excl 互斥、eff 效果。 */
  const TECH_TREES = {
    fire:     { name: '火力校准', kind: 'linear', levels: 3, desc: '全程提升所有单位伤害' },
    armor:    { name: '复合装甲', kind: 'linear', levels: 3, desc: '全程提升所有单位耐久' },
    maneuver: { name: '机动学说', kind: 'cross', levels: 5, desc: '2 级分叉（速度/射程）→ 4 级汇合 → 5 级双修' },
    industry: { name: '工业体系', kind: 'cross', levels: 5, desc: '2 级分叉（产能/采集）→ 4 级汇合 → 5 级双修' }
  };
  const TECHS = {
    /* 火力树不再直接加伤害，而是改「炮」：初速越高，落速越高，动能伤害越大（∝v²）。
     * F2 解锁高爆弹、F3 解锁硬芯穿甲弹 —— 弹种与初速都由科技决定。 */
    F1: { tree: 'fire', tier: 1, name: '改良发射药 I', tick: 90, cu: 80, req: [], eff: { muzzle: 0.10 } },
    F2: { tree: 'fire', tier: 2, name: '高爆弹头', tick: 150, cu: 140, req: ['F1'], eff: { muzzle: 0.12, unlock: 'HE' } },
    F3: { tree: 'fire', tier: 3, name: '硬芯穿甲弹', tick: 220, cu: 220, req: ['F2'], eff: { muzzle: 0.15, unlock: 'APCR' } },
    A1: { tree: 'armor', tier: 1, name: '复合装甲 I', tick: 90, cu: 80, req: [], eff: { hp: 0.10 } },
    A2: { tree: 'armor', tier: 2, name: '复合装甲 II', tick: 150, cu: 140, req: ['A1'], eff: { hp: 0.12 } },
    A3: { tree: 'armor', tier: 3, name: '复合装甲 III', tick: 220, cu: 220, req: ['A2'], eff: { hp: 0.15 } },
    /* 机动：M2a 速度 / M2b 射程 二选一 → M3 沿分支 → M4 汇合（射速）→ M5 双修 */
    M1:  { tree: 'maneuver', tier: 1, name: '机动学说 I', tick: 80, cu: 70, req: [], eff: { speed: 0.05 } },
    M2a: { tree: 'maneuver', tier: 2, name: '机动 · 疾行', tick: 120, cu: 110, req: ['M1'], excl: ['M2b'], eff: { speed: 0.08 } },
    M2b: { tree: 'maneuver', tier: 2, name: '机动 · 增程', tick: 120, cu: 110, req: ['M1'], excl: ['M2a'], eff: { range: 0.08 } },
    M3a: { tree: 'maneuver', tier: 3, name: '机动 · 突进', tick: 160, cu: 160, req: ['M2a'], eff: { speed: 0.10 } },
    M3b: { tree: 'maneuver', tier: 3, name: '机动 · 校准', tick: 160, cu: 160, req: ['M2b'], eff: { range: 0.10 } },
    M4:  { tree: 'maneuver', tier: 4, name: '机动 · 协同', tick: 210, cu: 200, reqAny: ['M3a', 'M3b'], eff: { rof: 0.10 } },
    M5:  { tree: 'maneuver', tier: 5, name: '机动 · 决胜', tick: 270, cu: 260, req: ['M4'], eff: { speed: 0.10, range: 0.10 } },
    /* 工业：I2a 产能 / I2b 采集 二选一 → I3 沿分支 → I4 汇合（造价）→ I5 双修 */
    I1:  { tree: 'industry', tier: 1, name: '工业体系 I', tick: 80, cu: 70, req: [], eff: { income: 0.20 } },
    I2a: { tree: 'industry', tier: 2, name: '工业 · 流水线', tick: 120, cu: 110, req: ['I1'], excl: ['I2b'], eff: { produce: 0.25 } },
    I2b: { tree: 'industry', tier: 2, name: '工业 · 采掘', tick: 120, cu: 110, req: ['I1'], excl: ['I2a'], eff: { harvest: 0.30 } },
    I3a: { tree: 'industry', tier: 3, name: '工业 · 量产', tick: 160, cu: 160, req: ['I2a'], eff: { produce: 0.25 } },
    I3b: { tree: 'industry', tier: 3, name: '工业 · 精炼', tick: 160, cu: 160, req: ['I2b'], eff: { harvest: 0.30 } },
    I4:  { tree: 'industry', tier: 4, name: '工业 · 标准件', tick: 210, cu: 200, reqAny: ['I3a', 'I3b'], eff: { cost: 0.10 } },
    I5:  { tree: 'industry', tier: 5, name: '工业 · 总动员', tick: 270, cu: 260, req: ['I4'], eff: { produce: 0.20, income: 0.30 } }
  };

  const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
  const ci = (gx, gy) => gy * GRID + gx;
  const gxOf = x => clamp(Math.floor(x / CELL), 0, GRID - 1);
  const gyOf = y => clamp(Math.floor(y / CELL), 0, GRID - 1);
  const dist = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);

  /* 百分比 → 效果。生产/研究是整数槽位（每 5% 一档、封顶 3）；
   * 武器槽位是连续值（0~5，允许小数），实际炮数向下取整；速度连续。 */
  const slotsOf = pct => Math.min(MAX_SLOTS, Math.floor(clamp(pct || 0, 0, 100) / SLOT_STEP));
  const weaponSlotsOf = pct => clamp(pct || 0, 0, 100) / 100 * MAX_WEAPONS;
  const weaponCapOf = pct => Math.floor(weaponSlotsOf(pct) + 1e-9);
  const speedMulOf = pct => MOVE_MUL_MIN + (MOVE_MUL_MAX - MOVE_MUL_MIN) * clamp(pct || 0, 0, 100) / 100;

  /* ---------------------- 规则表冻结（防进程内篡改） ----------------------
   * Bot 与裁判同进程，`require('./rules.js')` 拿到的是同一个模块实例。
   * JSON 深拷贝只保护「消息」，保护不了「共享规则表」与「原型方法」：
   * v0.9 实测只要在 decide 里写 `R.TYPES.LAV.dmg = 999` 或 patch
   * `Game.prototype.encodeTick`，就能改数值/看穿迷雾，而且不触发任何故障。
   *
   * 冻结**不是**安全边界（真正的隔离要独立进程 + 白名单），它的作用是把
   * 「静默改规则」变成「显式报错」，让这类作弊在自测阶段就暴露。
   * 严格模式下写冻结对象会抛 TypeError；非严格模式静默失败 —— 两种都比成功好。
   */
  function deepFreeze(o, seen) {
    if (o == null || (typeof o !== 'object' && typeof o !== 'function')) return o;
    const seenSet = seen || new Set();
    if (seenSet.has(o)) return o;
    seenSet.add(o);
    for (const k of Object.getOwnPropertyNames(o)) {
      const d = Object.getOwnPropertyDescriptor(o, k);
      if (d && 'value' in d) deepFreeze(d.value, seenSet);
    }
    return Object.freeze(o);
  }
  /* 只冻结「规则本身」：数值表、克制表、科技表、弹种表、名单与权重。
   * 运行时状态（sides / units / map）不冻结 —— 那是每局新建的可变对象。 */
  deepFreeze([TYPES, RPS, TECHS, TECH_TREES, START_ALLOC, PRODUCIBLE, SMALL_PRODUCIBLE,
    ALLOC_KEYS, MODES, MODE_DESC, WEIGHTS, FIELD_NAMES]);

  /* ---------------------- 视野：山脉遮挡（LOS）+ 战争迷雾 ----------------------
   * 两项机制共用一个开关 cfg.vision，默认关闭：
   *
   *   undefined / false      → 全关（默认）。协议、帧、渲染、计分与 v0.7 逐字节一致。
   *   true / {}              → 一键启用：更远看不穿山脊（los），且看不到视野外敌人（fog）。
   *   {fog:true,  los:false} → 只按距离算视野，不管地形（近处山后也看得见）
   *   {fog:false, los:true}  → 只算地形遮挡，不按距离（远到 LOS_RANGE 都能看见，山后看不见）
   *
   * 关闭时 encodeTick / frame 不新增任何字段、也不做任何过滤 —— 这是「关闭不改变原有
   * 行为」的实现级保证，由 test/vision_test.js 用改动前的协议基线哈希钉住。
   *
   * 地形全图公开（Plan_Beta §2.3「物理定律不可欺」）：迷雾只隐藏敌方单位与敌弹，
   * 不隐藏地形与高程。
   */
  const EYE_H = B.UNIT_H;        // 视线起点离地高度（m），与炮口同源，避免两套高度
  const LOS_TOL = 0.5;           // 视线容差（m）：同一道沙丘上的单位不能互相挡住
  /* 采样步长必须**小于 1 格**：网格变细后如果按「同样的物理间隔」放大到 2 格，
   * 1 格厚的山脊会被采样点整个跳过（实测视线直接穿山）。所以这里不缩放，
   * 始终保持 0.5 格 —— 精度换 CPU，视线判定不能含糊。 */
  const LOS_STEP = 0.5;
  /* 高度差视野：单位之间没有身高差，只有脚下海拔差。
   * 沿某个方向看过去，只要两端海拔差超过门槛，该方向视距 +2 格（站得不一样高的两端，
   * 视线是斜的，能多看出去一点）；但更高处的地形/山体仍然照常拦截（LOS 先生效）。 */
  const HEIGHT_BONUS_CELLS = 2 * SCALE;
  const HEIGHT_DIFF_MIN = 1.0;   // 「存在高度差」的门槛（m）≈ 3 级高程，滤掉微起伏
  /* 只开 LOS 时没有「视距半径」可用，这里给一个硬上限：
   * 既让「山后看不见」成立，又不至于每 tick 对全图 4096 格做 O(N²) 射线。 */
  const LOS_RANGE_CELLS = 32 * SCALE;
  const VISION_OFF = { enabled: false, fog: false, los: false };

  /* 统一开关的归一化：所有入口（Game / header / rulesHash / visionGrid）都过这一层，
   * 保证「怎么写开关」不会在不同地方解释成两种含义。 */
  function normalizeVision(v) {
    if (v === true) return { enabled: true, fog: true, los: true };
    if (v == null || v === false) return Object.assign({}, VISION_OFF);
    if (typeof v !== 'object') return Object.assign({}, VISION_OFF);
    const fog = v.fog !== false, los = v.los !== false;
    /* 显式 enabled:false 或两项都关 → 视为关闭，避免留下一个「开着但什么都没变」的状态 */
    const enabled = v.enabled === false ? false : (fog || los);
    return { enabled, fog: enabled && fog, los: enabled && los };
  }

  /* 某一格的地形高度（m）。山脊按 MOUNTAIN_H 实体处理，与弹道遮挡同一口径：
   * 眼里的山和炮弹眼里的山必须是同一座，否则「打得到但看不见」会变成诡异体验。 */
  function terrainHeightAt(map, x, y) {
    const idx = ci(gxOf(x), gyOf(y));
    return map.base[idx] === 3 ? B.MOUNTAIN_H : map.elev[idx] * B.ELEV_M;
  }

  /* 视线是否通畅：沿两点连线按 LOS_STEP 采样，比较「地形高度」与「视线插值高度」。
   *
   * 判定是**统一**的：山脉（base===3 → MOUNTAIN_H）与普通地形（elev × 0.30）走同一套比较，
   * 谁高谁挡。所以「一道 66 m 的高沙丘」和「55 m 的山脊」在遮挡上没有身份差别 ——
   * 差别只在数值，不在规则分支。
   *
   * 三个细节缺一不可：
   *   1) 方向规范化：始终从字典序更小的端点出发，让 A→B 与 B→A 逐位相同。
   *      数学上镜像采样的插值高度本来就相等，但浮点下两端算出的坐标可能差最后几个比特，
   *      在格子边线上有极小概率落进不同格。规范化把「对称」从巧合变成保证
   *      （变异测试显示这一条几乎观察不到差异 —— 它守的是边界情况，不是常态）。
   *   2) 不取端点（t=1 是目标脚下的地面），否则站在沙丘上的单位会挡住自己；
   *   3) 跳过「起点格 / 终点格」内的采样点 —— 山脊格本身高 55 m，若拿自己的高度跟
   *      插值视线比，会得出「看不见山脊」这种结论。物理上你永远看得见那一格的近侧面，
   *      看不见的是它**背后**。 */
  function losClear(map, ax, ay, bx, by) {
    /* 规范化：让 (ax,ay) 永远是字典序较小的那个端点，保证严格对称 */
    if (ax > bx || (ax === bx && ay > by)) {
      const tx = ax, ty = ay; ax = bx; ay = by; bx = tx; by = ty;
    }
    const dx = bx - ax, dy = by - ay;
    const dpx = Math.hypot(dx, dy);
    if (dpx < 1e-6) return true;
    const aIdx = ci(gxOf(ax), gyOf(ay)), bIdx = ci(gxOf(bx), gyOf(by));
    const h0 = terrainHeightAt(map, ax, ay) + EYE_H;
    const h1 = terrainHeightAt(map, bx, by) + EYE_H;
    const steps = Math.max(1, Math.ceil((dpx / CELL) / LOS_STEP));
    for (let i = 1; i < steps; i++) {
      const t = i / steps;
      const x = ax + dx * t, y = ay + dy * t;
      const idx = ci(gxOf(x), gyOf(y));
      if (idx === aIdx || idx === bIdx) continue;
      const line = h0 + (h1 - h0) * t;
      if (terrainHeightAt(map, x, y) > line + LOS_TOL) return false;
    }
    return true;
  }

  /* 两端海拔差（m）。单位没有身高，所以「高度差」就是脚下地形高度之差。 */
  function heightDiffAt(map, ax, ay, bx, by) {
    return terrainHeightAt(map, bx, by) - terrainHeightAt(map, ax, ay);
  }

  /* 某个方向上视野该加多少格：存在高度差就 +2，否则 0。
   * 注意这只加「看多远」，不加「看不看得穿」—— 更高的阻挡仍然由 losClear 拦截。 */
  function heightBonusAt(map, ax, ay, bx, by) {
    return Math.abs(heightDiffAt(map, ax, ay, bx, by)) >= HEIGHT_DIFF_MIN ? HEIGHT_BONUS_CELLS : 0;
  }

  /* 某个观察者能看到多远（格）：开雾用单位视距，只开 LOS 用硬上限 */
  function visionRangeOf(kind, v) {
    const r = (TYPES[kind] && TYPES[kind].vision) || 0;
    return v.fog ? r : LOS_RANGE_CELLS;
  }

  /* 单个点是否在 side 的视野内。units 传当前存活单位即可（引擎与网页共用同一份实现）。
   * 这是「单位/炮弹可见性」的唯一判定入口，不要在别处再写一套。
   *
   * 顺序：先算「基础视距 + 高度差加成」，再判距离，最后判遮挡。
   * 加成只能在距离上帮忙，绝不能让一条被山脊切断的视线复活。 */
  function canSee(map, units, side, target, vision) {
    const v = normalizeVision(vision);
    if (!v.enabled) return true;
    for (const u of units) {
      if (!u || u.dead || u.side !== side) continue;
      /* 只判遮挡（fog 关）时没有距离约束，也就不需要高度差加成 */
      const rad = visionRangeOf(u.kind, v) +
        (v.fog ? heightBonusAt(map, u.x, u.y, target.x, target.y) : 0);
      if (v.fog && Math.hypot(u.x - target.x, u.y - target.y) > rad * CELL) continue;
      if (!v.los || losClear(map, u.x, u.y, target.x, target.y)) return true;
    }
    return false;
  }

  /* 空中目标（在飞的炮弹）只按视距判：高抛弹本来就在山脊之上，
   * 用地形遮挡去判会把「明明看得见的越顶弹」误判成不可见。 */
  function pointVisible(map, units, side, x, y, vision) {
    const v = normalizeVision(vision);
    if (!v.enabled) return true;
    for (const u of units) {
      if (!u || u.dead || u.side !== side) continue;
      const rad = visionRangeOf(u.kind, v);
      if (Math.hypot(u.x - x, u.y - y) <= rad * CELL) return true;
    }
    return false;
  }

  /* 可见性网格：Uint8Array(N)，1 = 该格对该方可见。
   * 只给「看」用（网页迷雾、测试、以后的小地图），引擎每 tick 只做 canSee 的点判定，
   * 所以这张网格的开销不影响对局速度。关闭视野时全 1，等价于「处处可见」。 */
  function visionGrid(map, units, side, vision) {
    const v = normalizeVision(vision);
    const out = new Uint8Array(N);
    if (!v.enabled) { out.fill(1); return out; }
    /* 高度差加成是逐格算的，循环边界要按「最大可能半径」放宽，否则加成永远看不到 */
    const reach = v.fog ? HEIGHT_BONUS_CELLS : 0;
    for (const u of units) {
      if (!u || u.dead || u.side !== side) continue;
      const base = visionRangeOf(u.kind, v);
      const gx0 = gxOf(u.x), gy0 = gyOf(u.y);
      /* 自己脚下永远可见（半径 0 的单位，或者雾被关掉时，也能看到身边一圈） */
      out[ci(gx0, gy0)] = 1;
      const rad = base + reach;
      for (let gy = Math.max(0, gy0 - rad); gy <= Math.min(GRID - 1, gy0 + rad); gy++) {
        for (let gx = Math.max(0, gx0 - rad); gx <= Math.min(GRID - 1, gx0 + rad); gx++) {
          const idx = ci(gx, gy);
          if (out[idx]) continue;
          const cx = gx * CELL + CELL / 2, cy = gy * CELL + CELL / 2;
          if (v.fog) {
            const rr = base + heightBonusAt(map, u.x, u.y, cx, cy);
            if (Math.hypot(cx - u.x, cy - u.y) > rr * CELL) continue;
          }
          if (v.los && !losClear(map, u.x, u.y, cx, cy)) continue;
          out[idx] = 1;
        }
      }
    }
    return out;
  }

  /* ---------------------- 协议指纹 ---------------------- */
  const PROTO = { name: 'wod-arena', version: 5 };

  function fnv1a(s) {
    let h = 2166136261;
    for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); }
    return (h >>> 0).toString(16);
  }
  /* 规则指纹覆盖一切会影响胜负的常量与数值表；改了其中任何一个，hello 就会对不上。
   * 时序常量也进指纹：tick 长度与决策预算直接决定「Bot 有多快能改主意」，
   * 同一份 Bot 在不同时序下胜率会变，所以它属于规则，不属于驱动配置。 */
  function rulesHash(tickLimit, vision) {
    const v = normalizeVision(vision);
    const body = {
      proto: PROTO, GRID, CELL, tickLimit, UNIT_SLOPE, CARRIER_SLOPE,
      VP_RADIUS, WEIGHTS, TYPES, RPS, PRODUCIBLE, SMALL_PRODUCIBLE,
      ALLOC_KEYS, ALLOC_STEP, SLOT_STEP, MAX_SLOTS, MAX_WEAPONS, START_ALLOC,
      MOVE_MUL_MIN, MOVE_MUL_MAX,
      START_CU, BASE_INCOME, SALVAGE_RATE, RES_AMOUNT, POP_MAX,
      WEAPON_COST, WEAPON_BUILD, WEAPON_QUEUE_MAX, REPAIR_RADIUS, REPAIR_RATE,
      TICK_MS, DECIDE_MS, ORDER_LATENCY,
      /* 弹道常数进指纹：阻力、重力、尺度、山体高度、弹种表，改一个就换一个游戏 */
      AIR_K: B.AIR_K, G: B.G, M_PER_CELL: B.M_PER_CELL, ELEV_M: B.ELEV_M,
      MOUNTAIN_H: B.MOUNTAIN_H, HIT_R_M: B.HIT_R_M, SHELLS: B.SHELLS,
      /* 视野与行为规则：高度差加成、模式集合、默认模式、后撤距离都影响胜负 */
      HEIGHT_BONUS_CELLS, HEIGHT_DIFF_MIN, MODES, DEFAULT_MODE, MODE_RETREAT_CELLS,
      TECHS, TECH_TREES
    };
    /* 视野开关只有在**开启**时才进指纹：
     * 关闭 = 规则与 v0.7 完全相同，指纹也必须相同（老 Bot 的 rulesHash 校验不能无端失败）。
     * 开启 = 信息集变了，是一套不同的规则，必须换指纹，否则「同指纹不同规则」会骗到人。 */
    if (v.enabled) body.vision = { enabled: v.enabled, fog: v.fog, los: v.los, eyeH: EYE_H, losRange: LOS_RANGE_CELLS };
    return fnv1a(JSON.stringify(body));
  }

  /* 协议长度按 UTF-8 字节计算，而不是 JS 的 UTF-16 code unit 数。 */
  function utf8Length(s) {
    const str = String(s);
    if (typeof TextEncoder !== 'undefined') return new TextEncoder().encode(str).length;
    if (typeof Buffer !== 'undefined') return Buffer.byteLength(str, 'utf8');
    let n = 0;
    for (const ch of str) {
      const cp = ch.codePointAt(0);
      n += cp <= 0x7f ? 1 : (cp <= 0x7ff ? 2 : (cp <= 0xffff ? 3 : 4));
    }
    return n;
  }

  /* 地图生成整体搬到了 mapgen.js（地貌过程 + VP/资源点搜索）。
   * 这里只保留一层「按规则参数调用」的薄封装：地图生成必须知道
   * 单位能爬多陡（UNIT_SLOPE），所以把规则侧的常数传下去，
   * 而不是让 mapgen 自己抄一份阈值（两份阈值迟早会漂）。
   *
   * 为什么不再把生成算法留在 rules.js：它已经从「噪声 + 常量坐标」长成
   * 「构造地貌 → 水系 → 风成 → 分类 → 目标点搜索」，和协议/规则/战斗放一起
   * 会让 1800 行的引擎更难读；而且 512² 迁移时生成器要整体替换。 */
  /* 两套生成器并存：minecraft（多噪声气候参数 + 样条整形 + 群系 + 峡谷/洞穴/矿脉）
   * 与 geomorphic（构造地貌 + 河道生长 + 风成沙丘）。默认 minecraft。
   * 选哪套只影响「地形怎么来」，通行/VP/协议/公平性完全共用一套收尾流程。 */
  const GENERATORS = { minecraft: MC, geomorphic: M };
  const GENERATOR_DEFAULT = 'minecraft';
  function generatorOf(name) {
    const g = GENERATORS[name || GENERATOR_DEFAULT];
    if (!g) throw new Error('未知的地图生成器：' + name + '（可选 ' + Object.keys(GENERATORS).join(' / ') + '）');
    return g;
  }
  const MAP_OPTS = gen => ({
    unitSlope: UNIT_SLOPE,
    carrierSlope: CARRIER_SLOPE,
    vpRadiusCells: VP_RADIUS / CELL,
    vpCount: 4,
    resCount: 4,
    generator: gen || GENERATOR_DEFAULT
  });
  const surfString = surf => M.surfString(surf);
  const seedSalt = (seed, attempt) => M.seedSalt(seed, attempt);
  const buildOnce = (salt, opts) => {
    const o = Object.assign(MAP_OPTS(opts && opts.generator), opts || {});
    return generatorOf(o.generator).buildOnce(salt, o);
  };
  const generateMap = (seed, opts) => {
    const o = Object.assign(MAP_OPTS(opts && opts.generator), opts || {});
    return generatorOf(o.generator).generateMap(seed, o);
  };
  const connected = map => M.connected(map, MAP_OPTS());
  const spawnConnected = map => M.spawnConnected(map, MAP_OPTS());
  const carveCorridor = (map, width) => M.carveCorridor(map, width);
  const wallRatio = map => M.wallRatio(map);

  /* 视线判定已删除：v5 起「打不打得到」由弹道积分回答（飞过去会不会撞山），
   * 不再用 2D 射线近似。旧的 hasLOS 保留在 git 历史里（v0.6）。 */

  function stepAllowed(map, u, fx, fy, tx, ty) {
    const gi = ci(gxOf(tx), gyOf(ty));
    if (map.base[gi] === 3) return false;
    const hi = ci(gxOf(fx), gyOf(fy));
    const lim = u.kind === 'CARRIER' ? CARRIER_SLOPE : UNIT_SLOPE;
    const eff = (map.base[gi] === 4 || map.base[hi] === 4) ? Math.max(lim, 55) : lim;
    return Math.abs(map.elev[gi] - map.elev[hi]) <= eff;
  }

  /* ---------------------------- Game ---------------------------- */
  class Game {
    constructor(n, seed, cfg) {
      /* 本原型只实现了 2 方规则。主仓库 run_match() 传的是 len(commands)，
       * 静默把 4 人混战当成 1v1 跑完比直接报错危险得多，所以这里显式拒绝。 */
      if (n != null && n !== 2) throw new Error('Desert Arena 原型只支持 2 方，收到 ' + n + ' 方');
      this.cfg = Object.assign({
        tickLimit: TICK_LIMIT, tickMs: TICK_MS, decideMs: DECIDE_MS, initMs: INIT_MS,
        generator: GENERATOR_DEFAULT
      }, cfg || {});
      if (!GENERATORS[this.cfg.generator]) {
        throw new Error('未知的地图生成器：' + this.cfg.generator);
      }
      /* 允许调用方只传一部分配置：缺项必须回落到默认值，不能被 undefined 顶掉 */
      if (this.cfg.tickLimit == null) this.cfg.tickLimit = TICK_LIMIT;
      if (!(this.cfg.tickMs > 0)) this.cfg.tickMs = TICK_MS;
      if (!(this.cfg.decideMs > 0)) this.cfg.decideMs = DECIDE_MS;
      if (!(this.cfg.initMs > 0)) this.cfg.initMs = INIT_MS;
      /* 视野统一开关：两个机制共用一个入口，默认关闭（关闭时行为与 v0.7 完全一致） */
      this.vision = normalizeVision(this.cfg.vision);
      this.cfg.vision = this.vision;
      const baseSeed = seed == null ? 20261109 : (seed | 0);
      this.map = generateMap(baseSeed, { generator: this.cfg.generator });
      this.seed = this.map.seed;          // = 调用方给的 requested seed，不再被改写
      this.players = 2;
      this.turn = 0;
      this.units = []; this.nextId = 1;
      this.vps = this.map.vpCells.map((c, i) => ({ id: i, x: c[0] * CELL + CELL / 2, y: c[1] * CELL + CELL / 2, owner: -1, contested: false }));
      this.resources = this.map.resCells.map((c, i) => ({ id: i, x: c[0] * CELL + CELL / 2, y: c[1] * CELL + CELL / 2, amount: RES_AMOUNT, acc: 0 }));
      this.sides = [0, 1].map(() => ({
        cu: START_CU, pop: 0,
        vpScore: 0, vpTicks: 0, kills: 0, losses: 0,
        /* 弹道统计：打了多少发、直击多少发（只看直击，溅射不算） */
        shots: 0, hits: 0,
        killValue: 0, producedCost: 0, initialCost: 0, controlTicks: 0,
        /* 母舰分配：alloc 是当前值（逐 tick 过渡），target 是指令目标 */
        alloc: Object.assign({}, START_ALLOC),
        target: Object.assign({}, START_ALLOC),
        /* 科技：done 已解锁；doing 正在研究（长度 ≤ 研究槽位） */
        techDone: {}, techDoing: [],
        /* 生产：queue 待开工，active 正在生产（长度 ≤ 生产槽位） */
        queue: [], active: [],
        /* 全队默认行动模式；单台单位可在 u.mode 上覆盖 */
        mode: DEFAULT_MODE,
        /* 武器：独立队列，一门一门造；ready 是已经装好的炮数 */
        weaponsReady: 0, weaponQueue: [],
        /* 科技加成累计（乘法/加法见 applyTech） */
        /* muzzle：发射药科技给的初速加成；dmg 已废弃（伤害由弹道算），保留键位避免旧存档报错 */
        mods: { muzzle: 0, dmg: 0, hp: 0, range: 0, speed: 0, rof: 0, income: 0, produce: 0, harvest: 0, cost: 0 }
      }));
      this.botMs = [0, 0]; this.botMax = [0, 0]; this.botCalls = [0, 0]; this.botFaults = [0, 0];
      /* late：决策耗时超过一个 tick 的次数。它不影响判定（指令照样在下一 tick 生效），
       * 但它是「这个 Bot 的真实算力跟不上 20 tick/s」的证据，进成绩表供人看。 */
      this.botLate = [0, 0];
      this.events = []; this.lastEvents = [];
      /* 在飞的炮弹：{s,h,vx,vh,t,...}，与单位一样参与帧哈希 */
      this.projectiles = []; this.nextShot = 1;
      this.resultData = null; this.forfeited = -1;
      /* 确定性基础设施：逐回合 orders + 逐回合帧哈希，非法动作单独记录 */
      this.orderLog = []; this.frameHashes = []; this.rejected = [];
      /* 初始兵力：母舰（MOTHERSHIP）+ 2 LAV。运载舰要靠自己造。 */
      for (let s = 0; s < 2; s++) {
        const sp = this.map.spawns[s], cx = sp.gx * CELL + CELL / 2, cy = sp.gy * CELL + CELL / 2;
        this.spawn(s, 'MOTHERSHIP', cx, cy);
        for (let i = 0; i < 2; i++) {
          const a = Math.PI * s + (i / 2) * Math.PI * 2;
          this.spawn(s, 'LAV', cx + Math.cos(a) * CELL * 2.8 * SCALE, cy + Math.sin(a) * CELL * 2.8 * SCALE);
        }
      }
      this.frameHashes.push(this.hashFrame());
    }
    get done() { return !!this.resultData; }
    spawn(side, kind, x, y) {
      const t = TYPES[kind];
      const S = this.sides[side];
      const maxHp = Math.round(t.hp * (1 + S.mods.hp));
      const u = {
        id: this.nextId++, side, kind, x, y, hp: maxHp, maxHp, cd: 0, dead: false, order: null,
        /* 行动模式：默认服从（只执行 Bot 指令）。新造出来的单位继承本方当前默认模式，
         * 之后 Bot 可以用 {type:'mode'} 单独改某一台。 */
        mode: S.mode || DEFAULT_MODE,
        /* 本拍由引擎行为层算出来的开火优先目标（null = 没有） */
        aiTarget: null,
        /* 上一 tick 的位置：用来估目标速度，弹道解算要给提前量 */
        px: x, py: y, vx: 0, vy: 0,
        /* 运载舰自带 1 个生产槽：队列入挂在单位上，不占母舰的槽位 */
        queue: kind === 'CARRIER' ? [] : null
      };
      this.units.push(u);
      if (kind !== 'MOTHERSHIP') {
        S.pop += t.pop;
        S.initialCost += t.cost;
        S.producedCost += t.cost;
      }
      return u;
    }
    mothership(s) { return this.units.find(u => u.side === s && u.kind === 'MOTHERSHIP' && !u.dead) || null; }
    /* @deprecated 兼容旧名字：carrier() 返回的是**母舰**，不是 CARRIER（运载舰）。
     * 这两个概念在 v0.5 被拆开后，这个名字就一直是误导源（测试里曾用 carrier().hp=0
     * 打母舰、读的人以为是运载舰）。新代码一律用 mothership()，这个别名留给旧脚本。 */
    carrier(s) { return this.mothership(s); }
    noteBotTime(seat, ms) {
      this.botMs[seat] += ms;
      this.botCalls[seat]++;
      if (ms > this.botMax[seat]) this.botMax[seat] = ms;
      /* 超过一个 tick 的思考时间：异步驱动下这条指令会晚到，记一笔但不改判定 */
      if (ms > this.cfg.tickMs) this.botLate[seat]++;
    }
    noteFault(seat) { this.botFaults[seat]++; }

    /* ---------------- 单位行为层（obey / neutral / aggressive） ----------------
     * Bot 负责「调度」（去哪、打谁、要不要自己动手），引擎负责「基础逻辑」。
     * 这一层只产出两样东西：这一拍想移动到哪、想优先打谁；真正执行仍然走
     * applyMove（坡度/山脉/速度）与 fire（射程/弹道/视线），所以物理定律永远最高优。 */

    /* 这个单位打不打得了仗：SCOUT / SALVAGER 这类 arc==='none' 的单位直接不参与索敌 */
    canFightKind(kind) {
      const t = TYPES[kind];
      return !!t && t.arc !== 'none' && t.range > 0 && t.dmg > 0;
    }
    /* 射程内（含科技加成）。只做距离判断，弹道能不能解由 fire() 决定 */
    inWeaponRange(u, e) {
      const t = TYPES[u.kind];
      if (!t || t.arc === 'none' || t.range <= 0) return false;
      return Math.hypot(u.x - e.x, u.y - e.y) / CELL <= t.range * (1 + this.sides[u.side].mods.range);
    }
    /* 最近的「看得见」的敌人。视野关闭时等价于「全图最近」，与 v0.8 的自动索敌一致。
     * 距离按格返回，方便与 TYPES[kind].vision 直接比。 */
    nearestVisibleEnemy(u) {
      let best = null, bestD = Infinity;
      for (const e of this.units) {
        if (e.dead || e.side === u.side) continue;
        if (this.vision.enabled && !canSee(this.map, this.units, u.side, e, this.vision)) continue;
        const d = Math.hypot(u.x - e.x, u.y - e.y);
        if (d < bestD) { bestD = d; best = e; }
      }
      return best ? { e: best, d: bestD / CELL } : null;
    }
    /* 把坐标夹回地图内（后撤/追击的落点可能出界） */
    clampToWorld(x, y) {
      return {
        x: clamp(x, CELL * 0.5, WORLD - CELL * 0.5),
        y: clamp(y, CELL * 0.5, WORLD - CELL * 0.5)
      };
    }
    /* 计算这一拍的行为。返回 {move:{x,y}|null, targetId:number|null}。
     * 只做决策，不改状态 —— 方便单测直接调它看意图。 */
    unitIntent(u) {
      const mode = u.mode || DEFAULT_MODE;
      const order = u.order;
      const t = TYPES[u.kind];
      const canFight = this.canFightKind(u.kind);
      /* SCOUT / SALVAGER 没有武器：三种模式都退化成「服从」，它们本来也不该自己去打 */
      if (!canFight) {
        if (order && order.type === 'move') return { move: { x: order.x, y: order.y }, targetId: null };
        return { move: null, targetId: null };
      }
      if (mode === 'obey') {
        /* 完全执行 Bot：只有显式 move 才移动；显式 attack 才锁定目标。
         * 视野里出现敌人也不会让它离开位置（开火不算移动，射程内照样还击）。 */
        return {
          move: (order && order.type === 'move') ? { x: order.x, y: order.y } : null,
          targetId: (order && order.targetId != null) ? order.targetId : null
        };
      }
      const foe = this.nearestVisibleEnemy(u);
      if (!foe) {
        /* 视野里没有敌人：回归 Bot 的原定指令（neutral 与 aggressive 都是这个收尾） */
        return {
          move: (order && order.type === 'move') ? { x: order.x, y: order.y } : null,
          targetId: null
        };
      }
      const engage = this.inWeaponRange(u, foe.e);
      if (mode === 'neutral') {
        /* 中间模式：停步开火 + 高差拉扯，但绝不主动追击 */
        if (engage) {
          const myH = terrainHeightAt(this.map, u.x, u.y);
          const hisH = terrainHeightAt(this.map, foe.e.x, foe.e.y);
          if (hisH - myH >= HEIGHT_DIFF_MIN) {
            /* 自己站在低处被压着打：朝相反方向撤一小段，脱离后靠「回归原指令」复位 */
            const dx = u.x - foe.e.x, dy = u.y - foe.e.y;
            const d = Math.hypot(dx, dy) || 1;
            const p = this.clampToWorld(u.x + (dx / d) * MODE_RETREAT_CELLS * CELL,
                                        u.y + (dy / d) * MODE_RETREAT_CELLS * CELL);
            return { move: p, targetId: foe.e.id };
          }
          /* 持平或占高：原地开火（停步），不追 */
          return { move: null, targetId: foe.e.id };
        }
        /* 射程外但在视野内：只做「占高差视野」的原地判断，不前进 */
        return {
          move: (order && order.type === 'move' && !this.hasHeightAdvantage(u, foe.e))
            ? { x: order.x, y: order.y } : null,
          targetId: foe.e.id
        };
      }
      /* aggressive：看见就压上去。打得到就停下打，打不到就追，追出视野就放弃。 */
      if (engage) return { move: null, targetId: foe.e.id };
      const vis = TYPES[u.kind].vision + (this.vision.enabled ? HEIGHT_BONUS_CELLS : 0);
      if (foe.d > vis) {
        /* 跟丢了（远处那个只是巧合在视野边缘）：回到 Bot 的指令 */
        return {
          move: (order && order.type === 'move') ? { x: order.x, y: order.y } : null,
          targetId: null
        };
      }
      /* 停在射程边缘而不是怼到脸上：留出输出距离，也避免和地形死磕 */
      const keep = Math.max(1, TYPES[u.kind].range * 0.8) * CELL;
      const dx = u.x - foe.e.x, dy = u.y - foe.e.y;
      const d = Math.hypot(dx, dy) || 1;
      const p = this.clampToWorld(foe.e.x + (dx / d) * keep, foe.e.y + (dy / d) * keep);
      return { move: p, targetId: foe.e.id };
    }
    /* 自己是否占着高差（比对手高出一个门槛以上）。neutral 用它决定「要不要继续前进」 */
    hasHeightAdvantage(u, e) {
      if (!e) return false;
      const myH = terrainHeightAt(this.map, u.x, u.y);
      const hisH = terrainHeightAt(this.map, e.x, e.y);
      return myH - hisH >= HEIGHT_DIFF_MIN;
    }

    /* ---------------- 母舰分配 ---------------- */
    slots(side) {
      const a = this.sides[side].alloc;
      return {
        produce: slotsOf(a.produce), research: slotsOf(a.research),
        /* 攻击是连续槽位，整数部分才是「现在能装几门」 */
        attackSlots: Number(weaponSlotsOf(a.attack).toFixed(2)),
        attackCap: weaponCapOf(a.attack)
      };
    }
    /* 过渡：每 tick 最多移动 ALLOC_STEP 个百分点。
     * 必须「从超出目标的项挪给不足目标的项」，否则各自独立的加减会把总和推离 100
     * （第一版就是这样：produce 一路涨到 100，其它项没同步降，总和变成 145）。 */
    stepAlloc(side) {
      const S = this.sides[side], cur = S.alloc, tgt = S.target;
      let budget = ALLOC_STEP, moved = 0;
      for (const k of ALLOC_KEYS) {
        if (budget <= 0) break;
        const surplus = cur[k] - tgt[k];
        if (surplus <= 0) continue;
        const take = Math.min(surplus, budget);
        cur[k] -= take; budget -= take; moved += take;
      }
      for (const k of ALLOC_KEYS) {
        if (moved <= 0) break;
        const need = tgt[k] - cur[k];
        if (need <= 0) continue;
        const give = Math.min(need, moved);
        cur[k] += give; moved -= give;
      }
    }

    /* ---------------- 科技 ---------------- */
    techAvailable(side, id) {
      const def = TECHS[id];
      if (!def) return { ok: false, reason: 'unknown tech' };
      const S = this.sides[side];
      if (S.techDone[id]) return { ok: false, reason: 'already researched' };
      if (S.techDoing.some(t => t.id === id)) return { ok: false, reason: 'already researching' };
      for (const r of (def.req || [])) if (!S.techDone[r]) return { ok: false, reason: 'missing prereq ' + r };
      if (def.reqAny && !def.reqAny.some(r => S.techDone[r])) {
        return { ok: false, reason: 'missing prereq (one of ' + def.reqAny.join('/') + ')' };
      }
      for (const x of (def.excl || [])) if (S.techDone[x] || S.techDoing.some(t => t.id === x)) {
        return { ok: false, reason: 'excluded by ' + x };
      }
      if (S.techDoing.length >= slotsOf(S.alloc.research)) return { ok: false, reason: 'no free research slot' };
      if (S.cu < def.cu) return { ok: false, reason: 'not enough cu' };
      return { ok: true };
    }
    applyTech(side, id) {
      const S = this.sides[side], eff = TECHS[id].eff;
      /* unlock 是「解锁弹种」这种非数值效果，不进 mods（它的生效靠 techDone 查询） */
      for (const k of Object.keys(eff)) if (typeof eff[k] === 'number') S.mods[k] = (S.mods[k] || 0) + eff[k];
      /* 耐久是唯一会影响已有单位的效果：统一按 TYPES 基准重算，避免多次乘算漂移 */
      if (eff.hp) {
        for (const u of this.units) {
          if (u.side !== side || u.dead) continue;
          const ratio = u.hp / u.maxHp;
          u.maxHp = Math.round(TYPES[u.kind].hp * (1 + S.mods.hp));
          u.hp = u.maxHp * ratio;
        }
      }
    }
    stepResearch(side) {
      const S = this.sides[side];
      const keep = [];
      for (const r of S.techDoing) {
        r.remain -= 1;
        if (r.remain <= 0) {
          S.techDone[r.id] = true;
          this.applyTech(side, r.id);
          const ev = { t: this.turn, type: 'tech', side, id: r.id };
          this.lastEvents.push(ev); this.events.push(ev);
        } else keep.push(r);
      }
      S.techDoing = keep;
    }

    /* ---------------- 经济与生产 ---------------- */
    stepEconomy(side) {
      const S = this.sides[side];
      S.cu += BASE_INCOME * (1 + S.mods.income);
      /* 采集车：站在资源点圈内就产 CU，资源点按次数耗尽。
       * 采集是机械劳动，交给引擎托管；Bot 只需要决定「造几台、派去哪」。 */
      for (const u of this.units) {
        if (u.dead || u.side !== side || u.kind !== 'SALVAGER') continue;
        for (const r of this.resources) {
          if (r.amount <= 0) continue;
          if (dist(u, r) > RES_RADIUS) continue;
          S.cu += SALVAGE_RATE * (1 + S.mods.harvest);
          r.acc += 1;
          if (r.acc >= 6) { r.acc = 0; r.amount -= 1; }
          break;
        }
      }
    }
    stepProduction(side) {
      const S = this.sides[side];
      const slots = slotsOf(S.alloc.produce);
      while (S.active.length < slots && S.queue.length) {
        const kind = S.queue[0];
        const t = TYPES[kind];
        const cost = Math.round(t.cost * (1 - S.mods.cost));
        if (S.cu < cost) break;                 // 钱不够就等着，不丢单
        S.cu -= cost;
        S.queue.shift();
        S.active.push({ kind, remain: t.build });
      }
      for (const a of S.active) a.remain -= 1 * (1 + S.mods.produce);
      const finished = S.active.filter(a => a.remain <= 0);
      S.active = S.active.filter(a => a.remain > 0);
      if (!finished.length) return;
      const cv = this.mothership(side);
      if (!cv) return;                          // 母舰没了就停产（也就输定了）
      for (const f of finished) {
        const a = (this.turn * 1.7 + this.nextId * 2.3) % (Math.PI * 2);
        const u = this.spawn(side, f.kind, cv.x + Math.cos(a) * CELL * 2.6 * SCALE, cv.y + Math.sin(a) * CELL * 2.6 * SCALE);
        const ev = { t: this.turn, type: 'spawn', side, kind: f.kind, id: u.id };
        this.lastEvents.push(ev); this.events.push(ev);
      }
    }

    /* 武器：独立队列，一门一门造；造好计入 weaponsReady，不占生产槽，也不占人口 */
    stepWeapons(side) {
      const S = this.sides[side];
      if (!S.weaponQueue.length) return;
      const job = S.weaponQueue[0];
      job.remain -= 1;
      if (job.remain > 0) return;
      /* 钱不够就停在最后一步等着，不扣成负数。
       * v0.7 是直接 `cu -= 120`，CU 为 0 时能扣到 -120，与生产队列「钱不够就等」的
       * 语义不一致，而且负数 CU 会让后面的研究/生产判断全线失真。 */
      if (S.cu < WEAPON_COST) { job.remain = 1; return; }
      S.cu -= WEAPON_COST;
      S.weaponQueue.shift();
      S.weaponsReady += 1;
      const ev = { t: this.turn, type: 'weapon', side, ready: S.weaponsReady };
      this.lastEvents.push(ev); this.events.push(ev);
    }

    /* 运载舰：前线修理（半径内所有己方单位，含母舰）+ 自带 1 槽造小单位。
     * 它不参与四项分配，被摧毁也不判负 —— 是「前线据点」，不是第二个母舰。 */
    stepCarriers(side) {
      const S = this.sides[side];
      for (const c of this.units) {
        if (c.dead || c.side !== side || c.kind !== 'CARRIER') continue;
        /* 修理：只对活着的、没满血的目标生效 */
        for (const u of this.units) {
          if (u.dead || u.side !== side || u.hp >= u.maxHp) continue;
          if (Math.hypot(u.x - c.x, u.y - c.y) > REPAIR_RADIUS) continue;
          u.hp = Math.min(u.maxHp, u.hp + REPAIR_RATE);
        }
        /* 生产：队列串行，1 槽，开工时扣钱 */
        if (!c.queue.length) continue;
        const job = c.queue[0];
        const cost = Math.round(TYPES[job.kind].cost * (1 - S.mods.cost));
        if (job.remain === TYPES[job.kind].build) {
          if (S.cu < cost) continue;            // 钱不够就等着
          S.cu -= cost;
        }
        job.remain -= 1;
        if (job.remain > 0) continue;
        c.queue.shift();
        const a = (this.turn * 2.1 + this.nextId * 1.7) % (Math.PI * 2);
        const u = this.spawn(side, job.kind, c.x + Math.cos(a) * CELL * 2 * SCALE, c.y + Math.sin(a) * CELL * 2 * SCALE);
        const ev = { t: this.turn, type: 'spawn', side, kind: job.kind, id: u.id, by: c.id };
        this.lastEvents.push(ev); this.events.push(ev);
      }
    }

    /* ---------------- 静态信息 ---------------- */
    header() {
      return {
        /* version 是「header 结构版本」，必须跟协议版本同一个数，不能各写一套
         * （v0.7 这里写死 4 而 protoVersion 是 5，外部工具按 version 判断会误判）。 */
        version: PROTO.version, proto: PROTO.name, protoVersion: PROTO.version,
        rulesHash: rulesHash(this.cfg.tickLimit, this.vision),
        game: 'Desert Arena · Referee MVP', seed: this.seed, players: this.players,
        /* 生成器名字：同 seed 在不同算法下是两张图，回放/复现必须知道用的是哪套 */
        generator: this.map.generator,
        /* seed 是调用方给的；mapAttempt 是内部第几次重试才得到连通的图。
         * 两个字段一起才能唯一确定地图，(seed, attempt) 不会与其他 seed 相撞。 */
        mapAttempt: this.map.attempt || 0,
        w: GRID, h: GRID, cell: CELL, world: WORLD,
        /* 地图三层（base 通行 / elev 高程 / surf 地表类型）用紧凑编码传输：
         * 256² 直接发数组是 450 KB，RLE+base64 后约 105 KB。
         * 解码入口是 decodeMapLayers（驱动会在交给 Bot 之前解开，
         * 外部进程 Bot 自己调一次即可）。 */
        map: M.encodeMapLayers(this.map),
        spawns: this.map.spawns.map(s => [s.gx, s.gy]),
        vp: this.vps.map(v => ({ id: v.id, x: v.x, y: v.y })),
        res: this.resources.map(r => ({ id: r.id, x: r.x, y: r.y })),
        rules: {
          tickLimit: this.cfg.tickLimit, vpRadius: VP_RADIUS, unitSlope: UNIT_SLOPE, carrierSlope: CARRIER_SLOPE,
          /* 网格密度与平台坡限：Bot 自己算流场/可走性时要用同一份，不能各自写死 */
          grid: GRID, cellPx: CELL, scale: SCALE, platformSlope: PLATFORM_SLOPE,
          /* 时序是规则的一部分：Bot 必须知道 tick 多长、自己有多少决策预算、
           * 以及「现在这条指令最早什么时候生效」。 */
          timing: {
            tickMs: this.cfg.tickMs, decideMs: this.cfg.decideMs, initMs: this.cfg.initMs,
            orderLatency: ORDER_LATENCY
          },
          /* 弹道常数与弹种表：Bot 想算「这发能不能打到 / 打上去多疼」需要同一份数 */
          ballistics: {
            airK: B.AIR_K, gravity: B.G, mPerCell: B.M_PER_CELL, elevM: B.ELEV_M,
            mountainH: B.MOUNTAIN_H, hitRadius: B.HIT_R_M,
            shells: B.SHELLS, shellIds: B.SHELL_IDS
          },
          types: TYPES, producible: PRODUCIBLE, smallProducible: SMALL_PRODUCIBLE,
          alloc: {
            keys: ALLOC_KEYS, step: ALLOC_STEP, start: START_ALLOC,
            slotStep: SLOT_STEP, maxSlots: MAX_SLOTS, maxWeapons: MAX_WEAPONS,
            moveMul: [MOVE_MUL_MIN, MOVE_MUL_MAX]
          },
          economy: { startCu: START_CU, income: BASE_INCOME, salvage: SALVAGE_RATE, resAmount: RES_AMOUNT, resRadius: RES_RADIUS, popMax: POP_MAX },
          weapon: { cost: WEAPON_COST, build: WEAPON_BUILD, queueMax: WEAPON_QUEUE_MAX },
          repair: { radius: REPAIR_RADIUS, rate: REPAIR_RATE },
          /* 视野：Bot 必须能预判「我看不到什么」，否则会把「看不见」当成「不存在」。
           * enabled=false 时 fog/los 也都是 false，语义就是「全图可见，与老版本一致」。 */
          vision: {
            enabled: this.vision.enabled, fog: this.vision.fog, los: this.vision.los,
            eyeH: EYE_H, losTol: LOS_TOL, losRange: LOS_RANGE_CELLS,
            /* 高度差加成：存在高度差 → 该方向视距 +2 格；更高的阻挡仍然拦截 */
            heightBonus: HEIGHT_BONUS_CELLS, heightDiffMin: HEIGHT_DIFF_MIN
          },
          /* 行动模式：Bot 必须知道有哪三种、默认是哪一种，否则无法预期单位行为 */
          modes: MODES.slice(), defaultMode: DEFAULT_MODE, modeDesc: MODE_DESC,
          modeRetreatCells: MODE_RETREAT_CELLS,
          techs: TECHS, trees: TECH_TREES
        }
      };
    }
    frame() {
      return {
        turn: this.turn,
        /* maxHp 是渲染需要的：网页用 hp/maxHp 判断「残血描边」。
         * v0.7 这里只发 hp，match.js 里 `u.hp < u.maxHp * 0.4` 永远比较 undefined，
         * 残血圈一次都没画出来过 —— 这是渲染 bug，不是设计取舍。 */
        units: this.units.map(u => ({ id: u.id, side: u.side, kind: u.kind, x: Math.round(u.x), y: Math.round(u.y), hp: Math.round(u.hp), maxHp: u.maxHp, mode: u.mode })),
        vp: this.vps.map(v => ({ id: v.id, owner: v.owner })),
        score: [0, 1].map(s => ({ vp: this.sides[s].vpScore, kills: this.sides[s].kills, losses: this.sides[s].losses })),
        cu: [Math.round(this.sides[0].cu), Math.round(this.sides[1].cu)],
        alloc: [ALLOC_KEYS.map(k => this.sides[0].alloc[k]), ALLOC_KEYS.map(k => this.sides[1].alloc[k])],
        techs: [Object.keys(this.sides[0].techDone).sort(), Object.keys(this.sides[1].techDone).sort()],
        queue: [this.sides[0].active.map(a => a.kind), this.sides[1].active.map(a => a.kind)],
        weapons: [this.sides[0].weaponsReady, this.sides[1].weaponsReady],
        /* 在飞的炮弹（只留位置，够渲染与复核；高度决定它会不会被山挡住） */
        shells: this.projectiles.map(p => ({
          id: p.id, side: p.side,
          x: Math.round(p.sx + p.dx * p.s / B.M_PER_PX),
          y: Math.round(p.sy + p.dy * p.s / B.M_PER_PX), h: Math.round(p.h)
        }))
      };
    }
    /* 帧哈希：确定性重放的比对单位 */
    hashFrame(f) { return fnv1a(JSON.stringify(f || this.frame())); }

    /* ---------------- 三段式协议：hello（开局一次）/ tick（每回合）/ end（终局） ---------------- */
    encodeHello() { return JSON.stringify(Object.assign({ type: 'hello' }, this.header())) + '\n'; }

    /* ---------------- 视野：给协议层用的三个判定 ----------------
     * 三者都只读 this.units / this.map，不改任何状态，所以不会影响确定性。 */
    /* 本方当前能看到的敌方单位 id 集合（山脊遮挡 + 视距，由 canSee 一处判定） */
    visibleEnemies(side) {
      const set = new Set();
      if (!this.vision.enabled) return set;
      for (const e of this.units) {
        if (e.dead || e.side === side) continue;
        if (canSee(this.map, this.units, side, e, this.vision)) set.add(e.id);
      }
      return set;
    }
    /* 空中目标（在飞的炮弹）只按视距判：见 pointVisible 的注释 */
    pointVisible(side, x, y) {
      return pointVisible(this.map, this.units, side, x, y, this.vision);
    }
    /* 事件也是信息：看不见的敌人在哪造了什么、死了什么，都不该出现在我方 tick 里。
     * VP 归属是公开信息（计分板要显示），所以 vp 事件始终保留。 */
    visibleEvents(side) {
      if (!this.vision.enabled) return this.lastEvents.slice(-12);
      return this.lastEvents.filter(e => {
        if (!e || e.type === 'vp') return true;
        if (e.type === 'kill') return e.by === side;
        return e.side === side;
      }).slice(-12);
    }
    /* 发第 i 方的一行输入（以换行结尾）；深拷贝由 JSON 序列化天然完成 */
    encodeTick(i) {
      const S = this.sides[i];
      /* 视野关闭时 seen = null：下面的 map 不走 filter，输出与 v0.7 逐字节一致。
       * 开启时只过滤「敌方单位 / 敌方炮弹 / 敌方事件」，己方信息一条不少。 */
      const seen = this.vision.enabled ? this.visibleEnemies(i) : null;
      const state = {
        type: 'tick',
        turn: this.turn, tickLimit: this.cfg.tickLimit, side: i, seed: this.seed,
        /* 这一行对应的决策将在 applyAt 生效：引擎不会停下来等，指令一律晚一步落地。
         * Bot 因此不必（也不能）假设「本 tick 改了本 tick 就生效」。 */
        timing: {
          tickMs: this.cfg.tickMs, decideMs: this.cfg.decideMs,
          applyAt: this.turn + ORDER_LATENCY, orderLatency: ORDER_LATENCY
        },
        units: (seen
          ? this.units.filter(u => u.side === i || seen.has(u.id))
          : this.units
        ).map(u => {
          const o = { id: u.id, side: u.side, kind: u.kind, x: u.x, y: u.y, hp: u.hp, maxHp: u.maxHp };
          /* 模式只发给自己：敌人在用什么模式属于情报，不能白给 */
          if (u.side === i) o.mode = u.mode;
          return o;
        }),
        vp: this.vps.map(v => ({ id: v.id, x: v.x, y: v.y, owner: v.owner, contested: v.contested })),
        res: this.resources.map(r => ({ id: r.id, x: r.x, y: r.y, amount: r.amount })),
        score: [0, 1].map(s => ({ vp: this.sides[s].vpScore, kills: this.sides[s].kills, losses: this.sides[s].losses })),
        /* 母舰决策需要的全部信息：当前分配、正在过渡的目标、槽位、经济、科技进度 */
        me: {
          cu: Math.round(S.cu), pop: S.pop, popMax: POP_MAX,
          alloc: Object.assign({}, S.alloc), allocTarget: Object.assign({}, S.target),
          /* 本方当前的默认行动模式（单台单位可能各自不同，看 units[].mode） */
          mode: S.mode || DEFAULT_MODE,
          slots: this.slots(i),
          weaponSlots: Number(weaponSlotsOf(S.alloc.attack).toFixed(2)),
          weaponsReady: S.weaponsReady,
          weaponQueue: S.weaponQueue.map(w => Math.ceil(w.remain)),
          speedMul: Number(speedMulOf(S.alloc.move).toFixed(3)),
          techDone: Object.keys(S.techDone).sort(),
          techDoing: S.techDoing.map(t => ({ id: t.id, remain: Math.ceil(t.remain) })),
          queue: S.queue.slice(), active: S.active.map(a => ({ kind: a.kind, remain: Math.ceil(a.remain) })),
          /* 运载舰的队列挂在单位上，单独列出来，Bot 才知道前线在造什么 */
          carriers: this.units.filter(u => u.side === i && u.kind === 'CARRIER' && !u.dead)
            .map(u => ({ id: u.id, queue: u.queue.map(q => ({ kind: q.kind, remain: Math.ceil(q.remain) })) })),
          mods: Object.assign({}, S.mods)
        },
        /* 在飞的炮弹：Bot 能看到「有东西朝我飞过来」，从而决定散开（这版没做闪避逻辑，
         * 但状态是给了的 —— 不给的话 Bot 连"被瞄准了"都不知道） */
        shells: this.projectiles
          .filter(p => p.side === i || !this.vision.enabled ||
            this.pointVisible(i, p.sx + p.dx * p.s / B.M_PER_PX, p.sy + p.dy * p.s / B.M_PER_PX))
          .map(p => ({
            id: p.id, side: p.side,
            x: Math.round(p.sx + p.dx * p.s / B.M_PER_PX),
            y: Math.round(p.sy + p.dy * p.s / B.M_PER_PX), h: Math.round(p.h)
          })),
        events: this.visibleEvents(i)
      };
      return JSON.stringify(state) + '\n';
    }
    encodeEnd() {
      return JSON.stringify({ type: 'end', turn: this.turn, result: this.result() }) + '\n';
    }
    /* 兼容旧接口：等价于 tick 段 */
    encode(i) { return this.encodeTick(i); }

    /* 解析一行回复。结构性错误（不是 JSON / 不是数组 / id 越权）抛错 → 判故障；
     * 语义性非法（撞山、打不存在的目标、槽位已满、钱不够）只丢掉这一条并记账 → 不判故障。
     * 技能 v2：actions 里除单位动作外，还允许三类「阵营动作」（每回合各最多 1 条）：
     *   {type:'alloc', alloc:{produce,attack,research,move}}  设母舰分配目标
     *   {type:'produce', kind}                                入生产队列
     *   {type:'research', tech}                               开始研究
     */
    decode(i, line) {
      if (utf8Length(line) > MAX_LINE) throw new Error('line too long');
      let obj;
      try { obj = JSON.parse(String(line).replace(/\n+$/, '')); }
      catch (e) { throw new Error('bad json'); }
      if (!obj || !Array.isArray(obj.actions)) throw new Error('actions must be array');
      const mine = new Set(this.units.filter(u => u.side === i && !u.dead).map(u => u.id));
      /* 上限按「每台单位最多两条（动作 + 模式）+ 4 条阵营动作」算：
       * 模式动作是逐台的，不能用 mine.size + 4 卡住。 */
      if (obj.actions.length > mine.size * 2 + 4) throw new Error('too many actions');
      const seen = new Set(), seenCamp = new Set();
      const out = [];
      for (const a of obj.actions) {
        if (!a || typeof a.type !== 'string') throw new Error('bad action type');
        /* ---- 阵营动作 ---- */
        if (a.type === 'alloc') {
          if (seenCamp.has('alloc')) throw new Error('duplicate camp action');
          seenCamp.add('alloc');
          const alloc = a.alloc || {};
          const keys = Object.keys(alloc);
          if (keys.length !== ALLOC_KEYS.length) { this.noteRejected(i, 0, 'alloc must have 4 keys'); continue; }
          let bad = false, sum = 0;
          for (const k of ALLOC_KEYS) {
            const v = alloc[k];
            if (!Number.isInteger(v) || v < 0 || v > 100) { bad = true; break; }
            sum += v;
          }
          if (bad) { this.noteRejected(i, 0, 'alloc must be integer 0..100'); continue; }
          if (sum !== 100) { this.noteRejected(i, 0, 'alloc sum must be 100 (got ' + sum + ')'); continue; }
          out.push({ type: 'alloc', alloc: ALLOC_KEYS.reduce((o, k) => (o[k] = alloc[k], o), {}) });
          continue;
        }
        if (a.type === 'produce') {
          if (seenCamp.has('produce')) throw new Error('duplicate camp action');
          seenCamp.add('produce');
          if (PRODUCIBLE.indexOf(a.kind) < 0) { this.noteRejected(i, 0, 'not producible'); continue; }
          const t = TYPES[a.kind];
          if (this.sides[i].pop + t.pop > POP_MAX) { this.noteRejected(i, 0, 'pop full'); continue; }
          /* by 指向一艘自己的运载舰：它只能造小单位，且只有 1 个槽 */
          if (a.by != null) {
            const host = this.units.find(u => u.id === a.by && !u.dead && u.side === i);
            if (!host || host.kind !== 'CARRIER') { this.noteRejected(i, 0, 'produce: not a carrier'); continue; }
            if (SMALL_PRODUCIBLE.indexOf(a.kind) < 0) { this.noteRejected(i, 0, 'produce: carrier cannot build ' + a.kind); continue; }
            if (host.queue.length >= 3) { this.noteRejected(i, 0, 'carrier queue full'); continue; }
            out.push({ type: 'produce', kind: a.kind, by: a.by });
            continue;
          }
          if (this.sides[i].queue.length + this.sides[i].active.length >= 6) {
            this.noteRejected(i, 0, 'queue full'); continue;
          }
          out.push({ type: 'produce', kind: a.kind });
          continue;
        }
        /* 造炮：独立队列，一门一门造；受「武器槽位上限」与队列长度约束 */
        if (a.type === 'weapon') {
          if (seenCamp.has('weapon')) throw new Error('duplicate camp action');
          seenCamp.add('weapon');
          const S = this.sides[i];
          const cap = weaponCapOf(S.alloc.attack);
          if (S.weaponsReady + S.weaponQueue.length >= cap) {
            this.noteRejected(i, 0, 'weapon: no free weapon slot (cap ' + cap + ')'); continue;
          }
          if (S.weaponQueue.length >= WEAPON_QUEUE_MAX) { this.noteRejected(i, 0, 'weapon queue full'); continue; }
          out.push({ type: 'weapon' });
          continue;
        }
        if (a.type === 'research') {
          if (seenCamp.has('research')) throw new Error('duplicate camp action');
          seenCamp.add('research');
          const chk = this.techAvailable(i, a.tech);
          if (!chk.ok) { this.noteRejected(i, 0, 'research: ' + chk.reason); continue; }
          out.push({ type: 'research', tech: a.tech });
          continue;
        }
        /* 行动模式：给全队（不带 id）或某一台（带 id）设 obey / neutral / aggressive。
         * 这是「调度权」的开关，本身不花 CU、不占槽位，所以同 tick 允许多条。 */
        if (a.type === 'mode') {
          if (MODES.indexOf(a.mode) < 0) { this.noteRejected(i, 0, 'mode: unknown ' + a.mode); continue; }
          if (a.id != null) {
            if (typeof a.id !== 'number') throw new Error('bad unit id');
            if (!mine.has(a.id)) throw new Error('bad unit id');
            out.push({ type: 'mode', id: a.id, mode: a.mode });
            continue;
          }
          out.push({ type: 'mode', mode: a.mode });
          continue;
        }
        /* ---- 单位动作 ---- */
        if (a.type !== 'move' && a.type !== 'attack' && a.type !== 'idle') throw new Error('bad action type');
        if (typeof a.id !== 'number' || !mine.has(a.id)) throw new Error('bad unit id');
        if (seen.has(a.id)) throw new Error('duplicate unit action');
        seen.add(a.id);
        if (a.type === 'move') {
          if (!Number.isFinite(a.x) || !Number.isFinite(a.y)) throw new Error('bad coords');
          if (a.x < 0 || a.y < 0 || a.x > WORLD || a.y > WORLD) throw new Error('coords out of range');
          if (this.map.base[ci(gxOf(a.x), gyOf(a.y))] === 3) {
            this.noteRejected(i, a.id, 'target on mountain');
            continue;
          }
        }
        if (a.targetId !== undefined && typeof a.targetId !== 'number') throw new Error('bad targetId');
        if (a.type === 'attack') {
          const t = this.units.find(u => u.id === a.targetId && !u.dead && u.side !== i);
          if (!t) { this.noteRejected(i, a.id, 'bad attack target'); continue; }
        }
        out.push({ id: a.id, type: a.type, x: a.x, y: a.y, targetId: a.targetId });
      }
      return out;
    }
    noteRejected(seat, id, reason) {
      if (this.rejected.length < 200) this.rejected.push({ turn: this.turn, seat, id, reason });
    }

    applyMove(u, tx, ty) {
      const dx = tx - u.x, dy = ty - u.y, d = Math.hypot(dx, dy);
      if (d < 2.5) return;
      const here = ci(gxOf(u.x), gyOf(u.y));
      const ahead = ci(clamp(gxOf(u.x + (dx / d) * CELL), 0, GRID - 1), clamp(gyOf(u.y + (dy / d) * CELL), 0, GRID - 1));
      /* 24 是「每格 24 级 = 100% 坡度」的标定；格变细后每格高差变成 1/S，
       * 除数也要跟着缩小，否则上坡惩罚会凭空消失。 */
      const slope = (this.map.elev[ahead] - this.map.elev[here]) / (24 / SCALE);
      let f = clamp(1 - 0.55 * Math.max(0, slope) + 0.18 * Math.max(0, -slope), 0.35, 1.30);
      f *= [0.9, 1.1, 0.7, 0, 1][this.map.base[here]] || 1;
      const S = this.sides[u.side];
      let speed = TYPES[u.kind].speed * (1 + S.mods.speed);
      /* 母舰速度由「移动」分配决定，0.4×~1.6×；其他单位只吃科技加成 */
      if (u.kind === 'MOTHERSHIP') speed *= speedMulOf(S.alloc.move);
      const travel = Math.min(speed * CELL * 0.1 * f, d);
      const steps = Math.max(1, Math.ceil(travel / (CELL * 0.5)));
      const step = travel / steps;
      for (let s = 0; s < steps; s++) {
        const nx = u.x + (dx / d) * step, ny = u.y + (dy / d) * step;
        if (!stepAllowed(this.map, u, u.x, u.y, nx, ny)) break;   // 山脉硬阻挡
        u.x = nx; u.y = ny;
      }
    }
    /* ---------------- 弹道与命中（v5：命中不再即时结算） ----------------
     * 一次开火 = 解一次弹道（求仰角）→ 生成一发炮弹 → 炮弹在后续 tick 里飞行
     * → 落地或撞上单位时才结算伤害。伤害只看「落速 + 弹种」，不看距离远近本身：
     * 远射伤害低是因为落速低，不是因为有距离惩罚。 */
    /* 地形高度（m）：只用于炮口/目标的离地高度（高打低会多得重力势能） */
    groundH(x, y) { return B.ELEV_M * this.map.elev[ci(gxOf(x), gyOf(y))]; }
    /* 弹道碰撞高度（m）：只有山脉（base===3，单位也走不过去）算实体遮挡。
     * 地图是 64×64 的粗网格，把每一个沙丘都当硬遮挡会让直射武器在这个分辨率下
     * 几乎无法交战，所以「山脊挡炮弹、沙丘可掠过」是刻意的建模取舍，写在 README。 */
    solidH(x, y) { return this.map.base[ci(gxOf(x), gyOf(y))] === 3 ? B.MOUNTAIN_H : 0; }
    muzzleH(u) { return this.groundH(u.x, u.y) + B.UNIT_H; }
    /* 初速：基础初速 × 弹种倍率 × (1 + 发射药科技) */
    v0Of(u, shellId) {
      const t = TYPES[u.kind], S = this.sides[u.side];
      const sh = B.SHELLS[shellId] || B.SHELLS.AP;
      return t.v0 * sh.vMul * (1 + (S.mods.muzzle || 0));
    }
    shellsOf(side) {
      return B.SHELL_IDS.filter(id => {
        const req = B.SHELLS[id].unlock;
        return !req || !!this.sides[side].techDone[req];
      });
    }
    /* 自动选弹：在已解锁弹种里挑「对本目标装甲等级期望最高」的那个。
     * 高爆弹单发伤害低但带溅射，给一点固定加成，避免它永远落选。 */
    pickShell(side, target) {
      let best = 'AP', bestScore = -1;
      for (const id of this.shellsOf(side)) {
        const sh = B.SHELLS[id];
        const armor = (sh.armor && sh.armor[TYPES[target.kind].armor]) || 1;
        const score = sh.d0 * armor + (sh.splash ? 0.12 : 0);
        if (score > bestScore) { bestScore = score; best = id; }
      }
      return best;
    }
    /* 解一发：给「射击者 → 目标（含前置量）」求仰角。
     * 目标在飞，所以迭代两次：用上一轮飞行时间估计提前量，再解一次。 */
    solveShot(u, shellId, ex, ey, evx, evy) {
      const t = TYPES[u.kind];
      const h0 = this.muzzleH(u);
      const v0 = this.v0Of(u, shellId);
      /* 提前量：先夹速度，再夹偏移量。
       * 单位被瞬移（测试摆位、以后可能的传送）时，「位移/tick」估出来的速度是几千 px/s，
       * 光夹速度还不够 —— 必须保证瞄点不会离目标本身超过半个射程，否则解算直接飞出包线。 */
      const vCap = 12 * CELL;
      const lvx = clamp(evx || 0, -vCap, vCap), lvy = clamp(evy || 0, -vCap, vCap);
      const dRaw = Math.hypot(ex - u.x, ey - u.y);
      if (dRaw < B.M_PER_PX * 2) return { ok: false, reason: 'too-close' };
      let tGuess = 0.6, sol = null, aimX = ex, aimY = ey, dx = 1, dy = 0;
      for (let it = 0; it < 2; it++) {
        const offX = lvx * tGuess, offY = lvy * tGuess;
        const offLen = Math.hypot(offX, offY);
        const maxOff = dRaw * 0.5;
        const k = offLen > maxOff ? maxOff / offLen : 1;
        aimX = ex + offX * k;
        aimY = ey + offY * k;
        const dPx = Math.hypot(aimX - u.x, aimY - u.y);
        if (dPx < B.M_PER_PX * 2) return { ok: false, reason: 'too-close' };
        dx = (aimX - u.x) / dPx; dy = (aimY - u.y) / dPx;
        const d = dPx * B.M_PER_PX;
        const dh = this.groundH(aimX, aimY) - this.groundH(u.x, u.y);
        const self = this;
        const groundAt = (sM) => self.solidH(u.x + dx * sM / B.M_PER_PX, u.y + dy * sM / B.M_PER_PX);
        sol = B.solve({ v0, h0, d, dh, elevMin: t.elev[0], elevMax: t.elev[1], groundAt });
        if (!sol.ok) return sol;
        tGuess = sol.t;
      }
      return { ok: true, theta: sol.theta, t: sol.t, vImpact: sol.vImpact, apex: sol.apex, aimX, aimY, dx, dy, shell: shellId };
    }
    /* 母舰的伤害乘「已造好的炮数」（多门炮齐射同一目标）；其他单位固定 1 倍。
     * 分配只决定槽位上限，炮本身还要走独立队列花 CU 造出来。 */
    weaponCount(u) {
      return u.kind === 'MOTHERSHIP' ? this.sides[u.side].weaponsReady : 1;
    }
    /* 开火：算一次解，然后按炮数生成若干发炮弹（多门炮 = 同一解的轻微散布，
     * 不是同一发炮弹重复扣血）。返回是否真的打出去了。 */
    fire(u, e) {
      const t = TYPES[u.kind], S = this.sides[u.side];
      /* 先用设计射程粗筛：解一次弹道要积分上百步，不能对射程外的目标白算 */
      if (this.weaponCount(u) <= 0) return false;      // 攻击分配 0% → 母舰没有武器
      const d = Math.hypot(u.x - e.x, u.y - e.y) / CELL;
      if (d > t.range * (1 + S.mods.range)) return false;
      const shellId = this.pickShell(u.side, e);
      const sol = this.solveShot(u, shellId, e.x, e.y, e.vx || 0, e.vy || 0);
      if (!sol.ok) return false;
      const n = Math.max(1, this.weaponCount(u));
      for (let i = 0; i < n; i++) {
        const spread = n > 1 ? (i - (n - 1) / 2) * 0.4 : 0;
        this.spawnShot(u, shellId, sol, spread, e.id);
      }
      S.shots += n;
      return true;
    }
    spawnShot(u, shellId, sol, spreadDeg, targetId) {
      const v0 = this.v0Of(u, shellId);
      const st = B.makeShellState(v0, sol.theta + spreadDeg, this.muzzleH(u));
      this.projectiles.push({
        id: this.nextShot++, side: u.side, srcKind: u.kind, shell: shellId,
        sx: u.x, sy: u.y, dx: sol.dx, dy: sol.dy,
        s: 0, h: st.h, vx: st.vx, vh: st.vh, t: 0,
        v0, targetId: targetId == null ? null : targetId, dead: false
      });
    }
    /* 伤害结算：落速 + 弹种 + 装甲等级 + 兵种克制 */
    hitUnit(p, target, factor) {
      const t = TYPES[p.srcKind];
      let dmg = B.damageOf(p.shell, t.dmg, t.v0, p.vImpact, TYPES[target.kind].armor);
      /* 克制倍率：表里**显式写了 0** 的（SCOUT、SALVAGER 这种没有武器的）必须保持 0，
       * 不能靠 `|| 1` 兜底 —— 那个写法会把有意的 0 悄悄变成 1。 */
      const row = RPS[p.srcKind];
      if (row && Object.prototype.hasOwnProperty.call(row, target.kind)) dmg *= row[target.kind];
      dmg *= factor;
      this.damageUnit(target, dmg, p.side);
    }
    damageUnit(e, dmg, bySide) {
      if (e.dead) return;
      e.hp -= dmg;
      if (e.hp > 0) return;
      e.dead = true;
      /* 阵亡必须回收人口，否则被打光的一方会永久占着 POP_MAX、再也补不出兵 */
      const def = TYPES[e.kind];
      if (def.pop) this.sides[e.side].pop = Math.max(0, this.sides[e.side].pop - def.pop);
      this.sides[e.side].losses++;
      this.sides[bySide].kills++;
      this.sides[bySide].killValue += TYPES[e.kind].cost;
      const ev = { t: this.turn, type: 'kill', by: bySide, target: e.id, kind: e.kind };
      this.lastEvents.push(ev); this.events.push(ev);
    }
    /* 一发炮弹落地：先看有没有砸中单位，再看路面；最后按弹种决定是否溅射 */
    explode(p, x, y, direct) {
      const sh = B.SHELLS[p.shell];
      p.dead = true;
      if (direct) { this.hitUnit(p, direct, 1); this.sides[p.side].hits++; }
      if (!sh.splash) return;
      const R = sh.splash / B.M_PER_PX;
      for (const u of this.units) {
        if (u.dead || u === direct) continue;
        const dPx = Math.hypot(u.x - x, u.y - y);
        if (dPx > R) continue;
        const falloff = B.splashFactor(dPx, R) * sh.splashDmg;
        this.hitUnit(p, u, falloff * (u.side === p.side ? 0.5 : 1));
      }
    }
    /* 炮弹推进：每 tick 走 TICK_MS，内部再分子步（10ms），
     * 用「线段 vs 单位圆」判命中，所以再快也不会穿模。 */
    stepShots() {
      if (!this.projectiles.length) return;
      const dt = TICK_MS / 1000, SUB = 5;
      for (const p of this.projectiles) {
        if (p.dead) continue;
        for (let i = 0; i < SUB && !p.dead; i++) {
          const s0 = p.s, h0 = p.h;
          const st = B.advance({ s: p.s, h: p.h, vx: p.vx, vh: p.vh, t: p.t }, dt / SUB);
          p.s = st.s; p.h = st.h; p.vx = st.vx; p.vh = st.vh; p.t = st.t;
          /* 单位换算：s 是米，p.dx/dy 是 px 空间的单位向量，必须先换算成 px 再乘 */
          const mp1 = p.s / B.M_PER_PX, mp0 = s0 / B.M_PER_PX;
          const x1 = p.sx + p.dx * mp1, y1 = p.sy + p.dy * mp1;
          const x0 = p.sx + p.dx * mp0, y0 = p.sy + p.dy * mp0;
          /* 1) 撞山：山脊按 MOUNTAIN_H 算实体 */
          const solid = this.solidH(x1, y1);
          if (solid > 0 && p.h <= solid) { p.vImpact = B.speedOf(p); this.explode(p, x1, y1, null); break; }
          /* 2) 落地 */
          if (p.h <= 0) { p.vImpact = B.speedOf(p); this.explode(p, x1, y1, null); break; }
          /* 3) 命中单位：线段到圆心的最近距离 < 命中半径 */
          let hit = null;
          for (const u of this.units) {
            if (u.dead || u.side === p.side) continue;   // 直击只打敌人（友军只吃溅射）
            const ux = u.x, uy = u.y;
            const vx = x1 - x0, vy = y1 - y0;
            const len2 = vx * vx + vy * vy;
            let tt = len2 > 0 ? ((ux - x0) * vx + (uy - y0) * vy) / len2 : 0;
            tt = Math.max(0, Math.min(1, tt));
            const cx = x0 + vx * tt, cy = y0 + vy * tt;
            if (Math.hypot(ux - cx, uy - cy) > B.HIT_R_M / B.M_PER_PX) continue;
            const hAt = h0 + (p.h - h0) * tt;
            if (Math.abs(hAt - (this.groundH(ux, uy) + B.UNIT_H)) > 6) continue;
            hit = u; break;
          }
          if (hit) { p.vImpact = B.speedOf(p); this.explode(p, hit.x, hit.y, hit); break; }
        }
        if (p.t >= B.MAX_T) p.dead = true;
      }
      this.projectiles = this.projectiles.filter(p => !p.dead);
    }
    updateVP() {
      for (const vp of this.vps) {
        let c0 = 0, c1 = 0;
        for (const u of this.units) {
          if (u.dead || u.kind === 'MOTHERSHIP') continue;
          if (Math.hypot(u.x - vp.x, u.y - vp.y) <= VP_RADIUS) { if (u.side === 0) c0++; else c1++; }
        }
        this.sides[0].controlTicks += c0;
        this.sides[1].controlTicks += c1;
        const prev = vp.owner;
        vp.contested = c0 > 0 && c1 > 0;
        vp.owner = vp.contested ? -1 : (c0 > 0 ? 0 : (c1 > 0 ? 1 : -1));
        if (vp.owner >= 0) { this.sides[vp.owner].vpScore++; this.sides[vp.owner].vpTicks++; }
        if (prev !== vp.owner) {
          const ev = { t: this.turn, type: 'vp', id: vp.id, owner: vp.owner };
          this.lastEvents.push(ev); this.events.push(ev);
        }
      }
    }
    computeCards() {
      const denom = Math.max(1, this.turn);
      return [0, 1].map(s => {
        const S = this.sides[s], cv = this.mothership(s);
        const alive = this.units.filter(u => u.side === s && u.kind !== 'MOTHERSHIP');
        const aliveCost = alive.reduce((a, u) => a + TYPES[u.kind].cost, 0);
        const avgBot = this.botCalls[s] ? this.botMs[s] / this.botCalls[s] : 0;
        /* 这一项衡量「算力余量」：均摊决策耗时相对 120ms 预算还剩多少。
         * 用预算而不是 tick 长度做分母 —— Bot 被允许跨 tick 思考，判它慢的门槛是预算。 */
        const limit = this.cfg.decideMs;
        const faultPenalty = Math.min(1, (this.botFaults[s] || 0) * 0.25);
        const f = [
          100 * clamp(S.vpTicks / (this.vps.length * denom), 0, 1),
          /* 歼敌：击杀按造价加权 / 敌方累计投入（initialCost 会随对手生产一起长），
           * 所以这一项衡量的是「你打掉了对方多少投资」，不是「打掉了开局那点兵」 */
          100 * clamp(S.killValue / Math.max(1, this.sides[1 - s].initialCost), 0, 1),
          100 * clamp(aliveCost / Math.max(1, S.producedCost), 0, 1),
          100 * (cv ? clamp(cv.hp / cv.maxHp, 0, 1) : 0),
          clamp(50 + 10 * (S.kills - S.losses), 0, 100),
          100 * clamp(S.controlTicks / (denom * this.vps.length * 1.5), 0, 1),
          100 * clamp((S.vpScore + S.killValue / 60) / (denom / 6), 0, 1),
          100 * clamp(1 - avgBot / limit - faultPenalty, 0, 1)
        ];
        let total = 0; for (let i = 0; i < 8; i++) total += f[i] * WEIGHTS[i];
        return { fields: f, total, vpScore: S.vpScore, kills: S.kills, losses: S.losses, botMs: avgBot };
      });
    }
    endGame(winner, level, reason) {
      if (this.resultData) return;
      this.resultData = {
        winner, level, reason, turn: this.turn, seed: this.seed,
        /* 与主仓库 results/games.jsonl 的字段名对齐（mode / turns / vp），
         * 让逐局记录能直接被 tournament/rate.py 一类脚本消费。 */
        mode: 'duel', players: this.players, turns: this.turn, mapAttempt: this.map.attempt || 0,
        score: [this.sides[0].vpScore, this.sides[1].vpScore],
        vp: [this.sides[0].vpScore, this.sides[1].vpScore],
        cards: this.computeCards(),
        events: this.events.length,
        /* 决策快照：看完一局能知道双方最后把母舰调成了什么 */
        alloc: [Object.assign({}, this.sides[0].alloc), Object.assign({}, this.sides[1].alloc)],
        techs: [Object.keys(this.sides[0].techDone).sort(), Object.keys(this.sides[1].techDone).sort()],
        /* 弹道复盘：打了多少发、多少发直击（命中率低说明提前量/地形在起作用） */
        shots: [this.sides[0].shots, this.sides[1].shots],
        hits: [this.sides[0].hits, this.sides[1].hits],
        /* 时序复盘：一局的实际节奏，以及「有多少次思考超过了一个 tick」 */
        timing: {
          tickMs: this.cfg.tickMs, decideMs: this.cfg.decideMs, initMs: this.cfg.initMs,
          orderLatency: ORDER_LATENCY, wallMs: this.turn * this.cfg.tickMs
        },
        decide: [0, 1].map(s => ({
          calls: this.botCalls[s],
          avg_ms: this.botCalls[s] ? Number((this.botMs[s] / this.botCalls[s]).toFixed(3)) : 0,
          max_ms: Number(this.botMax[s].toFixed(3)),
          late: this.botLate[s],
          faults: this.botFaults[s]
        }))
      };
    }
    forfeit(seat, reason) {
      if (this.resultData) return;
      this.forfeited = seat;
      this.endGame(1 - seat, 0, reason || '故障出局：连续失败达到上限（红牌）');
    }
    checkEnd() {
      if (this.resultData) return;
      if (this.forfeited >= 0) return this.endGame(1 - this.forfeited, 0, '故障出局：失败达到上限（红牌）');
      const c0 = this.mothership(0), c1 = this.mothership(1);
      const dead0 = !c0 || c0.hp <= 0, dead1 = !c1 || c1.hp <= 0;
      if (dead0 && dead1) return this.endGame(-1, 1, '双方母舰同时被摧毁，判为平局');
      if (dead0) return this.endGame(1, 1, '红方母舰被摧毁');
      if (dead1) return this.endGame(0, 1, '蓝方母舰被摧毁');
      if (this.turn >= this.cfg.tickLimit) {
        const a = this.sides[0], b = this.sides[1];
        if (a.vpScore !== b.vpScore) return this.endGame(a.vpScore > b.vpScore ? 0 : 1, 2, '时间到 · 按 VP 累计分');
        const kd0 = a.kills - a.losses, kd1 = b.kills - b.losses;
        if (kd0 !== kd1) return this.endGame(kd0 > kd1 ? 0 : 1, 3, '时间到 · VP 相同，按 KD 差');
        const h0 = c0 ? c0.hp / c0.maxHp : 0, h1 = c1 ? c1.hp / c1.maxHp : 0;
        if (Math.abs(h0 - h1) > 1e-6) return this.endGame(h0 > h1 ? 0 : 1, 4, '时间到 · 按母舰剩余 HP');
        return this.endGame(-1, 5, '时间到 · 全部相同，平局');
      }
    }
    result() { if (!this.resultData) this.checkEnd(); return this.resultData; }

    /* 推进一回合；orders[i] 缺席为 {}（对齐 core.py） */
    step(orders) {
      if (this.done) return this.frame();
      /* 记录本回合归一化后的指令 —— 确定性重放的全部外部输入都在这里 */
      const rec = [0, 1].map(s => (Array.isArray(orders[s]) ? orders[s].map(a => {
        if (a.type === 'alloc') return { type: 'alloc', alloc: Object.assign({}, a.alloc) };
        if (a.type === 'produce') return a.by != null
          ? { type: 'produce', kind: a.kind, by: a.by }
          : { type: 'produce', kind: a.kind };
        if (a.type === 'research') return { type: 'research', tech: a.tech };
        if (a.type === 'weapon') return { type: 'weapon' };
        /* 模式动作必须进指令日志，否则重放会丢掉「谁在哪一拍换了模式」→ 行为不一致 */
        if (a.type === 'mode') return a.id != null
          ? { type: 'mode', id: a.id, mode: a.mode }
          : { type: 'mode', mode: a.mode };
        return { id: a.id, type: a.type, x: a.x, y: a.y, targetId: a.targetId };
      }) : []));
      this.orderLog.push({ t: this.turn + 1, o: rec });
      this.turn++;
      this.lastEvents = [];

      /* 1. 阵营动作：改分配目标 / 入生产队列 / 开始研究（都在 tick 开头统一生效） */
      for (let s = 0; s < 2; s++) {
        const acts = Array.isArray(orders[s]) ? orders[s] : [];
        for (const a of acts) {
          if (a.type === 'alloc') {
            this.sides[s].target = Object.assign({}, a.alloc);
          } else if (a.type === 'produce') {
            if (a.by != null) {
              /* 运载舰队列：1 个槽，只有队首在造；同样在开工时才扣钱 */
              const host = this.units.find(u => u.id === a.by && !u.dead && u.side === s && u.kind === 'CARRIER');
              if (!host) { this.noteRejected(s, a.by, 'produce: carrier gone'); continue; }
              host.queue.push({ kind: a.kind, remain: TYPES[a.kind].build });
            } else {
              this.sides[s].queue.push(a.kind);
            }
          } else if (a.type === 'weapon') {
            /* 独立队列：同 tick 内也要再验一次上限，防止绕过 decode 直接 step */
            const S = this.sides[s];
            if (S.weaponsReady + S.weaponQueue.length >= weaponCapOf(S.alloc.attack)) {
              this.noteRejected(s, 0, 'weapon: no free weapon slot'); continue;
            }
            S.weaponQueue.push({ remain: WEAPON_BUILD });
          } else if (a.type === 'research') {
            /* decode 已经校验过一遍；这里再确认一次，避免调用方绕过 decode 直接 step
             * 时把槽位挤爆或重复扣钱。不合法就静默丢弃（语义非法不判故障）。 */
            const chk = this.techAvailable(s, a.tech);
            if (!chk.ok) { this.noteRejected(s, 0, 'research: ' + chk.reason); continue; }
            const def = TECHS[a.tech];
            this.sides[s].cu -= def.cu;
            this.sides[s].techDoing.push({ id: a.tech, remain: def.tick });
            const ev = { t: this.turn, type: 'research-start', side: s, id: a.tech };
            this.lastEvents.push(ev); this.events.push(ev);
          } else if (a.type === 'mode') {
            /* 模式切换：不带 id 就是「全队 + 后续新造的单位」，带 id 只改那一台。
             * 绕过 decode 直接 step 时也要过一遍白名单，否则会把 u.mode 写成垃圾值。 */
            if (MODES.indexOf(a.mode) < 0) { this.noteRejected(s, 0, 'mode: unknown ' + a.mode); continue; }
            if (a.id != null) {
              const one = this.units.find(u => u.id === a.id && !u.dead && u.side === s);
              if (!one) { this.noteRejected(s, a.id, 'mode: unit gone'); continue; }
              one.mode = a.mode;
            } else {
              this.sides[s].mode = a.mode;
              for (const u of this.units) if (!u.dead && u.side === s) u.mode = a.mode;
            }
          }
        }
      }

      /* 2. 分配过渡 → 经济 → 生产 → 研究 */
      for (let s = 0; s < 2; s++) {
        this.stepAlloc(s);
        this.stepEconomy(s);
        this.stepProduction(s);
        this.stepCarriers(s);
        this.stepWeapons(s);
        this.stepResearch(s);
      }

      /* 3. 单位动作：先绑定本回合指令，再由行为层算「这一拍想做什么」 */
      for (let s = 0; s < 2; s++) {
        const acts = (Array.isArray(orders[s]) ? orders[s] : []).filter(a => a.id != null);
        const byId = new Map(acts.map(a => [a.id, a]));
        for (const u of this.units) if (!u.dead && u.side === s) u.order = byId.get(u.id) || null;
      }
      for (const u of this.units) {
        if (u.dead) continue;
        u.aiTarget = null;
        const intent = this.unitIntent(u);
        /* 模式只决定意图，能不能走过去由 applyMove 说了算（坡度/山脉/速度） */
        if (intent.move) this.applyMove(u, intent.move.x, intent.move.y);
        u.aiTarget = intent.targetId;
      }
      /* 3b. 速度估计：弹道解算要给提前量，否则 10~20 tick 的飞行时间里目标早走了。
       * 用「本 tick 位移 / tick 时长」，所以 Bot 突然转向时提前量会滞后一拍 —— 和真炮一样。 */
      for (const u of this.units) {
        if (u.dead) continue;
        u.vx = (u.x - u.px) / (TICK_MS / 1000);
        u.vy = (u.y - u.py) / (TICK_MS / 1000);
        u.px = u.x; u.py = u.y;
      }
      /* 4. 开火：解弹道 → 生成炮弹。伤害不在这一步结算，等炮弹飞过去。 */
      for (const u of this.units) {
        if (u.dead) continue;
        const t = TYPES[u.kind];
        if (t.arc === 'none' || t.range <= 0) continue;
        if (u.cd > 0) { u.cd--; continue; }
        if (this.weaponCount(u) <= 0) continue;          // 攻击分配 0% → 母舰没有武器
        /* 首选顺序：Bot 显式指定的目标 → 行为层算出来的目标 → 射程内最近的敌人。
         * 开雾时三种来源都要过视线检查：看不见的目标不能打（物理定律优先于一切模式）。 */
        const prefId = (u.order && u.order.targetId != null) ? u.order.targetId : u.aiTarget;
        const pref = prefId != null
          ? this.units.find(e => e.id === prefId && !e.dead && e.side !== u.side) : null;
        const prefVisible = pref && (!this.vision.enabled || canSee(this.map, this.units, u.side, pref, this.vision));
        let fired = prefVisible ? this.fire(u, pref) : false;
        if (!fired) {
          const lim = t.range * (1 + this.sides[u.side].mods.range);
          const cands = [];
          for (const e of this.units) {
            if (e.dead || e.side === u.side) continue;
            const d = Math.hypot(u.x - e.x, u.y - e.y) / CELL;
            if (d > lim) continue;
            /* 看不见就不该有候选，否则「隔着山脊自动索敌」会把迷雾白开 */
            if (this.vision.enabled && !canSee(this.map, this.units, u.side, e, this.vision)) continue;
            cands.push({ e, d });
          }
          cands.sort((a, b) => (a.d - b.d) || (a.e.id - b.e.id));
          for (let i = 0; i < Math.min(3, cands.length) && !fired; i++) fired = this.fire(u, cands[i].e);
        }
        if (fired) {
          const rof = t.rof * (1 - Math.min(0.5, this.sides[u.side].mods.rof));
          u.cd = Math.max(1, rof);
        }
      }
      /* 5. 炮弹推进与命中结算：命中在这里发生，而不是在开火那一 tick */
      this.stepShots();
      this.units = this.units.filter(u => !u.dead);
      this.updateVP();
      this.checkEnd();
      const f = this.frame();
      this.frameHashes.push(this.hashFrame(f));
      return f;
    }
  }

  /* ---------------- 确定性校验：同 seed + 同 orders 必得同结果 ---------------- */
  function verifyDeterminism(rec) {
    if (!rec || !Array.isArray(rec.orderLog)) return { ok: false, reason: 'no order log' };
    const cfg = { tickLimit: rec.tickLimit || TICK_LIMIT };
    if (rec.generator) cfg.generator = rec.generator;
    /* 视野开关不改变模拟本身（step 不读它），但重建时仍按原样带上：
     * 一是保持「同配置才能对同结果」的可读性，二是避免以后把视野接进 step 时漏掉这里。 */
    if (rec.vision) cfg.vision = rec.vision;
    /* 时序不参与重放：orderLog 已经按「落地 tick」存好，重放只认 tickLimit + seed。
     * 这正是解耦的好处 —— Bot 想多久都不影响复现。 */
    /* 用「调用方给的 seed」重建：这一步同时验证「同 seed 必得同地图、且地图由 (seed, attempt) 唯一确定」 */
    const g = new Game(2, rec.seed, cfg);
    let mismatch = null;
    if (rec.mapAttempt != null && rec.mapAttempt !== (g.map.attempt || 0)) {
      mismatch = { turn: 0, expected: 'mapAttempt ' + rec.mapAttempt, got: 'mapAttempt ' + (g.map.attempt || 0) };
    }
    if (!mismatch && Array.isArray(rec.frameHashes) && rec.frameHashes[0] !== g.frameHashes[0]) {
      mismatch = { turn: 0, expected: rec.frameHashes[0], got: g.frameHashes[0] };
    }
    for (let k = 0; k < rec.orderLog.length && !mismatch; k++) {
      const e = rec.orderLog[k];
      g.step([e.o[0], e.o[1]]);
      const exp = Array.isArray(rec.frameHashes) ? rec.frameHashes[k + 1] : null;
      const got = g.frameHashes[g.frameHashes.length - 1];
      if (exp != null && exp !== got) mismatch = { turn: g.turn, expected: exp, got };
    }
    const r = g.result();
    let resultMismatch = null;
    if (!mismatch && rec.result && r &&
        (rec.result.winner !== r.winner || rec.result.level !== r.level)) {
      resultMismatch = { expected: rec.result.winner + '/' + rec.result.level, got: r.winner + '/' + r.level };
    }
    return {
      ok: !mismatch && !resultMismatch,
      turns: g.turn, frames: g.frameHashes.length, mismatch, resultMismatch,
      result: r ? { winner: r.winner, level: r.level } : null
    };
  }

  /* 原型也要冻结：否则 Bot 可以 `Game.prototype.encodeTick = ...` 绕过信息集
   * （实测 v0.9 在开雾局里能借此看到敌方全部单位，且零故障）。
   * 冻结后严格模式写原型直接抛 TypeError，非严格模式静默失败 —— 两种都拦得住。 */
  Object.freeze(Game.prototype);
  Object.freeze(Game);

  const API = {
    GRID, CELL, WORLD, N, TICK_LIMIT, VP_RADIUS, MAX_LINE, utf8Length,
    /* 时序：引擎 tick 长度、决策预算、开局预算、指令延迟 */
    TICK_MS, DECIDE_MS, INIT_MS, ORDER_LATENCY,
    WEIGHTS, FIELD_NAMES, TYPES, PRODUCIBLE, RPS, PROTO, rulesHash, Game, generateMap, verifyDeterminism,
    /* 地图生成器的公开面：地貌层与地表类型（渲染/测试/工具都要同一份定义） */
    SURF, SURF_NAMES, MAPGEN: M, MAPGEN_MC: MC, SCALE, PLATFORM_SLOPE,
    encodeMapLayers: M.encodeMapLayers, decodeMapLayers: M.decodeMapLayers,
    GENERATORS, GENERATOR_DEFAULT,
    /* 弹道模块整体导出：测试与 Bot 分析都用得到；rules 与它共用同一份常数 */
    B, SHELLS: B.SHELLS, SHELL_IDS: B.SHELL_IDS, AIR_K: B.AIR_K, G: B.G,
    M_PER_CELL: B.M_PER_CELL, M_PER_PX: B.M_PER_PX, ELEV_M: B.ELEV_M, MOUNTAIN_H: B.MOUNTAIN_H,
    /* 平衡常量一并导出：测试要断言「两侧数值一致」，选手包也需要同一份阈值 */
    UNIT_SLOPE, CARRIER_SLOPE,
    ALLOC_KEYS, ALLOC_STEP, SLOT_STEP, MAX_SLOTS, MAX_WEAPONS, START_ALLOC,
    MOVE_MUL_MIN, MOVE_MUL_MAX,
    START_CU, BASE_INCOME, SALVAGE_RATE, RES_AMOUNT, RES_RADIUS, POP_MAX,
    WEAPON_COST, WEAPON_BUILD, WEAPON_QUEUE_MAX, REPAIR_RADIUS, REPAIR_RATE,
    SMALL_PRODUCIBLE, TECHS, TECH_TREES,
    slotsOf, weaponSlotsOf, weaponCapOf, speedMulOf,
    /* 视野：统一开关的归一化 + 两个判定入口 + 网格。Bot 与网页都调这一份实现，
     * 不允许各自再写一套「看不看得见」的判定（那必然漂移）。 */
    VISION_OFF, EYE_H, LOS_TOL, LOS_RANGE_CELLS,
    normalizeVision, losClear, terrainHeightAt, canSee, visionGrid, pointVisible,
    /* 高度差视野 + 三种行动模式：Bot 与网页都要同一份语义 */
    HEIGHT_BONUS_CELLS, HEIGHT_DIFF_MIN, heightDiffAt, heightBonusAt,
    MODES, DEFAULT_MODE, MODE_DESC, MODE_RETREAT_CELLS,
    /* 仅测试用的内部入口：地图兜底通道（carveCorridor）与连通性/山脉占比，
     * 这些路径无法从公开 API 构造出来（要连续 32 次重试都失败），
     * 但一旦线上触发就是首次执行 —— 所以必须能被测到。
     * 生产代码不应依赖这个字段。 */
    INTERNALS: { carveCorridor, connected, spawnConnected, wallRatio, buildOnce, seedSalt, stepAllowed, slotsOf }
  };
  return deepFreeze(API);
});
