# 造桥大赛 · 选手包

先读 `RULES.md`（完整规则、赛程、材料表、第 1～6 关说明）和 `RULES_决赛.md`（第 7 关攻防）。交卷文件放 `submission/`。

| 文件 | 用途 |
|---|---|
| `RULES.md` | 规则（选手版） |
| `RULES_决赛.md` | 第 7 关：攻防（决赛桥 + 捣蛋车） |
| `saboteur.py` | 第 7 关的捣蛋车检查与攻防对局（`python bridge.py duel ...` 会调用） |
| `bridge.py` | 模拟器前端：检查设计、算造价、跑场景、生成回放网页（需要 Python 3.9+，不需要第三方库；JSON 用 UTF-8，带不带 BOM 都行） |
| `bridge_sim.exe` | 物理内核（`bridge.py` 自动调用；Windows x64） |
| `viewer.html` | 回放查看器（`--html` 生成的网页已内嵌数据，双击打开；也可以打开 viewer.html 后拖入 `--replay` 导出的 JSON） |
| `levels/levelN.json` | 第 N 关的关卡文件（地形、锚点、预算）；第 7 关是 `levels/levelfinal.json` |
| `levels/N/sample*.json` | 第 N 关的场景（就是正式评分用的场景） |

常用命令：
```
python bridge.py check 1 my_level1.json
python bridge.py run 1 my_level1.json
python bridge.py run 1 my_level1.json levels/1/sample2.json --html replay.html
python bridge.py check final submission/final_bridge.json
python bridge.py duel submission/final_bridge.json submission/saboteur.json --html duel.html
```
每次 run / duel 都会自动在 `archive/` 下存档。
