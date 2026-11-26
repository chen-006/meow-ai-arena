# 第 11–12 期 3D 场景与资源管线

网页版的 3D 战场直接复用了正片 Blender 渲染器的搭景结果（[`../../src/games/boomerang/r3d/`](../../src/games/boomerang/r3d/) 移植自正片渲染器）。为避免把 31.7 MB 的二进制网格包入库，这里提供完整导出与打包脚本。

| 产物 | 来源 | 脚本 |
|---|---|---|
| `../data/ep11_r3d/scenes/<图>.json + .bin.br + .webp`（第 11 集 10 张图） | `../../../../boomerang/renderer/`（Blender 无界面跑 `env.build_env`） | `export_scene.py` → `pack_scenes.py` → `br_bins.mjs` |
| `../data/ep11_r3d/scenes/<图>.*`（第 12 集 `caldera` / `piers` / `keeps`） | `../../../renderer/`（含岩浆/海水液面、发光地缝、双堡队旗与队色占位） | `export_scene12.py` → `pack_scenes.py` → `br_bins.mjs` |
| `../data/ep11_r3d/vox/<角色>.json`（13 位体素小人，已入库） | `../../../renderer/src/voxchars/`（`voxkit.Model.to_json()`） | `../../../renderer/src/export_voxchars.py` |
| `../../public/img/ep11/maps/<图>.webp`（13 张选图缩略图，已入库） | 网站自己的 3D 画面截图 | `thumbs.mjs`（需先启动 `npm run dev` 或 `npm run preview`） |
| 第 11 集 4v4 精选回放 | 同份 WASM 引擎 + 海选原版程序现场跑 | `gen_4v4.mjs`（依赖 `node-arena.mjs`） |

## 生成命令

在 `boomerang-team/web-play/` 目录执行（需要 Blender 5.2 及渲染器 Python 依赖 Pillow / numpy）：

```bash
# 1. 导出第 11 集 10 张图（环境变量 EP11_RENDERER 可覆盖默认路径 ../../boomerang/renderer）
blender -b --factory-startup -P scripts/ep11-r3d/export_scene.py -- _scene_raw

# 2. 导出第 12 集 3 张 5v5 图（环境变量 EP12_RENDERER 可覆盖默认路径 ../renderer）
blender -b --factory-startup -P scripts/ep11-r3d/export_scene12.py -- _scene_raw caldera piers keeps

# 3. 转 WebP 地面贴图并写入 scripts/data/ep11_r3d/scenes/
python scripts/ep11-r3d/pack_scenes.py _scene_raw

# 4. 用 Brotli 压缩 .bin → .bin.br
node scripts/ep11-r3d/br_bins.mjs

# 5. 同步到 public/gen/ep11/scenes/
npm run build:data
```
