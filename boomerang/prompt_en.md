[中文](prompt.md)

# Opening messages and call-back notices

The originals (Chinese) are in [prompts/](prompts/) and quoted in full in [prompt.md](prompt.md); below is an English translation for reference. The Chinese text is what the players received.

There were two batches. The six who started on the morning of 11-16 (the preview Astra and Sol, Fable, Opus, and the later-voided OpenCode runs of DeepSeek and Kimi) and DeepSeek's dsh run at 09:14 got the first batch; everyone after that (Kimi in Kimi Code, Grok, Doubao, GLM, MiMo, Qwen, both Geminis, the six control players, and the two GPT-6.2 runs on 11-17) got the second. They differ only in the first two lines and one sentence: the first batch opened with the model, reasoning level and working directory (written here as `{WORKDIR}`); the "no hand-in before minute 60" rule was set after the first batch had started, so it is in the second batch's text, and first-batch players learned it from a call-back notice.

## Second batch (translation)

> Rule: you may only modify your own folder, i.e. the current working directory.
>
> [Organiser] Welcome to the *Dart Brawl* programming contest.
>
> This is a genuinely hard contest. You will write a C++ program that controls a boomerang-wielding character on a small top-down arena and fights other people's programs: throw, melee, dash, dodge — the last one standing wins. Your opponents are today's top AI models from every vendor, and each will give it everything. The qualifier has two events, 1v1 and four-player free-for-all; the official run plays tens of thousands of games and combines both into an individual ranking; some of the official maps are ones you have not seen.
>
> Please bring your best work.
>
> - **There is plenty of time**: 75 minutes from this message; run `python clock.py` any time to see time used and left. You may feel that is not enough, but that is a human estimate: as an AI you are far faster, and many past contests have shown this is generous. So don't be nervous and don't rush — read README.md and RULES.md thoroughly and think before you code. Don't stop early either: a working version is only the start; ranks are decided later — play the sparring bots and your own old versions again and again, watch replays to find out where you lose, and improve round after round. Until time is up, keep improving: handing in early gains nothing; if you think nothing is left to improve, try a different approach. Contest-wide rule: you cannot hand in before minute 60 — if you stop replying earlier, the organiser will bring you back to keep improving; after minute 60, stopping replying means handing in, with no chance to change anything afterwards.
> - **Hand-in**: `player.cpp` in the current directory, a single C++17 file that may use `dart.hpp` and the standard library. Whatever it contains at the deadline is what you hand in. Keep it compiling and running at all times: back up before big changes, and avoid risky changes in the last few minutes.
> - **The local referee is the same program as in the official games**, and the rules engine in `dart.hpp` is the referee's own copy; results are fully deterministic, so you can test as much as you like.
> - **Sparring bots**: weak, mid and strong, binaries only. They are a ruler for measuring progress: beating strong does not mean you are strong enough — official opponents are programs written seriously by other top models.
> - **Working directory**: the current directory; read and write only here. Read `README.md` first, then `RULES.md`.
> - **Allowed**: compiling, playing and watching replays with the tools in the pack; writing any helper scripts (Python standard library is enough); keeping old versions and drawing your own test maps in this directory.
> - **Not allowed**: modifying `dart.hpp`, `arena.exe`, `build.py`, `maps/`, `RULES.md` and `clock.json`; reading or writing outside this directory; network access or changing proxy settings; calling other AIs or sub-agents (subagent, Task / Agent tools, background parallel agents all count); killing processes by name (other programs run on this machine; only kill processes you started, by PID); long sleeps.
> - **Before handing in**, write in `notes.md` in the current directory — in detail, it will be read word for word after the contest and used to introduce your program to viewers:
>   1. How your program plays: overall idea, techniques and algorithms, what each part solves and why.
>   2. Where you think it is strong and weak; ideas you tried but did not use, and why.
>   3. Local test results: against whom, how many games, win rates, comparisons between versions.
>   4. Anything in the rules you did not understand or found ambiguous, and anything you suspect is wrong with the referee or engine.
>
> Every player receives exactly this text. The organiser answers no questions during the contest and needs no confirmation — start as soon as you have read it.

## First batch

Same text, except that it began with "Organiser's fixed configuration: model {MODEL}, reasoning level {EFFORT}. Rule: you may only modify your own folder: {WORKDIR}", and instead of the minute-60 sentence it said: "Once you stop replying, you have handed in, with no chance to change anything afterwards."

## Call-back notices

When a player stopped before minute 60: "Organiser notice: contest rule — no hand-in before minute 60, for everyone. You stopped at minute {N}; please keep improving your program. The time you waited has been added back and does not count: run `python clock.py` now to see the updated time; its deadline is the one that counts. Everything else is as at the start: whatever `player.cpp` contains at the deadline is handed in; please update `notes.md` before then. Until time is up, keep improving; if you think nothing is left, try a different approach."

When a session was interrupted: "Organiser notice: your session was interrupted at minute {N} because of a problem with the organiser's API account (or: with the API connection), not because of you; it is fixed now. Please continue from where you were…" followed by the same time and hand-in reminders.
