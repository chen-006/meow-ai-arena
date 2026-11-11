[简体中文](README.md)

# Episode 7 · District King Championship · Round 1: Tron light cycles

Episode 7 ("District King Championship", 区王争霸赛) has two rounds: round 1 is the Tron light-cycle game in this folder; round 2 is a legacy-code optimization round on the same warehouse-scheduling task as episode 5 (see [`../warehouse/README_en.md`](../warehouse/README_en.md)). The combined "District King" standings are at the end of this page.

Ten models received the exact same prompt and task package and each wrote a C++ Tron bot within 60 minutes. All bots then played 1-vs-1, 4-player and 12-player games together. Two reference contestants and one organizer-written strong sparring bot appear only on a second leaderboard ("strong-opponent board"). In total 5,713 formal-mode games were played (4,267 in the final on Oct 18 plus 1,446 added on Oct 20), and games from two earlier fast-mode previews were merged in.

English guides: [competition conditions](CONDITIONS_en.md), [original prompt (reference translation)](prompt_en.md), [full rules](workspace/RULES_en.md), [workspace guide](workspace/README_en.md), [bot index](bots/README_en.md), [results](results/README_en.md), [tournament reproduction](tournament/README_en.md), and [replay index](replays/README_en.md).

Contestant code, any model-authored notes, logs, session records, and replay data remain in their original form and language. Original Chinese task materials are unchanged; English versions are reference translations.

- **What's in this folder**: `workspace/` contestant package · `bots/` submitted bots · `sparring/` sparring-bot source · `results/` official and strong-opponent boards with per-game results · `replays/` replays · `tournament/` final-tournament scripts and schedules · `challenge.py` one-click challenge
- **Reproducing the leaderboard**: The final was driven by the organizer's control script, so the scripts here cannot re-run it as a whole; the schedule and per-game results are public, and any single game can be replayed from its seed and seating. See [`tournament/README_en.md`](tournament/README_en.md) and [`results/README_en.md`](results/README_en.md).

## Rules in brief (full rules: [English reference](workspace/RULES_en.md); exact implementation: `workspace/tron.hpp`)

- Everyone moves one step **simultaneously** (up/right/down/left); visited cells become walls forever. Moving off the board, into an obstacle or into any trail kills you. Surviving longer ranks higher; players who die on the same turn tie.
- **Contested cell**: if several players move into the same empty cell, the one with the highest priority takes it and the others die. Priority = (id − turn) mod N, lower first, rotating every turn.
- Game types: 1v1 (21×21, point-symmetric), 4 players (25×25), 12 players (41×41); about 8% obstacles. One bot plays all types.
- A bot is one function `int decide(const tron::State& s)` returning a direction; the engine (including `tron::step`, which resolves one turn exactly) is provided as a header.
- Thinking budget is the bot's own CPU time: 2000 ms on the first turn, 50 ms per turn afterwards. Over budget or an invalid move means the bot keeps its previous direction for that turn; it is **not** a loss.

## Format and scoring

- Each model started a fresh session in an empty folder containing only `workspace.zip` and received the same prompt (`prompt.md`). The clock started when the package was unzipped: 45 minutes recommended, forced seal at 60 minutes, no earliest submission time. Only `submission/bot.cpp` counts. No internet, no sub-agents.
- Nine official contestants played on Oct 17; the tenth, Mercury, played on Oct 20 with the same prompt and package. On Oct 20 only the games involving Mercury were played and merged with the untouched Oct 18 games, and both leaderboards were refitted.
- **Scoring**: one Bradley–Terry rating per game type (1v1, 3–4 players, 12 players; 400 points ≈ 10:1), total = 50% × 1v1 + 30% × 3–4p + 20% × 12p. **Doubao is the anchor at 0.** 95% intervals come from 1,000 stratified block-bootstrap resamples; places are 95% rank intervals ("2nd–3rd"). Whether two players are distinguishable depends on whether the 95% interval of their difference contains 0; this is pairwise and not transitive.
- **Changed after the race**: the `RULES.md` given to contestants said the greedy baseline is 0, there is a single board, and the 3–4 player type includes 3-player games. After the race the anchor became Doubao (a pure shift; ranks unchanged), greedy only fills seats in multiplayer games and is not scored, 3-player games were dropped, and two boards were used. The 50/30/20 weights did not change.

## Contestants

| Model | Short name | Maker | Tool | Access (model id) | Role |
|---|---|---|---|---|---|
| Mistral Large 4 | Mistral | Mistral AI | OpenCode | OpenRouter (`mistralai/mistral-large-4-0`) | official |
| LongCat-2.0 | LongCat | Meituan | OpenCode | OpenRouter (`meituan/longcat-2.0`) | official |
| Doubao-Seed-Evolving | Doubao | ByteDance | OpenCode | Volcano Engine Coding Plan | official (anchor) |
| Step 5 preview | Step | StepFun | OpenCode | OpenRouter (`stepfun/step-5-preview`) | official |
| openPangu-2.0-Pro | Pangu | Huawei | OpenCode | Huawei Cloud MaaS | official |
| Hunyuan Hy4 preview | Hunyuan | Tencent | OpenCode | OpenRouter (`tencent/hy4-preview`) | official |
| Qwen3.8-27B | Qwen27B | Alibaba | OpenCode | OpenRouter (`qwen/qwen3.8-27b`) | official |
| Mercury 3 (diffusion LM) | Mercury | Inception | OpenCode | OpenRouter (`inception/mercury-3`) | official (played Oct 20) |
| ERNIE 5.1 | Wenxin | Baidu | OpenCode | Baidu Qianfan | official |
| dots3-note preview | Dots | Xiaohongshu | OpenCode | dots official API | official |
| GPT-6 Astra (ultra reasoning + fast tier) | AstraUF | OpenAI | Codex | OpenAI subscription | reference, strong board only |
| DeepSeek V4.1 Flash | DSNew | DeepSeek | dsh 0.2.1-rc.2 standard mode, effort high | DeepSeek API | reference, strong board only |

OpenCode/Codex versions were not recorded; official contestants used the tool's default reasoning effort. DeepSeek ran three times; the first two were voided and only the third (DSNew) counts. dsh inserts the current time into the session at minutes 10, 20 and 30, which no other tool does. The strong sparring bot `sparring_hard` was written before the race by the organizer's sub-agent, took far more than 60 minutes and was tuned against rehearsal bots; it is fitted on the strong board but holds no place. Interruptions and time credits are in [competition conditions](CONDITIONS_en.md).

## Official board (10 official contestants)

| Place | Contestant | Total | 95% interval | 1v1 | 3–4p | 12p |
|---|---|---:|---|---:|---:|---:|
| 1st | Mistral Large 4 | 102 | 63 – 147 | 363 | −191 | −113 |
| 2nd–3rd | LongCat-2.0 | 10 | −24 – 50 | 261 | −271 | −198 |
| 2nd–4th | Doubao-Seed-Evolving (anchor) | 0 | — | 0 | 0 | 0 |
| 3rd–4th | Step 5 preview | −25 | −60 – 11 | 13 | −61 | −64 |
| 5th | openPangu-2.0-Pro | −208 | −242 – −175 | −170 | −199 | −316 |
| 6th | Hunyuan Hy4 preview | −240 | −280 – −203 | −352 | −258 | 66 |
| 7th–9th | Qwen3.8-27B | −290 | −325 – −253 | −363 | −239 | −185 |
| 7th–9th | Mercury 3 | −297 | −338 – −262 | −384 | −201 | −222 |
| 7th–9th | ERNIE 5.1 | −304 | −341 – −271 | −343 | −263 | −271 |
| 10th | dots3-note preview | −440 | −480 – −403 | −510 | −335 | −424 |

LongCat–Doubao and Doubao–Step are indistinguishable, but LongCat–Step is distinguishable (difference 34, interval 3 – 68). Qwen27B, Mercury and Wenxin are pairwise indistinguishable. All programs compiled; no crashes or invalid moves. Only Doubao exceeded the budget: 14 of 1,084 games had one over-budget turn each (max 51.6 ms).

## Strong-opponent board

| Place | Contestant | Total | 95% interval |
|---|---|---:|---|
| — (reference line) | sparring_hard | 324 | 290 – 363 |
| 1st | AstraUF (reference) | 313 | 279 – 352 |
| 2nd–3rd | DSNew (reference) | 128 | 95 – 164 |
| 2nd–3rd | Mistral | 120 | 86 – 155 |
| 4th–6th | LongCat / Doubao / Step | 13 / 0 / −9 | |
| 7th–8th | Pangu / Hunyuan | −193 / −221 | |
| 9th–11th | Qwen27B / Mercury / Wenxin | −265 / −276 / −276 | |
| 12th | Dots | −408 | −448 – −373 |

The two boards are separate fits (shared 1v1 games, separate multiplayer games); scores from different boards must not be compared. AstraUF minus the sparring line is −11 (−46 – 24), indistinguishable. Full tables: `results/RESULTS_正式榜.md` (official) and `results/RESULTS_强对手榜.md` (strong).

## Watch and play

- **Replays**: open any `.html` in `replays/` in a browser (offline). Space play/pause, arrow keys step, `[` `]` jump between events, `H` for all shortcuts. The 30 replays were **drawn at random, not picked**; see `replays/README.md`.
- **Challenge the AIs** (Python 3.10+ and g++; the referee uses Windows CPU-time APIs and runs on Windows only): `python challenge.py your_bot.cpp` plays 82 formal-mode games (1v1, 4p, 12p) against all 10 official bots, both reference bots and the sparring line, then places you on the strong-board scale. Add `--full` for 4× the games.
- **Develop**: run `python make_player_pack.py` once (it compiles the sparring bots from `sparring/` into `workspace/bots/`), then use `workspace/arena.py` exactly as the contestants did (`check`, `run`, `play`, `show`; see `workspace/README.md`).
- **Run a new model under the same conditions**: `python make_player_pack.py --zip` builds `out/workspace.zip` with the original file list (hashes checked against `workspace_manifest.json`); give it to the model in an empty folder with the prompt from `prompt.md`.

## Differences from the race

- The contestants received only compiled sparring bots; this repository ships their source (`sparring/`). Rebuilt with GCC 16.2, `sparring_hard.exe` is byte-identical to the one used in the race; `sparring.exe` differs only in the embedded source file name and timestamp.
- Otherwise `workspace/` is byte-identical to the package given to contestants, and `bots/` is byte-identical to the sealed code (sha256 in `results/清单.json`).
- The two final schedules, including every map seed, are in `tournament/赛程_1018终场.json` and `tournament/赛程_1020补打.json`.
- Not included: raw session logs, the contestants' written strategy notes, all replays except the 30 sampled ones, the organizer's timing/sealing/launch scripts, and data from post-race experiments.

## Side experiment: 2010 human bots

In 2010 the University of Waterloo Computer Science Club, sponsored by Google, ran a Tron AI contest (Google AI Challenge; [archived site](https://web.archive.org/web/20111117222059/http://tron.aichallenge.org:80/index.php)). After the race we adapted 14 human-written bots from that contest whose source we could find (2010 ranks 1–507) and played them 1v1 against this episode's bots (fast mode, 2,872 games; Mercury was not included). Under this episode's contested-cell rule the top AI bots (AstraUF, DSNew, Mistral) land in the 2010 top-5 tier; if contested cells count as draws, as in 2010, they land around 15th, 40th and 85th. Bots below about 150th cannot be told apart on this scale. This is not a claim that any AI "beat the 2010 champion".

**Acknowledgements.** Most of these programs have no open-source license, so **they are not included in this repository**. Thanks to: Andy Sloane (a1k0n, 1st, [github.com/a1k0n/tronbot](https://github.com/a1k0n/tronbot)), Krzysztof Duleba (kduleba, 3rd), Brian Haskin (Janzert, 5th, [janzert.com/tron](https://janzert.com/tron/)), ebrahim (7th, [github.com/ebrahim/tronbot](https://github.com/ebrahim/tronbot)), grogers (11th, [github.com/grogers0/grogers-tron-ai](https://github.com/grogers0/grogers-tron-ai)), Henrik Nordvik (henrikno, 21st, [github.com/henrikno/tronbot](https://github.com/henrikno/tronbot)), Iouri Khramtsov (iouri_, 24th, [github.com/ikhramts/TronBot](https://github.com/ikhramts/TronBot)), Maxime81 (41st), Daniel Hartmeier (dhartmei, 80th, [benzedrine.ch/tron](https://benzedrine.ch/tron/)), stungeye (146th, [github.com/stungeye/Tron-Bot](https://github.com/stungeye/Tron-Bot)), zacharyp (153rd, [github.com/zacharyp/Tron-AI-bot](https://github.com/zacharyp/Tron-AI-bot)), Nathan (ExperimentGarden, 196th, [gist 317060](https://gist.github.com/317060)), jamiely (488th, [github.com/jamiely/tron-bot-waterloo](https://github.com/jamiely/tron-bot-waterloo)) and Matthew Jones (matburt, 507th, [github.com/matburt/tronAI2010](https://github.com/matburt/tronAI2010)), and to the 2010 organizers and forum posters who shared their ideas.

## Episode 7 overall: District King

Each round awards 10 … 1 points by place (a place interval gets the average of the points in it); the two rounds are added.

| Contestant | Tron | Legacy-code optimization | Total |
|---|---:|---:|---:|
| Step 5 preview | 7.5 | 9.5 | **17** |
| Hunyuan Hy4 preview | 5 | 9.5 | 14.5 |
| Doubao-Seed-Evolving | 8 | 6 | 14 |
| Mistral Large 4 | 10 | 3.5 | 13.5 |
| Qwen3.8-27B | 3 | 7.5 | 10.5 |
| Mercury 3 | 3 | 7.5 | 10.5 |
| LongCat-2.0 | 8.5 | 1 | 9.5 |
| ERNIE 5.1 | 3 | 5 | 8 |
| openPangu-2.0-Pro | 6 | 2 | 8 |
| dots3-note preview | 1 | 3.5 | 4.5 |

## Caveats

Each model ran once; results describe the bot written this one time, in 60 minutes, with this prompt and tool, not a ranking of the models. In a post-race stability rerun some models' two attempts differed by about 300 points, comparable to gaps between models. Contestants used different API channels and tools. Bot code was generated by the models and not modified; rules, engine, referee, greedy and the two sparring bots were written by the organizer (the sparring bots by the organizer's sub-agent).
