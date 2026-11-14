[中文](README.md)

# Renderer

Renders a bridge replay into a silent 1920×1080 30 fps mp4: low-poly mesas, canyon and water; members coloured by material; vehicles built from the replay's physical dimensions; automatic slow motion on breaks, splashes and hydraulics; a max-stress readout. This produced the 3D match footage in the video. All physics comes from the replay frames; same frame number, same image.

**Requirements** (tested): Python 3.12 with `playwright` 1.63; Chrome 157 (or `--browser msedge` / `--browser chromium`); three.js 0.170.0 via `npm install` in this folder; ffmpeg 7.1 on PATH or `A12_FFMPEG`; a CJK system font (override with `A12_FONT`).

**Render:**

```text
cd bridge
python renderer/src/render3d.py replays/L1-opus.html -o L1.mp4
```

Input is either a replay page from [replays/](../replays/README_en.md) (the embedded JSON is extracted) or a JSON written by `workspace/bridge.py run ... --replay x.json`. Options: `--t0/--t1` (sim seconds), `--list`, `--still 0,120`, `--at 0:3.0`, `--fps`, `--ss`, `--analysis` (stress colouring), `--noao`, `--max-frames N`, `--config clips.json` (multi-clip with per-clip camera and slow motion; fields documented in the Chinese README and in `src/theater3d.js`).

**Differences from the video:** no sound, narration, subtitles or edit; the hand-written camera scripts used for some shots are not included (default is automatic framing); local paths replaced by PATH / environment variables.

**Known limits:** a harmless font 404 is logged when `A12_FONT` is unset; tested on Windows only.

Third-party dependencies: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
