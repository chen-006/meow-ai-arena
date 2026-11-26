// 把各期的引擎和 bot 编成 WebAssembly，输出到 public/gen/<期>/wasm/。
//
//   node scripts/build-wasm.mjs                 编全部期（默认并发 2）
//   node scripts/build-wasm.mjs ep6 -j 1        只编第 6 期，并发 1
//   node scripts/build-wasm.mjs ep6 --only gpt-6-astra,baseline
//   node scripts/build-wasm.mjs --force         忽略缓存全部重编
//
// 每一期的配置在 scripts/episodes/<期>.mjs（自动发现）。配置里的字段：
//   root      素材根目录（默认是 meow-ai-arena 仓库；第 7 期的素材不在仓库里，写绝对路径）
//   include   要拷进编译目录的头文件所在目录（相对 root），可以是数组
//   engineShim  有 C++ 裁判模块的期才写（相对网站工程），编成 engine.mjs
//   botKind   'api'   = 选手只写 init/decide 之类的函数，配 botShim 编（第 6、7 期）
//             'stdio' = 选手写完整程序（main + 标准输入输出），用 Asyncify 把读标准输入变成"等下一回合"（第 1、2、3 期）
//   botShim   botKind 为 api 时的外壳（相对网站工程）
//   flags     这一期额外的编译选项（比如更大的栈）
//   engineFlags  裁判模块的完整编译选项（替换通用的 ENGINE_FLAGS；第 9 期的比赛引擎是带 main、读写文件的完整程序）
//   stageCopy { 暂存目录里的相对路径: 素材路径（相对 root） }：头文件里写了 "../xxx/yyy.hpp" 这种相对包含时，按同样的相对位置放一份（第 8 期的参考队）
//   bots      [{ id, src（相对 root）, defines? }]
//
// 需要 Emscripten：环境变量 EMSDK 指向 emsdk 目录，或装在 C:\tools\emsdk（Windows 默认位置）。
// 素材仓库默认在网站工程旁边的 ../meow-ai-arena，可用环境变量 MEOW_REPO 改。
// 输入先拷到系统临时目录里一个纯英文路径下再编（emcc 在 Windows 上对中文路径不太友好），产物再拷回来。
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
export const REPO = path.resolve(process.env.MEOW_REPO || path.join(ROOT, '..', '..'));
const EPISODES = fs.readdirSync(path.join(ROOT, 'scripts', 'episodes')).filter(f => /^ep\d+\.mjs$/.test(f)).map(f => f.replace('.mjs', '')).sort();

// ---- 参数 ----
const argv = process.argv.slice(2);
let jobs = 2, force = false, only = null;
const wanted = [];
for (let i = 0; i < argv.length; i++) {
  const a = argv[i];
  if (a === '-j') jobs = Math.max(1, Math.min(4, +argv[++i] || 1));      // 上限 4：机器上可能有别的任务
  else if (a.startsWith('-j')) jobs = Math.max(1, Math.min(4, +a.slice(2) || 1));
  else if (a === '--force') force = true;
  else if (a === '--only') only = new Set(argv[++i].split(','));
  else wanted.push(a);
}

// ---- 找 em++ ----
function findEmcc() {
  const roots = [process.env.EMSDK, 'C:\\tools\\emsdk', path.join(os.homedir(), 'emsdk')].filter(Boolean);
  for (const r of roots) {
    for (const name of process.platform === 'win32' ? ['em++.exe', 'em++.bat'] : ['em++']) {
      const p = path.join(r, 'upstream', 'emscripten', name);
      if (fs.existsSync(p)) return { emcc: p, emsdk: r };
    }
  }
  return { emcc: process.platform === 'win32' ? 'em++.exe' : 'em++', emsdk: null };
}
const { emcc, emsdk } = findEmcc();

function emccEnv() {
  const env = { ...process.env };
  if (emsdk) {
    env.EMSDK = emsdk;
    const extra = [];
    for (const sub of ['node', 'python']) {
      const d = path.join(emsdk, sub);
      if (!fs.existsSync(d)) continue;
      for (const v of fs.readdirSync(d)) {
        const bin = path.join(d, v, sub === 'node' ? 'bin' : '');
        if (fs.existsSync(bin)) extra.push(bin);
      }
    }
    const key = Object.keys(env).find(k => k.toLowerCase() === 'path') || 'PATH';
    env[key] = [path.join(emsdk, 'upstream', 'emscripten'), ...extra, env[key]].join(path.delimiter);
  }
  return env;
}

function run(cmd, args, cwd) {
  return new Promise((resolve) => {
    const p = spawn(cmd, args, { cwd, env: emccEnv(), shell: /\.bat$/i.test(cmd) });
    p.on('error', e => resolve({ code: -1, out: String(e) }));
    let out = '';
    p.stdout.on('data', d => (out += d));
    p.stderr.on('data', d => (out += d));
    p.on('close', code => resolve({ code, out }));
  });
}

// ---- 编译选项 ----
// 优化级别取 -O2：bot 要算得快（-Oz 小 15～25%，但搜索型 bot 慢 10～30%），-O3 比 -O2 大一些、快不了多少。
// 栈默认 8MB（原生 Linux 的默认值；有的 bot 在栈上开大数组），内存可增长。
const OPT = process.env.WASM_OPT || '-O2';
const COMMON = [OPT, '-std=c++20', '-sALLOW_MEMORY_GROWTH=1', '-sSTACK_SIZE=8388608', '-sINITIAL_MEMORY=33554432',
  '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sFILESYSTEM=0', '-sEXIT_RUNTIME=0',
  '-sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToNewUTF8,HEAPU8'];
const ENGINE_FLAGS = [...COMMON, '-sENVIRONMENT=web,worker,node',
  '-sEXPORTED_FUNCTIONS=_eng_call,_eng_state_ptr,_eng_state_size,_malloc,_free'];
const API_BOT_FLAGS = [...COMMON, '-sENVIRONMENT=worker,node',
  '-sEXPORTED_FUNCTIONS=_arena_abi,_arena_state_size,_arena_state_buf,_arena_init,_arena_decide,_arena_used_ms'];
// stdio bot：选手自己的 main。读标准输入（fd_read）换成异步的 JS 函数，没有输入时挂起，等网页送来下一回合；
// 写标准输出（fd_write）交给 JS 按行收集。见 native/common/stdio_bot.js。
const STDIO_DIR = path.join(ROOT, 'native', 'common');
const STDIO_BOT_FLAGS = [OPT, '-std=gnu++20', '-sALLOW_MEMORY_GROWTH=1', '-sSTACK_SIZE=8388608', '-sINITIAL_MEMORY=33554432',
  '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sFILESYSTEM=0', '-sEXIT_RUNTIME=1', '-sENVIRONMENT=worker,node',
  '-sASYNCIFY=1', '-sASYNCIFY_IMPORTS=["fd_read"]', '-sASYNCIFY_STACK_SIZE=1048576',
  '-sINVOKE_RUN=0', '-sEXPORTED_RUNTIME_METHODS=callMain', '-Wno-deprecated-declarations'];

const sha = (...parts) => {
  const h = createHash('sha256');
  for (const p of parts) h.update(typeof p === 'string' ? p : JSON.stringify(p)).update('\0');
  return h.digest('hex').slice(0, 16);
};

async function pool(items, n, fn) {
  let k = 0;
  const workers = Array.from({ length: Math.min(n, items.length) }, async () => {
    while (k < items.length) { const it = items[k++]; await fn(it); }
  });
  await Promise.all(workers);
}

function copyHeaders(from, to) {
  let h = '';
  for (const f of fs.readdirSync(from)) {
    if (!/\.(hpp|h)$/.test(f)) continue;
    const data = fs.readFileSync(path.join(from, f));
    fs.writeFileSync(path.join(to, f), data);
    h += sha(f, data.toString('latin1'));
  }
  return h;
}

async function buildEpisode(epId) {
  const ep = (await import(`./episodes/${epId}.mjs`)).default;
  const root = ep.root ? path.resolve(ep.root) : REPO;
  const outDir = path.join(ROOT, 'public', 'gen', epId, 'wasm');
  fs.mkdirSync(outDir, { recursive: true });
  const stage = path.join(os.tmpdir(), 'arena-wasm-build', epId);
  fs.mkdirSync(stage, { recursive: true });

  // 头文件拷进暂存目录（stdio 的期另带一份 bits/stdc++.h）
  const incDir = path.join(stage, 'include');
  fs.mkdirSync(path.join(incDir, 'bits'), { recursive: true });
  let incHash = '';
  for (const inc of [].concat(ep.include || [])) incHash += copyHeaders(path.resolve(root, inc), incDir);
  for (const [to, from] of Object.entries(ep.stageCopy || {})) {
    const data = fs.readFileSync(path.resolve(root, from));
    fs.mkdirSync(path.dirname(path.join(stage, to)), { recursive: true });
    fs.writeFileSync(path.join(stage, to), data);
    incHash += sha(to, data.toString('latin1'));
  }
  if (ep.botKind === 'stdio') {
    incHash += copyHeaders(path.join(STDIO_DIR, 'bits'), path.join(incDir, 'bits'));
    fs.copyFileSync(path.join(STDIO_DIR, 'stdio_bot.js'), path.join(stage, 'stdio_bot.js'));
    incHash += sha(fs.readFileSync(path.join(STDIO_DIR, 'stdio_bot.js'), 'utf8'));
  }
  const stampFile = path.join(outDir, 'stamps.json');
  const stamps = fs.existsSync(stampFile) ? JSON.parse(fs.readFileSync(stampFile, 'utf8')) : {};
  const { out: ver } = await run(emcc, ['--version'], stage);
  const emver = (ver.split('\n').find(l => /^(emcc|em\+\+) \(Emscripten/.test(l)) || '').trim();
  if (!emver) throw new Error('找不到 em++。请安装 emsdk 并设置 EMSDK 环境变量。\n' + ver);
  console.log(`[${epId}] ${emver}  ${OPT}`);

  if (ep.cmake) {
    const ok = await buildCmake(epId, ep, outDir, stage, stamps, stampFile, emver);
    if (!ok || !ep.bots.length) return ok;   // 第 10 期：引擎之外还有选手的指挥程序（完整程序，stdio）
  }

  const extra = ep.flags || [];
  const targets = [];
  if (ep.engineShim) {
    const engineSrc = fs.readFileSync(path.join(ROOT, ep.engineShim), 'utf8');
    targets.push({ id: 'engine', files: { 'engine_api.cpp': engineSrc }, flags: [...(ep.engineFlags || ENGINE_FLAGS), ...extra] });
  }
  const botShim = ep.botShim ? fs.readFileSync(path.join(ROOT, ep.botShim), 'utf8') : null;
  for (const b of ep.bots) {
    let src = fs.readFileSync(path.resolve(root, b.src), 'utf8');
    if (b.patch) src = b.patch(src);
    const defs = (b.defines || []).map(d => '-D' + d);
    if (ep.botKind === 'stdio') targets.push({ id: b.id, files: { 'bot.cpp': src }, flags: [...STDIO_BOT_FLAGS, ...extra, ...defs, '--js-library', '../stdio_bot.js'] });
    else targets.push({ id: b.id, files: { 'bot.cpp': src, 'bot_api.cpp': botShim }, flags: [...API_BOT_FLAGS, ...extra, ...defs] });
  }

  const failed = [];
  let built = 0, cached = 0;
  await pool(targets.filter(t => !only || only.has(t.id)), jobs, async (t) => {
    const key = sha(emver, incHash, t.files, t.flags);
    const js = path.join(outDir, t.id + '.mjs'), wasm = path.join(outDir, t.id + '.wasm');
    if (!force && stamps[t.id] === key && fs.existsSync(js) && fs.existsSync(wasm)) { cached++; return; }
    const dir = path.join(stage, t.id);
    fs.mkdirSync(dir, { recursive: true });
    for (const [name, text] of Object.entries(t.files)) fs.writeFileSync(path.join(dir, name), text);
    const t0 = Date.now();
    const r = await run(emcc, [...t.flags, '-I../include', ...Object.keys(t.files), '-o', t.id + '.mjs'], dir);
    if (r.code !== 0) { failed.push(t.id); console.log(`[${epId}] ✗ ${t.id}\n${r.out}`); return; }
    fs.copyFileSync(path.join(dir, t.id + '.mjs'), js);
    fs.copyFileSync(path.join(dir, t.id + '.wasm'), wasm);
    stamps[t.id] = key;
    built++;
    const kb = (fs.statSync(wasm).size / 1024).toFixed(0);
    console.log(`[${epId}] ✓ ${t.id}  ${kb} KB  ${((Date.now() - t0) / 1000).toFixed(1)}s` + (r.out.trim() ? '  （有警告）' : ''));
  });
  fs.writeFileSync(stampFile, JSON.stringify(stamps, null, 1));
  console.log(`[${epId}] 新编 ${built} 个，沿用缓存 ${cached} 个` + (failed.length ? `，失败 ${failed.length} 个：${failed.join('、')}` : ''));
  return failed.length === 0;
}

// 带 CMake 工程的期（第 10 期：比赛引擎 + Jolt 物理库，几百个源文件）：
//   cmake: { src: 工程目录（相对网站工程，整个拷进暂存目录）, target, defines: { 名字: 值 }, hashDirs: [参与缓存键的外部源码目录] }
// emcmake cmake -G Ninja + cmake --build；ninja 要在 PATH 里。产物 <target>.mjs / .wasm 拷成 engine.mjs / engine.wasm。
function hashDir(dir, h = createHash('sha256')) {
  for (const f of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
    const p = path.join(dir, f.name);
    if (f.isDirectory()) { if (!/^(\.git|Build|Docs|Samples|TestFramework|UnitTests|Assets|PerformanceTest|HelloWorld|JoltViewer)$/.test(f.name)) hashDir(p, h); }
    else if (/\.(cpp|h|hpp|inl|txt|cmake)$/i.test(f.name)) h.update(f.name).update(fs.readFileSync(p));
  }
  return h;
}
async function buildCmake(epId, ep, outDir, stage, stamps, stampFile, emver) {
  const c = ep.cmake;
  const src = path.join(ROOT, c.src);
  const h = hashDir(src);
  for (const d of c.hashDirs || []) hashDir(d, h);
  h.update(JSON.stringify(c.defines || {})).update(emver);
  const key = h.digest('hex').slice(0, 16);
  const js = path.join(outDir, 'engine.mjs'), wasm = path.join(outDir, 'engine.wasm');
  if (!force && stamps.engine === key && fs.existsSync(js) && fs.existsSync(wasm)) { console.log(`[${epId}] 沿用缓存 engine`); return true; }
  const sdir = path.join(stage, 'src'), bdir = path.join(stage, 'build');
  fs.rmSync(sdir, { recursive: true, force: true });
  fs.cpSync(src, sdir, { recursive: true });
  const emcmake = path.join(path.dirname(emcc), process.platform === 'win32' ? 'emcmake.exe' : 'emcmake');
  const t0 = Date.now();
  if (!fs.existsSync(path.join(bdir, 'build.ninja'))) {
    fs.mkdirSync(bdir, { recursive: true });
    const defs = Object.entries(c.defines || {}).map(([k, v]) => `-D${k}=${v}`);
    const r = await run(emcmake, ['cmake', '-G', 'Ninja', '-S', sdir, '-B', bdir, '-DCMAKE_BUILD_TYPE=Distribution', ...defs], stage);
    if (r.code !== 0) { console.log(`[${epId}] ✗ cmake 配置失败\n${r.out}`); return false; }
  }
  const r = await run('cmake', ['--build', bdir, '--target', c.target, '-j', String(jobs)], stage);
  if (r.code !== 0) { console.log(`[${epId}] ✗ ${c.target}\n${r.out.slice(-6000)}`); return false; }
  fs.copyFileSync(path.join(bdir, c.target + '.mjs'), js);
  fs.copyFileSync(path.join(bdir, c.target + '.wasm'), wasm);
  stamps.engine = key;
  fs.writeFileSync(stampFile, JSON.stringify(stamps, null, 1));
  console.log(`[${epId}] ✓ engine  ${(fs.statSync(wasm).size / 1024).toFixed(0)} KB  ${((Date.now() - t0) / 1000).toFixed(1)}s`);
  return true;
}

let ok = true;
for (const ep of wanted.length ? wanted : EPISODES) ok = (await buildEpisode(ep)) && ok;
process.exit(ok ? 0 : 1);
