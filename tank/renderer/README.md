[English](README_en.md)

# 渲染器（坦克大战）

把一局坦克大战回放渲成 1920×1080、30 帧的 mp4：左边棋盘（砖墙、钢墙、水、三种坦克、子弹、星星、基地，带小步补间和爆炸、中弹、升级特效），右边比分栏（双方基地血、每辆车的血量和等级、击毁 / 吃星）。视频第 6 集里的对局画面就是它画的。

## 1. 要装什么

| 东西 | 版本 | 说明 |
|---|---|---|
| Python | 3.10 以上（测试过 3.12） | `pip install pillow`（测试过 Pillow 12.3.0） |
| ffmpeg | 任意较新版本（测试过 7.1） | 合成 mp4。放进 PATH，或用环境变量 `ARENA_FFMPEG` 指定。只出静帧时不需要 |
| 中文字体 | 默认 Windows 的 Noto Sans SC（`NotoSansSC-VF.ttf`），没有就用微软雅黑 | 字体不随仓库提供；别的字体用环境变量 `ARENA_FONT` 指定（Linux、macOS 上找不到 Noto Sans CJK / 苹方时要指定） |

只在 Windows 11 上测过。

## 2. 出片

在 `tank/` 下：

```text
python renderer/src/make_video.py <回放> [-o 输出.mp4] [--from 秒] [--to 秒] [--fps 30] [--still 回合]
       [--tps 2.2] [--slow 1.0] [--style A|B|C]
python renderer/src/make_video.py replays/final-qwen-vs-glm.html -o qwen-vs-glm.mp4
python renderer/src/make_video.py replays/final-gemini-vs-opus.html --still -1 -o 终局.png
```

Windows 上也可以用 `renderer\出片.cmd <回放> [同样的选项]`。

- **输入**：仓库 [`replays/`](../replays/) 里的 `.html`（内嵌的是 `arena.py expand` 展开的逐帧数据 `tank-frames-1`），或比赛工具存的原始回放 `tank-replay-1`（`.json` / `.json.gz`）。渲染器从起始局面和每回合命令用 `tsim.py`（照 `workspace/tank.hpp` 的 `step()` 移植）重算每个小步；读 `.html` 时还会和里面记的逐帧局面逐回合核对，对不上就报错。仓库里 15 份回放都核对通过。
- 不带 `-o` 时输出到 `renderer/renders/<回放名>/`（不入库；`ARENA_WORK` 可改）。
- 平时每秒 `--tps` 回合，有车被毁、吃星、终局的回合前后放慢到 `--slow`；末尾停 1 秒。`--from/--to` 是成片里的秒数。`--still` 出一张 PNG（回合可以是小数，`-1` = 终局）。
- `--style`：`C` 解说版（默认，视频用的）、`A` 扁平、`B` 复古像素。
- 约 0.3 秒一帧（单进程），一局 50 回合的比赛约 25 秒成片，几分钟出完。

## 3. 和视频正片的差别

- 头像和视频里一样，放在 `renderer/assets/avatars/`；想换成自己的头像，把 `<名字>.png` 放进一个目录，用环境变量 `ARENA_AVATAR_DIR` 指过去。没有头像的选手画代表色圆加首字。
- 出片的版式（标题、棋盘、侧栏的位置）是渲染器自带的简单版式；视频里的镜头推拉、局部放大、字幕、配音、音乐、音效是剪辑时另加的，不在渲染器里。

## 4. 已知限制

- `tsim.py` 只支持本届比赛用到的参数组合（`base_armor`、`base_regen`、`proto_*` 必须为 0）。
- 坦克在规则里没有朝向，画面上的朝向按"最近一次移动或开炮的方向"推出来。

## 5. 文件

```text
renderer/
  出片.cmd  THIRD_PARTY_NOTICES.md
  assets/avatars/   视频里用的选手头像
  src/
    make_video.py   出片入口（版式、播放速度、ffmpeg）
    tboard.py       回放读取（含 tank-frames-1 → tank-replay-1 还原）、棋盘、比分栏
    tsim.py         结算，逐行照 tank.hpp 移植，记下两个小步的轨迹
    common.py       字体、ffmpeg、头像查找、从回放网页里取数据
```
