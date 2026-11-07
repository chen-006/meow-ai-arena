# 社区比赛 · 峡谷对决（MOBA 5v5）

模仿《英雄联盟》的 5v5：**一个程序控制一支 5 人队伍**（上单战士、打野刺客、中单法师、射手、辅助），
在三路兵线上补兵、打野、推塔，摧毁对方水晶获胜。1 个程序也可以打单人挑战——对面是引擎控制的
NPC 队（英雄数量和强度、小兵波次、防御塔、野怪、地图大小都由难度决定）。

这是**社区自建的非官方比赛**，由观众 KananPond（@KananPond）提出玩法与难度分档，
不属于「AI 算法对抗大赛」的官方集数，赛制、成绩和名次都由本目录自己定义，也不进官方社区榜。
这里备齐了开办一场比赛所需的全部材料：规则、引擎、裁判、示例 bot、循环赛脚本、回放和成绩，
都可以在本地跑起来、自己复现。

## 规则速览（完整规则见 [`workspace/RULES.md`](workspace/RULES.md)）

- 41×41（普通）的旋转对称地图：三路车道 + 迷宫野区 + 两块基地。四方向移动，一格一回合。
- 五个职业各有普攻（射程 1–2）和一个冷却技能（回春 / 突袭 / 火球 / 穿云箭 / 治疗）。
- NPC：小兵每 12 回合从三路出兵，沿兵线推进、会互殴；每路 2 座防御塔会攻击射程 2 内的敌人；
  野区营地有中立野怪，击杀得经验；双方水晶在基地最深处。
- 击杀、补兵、清野都涨经验，升级 +12 血 +2 攻（最高 12 级）。英雄阵亡 14 回合后在泉水重生。
- 一局 700/900/1100 回合（按难度），摧毁对方水晶获胜；双方水晶同回合被毁时按团队 KDA 裁决；
  打满按剩余建筑血量比例判定。
- 循环赛胜 1 分、平 0.5 分；同分依次比较小循环积分和对手分，仍相同则并列。

## 难度由什么决定

| | 简单 | 普通 | 困难 |
|---|---|---|---|
| 地图 | 33×33 | 41×41 | 49×49 |
| 每波小兵（每路每队） | 2 | 3 | 4 |
| 小兵生命 / 攻击 | 55 / 14 | 70 / 18 | 85 / 22 |
| 防御塔生命 / 攻击 | 300 / 32 | 380 / 40 | 460 / 48 |
| 野怪营地（每侧） | 2 | 3 | 4 |
| 单人挑战的 NPC 英雄 | 3 个 ×0.8 | 4 个 ×1.0 | 5 个 ×1.25 |

5v5 对战时两队完全对称，难度只改变地图大小和公共 NPC 参数；单人挑战时 NPC 英雄的数量和强度也随档位增长。

## 本地运行

需要 Python 3.10+ 和 g++（Windows 的装法见[仓库根 README](../../../README.md)）。

在**仓库根目录**运行：

```bash
python community/contests/moba/challenge.py 你的bot.cpp            # 单人难度阶梯：简单 → 普通 → 困难，打引擎 NPC 队
python community/contests/moba/challenge.py 你的bot.cpp --pvp      # 再和 5 个示例 bot 各打两场（换边）
```

也可以进到本目录里跑，脚本内部都用相对路径，两种写法等价：

```bash
cd community/contests/moba
python challenge.py 你的bot.cpp
```

**看回放**：直接用浏览器打开 [`replays/`](replays/) 里任意一个 `.html`，逐回合播放。所有图形都是
SVG 现场画的：英雄是带汉字的圆盘（金环 = 技能就绪），小兵是方块，防御塔会随血量开裂，水晶是一颗大菱形。
[`replays/index.html`](replays/index.html) 是下拉切换的索引页。

**用本地裁判调试**（和正式比赛完全一样的结算），在本目录的 `workspace/` 下运行：

```bash
cd community/contests/moba/workspace
python arena.py play submission/bot.cpp baseline --seed 7          # 5v5 打基准
python arena.py play submission/bot.cpp --difficulty 困难          # 单人挑战困难档
python arena.py batch submission/bot.cpp baseline --games 8        # 连打多局看平均
```

Windows 上没有 `python` 命令时，把 `python` 换成 `.\run.cmd`（在 `workspace` 目录内）。

**跑循环赛**：把自己的 bot 放进 [`bots/`](bots/)，改 [`tournament/run_roundrobin.py`](tournament/run_roundrobin.py)
顶部的 `BOTS` 名单，然后：

```bash
cd community/contests/moba/tournament
python run_roundrobin.py --replays     # 循环赛 × 三档难度，成绩写入 ../results/
```

## 成绩

| 赛事 | 内容 | 结果 |
|---|---|---|
| [示例赛](results/示例赛-成绩.md) | 5 个示例 bot 循环赛 × 三档难度换边，共 60 局 | **均衡**、**速推**同为 21 胜 3 负并列第一，游走 12 胜 12 负第三 |
| [正式赛](results/正式赛-成绩.md) | 4 个真实模型 bot 循环赛 × 三档难度换边，共 3024 局 | kimi-k3.1-pro 1454 分第一，deepseek-v4.1-flash 1003 分第二，MiniMax-M3.1 464.5 分第三，doubao-seed-2.1-Lite 102.5 分第四 |

两场都是 `moba-1.3` 规则下的结果。示例赛是主办方代写的占位 bot 之间的表演赛，用来演示赛制；
正式赛是真实模型交卷之间的循环赛。逐局明细在 `results/*-对局.json` 里，每个 bot 的源码哈希都记在成绩表上。

示例 bot 的思路见 [`bots/README.md`](bots/README.md)。

## 目录

| 路径 | 内容 |
|---|---|
| `workspace/` | 选手包：规则、引擎、本地裁判、SVG 回放网页、`bot.hpp` 骨架、基准源码 |
| `prompt.md` | 发给模型的提示词（原文） |
| `bots/` | 5 个示例 bot（占位陪练）和 4 个真实模型交卷 |
| `results/` | 示例赛与正式赛的成绩表和逐局结果 |
| `replays/` | 精选回放（循环赛普通难度的对局）和索引页 |
| `tournament/` | 循环赛脚本（循环赛 × 三档难度，可复现） |
| `challenge.py` | 一键挑战（单人难度阶梯 + 挑战全部示例 bot） |
| `.gitignore` | 本地产物（编译缓存、回放副产物、运行缓存） |

## 如何复现

1. 把 `workspace/` 打包发给每个模型（里面没有基准源码，比赛时基准只给编译好的程序），发 `prompt.md` 原文，限时 60 分钟；
2. 每个模型的交卷放进 `bots/<模型名>.cpp`，改 `tournament/run_roundrobin.py` 顶部的 `BOTS`；
3. `python tournament/run_roundrobin.py --replays` 跑循环赛（每个对阵 × 三档难度 × 同种子换边），
   成绩自动写入 `results/`；同分判定和小循环、对手分数据会一并记录。

## 和官方各集的差异

- 对局单位是"队伍"（一个程序控 5 个英雄），单人挑战模式的对手是引擎内置 NPC 队，
  所以 `challenge.py` 除了循环赛还提供**难度阶梯**——地图更大、NPC 更多更强。
- 选手包不提供写好的 `bot.cpp`，只给 `bot.hpp`（结构体和协议常量）；最终交卷仍是一个自包含的 `bot.cpp`。
- 回放查看器全部资产都是 SVG 矢量图形，没有任何位图。

## 来源与说明

本比赛由观众 KananPond（@KananPond）提出玩法设计与难度分档，全部代码（引擎、裁判、回放、
示例 bot、挑战脚本）由 GLM-5.3 实现。示例 bot 为主办方代写的占位选手；正式赛成绩来自真实模型交卷。
这是社区自建的比赛，不属于官方集数，也不代表主办方的官方赛制。
