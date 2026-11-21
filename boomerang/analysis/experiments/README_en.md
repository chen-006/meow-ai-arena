# Exploration mini-experiments: reruns and single-change comparisons

The "reruns" and "single-change comparisons" quoted in the bot notes ([`../README_en.md`](../README_en.md)) are **not tournament games**. While writing the analysis, one thing in a program was changed (a branch switched off, a constant altered, a unit conversion added) and the result was compared with the original on the same maps, seeds and seat pairings. This directory holds the patches, the per-game results and the batch scripts. Do not mix these numbers with the official results in `../../results/`.

| Path | Content |
|---|---|
| `patches/<group>/<name>.diff` | 59 patches against the contestants' original `player.cpp` (the `--- a/` line names the original); `patches/index.json` is the table |
| `make_variant.py` | Applies a patch to the original and writes the modified `.cpp` (`--all DIR` builds all) |
| `tools/run_batch.py`, `tools/runner.py`, `tools/run_specs.py` | The three batch runners used at the time, now using the repository's contestant kit |
| `raw/<group>.zip` | Per-game results (JSON, one file per batch; replay paths removed) |
| `results_summary.json` | Aggregates accumulated by `run_batch.py` (key `tag\|map\|seeds`) |
| `readers/` | Small scripts used to read results and replays (death causes, kills, falls...). Reference only: many read replays that are not included and cannot run as is |

Reproduce: run `python build_engine.py` in `boomerang/` once, then `python make_variant.py patches/fable/fable_v_thr60.diff out.cpp` and `tools/runner.py` (see the Chinese README for the exact command). Replaying Astra 6.2 vs Fable (original and `fable_v_thr60`, 12 games each) reproduced all 12 `digest` values stored in `raw/fable.zip`. Programs that choose their search depth by wall-clock may differ game by game.

Not included: replays (re-run with `--replays DIR`), and an earlier round of ablations (about 20 repair variants for Fable, Opus, Carbon and Hydrogen, over 1 GB, mostly replays). The full table of modified programs and result files is in the Chinese [README.md](README.md).
