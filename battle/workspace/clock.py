#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""看比赛用时：python clock.py（只读，不写任何文件）。

时间来源：同目录的 clock.json。主办方开赛时会在里面写好开始时刻（start，Unix 秒）；
如果 start 是 null，就从 clock.json 在本机被创建（解压出来）的那一刻算起。
"""
import json
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent


def born(path):
    st = Path(path).stat()
    return getattr(st, "st_birthtime", None) or st.st_ctime


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    p = HERE / "clock.json"
    try:
        d = json.loads(p.read_text(encoding="utf-8-sig"))
    except Exception:
        print("找不到比赛时间信息（clock.json）。请把这个情况写进 notes.md。")
        return
    minutes = float(d.get("minutes", 90))
    start = d.get("start") or born(p)
    used = max(0.0, time.time() - float(start)) / 60
    left = minutes - used
    if left > 0:
        print(f"已用 {used:.1f} 分钟，还剩 {left:.1f} 分钟（总共 {minutes:g} 分钟）。")
    else:
        print(f"已用 {used:.1f} 分钟，{minutes:g} 分钟已到：请停止修改，回复\"交卷\"。")


if __name__ == "__main__":
    main()
