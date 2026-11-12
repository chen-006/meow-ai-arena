[English](README_en.md)

# 模拟器源码

`war.exe` 的全部源码。比赛时选手包里只有编译好的 `war.exe`，这里补上源码；用 `python build_engine.py`（在 `battle/` 下运行）编译，产物放到 `workspace/war.exe`，选手包里的 `play.py`、`build.py` 照常用。

## 编译

需要 Windows（指挥程序的进程管理用 Windows Job Object）、g++、cmake 3.20+、ninja、git。g++ 用 [w64devkit](https://github.com/skeeto/w64devkit/releases)（比赛时是它带的 GCC 16.2.0），解压到仓库的 `tools/w64devkit/` 即可；cmake、ninja 放进 PATH 或同一个 `tools/w64devkit/bin/`。git 只在第一次下载 Jolt Physics 时用。

```text
python build_engine.py
```

第一次会把 [Jolt Physics](https://github.com/jrouwe/JoltPhysics)（MIT 许可）提交 `6a985659a781c5a196b8f34a8394b2ccf6a9e1cc` 下载到 `engine/third_party/JoltPhysics`（不入库）；已经有的话用 `--jolt 目录` 或环境变量 `JOLT_DIR` 指过去。构建目录是 `engine/build/`，第一次连 Jolt 一起编，一两分钟。

用 w64devkit 的 GCC 16.2.0 编出来的 `war.exe` 和比赛时**逐字节相同**（SHA-256 `f703bab3e9ea7175f1adce18bb38be8692bd9e8ae94fc62951597dc8097fbbc0`），脚本最后会核对。确定性靠 [src/CMakeLists.txt](src/CMakeLists.txt) 里的三项设置：Jolt 的 `CROSS_PLATFORM_DETERMINISTIC`、主程序显式 `-O2`、`-std=c++17` 加 `-ffp-contract=off`（不让编译器合成乘加）。

## 源码

| 文件 | 内容 |
|---|---|
| `src/defs.hpp` | 兵种数值表、规则常数（都能用 `war.exe --cfg 名字=值` 临时改） |
| `src/scenario.*` | 读地图、关卡、布阵文件，布阵校验（不依赖 Jolt） |
| `src/nav.*` | 半米格子、A* 寻路、连通块 |
| `src/sim.*` | 一场仗：Jolt 刚体物理、兵种行为、伤害、道具、镇山兽、胜负、回放、指挥程序协议 |
| `src/host.*` | 指挥程序进程管理（Job Object、管道、CPU 计时、故障处理） |
| `src/main.cpp` | 命令行 `run / check / batch / info / nav` |
| `src/json.hpp` | 很小的只读 JSON 解析器 |
| `src/CMakeLists.txt` | 构建配置 |

指挥程序的头文件是 [workspace/commander/commander.hpp](../workspace/commander/commander.hpp)。

## 文档

- [接口说明.md](接口说明.md)：命令行、结果 JSON、布阵文件、指挥程序协议、关卡文件字段。文中的 `关卡\` 目录就是选手包的 `workspace/levels/`。
- [回放格式.md](回放格式.md)：`.wrp` 回放的二进制格式，渲染器就是按它读的。

## 和比赛时的差别

改了三行，编译结果不变（已核对逐字节相同）：`src/host.hpp` 开头两行注释删掉了提到节目另一集内部文件的说法；`src/CMakeLists.txt` 里 `JOLT_DIR` 的默认值从主办方电脑上的路径改成 `engine/third_party/JoltPhysics`。
