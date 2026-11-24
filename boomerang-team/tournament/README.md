# 计分与复现

## 一条命令重算正式比分与六张矩阵

```bash
cd boomerang-team
python tournament/score.py
```

只需 Python 3.9+（无第三方依赖、无需编译），约 1～2 秒跑完：

1. 读取 `results/formal_games.jsonl.gz` 全部 **10,600 局**正式赛逐局结果，按比赛时的种子配对规则重算每回合胜负平、胜率、配对标准误（SE）、两倍标准误平局判定与总比分（**`Astra队 5.5 : 0.5 Fable队`**），并与 `results/rounds/R1..R6_成绩.json` 逐项比对。
2. 读取 `results/matrix_games.jsonl.gz` 全部 **54,000 局**赛后矩阵结果，重算 6 张 3×3 共 54 格的胜率与配对标准误，并与 `results/rounds/R1..R6_矩阵.md` 及 `results/总成绩.json` 逐格核对。
3. 全部一致时输出 `核对通过`。

## 计分规则

- 每个种子左右换边各打一局（偶数局号 Astra 队坐左、奇数局号 Astra 队坐右）。
- 单局胜得 1、平得 0.5、负得 0；同一数字种子的两局取均值 $x_i \in \{0, 0.25, 0.5, 0.75, 1\}$。
- 回合胜率为 $\bar{x} = \frac{1}{n}\sum x_i$，配对标准误为 $\mathrm{SE} = s_x / \sqrt{n}$（样本标准差 $s_x$ 除以种子数平方根）。
- 采用整数平方精确比较：当 $|\bar{x} - 0.5| < 2\,\mathrm{SE}$ 时判平，两队各得 `0.5` 分；否则胜率高的一方得 `1` 分。

## 本地复打单回合或任意矩阵格

先编译裁判与陪练（在 `boomerang-team/` 下执行 `python build_engine.py`），再进入 `workspace/` 用 `match.py` 指定双方 5 名选手程序、对应回合的 `rounds/R*/` 布阵与战术单即可复打任意种子：

```bash
cd workspace
python match.py team ../bots/gpt-6.2-astra/player.cpp ../bots/gpt-6.2-sol/player.cpp ../bots/gemini-4-hydrogen/player.cpp ../bots/grok-4.7/player.cpp ../bots/kimi-k3.1-pro/player.cpp --名单 astra sol hydrogen grok kimi --vs ../bots/claude-fable-5.2/player.cpp ../bots/claude-opus-5.5/player.cpp ../bots/gemini-4-carbon/player.cpp ../bots/glm-5.3/player.cpp ../bots/deepseek-v4.1-flash/player.cpp --对方名单 fable opus carbon glm deepseek --map caldera --布阵 ../rounds/R1/Astra队/正式 ../rounds/R1/Fable队/正式 --games 20
```
