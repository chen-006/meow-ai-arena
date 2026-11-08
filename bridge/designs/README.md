[English](README_en.md)

# 选手设计

每位选手一个文件夹，收录计分用的文件，逐字节原样（哈希见 [来源清单](../source_manifest.json)）：

- `level1.json`～`level6.json`：第 1～6 关的桥；
- `final_bridge.json`：第 7 关决赛桥；
- `saboteur.json`：第 7 关捣蛋车。

格式见 [RULES.md](../workspace/RULES.md) 第 2 节和 [RULES_决赛.md](../workspace/RULES_决赛.md) 第 2 节。[roster.json](roster.json) 是文件夹名、显示名和工具的对照表，`tournament/score.py` 按它的顺序读取。

| 文件夹 | 选手 | 工具 | 文件 | 备注 |
|---|---|---|---|---|
| `sol62/` | GPT-6.2 Sol | Codex 桌面版 | [L1](sol62/level1.json) [L2](sol62/level2.json) [L3](sol62/level3.json) [L4](sol62/level4.json) [L5](sol62/level5.json) [L6](sol62/level6.json) [final_bridge](sol62/final_bridge.json) [saboteur](sol62/saboteur.json) | |
| `opus/` | Claude Opus 5.5 | Claude Code | [L1](opus/level1.json) [L2](opus/level2.json) [L3](opus/level3.json) [L4](opus/level4.json) [L5](opus/level5.json) [L6](opus/level6.json) [final_bridge](opus/final_bridge.json) [saboteur](opus/saboteur.json) | |
| `astra/` | GPT-6 Astra | Codex CLI | [L1](astra/level1.json) [L2](astra/level2.json) [L3](astra/level3.json) [L4](astra/level4.json) [L5](astra/level5.json) [L6](astra/level6.json) [final_bridge](astra/final_bridge.json) [saboteur](astra/saboteur.json) | |
| `gemini/` | Gemini 4 Argon | OpenCode（Google Vertex AI） | [L1](gemini/level1.json) [L2](gemini/level2.json) [L3](gemini/level3.json) [L4](gemini/level4.json) [L5](gemini/level5.json) [L6](gemini/level6.json) [final_bridge](gemini/final_bridge.json) [saboteur](gemini/saboteur.json) | |
| `grok/` | Grok 4.7 | Grok Build | [L1](grok/level1.json) [L2](grok/level2.json) [L3](grok/level3.json) [L4](grok/level4.json) [final_bridge](grok/final_bridge.json) [saboteur](grok/saboteur.json) | 没交：level5.json、level6.json |
| `kimi/` | Kimi K3.1 Pro | Kimi Code | [L1](kimi/level1.json) [L2](kimi/level2.json) [L3](kimi/level3.json) [L4](kimi/level4.json) [L5](kimi/level5.json) [L6](kimi/level6.json) [final_bridge](kimi/final_bridge.json) [saboteur](kimi/saboteur.json) | |
| `hunyuan/` | 腾讯混元 Hy4 | OpenCode（OpenRouter，腾讯官方供应商） | [L1](hunyuan/level1.json) [L2](hunyuan/level2.json) [L3](hunyuan/level3.json) [L4](hunyuan/level4.json) [L5](hunyuan/level5.json) [L6](hunyuan/level6.json) [final_bridge](hunyuan/final_bridge.json) [saboteur](hunyuan/saboteur.json) | |
| `doubao/` | 豆包 Seed Evolving | OpenCode（火山方舟） | [L1](doubao/level1.json) [L2](doubao/level2.json) [L3](doubao/level3.json) [L4](doubao/level4.json) [L5](doubao/level5.json) [L6](doubao/level6.json) [final_bridge](doubao/final_bridge.json) [saboteur](doubao/saboteur.json) | |
| `deepseek/` | DeepSeek V4.1 Flash | dsh | [L1](deepseek/level1.json) [L2](deepseek/level2.json) [L3](deepseek/level3.json) [L4](deepseek/level4.json) [L5](deepseek/level5.json) [L6](deepseek/level6.json) [final_bridge](deepseek/final_bridge.json) [saboteur](deepseek/saboteur.json) | |

各关成绩见 [results/](../results/README.md)。在本地看某份设计：

```text
cd workspace
python bridge.py run 4 ../designs/sol62/level4.json levels/4/sample1.json --html replay.html
```

[reference/](../reference/) 里是主办方的第 1～6 关参考答案，关卡分以它的造价为 100。
