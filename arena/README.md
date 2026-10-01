# arena · 通用对战平台

第 3 集（夺旗）起使用的通用工具：运行单局、排赛程、跑比赛、Bradley–Terry 评分。一道新游戏只要提供规则引擎（一个 `Game` 类，接口见 `core.py` 开头的说明），就能直接用这套工具办比赛。

| 文件 | 作用 |
|---|---|
| `core.py` | 启动选手程序（`.cpp` 自动编译并按源码缓存、`.py`、可执行文件）、按时限收发每回合指令、跑一局并记录回放 |
| `schedule.py` | 排赛程：每种人数若干张地图，每张图把座位完整轮换一圈，让每位选手出场和座位次数均衡 |
| `bt.py` | Bradley–Terry 拟合，基准锚定 0，各人数等权，按地图重抽样给出区间 |
| `tournament.py` | 正式赛三步：`plan`（冻结赛程）→ `run`（可中断续跑）→ `rate`（出成绩，可合并多个赛事目录） |
| `clock.py`、`run.cmd`、`run.sh` | 打包进选手包的计时器和找 Python 的启动脚本 |

```bash
python arena/tournament.py plan  赛事配置.json  赛事目录
python arena/tournament.py run   赛事目录 --workers 4
python arena/tournament.py rate  赛事目录
```

赛事配置的字段见 `tournament.py` 开头；`ctf/tournament/formal-*.json` 是实际例子。

每回合限时默认首回合 2 秒、之后 50 毫秒，对机器负载敏感：每局程序数 × 并发局数，最好不超过 CPU 线程数的约 3/4。
