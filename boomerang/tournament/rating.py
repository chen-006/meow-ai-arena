"""A11 海选个人榜：Bradley-Terry 实力值（2026-11-16 起替代"两项名次相加"和 rank_uncertainty.py）。

口径（和规则第 11 节一致）：
- 一对一实力值：全部一对一小局，胜 1、平 0.5、负 0。
- 四人局实力值：每一小局里四个人两两比较这局的得分（名次分 + 击杀分），高的算赢、相同算平，一局 6 组比较；用全部比较拟合。
- 拟合用 bt.py（A07 足球的现成实现）：每对交过手的加一局虚拟平局、刻度 400/ln10（差 400 ≈ 10:1）。
- 零点：十二位选手的平均 = 0（两项各自归零）。
- 个人实力值 = (一对一实力值 + 四人局实力值) / 2，高的靠前；相同先比一对一实力值，仍相同按固定抽签 lot。
  "相同"按四舍五入到 1e-6 比，避免浮点尾数决定名次。
- 区间：按地图整块重抽样。一对一和四人局各自在本项内部按地图重抽（图数不变），每次两项都重新拟合、各自归零、再平均。
  个人实力值、两项实力值、名次各取 2.5% / 97.5% 分位；相邻两名"个人实力值之差"在每次重抽样里直接算差，
  差的 95% 区间含 0（下界 ≤ 0）就注明"差距在误差内"，名次不变（没有加赛）。
- 速度：先按"地图 × 配对"汇总成小矩阵（bt.tally），重抽样只合并矩阵（bt.merge）再拟合，不再逐局重抽。
"""
from __future__ import annotations

import math
import random

import bt

ROUND = 6


def pair_rows(observations):
    """observations：[(局, {名字: 得分}), ...]。返回 (一对一行, 四人局比较行)，每行 {"map", "a", "b", "score"}。"""
    duel, ffa = [], []
    for g, values in observations:
        if g['mode'] not in (0, 1):
            continue
        names = list(values) if g['mode'] == 1 else list(g['names'])
        out = duel if g['mode'] == 0 else ffa
        for i, a in enumerate(names):
            for b in names[i + 1:]:
                va, vb = values[a], values[b]
                out.append(dict(map=g['map'], a=a, b=b, score=1.0 if va > vb else 0.5 if va == vb else 0.0))
    return duel, ffa


def _fit(by_map, maps, names, start=None, tol=1e-9):
    pairs = bt.merge(by_map, maps)
    if not pairs:
        return dict.fromkeys(names)
    return bt.fit(pairs, names, None, start=start, tol=tol)


def _combine(d, f):
    return {n: (d[n] + f[n]) / 2 if d.get(n) is not None and f.get(n) is not None else None for n in d}


def _order(names, rating, duel, lot):
    key = lambda n: (rating[n] is None, -round(rating[n] or 0.0, ROUND), -round(duel[n] or 0.0, ROUND), lot.index(n))
    return sorted(names, key=key)


def _interval(values, point):
    s = sorted(values)
    if len(s) < 20 or point is None:
        return None
    low, high = s[int(0.025 * (len(s) - 1))], s[int(math.ceil(0.975 * (len(s) - 1)))]
    return [min(low, point), max(high, point)]


def individual(observations, names, lot, seed, boot=2000):
    """返回 dict(players={名字: {...}}, ranking=[...], gaps=[...], boot=实际重抽样次数, method=...)。"""
    duel_rows, ffa_rows = pair_rows(observations)
    tallies = {'duel': bt.tally(duel_rows), 'ffa': bt.tally(ffa_rows)}
    maps = {k: sorted(v, key=str) for k, v in tallies.items()}
    point = {k: _fit(tallies[k], maps[k], names) for k in tallies}
    rating = _combine(point['duel'], point['ffa'])
    ordered = _order(names, rating, point['duel'], lot)
    start = {k: {n: v / bt.SCALE for n, v in point[k].items() if v is not None} for k in point}
    rng = random.Random(str(seed))
    samples = {k: {n: [] for n in names} for k in ('duel', 'ffa', 'rating', 'rank')}
    gaps = list(zip(ordered, ordered[1:]))
    gap_samples = {p: [] for p in gaps}
    done = 0
    for _ in range(boot if min(len(maps['duel']), len(maps['ffa'])) >= 2 else 0):
        est = {}
        try:
            for k in ('duel', 'ffa'):
                pick = rng.choices(maps[k], k=len(maps[k]))
                est[k] = _fit(tallies[k], pick, names, start=start[k], tol=1e-7)
        except ValueError:
            continue    # 抽到的图里对战关系断开（只在图极少时会发生）
        done += 1
        r = _combine(est['duel'], est['ffa'])
        for k, src in (('duel', est['duel']), ('ffa', est['ffa']), ('rating', r)):
            for n, v in src.items():
                if v is not None:
                    samples[k][n].append(v)
        for i, n in enumerate(_order(names, r, est['duel'], lot), 1):
            samples['rank'][n].append(i)
        for a, b in gaps:
            if r[a] is not None and r[b] is not None:
                gap_samples[a, b].append(r[a] - r[b])
    players = {}
    for i, n in enumerate(ordered, 1):
        rk = sorted(samples['rank'][n])
        players[n] = dict(
            rating=rating[n], rating_ci95=_interval(samples['rating'][n], rating[n]),
            duel_rating=point['duel'][n], duel_rating_ci95=_interval(samples['duel'][n], point['duel'][n]),
            ffa_rating=point['ffa'][n], ffa_rating_ci95=_interval(samples['ffa'][n], point['ffa'][n]),
            rank=i, rank_ci95=[min(i, rk[int(0.025 * (len(rk) - 1))]), max(i, rk[int(math.ceil(0.975 * (len(rk) - 1)))])] if len(rk) >= 20 else None)
    gap_out = []
    for a, b in gaps:
        d = None if rating[a] is None or rating[b] is None else rating[a] - rating[b]
        ci = _interval(gap_samples[a, b], d)
        gap_out.append(dict(higher=a, lower=b, diff=d, ci95=ci, within_error=ci is not None and ci[0] <= 0.0))
    return dict(players=players, ranking=ordered, gaps=gap_out, boot=done, boot_requested=boot,
                maps=dict(duel=len(maps['duel']), ffa=len(maps['ffa'])),
                comparisons=dict(duel=len(duel_rows), ffa=len(ffa_rows)),
                method='Bradley-Terry 实力值（bt.py，胜 1 平 0.5，每对加一局虚拟平局，刻度 400/ln10，全体选手的平均 = 0）。'
                       '一对一用全部小局；四人局每局四人两两比得分（名次分 + 击杀分），一局 6 组比较。'
                       '个人实力值 = 两项平均，高者靠前；相同先比一对一实力值，再按 draw_seed:ranking 固定抽签。'
                       f'区间：两项各自在本项内部按地图整块重抽样（图数不变），每次两项都重新拟合再平均，共 {done} 次，取 2.5%～97.5% 分位；'
                       '相邻两名个人实力值之差的区间含 0 记"差距在误差内"，名次不变、不加赛。区间只反映这些地图和这批对手下的波动。')


def main(argv=None):
    """单独出榜：python -B rating.py <赛事根目录> [--config 配置.json]

    读 <根目录>\海选比赛\赛程.json 和逐局结果 g*.json，按 host.report 的同一套代码重出
    <根目录>\海选成绩.json / 海选成绩.md（和 海选比赛 阶段末尾出榜完全相同，会覆盖这两个文件和 海选比赛\故障局.json）。
    配置默认用 <根目录>\赛事配置快照.json（计分、draw_seed、选手名单都从这里来）。"""
    import argparse, sys, time
    from pathlib import Path
    import host
    ap = argparse.ArgumentParser(description=main.__doc__)
    ap.add_argument('root'); ap.add_argument('--config')
    a = ap.parse_args(argv)
    root = Path(a.root).resolve()
    c = host.read_json(Path(a.config) if a.config else root / '赛事配置快照.json')
    c['root'] = str(root)
    plan = host.read_json(root / '海选比赛' / '赛程.json')
    t = time.time()
    host.report(c, False, plan)
    q = host.read_json(root / '海选成绩.json')
    sys.stdout.reconfigure(encoding='utf-8')
    for n in q['ranking']:
        r = q['players'][n]
        print(f"{r['rank']:>2} {n:<10} {r['rating']:+8.1f}  一对一 {r['duel_rating']:+8.1f}  四人局 {r['ffa_rating']:+8.1f}")
    print(f"用时 {time.time() - t:.1f} 秒；写入 {root / '海选成绩.json'} 和 .md")


if __name__ == '__main__':
    main()
