# parity · 移植基线钉定记录（2026-11-14）

Go 原生引擎重写的移植基线与行为对照参考。对拍口径见 `Dev/Plan_Beta.md` §5.2
（Go 自确定性 + golden trace + 绝对锚点测试；**不与 JS 逐位对拍**）。

## 移植基线（源语义）

| 项 | 值 |
|---|---|
| 仓库 | `Dev/MVP`（独立 Git 仓） |
| 提交 | `b9643b0`（v1.1 合并）+ **v0.11 收尾 WIP（未提交，30 文件）** |
| 工作树 diff SHA-256 前 16 位 | `ad32ea3a23e035a8` |
| 可恢复快照 | MVP 仓库 `stash@{0}`（`git stash store` 钉定，不动工作树） |
| 基线 rulesHash（JS 口径） | `4e0a8e2b` |
| 提交状态 | v0.11 WIP 的 commit 被 Mimosa L3 钩子拦截（29 项均为离线工具链模式误报，已复核接受）；提交暂缓，待钩子侧放行后再补 |

注：v0.11 WIP 中已含一处修复——`test/vision_test.js` 绊线哈希 sha1→fnv1a
（绊线只做回归报警不做加密），基线值重记为 `a5caaeb2`。

## 行为指纹（对照参考，非门禁）

- 生成器：`node engine/parity/gen_baseline.js`（在 `war_of_desert/` 下运行）
- 产物：`baselines/mvp.json` —— 5 seeds × greedy vs tactical × 镜像共 10 局摘要
  （winner/level/turns/vp/kills/cards）；规则版本记录在 JSON 内部 `rulesHash` 字段
- 用途：Go 引擎完成后同 seed 对照**量级与结构**（局数/回合数/胜负/VP/击杀），
  差异显著即人工复核；不要求逐位一致。

## 验收口径（Plan_Beta §5.2 / §6.2）

1. Go 引擎自确定性：同 seed + 同 orderLog 重放 → 逐帧哈希一致 + 同 result。
2. golden trace：`engine/golden/` 冻结归一化帧序列，`go test` 回归比对。
3. 绝对锚点：JS `rules_test` §9 的字面量断言逐条移植为 Go 测试。
