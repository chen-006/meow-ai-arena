# 第 11 集《回旋镖大作战》的体素小人

[English below](#english)

第 11 集对局画面里的 13 个体素（方块）小人，照 [正式形象 v2](../../art/v2/README.md) 的立绘做（Astra、Sol 照第 10 集那一套立绘，豆包是节目组另做的）。和上一层 `characters/` 里第 10 集《纸上谈兵》当主帅用的那一套不是同一套积木：这一套更小、更 Q，带三套表情和"被镖劈成两半"的切面，定义和渲染代码都在 [boomerang/renderer/src/](../../boomerang/renderer/README.md)（`voxkit.py`、`voxchars/<名字>.py`）。先看 [总览.jpg](总览.jpg)。

| 名字 | 角色 | 第 11 集谁用 |
|---|---|---|
| `astra` | GPT 大姐 Astra | GPT-6.2 Astra |
| `sol` | GPT 小妹 Sol | GPT-6.2 Sol |
| `fable` | Claude Fable | Claude Fable 5.2 |
| `opus` | Claude Opus | Claude Opus 5.5 |
| `gemini` | Gemini | Gemini 4 Carbon |
| `gemini_s` | 小 Gemini | Gemini 4 Hydrogen |
| `grok` | Grok | Grok 4.7 |
| `glm` | GLM | GLM 5.3 |
| `deepseek` | DeepSeek | DeepSeek-V4.1-Flash |
| `kimi` | Kimi | Kimi K3.1 Pro |
| `qwen` | 千问 | 千问 Qwen4-Max |
| `mimo` | MiMo | MiMo V2.6 Pro |
| `doubao` | 豆包 | 豆包 Seed-Evolving |

每人四个文件：`<名字>.glb`（每个可动部件是独立节点、带顶点色，节点原点在关节上）、`<名字>.json`（方块数据，`voxkit.from_json` 可以读回）、`<名字>_正面.png`、`<名字>_斜45.png`（1024×1024 透明底预览，带描边）。它们是用 `blender -b --factory-startup -P boomerang/renderer/src/export_voxchars.py -- <输出目录>` 从渲染器里的定义导出的，开源时重新导出了一遍。

和仓库其他部分一样以 [MIT 许可](../../LICENSE) 发布。署名：角色形象设计参考：小叶子XIYZ（B 站）；美术和制作：喵了个猫了个咪了个猫。体素小人不使用各家公司的商标图形；角色名里的模型名和公司名属于各自的权利人。

## English

The 13 voxel characters used in episode 11 (*Boomerang Brawl*), built from the [v2 character art](../../art/v2/README.md). This is a different kit from the episode-10 set one level up: smaller and more chibi, with three face sets and a "sliced in half" cut used when a boomerang hits. Definitions and rendering code live in [boomerang/renderer/src/](../../boomerang/renderer/README_en.md). Files per character: `.glb` model (one node per movable part, vertex colours, pivots at the joints), `.json` voxel data (readable with `voxkit.from_json`), and two 1024×1024 transparent previews. Exported with `export_voxchars.py`. MIT licensed; credit: Character design reference: 小叶子XIYZ (Bilibili); art and production: 喵了个猫了个咪了个猫.
