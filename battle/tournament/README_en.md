[简体中文](README.md)

# Scoring and reproduction

## Check the leaderboard with one command

```text
python tournament/score.py
```

Reads the 945 per-battle results and deployment checks in [results/eval/](../results/eval/), recomputes the overall, level and versus standings with the contest's own scoring script [scoring.py](scoring.py), and compares them with [results/](../results/README_en.md) (JSON item by item, Markdown character by character), printing "核对通过" (check passed). Takes seconds and needs only Python 3.9+, no compiler or simulator.

## Replay every battle from the submissions

```text
python build_engine.py
python tournament/score.py --rerun
```

1. `build_engine.py` compiles the simulator from [engine/src/](../engine/src/) into `workspace/war.exe` (needs Windows, g++, cmake, ninja; downloads Jolt Physics the first time; see [engine/README_en.md](../engine/README_en.md)).
2. `score.py --rerun` builds a contest directory under `rerun/` laid out exactly as during the contest (`kit/` = contestant package, `sealed/<seat>/submission/` = the files in `bots/`) and hands it to the evaluation stage of `scoring.py`: the nine commander programs are compiled with the same command as the package's `build.py`, `war.exe check` checks the 63 deployments, and `war.exe batch` plays all 945 battles.
3. Every battle is compared with `results/eval/results/` (except timing fields: the commanders' CPU timings `first_ms`, `avg_ms`, `max_ms`, `cpu_total_ms`, the engine's `t_phys` and `t_logic`, and the in-batch index `id`), then the standings are produced and checked.

Roughly 5–10 minutes on 8 threads, depending on the machine. Options: `--workers N` sets the thread count; `--replays` also saves all 945 replays (about 3 GB); `--keep` keeps the output under `rerun/` even when the check passes.

The simulator is deterministic: the same `war.exe` gives the same results on any machine. Two things can make a rerun differ from the official results:

- **A different compiler**: build the commanders and the simulator with GCC 16.2.0 (w64devkit), as in the contest; other compilers may optimise floating point differently. `build_engine.py` reports whether `war.exe` is byte-identical to the contest exe.
- **A much slower machine**: commander programs have a CPU time limit ([COMMANDER.md](../workspace/COMMANDER.md)); none timed out in the official evaluation, but on a machine that is too slow a program may, and that battle will then differ.

## Files

| File | Contents |
|---|---|
| [scoring.py](scoring.py) | The contest's evaluation and scoring code, unmodified. The `host.py` its docstring mentions is the organizer's unpublished operations script (launching contestants, timing, collecting); it is not needed here |
| [config.json](config.json) | Scoring config: only the fields `scoring.py` uses (reference costs, weights, versus maps, free-for-all seating, contestant order), with the contest's values |
| [score.py](score.py) | Reproduction script: lays out the directory, calls `scoring.py`, checks |

The schedule has no random seeds: one run per contestant per level; every pair plays each 1v1 map once in each seating; free-for-alls take every 4-player combination in the order of `config.json`, with the first player of each group in the south seat and the other three rotating through the remaining seats (`scoring.make_plan`).
