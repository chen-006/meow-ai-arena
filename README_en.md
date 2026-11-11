[简体中文](README.md)

# meow-ai-arena · AI Coding Arena

Several AI models receive **the exact same prompt**, each writes a game bot in about 60 minutes, and the bots then play thousands of matches against each other. A few episodes change the format: the models play Diplomacy themselves, relay-optimize legacy code, or design bridges. This repository contains the materials from the video series: game rules, referees, every model's submitted code (unmodified), per-game results, rating scripts, and one-click scripts to challenge the AIs with your own bot.

## Episodes

| Ep. | Game | Folder | What happens | Winner | Reproduce | Play online |
|---|---|---|---|---|---|---|
| 1 | Territory (paper.io-like) | [`land/`](land/README_en.md) | 9 models each write a bot that plays both 1v1 duels and 4-player free-for-alls; 30,000 games | Sonnet 5.5 | [Reproduce the tournament](land/README_en.md#reproduce-the-tournament) | [ep1](https://arena.meowllm.top/en/ep1/) |
| 2 | Bomberman | [`bomberman/`](bomberman/README_en.md) | 9 models, 2–10 players per board with a shrinking arena; 8,930 games | GPT-6 Astra | [`tournament/`](bomberman/tournament/README_en.md) | [ep2](https://arena.meowllm.top/en/ep2/) |
| 3 | Capture the flag | [`ctf/`](ctf/README_en.md) | 11 models, 2–15 sides grabbing flags and carrying them home; 32,010 games | GPT-6 Astra / GPT-6.1 Sol (tie) | [`tournament/`](ctf/tournament/README_en.md) | [ep3](https://arena.meowllm.top/en/ep3/) |
| 4 | Diplomacy | [`diplomacy/`](diplomacy/README_en.md) | No bots: 7 models play seven-power Diplomacy themselves, negotiating, allying and betraying; 3 games | Fable 5.1 (total over 3 games; solo win in game 3) | [`games/`](diplomacy/games/) archives | — |
| 5 | Warehouse scheduling | [`warehouse/`](warehouse/README_en.md) | No head-to-head games: 12 models in 6 two-model teams relay-optimize slow, messy legacy code and hunt for counterexamples in other teams' code | Team F: MiMo V2.6 Pro + GPT-6 Astra (team score; rounds 2–3 were a rematch) | [`referee/`](warehouse/referee/README_en.md) | — |
| 6 | Tank battle | [`tank/`](tank/README_en.md) | 14 models, 1v1, two halves: at half time each gets the standings, its replays and its opponents' programs, then improves its bot; 27,840 games | GPT-6 Astra | [`tournament/`](tank/tournament/README_en.md) | [ep6](https://arena.meowllm.top/en/ep6/) |
| 7, round 1 | Tron light cycles | [`tron/`](tron/README_en.md) | 10 models (+2 reference contestants) in 1v1, 4-player and 12-player games; 5,713 games | Mistral Large 4 | [`tournament/`](tron/tournament/README_en.md) | [ep7](https://arena.meowllm.top/en/ep7/) |
| 7, round 2 | Legacy-code optimization | [`warehouse/`](warehouse/README_en.md#episode-7--legacy-code-optimization-round) | The same 10 models each optimize the episode 5 legacy code on their own (+3 Claude reference runs) | StepFun Step 5 preview / Tencent Hunyuan Hy4 preview (tie) | [`results/ep7/`](warehouse/results/ep7/README_en.md) | — |
| 8 | 2v2 simplified soccer | [`soccer/`](soccer/README_en.md) | 12 models in 6 teams, one player program each; individual teams added in the second half; 8,280 games | Team: GPT-6 Astra + GPT-6.1 Terra; individual: GPT-6 Astra | [`tournament/`](soccer/tournament/README_en.md) | [ep8](https://arena.meowllm.top/ep8/) (Chinese) |
| 9 | Bridge building | [`bridge/`](bridge/README_en.md) | No bots: 9 models design bridges in 90 minutes, 6 challenge levels + 1 bridge-vs-saboteur level | GPT-6.2 Sol | [`tournament/`](bridge/tournament/README_en.md) | [ep9](https://arena.meowllm.top/ep9/) (Chinese) |

Every episode README opens with what the episode is, what is in the folder, and how to reproduce its leaderboard.

Other top-level folders:

| Folder | Contents |
|---|---|
| [`community/`](community/README_en.md) | Community submissions and the [community leaderboard](community/LEADERBOARD.md) (see "Submit your own" below); `community/contests/` holds community-built contests |
| [`arena/`](arena/README_en.md) | General-purpose tournament platform: single games, schedules, Bradley–Terry ratings. Only the episode 3 official tournament ran on it; the other episodes ship their own tournament scripts under their `tournament/` |
| [`docs/`](docs/) | [Scoring methods](docs/scoring_en.md), [version-evolution experiment](docs/version-evolution_en.md), [open-source scope](docs/open-source-scope_en.md) |
| [`LICENSES/`](LICENSES/) | Full texts of CC BY-NC 4.0 and CC BY-NC-SA 4.0; see "License" below |
| `start.cmd`, `start.sh`, `start.py` | One-click menu (Chinese): watch replays, challenge the AIs with your bot, submit, check your setup (episodes 1–7 for now) |

## Watch and play

**Watch**: no installation needed. Open any `.html` file under `*/replays/` in a browser (episode 1: `land/replays/index.html`). Episode 5 has no replays; its results are in `warehouse/results/最终成绩.md`. For episode 8, open `soccer/workspace/viewer.html` and drop in a `.txt` from `soccer/replays/`. Or use the [website](https://arena.meowllm.top/en/), which also lets you play against the AI-written bots.

**Challenge the AIs**: install Python 3.10+ and g++, then run `start.cmd` (Windows) or `sh start.sh`, or directly `python ctf/challenge.py your_bot.cpp` (episode 6: `python tank/challenge.py your_bot.cpp`; episode 7: `python tron/challenge.py your_bot.cpp`, Windows only). Rules and the I/O protocol are in each episode’s `workspace/RULES.md`; English reference translations are available as `workspace/RULES_en.md`.

## Videos

The Chinese series **AI 算法对抗大赛** is on Bilibili. The English edition, **AI Coding Arena**, is being released on YouTube: **TODO: YouTube link**.

## Episode notes

Episode 4 is different: no bots. Seven models each play one power in standard Diplomacy, negotiating by private messages and issuing orders themselves, for three games with memory carried over. `diplomacy/` contains the referee and scheduler, full game archives (every message, order and adjudication), replays, and the models' own notes.

Episode 5 is different again: no head-to-head games. Twelve models form six teams of a "lead" and a "tester" and relay-optimize a slow, messy C++ warehouse-robot simulation that hides five quirks which must be preserved. The lead speeds it up while keeping the output byte-identical; each tester hunts for counterexamples in the other five teams' code; then the tester takes over its own lead's code, implements a requirement change and keeps optimizing. The five working final versions ran roughly 400–2700× faster than the original, close to the resolution of the Windows CPU timer. `warehouse/` contains the task packages, every submission (code, hand-off notes, counterexamples), hidden workloads, the reference implementation, referee scripts, blind maintainability reviews by three of the contestants, and each model's post-mortem. Each model ran each round once, rounds 2–3 were re-run after the first attempt was voided, and one model (Grok 4.7) ran on official accounts dispatched through an account pool and behaved abnormally in round 3 (we suspect the account was being throttled at the time, but the logs cannot confirm it); see [warehouse conditions](warehouse/CONDITIONS_en.md). Two human volunteers also did the round-1 task with the same package; their code and separately measured results are in `warehouse/appendix/human/` and do not count toward the official scores.

Episode 6 returns to bot battles with two halves. Fourteen models write a first version of a 1-vs-1 tank-battle bot; all first versions play a round robin; at half time each model receives the standings, its own replays and the other models' first-version executables, and then improves its bot. Only the second-half version counts. `tank/` contains both versions of every bot, all three tournaments (half-time, final, and a progress tournament measuring each model's improvement), selected replays, the tournament platform and configs, trash talk and post-match reviews. Three of the fourteen were run by outside operators on a faster machine and were not timed by the organizer; five first halves were rerun after the originals were voided; see [tank conditions](tank/CONDITIONS_en.md) and [Tank Battle](tank/README_en.md).

Episode 7, "District King Championship", has two rounds with the same ten models. Round 1 is Tron light cycles: each model writes one C++ bot that plays 1-vs-1, 4-player and 12-player games; ratings are Bradley–Terry per game type weighted 50/30/20, anchored at Doubao = 0 (the anchor was chosen after the race). GPT-6 Astra and DeepSeek V4.1 Flash ran as reference contestants and, with an organizer-written strong sparring bot, appear only on a separate strong-opponent board. `tron/` contains the task package, all sealed bots, both leaderboards, per-game results, 30 randomly sampled replays and a challenge script; it also lists, with links and thanks, the 2010 Google AI Challenge human bots used in a side experiment (their code is not included, as most have no license). There was no trash talk and no post-match review this time. Round 2 reuses the episode 5 warehouse task as a solo legacy-code optimization round (`warehouse/`, section "Episode 7"). Each round awards 10…1 points by place; the overall "District King" is StepFun Step 5 preview (see the end of `tron/README_en.md`).

Episode 9, "Bridge Building", has no bots: nine models design bridges within 90 minutes, with all 7 levels handed out at the start. In levels 1–6 the given vehicles must cross in time and intact, and cheaper passing designs score higher; in level 7 everyone submits a final bridge and a saboteur vehicle, and they play each other. `bridge/` contains the contestant package, the physics core source (`bridge/build_engine.py`), every contestant's designs, the reference solutions, results, replays and `tournament/score.py`, which recomputes and checks all results; see [bridge/README_en.md](bridge/README_en.md).

Episode 8, "2v2 Simplified Soccer", pairs twelve models into six teams; each model writes the program for one player, after a 5-minute tactics meeting, and the two programs play as a team. At half time the teams get their standings and replays and keep developing; in the second half each model also fields an individual team made of its own two programs, ranked on a separate board. `soccer/` contains the contestant package, every organizer message, both halves' programs, both boards with per-game results, replays, and scripts that rescore or replay any game; see [soccer/README_en.md](soccer/README_en.md).

## Submit your own

Your work can be added to this repository and, once merged, appears on the [community leaderboard](community/LEADERBOARD.md).

**What you can submit**: a bot for episode 1, 2, 3, 6 or the Tron round of episode 7 (hand-written, AI-written, or modified from an official entry), an optimized version of the episode 5 warehouse code, or a fixed version of a model's code with known defects. Episode 4 has no program to submit, and episode 8 (soccer) has no submission channel yet. Episode 9 (bridge building) takes submissions through the website leaderboard, not pull requests; see [bridge/README_en.md](bridge/README_en.md#submit-your-own).

**The fastest way** is to hand it to your coding agent (Claude Code, Codex, Cursor, ...). Send it the line below; it will ask you a few questions, test locally, show you what it is about to submit, and open the pull request after you approve:

```text
Read "Prompt A (English)" in https://github.com/chen-006/meow-ai-arena/blob/main/community/AGENT_PROMPT.md and follow its steps to submit my entry.
```

If you have nothing yet and want the AI to write one from scratch, use "Prompt B (English)" in the same file. The full prompts are in [`community/AGENT_PROMPT.md`](community/AGENT_PROMPT.md) and can also be copied as a whole.

**By hand**, it takes three steps:

1. Fork this repository and create `community/<episode>/<your-github-username>__<entry-name>/` containing `bot.cpp` (`src/` for episode 5) and `meta.json`.
2. Run `python community/check.py community/<episode>/<your-folder>`; it checks the format and runs the evaluation.
3. Commit only that folder, open a pull request, and paste the summary into the description.

Use `tank` for episode 6. Tank bots implement `init` and `decide`, include the official `tank.hpp`, and link with the official `tank_main.cpp`; do not provide your own `main`. Use `tron` for episode 7 light cycles. Tron bots implement only `int decide(const tron::State&)`, include the official `tron.hpp`, and link with the official `tron_main.cpp`; local compilation and evaluation require Windows. Tank / Tron submission support must be merged into the target branch before entries can be evaluated, because PR checks use trusted scripts from that branch.

GitHub Actions then evaluates the entry and a maintainer reviews and merges it. The community leaderboard is measured on GitHub-hosted VMs with few games and is not comparable with the official results from the show. Rules, the `meta.json` format and how the numbers are measured: [`community/README_en.md`](community/README_en.md) (short) and [`community/README.md`](community/README.md) (full, Chinese).

## Notes

Each episode's `CONDITIONS.md` (with an English `CONDITIONS_en.md`) documents tools, model versions, reasoning effort, API channels, time used, and every interruption or incident. Ratings use different scales across episodes and are not comparable. Bot code (and, in episode 4, all messages, orders and notes) was generated by the models; everything else was written by the organizer.

## License

Licensing follows directory boundaries:

| What | License |
|---|---|
| The games themselves (engines, rules, maps and levels), contestant packages, contestant submissions, referees and scoring scripts, results and replays, documentation, community submissions: everything not covered by the next two rows or the exceptions below | [MIT](LICENSE). Reproduce it, adapt it, build new contests on it |
| The renderer and shared art assets: each episode's `renderer/` directory (replay-to-video code, scene themes, unit and item models, render scripts) | [CC BY-NC 4.0](LICENSES/CC-BY-NC-4.0.txt): attribution, non-commercial |
| The voxel characters for each AI: the top-level `characters/` directory | [CC BY-NC-SA 4.0](LICENSES/CC-BY-NC-SA-4.0.txt): attribution, non-commercial, share-alike |

The renderer and the voxel characters are published together with the videos, starting with the first episode released after episode 9; episodes 1–9 will not get them retroactively, so neither directory exists yet. Each will carry a `LICENSE.md` stating which license applies. Images and videos you make with them are likewise non-commercial only and need attribution. For what is and isn't published, and how each episode's folder is laid out, see [Open-source scope](docs/open-source-scope_en.md).

Exceptions:

- The episode 4 referee depends on the [`diplomacy`](https://github.com/diplomacy/diplomacy) package (AGPL-3.0 or later, not included here). Its replay pages embed jDip map graphics (GPL) rendered by that package, so the replay pages as a whole are not MIT-licensed; see [`diplomacy/README_en.md`](diplomacy/README_en.md).
- The community contest [`community/contests/rmuc2026/`](community/contests/rmuc2026/) is based on Arena-1 and keeps the non-commercial learning license in its own directory, not MIT.
- Third-party dependencies (physics libraries, Blender and so on) are not bundled and are used under their own licenses.

## Attribution and thanks

Most character artwork in the videos comes from the Bilibili creator **ZipZipPipe** ([space.bilibili.com/4168597](https://space.bilibili.com/4168597)); per the creator's notice, the whale-girl character is a derivative of an original character by 上善无形 ([source](https://b23.tv/3dNz55h)) and is licensed under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/).

The character illustrations, voice-over and video editing from the videos are not part of this repository and are not covered by any of its licenses; if you want to use the illustrations, follow their original creators' terms.

The voxel characters were made by this project as derivative works of those character designs. When you use or adapt them, credit both:

- Voxel characters: made by meow-ai-arena (AI Coding Arena), CC BY-NC-SA 4.0;
- Original design: the creator of the character it is based on. `characters/CREDITS.md` lists the source for each character; for the whale girl, write "ZipZipPipe, based on an original character by 上善无形 ([b23.tv/3dNz55h](https://b23.tv/3dNz55h))".

Model and company names in character names belong to their respective owners and only indicate which model a character stands for; they do not imply endorsement by or involvement of those companies.
