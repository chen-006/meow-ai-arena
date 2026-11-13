---
name: 第 10 集纸上谈兵：第三方复测 / Ep10 Battle Sim rerun
about: 按 battle/CONDITIONS.md 的条件让某个模型重跑一次，提交结果 / Report a rerun under the conditions in battle/CONDITIONS.md
title: "[battle 复测] <型号> / <工具>"
labels: ""
assignees: ""
---

<!--
条件见 battle/CONDITIONS.md 的"欢迎第三方复测"一节：干净的 workspace/、开赛消息原样发、限时 90 分钟、中途不插话。
Conditions: see "Third-party retests welcome" in battle/CONDITIONS_en.md — a clean workspace/, the opening message sent verbatim, 90 minutes, no interjections.
请不要贴密钥、令牌、账号或本机路径里的个人信息。 / Do not paste keys, tokens, account names or personal paths.
-->

## 运行条件 / Setup

- 型号 / Model：
- 工具和版本 / Tool and version：
- 思考档 / Reasoning effort：
- 接入渠道 / Provider：
- 运行方式（交互式 / 无界面）/ Interactive or headless：
- 操作系统、命令行 / OS and shell：
- 用时（分钟）/ Time used (minutes)：
- 结束方式（主动交卷 / 到时收卷）/ Ended by (model submitted / time ran out)：
- 有没有中断、插话、子代理、联网 / Any interruptions, interjections, subagents, network access：

## 五关结果 / Levels

<!-- 在工作目录里运行 `python play.py check` 和 `python play.py levels`，把输出贴在下面。 / Paste the output of `python play.py check` and `python play.py levels`. -->

```text

```

## 对抗（可选）/ Duel and melee (optional)

<!-- 例如 `python play.py duel --opp ../bots/<选手>/duel.json:../bots/<选手>/commander.cpp` 的输出。 -->

```text

```

## 交卷文件 / Submission

<!-- 把 submission/ 打成 zip 拖进来：level1.json～level5.json、duel.json、melee.json、commander.cpp、notes.md。 / Attach submission/ as a zip. -->

- [ ] 已附上 `submission/` 的全部文件 / All files in `submission/` attached
- [ ] 选手包没有改过（`war.exe` 由 `python build_engine.py` 编出，或来自本仓库）/ The player kit was not modified
- [ ] 同意这些文件按本仓库的 MIT 许可公开 / I agree to publish these files under this repository's MIT license
