"""终版复测（赛后额外项目，不计入总分）的正式判定：Astra、Opus 给别队终版找的反例是否成立。

  python 裁判/终版复测判定.py

输入：赛事/终版复测/提交/<裁判>/cases/finalN/*.in（终版复测计时.sh 封存的）、赛事/终版复测/对照表.json、赛事/接手提交/<队>/src
输出：赛事/终版复测/判定.json、判定.md
规则和第 2 轮相同（见 击穿判定.py），只是：输入按新格式检查（--p2），标准答案是参考实现 裁判/参考实现/v2_变更。
要在机器空闲时跑。
"""
import json
import os
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'workspace', 'tools'))
from 赛事工具 import BUILD, EVENT, HERE, teams  # noqa: E402
from 击穿判定 import MAX_CASES, WORKERS, judge_one  # noqa: E402
import check_input  # noqa: E402
import measure  # noqa: E402

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')


def main():
    root = os.path.join(EVENT, '终版复测')
    with open(os.path.join(root, '对照表.json'), encoding='utf-8') as f:
        table = json.load(f)
    who = {t['team']: t for t in teams()}
    ref = measure.build(os.path.join(HERE, '参考实现', 'v2_变更'), os.path.join(BUILD, 'retest', 'ref2' + measure.EXE))
    exes = {}
    for code, team in table.items():
        try:
            exes[code] = measure.build(os.path.join(EVENT, '接手提交', team, 'src'), os.path.join(BUILD, 'retest', code + measure.EXE))
        except RuntimeError:
            exes[code] = None
    todo, invalid = [], []
    for judge in sorted(os.listdir(os.path.join(root, '提交'))):
        cdir = os.path.join(root, '提交', judge, 'cases')
        for code in sorted(os.listdir(cdir)) if os.path.isdir(cdir) else []:
            if code not in table:
                continue
            for f in sorted(x for x in os.listdir(os.path.join(cdir, code)) if x.endswith('.in'))[:MAX_CASES]:
                path = os.path.join(cdir, code, f)
                try:
                    with open(path, 'rb') as fh:
                        check_input.check(fh.read().decode('ascii'), p2=True)
                except (UnicodeDecodeError, check_input.Bad) as e:
                    invalid.append({'裁判': judge, '终版': code, 'file': f, 'verdict': '无效', 'why': '不合法：%s' % e})
                    continue
                todo.append((judge, code, f, path))
    with ThreadPoolExecutor(WORKERS) as ex:
        res = list(ex.map(lambda x: judge_one(ref, exes[x[1]], x[3], {'裁判': x[0], '终版': x[1], 'file': x[2]}), todo))
    res += invalid
    for r in res:
        team = table[r['终版']]
        r['队'] = team
        r['终版作者'] = '%s → %s' % (who[team]['主程'], who[team]['攻手'])
        r['why'] = r['why'].replace('基线', '参考实现')
        print('[%s → %s %s] %-40s %s  %s' % (r['裁判'], r['终版'], r['终版作者'], r['file'], r['verdict'], r['why'][:120]), flush=True)
    with open(os.path.join(root, '判定.json'), 'w', encoding='utf-8') as f:
        json.dump(res, f, ensure_ascii=False, indent=2)
    L = ['# 终版复测 · 正式判定', '', '| 裁判 | 终版 | 用例 | 判定 | 说明 |', '|---|---|---|---|---|']
    for r in sorted(res, key=lambda r: (r['队'], r['裁判'], r['file'])):
        L.append('| %s | %s（%s） | %s | %s | %s |' % (r['裁判'], r['队'], r['终版作者'], r['file'], '成立' if r['verdict'] == '击穿' else '不成立' if r['verdict'] == '守住' else '无效', r['why'][:160]))
    with open(os.path.join(root, '判定.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(L) + '\n')


if __name__ == '__main__':
    main()
