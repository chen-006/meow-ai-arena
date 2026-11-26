// 第 11 集 4v4 样局：用网站同一份引擎和第 11 集海选原版程序（第 12 集两队的前身）在四张 4v4 图上各跑几局，
// 挑节奏好的（够长、出局多、分出胜负）写进精选回放 scripts/data/ep11_src/replays/。
//   node scripts/ep11-r3d/gen_4v4.mjs <图> <种子起点> <局数>
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { runMatch } from './node-arena.mjs';

const [map = 'canyon', s0 = '1', cnt = '3'] = process.argv.slice(2);
const mapText = fs.readFileSync(path.join('public', 'gen', 'ep11', 'maps', map + '.txt'), 'utf8');
const A = ['astra', 'sol', 'hydrogen', 'grok'], F = ['fable', 'opus', 'carbon', 'glm'];
for (let i = 0; i < +cnt; i++) {
  const seed = +s0 + i;
  const t0 = Date.now();
  const text = await runMatch({ mode: 2, mapText, seed, bots: [...A, ...F], names: [...A, ...F] });
  const end = text.split('\n').find((l) => l.startsWith('END '));
  const deaths = text.split('\n').filter((l) => / DEATH /.test(l)).length;
  const out = path.join('scripts', 'ep11-r3d', '_4v4', `${map}_s${seed}.txt`);
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, text);
  console.log(map, 'seed', seed, end, 'deaths', deaths, ((Date.now() - t0) / 1000).toFixed(1) + 's');
}
void zlib;
