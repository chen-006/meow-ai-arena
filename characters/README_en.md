[简体中文](README.md)

# Voxel characters for each AI

The block-built (voxel), big-headed characters that stand for each AI in the videos; in episode 10 (*Armchair Generals*) they appear as the general (renderer option `--lord <name>`). They were made from the new illustrations in [art/](../art/README_en.md); start with [合影.png](合影.png) (the lineup, true to scale). Released under the [MIT license](../LICENSE) like the rest of the repository; credits are in [CREDITS.md](CREDITS.md).

| Name | Based on illustration | How to recognise them |
|---|---|---|
| `sol` | GPT_小_Sol | Pale mint short hair, ahoge, low little braids with green bows, big green-and-white round ornament; green cardigan, white sailor collar, white skirt with green trim; the smallest |
| `fable` | Claude_特大_Fable | Apricot-orange long hair, side bun with an orange hairpin and white ribbon, round gold glasses; cream long dress, terracotta cloak with gold trim, a book held to her chest; the tallest |
| `gemini_carbon` | Gemini_大 | Wavy rainbow hair (red on top to blue at the tips), orange cat ears, rainbow four-pointed star, rainbow cat tail; colour-block skirt; one yellow-striped and one blue leg |
| `gemini_hydrogen` | Gemini_小 | The younger version of the above: short rainbow hair, low braids with yellow bows, ahoge, cat ears, star, tail; small |
| `qwen` | 千问_大 (Qwen) | Pale purple hair, top bun with a silver hairpin, purple hexagonal ornament with a tassel, a thick braid over the right shoulder; white top, purple sash, skirt fading to purple; round fan |
| `deepseek` | DeepSeek_大 | Icy blue-white wavy hair, low braids with blue bows, blue whale clip; deep-blue jacket (hood bunched at the neck, little fins), white sailor top, navy pleated skirt; whale bag at the hip |
| `kimi` | Kimi_大 | Pure white straight hime-cut hair, a silver crescent at the top of the head, black square clip; midnight-blue starry dress, sheer white capelet, black bow |
| `ernie` | 文心_大 (ERNIE) | Bright blue bob, two small buns with white ribbons, hexagonal clip; houndstooth capelet, navy culottes; gold magnifying glass, brown satchel |

The names are the episode 10 seat names. Brand logos from the illustrations are not copied; they become plain round, hexagonal, star or square ornaments in the same colours.

## Four files per character

- `<name>.glb`: the model. Each movable part is a separate node (`body`, `head`, `hairback` long hair at the back, `tailL` / `tailR` twin tails or side braids, `tail`, `armA` / `armB`, `footA` / `footB`, and three face sets `face_open` / `face_tight` / `face_happy`, one shown at a time), with each node's origin at its joint (neck, shoulder, hip, hair root), so rotating a node poses it. Colours are vertex colours (`COLOR_0`); no textures, no outline. Faces glTF +Z (−Y in Blender), feet at the origin, one block is 0.07 units, a character is about 0.9 units tall.
- `<name>.json`: the voxel data itself (each part's pivot, block coordinates and colours, single-face recolours, thin decals), independent of any software. `from_json` in [src/voxkit.py](src/voxkit.py) loads it back into a model; the episode 10 renderer reads it this way.
- `<name>_正面.png` (front), `<name>_斜45.png` (45°): 1024×1024 transparent previews with a dark outline, usable as textures, avatars or cover art.

## Using them in the renderer

```text
battle\renderer\出片.cmd battle\replays\L4_gemini_carbon.wrp -o L4.mp4 --lord gemini_carbon
```

The renderer looks for `<name>.json` in this folder by default; point `A12_LORD_DIR` elsewhere or pass a `.json` path to `--lord`.

## Source

- [src/voxchars/<name>.py](src/voxchars/): one file per character: display name, main colour and palette, then the code that places the blocks (`build(team=None)` returns a model). To add a character, copy a file and add the name to `KEYS` in `voxchars/__init__.py`.
- [src/voxkit.py](src/voxkit.py): voxel modelling kit (building blocks for heads, faces, limbs and hairstyles; JSON export and import).

Design conventions: an 8 × 7 × 7 head and a 4-wide body, big-headed chibi style; the face is just 1 × 2 eyes, a blush block and a dot of a mouth. Five to eight colours per character, shading from lit faces and colour blocks, no gradients. Characters are told apart by the colour on top of the head, the volume of the hair and the emblem on the head (the top of the head takes up the most area in overhead shots).

## Differences from production

The scripts that export the models to `.glb` and render the previews and the lineup (`export_voxchars.py`, `lineup.py` and so on) need the character-assembly modules from another episode's renderer, which has not aired yet, so they are not included for now. The `.glb`, `.json` and preview files here are their output; the `.json` files load with `voxkit.from_json` and work directly in the episode 10 renderer. Three comments in `voxkit.py` and `voxchars/__init__.py` that mentioned that episode's props were made generic.
