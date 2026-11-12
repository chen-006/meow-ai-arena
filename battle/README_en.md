[简体中文](README.md)

# Episode 10 · Armchair Generals (纸上谈兵)

Nine models each get 90 minutes to deploy armies. In every battle a contestant gets a budget, buys troops, places them inside a deployment zone and writes a C++ commander program; once the battle starts nobody can intervene, and block-built soldiers hack at, crash into, knock flying and push each other off cliffs in rigid-body physics. Five challenge levels are against the organizer's defenders and bosses, and among those who pass, the cheaper army scores higher; then the contestants face each other blind, with one deployment for 1v1 and one for a four-player free-for-all. One hand-in, no randomness, 945 battles in total (45 level runs, 144 duels, 756 free-for-alls).

- **What's here**: `workspace/` contestant package · `engine/` simulator source · `bots/` the nine contestants' deployments and commander programs · `results/` standings and per-battle results · `replays/` replays · `tournament/` scoring and reproduction · `renderer/` the replay-to-video renderer
- **Reproduce the leaderboard**: `python tournament/score.py` recomputes the overall, level and versus standings from the 945 per-battle results with the contest's own scoring script and checks them item by item against `results/`, printing "核对通过" (check passed) in a few seconds. To recompile the submissions and replay all 945 battles: `python build_engine.py`, then `python tournament/score.py --rerun` (a few minutes with 8 threads).

## Rules in brief

- **Nine unit types**: militia, shield, spear, archer, cavalry, hammer, bomber, catapult and giant, costing 10–350. A deployment is a JSON file placing units on the 0.5 m grid of the deployment zone; each unit can be set to "hold position" or "free" (default behaviour) and given a tag for the program to find it by. The commander program sees the battlefield every 0.1 s (positions, health and facing of all units, items, terrain) and gives orders to its own units; one program is used for every battle, and it is optional.
- **Physics**: the simulator runs deterministic rigid-body physics on Jolt Physics: the same deployment and program always produce the same battle. Units get knocked flying, toppled, fall off cliffs, are shot by their own archers, or drown in the rising tide. Contestants had the very same `war.exe` the referee used.
- **Five levels** (all public, each with its own win condition):

| Level | Name | Budget | Time limit | Win condition | Organizer's reference cost |
|---:|---|---:|---:|---|---:|
| 1 | Meadow Ambush (草甸迎击) | 700 | 90 s | Wipe out the attackers (cavalry charge across a meadow with a mud pond in front of you) | 335 |
| 2 | Broken Dragon Bridge (断龙桥) | 600 | 150 s | Wipe out the attackers while your banner stays up (powder kegs hang by the bridge and can blow it) | 280 |
| 3 | Storm the Battery (拔炮台) | 450 | 150 s | Wipe out the plateau's defenders and its three catapults | 410 |
| 4 | Narrow Pass (一线天) | 400 | 150 s | Keep your general alive and wipe out the canyon's defenders (snowballs on the slope) | 370 |
| 5 | Mountain Beast (镇山兽) | 500 | 120 s | Bring down the boss | 400 |

- **Level score**: 0 if you fail; among those who pass, the cheapest gets 100 and every doubling of cost costs 25 points, with a floor of 20 for a pass. **Level total** = mean of the five. The organizer's reference cost is not used in scoring.
- **1v1**: budget 1000; the same `duel.json` plays both maps, Sakura Hill (樱丘) and River Isle (河心洲); every pair plays 4 battles (2 maps × swapped seats); win 1, draw 0.5, loss 0. Wiping out the opponent wins; at time-out the side with more value on the hill wins.
- **Four-player free-for-all**: budget 600 on Tidal Sandbar (涨潮沙洲); the tide starts rising at 20 s and anyone standing in water is out. Every 4-player combination plays 6 battles; each battle scores placement points (3/2/1/0) + kill points (enemy value destroyed ÷ 600, at most 3).
- **Total** = levels × 60% + versus × 40%, with versus = 1v1 × 60% + free-for-all × 40%.
- Full rules: [RULES.md](workspace/RULES.md) (Chinese); commander interface: [COMMANDER.md](workspace/COMMANDER.md); the simulator in [engine/src/](engine/src/) is authoritative.

## Contestants

| Contestant | Tool | Reasoning effort | Submission folder | Seat name |
|---|---|---|---|---|
| GPT-6.2 Sol | Codex desktop app (started by the organizer by hand) | max | [`bots/gpt-6.2-sol/`](bots/gpt-6.2-sol/) | `sol` |
| Claude Fable 5.2 | Claude Code (interactive window, run by a third party on another machine) | max | [`bots/claude-fable-5.2/`](bots/claude-fable-5.2/) | `fable` |
| Gemini 4 Carbon | OpenCode (run on another machine by a stand-in) | high | [`bots/gemini-4-carbon/`](bots/gemini-4-carbon/) | `gemini_carbon` |
| Gemini 4 Hydrogen | OpenCode (Google Vertex AI) | high | [`bots/gemini-4-hydrogen/`](bots/gemini-4-hydrogen/) | `gemini_hydrogen` |
| Qwen4-Max-Preview | Qoder (started by the organizer by hand) | xhigh | [`bots/qwen4-max-preview/`](bots/qwen4-max-preview/) | `qwen` |
| DeepSeek-V4.1-Flash | dsh | max | [`bots/deepseek-v4.1-flash/`](bots/deepseek-v4.1-flash/) | `deepseek` |
| Kimi K3.1 Pro | Kimi Code | max | [`bots/kimi-k3.1-pro/`](bots/kimi-k3.1-pro/) | `kimi` |
| Doubao Seed-Evolving | OpenCode (Volcengine Ark) | high | [`bots/doubao-seed-evolving/`](bots/doubao-seed-evolving/) | `doubao` |
| ERNIE 5.5 Flash Preview (文心) | OpenCode (OpenRouter) | high | [`bots/ernie-5.5-flash-preview/`](bots/ernie-5.5-flash-preview/) | `ernie` |

The seat name is the internal name used during the contest; per-battle results and replay file names use it. Midway through the contest the organizer's whole machine rebooted once; every seat resumed its original session and got back exactly the time it lost. Gemini 4 Carbon could not run because of rate limits on the organizer's API account, so a friend of the organizer ran it on another machine with the same contestant package and the word-for-word same prompt. ERNIE was added as a ninth seat during the contest. The Claude Fable 5.2 seat uses the record of a post-contest rerun by a third party on another machine; its first run is archived unmodified in [first-run/](first-run/claude-fable-5.2/README_en.md) and not scored. Gemini 4 Hydrogen's `high` is not this model's highest effort level (`xhigh`). See [CONDITIONS_en.md](CONDITIONS_en.md).

## Overall standings

| Rank | Contestant | Total | Levels | Versus | 1v1 | Free-for-all | Levels passed |
|---:|---|---:|---:|---:|---:|---:|---:|
| 1 | GPT-6.2 Sol | 89.9 | 95.4 | 81.7 | 100.0 | 54.1 | 5 |
| 2 | Claude Fable 5.2 | 83.4 | 91.2 | 71.8 | 85.9 | 50.7 | 5 |
| 3 | Gemini 4 Carbon | 71.3 | 84.7 | 51.0 | 59.4 | 38.4 | 5 |
| 4 | Gemini 4 Hydrogen | 68.8 | 77.7 | 55.6 | 64.1 | 42.9 | 5 |
| 5 | DeepSeek-V4.1-Flash | 47.6 | 50.8 | 42.7 | 50.0 | 31.8 | 3 |
| 6 | Qwen4-Max-Preview | 45.5 | 52.5 | 35.0 | 35.9 | 33.5 | 3 |
| 7 | Kimi K3.1 Pro | 32.2 | 38.7 | 22.4 | 29.7 | 11.4 | 3 |
| 8 | ERNIE 5.5 Flash Preview | 31.6 | 40.5 | 18.4 | 21.9 | 13.2 | 3 |
| 9 | Doubao Seed-Evolving | 12.0 | 15.6 | 6.7 | 3.1 | 12.1 | 1 |

## Levels

Each cell is the level score with the army's cost in parentheses; "fail" is followed by the cost.

| Contestant | Level 1 | Level 2 | Level 3 | Level 4 | Level 5 |
|---|---|---|---|---|---|
| GPT-6.2 Sol | 92.0 (250) | **100.0** (60) | 85.8 (385) | **100.0** (295) | 99.3 (250) |
| Claude Fable 5.2 | **100.0** (200) | 67.0 (150) | **100.0** (260) | 89.0 (400) | **100.0** (245) |
| Gemini 4 Carbon | 96.6 (220) | 51.5 (230) | 92.5 (320) | 93.3 (355) | 89.8 (325) |
| Gemini 4 Hydrogen | 92.0 (250) | 43.2 (290) | 81.9 (430) | 97.1 (320) | 74.3 (500) |
| Qwen4-Max-Preview | 78.8 (360) | 94.4 (70) | fail (430) | 89.0 (400) | fail (500) |
| DeepSeek-V4.1-Flash | 82.5 (325) | 81.6 (100) | fail (425) | 89.9 (390) | fail (475) |
| ERNIE 5.5 Flash Preview | 75.0 (400) | 45.1 (275) | 82.3 (425) | fail (395) | fail (490) |
| Kimi K3.1 Pro | 76.4 (385) | 22.1 (520) | fail (450) | 94.9 (340) | fail (500) |
| Doubao Seed-Evolving | 77.8 (370) | fail (600) | fail (450) | fail (400) | fail (500) |

9, 8, 5, 7 and 4 contestants passed levels 1–5. The cheapest pass on level 2 cost 60 against a reference of 280, and on levels 1, 3, 4 and 5 someone also beat the reference cost. End reasons, times and cost ratios per level are in [闯关榜.md](results/闯关榜.md).

## Versus

1v1 points of the row against the column over 4 battles (maximum 4):

| | Sol | Fable | Hydrogen | Carbon | DeepSeek | Qwen | Kimi | ERNIE | Doubao |
|---|---|---|---|---|---|---|---|---|---|
| GPT-6.2 Sol | — | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 4 |
| Claude Fable 5.2 | 0 | — | 3.5 | 4 | 4 | 4 | 4 | 4 | 4 |
| Gemini 4 Hydrogen | 0 | 0.5 | — | 2 | 4 | 2 | 4 | 4 | 4 |
| Gemini 4 Carbon | 0 | 0 | 2 | — | 2 | 4 | 4 | 3 | 4 |
| DeepSeek-V4.1-Flash | 0 | 0 | 0 | 2 | — | 4 | 2 | 4 | 4 |
| Qwen4-Max-Preview | 0 | 0 | 2 | 0 | 0 | — | 1.5 | 4 | 4 |
| Kimi K3.1 Pro | 0 | 0 | 0 | 0 | 2 | 2.5 | — | 2 | 3 |
| ERNIE 5.5 Flash Preview | 0 | 0 | 0 | 1 | 0 | 0 | 2 | — | 4 |
| Doubao Seed-Evolving | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | — |

GPT-6.2 Sol won all 32 of its duels. Free-for-all results (336 battles each: mean score, mean placement, wins) are in [对抗榜.md](results/对抗榜.md).

## Watch / play

**Run locally** (Windows; Python 3.9+, g++; no third-party Python packages):

```text
python build_engine.py                 build the simulator into workspace/war.exe (downloads Jolt Physics the first time)
cd workspace
python play.py map 2                   draw level 2 in the terminal
python play.py level 2 ../bots/gpt-6.2-sol/level2.json --cmd ../bots/gpt-6.2-sol/commander.cpp
python play.py duel                    1v1 with submission/duel.json against the sparring armies
python play.py replay replays/level2.wrp --at 20    narrate a battle and draw the field at 20 s
```

All `play.py` commands are listed in [workspace/README.md](workspace/README.md) (Chinese). To pit a contestant's army against others: `play.py duel --opp ../bots/<name>/duel.json:../bots/<name>/commander.cpp`.

**Replays**: [replays/](replays/README_en.md) holds all 45 level runs plus 18 versus battles picked by a fixed rule, compressed as `.wrp.xz`; `python replays/unpack.py` unpacks them for `play.py replay`, or render them to video with [renderer/](renderer/README_en.md). `python tournament/score.py --rerun --replays` regenerates every replay.

**Make videos**: all footage in the video was rendered from replays by [renderer/](renderer/README_en.md) with Blender; with Blender 5.2, Python (numpy, pillow) and ffmpeg installed, one command renders a clip.

## Folder contents

| Path | Contents |
|---|---|
| [workspace/](workspace/README.md) | The contestant package as handed out: README, RULES, COMMANDER, local tools `play.py`, `build.py`, `clock.py`, five levels, three versus maps, sparring armies, examples, starter `submission/` |
| [engine/](engine/README_en.md) | Simulator source (C++, on Jolt Physics), interface notes, replay format; built with `python build_engine.py` |
| [prompt_en.md](prompt_en.md) | The opening and resume messages (translation; original in [prompt.md](prompt.md)) |
| [bots/](bots/README_en.md) | The nine hand-ins, unmodified: `level1.json`–`level5.json`, `duel.json`, `melee.json`, `commander.cpp`, `notes.md` |
| [results/](results/README_en.md) | Overall, level and versus standings (.md and .json); `eval/` holds the contest's deployment checks, schedule, program health and all 945 per-battle results |
| [replays/](replays/README_en.md) | 63 replays |
| [tournament/](tournament/README_en.md) | The contest's scoring script `scoring.py` (unmodified), scoring config, reproduction script `score.py` |
| [renderer/](renderer/README_en.md) | Replay-to-video renderer: code, scene themes for the eight maps, unit and item models, render command |
| [CONDITIONS_en.md](CONDITIONS_en.md) | Contest conditions, time used per seat, interruptions and time extensions; why the Claude Fable 5.2 seat uses the third-party rerun |
| [first-run/](first-run/claude-fable-5.2/README_en.md) | Claude Fable 5.2's first run (archived, not scored): its submission unmodified and five level replays |
| [source_manifest.json](source_manifest.json) | SHA-256 of each published file and of the contest-time original |

The character illustrations, expressions and voxel characters live at the top level in [art/](../art/README_en.md) and [characters/](../characters/README_en.md).

## Differences from the contest

- The contestant package (`workspace/`) and the submissions (`bots/`) are byte-for-byte originals; hashes are in [source_manifest.json](source_manifest.json). The fallback compiler path in `workspace/build.py` points to a location on the organizer's machine and is kept as is; elsewhere set `CXX` or put g++ on PATH (`score.py` and `build_engine.py` also look in the repository's `tools/w64devkit/`).
- The contest package shipped a prebuilt `war.exe`. The repository does not commit the exe; it ships the source and `build_engine.py`. Built with w64devkit's GCC 16.2.0 and Jolt Physics commit `6a985659a781`, the exe is **byte-identical** to the contest one (SHA-256 `f703bab3…`). Other compilers are not guaranteed to match; check the results with `tournament/score.py --rerun`.
- Three lines of engine source changed, none affecting the binary: two comment lines at the top of `host.hpp` that referred to another episode's internal files, and the default Jolt Physics path in `CMakeLists.txt`, which pointed to the organizer's machine and now points to `engine/third_party/JoltPhysics`.
- The contestant working directory in the opening message is written as `{WORKDIR}`.
- `results/` is the output of the organizer's re-evaluation and new standings after the Claude Fable 5.2 seat switched to the rerun record (see [CONDITIONS_en.md](CONDITIONS_en.md#claude-fable-52-the-third-party-rerun-counts)). In its JSON files (standings and `eval/`), absolute paths on the organizer's machine were rewritten relative to the contest directory (`kit/` = contestant package, `sealed/<seat>/submission/` = hand-ins, `eval/` = evaluation), and the compiler path reads `g++`; all values are unchanged. The `.md` standings are unmodified.
- Replays are xz-compressed; unpacked they are byte-identical to the evaluation replays (`replays/sha256.json`), so the map-file path in each replay's trailing result block is still an absolute path on the organizer's machine.
- Not included: contestant session logs and reasoning, post-contest interviews, the organizer's operations scripts (launching, timing, collecting, resuming, merging the stand-in run, API monitoring), keys and private configuration. `tournament/scoring.py` is the unmodified code of the contest's evaluation and scoring stages; the `host.py` its docstring mentions is the unpublished operations script and is not needed for reproduction.

## Disclaimer

This is the record of one specific contest. Tools, access channels and sampling all affect the outcome; it does not presume which model is stronger. Deployments and commander programs were written by the models and not modified by the organizer; the task, simulator, levels, sparring armies and contest rules were provided by the organizer. Licensed under the repository's [MIT license](../LICENSE).
