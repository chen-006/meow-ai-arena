// 13 张图的选图缩略图：用网站自己的 3D 画面（play.html#thumb=<图>，正片机位、开局站位）截图，再缩成 WebP。
//   先 npx vite（或 npm run preview），再：node scripts/ep11-r3d/thumbs.mjs http://localhost:4374
// 输出 public/img/ep11/maps/<图>.webp（480×300）。转 WebP 用渲染器 venv 的 Python（PIL）。
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { chromium } from 'playwright-core';

const base = process.argv[2] || 'http://localhost:4374';
const MAPS = ['twinlakes', 'yinyang', 'stonecircle', 'skybridge', 'ruins', 'icefloes', 'hourglass', 'canyon', 'oldtown', 'forest', 'caldera', 'piers', 'keeps'];
const OUT = path.resolve('public/img/ep11/maps');
const TMP = path.resolve('test-results/thumbs');
fs.mkdirSync(OUT, { recursive: true }); fs.mkdirSync(TMP, { recursive: true });
const PY = process.env.PY || 'python';
const browser = await chromium.launch({ channel: 'msedge', headless: true, args: ['--enable-gpu', '--ignore-gpu-blocklist', '--use-angle=d3d11'] });
const page = await browser.newPage({ viewport: { width: 960, height: 600 }, locale: 'zh-CN' });
for (const m of MAPS) {
  await page.goto(`${base}/ep11/play.html?thumb=${m}#thumb=${m}`);
  await page.waitForFunction(() => window.__thumbReady, null, { timeout: 90000 });
  await page.waitForTimeout(1200);
  const png = path.join(TMP, m + '.png');
  await page.locator('#stage').screenshot({ path: png });
  execFileSync(PY, ['-c', `from PIL import Image; im=Image.open(r'${png}').convert('RGB'); im=im.resize((480,300), Image.LANCZOS); im.save(r'${path.join(OUT, m + '.webp')}', 'WEBP', quality=82, method=6)`]);
  console.log(m, fs.statSync(path.join(OUT, m + '.webp')).size, 'bytes');
}
await browser.close();
