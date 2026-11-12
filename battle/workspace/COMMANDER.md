# 指挥程序接口

头文件是 `commander/commander.hpp`（不要改），例子是 `examples/示例指挥.cpp`（演示了点名、架枪、骑兵冲撤、射火药桶、推雪球、读镇山兽状态、指挥主帅）。

## 1. 写法和编译

单个 C++17 文件 `submission/commander.cpp`，包含 `commander.hpp`，写两个函数：

```cpp
#include "commander.hpp"

void on_start(Game& g) {
    // 开局调用一次。这时已经收到第一回合的状态，g.units / g.props 都有内容；在这里下的指令和第一回合的一起发出
}

void on_tick(Game& g) {
    // 每 0.1 秒调用一次，在这里下指令
    for (Unit* u : g.by_tag("点名组")) {
        Unit* e = g.nearest_enemy(u->x, u->z);
        if (e && u->order != 打) g.attack(u->id, e->id);
    }
}
```

- 编译：`python build.py`（等于 `g++ -O2 -std=c++17 -static -I commander submission/commander.cpp -o build/commander.exe`，正式比赛用的就是这条命令）。`python play.py ...` 发现源码改过会自动重编。
- `commander.hpp` 里已经有 `main()`，不要自己写。
- 兵种、状态、指令在头文件里是中文枚举名（`新兵`、`站着`、`打` 等），源文件请存成 UTF-8。
- 同一个程序打所有场次：用 `g.mode`（`"level"` 闯关、`"duel"` 1v1、`"melee"` 混战）和 `g.map`（例如 `"第3关_拔炮台"`、`"樱丘"`、`"涨潮沙洲"`）分支。
- 全局变量和 `static` 变量在一场里一直保留（每一场是一个新进程）。

## 2. 规则

- **每 0.1 秒一回合**（物理每秒 60 步，每 6 步问一次程序）。
- **必须是确定的**：不许读时钟、不许用随机数、不许依赖多线程的先后。
- **调试输出写标准错误**（`g.log("...", ...)` 或 `std::cerr`），**不要写标准输出**：标准输出是给模拟器的指令通道，每回合一行、开头是回合号。万一写了：模拟器每回合只认开头是这一回合回合号的那一行，别的行（多打的调试信息、上一回合多出来的行）直接丢掉，每丢一行记一次"输出无效"（结果里 `commanders[k].invalid` 加一，`faults` 里记一条），这一回合的指令照常生效，下一回合也不受影响。想看自己的调试输出：`war.exe run 关卡.json --deploy 布阵.json --cmd build\commander.exe --stderr-dir 目录`，内容在 `目录\g0_p0.txt`。
- **时间**：每回合的计算时间（CPU 时间）上限 20 毫秒；第 0 回合（含程序启动）上限 2000 毫秒。
  - 某一回合超时：这一回合的指令作废，兵继续执行上一条指令。
  - 累计超时 50 次，或者某一回合迟迟不回复（墙上时钟超过 2 秒，第 0 回合 10 秒）：程序被结束。
- **崩溃、退出、被结束**：从那一刻起程序不再下指令，**每个兵继续执行它最后收到的那条指令**（守的继续守，走的走到了就原地守，打的目标没了就回到自由）；从没收到过指令的兵保持布阵文件里的开场姿态（"原地守"的继续守，"自由"的按默认行为）。这和"指令一直有效，直到下一条"是同一条规则。
- 其他限制：内存 1 GB；不能再开子进程；一行指令最长 256 KB；标准输出累计不超过 64 MB，还没被读走的输出最多积压 4096 行。
- 编译不过或者没交：所有场次按布阵文件里的姿态加默认行为打。

## 3. 每回合能看到什么

坐标全是**我方视角**（x 向右，z 朝敌人），和布阵文件一样。

**`g.units`**：全场还在场上的单位（含还没消失的尸体，尸体 `alive == false`）。

| 字段 | 含义 |
|---|---|
| `id` | 编号，整场不变 |
| `kind` | `新兵 盾卫 长枪兵 弓手 骑兵 大锤 炮仗兵 投石车 大块头 镇山兽 主帅` |
| `team`、`seat`、`mine` | 阵营、座位；是不是我的兵 |
| `state`、`alive` | `站着 / 倒地 / 起身 / 阵亡`；前三种都算活着 |
| `x, y, z`、`vx, vz` | 位置（y 是脚底高度）、水平速度 |
| `hp` | 血量 |
| `cooldown`、`attacking` | 离下次能出手还有几秒（投石车 = 离下一发）；正在起手 |
| `braced`、`holding`、`fuse` | 长枪兵已经架好枪；正在原地守；炮仗兵已经点火 |
| `facing` | 朝向（弧度，0 = 朝 +z，正值往 +x 偏） |
| `order`、`target` | 当前指令（`自由 原地守 走到 打 瞄 行军 不动 推`）和正在打的目标编号 |
| `tag` | 布阵文件里的标签（只有自己的兵有；敌人的标签看不到，关卡文件里写的标签只在 `play.py` 和结果里出现） |

主帅（第 4 关）也在 `g.units` 里，`kind == 主帅`、`mine == true`；`g.lord()` 直接拿到他。

**`g.props`**：道具。`type` 是 `keg`（火药桶）、`snowball`（雪球）、`flag`（军旗）、`barrier`（木栅）、`gate`（木栅门）、`pillar`（立石）；另有 `id`、`x y z`、`hp`、`alive`、`triggered`（雪球已经松开 / 火药桶已经点着）、`tag`（关卡文件里起的，例如 `路障甲`、`北近`）、`team`（军旗属于哪一方）。

**`g.flying`**：飞行中的石头和滚动中的雪球：位置、速度、`land_x, land_z`（预计落点，估算）。箭太多，不给。

**`g.bosses`**（第 5 关）：

| 字段 | 含义 |
|---|---|
| `id` | 镇山兽在 `g.units` 里的编号（位置、血量去那里看） |
| `state` | `游走 / 刨地 / 冲撞 / 晕` |
| `dir_x, dir_z` | 冲撞锁定的方向（单位向量）；刨地完成之前是上一次的 |
| `next_charge` | 离下一次冲撞开始还有几秒（正在冲 = 0） |
| `state_left` | 这个状态还剩几秒；冲撞时是还要冲几米 |

**`g.events`**：上一回合（0.1 秒内）发生的事，每条一个字符串：

| 例子 | 含义 |
|---|---|
| `terrain_removed 桥中段` | 带这个标签的地形被移除了（第 2 关桥塌）。`g.terrain_removed("桥中段")` 可以随时查 |
| `destroyed 37 gate` | 37 号道具没了（`keg` `gate` `pillar` `barrier` …） |
| `released 12` | 12 号雪球松开了 |
| `boss 41 paw` | 镇山兽换状态：`wander`（游走）、`paw`（刨地）、`charge`（冲撞）、`stun`（晕） |
| `sortie 坡顶` | 主办方的这一队守军出击了（规则见 RULES.md 第 5 节"分队出击"）；没有队名的单个守军写他的编号 |
| `rally_go 桥中段` | 第 2 关桥断以后改道、在集结的敌军出发了（见 RULES.md 7.2；后面是触发改道的地形标签） |

**其他**：`g.time`（第几秒）、`g.tick`（第几回合）、`g.time_limit`、`g.team`、`g.seat`、`g.water`（当前水面高度，没有水是很小的数）、`g.water_at(t)`、`g.score_zone`、`g.info[兵种]`（`cost hp dmg cycle windup reach speed radius climb`）。

**地图（开局给一次）**：

| 函数 | 作用 |
|---|---|
| `g.height(x, z)` | 地面高度；空洞、墙里、水下返回 -1000（地形图按开局算，桥塌了不会更新） |
| `g.walkable(x, z)` | 站得住吗 |
| `g.path_dist(x0, z0, x1, z1, climb)` | 绕路走过去有多远（米），走不到返回 -1。`climb` 是能翻过的高度：步兵 1.1，骑兵 0.5。按半米格子和普通步兵的体型算，不考虑兵的体宽：比步兵宽的兵（大块头、骑兵）可能过不去树木之间这类窄缝，会被挡住；引擎给兵寻路也是这样 |
| `g.in_zone(名字, x, z)` | 在不在某个区里（例如第 1 关的 `泥塘`、1v1 的计分区） |
| `g.in_score_zone(x, z)` | 在不在计分区里 |
| `g.to_world` / `g.to_local` | 我方视角和关卡文件的世界坐标互换 |

**查找的便利函数**：`g.mine()`（我还活着的兵）、`g.enemies()`、`g.by_tag(标签)`、`g.mine_of(兵种)`、`g.unit(id)`、`g.prop(id)`、`g.nearest_enemy(x, z[, 最远])`、`Game::dist(a, b)`。

## 4. 指令

指令下了以后一直有效，直到下一条。一回合里对同一个兵下多条，最后一条算数。

| 函数 | 含义 |
|---|---|
| `g.move(id, x, z)` | 走到 (x, z)，自动绕开悬崖和墙，路上不还手。走到（0.4 米内）以后变成在那里原地守 |
| `g.move_straight(id, x, z)` | 同上，但走直线、不绕路，自己负责别掉下去 |
| `g.attack(id, 目标)` | 追上去打这个目标。目标可以是敌方单位，也可以是道具（射火药桶、砸木栅）。目标没了就回到自由。弓手射道具和射人一样：箭要真的碰到桶身才算射中，被桥面、墙、人挡住就没中 |
| `g.hold(id)` | 原地守：守住现在这个位置，只打够得着的。长枪兵站定 0.5 秒就架好枪；弓手射射程内最近的 |
| `g.free(id)` | 自由 = 默认行为 |
| `g.aim(id, x, z)` | 只对弓手和投石车：朝一个地点射（封路口、预判）。对别的兵种无效 |
| `g.detonate(id)` | 只对炮仗兵：立刻点火，0.6 秒后炸 |
| `g.push(id, 雪球)` | 走到雪球背后顶住它，顶满 1 秒它就滚下去；大锤是走到跟前敲一下。目标要是还没松开的雪球 |

- 主帅只听 `move`、`move_straight`、`hold`，别的指令会被忽略；没下过指令时他站着不动。
- 投石车不能走，`move` 对它无效；可以 `attack`（打一个目标，会按目标当时的速度预判）和 `aim`。
- 对敌人的兵、死了的兵、不存在的编号下令，这一条被忽略（计入结果里的 `ignored_orders`），同一回合别的指令照常生效。
- 开头是这一回合回合号、但后面整行看不懂的应答（例如指令字母写错），这一回合的指令全部作废，记一次 `invalid`，程序不会被结束，下一回合照常。开头不是这一回合回合号的行按第 2 节"调试输出"那条丢掉，不影响这一回合的应答。
- 指令不保证被准确执行：兵会被挤开、撞退、打倒，倒地的 2.5 秒里什么都做不了。要自己发现"他没到位"再补救。
- 同一步里所有人同时出手：每一步先按这一步开始时的局面算出每个兵、每支箭、每块石头、每次爆炸打中了谁，再一起扣血、击退、判倒地和阵亡。两个人同一步互砍会同时生效，结果和编号、座位无关。
- 道具挨打和兵一样，"飞出去的东西不认人，手里的武器只打敌人"：箭、石头、爆炸打到军旗、木栅、门都会掉血，不分敌我；近战只有敌军砍得动军旗。火药桶被箭射中、被石头砸中、被爆炸波及都会点着。

## 5. 直接用 war.exe（一般用不到）

`play.py` 都是调 `war.exe`。想自己写批量试的脚本，可以直接用它：

```
war.exe run levels\第3关_拔炮台.json --deploy my.json --cmd build\commander.exe --json 结果.json --replay 回放.wrp
war.exe run maps\樱丘.json --deploy 我.json --deploy 对手.json --cmd build\commander.exe --cmd -
war.exe check levels\第5关_镇山兽.json my.json
war.exe batch --workers 8 < 任务.txt
war.exe info
```

- `run`：`--deploy` 和 `--cmd` 按座位顺序写（闯关一个、1v1 两个、混战四个），`--cmd -` 表示这个座位不用程序。退出码 `0` 过关、`1` 没过、`3` 布阵不合法、`2` 命令行或文件有错、`4` 引擎超时。标准错误最后一行是一句话的摘要。
- **引擎超时**：一场的墙上时钟超过 `--match-wall-s` 秒（默认 120；`0` = 不设）还没跑完，引擎结束这一场，结果是 `{"id":0,"error":"engine_timeout",…}`，没有胜负。正常一场一两秒，就算程序每回合都用满 20 毫秒，一整场也只有二三十秒，所以这是引擎自己的毛病，遇到请把布阵和程序写进 `notes.md`。用调试器单步跟自己的程序时可以设成 `0`。
- `batch`：任务文件每行一个 JSON：`{"id":1,"map":"levels\\第1关_草甸迎击.json","deploys":["a.json"],"cmds":["build\\commander.exe"]}`，可选 `"replay"`。每跑完一场打印一行结果 JSON（带 `id`），顺序不保证。适合一次试很多种布阵。出错的场次（地图读不了、布阵数量不对、引擎超时）是带 `"error"` 字段的一行，其他场次照常跑完。
- 结果 JSON 里常看的字段：`passed`（过没过）、`reason`（闯关：`win:eliminate` 全歼、`win:destroy:镇山兽` 打倒镇山兽、`lose:destroy:军旗` 军旗倒了、`lose:dead:主帅` 主帅阵亡、`timeout_lose` 时间到没达成、`eliminated` 我方全灭）、`secs`、`teams[k]`（`start_units alive_units start_cost alive_cost zone_cost kill_cost`，混战另有 `rank rank_pts kill_pts score`）、`seats[k]`（`deploy_cost deploy_ok deploy_errors faults ignored_orders`）、`level_info`（标签还剩几个、军旗/主帅/镇山兽血量）、`commanders[k]`（`status timeouts invalid avg_ms max_ms`；`status` 是 `ok` / `crash` / `lost` / `timeout_limit` / `output_limit` / `start_fail`，`invalid` 是看不懂的应答和丢掉的多余输出行一共几行）。1v1 另有 `winner`（胜方座位，`-1` = 平）和 `reason`：`annihilation`（一方全灭；同时全灭算平）、`timeout_zone`（时间到，山头上活着的兵造价多的一方胜）、`timeout_cost`（时间到、山头上双方都没有兵，全场剩下的兵造价多的一方胜）、`timeout_draw`（时间到，以上都一样）。
- **正式评分用 war.exe 的默认设置。** `--cfg`（改数值）、`--secs`（改时限）这类选项只用来调试，用它们跑出来的结果不算数。
