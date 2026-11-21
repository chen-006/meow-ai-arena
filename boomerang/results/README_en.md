[简体中文](README.md)

# Results and per-game data (official qualifier)

| File | Contents |
|---|---|
| [海选榜.md](海选榜.md) | The individual ranking and one-on-one head-to-head records (each cell is the row player's win-draw-loss against the column player, 228 games per pair). This is the copy the organizer generated at the event and put in the auction's due-diligence material, unchanged. Chinese |
| [海选成绩.json](海选成绩.json) | The ranking program's full output, unchanged: each player's individual rating, one-on-one and four-player ratings with 95% intervals, rank and rank interval (`players`), rank order (`ranking`), the gap between neighbouring ranks and whether it is within the error (`gaps`), each player's step time-outs, terminations and longest step in ms (`faults`), the draw seed and `lot` |
| `games.jsonl.gz` | 32,904 per-game results, one game per line: `id` (game number in the schedule), `mode` (0 one-on-one, 1 four-player), `map`, `seed`, `names` (seat names in seat order), `steps` (number of steps played, 0.05 s each), `players` (each player's `rank`, `kills`, `alive` (survived to the end), `cause` of elimination: `dart` hit by a boomerang, `slash` cut, `fall` fell off, `none` not eliminated; `faults` step time-outs, `status`, `max_ms` longest step in CPU ms) |
| `schedule.json.gz` | The schedule: draw seed, maps, seed table, four-player bye order, how often each pair shared a table, and every game (`games`, numbered one-to-one with `games.jsonl.gz`); `mapfile` is a path inside the repository |

Reading it:

```python
import gzip, json
rows = [json.loads(l) for l in gzip.open('results/games.jsonl.gz', 'rt', encoding='utf-8')]
```

Seat names map to contestants in [../README_en.md](../README_en.md#players). To recompute the ranking: `python tournament/score.py`; see [../tournament/README_en.md](../tournament/README_en.md).

No per-game replays were saved at this event; [../replays/](../replays/README_en.md) are 117 games replayed for the video.
