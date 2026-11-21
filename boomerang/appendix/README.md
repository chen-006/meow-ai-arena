[English](README_en.md)

# 附录（不计成绩）

正式榜只有 [bots/](../bots/README.md) 里的十三位。这里是同一份选手包、同一份开赛消息下做出来、但不进正式榜的作品和成绩，视频复盘时讲到了它们。

## 作品

| 目录 | 是什么 | 席位名 | 怎么做的 |
|---|---|---|---|
| [bots/gpt-6.2-astra-grey](bots/gpt-6.2-astra-grey/) | **灰度版** GPT-6.2 Astra | `astra` | 6.2 正式发布前，11-16 用 Codex 命令行 0.167.2 经主办人指定的第三方 API 中转接入 `gpt-6.2-astra`，max，第一批开赛消息 |
| [bots/gpt-sol-grey-desktop](bots/gpt-sol-grey-desktop/) | **灰度版** Sol | `sol` | 11-16 主办人在 Codex 桌面版里手动开，max，第一批开赛消息。桌面版会话记录里的请求名是 `gpt-6.1-sol`，主办方当时认为桌面版给的是 6.2 Sol 的灰度；全员榜里写作"Sol 6.1 灰度版" |
| [bots/gpt-6-astra](bots/gpt-6-astra/) | 对照：GPT-6 Astra | `astra6` | 11-16 Codex 命令行（主办人的 ChatGPT 订阅），`gpt-6-astra`，max，第二批开赛消息 |
| [bots/gpt-6.1-sol](bots/gpt-6.1-sol/) | 对照：GPT-6.1 Sol | `sol61` | 同上，`gpt-6.1-sol` |
| [bots/gpt-6.1-terra](bots/gpt-6.1-terra/) | 对照：GPT-6.1 Terra | `terra` | 同上，`gpt-6.1-terra` |
| [bots/gpt-6-luna](bots/gpt-6-luna/) | 对照：GPT-6 Luna | `luna` | 同上，`gpt-6-luna` |
| [bots/claude-sonnet-5.5](bots/claude-sonnet-5.5/) | 对照：Claude Sonnet 5.5 | `sonnet` | 11-16 Claude Code 命令行（`claude -p`），`claude-sonnet-5-5`，max，第二批开赛消息 |
| [bots/claude-haiku-5.5](bots/claude-haiku-5.5/) | 对照：Claude Haiku 5.5 | `haiku` | 同上，`claude-haiku-5-5`；第 34 分钟停下被叫回 |
| [bots/deepseek-v4.1-flash-opencode](bots/deepseek-v4.1-flash-opencode/) | DeepSeek 的 **OpenCode 版**（作废） | `deepseek` | 11-16 第一批用 OpenCode（`deepseek/deepseek-flash`，max）；第 42 分钟停下被叫回。之后改用自家工具 dsh 重做，正式成绩用 dsh 版 |
| [bots/kimi-k3.1-pro-opencode](bots/kimi-k3.1-pro-opencode/) | Kimi 的 **OpenCode 版**（作废） | `kimi` | 11-16 第一批用 OpenCode（`moonshotai-cn/kimi-k3.1-pro`，max）；第 54 分钟因主办方的接口账号问题中断，续接。之后改用 Kimi Code 重做 |
| [early/](early/) | 60 分钟前停下或被中断那一刻留存的版本 | | 目录名是比赛时的原名（`<收卷目录>_第一次交卷_<分钟>`、`<收卷目录>_中断时_<分钟>`）；有的只留了 `player.cpp`，Haiku 那一份只有收卷记录 |

对照组六位用的都是主办人的订阅，同一时段做题（12:45～14:03），75 分钟。

## 成绩

| 目录 | 内容 |
|---|---|
| [results/灰度版海选/](results/灰度版海选/) | 11-16 用灰度版 Astra、Sol 打的一整届海选（赛程和正式完全相同，32,904 局）：`海选成绩.json` 和每局得分 `obs.json.gz`。11-16 的第一场拍卖（后来作废的演习）用的是这份成绩 |
| [results/对照赛/](results/对照赛/) | 六位对照选手加进来的 19 人赛：`对照赛成绩.json` 和新打的局 `obs_新打的.json.gz`。十三位之间的一对一沿用灰度版海选 |
| [results/全员大混战/](results/全员大混战/) | 21 位一张榜：正式十三位（Astra、Sol 是 6.2 正式版）+ 灰度两位 + 对照六位，共 103,908 局（沿用 87,660、补打 16,248），零点是正式十三位的平均。`全员榜.md/json`、两两胜率表、补打的局 `obs_补打.json.gz` |

`obs*.json.gz` 是出榜程序的输入，每局一项 `[{"mode", "map", "names"?}, {选手: 得分}]`：一对一得分是名次的相反数，四人局是名次分加击杀分。

**复现全员榜**：`python appendix/score_all21.py`（约 10 秒），把上面几份数据和正式海选的逐局结果拼起来，用正式出榜程序重算，和 `全员榜.json` 逐项核对。实测输出"核对通过"。灰度版海选、对照赛只给了数据，没写单独的核对脚本。

全员榜前列：GPT-6.2 Astra +303、Astra 灰度版 +283、Fable +128、Sol 灰度版 +95、Opus +75、Sonnet（对照）+61、GPT-6 Astra（对照）+58、Terra（对照）+51、GPT-6.1 Sol（对照）+50、Carbon +45、GPT-6.2 Sol +32。每个模型只做了一次题，同一个模型重做一次差别可以很大，这些数不说明版本之间的高下。
