# bots · 封存与编译产物

> 本目录只放“已经收卷的东西”。选手在编码期改的是 `workspace/submission/`，不是这里。

## 目录

```text
bots/
├── bin/                 编译产物（.exe），不入库，可随时重建
├── seal-records/             按赛季分册，文件名 = 配置名（生成物，不入库）
│   ├── formal_v19.json        第 6 季正式赛（v1.9.2）
│   ├── experiment_v19_700.json  v1.9.1 714 局标定
│   ├── formal_v14.json        第 5 季正式赛（历史）
│   └── experiment_v15.json    v1.5 实验（历史）
└── README.md            本文件
```

**为什么按赛季分册**：一个赛季一本账。早先只有一本 `bots/封存记录.json`，
最后一次 `build` 用哪份配置，它就记哪个名册——拿 v1.9.2 配置 build 一次，
v1.4 历史赛季的封存就被顶掉，`arena seal -config tournament/formal_v14.json`
会报名册不一致，看起来像源码被改，其实只是账本被换了。现在 `build` 只写自己那本，各赛季并存。

每本的内容：源码哈希稳定，`binSha256` 随编译器/环境变化，所以都不入库，
只为「当前这台机器上跑的就是这份源码」服务。
**赛季正式跑过的那一本**按赛季归档到 `results/<赛季>/封存记录.json` 并入库，
那是赛后审计的依据——详见 [`../results/README.md`](../results/README.md#审计)。

## 封存记录口径

`arena build -config tournament/<赛季>.json` 会：

1. 对每个 entry 执行 `go build -o bots/bin/<name>.exe ./<source>`；
2. 递归计算 `source` 下所有 `.go` 文件的 SHA-256；
3. 计算共用库 `workspace/botkit`、`workspace/refbot`、`workspace/acebot` 的 SHA-256；
4. 写入 `seal-records/<赛季>.json`（只覆盖这一本）。

只有“源码哈希 + 共用库哈希 + 编译产物”三者一致，才认为这名选手的参赛版本没有被替换。

## 重建

```bash
cd war_of_desert
go run ./cmd/arena build -config tournament/formal_v19.json   # 当前正式赛（v1.9.2）
go run ./cmd/arena build -config tournament/formal_v14.json   # 历史正式赛（v1.4）
go run ./cmd/arena build -config tournament/experiment_v15.json  # v1.5 实验（历史）
go run ./cmd/arena seal  -config tournament/formal_v19.json   # 只校验，出 SEAL_OK
```

编译失败、源码缺失、哈希不符，都按“未提交”处理，不进入赛程。

## 已封存选手（第 1 季）

| 选手 | 来源 | 说明 |
|---|---|---|
| Vanguard | `workspace/bots/vanguard` | 正面推进，BT 锚 |
| Bulwark | `workspace/bots/bulwark` | 经济防守 |
| Swarm | `workspace/bots/swarm` | 数量压制 |
| Nomad | `workspace/bots/nomad` | 机动抢点 |
| Starter | `workspace/submission` | 选手提交模板 |

共用库：`workspace/botkit`（协议与寻路）、`workspace/refbot`（baseline@1 策略）、`workspace/acebot`（baseline@2 策略）。
