English: [README_en.md](README_en.md)

# 示例投稿

这里的六个文件夹只用来演示投稿的格式，不参加社区榜，也不是谁的真实投稿。

| 文件夹 | 说明 |
|---|---|
| `ctf/chen-006__stand-still/` | bot 类投稿的样子：`bot.cpp` 加 `meta.json`。代码是第 3 集选手包里的模板，三个角色原地不动 |
| `tank/chen-006__starter/` | 坦克大战投稿的样子：`bot.cpp` 加 `meta.json`。第 6 集官方起步模板原样复制，只定义 `init` 和 `decide`，使用官方 `tank.hpp` 和 `tank_main.cpp`，不自带 `main` |
| `tron/chen-006__starter/` | 光轮对战投稿的样子：第 7 集官方起步模板原样复制，只实现 `decide`，使用官方 `tron.hpp` 和 `tron_main.cpp`；本地编译和评测需要 Windows |
| `soccer/chen-006__starter/` | 足球投稿的样子：第 8 集选手包里的球员模板原样复制成 `bot.cpp`，评测时两个座位都用它；本地编译和评测需要 Windows |
| `boomerang/chen-006__starter/` | 回旋镖大作战投稿的样子：第 11 集选手包里的 `template.cpp` 原样复制成 `bot.cpp`，`#include "dart.hpp"`、用 `DART_MAIN`；本地编译和评测需要 Windows |
| `warehouse/chen-006__kimi-k3-round1/` | 仓储调度投稿的样子：`src/` 加 `meta.json`。代码是 Kimi K3 第 1 轮交的原样 |

可以拿它们试一下自测脚本：

```bash
python community/check.py community/_example/ctf/chen-006__stand-still
python community/check.py community/_example/tank/chen-006__starter --skip-eval
python community/check.py community/_example/tank/chen-006__starter
python community/check.py community/_example/tron/chen-006__starter --skip-eval  # Windows
python community/check.py community/_example/tron/chen-006__starter              # Windows
python community/check.py community/_example/soccer/chen-006__starter             # Windows
python community/check.py community/_example/boomerang/chen-006__starter          # Windows
python community/check.py community/_example/warehouse/chen-006__kimi-k3-round1
```

足球默认正式计时打 120 局（15 个对手 × 4 个种子 × 换边），`--full` 为 480 局；固定个人榜的参照分，只拟合投稿者。

回旋镖大作战默认正式计时打 416 局（一对一 312、四人局 104），`--full` 为 1664 局；固定海选成绩里 13 位选手的两项实力值，只拟合投稿者，两项平均。

坦克大战默认正式评测 320 局，`--full` 为 1280 局；`--skip-eval` 只检查结构和编译。它固定官方终场赛对手的实力值，只拟合投稿者的分数。

光轮对战默认正式评测 82 局，`--full` 为 328 局；两档都固定强对手榜各局型的参照分，只拟合投稿者，再按 50/30/20 加权。陪练参与拟合但不占名次。`--skip-eval` 仍需 Windows 编译环境。

真正投稿时，文件夹要放在 `community/<集>/` 下，不是 `community/_example/<集>/` 下。规则见 [`../README.md`](../README.md)。
