[English](README_en.md)

# 渲染器

把一份造桥回放渲成 1920×1080、30 帧的无声 mp4：低多边形的台地、峡谷和水面，桥按材料上色（木 = 蜂蜜黄、钢 = 朱红、液压 = 青、弹簧 = 黄绿、钢索 = 深灰），车辆按回放里的物理尺寸建模；断裂、落水、液压动作时自动慢放，左上角显示全桥最大受力。视频里的 3D 比赛画面都是它出的。物理全部来自回放的逐帧数据，网页里不另算；同一帧号画面完全相同。

## 要装什么

| 东西 | 测试过的版本 | 说明 |
|---|---|---|
| Python | 3.12 | 装 `playwright`（测过 1.63） |
| 浏览器 | Chrome 157 | 默认用系统装的 Chrome。没有时加 `--browser msedge`，或 `playwright install chromium` 后加 `--browser chromium` |
| three.js | 0.170.0 | 在本目录运行 `npm install`（装到 `node_modules/`，不入库） |
| ffmpeg | 7.1 | 放进 PATH，或用 `A12_FFMPEG` 指定 |
| 中文字体 | 系统字体 | 按名字找 Noto Sans SC / 微软雅黑 / 苹方；别的字体用 `A12_FONT` 指到字体文件 |

测过的环境：Windows 11 + RTX 4060 笔记本显卡，约 9 帧/秒。

## 出片

```text
cd bridge
renderer\出片.cmd replays\L1-opus.html -o L1.mp4
renderer\出片.cmd replays\L7-bridge-sol62-vs-saboteur-kimi.html -o L7.mp4 --t0 2 --t1 12
```

不用 `.cmd` 时：`python renderer/src/render3d.py <回放> [选项]`。不带 `-o` 时输出到 `renderer/renders/<回放名>.mp4`（不入库）。

**输入**：[replays/](../replays/README.md) 里的回放网页 `.html`（里面内嵌了回放 JSON，渲染器直接取出来），或者自己用 `python workspace/bridge.py run <关卡> <设计.json> [场景.json] --replay 回放.json`（第 7 关用 `duel ... --replay`）导出的 JSON。

常用选项：

| 选项 | 说明 |
|---|---|
| `--t0 / --t1` | 只出这段模拟时间（秒） |
| `--title / --sub` | 开头的标题字条 |
| `--list` | 只打印时长和慢放窗口 |
| `--still 0,120`、`--at 0:3.0` | 只截这几帧 PNG（帧号，或"段号:模拟秒"） |
| `--fps 60`、`--ss 2` | 帧率、超采样倍数（默认 30、2） |
| `--analysis` | 受力上色（拉 = 红、压 = 蓝） |
| `--noao` | 关掉环境光遮蔽，快一些 |
| `--max-frames N` | 只渲前 N 帧（试渲） |
| `--config 剪辑表.json` | 多段拼接，每段单独设机位和慢放 |

剪辑表是一个列表（或 `{"fade": 0, "clips": [...]}`），每段：`file`（回放路径，相对剪辑表）、`t0/t1`、`title/sub`、`slow`（默认自动；`false` 关掉）、`speed`、`peakSlow`（`{at, rate, pre, post, zoom}` 受力峰值慢放）、`cam`（`{yaw, pitch, dist, tx, ty, tz, fov, fill, keys, track}`：`fill` 桥占画面宽度比例，`keys` 按模拟时间的镜头关键帧，`track` 跟车）、`hud`（`false` 关掉受力读数）、`bg`（远景 canyon / bay / river）、`build`（开头杆件依次飞入拼成桥的搭桥动画）。更多字段的用法写在 `src/theater3d.js` 里各处注释。

## 文件

```text
renderer/
  出片.cmd             一条命令出片
  package.json         three.js 版本
  src/render3d.py      主程序：读回放 → 无头 Chrome 逐帧截图 → ffmpeg
  src/theater3d.html   页面
  src/theater3d.js     场景、车辆、船、特效、机位、慢放、受力读数
```

## 和视频正片的差别

- 不带音效、配音、字幕和剪辑；视频里一些镜头用了手写的剪辑表（机位关键帧、搭桥动画），这里没有收录，默认是自动取景。
- 本机路径换成了 PATH 和环境变量；不再强制要求 NVIDIA 显卡；默认编码 x264 medium CRF 16。
- 这是第 9 集制作用的"搭桥动画版"，包含原版小剧场渲染器的全部功能。

## 已知限制

- 没设 `A12_FONT` 时页面日志里会有一条字体 404，不影响画面（会用系统字体）。
- 水花是程式化的，近看偏白；捣蛋车造型按物理质点和杆件自动生成。
- 第 6 关 36 米长桥自动取景时桥在画面里偏细，可用剪辑表 `cam: {fill: 0.95, pitch: 0.2}`。
- 只在 Windows 上测过。

第三方依赖见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
