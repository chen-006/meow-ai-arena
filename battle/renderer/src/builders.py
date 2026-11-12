# -*- coding: utf-8 -*-
"""地形上的物件（全部是方块拼的，和兵是一套造型语言）：木桥、房子、钟楼、树、礁石，以及场外布景的散布。
都往 MeshBuilder 里加带顶点色的面，材质号 2（mats.py 的顶点色材质）。坐标：Blender X = x，Y = z，Z = y。"""
import math

import numpy as np

from scene_lib import hexc

VC = 2          # 顶点色材质号
_FACES = ((0, 1, 2, 3), (4, 7, 6, 5), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0))


def C(h, k=1.0):
    c = hexc(h) if isinstance(h, str) else h
    return (c[0] * k, c[1] * k, c[2] * k, 1.0)


def rng_of(*a):
    s = 1469598103
    for v in a:
        s = (s * 1099511 + int(v * 1000) & 0xFFFFFFFF) & 0xFFFFFFFF
    return np.random.default_rng(s)


def vbox(b, x0, x1, y0, y1, z0, z1, col, top=None, rz=0.0, about=None, mat=VC, tilt=None):
    """一个方块（Blender 坐标：x0..x1, y0..y1, z0..z1）。top：顶面另一个颜色。rz：绕 about（默认自己中心）的竖轴转角。"""
    V = np.array([(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
                  (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], dtype=float)
    if tilt is not None:
        cx, cy, cz = (x0 + x1) / 2, (y0 + y1) / 2, z0
        ax, ay = tilt
        Rx = np.array([[1, 0, 0], [0, math.cos(ax), -math.sin(ax)], [0, math.sin(ax), math.cos(ax)]])
        Ry = np.array([[math.cos(ay), 0, math.sin(ay)], [0, 1, 0], [-math.sin(ay), 0, math.cos(ay)]])
        V = (V - (cx, cy, cz)) @ (Ry @ Rx).T + (cx, cy, cz)
    if rz:
        cx, cy = about if about is not None else ((x0 + x1) / 2, (y0 + y1) / 2)
        c, s = math.cos(rz), math.sin(rz)
        dx, dy = V[:, 0] - cx, V[:, 1] - cy
        V[:, 0], V[:, 1] = cx + dx * c - dy * s, cy + dx * s + dy * c
    col = C(col) if isinstance(col, str) else col
    tcol = (C(top) if isinstance(top, str) else top) if top is not None else col
    for k, f in enumerate(_FACES):
        cc = tcol if k == 1 else col
        b.add(V[list(f)], [(0, 1, 2, 3)], mat=mat, color=cc, smooth=False)


def cube(b, x, y, z, s, col, top=None, rz=0.0):
    vbox(b, x - s / 2, x + s / 2, y - s / 2, y + s / 2, z, z + s, col, top=top, rz=rz)


# ================================================================ 木桥
def bridge(b, p, L, narrow=False):
    """木板桥：横铺的木板（板缝一点点），底下两根纵梁；没有栏杆（掉下去就出局，所以不画栏杆）。"""
    c, h = p["c"], p["h"]
    top = c[1] + h[1]
    wood = L["wood"]
    along_x = h[0] >= h[2]
    Lh, Wh = (h[0], h[2]) if along_x else (h[2], h[0])
    pw = 0.32
    n = max(1, int(round(2 * Lh / pw)))
    pw = 2 * Lh / n
    r = rng_of(c[0], c[2], 3)
    for k in range(n):
        u0 = -Lh + k * pw + 0.02
        u1 = -Lh + (k + 1) * pw - 0.02
        jag = r.uniform(-0.06, 0.06)
        w0, w1 = -Wh - 0.05 + jag, Wh + 0.05 + jag
        col = wood[k % 3 if k % 5 else 1]
        dz = r.uniform(-0.02, 0.0)
        if along_x:
            vbox(b, c[0] + u0, c[0] + u1, c[2] + w0, c[2] + w1, top - 0.14 + dz, top + dz, col)
        else:
            vbox(b, c[0] + w0, c[0] + w1, c[2] + u0, c[2] + u1, top - 0.14 + dz, top + dz, col)
    # 纵梁
    for s in ((-0.7, -0.45) if not narrow else (-0.25, 0.0)), ((0.45, 0.7) if not narrow else (0.0, 0.25)):
        a0, a1 = s[0] * Wh * 1.3, s[1] * Wh * 1.3
        if along_x:
            vbox(b, c[0] - Lh, c[0] + Lh, c[2] + a0, c[2] + a1, top - 0.45, top - 0.14, "#5E3E26")
        else:
            vbox(b, c[0] + a0, c[0] + a1, c[2] - Lh, c[2] + Lh, top - 0.45, top - 0.14, "#5E3E26")


# ================================================================ 房子、钟楼
def house(b, p, L):
    c, h = p["c"], p["h"]
    x0, x1 = c[0] - h[0], c[0] + h[0]
    y0, y1 = c[2] - h[2], c[2] + h[2]
    zb, zt = c[1] - h[1], c[1] + h[1]
    wall, roof = L["wall"], L["roof"]
    r = rng_of(c[0], c[2])
    eave = zb + (zt - zb) * 0.62
    vbox(b, x0, x1, y0, y1, zb, zb + 0.6, wall[2])                 # 石基
    vbox(b, x0 + 0.05, x1 - 0.05, y0 + 0.05, y1 - 0.05, zb + 0.6, eave, wall[0])
    # 双坡屋顶（沿长边起脊），每坡一层层瓦（用阶梯方块表示）
    along_x = (x1 - x0) >= (y1 - y0)
    rc = roof[int(r.integers(0, 2))]
    steps = 5
    span = (y1 - y0) / 2 + 0.25 if along_x else (x1 - x0) / 2 + 0.25
    rise = zt - eave
    for k in range(steps):
        t0 = k / steps
        w = span * (1 - t0)
        zz = eave + rise * t0
        col = rc if k % 2 == 0 else roof[1]
        if along_x:
            cy = (y0 + y1) / 2
            vbox(b, x0 - 0.3, x1 + 0.3, cy - w, cy + w, zz, zz + rise / steps + 0.02, col)
        else:
            cx = (x0 + x1) / 2
            vbox(b, cx - w, cx + w, y0 - 0.3, y1 + 0.3, zz, zz + rise / steps + 0.02, col)
    # 门、窗（深色薄片）
    dark = "#4A3A34"
    for side in (-1, 1):
        if along_x:
            yy = y0 - 0.02 if side < 0 else y1 + 0.02
            for k in range(int((x1 - x0) // 2.2)):
                xx = x0 + 1.1 + k * 2.2
                vbox(b, xx - 0.35, xx + 0.35, min(yy, yy + 0.04), max(yy, yy + 0.04), zb + 1.6, zb + 2.4, dark)
                vbox(b, xx - 0.45, xx + 0.45, min(yy, yy + 0.06) - 0.03, max(yy, yy + 0.06), zb + 1.5, zb + 1.6, "#FFFFFF")
        else:
            xx = x0 - 0.02 if side < 0 else x1 + 0.02
            for k in range(int((y1 - y0) // 2.2)):
                yy = y0 + 1.1 + k * 2.2
                vbox(b, min(xx, xx + 0.04), max(xx, xx + 0.04), yy - 0.35, yy + 0.35, zb + 1.6, zb + 2.4, dark)
    # 烟囱
    vbox(b, x0 + 0.8, x0 + 1.4, (y0 + y1) / 2 + 0.3, (y0 + y1) / 2 + 0.9, eave + rise * 0.4, zt + 0.5, wall[2], top="#3A3434")


def bell_tower(b, p, L):
    c, h = p["c"], p["h"]
    x0, x1 = c[0] - h[0], c[0] + h[0]
    y0, y1 = c[2] - h[2], c[2] + h[2]
    zb, zt = c[1] - h[1], c[1] + h[1]
    wall, roof = L["wall"], L["roof"]
    body_top = zt - 3.5
    vbox(b, x0, x1, y0, y1, zb, zb + 1.0, wall[2])
    vbox(b, x0 + 0.1, x1 - 0.1, y0 + 0.1, y1 - 0.1, zb + 1.0, body_top, wall[0])
    for k in range(int((body_top - zb) // 1.6)):                        # 一圈圈的石带
        z = zb + 1.0 + k * 1.6
        vbox(b, x0 + 0.05, x1 - 0.05, y0 + 0.05, y1 - 0.05, z, z + 0.18, wall[1])
    # 钟室：四角柱子 + 中间一口金钟
    for (xa, ya) in ((x0, y0), (x1 - 0.7, y0), (x0, y1 - 0.7), (x1 - 0.7, y1 - 0.7)):
        vbox(b, xa, xa + 0.7, ya, ya + 0.7, body_top, body_top + 2.0, wall[0])
    cx, cy = c[0], c[2]
    vbox(b, cx - 0.6, cx + 0.6, cy - 0.6, cy + 0.6, body_top + 0.6, body_top + 1.7, "#E8B93A", top="#C9962A")
    vbox(b, x0 - 0.2, x1 + 0.2, y0 - 0.2, y1 + 0.2, body_top + 2.0, body_top + 2.35, wall[2])
    for k in range(5):                                                  # 尖顶
        w = (x1 - x0) / 2 + 0.2 - k * 0.45
        z = body_top + 2.35 + k * 0.45
        vbox(b, cx - w, cx + w, cy - w, cy + w, z, z + 0.47, roof[0] if k % 2 == 0 else roof[1])
    vbox(b, cx - 0.06, cx + 0.06, cy - 0.06, cy + 0.06, body_top + 4.6, body_top + 5.3, "#E8B93A")


# ================================================================ 树
def tree(b, x, y, z0, r, z1, L, style=None, seed=0):
    """引擎里的树是一根圆柱（挡箭挡马的就是这根树干）。画：方块树干（宽 = 碰撞直径）+ 方块树冠，树冠底在 2.2 米以上，不挡人。"""
    style = style or L["tree"][0]
    rg = rng_of(x, y, seed)
    lc = L["leaf"]
    tr = L.get("trunk", "#8A6048")
    w = max(0.5, r * 1.8)
    top = max(z1, z0 + 4.0)
    if style in ("pine", "snowpine"):
        vbox(b, x - w / 2, x + w / 2, y - w / 2, y + w / 2, z0, z0 + 2.2, tr)
        tiers = 5
        span = top - z0 - 1.7
        rot0 = rg.uniform(0, 0.8)
        for k in range(tiers):
            f = k / (tiers - 1)
            s = (2.5 - 1.9 * f) * rg.uniform(0.92, 1.06)
            zz = z0 + 1.7 + k * span / tiers
            hh = span / tiers + 0.25
            col = lc[1] if k % 2 == 0 else lc[0]
            tcol = lc[2] if style == "snowpine" else lc[1]
            vbox(b, x - s / 2, x + s / 2, y - s / 2, y + s / 2, zz, zz + hh, col, top=tcol, rz=rot0 + (k % 2) * math.pi / 4)
        vbox(b, x - 0.18, x + 0.18, y - 0.18, y + 0.18, top + 0.2, top + 0.7, lc[2] if style == "snowpine" else lc[1])
        return
    if style == "cypress":
        vbox(b, x - w / 2, x + w / 2, y - w / 2, y + w / 2, z0, z0 + 2.3, tr)
        for k in range(4):
            s = 1.5 - 0.25 * abs(k - 1.2)
            zz = z0 + 2.0 + k * 1.0
            vbox(b, x - s / 2, x + s / 2, y - s / 2, y + s / 2, zz, zz + 1.1, lc[k % 2], top=lc[2])
        return
    if style == "palm":
        lean = rg.uniform(-0.5, 0.5)
        for k in range(7):
            zz = z0 + k * 0.75
            dx = lean * (k / 7) ** 1.5
            vbox(b, x + dx - w / 2, x + dx + w / 2, y - w / 2, y + w / 2, zz, zz + 0.75, tr if k % 2 else "#8A6440")
        cx = x + lean
        zt = z0 + 5.25
        for k in range(6):
            a = 2 * math.pi * k / 6 + rg.uniform(0, 0.5)
            vbox(b, cx - 0.3, cx + 2.2, y - 0.35, y + 0.35, zt - 0.25, zt + 0.1, lc[k % 2], top=lc[2], rz=a,
                 about=(cx, y), tilt=(0.0, 0.25))
        vbox(b, cx - 0.4, cx + 0.4, y - 0.4, y + 0.4, zt - 0.3, zt + 0.3, "#6E5030")
        return
    if style == "juniper":
        vbox(b, x - w / 2, x + w / 2, y - w / 2, y + w / 2, z0, z0 + 2.4, tr)
        for k in range(5):
            a = rg.uniform(0, 2 * math.pi)
            rr = rg.uniform(0.3, 1.2)
            s = rg.uniform(1.2, 1.9)
            zz = z0 + 2.2 + rg.uniform(0, 1.2)
            cube(b, x + rr * math.cos(a), y + rr * math.sin(a), zz, s, lc[k % 3], top=lc[2])
        return
    if style == "bare":
        vbox(b, x - w / 2, x + w / 2, y - w / 2, y + w / 2, z0, top - 0.5, "#7A6A5E")
        for k in range(4):
            a = 2 * math.pi * k / 4 + rg.uniform(0, 0.6)
            zz = z0 + 2.6 + k * 0.6
            vbox(b, x, x + 1.4, y - 0.12, y + 0.12, zz, zz + 0.24, "#7A6A5E", rz=a, about=(x, y), tilt=(0.0, -0.5))
        return
    # round / sakura：一团方块
    vbox(b, x - w / 2, x + w / 2, y - w / 2, y + w / 2, z0, z0 + 2.8, tr if style != "sakura" else "#6A4A44")
    # 树冠：一大块 + 几块小的，下暗上亮
    zc = z0 + 2.6
    cube(b, x, y, zc, 2.0, lc[0], top=lc[1], rz=rg.uniform(0, 1.0))
    n = int(rg.integers(3, 6))
    for k in range(n):
        a = rg.uniform(0, 2 * math.pi)
        rr = rg.uniform(0.5, 1.0)
        s = rg.uniform(0.9, 1.4)
        zz = zc + rg.uniform(0.6, 1.6)
        cube(b, x + rr * math.cos(a), y + rr * math.sin(a), zz, s, lc[1], top=lc[2], rz=rg.uniform(0, 1.0))
    cube(b, x, y, zc + 1.8, 1.1, lc[1], top=lc[2], rz=rg.uniform(0, 1.0))


def reef(b, x, y, r, z0, z1, L):
    rk = L["rock"]
    rg = rng_of(x, y, 5)
    zz = z0
    k = 0
    while zz < z1:
        s = r * 2 * (1.0 - 0.12 * k) * rg.uniform(0.9, 1.05)
        hh = rg.uniform(1.0, 1.8)
        vbox(b, x - s / 2, x + s / 2, y - s / 2, y + s / 2, zz, min(z1, zz + hh), rk[k % 2], top=rk[0], rz=rg.uniform(0, 0.8))
        zz += hh
        k += 1


# ================================================================ 场外布景
def boulder(b, x, y, z, s, L, rg):
    rk = L["rock"]
    vbox(b, x - s * 0.6, x + s * 0.6, y - s * 0.5, y + s * 0.5, z - 0.1, z + s * 0.7, rk[1], top=rk[0], rz=rg.uniform(0, 1.6))
    if s > 0.8:
        vbox(b, x - s * 0.3, x + s * 0.4, y - s * 0.3, y + s * 0.3, z + s * 0.6, z + s * 1.0, rk[1], top=rk[0],
             rz=rg.uniform(0, 1.6))


def tuft(b, x, y, z, L, rg, s=1.0):
    lc = L["leaf"] if L["name"] not in ("glacier", "dunes", "canyon") else L["leaf"]
    g = L["ground"]
    for k in range(3):
        dx, dy = rg.uniform(-0.12, 0.12) * s, rg.uniform(-0.12, 0.12) * s
        hh = rg.uniform(0.18, 0.38) * s
        col = g[0] if k == 0 else g[1]
        vbox(b, x + dx - 0.04 * s, x + dx + 0.04 * s, y + dy - 0.04 * s, y + dy + 0.04 * s, z, z + hh, C(col, 0.85),
             top=g[2])


def flower(b, x, y, z, L, rg):
    ac = L["accent"]
    vbox(b, x - 0.02, x + 0.02, y - 0.02, y + 0.02, z, z + 0.22, L["ground"][0])
    cube(b, x, y, z + 0.2, 0.12, ac[int(rg.integers(0, len(ac)))])


def spire(b, x, y, z, h, s, L, rg):
    """红岩峡谷的风蚀岩柱：一层层的条纹方块，往上收。"""
    cl = L["cliff"]
    zz = z
    k = 0
    while zz < z + h:
        hh = rg.uniform(0.8, 1.6)
        ss = s * (1 - 0.35 * (zz - z) / h) * rg.uniform(0.92, 1.05)
        vbox(b, x - ss / 2, x + ss / 2, y - ss / 2, y + ss / 2, zz, zz + hh, cl[k % 3], top=cl[0], rz=rg.uniform(-0.15, 0.15))
        zz += hh
        k += 1
    vbox(b, x - s * 0.45, x + s * 0.45, y - s * 0.45, y + s * 0.45, zz, zz + 0.6, cl[2], top=cl[1])


def mesa(b, x, y, z, h, s, L, rg):
    """场外的红岩台地：几层叠起来、往上略收的大方块，侧面一层层岩带。"""
    cl = L["cliff"]
    zz = z - 1.0
    w, d = s * rg.uniform(0.8, 1.3), s * rg.uniform(0.8, 1.3)
    rz = rg.uniform(0, 0.6)
    k = 0
    while zz < z + h:
        hh = rg.uniform(1.2, 2.6)
        f = 1 - 0.18 * (zz - z) / h
        vbox(b, x - w * f / 2, x + w * f / 2, y - d * f / 2, y + d * f / 2, zz, min(z + h, zz + hh), cl[k % 3], top=cl[0],
             rz=rz)
        zz += hh
        k += 1


def ruin(b, x, y, z, L, rg):
    """古垣的残迹（场外）：两三根方石柱（一节节的，略错开），一根搭着横梁，地上倒着几节。"""
    wl = L["wall"]
    n = int(rg.integers(2, 4))
    tops = []
    for k in range(n):
        px, py = x + (k - (n - 1) / 2) * 2.6, y + rg.uniform(-0.4, 0.4)
        hh = rg.uniform(2.0, 4.8)
        zz = z - 0.2
        vbox(b, px - 0.65, px + 0.65, py - 0.65, py + 0.65, zz, zz + 0.4, wl[2], top=wl[1])
        zz += 0.4
        while zz < z + hh:
            d = rg.uniform(0.7, 1.0)
            vbox(b, px - 0.45, px + 0.45, py - 0.45, py + 0.45, zz, zz + d, wl[0] if rg.uniform() < 0.6 else wl[1],
                 top=wl[0], rz=rg.uniform(-0.08, 0.08))
            zz += d
        tops.append((px, py, zz))
    if n >= 2 and abs(tops[0][2] - tops[1][2]) < 1.2:
        zt = max(tops[0][2], tops[1][2])
        vbox(b, tops[0][0] - 0.6, tops[1][0] + 0.6, tops[0][1] - 0.4, tops[0][1] + 0.4, zt, zt + 0.55, wl[1], top=wl[0])
    for k in range(3):
        a = rg.uniform(0, 2 * math.pi)
        fx, fy = x + rg.uniform(-3, 3), y + rg.uniform(1.0, 2.6)
        vbox(b, fx - 0.45, fx + 0.45, fy - 0.42, fy + 0.42, z - 0.1, z + 0.8, wl[1], top=wl[0], rz=a)


def ice_shard(b, x, y, z, s, L, rg):
    rk = L["rock"]
    for k in range(int(rg.integers(2, 4))):
        h = s * rg.uniform(1.0, 2.4)
        w = s * rg.uniform(0.35, 0.6)
        dx, dy = rg.uniform(-0.4, 0.4) * s, rg.uniform(-0.4, 0.4) * s
        vbox(b, x + dx - w / 2, x + dx + w / 2, y + dy - w / 2, y + dy + w / 2, z - 0.2, z + h, rk[1], top=rk[0],
             tilt=(rg.uniform(-0.3, 0.3), rg.uniform(-0.3, 0.3)), rz=rg.uniform(0, 1.5))


def reeds(b, x, y, z, rg):
    for k in range(5):
        dx, dy = rg.uniform(-0.3, 0.3), rg.uniform(-0.3, 0.3)
        hh = rg.uniform(0.5, 1.0)
        vbox(b, x + dx - 0.03, x + dx + 0.03, y + dy - 0.03, y + dy + 0.03, z, z + hh, "#C9A05A", top="#F6E3AC",
             tilt=(rg.uniform(-0.2, 0.2), rg.uniform(-0.2, 0.2)))


# ================================================================ 沙洲（涨潮沙洲）的布景：都很矮、很小，只是布景，不挡路
SHELL_COLS = ("#FFF6EA", "#F7C9B8", "#F2D7A6", "#E8B4C8", "#FFFFFF", "#F4B48C")


def shell(b, x, y, z, rg):
    """贝壳：一片小扇形（两三块薄方块）或一个小螺（两块叠起来的小方块），十几厘米。"""
    c = SHELL_COLS[int(rg.integers(len(SHELL_COLS)))]
    a = rg.uniform(0, math.pi)
    if rg.uniform() < 0.65:
        s = rg.uniform(0.13, 0.22)
        vbox(b, x - s / 2, x + s / 2, y - s * 0.35, y + s * 0.35, z - 0.01, z + 0.05, c, top=C(c, 1.06), rz=a)
        vbox(b, x - s * 0.3, x + s * 0.3, y - s * 0.55, y - s * 0.2, z - 0.01, z + 0.045, C(c, 0.9), rz=a, about=(x, y))
    else:
        s = rg.uniform(0.1, 0.16)
        vbox(b, x - s / 2, x + s / 2, y - s / 2, y + s / 2, z - 0.01, z + s * 0.7, c, top=C(c, 0.9), rz=a)
        vbox(b, x - s * 0.28, x + s * 0.28, y - s * 0.28, y + s * 0.28, z + s * 0.6, z + s * 1.05, C(c, 0.85), rz=a + 0.4)


def starfish(b, x, y, z, rg):
    c = ("#F08A4A", "#E8664A", "#F2B04A")[int(rg.integers(3))]
    a0 = rg.uniform(0, 2 * math.pi)
    s = rg.uniform(0.85, 1.2)
    vbox(b, x - 0.07 * s, x + 0.07 * s, y - 0.07 * s, y + 0.07 * s, z - 0.01, z + 0.06, c)
    for k in range(5):
        a = a0 + 2 * math.pi * k / 5
        vbox(b, x, x + 0.22 * s, y - 0.035 * s, y + 0.035 * s, z - 0.01, z + 0.045, c, top=C(c, 1.1), rz=a, about=(x, y))


def driftwood(b, x, y, z, rg, height=None):
    """浮木：一根褪成银灰色的弯木头（两三节、略错开），带一个断枝；躺在地上，顺着坡贴地。"""
    Lg = rg.uniform(1.6, 2.8)
    t = rg.uniform(0.2, 0.3)
    a = rg.uniform(0, math.pi)
    ca, sa = math.cos(a), math.sin(a)
    cols = ("#B8AC98", "#A2937E", "#8C7C68")
    n = 3
    for k in range(n):
        u0 = -Lg / 2 + k * Lg / n
        u1 = u0 + Lg / n + 0.05
        um = (u0 + u1) / 2
        px, py = x + um * ca, y + um * sa
        zz = (height(px, py) if height else z)
        zz = z if zz is None else zz
        off = (k - 1) * rg.uniform(0.02, 0.06)
        tk = t * (1.0 - 0.12 * k)
        vbox(b, px - (u1 - u0) / 2, px + (u1 - u0) / 2, py - tk / 2 + off, py + tk / 2 + off, zz - 0.07, zz + tk * 0.85,
             cols[k % 3], top=C(cols[0], 1.08), rz=a, about=(px, py))
    # 断枝
    bx, by = x + Lg * 0.15 * ca, y + Lg * 0.15 * sa
    zz = (height(bx, by) if height else z) or z
    vbox(b, bx, bx + 0.7, by - 0.05, by + 0.05, zz + 0.05, zz + 0.15, cols[1], rz=a + rg.choice([-1, 1]) * 0.7, about=(bx, by))


def beach_grass(b, x, y, z, rg):
    """滩涂上的一小丛硬草：七八根细叶往外斜，黄绿色，叶尖发白。"""
    cols = ("#8FA84A", "#A8B85A", "#7A9444", "#C2BC6A")
    for k in range(int(rg.integers(6, 10))):
        a = rg.uniform(0, 2 * math.pi)
        hh = rg.uniform(0.3, 0.7)
        dx, dy = 0.08 * math.cos(a), 0.08 * math.sin(a)
        vbox(b, x + dx - 0.025, x + dx + 0.025, y + dy - 0.025, y + dy + 0.025, z - 0.05, z + hh, cols[k % 4], top="#E8E2A8",
             tilt=(0.45 * math.sin(a) * rg.uniform(0.5, 1.0), -0.45 * math.cos(a) * rg.uniform(0.5, 1.0)))
    vbox(b, x - 0.12, x + 0.12, y - 0.12, y + 0.12, z - 0.03, z + 0.06, "#7A8A44")


def wet_rock(b, x, y, z, rg, L):
    """岸边的矮礁石：深色，顶上一点绿藻，侧面几粒白色藤壶。"""
    rk = L["rock"]
    s = rg.uniform(0.45, 0.95)
    hgt = s * rg.uniform(0.45, 0.7)
    rz = rg.uniform(0, 1.6)
    vbox(b, x - s * 0.6, x + s * 0.6, y - s * 0.45, y + s * 0.45, z - 0.15, z + hgt, rk[1], top="#5E7A4E", rz=rz)
    vbox(b, x - s * 0.3, x + s * 0.35, y - s * 0.25, y + s * 0.3, z + hgt - 0.05, z + hgt + s * 0.2, rk[0], top="#6E8A52",
         rz=rz + 0.5)
    for k in range(4):
        a = rg.uniform(0, 2 * math.pi)
        px, py = x + s * 0.55 * math.cos(a), y + s * 0.42 * math.sin(a)
        cube(b, px, py, z + rg.uniform(0.0, hgt * 0.6), 0.07, "#EDE6D8")


def seaweed_bit(b, x, y, z, rg):
    """潮线上的一小截干海草：深绿 / 褐色的扁条。"""
    c = ("#4E5A2E", "#6A5A34", "#3E4A2C")[int(rg.integers(3))]
    a = rg.uniform(0, math.pi)
    Lg = rg.uniform(0.25, 0.55)
    vbox(b, x - Lg / 2, x + Lg / 2, y - 0.05, y + 0.05, z - 0.01, z + 0.035, c, rz=a)


def lighthouse(b, x, y, base, top, L, rg):
    """沙洲正中那块礁石上的老灯塔（这张图的记忆点）：底下是一堆深色玄武岩礁石（只占引擎里礁石圆柱那么大），
    上面一座红白相间的方塔、一圈回廊、发光的灯室、红顶。灯室的光单独做（scene._landmark），这里只画实体。"""
    rk = L["rock"]
    # 礁石：几块错开的大方石，越往上越小，侧面带藤壶和绿藻
    blocks = [(-0.9, -0.6, 1.5, 1.4, 0.0, 2.4), (0.8, -0.4, 1.3, 1.5, 0.0, 2.9), (0.0, 0.9, 1.6, 1.3, 0.0, 2.2),
              (-0.4, 0.2, 1.4, 1.4, 1.8, 3.4), (0.6, 0.5, 1.0, 1.0, 2.3, 3.6), (-1.3, 0.9, 0.9, 0.8, 0.0, 1.4),
              (1.5, 0.9, 0.8, 0.9, 0.0, 1.1), (0.2, -1.4, 1.0, 0.7, 0.0, 1.5)]
    for k, (dx, dy, w, d, z0, z1) in enumerate(blocks):
        rz = rg.uniform(-0.5, 0.5)
        vbox(b, x + dx - w / 2, x + dx + w / 2, y + dy - d / 2, y + dy + d / 2, base + z0 - 0.4, base + z1, rk[k % 2],
             top=rk[0] if z1 < 3.0 else "#6A6672", rz=rz, about=(x + dx, y + dy))
        if z0 == 0.0:
            vbox(b, x + dx - w / 2 - 0.04, x + dx + w / 2 + 0.04, y + dy - d / 2 - 0.04, y + dy + d / 2 + 0.04, base - 0.1,
                 base + 0.35, "#4E6A3E", rz=rz, about=(x + dx, y + dy))
    for k in range(18):
        a = rg.uniform(0, 2 * math.pi)
        r = rg.uniform(1.0, 1.9)
        cube(b, x + r * math.cos(a), y + r * math.sin(a), base + rg.uniform(0.3, 2.2), rg.uniform(0.07, 0.12), "#EDE6D8")
    # 塔：底座 + 红白相间的塔身（往上略收）+ 回廊 + 灯室 + 红顶
    z = base + 3.4
    vbox(b, x - 1.0, x + 1.0, y - 1.0, y + 1.0, z - 0.2, z + 0.45, "#8E8A86", top="#A8A49E")
    z += 0.45
    bands = 5
    hb = (top - 2.2 - z) / bands
    for k in range(bands):
        w = 0.78 - 0.06 * k
        col = "#F2ECE0" if k % 2 == 0 else "#D8463A"
        vbox(b, x - w, x + w, y - w, y + w, z, z + hb + 0.01, col, top=C(col, 1.05))
        z += hb
    # 门、小窗
    vbox(b, x - 0.22, x + 0.22, y - 0.8, y - 0.76, base + 3.85, base + 4.75, "#4A3A34")
    vbox(b, x - 0.12, x + 0.12, y - 0.72, y - 0.68, z - hb * 1.6, z - hb * 1.1, "#3A4656")
    vbox(b, x - 0.95, x + 0.95, y - 0.95, y + 0.95, z, z + 0.16, "#3A3A44")             # 回廊
    for (sx, sy) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        vbox(b, x + sx * 0.88 - 0.04, x + sx * 0.88 + 0.04, y + sy * 0.88 - 0.04, y + sy * 0.88 + 0.04, z + 0.16, z + 0.55, "#3A3A44")
    for (ax, ay, bx_, by_) in ((-0.92, -0.92, 0.92, -0.84), (-0.92, 0.84, 0.92, 0.92), (-0.92, -0.92, -0.84, 0.92),
                               (0.84, -0.92, 0.92, 0.92)):
        vbox(b, x + ax, x + bx_, y + ay, y + by_, z + 0.5, z + 0.58, "#3A3A44")
    zl = z + 0.16
    for (sx, sy) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):                            # 灯室四角的框
        vbox(b, x + sx * 0.5 - 0.06, x + sx * 0.5 + 0.06, y + sy * 0.5 - 0.06, y + sy * 0.5 + 0.06, zl, zl + 1.05, "#2E2E36")
    zr = zl + 1.05
    for k in range(4):
        w = 0.72 - 0.17 * k
        vbox(b, x - w, x + w, y - w, y + w, zr + k * 0.2, zr + (k + 1) * 0.2 + 0.01, "#C83A30" if k % 2 == 0 else "#A82E28")
    vbox(b, x - 0.05, x + 0.05, y - 0.05, y + 0.05, zr + 0.8, zr + 1.25, "#E8B93A")
    return zl, zr                                                                     # 灯室的上下沿（给发光的灯用）


def shipwreck(b, x, y, z, a, L, rg):
    """场外浅水里一条搁浅的破船：歪着半截露出水面的船身、几根肋骨、一根斜着的断桅杆。"""
    ca, sa = math.cos(a), math.sin(a)

    def P(u, v):
        return x + u * ca - v * sa, y + u * sa + v * ca
    wood = ("#7A5434", "#5E3E26", "#8E6440")
    for k in range(9):                                      # 船身：一节节往船头收窄
        u = -4.0 + k * 0.9
        half = 1.5 * (1 - max(0.0, (u - 1.5) / 3.2) ** 1.6) if u > 1.5 else 1.5
        if k < 5:                                           # 后半截沉在水里只露两舷
            for s in (-1, 1):
                cx_, cy_ = P(u + 0.45, s * (half - 0.15))
                vbox(b, cx_ - 0.46, cx_ + 0.46, cy_ - 0.15, cy_ + 0.15, z - 1.0, z + 0.7 + 0.1 * k, wood[k % 3], top=wood[2],
                     rz=a, about=(cx_, cy_))
        else:
            cx_, cy_ = P(u + 0.45, 0.0)
            vbox(b, cx_ - 0.46, cx_ + 0.46, cy_ - half, cy_ + half, z - 1.0, z + 1.0 + 0.25 * (k - 4), wood[k % 3], top=wood[2],
                 rz=a, about=(cx_, cy_))
    for k in range(4):                                      # 露出来的肋骨
        u = -3.6 + k * 0.9
        for s in (-1, 1):
            cx_, cy_ = P(u, s * 1.2)
            vbox(b, cx_ - 0.08, cx_ + 0.08, cy_ - 0.08, cy_ + 0.08, z - 0.5, z + 1.3 - 0.15 * k, "#4E3420", rz=a, about=(cx_, cy_))
    mx, my = P(0.5, 0.0)
    vbox(b, mx - 0.13, mx + 0.13, my - 0.13, my + 0.13, z, z + 5.0, "#6B4A2E", tilt=(0.35, -0.25))
    vbox(b, mx - 1.3, mx + 1.3, my - 0.07, my + 0.07, z + 3.4, z + 3.55, "#6B4A2E", tilt=(0.35, -0.25), rz=a + 0.3,
         about=(mx, my))


def sea_stack(b, x, y, z, h, s, L, rg):
    """场外海里的一根礁柱：几块叠起来的深色方石，顶上一点绿。"""
    rk = L["rock"]
    zz = z - 2.0
    k = 0
    while zz < z + h:
        hh = rg.uniform(0.9, 1.6)
        ss = s * (1 - 0.3 * max(0.0, zz - z) / h) * rg.uniform(0.9, 1.05)
        vbox(b, x - ss / 2, x + ss / 2, y - ss / 2, y + ss / 2, zz, min(z + h, zz + hh), rk[k % 2], top=rk[0],
             rz=rg.uniform(-0.3, 0.3))
        zz += hh
        k += 1
    vbox(b, x - s * 0.35, x + s * 0.35, y - s * 0.35, y + s * 0.35, z + h, z + h + 0.25, "#5E8A4E")
    for k in range(10):                                     # 水线一圈白色藤壶
        a = rg.uniform(0, 2 * math.pi)
        cube(b, x + s * 0.5 * math.cos(a), y + s * 0.5 * math.sin(a), z + rg.uniform(-0.1, 0.6), 0.12, "#EDE6D8")


# ================================================================ 炮兵阵地（拔炮台的台地顶）的布景：都放在投石车背后和台地后沿，不在进攻的路上
def ammo_pile(b, x, y, z, rg, a=0.0):
    """一堆打出去用的石弹：下面一层四块、上面一块，旁边滚落一块；底下垫一块木托。"""
    vbox(b, x - 0.95, x + 0.95, y - 0.7, y + 0.7, z - 0.05, z + 0.12, "#8A5A34", top="#A87448", rz=a, about=(x, y))
    cols = ("#8E8A86", "#A8A49E", "#76726E")
    for k, (dx, dy) in enumerate(((-0.42, -0.3), (0.42, -0.3), (-0.42, 0.3), (0.42, 0.3))):
        px = x + dx * math.cos(a) - dy * math.sin(a)
        py = y + dx * math.sin(a) + dy * math.cos(a)
        vbox(b, px - 0.34, px + 0.34, py - 0.3, py + 0.3, z + 0.1, z + 0.66, cols[k % 3], top=C(cols[k % 3], 1.12),
             rz=a + rg.uniform(-0.2, 0.2), about=(px, py))
    vbox(b, x - 0.3, x + 0.3, y - 0.28, y + 0.28, z + 0.64, z + 1.15, cols[1], top=C(cols[1], 1.12), rz=a + 0.4)
    px, py = x + 1.25 * math.cos(a + 0.5), y + 1.25 * math.sin(a + 0.5)
    vbox(b, px - 0.3, px + 0.3, py - 0.28, py + 0.28, z - 0.05, z + 0.48, cols[2], top=cols[1], rz=a + 0.8)


def crate(b, x, y, z, rg, s=0.8, a=0.0):
    """木箱：木板色，四条深色边框。"""
    vbox(b, x - s / 2, x + s / 2, y - s / 2, y + s / 2, z - 0.02, z + s * 0.85, "#B07A48", top="#C8925A", rz=a, about=(x, y))
    for (dx, dy) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        px = x + dx * (s / 2 - 0.04) * math.cos(a) - dy * (s / 2 - 0.04) * math.sin(a)
        py = y + dx * (s / 2 - 0.04) * math.sin(a) + dy * (s / 2 - 0.04) * math.cos(a)
        vbox(b, px - 0.06, px + 0.06, py - 0.06, py + 0.06, z - 0.02, z + s * 0.86, "#6B4426", rz=a, about=(px, py))


def tent(b, x, y, z, rg, a=0.0, col="#E6DCC4", trim="#F0302C"):
    """营帐：人字顶的帆布帐篷——两片斜着搭起来的帆布（屋脊沿东西向，a 再转），两头山墙是一层层收窄的方块，
    东头一块深色门帘，屋脊一条队伍色的边，两根帐杆。"""
    Lg, w, h = 3.2, 2.8, 2.1                     # 屋脊长、宽、高
    th = math.atan2(w / 2, h)
    sl = math.hypot(w / 2, h) + 0.08
    n = 5
    for s_, ang in ((-1, -th), (1, th)):        # 南、北两片帆布（底边在檐口，往屋脊斜），竖条纹：帆布色和队伍色相间
        yb = y + s_ * w / 2
        for k in range(n):
            x0 = x - Lg / 2 + k * Lg / n
            c = col if k % 2 == 0 else C(trim, 0.92)
            c = c if s_ < 0 else C(c, 0.9)
            vbox(b, x0, x0 + Lg / n + 0.005, yb - 0.04, yb + 0.04, z - 0.02, z - 0.02 + sl, c, rz=a, about=(x, y), tilt=(ang, 0.0))
    for e in (-1, 1):                           # 山墙：一层层收窄
        xe = x + e * (Lg / 2 - 0.06)
        for k in range(5):
            f = k / 5
            ww = w / 2 * (1 - f) - 0.05
            zz = z + h * f
            vbox(b, xe - 0.05, xe + 0.05, y - ww, y + ww, zz, zz + h / 5 + 0.01, C(col, 0.84), rz=a, about=(x, y))
    xd = x + Lg / 2 + 0.01                      # 门帘
    vbox(b, xd - 0.04, xd + 0.04, y - 0.4, y + 0.4, z, z + 1.25, "#4A3A34", rz=a, about=(x, y))
    vbox(b, x - Lg / 2 - 0.12, x + Lg / 2 + 0.12, y - 0.09, y + 0.09, z + h - 0.06, z + h + 0.1, trim, rz=a, about=(x, y))
    for e in (-1, 1):                           # 帐杆
        px, py = x + e * (Lg / 2 + 0.2) * math.cos(a), y + e * (Lg / 2 + 0.2) * math.sin(a)
        vbox(b, px - 0.05, px + 0.05, py - 0.05, py + 0.05, z, z + h + 0.4, "#6B4426")


def flagpole(b, x, y, z, rg, col="#F0302C", h=6.0):
    """旗杆：木杆 + 一面队伍色的长条旗（几块错开的方块，像在飘）+ 石头底座。"""
    vbox(b, x - 0.35, x + 0.35, y - 0.35, y + 0.35, z - 0.05, z + 0.35, "#A8A49E", top="#C4C0B8")
    vbox(b, x - 0.08, x + 0.08, y - 0.08, y + 0.08, z, z + h, "#6B4426", top="#E8B93A")
    for k in range(5):
        dz = 0.08 * math.sin(k * 1.3)
        vbox(b, x + 0.08 + k * 0.38, x + 0.08 + (k + 1) * 0.38 + 0.01, y - 0.04, y + 0.04, z + h - 1.25 + dz - k * 0.06,
             z + h - 0.15 + dz - k * 0.02, col if k % 2 == 0 else C(col, 0.85))


def fence(b, x0, y0, x1, y1, z, rg):
    """矮木栅栏：一根根木桩 + 两道横杆，中间偶尔缺一段。"""
    Lg = math.hypot(x1 - x0, y1 - y0)
    ux, uy = (x1 - x0) / Lg, (y1 - y0) / Lg
    a = math.atan2(uy, ux)
    n = int(Lg / 1.2)
    for k in range(n + 1):
        px, py = x0 + ux * k * Lg / n, y0 + uy * k * Lg / n
        vbox(b, px - 0.08, px + 0.08, py - 0.08, py + 0.08, z - 0.1, z + rg.uniform(1.0, 1.25), "#8A5A34", top="#C08A52")
    t = 0.0
    while t < Lg:
        seg = rg.uniform(3.0, 6.0)
        t1 = min(Lg, t + seg)
        for hh in (0.45, 0.85):
            cx, cy = x0 + ux * (t + t1) / 2, y0 + uy * (t + t1) / 2
            vbox(b, cx - (t1 - t) / 2, cx + (t1 - t) / 2, cy - 0.05, cy + 0.05, z + hh, z + hh + 0.1, "#9A6A3E", rz=a,
                 about=(cx, cy))
        t = t1 + (rg.uniform(1.0, 2.0) if rg.uniform() < 0.3 else 0.0)


def stone_stack(b, x, y, z, rg, a=0.0):
    """一垛码好的方石料（修墙用）：两三层，略错开。"""
    cols = ("#E2D6B8", "#D2C4A2", "#C4B496")
    for lay in range(3):
        for k in range(3 - lay):
            dx = (k - (2 - lay) / 2) * 0.95 + rg.uniform(-0.05, 0.05)
            px, py = x + dx * math.cos(a), y + dx * math.sin(a)
            vbox(b, px - 0.44, px + 0.44, py - 0.36, py + 0.36, z + lay * 0.5 - 0.02, z + lay * 0.5 + 0.48, cols[(k + lay) % 3],
                 top=C(cols[0], 1.05), rz=a + rg.uniform(-0.06, 0.06), about=(px, py))


def weapon_rack(b, x, y, z, rg, a=0.0):
    """兵器架：两根立柱一根横梁，斜靠着几杆枪。"""
    ca, sa = math.cos(a), math.sin(a)
    for s in (-1, 1):
        px, py = x + s * 0.8 * ca, y + s * 0.8 * sa
        vbox(b, px - 0.07, px + 0.07, py - 0.07, py + 0.07, z, z + 1.3, "#6B4426")
    vbox(b, x - 0.9, x + 0.9, y - 0.05, y + 0.05, z + 1.15, z + 1.27, "#8A5A34", rz=a, about=(x, y))
    for k in range(4):
        u = -0.55 + k * 0.37
        px, py = x + u * ca, y + u * sa
        vbox(b, px - 0.03, px + 0.03, py - 0.03, py + 0.03, z, z + 1.9, "#A87448", tilt=(0.18, 0.0), rz=a, about=(px, py))
        vbox(b, px - 0.05, px + 0.05, py - 0.05, py + 0.05, z + 1.85, z + 2.1, "#C8CCD4", tilt=(0.18, 0.0), rz=a, about=(px, py))


CAMP = {"ammo": ammo_pile, "crate": crate, "tent": tent, "flag": flagpole, "stones": stone_stack, "rack": weapon_rack}


def scatter(b, mp, L, spec, height):
    """按 render.decor 的一条规格往区域里撒东西。height(x, z) -> 地面高度（None = 那里没有地，跳过）。"""
    typ = spec["type"]
    x0, z0, x1, z1 = spec["region"]
    n = int(spec.get("n", 20))
    rg = np.random.default_rng(int(spec.get("seed", 7)))
    avoid = spec.get("avoid", [])
    min_y, max_y = spec.get("min_y", -1e9), spec.get("max_y", 1e9)
    placed = 0
    tries = 0
    rad = spec.get("radial")                 # [cx, cz, 最小半径, 最大半径]：只撒在这个圆环里
    while placed < n and tries < n * 12:
        tries += 1
        if rad:
            a_ = rg.uniform(0, 2 * math.pi)
            r_ = math.sqrt(rg.uniform(rad[2] ** 2, rad[3] ** 2))
            x, z = rad[0] + r_ * math.cos(a_), rad[1] + r_ * math.sin(a_)
        else:
            x, z = rg.uniform(x0, x1), rg.uniform(z0, z1)
        if any(a[0] <= x <= a[2] and a[1] <= z <= a[3] for a in avoid):
            continue
        y = height(x, z)
        if y is None or y < min_y or y > max_y:
            continue
        placed += 1
        if typ == "trees":
            st = spec.get("style") or L["tree"][int(rg.integers(0, len(L["tree"])))]
            tree(b, x, z, y, rg.uniform(0.3, 0.45), y + rg.uniform(5.0, 7.5), L, style=st, seed=placed)
        elif typ == "rocks":
            boulder(b, x, z, y, rg.uniform(*spec.get("size", (0.5, 1.6))), L, rg)
        elif typ == "tufts":
            tuft(b, x, z, y, L, rg, s=spec.get("s", 1.0))
        elif typ == "flowers":
            flower(b, x, z, y, L, rg)
        elif typ == "spires":
            spire(b, x, z, y, rg.uniform(*spec.get("h", (5, 12))), rg.uniform(*spec.get("size", (2.0, 4.0))), L, rg)
        elif typ == "mesas":
            mesa(b, x, z, y, rg.uniform(*spec.get("h", (8, 20))), rg.uniform(*spec.get("size", (8, 16))), L, rg)
        elif typ == "ice_shards":
            ice_shard(b, x, z, y, rg.uniform(*spec.get("size", (0.6, 1.4))), L, rg)
        elif typ == "reeds":
            reeds(b, x, z, y, rg)
        elif typ == "shells":
            shell(b, x, z, y, rg)
        elif typ == "starfish":
            starfish(b, x, z, y, rg)
        elif typ == "driftwood":
            driftwood(b, x, z, y, rg, height=height)
        elif typ == "beach_grass":
            beach_grass(b, x, z, y, rg)
        elif typ == "wet_rocks":
            wet_rock(b, x, z, y, rg, L)
        elif typ == "seaweed":
            seaweed_bit(b, x, z, y, rg)
        elif typ == "sea_stacks":
            sea_stack(b, x, z, -1.0, rg.uniform(*spec.get("h", (3, 6))), rg.uniform(*spec.get("size", (2.0, 3.5))), L, rg)
