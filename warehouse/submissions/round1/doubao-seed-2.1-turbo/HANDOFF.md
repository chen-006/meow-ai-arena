# 交接文档 —— WMS 仓储机器人调度仿真优化版

## 一、代码结构

| 文件 | 作用 |
|---|---|
| `common.h` | 公共头文件：数据结构、全局变量声明、内联函数 |
| `main.cpp` | 主函数 + 全局变量定义 |
| `sim.cpp` | 仿真主体：输入读取、主循环（7 个阶段）、收尾统计 |
| `util.cpp` | 工具函数：BFS、距离计算、日志、字符串工具 |
| `old_stuff.cpp` | 旧版派单 + 旧版 BFS 缓存（当前不启用，保留以便回滚） |

编译命令：`g++ -O2 -std=c++20 src/*.cpp`

## 二、主要优化点（与基线相比）

### 2.1 BFS 全局复用 + generation counter
- 基线每个 BFS 都 new 一个 `vector<vector<int>> d(H, vector<int>(W, BIGNUM))`，有 H+1 次分配 + 全表 memset。
- 优化版用两个 flat 数组 `BFS_DIST` 和 `BFS_VISIT`（各 W*H 大小），`BFS_VISIT` 存 generation 号，每次 BFS 只需 `BFS_GEN++`，不用 memset。
- `_bfs_get(x, y)` 内联，先检查 `BFS_VISIT[y*W+x] == BFS_GEN`，不匹配返回 BIGNUM。
- 注意：BFS 行为和基线完全一致——起点不论是否可通行都距离为 0，之后只走 `OK()` 的格。

### 2.2 封锁检查从 `set<pair<int,int>>` 改为 2D bool 数组
- 基线 `BLK` 是 `set<pair<int,int>>`，每次 `OK()` 调 `BLK.find(...)` 是 O(log n)。
- 优化版 `BLK_ARR[y][x]` 是 `vector<vector<bool>>`，O(1) 访问。
- 同时增加 `BLK_VERSION` 计数，每次 BLOCK/UNBLOCK 递增，用于 BFS 缓存失效。

### 2.3 机器人状态从 string 改为 int 枚举
- `RST[i]` 现在是 `int`，取值 `S_IDLE / S_TO_PICKUP / S_DELIVERING / S_TO_CHARGER / S_CHARGING / S_DEAD`。
- 输出时用 `robotStatusStr(s)` 转回字符串。
- 避免了每 tick 大量字符串比较。

### 2.4 订单从 `map<string, vector<string>>` 改为 struct 数组 + 哈希索引
- `ORDS` 是 `vector<Order>`，每个 Order 存 px/py/dx/dy/prio/arrival/status/robotIdx/idStr。
- `ORD_MAP` 是 `unordered_map<string, int>` 把订单 id 字符串映射到索引。
- `PEND` 存订单索引（int），不再存字符串。
- `ROID[i]` 存订单索引（int），-1 表示无。
- 状态比较从字符串相等变成 int 比较。

### 2.5 事件预解析
- 基线每 tick 把所有事件行 SPLIT 一遍，O(E*T) 总时间。
- 优化版在 `ReadAll` 时一次性解析所有事件为 `vector<Event>`，存好类型、字段。
- 主循环用指针 `EVT_PTR` 推进，事件按 tick 非递减，所以每个事件正好处理一次。

### 2.6 移动用 BFS 目标缓存
- `MOVE_BFS_CACHE` 缓存从目标 (tx,ty) 出发的 BFS 距离场。
- 同一 tick 多个机器人去同一个目标（如同一充电桩），只有第一个做 BFS，后面复用。
- 缓存失效条件：目标不同 或 `BLK_VERSION` 变化。

### 2.7 充电桩位置预计算
- `CHG_LIST` 在 `ReadAll` 时扫一遍地图存好，CHG_DIST / CHG_NEAR 直接遍历这个列表，不再全图扫。

### 2.8 手写 I2S
- 基线用 `stringstream` 做整数转字符串，慢。
- 优化版手写 digit 拼接，速度提升明显。

### 2.9 `dumpLog` 用 `'\n'` 代替 `endl`
- `endl` 每次 flush，慢。换成 `'\n'`。

## 三、几个"怪行为"（和基线一致，改之前要确认清楚）

1. **DIST(a,b) 的不对称性**：当 a 不可通行但 b 可通行时，`DIST(a,b)` 从 a 出发 BFS，a 本身距离 0 但不能扩展，所以 b 距离是 BIGNUM；反过来 `DIST(b,a)` 就能得到真实距离。基线代码就是这样的，不能改。
2. **派单选机器人用 `<=`**：距离相等时选编号大的（后面的覆盖前面的），不是小的。基线注释说"选编号小的"但代码是 `a <= best` 后更新，实际选编号大的。**这个行为必须保留，输出顺序依赖它。**
3. **充电判断 `RB[i] * 10 >= PRM[0] * 9`**：90% 电量时认为充满离开，不是 100%。用整数乘法避免浮点。
4. **CHG_DIST 的 BFS 和 BFS 函数等价但写法不同**：基线里 CHG_DIST 自己写了一份 BFS（用 `q.push(p); q.back() = make_pair(nx, ny);` 这种奇怪写法），但行为和 BFS 函数一样。优化版统一调用 `BFS_FILL`。
5. **BFS2 和 BFS 完全等价**：基线注释说"别合并，上次合并出过事"，实际上两个函数逻辑完全一样。优化版统一用 `BFS_FILL`。
6. **PEND 删除用"构造新 vector"**：基线每次删除一个元素都重新构造整个 PEND vector，不是 erase。优化版保留了这个行为（虽然慢，但行为完全一致，而且 PEND 通常不大）。
7. **报表里的热点：行优先，y 小的先，x 小的先**：`n > hc` 才更新，相等不更新，所以热点取最先遇到（行优先最靠上最靠左）的那个最大值格子。

## 四、需求变更时该动哪里

| 想改什么 | 动哪个文件 | 关键点 |
|---|---|---|
| 派单规则（分配策略） | `sim.cpp` 第 4 步 "派单" | 现在是"优先级高→到达早→编号小"排序，每单选最近够电的 IDLE 机器人 |
| 移动规则（避让/路径） | `sim.cpp` 第 6 步 "走" | BFS 从目标往回搜，选距离最小的邻格；被占时等 4 tick 后 SIDESTEP |
| 充电逻辑（阈值/速度/充满判断） | `sim.cpp` 第 3 步 + 第 5 步 + 报表 | 第 3 步是充，第 5 步是去充电的判断 |
| 报表内容/格式 | `sim.cpp` 第 7 步 "报表" + `Finish()` | 所有输出在 writeLog/writeRaw 里 |
| 电量/救援规则 | `sim.cpp` 第 2 步 "救援" + 第 6 步 DEAD 处理 | 趴窝 100 tick 后救援，恢复半电 |
| 事件类型 | `sim.cpp::ReadAll` 事件解析 + 第 1 步事件处理 | 加新事件类型要同时加枚举和解析 |
| BFS 相关 | `util.cpp::BFS_FILL` + `common.h::_bfs_get` | 所有距离计算都走这俩，改一处全局生效 |

## 五、已知风险 & 容易踩坑的地方

1. **BFS 起点不检查 OK()**：`d[sy][sx] = 0` 不管起点能不能走。如果需求变更说"起点是墙就返回 INF"，要小心加。
2. **BFS 缓存的有效性**：`MOVE_BFS_CACHE` 只在 BLOCK/UNBLOCK 时失效。如果以后加了别的改变地形的事件（比如新增墙、新充电桩），记得也要递增 `BLK_VERSION`。
3. **`ORD_MAP` 同步问题**：订单状态变化时（PENDING→ASSIGNED→PICKED→DONE 等），只改 `ORDS[idx].status`，ORD_MAP 的 key 不变。如果以后加了"重命名订单"之类的需求，记得同步更新 ORD_MAP。
4. **PEND 的顺序**：基线是 push_back 追加，删除时保留原顺序。优化版也是如此。如果派单排序依赖 PEND 的原始顺序（比如优先级相同时按到达顺序），不要改 PEND 的插入/删除方式。
5. **机器人编号和输出名称**：机器人 i 的名字是 `"R" + I2S(i)`，**编号从 0 开始**。`findRobot("R12")` 返回 12。
6. **输出日志顺序**：所有 `writeLog(t, ...)` 调用顺序决定输出顺序。主循环 7 个阶段的顺序不能乱，否则输出顺序不对。
7. **SUMMARY 的 open 统计**：包含 PENDING + ASSIGNED + PICKED 三种状态的订单，不包含 DONE/CANCELLED/LOST。

## 六、攻击面提醒（可能被击穿的点）

1. **BFS 的 `BFS_VISIT` generation 溢出**：`int` 类型，BFS_GEN 从 0 递增。20 万 tick × 每 tick 几十次 BFS ≈ 几百万次，远小于 2^31，不会溢出。但如果以后 T 变得极大（> 2^31 次 BFS），要考虑回绕。
2. **`ORD_MAP` 哈希冲突**：用的是 `unordered_map<string, int>`，订单 id 是正整数字符串，攻击者可能构造大量哈希冲突的 id 导致性能退化。如果被攻击，可以换成 `map` 或者自定义哈希。
3. **`BLK_ARR[y][x]` 越界**：所有调用前都有 `INMAP(x,y)` 或 `OK(x,y)` 检查，理论上安全。但如果以后新增调用点忘了检查，可能越界。
4. **`MOVE_BFS_CACHE` 数组复制**：每次缓存未命中时 `MOVE_BFS_CACHE.dist = BFS_DIST` 会复制整个 W*H 数组（200×200=160KB）。如果目标变化频繁（比如每 tick 每个机器人目标都变），反而慢。目前看公开负载上是赚的。
5. **事件解析的 SPLIT 只在 ReadAll 时做一次**：如果事件行格式有异常（比如多个空格、tab），SPLIT 处理和基线一致（多种空白符都当分隔符）。注意 `\r` 也被当分隔符。

## 七、基线对照

- `baseline/` 是原始代码的只读副本。
- 优化版在 5 个公开负载 + 5 个随机种子 + 4 个其他配置随机种子上均验证过输出与基线逐字节一致。
- 如果怀疑某个优化引入 bug，可以逐步回退：先看 `FLAG_FAST_BFS`/`USE_NEW_DISPATCH` 等开关，再看数据结构替换。

—— 主程交接，祝队友好运 🫡
