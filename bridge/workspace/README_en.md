[简体中文](README.md)

# Bridge Building · contestant package

This folder is the package every contestant received, unchanged (the Chinese files are the originals; the `*_en.md` files are reference translations added for the open-source release). The only file not included is `bridge_sim.exe`: build it from source with `python build_engine.py` in the parent folder.

Read `RULES.md` first ([English](RULES_en.md): full rules, schedule, materials, levels 1–6) and `RULES_决赛.md` ([English](RULES_final_en.md): level 7, attack and defense). Submissions go into `submission/`.

| File | Purpose |
|---|---|
| `RULES.md` | Rules (contestant version) |
| `RULES_决赛.md` | Level 7: attack and defense (final bridge + saboteur) |
| `saboteur.py` | Saboteur checks and attack/defense games for level 7 (called by `python bridge.py duel ...`) |
| `bridge.py` | Simulator front end: checks designs, computes cost, runs scenarios, writes replay pages (Python 3.9+, no third-party libraries; JSON in UTF-8, with or without BOM) |
| `bridge_sim.exe` | Physics core (called automatically by `bridge.py`); built from `../engine/bridge_sim.cpp` |
| `viewer.html` | Replay viewer (pages written with `--html` embed the data and open by double-click; you can also open viewer.html and drop in a JSON exported with `--replay`) |
| `levels/levelN.json` | Level N (terrain, anchors, budget); level 7 is `levels/levelfinal.json` |
| `levels/N/sample*.json` | Scenarios of level N (exactly the ones used for official scoring) |

Common commands:
```
python bridge.py check 1 my_level1.json
python bridge.py run 1 my_level1.json
python bridge.py run 1 my_level1.json levels/1/sample2.json --html replay.html
python bridge.py check final submission/final_bridge.json
python bridge.py duel submission/final_bridge.json submission/saboteur.json --html duel.html
```
Every run / duel is archived automatically under `archive/` in the current directory (add `--no-archive` and set `BRIDGE_NO_ARCHIVE=1` to turn it off). Program output is in Chinese.

For how level 7 was officially scored, see [../tournament/README_en.md](../tournament/README_en.md).
