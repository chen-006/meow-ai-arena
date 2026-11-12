[English](README_en.md)

# 回放

63 场正式赛的回放，是正式评测时 `war.exe batch` 存下的原件，用 xz 压缩成 `.wrp.xz`（解压后和评测时逐字节相同，哈希在 [sha256.json](sha256.json)）。Claude Fable 5.2 这一席换成第三方重跑的记录以后，主办方重新评测了全部 945 场，这里是那次评测的回放，按新的名次挑选；Fable 首次运行的五关回放在 [first-run/claude-fable-5.2/replays/](../first-run/claude-fable-5.2/README.md)。

## 挑了哪些

挑选规则是事先按名次定的，不看比赛内容：

- **闯关全部 45 场**：`L<关>_<席位>`，九位选手 × 五关（布阵全部合法，所以每人每关都有一场）。
- **1v1 16 场**：总榜相邻名次的 8 对（第 1 对第 2、第 2 对第 3……第 8 对第 9），樱丘（`D0`）和河心洲（`D1`）各一场，名次高的一方坐 0 号座位。文件名 `D<图>_<0 号座位>_<1 号座位>`。
- **四人混战 2 场**：总榜前 4 名一组（`M000_0`：sol、fable、gemini_carbon、gemini_hydrogen），第 6～9 名一组（`M124_0`：qwen、kimi、doubao、ernie），各取 6 种坐法里的第 0 种（`_0`：四人按赛程顺序坐 0～3 号座位，0 号是南边）。

每场是谁对谁、座位、结果，见 [`results/eval/对局索引.json`](../results/eval/对局索引.json) 和 `results/eval/results/<场次>.json`。席位名和选手的对照见 [CONDITIONS.md](../CONDITIONS.md#选手与工具)。

其余 882 场没有放进仓库（全部回放约 3 GB）。模拟器是确定性的，`python tournament/score.py --rerun --replays` 会重打全部 945 场并存下全部回放，内容和比赛时相同。

## 怎么看

```text
python replays/unpack.py                 全部解压成 .wrp；只要几场就写名字里的字，例如 unpack.py L2 D0_sol
cd workspace
python play.py replay ../replays/L2_sol.wrp --at 10,20         讲经过，画第 10、20 秒的战场
python play.py replay ../replays/D0_sol_fable.wrp --seat 1 --deaths   从 1 号座位看，列出每个兵的死因
```

要做成视频，用 [renderer/](../renderer/README.md)：`出片.cmd ..\replays\L2_sol.wrp -o L2_sol.mp4`（Windows），或 `python renderer/src/make_video.py replays/L2_sol.wrp -o L2_sol.mp4`。

回放文件格式见 [engine/回放格式.md](../engine/回放格式.md)。
