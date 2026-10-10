"""第 3 轮结束后：检查每队的接手代码有没有修好本队被击穿的用例。

  python 裁判/修复判定.py

输入：赛事/接手提交/<队>/src，赛事/接手包/被击穿用例_第二阶段格式/<队>/*.in/.out
输出：赛事/结果/修复.json   {队: {'total': n, 'fixed': k, 'cases': {...}}}
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'workspace', 'tools'))
from 赛事工具 import BUILD, EVENT, teams  # noqa: E402
import measure  # noqa: E402


def main():
    res = {}
    for t in teams():
        team = t['team']
        cdir = os.path.join(EVENT, '接手包', '被击穿用例_第二阶段格式', team)
        cases = sorted(f[:-3] for f in os.listdir(cdir) if f.endswith('.in')) if os.path.isdir(cdir) else []
        rec = {'total': len(cases), 'fixed': 0, 'cases': {}}
        if cases:
            d = os.path.join(EVENT, '接手提交', team)
            src = os.path.join(d, 'src') if os.path.isdir(os.path.join(d, 'src')) else d
            try:
                exe = measure.build(src, os.path.join(BUILD, 'fix', team + measure.EXE))
            except RuntimeError:
                exe = None
            for c in cases:
                ok = False
                if exe:
                    r = measure.run(exe, os.path.join(cdir, c + '.in'), timeout=60)
                    with open(os.path.join(cdir, c + '.out'), 'rb') as f:
                        ok = r['code'] == 0 and not r['timeout'] and r['digest'] == measure.digest(f.read())
                rec['cases'][c] = ok
                rec['fixed'] += ok
        res[team] = rec
        print('%s：被击穿用例 %d 个，修好 %d 个' % (team, rec['total'], rec['fixed']))
    os.makedirs(os.path.join(EVENT, '结果'), exist_ok=True)
    with open(os.path.join(EVENT, '结果', '修复.json'), 'w', encoding='utf-8') as f:
        json.dump(res, f, ensure_ascii=False, indent=2)


if __name__ == '__main__':
    main()
