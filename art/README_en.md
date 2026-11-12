[简体中文](README.md)

# Character illustrations, expression sheets and image-generation prompts

From episode 10 (*Armchair Generals*, 纸上谈兵) on, the characters that stand for each AI in the videos use this new set of designs. This folder holds the delivery-version illustrations, the cut-out expressions, the prompts and model parameters actually used for the final images, and the scripts that generate, tidy and slice them. Like the rest of the repository it is released under the [MIT license](../LICENSE).

**Credit**: Character design: 小叶子XIYZ (Bilibili); art and production: 喵了个猫了个咪了个猫. The new designs are based on 小叶子XIYZ's designs and were made by the show. Please include these two lines and a link to this repository when you use them.

Model and company names in character names belong to their owners and only indicate which model a character stands for; they do not imply endorsement by or involvement of those companies.

## Contents

| Path | Contents |
|---|---|
| [portraits/](portraits/) | 22 illustrations (delivery version): 1672×941 transparent PNGs, figure 927 px tall and centred horizontally, ready to drop into a 16:9 frame |
| [expressions/<character>/](expressions/) | Expressions for 13 characters: 12 transparent 1024×1024 busts each (cut flat at the chest, which sits on the bottom edge), plus 512×512 square and round avatars `头像_方.png` and `头像_圆.png`; [预览_总览.jpg](expressions/预览_总览.jpg) is an overview |
| [prompts/portraits/](prompts/portraits/) | Illustration prompts, verbatim |
| [prompts/expressions/](prompts/expressions/) | Expression-sheet prompts, verbatim; `军需官_表情名.json` holds the Quartermaster's own 12 expression names |
| [prompts/jobs_portraits.txt](prompts/jobs_portraits.txt), [jobs_expressions.txt](prompts/jobs_expressions.txt) | The job lists of the runs that produced the final images: one line per image, `name\|prompt file\|reference images,…\|size` |
| [tools/](tools/) | Scripts that generate, tidy and slice the images |

## Characters

| File name | Character | Used in this episode by | Design basis |
|---|---|---|---|
| `Claude_特大_Fable` | Fable, eldest of the Claude sisters | Claude Fable 5.2 | 小叶子XIYZ's Claude design |
| `Claude_大_Opus` | Opus, the middle Claude sister | — | same |
| `Claude_小` | Claude little sister | — | same |
| `GPT_大_Astra` | Astra, elder GPT sister | — | 小叶子XIYZ's GPT design |
| `GPT_小_Sol` | Sol, little GPT sister | GPT-6.2 Sol | same |
| `DeepSeek_大`, `DeepSeek_小` | DeepSeek, elder and little sister | DeepSeek-V4.1-Flash (elder) | 小叶子XIYZ's DeepSeek design |
| `Gemini_大`, `Gemini_小` | Gemini, elder and little sister | Gemini 4 Carbon (elder), Gemini 4 Hydrogen (little) | 小叶子XIYZ's Gemini design |
| `Kimi_大`, `Kimi_小` | Kimi | Kimi K3.1 Pro (elder) | Designed by the show following 小叶子XIYZ's design system |
| `千问_大`, `千问_小` | Qwen | Qwen4-Max-Preview (elder) | same |
| `文心_大`, `文心_小` | ERNIE | ERNIE 5.5 Flash Preview (elder) | same |
| `GLM_大`, `GLM_小` | GLM | — | same |
| `Grok_大`, `Grok_小` | Grok | — | same |
| `MiMo_大`, `MiMo_小` | MiMo | — | same |
| `军需官` | The Quartermaster, the show's own host character (stands for no vendor) | Host | Original to the show |

"大" (big) is the adult elder-sister version and "小" (little) the roughly 10-year-old little-sister version; expressions exist for the elder versions, little Sol and the Quartermaster. The brand-logo hair ornaments are drawn after each company's logo.

## Model and parameters

- Model: OpenAI image API `gpt-image-2.6-sunburst`, through `images/edits` when reference images are given.
- Illustration masters: `size 2160x3840`, `quality max`, `background transparent`, `output_format png`, `n 1`. [tools/finalize.py](tools/finalize.py) turns a master into the delivery version: normalise edge alpha, drop small fragments not connected to the figure, scale the figure's bounding box to 927 px tall and place it on a 1672×941 transparent canvas.
- Expression-sheet masters: `size 3456x2304`, otherwise the same. Each character has two 3×2 six-panel sheets (A: neutral, happy, smug, angry, sad, shocked; B: speechless, thinking, drooling, hit by a basin, awkward laugh, sleepy; the Quartermaster has her own set), which [tools/slice.py](tools/slice.py) cuts into 12 images by connected components, scaling both sheets alike, and then crops the face from "neutral" for the avatars.
- The masters themselves (2160×3840, 3456×2304) are not in the repository.

## Prompts

Each prompt file is the complete text (character section + reference-image notes + shared style block) sent to the API as is.

| Image | Prompt |
|---|---|
| Elder versions: Fable, Opus, DeepSeek, Gemini, Kimi, GLM, Grok, Qwen, MiMo | `v3_cur_<name>.txt` (third-round design, final) |
| ERNIE elder | `v3_ernie_da.txt` |
| Little versions: Claude, DeepSeek, Gemini, Kimi, GLM, Grok, Qwen, MiMo, ERNIE | `v3_small_<character>.txt` |
| GPT elder Astra, little Sol | `full_gpt_astra.txt`, `full_gpt_sol.txt` (final from the first round, never redone) |
| Quartermaster | `quartermaster_A.txt` (option A of two) |
| Intermediate Gemini elder draft (only used as a reference for the third-round Gemini, not a final image) | `v2_gem_da.txt` |
| Expressions | `prompts/expressions/<character>_A.txt`, `_B.txt` |

- Shared style block: [`_style_v3.txt`](prompts/portraits/_style_v3.txt) (third round) was derived from [`_style.txt`](prompts/portraits/_style.txt) (first round); the two GPT images use the first-round block. The Quartermaster uses a variant of the third-round block (she belongs to no vendor, so the logo requirement is replaced by a fictional coin emblem), already merged into `quartermaster_A.txt`.
- `gpt_astra.txt`, `gpt_sol.txt` and `v3_ernie_da_char.txt` are character sections; [tools/build_prompts.py](tools/build_prompts.py) takes the design points from them and from `v3_cur_*.txt` to assemble the expression prompts. Re-running it reproduces all 24 files in `prompts/expressions/` word for word. The Quartermaster's two expression prompts came from a one-off script; their text is here.

## Reference images (not in the repository)

None of the reference images in the job lists are included. What each kind was:

| File in the job list | What it is |
|---|---|
| `refs/tri_GPT.jpg`, `tri_DeepSeek.jpg`, … | Character turnaround sheets drawn by 小叶子XIYZ (third-party work, not included) |
| `refs/logo_<vendor>.png` | Company logos, only used for the shape of the hair ornament (trademarks, not included) |
| `refs/style_Astra.png`, `style_Sol.png` | The elder GPT sister Astra (original design by 小叶子XIYZ) and little sister Sol illustrations used in episodes 1–9, as style references |
| `refs/anchor_*.png`, `cur_*.png`, `oldsmall_*.png`, `draft_*.png`, `elder_*.png` | Earlier versions from the same redesign (white-dress illustrations, drafts) used as style and proportion references; superseded by the third round, not included |
| `refs/elder3_<character>.png` | Third-round elder masters downscaled to 1080×1920, used as references for the little versions |
| `raw/Gemini_大_猫娘__*.png` | The intermediate Gemini elder draft listed above |
| `refs/upper_<character>.png`, `full_<character>.png` in the expression jobs | Upper body and full figure cropped from the master by [tools/prep.py](tools/prep.py), on a light grey background |

## Regenerating

This calls a paid API (about USD 0.4–0.5 per illustration and about USD 0.49 per expression sheet); decide before you run it. Tested on Windows 11 with Git Bash and Python 3.12 with `requests`, `pillow`, `numpy`, `opencv-python`.

```text
# In an empty working directory: copy art/prompts/portraits/ to prompts/ and put your own reference images in refs/
export OPENAI_API_KEY=...                         the key is only read from the environment
bash <repo>/art/tools/batch.sh jobs.txt              illustration masters into raw/ (job format as in jobs_portraits.txt)
python <repo>/art/tools/finalize.py raw/<master>.png <name>   delivery version into portraits/, portraits_2x/
# Expressions: put the illustration masters in master/<character>.png
python <repo>/art/tools/prep.py                    crop references into expr/refs/
python <repo>/art/tools/build_prompts.py           assemble prompts into expr/prompts/
cd expr && bash <repo>/art/tools/batch.sh jobs.txt && cd ..   expression masters into expr/raw/ (format as in jobs_expressions.txt)
python <repo>/art/tools/slice.py <character>       cut into expressions/<character>/
python <repo>/art/tools/preview.py                 previews
```

The scripts read and write under the current directory; output folders can be changed with `ART_OUT` (finalize), `EXPR_OUT` (slice, preview) and `PERSONA_MASTER` (where prep reads the masters).

## Differences from production

- Absolute paths on the organizer's machine became the current working directory and environment variables; the API key, which used to be read from a local file, is read from `OPENAI_API_KEY`; a hard-coded local proxy setting was removed.
- The expression prompt builder was called `build_prompts_v3.py` and is `build_prompts.py` here (the first-round version is not included); `slice.py` gained one feature: if `expr/<character>_表情名.json` exists, its names are used (in production the Quartermaster's names were swapped in by hand).
- The job lists merge the job lines of the runs that produced the final images; reference-image paths are kept as they were.
- Drafts, the older white-dress version, rejects, master originals, call logs and cost tallies are not included.
