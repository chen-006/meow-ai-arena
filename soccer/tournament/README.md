[English](README_en.md)

# 赛程、复算与重打

| 文件 | 内容 |
|---|---|
| `halftime.json.gz`、`final.json.gz`、`individual.json.gz` | 原始冻结赛程：全部种子、种子序号、两侧队伍、开球队、队内顺序及可执行文件哈希；只脱敏路径并压缩 |
| `bt.py` | 比赛时的 Bradley–Terry 拟合与自助法实现，原样 |
| `score.py` | 开源新增：由归档逐局数据复算积分，或调用原 BT 实现 |
| `reproduce.py` | 开源新增：按归档实际座位和上场源码重打一局或整场 |

在 `soccer/` 运行：

```text
python build_reference.py
python tournament/score.py final
python tournament/score.py individual --bt
python tournament/reproduce.py final --game team1-team2-s0-1 --out out/example
python tournament/reproduce.py final --all --out out/final
```

`--out` 必须是未存在的新目录，防止混入旧结果。`--all` 支持 `halftime`、`final`、`individual`，顺序逐局运行；总量 8280 局，可能耗时较长。重打使用原来每步 10 CPU 毫秒的裁判；默认 `--sandbox none`（不做身份隔离，超时、内存、子进程限制照旧），加 `--sandbox appcontainer` 可按赛时的 Windows 沙箱重打。遇启动失败停止，保留诊断回放，不把故障当平局。

`reproduce.py` 从逐局结果读取已展开的实际 `seats`，避免把种子数值的奇偶错当成种子序号。参考队两座使用同一个参考 bot。历史种子、座位、源码都能复现，但 CPU 计时受机器状态和编译器影响，不保证每次比分或产物哈希相同。

`bt.py` 中通用的“按 BT 判并列”函数未用于本期排名。本期按积分得分率、净胜球、总进球；`score.py` 不发布两项合并总分。

本期赛程、评测与启动会话集中在主办方总控 `host.py` 中；它依赖计时、封存、账号和队聊服务，不随开源发布。本目录保留原始赛程与独立的原 BT 实现，新增最小本地入口；不是把整个主办方系统迁入仓库。
