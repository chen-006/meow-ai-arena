[English](README_en.md)

# 新玩法提案 · RMUC 2026 机器人全队对抗

让每个模型编写一个 Python 程序，控制一整支 **6v6** 机器人队伍。模型决定英雄火力、工程装配、步兵分工、补给与空中支援，程序在带有坡道、隧道和遮挡的物理场地上自主比赛。

这是基于 [Arena-1](https://github.com/RM-Arena/Arena-1) 的可运行新玩法提案，**尚未成为节目的一期，也没有新的多模型正式榜单**。这里提供固定硬件、引擎源代码、选手包、两个公开基准、挑战与循环赛脚本，以及实际示例赛的静音回放。

## 固定阵容

红蓝两方完全一致，禁止选手改硬件：

| 位置 | 型号 |
| --- | --- |
| 1 号英雄 | 1 · H42 重型英雄 |
| 2 号工程 | 201 · E-D 双臂工程 |
| 3、4、7 号步兵 | **305 · I-L 串联腿步兵**，三个位置使用同一型号 |
| 6 号空中 | 601 · A-Q 轻量四旋翼 |

配置集中在 [lineup.json](lineup.json)，策略封装时双方都锁定这套配置。保留 RMUC 2026 规则，**不是 2027 控制度玩法**：基地被毁提前结束，420 秒到时先比基地剩余血量。完整判据见 [RULES.md](workspace/RULES.md)。

## 先看示例

[示例赛说明与结果](results/README.md)。打开 `results/example/0000.html` 或 `0001.html` 即可离线看静音俯视回放，无需安装任何依赖。这些回放从同一场计分比赛采样，没有另跑一局冒充原结果。

这是每秒采样的 2D 回放，不含逐帧动作校验或 3D 素材；原 Arena-1 应用的 3D 观战展示可参见[项目文档](https://rm-arena.github.io/Arena-1/)。

## 从源码开赛

主办方准备 **Python 3.10+、stable Rust/Cargo 和运行中的 Docker**。策略容器固定 Python 3.12；Python 宿主脚本只用标准库，不需要安装前端、Electron、Node、Bun 或 FastAPI。

从仓库根目录运行：

```sh
python rmuc2026/build.py --docker
# 短练习：不计排名
python rmuc2026/arena.py --duration 30 --replay rmuc2026/practice/opening.html
# 完整挑战：两个示例对手 × 两个种子 × 红蓝换边 = 8 局
python rmuc2026/challenge.py rmuc2026/workspace/strategy.py --output rmuc2026/practice/eval-001
# 多个 .py 策略的完整循环赛；这里用两个公开基准演示
python rmuc2026/tournament.py rmuc2026/bots/assault.py rmuc2026/bots/guard.py --output rmuc2026/practice/round-robin
```

在解释器名为 `python3` 的系统上替换命令中的 `python`。首次构建需要联网下载 Cargo 依赖和固定 Docker 基础镜像。后续策略比赛禁用容器网络。

运行中保留 `games.jsonl` 逐局记录，完成后生成成绩表。每个种子对每组对阵都换边；胜 1、平 0.5、负 0，同分并列。输出目录存在时拒绝覆盖；未实现自动续跑，防止技术判负被重跑成功覆盖。

循环赛的收卷语法检查失败不会取消整场比赛：错误队伍及其源码哈希、诊断仍保留，预定对局计技术判负，正常队伍间继续比赛；双方源码都不合法时记平。未启动仿真的判罚局标注 `simulation_started: false`，查看对应 HTML 可见诊断。源码读取、磁盘和运行环境故障仍中止评测，不会伪装为选手失误。

默认使用 Docker。`--trusted-local` 仅供自己的可信代码调试，该结果总是标为不排名；不要用来执行陌生投稿。`--duration 30` 等短局也明确标为不排名。

## 交给模型开发

使用统一 [prompt.md](prompt.md)，从 [workspace/strategy.py](workspace/strategy.py) 模板改进，交付单个 Python 源文件。两份公开陪练的来源和差异见 [SOURCE.json](SOURCE.json)，所有参赛者都可见。

生成独立选手包：

```sh
python rmuc2026/make_player_pack.py --output rmuc2026/player-pack
```

它包含比赛所需源码与公开材料，排除构建缓存和练习产物。开发计时由组织者统一管理，本包不需要本地 HTTP 服务或私人凭证。建议先统一 90 分钟、小规模试赛，再决定模型名单和正式种子数。

## 验证

```sh
python -B -m unittest discover -s rmuc2026 -p 'test*.py' -v
# 构建沙箱后，额外执行真实 Docker 故障回归：
RMUC_DOCKER_TESTS=1 python -B -m unittest discover -s rmuc2026 -p 'test*.py' -v
```

这些检查覆盖实际引擎中的双方串联腿阵容、源文件指纹、冻结后篡改、故障计分、回放 HTML 转义，以及不读 stdin、容器启动失败、输出路径冲突、含错误源码的循环赛和停用时机。只读权限的 Ubuntu CI 构建引擎和沙箱后运行全部测试。完整示例赛、真实容器错误注入和运行条件见 [CONDITIONS.md](CONDITIONS.md)。构建、回放生成与单局是否可运行，不代表跨机器性能或模型能力排名。

## 来源和许可

运行时从 Arena-1 的固定提交抽取，逐文件哈希见 [SOURCE.json](SOURCE.json)；核心物理规则未修改。执行器与 worker 另有本 PR 的输入时限和启动握手补丁，清单同时保留原文件哈希与当前哈希，并在新结果中记录 `runtime_revision`。源码中含共享的 2027 分支，本包比赛入口只使用 2026。

本目录适用 [Arena-1 非商业学习许可证](LICENSE)，**不由上层 MIT 许可重新授权**；第三方依赖仍按各自许可。详见 [第三方说明](THIRD_PARTY_NOTICES.md)。不含音乐、音效、录音、视频、GLB、纹理或原版赛事规则 PDF，也没有音频下载入口。

本提案由 RM-Arena 提交，Codex 协助整理与实现比赛适配；独立于 RoboMaster 赛事方。
