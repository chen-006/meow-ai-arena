English: [README_en.md](README_en.md)

# 社区投稿指南

这里收观众自己的作品：你写的 bot、你让 AI 写的 bot、你优化过的仓储调度代码。投稿走 Pull Request，合并后进 [社区榜](LEADERBOARD.md)。

你可以自己动手提交，也可以把 [`AGENT_PROMPT.md`](AGENT_PROMPT.md) 里的提示词整段交给你的 coding agent（Claude Code、Codex、Cursor 等），让它代你完成。

English: [README_en.md](README_en.md)

## 能投什么

| 集 | 目录 | 交什么 |
|---|---|---|
| 第 1 集 · 圈地 | `community/land/` | 一个 `bot.cpp` |
| 第 2 集 · 炸弹人 | `community/bomberman/` | 一个 `bot.cpp` |
| 第 3 集 · 夺旗 | `community/ctf/` | 一个 `bot.cpp` |
| 第 5 集 · 仓储调度 | `community/warehouse/` | 一个 `src/` 文件夹：优化后的仓储调度程序 |
| 第 6 集 · 坦克大战 | `community/tank/` | 一个 `bot.cpp`，只实现 `init` 和 `decide` |
| 第 7 集 · 光轮对战 | `community/tron/` | 一个 `bot.cpp`，只实现 `decide`；本地评测需要 Windows |

第 9 集 · 造桥不收 Pull Request：投稿走网站的第 1～6 关排行榜（[https://arena.meowllm.top/ep9/](https://arena.meowllm.top/ep9/)），见 [`bridge/README.md`](../bridge/README.md#投稿)；给 coding agent 的提示词在 [`bridge/AGENT_PROMPT.md`](../bridge/AGENT_PROMPT.md)。

下面这些都可以：

- 你自己写的 bot。
- 你让 AI 写的 bot。把那一集的 `prompt.md` 和 `workspace/` 交给模型，它拿到的就是节目里参赛模型拿到的任务。
- 在官方选手代码上改出来的版本，比如给 `land/bots/` 里某个 bot 调了参数、补了一种战术。
- 优化过的“屎山”：从 `warehouse/workspace/baseline/` 出发，改快并且保持输出逐字节不变。
- 修好了已知缺陷的版本：第 5 集有几份模型代码被找到了反例（见 `warehouse/results/round2/缺陷点归类.md`），可以交一份修好的。

在别人代码上改的，请在 `meta.json` 的 `based_on` 里写明来源。

## 不收什么

- 第 4 集（外交）。那一集是模型直接当玩家对话，没有可以提交的程序。
- 恶意代码，以及任何违反那一集规则的程序：读写文件、联网、启动别的程序、干扰对手或评测。
- 抄别人的投稿又不注明来源的。
- 没通过自动评测的：编译不过、仓储调度的输出和基线不一致。
- 带有密钥、令牌、个人信息的文件。

## 目录和文件

每份投稿是一个文件夹：

```text
community/<集>/<GitHub用户名>__<作品名>/
```

- `<集>` 是 `land`、`bomberman`、`ctf`、`warehouse`、`tank`、`tron` 之一。
- `<GitHub用户名>` 是你开 PR 用的账号，全部写成小写。
- `<作品名>` 自己取，最多 40 个字符。
- 两段都只能用小写字母、数字和连字符 `-`；连字符不能在开头和结尾，也不能连着用。两段之间是两个下划线。
- 例子：`community/ctf/chen-006__stand-still/`。

一个人可以投多份，每份一个文件夹、一个 PR。更新已有的投稿就改自己那个文件夹，再开一个 PR。

文件夹里只能有这些文件：

| 文件 | bot 类五集 | 仓储调度 | 说明 |
|---|---|---|---|
| `bot.cpp` | 必须有 | 不要 | 单个源文件，最大 256 KB |
| `src/` | 不要 | 必须有 | 只能有 `.cpp`、`.h`、`.hpp`、`.md` 文件，不能有子文件夹；单个文件最大 256 KB，合计最大 768 KB，最多 40 个文件 |
| `meta.json` | 必须有 | 必须有 | 作者和作品信息，见下 |
| `NOTES.md` | 可选 | 可选 | 思路说明，最大 64 KB |

所有文件都要是 UTF-8 文本。可以直接复制 [`_example/`](_example/) 里的示例来改。

### 代码要遵守的规则

和节目里参赛模型遵守的相同，以各集选手包里的说明为准。

| 集 | 编译命令 | 时间和资源 | 禁止 | 规则原文 |
|---|---|---|---|---|
| 圈地 | `g++ -O2 -std=c++20 bot.cpp` | 第 0 回合 2000 毫秒，之后每回合 50 毫秒；单线程；内存 512 MB 以内 | 访问网络、读写文件、在对局之间保存信息；只能用 C++ 标准库 | `land/workspace/TASK.md`、`RULES.md` |
| 炸弹人 | `g++ -std=c++20 -O2 bot.cpp` | 首回合 2000 毫秒，之后每回合 50 毫秒；同一个程序要支持 2–10 人 | 访问文件或网络、干扰对手 | `bomberman/workspace/README.md`、`RULES.md` |
| 夺旗 | `g++ -std=c++20 -O2 bot.cpp` | 第一回合 2 秒（含程序启动），之后每回合 50 毫秒；同一个程序要支持 2–15 方 | 读写文件、联网、启动其他程序；只能用 C++ 标准库 | `ctf/workspace/RULES.md` |
| 坦克大战 | `g++ -O2 -std=c++20 -I<官方tank/workspace> bot.cpp <官方tank/workspace>/tank_main.cpp` | 首回合（`init` + 首次 `decide`，含启动）2000 CPU 毫秒，之后每回合 50 CPU 毫秒；正式模式独立进程，线程 CPU 时间合计 | 读写文件、联网、启动其他程序、干扰裁判；只依赖官方 `tank.hpp` 和标准库 | `tank/workspace/README.md`、`RULES.md` |
| 光轮对战 | `g++ -O2 -std=c++20 -static -Wl,--stack,67108864 -I<官方tron/workspace> bot.cpp <官方tron/workspace>/tron_main.cpp` | 首回合（含启动）2000 CPU 毫秒，之后每回合 50 CPU 毫秒；正式模式独立进程，线程 CPU 时间合计 | 读写文件、联网、启动其他程序、干扰裁判；只依赖官方 `tron.hpp` 和标准库 | `tron/workspace/README.md`、`RULES.md` |
| 仓储调度 | `g++ -O2 -std=c++20 src/*.cpp` | 内存上限 1 GB；计时用进程的 CPU 总时间，多线程不会更快 | 读写文件、联网、在两次运行之间缓存信息；只能用 C++ 标准库；只读标准输入、只写标准输出 | `warehouse/workspace/TASK.md` |

Windows 上编译时会多加一个 `-static`。**Tron 的裁判和主循环只支持 Windows**，本地编译（包括 `--skip-eval`）和评测都需要 Windows + g++；自动评测用 GitHub 标准 `windows-2025` 虚拟机。其他集仍在 Linux 上用 g++ 编译，请不要依赖 Windows 专有的头文件。

坦克大战不能提交自己的 `main`、`tank.hpp` 或 `tank_main.cpp`。在 `bot.cpp` 中 `#include "tank.hpp"`，定义 `void init(const tank::State&, int)` 和 `tank::Orders decide(const tank::State&, int)`；自测会使用仓库里的可信头文件和主循环一起编译。评测沿用原生正式模式的 CPU 预算，不使用不判超时的快速模式，也不放宽 2000/50 毫秒的时限。Tank 允许开线程，预算按所有线程合计；源码扫描中的线程提示只是审查提醒，不代表禁止。正式比赛允许的 Windows 专用头文件不适用于 Linux 社区评测。

光轮对战不能提交自己的 `main`、`tron.hpp` 或 `tron_main.cpp`。在 `bot.cpp` 中 `#include "tron.hpp"`，定义 `int decide(const tron::State&)`，返回 `tron::U / R / D / L`。自测只使用可信选手包里的头文件和主循环，正式评测不改引擎、2000/50 CPU 毫秒预算或超时规则，也不用快速模式。允许开线程，CPU 时间按整个进程合计。超时或返回不合法值时，该回合沿上一回合方向走（第 0 回合为 `U`），不直接判负；摘要会记录超时、不合法输出和崩溃。

仓储调度的评测只编译 `src/` 这一层的全部 `.cpp`（和 `warehouse/workspace/tools/measure.py` 的做法一样），文件怎么拆随你。对任何合法输入，输出都要和 `warehouse/workspace/baseline/` 逐字节相同，`\r\n` 和 `\n` 视为相同。

自测脚本会扫描源码里的 `fopen`、`system`、`<thread>` 一类写法并给出提醒。提醒不会让评测不通过，但维护者会看。

### meta.json

```json
{
  "author": "小明",
  "github": "xiaoming",
  "name": "贴墙流",
  "description": "沿着墙圈地，尽量不走到开阔处。",
  "written_by": "human+ai",
  "model": "Opus 5.5",
  "tool": "Claude Code",
  "based_on": "land/bots/opus-5.5@claude-code#1",
  "license": "MIT"
}
```

| 字段 | 必填 | 说明 |
|---|---|---|
| `author` | 是 | 榜单上显示的名字，最多 40 个字符 |
| `github` | 是 | GitHub 用户名，要和目录名的前一段对得上（不分大小写） |
| `name` | 是 | 榜单上显示的作品名，可以用中文，最多 40 个字符 |
| `description` | 是 | 一句话说明，最多 120 个字符 |
| `written_by` | 是 | `human`（人写的）、`ai`（AI 写的）、`human+ai`（合写）三选一，请如实填 |
| `model` | `written_by` 不是 `human` 时必填 | 用了什么模型，比如 `Opus 5.5`、`GPT-6 Astra` |
| `tool` | 否 | 用了什么工具，比如 `Claude Code`、`Codex`、`Cursor` |
| `based_on` | 否 | 基于哪份代码改的，比如 `land/bots/opus-5.5@claude-code#1`、`warehouse 屎山原版`；从零写的留空 |
| `license` | 是 | 固定写 `"MIT"`，表示你同意按本仓库的 MIT 许可发布这份投稿 |

不用的可选字段可以留空字符串，也可以不写。不能有别的字段。

## 本地自测

需要 Python 3.10+ 和 g++（Windows 的装法见仓库根目录 README）。在仓库根目录运行：

```bash
python community/check.py community/<集>/<你的目录>
```

它做三件事：

1. 检查目录名、文件、`meta.json` 和大小限制。
2. 运行那一集的评测。
   - bot 类五集：调用那一集的 `challenge.py`。圈地约 380 局，炸弹人 330 局，夺旗约 180 局，和官方选手、基准打一轮小比赛。坦克大战则只让你的 bot 和 14 个下半场版本、出题方参照、基准这 16 个对手各打 20 局，共 320 局；`--full` 为每个对手 80 局，共 1280 局，均为正式模式。光轮对战默认 82 局（1v1 52 局、4 人局 24 局、12 人局 6 局），`--full` 为 328 局，均为正式模式；对手是 10 位正式选手、2 位参考选手和强档陪练线。首次运行还需编译官方选手。
   - 仓储调度：在你的电脑上把基线和你的代码各跑一遍，先比输出，再算每组的加速比和几何平均。
3. 输出成绩表，并把 `result.json` 和 `summary.md` 写到 `community/.runs/<集>/<你的目录>/`。这个目录不会进 git。

其他用法：

```bash
python community/check.py <目录> --skip-eval   # 只查结构和能不能编译，几秒钟
python community/check.py <目录> --full        # bot 类：局数更多；仓储调度：跑全部 8 组隐藏负载
```

仓储调度有两档，因为基线非常慢：

| 档 | 负载 | 基线用时（比赛电脑上） |
|---|---|---|
| 快速档（默认） | 5 组公开负载，加上 3 组隐藏负载 `hid_lowbattery`、`hid_mixed`、`hid_dynamic` | 约 4 到 5 分钟 |
| 完整档（`--full`） | 第 1 轮的全部 8 组隐藏负载 | 约 18 分钟 |

快速档选的 3 组是 8 组里基线跑得最快的。其中 `hid_dynamic` 和 `hid_mixed` 是第 1 轮里各家加速比最低的两组，GLM 5.3 那份代码也是在 `hid_dynamic` 上超时的。基线的用时和输出测过一次就缓存在 `community/.cache/` 里，第二次起只跑你的代码。慢于基线 1.2 倍加 5 秒的会被直接判超时。

## 怎么开 PR

1. 在 GitHub 上 fork [chen-006/meow-ai-arena](https://github.com/chen-006/meow-ai-arena)，把 fork 克隆到本地，新建一个分支。
2. 建好 `community/<集>/<GitHub用户名>__<作品名>/`，放进代码和 `meta.json`，运行 `python community/check.py <目录>`，直到显示通过。
3. 只提交这一个文件夹，推到你的 fork，向本仓库的 `main` 开 PR，把自测输出里从“## 社区投稿自测”开始的那段摘要贴进 PR 描述。

一个 PR 只放一份投稿，也不要顺手改别的文件。想改文档或脚本，请另开一个 PR。

## PR 之后会发生什么

1. **自动评测**。GitHub Actions 检查这个 PR 是不是只动了你自己的那个文件夹，然后在 GitHub 的虚拟机上跑一遍 `community/check.py`。评测脚本、官方对手和 Tank / Tron 的头文件、主循环都取自目标分支；PR 里只取投稿目录。因此 Tank / Tron 投稿需要目标分支已经合并了对应的投稿支持，先更新到支持它的最新 `main`，不要把评测脚本混进投稿 PR。结果在 PR 页面的 Checks 里，点进去能看到成绩表。第一次给本仓库提 PR 的账号，要等维护者点一下批准，评测才会开始。
2. **维护者看一眼**。主要看代码有没有违反规则的行为、`meta.json` 是否如实。
3. **合并**。
4. **进榜单**。合并后会再评测一次（仓储调度这次跑完整档），几十分钟后 [LEADERBOARD.md](LEADERBOARD.md) 自动更新。

## 成绩口径

- 社区榜是在 GitHub 提供的虚拟机上测的，bot 类每份投稿只打几百局。**它和节目里的正式成绩不能直接比**：正式赛是在比赛电脑上打几千到几万局。
- 圈地、炸弹人、夺旗的实力值来自“这份投稿 + 全部官方选手 + 基准”的一轮小比赛，基准记 0 分。社区投稿之间不直接交手。局数少，实力值上下浮动几十分是正常的。
- 坦克大战单独使用**固定终场赛参照**：固定 `tank/results/final/成绩.json` 中 16 个对手的实力值，只拟合投稿者自己的实力值（Bradley–Terry，基准 = 0，强 10 倍 = +400），不会重新拟合一轮小比赛。摘要中的对手分数是历史终场赛参照值，不是这次社区评测重新测出的成绩；相当名次只和 14 位正式选手比较，基准和出题方参照不排名。局数、地图和机器都与正式赛不同，这个估计不能当作正式赛名次。
- 光轮对战沿用 `tron/challenge.py` 的**固定终场强对手榜参照**：`tron/results/results.json` 中强对手榜的各局型实力值保持不变，分别只拟合投稿者的 Bradley–Terry 实力值，再按 1v1 50%、4 人局 30%、12 人局 20% 加权（豆包 = 0，强 10 倍 = +400）。`sparring_hard` 参与对战和拟合，但只作参考线、不占名次；参照名次与 12 位选手比较。这里的“3～4 人”分项实际只打 4 人局。选手包 `RULES.md` 保留赛前的 greedy = 0 等口径，社区挑战采用赛后强对手榜口径，不改写历史规则文件。分数、名次只是新地图上少量对局的估计，不是重跑正式赛。
- 圈地的官方评分脚本会把场上最强的选手换算成 100 分，不适合拿来给社区投稿排序。社区榜用的是单挑和混战两种赛制的 Bradley–Terry 实力值的平均（greedy 基准 = 0），官方脚本算出的 0–100 总分也会列在自测摘要里。
- 仓储调度的加速比是同一台机器上基线和投稿的 CPU 时间之比，输出有任何一组不对就没有成绩。不同投稿可能在不同的虚拟机上测，相近的数字不必当真。
- 榜单默认只重测有改动的投稿，原因写在 `.github/workflows/community-leaderboard.yml` 开头。

## 许可

投稿按本仓库的 [MIT 许可](../LICENSE) 发布，`meta.json` 里的 `"license": "MIT"` 就是你的同意。请只提交你有权这样发布的代码。基于官方选手代码或别人投稿改的，在 `based_on` 里写明来源。

## 安全提示

这里的投稿是陌生人写的 C++ 程序。维护者合并前会看代码，但不能保证没有疏漏。不要在存着重要数据的电脑上直接编译运行来历不明的投稿，想试的话放在虚拟机或容器里。同样的道理，自动评测只在 GitHub 的一次性虚拟机上运行投稿，运行投稿的任务使用只读令牌、不保存检出凭据、不给 secrets。合并后的每份投稿在独立评测任务中运行；有写权限的发布任务只校验结果并生成榜单，不编译或运行投稿。可信脚本和任务隔离不等于程序沙箱，绿勾也不能替代人工审查。

## 这个目录里的文件

| 路径 | 内容 |
|---|---|
| `land/`、`bomberman/`、`ctf/`、`warehouse/`、`tank/`、`tron/` | 各集的投稿 |
| `_example/` | 四份示例投稿，演示格式，不参加榜单 |
| `check.py` | 本地自测，自动评测用的也是它 |
| `ci_tools.py` | GitHub Actions 用的工具：检查 PR 的改动范围、生成榜单 |
| `AGENT_PROMPT.md` | 给 coding agent 的提交提示词（中文、英文各一版） |
| `LEADERBOARD.md` | 社区榜，自动生成 |
| `results/` | 每份投稿最近一次评测的结果，自动生成 |
