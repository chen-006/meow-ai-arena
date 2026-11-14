[English](README_en.md)

# 第 6 集 · 坦克大战

回到写 bot 对战。14 个模型各写一个 C++ 坦克大战 bot，1 对 1 循环赛。这一集分上下半场：上半场写第一版，所有人的第一版先打一轮；中场每位选手拿到成绩表、自己的全部对局回放和其他选手第一版的可执行程序，再用下半场改进。最终名次只看下半场的版本。三场比赛一共打了 27840 局。

- **目录里有什么**：`workspace/` 选手包 · `bots/half1/`、`bots/half2/` 上下半场代码 · `results/` 中场赛、终场赛、进步赛的成绩和逐局结果 · `replays/` 回放 · `tournament/` 对战平台和三场比赛的配置 · `notes/` 赛前狠话和复盘 · `challenge.py` 一键挑战
- **复现榜单**：在 `tournament/` 下运行 `python tournament.py plan final.json`、`run run-final`、`rate run-final`，终场约 70 分钟，见 [`tournament/README.md`](tournament/README.md)。
- **出视频画面**：`renderer/` 把一局回放渲成视频里的对局画面，见 [`renderer/README.md`](renderer/README.md)。

## 规则速览（完整规则见 `workspace/RULES.md`，精确实现是 `workspace/tank.hpp`）

- 15×15 地图，地形有空地、砖墙、钢墙、水。1 对 1，每方一个基地（24 血）和三辆坦克：快速型（1 血，一回合走 2 步）、火力型（1 血，不用装填）、装甲型（2 血）。
- 每回合双方同时给三辆车各下一条命令（不动、移动、开炮），子弹每回合飞 2 格。子弹不分敌我，打到自己的基地也扣血；基地每回合最多扣 1 血。
- 被毁的车 3 回合后在出生点复活；复活时压在出生点上的敌车直接被毁。第 10、30、50……回合末在补给点出星，吃一颗升一级（加血、免装填或多一发子弹，2 级子弹能打掉钢墙）。
- 把对方基地打到 0 血获胜；300 回合没分出胜负，依次比基地血量、击毁数、吃星数。对局里没有随机，也没有隐藏信息。
- 选手只写 `init` 和 `decide` 两个函数，引擎（含精确结算一回合的 `tank::step`）以头文件 `tank.hpp` 给出。思考预算是程序自己消耗的 CPU 时间：首回合 2000 毫秒，之后每回合 50 毫秒；连续 3 次超预算或命令不合法判负。

## 赛制

| 阶段 | 内容 |
|---|---|
| 上半场 | 每个模型在空文件夹里拿到 `workspace.zip`（即本目录的 `workspace/`，当时基准只给编译好的程序）和同一段提示词（`prompt.md`）。解压时开始计时：30 分钟，最早第 20 分钟可以交卷，到 45 分钟强制收卷。到点时 `submission/bot.cpp` 是什么就算什么 |
| 中场赛 | 14 个上半场版本、基准、出题方参照（`sparring`，不排名）共 16 个程序，每张图上每一对换边各打一局，42 张图，10080 局 |
| 中场包 | 每位选手在原会话里收到同一条消息和自己的中场包：全员成绩表、自己的 1260 局回放、其他 13 位选手上半场版本和基准的可执行程序（没有源码，出题方参照不发） |
| 下半场 | 解压中场包时开始计时，时间规则和上半场相同。最终名次只看下半场交上来的版本 |
| 终场赛 | 14 个下半场版本 + 基准 + 出题方参照，60 张新图，14400 局，决定最终名次 |
| 进步赛 | 每位选手的下半场版本去打"全员上半场版本 + 基准"，8 张新图，3360 局，用来衡量各自进步了多少，不影响名次 |

实力值用 Bradley–Terry 拟合：胜 1、平 0.5，基准 = 0，强 10 倍记 +400。区间按地图整块重抽样 1000 次。相邻两名"实力值之差"的 95% 区间含 0 时记为并列。

## 选手

| 模型 | 短名 | 工具（版本） | 渠道 | 思考档 | 在哪台机器上跑 |
|---|---|---|---|---|---|
| GPT-6 Astra | Astra | Codex 桌面版 0.162.0-alpha.17.2 | OpenAI 官方订阅（官方客户端） | medium | 比赛电脑 |
| GPT-6.1 Sol | Sol | Codex 桌面版 0.162.0-alpha.17.2 | OpenAI 官方订阅（官方客户端） | medium | 比赛电脑 |
| GPT-6.1 Sol（Ultrafast） | SolUF | Codex CLI 0.162.0-alpha.17.2 | OpenAI API 超快档 | medium | 外部机器 |
| Gemini 4 Argon Contributor | Gemini | OpenCode 2.0.29 | OpenCode Go | 工具默认（variant: default） | 外部机器 |
| Claude Opus 5.5 | Opus | Claude Code 2.1.296 | Anthropic 官方订阅（官方客户端） | medium | 比赛电脑 |
| Claude Haiku 5.5 | Haiku | Claude Code 2.1.296 | Anthropic 官方订阅（官方客户端） | medium | 比赛电脑 |
| Muse Wonder Full | Muse | Muse Code CLI 1.4.7 | Meta | high（工具默认） | 外部机器 |
| 小米 MiMo V2.6 Pro | MiMo | OpenCode 2.0.19 | Cline 通行证 | 工具默认 | 比赛电脑 |
| Grok 4.7 | Grok | Grok Build 1.0.46 | 经账号池调度（地址不公开） | 工具默认 | 比赛电脑 |
| DeepSeek V4.1 Flash | DeepSeek | dsh（版本未记录） | Cline 通行证 | high | 比赛电脑 |
| Doubao-Seed-Evolving | Doubao | OpenCode 2.0.19 | 火山引擎 | 工具默认 | 比赛电脑 |
| Kimi K3 | Kimi | Kimi Code CLI 2.1.1 | Cline 通行证 | high | 比赛电脑 |
| 通义千问 Qwen 3.8 Max | Qwen | Qwen Code CLI 0.24.7 | Cline 通行证 | 工具默认（没有设置） | 比赛电脑 |
| 智谱 GLM 5.3 | GLM | ZCode 桌面版 0.16.9 | Cline 通行证 | max | 比赛电脑 |

- 思考档没有统一设置，用的是各工具的默认值，表里是会话记录里能查到的档位。
- **外部机器**：SolUF、Gemini、Muse 三位由外部操作员在另一台电脑（AMD Ryzen 7 9800X3D）上跑，比比赛电脑（Intel Core i9-13900HX）快，同样的时间里本地能打更多局。主办方没有给这三位计时，用时是操作员的记录，bot 由主办方登记收下。最终强度都在比赛电脑上统一评测。
- 每个半场的用时、补时、事故见 **`CONDITIONS.md`**。

## 最终成绩（终场赛，下半场版本）

| 名次 | 选手 | 实力值 | 95% 区间 | 胜-平-负 | 得分率 | 中场赛（上半场版本） |
|---:|---|---:|---|---|---:|---:|
| 1 | GPT-6 Astra | 507 | 468 ~ 550 | 1556-4-240 | 86.6% | 344 |
| — | 出题方参照（sparring） | 489 | 460 ~ 521 | 1534-4-262 | 85.3% | 558 |
| 2 | GPT-6.1 Sol（Ultrafast） | 467 | 432 ~ 503 | 1509-0-291 | 83.8% | 486 |
| 3 | Gemini 4 Argon Contributor | 355 | 325 ~ 389 | 1351-3-446 | 75.1% | 232 |
| 4 | GPT-6.1 Sol | 319 | 286 ~ 353 | 1295-1-504 | 72.0% | 274 |
| 5 | Claude Opus 5.5 | 259 | 224 ~ 295 | 1196-2-602 | 66.5% | 154 |
| 6（6–10 并列） | Muse Wonder Full | 55 | 22 ~ 88 | 823-6-971 | 45.9% | 74 |
| 7（6–10 并列） | 小米 MiMo V2.6 Pro | 29 | 2 ~ 59 | 773-4-1023 | 43.1% | 39 |
| 8（6–10 并列） | Grok 4.7 | 26 | −3 ~ 55 | 768-1-1031 | 42.7% | −8 |
| — | 基准 | 0 | — | 718-3-1079 | 40.0% | 0 |
| 9（6–10 并列） | DeepSeek V4.1 Flash | −6 | −37 ~ 28 | 706-3-1091 | 39.3% | −60 |
| 10（6–10 并列） | Doubao-Seed-Evolving | −31 | −64 ~ 0 | 655-12-1133 | 36.7% | −470 |
| 11（11–12 并列） | Kimi K3 | −129 | −162 ~ −97 | 466-30-1304 | 26.7% | −126 |
| 12（11–12 并列） | Claude Haiku 5.5 | −149 | −182 ~ −117 | 438-18-1344 | 24.8% | −132 |
| 13 | 通义千问 Qwen 3.8 Max | −205 | −239 ~ −171 | 356-2-1442 | 19.8% | −331 |
| 14 | 智谱 GLM 5.3 | −315 | −362 ~ −271 | 203-13-1584 | 11.6% | −447 |

- 第 1 名和第 2 名之差是 40，差的 95% 区间是 1 ~ 84，分得开；Astra 对 SolUF 75 胜 45 负。第 6 到第 10 名的相邻差距都分不开，记为并列。
- 最后一列是中场赛（上半场版本互相打）的实力值，和终场赛不是同一把尺，只能看大致位置。中场赛的完整成绩见 `results/halftime/成绩.md`。
- 出题方参照是出题方自己写的搜索型 bot，只作参照、不排名，比赛时不发给选手。它的源码在 `tournament/sparring.cpp`。
- 终场赛 14400 局里没有一局因故障结束，所有程序的故障判负都是 0。中场赛里 Doubao 的上半场版本有 2 局因故障判负。
- 两两胜负、相邻名次之差、每回合用量见 `results/final/成绩.md`。

## 进步赛：下半场比上半场强了多少

每位选手的下半场版本去打"全员上半场版本 + 基准"这个固定的对手池，和中场赛的全部对局合在一起拟合，下半场版本和上半场版本就在同一把尺上。进步幅度 = 下半场版本的实力值 − 自己上半场版本的实力值。

| 选手 | 上半场 | 下半场（同一把尺） | 进步幅度 | 95% 区间 | 最终名次 |
|---|---:|---:|---:|---|---:|
| Doubao-Seed-Evolving | −444 | −49 | +395 | +332 ~ +460 | 10 |
| Gemini 4 Argon Contributor | 222 | 392 | +170 | +122 ~ +225 | 3 |
| GPT-6 Astra | 337 | 485 | +147 | +87 ~ +228 | 1 |
| 智谱 GLM 5.3 | −427 | −298 | +129 | +70 ~ +178 | 14 |
| 通义千问 Qwen 3.8 Max | −330 | −244 | +86 | +41 ~ +127 | 13 |
| DeepSeek V4.1 Flash | −65 | −8 | +58 | +19 ~ +97 | 9 |
| Claude Opus 5.5 | 138 | 177 | +39 | −6 ~ +88 | 5 |
| GPT-6.1 Sol | 258 | 287 | +29 | −50 ~ +111 | 4 |
| Grok 4.7 | −7 | 21 | +29 | −29 ~ +78 | 8 |
| Muse Wonder Full | 76 | 90 | +14 | −28 ~ +55 | 6 |
| Kimi K3 | −120 | −118 | +2 | −67 ~ +72 | 11 |
| Claude Haiku 5.5 | −132 | −141 | −9 | −78 ~ +57 | 12 |
| 小米 MiMo V2.6 Pro | 37 | 21 | −16 | −51 ~ +21 | 7 |
| GPT-6.1 Sol（Ultrafast） | 467 | 405 | −62 | −120 ~ +1 | 2 |

- Muse、MiMo、Haiku 下半场交回的程序和自己的上半场版本逐字节相同，它们的"进步幅度"只是测量的波动。
- 另有一张表是每位选手"下半场版本 对 自己上半场版本"的直接对局（每人 16 局），两张表都在 `results/附表.md`。
- "上半场"一栏是合并拟合里的值，和中场赛单独拟合的值略有出入，属正常。

## 马上看 / 马上玩

**看回放**：用浏览器打开 `replays/` 里任意一个 `.html`，不需要联网。空格播放 / 暂停，左右方向键逐回合，`[` `]` 跳到上一个 / 下一个关键事件，按 `H` 看全部快捷键。

| 文件 | 对局 |
|---|---|
| `final-astra-vs-soluf.html` | 终场赛，冠亚军 |
| `final-sparring-vs-astra.html` | 终场赛，冠军对出题方参照（60 胜 3 平 57 负） |
| `final-gemini-vs-astra.html`、`final-sol-vs-soluf.html`、`final-sol-vs-opus.html`、`final-gemini-vs-opus.html` | 终场赛，前五名之间 |
| `final-opus-vs-haiku.html` | 终场赛，同一家的两个模型 |
| `final-baseline-vs-astra.html`、`final-baseline-vs-doubao.html` | 终场赛，对基准 |
| `final-muse-vs-mimo.html`、`final-haiku-vs-kimi.html`、`final-qwen-vs-glm.html` | 终场赛，并列区和末位 |
| `halftime-astra-vs-soluf.html` | 中场赛，上半场版本的 Astra 对 SolUF |
| `halftime-glm-vs-doubao.html` | 中场赛，上半场版本的 GLM 对 Doubao（这一对 84 局里 53 局是打满 300 回合的平局） |
| `progress-doubao2-vs-doubao1.html` | 进步赛，Doubao 的下半场版本对自己的上半场版本 |

每一局都是在指定的对阵里取"多数结果、局长最接近中位数"的那一局，不挑反杀。

**用你的 bot 挑战全部 AI**（需要 Python 3.10+ 和 g++）：

```bash
python challenge.py 你的bot.cpp           # 和 14 个下半场版本、出题方参照、基准各打 20 局，共 320 局，几分钟
python challenge.py 你的bot.cpp --full    # 每个对手 80 局，共 1280 局，结果更稳
```

不带参数时用 `workspace/submission/bot.cpp`，这是选手拿到的起步模板（三辆车直冲对方基地，很弱）。评测沿用独立进程的正式模式和原来的 2000/50 CPU 毫秒预算；最后固定终场赛 16 个对手的实力值，只拟合你的实力值，再和 14 位正式选手比较相当名次，基准和出题方参照不排名。不会重新拟合一轮小比赛，估计名次也不是正式赛成绩。

**投到社区榜**：在仓库根目录新建 `community/tank/<GitHub用户名小写>__<作品名>/`，只放 `bot.cpp`、`meta.json` 和可选的 `NOTES.md`。`bot.cpp` 包含官方 `tank.hpp`、实现 `init` 和 `decide`，不要自带 `main` 或引擎文件。然后在仓库根目录运行：

```bash
python community/check.py community/tank/<你的目录> --skip-eval  # 结构检查，并与官方 tank_main.cpp 一起编译
python community/check.py community/tank/<你的目录>              # 320 局正式评测
python community/check.py community/tank/<你的目录> --full       # 1280 局正式评测
```

通过后只提交这个文件夹，向本仓库开 PR。格式、许可、成绩口径和安全说明见 [社区投稿指南](../community/README.md)，也可以使用 [coding agent 提示词](../community/AGENT_PROMPT.md)。PR 评测使用目标分支的可信脚本，因此目标分支需先合并 Tank 投稿支持。

**自己开发时**，在 `workspace/` 里用本地工具，和比赛时选手用的完全一样（用法见 `workspace/README.md`）：

```bash
cd workspace
python arena.py check                                                   # 检查编译器，模板能不能打完一局
python arena.py play submission/bot.cpp baseline.cpp --seed 7 --out first  # 打一局，回放存进 replays/，同时生成网页
python arena.py vs submission/bot.cpp baseline.cpp --games 200           # 两个版本对比，看最后一行的汇总
python arena.py batch submission/bot.cpp ../bots/half2/gpt-6-astra.cpp --games 20  # 直接和冠军打
```

Windows 上没有 `python` 命令时，把 `python` 换成 `.\run.cmd`。如果只把 w64devkit 放进了仓库的 `tools/`、没有加入 PATH，先在仓库根目录的 PowerShell 中运行 `$env:CXX = (Resolve-Path .\tools\w64devkit\bin\g++.exe).Path`，再进入 `tank/workspace/`（`challenge.py` 会自己找 `tools/` 里的编译器）。

**复现正式赛**：见 `tournament/README.md`。配置里写着原来的赛程种子，地图、座位、对阵可以一模一样地重建。

## 目录

| 路径 | 内容 |
|---|---|
| `workspace/` | 选手包（和比赛时的 `workspace.zip` 相同，差别见下）：规则、引擎、本地裁判、回放网页、起步模板、基准源码 |
| `prompt.md` | 发给模型的全部消息原文：赛前狠话、开赛提示词、中场消息、续跑消息、赛后复盘 |
| `bots/half1/`、`bots/half2/` | 14 个模型上半场、下半场的封存代码（原样），见 `bots/README.md` |
| `results/` | 中场赛、终场赛、进步赛的成绩和逐局结果，两张附表，见 `results/README.md` |
| `replays/` | 15 局精选回放 |
| `tournament/` | 正式赛平台、三场比赛的配置、出题方参照的源码，可复现赛程，见 `tournament/README.md` |
| `notes/` | 赛前狠话、赛后复盘，见 `notes/README.md` |
| `challenge.py` | 一键挑战 |
| `make_player_pack.py`、`workspace_manifest.json` | 打一份和比赛时一样的选手包（见上一节），以及比赛时选手包的文件清单和哈希 |
| `CONDITIONS.md` | 比赛条件与异常记录 |

## 用比赛时的条件让新模型参赛

`workspace/` 里有基准的源码 `baseline.cpp`，比赛时选手只拿到编译好的程序。**不要把 `workspace/` 文件夹直接交给模型**，先打一份和比赛时一样的选手包：

```bash
python make_player_pack.py        # 生成 out/workspace.zip
```

脚本用比赛时的编译参数把 `baseline.cpp` 编成 `baseline.exe`，逐个核对其余文件和 `workspace_manifest.json` 里记的比赛时选手包的 sha256，再按原来的文件清单和目录结构打包，包里没有 `baseline.cpp`。然后：

1. 新建一个空文件夹，只放 `out/workspace.zip`，在里面给模型开一个新会话；
2. 发 `prompt.md` 里的"开赛提示词"原文。时间规则（30 分钟、最早第 20 分钟交卷、45 分钟强制收卷）写在提示词里，选手包里的 `clock.py` 从解压时刻开始计时；
3. 上半场结束后，要照原赛制给中场包，就用 `tournament/` 打一轮中场赛，把成绩表、这位选手的回放和对手的可执行程序按 `README.md`"赛制"一节的内容放进 `workspace/halftime/`，再发 `prompt.md` 里的"中场消息"。只比一个半场的话，直接拿交上来的 `submission/bot.cpp` 跑 `challenge.py` 即可。

## 和比赛时的差别

- **基准**：比赛时选手只拿到编译好的程序 `baseline.exe`，开源版放的是源码 `workspace/baseline.cpp`，两者对局逐回合一致（开源前对打 30 张图核对过）。`workspace/README.md` 里写到 `baseline.exe` 的命令，换成 `baseline.cpp` 就能用，`arena.py` 会自动编译。
- 除此之外，`workspace/` 和发给选手的 `workspace.zip` 逐字节相同；`bots/` 里的代码和主办方封存的版本逐字节相同（sha256 与 `results/*/清单.json` 里记录的一致）。
- **中场包**没有单独放：成绩表就是 `results/halftime/成绩.md` 的内容，回放可以用 `tournament/` 重打，对手程序就是 `bots/half1/` 编译出来的。比赛时主办方用脚本计时、封存、补时和生成中场包，这些脚本没有放。
- **没有放进仓库的**：各模型的原始会话记录；选手交卷时用文字写的策略说明（在会话记录里）；全部 27840 局的完整回放（压缩后约 32 MB，逐局结果在 `results/` 里）；主办方的操作手册、内部说明和测试脚本。

## 声明

- 每个模型每个半场只跑了一次，结论是"这一场"的结果，不是严格的模型排名。工具、渠道、思考档、机器都会影响成绩，各家条件不完全相同，见 `CONDITIONS.md`。
- 上半场有五位（Grok、GLM、Doubao、MiMo、Qwen）是重跑的：赛后审计发现原场次受外部中断影响、补时不足，或开赛提示词被启动脚本截断，主办方作废原场次，在原文件夹从头重跑。表里只计重跑的那一次。
- 断网、服务中断和工具故障造成的停顿，主办方按会话记录补了时间（补时方向对选手有利，部分秒数是估算）；Doubao 下半场在第 27.8 分钟用完了火山引擎的 5 小时额度，没有补时，按当时的代码收卷。主办方自己的几处失误也列在 `CONDITIONS.md` 里。
- 外部机器上的三位（SolUF、Gemini、Muse）没有主办方计时，用时是操作员的记录；那台机器更快，本地验证能打的局数和比赛电脑上的选手不可比。
- 中场赛 42 批里有 5 批、终场赛 60 批里有 5 批在测速时被标记为"前后不一致"（机器状态有波动），结果照常计入。用 `budget_left()` 按时间停止搜索的 bot，结果会随机器状态略有变化。
- 选手代码都是模型生成的，主办方没有改动。题目、规则、裁判、基准和出题方参照由主办方编写。
