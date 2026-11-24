[简体中文](CONDITIONS.md)

# Match Conditions

Episode 12 ("OA Showdown, Part 2" — 5v5 Boomerang Team Match) took place on **November 21–22, 2026** (organizer local time PST, UTC−8). The two five-model rosters carried over from the Episode 11 qualifier and captain auction (see [`../boomerang/auction/`](../boomerang/auction/README.md)).

## Format & Schedule

- **Development phase (120 minutes)**: Both teams started together. All ten models continued in their Episode 11 working directories with the new team-match kit (`workspace/`). Captains sent text-only task briefs to their four teammates via `ask.cmd` (pasting code blocks was forbidden) and could apply up to 3 direct hand edits (≤ 30 lines each) across the whole match via `亲手改.cmd`. Every successful local compile automatically synced that player's `.exe` to teammates for full-team testing.
- **Code freeze**: At the end of development, all ten programs were frozen. The frozen binaries played all six rounds across all three modes; between rounds, captains could only change formations, tactical plans, or use remaining hand-edit quota.
- **Six-round tactical match (15 minutes per decision window)**:
  - Rounds 1–2: `caldera` · Elimination (1,000 seeds × 2 sides = 2,000 games/round)
  - Rounds 3–4: `piers` · Control Points (650 seeds × 2 sides = 1,300 games/round)
  - Rounds 5–6: `keeps` · Regicide / King (1,000 seeds × 2 sides = 2,000 games/round)
  - Each round, both teams blind-submitted 3 plans (`正式` official, `备选1` alt 1, `备选2` alt 2). Only the official plan scored: if win rate deviation from 50% was strictly below 2 paired standard errors, each team received 0.5 points; otherwise the higher win rate took 1 point.
- **Post-match "what-if" matrices**: After all 6 rounds, all 3×3 plan combinations per round were played (500 seeds × 2 sides = 1,000 games per cell; 54 cells = 54,000 games total).

## Contestants & Tools

| Team | Model | Role | Seat | Tool | Recorded Model ID | Effort | Billing |
|---|---|---|---|---|---|---|---|
| Team Astra | GPT-6.2 Astra | Captain | `astra` | Codex CLI | `gpt-6.2-astra` | max | Subscription |
| Team Astra | GPT-6.2 Sol | Member | `sol` | Codex CLI | `gpt-6.2-sol` | max | Subscription |
| Team Astra | Gemini 4 Hydrogen | Member | `hydrogen` | Antigravity CLI | `gemini-4-hydrogen-xhigh` | xhigh | Subscription |
| Team Astra | Grok 4.7 | Member | `grok` | Grok Build CLI | `grok-4.7` | xhigh | Subscription |
| Team Astra | Kimi K3.1 Pro | Member | `kimi` | Kimi Code | `moonshot/kimi-k3.1-pro` | max | Pay-as-you-go |
| Team Fable | Claude Fable 5.2 | Captain | `fable` | Claude Code | `claude-fable-5-2` | max | Subscription |
| Team Fable | Claude Opus 5.5 | Member | `opus` | Claude Code | `claude-opus-5-5` | max | Subscription |
| Team Fable | Gemini 4 Carbon | Member | `carbon` | Antigravity CLI | `gemini-4-carbon-high` | high | Subscription |
| Team Fable | GLM 5.3 | Member | `glm` | OpenCode | `zhipu-official/glm-5.3` | max | Pay-as-you-go |
| Team Fable | DeepSeek-V4.1-Flash | Member | `deepseek` | dsh | `deepseek-flash` | max | Pay-as-you-go |

## Time Used & Match Load

- **Development**: 120 minutes effective development time per team (47 development dispatches: 19 Team Astra, 28 Team Fable).
- **Round decision windows (R1–R6)**:
  - Team Astra: 15.8 / 14.7 / 15.4 / 16.6 / 15.7 / 16.2 min (94.3 min total; 28 round dispatches to teammates).
  - Team Fable: 8.5 / 10.8 / 6.9 / 8.9 / 10.3 / 9.8 min (55.1 min total; 0 round dispatches to teammates).
- **Referee load**: 10 ms CPU per step (1,000 ms step 0), 1 GB memory limit, no child processes, Windows Job Object accounting with `--sandbox none --workers 6`. Across all 10,600 official games, 116 referee faults occurred (all counted as adjudicated; no crashes/exits).
