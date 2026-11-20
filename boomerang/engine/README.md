# 引擎和陪练

这一集的规则引擎是单头文件 [`workspace/dart.hpp`](../workspace/dart.hpp)，裁判是 [`workspace/arena.cpp`](../workspace/arena.cpp)，都在选手包里，选手和裁判用的是同一份。这里放的是比赛时**只发了编译好的程序**的三档陪练的源码：

| 文件 | 内容 |
|---|---|
| `sparring/weak.cpp` | 弱档陪练 |
| `sparring/mid.cpp` | 中档陪练 |
| `sparring/strong.cpp` | 强档陪练：补偿反应延迟的推演、每步十几个候选走法打分、挡镖、冲刺前摇，计算量固定（不看时钟，结果确定） |
| `sparring/说明.md`、`说明_踢飞新规则.md` | 主办方做陪练时的说明：思路、每步耗时实测、强对中、中对弱的胜率 |

`python build_engine.py`（在 `boomerang/` 下）把它们编译到 `workspace/bots/`，同时编译 `workspace/arena.exe`。用 w64devkit 的 GCC 16.2.0 编出来和比赛时的 exe 逐字节相同（开源时实测，四个都相同）。

说明里提到的 `原型\bots\pro_core.hpp`、`选手包\`、`match_py.diff` 是主办方做游戏时的内部文件，没有收录；`strong.cpp` 是拼好的单文件，不依赖它们。
