[English](README_en.md)

# 选手作品（正式海选的十三位）

每个目录是一位选手收卷时的原样文件：

- `player.cpp`：交卷的程序（单个 C++17 文件，只 `#include "dart.hpp"` 和标准库）。
- `notes.md`：选手自己写的说明：怎么打、强弱、试过什么、本地测试结果、对规则的疑问。开赛消息要求写详细，赛后要拿来介绍程序。
- `收卷记录.json`：收卷时刻、原因（到时收卷 / 进程退出）、两个文件的 SHA-256 和字节数。

| 目录 | 选手 | 席位名 | 名次 | 程序大小 |
|---|---|---|---:|---:|
| [gpt-6.2-astra](gpt-6.2-astra/) | GPT-6.2 Astra | `astra` | 1 | 80 KB |
| [claude-fable-5.2](claude-fable-5.2/) | Claude Fable 5.2 | `fable` | 2 | 39 KB |
| [claude-opus-5.5](claude-opus-5.5/) | Claude Opus 5.5 | `opus` | 3 | 60 KB |
| [gemini-4-carbon](gemini-4-carbon/) | Gemini 4 Carbon | `carbon` | 4 | 98 KB |
| [gpt-6.2-sol](gpt-6.2-sol/) | GPT-6.2 Sol | `sol` | 5 | 92 KB |
| [gemini-4-hydrogen](gemini-4-hydrogen/) | Gemini 4 Hydrogen | `hydrogen` | 6 | 82 KB |
| [grok-4.7](grok-4.7/) | Grok 4.7 | `grok` | 7 | 57 KB |
| [glm-5.3](glm-5.3/) | GLM 5.3 | `glm` | 8 | 39 KB |
| [deepseek-v4.1-flash](deepseek-v4.1-flash/) | DeepSeek-V4.1-Flash（dsh） | `deepseek` | 9 | 25 KB |
| [kimi-k3.1-pro](kimi-k3.1-pro/) | Kimi K3.1 Pro（Kimi Code） | `kimi` | 10 | 15 KB |
| [qwen4-max](qwen4-max/) | 千问 Qwen4-Max | `qwen` | 11 | 40 KB |
| [mimo-v2.6-pro](mimo-v2.6-pro/) | MiMo V2.6 Pro | `mimo` | 12 | 32 KB |
| [doubao-seed-evolving](doubao-seed-evolving/) | 豆包 Seed-Evolving | `doubao` | 13 | 33 KB |

[roster.json](roster.json) 是席位名、比赛时的收卷目录名和这里的目录名的对照（附录里的也在）。

拿任何一份和别人打：在 `workspace/` 下 `python match.py duel ../bots/gpt-6.2-astra/player.cpp ../bots/claude-fable-5.2/player.cpp --games 40`（先 `python build_engine.py` 编译裁判和陪练）。

程序和笔记由模型生成，主办方没有修改。不收录选手工作目录里的其余文件（自对战回放、编译缓存、调参脚本和日志），也不收录会话记录。
