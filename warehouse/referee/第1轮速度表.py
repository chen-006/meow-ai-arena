"""第 1 轮主程速度成绩表（只看 results1.jsonl，口径同 团队计分.py 的 S1）。

  python 裁判/第1轮速度表.py      # → 赛事/结果/第1轮_速度成绩.md
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from 赛事工具 import EVENT, teams  # noqa: E402

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')

recs = [json.loads(l) for l in open(os.path.join(EVENT, '结果', 'results1.jsonl'), encoding='utf-8')]
who = {t['team']: t['主程'] for t in teams()}
loads = list(dict.fromkeys(r['workload'] for r in recs))
sp, st = {}, {}
for r in recs:
    ok = r['status'] == 'ok'
    sp.setdefault(r['entry'], {})[r['workload']] = r['baseline_cpu'] / r['cpu'] if ok and r.get('cpu') else None
    st.setdefault(r['entry'], {})[r['workload']] = r['status']
best = {w: max((sp[e][w] or 0) for e in sp) for w in loads}
rows = []
for e in sorted(sp):
    vals = [sp[e][w] for w in loads]
    s1 = sum(0.0 if not s or s <= 1 or best[w] <= 1 else 100 * math.log(s) / math.log(best[w]) for s, w in zip(vals, loads)) / len(loads)
    good = [v for v in vals if v]
    geo = math.exp(sum(math.log(v) for v in good) / len(good)) if len(good) == len(vals) else None
    rows.append((s1, e, vals, geo))
rows.sort(reverse=True)
L = ['# 第 1 轮 · 主程速度（正式评测，第一阶段隐藏负载，CPU 时间）', '',
     '加速比 = 基线 CPU ÷ 提交 CPU（各跑 3 次取中位数）。S1 = 每组 100×ln(加速比)/ln(该组最大加速比) 的平均（团队分里折算成 15 分）。', '',
     '| 名次 | 队 | 主程 | S1 | 几何平均加速 | ' + ' | '.join(loads) + ' |', '|---|---|---|---|---|' + '---|' * len(loads)]
for i, (s1, e, vals, geo) in enumerate(rows, 1):
    cells = ['%.1fx' % v if v else '✖ ' + st[e][w] for v, w in zip(vals, loads)]
    L.append('| %d | %s | %s | %.1f | %s | %s |' % (i, e, who[e], s1, '%.1fx' % geo if geo else '—', ' | '.join(cells)))
L += ['', '每组最大加速比：' + '，'.join('%s %.1fx' % (w, best[w]) for w in loads)]
out = os.path.join(EVENT, '结果', '第1轮_速度成绩.md')
open(out, 'w', encoding='utf-8', newline='\n').write('\n'.join(L) + '\n')
print('\n'.join(L))
