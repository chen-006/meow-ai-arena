"""校验某个赛季目录里的封存记录是否与当前源码一致。

用法：

  cd community/contests/war-of-desert
  py -3 -X utf8 scripts/verify_seal.py <赛季目录>/封存记录.json

为什么需要单独一份：`bots/seal-records/<赛季>.json` 每次 `arena build` 都会被重写
（源码哈希稳定，`binSha256` 随编译环境变），所以它不入库。赛季目录里的
`封存记录.json` 是开跑那一刻的字节快照，入库冻结，才拿得住
「这批成绩是哪份源码跑出来的」。

只比对**源码**哈希。`binSha256` 依赖编译器与平台，换台机器必然不同，
拿它做门槛只会把正常复现判成失败——它的作用是在同一台机器上确认
「执行的 exe 没被换掉」，属于 build 当时的自检，不是归档校验的对象。
"""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[3].strip())
        return 2

    seal_path = Path(argv[1])
    if not seal_path.is_absolute():
        seal_path = (Path.cwd() / seal_path).resolve()
    if not seal_path.is_file():
        print(f"读不到封存记录：{seal_path}")
        return 2

    seal = json.loads(seal_path.read_text(encoding="utf-8"))
    print(f"封存记录：{seal_path}")
    print(f"生成时间：{seal.get('generatedAt', '?')}")
    print()

    checked = 0
    failed: list[str] = []

    for rel, expected in (seal.get("shared") or {}).items():
        checked += 1
        actual = sha256(ROOT / rel) if (ROOT / rel).is_file() else None
        if actual != expected:
            failed.append(f"共用库  {rel}")

    for entry in seal.get("entries") or []:
        name = entry.get("name", "?")
        source = entry.get("source", "")
        for fname, expected in (entry.get("files") or {}).items():
            checked += 1
            rel = f"{source}/{fname}" if source else fname
            path = ROOT / rel
            actual = sha256(path) if path.is_file() else None
            if actual != expected:
                failed.append(f"{name:<9} {rel}")

    if not failed:
        print(f"源码哈希全部命中：{checked}/{checked}")
        return 0

    print(f"源码哈希命中 {checked - len(failed)}/{checked}，以下与封存记录不符：")
    for line in failed:
        print("  ·", line)
    print()
    print("改动过源码就会这样。若是有意改动，重跑 arena build 并归档新赛季；")
    print("若是无意的，用 git checkout 把源码还原。")
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
