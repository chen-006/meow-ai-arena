[简体中文](README.md)

# Engine and sparring bots

The rules engine of this episode is the single header [`workspace/dart.hpp`](../workspace/dart.hpp) and the referee is [`workspace/arena.cpp`](../workspace/arena.cpp); both are in the player pack, and contestants and referee use the same copy. This folder holds the source of the three sparring bots that were **handed out only as compiled programs** during the event:

| File | Contents |
|---|---|
| `sparring/weak.cpp` | Weak sparring bot |
| `sparring/mid.cpp` | Medium sparring bot |
| `sparring/strong.cpp` | Strong sparring bot: look-ahead that compensates for the reaction delay, a dozen or so candidate moves scored per step, blocking, dash wind-up; fixed amount of computation (no clock, deterministic results) |
| `sparring/说明.md`, `说明_踢飞新规则.md` | The organizer's notes from building the sparring bots (Chinese): approach, measured time per step, win rates strong vs medium and medium vs weak |

`python build_engine.py` (in `boomerang/`) compiles them into `workspace/bots/` and also builds `workspace/arena.exe`. With w64devkit's GCC 16.2.0 the results are byte-identical to the executables used at the event (checked for all four at release).

The notes mention `原型\bots\pro_core.hpp`, `选手包\` and `match_py.diff`; these are internal files from building the game and are not included. `strong.cpp` is a self-contained single file and does not need them.
