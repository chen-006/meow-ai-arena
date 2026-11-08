[简体中文](README.md)

# Replays

Double-click any `.html` to watch frame by frame in a browser, offline: scrub the timeline, hover a member to see its material and load percentage, and read the event list (breaks, sensor triggers, hydraulic actions, vehicles arriving or failing). The viewer's interface is in Chinese.

The selection does not depend on looks or outcomes: for levels 1–6, the highest-scoring design of each level on that level's first scenario (`sample1`); for level 7, 3 of the 72 games drawn with `random.Random(20261107)`.

| File | Content | Result |
|---|---|---|
| [L1-opus.html](L1-opus.html) | Level 1 · Claude Opus 5.5 · car, left to right | pass, cost 3531.70 |
| [L2-sol62.html](L2-sol62.html) | Level 2 · GPT-6.2 Sol · car, left to right | pass, cost 7701.80 |
| [L3-sol62.html](L3-sol62.html) | Level 3 · GPT-6.2 Sol · rush hour A | pass, cost 7714.88 |
| [L4-sol62.html](L4-sol62.html) | Level 4 · GPT-6.2 Sol · ship at 12 s | pass, cost 11712.20 |
| [L5-sol62.html](L5-sol62.html) | Level 5 · GPT-6.2 Sol · car first | pass, cost 8620.74 |
| [L6-sol62.html](L6-sol62.html) | Level 6 · GPT-6.2 Sol · ship at 16 s, truck first | pass, cost 17212.99 |
| [L7-bridge-hunyuan-vs-saboteur-sol62.html](L7-bridge-hunyuan-vs-saboteur-sol62.html) | Level 7 · Tencent Hunyuan Hy4's bridge × GPT-6.2 Sol's saboteur | breached |
| [L7-bridge-doubao-vs-saboteur-sol62.html](L7-bridge-doubao-vs-saboteur-sol62.html) | Level 7 · Doubao Seed Evolving's bridge × GPT-6.2 Sol's saboteur | breached |
| [L7-bridge-sol62-vs-saboteur-kimi.html](L7-bridge-sol62-vs-saboteur-kimi.html) | Level 7 · GPT-6.2 Sol's bridge × Kimi K3.1 Pro's saboteur | held |

Replays were regenerated after the contest with the same engine (deterministic, identical to scoring); level 7 replays state the official scoring verdict. Regenerate these 9: `python tournament/replays.py`; generate everything (54 designs × every scenario, the 6 reference solutions and all 72 games): `python tournament/replays.py --all out/replays` (about 200 files, one to two hundred MB).

You can also make one for any design:

```text
cd workspace
python bridge.py run 3 ../designs/opus/level3.json levels/3/sample2.json --html replay.html
```
