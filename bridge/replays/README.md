[English](README_en.md)

# 回放

双击 `.html` 就能在浏览器里逐帧看，不用联网：拖动时间轴、悬停看每根杆件的材料和受力百分比、看事件列表（断裂、传感器触发、液压动作、车辆到达 / 失败）。

收录规则不按好看或结果挑：第 1～6 关各取关卡分最高的那份设计，跑该关第一个场景（`sample1`）；第 7 关从 72 局里用 `random.Random(20261107)` 抽 3 局。

| 文件 | 内容 | 结果 |
|---|---|---|
| [L1-opus.html](L1-opus.html) | 第 1 关 · Claude Opus 5.5 · 小轿车 左→右 | 通过，造价 3531.70 |
| [L2-sol62.html](L2-sol62.html) | 第 2 关 · GPT-6.2 Sol · 小轿车 左→右 | 通过，造价 7701.80 |
| [L3-sol62.html](L3-sol62.html) | 第 3 关 · GPT-6.2 Sol · 早高峰 A | 通过，造价 7714.88 |
| [L4-sol62.html](L4-sol62.html) | 第 4 关 · GPT-6.2 Sol · 船 12 秒 | 通过，造价 11712.20 |
| [L5-sol62.html](L5-sol62.html) | 第 5 关 · GPT-6.2 Sol · 轿车先 | 通过，造价 8620.74 |
| [L6-sol62.html](L6-sol62.html) | 第 6 关 · GPT-6.2 Sol · 船 16 秒 卡车先 | 通过，造价 17212.99 |
| [L7-bridge-hunyuan-vs-saboteur-sol62.html](L7-bridge-hunyuan-vs-saboteur-sol62.html) | 第 7 关 · 腾讯混元 Hy4 的桥 × GPT-6.2 Sol 的捣蛋车 | 攻破 |
| [L7-bridge-doubao-vs-saboteur-sol62.html](L7-bridge-doubao-vs-saboteur-sol62.html) | 第 7 关 · 豆包 Seed Evolving 的桥 × GPT-6.2 Sol 的捣蛋车 | 攻破 |
| [L7-bridge-sol62-vs-saboteur-kimi.html](L7-bridge-sol62-vs-saboteur-kimi.html) | 第 7 关 · GPT-6.2 Sol 的桥 × Kimi K3.1 Pro 的捣蛋车 | 守住 |

回放是赛后用同一引擎重跑生成的（引擎确定，与评分时一致），第 7 关回放里写的是正式计分结论。重新生成这 9 份：`python tournament/replays.py`；生成全部 54 份设计 × 每个场景、6 份参考答案和 72 局：`python tournament/replays.py --all out/回放`（约 200 份、一两百 MB）。

任意一份设计也可以自己生成：

```text
cd workspace
python bridge.py run 3 ../designs/opus/level3.json levels/3/sample2.json --html replay.html
```
