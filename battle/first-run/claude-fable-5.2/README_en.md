[简体中文](README.md)

# First run: Claude Fable 5.2 (archived, not scored)

This is the **first run** of the Claude Fable 5.2 seat: launched during the contest by the contest script on the organizer's machine (Claude Code 2.1.322, headless `claude -p`), working at the same time as the other contestants; it was hit by the organizer's machine reboot at minute 55.7, resumed, and handed in by itself at minute 83.0.

The official result of this seat **is the post-contest rerun by a third party on another machine**; that submission is in [bots/claude-fable-5.2/](../../bots/claude-fable-5.2/). This first run **does not count toward any leaderboard** and is archived unmodified for comparison. Why the record was switched, the conditions and usage of both runs, what we checked and the limitations are in [Contest conditions](../../CONDITIONS_en.md#claude-fable-52-the-third-party-rerun-counts).

## Level results

The organizer evaluated this submission during the contest. For this archive we also put [submission/](submission/) into a clean contestant package and ran `python play.py levels` with the same `war.exe`; every level result (pass or fail, time, end reason and the simulation `digest`) matches the contest evaluation:

| Level | First run | The rerun that counts |
|---|---|---|
| 1 Meadow Ambush | cleared, 260 (52.6 s) | cleared, 200 |
| 2 Broken Dragon Bridge | cleared, 395 (52.9 s) | cleared, 150 |
| 3 Storm the Battery | failed (wiped out at 30.6 s; handed in the one-militia starter file, 10) | cleared, 260 |
| 4 Narrow Pass | cleared, 375 (115.1 s) | cleared, 400 |
| 5 Mountain Beast | failed (wiped out at 4.4 s; starter file, 10) | cleared, 245 |

Three of five levels cleared. The 1v1 and four-player layouts are also in [submission/](submission/); they are not part of the official 945 battles, so no versus score is given here.

## Contents

| Path | Contents |
|---|---|
| [submission/](submission/) | Every file in `submission/` when the first run was collected, byte for byte (hashes in [source_manifest.json](../../source_manifest.json), `source` = `first_run_submission`) |
| [replays/](replays/) | The five level replays `L<level>_fable.wrp.xz`: the originals saved by `war.exe batch` when the organizer evaluated this submission during the contest (the evaluation before the record was switched), xz-compressed; SHA-256, size and result of each original are in `manifest.json` |

## Verify it yourself

```text
# in battle/
python build_engine.py
# copy first-run/claude-fable-5.2/submission/ to workspace/submission/
cd workspace
python play.py check
python play.py levels
```

Compared with the originals here, the replays `play.py levels` saves differ only in the seat name (`level1` vs `fable`), the map-file path in the trailing result block and the timing statistics; the battles are the same. Unpack a replay here with Python: `python -c "import lzma,sys; open(sys.argv[2],'wb').write(lzma.decompress(open(sys.argv[1],'rb').read()))" L3_fable.wrp.xz L3_fable.wrp`, then watch it with `play.py replay`.

## Not included

The session transcript is not included (this repository does not publish any contestant's transcript).
