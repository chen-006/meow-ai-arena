# 第 12 集纯享版分层随机抽局

本目录收录第 12 集纯享版视频的 72 局分层随机抽样脚本、抽样统计与逐局选局清单。

## 文件说明

| 文件 | 内容 |
|---|---|
| [`select_pure.py`](select_pure.py) | 分层随机抽局脚本（仅用 Python 标准库，直接读取 `../results/formal_games.jsonl.gz` 全部 10,600 局） |
| [`纯享版选局.md`](纯享版选局.md) | 72 局选局方法说明、六回合胜率对照表与逐局列表 |
| [`pure_pick.json`](pure_pick.json) | 抽样得到的 72 局完整列表（含播放顺序号、回合、局号、种子、地图、换边标记与估算帧数） |
| [`pure_stats.json`](pure_stats.json) | 抽样统计摘要（命中随机种子 `263`，逐回合抽样胜率与正式胜率对照） |

## 复现核对

在 `boomerang-team/` 或本目录下直接运行（约 0.2 秒）：

```bash
cd boomerang-team/purecut
python select_pure.py
```

脚本会从 `../results/formal_games.jsonl.gz` 重跑分层抽样，命中第 `263` 号种子并逐项核对与 [`pure_pick.json`](pure_pick.json) 完全一致。

## 说明

- 原制作目录中仅服务于本地批量渲染和成片剪辑的辅助脚本（如 `chain.py`、`post.py`、`mv12.py`、`shots.py`、`launch.ps1`）不收录；抽局所需数据已全部在仓库的 `results/formal_games.jsonl.gz` 中，无需原制作目录即可完整复现抽样过程。
