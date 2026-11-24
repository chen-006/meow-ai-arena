"""看比赛用时：python clock.py

只读，不写任何文件。开始和截止时刻由主办方在开赛时写进本目录的 clock.json，
本脚本只是把它换算成"已用多少、还剩多少"。改动 clock.json 只会改变你自己看到的数字，
收卷仍按主办方记录的时刻进行。
"""
import json
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
CLOCK = HERE / 'clock.json'


def hm(t):
    return time.strftime('%H:%M:%S', time.localtime(t))


def mins(seconds):
    return f'{seconds / 60:.1f}'


def message(d, now):
    stage = d.get('stage', '本阶段')
    if stage == '等待开发':
        return '主办方尚未开始开发计时，请等本阶段时钟更新后再开始。'
    if stage == '赛后复盘':
        return '比赛已经结束，现在是赛后复盘时间。'
    start = float(d['start'])
    deadline = float(d['deadline'])
    used = max(0.0, now - start)
    total = deadline - start
    lines = [f'{stage}：{hm(start)} 开始，{hm(deadline)} 截止，共 {mins(total)} 分钟。']
    if now < deadline:
        lines.append(f'已用 {mins(used)} 分钟，还剩 {mins(deadline - now)} 分钟。')
        lines.append(f'{d.get("submit", "交卷文件")} 以截止时刻主办方采集到的版本为准。请在截止前一分钟保存好最终版本，截止前最后几秒的改动不保证被收到。')
    else:
        lines.append(f'时间已到（已用 {mins(used)} 分钟）。{d.get("submit", "交卷文件")} 以截止时刻主办方采集到的版本为准。')
    return '\n'.join(lines)


def main():
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except (AttributeError, ValueError):
        pass
    try:
        d = json.loads(CLOCK.read_text(encoding='utf-8-sig'))
        text = message(d, time.time())
    except (OSError, ValueError, KeyError, TypeError):
        text = ('没有找到计时信息（本目录的 clock.json）。正式比赛开赛时主办方会把它放进来；'
                '请等主办方确认本阶段已开始并更新时钟。')
    print(text)


if __name__ == '__main__':
    main()
