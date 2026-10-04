# meow-ai-arena · AI 算法对抗大赛

让一群 AI 模型拿到**一字不差的同一句提示词**，各自在约 60 分钟里写出一个游戏 bot，再把这些 bot 扔进同一个赛场打几千到几万局，看谁最强。

这里是视频系列"AI 算法对抗大赛"的全部比赛材料：题目、裁判、每个模型交上来的原样代码、逐局结果、评分脚本，以及可以直接上手的一键脚本。你可以看回放、复现正式赛，也可以**写一个自己的 bot，和这些 AI 一较高下**。

第 4 集换了玩法：不写 bot，7 个模型直接当玩家打七国《外交》，自己谈判、结盟、背刺。

| 集 | 游戏 | 人数 | 局数 | 冠军 | 目录 |
|---|---|---|---:|---|---|
| 第 1 集 | 圈地 | 1v1 单挑 + 4 人混战 | 30000 | Sonnet 5.5 | [`land/`](land/) |
| 第 2 集 | 炸弹人 | 2–10 人 | 8930 | GPT-6 Astra | [`bomberman/`](bomberman/) |
| 第 3 集 | 夺旗 | 2–15 人 | 32010 | GPT-6 Astra / GPT-6.1 Sol（并列） | [`ctf/`](ctf/) |
| 第 4 集 | 外交 | 7 人，模型直接当玩家 | 3 | Fable 5.1（三局总分；第 3 局单独获胜） | [`diplomacy/`](diplomacy/) |

参赛模型包括 Claude（Opus、Sonnet、Fable）、GPT（Astra、Sol）、DeepSeek、Qwen、Kimi、GLM、MiMo、豆包等，每集略有不同。完整排名见各集 README。

## 30 秒上手

**只想看比赛**：不需要装任何东西。用浏览器打开任意一集 `replays/` 文件夹里的 `.html` 文件，就能逐回合播放。第 1 集打开 `land/replays/index.html`；第 4 集的回放带着全部私信，打开 `diplomacy/replays/game3.html` 看最后一局。

**想用自己的 bot 挑战 AI**：

1. 装好 [Python 3.10+](https://www.python.org/downloads/) 和 C++ 编译器（g++）。Windows 推荐免安装的 [w64devkit](https://github.com/skeeto/w64devkit/releases)，解压后把 `w64devkit` 文件夹放进本仓库的 `tools/` 文件夹即可，不用改环境变量。
2. Windows 双击 **`start.cmd`**；macOS / Linux 运行 `sh start.sh`。
3. 在菜单里选"挑战 AI"，选一集，把你的 `.cpp` 拖进窗口，或者直接回车用模板。几分钟后得到你在所有 AI 中的排名。

也可以不用菜单，直接在命令行运行：

```bash
cd ctf
python challenge.py 你的bot.cpp
```

**想写 bot**：先读那一集的 `workspace/RULES.md`（规则与输入输出协议），再看 `workspace/` 里的模板，它已经写好了输入解析。`workspace/` 就是当时发给 AI 的选手包，里面的本地裁判和比赛时完全一样。各个 AI 的代码在 `bots/` 里，可以直接拿来当对手或参考。

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

`arena/` 是第 3 集起使用的通用对战平台（赛程、Bradley–Terry 评分）。`docs/` 里有评分方法和"版本演化"实验的说明。

## 关于公平性

- 每个模型只测一次，单次结果有偶然性。工具（agent harness）、思考档、接入渠道都会影响成绩，各集的具体条件都写在 `CONDITIONS.md` 里。第 3 集的附加实验显示，同一个模型只换工具，实力值能差 120 分以上。
- 评分口径：第 1 集是 0–100 归一化；第 2、3 集是"基准 = 0、不封顶"的 Bradley–Terry 实力值；第 4 集是《外交》的局分（单独获胜 100，和局按中心数平方分配），只有三局。**不同集的分数不能直接比较。**
- 选手代码均为模型生成，主办方未做修改。题目、规则和裁判由主办方编写。第 4 集里的私信、命令和笔记都是模型自己写的，原样保存。

## 许可

[MIT](LICENSE)。欢迎拿去复现、改编、出新题。第 4 集回放里的地图图形来自 jDip（GPL），经 `diplomacy` 包渲染，不适用 MIT 许可。

## 致谢

视频里的角色形象大多来自 B 站博主 **ZipZipPipe**（[主页](https://space.bilibili.com/4168597)），特此致谢。按其主页公告，鲸鱼娘形象是基于上善无形原创角色的二创（原作来源 [b23.tv/3dNz55h](https://b23.tv/3dNz55h)），以 [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/deed.zh-hans)（署名、非商业性使用、相同方式共享）授权。

角色形象、配音和剪辑都不在本仓库内，也不适用本仓库的 MIT 许可；如需使用角色形象，请遵守原作者的协议。
