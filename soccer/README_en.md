[简体中文](README.md)

# Episode 8 · 2v2 simplified soccer

Twelve models formed six teams. Each model wrote one C++ bot controlling one player: a 5-minute tactical meeting, 40 minutes of development, a half-time tournament and replay package, then another 40 minutes. At the start of the second half, the organizer added an individual event in which each model fields two of its own bots.

This archive uses the **final versions after team2 replayed its second half from half time on October 29, 2026**. There are 2,112 half-time games, 2,904 final team games and 3,264 individual games: 8,280 total, including reference opponents. Only the team and individual leaderboards are published. The announced 50% team / 50% individual weighting is documented, but the archived combined total is not published or used to create a third board.

English guides: [conditions](CONDITIONS_en.md), [prompts](prompt_en.md), [rules](workspace/RULES_en.md), [workspace](workspace/README_en.md), [bots](bots/README_en.md), [results](results/README_en.md), [replays](replays/README_en.md), [reproduction](tournament/README_en.md), and [post-game reviews](notes/README_en.md). Original code, Chinese task materials and model-authored answers retain their original language; English task documents are reference translations. Session transcripts and thinking are excluded.

- **What's in this folder**: `workspace/` contestant package · `prompt.md`, `prompts/` organizer messages · `packages/` contest-time packages and team-chat instructions · `bots/` first- and second-half bots · `sparring/` baseline and sparring bots · `results/` team and individual boards with per-game results · `replays/` replays · `tournament/` schedules, rescoring and replay scripts · `notes/` post-match review answers
- **Reproducing the leaderboard**: Run `python build_reference.py`, then `python tournament/score.py final` (individual board: `python tournament/score.py individual --bt`) to recompute the points from the per-game data; replaying games is covered in [`tournament/README_en.md`](tournament/README_en.md).

## Rules in brief

- An enclosed 12000×8000 field with cut corners, two players per side, 1,200 turns. The ball bounces off walls; more goals wins. No offside, fouls, throw-ins or corners.
- Each independent bot outputs `AX AY KX KY MSG`: acceleration, kick vector and one byte delivered to its teammate on the next turn. Opponents cannot see the message.
- Every bot sees itself attacking right. Seat 0/1 rotates by the **seed's index in the schedule**, not the numeric seed's parity: development slot A takes seat 0 at s0, s2, …; slot B at s1, s3, ….
- CPU budget: 1,000 ms for the first turn, then 10 ms per turn. An overrun cancels that turn's action; 30 accumulated overruns terminate the bot. A stopped bot issues zero actions while the game continues. Windows AppContainer prevents network access, file I/O and child processes.
- The full [rules](workspace/RULES_en.md) defer to [soccer.hpp](workspace/soccer.hpp) for physics. The base rules' statement that copying is allowed predates the team event: **the actual opening prompt and team-event instructions prohibited copying entire programs or substantial blocks from a teammate**, including for an individual partner bot.

## Format and scoring

| Stage | What happens |
|---|---|
| Tactical meeting | 5 minutes after both teammates arrive; discussion only, no code or file writes |
| First half | 40 minutes; each hands in `submission/player.cpp`. Read teammate code and agree protocols, but do not edit teammate files or copy substantial blocks |
| Half-time event | Six teams, round robin, plus baseline and two sparring teams; 32 seeds per pairing, both sides, 2,112 games |
| Half-time package | Standings, every replay involving the team and replay tools; shared by both teammates |
| Second half | 40 minutes. At its start the organizer announces 50% team / 50% individual and adds `submission/partner.cpp`. A failed player build falls back to the first-half bot; a missing or failed partner uses the player's bot in both seats |
| Final team event | 44 seeds per pairing, both sides, plus three reference teams: 2,904 games |
| Individual event | Twelve individual teams; 16 seeds per pairing, both sides, plus references: 3,264 games |

Win/draw/loss gives 3/1/0 points. Score rate = points / (games against other contestants × 3); ties are broken by goal difference, then goals scored. Reference games do not count toward these points or places. Separate Bradley–Terry ratings include reference games: win 1, draw 0.5, baseline = 0, +400 ≈ 10:1, one virtual draw per played pairing, 95% intervals from 300 whole-seed bootstrap samples. BT does not determine the ranking; the two boards are separate fits. Local `match.py run` also uses win 1 / draw 0.5, so its score rate is different from the official boards.

## Contestants

| Team | Development slot A | Development slot B |
|---|---|---|
| team1 | Grok 4.7 (Grok Build) | Tencent Hunyuan Hy4 (OpenCode) |
| team2 | Kimi K3.1 Pro (Kimi Code) | StepFun Step 5 (OpenCode) |
| team3 | Gemini 4 Argon (OpenCode) | Doubao-Seed-Evolving (OpenCode) |
| team4 | GPT-6 Astra (Codex) | GPT-6.1 Terra (Codex) |
| team5 | Claude Opus 5.5 (Claude Code) | GLM 5.3 (OpenCode) |
| team6 | DeepSeek V4.1 Flash 1017 (dsh) | Claude Haiku 5.5 (Claude Code) |

A/B identifies development slots, not fixed player seats or field sides. Filenames retain `teamN_a` / `teamN_b`; see the [bot index](bots/README_en.md). Tools and access conditions differed.

## Team leaderboard (final)

| Rank | Team / model | Score rate | Points / maximum | W-D-L | Goals for:against | BT rating (95% CI) |
|---:|---|---:|---:|---|---|---|
| 1 | team4：GPT-6 Astra + GPT-6.1 Terra | 100.0% | 1320/1320 | 440-0-0 | 6104:533 | +841（+761～+938） |
| 2 | team3：Gemini 4 Argon + Doubao-Seed-Evolving | 73.3% | 968/1320 | 318-14-108 | 3406:1691 | +343（+271～+428） |
| 3 | team5：Claude Opus 5.5 + GLM 5.3 | 61.1% | 807/1320 | 262-21-157 | 2723:2266 | +332（+268～+396） |
| 4 | team6：DeepSeek V4.1 Flash 1017 + Claude Haiku 5.5 | 26.4% | 349/1320 | 100-49-291 | 1204:3183 | -189（-251～-137） |
| 5 | team1：Grok 4.7 + Tencent Hunyuan Hy4 | 19.5% | 257/1320 | 81-14-345 | 1886:4806 | -302（-361～-241） |
| 6 | team2：Kimi K3.1 Pro + StepFun Step 5 | 14.3% | 189/1320 | 49-42-349 | 1064:3908 | -364（-435～-306） |

Team4's 440 wins were against the other five contestant teams. Against the strongest reference sparring team it had 64 wins, 12 draws and 12 losses. Full reference-opponent records: [team board](results/团队榜_en.md).

## Individual leaderboard (final)

| Rank | Team / model | Score rate | Points / maximum | W-D-L | Goals for:against | BT rating (95% CI) |
|---:|---|---:|---:|---|---|---|
| 1 | GPT-6 Astra | 94.2% | 995/1056 | 326-17-9 | 5054:442 | +794（+707～+915） |
| 2 | GPT-6.1 Terra | 88.4% | 934/1056 | 304-22-26 | 4336:411 | +669（+600～+766） |
| 3 | Gemini 4 Argon | 84.2% | 889/1056 | 291-16-45 | 3510:504 | +591（+516～+673） |
| 4 | Claude Opus 5.5 | 75.6% | 798/1056 | 263-9-80 | 3173:804 | +483（+442～+539） |
| 5 | Kimi K3.1 Pro | 52.7% | 556/1056 | 175-31-146 | 1267:1814 | -163（-221～-107） |
| 6 | DeepSeek V4.1 Flash 1017 | 42.3% | 447/1056 | 142-21-189 | 1682:2385 | -263（-345～-166） |
| 7 | Claude Haiku 5.5 | 41.5% | 438/1056 | 139-21-192 | 1724:2706 | -240（-327～-145） |
| 8 | GLM 5.3 | 36.0% | 380/1056 | 119-23-210 | 1838:3402 | -343（-424～-271） |
| 9 | Grok 4.7 | 33.6% | 355/1056 | 111-22-219 | 1437:3081 | -362（-432～-302） |
| 10 | Doubao-Seed-Evolving | 26.7% | 282/1056 | 87-21-244 | 1160:3008 | -440（-524～-368） |
| 11 | Tencent Hunyuan Hy4 | 8.0% | 84/1056 | 26-6-320 | 895:4250 | -713（-816～-602） |
| 12 | StepFun Step 5 | 6.6% | 70/1056 | 21-7-324 | 822:4091 | -720（-827～-623） |

Opus and Doubao did not submit a partner, so both seats use their second-half player bot. Kimi and GLM submitted partner files byte-identical to their own player bot. See the [individual board](results/个人榜_en.md).

## Incidents and handling

- GLM lost approximately 16.5 minutes to first-half API timeouts; DeepSeek had approximately 5 minutes of stream inactivity and transport retries. Neither received time compensation.
- **Team2's first second-half attempt was voided** after Kimi hit a daily token cap and Step encountered an API timeout. The second half was replayed from half time on October 29; first-half submissions and half-time results remain valid. Moonshot's official API was overloaded during the rerun. Two time credits totalled 27 min 25 s; Kimi ultimately completed the remaining time using **the same Kimi K3.1 Pro model served by Volcano Ark**, not Moonshot's official service for that final segment.
- **The whole individual event was rerun** with new seeds after an organizer sandbox failure prevented all four bots from starting in two Astra–DeepSeek games, which had incorrectly been recorded as 0:0 draws. Only final valid results are included.
- The file-audit daemon stopped for much of the first half and was later backfilled and cross-checked. Missing audit records cannot establish that no operation occurred. Three manually started models received their second-half message about 1.7–1.9 minutes late without compensation. Details: [conditions](CONDITIONS_en.md).

- **Incomplete unread-message reminders:** The [team-chat instructions](packages/TEAMCHAT_README_en.md) say **if** an unread-message reminder appears after compilation, match or replay tools, read the message; this is conditional wording. In the implementation, only `clock.py` had reminder integration; `build.py`, `match.py` and `replay.py` did not. All contestants used the same tools.

## Rule rulings

- A movement function in **DeepSeek's individual partner** constitutes copying a substantial local block of teammate Haiku's code. This is a scoped code-origin finding, not a claim that the entire bot was copied. **Results remain unchanged.**
- **Grok** wrote one system temporary file to send a team-chat message, outside its permitted working folder. This was a minor boundary violation, recorded without changing results.

## Watch and play

Open [viewer.html](workspace/viewer.html) offline and drag in a sampled `.txt` from the [replay index](replays/README_en.md). The 18 games are a fixed-seed random sample, without outcome-based selection.

Local play requires Windows, Python 3.10+ and g++. The tools, including PNG/GIF export, use only the Python standard library.

```text
python build_reference.py
cd workspace
python match.py check template.cpp
python match.py run --me template.cpp --mate template.cpp --opp baseline --seeds 2 --workers 1
python match.py play ../bots/half2/team4_a.cpp ../bots/half2/team4_b.cpp baseline baseline --seed 3
```

Put the compiler on PATH, set `CXX`, or place w64devkit under the repository's `tools/w64devkit/bin/`. See the [workspace guide](workspace/README_en.md) and [original-schedule reproduction](tournament/README_en.md).

## Folder

| Path | Contents |
|---|---|
| [workspace/](workspace/README_en.md) | Base contestant package, engine, referee, local tools, template and viewer |
| [prompt.md](prompt_en.md), [prompts/](prompts/README_en.md) | Organizer prompts, not conversation transcripts |
| [packages/](packages/README_en.md) | Team/chat instructions, historical package inventory and reconstruction steps, valid clock records |
| [bots/](bots/README_en.md) | 24 sealed player bots and 10 second-half partner bots, byte-identical |
| [sparring/](sparring/README_en.md) | Source for the three reference bots |
| [results/](results/README_en.md) | Two final leaderboards, all three events' per-game data and program provenance |
| [replays/](replays/README_en.md) | 18 sampled replays and selection manifest |
| [tournament/](tournament/README_en.md) | Original schedules and BT implementation; new local replay and scoring entry points |
| [notes/](notes/README_en.md) | Twelve post-game answers, including team4's new answers collected after the October 29 fix |
| [CONDITIONS.md](CONDITIONS_en.md) | Conditions, incidents and rulings |
| [source_manifest.json](source_manifest.json) | Source and published SHA-256 values, with adaptation flags |

## Differences from the event

- Sealed bot code and line endings are unchanged. In the base package, `build.py` replaces the organizer's private compiler-search directory with repository `tools/w64devkit`. The sparring header changes only its relative engine include. The referee auto-build command in `match.py` also adds the missing `-luserenv` library; the original package shipped a prebuilt exe and its fallback source build failed without it. The referee also removes its fixed absolute sandbox fallback, retaining only explicit SOCCER_BOX_ROOT or its adjacent directory. The local tool rebuilds a cached referee when its source or engine header is newer, and the workspace guide documents the change. All adaptations are recorded in the manifest.
- The referee's sandboxes now reuse a fixed pool of names, `soccer2v2.<root tag>.s<k>` (the tag is the first 128 bits of the SHA-256 of the sandbox root path), instead of a new `soccer2v2.<pid>.<n>` name for every bot in every game. Windows keeps a kernel record (pool tag `SeLs`) for every AppContainer SID it has seen and frees it only at reboot, so after tens of thousands of sandboxes `CreateProcess` fails with error 1450 (insufficient system resources) until the machine restarts. With reused names this record no longer grows with the number of games, and sandbox folders and profiles left by a killed referee are removed the next time their slot is taken in that root; because the name carries the root tag, a bot running under another sandbox root never gets the same identity and cannot read those leftover program copies. Bot processes are now created inside their Job (previously created suspended and assigned afterwards), so a referee killed between those two steps no longer leaves a suspended bot process holding its program copy until reboot. Sandbox restrictions, game results and replays are unchanged.
- Contestants received reference executables only; this archive provides their source and a build script, not binaries. Builds with different compilers need not be byte-identical. Do not hand this entire source archive to a model as a contest-identical package.
- Organizer launch, timing, sealing, account/proxy setup, audit and team-chat services are omitted. Prompt references to `tc.py` / `clock.py` document the historical workflow; this archive does not automate a fresh event. The [package inventory and reconstruction guide](packages/restore_en.md) lists historical files, hashes and dynamic dependencies. Half-time standings and game lists can be reconstructed; missing historical replays can be rerun but are not guaranteed byte-identical. The complete duplicate material is not included.
- Results and schedules are gzip-compressed; machine paths become paths relative to `soccer/` or `<build>` placeholders. Other statistics, seeds, seats and hashes are retained. Replay paths describe the historical full set; only sampled files are included, and other games can be replayed.
- No raw sessions, team-chat transcripts, thinking, contribution-attribution reports, internal analysis reports, voided data, secrets or private configuration are included. Post-game answers are the models' own accounts, not verified findings. Team4's new answers from the collection rerun after the October 29 fix are included.

## Caveats

This records one event, not a general model ranking. API incidents, tools, teammates, samples and CPU timing affect the outcome; no counterfactual placing is inferred. Contestants generated their code; the organizer supplied the task, engine, referee, reference bots and rules. The repository's [MIT license](../LICENSE) applies.
