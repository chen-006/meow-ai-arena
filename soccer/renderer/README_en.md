[中文](README.md)

# Renderer

Renders a soccer replay (`SOCCER2V2-REPLAY 2`, the `.txt` files in [replays/](../replays/README_en.md)) into a silent 1920×1080 60 fps mp4: a three.js pitch, a broadcast camera following the ball, a goal close-up with caption, a ~4 s slow-motion replay, and a score bug. This is what produced the match footage in the video. Same arguments, same frames.

**Requirements** (tested): Python 3.12 with `playwright` 1.63; Chrome 157 (or `--browser msedge` / `--browser chromium` after `playwright install chromium`); three.js 0.170.0 via `npm install` in this folder; ffmpeg 7.1 on PATH or `A12_FFMPEG`; a CJK font (default `C:\Windows\Fonts\NotoSansSC-VF.ttf`, override with `A12_FONT`).

**Render:**

```text
cd soccer
python renderer/src/render_soccer.py replays/final/team4-team5-s38-1.txt -o t4t5.mp4 --start 0 --end 160
```

Useful options: `--plan` (timeline only), `--no-replay`, `--still 300,540` (PNG frames), `--every 2 --preset veryfast` (quick preview), `--max-frames N`, `--nvenc`. See `-h` for the rest. Player labels, avatars and team colours are in `src/players.json`; avatars live in `assets/avatars/`.

**Differences from the video:** no sound effects (not included); the series logo in transitions is replaced by text; local paths replaced by PATH / environment variables; no editing, subtitles or narration.

**Known limits:** the frames right after a goal are extrapolated (not in the replay); pitch markings are decorative; tested on Windows only.

Third-party dependencies: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
