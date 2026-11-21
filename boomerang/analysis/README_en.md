[中文](README.md)

# Episode 11 analysis notes (Chinese)

Analysis written while making episode 11: how each contestant's program thinks, where it is strong and weak, and how the two captains did their sums in the auction. It is published to help viewers understand the contestants' approaches.

**Caveat**: these notes were compiled by AI (Gemini, Claude) from the contestants' code and controlled experiments, then checked claim by claim by GPT (GPT-6.2 Sol); every bot analysis ends with that check table ("holds / partly holds / does not hold"). Errors may remain. **The contestants' code ([`../bots/`](../bots/README.md)) and the game data ([`../results/`](../results/README.md)) are authoritative.**

| File | Contents |
|---|---|
| `bot解析_<contestant>.md` (14 files) | One per official contestant plus the previous-generation GPT-6 Astra: the playing style in one paragraph, architecture, latency compensation / prediction / search / evaluation / throwing and blocking / movement, the cleverest designs, the most costly weaknesses, a comparison with one named opponent, and GPT's claim-by-claim check |
| `拍卖环节分析.md` | The two captains' drafting strategies, what they did during due diligence, their pre-auction reasoning versus what happened lot by lot, and four decisive moments |
| `全场分析总汇.md` | One-page overview, themes shared across contestants, and a numbers appendix for the official board and the 21-player all-in board |
| [`data/`](data/) | Statistics tables (CSV, UTF-8, Chinese headers) on the development process, cost, playing style and win rates; the charts in the video were drawn from them. `01_花费与总分.csv` is the **cost table**: tokens used by each contestant priced at each vendor's official API rates in CNY (`参考费用元`, with lower/upper bounds where uncertain), the unit prices, links to the price pages, exchange rate and date. Time figures are estimates counted from the process records (not included); source, evidence-path and contest-operations columns were removed |

- Line numbers (`L123`) refer to each contestant's `player.cpp` in `../bots/`.
- Seat names follow the organizer's lab machine: `astra62` = `astra`, `sol62` = `sol`, `gemini_carbon` = `carbon`, `gemini_hydrogen` = `hydrogen`, `deepseek_dsh` = `deepseek`, `kimi2` = `kimi`; `astra6` is the previous-generation GPT-6 Astra in the appendix.
- "Official" numbers come from the tournament's per-game results in `../results/`. "Reruns" and single-change comparisons were extra games played on the organizer's machine while writing the analysis; their per-game results, the patches for the modified programs and the batch scripts are in [`experiments/`](experiments/) (with reproduction steps); the **replays are not included**, and "主办方本地文件，未收录" in the notes refers to such uncollected items (replays, intermediate files read from replays). They are a different batch from the tournament, so do not mix the numbers.
- "早先一轮分析" (an earlier round of analysis) is not included; where it conflicts with these notes, these notes prevail.
- The notes were internal working documents. Production material (suggestions for the video, on-screen lists), internal paths and the organizer's internal arrangements were removed; the analysis itself was not rewritten. No raw contestant session logs are included; quotes in the auction analysis come from the Q&A and captain notes in [`../auction/`](../auction/README.md) and the contestants' `notes.md`.
