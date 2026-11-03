[简体中文](README.md)

# meow-ai-arena · AI Coding Arena

Several AI models receive **the exact same prompt**, each writes a game bot in about 60 minutes, and the bots then play thousands of matches against each other. This repository contains the materials from the video series: game rules, referees, every model's submitted code (unmodified), per-game results, rating scripts, and one-click scripts to challenge the AIs with your own bot.

| Episode | Game | Players | Games | Winner | Folder |
|---|---|---|---:|---|---|
| 1 | Territory (paper.io-like) | 1v1 + 4-player | 30,000 | Sonnet 5.5 | `land/` |
| 2 | Bomberman | 2–10 | 8,930 | GPT-6 Astra | `bomberman/` |
| 3 | Capture the flag | 2–15 | 32,010 | GPT-6 Astra / GPT-6.1 Sol (tie) | `ctf/` |
| 4 | Diplomacy (models play directly) | 7 | 3 | Fable 5.1 (total over 3 games; solo win in game 3) | `diplomacy/` |
| 5 | Warehouse scheduling (legacy-code optimization relay) | 12, in 6 two-model teams | 1 run per round | Team F: MiMo V2.6 Pro + GPT-6 Astra (team score; rounds 2–3 were a rematch) | `warehouse/` |
| 6 | Tank battle (two halves) | 1v1, 14 models | 27,840 | GPT-6 Astra | `tank/` |
| 7, round 1 | Tron light cycles | 1v1 + 4p + 12p, 10 models (+2 reference) | 5,713 | Mistral Large 4 | `tron/` |
| 7, round 2 | Legacy-code optimization (warehouse task) | 10 models, solo (+3 Claude reference runs) | 1 run each | StepFun Step 5 preview / Tencent Hunyuan Hy4 preview (tie) | `warehouse/` |
| 8 | 2v2 simplified soccer | 12 models in 6 teams; individual teams added in the second half | 8,280 | Team: GPT-6 Astra + GPT-6.1 Terra; individual: GPT-6 Astra | [soccer/](soccer/README_en.md) |

Episode 4 is different: no bots. Seven models each play one power in standard Diplomacy, negotiating by private messages and issuing orders themselves, for three games with memory carried over. `diplomacy/` contains the referee and scheduler, full game archives (every message, order and adjudication), replays, and the models' own notes.

Episode 5 is different again: no head-to-head games. Twelve models form six teams of a "lead" and a "tester" and relay-optimize a slow, messy C++ warehouse-robot simulation that hides five quirks which must be preserved. The lead speeds it up while keeping the output byte-identical; each tester hunts for counterexamples in the other five teams' code; then the tester takes over its own lead's code, implements a requirement change and keeps optimizing. The five working final versions ran roughly 400–2700× faster than the original, close to the resolution of the Windows CPU timer. `warehouse/` contains the task packages, every submission (code, hand-off notes, counterexamples), hidden workloads, the reference implementation, referee scripts, blind maintainability reviews by three of the contestants, and each model's post-mortem. Each model ran each round once, rounds 2–3 were re-run after the first attempt was voided, and one model (Grok 4.7) ran on official accounts dispatched through an account pool and behaved abnormally in round 3 (we suspect the account was being throttled at the time, but the logs cannot confirm it); see [warehouse conditions](warehouse/CONDITIONS_en.md). Two human volunteers also did the round-1 task with the same package; their code and separately measured results are in `warehouse/appendix/human/` and do not count toward the official scores.

Episode 6 returns to bot battles with two halves. Fourteen models write a first version of a 1-vs-1 tank-battle bot; all first versions play a round robin; at half time each model receives the standings, its own replays and the other models' first-version executables, and then improves its bot. Only the second-half version counts. `tank/` contains both versions of every bot, all three tournaments (half-time, final, and a progress tournament measuring each model's improvement), selected replays, the tournament platform and configs, trash talk and post-match reviews. Three of the fourteen were run by outside operators on a faster machine and were not timed by the organizer; five first halves were rerun after the originals were voided; see [tank conditions](tank/CONDITIONS_en.md) and [Tank Battle](tank/README_en.md).

Episode 7, "District King Championship", has two rounds with the same ten models. Round 1 is Tron light cycles: each model writes one C++ bot that plays 1-vs-1, 4-player and 12-player games; ratings are Bradley–Terry per game type weighted 50/30/20, anchored at Doubao = 0 (the anchor was chosen after the race). GPT-6 Astra and DeepSeek V4.1 Flash ran as reference contestants and, with an organizer-written strong sparring bot, appear only on a separate strong-opponent board. `tron/` contains the task package, all sealed bots, both leaderboards, per-game results, 30 randomly sampled replays and a challenge script; it also lists, with links and thanks, the 2010 Google AI Challenge human bots used in a side experiment (their code is not included, as most have no license). There was no trash talk and no post-match review this time. Round 2 reuses the episode 5 warehouse task as a solo legacy-code optimization round (`warehouse/`, section "Episode 7"). Each round awards 10…1 points by place; the overall "District King" is StepFun Step 5 preview (see the end of `tron/README_en.md`).

**Watch**: open any `.html` file under `*/replays/` in a browser (episode 1: `land/replays/index.html`). Episode 5 has no replays; its results are in `warehouse/results/最终成绩.md`.

**Challenge the AIs**: install Python 3.10+ and g++, then run `start.cmd` (Windows) or `sh start.sh`, or directly `python ctf/challenge.py your_bot.cpp` (episode 6: `python tank/challenge.py your_bot.cpp`; episode 7: `python tron/challenge.py your_bot.cpp`, Windows only). Rules and the I/O protocol are in each episode’s `workspace/RULES.md`; English reference translations are available as `workspace/RULES_en.md`.

## Videos and English documentation

The Chinese series **AI 算法对抗大赛** is on Bilibili. The English edition, **AI Coding Arena**, is being released on YouTube: **TODO: YouTube link**.

| Episode | English documentation |
|---|---|
| 1 · Territory | [Land Grab](land/README_en.md) |
| 2 · Bomberman | [Bomberman](bomberman/README_en.md) |
| 3 · Capture the Flag | [Capture the Flag](ctf/README_en.md) |
| 4 · Diplomacy | [Diplomacy](diplomacy/README_en.md) |
| 5 · Warehouse scheduling | [Warehouse](warehouse/README_en.md) |
| 6 · Tank Battle | [Tank Battle](tank/README_en.md) |
| 7 · Tron and Legacy Code Optimization | [Tron](tron/README_en.md) · [Warehouse](warehouse/README_en.md#episode-7--legacy-code-optimization-round) |
| Shared tournament platform | [Arena](arena/README_en.md) |

## Submit your own

Your work can be added to this repository and, once merged, appears on the [community leaderboard](community/LEADERBOARD.md).

**What you can submit**: a bot for episode 1, 2, 3, 6 or the Tron round of episode 7 (hand-written, AI-written, or modified from an official entry), an optimized version of the episode 5 warehouse code, or a fixed version of a model's code with known defects. Episode 4 has no program to submit.

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

Each episode's `CONDITIONS.md` (with an English `CONDITIONS_en.md`) documents tools, model versions, reasoning effort, API channels, time used, and every interruption or incident. Ratings use different scales across episodes and are not comparable. Bot code (and, in episode 4, all messages, orders and notes) was generated by the models; everything else was written by the organizer. License: MIT, except that episode 4 depends on the `diplomacy` package (AGPL-3.0+) and its replay pages embed jDip map graphics (GPL).

Most character artwork in the videos comes from the Bilibili creator **ZipZipPipe** ([space.bilibili.com/4168597](https://space.bilibili.com/4168597)); per the creator's notice, the whale-girl character is a derivative of an original character by 上善无形 ([source](https://b23.tv/3dNz55h)) and is licensed under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/). Character art, voice-over, and video editing are not part of this repository and are not covered by the MIT license.
