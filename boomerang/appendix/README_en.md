[简体中文](README.md)

# Appendix (not scored)

The official ranking contains only the thirteen in [bots/](../bots/README_en.md). This folder holds work made from the same player pack and the same kickoff message that does not count toward the official ranking; the video's review mentions some of it.

## Programs

| Folder | What it is | Seat | How it was made |
|---|---|---|---|
| [bots/gpt-6.2-astra-grey](bots/gpt-6.2-astra-grey/) | **Preview build** of GPT-6.2 Astra | `astra` | Before the official 6.2 release, on 11-16, via Codex CLI 0.167.2 through a third-party API relay chosen by the organizer, `gpt-6.2-astra`, max, first kickoff batch |
| [bots/gpt-sol-grey-desktop](bots/gpt-sol-grey-desktop/) | **Preview build** of Sol | `sol` | Started by hand in the Codex desktop app on 11-16, max, first kickoff batch. The request name in the desktop app's log is `gpt-6.1-sol`; at the time the organizer believed the app was serving a 6.2 Sol preview. The combined board calls it "Sol 6.1 preview" |
| [bots/gpt-6-astra](bots/gpt-6-astra/) | Control: GPT-6 Astra | `astra6` | 11-16, Codex CLI (the organizer's ChatGPT subscription), `gpt-6-astra`, max, second kickoff batch |
| [bots/gpt-6.1-sol](bots/gpt-6.1-sol/) | Control: GPT-6.1 Sol | `sol61` | As above, `gpt-6.1-sol` |
| [bots/gpt-6.1-terra](bots/gpt-6.1-terra/) | Control: GPT-6.1 Terra | `terra` | As above, `gpt-6.1-terra` |
| [bots/gpt-6-luna](bots/gpt-6-luna/) | Control: GPT-6 Luna | `luna` | As above, `gpt-6-luna` |
| [bots/claude-sonnet-5.5](bots/claude-sonnet-5.5/) | Control: Claude Sonnet 5.5 | `sonnet` | 11-16, Claude Code CLI (`claude -p`), `claude-sonnet-5-5`, max, second kickoff batch |
| [bots/claude-haiku-5.5](bots/claude-haiku-5.5/) | Control: Claude Haiku 5.5 | `haiku` | As above, `claude-haiku-5-5`; stopped at minute 34 and was called back |
| [bots/deepseek-v4.1-flash-opencode](bots/deepseek-v4.1-flash-opencode/) | DeepSeek's **OpenCode version** (void) | `deepseek` | 11-16, first batch, OpenCode (`deepseek/deepseek-flash`, max); stopped at minute 42 and was called back. It later redid the task with its own tool dsh; the official result uses the dsh version |
| [bots/kimi-k3.1-pro-opencode](bots/kimi-k3.1-pro-opencode/) | Kimi's **OpenCode version** (void) | `kimi` | 11-16, first batch, OpenCode (`moonshotai-cn/kimi-k3.1-pro`, max); interrupted at minute 54 by a problem with the organizer's API account, then resumed. It later redid the task with Kimi Code |
| [early/](early/) | Versions saved when a contestant stopped before minute 60 or was interrupted | | Folder names are the original names from the event (`<submission folder>_第一次交卷_<minute>` = first hand-in, `<submission folder>_中断时_<minute>` = at interruption); some kept only `player.cpp`, Haiku's only has the submission record |

The six control players all used the organizer's subscriptions, worked in the same time slot (12:45–14:03) and had 75 minutes.

## Results

| Folder | Contents |
|---|---|
| [results/灰度版海选/](results/灰度版海选/) | A full qualifier played on 11-16 with the preview builds of Astra and Sol (same schedule as the official one, 32,904 games): `海选成绩.json` and per-game scores `obs.json.gz`. The first auction on 11-16 (the rehearsal that was later voided) used these results |
| [results/对照赛/](results/对照赛/) | The 19-player event with the six control players added: `对照赛成绩.json` and the newly played games `obs_新打的.json.gz`. One-on-one games among the thirteen are reused from the preview qualifier |
| [results/全员大混战/](results/全员大混战/) | One board for all 21: the official thirteen (Astra and Sol as the 6.2 release) + the two preview builds + the six controls, 103,908 games (87,660 reused, 16,248 added); zero is the average of the official thirteen. `全员榜.md/json`, pairwise win-rate tables, and the added games `obs_补打.json.gz` |

`obs*.json.gz` is the input of the ranking program, one item per game: `[{"mode", "map", "names"?}, {player: score}]`. In one-on-one the score is minus the placing; in four-player games it is placing points plus kill points.

**Reproduce the combined board**: `python appendix/score_all21.py` (about 10 seconds) joins the data above with the official qualifier's per-game results, recomputes with the official ranking program and checks every value against `全员榜.json`. It prints "核对通过" (check passed). The preview qualifier and the control event come with data only, without a separate check script.

Top of the combined board: GPT-6.2 Astra +303, Astra preview +283, Fable +128, Sol preview +95, Opus +75, Sonnet (control) +61, GPT-6 Astra (control) +58, Terra (control) +51, GPT-6.1 Sol (control) +50, Carbon +45, GPT-6.2 Sol +32. Each model did the task once; the same model can do very differently on a second attempt, so these numbers do not rank versions against each other.
