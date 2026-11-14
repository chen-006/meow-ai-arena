[中文原文](README.md)

# Episode 3 · Capture the Flag

2–15 sides share one map. Each side's program controls a squad of 3 units, collecting neutral flags and bringing them home to score. Eleven AI models received the exact same prompt and each wrote a C++ bot in about 60 minutes, then their bots played 32010 games.

- **What's in this folder**: `workspace/` contestant package · `bots/` the 11 submitted bots · `results/` standings, pairwise win rates and per-game results · `replays/` replays · `tournament/` tournament configs · `challenge.py` one-click challenge · `appendix/` side experiments
- **Reproducing the leaderboard**: From `tournament/`, five `arena/tournament.py` commands (plan, run, rate) rebuild the schedule, replay it and rate it, in about 2 hours; see [`tournament/README_en.md`](tournament/README_en.md).
- **Rendering the game footage**: `renderer/` turns a replay into the board animation used in the video; see [`renderer/README_en.md`](renderer/README_en.md).

## Rules in brief

Full rules and protocol: [workspace/RULES_en.md](workspace/RULES_en.md).

- Square maps with walls and pillars blocking both movement and shots. Each side has 3 units, each with 100 health.
- All units act simultaneously each turn: movement, then attacks. Range 2 (Manhattan distance), 34 damage per hit; three hits take a unit out.
- There are max(2, ⌈number of sides/2⌉) flags, randomly returning to empty flag spots (one in front of each base, one at the centre). Stand on a flag and use the pickup action; bring it to your base for 1 point.
- Flag carriers cannot attack; when taken out they drop their flag. Units respawn at their own base with full health after 10 turns.
- 400 turns, rank by score. 50 ms per turn, 2 seconds on the first turn.

## Official results

Bradley–Terry ratings, baseline = 0, ten times as strong = +400. Every side count from 2–15 has equal weight.

| Rank | Model | Tool | Rating | 95% interval |
|---:|---|---|---:|---|
| 1 | GPT-6 Astra | Codex | 254 | 246 ~ 263 |
| 2 | GPT-6.1 Sol | Codex | 252 | 244 ~ 260 |
| 3 | Fable 5.1 | Claude Code | 213 | 206 ~ 222 |
| 4 | Sonnet 5.5 | Claude Code | 203 | 195 ~ 210 |
| 5 | Opus 5.5 | Claude Code | 196 | 188 ~ 204 |
| 6 | DeepSeek V4.1 Flash | dsh | 129 | 121 ~ 138 |
| — | Baseline | | 0 | |
| 7 | Qwen 3.8 Max | Qwen Code | −21 | −28 ~ −12 |
| 8 | MiMo V2.6 Pro | MiMo Code | −26 | −34 ~ −17 |
| 9 | Kimi K3 | Kimi Code | −35 | −44 ~ −25 |
| 10 | GLM 5.3 | ZCode | −92 | −100 ~ −84 |
| 11 | Doubao Seed 2.1 Turbo | trae-agent | −307 | −315 ~ −298 |

- The intervals for 1st and 2nd overlap substantially; they count as tied.
- Ratings by side count: [results/ratings_en.md](results/ratings_en.md). Pairwise win rates: `results/pairwise-winrate.csv`; pickups, deliveries, kills and deaths: `results/player-stats.csv`.
- Tool versions, channels, time used, all interruptions and incidents: [CONDITIONS_en.md](CONDITIONS_en.md).

## Watch and play

**Replays:** open any `.html` under `replays/` for turn-by-turn playback in a browser. `2p-astra-vs-sol.html` is the top two head-to-head; `15p-melee.html` is a 15-side game.

**Challenge the AIs** (Python 3.10+ and g++), from this directory:

```bash
python challenge.py your_bot.cpp           # about 200 games, around half a minute
python challenge.py your_bot.cpp --full    # about 2000 games, a few minutes, more stable results
```

With no argument, this uses `workspace/submission/bot.cpp`, the contestants' starter with full input parsing and units standing still. You can edit it directly. To enter the community leaderboard, see the [submission guide](../community/README_en.md).

**Develop** with the same local referee the contestants used ([workspace guide](workspace/README_en.md)):

```bash
cd workspace
python arena.py play submission/bot.cpp baseline --seed 7             # one game, saves replays/
python arena.py batch submission/bot.cpp baseline baseline --games 12 # multiple games, average results
python arena.py play submission/bot.cpp ../bots/gpt-6-astra.cpp        # challenge the winner
```

On Windows, replace `python` with `.\run.cmd` if the `python` command is unavailable.

## Folder

| Path | Contents |
|---|---|
| `workspace/` | Contestant package: rules, engine, local referee, replay page, starter, baseline source |
| [prompt_en.md](prompt_en.md) | Reference translation of the original prompt |
| [bots/README_en.md](bots/README_en.md) | Unmodified submissions of all 11 models |
| [results/ratings_en.md](results/ratings_en.md) | Official results; `games.jsonl.gz` has one game per line (seats, scores, pickups, kills, etc.); pairwise win rates and player statistics |
| `replays/` | 11 selected replays: a typical game for each specified pairing or side count, without selecting comebacks |
| [tournament/README_en.md](tournament/README_en.md) | Official configs and full schedule reproduction |
| `challenge.py` | One-command challenge |
| `make_player_pack.py`, `workspace_manifest.json` | Build a contest-equivalent package; original file list and hashes |
| [CONDITIONS_en.md](CONDITIONS_en.md) | Conditions and incidents |
| `appendix/` | Unranked experiments: [DeepSeek with four tools](appendix/deepseek-tool-comparison/README_en.md), [Doubao's 20 versions](appendix/doubao-history/README_en.md), [development version history](appendix/version-evolution_en.md) |

Bot code, any contestant notes, trash talk, post-mortems, logs or session records, and replay data remain in their original form and language. English copies of original task materials are reference translations; the contestants received the Chinese originals.

## Run a new model under contest conditions

`workspace/` contains the baseline source `baseline.cpp`; contestants received only the compiled program. **Do not give a model the `workspace/` folder directly.** First build the package:

```bash
python make_player_pack.py        # writes out/workspace.zip
```

The script compiles `baseline.cpp` into `baseline.exe` with the contest flags, checks other files against `workspace_manifest.json`, and packages the original file list and layout without `baseline.cpp`. The manifest comes from the same `workspace.zip` sent to all 11 contestants. `arena.py` and `core.py` were changed for the open-source release (compile source if `baseline.exe` is absent; look for the compiler in the repository's `tools/` w64devkit). The package uses these released versions; with `baseline.exe` present, game behaviour matches the contest. Then:

1. Put only `out/workspace.zip` in a new empty folder and start a fresh model session there.
2. Send the original prompt from `prompt.md`. It specifies 60 minutes, some overrun allowed, forced hand-in at 90 minutes. The package's `clock.py` starts at extraction.
3. After hand-in, run `challenge.py` on `submission/bot.cpp` to compare with the 11 models.

## Differences from the contestant package

Contestants received only the compiled Windows `baseline.exe`, without source, and were told not to decompile it. This release provides `workspace/baseline.cpp`; `arena.py` automatically compiles it if `baseline.exe` is absent. Otherwise the rules, engine and referee match the contest.
