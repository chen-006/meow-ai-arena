[简体中文](README.md)

# Character art v2 (from episode 11)

The character illustrations and expression sheets used in the videos from episode 11 (*Boomerang Brawl*, 回旋镖大作战) on. Compared with the set one level up in [art/](../README_en.md) (used in episode 10), GPT's Astra and Sol and Doubao are unchanged, while ten characters got new outfits, props and overall designs; hair colour, hairstyle and each vendor's brand elements stay recognisable. Start with [全家福.jpg](全家福.jpg) (group picture) and the [expression overview](expressions/总览.jpg).

**Credit**: Character design reference: 小叶子XIYZ (Bilibili); art and production: 喵了个猫了个咪了个猫. Released under the [MIT license](../../LICENSE); when you use it, include these two credits and a link to this repository. Model and company names in character names belong to their owners and only say which model a character stands for; they do not imply that these companies endorse or take part in this project. The brand-logo hair ornaments are drawn after each vendor's logo.

## Contents

| Path | Contents |
|---|---|
| [portraits/](portraits/) | Full-size illustrations of ten characters, transparent PNG (about 1024×1536): Opus, Fable, DeepSeek, Gemini, Kimi, GLM, Grok, Qwen, MiMo, Ernie |
| [portraits_small/](portraits_small/) | Small (younger) versions of nine: Claude, DeepSeek, Gemini, Kimi, GLM, Grok, Qwen, MiMo, Ernie. In episode 11, Gemini 4 Hydrogen uses `Gemini_小` |
| [expressions/<character>/](expressions/) | Expression sheets for the ten full-size characters and small Gemini: 12 transparent single images each (`01_常态` neutral, happy, smug, angry, sad, shocked, speechless, thinking, drooling, basin on the head, awkward laugh, `12_犯困` sleepy); the ten full-size characters also have a 640×640 `头像.png` (avatar); [总览.jpg](expressions/总览.jpg), [Gemini_小_总览.jpg](expressions/Gemini_小_总览.jpg) |
| [prompts/portraits/](prompts/portraits/), [prompts/portraits_small/](prompts/portraits_small/) | The English prompts used for the final full-size and small versions, verbatim |
| [prompts/expressions/](prompts/expressions/) | The shared prompt for the expression master sheets (`表情母版说明.txt`; each character appends its own design paragraph), and small Gemini's per-image prompts and generation log |
| [大小版对照.jpg](大小版对照.jpg), [全家福.jpg](全家福.jpg) | Previews (full-size vs small; group picture) |

GPT's Astra (full-size) and Sol (small) are the episode-10 versions in [../portraits/](../portraits/) (`GPT_大_Astra.png`, `GPT_小_Sol.png`); Doubao keeps the realistic style of its official image and is not here.

## How it was made

- The images were generated with OpenAI's image model through Codex's built-in image generation (not via the API; no model name was returned). Several drafts were made, one picked, then the final. The prompts were written by the production team together with Gemini.
- Expression sheets: each character gets a 4×3, twelve-cell master sheet from `表情母版说明.txt`, cut into single images; the avatar is the face from "neutral". The master sheets are not in the repository.
- For the repository the single expression images were scaled from about 1254×1254 to fit within 1024×1024 (the same size as the episode-10 set); illustrations, small versions and avatars are the originals.
- Reference images used during generation (vendor logos, the previous illustrations, drafts) are not bundled: the logos belong to third parties and the drafts were superseded.
