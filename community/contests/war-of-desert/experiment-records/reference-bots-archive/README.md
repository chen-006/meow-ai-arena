# 参考 Bot 归档（v1.9.1 名册 · 7 个）

> 2026-11-23 预制 Bot 名册合并为 baseline@1 / baseline@2 两个基线，
> 原 7 个参考 Bot 的**源码与预编译 exe** 整体移入本目录存档。
> 调试过程见 `../bot-experiments/baseline-tuning/`。

## 名册与强度锚（v1.9.1 标定 714 局，指纹 393ee404）

| Bot | 风格 | BT | 胜/平/负 | 源码 |
|---|---|---:|---|---|
| Hunter | 轻甲集群猎杀母舰 | 11.0 | 134/4/66 | hunter/ |
| Vanguard | 正面推进 + 攻坚 | 0.0（锚） | 125/13/64 | vanguard/ |
| Swarm | 轻甲海抢点 | −9.1 | 124/9/69 | swarm/ |
| Warden | 远程攻城消耗 | −55.1 | 110/9/83 | warden/ |
| Bulwark | 防御推进 | −139.5 | 78/11/107 | bulwark/ |
| Sapper | RAILGUN 拆舰 | −226.9 | 59/5/140 | sapper/ |
| Nomad | 机动抢点 | −266.6 | 44/9/145 | nomad/ |

## 复现注意

- 源码共用 `war_of_desert/workspace/refbot` 策略库（该库保留在原位，baseline@1 仍使用）。
- **源码现位于 ASCII 路径**：可直接 `go build ./experiment-records/reference-bots-archive/<name>`，
  也可拷到临时目录编译；exe 输出路径（`-o`）不受限制。
- 历史配置 `tournament/experiment_v19_700.json` 等的 entries 指向旧路径
  `workspace/bots/<name>`；复现旧标定需把 source 改指本目录的 ASCII 副本。
- exe 为参考 Bot 协议层产物（规则由 enginehost 注入，与规则版本无关）；新规则下可直接复用或重编。
- 攻守模式实验 Bot（attacker/defender/pool_v1–4）归档在 `../offense-defense-experiments/bin/`。