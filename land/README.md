# 第 1 集 · 圈地

经典"纸片人"式圈地游戏：从自己的领地出发画线，绕一圈回到领地就把圈住的地全部收下；画线途中轨迹被人踩到就会阵亡（**领地保留**）。9 个 AI 模型拿到同一句提示词，各自用约 60 分钟写出一个 C++ bot，同一个 bot 既打 1 对 1 单挑，也打 4 人混战，共 30000 局。

## 规则速览（完整规则见 `workspace/RULES.md`）

- 单挑 40×40、混战 48×48，600 回合，所有玩家同时移动（上下左右，不能掉头）。
- 走出领地会留下轨迹；回到自己领地时，轨迹和被它围住的区域都变成你的领地（可以圈走对手的地，出生点的 3×3 保护区除外）。
- 撞墙、撞头、自己的轨迹被任何人踩到都会阵亡：轨迹清除、回出生点，但领地保留。
- 600 回合后按领地格数排名。每回合限时见规则。

## 正式赛成绩

单挑 26400 局 + 混战 3600 局。每种赛制以 greedy 基准 = 0、该赛制最强模型 = 100 换算，总分是两种赛制平均后再把最高者换算成 100（**口径和第 2、3 集不同**，见 `CONDITIONS.md`）。

| 名次 | 模型 | 工具 | 总分 | 单挑 | 混战 |
|---:|---|---|---:|---:|---:|
| 1 | Sonnet 5.5（见 `CONDITIONS.md` 注 1） | Claude Code | 100.0 | 100.0 | 100.0 |
| 2 | GPT-6 Astra | Codex | 81.7 | 72.3 | 91.0 |
| 参考 | 出题方陪练 bot | — | 79.8 | 89.3 | 70.3 |
| 3 | Opus 5.5 | Claude Code | 79.4 | 81.2 | 77.5 |
| 4 | GPT-6 Sol | Codex | 39.4 | 54.0 | 24.8 |
| 5 | MiMo V2.6 Pro | ZCode | 32.4 | 50.4 | 14.4 |
| 6 | Kimi K3 | WorkBuddy | 16.7 | 22.0 | 11.5 |
| 7 | GLM 5.3 | WorkBuddy | 5.9 | 11.8 | 0.0 |
| — | greedy 基准 | — | 0 | 0 | 0 |
| 8 | Spacebunny（后确认为 MiniMax M3.1 Flash） | ZCode | 0.0 | 0.0 | 0.0 |
| 9 | DeepSeek V4.1 Flash | WorkBuddy | 0.0 | 0.0 | 0.0 |

- 第 2、3 名的区间重叠，算并列。
- 第 8、9 名都低于 greedy，被截成 0。按原始实力值看：Spacebunny 单挑更差，DeepSeek 混战更差。
- 完整表（区间、得分率、平均面积、被杀次数）见 `results/ratings.md`。比赛条件见 **`CONDITIONS.md`**。

## 马上玩

**看回放**：双击 `replays/index.html`，左上角下拉切换 9 局（单挑：Opus vs Sonnet、Astra vs Sonnet、MiMo vs greedy；还有 6 局混战）。

**用你的 bot 挑战全部 AI**（需要 Python 3.10+ 和 g++）：

```bash
python challenge.py 你的bot.cpp           # 约 380 局，四分钟左右
python challenge.py 你的bot.cpp --full    # 约 1100 局，十几分钟
```

不带参数时用 `workspace/starter/bot.cpp`。这是选手拿到的起步模板，已经实现协议读写。

**自己开发时**，在 `workspace/` 里用本地裁判（和比赛时选手用的完全一样）：

```bash
cd workspace
python engine/referee.py 你的bot.cpp anchors/greedy.cpp --games 10                                    # 单挑
python engine/referee.py 你的bot.cpp anchors/greedy.cpp anchors/greedy.cpp anchors/random.cpp --games 8  # 混战
python engine/referee.py 你的bot.cpp "../bots/sonnet-5@claude-code#1/bot.cpp" --games 10               # 直接挑战冠军
```

回放用 `workspace/viewer/viewer.html` 打开（把 `.json` 拖进页面）。

## 目录

| 路径 | 内容 |
|---|---|
| `workspace/` | 选手包：规则、任务说明、引擎与本地裁判、两个基准（random、greedy）、起步模板、回放查看器 |
| `prompt.md` | 发给模型的提示词（原文） |
| `bots/` | 9 个模型的交卷代码（原样），文件夹名就是正式赛里的编号 |
| `tournament/` | 正式赛的循环赛脚本、评分脚本、出题方陪练 bot（`sparring.cpp`） |
| `results/` | 成绩表、逐局结果（`games.jsonl.gz`，30000 局） |
| `replays/` | 精选回放（`index.html` 双击即看） |
| `challenge.py` | 一键挑战 |
| `CONDITIONS.md` | 比赛条件与异常记录 |
| `appendix/version-evolution.md` | 各模型开发过程中各个历史版本的实力变化 |

## 复现正式赛

```bash
python tournament/round_robin.py --entries bots --out results/rerun.jsonl --maps 200 --melee-rounds 300 --jobs 12
python tournament/rate.py results/rerun.jsonl --entries bots --out results/rerun-ratings
```

地图种子从 1000 起，地图和对阵与正式赛相同。有的 bot 会按时间控制搜索量，逐局比分不保证和 `results/games.jsonl.gz` 完全一致，但总排名应当一致。
