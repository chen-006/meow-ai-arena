# -*- coding: utf-8 -*-
"""液面跟着塌陷走（第 12 集，主题负责人加）：纯 Python，Blender 和普通 Python 都能用。

规则：每个液面格归给离它最近的岸（同样近的取塌得最晚的那块岸）。这块岸塌掉、往下掉的时候，这一格的液面也跟着没了
（岩浆 / 水从新塌出来的口子泄到云海里）；在那之前，它旁边已经塌空的边上挂一道往下流的"岩浆瀑布 / 水瀑布"。
所以：湖心岩台周围那圈岩浆一直留到最后，外圈的湖面随着外圈的岸一圈圈没掉。

env.py 用它决定画面；pose / fx 想判断"这一刻这里还有没有液面"可以用 alive()。
"""
import liquid

DELAY = 4.0          # 岸塌的那一步之后再过几步液面消失（碎块开始往下掉大约在这时）


def _land_wave(rp, x, y):
    w = rp.wave[y][x]
    return w if 0 <= w < len(rp.wave_steps) else None


def cell_waves(rp):
    """{(x, y): 决定这一格液面何时消失的那波塌陷（波号；None = 一直在）}。只含有液面的格子。"""
    if hasattr(rp, "_liq_waves"):
        return rp._liq_waves
    cells = liquid.cells(rp)
    out = {}
    if cells:
        INF = 10 ** 9

        def key(w):
            return INF if w is None else w

        # 贴着岸的一格（8 邻接有陆地）：跟着身边塌得最晚的那块岸走
        tval = {}
        for (x, y) in cells:
            best = None
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    nx, ny = x + dx, y + dy
                    if (dx or dy) and 0 <= nx < rp.W and 0 <= ny < rp.H and (nx, ny) not in cells and rp.grid[ny][nx] != 2:
                        k = key(_land_wave(rp, nx, ny))
                        best = k if best is None else max(best, k)
            if best is not None:
                tval[(x, y)] = best
        # 离岸远的湖心：跟着身边最早没掉的那一格走（湖一有口子，湖心就泄掉，只剩贴着岸的一圈）
        frontier = list(tval)
        while frontier:
            nxt = {}
            for c in frontier:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    q = (c[0] + dx, c[1] + dy)
                    if q in cells and q not in tval:
                        nxt[q] = min(nxt.get(q, INF + 1), tval[c])
            tval.update(nxt)
            frontier = list(nxt)
        changed = True                                  # 湖心之间再取一遍最小（同一片湖心一起泄）
        while changed:
            changed = False
            for c in cells:
                if c in tval and any((c[0] + dx, c[1] + dy) in tval for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    is_rim = any((c[0] + dx, c[1] + dy) not in cells and 0 <= c[0] + dx < rp.W and 0 <= c[1] + dy < rp.H
                                 and rp.grid[c[1] + dy][c[0] + dx] != 2 for dx in (-1, 0, 1) for dy in (-1, 0, 1))
                    if is_rim:
                        continue
                    m = min(tval[(c[0] + dx, c[1] + dy)] for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))
                            if (c[0] + dx, c[1] + dy) in tval)
                    if m < tval[c]:
                        tval[c] = m
                        changed = True
        for c in cells:
            v = tval.get(c, INF)
            out[c] = None if v >= INF else v
    rp._liq_waves = out
    return out


def live_waves(rp, gt):
    """小局结束以后不再塌新的一波（和 env.set_collapse 一样）：返回每波是否照常塌。"""
    return [not (gt > rp.T + 1.0 and s > rp.T) for s in rp.wave_steps]


def gone_step(rp, w):
    """波号 w 的液面在第几步消失（None = 不消失）。"""
    if w is None:
        return None
    return rp.wave_steps[w] + DELAY


def alive(rp, mx, my, gt):
    """地图坐标 (米) 这一点在 gt 时还有没有液面：有就返回液面高度（米），没有返回 None。"""
    z = liquid.z_at(rp, mx, my)
    if z is None:
        return None
    cx, cy = int(mx // rp.cell), int(my // rp.cell)
    w = cell_waves(rp).get((cx, cy))
    if w is None:
        return z
    if not live_waves(rp, gt)[w]:
        return z
    return z if gt < gone_step(rp, w) else None
