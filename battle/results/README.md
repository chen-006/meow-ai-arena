[English](README_en.md)

# 成绩和逐局结果

全部是评测和出榜的输出，数值原样。Claude Fable 5.2 这一席换成第三方重跑的记录以后，主办方用比赛时的同一套流程把九份作品重新评测、出榜（945 场全部重打），这里是那次的输出；不涉及 Fable 的场次，结果除计时字段外和换之前相同。见 [CONDITIONS.md](../CONDITIONS.md#claude-fable-52-这一席采用第三方重跑)。

| 文件 | 内容 |
|---|---|
| [总榜.md](总榜.md)、[总榜.json](总榜.json) | 总分 = 闯关 × 60% + 对抗 × 40% |
| [闯关榜.md](闯关榜.md)、[闯关榜.json](闯关榜.json) | 五关的关卡分和造价、每关最低造价和主办方参考线、各关明细（结果、结束原因、用时） |
| [对抗榜.md](对抗榜.md)、[对抗榜.json](对抗榜.json) | 1v1 和四人混战的成绩、1v1 两两比分 |
| [eval/检查.json](eval/检查.json) | 每人七份布阵的检查结果（合法与否、造价）和指挥程序的编译结果 |
| [eval/对局索引.json](eval/对局索引.json) | 赛程：945 场，每场的地图、座位、用到的布阵和程序 |
| [eval/results/](eval/results/) | 945 场逐局结果，文件名就是场次名（`L<关>_<席位>`、`D<图>_<0 号座位>_<1 号座位>`、`M<组>_<坐法>`） |
| [eval/程序运行情况.json](eval/程序运行情况.json) | 每人的指挥程序在各场里有没有超时、崩溃（全部为 0） |
| [eval/编译器.json](eval/编译器.json) | 编译器版本和参数 |
| [eval/引擎错误场次.json](eval/引擎错误场次.json) | 引擎超时、没有胜负的场次（空） |

席位名（`sol`、`gemini_carbon` 等）和选手的对照见 [CONDITIONS.md](../CONDITIONS.md#选手与工具)。

**路径**：json 里原来是主办方电脑上的绝对路径，收录时改成了相对赛事目录的路径：`kit/` 是选手包（本仓库的 `workspace/`），`sealed/<席位>/submission/` 是收卷（本仓库的 `bots/<目录>/`），`eval/build/<席位>/commander.exe` 是编译出来的指挥程序，`eval/replays/<场次>.wrp` 是回放（仓库里只放了 63 场，见 [replays/](../replays/README.md)）。编译器路径写成 `g++`。`tournament/score.py` 复现时会在 `rerun/` 下临时搭出同样的结构。

**逐局结果的字段**（详见 [engine/接口说明.md](../engine/接口说明.md)）：`winner` 获胜的座位（-1 = 平局或闯关没过）、`reason` 结束原因、`passed` 闯关是否过关、`secs` 打了多少秒、`digest` 整场模拟的摘要（同样的输入一定得到同样的摘要）、`teams` 每方的开场和剩余兵力、造价、歼敌造价（混战另有名次分、歼敌分、得分）、`seats` 每个座位的布阵和指令统计、`commanders` 指挥程序的运行情况（`first_ms`、`avg_ms` 等 CPU 计时每次跑都会不同）。
