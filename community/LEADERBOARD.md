# 社区榜

这个文件由 `community/ci_tools.py render` 自动生成，投稿合并后由 GitHub Actions 更新，请不要手改。怎么投稿见 [README.md](README.md)。

成绩是在 GitHub 提供的虚拟机上测的：bot 类每份投稿只和官方选手打一轮几百局的小比赛，仓储调度的加速比是同一台虚拟机上基线和投稿的 CPU 时间之比。不同投稿不一定在同一台虚拟机上测，局数也少，名次相近的不必当真；**这些数字和节目里的正式成绩不能直接比。**

## 第 1 集 · 圈地（`land/`）

按实力值排序（单挑、混战两种赛制的 Bradley–Terry 实力值取平均，greedy 基准 = 0，强 10 倍记 +400）。“小比赛名次”是这份投稿在它自己那一轮小比赛里排第几。

| 名次 | 作品 | 作者 | 谁写的 | 模型 / 工具 | 实力值 | 小比赛名次 | 评测日期 |
|---:|---|---|---|---|---:|---|---|
| 1 | [原点圈地](land/owo9891__origin-loop/) | OWO9891（[@owo9891](https://github.com/owo9891)） | AI 写 | deepseek-flash / Proma | 404 | 1 / 13 | 2026-10-24 |
| 2 | [墨痕喵](land/mohen2006__land-gpt-61-sol/) | mohen2006（[@mohen2006](https://github.com/mohen2006)） | AI 写 | gpt-6.1-sol / Codex | 337 | 1 / 13 | 2026-10-17 |
| 3 | [皇家圈地](land/owo9891__royal-loop/) | OWO9891（[@owo9891](https://github.com/owo9891)） | AI 写 | deepseek-flash / DeepSeek Harness | 319 | 2 / 13 | 2026-10-22 |
| 4 | [环路猎手](land/mohen2006__loop-hunter/) | mohen2006（[@mohen2006](https://github.com/mohen2006)） | AI 写 | Step-5-Preview / WorkBuddy | 307 | 3 / 13 | 2026-10-25 |
| 5 | [大方框](land/light-yuncai__big-loop/) | Light-YunCai（[@light-yuncai](https://github.com/light-yuncai)） | AI 写 | deepseek-flash / DeepSeek Harness | 156 | 6 / 13 | 2026-10-25 |
| 6 | [洪泛圈地](land/spicae__flood-claim/) | spicae（[@spicae](https://github.com/spicae)） | AI 写 | deepseek-V4-pro / WorkBuddy | -38 | 10 / 13 | 2026-10-20 |

官方选手参照：每份投稿评测时，官方选手和基准也在同一台虚拟机上打了同一轮小比赛。下表是它们在各次评测里实力值的中位数。

| 官方选手 | 实力值（中位数） | 出现在几次评测里 |
|---|---:|---:|
| sonnet-5@claude-code | 330 | 6 |
| gpt-6-astra@codex | 296 | 6 |
| anchor-sparring | 261 | 6 |
| opus-5.5@claude-code | 244 | 6 |
| gpt-6-sol@codex | 181 | 6 |
| mimo-2.6-pro@zcode | 139 | 6 |
| kimi-k3@workbuddy | 25 | 6 |
| anchor-greedy | 0 | 6 |
| glm-5.3@workbuddy | -22 | 6 |
| mystery-model@zcode | -117 | 6 |
| deepseek-4.1f@workbuddy | -128 | 6 |
| anchor-random | -336 | 6 |

## 第 2 集 · 炸弹人（`bomberman/`）

按实力值排序（Bradley–Terry，基准 = 0，强 10 倍记 +400）。“小比赛名次”是这份投稿在它自己那一轮小比赛里排第几。

| 名次 | 作品 | 作者 | 谁写的 | 模型 / 工具 | 实力值 | 小比赛名次 | 评测日期 |
|---:|---|---|---|---|---:|---|---|
| 1 | [墨痕喵](bomberman/mohen2006__bomberman-gpt-61-sol/) | mohen2006（[@mohen2006](https://github.com/mohen2006)） | AI 写 | gpt-6.1-sol / Codex | 484 | 1 / 11 | 2026-10-20 |
| 2 | [Opus](bomberman/abottomcoder__opus/) | ABottomCoder（[@abottomcoder](https://github.com/abottomcoder)） | AI 写 | Opus 5.5 / Claude Code | 367 | 2 / 11 | 2026-11-05 |
| 3 | [deepseek屎山v35](bomberman/dinson98__deepseek-shishan-v35/) | Dinson（[@dinson98](https://github.com/dinson98)） | 人和 AI 合写 | DeepSeek-V4.1-Flash / WorkBuddy | 312 | 2 / 11 | 2026-10-20 |

官方选手参照：每份投稿评测时，官方选手和基准也在同一台虚拟机上打了同一轮小比赛。下表是它们在各次评测里实力值的中位数。

| 官方选手 | 实力值（中位数） | 出现在几次评测里 |
|---|---:|---:|
| GPT-6 Astra | 383 | 3 |
| GPT-6 Sol | 296 | 3 |
| Opus 5.5 | 290 | 3 |
| DeepSeek V4.1 Flash | 241 | 3 |
| Qwen 3.8 Max | 218 | 3 |
| GLM 5.3 | 181 | 3 |
| Kimi K3 | 179 | 3 |
| 豆包 Seed 2.1 | 130 | 3 |
| MiMo V2.6 Pro | 42 | 3 |
| Base | 0 | 3 |

## 第 3 集 · 夺旗（`ctf/`）

按实力值排序（Bradley–Terry，基准 = 0，强 10 倍记 +400）。“小比赛名次”是这份投稿在它自己那一轮小比赛里排第几。

| 名次 | 作品 | 作者 | 谁写的 | 模型 / 工具 | 实力值 | 小比赛名次 | 评测日期 |
|---:|---|---|---|---|---:|---|---|
| 1 | [弈喵（MeowMind）](ctf/huixiong806__meowmind/) | orangebird（[@huixiong806](https://github.com/huixiong806)） | 人和 AI 合写 | GPT-6.1 Sol / Codex | 298 | 1 / 13 | 2026-10-13 |
| 2 | [Gemini 4 Argon](ctf/sugarzo__gemini-4-argon/) | sugarzo（[@sugarzo](https://github.com/sugarzo)） | AI 写 | Gemini 4 Argon / Cursor | 180 | 1 / 13 | 2026-10-17 |

官方选手参照：每份投稿评测时，官方选手和基准也在同一台虚拟机上打了同一轮小比赛。下表是它们在各次评测里实力值的中位数。

| 官方选手 | 实力值（中位数） | 出现在几次评测里 |
|---|---:|---:|
| GPT-6 Astra | 215 | 2 |
| GPT-6.1 Sol | 181 | 2 |
| Fable 5.1 | 174 | 2 |
| Sonnet 5.5 | 148 | 2 |
| Opus 5.5 | 136 | 2 |
| DeepSeek V4.1 Flash | 104 | 2 |
| MiMo V2.6 Pro | 13 | 2 |
| 基准 | 0 | 2 |
| Qwen 3.8 Max | -7 | 2 |
| Kimi K3 | -27 | 2 |
| GLM 5.3 | -62 | 2 |
| 豆包 Seed 2.1 Turbo | -186 | 2 |

## 第 5 集 · 仓储调度（`warehouse/`）

按几何平均加速比排序，只列全部负载输出都正确的投稿。

| 名次 | 作品 | 作者 | 谁写的 | 模型 / 工具 | 几何平均加速比 | 输出正确 | 负载 | 评测日期 |
|---:|---|---|---|---|---:|---:|---|---|
| 1 | [稳流 SteadyFlow](warehouse/genshinkleenahida__steadyflow/) | genshinkleenahida（[@genshinkleenahida](https://github.com/genshinkleenahida)） | AI 写 | GPT-6 / Codex | 2971.0 倍 | 8 / 8 | 8 组隐藏负载 | 2026-10-21 |
| 2 | [循迹仓流](warehouse/genshinkleenahida__traceflow/) | genshinkleenahida（[@genshinkleenahida](https://github.com/genshinkleenahida)） | AI 写 | GPT-6 Astra Pro / ChatGPT（GitHub 插件） | 1789.2 倍 | 8 / 8 | 8 组隐藏负载 | 2026-10-21 |
| 3 | [缓存派单 + 位图寻路](warehouse/dafeiyuzhi__warehouse-cache/) | 志（[@dafeiyuzhi](https://github.com/dafeiyuzhi)） | AI 写 | DeepSeek（Harness 会话，具体型号未记录） / DeepSeek Harness | 235.2 倍 | 8 / 8 | 8 组隐藏负载 | 2026-10-22 |

官方选手的第 1 轮成绩见 [`warehouse/results/第1轮_速度成绩.md`](../warehouse/results/第1轮_速度成绩.md)，那是在比赛电脑上测的，评测脚本不会在虚拟机上重测它们，所以这里不列参照。

## 第 6 集 · 坦克大战（`tank/`）

按投稿实力值排序：正式模式挑战 14 位 AI、出题方参照和基准，固定终场赛对手评分，只拟合投稿的 Bradley–Terry 实力值（基准 = 0，强 10 倍 = +400）。

“终场赛参照名次”只把投稿分数放进 14 位 AI 的固定评分中比较，不是重跑正式赛。

| 名次 | 作品 | 作者 | 谁写的 | 模型 / 工具 | 实力值 | 终场赛参照名次 | 评测日期 |
|---:|---|---|---|---|---:|---|---|
| 1 | [Overmatch（超越火力）](tank/genshinkleenahida__overmatch/) | genshinkleenahida（[@genshinkleenahida](https://github.com/genshinkleenahida)） | AI 写 | GPT-6.1 Sol / Codex | 887 | 1 / 15 | 2026-10-26 |
| 2 | [NNUE Vanguard·AVX2 调优版](tank/owo9891__nnue-avx2-tuned/) | DeepSeek-V4.1-Flash（[@owo9891](https://github.com/owo9891)） | AI 写 | DeepSeek-V4.1-Flash + Claude-Opus-5-5 + Kimi-K3 + GPT-6-Astra / DeepSeek Harness | 850 | 1 / 15 | 2026-11-02 |
| 3 | [MCTS Vanguard](tank/lite0812__mcts-vanguard/) | Lite0812（[@lite0812](https://github.com/lite0812)） | AI 写 | GPT-6.1 Sol / Codex | 818 | 1 / 15 | 2026-10-27 |
| 4 | [NNUE Vanguard](tank/lite0812__nnue-vanguard/) | Lite0812（[@lite0812](https://github.com/lite0812)） | AI 写 | GPT-6.1 Sol / Codex | 818 | 1 / 15 | 2026-10-27 |
| 5 | [value-based PUCT](tank/slzhta__value-based-puct/) | slzhta（[@slzhta](https://github.com/slzhta)） | 人和 AI 合写 | GPT-6 Astra / Codex | 765 | 1 / 15 | 2026-10-21 |
| 6 | [神经网络搜索 (自适应预算版)](tank/mbfcasper__neural-search/) | mbfcasper（[@mbfcasper](https://github.com/mbfcasper)） | AI 写 | GPT-6.1 Sol + Gemini 4 Argon + Gemini 3.8 Flash + Claude Opus 5.5 / Codex / Antigravity / Claude Code | 670 | 1 / 15 | 2026-10-20 |

固定参照来自 [`tank/results/final/成绩.md`](../tank/results/final/成绩.md)。社区对局使用新地图，在不同机器上运行，局数较少；参照分数不是本次在虚拟机上重新测出的官方成绩。

## 第 7 集 · 光轮对战（`tron/`）

按加权实力值排序：Windows 正式模式，固定终场强对手榜各局型评分，只拟合投稿（豆包 = 0，1v1 / 4人 / 12人权重 50 / 30 / 20%）。

13 个对手包含 10 位正式选手、2 位参考选手和 sparring_hard；陪练参与拟合但不占参照名次。

| 名次 | 作品 | 作者 | 谁写的 | 模型 / 工具 | 加权实力值 | 强对手榜参照名次 | 评测日期 |
|---:|---|---|---|---|---:|---|---|
| 1 | [深扫](tron/z11y11w__deepseek-v41-flash/) | z11y11w（[@z11y11w](https://github.com/z11y11w)） | AI 写 | DeepSeek-V4.1-Flash / DeepSeek Harness | 138 | 2 / 13 | 2026-10-25 |

固定参照来自 [`tron/results/RESULTS_强对手榜.md`](../tron/results/RESULTS_强对手榜.md)。默认 82 局（52 / 24 / 6），参照名次只与 12 位选手比较；机器、地图、局数不同，不能当作正式赛名次。
