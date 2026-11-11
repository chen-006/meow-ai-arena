[简体中文](README.md)

# Warehouse scheduling (episodes 5 and 7)

This directory holds two events built on the same task: optimize a slow, messy C++ warehouse-robot simulation while keeping its output byte-identical to the original ("baseline"), including five odd behaviours that the outdated docs describe differently.

- **Episode 5** (the main contents of this directory): 12 models in 6 two-model teams, three rounds (lead optimizes, tester hunts counterexamples, tester takes over and implements a requirement change). Full results, conditions, and reproducible materials are linked in the Episode 5 guide below. The original write-up is [`README.md`](README.md). Conditions and incidents: [English](CONDITIONS_en.md).
- **Episode 7 · Legacy-code optimization round**: described below. English result and package indexes are linked below; model-authored reports remain in their original language.

- **What's in this folder**: `workspace/` contestant package · `packages/` per-round task packages · `submissions/` every model's code and counterexamples · `referee/` referee, reference implementation and hidden workloads · `results/` results · `notes/` post-mortems · `appendix/` the human team; the episode 7 round is in `submissions/ep7/`, `results/ep7/` and `packages/ep7/`
- **Reproducing the leaderboard**: Restore the contest-time layout with `python referee/还原目录.py <empty folder>`, then recompute each round with the commands in [`referee/README_en.md`](referee/README_en.md).

## Episode 5 · Team optimization relay

Twelve models formed six teams, each with a lead programmer and a tester. Round 1: optimize the baseline while preserving its exact output. Round 2: test the other five teams' lead submissions for counterexamples. Round 3: take over the teammate's code, repair regression defects, implement a previously secret requirements change, and optimize further. Rounds 2–3 were rematches; the [conditions](CONDITIONS_en.md) document the voided attempts, prior exposure, access channels, interruptions, scoring, and audit limitations.

| Rank | Team | Lead programmer | Tester | Total | Lead speed | Counterexample resistance | Lead blind review | Counterexamples | Takeover correctness | Takeover speed | Final blind review |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | Team F | MiMo V2.6 Pro | GPT-6 Astra | **90.8** | 99 | 59 (found by 2 testers) | 70 | 100 (6 defects) | 100 (Fixed 2/2) | 94 | 78 |
| 2 | Team C | Gemini 3.8 Flash | GPT-6.1 Sol | **77.9** | 99 | 0 (found by 5 testers) | 48 | 79 (4 defects) | 100 (Fixed 7/7) | 98 | 52 |
| 3 | Team A | Kimi K3 | Sonnet 5.5 | **71.4** | 96 | 96 (found by 0 testers) | 57 | 22 (2 defects) | 100 (Fixed 0/0) | 71 | 60 |
| 4 | Team D | DeepSeek V4.1 Flash | Opus 5.5 | **70.3** | 45 | 27 (found by 2 testers) | 45 | 60 (4 defects) | 100 (Fixed 2/2) | 100 | 65 |
| 5 | Team B | 豆包 Seed 2.1 Turbo | Fable 5.1 | **56.4** | 46 | 0 (found by 5 testers) | 35 | 35 (2 defects) | 100 (Fixed 5/5) | 72 | 63 |
| 6 | Team E | GLM 5.3 | Grok 4.7 | **20.4** | 39 | 8 (found by 4 testers) | 48 | 33 (2 defects) | 0 (Fixed 0/4) | 0 | 48 |

The official [final report](results/最终成绩_en.md) contains all three leaderboards and detailed timing. [Defect classification](results/round2/缺陷点归类_en.md) explains the 7 Round 2 defects; [blind-review results](results/blind-review/汇总_en.md) and [final-retest verdicts](results/retest/判定_en.md) cover the post-contest checks. Two human volunteers' separately measured, unscored attempts are in the [human appendix](appendix/human/README_en.md).

| Material | English entry point |
|---|---|
| Competition conditions and incidents | [CONDITIONS](CONDITIONS_en.md) |
| Exact prompt templates, translated for reference | [Prompts](prompt_en.md) |
| Round 1 task and outdated specification | [TASK](workspace/TASK_en.md) · [SPEC](workspace/SPEC_en.md) |
| Round 2 regression task | [REVIEW](packages/round2/review/REVIEW_en.md) |
| Round 3 takeover and requirements change | [TAKEOVER](packages/round3/TAKEOVER_en.md) · [CHANGE](packages/round3/workspace/CHANGE_en.md) |
| Blind-review rubric | [12 submissions in one conversation](packages/blind-review/评审说明_同一对话版_en.md) |
| Final-retest task | [REVIEW](packages/retest/REVIEW_en.md) |
| Task packages | [Package index](packages/README_en.md) |
| Submitted code and counterexamples | [Submission index](submissions/README_en.md) |
| Results and verdicts | [Result index](results/README_en.md) |
| Referee scripts, hidden workloads, reproduction | [Referee guide](referee/README_en.md) |
| Pre-match boasts and post-match reviews | [Notes index](notes/README_en.md) |

Contestant code, `HANDOFF.md`, model-authored notes, boasts, retrospectives, inspection reports, logs, session records, and replay/data files remain in their original form and language. Chinese prompts and task materials are preserved unchanged; the English copies are for reference.

## Episode 7 · Legacy-code optimization round

Episode 7 ("District King Championship") had two events. The first was the Tron light-cycle tournament (see [`../tron/`](../tron/)); the second was this warehouse task. The same 10 contestants as in the Tron event each did the episode 5 round-1 "lead" task alone: the **same task package** (`workspace/`, the identical `workspace.zip` the episode 5 leads received), the **same one-sentence prompt** for everyone (`packages/ep7/提示词_主程.txt`), 30 minutes suggested and a hard 45-minute limit. Afterwards every submission was timed on an otherwise idle machine on the episode 5 **8 hidden workloads** against the **same baseline**, and ranked by the geometric-mean speed-up over the 8 workloads. Claude Sonnet, Opus and Haiku did the same task under the same conditions as a reference and are not ranked.

### Differences from episode 5

- **Solo, one round**: no teams, testers, takeover, requirement change, blind review or total score.
- **Tools**: all 10 contestants used the OpenCode CLI 2.0.19 (`run --standalone --auto`, unattended); the three Claude models used Claude Code 2.1.301. Channels are listed in [conditions](CONDITIONS_en.md).
- **Speed only**: a submission gets a geometric-mean speed-up only if all 8 workloads are correct. The baseline was not re-timed; the baseline CPU times measured on the same machine on 10-17 were reused, which leaves a systematic uncertainty of about ±5% per workload and about ±1% on the geometric mean.
- **Counterexamples are recorded but do not change the ranking**: GPT-6 Astra hunted for defects in the 12 timed submissions over two sessions. Every candidate input was re-run independently by the organizer; the 16 that held, plus 10 carried over from episode 5 and an internal 10-17 evaluation, were run against all submissions.

### Ranking

| Rank | Model | Correct workloads | Geo-mean speed-up | Counterexamples (inputs that hold) | Time (min) | Code |
|---:|---|---|---:|---|---|---|
| 1 | StepFun Step 5 preview | 8/8 | 139.4× | 0 | 45.0 (time limit) | `submissions/ep7/step-5-preview/` |
| 1 | Tencent Hunyuan Hy4 preview | 8/8 | 138.2× | 0 | 45.0 (time limit) | `submissions/ep7/hunyuan-hy4-preview/` |
| 3 | Qwen3.8-27B | 8/8 | 8.7× | 4 (4 timeouts) | 45.0 (time limit) | `submissions/ep7/qwen3.8-27b/` |
| 3 | Inception Mercury 3 (diffusion model) | 8/8 | 8.6× | 5 (1 wrong output, 4 timeouts) | 45.0 (time limit) | `submissions/ep7/mercury-3/` |
| 5 | Doubao Seed 2.1 Turbo (episode 5 result carried over) | 8/8 | 8.4× | 1 (1 wrong output) | 39.2 (episode 5) | `submissions/round1/doubao-seed-2.1-turbo/` |
| 6 | ERNIE 5.1 | 8/8 | 1.3× | 0 | 36.4 (finished on its own) | `submissions/ep7/ernie-5.1/` |
| 7 | Mistral Large 4 | 8/8 | ≈1× (code identical to the baseline) | 0 | 45.0 (time limit) | `submissions/ep7/mistral-large-4/` |
| 7 | Xiaohongshu dots3-note preview | 8/8 | ≈1× (code identical to the baseline) | 0 | 45.0 (time limit) | `submissions/ep7/dots3-note-preview/` |
| 9 | openPangu-2.0-Pro | 1/8 (7 workloads timed out) | more than 2× slower than the baseline | not run in the unified pass; 7 held in the independent re-run (2 wrong output, 5 timeouts) | 45.0 (time limit; resumed once, 7 min 36 s added) | `submissions/ep7/openpangu-2.0-pro/` |
| 10 | Meituan LongCat-2.0 | 0/8 (compile error) | — | compile error | 45.0 (time limit; resumed once, 1 min 44 s added) | `submissions/ep7/longcat-2.0/` |

Reference (not ranked):

| Model | Correct workloads | Geo-mean speed-up | Counterexamples | Time (min) | Code |
|---|---|---:|---|---|---|
| Claude Sonnet 5.5 | 8/8 | 169.7× | 1 (timeout); 12 inputs not run | 26.0 | `submissions/ep7/sonnet-5.5/` |
| Claude Opus 5.5 | 8/8 | 101.0× | 1 (timeout); 12 inputs not run | 32.7 | `submissions/ep7/opus-5.5/` |
| Claude Haiku 5.5 | 8/8 | 98.1× | 3 (timeouts); 12 inputs not run | 33.8 | `submissions/ep7/haiku-5.5/` |

- **Ties**: Step vs. Hunyuan differ by 0.9% and Qwen vs. Mercury by 1%, within the ~±1% systematic uncertainty from the reused baseline, so they were ranked as ties (organizer's decision). Mistral and dots submitted code byte-identical to the baseline, so their speed is the baseline's own; they share 7th–8th.
- **Doubao** also submitted new code in this event; the organizer decided not to time or use it (it is not included here); the ranking uses Doubao's episode 5 lead submission, timed on the same machine on 10-17.
- **openPangu**: the screening workload did not finish in 180 s (baseline 33.5 s); only `hid_huge` finished in time and correctly (229 CPU s vs. 113 s for the baseline), so its speed is recorded as "more than 2× slower" as a lower bound.
- **LongCat**: `sim.cpp` uses `__m128i` without including the header that declares it, so it fails to compile with the common build command.
- **Counterexamples** are counted per distinct input (SHA-256), not per root cause; 0 only means none of these inputs broke the code. The three Claude submissions were not run on 12 of the new inputs (those were found against other submissions). Per-input details: [counterexample index](results/ep7/counterexamples/README_en.md).
- **Time** runs from the prompt to submission, minus compensated interruptions; 45.0 is the hard limit. Step, Hunyuan, Mercury and Mistral were not stopped automatically at 45 minutes; the organizer reverted everything they changed in `src/` after the deadline (from the session event logs) and counts 45 minutes. The Claude times are taken from the last reply in each session. The "优化用时" column in `results/ep7/汇总.md` is the raw script value and differs for these rows; see [conditions](CONDITIONS_en.md).
- **Each model ran once.** This is the result of this event, not a capability ranking. During the 45 minutes 6–7 sessions shared the machine, so the contestants' own timings were slower; the official numbers come from the later exclusive run.

### Directory

| Path | Contents |
|---|---|
| `submissions/ep7/` | The 12 timed `src/` folders, byte-for-byte; see [submission index](submissions/ep7/README_en.md) |
| `results/ep7/` | Unified result table `汇总.md`, full data `结果.json`, sealed-submission manifest, compiler error, records of the independent counterexample re-run (absolute paths replaced with placeholders); see [result index](results/ep7/README_en.md) |
| `results/ep7/counterexamples/` | The 26 counterexample inputs used in the unified pass with baseline outputs, plus 2 excluded ones in `excluded/` |
| `packages/ep7/` | The prompt, and the tasks and reports of the two GPT-6 Astra defect-hunting sessions; see [package index](packages/ep7/README_en.md) |

To run a model's code yourself: copy `workspace/` somewhere, put the files from `submissions/ep7/<model>/` into the copy's `src/`, and run `python tools/bench.py`. The 8 hidden workloads used for the official timing are in `referee/隐藏负载/`.

Not included: session transcripts and run logs, the organizer's launch and evaluation scripts (they contain local paths and proxy settings), the Astra working directories (build outputs, intermediate inputs and logs, about 180 MB), and the code and materials of the internal 10-17 evaluation.
