# 社区比赛 · 沙漠战争（War of Desert）

> **社区自建的非官方比赛**：本目录是「AI 算法对抗大赛」社区自建比赛「沙漠战争」的全部比赛材料，
> 不属于官方集数，赛制、成绩和名次都由本目录自己定义，也不进官方社区榜。玩法、规则与赛制由
> 组织方设计，引擎移植、参考 Bot、测试与文档由 AI（ZCode 智能体、TRAE/DeepSeek-V4.1-Flash）
> 在人类指导下参与编写。原工程还包含 `Dev/` 开发资料与 `algorithm-contest/` 选手包，本社区副本
> 只收录 `war_of_desert/` 目录本身；因此文中指向 `../Dev/`、`../algorithm-contest/`、
> `../裁判台.bat`、`../赛务.py` 的链接与命令在本副本中不可用，运行时以本目录内的命令为准。
>
> **状态：可开赛**（当前正式赛 = **v1.9.2 · 第 6 季**：在 v1.4 基线上叠加胜利分三本账、遗产物与研究舰、CEP／射界／炮塔、火控与五级军衔、人口上限 36，1500 tick；v1.0 第 1 季 / v1.1 第 2 季 / v1.3 第 4 季 / v1.4 第 5 季已完赛，见 [`LEADERBOARD.md`](LEADERBOARD.md)）。
>
> **规则演进**：v1.5 → v1.9.2 仍由 `GameCfg.Ruleset == "v15"`（`sim.RulesetV15`）开关承载，当前规则指纹见 `arena version`；v1.4 语义与指纹 `553785d4` 保持不变，作为历史基线。规则见 [`workspace/RULES_V15.md`](workspace/RULES_V15.md)，配置 [`tournament/formal_v19.json`](tournament/formal_v19.json)，714 局标定见 [`experiment-records/rule-experiments/exp-v191-700/`](experiment-records/rule-experiments/exp-v191-700/README.md)。
>
> **AI 参与声明**：本仓库的引擎移植、参考 Bot、测试与文档由 AI（ZCode 智能体、TRAE/DeepSeek-V4.1-Flash）在人类指导下参与编写；
> 规则设计、赛制与验收由人类主导，全部对局结果均经引擎内置的确定性复核门禁（golden trace + orderLog 重放）。
>
> **赛事手册**：[`赛事手册.md`](赛事手册.md) ｜ **规则基线（v1.0 历史）**：原工程 `Dev/Plan_Beta.md` 快照（未随本副本提供）
> **引擎双轨**：`engine.kind` 二选一——`oracle`（只读快照 `MVP@288db07f`，`engine/oracle_host.js` 承载，校验 `python -X utf8 scripts/verify_oracle.py`）
> 或 `native`（**Go 原生引擎**：规则语义在 `internal/sim`，宿主 `cmd/enginehost`，当前赛季配置 [`tournament/formal_v19.json`](tournament/formal_v19.json)）
> **执行链**：Go 标准库，零第三方依赖。当前正式赛规则 = **v1.9.2**（ruleset `v15` / 1500 tick / 胜利分三本账 / 遗产与研究舰 / CEP 与射界 / 火控 C1–C3 / 五级军衔 / 七领域计分）

---

## 这个目录是什么

本目录是这场比赛的材料目录：能开赛、能判罚、能出成绩。下面所有命令都在
`community/contests/war-of-desert/` 内执行。

- 选手交一个 Go 程序，官方编译、封存源码哈希；
- 裁判为每个座位拉起独立进程，执行 120 ms 决策死线、连续 3 次红牌、超时 kill 与重启、换边重开；
- 赛程先冻结再执行，赛后产出逐局结果、完整回放、确定性复核、BT 实力值与七领域加权总分；
- v1.0 赛季的规则语义来自冻结快照 oracle；**v1.1 起规则引擎为 Go 原生**
  （移植自 MVP 合并版，golden trace 回归：`engine/golden/` + `internal/sim/golden_test.go`）。

第 1 季基准赛结果：5 名选手、20 场、40 局、零故障、40/40 确定性复核通过。

```text
第 1 季（oracle，600 tick）BT：Nomad → Vanguard（锚）→ Swarm → Bulwark → Starter
第 2 季（native，1200 tick）BT：Nomad → Vanguard（锚）→ Bulwark → Starter → Swarm
第 4 季（native，v1.3，6 人）BT：Nomad → Bulwark → Sapper → Starter → Swarm → Vanguard（锚）
第 5 季（native，v1.4，6 人）BT：与第 4 季逐局相同（母舰削弱未被任何对局触及）
```

两季 Nomad 都是 BT 第一；v1.1 运输线经济重置环境后 Swarm 从第 3 跌到第 5。
两榜不一致是设计目标：胜负与质量是两条独立信号。详见 [`LEADERBOARD.md`](LEADERBOARD.md)。

**开发与历史资料在原工程的 `Dev/` 目录**：MVP 规则源码（2026-11-13 已解冻）、原作调研、Plan/ORACLE、历次审查报告、第三方参照模板。它们不参与正式赛执行，也未随本社区副本提供。

---

## 快速开始

原完整工程根目录的裁判台（`裁判台.bat` / `referee_desk.py` / `赛务.py`）与 `algorithm-contest/`
选手包不随本副本提供；本目录自带等价的组织方菜单与 Go 命令：

```bash
python -X utf8 start.py          # 交互菜单：build / plan / run / rate / check / selftest / season / status
python -X utf8 start.py season   # 整季流水线：build → plan → run → rate
```

在**本目录**执行：

```bash
CFG=tournament/formal_v19.json   # 当前赛季（v1.9.2）
go run ./cmd/arena selftest -config $CFG   # 验证超时 kill / 崩溃重启 / 非法结构红牌
go run ./cmd/arena build    -config $CFG   # 编译 2 个基线（baseline1 中等 / baseline2 极强）并封存源码哈希
go run ./cmd/arena plan     -config $CFG   # 冻结赛程 → results/formal-v19/赛程.json
go run ./cmd/arena run      -config $CFG   # 双基线 17 场 / 34 局（加选手后按名册扩展）→ 结果.jsonl + 回放（开赛前校验封存）
go run ./cmd/arena rate     -config $CFG   # BT + 加权总分 → 成绩.md / 成绩_加权总分.md / 成绩.json
```

单项自检（选手包默认入口）：

```bash
go run ./cmd/arena check -config tournament/formal_v19.json -seed 20261109
```

观赛：把 `results/*/replays/match-NNN.json` 拖进 [`回放查看器.html`](回放查看器.html)
（零依赖单文件，浏览器直接打开；地形 / 单位 / VP / 记分 / 终局复核徽标）。

3D 观赛：[`回放查看器3D.html`](回放查看器3D.html) 把同一份回放渲染成卡拉克沙漠风格的三维战场
（Three.js；沙丘 / 台地 / 干河谷 / 盐沼、程序化地面舰队、VP 信标、跟随镜头与战术地图）。
它从 CDN 取 Three.js，建议用本地 HTTP 服务打开：

```bash
python -m http.server 8777     # 在本目录执行
# http://127.0.0.1:8777/回放查看器3D.html
```

原工程在 `refs/kharak/` 放了《家园：卡拉克沙漠》的游戏截图，仅作三维回放的视觉风格参考，
不参与运行时渲染；为不在公开仓库分发第三方版权素材，本社区副本未收录这些截图，只保留
[`refs/kharak/SOURCE.md`](refs/kharak/SOURCE.md) 的来源与版权说明。

---

## 攻守对战（实验）

v1.0 之外的独立变体：一方守擂、一方进攻，守方有工事、攻方没有；VP 改为矿场，胜负只看母舰是否被摧毁。
规则与协议见 [`赛事手册_攻守.md`](赛事手册_攻守.md)，标定过程见 [`experiment-records/offense-defense-experiments/siege/结论.md`](experiment-records/offense-defense-experiments/siege/结论.md)。

**当前采纳参数**（40 局/档 + 样本外验证标定）：

| 参数 | 值 |
|---|---|
| 母舰 HP | 1250（双方相同） |
| 守方工事预算 | 16（5 炮塔 + 1 墙） |
| tickLimit | 1260（63 s） |
| 攻方胜率 | 45.0%（80 局）/ 46.0%（100 局），0 故障 |

只读快照 `engine/oracle/rules.js` 未改动；攻守规则在 `engine/oracle/rules_siege.js`（`MANIFEST_siege.json` 记哈希），
旧赛季仍按 `rulesHash=906f1b14` 复现。

```bash
# 参考 Bot
go build -o bots/bin/attacker.exe ./workspace/siege/attacker
go build -o bots/bin/defender.exe ./workspace/siege/defender

# 单局 / 批量标定（默认就是上面的参数）
go run ./cmd/siege -smoke -seed 20261201
go run ./cmd/siege -seeds 20261201-20261240 -workers 4 -out experiment-records/offense-defense-experiments/siege/run.csv

# 契约自检：三模式（obey / neutral / aggressive）的移动与索敌边界
node engine/oracle/selftest_modes.js
```

**BT 评分**：攻守不对称，用攻/守双参数模型，不是单值实力值。

```bash
# 编一组风格不同的参考 Bot（同一份源码，-ldflags 注入变体）
for v in v1 v2 v3 v4; do
  go build -ldflags "-X desertarena/workspace/siege/siegebot.Variant=$v" \
    -o bots/bin/pool_$v.exe ./workspace/siege/pool
done

# 循环赛 + 拟合（有序对 × seed 各一局）
go run ./cmd/siegepool \
  -pool "v1=bots/bin/pool_v1.exe,v2=bots/bin/pool_v2.exe,v3=bots/bin/pool_v3.exe,v4=bots/bin/pool_v4.exe" \
  -seeds 20261301-20261316 -workers 4 -out experiment-records/offense-defense-experiments/siege/bt_pool

# 只重算评分，不重跑对局
go run ./cmd/siegepool -from experiment-records/offense-defense-experiments/siege/bt_pool.csv -out experiment-records/offense-defense-experiments/siege/bt_pool
```

口径见 [`赛事手册_攻守.md`](赛事手册_攻守.md) §10，结果见 [`experiment-records/offense-defense-experiments/siege/bt_pool.md`](experiment-records/offense-defense-experiments/siege/bt_pool.md)。

---

## 当前正式赛 · v1.9.2

v1.9.2 是当前正式赛口径（v1.9.1 基础上修复运货死锁），规则由 `GameCfg.Ruleset == "v15"`（`sim.RulesetV15`）承载，
配置 [`tournament/formal_v19.json`](tournament/formal_v19.json)，规则实现 `internal/sim/rules15.go`，
完整规则见 [`workspace/RULES_V15.md`](workspace/RULES_V15.md)。v1.4 语义与指纹 `553785d4` 保持不变，作为历史基线。

- 胜利分：第 1/2 件遗产各 +50（上限 100），第 3 件起 0 VS 但推进遗产科技；击杀 `min(造价×2/3,200)×军衔系数（满级 ×1.6）`；插旗 0 分；被拔旗 −25，攻方 +25（同 VP 一次）；库存遗产分随旗毁撤回。
- 新单位 **研究舰（RESEARCH）**：无攻击、80 HP、100 CU / 25 tick；停在己方旗下研究 R1–R4 遗产科技；拔旗/舰毁中断研究。
- 遗产科技 R1–R4：60 / 85 / 110 / 135 tick，提供伤害/精度/装甲/射程（R4 为全体单位射程 +15%、伤害 +15%）；单旗库存 >5 件给过载伤害（每多 1 件 +2%，封顶 +20%）。
- v1.8 系增量：人口上限 36、参考 Bot 全线 Push、RAILGUN 240/40、ARTILLERY 550/180，
  两者对母舰伤害 ×1.5；时间到判定链只保留 VS → KD → 母舰 HP → 旗控制 → 平。
- v1.8.3 关键修复：neutral 模式的「高差据守」只在敌人进入自身射程时生效。
- v1.9：新增第 7 个参考 Bot **Warden**（RAILGUN + ARTILLERY + CARRIER）；遗产补给 70 tick / 场上 3 件。
- 1500 tick；预制 Bot 名册已合并为 **2 个基线**：baseline@1（中等战术，refbot 攻城剖面）/
  baseline@2（极强，acebot 集火/微操/护旗/劫旗/遗产竞速，多轮调试见
  [`experiment-records/bot-experiments/baseline-tuning/`](experiment-records/bot-experiments/baseline-tuning/README.md)）；
  旧 7 参考 Bot 归档在 [`experiment-records/reference-bots-archive/`](experiment-records/reference-bots-archive/README.md)。
  v1.9.2 指纹 `077336ff`（v1.9.1 为 `393ee404`，运货死锁修复后变更），v1.4 指纹仍是 `553785d4`。

```bash
CFG=tournament/formal_v19.json
go run ./cmd/arena build -config $CFG
go run ./cmd/arena plan  -config $CFG
go run ./cmd/arena run   -config $CFG
go run ./cmd/arena rate  -config $CFG
python -X utf8 scripts/exp_v16_metrics.py results/formal-v19
```

标定数据见 [`experiment-records/rule-experiments/exp-v191-700/README.md`](experiment-records/rule-experiments/exp-v191-700/README.md)（v1.9.1 定稿）与
[`experiment-records/rule-experiments/exp-v19-700/README.md`](experiment-records/rule-experiments/exp-v19-700/README.md)（v1.9.0）。
v1.9.1：714 局、平局 4.2%、决胜座位 49.9:50.1、母舰斩杀 7.1%、平均 1139 tick、
Warden BT 第 4；同 Bot 双座位 80 局送达差 t=1.11（不显著）。

---

## 目录

原工程的目录结构如下，本社区副本只包含其中的 `war_of_desert/`（即本目录）：

```text
社区比赛__WoD/
├── war_of_desert/            ← 正式比赛（本文件所在目录）
│   ├── README.md            本文件
│   ├── 赛事手册.md           正式赛制、流程、判罚、申诉、运营清单
│   ├── LEADERBOARD.md       两季榜单与读法
│   ├── 回放查看器.html       零依赖回放播放器（拖入 match-NNN.json 观赛）
│   ├── 回放查看器3D.html     Three.js 三维回放（卡拉克沙漠风格）
│   ├── refs/kharak/          参考图来源与版权说明（截图未随本副本收录）
│   ├── go.mod
│   ├── cmd/
│   │   ├── arena/           build / plan / run / rate / check / selftest
│   │   ├── siege/           攻守：单局 smoke + 母舰 HP 扫描
│   │   ├── siegepool/       攻守：循环赛 + 双参数 BT 评分
│   │   └── faultbot/        自检用故障注入 Bot
│   ├── internal/
│   │   ├── sim/             Go 原生规则引擎（rules15.go = v1.9.2 实验通道；v14 语义不变）
│   │   ├── protocol/        hello / tick / actions / end 的线上契约与地图解码
│   │   ├── engine/          引擎宿主客户端（行分隔 JSON）
│   │   ├── judge/           进程隔离、死线、红牌、重启、回放、确定性复核
│   │   ├── schedule/        赛程冻结（换边、seed 递增）
│   │   ├── rating/          Bradley–Terry（v1.0 单值）+ 攻守双参数
│   │   └── contest/         配置加载与路径解析
│   ├── engine/
│   │   ├── oracle_host.js   宿主：命令流 → 快照 oracle（可按参数加载规则变体）
│   │   └── oracle/          只读快照 + MANIFEST.json + rules_siege.js
│   ├── tournament/formal.json       第 1 季（v1.0，历史）
│   ├── tournament/formal_v11.json   第 2 季（v1.1，历史）
│   ├── tournament/formal_v12.json   第 3 季（v1.2，赛程作废）
│   ├── tournament/formal_v13.json   第 4 季（v1.3）
│   ├── tournament/formal_v14.json   第 5 季（v1.4，历史）
│   ├── tournament/formal_v19.json   第 6 季（v1.9.2，当前正式赛）
│   ├── tournament/experiment_v15.json  v1.5 实验（历史，1500 tick）
│   ├── tournament/experiment_v19_700.json  v1.9.1 714 局标定配置
│   ├── workspace/           引擎侧参考工作区（Starter 选手 + botkit/refbot/acebot 共用库）
│   │   ├── bots/baseline1/  基线一：中等战术（refbot 攻城剖面）
│   │   ├── bots/baseline2/  基线二：极强（acebot 集火/微操/护旗/劫旗/遗产竞速）
│   │   └── siege/           攻守参考 Bot：siegebot 库 + attacker / defender / pool
│   ├── bots/                封存记录与编译产物（bin 不入库）
│   ├── results/             当前与今后的正式赛事产物
│   ├── experiment-records/            规则实验归档（README = 实验日志索引）
│   │   ├── rule-experiments/        exp-v15 → exp-v191-700
│   │   ├── reference-bots-archive/     旧 7 参考 Bot 源码与 exe（v1.9.1 名册）
│   │   ├── bot-experiments/         baseline-tuning 多轮调试记录
│   │   ├── offense-defense-experiments/siege/  HP 扫描、工事预算标定、BT 榜单与结论
│   │   └── temp-diagnostics/        座位/公平性短跑，结论已并入正式实验
│   ├── notes/               赛前狠话、赛后复盘（预留）
│   ├── start.py             组织方菜单（默认走当前赛季 formal_v19.json）
│   ├── 开赛.py               旧版计时脚本，已被原工程根目录 赛务.py 取代（保留备查）
│   └── workspace/clock.py   选手侧计时展示
└── Dev/                     开发/历史资料区（未随本副本提供）
```

`Dev/` 与 `algorithm-contest/` 不随本副本提供；上面的目录树保留原工程结构，仅作索引。

---

## 架构

```text
cmd/arena ──► judge ──► engine(宿主客户端) ──► node engine/oracle_host.js
                 │                                   │
                 ├──► Bot 进程 A（stdin/stdout）      └──► engine/oracle/rules.js（只读）
                 └──► Bot 进程 B（stdin/stdout）
```

- **规则唯一来源**：`engine/oracle/` 是冻结提交 `288db07f` 的 `rules.js / mapgen.js / mapgen_mc.js / ballistics.js` 快照，SHA-256 记在 `MANIFEST.json`。宿主不修改规则，不读墙钟，不写文件。
- **进程边界**：引擎宿主、每个 Bot 都是独立操作系统进程；裁判用真实管道收发 UTF-8 行分隔 JSON。Bot 拿不到裁判内存，也无法通过原型链改规则。
- **失败隔离**：Bot 超时/崩溃 → kill → 重启 → 重发 hello；引擎宿主异常 → 整场失败并记录，不产出半截成绩。
- **未来替换**：Go 引擎实现后，只需替换 `engine/oracle_host.js`，`internal/*`、选手包与成绩契约不变。

### 与 `Plan_Beta.md` 的关系

- 引擎迁移验收（§5.2/§6.2）三件套已就位并通过：Go 自确定性（裁判逐局重放复核，第 2 季 40/40）、golden trace 回归（`engine/golden/` + `internal/sim/golden_test.go`）、绝对锚点与 JS 契约测试（`internal/sim/sim_contract_test.go` / `game_test.go`）；
- 第 1 季的 `hello` 113 KB 超 64 KiB 是 §11 已登记偏差，v1.1 原生引擎的 hello 是否收窄见 `internal/protocol`；
- 赛制层（BT、赛程、收卷、榜单）两季均已落地。

---

## 产物契约

以当前赛季目录 `results/formal-v19/` 为例（历史赛季目录结构相同，v1.0 为八领域）：

| 文件 | 内容 |
|---|---|
| `results/formal-v19/赛程.json` | 冻结的选手、场次、seed、规则与判罚口径 |
| `results/formal-v19/结果.jsonl` | 每局一行：场次、座位、胜者、层级、VP、七领域总分、故障、确定性、规则指纹 |
| `results/formal-v19/成绩.md` | BT 实力值、95% 区间、胜平负、总分 |
| `results/formal-v19/成绩_加权总分.md` | 七领域合计与场均 |
| `results/formal-v19/成绩.json` | 机器可读的 BT 结果 |
| `results/formal-v19/replays/match-NNN.json` | 原始 hello + 逐局终局 + 每 5 tick 帧 + 复核结果 |
| `bots/seal-records/<赛季>.json` | 该赛季选手源码与共用库的 SHA-256、编译产物路径（一季一本） |

---

## 已知边界

1. **两代引擎并存**。第 1 季成绩由 JS oracle 引擎产出，复现需 Node 运行时；第 2 季起 `engine.kind=native`（Go 原生，`internal/sim`），oracle 仅作 v1.0 历史复现与对照。
2. **hello 约 110 KB**。v1.3 契约单行上限 128 KiB，hello 实测约 110 KB，在契约内。
3. **视野关闭**。`rules.vision=false`，全图可见；开雾需要另跑一个赛季，不能与本赛季混排。
4. **参考 Bot 只用于基准**。它们只有流场寻路和简单交战规则，不代表规则上限；第 1–5 季无人斩舰，属选手强度不足，不是规则问题。v1.3 起「斩舰」在数值上可达（`internal/sim/victory_test.go` 用 3 台 RAILGUN 在 25 格距离验证），但参考 Bot 的推进仍会在基地口被地形卡住。
5. **回放不入库**。`results/*/replays/` 与 `experiment-records/*/*/replays/` 可由 `arena run` 重建；`bots/bin/` 同理。
6. **攻守模式仍是实验**。规则、协议、标定都齐了，但缺选手包、许可、回放查看器与沙箱，不能直接当正式赛季。
7. **攻守的 BT 样本偏少**。每对 16 局时头名区间仍有 ±117 分；出正式榜单建议每对 ≥40 局（4 人池约 480 局）。
8. **母舰削弱在 v1.4 对局中不可感知**。v1.4 把母舰 HP/攻击/移速/坡限全部下调，但第 5 季 90 局与第 4 季逐局完全相同——参考 Bot 既不把兵送进敌方母舰射程，也不指挥自家母舰移动。v1.9.1 通过远程兵降价与对舰倍率把斩杀率提到 7.1%（714 局中 51 局），削弱本身有门禁保证（`mobility_test.go`：母舰 400 拍内必须能驶出基地 60 px）。
9. **参考 Bot 的攻坚能力有限但已可斩舰**。v1.3 修掉了「出生即被困」与「建造轮转饿死高价兵」两个缺陷；v1.9.1 的 Warden / Hunter 能稳定完成部分斩舰，但推进仍可能被基地周边地形卡住。斩舰要成为赛季常态，还需要选手侧的真实攻坚打法。
10. **沙箱只覆盖进程层，且只支持 Windows**。Windows Job Object 挡住子进程派生、内存爆炸与孤儿进程，工作目录是一次性临时目录；**文件读权限与出网没有隔离**，需要独立低权账户或容器。非 Windows 平台没有等价沙箱，裁判会**拒绝开赛并报错**，而不是无沙箱裸跑或伪造平局记录。手册 §3.2 已写明这一点。
11. **规则指纹=规则身份**。改动任何影响对局语义的代码都必须递增 `sim.RulesVersion`，否则 `rulesHash` 不会变，历史成绩就失去可区分性。指纹本身补齐遗漏常量后，`rulesHash` 也会变化，旧成绩需要用旧版本引擎复核。

## 禁止事项

- 不修改 `engine/oracle/` 快照；发现疑似缺陷先登记到原工程 `Dev/Plan_Beta.md` §11。
- 不在裁判进程内跑选手代码；进程隔离是硬约束。
- 不把 `赛程.json` 当草稿改；要改就重新 `plan` 并注明原因。
- 不在未更新 `赛事手册.md` 的情况下改判罚或计分口径。
