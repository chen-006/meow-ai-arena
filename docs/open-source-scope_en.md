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

## What is not published

- Voice-over, music, narration scripts, final cuts and editing projects.
- Contestants' raw session logs: conversations, reasoning, tool-call logs.
- The organizer's operations scripts, i.e. programs that exist only to run the contest: driving each vendor's command-line tool, timing and collecting submissions, checking account balances.
- Keys, tokens, account details, internal notes and one-off scripts.
- Third-party material is not bundled. Dependencies such as physics libraries are referenced by name and version with their source and license; Blender is installed by the user, and the README names the version it was tested with.

## When it is published

Together with the video. Nothing from an episode that hasn't aired yet is published ahead of time: not the task, not the contestant package, not the results.

## Licenses

Licensing follows directory boundaries:

| What | License | License text |
|---|---|---|
| The game itself (engines, rules, maps and levels), contest materials, contestant submissions, referee and scoring, results, documentation, community submissions | MIT | [`LICENSE`](../LICENSE) |
| The renderer and shared art assets: code, scene themes, unit and item models and render scripts under each episode's `renderer/` | CC BY-NC 4.0 (attribution, non-commercial) | [`LICENSES/CC-BY-NC-4.0.txt`](../LICENSES/CC-BY-NC-4.0.txt) |
| The voxel characters: everything under the top-level `characters/` | CC BY-NC-SA 4.0 (attribution, non-commercial, share-alike) | [`LICENSES/CC-BY-NC-SA-4.0.txt`](../LICENSES/CC-BY-NC-SA-4.0.txt) |

- **MIT**: use, modify and redistribute freely, including commercially, as long as the copyright and license notice is kept.
- **CC BY-NC 4.0**: copy, modify and redistribute, with attribution, for non-commercial purposes only.
- **CC BY-NC-SA 4.0**: as CC BY-NC 4.0, and adaptations you share must use the same license. The voxel characters are derivative works of the character designs in the videos, and some of those designs are themselves licensed under CC BY-NC-SA 4.0, so the whole set uses this license rather than a different one per character.
- Images and videos made with the renderer or the voxel characters are likewise non-commercial only and need attribution. Footage that shows voxel characters is also subject to CC BY-NC-SA 4.0.
- The non-commercial terms do not reach the games themselves: if you only use the engines, rules, maps and referees, without the renderer or the characters, only MIT applies.
- Every directory under a non-commercial license carries a `LICENSE.md` that names the license and how to give credit; directories that use third-party dependencies also carry a `THIRD_PARTY_NOTICES.md` listing each one's name, version, source and license.
- A few directories have their own license, stated inside them, for example the jDip map graphics (GPL) in the episode 4 replay pages and the community contest `community/contests/rmuc2026/` (Arena-1 non-commercial learning license).

### How to give credit

- Renderer and shared assets: `meow-ai-arena (AI Coding Arena), CC BY-NC 4.0`, with a link to `https://github.com/chen-006/meow-ai-arena`.
- Voxel characters: `Voxel characters made by meow-ai-arena (AI Coding Arena), CC BY-NC-SA 4.0; original design: <creator>`.
- The original design source for each character is listed in `characters/CREDITS.md`. Most character designs in the videos come from the Bilibili creator ZipZipPipe ([profile](https://space.bilibili.com/4168597)); for the whale girl, write "ZipZipPipe, based on an original character by 上善无形 ([b23.tv/3dNz55h](https://b23.tv/3dNz55h))". Where the original creator has published terms of use, `CREDITS.md` states them too; where they haven't, it gives the source only.
- Model and company names in character names belong to their owners and only indicate which model a character stands for. The voxel characters do not use any company's logos.

## Which episodes this applies to

- The full scope applies from the first episode released after episode 9; the renderer and voxel characters are published from that episode on, together with its video.
- Episodes 1–9 will not get a renderer or voxel characters retroactively. Their other materials were already published to roughly the same scope; where an episode falls short (for example, a final tournament that can only be re-run in full with the organizer's control script), its README says so in its section on differences from the contest or in its reproduction notes.
- The voxel characters are shared across episodes, so they live in the top-level `characters/`, and new characters are added there.

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
├── renderer/                       The renderer (CC BY-NC 4.0)
│   ├── README.md                   Render commands, dependencies, tested Blender version
│   ├── LICENSE.md                  CC BY-NC 4.0 and how to give credit
│   ├── THIRD_PARTY_NOTICES.md      Third-party dependencies it uses
│   ├── src/                        Replay-to-footage code
│   ├── themes/                     Scene themes
│   └── models/                     Unit and item models
├── challenge.py                    (optional) One-click challenge: play your program against every contestant
└── appendix/                       (optional) Side experiments that don't count toward the results
```

Shared material lives at the top level:

```text
characters/                 Voxel characters for each AI (CC BY-NC-SA 4.0)
├── README.md               File formats, how to load them, how to use them in a renderer
├── LICENSE.md              CC BY-NC-SA 4.0 and how to give credit
├── CREDITS.md              Original design source for every character (and the creator's published terms, if any)
└── <character>/            Model files, voxel data, preview images
LICENSES/                   Full texts of CC BY-NC 4.0 and CC BY-NC-SA 4.0
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
