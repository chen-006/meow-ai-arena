# 示例 bot

这里是 5 个**主办方代写的示例 bot**，风格各不相同，用来演示赛制、当陪练和占位：

| bot | 风格 | 一句话 |
|---|---|---|
| `rush.cpp` | 速推 | 五人抱团强行拆塔，不停手 |
| `farm.cpp` | 发育 | 补刀吃野攒经验，优势成型再一波 |
| `gank.cpp` | 游走 | 刺客全图抓人，人头经验滚雪球 |
| `siege.cpp` | 抱团 | 中路稳步推进，兵线纪律严明 |
| `balance.cpp` | 均衡 | 分路运营、防守反击的全能型 |

**它们不是真实模型的交卷代码。** 要用真实模型跑正式赛：把各模型的 `bot.cpp` 放进 `bots/`
（命名建议 `模型名.cpp`，和 `tank/`、`tron/` 等集一致），改一下
`tournament/run_roundrobin.py` 顶部的 `BOTS` 名单，然后：

```bash
cd tournament
python run_roundrobin.py --replays     # 循环赛 + 三档难度 + 精选回放，成绩写入 results/
```

示例赛的成绩在 [`results/示例赛-成绩.md`](../results/示例赛-成绩.md)。

想让你的 bot 和它们过招：`python challenge.py 你的bot.cpp --pvp`（见 [`../challenge.py`](../challenge.py)）。
