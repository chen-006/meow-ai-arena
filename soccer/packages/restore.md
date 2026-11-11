[English](restore_en.md)

# 赛时包清单与还原

本页把“公开比赛资料”和“当时发给选手的工作区”分开说明。完整清单为 [player_pack_manifest.json](player_pack_manifest.json)：每个席位初始 23 个文件，另列队伍共享文件、运行时文件，以及六队的中场包。清单依据原选手包、安装程序、队聊初始化程序和实际选手目录核对，不把选手赛后留下的缓存当成初始包。

与第 3、6、7 集相同，比赛时只给参考程序的 exe，不给陪练源码；开源资料则提供源码和编译入口。本次提供完整清单与还原说明，**没有包含全部服务的一键打包器**。下列固定材料可以按公开内容还原，原计时、队聊客户端及后台仅登记边界和静态哈希；需要相同服务的使用者仍须自行实现或取得独立可用版本。不能只复制源码树，就声称复现了原比赛全部条件。

## 1. 目录和文件

每队有 `a/`、`b/` 两个工作目录，文件类别相同；模型对应关系见 [roster.json](../bots/roster.json)。下表中的路径相对于某个席位目录。

| 文件 | 数量 | 赛时来源 / 开源还原办法 |
|---|---:|---|
| `arena.cpp`、`build.py`、`match.py`、`replay.py`、`run.cmd`、`soccer.hpp`、`soccer_engine.py`、`template.cpp`、`viewer.html`、`示例回放.txt` | 10 | 基础包固定文件；对应 `workspace/` 同名文件，开源适配见第 4 节 |
| `RULES.md` | 1 | 基础规则，安装时加 UTF-8 BOM |
| `README.md` | 1 | 已填参数的 `format.md` + 基础包 README；安装时加 UTF-8 BOM |
| `arena.exe` | 1 | 由 `workspace/arena.cpp` 编译，保留沙箱 |
| `bots/baseline.exe`、`bots/midfield.exe`、`bots/sparring.exe` | 3 | `build_reference.py` 从公开陪练源码编译；源码不得放进新比赛的选手包 |
| `clock.py` | 1 | 原赛制覆盖层客户端，读取上级 `phase.json` 并调用 `tc.notify()`；未提供原脚本 |
| `tc.py`、`with_notice.py` | 2 | 原队聊客户端和通知包装器，依赖下述本地配置/存储；未提供原脚本 |
| `tc.bat` | 1 | ASCII 启动器，一行 `@python "%~dp0tc.py" %*`，原文件经 Windows 文本写入换行；按历史哈希核对时不要假定普通 LF 等价 |
| `TEAMCHAT_README.md` | 1 | 本目录的实际下发原件，包含 UTF-8 BOM，按字节复制 |
| `teamchat.json` | 1 | 每席位单独生成的身份与路径配置；原值不公开，也不公布原配置哈希 |
| `submission/player.cpp` | 1 | 开赛前复制 `template.cpp`；不能把最后封存的 bot 当成初始文件 |
| **总计** | **23** | 完整逐文件路径及固定文件 SHA-256 在清单中 |

队伍共享目录如下；中场包在中场后才下发。运行生成文件在清单中单列，不表示它们在开赛时已经有内容。

```text
teamN/
  a/                         # 上表的 23 个初始文件
  b/                         # 同上，身份和 README 参数对应另一席位
  team.json                  # 队名、两人名称、计时和聊天镜像配置
  phase.json                 # 主办方更新的当前阶段、截止时刻和时长
  chat/
    chat.jsonl               # 运行时聊天内容，不公开
    chat.lock                # 运行时锁，不复制旧锁
    state/a.json             # 运行时已读游标，不复制旧状态
    state/b.json
  halftime/                  # 中场后共享，见第 3 节
```

`submission/partner.cpp` 是下半场新增的可选提交，不是初始包。`build/`、`replays/`、`_soccer_box/`、`__pycache__/` 为本地运行产物。选手自建测试、策略笔记和任意临时文件不属于初始包；主办方聊天镜像/备份、账号配置、原始会话和作废轮次均不在发布范围内。

## 2. 还原固定开发材料

1. 在 `soccer/` 执行 `python build_reference.py`，得到裁判和三个参考 exe。环境要求 Windows、Python 3.10+、g++。
2. 新建空目录，按上述树建队伍和 `a/`、`b/`。从 `workspace/` 复制表中 10 个固定文件、`RULES.md`、四个 exe；另复制本目录的 `TEAMCHAT_README.md`。不要复制 `sparring/`、正式选手的 `bots/`、`notes/`、最终榜单或任何现有缓存。
3. 每个席位新建 `submission/`，把 `template.cpp` 复制为 `submission/player.cpp`。赛时安装不创建 partner；不要替选手预填个人搭档。
4. 用下面参数填充 [format.md](format.md)，后接 `workspace/README.md`。只把基础 README 的第一个 `# ` 改成 `## `；保持模板末尾的分隔线和换行。合成 README 与 RULES 写为 UTF-8 BOM（已有 BOM 不重复添加）。这是原安装程序的拼接方式；当前基础 README 含已披露的开源适配，所以合成文件不保证匹配历史哈希。
5. 清单中的 `historical_sha256` 对应原静态下发材料；`published_sha256` 对应当前公开文件。README 另按 12 个席位给出历史哈希。初始 exe 的哈希取原始基础包，不取选手后来重编的缓存。可在 `soccer/` 运行 `python check_archive.py` 核对公开来源哈希和清单结构。
6. 开新比赛前还需接入第 2.1 节的计时和队聊接口，并用两个人工席位演练消息、计时和收卷。缺这些服务时，只按 [workspace 说明](../workspace/README.md) 使用独立本地对战，不发送要求选手等待 `tc.py` 的原开赛提示词。

| 模板参数 | 值 |
|---|---|
| `提交文件` | `submission/player.cpp` |
| `队伍数`、`会议分钟`、`上半场分钟`、`下半场分钟` | 初始下发 README 填 `6`、`5`、`40`、`25`；中场消息将实际下半场改为 `40` |
| `中场种子`、`终场种子` | `32`、`44` |
| `胜`、`平`、`负` | `3`、`1`、`0` |
| `队友位` | a 席填 b，b 席填 a |
| `A位`、`B位` | 对应队伍的模型显示名，来自 `bots/roster.json` 的 `model` |

原模板“偶数种子”指赛程中的种子序号，不是种子数值奇偶。12 份实际下发 README 都保留了下半场 25 分钟的旧值；按上述初始参数和原始基础 README 拼接，可匹配清单中的 12 份历史哈希。中场提示词才宣布下半场 40 分钟、个人队和 50%/50% 规则，实际计时也使用 40 分钟。还原初始包时不能提前写入这些后来调整，改变选手当时获知的信息。

### 2.1 计时和队聊的外部依赖

清单记录以下字段名，不带原值；新比赛在自己的新目录生成配置，不能从原赛事复制身份、时刻或聊天状态。

- `teamchat.json`：`team`、`me`、`peer`、`me_name`、`peer_name`、`chat_dir`、`my_dir`、`peer_dir`。路径必须由新比赛根目录解析，不能使用本机历史位置；身份配置只留在使用者本地，不入公开包。
- `team.json`：`team`、`names`、`mirror`、`start`、`start_t`。主办方镜像不属于选手材料；若自行实现服务，镜像位置也由使用者自行配置。
- `phase.json`：`phase`、`desc`、`ends_t`、`ends`、`meeting_minutes`、`half1_minutes`、`half2_minutes`、`t0`、`updated`。主办方在阶段变化和补时后更新；`clock.py` 读取当前阶段和剩余秒数。两人到齐才开始 5 分钟会议，会议后 40 分钟开发，中场包发出时另起下半场 40 分钟。
- 队聊要实现原 [选手说明](TEAMCHAT_README.md) 的 `send/inbox/wait/log/status` 和 `notify()` 语义，等待时不能忙轮询，已读游标按席位隔离。运行时使用新聊天文件/锁/游标，禁止复制历史聊天。
- 原实现只有 `clock.py` 接入未读提醒；如为新比赛修正其他工具的提醒，应公开声明改变了比赛条件，不能将新实现冒充原赛时行为。
- 收卷需要主办方另行复制当时的提交并计算哈希，停止过程只处理自己的进程。公开的本地对战工具不负责启动模型、维护账号或停止模型会话。

## 3. 中场包还原

每队收到以下文件，两个席位共用 `../halftime/`：

| 路径 | 还原来源 |
|---|---|
| `README.md` | 说明成绩表、本队回放和三个复盘工具的位置；原文件静态哈希在清单中 |
| `RESULTS.md` | 所有队伍中场成绩 + 本队对局列表 + 本队故障；按下述规则从逐局数据重建 |
| `tools/replay.py`、`tools/soccer_engine.py`、`tools/viewer.html` | `workspace/` 同名文件，原样复制 |
| `replays/{id}.txt` | 从中场赛选择 `A == teamN` 或 `B == teamN` 的全部局，包括参考队；每队 512 局 |

在 `soccer/` 执行 `python tournament/score.py halftime` 复算中场积分；逐局输入为 `results/halftime/results.json.gz`。`RESULTS.md` 先列全部六队的 `standings`；再逐局列 `id`、对手、A/B 左右、本队两个 `seats`、按本队方向排列的 `score`、胜平负和 `seed`，链接到 `replays/{id}.txt`；`faults` 仅列本队两席位的记录。每队 `(5 + 3) × 32 × 2 = 512` 局，不要漏掉对参考队的局。两队互打的回放会出现在两个中场包中。

公开仓库没有完整历史中场回放。需要可播放的等价材料时，可执行：

```text
python build_reference.py
python tournament/reproduce.py halftime --all --out out/restored-halftime
```

这会重新打 2112 局，耗时取决于机器。`out/restored-halftime/results.jsonl` 每行有 `original_id`；输出顺序与输入赛程相同，第 n 行的回放是 `replays/g{n}.txt`（n 从 0 起）。按 `original_id` 重命名复制到每队 `replays/`，并筛选本队对局。**重打回放必须标为重新生成**：CPU 计时、编译器和机器状态可能改变逐步过程或比分，不能把原历史成绩表和新回放说成同一批原件；可另附重打成绩。只有持有原历史回放并核对原件时，才能恢复原中场包。清单哈希描述原文件，不承诺由重打结果生成的文件与原件相同。

## 4. 开源适配与复现边界

- 固定文件适配详见 [source_manifest.json](../source_manifest.json)：编译器探测位置、裁判链接库、陪练 include 路径、沙箱目录选择、裁判源码更新后的缓存重建、本地对战和重打默认 `--sandbox none`，以及相应说明。核心物理、封存选手源码、比赛种子和成绩没有改变。
- 裁判不再使用任何主办方绝对备用目录；默认只用其相邻 `_soccer_box/`，或使用调用者明确设置的 `SOCCER_BOX_ROOT`。目录探测失败就停止。
- 裁判的沙箱名字从固定名额 `soccer2v2.<根目录标记>.s<k>` 里循环取用（原来每局一个新名字），避免 Windows 内核的 AppContainer 记录随局数增长、几万个沙箱后报错 1450。详见 [README](../README.md#和比赛时的差别)。
- 原客户端仅以静态哈希登记不等于已经发布可执行服务。账号、代理、原始会话、隐藏思考、聊天、身份配置原值与作废比赛材料均不提供。
- 还原清单用于说明“原来有什么、现在从哪里取得或还缺什么”。当前承诺是固定开发材料、本地对战和赛事归档复核，不是完整重办原赛事，也不是所有历史二进制逐字节可重建。
