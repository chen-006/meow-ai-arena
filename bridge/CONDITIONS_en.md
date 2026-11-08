[简体中文](CONDITIONS.md)

# Contest conditions

Contest date: 2026-11-07 (Los Angeles local time, UTC−8).

## Format

- Each contestant had its own working directory containing the same contestant package ([workspace/](workspace/README_en.md), identical file hashes). The opening message is in [prompt_en.md](prompt_en.md); the nine messages differ only in the directory name.
- Total time **90 minutes**, counted from when the contestant started; contestants checked the time themselves with `date`. **All 7 levels were handed out at once**; contestants decided how to split the time and could skip around and come back.
- Submission: `submission/level1.json` … `level6.json`, `final_bridge.json`, `saboteur.json`. When time was up the organizer took a snapshot of `submission/`; the file contents at the deadline count.
- Contestants could write any helper scripts (Python standard library). They could not modify `bridge.py`, `saboteur.py`, `bridge_sim.exe` or `levels/`, access the internet, spawn sub-agents, kill processes by name in bulk, or wait with long sleeps. Nobody could see anyone else's designs during the contest.
- After collection the organizer scored levels 1–6 and ran the 72 level 7 cross games.

## Contestants and tools

| Contestant | Tool | Access | Configured reasoning level |
|---|---|---|---|
| GPT-6.2 Sol | Codex desktop app | not recorded in the contest configuration | highest |
| Claude Opus 5.5 | Claude Code | not recorded in the contest configuration | max |
| GPT-6 Astra | Codex CLI | not recorded in the contest configuration | max (`model_reasoning_effort`) |
| Gemini 4 Argon | OpenCode | Google Vertex AI | high (`thinkingLevel`) |
| Grok 4.7 | Grok Build | not recorded in the contest configuration | xhigh |
| Kimi K3.1 Pro | Kimi Code | Moonshot official | max |
| Tencent Hunyuan Hy4 | OpenCode | OpenRouter, pinned to Tencent as provider | xhigh |
| Doubao Seed Evolving | OpenCode | Volcengine Ark | high |
| DeepSeek V4.1 Flash | dsh | DeepSeek official | max |

Reasoning levels are the launch-configuration settings, aimed at each tool's highest available level; they are **not an independent verification of the parameters of every actual request**, and same-named levels in different tools are not equivalent. Tool versions were not recorded per contestant and are not back-filled from the versions on the machine at release time. Only public service names are listed; no accounts, credentials, project identifiers or proxy settings are published.

## Environment

- All nine contestants ran on the same Windows computer, with physics computed by the `bridge_sim.exe` shipped in the package. The engine is deterministic: the same design gives the same result at any time in any directory, so local test results during the contest were the official results.
- This repository's engine is built from source and is byte-identical to the contest's exe (see [README_en.md](README_en.md#differences-from-the-contest)).

## Scoring

See [README_en.md](README_en.md#rules-at-a-glance). Official scoring of level 7: the standard convoy all arriving in time and intact = the bridge holds; otherwise = breached, credited to the saboteur's side. All results can be recomputed and checked with `python tournament/score.py`.

## Limits of interpretation

- Each model ran once; a single run involves chance. Tools, access channels and reasoning levels all affect the results.
- Level scores use the organizer's reference solution as 100; it is not a theoretical optimum, and exceeding 100 only means being cheaper than the reference.
- Each level 7 pairing was played once; saboteurs are not compared with each other directly, nor are bridges.
