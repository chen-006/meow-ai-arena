[English](README_en.md)

# 渲染器（光轮对战）

把一局光轮对战的回放渲成 1920×1080、30 帧的 mp4：左边大棋盘（霓虹光墙 + 头像光轮，带逐帧补间、撞墙爆点、抢格和互换特效），右边侧栏（回合数、存活人数、每人长度 / 死因 / 名次），顶上标题，底下一条节目条。视频第 7 集里的对局画面就是它出的。

## 1. 要装什么

| 东西 | 版本 | 说明 |
|---|---|---|
| Python | 3.10 以上（测试过 3.12） | 装 `pillow`、`numpy`：`pip install pillow numpy`（测试过 Pillow 12.3.0、NumPy 2.5.1） |
| ffmpeg | 任意较新版本（测试过 7.1） | 合成 mp4。放进 PATH，或用环境变量 `ARENA_FFMPEG` 指到可执行文件。只出静帧时不需要 |
| 中文字体 | 默认 Windows 的 `Noto Sans SC`（`NotoSansSC-VF.ttf`），没有就用微软雅黑 | 字体不随仓库提供。别的字体用环境变量 `ARENA_FONT` 指定一个 `.ttf/.ttc/.otf`；Linux、macOS 会依次试 Noto Sans CJK、苹方，找不到就要指定 |

测试过的环境是 Windows 11。代码里没有只认 Windows 的东西，但别的系统没测过。

## 2. 出片（一条命令）

在 `tron/` 下：

```text
python renderer/src/make_video.py <回放> [-o 输出.mp4] [--from 秒] [--to 秒] [--fps 30] [--still 回合]
       [--tps 4] [--slow 1.5] [--style mix|neon|flat|avatar] [--vor] [--palette 配色.json] [--names 甲,乙] [--sub 标题]
```

Windows 上也可以用 `renderer\出片.cmd <回放> [同样的选项]`（用 PATH 里的 `python`，别的解释器用环境变量 `ARENA_PYTHON` 指定）。

例：

```text
python renderer/src/make_video.py replays/07-1v1-DSNew-vs-Mistral.html -o DSNew-vs-Mistral.mp4
python renderer/src/make_video.py replays/25-12p-official-m05-rot01.html --to 10
python renderer/src/make_video.py replays/14-4p-strong-Doubao-greedy-Dots-Wenxin.html --still -1 -o 终局.png
```

- **输入**：仓库 [`replays/`](../replays/README.md) 里的 `.html`（查看器和回放打包在一个网页里，渲染器直接取出里面的回放数据），或比赛工具 `arena.py play` 存的 `tron-replay-1` 格式 `.json`。载入时按规则逐回合重算一遍，和回放里记的回合数、死亡回合、死因、名次核对，不一致就报错。
- 不带 `-o` 时输出到 `renderer/renders/<回放名>/`（不入库；环境变量 `ARENA_WORK` 可改）。
- 播放速度：平时每秒 `--tps` 回合（默认 4，和网页查看器的 1× 一样），有人出局、抢格、互换的回合前后放慢到每秒 `--slow` 回合；整局末尾多停 1 秒。
- `--from / --to` 是**成片里的秒数**（按上面的播放速度算），不给就出整局。一局 1v1 一般 20～40 秒成片，在一台 32 线程的笔记本上约 0.15 秒一帧（单进程），整局 2～3 分钟出完。`--ss 1` 关掉超采样，快一倍左右，线条会有锯齿。
- `--still 回合` 只出一张 PNG，回合可以是小数（`12.5` = 第 12 回合结算完、第 13 步走到一半），`-1` = 终局。
- `--style`：`mix`（默认，视频用的：霓虹底和光墙 + 头像做光轮的头）、`neon`（纯霓虹，头是光轮）、`flat`（扁平色带，头是带编号的方块）、`avatar`（深蓝底 + 头像）。
- `--vor` 叠加"地盘"热区（每个空格离谁的头最近就算谁的），侧栏加地盘条。
- `--palette` 换配色和称呼：`{"colors": {"Mistral": "#ff7300"}, "labels": {"Doubao": "豆包"}, "avatars": {"Qwen27B": ["Qwen"]}}`，键是回放里的选手名。
- `--names` 按座位顺序替换显示的名字（头像和颜色也跟着名字走）。

## 3. 在自己的程序里用

```python
import sys; sys.path.insert(0, 'renderer/src')
import tronboard as T
rp = T.TronReplay('replays/07-1v1-DSNew-vs-Mistral.html')
img = rp.draw(40.5, 800)              # 只要棋盘：800×800 透明 RGBA，回合可以是小数
sb = rp.scoreboard(40.5, 560, 800)    # 只要侧栏
full = T.frame(rp, 40.5)              # 1920×1080 整帧
times = rp.play_times(0, rp.T)        # 每一帧对应的小数回合（含放慢）
```

## 4. 和视频正片的差别

- 头像和视频里一样，放在 `renderer/assets/avatars/`（按 `tronboard.AVATAR` 把回放里的名字对到文件名）；greedy、陪练是程序画的小圆脸。想换成自己的头像，把 `<名字>.png` 放进一个目录，用环境变量 `ARENA_AVATAR_DIR` 指过去。
- 底部节目条只写文字，没有系列 logo；节目条上方那一条（视频里放字幕）留空。
- 视频里的镜头推拉、局部放大、旁白字幕、配音、音乐、音效都是剪辑时另加的，不在渲染器里。

## 5. 已知限制

- 单进程逐帧画，不并行；12 人局（41×41）比 1v1 慢一些。
- 回放必须是完整的一局（`moves` 从第 0 回合开始），载入时的核对不通过会直接报错，不会画一局和记录对不上的画面。
- 同一局里两个人颜色太近时只在 `rp.close_pairs` 里提示，不自动换色；用 `--palette` 改。

## 6. 文件

```text
renderer/
  出片.cmd            Windows 一条命令出片（调 src/make_video.py）
  THIRD_PARTY_NOTICES.md
  src/
    make_video.py     出片入口：读回放 → 逐帧画 → 管道送给 ffmpeg；--still 出静帧
    tronboard.py      回放重算（同回放网页里查看器的 simulate()，即 workspace/tron.hpp 的 step）、棋盘、侧栏、整帧版式
    common.py         字体、ffmpeg、头像查找、从回放网页里取数据
  assets/avatars/     视频里用的选手头像
```
