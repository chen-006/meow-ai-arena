"""查看本轮已用时间：python3 clock.py（只有 python 时用 python clock.py）

本轮从你解压比赛包的那一刻开始计时；主办方按同一个时刻计时，并在 45 分钟时封存你的提交。
本脚本只读，不写任何文件。
"""
import json
import time
from datetime import datetime
from pathlib import Path

LIMIT = 45                              # 分钟：到点强制停止。提前结束没有加分
MARKER = '__MARKER__'                   # 主办方记下的开赛时间（解压后几秒内写入你的比赛文件夹）
SUBMIT = '__SUBMIT__'                   # 本轮以什么为提交
FINISH = '__FINISH__'                   # 收尾时该做的事

here = Path(__file__).resolve()
start = None
for d in here.parents:
    m = d / MARKER
    if m.is_file():
        try:
            start = float(json.loads(m.read_text(encoding='utf-8'))['start_unix'])
            break
        except (OSError, ValueError, KeyError):
            pass
if start is None:                       # 主办方的记录还没写进来：用本文件的解压时间，两者相差不超过几秒
    st = here.stat()
    start = getattr(st, 'st_birthtime', None) or st.st_ctime

used = max(0.0, time.time() - start) / 60
print('现在：%s' % datetime.now().strftime('%Y-%m-%d %H:%M:%S'))
print('本轮开始：%s（解压比赛包的时刻）' % datetime.fromtimestamp(start).strftime('%H:%M:%S'))
print('已用 %.1f 分钟 | 上限 %d 分钟，到点强制停止（还剩 %.1f 分钟） | 提前结束没有加分' % (used, LIMIT, max(0.0, LIMIT - used)))
if used >= LIMIT:
    print('已到强制停止时间：%s就是你的提交。' % SUBMIT)
elif used >= LIMIT - 5:
    print('只剩不到 5 分钟：请收尾，%s。' % FINISH)
