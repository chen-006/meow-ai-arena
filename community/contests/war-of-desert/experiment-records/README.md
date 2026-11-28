# 实验记录 · 规则与参数标定日志

> 这里归档 `war_of_desert` 的规则/参数实验批次，按「实验日志」口径整理：
> 每批记录规则版本、规则指纹、配置、规模、关键结论与产物目录。
> 正式赛季成绩不在这里，见 `algorithm-contest/tests/history-seasons/`。
>
> 回放（`replays/`）体积大且可由 `arena run` 重建，默认不入库，见 `.gitignore`。

## 规则实验一览

| 批次 | 规则版本 | 规则指纹 | 配置 | 规模 | 关键结论 |
|---|---|---:|---|---:|---|
| [exp-v15](rule-experiments/exp-v15/) | v1.5 | `24d4935b` | `experiment_v15.json` | 45 场 / 90 局 | 首次接入胜利分三本账、遗产与插旗；阈值胜 66%，斩舰 3% |
| [exp-v16-500](rule-experiments/exp-v16-500/) | v1.6 | `aa1e7e0f` | `experiment_v16_500.json` | 255 场 / 510 局 | 座位 52.2:47.8；研究舰进入实战；平局 46.3% 成为主问题 |
| [exp-v17-500](rule-experiments/exp-v17-500/) | v1.7 | `789bd978` | `experiment_v17_500.json` | 255 场 / 510 局 | tie-break 把平局从 46.3% 降到 25.1%，但座位偏置恶化到 58.1:41.9 |
| [exp-v17-500-fair](rule-experiments/exp-v17-500-fair/) | v1.7.1 | `8cc42f0b` | `experiment_v17_500.json` | 255 场 / 510 局 | 出兵角镜像修正有效，但座位差只降到 57.3:42.7；同 Bot 镜像 t≈5 |
| [exp-v17-500-tb](rule-experiments/exp-v17-500-tb/) | v1.7.2 | `e33559c4` | `experiment_v17_500.json` | 255 场 / 510 局 | 删掉科技/送达 tie-break；座位回到 51.9:48.1，平局回到 48.0% |
| [exp-v18-500](rule-experiments/exp-v18-500/) | v1.8.0 | `90c3c937` | `experiment_v17_500.json` | 255 场 / 510 局 | 人口 36 + 全线压上；平局 21.0%，座位 50.5:49.5，战斗显著增加 |
| [exp-v18-500-siege](rule-experiments/exp-v18-500-siege/) | v1.8.0 | `90c3c937` | `experiment_v17_500.json` | 255 场 / 510 局 | 只改 Bot 编成无法建立拆舰路线，母舰斩杀仍 2.0% |
| [exp-v181-500](rule-experiments/exp-v181-500/) | v1.8.1 | `526aab09` | `experiment_v17_500.json` | 255 场 / 510 局 | 远程兵降价：RAILGUN +30%、ARTILLERY +81%，斩杀仍仅 2.5% |
| [exp-v182-500](rule-experiments/exp-v182-500/) | v1.8.2 | `a0303a07` | `experiment_v17_500.json` | 255 场 / 510 局 | 对母舰伤害 ×1.5 无效；远程兵 84% 对局进不了射程 |
| [exp-v183-500](rule-experiments/exp-v183-500/) | v1.8.3 | `f281c99a` | `experiment_v17_500.json` | 255 场 / 510 局 | 修高差冻结：平局 21.2% → 3.3%，座位 51.1:48.9 |
| [exp-v19-700](rule-experiments/exp-v19-700/) | v1.9.0 | `ff236590` | `experiment_v19_700.json` | 357 场 / 714 局 | 7 人赛；母舰斩杀 6.6%，遗产科技仍偏弱 |
| [exp-v191-700](rule-experiments/exp-v191-700/) | v1.9.1 | `393ee404` | `experiment_v19_700.json` | 357 场 / 714 局 | 定稿：座位 49.9:50.1，斩杀 7.1%，ARTILLERY 0.11 台/方/局 |

> 规模按「场次 × 换边 2 局」计。v1.5 / v1.7 批次未跑 `arena rate`，
> 因此没有 `成绩.md` 系列；实验结论以各目录 `README.md` 为准。

## 攻守实验

| 批次 | 说明 | 目录 |
|---|---|---|
| siege | 母舰 HP 扫描、工事预算、攻守双参数 BT | [offense-defense-experiments/siege](offense-defense-experiments/siege/) |

标定过程见原工程 `Dev/experiments/Plan_Siege.md`（未随本副本提供）。

## 临时诊断

短跑目录只用于定位座位/公平性问题，结论已并入正式实验：

| 目录 | 规则 | 规模 | 用途 |
|---|---|---:|---|
| [temp-diagnostics/_tmp-mirror](temp-diagnostics/_tmp-mirror/) | v1.7 | 17 场 | 同 Bot 双座位镜像，初查座位偏置 |
| [temp-diagnostics/_tmp-mirror2](temp-diagnostics/_tmp-mirror2/) | v1.7.1 | 40 场 | 出兵角修正后的同 Bot 镜像复核 |
| [temp-diagnostics/_tmp-mirror-v19](temp-diagnostics/_tmp-mirror-v19/) | v1.9.1 | 40 场 | v1.9.1 送达差复验（t=1.11） |
| [temp-diagnostics/_tmp-v17-90](temp-diagnostics/_tmp-v17-90/) | v1.7 | 45 场 | tie-break 改动的短跑对照 |

完整排查过程见 [`座位偏差排查报告_v1.7.1.md`](座位偏差排查报告_v1.7.1.md)。

## 复现

```bash
cd war_of_desert
go run ./cmd/arena build -config tournament/<配置>.json
go run ./cmd/arena plan  -config tournament/<配置>.json
go run ./cmd/arena run   -config tournament/<配置>.json
go run ./cmd/arena rate  -config tournament/<配置>.json   # 需要 BT 榜单的批次
```

实验产物默认写到配置里的 `output`；若要复现到本目录，把 `output` 改成
`experiment-records/rule-experiments/<批次>`（或直接用配置里的原路径，跑完再归档）。
