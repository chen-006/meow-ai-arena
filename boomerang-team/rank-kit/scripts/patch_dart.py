#!/usr/bin/env python3
"""从 ../workspace/dart.hpp 生成带 Linux 线程 CPU 计时补丁的 rank-kit/dart.hpp。

仅修改 dart::thread_cpu_100ns() 的非 Windows 分支，改用 clock_gettime(CLOCK_THREAD_CPUTIME_ID)
返回纳秒级线程 CPU 时间（原版在非 Windows 返回 -1）；所有游戏物理与协议指令保持逐位一致。
"""
from __future__ import annotations

import hashlib
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_SRC = HERE.parent.parent / "workspace" / "dart.hpp"
DEFAULT_DST = HERE.parent / "dart.hpp"

INCLUDE_TARGET = """#pragma pop_macro("near")
#endif"""

INCLUDE_REPLACE = """#pragma pop_macro("near")
#else
#include <time.h>
#endif"""

CPU_TARGET = """inline i64 thread_cpu_100ns() {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return -1;
    auto ticks=[](FILETIME f) { return (u64(f.dwHighDateTime)<<32)|f.dwLowDateTime; };
    return i64(ticks(kernel)+ticks(user));
#else
    return -1;
#endif
}"""

CPU_REPLACE = """inline i64 thread_cpu_100ns() {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return -1;
    auto ticks=[](FILETIME f) { return (u64(f.dwHighDateTime)<<32)|f.dwLowDateTime; };
    return i64(ticks(kernel)+ticks(user));
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return -1;
    return i64(ts.tv_sec) * 10000000LL + i64(ts.tv_nsec) / 100LL;
#endif
}"""


def patch_header(src_path: Path = DEFAULT_SRC, dst_path: Path = DEFAULT_DST) -> str:
    raw = src_path.read_text(encoding="utf-8")
    norm = raw.replace("\r\n", "\n")
    if INCLUDE_TARGET not in norm:
        raise RuntimeError("Could not locate #ifdef _WIN32 include block in dart.hpp")
    if CPU_TARGET not in norm:
        raise RuntimeError("Could not locate thread_cpu_100ns() in dart.hpp")

    patched = norm.replace(INCLUDE_TARGET, INCLUDE_REPLACE, 1).replace(CPU_TARGET, CPU_REPLACE, 1)
    dst_path.parent.mkdir(parents=True, exist_ok=True)
    with open(dst_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(patched)
    return hashlib.sha256(patched.encode("utf-8")).hexdigest()


if __name__ == "__main__":
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_SRC
    dst = Path(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_DST
    digest = patch_header(src, dst)
    print(f"[patch_dart] wrote {dst} (sha256={digest[:16]})")
