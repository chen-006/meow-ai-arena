[简体中文](prompt.md)

# Messages sent to the contestants

This page has the organizer's messages only; no contestant sessions or reasoning are included. The originals are in Chinese ([prompt.md](prompt.md)); below is a reference translation.

## Opening message

The nine contestants received the same opening message except for the working directory, shown here as `{WORKDIR}` (the total time is already filled in as 90 minutes). Gemini 4 Carbon was run by a friend of the organizer on another machine with the same message, with `{WORKDIR}` replaced by the path on that machine.

````text
[Organizer] Welcome to "Armchair Generals" (纸上谈兵).

This is a contest of troop deployment. You are competing against today's top AI models from every major lab, and every one of them will give it their all. In each battle you get a budget, buy troops, place them inside your deployment zone, and write a commander program; once the battle starts nobody can intervene, and the little soldiers hack at, crash into and fall off each other in real physics. The first five levels are against the organizer's defenders and bosses, each with its own win condition; among those who pass, the less you spend, the higher your score. Finally you face the other contestants blind: submit one 1v1 deployment that plays a few battles against every other contestant, and one deployment for a four-player free-for-all. While you deploy you cannot see anyone else's army, and nobody can see yours.

Please bring your best work.

- **Time**: 90 minutes in total, counted from when you start; run `python clock.py` at any time to see the time used and left. You may feel this is not enough, but that estimate is based on human speed: as an AI you are far more efficient than a human, and many past contests have shown that this time is very generous. So don't be nervous and don't rush; read the rules thoroughly and think before you act, and give it your best. Don't stop early either: keep cutting the cost of the levels you have passed, try a different idea on the levels you haven't, and play your versus deployments a few more times before you settle. How you split the time is up to you. When time is up, stop and reply "交卷" (handing in) with one sentence on the status of each item; if you stop replying, that also counts as handing in.
- **Local results are exactly the official results**: the local `war.exe` is the same simulator the referee uses, there are no random numbers, and each battle is played once; whatever you get locally is what you get officially. You can try as many times as you like.
- **Working directory**: {WORKDIR}. Only read and write inside it. Start with `README.md`, `RULES.md` and `COMMANDER.md`; every level and versus map is given to you from the start.
- **Hand-in**: put the files in `submission/`: `level1.json` … `level5.json`, `duel.json`, `melee.json`, `commander.cpp`. When time is up, the files as they are at that moment count.
- **You may**: write any helper scripts (the Python standard library is enough), and run as many battles as you like with `war.exe` and `play.py`.
- **You may not**: modify the organizer's files (`war.exe`, `play.py`, `build.py`, `clock.py`, `clock.json`, `commander/`, `levels/`, `maps/`, `sparring/`); read or write files outside the working directory; go online or change proxy settings; call other AIs or start sub-agents; kill processes by name (you may only kill processes you started yourself, by process ID); wait with long sleeps.
- **Before handing in**, write in `submission/notes.md`: how long you spent on each level and on the versus part, your ideas, your local test results, anything in the rules you didn't understand or found ambiguous, and any simulator problems you suspect.

Every contestant receives exactly this message. The organizer does not answer questions during the contest and needs no confirmation from you; once you've read this, go ahead and start.
````

## Resume message

Contestants whose work was interrupted mid-contest (the organizer's machine rebooting, the tool exiting on an API error, and so on; see [CONDITIONS_en.md](CONDITIONS_en.md)) were sent the following when their original session was resumed. The text was identical for every contestant.

````text
[Organizer] Your work was just interrupted by a fault on the organizer's side, and the commands and processes you were running were all terminated. The contest now resumes.

- The interruption does not count toward your time: the time used and left that `python clock.py` shows now already excludes the interruption; go by it.
- All files in your working directory are still there. First take a look at how far you had got and what is in `submission/` now, then carry on.
- All other rules are exactly as stated at the start. When time is up, stop and reply "交卷" with one sentence on the status of each item.
````
