"""汇总 3 个裁判的盲评，得到每队主程代码和终版代码的可维护性分（0～10）。

  python 裁判/盲评汇总.py

输入：赛事/盲评/对照表.json、赛事/盲评/结果/<裁判名>/<编号>.json
输出：赛事/盲评/汇总.json、汇总.md（含每份代码的"一句话点评"，可以直接做视频素材）

换算：5 个维度各 1～5 分，合计 5～25 分 → (合计 − 5) / 2，得到 0～10 分。3 个裁判各占 1/3。
回避（2026-10-09 重赛起）：裁判评到自己所在队伍的代码时（替补出战过的队伍也算，见 赛事/替补.json），这一票不计，取另外两位的平均分。
其余代码 3 个裁判各占 1/3。
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from 赛事工具 import EVENT, player, substitutes, teams  # noqa: E402

JUDGES = ['GPT-6 Astra', 'Opus 5.5', 'DeepSeek V4.1 Flash']
DIMS = ['结构与可读性', '行为保真的可见性', '可修改性', '优化逻辑的清晰度', '技术债清理']


def main():
    who = {t['team']: t for t in teams()}
    with open(os.path.join(EVENT, '盲评', '对照表.json'), encoding='utf-8') as f:
        table = json.load(f)
    raw = {}
    for code in table:
        raw[code] = {}
        for j in JUDGES:
            p = os.path.join(EVENT, '盲评', '结果', j, code + '.json')
            if not os.path.exists(p):
                sys.exit('缺少评分：%s' % p)
            with open(p, encoding='utf-8') as f:
                d = json.load(f)
            pts = [min(5, max(1, int(d[k]['分']))) for k in DIMS]
            raw[code][j] = {'score': (sum(pts) - 5) / 2, 'dims': pts, 'comment': d.get('一句话点评', '')}
    summary, notes = {}, []
    for code, info in table.items():
        team, kind = info['team'], info['kind']
        members = {who[team]['主程'], who[team]['攻手']} | {s['替补'] for s in substitutes() if s['team'] == team}
        scores = {j: raw[code][j]['score'] for j in JUDGES}
        counted = [j for j in JUDGES if j not in members]
        for j in JUDGES:
            if j in members:
                notes.append('%s 评本队 %s 的%s代码给了 %.1f，按回避规则不计（另两位平均 %.1f）' % (
                    j, team, kind, scores[j], sum(scores[k] for k in counted) / len(counted)))
        summary.setdefault(team, {})[kind] = round(sum(scores[j] for j in counted) / len(counted), 2)
        summary[team].setdefault('明细', {})[kind] = {'编号': code, '各裁判': scores,
                                                     '点评': {j: raw[code][j]['comment'] for j in JUDGES}}
    with open(os.path.join(EVENT, '盲评', '汇总.json'), 'w', encoding='utf-8') as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)
    L = ['# 盲评汇总', '', '| 队伍 | 类型 | 编号 | ' + ' | '.join(JUDGES) + ' | 最终 |',
         '|---|---|---|' + '---|' * (len(JUDGES) + 1)]
    for team in sorted(summary):
        for kind in ('主程', '终版'):
            m = summary[team]['明细'][kind]
            L.append('| %s | %s | %s | %s | **%.1f** |' % (
                team, kind, m['编号'], ' | '.join('%.1f' % m['各裁判'][j] for j in JUDGES), summary[team][kind]))
    L += [''] + ['> ' + n for n in notes] + ['', '## 一句话点评', '']
    for team in sorted(summary):
        for kind in ('主程', '终版'):
            for j, c in summary[team]['明细'][kind]['点评'].items():
                L.append('- %s %s（%s）｜%s：%s' % (team, kind, who[team]['主程'] if kind == '主程' else player(who[team], '攻手', 3), j, c))
    with open(os.path.join(EVENT, '盲评', '汇总.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(L) + '\n')
    print('\n'.join(L))


if __name__ == '__main__':
    main()
