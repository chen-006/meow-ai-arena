[简体中文](README.md)

# Schedules, scoring and reproduction

| File | Contents |
|---|---|
| `halftime.json.gz`, `final.json.gz`, `individual.json.gz` | Frozen schedules: seeds, seed indices, sides, kickoff, within-team order and executable hashes; paths sanitized, gzip-compressed |
| `bt.py` | Original, unchanged Bradley–Terry and bootstrap implementation |
| `score.py` | New archive entry point: recompute points and optionally call original BT code |
| `reproduce.py` | New local entry point: replay one game or an event with archived seats and source choices |

Run from `soccer/`:

```text
python build_reference.py
python tournament/score.py final
python tournament/score.py individual --bt
python tournament/reproduce.py final --game team1-team2-s0-1 --out out/example
python tournament/reproduce.py final --all --out out/final
```

The output directory must not already exist, preventing old/new result mixing. `--all` supports all three phases and runs games sequentially; all 8,280 games may take considerable time. Original 10 CPU ms limits are used. The default is `--sandbox none` (no identity isolation; timeout, memory and child-process limits still apply); add `--sandbox appcontainer` to rerun under the event's Windows sandbox. A start failure stops the run, retains diagnostic replays and is not counted as a draw.

The replay entry point uses recorded `seats`, not numeric seed parity. Both seats of a reference team use its same bot. Seeds, seats and source choices are reproducible; CPU timing and compiler differences mean scores and executable hashes need not be identical across machines.

The generic BT tie-grouping helpers in `bt.py` did not determine this event's ranks. Ranking uses score rate, goal difference, goals scored. No combined overall score is emitted.

The original `host.py` combined tournament evaluation with model launching, timing, sealing, accounts and team-chat services. It is excluded; this folder provides frozen data, the original standalone BT implementation and small new local entry points.
