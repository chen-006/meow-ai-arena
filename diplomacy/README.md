# 第 4 集 · 外交

这一集不写 bot：7 个 AI 模型直接当玩家，各坐一国，打标准七国《外交》（Diplomacy）。它们自己发私信谈判、结盟、背刺、下军令，本地裁判负责保密和结算。同一批模型连打三局，每局重新抽座位，并且带着之前几局的记忆。

## 规则速览（完整规则见 `workspace/RULES.md`）

- 标准地图，1901 年春开始。每个模型只知道对手是哪国，不知道背后是哪个模型。
- 春、秋两季先外交再下令：私信条数不限，单条 300 字以内；公开发言 400 字以内。**承诺没有强制力，允许欺骗。**
- 所有需要行动的国家都交了军令、且没有人还在读新消息时，同时结算。没有时间限制，也没有人替你下令。
- 秋季后控制 18 个补给中心即单独获胜。也可以全体同意协议和局，打满 1920 年也算和局。
- 计分：单独获胜 100 分，其余 0 分；和局时按补给中心数的平方分 100 分（SoS）。

## 成绩

| 局 | 结束 | 结果 | 补给中心 |
|---|---|---|---|
| 第 1 局 | 1906 春 | 6 国协议和局 | 英 9、德 7、意 6、土 6、法 3、俄 3、奥 0 |
| 第 2 局 | 1909 春 | 4 国协议和局 | 英 13、俄 9、意 8、土 4，奥、法、德出局 |
| 第 3 局 | 1913 秋 | **意大利单独获胜** | 意 18、俄 8、法 6、英 2，其余出局 |

| 名次 | 模型 | 工具 | 第 1 局 | 第 2 局 | 第 3 局 | 合计 |
|---:|---|---|---|---|---|---:|
| 1 | Fable 5.1 | Claude Code | 英 36.82 | 俄 24.55 | 意 **100** | 161.37 |
| 2 | Opus 5.5 | Claude Code | 俄 4.09 | 英 **51.21** | 奥 0 | 55.30 |
| 3 | DeepSeek V4 Pro | OpenCode | 德 22.27 | 奥 0 | 英 0 | 22.27 |
| 4 | GPT-6 Astra | Codex | 意 16.36 | 土 4.85 | 俄 0 | 21.21 |
| 5 | Sonnet 5.5 | Claude Code | 奥 0 | 意 19.39 | 德 0 | 19.39 |
| 6 | GPT-6.1 Sol | Codex | 土 16.36 | 德 0 | 法 0 | 16.36 |
| 7 | DeepSeek V4.1 Flash | OpenCode | 法 4.09 | 法 0 | 土 0 | 4.09 |

- **只打了三局，座位影响很大**（前两局的最高分都坐英国）。这个排名可以当看点，不宜当成"谁最会外交"的定论。
- 三局之间改过条件：第 2 局开局后管理员提醒了一次"争取得分最高"；第 3 局 1910 年前禁止和局，并明说是最后一局。详见 `prompt.md` 和 `CONDITIONS.md`。
- 工具、思考档、渠道、所有中断和事故见 **`CONDITIONS.md`**。

## 马上看

**看回放**：浏览器打开 `replays/game1.html`、`game2.html`、`game3.html`。逐阶段播放地图和军令，旁边是这一阶段的全部私信和公开发言（含发信模型名），以及每家补给中心的走势。

**读全文**：`games/gameN/全文.txt` 按阶段列出每条消息、每条命令和结算结果，可以按阶段号（如 `F1905M`）搜索。

**看模型的笔记**：`notes/` 里是各模型在自己工作目录里写下的笔记、推演文件和辅助脚本，原样保存。比如 Fable 第 3 局的决策笔记 `notes/fable-5.1/workdir/笔记_第三局_意大利.md`，五次背刺的理由都写在里面。

## 自己跑一局

`referee/` 是比赛用的全套程序：裁判服务、事件调度器、对局管理、管理员命令、观战窗口、统计、承诺核对、越界审计和回放生成。依赖 `diplomacy==1.1.2`，在 Python 3.12 上测试通过，其他版本没有验证。比赛时在 Windows 上运行，建议照当时的做法把虚拟环境建在 `referee/.venv`（Codex 沙箱里选手要用这个目录下的 python）：

```bash
cd referee
python -m venv .venv
.venv\Scripts\python.exe -m pip install -r requirements.txt     # macOS / Linux 用 .venv/bin/python
```

下面的命令都在 `referee/` 下，用这个虚拟环境的 python 运行（简写成 `python`）。

**不接模型，先跑通流程**：名单里每国都可以用脚本选手（`"harness": "script"`，见 `referee/bots/`），不需要任何模型。脚本选手和测试在 macOS / Linux 上也能跑。设环境变量 `DIPLOMACY_NO_TOAST=1` 可以关掉冻结时的 Windows 桌面通知。

```bash
python -X utf8 -m unittest test_v3                                   # 15 项回归测试，约 5 分钟
python -X utf8 match.py new --roster 名单.json --root 选手工作区目录    # 工作区目录不要放在 referee/ 里
python -X utf8 match.py run [--stop-at S1902M]                       # 运行 / 续跑；到达指定阶段自动冻结
```

**接真实模型**：调度器通过三家 Agent 的命令行唤醒选手，即 Claude Code（`claude -p --resume`）、Codex（`codex exec resume`）、OpenCode（`opencode run --session`），需要事先装好并登录。这部分只在 Windows 上验证过。

**连打几局**（三局正式赛就是这样打的）：

```bash
python -X utf8 match.py rematch --prev 管理员/对局/上一局目录 [--draw-from 1910] [--final]
```

同一批模型续接各自的会话，国家重新抽签并避开坐过的国家；`--draw-from` 指定从哪一年春季起才能表决和局，`--final` 告诉选手这是最后一局。

**管理员命令**：`admin.py status | pause | resume | stop`；对局停止时可用 `admin.py swap 国家 模型` 换模型（同一种 Agent、沿用会话），`admin.py notice "内容"` 登记一条通知，续跑后随各国下一次唤醒送达。名单格式见 `referee/管理员说明.md`。这份说明是第 1 局期间的版本，没有 `rematch` 和 `notice`，以上面的用法为准。

> **运行前请注意**
> - 这套程序会让模型在你的电脑上无人值守地运行命令。权限限制作用在 Agent 工具层面，挡不住选手自己写的 python。建议在虚拟机或单独的系统用户下运行。
> - 调度器会把 `~/.codex/auth.json` 复制到对局目录里的 `codex_home/`；DeepSeek 余额不足时，如果你放了备用 key（`referee/管理员/备用key/deepseek.txt`），它会改写 OpenCode 的 `auth.json`（先备份）。
> - 对局目录 `referee/管理员/` 里有各国令牌、登录凭据和完整日志，已在 `.gitignore` 里排除，**切勿提交或分享**。

**拿开源的对局数据用这些工具**：

```bash
python -X utf8 统计.py --match ../games/game3        # 每年补给中心、各家唤醒次数与用量、最近消息（只读）
python -X utf8 回放.py --match ../games/game3        # 重新生成回放，写到 games/game3/回放.html
python -X utf8 承诺核对.py --match ../games/game3    # 重新跑承诺核对，会覆盖 games/game3/承诺核对.md
```

`audit.py` 需要原始唤醒日志，开源存档里没有，会直接报错退出、不写文件。

## 目录

| 路径 | 内容 |
|---|---|
| `workspace/` | 选手包：规则（模板及每局原样）、操作说明、客户端 `game.py`、离线推演 `simulate.py`、地图索引与邻接表 |
| `prompt.md` | 开局消息、管理员通知、简报格式的原文 |
| `referee/` | 裁判与调度程序（第 3 局使用的版本），含测试和脚本选手 |
| `games/game1`～`game3` | 三局的完整存档：全部私信、命令、结算、和局投票、唤醒记录与用量、调度日志、承诺核对、越界审计（见 `games/README.md`） |
| `replays/` | 三局回放网页 |
| `notes/` | 各模型工作目录里的笔记与推演文件（见 `notes/README.md`） |
| `CONDITIONS.md` | 比赛条件与异常记录 |

## 和比赛时的差别

- `workspace/RULES.md` 是模板。每局开局时 `referee/match.py` 按本局设置改写后放进各国目录（第 3 局把和局那一行改成"1910 年春季起"）；另外"你的目标"那一行是第 3 局前才加进模板的。选手实际拿到的规则原样放在 `workspace/RULES_第1-2局.md` 和 `workspace/RULES_第3局.md`。
- 比赛时每个国家的目录里还有本局生成的 `connection.json`（裁判地址和本国令牌），OpenCode 选手另有 `opencode.json`（权限与上下文上限）。所以 `workspace/` 不能单独运行，要先用 `referee/match.py new` 建局、启动裁判，它会把选手包和连接信息复制到每个国家的目录。
- 选手包里原本还有一份官方完整规则 PDF（`资料/官方完整规则.pdf`），版权属于原出版方，开源版没有附带；`RULES.md` 里提到它的那句话保留原样。`referee/match.py` 相应改为：找不到这个文件就跳过。
- `referee/` 是第 3 局使用的版本，第 1、2 局期间调度程序改过几次，详见 `CONDITIONS.md` 的"代码版本"。开源时又改了几处，都不影响结算：
  - `match.py`：找不到官方规则 PDF 就跳过；推演用的 python 改为运行裁判的那个解释器（原来写死 `.venv\Scripts\python.exe`）；`new` 的 `--root` 改为必填（原来默认 `C:\bench\外交`）。
  - `audit.py`：写死的本机 Python 路径改成按当前用户目录推算；去掉 Claude 加密思考签名造成的误报；没有原始唤醒日志时报错退出，不覆盖已有结果。
- 数据：存档里的身份令牌已删除，日志里的本机用户名替换成了 `<user>`，越界审计里 12 处加密思考签名替换成了一句说明。除此之外都是比赛时的原样（文件换行符不统一，属原样保留）。
- 各模型每次被唤醒的原始输出（工具调用、命令输出等）没有放进仓库；唤醒次数、耗时和 token 用量在 `games/gameN/唤醒记录.jsonl`。

## 许可说明

- 本目录的代码和数据随仓库按 MIT 许可发布，以下除外。
- 裁判依赖 [`diplomacy`](https://github.com/diplomacy/diplomacy) 包，它是 AGPL-3.0 或更高版本许可。本仓库不包含它，使用时请遵守它的条款。
- 回放网页里的地图由这个包渲染，图形来自 jDip（GPL，每张地图里保留了版权注释），所以回放网页整体不按 MIT 授权。
- `资料/` 里的地区索引和邻接表按裁判使用的标准地图整理。
- 《Diplomacy》（外交）是其版权方的商标，本项目与版权方无关。
