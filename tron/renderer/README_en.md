[中文](README.md)

# Renderer (Tron)

Renders one Tron game replay into a 1920×1080, 30 fps mp4: a large board on the left (neon light walls with avatar light-cycles, frame-by-frame tweening, crash bursts, contested-cell and swap effects), a side panel on the right (turn, players alive, each player's length / cause of death / place), a title on top and a strip at the bottom. The game footage in episode 7 was made with it.

## Requirements

| What | Version | Notes |
|---|---|---|
| Python | 3.10+ (tested 3.12) | `pip install pillow numpy` (tested Pillow 12.3.0, NumPy 2.5.1) |
| ffmpeg | any recent (tested 7.1) | Encodes the mp4. Put it on PATH or point `ARENA_FFMPEG` at it. Not needed for stills |
| CJK font | defaults to Windows' Noto Sans SC (`NotoSansSC-VF.ttf`), then Microsoft YaHei | Fonts are not bundled. Set `ARENA_FONT` to any `.ttf/.ttc/.otf` with Chinese glyphs; on Linux/macOS it tries Noto Sans CJK and PingFang |

Tested on Windows 11 only.

## One command

From `tron/`:

```text
python renderer/src/make_video.py <replay> [-o out.mp4] [--from SEC] [--to SEC] [--fps 30] [--still TURN]
       [--tps 4] [--slow 1.5] [--style mix|neon|flat|avatar] [--vor] [--palette colors.json] [--names A,B] [--sub TITLE]
```

```text
python renderer/src/make_video.py replays/07-1v1-DSNew-vs-Mistral.html -o DSNew-vs-Mistral.mp4
python renderer/src/make_video.py replays/14-4p-strong-Doubao-greedy-Dots-Wenxin.html --still -1 -o final.png
```

- **Input**: a `.html` from [`replays/`](../replays/README_en.md) (the replay data is extracted from the page) or a `tron-replay-1` `.json` saved by `arena.py play`. The game is re-simulated on load and checked against the recorded turns, death turns, causes and places; a mismatch is an error.
- Without `-o`, output goes to `renderer/renders/<replay>/` (git-ignored; override with `ARENA_WORK`).
- Speed: `--tps` turns per second (default 4, the web viewer's 1×), slowing to `--slow` around deaths, contested cells and swaps; 1 s hold at the end.
- `--from/--to` are seconds of the **output video**. `--still TURN` writes one PNG (fractional turns allowed, `-1` = final position).
- `--style mix` (default, as in the video), `neon`, `flat`, `avatar`; `--vor` overlays territory; `--palette` / `--names` change colors, labels and displayed names.
- Windows shortcut: `renderer\出片.cmd <replay> [same options]` (`ARENA_PYTHON` selects the interpreter).

## Differences from the video

- Avatars are the ones used in the video, in `renderer/assets/avatars/`. To use your own, put `<name>.png` files in a folder and set `ARENA_AVATAR_DIR`.
- The bottom strip is text only (no series logo); the subtitle area above it is left empty.
- Camera moves, zooms, subtitles, narration, music and sound effects were added in editing and are not part of the renderer.

## Known limitations

- Single process, frame by frame (about 0.15 s per frame on a 32-thread laptop; a 1v1 game is 20–40 s of video).
- Players with very similar colors are only reported in `rp.close_pairs`; use `--palette` to change them.
