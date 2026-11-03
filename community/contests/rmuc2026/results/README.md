# 公开基准示例赛

2026-11-01 在固定 2026 阵容、Docker 后端、每轮 200 ms 预算下实际执行。两个公开示例策略，1 个种子，红蓝各一局，共 **2 局**；每局完整 420 秒仿真时间，均无策略错误。

| 局 | 种子 | 红方 | 蓝方 | 胜方 | 策略错误 | 实际耗时 | 回放 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 11 | assault | guard | assault（红） | 0 | 101.597 s | [静音俯视](example/0000.html) |
| 1 | 11 | guard | assault | assault（蓝） | 0 | 109.801 s | [静音俯视](example/0001.html) |

| 策略 | 胜 / 平 / 负 | 积分 |
| --- | --- | --- |
| assault | 2 / 0 / 0 | 2 |
| guard | 0 / 0 / 2 | 0 |

这是社区比赛包的流程验证示例，不属于节目官方集数或新模型正式赛，也不足以推断普遍胜率或机器性能。实际条件、同时段短故障注入和局限见 [CONDITIONS.md](../CONDITIONS.md)。

- [逐局原始结果](example/games.jsonl) · [成绩 JSON](example/standings.json) · [赛程与代码指纹](example/run.json)
- `example/sources/` 保留该轮实际运行的代码；清单在 `example/manifests/`。
- HTML 从同一场比赛按每秒采样生成；仅压缩保留的展示字段，没有重新模拟或改动比赛结果。

复现：进入 `community/contests/rmuc2026/`（或独立导出的选手包目录）后构建运行时，然后用新输出目录执行相同赛程。运行时间和最终 digest 不承诺跨平台逐位一致。

```sh
python build.py --docker
python tournament.py bots/assault.py bots/guard.py --seeds 11 --output practice/reproduce-001
```
