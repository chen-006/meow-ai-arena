# HANDOFF —— 交接文档（给接手的队友）

> 读这份文档前请先看 `common.h` 顶部的重构说明。
> 一句话总结：**输出与 `baseline/` 逐字节相同**（公开负载 + 随机对拍 + 边界用例都验过），
> 只换了内部表示、加了缓存、给 BFS 加了"观察点提前退出"。公开负载上快 10~20 倍。

---

## 1. 代码地图

| 文件 | 内容 |
|---|---|
| `main.cpp` | 全局变量定义 + `main()`。全局量集中在文件开头。 |
| `common.h` | 类型（`Order` / `Event` / 状态枚举）、全局量声明、`OK()/INMAP()/ISWALL()` 内联 |
| `util.cpp` | 字符串工具、**BFS 内核**、`DIST/CHG_DIST/CHG_NEAR/pickCharger/bfsRobotNeighborDist`、日志 |
| `sim.cpp` | `ReadAll` / `RunSim`（7 个阶段）/ `Finish` |

原来的 `old_stuff.cpp` **已删除**：里面的 `bfs_fast_cached`（按起点缓存 BFS）和 `oldDispatch`
都是死代码（`FLAG_FAST_BFS=0`、`USE_NEW_DISPATCH=1`），而且那个缓存就是当年"结果对不上"的元凶
——它按 `(sx,sy)` 缓存 BFS，却没考虑 `BLK`（封路）会变，所以缓存一旦命中就会用旧地图的距离。
新代码里 `BLK` 每次真正变化都会让 `g_blkGen++`，订单距离缓存带版本号，不会再犯这个错。

---

## 2. 性能改动清单（以及为什么不会改变输出）

1. **事件预解析 + 按 tick 分桶**（`ReadAll` 尾部）
   旧版主循环每个 tick 都把所有事件行 `SPLIT` 一遍，是 O(T·E) 次字符串切分。
   现在解析一次，用**稳定**计数排序按 tick 分桶，`g_evBegin[t]..g_evBegin[t+1]` 就是该 tick
   要按文件顺序处理的事件。语义完全等价（同一 tick 内仍按文件顺序）。

2. **状态枚举化**
   `RST[i]` 从 `"IDLE"/"TO_PICKUP"/...` 变成 `ST_*`；`ORD[id][6]` 从字符串变成 `OS_*`。
   日志里的名字由 `ST_NAME[]` 还原。**注意 `ST_NAME` 的顺序不能改**，它决定 `Finish()` 打印的
   `ROBOT ... <状态>` 文本。

3. **订单从 `map<string, vector<string>>` 变成 `vector<Order>` + `unordered_map` 索引**
   字段都是整数（`px,py,dx,dy,prio,arrival`），日志用 `Order::id` 原文。
   旧版 `ORD[id][5]` 存的是 `I2S(t)`，现在就是 `arrival = t`（同一个值）。

4. **`PEND` 从 `vector<string>` 变成有序 `multiset<int, OrdCmp>`**
   旧版每 tick `sort` 一份快照，比较器是
   `(优先级降序, 到达时间升序, S2I(订单号) 升序)`。
   `check_input.py` 保证订单号唯一且是标准十进制正整数，所以这个比较器在合法输入下是**全序**，
   `std::sort` 的结果唯一 —— 用同一个比较器的有序容器逐项相同。
   **不要**给 `OrdCmp` 加新的并列规则，也不要改成无序容器。

5. **`BLK` 从 `set<pair>` 变成平数组 `vector<char>`**，`OK()` 从 O(log n) 变成一次数组访问。
   `g_blkGen` 只在**真正**插入/删除成功时自增（重复 BLOCK 不自增）。

6. **`OCC()` 从"遍历所有机器人"变成查 `LIVE[]` 平表**
   `LIVE[cell]` = 停在该格且没死的机器人编号（-1 表示没有）。
   **不改动位置的地方一定不要漏改 `LIVE`**：目前会动它的地方是
   机器人移动、SIDESTEP、死亡、救援、`ReadAll` 初始化。
   （CANCEL 一个 ASSIGNED 单不会移动机器人，所以不用动 `LIVE`。）

7. **BFS 内核**（`util.cpp` 顶部）—— 这是加速的大头
   - 平数组 + 时间戳（`g_stamp`），每次 BFS 不用清零整张表；
   - 平队列 `g_qx/g_qy`，替掉旧版 `std::list`（旧版每入队一个点 malloc 一次）；
   - 观察点（`g_mark`）+ **提前退出**：BFS 按距离非递减出队，格子一旦被赋值距离就是最终值，
     所以"所有观察点都拿到值"就可以停。旧版永远搜完整张图。
   - `bfsAll`：等所有观察点；`bfsFirst`：等最近的观察点（可带 `maxDist` 上限，同层并列可
     按 `g_rank` 取扫描顺序靠前的）。
   **正确性依据**：只影响"搜索多少"，不影响"任何已赋值格子的距离"，所以对外结果不变。
   注意 `bfsRobotNeighborDist` 只观察机器人 4 个**可走**邻居 —— 不可走的邻居旧版会 `continue` 掉。

8. **派单剪枝**（`RunSim` 第 4 步，都写成"保守剪枝"，不满足就一定没人能接单）
   - 没有 IDLE 车 → 整个派单循环跳过；
   - `maxIdleBat < PRM[2]` / `< |px-dx|+|py-dy| + PRM[3]` / `< need` → 跳过该单；
   - 机器人循环里 `RB[i] < need` 或 `曼哈顿距离 > best` → 跳过（曼哈顿是真实距离的下界，
     而旧版是 `a <= best` 才更新，所以跳过不影响结果）。
   - `b = DIST(取货点, 送货点)`、`c = CHG_DIST(送货点)` 只跟订单和地图有关，旧版在每个机器人
     身上重算，现在按 `(订单, g_blkGen)` 缓存（`Order::bMemo/cMemo/memoGen`）。
   **`a <= best` 这个 `<=` 是"一样近取编号大的"，别改成 `<`。**

9. **充电桩选择**（第 5 步）
   旧版对每个桩遍历所有机器人问"被占了吗"，而且是在每个机器人身上现算的 ——
   所以同一 tick 里**前面刚被派去充电的车也算占位**。现在用 `usedMark[]`，并且派完一个立刻置位。
   **这里很容易改错**，改动前请对照 baseline 的 `RunSim` 第 5 步。
   `pickCharger` 的 `limit = RB[i] - PRM[3]`，超过这个距离的桩旧版也会 `continue`。

10. **报表热点**（第 7 步）
    旧版 `H*W*NR` 暴力数；现在对每个活着的机器人，把它的曼哈顿菱形按行用差分加到 `hdiff`
    （每行 `W+1` 个槽），再逐行前缀和 + 扫描。
    判定规则原样保留：从 `y=0` 起、每行 `x` 从小到大，**严格更大**才更新（`hc` 初值 -1，
    所以第一个非墙格子一定会把 `hc` 抬到 0）。并列时取扫描顺序靠前的那个。

11. **日志缓冲**
    `LOGBUF` 是 `std::string`，`dumpLog()` 一次性 `fwrite`。旧版逐行 `cout << ... << endl`
    （每个 `endl` 都 flush）。
    `writeLog()` 里的统计（`DELIVER` → `CNT[0]/CNT[4]/CNT[5]`，`LOST` → `CNT[1]`，
    `REJECT` → `CNT[2]`）**仍然是按消息第一个词判断**，和旧版一模一样 —— 别绕过它直接改 `CNT`。

---

## 3. 从基线里挖出来的"怪行为"（都是**故意的**，改需求时注意）

1. **`DIST(ax,ay,bx,by)` 不对称**
   - `ax==bx && ay==by` → 直接返回 0，**不看起点能不能走**；
   - 否则如果终点 `!OK` → `BIGNUM`；
   - 否则从**起点**做 BFS（起点即使是墙/封路格，距离也记 0，只从它的可走邻居往外扩）。
   所以"车停在封路格上"和"取货点被封路"是两种不同的情形，结果不一样。

2. **`CHG_DIST(x,y)` 的起点也不要求可走**，而且它只统计**可走**的充电桩。
   如果 `(x,y)` 本身就是可走的充电桩 → 返回 0。

3. **SIDESTEP 只要求"邻居可走且没人"，不要求它更靠近目标**（`sim.cpp` 第 6 步）。
   也就是说车可能往反方向挪一格。这是线上行为，必须保留。

4. **`CHG_DIST` 旧版那段 `q.push(p); q.back() = make_pair(nx,ny);`**
   看着像 bug，实际等价于 `q.push(make_pair(nx,ny))`，所以新代码直接按正常 BFS 处理，结果一致。

5. **DEAD 的车不算占位**（`OCC` 跳过 `DEAD`），所以活车可以开到死车那一格上。
   此时死车不会被救援（`OCC` 为真），要等活车开走。`LIVE[]` 已经正确表达了这个语义。

6. **救援只充到 `PRM[0]/2`**（注释里写"充满电恢复"，是过时注释）。

7. **`ORDER` 事件里取货点/送货点是墙或越界 → `REJECT` 并且不建订单**，
   但**同一 tick 内 `CANCEL` 排在 `ORDER` 前面的话，`CANCEL` 会 `CANCEL_FAIL`**
   （因为订单记录是处理到 ORDER 那一刻才建的）。`ReadAll` 里因此**没有**预建订单记录。

8. **`CANCEL` 一个 ASSIGNED 的单**会把车直接变回 `IDLE` 并清空目标，**但不清 `RDS`**。

9. **报表 `REPORT ... hot=` 的 `hc` 可能是 -1**（整张图全是墙时 `hx=hy=hc=-1`）。

10. **`(t+1) % PRM[4]`** —— 报表在第 `PRM[4]-1`、`2*PRM[4]-1`... tick 出。
    `check_input.py` 保证 `reportEvery >= 1`，所以不会除零。

---

## 4. 想改某类逻辑该动哪里

| 需求方向 | 该看哪里 |
|---|---|
| 派单规则 / 优先级 | `OrdCmp`（排序键）、`RunSim` 第 4 步的机器人筛选与 `RB[i] >= a+need` |
| 电量 / 充电 | 第 3 步（充）、第 5 步（去充）、`pickCharger` / `CHG_NEAR` / `CHG_DIST` |
| 报表 | 第 7 步 + `Finish()`；字段拼在 `string s = "REPORT ..."` 那一行 |
| 地图 / 封路 | `BLK` + `g_blkGen`（**任何改地图的地方都要 `g_blkGen++`**，否则订单距离缓存会用旧的） |
| 新事件类型 | `Event` 结构 + `ReadAll` 的解析分支 + 主循环第 1 步 |
| 新订单字段 | `Order` 结构；注意 `memoGen` 缓存失效 |

---

## 5. 风险点 / 我没做的事

- **`OrdCmp` 依赖"订单号唯一"**。`check_input.py` 会拦掉重复订单号。如果以后允许重复，
  派单顺序会与旧版不同（旧版是 `std::sort` 不稳定排序，本来就不可复现）。
- **`ReadAll` 对事件行的字段数做了越界保护**（缺字段当 0）。旧版是 `vector::operator[]`
  越界（UB）。`check_input.py` 保证字段数正确，所以正式负载上两者一致；只有非法输入才会看到差别。
- **没有做 BFS 结果的全图缓存**。曾经考虑过，但因为 `BLK` 变得频繁，缓存命中率低、内存又大，
  收益不划算；现在的做法是"每次都搜，但只搜需要的那一小片"。
- 内存：`W*H <= 40000`，BFS 内核几张平表 + `hdiff`，实测峰值 < 10MB，离 1GB 上限很远。
- 随机对拍脚本在 `work/`（`fuzz.py`、`diff_gen.py`），改动后建议跑一遍：
  `python work/fuzz.py 240` 和 `python work/diff_gen.py 2`。
