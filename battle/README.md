[English](README_en.md)

# 第 10 集 · 纸上谈兵

9 个模型各自在 90 分钟里排兵布阵：每一场拿一笔军费买兵、在布阵区里摆好，再写一个 C++ 指挥程序，开打以后谁都不能插手，方块小兵在刚体物理里互相砍、互相撞、被打飞、掉下悬崖。前五关打主办方的守军和 Boss，过关以后比谁花得少；最后和其他选手盲摆对打，一份布阵打 1v1，一份布阵打四人混战。一次交卷，没有随机数，共 945 场（闯关 45、1v1 144、混战 756）。

- **目录里有什么**：`workspace/` 选手包 · `engine/` 模拟器源码 · `bots/` 九位选手的布阵和指挥程序 · `results/` 成绩和逐局结果 · `replays/` 回放 · `tournament/` 计分和复现 · `renderer/` 回放转视频的渲染器
- **复现榜单**：`python tournament/score.py`：用比赛时的计分脚本从 945 场逐局结果重算总榜、闯关榜、对抗榜，逐项和 `results/` 核对，打印"核对通过"，几秒钟。从选手作品重新编译、重打全部 945 场：`python build_engine.py` 后运行 `python tournament/score.py --rerun`，8 线程几分钟。

## 规则速览

- **兵种** 9 种：新兵、盾卫、长枪兵、弓手、骑兵、大锤、炮仗兵、投石车、大块头，造价 10～350。布阵文件是一份 JSON：在布阵区的 0.5 米格点上摆兵，每个兵可以设"原地守"或"自由"（默认行为），再打一个标签给程序认人。指挥程序每 0.1 秒看一次战场（全部单位的位置、血量、朝向，道具和地形），给自己的兵下指令；所有场次共用一个程序，也可以不交。
- **物理**：模拟器基于 Jolt Physics 的刚体物理，确定性运行：同样的布阵和程序永远打出同样的结果。会被打飞、撞倒、掉下悬崖、被自己人的箭射中、被潮水淹掉。选手手上的 `war.exe` 和裁判用的是同一个程序。
- **五关**（全部公开，过关条件各不相同）：

| 关 | 名称 | 预算 | 时限 | 过关条件 | 主办方参考造价 |
|---:|---|---:|---:|---|---:|
| 1 | 草甸迎击 | 700 | 90 秒 | 全歼来敌（骑兵从草甸冲过来，阵前一片泥塘） | 335 |
| 2 | 断龙桥 | 600 | 150 秒 | 全歼来敌而且军旗不倒（桥边挂着火药桶，可以炸断） | 280 |
| 3 | 拔炮台 | 450 | 150 秒 | 全歼台地上的守军和三架投石车 | 410 |
| 4 | 一线天 | 400 | 150 秒 | 护着主帅全歼峡谷守军（坡上有雪球） | 370 |
| 5 | 镇山兽 | 500 | 120 秒 | 打倒 Boss 镇山兽 | 400 |

- **关卡分**：没过 0 分；过关的人里造价最低的 100 分，比他每贵一倍扣 25 分，过关最少 20 分。**闯关分** = 五关平均。主办方参考造价不参与算分。
- **1v1**：预算 1000，同一份 `duel.json` 打樱丘、河心洲两张图，每两人 4 场（两张图 × 换座位），胜 1 平 0.5 负 0。全歼对方即胜，时间到比山头上的兵。
- **四人混战**：预算 600，涨潮沙洲，第 20 秒开始涨潮，站在水里就出局。任取 4 人的每种组合各打 6 场；每场得分 = 名次分（3/2/1/0）+ 歼敌分（打掉的造价 ÷ 600，最多 3）。
- **总分** = 闯关 × 60% + 对抗 × 40%，对抗 = 1v1 × 60% + 混战 × 40%。
- 完整规则见 [RULES.md](workspace/RULES.md)，指挥程序接口见 [COMMANDER.md](workspace/COMMANDER.md)，模拟器实现以 [engine/src/](engine/src/) 为准。

## 选手

| 选手 | 工具 | 思考档 | 作品目录 | 席位名 |
|---|---|---|---|---|
| GPT-6.2 Sol | Codex 桌面版（主办人手动启动） | max | [`bots/gpt-6.2-sol/`](bots/gpt-6.2-sol/) | `sol` |
| Claude Fable 5.2 | Claude Code（交互式窗口，另一台机器上由第三方运行） | max | [`bots/claude-fable-5.2/`](bots/claude-fable-5.2/) | `fable` |
| Gemini 4 Carbon | OpenCode（另一台机器上代跑） | high | [`bots/gemini-4-carbon/`](bots/gemini-4-carbon/) | `gemini_carbon` |
| Gemini 4 Hydrogen | OpenCode（Google Vertex AI） | high | [`bots/gemini-4-hydrogen/`](bots/gemini-4-hydrogen/) | `gemini_hydrogen` |
| Qwen4-Max-Preview | Qoder（主办人手动启动） | xhigh | [`bots/qwen4-max-preview/`](bots/qwen4-max-preview/) | `qwen` |
| DeepSeek-V4.1-Flash | dsh | max | [`bots/deepseek-v4.1-flash/`](bots/deepseek-v4.1-flash/) | `deepseek` |
| Kimi K3.1 Pro | Kimi Code | max | [`bots/kimi-k3.1-pro/`](bots/kimi-k3.1-pro/) | `kimi` |
| Doubao Seed-Evolving | OpenCode（火山方舟） | high | [`bots/doubao-seed-evolving/`](bots/doubao-seed-evolving/) | `doubao` |
| 文心 ERNIE 5.5 Flash Preview | OpenCode（OpenRouter） | high | [`bots/ernie-5.5-flash-preview/`](bots/ernie-5.5-flash-preview/) | `ernie` |

席位名是比赛时的内部名字，逐局结果和回放文件名里用的是它。比赛中途主办方整机重启过一次，各席续接原会话、按实际损失的时间补时；Gemini 4 Carbon 因主办方接口限流跑不起来，由主办人的朋友在另一台机器上用同一份选手包和逐字相同的提示词代跑；文心是赛中加的第九席；Claude Fable 5.2 这一席采用的是赛后第三方在另一台机器上重跑的记录，首次运行原样存档在 [first-run/](first-run/claude-fable-5.2/README.md)，不计成绩。Gemini 4 Hydrogen 用的 `high` 不是这个型号的最高档（`xhigh`）。详见 [CONDITIONS.md](CONDITIONS.md)。

## 总榜

| 名次 | 选手 | 总分 | 闯关分 | 对抗分 | 1v1 分 | 混战分 | 过关数 |
|---:|---|---:|---:|---:|---:|---:|---:|
| 1 | GPT-6.2 Sol | 89.9 | 95.4 | 81.7 | 100.0 | 54.1 | 5 |
| 2 | Claude Fable 5.2 | 83.4 | 91.2 | 71.8 | 85.9 | 50.7 | 5 |
| 3 | Gemini 4 Carbon | 71.3 | 84.7 | 51.0 | 59.4 | 38.4 | 5 |
| 4 | Gemini 4 Hydrogen | 68.8 | 77.7 | 55.6 | 64.1 | 42.9 | 5 |
| 5 | DeepSeek-V4.1-Flash | 47.6 | 50.8 | 42.7 | 50.0 | 31.8 | 3 |
| 6 | Qwen4-Max-Preview | 45.5 | 52.5 | 35.0 | 35.9 | 33.5 | 3 |
| 7 | Kimi K3.1 Pro | 32.2 | 38.7 | 22.4 | 29.7 | 11.4 | 3 |
| 8 | 文心 ERNIE 5.5 Flash Preview | 31.6 | 40.5 | 18.4 | 21.9 | 13.2 | 3 |
| 9 | Doubao Seed-Evolving | 12.0 | 15.6 | 6.7 | 3.1 | 12.1 | 1 |

## 闯关

每格是关卡分，括号里是造价；"没过"后面是造价。

| 选手 | 第 1 关 | 第 2 关 | 第 3 关 | 第 4 关 | 第 5 关 |
|---|---|---|---|---|---|
| GPT-6.2 Sol | 92.0（250） | **100.0**（60） | 85.8（385） | **100.0**（295） | 99.3（250） |
| Claude Fable 5.2 | **100.0**（200） | 67.0（150） | **100.0**（260） | 89.0（400） | **100.0**（245） |
| Gemini 4 Carbon | 96.6（220） | 51.5（230） | 92.5（320） | 93.3（355） | 89.8（325） |
| Gemini 4 Hydrogen | 92.0（250） | 43.2（290） | 81.9（430） | 97.1（320） | 74.3（500） |
| Qwen4-Max-Preview | 78.8（360） | 94.4（70） | 没过（430） | 89.0（400） | 没过（500） |
| DeepSeek-V4.1-Flash | 82.5（325） | 81.6（100） | 没过（425） | 89.9（390） | 没过（475） |
| 文心 ERNIE 5.5 Flash Preview | 75.0（400） | 45.1（275） | 82.3（425） | 没过（395） | 没过（490） |
| Kimi K3.1 Pro | 76.4（385） | 22.1（520） | 没过（450） | 94.9（340） | 没过（500） |
| Doubao Seed-Evolving | 77.8（370） | 没过（600） | 没过（450） | 没过（400） | 没过（500） |

过关人数依次是 9、8、5、7、4。第 2 关全场最低 60（参考造价 280），第 1、3、4、5 关也都有人比参考造价更省。每关的结束原因、用时和"是最低的几倍"见 [闯关榜.md](results/闯关榜.md)。

## 对抗

1v1 每格是行对列 4 场的得分（满分 4）：

| | Sol | Fable | Hydrogen | Carbon | DeepSeek | Qwen | Kimi | 文心 | 豆包 |
|---|---|---|---|---|---|---|---|---|---|
| GPT-6.2 Sol | — | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 4 |
| Claude Fable 5.2 | 0 | — | 3.5 | 4 | 4 | 4 | 4 | 4 | 4 |
| Gemini 4 Hydrogen | 0 | 0.5 | — | 2 | 4 | 2 | 4 | 4 | 4 |
| Gemini 4 Carbon | 0 | 0 | 2 | — | 2 | 4 | 4 | 3 | 4 |
| DeepSeek-V4.1-Flash | 0 | 0 | 0 | 2 | — | 4 | 2 | 4 | 4 |
| Qwen4-Max-Preview | 0 | 0 | 2 | 0 | 0 | — | 1.5 | 4 | 4 |
| Kimi K3.1 Pro | 0 | 0 | 0 | 0 | 2 | 2.5 | — | 2 | 3 |
| 文心 ERNIE 5.5 Flash Preview | 0 | 0 | 0 | 1 | 0 | 0 | 2 | — | 4 |
| Doubao Seed-Evolving | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | — |

GPT-6.2 Sol 的 1v1 32 场全胜。四人混战（每人 336 场）的场均得分、平均名次、第 1 名次数见 [对抗榜.md](results/对抗榜.md)。

## 马上看 / 马上玩

**本地跑**（Windows；Python 3.9+，g++；编译模拟器另需 cmake 3.20+、ninja、git，w64devkit 里自带 cmake 和 ninja；不需要第三方 Python 包）：

```text
python build_engine.py                 编译模拟器到 workspace/war.exe（第一次会下载 Jolt Physics 源码）
cd workspace
python play.py map 2                   在终端里画第 2 关
python play.py level 2 ../bots/gpt-6.2-sol/level2.json --cmd ../bots/gpt-6.2-sol/commander.cpp
python play.py duel                    用 submission/duel.json 和陪练打 1v1
python play.py replay replays/level2.wrp --at 20    讲一场的经过，画第 20 秒的战场
```

`play.py` 的全部命令见 [workspace/README.md](workspace/README.md)。拿某位选手的布阵和程序打别人：`play.py duel --opp ../bots/<甲>/duel.json:../bots/<甲>/commander.cpp`。

**看回放**：[replays/](replays/README.md) 里是闯关全部 45 场和挑出来的 18 场对抗（挑选规则写在那里），压缩成 `.wrp.xz`，`python replays/unpack.py` 解压后用 `play.py replay` 看，或者用 [renderer/](renderer/README.md) 渲染成视频。不在仓库里的场次，`python tournament/score.py --rerun --replays` 可以全部重新生成。

**出视频**：视频里的画面都是 [renderer/](renderer/README.md) 用 Blender 从回放渲染出来的，装好 Blender 5.2、Python（numpy、pillow）和 ffmpeg 后，一条命令出片。

## 目录

| 路径 | 内容 |
|---|---|
| [workspace/](workspace/README.md) | 赛时选手包原样：README、RULES、COMMANDER、本地工具 `play.py`、`build.py`、`clock.py`、五关、三张对抗图、陪练、示例、起步版 `submission/` |
| [engine/](engine/README.md) | 模拟器源码（C++，基于 Jolt Physics）、接口说明、回放格式；`python build_engine.py` 编译 |
| [prompt.md](prompt.md) | 开赛消息和续接消息原文 |
| [bots/](bots/README.md) | 九位选手的收卷原样：`level1.json`～`level5.json`、`duel.json`、`melee.json`、`commander.cpp`、`notes.md` |
| [results/](results/README.md) | 总榜、闯关榜、对抗榜（.md 和 .json）；`eval/` 是评测时的布阵检查、赛程、程序运行情况和 945 场逐局结果 |
| [replays/](replays/README.md) | 63 场回放 |
| [tournament/](tournament/README.md) | 比赛时的计分脚本 `scoring.py`（原样）、计分配置、复现脚本 `score.py` |
| [renderer/](renderer/README.md) | 回放转视频的渲染器：代码、八张图的场景主题、兵种和道具模型、出片命令 |
| [CONDITIONS.md](CONDITIONS.md) | 比赛条件、各席用时、中断和补时；Claude Fable 5.2 这一席为什么采用第三方重跑 |
| [first-run/](first-run/claude-fable-5.2/README.md) | Claude Fable 5.2 的首次运行（存档，不计成绩）：作答原样和五关回放 |
| [source_manifest.json](source_manifest.json) | 收录文件的 SHA-256 和比赛时原件的 SHA-256 |

角色立绘、表情差分和各家 AI 的体素小人在仓库顶层的 [art/](../art/README.md) 和 [characters/](../characters/README.md)。

## 和比赛时的差别

- 选手包（`workspace/`）和选手作品（`bots/`）逐字节原样，哈希见 [source_manifest.json](source_manifest.json)。`workspace/build.py` 里找编译器的备选路径是主办方电脑上的位置，原样保留；在别的电脑上设环境变量 `CXX` 或把 g++ 放进 PATH 即可（`score.py`、`build_engine.py` 也会找仓库的 `tools/w64devkit/`）。
- 比赛时选手包带着编译好的 `war.exe`。仓库不提交 exe，改为提供源码和 `build_engine.py`：用 w64devkit 的 GCC 16.2.0 和 Jolt Physics 提交 `6a985659a781` 编译，得到的 exe 与比赛时**逐字节相同**（SHA-256 `f703bab3…`）。其他编译器不保证字节相同，可以用 `tournament/score.py --rerun` 核对结果。
- 引擎源码改了三行，都不影响编译结果：`host.hpp` 开头两行注释里提到节目另一集内部文件的说法删掉了；`CMakeLists.txt` 里 Jolt Physics 的默认路径从主办方电脑上的位置改成 `engine/third_party/JoltPhysics`。
- 选手的工作目录在开赛消息里写成 `{WORKDIR}`。
- `results/` 是 Claude Fable 5.2 这一席换成重跑记录以后，主办方重新评测、出榜的输出（见 [CONDITIONS.md](CONDITIONS.md#claude-fable-52-这一席采用第三方重跑)）。里面的 json（成绩表和 `eval/`）把主办方电脑上的绝对路径改成了相对赛事目录的路径（`kit/` 是选手包，`sealed/<席位>/submission/` 是收卷，`eval/` 是评测），编译器路径写成 `g++`，其余数值原样；`.md` 成绩表原样。
- 回放用 xz 压缩，解压后与评测时的回放逐字节相同（`replays/sha256.json`），所以回放末尾结果段里的地图文件路径仍是主办方电脑上的绝对路径。
- 不收录选手会话记录和思考内容、赛后采访、主办方的启动 / 计时 / 收卷 / 续接 / 代跑并入 / 接口监控等运营脚本、密钥和私人配置。`tournament/scoring.py` 是比赛时"评测""出榜"两个阶段的原样代码，文档字符串里提到的 `host.py` 是不公开的运营脚本，复现不需要它。

## 声明

这是一次具体比赛的记录。工具、接入渠道和采样都会影响结果，不据此预设模型强弱。布阵和指挥程序由模型生成，主办方没有修改；题目、模拟器、关卡、陪练和赛事规则由主办方提供。沿用仓库 [MIT 许可](../LICENSE)。
