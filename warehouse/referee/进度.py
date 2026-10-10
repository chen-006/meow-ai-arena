"""看各选手的进度：已用时、事件数、有没有解压、提交物最近改动、有没有交接文档、是否已结束，以及越界报警。

  python 裁判/进度.py              # 第 1 轮（主程）
  python 裁判/进度.py --round 2    # 第 2 轮（攻手，提交物 attack/hacks）
  python 裁判/进度.py --round 3    # 第 3 轮（攻手，提交物 workspace/src）

事件数和越界报警只覆盖命令行席位（有 输出.jsonl 的）；手动席位只看文件夹里的文件。
"""
import argparse
import json
import re
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from 赛事工具 import CLOCK_ROUNDS, EVENT, contest_folder, player, teams  # noqa: E402
from 开局 import ROUNDS, find_dir  # noqa: E402

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')


def out_of_bounds(path, team, rnd):
    """在选手的完整输出里找越界痕迹：出题目录、别队或别的轮次的比赛文件夹、C:/bench、作废存档、联网工具。
    第 3 轮读本队第 2 轮的文件夹也算越界（那里有别队主程的代码）。"""
    s = open(path, encoding='utf-8', errors='replace').read().replace('\\\\', '/').replace('\\', '/').lower()
    hits = []
    if 'a02_仓储调度优化' in s:
        hits.append('出题目录')
    seen = set(re.findall(r'arena5/第(\d)轮 (队[a-f])', s))
    others = sorted({x for r, x in seen if x != team.lower()})
    if others:
        hits.append('别队文件夹 ' + ' '.join(x.upper() for x in others))
    rounds = sorted({r for r, x in seen if int(r) != rnd})
    if rounds:
        hits.append('别的轮次文件夹 第%s轮' % ','.join(rounds))
    if 'c:/bench' in s:
        hits.append('C:/bench')
    if '运行记录_作废' in s or '作废存档' in s:
        hits.append('作废存档')
    # 联网只靠提示词约束；这里只在选手真正调用联网类工具时报警（不看工具列表）
    web = set(re.findall(r'"(?:tool|name|toolname|tool_name)"\s*:\s*"(search_web|read_url_content|open_browser_url|'
                         r'browser_subagent|webfetch|websearch|web_search|web_fetch|fetch_url|fetchurl|searchweb|browser)"', s))
    if web:
        hits.append('联网工具 ' + ' '.join(sorted(web)))
    return hits


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--round', type=int, choices=[1, 2, 3], default=1)
    rnd = ap.parse_args().round
    role, _, (parent, name), _, rec_tpl = ROUNDS[rnd]
    marker_name = CLOCK_ROUNDS[rnd][0]
    now = time.time()
    for t in teams():
        team, model = t['team'], player(t, role, rnd)
        folder = contest_folder(team, role, model, rnd)
        rec = os.path.join(EVENT, '运行记录', rec_tpl.format(队=team))
        if not os.path.isdir(folder):
            print('%s %-20s 未开始' % (team, model))
            continue
        marker = os.path.join(folder, marker_name)
        if not os.path.exists(marker):
            print('%s %-20s 已准备，待开始（还没写开赛时间）' % (team, model))
            continue
        start = float(json.load(open(marker, encoding='utf-8'))['start_unix'])
        out = os.path.join(rec, '输出.jsonl')
        lines = sum(1 for _ in open(out, 'rb')) if os.path.exists(out) else 0
        sub = find_dir(folder, parent, name)
        files = [os.path.join(r, f) for r, _, fs in os.walk(sub) for f in fs] if sub else []
        last = max((os.path.getmtime(f) for f in files), default=0)
        meta = os.path.join(rec, '元数据.json')
        done = json.load(open(meta, encoding='utf-8')) if os.path.exists(meta) else None
        done = done if done and '用时分钟' in done else None  # 开局.py 开始时先写一份不含用时的元数据
        state = ('已结束 %.1f 分钟%s' % (done['用时分钟'], '（强制停止）' if done['强制停止'] else '')) if done else '进行中'
        alarm = out_of_bounds(out, team, rnd) if os.path.exists(out) else []
        extra = ('交接文档 %s' % ('有' if sub and os.path.exists(os.path.join(sub, 'HANDOFF.md')) else '无')) if name == 'src' \
            else '用例 %d 个' % sum(1 for f in files if f.endswith('.in'))
        print('%s %-20s %s | 已用 %4.1f 分 | 事件 %4d | %s | %s 最近改动 %s | %s%s' % (
            team, model, state, (now - start) / 60, lines, '已解压' if sub else '未解压', name,
            ('%.0f 秒前' % (now - last)) if last else '—', extra,
            (' | !!!越界：' + '、'.join(alarm)) if alarm else ''))


if __name__ == '__main__':
    main()
