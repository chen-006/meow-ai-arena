# Proposed game: RMUC 2026 robot team battle

Each model writes one Python program controlling six robots in a physical arena. This is a runnable proposal based on [Arena-1](https://github.com/RM-Arena/Arena-1), not an announced episode or a new official model leaderboard.

Both sides use the identical locked roster: hero model **1**, engineer **201**, infantry slots **3 / 4 / 7 all model 305 (serial-leg infantry)**, and aerial **601**. The episode uses **2026**, not 2027 rules. Base destruction ends a match early; otherwise the 420-second referee first compares base HP. See the [rules](workspace/RULES.md) and [full mechanics](workspace/MECHANICS.md) in Chinese.

Hosts need Python 3.10+, stable Rust/Cargo and Docker. Policy containers use pinned Python 3.12. No web frontend or Python packages are needed by the host scripts.

From the repository root:

```sh
python rmuc2026/build.py --docker
python rmuc2026/arena.py --duration 30 --replay rmuc2026/practice/opening.html
python rmuc2026/challenge.py rmuc2026/workspace/strategy.py --output rmuc2026/practice/eval-001
python rmuc2026/tournament.py rmuc2026/bots/assault.py rmuc2026/bots/guard.py --output rmuc2026/practice/round-robin
python -B -m unittest discover -s rmuc2026 -p 'test*.py' -v
RMUC_DOCKER_TESTS=1 python -B -m unittest discover -s rmuc2026 -p 'test*.py' -v
```

Write `act(observation, state)` in the provided template. Read-only, non-root, network-free Docker policies receive 1 CPU, 1 GiB RAM and a 200 ms wall-clock budget per decision. Policies run at 10 Hz in simulation time. Recoverable missed decisions remain in-play; disabled or uninitializable policies forfeit, with both sides disabled scored as a draw. Host/referee failures abort evaluation rather than silently creating a draw. All scored technical losses stay in the ledger.

Round robins freeze sources before running, pair both sides for every seed, score win/draw/loss as 1/0.5/0 and share ranks for ties. Short practices and explicit `--trusted-local` runs are always unranked. Existing output directories cannot be overwritten or resumed automatically.

Syntax-invalid entries remain in the round robin: preserve their source snapshots, hashes and validation errors, score all their scheduled forfeits, and continue healthy pairings. Two invalid entries draw by double forfeit. These records have `simulation_started: false` and diagnostic-only HTML pages. Source I/O and host-runtime failures still abort evaluation. Response timeouts disable a policy after three consecutive misses; exceptions or invalid output disable it on the first occurrence, and an input-write timeout is immediately terminal.

[Example results and silent HTML replays](results/README.md) are from the actual scored games, sampled once per simulation second. The offline 2D viewer uses no external resources and is not a full frame-by-frame verification trace. Development timing is organizer-managed; use the same [prompt](prompt.md), template, SDK and public opponents for all contestants.

The [source manifest](SOURCE.json) pins the unmodified physics referee and records both upstream and patched hashes for the runner/worker transport fixes. New results include `runtime_revision`. Regression tests cover nonreading policies, failed container entrypoints and colliding output paths; a read-only Ubuntu CI builds the referee and sandbox and runs the full suite. The two example opponents are an attributed historical baseline and a defensive variant, not new model contest entries. [Conditions](CONDITIONS.md) record the actual validation and limitations.

This subtree retains the [Arena-1 Non-Commercial Learning License](LICENSE), independently of the parent repository's MIT license. See [third-party notices](THIRD_PARTY_NOTICES.md). **No music, audio, videos, GLB assets, textures or original rulebook PDFs are included or downloaded.** This is an independent simulation, not an official RoboMaster event.
