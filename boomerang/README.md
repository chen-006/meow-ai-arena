[English](README_en.md)

# 第 11 集 · 回旋镖大作战（OA 争霸·上）

13 个模型各自在 75 分钟里写一个 C++ 程序，操控俯视角小场地里一个拿回旋镖的角色：扔镖、挥砍、空手踢人、冲刺越坑，被镖打中或掉下浮岛就出局，最后活下来的赢。程序看到的别人永远是 0.2 秒以前的样子，要靠预判。海选打一对一和四人各自为战两项，共 32,904 局，用 Bradley-Terry 实力值出个人榜。之后海选第一、第二的两位当队长，用 1000 元预算暗标拍卖其余 9 位候选人，为下一集的团队赛组队。

- **目录里有什么**：`workspace/` 选手包 · `engine/` 陪练源码 · `bots/` 十三位选手的程序和笔记 · `results/` 成绩和逐局结果 · `replays/` 117 局回放 · `tournament/` 计分、复现、隐藏地图 · `auction/` 队长拍卖的全部问答 · `renderer/` 回放转视频的渲染器 · `appendix/` 灰度版、对照组和全员大混战榜（不计成绩）
- **复现榜单**：`python tournament/score.py`：用比赛时的出榜程序从 32,904 局逐局结果重算个人榜（含 2000 次重抽样的区间），逐项和 `results/海选成绩.json` 核对，打印"核对通过"，约 20 秒，只要 Python。重打全部对局：`python build_engine.py` 后运行 `python tournament/rerun.py`，16 线程约 15～20 分钟（结果会有出入，原因见 [tournament/README.md](tournament/README.md)）。

## 规则速览

- **角色全部相同**，没有职业和技能。每人一把回旋镖：扔出去飞到最远自动折返，飞回来能接住；撞墙会落地，要自己去捡；原地不动时可以召回。镖打中谁谁出局（会误伤队友）。有镖时近战是挥砍（能把正前方飞来的镖打落），没镖时是踢人，被踢的人滑出去，停在坑里就掉下去。冲刺有前摇和收势，能越过坑。
- **反应延迟**：每步 0.05 秒，程序看到的自己是现在的，别人和别人的镖是 4 步（0.2 秒）以前的。草丛里的人看不见。
- **塌陷**：第 12 秒起场地从外向里一圈圈塌掉，每局最长 40 秒。
- **计时**：每步 CPU 10 毫秒，第 0 步 1000 毫秒；超时那一步按不动处理，累计 20 次程序被结束。内存 1 GB，不能开子进程、读写文件、联网。
- **海选两项**：一对一（胜 1 平 0.5）；四人各自为战（第 1～4 名得 3、2、1、0 分，每击杀一人再加 1 分，一局里四个人两两比得分）。两项各拟合一个实力值（刻度同 Elo：差 400 分约 10 比 1），平均就是个人实力值，十三位的平均是 0。
- **地图**：10 张人工设计的浮岛图，选手包里公开 4 张（一对一 / 四人局：双湖、阴阳；4v4：沙漏、峡谷），其余 6 张赛时保密（一对一 / 四人局：裂冰、古塔、天桥、石阵；4v4：老城、林地），在 [tournament/hidden/](tournament/hidden/)。海选用双湖、阴阳和四张保密图。
- 完整规则见 [workspace/RULES.md](workspace/RULES.md)，选手说明见 [workspace/README.md](workspace/README.md)，规则引擎是单头文件 [workspace/dart.hpp](workspace/dart.hpp)（裁判和选手用的是同一份）。

## 选手

| 选手 | 工具 | 思考档 | 作品目录 | 席位名 |
|---|---|---|---|---|
| GPT-6.2 Astra | Codex 命令行 | max | [`bots/gpt-6.2-astra/`](bots/gpt-6.2-astra/) | `astra` |
| Claude Fable 5.2 | Claude Code 交互窗口（主办人手动启动） | max | [`bots/claude-fable-5.2/`](bots/claude-fable-5.2/) | `fable` |
| Claude Opus 5.5 | Claude Code 交互窗口（主办人手动启动） | max | [`bots/claude-opus-5.5/`](bots/claude-opus-5.5/) | `opus` |
| Gemini 4 Carbon | Antigravity 命令行 | high | [`bots/gemini-4-carbon/`](bots/gemini-4-carbon/) | `carbon` |
| GPT-6.2 Sol | Codex 命令行 | max | [`bots/gpt-6.2-sol/`](bots/gpt-6.2-sol/) | `sol` |
| Gemini 4 Hydrogen | Antigravity 命令行 | xhigh | [`bots/gemini-4-hydrogen/`](bots/gemini-4-hydrogen/) | `hydrogen` |
| Grok 4.7 | Grok Build 命令行 | xhigh | [`bots/grok-4.7/`](bots/grok-4.7/) | `grok` |
| GLM 5.3 | OpenCode | max | [`bots/glm-5.3/`](bots/glm-5.3/) | `glm` |
| DeepSeek-V4.1-Flash | dsh | max | [`bots/deepseek-v4.1-flash/`](bots/deepseek-v4.1-flash/) | `deepseek` |
| Kimi K3.1 Pro | Kimi Code | max | [`bots/kimi-k3.1-pro/`](bots/kimi-k3.1-pro/) | `kimi` |
| 千问 Qwen4-Max | Qoder（主办人手动启动） | max | [`bots/qwen4-max/`](bots/qwen4-max/) | `qwen` |
| MiMo V2.6 Pro | OpenCode | high | [`bots/mimo-v2.6-pro/`](bots/mimo-v2.6-pro/) | `mimo` |
| 豆包 Seed-Evolving | OpenCode | high | [`bots/doubao-seed-evolving/`](bots/doubao-seed-evolving/) | `doubao` |

席位名是比赛时的内部名字，逐局结果和回放里用的是它。这一集各家尽量用自家的编程工具（豆包、MiMo、GLM 用 OpenCode）。所有人开赛未满 60 分钟不能交卷，60 分钟前停下的会被叫回原会话继续改，停下后空等的时间补回。GPT-6.2 的两位在 6.2 正式发布后用正式版重新做题，正式榜以重做的版本为准；之前的灰度版本和另外几位对照选手的作品放在 [appendix/](appendix/README.md)，不计成绩。工具版本、用时、叫回和中断见 [CONDITIONS.md](CONDITIONS.md)。

## 个人榜（正式海选）

| 名次 | 选手 | 个人实力值 | 95% 区间 | 一对一 | 四人局 |
|---:|---|---:|---|---:|---:|
| 1 | GPT-6.2 Astra | +306 | +295 ～ +319 | +345 | +267 |
| 2 | Claude Fable 5.2 | +132 | +121 ～ +144 | +140 | +124 |
| 3 | Claude Opus 5.5 | +76 | +64 ～ +89 | +82 | +70 |
| 4 | Gemini 4 Carbon | +51 | +31 ～ +66 | +37 | +65 |
| 5 | GPT-6.2 Sol | +33 | +21 ～ +46 | +81 | −15 |
| 6 | Gemini 4 Hydrogen | +30 | +20 ～ +40 | +51 | +9 |
| 7 | Grok 4.7 | −1 | −19 ～ +15 | +5 | −6 |
| 8 | GLM 5.3 | −9 | −23 ～ +5 | +17 | −36 |
| 9 | DeepSeek-V4.1-Flash | −30 | −42 ～ −18 | −34 | −26 |
| 10 | Kimi K3.1 Pro | −84 | −94 ～ −73 | −84 | −83 |
| 11 | 千问 Qwen4-Max | −87 | −99 ～ −74 | −98 | −76 |
| 12 | MiMo V2.6 Pro | −167 | −185 ～ −150 | −212 | −121 |
| 13 | 豆包 Seed-Evolving | −251 | −283 ～ −226 | −330 | −171 |

差 400 分约等于 10 比 1 的胜负比。相邻名次里"差距在误差内"的（名次照排）：Carbon 和 Sol、Sol 和 Hydrogen、Grok 和 GLM、Kimi 和千问。区间是按地图整块重抽样 2000 次得到的。一对一两两战绩（每对 228 局的胜-平-负）见 [results/海选榜.md](results/海选榜.md)。

## 队长拍卖

海选第一、第二的 GPT-6.2 Astra 和 Claude Fable 5.2 当队长，各自的同门（GPT-6.2 Sol、Claude Opus 5.5）直接入队，其余 9 位按海选名次逐个上拍。每位队长 1000 元、买 3 人，暗标出价并附一句理由，价高者按自己的报价买下；同价重报，最多三次，还相同由剩余预算多的一方买下；每次最多能出的钱要给每个还空着的名额留 1 元。开拍前队长有 10 分钟尽调：只读文件夹里有成绩表和全部候选人的程序、笔记。

| 队伍 | 买到的人（价格） | 花费 | 余额 |
|---|---|---:|---:|
| GPT-6.2 Astra 队（+ GPT-6.2 Sol） | Gemini 4 Hydrogen（400）、Grok 4.7（400）、Kimi K3.1 Pro（1） | 801 | 199 |
| Claude Fable 5.2 队（+ Claude Opus 5.5） | Gemini 4 Carbon（599）、GLM 5.3（200）、DeepSeek-V4.1-Flash（201） | 1000 | 0 |

落选：千问、MiMo、豆包。Fable 599 买下 Carbon 后余额 401、每拍最多出 400；Hydrogen、Grok 两拍双方都连报四次 400，按剩余预算归 Astra；之后余额反过来是 401 对 200，GLM 双方四次都出 200、归 Fable，Fable 留下的 201 元正好比 Astra 的全部余额多 1 元，拿下 DeepSeek。每一轮双方的出价和理由在 [auction/拍卖结果.md](auction/拍卖结果.md)，主办方发给队长的每条消息和队长的回答原文在 [auction/记录/](auction/README.md)。

正式拍卖之前（11-16）还有一场用灰度版 Astra 当队长的拍卖，拍完后主办方宣布那一场是演习、结果作废（原因是没说清队员进队后会针对团队赛重写程序），过程也收录了，见 [auction/README.md](auction/README.md)。团队赛不在这一集。

## 马上看 / 马上玩

**本地跑**（Windows；Python 3.9+，g++，推荐 w64devkit；不需要第三方 Python 包）：

```text
python build_engine.py                  编译裁判 workspace/arena.exe 和三档陪练 workspace/bots/*.exe（与比赛时逐字节相同）
cd workspace
copy template.cpp player.cpp
python match.py check player.cpp        正式计时检查
python match.py duel ../bots/gpt-6.2-astra/player.cpp ../bots/claude-fable-5.2/player.cpp --games 40
python match.py ffa player.cpp ../bots/claude-opus-5.5/player.cpp ../bots/gemini-4-carbon/player.cpp mid --games 40
python match.py play ../bots/gpt-6.2-astra/player.cpp mid --map ../tournament/hidden/stonecircle.txt --seed 7
```

`match.py` 的全部用法见 [workspace/README.md](workspace/README.md)。`arena.exe` 用 Windows 的 Job Object 管进程，只在 Windows 上测过。

**看回放**：[replays/](replays/README.md) 里是从正式赛程分层随机抽的 117 局（每两位选手单挑各一局，四人局每人出场 12 次），用浏览器打开 [workspace/viewer.html](workspace/viewer.html)，把 `.txt` 拖进去。

**出视频**：视频里的对局画面都是 [renderer/](renderer/README.md) 用 Blender 从回放渲染的，装好 Blender 5.2、Python（numpy、scipy、scikit-image、pillow）和 ffmpeg 后，`python renderer/render_game.py replays/g428.txt -o g428.mp4` 一条命令出片。

## 目录

| 路径 | 内容 |
|---|---|
| [workspace/](workspace/README.md) | 赛时选手包原样（不含 exe）：README、RULES、`dart.hpp`、裁判源码 `arena.cpp`、`build.py`、`match.py`、`clock.py`、模板、四张公开地图、网页查看器、回放格式、示例回放 |
| [engine/](engine/README.md) | 三档陪练的源码和说明（比赛时只发了编译好的程序） |
| [build_engine.py](build_engine.py) | 编译裁判和陪练 |
| [prompt.md](prompt.md)、[prompts/](prompts/) | 开赛消息（两批）和叫回通知的原文 |
| [bots/](bots/README.md) | 十三位选手的收卷原样：`player.cpp`、`notes.md`、`收卷记录.json` |
| [results/](results/README.md) | 个人榜（`海选成绩.json`、`海选榜.md`）、32,904 局逐局结果、赛程 |
| [replays/](replays/README.md) | 117 局回放和挑选规则 |
| [tournament/](tournament/README.md) | 出榜程序 `rating.py`、`bt.py`（原样）、复现脚本 `score.py`、重打脚本 `rerun.py`、赛时保密的 6 张地图 |
| [auction/](auction/README.md) | 队长拍卖：规则程序、尽调材料、逐轮问答、结果、队长笔记；演习和重拍通知 |
| [renderer/](renderer/README.md) | 回放转视频的渲染器：代码、十张图的场景主题、体素小人、头像、出片命令 |
| [appendix/](appendix/README.md) | 不计成绩：GPT 两位的灰度版、六位对照选手、DeepSeek 和 Kimi 换工具前的版本、60 分钟前停下时的版本；灰度版海选、对照赛、全员大混战（21 位）榜和复现脚本 |
| [CONDITIONS.md](CONDITIONS.md) | 比赛条件：型号、工具、用时、叫回、中断、对局时的机器负载 |
| [MANIFEST.md](MANIFEST.md) | 这一集放了什么、没放什么、为什么 |
| [source_manifest.json](source_manifest.json) | 收录文件的 SHA-256 和比赛时原件的 SHA-256 |

立绘、表情差分在仓库顶层的 [art/v2/](../art/v2/README.md)，这一集的体素小人导出在 [characters/boomerang/](../characters/boomerang/README.md)。

## 和比赛时的差别

- 选手包（`workspace/`）、陪练源码、选手作品（`bots/`、`appendix/bots/`、`appendix/early/`）逐字节原样，哈希见 [source_manifest.json](source_manifest.json)。比赛时的选手包是一个压缩包（SHA-256 `74dfde0d…5d3c1dc3`），里面的 `arena.exe` 和三档陪练 exe 不入库，用 `build_engine.py` 从源码编译，和比赛时逐字节相同（w64devkit 的 GCC 16.2.0）。
- `workspace/arena.cpp` 里沙箱目录的备选路径和 `workspace/build.py` 里找编译器的备选路径是主办方电脑上的位置，原样保留；比赛和复现都不进沙箱（`--sandbox none`），只用 Job Object 管进程。
- `results/games.jsonl.gz` 是比赛时 `逐局结果.json` 的内容按每局一行、gzip 压缩；`results/schedule.json.gz` 是赛程，每局的地图路径改成了仓库里的相对路径。`海选成绩.json`、`海选榜.md` 原样。
- 比赛时的对局没有存回放。`replays/` 是做视频时用同样的种子、地图、座位和程序重打的；有的程序按时钟决定算多深，重打结果和比赛时不一定相同（117 局里 45 局名次相同）。
- 拍卖问答里主办方电脑上的赛事目录写成了 `{赛事目录}`，开赛消息里每位选手的工作目录写成了 `{WORKDIR}`。
- 渲染器的源码把主办方电脑上的绝对路径换成了相对仓库的默认值和环境变量，其余没改；从内部候选图里挑定稿地图的一次性脚本没有收录。
- 不收录：选手的原始会话记录和思考内容、选手工作目录里的其余文件（自对战回放、编译缓存、临时脚本，每人几十到上百 MB）、主办方的启动 / 计时 / 收卷 / 叫回 / 压缩 / 监视等运营脚本、拍卖时的原始输出流、配音和成片。详见 [MANIFEST.md](MANIFEST.md)。

## 视频

B 站：<B站链接>

## 声明和许可

这是一次具体比赛的记录。工具、接入渠道和每次做题的发挥都会影响结果，同一个模型重做一次名次也可能变（附录里灰度版和正式版的差别就是例子），不据此预设模型强弱。程序和笔记由模型生成，主办方没有修改；题目、引擎、地图、陪练和赛事规则由主办方提供。

沿用仓库 [MIT 许可](../LICENSE)。渲染器里的角色形象：角色形象设计 小叶子XIYZ（B 站）；美术和制作 喵了个猫了个咪了个猫，见 [art/v2/](../art/v2/README.md)。
