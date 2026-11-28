# 选手工作区

> 当前比赛规则：**v1.9.2**（ruleset `v15`，1500 tick）。`RULES.md` 是协议与经济基线，
> `RULES_V15.md` 是 v1.9.2 增量，冲突时以 `RULES_V15.md` 为准。

> 这是**引擎侧**的参考工作区：正式赛的 Starter 选手，加上 `botkit/`、`refbot/` 共用库。
> 原工程发给选手的完整包在 `algorithm-contest/rts-contest@model-name/workspace/`（未随本副本提供），
> 里面另有 `arena/`（引擎与裁判源码）、两个预编译基线与 `本地对局.bat`；在本目录里直接用
> `submission/` 与 `bots/` 即可。

> 正式赛编码期，你只需要动 `submission/`。其余目录是规则、工具和陪练。

## 目录

```text
workspace/
├── RULES.md               完整规则与行协议（先读）
├── RULES_V15.md           v1.9.2 增量规则（当前比赛）
├── prompt.md              发给模型的原始任务书
├── README.md              本文件
├── clock.py               编码期计时器（只读）
├── submission/decide.go   你的提交物
├── botkit/                可选工具库：协议、地图解码、一步寻路
├── refbot/                baseline@1 共用策略库（只读）
├── acebot/                baseline@2 共用策略库（只读）
└── bots/                  两个官方基线（旧 7 参考 Bot 见 experiment-records/reference-bots-archive/）
    ├── baseline1/         中等战术（refbot 攻城剖面）
    └── baseline2/         极强（acebot：集火/微操/护旗/劫旗/遗产竞速）
                          （正式赛第 6 名选手 Starter 就是 submission/decide.go）
```

## 三条命令

```bash
# 1. 编译你的 Bot
go build -o bots/bin/submission.exe ./workspace/submission

# 2. 自检：换边两局，打印判定、VP、总分、故障、确定性
go run ./cmd/arena check -seed 20261109

# 3. 换对手
go run ./cmd/arena check -a bots/bin/submission.exe -b bots/bin/nomad.exe -seed 20261110
```

以上命令都在本目录（`community/contests/war-of-desert/`）执行。

## 沙箱（正式赛会对你的程序做的事）

正式赛不是「随手起个进程跑你的 Bot」：裁判用 Windows Job Object 给它加了硬约束。
这些约束在本地也能验证，用的**和赛场是同一份实现**：

```bash
# 1. 先确认本机沙箱可用（沙箱内必须无法派生子进程）
go run ./cmd/sandbox

# 2. 用正式赛的限制把你的 Bot 跑起来：真实 hello + 逐拍 tick
go run ./cmd/sandbox -exe bots/bin/submission.exe -turns 200
```

（工具在 `cmd/sandbox/`：它要 import `internal/sandbox`，所以必须跑在
引擎模块内。选手包里的等价命令是 `cd arena && go run ./cmd/sandbox`。）

限制清单：

| 项 | 值 | 说明 |
|---|---|---|
| 活跃进程数 | 1 | 派生子进程会被系统拒绝 |
| 单进程内存 | 512 MiB | 超了分配失败或被 kill |
| Job 内存 | 1024 MiB | 同上，整组上限 |
| 工作目录 | 一次性临时目录 | 相对路径写不到仓库，也读不到别人 |
| 环境变量 | 白名单 | 只有 SystemRoot/TEMP/TMP/PATH 等 |

**没有覆盖的**：文件系统读权限与出网。别指望沙箱能挡住 `os.Open("C:/...")`——
手册 §3.2 把这两条写成选手义务，裁判侧靠赛后封存哈希校验兜底。

## 计时

```bash
python -X utf8 clock.py
```

显示已用时间、90 分钟目标、120 分钟强制收卷时刻。它与正式赛务记录同源，但最终以组织方的 `赛务记录.json` 为准。

## 提交纪律

- 只改 `submission/`（以及你自己新增的包）；
- 不要动 `RULES.md`、`clock.py`、`botkit/`、`refbot/`、`bots/` 里的官方文件；
- 不要试图写文件、联网、起子进程；
- 收卷时冻结的是 `submission/decide.go`。没有它，这届就没有你。

## 参考 Bot 的用法

读源码比读文档快：

- `baseline1`：中等战术、攻城消耗，看它怎么排科技与生产顺序；
- `baseline2`：极强对手，看 acebot 的集火评分、伤兵微操、护旗/劫旗决策；
- 旧 7 参考 Bot（vanguard/bulwark/swarm/nomad/sapper/hunter/warden）源码在 `experiment-records/reference-bots-archive/`，可读；
- `submission`：基线模板，注释最多，适合从它改。

想快速验证一个想法，直接改 `submission/decide.go` 里的一行常量再跑 `check`，比空想快。
