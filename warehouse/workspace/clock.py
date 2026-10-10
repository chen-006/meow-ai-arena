"""查看本轮已用时间：python3 clock.py（只有 python 时用 python clock.py）

开赛时间由主办方在你的比赛文件夹里写入（文件名见下面的 MARKER）。本脚本从自己所在的目录开始，
逐级往上找这个文件，所以解压到哪一层都能找到。只读，不写任何文件。
"""
import json
import time
from datetime import datetime
from pathlib import Path

TARGET, LIMIT = 30, 45                  # 分钟：建议用时、强制停止
MARKER = '.clock_start.json'            # 第 2、3 轮的包里各自换成本轮的文件名
SUBMIT = '当前的 src/'                   # 本轮以什么为提交
FINISH = '确认输出正确、写好 src/HANDOFF.md'  # 收尾时该做的事

here = Path(__file__).resolve()
start, found = None, None
for d in here.parents:
    m = d / MARKER
    if m.is_file():
        try:
            start = float(json.loads(m.read_text(encoding='utf-8'))['start_unix'])
            found = m
            break
        except (OSError, ValueError, KeyError):
            pass

if start is None:
    st = here.stat()
    start = getattr(st, 'st_birthtime', None) or st.st_ctime
    print('！！！注意：没有找到主办方写入的开赛时间（%s），下面暂按本文件的解压时间计算，可能偏短。' % MARKER)
    print('！！！强制停止以主办方的计时为准，请留出余量。')

used = max(0.0, time.time() - start) / 60
print('现在：%s' % datetime.now().strftime('%Y-%m-%d %H:%M:%S'))
print('本轮开始：%s（%s）' % (datetime.fromtimestamp(start).strftime('%H:%M:%S'), '主办方开赛时间' if found else '解压时间'))
print('已用 %.1f 分钟 | 建议 %d 分钟内完成（还剩 %.1f 分钟） | %d 分钟强制停止（还剩 %.1f 分钟）' % (
    used, TARGET, max(0.0, TARGET - used), LIMIT, max(0.0, LIMIT - used)))
if used >= LIMIT:
    print('已到强制停止时间：%s就是你的提交。' % SUBMIT)
elif used >= TARGET:
    print('已超过建议用时：请尽快收尾，%s。' % FINISH)
