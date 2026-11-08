# Bridge Building · Level 7: attack and defense

> Reference translation of [RULES_决赛.md](RULES_决赛.md); the Chinese original takes precedence. Official scoring of level 7 is described in [../tournament/README_en.md](../tournament/README_en.md).

> Handed out together with levels 1–6 at the start, completed in the same session and collected together. Materials, physics and vehicles are as in `RULES.md`; only what differs for level 7 is written here. Level file `levels/levelfinal.json` (write `final` in commands). Submission files: `submission/final_bridge.json`, `submission/saboteur.json`.

## 0. In one sentence
Everyone submits two things: a **final bridge** (a static bridge with a fixed budget) and a **saboteur** (a vehicle built from the same materials). Every bridge plays every other contestant's saboteur (no one plays themselves): the saboteur crosses first, trying to damage the bridge and still reach the far bank itself; then the standard convoy crosses. **The convoy all arriving = the bridge side scores.**

## 1. Final bridge
- A 28 m river; anchors, budget and build area are in `levels/levelfinal.json` (`python bridge.py check final bridge.json`).
- **Static bridges only**: no hydraulics, sensors, traffic lights or rules. Road, wood, steel, cable and springs are allowed.
- Standard convoy: car at 12 s, van at 13 s, car at 14 s, van at 15 s, all left to right (`levels/final/sample1.json`); they will be on the bridge at the same time.

## 2. Saboteur (file `saboteur.json`)
```json
{
 "nodes":   [[0,0.22], [-2.2,0.22], [-0.2,0.8], [-2.0,0.8]],
 "members": [[0,1,"steel"], [2,3,"steel"], [0,2,"hydraulic","A"], [1,3,"hydraulic","A"], [0,3,"steel"]],
 "wheels":  [0, 1],
 "motor":   {"speed": 8.0, "accel": 3.0},
 "sensors": [{"id":"g","type":"mygate","x":10.0}],
 "rules":   [{"on":"g","delay":0.0,"do":"hyd","group":"A","ratio":1.5,"speed":3.0}]
}
```
- Coordinates are relative to "the ground point directly below the front of the vehicle at departure": x positive forward, so the body has x ≤ 0; y up, ground at y = 0.
- `wheels` are motorized wheels (2–4, radius 0.22; each wheel node has a mass of 80 kg instead of an ordinary node's 5 kg). Wheel nodes need y ≥ 0.22, other nodes y ≥ 0.1 (body nodes also collide with the road, radius 0.1).
- Optional `"ballast": [[node index, kg], ...]`: extra mass on nodes, 1 per kg, counted in cost and total mass.
- `road` may not be used; node references must be integer indices.
- Limits: total mass ≤ 2500 kg, cost ≤ 6000 (members priced by the material table + 20 per node + ballast at 1 per kg; sensors free), length ≤ 3 m and height ≤ 1.5 m (measured at node centers, excluding wheel radius), 2–40 nodes at least 0.1 m apart, ≤ 120 members, `motor.speed` 0.5–10 m/s, `motor.accel` 0.1–4 m/s² (braking fixed at 6).
- ≤ 10 sensors with unique ids; ≤ 30 rules; hydraulic group names must be strings; all numbers must be plain JSON numbers (no strings, no NaN).
- Only two sensor types: `start` (start of the game) and `mygate` (the front of **your own** vehicle passes world coordinate x). Rules can only control your own hydraulic groups.
- **Speed limits for hydraulics on the saboteur**: `speed` 0.05–3 m/s, `accel` 0.5–5 m/s² (if accel is omitted, min(8 × speed, 5); 0 is invalid).
- The saboteur ignores traffic lights and does not queue. A broken member = it falls apart; landing too hard = it breaks; rollover is not checked.
- The saboteur departs at 0.5 s and **must reach the far bank intact, as a whole vehicle (every point past the finish line), by 12 s (inclusive)**; otherwise the attack does not count.

## 3. Deciding a game
1. The convoy all arrives in time and intact → **the bridge side scores**.
2. The convoy does not all arrive, and the saboteur arrived intact by 12 s (inclusive) → **the saboteur side scores**.
3. The saboteur did not arrive intact in time → the attack does not count; the game is re-run "without a saboteur": if the convoy all arrives the bridge side scores, otherwise neither side scores.
4. A side's file is invalid (fails the check, exceeds limits): that side loses all its games; if both are invalid, neither scores.
5. A physics anomaly during the game (section 6 of RULES.md) → treated as the saboteur exploiting a loophole, the bridge side scores; but if the bridge itself (without a saboteur) also produces an anomaly, the bridge side does not score.

## 4. Schedule and scoring
- Level 7 is completed in the same session as levels 1–6 and collected together. After collection the organizer plays every final bridge against every saboteur (no one plays themselves). During the contest you cannot see anyone else's designs; you can only test locally (you may build a few "imaginary opponent bridges" to try your saboteur on).
- Attack-and-defense score (out of 100) = 50 × bridge-side wins ÷ number of games + 50 × saboteur wins ÷ number of games.
- Level 7 and each of levels 1–6 are **equally important**: total = (level 1–6 scores + level 7 score) ÷ 7 (each level score is rounded to two decimals first, then the seven are averaged).
- Local testing: `python bridge.py duel bridge.json saboteur.json --html replay.html`.
