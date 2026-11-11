[中文版](README.md)

# Episode 4 · Diplomacy

No bots this time: 7 AI models play standard seven-power Diplomacy directly, one power each. They send private messages, negotiate alliances, betray one another and issue orders themselves; a local referee keeps information private and adjudicates. The same models play three games, drawing new powers each time and carrying over their memories of previous games.

- **What's in this folder**: `games/` full archives of the three games (every message, order and adjudication) · `notes/` the models' own notes · `replays/` replay pages · `referee/` referee and scheduler · `workspace/` the rules given to the models, a local simulator and map data
- **Reproducing the leaderboard**: The standings follow directly from the supply-center counts at the end of the three games under the scoring rule (see the results below). `referee/` can start new games with the same software, but the models' games themselves cannot be replayed.

## Rules in brief (full rules: [workspace/RULES_en.md](workspace/RULES_en.md))

- Standard map, starting in Spring 1901. Each model knows its opponents' powers, but not which model is behind each one.
- Spring and Fall have diplomacy followed by orders: unlimited private messages, up to 300 characters each; public messages up to 400 characters each. **Promises are not binding; deception is allowed.**
- Orders are adjudicated simultaneously once every power that needs to act has submitted and nobody is still reading new messages. There is no thinking time limit and nobody submits orders for you.
- Control 18 supply centers after Fall to win solo. A unanimous vote can end the game in a draw; reaching the end of 1920 also produces a draw.
- Scoring: a solo winner gets 100 points, everyone else 0; a draw divides 100 points in proportion to the squares of supply-center counts (SoS).

## Results

| Game | End | Result | Supply centers |
|---|---|---|---|
| 1 | Spring 1906 | 6-power agreed draw | England 9, Germany 7, Italy 6, Turkey 6, France 3, Russia 3, Austria 0 |
| 2 | Spring 1909 | 4-power agreed draw | England 13, Russia 9, Italy 8, Turkey 4; Austria, France and Germany eliminated |
| 3 | Fall 1913 | **Italy wins solo** | Italy 18, Russia 8, France 6, England 2; all others eliminated |

| Place | Model | Tool | Game 1 | Game 2 | Game 3 | Total |
|---:|---|---|---|---|---|---:|
| 1 | Fable 5.1 | Claude Code | England 36.82 | Russia 24.55 | Italy **100** | 161.37 |
| 2 | Opus 5.5 | Claude Code | Russia 4.09 | England **51.21** | Austria 0 | 55.30 |
| 3 | DeepSeek V4 Pro | OpenCode | Germany 22.27 | Austria 0 | England 0 | 22.27 |
| 4 | GPT-6 Astra | Codex | Italy 16.36 | Turkey 4.85 | Russia 0 | 21.21 |
| 5 | Sonnet 5.5 | Claude Code | Austria 0 | Italy 19.39 | Germany 0 | 19.39 |
| 6 | GPT-6.1 Sol | Codex | Turkey 16.36 | Germany 0 | France 0 | 16.36 |
| 7 | DeepSeek V4.1 Flash | OpenCode | France 4.09 | France 0 | Turkey 0 | 4.09 |

- **Only three games were played, and the power drawn matters greatly** (England scored highest in both of the first two games). The standings are interesting to watch, but do not establish who is best at diplomacy.
- Conditions changed between games: after game 2 began, the administrator sent a reminder to maximize one's score; game 3 prohibited draws before 1910 and explicitly said it was the last game. See [prompt_en.md](prompt_en.md) and [CONDITIONS_en.md](CONDITIONS_en.md).
- Tools, reasoning effort, channels, every interruption and incident: **[CONDITIONS_en.md](CONDITIONS_en.md)**.

## Watch and read

**Replays**: open [game1.html](replays/game1.html), [game2.html](replays/game2.html) or [game3.html](replays/game3.html) in a browser. Step through the map and orders by phase, alongside all private and public messages for that phase (with the sending model's name) and each power's supply-center history.

**Full transcripts**: `games/gameN/全文.txt` lists every message, order and adjudication by phase. Search for a phase code such as `F1905M`.

**Models' notes**: [notes/](notes/) contains the notes, simulation inputs and helper scripts each model wrote in its working directory, preserved as written. For example, Fable's game-3 decision notes, [笔记_第三局_意大利.md](notes/fable-5.1/workdir/笔记_第三局_意大利.md), give its reasons for five betrayals. Model-authored notes, messages and other game records, session records, code and replay data remain in their original language; only the explanatory documentation is translated.

## Run a game yourself

[referee/](referee/) contains the full software used in the event: referee server, event scheduler, match management, administrator commands, live viewer, statistics, promise checker, boundary audit and replay generator. It requires `diplomacy==1.1.2` and was tested on Python 3.12; other versions are unverified. The event ran on Windows. As in the event, we recommend creating the virtual environment at `referee/.venv` (contestants in the Codex sandbox need to use the Python in this directory):

```bash
cd referee
python -m venv .venv
.venv\Scripts\python.exe -m pip install -r requirements.txt     # macOS / Linux: .venv/bin/python
```

Run the commands below from `referee/` with that environment's Python, abbreviated to `python`.

**Try the workflow without models**: every power in the roster can use a scripted player (`"harness": "script"`; see [referee/bots/](referee/bots/)), so no models are needed. Scripted players and tests also run on macOS / Linux. Set `DIPLOMACY_NO_TOAST=1` to disable Windows desktop notifications when the game freezes.

```bash
python -X utf8 -m unittest test_v3                                   # 15 regression tests, about 5 minutes
python -X utf8 match.py new --roster 名单.json --root <player-workspace-directory>
python -X utf8 match.py run [--stop-at S1902M]                       # run / resume; freeze at the specified phase
```

Keep the player workspace directory outside `referee/`.

**Use real models**: the scheduler wakes players through three agents' command-line interfaces: Claude Code (`claude -p --resume`), Codex (`codex exec resume`) and OpenCode (`opencode run --session`). Install and log in to them first. This part has only been verified on Windows.

**Play successive games** (as in the three official games):

```bash
python -X utf8 match.py rematch --prev 管理员/对局/<previous-game-directory> [--draw-from 1910] [--final]
```

The same models resume their own sessions, drawing new powers while avoiding powers they have already played. `--draw-from` sets the first Spring in which draw votes are allowed; `--final` tells players this is the last game.

**Administrator commands**: `admin.py status | pause | resume | stop`. While the match is stopped, `admin.py swap <power> <model>` changes a model (same agent, same session); `admin.py notice "<message>"` registers a notice for delivery on each power's next wake after resuming. The roster format is in [referee/管理员说明_en.md](referee/管理员说明_en.md). That document dates from game 1 and does not cover `rematch` or `notice`; use the commands above for those.

> **Before running**
> - This software lets models run commands unattended on your computer. Permission restrictions apply at the agent-tool layer and cannot constrain Python written by a player. A virtual machine or separate OS user is recommended.
> - The scheduler copies `~/.codex/auth.json` into the match directory's `codex_home/`. If DeepSeek runs out of credit and you have provided a backup key (`referee/管理员/备用key/deepseek.txt`), it rewrites OpenCode's `auth.json` after backing it up.
> - The match directory `referee/管理员/` holds power tokens, login credentials and full logs. It is excluded by `.gitignore`; **never commit or share it**.

**Use the tools on the published game data**:

```bash
python -X utf8 统计.py --match ../games/game3        # centers by year, wakes and usage by player, recent messages (read-only)
python -X utf8 回放.py --match ../games/game3        # regenerate replay as games/game3/回放.html
python -X utf8 承诺核对.py --match ../games/game3    # rerun promise checks, overwriting games/game3/承诺核对.md
```

`audit.py` requires raw wake logs, which are not in the public archive; it exits with an error without writing files.

## Directory

| Path | Contents |
|---|---|
| [workspace/](workspace/) | Contestant package: rules (template and exact per-game versions), operating instructions, client `game.py`, offline simulator `simulate.py`, map index and adjacency tables |
| [prompt_en.md](prompt_en.md) | Translation of opening messages, administrator notices and briefing format |
| [referee/](referee/) | Referee and scheduler (game-3 version), tests and scripted players |
| [games/](games/) | Full archives of all three games: private messages, orders, adjudications, draw votes, wake records and usage, scheduler logs, promise checks and boundary audits; see [games/README_en.md](games/README_en.md) |
| [replays/](replays/) | Three replay pages |
| [notes/](notes/) | Models' working-directory notes and simulation files; see [notes/README_en.md](notes/README_en.md) |
| [CONDITIONS_en.md](CONDITIONS_en.md) | Conditions and incidents |

## Differences from the event

- `workspace/RULES.md` is a template. At each game's start, `referee/match.py` rewrote it for that game's settings and placed it in each power's directory (game 3 changed the draw-voting line to "from Spring 1910"). The "Your objective" line was added to the template only before game 3. The exact rules given to contestants are preserved in [RULES_第1-2局.md](workspace/RULES_第1-2局.md) and [RULES_第3局.md](workspace/RULES_第3局.md); reference translations are [here](workspace/RULES_第1-2局_en.md) and [here](workspace/RULES_第3局_en.md).
- During play, each power's directory also contained a generated `connection.json` (referee address and that power's token); OpenCode players also had `opencode.json` (permissions and context limit). `workspace/` therefore cannot run alone: create a match with `referee/match.py new` and start the referee first. It copies the package and connection information to each power's directory.
- The original package also included the official full rulebook PDF (`资料/官方完整规则.pdf`), copyrighted by its publisher. It is omitted here; the sentence mentioning it in `RULES.md` is preserved. `referee/match.py` now skips the file if it is missing.
- `referee/` is the game-3 version; the scheduler changed several times during games 1 and 2 (see "Code versions" in [CONDITIONS_en.md](CONDITIONS_en.md)). A few changes were made for publication; none affects adjudication:
  - `match.py`: skip the official rulebook PDF if missing; use the referee's Python interpreter for simulation (previously hardcoded to `.venv\Scripts\python.exe`); make `new`'s `--root` mandatory (previous default: `C:\bench\外交`).
  - `audit.py`: derive the local Python path from the current user's directory instead of hardcoding it; remove false positives from Claude's encrypted thinking signatures; exit with an error without overwriting results if raw wake logs are missing.
- Data: identity tokens were removed from archives, the local username in logs was replaced with `<user>`, and 12 encrypted thinking signatures in boundary audits were replaced with an explanatory sentence. Everything else is as it was during the event (including inconsistent line endings).
- Raw output from each model wake (tool calls, command output, etc.) is not included. Wake counts, durations and token usage are in `games/gameN/唤醒记录.jsonl`.

## Licensing

- Code and data in this directory are released under the repository's MIT license, with the exceptions below.
- The referee depends on the [`diplomacy`](https://github.com/diplomacy/diplomacy) package, licensed under AGPL-3.0 or later. It is not included in this repository; comply with its terms when using it.
- Replay maps are rendered by that package using jDip graphics (GPL, with copyright comments retained in each map), so the replay pages as a whole are not MIT-licensed.
- The province index and adjacency tables in `资料/` were compiled from the standard map used by the referee.
- Diplomacy is a trademark of its rights holder. This project is not affiliated with the rights holder.
