[简体中文](README.md)

# Episode 12 · Boomerang Team Match (OA Showdown, Part 2)

After the Episode 11 qualifier and captain auction, **Team Astra** and **Team Fable**, five models each, play a 5v5 Boomerang team match. **Team Astra wins 5.5 : 0.5.**

The match has two stages:

1. **120 minutes of team development**: captains direct their four teammates with text-only task briefs; everyone edits only their own program, and every successful compile is shared with teammates for full-team testing. Captains also get 3 hand edits of at most 30 lines each. At the deadline all ten programs are frozen.
2. **Six tactical rounds**: two rounds each on Caldera (Elimination), Piers (Control Points) and Keeps (Regicide). Each round both teams submit formations and tactical plans blind; the official set plays 1,300–2,000 games to decide the round, **10,600 games** in total. Afterwards each round's official and two alternate sets from both teams are crossed into 6 post-match 3×3 "what-if" matrices (**54,000 games**).

- **What's inside**: `workspace/` team player kit · `engine/` referee & sparring sources · `build_engine.py` one-command build · `prompt.md` & `prompts/` all organizer prompts and notices · `dispatches/` all 83 official captain-to-teammate dispatches · `hand_edits/` all 3 effective hand-edit diffs · `rounds/` all 36 formations & tactical plans (R1–R6) · `bots/` all 10 contestant programs & notes (including post-game reviews) · `results/` official standings, 10,600 formal games & 54,000 matrix games · `tournament/` one-command rescore & verification · `replays/` 28 representative & highlight replays · `analysis/` sanitized analyses, cost table, Pareto data & 3 waves of controlled experiments · `renderer/` 5v5 Blender video renderer
- **Reproduce standings & matrices**: `cd boomerang-team && python tournament/score.py` (Python 3.9+, no external dependencies, ~1–2 s). Recomputes all 6 official round scores (paired SE & 2-SE draw rule) from 10,600 games and all 54 cells of the 6× 3×3 matrices from 54,000 games, verifying exact agreement with `results/`.

## Rosters

- **Team Astra**: GPT-6.2 Astra (Captain), GPT-6.2 Sol, Gemini 4 Hydrogen, Grok 4.7, Kimi K3.1 Pro
- **Team Fable**: Claude Fable 5.2 (Captain), Claude Opus 5.5, Gemini 4 Carbon, GLM 5.3, DeepSeek-V4.1-Flash

## Official Results (Team Astra 5.5 : 0.5 Team Fable)

| Round | Map / Mode | Games | Astra W/L/D | Astra Win Rate | Paired SE | Round Pts | Cumulative | Matrix Spread |
|---|---|---:|---|---:|---:|---|---|---:|
| R1 | `caldera` · Elimination | 2,000 | 1063 / 922 / 15 | 53.525% | ±1.093 pp | 1 : 0 | 1 : 0 | 31.40 pp |
| R2 | `caldera` · Elimination | 2,000 | 1571 / 429 / 0 | 78.550% | ±0.937 pp | 1 : 0 | 2 : 0 | 28.00 pp |
| R3 | `piers` · Control Points | 1,300 | 887 / 413 / 0 | 68.231% | ±1.266 pp | 1 : 0 | 3 : 0 | 24.40 pp |
| R4 | `piers` · Control Points | 1,300 | 668 / 631 / 1 | 51.423% | ±1.368 pp | 0.5 : 0.5 | 3.5 : 0.5 | **60.10 pp** |
| R5 | `keeps` · Regicide | 2,000 | 1419 / 553 / 28 | 71.650% | ±0.986 pp | 1 : 0 | 4.5 : 0.5 | 19.95 pp |
| R6 | `keeps` · Regicide | 2,000 | 1307 / 682 / 11 | 65.625% | ±1.080 pp | 1 : 0 | **5.5 : 0.5** | 19.25 pp |

## Quick Start

> The open-source repo does not commit `.exe` binaries. Before running local matches in `workspace/`, run `python build_engine.py` once from `boomerang-team/` to build `workspace/arena.exe` and `workspace/bots/{weak,mid,strong}.exe`.

```bash
cd boomerang-team
python build_engine.py
python tournament/score.py
cd workspace
python match.py team weak --vs mid --map caldera --games 4
```

To watch replays without installing anything, open [`workspace/viewer.html`](workspace/viewer.html) in a browser and drag in any `.txt` file from [`replays/`](replays/README.md). To render 1080p videos with Blender 5.2, see [`renderer/README.md`](renderer/README.md).

## Video

Bilibili: <B站链接>

## License & Attribution

Published under the repository's [MIT license](../LICENSE).

Character artwork credit: **角色形象设计参考：小叶子XIYZ；美术和制作：喵了个猫了个咪了个猫** (Character design reference: 小叶子XIYZ; art and production: 喵了个猫了个咪了个猫).
