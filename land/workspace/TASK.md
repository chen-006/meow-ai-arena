# 任务：编写圈地对战 bot

为"圈地对战"写一个尽可能强的 C++ bot。它会和其他 AI 模型写的 bot 进行 **1 对 1 单挑**和 **4 人混战**两种赛制的对局，两种都计分。

完整规则和通信协议见 `RULES.md`，规则以 `engine/game.py` 为准。

## 工作目录

| 路径 | 内容 |
|---|---|
| `RULES.md` | 规则与通信协议 |
| `clock.py` | 计时器：`python3 clock.py` 显示已用时间 |
| `engine/` | 规则引擎 `game.py`、本地裁判 `referee.py` |
| `anchors/` | 两个基准 bot（random、greedy） |
| `starter/bot.cpp` | 起步模板，已实现协议读写 |
| `viewer/viewer.html` | 回放查看器（给人看的） |
| `submission/` | 最终 bot 放在 `submission/bot.cpp` |

本地对战（`.cpp` 会自动编译；没有 `python3` 就用 `python`）：

```bash
python3 engine/referee.py submission/bot.cpp anchors/greedy.cpp --games 10                                     # 单挑
python3 engine/referee.py submission/bot.cpp anchors/greedy.cpp anchors/greedy.cpp anchors/random.cpp --games 8  # 混战
```

其他参数见 `python3 engine/referee.py -h`。

## 提交要求

1. 单个文件 `submission/bot.cpp`，同一个程序要能打单挑和混战（根据开局的 `N` 判断）。
2. 编译命令 `g++ -O2 -std=c++20 bot.cpp`，只用 C++ 标准库，单线程，内存 512 MB 以内。
3. bot 程序不得访问网络或读写文件，不得在对局之间保存信息。
4. 每回合 50 ms，第 0 回合 2000 ms。崩溃或单局累计 10 次超时/格式错误判负。正式比赛会在同一台机器上同时进行多局，CPU 负载比你本地测试时高，请为每回合的计算时间留足余量。
5. 工作时只能读写当前 `workspace` 文件夹里的文件，不要访问这个文件夹以外的任何内容。

## 评分

单挑循环赛 + 多轮随机分桌的混战，只看名次。每种赛制用 Bradley-Terry 模型拟合实力值，greedy 基准 = 0 分、本期最强者 = 100 分；两种赛制的分数加权合成总分，单挑的权重不低于混战。

对手是其他 AI 模型写的 bot 和两个基准 bot，可能还有出题方的参考 bot。两个基准 bot 很弱，只用来确认程序能正常运行，打赢它们说明不了你的 bot 有多强；怎样检验和提高强度，由你自己决定。

## 时间

时间预算约 **60 分钟**，请自己把握进度，做完就可以结束。超过 90 分钟会被强制停止，以当时的 `submission/bot.cpp` 为准。`python3 clock.py` 可以查看从解压到现在已经过了多久。

不能联网搜索，不要向用户提问，独立完成。结束时用几句话总结你的策略。
