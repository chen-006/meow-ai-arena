# 炸弹人对抗

编写尽可能强的 bot，同一程序须支持2–10人。游戏规则和通信协议见 RULES.md；baseline.cpp 是可修改的公开练习对手。各场人数由实际参赛配置决定。正式赛多轮对战，使用 Bradley–Terry 模型拟合实力值，公开基准为0分线。单局分为生存分＋0.5×击杀份额，按合计分排名，同分并列。

解压起计时，争取60分钟内完成开发与测试，90分钟必须停止修改并交卷，无需启动计时器。允许本地工具、编译、模拟与复盘；禁止联网、调用其他AI、读取其他选手资料、修改裁判或计时器、干扰对手。比赛中的 bot 按标准输入输出协议独立决策，不访问文件或网络。

提交 `submission/bot.cpp`（C++20），简述策略。

## 本地使用

以下在 workspace 目录运行。Windows 使用 `run.cmd`；Linux 替换为 `sh run.sh`，并去掉可执行文件名的 `.exe`。只需要现有 Python；C++程序还需要现有编译器，无第三方Python依赖。

- 剩余时间：`run.cmd clock.py`
- 编译基准：`run.cmd engine/build.py --compile --output baseline.exe`
- 编译C++提交：`run.cmd engine/build.py --compile --source submission/bot.cpp --output bot.exe`
- 对战：`run.cmd engine/arena.py --bots Mine=bot.exe Base=baseline.exe --seed 1 --replay replays/duel.html`
- 文本复盘：`run.cmd engine/arena.py --inspect replays/duel.json --turn 20 --context 2`

多人对战增加 `Name=程序路径`，每个名字唯一，总计2–10个；可加入重复基准实例练习，但正式赛程不默认补位。带空格的路径需将整个 `Name=路径` 参数加引号。

每局自动保存 JSON 和 HTML。HTML可逐帧查看该步动作、实际落点、事件、最终得分和名次；文本复盘还展示完整棋盘与故障详情。结合具体回合改进，不必提交测试报告。

## 时间与环境

开发90分钟与回合预算不同：bot首回合2000ms，之后50ms。回合限时包含通信与系统调度。

用 `clock.py` 查看剩余时间，无需初始化。不要覆盖解压、修改计时器或调整系统时间；到时自行停止修改并交卷。

环境故障请报告组织方，不修改系统权限、防护或安装软件。编译命令只编译，不会额外启动你的程序；运行能力通过对战验证。
