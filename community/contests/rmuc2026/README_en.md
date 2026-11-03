# Community contest: RMUC 2026 robot 6v6

Each model writes one Python program controlling six robots in a physical arena. This is a **community-created, unofficial contest maintained by RM-Arena**, based on [Arena-1](https://github.com/RM-Arena/Arena-1). It is independent of the AI Algorithm Arena show's official episodes and of the RoboMaster organizers. The directory contains its own referee, rules, example entrants and local tools.

Both sides use the identical locked roster: hero model **1**, engineer **201**, infantry slots **3 / 4 / 7 all model 305 (serial-leg infantry)**, and aerial **601**. The contest uses **2026**, not 2027 rules. Base destruction ends a match early; otherwise the 420-second referee first compares base HP. See the [rules](workspace/RULES.md) and [full mechanics](workspace/MECHANICS.md) in Chinese.

Hosts need **Python 3.12+**, stable Rust/Cargo and Docker. Strategy code must target the pinned **Python 3.12** container even on a newer host. All contest entrypoints reject older hosts before validating submissions or creating outputs, so valid Python 3.12 syntax is not scored as a contestant error. No web frontend or Python packages are needed by the host scripts.

First enter `community/contests/rmuc2026/` in the repository, or the output directory of an exported player pack. Run the following commands there:

```sh
python build.py --docker
python arena.py --duration 30 --output practice/opening.json --replay practice/opening.html
python challenge.py workspace/strategy.py --output practice/eval-001
python tournament.py bots/assault.py bots/guard.py --output practice/round-robin
python make_player_pack.py --output player-pack
python -B -m unittest discover -s . -p 'test*.py' -v
RMUC_DOCKER_TESTS=1 python -B -m unittest discover -s . -p 'test*.py' -v
```

Write `act(observation, state)` in the provided template. Read-only, non-root, network-free Docker policies receive 1 CPU, 1 GiB RAM and a 200 ms wall-clock budget per decision. Policies run at 10 Hz in simulation time. Recoverable missed decisions remain in-play; disabled or uninitializable policies forfeit, with both sides disabled scored as a draw. Host/referee failures abort evaluation rather than silently creating a draw. All scored technical losses stay in the ledger.

Each contestant is initialized once: the same ready process enters the battle, with verified source hash, seed, backend and budget. There is no second import that can fail nondeterministically. Failed contestants forfeit; host or battle construction failures also clean up any already-ready peer.

Round robins freeze sources before running, pair both sides for every seed, score win/draw/loss as 1/0.5/0 and share ranks for ties. Short practices and explicit `--trusted-local` runs are always unranked. Existing output directories cannot be overwritten or resumed automatically. Team filenames must be distinct after NFC normalization, case folding and trimming trailing spaces/dots; this portable rule rejects collisions before creating outputs.

Syntax-invalid entries remain in the round robin: preserve their source snapshots, hashes and validation errors, score all their scheduled forfeits, and continue healthy pairings. Two invalid entries draw by double forfeit. These records have `simulation_started: false` and diagnostic-only HTML pages. Source I/O and host-runtime failures still abort evaluation. Response timeouts disable a policy after three consecutive misses; exceptions or invalid output disable it on the first occurrence, and an input-write timeout is immediately terminal.

[Example results and silent HTML replays](results/README.md) are from the actual scored games, sampled once per simulation second. The offline 2D viewer uses no external resources and is not a full frame-by-frame verification trace. Development timing is organizer-managed; use the same [prompt](prompt.md), template, SDK and public opponents for all contestants.

The [source manifest](SOURCE.json) pins the unmodified physics referee and records both upstream and patched hashes for the runner/worker transport and process-reuse fixes. New results include `runtime_revision`. Regression tests run locally; this contest does not add a repository workflow. The exported player pack is a standalone directory with its own scripts, runtime, rules and examples. The two example opponents are an attributed historical baseline and a defensive variant. [Conditions](CONDITIONS.md) record validation and limitations.

This subtree retains the [Arena-1 Non-Commercial Learning License](LICENSE), independently of the parent repository's MIT license. See [third-party notices](THIRD_PARTY_NOTICES.md). **No music, audio, videos, GLB assets, textures or original rulebook PDFs are included or downloaded.** This is an independent simulation, not an official RoboMaster event.
