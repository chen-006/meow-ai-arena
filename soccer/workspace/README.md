# 2v2 足球 · 选手包

你和队友各写一个球员程序（C++，一个 `.cpp` 文件），两个程序组成一队。规则见 `RULES.md`（请先读完）。

## 文件

| 文件 | 作用 |
|---|---|
| `RULES.md` | 比赛规则、输入输出协议、时间限制 |
| `template.cpp` | 最简单的合法程序（会跑会踢，很弱）。从它改起 |
| `soccer.hpp` | 规则引擎（裁判用的同一份）。可以在你的程序里 `#include "soccer.hpp"` 做精确模拟 |
| `build.py` | 编译脚本，与正式比赛相同的编译命令 |
| `match.py` | 本地对战：你 + 队友 对 基准 / 中档陪练 / 陪练 / 任何程序 |
| `replay.py` | 复盘工具：事件、任意一步的状态和小地图、进球前后、每名球员每步应有的观测和实际执行的动作（含队友消息）、导出 PNG / GIF |
| `viewer.html` | 网页回放查看器：用浏览器打开，把回放文件拖进去 |
| `arena.exe`、`arena.cpp` | 裁判程序（与正式比赛相同）及源码 |
| `soccer_engine.py` | 引擎的 Python 版（复盘工具用，与 soccer.hpp 逐位一致） |
| `bots/baseline.exe` | 基准 bot（中等强度），只有程序 |
| `bots/midfield.exe` | 中档陪练 bot（强于基准、弱于陪练），只有程序 |
| `bots/sparring.exe` | 陪练 bot（较强），只有程序 |
| `示例回放.txt` | 一局示例回放（陪练 对 基准），可拖进 viewer.html 或用 replay.py 试 |
| `run.cmd` | 找到 Python 并运行脚本：`.\run.cmd match.py ...`（直接 `python match.py ...` 也行） |

运行后会自动生成 `build/`（编译缓存）和 `replays/`（回放）。

## 常用命令

在本目录运行（需要 Python 3.8+ 和 g++；g++ 不在 PATH 时设环境变量 `CXX`）：

```text
python build.py --show                              看找到的编译器和正式编译命令
python build.py player.cpp                          编译（编译错误会直接显示）
python match.py check player.cpp                    检查：编译 + 正式计时打两局，报告超时 / 非法输出 / 崩溃 / 每步用时
python match.py run --me player.cpp --mate ..\队友目录\player.cpp --opp baseline --seeds 8
                                                    我方对基准打 16 局（8 个种子 × 换边），看得分率和故障
python match.py run --me player.cpp --opp sparring  不写 --mate：两个座位都用你的程序
python match.py play player.cpp 队友.cpp sparring sparring --seed 3
                                                    打一局（A1 A2 对 B1 B2），存回放，显示每人的 CPU 用时
python replay.py info replays\<回放>.txt            比分、每个进球、踢球次数、故障
python replay.py events replays\<回放>.txt -p A1    A1 的事件（踢球、进球、故障……）
python replay.py around replays\<回放>.txt 3        第 3 个进球前 20 步逐步回看
python replay.py state replays\<回放>.txt 500       第 500 步的完整状态 + ASCII 小地图
python replay.py player replays\<回放>.txt B2 --from 100 --to 140
                                                    B2 每步应有的观测（含队友消息）和实际执行的动作
python replay.py gif replays\<回放>.txt 300 360 片段.gif
```

参赛者可以写：`baseline`、`midfield`、`sparring`、`template`、`.cpp` 路径（自动编译）、`.exe` 路径。

## 提示

- `match.py run` 的结果按"胜 1 平 0.5"算得分率，本地对手和正式比赛不同，只用来比较自己的版本。
- 程序的调试输出（标准错误）保存在回放旁边的 `_stderr` 目录里（`g<局号>_p<球员号>.txt`）。
- 复盘工具只列事实。想知道两人有没有同时扑球、有没有人回防，用 `player` 看两人各自每步的观测和实际执行的动作，或在 `viewer.html` 里打开"队友消息"。
- 计时是进程 CPU 时间，每步 10 毫秒。`check` 和 `play` 会显示每步平均和最多用了多少。电脑忙的时候测得会偏大，留出余量。
- 正式比赛时程序在 AppContainer 沙箱里运行（不能开子进程、不能读写文件，见 `RULES.md` 第 8 节）。开源版本地 `match.py` 默认 `--sandbox none`：不做身份隔离，但每步 CPU 计时、超时强杀、内存 1 GB、不能开子进程仍由裁判的 Job Object 限制；想按赛时环境复查就加 `--sandbox appcontainer`。`build.py` 编译前会做源码检查（不能包含 `windows.h` 等系统头文件，见 `RULES.md` 第 9 节）。
- 用 `--sandbox appcontainer` 时若报"沙箱目录都不能用"（杀毒软件拦截新复制出来的 exe 时会这样）：设环境变量 `SOCCER_BOX_ROOT` 指向一个能运行新 exe 的目录再试。开源版未设置该变量时只使用 `arena.exe` 旁边的 `_soccer_box\`；失败就停止，不再回退到主办方机器的固定目录。设置该变量时只使用指定目录。
