"""团体攻防接力赛计分：团队总分、主程榜、攻手榜。

  python 裁判/团队计分.py

输入（都在 赛事/ 下）：
  抽签结果.json
  结果/results1.jsonl     主程代码在第一阶段隐藏负载上的评测（评测.py --phase 1 --entries 赛事/主程提交）
  结果/results2.jsonl     接手代码在变更后隐藏负载上的评测（评测.py --phase 2 --entries 赛事/接手提交）
  击穿结果/矩阵.json       击穿判定.py
  结果/修复.json           修复判定.py
  盲评/汇总.json           盲评汇总.py（没有时盲评按 0 分计，报表里注明）
输出：结果/团队成绩.json、结果/团队成绩.md

各分项都换算到 0～100（2026-10-09 重赛起的口径，见 赛事/重赛记录.md；第一次比赛的版本在 提示词/第一次_存档/团队计分.py）：
  S1 主程速度   第一阶段每组：错误或不比基线快 → 0，否则 100 × ln(加速比) / ln(本期主程最大加速比)；取平均
  D  抗击穿     (5 − 被击穿家数) / 5 × 100 × S1 / 100
  A  找反例     按缺陷点（击穿结果/缺陷点归类.json）：每个点的分值 = 1 + 没找到它的测试手占比（按能测这份实现的 5 人算）；
                测试手得分 = 找到的各点分值之和 + 本队那一份的补分（其他 5 人在这份实现上的平均得分）；按本期最高分归一到 100
  C  接手正确   100 × (0.75 × 变更后隐藏负载正确率 + 0.25 × 修复率)；本队没被击穿时修复率记 1
  C′           只用于攻手榜：本队没被击穿时只看隐藏负载正确率
  S2 接手速度   变更后每组：错误 → 0，否则 100 × min(1, ln(max(加速比,1)) / ln(max(本期最大加速比, 2)))；取平均
  V  增值       只用于攻手榜。每组：r = 主程原版 CPU / 终版 CPU（同一张图、同一批事件，主程原版跑旧格式，见 结果/主程基准.jsonl）；
                终版有错 → 0，否则 100 × min(1, ln(max(r,1)) / ln(max(本期最大 r, 2)))；取平均
  M1 / M2      主程代码 / 接手终版代码的盲评（0～10 分 × 10）；裁判评到本队代码时这一票不计（盲评汇总.py）

团队总分 = 0.15 S1 + 0.10 D + 0.05 M1 + 0.20 A + 0.20 C + 0.20 S2 + 0.10 M2
主程榜     = (15 S1 + 10 D + 5 M1) / 30
攻手榜     = 0.25 A + 0.20 C′ + 0.20 S2 + 0.20 V + 0.15 M2

赛中替补（赛事/替补.json）：团队分照常计算，攻手一栏写"原选手 → 替补"。
攻手榜上这一队标"替补出战"，列在最后、不参加排名：原选手只打了第 2 轮，替补只打了第 3 轮，都不是完整成绩。
"""
import json
import math
import os
import statistics
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from 赛事工具 import EVENT, substitute, teams  # noqa: E402


def load_jsonl(path):
    with open(path, encoding='utf-8') as f:
        return [json.loads(x) for x in f if x.strip()]


def load_json(path, default=None):
    if not os.path.exists(path):
        return default
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def speed_table(recs):
    """队 -> 负载 -> 加速比（错误为 None）；多次运行取中位数。"""
    runs = defaultdict(lambda: defaultdict(list))
    for r in recs:
        s = r['baseline_cpu'] / max(r['cpu'], 0.01) if r['status'] == 'ok' else 0.0
        runs[r['entry'].split('#')[0]][r['workload']].append(s)
    out = {}
    for team, ws in runs.items():
        out[team] = {}
        for w, v in ws.items():
            m = statistics.median(v)
            out[team][w] = m if m > 0 else None
    return out


def main():
    ts = teams()
    names = [t['team'] for t in ts]
    who = {t['team']: t for t in ts}
    sp1 = speed_table(load_jsonl(os.path.join(EVENT, '结果', 'results1.jsonl')))
    sp2 = speed_table(load_jsonl(os.path.join(EVENT, '结果', 'results2.jsonl')))
    mat = load_json(os.path.join(EVENT, '击穿结果', '矩阵.json'))
    fix = load_json(os.path.join(EVENT, '结果', '修复.json'))
    blind = load_json(os.path.join(EVENT, '盲评', '汇总.json'), {})
    notes = []
    if not blind:
        notes.append('没有找到盲评结果，M1、M2 按 0 计。')

    w1 = sorted({w for t in sp1.values() for w in t})
    w2 = sorted({w for t in sp2.values() for w in t})
    best1 = {w: max([sp1[t][w] for t in sp1 if sp1[t].get(w)] + [1.0]) for w in w1}
    best2 = {w: max([sp2[t][w] for t in sp2 if sp2[t].get(w)] + [1.0]) for w in w2}

    def s1(team):
        v = []
        for w in w1:
            s = sp1.get(team, {}).get(w)
            v.append(0.0 if not s or s <= 1 or best1[w] <= 1 else 100 * math.log(s) / math.log(best1[w]))
        return sum(v) / len(v) if v else 0.0

    def s2(team):
        v = []
        for w in w2:
            s = sp2.get(team, {}).get(w)
            v.append(0.0 if not s else 100 * min(1.0, math.log(max(s, 1.0)) / math.log(max(best2[w], 2.0))))
        return sum(v) / len(v) if v else 0.0

    sub = {t: substitute(t, '攻手', 3) for t in names}

    # 找反例（缺陷点）：没有归类文件时退回"拿下家数 / 5"
    pts = load_json(os.path.join(EVENT, '击穿结果', '缺陷点归类.json'))
    attack_raw, attack_detail = {}, {}
    if pts:
        value = {p['id']: 1 + (5 - len(p['found_by'])) / 5 for p in pts['points']}
        on = {a: defaultdict(float) for a in names}          # 测试手 -> 被测队 -> 得分
        for p in pts['points']:
            for a in p['found_by']:
                on[a][p['target']] += value[p['id']]
        for a in names:
            own = sum(on[o][a] for o in names if o != a) / 5      # 本队那一份：其他 5 人在它上面的平均得分
            attack_raw[a] = sum(on[a].values()) + own
            attack_detail[a] = {'缺陷点': [p['id'] for p in pts['points'] if a in p['found_by']],
                                '自己找到的分值': round(sum(on[a].values()), 2), '本队补分': round(own, 2)}
        top = max(attack_raw.values()) or 1.0
        attack = {a: 100 * attack_raw[a] / top for a in names}
    else:
        notes.append('没有 击穿结果/缺陷点归类.json，找反例按"拿下家数 / 5"计。')
        attack = {a: mat['attack'][a] / 5 * 100 for a in names}

    # 增值：主程原版在同图旧格式负载上的 CPU（主程基准.py）÷ 终版 CPU
    lead_cpu = defaultdict(dict)
    lp = os.path.join(EVENT, '结果', '主程基准.jsonl')
    if os.path.exists(lp):
        for r in load_jsonl(lp):
            lead_cpu[r['entry']][r['workload']] = r['cpu']
    else:
        notes.append('没有 结果/主程基准.jsonl，增值按 0 计。')
    cpu2 = defaultdict(dict)
    for r in load_jsonl(os.path.join(EVENT, '结果', 'results2.jsonl')):
        if r['status'] == 'ok':
            cpu2[r['entry'].split('#')[0]][r['workload']] = max(r['cpu'], 0.01)
    ratio = {t: {w: lead_cpu[t][w] / cpu2[t][w] for w in w2 if w in lead_cpu[t] and w in cpu2[t]} for t in names}
    best_r = {w: max([ratio[t][w] for t in names if w in ratio[t]] + [1.0]) for w in w2}

    def gain(team):
        if not lead_cpu:
            return 0.0
        v = [100 * min(1.0, math.log(max(ratio[team][w], 1.0)) / math.log(max(best_r[w], 2.0))) if w in ratio[team] else 0.0
             for w in w2]
        return sum(v) / len(v) if v else 0.0

    def attacker_name(t):
        s = sub[t]
        return '%s → %s（第 3 轮替补）' % (s['原选手'], s['替补']) if s else who[t]['攻手']

    for t in names:
        if sub[t]:
            notes.append('%s 第 3 轮由 %s 替补 %s 出战：%s' % (t, sub[t]['替补'], sub[t]['原选手'], sub[t]['原因']))

    rows = {}
    for team in names:
        S1 = s1(team)
        k = mat['broken_by'][team]
        D = (5 - k) / 5 * 100 * S1 / 100
        A = attack[team]
        correct = sum(1 for w in w2 if sp2.get(team, {}).get(w)) / len(w2) if w2 else 0.0
        f = fix.get(team, {'total': 0, 'fixed': 0})
        fr = f['fixed'] / f['total'] if f['total'] else 1.0
        C = 100 * (0.75 * correct + 0.25 * fr)
        S2 = s2(team)
        M1 = 10 * blind.get(team, {}).get('主程', 0.0)
        M2 = 10 * blind.get(team, {}).get('终版', 0.0)
        V = gain(team)
        C2 = 100 * correct if not f['total'] else C      # 攻手榜：没被击穿的队不白送修复率
        geo = [ratio[team][w] for w in w2 if w in ratio[team]]
        rows[team] = dict(S1=S1, D=D, A=A, C=C, C2=C2, S2=S2, M1=M1, M2=M2, V=V, broken_by=k, attack=mat['attack'][team],
                          attack_detail=attack_detail.get(team, {}),
                          gain_geomean=round(math.exp(sum(math.log(x) for x in geo) / len(geo)), 2) if geo else None,
                          correct_rate=correct, fixed='%d/%d' % (f['fixed'], f['total']),
                          team_total=0.15 * S1 + 0.10 * D + 0.05 * M1 + 0.20 * A + 0.20 * C + 0.20 * S2 + 0.10 * M2,
                          lead=(15 * S1 + 10 * D + 5 * M1) / 30,
                          attacker=0.25 * A + 0.20 * C2 + 0.20 * S2 + 0.20 * V + 0.15 * M2)

    out = {'teams': {t: {k: round(v, 2) if isinstance(v, float) else v for k, v in r.items()} for t, r in rows.items()},
           'best_speedup_phase1': best1, 'best_speedup_phase2': best2, 'gain_ratio': ratio, 'notes': notes}
    os.makedirs(os.path.join(EVENT, '结果'), exist_ok=True)
    with open(os.path.join(EVENT, '结果', '团队成绩.json'), 'w', encoding='utf-8') as f:
        json.dump(out, f, ensure_ascii=False, indent=2)

    L = ['# 团体攻防接力赛 成绩', '']
    L += ['> ' + n for n in notes] + ([''] if notes else [])
    L += ['## 团队总分', '',
          '| 名次 | 队伍 | 主程 | 攻手 | 总分 | 主程速度 | 抗击穿 | 主程盲评 | 攻击 | 接手正确 | 接手速度 | 终版盲评 |',
          '|---|---|---|---|---|---|---|---|---|---|---|---|']
    for i, t in enumerate(sorted(names, key=lambda x: -rows[x]['team_total']), 1):
        r = rows[t]
        L.append('| %d | %s | %s | %s | **%.1f** | %.0f | %.0f（被 %d 人找到反例） | %.0f | %.0f（%d 个缺陷点） | %.0f（修复 %s） | %.0f | %.0f |' % (
            i, t, who[t]['主程'], attacker_name(t), r['team_total'], r['S1'], r['D'], r['broken_by'], r['M1'],
            r['A'], len(r['attack_detail'].get('缺陷点', [])), r['C'], r['fixed'], r['S2'], r['M2']))
    L += ['', '权重：主程速度 15、抗击穿 10、主程盲评 5、攻击 20、接手正确 20、接手速度 20、终版盲评 10。', '',
          '## 主程榜（弱档）', '', '| 名次 | 主程 | 队伍 | 得分 | 速度 | 抗击穿 | 盲评 |', '|---|---|---|---|---|---|---|']
    for i, t in enumerate(sorted(names, key=lambda x: -rows[x]['lead']), 1):
        r = rows[t]
        L.append('| %d | %s | %s | **%.1f** | %.0f | %.0f | %.0f |' % (i, who[t]['主程'], t, r['lead'], r['S1'], r['D'], r['M1']))
    L += ['', '得分 = (15 × 速度 + 10 × 抗击穿 + 5 × 盲评) / 30。', '',
          '## 攻手榜（强档）', '', '| 名次 | 攻手 | 队伍 | 得分 | 找反例 | 接手正确 | 接手速度 | 增值 | 终版盲评 |', '|---|---|---|---|---|---|---|---|---|']
    ranked = sorted([x for x in names if not sub[x]], key=lambda x: -rows[x]['attacker'])
    for i, t in enumerate(ranked + [x for x in names if sub[x]], 1):
        r = rows[t]
        L.append('| %s | %s | %s | **%.1f** | %.0f | %.0f | %.0f | %.0f（平均 %s 倍） | %.0f |' % (
            '替补出战' if sub[t] else i, attacker_name(t), t, r['attacker'], r['A'], r['C2'], r['S2'], r['V'],
            r['gain_geomean'] if r['gain_geomean'] is not None else '—', r['M2']))
    L += ['', '得分 = 0.25 找反例 + 0.20 接手正确 + 0.20 接手速度 + 0.20 增值 + 0.15 终版盲评。增值看同一批数据上终版比主程原版快多少倍（括号里是各组的几何平均）。']
    if any(sub.values()):
        L.append('标"替补出战"的一队：攻击分属于原选手（第 2 轮），其余三项属于替补（第 3 轮），不参加排名。')
    with open(os.path.join(EVENT, '结果', '团队成绩.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(L) + '\n')
    print('\n'.join(L))


if __name__ == '__main__':
    main()
