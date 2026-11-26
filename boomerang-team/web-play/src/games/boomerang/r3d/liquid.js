// 照正片 src\liquid.py + src\liquid_drain.py 移植（第 12 集）：坑里的液面（岩浆湖、港湾 / 护城河的水）。
// 液面设置来自场景包（scene.js 载入后写进 rp.liquid = {kind, z, where, min_cells}）；没有就是普通的坑（通到云海）。
const DELAY = 4.0;   // 岸塌的那一步之后再过几步液面消失

function pitComponents(rp) {
  const H = rp.H, W = rp.W, seen = new Set(), out = [];
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    if (rp.grid[y][x] !== 2 || seen.has(x + ',' + y)) continue;
    const st = [[x, y]], comp = [];
    let border = false;
    seen.add(x + ',' + y);
    while (st.length) {
      const q = st.pop();
      comp.push(q);
      for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
        const rx = q[0] + dx, ry = q[1] + dy;
        if (!(rx >= 0 && rx < W && ry >= 0 && ry < H)) border = true;
        else if (rp.grid[ry][rx] === 2 && !seen.has(rx + ',' + ry)) { seen.add(rx + ',' + ry); st.push([rx, ry]); }
      }
    }
    out.push([comp, border]);
  }
  return out;
}

/** 有液面的格子集合（键 "x,y"） */
export function cells(rp) {
  if (rp._liquid_cells) return rp._liquid_cells;
  const sp = rp.liquid, out = new Set();
  if (sp) for (const [comp, border] of pitComponents(rp)) {
    if ((sp.where || 'enclosed') === 'enclosed' && border) continue;
    if (comp.length < (sp.min_cells || 1)) continue;
    for (const [x, y] of comp) out.add(x + ',' + y);
  }
  rp._liquid_cells = out;
  return out;
}

export function z_at(rp, mx, my) {
  const sp = rp.liquid;
  if (!sp) return null;
  const cx = Math.floor(mx / rp.cell), cy = Math.floor(my / rp.cell);
  if (!(cx >= 0 && cx < rp.W && cy >= 0 && cy < rp.H)) return sp.where === 'all' ? (sp.z ?? -0.55) : null;
  return cells(rp).has(cx + ',' + cy) ? (sp.z ?? -0.55) : null;
}

export function kind(rp) { return rp.liquid ? rp.liquid.kind : null; }

// ---------------------------------------------------------------- liquid_drain
function landWave(rp, x, y) { const w = rp.wave[y][x]; return w >= 0 && w < rp.wave_steps.length ? w : null; }

/** Map "x,y" -> 决定这一格液面何时消失的那波塌陷（null = 一直在） */
export function cell_waves(rp) {
  if (rp._liq_waves) return rp._liq_waves;
  const cs = cells(rp), out = new Map();
  if (cs.size) {
    const INF = 1e9, key = (w) => (w === null ? INF : w);
    const tval = new Map();
    const has = (x, y) => cs.has(x + ',' + y);
    for (const c of cs) {
      const [x, y] = c.split(',').map(Number);
      let best = null;
      for (let dx = -1; dx <= 1; dx++) for (let dy = -1; dy <= 1; dy++) {
        const nx = x + dx, ny = y + dy;
        if ((dx || dy) && nx >= 0 && nx < rp.W && ny >= 0 && ny < rp.H && !has(nx, ny) && rp.grid[ny][nx] !== 2) {
          const k = key(landWave(rp, nx, ny));
          best = best === null ? k : Math.max(best, k);
        }
      }
      if (best !== null) tval.set(c, best);
    }
    let frontier = [...tval.keys()];
    while (frontier.length) {
      const nxt = new Map();
      for (const c of frontier) {
        const [x, y] = c.split(',').map(Number);
        for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
          const q = (x + dx) + ',' + (y + dy);
          if (cs.has(q) && !tval.has(q)) nxt.set(q, Math.min(nxt.has(q) ? nxt.get(q) : INF + 1, tval.get(c)));
        }
      }
      for (const [k, v] of nxt) tval.set(k, v);
      frontier = [...nxt.keys()];
    }
    let changed = true;
    while (changed) {
      changed = false;
      for (const c of cs) {
        if (!tval.has(c)) continue;
        const [x, y] = c.split(',').map(Number);
        const nb = [[1, 0], [-1, 0], [0, 1], [0, -1]].map(([dx, dy]) => (x + dx) + ',' + (y + dy)).filter((q) => tval.has(q));
        if (!nb.length) continue;
        let rim = false;
        for (let dx = -1; dx <= 1 && !rim; dx++) for (let dy = -1; dy <= 1; dy++) {
          const nx = x + dx, ny = y + dy;
          if (!has(nx, ny) && nx >= 0 && nx < rp.W && ny >= 0 && ny < rp.H && rp.grid[ny][nx] !== 2) { rim = true; break; }
        }
        if (rim) continue;
        const mm = Math.min(...nb.map((q) => tval.get(q)));
        if (mm < tval.get(c)) { tval.set(c, mm); changed = true; }
      }
    }
    for (const c of cs) { const v = tval.has(c) ? tval.get(c) : INF; out.set(c, v >= INF ? null : v); }
  }
  rp._liq_waves = out;
  return out;
}

export function live_waves(rp, gt) { return rp.wave_steps.map((s) => !(gt > rp.T + 1 && s > rp.T)); }
export function gone_step(rp, w) { return w === null || w < 0 ? null : rp.wave_steps[w] + DELAY; }
