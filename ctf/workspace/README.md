# 夺旗

编写 `submission/bot.cpp`：一个程序统一控制己方 3 个角色，支持 2–15 个阵营同场。完整规则和输入输出格式见 `RULES.md`。

解压时开始计时。你有 60 分钟，只看最终 bot 的强度，提前交卷不加分；可以适当超时，但到 90 分钟会强制交卷。

## 文件

| 文件 | 用途 |
|---|---|
| `RULES.md` | 规则与程序接口 |
| `submission/bot.cpp` | 你的程序。现在是一个模板：已经写好完整的输入解析，角色原地不动 |
| `baseline.exe` | 中等强度的练习对手，思路常规，没有提供源码，请不要反编译 |
| `arena.py`、`engine.py`、`core.py` | 本地对战用的裁判，与正式比赛相同 |
| `viewer.html` | 回放网页模板 |
| `clock.py` | 查看已用时间 |
| `run.cmd`、`run.sh` | 找到 Python 并运行上面的脚本 |

## 常用命令

在本目录运行。下面以 PowerShell 和命令提示符为例，写作 `.\run.cmd`；在 bash 里（包括 Windows 上的 Git Bash 和 Linux）把它换成 `sh run.sh`，后面的参数不变。

```text
.\run.cmd clock.py                                         已用时间
.\run.cmd arena.py check                                   检查环境
.\run.cmd arena.py play submission/bot.cpp baseline --seed 7
.\run.cmd arena.py play submission/bot.cpp baseline baseline baseline baseline --out five
.\run.cmd arena.py show replays/five.json 120 --around 2    看第 118–122 回合
.\run.cmd arena.py batch submission/bot.cpp baseline baseline --games 12
```

- `.cpp` 程序会先自动编译，结果按源码缓存在 `build/`，源码不变就不重复编译。编译出错时会显示编译器的报错。
- `play` 打一局，打印每个程序的得分、名次、每回合平均和最慢用时，并把回放存成 `replays/名字.json` 和 `replays/名字.html`。网页可以逐回合播放。
- `show` 在终端里打印棋盘、所有角色的位置、血量和携旗情况、事件，以及各方的指令。
- `batch` 连续打多局并汇总每个程序的平均得分、平均名次、第一名次数、故障和最慢用时，不保存回放。它和正式比赛一样，每张地图把座位完整轮换一圈（n 方就是 n 局），所以局数最好取人数的整数倍。
- `--seed` 只决定地图。旗子每次出现在哪个旗点每局都不同，所以同一个种子重打，结果也可能不同。
- 程序可以重复出场，比如自己和自己对打。

## 提交

最终代码放在 `workspace/submission/bot.cpp`，结束时用几句话说明你的策略。
