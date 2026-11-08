# Bridge Building · Rules (contestant version)

> Reference translation of [RULES.md](RULES.md); the Chinese original takes precedence. Program output and scenario names are in Chinese.

## 0. In one sentence

Each level gives you a stretch of river or a deep gorge, anchor points on the banks, a budget, and 2–3 **scenarios** (which vehicles, from which side, departing when). You submit a **bridge design file** (nodes, members, mechanisms). A scenario passes only if every vehicle reaches the far bank in time, intact and without falling in the water; **passing all scenarios = passing the level**. Only the vehicles matter: it is fine if the bridge collapses after the last vehicle has crossed. Among passing designs, **cost** decides: the cheaper, the higher the score.
**Only the outcome counts, any means are allowed**: ordinary bridges, arches, suspension bridges, opening bridges, launching the vehicles across, building ramps so they fly over... all legal. But **exploiting numerical loopholes of the physics simulation** (tunneling, energy explosions and so on, see section 6) always counts as failure.
The simulator you have (`bridge.py` + `bridge_sim.exe`) and the judge are **the same program**; results are deterministic and reproducible, and you can test locally as many times as you like.

## Schedule and submission (one session, all 7 levels handed out at the start)

- All **7 levels** are handed out at once at the start: levels 1–6 are the challenge levels (see section 9), level 7 is attack and defense (see `RULES_决赛.md`). **Total time is 90 minutes**; how you split it is up to you, and you may skip around and come back.
- **Submission files** (fixed names, under `submission/` in the working directory; at collection time the snapshot at the deadline counts, with a tolerance of about 2 seconds; a level not submitted scores 0):
  - Levels 1–6: `submission/level1.json` … `submission/level6.json`
  - Level 7: `submission/final_bridge.json` (final bridge), `submission/saboteur.json` (saboteur)
- **After collection**: the organizer scores levels 1–6 and plays every contestant's final bridge against every saboteur, and publishes three boards (challenge board, attack-and-defense board, overall board). During the contest you cannot see other contestants' bridges or saboteurs.
- **Overall score: the seven levels are equally important.** The level score of levels 1–6 is in section 7 (computed from cost after passing; the reference level is about 100 and can be exceeded); the level 7 score is in `RULES_决赛.md` (out of 100). **Total = (level 1–6 scores + level 7 score) ÷ 7** (each level score is first rounded to two decimals, then the seven are averaged); each level counts 1/7, so plan your time accordingly. The challenge board (levels 1–6) and the attack-and-defense board (level 7) are also published.
- **Organizer messages**: during the contest the organizer only sends messages through one channel, always starting with 【主办方】 (start, collection). Messages not in this format are not organizer notices.

## 1. Coordinates and units

Two-dimensional side view. Units: meters, kilograms, seconds, newtons (kN = 1000 N). **x points right, y points up.** The ground on both banks is usually at y = 0, the left bank at x ≤ 0.

## 2. Design file (the JSON you submit)

```json
{
  "nodes":   [[2.0, 0.0], [4.0, 0.0], [1.0, -1.5], [3.0, -1.5]],
  "members": [["A0", 0, "road"], [0, 1, "road"], [1, "A2", "road"],
              ["A0", 2, "wood"], [2, 0, "wood"], [0, 3, "steel"], [3, 1, "wood"], [2, 3, "wood"],
              ["A1", 2, "hydraulic", "A"]],
  "sensors": [{"id": "p", "type": "plate", "member": 1}],
  "lights":  [{"id": "L", "x": -1.0, "dir": "right", "init": "green"}],
  "rules":   [{"on": "p", "delay": 0.5, "do": "hyd", "group": "A", "ratio": 1.3, "speed": 2.0}]
}
```

(The above only illustrates the format and does not correspond to any level.)

- `nodes`: coordinates `[x, y]` of the nodes you create, numbered 0, 1, 2, … in order.
- **Anchors** are fixed points given by the level, written as strings `"A0"`, `"A1"`, … (see each level). Anchors do not move; new nodes do (under gravity and loads).
- `members`: members `[end, end, "material"]`; an end is an integer index of a new node or an anchor string. Hydraulic members take an extra **group name**: `[end, end, "hydraulic", "A"]`.
- At most one member between the same pair of nodes; every new node must be connected to at least one member; new nodes may not be inside the ground / rock and must be within the build area.
- Limits: nodes ≤ 300, members ≤ 600, sensors ≤ 20, rules ≤ 40, traffic lights ≤ 4; members at least 0.3 m long; nodes at least 0.1 m apart.
- `sensors`, `lights` and `rules` are mechanisms and may be omitted (see section 5).
- Check validity and cost: `python bridge.py check <level> design.json`
- Files are UTF-8 (with or without BOM). All numbers must be plain JSON numbers (not strings like `"10"`, not NaN/Infinity).

## 3. Materials

| Material | Name | Max length (m) | Mass per m (kg) | Tension limit (kN) | Compression limit (kN) | Buckling coefficient (kN·m²) | Price per m | Fixed price each |
|---|---|---|---|---|---|---|---|---|
| `road` | road deck | 2.5 | 50 | 90 | 90 | 300 | 150 | 0 |
| `wood` | wood | 3 | 12 | 50 | 50 | 120 | 40 | 0 |
| `steel` | steel | 6 | 35 | 140 | 140 | 900 | 160 | 0 |
| `cable` | cable | 30 | 4 | 90 | none (goes slack) | — | 70 | 0 |
| `spring` | spring | 4 | 10 | 60 | 60 | — | 30 | 300 |
| `hydraulic` | hydraulic | 5 | 40 | 250 | 250 | — | 150 | 800 |

- **Cost** = Σ (price per m × length + fixed price each) + 20 per new node + sensors (plate 100, gate 100, start 0, horn 50) + 200 per traffic light. Exceeding the budget = invalid design (0 for the level).
- **Mass**: each member's mass is split evenly between its two end nodes, and each node has an extra 5 kg. The bridge's own weight is also a load.
- **Breaking**: each member has a tension limit and a compression limit. **The compression limit drops with length**: effective compression limit = min(compression limit, buckling coefficient ÷ length²). For example, a 2 m wood member fails at 30 kN in compression, a 3 m one at only about 13.3 kN — long members in compression break easily; **short compression members and triangles** work well. Axial forces are judged on a moving average with a 0.1 s time constant (very short impacts are spread out somewhat). Broken members disappear.
- When a wheel is on a road member, its load is split between that member's two end nodes according to the wheel position (the nearer end takes more). Driving on the road causes some dynamic amplification (bumps, starting and braking), so do not design right at the limits computed from statics.
- `road` is **the only thing vehicles can drive on**: wheels only touch road members and the ground on the banks; vehicles pass straight through other members (wood, steel, cable, spring, hydraulic — they are at the sides of the bridge). The road is also a structural member and can break.
- `cable` only takes tension and goes slack (no force) in compression.
- `spring`: stiffness 400 kN/m, natural length = built length.
- `hydraulic`: its length can be changed by rules, see section 5. When idle it is a very stiff strut (axial stiffness EA = 3×10⁸ N; steel is 4×10⁸); maximum push/pull 250 kN — beyond that it is pushed back or pulled longer (it does not break).

## 4. Vehicles and driving

| Type | Name | Mass (kg) | Wheelbase (m) | Wheel radius (m) | Cruise speed (m/s) | Max acceleration | Braking deceleration |
|---|---|---|---|---|---|---|---|
| `car` | car | 1000 | 1.1 | 0.22 | 8.0 | 3.0 | 6.0 |
| `van` | van | 1800 | 1.2 | 0.22 | 7.0 | 2.5 | 5.0 |
| `truck` | truck | 5000 | 1.9 | 0.28 | 5.5 | 1.8 | 4.0 |

- Each vehicle is a rigid body of 4 point masses (rear wheel, front wheel, two body points). **Collisions only happen between the vehicle's wheels / body points and road members or the ground** (both the top and bottom faces of the road collide); the vehicle frame and other bridge members pass through each other. A wheel pressing on a road member from below has no grip (cannot drive).
- Vehicles leave the start at cruise speed and **always try to keep cruise speed**: they slow down uphill (a vehicle with max acceleration 3 can climb at most about 17°) and brake downhill. Vehicles do not steer, reverse or jump.
- In the air a vehicle is not controlled (it cannot accelerate or brake without a wheel on the ground), and there is no air resistance.
- Acceleration and braking are **relative to the road under the vehicle**: a vehicle stopped on a moving platform is carried along by brake friction (friction limit = braking deceleration × vehicle weight).
- **Red lights**: a vehicle stops before the red light line (leaving braking distance at its current speed). It continues on green. If the light turns red when the vehicle can no longer stop (too close to the line), it drives through rather than braking hard past the line.
- **Following**: vehicles in the same direction keep a distance from the vehicle ahead and queue when it stops. Vehicles do not collide with each other (different lanes).

**Failure** (any vehicle having any of these fails the scenario):
- Falling into water / falling: any part of the vehicle below the water surface (or falling into the gorge).
- Rollover / body touching: a body point touches the road or the ground, or the vehicle is upside down (the midpoint of the two body points is below the midpoint of the two wheels; flipping in the air counts too).
- Wrecked: landing with a velocity perpendicular to the contact surface above **12 m/s** (about the speed of a free fall from 7.3 m).
- Timeout: not across by the time limit.
- **Arrival**: a vehicle has arrived when the x of all its wheels and body points is past the finish line (passing it in the air counts); arrived vehicles leave the scene immediately.
- Hitting the ship (levels with a ship): while the ship is passing through the channel, any unbroken bridge member (the whole segment, not only its ends) intersects the channel rectangle, or any point of a vehicle is in the channel.
- Physics anomalies (see section 6).

## 5. Mechanisms: sensors, rules, hydraulics, traffic lights

Mechanisms are a fixed set of "building blocks"; you cannot write arbitrary code.

**Sensors** (`sensors`, each with a unique `id`):

| type | Syntax | Triggers when |
|---|---|---|
| `plate` pressure plate | `{"id":"p","type":"plate","member":5,"min_mass":0}` | a vehicle's wheel first touches member 5 (must be road; pressing from above or pushing from below both count). **Once per vehicle.** Optional `min_mass`: only for vehicles with mass ≥ it. |
| `gate` light gate | `{"id":"g","type":"gate","x":3.0,"dir":"right"}` | the front of a vehicle passes x = 3.0 (`dir`: right only for vehicles going right, left only for vehicles going left, both for both). Once per vehicle. `min_mass` may be added. |
| `start` start | `{"id":"s","type":"start"}` | once at the start of the simulation (t = 0). |
| `horn` ship horn | `{"id":"h","type":"horn"}` | levels with a ship: once, some seconds before the ship enters the channel. |

**Rules** (`rules`): after a sensor triggers, wait `delay` seconds (0–120; this is the "timer"), then perform an action. A sensor can have several rules.
- Hydraulics: `{"on":"p","delay":0.5,"do":"hyd","group":"A","ratio":1.5,"speed":3.0}` — the target length of every hydraulic member in group A becomes **built length × ratio** (ratio 0.5–2), and they move there at no more than `speed` m/s (≤ 15): accelerating first and decelerating before the target, with acceleration 8 × speed by default, or as given by the optional field `"accel"` (0.5–120 m/s²; smaller is gentler; omitted = default; 0 is invalid). The length is then held until another rule changes it. When a hydraulic member's force exceeds its limit it "relieves": it is pushed back / pulled longer and then continues following the command from its new position.
- Traffic light: `{"on":"h","delay":0,"do":"light","light":"L","state":"red"}`.

**Traffic lights** (`lights`): `{"id":"L","x":-1.0,"dir":"right","init":"green"}` — placed at x = -1.0, controlling only vehicles in direction `dir` (right/left/both), initial color `init`.

## 6. Physics anomalies (count as failure)

The following indicate that a design is exploiting numerical loopholes of the simulator; **the scenario fails immediately**:
- any object faster than 60 m/s (energy explosion);
- the center of a wheel or body point crossing the center line of a road member (tunneling);
- non-numbers (NaN);
- an unbroken **road, wood or steel** member deviating from its built length by more than 20%, or a **cable** stretched by more than 20% (numerical instability). **Springs and hydraulics are not checked** (they are meant to change length a lot); a cable getting shorter (slack) is normal. Deformation is checked at the end of each frame.
- Checking period: from the start until the end of the simulation (it runs 2.5 more seconds after the last vehicle arrives or fails); the bridge may collapse during those 2.5 seconds, but the anomalies above still count as failure.
- Note: bridge nodes and members **do not collide with the ground** and may pass into ground / rock; this is not an anomaly (only vehicles are stopped by the ground and the road).

## 7. Scenarios, passing and scoring

- A **scenario** specifies which vehicles depart from which side at what time (and whether there is a ship and when it arrives). Each level's scenario files are `levels/N/sample*.json`; **official scoring uses exactly these scenarios, there are no hidden scenarios**. The simulator is deterministic: passing all locally = passing all officially.
- **Passing a level**: all scenarios of the level pass.
- **Level score**: only for a passed level = 100 × the organizer's reference cost ÷ your cost. As cheap as the reference scores 100; cheaper than the reference exceeds 100 ("beyond the limit"). A level not passed scores 0.
- An invalid design (bad format, over budget and so on) scores 0 for the level.
- Every simulation (`python bridge.py run`, and calls into the simulation from your own scripts via `import bridge`) is archived automatically under `archive/` in the current directory (design, result, and for command-line runs also the replay); you don't need to do anything, and don't delete it.

## 8. Tools

```
python bridge.py check <level> design.json                      check validity, compute cost
python bridge.py run   <level> design.json                      run all public scenarios of the level
python bridge.py run   <level> design.json my_scenario.json --html replay.html     run a given scenario and write a replay page
```
`<level>` is the level number (e.g. `3`). `run` prints each vehicle's result, the reason for failures, and the most loaded members (load percentage = current axial force ÷ breaking limit).
Replay pages open with a double-click: step frame by frame, see each member's load (hover to see member index, material, load %), and see the event list (breaks, sensor triggers, hydraulic actions, vehicles arriving / failing).
One scenario usually takes a fraction of a second. Requires Python 3.9 or newer, no third-party libraries.
Exit codes of `run`: 0 = all scenarios passed, 1 = invalid design, 2 = bad command arguments, 3 = some scenario failed (handy for scripts).

**Other physics conventions** (rarely needed, listed for reference): during the first second of the simulation gravity ramps up from 0 to full (so the bridge settles smoothly); bridge nodes have slight damping, more under water; a broken member's mass stays on its two end nodes; vehicles do not collide with each other.

## 9. Levels

### Level 1 "Small ditch" (小水沟)

A 15 m wide ditch.

- Budget **10000**; build area x∈[-2, 17], y∈[-6, 6]; time limit 30 s.
- Anchors: A0=(0, 0), A1=(0, -2), A2=(15, 0), A3=(15, -2)
- Water surface y = -8
- Vehicles start from the left bank at x=-14 / right bank at x=29 (front of vehicle); arrival when the whole vehicle is past x=23 (going right) / x=-8 (going left).
- **Scenarios** (all pass = level passed):
  - `levels/1/sample1.json` "car left→right": car, left→right, 1 s
  - `levels/1/sample2.json` "van right→left": van, right→left, 1 s
  - `levels/1/sample3.json` "car + van following": car, left→right, 1 s; van, left→right, 2 s

### Level 2 "Grand canyon" (大峡谷)

A 30 m wide deep gorge with no water; whatever falls is gone.

- Budget **26000**; build area x∈[-8, 38], y∈[-14, 14]; time limit 40 s.
- Anchors: A0=(0, 0), A1=(0, -4), A2=(0, -8), A3=(-6, 0), A4=(30, 0), A5=(30, -4), A6=(30, -8), A7=(36, 0)
- No water; gorge bottom y = -30
- Vehicles start from the left bank at x=-14 / right bank at x=44; arrival past x=38 (going right) / x=-8 (going left).
- **Scenarios**:
  - `levels/2/sample1.json` "car left→right": car, left→right, 1 s
  - `levels/2/sample2.json` "overloaded van right→left": van (2200 kg), right→left, 1 s

### Level 3 "Rush hour" (早高峰)

A 28 m river with a rock in the middle (x=14) that can serve as a pier. Rush hour: traffic in both directions will be on the bridge at the same time.

- Budget **18000**; build area x∈[-2, 30], y∈[-7, 10]; time limit 45 s.
- Anchors: A0=(0, 0), A1=(0, -2), A2=(14, -4), A3=(28, 0), A4=(28, -2)
- Water surface y = -8
- Vehicles start from the left bank at x=-14 / right bank at x=42; arrival past x=36 (going right) / x=-8 (going left).
- **Scenarios**:
  - `levels/3/sample1.json` "rush hour A": car left→right 1 s; car right→left 1.5 s; van left→right 2.3 s; van right→left 3 s; car left→right 3.6 s
  - `levels/3/sample2.json` "van convoy": van left→right 1 s; van left→right 1.6 s; van right→left 1.2 s; van right→left 1.8 s
  - `levels/3/sample3.json` "two-way traffic": van left→right 1 s; car right→left 1.2 s; car left→right 2 s; van right→left 2.6 s; car right→left 3.4 s; car left→right 4 s

### Level 4 "Let the ship through" (过船)

A 24 m river with a navigation channel at 9–15 m. At some moment a ship enters the channel, and for the following 6 seconds no part of the bridge may be in the channel (from the water surface up to 6 m). The ship sounds its horn 10 seconds before arriving (horn sensor).

- Budget **25000**; build area x∈[-13, 37], y∈[-6, 14]; time limit 50 s.
- Anchors: A0=(0, 0), A1=(0, -2), A2=(-4, 0), A3=(24, 0), A4=(24, -2), A5=(28, 0), A6=(-12, 0), A7=(-10, 0), A8=(-8, 0), A9=(-6, 0), A10=(-2, 0), A11=(26, 0), A12=(30, 0), A13=(32, 0), A14=(34, 0), A15=(36, 0)
- Water surface y = -2
- Vehicles start from the left bank at x=-14 / right bank at x=38; arrival past x=32 (going right) / x=-8 (going left).
- **Ship**: channel x∈[9, 15], y∈[-2, 6]. For 6.0 s after the ship enters the channel, no bridge member (and no vehicle) may be inside the channel rectangle, otherwise the scenario fails. The horn sounds 10.0 s before the ship arrives (the `horn` sensor triggers).
- **Scenarios**:
  - `levels/4/sample1.json` "ship at 12 s": car left→right 1 s; van right→left 3 s; car left→right 9 s; car right→left 14 s; ship enters the channel at 12 s
  - `levels/4/sample2.json` "ship at 20 s": van left→right 2 s; car right→left 6 s; van right→left 16 s; car left→right 22 s; ship enters the channel at 20 s

### Level 5 "Not enough money" (钱不够)

A 24 m river; the budget is far from enough for a complete bridge, and **there is no wood in this level** (it has run out). On the left bank there is an old dock pit 4 m deep and 10.5 m wide (x from -11 to -0.5); vehicles start at x=-24 and **must cross the pit first, then the river**. 2–3 vehicles go left to right one after another.

- Budget **10000**; build area x∈[-14, 40], y∈[-6, 14]; time limit 40 s.
- Anchors: A0=(0, 0), A1=(0, -2), A2=(-11, 0), A3=(-2, -4), A4=(-4, -4), A5=(-6, -4), A6=(-8, -4), A7=(24, 0), A8=(24, -2), A9=(30, 0)
- Water surface y = -8
- Vehicles start from the left bank at x=-24 / right bank at x=38; arrival past x=32 (going right) / x=-8 (going left).
- **Scenarios**:
  - `levels/5/sample1.json` "car first": car left→right 1 s; van left→right 6 s
  - `levels/5/sample2.json` "van first, three vehicles": van left→right 1 s; car left→right 5 s; car left→right 10 s

### Level 6 "Hell level" (地狱关)

A 36 m river with rocks at x=12 and x=24 (top at y=-2, usable as piers); a navigation channel at 15–21 m (from the water surface up to 6 m) through which a ship passes; vehicles in both directions, including trucks (see the scenarios below).

- Budget **20000**; build area x∈[-8, 44], y∈[-6, 16]; time limit 60 s.
- Anchors: A0=(0, 0), A1=(0, -2), A2=(0, -4), A3=(-5, 0), A4=(12, -2), A5=(24, -2), A6=(36, 0), A7=(36, -2), A8=(36, -4), A9=(41, 0)
- Water surface y = -2
- Vehicles start from the left bank at x=-14 / right bank at x=50; arrival past x=44 (going right) / x=-8 (going left).
- **Ship**: channel x∈[15, 21], y∈[-2, 6]. For 6.0 s after the ship enters the channel, no bridge member (and no vehicle) may be inside the channel rectangle, otherwise the scenario fails. The horn sounds 10.0 s before the ship arrives (the `horn` sensor triggers).
- **Scenarios**:
  - `levels/6/sample1.json` "ship at 16 s, truck first": truck left→right 1 s; car right→left 2 s; van left→right 5 s; car right→left 18 s; ship enters the channel at 16 s
  - `levels/6/sample2.json` "ship at 24 s, traffic": car left→right 1 s; van right→left 2 s; truck right→left 8 s; van left→right 12 s; car left→right 27 s; ship enters the channel at 24 s
  - `levels/6/sample3.json` "ship at 14 s, truck from the right": van left→right 1 s; truck right→left 3 s; car left→right 21 s; ship enters the channel at 14 s
