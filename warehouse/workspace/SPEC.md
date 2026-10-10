# 仓储机器人调度仿真：行为规格（v2.0，2022 年）

> **⚠ 注意：这是 2022 年系统上线时写的文档，之后代码改过很多次，文档没有同步更新。**
> 文档里的大部分内容仍然准确，但有些地方已经和代码不一致；代码里的注释也可能过时。
> **一切以 `baseline/` 里基线程序的实际行为为准**：你的程序对任何合法输入，都必须输出与基线程序逐字节相同的内容（行尾的 CRLF 与 LF 视为相同）。

## 1. 输入格式

从标准输入读取，全部为空白分隔的文本：

```
W H T
<H 行，每行 W 个字符>
PARAMS batteryMax chargeRate lowThreshold safetyMargin reportEvery hotRadius
ROBOTS R
x y            （共 R 行，第 i 行是机器人 i 的初始位置，名字为 "R<i>"）
EVENTS E
<E 行事件>
```

- 地图字符：`.` 地面，`#` 墙（货架），`C` 充电桩（也是地面）。
- 坐标 `(x, y)`：x 为列，y 为行，从 0 开始。
- 事件行格式为 `tick 类型 参数…`，类型有四种：
  - `ORDER id px py dx dy priority`：新订单，取货点 `(px, py)`，送货点 `(dx, dy)`，优先级（越大越优先）；
  - `CANCEL id`：取消订单；
  - `BLOCK x y`：临时封锁格子；
  - `UNBLOCK x y`：解除封锁。
- 保证：事件按 tick 非递减排列；0 ≤ tick < T；订单编号是互不相同的正整数，没有前导零；初始位置是互不相同的地面格；地图四周是墙。

## 2. 基本概念

- **可通行**：在地图内、不是墙、没有被封锁。充电桩可通行（除非被封锁）。
- **方向顺序**：上 (0,−1)、右 (1,0)、下 (0,1)、左 (−1,0)。所有"按方向"的平局都按这个顺序。
- **行优先**：先比 y，再比 x。
- **BFS 距离 `dist(a, b)`**：
  - a == b 时为 0；
  - 否则 b 不可通行时为 INF；
  - 否则从 a 出发做四连通 BFS，**a 本身无论是否可通行都作为起点**，之后只经过可通行格，得到到 b 的步数；到不了为 INF。
  - 注意：a 不可通行时，`dist(a, b)` 与 `dist(b, a)` 可能不同。
- **机器人状态**：`IDLE` 空闲、`TO_PICKUP` 前往取货、`DELIVERING` 送货中、`TO_CHARGER` 前往充电、`CHARGING` 充电中、`DEAD` 停机。
- **订单状态**：`PENDING` 待分配、`ASSIGNED` 已分配、`PICKED` 已取货、`DONE` 已送达、`CANCELLED` 已取消、`LOST` 丢失。
- **占据**：除 `DEAD` 以外的机器人占据它所在的格子。

## 3. 每个 tick 的流程

对 tick = 0, 1, …, T−1，依次执行：

### 3.1 外部事件

按输入顺序处理本 tick 的所有事件：

- **ORDER**：取货点或送货点不在地图内或是墙 → 拒绝，输出 `REJECT id`。否则订单进入 `PENDING`，到达时间记为当前 tick。
- **CANCEL**：
  - 订单不存在，或状态不是 `PENDING`/`ASSIGNED` → 输出 `CANCEL_FAIL id`；
  - `PENDING` → 订单变为 `CANCELLED`，输出 `CANCEL id`；
  - `ASSIGNED` → 负责的机器人立即变为 `IDLE`（清空订单和目标，连续等待次数清零），订单变为 `CANCELLED`，输出 `CANCEL id`。
- **BLOCK x y**：在地图内且不是墙时封锁（已封锁则无变化）。封锁不影响已经在该格上的机器人。
- **UNBLOCK x y**：解除封锁（未封锁则无变化）。

### 3.2 救援

按编号顺序检查每个 `DEAD` 机器人：若停机已满 100 个 tick（当前 tick − 停机 tick ≥ 100），且它所在格没有被其他机器人占据，则电量充满，变为 `IDLE`，连续等待次数清零，输出 `RESCUE 名字`。

### 3.3 充电

按编号顺序，每个 `CHARGING` 机器人电量增加 chargeRate（不超过 batteryMax）；达到 batteryMax 时变为 `IDLE`，输出 `CHARGED 名字`。

### 3.4 派单

1. 所有 `PENDING` 订单按以下顺序排序：优先级大的在前 → 到达 tick 小的在前 → 编号（按整数）小的在前。
2. 依次处理每个订单 o。候选机器人是：状态为 `IDLE` 且电量 ≥ lowThreshold 的机器人（在本 tick 前面的订单中已被分配的不再是候选）。对每个候选 r，计算：
   - `a = dist(r 的位置, 取货点)`
   - `b = dist(取货点, 送货点)`
   - `c = ` 在所有**可通行**的充电桩 q 中，`dist(送货点, q)` 的最小值
   - a、b、c 任一为 INF，或 电量 < a + b + c + safetyMargin，则 r 不合格。
3. 在合格者中选 a 最小的，相同取编号小的。选中后：机器人变为 `TO_PICKUP`，目标为取货点，连续等待次数清零；订单变为 `ASSIGNED`；输出 `ASSIGN id 名字 a`。没有合格者则订单保持 `PENDING`。

### 3.5 低电量去充电

按编号顺序，每个电量 < lowThreshold 的 `IDLE` 机器人选择一个充电桩：

1. 按行优先遍历所有可通行的充电桩 q，排除"被占用"的：有**其他**机器人处于 `CHARGING` 且位于 q，或处于 `TO_CHARGER` 且目标是 q。还要排除 `dist(机器人位置, q) + safetyMargin > 电量` 的。在剩下的充电桩中选 `dist` 最小的（相同取行优先靠前的）。
2. 若第 1 步没有结果：在所有可通行的充电桩中选 `dist(机器人位置, q)` 最小且不为 INF 的（相同取行优先靠前的），不管是否被占用。
3. 若仍没有结果（没有可通行的充电桩，或全都到不了），则不动作。否则机器人变为 `TO_CHARGER`，目标为该充电桩，连续等待次数清零，输出 `GO_CHARGE 名字 x y`。

### 3.6 移动

按编号顺序，处理状态为 `TO_PICKUP`、`DELIVERING`、`TO_CHARGER` 的机器人 r（前面的机器人本 tick 的移动会影响后面机器人看到的占据情况）：

1. 若 r 已在目标格，跳到第 6 步（到达）。
2. 若电量为 0：**停机**（见 3.8），结束。
3. 若目标格不可通行：等待（累计等待次数 +1，连续等待次数不变），结束。
4. 计算下一步：在 r 的四个相邻格中，只考虑可通行的，选 `dist(目标, 该格)`（即从目标出发的 BFS 距离）最小的，相同按方向顺序。若最小值为 INF：等待（累计等待 +1，连续等待不变），结束。
5. 若下一步格子被其他机器人占据：
   - 若连续等待次数 ≥ 4，且下一步格子**不是** r 的目标格：让路。按方向顺序找第一个可通行且未被占据的相邻格，移动过去（电量 −1，移动步数 +1，连续等待清零），输出 `SIDESTEP 名字`，然后进入第 6 步。找不到这样的格子则按下一条处理。
   - 否则等待：累计等待 +1，连续等待 +1，结束。
   
   若下一步格子没有被占据：移动过去，电量 −1，移动步数 +1，连续等待清零。
6. **到达**：若 r 位于目标格，连续等待清零，并按状态处理：
   - `TO_PICKUP`：订单变为 `PICKED`；机器人变为 `DELIVERING`，目标改为送货点；输出 `PICK id 名字`。本 tick 不再移动。
   - `DELIVERING`：订单变为 `DONE`，延迟 = 当前 tick − 订单到达 tick；输出 `DELIVER id 名字 延迟`；机器人变为 `IDLE`，清空订单和目标。
   - `TO_CHARGER`：变为 `CHARGING`，输出 `CHARGE 名字`。

### 3.7 报表

若 (tick + 1) 是 reportEvery 的倍数，输出一行：

```
REPORT pending=P idle=… to_pickup=… delivering=… to_charger=… charging=… dead=… hot=X,Y,K
```

- P 为 `PENDING` 订单数，其余为各状态机器人数。
- **热点**：对每个非墙格子（行优先遍历），统计与它曼哈顿距离 ≤ hotRadius 的**非停机**机器人数；取最大值 K，相同取行优先靠前的格子 (X, Y)。

### 3.8 停机

输出 `DEAD 名字`。若机器人在 `TO_PICKUP`：订单变回 `PENDING`（保留原到达 tick），输出 `REQUEUE id`。若在 `DELIVERING`：订单变为 `LOST`，输出 `LOST id`。机器人变为 `DEAD`，记录停机 tick，清空订单和目标。停机的机器人不再占据格子。

## 4. 输出格式

- 事件行格式为 `tick 事件 参数…`，按产生的先后顺序输出。
- 所有 tick 结束后，输出：
  ```
  SUMMARY delivered=… lost=… rejected=… cancelled=… open=…
  LATENCY sum=… max=… avg=…
  ROBOT 名字 x y 电量 状态 移动步数 累计等待次数      （每个机器人一行，按编号）
  ```
  - open 为状态是 `PENDING`/`ASSIGNED`/`PICKED` 的订单数；
  - avg 为 sum / delivered 的整数除法，delivered 为 0 时是 0。
- 所有数都是整数，没有浮点运算。
