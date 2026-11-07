# 示例 bot 与真实交卷

## 5 个示例 bot

这里是 5 个**主办方代写的示例 bot**，风格各不相同，用来演示赛制、当陪练和占位：

| bot | 风格 | 一句话 |
|---|---|---|
| `rush.cpp` | 速推 | 五人抱团强行拆塔，不停手 |
| `farm.cpp` | 发育 | 补刀吃野攒经验，优势成型再一波 |
| `gank.cpp` | 游走 | 刺客全图抓人，人头经验滚雪球 |
| `siege.cpp` | 抱团 | 中路稳步推进，兵线纪律严明 |
| `balance.cpp` | 均衡 | 分路运营、防守反击的全能型 |

**它们不是真实模型的交卷代码。** 想让它们和你的 bot 过招：
`python challenge.py 你的bot.cpp --pvp`（见 [`../challenge.py`](../challenge.py)）。

示例赛的成绩在 [`results/示例赛-成绩.md`](../results/示例赛-成绩.md)。

## 4 个真实模型交卷

正式赛用的是 4 个真实模型的交卷代码，文件名就是模型名：

| bot | 模型 | 正式赛名次 |
|---|---|---|
| `kimi-k3.1-pro.cpp` | Kimi K3.1 Pro | 1（1454 分） |
| `deepseek-v4.1-flash.cpp` | DeepSeek V4.1 Flash | 2（1003 分） |
| `MiniMax-M3.1.cpp` | MiniMax M3.1 | 3（464.5 分） |
| `doubao-seed-2.1-Lite.cpp` | Doubao Seed 2.1 Lite | 4（102.5 分） |

成绩在 [`results/正式赛-成绩.md`](../results/正式赛-成绩.md)，逐局明细在
[`results/正式赛-对局.json`](../results/正式赛-对局.json)。成绩表里记了每个 bot 的源码
SHA-256，可以用来核对跑的是哪一版代码。

## 换成自己的 bot 跑循环赛

把各模型的 `bot.cpp` 放进 `bots/`（命名建议 `模型名.cpp`），改一下
`tournament/run_roundrobin.py` 顶部的 `BOTS` 名单，然后：

```bash
cd tournament
python run_roundrobin.py --replays     # 循环赛 + 三档难度 + 精选回放，成绩写入 results/
```

`--label` 可以给成绩文件换个前缀（比如 `--label 正式赛`），默认是 `示例赛`。
