# 需求变更（第二阶段）

运营提了三条新需求。请在 `src/` 的代码上实现它们（`src/` 里是本队主程交上来的代码，你也可以大改、重写，或者从 `baseline/` 重新开始）。

除了下面写明的改动，其余所有行为都和第一阶段的基线**完全相同**（包括基线里所有的怪行为）。

## 输入格式变化

`PARAMS` 行末尾多了两个整数：

```
PARAMS batteryMax chargeRate lowThreshold safetyMargin reportEvery hotRadius agingEvery carryCost
```

保证 `agingEvery ≥ 1`，`carryCost ≥ 1`。其余输入格式不变。

## 变更 1：订单老化

订单等得越久，优先级越高。在第 `tick` 个 tick 派单时，待分配订单 o 的**有效优先级**为：

```
有效优先级 = o 的原优先级 + (tick − o 的到达 tick) / agingEvery      （整数除法）
```

派单排序改为：**有效优先级**大的在前 → 到达 tick 小的在前 → 编号（按整数）小的在前。

- 这里的 tick 就是本次派单所在的 tick（当前 tick），本 tick 新到的订单等待时长为 0。
- 因停机被重新放回待分配队列（`REQUEUE`）的订单，到达 tick 仍是它最初的到达 tick（与原来一样）。
- `ASSIGN` 输出的格式不变。

## 变更 2：载货耗电

机器人处于 `DELIVERING`（送货中）状态时，每移动一步（包括让路 `SIDESTEP`）消耗 `carryCost` 点电量；其他状态每步仍消耗 1 点。

- 本步耗电按机器人**移动前**的状态计算。例如 `TO_PICKUP` 的机器人走到取货点的那一步耗 1；从取货点出发、处于 `DELIVERING` 的每一步才耗 `carryCost`。
- **停机条件**：移动阶段，机器人不在目标格时，原来是"电量为 0 则停机"，改为"**电量 < 本步耗电**则停机"。本步耗电按机器人当前状态计算：`DELIVERING` 为 `carryCost`，其余为 1。停机的检查位置和后续处理都不变：已在目标格的机器人不会停机；这项检查仍在"目标格被封锁""走不过去""下一步被占"等判断之前，所以电量不够的机器人即使本来只会等待或让路，也会停机。
- **派单电量检查**：原来的条件"电量 ≥ a + b + c + safetyMargin"改为

  ```
  电量 ≥ a + b × carryCost + c + safetyMargin
  ```

  其中 a、b、c 的含义不变（a：机器人到取货点；b：取货点到送货点；c：送货点到最近的可通行充电桩）。
- 其他用到电量的地方（低电量阈值、选择充电桩时的电量判断等）都不变。

## 变更 3：报表新增 `aged`

`REPORT` 行在 `pending=` 之后新增一个字段 `aged=`：

```
REPORT pending=P aged=A idle=… to_pickup=… delivering=… to_charger=… charging=… dead=… hot=X,Y,K
```

A 是输出报表时，所有待分配（`PENDING`）订单中，满足 `(当前 tick − 到达 tick) / agingEvery ≥ 1`（即有效优先级已高于原优先级）的订单数。

## 验收

- `workloads2/public/` 里有 4 组公开负载（`.in` + 标准输出 `.out`），`workloads2/edge/` 里有 30 个小型边界用例。
- `workloads2/regression/` 里是互测中别队找到的、本队主程代码**输出不一致的反例**（已转换成新格式，见 `TAKEOVER.md`），也必须全部通过。
- `python tools/bench2.py`：编译 `src/`，跑全部第二阶段用例，逐字节比对，报告 CPU 时间。
- `python tools/gen2.py 配置名 种子`：生成更多同类第二阶段输入（但没有标准输出可以对照）。

正式评测使用第二阶段的隐藏负载，评分规则见 `TASK.md`。
