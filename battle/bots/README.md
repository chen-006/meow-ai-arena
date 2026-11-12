[English](README_en.md)

# 选手作品

九位选手收卷时 `submission/` 里的全部文件，逐字节原样（哈希见 [source_manifest.json](../source_manifest.json)）。都由模型自己写，主办方没有改动。

| 文件 | 用在 |
|---|---|
| `level1.json` … `level5.json` | 第 1～5 关的布阵 |
| `duel.json` | 1v1（樱丘、河心洲两张图都用它） |
| `melee.json` | 四人混战 |
| `commander.cpp` | 指挥程序，全部场次共用 |
| `notes.md` | 选手自己写的交卷说明：时间分配、思路、本地测试结果、对规则和模拟器的疑问 |

| 目录 | 选手 | 席位名 |
|---|---|---|
| [gpt-6.2-sol/](gpt-6.2-sol/) | GPT-6.2 Sol | `sol` |
| [claude-fable-5.2/](claude-fable-5.2/) | Claude Fable 5.2 | `fable` |
| [gemini-4-carbon/](gemini-4-carbon/) | Gemini 4 Carbon | `gemini_carbon` |
| [gemini-4-hydrogen/](gemini-4-hydrogen/) | Gemini 4 Hydrogen | `gemini_hydrogen` |
| [qwen4-max-preview/](qwen4-max-preview/) | Qwen4-Max-Preview | `qwen` |
| [deepseek-v4.1-flash/](deepseek-v4.1-flash/) | DeepSeek-V4.1-Flash | `deepseek` |
| [kimi-k3.1-pro/](kimi-k3.1-pro/) | Kimi K3.1 Pro | `kimi` |
| [doubao-seed-evolving/](doubao-seed-evolving/) | Doubao Seed-Evolving | `doubao` |
| [ernie-5.5-flash-preview/](ernie-5.5-flash-preview/) | 文心 ERNIE 5.5 Flash Preview | `ernie` |

型号、工具、思考档的对照也在 [roster.json](roster.json)。

- Sol 的 `notes.md` 是到点收卷那一刻的快照，选手当时还在写，内容不完整；只影响说明文字，不影响成绩。
- Claude Fable 5.2 这一席采用的是赛后第三方在另一台机器上重跑的记录，这里是那次交回的作答；首次运行的作答存档在 [first-run/claude-fable-5.2/](../first-run/claude-fable-5.2/README.md)，不计成绩。原因见 [CONDITIONS.md](../CONDITIONS.md#claude-fable-52-这一席采用第三方重跑)。

在本地拿某位选手的作品跑：

```text
cd workspace
python play.py level 5 ../bots/gpt-6.2-sol/level5.json --cmd ../bots/gpt-6.2-sol/commander.cpp
python play.py duel --opp ../bots/gemini-4-carbon/duel.json:../bots/gemini-4-carbon/commander.cpp
```

`play.py duel` 默认用 `submission/duel.json` 和 `submission/commander.cpp` 当自己；把某位选手的文件复制进 `workspace/submission/` 就能让两位选手互打。
