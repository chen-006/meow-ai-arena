[中文原文](README.md)

# Episode 2 · Bomberman

2–10 players share a Bomberman board: drop bombs to destroy crates and opponents, avoid the closing walls, and survive longer to score more. Nine AI models received the exact same prompt and each wrote a C++ bot in about 60 minutes. Their bots then played every game size from 2 to 10 players, for 8930 games in total.

- **What's in this folder**: `workspace/` contestant package (rules, engine and local referee, replay player, baseline) · `bots/` the 9 submitted bots · `results/` standings and per-game results · `replays/` replays · `tournament/` evaluation and rating scripts, frozen schedule · `challenge.py` one-click challenge · `appendix/` side experiments
- **Reproducing the leaderboard**: `python tournament/run.py --formal --output rerun/对战` replays all 8,930 games on the frozen schedule (a few hours); see [`tournament/README_en.md`](tournament/README_en.md).

## Rules in brief

Full rules and protocol: [workspace/RULES_en.md](workspace/RULES_en.md).

- Square maps with permanent walls and destructible crates. Everyone acts simultaneously: up/down/left/right or wait, optionally also dropping a bomb at their current position. At most 2 active bombs per player.
- Bombs explode after 4 turns, with a cross-shaped blast extending 3 cells. Walls block it; crates are destroyed and stop it; other bombs trigger chain explosions. Flames eliminate players immediately. No health or respawn.
- After a set number of turns the outer rings turn into walls. Players standing on newly closed cells are immediately eliminated.
- Game score = survival score + 0.5 × kill credit; rank by that score. 50 ms per turn, 2 seconds on the first turn.

## Official results

8930 games; each player count from 2–10 has equal weight. Ratings use Bradley–Terry, baseline = 0, scale = 400 × log10(strength ratio), with no ceiling.

| Rank | Model | Tool | Rating |
|---:|---|---|---:|
| 1 | GPT-6 Astra | Codex | 488.6 |
| 2 | Opus 5.5 | Claude Code | 321.8 |
| 3 | GPT-6 Sol | Codex | 301.9 |
| 4 | DeepSeek V4.1 Flash | ZCode | 270.7 |
| 5 | Kimi K3 | ZCode | 230.4 |
| 6 | Qwen 3.8 Max | ZCode | 226.5 |
| 7 | GLM 5.3 | ZCode | 175.0 |
| 8 | Doubao Seed 2.1 | ZCode | 95.4 |
| 9 | MiMo V2.6 Pro | ZCode | 39.4 |
| — | Baseline | — | 0 |

- This episode's baseline is weak; all 9 models finished above it.
- Per-player-count survival score, kill score, first-place rates and faults: [results/summary_en.md](results/summary_en.md). The original 2600-game tournament (2, 5 and 10 players only) had exactly the same ranking; see [the comparison](results/vs-original-2600_en.md).
- Tools, reasoning effort, time used and interruptions: [CONDITIONS_en.md](CONDITIONS_en.md).

## Watch and play

**Replays:** open any `.html` under `replays/`, such as `10p-melee.html` (10 players) or `2p-astra-vs-opus.html` (the top two). [Replay list and selection method](replays/README_en.md).

**Challenge the AIs** (Python 3.10+ and g++), from this directory:

```bash
python challenge.py your_bot.cpp           # 2- and 10-player games, 330 games, about 4 minutes
python challenge.py your_bot.cpp --full    # adds 3-player games, 1320 games, about 15 minutes
```

With no argument, the public `workspace/baseline.cpp` serves as your bot for a demonstration. An HTML replay of every game is saved in the results directory. To enter the community leaderboard, see the [submission guide](../community/README_en.md).

**Develop** with the same local referee the contestants used ([workspace guide](workspace/README_en.md)):

```bash
cd workspace
# Save your code as submission/bot.cpp; baseline.cpp can be your starting point.
python engine/build.py --compile --source baseline.cpp --output baseline.exe
python engine/build.py --compile --source submission/bot.cpp --output bot.exe
python engine/arena.py --bots Mine=bot.exe Base=baseline.exe --seed 1 --replay replays/duel.html
```

On Windows, use `run.cmd` if `python` is unavailable. On Linux use `sh run.sh` and omit `.exe`.

## Folder

| Path | Contents |
|---|---|
| `workspace/` | Contestant package: rules, engine, local referee, replay viewer, public baseline and empty submission folder |
| [prompt_en.md](prompt_en.md) | Reference translation of the original prompt |
| [bots/README_en.md](bots/README_en.md) | Sealed, unmodified code of all 9 models, with hashes |
| [results/summary_en.md](results/summary_en.md) | Official results; `games.json.gz` contains all 8930 games |
| [replays/README_en.md](replays/README_en.md) | 8 selected replays |
| [tournament/README_en.md](tournament/README_en.md) | Evaluation and rating scripts, frozen schedule and reproduction instructions |
| `challenge.py` | One-command challenge |
| [CONDITIONS_en.md](CONDITIONS_en.md) | Conditions and incidents |
| `appendix/` | [Original 2600-game results](appendix/original-2600/summary_en.md) and [version history](appendix/version-evolution_en.md) |

Bot code, any contestant notes, trash talk, post-mortems, logs or session records, and replay data remain in their original form and language. English copies of original task materials are reference translations; the contestants received the Chinese originals.
