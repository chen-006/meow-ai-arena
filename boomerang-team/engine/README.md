# 团队赛引擎与陪练源码

本目录存放第 12 集 5v5 团队赛的裁判源码与三档陪练源码（规则引擎头文件 `dart.hpp` 位于 `../workspace/dart.hpp`）。

## 文件说明

| 路径 | 说明 |
|---|---|
| `arena.cpp` | Windows 裁判与批跑引擎（负责 Job Object 资源限制、CPU 计时、5v5 三模式布阵与战术单加载、回放输出）。开源版移除了原文件中主办方本地沙箱回退路径，对局行为与比赛时完全一致（正式赛与本地对战默认均使用 `--sandbox none`）。 |
| `json.hpp` | 裁判使用的轻量 JSON 读写头。 |
| `接口说明.md` | 裁判命令行参数、批跑协议与结果 JSON 字段说明。 |
| `sparring/weak.cpp`、`mid.cpp`、`strong.cpp`、`objective.hpp` | 弱、中、强三档团队赛陪练源码（比赛时选手包中只提供编译后的 `.exe`）。 |

## 编译方法

在上一层 `boomerang-team/` 目录执行：

```bash
python build_engine.py
```

将自动编译生成 `workspace/arena.exe` 以及 `workspace/bots/weak.exe`、`mid.exe`、`strong.exe`。
