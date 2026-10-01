# 第 2 集 · 炸弹人

2–10 人同场的炸弹人：放炸弹炸开箱子、炸死对手，地图会从外向内缩圈，活得越久得分越高。9 个 AI 模型拿到同一句提示词，各自用约 60 分钟写出一个 C++ bot，然后从 2 人局打到 10 人局，一共 8930 局。

## 规则速览（完整规则见 `workspace/RULES.md`）

- 方形地图，有永久墙和可炸的箱子。所有玩家同时行动：上下左右或等待，同一回合还可以在原地放炸弹（每人最多 2 颗在场）。
- 炸弹 4 回合后引爆，十字形火焰射程 3 格：被墙挡住；碰到箱子会炸毁箱子并停下；碰到别的炸弹会连锁引爆。被火焰碰到就淘汰，没有血量、不会复活。
- 一定回合后从外圈开始缩圈，缩进来的格子会直接淘汰站在上面的人。
- 单局分 = 生存分（按淘汰先后）+ 0.5 × 击杀份额，按单局分排名。每回合限时 50 毫秒（首回合 2 秒）。

## 正式赛成绩

8930 局，2–10 人每档等权。实力值 = Bradley–Terry，基准 = 0，尺度为 400 × log10(实力比)，不封顶。

| 名次 | 模型 | 工具 | 实力值 |
|---:|---|---|---:|
| 1 | GPT-6 Astra | Codex | 488.6 |
| 2 | Opus 5.5 | Claude Code | 321.8 |
| 3 | GPT-6 Sol | Codex | 301.9 |
| 4 | DeepSeek V4.1 Flash | ZCode | 270.7 |
| 5 | Kimi K3 | ZCode | 230.4 |
| 6 | Qwen 3.8 Max | ZCode | 226.5 |
| 7 | GLM 5.3 | ZCode | 175.0 |
| 8 | 豆包 Seed 2.1 | ZCode | 95.4 |
| 9 | MiMo V2.6 Pro | ZCode | 39.4 |
| — | 基准 | — | 0 |

- 本期的基准很弱，9 个模型全部高于基准。
- 各人数统计（生存分、击杀分、第一率、故障）见 `results/summary.md`。最初那版 2600 局（只打 2、5、10 人）的排名完全相同，对比见 `results/vs-original-2600.md`。
- 比赛条件（工具、思考档、用时、中断与处理）见 **`CONDITIONS.md`**。

## 马上玩

**看回放**：打开 `replays/` 里任意一个 `.html`，比如 `10p-melee.html`（十人混战）、`2p-astra-vs-opus.html`（冠亚军单挑）。

**用你的 bot 挑战全部 AI**（需要 Python 3.10+ 和 g++）：

```bash
python challenge.py 你的bot.cpp           # 2 人局 + 10 人局，330 局，约 4 分钟
python challenge.py 你的bot.cpp --full    # 再加 3 人局，1320 局，约 15 分钟
```

不带参数时，用公开基准 `workspace/baseline.cpp` 当"你的 bot"演示一遍。每局的 HTML 回放都会存进结果目录。

**自己开发时**，在 `workspace/` 里用本地裁判（和比赛时选手用的完全一样，见 `workspace/README.md`）：

```bash
cd workspace
# 先把你的代码存成 submission/bot.cpp（可以复制 baseline.cpp 起步）
python engine/build.py --compile --source baseline.cpp --output baseline.exe
python engine/build.py --compile --source submission/bot.cpp --output bot.exe
python engine/arena.py --bots Mine=bot.exe Base=baseline.exe --seed 1 --replay replays/duel.html
```

Windows 上没有 `python` 命令时用 `run.cmd`，Linux 上用 `sh run.sh`，并去掉 `.exe`。

## 目录

| 路径 | 内容 |
|---|---|
| `workspace/` | 选手包：规则、引擎与本地裁判、回放播放器、公开基准、空的提交目录 |
| `prompt.md` | 发给模型的提示词（原文） |
| `bots/` | 9 个模型的交卷代码（封存原样，附哈希） |
| `results/` | 正式赛成绩、逐局结果（`games.json.gz`，8930 局） |
| `replays/` | 8 局精选回放 |
| `tournament/` | 正式赛评测与评分脚本、冻结赛程 |
| `challenge.py` | 一键挑战 |
| `CONDITIONS.md` | 比赛条件与异常记录 |
| `appendix/` | 最初的 2600 局正式赛；各模型开发过程中的版本演化 |
