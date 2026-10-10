# 第 2、3 轮和赛后的包

第 1 轮的选手包是固定的，原样放在 `../workspace/`。第 2、3 轮每队拿到的包不一样（里面有别队或本队的代码），是比赛时用脚本现拼的。这里放的是固定的部分，下面写明每个包实际还装了什么。拼包的脚本在 `../referee/`。

| 路径 | 内容 |
|---|---|
| `round2/review/` | 第 2 轮互测包的固定部分：任务说明 `REVIEW.md`、旧文档 `SPEC.md`、工具 `tools/`（合法性检查、生成器、本地判定 `try_case.py`） |
| `round3/TAKEOVER.md` | 第 3 轮的任务说明 |
| `round3/workspace/` | 第 3 轮接手包新增的部分：需求变更 `CHANGE.md`、`tools/bench2.py`、`tools/gen2.py`、变更后的 4 组小型公开负载、30 个边界用例、3 组大负载（都带标准输出） |
| `round3/regression/<主程>/` | 各队的回归用例：第 2 轮别队找到的、判定成立的反例，已转换成新格式并配好标准输出。文件名 `fromX_…` 里的 X 是找到它的测试手所在的队，`slow_` 开头的是超时类。队A（Kimi K3）没有被找到反例，所以没有这个目录 |
| `retest/REVIEW.md` | 赛后终版复测的任务说明 |
| `blind-review/评审说明_同一对话版.md` | 盲评细则，裁判实际拿到的就是这一份 |
| `blind-review/评审说明.md` | 早先"每份代码开一个对话"的版本，裁判没有用到；留着是因为 `referee/生成盲评包.py` 要读它 |

## 每个包实际装了什么

**第 2 轮 · 互测包**（`referee/生成攻防包.py`，解压后是 `review/`）：

- `round2/review/` 的全部内容；
- `clock.py`：由 `referee/clock_重赛模板.py` 填上本轮的参数生成；
- `baseline/` 和 `workloads/public/`：和 `../workspace/` 里的相同；
- `submissions/队X/src/`：另外 5 队主程的代码，即 `../submissions/round1/` 里除本队以外的 5 份。发出去的副本里，"攻击面""攻手""击穿""漏洞"等词换成了中性说法（交接文档和代码注释都换），正式判定用的是原件；
- `cases/队X/把用例放在这里.txt`：5 个空的提交目录。

**第 3 轮 · 接手包**（`referee/生成接手包.py`，解压后是 `workspace/`）：

- `round3/TAKEOVER.md`；
- 第 1 轮选手包里的 `SPEC.md`、`baseline/`、`workloads/`、`tools/`（其中 `check_input.py` 换成了 `round2/review/tools/` 里措辞中性的那一份）；
- `round3/workspace/` 的全部内容；
- `workloads2/regression/`：本队的回归用例，即 `round3/regression/<主程>/`；
- `src/`：本队主程的代码，即 `../submissions/round1/<主程>/`。交接文档的副本同样换成了中性说法，代码文件原样；
- `clock.py`：同上，换成第 3 轮的参数。

**赛后 · 终版复测包**（`referee/生成赛后评审包.py`，解压后是 `review/`）：

- `retest/REVIEW.md`、`SPEC.md`、`CHANGE.md`、`clock.py`；
- `reference/`：参考实现 `../referee/参考实现/v2_变更/`；
- `tools/`：`check_input.py`、`measure.py`、`gen.py`、`gen2.py`，以及改成"和参考实现比、按新格式检查"的 `try_case.py`；
- `submissions/finalN/src/`：别队的 5 份终版，编号是随机的（对照表在 `../results/retest/对照表.json`），文字里的模型名、厂商名、工具名和队名换成了"某某"。

**赛后 · 盲评**（`referee/生成盲评包.py` 和 `生成赛后评审包.py`，每位裁判一个文件夹）：

- 12 个 `代码XXX/src/`：6 份主程代码和 6 份终版，编号随机（对照表在 `../results/blind-review/对照表.json`），同样做了匿名替换；
- `锚点/屎山基线/`（即 `../workspace/baseline/`）和 `锚点/干净参考/`（即 `../referee/参考实现/v2/`）；
- `CHANGE.md`、`评审说明.md`（内容是 `blind-review/评审说明_同一对话版.md`）、`评审顺序.txt`（每位裁判的顺序不同）。

想在本地照样拼出这些包，先按 `../referee/README.md` 还原目录，再运行对应的脚本。
