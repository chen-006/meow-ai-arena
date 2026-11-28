# Desert Arena · 榜单

> 本文件汇总历季**正式赛**榜单与读法。完整数据归档在原工程的 `algorithm-contest/tests/history-seasons/<dir>/`（未随本社区副本提供，文中的历史明细链接指向原工程）。
> **当前正式赛季 = v1.9.2 · 第 6 季**（配置 `tournament/formal_v19.json`，双基线）；v1.9.1 的 714 局标定与 BT 见 [`experiment-records/rule-experiments/exp-v191-700/README.md`](experiment-records/rule-experiments/exp-v191-700/README.md)。以下第 5 季及更早为已完赛的历史赛季（v1.0 / v1.1 / v1.3 / v1.4），数据保持原样。
>
> v1.5–v1.8 的规则实验产物在 `experiment-records/rule-experiments/exp-v*`，只用于规则验证，不混入正式榜单；v1.9.2 已是当前正式口径（v1.9.1 基础上修复运货死锁）。

---

## 第 5 季 · 基准赛（Go 原生引擎，v1.4 规则）

> 规则指纹 `553785d4` ｜ 引擎 `native` ｜ 6 名选手 ｜ 45 场 / 90 局 ｜ 零故障 ｜ 90/90 确定性复核
> 规则：1200 tick ｜ 母舰 HP 1800 / 攻击 13 / 移速 0.30 / 坡限 4（v1.3 为 3000 / 22 / 0.55 / 32）
> 完整数据：[`../algorithm-contest/tests/history-seasons/formal-v14/成绩.md`](../algorithm-contest/tests/history-seasons/formal-v14/成绩.md)、[`../algorithm-contest/tests/history-seasons/formal-v14/成绩_加权总分.md`](../algorithm-contest/tests/history-seasons/formal-v14/成绩_加权总分.md)

### 正式名次（Bradley–Terry）

| 名次 | 选手 | 实力值 | 95% 区间 | 胜/平/负 | 局数 |
|---:|---|---:|---|---:|---:|
| 1 | Nomad | 378.8 | 267.2 ~ 507.8 | 28/0/2 | 30 |
| 2 | Sapper | 73.9 | -24.7 ~ 184.5 | 14/0/16 | 30 |
| 3 | Bulwark | 73.9 | -11.7 ~ 211.4 | 12/4/14 | 30 |
| 4 | Starter | 55.6 | -20.1 ~ 167.4 | 9/8/13 | 30 |
| 5 | Swarm | 18.8 | -114.0 ~ 150.8 | 11/0/19 | 30 |
| 6 | Vanguard（锚） | 0.0 | 0.0 ~ 0.0 | 6/8/16 | 30 |

### 怎么读

- **与第 4 季逐局完全相同（90/90）**。母舰四项数值确实改了，但当前参考 Bot 既不会把兵送到敌方母舰射程内，也不会指挥自家母舰移动——这四项在所有对局里都是「无人触碰的数值」。
- 换句话说：**削弱已生效，但战局感受不到**。要让削弱产生实际影响，得先解决 Bot 的攻坚与机动执行（见根 README 已知边界 4/8）。
- 名次结构与第 4 季一致，说明本季的改动没有意外波及平衡。
- **2026-11-20 按新指纹 `553785d4` 全季重跑**（裁判链修复：RulesHash 补齐战斗/经济/视野的遗漏常量，`me.mods` 参与每拍编码）。45 场 / 90 局的胜负与实力值逐局不变，只有 95% 区间变了：bootstrap 改为按「场次整块」抽样，不再把同图换边两局当独立样本，区间因此普遍收窄。Sapper 与 Bulwark 实力值同为 73.9，名次由小数位决定。

---

## 第 4 季 · 基准赛（Go 原生引擎，v1.3 规则）

> 规则指纹 `c3a88788` ｜ 引擎 `native` ｜ 6 名选手 ｜ 45 场 / 90 局 ｜ 零故障 ｜ 90/90 确定性复核
> 规则：1200 tick ｜ 资源类型按镜像成对分配 ｜ 母舰脱战才自愈 ｜ 攻坚兵对舰倍率 2.5/2.0
> 完整数据：[`../algorithm-contest/tests/history-seasons/formal-v13/成绩.md`](../algorithm-contest/tests/history-seasons/formal-v13/成绩.md)、[`../algorithm-contest/tests/history-seasons/formal-v13/成绩_加权总分.md`](../algorithm-contest/tests/history-seasons/formal-v13/成绩_加权总分.md)

### 正式名次（Bradley–Terry）

| 名次 | 选手 | 实力值 | 95% 区间 | 胜/平/负 | 局数 |
|---:|---|---:|---|---:|---:|
| 1 | Nomad | 378.8 | 242.6 ~ 521.2 | 28/0/2 | 30 |
| 2 | Bulwark | 73.9 | -68.3 ~ 222.5 | 12/4/14 | 30 |
| 3 | Sapper | 73.9 | -67.7 ~ 211.7 | 14/0/16 | 30 |
| 4 | Starter | 55.6 | -33.1 ~ 170.8 | 9/8/13 | 30 |
| 5 | Swarm | 18.8 | -137.7 ~ 145.4 | 11/0/19 | 30 |
| 6 | Vanguard（锚） | 0.0 | 0.0 ~ 0.0 | 6/8/16 | 30 |

### 七领域加权总分

| 名次 | 选手 | 合计 | 场均 |
|---:|---|---:|---:|
| 1 | Nomad | 1430.7 | 47.69 |
| 2 | Swarm | 1419.0 | 47.30 |
| 3 | Starter | 1347.5 | 44.92 |
| 4 | Sapper | 1320.3 | 44.01 |
| 5 | Bulwark | 1290.9 | 43.03 |
| 6 | Vanguard | 1286.6 | 42.89 |

### 怎么读

- **Nomad 连续第三季 BT 第一**，且区间首次收窄到 ±140（3 个基准 seed、每对 6 局）。
- **v1.3 的公平性修复改变了名次结构**：v1.2 时 Starter 一度排第二，本季回到第 4。资源类型按镜像成对分配后，两侧经济结构相同，抢点型打法的座位运气成分消失。
- **Bulwark 与 Sapper 完全同分（73.9）**，区间大幅重叠——统计上无法区分，这正是加 seed 后的正确表现，不要拿来排座次。
- **Sapper（新增攻坚 Bot）拿到 2 名**，说明远程攻坚阵容在 1200 拍内能打出战绩，但整季仍然没有斩舰局：参考 Bot 的路径推进会在基地口被地形卡住（见根 README 已知边界 8）。
- **10 局平局**全部是 L5（VP、KD、母舰血量全同）——镜像换边下强对称对局的正常结果。

---

## 第 3 季 · 基准赛（Go 原生引擎，v1.2 规则）

> 仅冻结赛程，未跑完（v1.2 验收时发现公平性与审计缺陷，赛程作废，见 v1.3 变更记录）。
> 结果目录：`../algorithm-contest/tests/history-seasons/formal-v12/`（只有 `赛程.json`）。

---

## 第 2 季 · 基准赛（Go 原生引擎，v1.1 规则）

> 规则指纹 `9874da0a` ｜ 引擎 `native`（Go 原生，golden trace 回归通过）｜ 5 名选手 ｜ 20 场 / 40 局 ｜ 零故障 ｜ 40/40 确定性复核
> 规则：1200 tick ｜ 四子系统分配 / CU-RU 运输线经济 ｜ 七领域计分
> 完整数据：[`../algorithm-contest/tests/history-seasons/formal-v11/成绩.md`](../algorithm-contest/tests/history-seasons/formal-v11/成绩.md)、[`../algorithm-contest/tests/history-seasons/formal-v11/成绩_加权总分.md`](../algorithm-contest/tests/history-seasons/formal-v11/成绩_加权总分.md)

### 正式名次（Bradley–Terry）

| 名次 | 选手 | 实力值 | 95% 区间 | 胜/平/负 | 局数 |
|---:|---|---:|---|---:|---:|
| 1 | Nomad | 140.4 | -29.1 ~ 343.6 | 14/0/2 | 16 |
| 2 | Vanguard（锚） | 0.0 | 0.0 ~ 0.0 | 9/2/5 | 16 |
| 3 | Bulwark | -94.2 | -279.6 ~ 99.0 | 5/4/7 | 16 |
| 4 | Starter | -125.6 | -270.6 ~ 57.1 | 5/2/9 | 16 |
| 5 | Swarm | -226.1 | -441.5 ~ -23.6 | 3/0/13 | 16 |

### 七领域加权总分

| 名次 | 选手 | 合计 | 场均 |
|---:|---|---:|---:|
| 1 | Nomad | 782.1 | 48.88 |
| 2 | Starter | 726.2 | 45.39 |
| 3 | Vanguard | 721.7 | 45.11 |
| 4 | Bulwark | 700.0 | 43.75 |
| 5 | Swarm | 643.4 | 40.22 |

### 怎么读

- **Nomad 双榜第一**：BT 14 胜 2 负 + 总分 782，本季无争议的统治者；但 95% 区间仍有 -29 ~ 344，样本量是硬伤。
- **Swarm 崩盘**：第 1 季 BT 第三（7/0/9）→ 本季 3/0/13 垫底。v1.1 的运输线经济与四子系统分配重置了环境，Swarm 的爆兵流失去了边际优势。
- **Starter 两榜背离**：BT 第 4 但总分第 2——模板 Bot 打不赢关键局，但每局质量稳定。
- **Bulwark 仍是平局大师**：4 局 L5 平局全部出自它的对局（Vanguard–Bulwark、Bulwark–Starter 各 2 局），与第 1 季同款对称僵局。
- **仍然没有斩舰局**：40 局全部走满 1200 tick，参考 Bot 的集火与突防依旧不够狠。

### 遗留到下一季

1. 基准 seed 增到 4–6 个，每对选手 8–12 局，收窄 BT 区间（连续两季都是 ±300 级别）；
2. 给参考 Bot 补集火 / 撤退 / 突防，让"斩舰"成为真实威胁；
3. 开雾赛季（`vision=true`）单独排，不与全图赛季混排；
4. v1.1 规则下重标定参考 Bot 强度后，再引入真人/大模型选手。

---

## 第 1 季 · 基准赛（JS oracle 引擎，v1.0 规则）

> 规则指纹 `906f1b14` ｜ 5 名选手 ｜ 20 场 / 40 局 ｜ 零故障 ｜ 40/40 确定性复核
> 完整数据：[`../algorithm-contest/tests/history-seasons/formal/成绩.md`](../algorithm-contest/tests/history-seasons/formal/成绩.md)、[`../algorithm-contest/tests/history-seasons/formal/成绩_加权总分.md`](../algorithm-contest/tests/history-seasons/formal/成绩_加权总分.md)

### 正式名次（Bradley–Terry）

| 名次 | 选手 | 实力值 | 95% 区间 | 胜/平/负 | 局数 |
|---:|---|---:|---|---:|---:|
| 1 | Nomad | 175.3 | 32.4 ~ 395.0 | 13/1/2 | 16 |
| 2 | Vanguard（锚） | 0.0 | 0.0 ~ 0.0 | 7/2/7 | 16 |
| 3 | Swarm | -29.6 | -199.8 ~ 127.3 | 7/0/9 | 16 |
| 4 | Bulwark | -59.4 | -241.8 ~ 98.5 | 4/4/8 | 16 |
| 5 | Starter | -74.6 | -190.2 ~ 123.0 | 4/3/9 | 16 |

### 八领域加权总分

| 名次 | 选手 | 合计 | 场均 |
|---:|---|---:|---:|
| 1 | Nomad | 927.5 | 58.0 |
| 2 | Swarm | 926.8 | 57.9 |
| 3 | Starter | 884.1 | 55.3 |
| 4 | Vanguard | 870.2 | 54.4 |
| 5 | Bulwark | 859.8 | 53.7 |

### 怎么读（第 1 季存档）

- **两榜不一致是正常的**：`Starter` 总分第三、BT 垫底；`Swarm` 总分第二、BT 第三。总分高说明每局质量稳定，BT 低说明关键局没赢下来。
- **区间很宽**：40 局、5 人，每对选手只打 4 局。`Bulwark` 与 `Starter` 的区间几乎完全重叠，不能断言谁更强。
- **锚定选手恒为 0**：BT 以名册第一名为刻度原点；`Vanguard` 是 0 不代表最弱，它本季 7 胜 2 平 7 负，正好在中位。
- **5 局 L5 平局**：VP 完全相同的对称僵局。
- **没有斩舰局**：40 局全部走满 600 tick。
