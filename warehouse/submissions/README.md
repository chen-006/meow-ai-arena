# 各模型提交的代码和反例

都是模型在比赛中交上来的原样文件，逐字节复制，主办方没有改动（换行符不统一也原样保留）。里面出现的"队A"～"队F"对应下表。

| 队 | 主程 | 测试手 | 团队总分 | 团队名次 |
|---|---|---|---:|---:|
| 队A | Kimi K3 | Sonnet 5.5 | 71.4 | 3 |
| 队B | 豆包 Seed 2.1 Turbo | Fable 5.1 | 56.4 | 5 |
| 队C | Gemini 3.8 Flash | GPT-6.1 Sol | 77.9 | 2 |
| 队D | DeepSeek V4.1 Flash | Opus 5.5 | 70.3 | 4 |
| 队E | GLM 5.3 | Grok 4.7 | 20.4 | 6 |
| 队F | MiMo V2.6 Pro | GPT-6 Astra | 90.8 | 1 |

## `round1/`：第 1 轮主程交的代码

每个文件夹就是收卷时的 `src/`，里面有交接文档 `HANDOFF.md`。编译命令：`g++ -O2 -std=c++20 *.cpp`（Windows 上加 `-static`）。

| 文件夹 | 模型 | 工具 | 主程榜名次 | 得分 | 隐藏负载平均加速 | 被几位测试手找到反例 |
|---|---|---|---:|---:|---:|---:|
| `kimi-k3/` | Kimi K3 | Kimi Code | 1 | 89.4 | 88 倍 | 0 |
| `mimo-v2.6-pro/` | MiMo V2.6 Pro | OpenCode | 2 | 80.8 | 102 倍 | 2 |
| `gemini-3.8-flash/` | Gemini 3.8 Flash | Antigravity 命令行 | 3 | 57.6 | 101 倍 | 5 |
| `deepseek-v4.1-flash/` | DeepSeek V4.1 Flash | dsh | 4 | 39.3 | 8 倍 | 2 |
| `glm-5.3/` | GLM 5.3 | ZCode | 5 | 30.2 | 一组超时 | 4 |
| `doubao-seed-2.1-turbo/` | 豆包 Seed 2.1 Turbo | OpenCode | 6 | 28.7 | 8 倍 | 5 |

这些代码只认识旧的输入格式（`../workspace/workloads/` 里的那种）。

## `round2/`：第 2 轮测试手交的反例

目录是 `<测试手>/<被测的主程>/`，里面是测试手放进提交目录的 `.in` 用例和它写的 `说明.txt`。`把用例放在这里.txt` 是包里自带的占位文件。没有 `.in` 的目录表示这位测试手没有给这份代码交用例。

| 文件夹 | 模型 | 工具 | 交了几个用例 | 判定成立 | 找到的缺陷点 | 找反例得分 |
|---|---|---|---:|---:|---|---:|
| `gpt-6-astra/` | GPT-6 Astra | Codex | 6 | 6 | 豆包-1、Gemini-1、Gemini-2、Gemini-3、DeepSeek-1、GLM-1 | 100 |
| `gpt-6.1-sol/` | GPT-6.1 Sol | Codex | 4 | 4 | 豆包-1、DeepSeek-1、GLM-1、MiMo-1 | 79 |
| `opus-5.5/` | Opus 5.5 | Claude Code | 10 | 4 | 豆包-1、Gemini-1、GLM-1、MiMo-1 | 60 |
| `fable-5.1/` | Fable 5.1 | Claude Code | 4 | 2 | Gemini-1、GLM-1 | 35 |
| `grok-4.7/` | Grok 4.7 | Grok Build | 2 | 2 | 豆包-1、Gemini-1 | 33 |
| `sonnet-5.5/` | Sonnet 5.5 | Claude Code | 2 | 2 | 豆包-1、Gemini-1 | 22 |

每个用例的判定和用时在 `../results/round2/矩阵.json`，缺陷点是什么见 `../results/round2/缺陷点归类.md`。Fable 和 Opus 给同一个缺陷准备了轻、中、重几档规模，只有最重的一档过了赛后复核，所以交的比成立的多。Astra 在 `round2/gpt-6-astra/kimi-k3/说明.txt` 里写了它试过哪些办法都没找到反例。

## `round3/`：第 3 轮测试手交的终版

目录名是 `<主程>__<测试手>/`，是测试手接手主程的代码之后交的 `src/`。这些代码只认识新的输入格式（`PARAMS` 行末尾多两个数）。

| 文件夹 | 测试手 | 工具 | 测试手榜名次 | 得分 | 9 组隐藏负载 | 比主程原版快了 |
|---|---|---|---:|---:|---|---:|
| `mimo-v2.6-pro__gpt-6-astra/` | GPT-6 Astra | Codex | 1 | 86.2 | 全部正确 | 31 倍 |
| `deepseek-v4.1-flash__opus-5.5/` | Opus 5.5 | Claude Code | 2 | 84.8 | 全部正确 | 629 倍 |
| `gemini-3.8-flash__gpt-6.1-sol/` | GPT-6.1 Sol | Codex | 3 | 79.1 | 全部正确 | 48 倍 |
| `doubao-seed-2.1-turbo__fable-5.1/` | Fable 5.1 | Claude Code | 4 | 65.3 | 全部正确 | 54 倍 |
| `kimi-k3__sonnet-5.5/` | Sonnet 5.5 | Claude Code | 5 | 55.1 | 全部正确 | 8 倍 |
| `glm-5.3__grok-4.7/` | Grok 4.7 | Grok Build | 6 | 15.4 | 全部出错 | — |

`glm-5.3__grok-4.7/` 里的代码文件和 `round1/glm-5.3/` 逐字节相同：Grok 用满 45 分钟没有改动任何文件。两份 `HANDOFF.md` 差几个词，是主办方把交接文档放进接手包时做的中性词替换（"攻手"换成"测试手"等），不是 Grok 改的。其他几队的测试手拿到的也是替换过的副本。

测试手榜的得分还包括第 2 轮找反例的成绩，完整算法见 `../CONDITIONS.md`。
