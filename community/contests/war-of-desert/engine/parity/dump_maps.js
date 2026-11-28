'use strict';
/* dump_maps.js · 生成 JS 侧地图基准（Go 逐格比对的锚）
 *
 * 运行：node engine/parity/dump_maps.js   （在 war_of_desert/ 下）
 * 产物：engine/parity/baselines/maps-<rules无关>.json
 *   —— 3 个 seed × 2 个生成器，wire 三层（RLE+b64）+ spawns/VP/资源点 + wallRatio。
 * 注意：这是移植保真度的「诊断锚」而非正式门禁（正式门禁是 golden trace +
 * 绝对锚点）。若 Go 与 JS 有少量格子漂移（超越函数 ulp），报告差异计数。
 */
const path = require('path');
const fs = require('fs');
const M = require('../../../Dev/MVP/referee/mapgen.js');
const MC = require('../../../Dev/MVP/referee/mapgen_mc.js');

const SEEDS = [1, 2, 20261109];
const GENS = [
  ['geomorphic', M],
  ['minecraft', MC],
];

const out = { generatedAt: new Date().toISOString(), maps: [] };
for (const [gen, mod] of GENS) {
  for (const seed of SEEDS) {
    const m = mod.generateMap(seed, { generator: gen });
    out.maps.push({
      generator: gen,
      seed,
      attempt: m.attempt,
      outOfBand: !!m.outOfBand,
      fallback: !!m.fallback,
      wallRatio: m.wallRatio,
      layers: M.encodeMapLayers(m),
      spawns: m.spawns.map(s => [s.gx, s.gy]),
      vpCells: m.vpCells,
      resCells: m.resCells,
    });
  }
}
const dest = path.join(__dirname, 'baselines', 'maps-js.json');
fs.writeFileSync(dest, JSON.stringify(out, null, 1));
console.log('MAPS_WRITTEN ' + path.relative(process.cwd(), dest) + '  maps=' + out.maps.length);
