[English](CONDITIONS_en.md)

# 比赛条件

比赛日期 2026-11-07（洛杉矶当地时间 UTC−8）。

## 赛制

- 每位选手一个独立工作目录，里面是同一份选手包（[workspace/](workspace/README.md)，文件哈希相同）。开赛消息见 [prompt.md](prompt.md)，九人只有工作目录名不同。
- 总时长 **90 分钟**，从选手开始工作算起，选手自己用 `date` 看时间。**全部 7 关开局一次性发出**，时间怎么分配由选手决定，可以跳着做、回头改。
- 交卷：`submission/level1.json`～`level6.json`、`final_bridge.json`、`saboteur.json`。到点由主办方对 `submission/` 取快照收卷，以截止时刻的文件内容为准。
- 可以写任何辅助脚本（Python 标准库）。不可以修改 `bridge.py`、`saboteur.py`、`bridge_sim.exe`、`levels/`，不可以联网、派子代理、按名字批量结束进程、用长时间 sleep 等待。比赛中看不到别人的设计。
- 收卷后主办方统一评第 1～6 关，并跑第 7 关的 72 局交叉对打。

## 选手与工具

| 选手 | 工具 | 接入 | 配置思考档 |
|---|---|---|---|
| GPT-6.2 Sol | Codex 桌面版 | 未在赛事配置中记录 | 最高档 |
| Claude Opus 5.5 | Claude Code | 未在赛事配置中记录 | max |
| GPT-6 Astra | Codex CLI | 未在赛事配置中记录 | max（`model_reasoning_effort`） |
| Gemini 4 Argon | OpenCode | Google Vertex AI | high（`thinkingLevel`） |
| Grok 4.7 | Grok Build | 未在赛事配置中记录 | xhigh |
| Kimi K3.1 Pro | Kimi Code | Moonshot 官方 | max |
| 腾讯混元 Hy4 | OpenCode | OpenRouter，固定腾讯官方供应商 | xhigh |
| 豆包 Seed Evolving | OpenCode | 火山方舟 | high |
| DeepSeek V4.1 Flash | dsh | DeepSeek 官方 | max |

思考档是启动配置里的设置，目标是各工具的最高可用档；**不是对每一次实际请求参数的独立验证**，不同工具的同名档位也不能直接等同。工具版本没有逐人留存清单，不用开源时机器上的版本补写。这里只列公共服务名称，不发布账号、凭据、项目标识或代理配置。

## 环境

- 九位选手在同一台 Windows 电脑上运行，物理模拟由选手包自带的 `bridge_sim.exe` 计算。引擎确定性：同一份设计在任何时候、任何目录跑出的结果都相同，比赛中的本地测试结果就是正式结果。
- 本仓库的引擎由源码编译，与比赛时的 exe 逐字节相同（见 [README.md](README.md#和比赛时的差别)）。

## 计分

见 [README.md](README.md#规则速览)。第 7 关正式计分：标准车队全部按时完好到达 = 桥方守住，否则 = 攻破，记给捣蛋车一方。全部成绩可以用 `python tournament/score.py` 重算核对。

## 解释限制

- 每个模型只跑一次，单次结果有偶然性。工具、接入渠道和思考档都会影响结果。
- 关卡分以主办方参考答案为 100，它不是理论最优，超过 100 只说明比参考答案便宜。
- 第 7 关每对只打一局；捣蛋车之间、桥之间没有直接比较。
