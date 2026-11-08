[简体中文](prompt.md)

# Opening message sent to the contestants

The nine contestants received the same message except for the working directory, shown here as `{选手目录}` (contestant directory). The original is in Chinese ([prompt.md](prompt.md)); below is a reference translation. This is the organizer's message only; no contestant sessions or reasoning are included.

"The other seven" is in the original message; nine contestants took part and were scored in the end.

```text
[Organizer] Welcome to the "AI Bridge Building Contest".

This is a genuinely hard engineering contest. You are competing against seven other top AI models of today, and every one of them will give it their all. The seven levels get harder one after another: the early ones test solid structural fundamentals; the later ones ask you to come up with ideas no one else thinks of, under tight budgets and complex conditions. Merely "passing" is far from enough: among passing designs, the cheaper the design, the higher the score.

Please bring your best work.

- **Plenty of time**: 90 minutes in total, counted from when you start; check the time yourself with `date`. No need to rush: read the rules thoroughly and think before you build; but don't stop early either: keep cutting the cost of levels you have passed, and try a different idea on levels you haven't. How you split the time across the seven levels is up to you.
- **The local simulator and the judge are the same program**; results are fully deterministic, and you can try as many times as you like.
- **Working directory**: {选手目录}; work only inside this directory. Read `README.md`, `RULES.md` and `RULES_决赛.md` first; all 7 levels are given to you from the start.
- **Submission**: put the files in `submission/`: `level1.json` … `level6.json`, `final_bridge.json`, `saboteur.json`. When time is up and the papers are collected, the files' contents at that moment count. When time is up, stop and reply "交卷" (submitted), with one sentence on the status of each level.
- **You may**: write any helper scripts (Python standard library is enough).
- **You may not**: modify `bridge.py`, `saboteur.py`, `bridge_sim.exe` or `levels/`; kill processes by name in bulk (only kill processes you started yourself, by PID); access the internet or change proxy settings; wait with long sleeps; spawn sub-agents.
- **Before submitting**, write in `submission/notes.md`: how long you spent on each level, your design ideas, your local test results, anything in the rules you did not understand or found ambiguous, and any simulator issues you suspect.
```
