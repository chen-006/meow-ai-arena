// Bradley–Terry 实力分计算器：严格对齐官方评分口径（见 ../../boomerang/tournament/bt.py 与 rating.py）。
// 刻度：SCALE = 400 / ln(10)（差 400 分 ≈ 10:1 胜率），每对交手加 virtual=1 场虚拟平局防发散。
// 支持：
//   1. fullFit：完整矩阵 MM 迭代拟合（与 bt.py 的 fit() 数值逐位对齐）
//   2. rateHumanAgainstAnchors：固定官方 AI 参照分，对玩家多局 1v1 + 4人混战战绩精确求最大似然实力分与 95% 区间！
//      （与探索小实验三波「固定官方海选对手实力分拟合新选手」同一数学模型！）

export const BT_SCALE = 400 / Math.LN10;

export interface PairObs {
  a: string;
  b: string;
  score: number; // 1 = a胜, 0.5 = 平, 0 = b胜
}

export interface FfaResult {
  seat: number;
  id: string;
  rank: number;   // 1..4，并列取最好名次
  kills: number;
}

/** 官方四人局单局得分：名次分（3/2/1/0，并列平分对应档位）+ 每杀 1 分 */
export function ffaScores(results: FfaResult[]): Record<string, number> {
  const rankPts = [3, 2, 1, 0];
  const byRank = new Map<number, FfaResult[]>();
  for (const r of results) {
    const arr = byRank.get(r.rank) || [];
    arr.push(r);
    byRank.set(r.rank, arr);
  }
  const out: Record<string, number> = {};
  for (const [rk, group] of byRank) {
    let sum = 0;
    for (let i = 0; i < group.length; i++) sum += rankPts[rk - 1 + i] ?? 0;
    const avg = sum / group.length;
    for (const r of group) out[r.id] = avg + r.kills;
  }
  return out;
}

/** 四人局转 6 组两两比较（同 rating.py pair_rows） */
export function ffaToPairs(results: FfaResult[]): PairObs[] {
  const sc = ffaScores(results);
  const out: PairObs[] = [];
  for (let i = 0; i < results.length; i++) {
    for (let j = i + 1; j < results.length; j++) {
      const a = results[i].id, b = results[j].id;
      const va = sc[a], vb = sc[b];
      out.push({ a, b, score: va > vb ? 1 : va === vb ? 0.5 : 0 });
    }
  }
  return out;
}

/** 与官方 bt.py 完全一致的 Bradley-Terry MM 迭代拟合 */
export function btFit(
  pairs: Map<string, [number, number]>, // key "a\0b" (a<b) -> [games, aWins]
  names: string[],
  anchor: string | null = null,
  virtual = 1.0,
  tol = 1e-9,
  maxIter = 200000,
): Record<string, number | null> {
  const games: [string, string, number, number][] = [];
  const activeSet = new Set<string>();
  for (const [k, [g, w]] of pairs) {
    if (g <= 0) continue;
    const [a, b] = k.split('\0');
    games.push([a, b, g + virtual, w + virtual / 2]);
    activeSet.add(a);
    activeSet.add(b);
  }
  const active = [...activeSet].sort();
  if (!active.length) return Object.fromEntries(names.map(n => [n, null]));

  const wins: Record<string, number> = Object.fromEntries(active.map(n => [n, 0]));
  for (const [a, b, g, w] of games) {
    wins[a] += w;
    wins[b] += g - w;
  }
  const p: Record<string, number> = Object.fromEntries(active.map(n => [n, 1.0]));

  for (let it = 0; it < maxIter; it++) {
    const denom: Record<string, number> = Object.fromEntries(active.map(n => [n, 0]));
    for (const [a, b, g] of games) {
      const v = g / (p[a] + p[b]);
      denom[a] += v;
      denom[b] += v;
    }
    let center = 0;
    const next: Record<string, number> = {};
    for (const n of active) {
      next[n] = wins[n] / denom[n];
      center += Math.log(next[n]);
    }
    const scale = Math.exp(-center / active.length);
    let delta = 0;
    for (const n of active) {
      next[n] *= scale;
      delta = Math.max(delta, Math.abs(Math.log(next[n] / p[n])));
      p[n] = next[n];
    }
    if (delta < tol) break;
  }

  const origin = anchor !== null && anchor in p
    ? Math.log(p[anchor])
    : active.reduce((s, n) => s + Math.log(p[n]), 0) / active.length;

  const out: Record<string, number | null> = Object.fromEntries(names.map(n => [n, null]));
  for (const n of active) out[n] = BT_SCALE * (Math.log(p[n]) - origin);
  return out;
}

export function expectedScore(ra: number, rb: number): number {
  return 1 / (1 + Math.pow(10, (rb - ra) / 400));
}

export interface HumanMatchRecord {
  oppId: string;
  oppRating: number;
  score: number;     // 1 / 0.5 / 0
  mode: 0 | 1;
}

export interface HumanBtEstimate {
  rating: number;
  se: number;
  ci95: [number, number];
  duelRating: number | null;
  ffaRating: number | null;
  games: number;
  points: number;
  expectedAtEst: number;
}

/**
 * 固定官方 AI 参照分，对玩家的一组两两比较结果用牛顿法求最大似然 Bradley-Terry 实力分。
 * 与官方 bt.py 同口径：每个交手过的对手附带 virtualDraw 局虚拟平局（默认 0.5 局，确保全胜/全负也能给出合理有限估值），
 * 并用 Fisher 信息量 I(θ) 给出渐近标准误 SE 与 95% 置信区间！
 */
function solveSinglePool(obs: { oppRating: number; score: number }[], virtualPerOpp = 0.5, priorSd = 0): { rating: number; se: number } | null {
  if (!obs.length) return null;
  // 按对手合并并加入虚拟平局
  const byOpp = new Map<number, { g: number; w: number }>();
  for (const o of obs) {
    const k = Math.round(o.oppRating * 10);
    const cur = byOpp.get(k) || { g: 0, w: 0 };
    cur.g += 1;
    cur.w += o.score;
    byOpp.set(k, cur);
  }
  const items = [...byOpp.entries()].map(([k, v]) => ({
    thetaOpp: (k / 10) / BT_SCALE,
    g: v.g + virtualPerOpp,
    w: v.w + virtualPerOpp * 0.5,
  }));

  // 最大化 L(θ) = Σ [w_i * ln σ(θ - θ_i) + (g_i - w_i) * ln(1 - σ(θ - θ_i))]
  // 导数 L'(θ) = Σ (w_i - g_i * p_i) = 0；二阶导 L''(θ) = -Σ g_i * p_i * (1 - p_i)
  // 可选高斯先验 N(0, priorSd²)（定级未满时用，避免 1 局输给弱 AI 就跌到 -600 这种极端值）
  const pInv = priorSd > 0 ? (BT_SCALE / priorSd) ** 2 : 0;
  let theta = 0;
  for (let it = 0; it < 60; it++) {
    let grad = -theta * pInv, hess = pInv;
    for (const itm of items) {
      const p = 1 / (1 + Math.exp(-(theta - itm.thetaOpp)));
      grad += itm.w - itm.g * p;
      hess += itm.g * p * (1 - p);
    }
    if (hess < 1e-12) break;
    const step = grad / hess;
    theta += Math.max(-2, Math.min(2, step));
    if (Math.abs(step) < 1e-10) break;
  }
  let info = pInv;
  for (const itm of items) {
    const p = 1 / (1 + Math.exp(-(theta - itm.thetaOpp)));
    info += itm.g * p * (1 - p);
  }
  const rating = theta * BT_SCALE;
  const se = info > 1e-9 ? BT_SCALE / Math.sqrt(info) : 180;
  return { rating, se };
}

/**
 * priorSd > 0：定级校准中的温和估计（MAP，先验以 0 分为中心、标准差 priorSd）；
 * priorSd = 0（默认）：与官方 bt.py 同口径的最大似然估计（只加 0.5 局虚拟平局）。
 */
export function estimateHumanRating(records: HumanMatchRecord[], virtualPerOpp = 0.5, priorSd = 0): HumanBtEstimate {
  const duelObs = records.filter(r => r.mode === 0);
  const ffaObs = records.filter(r => r.mode === 1);
  const duel = solveSinglePool(duelObs, virtualPerOpp, priorSd);
  const ffa = solveSinglePool(ffaObs, virtualPerOpp, priorSd);
  const all = solveSinglePool(records, virtualPerOpp, priorSd);

  let rating = 0, se = 160;
  if (duel && ffa) {
    rating = (duel.rating + ffa.rating) / 2;
    se = 0.5 * Math.sqrt(duel.se * duel.se + ffa.se * ffa.se);
  } else if (all) {
    rating = all.rating;
    se = all.se;
  }
  const points = records.reduce((s, r) => s + r.score, 0);
  const expectedAtEst = records.reduce((s, r) => s + expectedScore(rating, r.oppRating), 0);
  return {
    rating: +rating.toFixed(1),
    se: +se.toFixed(1),
    ci95: [+(rating - 1.96 * se).toFixed(1), +(rating + 1.96 * se).toFixed(1)],
    duelRating: duel ? +duel.rating.toFixed(1) : null,
    ffaRating: ffa ? +ffa.rating.toFixed(1) : null,
    games: records.length,
    points,
    expectedAtEst: +expectedAtEst.toFixed(2),
  };
}
