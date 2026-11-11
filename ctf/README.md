[English](README_en.md)

# 第 3 集 · 夺旗

2–15 个阵营同场，每方用一个程序控制 3 个角色，抢中立的旗子送回自家基地得分。11 个 AI 模型拿到同一句提示词，各自用约 60 分钟写出一个 C++ bot，然后打了 32010 局。

- **目录里有什么**：`workspace/` 选手包 · `bots/` 11 个模型的交卷代码 · `results/` 成绩、两两胜率和逐局结果 · `replays/` 回放 · `tournament/` 正式赛配置 · `challenge.py` 一键挑战 · `appendix/` 附加实验
- **复现榜单**：在 `tournament/` 下用 `arena/tournament.py` 的 plan → run → rate 五条命令重建赛程、重打并评分，约 2 小时，见 [`tournament/README.md`](tournament/README.md)。

## 规则速览（完整规则见 `workspace/RULES.md`）

- 方形地图，墙和柱子挡路也挡射击。每方 3 个角色，100 血。
- 每回合所有角色同时行动：先移动，再攻击。攻击射程 2（曼哈顿距离），每次 34 伤害，3 下击杀。
- 旗子共 max(2, ⌈人数/2⌉) 面，随机刷新在空旗点上（每个基地门口一个，中心一个）。站到旗子所在格、用"拾旗"动作拿起，送回自家基地得 1 分。
- 携旗者不能攻击；被击杀就在原地掉旗。死后 10 回合在自家基地满血重生。
- 一局 400 回合，按得分排名。每回合限时 50 毫秒（首回合 2 秒）。

## 正式赛成绩

实力值用 Bradley–Terry 拟合，基准 = 0，强 10 倍记 +400；2–15 方每种人数等权。

| 名次 | 模型 | 工具 | 实力值 | 95% 区间 |
|---:|---|---|---:|---|
| 1 | GPT-6 Astra | Codex | 254 | 246 ~ 263 |
| 2 | GPT-6.1 Sol | Codex | 252 | 244 ~ 260 |
| 3 | Fable 5.1 | Claude Code | 213 | 206 ~ 222 |
| 4 | Sonnet 5.5 | Claude Code | 203 | 195 ~ 210 |
| 5 | Opus 5.5 | Claude Code | 196 | 188 ~ 204 |
| 6 | DeepSeek V4.1 Flash | dsh | 129 | 121 ~ 138 |
| — | 基准 | | 0 | |
| 7 | Qwen 3.8 Max | Qwen Code | −21 | −28 ~ −12 |
| 8 | MiMo V2.6 Pro | MiMo Code | −26 | −34 ~ −17 |
| 9 | Kimi K3 | Kimi Code | −35 | −44 ~ −25 |
| 10 | GLM 5.3 | ZCode | −92 | −100 ~ −84 |
| 11 | 豆包 Seed 2.1 Turbo | trae-agent | −307 | −315 ~ −298 |

- 第 1、2 名的区间大幅重叠，算并列。
- 各人数分项见 `results/ratings.md`；两两胜率见 `results/pairwise-winrate.csv`；拾旗、交旗、击杀、死亡统计见 `results/player-stats.csv`。
- 比赛条件（工具版本、渠道、用时、所有中断和事故）见 **`CONDITIONS.md`**。

## 马上玩

**看回放**：打开 `replays/` 里任意一个 `.html`，浏览器里逐回合播放。比如 `2p-astra-vs-sol.html` 是冠亚军单挑，`15p-melee.html` 是 15 人混战。

**用你的 bot 挑战全部 AI**（需要 Python 3.10+ 和 g++）：

```bash
python challenge.py 你的bot.cpp           # 约 200 局，半分钟左右
python challenge.py 你的bot.cpp --full    # 约 2000 局，几分钟，结果更稳
```

不带参数时用 `workspace/submission/bot.cpp`。这是选手拿到的模板，已经写好完整的输入解析，角色原地不动，你可以直接在上面改。

打完想留个名次，可以把这个 bot 投到社区榜，见 [`community/README.md`](../community/README.md)。

**自己开发时**，在 `workspace/` 里用本地裁判（和比赛时选手用的完全一样）：

```bash
cd workspace
python arena.py play submission/bot.cpp baseline --seed 7            # 打一局，回放存进 replays/
python arena.py batch submission/bot.cpp baseline baseline --games 12  # 连打多局看平均
python arena.py play submission/bot.cpp ../bots/gpt-6-astra.cpp        # 直接和冠军打
```

Windows 上没有 `python` 命令时，把 `python` 换成 `.\run.cmd`。

## 目录

| 路径 | 内容 |
|---|---|
| `workspace/` | 选手包：规则、引擎、本地裁判、回放网页、模板、基准源码 |
| `prompt.md` | 发给模型的提示词（原文） |
| `bots/` | 11 个模型的交卷代码（原样） |
| `results/` | 正式赛成绩、逐局结果（`games.jsonl.gz`，每行一局：座位、比分、拾旗、击杀等）、两两胜率、选手统计 |
| `replays/` | 11 局精选回放：指定对阵或人数里取"典型"的一局，不挑反杀 |
| `tournament/` | 正式赛配置，可完整复现赛程 |
| `challenge.py` | 一键挑战 |
| `make_player_pack.py`、`workspace_manifest.json` | 打一份和比赛时一样的选手包（见上面），以及比赛时选手包的文件清单和哈希 |
| `CONDITIONS.md` | 比赛条件与异常记录 |
| `appendix/` | 不计成绩的附加实验：同一个 DeepSeek 换四种工具的对照、豆包的 20 个历史版本、各模型开发过程中的版本演化 |

## 用比赛时的条件让新模型参赛

`workspace/` 里有基准的源码 `baseline.cpp`，比赛时选手只拿到编译好的程序。**不要把 `workspace/` 文件夹直接交给模型**，先打一份和比赛时一样的选手包：

```bash
python make_player_pack.py        # 生成 out/workspace.zip
```

脚本用比赛时的编译参数把 `baseline.cpp` 编成 `baseline.exe`，逐个核对其余文件和 `workspace_manifest.json` 里记的哈希，再按比赛时的文件清单和目录结构打包，包里没有 `baseline.cpp`。清单取自比赛时发给 11 位选手的同一个 `workspace.zip`；其中 `arena.py`、`core.py` 开源时改过（没有 `baseline.exe` 时改用源码编译；找编译器时改找仓库 `tools/` 下的 w64devkit），包里放的是开源版，有 `baseline.exe` 时对局行为和比赛时相同。然后：

1. 新建一个空文件夹，只放 `out/workspace.zip`，在里面给模型开一个新会话；
2. 发 `prompt.md` 里的提示词原文。时间规则（60 分钟，可以适当超时，90 分钟强制交卷）写在提示词里，选手包里的 `clock.py` 从解压时刻开始计时；
3. 交卷后拿 `submission/bot.cpp` 跑 `challenge.py`，和 11 个模型比。

## 和比赛时选手包的差别

比赛时的选手包里，基准只有编译好的 Windows 程序 `baseline.exe`，没有源码，并且要求选手不要反编译。开源版放的是基准源码 `workspace/baseline.cpp`，`arena.py` 找不到 `baseline.exe` 时会自动从源码编译。除此之外，规则、引擎、裁判都和比赛时一致。
