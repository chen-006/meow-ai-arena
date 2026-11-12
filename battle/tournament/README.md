[English](README_en.md)

# 计分和复现

## 一条命令核对榜单

```text
python tournament/score.py
```

读 [results/eval/](../results/eval/) 里的 945 场逐局结果和布阵检查结果，用比赛时的计分脚本 [scoring.py](scoring.py) 重算总榜、闯关榜、对抗榜，和 [results/](../results/README.md) 里的 json 逐项、md 逐字核对，最后打印"核对通过"。几秒钟，只要 Python 3.9+，不需要编译器和模拟器。

## 从选手作品重打全部对局

```text
python build_engine.py
python tournament/score.py --rerun
```

1. `build_engine.py` 从 [engine/src/](../engine/src/) 编译模拟器到 `workspace/war.exe`（需要 Windows、g++、cmake、ninja；第一次会下载 Jolt Physics 源码，见 [engine/README.md](../engine/README.md)）。
2. `score.py --rerun` 在 `rerun/` 下搭一份和比赛时一样的赛事目录（`kit/` = 选手包，`sealed/<席位>/submission/` = `bots/` 里的作品），然后交给 `scoring.py` 的评测阶段：用选手包 `build.py` 的同一条命令编译九份指挥程序，`war.exe check` 检查 63 份布阵，`war.exe batch` 跑完 945 场。
3. 逐场和 `results/eval/results/` 核对（除了耗时类字段：指挥程序的 CPU 计时 `first_ms`、`avg_ms`、`max_ms`、`cpu_total_ms`，引擎的 `t_phys`、`t_logic`，以及批次内序号 `id`），再出榜、核对三张成绩表。

8 线程大约 5～10 分钟（看机器）。选项：`--workers N` 改线程数；`--replays` 同时存下全部 945 场回放（约 3 GB）；`--keep` 核对通过也保留 `rerun/` 下的输出。

模拟器是确定性的，同一个 `war.exe` 在任何机器上跑出同样的结果。有两件事可能让重打和正式成绩不同：

- **编译器不同**：指挥程序和模拟器都要用 GCC 16.2.0（w64devkit）编，比赛时就是它；别的编译器的浮点优化可能不同。`build_engine.py` 会打印 `war.exe` 是否和比赛时逐字节相同。
- **机器慢很多**：指挥程序有 CPU 时间限制（[COMMANDER.md](../workspace/COMMANDER.md)），正式评测里九份程序都没有超时；机器太慢时个别程序可能超时，那一场就会不同。

## 文件

| 文件 | 内容 |
|---|---|
| [scoring.py](scoring.py) | 比赛时的评测和算分代码，原样。文档字符串里提到的 `host.py` 是主办方不公开的运营脚本（启动选手、计时、收卷），这里不需要它 |
| [config.json](config.json) | 计分配置：从赛事配置里只取 `scoring.py` 用到的字段（关卡参考造价、权重、对抗图、混战座位、选手顺序），数值和比赛时相同 |
| [score.py](score.py) | 复现脚本：搭目录、调 `scoring.py`、核对 |

赛程没有随机种子：闯关每人每关一场；1v1 每两人在两张图上换座位各一场；混战按 `config.json` 里的选手顺序取全部 4 人组合，每组第一位坐南边，另三位轮遍其余座位（`scoring.make_plan`）。
