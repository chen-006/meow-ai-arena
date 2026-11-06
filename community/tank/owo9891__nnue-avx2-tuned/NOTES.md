# NNUE Vanguard·AVX2 调优版 — 思路说明

## 来源

基于社区投稿 `community/tank/lite0812__nnue-vanguard/bot.cpp`（官方 NNUE Vanguard，MIT 许可）修改。
原始作者：lite0812；本次改动由 AI 多模型协作完成，署名见 `meta.json`。

## 改动

1. **推理加速（AVX2/SSE2）**：给 NNUE 推理的累加器、L1/dense、policy/value head 增加 SSE2 与 AVX2 内核；运行时用 `__builtin_cpu_supports` 检测 CPU：有 AVX2 用 AVX2，无 AVX2 回退 SSE2，再回退标量。所有内核与标量路径做过逐位一致性自测（mismatches = 0）。
2. **搜索参数调优**：先验温度 0.9、局部搜索深度上限 14、PUCT 系数 1.0（其余保持原版）；按正式 CPU 预算截止。
3. **预算**：常规回合 47 CPU 毫秒、首回合 1800 毫秒（低于官方 50/2000 上限，留出调试与解码余量）。
4. **权重压缩**：神经网络权重用无损编码（CM+rANS）打包，使单文件满足社区 256 KB 上限（本文件 261,671 字节）；解码结果与训练导出权重逐字节一致（5 个 -O 级别复检）。

## v2：全网络 QAT 重训（3000 局教师自对弈）

- 数据：3000 局 **8192-sims 教师自对弈**（固定预算搜索当教师）→ 1,068,120 条逐 decide 样本（特征槽/累计器/教师根访问分布/终局）；
- 训练：全网络量化感知训练（embedding 3504×60 int8 + dense 96×60 int8 + policy/value/residual 头），按局切分、双侧零和正则；固定验证集 policy CE −2.0%、value MSE −0.7%；
- 实测（对上一版 v1 的镜像 A/B）：1000 图/2000 局，**53.70%**（95% CI [50.85, 55.12]）；外部非同族 bot 泛化验证未见退化。

## v3：trunk 训练修复（本轮核心）

- 诊断：v2 的"全网络重训"实际只有 heads（约 1.2% 参数）被有效更新，embedding/dense 权重变化 ≤1 LSB，trunk 几乎未训练；
- 修复：对 embedding/dense 使用 **30× 学习率**并训练 20 epochs（QAT 不变，导出仍为 int8/int16），固定验证集相对 v2：
  - policy CE **1.23523 → 1.20911（−2.1%）**、value MSE **0.72258 → 0.69409（−3.9%）**、value sign **69.75% → 71.14%**、policy top1 69.48% → 70.46%；
  - trunk 实际变化：embedding 63.4% 元素（平均 2.5 LSB）、dense 55.0%；
- 实测（对 v2 的镜像 A/B）：1000 图/2000 局，**69.47%**（95% CI [67.15, 70.95]），四段一致、两座位均大幅领先。

## 合规说明

- 只包含官方 `tank.hpp`、C++ 标准库与**编译器自带的系统头**（`<immintrin.h>` 等）。选手包 README 明确允许 `<windows.h>` 这类系统头；这里同样不依赖任何外部文件、库或运行时数据。
- 不读写文件、不联网、不启动其它进程；单线程；不保存跨对局信息（权重解码在 `init` 中、只用于本进程）。
- 无 AVX2 的评测机也能运行（自动回退 SSE2/标量）。

## 本地自测

用仓库自带脚本 `python community/check.py community/tank/owo9891__nnue-avx2-tuned`（正式模式，对 16 个固定对手各 20 局，共 320 局）验证编译与对局；摘要见 PR 描述。
