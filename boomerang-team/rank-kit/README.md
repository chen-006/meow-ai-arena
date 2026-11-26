# 回旋镖大作战 1v1 排位赛本地选手包

本目录收录网站排位赛（`boomerang-1v1`）发放给观众的本地选手包源文件，用于在 Windows 或 Linux 本机编写、编译、与弱/中/强三档官方陪练对打，并用离线网页查看回放。

> 开源仓库直接放源文件目录（不提交 `kit.zip` 和编译产物 `.exe`）；需要打包分发 `kit.zip` 时，运行 `python scripts/pack_kit.py` 即可生成。

## 目录结构

| 路径 | 说明 |
|---|---|
| [`player.cpp`](player.cpp) | C++17 入门模板（上传排位赛时只提交改好的单个 `.cpp` 文件） |
| [`dart.hpp`](dart.hpp) | 规则引擎与输入输出协议单头文件（在 `../workspace/dart.hpp` 基础上补了 Linux `clock_gettime(CLOCK_THREAD_CPUTIME_ID)` 线程计时，物理逻辑逐位一致） |
| [`arena.cpp`](arena.cpp) | 跨平台 1v1 排位赛裁判源码（Windows / Linux 均可直接用 `g++` 编译） |
| [`play.py`](play.py) | 一键编译选手程序、陪练和裁判，并打完一局、写出回放和 `result.json` |
| [`config.json`](config.json) | 排位赛 6 张官方地图、陪练档位、正式编译参数与资源上限 |
| [`sparring/`](sparring/) | 弱（`weak.cpp`）、中（`mid.cpp`）、强（`strong.cpp`）三档陪练源码 |
| [`maps/`](maps/) | 6 张 1v1 排位官方地图（`twinlakes`、`yinyang`、`stonecircle`、`skybridge`、`ruins`、`icefloes`） |
| [`viewer.html`](viewer.html) | 离线回放查看器（与 `../workspace/viewer.html` 相同，浏览器直接打开即可） |
| [`scripts/patch_dart.py`](scripts/patch_dart.py) | 从 `../workspace/dart.hpp` 生成 Linux 计时补丁版 `dart.hpp` 的脚本 |
| [`scripts/pack_kit.py`](scripts/pack_kit.py) | 将本目录打包为网站排位赛分发的 `kit.zip` 的脚本 |

## 快速开始

改 [`player.cpp`](player.cpp) 里的 `decide()` 决策逻辑，用 `play.py` 编译并与三档陪练对打。完整规则与协议见 [`../../boomerang/workspace/RULES.md`](../../boomerang/workspace/RULES.md)（1v1 规则）和 [`../workspace/RULES.md`](../workspace/RULES.md)。

### Windows

准备 **Python 3.10+** 和支持 **C++17** 的 GCC（推荐免安装的 [w64devkit](https://github.com/skeeto/w64devkit/releases)），确保 `python --version` 和 `g++ --version` 能在同一终端运行：

```bash
cd boomerang-team/rank-kit
python play.py --vs weak
python play.py --vs mid --map skybridge --seed 42
python play.py --vs strong --swap
```

### Linux

准备 **Python 3.10+** 和支持 **C++17** 的 GCC（Debian/Ubuntu 可运行 `sudo apt install python3 g++`）：

```bash
cd boomerang-team/rank-kit
python3 play.py --vs weak
python3 play.py --vs mid --map skybridge --seed 42
python3 play.py --vs strong --swap
```

### 常用参数

- `--vs {weak,mid,strong}`：选择陪练档位（默认 `weak`）
- `--map {twinlakes,yinyang,stonecircle,skybridge,ruins,icefloes}`：选择地图（默认 `twinlakes`）
- `--seed <整数>`：指定地图随机种子（1 到 2^64−1，相同地图和种子可完全复现开局）
- `--source <路径>`：指定另一份单文件 C++17 源码（例如测试 `../bots/` 里的模型程序）
- `--swap`：交换先手座位，让自己坐 1 号位

> `play.py` 会在本目录的 `runs/duel-.../` 下生成编译产物、回放 `replay.txt` 和结果 `result.json`。脚本在本机直接执行程序，仅用于测试你信任的源码。

## 看离线回放

用浏览器直接双击打开 [`viewer.html`](viewer.html)，拖入或选择终端输出的 `runs/duel-.../replay.txt` 即可离线逐帧回放（无需联网或本地服务器）。同目录的 `result.json` 记录了胜负、步数、每步耗时统计与故障信息。

## 排位赛编译参数与限制

与 [`config.json`](config.json) 保持一致：

- **语言**：单文件 C++17（`#include "dart.hpp"`）
- **源码上限**：**按字符计 262,144 字符**（Unicode 码位，`max_source_chars: 262144`；UTF-8 解码后字节上限 1 MiB，`max_source_bytes: 1048576`）
- **编译参数**：`g++ -std=c++17 -O2 -static -s -ftemplate-depth=128 -fconstexpr-depth=128 -fconstexpr-ops-limit=10000000 -fmax-errors=20 -fno-diagnostics-show-caret -Werror=return-type`
- **运行时限**：第 0 步（启动与初始化）CPU 预算 **1,000 ms**，每步 CPU 预算 **10 ms**；单局总 CPU 上限 **10 秒**，墙钟上限 **35 秒**
- **内存与栈**：内存上限 **512 MB**，栈上限 **64 MB**；编译限 CPU 20 秒 / 墙钟 30 秒

## 重新生成 `dart.hpp` 与打包 `kit.zip`

```bash
# 从 ../workspace/dart.hpp 重新打 Linux 计时补丁生成本目录 dart.hpp
python scripts/patch_dart.py

# 将本目录源文件打包成 kit.zip（默认输出到 ./dist/kit.zip，不入库）
python scripts/pack_kit.py
```

## 许可

本目录全部文件采用 [MIT 许可](LICENSE)。
