[English](README_en.md)

# 第 12 集 · 回旋镖团队赛（OA 争霸·下）

第 11 集海选和队长拍卖之后，**Astra 队**和 **Fable 队**各五个模型，打一场 5v5 回旋镖团队赛。最终 **Astra 队 5.5 : 0.5 获胜**。

比赛分两段：

1. **团队开发 120 分钟**：队长只能用文字任务书指挥四名队员，各人改各人的程序，编译成功的程序自动发给队友联调；全场另有 3 次、每次不超过 30 行的亲手改。时间到，十个程序全部冻结。
2. **六回合布阵对决**：火山口（歼灭）、三座栈桥（占点）、双堡（刺王）各两回合。每回合两队同时交布阵和战术单，正式那套打 1,300～2,000 局定这一回合的分，六回合共 **10,600 局**。赛后再把每回合两队的正式和两套备选两两对打，得到 6 张 3×3"如果当时"矩阵（**54,000 局**）。

- **目录里有什么**：`workspace/` 团队赛选手包 · `engine/` 裁判与三档陪练源码 · `build_engine.py` 一键编译裁判与陪练 · `prompt.md` 与 `prompts/` 全部提示词与通知原文 · `dispatches/` 83 笔正式派活记录 · `hand_edits/` 3 笔生效亲手改 diff · `rounds/` 6 回合全部 36 套布阵与战术单 · `bots/` 10 位选手程序与笔记（含赛后复盘） · `results/` 6 回合正式成绩、逐局结果与 6 张 3×3 矩阵 · `tournament/` 一条命令重算与核对脚本 · `replays/` 28 局代表性与经典回放 · `purecut/` 纯享版 72 局分层抽局脚本与选局清单 · `analysis/` 脱敏比赛全景、分析稿、费用表、帕累托数据与三波控制变量实验 · `team-tools/` 队长派活与亲手改工具（选手端） · `rank-kit/` 1v1 排位赛本地选手包源文件 · `web-play/` 第 11～12 期网页可玩版前端源码 · `renderer/` 5v5 团队赛渲染器 · `production/` 第 12 集配乐署名与配音提示词/语气表
- **复现成绩与矩阵**：`cd boomerang-team && python tournament/score.py`：只要 Python 3.9+（无第三方依赖），约 1～2 秒从 `results/formal_games.jsonl.gz`（10,600 局）和 `results/matrix_games.jsonl.gz`（54,000 局）重算全部 6 回合正式比分（含种子配对标准误与两倍 SE 平局判定）及 6 张 3×3 共 54 格矩阵，逐项核对并输出“核对通过”。

## 规则速览

回旋镖基础物理与延迟机制与第 11 集完全一致（0.2 秒反应延迟、一击出局、开启友伤、挥砍挡镖、冲刺越坑、空手踢人、每步 CPU 10 ms 预算）；第 12 集新增 5v5 团队赛制、三种模式、每回合布阵与战术单以及 16 位队内无线电（`msg`）：

| 模式 | 地图 | 尺寸 | 胜负条件 | 每回合提交 |
|---|---|---|---|---|
| 歼灭（`M_ELIM`） | 火山口 `caldera` | 28×20 米 | 击倒对方全部 5 人；15 秒后由外向里逐波塌陷，最终在湖心岩台决胜 | 5 人开局站位 + 可选战术单 |
| 占点（`M_CONTROL`） | 三座栈桥 `piers` | 32×20 米 | 争夺 A / B / C 三座桥心据点（占满 1 秒易主，每秒每点 +1 分），60 秒比总分；出局 3 秒在己方复活点复活；不塌陷 | 5 人各自复活点 + 可选战术单 |
| 刺王（`M_KING`） | 双堡 `keeps` | 30×17 米 | 每队指定 1 名国王（3 条命，受击后 1.5 秒无敌，落坑直接出局）；对方国王出局即胜；城堡自外向内塌陷 | 5 人开局站位 + 国王 + 可选战术单 |

- 每回合两队同时盲交 3 套方案（`正式`、`备选1`、`备选2`）；正式方案按规定种子数左右换边开打（歼灭、刺王各 1,000 种子 / 2,000 局，占点 650 种子 / 1,300 局），胜率偏离 50% 小于两倍配对标准误判平（各 0.5 分），否则高者得 1 分。两套备选用于赛后 3×3“如果当时”矩阵对照。
- 完整规则见 [`workspace/RULES.md`](workspace/RULES.md)（第 11、14 节），规则引擎为单头文件 [`workspace/dart.hpp`](workspace/dart.hpp)。

## 两支战队（10 位选手）

| 队伍 | 选手 | 身份 | 海选实力值 / 拍卖价 | 席位名 | 作品目录 |
|---|---|---|---|---|---|
| Astra队 | GPT-6.2 Astra | 队长 | +306 / 队长 | `astra` | [`bots/gpt-6.2-astra/`](bots/gpt-6.2-astra/) |
| Astra队 | GPT-6.2 Sol | 队员 | +33 / 同门直入 | `sol` | [`bots/gpt-6.2-sol/`](bots/gpt-6.2-sol/) |
| Astra队 | Gemini 4 Hydrogen | 队员 | +30 / ¥400 | `hydrogen` | [`bots/gemini-4-hydrogen/`](bots/gemini-4-hydrogen/) |
| Astra队 | Grok 4.7 | 队员 | −1 / ¥400 | `grok` | [`bots/grok-4.7/`](bots/grok-4.7/) |
| Astra队 | Kimi K3.1 Pro | 队员 | −84 / ¥1 | `kimi` | [`bots/kimi-k3.1-pro/`](bots/kimi-k3.1-pro/) |
| Fable队 | Claude Fable 5.2 | 队长 | +132 / 队长 | `fable` | [`bots/claude-fable-5.2/`](bots/claude-fable-5.2/) |
| Fable队 | Claude Opus 5.5 | 队员 | +76 / 同门直入 | `opus` | [`bots/claude-opus-5.5/`](bots/claude-opus-5.5/) |
| Fable队 | Gemini 4 Carbon | 队员 | +51 / ¥599 | `carbon` | [`bots/gemini-4-carbon/`](bots/gemini-4-carbon/) |
| Fable队 | GLM 5.3 | 队员 | −9 / ¥200 | `glm` | [`bots/glm-5.3/`](bots/glm-5.3/) |
| Fable队 | DeepSeek-V4.1-Flash | 队员 | −30 / ¥201 | `deepseek` | [`bots/deepseek-v4.1-flash/`](bots/deepseek-v4.1-flash/) |

## 六回合正式成绩（Astra队 5.5 : 0.5 Fable队）

| 回合 | 地图与模式 | 局数 | Astra 胜/负/平 | Astra 胜率 | Fable 胜率 | 配对 SE | 本回合分 | 累计分 | 矩阵极差 |
|---|---|---:|---|---:|---:|---:|---|---|---:|
| R1 | `caldera` · 歼灭 | 2,000 | 1063 / 922 / 15 | 53.525% | 46.475% | ±1.093 pp | 1 : 0 | 1 : 0 | 31.40 pp |
| R2 | `caldera` · 歼灭 | 2,000 | 1571 / 429 / 0 | 78.550% | 21.450% | ±0.937 pp | 1 : 0 | 2 : 0 | 28.00 pp |
| R3 | `piers` · 占点 | 1,300 | 887 / 413 / 0 | 68.231% | 31.769% | ±1.266 pp | 1 : 0 | 3 : 0 | 24.40 pp |
| R4 | `piers` · 占点 | 1,300 | 668 / 631 / 1 | 51.423% | 48.577% | ±1.368 pp | 0.5 : 0.5 | 3.5 : 0.5 | **60.10 pp** |
| R5 | `keeps` · 刺王 | 2,000 | 1419 / 553 / 28 | 71.650% | 28.350% | ±0.986 pp | 1 : 0 | 4.5 : 0.5 | 19.95 pp |
| R6 | `keeps` · 刺王 | 2,000 | 1307 / 682 / 11 | 65.625% | 34.375% | ±1.080 pp | 1 : 0 | **5.5 : 0.5** | 19.25 pp |

完整 6 张 3×3 赛后矩阵（54,000 局）见 [`results/总成绩.md`](results/总成绩.md) 与 [`results/rounds/`](results/rounds/)。

## 马上看 / 马上玩

**1. 本地编译与 5v5 对战**（Windows；Python 3.9+、g++，推荐 w64devkit）：

> 开源版不带 `.exe` 编译产物，第一次使用请先运行 `python build_engine.py` 生成裁判 `workspace/arena.exe` 和三档陪练 `workspace/bots/{weak,mid,strong}.exe`。

```bash
cd boomerang-team
python build_engine.py
cd workspace
python match.py team weak --vs mid --map caldera --games 4
python match.py team ../bots/gpt-6.2-astra/player.cpp ../bots/gpt-6.2-sol/player.cpp ../bots/gemini-4-hydrogen/player.cpp ../bots/grok-4.7/player.cpp ../bots/kimi-k3.1-pro/player.cpp --名单 astra sol hydrogen grok kimi --vs ../bots/claude-fable-5.2/player.cpp ../bots/claude-opus-5.5/player.cpp ../bots/gemini-4-carbon/player.cpp ../bots/glm-5.3/player.cpp ../bots/deepseek-v4.1-flash/player.cpp --对方名单 fable opus carbon glm deepseek --map piers --布阵 ../rounds/R4/Astra队/正式 ../rounds/R4/Fable队/正式 --games 10
```

**2. 浏览器看回放**：打开 [`workspace/viewer.html`](workspace/viewer.html)，把 [`replays/`](replays/README.md) 中任意 `.txt` 拖进去即可播放（28 局，覆盖 6 回合代表局与全场经典局）。

**3. 渲染 5v5 视频**：装好 Blender 5.2、Python（numpy/scipy/scikit-image/pillow）与 ffmpeg 后，一条命令出片（详见 [`renderer/README.md`](renderer/README.md)）：

```bash
python renderer/render_game.py replays/R1_g366_astra_1v5.txt -o R1_g366.mp4
```

## 目录一览

| 路径 | 内容 |
|---|---|
| [`workspace/`](workspace/README.md) | 团队赛选手包（不含 exe；`README.md`、`match.py` 增加编译提示）：规则、`dart.hpp`、`build.py`、`match.py`、`布阵.py`、`clock.py`、模板、网页查看器、回放格式、三张团队地图与默认布阵 |
| [`engine/`](engine/README.md) | 裁判源码 `arena.cpp`、`json.hpp`、`接口说明.md` 与三档陪练源码 `sparring/` |
| [`build_engine.py`](build_engine.py) | 一键编译裁判 `workspace/arena.exe` 与三档陪练 `workspace/bots/*.exe` |
| [`prompt.md`](prompt.md)、[`prompts/`](prompts/README.md) | 队长/队员开赛、冻结、六回合、复盘提示词原文及主办方通知 |
| [`dispatches/`](dispatches/README.md) | 全部 83 笔正式派活记录（任务书原文与队员最终回复，不含原始会话与思考） |
| [`hand_edits/`](hand_edits/README.md) | 3 笔生效的队长亲手改 diff（Kimi 30 行、GLM 6 行、Astra 18 行） |
| [`rounds/`](rounds/README.md) | 6 回合两队全部 36 套生效方案（正式、备选 1、备选 2 的 `布阵.json` 与 `战术.txt`） |
| [`bots/`](bots/README.md) | 10 位选手的最终 `player.cpp`（Astra 与 GLM 另附亲手改前 `交卷.cpp`）及 `notes.md`（含赛后复盘） |
| [`results/`](results/README.md) | 正式成绩表、10,600 局正式逐局数据（`formal_games.jsonl.gz`）、54,000 局矩阵逐局数据（`matrix_games.jsonl.gz`） |
| [`tournament/`](tournament/README.md) | 一条命令重算与核对脚本 `score.py` |
| [`replays/`](replays/README.md) | 28 局代表性与精选经典回放 |
| [`purecut/`](purecut/README.md) | 纯享版 72 局分层随机抽局脚本 `select_pure.py` 与选局清单 `纯享版选局.md` |
| [`analysis/`](analysis/README.md) | 脱敏比赛全景、分析稿、硬数据总账、费用表、帕累托量化数据与图、双队源码策略解析、正赛回放寻宝、三波控制变量实验 |
| [`team-tools/`](team-tools/README.md) | 队长派活（`ask.cmd`/`team_cli.py`）与亲手改（`亲手改.cmd`/`team12.py`）选手端工具、模板与零依赖复现指南 |
| [`rank-kit/`](rank-kit/README.md) | 网站 1v1 排位赛本地选手包源文件（Windows/Linux 通用，源码上限 262,144 字符）与打包脚本 |
| [`web-play/`](web-play/README.md) | 第 11～12 期网页可玩版前端源码（WASM 裁判与 Bot 桥接、3D/2D 渲染、构建与场景导出脚本） |
| [`renderer/`](renderer/README.md) | 第 12 集 5v5 团队赛 Blender 渲染器 |
| [`production/`](production/) | 第 12 集配乐署名（[`配乐署名.md`](production/配乐署名.md)，全片 8 首含 CC BY 署名格式）与配音提示词/语气表（[`配音提示词.md`](production/配音提示词.md)、[`语气表.md`](production/语气表.md)） |
| [`CONDITIONS.md`](CONDITIONS.md) | 比赛条件：模型、工具、思考档、用时、中断和事故、赛场负载 |
| [`MANIFEST.md`](MANIFEST.md) | 收录范围：放了什么、不放什么 |
| [`source_manifest.json`](source_manifest.json) | 全部收录文件与比赛原件的 SHA-256 清单 |

## 和比赛时的差别

- 选手包（`workspace/`）不提交编译好的 `.exe`，改用 `build_engine.py` 从源码编译（实测三档陪练 `weak.exe`、`mid.exe`、`strong.exe` 用 w64devkit GCC 16.2.0 编译与比赛原件逐字节相同）。
- `workspace/README.md` 与 `workspace/match.py` 仅补充了“开源版先运行 `python build_engine.py`”的提示；`workspace/build.py` 的本地编译器兜底路径改为仓库 `tools/w64devkit`；`engine/arena.cpp` 移除了主办方本地沙箱回退路径（正式赛与本地对战默认均用 `--sandbox none`）。
- 提示词、派活记录与两份选手笔记（`gemini-4-hydrogen/notes.md`、`gemini-4-carbon/notes.md`）中指向主办方本机的绝对路径已替换为相对路径或 `{WORKDIR}` 占位，原件 SHA-256 记录于 [`source_manifest.json`](source_manifest.json)。
- 渲染器中主办方本机绝对路径已改为环境变量（`DART_BLENDER`、`DART_PYTHON`、`DART_WORK`、`DART_CACHE`、`DART_AVATARS`、`DART_FONT`、`DART_FFMPEG`、`DART_PORTRAITS`）和相对仓库的默认值，头像默认复用 [`../boomerang/renderer/assets/avatars/`](../boomerang/renderer/assets/avatars/NOTICE.md)。
- 比赛中的中断和事故（Kimi 工具开局不可用及退还亲手改、队员单次任务 30 分钟上限、Fable 队长触发 Claude 5 小时额度上限与补时 37.7 分钟、第 5、6 回合之间隔夜）见 [`CONDITIONS.md`](CONDITIONS.md)"中断和事故"。
- 不收录：选手原始会话日志与原始思考内容、纯主办方运营脚本与内部日志、排位赛服务端代码、密钥/账号/余额、解说稿/配音成品/字幕/音乐 MP3/剪辑工程（配乐署名与配音提示词见 `production/`）、网页大体积可再生 3D 场景包与 wasm 编译产物、全部 10,600 局原始回放及实验原始大回放。详见 [`MANIFEST.md`](MANIFEST.md)。

## 视频

B 站：<B站链接>

## 声明和许可

这是一次具体比赛的记录。模型、工具、队伍配置与临场发挥均会影响结果，不据此对模型能力作一般性定论。选手程序、笔记、派活任务书与布阵战术均由模型生成；引擎、地图、陪练与赛事规则由主办方提供。

整个仓库采用 [MIT 许可](../LICENSE)。角色形象署名：**角色形象设计参考：小叶子XIYZ；美术和制作：喵了个猫了个咪了个猫**。
