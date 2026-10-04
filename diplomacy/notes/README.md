# 模型笔记

各模型在自己工作目录里留下的文件，三局结束后逐字节原样复制（只去掉了裁判发给它们的规则、客户端、地图资料、连接信息和 OpenCode 配置）。三局共用同一个工作目录，所以一个文件夹里混着三局的文件。

| 文件夹 | 模型 | 工作目录名（= 第 1 局座位） | 主要内容 |
|---|---|---|---|
| `fable-5.1/` | Fable 5.1 | ENGLAND | 三局决策笔记：`笔记.md`（第 1 局英国）、`笔记_第二局_俄国.md`、`笔记_第三局_意大利.md`；推演用的假设命令和脚本 |
| `opus-5.5/` | Opus 5.5 | RUSSIA | `notes.md`（第 1 局俄国）、`notes_game2_england.md`、`notes_game3_austria.md`；推演脚本 |
| `gpt-6.1-sol/` | GPT-6.1 Sol | TURKEY | `战略笔记.md`（第 1 局土耳其）、`GERMANY_第二局.md`、`FRANCE_第三局.md`；上百份推演用的假设命令 |
| `gpt-6-astra/` | GPT-6 Astra | ITALY | 只有推演用的假设命令，没有文字笔记 |
| `deepseek-v4-pro/` | DeepSeek V4 Pro | GERMANY | 只有推演用的假设命令 |
| — | Sonnet 5.5 | AUSTRIA | 工作目录里没有留下任何文件 |
| — | DeepSeek V4.1 Flash | FRANCE | 工作目录里没有留下任何文件 |

假设命令文件（`.json`）的格式见 `../workspace/README.md`，用 `python game.py simulate 文件` 推演，不影响真实对局。

Claude 系三个模型还有 Claude Code 自带的自动记忆，写在工作目录之外，没有收进来。
