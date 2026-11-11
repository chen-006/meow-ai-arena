[中文原文](README.md)

# Episode 1 · Territory

A classic paper.io-like territory game: leave your land to draw a trail, then return to claim the enclosed area. If anyone steps on your trail while you are out, you die (**your territory remains**). Nine AI models received the exact same prompt and each wrote a C++ bot in about 60 minutes. The same bot played both 1-vs-1 and 4-player games, for 30000 games in total.

- **What's in this folder**: `workspace/` contestant package (rules, engine, local referee, starter) · `bots/` the 9 submitted bots · `results/` standings and per-game results · `replays/` replays · `tournament/` tournament scripts · `challenge.py` one-click challenge · `appendix/` side experiments
- **Reproducing the leaderboard**: Replay every game and rate it with the two commands under the reproduction section below (`tournament/round_robin.py`, then `tournament/rate.py`). Individual scores may differ slightly; the overall ranking should match.

## Rules in brief

Full rules: [workspace/RULES_en.md](workspace/RULES_en.md); task: [workspace/TASK_en.md](workspace/TASK_en.md).

- 40×40 for 1v1, 48×48 for 4-player games; 600 turns. Everyone moves simultaneously, up/down/left/right, with no U-turns.
- Leaving your territory creates a trail. Returning converts the trail and its enclosed area into your land, including opponents' land except their protected 3×3 spawn areas.
- Hitting a wall, colliding head-on, or having anyone step on your trail kills you. Your trail disappears and you return to spawn, but your territory remains.
- After 600 turns, rank by territory size. Per-turn limits are in the full rules.

## Official results

26400 1v1 games + 3600 4-player games. Each format is scaled with greedy = 0 and the strongest model in that format = 100. The two format scores are averaged, then the highest total is rescaled to 100. **This differs from episodes 2 and 3**; see [CONDITIONS_en.md](CONDITIONS_en.md).

| Rank | Model | Tool | Total | 1v1 | 4-player |
|---:|---|---|---:|---:|---:|
| 1 | Sonnet 5.5 (see note 1 in `CONDITIONS_en.md`) | Claude Code | 100.0 | 100.0 | 100.0 |
| 2 | GPT-6 Astra | Codex | 81.7 | 72.3 | 91.0 |
| Reference | Organizer's sparring bot | — | 79.8 | 89.3 | 70.3 |
| 3 | Opus 5.5 | Claude Code | 79.4 | 81.2 | 77.5 |
| 4 | GPT-6 Sol | Codex | 39.4 | 54.0 | 24.8 |
| 5 | MiMo V2.6 Pro | ZCode | 32.4 | 50.4 | 14.4 |
| 6 | Kimi K3 | WorkBuddy | 16.7 | 22.0 | 11.5 |
| 7 | GLM 5.3 | WorkBuddy | 5.9 | 11.8 | 0.0 |
| — | greedy baseline | — | 0 | 0 | 0 |
| 8 | Spacebunny (later identified as MiniMax M3.1 Flash) | ZCode | 0.0 | 0.0 | 0.0 |
| 9 | DeepSeek V4.1 Flash | WorkBuddy | 0.0 | 0.0 | 0.0 |

- The intervals for 2nd and 3rd overlap; they count as tied.
- 8th and 9th are both below greedy and clipped to 0. In the raw ratings, Spacebunny is worse at 1v1 and DeepSeek is worse at 4-player games.
- Full intervals, score rates, average area and deaths: [results/ratings_en.md](results/ratings_en.md). Tools, time used and incidents: [CONDITIONS_en.md](CONDITIONS_en.md).

## Watch and play

**Replays:** open `replays/index.html` in a browser. The top-left menu switches between 9 games: Opus vs Sonnet, Astra vs Sonnet, MiMo vs greedy, and 6 multiplayer games.

**Challenge the AIs** (Python 3.10+ and g++), from this directory:

```bash
python challenge.py your_bot.cpp           # about 380 games, around four minutes
python challenge.py your_bot.cpp --full    # about 1100 games, a little over ten minutes
```

With no argument, this uses `workspace/starter/bot.cpp`, the contestants' starter with protocol I/O already implemented. To submit your bot to the community leaderboard, see the [submission guide](../community/README_en.md).

**Develop** with the same local referee the contestants used:

```bash
cd workspace
python engine/referee.py your_bot.cpp anchors/greedy.cpp --games 10
python engine/referee.py your_bot.cpp anchors/greedy.cpp anchors/greedy.cpp anchors/random.cpp --games 8
python engine/referee.py your_bot.cpp "../bots/sonnet-5@claude-code#1/bot.cpp" --games 10
```

Open `workspace/viewer/viewer.html` and drag in a `.json` replay.

## Folder

| Path | Contents |
|---|---|
| `workspace/` | Contestant package: rules, task, engine, local referee, random and greedy baselines, starter and replay viewer |
| [prompt_en.md](prompt_en.md) | Reference translation of the prompt sent to every model |
| [bots/README_en.md](bots/README_en.md) | Unmodified submissions of all 9 models; folder names are official entry IDs |
| `tournament/` | Round-robin and rating scripts, and the organizer's `sparring.cpp` |
| [results/ratings_en.md](results/ratings_en.md) | Results; `games.jsonl.gz` contains all 30000 games |
| `replays/` | Selected replays; open `index.html` directly |
| `challenge.py` | One-command challenge |
| [CONDITIONS_en.md](CONDITIONS_en.md) | Conditions and incidents |
| [appendix/version-evolution_en.md](appendix/version-evolution_en.md) | Strength of historical versions during development |

Bot code, any contestant notes, trash talk, post-mortems, logs or session records, and replay data remain in their original form and language. English copies of the original task materials are reference translations; the contestants received the Chinese originals.

## Reproduce the tournament

```bash
python tournament/round_robin.py --entries bots --out results/rerun.jsonl --maps 200 --melee-rounds 300 --jobs 12
python tournament/rate.py results/rerun.jsonl --entries bots --out results/rerun-ratings
```

Map seeds start at 1000; maps and pairings match the official tournament. Some bots control their search by elapsed time, so individual results need not exactly match `results/games.jsonl.gz`, though the overall ranking should agree.
