[English](README_en.md)

# 第 9 集 · 造桥大赛

9 个模型各自在 90 分钟里设计桥梁。开局一次性拿到全部 7 关：第 1～6 关是闯关，造一座桥让规定的车辆按时、完好地开到对岸，过关以后比造价；第 7 关是攻防，每人交一座决赛桥和一辆“捣蛋车”，所有人的桥和别人的捣蛋车两两对打。

桥是二维侧视的杆件结构，由一个确定性的 XPBD 物理内核模拟。选手手上的本地模拟器和裁判用的是**同一份程序**，可以无限次试；正式评分就用发给选手的公开场景，没有隐藏场景。

## 规则速览

- **设计文件**是一份 JSON：新节点坐标、杆件（路面、木杆、钢杆、钢索、弹簧、液压杆）、机关（压力板、光电门、开局、船笛四种传感器，延时规则，红绿灯）。车只能开在路面杆件上，其他杆件车会穿过去；杆件超过受拉 / 受压上限就断，长杆受压上限按屈曲系数 ÷ 长度² 下降。
- **过关**：本关全部场景里每辆车都按时完好到达。车过去以后桥塌了也算过。穿模、速度爆炸、NaN、刚性杆件形变超过 20% 等物理异常判失败。
- **关卡分**：过关才有分，= round(100 × 参考造价 ÷ 你的造价, 2)。和参考答案一样便宜得 100，更便宜就超过 100；没过、没交、超预算都是 0。
- **第 7 关**：决赛桥只能是静态桥（预算 26000）；捣蛋车用同一套材料搭（造价 ≤ 6000、质量 ≤ 2500 kg、长 ≤ 3 米），0.5 秒先出发，之后 4 辆标准车（轿车、面包车交替）在 12～15 秒出发。每人的桥对其他 8 人的捣蛋车各打 1 局，共 72 局。**正式计分**：标准车队全部按时完好到达 = 桥方**守住**；没有全部到达 = **攻破**，记给捣蛋车一方。攻防分 = 50 × 守住局数 ÷ 8 + 50 × 攻破局数 ÷ 8。
- **总分** = (第 1～6 关关卡分之和 + 第 7 关攻防分) ÷ 7，七关同等重要。闯关分 = 六关关卡分的平均。
- 完整规则见 [RULES.md](workspace/RULES.md) 和 [RULES_决赛.md](workspace/RULES_决赛.md)（英文参考译文 [RULES_en.md](workspace/RULES_en.md)、[RULES_final_en.md](workspace/RULES_final_en.md)），物理实现以 [bridge_sim.cpp](engine/bridge_sim.cpp) 和 [bridge.py](workspace/bridge.py) 为准。第 7 关的正式计分口径以本页和 [tournament/README.md](tournament/README.md) 为准。

| 关 | 名称 | 地形 | 预算 | 场景 | 参考造价 |
|---:|---|---|---:|---:|---:|
| 1 | 小水沟 | 15 米水沟 | 10000 | 3 | 3509.84 |
| 2 | 大峡谷 | 30 米深谷，超载面包车 | 26000 | 2 | 10616.73 |
| 3 | 早高峰 | 28 米河、中间礁石，双向车流同时上桥 | 18000 | 3 | 7967.26 |
| 4 | 过船 | 24 米河、中间航道，船过时航道里不能有桥 | 25000 | 2 | 12342.52 |
| 5 | 钱不够 | 24 米河加左岸旧码头坑，没有木杆 | 10000 | 2 | 8216.12 |
| 6 | 地狱关 | 36 米河、两块礁石、航道、卡车双向 | 20000 | 3 | 16709.27 |
| 7 | 攻防 | 28 米河；决赛桥 × 捣蛋车 | 26000 | 72 局 | — |

参考答案是主办方事先做的，见 [reference/](reference/)。

## 选手

| 选手 | 工具 | 配置思考档 |
|---|---|---|
| GPT-6.2 Sol | Codex 桌面版 | max |
| Claude Opus 5.5 | Claude Code | max |
| GPT-6 Astra | Codex CLI | max |
| Gemini 4 Argon | OpenCode（Google Vertex AI） | high（thinkingLevel） |
| Grok 4.7 | Grok Build | xhigh |
| Kimi K3.1 Pro | Kimi Code | max |
| 腾讯混元 Hy4 | OpenCode（OpenRouter，腾讯官方供应商） | xhigh |
| 豆包 Seed Evolving | OpenCode（火山方舟） | high |
| DeepSeek V4.1 Flash | dsh | max |

每位选手都用所在工具的最高思考档；不同工具的同名档位不能直接等同。工具和接入渠道不同，不能把本场结果当作模型能力排名。详见 [CONDITIONS.md](CONDITIONS.md)。

## 总榜

| 名次 | 选手 | 总分 | 闯关分 | 攻防分 | 桥守住 | 车攻破 |
|---:|---|---:|---:|---:|---:|---:|
| 1 | GPT-6.2 Sol | 102.67 | 106.24 | 81.25 | 8/8 | 5/8 |
| 2 | Claude Opus 5.5 | 79.02 | 79.69 | 75.00 | 8/8 | 4/8 |
| 3 | GPT-6 Astra | 73.48 | 71.14 | 87.50 | 8/8 | 6/8 |
| 4 | Gemini 4 Argon | 45.31 | 42.45 | 62.50 | 7/8 | 3/8 |
| 5 | Grok 4.7 | 35.62 | 37.39 | 25.00 | 4/8 | 0/8 |
| 6 | Kimi K3.1 Pro | 32.47 | 32.68 | 31.25 | 5/8 | 0/8 |
| 7 | 腾讯混元 Hy4 | 31.45 | 30.44 | 37.50 | 6/8 | 0/8 |
| 8 | 豆包 Seed Evolving | 30.96 | 31.95 | 25.00 | 4/8 | 0/8 |
| 9 | DeepSeek V4.1 Flash | 27.93 | 28.41 | 25.00 | 4/8 | 0/8 |

“桥守住”是自己的决赛桥挡住了几辆别人的捣蛋车，“车攻破”是自己的捣蛋车攻破了几座别人的桥。

## 逐关成绩

每格是关卡分，括号里是造价；“未过”是交了合法设计但有场景没通过，“超预算”是设计不合法。

| 选手 | 第 1 关 | 第 2 关 | 第 3 关 | 第 4 关 | 第 5 关 | 第 6 关 |
|---|---|---|---|---|---|---|
| GPT-6.2 Sol | 98.54（3562） | **137.85**（7702） | **103.27**（7715） | **105.38**（11712） | **95.31**（8621） | **97.07**（17213） |
| Claude Opus 5.5 | **99.38**（3532） | 114.93（9237） | 101.99（7812） | 70.19（17585） | 0（超预算 13472） | 91.64（18234） |
| GPT-6 Astra | 91.58（3832） | 95.99（11061） | 88.45（9008） | 65.94（18718） | 0（超预算 10199） | 84.90（19682） |
| Gemini 4 Argon | 93.46（3756） | 78.02（13608） | 83.22（9573） | 0（未过 0/2） | 0（未过 0/2） | 0（未过 0/3） |
| Grok 4.7 | 91.25（3846） | 68.72（15450） | 64.35（12382） | 0（未过 0/2） | 0（未交） | 0（未交） |
| Kimi K3.1 Pro | 57.87（6065） | 57.90（18338） | 80.30（9922） | 0（未过 0/2） | 0（未过 0/2） | 0（未过 0/3） |
| 腾讯混元 Hy4 | 85.33（4113） | 49.02（21656） | 48.27（16505） | 0（未过 0/2） | 0（未过 0/2） | 0（超预算 34497） |
| 豆包 Seed Evolving | 90.40（3882） | 52.75（20128） | 48.56（16406） | 0（未过 0/2） | 0（未过 0/2） | 0（未过 0/3） |
| DeepSeek V4.1 Flash | 67.01（5238） | 52.75（20128） | 50.72（15707） | 0（未过 0/2） | 0（未过 0/2） | 0（未过 0/3） |

第 4～6 关只有 GPT-6.2 Sol、Claude Opus 5.5、GPT-6 Astra 过了关；第 5 关只有 GPT-6.2 Sol 过了。含造价、场景通过数和文件哈希的完整表见 [results/](results/README.md)。

## 第 7 关：72 局

行是桥方，列是捣蛋车方。✕ = 攻破（标准车队没有全部到达），· = 守住。

| 桥 \ 捣蛋车 | Sol | Opus | Astra | Gemini | Grok | Kimi | 混元 | 豆包 | DeepSeek |
|---|---|---|---|---|---|---|---|---|---|
| GPT-6.2 Sol | — | · | · | · | · | · | · | · | · |
| Claude Opus 5.5 | · | — | · | · | · | · | · | · | · |
| GPT-6 Astra | · | · | — | · | · | · | · | · | · |
| Gemini 4 Argon | · | · | ✕ | — | · | · | · | · | · |
| Grok 4.7 | ✕ | ✕ | ✕ | ✕ | — | · | · | · | · |
| Kimi K3.1 Pro | ✕ | ✕ | ✕ | · | · | — | · | · | · |
| 腾讯混元 Hy4 | ✕ | · | ✕ | · | · | · | — | · | · |
| 豆包 Seed Evolving | ✕ | ✕ | ✕ | ✕ | · | · | · | — | · |
| DeepSeek V4.1 Flash | ✕ | ✕ | ✕ | ✕ | · | · | · | · | — |

72 局里 18 局攻破，都没有出现物理异常。逐局每辆车的到达 / 失败状态和时刻见 [duels.json](results/duels.json)。

## 马上看 / 马上玩

**看回放**：双击 [replays/](replays/README.md) 里的 `.html`，不用联网。每关关卡分最高的设计各一份，第 7 关按固定种子随机抽 3 局。

**网页版**：[https://arena.meowllm.top/ep9/play.html](https://arena.meowllm.top/ep9/play.html) 是浏览器里的造桥模拟器（第 9 集页面 [https://arena.meowllm.top/ep9/](https://arena.meowllm.top/ep9/)），可以载入九位选手和参考答案的设计、自己搭桥试车、看第 7 关任意一局。

**本地试车**（Python 3.9+、g++；不需要第三方 Python 包）：

```text
python build_engine.py
cd workspace
python bridge.py check 1 ../designs/opus/level1.json
python bridge.py run 2 ../designs/sol62/level2.json
python bridge.py run 6 ../designs/sol62/level6.json levels/6/sample1.json --html replay.html
python bridge.py duel ../designs/grok/final_bridge.json ../designs/gemini/saboteur.json --html duel.html
```

Windows 的编译器可以放进仓库的 `tools/w64devkit/`（同其他集），或加入 PATH，或设置 `CXX`。`bridge.py` 每次 run / duel 都会在**当前目录**的 `archive/` 下自动存档（比赛时用来记录过程）；加 `--no-archive` 不建存档目录，再设环境变量 `BRIDGE_NO_ARCHIVE=1` 连模拟日志也不写。

**复现正式成绩**：

```text
python build_engine.py
python tournament/score.py
```

重算第 1～6 关的 54 份设计和 6 份参考答案、第 7 关 72 局，逐项和 [results/](results/README.md) 核对，最后打印“核对通过”。8 核机器上几秒到一分钟。说明见 [tournament/README.md](tournament/README.md)。

## 投稿

第 9 集的投稿**走网站，不走 Pull Request**：网站第 9 集页面 [https://arena.meowllm.top/ep9/](https://arena.meowllm.top/ep9/) 有第 1～6 关的排行榜，按造价从低到高排。在[模拟器](https://arena.meowllm.top/ep9/play.html)里搭桥（或导入设计 JSON），试车全部公开场景通过后就能上传。

设计 JSON 的格式和比赛完全相同，可以先用本仓库的 `bridge.py run` 在本地跑通，再到网站导入、上传。想让 coding agent 代劳，把 [AGENT_PROMPT.md](AGENT_PROMPT.md) 里的提示词交给它；那里也写了网站的投稿接口。第 7 关没有排行榜。

## 目录

| 路径 | 内容 |
|---|---|
| [workspace/](workspace/README.md) | 赛时选手包原样：规则、模拟器前端 `bridge.py`、捣蛋车与攻防 `saboteur.py`、回放查看器、7 关关卡与场景 |
| [engine/](engine/bridge_sim.cpp) | 物理内核源码；`python build_engine.py` 编译到 `workspace/bridge_sim.exe` |
| [prompt.md](prompt.md) | 发给选手的开赛消息 |
| [designs/](designs/README.md) | 九位选手的最终设计：`level1.json`～`level6.json`、`final_bridge.json`、`saboteur.json` |
| [reference/](reference/) | 第 1～6 关参考答案（关卡分 100 的基准） |
| [results/](results/README.md) | 总榜、逐关成绩、72 局矩阵；`scores.json`、`duels.json` |
| [replays/](replays/README.md) | 9 份回放网页 |
| [tournament/](tournament/README.md) | 复现正式成绩、生成回放 |
| [AGENT_PROMPT.md](AGENT_PROMPT.md) | 给 coding agent 的投稿提示词（走网站） |
| [CONDITIONS.md](CONDITIONS.md) | 比赛条件 |
| [source_manifest.json](source_manifest.json) | 收录文件的 SHA-256 |

## 和比赛时的差别

- 选手包（`workspace/` 里的规则、`bridge.py`、`saboteur.py`、`viewer.html`、`levels/`）和选手设计逐字节原样，哈希见 [来源清单](source_manifest.json)。英文规则是另加的参考译文。
- 比赛时选手包带着编译好的 `bridge_sim.exe`。仓库不提交 exe，改为提供源码和 `build_engine.py`；用 w64devkit 的 GCC 16.2.0 按原参数（`-O2 -std=c++17 -static -municode`）编译，得到的 exe 与比赛时**逐字节相同**。其他编译器不保证字节相同，可用 `tournament/score.py` 核对结果。
- 开赛消息里的选手工作目录换成了 `{选手目录}`。
- `results/` 由 `tournament/score.py` 重算写出，数值与比赛时的最终成绩逐项相同；回放是赛后用同一引擎重跑生成的（引擎确定，与评分时一致），第 7 关回放里的结论按正式计分口径写。
- 不收录选手会话记录和思考内容、主办方的启动 / 计时 / 收卷 / 进程管理脚本、密钥和私人配置。

## 声明

这是一次具体比赛的记录。工具、接入渠道和采样都会影响结果，不据此预设模型强弱。设计文件由模型生成；题目、物理引擎、关卡、参考答案和赛事规则由主办方提供。沿用仓库 [MIT 许可](../LICENSE)。
