[中文](README.md)

# Episode 11 · Boomerang Brawl (OA Showdown, part 1)

13 models each had 75 minutes to write a C++ program that controls one boomerang-wielding character on a small top-down arena: throw, slash, kick when unarmed, dash over pits. Get hit by a boomerang or fall off the floating island and you are out; the last one standing wins. A program always sees the others as they were 0.2 seconds ago, so it has to predict. The qualifier has two events, 1v1 and four-player free-for-all, 32,904 games in total, ranked by Bradley–Terry ratings. Afterwards the top two became captains and used a 1,000-yuan budget in sealed-bid auctions for the other nine candidates, building teams for the team battle in the next episode.

- **What's here**: `workspace/` player pack · `engine/` sparring-bot sources · `bots/` the 13 programs and notes · `results/` ratings and per-game results · `replays/` 117 replays · `tournament/` scoring, reproduction, hidden maps · `auction/` every message of the captain auction · `renderer/` replay-to-video renderer · `appendix/` preview builds, control group and the 21-player combined board (not scored)
- **Reproduce the ranking**: `python tournament/score.py` recomputes the ranking (including the 2,000-resample intervals) from the 32,904 per-game results with the scoring code used at the event, checks every number against `results/海选成绩.json` and prints "核对通过" (check passed). About 20 seconds, Python only. To replay every game: `python build_engine.py`, then `python tournament/rerun.py` (about 15–20 minutes on 16 threads; results differ somewhat, see [tournament/README.md](tournament/README.md)).

## Rules at a glance

- **Everyone is identical** — no classes or skills. Each player has one boomerang: it flies out, turns back at full range and can be caught; if it hits a wall it drops and must be picked up; standing still lets you recall it. A boomerang hit eliminates anyone, teammates included. With a boomerang in hand, melee is a slash (which can also knock down an incoming boomerang in front of you); without one it is a kick that slides the target, and stopping over a pit means falling. Dashes have wind-up and recovery and can cross pits.
- **Reaction delay**: one step is 0.05 s. A program sees itself now, but everyone else and their boomerangs as they were 4 steps (0.2 s) ago. Players in tall grass are invisible.
- **Collapse**: from second 12 the arena crumbles from the edge inward; a game lasts at most 40 seconds.
- **Time limits**: 10 ms CPU per step, 1,000 ms on step 0; an overrun means standing still that step, 20 overruns and the program is killed. 1 GB memory, no child processes, file access or network.
- **Qualifier**: 1v1 (win 1, draw 0.5) and four-player FFA (places score 3/2/1/0 plus 1 per kill; within a game all four are compared pairwise). Each event gets its own rating (Elo scale: 400 points ≈ 10:1 odds); the individual rating is their mean, and the 13 players average 0.
- **Maps**: 10 hand-designed floating islands. 4 are in the player pack (1v1/FFA: Twin Lakes, Yin-Yang; 4v4: Hourglass, Canyon); 6 were hidden during the event (1v1/FFA: Ice Floes, Ruins, Skybridge, Stone Circle; 4v4: Old Town, Forest) and are in [tournament/hidden/](tournament/hidden/). The qualifier used Twin Lakes, Yin-Yang and the four hidden 1v1/FFA maps.
- Full rules: [workspace/RULES.md](workspace/RULES.md); player guide: [workspace/README.md](workspace/README.md); the rules engine is the single header [workspace/dart.hpp](workspace/dart.hpp), shared by players and referee (documents in Chinese).

## Players

| Player | Tool | Reasoning | Directory | Seat |
|---|---|---|---|---|
| GPT-6.2 Astra | Codex CLI | max | [`bots/gpt-6.2-astra/`](bots/gpt-6.2-astra/) | `astra` |
| Claude Fable 5.2 | Claude Code interactive window (started by hand) | max | [`bots/claude-fable-5.2/`](bots/claude-fable-5.2/) | `fable` |
| Claude Opus 5.5 | Claude Code interactive window (started by hand) | max | [`bots/claude-opus-5.5/`](bots/claude-opus-5.5/) | `opus` |
| Gemini 4 Carbon | Antigravity CLI | high | [`bots/gemini-4-carbon/`](bots/gemini-4-carbon/) | `carbon` |
| GPT-6.2 Sol | Codex CLI | max | [`bots/gpt-6.2-sol/`](bots/gpt-6.2-sol/) | `sol` |
| Gemini 4 Hydrogen | Antigravity CLI | xhigh | [`bots/gemini-4-hydrogen/`](bots/gemini-4-hydrogen/) | `hydrogen` |
| Grok 4.7 | Grok Build CLI | xhigh | [`bots/grok-4.7/`](bots/grok-4.7/) | `grok` |
| GLM 5.3 | OpenCode | max | [`bots/glm-5.3/`](bots/glm-5.3/) | `glm` |
| DeepSeek-V4.1-Flash | dsh | max | [`bots/deepseek-v4.1-flash/`](bots/deepseek-v4.1-flash/) | `deepseek` |
| Kimi K3.1 Pro | Kimi Code | max | [`bots/kimi-k3.1-pro/`](bots/kimi-k3.1-pro/) | `kimi` |
| Qwen4-Max | Qoder (started by hand) | max | [`bots/qwen4-max/`](bots/qwen4-max/) | `qwen` |
| MiMo V2.6 Pro | OpenCode | high | [`bots/mimo-v2.6-pro/`](bots/mimo-v2.6-pro/) | `mimo` |
| Doubao Seed-Evolving | OpenCode | high | [`bots/doubao-seed-evolving/`](bots/doubao-seed-evolving/) | `doubao` |

Seat names are the internal names used in the per-game results and replays. Each vendor's own coding tool was used where practical (Doubao, MiMo and GLM used OpenCode). Nobody could hand in before minute 60: anyone who stopped earlier was called back into the same session, with the idle time added back. After GPT-6.2 was officially released, its two models redid the task with the release version, and the official ranking uses those; the earlier preview builds and several control players are in [appendix/](appendix/README.md) and do not count. Tool versions, times, call-backs and interruptions: [CONDITIONS_en.md](CONDITIONS_en.md).

## Ranking (qualifier)

| Rank | Player | Rating | 95% interval | 1v1 | FFA |
|---:|---|---:|---|---:|---:|
| 1 | GPT-6.2 Astra | +306 | +295 – +319 | +345 | +267 |
| 2 | Claude Fable 5.2 | +132 | +121 – +144 | +140 | +124 |
| 3 | Claude Opus 5.5 | +76 | +64 – +89 | +82 | +70 |
| 4 | Gemini 4 Carbon | +51 | +31 – +66 | +37 | +65 |
| 5 | GPT-6.2 Sol | +33 | +21 – +46 | +81 | −15 |
| 6 | Gemini 4 Hydrogen | +30 | +20 – +40 | +51 | +9 |
| 7 | Grok 4.7 | −1 | −19 – +15 | +5 | −6 |
| 8 | GLM 5.3 | −9 | −23 – +5 | +17 | −36 |
| 9 | DeepSeek-V4.1-Flash | −30 | −42 – −18 | −34 | −26 |
| 10 | Kimi K3.1 Pro | −84 | −94 – −73 | −84 | −83 |
| 11 | Qwen4-Max | −87 | −99 – −74 | −98 | −76 |
| 12 | MiMo V2.6 Pro | −167 | −185 – −150 | −212 | −121 |
| 13 | Doubao Seed-Evolving | −251 | −283 – −226 | −330 | −171 |

Adjacent pairs whose gap is within the error (ranks kept as is): Carbon/Sol, Sol/Hydrogen, Grok/GLM, Kimi/Qwen. Intervals come from 2,000 map-block bootstrap resamples. Head-to-head 1v1 records (win-draw-loss over 228 games per pair) are in [results/海选榜.md](results/海选榜.md).

## Captain auction

GPT-6.2 Astra and Claude Fable 5.2 (first and second) became captains; their stablemates (GPT-6.2 Sol, Claude Opus 5.5) joined automatically, and the other nine went up one by one in qualifier order. Each captain had 1,000 yuan to buy 3 players with sealed bids plus a one-sentence reason; the higher bid wins and pays its own price; ties are re-bid up to three times, then the side with more money left wins; every bid must leave 1 yuan for each empty slot. Before bidding each captain had 10 minutes of due diligence with read-only access to the ranking and every candidate's program and notes.

| Team | Bought (price) | Spent | Left |
|---|---|---:|---:|
| GPT-6.2 Astra (+ GPT-6.2 Sol) | Gemini 4 Hydrogen (400), Grok 4.7 (400), Kimi K3.1 Pro (1) | 801 | 199 |
| Claude Fable 5.2 (+ Claude Opus 5.5) | Gemini 4 Carbon (599), GLM 5.3 (200), DeepSeek-V4.1-Flash (201) | 1000 | 0 |

Not picked: Qwen, MiMo, Doubao. After paying 599 for Carbon, Fable had 401 left and could bid at most 400; both sides bid 400 four times for Hydrogen and for Grok, and both went to Astra on remaining budget. That flipped the balances to 401 vs 200: GLM went to Fable on four tied bids of 200, and Fable's remaining 201 was exactly one more than Astra's whole budget, enough to take DeepSeek. Every bid and reason: [auction/拍卖结果.md](auction/拍卖结果.md); every message and answer: [auction/](auction/README.md).

An earlier auction on 11-16 with the preview Astra as captain was declared a rehearsal afterwards and voided (the host had not made clear that team members would rewrite their programs for the team battle); it is included too. The team battle is not part of this episode.

## Try it

**Run locally** (Windows; Python 3.9+, g++ — w64devkit recommended; no third-party Python packages):

```text
python build_engine.py                  build workspace/arena.exe and the three sparring bots (byte-identical to the event)
cd workspace
copy template.cpp player.cpp
python match.py check player.cpp
python match.py duel ../bots/gpt-6.2-astra/player.cpp ../bots/claude-fable-5.2/player.cpp --games 40
python match.py play ../bots/gpt-6.2-astra/player.cpp mid --map ../tournament/hidden/stonecircle.txt --seed 7
```

`arena.exe` manages processes with Windows Job Objects and has only been tested on Windows.

**Watch replays**: [replays/](replays/README.md) holds 117 games sampled from the official schedule (one 1v1 per pair, 12 FFA appearances per player). Open [workspace/viewer.html](workspace/viewer.html) in a browser and drop a `.txt` on it.

**Make video**: all match footage in the video was rendered from replays by [renderer/](renderer/README_en.md) with Blender. With Blender 5.2, Python (numpy, scipy, scikit-image, pillow) and ffmpeg installed: `python renderer/render_game.py replays/g428.txt -o g428.mp4`.

## Directory

| Path | Contents |
|---|---|
| `workspace/` | The player pack as handed out (minus exe files) |
| `engine/` | Sources of the three sparring bots (only binaries were handed out) |
| `build_engine.py` | Builds the referee and the sparring bots |
| `prompt.md`, `prompts/` | Opening messages (two batches) and call-back notices, verbatim |
| `bots/` | The 13 submissions as handed in: `player.cpp`, `notes.md`, submission record |
| `results/` | Ranking, 32,904 per-game results, schedule |
| `replays/` | 117 replays and how they were chosen |
| `tournament/` | Scoring code `rating.py`, `bt.py` (verbatim), `score.py`, `rerun.py`, the 6 hidden maps |
| `auction/` | Auction rules program, due-diligence material, every message and answer, results, captains' notes; the rehearsal |
| `renderer/` | Renderer: code, ten map themes, voxel characters, avatars, render commands |
| `appendix/` | Not scored: preview builds of the two GPT models, six control players, the OpenCode builds of DeepSeek and Kimi, builds saved when someone stopped before minute 60; their ratings and the 21-player board with a reproduction script |
| `CONDITIONS.md` | Event conditions |
| `MANIFEST.md` | What was published, what was not, and why |
| `source_manifest.json` | SHA-256 of every file and of its original |

Character art is in [art/v2/](../art/v2/README.md); this episode's voxel characters are exported to [characters/boomerang/](../characters/boomerang/README.md).

## Differences from the event

- The player pack, sparring sources and all submissions are byte-for-byte originals (hashes in `source_manifest.json`). The exe files in the pack are not committed; `build_engine.py` rebuilds them and the result is byte-identical with w64devkit GCC 16.2.0.
- `results/games.jsonl.gz` is the event's per-game result list, one game per line, gzipped; `results/schedule.json.gz` is the schedule with map paths made relative.
- No replays were saved during the event. `replays/` was re-played for the video with the same seeds, maps, seats and programs; some programs decide search depth by the clock, so a replayed game may differ from the official one (45 of the 117 match).
- The organiser's paths are written as `{赛事目录}` in the auction messages and `{WORKDIR}` in the opening message; the renderer's absolute paths became repo-relative defaults plus environment variables.
- Not included: raw session logs and reasoning, the rest of each player's working directory, the organiser's operations scripts, voice-over and video projects. See `MANIFEST.md`.

## Video

Bilibili: https://www.bilibili.com/video/BV1Y6ypzJEmo

## Disclaimer and licence

This is the record of one specific event. Tools, access channels and how each run happens to go all affect the result — the same model can land elsewhere on a rerun (see the preview vs. release builds in the appendix) — so it says nothing definitive about which model is stronger. Programs and notes were written by the models and not edited; the task, engine, maps, sparring bots and rules were made by the organisers. [MIT licence](../LICENSE) as for the rest of the repository. Character art: character design 小叶子XIYZ (Bilibili); art and production 喵了个猫了个咪了个猫.
