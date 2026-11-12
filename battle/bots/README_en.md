[简体中文](README.md)

# Submissions

Every file in each of the nine contestants' `submission/` folders at hand-in, byte for byte (hashes in [source_manifest.json](../source_manifest.json)). All were written by the models themselves; the organizer changed nothing.

| File | Used for |
|---|---|
| `level1.json` … `level5.json` | Deployments for levels 1–5 |
| `duel.json` | 1v1 (used on both Sakura Hill and River Isle) |
| `melee.json` | Four-player free-for-all |
| `commander.cpp` | Commander program, shared by every battle |
| `notes.md` | The contestant's own hand-in notes: time split, ideas, local test results, questions about the rules and the simulator (Chinese) |

| Folder | Contestant | Seat |
|---|---|---|
| [gpt-6.2-sol/](gpt-6.2-sol/) | GPT-6.2 Sol | `sol` |
| [claude-fable-5.2/](claude-fable-5.2/) | Claude Fable 5.2 | `fable` |
| [gemini-4-carbon/](gemini-4-carbon/) | Gemini 4 Carbon | `gemini_carbon` |
| [gemini-4-hydrogen/](gemini-4-hydrogen/) | Gemini 4 Hydrogen | `gemini_hydrogen` |
| [qwen4-max-preview/](qwen4-max-preview/) | Qwen4-Max-Preview | `qwen` |
| [deepseek-v4.1-flash/](deepseek-v4.1-flash/) | DeepSeek-V4.1-Flash | `deepseek` |
| [kimi-k3.1-pro/](kimi-k3.1-pro/) | Kimi K3.1 Pro | `kimi` |
| [doubao-seed-evolving/](doubao-seed-evolving/) | Doubao Seed-Evolving | `doubao` |
| [ernie-5.5-flash-preview/](ernie-5.5-flash-preview/) | ERNIE 5.5 Flash Preview (文心) | `ernie` |

Model, tool and effort for each are also in [roster.json](roster.json).

- Sol's `notes.md` is the snapshot taken at the deadline; Sol was still editing it then, so the text is incomplete. This affects only the notes, not the scores.
- The Claude Fable 5.2 seat uses the record of a post-contest rerun by a third party on another machine; this folder holds that run's hand-in. The first run's hand-in is archived in [first-run/claude-fable-5.2/](../first-run/claude-fable-5.2/README_en.md) and not scored. The reasons are in [CONDITIONS_en.md](../CONDITIONS_en.md#claude-fable-52-the-third-party-rerun-counts).

To run a contestant's files locally:

```text
cd workspace
python play.py level 5 ../bots/gpt-6.2-sol/level5.json --cmd ../bots/gpt-6.2-sol/commander.cpp
python play.py duel --opp ../bots/gemini-4-carbon/duel.json:../bots/gemini-4-carbon/commander.cpp
```

`play.py duel` plays `submission/duel.json` and `submission/commander.cpp` as "you"; copy a contestant's files into `workspace/submission/` to pit two contestants against each other.
