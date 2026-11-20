[中文](CONDITIONS.md)

# Event conditions

Programs were written on 2026-11-16 (eleven players) and 2026-11-17 (the two GPT-6.2 models). Times are the organiser's local time (UTC−8).

## Format

- Each player had its own working directory with the same player pack ([workspace/](workspace/README.md), zip SHA-256 `74dfde0d44142ff013d34076dcc0dfd1fbb7f6edbafc5ca524ab6dcb5d3c1dc3`). Opening messages: [prompt_en.md](prompt_en.md).
- **75 minutes**, counted from the opening message; players run `python clock.py` to see the time. They hand in `player.cpp` and `notes.md`; when a player stops replying, or at the deadline, the organiser snapshots the directory and the `player.cpp` at that moment counts.
- **No hand-in before minute 60**, a rule set on the day: a player that stopped earlier received a call-back message in the same session and kept going, with the idle time added back (start and deadline both shifted). Sessions interrupted by API or account problems were resumed the same way. Stopping after minute 60 counts as handing in. The first batch of six had not been told this rule in their opening message; from the second batch on it was part of the message. Builds saved at call-backs and interruptions are in [appendix/early/](appendix/README.md) and do not count.
- Players could write any helper scripts and play unlimited games with the local referee and the three sparring bots (binaries only). They could not modify organiser files, read or write outside their directory, use the network, call other AIs or sub-agents, or kill processes by name. Nobody could see other programs or results during the event; the organiser answered no questions.
- All players ran on the organiser's one Windows PC, several at a time.

## Players and tools

| Player | Seat | Tool | Model (event record) | Reasoning | Access |
|---|---|---|---|---|---|
| GPT-6.2 Astra | `astra` (submission dir `astra62`) | Codex CLI 0.169.0 | `gpt-6.2-astra` | max | organiser's ChatGPT subscription |
| GPT-6.2 Sol | `sol` (`sol62`) | Codex CLI 0.169.0 | `gpt-6.2-sol` | max | organiser's ChatGPT subscription |
| Claude Fable 5.2 | `fable` | Claude Code interactive, **started by hand** | `claude-fable-5-2` | max | organiser's Claude subscription |
| Claude Opus 5.5 | `opus` | Claude Code interactive, **started by hand** | `claude-opus-5-5` | max | organiser's Claude subscription |
| Gemini 4 Carbon | `carbon` (`gemini_carbon`) | Antigravity CLI | `gemini-4-carbon-high` | high | Google |
| Gemini 4 Hydrogen | `hydrogen` (`gemini_hydrogen`) | Antigravity CLI | `gemini-4-hydrogen-xhigh` | xhigh | Google |
| Grok 4.7 | `grok` | Grok Build CLI 1.0.77 | `grok-4.7` | xhigh | xAI |
| GLM 5.3 | `glm` | OpenCode | `zhipu-official/glm-5.3` | max (no suffix = highest) | Zhipu official |
| DeepSeek-V4.1-Flash | `deepseek` (`deepseek_dsh`) | dsh 0.2.2-rc.1 (standard mode, `danger-full-access`) | `deepseek-flash` | max | DeepSeek official |
| Kimi K3.1 Pro | `kimi` (`kimi2`) | Kimi Code 2.6.0 | `moonshot/kimi-k3.1-pro` | max | Moonshot official |
| Qwen4-Max | `qwen` | Qoder, **started by hand** | `qwen4-max` | max | built into Qoder |
| MiMo V2.6 Pro | `mimo` | OpenCode | `mimo-official/mimo-v2.6-pro` | high (highest on the official API) | Xiaomi official |
| Doubao Seed-Evolving | `doubao` | OpenCode | `volcengine-api/doubao-seed-evolving` | high | Volcano Ark |

- The aim was each vendor's own coding tool; for Doubao, MiMo and GLM the vendor tools are little used, so OpenCode was used. DeepSeek and Kimi first ran in OpenCode in the first batch; after DeepSeek's OpenCode run stopped at minute 42 with clearly weak results, both redid the task in their own tools and the official results use those runs (the OpenCode builds are in the appendix). Each tool ran once, which is not enough to say anything about the tools themselves.
- Reasoning levels are launch settings, not verified per request; levels with the same name in different tools are not equivalent.
- Versions come from a check of the machine on the morning of 11-16: Claude Code 2.1.325, dsh 0.2.2-rc.1, Kimi Code 2.6.0, Grok Build 1.0.77; Codex CLI 0.169.0 was recorded when the two GPT-6.2 runs started. Whether OpenCode (2.2.7 that morning) and the Antigravity CLI (1.3.1) were upgraded before the start was not recorded; Codex Desktop and Qoder were the organiser's own windows and their versions were not recorded. Versions are not back-filled from the machine at publication time.
- Only public service names are listed; no accounts, credentials or proxy settings.

## Time used and how each run ended

| Player | Start | Collected | Time used | Ended by | Call-backs and interruptions |
|---|---|---|---:|---|---|
| GPT-6.2 Astra | 11-17 09:57 | 11:12 | 75 min | deadline | none |
| GPT-6.2 Sol | 11-17 09:49 | 11:04 | ~75 min | stopped itself | none |
| Claude Fable 5.2 | 11-16 07:52 | 09:22 | 75 min | deadline | stopped at 59.1 min, called back after 12.5 idle minutes |
| Claude Opus 5.5 | 11-16 07:55 | 09:11 | 75 min | deadline | none |
| Gemini 4 Carbon | 11-16 10:44 | 11:58 | ~73 min | stopped itself | none |
| Gemini 4 Hydrogen | 11-16 10:45 | 11:52 | ~67 min | stopped itself | relaunched within a minute of the first launch; timed from the second |
| Grok 4.7 | 11-16 10:17 | 11:17 | ~60 min | stopped itself | none |
| GLM 5.3 | 11-16 10:17 | 11:32 | ~61 min | stopped itself | organiser's API account ran out of balance three times (twice at 14.7 min, once at 46.2 min); resumed after top-up with time added back |
| DeepSeek-V4.1-Flash | 11-16 09:14 | 10:47 | ~62 min | stopped itself | stopped at 35.3 min and called back; API connection dropped at 47.8, 49.3 and 50.9 min, resumed with time added back |
| Kimi K3.1 Pro | 11-16 10:17 | 11:23 | ~61 min | stopped itself | API connection dropped at 54.9 min, resumed |
| Qwen4-Max | 11-16 10:29 | 11:44 | 75 min | deadline | the organiser sent the opening message in Qoder before starting the clock, so it actually had a few minutes more than 75 |
| MiMo V2.6 Pro | 11-16 10:17 | 11:22 | ~63 min | stopped itself | stopped at 51.1 min and called back |
| Doubao Seed-Evolving | 11-16 10:17 | 11:30 | ~63 min | stopped itself | organiser's API account ran out of balance at 48.0 min; resumed after top-up |

"Start" and "Collected" are wall-clock times; "Time used" excludes idle time that was added back. The call-back messages actually sent are in [prompts/叫回通知_实发/](prompts/).

## Qualifier games

- 1v1: every pair of the 13, on 6 maps (Twin Lakes, Yin-Yang + the hidden Ice Floes, Ruins, Skybridge, Stone Circle), 19 seeds per map, each side in seat 0 once: 228 games per pair, 17,784 in total.
- FFA: 420 rounds; each round one player sits out by a fixed draw (13 is not a multiple of 4) and the other 12 form 3 groups chosen to balance how often each pair meets; each group plays 12 games on one map and seed (three pairings × four seat rotations): 15,120 games. Draw seed `a11-ep11-haixuan-20261116`; schedule in `results/schedule.json.gz`.
- 10 ms CPU per step; referee with `--sandbox none`, Windows Job Objects limit memory and child processes.
- **Played in two passes**: on 11-16 a full qualifier was played with the preview builds of Astra and Sol ([appendix/results/灰度版海选/](appendix/README.md)). After the two redid the task with the GPT-6.2 release on 11-17, the 19,512 games of the same schedule not involving them were kept, and the 13,392 games involving them were re-played with the new programs (377 seconds); together they form the official ranking.
- The 11-16 pass ran while six control players were working on the same machine, so the load was high: 5 to 39 one-step timeouts per player (each just means "stand still that step"; Qwen had the most), and no program was killed. See `faults` in `results/海选成绩.json`. The organiser did not replay them.

## Auction

- Around 14:00 on 11-16 an auction was held with the preview Astra and Fable as captains. Afterwards the organiser declared it a rehearsal and voided it: the auction messages had not said that team members would rewrite their programs for the team battle, and both captains had judged candidates by running their qualifier programs in 4v4 and counting friendly fire. A clarification was sent that evening; Fable hit its usage limit and did not reply, so the re-run was postponed.
- The official auction was on 11-17 with the release GPT-6.2 Astra as captain; rules, candidates and order unchanged; both captains got 10 minutes to re-read the material and both were given the rehearsal bids. Messages and answers: [auction/](auction/README.md).
- Captains took part in the same sessions they had used for the qualifier.

## Not published

Raw session logs (conversation, reasoning, tool calls), the organiser's operations scripts and logs, keys and account details, raw bills.
