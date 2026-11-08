[中文版](README.md)

# Community submissions

Viewers can submit their own work here: a bot you wrote, a bot you had an AI write, or your optimized version of the warehouse code. Submissions come in as pull requests and, once merged, appear on the [community leaderboard](LEADERBOARD.md) (auto-generated, in Chinese). The full guide is in Chinese: [README.md](README.md). This page is a short version.

You can do it by hand, or paste the prompt in [`AGENT_PROMPT.md`](AGENT_PROMPT.md) (English prompts are in the second half) into your coding agent (Claude Code, Codex, Cursor, ...) and let it do the work. Submitted code and contributors' `NOTES.md` remain as supplied.

## What you can submit

| Episode | Folder | What to submit |
|---|---|---|
| 1 · Territory | `community/land/` | one `bot.cpp` |
| 2 · Bomberman | `community/bomberman/` | one `bot.cpp` |
| 3 · Capture the flag | `community/ctf/` | one `bot.cpp` |
| 5 · Warehouse scheduling | `community/warehouse/` | a `src/` folder with your optimized program |
| 6 · Tank Battle | `community/tank/` | one `bot.cpp` implementing only `init` and `decide` |
| 7 · Tron | `community/tron/` | one `bot.cpp` implementing only `decide`; Windows required for local evaluation |

Episode 9 (bridge building) does not take pull requests: submissions go to the website leaderboards for levels 1–6; see [`bridge/README_en.md`](../bridge/README_en.md#submit-your-own). The prompt for coding agents is in [`bridge/AGENT_PROMPT.md`](../bridge/AGENT_PROMPT.md).

Hand-written bots, AI-written bots, modified versions of the official bots, optimized versions of the warehouse baseline, and fixed versions of a model's code with known defects are all welcome. Episode 4 (Diplomacy) has nothing to submit. Malicious code, code that breaks the episode's rules (file access, network access, spawning processes), and uncredited copies of other people's work are not accepted.

## Layout

```text
community/<episode>/<github-username>__<entry-name>/
    bot.cpp        (bot episodes; single file, up to 256 KB)
    src/           (warehouse; flat folder of .cpp/.h/.hpp/.md, up to 768 KB in total)
    meta.json      (required)
    NOTES.md       (optional)
```

`<episode>` is `land`, `bomberman`, `ctf`, `warehouse`, `tank` or `tron`.

Both parts of the folder name use only lowercase letters, digits and single hyphens, joined by two underscores. The first part must be the GitHub account that opens the pull request, in lowercase.

```json
{
  "author": "Display name",
  "github": "your-github-username",
  "name": "Entry name shown on the leaderboard",
  "description": "One sentence about the approach.",
  "written_by": "human",
  "model": "",
  "tool": "",
  "based_on": "",
  "license": "MIT"
}
```

`written_by` is `human`, `ai` or `human+ai`; fill it in honestly. `model` is required unless `written_by` is `human`. `based_on` names the code you started from, if any. `"license": "MIT"` means you agree to publish the entry under this repository's MIT license.

The code must follow the episode rules: C++20, compiled with `g++ -O2 -std=c++20`, no file or network access. Episodes 1–3 and warehouse use only the standard library. Tank and Tron also use the official `tank.hpp` or `tron.hpp`; follow their workspace rules for permitted dependencies and threading. Bots get 2 seconds for the first turn and 50 ms per turn afterwards. The warehouse program must produce byte-identical output to `warehouse/workspace/baseline/` within 1 GB of memory. The authoritative rules are in each episode's `workspace/` (Chinese).

For Tank, `bot.cpp` includes `"tank.hpp"` and defines `void init(const tank::State&, int)` and `tank::Orders decide(const tank::State&, int)`. Do not submit your own `main`, header or driver. The checker compiles your source with the trusted `tank/workspace/tank.hpp` and `tank/workspace/tank_main.cpp`, using the equivalent of `g++ -O2 -std=c++20 -I<tank/workspace> bot.cpp <tank/workspace>/tank_main.cpp`. Evaluation uses native formal-mode processes and the unchanged CPU limits: 2000 ms for startup, `init` and the first `decide`, then 50 ms per turn. Tank permits threads, with CPU time summed across all threads; a source-scan warning is not a ban on threads. Fast mode does not enforce time limits and is not used for community scores. Tank community evaluation runs on Linux, so Windows-specific headers are not supported.

For Tron, `bot.cpp` includes `"tron.hpp"` and defines `int decide(const tron::State&)`, returning `tron::U / R / D / L`. Do not submit your own `main`, header or driver. The checker uses the trusted `tron.hpp` and `tron_main.cpp`. **Tron local compilation (including `--skip-eval`) and evaluation require Windows and g++**; CI uses a standard GitHub-hosted `windows-2025` VM, while the other episodes remain on Linux. Formal mode preserves the original 2000/50 CPU ms limits, including startup on turn 0 and all threads in the process. Threads are allowed. A timeout or invalid direction keeps the previous direction for that turn (`U` on turn 0), rather than immediately losing the game; timeouts, invalid outputs and crashes are reported.

## Test locally

```bash
python community/check.py community/<episode>/<your-folder>
```

This validates the folder, runs the episode's `challenge.py` for bots, and prints a Markdown summary. Territory, Bomberman and Capture the flag run a small tournament with the official field. Tank plays only your entry against 16 opponents (14 second-half bots, the reference bot and the baseline): 20 games each, 320 total; `--full` runs 80 each, 1,280 total. Tank uses formal mode and fixed final-tournament reference ratings, as described below. Tron plays 82 formal-mode games (52 1v1, 24 four-player, six twelve-player); `--full` plays 328. Its 13 opponents are the ten official contestants, two reference contestants and `sparring_hard`. For the warehouse, baseline and submission run on your machine, outputs are compared, then speedups computed. `--skip-eval` only checks structure and compilation. `--full` plays more games, or runs all eight hidden warehouse workloads instead of the default quick set (five public workloads plus the three fastest hidden ones; the baseline is very slow).

Results are saved in `community/.runs/<episode>/<your-folder>/result.json` and `summary.md` (ignored by Git). Examples are in the [example guide](_example/README_en.md), including Tank and Tron starters.

## Open a pull request

1. Fork the repository, clone your fork, create a branch.
2. Add your folder, run `check.py` until it passes.
3. Commit only that folder, push, open a pull request against `main`, and paste the summary into the description.

One entry per pull request, and do not touch other files in the same pull request.

After that: GitHub Actions checks that only your folder changed and runs the target branch's trusted `check.py`, official opponents and Tank / Tron support files on a GitHub-hosted VM; only the entry folder comes from the PR. Tank / Tron submissions therefore require the corresponding support change to have been merged into the target branch first. Update to the latest supporting `main`; do not include evaluation-script changes in your submission PR. A maintainer reviews the code; after the merge the entry is evaluated again and the leaderboard is regenerated.

## About the numbers

Community results are measured on GitHub-hosted VMs with fewer games per bot. **They are not directly comparable with the official results from the show.** Territory, Bomberman and Capture the flag ratings come from a small tournament of your entry and the official field (baseline = 0). Community entries do not play each other.

Tank instead uses a **fixed final-tournament reference**: the 16 opponent ratings from `tank/results/final/成绩.json` stay fixed, and only the entrant's Bradley–Terry rating is fitted (baseline = 0, ten times stronger = +400). The opponent numbers in the summary are historical reference ratings, not ratings newly measured in this community run. The estimated placement compares only the 14 ranked contestants, excluding baseline and sparring. This is not a refitted mini-tournament or an official final-tournament placing: the maps, game counts and hardware differ.

Tron likewise keeps the **final strong-opponent board** ratings in `tron/results/results.json` fixed and fits only the entrant, separately by game type. The weighted total is 50% 1v1, 30% four-player and 20% twelve-player (Doubao = 0, ten times stronger = +400). `sparring_hard` participates in games and fitting but is a reference line, excluded from the estimated rank among the 12 contestants. The “3–4” category uses only four-player games. The archived workspace rules retain the pre-tournament greedy = 0 scale; community scores follow the post-tournament strong-opponent board used by `tron/challenge.py`, without changing those historical files. New maps, fewer games and different hardware mean this is not an official tournament placing.

Warehouse speedups are the ratio of baseline to submission CPU time on the same machine.

## License and safety

Entries are published under the repository's [MIT license](../LICENSE). Submissions are C++ programs written by strangers: do not build and run them on a machine you care about; use a VM or a container. Evaluation jobs use read-only tokens, no secrets and no persisted checkout credentials. After merge, each entry runs in a separate disposable VM; only the separate publishing job can write, and it validates results without compiling or running submissions. This isolation is not a program sandbox, and passing CI does not replace code review.
