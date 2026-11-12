#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从源码编译模拟器 war.exe，放到 workspace/war.exe（选手包里的位置）。

用法：
  python build_engine.py                 编译；缺 Jolt Physics 源码时自动下载到 engine/third_party/JoltPhysics
  python build_engine.py --jolt 目录      用已有的 Jolt Physics 源码（必须是提交 6a985659a781…）
  python build_engine.py --no-download   缺 Jolt 时只提示，不下载

需要：Windows、g++（MinGW-w64，比赛时用 w64devkit 的 GCC 16.2.0）、cmake 3.20+、ninja、git。
编译器按顺序找：环境变量 CXX、本仓库 tools/w64devkit/bin、PATH。
用 GCC 16.2.0 编出来的 war.exe 和比赛时逐字节相同（最后会打印 SHA-256 并核对）；
别的编译器不保证字节相同，结果可以用 tournament/score.py --rerun 核对。
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE / "engine" / "src"
JOLT_URL = "https://github.com/jrouwe/JoltPhysics.git"
JOLT_COMMIT = "6a985659a781c5a196b8f34a8394b2ccf6a9e1cc"
RACE_SHA256 = "f703bab3e9ea7175f1adce18bb38be8692bd9e8ae94fc62951597dc8097fbbc0"


def find_tool(name):
    """CXX 环境变量（只对 g++）→ 仓库 tools/w64devkit/bin → PATH。"""
    if name == "g++" and os.environ.get("CXX"):
        return os.environ["CXX"]
    for base in [HERE.parent / "tools" / "w64devkit" / "bin"]:
        for cand in (base / (name + ".exe"), base / name):
            if cand.is_file():
                return str(cand)
    return shutil.which(name)


def run(cmd, **kw):
    print("  $", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def get_jolt(args):
    if args.jolt:
        d = Path(args.jolt).resolve()
    elif os.environ.get("JOLT_DIR"):
        d = Path(os.environ["JOLT_DIR"]).resolve()
    else:
        d = HERE / "engine" / "third_party" / "JoltPhysics"
    if (d / "Build" / "CMakeLists.txt").is_file():
        return d
    if args.no_download or args.jolt or os.environ.get("JOLT_DIR"):
        sys.exit(f"找不到 Jolt Physics 源码：{d}\n请先：git clone {JOLT_URL} 再 git checkout {JOLT_COMMIT}")
    git = shutil.which("git")
    if not git:
        sys.exit(f"没有 git。请手动下载 Jolt Physics 提交 {JOLT_COMMIT} 的源码，解压到 {d}")
    print(f"下载 Jolt Physics（提交 {JOLT_COMMIT[:10]}）到 {d}")
    d.mkdir(parents=True, exist_ok=True)
    run([git, "init", "-q"], cwd=d)
    run([git, "fetch", "--depth", "1", JOLT_URL, JOLT_COMMIT], cwd=d)
    run([git, "checkout", "-q", "FETCH_HEAD"], cwd=d)
    return d


def main():
    ap = argparse.ArgumentParser(description="编译 war.exe")
    ap.add_argument("--jolt", help="Jolt Physics 源码目录")
    ap.add_argument("--no-download", action="store_true", help="缺 Jolt 时不自动下载")
    ap.add_argument("--build-dir", default=str(HERE / "engine" / "build"), help="构建目录（默认 engine/build）")
    args = ap.parse_args()
    if os.name != "nt":
        print("提醒：引擎的指挥程序进程管理用的是 Windows Job Object，只能在 Windows 上编译和运行。")
    cxx, cmake, ninja = find_tool("g++"), find_tool("cmake"), find_tool("ninja")
    for n, t in (("g++", cxx), ("cmake", cmake), ("ninja", ninja)):
        if not t:
            sys.exit(f"找不到 {n}。g++ 用 w64devkit（https://github.com/skeeto/w64devkit/releases），解压到仓库的 tools/w64devkit/；cmake、ninja 放进 PATH 或 tools/w64devkit/bin/。")
    jolt = get_jolt(args)
    build = Path(args.build_dir).resolve()
    build.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["PATH"] = str(Path(cxx).resolve().parent) + os.pathsep + env.get("PATH", "")
    if not (build / "build.ninja").is_file():
        run([cmake, "-G", "Ninja", str(SRC), "-DCMAKE_BUILD_TYPE=Distribution",
             f"-DCMAKE_CXX_COMPILER={cxx}", f"-DCMAKE_MAKE_PROGRAM={ninja}", f"-DJOLT_DIR={jolt.as_posix()}"],
            cwd=build, env=env)
    run([ninja, "war"], cwd=build, env=env)
    exe = build / "war.exe"
    dst = HERE / "workspace" / "war.exe"
    shutil.copy2(exe, dst)
    h = hashlib.sha256(dst.read_bytes()).hexdigest()
    print(f"\n已生成 {dst}\nSHA-256 {h}")
    if h == RACE_SHA256:
        print("和比赛时的 war.exe 逐字节相同。")
    else:
        ver = subprocess.run([cxx, "--version"], capture_output=True, text=True).stdout.splitlines()[:1]
        print(f"和比赛时的 war.exe（{RACE_SHA256[:16]}…）不同。编译器：{ver}。"
              "比赛用的是 GCC 16.2.0；别的编译器不保证字节相同，可以用 python tournament/score.py --rerun 核对结果。")


if __name__ == "__main__":
    main()
