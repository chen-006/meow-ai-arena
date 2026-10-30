[English](README_en.md)

# meow-ai-arena · AI 算法对抗大赛

让一群 AI 模型拿到**一字不差的同一句提示词**，各自在约 60 分钟里写出一个游戏 bot，再把这些 bot 扔进同一个赛场打几千到几万局，看谁最强。

这里是视频系列"AI 算法对抗大赛"的全部比赛材料：题目、裁判、每个模型交上来的原样代码、逐局结果、评分脚本，以及可以直接上手的一键脚本。你可以看回放、复现正式赛，也可以**写一个自己的 bot，和这些 AI 一较高下**。

第 4 集换了玩法：不写 bot，7 个模型直接当玩家打七国《外交》，自己谈判、结盟、背刺。

第 5 集又换了一种：不打对战，12 个模型分成 6 支双人队，接力优化一份又慢又乱的老代码，还要互相给对方的代码找反例。

第 6 集回到 bot 对战，改成上下半场：14 个模型先写第一版、互相打一轮，中场拿到成绩、自己的回放和对手的程序，再改一版。

第 7 集《区王争霸赛》有两场比赛：第一场光轮对战（Tron），10 个模型各写一个 bot，打 1 对 1、4 人局、12 人局；第二场屎山优化，同样这 10 个模型各自优化第 5 集那份又慢又乱的仓储调度代码。两场按名次计分相加，得分最高的是“区王”。

第 9 集走进峡谷：模仿《英雄联盟》的 MOBA，一个程序控制一支 5 人队伍（上单战士、打野刺客、中单法师、射手、辅助），三路兵线、防御塔、野区野怪，摧毁对方水晶获胜。地图大小和 NPC 的数量与强度分三档难度；单人挑战时对面是引擎控制的 NPC 队。本集先收录了 5 个主办方代写的示例 bot 之间的循环赛，正式赛的跑法见那一集的 README。

| 集 | 游戏 | 人数 | 局数 | 冠军 | 目录 |
|---|---|---|---:|---|---|
| 第 1 集 | 圈地 | 1v1 单挑 + 4 人混战 | 30000 | Sonnet 5.5 | [`land/`](land/) |
| 第 2 集 | 炸弹人 | 2–10 人 | 8930 | GPT-6 Astra | [`bomberman/`](bomberman/) |
| 第 3 集 | 夺旗 | 2–15 人 | 32010 | GPT-6 Astra / GPT-6.1 Sol（并列） | [`ctf/`](ctf/) |
| 第 4 集 | 外交 | 7 人，模型直接当玩家 | 3 | Fable 5.1（三局总分；第 3 局单独获胜） | [`diplomacy/`](diplomacy/) |
| 第 5 集 | 仓储调度 | 12 人，6 支双人队接力优化 | 每轮 1 次 | 队F：MiMo V2.6 Pro + GPT-6 Astra（团队总分；第 2、3 轮是重赛） | [`warehouse/`](warehouse/) |
| 第 6 集 | 坦克大战 | 1v1，14 人，上下半场 | 27840 | GPT-6 Astra | [`tank/`](tank/) |
| 第 7 集 · 第一场 | 光轮对战 | 1v1 + 4 人 + 12 人，10 人（另有 2 位参考选手） | 5713 | Mistral Large 4 | [`tron/`](tron/) |
| 第 7 集 · 第二场 | 屎山优化（仓储调度） | 10 人各自单独优化（另有 Claude 3 位作参照） | 每人 1 次 | 阶跃 Step 5 preview / 腾讯混元 Hy4 preview（并列） | [`warehouse/`](warehouse/#第-7-集--屎山优化) |
| 第 8 集 | 2v2 简化足球 | 12 个模型，6 支双人队；下半场新增个人队 | 8280 | 团队：GPT-6 Astra + GPT-6.1 Terra；个人：GPT-6 Astra | [`soccer/`](soccer/) |
| 第 9 集 | 峡谷对决（MOBA） | 5v5，一个程序控一支队伍（另有单人难度阶梯挑战） | 示例赛循环赛 | 见 [`moba/`](moba/) | [`moba/`](moba/) |

第 7 集两场合计的“区王”是阶跃 Step 5 preview（两场各按名次给 10～1 分相加，见 [`tron/README.md`](tron/README.md) 文末）。

参赛模型包括 Claude（Opus、Sonnet、Fable、Haiku）、GPT（Astra、Sol）、DeepSeek、Qwen、Kimi、GLM、MiMo、豆包、Gemini、Grok、Muse、Mistral、LongCat、阶跃、盘古、混元、文心、dots、Mercury 等，每集略有不同。完整排名见各集 README。

## 30 秒上手

**只想看比赛**：不需要装任何东西。用浏览器打开任意一集 `replays/` 文件夹里的 `.html` 文件，就能逐回合播放。第 1 集打开 `land/replays/index.html`；第 4 集的回放带着全部私信，打开 `diplomacy/replays/game3.html` 看最后一局。第 5 集没有回放，成绩在 `warehouse/results/最终成绩.md`。第 6 集打开 `tank/replays/final-astra-vs-soluf.html` 看冠亚军。第 7 集光轮对战的回放是随机抽的 30 局，在 `tron/replays/`（清单见那里的 `README.md`）；屎山优化没有回放，成绩在 `warehouse/README.md` 的第 7 集一节。第 9 集的 MOBA 回放全是 SVG 现场画的，随便开一份 `moba/replays/` 里的都好看。

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

**可以交什么**：第 1、2、3、6 集的 bot 和第 7 集的光轮 bot（自己写的、让 AI 写的、在官方选手代码上改的都行），第 5 集优化过的仓储调度代码，或者修好了某份模型代码里已知缺陷的版本。第 4 集没有可以提交的程序，不收。

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

`community/` 是观众投稿和社区榜，见上面的“来投稿”。`arena/` 是第 3 集起使用的通用对战平台（赛程、Bradley–Terry 评分）。`docs/` 里有评分方法和"版本演化"实验的说明。

## 关于公平性

- 每个模型只测一次，单次结果有偶然性。工具（agent harness）、思考档、接入渠道都会影响成绩，各集的具体条件都写在 `CONDITIONS.md` 里。第 3 集的附加实验显示，同一个模型只换工具，实力值能差 120 分以上。
- 评分口径：第 1 集是 0–100 归一化；第 2、3 集是"基准 = 0、不封顶"的 Bradley–Terry 实力值；第 4 集是《外交》的局分（单独获胜 100，和局按中心数平方分配），只有三局；第 5 集是 7 个分项按权重加总的团队分（0–100），每个模型每轮只跑一次，另有主程和测试手两张个人榜；第 6 集是 1 对 1 的 Bradley–Terry 实力值（基准 = 0），每个模型每个半场只跑一次，有三位选手在另一台更快的电脑上由外部操作员运行；第 7 集光轮对战是三个局型各一个 Bradley–Terry 实力值按 50/30/20 加权（豆包 = 0，锚点是赛后定的），屎山优化是 8 组隐藏负载的几何平均加速比，两场都每个模型只跑一次。**不同集的分数不能直接比较。**
- 选手代码均为模型生成，主办方未做修改。题目、规则和裁判由主办方编写。第 4 集里的私信、命令和笔记都是模型自己写的，原样保存。第 5 集里各轮提交的代码、交接文档、反例和说明、盲评评分、复盘也都是模型写的，原样保存；把反例归成缺陷点是主办方做的。第 5 集附录里人工队的代码是两位人类选手写的（其中一份是带着 Gemini 写的）。第 6 集的出题方参照（`tank/tournament/sparring.cpp`）是主办方写的，只作参照、不排名。第 7 集光轮对战的两个陪练（`tron/sparring/`）是主办方的子代理写的，强档陪练只作参考线、不占名次；附加实验里用到的 2010 年人类选手 bot 大多没有开源许可，都不放进本仓库，只在 `tron/README.md` 列了出处和致谢。

## 许可

[MIT](LICENSE)。欢迎拿去复现、改编、出新题。第 4 集回放里的地图图形来自 jDip（GPL），经 `diplomacy` 包渲染，不适用 MIT 许可。

## 致谢

视频里的角色形象大多来自 B 站博主 **ZipZipPipe**（[主页](https://space.bilibili.com/4168597)），特此致谢。按其主页公告，鲸鱼娘形象是基于上善无形原创角色的二创（原作来源 [b23.tv/3dNz55h](https://b23.tv/3dNz55h)），以 [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/deed.zh-hans)（署名、非商业性使用、相同方式共享）授权。

角色形象、配音和剪辑都不在本仓库内，也不适用本仓库的 MIT 许可；如需使用角色形象，请遵守原作者的协议。
