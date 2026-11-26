// 把 scripts/data/ep11_r3d/scenes/ 里 pack_scenes.py 刚放进来的 <图>.bin 压成 <图>.bin.br（入库用，体积约原来的一半），删掉原文件。
// build-data（scripts/data/ep11.mjs）再解压到 public/gen/ep11/scenes/<图>.bin，线上由 compress.mjs 生成的 .br 传输。
//   node scripts/ep11-r3d/br_bins.mjs
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';

const DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', 'data', 'ep11_r3d', 'scenes');
for (const f of fs.readdirSync(DIR).filter(f => f.endsWith('.bin'))) {
  const p = path.join(DIR, f);
  const raw = fs.readFileSync(p);
  const br = zlib.brotliCompressSync(raw, { params: { [zlib.constants.BROTLI_PARAM_QUALITY]: 11, [zlib.constants.BROTLI_PARAM_LGWIN]: 24, [zlib.constants.BROTLI_PARAM_SIZE_HINT]: raw.length } });
  fs.writeFileSync(p + '.br', br);
  fs.unlinkSync(p);
  console.log(`${f.padEnd(16)} ${(raw.length / 1048576).toFixed(2)} MB → br ${(br.length / 1048576).toFixed(2)} MB`);
}
