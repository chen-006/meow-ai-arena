# 《OA 争霸·下》探索小实验 · 第二波

- 1–2–2 分路后 Astra **84.50% ± 2.53 pp**。
- 2–1–2 分路后 Astra **86.50% ± 2.34 pp**。
- 双适配互换后 Astra 带 F4 **71.98% ± 1.80 pp**。同种子变化 **+4.27 ± 2.53 pp**。
- 终版 A 队对海选兼容 A 队 **70.28% ± 1.82 pp**。
- 终版 F 队对海选兼容 F 队 **84.31% ± 1.73 pp**。
- 海选兼容 A 队对终版 F 队 **29.03% ± 2.11 pp**。
- GLM 修正后 F 队默认阵 **31.08% ± 1.88 pp**。同种子变化 **-3.02 ± 2.86 pp**。
- GLM 修正后 F 队 R3 正式阵 **29.50% ± 3.11 pp**。同种子变化 **-2.00 ± 4.55 pp**。

## 计分与复现

- 主实验共 4,200 局，另外保留 26 局验收回放。核心补丁与结果摘要已整理至同目录 `patches/` 与 `results/`。
- 裁判与正式封存的 SHA-256 一致。统一参数为 `--sandbox none --workers 4 --step-ms 10`，首步预算 1,000 ms，同一时间只跑一个裁判批次。 现场资源抽查中，裁判与参赛程序的合计工作集最高 341.4 MiB，系统可用物理内存最低 4743.7 MiB。
- 每图种子从 1 开始，同种子两局镜像换边，名单、王位、站位与战术随队换边。胜计 1，平计 0.5，负计 0。先对换边两局取均值，再求样本标准差除以种子数平方根。三图等权先在同种子内平均。变化在同图同种子内先相减。± 表示一个配对 SE，pp 为百分点。
- 击杀取 kills − k_team，误击只计造成队友出局的 k_team，故障保留裁判终局并计入。所有并列数据按表中对阵顺序。
- 采用第一波的三张地图和默认站位。R3 复用该回合正式站位、战术、地图及其余九份程序。第一波基线和本轮均用四并发，历史 R3 档案用六并发。
- 输入、命令、回放和逐局结果可交叉核对。

## 5 · 五队长对称分路

Astra 的空单默认按正式队员短名分路，所有克隆短名都落到 B。第一波 g800–g803 是种子 1、2 各一对换边，Astra 20/20 个克隆席首次进入 B。Fable 空单首次触点按本队视角为 A/B/C/A/C。第一波主批 Astra 0/200 胜，场均对手击杀 21.14 / 8.06、占点分 1139.1 / 2300.9。

5a 启动前跑种子 1 的两局验收。两队各席按本队视角的首次触点均为 A/B/B/C/C，共 20/20。5b 初始一对镜像为 A/A/B/C/C，同样 20/20。触点按首次进入 2 米圈判定。右侧的本队 A/C 映射到世界 C/A。

| 对阵 | 地图 | 局数 | 首队胜/负/平 | 首队胜率 ± SE | 场均对手击杀 | 场均误击致死 | 场均占点分 | 故障 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 5a · 1–2–2 | 占点 | 200 | 169/31/0 | 84.50% ± 2.53 pp | 33.84 / 16.99 | 1.75 / 0.79 | 2050.5 / 1343.5 | 0 / 0 |
| 5b · 2–1–2 | 占点 | 200 | 173/27/0 | 86.50% ± 2.34 pp | 35.55 / 20.39 | 1.87 / 0.81 | 2137.0 / 1270.2 | 1 / 1 |

| 对阵 | 地图 | 每图配对种子数 | 同种子基线 | 同种子补测 | 配对变化 ± SE | 场均误击 基线 → 补测 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 5a · 1–2–2 | 占点 | 100 | 0.00% ± 0.00 pp | 84.50% ± 2.53 pp | +84.50 ± 2.53 pp | 3.42 / 1.53 → 1.75 / 0.79 |
| 5b · 2–1–2 | 占点 | 100 | 0.00% ± 0.00 pp | 86.50% ± 2.34 pp | +86.50 ± 2.34 pp | 3.42 / 1.53 → 1.87 / 0.81 |

同种子下，2–1–2 相对 1–2–2 的 Astra 胜率差为 **+2.00 ± 3.33 pp**。两种分路都把 Astra 从零胜变成明显优势。Fable 分别还能拿到 31 局和 27 局。

## 6 · 互换队长与静音

### 6a · 双向协议适配

Astra 改用 F 格式发当前位置和实际投掷、冲刺动作的端点。接收 F 的事件包时，用上一步已知位置或延迟观察恢复原有电台逻辑所需的起点，将事件映射到对应意图。Fable 用 A 格式发当步位置、意图和相位。接收时验相位，把新投掷、冲刺意图映射为事件，端点用延迟观察的朝向估计。A 的 16 位帧没有瞄准向量，F 的事件帧没有起点，F 的位置帧也没有近战意图。

三图种子 1 换边共六局。统计单位是已经进入下一帧、收发方满足活跃条件的一次有向收包，队长一发四收会分别计数。下表的坐标错误指位置包网格与发送时真实坐标的量化值不一致。

| 方向 | 非零送达 | 跨协议格式错误 | 位置包网格错误 |
| --- | ---: | ---: | ---: |
| Astra → F4 | 10,541 | 0 | 0/10,196 |
| F4 → Astra | 10,440 | 0 | 0/10,303 |
| Fable → A4 | 7,886 | 0 | 0/7,886 |
| A4 → Fable | 7,927 | 0 | 0/7,927 |

F4 继续用封存 GLM。Astra 发给 GLM 的位置包仍有 1,897 次在 GLM 原解码后偏离超过 1 米，见实验 8。Fable 收到的可与实际动作比对的 186 个合成事件中，估计朝向有 101 个偏差超过 45°。因此格式互通的胜率仍受端点估计、旧解码器及队长与队友配合影响。

| 对阵 | 地图 | 局数 | 首队胜/负/平 | 首队胜率 ± SE | 场均对手击杀 | 场均误击致死 | 场均占点分 | 故障 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Astra（F 语）带 F4 vs 原 A 队 | 歼灭 | 160 | 85/74/1 | 53.44% ± 3.97 pp | 3.18 / 3.11 | 0.27 / 0.47 | — | 0 / 0 |
| Astra（F 语）带 F4 vs 原 A 队 | 占点 | 160 | 75/85/0 | 46.88% ± 3.60 pp | 19.05 / 18.43 | 1.02 / 1.42 | 1601.3 / 1835.9 | 0 / 0 |
| Astra（F 语）带 F4 vs 原 A 队 | 刺王 | 160 | 94/65/1 | 59.06% ± 3.75 pp | 3.36 / 2.30 | 0.71 / 0.64 | — | 0 / 1 |
| Astra（F 语）带 F4 vs 原 A 队 | 三图等权 | 480 | 254/224/2 | 53.12% ± 2.10 pp | 8.53 / 7.95 | 0.67 / 0.84 | 1601.3 / 1835.9 | 0 / 1 |
| Astra（F 语）带 F4 vs Fable（A 语）带 A4 | 歼灭 | 160 | 134/24/2 | 84.38% ± 3.00 pp | 3.96 / 2.28 | 0.29 / 0.31 | — | 0 / 0 |
| Astra（F 语）带 F4 vs Fable（A 语）带 A4 | 占点 | 160 | 84/76/0 | 52.50% ± 3.55 pp | 20.67 / 16.14 | 0.75 / 1.91 | 1713.5 / 1710.7 | 0 / 0 |
| Astra（F 语）带 F4 vs Fable（A 语）带 A4 | 刺王 | 160 | 126/33/1 | 79.06% ± 2.83 pp | 3.86 / 2.21 | 0.74 / 0.54 | — | 0 / 0 |
| Astra（F 语）带 F4 vs Fable（A 语）带 A4 | 三图等权 | 480 | 344/133/3 | 71.98% ± 1.80 pp | 9.50 / 6.88 | 0.59 / 0.92 | 1713.5 / 1710.7 | 0 / 0 |
| Fable（A 语）带 A4 vs 原 F 队 | 歼灭 | 160 | 48/111/1 | 30.31% ± 3.82 pp | 2.96 / 3.70 | 0.34 / 0.25 | — | 0 / 0 |
| Fable（A 语）带 A4 vs 原 F 队 | 占点 | 160 | 44/115/1 | 27.81% ± 3.30 pp | 24.71 / 26.51 | 2.63 / 0.34 | 1496.6 / 1907.9 | 0 / 0 |
| Fable（A 语）带 A4 vs 原 F 队 | 刺王 | 160 | 69/83/8 | 45.62% ± 3.79 pp | 3.49 / 3.55 | 0.53 / 0.18 | — | 1 / 1 |
| Fable（A 语）带 A4 vs 原 F 队 | 三图等权 | 480 | 161/309/10 | 34.58% ± 2.00 pp | 10.39 / 11.25 | 1.17 / 0.26 | 1496.6 / 1907.9 | 1 / 1 |

| 对阵 | 地图 | 每图配对种子数 | 同种子基线 | 同种子补测 | 配对变化 ± SE | 场均误击 基线 → 补测 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Astra（F 语）带 F4 vs 原 A 队 | 歼灭 | 80 | 55.62% ± 3.77 pp | 53.44% ± 3.97 pp | -2.19 ± 5.41 pp | 0.41 / 0.49 → 0.27 / 0.47 |
| Astra（F 语）带 F4 vs 原 A 队 | 占点 | 80 | 28.12% ± 3.43 pp | 46.88% ± 3.60 pp | +18.75 ± 4.74 pp | 1.66 / 1.40 → 1.02 / 1.42 |
| Astra（F 语）带 F4 vs 原 A 队 | 刺王 | 80 | 43.44% ± 3.66 pp | 59.06% ± 3.75 pp | +15.62 ± 4.94 pp | 0.86 / 0.56 → 0.71 / 0.64 |
| Astra（F 语）带 F4 vs 原 A 队 | 三图等权 | 80/80/80 | 42.40% ± 2.20 pp | 53.12% ± 2.10 pp | +10.73 ± 2.89 pp | 0.98 / 0.82 → 0.67 / 0.84 |
| Astra（F 语）带 F4 vs Fable（A 语）带 A4 | 歼灭 | 80 | 79.06% ± 3.40 pp | 84.38% ± 3.00 pp | +5.31 ± 4.44 pp | 0.47 / 0.53 → 0.29 / 0.31 |
| Astra（F 语）带 F4 vs Fable（A 语）带 A4 | 占点 | 80 | 48.75% ± 3.87 pp | 52.50% ± 3.55 pp | +3.75 ± 5.09 pp | 1.27 / 3.75 → 0.75 / 1.91 |
| Astra（F 语）带 F4 vs Fable（A 语）带 A4 | 刺王 | 80 | 75.31% ± 2.93 pp | 79.06% ± 2.83 pp | +3.75 ± 4.27 pp | 0.59 / 0.79 → 0.74 / 0.54 |
| Astra（F 语）带 F4 vs Fable（A 语）带 A4 | 三图等权 | 80/80/80 | 67.71% ± 1.93 pp | 71.98% ± 1.80 pp | +4.27 ± 2.53 pp | 0.78 / 1.69 → 0.59 / 0.92 |
| Fable（A 语）带 A4 vs 原 F 队 | 歼灭 | 80 | 26.25% ± 3.08 pp | 30.31% ± 3.82 pp | +4.06 ± 4.86 pp | 0.54 / 0.24 → 0.34 / 0.25 |
| Fable（A 语）带 A4 vs 原 F 队 | 占点 | 80 | 21.25% ± 3.42 pp | 27.81% ± 3.30 pp | +6.56 ± 4.74 pp | 4.76 / 0.33 → 2.63 / 0.34 |
| Fable（A 语）带 A4 vs 原 F 队 | 刺王 | 80 | 41.25% ± 3.67 pp | 45.62% ± 3.79 pp | +4.38 ± 5.57 pp | 0.94 / 0.24 → 0.53 / 0.18 |
| Fable（A 语）带 A4 vs 原 F 队 | 三图等权 | 80/80/80 | 29.58% ± 2.02 pp | 34.58% ± 2.00 pp | +5.00 ± 3.09 pp | 2.08 / 0.27 → 1.17 / 0.26 |

Astra 带 F4 对原 A 队，从 42.40% ± 2.20 pp 变为 **53.12% ± 2.10 pp**。双适配互换后，Astra 带 F4 的配对变化为 **+4.27 ± 2.53 pp**。Fable 带 A4 对原 F 队的配对变化为 **+5.00 ± 3.09 pp**，该队场均误击致死 2.08 → 1.17。

### 6b · 双方全员静音

十份选手源码原样重编，仅在共用头文件的动作输出处把第六列固定为 0，维持原始配置与各程序内部动作选择。比较窗口是种子 1–60。由于两边同时静音，差值反映本组无线电对头对头结果的净效应。

| 对阵 | 地图 | 局数 | 首队胜/负/平 | 首队胜率 ± SE | 场均对手击杀 | 场均误击致死 | 场均占点分 | 故障 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| A 静音 vs F 静音 | 歼灭 | 120 | 65/54/1 | 54.58% ± 4.81 pp | 3.48 / 2.83 | 0.76 / 0.36 | — | 0 / 0 |
| A 静音 vs F 静音 | 占点 | 120 | 90/30/0 | 75.00% ± 3.66 pp | 25.10 / 24.98 | 2.75 / 1.14 | 1982.8 / 1427.0 | 0 / 0 |
| A 静音 vs F 静音 | 刺王 | 120 | 81/37/2 | 68.33% ± 4.24 pp | 3.39 / 2.92 | 0.88 / 0.66 | — | 1 / 0 |
| A 静音 vs F 静音 | 三图等权 | 360 | 236/121/3 | 65.97% ± 2.07 pp | 10.66 / 10.25 | 1.46 / 0.72 | 1982.8 / 1427.0 | 1 / 0 |

| 对阵 | 地图 | 每图配对种子数 | 同种子基线 | 同种子补测 | 配对变化 ± SE | 场均误击 基线 → 补测 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| A 静音 vs F 静音 | 歼灭 | 60 | 55.83% ± 4.46 pp | 54.58% ± 4.81 pp | -1.25 ± 6.68 pp | 0.48 / 0.12 → 0.76 / 0.36 |
| A 静音 vs F 静音 | 占点 | 60 | 75.83% ± 3.85 pp | 75.00% ± 3.66 pp | -0.83 ± 4.83 pp | 2.21 / 0.44 → 2.75 / 1.14 |
| A 静音 vs F 静音 | 刺王 | 60 | 70.00% ± 4.24 pp | 68.33% ± 4.24 pp | -1.67 ± 5.91 pp | 0.72 / 0.18 → 0.88 / 0.66 |
| A 静音 vs F 静音 | 三图等权 | 60/60/60 | 67.22% ± 2.69 pp | 65.97% ± 2.07 pp | -1.25 ± 3.38 pp | 1.14 / 0.25 → 1.46 / 0.72 |

六局回放抽样共 45,530 条席位动作，非零 MSG 为 0。

## 7 · 海选原班人马与团队终版

### 7a · 海选原生态

十个海选可执行文件的 SHA-256 都与相应封存工作目录快照 ZIP 根目录内的 player.exe 一致。使用第 11 集原件目录的同一字节副本。

| 地图 | 局数 | 受影响席次 | 第 0 步退出码 0 | 裁判步数范围 |
| --- | ---: | ---: | ---: | ---: |
| 歼灭 | 40 | 400 | 400 | 391–421 |
| 占点 | 40 | 400 | 400 | 1200–1200 |
| 刺王 | 40 | 400 | 400 | 421–451 |

三图每位选手 40/40 席次均在初始化退出。旧头文件看见 SEAT 人数大于 8 就拒绝解析，run_bot 随即返回 0。正式裁判记录为 crash，退出码是 0x00000000。退役棋子随后由裁判继续推进，对局胜负只反映该退役流程。

### 7b · 最小兼容版

全队共用旧头文件的兼容副本。人数上限改为 10，裁判的三种团队模式仍按正式规则运行。旧程序内部映射到原有团队模式 2，读走扩展握手与每步 STATE 行，继续用自己的旧推演和团队策略。

- Astra、Sol、Hydrogen、Grok、Carbon 只换兼容头文件。Kimi 的历史容量从 805 改到 1201，并使用已有队伍编号过滤威胁推演里假想投向自己的队友。Fable 的历史容量从 1100 改到 1202，保持原边界判断并覆盖 1200 步。
- GLM 将历史容量从 950 改到 1201，九个席位数组从固定 8 改为 MAXP。Opus 将十个席位的威胁倍率统一初始化为原先的 1。
- DeepSeek 将历史容量从 900 改到 1201，先检边界再访问，补齐阵亡时的写入检查。选择对手与奖励对手击杀时排除队友；队友的飞镖仍是碰撞威胁。
- 六局兼容烟测的 60 席都完成初始化与收尾，其中一局 Kimi 有一次步内超时。两局占点都走满 1,200 步。完整兼容差异附在文末。

| 对阵 | 地图 | 局数 | 首队胜/负/平 | 首队胜率 ± SE | 场均对手击杀 | 场均误击致死 | 场均占点分 | 故障 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 终版 A 队 vs 海选兼容 A 队 | 歼灭 | 120 | 64/56/0 | 53.33% ± 3.92 pp | 3.02 / 3.09 | 0.47 / 0.91 | — | 0 / 0 |
| 终版 A 队 vs 海选兼容 A 队 | 占点 | 120 | 120/0/0 | 100.00% ± 0.00 pp | 20.52 / 19.52 | 1.85 / 3.44 | 2900.4 / 489.2 | 0 / 0 |
| 终版 A 队 vs 海选兼容 A 队 | 刺王 | 120 | 69/51/0 | 57.50% ± 4.73 pp | 3.23 / 3.12 | 0.65 / 0.97 | — | 0 / 0 |
| 终版 A 队 vs 海选兼容 A 队 | 三图等权 | 360 | 253/107/0 | 70.28% ± 1.82 pp | 8.92 / 8.58 | 0.99 / 1.77 | 2900.4 / 489.2 | 0 / 0 |
| 终版 F 队 vs 海选兼容 F 队 | 歼灭 | 120 | 99/21/0 | 82.50% ± 3.32 pp | 2.86 / 2.66 | 0.18 / 1.46 | — | 0 / 0 |
| 终版 F 队 vs 海选兼容 F 队 | 占点 | 120 | 120/0/0 | 100.00% ± 0.00 pp | 6.67 / 4.91 | 0.15 / 6.48 | 3275.0 / 149.1 | 0 / 0 |
| 终版 F 队 vs 海选兼容 F 队 | 刺王 | 120 | 84/35/1 | 70.42% ± 4.22 pp | 2.95 / 3.17 | 0.31 / 1.29 | — | 0 / 0 |
| 终版 F 队 vs 海选兼容 F 队 | 三图等权 | 360 | 303/56/1 | 84.31% ± 1.73 pp | 4.16 / 3.58 | 0.21 / 3.08 | 3275.0 / 149.1 | 0 / 0 |
| 海选兼容 A 队 vs 终版 F 队 | 歼灭 | 120 | 37/82/1 | 31.25% ± 4.66 pp | 2.98 / 2.83 | 1.14 / 0.26 | — | 0 / 0 |
| 海选兼容 A 队 vs 终版 F 队 | 占点 | 120 | 2/118/0 | 1.67% ± 1.17 pp | 18.29 / 18.33 | 2.86 / 0.41 | 788.6 / 2603.5 | 0 / 0 |
| 海选兼容 A 队 vs 终版 F 队 | 刺王 | 120 | 64/54/2 | 54.17% ± 4.21 pp | 3.70 / 3.02 | 0.93 / 0.28 | — | 0 / 0 |
| 海选兼容 A 队 vs 终版 F 队 | 三图等权 | 360 | 103/254/3 | 29.03% ± 2.11 pp | 8.32 / 8.06 | 1.64 / 0.31 | 788.6 / 2603.5 | 0 / 0 |

终版对本队兼容版的优势包括团队目标、现代模拟器和通信等整套变化。7b 的比较对象是能读 5v5 裁判输入的海选代码。

## 8 · GLM 坐标解包

终版源文件第 800 行与 R3 改前备份第 801 行只交换 qx/qy 的位来源，低七位归 x。R3 改前备份用正式参数重编，产物与正赛封存 GLM 的 SHA-256 完全一致。

| 解码版本 | 回放局数 | F=1/2/3 | 解出的网格与发包不一致 | 位置偏差 > 1 米 | 位置平均偏差 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 原 GLM | 6 | 6,682/81/71 | 6,713/6,834 | 6,049/6,682 | 10699.5 mm |
| 修复 GLM | 6 | 8,015/85/61 | 0/8,161 | 0/8,015 | 95.7 mm |

以正式回放实际送达的队友包复算两版解码。修正后位置网格偏差为 0，物理位置误差是 250 mm 网格量化。

| 对阵 | 地图 | 局数 | 首队胜/负/平 | 首队胜率 ± SE | 场均对手击杀 | 场均误击致死 | 场均占点分 | 故障 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| F 队（修 GLM）vs 原 A 队 | 歼灭 | 200 | 93/106/1 | 46.75% ± 3.29 pp | 3.03 / 3.62 | 0.24 / 0.52 | — | 0 / 0 |
| F 队（修 GLM）vs 原 A 队 | 占点 | 200 | 41/159/0 | 20.50% ± 2.67 pp | 24.75 / 25.75 | 0.37 / 2.19 | 1380.1 / 2030.1 | 0 / 0 |
| F 队（修 GLM）vs 原 A 队 | 刺王 | 200 | 50/146/4 | 26.00% ± 3.01 pp | 2.81 / 3.69 | 0.32 / 0.86 | — | 0 / 0 |
| F 队（修 GLM）vs 原 A 队 | 三图等权 | 600 | 184/411/5 | 31.08% ± 1.88 pp | 10.20 / 11.02 | 0.31 / 1.19 | 1380.1 / 2030.1 | 0 / 0 |
| R3 F 队（修 GLM）vs R3 A 队 | 占点 | 200 | 59/141/0 | 29.50% ± 3.11 pp | 26.59 / 37.05 | 1.73 / 2.30 | 1550.4 / 1858.1 | 1 / 0 |

| 对阵 | 地图 | 每图配对种子数 | 同种子基线 | 同种子补测 | 配对变化 ± SE | 场均误击 基线 → 补测 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| F 队（修 GLM）vs 原 A 队 | 歼灭 | 80 | 47.50% ± 3.97 pp | 47.19% ± 3.72 pp | -0.31 ± 5.63 pp | 0.16 / 0.51 → 0.25 / 0.53 |
| F 队（修 GLM）vs 原 A 队 | 占点 | 80 | 26.25% ± 3.56 pp | 20.00% ± 3.03 pp | -6.25 ± 4.21 pp | 0.39 / 2.17 → 0.39 / 2.26 |
| F 队（修 GLM）vs 原 A 队 | 刺王 | 80 | 27.50% ± 3.57 pp | 25.00% ± 3.42 pp | -2.50 ± 4.74 pp | 0.22 / 0.72 → 0.31 / 0.86 |
| F 队（修 GLM）vs 原 A 队 | 三图等权 | 80/80/80 | 33.75% ± 2.31 pp | 30.73% ± 2.10 pp | -3.02 ± 2.86 pp | 0.25 / 1.14 → 0.31 / 1.21 |
| R3 F 队（修 GLM）vs R3 A 队 | 占点 | 100 | 31.50% ± 3.38 pp | 29.50% ± 3.11 pp | -2.00 ± 4.55 pp | 1.93 / 2.00 → 1.73 / 2.30 |

- F 队（修 GLM）vs 原 A 队同种子下，GLM 场均误击致死 0.06 → 0.02，场均对手击杀 0.48 → 0.52。
- R3 F 队（修 GLM）vs R3 A 队同种子下，GLM 场均误击致死 1.25 → 1.16，场均对手击杀 1.27 → 1.41。

默认阵三图的 F 队胜率变化为 **-3.02 ± 2.86 pp**，两倍配对 SE 的区间包含 0。 R3 正式阵的 F 队胜率变化为 **-2.00 ± 4.55 pp**，两倍配对 SE 的区间包含 0。

## 构建验证与完整差异

正式编译选项为 `g++ -std=c++17 -O2 -static -s -Wl,--stack,67108864`。GCC 版本、命令、退出码、源码、头文件及产物 SHA-256 均记录在独立目录。下表列成功构建，Astra 适配版第一次编译返回非零，重跑成功，两份记录均保留。

| 目标 | 成功退出码 | 二进制 SHA-256 | 核对记录 |
| --- | ---: | --- | --- |
| astra_speak_f | 0 | 247f4528cd01be4a943984a14e7e955f4fdbd09bf17f5be4fccbdd410a778f26 |  |
| fable_speak_a | 0 | 125660e7293ec54599cacf640767c47bb1699487e638cf4e45b9038573543ff1 |  |
| glm_r3_rebuild | 0 | d854909de34bab73115179b8a0ca928c014dabb86b8900c99641c1b63babc84f |  |
| glm_r3_xy | 0 | 93d6a249fc39b5c976af7ca1bec7ec044c5432a87be40c6544ef0dab4231e19b |  |
| glm_xy | 0 | e7215f5353196007b2a9338555b7d02de4af6038d3c9cd04482fb05c0182d99f |  |
| astra | 0 | f0a8cd37edf51d4b138a0c83f10db58e71ec876906ea75f912ca69ed5f8e3cd9 |  |
| carbon | 0 | 330ad3933dbd16d0c7c210e494ec79fe2c1d925c339107c10f2f2badf937d8bb |  |
| deepseek | 0 | 7467e59183dbed6fe1a0afc49438bd4f9f6c41f3d9a15893d0fcb71b0f3849ba |  |
| fable | 0 | c4e3f7c84a63f892305a5a2b21e9c6d0d6ed9673138dae650fa527f0ef2909ac |  |
| glm | 0 | 27a822fdac2a7703275c8806e86ff395956a720761deb35baa7854e19356dbd1 |  |
| grok | 0 | f41ff88ddf6a2d0444187ead39987ac1e10769206d438ce2ebe7df22d2b8ca02 |  |
| hydrogen | 0 | 2a8100d1a1a5ccaca4a0f29a2e1f3d587c93255e235e93262c1ac6d673dda273 |  |
| kimi | 0 | 3ed90265a54409bd8a3c9ff84c51f06f471e79a0f547c38b01021c83b86bd675 |  |
| opus | 0 | aa901cc1c1653ef419faca78df57c9c80a31880109348965ad6a72fec1671b8e |  |
| sol | 0 | 695d5a1831bb1de2a748c89190a70d81e82fc61a01e20457e9dd5b375d50eb5e |  |
| mute_astra | 0 | c6ee733be3f26338062881ebf59d2b9e001e5385bf7c7d6c47e2cdd125f42e27 |  |
| mute_carbon | 0 | 8fe97e2e0e4f0b3319640492f8f3b5cc95ec6899bbd7a82db12afb262195cd06 |  |
| mute_deepseek | 0 | facc6d6495930a672a6aa488b6dd4fad54c0ef5830a9e98c37a5eedecb67b457 |  |
| mute_fable | 0 | 19bf5abbeeee734cca156b5358212efe19a85b920734697c878d11dcea94d0df |  |
| mute_glm | 0 | 8d5dde7670cce179e2063a11b7d70ffe1c66c50f7f9f5e3ff053d51065201832 |  |
| mute_grok | 0 | 1d57a7dc14ed432c938e5646ac65916ae7292b7ea7f354f3fb91402ec63c6272 |  |
| mute_hydrogen | 0 | d05bd041040c1c601906ab00689ed4fdf2bdcaeb6f9e56dc55b00f41c93a6fee |  |
| mute_kimi | 0 | d705b9e7585c427734e71b96bdade681fcc442da97c8918e71de9062e557f807 |  |
| mute_opus | 0 | 3fe45c35dcd7f2313a5773cc035a4d3841a62951e6aebf701aa301cd35eb0fc1 |  |
| mute_sol | 0 | b64b52019266ebd9c95ebaba55fdcab3f1ff4a7fc9dde9f331210c9fcdb33c56 |  |

<details>
<summary>legacy_compat/dart.hpp.diff</summary>

```diff
--- legacy/astra/dart.hpp
+++ legacy_compat/dart.hpp
@@ -73,7 +73,7 @@

 constexpr const char* ENGINE_VERSION = "dartfu-v3";
 constexpr int MAX_DELAY = 20;      // DELAY 的上限（步）
-constexpr int MAXP = 8;            // 一局最多几人
+constexpr int MAXP = 10;            // 一局最多几人
 constexpr i64 TICKS = 20;          // 每秒步数
 constexpr i64 FULL = 36000;        // 一圈
 constexpr i64 CELL = 500;          // 格子边长（毫米）；程序勿写死格子尺寸
@@ -962,6 +962,7 @@
 struct Event { i64 t, type, a, b, c, d; };

 struct GameInfo {
+    bool extended_team_wire = false;
     Config cfg;
     std::shared_ptr<const Map> map;
     int n = 0;               // 这一局几人
@@ -2034,6 +2035,8 @@
         if (!(is >> gi.team[i])) return false;
     auto m = std::make_shared<Map>();
     if (!read_map(is, *m)) return false;
+    gi.extended_team_wire = gi.cfg.mode >= 3 && gi.cfg.mode <= 5;
+    if (gi.extended_team_wire) gi.cfg.mode = M_TEAM;
     const int md = int(iclamp(gi.cfg.mode, 0, 2));
     m->modes[md] = true;
     if (!(is >> tok) || tok != "SPAWN") return false;
@@ -2041,6 +2044,12 @@
         if (!(is >> gi.spx[i] >> gi.spy[i])) return false;
         m->spx[md][i] = gi.spx[i];
         m->spy[md][i] = gi.spy[i];
+    }
+    if (gi.extended_team_wire) {
+        std::getline(is, line); // remainder of SPAWN
+        for (const char* key : {"BASE ", "NAMES ", "KING ", "POINTS ", "PLAN"}) {
+            if (!std::getline(is, line) || line.rfind(key, 0) != 0) return false;
+        }
     }
     gi.map = m;
     return true;
@@ -2062,6 +2071,11 @@
         Dart& D = o.d[i];
         D = Dart();
         if (!(is >> D.st >> D.x >> D.y >> D.vx >> D.vy >> D.left >> D.age)) return false;
+    }
+    if (gi.extended_team_wire) {
+        std::string line;
+        std::getline(is, line); // remainder of the last dart record
+        if (!std::getline(is, line) || line.rfind("STATE ", 0) != 0) return false;
     }
     return true;
 }
```

</details>

<details>
<summary>legacy_compat/deepseek.cpp.diff</summary>

```diff
--- legacy/deepseek/player.cpp
+++ legacy_compat/deepseek.cpp
@@ -9,7 +9,7 @@

 using namespace dart;

-static const int MAXH = 900;
+static const int MAXH = 1201;

 struct Bot {
     Config cf;
@@ -115,7 +115,7 @@
             out = g;
             return true;
         }
-        if (!haveP[T0] || T0 >= MAXH) {
+        if (T0 >= MAXH || !haveP[T0]) {
             g.t = o.t;
             out = g;
             return false;
@@ -263,7 +263,7 @@
         Game g = base;
         g.hist.reset();
         g.over = false;
-        const i64 k0 = g.p[seat].kills;
+        const i64 k0 = g.p[seat].kills - g.p[seat].k_team;
         killOut = 0;
         double score = 0;
         bool dead = false;
@@ -286,7 +286,7 @@
             g.step(acts);
             g.over = false;
             if (!g.p[seat].alive) { dead = true; deadAt = s; break; }
-            const i64 km = g.p[seat].kills;
+            const i64 km = g.p[seat].kills - g.p[seat].k_team;
             if (km > k0) {
                 const i64 d = km - k0;
                 score += 22000.0 * (double)d;
@@ -295,7 +295,7 @@
             score += 2.0;
         }
         dbgEndX = g.p[seat].x; dbgEndY = g.p[seat].y; dbgN = (i64)g.n;
-        killOut = (double)(g.p[seat].kills - k0);
+        killOut = (double)(g.p[seat].kills - g.p[seat].k_team - k0);
         if (dead) return -1.0e8 + (double)deadAt * 100.0 + score * 0.01;

         const Player& me = g.p[seat];
@@ -303,7 +303,7 @@
         // 找最近对手
         i64 nearest = -1, nd = 0;
         for (int k = 0; k < g.n; ++k) {
-            if (k == seat || !g.p[k].alive) continue;
+            if (k == seat || is_mate(info(), k) || !g.p[k].alive) continue;
             const i64 d = ilen(g.p[k].x - me.x, g.p[k].y - me.y);
             if (nearest < 0 || d < nd) { nearest = k; nd = d; }
         }
@@ -346,7 +346,7 @@
         // 四人局：躲开有镖的对手，抓住对方空手/硬直的机会
         if (cf.mode == M_FFA) {
             for (int k = 0; k < g.n; ++k) {
-                if (k == seat || !g.p[k].alive) continue;
+                if (k == seat || is_mate(info(), k) || !g.p[k].alive) continue;
                 const i64 d = ilen(g.p[k].x - me.x, g.p[k].y - me.y);
                 const bool foeArmed = (g.d[k].st == D_HAND);
                 const bool busy = (g.p[k].st != S_FREE);
@@ -373,7 +373,7 @@
         recordSelf(o);
         const Player& me = o.me();
         Action res;
-        if (!me.alive) { myA[o.t] = res; return res; }
+        if (!me.alive) { if (o.t < MAXH) myA[o.t] = res; return res; }

         Game base;
         buildWorld(o, base);
@@ -416,7 +416,7 @@
         int tgt = -1;
         i64 tgtD = 0;
         for (int k = 0; k < n; ++k) {
-            if (k == seat) continue;
+            if (k == seat || is_mate(info(), k)) continue;
             if (!base.p[k].alive || base.p[k].unknown) continue;
             const i64 d = ilen(base.p[k].x - me.x, base.p[k].y - me.y);
             if (tgt < 0 || d < tgtD) { tgt = k; tgtD = d; }
@@ -478,7 +478,7 @@
         // ---------- 近战候选 ----------
         if (me.st == S_FREE) {
             for (int k = 0; k < n; ++k) {
-                if (k == seat || !base.p[k].alive || base.p[k].unknown || hiddenEst[k]) continue;
+                if (k == seat || is_mate(info(), k) || !base.p[k].alive || base.p[k].unknown || hiddenEst[k]) continue;
                 for (int j = 0; j <= 1; ++j) {
                     if (!paths.alive[k][j]) continue;
                     const i64 dx = paths.x[k][j] - me.x, dy = paths.y[k][j] - me.y;
@@ -507,7 +507,7 @@
         if (me.st == S_FREE && md.st == D_HAND && cf.mode == M_FFA) {
             int order[MAXP], no = 0;
             for (int k = 0; k < n; ++k) {
-                if (k == seat || !base.p[k].alive || base.p[k].unknown || hiddenEst[k]) continue;
+                if (k == seat || is_mate(info(), k) || !base.p[k].alive || base.p[k].unknown || hiddenEst[k]) continue;
                 order[no++] = k;
             }
             for (int a = 0; a < no; ++a)
@@ -556,7 +556,7 @@
         if (me.st == S_FREE && md.st == D_HAND && cf.mode != M_FFA) {
             int tried = 0;
             for (int k = 0; k < n && tried < 10; ++k) {
-                if (k == seat || !base.p[k].alive || base.p[k].unknown || hiddenEst[k]) continue;
+                if (k == seat || is_mate(info(), k) || !base.p[k].alive || base.p[k].unknown || hiddenEst[k]) continue;
                 for (int j = 16; j >= 2 && tried < 12; j -= 1) {
                     if (!paths.alive[k][j]) continue;
                     const i64 dx = paths.x[k][j] - me.x, dy = paths.y[k][j] - me.y;
```

</details>

<details>
<summary>legacy_compat/fable.cpp.diff</summary>

```diff
--- legacy/fable/player.cpp
+++ legacy_compat/fable.cpp
@@ -13,7 +13,7 @@
 using namespace dart;

 namespace {
-const int MAXT = 1100;
+const int MAXT = 1202;
 const i64 INF_LIFE = 1000000;
 enum World { W_CONT = 0, W_STOP, W_TOWARD, W_AWAY, W_LEFT, W_RIGHT, W_AGGR, W_N };
 enum PlanType { P_MOVE = 0, P_THROW, P_MELEE, P_DASH, P_RECALL, P_DASHMELEE };
```

</details>

<details>
<summary>legacy_compat/glm.cpp.diff</summary>

```diff
--- legacy/glm/player.cpp
+++ legacy_compat/glm.cpp
@@ -17,7 +17,7 @@

 struct Bot {
     // ---------------- 历史记录（延迟补偿用） ----------------
-    static const int HN = 950;
+    static const int HN = 1201;
     struct HRec {
         Player me;
         Dart md;
@@ -31,14 +31,14 @@
     std::vector<unsigned char> walk0;        // 开局能走的格
     std::vector<i64> flight_cum;             // 去程飞 s 步后的累计距离
     // 每个对手的网格距离图（本轮 decide 里算一次）
-    std::vector<int> dmap[8];
+    std::vector<int> dmap[MAXP];
     int dmap_n = 0;
     i64 safe_cx = 0, safe_cy = 0;            // 安全区质心
     i64 cpu0 = 0;
-    i64 lastx[8] = {}, lasty[8] = {}, lastt[8] = {};
-    bool ever_seen[8] = {};
+    i64 lastx[MAXP] = {}, lasty[MAXP] = {}, lastt[MAXP] = {};
+    bool ever_seen[MAXP] = {};
     int softn = 0;                            // 当前看不见但最近见过的对手
-    i64 softx[8] = {}, softy[8] = {};
+    i64 softx[MAXP] = {}, softy[MAXP] = {};

     struct Cand {
         Action a0;        // 第 0 步的动作
@@ -595,8 +595,8 @@
         }

         // -------- 估计的敌人列表（按距离排序） --------
-        int eidx[8], ne = 0;
-        i64 ed[8];
+        int eidx[MAXP], ne = 0;
+        i64 ed[MAXP];
         for (int k = 0; k < g.n; ++k) {
             if (k == seat || is_mate(gi, k) || !g.p[k].alive || g.p[k].unknown) continue;
             eidx[ne] = k;
```

</details>

<details>
<summary>legacy_compat/kimi.cpp.diff</summary>

```diff
--- legacy/kimi/player.cpp
+++ legacy_compat/kimi.cpp
@@ -6,7 +6,7 @@
 using namespace dart;

 struct Bot {
-    static constexpr int MAXT = 805;
+    static constexpr int MAXT = 1201;
     std::array<Player, MAXT> hp{};
     std::array<Dart, MAXT> hd{};
     std::array<Action, MAXT> ha{};
@@ -49,7 +49,7 @@
             for (int k=0;k<o.n;k++) if (k!=o.seat && g.p[k].alive && !g.p[k].unknown) {
                 if (g.p[k].st==S_FREE) {
                     as[k].mx=g.p[k].vx; as[k].my=g.p[k].vy;
-                    if (threat && g.d[k].st==D_HAND && ilen(g.p[k].x-g.p[o.seat].x,g.p[k].y-g.p[o.seat].y)<4300) {
+                    if (threat && !is_mate(info(), k) && g.d[k].st==D_HAND && ilen(g.p[k].x-g.p[o.seat].x,g.p[k].y-g.p[o.seat].y)<4300) {
                         as[k].act=A_THROW;
                         as[k].ax=g.p[o.seat].x+g.p[o.seat].vx*5-g.p[k].x;
                         as[k].ay=g.p[o.seat].y+g.p[o.seat].vy*5-g.p[k].y;
```

</details>

<details>
<summary>legacy_compat/opus.cpp.diff</summary>

```diff
--- legacy/opus/player.cpp
+++ legacy_compat/opus.cpp
@@ -66,7 +66,7 @@
     i64 lsx[MAXP], lsy[MAXP], lst[MAXP];
     double armedSteps[MAXP] = {0, 0, 0, 0, 0, 0, 0, 0}, nThrows[MAXP] = {0, 0, 0, 0, 0, 0, 0, 0};
     i64 prevSt[MAXP] = {0, 0, 0, 0, 0, 0, 0, 0};
-    double thrScale[MAXP] = {1, 1, 1, 1, 1, 1, 1, 1};
+    double thrScale[MAXP];
     std::vector<int> safeField;
     int nx = 0, ny = 0;
     i64 cpu0 = 0;
@@ -88,6 +88,7 @@
         cf = gi.cfg;
         me = gi.seat;
         N = gi.n;
+        std::fill_n(thrScale, MAXP, 1.0);
         nx = gi.M().nx;
         ny = gi.M().ny;
         rng ^= u64(me + 1) * 0x2545F4914F6CDD1DULL;
```

</details>

<details>
<summary>mute_src/dart.hpp.diff</summary>

```diff
--- kit/dart.hpp
+++ mute_src/dart.hpp
@@ -2570,7 +2570,7 @@
     Obs o;
     while (read_obs(std::cin, gi, o)) {
         const Action a = bot.decide(o);
-        std::cout << a.mx << ' ' << a.my << ' ' << a.act << ' ' << a.ax << ' ' << a.ay << ' ' << a.msg << '\n' << std::flush;
+        std::cout << a.mx << ' ' << a.my << ' ' << a.act << ' ' << a.ax << ' ' << a.ay << ' ' << 0 << '\n' << std::flush;
     }
     return 0;
 }
```

</details>

<details>
<summary>patches/astra_speak_f.cpp.diff</summary>

```diff
--- evidence/astra.cpp
+++ patches/astra_speak_f.cpp
@@ -319,30 +319,45 @@
             kingInv[k]=std::max(0,immuneUntil[k]-seen);
         }
     }
-    int message(const Player& me,int act) const {
-        int intent=0;
-        if(act==A_THROW) intent=1;
-        else if(act==A_MELEE) intent=2;
-        else if(act==A_DASH) intent=3;
-        else if(me.st==S_THROW) intent=1;
-        else if(me.st==S_SLASH || me.st==S_KICK) intent=2;
-        else if(me.st==S_DASH) intent=3;
-        int x=std::clamp(cell_index(me.x),0,63),y=std::clamp(cell_index(me.y),0,63);
-        return x+64*y+4096*intent+16384*(now%4);
+    int message(const Obs& o,const Action& a) const {
+        const Player& me=o.me();
+        i64 x=me.x,y=me.y;
+        int kind=1;
+        i64 ux,uy;
+        if(me.st==S_FREE && scale_to(a.ax,a.ay,1000,ux,uy)) {
+            if(a.act==A_THROW && o.my_dart().st==D_HAND) {
+                const i64 w=gi->M().width(),h=gi->M().height();
+                for(i64 length=c->range;length>0;length-=100) {
+                    const i64 ex=x+ux*length/1000,ey=y+uy*length/1000;
+                    if(ex>=300 && ey>=300 && ex<=w-300 && ey<=h-300) {
+                        x=ex; y=ey; kind=2; break;
+                    }
+                }
+            } else if(a.act==A_DASH && me.cd==0) {
+                const i64 length=c->dash_v*c->dash_t;
+                x+=ux*length/1000; y+=uy*length/1000; kind=3;
+            }
+        }
+        return iclamp(x/250,0,127)+128*iclamp(y/250,0,127)+16384*kind;
     }
     void receive() {
         if(!now) return;
         for(int k=0;k<n;++k) if(is_mate(*gi,k)) {
-            int v=int(obs->p[k].msg),time=now-1;
-            int x=v&63,y=(v>>6)&63;
-            if(!v || v>>14!=time%4 || !gi->M().inside(x,y)) continue;
+            const int v=int(obs->p[k].msg),time=now-1,kind=v>>14;
+            if(kind<1 || kind>3 || !obs->p[k].alive) continue;
+            const Packet& previous=radio[k];
+            // F=2/3 carries an endpoint; the most recent position is the event origin estimate.
+            Vec estimate=kind==1 ? vec((v&127)*250+125,((v>>7)&127)*250+125)
+                                 : previous.time==time-1 ? previous.where : pos(obs->p[k]);
+            int x=cell_index(ir(estimate.x)),y=cell_index(ir(estimate.y));
+            if(!gi->M().inside(x,y)) continue;
             Vec at=vec(cell_center(x),cell_center(y));
             if(obs->p[k].alive && (at-pos(obs->p[k])).sq()>3700.*3700) continue;
             Packet& p=radio[k];
             bool contiguous=p.time==time-1 && (at-p.where).sq()<1550.*1550;
             Vec delta=at-p.where;
             p.velocity=contiguous?p.velocity*0.56+delta*0.44:Vec{};
-            int intent=(v>>12)&3;
+            int intent=kind==2?1:kind==3?3:0;
             if(!contiguous || intent!=p.intent) p.since=time;
             p.run=contiguous?p.run+1:1; p.time=time; p.where=at; p.intent=intent;
         }
@@ -2982,7 +2997,7 @@
         rememberScene();
         if(groupLearning) watchReactions();
         receive();
-        result.msg=message(o.me(),A_NONE);
+        result.msg=message(o,result);
         bool armed=o.my_dart().st==D_HAND;
         bool free=o.me().st==S_FREE || o.me().st==S_RECALL;
         bool cancel=c->dash_slash && o.me().st==S_DASH && o.me().timer>c->dash_post && o.me().timer<=c->dash_t+c->dash_post;
@@ -3249,7 +3264,7 @@
         if(best.action==A_THROW && !best.intercept && !shot.spawn) recordThrow(shot.target);
         if(best.action!=A_RECALL) { result.mx=ir(best.move.x*1000); result.my=ir(best.move.y*1000); }
         result.ax=ir(best.aim.x*1000); result.ay=ir(best.aim.y*1000); result.act=best.action;
-        result.msg=message(o.me(),best.action);
+        result.msg=message(o,result);
         previousMove=best.move;
         history[now%HIST].action=result;
         return result;
```

</details>

<details>
<summary>patches/fable_speak_a.cpp.diff</summary>

```diff
--- evidence/fable.cpp
+++ patches/fable_speak_a.cpp
@@ -98,6 +98,8 @@
     unsigned char mrf[MAXP][MAXT] = {};
     i64 simWork = 0, nAct = 10;
     std::string myName;
+    int aIntent[MAXP] = {};
+    i64 aTime[MAXP] = {};

     void applyKey(const GameInfo& gi, const std::string& k, const std::string& v) {
         if (k == "pt") { const int i = gi.point_index(v); if (i >= 0) planPt = i; }
@@ -920,26 +922,15 @@
         const Player& M0 = o.me();
         a.msg = 0;
         if (M0.alive) {
-            i64 px = M0.x, py = M0.y;
-            int F = 1;
-            if (a.act == A_THROW && M0.st == S_FREE && o.my_dart().st == D_HAND && (a.ax || a.ay)) {
-                i64 ux, uy;
-                if (scale_to(a.ax, a.ay, 1000, ux, uy)) {
-                    const i64 W = info().M().width(), H = info().M().height();
-                    for (i64 L = cf.range; L > 0; L -= 100) {
-                        const i64 qx = M0.x + ux * L / 1000, qy = M0.y + uy * L / 1000;
-                        if (qx >= 300 && qy >= 300 && qx <= W - 300 && qy <= H - 300) { px = qx; py = qy; F = 2; break; }
-                    }
-                }
-            }
-            if (a.act == A_DASH && M0.st == S_FREE && M0.cd == 0 && (a.ax || a.ay)) {
-                i64 ux, uy;
-                if (scale_to(a.ax, a.ay, 1000, ux, uy)) {
-                    const i64 L = cf.dash_v * cf.dash_t;
-                    px = M0.x + ux * L / 1000; py = M0.y + uy * L / 1000; F = 3;
-                }
-            }
-            a.msg = iclamp(px / 250, 0, 127) + 128 * iclamp(py / 250, 0, 127) + 16384 * F;
+            int intent = 0;
+            if (a.act == A_THROW) intent = 1;
+            else if (a.act == A_MELEE) intent = 2;
+            else if (a.act == A_DASH) intent = 3;
+            else if (M0.st == S_THROW) intent = 1;
+            else if (M0.st == S_SLASH || M0.st == S_KICK) intent = 2;
+            else if (M0.st == S_DASH) intent = 3;
+            const i64 x = iclamp(cell_index(M0.x), 0, 63), y = iclamp(cell_index(M0.y), 0, 63);
+            a.msg = x + 64*y + 4096*intent + 16384*(o.t%4);
         }
         return a;
     }
@@ -955,11 +946,26 @@
                 mrf[k][T - 1] = 0;
                 if (k == me || !is_mate(info(), k) || !o.p[k].alive) continue;
                 const i64 m = o.p[k].msg;
-                const int F = int(m >> 14);
-                if (F >= 1) {
-                    mrx[k][T - 1] = (m & 127) * 250 + 125; mry[k][T - 1] = ((m >> 7) & 127) * 250 + 125;
-                    mrf[k][T - 1] = (unsigned char)F;
-                }
+                const i64 sent = T - 1;
+                const int x = m & 63, y = (m >> 6) & 63, intent = (m >> 12) & 3;
+                if (m <= 0 || m >> 14 != sent % 4 || !info().M().inside(x,y)) continue;
+                i64 px = cell_center(x), py = cell_center(y);
+                int kind = 1;
+                // A gives a position and intent, but no aim. Estimate new event endpoints from observed facing.
+                if ((intent == 1 || intent == 3) && (T == 1 || aTime[k] != sent || aIntent[k] != intent)) {
+                    const i64 length = intent == 1 ? cf.range : cf.dash_v * cf.dash_t;
+                    px += Game::icos_face(o.p[k].face) * length / 1000;
+                    py += Game::isin_face(o.p[k].face) * length / 1000;
+                    px = iclamp(px, 0, info().M().width() - 1);
+                    py = iclamp(py, 0, info().M().height() - 1);
+                    kind = intent == 1 ? 2 : 3;
+                    px = (px / 250) * 250 + 125;
+                    py = (py / 250) * 250 + 125;
+                }
+                aTime[k] = T;
+                aIntent[k] = intent;
+                mrx[k][T - 1] = px; mry[k][T - 1] = py;
+                mrf[k][T - 1] = (unsigned char)kind;
             }
         hp[T] = o.me(); hd[T] = o.my_dart(); ha[T] = out;
         const Player M0 = o.me();
```

</details>

<details>
<summary>patches/glm_r3_xy.cpp.diff</summary>

```diff
--- evidence/glm_r3.cpp
+++ patches/glm_r3_xy.cpp
@@ -798,7 +798,7 @@
             const i64 m = o.p[k].msg;
             if (m <= 0) continue;
             const i64 F = m / 16384;
-            const i64 qx = (m / 128) % 128, qy = m % 128;
+            const i64 qx = m % 128, qy = (m / 128) % 128;
             const i64 px = qx * 250 + 125, py = qy * 250 + 125;
             if (F == 1) {
                 msgx[k] = px; msgy[k] = py; msgt[k] = t; msgok[k] = true;
```

</details>

<details>
<summary>patches/glm_xy.cpp.diff</summary>

```diff
--- evidence/glm.cpp
+++ patches/glm_xy.cpp
@@ -798,7 +798,7 @@
             const i64 m = o.p[k].msg;
             if (m <= 0) continue;
             const i64 F = m / 16384;
-            const i64 qx = (m / 128) % 128, qy = m % 128;
+            const i64 qx = m % 128, qy = (m / 128) % 128;
             const i64 px = qx * 250 + 125, py = qy * 250 + 125;
             if (F == 1) {
                 msgx[k] = px; msgy[k] = py; msgt[k] = t; msgok[k] = true;
```

</details>
