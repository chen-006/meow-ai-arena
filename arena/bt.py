"""Bradley–Terry 实力值。

口径与第 2 集炸弹人一致：
- 每局按得分排出名次，拆成两两比较（名次靠前记胜，同分记半胜）；
- 各单局人数的总权重相等，避免人多的局拆出更多比较而压过其他人数；
- 每对交过手的程序加一局虚拟平局，防止全胜或全负时实力值趋于无穷；
- 基准 = 0 分，实力强 10 倍记 +400；弱于基准显示负数；
- 人数超过选手数时，同一选手可以复制多份上场（名字#2 …），计分时合并，复制品之间不比较。
置信区间：在每个单局人数内按地图重抽样，重新拟合。
"""
from __future__ import annotations

import itertools
import math
import random
import re
from collections import Counter, defaultdict


def ranks(score):
    return [1 + sum(s > x for s in score) for x in score]


def base(name):
    """同一选手的复制品写作 名字#2、名字#3……计分时合并回原名。"""
    return re.sub(r"#\d+$", "", name)


def comparisons(rows, sizes):
    counts = Counter(r["size"] for r in rows)
    pairs = defaultdict(lambda: [0.0, 0.0])  # (a, b) a<b -> [局数权重, a 的胜场权重]
    total = len(rows)
    for r in rows:
        n = r["size"]
        seats = [base(s) for s in r["seats"]]
        valid = [(i, j) for i, j in itertools.combinations(range(n), 2) if seats[i] != seats[j]]
        if not valid:
            continue
        # 同一选手的复制品之间不比较；每局的权重按实际比较数摊开，保证各人数总权重相等
        w = total / (len(sizes) * counts[n] * len(valid))
        rk = ranks(r["score"])
        for i, j in valid:
            a, b = seats[i], seats[j]
            win = 1.0 if rk[i] < rk[j] else 0.5 if rk[i] == rk[j] else 0.0
            if a > b:
                a, b, win = b, a, 1 - win
            pairs[a, b][0] += w
            pairs[a, b][1] += w * win
    return pairs


def fit(rows, names, anchor):
    sizes = sorted({r["size"] for r in rows})
    pairs = comparisons(rows, sizes)
    games = {p: (g + 1, w + 0.5) for p, (g, w) in pairs.items()}
    wins = dict.fromkeys(names, 0.0)
    for (a, b), (g, w) in games.items():
        wins[a] += w
        wins[b] += g - w
    strength = dict.fromkeys(names, 1.0)
    for _ in range(20000):
        denom = dict.fromkeys(names, 0.0)
        for (a, b), (g, _w) in games.items():
            v = g / (strength[a] + strength[b])
            denom[a] += v
            denom[b] += v
        new = {n: wins[n] / denom[n] if denom[n] else 1.0 for n in names}
        center = sum(math.log(v) for v in new.values()) / len(names)
        new = {n: math.exp(math.log(v) - center) for n, v in new.items()}
        delta = max(abs(math.log(new[n] / strength[n])) for n in names)
        strength = new
        if delta < 1e-10:
            break
    origin = math.log(strength[anchor])
    return {n: 400 / math.log(10) * (math.log(strength[n]) - origin) for n in names}


def rate(rows, names, anchor, boot=200, seed=0):
    """返回 {名字: (实力值, 下限, 上限)}，以及各单局人数单独拟合的实力值。"""
    if anchor not in names:
        raise ValueError("基准必须在名单中")
    point = fit(rows, names, anchor)
    rng = random.Random(seed)
    by_map = defaultdict(list)
    for r in rows:
        by_map[r["size"], r["map_seed"]].append(r)
    maps_by_size = defaultdict(list)
    for (k, m) in by_map:
        maps_by_size[k].append(m)
    samples = defaultdict(list)
    for _ in range(boot):
        rs = []
        for k, maps in maps_by_size.items():
            for m in rng.choices(maps, k=len(maps)):
                rs.extend(by_map[k, m])
        for n, v in fit(rs, names, anchor).items():
            samples[n].append(v)
    ci = {}
    for n in names:
        s = sorted(samples[n]) or [point[n]]
        ci[n] = (point[n], s[int(0.025 * (len(s) - 1))], s[int(0.975 * (len(s) - 1))])
    per_size = {k: fit([r for r in rows if r["size"] == k], names, anchor)
                for k in sorted(maps_by_size)}
    return ci, per_size
