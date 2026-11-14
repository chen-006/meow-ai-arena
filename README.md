[English](README_en.md)

# meow-ai-arena · AI 算法对抗大赛

让一群 AI 模型拿到**一字不差的同一句提示词**，各自在约 60 分钟里写出一个游戏 bot，再把这些 bot 扔进同一个赛场打几千到几万局，看谁最强。有几集换了玩法：模型直接当玩家下《外交》、接力优化老代码、设计桥梁、排兵布阵。

这里是视频系列"AI 算法对抗大赛"的全部比赛材料：题目、裁判、每个模型交上来的原样代码、逐局结果、评分脚本，以及可以直接上手的一键脚本。你可以看回放、复现正式赛，也可以**写一个自己的 bot，和这些 AI 一较高下**。

## 各集一览

| 集 | 题目 | 目录 | 比什么 | 冠军 | 复现入口 | 网页版 |
|---|---|---|---|---|---|---|
| 1 | 圈地 | [`land/`](land/) | 9 个模型各写一个 bot，同一个 bot 打 1v1 单挑和 4 人混战，30000 局 | Sonnet 5.5 | [复现正式赛](land/README.md#复现正式赛) | [ep1](https://arena.meowllm.top/ep1/) |
| 2 | 炸弹人 | [`bomberman/`](bomberman/) | 9 个模型，2–10 人同场放炸弹、缩圈，8930 局 | GPT-6 Astra | [`tournament/`](bomberman/tournament/README.md) | [ep2](https://arena.meowllm.top/ep2/) |
| 3 | 夺旗 | [`ctf/`](ctf/) | 11 个模型，2–15 个阵营抢旗子送回基地，32010 局 | GPT-6 Astra / GPT-6.1 Sol（并列） | [`tournament/`](ctf/tournament/README.md) | [ep3](https://arena.meowllm.top/ep3/) |
| 4 | 外交 | [`diplomacy/`](diplomacy/) | 不写 bot：7 个模型直接当玩家打七国《外交》，谈判、结盟、背刺，3 局 | Fable 5.1（三局总分；第 3 局单独获胜） | [`games/`](diplomacy/games/) 三局存档 | — |
| 5 | 仓储调度 | [`warehouse/`](warehouse/) | 不打对战：12 个模型分成 6 支双人队，接力优化一份又慢又乱的老代码，还要给别队找反例 | 队F：MiMo V2.6 Pro + GPT-6 Astra（团队总分；第 2、3 轮是重赛） | [`referee/`](warehouse/referee/README.md) | — |
| 6 | 坦克大战 | [`tank/`](tank/) | 14 个模型 1v1，上下半场：中场拿到成绩、回放和对手的程序再改一版，27840 局 | GPT-6 Astra | [`tournament/`](tank/tournament/README.md) | [ep6](https://arena.meowllm.top/ep6/) |
| 7 · 第一场 | 光轮对战 | [`tron/`](tron/) | 10 个模型（另有 2 位参考选手），1v1、4 人、12 人三种局型，5713 局 | Mistral Large 4 | [`tournament/`](tron/tournament/README.md) | [ep7](https://arena.meowllm.top/ep7/) |
| 7 · 第二场 | 屎山优化 | [`warehouse/`](warehouse/#第-7-集--屎山优化) | 同样 10 个模型各自优化第 5 集那份老代码（另有 Claude 3 位作参照） | 阶跃 Step 5 preview / 腾讯混元 Hy4 preview（并列） | [`results/ep7/`](warehouse/results/ep7/README.md) | — |
| 8 | 2v2 简化足球 | [`soccer/`](soccer/) | 12 个模型两两组队、一人写一名球员；下半场加个人队，8280 局 | 团队：GPT-6 Astra + GPT-6.1 Terra；个人：GPT-6 Astra | [`tournament/`](soccer/tournament/README.md) | [ep8](https://arena.meowllm.top/ep8/) |
| 9 | 造桥 | [`bridge/`](bridge/) | 不写 bot：9 个模型在 90 分钟里设计桥梁，6 关闯关 + 1 关桥和捣蛋车攻防 | GPT-6.2 Sol | [`tournament/`](bridge/tournament/README.md) | [ep9](https://arena.meowllm.top/ep9/) |
| 10 | 纸上谈兵 | [`battle/`](battle/) | 9 个模型在 90 分钟里买兵布阵、写指挥程序，刚体物理里开打：5 关闯关比谁花得少 + 1v1 和四人混战盲摆对打，945 局 | GPT-6.2 Sol | [`tournament/`](battle/tournament/README.md) | — |

第 7 集两场合计的“区王”是阶跃 Step 5 preview（两场各按名次给 10～1 分相加，见 [`tron/README.md`](tron/README.md) 文末）。

每集 README 的开头都写着这一集是什么、目录里有什么、怎么复现榜单。视频在 B 站搜“AI 算法对抗大赛”；网页版（[arena.meowllm.top](https://arena.meowllm.top/)）不用装任何东西，可以在浏览器里看回放、和 AI 写的 bot 对打。

参赛模型包括 Claude（Opus、Sonnet、Fable、Haiku）、GPT（Astra、Sol）、DeepSeek、Qwen、Kimi、GLM、MiMo、豆包、Gemini、Grok、Muse、Mistral、LongCat、阶跃、盘古、混元、文心、dots、Mercury 等，每集略有不同。完整排名见各集 README。

仓库顶层的其他目录：

| 目录 | 内容 |
|---|---|
| [`community/`](community/README.md) | 观众投稿和[社区榜](community/LEADERBOARD.md)，见下面的“来投稿”；`community/contests/` 是社区自建的比赛 |
| [`arena/`](arena/README.md) | 通用对战平台：单局、赛程、Bradley–Terry 评分。只有第 3 集夺旗的正式赛用它办，其余各集在自己的 `tournament/` 里自带赛事脚本 |
| [`docs/`](docs/) | [评分方法](docs/scoring.md)、[版本演化实验](docs/version-evolution.md)、[开源范围](docs/open-source-scope.md) |
| [`art/`](art/README.md) | 从第 10 集起的角色立绘、表情差分、生图提示词和脚本 |
| [`characters/`](characters/README.md) | 各家 AI 的体素小人（模型、方块数据、预览图） |
| [`audio/`](audio/README.md) | 背景音乐和音效的清单（曲名、出处）、配音用的音色名、提示词和参数、逐句语气表 |
| `start.cmd`、`start.sh`、`start.py` | 一键菜单：看回放、用你的 bot 挑战 AI、投稿、检查环境（目前收录第 1～7 集） |

## 30 秒上手

**只想看比赛**：不需要装任何东西。用浏览器打开任意一集 `replays/` 文件夹里的 `.html` 文件，就能逐回合播放。第 1 集打开 `land/replays/index.html`；第 4 集的回放带着全部私信，打开 `diplomacy/replays/game3.html` 看最后一局。第 5 集没有回放，成绩在 `warehouse/results/最终成绩.md`。第 6 集打开 `tank/replays/final-astra-vs-soluf.html` 看冠亚军。第 7 集光轮对战的回放是随机抽的 30 局，在 `tron/replays/`（清单见那里的 `README.md`）；屎山优化没有回放，成绩在 `warehouse/README.md` 的第 7 集一节。第 8 集打开 `soccer/workspace/viewer.html`，把 `soccer/replays/` 里的 `.txt` 拖进去。第 9 集打开 `bridge/replays/` 里的 `.html`。第 10 集的回放用 `python battle/replays/unpack.py` 解压后，在 `battle/workspace/` 下用 `python play.py replay` 在终端里看（只要 Python），或用 `battle/renderer/` 渲染成视频，见 [`battle/replays/`](battle/replays/README.md)。也可以直接上[网页版](https://arena.meowllm.top/)。

**想用自己的 bot 挑战 AI**：

1. 装好 [Python 3.10+](https://www.python.org/downloads/) 和 C++ 编译器（g++）。Windows 推荐免安装的 [w64devkit](https://github.com/skeeto/w64devkit/releases)，解压后把 `w64devkit` 文件夹放进本仓库的 `tools/` 文件夹即可，不用改环境变量。
2. Windows 双击 **`start.cmd`**；macOS / Linux 运行 `sh start.sh`。
3. 在菜单里选"挑战 AI"，选一集，把你的 `.cpp` 拖进窗口，或者直接回车用模板。几分钟后得到你在所有 AI 中的排名。

也可以不用菜单，直接在命令行运行：

```bash
cd ctf
python challenge.py 你的bot.cpp
```

**想写 bot**：先读那一集的 `workspace/RULES.md`（规则与输入输出协议），再看 `workspace/` 里的模板。第 1–3 集的模板已经写好了输入解析；第 6 集只实现 `init` 和 `decide`，使用官方 `tank.hpp`，由官方 `tank_main.cpp` 负责输入输出，不要自己写 `main`。`workspace/` 就是当时发给 AI 的选手包，里面的本地裁判和比赛时完全一样。各个 AI 的代码在 `bots/` 里，可以直接拿来当对手或参考。

## 来投稿

你的作品可以交到这个仓库里，合并后进[社区榜](community/LEADERBOARD.md)。

**可以交什么**：第 1、2、3、6 集的 bot 和第 7 集的光轮 bot（自己写的、让 AI 写的、在官方选手代码上改的都行），第 5 集优化过的仓储调度代码，或者修好了某份模型代码里已知缺陷的版本。第 4 集没有可以提交的程序，不收；第 8 集足球目前没有投稿通道。第 9 集造桥的投稿走网站排行榜，不走 Pull Request，见 [`bridge/README.md`](bridge/README.md#投稿)。

**最快的办法**是交给你的 coding agent（Claude Code、Codex、Cursor 等）。把下面这句话发给它，它会问你几个问题，在本地测一遍，然后把要提交的内容给你过目，你同意后再开 Pull Request：

```text
请阅读 https://github.com/chen-006/meow-ai-arena/blob/main/community/AGENT_PROMPT.md 里的“提示词 A”，按它的步骤帮我投稿。
```

还没有作品、想让 AI 从零写一个的，用同一个文件里的“提示词 B”。提示词全文在 [`community/AGENT_PROMPT.md`](community/AGENT_PROMPT.md)，也可以整段复制。

**自己动手**是三步：

1. fork 本仓库，新建文件夹 `community/<集>/<你的GitHub用户名>__<作品名>/`，放进 `bot.cpp`（第 5 集是 `src/`）和 `meta.json`。
2. 运行 `python community/check.py community/<集>/<你的文件夹>`，它会检查格式并跑一遍评测。
3. 只提交这一个文件夹，开 Pull Request，把自测摘要贴进描述。

第 6 集目录名用 `tank`，第 7 集光轮对战用 `tron`，详情见 [投稿规则](community/README.md#代码要遵守的规则)。Tron 只实现 `int decide(const tron::State&)`，使用官方 `tron.hpp` 和 `tron_main.cpp`，不要自己写 `main`；本地编译和评测需要 Windows。Tank / Tron 投稿支持需要先合并到目标分支，再提交作品；PR 评测使用目标分支的可信脚本。

之后 GitHub Actions 会自动评测，维护者看过后合并。社区榜是在 GitHub 的虚拟机上测的，局数少，和节目里的正式成绩不能直接比。规则、`meta.json` 的写法和成绩口径见 [`community/README.md`](community/README.md)。

## 每集目录里有什么

第 1～3 集的目录结构如下，后面各集在此基础上有增减，下面逐集说明。以后新一集的推荐结构见 [开源范围](docs/open-source-scope.md#每一集的目录)。

| 路径 | 内容 |
|---|---|
| `README.md` | 规则速览、成绩、怎么玩 |
| `CONDITIONS.md` | 比赛条件：工具、版本、思考档、渠道、用时，以及**所有中断、补时和事故**的完整记录 |
| `prompt.md` | 发给模型的提示词原文 |
| `workspace/` | 选手包：规则、引擎、本地裁判、回放查看器、模板、基准 |
| `bots/` | 各模型交卷的原样代码 |
| `results/` | 正式赛成绩和逐局结果（压缩 JSON） |
| `replays/` | 精选回放：指定对阵或人数里取典型的一局，不挑反杀 |
| `tournament/` | 正式赛脚本和配置，可复现赛程 |
| `challenge.py` | 一键挑战 |
| `appendix/` | 不计成绩的附加实验 |

第 4 集的目录结构不同（没有 bot 和挑战脚本，换成三局存档、模型笔记和裁判 / 调度程序），见 [`diplomacy/README.md`](diplomacy/README.md)。

第 5 集也不同（没有 bot、回放和挑战脚本，换成三轮的选手包、各模型提交的代码和反例、盲评和复盘、裁判程序和隐藏负载），见 [`warehouse/README.md`](warehouse/README.md)。这一集另有一支人工队：两位人类选手做了第 1 轮的任务，代码和赛后另测的成绩在 [`warehouse/appendix/human/`](warehouse/appendix/human/)，不计入正式成绩。

第 6 集回到 bot 对战，但每个模型交两版：`bots/half1/` 是上半场版本，`bots/half2/` 是下半场版本；`results/` 里有中场赛、终场赛、进步赛三场；另有赛前狠话和复盘（`notes/`），见 [`tank/README.md`](tank/README.md)。

第 7 集光轮对战（`tron/`）的结构和第 6 集相近，只有一个半场；另有两位参考选手和主办方的强档陪练线，单独出一张强对手榜；没有赛前狠话和赛后复盘（这一期没有收集狠话，复盘消息没有发）。陪练在比赛时只给了可执行程序，开源版放源码，用 `tron/make_player_pack.py` 编译。第 7 集屎山优化并进了 `warehouse/`（`submissions/ep7/`、`results/ep7/`、`packages/ep7/`），见 [`warehouse/README.md`](warehouse/README.md) 文末。

第 8 集足球（`soccer/`）也分上下半场：`bots/half1/`、`bots/half2/` 是两个半场的程序；`prompts/` 是开赛、中场、续跑的全部主办方消息，`packages/` 是赛时包和队聊说明，`sparring/` 是基准和陪练，`notes/` 是赛后复盘回答；没有挑战脚本，见 [`soccer/README.md`](soccer/README.md)。

第 9 集造桥（`bridge/`）的结构不同：没有 bot 和挑战脚本，`designs/` 是每人 8 份设计 JSON（第 1～6 关、决赛桥、捣蛋车），`reference/` 是参考答案，`engine/` 是物理内核源码（用 `bridge/build_engine.py` 编译），`tournament/score.py` 重算并核对全部成绩，见 [`bridge/README.md`](bridge/README.md)。

第 10 集纸上谈兵（`battle/`）的选手作品是布阵 JSON 加一个 C++ 指挥程序，放在 `bots/`；`engine/` 是模拟器源码（用 `battle/build_engine.py` 编译），`tournament/score.py` 用比赛时的计分脚本重算并核对全部成绩，`--rerun` 从选手作品重打全部 945 局；`renderer/` 是把回放渲染成视频的渲染器。这一集起，角色立绘、表情差分和体素小人放在仓库顶层的 [`art/`](art/README.md) 和 [`characters/`](characters/README.md)。见 [`battle/README.md`](battle/README.md)。

## 关于公平性

- 每个模型只测一次，单次结果有偶然性。工具（agent harness）、思考档、接入渠道都会影响成绩，各集的具体条件都写在 `CONDITIONS.md` 里。第 3 集的附加实验显示，同一个模型只换工具，实力值能差 120 分以上。
- 评分口径：第 1 集是 0–100 归一化；第 2、3 集是"基准 = 0、不封顶"的 Bradley–Terry 实力值；第 4 集是《外交》的局分（单独获胜 100，和局按中心数平方分配），只有三局；第 5 集是 7 个分项按权重加总的团队分（0–100），每个模型每轮只跑一次，另有主程和测试手两张个人榜；第 6 集是 1 对 1 的 Bradley–Terry 实力值（基准 = 0），每个模型每个半场只跑一次，有三位选手在另一台更快的电脑上由外部操作员运行；第 7 集光轮对战是三个局型各一个 Bradley–Terry 实力值按 50/30/20 加权（豆包 = 0，锚点是赛后定的），屎山优化是 8 组隐藏负载的几何平均加速比，两场都每个模型只跑一次；第 8 集按积分得分率排名（胜 3、平 1），Bradley–Terry 实力值只作参考；第 9 集的总分是 7 关的平均，第 1～6 关按造价给分（和参考答案一样便宜得 100），第 7 关是攻防分；第 10 集是闯关 60% + 对抗 40%，关卡分以全场最省的过关者为 100，对抗是 1v1 胜率和四人混战得分按 60/40 加权。**不同集的分数不能直接比较。**
- 选手代码均为模型生成，主办方未做修改。题目、规则和裁判由主办方编写。第 4 集里的私信、命令和笔记都是模型自己写的，原样保存。第 5 集里各轮提交的代码、交接文档、反例和说明、盲评评分、复盘也都是模型写的，原样保存；把反例归成缺陷点是主办方做的。第 5 集附录里人工队的代码是两位人类选手写的（其中一份是带着 Gemini 写的）。第 6 集的出题方参照（`tank/tournament/sparring.cpp`）是主办方写的，只作参照、不排名。第 7 集光轮对战的两个陪练（`tron/sparring/`）是主办方的子代理写的，强档陪练只作参考线、不占名次；附加实验里用到的 2010 年人类选手 bot 大多没有开源许可，都不放进本仓库，只在 `tron/README.md` 列了出处和致谢。

## 许可

整个仓库使用 [MIT 许可](LICENSE)：游戏本体（引擎、规则、地图和关卡）、选手包、选手作品、裁判和计分脚本、成绩和回放、文档、社区投稿，以及从第 10 集起随视频公开的渲染器、美术素材、角色立绘和表情差分、体素小人、生图提示词，都是 MIT。可以自由使用、修改、再发布，包括商业用途，保留版权和许可声明即可；用渲染器、立绘、体素小人做出来的画面和视频也一样。欢迎拿去复现、改编、出新题。什么公开、什么不公开、以后每集的目录怎么放，见 [开源范围](docs/open-source-scope.md)。

例外（这些目录自带许可，沿用不变）：

- 第 4 集的裁判依赖 [`diplomacy`](https://github.com/diplomacy/diplomacy) 包（AGPL-3.0 或更高版本，本仓库不包含）。回放网页里的地图图形来自 jDip（GPL），经这个包渲染，所以回放网页整体不适用 MIT 许可，见 [`diplomacy/README.md`](diplomacy/README.md#许可说明)。
- 社区比赛 [`community/contests/rmuc2026/`](community/contests/rmuc2026/) 基于 Arena-1，沿用它自己目录里的非商业学习许可，不适用 MIT。
- 第三方依赖（物理库、Blender 等）不打包进本仓库，按各自的许可使用。

## 署名与致谢

**从第 10 集起，视频里的角色形象换成了新的一套**，立绘、表情差分、体素小人和生图提示词都随仓库以 MIT 公开（见 [`art/`](art/README.md)、[`characters/`](characters/README.md)）：

- **角色形象设计：小叶子XIYZ（B 站）**。新形象参考自他的设计，由节目组制作。
- **美术和制作：喵了个猫了个咪了个猫**。

使用这些形象时，请写上"角色形象设计：小叶子XIYZ；美术和制作：喵了个猫了个咪了个猫"，并附本仓库链接。

第 1～9 集视频里的角色形象大多来自 B 站博主 **ZipZipPipe**（[主页](https://space.bilibili.com/4168597)），特此致谢。按其主页公告，鲸鱼娘形象是基于上善无形原创角色的二创（原作来源 [b23.tv/3dNz55h](https://b23.tv/3dNz55h)）。这些旧立绘不在本仓库内，也不适用本仓库的许可；如需使用，请遵守原作者的协议。往期目录里的旧图和旧说明保持原样。

视频的配音、音乐和剪辑不在本仓库内。

角色名里的模型名和公司名属于各自的权利人，只用来说明角色对应哪个模型，不表示这些公司认可或参与本项目。
