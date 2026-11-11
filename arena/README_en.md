[中文原文](README.md)

# arena · General-purpose game platform

General-purpose tooling used for the episode 3 (Capture the Flag) official tournament (the other episodes ship their own tournament scripts under their `tournament/`, separate from these): run individual games, create schedules, run tournaments and fit Bradley–Terry ratings. To run a tournament for a new game, provide a rules engine with a `Game` class implementing the interface documented at the start of `core.py`.

| File | Purpose |
|---|---|
| `core.py` | Launch player programs (`.cpp` automatically compiled and cached by source, `.py`, or executables), exchange turn commands within time limits, run a game and record its replay |
| `schedule.py` | Schedule maps for each player count, rotating every seat on each map to balance appearances and seat counts |
| `bt.py` | Fit Bradley–Terry ratings, baseline anchored at 0, equal weight per player count, intervals by resampling maps |
| `tournament.py` | Three tournament steps: `plan` (freeze schedule) → `run` (interruptible and resumable) → `rate` (produce results; can combine multiple tournament directories) |
| `clock.py`, `run.cmd`, `run.sh` | Timer and Python-launch scripts included in contestant packages |

From the repository root:

```bash
python arena/tournament.py plan  event_config.json  event_directory
python arena/tournament.py run   event_directory --workers 4
python arena/tournament.py rate  event_directory
```

Configuration fields are documented at the start of `tournament.py`. The files matching `ctf/tournament/formal-*.json` are real examples; see the [Capture the Flag tournament guide](../ctf/tournament/README_en.md).

Default turn limits are 2 seconds for the first turn and 50 ms thereafter. They are sensitive to machine load: programs per game × concurrent games should preferably stay below about 3/4 of the CPU thread count.
