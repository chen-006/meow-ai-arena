[English](README_en.md)

# 社区比赛 · RMUC 2026 机器人 6v6

让每个模型编写一个 Python 程序，控制一整支 **6v6** 机器人队伍。模型决定英雄火力、工程装配、步兵分工、补给与空中支援，程序在带有坡道、隧道和遮挡的物理场地上自主比赛。

这是由 **RM-Arena 发起的社区自建、非官方比赛**，基于 [Arena-1](https://github.com/RM-Arena/Arena-1)，不属于「AI 算法对抗大赛」的官方集数，也不代表 RoboMaster 赛事方。这里提供独立的裁判、规则、选手包、两个公开示例选手、挑战与循环赛脚本，以及示例赛的静音回放。

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

主办方准备 **Python 3.12+、stable Rust/Cargo 和运行中的 Docker**。策略容器固定 Python 3.12，参赛源码应以 Python 3.12 为目标；宿主版本即使更高也不改变策略语言版本。构建、收卷、挑战和比赛入口会在源码校验及输出创建前拒绝旧版宿主，避免把合法的 3.12 语法误记成选手失败。宿主脚本只用标准库，不需要安装前端、Electron、Node、Bun 或 FastAPI。

从仓库进入本比赛目录；使用导出的选手包时，直接进入其输出目录：

```sh
cd community/contests/rmuc2026
```

以下命令均在比赛目录中运行：

```sh
python build.py --docker
# 短练习：不计排名
python arena.py --duration 30 --output practice/opening.json --replay practice/opening.html
# 完整挑战：两个示例对手 × 两个种子 × 红蓝换边 = 8 局
python challenge.py workspace/strategy.py --output practice/eval-001
# 多个 .py 策略的完整循环赛；这里用两个公开基准演示
python tournament.py bots/assault.py bots/guard.py --output practice/round-robin
```

在解释器名为 `python3` 的系统上替换命令中的 `python`。首次构建需要联网下载 Cargo 依赖和固定 Docker 基础镜像。后续策略比赛禁用容器网络。

运行中保留 `games.jsonl` 逐局记录，完成后生成成绩表。每个种子对每组对阵都换边；胜 1、平 0.5、负 0，同分并列。输出目录存在时拒绝覆盖；未实现自动续跑，防止技术判负被重跑成功覆盖。

循环赛的收卷语法检查失败不会取消整场比赛：错误队伍及其源码哈希、诊断仍保留，预定对局计技术判负，正常队伍间继续比赛；双方源码都不合法时记平。未启动仿真的判罚局标注 `simulation_started: false`，查看对应 HTML 可见诊断。源码读取、磁盘和运行环境故障仍中止评测，不会伪装为选手失误。

队伍文件名按 Unicode NFC 归一化、大小写折叠和去除末尾空格/点后必须唯一，避免在 macOS 或 Windows 上发生快照覆盖。例如 `GPT.py` 与 `gpt.py` 不可同时参赛；冲突会在创建输出目录前被拒绝。

默认使用 Docker。`--trusted-local` 仅供自己的可信代码调试，该结果总是标为不排名；不要用来执行陌生投稿。`--duration 30` 等短局也明确标为不排名。

每方策略只启动和导入一次。通过启动检查的同一进程会直接进入比赛，保留相同的初始化状态；构造比赛时不重新导入。源码哈希、种子、后端和资源预算须与该场匹配。选手启动失败照常判负；宿主故障中止时，也会清理已启动的另一方进程。

## 交给模型开发

使用统一 [prompt.md](prompt.md)，从 [workspace/strategy.py](workspace/strategy.py) 模板改进，交付单个 Python 源文件。两份公开陪练的来源和差异见 [SOURCE.json](SOURCE.json)，所有参赛者都可见。

生成独立选手包：

```sh
python make_player_pack.py --output player-pack
```

输出目录直接包含 `arena.py`、`runtime/`、`workspace/` 等完整材料，进入该目录后可使用相同命令。导出排除构建缓存和练习产物。开发计时和收卷由社区比赛组织者统一管理，本包不需要本地 HTTP 服务或私人凭证。可先用统一 90 分钟开发窗口做小规模试赛，再决定本次社区比赛的队伍名单和种子数。

## 验证

```sh
python -B -m unittest discover -s . -p 'test*.py' -v
# 构建沙箱后，额外执行真实 Docker 故障回归：
RMUC_DOCKER_TESTS=1 python -B -m unittest discover -s . -p 'test*.py' -v
```

PowerShell 中先执行 `$env:RMUC_DOCKER_TESTS='1'`，再运行上面的 `python -B -m unittest ...` 命令。

检查覆盖固定阵容、源码指纹、故障计分、进程与时限、路径和文件名冲突，以及手册 CLI 和独立选手包。全部测试可本地运行；本目录不配置仓库 workflow。验证范围见 [CONDITIONS.md](CONDITIONS.md)。本包没有新的多模型能力榜单；示例赛只用来验证流程。

## 来源和许可

运行时从 Arena-1 的固定提交抽取，逐文件哈希见 [SOURCE.json](SOURCE.json)；核心物理规则未修改。执行器与 worker 另有本 PR 的输入时限、启动握手和进程复用补丁，清单同时保留原文件哈希与当前哈希，并在新结果中记录 `runtime_revision`。源码中含共享的 2027 分支，本包比赛入口只使用 2026。

本目录适用 [Arena-1 非商业学习许可证](LICENSE)，**不由上层 MIT 许可重新授权**；第三方依赖仍按各自许可。详见 [第三方说明](THIRD_PARTY_NOTICES.md)。不含音乐、音效、录音、视频、GLB、纹理或原版赛事规则 PDF，也没有音频下载入口。

本社区比赛由 RM-Arena 维护，Codex 协助整理与实现比赛适配。
