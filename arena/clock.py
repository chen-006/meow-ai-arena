"""查看开发用时：从解压出本文件的那一刻算起。只读，不写任何文件。"""
import time
from pathlib import Path

TARGET, LIMIT = 60, 90  # 分钟

st = Path(__file__).resolve().stat()
start = getattr(st, "st_birthtime", None) or st.st_ctime  # Windows 为创建时间；Linux 解压时新建文件的 ctime 即解压时刻
used = max(0.0, time.time() - start) / 60
if used < TARGET:
    print(f"已用 {used:.1f} 分钟，距 {TARGET} 分钟还有 {TARGET - used:.1f} 分钟（{LIMIT} 分钟强制交卷）。")
elif used < LIMIT:
    print(f"已用 {used:.1f} 分钟，超过 {TARGET} 分钟。可以继续改进，但 {LIMIT} 分钟会强制交卷，还剩 {LIMIT - used:.1f} 分钟。")
else:
    print(f"已用 {used:.1f} 分钟，已到 {LIMIT} 分钟，请立即交卷。")
