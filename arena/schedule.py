"""赛程生成：任意参赛人数、任意单局人数。

每张地图上，把参赛程序分成若干组，每组在同一张地图上把座位完整轮换一圈
（k 人组打 k 局），所以组内的座位优势完全抵消。分组时优先选出场少、
彼此交手少的程序，使出场次数最多相差 1、两两交手次数尽量接近。
"""
from __future__ import annotations

import itertools
import random
from collections import Counter


def groups_for_map(players, k, appear, meet, rng):
    """为一张地图选出若干 k 人组，覆盖所有程序至少一次。"""
    if k == len(players):
        order = players[:]
        rng.shuffle(order)
        return [order]
    remaining = set(players)
    groups = []
    while remaining:
        group = []
        pool = sorted(remaining, key=lambda p: (appear[p], rng.random()))
        while len(group) < k:
            if not pool:  # 最后一组不满时，从已分组的程序里补
                pool = sorted((p for p in players if p not in group),
                              key=lambda p: (appear[p], rng.random()))
            best = min(pool, key=lambda p: (appear[p], sum(meet[frozenset((p, q))] for q in group), rng.random()))
            group.append(best)
            pool.remove(best)
            remaining.discard(best)
        for a, b in itertools.combinations(group, 2):
            meet[frozenset((a, b))] += k
        for p in group:
            appear[p] += k
        rng.shuffle(group)
        groups.append(group)
    return groups


def make_schedule(players, sizes, maps_per_size, seed):
    """返回局列表：{id, size, map_seed, map_index, group, seats}。seats[i] 是第 i 号位的程序名。"""
    rng = random.Random(seed)
    used = set()
    games = []
    for k in sizes:
        if not 2 <= k <= len(players):
            raise ValueError(f"单局人数 {k} 超出范围")
        appear, meet = Counter(), Counter()
        for m in range(maps_per_size):
            map_seed = rng.randrange(1, 2**31)
            while map_seed in used:
                map_seed = rng.randrange(1, 2**31)
            used.add(map_seed)
            for g, group in enumerate(groups_for_map(players, k, appear, meet, rng)):
                for shift in range(k):
                    games.append({"size": k, "map_seed": map_seed, "map_index": m, "group": g,
                                  "seats": group[shift:] + group[:shift]})
    for i, game in enumerate(games, 1):
        game["id"] = f"{game['size']:02d}-{game['map_index']:03d}-{game['group']:02d}-{i:05d}"
    return games


def audit(games, players):
    """按人数统计出场、座位、交手是否均衡。"""
    report = {}
    for k in sorted({g["size"] for g in games}):
        rows = [g for g in games if g["size"] == k]
        appear = Counter(p for g in rows for p in g["seats"])
        seat = Counter((p, i) for g in rows for i, p in enumerate(g["seats"]))
        meet = Counter(frozenset(pair) for g in rows for pair in itertools.combinations(g["seats"], 2))
        pairs = [meet[frozenset(pair)] for pair in itertools.combinations(players, 2)]
        report[k] = {"games": len(rows), "maps": len({g["map_seed"] for g in rows}),
                     "appear_min": min(appear[p] for p in players), "appear_max": max(appear[p] for p in players),
                     "seat_min": min(seat[p, i] for p in players for i in range(k) if appear[p]),
                     "seat_max": max(seat.values()),
                     "meet_min": min(pairs), "meet_max": max(pairs)}
    return report
