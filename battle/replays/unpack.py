#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把本目录的 .wrp.xz 解压成 .wrp（放在同一目录），并按 sha256.json 核对和比赛时的回放逐字节相同。

用法：python replays/unpack.py [名字里的字 ...]      例：python replays/unpack.py L2 D0_sol
只用 Python 标准库。解压出来的 .wrp 不入库（.gitignore）。
"""
import hashlib
import json
import lzma
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    want = sys.argv[1:]
    sums = json.loads((HERE / "sha256.json").read_text(encoding="utf-8"))
    n = bad = 0
    for p in sorted(HERE.glob("*.wrp.xz")):
        name = p.name[:-3]
        if want and not any(w in name for w in want):
            continue
        data = lzma.decompress(p.read_bytes())
        if hashlib.sha256(data).hexdigest() != sums.get(name):
            print("哈希不对：", name)
            bad += 1
        (HERE / name).write_bytes(data)
        n += 1
    print(f"解压 {n} 份到 {HERE}" + (f"，{bad} 份哈希不对" if bad else "，哈希全部和比赛时相同"))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
