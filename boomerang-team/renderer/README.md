# 第 12 集 5v5 团队赛渲染器

把第 12 集 5v5 团队赛回放（`DART-REPLAY 2`，含 `MODE 3` 歼灭 / `MODE 4` 占点 / `MODE 5` 刺王）渲染成 1080p 30 帧 mp4：三张全新主题浮岛（火山口岩浆湖、三座栈桥海港、双堡石城）、10 名体素小人、队伍色外描边与脚底光盘、占点进度圈与复活光柱、国王像素金王冠与掉命心碎特效、5v5 双队信息栏。

## 1. 环境准备

| 依赖 | 建议版本 | 说明 |
|---|---|---|
| Blender | 5.2.4 LTS（已验证） | 后台 EEVEE 渲染（`-b`），无需安装额外插件。加入 `PATH`，或设置环境变量 `DART_BLENDER` 指向 `blender.exe`。 |
| Python | 3.10+，安装 `numpy`、`scipy`、`scikit-image`、`pillow` | 运行出片总控、地图预处理与 PIL 信息栏。可用环境变量 `DART_PYTHON` 指定解释器。 |
| ffmpeg | 7.x（或任意较新版本） | 合成 mp4，加入 `PATH` 或通过 `DART_FFMPEG` 指定。 |
| 中文字体 | 默认 `C:\Windows\Fonts\NotoSansSC-VF.ttf` | 信息栏文字用，可用环境变量 `DART_FONT` 指定任意支持中文的 `.ttf/.otf`。 |

- 信息栏头像默认复用仓库内 [`../../boomerang/renderer/assets/avatars/`](../../boomerang/renderer/assets/avatars/NOTICE.md)（完整克隆本仓库即可开箱即用，也可通过 `DART_AVATARS` 指定目录）。
- 中间文件（地形缓存、逐帧 PNG、日志）默认写入 `renderer/work/`（已加入 `.gitignore`），可通过 `DART_WORK`、`DART_CACHE` 自定义。

## 2. 一条命令出 5v5 视频

在 `boomerang-team/` 目录下运行：

```bash
python renderer/render_game.py replays/R1_g366_astra_1v5.txt -o R1_g366.mp4
python renderer/render_game.py replays/R4_g587_photo_finish_005.txt -o R4_g587.mp4 --procs 3 --samples 12
python renderer/render_game.py replays/R6_g1336_kings_duel.txt -o R6_g1336.mp4
```

也可以直接用 Windows 批处理或 `src/make_video.py`：

```bash
renderer\出片.cmd replays\R5_g1662_glm_kick_king.txt -o R5_g1662.mp4 --procs 3 --samples 12
```

- **模式自动识别**：从回放头部的 `MODE 3/4/5`、`POINTS`、`KING` 自动切换歼灭 / 占点 / 刺王视觉元素。
- **地图主题自动匹配**：`caldera` → 火山口岩浆、`piers` → 三座栈桥海港、`keeps` → 双堡石城；可用 `--theme <名字>` 覆盖。首次渲染某张图会自动做数分钟地形与贴图预处理，并按地图指纹缓存在 `renderer/work/cache/`。
- **常用参数**：`-o 输出.mp4`、`--procs 3`（并行 Blender 进程数）、`--samples 12`（EEVEE 采样数）、`--frames a:b`（仅渲片段）、`--roster ...`（手动指定 10 个造型名）、`--no-hud`、`--keep`（保留中间帧）。中断后重跑同一命令可自动跳过已渲帧。

没装 Blender 时，可以先确认回放和信息栏没问题（只需要 Python 和 pillow，几秒钟）：

```bash
cd renderer/src
python -c "import replay, timeline, hud; rp = replay.load('../../replays/R6_g1336_kings_duel.txt'); print(rp.T, timeline.Timeline(rp).n)"
```

## 3. 出关键时刻静帧

```bash
python renderer/src/stills12.py replays/R6_g1336_kings_duel.txt start@40 duel@330 finish@final --samples 32
```

或使用 `renderer\出静帧12.cmd`。时刻支持游戏步号、`final`、`capture:i`、`respawn:i`、`life:i`、`kill:i`、`f<帧号>`。

## 4. 三种模式的视觉元素

| 模式 | 场上表现 | 顶部信息栏 |
|---|---|---|
| **歼灭（火山口）** | 队伍色描边与脚盘、15 秒起逐波岩浆塌陷、掉进岩浆湖溅起火星、终结慢放与近景推镜 | 左右 5 人头像卡 + 中央存活比分 `5 : 5` |
| **占点（三座栈桥）** | 地面 A/B/C 三据点圆圈与翻点进度弧、悬空点名牌、阵亡位置倒计时环、复活光柱落地动画 | 左右 5 人头像卡（含复活倒计时）+ 中央实时积分、倒计时与 A/B/C 归属指示 |
| **刺王（双堡）** | 国王头顶像素金王冠（含队伍色宝石，出局时随头部切开）、头顶剩余心数、破命金色冲击环与无敌闪烁 | 国王头像卡标金冠 + 中央双王心数对比 + 破命/斩王播报 |

## 5. 目录结构

| 路径 | 内容 |
|---|---|
| `render_game.py` | 一条命令出片入口（自动映射 10 位选手造型与顶栏名字） |
| `出片.cmd`、`出静帧12.cmd` | Windows 快捷脚本 |
| `src/` | 全部渲染代码（含 5v5 扩展：`modes_bl.py`、`props12.py`、`liquid.py`、`stills12.py`、`replay.py`、`pose.py`、`timeline.py`、`hud.py`、`render_bl.py`、`voxchars/` 等） |
| `maps/` | 三张定稿团队赛地图（`caldera.txt`、`piers.txt`、`keeps.txt`） |
| `THIRD_PARTY_NOTICES.md` | 第三方依赖说明（不打包 Blender / ffmpeg / Python 库） |

## 6. 许可与署名

本目录代码与程序化生成的体素小人、场景均沿用仓库 [MIT 许可](../../LICENSE)。

角色形象署名：**角色形象设计参考：小叶子XIYZ；美术和制作：喵了个猫了个咪了个猫**。
