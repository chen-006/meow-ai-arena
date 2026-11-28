# engine · 规则引擎

> 本目录只有一件事：把快照 oracle 变成裁判可调用的独立进程。

## 文件

| 文件 | 说明 |
|---|---|
| `oracle_host.js` | 宿主：stdin 收命令，stdout 回单行 JSON；内部 `require('./oracle/rules.js')` |
| `oracle/` | 冻结快照（`rules.js` / `mapgen.js` / `mapgen_mc.js` / `ballistics.js`）+ `MANIFEST.json` |

> `parity/` 下的对拍脚本与 `oracle/MANIFEST.json` 的 `source` 字段指向原工程的 `Dev/MVP/referee/`；
> 本社区副本未收录 `Dev/`，这些脚本只在原完整工程中可运行，冻结快照本身已随本目录提供。

## 命令流

```text
{"cmd":"new", seed, generator, tickLimit, tickMs, decideMs, initMs, vision}
  → {ok, hello, seed, generator, mapAttempt, rulesHash, protoVersion, tickLimit}
{"cmd":"encode","seat":0}                  → {ok, line}
{"cmd":"apply","seat":0,"line":"..."}       → {ok, count} | {ok:false,error}
{"cmd":"step"}                              → {ok, done, turn, frame?}
{"cmd":"note","seat":0,"ms":12.5,"fault":false} → {ok}
{"cmd":"forfeit","seat":0,"reason":"..."}   → {ok, done}
{"cmd":"end","winner":-1,"level":0,"reason":"..."} → {ok, done}
{"cmd":"result"}                            → {ok, result}
{"cmd":"verify"}                            → {ok, verify}
{"cmd":"bye"}                               → 退出
```

## 纪律

- 不读墙钟、不写文件、不取系统随机数；同一 `seed + orderLog` 必得同一结果。
- 不修改 `oracle/`。发现疑似缺陷登记到原工程 `Dev/Plan_Beta.md` §11，由 Go 引擎决定修正或有意复刻。
- 唯一输出通道是 stdout 单行 JSON；任何调试输出都会破坏协议。

## 替换为 Go 引擎（已完成 · 2026-11-14）

1. ✅ `cmd/enginehost` 实现同一套命令流（`new/encode/apply/step/note/forfeit/end/result/verify/bye`），
   规则语义在 `internal/sim`（从 MVP 合并版 b9643b0+v0.11 移植，移植时基于 v1.2 规则）；
2. ✅ 配置层落实 `engine.kind`：`oracle`（本目录快照，走 Node）| `native`（enginehost 二进制）。
   移植当时的配置 `tournament/formal_v12.json` 用 `native`（第 2 季 `formal_v11.json` 起即 `native`）；
3. ✅ golden trace：`engine/golden/`（6 条冻结 trace + MANIFEST）+ `internal/sim/golden_test.go`
   回归门禁；重生成用 `go run ./cmd/tracegen`；
4. ✅ 选手包、裁判、赛程、成绩契约不动（judge 只在 `host.go:Start` 加了
   「.js 走 node、否则直接拉起二进制」一个分支）。

对拍口径按原工程 `Dev/Plan_Beta.md` §5.2：Go 自确定性 + golden trace + 绝对锚点，
不与 JS 逐位对拍（规则版本分叉 + 超越函数 ulp 双重不可行）。移植基线与
JS 行为指纹见 `engine/parity/BASELINE.md`。攻守引擎（rules_siege.js 的移植）
也在 `internal/sim/siege.go`，`cmd/siege -engine native` 可切换。

这就是把引擎放在独立进程里的收益：换引擎不需要改赛制。

## 内部口径备忘（2026-11-15，阶段 1 观测增强）

- **被拒指令（Rejected）**：上限 **200 条/局**，超出静默丢弃；`turn` 记该指令
  **本应生效的 tick**（= orderLog 的 `t`，解码发生在 N 拍 step 之后、指令原定
  N+1 拍生效），与 orderLog 逐拍对照不错位。Game 与 SiegeGame 同口径。
- **帧（Frame）**：新增 `waitingQueue [2][]string`（等待队列）与
  `queueBlocked [2]bool`（队首 CU/RU 付不起、与 stepProduction 的等待条件一致）。
  `queue` 字段继续表示生产中（Active）队列。帧内容变化 → 帧哈希全变，
  golden trace 已重生成；旧赛季回放的逐帧哈希与新引擎不再逐位可比
  （rulesHash 不含帧格式，赛季存档的 rulesHash 不受影响）。
- JS 活引擎（Dev/MVP referee/rules.js）已同步以上两点。
