# 第 9 集造桥：给 coding agent 的投稿提示词

第 9 集的投稿**走网站排行榜，不走 Pull Request**（其他集的 PR 流程见 [community/AGENT_PROMPT.md](../community/AGENT_PROMPT.md)）。把下面的提示词整段复制给你的 coding agent（Claude Code、Codex、Cursor 等）。中文和英文两版内容相同。

- [提示词（中文）](#提示词中文)
- [Prompt (English)](#prompt-english)

网站第 9 集页面：`<网站地址>/ep9/`。也可以不用 agent：在网页模拟器里导入设计 JSON（或直接搭桥），试车全部公开场景通过后点“上传到排行榜”。

## 提示词（中文）

```text
请帮我给 meow-ai-arena 第 9 集「造桥大赛」的网站排行榜投一座桥。仓库：https://github.com/chen-006/meow-ai-arena ，相关内容都在 bridge/ 目录。

1. 先问我：投第几关（1～6）、昵称（16 字以内，不能含网址）、是用我已有的设计 JSON，还是让你从零设计。
2. 完整阅读 bridge/workspace/RULES.md（规则、设计 JSON 格式、材料表、各关说明）。设计文件的格式和比赛完全相同。
3. 准备环境：需要 Python 3.9+ 和 g++。在 bridge/ 下运行 python build_engine.py 编译物理内核。
4. 设计或修改桥。每次改完都在 bridge/workspace/ 下运行
     python bridge.py check <关> <设计.json>
     python bridge.py run <关> <设计.json> --no-archive -q
   run 的退出码 0 表示该关全部公开场景通过。只有全部通过的设计才能上传；目标是在全部通过的前提下把造价压低（排行榜按造价从低到高排）。可以对照 bridge/designs/ 里九位参赛模型的设计和 bridge/reference/ 里的参考答案。
5. 上传前把这几样给我过目：关卡、造价、run 的完整输出、要上传的 JSON。我同意后再上传。
6. 上传接口（前缀 <网站地址>/api/ep9/，JSON，UTF-8）：
     POST submit   请求体 {"level": 1～6, "nick": "昵称", "design": 设计对象}
                   返回 {"id", "cost", "rank", "dup"}；dup 为 true 表示同一关已经有完全相同的设计
     GET  board?level=N   看第 N 关排行榜
     GET  entry?id=...    取榜上某一条的完整设计
     GET  health          检查接口是否可用
   design 里只保留 nodes、members、sensors、lights、rules 五个字段；请求体不超过 64 KB。
7. 上传后用 board?level=N 确认名次，把 id、造价、名次告诉我。

注意：
- 服务器只按比赛原版 bridge.py 重算造价、检查合法性，不跑物理。过没过关要你在本地用 bridge.py 跑通。网页访客的浏览器会抽查复跑榜单前几名，复跑不过的会被下榜。
- 有按 IP 的频率限制：上传每小时 12 次、每天 40 次。不要循环提交、不要拿接口试参数。
- 只投第 1～6 关；第 7 关没有排行榜。
```

## Prompt (English)

```text
Please help me submit a bridge to the website leaderboard of meow-ai-arena episode 9, "Bridge Building". Repository: https://github.com/chen-006/meow-ai-arena ; everything relevant is under bridge/.

1. First ask me: which level (1–6), a nickname (up to 16 characters, no URLs), and whether to use a design JSON I already have or design one from scratch.
2. Read bridge/workspace/RULES_en.md in full (rules, the design JSON format, materials, every level). The design format is exactly the one used in the contest. (RULES.md is the Chinese original and takes precedence.)
3. Set up: Python 3.9+ and g++ are required. In bridge/, run python build_engine.py to compile the physics core.
4. Design or edit the bridge. After every change, run in bridge/workspace/:
     python bridge.py check <level> <design.json>
     python bridge.py run <level> <design.json> --no-archive -q
   Exit code 0 from run means every public scenario of the level passes. Only designs that pass every scenario may be uploaded; the goal is to make the passing design as cheap as possible (the leaderboard is sorted by cost, lowest first). You can compare with the nine contestants' designs in bridge/designs/ and the reference solutions in bridge/reference/.
5. Before uploading, show me: the level, the cost, the full output of run, and the JSON to be uploaded. Upload only after I approve.
6. Upload API (prefix <网站地址>/api/ep9/, JSON, UTF-8):
     POST submit   body {"level": 1-6, "nick": "nickname", "design": design object}
                   returns {"id", "cost", "rank", "dup"}; dup = true means an identical design is already on that level's board
     GET  board?level=N   the leaderboard of level N
     GET  entry?id=...    the full design of one entry
     GET  health          check that the API is up
   Only the five fields nodes, members, sensors, lights, rules are kept from design; the request body must be at most 64 KB.
7. After uploading, check the rank with board?level=N and tell me the id, cost and rank.

Notes:
- The server only recomputes the cost and checks validity with the contest's original bridge.py; it does not run the physics. Whether the level passes is for you to verify locally with bridge.py. Visitors' browsers re-run the top entries of the leaderboard as spot checks, and entries that fail are removed.
- There is a per-IP rate limit: 12 uploads per hour and 40 per day. Do not submit in a loop or probe the API.
- Levels 1–6 only; level 7 has no leaderboard.
```
