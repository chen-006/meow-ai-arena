#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""编译指挥程序。

用法：
  python build.py                       编译 submission/commander.cpp → build/commander.exe
  python build.py 某个.cpp              编译别的文件 → build/<文件名>.exe
正式比赛用的编译命令和这里完全一样（见 COMMANDER.md）。
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD = HERE / "build"
HEADER = HERE / "commander" / "commander.hpp"
FLAGS = ["-O2", "-std=c++17", "-static"]


def find_compiler():
    """按顺序找 g++：环境变量 CXX、PATH、本机常见的 w64devkit 位置。"""
    cxx = os.environ.get("CXX") or shutil.which("g++")
    if cxx:
        return cxx
    bases = list(HERE.parents) + [Path.home() / "Desktop" / "测试"]
    for base in bases:
        guess = base / "00_项目管理" / "联调" / "toolchain" / "w64devkit" / "bin" / "g++.exe"
        if guess.is_file():
            return str(guess)
    for guess in (Path("C:/w64devkit/bin/g++.exe"), Path.home() / "w64devkit" / "bin" / "g++.exe"):
        if guess.is_file():
            return str(guess)
    return None


def exe_for(src):
    """submission/commander.cpp → build/commander.exe；别的文件名后面带路径的短哈希，同名不同路径的源码不会互相覆盖。"""
    src = Path(src).resolve()
    if src == (HERE / "submission" / "commander.cpp").resolve():
        return BUILD / "commander.exe"
    import hashlib
    h = hashlib.sha1(str(src).lower().encode("utf-8")).hexdigest()[:6]
    return BUILD / f"{src.stem}_{h}.exe"


def needs_build(src, exe):
    if not exe.is_file():
        return True
    t = exe.stat().st_mtime
    return Path(src).stat().st_mtime > t or HEADER.stat().st_mtime > t


def build(src, quiet=False):
    """编译 src，返回 (exe 路径, 错误信息)。exe 比源码新就不重编。"""
    src = Path(src)
    if not src.is_file():
        return None, f"找不到 {src}"
    exe = exe_for(src)
    if not needs_build(src, exe):
        return exe, None
    cxx = find_compiler()
    if not cxx:
        return None, "没有找到 g++（试过环境变量 CXX、PATH 和常见的 w64devkit 位置）。请把这个情况写进 notes.md。"
    BUILD.mkdir(exist_ok=True)
    tmp = exe.with_suffix(".tmp.exe")
    cmd = [cxx] + FLAGS + ["-I", str(HEADER.parent), str(src), "-o", str(tmp)]
    if not quiet:
        print(f"编译 {src.name} …", flush=True)
    p = subprocess.run(cmd, capture_output=True)
    out = (p.stdout + p.stderr).decode("utf-8", "replace").strip()
    if p.returncode != 0 or not tmp.is_file():
        if tmp.exists():
            tmp.unlink()
        return None, "编译失败：\n" + out[-4000:]
    if exe.exists():
        exe.unlink()
    tmp.rename(exe)
    if out and not quiet:
        print(out[-2000:])
    return exe, None


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    src = sys.argv[1] if len(sys.argv) > 1 else str(HERE / "submission" / "commander.cpp")
    exe, err = build(src)
    if err:
        print(err)
        return 1
    print(f"好了：{exe.relative_to(HERE) if exe.is_relative_to(HERE) else exe}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
