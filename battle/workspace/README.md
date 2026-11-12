# 纸上谈兵 · 选手包

## 比什么

每一场你拿一笔军费买兵、在布阵区里摆好，再写一个 C++ 指挥程序指挥他们。开打以后谁都不能插手，战斗在真实的刚体物理里算出来：会被打飞、撞倒、掉下悬崖、被自己人的箭射中。

- **五关闯关**：打主办方的守军和 Boss，每关的过关条件不一样（全歼来敌、全歼且守住军旗、攻下炮台、护着主帅全歼峡谷守军、打倒 Boss，详见 `RULES.md` 第 7 节）。**先要过关；过关以后比谁花得少**：每关全场最省的人 100 分，你比他每贵一倍扣 25 分，过关最少 20 分，没过 0 分（详见 `RULES.md` 第 2 节）。
- **对抗**：和其他选手盲摆对打。1v1 一份布阵打两张图，四人混战一份布阵。
- 总分 = 闯关分 × 60% + 对抗分 × 40%。完整规则在 `RULES.md`，指挥程序的接口在 `COMMANDER.md`。

**结果完全确定**：没有随机数，每场只打一次。本地的 `war.exe` 就是裁判用的同一个模拟器，本地跑出什么，正式就是什么。可以无限次试。

## 交什么

全部放在 `submission/`，文件名固定：

| 文件 | 内容 |
|---|---|
| `level1.json` … `level5.json` | 五关的布阵 |
| `duel.json` | 1v1 的布阵（两张图都用它） |
| `melee.json` | 四人混战的布阵 |
| `commander.cpp` | 指挥程序，所有场次共用一个 |
| `notes.md` | 交卷说明（RULES.md 第 9 节） |

现在 `submission/` 里已经放好了能交的起步版本：每份布阵 1 个新兵（合法，但过不了关），`commander.cpp` 什么都不做。某一份没交或不合法，只是那一项 0 分，不影响别的。到点时以文件当时的内容为准，所以别让文件长时间停在不合法或编译不过的状态。

## 先跑起来（两分钟）

```
python clock.py                    看已用和剩余时间
python play.py check               检查 submission/ 里的 7 份布阵，并编译指挥程序
python play.py map 1               在终端里画第 1 关：地形、布阵区、敌军和道具的开场位置（我方视角）
python play.py level 1             跑第 1 关：过没过、用时、造价、双方剩多少、这一关的关键数
python play.py replay replays/level1.wrp --at 10    看这一场的经过，并画第 10 秒的战场
```

然后改 `submission/level1.json` 再跑，直到过关，再往下压造价。

## 命令

所有工具只要 Python 3（标准库），在本目录运行。

| 命令 | 作用 |
|---|---|
| `python play.py check` | 检查 7 份布阵是否合法（造价、人数、格点、布阵区、重叠），编译指挥程序 |
| `python play.py level N [布阵.json] [--cmd 程序]` | 跑第 N 关一场（不到一秒到几秒）。默认用 `submission/levelN.json` 和 `submission/commander.cpp`；`--cmd -` 不用程序，`--cmd 别的.cpp` 换程序。退出码 0 过关、1 没过、3 布阵不合法、4 引擎超时 |
| `python play.py levels` | 五关各跑一场，打一张总表 |
| `python play.py duel [--opp 名字,…] [--map 樱丘]` | 1v1：`submission/duel.json` 对每支陪练打 4 场（樱丘、河心洲 × 两个座位），按正式算法给本地 1v1 分。`--opp` 也可以写 `别的布阵.json` 或 `布阵.json:程序.cpp`，拿来和自己的旧版本打 |
| `python play.py melee [--opp 甲,乙,丙]` | 四人混战：`submission/melee.json` 和三支陪练打 4 场（你轮流坐 4 个座位），给本地混战分 |
| `python play.py map N` | 画地图。N 可以是 1～5、`樱丘`、`河心洲`、`涨潮沙洲`；同时列出敌军和道具的开场坐标 |
| `python play.py replay 回放.wrp [--seat K] [--at 秒,秒] [--deaths]` | 讲一场战斗的经过：每 10 秒双方人数、关键事件（桥断、雪球、爆炸、守军出击、改道的敌军出发、镇山兽换状态、全灭）、你的兵怎么死的、谁杀的人、误伤；`--at` 画某几秒的战场，`--deaths` 列出你每个兵的死因。对抗回放用 `--seat` 选你坐的座位（`play.py duel` 的输出里写了） |
| `python build.py [文件.cpp]` | 只编译（`play.py` 发现源码改了会自动重编） |
| `python clock.py` | 看时间 |

每跑一场，回放都存进 `replays/`（同一关、同一组对局只留最新一份）。想批量试很多种布阵，可以自己写脚本调 `war.exe batch`（见 `COMMANDER.md` 第 5 节），或者循环调 `play.py level`。

## 文件夹里有什么

| 路径 | 是什么 |
|---|---|
| `RULES.md` | 完整规则：计分、兵种、通用规则、五关、对抗 |
| `COMMANDER.md` | 指挥程序接口：能看到什么、能下什么指令、时间限制、直接用 war.exe |
| `war.exe` | 模拟器（就是裁判用的那一份） |
| `levels/第N关_*.json` | 五关的关卡文件：地形、布阵区、预算、敌军、道具、过关条件，全部公开 |
| `maps/` | 对抗图：`樱丘.json`、`河心洲.json`（1v1），`涨潮沙洲.json`（混战） |
| `commander/commander.hpp` | 指挥程序的头文件 |
| `examples/示例指挥.cpp` | 一个能跑的示例程序：点名、架枪、骑兵冲撤、射火药桶、推雪球、读镇山兽状态、指挥主帅 |
| `examples/levelN_示例.json` | 各关的示例布阵：只演示格式和布阵区，过不了关 |
| `sparring/` | 对抗用的陪练：`duel/`、`melee/` 是布阵，`陪练.json` 写了每支用哪个程序 |
| `submission/` | 你的交卷文件 |
| `play.py`、`build.py`、`clock.py`、`clock.json` | 本地工具和时钟 |
| `build/`、`replays/` | 运行后自动生成 |

## 建议的顺序

1. 读 `RULES.md`（第 2 节计分、第 4～5 节兵种和通用规则），扫一眼 `COMMANDER.md`。
2. 每关先 `python play.py map N` 看地图，再读 RULES.md 里这一关的小节和关卡文件。
3. 先让每一关都有一份能过的布阵，再回头压造价：过了的关，少一个兵就是多几分。
4. 有的关只靠布阵文件（`原地守`）也有机会过，有的关不写程序过不了（分兵、绕路、点名、掐时机都要程序）。
5. 对抗：`duel.json` 要在两张图上都站得住；多和几支陪练、和自己的旧版本打。
6. 交卷前：`python play.py check` 全部合法、`python play.py levels` 看一遍，写 `submission/notes.md`。
