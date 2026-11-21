[简体中文](README.md)

# Contestant programs (the thirteen in the official qualifier)

Each folder holds a contestant's files exactly as collected:

- `player.cpp`: the submitted program (a single C++17 file that includes only `"dart.hpp"` and the standard library).
- `notes.md`: the contestant's own write-up: how it plays, strengths and weaknesses, what it tried, local test results, questions about the rules. The kickoff message asked for detail, since the notes would be used to introduce the programs afterwards. Chinese.
- `收卷记录.json`: the collection record: time, reason (time up / process exited), SHA-256 and size of both files.

| Folder | Contestant | Seat | Rank | Program size |
|---|---|---|---:|---:|
| [gpt-6.2-astra](gpt-6.2-astra/) | GPT-6.2 Astra | `astra` | 1 | 80 KB |
| [claude-fable-5.2](claude-fable-5.2/) | Claude Fable 5.2 | `fable` | 2 | 39 KB |
| [claude-opus-5.5](claude-opus-5.5/) | Claude Opus 5.5 | `opus` | 3 | 60 KB |
| [gemini-4-carbon](gemini-4-carbon/) | Gemini 4 Carbon | `carbon` | 4 | 98 KB |
| [gpt-6.2-sol](gpt-6.2-sol/) | GPT-6.2 Sol | `sol` | 5 | 92 KB |
| [gemini-4-hydrogen](gemini-4-hydrogen/) | Gemini 4 Hydrogen | `hydrogen` | 6 | 82 KB |
| [grok-4.7](grok-4.7/) | Grok 4.7 | `grok` | 7 | 57 KB |
| [glm-5.3](glm-5.3/) | GLM 5.3 | `glm` | 8 | 39 KB |
| [deepseek-v4.1-flash](deepseek-v4.1-flash/) | DeepSeek-V4.1-Flash (dsh) | `deepseek` | 9 | 25 KB |
| [kimi-k3.1-pro](kimi-k3.1-pro/) | Kimi K3.1 Pro (Kimi Code) | `kimi` | 10 | 15 KB |
| [qwen4-max](qwen4-max/) | Qwen4-Max | `qwen` | 11 | 40 KB |
| [mimo-v2.6-pro](mimo-v2.6-pro/) | MiMo V2.6 Pro | `mimo` | 12 | 32 KB |
| [doubao-seed-evolving](doubao-seed-evolving/) | Doubao Seed-Evolving | `doubao` | 13 | 33 KB |

[roster.json](roster.json) maps seat names, the submission folder names used at the event and the folder names here (appendix entries included).

To play any of them against another: in `workspace/`, run `python match.py duel ../bots/gpt-6.2-astra/player.cpp ../bots/claude-fable-5.2/player.cpp --games 40` (first run `python build_engine.py` to build the referee and sparring bots). To challenge all thirteen with your own program, see `challenge.py` in [../README_en.md](../README_en.md).

The programs and notes were written by the models and not modified by the organizer. The rest of each contestant's working folder (self-play replays, build caches, tuning scripts and logs) is not included, and neither are session logs. Analyses of every program are in [../analysis/](../analysis/README_en.md).
