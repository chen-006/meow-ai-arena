[简体中文](README.md)

# Standings and per-battle results

All files are the output of the evaluation and scoring stages; values are unchanged. After the Claude Fable 5.2 seat switched to the third-party rerun record, the organizer re-evaluated the nine submissions with the contest's own pipeline and rebuilt the standings (all 945 battles replayed); these files are that output. Battles without Fable give the same results as before apart from timing fields. See [CONDITIONS_en.md](../CONDITIONS_en.md#claude-fable-52-the-third-party-rerun-counts).

| File | Contents |
|---|---|
| [总榜.md](总榜.md), [总榜.json](总榜.json) | Overall standings: total = levels × 60% + versus × 40% |
| [闯关榜.md](闯关榜.md), [闯关榜.json](闯关榜.json) | Level scores and costs, cheapest cost and organizer's reference per level, per-level details (result, end reason, time) |
| [对抗榜.md](对抗榜.md), [对抗榜.json](对抗榜.json) | 1v1 and free-for-all results, 1v1 head-to-head table |
| [eval/检查.json](eval/检查.json) | Check results for each contestant's seven deployments (valid or not, cost) and the commander program's compile result |
| [eval/对局索引.json](eval/对局索引.json) | Schedule: 945 battles with map, seats, deployments and programs used |
| [eval/results/](eval/results/) | The 945 per-battle results, named by battle (`L<level>_<seat>`, `D<map>_<seat 0>_<seat 1>`, `M<group>_<seating>`) |
| [eval/程序运行情况.json](eval/程序运行情况.json) | Commander program timeouts and crashes per contestant (all zero) |
| [eval/编译器.json](eval/编译器.json) | Compiler version and flags |
| [eval/引擎错误场次.json](eval/引擎错误场次.json) | Battles that hit an engine timeout and have no result (none) |

Seat names (`sol`, `gemini_carbon`, …) are mapped to contestants in [CONDITIONS_en.md](../CONDITIONS_en.md#contestants-and-tools).

**Paths**: the JSON files originally held absolute paths on the organizer's machine; they now hold paths relative to the contest directory: `kit/` is the contestant package (this repository's `workspace/`), `sealed/<seat>/submission/` the hand-ins (`bots/<folder>/` here), `eval/build/<seat>/commander.exe` the compiled commander, `eval/replays/<battle>.wrp` the replay (only 63 are in the repository; see [replays/](../replays/README_en.md)). The compiler path reads `g++`. `tournament/score.py` recreates this layout under `rerun/` when reproducing.

**Per-battle fields** (details in [engine/接口说明.md](../engine/接口说明.md), Chinese): `winner` the winning seat (-1 = draw, or level failed), `reason` how it ended, `passed` whether a level was passed, `secs` battle length, `digest` a digest of the whole simulation (the same inputs always give the same digest), `teams` each side's starting and remaining units, costs and value destroyed (free-for-alls add placement points, kill points and score), `seats` each seat's deployment and order counts, `commanders` the commander programs' health. CPU timings such as `first_ms` and `avg_ms` differ on every run, as do the engine's own `stats.t_phys` and `stats.t_logic`.
