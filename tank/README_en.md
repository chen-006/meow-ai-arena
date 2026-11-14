[简体中文](README.md)

# Episode 6 · Tank Battle

Back to bot battles. Fourteen models each wrote a C++ tank-battle bot for 1-vs-1 round-robin play. This episode has two halves: each model writes a first version in the first half, all first versions play each other, and at half time every model receives the standings, replays of all its own games, and executables of every other model's first version. It then has a second half to improve. Only the second-half version counts for the final ranking. The three tournaments add up to 27,840 games.

English guides: [competition conditions](CONDITIONS_en.md), [original prompts (reference translation)](prompt_en.md), [full rules](workspace/RULES_en.md), [workspace guide](workspace/README_en.md), [bot index](bots/README_en.md), [results](results/README_en.md), [tournament reproduction](tournament/README_en.md), and [notes index](notes/README_en.md).

Contestant source code, pre-match boasts, post-match reviews, logs, session records, and replay data remain in their original form and language. The Chinese task materials are preserved unchanged; their English versions are reference translations.

- **What's in this folder**: `workspace/` contestant package · `bots/half1/`, `bots/half2/` first- and second-half bots · `results/` half-time, final and progress tournaments with per-game results · `replays/` replays · `tournament/` tournament platform and the three configs · `notes/` trash talk and post-match reviews · `challenge.py` one-click challenge
- **Reproducing the leaderboard**: From `tournament/`, run `python tournament.py plan final.json`, then `run run-final` and `rate run-final` (about 70 minutes for the final); see [`tournament/README_en.md`](tournament/README_en.md).
- **Rendering the game footage**: `renderer/` turns a replay into the board animation used in the video; see [`renderer/README_en.md`](renderer/README_en.md).

## Rules in brief (full rules: [English reference](workspace/RULES_en.md), exact implementation: `workspace/tank.hpp`)

- 15×15 map with empty cells, brick, steel and water. Each side has a base (24 HP) and three tanks: fast (1 HP, moves 2 cells per turn), fire (1 HP, no reload), armor (2 HP).
- Both sides give one command per tank simultaneously each turn (stay, move, fire); bullets travel 2 cells per turn. Friendly fire exists, including on your own base; a base loses at most 1 HP per turn.
- Destroyed tanks respawn after 3 turns; an enemy standing on the spawn point is destroyed. Stars appear on supply points at turns 10, 30, 50, …; each star is a level-up (more HP, no reload or a second bullet, level-2 bullets break steel).
- Destroy the enemy base to win; after 300 turns, compare base HP, then kills, then stars. No randomness, no hidden information.
- Contestants implement only `init` and `decide`; the engine, including an exact one-turn `tank::step`, is the header `tank.hpp`. Thinking budget is the bot's own CPU time: 2000 ms for the first turn, 50 ms per turn after that; 3 consecutive overruns or illegal commands lose the game.

## Format

| Stage | What happens |
|---|---|
| First half | Each model gets `workspace.zip` (this folder's `workspace/`; the baseline was given only as an executable) and the same prompt (`prompt.md`). The clock starts when the zip is extracted: 30 minutes, earliest submission at minute 20, forced collection at minute 45 |
| Half-time tournament | 14 first-half bots + baseline + the organizer's reference bot (`sparring`, unranked): every pair plays both seats on 42 maps, 10,080 games |
| Half-time package | Standings, the model's own 1,260 replays, executables of the other 13 first-half bots and the baseline (no source; the reference bot is not included) |
| Second half | Starts when the package is extracted; same time rules |
| Final tournament | 14 second-half bots + baseline + reference bot, 60 new maps, 14,400 games; decides the ranking |
| Progress tournament | Each second-half bot vs. the pool of all first-half bots + baseline, 8 new maps, 3,360 games; measures improvement only |

Ratings are Bradley–Terry (win 1, draw 0.5), baseline = 0, 10× stronger = +400, 95% intervals by resampling whole maps 1,000 times. Neighbours whose rating-difference interval contains 0 are marked as tied.

## Contestants

| Model | Short name | Tool (version) | Channel | Reasoning effort | Machine |
|---|---|---|---|---|---|
| GPT-6 Astra | Astra | Codex desktop 0.162.0-alpha.17.2 | OpenAI subscription (official client) | medium | contest PC |
| GPT-6.1 Sol | Sol | Codex desktop 0.162.0-alpha.17.2 | OpenAI subscription (official client) | medium | contest PC |
| GPT-6.1 Sol (Ultrafast) | SolUF | Codex CLI 0.162.0-alpha.17.2 | OpenAI API, ultrafast tier | medium | external |
| Gemini 4 Argon Contributor | Gemini | OpenCode 2.0.29 | OpenCode Go | tool default | external |
| Claude Opus 5.5 | Opus | Claude Code 2.1.296 | Anthropic subscription (official client) | medium | contest PC |
| Claude Haiku 5.5 | Haiku | Claude Code 2.1.296 | Anthropic subscription (official client) | medium | contest PC |
| Muse Wonder Full | Muse | Muse Code CLI 1.4.7 | Meta | high (tool default) | external |
| Xiaomi MiMo V2.6 Pro | MiMo | OpenCode 2.0.19 | Cline pass | tool default | contest PC |
| Grok 4.7 | Grok | Grok Build 1.0.46 | account pool (address not disclosed) | tool default | contest PC |
| DeepSeek V4.1 Flash | DeepSeek | dsh (version not recorded) | Cline pass | high | contest PC |
| Doubao-Seed-Evolving | Doubao | OpenCode 2.0.19 | Volcano Engine | tool default | contest PC |
| Kimi K3 | Kimi | Kimi Code CLI 2.1.1 | Cline pass | high | contest PC |
| Qwen 3.8 Max | Qwen | Qwen Code CLI 0.24.7 | Cline pass | tool default (not set) | contest PC |
| GLM 5.3 | GLM | ZCode desktop 0.16.9 | Cline pass | max | contest PC |

Reasoning effort was not unified; each tool's default was used. SolUF, Gemini and Muse were run by outside operators on a faster machine (AMD Ryzen 7 9800X3D vs. the contest PC's Intel Core i9-13900HX); the organizer could not time them, so their durations are the operators' records. All bots were evaluated on the contest PC.

## Final results (final tournament, second-half versions)

| Rank | Model | Rating | 95% CI | W-D-L | Score | Half-time rating |
|---:|---|---:|---|---|---:|---:|
| 1 | GPT-6 Astra | 507 | 468 ~ 550 | 1556-4-240 | 86.6% | 344 |
| — | reference bot (sparring) | 489 | 460 ~ 521 | 1534-4-262 | 85.3% | 558 |
| 2 | GPT-6.1 Sol (Ultrafast) | 467 | 432 ~ 503 | 1509-0-291 | 83.8% | 486 |
| 3 | Gemini 4 Argon Contributor | 355 | 325 ~ 389 | 1351-3-446 | 75.1% | 232 |
| 4 | GPT-6.1 Sol | 319 | 286 ~ 353 | 1295-1-504 | 72.0% | 274 |
| 5 | Claude Opus 5.5 | 259 | 224 ~ 295 | 1196-2-602 | 66.5% | 154 |
| 6 (6–10 tied) | Muse Wonder Full | 55 | 22 ~ 88 | 823-6-971 | 45.9% | 74 |
| 7 (6–10 tied) | Xiaomi MiMo V2.6 Pro | 29 | 2 ~ 59 | 773-4-1023 | 43.1% | 39 |
| 8 (6–10 tied) | Grok 4.7 | 26 | −3 ~ 55 | 768-1-1031 | 42.7% | −8 |
| — | baseline | 0 | — | 718-3-1079 | 40.0% | 0 |
| 9 (6–10 tied) | DeepSeek V4.1 Flash | −6 | −37 ~ 28 | 706-3-1091 | 39.3% | −60 |
| 10 (6–10 tied) | Doubao-Seed-Evolving | −31 | −64 ~ 0 | 655-12-1133 | 36.7% | −470 |
| 11 (11–12 tied) | Kimi K3 | −129 | −162 ~ −97 | 466-30-1304 | 26.7% | −126 |
| 12 (11–12 tied) | Claude Haiku 5.5 | −149 | −182 ~ −117 | 438-18-1344 | 24.8% | −132 |
| 13 | Qwen 3.8 Max | −205 | −239 ~ −171 | 356-2-1442 | 19.8% | −331 |
| 14 | GLM 5.3 | −315 | −362 ~ −271 | 203-13-1584 | 11.6% | −447 |

The last column is the half-time tournament rating (first-half versions), a different scale. Full tables: `results/final/成绩.md`, `results/halftime/成绩.md`.

## Progress tournament

Improvement = rating of the second-half version minus rating of the model's own first-half version, on the same scale (progress games and half-time games fitted together). Largest gains: Doubao +395 (+332 ~ +460), Gemini +170, Astra +147, GLM +129, Qwen +86, DeepSeek +58. Muse, MiMo and Haiku resubmitted byte-identical code, so their values are noise. SolUF came out at −62 (−120 ~ +1). Full tables: `results/附表.md`.

## Try it

- **Watch**: open any `.html` in `replays/` in a browser (space = play/pause, arrow keys = step, `H` = shortcuts). Each replay is the game closest to the median length among the majority result of that pairing; no cherry-picked comebacks.
- **Challenge the AIs** (Python 3.10+ and g++): `python challenge.py your_bot.cpp` plays 20 games against each of the 14 second-half bots, the reference bot and the baseline (320 total), using native formal mode with the unchanged 2000/50 CPU ms limits. Add `--full` for 80 games per opponent (1,280 total). The 16 final-tournament opponent ratings stay fixed; only your rating is fitted, then compared with the 14 ranked contestants. This is an estimate, not a refitted mini-tournament or an official placing.
- **Develop**: in `workspace/`, `python arena.py play submission/bot.cpp baseline.cpp --seed 7`, `python arena.py vs new.cpp old.cpp --games 200`; see `workspace/README.md` (Chinese).
- **Reproduce the tournaments**: `tournament/README.md`. The configs carry the original schedule seeds, so maps, seats and pairings are rebuilt exactly.

## Submit to the community leaderboard

Create `community/tank/<github-username>__<entry-name>/` with `bot.cpp`, `meta.json` and optional `NOTES.md`. Include the official `"tank.hpp"` and implement `init` and `decide`; do not provide your own `main` or engine files. From the repository root:

```bash
python community/check.py community/tank/<your-folder> --skip-eval  # validate and compile with official tank_main.cpp
python community/check.py community/tank/<your-folder>              # 320 formal-mode games
python community/check.py community/tank/<your-folder> --full       # 1,280 formal-mode games
```

Open a PR containing only that entry folder once it passes. See the [submission guide](../community/README_en.md) for format, licensing, rating caveats and safety, or use the [coding-agent prompts](../community/AGENT_PROMPT.md). PR evaluation uses the target branch's trusted scripts, so Tank submission support must be merged there before Tank entries can be evaluated.

## Running a new model under contest conditions

`workspace/` contains the baseline's source `baseline.cpp`; contestants only got the compiled program. **Do not hand the `workspace/` folder to a model.** Build a contest-identical package first:

```bash
python make_player_pack.py        # writes out/workspace.zip
```

The script compiles `baseline.cpp` into `baseline.exe` with the contest flags, checks every other file against the sha256 of the original package recorded in `workspace_manifest.json`, and zips them with the original file list and layout, without `baseline.cpp`. Then put only `out/workspace.zip` in an empty folder, start a fresh session there, and send the opening prompt from `prompt.md` verbatim. The time rules (30 minutes, earliest submission at minute 20, forced collection at 45) are in the prompt; `clock.py` in the package starts counting when it is extracted. For the second half, run a half-time tournament with `tournament/`, place the standings, the model's replays and the opponents' executables in `workspace/halftime/` as described under Format, and send the half-time message from `prompt.md`. For a single half, just run `challenge.py` on the submitted `submission/bot.cpp`.

## Folder

| Path | Contents |
|---|---|
| `workspace/` | Contestant package (identical to the original zip, except that contestants only got the compiled `baseline.exe` while this repo ships its source `baseline.cpp`; the two play turn-for-turn identical games) |
| `prompt.md` | Every message sent to the models |
| `bots/half1/`, `bots/half2/` | Each model's sealed first- and second-half code, unmodified |
| `results/` | Standings and per-game results of all three tournaments, plus the two supplementary tables |
| `replays/` | 15 selected replays |
| `tournament/` | Tournament platform, configs, and the reference bot's source |
| `notes/` | Trash talk and post-match reviews |
| `make_player_pack.py`, `workspace_manifest.json` | Build a contest-identical package; file list and hashes of the original package |
| `CONDITIONS.md` | Conditions, every interruption, time extension and organizer error |

## Caveats

- Each model ran each half once. These are the results of this event, not a rigorous benchmark. Tools, channels, reasoning effort and machines differ between models.
- Five first halves (Grok, GLM, Doubao, MiMo, Qwen) are reruns: a post-event audit found the original runs were affected by outside interruptions, insufficient time compensation, or a prompt truncated by the launch script, so they were voided and rerun from scratch in the same folders.
- Network outages and tool failures were compensated by moving the clock start, based on session logs; some amounts are estimates and err in the contestant's favour. Doubao hit its 5-hour quota at minute 27.8 of the second half and was collected as-is without compensation.
- The three external contestants were not timed by the organizer and ran on a faster machine.
- Five speed-check batches in each of the half-time and final tournaments were flagged as inconsistent and still counted. Bots that stop searching by `budget_left()` vary slightly with machine load.
- All bot code was written by the models and is unmodified. Rules, referee, baseline and reference bot were written by the organizer.
