[中文版](README.md)

# Example submissions

These six folders demonstrate the submission format. They do not enter the community leaderboard and are not anyone's actual submissions.

| Folder | Description |
|---|---|
| [ctf/chen-006__stand-still/](ctf/chen-006__stand-still/) | Bot submission: `bot.cpp` plus `meta.json`. The code is the episode-3 package template; all three roles stand still |
| [tank/chen-006__starter/](tank/chen-006__starter/) | Tank submission: `bot.cpp` plus `meta.json`. An unchanged copy of the official episode-6 starter, defining only `init` and `decide`, using the official `tank.hpp` and `tank_main.cpp`, with no custom `main` |
| [tron/chen-006__starter/](tron/chen-006__starter/) | Tron submission: an unchanged copy of the official episode-7 starter template, implementing only `decide`, using the official `tron.hpp` and `tron_main.cpp`. Local compilation and evaluation require Windows |
| [soccer/chen-006__starter/](soccer/chen-006__starter/) | Soccer submission: the episode-8 player template copied unchanged to `bot.cpp`; it takes both seats. Windows required |
| [boomerang/chen-006__starter/](boomerang/chen-006__starter/) | Boomerang submission: the episode-11 `template.cpp` copied unchanged to `bot.cpp` (`#include "dart.hpp"`, `DART_MAIN`). Windows required |
| [warehouse/chen-006__kimi-k3-round1/](warehouse/chen-006__kimi-k3-round1/) | Warehouse submission: `src/` plus `meta.json`. Kimi K3's round-1 code, unchanged |

Try the local checker on them:

```bash
python community/check.py community/_example/ctf/chen-006__stand-still
python community/check.py community/_example/tank/chen-006__starter --skip-eval
python community/check.py community/_example/tank/chen-006__starter
python community/check.py community/_example/tron/chen-006__starter --skip-eval  # Windows
python community/check.py community/_example/tron/chen-006__starter              # Windows
python community/check.py community/_example/soccer/chen-006__starter             # Windows
python community/check.py community/_example/boomerang/chen-006__starter          # Windows
python community/check.py community/_example/warehouse/chen-006__kimi-k3-round1
```

Tank's default formal evaluation runs 320 games; `--full` runs 1280. `--skip-eval` only checks structure and compilation. Official final-tournament opponent ratings stay fixed; only the entrant's rating is fitted.

Tron's default formal evaluation runs 82 games; `--full` runs 328. Both keep the strong-opponent board's per-game-type reference ratings fixed, fit only the entrant, and weight the results 50/30/20. The sparring bot takes part in fitting but does not take a rank. `--skip-eval` still needs a Windows compilation environment.

For an actual submission, put the folder under `community/<episode>/`, rather than `community/_example/<episode>/`. See the [submission guide](../README_en.md) for the rules.
