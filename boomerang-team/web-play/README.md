# 第 11～12 期网页可玩版前端（回旋镖大作战 / OA 争霸）

本目录收录第 11 集（海选 1v1 / 4 人混战 / 4v4）与第 12 集（5v5 团队赛：歼灭、占点、刺王）的浏览器可玩版前端源码、WebAssembly 裁判与 Bot 桥接、3D/2D 战场渲染器以及数据与资源构建脚本。

主要功能：

- **真人对战与定级**：WASD 移动、鼠标瞄准、左键投镖、右键挥砍/飞踢、空格冲刺，支持 1v1、4 人混战、4v4、5v5 三赛制及 Bradley–Terry 多局定级。
- **程序互打实验台**：在浏览器内用原版 C++ 裁判引擎（`engine.wasm`）和全部 26 份程序（13 份第 11 集海选版、10 份第 12 集团队终版、3 档陪练，均编译为 Asyncify WebAssembly）实时对打，并内置赛后对照实验预设。
- **3D / 2D 战场与名场面回放**：支持正片同款 Three.js 体素 3D 渲染与 2D 画布视图，可开关 0.2 秒 AI 延迟残影、慢放、单步与拖动。
- **赛事总览与矩阵探索**：第 12 集 6 回合 3×3“如果当时”矩阵探索器、10 人贡献榜、终版回炉升降榜、第 11 集 13 人海选榜与暗标拍卖复盘。

> 本目录只收录第 11～12 期回旋镖相关的前端源码、小体积静态元数据/体素/缩略图及构建脚本，**不包含**网站其他期内容、线上部署配置及依赖排位赛服务端的「挑战玩家 Bot」联机入口（保证纯离线构建与离线可玩）。大体积可再生构建产物（3D 场景网格包 `.bin.br` 约 31.7 MB、编译产物 `.wasm` 约 8 MB）未入库，下方说明如何用脚本重新生成。

## 目录结构

| 路径 | 内容 |
|---|---|
| `ep11/{index,play,watch,replay,source}.html` | 5 个前端页面入口（总览、真人对战/定级、程序互打、精选回放、源码浏览） |
| `src/games/boomerang/` | 游戏前端核心：`arena-page.ts`、`engine.ts`、`match.ts`、`human.ts`、`view.ts`、`viewer2d.ts`、`bt.ts`、`data.ts`、`sfx.ts`、`teams.ts`、`r3d/*.js`（Three.js 3D 场景与体素小人/特效/液面/HUD） |
| `src/pages/`、`src/episodes/ep11.ts` | 期总览页（含 3×3 矩阵交互与榜单）与源码高亮浏览页 |
| `src/common/`、`src/i18n/zh/` | 对局页外壳、Web Worker（`stdio-host.ts` / `stdio-worker.ts`）、WASM 加载器与中文文案 |
| `native/boomerang/engine_api.cpp` | WebAssembly 裁判外壳（直接封装 `../../workspace/dart.hpp`，支持 Mode 0..5 与 0.2s 延迟残影） |
| `native/common/` | 完整程序型 Bot 的 Emscripten Asyncify 标准输入输出桥接（`stdio_bot.js`、`bits/stdc++.h`） |
| `scripts/build-wasm.mjs`、`scripts/episodes/ep11.mjs` | 直接从本仓库 `../../boomerang/` 与 `../../boomerang-team/` 编译 `engine.wasm` 与全部 26 个 Bot WASM |
| `scripts/data/ep11.mjs` | 生成 `public/gen/ep11/` 静态数据（`data.json`、13 张地图、13 位体素小人、12 局精选回放、规则与源码文本、场景包） |
| `scripts/data/ep11_src/`、`scripts/data/ep11_r3d/vox/` | 榜单/矩阵/拍卖元数据 JSON、12 局精选压缩回放、13 位体素小人 JSON |
| `scripts/ep11-r3d/` | 从正片 Blender 渲染器导出并压缩 13 张图 3D 场景包的管线脚本（`export_scene.py`、`export_scene12.py`、`pack_scenes.py`、`br_bins.mjs`、`thumbs.mjs`、`node-arena.mjs`、`gen_4v4.mjs`） |
| `public/` | 图标、极简主题外壳、首屏主视觉图、17 张角色/陪练头像与 13 张地图缩略图 |

## 本地构建与运行

### 1. 环境准备

- **Node.js 20+**（运行 Vite 前端与数据构建脚本）
- **Emscripten（emsdk）**（仅编译 `.wasm` 时需要，安装后设置环境变量 `EMSDK` 或将 `em++` 加入 `PATH`）
- **Blender 5.2 + Python（Pillow / numpy）**（可选，仅重新生成 3D 场景网格包时需要）

### 2. 安装前端依赖并生成静态数据

```bash
cd boomerang-team/web-play
npm ci           # 或 npm install
npm run build:data
```

`npm run build:data` 会从本仓库的 `../../boomerang/`、`../../boomerang-team/` 和本目录的 `scripts/data/` 生成 `public/gen/ep11/`（含 `data.json`、13 张地图、13 位体素小人、12 局精选回放以及规则和 26 份源码文本）。若尚未导出 3D 场景包，脚本会跳过 `scenes/` 并给出提示，此时 2D 视图与页面榜单/源码均可正常使用。

### 3. 编译引擎与 26 份程序为 WebAssembly

安装好 Emscripten（`emsdk`）后执行：

```bash
npm run build:wasm
# 仅编译引擎与少量对手以加速体验：
node scripts/build-wasm.mjs ep11 --only engine,weak,mid,strong,astra,astra-t,fable-t
```

产物输出到 `public/gen/ep11/wasm/`（`engine.mjs` / `engine.wasm` 以及各 `<id>.mjs` / `<id>.wasm`）。编译时会自动读取本仓库的 `boomerang-team/workspace/dart.hpp`、第 11 集 13 份海选程序、第 12 集 10 份团队终版（对 `astra-t` 自动应用 2 行四人局国王索引兼容补丁，与 `../analysis/experiments/patches/wave3_astra_ffa.diff` 一致）及三档陪练源码。

### 4. 启动本地开发服务器或打包静态站点

```bash
npm run dev      # 默认监听 http://localhost:4374/ep11/
npm run build    # TypeScript 检查 + Vite 打包输出至 dist/
```

### 5. 可选：重新生成 3D 场景包（`scripts/data/ep11_r3d/scenes/`）

为控制仓库体积，13 张地图的高精度 3D 场景网格包（共约 31.7 MB 的 `.bin.br` / `.json` / `.webp`）未提交入库。装好 Blender 5.2 与渲染器依赖（见 [`../renderer/README.md`](../renderer/README.md) 和 [`../../boomerang/renderer/README.md`](../../boomerang/renderer/README.md)）后，可按 [`scripts/ep11-r3d/README.md`](scripts/ep11-r3d/README.md) 的命令从正片渲染器完整重新生成：

```bash
# 1) 导出第 11 集 10 张图（依赖 ../../boomerang/renderer）
blender -b --factory-startup -P scripts/ep11-r3d/export_scene.py -- _scene_raw

# 2) 导出第 12 集 3 张 5v5 图（依赖 ../../boomerang-team/renderer）
blender -b --factory-startup -P scripts/ep11-r3d/export_scene12.py -- _scene_raw caldera piers keeps

# 3) 打包贴图与网格，并用 Brotli 压缩 .bin → .bin.br
python scripts/ep11-r3d/pack_scenes.py _scene_raw
node scripts/ep11-r3d/br_bins.mjs

# 4) 重新同步到 public/gen/ep11/scenes/
npm run build:data
```

## 许可与署名

本目录源码与自制素材采用仓库统一的 [MIT 许可](../../LICENSE)。第三方前端依赖（`three`、`vite`、`typescript` 等）由 `npm install` 按各自开源许可安装，不打包入库。

角色形象署名：**角色形象设计参考：小叶子XIYZ；美术和制作：喵了个猫了个咪了个猫**。

例外：`public/img/avatars/ep11/doubao.webp`（豆包）是照豆包官方公开形象做的写实风角色，官方形象本身的权利属于其权利人，这张图不适用本仓库的 MIT 许可（与 [`../../boomerang/renderer/assets/avatars/NOTICE.md`](../../boomerang/renderer/assets/avatars/NOTICE.md) 的说明一致）。角色名里的模型名和公司名属于各自的权利人，只用来说明角色对应哪个模型。
