# results · 赛事产物

> 本目录只保留**当前与今后**由 `arena` 生成的正式赛事产物。
> 历史归档在原工程的 `algorithm-contest/` 下（未随本社区副本提供），按用途分开：
>
> - **比赛成绩** → 原工程 `algorithm-contest/tests/history-seasons/`
>   （第 1–5 季：v1.0 / v1.1 / v1.2 / v1.3 / v1.4；v1.2 只有赛程，未执行）
> - **规则实验** → [`../experiment-records/`](../experiment-records/)
>   （v1.5 → v1.9.1 的 12 个实验批次、攻守实验与临时诊断，附实验日志索引）
>
> 裁判台的正式比赛成绩在原工程 `algorithm-contest/match-records/`，测试成绩在
> `algorithm-contest/tests/test-results/`，都不写到本目录。

## 目录文件（以正式赛季为例）

| 文件 | 生产者 | 说明 |
|---|---|---|
| `赛程.json` | `arena plan` | 冻结的选手、场次、seed、规则与判罚口径；`run` 只认它 |
| `结果.jsonl` | `arena run` | 每局一行：场次、座位、胜者、层级、VP、七领域总分、故障、确定性、规则指纹 |
| `成绩.md` | `arena rate` | BT 实力值、95% 区间、胜平负、总分 |
| `成绩_加权总分.md` | `arena rate` | 七领域合计与场均 |
| `成绩.json` | `arena rate` | 机器可读的 BT 结果 |
| `封存记录.json` | `arena build` | 本季开跑时的源码/共用库 SHA-256 快照 |
| `replays/match-NNN.json` | `arena run` | 原始 hello + 每局结果 + 每 5 tick 帧 + 复核 |

## 重建（当前赛季）

```bash
cd war_of_desert
CFG=tournament/formal_v19.json
go run ./cmd/arena plan -config $CFG
go run ./cmd/arena run  -config $CFG   # 默认覆盖结果；-resume 可续跑
go run ./cmd/arena rate -config $CFG
```

## 审计

1. `结果.jsonl` 行数 = `赛程.json` 场次数 × 2；
2. 每行 `verifyOk=true`、`rulesHash` 一致；
3. `成绩.md` 的局数 = 结果行数；
4. `封存记录.json` 的源码哈希与本季提交逐一对得上；
5. 任何缺场、重跑、作废都要在 `workspace/CONDITIONS.md` 里留记录。

第 4 条为什么单列一份：`bots/seal-records/<赛季>.json` 每次 `build` 都会被重写
（`binSha256` 随环境变），入库就会一直脏。赛季目录里的这份是**开跑时**
的拷贝，字节冻结、只随赛季变，才拿得住「跑的是哪份源码」。
校验命令（以历史第 5 季为例）：

```bash
cd war_of_desert
py -3 -X utf8 scripts/verify_seal.py "../algorithm-contest/tests/history-seasons/formal-v14/封存记录.json"
```
