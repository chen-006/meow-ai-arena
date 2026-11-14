[中文](README.md)

# Renderer (Capture the Flag)

Renders one capture-the-flag replay into a 1920×1080, 30 fps mp4: the board (walls, bases, neutral flags, three units per side, with movement tweening and attack / pickup / drop / score effects) and a score panel. The game footage in episode 3 was drawn with it.

## Requirements

Python 3.10+ with Pillow (tested 3.12 / Pillow 12.3.0); ffmpeg on PATH or `ARENA_FFMPEG` (tested 7.1; not needed for stills); a CJK font (defaults to Windows' Noto Sans SC, then Microsoft YaHei; set `ARENA_FONT` otherwise; fonts are not bundled). Tested on Windows 11 only.

## One command

From `ctf/`: `python renderer/src/make_video.py <replay> [-o out.mp4] [--from SEC] [--to SEC] [--fps 30] [--still TURN] [--tps 4]`

- Input: a `.html` from `replays/` (the embedded replay is extracted) or a replay `.json/.json.gz` (`header` + per-turn `frames`). All 11 replays in the repository load.
- Constant speed of `--tps` turns per second (a 400-turn game ≈ 100 s of video); `--from/--to` are seconds of the output video; `--still -1` writes the final position as PNG. Default output: `renderer/renders/<replay>/` (git-ignored).
- Avatars are the ones used in the video (`renderer/assets/avatars/`; override with `ARENA_AVATAR_DIR`). Camera moves, subtitles, narration and music were added in editing and are not included.
