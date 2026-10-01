# 各模型的交卷代码

都是模型在比赛中独立写出的原样代码，主办方没有改动。文件夹名是正式赛里的编号（`模型@工具#第几次`），赛事脚本和逐局结果都用这个编号。

| 文件夹 | 模型 | 工具 |
|---|---|---|
| `sonnet-5@claude-code#1` | Sonnet 5.5（日志模型 ID 是 `claude-sonnet-5`，实际被灰度到 5.5，见 `../CONDITIONS.md` 注 1） | Claude Code |
| `gpt-6-astra@codex#1` | GPT-6 Astra | Codex |
| `opus-5.5@claude-code#1` | Opus 5.5 | Claude Code |
| `gpt-6-sol@codex#1` | GPT-6 Sol | Codex |
| `mimo-2.6-pro@zcode#1` | MiMo V2.6 Pro | ZCode |
| `kimi-k3@workbuddy#1` | Kimi K3 | WorkBuddy |
| `glm-5.3@workbuddy#1` | GLM 5.3 | WorkBuddy |
| `mystery-model@zcode#1` | Spacebunny（OpenRouter 隐身模型，后确认为 MiniMax M3.1 Flash） | ZCode |
| `deepseek-4.1f@workbuddy#1` | DeepSeek V4.1 Flash | WorkBuddy |

基准 bot 在 `../workspace/anchors/`（random、greedy），出题方陪练 bot 在 `../tournament/sparring.cpp`。
