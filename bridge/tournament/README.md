[English](README_en.md)

# 复现正式成绩

需要 Python 3.9+ 和 g++，不需要第三方 Python 包。

```text
python build_engine.py          编译物理内核 → workspace/bridge_sim.exe
python tournament/score.py      重算全部成绩并和 results/ 核对
```

`score.py` 做的事：

1. 跑 [reference/](../reference/) 里 6 份参考答案，确认全部场景通过，得到参考造价。
2. 跑 [designs/](../designs/README.md) 里九人第 1～6 关的设计：每关全部公开场景（`workspace/levels/N/sample*.json`），全过才算过关，关卡分 = round(100 × 参考造价 ÷ 造价, 2)。
3. 第 7 关：每人的 `final_bridge.json` × 其他 8 人的 `saboteur.json`，72 局，用选手包的 `saboteur.duel` 模拟。**正式计分**：这一局里 4 辆标准车全部按时完好到达 = 守住，否则 = 攻破。
4. 算闯关分、攻防分、总分，打印 72 局矩阵和总榜，再逐项比对 [results/scores.json](../results/scores.json)（每关分数、造价、场景通过数、文件 SHA-256、总分）和 [results/duels.json](../results/duels.json)（每局每辆车的状态和时刻）。全部一致时打印“核对通过”，退出码 0。

`--levels-only` 只算第 1～6 关；`--workers N` 设并行进程数。`score.py` 设了 `BRIDGE_NO_ARCHIVE=1`，不会在当前目录写存档。

**本地单局对打和正式计分的区别**：`python bridge.py duel 桥.json 捣蛋车.json` 打印的“裁决”按 [RULES_决赛.md](../workspace/RULES_决赛.md) 第 3 节逐条判，其中捣蛋车没有在 12 秒内完好到达时，会按“没有捣蛋车”重跑一次车队来判。正式计分只看有捣蛋车的那一局里标准车队是否全部到达。72 局里有 12 局两者结论不同（捣蛋车自己没按时到达，但车队也没有全部到达），这 12 局按正式计分记为攻破。车队各车的结果两边完全相同，可以在 `duel` 的输出里直接看到。

## 单关和单局

```text
cd workspace
python bridge.py run 5 ../designs/sol62/level5.json
python bridge.py run 5 ../designs/sol62/level5.json --no-archive -q
python bridge.py duel ../designs/kimi/final_bridge.json ../designs/opus/saboteur.json --html duel.html
```

`run` 的退出码：0 全部场景通过，1 设计不合法，2 参数错误，3 有场景没通过。

## 回放

`python tournament/replays.py` 重新生成 [replays/](../replays/README.md) 收录的 9 份；`--all <目录>` 生成全部。

## 引擎版本

比赛时的 `bridge_sim.exe` sha256 为 `4186445eec8989b344126c6e60eb8df8de0a3ca6543f83d06868a623c87209e7`。用 w64devkit 的 GCC 16.2.0 运行 `build_engine.py`（`-O2 -std=c++17 -static -municode`）得到逐字节相同的文件，`build_engine.py` 会打印是否一致。其他编译器得到的文件字节不同；引擎只用四则运算、`sqrt`、`fabs`，不要加 `-ffast-math`，结果是否一致以 `score.py` 的核对为准。
