[简体中文](README.md)

# Reproducing the official results

Requires Python 3.9+ and g++; no third-party Python packages.

```text
python build_engine.py          compile the physics core → workspace/bridge_sim.exe
python tournament/score.py      recompute all results and check them against results/
```

What `score.py` does:

1. Runs the 6 reference solutions in [reference/](../reference/), checks that all scenarios pass, and takes their costs as reference costs.
2. Runs the nine contestants' level 1–6 designs in [designs/](../designs/README_en.md) on every public scenario of the level (`workspace/levels/N/sample*.json`). A level is passed only if all scenarios pass; level score = round(100 × reference cost ÷ cost, 2).
3. Level 7: each contestant's `final_bridge.json` × the other 8 contestants' `saboteur.json`, 72 games, simulated with `saboteur.duel` from the contestant package. **Official scoring**: if all 4 standard vehicles arrive in time and intact in that game, the bridge holds; otherwise it is breached.
4. Computes the level 1–6 average, level 7 score and total, prints the 72-game matrix and the overall table, then compares item by item with [results/scores.json](../results/scores.json) (level scores, costs, scenarios passed, file SHA-256, totals) and [results/duels.json](../results/duels.json) (status and time of every vehicle in every game). If everything matches it prints "核对通过" (check passed) and exits with 0.

`--levels-only` computes levels 1–6 only; `--workers N` sets the number of worker processes. `score.py` sets `BRIDGE_NO_ARCHIVE=1` and writes no archive into the current directory.

**Local duels versus official scoring**: the verdict printed by `python bridge.py duel bridge.json saboteur.json` follows section 3 of [RULES_决赛.md](../workspace/RULES_决赛.md) ([translation](../workspace/RULES_final_en.md)) step by step; in particular, when the saboteur does not arrive intact by 12 s, it re-runs the convoy "without a saboteur" to decide. Official scoring only looks at whether the standard convoy all arrived in the game with the saboteur. In 12 of the 72 games the two differ (the saboteur itself did not arrive in time, but the convoy did not all arrive either); those 12 are counted as breached. The per-vehicle results are identical either way and are shown directly in the `duel` output.

## Single levels and games

```text
cd workspace
python bridge.py run 5 ../designs/sol62/level5.json
python bridge.py run 5 ../designs/sol62/level5.json --no-archive -q
python bridge.py duel ../designs/kimi/final_bridge.json ../designs/opus/saboteur.json --html duel.html
```

Exit codes of `run`: 0 all scenarios passed, 1 invalid design, 2 bad arguments, 3 some scenario failed.

## Replays

`python tournament/replays.py` regenerates the 9 replays in [replays/](../replays/README_en.md); `--all <dir>` generates all of them.

## Engine version

The contest's `bridge_sim.exe` has sha256 `4186445eec8989b344126c6e60eb8df8de0a3ca6543f83d06868a623c87209e7`. Running `build_engine.py` with GCC 16.2.0 from w64devkit (`-O2 -std=c++17 -static -municode`) produces a byte-identical file, and `build_engine.py` reports whether it matches. Other compilers produce different bytes; the engine only uses arithmetic, `sqrt` and `fabs` (do not add `-ffast-math`), and `score.py` is the check for whether results match.
