[English](README_en.md)

# 渲染器

把一局足球回放（`SOCCER2V2-REPLAY 2`，即 [replays/](../replays/README.md) 里的 `.txt`）渲成 1920×1080、60 帧的无声 mp4：three.js 球场，直播主机位跟球，进球后切特写、打"进球！"字幕，再接约 4 秒慢动作回放，左上比分条、右上 LIVE / 回放牌。视频里的比赛画面都是它出的。同样的参数，画面逐帧相同。

## 要装什么

| 东西 | 测试过的版本 | 说明 |
|---|---|---|
| Python | 3.12 | 装 `playwright`（测过 1.63） |
| 浏览器 | Chrome 157 | Playwright 默认用系统装的 Chrome（`channel=chrome`）。没有 Chrome 时加 `--browser msedge`，或 `playwright install chromium` 后加 `--browser chromium` |
| three.js | 0.170.0 | 在本目录运行 `npm install`（按 `package.json` 装到 `node_modules/`，不入库） |
| ffmpeg | 7.1 | 放进 PATH，或用环境变量 `A12_FFMPEG` 指定 |
| 中文字体 | 默认 `C:\Windows\Fonts\NotoSansSC-VF.ttf` | 没有时浏览器按名字找 Noto Sans SC / 微软雅黑 / 苹方。别的字体用 `A12_FONT` 指到字体文件 |

测过的环境：Windows 11 + RTX 4060 笔记本显卡，约 12 帧/秒（60 帧成片约 5 秒渲 1 秒）。

## 出片

```text
cd soccer
renderer\出片.cmd replays\final\team4-team5-s38-1.txt -o t4t5.mp4 --start 0 --end 160
```

不用 `.cmd` 时：`python renderer/src/render_soccer.py <回放.txt> [选项]`。不带 `-o` 时输出到 `renderer/renders/<回放名>.mp4`（不入库，`A12_WORK` 可改）。

常用选项：

| 选项 | 说明 |
|---|---|
| `--start / --end` | 起止步（默认整局；最好从开球那一步开始） |
| `--plan` | 只打印时间线和进球列表，不渲染（几秒） |
| `--no-replay`、`--replay-goals 3,5` | 不插慢动作回放 / 只给这几个进球插 |
| `--sps 12` | 直播速度：每秒多少步（1200 步 = 100 秒） |
| `--still 300,540` | 只导出这几帧的 PNG |
| `--every 2`、`--preset veryfast` | 快速预览：30 帧、编码快 |
| `--max-frames N` | 只渲前 N 帧（试渲） |
| `--nvenc` | 用 NVIDIA 硬件编码 |
| `--stage-text` | 比分条下面的小字（默认"终场赛 · 种子 N"，`individual/` 下的回放是"个人赛 · 种子 N"） |

机位、慢放、转场的细调参数见 `-h`。

## 文件

```text
renderer/
  出片.cmd                一条命令出片
  package.json            three.js 版本
  assets/avatars/         选手头像（视频里用的那套）
  src/render_soccer.py    主程序：读回放 → 排时间线和机位 → 无头 Chrome 逐帧截图 → ffmpeg
  src/camera_safety.py    机位不穿墙、不穿门网的约束（stage/camera_safety.js 是同一套的网页版）
  src/test_camera_safety.py  机位约束的单元测试
  src/players.json        回放里的选手名 → 画面短名、头像文件、所属队；队名和队色
  src/stage/              画面：index.html + stage.js（three.js 场景和转播包装）
```

新选手在 `players.json` 里加一行；没配置的名字也能渲（名字前 8 个字、默认头像、灰色）。个人赛回放里的"<名字>·搭档"自动沿用"<名字>"那一行，队名显示选手名。

## 和视频正片的差别

- 没有音效：正片版带踢球声、哨声、欢呼的音轨，音效素材不在仓库里，这里只出无声画面。
- 转场里的系列 logo 换成了文字"AI 算法对抗大赛"。
- 本机路径换成了 PATH 和环境变量；不再强制要求 NVIDIA 显卡；默认编码 x264 medium CRF 16（正片制作时用 NVENC）。
- 正片的剪辑、字幕、配音、图表不在这里。

## 已知限制

- 进球那一步之后的几帧回放里没有记录，球进网、球员滑停是按物理外推的示意。
- 场上禁区、点球点等白线只是装饰，规则里没有。
- 久无踢球（300 步）重新开球时画面压黑硬切，不插回放。
- 只在 Windows 上测过；别的系统要自己指定字体和浏览器。

第三方依赖见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
