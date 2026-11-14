简体中文: [open-source-scope.md](open-source-scope.md)

# Open-source scope

This page describes what each episode publishes and what it doesn't, when it is published, which license covers which part, and how each episode's folder should be laid out.

The goal is simple: anyone who clones this repository should be able to understand what each episode was about, recompute its leaderboard with a single command, and enter their own program or model under the same conditions.

## What each episode publishes

**The game itself**

- Engine source code and rule documents.
- Every map and level, including the ones that were hidden from contestants during the contest (hidden maps, hidden workloads, hidden scenarios).
- Local build and play tools: build scripts, the local referee, the replay viewer.

**Contest materials**

- The contestant package exactly as handed out, byte for byte. If anything was swapped for the open-source release (for example, a sparring bot that contestants only got as an executable is published as source), the episode README says so in its section on differences from the contest.
- The prompts, verbatim: every message the organizer sent to contestants, including the kickoff, half-time and resume messages.
- Contest conditions for every contestant: model, reasoning effort, access channel, tool and version, time limit, time actually used, and every interruption, time extension and incident. These go in `CONDITIONS.md`.

**Contestant submissions**

- Every contestant's code, design files and notes, unmodified.

**Referee and scoring**

- The referee, the schedule (including random seeds) and the scoring scripts. One command recomputes the leaderboard from the per-game results and checks it against the published tables.

**Results**

- Leaderboards, per-game results and representative replays. Replays are picked by a rule fixed in advance (a typical game from a given pairing or player count), not hand-picked comebacks and highlights.

**Renderer**

- All the code that turns replays into video footage, plus the scene themes, unit and item models, and the render commands.

**Voxel characters for each AI**

- The voxel (block-built) characters that stand for each model in the videos: model files, voxel data, the scripts that generate them, preview images, and the source of each character's original design.

**Character illustrations and expression sheets**

- The character illustrations made by the show (delivery versions) and each character's expressions (cut-out single images and avatars), including the show's own host character.

**Image-generation prompts and parameters**

- The prompts actually used for the final images, verbatim (including the shared style block), the model name, size, quality, background and other parameters, and the scripts that generate, tidy and slice the images. The scripts read the API key from an environment variable; no key is stored in the repository.
- Reference images used during generation are not bundled. Third-party ones (vendor logos, other people's design sheets) are only described; the show's own reference images (early drafts, downscaled or cropped masters) are not added separately either, and the README says what each one was.

**Track lists and voice settings**

- The list of background music and sound effects: title, composer, source and ID, and which episode used it.
- Text-to-speech settings: service and model, the voice used for each character, the prompts verbatim, post-processing parameters, and the per-line tone sheet.
- All of it lives in the top-level [`audio/`](../audio/README_en.md).

## What is not published

- Voice-over recordings and the music and sound-effect files (third-party assets or output of third-party services that may not be redistributed; the track list and voice settings are in `audio/`), narration scripts, final cuts and editing projects.
- Contestants' raw session logs: conversations, reasoning, tool-call logs.
- The organizer's operations scripts, i.e. programs that exist only to run the contest: driving each vendor's command-line tool, timing and collecting submissions, checking account balances.
- Keys, tokens, account details, internal notes and one-off scripts.
- Third-party material is not bundled. Dependencies such as physics libraries are referenced by name and version with their source and license; Blender is installed by the user, and the README names the version it was tested with.

## When it is published

Together with the video. Nothing from an episode that hasn't aired yet is published ahead of time: not the task, not the contestant package, not the results.

## Licenses

The whole repository uses a single license: [MIT](../LICENSE). Every category above is MIT: the games, contest materials, contestant submissions, referees and scoring, results, the renderer and art assets, character illustrations and expression sheets, voxel characters, image-generation prompts, documentation and community submissions.

- **MIT**: use, modify and redistribute freely, including commercially, as long as the copyright and license notice is kept. The same goes for images and videos you make with the renderer, the illustrations or the voxel characters.
- Third-party directories that carry their own license keep it, as stated inside them: the jDip map graphics in the episode 4 replay pages (GPL, rendered through the AGPL `diplomacy` package) and the community contest `community/contests/rmuc2026/` (Arena-1 non-commercial learning license).
- Third-party dependencies are not bundled and are used under their own licenses; directories that use them carry a `THIRD_PARTY_NOTICES.md` listing each one's name, version, source and license.

### How to give credit

- Code, rules and data: keep the MIT copyright and license notice.
- Character designs from episode 10 on (illustrations, expression sheets, voxel characters): `Character design: 小叶子XIYZ (Bilibili); art and production: 喵了个猫了个咪了个猫`, with a link to `https://github.com/chen-006/meow-ai-arena`. The new designs are based on 小叶子XIYZ's designs and were made by the show. Per-character notes are in `art/README_en.md` and `characters/CREDITS.md`.
- The older character designs in the episode 1–9 videos mostly come from the Bilibili creator ZipZipPipe ([profile](https://space.bilibili.com/4168597)). The only copies in this repository are the avatars that ship with the earlier episodes' renderers (each `renderer/assets/avatars/`, so the original look can be reproduced); they are not covered by the MIT license, and their credits and restrictions are in the `NOTICE.md` next to them.
- Model and company names in character names belong to their owners and only indicate which model a character stands for; they do not imply endorsement by or involvement of those companies.

## Which episodes this applies to

- The full scope applies from episode 10 (*Armchair Generals*, 纸上谈兵) on; the renderer, voxel characters, the new illustrations and expression sheets and the image-generation prompts are published from that episode on, together with its video.
- The episodes among 1–9 that had a standalone renderer got it added in November 2026: episode 3 (capture the flag), 6 (tanks), 7 (light cycles), 8 (soccer) and 9 (bridges), each under its `renderer/`. Voxel characters and illustrations are not added retroactively. Their other materials were already published to roughly the same scope; where an episode falls short (for example, a final tournament that can only be re-run in full with the organizer's control script), its README says so in its section on differences from the contest or in its reproduction notes. Old images and notes in those folders stay as they are.
- Voxel characters, illustrations, expression sheets and image-generation prompts are shared across episodes, so they live in the top-level `characters/` and `art/`, and new characters are added there.

## Folder layout for each episode

Future episodes follow the layout below. The folder is named after the game, in short lowercase English with hyphens between words, not after the episode number or an internal task code. Leave out any folder the episode has nothing for.

```text
<game>/
├── README.md, README_en.md         Opens with three things: what this episode is, what's in the folder, how to reproduce the leaderboard
├── CONDITIONS.md, CONDITIONS_en.md Contest conditions and every interruption, time extension and incident
├── prompt.md, prompt_en.md         The kickoff prompt, verbatim; add prompts/ when there were several messages
├── workspace/                      The contestant package as handed out: RULES.md, engine, local referee and play tools, template, replay viewer, public maps
├── engine/                         (optional) Engine source and build scripts, if the package only contained binaries
├── bots/                           Contestant submissions, unmodified; half1/ and half2/ for two-half contests; call it designs/ when the submissions aren't code
├── notes/                          (optional) Contestants' notes and pre-/post-match statements, unmodified
├── results/                        Leaderboards (.md for people, .json for programs) and per-game results (.jsonl.gz)
├── replays/                        Representative replays (.html, open in a browser); the README states how they were picked
├── tournament/                     Referee, schedule (with seeds), scoring scripts; maps and levels hidden during the contest go in tournament/hidden/
│   └── README.md                   The one command that reproduces the leaderboard, and the command that replays every game
├── renderer/                       The renderer
│   ├── README.md                   Render commands, dependencies, tested Blender version
│   ├── THIRD_PARTY_NOTICES.md      Third-party dependencies it uses
│   ├── src/                        Replay-to-footage code
│   ├── themes/                     Scene themes
│   └── models/                     Unit and item models
├── challenge.py                    (optional) One-click challenge: play your program against every contestant
└── appendix/                       (optional) Side experiments that don't count toward the results
```

Shared material lives at the top level:

```text
characters/                 Voxel characters for each AI
├── README.md               File formats, how to load them, how to use them in a renderer
├── CREDITS.md              Which illustration each character follows, and design credits
├── <character>.glb, .json, _正面.png, _斜45.png   Model files, voxel data, preview images
└── src/                    The scripts that generate them
art/                        Character illustrations, expression sheets, image-generation prompts and scripts
├── README.md               Character list, credits, model and parameters, what was used as reference
├── portraits/              Illustrations (delivery versions)
├── expressions/<character>/ Expressions (cut-out single images and avatars)
├── prompts/                The prompts and job lists actually used for the final images
└── tools/                  Scripts that generate, tidy and slice the images
arena/                      General-purpose tournament platform (used for the episode 3 official tournament)
community/                  Community submissions and the community leaderboard
docs/                       Cross-episode documentation (scoring methods, this page, ...)
```

A few more conventions:

- **README opening**: the first paragraph says what the episode is about, how many contestants and how many games; then a "what's in this folder" table; then the command that reproduces the leaderboard. Rules, results, replays and how to play come after that.
- **Reproducing the leaderboard**: the first command in `tournament/README.md` reads the per-game results in `results/`, recomputes the standings and checks them line by line against the published tables, finishing in minutes. Replaying every game, if that takes long, gets its own command with an estimated running time.
- **Originals keep their names**: the contestant package, submissions and the referee's contest-time output keep their original file names, Chinese ones included. Documentation files have English names, and the English version gets an `_en` suffix.
- **Naming contestants**: contestant folders or files use the model's short name (lowercase with hyphens, e.g. `gpt-6-astra`, `opus-5.5`), with a suffix when the same model ran with different tools or more than once; the README's "Contestants" section has the mapping.
- **What changed for the release**: anonymized paths, added source code, viewer fixes and so on are all listed in the README's section on differences from the contest.
- **Community submissions**: if the episode accepts them, they go in `community/<game>/`, under the rules in [`community/README_en.md`](../community/README_en.md).
