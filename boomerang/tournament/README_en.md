[简体中文](README.md)

# Scoring and reproduction

## Recompute the individual ranking with one command

```text
python tournament/score.py
```

Run it in `boomerang/`. It needs only Python 3.9+, no compiler and no third-party packages, and takes about 20 seconds. It reads the 32,904 per-game results in [results/games.jsonl.gz](../results/README_en.md), converts each game into scores exactly as at the event (one-on-one: the better placing wins; four-player: placing points 3/2/1/0, ties share, plus 1 per kill), fits them with the event's ranking program `rating.py`, and checks every value against `results/海选成绩.json`. The last line of a real run:

```text
核对通过：13 位选手的实力值、区间、名次和误差判断与 results/海选成绩.json 完全一致。
```

(check passed: ratings, intervals, ranks and error judgements of all 13 match.) `--quick` skips the resampling and checks only ratings and ranks, in 1 second.

## Ranking method

- One-on-one rating: all one-on-one games, win 1, draw 0.5, loss 0, Bradley–Terry fit.
- Four-player rating: within each game the four players are compared pairwise by that game's score (higher wins, equal draws), 6 comparisons per game, fitted the same way.
- The fit uses `bt.py` (the implementation from episode 8, soccer): one virtual draw per pair that met, scale 400/ln10 (400 points ≈ 10 to 1), the average of the thirteen set to 0.
- Individual rating = average of the two; higher ranks first; ties go to the higher one-on-one rating, then a fixed lot.
- Intervals: one-on-one and four-player games are each resampled by whole maps (same number of maps), both are refitted and averaged each time, 2,000 times, 2.5%–97.5% quantiles. When the interval of the difference between two neighbouring ranks contains 0, the pair is marked "within the error"; the ranks stand and there is no play-off.

This is written in `rating.py`'s docstring and matches section 11 of the player pack's [RULES.md](../workspace/RULES.md).

## Files

| File | Contents |
|---|---|
| `rating.py`, `bt.py` | The ranking program from the event, unchanged. `main()` at the end of `rating.py` calls the organizer's unpublished operations script `host.py`; reproduction does not need it — use `score.py` |
| `score.py` | Recompute from per-game results and check (written for the open-source release) |
| `rerun.py` | Regenerate the schedule and replay games (adapted from the event's tournament script for the release: only paths and output locations changed; schedule generation is line-for-line the same) |
| `hidden/` | The six maps kept secret during the event: one-on-one / four-player `icefloes`, `ruins`, `skybridge`, `stonecircle`, and 4v4 `oldtown`, `forest`. The four public maps are in `workspace/maps/` |

The community challenge (`../challenge.py`) plays your program against the thirteen on the six qualifier maps and keeps their ratings from `results/海选成绩.json` fixed; see [../README_en.md](../README_en.md).

## Replaying games

Build the referee first: `python build_engine.py` (see [../README_en.md](../README_en.md)). Then:

```text
python tournament/rerun.py --schedule-only   regenerate the schedule and check it game by game against results/schedule.json.gz (seconds; identical in practice)
python tournament/rerun.py --limit 600       replay 600 evenly spaced games and report the share with the same placings as at the event (minutes)
python tournament/rerun.py                   replay all 32,904 games and rank them (about 15–20 minutes on 16 threads), written to rerun/
```

The rules engine has no randomness, but several contestants' programs size their search by the clock, so a replay of the same game can differ when the machine is more or less busy. In a test of 120 replayed games, 62 (52%) had exactly the same placings as at the event; of the 117 games replayed for the video, 45 matched. A full replay is therefore a new sample: ratings move by a few points and close pairs may swap. The official results are the event's per-game results in `results/`.

At the event, 13,392 games were played on 16 threads; the other 19,512 were played on 11-16 while the machine was also running six control players doing the task (see [CONDITIONS_en.md](../CONDITIONS_en.md)).
