'use strict';
/* vectors.js · 生成 Go 契约测试用的 JS 向量（rng / hash / ballistics）
 *
 * 运行：node engine/parity/vectors.js   （在 war_of_desert/ 下）
 * 输出 JSON 到 stdout，用 node engine/parity/vectors.js > vectors.json 保存。
 * 这些向量是「移植正确的下界」：rng/hash 是纯整数运算，必须逐位一致；
 * ballistics 浮点允许 1e-9 相对误差（Math.hypot/pow 的 ulp 差异）。
 */
const M = require('../../../Dev/MVP/referee/mapgen.js');
const B = require('../../../Dev/MVP/referee/ballistics.js');

const out = {};

/* mulberry32：3 个种子各取前 8 个值 */
out.mulberry32 = {};
for (const s of [20261109, 1, 4294967295]) {
  const rng = M.makeRng(s);
  out.mulberry32[s] = Array.from({ length: 8 }, () => rng());
}

/* hash2：整数格点 + 负数格点 + 小数格点（Math.floor 后取哈希） */
out.hash2 = {};
for (const [s, x, y] of [
  [20261109, 0, 0], [20261109, 1, 2], [42, -3, 7], [7, 255, 255], [7, 1.5, 2.25],
]) {
  out.hash2[`${s}|${x}|${y}`] = M.hash2(s, x, y);
}

/* seedSalt：(seed, attempt) 若干对 + 「不同 seed 的第 n 次重试不撞盐」抽样验证 */
out.seedSalt = {};
for (const [s, a] of [[20261109, 0], [20261109, 1], [20261109, 31], [1, 0], [20261110, 0]]) {
  out.seedSalt[`${s}|${a}`] = M.seedSalt(s, a);
}

/* valueNoise / fbm / fbmN 各取几个点 */
out.noise = {};
for (const [s, x, y] of [[20261109, 12.4, 8.9], [7, 0.5, 0.5], [99, 123.75, 456.25]]) {
  out.noise[`${s}|${x}|${y}`] = { vn: M.valueNoise(s, x, y), fbm6: M.fbm(s, x, y), fbm3: M.fbmN(s, x, y, 3) };
}

/* fnv1a（UTF-16 口径）：与 rules.js 内部同款实现（rules.js 未导出，这里内联） */
const fnv1a = s => {
  let h = 2166136261;
  for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); }
  return (h >>> 0).toString(16).padStart(8, '0');
};
out.fnv1a = {};
for (const s of ['', 'a', 'hello', 'wod-arena', '中文中文中文', 'VP 控制', '{"json":true,"n":1.5}', 'x'.repeat(1000)]) {
  out.fnv1a[s.length > 40 ? 'long1000' : (s === '' ? 'empty' : s)] = fnv1a(s);
}

/* ballistics：fly / solutions / damageOf / splashFactor 采样 */
out.ballistics = {};
{
  out.ballistics.constants = { RHO: B.RHO, AIR_K: B.AIR_K, G: B.G, M_PER_CELL: B.M_PER_CELL, CELL_PX: B.CELL_PX, ELEV_M: B.ELEV_M, MOUNTAIN_H: B.MOUNTAIN_H, UNIT_H: B.UNIT_H, HIT_R_M: B.HIT_R_M, DT: B.DT, MAX_T: B.MAX_T };
  /* fly：128 m 直射（低伸），300 m 高抛 */
  const fly1 = B.fly(240, 2, 2.4, 2.4, {});
  const fly2 = B.fly(240, 30, 2.4, 2.4, {});
  const fly3 = B.fly(180, 45, 2.4, 0, {});
  out.ballistics.fly = [
    { case: '240m/s θ2° h0=2.4→2.4', ok: fly1.ok, s: fly1.s, t: fly1.t, vImpact: fly1.vImpact, apex: fly1.apex, nPoints: fly1.points.length },
    { case: '240m/s θ30° h0=2.4→2.4', ok: fly2.ok, s: fly2.s, t: fly2.t, vImpact: fly2.vImpact, apex: fly2.apex, nPoints: fly2.points.length },
    { case: '180m/s θ45° h0=2.4→0', ok: fly3.ok, s: fly3.s, t: fly3.t, vImpact: fly3.vImpact, apex: fly3.apex, nPoints: fly3.points.length },
  ];
  /* solutions：240 m/s 打 128 m / 300 m（无地形） */
  const sols1 = B.solutions(240, 2.4, 128, 2.4, 0, 45, {});
  const sols2 = B.solutions(240, 2.4, 300, 2.4, 0, 45, {});
  out.ballistics.solutions = [
    { case: 'd=128', thetas: sols1.map(x => x.theta), s: sols1.map(x => x.s) },
    { case: 'd=300', thetas: sols2.map(x => x.theta), s: sols2.map(x => x.s) },
  ];
  /* damageOf：零距离基准（=dmg）与 vImpact 减半 */
  out.ballistics.damage = {
    apZero: B.damageOf('AP', 10, 300, 300, 'light'),
    apHalf: B.damageOf('AP', 10, 300, 150, 'light'),
    heHalfHeavy: B.damageOf('HE', 10, 300, 150, 'heavy'),
    apcrHalfLight: B.damageOf('APCR', 10, 300, 150, 'light'),
  };
  out.ballistics.splash = [B.splashFactor(0, 30), B.splashFactor(15, 30), B.splashFactor(30, 30), B.splashFactor(31, 30), B.splashFactor(10, 0)];
  /* noDrag 对照：积分器 vs 解析解（θ=20°，v0=300） */
  out.ballistics.noDrag = { analytic: B.noDragRange(300, 20), flyS: B.fly(300, 20, 0, 0, { dt: 0.001, maxT: 30 }).s };
}
console.log(JSON.stringify(out, null, 2));
