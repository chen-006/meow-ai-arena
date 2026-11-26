# 第 12 集收录清单：放 / 不放

按 [`docs/open-source-scope.md`](../docs/open-source-scope.md) 统一范围整理。逐个文件的 SHA-256 见 [`source_manifest.json`](source_manifest.json)。

## 放

| 类别 | 放了什么 | 在哪 |
|---|---|---|
| 游戏本体与选手包 | 5v5 团队赛规则 `RULES.md`、引擎头文件 `dart.hpp`、本地工具 `build.py`、`match.py`、`布阵.py`、`clock.py`、模板、网页查看器 `viewer.html`、回放格式、全部 3 张团队地图（`caldera`、`piers`、`keeps`）与默认布阵 | `workspace/`（不提交 `.exe`；`README.md`、`match.py` 增加先运行 `build_engine.py` 提示） |
| 引擎与陪练源码 | 裁判源码 `arena.cpp`、`json.hpp`、`接口说明.md`、三档陪练源码 `sparring/`、一键构建脚本 `build_engine.py`（实测三档陪练与比赛原件逐字节相同） | `engine/`、`build_engine.py` |
| 比赛材料 | 队长/队员开赛、冻结、六回合、商量前言、复盘、队长须知与比赛期间 3 条临时通知原文（模板开头的引用块是开源时补的说明，主办方操作步骤已删）；比赛条件说明（含中断和事故） | `prompt.md`、`prompts/`、`CONDITIONS.md`、`CONDITIONS_en.md` |
| 派活与亲手改 | 全部 83 笔正式派活记录（仅含任务书原文、队员最终回复、阶段/耗时/状态）；3 笔生效亲手改 diff（Kimi 30 行、GLM 6 行、Astra 18 行） | `dispatches/`、`hand_edits/` |
| 回合布阵与战术 | 全部 6 回合两队正式、备选 1、备选 2 共 36 套生效 `布阵.json` 与 `战术.txt`，以及两位队长每回合交卷时的最终回复 `提交说明.md`（共 12 份，已清本机路径） | `rounds/R1..R6/` |
| 选手作品 | 10 位选手最终 `player.cpp`（Astra 与 GLM 另留亲手改前 `交卷.cpp`）及 `notes.md`（含赛后复盘） | `bots/` |
| 裁判、计分与结果 | 6 回合正式成绩（`5.5 : 0.5`）、10,600 局精简正式逐局数据、54,000 局精简 6× 3×3 矩阵逐局数据、一条命令重算并核对脚本 `score.py` | `results/`、`tournament/` |
| 代表性回放 | 28 局回放（6 回合官方代表局 18 局 + 精选经典局 10 局）及索引 | `replays/` |
| 分析稿与探索实验 | 脱敏版比赛全景、硬数据总账、正赛回放寻宝、量化与帕累托（含 CSV 与散点图）、费用表、双队源码与策略解析、三波控制变量实验报告/补丁/统计摘要与核心统计脚本 | `analysis/`、`analysis/experiments/` |
| 纯享版分层抽局 | 纯享版 72 局分层随机抽样脚本 `select_pure.py`（直接读取 `results/formal_games.jsonl.gz` 复现 seed 263）、统计摘要与选局清单 `纯享版选局.md` | `purecut/` |
| 1v1 排位赛本地选手包 | 网站排位赛本地选手包源文件（`player.cpp`、`config.json`［源码上限 262,144 字符］、跨平台 `arena.cpp`、Linux 计时补丁版 `dart.hpp`、`play.py`、`viewer.html`、三档陪练与 6 张地图源码）及打包/补丁脚本 | `rank-kit/` |
| 队长派活与亲手改工具（选手端） | 队长与队员直接使用的派活与亲手改工具（`team_cli.py`、`team12.py`、`common.py`、`session_lookup.py`、`grok_cli.py`、`opencode_runtime.py`、`fake_member.py`、入口模板 `ask.cmd`/`ask_entry.py`/`亲手改.cmd`/`edit_entry.py`、队长须知与前言模板、脱敏配置样例，已清理内部路径和密钥） | `team-tools/` |
| 网页可玩版前端 | 第 11～12 期浏览器可玩版前端源码与依赖锁文件 `package-lock.json`（`src/games/boomerang/`、Three.js 3D/2D 渲染、WASM 裁判与 Asyncify Bot 桥接、构建脚本与 3D 场景导出管线、体素与精简元数据；不含网站其他期、部署配置及大体积可再生 `.wasm`/`.bin.br` 产物） | `web-play/` |
| 5v5 渲染器 | 第 12 集 5v5 渲染器源码（已剔除 `.bak*`、`__pycache__` 及内部脚本，清除所有内部绝对路径）、三张定稿地图、出片脚本与使用说明 | `renderer/` |
| 顶层清单更新 | 第 12 集新选 15 首 Mixkit 配乐清单（仅曲名、作者、编号、链接与授权，不含 MP3） | 顶层 `audio/music.md` |

## 不放

| 东西 | 原因 |
|---|---|
| 选手原始会话 JSONL、原始思考内容（含心声深挖稿）、工具调用轨迹 | 范围规定不公开 |
| 纯主办方运营脚本（`组队.py`、`封存.py`、`队长启动.py`、`赛前压缩.py`、`路径核查.py`、`回合.py` 等）、看门狗配置、演练场与运营日志 | 仅服务于办赛调度，不公开（选手侧调用的 `team-tools/` 已脱敏收录） |
| 网站排位赛服务端代码（控制面、评测 Worker、数据库与部署配置） | 暂不放，待线上跑稳后再评估公开 |
| 网页可玩版大体积可再生构建产物（13 张图 3D 场景包 `.bin.br` 约 31.7 MB、编译产物 `.wasm`）及网站其他期/部署配置 | 可由 `web-play/scripts/` 完整重新生成，不入库以控制仓库体积 |
| 密钥、API Token、账号余额与内部账单 | 敏感信息，绝不公开 |
| 解说稿、视频规划大纲、排雷清单、上屏清单、配音成品、字幕、剪辑工程、音乐 MP3 文件 | 范围规定不公开（Mixkit 许可禁止再分发音频文件） |
| 编译产物 `.exe`（裁判、陪练、选手冻结 exe）、`kit.zip` 压缩包与选手工作目录编译/自测缓存 | 仓库不提交二进制与重复压缩包；裁判与陪练由 `build_engine.py` 编译，`kit.zip` 由 `rank-kit/scripts/pack_kit.py` 生成 |
| 三波探索实验的跑批脚本（`prepare.py`、`run.py` 等） | 只收统计脚本和汇总数据，见 `analysis/experiments/README.md`"能不能复跑" |
| 全部 10,600 局原始大回放、三波探索实验原始回放及纯享版批量渲染串联脚本（`chain.py`、`post.py`、`mv12.py`、`shots.py`） | 体积过大或仅属本地成片制作流程；已提供 28 局代表与经典回放、64,600 局精简逐局数据及可复现的 `purecut/select_pure.py` |
| 渲染器 `work/` 缓存、样片 mp4、静帧成品、旧 `.bak*` 备份、出样片的一次性串联脚本 | 可由渲染器按命令重出，不入库 |

## 许可与署名

整个仓库采用 [MIT 许可](../LICENSE)。

角色形象署名：**角色形象设计参考：小叶子XIYZ；美术和制作：喵了个猫了个咪了个猫**。
