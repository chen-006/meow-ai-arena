"""终版体检的性能部分：把第 2 轮成立的超时类反例换成新格式（新需求生效：agingEvery=50、carryCost=2），
让每份终版和参考实现各跑一次，看输出是否一致、有没有比参考实现慢很多。

  python 裁判/终版体检_性能.py

第 1 轮赛后的预审只比输出、不看用时，所以 4 个性能型缺陷全漏了；这次补上。
输出：赛事/结果/终版体检/性能探针.json、性能探针.md。要在机器空闲时跑。
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'workspace', 'tools'))
from 赛事工具 import BUILD, EVENT, HERE, teams  # noqa: E402
import check_input  # noqa: E402
import measure  # noqa: E402

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')
EXTRA = ' 50 2'
LIMIT = 60


def main():
    with open(os.path.join(EVENT, '击穿结果', '矩阵.json'), encoding='utf-8') as f:
        mat = json.load(f)['matrix']
    out_dir = os.path.join(EVENT, '结果', '终版体检')
    probe_dir = os.path.join(out_dir, '性能探针用例')
    os.makedirs(probe_dir, exist_ok=True)
    cases = []
    for a, row in mat.items():
        for target, cell in row.items():
            for c in cell['cases']:
                if c.get('kind') == '超时':
                    src = os.path.join(EVENT, '击穿提交', a, 'hacks', target, c['file'])
                    lines = open(src, encoding='ascii').read().replace('\r\n', '\n').split('\n')
                    i = next(i for i, s in enumerate(lines) if s.startswith('PARAMS '))
                    lines[i] += EXTRA
                    text = '\n'.join(lines)
                    check_input.check(text, p2=True)
                    name = '原打%s_%s_%s' % (target[-1], a[-1], c['file'])
                    path = os.path.join(probe_dir, name)
                    with open(path, 'w', encoding='ascii', newline='\n') as f:
                        f.write(text)
                    cases.append((name, target, path))
    ref = measure.build(os.path.join(HERE, '参考实现', 'v2_变更'), os.path.join(BUILD, 'retest', 'ref2' + measure.EXE))
    ts = teams()
    exes = {}
    for t in ts:
        try:
            exes[t['team']] = measure.build(os.path.join(EVENT, '接手提交', t['team'], 'src'), os.path.join(BUILD, 'retest', 'probe_' + t['team'] + measure.EXE))
        except RuntimeError:
            exes[t['team']] = None
    rows = []
    for name, target, path in sorted(cases):
        r0 = measure.run(ref, path, timeout=LIMIT)
        row = {'用例': name, '原本针对': target, '参考实现秒': round(r0['wall'], 2), '终版': {}}
        for t in ts:
            e = exes[t['team']]
            if e is None:
                row['终版'][t['team']] = '编译失败'
                continue
            r = measure.run(e, path, timeout=LIMIT)
            if r['timeout']:
                v = '超时(>%d秒)' % LIMIT
            elif r['code'] != 0:
                v = '异常退出'
            elif r['digest'] != r0['digest']:
                v = '输出不同(%.2f秒)' % r['wall']
            else:
                v = '%.2f秒' % r['wall']
            row['终版'][t['team']] = v
        rows.append(row)
        print(name, '参考 %.1f 秒 |' % r0['wall'], ' '.join('%s %s' % kv for kv in row['终版'].items()), flush=True)
    with open(os.path.join(out_dir, '性能探针.json'), 'w', encoding='utf-8') as f:
        json.dump(rows, f, ensure_ascii=False, indent=2)
    names = [t['team'] for t in ts]
    who = {t['team']: t['攻手'] for t in ts}
    L = ['# 终版体检 · 性能探针', '', '第 2 轮成立的 %d 个超时类反例，换成新格式（agingEvery=50、carryCost=2，新需求生效）后在 6 份终版上各跑一次，限时 %d 秒。' % (len(rows), LIMIT), '',
         '| 用例（原本针对哪队的主程） | 参考实现 | ' + ' | '.join('%s %s' % (n, who[n]) for n in names) + ' |', '|---|---|' + '---|' * len(names)]
    for r in rows:
        L.append('| %s | %.1f 秒 | %s |' % (r['用例'], r['参考实现秒'], ' | '.join(r['终版'][n] for n in names)))
    with open(os.path.join(out_dir, '性能探针.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(L) + '\n')


if __name__ == '__main__':
    main()
