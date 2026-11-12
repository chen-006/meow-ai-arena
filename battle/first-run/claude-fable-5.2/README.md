[English](README_en.md)

# 首次运行：Claude Fable 5.2（存档，不计成绩）

这里是 Claude Fable 5.2 这一席的**首次运行**：比赛时由赛事脚本在主办方电脑上启动（Claude Code 2.1.322，无界面模式 `claude -p`），和其他选手同时作答，第 55.7 分钟遇到主办方整机重启，续接后第 83.0 分钟主动交卷。

这一席的正式成绩**采用的是赛后第三方在另一台机器上的重跑**，作品在 [bots/claude-fable-5.2/](../../bots/claude-fable-5.2/)。这次首次运行**不计入任何榜单**，原样存档，供大家对照。为什么换、两次运行的条件和用量对照、我们核对过什么、局限，都写在 [比赛条件](../../CONDITIONS.md#claude-fable-52-这一席采用第三方重跑)。

## 五关结果

比赛时主办方评测过这份作答；收录时我们又把 [submission/](submission/) 放进一份干净的选手包，用同一个 `war.exe` 跑了 `python play.py levels`，逐关结果（过没过、用时、结束原因、整场模拟的摘要 `digest`）和比赛时的评测相同：

| 关 | 首次运行 | 采用的重跑 |
|---|---|---|
| 第 1 关 草甸迎击 | 过关，260（52.6 秒） | 过关，200 |
| 第 2 关 断龙桥 | 过关，395（52.9 秒） | 过关，150 |
| 第 3 关 拔炮台 | 没过（30.6 秒全军覆没；交的是只有一个新兵的起步布阵，10） | 过关，260 |
| 第 4 关 一线天 | 过关，375（115.1 秒） | 过关，400 |
| 第 5 关 镇山兽 | 没过（4.4 秒全军覆没；起步布阵，10） | 过关，245 |

过了 3/5 关。1v1 和四人混战的布阵也在 [submission/](submission/) 里；它们不在正式的 945 场里，这里不给对抗成绩。

## 目录

| 路径 | 内容 |
|---|---|
| [submission/](submission/) | 首次运行收卷时 `submission/` 里的全部文件，逐字节原样（哈希见 [source_manifest.json](../../source_manifest.json)，`source` 为 `first_run_submission`） |
| [replays/](replays/) | 上面五关的回放 `L<关>_fable.wrp.xz`：比赛时主办方评测这份作答（换记录之前那次评测）由 `war.exe batch` 存下的原件，xz 压缩；原文件的 SHA-256、大小和结果在 `manifest.json` |

## 自己验证

```text
# 在 battle/ 目录下
python build_engine.py
# 把 first-run/claude-fable-5.2/submission/ 整个复制成 workspace/submission/
cd workspace
python play.py check
python play.py levels
```

`play.py levels` 存下的回放和这里的原件相比，只有座位名（`level1` 和 `fable`）、末尾结果段里地图文件的路径和耗时统计不同，战斗过程相同。这里的回放用 Python 解开：`python -c "import lzma,sys; open(sys.argv[2],'wb').write(lzma.decompress(open(sys.argv[1],'rb').read()))" L3_fable.wrp.xz L3_fable.wrp`，再用 `play.py replay` 看。

## 这里没有什么

会话记录没有放（本仓库不收录任何选手的会话记录）。
