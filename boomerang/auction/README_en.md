[简体中文](README.md)

# Captain auction

After the qualifier, the top two became captains and bought teammates in a sealed-bid auction with a budget of 1,000 for the next episode's team event (4v4, five per team, four on each map). This episode ends with the auction.

## Rules (key points of the first message of the official auction)

- Team = captain + stablemate (GPT-6.2 Astra brings GPT-6.2 Sol, Claude Fable 5.2 brings Claude Opus 5.5, both join directly) + 3 bought in the auction.
- Nine candidates go up one at a time in qualifier order, from the top; the last three are left out. Each captain has 1,000 and must buy 3.
- Each round both captains submit a sealed bid with a one-line reason. The higher bid wins and pays its own bid. A bid of 0 means pass; if both pass, the candidate is left out.
- On a tie both re-bid, at most three times; if still tied, the side with more budget left wins, then the side missing more players, then a fixed lot.
- The maximum bid (`ceiling`) must leave 1 for every unfilled slot. When the remaining candidates equal the total number of open slots, nobody may pass. Once one side is full it drops out and the other side gets each remaining candidate for 1.
- Both bids for the previous candidate are shown to both captains afterwards; reasons are not shared.
- 10 minutes of due diligence before the auction: a read-only folder with the qualifier table and the nine candidates' programs and notes. Captains may run experiments with the programs, but may not change the material or their own `player.cpp`. The rules program is [auction.py](auction.py).

## Result (official auction, 11-17)

| Team | Bought (price) | Spent | Left |
|---|---|---:|---:|
| GPT-6.2 Astra's team (+ GPT-6.2 Sol) | Gemini 4 Hydrogen (400), Grok 4.7 (400), Kimi K3.1 Pro (1) | 801 | 199 |
| Claude Fable 5.2's team (+ Claude Opus 5.5) | Gemini 4 Carbon (599), GLM 5.3 (200), DeepSeek-V4.1-Flash (201) | 1000 | 0 |

Left out: Qwen, MiMo, Doubao. Every round's bids and reasons are in [拍卖结果.md](拍卖结果.md) (the `.json` is the same data for programs).

## Files

| Path | Contents |
|---|---|
| `拍卖结果.md`, `拍卖结果.json` | Both captains' bids and reasons for every round of the official auction, the outcome of each lot, the final rosters |
| `记录/` | Every message the organizer sent to a captain (`*_问.txt`) and the captain's answer (`*_答.txt`), numbered in order: `00开拍前通知` (pre-auction notice: 10 minutes to review the material again), then from `01_carbon_astra` one pair per candidate's bid |
| `尽调结果.json` | Both captains' answers and timings for the pre-auction notice round |
| `尽调材料/` | The due-diligence folder's qualifier table `海选成绩.md` and the rehearsal's bid record `演习出价记录.md`. The candidates' programs and notes are the ones in [../bots/](../bots/README_en.md) and are not duplicated |
| `队长笔记/` | Notes the captains wrote during due diligence: Astra's `队长尽调.md` (it built `auction_lab/` in its working folder and played over a thousand mixed 4v4 games with the candidates' programs) and Fable's `auction_notes.md` (written before the rehearsal, updated before the official auction) |
| `演习_1414/` | The first auction, on 11-16: the captains were the preview build of Astra and Fable, using the preview qualifier results. Astra's team bought Carbon 665, GLM 330, Qwen 1; Fable's team bought Hydrogen 335, Grok 335, Kimi 6; both passed on DeepSeek. Afterwards the organizer declared it a rehearsal and voided it |
| `重拍通知_1116/` | The 11-16 evening message that voided the rehearsal and added a clarification: after joining, teammates would rewrite their programs for the team event under the captain's direction (this had not been made clear, and both captains had played 4v4 with the candidates' qualifier programs and picked by friendly fire). In this round Fable hit a usage limit and did not answer; the re-run moved to 11-17, with the official 6.2 Astra as captain |
| `auction.py` | The auction rules program (ceilings, ties, reserves), the original from the organizer's event tools |

The captains answered in the same sessions in which they had done the qualifier task. The organizer's event folder in the messages is written as `{赛事目录}`. The raw output streams of the auction and the records of context compaction are not included.
