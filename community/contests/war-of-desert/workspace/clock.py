"""查看 Desert Arena 开发用时：从解压出本文件的那一刻算起。

只读，不写任何文件。计时由组织方控制（仓库根目录的 赛务.py，或双击 裁判台.bat）：
发卷时重置本文件的创建时间，暂停/恢复时把它同步回记录里的逻辑起点。

默认赛程：90 分钟是目标完成时间，120 分钟是强制收卷时间。
"""

from __future__ import annotations

import sys
import time
from datetime import datetime
from pathlib import Path

TARGET_MINUTES = 90
LIMIT_MINUTES = 120
HERE = Path(__file__).resolve()


def born(path: Path) -> float:
    """文件在本机被创建（也就是解压出来）的时刻。

    不能用修改时间：解压工具常会恢复打包时保存的 mtime。Windows 使用创建时间；
    Linux/macOS 新建文件的 ctime 即解压时刻。
    """
    st = Path(path).stat()
    return float(getattr(st, "st_birthtime", None) or st.st_ctime)


def local_time(ts: float) -> str:
    return datetime.fromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S")


def status(now: float | None = None, clock_file: Path = HERE) -> str:
    now = time.time() if now is None else float(now)
    start = born(clock_file)
    used = max(0.0, now - start) / 60
    target_at = start + TARGET_MINUTES * 60
    limit_at = start + LIMIT_MINUTES * 60

    if used < TARGET_MINUTES:
        headline = (
            f"已用 {used:.1f} 分钟，距 {TARGET_MINUTES} 分钟目标还有 "
            f"{TARGET_MINUTES - used:.1f} 分钟。"
        )
    elif used < LIMIT_MINUTES:
        headline = (
            f"已用 {used:.1f} 分钟，已超过 {TARGET_MINUTES} 分钟目标；"
            f"距 {LIMIT_MINUTES} 分钟强制收卷还有 {LIMIT_MINUTES - used:.1f} 分钟。"
        )
    else:
        headline = (
            f"已用 {used:.1f} 分钟，已到 {LIMIT_MINUTES} 分钟强制收卷时间；"
            "请立即停止修改并等待组织方冻结提交。"
        )

    return "\n".join(
        [
            headline,
            f"计时起点：{local_time(start)}",
            f"{TARGET_MINUTES} 分钟目标：{local_time(target_at)}",
            f"{LIMIT_MINUTES} 分钟强制收卷：{local_time(limit_at)}",
        ]
    )


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    print(status())
