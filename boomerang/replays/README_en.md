[简体中文](README.md)

# Replays (117 games)

**How to watch**: open [../workspace/viewer.html](../workspace/viewer.html) in a browser and drop a `.txt` into it (space to play, ← → to step). You can also render a video with [../renderer/](../renderer/README_en.md): `python renderer/render_game.py replays/g428.txt -o g428.mp4`. The format is described in [../workspace/回放格式.md](../workspace/回放格式.md) (Chinese).

**These are replays, not the original games.** The 32,904 games of the event were not saved as replays. For the video's "pure play" cut the organizer drew games from the official schedule and replayed them with the same seed, map, seats and the thirteen 6.2-release programs, saving replays. Some programs size their search by the clock, so a replay does not always match the event: 45 of these 117 games have the same placings as at the event. `same_as_official` in `index.json` says whether each one matches; `winner`, `ranks` and `reason` are the results of the replay.

**How they were picked (a rule fixed in advance, not looking at content)**:

1. Draw a candidate pool from the official schedule with fixed seeds and replay it: 5 random one-on-one games for every pair, 1,300 random four-player games (other, biased batches in the pool, such as "both players out", "long games" or "someone got three kills", take no part in the draw below).
2. One-on-one: 13 players, 78 pairs, exactly 1 game per pair and exactly 13 games per map; four-player: 39 games, each player appearing exactly 12 times.
3. Draw whole sets with seeds 1, 2, 3, ... and take the first seed for which every player's win rate (one-on-one win rate, share of first places in four-player games) is within 10 percentage points of the full official data (one-on-one: seed 5,568; four-player: seed 3,368). This only keeps a 12-game sample from being wildly off a player's real level; no game's content is looked at.
4. Playback order: one-on-one, one-on-one, four-player, repeating, with no two neighbouring games on the same map or sharing a player. `no` in `index.json` is this order.

The number in the file name `g<id>.txt` is the game's `id` in the schedule and in `results/games.jsonl.gz`, so you can compare with that game's result at the event.

| Format | Games | Note |
|---|---:|---|
| One-on-one | 78 | One game for every pair |
| Four-player | 39 | Each player appears 12 times |

To see other games: `python tournament/rerun.py` does not save replays; `workspace/match.py play <programs…> --map <map> --seed <seed>` replays any single game and saves the replay (programs in seat order).
