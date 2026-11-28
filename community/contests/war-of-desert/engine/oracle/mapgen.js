/* =====================================================================
 * mapgen.js — 地图生成：地貌过程 + 目标点搜索（UMD，无 DOM / 无第三方依赖）
 *
 * 旧实现（v0.10 及以前）是「fbm + 正弦脊 + 到中心距离」三样噪声直接堆高程，
 * 再把 4 个 VP / 4 个资源点写死成常量、在周围强行压出 5×5 的方形平台。
 * 结果是：地形像随机斑点（没有水系、没有风向、没有山前堆积），而 VP 位置
 * 和地形无关 —— 先有结论再改地形，方块台地本身就是「不真实」的来源。
 *
 * 这一版换成**按地貌过程生成**，四步走，每一步都对应一种真实营力：
 *
 *   1. 构造地貌（tectonic / structural）
 *      低地 + 向盆地中心的区域倾斜 → 山体（massif）与平顶台地（mesa）→
 *      单面山（escarpment，长条状陡坎）。山体是「块状隆起 + 边缘陡崖」，
 *      不是噪声斑点；台地顶部平坦、四周是崖带。
 *
 *   2. 流水地貌（fluvial）
 *      先做洼地填平（priority-flood），再 D8 汇流算汇水面积，
 *      按汇水面积刻蚀**干河谷（wadi）**：汇水越大切得越深越宽，
 *      从山前一路汇流到盆地中心的**盐沼（playa）**。这是内流盆地
 *      （endorheic basin）的典型格局：水不出去，最后蒸发成盐壳。
 *      干河谷天然形成通道与隘口 —— 这是地图上真正的 chokepoint 来源，
 *      不是靠随机摆石头摆出来的。
 *
 *   3. 风成地貌（aeolian）
 *      主风向下的**横向沙丘场**：迎风坡缓、背风坡陡（不对称剖面），
 *      叠加一层短波长小沙丘。沙丘只长在「有沙可供、坡度不大、不在河床
 *      和盐沼里」的地方 —— 这是风沙搬运的选择性堆积，不是全图加正弦波。
 *      背风陡坡会真实影响移动速度（applyMove 按坡度算），所以沙丘是
 *      「能过但难走」，而不是装饰。
 *
 *   4. 山前堆积（alluvial fan）
 *      山体出山口、坡度骤降处标记冲积扇：砾质硬地，通行快。
 *
 * 之后才做**目标点搜索**：出生点、VP、资源点全部由算法在地形上选，
 * 生成器不再是「先定坐标再压平台」。
 *
 * ---------------------- 对称性（公平性） ----------------------
 * 赛制要求双方严格等价（Plan/Docs D-02）。做法是：**先在整幅图上演化地貌
 * 过程，再把每个连续场沿「反对角线」做一次交叉淡化**：
 *
 *     f'(p) = (1-w)·f(p) + w·f(R p),   R = 180° 点旋转
 *     w = 0.5 + 0.5·smoothstep(-B, B, d),  d = p 到反对角线的有符号距离
 *
 * 因为 d(R p) = -d(p)，所以 w(R p) = 1 - w(p)，于是 f'(R p) = f'(p) ——
 * **逐格严格对称**，不是「近似对称」。淡化带宽 B 只有 2.5 格（100 m），
 * 影响的是缝线两侧极窄的一条，换来的是一整幅符合地貌学的地形。
 *
 * 关键细节：对称化的是**连续场**（高程、岩性、沙丘振幅、汇水对数、盐沼、
 * 冲积扇），离散的 base/surf 由对称化后的连续场**分类**得到。这样分类边界
 * 跟着地貌走，不会在缝线上出现一条直线状的地表类型接缝。
 *
 * ---------------------- 确定性 ----------------------
 * 只用整数哈希与种子化 PRNG，不碰 Math.random；所有排序都带 index 兜底
 * 比较，保证同 (seed, attempt) 逐格可复现。generateMap 的重试语义与旧版
 * 一致：seed 是调用方给的，attempt 是内部第几次重试。
 * ===================================================================== */
(function (root, factory) {
  const m = factory();
  if (typeof module === 'object' && module.exports) module.exports = m;
  else { root.WoDRef = root.WoDRef || {}; root.WoDRef.MapGen = m; }
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  /* 网格：256×256，世界仍是 2.56 km（1 格 = 10 m），像素比例 CELL = 6
   * （画布 1536²，和 v0.10 的 768² 同量级，不会突然变成超大 canvas）。
   *
   * S = GRID/64 是「相对 v0.10 的格密度倍数」。v0.10 里所有以「格」为单位的
   * 尺寸（沙丘波长、峡谷长度、平台半径、VP 环带、坡度阈值…）都是按 64 格标定的，
   * 现在统一乘 S 就能保持**物理尺度不变** —— 这是这轮升级最重要的一条纪律：
   * 提分辨率是为了让地形更细，不是为了把地图放大或把射程变短。 */
  const GRID = 256, CELL = 6, WORLD = GRID * CELL, N = GRID * GRID, HALF = N / 2;
  const S = GRID / 64;

  /* 地表类型（surf）：给渲染、测试与后续分析用；**规则只读 base**。
   * 两者分开是因为「规则要的通行系数」只有 5 档（沙/砾/丘/山/平台），
   * 而「看上去像什么地貌」需要更细的区分（河床、盐沼、冲积扇）。 */
  const SURF = { SAND: 0, REG: 1, DUNE: 2, ROCK: 3, WADI: 4, PLAYA: 5, FAN: 6, PAD: 7 };
  const SURF_NAMES = ['沙席', '砾漠', '沙丘', '岩壁', '干河谷', '盐沼', '冲积扇', '平台'];
  /* surf → base：base 的取值与语义必须和旧版一致（0 沙 0.9×、1 砾 1.1×、
   * 2 丘 0.7×、3 山不可通行、4 平台 1.0×），否则 applyMove 的速度表要改，
   * 那是规则变更，不在这轮范围内。 */
  const SURF_BASE = [0, 1, 2, 3, 1, 1, 1, 4];

  const DEFAULT_OPTS = {
    unitSlope: 26,        // 普通单位可跨越的相邻格高差（级）
    carrierSlope: 10,     // 运载舰（更重）的爬坡上限
    vpRadiusCells: 4.2,   // VP 控制半径（格），与 rules.js 的 VP_RADIUS 同源
    vpCount: 4,
    resCount: 4,
    wallBand: [0.05, 0.15],
    wallTarget: 0.10,
    attempts: 32,
    generator: 'geomorphic',
    forcedRes: null
  };

  /* ---------------------- 数学与噪声 ---------------------- */
  const clamp = (v, lo, hi) => (v < lo ? lo : (v > hi ? hi : v));
  const lerp = (a, b, t) => a + (b - a) * t;
  function smoothstep(a, b, x) {
    if (a === b) return x < a ? 0 : 1;
    const t = clamp((x - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
  }
  const ci = (gx, gy) => gy * GRID + gx;
  const gxOf = i => i % GRID;
  const gyOf = i => (i / GRID) | 0;
  /* 180° 点旋转：整幅图的 index 镜像（旧版 setSym 用的也是这个） */
  const rev = i => N - 1 - i;

  /* mulberry32：种子化 PRNG。所有「随机选择」（山体位置、风向、沙丘波长）
   * 都从它取，不用 Math.random，保证同 seed 同图。 */
  function makeRng(seed) {
    let a = seed >>> 0;
    return function () {
      a = (a + 0x6D2B79F5) >>> 0;
      let t = a;
      t = Math.imul(t ^ (t >>> 15), t | 1);
      t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  }
  function hash2(seed, x, y) {
    let h = (seed | 0) ^ Math.imul(x | 0, 374761393) ^ Math.imul(y | 0, 668265263);
    h = Math.imul(h ^ (h >>> 16), 2246822507);
    h = Math.imul(h ^ (h >>> 13), 3266489909);
    return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
  }
  const smoothT = t => t * t * (3 - 2 * t);
  function valueNoise(seed, x, y) {
    const xi = Math.floor(x), yi = Math.floor(y);
    const tx = smoothT(x - xi), ty = smoothT(y - yi);
    const a = hash2(seed, xi, yi), b = hash2(seed, xi + 1, yi);
    const c = hash2(seed, xi, yi + 1), d = hash2(seed, xi + 1, yi + 1);
    return (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty;
  }
  function fbm(seed, x, y) { return fbmN(seed, x, y, 6); }
  /* 可指定倍频数。6 倍频的最高频在一个格子里就走完一个周期 ≈ 白噪声，
   * 用它去调制「沙丘相位/振幅」会得到逐格抖动的地形（渲染出来是雪花点）。
   * 需要「大尺度变化」的地方一律用 3 倍频。 */
  function fbmN(seed, x, y, oct) {
    let v = 0, amp = 0.5, f = 1;
    for (let o = 0; o < (oct || 6); o++) { v += amp * valueNoise(seed, x * f, y * f); f *= 2; amp *= 0.5; }
    return v;
  }

  /* (seed, attempt) → 32 位盐值。不能简单用 seed + attempt：那样 A 的第 1 次
   * 重试会等于 B 的第 0 次，两个不同 seed 落到同一张图（旧版踩过，实测
   * 两图 4096/4096 逐格相同），多 seed 统计就失真。混入 attempt 后唯一。 */
  function seedSalt(seed, attempt) {
    let h = (seed | 0) ^ Math.imul((attempt | 0) + 1, 0x9E3779B1);
    h = Math.imul(h ^ (h >>> 15), 0x85EBCA6B);
    return (h ^ (h >>> 13)) | 0;
  }

  /* ---------------------- 1. 构造地貌 ---------------------- */
  /* 山体参数：椭圆半径（归一化）、旋转角、峰高、是否台地。
   * 归一化坐标 nx,ny ∈ [0,1]，所以 rx=0.09 ≈ 5.8 格 ≈ 230 m。 */
  function planMassifs(rng) {
    const list = [];
    const n = 2 + ((rng() * 2) | 0);            // 2~3 个 → 对称化后 4~6 个
    const near = (cx, cy, r) => list.some(m =>
      Math.hypot(cx - m.cx, cy - m.cy) < r ||
      Math.hypot(cx - (1 - m.cx), cy - (1 - m.cy)) < r);   // 同时避开对方的镜像
    for (let k = 0; k < n; k++) {
      let cx = 0.5, cy = 0.5, ok = false;
      for (let t = 0; t < 24; t++) {
        /* 山体摆在「环带」上：离中心 0.22~0.46。这不是为了好看，是盆地几何：
         * 山体围成一圈、中间留出汇水的低地，才有「四周来水汇入盐沼」的内流盆地，
         * 而且中央空出来才能当主战场。旧实现允许山体压在正中心，
         * 结果盆地被堵死，水系只剩几个格子。 */
        const ang = rng() * Math.PI * 2;
        const rad = 0.22 + rng() * 0.24;
        cx = 0.5 + Math.cos(ang) * rad;
        cy = 0.5 + Math.sin(ang) * rad;
        if (cx < 0.05 || cy < 0.05 || cx > 0.95 || cy > 0.95) continue;
        /* 别压在对称缝（反对角线）上：交叉淡化会把横跨缝线的山体削平半边 */
        if (Math.abs(cx + cy - 1) < 0.18) continue;
        if (near(cx, cy, 0.26)) continue;
        ok = true; break;
      }
      if (!ok) continue;
      list.push({
        cx, cy,
        rx: 0.065 + rng() * 0.075,
        ry: 0.045 + rng() * 0.075,
        ang: rng() * Math.PI,
        peak: 96 + rng() * 58,
        mesa: rng() < 0.38            // 台地：顶平、四周陡崖
      });
    }
    return list;
  }

  /* 单面山：一条长条形陡坎。真实沙漠盆地里它常常是「一边陡、一边缓」的
   * 掀斜地块，也是天然的分水岭与隘口所在。 */
  function planEscarpments(rng) {
    const list = [];
    if (rng() < 0.85) {
      list.push({
        ang: rng() * Math.PI,
        off: 0.18 + rng() * 0.64,
        peak: 78 + rng() * 52,
        w: 0.032 + rng() * 0.028
      });
    }
    return list;
  }

  /* 生成整幅连续场（未对称化）。返回值全部是 Float32Array，后面统一淡化。 */
  function buildFields(salt, rng) {
    const elev = new Float32Array(N);
    const rock = new Float32Array(N);   // 岩性：0 松软沉积 1 基岩
    const massifs = planMassifs(rng);
    const escs = planEscarpments(rng);
    const wind = (0.28 + rng() * 0.44) * Math.PI;   // 主风向 50°~130°

    const sMass = salt ^ 0x7f1b3c9d;
    const sEsc = salt ^ 0x2ab1d7e3;
    const sWarp = salt ^ 0x5bf03635;

    for (let gy = 0; gy < GRID; gy++) {
      for (let gx = 0; gx < GRID; gx++) {
        const i = ci(gx, gy);
        const nx = (gx + 0.5) / GRID, ny = (gy + 0.5) / GRID;

        /* 低地：缓起伏的沙质平原，向盆地中心倾斜（内流盆地，四周高中间低） */
        let e = 92 + (fbm(salt ^ 0x51ed, nx * 2.6, ny * 2.6) - 0.5) * 26;
        e += 44 * smoothstep(0.08, 0.62, Math.hypot(nx - 0.5, ny - 0.5) / 0.7071);
        let rk = 0;

        /* 山体 / 台地：椭圆 + 轮廓扭曲，边缘陡、内部缓（台地则是顶平崖陡） */
        for (let k = 0; k < massifs.length; k++) {
          const m = massifs[k];
          const ca = Math.cos(m.ang), sa = Math.sin(m.ang);
          const dx = nx - m.cx, dy = ny - m.cy;
          const u = (dx * ca + dy * sa) / m.rx;
          const v = (-dx * sa + dy * ca) / m.ry;
          let d = Math.hypot(u, v);
          /* 轮廓扭曲：真实山体的边界是锯齿状的山脊线，不是椭圆。
           * 除了低频 fbm，再加一个低阶角向分量（3~4 瓣）—— 台地/孤山在
           * 平面图上常常是「有缺口的多边形」，纯椭圆一眼就能看出是生成的。 */
          const ang = Math.atan2(v, u);
          d *= 1 + 0.5 * (fbm(sWarp, nx * 7.5 + 11, ny * 7.5 + 5) - 0.5) * 2
            + 0.13 * Math.sin(ang * (3 + (k % 2)) + m.cx * 9.7 + m.cy * 5.3);
          if (d > 1.06) continue;
          const prof = m.mesa
            ? smoothstep(1.04, 0.74, d)                       // 台地：顶平崖陡
            : Math.pow(smoothstep(1.04, 0.10, d), 1.15);      // 山体：尖顶
          const hh = 92 + m.peak * prof
            + (fbm(sMass, nx * 16, ny * 16) - 0.5) * 12 * prof;  // 山脊起伏细节
          if (hh > e) e = hh;
          const r2 = smoothstep(0.62, 0.92, prof);
          if (r2 > rk) rk = r2;
        }

        /* 单面山：到一条直线的距离场 × 高斯剖面；带一点摆动，避免笔直 */
        for (let k = 0; k < escs.length; k++) {
          const s = escs[k];
          const sd = (nx * Math.sin(s.ang) - ny * Math.cos(s.ang)) - s.off
            + (fbm(sEsc, nx * 3.4, ny * 3.4) - 0.5) * 0.10;
          const band = Math.exp(-Math.pow(sd / s.w, 2));
          const hh = 96 + s.peak * band;
          if (hh > e) e = hh;
          const r2 = smoothstep(0.42, 0.90, band);
          if (r2 > rk) rk = r2;
        }

        elev[i] = e;
        rock[i] = rk;
      }
    }
    return { elev, rock, massifs, escs, wind };
  }

  /* ---------------------- 2. 水系：河道生长 ----------------------
   * 这里**不用** DEM 的 priority-flood 填洼 + D8 汇流。原因是本图是一个
   * 封闭的内流盆地：填洼会把整个盆地抬到「溢出高程」，地形被抹平，
   * 水反而沿填出来的平面流到角落去（实测 playa 出现在地图角上）。
   *
   * 改成显式的**河道生长**：从高处的水源出发，每一步走向
   *   最低的邻居 + 弯曲噪声 + 汇入已有河道优先 + 少量上坡罚分
   * 的格子，直到进入盆地中心或并入别的河道。这条规则和「水往低处流」
   * 同构，但显式维护 visited 集合，既不会绕圈，也不会把地形改平。
   *
   * 汇流是真的汇流：流量 = 经过该格的上游水源数，主河道（多条支流汇入）
   * 切得深、支流切得浅，河谷宽度也跟着流量走。 */
  function pickOutlet(elev) {
    const cx = (GRID - 1) / 2, cy = (GRID - 1) / 2;
    let best = -1, bestE = Infinity;
    /* 只在中心 21×21 里找：设计意图是「盆地中心是盐沼」，也让 VP 的
     * 环形带都围绕它展开。确定性：并列取先扫到的（扫描顺序固定）。 */
    for (let gy = Math.round(cy - 10 * S); gy <= Math.round(cy + 10 * S); gy++) {
      for (let gx = Math.round(cx - 10 * S); gx <= Math.round(cx + 10 * S); gx++) {
        if (gx < 1 || gy < 1 || gx > GRID - 2 || gy > GRID - 2) continue;
        const i = ci(gx, gy);
        if (elev[i] < bestE) { bestE = elev[i]; best = i; }
      }
    }
    return best;
  }

  /* 水源：高处、非纯岩体、彼此至少隔 9 格（否则会挤在同一面坡上）。
   * 用「高程 + 种子抖动」排序，保证同一个 seed 每次挑到同一批。 */
  function pickSources(elev, rock, rng, count) {
    const cand = [];
    for (let i = 0; i < N; i++) {
      if (rock[i] > 0.55) continue;
      cand.push({ i, key: elev[i] + rng() * 26 });
    }
    cand.sort((a, b) => (b.key - a.key) || (a.i - b.i));
    const picked = [];
    for (const c of cand) {
      if (picked.length >= count) break;
      const gx = gxOf(c.i), gy = gyOf(c.i);
      if (picked.some(p => Math.hypot(gxOf(p) - gx, gyOf(p) - gy) < 9 * S)) continue;
      picked.push(c.i);
    }
    return picked;
  }

  const DX8 = [1, -1, 0, 0, 1, 1, -1, -1];
  const DY8 = [0, 0, 1, -1, 1, -1, 1, -1];

  /* 河道生长：返回 river 掩膜、流量（上游水源数）、以及每格的河床下切量。
   * 弯度来自 hash 噪声（不是随机数），同 seed 同河道。 */
  function growRivers(elev, rock, outlet, sources) {
    const river = new Uint8Array(N);
    const down = new Int32Array(N).fill(-1);
    const flow = new Float32Array(N);
    const reached = [];
    const ox = gxOf(outlet), oy = gyOf(outlet);
    /* 「到盐沼」按 1 格邻接算：河道不必正好踩在那一格上，
     * 进了盐沼边缘就算汇入盆地（也避免因为差一格而误判失败）。 */
    const atBasin = i => Math.abs(gxOf(i) - ox) <= S && Math.abs(gyOf(i) - oy) <= S;
    for (const src of sources) {
      let cur = src, guard = 0, ok = false;
      const walked = new Set();
      while (guard++ < 420 * S) {
        if (atBasin(cur)) { ok = true; break; }
        if (!walked.has(cur)) {
          walked.add(cur);
          river[cur] = 1;
          flow[cur] += 1;
        }
        const gx = gxOf(cur), gy = gyOf(cur);
        let next = -1, bestScore = Infinity;
        for (let k = 0; k < 8; k++) {
          const nx = gx + DX8[k], ny = gy + DY8[k];
          if (nx < 0 || ny < 0 || nx >= GRID || ny >= GRID) continue;
          const j = ci(nx, ny);
          if (walked.has(j)) continue;
          /* 打分口径统一放大 S 倍：v0.10 的权重（高程项 1×、上坡罚 3.0、
           * 汇入奖励 5、弯曲噪声 2.4）都是按「64 格、每格 40 m」标定的。
           * 网格细 4 倍后每格高差只剩 1/4，如果只放大高程项、不改其他项，
           * 罚分和噪声就会压过地形，河道变成随机漫步（实测主干断裂、进不了盐沼）。
           * 整体 ×S 之后各项比例与旧版完全一致，河道形状只是被采样得更细。 */
          const ej = elev[j] * S, ec = elev[cur] * S;
          let sc = ej;
          if (ej > ec) sc += 3.0 + (ej - ec) * 0.8;
          if (river[j]) sc -= 5.0;                    // 优先并入已有河道（支流汇合）
          if (rock[j] > 0.6) sc += 14;                // 基岩段：能切，但很贵
          sc += 2.4 * (hash2(0x5eed5eed, nx * 3, ny * 3) - 0.5) * 2;  // 弯曲
          if (nx <= 0 || ny <= 0 || nx >= GRID - 1 || ny >= GRID - 1) sc += 60;
          if (sc < bestScore) { bestScore = sc; next = j; }
        }
        if (next < 0) break;
        down[cur] = next;
        cur = next;
        /* 并入已有河道：沿它的下游把本支流的水量加进去，然后结束本条。
         * 「汇流」本身就是成功 —— 只要合并后的干流最终到得了盐沼。 */
        if (river[cur]) {
          let c2 = cur, g2 = 0;
          while (c2 >= 0 && g2++ < 420 * S) {
            if (!walked.has(c2)) { walked.add(c2); flow[c2] += 1; river[c2] = 1; }
            c2 = down[c2];
          }
          ok = chainReaches(cur, down, outlet);
          break;
        }
      }
      reached.push(ok);
    }
    return { river, flow, down, reached };
  }

  /* 沿 down 指针判断一条已建成的河道是否最终流入盐沼 */
  function chainReaches(start, down, outlet) {
    const ox = gxOf(outlet), oy = gyOf(outlet);
    let c = start, g = 0;
    while (c >= 0 && g++ < 520 * S) {
      if (Math.abs(gxOf(c) - ox) <= 1 && Math.abs(gyOf(c) - oy) <= 1) return true;
      c = down[c];
    }
    return false;
  }

  function carveRiver(elev, rock, flow) {
    const carve = new Float32Array(N);
    for (let i = 0; i < N; i++) {
      if (flow[i] <= 0) continue;
      /* 深度随流量对数增长：单支流 ~3 级（1 m）的浅沟，主河道 ~17 级（5 m）。
       * 基岩段只切一半（峡谷比冲积河槽浅）。 */
      const depth = Math.min(CARVE_MAX, 5.0 * Math.log2(1 + flow[i]));
      carve[i] = depth * (1 - 0.5 * rock[i]);
    }
    /* 河漫滩：河床两侧 1 格跟着下切一半，河谷才有宽度而不是一条缝 */
    for (let i = 0; i < N; i++) {
      if (carve[i] <= 0) continue;
      const gx = gxOf(i), gy = gyOf(i);
      for (let oy = -S; oy <= S; oy++) for (let ox = -S; ox <= S; ox++) {
        const nx = gx + ox, ny = gy + oy;
        if ((!ox && !oy) || nx < 0 || ny < 0 || nx >= GRID || ny >= GRID) continue;
        const j = ci(nx, ny);
        if (carve[j] >= carve[i] * 0.5) continue;
        carve[j] = carve[i] * 0.5;
      }
    }
    for (let i = 0; i < N; i++) elev[i] -= carve[i];
    return carve;
  }

  /* 盐沼：河道最终汇到的那块最低地。真实 playa 就是内流盆地的汇水终点：
   * 水带着溶解盐进来、出不去、蒸发后留下盐壳。压平成一块浅色硬地。 */
  function makePlaya(elev, outlet) {
    const playa = new Float32Array(N);
    const sx = gxOf(outlet), sy = gyOf(outlet);
    let floor = 1e9, sum = 0, cnt = 0;
    for (let gy = Math.max(0, sy - 6); gy <= Math.min(GRID - 1, sy + 6); gy++) {
      for (let gx = Math.max(0, sx - 6); gx <= Math.min(GRID - 1, sx + 6); gx++) {
        const i = ci(gx, gy);
        if (elev[i] < floor) floor = elev[i];
        sum += elev[i]; cnt++;
      }
    }
    const flat = lerp(floor, sum / Math.max(1, cnt), 0.35);
    for (let gy = 0; gy < GRID; gy++) {
      for (let gx = 0; gx < GRID; gx++) {
        const d = Math.hypot(gx - sx, gy - sy);
        const m = smoothstep(7.5 * S, 3.0 * S, d);
        if (m <= 0) continue;
        const i = ci(gx, gy);
        playa[i] = m;
        elev[i] = lerp(elev[i], flat + (hash2(0x9a1f, gx, gy) - 0.5) * 3, m);
      }
    }
    return { playa, sink: { gx: sx, gy: sy, floor } };
  }

  /* 冲积扇：河道出山口、坡度骤降处堆出的扇形砾质地。
   * 判定用「河道 + 3 格内出现过岩性高的格子」—— 这正是真实冲积扇的位置关系
   * （扇顶在峡谷口，扇缘伸进平原），不是随便在平原上刷一块硬地。 */
  function applyFans(fields, river) {
    const { rock } = fields;
    const fan = new Float32Array(N);
    for (let i = 0; i < N; i++) {
      if (!river[i] || rock[i] > 0.04) continue;
      const gx = gxOf(i), gy = gyOf(i);
      /* 3 格内必须真有高岩性：那才是「山口」 */
      let peak = 0;
      for (let y = gy - 3 * S; y <= gy + 3 * S; y++) {
        for (let x = gx - 3 * S; x <= gx + 3 * S; x++) {
          if (x < 0 || y < 0 || x >= GRID || y >= GRID) continue;
          const r = rock[ci(x, y)];
          if (r > peak) peak = r;
        }
      }
      if (peak < 0.55) continue;
      /* 扇体只铺 2 格半径，向外衰减：冲积扇是局部地貌，不是沿河整条硬地 */
      for (let y = gy - S; y <= gy + S; y++) {
        for (let x = gx - S; x <= gx + S; x++) {
          if (x < 0 || y < 0 || x >= GRID || y >= GRID) continue;
          const d = Math.hypot(x - gx, y - gy);
          const v = smoothstep(1.6 * S, 0.2 * S, d) * smoothstep(0.55, 0.85, peak);
          const j = ci(x, y);
          if (v > fan[j]) fan[j] = v;
        }
      }
    }
    fields.fan = fan;
  }

  /* ---------------------- 地貌叠加 ---------------------- */
  const CARVE_MAX = 34;        // 单格最大下切（级）≈ 10 m
  const SLOPE_CAP = 22 * S;    // 非岩质格之间的最大允许高差（保证可通行），按格密度缩放

  /* ---------------------- 风成地貌 ---------------------- */
  function applyAeolian(fields, rng) {
    const { elev, rock, playa, accLog, wind } = fields;
    /* 沙丘波长与振幅：主沙丘（几百米间距）+ 次级小沙丘 */
    const lam1 = (9 + rng() * 5) * S;    // 格
    const lam2 = (3.6 + rng() * 2.2) * S;
    const A1 = 24 + rng() * 22;          // 级（1 级 = 0.30 m）
    const A2 = 5 + rng() * 9;
    const sDune = fields.salt ^ 0x44c1a7;
    const sSec = fields.salt ^ 0x1f7d3b;
    const sErg = fields.salt ^ 0x61b7c5;
    const dune = new Float32Array(N);
    const relief = new Float32Array(N);   // 纯沙丘起伏（级），检验走向用
    const cw = Math.cos(wind), sw = Math.sin(wind);

    for (let gy = 0; gy < GRID; gy++) {
      for (let gx = 0; gx < GRID; gx++) {
        const i = ci(gx, gy);
        /* 沙源 + 风积区（erg）：岩区不积沙、盐沼不积沙、河床上不积沙
         * （风会把沙吹走，或已被水带走）。erg 掩膜让沙丘成片而不是满图铺：
         * 真实沙海里沙丘只占一部分，其余是丘间沙席与砾漠。 */
        const erg = smoothstep(0.42, 0.62, fbm(sErg, gx / GRID * 1.7 + 2.3, gy / GRID * 1.7 + 6.1));
        const sand = (1 - smoothstep(0.05, 0.55, rock[i]))
          * (1 - smoothstep(0.05, 0.55, playa[i]))
          * (1 - smoothstep(0.10, 0.45, accLog[i]))
          * (0.30 + 0.70 * erg);
        if (sand <= 0.01) continue;

        /* 迎风坡缓、背风坡陡：用不对称锯齿剖面（0.68 处为脊线） */
        const proj = (gx * cw + gy * sw);
        const ph = (proj / lam1) * Math.PI * 2
          + 2.0 * (fbmN(sDune, gx / GRID * 4.2, gy / GRID * 4.2, 3) - 0.5) * 2;
        let t = ph / (Math.PI * 2); t -= Math.floor(t);
        const shape = t < 0.68 ? t / 0.68 : (1 - t) / 0.32;

        const ph2 = (proj / lam2) * Math.PI * 2
          + 2.4 * (fbmN(sSec, gx / GRID * 9.0, gy / GRID * 9.0, 3) - 0.5) * 2;
        let u = ph2 / (Math.PI * 2); u -= Math.floor(u);
        const shape2 = u < 0.6 ? u / 0.6 : (1 - u) / 0.4;

        /* 振幅本身要有空间变化：真实沙丘场里丘高成片变化，不是等幅正弦 */
        const ampMod = 0.55 + 0.45 * fbmN(sDune ^ 0x9e37, gx / GRID * 3.1 + 7, gy / GRID * 3.1 + 3, 3);
        const dh = (A1 * shape + A2 * shape2) * sand * ampMod;
        elev[i] += dh;
        relief[i] = dh;
        dune[i] = clamp(dh / (A1 * 0.8), 0, 1);
      }
    }
    fields.dune = dune;
    fields.duneRelief = relief;
    fields.windRad = wind;
    fields.duneParams = { lam1, lam2, A1, A2 };
  }


  /* ---------------------- 对称化 ---------------------- */
  /* 交叉淡化：f'(p) = (1-w)·f(p) + w·f(Rp)，w 由到反对角线的距离决定。
   * 因为 w(Rp) = 1-w(p)，这个式子对 R 严格不变 —— 是恒等式，不是「大致相等」。
   * 只算一半（i < N/2）再镜像写出，避免浮点误差在同一点算出两个值。 */
  const SEAM_B = 2.5 * S;  // 淡化半宽（格），按格密度缩放
  function symmetrize(field) {
    const out = new Float32Array(N);
    for (let i = 0; i < HALF; i++) {
      const gx = gxOf(i), gy = gyOf(i);
      const d = (gx + gy - (GRID - 1)) / Math.SQRT2;
      const w = 0.5 + 0.5 * smoothstep(-SEAM_B, SEAM_B, d);
      const j = rev(i);
      const v = (1 - w) * field[i] + w * field[j];
      out[i] = v; out[j] = v;
    }
    return out;
  }

  /* ---------------------- 量化与坡度约束 ---------------------- */
  function quantize(fields) {
    const { elev, rock } = fields;
    const e = new Uint8Array(N);
    for (let i = 0; i < N; i++) e[i] = clamp(Math.round(elev[i]), 1, 255);
    /* 斜坡滑塌约束：非基岩格之间的高差不能超过 SLOPE_CAP，
     * 否则会出现「看着像沙丘、实际是一道隐形墙」的地形。
     * 用 src→dst 的 Jacobi 迭代保持镜像对称（同一快照、同一规则）。 */
    const DX = [1, -1, 0, 0, 1, 1, -1, -1], DY = [0, 0, 1, -1, 1, -1, 1, -1];
    for (let pass = 0; pass < 3; pass++) {
      const src = e.slice();
      for (let i = 0; i < N; i++) {
        if (rock[i] >= 0.28) continue;          // 基岩不受滑塌约束（那就是崖）
        const gx = gxOf(i), gy = gyOf(i);
        let excess = 0;
        for (let k = 0; k < 8; k++) {
          const nx = gx + DX[k], ny = gy + DY[k];
          if (nx < 0 || ny < 0 || nx >= GRID || ny >= GRID) continue;
          const j = ci(nx, ny);
          if (rock[j] >= 0.28) continue;
          const d = src[i] - src[j];
          if (d > SLOPE_CAP && d - SLOPE_CAP > excess) excess = d - SLOPE_CAP;
        }
        if (excess > 0) e[i] = clamp(src[i] - Math.ceil(excess * 0.7), 1, 255);
      }
      /* 保持严格镜像（Jacobi 已对称，这里只消掉可能的取整抖动） */
      for (let i = 0; i < HALF; i++) e[rev(i)] = e[i];
    }
    fields.elev8 = e;
    return e;
  }

  function slopeField(e) {
    const s = new Uint8Array(N);
    const DX = [1, -1, 0, 0, 1, 1, -1, -1], DY = [0, 0, 1, -1, 1, -1, 1, -1];
    for (let i = 0; i < N; i++) {
      const gx = gxOf(i), gy = gyOf(i);
      let m = 0;
      for (let k = 0; k < 8; k++) {
        const nx = gx + DX[k], ny = gy + DY[k];
        if (nx < 0 || ny < 0 || nx >= GRID || ny >= GRID) continue;
        const d = Math.abs(e[i] - e[ci(nx, ny)]);
        if (d > m) m = d;
      }
      s[i] = m;
    }
    return s;
  }

  /* ---------------------- 岩壁阈值搜索 ---------------------- */
  /* 岩壁（base=3，不可通行、按 MOUNTAIN_H 挡视线与炮弹）不能随手一刀切：
   * 切太低整张图变成迷宫，切太高规则里的地形就没意义了。这里用**二分阈值**
   * 让岩壁占比落在目标带上 —— 于是「坡多陡算崖」是由目标比例反解出来的，
   * 而不是凭感觉写一个 34。候选格限定在岩性够高的地方，沙丘的陡坡不会
   * 被误判成 55 m 的实体山（那会把沙丘变成硬墙，破坏「能过但难走」的设定）。 */
  function classifyWall(e, rock, slope, target) {
    const cand = [];
    for (let i = 0; i < N; i++) {
      if (rock[i] >= 0.30 && e[i] >= 126) cand.push(i);
    }
    if (!cand.length) return new Uint8Array(N);
    let lo = 1, hi = 60 * S, best = 60 * S, bestGap = Infinity;
    for (let it = 0; it < 12; it++) {
      const mid = (lo + hi) >> 1;
      let n = 0;
      for (let k = 0; k < cand.length; k++) if (slope[cand[k]] >= mid) n++;
      const gap = Math.abs(n - target);
      if (gap < bestGap) { bestGap = gap; best = mid; }
      if (n > target) lo = mid + 1; else hi = mid - 1;
      if (lo > hi) break;
    }
    const wall = new Uint8Array(N);
    for (let k = 0; k < cand.length; k++) {
      const i = cand[k];
      if (slope[i] >= best) wall[i] = 1;
    }
    /* 去斑点：孤立的一两格山尖在视觉上是「噪点」，在地貌上也不成立
     * （真实山体是连续的岩体）。删除 8 邻域里山格 < 2 的孤立格。 */
    const DX = [1, -1, 0, 0, 1, 1, -1, -1], DY = [0, 0, 1, -1, 1, -1, 1, -1];
    for (let pass = 0; pass < 2; pass++) {
      const snap = wall.slice();
      for (let i = 0; i < N; i++) {
        if (!snap[i]) continue;
        const gx = gxOf(i), gy = gyOf(i);
        let nb = 0;
        for (let k = 0; k < 8; k++) {
          const nx = gx + DX[k], ny = gy + DY[k];
          if (nx < 0 || ny < 0 || nx >= GRID || ny >= GRID) continue;
          if (snap[ci(nx, ny)]) nb++;
        }
        if (nb < 2) wall[i] = 0;
      }
    }
    return wall;
  }

  /* ---------------------- 地表分类 ---------------------- */
  function classify(fields, wall, pad) {
    const { elev8: e, rock, dune, accLog, playa, fan, flow } = fields;
    const regCut = fields.regCut == null ? 0.04 : fields.regCut;
    const slope = slopeField(e);
    const base = new Uint8Array(N), surf = new Uint8Array(N);

    /* 先分出「已经确定不是沙地」的格子，剩下的才是沙丘候选。
     * 沙丘的判定阈值用**候选格的振幅分位数**，而不是一个固定数值：
     * 固定阈值在不同 seed 上会给出 0%~30% 的覆盖率（振幅本身是随机的），
     * 有的图满屏沙丘、有的图一个沙丘都没有。取 70 分位相当于「这张图上
     * 最高的 30% 沙地算丘」，地形语言里是一致的，又保留了每图不同的丘高。 */
    for (let i = 0; i < N; i++) {
      let s;
      if (wall[i]) s = SURF.ROCK;
      else if (pad[i]) s = SURF.PAD;
      else if (playa[i] > 0.45) s = SURF.PLAYA;
      /* 干河谷按**河道本身**判（流量 ≥1，即至少有水源经过），不按对数流量的
       * 相对阈值：后者会随「本图最大流量」漂，同一条河在不同图上时有时无。
       * 河漫滩（0.45×）留给沙席/砾漠，视觉上靠下切与阴影表现谷地。 */
      else if (flow[i] >= 1.0 && slope[i] < 22 * S) s = SURF.WADI;
      else if (fan[i] > 0.45) s = SURF.FAN;
      else if (rock[i] > regCut) s = SURF.REG;       // 山前砾漠 / 剥蚀面
      else s = SURF.SAND;
      surf[i] = s;
    }
    const sandCells = [];
    for (let i = 0; i < N; i++) if (surf[i] === SURF.SAND) sandCells.push(dune[i]);
    sandCells.sort((a, b) => a - b);
    /* 下限 0.30：振幅太小的图上不存在真正的丘脊，不要硬凑出 30% 的「沙丘」 */
    const p70 = sandCells.length ? sandCells[Math.floor(sandCells.length * 0.70)] : 1;
    /* 下限可由生成器调整：MC 风格的低幅沙丘整体振幅更小，
     * 用同一条 0.30 会把大半丘脊判成沙席。 */
    const duneCut = Math.max(fields.duneFloor == null ? 0.30 : fields.duneFloor, p70);
    for (let i = 0; i < N; i++) {
      if (surf[i] === SURF.SAND && dune[i] > duneCut) surf[i] = SURF.DUNE;
      base[i] = SURF_BASE[surf[i]];
    }
    fields.slope = slope;
    fields.duneCut = duneCut;
    return { base, surf, slope };
  }

  /* ---------------------- 通行与连通 ---------------------- */
  function passable(base, i) { return base[i] !== 3; }
  function passableFrom(base, e, a, b, limit) {
    if (!passable(base, b)) return false;
    /* 平台按工程化地面处理：平台与平台之间允许更大高差（旧版规则，
     * 否则出生点平台边缘会把母舰困住） */
    const eff = (base[a] === 4 || base[b] === 4) ? Math.max(limit, 55 * S) : limit;
    return Math.abs(e[b] - e[a]) <= eff;
  }
  function reachSet(base, e, from, limit) {
    const seen = new Uint8Array(N), q = new Int32Array(N);
    const DX = [1, -1, 0, 0, 1, 1, -1, -1], DY = [0, 0, 1, -1, 1, -1, 1, -1];
    let h = 0, t = 0;
    q[t++] = from; seen[from] = 1;
    while (h < t) {
      const cur = q[h++], cx = gxOf(cur), cy = gyOf(cur);
      for (let k = 0; k < 8; k++) {
        const nx = cx + DX[k], ny = cy + DY[k];
        if (nx < 0 || ny < 0 || nx >= GRID || ny >= GRID) continue;
        const ni = ci(nx, ny);
        if (seen[ni] || !passableFrom(base, e, cur, ni, limit)) continue;
        seen[ni] = 1; q[t++] = ni;
      }
    }
    return seen;
  }

  /* 连通性判定沿用旧语义：两个出生点互通，且所有 VP / 资源点都可达。
   * （旧版只查 4 邻域，这里改成 8 邻域 —— 对角线也能走，更接近实际移动。） */
  function connected(map, opts) {
    const o = Object.assign({}, DEFAULT_OPTS, opts || {});
    const sp = map.spawns;
    const seen = reachSet(map.base, map.elev, ci(sp[0].gx, sp[0].gy), o.unitSlope);
    if (!seen[ci(sp[1].gx, sp[1].gy)]) return false;
    for (const c of map.vpCells) if (!seen[ci(c[0], c[1])]) return false;
    for (const c of map.resCells) if (!seen[ci(c[0], c[1])]) return false;
    return true;
  }
  /* 只问「两个出生点还通不通」：用于兜底通道的回归测试。
   * 兜底通道的职责是恢复主通路，不负责把每个目标点都接回去。 */
  function spawnConnected(map, opts) {
    const o = Object.assign({}, DEFAULT_OPTS, opts || {});
    const sp = map.spawns;
    const seen = reachSet(map.base, map.elev, ci(sp[0].gx, sp[0].gy), o.unitSlope);
    return !!seen[ci(sp[1].gx, sp[1].gy)];
  }

  /* 地表类型 ↔ 协议字符串。用 '0'..'7' 一字节一格：4096 格的 surf
   * 铺成数组要 16 KB，编码成字符串只有 4 KB，header 仍留在 45 KB 预算内。
   * 渲染层与测试都从这一对函数走，不允许各写一套编码。 */
  function surfString(surf) {
    let out = '';
    for (let i = 0; i < N; i++) out += String.fromCharCode(48 + (surf[i] | 0));
    return out;
  }
  function surfFromString(str) {
    const a = new Uint8Array(N);
    for (let i = 0; i < N; i++) a[i] = (str.charCodeAt(i) || 48) - 48;
    return a;
  }

  /* ---------------------- 地图层的协议编码 ----------------------
   * 256² = 65536 格，直接发 JSON 数组会让 hello 行涨到 450 KB。编码方案：
   *   · base / surf：值域只有 0~4 / 0~7，地块又成片 —— RLE（value,count-1）
   *     再 base64，实测各 ~10 KB；
   *   · elev：0~255 且逐格都有起伏，RLE 几乎压不动（86 KB），
   *     所以直接 1 字节/格 + base64（87 KB）。
   * 合计 ~106 KB，仍在「一行 JSON 能装下」的范围内，且**同步可解**：
   * 浏览器用 atob、Node 用 Buffer，都不依赖任何第三方库或异步 API
   * （zlib/CompressionStream 能压到 40 KB，但一个要 require、一个要 await，
   * 把 Bot 的 init 变成异步，代价大于省下的 60 KB）。
   *
   * 对外约定：wire 上是字符串；`decodeMapLayers` 还原成 Uint8Array。
   * 驱动会把解码后的地图交给 Bot（见 referee.js），外部进程 Bot 也可以
   * 直接调 R.decodeMapLayers(header.map)。 */
  /* base64 自己实现：浏览器沙箱（web_smoke 的 DOM 桩）里没有 btoa/atob，
   * Node 里有 Buffer —— 与其按环境分支，不如用一份纯 JS 实现，两边都能跑，
   * 也不会因为 Node 的 Buffer 与浏览器 atob 对换行/填充的处理差异埋雷。 */
  const B64C = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  function bytesToB64(bytes) {
    let out = '';
    for (let i = 0; i < bytes.length; i += 3) {
      const b0 = bytes[i], b1 = i + 1 < bytes.length ? bytes[i + 1] : 0, b2 = i + 2 < bytes.length ? bytes[i + 2] : 0;
      out += B64C[b0 >> 2] + B64C[((b0 & 3) << 4) | (b1 >> 4)]
        + (i + 1 < bytes.length ? B64C[((b1 & 15) << 2) | (b2 >> 6)] : '=')
        + (i + 2 < bytes.length ? B64C[b2 & 63] : '=');
    }
    return out;
  }
  /* 用普通数组而不是 Int16Array：DOM 桩沙箱只注入了少数几个 TypedArray，
   * 解码器不该因为「环境少注入一个构造器」就崩掉。 */
  const B64I = (() => {
    const t = new Array(128).fill(-1);
    for (let i = 0; i < B64C.length; i++) t[B64C.charCodeAt(i)] = i;
    return t;
  })();
  function b64ToBytes(str) {
    const clean = String(str).replace(/[^A-Za-z0-9+/]/g, '');
    const n = Math.floor(clean.length * 3 / 4);
    const out = new Uint8Array(n);
    let p = 0;
    for (let i = 0; i < clean.length; i += 4) {
      const c0 = B64I[clean.charCodeAt(i)] || 0, c1 = B64I[clean.charCodeAt(i + 1)] || 0;
      const c2 = B64I[clean.charCodeAt(i + 2)] || 0, c3 = B64I[clean.charCodeAt(i + 3)] || 0;
      if (p < n) out[p++] = (c0 << 2) | (c1 >> 4);
      if (p < n) out[p++] = ((c1 & 15) << 4) | (c2 >> 2);
      if (p < n) out[p++] = ((c2 & 3) << 6) | c3;
    }
    return out;
  }

  /* RLE：成对写 (value, count-1)，count 上限 256 */
  function rleEncode(arr) {
    const out = [];
    for (let i = 0; i < arr.length;) {
      const v = arr[i] | 0;
      let n = 1;
      while (i + n < arr.length && arr[i + n] === arr[i] && n < 256) n++;
      out.push(v, n - 1);
      i += n;
    }
    return Uint8Array.from(out);
  }
  function rleDecode(bytes, n) {
    const out = new Uint8Array(n);
    let p = 0;
    for (let i = 0; i + 1 < bytes.length && p < n; i += 2) {
      const v = bytes[i], c = bytes[i + 1] + 1;
      for (let k = 0; k < c && p < n; k++) out[p++] = v;
    }
    return out;
  }
  function encodeMapLayers(map) {
    return {
      w: GRID, h: GRID, enc: 'rle+b64/1',
      base: bytesToB64(rleEncode(map.base)),
      surf: bytesToB64(rleEncode(map.surf)),
      elev: bytesToB64(map.elev)
    };
  }
  function decodeMapLayers(enc) {
    if (!enc) return null;
    if (enc.enc !== 'rle+b64/1') throw new Error('未知的地图层编码：' + enc.enc);
    const n = enc.w * enc.h;
    return {
      base: rleDecode(b64ToBytes(enc.base), n),
      surf: rleDecode(b64ToBytes(enc.surf), n),
      elev: b64ToBytes(enc.elev).slice(0, n)
    };
  }

  function wallRatio(map) {
    let n = 0;
    for (let i = 0; i < N; i++) if (map.base[i] === 3) n++;
    return n / N;
  }

  /* 兜底通道：32 次重试都没得到「连通 + 占比达标」的图时才会走到。
   * 沿两个出生点之间的折线开挖，clear 掉的格子同时设为对称。 */
  function carveCorridor(map, width) {
    const sp = map.spawns;
    const a = [sp[0].gx, sp[0].gy], b = [sp[1].gx, sp[1].gy];
    const steps = Math.max(Math.abs(b[0] - a[0]), Math.abs(b[1] - a[1])) * 2;
    for (let k = 0; k <= steps; k++) {
      const gx = Math.round(a[0] + (b[0] - a[0]) * k / steps);
      const gy = Math.round(a[1] + (b[1] - a[1]) * k / steps);
      for (let oy = -width; oy <= width; oy++) {
        for (let ox = -width; ox <= width; ox++) {
          const x = gx + ox, y = gy + oy;
          if (x < 1 || y < 1 || x >= GRID - 1 || y >= GRID - 1) continue;
          const i = ci(x, y), j = rev(i);
          map.base[i] = 0; map.elev[i] = 112; map.surf[i] = SURF.WADI;
          map.base[j] = 0; map.elev[j] = 112; map.surf[j] = SURF.WADI;
        }
      }
    }
  }

  /* ---------------------- 出生点 ---------------------- */
  /* 不在固定坐标上放出生点：真实地形里 (10,10) 可能是崖壁。做法是在
   * 左下象限找一块「开阔、平整、离岩体够远」的地，再把镜像点作为对手出生点。 */
  function pickSpawn(map, opts) {
    const { base, elev, surf } = map;
    const slope = slopeField(elev);
    let best = -1, bestScore = -Infinity;
    /* 搜索区（左下角 18×18 格）与所有半径都按 S 缩放：
     * 64 格图上「6~24 格」是四分之一块地，256 格图上必须同步放大，
     * 否则出生点会被挤在角落里的一小块。 */
    const R0 = 6 * S, R1 = 24 * S;
    for (let gy = R0; gy <= R1; gy++) {
      for (let gx = R0; gx <= R1; gx++) {
        const i = ci(gx, gy);
        if (base[i] === 3) continue;
        /* 开阔度：4 格半径内能站人的比例 */
        let open = 0, tot = 0;
        for (let y = gy - 4 * S; y <= gy + 4 * S; y++) {
          for (let x = gx - 4 * S; x <= gx + 4 * S; x++) {
            if (x < 0 || y < 0 || x >= GRID || y >= GRID) continue;
            tot++;
            const j = ci(x, y);
            if (base[j] !== 3 && slope[j] <= 18 * S) open++;
          }
        }
        open = open / tot;
        /* 平整度：2 格内最大高差（起始部队要能展开） */
        let flat = 0;
        for (let y = gy - 2 * S; y <= gy + 2 * S; y++) {
          for (let x = gx - 2 * S; x <= gx + 2 * S; x++) {
            if (x < 0 || y < 0 || x >= GRID || y >= GRID) continue;
            flat = Math.max(flat, Math.abs(elev[ci(x, y)] - elev[i]));
          }
        }
        /* 离岩壁远一点：出生就被山围住的对局没法打 */
        let rockD = 99;
        for (let y = gy - 5 * S; y <= gy + 5 * S; y++) {
          for (let x = gx - 5 * S; x <= gx + 5 * S; x++) {
            if (x < 0 || y < 0 || x >= GRID || y >= GRID) continue;
            if (base[ci(x, y)] === 3) rockD = Math.min(rockD, Math.hypot(x - gx, y - gy));
          }
        }
        let sc = 0.42 * open + 0.30 * smoothstep(24 * S, 4 * S, flat) + 0.18 * smoothstep(S, 6 * S, rockD);
        sc += 0.10 * (1 - Math.abs((gx + gy) / GRID - 0.30) / 0.30);   // 偏好靠角落
        if (surf[i] === SURF.PLAYA) sc -= 0.5;                          // 别把基地放盐沼里
        if (sc > bestScore) { bestScore = sc; best = i; }
      }
    }
    if (best < 0) best = ci(10 * S, 10 * S);
    return { gx: gxOf(best), gy: gyOf(best) };
  }

  /* 工程化平台：出生点周围压出一块圆形硬地（base=4，速度 1.0×）。
   * 与旧版 5×5 方块的区别是：半径 3 的圆 + 边缘只做轻微平滑，
   * 不会在地形上留下一个突兀的方形补丁。 */
  function makePad(map, cx, cy, r) {
    const { elev, base, surf } = map;
    let sum = 0, cnt = 0;
    for (let y = cy - r; y <= cy + r; y++) {
      for (let x = cx - r; x <= cx + r; x++) {
        if (x < 0 || y < 0 || x >= GRID || y >= GRID) continue;
        if (Math.hypot(x - cx, y - cy) > r + 0.25 * S) continue;
        sum += elev[ci(x, y)]; cnt++;
      }
    }
    const mid = Math.round(sum / Math.max(1, cnt));
    for (let y = cy - r; y <= cy + r; y++) {
      for (let x = cx - r; x <= cx + r; x++) {
        if (x < 0 || y < 0 || x >= GRID || y >= GRID) continue;
        const d = Math.hypot(x - cx, y - cy);
        if (d > r + 0.25) continue;
        const i = ci(x, y), j = rev(i);
        const e = Math.round(lerp(elev[i], mid, 0.75));
        elev[i] = e; elev[j] = e;
        base[i] = 4; base[j] = 4;
        surf[i] = SURF.PAD; surf[j] = SURF.PAD;
      }
    }
  }

  /* ---------------------- VP / 资源点搜索 ---------------------- */
  /* 评分用的是**地形质量**，不是「离中心多远好看」：
   *   open    周边能站人的比例（控制点周围站不下兵就是废点）
   *   flat    局部平整度（能架炮、能停母舰）
   *   road    天然通道（近干河谷/汇流处 = 真实的路口与隘口）
   *   cover   离岩壁的距离（有掩体但不被埋在山里）
   *   spawn   到出生点的距离带（太近没争夺，太远赶不到）
   * 环形带与轴向夹角作为**硬约束**在外面卡，避免「分数最高但四个点挤在一起」。 */
  /* ---------------------- 目标点打分的预计算 ----------------------
   * 打分要四个量：开阔度（9×9 可站比例）、路网邻近（9×9 河道路网比例）、
   * 平整度（5×5 最大高差）、离岩壁距离。逐个候选格直接开窗扫的话，
   * 256² 下是 6.5 万格 × 上千次读取 ≈ 1.3 亿次 —— 生成一张图从 20 ms 变 600 ms。
   * 改成「一次性预计算」：
   *   · 开阔度 / 路网  → 积分图，O(1) 查询；
   *   · 平整度          → 可分离滑窗 min/max（两趟 1D），O(N)；
   *   · 离岩壁距离      → 一次多源 BFS 距离变换，O(N)。
   * 结果与逐格扫窗口完全等价，只是不再重复读同一批格子。 */
  function integral(mask) {
    const W = GRID + 1, sat = new Int32Array(W * W);
    for (let y = 0; y < GRID; y++) {
      let row = 0;
      for (let x = 0; x < GRID; x++) {
        row += mask[y * GRID + x];
        sat[(y + 1) * W + (x + 1)] = sat[y * W + (x + 1)] + row;
      }
    }
    return sat;
  }
  function windowSum(sat, x0, y0, x1, y1) {
    const W = GRID + 1;
    return sat[(y1 + 1) * W + (x1 + 1)] - sat[y0 * W + (x1 + 1)] - sat[(y1 + 1) * W + x0] + sat[y0 * W + x0];
  }
  /* 方窗滑动的最大/最小值：先按行做，再按列做 */
  function slide1D(src, out, r, wantMax) {
    const W = GRID;
    for (let y = 0; y < W; y++) {
      for (let x = 0; x < W; x++) {
        const a = Math.max(0, x - r), b = Math.min(W - 1, x + r);
        let m = src[y * W + a];
        for (let k = a + 1; k <= b; k++) {
          const v = src[y * W + k];
          m = wantMax ? (v > m ? v : m) : (v < m ? v : m);
        }
        out[y * W + x] = m;
      }
    }
  }
  function slide2DMinMax(elev, r) {
    const W = GRID;
    const tmpMax = new Float32Array(N), tmpMin = new Float32Array(N);
    const rowMax = new Float32Array(N), rowMin = new Float32Array(N);
    slide1D(elev, rowMax, r, true);
    slide1D(elev, rowMin, r, false);
    /* 列方向：把行结果转置着滑（直接按列取值即可） */
    for (let x = 0; x < W; x++) {
      for (let y = 0; y < W; y++) {
        const a = Math.max(0, y - r), b = Math.min(W - 1, y + r);
        let mx = rowMax[a * W + x], mn = rowMin[a * W + x];
        for (let k = a + 1; k <= b; k++) {
          const vMax = rowMax[k * W + x], vMin = rowMin[k * W + x];
          if (vMax > mx) mx = vMax;
          if (vMin < mn) mn = vMin;
        }
        tmpMax[y * W + x] = mx; tmpMin[y * W + x] = mn;
      }
    }
    const flat = new Float32Array(N);
    for (let i = 0; i < N; i++) {
      flat[i] = Math.max(tmpMax[i] - elev[i], elev[i] - tmpMin[i]);
    }
    return flat;
  }
  /* 多源 BFS：到最近岩壁的距离（格） */
  function rockDistance(base) {
    const dist = new Float32Array(N).fill(1e9);
    const q = new Int32Array(N);
    let h = 0, t = 0;
    for (let i = 0; i < N; i++) if (base[i] === 3) { dist[i] = 0; q[t++] = i; }
    while (h < t) {
      const cur = q[h++], cx = gxOf(cur), cy = gyOf(cur);
      for (let k = 0; k < 8; k++) {
        const nx = cx + DX8[k], ny = cy + DY8[k];
        if (nx < 0 || ny < 0 || nx >= GRID || ny >= GRID) continue;
        const j = ci(nx, ny);
        const nd = dist[cur] + 1;
        if (nd < dist[j]) { dist[j] = nd; q[t++] = j; }
      }
    }
    return dist;
  }
  function buildAux(map) {
    if (map.aux) return map.aux;
    const pass = new Uint8Array(N), road = new Uint8Array(N);
    for (let i = 0; i < N; i++) {
      pass[i] = (map.base[i] !== 3 && map.slope[i] <= 20 * S) ? 1 : 0;
      road[i] = (map.accLog && map.accLog[i] > 0.42) ? 1 : 0;
    }
    map.aux = {
      satPass: integral(pass), satRoad: integral(road),
      rad: 4 * S, flat: slide2DMinMax(map.elev, 2 * S), rockDist: rockDistance(map.base)
    };
    return map.aux;
  }

  function objectiveScore(map, i, centerDist, spawnDist) {
    const { base, surf } = map;
    if (base[i] === 3) return -1;
    if (base[i] === 4) return -1;                    // 出生点平台不放 VP
    const aux = buildAux(map);
    const gx = gxOf(i), gy = gyOf(i), r = aux.rad;
    const x0 = Math.max(0, gx - r), x1 = Math.min(GRID - 1, gx + r);
    const y0 = Math.max(0, gy - r), y1 = Math.min(GRID - 1, gy + r);
    const tot = (x1 - x0 + 1) * (y1 - y0 + 1);
    const open = windowSum(aux.satPass, x0, y0, x1, y1) / Math.max(1, tot);
    const near = windowSum(aux.satRoad, x0, y0, x1, y1);
    const flat = aux.flat[i];
    const rockD = aux.rockDist[i];
    const road = clamp(near / Math.max(1, tot) * 4, 0, 1);
    let sc = 0.32 * open
      + 0.24 * smoothstep(20 * S, 3 * S, flat)
      + 0.20 * road
      + 0.12 * smoothstep(S, 4 * S, rockD)
      + 0.12 * (1 - Math.abs(spawnDist - 12 * S) / (16 * S));
    if (surf[i] === SURF.PLAYA) sc -= 0.35;           // 盐沼中央不适合当争夺点
    if (surf[i] === SURF.ROCK) sc -= 1;
    return sc;
  }

  /* 由「一个点」得到它的镜像对：地形严格对称，所以这一对必然公平 ——
   * 双方到各自那个点的距离、地形质量、通行代价完全相同。 */
  function cellPair(gx, gy) {
    const a = ci(gx, gy), b = rev(a);
    const ga = [gxOf(a), gyOf(a)], gb = [gxOf(b), gyOf(b)];
    return ga[1] < gb[1] || (ga[1] === gb[1] && ga[0] <= gb[0]) ? [ga, gb] : [gb, ga];
  }

  function placeObjectives(map, opts) {
    const o = Object.assign({}, DEFAULT_OPTS, opts || {});
    const sp = map.spawns[0];
    const cx = (GRID - 1) / 2, cy = (GRID - 1) / 2;
    const seen = reachSet(map.base, map.elev, ci(sp.gx, sp.gy), o.unitSlope);

    /* 先算好每个格子的三项坐标量，避免在打分里反复开方 */
    /* 候选分两档：strict 额外要求 5×5 高差 ≤12 级（能架炮、能停母舰），
     * loose 只要求可达 + 非岩壁。正常情况下用 strict；如果极端地形下
     * strict 凑不满 4 个，再退回 loose —— 宁可 VP 稍微不平，也不能没有 VP。 */
    let cand = [];
    const loose = [];
    for (let i = 0; i < N; i++) {
      if (!seen[i]) continue;
      if (map.base[i] === 3) continue;
      const gx = gxOf(i), gy = gyOf(i);
      if (gx < 4 * S || gy < 4 * S || gx > GRID - 5 * S || gy > GRID - 5 * S) continue;
      const centerDist = Math.hypot(gx - cx, gy - cy);
      if (centerDist < 2.5 * S) continue;        // 正中心是 R 的不动点，做不成一对
      const spawnDist = Math.hypot(gx - sp.gx, gy - sp.gy);
      if (spawnDist < 7 * S) continue;
      const sc = objectiveScore(map, i, centerDist, spawnDist);
      if (sc < 0) continue;
      const item = { i, gx, gy, centerDist, spawnDist, sc, ang: Math.atan2(gy - cy, gx - cx) };
      loose.push(item);
      let flat = 0;
      for (let y = gy - 2 * S; y <= gy + 2 * S; y++) {
        for (let x = gx - 2 * S; x <= gx + 2 * S; x++) {
          if (x < 0 || y < 0 || x >= GRID || y >= GRID) continue;
          flat = Math.max(flat, Math.abs(map.elev[ci(x, y)] - map.elev[i]));
        }
      }
      if (flat <= 12 * S) cand.push(item);
    }
    if (cand.length < 4) cand = loose;
    /* 分数降序；同分按 index，保证确定性 */
    cand.sort((a, b) => (b.sc - a.sc) || (a.i - b.i));

    const used = [];
    const farEnough = (c, minD) => used.every(u =>
      Math.hypot(c.gx - u[0], c.gy - u[1]) >= minD &&
      Math.hypot(c.gx - (GRID - 1 - u[0]), c.gy - (GRID - 1 - u[1])) >= minD);
    const angDiff = (a, b) => {
      let d = Math.abs(a - b) % (Math.PI * 2);
      return d > Math.PI ? Math.PI * 2 - d : d;
    };

    /* 两对:近点对在 r∈[8,14]，侧翼对在 r∈[14,21]；轴向差 ≥ 55°。
     * 这样四个点分布在不同方向上，不会出现「四个 VP 全在一条线上」。 */
    const bands = [[8 * S, 14 * S], [14 * S, 21 * S]];
    const pairs = [];
    let first = null;
    for (let b = 0; b < 2; b++) {
      const [lo, hi] = bands[b];
      let pick = null;
      const passes = [0, 1, 2].map(k => ({
        ang: k === 0 ? 0.96 : (k === 1 ? 0.70 : 0.35),   // 55° / 40° / 20°
        sep: (k === 0 ? 8 : (k === 1 ? 6.5 : 5)) * S,
        pad: k * 2.5 * S
      }));
      for (const p of passes) {
        for (const c of cand) {
          if (c.centerDist < lo - p.pad || c.centerDist > hi + p.pad) continue;
          if (first && angDiff(c.ang, first.ang) < p.ang) continue;
          if (!farEnough(c, p.sep)) continue;
          pick = c; break;
        }
        if (pick) break;
      }
      if (!pick) continue;
      if (!first) first = pick;
      const pair = cellPair(pick.gx, pick.gy);
      used.push(pair[0], pair[1]);
      pairs.push(pair);
    }

    /* 兜底：极端地形下带内找不到两个满足夹角的点 → 退化成「沿中心两侧取最平的点」，
     * 仍然成对、仍然可达，只是分布不一定理想。 */
    if (pairs.length < 2) {
      for (const d of [[0.62, 0.38], [0.34, 0.66], [0.5, 0.72], [0.28, 0.5]]) {
        if (pairs.length >= 2) break;
        const ideal = [Math.round(d[0] * (GRID - 1)), Math.round(d[1] * (GRID - 1))];
        let best = null, bestD = Infinity;
        for (const c of cand) {
          if (!farEnough(c, 5 * S)) continue;
          const dd = Math.hypot(c.gx - ideal[0], c.gy - ideal[1]);
          if (dd < bestD) { bestD = dd; best = c; }
        }
        if (!best) continue;
        const pair = cellPair(best.gx, best.gy);
        used.push(pair[0], pair[1]);
        pairs.push(pair);
      }
    }

    const vpCells = [];
    for (const p of pairs) { vpCells.push(p[0], p[1]); }

    /* ---------- 资源点：采掘车要在硬地上跑，所以优先干河谷/砾漠 ---------- */
    /* 生成器自带矿脉时（MC 风格），资源点直接用矿脉位置。
     * 但只接受**成对且两格都可达**的矿脉：单边可达会让一方少一个资源点，
     * 那是公平性 bug，不是「运气不好」。凑不出两对就退回打分选点。 */
    if (Array.isArray(o.forcedRes) && o.forcedRes.length >= 4) {
      const okCell = c => {
        const i = ci(c[0], c[1]);
        return seen[i] && map.base[i] !== 3 && map.base[i] !== 4;
      };
      const pairs = [], taken = new Set();
      for (const c of o.forcedRes) {
        const key = c[0] + ',' + c[1];
        if (taken.has(key)) continue;
        const m = [GRID - 1 - c[0], GRID - 1 - c[1]];
        if (!okCell(c) || !okCell(m)) continue;
        if (!o.forcedRes.some(d => d[0] === m[0] && d[1] === m[1])) continue;
        pairs.push([c, m]);
        taken.add(key); taken.add(m[0] + ',' + m[1]);
        if (pairs.length === 2) break;
      }
      if (pairs.length >= 2) {
        const cells = [].concat(pairs[0], pairs[1]);
        const ord0 = (a, b) => (a[1] - b[1]) || (a[0] - b[0]);
        return { vpCells: vpCells.slice().sort(ord0), resCells: cells.sort(ord0), reachable: seen };
      }
    }
    const resCand = [];
    for (let i = 0; i < N; i++) {
      if (!seen[i]) continue;
      /* 矿点不打在沙丘脊上：丘脊是松沙，既不承重也没有露头；
       * 真实砂矿在干河谷的砾石层里（这也是下面 accLog 加分的原因）。 */
      if (map.base[i] === 3 || map.base[i] === 4 || map.base[i] === 2) continue;
      const gx = gxOf(i), gy = gyOf(i);
      if (gx < 3 * S || gy < 3 * S || gx > GRID - 4 * S || gy > GRID - 4 * S) continue;
      const spawnDist = Math.hypot(gx - sp.gx, gy - sp.gy);
      if (spawnDist < 4 * S || spawnDist > 13 * S) continue;
      if (map.surf[i] === SURF.PLAYA) continue;
      let vpD = 99 * S;
      for (const c of vpCells) vpD = Math.min(vpD, Math.hypot(gx - c[0], gy - c[1]));
      if (vpD < 4.5 * S) continue;
      if (map.slope[i] > 18 * S) continue;
      const hard = map.base[i] === 1 ? 1 : 0;
      const sc = 0.42 * hard + 0.24 * (1 - Math.abs(spawnDist - 7.5 * S) / (6 * S))
        + 0.18 * (1 - Math.min(1, map.slope[i] / (18 * S)))
        + 0.16 * clamp((map.accLog[i] - 0.25) * 2, 0, 1);   // 河道砂矿
      resCand.push({ i, gx, gy, sc, spawnDist });
    }
    resCand.sort((a, b) => (b.sc - a.sc) || (a.i - b.i));
    const resCells = [];
    const resUsed = [];
    for (const c of resCand) {
      if (resCells.length >= Math.min(4, o.resCount)) break;
      if (resUsed.some(u => Math.hypot(c.gx - u[0], c.gy - u[1]) < 5 * S)) continue;
      if (resUsed.some(u => Math.hypot(c.gx - (GRID - 1 - u[0]), c.gy - (GRID - 1 - u[1])) < 5 * S)) continue;
      if (vpCells.some(v => Math.hypot(c.gx - v[0], c.gy - v[1]) < 4.5 * S)) continue;
      const pair = cellPair(c.gx, c.gy);
      resCells.push(pair[0], pair[1]);
      resUsed.push(pair[0], pair[1]);
    }
    /* 资源点兜底：至少保证 2 个（一对），否则经济无法展开 */
    if (resCells.length < 2) {
      for (const c of cand) {
        if (resCells.length >= 2) break;
        const d = Math.hypot(c.gx - sp.gx, c.gy - sp.gy);
        if (d < 4 * S || d > 16 * S) continue;
        if (map.base[c.i] === 2) continue;
        if (vpCells.some(v => Math.hypot(c.gx - v[0], c.gy - v[1]) < 4 * S)) continue;
        const pair = cellPair(c.gx, c.gy);
        resCells.push(pair[0], pair[1]);
      }
    }

    /* 输出顺序固定：先按 y 再按 x，保证同图同协议 */
    const ord = (a, b) => (a[1] - b[1]) || (a[0] - b[0]);
    vpCells.sort(ord); resCells.sort(ord);
    return { vpCells, resCells, reachable: seen };
  }

  /* ---------------------- 组装 ---------------------- */
  /* buildOnce(salt, opts) —— 一次「直接生成」，不做连通性重试。
   * 旧版这个函数只产地形；现在它把地形、出生点、VP、资源点一次做完，
   * 因为目标点本来就是地形的函数。 */
  function buildOnce(salt, opts) {
    const o = Object.assign({}, DEFAULT_OPTS, opts || {});
    const rng = makeRng((salt ^ 0x2f6e2b1) >>> 0);
    const f = buildFields(salt, rng);
    f.salt = salt;

    /* 水系：先定汇水终点（盆地中心）→ 从高处放水源 → 生长河道 → 按流量下切 */
    const outlet = pickOutlet(f.elev);
    const sources = pickSources(f.elev, f.rock, rng, 6 + ((rng() * 5) | 0));
    const net = growRivers(f.elev, f.rock, outlet, sources);
    carveRiver(f.elev, f.rock, net.flow);

    /* 盐沼压在河道末端之上：汇水终点就是盐盘所在 */
    const pl = makePlaya(f.elev, outlet);
    f.playa = pl.playa; f.sink = pl.sink;

    /* 河漫滩：河道两侧 1 格给 55% 的流量权重。这样河谷有宽度，
     * 也让「沿河的低洼带」在分类里成为可读的干河谷，而不是一条 1 格细线。 */
    const flowSpread = new Float32Array(N);
    for (let i = 0; i < N; i++) {
      if (net.flow[i] > flowSpread[i]) flowSpread[i] = net.flow[i];
      if (net.flow[i] <= 0) continue;
      const gx = gxOf(i), gy = gyOf(i);
      for (let k = 0; k < 8; k++) {
        const nx = gx + DX8[k], ny = gy + DY8[k];
        if (nx < 0 || ny < 0 || nx >= GRID || ny >= GRID) continue;
        const j = ci(nx, ny);
        const v = net.flow[i] * 0.45;
        if (v > flowSpread[j]) flowSpread[j] = v;
      }
    }
    f.flowSpread = flowSpread;
    f.river = net.river;
    f.reached = net.reached;

    /* 流量先对称化，再由它算对数流量 —— accLog 必须严格对称，
     * 而且必须在铺沙丘**之前**算好：河床里不积沙，风沙不会堆在水里。 */
    const symFlow = symmetrize(flowSpread);
    let maxFlow = 1e-6;
    for (let i = 0; i < N; i++) if (symFlow[i] > maxFlow) maxFlow = symFlow[i];
    const lmax = Math.log2(1 + maxFlow);
    const accLog = new Float32Array(N);
    for (let i = 0; i < N; i++) accLog[i] = Math.log2(1 + symFlow[i]) / lmax;
    f.accLog = accLog;
    f.flow = symFlow;

    applyFans(f, net.river);
    applyAeolian(f, rng);

    return finalize(f, o);
  }

  /* ---------------------- 收尾（生成器无关） ----------------------
   * 两种生成器最后都要做同样的事：连续场对称化 → 量化 → 岩壁阈值 → 分类
   * → 出生点 → 平台 → 目标点。抽出来是为了「换生成算法只换前半段」：
   * 如果这些配套步骤各写一套，两套地图的公平性与点位规则迟早会漂。 */
  function finalize(f, o) {
    /* 对称化：只处理连续场。离散的 base/surf 由对称化后的连续场分类得到，
     * 所以不存在「分类边界在缝线上断开」的问题。 */
    const conts = ['elev', 'rock', 'dune', 'duneRelief', 'playa', 'fan', 'flow'];
    for (const k of conts) if (f[k]) f[k] = symmetrize(f[k]);
    /* 离散掩膜（峡谷/洞穴/矿脉）也要镜像：地形本身已经被淡化对称了，
     * 掩膜若只记录生成时的那一侧，就会和「另一半其实是同样地形」矛盾。
     * 用「取或」而不是「取平均」——掩膜是布尔事实，不是强度。 */
    if (f.mirrorMasks) {
      for (const k of Object.keys(f.mirrorMasks)) {
        const m = f.mirrorMasks[k];
        for (let i = 0; i < HALF; i++) {
          const j = N - 1 - i;
          const v = (m[i] || m[j]) ? 1 : 0;
          m[i] = v; m[j] = v;
        }
      }
    }

    /* 对数流量：目标点打分与「干河谷」分类都要它。生成器只负责给 flow，
     * 归一化口径统一放在这里，避免两套生成器各算一版（尺度不一致就不可比）。 */
    let maxFlow = 1e-6;
    if (f.flow) for (let i = 0; i < N; i++) if (f.flow[i] > maxFlow) maxFlow = f.flow[i];
    const lmax = Math.log2(1 + maxFlow);
    const accLog = new Float32Array(N);
    for (let i = 0; i < N; i++) accLog[i] = f.flow ? Math.log2(1 + f.flow[i]) / lmax : 0;
    f.accLog = accLog;

    const elev8 = quantize(f);
    const slope = slopeField(elev8);
    const target = Math.round(o.wallTarget * N);
    const wall = classifyWall(elev8, f.rock, slope, target);

    const pad = new Uint8Array(N);
    const map = {
      elev: elev8, base: new Uint8Array(N), surf: new Uint8Array(N),
      slope, spawns: [{ gx: 10, gy: 10 }, { gx: GRID - 11, gy: GRID - 11 }],
      vpCells: [], resCells: [],
      seed: o.seed == null ? 0 : (o.seed | 0),
      attempt: o.attempt | 0,
      generator: o.generator || 'geomorphic',
      wallRatio: 0,
      climate: Object.assign({ windRad: f.windRad, dune: f.duneParams }, f.climateExtra || {}),
      sink: f.sink,
      /* 连续地貌层一并挂在 map 上：搜索算法、测试与渲染阴影都要用同一份，
       * 不允许各自再算一遍（那必然漂移）。它们不进协议，只在本进程内用。 */
      accLog, duneAmp: f.dune, duneRelief: f.duneRelief,
      playa: f.playa, rock: f.rock, fan: f.fan, duneCut: f.duneCut,
      /* 河道掩膜与流量：测试要验证「水系真的连到盐沼」，渲染也可能用 */
      river: f.river, flow: f.flow, sourcesReached: f.reached
    };
    /* 生成器私有层（MC 的气候参数 / 峡谷 / 洞穴 / 矿脉）原样挂上 */
    if (f.mapExtra) Object.assign(map, f.mapExtra);

    const cls = classify(f, wall, pad);
    map.base.set(cls.base); map.surf.set(cls.surf); map.slope = cls.slope;

    /* 出生点：左下象限搜索 → 镜像出对手 → 压平台 */
    const sp0 = pickSpawn(map, o);
    map.spawns = [sp0, { gx: GRID - 1 - sp0.gx, gy: GRID - 1 - sp0.gy }];
    makePad(map, sp0.gx, sp0.gy, 3 * S);
    makePad(map, map.spawns[1].gx, map.spawns[1].gy, 3 * S);
    for (let i = 0; i < N; i++) if (map.base[i] === 4) pad[i] = 1;
    map.slope = slopeField(map.elev);

    const obj = placeObjectives(map, o);
    map.vpCells = obj.vpCells;
    map.resCells = obj.resCells;
    map.wallRatio = wallRatio(map);
    return map;
  }

  /* generateMap(seed, opts) —— 公开入口：最多 32 次重试，要求
   *   ① 地图连通（两出生点 + 全部 VP / 资源点可达）
   *   ② 岩壁占比落在 WALL_BAND
   * 都不满足时退回「最接近目标占比的连通图」并标 outOfBand；
   * 连连通图都没有时开兜底通道，并标 fallback。**不假装正常**。 */
  /* 地图生成是**纯函数**（同 seed 同算法必定同图），所以按 (seed, 生成器) 缓存。
   * 256² 下一张图 ~250 ms，而实测里同一个 seed 会被反复构造（多局换边、确定性复核、
   * 测试里几十次 new Game）—— 缓存把「每局重新生成」变成「每局克隆一次」。
   * 返回的是**深拷贝**：调用方（测试、工具）可能就地改地图，绝不能污染缓存。 */
  const MAP_CACHE = new Map();
  const MAP_CACHE_MAX = 4;
  function cloneMap(m) {
    if (typeof structuredClone === 'function') {
      try { return structuredClone(m); } catch (e) { /* 退化为手工拷贝 */ }
    }
    const out = Object.assign({}, m);
    for (const k of Object.keys(m)) {
      const v = m[k];
      if (ArrayBuffer.isView(v)) out[k] = v.slice();
      else if (Array.isArray(v)) out[k] = JSON.parse(JSON.stringify(v));
    }
    return out;
  }
  function runGenerate(seed, opts, build) {
    const o = Object.assign({}, DEFAULT_OPTS, opts || {});
    const requested = seed | 0;
    const make = build || buildOnce;
    const cacheKey = requested + '|' + (o.generator || 'geomorphic') + '|' + o.attempts;
    if (MAP_CACHE.has(cacheKey)) {
      const hit = MAP_CACHE.get(cacheKey);
      MAP_CACHE.delete(cacheKey);
      MAP_CACHE.set(cacheKey, hit);          // LRU：命中就挪到队尾
      return cloneMap(hit);
    }
    let best = null, bestGap = Infinity;
    for (let a = 0; a < o.attempts; a++) {
      const m = make(seedSalt(requested, a), Object.assign({}, o, { seed: requested, attempt: a }));
      if (!m) continue;
      if (!connected(m, o)) continue;
      const r = m.wallRatio;
      if (r >= o.wallBand[0] && r <= o.wallBand[1]) {
        MAP_CACHE.set(cacheKey, m);
        while (MAP_CACHE.size > MAP_CACHE_MAX) MAP_CACHE.delete(MAP_CACHE.keys().next().value);
        return cloneMap(m);
      }
      const gap = Math.abs(r - o.wallTarget);
      if (gap < bestGap) { bestGap = gap; best = m; }
    }
    if (best) {
      best.outOfBand = true;
      MAP_CACHE.set(cacheKey, best);
      while (MAP_CACHE.size > MAP_CACHE_MAX) MAP_CACHE.delete(MAP_CACHE.keys().next().value);
      return cloneMap(best);
    }
    const m = make(seedSalt(requested, o.attempts - 1), Object.assign({}, o, { seed: requested, attempt: o.attempts - 1 }));
    carveCorridor(m, 2 * S);
    if (!spawnConnected(m, o)) carveCorridor(m, 5 * S);
    m.fallback = true;
    m.wallRatio = wallRatio(m);
    MAP_CACHE.set(cacheKey, m);
    while (MAP_CACHE.size > MAP_CACHE_MAX) MAP_CACHE.delete(MAP_CACHE.keys().next().value);
    return cloneMap(m);
  }
  function generateMap(seed, opts) { return runGenerate(seed, opts, buildOnce); }

  const API = {
    GRID, CELL, WORLD, N, HALF, S,
    SURF, SURF_NAMES, SURF_BASE, DEFAULT_OPTS,
    seedSalt, makeRng, hash2, valueNoise, fbm, fbmN, clamp, lerp, smoothstep,
    buildFields, symmetrize, classifyWall, pickOutlet, pickSources,
    growRivers, carveRiver, makePlaya, applyFans,
    slopeField, passable, passableFrom, reachSet,
    buildOnce, generateMap, runGenerate, finalize, connected, spawnConnected, carveCorridor, wallRatio,
    pickSpawn, placeObjectives, objectiveScore, surfString, surfFromString,
    encodeMapLayers, decodeMapLayers, bytesToB64, b64ToBytes, rleEncode, rleDecode,
    INTERNALS: { buildFields, growRivers, carveRiver, classifyWall, symmetrize, makePad }
  };
  return API;
});
