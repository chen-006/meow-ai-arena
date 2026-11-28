# exp-v18-500-siege · v1.8.0 拆舰编成 500 局实验

> 规则 v1.8.0（ruleset v15，指纹 `90c3c937`，与 exp-v18-500 完全相同）｜ 1500 tick ｜ 6 名参考 Bot ｜ 510 局
> 本轮只改参考 Bot 建造表，不改任何规则数值。

## 本轮改动（相对 exp-v18-500）

| Bot | 改动 |
|---|---|
| Vanguard | 建造表加入 RAILGUN（顺序 RESEARCH → AAV → RAILGUN → LAV → LAV），开启 `Siege` |
| Bulwark | 建造表加入 ARTILLERY（RESEARCH → AAV → LAV → ARTILLERY），开启 `Siege` |
| Nomad | 建造表加入 CARRIER（RESEARCH → SCOUT → CARRIER → LAV） |
| Sapper | 不变（双 RAILGUN + Siege） |
| Swarm / Hunter | 不变 |

目的：让「射程压过母舰」的远程兵真正进入战场，验证能否把母舰斩杀率拉起来。

## 结果概览

| 指标 | 拆舰编成 | v1.8.0 全线 Push |
|---|---:|---:|
| 有效局 | 505/510 | 505/510 |
| 平局 | **114（22.4%）** | 107（21.0%） |
| 阈值胜 | **129（25.3%）** | 142（27.9%） |
| 时间到 | 366（72.5%） | 353（69.9%） |
| 决胜座位 seat0 : seat1 | **195 : 196（49.9 : 50.1）** | 201 : 197（50.5 : 49.5） |
| 平均时长 | 1383 tick | 1356 tick |
| 母舰被摧毁 | **10（2.0%）** | 10（2.0%） |
| 场均生产单位 | **15.33** | 17.94 |
| 每方场均科技 | 5.83 / 5.60 | 5.77 / 5.53 |
| 敌我最近距离中位 | — | 399 px |

## 远程兵实际建造量

| 单位 | 总数 | 折合每方每局 |
|---|---:|---:|
| RAILGUN | 337 | 0.33 |
| CARRIER | 241 | 0.24 |
| ARTILLERY | **43** | **0.04** |

对比上一轮：RAILGUN 224（0.22）、CARRIER 0、ARTILLERY 0。

生产构成：LAV 2702、SALVAGER 2189、RESEARCH 1012、SCOUT 994、RAILGUN 337、AAV 302、CARRIER 241、ARTILLERY 43。
总产量反而从 17.94 降到 15.33：昂贵单位挤占了廉价部队的产能。

## 判定来源（按座位）

| 判定原因 | seat0 | seat1 | 合计 |
|---|---:|---:|---:|
| 时间到 · 按胜利分 | 119 | 126 | 245 |
| 胜利分达到阈值 | 68 | 61 | 129 |
| 时间到 · 按旗控制时长 | 3 | 3 | 6 |
| 时间到 · 按母舰剩余 HP | 1 | 0 | 1 |
| 蓝方母舰被摧毁（seat0 胜） | 4 | 0 | 4 |
| 红方母舰被摧毁（seat1 胜） | 0 | 6 | 6 |
| 全部相同，平局 | — | — | 119（含 5 局无效） |

## 结论

1. **远程兵确实出现了**：RAILGUN 0.22 → 0.33 台/方/局，CARRIER 0 → 0.24，ARTILLERY 0 → 0.04。
2. **但量级太小，没有改变斩杀率**：母舰被摧毁仍是 10 局（2.0%），母舰终局 HP 99.5%。
   - ARTILLERY 950 CU + 250 RU 在 1350 tick 的经济里基本攒不出来；
   - RAILGUN 320 CU 也要排在经济/科技之后，平均一局不到半台。
3. **副作用**：昂贵单位挤掉廉价部队，总产量 17.94 → 15.33，阈值胜反而从 27.9% 降到 25.3%，
   平局 21.0% → 22.4%。座位公平仍然很好（49.9:50.1）。
4. 结论：**只改 Bot 编成无法建立拆舰路线**，瓶颈在「远程兵的性价比/可负担性」，需要动规则数值。

## 建议的规则侧选项（下一轮）

- **A. 降低远程兵造价**：RAILGUN 320 → 240 CU / 40 RU，ARTILLERY 950 → 650 CU / 180 RU，
  让两家能在一局内稳定造出 1–2 台。
- **B. 提高对母舰伤害**：给 RAILGUN/ARTILLERY 对 MOTHERSHIP 伤害 ×1.5，减少所需输出时间。
- **C. 降低母舰 HP**：1800 → 1400，让现有输出也能在 1500 tick 内完成击杀。
- **D. 回退编成**：承认拆舰路线在当前经济下不可行，回到 exp-v18-500 的全线 Push 编成。

## 复现

```bash
cd war_of_desert
go run ./cmd/arena build -config tournament/experiment_v17_500.json
go run ./cmd/arena plan  -config tournament/experiment_v17_500.json
go run ./cmd/arena run   -config tournament/experiment_v17_500.json
go run ./cmd/arena rate  -config tournament/experiment_v17_500.json
python -X utf8 scripts/exp_v16_metrics.py experiment-records/rule-experiments/exp-v18-500-siege
```
