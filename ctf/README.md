# 第 3 集 · 夺旗

2–15 个阵营同场，每方用一个程序控制 3 个角色，抢中立的旗子送回自家基地得分。11 个 AI 模型拿到同一句提示词，各自用约 60 分钟写出一个 C++ bot，然后打了 32010 局。

## 规则速览（完整规则见 `workspace/RULES.md`）

- 方形地图，墙和柱子挡路也挡射击。每方 3 个角色，100 血。
- 每回合所有角色同时行动：先移动，再攻击。攻击射程 2（曼哈顿距离），每次 34 伤害，3 下击杀。
- 旗子共 max(2, ⌈人数/2⌉) 面，随机刷新在空旗点上（每个基地门口一个，中心一个）。走到旗上拾取，送回自家基地得 1 分。
- 携旗者不能攻击；被击杀就在原地掉旗。死后 10 回合在自家基地满血重生。
- 一局 400 回合，按得分排名。每回合限时 50 毫秒。

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
| `CONDITIONS.md` | 比赛条件与异常记录 |
| `appendix/` | 不计成绩的附加实验：同一个 DeepSeek 换四种工具的对照、豆包的 20 个历史版本、各模型开发过程中的版本演化 |

## 和比赛时选手包的差别

比赛时的选手包里，基准只有编译好的 Windows 程序 `baseline.exe`，没有源码，并且要求选手不要反编译。开源版放的是基准源码 `workspace/baseline.cpp`，`arena.py` 找不到 `baseline.exe` 时会自动从源码编译。除此之外，规则、引擎、裁判都和比赛时一致。
