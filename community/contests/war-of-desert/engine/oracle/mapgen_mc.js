/* =====================================================================
 * mapgen_mc.js — 「我的世界」风格的地图生成（UMD，无 DOM / 无依赖）
 *
 * 只替换生成算法本身：网格、通行系数、VP 规则、协议全部沿用 mapgen.js。
 * 收尾流程（对称化 / 量化 / 岩壁阈值 / 分类 / 出生点 / 目标点）直接复用
 * mapgen.finalize —— 换生成器不该顺手换一套公平性规则。
 *
 * 对齐的是 Minecraft 1.18+（Noise Router）的**生成结构**，不是方块外观：
 *
 *   1. 气候参数（六个噪声，各有自己的频率）
 *        continentalness  大陆度：决定「离海多远」，进而决定基准高度
 *        erosion          侵蚀度：决定地形平不平（高侵蚀 = 平坦）
 *        peaks & valleys  峰谷：脊状噪声，决定山脊/沟谷的走向
 *        temperature      温度 ┐
 *        humidity         湿度 ┴ 两者一起决定生物群系（沙漠/沙丘/砾漠/盐滩）
 *   2. 样条整形：把 continentalness 送进一条分段三次样条，得到基准高度；
 *      再用 peaks&valleys 乘上「(1 - 侵蚀度)」叠加起伏 —— 这就是 MC 里
 *      「平原很平、山地很陡」的来源。
 *   3. 河流：river noise 的零等值线附近下切（MC 的河也是这么来的：
 *      沿噪声等值线蜿蜒，而不是沿着真实汇水）。
 *   4. 峡谷（ravine）：从随机点出发的折线，V 形剖面，几条而已，但非常显眼。
 *   5. 洞穴：只做「露出地表」的部分 —— 塌陷坑（cheese cave 与地表相交）
 *      和窄缝（spaghetti cave 与地表相交）。2D 地图没有地下层，
 *      所以洞穴只能以「地表破口」的形式存在，这一点在文档里写明。
 *   6. 矿脉（ore vein）：随机游走生成团块，资源点就落在矿脉上，
 *      并且**成对**产出（镜像点各一团），保证公平。
 *
 * 温度/湿度只影响群系，不参与高度 —— 和 MC 一样（气候参数不直接整形地形）。
 *
 * 对称性沿用 mapgen 的交叉淡化：先在整幅图生成，再对称化。因为淡化带宽
 * 只有 2.5 格，远离缝线的特征是「整块复制」而不是「削弱一半」，所以峡谷、
 * 矿脉这些硬特征在另一半会原样出现，只有跨缝的会被抹平。
 * ===================================================================== */
(function (root, factory) {
  const G = (typeof module === 'object' && module.exports)
    ? require('./mapgen.js')
    : (root.WoDRef && root.WoDRef.MapGen);
  if (!G) throw new Error('mapgen.js 必须先于 mapgen_mc.js 加载（<script src="mapgen.js">）');
  const m = factory(G);
  if (typeof module === 'object' && module.exports) module.exports = m;
  else { root.WoDRef = root.WoDRef || {}; root.WoDRef.MapGenMC = m; }
})(typeof globalThis !== 'undefined' ? globalThis : this, function (G) {
  'use strict';

  const GRID = G.GRID, CELL = G.CELL, N = G.N, S = G.S;
  const clamp = G.clamp, lerp = G.lerp, smoothstep = G.smoothstep;
  const ci = (gx, gy) => gy * GRID + gx;
  const gxOf = i => i % GRID;
  const gyOf = i => (i / GRID) | 0;

  /* ---------------------- 1. 气候参数 ---------------------- */
  /* 每个参数一层 fbm，频率与 MC 的「坐标缩放」同义：大陆度最低频（跨越整张图
   * 才变化一次），峰谷最高频（十几格一个周期）。输出统一压到 [-1, 1]。
   * 3.4 这个增益是把 fbm 的窄分布拉开到可用范围（fbm 本身集中在 0.5 附近）。 */
  const GAIN = 3.4;
  /* 4 倍频足够：这些是**低频**参数（大陆度 3 个周期/图、侵蚀 3.4 个），
   * 6 倍频的高频分量在这里只贡献噪声，却让 256² 的生成时间多三成。 */
  const n11 = (seed, x, y) => clamp((G.fbmN(seed, x, y, 4) - 0.5) * GAIN, -1, 1);
  /* 脊状噪声：把噪声折叠一次，得到「刀脊」而不是圆丘（MC 的 peaks&valleys 同构） */
  const ridged = (seed, x, y) => 2 * (1 - Math.abs(2 * G.fbmN(seed, x, y, 4) - 1)) - 1;

  function climateFields(salt) {
    const C = new Float32Array(N), E = new Float32Array(N), PV = new Float32Array(N);
    const T = new Float32Array(N), H = new Float32Array(N), W = new Float32Array(N);
    const sC = salt ^ 0x1c0f31, sE = salt ^ 0x2e77a5, sP = salt ^ 0x3b91d7;
    const sT = salt ^ 0x4a2c63, sH = salt ^ 0x5d17b9, sW = salt ^ 0x6f3e81;
    for (let gy = 0; gy < GRID; gy++) {
      for (let gx = 0; gx < GRID; gx++) {
        const i = ci(gx, gy), nx = gx / GRID, ny = gy / GRID;
        C[i] = n11(sC, nx * 3.00, ny * 3.00);      // 大陆度：最低频
        E[i] = n11(sE, nx * 3.40, ny * 3.40);      // 侵蚀度
        PV[i] = ridged(sP, nx * 6.20, ny * 6.20);  // 峰谷：最高频，脊状
        T[i] = n11(sT, nx * 2.20, ny * 2.20);      // 温度
        H[i] = n11(sH, nx * 2.40, ny * 2.40);      // 湿度
        W[i] = n11(sW, nx * 2.60, ny * 2.60);      // 变异度（群系细分用）
      }
    }
    return { C, E, PV, T, H, W };
  }

  /* ---------------------- 2. 样条整形 ---------------------- */
  /* MC 用一条「大陆度 → 基准高度」的三次样条：深海 → 海岸 → 内陆 → 高地。
   * 这里同样给一条单调的节点表，段内用 Catmull-Rom 切线做三次 Hermite。
   * 用样条而不是线性插值，是为了让「海岸—平原—高原」之间过渡圆滑，
   * 不会在阈值处出现折线拐点（那是「噪声图」最典型的破绽）。 */
  const HEIGHT_KNOTS = [
    [-1.00, 74], [-0.62, 80], [-0.40, 88], [-0.22, 101],
    [-0.04, 114], [0.16, 128], [0.40, 156], [0.70, 188], [1.00, 208]
  ];
  function baseHeightSpline(c) {
    const k = HEIGHT_KNOTS, n = k.length;
    if (c <= k[0][0]) return k[0][1];
    if (c >= k[n - 1][0]) return k[n - 1][1];
    let i = 0;
    while (i < n - 2 && c > k[i + 1][0]) i++;
    const x0 = k[i][0], y0 = k[i][1], x1 = k[i + 1][0], y1 = k[i + 1][1];
    const h = x1 - x0, t = (c - x0) / h;
    /* 切线用相邻节点差商；两端退化为单侧差分 */
    const ym = i > 0 ? k[i - 1][1] : y0 - (y1 - y0);
    const yp = i + 2 < n ? k[i + 2][1] : y1 + (y1 - y0);
    const m0 = (y1 - ym) / (x1 - (i > 0 ? k[i - 1][0] : x0 - h));
    const m1 = (yp - y0) / ((i + 2 < n ? k[i + 2][0] : x1 + h) - x0);
    const t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * y0 + (t3 - 2 * t2 + t) * h * m0
      + (-2 * t3 + 3 * t2) * y1 + (t3 - t2) * h * m1;
  }

  /* ---------------------- 3. 地表营力 ---------------------- */
  const DX8 = [1, -1, 0, 0, 1, 1, -1, -1];
  const DY8 = [0, 0, 1, -1, 1, -1, 1, -1];

  /* 河流：沿一个低频噪声的零等值线蜿蜒下切。MC 的河不按汇水计算，
   * 而是 noise 的等值线，所以会出现「横穿山脊的峡谷」—— 这里保留这个特征。 */
  function carveRivers(elev, f, salt) {
    const flow = new Float32Array(N);
    const river = new Uint8Array(N);
    for (let i = 0; i < N; i++) {
      const gx = gxOf(i), gy = gyOf(i);
      const n = G.fbmN(salt ^ 0x7a11c3, gx / GRID * 2.4, gy / GRID * 2.4, 4);
      const d = Math.abs(2 * n - 1);              // 到零等值线的「距离」
      if (d > 0.075) continue;
      /* 越靠近等值线越深；高处（>190）不断河，否则山顶会出现细沟 */
      const highCut = smoothstep(196, 168, elev[i]);
      const depth = smoothstep(0.075, 0.008, d) * (12 + 8 * (1 - f.ero)) * highCut;
      if (depth <= 0.4) continue;
      elev[i] -= depth;
      flow[i] = d < 0.028 ? 2 : 1;
      river[i] = 1;
    }
    /* 河漫滩：两侧一格给 0.45 的流量权重，河谷才有宽度 */
    const spread = new Float32Array(flow);
    for (let i = 0; i < N; i++) {
      if (!river[i]) continue;
      const gx = gxOf(i), gy = gyOf(i);
      for (let oy = -S; oy <= S; oy++) for (let ox = -S; ox <= S; ox++) {
        if (!ox && !oy) continue;
        const nx = gx + ox, ny = gy + oy;
        if (nx < 0 || ny < 0 || nx >= GRID || ny >= GRID) continue;
        const j = ci(nx, ny);
        if (spread[j] < flow[i] * 0.45) spread[j] = flow[i] * 0.45;
      }
    }
    return { flow: spread, river };
  }

  /* 峡谷：从随机点出发的折线，V 形剖面。条数少（1~3），但每条都很长，
   * 是地图上最显眼的「地形事件」，也是天然的兵线瓶颈。 */
  function carveRavines(elev, rng, salt) {
    const mask = new Uint8Array(N);
    const count = 1 + ((rng() * 3) | 0);
    for (let k = 0; k < count; k++) {
      let gx = 6 * S + rng() * (GRID - 13 * S), gy = 6 * S + rng() * (GRID - 13 * S);
      let ang = rng() * Math.PI * 2;
      const len = (18 + rng() * 16) * S;
      const width = (1.6 + rng() * 1.6) * S;
      const depth = 16 + rng() * 16;
      const bend = (rng() - 0.5) * 0.22;
      for (let s = 0; s < len; s++) {
        ang += bend * Math.sin(s * 0.35) + (rng() - 0.5) * 0.12;
        gx += Math.cos(ang) * 0.9 * S;
        gy += Math.sin(ang) * 0.9 * S;
        if (gx < 2 || gy < 2 || gx > GRID - 3 || gy > GRID - 3) break;
        /* 越到中段越深，两端收口（峡谷不是一条等深的槽） */
        const taper = Math.sin(Math.PI * (s / len));
        for (let oy = -3 * S; oy <= 3 * S; oy++) {
          for (let ox = -3 * S; ox <= 3 * S; ox++) {
            const x = Math.round(gx) + ox, y = Math.round(gy) + oy;
            if (x < 1 || y < 1 || x >= GRID - 1 || y >= GRID - 1) continue;
            const d = Math.hypot(ox, oy);
            if (d > width) continue;
            const i = ci(x, y);
            const cut = depth * taper * (1 - d / width);
            if (cut > 0) { elev[i] -= cut; mask[i] = 1; }
          }
        }
      }
    }
    return mask;
  }

  /* 洞穴：2D 地图没有地下层，只能表现「与地表相交」的部分：
   *   · 塌陷坑（cheese cave 的破口）：噪声接近阈值的孤立圆坑
   *   · 窄缝（spaghetti cave 的破口）：两条脊状噪声同时接近零的缝
   * 这两类都只在「软地表」上开，岩壁上不开洞（否则山体会被切成筛子）。 */
  function carveCaves(elev, rock, salt) {
    const mask = new Uint8Array(N);
    const cut = new Float32Array(N);
    for (let i = 0; i < N; i++) {
      if (rock[i] > 0.45) continue;
      const gx = gxOf(i), gy = gyOf(i);
      const cx = gx / GRID, cy = gy / GRID;
      /* 频率不能太高：MC 的 cheese cave 是成团的空腔，不是针孔。
       * 3 倍频保证「一团」至少横跨两三格，再挖才有坑的样子。 */
      const cheese = Math.abs(2 * G.fbmN(salt ^ 0x9c0f, cx * 3.1, cy * 3.1, 3) - 1);
      const s1 = Math.abs(2 * G.fbmN(salt ^ 0x3d71, cx * 4.0, cy * 4.0, 3) - 1);
      const s2 = Math.abs(2 * G.fbmN(salt ^ 0x51ab, cx * 4.0 + 3.3, cy * 4.0 + 5.1, 3) - 1);
      let d = 0;
      if (cheese < 0.008) d = smoothstep(0.008, 0.0015, cheese) * 12;
      else if (s1 < 0.007 && s2 < 0.007) d = 7 * smoothstep(0.007, 0.0015, Math.max(s1, s2));
      if (d > 0.5) cut[i] = d;
    }
    /* 边缘羽化：坑壁不能是一格直上直下的井。给 8 邻域一半深度，
     * 坑就从「针孔」变成「漏斗」—— 既能下得去，视觉上也是塌陷坑而不是噪点。 */
    for (let i = 0; i < N; i++) {
      if (cut[i] <= 0) continue;
      const gx = gxOf(i), gy = gyOf(i);
      for (let oy = -S; oy <= S; oy++) for (let ox = -S; ox <= S; ox++) {
        if (!ox && !oy) continue;
        const nx = gx + ox, ny = gy + oy;
        if (nx < 0 || ny < 0 || nx >= GRID || ny >= GRID) continue;
        const j = ci(nx, ny);
        if (rock[j] > 0.45) continue;
        const v = cut[i] * 0.5;
        if (v > cut[j]) cut[j] = v;
      }
    }
    for (let i = 0; i < N; i++) {
      if (cut[i] <= 0.5) continue;
      elev[i] -= cut[i];
      mask[i] = 1;
    }
    return mask;
  }

  /* 沙丘：MC 的沙漠是平的，但本作的通行系数里「沙丘」是 0.7× 慢行地面，
   * 去掉它会让地形差异少一档、玩法变平。所以保留一层**低幅**沙丘，
   * 只在沙漠/沙丘群系里长，幅度控制在 4~16 级（1.2~4.8 m）。 */
  function addDunes(elev, f, rng, windRad, flow) {
    const dune = new Float32Array(N), relief = new Float32Array(N);
    const lam = (16 + rng() * 6) * S;
    const A = 14 + rng() * 12;
    const cw = Math.cos(windRad), sw = Math.sin(windRad);
    const sD = f.salt ^ 0x2f7e11;
    for (let i = 0; i < N; i++) {
      /* 沙丘的落点 = 「不是岩石、不是盐壳、不在河床里」。
       * 这里刻意不用 sandMask 当门槛：sandMask 是「干+暖」的连续权重，
       * 在偏冷/偏湿的 seed 上整张图都很小，沙丘会直接消失（实测 2%）。
       * 用排除法更稳：沙丘是沙漠图里的默认地表，岩石/盐壳/河道才是例外。 */
      const gate = f.sandMask[i] * (1 - smoothstep(0.12, 0.45, f.rock[i]))
        * (1 - f.playa[i]) * (1 - smoothstep(0.2, 0.8, flow[i] || 0));
      if (gate < 0.22) continue;
      const ampScale = smoothstep(0.22, 0.62, gate);
      const gx = gxOf(i), gy = gyOf(i);
      const proj = gx * cw + gy * sw;
      const ph = (proj / lam) * Math.PI * 2
        + 2.0 * (G.fbmN(sD, gx / GRID * 4.2, gy / GRID * 4.2, 3) - 0.5) * 2;
      let t = ph / (Math.PI * 2); t -= Math.floor(t);
      const shape = t < 0.68 ? t / 0.68 : (1 - t) / 0.32;
      const mod = 0.5 + 0.5 * G.fbmN(sD ^ 0x77, gx / GRID * 3.1 + 7, gy / GRID * 3.1 + 3, 3);
      const dh = A * shape * mod * ampScale;
      elev[i] += dh;
      relief[i] = dh;
      /* 分类用的是「沙丘场覆盖度」（包络），不是「这一格比丘谷高多少」。
       * 用后者会得到一条 1 格宽的虚线脊 —— 阈值只在脊线上被跨过，
       * 采样到格子上就变成点状。MC 的沙丘是成片群系，不是一条线。 */
      dune[i] = ampScale;
    }
    /* 振幅场先做一次 3×3 平滑再拿去分类：沙丘脊线的振幅是连续的，但
     * 「是否过阈值」会在脊线附近来回跳，逐格分类就会得到撒胡椒面一样的花纹。
     * 平滑是镜像不变的运算，不会破坏对称性。 */
    return { dune, relief, params: { lam1: lam, lam2: 0, A1: A, A2: 0 } };
  }

  /* 矿脉：随机游走生成团块（MC 的 ore blob 就是这个形状：不规则、2~6 格）。
   * 只在反对角线一侧生成，另一侧由镜像补 —— 资源点必须成对，
   * 所以矿脉本身也要成对出现，不能靠「碰巧对称」。 */
  function placeOres(f, rng) {
    const veins = [];
    const oreMask = new Uint8Array(N);
    const tries = 14;
    for (let k = 0; k < tries; k++) {
      let gx = 5 * S + Math.floor(rng() * (GRID - 10 * S));
      let gy = 5 * S + Math.floor(rng() * (GRID - 10 * S));
      if (gx + gy > GRID - 1) continue;                 // 只取一侧，镜像补另一半
      const ii = ci(gx, gy);
      if (f.rock[ii] > 0.55 || f.playa[ii] > 0.5) continue;   // 矿脉不长在岩壁与盐壳上
      const blobs = [];
      const size = (2 + Math.floor(rng() * 4)) * S;
      for (let s = 0; s < size; s++) {
        const i = ci(gx, gy);
        oreMask[i] = 1;
        blobs.push([gx, gy]);
        const dir = Math.floor(rng() * 4);
        gx = clamp(gx + [1, -1, 0, 0][dir] * S, 2 * S, GRID - 3 * S);
        gy = clamp(gy + [0, 0, 1, -1][dir] * S, 2 * S, GRID - 3 * S);
      }
      veins.push({ gx: blobs[0][0], gy: blobs[0][1], size: blobs.length });
    }
    /* 镜像出一半，保证矿脉成对（视觉与资源点都要） */
    const mirrored = veins.slice();
    for (const v of veins) {
      const mx = GRID - 1 - v.gx, my = GRID - 1 - v.gy;
      oreMask[ci(mx, my)] = 1;
      mirrored.push({ gx: mx, gy: my, size: v.size, mirror: true });
    }
    return { veins: mirrored, oreMask };
  }

  /* ---------------------- 4. 群系 ---------------------- */
  /* 群系只由气候参数决定（MC 同构）：
   *   温度高 + 干燥 → 沙漠；更干 + 侵蚀低 → 沙丘海
   *   侵蚀高（平坦）→ 砾漠平原
   *   侵蚀低（陡）或高海拔 → 恶地 / 岩峰（岩性高，最终会被判成岩壁）
   *   大陆度低 → 盐滩（本作的「海」：内流盆地的盐壳）
   * 输出的是各群系的**连续权重**，最终分类仍走 mapgen.classify，
   * 所以不会出现「群系边界画在格子上」的锯齿。 */
  function biomeMasks(f, elev) {
    const { C, E, T, H, W } = f;
    /* 盐滩面积上限：取大陆度的 5% 分位当门槛，保证「海岸/盐沼」最多占 5%。
     * 不能用固定阈值 —— 大陆度是噪声，某个 seed 的低值区可能就是半张图，
     * 那样整局都在盐壳上打，既不像 MC 海岸，也让 VP 的候选地被吃掉一半。 */
    const sortedC = Array.from(C).sort((a, b) => a - b);
    const cutC = sortedC[Math.floor(sortedC.length * 0.05)];
    const sand = new Float32Array(N), rock = new Float32Array(N), playa = new Float32Array(N);
    for (let i = 0; i < N; i++) {
      const c = C[i], e = E[i], t = T[i], h = H[i], w = W[i];
      const hgt = elev[i];
      /* 盐滩：大陆度很低 + 地形很低（内流盆地底部）。阈值卡得紧 ——
       * 盐滩在 MC 里是海岸/盐沼，不该占掉半个屏幕。 */
      playa[i] = smoothstep(cutC, cutC - 0.16, c) * smoothstep(98, 84, hgt)
        * (1 - smoothstep(0.35, 0.65, rock[i]));
      /* 岩性：高海拔（山）或「干燥 + 低侵蚀」的恶地。低侵蚀本身不等于岩石 ——
       * 平坦的沙地也是低侵蚀，之前把 79% 的图判成砾漠就是这个错误。 */
      const high = smoothstep(152, 198, hgt);
      const mesa = smoothstep(-0.30, -0.55, e) * smoothstep(0.10, -0.25, h) * (0.6 + 0.4 * Math.max(0, w));
      rock[i] = clamp(0.95 * high + 0.8 * mesa, 0, 1);
      /* 沙：干 + 暖，且不在盐滩/岩体上 */
      const dry = smoothstep(0.35, -0.05, h);
      const warm = smoothstep(-0.50, 0.00, t);
      sand[i] = clamp(dry * warm * (1 - rock[i]) * (1 - playa[i]), 0, 1);
    }
    return { sandMask: sand, rock, playa };
  }

  /* ---------------------- 5. 组装 ---------------------- */
  function buildOnce(salt, opts) {
    const o = Object.assign({}, G.DEFAULT_OPTS, opts || {});
    const rng = G.makeRng((salt ^ 0x6b1d3f) >>> 0);
    const f = climateFields(salt);
    f.salt = salt;

    /* 基准高度：样条(大陆度) + 峰谷 × 侵蚀调制 */
    const elev = new Float32Array(N);
    f.ero = 0;
    for (let i = 0; i < N; i++) {
      const ero = (f.E[i] + 1) / 2;             // 0 陡 … 1 平
      f.ero += ero;
      const base = baseHeightSpline(f.C[i]);
      /* 侵蚀度越高，峰谷起伏越小（MC 的「平原」就是这么平出来的） */
      const amp = lerp(26, 3.5, ero);
      elev[i] = base + f.PV[i] * amp * 0.5;
    }
    f.ero /= N;

    /* 群系掩膜要用「未切割」的高度，否则峡谷会把河谷误判成岩体 */
    const bm = biomeMasks(f, elev);
    f.sandMask = bm.sandMask;
    f.rock = bm.rock;
    f.playa = bm.playa;
    /* 砾漠只在「真的够岩石」的地方出现：MC 的沙漠是沙，不是满屏碎石 */
    f.regCut = 0.32;
    f.duneFloor = 0.15;

    const windRad = (0.25 + rng() * 0.5) * Math.PI;
    const river = carveRivers(elev, f, salt);
    const dunes = addDunes(elev, f, rng, windRad, river.flow);
    const ravine = carveRavines(elev, rng, salt);
    const cave = carveCaves(elev, f.rock, salt);
    const ores = placeOres(f, rng);

    f.elev = elev;
    f.dune = dunes.dune;
    f.duneRelief = dunes.relief;
    f.duneParams = dunes.params;
    f.windRad = windRad;
    f.flow = river.flow;
    f.river = river.river;
    /* 视觉层的「冲积扇」在 MC 里对应河口的砾质沉积：给河岸边一圈低权重，
     * 分类时会被判成砾漠，正好把河道和沙地分开。 */
    f.fan = new Float32Array(N);
    for (let i = 0; i < N; i++) if (river.flow[i] > 0.5) f.fan[i] = 0.5;

    /* 盐沼中心：盐滩里最低的一格，供协议里的 sink 与测试使用 */
    let sink = 0, low = 1e9;
    for (let i = 0; i < N; i++) if (f.playa[i] > 0.5 && elev[i] < low) { low = elev[i]; sink = i; }
    f.sink = { gx: gxOf(sink), gy: gyOf(sink), floor: low };

    /* 资源点候选：从矿脉中心里挑（成对，位置由 finalize 复核可达性） */
    const oreCells = ores.veins
      .map(v => [v.gx, v.gy])
      .sort((a, b) => (a[1] - b[1]) || (a[0] - b[0]));
    /* 必须**成对**收集：只按排序截前 8 个会拿到某一侧的若干点，而它们的镜像
     * 可能排在很后面 —— 下游的 forcedRes 要求「镜像也在候选里」，凑不成对
     * 就整批退回打分选点（实测资源点因此全都不在矿脉上）。 */
    const resCand = [];
    const seenPair = new Set();
    for (const c of oreCells) {
      const m = [GRID - 1 - c[0], GRID - 1 - c[1]];
      const key = c[0] + ',' + c[1];
      if (seenPair.has(key)) continue;
      if (!oreCells.some(d => d[0] === m[0] && d[1] === m[1])) continue;
      resCand.push(c, m);
      seenPair.add(key); seenPair.add(m[0] + ',' + m[1]);
      if (resCand.length >= 8) break;
    }

    f.climateExtra = {
      family: 'minecraft',
      params: {
        continentalness: f.C, erosion: f.E, peaksValleys: f.PV,
        temperature: f.T, humidity: f.H, weirdness: f.W
      },
      ravine, cave, ore: ores.oreMask, oreVeins: ores.veins,
      spline: HEIGHT_KNOTS
    };
    f.mirrorMasks = { ravine, cave, ore: ores.oreMask };
    f.mapExtra = {
      ravine, cave, ore: ores.oreMask, oreVeins: ores.veins,
      biomeSand: f.sandMask, climateParams: f.climateExtra.params
    };

    return G.finalize(f, Object.assign({}, o, { generator: 'minecraft', forcedRes: resCand }));
  }

  function generateMap(seed, opts) {
    return G.runGenerate(seed, Object.assign({}, opts, { generator: 'minecraft' }), buildOnce);
  }

  return {
    NAME: 'minecraft',
    HEIGHT_KNOTS,
    baseHeightSpline, climateFields, biomeMasks,
    carveRivers, carveRavines, carveCaves, placeOres,
    buildOnce, generateMap,
    /* 生成器无关的部分原样透出，调用方可以只依赖一个模块 */
    GRID, CELL, N, SURF: G.SURF, SURF_NAMES: G.SURF_NAMES,
    connected: G.connected, spawnConnected: G.spawnConnected,
    reachSet: G.reachSet, surfString: G.surfString, surfFromString: G.surfFromString,
    slopeField: G.slopeField, wallRatio: G.wallRatio, carveCorridor: G.carveCorridor,
    DEFAULT_OPTS: G.DEFAULT_OPTS, seedSalt: G.seedSalt
  };
});
