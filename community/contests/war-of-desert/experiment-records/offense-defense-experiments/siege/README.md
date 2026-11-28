# experiment-records/offense-defense-experiments/siege · 攻守实验产物

> 当前采纳参数：**母舰 HP 1250 ｜ 守方工事预算 16 ｜ tickLimit 1260**。
> 其余文件是标定过程中的中间产物，保留用于溯源；旧参数的数据会在下表标注。

## 先看这两个

| 文件 | 内容 |
|---|---|
| [`结论.md`](结论.md) | 全部标定过程：两次参数改版、两个工程缺陷（寻路卡死、布防图不单调）、BT 评分 |
| [`sweep.md`](sweep.md) | 当前参数的胜率汇总、时间轴标定曲线、攻方增强试过的杠杆 |

## 数据文件

| 文件 | 内容 | 对应参数 |
|---|---|---|
| `bt_pool.csv` / `.json` / `.md` | 4 人池 × 12 有序对 × 16 seed = 192 局 + 双参数 BT 榜单 | HP 1250 / 工事 16 / 1260 |
| `t45_tl1260_20261201.csv` | 调参集 40 局（45.0% 的一半） | 同上 |
| `t45_tl1260_20261241.csv` | 样本外 40 局 | 同上 |
| `t45_thirds.csv` | 第三批抽检 20 局 | 同上 |
| `v3_tl1350_40.csv` / `v3_tl1350_holdout.csv` | 1350 tick 的 80 局（历史，51.2%） | tick 1350 |
| `v2_fb16_40.csv` / `v2_fb16_holdout.csv` | 1200 tick 的 80 局（历史，37.5%） | tick 1200 |
| `hp*_clean.csv` + `summary.csv` | 逐台微操版参考 Bot 的 10 档 HP 扫描（400 局，历史） | tick 1200 / 工事 24 |

## 重建

```bash
cd war_of_desert

# 当前参数的 40 局
go run ./cmd/siege -seeds 20261201-20261240 -workers 4 -out experiment-records/offense-defense-experiments/siege/t45_tl1260_20261201.csv

# BT 榜单（先按手册 §10 编译 pool 变体）
go run ./cmd/siegepool \
  -pool "v1=bots/bin/pool_v1.exe,v2=bots/bin/pool_v2.exe,v3=bots/bin/pool_v3.exe,v4=bots/bin/pool_v4.exe" \
  -seeds 20261301-20261316 -workers 4 -out experiment-records/offense-defense-experiments/siege/bt_pool
```

## 口径

- **摧毁率 = 攻方胜率**：这个模式下攻方只有「摧毁守方母舰」一种胜利条件。
- 上表所有数据均为 **0 故障**、`verify.ok=true` 全通过。
- `hp*_clean.csv` 那批是旧版（逐台微操）Bot 的成绩，只对旧 Bot 成立，不能与当前数据混用。
