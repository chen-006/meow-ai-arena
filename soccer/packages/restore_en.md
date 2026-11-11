[简体中文](restore.md)

# Historical package inventory and reconstruction

The [manifest](player_pack_manifest.json) lists all 23 initial files per contestant slot, shared team files, runtime categories and all six half-time packages. It was checked against the original base kit, installation/chat initialization code and distributed folders. Contestant-created caches are not initial package files.

As in episodes 3, 6 and 7, contestants received reference executables, not sparring source. The public archive supplies source and a build entry point. This release provides the complete inventory and instructions, **not a one-command generator of every historical service**. Fixed material can be reconstructed below; omitted clock/chat clients and services have their boundaries and static hashes documented. Users needing those services must independently implement or obtain usable versions. Copying the repository alone does not reproduce all event conditions.

## 1. Files and layout

Each team has `a/` and `b/` workspaces. Their file categories match; models are mapped in [roster.json](../bots/roster.json). Paths below are relative to a contestant's folder.

| Files | Count | Historical source / public reconstruction |
|---|---:|---|
| `arena.cpp`, `build.py`, `match.py`, `replay.py`, `run.cmd`, `soccer.hpp`, `soccer_engine.py`, `template.cpp`, `viewer.html`, `示例回放.txt` | 10 | Fixed base-kit files, now under `workspace/`; adaptations in section 4 |
| `RULES.md` | 1 | Base rules, with a UTF-8 BOM added at installation |
| `README.md` | 1 | Filled `format.md` plus base README, with UTF-8 BOM |
| `arena.exe` | 1 | Compile the public referee with its sandbox |
| `bots/baseline.exe`, `bots/midfield.exe`, `bots/sparring.exe` | 3 | Built by `build_reference.py`; exclude their source from a fresh contestant package |
| `clock.py` | 1 | Original client reads parent `phase.json` and calls `tc.notify()`; script omitted |
| `tc.py`, `with_notice.py` | 2 | Original chat client and reminder wrapper depend on local configuration/storage; scripts omitted |
| `tc.bat` | 1 | ASCII launcher: `@python "%~dp0tc.py" %*`; historical Windows text writing affects line endings, so ordinary LF is not necessarily hash-equivalent |
| `TEAMCHAT_README.md` | 1 | Copy the distributed original here byte-for-byte, including its BOM |
| `teamchat.json` | 1 | Generated per-slot identity/path configuration; original values and configuration hashes are excluded |
| `submission/player.cpp` | 1 | Copy `template.cpp` at setup; final sealed code is not initial package content |
| **Total** | **23** | Every path and fixed-file SHA-256 is in the manifest |

Shared team structure follows. Half-time material arrives later; runtime entries need not exist at opening.

```text
teamN/
  a/                         # 23 initial files above
  b/                         # same categories, different identity/README parameters
  team.json                  # names, timing and organizer mirror configuration
  phase.json                 # organizer-updated phase/deadline/durations
  chat/
    chat.jsonl               # runtime chat; omitted
    chat.lock                # runtime lock; never copy an old lock
    state/a.json             # runtime read cursors; never copy historical state
    state/b.json
  halftime/                  # delivered after the half-time event
```

`submission/partner.cpp` is optional second-half work, not initial content. `build/`, `replays/`, `_soccer_box/` and `__pycache__/` are local runtime artifacts. Contestant-created tests, notes and temporary files are not part of the initial kit. Organizer mirrors/backups, account configuration, raw sessions and voided data are excluded.

## 2. Reconstruct fixed development material

1. From `soccer/`, run `python build_reference.py` to build the referee and three references. Requires Windows, Python 3.10+ and g++.
2. Create fresh team folders with `a/` and `b/`. Copy the ten fixed files, `RULES.md` and four executables from `workspace/`, and copy this directory's `TEAMCHAT_README.md`. Do not copy `sparring/`, sealed contestant `bots/`, `notes/`, final standings or caches.
3. Create `submission/` and copy `template.cpp` to `submission/player.cpp`. Do not pre-create a partner bot.
4. Fill [format.md](format.md) with the parameters below and append `workspace/README.md`, changing only that base README's opening `# ` to `## `. Preserve the format template's trailing separator/newlines. Write the combined README and rules with one UTF-8 BOM. This follows historical installation; the public base README includes declared adaptations, so composed files need not match historical hashes.
5. `historical_sha256` describes original fixed material; `published_sha256` describes current public files. README historical hashes are provided for all 12 slots. Executable hashes come from the original base package, not later contestant rebuilds. Run `python check_archive.py` from `soccer/` to check public-source hashes and inventory structure.
6. Before a fresh event, connect the clock/chat interfaces below and rehearse messages, timing and sealing with two manual slots. Without those services, use only the standalone [workspace tools](../workspace/README_en.md); do not send an opening prompt asking a model to wait on absent `tc.py`.

| Template field | Value |
|---|---|
| `提交文件` | `submission/player.cpp` |
| `队伍数`, `会议分钟`, `上半场分钟`, `下半场分钟` | Initially distributed README: `6`, `5`, `40`, `25`; the half-time message changed the actual second half to `40` |
| `中场种子`, `终场种子` | `32`, `44` |
| `胜`, `平`, `负` | `3`, `1`, `0` |
| `队友位` | b for slot a; a for slot b |
| `A位`, `B位` | Team model display names from `bots/roster.json`, field `model` |

The original template's even/odd seed wording refers to schedule indices, not numeric seed values. All 12 distributed READMEs retain the old 25-minute second-half value. Composing them with these initial parameters and the original base README matches all 12 historical hashes. The half-time prompt introduced 40 minutes, individual teams and 50%/50% weighting; the actual clock also used 40 minutes. Do not backdate these changes into the opening kit.

### 2.1 External clock and chat dependencies

The manifest publishes field names, not original values. Generate configuration in a fresh local workspace; do not copy historical identities, timestamps or chat state.

- `teamchat.json`: `team`, `me`, `peer`, `me_name`, `peer_name`, `chat_dir`, `my_dir`, `peer_dir`. Resolve paths under the new event root. Keep identity configuration local, outside public packages.
- `team.json`: `team`, `names`, `mirror`, `start`, `start_t`. Organizer mirrors are outside contestant material and must be independently configured.
- `phase.json`: `phase`, `desc`, `ends_t`, `ends`, `meeting_minutes`, `half1_minutes`, `half2_minutes`, `t0`, `updated`. The organizer updates it on phase transitions and credits. The clock reports phase and remaining seconds. A five-minute meeting starts after both players arrive, followed by 40 minutes of development; package delivery starts another 40-minute half.
- Implement `send/inbox/wait/log/status` and `notify()` as described in the [original instructions](TEAMCHAT_README_en.md), including blocking waits and separate read cursors. Use fresh storage/locks/cursors, never historical chat exports.
- Only `clock.py` integrated reminders historically. If a new event adds reminders to other tools, disclose that condition change rather than calling it original behavior.
- The organizer must separately copy and hash submissions at sealing time and manage only its own processes. Public local-match tools do not launch models, manage accounts or stop model sessions.

## 3. Half-time reconstruction

Both slots share `../halftime/`:

| Path | Reconstruction source |
|---|---|
| `README.md` | Describe standings, team replays and three analysis tools; historical hash is listed |
| `RESULTS.md` | All six teams' standings, this team's game list and its faults, reconstructed below |
| `tools/replay.py`, `tools/soccer_engine.py`, `tools/viewer.html` | Copy corresponding `workspace/` files unchanged |
| `replays/{id}.txt` | All half-time games where `A == teamN` or `B == teamN`, including references: 512 per team |

Run `python tournament/score.py halftime` from `soccer/`. Source data is `results/halftime/results.json.gz`. For `RESULTS.md`, list all six `standings`, then each selected game's `id`, opponent, side A/B, two actual `seats`, `score` ordered from this team's perspective, result and `seed`, with a link to `replays/{id}.txt`. Include only this team's seats from `faults`. There are `(5 + 3) × 32 × 2 = 512` games per team, including references; a game between two contestant teams appears in both packages.

Complete historical half-time replays are not published. To obtain playable regenerated material:

```text
python build_reference.py
python tournament/reproduce.py halftime --all --out out/restored-halftime
```

This reruns 2,112 games and can take substantial time. Every output JSONL row has `original_id`; output order follows the input schedule, and row n's replay is `replays/g{n}.txt`, starting at zero. Rename/copy using `original_id` and filter into each team's package. **Label these as regenerated replays**: CPU timing, compilers and machine state may change steps or scores. Do not present historical standings and newly rerun replays as the same original batch; regenerated standings may be attached separately. Exact historical packages require the original replays. Manifest hashes describe originals, not guaranteed rerun output.

## 4. Adaptations and limits

- The [source manifest](../source_manifest.json) records compiler discovery, referee link libraries, sparring include paths, sandbox directory selection, rebuilding a stale referee, defaulting local matches and replays to `--sandbox none` and corresponding documentation changes. Physics, sealed contestant source, event seeds and results are unchanged.
- No fixed organizer sandbox directory remains. The referee uses adjacent `_soccer_box/` or explicitly configured `SOCCER_BOX_ROOT`, and stops when the selected location fails its probe.
- Referee sandboxes take their names from a fixed pool, `soccer2v2.<root tag>.s<k>`, instead of a new name per game, so Windows' kernel AppContainer records stop growing with the number of games and error 1450 no longer appears after tens of thousands of sandboxes. See the [README](../README_en.md#differences-from-the-event).
- Static hashes for omitted clients do not make their services available. Accounts, proxies, raw sessions, hidden thinking, chats, identity values and voided data are excluded.
- The inventory identifies what existed, what is available and what still requires local setup. It supports fixed development material, local play and archive verification, not turnkey recreation of the original event or guaranteed byte-identical historical binaries.
