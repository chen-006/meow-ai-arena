[中文版](scoring.md)

# Scoring methods

This page covers episodes 1–3. Scoring for later episodes is described in each episode's README and `results/`.

All three episodes use the Bradley–Terry (BT) model: each game is broken into pairwise results to fit a strength parameter for each contestant. They differ in how comparisons are formed and how strengths are converted to ratings:

| | Episode 1 · Territory | Episode 2 · Bomberman | Episode 3 · Capture the flag |
|---|---|---|---|
| Result within a game | Rank by territory cell count | Game score = survival score + 0.5 × kill share | Rank by flags delivered |
| Pairwise comparisons | Use 1v1 results directly; split each 4-player game into 6 pairs | Compare ranks; a tie counts as half a win | Compare ranks; a tie counts as half a win |
| Weighting player counts | Fit 1v1 and free-for-all separately, then give each half the total weight | Equal total weight for each player count from 2–10 | Equal total weight for each player count from 2–15 |
| Who is at 0 | Greedy baseline | Public baseline | Public baseline |
| Scale | Strongest model in each format = 100; values below baseline become 0; rescale the combined score so first place = 100 | 400 × log10(strength / baseline strength), uncapped | 400 × log10(strength / baseline strength), uncapped |
| Script | [land/tournament/rate.py](../land/tournament/rate.py) | [bomberman/tournament/bt.py](../bomberman/tournament/bt.py) | [arena/bt.py](../arena/bt.py) |

“400 × log10” means that being 10 times as strong as an opponent (an expected win rate of about 91%) is worth +400 points; 3 times as strong is about +190; equal strength is 0.

- Intervals: episodes 1 and 3 use bootstrap resampling for 95% intervals; episode 2 does not generate intervals.
- One virtual draw is added for each pair that has played, preventing estimates from diverging when a player wins or loses every game against an opponent.
- Strength estimates depend on the opponent pool and mix of player counts. Even within one episode, changing the opponent pool changes the numbers (the challenge experiments in [version-evolution_en.md](version-evolution_en.md) are an example). Only scores within the same table are comparable.
