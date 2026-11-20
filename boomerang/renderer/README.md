[English](README_en.md)

# 渲染器

把一份对局回放（`DART-REPLAY 2`，格式见 [../workspace/回放格式.md](../workspace/回放格式.md)）渲染成 1080p 30 帧的 mp4：浮在云海上的十个主题场地、体素（方块）小人、三叶回旋镖、命中和塌陷特效、顶部信息栏。视频里的对局画面都是它出的。

## 1. 要装什么

| 东西 | 版本 | 说明 |
|---|---|---|
| Blender | 5.2.4 LTS（测试过的版本） | EEVEE 渲染，后台模式 `-b`，不用装插件。放进 PATH，或用环境变量 `DART_BLENDER` 指到 `blender.exe`。渲染脚本在 Blender 自带的 Python 里跑，只用它自带的 numpy |
| Python | 3.10 以上，装 `numpy`、`scipy`、`scikit-image`、`pillow` | 跑出片总控、地图预处理（地形网格、地面贴图）、信息栏。`render_game.py` 用当前的 Python；`出片.cmd` 用 PATH 里的 `python`，别的解释器用 `DART_PYTHON` 指定 |
| ffmpeg | 较新版本（测试过 7.x） | 合成 mp4，放进 PATH |
| 中文字体 | 默认 `C:\Windows\Fonts\NotoSansSC-VF.ttf`（可变字重的 Noto Sans SC） | 信息栏用。别的字体用 `DART_FONT` 指定 |

测试过的环境是 Windows 11 + RTX 4060 笔记本显卡。中间文件（地形缓存、渲染帧、日志）默认写到 `renderer/work/`（不入库），`DART_WORK`、`DART_CACHE` 可以改。

## 2. 出片（一条命令）

```text
python renderer/render_game.py replays/g428.txt -o g428.mp4
```

在 `boomerang/` 下运行。`render_game.py` 按回放里的选手名（比赛时的席位名）自动选体素小人、写顶栏名字，再调用 `src/make_video.py`；其余参数原样传过去：

| 参数 | 说明 |
|---|---|
| `-o 输出.mp4` | 不给时输出到 `renderer/work/<回放名>/<回放名>.mp4` |
| `--procs 3` | 同时开几个 Blender 进程（默认 3，每个 4 线程）。3 个以后基本不再变快 |
| `--samples 12` | EEVEE 采样数（默认 12；视频里的纯享版用的是 6） |
| `--frames a:b` | 只渲第 a～b 帧（出小样用） |
| `--roster 甲,乙,…` | 按座位号手动指定角色（见下面的名单） |
| `--theme 名字` | 强制用某个场景主题（默认按地图名） |
| `--no-hud` | 不画信息栏 |
| `--chars smooth` | 用早期的圆润小人代替体素小人 |
| `--keep` | 保留中间帧 |

**中断续渲**：原样再跑同一条命令，已经渲好的帧会跳过，写了一半的帧会删掉重渲。实测 1 个进程 6 采样约 2.5 秒/帧（含每个进程启动时建场景、编译着色器的十几秒）；视频里的纯享版 3 个进程 1080p 约 0.29 秒/帧，一局 20 秒的单挑大约 3 分钟。

不经 `render_game.py`：`出片.cmd <回放> [-o 输出.mp4] [--roster …]`，或 `python renderer/src/make_video.py <回放> …`。

**角色名单**（`--roster` 用的名字）：`astra sol fable opus gemini gemini_s grok glm deepseek kimi qwen mimo doubao`。第 11 集里 Gemini 4 Carbon 用 `gemini`、Gemini 4 Hydrogen 用小一号的 `gemini_s`，顶栏名字由环境变量 `DART_LABELS="gemini=Carbon,gemini_s=Hydrogen"` 改（`render_game.py` 会自动设）。不给 `--roster` 时，2 人局默认 Opus、Astra，4 人局 Opus、Astra、DeepSeek、Grok。

## 3. 出静帧

```text
出静帧.cmd [ffa team team2 closeup win collapse lineup themes maps one:<地图名>]
```

或 `python renderer/src/stills.py …`。`lineup` 出全体角色阵容图，`themes` 出十张地图各一张主题全景（约 10 分钟），`one:<地图名>` 只出一张，`ffa`、`team` 等是定妆照（用 `samples/` 里主办方做游戏时的示例回放）。输出在 `renderer/work/stills/`。`compare`、`compare_chars` 要用开发时的旧定妆照和两张外部参考图，这些不在仓库里，跑不了。

体素小人导出成通用素材（`.glb` + 方块数据 `.json` + 透明 PNG，仓库顶层 [characters/boomerang/](../../characters/boomerang/README.md) 就是这样导出的）：

```text
blender -b --factory-startup -P renderer/src/export_voxchars.py -- <输出目录> [astra,sol,...]
```

## 4. 它是怎么工作的

回放每秒 20 步，只给位置、朝向、状态。`pose.py` 把每个角色、每把镖在任意时刻（可以是小数步）的样子写成游戏时间的纯函数：位置直线插值，姿势由"当前状态 + 前后几步的事件"决定，预备动作利用回放已知的未来提前起势，收势按动画自己的节奏放长。`timeline.py` 决定成片每一帧对应哪个游戏时间（正常每帧 2/3 步，击杀顿帧，最后一击慢放）和镜头。因为每一帧都能单独算出来，渲染可以任意切段并行、从任意一帧续渲。Blender 这边每帧渲染前的回调把所有东西摆到位。

| 文件 | 作用 |
|---|---|
| `src/replay.py` | 读回放（宽高、格子边长都从回放里读；按座位整理事件） |
| `src/pose.py` | 回放 → 每一刻的姿势、镖的状态、拖尾、特效时刻 |
| `src/timeline.py` | 成片的逐帧时间表：游戏时间、顿帧、慢放、镜头、震动 |
| `src/terrain.py`、`ground_paint.py`、`prep.py` | 浮岛网格（带符号距离场 + marching cubes）、塌陷碎块、地面贴图；按地图缓存 |
| `src/env.py`、`props.py`、`scene_lib.py`、`geom.py` | 场景总装：岛体、石头、高草、树、房子、云、瀑布、灯光 |
| `src/themes.py` | 十个地图主题（配色、障碍物造型、地面画法、远景、飘落物、光照）和"地图名 → 主题" |
| `src/voxkit.py`、`voxchars/<名字>.py` | 体素小人的积木和每个角色的定义（一人一个文件） |
| `src/chars_voxel.py`、`chars.py`、`cast.py` | 体素小人的装配和逐帧摆放（被镖命中拦腰切成两半）、早期圆润小人、角色名单和头像裁切 |
| `src/fx.py` | 特效：命中星爆、冲击环、火花、刀光、尘土、纸屑 |
| `src/hud.py` | 信息栏、击杀播报、塌陷预警、入场名牌、胜者横幅（PIL） |
| `src/render_bl.py` | Blender 入口：建场景、逐帧摆放并渲染 |
| `src/make_video.py` | 出片总控：地图预处理 → 分段并行渲染 → 信息栏 → ffmpeg |
| `src/demo.py` | 演示模式：用一份"导演表"代替自动时间线和镜头（视频里讲规则的镜头用它） |
| `src/stills.py`、`voxpreview.py`、`voxsheet.py`、`export_voxchars.py`、`mapfile.py` | 静帧、角色预览和对照表、导出体素小人、只有地图没有回放时生成开局回放 |

**主题**：每张地图一个，`twinlakes` 草甸、`yinyang` 春樱、`stonecircle` 石阵荒原、`skybridge` 晚霞、`ruins` 古垣、`icefloes` 冰原、`hourglass` 沙洲、`canyon` 红岩峡谷、`oldtown` 老城、`forest` 深林。新地图不用改代码，渲染完全由回放里的地图数据驱动。

## 5. 目录

| 路径 | 内容 |
|---|---|
| `render_game.py` | 第 11 集回放一条命令出片 |
| `出片.cmd`、`出静帧.cmd` | Windows 下的快捷入口 |
| `src/` | 全部代码 |
| `maps/` | 十张地图的定稿文件（格子布局和 `workspace/maps/`、`tournament/hidden/` 里的逐格相同，文件头多了标题、主题行）、`定稿说明.md`（对称方式、公平性数据）、`总览_定稿十图.png` |
| `samples/` | 主办方做游戏时的四份示例回放（`stills.py` 的定妆照用） |
| `assets/avatars/` | 信息栏头像的源图，见同目录 [NOTICE.md](assets/avatars/NOTICE.md) |

## 6. 素材与许可

- 地形、石头、草、花、树、云、瀑布、体素小人、镖、特效全部由脚本程序化生成，没有用外部模型、贴图或素材包；体素小人不用各家公司的商标图形。
- 依赖见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)，都不打包。
- 代码和体素小人沿用仓库 [MIT 许可](../../LICENSE)。角色形象设计：小叶子XIYZ（B 站）；美术和制作：喵了个猫了个咪了个猫。

## 和制作时的差别

源码里主办方电脑上的绝对路径（Blender、中间文件、头像、示例回放、立绘）换成了相对仓库的默认值和环境变量 `DART_BLENDER`、`DART_WORK`、`DART_CACHE`、`DART_AVATARS`、`DART_FONT`、`DART_PORTRAITS`，其余没改；逐个文件的哈希见 [../source_manifest.json](../source_manifest.json)。`render_game.py` 是开源时加的（做视频时用的是主办方的批量渲染脚本，按同样的名单调 `make_video.py`）。从内部候选图里挑定稿地图的一次性脚本 `finalize_maps.py`、网页版查看器、开发时的备份没有收录。
