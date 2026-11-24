# 正式成绩、逐局结果与赛后矩阵

本目录存放第 12 集 5v5 团队赛全部 6 回合正式赛（10,600 局）及 6 张 3×3 赛后“如果当时”矩阵（54,000 局）的成绩与精简逐局数据。

| 文件 | 内容 |
|---|---|
| [`总成绩.md`](总成绩.md) | 6 回合正式比分（`5.5 : 0.5`）及 6 张 3×3 矩阵汇总表 |
| [`总成绩.json`](总成绩.json) | 机器可读的总成绩与矩阵完整精度数据 |
| [`rounds/`](rounds/) | 比赛时每回合自动生成的原样 `R1..R6_成绩.json`、`R1..R6_回合报告.md`、`R1..R6_矩阵.md` |
| `formal_games.jsonl.gz` | 全部 10,600 局正式赛的精简逐局结果（每局一行 JSON，已移除本机绝对路径） |
| `matrix_games.jsonl.gz` | 全部 54,000 局赛后矩阵的精简逐局结果（每局一行 JSON） |

使用一条命令即可从 `formal_games.jsonl.gz` 与 `matrix_games.jsonl.gz` 重算全部比分和矩阵并逐项核对：

```bash
cd boomerang-team
python tournament/score.py
```
