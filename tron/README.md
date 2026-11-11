[English](README_en.md)

# 第 7 集 · 区王争霸赛 · 第一场：光轮对战

第 7 集《区王争霸赛》有两场：第一场是本目录的光轮对战（Tron，题目里叫"区王争霸"），第二场是屎山优化（和第 5 集同一道仓储调度题，在 [`../warehouse/`](../warehouse/)）。两场合计的"区王"见文末。

光轮对战回到写 bot 对战：10 个模型拿到逐字相同的提示词和选手包，各在 60 分钟里写一个 C++ 光轮 bot，然后所有 bot 一起打 1 对 1、4 人局、12 人局三种局型。另有 2 位参考选手和 1 条主办方写的强档陪练线，单独出一张"强对手榜"。正式模式一共打了 5713 局（10-18 终场 4267 局 + 10-20 补打 1446 局），另并入两次快速预览的对局。

- **目录里有什么**：`workspace/` 选手包 · `bots/` 交卷代码 · `sparring/` 陪练源码 · `results/` 正式榜、强对手榜和逐局结果 · `replays/` 回放 · `tournament/` 终场赛事脚本和赛程 · `challenge.py` 一键挑战
- **复现榜单**：终场由主办方的总控脚本调度，仓库里的脚本不能单独重跑整场；赛程和逐局结果都已公开，可以按种子和座位重打任意一局，见 [`tournament/README.md`](tournament/README.md) 和 [`results/README.md`](results/README.md)。

## 规则速览（完整规则见 `workspace/RULES.md`，精确实现是 `workspace/tron.hpp`）

- 所有人在方格棋盘上**同时**走一步（上 / 右 / 下 / 左），走过的格子永久变成墙；撞上出界、障碍或任何人的轨迹就死，活得越久名次越好，同一回合死的并列。
- **抢格**：几个人同一回合走进同一个空格时，优先级最高的占下、其余的死。优先级 = (编号 − 回合号) mod N，小者优先，每回合轮换。
- 局型：1v1（21×21，点对称）、4 人局（25×25）、12 人局（41×41），障碍约 8%。同一个 bot 要打所有局型。
- 选手只写一个函数 `int decide(const tron::State& s)`，返回方向。引擎（含精确结算一回合的 `tron::step`）以头文件 `tron.hpp` 给出，没有给 flood fill 之类的策略函数。
- 思考预算是程序自己消耗的 CPU 时间：首回合 2000 毫秒，之后每回合 50 毫秒。超预算或返回不合法值时，这一回合沿上一回合的方向走，**不判负**。

## 赛制

| 阶段 | 内容 |
|---|---|
| 开赛 | 每个模型在只放了 `workspace.zip` 的空文件夹里开一个新会话，收到同一段提示词（`prompt.md`）。解压时开始计时：推荐 45 分钟内完成，最长 60 分钟强制收卷，不设最早交卷时间。只交 `submission/bot.cpp`，到点时它是什么就算什么。禁止联网、禁止子代理 |
| 正赛 | 9 位正式选手 10-17 凌晨依次开赛；第 10 位 Mercury 10-20 凌晨用同一份提示词和选手包补赛 |
| 终场 | 10-18 打 4267 局；10-20 只新打和 Mercury 有关的 1446 局，10-18 的局一局不动、全部并入，连同两次快速预览里符合条件的对局，两张榜整体重新拟合 |

**计分**：每个局型（1v1、3～4 人、12 人）各拟合一个 Bradley–Terry 实力值（差 400 分 ≈ 10:1），总分 = 1v1 × 50% + 3～4 人 × 30% + 12 人 × 20%。**锚点是豆包 = 0**，所有分数都是相对豆包的。区间是自助法 1000 次（按局型分层整块重抽样）的 95% 区间；名次是 95% 名次区间，写作"第 a～b 名"。两人分不分得开，看两人总分之差的 95% 区间是否含 0——这是两两之间的关系，不传递。

**赛后调整**：选手拿到的 `RULES.md` 写的是"greedy 基准 = 0、只有一张榜、3～4 人局含 3 人局"。比赛结束后改成：锚点改为豆包（只是平移，不影响名次）；greedy 只在多人局补座位、不计分、不上榜、不打 1v1；不打 3 人局（"3～4 人"一项只用 4 人局）；分正式榜和强对手榜两张。权重 50/30/20 没变。

## 选手

| 模型 | 短名 | 厂商 | 工具 | 接入（模型名） | 身份 |
|---|---|---|---|---|---|
| Mistral Large 4 | Mistral | Mistral AI | OpenCode | OpenRouter（`mistralai/mistral-large-4-0`） | 正式选手 |
| 美团 LongCat-2.0 | LongCat | 美团 | OpenCode | OpenRouter（`meituan/longcat-2.0`） | 正式选手 |
| 豆包 Doubao-Seed-Evolving | Doubao | 字节跳动 | OpenCode | 火山引擎 Coding Plan（`doubao-seed-evolving`） | 正式选手（锚点） |
| 阶跃 Step 5 preview | Step | 阶跃星辰 | OpenCode | OpenRouter（`stepfun/step-5-preview`） | 正式选手 |
| 盘古 openPangu-2.0-Pro | Pangu | 华为 | OpenCode | 华为云 MaaS（`openpangu-2.0-pro`） | 正式选手 |
| 腾讯混元 Hy4 preview | Hunyuan | 腾讯 | OpenCode | OpenRouter（`tencent/hy4-preview`） | 正式选手 |
| 通义千问 Qwen3.8-27B | Qwen27B | 阿里巴巴 | OpenCode | OpenRouter（`qwen/qwen3.8-27b`） | 正式选手 |
| Inception Mercury 3（扩散语言模型） | Mercury | Inception | OpenCode | OpenRouter（`inception/mercury-3`） | 正式选手（10-20 补赛） |
| 文心 ERNIE 5.1 | Wenxin | 百度 | OpenCode | 百度千帆（`ernie-5.1`） | 正式选手 |
| 小红书 dots3-note preview | Dots | 小红书 | OpenCode | dots 官方接口（`dots3-note-prev`） | 正式选手 |
| GPT-6 Astra（ultra 思考档 + fast 服务档） | AstraUF | OpenAI | Codex | OpenAI 官方订阅 | 参考选手，只进强对手榜 |
| DeepSeek V4.1 Flash | DSNew | DeepSeek | dsh 0.2.1-rc.2 标准模式，思考档 high | DeepSeek 官方接口 | 参考选手，只进强对手榜 |

- 10 位正式选手都用 OpenCode 的命令行（`opencode run`），思考档是工具默认值。OpenCode、Codex 的版本没有记录。
- **参考选手**：AstraUF 和 DSNew 不占正式榜名次，只在强对手榜上，用来看"面对强对手时"各 bot 的表现。DeepSeek 一共跑了三次，前两次作废，只计第三次（DSNew）；dsh 会在第 10、20、30 分钟自动往会话里插一条当前时间，别的工具没有这个功能。详见 `CONDITIONS.md`。
- **强档陪练线 sparring_hard**：主办方的子代理赛前写的搜索型 bot，开发时间远超 60 分钟，还专门对着彩排时的 bot 调过。它在强对手榜上参与拟合、不占名次。选手包里给了它和中档陪练 `sparring` 的可执行程序，供选手本地测试；源码在 `sparring/`。
- 每位选手的用时、中断、补时见 **`CONDITIONS.md`**。

## 正式榜（10 位正式选手）

| 名次 | 选手 | 总分 | 95% 区间 | 1v1 | 3～4 人 | 12 人 | 和谁分不开 |
|---|---|---:|---|---:|---:|---:|---|
| 第 1 名 | Mistral Large 4 | 102 | 63 ~ 147 | 363 | −191 | −113 | — |
| 第 2～3 名 | 美团 LongCat-2.0 | 10 | −24 ~ 50 | 261 | −271 | −198 | Doubao |
| 第 2～4 名 | 豆包 Doubao-Seed-Evolving（锚点） | 0 | — | 0 | 0 | 0 | LongCat、Step |
| 第 3～4 名 | 阶跃 Step 5 preview | −25 | −60 ~ 11 | 13 | −61 | −64 | Doubao |
| 第 5 名 | 盘古 openPangu-2.0-Pro | −208 | −242 ~ −175 | −170 | −199 | −316 | — |
| 第 6 名 | 腾讯混元 Hy4 preview | −240 | −280 ~ −203 | −352 | −258 | 66 | — |
| 第 7～9 名 | 通义千问 Qwen3.8-27B | −290 | −325 ~ −253 | −363 | −239 | −185 | Mercury、Wenxin |
| 第 7～9 名 | Inception Mercury 3 | −297 | −338 ~ −262 | −384 | −201 | −222 | Qwen27B、Wenxin |
| 第 7～9 名 | 文心 ERNIE 5.1 | −304 | −341 ~ −271 | −343 | −263 | −271 | Qwen27B、Mercury |
| 第 10 名 | 小红书 dots3-note preview | −440 | −480 ~ −403 | −510 | −335 | −424 | — |

- 1v1、3～4 人、12 人三列是各局型的实力值（同样相对豆包）。同一个 bot 在三种局型里的位置可以差很多，例如混元的 12 人局分项是正式榜唯一的正分项，豆包的 3～4 人分项高于其余 9 位。
- LongCat 和豆包分不开、豆包和阶跃分不开，但 LongCat 和阶跃分得开（差 34，区间 3 ~ 68），所以不是三人并列。Qwen27B、Mercury、文心三人两两都分不开。
- 所有程序都编译通过，没有崩溃、没有不合法输出。只有豆包有超时：正式榜 1084 局里 14 局各有 1 回合，单回合最多 51.6 毫秒，这一回合沿原方向走，不判负。
- 两两之差的矩阵、相邻名次之差、对局来源和每人覆盖度见 `results/RESULTS_正式榜.md`。

## 强对手榜（12 位占名次 + 强档陪练线）

| 名次 | 选手 | 总分 | 95% 区间 | 1v1 | 3～4 人 | 12 人 | 和谁分不开 |
|---|---|---:|---|---:|---:|---:|---|
| —（参考线） | sparring_hard（强档陪练线） | 324 | 290 ~ 363 | 487 | 179 | 135 | AstraUF |
| 第 1 名 | GPT-6 Astra（AstraUF，参考选手） | 313 | 279 ~ 352 | 418 | 239 | 162 | 陪练线 |
| 第 2～3 名 | DeepSeek V4.1 Flash（DSNew，参考选手） | 128 | 95 ~ 164 | 277 | 9 | −67 | Mistral |
| 第 2～3 名 | Mistral | 120 | 86 ~ 155 | 383 | −160 | −119 | DSNew |
| 第 4～6 名 | LongCat | 13 | −21 ~ 49 | 240 | −241 | −174 | Doubao、Step |
| 第 4～6 名 | Doubao（锚点） | 0 | — | 0 | 0 | 0 | LongCat、Step |
| 第 4～6 名 | Step | −9 | −40 ~ 25 | 20 | −38 | −37 | LongCat、Doubao |
| 第 7～8 名 | Pangu | −193 | −225 ~ −161 | −167 | −176 | −283 | Hunyuan |
| 第 7～8 名 | Hunyuan | −221 | −260 ~ −181 | −340 | −228 | 91 | Pangu |
| 第 9～11 名 | Qwen27B | −265 | −301 ~ −233 | −355 | −194 | −147 | Mercury、Wenxin |
| 第 9～11 名 | Mercury | −276 | −314 ~ −241 | −373 | −183 | −172 | Qwen27B、Wenxin |
| 第 9～11 名 | Wenxin | −276 | −312 ~ −244 | −332 | −212 | −233 | Qwen27B、Mercury |
| 第 12 名 | Dots | −408 | −448 ~ −373 | −493 | −294 | −365 | — |

- 两张榜是两次独立拟合：1v1 共用同一批对局，多人局各打各的，同一位选手两榜的分数差 3～32 分，**不能互相比较或相减**。10 位正式选手在两张榜上的先后顺序（点估计）相同。
- 盘古和混元在正式榜分得开（差 32，区间 4 ~ 65），在强对手榜分不开（差 27，区间 −4 ~ 58）。
- AstraUF 减陪练线是 −11（区间 −46 ~ 24），分不开。1v1 直接对陪练线（每人 48 局，Mercury 40 局）：AstraUF 21 胜、Mistral 17 胜 2 同死、DSNew 9 胜 2 同死、LongCat 8 胜、阶跃 2 胜 1 同死、混元 1 胜，其余 6 位 0 胜。见 `results/RESULTS_强对手榜.md` 第 4 节。

## 马上看 / 马上玩

**看回放**：用浏览器打开 `replays/` 里任意一个 `.html`，不需要联网。空格播放 / 暂停，左右方向键逐回合，`[` `]` 跳到上一个 / 下一个事件（出局、抢格），`V` 显示地盘，按 `H` 看全部快捷键。30 局回放是**随机抽的，不挑局**，清单和抽法见 `replays/README.md`。

**用你的 bot 挑战全部 AI**（需要 Python 3.10+ 和 g++；裁判用到 Windows 的 CPU 计时接口，只支持 Windows）：

```bash
python challenge.py 你的bot.cpp           # 和 10 位正式选手、2 位参考选手、强档陪练线一起打 82 局（1v1、4 人局、12 人局），几分钟
python challenge.py 你的bot.cpp --full    # 局数是 4 倍，结果更稳
```

不带参数时用 `workspace/submission/bot.cpp`，这是选手拿到的起步模板（能直走就直走，很弱）。最后按终场强对手榜各局型的实力值估出你的分项和加权总分，以及相当于强对手榜第几名。第一次运行会先用 `make_player_pack.py` 把两个陪练从源码编出来。

**自己开发时**，在 `workspace/` 里用本地工具，和比赛时选手用的完全一样（用法见 `workspace/README.md`）。先运行一次 `python make_player_pack.py` 把陪练编出来（比赛时选手包里直接给的是编译好的陪练）：

```bash
python make_player_pack.py                                   # 编译 sparring、sparring_hard 到 workspace/bots/
cd workspace
python arena.py check                                        # 检查编译器，2/3/4/12 人局各打一局
python arena.py run                                          # 估分：对 greedy 和中档陪练，按名次分算（只用来比较自己的版本）
python arena.py run --opp sparring_hard                      # 对手换成强档陪练
python arena.py run --opp ../bots/mistral-large-4.cpp,../bots/step-5-preview.cpp   # 对手可以是任何 .cpp
python arena.py play submission/bot.cpp ../bots/mistral-large-4.cpp --seed 5    # 打一局 1v1，存回放
```

Windows 上没有 `python` 命令时，把 `python` 换成 `.\run.cmd`。如果只把 w64devkit 放进了仓库的 `tools/`、没有加入 PATH，先在仓库根目录的 PowerShell 中运行 `$env:CXX = (Resolve-Path .\tools\w64devkit\bin\g++.exe).Path`（`challenge.py` 和 `make_player_pack.py` 会自己找 `tools/` 里的编译器）。

## 目录

| 路径 | 内容 |
|---|---|
| `workspace/` | 选手包：规则、引擎、本地对战工具和裁判、回放查看器、起步模板、greedy 和 random（差别见下） |
| `prompt.md` | 发给选手的消息原文：开赛提示词、续跑消息 |
| `bots/` | 10 位正式选手的封存代码（原样）；`bots/reference/` 是两位参考选手的，见 `bots/README.md` |
| `sparring/` | 两个陪练的源码：`sparring.cpp`（中档）、`sparring_hard.cpp`（强档陪练线） |
| `results/` | 两张榜的成绩表、逐局结果、局内统计、冻结清单，见 `results/README.md` |
| `replays/` | 30 局随机抽样的回放，见 `replays/README.md` |
| `tournament/` | 终场用的赛事脚本（计分、赛程、并入）和配置，以及两次终场的赛程表（含全部地图种子），存档备查，见 `tournament/README.md` |
| `challenge.py` | 一键挑战 |
| `make_player_pack.py`、`workspace_manifest.json` | 编译陪练、打一份和比赛时一样的选手包；比赛时选手包的文件清单和哈希 |
| `CONDITIONS.md` | 比赛条件与异常记录 |

## 用比赛时的条件让新模型参赛

```bash
python make_player_pack.py --zip       # 编译陪练，按比赛时的文件清单打出 out/workspace.zip
```

脚本逐个核对文件和 `workspace_manifest.json` 里记的比赛时选手包的 sha256（陪练的可执行文件是现编的，哈希不一定相同），按原来的目录结构打包。然后新建一个空文件夹，只放 `out/workspace.zip`，在里面给模型开一个新会话，发 `prompt.md` 里的开赛提示词原文。时间规则写在提示词里，包里的 `clock.py` 从解压时刻开始计时。交上来的 `submission/bot.cpp` 直接拿去跑 `challenge.py`。

## 和比赛时的差别

- **陪练**：比赛时选手包里只有陪练编译好的程序（`bots/sparring.exe/.dll`、`bots/sparring_hard.exe/.dll`），开源版放的是源码 `sparring/*.cpp`，用 `make_player_pack.py` 按比赛时的编译参数编出来。开源前核对过：用 GCC 16.2 编出的 `sparring_hard.exe` 和比赛时上场的那份逐字节相同；`sparring.exe` 只差源文件名符号和时间戳；两个陪练的快速模式版本对打 24 组对局，逐回合相同。
- 除此之外，`workspace/` 和发给选手的 `workspace.zip` 逐字节相同（`RULES.md` 里写的计分口径就是赛前的版本，赛后调整见上面"赛制"）；`bots/` 里的代码和主办方封存的版本逐字节相同（sha256 与 `results/清单.json` 一致）。
- **没有放进仓库的**：各模型的原始会话记录；选手交卷时用文字写的策略说明；终场全部对局的回放（只放了随机抽的 30 局，逐局结果在 `results/` 里）；主办方计时、封存、补时、启动选手的脚本和操作手册；赛后的几个实验（成长曲线、大图实验、稳定性重跑）的数据；2010 年人类选手 bot 的适配代码（见下）。

## 附加实验：和 2010 年人类选手的 bot 对照

2010 年初，滑铁卢大学计算机科学俱乐部在 Google 赞助下办过一场 Tron 光轮 AI 比赛（Google AI Challenge，最终榜 708 名，[比赛网站存档](https://web.archive.org/web/20111117222059/http://tron.aichallenge.org:80/index.php)）。赛后我们把当年 14 个能找到源码的人类选手 bot（当年第 1～507 名）接进本期引擎，和本期 1v1 选手对打（快速模式，2872 局），用 Bradley–Terry 放到同一把尺上。**这个实验不计入成绩，也没有 Mercury**（它是在 Mercury 补赛前做的）。

结论取决于抢格规则（2010 年把走进同一格算平局，当年的 bot 不知道本期的抢格优先级）：

| 本期 bot | 按本期规则，约相当于 2010 年 | 按 2010 年判法（抢格算平局） |
|---|---|---|
| AstraUF（参考选手） | 第 1 名一档 | 约第 15 名 |
| DSNew（参考选手） | 第 1～3 名一档 | 约第 40 名 |
| Mistral | 约第 4 名 | 约第 85 名 |
| LongCat | 约第 10 名 | 约第 90 名 |
| Step、Doubao、Pangu | 约第 80～120 名 | 约第 120～150 名 |
| Qwen27B、Wenxin、Hunyuan、Dots | 约第 150 名一档或更低 | 约第 150 名一档或更低 |

- 当年第 146～507 名的 5 个 bot 在这把尺上挤在一起，**第 150 名以后分不出先后**。
- 当年时限是墙上 1 秒，本期是 50 CPU 毫秒，按时间截止的搜索型 bot 会变弱；有的 bot 不一定是最终版，有一个被迫降低了搜索深度。每个 bot 约 200 局，名次是区间，表里只写了点估计。
- 两种判法的结论差一个数量级，真实情况在两者之间，不能读成"打败了当年冠军"。

**致谢**。这些程序大多没有开源许可，我们**没有把它们放进本仓库**，只列出处，特此致谢：Andy Sloane（a1k0n，第 1 名，[github.com/a1k0n/tronbot](https://github.com/a1k0n/tronbot)）、Krzysztof Duleba（kduleba，第 3 名，[作者主页存档](https://web.archive.org/web/2010/http://www.duleba.com/tron/)）、Brian Haskin（Janzert，第 5 名，[janzert.com/tron](https://janzert.com/tron/)）、ebrahim（第 7 名，[github.com/ebrahim/tronbot](https://github.com/ebrahim/tronbot)）、grogers（第 11 名，[github.com/grogers0/grogers-tron-ai](https://github.com/grogers0/grogers-tron-ai)）、Henrik Nordvik（henrikno，第 21 名，[github.com/henrikno/tronbot](https://github.com/henrikno/tronbot)）、Iouri Khramtsov（iouri_，第 24 名，[github.com/ikhramts/TronBot](https://github.com/ikhramts/TronBot)）、Maxime81（第 41 名，作者个人页的 archive.org 存档）、Daniel Hartmeier（dhartmei，第 80 名，[benzedrine.ch/tron](https://benzedrine.ch/tron/)）、stungeye（第 146 名，[github.com/stungeye/Tron-Bot](https://github.com/stungeye/Tron-Bot)）、zacharyp（第 153 名，[github.com/zacharyp/Tron-AI-bot](https://github.com/zacharyp/Tron-AI-bot)）、Nathan（ExperimentGarden，第 196 名，[gist 317060](https://gist.github.com/317060)）、jamiely（第 488 名，[github.com/jamiely/tron-bot-waterloo](https://github.com/jamiely/tron-bot-waterloo)）、Matthew Jones（matburt，第 507 名，[github.com/matburt/tronAI2010](https://github.com/matburt/tronAI2010)）。也感谢当年的组织者和论坛里公开分享思路的参赛者。

## 第 7 集两场合计：区王

第 7 集两场各按名次给 10～1 分（第 1 名 10 分……第 10 名 1 分；名次是区间的，取区间内各名次分的平均），两场相加。光轮对战用上面的正式榜，屎山优化的名次见 [`../warehouse/README.md`](../warehouse/README.md) 的第 7 集一节。

| 选手 | 光轮对战 | 屎山优化 | 合计 |
|---|---:|---:|---:|
| 阶跃 Step 5 preview | 7.5 | 9.5 | **17** |
| 腾讯混元 Hy4 preview | 5 | 9.5 | 14.5 |
| 豆包 Doubao-Seed-Evolving | 8 | 6 | 14 |
| Mistral Large 4 | 10 | 3.5 | 13.5 |
| 通义千问 Qwen3.8-27B | 3 | 7.5 | 10.5 |
| Inception Mercury 3 | 3 | 7.5 | 10.5 |
| 美团 LongCat-2.0 | 8.5 | 1 | 9.5 |
| 文心 ERNIE 5.1 | 3 | 5 | 8 |
| 盘古 openPangu-2.0-Pro | 6 | 2 | 8 |
| 小红书 dots3-note preview | 1 | 3.5 | 4.5 |

## 声明

- 每个模型只跑了一次，结论是"这一次、60 分钟、这套提示词和工具"写出来的 bot 的结果，不是模型能力的排名。赛后的稳定性重跑（快速模式、缩小赛程）里，同一个模型写两次，有的差约 300 分，和模型之间的差距同量级。
- 正式选手都用 OpenCode，但接入渠道不同（OpenRouter、火山引擎、华为云、百度千帆、dots 官方接口），参考选手用的是 Codex 和 dsh。工具、渠道、思考档都会影响成绩。
- 盘古、dots、豆包在比赛中因断网、服务卡死、额度用尽中断过，主办方按记录补了时间后在原会话续跑；Mercury 得到的一次补时事后查明不该给，但不影响封存的代码。详见 `CONDITIONS.md`。
- 计分口径（锚点、greedy 的角色、局型、两张榜）是比赛结束后定的，和选手拿到的 `RULES.md` 不同，见上面"赛制"。权重没变。
- 选手代码都是模型生成的，主办方没有改动。题目、规则、引擎、裁判、greedy、两个陪练由主办方编写（陪练是主办方的子代理写的）。
