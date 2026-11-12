[简体中文](README.md)

# Replays

63 replays from the official contest: the originals saved by `war.exe batch` during the official evaluation, xz-compressed as `.wrp.xz` (byte-identical to the evaluation replays once unpacked; hashes in [sha256.json](sha256.json)). After the Claude Fable 5.2 seat switched to the third-party rerun record, the organizer re-evaluated all 945 battles; these are the replays of that evaluation, picked by the new ranking. The five level replays of Fable's first run are in [first-run/claude-fable-5.2/replays/](../first-run/claude-fable-5.2/README_en.md).

## Which ones

The rule was fixed by ranking, without looking at the battles:

- **All 45 level runs**: `L<level>_<seat>`, nine contestants × five levels (every deployment was valid, so everyone has a run on every level).
- **16 duels**: the 8 pairs of adjacent ranks in the overall standings (1st vs 2nd, 2nd vs 3rd, … 8th vs 9th), one battle on Sakura Hill (`D0`) and one on River Isle (`D1`), the higher-ranked contestant in seat 0. File names are `D<map>_<seat 0>_<seat 1>`.
- **2 free-for-alls**: the overall top 4 (`M000_0`: sol, fable, gemini_carbon, gemini_hydrogen) and ranks 6–9 (`M124_0`: qwen, kimi, doubao, ernie), seating 0 of 6 in each (`_0`: the four sit in seats 0–3 in schedule order; seat 0 is south).

Who played whom, the seats and the results are in [`results/eval/对局索引.json`](../results/eval/对局索引.json) and `results/eval/results/<battle>.json`. Seat names map to contestants in [CONDITIONS_en.md](../CONDITIONS_en.md#contestants-and-tools).

The other 882 battles are not in the repository (all replays take about 3 GB). The simulator is deterministic; `python tournament/score.py --rerun --replays` replays all 945 battles and saves every replay.

## How to watch

```text
python replays/unpack.py                 unpack everything to .wrp; or pass parts of names, e.g. unpack.py L2 D0_sol
cd workspace
python play.py replay ../replays/L2_sol.wrp --at 10,20         narrate the battle, draw the field at 10 s and 20 s
python play.py replay ../replays/D0_sol_fable.wrp --seat 1 --deaths   view from seat 1, list how each unit died
```

To make a video, use [renderer/](../renderer/README_en.md): `出片.cmd ..\replays\L2_sol.wrp -o L2_sol.mp4` (Windows) or `python renderer/src/make_video.py replays/L2_sol.wrp -o L2_sol.mp4`.

The replay format is described in [engine/回放格式.md](../engine/回放格式.md) (Chinese).
