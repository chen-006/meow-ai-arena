[中文](README.md)

# Renderer (Tank Battle)

Renders one tank-battle replay into a 1920×1080, 30 fps mp4: the board (brick, steel, water, three tank types, bullets, stars, bases, with half-step tweening and effects) and a score panel. The game footage in episode 6 was drawn with it.

## Requirements

Python 3.10+ with Pillow (tested 3.12 / Pillow 12.3.0); ffmpeg on PATH or `ARENA_FFMPEG` (tested 7.1; not needed for stills); a CJK font (defaults to Windows' Noto Sans SC, then Microsoft YaHei; set `ARENA_FONT` otherwise; fonts are not bundled). Tested on Windows 11 only.

## One command

From `tank/`: `python renderer/src/make_video.py <replay> [-o out.mp4] [--from SEC] [--to SEC] [--fps 30] [--still TURN] [--tps 2.2] [--slow 1.0] [--style A|B|C]`

- Input: a `.html` from `replays/` (embedded `tank-frames-1` data, converted back to start state + orders and checked turn by turn against the recorded frames) or a raw `tank-replay-1` `.json/.json.gz`. All 15 replays in the repository pass the check.
- `--from/--to` are seconds of the output video; `--still -1` writes the final position as PNG. Default output: `renderer/renders/<replay>/` (git-ignored).
- Avatars are the ones used in the video (`renderer/assets/avatars/`; override with `ARENA_AVATAR_DIR`). Camera moves, subtitles, narration and music were added in editing and are not included.
- `tsim.py` supports only the parameter set used in this contest.
