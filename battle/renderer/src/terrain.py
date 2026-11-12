# -*- coding: utf-8 -*-
"""地图（引擎的 JSON 格式）-> Blender 场景里的地形。

图元和引擎一致（scenario.cpp）：box（min/max 或 c/h + yaw）、ramp、cyl、mound、hull、forest（展开成树干）。
mat 只给渲染用：按名字分派成地面、崖壁、铺石、砌墙、木桥、房子、钟楼、树、礁石、冰、雪……；edge（看不见的边墙）不画。
地图里可以另带一个 "render" 段（只给渲染器，引擎不认）：
  "surround": [图元…]   场外的地形（让战场像是一个地方的一部分，而不是悬空的一块）
  "decor":    [{type: trees|rocks|tufts|flowers|spires|mesas|ice_shards|reeds, region:[x0,z0,x1,z1], n:…}]
  "deep":     深谷 / 悬崖往下画到多深（默认 kill_y - 18）
坐标：Blender X = x，Y = z，Z = y。
"""
import json
import math

import numpy as np

DEG = math.pi / 180.0


# ================================================================ 读地图、高度查询（纯 Python）
def load_map(path):
    return prepare(json.load(open(path, encoding="utf-8")))


def prepare(mp):
    """地图 dict（地图文件、或回放 META 里的 scenario 原文）-> 渲染用的 dict。
    地形、道具、守军一律以引擎的文件为准（回放里带着原文）。渲染器自己的附加段（场外地形、布景、土路）
    按地图名（去掉"第N关_"前缀）找 renderer\\themes\\<名字>.render.json；没有就只画引擎给的。"""
    import os
    import re
    mp = dict(mp)
    here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "themes")
    key = re.sub(r"^第\d+关_", "", mp.get("name", ""))
    key = {"测试_第5关坠崖": "镇山兽", "测试_第5关只有镇山兽": "镇山兽"}.get(key, key)
    if key.startswith("测试_第5关"):
        key = "镇山兽"
    extra = os.path.join(here, key + ".render.json")
    if "render" not in mp and os.path.exists(extra):
        mp["render"] = json.load(open(extra, encoding="utf-8"))
    mp["key"] = key
    mp["prims"] = expand(mp.get("terrain", []))
    rd = mp.get("render", {})
    mp["surround"] = expand(rd.get("surround", []))
    return mp


def expand(prims):
    out = []
    for p in prims:
        p = dict(p)
        t = p.get("type")
        if t == "box":
            if "min" in p:
                mn, mx = p["min"], p["max"]
                p["c"] = [(mn[i] + mx[i]) / 2 for i in range(3)]
                p["h"] = [abs(mx[i] - mn[i]) / 2 for i in range(3)]
            p["yaw"] = float(p.get("yaw", 0.0))
            out.append(p)
        elif t == "forest":
            mn, mx = p["min"], p["max"]
            sp = float(p.get("spacing", 3))
            r = float(p.get("r", 0.35))
            h = float(p.get("h", 5))
            y0 = float(p.get("y0", -0.5))
            idx = 0
            z = mn[1] + sp * 0.5
            while z <= mx[1]:
                x = mn[0] + sp * 0.5
                while x <= mx[0]:
                    idx += 1
                    hs = (idx * 2654435761) & 0xFFFFFFFF
                    jx = (((hs >> 8) & 255) / 255.0 - 0.5) * sp * 0.5
                    jz = (((hs >> 16) & 255) / 255.0 - 0.5) * sp * 0.5
                    out.append(dict(type="cyl", mat=p.get("mat", "tree"), tag=p.get("tag", ""), c=[x + jx, z + jz], r=r,
                                    y0=y0, y1=h, segs=8))
                    x += sp
                z += sp
        else:
            out.append(p)
    return out


def _box_local(p, x, z):
    c, h = p["c"], p["h"]
    dx, dz = x - c[0], z - c[2]
    a = p.get("yaw", 0.0) * DEG
    if a:
        ca, sa = math.cos(a), math.sin(a)
        dx, dz = dx * ca - dz * sa, dx * sa + dz * ca
    return dx, dz


def top_at(p, x, z):
    """图元在 (x, z) 处的顶面高度；不在图元上返回 None。"""
    t = p["type"]
    if t == "box":
        dx, dz = _box_local(p, x, z)
        if abs(dx) <= p["h"][0] and abs(dz) <= p["h"][2]:
            return p["c"][1] + p["h"][1]
        return None
    if t == "ramp":
        x0, z0 = p["from"]
        x1, z1 = p["to"]
        L = math.hypot(x1 - x0, z1 - z0)
        ux, uz = (x1 - x0) / L, (z1 - z0) / L
        s = (x - x0) * ux + (z - z0) * uz
        d = -(x - x0) * uz + (z - z0) * ux
        if 0 <= s <= L and abs(d) <= p.get("width", 4) / 2:
            return p.get("h0", 0) + (p.get("h1", 1) - p.get("h0", 0)) * s / L
        return None
    if t in ("mound", "cyl"):
        cx, cz = p["c"]
        r0 = p.get("r", p.get("r0", 1))
        r1 = p.get("r1", r0) if t == "mound" else r0
        y0, y1 = p.get("y0", 0), p.get("y1", 1)
        d = math.hypot(x - cx, z - cz)
        if d > r0:
            return None
        if d <= r1:
            return y1
        return y0 + (y1 - y0) * (r0 - d) / max(r0 - r1, 1e-6)
    if t == "hull":
        pts = np.array(p["pts"], dtype=float)
        if (x < pts[:, 0].min() or x > pts[:, 0].max() or z < pts[:, 2].min() or z > pts[:, 2].max()):
            return None
        return float(pts[:, 1].max())
    return None


def height_at(mp, x, z, skip_trees=True, include_surround=True):
    best = None
    lists = [mp["prims"]] + ([mp.get("surround", [])] if include_surround else [])
    for lst in lists:
        for p in lst:
            m = p.get("mat", "")
            if m == "edge" or (skip_trees and ("tree" in m or (p["type"] == "cyl" and m in ("reef", "rock", "stone")))):
                continue
            if m in ("house", "bell_tower", "stone_wall", "wall", "reef") and skip_trees:
                continue
            v = top_at(p, x, z)
            if v is not None and (best is None or v > best):
                best = v
    return best


# ================================================================ Blender 部分
def build(mp, L, coll):
    import bpy  # noqa: F401
    import mats as MT
    from geom import MeshBuilder
    deep = mp.get("render", {}).get("deep", mp.get("kill_y", -3) - 18)
    builders = {}

    def mb(key):
        if key not in builders:
            builders[key] = MeshBuilder()
        return builders[key]

    tagged = {}
    for src, lst in (("map", mp["prims"]), ("sur", mp.get("surround", []))):
        for i, p in enumerate(lst):
            m = p.get("mat", "ground")
            if m == "edge":
                continue
            kind = MT.kind_of(m)
            tag = p.get("tag", "")
            if tag and kind in ("bridge", "plank", "ground", "rock", "wall") and src == "map":
                key = ("tag", tag)
                if key not in tagged:
                    tagged[key] = MeshBuilder()
                tb = tagged[key]
            else:
                tb = None
            if p["type"] == "box":
                _box(p, kind, m, L, mb, tb, deep)
            elif p["type"] == "ramp":
                _ramp(p, kind, m, L, tb or mb(kind), deep)
            elif p["type"] == "mound":
                _mound(p, kind, m, L, mb, deep, linear=(src == "map"))
            elif p["type"] == "cyl":
                if kind == "reef" and src == "map" and mp.get("render", {}).get("landmark") == "lighthouse":
                    _lighthouse(mp, p, L, mb)
                    continue
                _cyl(p, kind, m, L, mb, i, deep)
            elif p["type"] == "hull":
                _hull(p, kind, L, mb(kind))
    objs = {}
    for key, b in builders.items():
        if not b.V:
            continue
        objs[key] = b.build("terrain_" + str(key), MT.materials_for(key, L), coll)
    for key, b in tagged.items():
        if not b.V:
            continue
        ob = b.build("tag_" + key[1], MT.materials_for("tagged", L), coll)
        objs[key] = ob
    return objs


# ---- 面的小工具：材质号 0 = 顶面（地面色），1 = 侧面（崖壁色），颜色属性 col 给需要的材质用
def _quad(b, P, mat, col=(1, 1, 1, 1)):
    b.add(np.array(P), [(0, 1, 2, 3)], mat=mat, color=col, smooth=False)


def _box_corners(p, y0=None, y1=None):
    c, h = p["c"], p["h"]
    a = p.get("yaw", 0.0) * DEG
    ca, sa = math.cos(a), math.sin(a)
    ys = (c[1] - h[1] if y0 is None else y0, c[1] + h[1] if y1 is None else y1)
    pts = []
    for (sx, sz) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
        lx, lz = sx * h[0], sz * h[2]
        # 世界 = 局部旋转 -yaw（和 _box_local 互逆）
        wx = c[0] + lx * ca + lz * sa
        wz = c[2] - lx * sa + lz * ca
        pts.append((wx, wz))
    return pts, ys


def _prism(b, pts, y0, y1, top_mat, side_mat, top_col=(1, 1, 1, 1), side_col=(1, 1, 1, 1), bottom=False):
    """把一个平面多边形（逆时针，Blender XY）拉成柱体。"""
    n = len(pts)
    # 保证逆时针
    area = sum(pts[i][0] * pts[(i + 1) % n][1] - pts[(i + 1) % n][0] * pts[i][1] for i in range(n))
    if area < 0:
        pts = pts[::-1]
    top = [(x, z, y1) for (x, z) in pts]
    b.add(np.array(top), [tuple(range(n))], mat=top_mat, color=top_col, smooth=False)
    for i in range(n):
        a0, a1 = pts[i], pts[(i + 1) % n]
        _quad(b, [(a0[0], a0[1], y0), (a1[0], a1[1], y0), (a1[0], a1[1], y1), (a0[0], a0[1], y1)], side_mat, side_col)
    if bottom:
        bot = [(x, z, y0) for (x, z) in pts[::-1]]
        b.add(np.array(bot), [tuple(range(n))], mat=side_mat, color=side_col, smooth=False)


def _box(p, kind, m, L, mb, tb, deep):
    import builders as BLD
    from mats import SLOTS
    pts, (y0, y1) = _box_corners(p)
    if kind in ("bridge", "plank"):
        BLD.bridge(tb or mb("vcol"), p, L, narrow=(kind == "plank"))
        return
    if kind == "house":
        BLD.house(mb("vcol"), p, L)
        return
    if kind == "tower":
        BLD.bell_tower(mb("vcol"), p, L)
        return
    area = 4 * p["h"][0] * p["h"][2]
    if kind in ("rock", "wall") and area > 150 and (y1 - y0) < 8:
        kind = "ground"                       # 大块平顶的（红岩地面、石台地）当地面画
    t, sd = SLOTS.get(kind, (0, 1))
    if kind == "rock" and p.get("crown", True) and (y1 - y0) > 1.2:
        _crown(mb("vcol"), pts, y1, L, p)
    yb = y0
    if kind in ("ground", "ice", "pave", "sand_wet", "mud", "plateau") and p.get("deep", True):
        yb = min(y0, deep)
    if kind == "water":
        yb = y1 - 0.01
    _prism(tb or mb(kind), pts, yb, y1, t, sd)


def _crown(b, pts, y1, L, p):
    """高岩块顶面的边沿：一圈高低不一的碎石块（只在边上 0.7 米以内、最高 0.35 米，不挡站在上面的人）。"""
    import builders as BLD
    rk = L["rock"]
    rg = BLD.rng_of(p["c"][0], p["c"][2], 17)
    n = len(pts)
    cx = sum(q[0] for q in pts) / n
    cz = sum(q[1] for q in pts) / n
    for i in range(n):
        a, c = np.array(pts[i]), np.array(pts[(i + 1) % n])
        L_ = np.linalg.norm(c - a)
        u = (c - a) / L_
        inward = np.array([cx, cz]) - (a + c) / 2
        inward = inward / (np.linalg.norm(inward) + 1e-9)
        t = rg.uniform(0.0, 1.5)
        while t < L_ - 0.4:
            w = rg.uniform(0.4, 1.1)
            dpt = rg.uniform(0.25, 0.6)
            hgt = rg.uniform(0.05, 0.3)
            if rg.uniform() < 0.35:
                t += w + rg.uniform(0.5, 2.0)
                continue
            q = a + u * (t + w / 2) + inward * (dpt / 2 + 0.02)
            ang = math.atan2(u[1], u[0])
            BLD.vbox(b, q[0] - w / 2, q[0] + w / 2, q[1] - dpt / 2, q[1] + dpt / 2, y1 - 0.05, y1 + hgt,
                     rk[1] if rg.uniform() < 0.5 else rk[2], top=rk[0], rz=ang)
            t += w + rg.uniform(0.6, 2.4)


def _ramp(p, kind, m, L, b, deep):
    x0, z0 = p["from"]
    x1, z1 = p["to"]
    w = p.get("width", 4) / 2
    h0, h1 = p.get("h0", 0), p.get("h1", 1)
    base = p.get("base", min(h0, h1) - 1)
    if p.get("deep", True) and kind in ("ground", "ice"):
        base = min(base, deep)
    from mats import SLOTS
    tm, sm = SLOTS.get(kind, (0, 1))
    Lr = math.hypot(x1 - x0, z1 - z0)
    ux, uz = (x1 - x0) / Lr, (z1 - z0) / Lr
    nx, nz = -uz * w, ux * w
    A = (x0 + nx, z0 + nz)
    B = (x0 - nx, z0 - nz)
    C = (x1 - nx, z1 - nz)
    D = (x1 + nx, z1 + nz)
    # 顶面：沿长度切成几段（光照、阴影更稳）
    nseg = max(1, int(Lr / 4))
    for k in range(nseg):
        t0, t1 = k / nseg, (k + 1) / nseg
        P0 = (A[0] + (D[0] - A[0]) * t0, A[1] + (D[1] - A[1]) * t0, h0 + (h1 - h0) * t0)
        P1 = (B[0] + (C[0] - B[0]) * t0, B[1] + (C[1] - B[1]) * t0, h0 + (h1 - h0) * t0)
        P2 = (B[0] + (C[0] - B[0]) * t1, B[1] + (C[1] - B[1]) * t1, h0 + (h1 - h0) * t1)
        P3 = (A[0] + (D[0] - A[0]) * t1, A[1] + (D[1] - A[1]) * t1, h0 + (h1 - h0) * t1)
        quad = [P0, P1, P2, P3]
        # 法线朝上
        v1 = np.subtract(P1, P0)
        v2 = np.subtract(P3, P0)
        if np.cross(v1, v2)[2] < 0:
            quad = quad[::-1]
        _quad(b, quad, tm)
    # 两条长边、两头
    for (Pa, Pb, ha, hb) in ((B, C, h0, h1), (D, A, h1, h0), (A, B, h0, h0), (C, D, h1, h1)):
        _quad(b, [(Pa[0], Pa[1], base), (Pb[0], Pb[1], base), (Pb[0], Pb[1], hb), (Pa[0], Pa[1], ha)], sm)


def _mound(p, kind, m, L, mb, deep, linear=False):
    """linear=True（引擎正式地图里的土丘）：和引擎一样是直的圆台（引擎用凸包做碰撞），兵脚底和地面对得上——
    原来侧面略鼓，涨潮沙洲半坡上画出来的地面比引擎高 1 米，画面上的岸线比引擎宽 5 米。场外布景的土丘仍然略鼓。"""
    cx, cz = p["c"]
    r0 = p.get("r", p.get("r0", 1))
    r1 = p.get("r1", r0)
    y0, y1 = p.get("y0", 0), p.get("y1", 1)
    seg = int(p.get("segs", 32))
    seg = max(seg, 24)
    from mats import SLOTS
    tm, sm = SLOTS.get(kind, (0, 1))
    b = mb("mound")
    rings = []
    nr = 5
    for k in range(nr + 1):
        t = k / nr
        r = r0 + (r1 - r0) * t
        yy = y0 + (y1 - y0) * (1 - (1 - t) ** 1.6) if (r1 < r0 and not linear) else y0 + (y1 - y0) * t
        rings.append((r, yy))
    V = []
    for (r, yy) in rings:
        for j in range(seg):
            a = 2 * math.pi * j / seg
            V.append((cx + r * math.cos(a), cz + r * math.sin(a), yy))
    F = []
    for k in range(nr):
        for j in range(seg):
            j2 = (j + 1) % seg
            F.append((k * seg + j, k * seg + j2, (k + 1) * seg + j2, (k + 1) * seg + j))
    top = nr * seg
    V.append((cx, cz, y1))
    for j in range(seg):
        F.append((top + j, top + (j + 1) % seg, len(V) - 1))
    b.add(np.array(V), F, mat=tm, smooth=True)
    # 底下的裙边（往下画，别露空）
    Vs, Fs = [], []
    for j in range(seg):
        a = 2 * math.pi * j / seg
        Vs.append((cx + r0 * math.cos(a), cz + r0 * math.sin(a), y0))
        Vs.append((cx + r0 * math.cos(a), cz + r0 * math.sin(a), min(y0 - 2.0, deep)))
    for j in range(seg):
        j2 = (j + 1) % seg
        Fs.append((2 * j + 1, 2 * j2 + 1, 2 * j2, 2 * j))
    b.add(np.array(Vs), Fs, mat=sm, smooth=False)


def _cyl(p, kind, m, L, mb, i, deep=-20):
    import builders as BLD
    cx, cz = p["c"]
    r = p.get("r", p.get("r0", 1))
    y0, y1 = p.get("y0", 0), p.get("y1", 1)
    if kind == "tree":
        BLD.tree(mb("vcol"), cx, cz, max(y0, 0.0) if y0 > -1 else 0.0, r, y1, L, style=_tree_style(m, L), seed=i)
        return
    if kind == "reef":
        BLD.reef(mb("vcol"), cx, cz, r, y0, y1, L)
        return
    seg = max(8, int(p.get("segs", 12)))
    pts = [(cx + r * math.cos(2 * math.pi * j / seg), cz + r * math.sin(2 * math.pi * j / seg)) for j in range(seg)]
    from mats import SLOTS
    if kind in ("ground", "ice", "pave", "sand_wet", "mud"):
        tm, sm = SLOTS.get(kind, (0, 1))
        yb = min(y0, deep) if p.get("deep", True) else y0
        _prism(mb(kind), pts, yb, y1, tm, sm)
        return
    tm, sm = SLOTS.get(kind if kind in ("wall", "rock") else "rock")
    _prism(mb(kind), pts, y0, y1, tm, sm)


def _lighthouse(mp, p, L, mb):
    """涨潮沙洲正中的礁石（引擎里是一根过不去的圆柱）画成礁石 + 老灯塔，礁石只占圆柱那么大。"""
    import builders as BLD
    cx, cz = p["c"]
    r = p.get("r", 2.5)
    base = height_at(mp, cx + r + 0.05, cz)
    base = p.get("y0", 0.0) if base is None else base
    rg = np.random.default_rng(11)
    zl, zr = BLD.lighthouse(mb("vcol"), cx, cz, base, base + 9.6, L, rg)
    mp.setdefault("_lamp", (cx, cz, zl, zr))


def _tree_style(m, L):
    if "cherry" in m or "sakura" in m:
        return "sakura"
    if "pine" in m:
        return "snowpine" if L["name"] == "glacier" else "pine"
    return L["tree"][0]


def _hull(p, kind, L, b):
    pts = np.array(p["pts"], dtype=float)
    from scipy.spatial import ConvexHull
    P = pts[:, [0, 2, 1]]
    h = ConvexHull(P)
    F = []
    for s in h.simplices:
        a, bb, c = P[s[0]], P[s[1]], P[s[2]]
        n = np.cross(bb - a, c - a)
        cen = P.mean(axis=0)
        if np.dot(n, a - cen) < 0:
            s = s[::-1]
        F.append(tuple(int(v) for v in s))
    mats = [0 if (np.cross(P[f[1]] - P[f[0]], P[f[2]] - P[f[0]])[2] > 0.5 * np.linalg.norm(np.cross(P[f[1]] - P[f[0]], P[f[2]] - P[f[0]]))) else 1 for f in F]
    from mats import SLOTS
    tm, sm = SLOTS.get(kind, (5, 1))
    for f, mm in zip(F, mats):
        b.add(P[list(f)], [(0, 1, 2)], mat=tm if mm == 0 else sm, smooth=False)
