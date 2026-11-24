# -*- coding: utf-8 -*-
"""主题专属的物件（Blender 里跑）：场内的障碍物造型（房子、树、砌墙、立石、风蚀岩柱、货箱）、喷泉、桥头桩，
场内贴地的小物（仙人掌、蘑菇、蕨、花盆……），以及场外远景上的东西（樱花树、棕榈、鸟居、钟楼、石柱……）。

规矩：障碍物的外形不改变碰撞读法——挡镖的墙是实心的、比人高；外形基本不出墙格（最多探出几厘米）；
不挡镖的东西（坑里的喷泉、地上的小物）都做得低矮、细小。全部是色块 + 平直着色，和石头、角色是一套造型语言。
"""
import math

import bmesh
import bpy
import numpy as np

import geom
import scene_lib as SL
from geom import MeshBuilder
from scene_lib import hexc

M_SOLID, M_GLOW, M_WATER = 0, 1, 2


def mats():
    return [SL.mat_vcol("M_prop", rough=0.88, spec=0.12), SL.mat_vcol("M_propglow", rough=0.5, spec=0.2, emis=1.4),
            SL.mat_water()]


def far_mats():
    return [SL.mat_vcol("M_farprop", rough=0.9, spec=0.1, haze=True), SL.mat_vcol("M_farglow", rough=0.5, spec=0.2, emis=1.4, haze=True),
            SL.mat_water()]


def C(h, k=1.0):
    c = np.array(hexc(h)) if isinstance(h, str) else np.array(h, dtype=float)
    c = c.copy()
    c[:3] = np.clip(c[:3] * k, 0, 1)
    return c


# ---------------------------------------------------------------- 基本形
def quad(mb, P, col, mat=M_SOLID):
    mb.add(np.array(P, dtype=float), [(0, 1, 2, 3)], mat=mat, vcolors=np.tile(col, (4, 1)), smooth=False)


def tri(mb, P, col, mat=M_SOLID):
    mb.add(np.array(P, dtype=float), [(0, 1, 2)], mat=mat, vcolors=np.tile(col, (3, 1)), smooth=False)


def box(mb, x0, x1, y0, y1, z0, z1, col, top=None, mat=M_SOLID, rz=0.0, tilt=(0.0, 0.0)):
    """长方体（不带底面）。rz：绕自己中心转；tilt：顶面沿 x / y 方向的倾斜量（米）。"""
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    c, s = math.cos(rz), math.sin(rz)

    def R(x, y, z):
        if z == z1 and (tilt[0] or tilt[1]):
            z = z + tilt[0] * (x - cx) / max(1e-6, x1 - x0) * 2 + tilt[1] * (y - cy) / max(1e-6, y1 - y0) * 2
        if rz:
            dx, dy = x - cx, y - cy
            return (cx + dx * c - dy * s, cy + dx * s + dy * c, z)
        return (x, y, z)

    tcol = col if top is None else top
    quad(mb, [R(x0, y0, z1), R(x1, y0, z1), R(x1, y1, z1), R(x0, y1, z1)], tcol, mat)
    quad(mb, [R(x0, y0, z0), R(x1, y0, z0), R(x1, y0, z1), R(x0, y0, z1)], col, mat)
    quad(mb, [R(x1, y1, z0), R(x0, y1, z0), R(x0, y1, z1), R(x1, y1, z1)], col, mat)
    quad(mb, [R(x1, y0, z0), R(x1, y1, z0), R(x1, y1, z1), R(x1, y0, z1)], col, mat)
    quad(mb, [R(x0, y1, z0), R(x0, y0, z0), R(x0, y0, z1), R(x0, y1, z1)], col, mat)


def blob(mb, x, y, z, rx, ry, rz_, c_lo, c_hi, sub=2, mat=M_SOLID, smooth=True, rot=(0, 0, 0)):
    V, F = geom.icosphere(sub)
    t = np.clip(V[:, 2] * 0.5 + 0.5, 0, 1)[:, None]
    cols = c_lo[None, :] * (1 - t) + c_hi[None, :] * t
    mb.add(geom.transform(V, scale=(rx, ry, rz_), rot=rot, loc=(x, y, z)), F, mat=mat, vcolors=cols, smooth=smooth)


def stack(mb, cx, cy, levels, cols, n=8, jitter=0.0, rng=None, ell=1.0, rot0=0.0, mat=M_SOLID, cap=None, smooth=False):
    """一摞多边形环：levels = [(半径, 高度)]，相邻两环之间一圈侧面，每圈一个颜色 cols[k]。ell：Y 方向压扁比例。"""
    ang = [rot0 + 2 * math.pi * (i + (rng.uniform(-0.25, 0.25) if (rng is not None and jitter) else 0.0)) / n for i in range(n)]
    rj = [1.0 + (rng.uniform(-jitter, jitter) if (rng is not None and jitter) else 0.0) for _ in range(n)]
    rings = []
    for (r, z) in levels:
        rings.append([(cx + r * rj[i] * math.cos(ang[i]), cy + r * rj[i] * math.sin(ang[i]) * ell, z) for i in range(n)])
    for k in range(len(rings) - 1):
        a, b = rings[k], rings[k + 1]
        col = cols[k % len(cols)] if isinstance(cols, list) else cols
        if smooth:
            V = np.array(a + b)
            F = [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
            mb.add(V, F, mat=mat, vcolors=np.tile(col, (2 * n, 1)), smooth=True)
        else:
            for i in range(n):
                j = (i + 1) % n
                quad(mb, [a[i], a[j], b[j], b[i]], col, mat)
    top = rings[-1]
    tc = cap if cap is not None else (cols[(len(rings) - 2) % len(cols)] if isinstance(cols, list) else cols)
    ctr = (sum(p[0] for p in top) / n, sum(p[1] for p in top) / n, top[0][2])
    mb.add(np.array(top + [ctr]), [(i, (i + 1) % n, n) for i in range(n)], mat=mat, vcolors=np.tile(tc, (n + 1, 1)), smooth=False)


def tube(mb, pts, r, col, seg=6, mat=M_SOLID, r1=None):
    pts = [np.asarray(p, dtype=float) for p in pts]
    n = len(pts)
    V, F = [], []
    for i, p in enumerate(pts):
        d = pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]
        d /= max(1e-9, np.linalg.norm(d))
        u = np.array([0.0, 0.0, 1.0]) if abs(d[2]) < 0.9 else np.array([1.0, 0.0, 0.0])
        w = np.cross(d, u)
        w /= np.linalg.norm(w)
        u = np.cross(w, d)
        rr = r if r1 is None else r + (r1 - r) * i / max(1, n - 1)
        for j in range(seg):
            a = 2 * math.pi * j / seg
            V.append(p + rr * (math.cos(a) * w + math.sin(a) * u))
    for i in range(n - 1):
        for j in range(seg):
            j2 = (j + 1) % seg
            F.append((i * seg + j, i * seg + j2, (i + 1) * seg + j2, (i + 1) * seg + j))
    V.append(pts[-1])
    for j in range(seg):
        F.append(((n - 1) * seg + j, (n - 1) * seg + (j + 1) % seg, len(V) - 1))
    mb.add(np.array(V), F, mat=mat, vcolors=np.tile(col, (len(V), 1)), smooth=True)


# ---------------------------------------------------------------- 房子（老城）
WALLS = ["#FBF1DC", "#F7E3BC", "#F6D9C6", "#F1EEE6", "#F8E7A8", "#EBD9BE", "#DDE8E2"]
ROOFS = ["#E2643C", "#C9553A", "#F08048", "#B8473A", "#E86A4A", "#5F86B2", "#D9773F", "#A8504A"]
DOORS = ["#7A5238", "#3F7F7A", "#9A3E38", "#5A6FA0", "#7A5238"]


def gable(mb, x0, x1, y0, y1, zb, rh, col, wall, along_x=True, ov=0.04, rows=4, th=0.07):
    """双坡屋顶。along_x：屋脊沿 X；否则沿 Y（山墙朝南北）。"""
    def P(u, v, z):
        return (u, v, z) if along_x else (v, u, z)

    def Q(pts, c):
        pts = [P(*p) for p in pts]
        quad(mb, pts if along_x else pts[::-1], c)

    def T(pts, c):
        pts = [P(*p) for p in pts]
        tri(mb, pts if along_x else pts[::-1], c)

    u0, u1, v0, v1 = (x0, x1, y0, y1) if along_x else (y0, y1, x0, x1)
    vm = (v0 + v1) / 2
    a0, a1 = u0 - ov, u1 + ov
    half = (v1 - v0) / 2
    ze = zb - ov * rh / half
    zr = zb + rh
    e0, e1 = v0 - ov, v1 + ov
    for k in range(rows):
        t0, t1 = k / rows, (k + 1) / rows
        kk = (0.86 if k == 0 else (0.97 if k % 2 else 1.08)) * (1.2 if k == rows - 1 else 1.0)
        va, vb = e0 + (vm - e0) * t0, e0 + (vm - e0) * t1
        za, zb_ = ze + (zr - ze) * t0, ze + (zr - ze) * t1
        Q([(a0, va, za), (a1, va, za), (a1, vb, zb_), (a0, vb, zb_)], C(col, kk * (1.05 if along_x else 1.08)))
        va, vb = e1 + (vm - e1) * t0, e1 + (vm - e1) * t1
        Q([(a1, va, za), (a0, va, za), (a0, vb, zb_), (a1, vb, zb_)], C(col, kk * 0.8))
    dk = C(col, 0.66)
    Q([(a0, e0, ze - th), (a1, e0, ze - th), (a1, e0, ze), (a0, e0, ze)], dk)
    Q([(a1, e1, ze - th), (a0, e1, ze - th), (a0, e1, ze), (a1, e1, ze)], dk)
    Q([(a1, e0, ze - th), (a1, vm, zr - th), (a1, vm, zr), (a1, e0, ze)], dk)
    Q([(a1, e1, ze), (a1, vm, zr), (a1, vm, zr - th), (a1, e1, ze - th)], dk)
    Q([(a0, e0, ze), (a0, vm, zr), (a0, vm, zr - th), (a0, e0, ze - th)], dk)
    Q([(a0, e1, ze - th), (a0, vm, zr - th), (a0, vm, zr), (a0, e1, ze)], dk)
    T([(u1, v0, zb), (u1, v1, zb), (u1, vm, zr - 0.01)], wall)
    T([(u0, v1, zb), (u0, v0, zb), (u0, vm, zr - 0.01)], wall)


def house(mb, X0, Y0, Wd, Hd, rng, idx=0, ins=0.05, far=False):
    """一栋小房子，占满 (X0, Y0)-(X0+Wd, Y0+Hd) 这块地（Blender 坐标，Y0 是南边）。门窗开在朝镜头的南墙上。"""
    x0, x1, y0, y1 = X0 + ins, X0 + Wd - ins, Y0 + ins, Y0 + Hd - ins
    short = min(Wd, Hd)
    hw = 0.74 + 0.15 * min(short, 2.5) + rng.uniform(-0.04, 0.06)
    rh = min(0.6, 0.2 * short + 0.14)
    wall = C(WALLS[(idx * 2) % len(WALLS)])
    roofc = ROOFS[idx % len(ROOFS)]
    stone, wood = C("#B4A28E"), C("#7A5238")
    box(mb, x0 - 0.03, x1 + 0.03, y0 - 0.03, y1 + 0.03, -0.03, 0.14, stone)
    box(mb, x0, x1, y0, y1, 0.1, hw, wall)
    along_x = Wd > Hd + 0.2 or (abs(Wd - Hd) <= 0.2 and idx % 2 == 0)
    gable(mb, x0, x1, y0, y1, hw, rh, roofc, wall, along_x)
    # 檐下一道木梁，墙角两根木柱
    box(mb, x0 - 0.012, x1 + 0.012, y0 - 0.014, y0, hw - 0.11, hw - 0.04, wood)
    for xx in (x0, x1 - 0.06):
        box(mb, xx - 0.006, xx + 0.066, y0 - 0.014, y0, 0.14, hw - 0.11, wood)
    # 南墙：一扇门 + 几扇窗
    wdt = x1 - x0
    ns = max(1, int(wdt / 0.62))
    sw = wdt / ns
    door_i = int(rng.integers(0, ns)) if ns < 3 else int(rng.integers(1, ns - 1))
    doorc = C(DOORS[idx % len(DOORS)])
    for i in range(ns):
        cx = x0 + (i + 0.5) * sw
        if i == door_i:
            box(mb, cx - 0.18, cx + 0.18, y0 - 0.02, y0, 0.12, 0.7, C("#FFF8EA"))
            box(mb, cx - 0.135, cx + 0.135, y0 - 0.034, y0, 0.12, 0.64, doorc, top=C(doorc, 0.8))
            box(mb, cx + 0.07, cx + 0.1, y0 - 0.045, y0 - 0.034, 0.36, 0.4, C("#FFD86A"), mat=M_GLOW)
            box(mb, cx - 0.2, cx + 0.2, y0 - 0.045, y0, 0.0, 0.12, C(stone, 1.12))
        else:
            lit = rng.random() < 0.3
            box(mb, cx - 0.16, cx + 0.16, y0 - 0.02, y0, 0.42, 0.8, C("#FFFFFF"))
            box(mb, cx - 0.115, cx + 0.115, y0 - 0.03, y0, 0.465, 0.755, C("#FFE39A") if lit else C("#7CC4EA"),
                mat=M_GLOW if lit else M_SOLID)
            box(mb, cx - 0.012, cx + 0.012, y0 - 0.036, y0, 0.465, 0.755, C("#FFFFFF"))
            box(mb, cx - 0.115, cx + 0.115, y0 - 0.036, y0, 0.6, 0.622, C("#FFFFFF"))
            box(mb, cx - 0.17, cx + 0.17, y0 - 0.046, y0, 0.36, 0.425, wood)
            if not far:
                for k in range(4):
                    fx = cx - 0.12 + 0.08 * k
                    fc = C(["#FF6A6A", "#FFD24A", "#FF9CC0", "#FFFFFF"][int(rng.integers(0, 4))])
                    blob(mb, fx, y0 - 0.03, 0.45, 0.034, 0.03, 0.034, fc, fc, sub=1, mat=M_GLOW)
    if not along_x:      # 山墙朝南：山尖上一扇小圆窗
        xm = (x0 + x1) / 2
        zc = hw + rh * 0.32
        s_ = 0.085
        quad(mb, [(xm, y0 - 0.012, zc - s_), (xm + s_, y0 - 0.012, zc), (xm, y0 - 0.012, zc + s_), (xm - s_, y0 - 0.012, zc)], C("#7CC4EA"))
    # 东西墙各一扇窗（近景斜着拍时看得见）
    ym = (y0 + y1) / 2
    for xx, sg in ((x1, 1), (x0, -1)):
        xa, xb = (xx, xx + 0.02) if sg > 0 else (xx - 0.02, xx)
        box(mb, xa, xb, ym - 0.15, ym + 0.15, 0.44, 0.78, C("#FFFFFF"))
        xa, xb = (xx, xx + 0.03) if sg > 0 else (xx - 0.03, xx)
        box(mb, xa, xb, ym - 0.105, ym + 0.105, 0.485, 0.735, C("#7CC4EA"))
    # 烟囱
    if along_x:
        chx, chy = x0 + (x1 - x0) * (0.24 if idx % 2 else 0.76), y0 + (y1 - y0) * 0.64
    else:
        chx, chy = x0 + (x1 - x0) * 0.66, y0 + (y1 - y0) * (0.3 if idx % 2 else 0.7)
    box(mb, chx - 0.09, chx + 0.09, chy - 0.09, chy + 0.09, hw + rh * 0.3, hw + rh + 0.14, C("#C98A6A"), top=C("#5A4A48"))
    box(mb, chx - 0.11, chx + 0.11, chy - 0.11, chy + 0.11, hw + rh + 0.1, hw + rh + 0.16, C("#E8D8C0"), top=C("#5A4A48"))
    return hw + rh


def tower(mb, cx, cy, z, s, rng, clock=True):
    """远景的钟楼：方塔 + 尖顶 + 钟面。"""
    w = 0.55 * s
    h = 3.4 * s
    wall = C("#F6E8CC")
    box(mb, cx - w, cx + w, cy - w, cy + w, z - 0.2, z + h, wall)
    box(mb, cx - w - 0.05, cx + w + 0.05, cy - w - 0.05, cy + w + 0.05, z + h * 0.62, z + h * 0.66, C("#B4A28E"))
    box(mb, cx - w - 0.07, cx + w + 0.07, cy - w - 0.07, cy + w + 0.07, z + h, z + h + 0.1 * s, C("#B4A28E"))
    stack(mb, cx, cy, [(w * 1.5, z + h + 0.1 * s), (w * 0.9, z + h + 0.9 * s), (0.02, z + h + 2.2 * s)],
          [C("#D3502E"), C("#E2643C")], n=4, rot0=math.pi / 4)
    if clock:
        V, F = geom.lathe([(0.0001, 0.0), (0.34 * s, 0.0), (0.34 * s, 0.03)], seg=16)
        mb.add(geom.transform(V, rot=(math.pi / 2, 0, 0), loc=(cx, cy - w - 0.005, z + h * 0.82)), F, mat=M_GLOW,
               vcolors=np.tile(C("#FFF6DA"), (len(V), 1)), smooth=False)
        box(mb, cx - 0.02 * s, cx + 0.02 * s, cy - w - 0.05, cy - w - 0.03, z + h * 0.82, z + h * 0.82 + 0.24 * s, C("#3A2E38"))
        box(mb, cx, cx + 0.17 * s, cy - w - 0.05, cy - w - 0.03, z + h * 0.82 - 0.02 * s, z + h * 0.82 + 0.02 * s, C("#3A2E38"))
    for k in range(3):
        zz = z + h * (0.2 + 0.16 * k)
        box(mb, cx - 0.1 * s, cx + 0.1 * s, cy - w - 0.02, cy - w, zz, zz + 0.3 * s, C("#7CC4EA"))


# ---------------------------------------------------------------- 树（林地的树干、春樱的樱花树）
def tree(mb, cx, cy, a, rng, trunk=("#8A5E44", "#6E4838"), crown=("#2F8050", "#4FA65C", "#9AD476"), h=1.25, cr=1.0,
         blobs=5, flat=0.8, z=0.0, knot=True, mush=False):
    """粗树干 + 压低收小的树冠。a：树干占地的半边长（米）。树冠半径约 0.8a·cr，探出树干不多，免得挡住旁边的人。"""
    c0, c1 = C(trunk[0]), C(trunk[1])
    n = 9
    r0 = a * 0.8
    rot0 = rng.uniform(0, 6.28)
    lv = [(r0 * 1.22, z - 0.05), (r0 * 1.0, z + 0.14), (r0 * 0.84, z + h * 0.4), (r0 * 0.74, z + h * 0.78), (r0 * 0.7, z + h)]
    stack(mb, cx, cy, lv, [C(c1, 0.92), c0, C(c0, 1.06), c0], n=n, jitter=0.07, rng=rng, rot0=rot0, cap=c1)
    for k in range(5):                      # 板根
        an = rot0 + 2 * math.pi * (k + rng.uniform(-0.2, 0.2)) / 5
        rr = r0 * 1.02
        blob(mb, cx + math.cos(an) * rr, cy + math.sin(an) * rr, z + 0.06, r0 * 0.36, r0 * 0.2, 0.17, c1, c0, sub=1, smooth=False,
             rot=(0, 0, an))
    if knot:                                # 朝镜头的一个树洞
        V, F = geom.icosphere(1)
        mb.add(geom.transform(V, scale=(r0 * 0.2, 0.05, r0 * 0.27), loc=(cx + r0 * 0.1, cy - r0 * 0.8, z + h * 0.5)), F,
               vcolors=np.tile(C("#3A2620"), (len(V), 1)), smooth=False)
    if mush:                                # 树干上的层菇
        for k in range(2):
            an = -math.pi / 2 + (-0.7 + 1.5 * k)
            blob(mb, cx + math.cos(an) * r0 * 0.86, cy + math.sin(an) * r0 * 0.86, z + h * (0.3 + 0.16 * k), 0.12, 0.12, 0.035,
                 C("#E8C89A"), C("#FFF2D8"), sub=1, smooth=False)
    cA, cB, cC = (C(c) for c in crown)
    R = a * 0.82 * cr
    top = z + h
    blob(mb, cx, cy, top + R * 0.45 * flat, R * 0.8, R * 0.8, R * 0.8 * flat, cA, cB)
    for k in range(blobs - 1):
        an = rot0 + 2 * math.pi * k / (blobs - 1)
        rr = R * 0.6
        rb = R * rng.uniform(0.44, 0.56)
        blob(mb, cx + math.cos(an) * rr, cy + math.sin(an) * rr, top + R * rng.uniform(0.15, 0.6) * flat, rb, rb, rb * flat,
             cA, cC if k % 2 else cB)
    blob(mb, cx - R * 0.15, cy + R * 0.1, top + R * 1.0 * flat, R * 0.52, R * 0.52, R * 0.46 * flat, cB, cC)
    blob(mb, cx + R * 0.3, cy - R * 0.2, top + R * 0.85 * flat, R * 0.36, R * 0.36, R * 0.32 * flat, cB, cC)
    return top + R * 1.5 * flat


# ---------------------------------------------------------------- 风蚀岩柱（沙洲、峡谷）
def hoodoo(mb, cx, cy, a, b, h, rng, bands, cap=True, n=None, z=0.0):
    """一根层层叠叠的砂岩柱：腰细、顶上压着一块宽一点的硬石帽。a, b：占地半径。"""
    n = n or int(rng.integers(6, 9))
    cols = [C(c) for c in bands]
    k0 = int(rng.integers(0, len(cols)))
    waist = rng.uniform(0.6, 0.72)
    lv = [(1.0, -0.05), (0.97, 0.1), (0.86, 0.26), (waist + 0.08, 0.42), (waist, 0.56), (waist + 0.06, 0.68), (waist - 0.04, 0.8)]
    if cap:
        lv += [(waist - 0.04, 0.83), (0.9, 0.84), (0.95, 0.93), (0.72, 1.0)]
    else:
        lv += [(waist - 0.12, 0.9), (waist - 0.3, 1.0)]
    levels = [(a * r, z + h * t) for (r, t) in lv]
    cl = [cols[(k0 + i) % len(cols)] for i in range(len(levels) - 1)]
    if cap:
        cl[-2] = C(cols[k0 % len(cols)], 1.08)
        cl[-1] = C(cols[(k0 + 1) % len(cols)], 1.12)
    stack(mb, cx, cy, levels, cl, n=n, jitter=0.1, rng=rng, ell=b / a, rot0=rng.uniform(0, 6.28), cap=C(cols[0], 1.15))


def mesa(mb, cx, cy, a, b, h, rng, bands, z=0.0):
    """平顶的方山：两级台阶。"""
    cols = [C(c) for c in bands]
    n = int(rng.integers(7, 10))
    lv = [(1.0, -0.05), (0.98, 0.16), (0.93, 0.34), (0.9, 0.5), (0.74, 0.52), (0.72, 0.7), (0.68, 0.86), (0.64, 1.0)]
    levels = [(a * r, z + h * t) for (r, t) in lv]
    stack(mb, cx, cy, levels, [cols[i % len(cols)] for i in range(len(levels) - 1)], n=n, jitter=0.08, rng=rng, ell=b / a,
          rot0=rng.uniform(0, 6.28), cap=C(cols[0], 1.18))


# ---------------------------------------------------------------- 货箱、木桶（晚霞天桥）
def crate(mb, cx, cy, s, z, rng, rz=0.0):
    w = s / 2
    frame, panel = C("#8A5A3A"), C(["#D9A066", "#CF9458", "#E2AE74"][int(rng.integers(0, 3))])
    box(mb, cx - w, cx + w, cy - w, cy + w, z, z + s, frame, rz=rz)
    i = s * 0.13
    e = 0.012
    c, sn = math.cos(rz), math.sin(rz)

    def R(x, y, zz):
        return (cx + x * c - y * sn, cy + x * sn + y * c, z + zz)

    quad(mb, [R(-w + i, -w + i, s + e), R(w - i, -w + i, s + e), R(w - i, w - i, s + e), R(-w + i, w - i, s + e)], C(panel, 1.06))
    for (ux, uy) in ((0, -1), (1, 0), (0, 1), (-1, 0)):
        px, py = -uy, ux
        o = w + e

        def S(t, zz):
            return R(ux * o + px * t, uy * o + py * t, zz)
        quad(mb, [S(-w + i, i), S(w - i, i), S(w - i, s - i), S(-w + i, s - i)], panel)
        d = s * 0.075
        quad(mb, [R(ux * (o + e) + px * (-w + i), uy * (o + e) + py * (-w + i), i),
                  R(ux * (o + e) + px * (-w + i + d), uy * (o + e) + py * (-w + i + d), i),
                  R(ux * (o + e) + px * (w - i), uy * (o + e) + py * (w - i), s - i),
                  R(ux * (o + e) + px * (w - i - d), uy * (o + e) + py * (w - i - d), s - i)], frame)


def barrel(mb, cx, cy, r, h, z):
    wood, hoop = C("#C98A54"), C("#5A4A48")
    lv = [(0.84, 0.0), (0.84, 0.1), (0.9, 0.1), (0.9, 0.17), (1.0, 0.36), (1.0, 0.64), (0.9, 0.83), (0.9, 0.9), (0.84, 0.9), (0.84, 1.0)]
    cl = [C(wood, 0.9), hoop, hoop, wood, C(wood, 1.08), wood, hoop, hoop, C(wood, 0.9)]
    stack(mb, cx, cy, [(r * a, z + h * t) for a, t in lv], cl, n=10, cap=C(wood, 1.15))


# ---------------------------------------------------------------- 喷泉（老城的坑）
def fountain(mb, cx, cy, rx, ry):
    """坑 = 水池：一圈石头池沿、池里的水面、正中一座矮矮的出水台。出水台和水柱都很矮很细——坑不挡镖。"""
    stone, stone_hi, dk = C("#E6D9C0"), C("#F6EEDC"), C("#A89884")
    n = 28
    out = [(1.0 + 0.3 / rx, -0.02), (1.0 + 0.3 / rx, 0.13), (1.0 + 0.24 / rx, 0.17), (1.0 + 0.06 / rx, 0.17), (1.0 + 0.0 / rx, 0.12),
           (1.0 - 0.02 / rx, -0.5)]
    rings = [[(cx + rx * r * math.cos(2 * math.pi * i / n), cy + ry * (1 + (r - 1) * rx / ry) * math.sin(2 * math.pi * i / n), z)
              for i in range(n)] for (r, z) in out]
    cols = [stone, C(stone, 0.92), stone_hi, C(stone, 0.9), dk]
    for k in range(len(rings) - 1):
        for i in range(n):
            j = (i + 1) % n
            c = C(cols[k], 1.0 if (i % 2) else 0.94)
            quad(mb, [rings[k][i], rings[k][j], rings[k + 1][j], rings[k + 1][i]], c)
    # 水面
    wz = -0.2
    V = [(cx, cy, wz)] + [(cx + (rx + 0.5) * math.cos(2 * math.pi * i / n), cy + (ry + 0.5) * math.sin(2 * math.pi * i / n), wz) for i in range(n)]
    mb.add(np.array(V), [(0, 1 + i, 1 + (i + 1) % n) for i in range(n)], mat=M_GLOW,
           vcolors=np.array([C("#8FE0F6")] + [C("#3FA8E0")] * n), smooth=False)
    for rr, wd in ((0.52, 0.035), (0.8, 0.025)):          # 水面上的涟漪
        a = [(cx + rx * rr * math.cos(2 * math.pi * i / n), cy + ry * rr * math.sin(2 * math.pi * i / n), wz + 0.006) for i in range(n)]
        b = [(cx + (rx * rr + wd) * math.cos(2 * math.pi * i / n), cy + (ry * rr + wd) * math.sin(2 * math.pi * i / n), wz + 0.006) for i in range(n)]
        for i in range(n):
            if i % 4 != 3:
                j = (i + 1) % n
                quad(mb, [a[i], a[j], b[j], b[i]], C("#DFF8FF"), M_GLOW)
    # 出水台
    r0 = min(rx, ry) * 0.26
    stack(mb, cx, cy, [(r0 * 1.15, -0.5), (r0 * 1.15, -0.1), (r0, -0.06), (r0 * 0.9, 0.06), (r0 * 1.25, 0.1), (r0 * 1.3, 0.16), (r0 * 1.1, 0.17)],
          [dk, stone, C(stone, 0.92), stone_hi, stone, stone_hi], n=10, cap=C("#8FE0F6"))
    stack(mb, cx, cy, [(r0 * 0.3, 0.16), (r0 * 0.24, 0.3), (r0 * 0.4, 0.33), (r0 * 0.4, 0.36)], [stone, stone_hi, stone], n=8, cap=C("#DFF8FF"))
    # 水柱：几道细细的弧线落进池里
    for k in range(8):
        an = 2 * math.pi * k / 8 + 0.2
        pts = []
        for i in range(8):
            t = i / 7
            rr = r0 * 0.3 + (min(rx, ry) * 0.52 - r0 * 0.3) * t
            zz = 0.36 + 0.3 * math.sin(math.pi * min(1.0, t * 1.25)) * (1 - t * 0.2) - (0.56) * t ** 2.2
            pts.append((cx + rr * math.cos(an), cy + rr * math.sin(an) * ry / rx, zz))
        V, F = [], []
        px, py = -math.sin(an) * 0.022, math.cos(an) * 0.022
        cl = []
        for i, p in enumerate(pts):
            V += [(p[0] - px, p[1] - py, p[2]), (p[0] + px, p[1] + py, p[2])]
            cl += [(0.85, 0.96, 1.0, 0.85), (0.85, 0.96, 1.0, 0.85)]
        for i in range(len(pts) - 1):
            F.append((2 * i, 2 * i + 1, 2 * i + 3, 2 * i + 2))
        mb.add(np.array(V), F, mat=M_WATER, vcolors=np.array(cl), smooth=False)
    blob(mb, cx, cy, 0.4, r0 * 0.3, r0 * 0.3, 0.07, C("#DFF8FF"), C("#FFFFFF"), sub=1, mat=M_GLOW)


# ---------------------------------------------------------------- 桥头桩（木板桥）
def bridge_post(mb, x, y, h=0.5):
    wood, dk = C("#8A5A3A"), C("#5E3E2C")
    box(mb, x - 0.055, x + 0.055, y - 0.055, y + 0.055, -0.05, h, wood, top=C(wood, 1.2))
    box(mb, x - 0.07, x + 0.07, y - 0.07, y + 0.07, h - 0.12, h - 0.06, C("#E8D2A0"))
    box(mb, x - 0.07, x + 0.07, y - 0.07, y + 0.07, h, h + 0.04, dk)


# ---------------------------------------------------------------- 场内贴地的小物（往 env.Groups 的网格里加；材质号：2 花 3 石子 4 叶）
def d_cactus(mb, x, y, rng, s=1.0):
    g0, g1 = C("#4E9A5A"), C("#8ED07A")
    if rng.random() < 0.5:          # 圆球仙人掌
        r = rng.uniform(0.07, 0.11) * s
        blob(mb, x, y, r * 0.8, r, r, r * 0.9, g0, g1, sub=1, mat=3, smooth=False)
        blob(mb, x, y, r * 1.62, r * 0.3, r * 0.3, r * 0.22, C("#FF7FA8"), C("#FFC2D4"), sub=1, mat=2)
    else:                           # 小柱子带一条胳膊
        h = rng.uniform(0.2, 0.32) * s
        blob(mb, x, y, h * 0.5, 0.05 * s, 0.05 * s, h * 0.56, g0, g1, sub=1, mat=3, smooth=False)
        sg = 1 if rng.random() < 0.5 else -1
        blob(mb, x + sg * 0.07 * s, y, h * 0.5, 0.045 * s, 0.03 * s, 0.03 * s, g0, g0, sub=1, mat=3, smooth=False)
        blob(mb, x + sg * 0.1 * s, y, h * 0.66, 0.03 * s, 0.03 * s, h * 0.24, g0, g1, sub=1, mat=3, smooth=False)


def d_dryshrub(mb, x, y, rng, cols=("#8A7A46", "#D6C27A")):
    c0, c1 = C(cols[0]), C(cols[1])
    for k in range(int(rng.integers(7, 12))):
        a = rng.uniform(0, 6.28)
        L = rng.uniform(0.1, 0.2)
        el = rng.uniform(0.5, 1.2)
        tip = (x + math.cos(a) * L * math.cos(el), y + math.sin(a) * L * math.cos(el), L * math.sin(el))
        w = 0.012
        V = [(x - math.sin(a) * w, y + math.cos(a) * w, 0.0), (x + math.sin(a) * w, y - math.cos(a) * w, 0.0), tip]
        mb.add(np.array(V), [(0, 1, 2)], mat=1, vcolors=np.array([c0, c0, c1]))


def d_mushroom(mb, x, y, rng, s=1.0):
    cap = C(["#E8453C", "#E8453C", "#F0A23C", "#B86AD0"][int(rng.integers(0, 4))])
    for k in range(int(rng.integers(1, 4))):
        ox, oy = (rng.normal(0, 0.07, 2) if k else (0.0, 0.0))
        h = rng.uniform(0.07, 0.13) * s * (1.0 if k == 0 else 0.7)
        r = h * rng.uniform(0.7, 0.95)
        stack(mb, x + ox, y + oy, [(r * 0.3, 0.0), (r * 0.24, h)], C("#FFF4E0"), n=6, mat=3)
        V, F = geom.icosphere(1)
        V = V[:, :] * np.array([r, r, r * 0.6])
        V[:, 2] = np.maximum(V[:, 2], -r * 0.08)
        mb.add(V + np.array([x + ox, y + oy, h]), F, mat=2, vcolors=np.tile(cap, (len(V), 1)), smooth=False)
        for j in range(4):
            an = 6.28 * j / 4 + k
            blob(mb, x + ox + math.cos(an) * r * 0.5, y + oy + math.sin(an) * r * 0.5, h + r * 0.44, r * 0.14, r * 0.14, r * 0.07,
                 C("#FFFFFF"), C("#FFFFFF"), sub=1, mat=2)


def d_fern(mb, x, y, rng, cols=("#2F7E46", "#8AD06A"), size=None):
    c0, c1 = C(cols[0]), C(cols[1])
    size = size or rng.uniform(0.2, 0.32)
    for j in range(int(rng.integers(7, 11))):
        a = rng.uniform(0, 2 * math.pi)
        L = size * rng.uniform(0.7, 1.1)
        wd = L * 0.2
        ca, sa = math.cos(a), math.sin(a)
        V = np.array([(x, y, 0.01), (x + ca * L * .5 - sa * wd, y + sa * L * .5 + ca * wd, L * 0.5),
                      (x + ca * L, y + sa * L, L * 0.22), (x + ca * L * .5 + sa * wd, y + sa * L * .5 - ca * wd, L * 0.5)])
        c = c0 + (c1 - c0) * rng.uniform(0.3, 1.0)
        mb.add(V, [(0, 1, 2, 3)], mat=4, vcolors=np.array([c0, c, c1, c]))


def d_heather(mb, x, y, rng, cols=("#8E6CB4", "#D9A6E8")):
    c0, c1 = C(cols[0]), C(cols[1])
    for k in range(int(rng.integers(4, 9))):
        ox, oy = rng.normal(0, 0.1, 2)
        r = rng.uniform(0.04, 0.075)
        blob(mb, x + ox, y + oy, r * 0.7, r, r, r * 0.9, c0, c1, sub=1, mat=2, smooth=False)


def d_pot(mb, x, y, rng):
    r = rng.uniform(0.07, 0.1)
    stack(mb, x, y, [(r * 0.7, 0.0), (r, r * 1.5), (r * 1.1, r * 1.5), (r * 1.1, r * 1.8)], [C("#C9704A"), C("#E08A5E"), C("#E08A5E")],
          n=8, mat=3, cap=C("#6A4A3A"))
    if rng.random() < 0.6:
        fc = C(["#FF6A6A", "#FFD24A", "#FF9CC0", "#FFFFFF"][int(rng.integers(0, 4))])
        blob(mb, x, y, r * 2.4, r * 1.0, r * 1.0, r * 0.8, C("#4FA65C"), C("#8AD06A"), sub=1, mat=4, smooth=False)
        for k in range(4):
            an = 6.28 * k / 4 + 0.4
            blob(mb, x + math.cos(an) * r * 0.6, y + math.sin(an) * r * 0.6, r * 2.9, r * 0.32, r * 0.32, r * 0.26, fc, fc, sub=1, mat=2)
    else:
        blob(mb, x, y, r * 2.7, r * 0.9, r * 0.9, r * 1.3, C("#3E8A56"), C("#7CC46A"), sub=1, mat=4, smooth=False)


def d_rubble(mb, x, y, rng, col="#D6CCB4"):
    for k in range(int(rng.integers(1, 3))):
        ox, oy = rng.normal(0, 0.1, 2)
        s = rng.uniform(0.07, 0.12)
        c = C(col, rng.uniform(0.88, 1.06))
        box(mb, x + ox - s, x + ox + s, y + oy - s * 0.7, y + oy + s * 0.7, -0.02, s * rng.uniform(0.7, 1.2), c, mat=3,
            rz=rng.uniform(0, 3.14), tilt=(rng.uniform(-0.03, 0.03), rng.uniform(-0.03, 0.03)))


DETAILS = dict(cactus=d_cactus, dryshrub=d_dryshrub, mushroom=d_mushroom, fern=d_fern, heather=d_heather, pots=d_pot, rubble=d_rubble)


# ---------------------------------------------------------------- 远景上的东西
def far_sakura(x, y, z, s, rng, T, L, S, pal):
    c = np.tile(C("#5A3A3A"), (1, 1))[0]
    tube(T, [(x, y, z - 0.05), (x + 0.04 * s, y, z + 0.5 * s), (x - 0.06 * s, y, z + 0.95 * s)], 0.11 * s, c, r1=0.07 * s)
    for sg in (-1, 1):
        tube(T, [(x, y, z + 0.5 * s), (x + sg * 0.3 * s, y + 0.05 * s, z + 0.9 * s), (x + sg * 0.5 * s, y, z + 1.15 * s)], 0.06 * s, c,
             r1=0.03 * s)
    cA, cB, cC = (C(c_) for c_ in pal)
    for k in range(int(rng.integers(6, 9))):
        a = rng.uniform(0, 6.28)
        rr = rng.uniform(0.1, 0.62) * s
        r = rng.uniform(0.36, 0.56) * s
        blob(L, x + math.cos(a) * rr, y + math.sin(a) * rr, z + s * rng.uniform(1.05, 1.42), r, r, r * 0.62, cA, cC if k % 3 else cB)


def far_palm(x, y, z, s, rng, T, L, S, pal=None):
    lean = rng.uniform(-0.35, 0.35) * s
    h = rng.uniform(1.5, 2.3) * s
    pts = [(x + lean * (t ** 1.6), y, z - 0.05 + h * t) for t in (0, 0.25, 0.5, 0.75, 1.0)]
    tube(T, pts, 0.09 * s, C("#A07A54"), r1=0.06 * s)
    tx, tz = x + lean, z + h
    g0, g1 = C("#3E9A58"), C("#A4DC6A")
    for k in range(8):
        a = 2 * math.pi * k / 8 + rng.uniform(-0.2, 0.2)
        ca, sa = math.cos(a), math.sin(a)
        Lf = rng.uniform(0.7, 0.95) * s
        V, F, cl = [], [], []
        for i in range(5):
            t = i / 4
            r = Lf * t
            zz = tz + 0.22 * s * math.sin(t * 2.2) - 0.5 * s * t * t
            w = 0.13 * s * math.sin(math.pi * (0.15 + 0.85 * t)) + 0.01
            V += [(tx + ca * r - sa * w, y + sa * r + ca * w, zz - 0.04 * s), (tx + ca * r, y + sa * r, zz + 0.02 * s),
                  (tx + ca * r + sa * w, y + sa * r - ca * w, zz - 0.04 * s)]
            c = g0 + (g1 - g0) * t
            cl += [c * 0.9, c, c * 0.9]
        for i in range(4):
            F += [(3 * i, 3 * i + 1, 3 * i + 4, 3 * i + 3), (3 * i + 1, 3 * i + 2, 3 * i + 5, 3 * i + 4)]
        L.add(np.array(V), F, vcolors=np.clip(np.array(cl), 0, 1), smooth=False)
    for k in range(3):
        blob(T, tx + 0.07 * s * math.cos(k * 2.1), y + 0.07 * s * math.sin(k * 2.1), tz - 0.06 * s, 0.06 * s, 0.06 * s, 0.06 * s,
             C("#7A5238"), C("#9A6A48"), sub=1)


def far_cactus(x, y, z, s, rng, T, L, S, pal=None):
    g0, g1 = C("#3F8A5A"), C("#86C87A")
    h = rng.uniform(0.9, 1.5) * s
    r = 0.13 * s
    tube(L, [(x, y, z - 0.05), (x, y, z + h * 0.5), (x, y, z + h)], r, g0, seg=8, r1=r * 0.9)
    blob(L, x, y, z + h, r * 0.9, r * 0.9, r * 0.9, g0, g1, sub=1)
    for sg in ((-1,), (1,), (-1, 1))[int(rng.integers(0, 3))]:
        zb = z + h * rng.uniform(0.35, 0.55)
        ex = x + sg * 0.32 * s
        tube(L, [(x, y, zb), (ex, y, zb), (ex, y, zb + 0.1 * s), (ex, y, zb + h * 0.35)], r * 0.7, g0, seg=7)
        blob(L, ex, y, zb + h * 0.35, r * 0.66, r * 0.66, r * 0.66, g0, g1, sub=1)


def far_spire(x, y, z, s, rng, T, L, S, pal=None):
    bands = SL.PAL.get("spire_bands", ("#F2A070", "#D8704A", "#B8503E", "#F6C89A"))
    a = rng.uniform(0.35, 0.6) * s
    hoodoo(S, x, y, a, a * rng.uniform(0.8, 1.0), rng.uniform(1.6, 3.0) * s, rng, bands, cap=rng.random() < 0.7, z=z - 0.1)


def far_juniper(x, y, z, s, rng, T, L, S, pal=None):
    tube(T, [(x, y, z - 0.05), (x + 0.05 * s, y, z + 0.35 * s)], 0.07 * s, C("#7A5E4A"))
    g0, g1 = C("#4E7A4A"), C("#9ABE6E")
    for k in range(3):
        a = 2.1 * k + rng.uniform(0, 1)
        r = rng.uniform(0.26, 0.38) * s
        blob(L, x + math.cos(a) * 0.16 * s, y + math.sin(a) * 0.16 * s, z + (0.42 + 0.12 * k) * s, r, r, r * 0.8, g0, g1)


def far_bare(x, y, z, s, rng, T, L, S, pal=None):
    c = C("#8A7468")
    h = rng.uniform(0.9, 1.4) * s
    tube(T, [(x, y, z - 0.05), (x + 0.03 * s, y, z + h * 0.6), (x - 0.04 * s, y, z + h)], 0.07 * s, c, r1=0.025 * s)
    for k in range(4):
        a = 1.6 * k + rng.uniform(0, 1)
        zb = z + h * (0.4 + 0.13 * k)
        tube(T, [(x, y, zb), (x + math.cos(a) * 0.25 * s, y + math.sin(a) * 0.25 * s, zb + 0.22 * s),
                 (x + math.cos(a) * 0.4 * s, y + math.sin(a) * 0.4 * s, zb + 0.5 * s)], 0.035 * s, c, r1=0.012 * s, seg=5)


def far_cypress(x, y, z, s, rng, T, L, S, pal=None):
    tube(T, [(x, y, z - 0.05), (x, y, z + 0.3 * s)], 0.06 * s, C("#7A5E4A"))
    p = SL.PAL["pine_pal"]
    h = rng.uniform(1.5, 2.3) * s
    blob(L, x, y, z + 0.2 * s + h * 0.5, 0.27 * s, 0.27 * s, h * 0.52, C(p[0]), C(p[2]))


def far_heather(x, y, z, s, rng, T, L, S, pal=None):
    SL.make_bush(x, y, z, s * 1.3, rng, L, palette=("#8E6CB4", "#D9A6E8"))


def far_house(x, y, z, s, rng, T, L, S, pal=None):
    w, d = rng.uniform(1.3, 2.0) * s, rng.uniform(1.2, 1.7) * s
    mbl = MeshBuilder()
    house(mbl, -w / 2, -d / 2, w, d, rng, idx=int(rng.integers(0, 50)), far=True)
    Vall = np.concatenate(mbl.V) * 0.95 + np.array([x, y, z])
    S.add(Vall, mbl.F, vcolors=np.concatenate(mbl.C), smooth=False)
    S.M[-len(mbl.M):] = mbl.M            # 材质号（发光的窗）带过去


def far_bigtree(x, y, z, s, rng, T, L, S, pal):
    h = rng.uniform(1.6, 2.6) * s
    r = rng.uniform(0.2, 0.3) * s
    tube(T, [(x, y, z - 0.1), (x + 0.03 * s, y, z + h * 0.5), (x, y, z + h)], r, C("#6E4838"), seg=8, r1=r * 0.7)
    cA, cB, cC = (C(c_) for c_ in pal)
    for k in range(int(rng.integers(6, 9))):
        a = rng.uniform(0, 6.28)
        rr = rng.uniform(0.1, 0.75) * s
        rb = rng.uniform(0.6, 0.95) * s
        blob(L, x + math.cos(a) * rr, y + math.sin(a) * rr, z + h + s * rng.uniform(-0.1, 0.7), rb, rb, rb * 0.8, cA, cC if k % 2 else cB)


FAR = dict(sakura=far_sakura, palm=far_palm, cactus=far_cactus, spire=far_spire, juniper=far_juniper, bare=far_bare,
           cypress=far_cypress, heather=far_heather, house=far_house, bigtree=far_bigtree)


# ---- 每个主题一两样只此一处的大件
def f_torii(x, y, z, s, rng, T, L, S):
    red, dk = C("#E8432E"), C("#2E2630")
    w, h = 1.25 * s, 2.3 * s
    for sg in (-1, 1):
        stack(S, x + sg * w, y, [(0.13 * s, z - 0.1), (0.1 * s, z + h)], red, n=8)
        stack(S, x + sg * w, y, [(0.16 * s, z - 0.1), (0.16 * s, z + 0.22 * s)], dk, n=8)
    box(S, x - w * 1.18, x + w * 1.18, y - 0.09 * s, y + 0.09 * s, z + h * 0.74, z + h * 0.82, red)
    box(S, x - w * 1.4, x + w * 1.4, y - 0.11 * s, y + 0.11 * s, z + h * 0.96, z + h * 1.04, red)
    box(S, x - w * 1.5, x + w * 1.5, y - 0.15 * s, y + 0.15 * s, z + h * 1.04, z + h * 1.11, dk, tilt=(0.0, 0.0))
    box(S, x - 0.09 * s, x + 0.09 * s, y - 0.06 * s, y + 0.06 * s, z + h * 0.82, z + h * 0.96, red)


def f_lantern(x, y, z, s, rng, T, L, S):
    st, hi = C("#C9C4BC"), C("#E6E2DA")
    stack(S, x, y, [(0.22 * s, z - 0.05), (0.2 * s, z + 0.1 * s), (0.09 * s, z + 0.14 * s), (0.08 * s, z + 0.62 * s), (0.2 * s, z + 0.68 * s),
                    (0.2 * s, z + 0.74 * s)], [st, hi, st, hi, st], n=6)
    box(S, x - 0.12 * s, x + 0.12 * s, y - 0.12 * s, y + 0.12 * s, z + 0.74 * s, z + 0.96 * s, C("#FFE08A"), mat=M_GLOW)
    stack(S, x, y, [(0.3 * s, z + 0.96 * s), (0.2 * s, z + 1.08 * s), (0.04 * s, z + 1.2 * s)], [st, hi], n=6)


def f_menhirs(x, y, z, s, rng, T, L, S):
    st = [C("#BCC6D4"), C("#909CB2"), C("#D8DEE8")]
    n = 6
    R = 1.35 * s
    for k in range(n):
        a = 2 * math.pi * k / n + 0.3
        px, py = x + R * math.cos(a), y + R * math.sin(a) * 0.8
        h = rng.uniform(1.1, 1.7) * s
        box(S, px - 0.2 * s, px + 0.2 * s, py - 0.13 * s, py + 0.13 * s, z - 0.1, z + h, st[k % 3], top=st[2], rz=a + 1.57,
            tilt=(rng.uniform(-0.1, 0.1), 0.0))
    for sg in (-1, 1):
        box(S, x + sg * 0.45 * s - 0.17 * s, x + sg * 0.45 * s + 0.17 * s, y - 0.15 * s, y + 0.15 * s, z - 0.1, z + 1.7 * s, st[0], top=st[2])
    box(S, x - 0.75 * s, x + 0.75 * s, y - 0.18 * s, y + 0.18 * s, z + 1.7 * s, z + 1.98 * s, st[1], top=st[2])


def f_lamp(x, y, z, s, rng, T, L, S):
    wood = C("#6E4838")
    box(S, x - 0.05 * s, x + 0.05 * s, y - 0.05 * s, y + 0.05 * s, z - 0.05, z + 1.5 * s, wood)
    box(S, x - 0.05 * s, x + 0.34 * s, y - 0.035 * s, y + 0.035 * s, z + 1.4 * s, z + 1.47 * s, wood)
    box(S, x + 0.2 * s, x + 0.38 * s, y - 0.09 * s, y + 0.09 * s, z + 1.12 * s, z + 1.36 * s, C("#FFD27A"), mat=M_GLOW)
    box(S, x + 0.17 * s, x + 0.41 * s, y - 0.12 * s, y + 0.12 * s, z + 1.36 * s, z + 1.4 * s, C("#4A3430"))


def f_columns(x, y, z, s, rng, T, L, S):
    st, hi = C("#E6DCC4"), C("#F6EEDC")
    for k in range(4):
        px = x + (k - 1.5) * 0.85 * s
        h = [2.0, 1.2, 2.2, 0.7][k] * s * rng.uniform(0.9, 1.1)
        stack(S, px, y, [(0.24 * s, z - 0.05), (0.24 * s, z + 0.12 * s), (0.17 * s, z + 0.14 * s), (0.15 * s, z + h)], [hi, st, C(st, 0.95)],
              n=10, cap=hi)
        if k in (0, 2):
            box(S, px - 0.24 * s, px + 0.24 * s, y - 0.24 * s, y + 0.24 * s, z + h, z + h + 0.12 * s, hi)
    box(S, x + 0.2 * s, x + 0.9 * s, y - 0.75 * s, y - 0.45 * s, z - 0.02, z + 0.26 * s, st, rz=0.4)


def f_arch(x, y, z, s, rng, T, L, S):
    st, hi = C("#E0D6BE"), C("#F6EEDC")
    w, h = 0.95 * s, 1.9 * s
    for sg in (-1, 1):
        box(S, x + sg * w - 0.24 * s, x + sg * w + 0.24 * s, y - 0.24 * s, y + 0.24 * s, z - 0.1, z + h, st, top=hi)
    n = 9
    for k in range(n):
        a0 = math.pi * k / n
        a1 = math.pi * (k + 1) / n
        if k in (5, 6):
            continue                    # 塌掉的一段
        am = (a0 + a1) / 2
        cx_, cz_ = x + math.cos(am) * w, z + h + math.sin(am) * w * 0.9
        V = []
        for (aa, rr) in ((a0, 0.76), (a1, 0.76), (a1, 1.24), (a0, 1.24)):
            V.append((x + math.cos(aa) * w * rr, z + h + math.sin(aa) * w * 0.9 * rr))
        c = hi if k % 2 else st
        f = [(V[i][0], y - 0.22 * s, V[i][1]) for i in range(4)]
        b = [(V[i][0], y + 0.22 * s, V[i][1]) for i in range(4)]
        quad(S, f, c)
        quad(S, b[::-1], c)
        quad(S, [f[3], f[2], b[2], b[3]], hi)
        quad(S, [f[1], f[0], b[0], b[1]], C(st, 0.85))


def f_arch_rock(x, y, z, s, rng, T, L, S):
    bands = SL.PAL.get("spire_bands", ("#F2A070", "#D8704A", "#B8503E", "#F6C89A"))
    cols = [C(c) for c in bands]
    w = 1.3 * s
    hoodoo(S, x - w, y, 0.55 * s, 0.5 * s, 2.3 * s, rng, bands, cap=False, z=z - 0.1)
    hoodoo(S, x + w, y, 0.5 * s, 0.45 * s, 1.9 * s, rng, bands, cap=False, z=z - 0.1)
    n = 8
    for k in range(n):
        t0, t1 = k / n, (k + 1) / n
        xa, xb = x - w * 0.8 + 1.6 * w * t0, x - w * 0.8 + 1.6 * w * t1
        za = z + 1.9 * s + 0.5 * s * math.sin(math.pi * t0)
        zb = z + 1.9 * s + 0.5 * s * math.sin(math.pi * t1)
        th = 0.42 * s
        c = cols[k % len(cols)]
        quad(S, [(xa, y - 0.3 * s, za - th), (xb, y - 0.3 * s, zb - th), (xb, y - 0.3 * s, zb), (xa, y - 0.3 * s, za)], c)
        quad(S, [(xb, y + 0.3 * s, zb - th), (xa, y + 0.3 * s, za - th), (xa, y + 0.3 * s, za), (xb, y + 0.3 * s, zb)], c)
        quad(S, [(xa, y - 0.3 * s, za), (xb, y - 0.3 * s, zb), (xb, y + 0.3 * s, zb), (xa, y + 0.3 * s, za)], C(cols[0], 1.15))
        quad(S, [(xa, y + 0.3 * s, za - th), (xb, y + 0.3 * s, zb - th), (xb, y - 0.3 * s, zb - th), (xa, y - 0.3 * s, za - th)], C(c, 0.7))


def f_spires(x, y, z, s, rng, T, L, S):
    for k in range(4):
        a = 1.57 * k + rng.uniform(0, 1)
        far_spire(x + math.cos(a) * 0.9 * s, y + math.sin(a) * 0.7 * s, z, s * rng.uniform(0.8, 1.3), rng, T, L, S)


def f_tower(x, y, z, s, rng, T, L, S):
    tower(S, x, y, z, s * rng.uniform(0.95, 1.15), rng)


def f_shrooms(x, y, z, s, rng, T, L, S):
    for k in range(3):
        a = 2.1 * k + rng.uniform(0, 1)
        px, py = x + math.cos(a) * 0.6 * s * (k > 0), y + math.sin(a) * 0.6 * s * (k > 0)
        h = (1.5 if k == 0 else rng.uniform(0.6, 1.0)) * s
        r = h * 0.62
        stack(S, px, py, [(r * 0.26, z - 0.1), (r * 0.18, z + h * 0.6), (r * 0.2, z + h)], C("#FFF4E0"), n=8, smooth=True)
        V, F = geom.icosphere(2)
        V = V * np.array([r, r, r * 0.62])
        V[:, 2] = np.maximum(V[:, 2], -r * 0.1)
        S.add(V + np.array([px, py, z + h]), F, vcolors=np.tile(C("#E8453C"), (len(V), 1)), smooth=True)
        for j in range(7):
            an = 0.9 * j + k
            rr = r * (0.3 + 0.08 * (j % 4))
            blob(S, px + math.cos(an) * rr, py + math.sin(an) * rr, z + h + r * 0.62 * math.sqrt(max(0.05, 1 - (rr / r) ** 2)) - 0.01,
                 r * 0.13, r * 0.13, r * 0.05, C("#FFFFFF"), C("#FFFFFF"), sub=1)


FEATURES = dict(torii=f_torii, lantern=f_lantern, menhirs=f_menhirs, lamp=f_lamp, columns=f_columns, arch=f_arch,
                arch_rock=f_arch_rock, spires=f_spires, tower=f_tower, shrooms=f_shrooms)
FEATURE_SCALE = dict(torii=1.0, lantern=1.0, menhirs=1.0, lamp=1.0, columns=1.0, arch=1.1, arch_rock=1.0, spires=1.0, tower=1.0,
                     shrooms=1.2)


# ---------------------------------------------------------------- 把一件跨了几波塌陷的大物件切开，各自跟着脚下的地块掉
def wall_components(grid):
    """墙格的连通块（四邻）。返回 [格子列表]。"""
    H, W = len(grid), len(grid[0])
    seen = set()
    out = []
    for y in range(H):
        for x in range(W):
            if grid[y][x] == 1 and (x, y) not in seen:
                st, comp = [(x, y)], []
                seen.add((x, y))
                while st:
                    cx, cy = st.pop()
                    comp.append((cx, cy))
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        q = (cx + dx, cy + dy)
                        if 0 <= q[0] < W and 0 <= q[1] < H and grid[q[1]][q[0]] == 1 and q not in seen:
                            seen.add(q)
                            st.append(q)
                out.append(comp)
    return out


def rects_of(cells):
    """把一组格子贪心切成尽量大的矩形。返回 [(x0, y0, w, h)]。"""
    left = set(cells)
    out = []
    while left:
        best = None
        for (x, y) in sorted(left, key=lambda c: (c[1], c[0])):
            w = 1
            while (x + w, y) in left:
                w += 1
            h = 1
            while all((xx, y + h) in left for xx in range(x, x + w)):
                h += 1
            if best is None or w * h > best[2] * best[3]:
                best = (x, y, w, h)
            break
        x, y, w, h = best
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                left.discard((xx, yy))
        out.append(best)
    return out


def split_build(mb, cells, rp, router, attach, coll, name, materials=None):
    """建成物体；如果它脚下的格子分属几波塌陷，就沿格线切开，每一部分挂到各自的地块上。返回物体列表。"""
    materials = materials or mats()
    cs = rp.cell
    ob = mb.build(name, materials, coll)
    cellset = set(cells)
    comp = {}
    groups = []
    for c in cells:
        if c in comp:
            continue
        gi = len(groups)
        st, grp = [c], []
        comp[c] = gi
        wv = rp.wave[c[1]][c[0]]
        while st:
            q = st.pop()
            grp.append(q)
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                r = (q[0] + dx, q[1] + dy)
                if r in cellset and r not in comp and rp.wave[r[1]][r[0]] == wv:
                    comp[r] = gi
                    st.append(r)
        groups.append(grp)

    def gid_of(grp):
        mx = sum(x + 0.5 for x, y in grp) / len(grp)
        my = sum(y + 0.5 for x, y in grp) / len(grp)
        c = min(grp, key=lambda q: (q[0] + 0.5 - mx) ** 2 + (q[1] + 0.5 - my) ** 2)
        return router((c[0] + 0.5) * cs, (c[1] + 0.5) * cs)

    if len(groups) == 1:
        attach(ob, gid_of(groups[0]))
        return [ob]
    xcuts = sorted({x for (x, y) in cells if (x - 1, y) in cellset and comp[(x - 1, y)] != comp[(x, y)]})
    ycuts = sorted({y for (x, y) in cells if (x, y - 1) in cellset and comp[(x, y - 1)] != comp[(x, y)]})
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    for x in xcuts:
        bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=(x * cs, 0, 0), plane_no=(1, 0, 0))
    for y in ycuts:
        bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=(0, -y * cs, 0), plane_no=(0, 1, 0))
    centers = [((x + 0.5) * cs, -(y + 0.5) * cs, comp[(x, y)]) for (x, y) in cells]
    fg = []
    for f in bm.faces:
        c = f.calc_center_median()
        cell = (int(math.floor(c.x / cs)), int(math.floor(-c.y / cs)))
        if cell in comp:
            fg.append(comp[cell])
        else:
            fg.append(min(centers, key=lambda q: (q[0] - c.x) ** 2 + (q[1] - c.y) ** 2)[2])
    out = []
    for gi, grp in enumerate(groups):
        b2 = bm.copy()
        b2.faces.ensure_lookup_table()
        kill = [f for f, g in zip(b2.faces, fg) if g != gi]
        if len(kill) == len(b2.faces):
            b2.free()
            continue
        bmesh.ops.delete(b2, geom=kill, context='FACES')
        me = bpy.data.meshes.new("%s_%d" % (name, gi))
        b2.to_mesh(me)
        b2.free()
        for m in materials:
            me.materials.append(m)
        o2 = bpy.data.objects.new("%s_%d" % (name, gi), me)
        coll.objects.link(o2)
        attach(o2, gid_of(grp))
        out.append(o2)
    bm.free()
    me0 = ob.data
    bpy.data.objects.remove(ob)
    bpy.data.meshes.remove(me0)
    return out
