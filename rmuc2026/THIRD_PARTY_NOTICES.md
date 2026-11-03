# 来源与许可范围

本目录是 Arena-1 的 RMUC 2026 新玩法提案及比赛包，保留 [Arena-1 非商业学习许可证](LICENSE)。上层仓库的 MIT 许可不替换本目录及其中 Arena-1 源码的许可。本次没有变更 Arena-1 许可。

- Python 执行器、SDK、规则配置和 Rust 引擎来自 [RM-Arena/Arena-1](https://github.com/RM-Arena/Arena-1)，固定提交及逐文件 SHA-256 见 [SOURCE.json](SOURCE.json)。其中执行器 `runner.py` 和 `worker.py` 有本 PR 的输入时限、启动握手与已就绪进程复用补丁，清单同时保留补丁说明、原文件哈希与当前哈希。2026 物理、判胜与 200 ms 决策预算未改动。引擎源代码中仍包含共享的 2027 实现，本包入口只启用 2026。
- 模板和 `bots/assault.py` 来自同仓库历史 Claude Opus 5.5 xhigh 全队基准；`guard.py` 只把 `DEFENSIVE` 改为 `True`。具体提交、原文件和哈希同样记录于 SOURCE.json。这两个文件是公开示例，不是本次新增的模型实测参赛作品。
- 本包适配层、静音俯视回放、文档和测试由用户提出要求、Codex 协助编写，随本目录许可分发。
- Rust 依赖遵守各自许可，版本及来源由 `runtime/simulation/Cargo.lock` 固定。Rapier 3D 使用 Apache-2.0；其他依赖保留其各自上游许可，不被本目录许可替换。Cargo 安装时获取依赖源码及其许可。
- Docker 的 Python 3.12 基础镜像及其系统依赖保留各自许可，镜像按摘要固定。镜像不提交到本仓库，构建时从发行方获取。

**本包不含任何音乐、录音、音效、视频文件、GLB 模型、纹理、赛事规则原版 PDF 或其他外部多媒体素材，也没有音频下载步骤。** 静音 HTML 只包含本局采样数据和自编的 SVG 俯视绘制器。

RoboMaster 及相关商标属于其权利人。本包为独立仿真项目提案，不声称代表赛事方，也不冒用官方比赛结果。
