# -*- coding: utf-8 -*-
"""第 12 集三个主题的物件（Blender 里跑）：火山口的玄武岩柱、港湾的渔具堆和栈桥桩、双堡的城墙角楼和队旗，
以及它们的远景大件（火山锥、灯塔、帆船、城堡塔楼）和贴地小物（黑曜石、冒热气的小火口、贝壳、海滩草）。

规矩和 props.py 一样：挡镖的墙是实心的、比人高，外形不出墙格（最多探出几厘米）；不挡镖的东西低矮细小。
import 时把远景、小物登记进 props.FAR / FEATURES / DETAILS，env.py 照常按名字取。
"""
import math

import numpy as np

import geom
import props as PR
import scene_lib as SL
from geom import MeshBuilder
from props import C, M_GLOW, M_SOLID, blob, box, quad, stack, tri, tube


# ================================================================ 火山口：玄武岩柱
def hex_col(mb, cx, cy, r, h, side, top, z=0.0, rot=0.0, glow=None):
    """一根六棱柱：侧面两种明暗交替，顶面带一圈倒角高光。"""
    lv = [(r, z - 0.06), (r, z + h - 0.035), (r * 0.84, z + h)]
    cols = [side, C(top, 0.82)]
    stack(mb, cx, cy, lv, cols, n=6, rot0=rot, cap=top)
    if glow is not None:                         # 柱脚一圈暗红的余烬光
        stack(mb, cx, cy, [(r * 1.04, z - 0.02), (r * 1.04, z + 0.05)], glow, n=6, rot0=rot, mat=M_GLOW)


def basalt(mb, X0, Y0, Wd, Hd, hk, rng):
    """一组同波的墙格 -> 一丛高低错落的玄武岩六棱柱（柱顶是平的、一级级台阶），外沿不出墙格。"""
    Sh = min(Wd, Hd)
    r = 0.125 if Sh < 0.75 else 0.19
    dx, dy = math.sqrt(3) * r * 1.0, 1.5 * r * 1.0
    side_a, side_b = C(SL.PAL["stone_side"]), C(SL.PAL["stone_shade"])
    top = C(SL.PAL["stone_top"])
    m = 0.02
    cx, cy = X0 + Wd / 2, Y0 + Hd / 2
    ang = rng.uniform(0, 6.28)
    tilt = np.array([math.cos(ang), math.sin(ang)])
    base_h = rng.uniform(1.35, 1.6) * hk
    pts = []
    row = 0
    y = Y0 + m + r * 0.87
    while y <= Y0 + Hd - m - r * 0.87 + 1e-6:
        x = X0 + m + r + (dx / 2 if row % 2 else 0.0)
        while x <= X0 + Wd - m - r + 1e-6:
            pts.append((x, y))
            x += dx
        y += dy
        row += 1
    if not pts:
        pts = [(cx, cy)]
        r = min(Wd, Hd) / 2 - m
    # 行、列没铺满的一边居中
    P = np.array(pts)
    P[:, 0] += (X0 + Wd / 2) - (P[:, 0].min() + P[:, 0].max()) / 2
    P[:, 1] += (Y0 + Hd / 2) - (P[:, 1].min() + P[:, 1].max()) / 2
    span = max(Wd, Hd) / 2 + 1e-6
    for i, (x, y) in enumerate(P):
        d = np.array([x - cx, y - cy])
        s = float(d @ tilt) / span                       # -1..1：一侧高、一侧低
        e = max(abs(x - cx) / (Wd / 2), abs(y - cy) / (Hd / 2))
        h = base_h * (1.0 + 0.22 * s - 0.18 * max(0.0, e - 0.55)) + rng.uniform(-0.12, 0.12)
        h = max(1.05 * hk, h)
        sd = side_a if (i + int(rng.integers(0, 2))) % 2 else side_b
        hex_col(mb, x, y, r * rng.uniform(0.93, 1.0), h, C(sd, rng.uniform(0.94, 1.05)), C(top, rng.uniform(0.95, 1.05)),
                rot=math.pi / 6 + rng.uniform(-0.08, 0.08), glow=C("#FF6A2A") if rng.random() < 0.35 else None)
    return mb


# ================================================================ 港湾：渔具堆
WOOD_PAINT = ["#3FA7B5", "#2E6E9E", "#EDE6D6", "#E0573F", "#4FA87A"]


def painted_crate(mb, cx, cy, s, h, z, col, rng, rz=0.0):
    """刷了漆的木箱：漆色箱体 + 横向的板缝 + 深色的角柱。"""
    w = s / 2
    c = C(col, rng.uniform(0.95, 1.05))
    box(mb, cx - w, cx + w, cy - w, cy + w, z, z + h, c, top=C(c, 1.1), rz=rz)
    dk = C(col, 0.62)
    e = 0.008
    cr, sr = math.cos(rz), math.sin(rz)

    def R(x, y):
        return (cx + x * cr - y * sr, cy + x * sr + y * cr)
    for k in range(1, 3):                       # 两道板缝
        zz = z + h * k / 3
        for (ux, uy) in ((0, -1), (1, 0), (0, 1), (-1, 0)):
            px, py = -uy, ux
            o = w + e
            a = R(ux * o + px * (-w), uy * o + py * (-w))
            b = R(ux * o + px * w, uy * o + py * w)
            quad(mb, [(a[0], a[1], zz - 0.012), (b[0], b[1], zz - 0.012), (b[0], b[1], zz + 0.012), (a[0], a[1], zz + 0.012)], dk)
    for (sx, sy) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):    # 角柱
        p = R(sx * (w - 0.025), sy * (w - 0.025))
        box(mb, p[0] - 0.032, p[0] + 0.032, p[1] - 0.032, p[1] + 0.032, z, z + h + 0.01, C("#6E4E3A"), rz=rz)


def trap(mb, cx, cy, sx_, sy_, h, z, rng):
    """捕虾笼：木框 + 深色的网面（侧面横竖几道浅色网线）。"""
    net, frame = C("#4E6A6E"), C("#C99A5E")
    box(mb, cx - sx_ / 2, cx + sx_ / 2, cy - sy_ / 2, cy + sy_ / 2, z, z + h, net, top=C("#5E7E80"))
    for (ax, ay) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        x, y = cx + ax * (sx_ / 2 - 0.02), cy + ay * (sy_ / 2 - 0.02)
        box(mb, x - 0.025, x + 0.025, y - 0.025, y + 0.025, z, z + h + 0.01, frame)
    e = 0.006
    for zz in (z + 0.015, z + h - 0.015):
        box(mb, cx - sx_ / 2 - e, cx + sx_ / 2 + e, cy - sy_ / 2 - e, cy + sy_ / 2 + e, zz - 0.014, zz + 0.014, frame)
    line = C("#A8C4C0")
    for k in range(1, 4):
        t = k / 4
        x = cx - sx_ / 2 + sx_ * t
        quad(mb, [(x - 0.006, cy - sy_ / 2 - e, z), (x + 0.006, cy - sy_ / 2 - e, z), (x + 0.006, cy - sy_ / 2 - e, z + h),
                  (x - 0.006, cy - sy_ / 2 - e, z + h)], line)
        y = cy - sy_ / 2 + sy_ * t
        quad(mb, [(cx + sx_ / 2 + e, y - 0.006, z), (cx + sx_ / 2 + e, y + 0.006, z), (cx + sx_ / 2 + e, y + 0.006, z + h),
                  (cx + sx_ / 2 + e, y - 0.006, z + h)], line)


def barrel2(mb, cx, cy, r, h, z, hoop="#2E5E8E"):
    wood, hp = C("#C08A58"), C(hoop)
    lv = [(0.86, 0.0), (0.86, 0.1), (0.92, 0.1), (0.92, 0.17), (1.0, 0.36), (1.0, 0.64), (0.92, 0.83), (0.92, 0.9), (0.86, 0.9), (0.86, 1.0)]
    cl = [C(wood, 0.9), hp, hp, wood, C(wood, 1.08), wood, hp, hp, C(wood, 0.9)]
    stack(mb, cx, cy, [(r * a, z + h * t) for a, t in lv], cl, n=10, cap=C(wood, 1.15))


def buoy(mb, x, y, z, r):
    blob(mb, x, y, z + r, r, r, r * 0.95, C("#E0503C"), C("#FFFFFF"), sub=2)
    box(mb, x - 0.012, x + 0.012, y - 0.012, y + 0.012, z + r * 1.8, z + r * 2.4, C("#5A4A48"))


def rope_coil(mb, x, y, z, r, turns=2):
    for k in range(turns):
        rr = r * (1.0 - 0.18 * k)
        pts = [(x + rr * math.cos(2 * math.pi * i / 14), y + rr * math.sin(2 * math.pi * i / 14), z + 0.03 + 0.055 * k) for i in range(15)]
        tube(mb, pts, 0.028, C("#E8D2A0"), seg=5)


def harbor_pile(mb, X0, Y0, Wd, Hd, rng):
    """一组同波的墙格 -> 一堆渔具：漆色木箱、捕虾笼、蓝箍木桶摞起来，顶上放缆绳卷和红白浮标。都比人高、不出墙格。"""
    nx, ny = max(1, int(round(Wd / 1.0))), max(1, int(round(Hd / 1.0)))
    if min(Wd, Hd) < 0.75:                       # 窄条：沿长边切成半米见方的几摞
        nx, ny = (max(1, int(round(Wd / 0.5))), 1) if Wd >= Hd else (1, max(1, int(round(Hd / 0.5))))
    sx, sy = Wd / nx, Hd / ny
    k0 = int(rng.integers(0, 3))
    for j in range(ny):
        for i in range(nx):
            px, py = X0 + (i + 0.5) * sx, Y0 + (j + 0.5) * sy
            s = min(sx, sy) - 0.06
            kind = (k0 + i + j) % 3
            if s >= 0.8:
                if kind == 0:                    # 大漆箱 + 小漆箱 + 缆绳卷
                    col = WOOD_PAINT[int(rng.integers(0, len(WOOD_PAINT)))]
                    painted_crate(mb, px, py, s, s * 0.78, 0.0, col, rng, rz=rng.uniform(-0.04, 0.04))
                    s2 = s * 0.56
                    ox, oy = rng.uniform(-0.12, 0.12) * s, rng.uniform(-0.12, 0.12) * s
                    painted_crate(mb, px + ox, py + oy, s2, s2 * 0.8, s * 0.78, WOOD_PAINT[int(rng.integers(0, len(WOOD_PAINT)))], rng,
                                  rz=rng.uniform(0.2, 0.6))
                    rope_coil(mb, px - ox * 1.6, py - oy * 1.6, s * 0.78, 0.12)
                    buoy(mb, px + ox, py + oy, s * 0.78 + s2 * 0.8, 0.08)
                elif kind == 1:                  # 四只捕虾笼摞两层 + 一只在顶上
                    a = s / 2 - 0.01
                    hh = 0.36
                    for (ax, ay) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                        trap(mb, px + ax * a / 2, py + ay * a / 2, a - 0.02, a - 0.02, hh, 0.0, rng)
                        trap(mb, px + ax * a / 2, py + ay * a / 2, a - 0.02, a - 0.02, hh, hh, rng)
                    trap(mb, px + rng.uniform(-0.05, 0.05), py + rng.uniform(-0.05, 0.05), a, a * 0.9, hh * 0.9, 2 * hh, rng)
                    buoy(mb, px + a * 0.55, py - a * 0.4, 2 * hh, 0.075)
                    buoy(mb, px - a * 0.5, py + a * 0.45, 2 * hh, 0.07)
                else:                            # 四只蓝箍木桶 + 顶上一只 + 浮标
                    br = s * 0.24
                    for (ax, ay) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                        barrel2(mb, px + ax * s * 0.24, py + ay * s * 0.24, br, s * 0.6, 0.0)
                    barrel2(mb, px, py, br * 1.05, s * 0.55, s * 0.6, hoop="#E0573F")
                    buoy(mb, px + br * 0.2, py, s * 1.15, 0.07)
            else:                                # 半米的一摞
                if kind == 0:
                    barrel2(mb, px, py, s * 0.47, 0.6, 0.0)
                    barrel2(mb, px, py, s * 0.45, 0.56, 0.6, hoop="#E0573F")
                    buoy(mb, px, py, 1.16, 0.065)
                elif kind == 1:
                    for k in range(3):
                        trap(mb, px + rng.uniform(-0.015, 0.015), py + rng.uniform(-0.015, 0.015), s, s, 0.38, 0.38 * k, rng)
                    rope_coil(mb, px, py, 1.14, s * 0.3)
                else:
                    col = WOOD_PAINT[int(rng.integers(0, len(WOOD_PAINT)))]
                    painted_crate(mb, px, py, s, 0.5, 0.0, col, rng, rz=rng.uniform(-0.05, 0.05))
                    painted_crate(mb, px, py, s * 0.9, 0.42, 0.5, WOOD_PAINT[int(rng.integers(0, len(WOOD_PAINT)))], rng,
                                  rz=rng.uniform(0.15, 0.4))
                    buoy(mb, px, py, 0.92, 0.08)
                    box(mb, px - 0.02, px + 0.02, py - 0.02, py + 0.02, 0.9, 1.3, C("#8A6A4A"))       # 一根竖着的船桨
                    box(mb, px - 0.05, px + 0.05, py - 0.012, py + 0.012, 1.18, 1.36, C("#E0573F"))
    return mb


def build_piers(rp, router, attach, coll, rng, aux, z_liq):
    """栈桥挨着海水的边：一道深色的木边梁贴着岸壁垂到水里，隔一米一根木桩（都在水里、比桥面只高几厘米）。"""
    if "deck" not in aux:
        return
    deck = np.asarray(aux["deck"])
    cs = rp.cell
    import liquid as LQ
    liq = LQ.cells(rp)
    H, W = rp.H, rp.W
    beam, pile, pile_top = C("#6E4A34"), C("#7E5A40"), C("#A88A66")
    mbs = {}
    for y in range(H):
        for x in range(W):
            if not deck[y, x]:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                q = (x + dx, y + dy)
                if q not in liq:
                    continue
                gid = router((x + 0.5) * cs, (y + 0.5) * cs)
                mb = mbs.setdefault(gid, MeshBuilder())
                # 这条边在 Blender 坐标里的两端，外法线 (dx, -dy)
                if dx:
                    ex = (x + (1 if dx > 0 else 0)) * cs
                    a, b = (ex, -y * cs), (ex, -(y + 1) * cs)
                else:
                    ey = -(y + (1 if dy > 0 else 0)) * cs
                    a, b = (x * cs, ey), ((x + 1) * cs, ey)
                o = 0.035
                nx_, ny_ = dx * o, -dy * o
                quad(mb, [(a[0] + nx_, a[1] + ny_, z_liq - 0.15), (b[0] + nx_, b[1] + ny_, z_liq - 0.15),
                          (b[0] + nx_, b[1] + ny_, 0.0), (a[0] + nx_, a[1] + ny_, 0.0)], beam)
                quad(mb, [(a[0] + nx_, a[1] + ny_, -0.16), (b[0] + nx_, b[1] + ny_, -0.16),
                          (b[0] + nx_, b[1] + ny_, -0.1), (a[0] + nx_, a[1] + ny_, -0.1)], C(beam, 0.7))
                k = (x if dy else y)
                if k % 2 == 0:                   # 每米一根木桩
                    mx = (a[0] + b[0]) / 2 + dx * 0.09
                    my = (a[1] + b[1]) / 2 - dy * 0.09
                    stack(mb, mx, my, [(0.075, z_liq - 0.4), (0.075, 0.03), (0.06, 0.06)], [pile, C(pile, 1.1)], n=8, cap=pile_top)
                    stack(mb, mx, my, [(0.09, z_liq - 0.02), (0.09, z_liq + 0.05)], C("#3E6E5A"), n=8)     # 水线上的青苔
    for gid, mb in mbs.items():
        attach(mb.build("pier_edge_%d" % gid, PR.mats(), coll), gid)
    # 一两条系在栈桥边的小船（在水里，船舷比桥面低，不挡人和镖）
    comps = {}
    for c in liq:
        comps.setdefault(None, []).append(c)
    import liquid as LQ2
    big = [cc for cc, bd in LQ2.pit_components(rp) if not bd and len(cc) >= 40]
    for i, comp in enumerate(sorted(big, key=len, reverse=True)[:2]):
        cset = set(comp)
        # 离岸最远的格子
        def d_land(c):
            d = 0
            while all((c[0] + ex, c[1] + ey) in cset for ex in range(-d - 1, d + 2) for ey in range(-d - 1, d + 2)):
                d += 1
            return d
        best = max(comp, key=lambda c: (d_land(c), -abs(c[0] - rp.W / 2)))
        bx, by = (best[0] + 0.5) * cs, -(best[1] + 0.5) * cs
        xs = [c[0] for c in comp]
        ys = [c[1] for c in comp]
        along_x = (max(xs) - min(xs)) >= (max(ys) - min(ys))
        mb = MeshBuilder()
        rowboat(mb, bx + (0.6 if along_x else 0.0) * (1 if i else -1), by, z_liq, 0.0 if along_x else math.pi / 2, rng)
        mb.build("rowboat_%d" % i, PR.mats(), coll)


def rowboat(mb, x, y, z, rot, rng):
    """小划船：船身比水面高一点点，船舷顶在水面以上 0.17 米（仍比岸面低）。"""
    L, Wb = 0.62, 0.24
    hull, rim, inner = C("#E8E2D2"), C("#2E6E9E"), C("#B88A5E")
    n = 12
    c, s = math.cos(rot), math.sin(rot)

    def R(px, py, pz):
        return (x + px * c - py * s, y + px * s + py * c, pz)
    prof = []
    for i in range(n):
        a = 2 * math.pi * i / n
        px, py = math.cos(a) * L, math.sin(a) * Wb
        if px > 0:
            py *= 1 - 0.55 * (px / L) ** 2          # 船头收尖
        prof.append((px, py))
    lv = [(0.55, z - 0.06), (0.9, z + 0.07), (1.0, z + 0.16)]
    rings = [[R(px * k, py * k, zz) for (px, py) in prof] for (k, zz) in lv]
    for li in range(len(rings) - 1):
        for i in range(n):
            j = (i + 1) % n
            quad(mb, [rings[li][i], rings[li][j], rings[li + 1][j], rings[li + 1][i]], hull if li == 0 else rim)
    top = rings[-1]
    inn = [R(px * 0.86, py * 0.8, z + 0.05) for (px, py) in prof]
    for i in range(n):
        j = (i + 1) % n
        quad(mb, [top[i], top[j], inn[j], inn[i]], C(rim, 1.15))
    ctr = R(0, 0, z + 0.05)
    for i in range(n):
        tri(mb, [inn[i], inn[(i + 1) % n], ctr], inner)
    box(mb, *(lambda p: (p[0] - 0.05, p[0] + 0.05, p[1] - 0.2, p[1] + 0.2))(R(0.0, 0.0, 0)), z + 0.09, z + 0.12, C("#8A6A4A"), rz=rot)
    for sg in (-1, 1):                               # 一对船桨搭在船舷上
        p0, p1 = R(-0.05, sg * 0.18, z + 0.16), R(0.25, sg * 0.5, z + 0.02)
        tube(mb, [p0, p1], 0.014, C("#C9A070"), seg=5)


# ================================================================ 双堡：城墙、角楼、门楼、队旗
FLAGS = []           # env 每帧让旗子飘：[(物体, 静止时的顶点, 每个顶点离旗杆多远 0..1)]


def _frames(rp):
    """墙格的连通块里"围成方框"的那些（城堡）：[(格子集合, (x0, y0, x1, y1) 外框，格坐标，x1/y1 不含)]，其余的连通块另列。"""
    frames, others = [], []
    comps = PR.wall_components(rp.grid)
    # 只隔着门洞（≤2 格）的墙块并成一组：南北都开门的城堡会被门洞切成两半
    par = list(range(len(comps)))

    def find(i):
        while par[i] != i:
            par[i] = par[par[i]]
            i = par[i]
        return i
    for i in range(len(comps)):
        for j in range(i + 1, len(comps)):
            if any(max(abs(a[0] - b[0]), abs(a[1] - b[1])) <= 3 for a in comps[i] for b in comps[j]):
                par[find(i)] = find(j)
    merged = {}
    for i, c in enumerate(comps):
        merged.setdefault(find(i), []).extend(c)
    for grp in merged.values():
        xs, ys = [c[0] for c in grp], [c[1] for c in grp]
        x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
        if x1 - x0 >= 5 and y1 - y0 >= 5 and len(grp) < 0.7 * (x1 - x0) * (y1 - y0):
            frames.append((set(grp), (x0, y0, x1, y1)))
    for comp in comps:
        if not any(comp[0] in fs for fs, _ in frames):
            others.append(comp)
    return frames, others


# 两边城堡 / 远景城塔的颜色：跟着"这一边出生的是哪队"走，队伍色按队伍身份（Astra 队蓝、Fable 队红）。
# env.build_env 每建一局先调 set_teams 整个覆盖，不会沿用上一局。
ROOF_OF = {"#FF5B4D": "#D9573F", "#2E8CFF": "#4A78C8"}     # 旗是队伍色，屋顶用压暗一点的同色
SIDES = {}


def set_teams(rp, team_colors=None):
    """team_colors：(team 0, team 1)。左边 = 出生点平均 x 更小的那队（回放里一向是 team 0，这里按出生点算，不靠约定）。"""
    import chars as _CH
    tc = tuple(team_colors) if team_colors else tuple(_CH.TEAM_COLORS)
    xs = [[], []]
    try:
        sp = rp.spawns()
        for (s, t, _) in rp.players:
            if t in (0, 1) and s < len(sp):
                xs[t].append(sp[s][0])
    except Exception:
        pass
    left_team = 1 if (xs[0] and xs[1] and sum(xs[1]) / len(xs[1]) < sum(xs[0]) / len(xs[0])) else 0
    SIDES.clear()
    SIDES.update(left=tc[left_team].upper(), right=tc[1 - left_team].upper(), mid_x=rp.Wm / 2)


def side_cols(left):
    """这一边的 (旗色, 屋顶色)，#RRGGBB。主题 rock 里的 red / blue 可以微调这两种旗色。"""
    from env import ROCK
    if not SIDES:
        SIDES.update(left="#FF5B4D", right="#2E8CFF", mid_x=0.0)
    tc = SIDES["left" if left else "right"]
    flag = {"#FF5B4D": ROCK.get("red", "#FF5B4D"), "#2E8CFF": ROCK.get("blue", "#2E8CFF")}.get(tc, tc)
    return flag, ROOF_OF.get(tc, tc)


def team_cols(rp, bbox):
    left = (bbox[0] + bbox[2]) / 2 < rp.W / 2
    flag, roof = side_cols(left)
    return C(flag), C(roof), left


def _brick_cell(mb, X0, Y0, cs, z0, z1, horiz, rng, stones, rowh=0.23):
    """一格墙身：一排排错缝的砖。horiz：墙是东西走向（砖沿 x 方向切）。"""
    nrow = max(1, int(round((z1 - z0) / rowh)))
    rh = (z1 - z0) / nrow
    j = 0.006
    for k in range(nrow):
        za, zb = z0 + k * rh + 0.006, z0 + (k + 1) * rh - 0.006
        col = C(stones[int(rng.integers(0, len(stones)))], rng.uniform(0.94, 1.05) * (0.9 if k == 0 else 1.0))
        if k % 2 == 0:
            parts = [(X0, X0 + cs, Y0, Y0 + cs)]
        elif horiz:
            parts = [(X0, X0 + cs / 2, Y0, Y0 + cs), (X0 + cs / 2, X0 + cs, Y0, Y0 + cs)]
        else:
            parts = [(X0, X0 + cs, Y0, Y0 + cs / 2), (X0, X0 + cs, Y0 + cs / 2, Y0 + cs)]
        for (xa, xb, ya, yb) in parts:
            c2 = C(col, rng.uniform(0.96, 1.04))
            box(mb, xa + j, xb - j, ya + j, yb - j, za, zb, c2, top=C(c2, 1.08))


def flag_obj(coll, x, y, z, w, h, col, side, name):
    """旗杆顶上的一面小旗（单独一个物体，每帧飘）。side：旗面朝 +x(1) 还是 -x(-1) 展开。"""
    nu, nv = 7, 3
    V, F, Cc, U = [], [], [], []
    for i in range(nu):
        u = i / (nu - 1)
        for j in range(nv):
            v = j / (nv - 1)
            V.append((x + side * u * w, y, z - v * h))
            U.append(u)
            stripe = 0.42 < v < 0.58 and u > 0.25
            c = C("#FFFFFF") if stripe else C(col, 1.0 - 0.12 * v)
            Cc.append(c)
    for i in range(nu - 1):
        for j in range(nv - 1):
            a = i * nv + j
            F.append((a, a + nv, a + nv + 1, a + 1))
    mb = MeshBuilder()
    mb.add(np.array(V), F, vcolors=np.array(Cc), smooth=True)
    ob = mb.build(name, [SL.mat_vcol("M_flag", rough=0.75, spec=0.15, emis=0.15)], coll)
    try:
        ob.visible_shadow = True
    except Exception:
        pass
    FLAGS.append((ob, np.array(V, dtype=np.float32), np.array(U, dtype=np.float32)))
    return ob


def update_flags(gt):
    for k, (ob, V0, U) in enumerate(FLAGS):
        ph = gt * 0.42 + k * 1.7
        V = V0.copy()
        V[:, 1] += (0.045 * np.sin(ph - U * 5.0) * U).astype(np.float32)
        V[:, 2] += (0.012 * np.sin(ph * 1.3 - U * 4.0) * U).astype(np.float32)
        ob.data.vertices.foreach_set("co", V.ravel())
        ob.data.update()


def build_castles(rp, router, attach, coll, rng):
    """kind=castle：围成方框的墙 = 一座完整的小城堡（错缝砖墙、外侧一排垛口、四角圆角楼和尖顶、门两边的门楼、队旗、墙上挂的队旗幡）；
    别的墙块 = 广场上的东西（方的是石砌花台和修剪成圆球的树，长条是一段带垛口的矮墙）。每格按自己的塌陷碎块归组。"""
    del FLAGS[:]
    cs = rp.cell
    grid, H, W = rp.grid, rp.H, rp.W
    frames, others = _frames(rp)
    mbs = {}
    stones = [SL.PAL["stone_lit"], SL.PAL["stone_top"], SL.PAL["stone_side"], SL.PAL["stone_lit"]]
    cap = C(SL.PAL["stone_top"], 1.04)
    dark = C(SL.PAL["stone_base"])
    Hw = 1.15

    def isw(x, y):
        return 0 <= x < W and 0 <= y < H and grid[y][x] == 1

    def get(x, y):
        gid = router((x + 0.5) * cs, (y + 0.5) * cs)
        return gid, mbs.setdefault(gid, MeshBuilder())

    def merlon_row(mb, X0, Y0, side, z, n_per=1, depth=0.15, hh=0.24, full=False):
        """side：垛口贴着哪条边（'n' 's' 'e' 'w'）；full=True 时两边都有（矮墙）。"""
        sides = [side] if not full else (['n', 's'] if side in 'ns' else ['e', 'w'])
        for sd in sides:
            for k in range(n_per):
                t0 = (k + 0.22) / n_per
                t1 = (k + 0.78) / n_per
                if sd in 'ns':
                    ya, yb = (Y0 + cs - depth, Y0 + cs) if sd == 'n' else (Y0, Y0 + depth)
                    box(mb, X0 + cs * t0, X0 + cs * t1, ya, yb, z, z + hh, C(stones[k % 4], 1.02), top=cap)
                else:
                    xa, xb = (X0 + cs - depth, X0 + cs) if sd == 'e' else (X0, X0 + depth)
                    box(mb, xa, xb, Y0 + cs * t0, Y0 + cs * t1, z, z + hh, C(stones[k % 4], 1.02), top=cap)

    for fi, (cset, (bx0, by0, bx1, by1)) in enumerate(frames):
        flagc, roofc, left = team_cols(rp, (bx0, by0, bx1, by1))
        for (x, y) in sorted(cset):
            gid, mb = get(x, y)
            X0, Y0 = x * cs, -(y + 1) * cs
            nb_h = isw(x - 1, y) or isw(x + 1, y)
            nb_v = isw(x, y - 1) or isw(x, y + 1)
            on_l, on_r, on_t, on_b = x == bx0, x == bx1 - 1, y == by0, y == by1 - 1
            corner = (on_l or on_r) and (on_t or on_b)
            # 门洞旁边的格子：沿着墙走，一边是墙、一边是空（门洞）
            gate = False
            if (on_t or on_b) and not corner:
                gate = (not isw(x - 1, y)) or (not isw(x + 1, y))
            if (on_l or on_r) and not corner:
                gate = gate or (not isw(x, y - 1)) or (not isw(x, y + 1))
            # 墙脚一圈略宽的深色勒脚
            box(mb, X0 - 0.015, X0 + cs + 0.015, Y0 - 0.015, Y0 + cs + 0.015, -0.04, 0.12, dark, top=C(dark, 1.2))
            if corner:
                # 圆角楼：比墙高，顶上一圈挑出的檐口、尖顶、旗杆和旗
                ccx, ccy = X0 + cs / 2, Y0 + cs / 2
                r = cs * 0.6
                lv = [(r, 0.1), (r, 0.6), (r * 0.98, 1.1), (r, 1.5)]
                stack(mb, ccx, ccy, lv, [C(stones[0]), C(stones[2], 1.02), C(stones[1])], n=14, cap=cap)
                stack(mb, ccx, ccy, [(r, 1.5), (r * 1.12, 1.56), (r * 1.12, 1.64)], [C(dark, 1.3), cap], n=14, cap=cap)
                stack(mb, ccx, ccy, [(r * 1.14, 1.64), (r * 0.66, 2.0), (r * 0.3, 2.3), (0.015, 2.62)], [roofc, C(roofc, 1.1), C(roofc, 1.2)], n=14)
                for k in range(2):                   # 塔身上两个窄窗
                    a = -math.pi / 2 + (k - 0.5) * 1.1
                    px, py = ccx + math.cos(a) * r * 1.0, ccy + math.sin(a) * r * 1.0
                    box(mb, px - 0.035, px + 0.035, py - 0.012, py + 0.012, 0.78, 1.0, C("#3A3448"), rz=a + math.pi / 2)
                tube(mb, [(ccx, ccy, 2.56), (ccx, ccy, 3.0)], 0.014, C("#5A4A48"), seg=5)
                blob(mb, ccx, ccy, 3.01, 0.025, 0.025, 0.025, C("#E8C24A"), C("#FFE89A"), sub=1)
                side = -1 if on_l else 1
                attach(flag_obj(coll, ccx, ccy, 2.98, 0.4, 0.24, flagc, side, "flag_%d_%d_%d" % (fi, x, y)), gid)
                continue
            Hc = Hw + (0.2 if gate else 0.0)
            _brick_cell(mb, X0, Y0, cs, 0.12, Hc, nb_h and not nb_v, rng, stones)
            box(mb, X0, X0 + cs, Y0, Y0 + cs, Hc - 0.02, Hc + 0.04, C(dark, 1.35), top=C(cap, 0.96))       # 墙顶走道
            outer = 'w' if on_l else 'e' if on_r else 'n' if on_t else 's'
            if gate:                                 # 门楼：比墙高一截，四角各一个垛
                for (ax, ay) in ((0, 0), (1, 0), (0, 1), (1, 1)):
                    xa = X0 + ax * (cs - 0.15)
                    ya = Y0 + ay * (cs - 0.15)
                    box(mb, xa, xa + 0.15, ya, ya + 0.15, Hc + 0.04, Hc + 0.3, C(stones[(ax + ay) % 4], 1.02), top=cap)
                # 朝外的一面挂一盏灯
                ox = {'w': -1, 'e': 1}.get(outer, 0)
                oy = {'n': 1, 's': -1}.get(outer, 0)
                lx, ly = X0 + cs / 2 + ox * (cs / 2 + 0.03), Y0 + cs / 2 + oy * (cs / 2 + 0.03)
                box(mb, lx - 0.045, lx + 0.045, ly - 0.045, ly + 0.045, 0.86, 1.0, C("#FFD27A"), mat=M_GLOW)
                box(mb, lx - 0.06, lx + 0.06, ly - 0.06, ly + 0.06, 1.0, 1.03, C("#3A3040"))
            else:
                merlon_row(mb, X0, Y0, outer, Hc + 0.04)
                inner = {'w': 'e', 'e': 'w', 'n': 's', 's': 'n'}[outer]
                if inner in 'ns':
                    ya, yb = (Y0 + cs - 0.06, Y0 + cs) if inner == 'n' else (Y0, Y0 + 0.06)
                    box(mb, X0, X0 + cs, ya, yb, Hc + 0.04, Hc + 0.1, C(stones[1]), top=cap)
                else:
                    xa, xb = (X0 + cs - 0.06, X0 + cs) if inner == 'e' else (X0, X0 + 0.06)
                    box(mb, xa, xb, Y0, Y0 + cs, Hc + 0.04, Hc + 0.1, C(stones[1]), top=cap)
                # 队旗幡：朝南（镜头）那面墙挂在门楼两边，朝广场那面墙挂在三分之一、三分之二处；贴着墙面不探出
                face_cam = outer == 's'
                face_plaza = (outer == 'e' and left) or (outer == 'w' and not left)

                def is_gate(xx, yy):
                    return (xx, yy) in cset and (not isw(xx - 1, yy) or not isw(xx + 1, yy)) and bx0 < xx < bx1 - 1
                hang = False
                if face_cam:
                    hang = is_gate(x - 1, y) or is_gate(x + 1, y)
                elif face_plaza:
                    L_ = by1 - by0
                    hang = (y - by0) in (L_ // 3, L_ - 1 - L_ // 3)
                if hang:
                    _banner(mb, X0, Y0, cs, outer, Hc, flagc)
    # 广场上的别的墙块
    for i, comp in enumerate(others):
        for (x0, y0, w, h) in PR.rects_of(comp):
            cells = [(x, y) for y in range(y0, y0 + h) for x in range(x0, x0 + w)]
            gid = router((x0 + w / 2) * cs, (y0 + h / 2) * cs)
            X0, Y0, Wd, Hd = x0 * cs, -(y0 + h) * cs, w * cs, h * cs
            mb = MeshBuilder()
            if min(Wd, Hd) >= 0.9:
                planter(mb, X0, Y0, Wd, Hd, rng, stones, cap, dark)
            else:
                for (x, y) in cells:
                    _brick_cell(mb, x * cs, -(y + 1) * cs, cs, 0.0, 0.95, Wd > Hd, rng, stones)
                    box(mb, x * cs, (x + 1) * cs, -(y + 1) * cs, -y * cs, 0.93, 0.98, C(dark, 1.35), top=C(cap, 0.96))
                    merlon_row(mb, x * cs, -(y + 1) * cs, 'n' if Wd > Hd else 'e', 0.98, full=True, depth=0.14, hh=0.3)
                box(mb, X0 - 0.015, X0 + Wd + 0.015, Y0 - 0.015, Y0 + Hd + 0.015, -0.04, 0.1, dark, top=C(dark, 1.2))
            PR.split_build(mb, cells, rp, router, attach, coll, "plaza_%d_%d_%d" % (i, x0, y0))
    for gid, mb in mbs.items():
        attach(mb.build("castle_%d" % gid, PR.mats(), coll), gid)
    return {(x, y) for y in range(H) for x in range(W) if grid[y][x] == 1}


def _banner(mb, X0, Y0, cs, outer, Hc, col):
    w, h = 0.26, 0.5
    top = Hc - 0.08
    e = 0.014
    c0, c1 = col, C("#FFE9A8")
    if outer in 'ns':
        yy = (Y0 + cs + e) if outer == 'n' else (Y0 - e)
        xm = X0 + cs / 2
        P = lambda u, v: (xm + u, yy, v)
    else:
        xx = (X0 + cs + e) if outer == 'e' else (X0 - e)
        ym = Y0 + cs / 2
        P = lambda u, v: (xx, ym + u, v)
    quad(mb, [P(-w / 2, top - h), P(w / 2, top - h), P(w / 2, top), P(-w / 2, top)], c0)
    tri(mb, [P(-w / 2, top - h), P(0, top - h - 0.1), P(w / 2, top - h)], c0)              # 下端剪成燕尾
    quad(mb, [P(-0.05, top - 0.32), P(0.05, top - 0.32), P(0.05, top - 0.12), P(-0.05, top - 0.12)], c1)   # 一道金色的纹
    quad(mb, [P(-0.1, top - 0.24), P(0.1, top - 0.24), P(0.1, top - 0.2), P(-0.1, top - 0.2)], c1)
    quad(mb, [P(-w / 2 - 0.03, top - 0.01), P(w / 2 + 0.03, top - 0.01), P(w / 2 + 0.03, top + 0.03), P(-w / 2 - 0.03, top + 0.03)],
         C("#5A4A48"))


def planter(mb, X0, Y0, Wd, Hd, rng, stones, cap, dark):
    """石砌花台 + 修剪成圆球的树：花台 0.55 米、树冠顶到 1.5 米左右，整体不出墙格。"""
    cx, cy = X0 + Wd / 2, Y0 + Hd / 2
    a, b = Wd / 2 - 0.03, Hd / 2 - 0.03
    box(mb, cx - a, cx + a, cy - b, cy + b, -0.04, 0.12, dark, top=C(dark, 1.2))
    for k in range(2):
        za, zb = 0.12 + k * 0.2, 0.12 + (k + 1) * 0.2
        n = 4
        for i in range(n):
            t0, t1 = i / n, (i + 1) / n
            off = 0.5 / n if k % 2 else 0.0
            c = C(stones[(i + k) % 4], rng.uniform(0.95, 1.05))
            box(mb, cx - a + 2 * a * t0 + 0.004, cx - a + 2 * a * t1 - 0.004, cy - b + 0.004, cy + b - 0.004, za + 0.004, zb - 0.004, c,
                top=C(c, 1.08))
    box(mb, cx - a - 0.02, cx + a + 0.02, cy - b - 0.02, cy + b + 0.02, 0.52, 0.58, cap)
    box(mb, cx - a + 0.06, cx + a - 0.06, cy - b + 0.06, cy + b - 0.06, 0.58, 0.6, C("#6E4E3A"))
    g0, g1, g2 = C("#2F8050"), C("#4FA65C"), C("#9AD476")
    R = min(a, b) * 0.86
    blob(mb, cx, cy, 0.58 + R * 0.55, R * 0.55, R * 0.55, R * 0.5, g0, g1, sub=2)
    blob(mb, cx, cy, 0.58 + R * 1.2, R, R, R * 0.78, g0, g2, sub=2)
    for k in range(7):                               # 树冠上几朵小花
        an = 2 * math.pi * k / 7 + rng.uniform(0, 0.4)
        el = rng.uniform(0.2, 0.9)
        px, py = cx + math.cos(an) * R * math.cos(el) * 0.98, cy + math.sin(an) * R * math.cos(el) * 0.98
        pz = 0.58 + R * 1.2 + R * 0.78 * math.sin(el)
        blob(mb, px, py, pz, 0.03, 0.03, 0.025, C("#FFFFFF"), C(["#FFB7D0", "#FFE27A", "#FFFFFF"][k % 3]), sub=1)
    for (sx, sy) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):    # 花台四角的小花丛
        blob(mb, cx + sx * (a - 0.1), cy + sy * (b - 0.1), 0.66, 0.08, 0.08, 0.06, C("#4FA65C"), C("#FF9CC0"), sub=1)


# ================================================================ 贴地小物（材质号：1 草簇 2 花 3 石子 4 叶 5 发光）
def d_obsidian(mb, x, y, rng):
    for k in range(int(rng.integers(1, 4))):
        ox, oy = rng.normal(0, 0.07, 2)
        s = rng.uniform(0.04, 0.08)
        box(mb, x + ox - s, x + ox + s * 0.6, y + oy - s * 0.5, y + oy + s * 0.5, -0.02, s * rng.uniform(0.9, 1.6),
            C(["#2E2A3A", "#3A3048", "#24202E"][k % 3]), top=C("#6A6488"), mat=3, rz=rng.uniform(0, 3.14),
            tilt=(rng.uniform(-0.04, 0.04), rng.uniform(-0.04, 0.04)))


def d_vent(mb, x, y, rng):
    """冒着热气的小火口：一圈深色小石头围着一点岩浆光（很矮，不挡人）。"""
    n = int(rng.integers(5, 8))
    r0 = rng.uniform(0.08, 0.12)
    for k in range(n):
        a = 2 * math.pi * k / n + rng.uniform(-0.2, 0.2)
        s = rng.uniform(0.035, 0.055)
        blob(mb, x + math.cos(a) * r0, y + math.sin(a) * r0, s * 0.4, s, s, s * 0.8, C("#3A3448"), C("#6E6478"), sub=1, mat=3, smooth=False)
    blob(mb, x, y, -0.005, r0 * 0.75, r0 * 0.75, 0.02, C("#FF6A1A"), C("#FFD24A"), sub=1, mat=5)


def d_shells(mb, x, y, rng):
    for k in range(int(rng.integers(1, 4))):
        ox, oy = rng.normal(0, 0.08, 2)
        r = rng.uniform(0.025, 0.04)
        c = C(["#FFF4EC", "#FFD6DC", "#FFE6C8", "#F2C6B4"][int(rng.integers(0, 4))])
        blob(mb, x + ox, y + oy, r * 0.3, r, r * 0.8, r * 0.45, C(c, 0.9), c, sub=1, mat=3, smooth=False, rot=(0, 0, rng.uniform(0, 6.28)))
    if rng.random() < 0.3:                           # 一只小海星
        c = C(["#FF8A5A", "#FFB04A"][int(rng.integers(0, 2))])
        a0 = rng.uniform(0, 6.28)
        for k in range(5):
            a = a0 + 2 * math.pi * k / 5
            tri(mb, [(x + math.cos(a + 0.5) * 0.02, y + math.sin(a + 0.5) * 0.02, 0.012),
                     (x + math.cos(a) * 0.07, y + math.sin(a) * 0.07, 0.008),
                     (x + math.cos(a - 0.5) * 0.02, y + math.sin(a - 0.5) * 0.02, 0.012)], c)
            tri(mb, [(x, y, 0.016), (x + math.cos(a + 0.5) * 0.02, y + math.sin(a + 0.5) * 0.02, 0.012),
                     (x + math.cos(a - 0.5) * 0.02, y + math.sin(a - 0.5) * 0.02, 0.012)], c)


def d_drygrass(mb, x, y, rng):
    PR.d_dryshrub(mb, x, y, rng, cols=("#B8B070", "#F2E6A8"))


# ================================================================ 远景
def _smoke(S, x, y, z, s, rng, n=6, drift=(0.35, 0.12)):
    """一柱往上、往一边飘散的烟团（实心的软团，Q 版画法）。"""
    for k in range(n):
        t = k / max(1, n - 1)
        r = (0.32 + 0.55 * t) * s
        px = x + drift[0] * s * k * (1 + 0.4 * t) + rng.uniform(-0.08, 0.08) * s
        py = y + drift[1] * s * k
        pz = z + (0.25 + 0.62 * k) * s
        c0 = C("#8E7E90", 0.95 + 0.1 * t)
        c1 = C("#D6CAD2")
        blob(S, px, py, pz, r, r * 0.85, r * 0.72, c0, c1, sub=2)
        if k > 1:
            blob(S, px + r * 0.6, py + r * 0.2, pz - r * 0.15, r * 0.6, r * 0.5, r * 0.45, c0, c1, sub=2)


def f_volcano(x, y, z, s, rng, T, L, S):
    """火山锥：一层层深色岩带的锥体，口里是岩浆的光，两三道岩浆顺坡流下，口上冒烟。"""
    bands = [C("#6E5E6E"), C("#5A4E62"), C("#7A6670"), C("#4E4458"), C("#665868")]
    lv = [(1.75 * s, z - 0.15), (1.55 * s, z + 0.35 * s), (1.2 * s, z + 1.1 * s), (0.9 * s, z + 1.9 * s), (0.7 * s, z + 2.5 * s),
          (0.62 * s, z + 2.7 * s), (0.5 * s, z + 2.62 * s)]
    stack(S, x, y, lv, bands, n=13, jitter=0.07, rng=rng, rot0=rng.uniform(0, 6.28), cap=C("#FF8A2A"))
    V = [(x, y, z + 2.55 * s)] + [(x + 0.5 * s * math.cos(2 * math.pi * i / 13), y + 0.5 * s * math.sin(2 * math.pi * i / 13),
                                   z + 2.6 * s) for i in range(13)]
    S.add(np.array(V), [(0, 1 + i, 1 + (i + 1) % 13) for i in range(13)], mat=M_GLOW,
          vcolors=np.array([C("#FFE27A")] + [C("#FF6A1A")] * 13), smooth=False)
    for k in range(3):                                   # 顺坡流下的岩浆
        a = -math.pi / 2 + (k - 1) * 0.75 + rng.uniform(-0.2, 0.2)
        pts = []
        for i in range(7):
            t = i / 6
            rr = (0.58 + 1.05 * t) * s
            zz = z + (2.62 - 2.45 * t ** 0.95) * s
            aa = a + 0.15 * math.sin(t * 5 + k)
            pts.append((x + rr * math.cos(aa), y + rr * math.sin(aa), zz + 0.03))
        for i in range(len(pts) - 1):
            p, q = np.array(pts[i]), np.array(pts[i + 1])
            d = q - p
            nrm = np.array([-d[1], d[0], 0.0])
            nrm /= max(1e-6, np.linalg.norm(nrm))
            w0 = (0.12 - 0.07 * i / 6) * s
            w1 = (0.12 - 0.07 * (i + 1) / 6) * s
            quad(S, [p - nrm * w0, p + nrm * w0, q + nrm * w1, q - nrm * w1], C("#FF7A2A" if i % 2 else "#FFB040"), M_GLOW)
    _smoke(S, x + 0.05 * s, y, z + 2.6 * s, s * 0.9, rng, n=6)


def f_smoke(x, y, z, s, rng, T, L, S):
    """冒烟的小火口：一圈黑石头，中间一汪岩浆，上面一柱烟。"""
    for k in range(9):
        a = 2 * math.pi * k / 9
        blob(S, x + math.cos(a) * 0.6 * s, y + math.sin(a) * 0.5 * s, z + 0.1 * s, 0.26 * s, 0.24 * s, 0.24 * s, C("#3E3648"), C("#6E6478"),
             sub=1, smooth=False)
    blob(S, x, y, z + 0.02, 0.5 * s, 0.4 * s, 0.06 * s, C("#FF6A1A"), C("#FFD24A"), sub=2, mat=M_GLOW)
    _smoke(S, x, y, z + 0.1 * s, s * 0.75, rng, n=6, drift=(0.3, 0.15))


def far_basalt(x, y, z, s, rng, T, L, S, pal=None):
    n = int(rng.integers(3, 7))
    for k in range(n):
        a = rng.uniform(0, 6.28)
        rr = rng.uniform(0.0, 0.3) * s if k else 0.0
        h = rng.uniform(0.6, 1.5) * s * (1.0 if k else 1.25)
        hex_col(S, x + math.cos(a) * rr, y + math.sin(a) * rr, 0.16 * s, h, C("#575670", rng.uniform(0.9, 1.1)), C("#9A92A0"), z=z,
                rot=math.pi / 6)


def far_charred(x, y, z, s, rng, T, L, S, pal=None):
    c = C("#3E3440")
    h = rng.uniform(1.0, 1.5) * s
    tube(T, [(x, y, z - 0.05), (x + 0.04 * s, y, z + h * 0.5), (x - 0.03 * s, y, z + h)], 0.09 * s, c, r1=0.03 * s)
    for k in range(int(rng.integers(2, 4))):
        a = rng.uniform(0, 6.28)
        zb = z + h * rng.uniform(0.45, 0.8)
        L_ = rng.uniform(0.3, 0.5) * s
        tip = (x + math.cos(a) * L_, y + math.sin(a) * L_, zb + L_ * 0.6)
        tube(T, [(x, y, zb), tip], 0.04 * s, c, r1=0.012 * s)
        if rng.random() < 0.5:                           # 枝头一点余烬
            blob(S, tip[0], tip[1], tip[2], 0.035 * s, 0.035 * s, 0.035 * s, C("#FF6A1A"), C("#FFC24A"), sub=1, mat=M_GLOW)


def far_emberbush(x, y, z, s, rng, T, L, S, pal=None):
    SL.make_bush(x, y, z, s * 1.1, rng, L, palette=("#A8503E", "#F0904E"))


def f_lighthouse(x, y, z, s, rng, T, L, S):
    """灯塔：石基 + 红白相间的塔身 + 走廊 + 发光的灯室 + 红顶。"""
    stack(S, x, y, [(0.75 * s, z - 0.1), (0.7 * s, z + 0.3 * s), (0.55 * s, z + 0.36 * s)], [C("#B8AE9E"), C("#D8D0C0")], n=10, cap=C("#D8D0C0"))
    H = 3.0 * s
    nb = 6
    lv = []
    cols = []
    for k in range(nb + 1):
        t = k / nb
        lv.append(((0.46 - 0.14 * t) * s, z + 0.36 * s + H * t))
        if k < nb:
            cols.append(C("#E2503C") if k % 2 else C("#FFFFFF"))
    stack(S, x, y, lv, cols, n=14, smooth=True)
    zt = z + 0.36 * s + H
    stack(S, x, y, [(0.48 * s, zt), (0.48 * s, zt + 0.08 * s)], C("#3A3440"), n=14)
    for k in range(12):                                  # 走廊栏杆
        a = 2 * math.pi * k / 12
        px, py = x + math.cos(a) * 0.45 * s, y + math.sin(a) * 0.45 * s
        box(S, px - 0.015 * s, px + 0.015 * s, py - 0.015 * s, py + 0.015 * s, zt + 0.08 * s, zt + 0.28 * s, C("#3A3440"))
    stack(S, x, y, [(0.26 * s, zt + 0.08 * s), (0.26 * s, zt + 0.55 * s)], C("#FFE27A"), n=10, mat=M_GLOW, cap=C("#FFE27A"))
    stack(S, x, y, [(0.36 * s, zt + 0.55 * s), (0.2 * s, zt + 0.75 * s), (0.02 * s, zt + 0.95 * s)], [C("#D9473A"), C("#E8604A")], n=10)
    blob(S, x, y, zt + 1.0 * s, 0.05 * s, 0.05 * s, 0.05 * s, C("#3A3440"), C("#5A5060"), sub=1)


def f_boat(x, y, z, s, rng, T, L, S):
    """靠在小码头边的帆船：一小汪水、木头小码头、白船身蓝船舷、桅杆和三角帆。"""
    n = 20
    V = [(x, y, z + 0.03)] + [(x + 1.05 * s * math.cos(2 * math.pi * i / n), y + 0.75 * s * math.sin(2 * math.pi * i / n), z + 0.03)
                              for i in range(n)]
    S.add(np.array(V), [(0, 1 + i, 1 + (i + 1) % n) for i in range(n)], mat=M_SOLID,
          vcolors=np.array([C("#4FB8DA")] + [C("#8FE0F0")] * n), smooth=False)
    box(S, x - 1.1 * s, x - 0.35 * s, y - 0.62 * s, y - 0.4 * s, z - 0.05, z + 0.12 * s, C("#B8875A"), top=C("#D8A870"))
    hull, rim = C("#FFFFFF"), C("#2E6E9E")
    m = 14
    prof = []
    for i in range(m):
        a = 2 * math.pi * i / m
        px, py = math.cos(a) * 0.75 * s, math.sin(a) * 0.24 * s
        if px > 0:
            py *= 1 - 0.6 * (px / (0.75 * s)) ** 2
        prof.append((px, py))
    lv = [(0.6, z - 0.02), (0.92, z + 0.14 * s), (1.0, z + 0.3 * s)]
    rings = [[(x + px * k, y + py * k, zz) for (px, py) in prof] for (k, zz) in lv]
    for li in range(2):
        for i in range(m):
            j = (i + 1) % m
            quad(S, [rings[li][i], rings[li][j], rings[li + 1][j], rings[li + 1][i]], hull if li == 0 else rim)
    top = rings[-1]
    ctr = (x, y, z + 0.24 * s)
    for i in range(m):
        tri(S, [top[i], top[(i + 1) % m], ctr], C("#C9A070"))
    tube(S, [(x - 0.05 * s, y, z + 0.2 * s), (x - 0.05 * s, y, z + 1.9 * s)], 0.03 * s, C("#8A6A4A"), seg=6)
    tri(S, [(x - 0.03 * s, y + 0.01, z + 0.45 * s), (x + 0.62 * s, y + 0.01, z + 0.45 * s), (x - 0.03 * s, y + 0.01, z + 1.8 * s)],
        C("#FFF8EC"))
    tri(S, [(x - 0.08 * s, y - 0.01, z + 0.5 * s), (x - 0.08 * s, y - 0.01, z + 1.5 * s), (x - 0.55 * s, y - 0.01, z + 0.5 * s)],
        C("#F2E2C8"))
    tri(S, [(x - 0.05 * s, y, z + 1.9 * s), (x + 0.25 * s, y, z + 1.84 * s), (x - 0.05 * s, y, z + 1.78 * s)], C("#E0503C"))


def far_seastack(x, y, z, s, rng, T, L, S, pal=None):
    cols = [C("#D8C8B0"), C("#C0AE98"), C("#E6D8C0"), C("#B8A48E")]
    h = rng.uniform(0.9, 1.7) * s
    stack(S, x, y, [(0.42 * s, z - 0.1), (0.38 * s, z + h * 0.4), (0.3 * s, z + h * 0.8), (0.26 * s, z + h)], cols, n=7, jitter=0.12,
          rng=rng, rot0=rng.uniform(0, 6.28), cap=C("#FFFFFF"))
    SL.make_bush(x + 0.1 * s, y, z + h, s * 0.5, rng, L, palette=("#5FAE70", "#8FCB7E"))


def f_keep_tower(x, y, z, s, rng, T, L, S):
    """远处城里的圆塔：石头塔身、一圈垛口、尖顶（跟着它在哪一边：左边的塔用左边那队的屋顶色）、顶上一面小旗。"""
    st = [C("#E6DED0"), C("#D2C8B8"), C("#EEE8DC")]
    H = rng.uniform(2.4, 3.2) * s
    r = 0.5 * s
    stack(S, x, y, [(r * 1.05, z - 0.1), (r, z + H * 0.5), (r * 0.96, z + H)], [st[0], st[1]], n=14, smooth=True)
    stack(S, x, y, [(r * 0.96, z + H), (r * 1.15, z + H + 0.1 * s), (r * 1.15, z + H + 0.25 * s)], [C("#A89E90"), st[2]], n=14, cap=st[2])
    int(rng.integers(0, 3))                     # 原来按这个随机数挑红 / 蓝；照样取一次，后面的随机摆放不变
    roof = C(side_cols(x < SIDES.get("mid_x", 0.0))[1])
    stack(S, x, y, [(r * 1.05, z + H + 0.25 * s), (0.02, z + H + 1.4 * s)], [roof], n=14)
    for k in range(3):
        a = -math.pi / 2 + (k - 1) * 0.6
        px, py = x + math.cos(a) * r, y + math.sin(a) * r
        box(S, px - 0.05 * s, px + 0.05 * s, py - 0.02 * s, py + 0.02 * s, z + H * (0.35 + 0.2 * k), z + H * (0.35 + 0.2 * k) + 0.22 * s,
            C("#3A3448"), rz=a + math.pi / 2)
    tube(S, [(x, y, z + H + 1.35 * s), (x, y, z + H + 1.75 * s)], 0.02 * s, C("#5A4A48"), seg=5)
    quad(S, [(x, y, z + H + 1.73 * s), (x + 0.4 * s, y, z + H + 1.68 * s), (x + 0.4 * s, y, z + H + 1.5 * s), (x, y, z + H + 1.55 * s)],
         roof)


# ---- 登记
PR.FAR.update(basalt=far_basalt, charred=far_charred, emberbush=far_emberbush, seastack=far_seastack)
PR.FEATURES.update(volcano=f_volcano, smoke=f_smoke, lighthouse=f_lighthouse, boat=f_boat, keep_tower=f_keep_tower)
PR.FEATURE_SCALE.update(volcano=1.15, smoke=1.0, lighthouse=1.0, boat=1.0, keep_tower=1.0)
PR.DETAILS.update(obsidian=d_obsidian, vent=d_vent, shells=d_shells, drygrass=d_drygrass)
