# baseline 基线调试实验（baseline@1 中等 / baseline@2 极强）

> 目标（2026-11-23）：预制 Bot 名册合并为两个——baseline@1（中等战术水平）与
> baseline@2（精心调试、极强能力）；旧 7 个参考 Bot 移入 `experiment-records/reference-bots-archive/`。
> 本目录记录多轮调试实验。规则口径 v1.9.2（指纹 `077336ff`）。

## 方法

- 实验场：旧 7 参考 Bot（Vanguard/Nomad/Swarm/Sapper/Bulwark/Hunter/Warden）+ baseline1 + baseline2，共 9 人。
- 迭代轮：5 seed × 36 对 × 换边 2 局 = 360 局；定稿轮：17 seed = 1224 局。
- 配置：`tournament/_tmp_baseline_exp.json`（逐轮改名/输出目录）；逐局结果在 `results/baseline-exp-r*/`。
- 强度锚：v1.9.1 标定 714 局（`experiment-records/rule-experiments/exp-v191-700/`）——Hunter BT 第 1（134/4/66），
  Warden 中游（110/9/83，54% 胜率）。
- baseline@1 取 Warden 剖面（refbot 库，中等）；baseline@2 用新策略库 `workspace/acebot`：
  集火（价值/残血/距离评分）、伤兵后撤（高军衔更保守）、护旗（阻断入库）、劫旗（撤回遗产分）、
  遗产竞速（携货无旗主动插旗，v1.9.2 语义）、攻城 face 微操、obey 全控、队列上限 6、首炮 120 CU 门槛。

## 轮次记录

### R1（acebot v1：集火/后撤/护旗/劫旗/face，AAV+LAV 混编，1 遗产车）

360 局（9 人 × 5 seed × 2），verify 360/360，故障 0，无效 6。

| Bot | 胜/平/负 | 胜率 | vs old7 |
|---|---|---:|---:|
| **baseline2** | 59/2/19 | 73.8% | 72.9% |
| Hunter | 55/0/25 | 68.8% | — |
| Swarm | 49/2/29 | 61.2% | — |
| Vanguard | 40/2/38 | 50.0% | — |
| **baseline1** | 38/8/34 | 47.5% | **51.4%（中等 ✓）** |
| Warden | 35/9/32 | 46.1% | — |

baseline2 对位：Vanguard 10:0、Nomad 10:0、Sapper 9:1、Bulwark 8:2、Hunter 6:4、
Warden 5:3、**Swarm 3:7**。短板：纯 LAV 海（Swarm）用零 RU 低成本快节奏压垮 AAV 线；
攻城镜像（Warden）无 RAILGUN 反制。baseline1 中等定位达标，不再调整。

### R2（纯 LAV tempo + RAILGUN 反攻城，2 遗产车，低后撤阈值，库存护旗）

360 局，verify 360/360，故障 0，无效 6。

| Bot | 胜/平/负 | 胜率 |
|---|---|---:|
| **baseline2** | 67/0/13 | **83.8%** |
| Hunter | 53/0/27 | 66.2% |
| Swarm | 47/2/31 | 58.8% |
| baseline1 | 36/8/36 | 45.0%（vs old7 51.4%，中等 ✓） |

baseline2 vs old7 **81.4%**（57/0/13）；对 baseline1 **10:0**。
对位：Warden 10:0、Sapper 10:0、Bulwark 9:1、Hunter 8:2、Nomad 8:2、Vanguard 7:3、**Swarm 5:5**。
败局特征：全部 L2（母舰从未被毁），多数败局遗产送达 0、击杀极低——
**严格生产序在 RAILGUN 付不起时停摆整条产线 600 拍**（引擎本是跳过式出队，Bot 侧不该死等）。

### R3（轮转下单 + RAILGUN 后置 + 集火威胁加成）

360 局，verify 360/360，故障 0。baseline2 **85.0%**（vs old7 **82.9%**，58/0/12），
对 baseline1 10:0。Swarm 6:4、Vanguard 7:3、Nomad 8:2。败局仍集中在固定 seed。

**败局根因（m086 回放逐帧追踪 + 引擎探针）**：
- 引擎探针（单次指派、无对手）900 拍 CU 正常入账 → 引擎无缺陷；
- 回放放大：CU 运输车 t820 成功卸货 +200，同拍被产线消耗——经济活着，但 LAV 添油般送死；
- 真正缺陷：**伤兵后撤无治疗源**（建造表无 CARRIER），25% 血撤到母舰后永远到不了 60% 回归线，
  战力单调流失（preserve 85 但全是基地里的伤兵）；
- 远矿图（CU 矿距基地 930px）单车收入仅 0.57 CU/tick，产能被 Nomad 压制。

### R4（2 CU + 1 RU 采集、CARRIER 治疗闭环、5 采集车）

360 局，verify 360/360，故障 0。baseline2 **93.8%**（vs old7 **92.9%**，65/0/5），
对 baseline1 10:0。**Hunter 10:0、Vanguard 10:0、Warden 10:0、Sapper 10:0、Bulwark 10:0、Nomad 9:1**。
仅 Swarm 6:4（败局 m056/092/128 均为 550–924 tick 早期阈值负——重单位上线前被轻甲海淹没）。

### R5（4 LAV 开局 + Aggression 3.0 + 后撤线 0.2）

360 局。baseline2 vs old7 **90.0%**——**劣于 R4**（Swarm 仍 6:4，Vanguard/Bulwark 反而下滑）。
结论：早期多 1 LAV 的收益不及更早 CARRIER 治疗闭环；**回退 R4 参数定稿**。

### 定稿轮（R4 参数 · 9 人 × 17 seed × 2 = 1224 局）

verify **1224/1224**，故障 **0**，无效 18。

| Bot | 胜/平/负 | 胜率 |
|---|---|---:|
| **baseline2** | 232/0/38 | **85.9%** |
| Swarm | 176/14/82 | 64.7% |
| Hunter | 162/3/107 | 59.6% |
| Vanguard | 131/19/118 | 48.9% |
| Warden | 117/32/113 | 44.7% |
| **baseline1** | 111/31/126 | **41.4%** |
| Bulwark | 79/26/157 | 30.2% |
| Sapper | 70/3/199 | 25.7% |
| Nomad | 59/10/197 | 22.2% |

baseline2 vs old7 **86.0%（203/0/33）**，对全部 7 Bot 逐个胜多负少：
Vanguard 31:3、Nomad 32:2、Sapper 30:4、Bulwark 29:3、Hunter 31:3、Warden 28:6、Swarm 22:12。
对 baseline1 **29:5**。baseline1 vs old7 **45.3%**（中等定位达标）。

**定稿参数（baseline2）**：Alloc {armour 35, repair 15, attack 30, range 20}；
Build [RESEARCH, LAV×3, CARRIER, RAILGUN, LAV]；Techs [I1, F1, C1, I3a, F2, C2, F3, C3]；
Aggression 2.6；Weapons 2（首炮 120 CU 门槛）；RelicRunners 2；采集 2 CU + 1 RU；
RetreatHP 0.25 / Vet 0.4（回归 0.6，CARRIER 治疗闭环）；护旗（库存 ≥1）/ 劫旗（库存 ≥1，≤2 台）/
face 微操 / obey 全控 / 轮转下单（队列上限 6）。

**结论**：baseline@2 显著强于旧场最强 Hunter（旧标定 66% 胜率），满足「极强」；
baseline@1 维持中等（~45–51%）。Swarm（纯轻甲海）是最难对位（64.7%），留作选手可针对的风格。

## 证据

- `r1/`…`r5/`、`final/`：各轮 结果.jsonl + 成绩.md（回放留在本地 results/，不入库）。
- `final_config.json`：定稿轮配置（旧 7 Bot 的 source 指向 `_tmp_archive_src/` ASCII 副本，
  复现时按 `../reference-bots-archive/README.md` 说明先从归档拷贝）。
- 正式短赛季（双基线 × 17 seed = 34 局）：baseline2 对 baseline1 **27:7**，34/34 复核通过，
  产物在 `results/formal-v19/`。