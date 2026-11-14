[English](README_en.md)

# 渲染器（夺旗）

把一局夺旗回放渲成 1920×1080、30 帧的 mp4：左边棋盘（墙、各方基地、中立旗、每方 3 个单位，带移动补间、攻击、拾旗、丢旗、得分特效），右边比分栏（各方得分和手里的旗）。视频第 3 集里的对局画面就是它画的。

## 1. 要装什么

| 东西 | 版本 | 说明 |
|---|---|---|
| Python | 3.10 以上（测试过 3.12） | `pip install pillow`（测试过 Pillow 12.3.0） |
| ffmpeg | 任意较新版本（测试过 7.1） | 合成 mp4。放进 PATH，或用环境变量 `ARENA_FFMPEG` 指定。只出静帧时不需要 |
| 中文字体 | 默认 Windows 的 Noto Sans SC（`NotoSansSC-VF.ttf`），没有就用微软雅黑 | 字体不随仓库提供；别的字体用环境变量 `ARENA_FONT` 指定（Linux、macOS 上找不到 Noto Sans CJK / 苹方时要指定） |

只在 Windows 11 上测过。

## 2. 出片

在 `ctf/` 下：

```text
python renderer/src/make_video.py <回放> [-o 输出.mp4] [--from 秒] [--to 秒] [--fps 30] [--still 回合] [--tps 4]
python renderer/src/make_video.py replays/2p-astra-vs-sol.html -o astra-vs-sol.mp4 --to 20
python renderer/src/make_video.py replays/8p-melee.html --still 150 -o 第150回合.png
```

Windows 上也可以用 `renderer\出片.cmd <回放> [同样的选项]`。

- **输入**：仓库 [`replays/`](../replays/) 里的 `.html`（查看器和回放打包在一起，渲染器取出里面的回放数据），或比赛工具存的回放 `.json` / `.json.gz`（`header` + 逐回合 `frames`）。仓库里 11 份回放都能读。
- 不带 `-o` 时输出到 `renderer/renders/<回放名>/`（不入库；`ARENA_WORK` 可改）。
- 匀速每秒 `--tps` 回合（默认 4；一局 400 回合约 100 秒成片），末尾停 1 秒。`--from/--to` 是成片里的秒数。`--still` 出一张 PNG（回合可以是小数，`-1` = 终局）。
- 约 0.1 秒一帧（单进程）。

## 3. 和视频正片的差别

- 头像和视频里一样，放在 `renderer/assets/avatars/`；想换成自己的头像，把 `<名字>.png` 放进一个目录，用环境变量 `ARENA_AVATAR_DIR` 指过去。没有头像的选手画代表色圆加首字。
- 出片的版式（标题、棋盘、侧栏的位置）是渲染器自带的简单版式；视频里的镜头推拉、局部放大、字幕、配音、音乐、音效是剪辑时另加的，不在渲染器里。
- 视频里的解说版会把选手写在指令里的角色注释（"送旗""拦截"之类）显示成小气泡，渲染器支持（`FReplay.draw(..., notes=...)`），但出片入口没有打开。

## 4. 文件

```text
renderer/
  出片.cmd  THIRD_PARTY_NOTICES.md
  assets/avatars/   视频里用的选手头像
  src/
    make_video.py   出片入口（版式、播放速度、ffmpeg）
    fboard.py       回放读取、棋盘、比分栏
    common.py       字体、ffmpeg、头像查找、从回放网页里取数据
```
