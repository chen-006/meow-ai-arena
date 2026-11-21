[English](README_en.md)

# 成绩和逐局结果（正式海选）

| 文件 | 内容 |
|---|---|
| [海选榜.md](海选榜.md) | 个人榜和一对一两两战绩（每格是"行选手对列选手"的胜-平-负，每对 228 局）。这是比赛时主办方生成、放进拍卖尽调材料里的那一份，原样 |
| [海选成绩.json](海选成绩.json) | 出榜程序的完整输出，原样：每位的个人实力值、一对一和四人局实力值及 95% 区间、名次和名次区间（`players`），名次顺序（`ranking`），相邻两名的差和是否在误差内（`gaps`），每人的单步超时次数、被结束次数、单步最长毫秒（`faults`），抽签种子和 `lot` |
| `games.jsonl.gz` | 32,904 局逐局结果，每行一局：`id`（赛程里的局号）、`mode`（0 一对一、1 四人局）、`map`、`seed`、`names`（按座位顺序的席位名）、`steps`（这局打了多少步，每步 0.05 秒）、`players`（每人的 `rank` 名次、`kills` 击杀、`alive` 是否活到最后、`cause` 出局原因：`dart` 被镖打中、`slash` 被砍、`fall` 掉下去、`none` 没出局、`faults` 单步超时次数、`status`、`max_ms` 单步最长 CPU 毫秒） |
| `schedule.json.gz` | 赛程：抽签种子、地图、种子表、四人局轮空顺序、两两同组次数，以及每一局（`games`，局号和 `games.jsonl.gz` 一一对应）；`mapfile` 是仓库里的相对路径 |

读法：

```python
import gzip, json
rows = [json.loads(l) for l in gzip.open('results/games.jsonl.gz', 'rt', encoding='utf-8')]
```

席位名和选手的对应见 [../README.md](../README.md#选手)。重算个人榜：`python tournament/score.py`，见 [../tournament/README.md](../tournament/README.md)。

这一届没有存逐局回放；[../replays/](../replays/README.md) 是做视频时重打的 117 局。
