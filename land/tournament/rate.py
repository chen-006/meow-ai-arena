"""根据循环赛结果计算 Bradley-Terry 实力值和 0–100 分（单挑、混战各占一半）。

  python tournament/rate.py results/results.jsonl --entries bots --out results/ratings
  python tournament/rate.py 新结果.jsonl --entries bots --out 新评分 --frozen results/ratings.json

方法
  * 单挑和混战分开拟合。混战的每一局拆成 6 组两两比较（名次高者胜，同名次算平），
    和单挑一样当作"对局"处理。
  * 每种模式：用全部对局做 Bradley-Terry 最大似然拟合（平局算半胜），MM 迭代求解；
    每对交过手的选手额外加一局虚拟平局，避免全胜/全负时估计发散。
    θ = ln(实力)，平移使 anchor-greedy = 0。Elo 刻度 = θ × 400 / ln10。
  * 模型的 θ = 其各次运行 θ 的中位数。编译失败的运行（在 --entries 里但没有对局）按负无穷计。
  * 每种模式的分数 = 100 × (θ模型 − θ贪心) / (θ该模式最强模型 − θ贪心)，低于 0 记 0。
  * 总分 = (单挑分 + 混战分) / 2，再按比例放大，使总分最高的模型 = 100。
  * 95% 区间：bootstrap，每对选手的对局有放回重采样。
  * --frozen：固定之前 ratings.json 中各 bot 的 θ 和各项满分基准，只估计新加入的 bot；
    新模型超过原基准时，按新基准重新换算并在报表中注明。
"""
import argparse
import json
import math
import os
import random
import statistics
from collections import defaultdict

GREEDY = 'anchor-greedy'
ELO = 400 / math.log(10)
MODES = [('duel', '单挑'), ('melee', '混战')]


def model_of(bot_id):
    return bot_id.split('#')[0]


def load(path):
    games = {'duel': [], 'melee': []}  # (bot_a, bot_b, score_of_a) in canonical order a < b
    stats = {m: defaultdict(lambda: defaultdict(float)) for m in games}
    with open(path, encoding='utf-8') as f:
        for line in f:
            r = json.loads(line)
            mode = r.get('mode', 'duel')
            n = r['names']
            ranks = r.get('ranks')
            if ranks is None:  # 旧格式
                w = r['winner']
                ranks = [1, 1] if w == -1 else [1 if i == w else 2 for i in range(2)]
            for i in range(len(n)):
                for j in range(i + 1, len(n)):
                    a, b = n[i], n[j]
                    sa = 0.5 if ranks[i] == ranks[j] else float(ranks[i] < ranks[j])
                    if a > b:
                        a, b, sa = b, a, 1 - sa
                    games[mode].append((a, b, sa))
            for i, bot in enumerate(n):
                s = stats[mode][bot]
                s['games'] += 1
                s['rank'] += ranks[i]
                s['wins'] += ranks[i] == 1 and ranks.count(1) == 1
                s['points'] += 1.0 if (ranks[i] == 1 and ranks.count(1) == 1) else 0.5 if ranks[i] == 1 else 0.0
                s['area'] += r['areas'][i]
                s['timeouts'] += r['timeouts'][i]
                s['forfeits'] += bool(r['forfeit'][i])
                s['deaths'] += r['deaths'][i]
                s['kills'] += r.get('kills', [0] * len(n))[i]
    return games, stats


def fit(pairs, bots, frozen=None, iters=5000, tol=1e-8):
    """pairs: {(a, b): (games, wins_of_a)}. Returns {bot: theta}."""
    frozen = frozen or {}
    idx = {b: i for i, b in enumerate(bots)}
    n = len(bots)
    wins = [0.0] * n
    opp = [[] for _ in range(n)]  # (j, games)
    for (a, b), (g, wa) in pairs.items():
        i, j = idx[a], idx[b]
        g, wa = g + 1.0, wa + 0.5  # one virtual draw
        wins[i] += wa
        wins[j] += g - wa
        opp[i].append((j, g))
        opp[j].append((i, g))
    p = [math.exp(frozen.get(b, 0.0)) for b in bots]
    free = [i for i, b in enumerate(bots) if b not in frozen and opp[i]]
    for _ in range(iters):
        delta = 0.0
        for i in free:
            den = sum(g / (p[i] + p[j]) for j, g in opp[i])
            new = wins[i] / den
            delta = max(delta, abs(math.log(new / p[i])))
            p[i] = new
        if delta < tol:
            break
    theta = {b: math.log(p[idx[b]]) for b in bots if opp[idx[b]]}
    if not frozen and GREEDY in theta:
        shift = theta[GREEDY]
        theta = {b: t - shift for b, t in theta.items()}
    return theta


def aggregate(games):
    pairs = defaultdict(lambda: [0, 0.0])
    for a, b, sa in games:
        pairs[(a, b)][0] += 1
        pairs[(a, b)][1] += sa
    return {k: tuple(v) for k, v in pairs.items()}


def model_scores(theta, runs_by_model, tmax_floor=None):
    mt = {}
    for m, runs in runs_by_model.items():
        vals = [theta.get(r, -math.inf) for r in runs]
        mt[m] = statistics.median(vals)
    t0 = theta.get(GREEDY, 0.0)
    contenders = [t for m, t in mt.items() if not m.startswith('anchor-') and t > -math.inf]
    tmax = max(contenders) if contenders else t0
    if tmax_floor is not None:
        tmax = max(tmax, tmax_floor)
    sc = {}
    for m, t in mt.items():
        sc[m] = 0.0 if tmax <= t0 or t == -math.inf else max(0.0, 100 * (t - t0) / (tmax - t0))
    return mt, sc, tmax


def combine(mode_scores, total_floor=None):
    """mode_scores: {mode: {model: score}} -> ({model: total}, raw_best)"""
    modes = [m for m in mode_scores if mode_scores[m]]
    models = set().union(*(mode_scores[m].keys() for m in modes)) if modes else set()
    raw = {x: sum(mode_scores[m].get(x, 0.0) for m in modes) / len(modes) for x in models}
    best = max([v for x, v in raw.items() if not x.startswith('anchor-')] or [0.0])
    if total_floor is not None:
        best = max(best, total_floor)
    return {x: (100 * v / best if best > 0 else 0.0) for x, v in raw.items()}, best


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('results')
    ap.add_argument('--entries', help='参赛文件夹，用于统计编译失败的运行')
    ap.add_argument('--out', required=True, help='输出文件前缀（生成 .json 和 .md）')
    ap.add_argument('--frozen', help='冻结的旧版 ratings.json，其中 bot 的 θ 保持不变')
    ap.add_argument('--bootstrap', type=int, default=200)
    ap.add_argument('--seed', type=int, default=0)
    args = ap.parse_args()

    games, stats = load(args.results)
    active = [m for m, _ in MODES if games[m]]
    bots = {m: sorted({g[0] for g in games[m]} | {g[1] for g in games[m]}) for m in active}
    all_runs = set().union(*bots.values())
    if args.entries:
        all_runs |= {d for d in os.listdir(args.entries) if os.path.isfile(os.path.join(args.entries, d, 'bot.cpp'))}
    runs_by_model = defaultdict(list)
    for r in sorted(all_runs):
        runs_by_model[model_of(r)].append(r)

    prev = None
    if args.frozen:
        with open(args.frozen, encoding='utf-8') as f:
            prev = json.load(f)

    def frozen_of(m):
        if not prev or m not in prev.get('modes', {}):
            return {}, None
        pm = prev['modes'][m]
        return {b: t for b, t in pm['bots'].items() if b in bots[m]}, pm['theta_best']

    theta, mt, msc, tmax = {}, {}, {}, {}
    for m in active:
        fz, fmax = frozen_of(m)
        theta[m] = fit(aggregate(games[m]), bots[m], fz)
        mt[m], msc[m], tmax[m] = model_scores(theta[m], runs_by_model, fmax)
    total_floor = prev.get('total_raw_best') if prev else None
    total, total_best = combine(msc, total_floor)
    renormalized = []
    if prev:
        for m in active:
            fz, fmax = frozen_of(m)
            if fmax is not None and tmax[m] > fmax + 1e-12:
                renormalized.append(dict(MODES)[m])
        if total_floor is not None and total_best > total_floor + 1e-9:
            renormalized.append('总分')

    rng = random.Random(args.seed)
    by_pair = {m: defaultdict(list) for m in active}
    for m in active:
        for a, b, sa in games[m]:
            by_pair[m][(a, b)].append(sa)
    boot = defaultdict(lambda: defaultdict(list))  # boot[mode or 'total'][model]
    boot_rank = defaultdict(list)
    for _ in range(args.bootstrap):
        bsc, braw = {}, {}
        for m in active:
            fz, fmax = frozen_of(m)
            pairs = {}
            for k, v in by_pair[m].items():
                s = [rng.choice(v) for _ in v]
                pairs[k] = (len(s), sum(s))
            th = fit(pairs, bots[m], fz, iters=2000, tol=1e-6)
            _, bsc[m], _ = model_scores(th, runs_by_model, fmax)
            for x, rs in runs_by_model.items():  # 同分（都被截到 0）时按原始实力区分
                braw[x] = braw.get(x, 0.0) + max(th.get(r, -50.0) for r in rs)
            for x, v in bsc[m].items():
                boot[m][x].append(v)
        btot, _ = combine(bsc, total_floor)
        order = sorted((x for x in btot if not x.startswith('anchor-')), key=lambda x: (-btot[x], -braw.get(x, 0.0)))
        for x, v in btot.items():
            boot['total'][x].append(v)
        for r, x in enumerate(order, 1):
            boot_rank[x].append(r)

    def ci(v):
        if not v:
            return [None, None]
        v = sorted(v)
        return [round(v[int(0.025 * (len(v) - 1))], 1), round(v[int(0.975 * (len(v) - 1))], 1)]

    def rnd(t):
        return None if t == -math.inf else round(t, 4)

    raw = {x: sum(max(theta[m].get(r, -50.0) for r in rs) for m in active) for x, rs in runs_by_model.items()}
    models = sorted(runs_by_model, key=lambda x: (-total.get(x, 0.0), -raw[x], x))
    out = {
        'method': 'bradley-terry-mle, duel + melee (pairwise), 50/50',
        'total_raw_best': total_best,
        'renormalized_from_frozen': renormalized,
        'modes': {m: {'games': len(games[m]), 'theta_best': tmax[m],
                      'bots': {b: round(t, 6) for b, t in theta[m].items()}} for m in active},
        'models': {x: {'score': round(total.get(x, 0.0), 2), 'score_ci95': ci(boot['total'][x]),
                       'rank_ci95': ci(boot_rank[x]) if boot_rank[x] else None,
                       'modes': {m: {'score': round(msc[m].get(x, 0.0), 2), 'score_ci95': ci(boot[m][x]),
                                     'theta': rnd(mt[m].get(x, -math.inf)),
                                     'runs': {r: rnd(theta[m].get(r, -math.inf)) for r in runs_by_model[x]}}
                                 for m in active}}
                   for x in models},
    }
    os.makedirs(os.path.dirname(os.path.abspath(args.out)) or '.', exist_ok=True)
    with open(args.out + '.json', 'w', encoding='utf-8') as f:
        json.dump(out, f, ensure_ascii=False, indent=2)

    names = dict(MODES)
    lines = ['# 圈地对战 评分', '']
    lines.append('单挑 %s 局；混战 %s 局（拆成 %s 组两两比较）。Bradley-Terry 最大似然；每种模式以 anchor-greedy = 0、'
                 '该模式最强模型 = 100 换算；总分 = 两种模式平均，再使最高者 = 100。区间为 bootstrap 95%%（%d 次）。' % (
                     len(games['duel']), sum(stats['melee'][b]['games'] for b in stats['melee']) // 4 or 0,
                     len(games['melee']), args.bootstrap))
    lines.append('')
    if renormalized:
        lines += ['> 本次补测有模型超过了冻结版的满分基准（%s），已按新基准重新换算。' % '、'.join(renormalized), '']
    head = '| 排名 | 模型 | 总分 | 95% 区间 | 名次区间 | ' + ' | '.join(
        '%s分 | %s实力（Elo 刻度）' % (names[m], names[m]) for m in active) + ' |'
    lines += [head, '|' + '---|' * (5 + 2 * len(active))]
    rank = 0
    for x in models:
        if not x.startswith('anchor-'):
            rank += 1
        c = ci(boot['total'][x])
        rc = ci(boot_rank[x]) if boot_rank[x] else None
        cells = []
        for m in active:
            t = mt[m].get(x, -math.inf)
            mc = ci(boot[m][x])
            cells.append('%.1f（%s–%s）' % (msc[m].get(x, 0.0), mc[0], mc[1]))
            cells.append('编译失败' if t == -math.inf else '%.0f' % (t * ELO))
        lines.append('| %s | %s | %.1f | %s–%s | %s | %s |' % (
            rank if not x.startswith('anchor-') else '锚点', x, total.get(x, 0.0), c[0], c[1],
            '%d–%d' % (rc[0], rc[1]) if rc else '—', ' | '.join(cells)))
    for m in active:
        st = stats[m]
        lines += ['', '## %s：各 bot 统计' % names[m], '']
        if m == 'duel':
            lines += ['| bot | 局数 | 得分率 | 平均面积 | 平均被杀 | 平均击杀 | 超时 | 判负 |', '|---|---|---|---|---|---|---|---|']
        else:
            lines += ['| bot | 局数 | 第一名占比 | 平均名次 | 平均面积 | 平均被杀 | 平均击杀 | 超时 | 判负 |',
                      '|---|---|---|---|---|---|---|---|---|']
        for b in sorted(st, key=lambda b: st[b]['rank'] / st[b]['games']):
            s = st[b]
            g = s['games']
            common = '%.0f | %.1f | %.1f | %d | %d' % (s['area'] / g, s['deaths'] / g, s['kills'] / g,
                                                       s['timeouts'], s['forfeits'])
            if m == 'duel':
                lines.append('| %s | %d | %.1f%% | %s |' % (b, g, 100 * s['points'] / g, common))
            else:
                lines.append('| %s | %d | %.1f%% | %.2f | %s |' % (b, g, 100 * s['wins'] / g, s['rank'] / g, common))
    with open(args.out + '.md', 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('\n'.join(lines))


if __name__ == '__main__':
    main()
