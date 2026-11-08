# 给 coding agent 的提交提示词

把下面的提示词整段复制给你的 coding agent（Claude Code、Codex、Cursor 等），它会带着你走完投稿流程。提示词有中文和英文两版，内容相同，用哪一版都行。

- [提示词 A：我已经有作品了，帮我提交](#提示词-a我已经有作品了帮我提交)
- [提示词 B：让 AI 从零写一个，然后提交](#提示词-b让-ai-从零写一个然后提交)
- [Prompt A (English): submit an existing entry](#prompt-a-english-submit-an-existing-entry)
- [Prompt B (English): write one from scratch, then submit](#prompt-b-english-write-one-from-scratch-then-submit)

第 9 集造桥不走这里的 Pull Request 流程，投稿走网站排行榜，提示词见 [`bridge/AGENT_PROMPT.md`](../bridge/AGENT_PROMPT.md)（Episode 9, bridge building, is submitted through the website instead; see that file）。

用之前你需要有一个 GitHub 账号。电脑上最好装了 Python 3.10+、g++ 和 GitHub 命令行工具 `gh`（并且已经 `gh auth login`）；没有 `gh` 也可以，agent 会告诉你在网页上怎么点。

## 提示词 A：我已经有作品了，帮我提交

```text
请帮我把一份作品投稿到 GitHub 仓库 chen-006/meow-ai-arena（B 站节目“AI 算法对抗大赛”的开源仓库），方式是开一个 Pull Request。按下面的步骤做，每一步做完再做下一步。

一、先了解规则
1. 如果当前目录不是这个仓库的克隆，先 fork 并克隆：有 gh 就运行 `gh repo fork chen-006/meow-ai-arena --clone`；没有 gh 就让我在网页上 fork https://github.com/chen-006/meow-ai-arena ，再 `git clone` 我的 fork。
2. 完整阅读 `community/README.md`。里面的目录规则、meta.json 字段、大小限制都以它为准；它和这段提示词不一致时听它的。
3. 确定投哪一集之后，再读那一集的规则文件：圈地读 `land/workspace/TASK.md` 和 `RULES.md`；炸弹人读 `bomberman/workspace/README.md` 和 `RULES.md`；夺旗读 `ctf/workspace/README.md` 和 `RULES.md`；仓储调度读 `warehouse/workspace/TASK.md`；坦克大战读 `tank/workspace/README.md`、`RULES.md` 和 `tank.hpp`。Tank 的 `bot.cpp` 只定义 `init` 和 `decide`，包含官方 `tank.hpp`，由官方 `tank_main.cpp` 提供 `main`，不要自己提交头文件或主循环。光轮对战读 `tron/workspace/README.md`、`RULES.md` 和 `tron.hpp`，另读 `tron/README.md` 的赛后评分说明；Tron 只定义 `int decide(const tron::State&)`，包含官方 `tron.hpp`，由官方 `tron_main.cpp` 提供 `main`，不要自己提交头文件或主循环。第 4 集（外交）不收投稿。

二、向我确认这几件事（一次问完，等我回答；不要自己猜）
- 投哪一集：land（圈地）、bomberman（炸弹人）、ctf（夺旗）、warehouse（仓储调度）、tank（坦克大战）、tron（光轮对战）。
- 作品文件在哪里：bot 类是一个 .cpp 文件；仓储调度是一个放源码的文件夹。
- 作品名：目录名里用的那一段只能有小写字母、数字、连字符；另外要一个显示在榜单上的名字，可以是中文。
- 署名：榜单上显示的作者名，以及我的 GitHub 用户名（可以用 `gh api user --jq .login` 查出来让我确认）。
- 这份代码是谁写的：人写的（human）、AI 写的（ai）、还是合写的（human+ai）；用了哪个模型、哪个工具。
- 是不是在别人的代码上改的（比如某个官方 bot 或屎山原版）。
- 一句话说明。

三、放好文件
1. 从最新的 main 建一个分支，例如 `submit/<集>-<作品名>`。
2. 新建 `community/<集>/<GitHub用户名小写>__<作品名>/`。bot 类把代码放成这个目录下的 `bot.cpp`；仓储调度把源文件放进这个目录下的 `src/`。
3. 按 `community/README.md` 的字段说明写 `meta.json`。written_by、model、tool 要如实填写：我亲手写的填 human；代码是你（或别的 AI）写的填 ai，并在 model 里写上真实的模型名；我们一起改的填 human+ai。不知道就问我，不要编。license 固定写 "MIT"，写之前告诉我这表示同意按仓库的 MIT 许可公开发布，并得到我的同意。
4. 我想附思路说明的话，写成这个目录下的 `NOTES.md`。
5. 除了这一个文件夹，不要新增、修改或删除仓库里的任何其他文件。不要动官方的 `bots/`、`submissions/`、`workspace/`、`referee/`，不要改 `community/check.py`、`community/LEADERBOARD.md`、`community/results/`。

四、本地自测
1. 在仓库根目录运行 `python community/check.py community/<集>/<目录>`（没有 `python` 就用 `python3`）。Tank 使用原生正式模式（首回合 2000 CPU 毫秒，之后 50 CPU 毫秒），16 个对手各 20 局，共 320 局；`--full` 为各 80 局，共 1280 局。它固定终场赛对手的实力值，只拟合投稿者分数，不是重新拟合小比赛。Tron 需要 Windows + g++，默认正式模式 82 局（52 局 1v1、24 局 4 人、6 局 12 人），`--full` 为 328 局；沿用强对手榜固定参照，按 50/30/20 加权，豆包 = 0，强档陪练参与拟合但不占名次。Tank / Tron 支持必须已合并到目标分支，因为 PR 评测运行的是目标分支的可信脚本。bot 类用时取决于程序；仓储调度第一次要先跑基线，大约 5 到 10 分钟。
2. 没有通过就不要提交。把没通过的原因告诉我。结构或 meta.json 的问题你直接改；代码本身的问题（编译不过、超时、仓储调度的输出和基线不一致）先告诉我，由我决定是修还是放弃。不要为了通过去改评测脚本或官方文件。
3. 电脑上没有 g++ 或 Python 时，告诉我缺什么、怎么装（仓库根目录 README 有说明）。Tron 还需要 Windows。`--skip-eval` 仍会编译，不会绕过这些要求；缺少环境就如实写明“未做本地编译和评测”，不要把它记作通过或用快速模式替代。

五、开 PR 之前先给我看
开 PR 是公开的操作。在 commit、push、开 PR 之前，把下面的内容发给我，等我明确同意：
- 将要提交的文件列表（`git status --short`），以及 `meta.json` 的全文；
- PR 标题，格式是 `投稿：<集> / <作品名>`；
- PR 正文：按 `.github/PULL_REQUEST_TEMPLATE.md` 的勾选项填写，并贴上自测输出里从“## 社区投稿自测”开始的那段摘要（也在 `community/.runs/<集>/<目录>/summary.md` 里）。
提交内容里不能有密钥、令牌、密码、邮箱、手机号、本机路径或其他个人信息；代码、NOTES.md、meta.json 和 commit 信息都检查一遍。

六、提交
我同意之后：
1. `git add community/<集>/<目录>`，只添加这一个文件夹，然后 commit。
2. 推到我的 fork：`git push -u origin <分支名>`。
3. 有 gh：`gh pr create --repo chen-006/meow-ai-arena --base main --title "<标题>" --body-file <正文文件>`。正文文件放在仓库外面的临时位置，不要提交进去。
   没有 gh：把推送后 GitHub 给出的链接（`https://github.com/<我的用户名>/meow-ai-arena/pull/new/<分支名>`）和准备好的标题、正文发给我，告诉我在网页上点“Compare & pull request”，目标仓库选 chen-006/meow-ai-arena 的 main，把标题和正文粘进去。
4. 把 PR 链接发给我，并告诉我接下来会发生什么：GitHub Actions 会自动评测，第一次给这个仓库提 PR 的账号要等维护者批准后评测才开始；维护者审过并合并之后，作品会出现在 `community/LEADERBOARD.md`。

其他要求：不要替我做上面没有提到的公开操作（发 Issue、评论、给别的仓库提 PR）；社区榜的成绩和节目里的正式成绩不是一个口径，不要在 PR 里拿两者直接比较。
```

## 提示词 B：让 AI 从零写一个，然后提交

把下面这段放在最前面，后面接上提示词 A 的全文。把第一行的“夺旗（ctf）”换成你想玩的那一集。

```text
我想让你从零写一份作品，然后投稿到 GitHub 仓库 chen-006/meow-ai-arena。这次选的是：夺旗（ctf）。

先 fork 并克隆仓库（做法见后面的“一、先了解规则”），然后按这一集的任务书来写。任务书和选手包的原始内容见各集说明；坦克大战开源包多了基准源码，若要严格复现比赛条件，先按 `tank/README.md` 用 `make_player_pack.py` 打包。社区开发不要求复现比赛时限：
- 圈地（land）：任务书是 `land/prompt.md`，选手包是 `land/workspace/`，先读其中的 `TASK.md` 和 `RULES.md`。成品是 `land/workspace/submission/bot.cpp`。
- 炸弹人（bomberman）：任务书是 `bomberman/prompt.md`，选手包是 `bomberman/workspace/`，先读其中的 `README.md` 和 `RULES.md`。成品是 `bomberman/workspace/submission/bot.cpp`。
- 夺旗（ctf）：任务书是 `ctf/prompt.md`，选手包是 `ctf/workspace/`，先读其中的 `README.md` 和 `RULES.md`。成品是 `ctf/workspace/submission/bot.cpp`。
- 坦克大战（tank）：任务书是 `tank/prompt.md`，选手包是 `tank/workspace/`，先读 `README.md`、`RULES.md` 和 `tank.hpp`。成品是 `tank/workspace/submission/bot.cpp`，包含官方 `tank.hpp` 并定义 `init` 和 `decide`，不要写自己的 `main`。
- 光轮对战（tron）：任务书是 `tron/prompt.md`，选手包是 `tron/workspace/`，先读 `README.md`、`RULES.md` 和 `tron.hpp`。成品是 `tron/workspace/submission/bot.cpp`，只实现 `decide`；本地编译和正式评测需要 Windows。社区评分沿用 `tron/challenge.py`，不是选手包的本地名次分。
- 仓储调度（warehouse）：任务书是 `warehouse/prompt.md` 里第 1 轮的那一段，选手包是 `warehouse/workspace/`，先读其中的 `TASK.md`。成品是改写后的 `warehouse/workspace/src/`，对任何合法输入，输出都要和 `baseline/` 逐字节相同。

写的时候：
- 按任务书里的规则来：C++20、按各集规定使用依赖（Tank / Tron 使用官方 `tank.hpp` / `tron.hpp`；Tron 社区评测在 Windows，其余集在 Linux，依赖仍以各集规则为准）、单个文件（仓储调度是 `src/` 下的若干文件）、不读写文件、不联网、遵守每回合的时间限制。用选手包里自带的本地裁判或 `tools/bench.py` 反复测试。
- 节目里的模型有时间限制（多数是 60 分钟，仓储调度是 30 分钟，坦克大战是每半场 30 分钟），也不能看别的选手的代码。你不受这两条约束，可以参考仓库里官方选手的代码（`<集>/bots/`、`warehouse/submissions/`）。参考了哪一份，就在 meta.json 的 based_on 里写明。
- 开发过程中的改动只落在选手包的 `submission/`（仓储调度是 `workspace/src/`）里，这些都只是你的工作副本。投稿时把成品复制到 `community/<集>/<目录>/`，然后用 `git checkout -- <集>/workspace` 把选手包恢复原样，保证最后的 PR 里只有 community 下的那一个文件夹。
- 写完先用这一集的 `python <集>/challenge.py <你的bot.cpp>` 看看它在官方选手里排第几（仓储调度用 `python tools/bench.py --baseline`），把结果告诉我，问我是继续改还是就这样投。
- 这份作品是你写的，meta.json 里 written_by 填 ai，model 填你自己的模型名，tool 填我正在用的这个工具的名字。我中途提了具体改法或亲手改了代码的，填 human+ai。

接下来按下面的流程投稿。其中“作品文件在哪里”和“谁写的”你已经知道，不用再问我，其余几项照常向我确认。
```

## Prompt A (English): submit an existing entry

```text
Please help me submit an entry to the GitHub repository chen-006/meow-ai-arena (the open-source repository of the Bilibili series "AI Algorithm Arena") by opening a pull request. Follow these steps in order.

1. Learn the rules
1.1 If the current directory is not a clone of that repository, fork and clone it: with gh, run `gh repo fork chen-006/meow-ai-arena --clone`; without gh, ask me to fork https://github.com/chen-006/meow-ai-arena in the browser, then `git clone` my fork.
1.2 Read `community/README.md` in full (it is in Chinese; `community/README_en.md` is a short English version). Its rules on folder names, meta.json fields and size limits are authoritative; if they differ from this prompt, follow the README.
1.3 Once the episode is chosen, read its rule files: territory: `land/workspace/TASK.md` and `RULES.md`; bomberman: `bomberman/workspace/README.md` and `RULES.md`; capture the flag: `ctf/workspace/README.md` and `RULES.md`; warehouse: `warehouse/workspace/TASK.md`; Tank: `tank/workspace/README.md`, `RULES.md` and `tank.hpp`. A Tank `bot.cpp` includes the official `tank.hpp` and defines only `init` and `decide`; the official `tank_main.cpp` provides `main`. Do not submit your own header or driver. For Tron read `tron/workspace/README.md`, `RULES.md`, `tron.hpp`, and the post-tournament scoring notes in `tron/README_en.md`; define only `int decide(const tron::State&)`, include the official `tron.hpp`, and use the official `tron_main.cpp`, with no custom header or driver. Episode 4 (Diplomacy) does not accept submissions.

2. Confirm these with me (ask once, wait for my answers, do not guess)
- Which episode: land, bomberman, ctf, warehouse, tank or tron.
- Where the entry is: a single .cpp file for the bot episodes; a folder of sources for the warehouse.
- The entry name: the part used in the folder name may only contain lowercase letters, digits and hyphens; I also need a display name for the leaderboard (any language).
- Credit: the author name shown on the leaderboard, and my GitHub username (you can look it up with `gh api user --jq .login` and ask me to confirm).
- Who wrote the code: a human (human), an AI (ai), or both (human+ai); which model and which tool were used.
- Whether it is based on someone else's code (an official bot, the original warehouse baseline, ...).
- A one-sentence description.

3. Put the files in place
3.1 Create a branch from the latest main, for example `submit/<episode>-<entry-name>`.
3.2 Create `community/<episode>/<github-username-in-lowercase>__<entry-name>/`. For bot episodes, put the code there as `bot.cpp`; for the warehouse, put the source files in its `src/` subfolder.
3.3 Write `meta.json` following the field table in `community/README.md`. Fill in written_by, model and tool truthfully: human if I wrote it myself; ai if you (or another AI) wrote it, with the real model name in model; human+ai if we both worked on it. Ask me if you do not know; do not make anything up. license must be "MIT"; before writing it, tell me this means agreeing to publish the entry under the repository's MIT license, and get my consent.
3.4 If I want to include notes on the approach, write them as `NOTES.md` in the same folder.
3.5 Do not add, modify or delete any other file in the repository. Do not touch the official `bots/`, `submissions/`, `workspace/`, `referee/`, and do not edit `community/check.py`, `community/LEADERBOARD.md` or `community/results/`.

4. Test locally
4.1 From the repository root, run `python community/check.py community/<episode>/<folder>` (use `python3` if there is no `python`). Tank uses native formal mode (2000 CPU ms for the first turn, 50 CPU ms thereafter), playing 20 games against each of 16 opponents, 320 total; `--full` uses 80 each, 1,280 total. Opponent final-tournament ratings stay fixed; only the entrant rating is fitted, rather than refitting a mini-tournament. Tron requires Windows and g++; it runs 82 formal-mode games (52 1v1, 24 four-player, six twelve-player), or 328 with `--full`. It keeps the strong-opponent reference ratings fixed and weights the three modes 50/30/20, with Doubao = 0; the strong sparring bot is included among the fixed opponents but is not ranked. Tank / Tron support must already be merged into the target branch because PR evaluation runs that branch's trusted scripts. Bot evaluation time depends on the programs; the warehouse needs to run the slow baseline first, about 5 to 10 minutes the first time.
4.2 If it does not pass, do not submit. Tell me why it failed. Fix structure and meta.json problems yourself; for problems in the code itself (compile errors, timeouts, warehouse output differing from the baseline), tell me first and let me decide whether to fix it or give up. Never edit the evaluation scripts or official files to make it pass.
4.3 If g++ or Python is missing, tell me what is missing and how to install it (see the root README). Tron also requires Windows. `--skip-eval` still compiles and cannot bypass these requirements. If the environment is unavailable, state "no local compilation or evaluation was run"; do not claim a pass or substitute fast mode.

5. Show me before opening the pull request
Opening a pull request is a public action. Before you commit, push or open it, send me the following and wait for my explicit approval:
- the list of files to be committed (`git status --short`) and the full text of `meta.json`;
- the pull request title, in the form `投稿：<episode> / <entry name>`;
- the pull request body: fill in the checklist from `.github/PULL_REQUEST_TEMPLATE.md` and paste the summary printed by check.py, starting at the line "## 社区投稿自测" (also saved in `community/.runs/<episode>/<folder>/summary.md`).
Nothing to be committed may contain keys, tokens, passwords, email addresses, phone numbers, local paths or other personal information; check the code, NOTES.md, meta.json and the commit message.

6. Submit
After I approve:
6.1 `git add community/<episode>/<folder>` (only this folder), then commit.
6.2 Push to my fork: `git push -u origin <branch>`.
6.3 With gh: `gh pr create --repo chen-006/meow-ai-arena --base main --title "<title>" --body-file <body file>`. Keep the body file in a temporary location outside the repository; do not commit it.
    Without gh: give me the link GitHub prints after the push (`https://github.com/<my-username>/meow-ai-arena/pull/new/<branch>`) together with the prepared title and body, and tell me to click "Compare & pull request", choose chen-006/meow-ai-arena and its main branch as the target, and paste them in.
6.4 Send me the pull request link and tell me what happens next: GitHub Actions evaluates the entry automatically (for an account's first pull request to this repository, a maintainer has to approve the run first); after a maintainer reviews and merges it, the entry appears in `community/LEADERBOARD.md`.

Also: do not take any other public action on my behalf (issues, comments, pull requests to other repositories). Community leaderboard numbers are not measured the same way as the official results from the show; do not compare them directly in the pull request.
```

## Prompt B (English): write one from scratch, then submit

Put this in front of the full text of Prompt A. Replace "capture the flag (ctf)" in the first line with the episode you want.

```text
I want you to write an entry from scratch and then submit it to the GitHub repository chen-006/meow-ai-arena. The episode is: capture the flag (ctf).

Fork and clone the repository first (see "1. Learn the rules" below), then work from the episode's task statement. See each episode's documentation for the original task and package. The open-source Tank package adds baseline source; to reproduce contest conditions exactly, first use `make_player_pack.py` as described in `tank/README.md`. Community development does not have to reproduce the contest time limit:
- Territory (land): the task statement is `land/prompt.md`, the package is `land/workspace/`; read `TASK.md` and `RULES.md` there first. The deliverable is `land/workspace/submission/bot.cpp`.
- Bomberman: the task statement is `bomberman/prompt.md`, the package is `bomberman/workspace/`; read `README.md` and `RULES.md` there first. The deliverable is `bomberman/workspace/submission/bot.cpp`.
- Capture the flag (ctf): the task statement is `ctf/prompt.md`, the package is `ctf/workspace/`; read `README.md` and `RULES.md` there first. The deliverable is `ctf/workspace/submission/bot.cpp`.
- Tank: the task statement is `tank/prompt.md`, the package is `tank/workspace/`; read `README.md`, `RULES.md` and `tank.hpp` first. The deliverable is `tank/workspace/submission/bot.cpp`, including the official `tank.hpp` and defining `init` and `decide`, with no custom `main`.
- Tron: the task statement is `tron/prompt.md`, the package is `tron/workspace/`; read `README.md`, `RULES.md` and `tron.hpp` first. The deliverable is `tron/workspace/submission/bot.cpp`, implementing only `decide`. Local compilation and formal evaluation require Windows. Community scoring follows `tron/challenge.py`, not the workspace tool's local placement score.
- Warehouse: the task statement is the round 1 section of `warehouse/prompt.md`, the package is `warehouse/workspace/`; read `TASK.md` there first. The deliverable is the rewritten `warehouse/workspace/src/`, whose output must be byte-identical to `baseline/` on every valid input.
These files are in Chinese.

While writing:
- Follow the rules in the task statement: C++20, only dependencies allowed by the episode (Tank / Tron use the official `tank.hpp` / `tron.hpp`; Tron CI runs on Windows, the other episodes on Linux, and each episode's dependency restrictions still apply), a single file (several files under `src/` for the warehouse), no file or network access, and the per-turn time limits. Test repeatedly with the local referee in the package, or with `tools/bench.py`.
- The models on the show had a time limit (60 minutes in most episodes, 30 for the warehouse, 30 per half for Tank) and could not see other contestants' code. Neither restriction applies to you: you may study the official entries in the repository (`<episode>/bots/`, `warehouse/submissions/`). If you build on one of them, name it in based_on in meta.json.
- Keep your work-in-progress changes inside the package's `submission/` folder (`workspace/src/` for the warehouse); these are only working copies. When submitting, copy the finished code to `community/<episode>/<folder>/`, then restore the package with `git checkout -- <episode>/workspace`, so that the final pull request contains nothing but that one folder under community.
- When you are done, run `python <episode>/challenge.py <your bot.cpp>` to see where it ranks among the official bots (for the warehouse, `python tools/bench.py --baseline`), tell me the result, and ask whether to keep improving or submit as is.
- You wrote this entry, so in meta.json set written_by to ai, model to your own model name, and tool to the name of the tool I am using right now. If I suggested specific changes or edited the code myself along the way, use human+ai.

Then follow the submission procedure below. You already know where the entry is and who wrote it, so skip those two questions and confirm the rest with me as usual.
```
