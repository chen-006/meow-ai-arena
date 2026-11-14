[简体中文](README.md)

# Episode 9 · Bridge Building

Match renderer (replay → 3D video footage): [renderer/](renderer/README_en.md)

Nine models each design bridges within 90 minutes. All 7 levels are handed out at the start. Levels 1–6 are challenge levels: build a bridge so that the given vehicles reach the far bank in time and intact; once a level is passed, cheaper is better. Level 7 is attack and defense: everyone submits a final bridge and a "saboteur" vehicle, and every bridge plays every other contestant's saboteur.

Bridges are two-dimensional side-view truss structures simulated by a deterministic XPBD physics core. The local simulator the contestants had and the judge are **the same program**, so they could test as often as they liked; official scoring uses exactly the public scenarios handed out, with no hidden scenarios.

- **What's in this folder**: `workspace/` contestant package · `engine/` physics core source · `designs/` every contestant's designs · `reference/` reference solutions · `results/` results · `replays/` replays · `tournament/` scoring script
- **Reproducing the leaderboard**: Run `python build_engine.py`, then `python tournament/score.py`: it recomputes every result, checks it against `results/` and prints 核对通过 ("check passed"), in a few seconds to a minute.

## Rules at a glance

- A **design file** is JSON: new node coordinates, members (road deck, wood, steel, cable, spring, hydraulic) and mechanisms (four sensor types — pressure plate, light gate, start, ship horn — timed rules, and traffic lights). Vehicles drive only on road members and pass through all others; a member breaks above its tension / compression limit, and the compression limit of long members drops as buckling coefficient ÷ length².
- **Passing**: every vehicle in every scenario of the level arrives in time and intact. It is fine if the bridge collapses after the vehicles have crossed. Physics anomalies (tunneling, velocity explosions, NaN, rigid members deforming by more than 20%) count as failure.
- **Level score**: only for passed levels, = round(100 × reference cost ÷ your cost, 2). Matching the reference scores 100, cheaper exceeds 100; failing, not submitting or exceeding the budget scores 0.
- **Level 7**: the final bridge must be static (budget 26000); the saboteur is built from the same materials (cost ≤ 6000, mass ≤ 2500 kg, length ≤ 3 m), departs first at 0.5 s, and then 4 standard vehicles (car and van alternating) depart at 12–15 s. Each bridge plays each of the other 8 saboteurs once: 72 games. **Official scoring**: the standard convoy all arriving in time and intact = the bridge **holds**; otherwise it is **breached**, credited to the saboteur's side. Level 7 score = 50 × games held ÷ 8 + 50 × bridges breached ÷ 8.
- **Total** = (sum of level 1–6 scores + level 7 score) ÷ 7; the seven levels weigh the same. The challenge average is the mean of the six level scores.
- Full rules: [RULES.md](workspace/RULES.md) and [RULES_决赛.md](workspace/RULES_决赛.md) (Chinese originals; reference translations [RULES_en.md](workspace/RULES_en.md), [RULES_final_en.md](workspace/RULES_final_en.md)). The physics is defined by [bridge_sim.cpp](engine/bridge_sim.cpp) and [bridge.py](workspace/bridge.py). Official scoring of level 7 is as stated on this page and in [tournament/README_en.md](tournament/README_en.md).

| Level | Name | Terrain | Budget | Scenarios | Reference cost |
|---:|---|---|---:|---:|---:|
| 1 | Small ditch | 15 m ditch | 10000 | 3 | 3509.84 |
| 2 | Grand canyon | 30 m gorge, overloaded van | 26000 | 2 | 10616.73 |
| 3 | Rush hour | 28 m river with a rock in the middle, two-way traffic on the bridge at once | 18000 | 3 | 7967.26 |
| 4 | Let the ship through | 24 m river with a channel; no bridge in the channel while the ship passes | 25000 | 2 | 12342.52 |
| 5 | Not enough money | 24 m river plus an old dock pit on the left bank; no wood | 10000 | 2 | 8216.12 |
| 6 | Hell level | 36 m river, two rocks, a channel, trucks both ways | 20000 | 3 | 16709.27 |
| 7 | Attack and defense | 28 m river; final bridge × saboteur | 26000 | 72 games | — |

The reference solutions were made by the organizer beforehand; see [reference/](reference/).

## Contestants

| Contestant | Tool | Configured reasoning level |
|---|---|---|
| GPT-6.2 Sol | Codex desktop app | max |
| Claude Opus 5.5 | Claude Code | max |
| GPT-6 Astra | Codex CLI | max |
| Gemini 4 Argon | OpenCode (Google Vertex AI) | high (thinkingLevel) |
| Grok 4.7 | Grok Build | xhigh |
| Kimi K3.1 Pro | Kimi Code | max |
| Tencent Hunyuan Hy4 | OpenCode (OpenRouter, Tencent as provider) | xhigh |
| Doubao Seed Evolving | OpenCode (Volcengine Ark) | high |
| DeepSeek V4.1 Flash | dsh | max |

Every contestant used the highest reasoning level of its tool; levels with the same name in different tools are not equivalent. Tools and access channels differ, so this result is not a ranking of model ability. See [CONDITIONS_en.md](CONDITIONS_en.md).

## Overall

| Rank | Contestant | Total | Levels 1–6 avg | Level 7 | Bridge held | Saboteur breached |
|---:|---|---:|---:|---:|---:|---:|
| 1 | GPT-6.2 Sol | 102.67 | 106.24 | 81.25 | 8/8 | 5/8 |
| 2 | Claude Opus 5.5 | 79.02 | 79.69 | 75.00 | 8/8 | 4/8 |
| 3 | GPT-6 Astra | 73.48 | 71.14 | 87.50 | 8/8 | 6/8 |
| 4 | Gemini 4 Argon | 45.31 | 42.45 | 62.50 | 7/8 | 3/8 |
| 5 | Grok 4.7 | 35.62 | 37.39 | 25.00 | 4/8 | 0/8 |
| 6 | Kimi K3.1 Pro | 32.47 | 32.68 | 31.25 | 5/8 | 0/8 |
| 7 | Tencent Hunyuan Hy4 | 31.45 | 30.44 | 37.50 | 6/8 | 0/8 |
| 8 | Doubao Seed Evolving | 30.96 | 31.95 | 25.00 | 4/8 | 0/8 |
| 9 | DeepSeek V4.1 Flash | 27.93 | 28.41 | 25.00 | 4/8 | 0/8 |

"Bridge held" counts the other contestants' saboteurs your final bridge withstood; "saboteur breached" counts the other bridges your saboteur breached.

## Levels 1–6

Each cell is the level score with the cost in parentheses; "failed" means a valid design that failed some scenario, "over budget" means an invalid design.

| Contestant | Level 1 | Level 2 | Level 3 | Level 4 | Level 5 | Level 6 |
|---|---|---|---|---|---|---|
| GPT-6.2 Sol | 98.54 (3562) | **137.85** (7702) | **103.27** (7715) | **105.38** (11712) | **95.31** (8621) | **97.07** (17213) |
| Claude Opus 5.5 | **99.38** (3532) | 114.93 (9237) | 101.99 (7812) | 70.19 (17585) | 0 (over budget, 13472) | 91.64 (18234) |
| GPT-6 Astra | 91.58 (3832) | 95.99 (11061) | 88.45 (9008) | 65.94 (18718) | 0 (over budget, 10199) | 84.90 (19682) |
| Gemini 4 Argon | 93.46 (3756) | 78.02 (13608) | 83.22 (9573) | 0 (failed 0/2) | 0 (failed 0/2) | 0 (failed 0/3) |
| Grok 4.7 | 91.25 (3846) | 68.72 (15450) | 64.35 (12382) | 0 (failed 0/2) | 0 (not submitted) | 0 (not submitted) |
| Kimi K3.1 Pro | 57.87 (6065) | 57.90 (18338) | 80.30 (9922) | 0 (failed 0/2) | 0 (failed 0/2) | 0 (failed 0/3) |
| Tencent Hunyuan Hy4 | 85.33 (4113) | 49.02 (21656) | 48.27 (16505) | 0 (failed 0/2) | 0 (failed 0/2) | 0 (over budget, 34497) |
| Doubao Seed Evolving | 90.40 (3882) | 52.75 (20128) | 48.56 (16406) | 0 (failed 0/2) | 0 (failed 0/2) | 0 (failed 0/3) |
| DeepSeek V4.1 Flash | 67.01 (5238) | 52.75 (20128) | 50.72 (15707) | 0 (failed 0/2) | 0 (failed 0/2) | 0 (failed 0/3) |

Only GPT-6.2 Sol, Claude Opus 5.5 and GPT-6 Astra passed levels 4 and 6; only GPT-6.2 Sol passed level 5. The full table with costs, scenarios passed and file hashes is in [results/](results/README_en.md).

## Level 7: 72 games

Rows: bridge; columns: saboteur. ✕ = breached (the standard convoy did not all arrive), · = held.

| Bridge \ Saboteur | Sol | Opus | Astra | Gemini | Grok | Kimi | Hunyuan | Doubao | DeepSeek |
|---|---|---|---|---|---|---|---|---|---|
| GPT-6.2 Sol | — | · | · | · | · | · | · | · | · |
| Claude Opus 5.5 | · | — | · | · | · | · | · | · | · |
| GPT-6 Astra | · | · | — | · | · | · | · | · | · |
| Gemini 4 Argon | · | · | ✕ | — | · | · | · | · | · |
| Grok 4.7 | ✕ | ✕ | ✕ | ✕ | — | · | · | · | · |
| Kimi K3.1 Pro | ✕ | ✕ | ✕ | · | · | — | · | · | · |
| Tencent Hunyuan Hy4 | ✕ | · | ✕ | · | · | · | — | · | · |
| Doubao Seed Evolving | ✕ | ✕ | ✕ | ✕ | · | · | · | — | · |
| DeepSeek V4.1 Flash | ✕ | ✕ | ✕ | ✕ | · | · | · | · | — |

18 of the 72 games were breaches, and none had a physics anomaly. The arrival / failure status and time of every vehicle in every game is in [duels.json](results/duels.json).

## Watch / play

**Replays**: double-click any `.html` in [replays/](replays/README_en.md); no internet needed. One per level from the highest-scoring design, plus 3 level 7 games drawn with a fixed seed.

**Web version**: [https://arena.meowllm.top/ep9/play.html](https://arena.meowllm.top/ep9/play.html) is an in-browser bridge simulator (episode 9 page: [https://arena.meowllm.top/ep9/](https://arena.meowllm.top/ep9/)): load the nine contestants' designs and the reference solutions, build and test your own bridge, and watch any level 7 game.

**Local test runs** (Python 3.9+, g++; no third-party Python packages):

```text
python build_engine.py
cd workspace
python bridge.py check 1 ../designs/opus/level1.json
python bridge.py run 2 ../designs/sol62/level2.json
python bridge.py run 6 ../designs/sol62/level6.json levels/6/sample1.json --html replay.html
python bridge.py duel ../designs/grok/final_bridge.json ../designs/gemini/saboteur.json --html duel.html
```

On Windows, put the compiler in the repository's `tools/w64devkit/` (as for the other episodes), add it to PATH, or set `CXX`. Every `bridge.py` run / duel archives itself under `archive/` in the **current directory** (this recorded the process during the contest); add `--no-archive` to skip the archive folder, and set `BRIDGE_NO_ARCHIVE=1` to skip the simulation log as well. Program output is in Chinese.

**Reproduce the official results**:

```text
python build_engine.py
python tournament/score.py
```

This recomputes the 54 level 1–6 designs, the 6 reference solutions and the 72 level 7 games, checks everything against [results/](results/README_en.md) item by item, and prints "核对通过" (check passed). It takes seconds to a minute on an 8-core machine. See [tournament/README_en.md](tournament/README_en.md).

## Submit your own

Episode 9 submissions go **through the website, not pull requests**: the episode 9 page [https://arena.meowllm.top/ep9/](https://arena.meowllm.top/ep9/) has leaderboards for levels 1–6, sorted by cost from lowest. Build a bridge in the [simulator](https://arena.meowllm.top/ep9/play.html) (or import a design JSON); once a test run passes every public scenario you can upload it.

The design JSON format is exactly the contest's, so you can first make it pass locally with this repository's `bridge.py run`, then import and upload it on the website. To let a coding agent do it, give it the prompt in [AGENT_PROMPT.md](AGENT_PROMPT.md), which also documents the website's submission API. Level 7 has no leaderboard.

## Contents

| Path | Contents |
|---|---|
| [workspace/](workspace/README_en.md) | The contestant package as handed out: rules, simulator front end `bridge.py`, saboteur and duels `saboteur.py`, replay viewer, the 7 levels and their scenarios |
| [engine/](engine/bridge_sim.cpp) | Physics core source; `python build_engine.py` builds it into `workspace/bridge_sim.exe` |
| [prompt_en.md](prompt_en.md) | Opening message sent to the contestants |
| [designs/](designs/README_en.md) | The nine contestants' final designs: `level1.json` … `level6.json`, `final_bridge.json`, `saboteur.json` |
| [reference/](reference/) | Reference solutions for levels 1–6 (the basis of a level score of 100) |
| [results/](results/README_en.md) | Overall, per-level and 72-game tables; `scores.json`, `duels.json` |
| [replays/](replays/README_en.md) | 9 replay pages |
| [tournament/](tournament/README_en.md) | Reproduce the official results, generate replays |
| [AGENT_PROMPT.md](AGENT_PROMPT.md) | Submission prompt for coding agents (via the website) |
| [CONDITIONS_en.md](CONDITIONS_en.md) | Contest conditions |
| [source_manifest.json](source_manifest.json) | SHA-256 of the included files |

## Differences from the contest

- The contestant package (rules, `bridge.py`, `saboteur.py`, `viewer.html`, `levels/` in `workspace/`) and the contestants' designs are byte-for-byte unchanged; hashes are in the [source manifest](source_manifest.json). The English rules are added reference translations.
- During the contest the package shipped a compiled `bridge_sim.exe`. The repository does not commit executables; it provides the source and `build_engine.py` instead. Compiling with GCC 16.2.0 from w64devkit with the original flags (`-O2 -std=c++17 -static -municode`) yields an exe **byte-identical** to the contest's. Other compilers are not guaranteed to produce the same bytes; use `tournament/score.py` to check the results.
- The contestant working directory in the opening message is replaced by `{选手目录}`.
- `results/` is written by `tournament/score.py`; the numbers equal the official final results item by item. Replays were regenerated after the contest with the same engine (deterministic, identical to scoring); level 7 replays state the official scoring verdict.
- Not included: contestant sessions and reasoning, the organizer's launch / timing / collection / process-management scripts, keys and private configuration.

## Disclaimer

This is the record of one specific contest. Tools, access channels and sampling all affect the results; no assumption about model strength is made from it. The design files were generated by the models; the task, physics engine, levels, reference solutions and rules were provided by the organizer. Licensed under the repository's [MIT license](../LICENSE).
