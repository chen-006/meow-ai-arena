"""Bradley–Terry 实力值（1 对 1）。

口径：
- 胜记 1，平记 0.5；
- 每对交过手的程序加一局虚拟平局（全胜、全负时实力值不会趋于无穷）；
- 基准 = 0，实力强 10 倍记 +400（和 Elo 同一把尺：差 400 分 ≈ 10:1）；弱于基准是负数；
- 区间：按地图整块重抽样（同一张图上的所有对局一起抽，一起放回），每次重新拟合，取 2.5% 和 97.5% 分位；
- 并列：按实力值排好之后，相邻两名“实力值之差”的 95% 区间含 0 就算并列（并列可以连成一串）。
  差的区间是在每次重抽样里直接算差得到的。不用“两人各自的区间是否重叠”来判：各自的区间里还含着基准自身的噪声，
  两个人对基准的偏差又是一起动的，用它判并列偏保守（同样的数据，能分开的差距大约要大一倍）。

输入的一局是 {"map": 地图的标识, "a": 名字, "b": 名字, "score": a 的得分（1 / 0.5 / 0）}。
"map" 可以是任何可哈希的值；几场比赛合在一起拟合时，用 (哪一场, 图号) 这样的元组，重抽样会在每一场内部各抽各的。

A11 复制说明（2026-11-16 从 A07_足球对战\主办方\bt.py 原样复制，只改了一处）：
- fit() 的 anchor 可以传 None：这时零点 = 所有有对局的程序的平均（A11 没有基准程序参赛，十二位选手的平均定为 0）。
  传名字时行为和原件完全相同。A11 的个人榜由 rating.py 调用 tally / merge / fit，不走 rate()。
"""
from __future__ import annotations

import math
import random
from collections import defaultdict

SCALE = 400 / math.log(10)


def tally(rows):
    """按地图汇总：{地图: {(a, b): [局数, a 的得分]}}，其中 a < b。"""
    by_map = defaultdict(lambda: defaultdict(lambda: [0.0, 0.0]))
    for r in rows:
        a, b, s = r["a"], r["b"], float(r["score"])
        if a == b:
            continue
        if a > b:
            a, b, s = b, a, 1.0 - s
        cell = by_map[r["map"]][a, b]
        cell[0] += 1.0
        cell[1] += s
    return by_map


def merge(by_map, maps):
    """把若干张图（可重复）的汇总加在一起。"""
    total = defaultdict(lambda: [0.0, 0.0])
    for m in maps:
        for pair, (g, w) in by_map[m].items():
            cell = total[pair]
            cell[0] += g
            cell[1] += w
    return total


def fit(pairs, names, anchor, virtual=1.0, start=None, tol=1e-9, max_iter=200000):
    """pairs：{(a, b): [局数, a 的得分]}。返回 {名字: 实力值}；没有任何对局的名字是 None。

    virtual：每对交过手的程序加几局虚拟平局。start：上一次的解（对数实力），用来热启动。"""
    games = {}
    for (a, b), (g, w) in pairs.items():
        if g > 0:
            games[a, b] = (g + virtual, w + virtual / 2)
    active = sorted({n for pair in games for n in pair})
    if anchor is not None and anchor not in active:
        raise ValueError(f"基准 {anchor} 没有任何对局，定不了零点")
    _check_connected(games, active)
    wins = dict.fromkeys(active, 0.0)
    edges = []
    for (a, b), (g, w) in games.items():
        wins[a] += w
        wins[b] += g - w
        edges.append((a, b, g))
    p = {n: math.exp((start or {}).get(n) or 0.0) for n in active}
    for _ in range(max_iter):
        denom = dict.fromkeys(active, 0.0)
        for a, b, g in edges:
            v = g / (p[a] + p[b])
            denom[a] += v
            denom[b] += v
        new = {n: wins[n] / denom[n] for n in active}
        center = sum(math.log(v) for v in new.values()) / len(new)
        scale = math.exp(-center)
        delta = 0.0
        for n in active:
            new[n] *= scale
            delta = max(delta, abs(math.log(new[n] / p[n])))
        p = new
        if delta < tol:
            break
    origin = math.log(p[anchor]) if anchor is not None else sum(math.log(p[n]) for n in active) / len(active)
    out = dict.fromkeys(names)
    for n in active:
        out[n] = SCALE * (math.log(p[n]) - origin)
    return out


def _check_connected(games, active):
    """对战关系必须连成一片，否则几块之间的实力值没法比。"""
    parent = {n: n for n in active}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for a, b in games:
        parent[find(a)] = find(b)
    roots = {find(n) for n in active}
    if len(roots) > 1:
        raise ValueError(f"对战关系分成了 {len(roots)} 块，互相之间没有交手，没法放在一把尺上")


def rate(rows, names, anchor, boot=1000, seed=0, virtual=1.0, diffs=None, stats=None):
    """返回 {名字: {"rating", "low", "high"}}（没有对局的名字三项都是 None），以及实际重抽样的次数。

    区间按地图整块重抽样。地图标识是元组时，按元组的第一项分层（每一层内部各抽各的，层的图数不变）。
    diffs：[(a, b), ...]。给了它就多返回一项 {(a, b): {"diff", "low", "high"}}，是 a 减 b 的实力值之差和它的区间
    （在每次重抽样里算差，比两个区间相减准）。
    stats：{键: 函数}。函数收到一次拟合的结果 {名字: 实力值}，返回一个数（算不了返回 None），比如"几个程序实力值的标准差"。
    给了它就在最后多返回一项 {键: {"value", "low", "high", "sd", "n"}}：value 是用全部数据算的，low / high 是重抽样的
    95% 区间，sd 是这个数在重抽样里的标准差（也就是它的抽样误差），n 是算出来的次数。"""
    by_map = tally(rows)
    maps = list(by_map)
    point = fit(merge(by_map, maps), names, anchor, virtual)
    start = {n: v / SCALE for n, v in point.items() if v is not None}
    strata = defaultdict(list)
    for m in maps:
        strata[m[0] if isinstance(m, tuple) else None].append(m)
    rng = random.Random(seed)
    samples = defaultdict(list)
    done = 0
    for _ in range(boot if len(maps) >= 2 else 0):
        pick = []
        for group in strata.values():
            pick += rng.choices(group, k=len(group))
        try:
            est = fit(merge(by_map, pick), names, anchor, virtual, start=start, tol=1e-7)
        except ValueError:
            continue        # 这次抽到的图里基准没有出场或者对战关系断开了（只在图极少时会发生）
        done += 1
        for n, v in est.items():
            if v is not None:
                samples[n].append(v)
        for a, b in diffs or ():
            if est.get(a) is not None and est.get(b) is not None:
                samples[a, b].append(est[a] - est[b])
        for key, fn in (stats or {}).items():
            value = fn(est)
            if value is not None:
                samples["stat", key].append(value)

    def interval(key, v):
        s = sorted(samples[key])
        if len(s) < 20:
            return None, None
        low, high = s[int(0.025 * (len(s) - 1))], s[int(math.ceil(0.975 * (len(s) - 1)))]
        return min(low, v), max(high, v)

    out = {}
    for n in names:
        v = point.get(n)
        if v is None:
            out[n] = {"rating": None, "low": None, "high": None}
            continue
        low, high = interval(n, v)
        out[n] = {"rating": v, "low": low, "high": high}
    stat_out = {}
    for key, fn in (stats or {}).items():
        value = fn(point)
        s = samples["stat", key]
        low, high = interval(("stat", key), value) if value is not None else (None, None)
        sd = None
        if len(s) >= 20:
            mean = sum(s) / len(s)
            sd = math.sqrt(sum((x - mean) ** 2 for x in s) / (len(s) - 1))
        stat_out[key] = {"value": value, "low": low, "high": high, "sd": sd, "n": len(s)}
    if diffs is None:
        return (out, done) if stats is None else (out, done, stat_out)
    diff_out = {}
    for a, b in diffs:
        if point.get(a) is None or point.get(b) is None:
            diff_out[a, b] = {"diff": None, "low": None, "high": None}
            continue
        d = point[a] - point[b]
        low, high = interval((a, b), d)
        diff_out[a, b] = {"diff": d, "low": low, "high": high}
    return (out, done, diff_out) if stats is None else (out, done, diff_out, stat_out)


def tie_groups_by_diff(order, diffs):
    """order：按实力值从高到低排好的名字。diffs：rate(..., diffs=相邻的各对) 返回的 {(高的, 低的): {"diff", "low", "high"}}。

    相邻两名之差的 95% 区间含 0（下界 ≤ 0）就并列；并列可以连成一串。差的区间算不出来（图太少）时不并列。
    返回 [[一组并列的名字], ...]。"""
    groups = []
    for n in order:
        if groups:
            d = diffs.get((groups[-1][-1], n))
            if d is not None and d["low"] is not None and d["low"] <= 0.0:
                groups[-1].append(n)
                continue
        groups.append([n])
    return groups


def tie_groups(order, ratings):
    """旧判法，成绩表已经不用它，留着做对比：相邻两名各自的区间有重叠就并列。返回 [[一组并列的名字], ...]。"""
    groups = []
    for n in order:
        r = ratings[n]
        if groups:
            prev = ratings[groups[-1][-1]]
            if None not in (r["low"], r["high"], prev["low"], prev["high"]) and r["high"] >= prev["low"]:
                groups[-1].append(n)
                continue
        groups.append([n])
    return groups


def expected(ra, rb):
    """实力值 ra 的程序对 rb 的程序的期望得分率。"""
    return 1.0 / (1.0 + 10 ** ((rb - ra) / 400.0))
