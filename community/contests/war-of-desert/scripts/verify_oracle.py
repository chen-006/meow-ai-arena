#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""校验 engine/oracle/ 快照与 MANIFEST.json 的 SHA-256 / 字节数是否一致。

背景：Dev/MVP 源仓库已于 2026-11-13 解冻，可以继续改规则；正式赛仍然只读
engine/oracle/ 这份字节快照。快照"有没有被就地改动"必须能一条命令查出来，
否则第 1 季结果（rulesHash=906f1b14）的复现性只是口头承诺。

用法（在本目录执行）：
    python -X utf8 scripts/verify_oracle.py
退出码：0 = 全部一致；1 = 有缺失或不一致。
"""
import hashlib
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "engine", "oracle", "MANIFEST.json")


def main():
    with open(MANIFEST, encoding="utf-8") as fh:
        manifest = json.load(fh)

    oracle_dir = os.path.join(ROOT, "engine", "oracle")
    bad = 0
    for name, meta in manifest.get("files", {}).items():
        path = os.path.join(oracle_dir, name)
        if not os.path.exists(path):
            print("MISSING %s" % name)
            bad += 1
            continue
        with open(path, "rb") as fh:
            raw = fh.read()
        digest = hashlib.sha256(raw).hexdigest()
        same_hash = digest == meta["sha256"]
        same_size = len(raw) == meta["bytes"]
        ok = same_hash and same_size
        print("%s %-14s %7d B  %s" % ("OK  " if ok else "FAIL", name, len(raw), digest[:16]))
        if not ok:
            print("     期望 %7d B  %s" % (meta["bytes"], meta["sha256"][:16]))
            bad += 1

    # 攻守变体是独立文件，哈希记在 MANIFEST_siege.json；同样按字节校验。
    siege = os.path.join(oracle_dir, "MANIFEST_siege.json")
    if os.path.exists(siege):
        with open(siege, encoding="utf-8") as fh:
            sm = json.load(fh)
        path = os.path.join(oracle_dir, "rules_siege.js")
        raw = open(path, "rb").read() if os.path.exists(path) else b""
        digest = hashlib.sha256(raw).hexdigest()
        ok = bool(raw) and digest == sm.get("sha256") and len(raw) == sm.get("bytes")
        print("%s %-14s %7d B  %s" % ("OK  " if ok else "FAIL", "rules_siege.js", len(raw), digest[:16]))
        if not ok:
            print("     期望 %7d B  %s" % (sm.get("bytes", -1), str(sm.get("sha256"))[:16]))
            bad += 1

    print()
    if bad:
        print("ORACLE_SNAPSHOT_FAIL (%d 个文件不一致)" % bad)
        return 1
    print("ORACLE_SNAPSHOT_OK  %s" % manifest.get("oracleCommit", "")[:8])
    return 0


if __name__ == "__main__":
    sys.exit(main())
