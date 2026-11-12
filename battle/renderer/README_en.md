[简体中文](README.md)

# Renderer

Renders the simulator's `.wrp` replays into 1080p, 30 fps video: voxel soldiers and props on a small themed battlefield per map, an oblique top-down camera that follows the action, event-specific close-ups for big moments, and a scoreboard-style bar across the top. All battle footage in the video came from this renderer. The Chinese [README.md](README.md) has more detail on the camera rules and effects.

## 1. Requirements

| What | Version | Notes |
|---|---|---|
| Blender | 5.2.4 LTS (tested; nothing below 5.x was tried) | EEVEE, background mode `-b`, no add-ons. Put it on PATH or point `A12_BLENDER` at `blender.exe`. Blender's bundled Python already has numpy. |
| Python | 3.10+, with `numpy` and `pillow` | Runs `make_video.py` (reads the replay, plans the camera, draws the HUD, calls ffmpeg). `出片.cmd` uses `python` from PATH; use `A12_PYTHON` for another interpreter. |
| ffmpeg | any recent version (tested 7.1) | Encodes the mp4. On PATH or via `A12_FFMPEG`. |
| Chinese font | `C:\Windows\Fonts\NotoSansSC-VF.ttf` by default, falling back to Microsoft YaHei | For the HUD. Use `A12_FONT` for another font (required on Linux and macOS). |

Levels and versus maps are read from this repository's `battle/workspace/` (`levels/`, `maps/`) and the AI generals from the top-level [`characters/`](../../characters/README_en.md); nothing needs configuring. Tested on Windows 11; nothing in the code is Windows-only, but other systems were not tried.

## 2. Rendering a clip

```text
出片.cmd <replay.wrp> [-o out.mp4] [--from s] [--to s] [--procs 3] [--samples 12]
         [--lord default|<name>|<model.json>] [--shots shots.json] [--no-close] [--pitch deg] [--names A,B]
         [--no-hud] [--no-fx] [--no-shake] [--keep]
```

Without the `.cmd`: `python renderer/src/make_video.py <replay.wrp> [same options]`. Examples (from `battle/`):

```text
python replays/unpack.py L2_sol
renderer\出片.cmd replays\L2_sol.wrp -o L2_sol.mp4 --to 20 --names "GPT-6.2 Sol,Defenders"
renderer\出片.cmd replays\L4_gemini_carbon.wrp -o L4.mp4 --lord gemini_carbon
```

- Without `-o` the output goes to `renderer/renders/<replay>/<replay>.mp4` (intermediate frames too, not committed; change with `A12_WORK`).
- `--from/--to` render a section (seconds). If the clip includes the last frame, it holds for 3 seconds on a result card.
- `--procs` parallel Blender processes (default 3). On a 32-thread CPU with an RTX 4060 laptop GPU, 4 processes manage about 0.15–0.22 s per frame together; a one-minute replay takes 5–7 minutes.
- Interrupted renders resume: run the same command again and finished frames are skipped. After changing rendering code, delete `renders/<replay>/frames`.
- `--names` names per team on the HUD (comma-separated). `--no-close` disables automatic close-ups; `--pitch` changes the default camera pitch; `--no-fx`, `--no-shake` turn off effects and camera shake.

## 3. Stills

From `src/`: one frame of a replay with `blender -b --factory-startup -P render_replay.py -- <replay.wrp> --out <dir> --still <frame> [--cam x,z,y,width,pitch,yaw]`; unit, general, prop and boss line-ups with `blender -b --factory-startup -P stills_units.py -- <dir> [close wide host poses lord boss]`. Frame = seconds × 30; `python src/replay.py <replay.wrp>` lists the close-ups a replay will trigger.

## 4. Camera, effects, generals

- `replay.director()` gives one camera per frame from three layers: a following wide shot that frames the busiest window of "interesting points"; event-designed close-ups (bridge collapse, snowball breaking a barricade, the boss being stunned, catapult stones landing, cavalry hitting a spear wall, defender sorties, a general's death); and an optional hand-written shot list (`--shots`, JSON format in the Chinese README). Per-map camera defaults live in the `"camera"` section of `themes/<map>.render.json`.
- All effects are block particles (`src/fxsys.py`); hit flashes, exaggerated limb animation (`army.py`), bodies sinking into the ground, mud on legs in level 1, the rising tide on Tidal Sandbar.
- Level 4's general is a generic voxel commander by default (`units.m_lord()`, original). `--lord <name>` swaps in an AI character: `<name>.json` is looked up in `A12_LORD_DIR`, then in the top-level `characters/` (names are the seat names: `sol`, `fable`, `gemini_carbon`, …).

## 5. Files

`出片.cmd` one-command render; `themes/<map>.render.json` scenery per map (surroundings, decor, paths, camp props, landmarks, camera defaults; terrain, props, defenders and deployment zones always come from the engine files embedded in the replay); `src/` the code: `make_video.py` (driver), `render_replay.py` (Blender entry point), `replay.py` (replay parsing, camera direction), `hud.py`, `scene.py`, `terrain.py`, `builders.py`, `mats.py`, `looks.py` (eight map themes), `units.py` (9 unit types, the boss, the generic general and props as voxel code; there are no separate model files), `army.py`, `fx.py`, `fxsys.py`, `stage.py`, `scene_lib.py`, `voxkit.py`, `geom.py`, `stills_units.py`.

Conventions: Blender (X, Y, Z) = engine (x, z, y); team colours 0 blue #2F6BFF, 1 red #F0302C, 2 green #18B45A, 3 purple #A64BF0 (in levels the contestant is team 0, the defenders team 1); the HUD sits at the top, the bottom is left free for subtitles.

## Differences from production

- Local paths became environment variables and PATH: no default locations for Blender, ffmpeg or Python; intermediate frames go to `renderer/renders/`; levels and maps are read from `battle/workspace/` (both `levels/` and the engine's original `关卡/` folder name are accepted); generals from the repository's `characters/`. The scene-theme folder was renamed from `关卡/` to `themes/`.
- Two production-only scripts are not included: `make_samples.py` (re-rendering the show's sample clips) and `stills_scenes.py` (scene stills for the eight maps), which depend on the organizer's own demo replays.
- The AI general in the `stills_units.py` line-up is `fable` instead of `opus` (there is no opus in `characters/`).
- A few comments that named another episode or its props (`looks.py`, `voxkit.py`) were made generic.

Third-party dependencies: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
