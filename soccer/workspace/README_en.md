[简体中文](README.md)

# 2v2 soccer · base contestant package

You and a teammate each write one C++ player bot. Read the [full rules](RULES_en.md), the [team-event supplement](../packages/format_en.md), and the [second-half amendment](../prompt_en.md). The historical Chinese base README is unchanged. The opening prompt and supplement override its older copying permission.

| File | Purpose |
|---|---|
| RULES.md / RULES_en.md | Original rules / reference translation |
| template.cpp | Minimal legal starting bot |
| soccer.hpp | Exact C++17 engine used by the referee; may be included in bots |
| build.py | Source checks and original player compilation flags |
| match.py | Local team matches and diagnostics |
| replay.py | Events, states, observations, actions and teammate messages; optional PNG/GIF export |
| viewer.html | Offline viewer; drag a replay file into it |
| arena.cpp | Referee source; arena.exe is built locally |
| soccer_engine.py | Python physics implementation used by replay tools |
| 示例回放.txt | Original sample replay |
| run.cmd | Finds Python and runs a script on Windows |

Requires Windows, Python 3.10+ and g++. The referee arena.cpp calls the Windows API directly (processes, pipes, Job Objects, CPU timing), so it currently runs only on Windows. Put g++ on PATH or set CXX; the archive also searches repository tools/w64devkit/bin. From the parent directory first run `python build_reference.py` to compile the referee and baseline/midfield/sparring bots. Their source is public here but was not given to contestants. Source adaptations are recorded in [source_manifest.json](../source_manifest.json).

From this folder:

```text
python build.py --show
python build.py template.cpp
python match.py check template.cpp
python match.py run --me template.cpp --mate template.cpp --opp baseline --seeds 8 --workers 1
python match.py play ../bots/half2/team4_a.cpp ../bots/half2/team4_b.cpp sparring sparring --seed 3
python replay.py info ../replays/final/team1-team5-s12-2.txt
```

More files are in the [sample index](../replays/README_en.md). `match.py` accepts baseline, midfield, sparring, template, source paths (compiled automatically) and executable paths. Without `--mate`, both seats use your bot. `run` uses win 1 / draw 0.5, whereas official score rate uses 3/1/0. Fixed snapshots make comparisons independent of teammates' ongoing edits.

Generated build/ and replays/ folders are local artifacts. Diagnostics go to replay-adjacent stderr files. CPU budget is 10 ms per turn; busy machines may affect measurements. During the event each bot ran in an AppContainer sandbox. Local match.py defaults to `--sandbox none`: no identity isolation, while CPU timing, timeout kills, the 1 GB memory cap and the no-child-process rule are still enforced by the referee's Job Objects. Add `--sandbox appcontainer` to check under event conditions. In that mode, if antivirus prevents sandbox startup, set SOCCER_BOX_ROOT to a folder where new executables can run. Without SOCCER_BOX_ROOT, the open-source referee uses only _soccer_box/ beside its executable and stops if that directory cannot run the probe. With SOCCER_BOX_ROOT, it uses only the specified directory. There is no fixed machine-directory fallback.

Replay commands include `info`, `events`, `around`, `state`, `player`, `gif`; run `python replay.py --help`. PNG/GIF export, matches and text replay inspection use only the standard library. No packages are installed automatically.

Live clock and team-chat services are omitted. For reproducing historical games, use the [tournament guide](../tournament/README_en.md).
