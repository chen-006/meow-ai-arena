[简体中文](README.md)

# Simulator source

The full source of `war.exe`. The contestant package only shipped the compiled `war.exe`; the source is added here. Build it with `python build_engine.py` (run from `battle/`); the result goes to `workspace/war.exe`, where the package's `play.py` and `build.py` expect it.

## Building

Needs Windows (commander processes are managed with Windows Job Objects), g++, cmake 3.20+, ninja and git. Use [w64devkit](https://github.com/skeeto/w64devkit/releases) for g++ (the contest used its GCC 16.2.0), unpacked into the repository's `tools/w64devkit/`; put cmake and ninja on PATH or in the same `tools/w64devkit/bin/`. git is only used to download Jolt Physics the first time.

```text
python build_engine.py
```

The first run downloads [Jolt Physics](https://github.com/jrouwe/JoltPhysics) (MIT license) at commit `6a985659a781c5a196b8f34a8394b2ccf6a9e1cc` into `engine/third_party/JoltPhysics` (not committed); if you already have it, point `--jolt DIR` or the `JOLT_DIR` environment variable at it. The build directory is `engine/build/`; the first build compiles Jolt too and takes a minute or two.

Built with w64devkit's GCC 16.2.0, `war.exe` is **byte-identical** to the contest exe (SHA-256 `f703bab3e9ea7175f1adce18bb38be8692bd9e8ae94fc62951597dc8097fbbc0`); the script checks this at the end. Determinism relies on three settings in [src/CMakeLists.txt](src/CMakeLists.txt): Jolt's `CROSS_PLATFORM_DETERMINISTIC`, an explicit `-O2` for the main program, and `-std=c++17` with `-ffp-contract=off` (no fused multiply-add).

## Source

| File | Contents |
|---|---|
| `src/defs.hpp` | Unit stats and rule constants (each can be overridden with `war.exe --cfg name=value`) |
| `src/scenario.*` | Reading maps, levels and deployments; deployment validation (no Jolt dependency) |
| `src/nav.*` | Half-metre grid, A* pathfinding, connected regions |
| `src/sim.*` | One battle: Jolt rigid-body physics, unit behaviour, damage, items, the boss, win conditions, replays, commander protocol |
| `src/host.*` | Commander process management (Job Objects, pipes, CPU timing, fault handling) |
| `src/main.cpp` | Command line `run / check / batch / info / nav` |
| `src/json.hpp` | A tiny read-only JSON parser |
| `src/CMakeLists.txt` | Build configuration |

The commander header is [workspace/commander/commander.hpp](../workspace/commander/commander.hpp).

## Documents (Chinese)

- [接口说明.md](接口说明.md): command line, result JSON, deployment files, commander protocol, level file fields. Its `关卡\` folder is the package's `workspace/levels/`.
- [回放格式.md](回放格式.md): the binary `.wrp` replay format, which the renderer reads.

## Differences from the contest

Three lines changed, without changing the binary (checked byte-identical): two comment lines at the top of `src/host.hpp` that referred to another episode's internal files, and the default of `JOLT_DIR` in `src/CMakeLists.txt`, which pointed to the organizer's machine and now points to `engine/third_party/JoltPhysics`.
