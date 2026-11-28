/* =====================================================================
 * ballistics.js — 外弹道（UMD，无依赖、无 DOM）
 *
 * 这一版把「命中即扣血」换成「发射一枚炮弹，按空气阻力 + 重力飞出去，
 * 命中时按落速与弹种结算伤害」。三条约定：
 *
 *   1. 运动方程是牛顿阻力 + 重力，阻力按二次律（与真实外弹道一致）：
 *          dv/dt = g_vec - k·|v|·v
 *      其中 k = ρ·C_d·A / (2m) 是「阻力常数」，取全局唯一值 —— 题目要求
 *      「空气阻力是常数」，所以不按弹种/温度/高度变化。
 *   2. 伤害只看落速与弹种：动能 ∝ v²，所以射程越远、落速越低、伤害越小；
 *      高打低会多得一点重力势能，低打高会少一点 —— 这是算出来的，不是补的系数。
 *   3. 弹道本身参与判定：掠过山体（base===3 的格子）时会被挡下来，
 *      高抛弹道能越过去。所以「山脉挡直射、曲射能越过」不再是一条规则，
 *      而是把弹道积出来看它撞没撞山。
 *
 * ---------------------- 常数是怎么定的 ----------------------
 * 空气阻力常数
 *   k = ρ·C_d·A / (2m)，ρ=1.225 kg/m³（海平面 20℃）、C_d=0.3（尖头弹体的典型值，
 *   流线型可低至 0.04，球形约 0.47）。按 30mm 口径、0.4kg 弹丸算 ≈ 1.0e-3 1/m；
 *   按 120mm、15kg 算 ≈ 1.4e-4 1/m；按 155mm 榴弹、43kg 算 ≈ 8e-5 1/m。
 *   本作所有弹种共用一个常数，取中偏上的 **4.0e-4 1/m**：
 *   它对应「中小口径、中等长径比」的炮弹，在 128~560 m 的射程上会让落速掉 5%~25%，
 *   正好能看出「远射伤害衰减」而不至于让远射完全无用。
 *
 * 重力与尺度
 *   1 格 = 40 m（64 格地图 = 2.56 km），重力取 **300 m/s²**（≈30 g）。
 *   这不是地球重力，是「重力标度」：若用 9.81，600 tick（30 秒）的对局里
 *   炮弹在 100~500 m 上的弹道会完全贴地，抛物线退化成直线，
 *   地形遮挡、曲射越顶、俯射加成这三件事就全都消失了。
 *   300 与射程/初速/50ms tick 一起标定：直射弹道在射程内的弧度是 3~40 m，
 *   高抛弹道拱高 100 m 以上，正好横跨地形起伏（0~76 m）。
 *
 * 高程
 *   map.elev 是 0~255 的无量纲高度场，按 **0.30 m/级** 折算成 0~76 m 的实际高度。
 *   坦克炮在 128 m 上的弹道下垂约 5 m，所以沙丘能挡住它，山脊（40 m+）更挡得住。
 * ===================================================================== */
(function (root, factory) {
  const m = factory();
  if (typeof module === 'object' && module.exports) module.exports = m;
  else { root.WoDRef = root.WoDRef || {}; root.WoDRef.Ballistics = m; }
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  /* ---------------------- 常数 ---------------------- */
  const RHO = 1.225;            // kg/m³，海平面 20℃
  const AIR_K = 4.0e-4;         // 1/m，阻力常数 k = ρCdA/2m
  const G = 300;                // m/s²，重力标度（见文件头）
  const M_PER_CELL = 10;        // 1 格 = 10 m（256² 网格，世界仍是 2.56 km）
  /* 与 rules.js / mapgen 的 CELL 必须一致（像素/格）。256² 网格下 CELL = 6：
   * 这一条**只能靠测试守**（规则表冻结 + 断言 R.CELL === B.CELL_PX），
   * 因为 ballistics 是独立 UMD 模块，拿不到 rules 的常数。 */
  const CELL_PX = 6;
  const M_PER_PX = M_PER_CELL / CELL_PX;
  const ELEV_M = 0.30;          // 每级高程 = 0.30 m
  const MOUNTAIN_H = 55;        // 山体等效高度（m）：山脊 base===3 的格子按这个算
  const UNIT_H = 2.4;           // 单位中心离地高度（m），用来判「炮弹是否打到车身」
  const HIT_R_M = 7.5;          // 命中半径（m）≈ 0.19 格，比车身略宽
  const DT = 0.01;              // 积分步长（s）：0.24 m @24m/s，不会穿模
  const MAX_T = 8;              // 单发最长飞行时间（s），超时按自毁处理
  const V_REF = 300;            // 伤害标定的参考落速（只用于文档与测试）

  /* ---------------------- 弹种 ----------------------
   * mass  弹丸质量（kg，只用于展示与说明；阻力常数全局统一，不按质量变）
   * vMul  初速倍率：硬芯弹更轻所以更快，高爆弹更重所以更慢
   * exp   落速→伤害的指数：动能弹接近 2（纯动能），高爆弹 0.6（化学能为主）
   * d0    弹种伤害系数
   * armor 对不同装甲等级的修正（light: LAV/Salvager/Railgun/Artillery，heavy: AAV/母舰/运载舰）
   * splash/splashDmg 溅射半径（m）与溅射伤害系数
   * unlock 需要哪项科技才解锁（null = 开局就有） */
  const SHELLS = {
    AP: {
      id: 'AP', name: '穿甲弹', mass: 7.0, vMul: 1.00, exp: 2.0, d0: 1.00,
      armor: { light: 1.00, heavy: 1.15 }, splash: 0, splashDmg: 0, unlock: null
    },
    HE: {
      id: 'HE', name: '高爆弹', mass: 12.0, vMul: 0.92, exp: 0.6, d0: 0.75,
      armor: { light: 1.25, heavy: 0.65 }, splash: 30, splashDmg: 0.55, unlock: 'F2'
    },
    APCR: {
      id: 'APCR', name: '硬芯穿甲弹', mass: 4.2, vMul: 1.28, exp: 2.0, d0: 0.90,
      armor: { light: 0.65, heavy: 1.50 }, splash: 0, splashDmg: 0, unlock: 'F3'
    }
  };
  const SHELL_IDS = ['AP', 'HE', 'APCR'];
  /* 弹种表也是规则：进程内 Bot 能 require 到这里，深冻结把「偷偷改弹种系数」
   * 变成显式报错（同 rules.js 的 deepFreeze，这里只针对这一张表，足够）。 */
  for (const id of SHELL_IDS) {
    Object.freeze(SHELLS[id].armor);
    Object.freeze(SHELLS[id]);
  }
  Object.freeze(SHELLS);
  Object.freeze(SHELL_IDS);

  /* ---------------------- 运动积分 ---------------------- */
  /* 状态：s = 沿射向的水平距离（m），h = 高度（m），vx/vh = 速度分量（m/s）。
   * 用 RK4：二次阻力下显式欧拉在 dt=10ms 时误差已经可见（远射差 2~3 m）。 */
  function makeShellState(v0, thetaDeg, h0) {
    const th = thetaDeg * Math.PI / 180;
    return { s: 0, h: h0, vx: v0 * Math.cos(th), vh: v0 * Math.sin(th), t: 0 };
  }
  function deriv(s, h, vx, vh, out) {
    const v = Math.hypot(vx, vh);
    out[0] = vx;
    out[1] = vh;
    out[2] = -AIR_K * v * vx;
    out[3] = -G - AIR_K * v * vh;
  }
  /* 推进一步（固定 dt），返回推进后的状态副本；不改原对象，方便「试算」与「实走」共用 */
  function advance(st, dt) {
    const k1 = [0, 0, 0, 0], k2 = [0, 0, 0, 0], k3 = [0, 0, 0, 0], k4 = [0, 0, 0, 0];
    deriv(st.s, st.h, st.vx, st.vh, k1);
    deriv(st.s + dt / 2 * k1[0], st.h + dt / 2 * k1[1], st.vx + dt / 2 * k1[2], st.vh + dt / 2 * k1[3], k2);
    deriv(st.s + dt / 2 * k2[0], st.h + dt / 2 * k2[1], st.vx + dt / 2 * k2[2], st.vh + dt / 2 * k2[3], k3);
    deriv(st.s + dt * k3[0], st.h + dt * k3[1], st.vx + dt * k3[2], st.vh + dt * k3[3], k4);
    const out = {
      s: st.s + dt / 6 * (k1[0] + 2 * k2[0] + 2 * k3[0] + k4[0]),
      h: st.h + dt / 6 * (k1[1] + 2 * k2[1] + 2 * k3[1] + k4[1]),
      vx: st.vx + dt / 6 * (k1[2] + 2 * k2[2] + 2 * k3[2] + k4[2]),
      vh: st.vh + dt / 6 * (k1[3] + 2 * k2[3] + 2 * k3[3] + k4[3]),
      t: st.t + dt
    };
    return out;
  }
  const speedOf = st => Math.hypot(st.vx, st.vh);

  /* 无阻力解析解：只用来给求解器做初值估算（θ 在 0~45° 区间单调，可用它定初值） */
  function noDragAngle(d, dh, v0) {
    const v2 = v0 * v0;
    const disc = v2 * v2 - G * (G * d * d + 2 * dh * v2);
    if (disc < 0) return null;
    const root = Math.sqrt(disc);
    const low = Math.atan((v2 - root) / (G * d));
    const high = Math.atan((v2 + root) / (G * d));
    return { low: low * 180 / Math.PI, high: high * 180 / Math.PI };
  }

  /* 从 (0, h0) 以 theta 发射，积分到「降到 targetH」或落地或超时。
   * 只做纯弹道（不掺地形），地形遮挡由 blockedByTerrain 单独判 ——
   * 两件事分开，求解器才不会被「提前撞地」的样本带偏。
   * 返回 { theta, points, t, s, h, vImpact, apex }：s 是降到 targetH 时的水平距离。 */
  function fly(v0, thetaDeg, h0, targetH, opts) {
    const o = opts || {};
    const dtMax = o.dt || DT;
    const maxT = o.maxT || MAX_T;
    let st = makeShellState(v0, thetaDeg, h0);
    let apex = st.h, prev = st, nextSample = 4;
    const points = [{ s: 0, h: h0 }];
    /* 关键：只有在「从上方降下来穿过 targetH」时才算到达，不能一出发就判到达。
     * 打上坡时炮口本来就在目标高度以下，若不区分，任何仰角都会立刻返回 s≈0，
     * 求解器于是永远找不到解（这一版真踩过：整局 7000 次开火全部 out-of-envelope）。 */
    let above = h0 > targetH;
    while (st.t < maxT) {
      const nxt = advance(st, dtMax);
      if (nxt.h > targetH) above = true;
      if (above && nxt.h <= targetH) {
        let lo = st, hi = nxt;
        for (let i = 0; i < 14; i++) {
          const mid = advance(lo, (hi.t - lo.t) / 2);
          if (mid.h <= targetH) hi = mid; else lo = mid;
        }
        const v = speedOf(hi);
        points.push({ s: hi.s, h: hi.h });
        return { theta: thetaDeg, points, t: hi.t, s: hi.s, h: hi.h, vImpact: v, apex, ok: true };
      }
      st = nxt;
      if (st.h > apex) apex = st.h;
      /* 采样按「弧长每 4 m 一个点」，而不是「每 N 步」：
       * 每 N 步在高速弹上会变成 50 m 一个点，山脉（几十米宽）会被整段跳过，
       * 于是「山的另一侧也能直射」—— 这是采样密度导致的假阴性。 */
      if (st.s >= nextSample) { points.push({ s: st.s, h: st.h }); nextSample = st.s + 4; }
      if (st.h <= 0 && st.t > 0.05) break;   // 打到地面以下：够不到那个高度
    }
    /* 够不到目标高度：返回 0，让求解器知道这个仰角无效 */
    return { theta: thetaDeg, points, t: st.t, s: 0, h: st.h, vImpact: speedOf(st), apex, ok: false };
  }

  /* 解「打到水平距离 d、高度 targetH」的所有仰角（低伸解在前）。
   *
   * 有阻力时没有闭式解，而且阻力让高抛解与低伸解不再对称（高抛飞得久、掉速更多），
   * 所以用「扫描找跨零点 + 二分」：射程在 θ→45° 前单调增、之后单调减，
   * 一个目标距离最多两个解，都能被扫出来。 */
  function solutions(v0, h0, d, targetH, elevMin, elevMax, opts) {
    const dt = (opts && opts.dt) || DT;
    const rangeAt = (th) => fly(v0, th, h0, targetH, { dt }).s;
    const N = (opts && opts.scan) || 16;
    const out = [];
    let prevTh = null, prevR = null;
    for (let i = 0; i <= N; i++) {
      const th = elevMin + (elevMax - elevMin) * i / N;
      const r = rangeAt(th);
      if (prevR != null) {
        const f0 = prevR - d, f1 = r - d;
        if ((f0 <= 0 && f1 >= 0) || (f0 >= 0 && f1 <= 0)) {
          let lo = prevTh, hi = th, rl = prevR;
          for (let k = 0; k < 10; k++) {
            const mid = (lo + hi) / 2;
            const rm = rangeAt(mid);
            if ((rl - d) * (rm - d) <= 0) { hi = mid; } else { lo = mid; rl = rm; }
          }
          const th2 = (lo + hi) / 2;
          const r2 = rangeAt(th2);
          if (Math.abs(r2 - d) <= Math.max(4, d * 0.02)) out.push(Object.assign({ s: r2 }, fly(v0, th2, h0, targetH, { dt }), { theta: th2 }));
        }
      }
      prevTh = th; prevR = r;
    }
    /* 排序：仰角小的先（飞得短、掉速少、到得快） */
    out.sort((a, b) => a.theta - b.theta);
    /* 去重（同一解被相邻区间各算一次） */
    const uniq = [];
    for (const s of out) if (!uniq.length || Math.abs(s.theta - uniq[uniq.length - 1].theta) > 0.15) uniq.push(s);
    return uniq;
  }

  /* 在解集里挑一个「打得到且不被山挡住」的弹道。
   * points 是纯弹道的采样点（s, h），groundAt 给该 arc 距离处的阻挡高度。 */
  function solve(params) {
    const sols = solutions(params.v0, params.h0, params.d, params.h0 + params.dh,
      params.elevMin, params.elevMax, params);
    if (!sols.length) return { ok: false, reason: 'out-of-envelope' };
    const gAt = params.groundAt || (() => -Infinity);
    for (const s of sols) {
      const b = blockedByTerrain(s.points, gAt);
      if (!b.blocked) {
        return { ok: true, theta: s.theta, t: s.t, vImpact: s.vImpact, apex: s.apex, s: s.s, path: s.points };
      }
    }
    return { ok: false, reason: 'blocked', theta: sols[0].theta, solutions: sols.length };
  }

  /* 弹道是否会被山挡住：把弹道按 arc 距离采样，看有没有落在地面（含山体）以下。
   * 只有 base===3（山脉）会真的把弹道截断 —— 沙丘的起伏按物理也挡得住，
   * 但那样「随便一个 5 m 的坎就能挡炮弹」会让直射武器几乎无法交战，
   * 所以碰撞只认山脉，其余地形当可掠过（战场尺度上的简化，写在 README 里）。 */
  function blockedByTerrain(points, groundAt) {
    for (const p of points) if (p.h < groundAt(p.s)) return { blocked: true, at: p };
    return { blocked: false };
  }

  /* ---------------------- 伤害 ----------------------
   * 动能弹：伤害 ∝ 落速²（½mv² 的能量），所以初速 +10% 伤害 +21%；
   * 高爆弹：化学能为主，落速指数 0.6，速度影响小但仍然存在。
   * 归一化用「该单位的基础初速」，于是在基础科技、零距离时伤害恰好等于 TYPES[k].dmg，
   * 旧版的数值平衡原样保留，只是现在它是「物理算出来的一个点」。 */
  function damageOf(shellId, dmg, baseV0, vImpact, armorClass) {
    const sh = SHELLS[shellId] || SHELLS.AP;
    const ratio = Math.max(0.02, vImpact / Math.max(1, baseV0));
    const armor = (sh.armor && sh.armor[armorClass]) || 1;
    return dmg * sh.d0 * Math.pow(ratio, sh.exp) * armor;
  }
  /* 溅射：中心全伤，边缘按 1-r/R 线性衰减 */
  function splashFactor(dist, radius) {
    if (!radius || dist >= radius) return 0;
    return 1 - dist / radius;
  }

  return {
    RHO, AIR_K, G, M_PER_CELL, CELL_PX, M_PER_PX, ELEV_M, MOUNTAIN_H,
    UNIT_H, HIT_R_M, DT, MAX_T, V_REF, SHELLS, SHELL_IDS,
    makeShellState, advance, speedOf, fly, solutions, solve, noDragAngle,
    blockedByTerrain, damageOf, splashFactor,
    /* 便于测试：纯无阻力射程公式，用来校验积分器 */
    noDragRange: (v0, thetaDeg) => v0 * v0 * Math.sin(2 * thetaDeg * Math.PI / 180) / G
  };
});
