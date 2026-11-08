[简体中文](README.md)

# Results

All numbers below are recomputed from the designs by `python tournament/score.py`, and match the official final results item by item. Machine-readable: [scores.json](scores.json) (per player, per level: score, cost, scenarios passed, file SHA-256; reference costs) and [duels.json](duels.json) (72 level 7 games: whether the bridge was breached, the status of every vehicle).

## Overall

| Rank | Contestant | Total | Levels 1–6 avg | Level 7 | Bridge held | Saboteur breached |
|---:|---|---:|---:|---:|---:|---:|
| 1 | GPT-6.2 Sol | 102.67 | 106.24 | 81.25 | 8/8 | 5/8 |
| 2 | Claude Opus 5.5 | 79.02 | 79.69 | 75.00 | 8/8 | 4/8 |
| 3 | GPT-6 Astra | 73.48 | 71.14 | 87.50 | 8/8 | 6/8 |
| 4 | Gemini 4 Argon | 45.31 | 42.45 | 62.50 | 7/8 | 3/8 |
| 5 | Grok 4.7 | 35.62 | 37.39 | 25.00 | 4/8 | 0/8 |
| 6 | Kimi K3.1 Pro | 32.47 | 32.68 | 31.25 | 5/8 | 0/8 |
| 7 | Tencent Hunyuan Hy4 | 31.45 | 30.44 | 37.50 | 6/8 | 0/8 |
| 8 | Doubao Seed Evolving | 30.96 | 31.95 | 25.00 | 4/8 | 0/8 |
| 9 | DeepSeek V4.1 Flash | 27.93 | 28.41 | 25.00 | 4/8 | 0/8 |

Total = (sum of level 1–6 scores + level 7 score) ÷ 7. Level score = round(100 × reference cost ÷ cost, 2) if all scenarios pass, otherwise 0. Level 7 score = 50 × bridges held ÷ 8 + 50 × bridges breached by your saboteur ÷ 8.

## Levels 1–6

Each cell: level score / cost / scenarios passed. Reference costs: L1 3509.84, L2 10616.73, L3 7967.26, L4 12342.52, L5 8216.12, L6 16709.27.

| Contestant | L1 | L2 | L3 | L4 | L5 | L6 |
|---|---|---|---|---|---|---|
| GPT-6.2 Sol | 98.54 / 3561.67 / 3/3 | 137.85 / 7701.80 / 2/2 | 103.27 / 7714.88 / 3/3 | 105.38 / 11712.20 / 2/2 | 95.31 / 8620.74 / 2/2 | 97.07 / 17212.99 / 3/3 |
| Claude Opus 5.5 | 99.38 / 3531.70 / 3/3 | 114.93 / 9237.42 / 2/2 | 101.99 / 7811.83 / 3/3 | 70.19 / 17584.81 / 2/2 | over budget (13472) | 91.64 / 18233.71 / 3/3 |
| GPT-6 Astra | 91.58 / 3832.47 / 3/3 | 95.99 / 11060.51 / 2/2 | 88.45 / 9007.95 / 3/3 | 65.94 / 18718.15 / 2/2 | over budget (10199) | 84.90 / 19681.97 / 3/3 |
| Gemini 4 Argon | 93.46 / 3755.53 / 3/3 | 78.02 / 13607.99 / 2/2 | 83.22 / 9573.31 / 3/3 | failed 0/2 (19189) | failed 0/2 (9992) | failed 0/3 (17922) |
| Grok 4.7 | 91.25 / 3846.48 / 3/3 | 68.72 / 15450.00 / 2/2 | 64.35 / 12381.56 / 3/3 | failed 0/2 (24846) | not submitted | not submitted |
| Kimi K3.1 Pro | 57.87 / 6065.40 / 3/3 | 57.90 / 18337.64 / 2/2 | 80.30 / 9921.72 / 3/3 | failed 0/2 (24935) | failed 0/2 (9780) | failed 0/3 (11741) |
| Tencent Hunyuan Hy4 | 85.33 / 4113.37 / 3/3 | 49.02 / 21656.32 / 2/2 | 48.27 / 16505.20 / 3/3 | failed 0/2 (23142) | failed 0/2 (5651) | over budget (34497) |
| Doubao Seed Evolving | 90.40 / 3882.36 / 3/3 | 52.75 / 20128.23 / 2/2 | 48.56 / 16406.38 / 3/3 | failed 0/2 (22055) | failed 0/2 (9416) | failed 0/3 (15001) |
| DeepSeek V4.1 Flash | 67.01 / 5237.57 / 3/3 | 52.75 / 20128.23 / 2/2 | 50.72 / 15707.18 / 3/3 | failed 0/2 (20839) | failed 0/2 (5584) | failed 0/3 (19311) |

## Level 7: 72 games

Rows: bridge; columns: saboteur. ✕ = breached (the standard convoy did not all arrive in time and intact), · = held. No game had a physics anomaly.

| Bridge \ Saboteur | Sol | Opus | Astra | Gemini | Grok | Kimi | Hunyuan | Doubao | DeepSeek | Held |
|---|---|---|---|---|---|---|---|---|---|---|
| GPT-6.2 Sol | — | · | · | · | · | · | · | · | · | 8/8 |
| Claude Opus 5.5 | · | — | · | · | · | · | · | · | · | 8/8 |
| GPT-6 Astra | · | · | — | · | · | · | · | · | · | 8/8 |
| Gemini 4 Argon | · | · | ✕ | — | · | · | · | · | · | 7/8 |
| Grok 4.7 | ✕ | ✕ | ✕ | ✕ | — | · | · | · | · | 4/8 |
| Kimi K3.1 Pro | ✕ | ✕ | ✕ | · | · | — | · | · | · | 5/8 |
| Tencent Hunyuan Hy4 | ✕ | · | ✕ | · | · | · | — | · | · | 6/8 |
| Doubao Seed Evolving | ✕ | ✕ | ✕ | ✕ | · | · | · | — | · | 4/8 |
| DeepSeek V4.1 Flash | ✕ | ✕ | ✕ | ✕ | · | · | · | · | — | 4/8 |
| Breached | 5/8 | 4/8 | 6/8 | 3/8 | 0/8 | 0/8 | 0/8 | 0/8 | 0/8 | |

In [duels.json](duels.json), `vehicles[0]` is the saboteur and `vehicles[1:]` are the four standard vehicles (car 12 s, van 13 s, car 14 s, van 15 s); `status` 2 = arrived, 3 = failed; `t` is the arrival or failure time in seconds; `breached` is the official result.
