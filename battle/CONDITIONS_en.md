[简体中文](CONDITIONS.md)

# Contest conditions

The contest took place on 2026-11-12; all times below are Los Angeles local time (UTC−8).

## Format

- Each contestant had a separate working directory containing the same contestant package ([workspace/](workspace/README.md); package SHA-256 `380d47a9…b584ff`, see [source_manifest.json](source_manifest.json)). The opening message is in [prompt_en.md](prompt_en.md); the nine messages differed only in the working directory.
- **90-minute limit**, counted from when the contestant starts; contestants check the time themselves with `python clock.py`. All five levels and both versus formats were handed out at the start; contestants decide how to split the time.
- **One hand-in**: `level1.json`–`level5.json`, `duel.json`, `melee.json`, `commander.cpp` and `notes.md` in `submission/`. Either the contestant declares it is done, or at the deadline the organizer snapshots `submission/`; the files at that moment count. There is no half-time and no chance to see anyone else's results.
- **No randomness**: the simulator uses no random numbers and every battle is played once. Contestants had the same `war.exe` the referee used; whatever they got locally is the official result.
- Contestants may write any helper scripts (Python standard library) and run as many battles as they like with `war.exe` and `play.py`. They may not modify the organizer's files, read or write outside the working directory, go online, call other AIs or start sub-agents, kill processes by name or wait with long sleeps. Nobody can see anyone else's deployments or programs during the contest, and the organizer answers no questions.
- After the hand-in the organizer evaluated everything on one Windows machine: the nine commander programs were compiled with the same command as the package's `build.py` (`g++ -O2 -std=c++17 -static`, GCC 16.2.0 from w64devkit), the 63 deployments were checked, and `war.exe batch` ran all 945 battles (45 level runs, 144 duels, 756 free-for-alls) on 8 threads. All nine programs compiled, all 63 deployments were valid, no battle hit an engine error, and no commander program timed out or crashed ([程序运行情况.json](results/eval/程序运行情况.json)).

## Scoring

As in section 2 of the package's [RULES.md](workspace/RULES.md):

- **Level score**: 0 for a fail; among those who pass, the cheapest gets 100 and every doubling of cost loses 25 points, with a floor of 20 for a pass. "Cheapest" is taken over the nine contestants only; the organizer's reference cost is not used. **Level total** = mean of the five levels.
- **1v1**: every pair plays once on Sakura Hill and once on River Isle, then again with seats swapped: 4 battles; win 1, draw 0.5, loss 0; 1v1 score = mean × 100.
- **Free-for-all**: every 4-player combination (126 groups) plays 6 battles (one fixed player in the south seat, the other three rotating through the remaining seats); each battle scores placement points (3/2/1/0) + kill points (enemy value destroyed ÷ 600, at most 3); free-for-all score = mean ÷ 6 × 100.
- **Versus** = 1v1 × 60% + free-for-all × 40%; **total** = levels × 60% + versus × 40%. Ties are broken by levels passed, then versus score, then the total cost of passed levels.

All results can be recomputed and checked with `python tournament/score.py`; see [tournament/README_en.md](tournament/README_en.md).

## Contestants and tools

| Contestant | Seat | Tool | Model (as recorded) | Effort | Access |
|---|---|---|---|---|---|
| GPT-6.2 Sol | `sol` | Codex desktop app, **started by the organizer by hand** | `gpt-6.2-sol` | max | Built into the Codex desktop app |
| Claude Fable 5.2 | `fable` | Claude Code 2.1.322, interactive window, **run by a third party on another machine** (the post-contest rerun is the one that counts, see [below](#claude-fable-52-the-third-party-rerun-counts)) | `claude-fable-5-2` | max | Not recorded |
| Gemini 4 Carbon | `gemini_carbon` | OpenCode desktop app, **run on another machine by a stand-in** (see below) | `gemini-4-carbon` | high | Google API directly |
| Gemini 4 Hydrogen | `gemini_hydrogen` | OpenCode CLI 2.2.7 | `google-vertex/gemini-4-hydrogen` | high (not this model's highest level, `xhigh`; see below) | Google Vertex AI |
| Qwen4-Max-Preview | `qwen` | Qoder, **started by the organizer by hand** | `qwen4-max-preview` | xhigh | Built into Qoder |
| DeepSeek-V4.1-Flash | `deepseek` | dsh (standard mode) | `deepseek-official/deepseek-flash` | max | DeepSeek official |
| Kimi K3.1 Pro | `kimi` | Kimi Code 2.6.0 | `moonshot/kimi-k3.1-pro` | max | Moonshot official |
| Doubao Seed-Evolving | `doubao` | OpenCode CLI 2.2.7 | `volcengine-api/doubao-seed-evolving` | high | Volcengine Ark |
| ERNIE 5.5 Flash Preview (文心) | `ernie` | OpenCode CLI 2.2.7 | `openrouter/baidu/ernie-5.5-flash-preview:free` | high | OpenRouter (free model) |

- The seat is the contest's internal name, used in per-battle results and replay file names; submissions sit in `bots/` under folders named after the model, mapped in [bots/roster.json](bots/roster.json).
- The effort level is the setting in the launch configuration. The organizer's policy was to give every model its own highest level; Gemini 4 Hydrogen is the exception: it ran with `high`, while this model's highest level is `xhigh`, which the organizer did not use when configuring it (Gemini 4 Carbon's highest level is `high`). The effort level is not an independent check of every request, and levels with the same name in different tools are not equivalent.
- Every tool was updated to its latest version before the contest (11-12); the version numbers in the table come from that update, and Claude Code's version comes from the session records of its two runs; dsh, the Codex desktop app and Qoder have no recorded version, and we do not fill them in from whatever is installed today.
- In standard mode dsh hit permission errors on every shell command in the working directory, so this seat ran with `permission_mode=danger-full-access` (same as episode 9).
- Apart from Gemini 4 Carbon and Claude Fable 5.2, the other seven contestants worked on the same Windows machine of the organizer. Only public service names are listed; no accounts, credentials, project IDs or proxy settings are published.

## Time used and how each seat ended

| Contestant | Time used | Ended by | Interruptions |
|---|---:|---|---|
| GPT-6.2 Sol | 90.0 min | Deadline | Machine reboot after 51.7 min; resumed |
| Claude Fable 5.2 | 80.9 min | Finished itself | The rerun on another machine counts; no interruptions. The first run is archived, see below |
| Gemini 4 Carbon | 86.7 min | Finished itself | Could not run on the organizer's machine; rerun from scratch on another machine (see below) |
| Gemini 4 Hydrogen | 86.9 min | Finished itself | Stopped 3 times by the API's content filter, resumed each time (the first stop was followed by the machine reboot; it resumed after the reboot) |
| Qwen4-Max-Preview | 89.6 min | Finished itself | Machine reboot after 47.2 min; resumed |
| DeepSeek-V4.1-Flash | 59.1 min | Finished itself | Machine reboot after 55.7 min; resumed |
| Kimi K3.1 Pro | 87.7 min | Finished itself | Machine reboot after 55.7 min, resumed; later stopped once by an "insufficient balance" API error, resumed after a top-up |
| Doubao Seed-Evolving | 90.0 min | Deadline | Machine reboot after 55.7 min; resumed |
| ERNIE 5.5 Flash Preview | 67.1 min | Finished itself | Joined mid-contest; no interruptions |

"Time used" excludes interruptions. "Finished itself" means the contestant declared its hand-in and the session ended.

## Interruptions, time extensions and special cases

**Start.** At 04:27:00 the contest script started the six automated seats (fable, gemini_carbon, gemini_hydrogen, deepseek, kimi, doubao) at the same time (the fable seat later switched to its post-contest rerun; here and in the reboot paragraph below, "fable" means its first run). The sol and qwen seats were started by the organizer by hand in desktop tools (Sol in the Codex desktop app, Qwen in Qoder) with the same opening message. In the contest record the clocks of these two seats start at 04:31:00 (Sol) and 04:35:30 (Qwen), earlier than the moment (04:39) the organizer registered "manual start" in the contest script; their time is counted from the recorded start.

**Machine reboot.** Around 05:22:40 the organizer's whole machine rebooted. Every seat's tool process, the timers and whatever commands the contestants were running were killed; the files in the working directories survived. The organizer's ruling: every seat **resumes its original session** with the [resume message](prompt_en.md#resume-message) and **gets back exactly the time it lost**: time used before the interruption still counts, the remaining time is given in full from the moment of resumption, nothing is counted during the interruption, and `clock.py` shows times with the interruption removed.

- deepseek, kimi, doubao, and fable's first run: 55.7 minutes used before the interruption; resumed at 05:32:23 with 34.3 minutes left.
- sol, qwen: 51.7 and 47.2 minutes used; at 05:34:48 the organizer sent the resume message in the original desktop-tool sessions, leaving 38.3 and 42.8 minutes. sol was collected at its deadline at 06:13:08; qwen ended at 06:17:13, registered by the organizer in the contest script.
- gemini_hydrogen: it had already been stopped by the content filter before the reboot (see below); it was resumed with the others at 05:32:23 with 55.7 minutes left.

**Stops on API errors.** Handled the same way: resume the original session and give back the lost time.

- gemini_hydrogen: stopped by the API's content filter (`provider.content-filter: Provider blocked the response`) at 05:01:21, 05:58:35 and 06:03:31, after 34.3, 60.5 and 64.8 minutes in total, and resumed each time; it finished itself at 06:26:29 after 86.9 minutes.
- kimi: stopped at 05:45:40 by an API error saying the account balance was low (68.9 minutes used); after the organizer topped up, it resumed at 05:49:57 with 21.1 minutes left and finished itself at 06:08:46 after 87.7 minutes. The contest script first logged this ending as an abnormal exit (it re-read the previous balance error from the same log file); after checking it was corrected to "finished itself", and the original entry is kept in the contest record.

**Gemini 4 Carbon was run by a friend of the organizer.** This seat never got going on the organizer's Vertex AI account: at 04:53 it exited on a request timeout (`provider.transport: Timed out waiting for response headers`) after being stuck on a single request since 04:33; after the reboot it was resumed and then repeatedly rate-limited (HTTP 429) or exited abnormally, unable to work normally. The organizer moved the whole seat to another machine: a friend of the organizer ran it from scratch with **the same contestant package** (all 33 fixed files checked by hash) and **the word-for-word same opening message**, using the OpenCode desktop app, the Google API directly, model `gemini-4-carbon`, effort high. The half-finished attempt on the organizer's machine was discarded and not evaluated. The `submission/` returned by the stand-in run was merged into the official contest directory and evaluated together with the other eight on the organizer's machine. One thing differed from the other seats: the stand-in wrote the clock's start time into a `clock.json` one level above the working directory, so the `clock.json` inside the working directory still said "not started", and the first `clock.py` call showed "186.1 minutes used, 90 minutes are up". The contestant read the source of `clock.py`, concluded that file timestamps were the cause, changed neither `clock.json` nor `clock.py`, kept time itself from when it received the task, and handed in at 86.7 minutes; its time is counted from the start recorded by the stand-in to its last reply.

**ERNIE was added as a ninth seat during the contest.** The contest started with eight seats; the organizer added ERNIE 5.5 Flash Preview while it was running. It started at 05:32:23 with a fresh 90 minutes and finished itself at 06:39:28 after 67.1 minutes. It received the same package and opening message as everyone else and, like them, could not see anyone else's deployments or results.

## Claude Fable 5.2: the third-party rerun counts

The official result of the Claude Fable 5.2 seat is the record of a post-contest rerun by a third party on another machine (the organizer's decision). The first run (the one the contest script launched on the organizer's machine) is archived unmodified, with its submission and five level results, in [first-run/claude-fable-5.2/](first-run/claude-fable-5.2/README_en.md), and **is not scored**. After the submission was swapped, the organizer re-evaluated all nine contestants with the same pipeline and rebuilt the standings (all 945 battles replayed); [results/](results/README_en.md) holds the recomputed standings. The other eight submissions did not change: the 572 battles without Fable give exactly the same results as before apart from timing fields; the 373 battles involving Fable, and the level scores that are relative to the cheapest pass, changed accordingly.

### Conditions of the two runs

| | First run (archived, not scored) | Third-party rerun (counts) |
|---|---|---|
| Model, effort | `claude-fable-5-2`, max | same |
| Tool | Claude Code 2.1.322, headless (`claude -p`), called by the contest script | Claude Code 2.1.322, interactive window, opening prompt pasted by hand by the third party |
| Workspace | see [source_manifest.json](source_manifest.json) | the same package (33 fixed files, hashes verified) |
| Opening prompt | see [prompt_en.md](prompt_en.md) | identical except the working-directory line |
| Machine | the organizer's machine, nine contestants running at the same time | another computer, running only this seat |
| Shell | only Windows PowerShell 5.1 and Git Bash were available; the model used Git Bash almost throughout (99 calls, PowerShell once) | PowerShell 7 was available; the model used PowerShell throughout (110 calls) |
| Interruption | the organizer's machine rebooted at minute 55.7; the session was resumed and the lost time given back | none |
| Time used | 83.0 min, submitted by itself | 80.9 min, submitted by itself |
| Human input | none (one resume message from the organizer) | none; the opening prompt was the only message |
| Sub-agents, web access | none | none |

Usage during the contest window (from the sessions' usage records):

| | First run | Third-party rerun |
|---|---|---|
| Requests | 107 | 92 |
| Tool calls | 137 | 172 |
| Output tokens (thinking) | about 341k (231k) | about 261k (160k) |
| Time waiting for the model / running tools | about 64 min / about 19 min | about 48 min / about 33 min |
| Output tokens per second | about 88 | about 90 |

Level results of the two runs (same `war.exe`; seconds to finish in parentheses):

| Level | First run | Third-party rerun (counts) |
|---|---|---|
| 1 Meadow Ambush | cleared, 260 (52.6 s) | cleared, 200 (55.1 s) |
| 2 Broken Dragon Bridge | cleared, 395 (52.9 s) | cleared, 150 (73.8 s) |
| 3 Storm the Battery | failed (handed in the one-militia starter file, 10) | cleared, 260 (136.2 s) |
| 4 Narrow Pass | cleared, 375 (115.1 s) | cleared, 400 (110.5 s) |
| 5 Mountain Beast | failed (starter file, 10) | cleared, 245 (103.6 s) |

The two runs also took different paths. In the rerun the model first built a batch-testing script that calls `war.exe batch` directly, cleared all five levels in about 40 minutes, then worked on the versus layouts while searching for cheaper formations in the background (its own time log is in [bots/claude-fable-5.2/notes.md](bots/claude-fable-5.2/notes.md)). In the first run the model spent a long time on levels 2 and 3 and only wrote its cost-reduction script in the last few minutes (see [first-run/claude-fable-5.2/submission/notes.md](first-run/claude-fable-5.2/submission/notes.md)).

### What we checked

- **Workspace**: the 33 fixed files of the package used for the rerun were checked one by one by hash and match what the other contestants received.
- **Prompt**: word for word the same as for the other contestants, except the working-directory line.
- **Process**: in the rerun the opening prompt was the only message; no human input, no sub-agents, no web access.
- **Model and effort**: the same in both runs (`claude-fable-5-2`, max); output speed was the same too, about 88–90 tokens per second.
- **First run**: its session record shows no downgrade, fallback or safety stop.
- **Evaluation**: the `submission/` returned by the rerun was merged into the official contest directory and evaluated together with the other eight seats on the organizer's machine: the program compiled, all 7 deployments were valid, no timeouts or crashes.

### Why the rerun counts

The first run may have been affected by its environment. Possible causes, none of them tested on its own:

1. **Shell**: the organizer's machine had no PowerShell 7 at the time. Claude Code's tool description to the model listed a series of Windows PowerShell 5.1 limitations; from its first command the model switched to Git Bash (99 calls, PowerShell once) and once ran into Git Bash rewriting command-line arguments (see its notes.md). The rerun machine had PowerShell 7, and the model used PowerShell throughout (110 calls).
2. **Shared machine**: in the first run nine contestants worked on the same computer at the same time.
3. **Reboot**: at minute 55.7 the organizer's whole machine rebooted; the original session was resumed, but every background job the contestant had running was gone.
4. **How it was called**: the first run was headless (`claude -p`, called by the contest script); the rerun was an interactive window.

**The organizer's conjecture (no evidence)**: when a model is called headlessly through the SDK, it may perform worse than in an interactive window. The organizer has previously seen less usable quota under this way of calling, and suspects the model is held back in this mode. This is only a conjecture; we have no data that proves it, and the checks above found no record of a downgrade or fallback. The organizer considered scoring the first run unfair to Claude Fable 5.2, so this seat uses the rerun in an interactive window.

### Limitations

- The other eight contestants ran once each; the Fable seat effectively ran twice and the second run was taken, and the decision to switch was made after the first run's results were known.
- The rerun was not on the organizer's machine; its machine, shell and load differ from those of the seven contestants on the organizer's machine (the Gemini 4 Carbon seat was also run on another machine).
- There is one run per condition, so the causes listed above cannot be separated, and plain run-to-run variation cannot be ruled out.
- Both runs' submissions and results are public: the one that counts is in [bots/claude-fable-5.2/](bots/claude-fable-5.2/), the first run in [first-run/claude-fable-5.2/](first-run/claude-fable-5.2/README_en.md). Readers can judge for themselves.

### Third-party retests welcome

Any model can be run again under the same conditions:

1. Copy a clean [workspace/](workspace/README.md) as the contestant's working directory; first run `python build_engine.py` in `battle/` to build `war.exe` (or put a built `war.exe` into the working directory).
2. Before starting, write the start time into `clock.json` in the working directory: `{"minutes": 90, "start": <Unix seconds>}` (when `start` is `null`, `clock.py` counts from the moment `clock.json` was created).
3. Send the opening message from [prompt.md](prompt.md) (English translation in [prompt_en.md](prompt_en.md)) unchanged, with `{WORKDIR}` replaced by the working directory's path. 90 minutes; no messages in between.
4. After the hand-in, run `python play.py check` and `python play.py levels` in the working directory for the five level results; for versus, play the contestants in this repository with `python play.py duel --opp ../bots/<name>/duel.json:../bots/<name>/commander.cpp`.
5. Submit results as an issue in this repository: model, tool and version, effort, how it was run (interactive or headless), time used, any interruptions, and all files in `submission/`.

## Environment

- The simulator is a deterministic rigid-body simulation on Jolt Physics (cross-platform deterministic mode). The same deployment and program give the same result at any time in any directory, so contestants' local tests were the official results.
- This repository's `war.exe` is built from source (`python build_engine.py`); built with GCC 16.2.0 it is byte-identical to the contest exe (see [README_en.md](README_en.md#differences-from-the-contest)).
- Commander programs have a CPU time limit (see [COMMANDER.md](workspace/COMMANDER.md)). None of the nine timed out in the official evaluation; replaying on a much slower machine could make a program time out, and that battle would then differ from the official result.

## Caveats

- Apart from Claude Fable 5.2, each model ran once; a single run has randomness. Tools, access channels and effort levels all affect results; do not read this contest as a ranking of model capability.
- A concrete example: Claude Fable 5.2's two runs used the same workspace and prompt; the first run cleared three levels, the third party's post-contest rerun cleared all five. This seat uses the rerun; see [above](#claude-fable-52-the-third-party-rerun-counts).
- Gemini 4 Hydrogen ran with effort `high`, not this model's highest level, `xhigh`.
- Level scores are relative to the cheapest contestant, not to a theoretical optimum; the organizer's reference costs are only for comparison (listed in the level standings).
- Versus play is blind: each contestant submits one 1v1 deployment and one free-for-all deployment, so match-ups against particular opponents weigh heavily.
