# -*- coding: utf-8 -*-
"""场景（Blender 里跑）：岛体各塌陷状态、塌陷碎块、石头、高草、地表点缀、场外台地、云海、灯光。
全部由回放里的地图数据驱动（宽、高、格子边长、塌陷图、出生点），没有针对某张图的手摆内容。"""
import json
import math
import os

import bmesh
import bpy
import numpy as np
from mathutils import Matrix

import geom
import props as PR
import scene_lib as SL
import themes
from geom import MeshBuilder
from scene_lib import NB, PAL, hexc

CACHE = os.environ.get("DART_CACHE") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "work", "cache")
ROCK = dict(themes.BASE["rock"])       # 障碍物样式（主题可换）
THEME = dict(themes.BASE)


def apply_theme(rp):
    """按回放 / 地图定下主题，把配色并进调色板。建任何材质之前调用。"""
    th = themes.resolve(rp)
    PAL.update(th["pal"])
    ROCK.update(th["rock"])
    THEME.clear()
    THEME.update(th)
    return th


def new_coll(name):
    c = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(c)
    return c


# ---------------------------------------------------------------- 地形材质
def mat_terrain(ground_png):
    key = ('terrain', ground_png)
    if key in SL._MATS:
        return SL._MATS[key]
    m = bpy.data.materials.new("M_terrain")
    nb = NB(m)
    g = nb.geo()
    pos = nb.out(g, 'Position')
    nrm = nb.sep(nb.out(g, 'Normal'))
    Z = nb.sep(pos).outputs['Z']
    nz = nrm.outputs['Z']
    top = nb.smooth(nz, 0.55, 0.85)
    top = nb.math('MULTIPLY', top, nb.smooth(nb.out(nb.attr('depth'), 'Factor'), 0.16, 0.05))   # 只有草面算"顶"，崖壁上的小台阶不算
    # 草面：贴图（按建模时的平面坐标取，碎块掉下去时贴图跟着走）
    if ground_png:
        uv = nb.node("ShaderNodeAttribute", attribute_name="guv")
        tex = nb.node("ShaderNodeTexImage")
        tex.image = bpy.data.images.load(ground_png, check_existing=True)
        tex.extension = 'EXTEND'
        nb.link(nb.out(uv, 'Vector'), tex.inputs['Vector'])
        ground = tex.outputs['Color']
    else:
        n1 = nb.noise(pos, 0.2, 2.0, 0.45)
        ground = nb.mix(nb.smooth(n1, 0.45, 0.62), nb.rgb(hexc(PAL['grass'])), nb.rgb(hexc(PAL['grass2'])))
        es = nb.out(nb.attr('edge_s'), 'Factor')
        ground = nb.mix(nb.smooth(es, 0.2, 0.05), ground, nb.rgb(hexc(PAL['grass_rim'])))
    # 崖壁：按深度从暖到冷，每个柱面一点色差
    dz = nb.out(nb.attr('depth'), 'Factor')
    fc = nb.out(nb.attr('facet'), 'Factor')
    ramp = nb.node("ShaderNodeValToRGB")
    nb.link(nb.math('DIVIDE', dz, 7.0), ramp.inputs['Factor'])
    els = ramp.color_ramp.elements
    els[0].position = 0.04
    els[0].color = hexc(PAL['rock_a'])
    els[1].position = 0.2
    els[1].color = hexc(PAL['rock_b'])
    e = els.new(0.42)
    e.color = hexc(PAL['rock_c'])
    e = els.new(0.75)
    e.color = hexc(PAL['rock_deep'])
    rock = ramp.outputs['Color']
    hsv = nb.node("ShaderNodeHueSaturation")
    nb.link(rock, hsv.inputs['Color'])
    nb.link(nb.math('ADD', nb.math('MULTIPLY', fc, 0.09), 1.0), hsv.inputs['Value'])
    nb.link(nb.math('ADD', nb.math('MULTIPLY', fc, 0.012), 0.5), hsv.inputs['Hue'])
    rock = hsv.outputs['Color']
    # 朝上的小台阶亮一点（台阶沿口的高光）
    rock = nb.mix(nb.math('MULTIPLY', nb.smooth(nz, 0.25, 0.6), 0.5), rock, nb.rgb(hexc(PAL['ledge_hi'])))
    rock = nb.mix(nb.smooth(nz, -0.15, -0.6), rock, nb.rgb(hexc(PAL['rock_under'])))
    soil = nb.mix(nb.smooth(dz, 0.2, 0.34), nb.rgb(hexc(PAL['soil'])), rock)
    side = nb.mix(nb.smooth(dz, 0.09, 0.17), nb.rgb(hexc(PAL['lip'])), soil)
    col = nb.mix(top, side, ground)
    # 物体颜色 alpha < 1 = 塌陷预警：整块地往橙红色闪（1 - alpha 是闪的程度）
    oi = nb.node("ShaderNodeObjectInfo")
    col = nb.mix(nb.math('SUBTRACT', 1.0, nb.out(oi, 'Alpha'), clamp=True), col, nb.rgb(hexc("#FF7A2E")))
    col = SL._haze(nb, col, Z, -1.6, -11.0, 0.88)
    p = nb.principled(col, rough=0.95, spec=0.1)
    nb.output(p)
    SL._MATS[key] = m
    return m


def load_terrain(npz, prefix, name, coll, ground_png, uv_rect, center=None, sharp_deg=33.0):
    """npz 里的 prefix__V/F/S -> 网格物体。uv_rect = (x0, y0, w, h)：地面贴图覆盖的范围（Blender 坐标）。
    center：给了就把网格挪到以它为原点（碎块用，方便翻滚）。"""
    V = npz[prefix + "__V"]
    F = npz[prefix + "__F"]
    S = npz[prefix + "__S"]
    if len(V) == 0:
        return None
    me = bpy.data.meshes.new(name)
    Vl = V.copy()
    if center is not None:
        Vl[:, 0] -= center[0]
        Vl[:, 1] -= center[1]
    me.vertices.add(len(V))
    me.vertices.foreach_set("co", Vl.ravel())
    nl = len(F)
    me.loops.add(nl * 3)
    me.loops.foreach_set("vertex_index", F.ravel().astype(np.int32))
    me.polygons.add(nl)
    me.polygons.foreach_set("loop_start", np.arange(0, nl * 3, 3, dtype=np.int32))
    me.update(calc_edges=True)
    me.validate()
    me.shade_smooth()
    try:
        me.set_sharp_from_angle(angle=math.radians(sharp_deg))
    except Exception:
        pass
    for i, nm in enumerate(("edge_s", "strata", "depth", "facet")):
        at = me.attributes.new(nm, 'FLOAT', 'POINT')
        at.data.foreach_set("value", np.ascontiguousarray(S[:, i]).astype(np.float32))
    uv = np.zeros((len(V), 3), dtype=np.float32)
    uv[:, 0] = (V[:, 0] - uv_rect[0]) / uv_rect[2]
    uv[:, 1] = (V[:, 1] - uv_rect[1]) / uv_rect[3]
    at = me.attributes.new("guv", 'FLOAT_VECTOR', 'POINT')
    at.data.foreach_set("vector", uv.ravel())
    me.materials.append(mat_terrain(ground_png))
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    if center is not None:
        ob.location = (center[0], center[1], 0.0)
    return ob


# ---------------------------------------------------------------- 石头（墙）：凸包切面 + 一道倒角，平直着色
def make_rock(cx, cy, a, b, h, rng, coll, name="rock", mossy=False, sun=(-0.6, 0.45), rot=None):
    bm = bmesh.new()
    n = int(rng.integers(ROCK["n"][0], ROCK["n"][1]))
    rot = rng.uniform(0, math.pi) if rot is None else rot
    base = rng.uniform(0, 2 * math.pi)
    levels = [(-0.05, 1.0, 0.03), (h * rng.uniform(0.42, 0.6), rng.uniform(0.9, 1.0), 0.06),
              (h, rng.uniform(*ROCK["top"]), 0.1)]
    ox, oy = rng.uniform(-0.12, 0.12) * a, rng.uniform(-0.12, 0.12) * b
    tx, ty = rng.uniform(-0.22, 0.22), rng.uniform(-0.22, 0.22)
    for li, (z, sc, jit) in enumerate(levels):
        for i in range(n):
            t = base + 2 * math.pi * (i + rng.uniform(-0.25, 0.25) + 0.5 * (li % 2)) / n
            ct, st = math.cos(t), math.sin(t)
            p = ROCK["p"]
            r = (abs(ct) ** p + abs(st) ** p) ** (-1.0 / p)
            k = sc * rng.uniform(1 - jit * 2, 1.0)
            x, y = a * r * ct * k, b * r * st * k
            if li == 2:
                x += ox
                y += oy
            zz = z + (tx * x + ty * y if li == 2 else 0.0) + (rng.uniform(-0.04, 0.04) * h if li else 0.0)
            cr, sr = math.cos(rot), math.sin(rot)
            bm.verts.new((cx + x * cr - y * sr, cy + x * sr + y * cr, zz))
    res = bmesh.ops.convex_hull(bm, input=bm.verts)
    for v in res.get("geom_interior", []) + res.get("geom_unused", []):
        if isinstance(v, bmesh.types.BMVert) and v.is_valid:
            bm.verts.remove(v)
    bm.normal_update()
    edges = [e for e in bm.edges if not all(v.co.z < 0.0 for v in e.verts)]
    bmesh.ops.bevel(bm, geom=edges, offset=min(a, b) * 0.085, segments=1, profile=0.5, affect='EDGES',
                    clamp_overlap=True)
    bm.normal_update()
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    c_top, c_lit, c_side, c_shade, c_base, c_moss = (np.array(hexc(PAL[k])) for k in
                                                     ("stone_top", "stone_lit", "stone_side", "stone_shade",
                                                      "stone_base", "moss"))
    cols = np.zeros((len(me.loops), 4), dtype=np.float32)
    big = max((p.area for p in me.polygons), default=1.0)
    for p in me.polygons:
        nrm = p.normal
        tint = rng.uniform(0.95, 1.05)
        chamfer = p.area < big * 0.06
        if nrm.z > 0.6:
            c = c_moss if (mossy and not chamfer) else c_top
        else:
            d = nrm.x * sun[0] + nrm.y * sun[1]
            f = min(1.0, max(0.0, d * 0.75 + 0.5))
            c = c_shade + (c_lit - c_shade) * f
            if nrm.z > 0.25:
                c = c * 0.5 + c_top * 0.5
            if nrm.z < -0.2:
                c = c_base
        if chamfer and nrm.z > 0.1:
            c = c * 0.45 + np.array(hexc("#FFF3D6")) * 0.55
        zc = p.center.z
        if zc < 0.12 and nrm.z < 0.5:
            c = c * 0.86
        c = np.clip(c * tint, 0, 1)
        for li in p.loop_indices:
            cols[li] = c
            cols[li][3] = 1.0
    attr = me.color_attributes.new("col", 'FLOAT_COLOR', 'CORNER')
    attr.data.foreach_set("color", cols.ravel())
    me.materials.append(SL.mat_vcol("M_stone", rough=0.92, spec=0.12))
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    return ob


def wall_groups(grid, wave, maxc=2):
    """把墙格分成矩形块（同一塌陷波的才并在一起）。返回 [(x0, y0, w, h)]（格坐标）。"""
    H, W = len(grid), len(grid[0])
    seen = [[False] * W for _ in range(H)]
    rects = []
    for y in range(H):
        for x in range(W):
            if grid[y][x] == 1 and not seen[y][x]:
                wv = wave[y][x]
                w = 1
                while x + w < W and grid[y][x + w] == 1 and not seen[y][x + w] and wave[y][x + w] == wv and w < maxc:
                    w += 1
                h = 1
                ok = True
                while ok and y + h < H and h < maxc:
                    for xx in range(x, x + w):
                        if grid[y + h][xx] != 1 or seen[y + h][xx] or wave[y + h][xx] != wv:
                            ok = False
                            break
                    if ok:
                        h += 1
                for yy in range(y, y + h):
                    for xx in range(x, x + w):
                        seen[yy][xx] = True
                rects.append((x, y, w, h))
    return rects


def make_rock_cluster(x0, y0, w, h, cs, rng, coll):
    """一组墙格（格坐标，cs = 格子边长）-> 一组错落的石头。外沿不出格。返回物体列表。"""
    objs = []
    X0, Y0, Wd, Hd = x0 * cs, -(y0 + h) * cs, w * cs, h * cs     # Blender 坐标下的矩形（左下角 + 宽高）
    cx, cy = X0 + Wd / 2, Y0 + Hd / 2
    hk = (0.55 + 0.45 * min(1.0, min(Wd, Hd))) * ROCK["h"]     # 占地小的石头矮一点
    parts = []
    ratio = max(Wd, Hd) / max(1e-6, min(Wd, Hd))
    if ratio >= 1.6:                           # 长条：一大一小两块排开
        if Wd > Hd:
            parts.append((X0 + Wd * 0.28, cy, Wd * 0.27, Hd * 0.47, rng.uniform(1.05, 1.3) * hk, True))
            parts.append((X0 + Wd * 0.76, cy + rng.uniform(-0.03, 0.03), Wd * 0.23, Hd * 0.44,
                          rng.uniform(0.6, 0.8) * hk, False))
        else:
            parts.append((cx, Y0 + Hd * 0.28, Wd * 0.47, Hd * 0.27, rng.uniform(1.05, 1.3) * hk, True))
            parts.append((cx + rng.uniform(-0.03, 0.03), Y0 + Hd * 0.76, Wd * 0.44, Hd * 0.23,
                          rng.uniform(0.6, 0.8) * hk, False))
        if rng.random() < 0.5:
            parts = [(2 * cx - p[0], 2 * cy - p[1]) + p[2:] for p in parts]
    elif Wd * Hd <= 1.3:                       # 小块：一块石头
        parts.append((cx, cy, Wd * 0.47, Hd * 0.47, rng.uniform(0.95, 1.2) * hk, True))
    else:
        sx, sy = rng.choice([-1, 1]), rng.choice([-1, 1])
        A, B = Wd / 2 - 0.03, Hd / 2 - 0.03
        bw, bh = A * 0.62, B * 0.62
        parts.append((cx + sx * (A - bw), cy + sy * (B - bh), bw, bh, rng.uniform(1.15, 1.4) * hk, True))
        mw, mh = A * 0.46, B * 0.46
        parts.append((cx - sx * (A - mw), cy - sy * (B - mh), mw, mh, rng.uniform(0.75, 0.95) * hk, False))
        parts.append((cx - sx * (A - mw * 0.82), cy + sy * (B - mh * 0.82), mw * 0.82, mh * 0.82,
                      rng.uniform(0.5, 0.68) * hk, False))
        parts.append((cx + sx * (A - mw * 0.8), cy - sy * (B - mh * 0.8), mw * 0.8, mh * 0.8,
                      rng.uniform(0.55, 0.75) * hk, False))
    for i, (px, py, a, b, hh, main) in enumerate(parts):
        objs.append(make_rock(px, py, a, b, hh, rng, coll, mossy=(main and rng.random() < ROCK["mossy"]),
                              rot=rng.uniform(-0.25, 0.25)))
    # 脚下一两块小碎石（在格子里面）
    for k in range(int(rng.integers(1, 3))):
        sx, sy = rng.choice([-1, 1]), rng.choice([-1, 1])
        if min(Wd, Hd) < 0.7:
            break
        r = rng.uniform(0.1, 0.16)
        objs.append(make_rock(cx + sx * (Wd / 2 - r), cy + sy * (Hd / 2 - r), r, r * rng.uniform(0.8, 1.0),
                              rng.uniform(0.16, 0.3), rng, coll, name="rock_small"))
    return objs


def obstacle_rect(x0, y0, w, h, cs, rng, coll):
    """一组同波的墙格（最大 2 米见方）-> 这个主题的障碍物。返回物体列表（由调用方挂到地块上）。"""
    kind = ROCK.get("kind", "rocks")
    X0, Y0, Wd, Hd = x0 * cs, -(y0 + h) * cs, w * cs, h * cs
    cx, cy = X0 + Wd / 2, Y0 + Hd / 2
    Lg, Sh = max(Wd, Hd), min(Wd, Hd)
    hk = ROCK.get("h", 1.0)
    if kind == "hoodoo":
        mb = MeshBuilder()
        bands = ROCK["bands"]
        if Sh >= 1.9:
            PR.mesa(mb, cx, cy, Wd / 2 - 0.04, Hd / 2 - 0.04, 1.3 * hk, rng, bands)
            sx, sy = rng.choice([-1, 1]), rng.choice([-1, 1])
            PR.hoodoo(mb, cx + sx * Wd * 0.14, cy + sy * Hd * 0.12, Wd * 0.2, Hd * 0.2, 0.75 * hk, rng, bands, z=1.25 * hk)
        elif Lg / Sh >= 1.6:
            big = rng.random() < 0.5
            for k, (t, sc, hh) in enumerate(((0.28, 0.27, 1.75), (0.76, 0.22, 1.15))):
                if not big:
                    t = 1 - t
                if Wd > Hd:
                    PR.hoodoo(mb, X0 + Wd * t, cy, Wd * sc, Hd * 0.46, hh * hk * (0.7 + 0.3 * min(1.0, Sh)), rng, bands, cap=(k == 0))
                else:
                    PR.hoodoo(mb, cx, Y0 + Hd * t, Wd * 0.46, Hd * sc, hh * hk * (0.7 + 0.3 * min(1.0, Sh)), rng, bands, cap=(k == 0))
        else:
            PR.hoodoo(mb, cx, cy, Wd / 2 - 0.03, Hd / 2 - 0.03, (1.05 + 0.75 * min(1.0, Sh)) * hk * rng.uniform(0.92, 1.08), rng, bands)
        return [mb.build("hoodoo", PR.mats(), coll)]
    if kind == "crates":
        mb = MeshBuilder()
        nx, ny = max(1, int(round(Wd / 1.0))), max(1, int(round(Hd / 1.0)))
        sx, sy = Wd / nx, Hd / ny
        for j in range(ny):
            for i in range(nx):
                px, py = X0 + (i + 0.5) * sx, Y0 + (j + 0.5) * sy
                s_ = min(sx, sy) - 0.1
                r = rng.random()
                if s_ >= 0.8 and r < 0.3:           # 三只木桶
                    br = s_ * 0.24
                    for (ox, oy) in ((-0.24, -0.2), (0.24, -0.16), (0.0, 0.24)):
                        PR.barrel(mb, px + ox * s_, py + oy * s_, br, s_ * 0.62, 0.0)
                    PR.barrel(mb, px, py - 0.04 * s_, br * 0.92, s_ * 0.55, s_ * 0.62)
                else:
                    PR.crate(mb, px, py, s_, 0.0, rng, rz=rng.uniform(-0.06, 0.06))
                    s2 = s_ * rng.uniform(0.5, 0.6)
                    PR.crate(mb, px + rng.uniform(-0.1, 0.1) * s_, py + rng.uniform(-0.1, 0.1) * s_, s2, s_ + 0.012, rng,
                             rz=rng.uniform(0.2, 0.7))
        return [mb.build("crates", PR.mats(), coll)]
    if kind == "menhir":
        save = dict(ROCK)
        objs = []
        ROCK.update(n=(4, 6), p=4.5, top=(0.5, 0.68))
        if Sh >= 0.9:                               # 一米见方以上：一根大立石，脚边两块小的
            objs.append(make_rock(cx, cy, Wd * 0.4, Hd * 0.38, rng.uniform(1.9, 2.25) * hk, rng, coll, rot=rng.uniform(-0.3, 0.3)))
            for k in range(2):
                sx, sy = rng.choice([-1, 1]), rng.choice([-1, 1])
                objs.append(make_rock(cx + sx * Wd * 0.33, cy + sy * Hd * 0.33, 0.15, 0.13, rng.uniform(0.35, 0.6), rng, coll))
        elif Lg >= 1.4:                             # 长条：两根立石顶一根横梁（立石几乎挨着，不留能过人的门洞）
            Hh = rng.uniform(1.45, 1.6) * hk
            for sg in (-1, 1):
                if Wd > Hd:
                    objs.append(make_rock(cx + sg * (Wd / 2 - 0.3), cy, 0.27, Hd * 0.42, Hh, rng, coll, rot=rng.uniform(-0.1, 0.1)))
                else:
                    objs.append(make_rock(cx, cy + sg * (Hd / 2 - 0.3), Wd * 0.42, 0.27, Hh, rng, coll, rot=rng.uniform(-0.1, 0.1)))
            if Lg >= 1.9:
                objs.append(make_rock(cx, cy, Wd * 0.2 if Wd > Hd else Wd * 0.38, Hd * 0.38 if Wd > Hd else Hd * 0.2, Hh * 0.8, rng, coll))
            ROCK.update(top=(0.92, 0.98), n=(4, 5), p=6.0)
            lin = make_rock(cx, cy, Wd / 2 - 0.02 if Wd > Hd else Wd * 0.46, Hd * 0.46 if Wd > Hd else Hd / 2 - 0.02, 0.32, rng, coll,
                            rot=0.0)
            lin.location = (0, 0, Hh - 0.04)
            objs.append(lin)
        elif Lg >= 0.9:                             # 两块并排，一高一矮
            for k, sg in enumerate((-1, 1)):
                hh = (1.7 if k == 0 else 1.15) * hk * rng.uniform(0.92, 1.08)
                if Wd > Hd:
                    objs.append(make_rock(cx + sg * Wd * 0.25, cy, Wd * 0.23, Hd * 0.42, hh, rng, coll, rot=rng.uniform(-0.15, 0.15)))
                else:
                    objs.append(make_rock(cx, cy + sg * Hd * 0.25, Wd * 0.42, Hd * 0.23, hh, rng, coll, rot=rng.uniform(-0.15, 0.15)))
        else:
            objs.append(make_rock(cx, cy, Wd * 0.44, Hd * 0.4, rng.uniform(1.0, 1.4) * hk, rng, coll, rot=rng.uniform(-0.2, 0.2)))
        ROCK.clear()
        ROCK.update(save)
        return objs
    return make_rock_cluster(x0, y0, w, h, cs, rng, coll)


def build_structures(rp, router, attach, coll, rng):
    """整块连在一起的障碍物（房子、树、砌墙）：按墙格的连通块建，跨了几波塌陷的切开。返回已经处理掉的墙格集合。"""
    kind = ROCK.get("kind", "rocks")
    cs = rp.cell
    allw = {(x, y) for y in range(rp.H) for x in range(rp.W) if rp.grid[y][x] == 1}
    if kind == "garden":                            # 每组石头里一棵长在石台上的樱花树，其余的格子还是石头
        k1 = max(1, int(round(1.0 / cs)))
        used = set()
        for i, comp in enumerate(PR.wall_components(rp.grid)):
            cset = set(comp)
            cands = [(x, y) for (x, y) in comp if all((x + i_, y + j_) in cset for i_ in range(k1) for j_ in range(k1))]
            if not cands:
                continue
            mx, my = rp.Wm / 2 / cs, rp.Hm / 2 / cs
            x0, y0 = max(cands, key=lambda q: (q[0] - mx) ** 2 + (q[1] - my) ** 2)
            cells = [(x0 + i_, y0 + j_) for i_ in range(k1) for j_ in range(k1)]
            tx, ty = (x0 + k1 / 2) * cs, -(y0 + k1 / 2) * cs
            a = k1 * cs / 2
            mb = MeshBuilder()
            PR.stack(mb, tx, ty, [(a * 1.08, -0.05), (a * 1.04, 0.24), (a * 0.86, 0.36)], [PR.C(PAL["stone_side"]), PR.C(PAL["stone_lit"])],
                     n=7, jitter=0.08, rng=rng, cap=PR.C(PAL["moss"]))
            PR.tree(mb, tx, ty, a * 0.6, rng, trunk=("#6E4A48", "#553838"), crown=("#E06C98", "#F7A6C0", "#FFE2EC"), h=1.2, cr=2.3,
                    z=0.32, knot=False, blobs=6)
            PR.split_build(mb, cells, rp, router, attach, coll, "sakura%d" % i)
            used |= set(cells)
        return used
    if kind == "houses":
        for i, comp in enumerate(PR.wall_components(rp.grid)):
            for j, (x0, y0, w, h) in enumerate(PR.rects_of(comp)):
                mb = MeshBuilder()
                PR.house(mb, x0 * cs, -(y0 + h) * cs, w * cs, h * cs, rng, idx=i * 2 + j + 1)
                cells = [(x, y) for y in range(y0, y0 + h) for x in range(x0, x0 + w)]
                PR.split_build(mb, cells, rp, router, attach, coll, "house%d_%d" % (i, j))
        return allw
    if kind == "trees":
        k1 = max(1, int(round(1.0 / cs)))
        for i, comp in enumerate(PR.wall_components(rp.grid)):
            mb = MeshBuilder()
            for (x0, y0, w, h) in PR.rects_of(comp):
                nx, ny = max(1, w // k1), max(1, h // k1)
                for jy in range(ny):
                    for jx in range(nx):
                        sw, sh = w * cs / nx, h * cs / ny
                        PR.tree(mb, x0 * cs + (jx + 0.5) * sw, -(y0 * cs + (jy + 0.5) * sh), min(sw, sh) / 2, rng,
                                crown=[PAL.get("crown_pal", ("#2F8050", "#4FA65C", "#9AD476")), ("#2F8050", "#5CB058", "#BCE070"),
                                       ("#22705A", "#3C9670", "#86CCA0")][int(rng.integers(0, 3))], h=rng.uniform(1.2, 1.4),
                                cr=rng.uniform(1.6, 1.85), mush=rng.random() < 0.6)
            PR.split_build(mb, comp, rp, router, attach, coll, "tree%d" % i)
        return allw
    if kind == "masonry":
        grid, H, W = rp.grid, rp.H, rp.W
        mbs = {}

        def isw(x, y):
            return 0 <= x < W and 0 <= y < H and grid[y][x] == 1

        stones = [PR.C(PAL["stone_lit"]), PR.C(PAL["stone_top"]), PR.C(PAL["stone_side"]), PR.C(PAL["stone_lit"], 1.06)]
        dark, moss = PR.C(PAL["stone_base"]), PR.C(PAL["moss"])
        ch = 0.29
        for y in range(H):
            for x in range(W):
                if grid[y][x] != 1:
                    continue
                gid = router((x + 0.5) * cs, (y + 0.5) * cs)
                mb = mbs.setdefault(gid, MeshBuilder())
                nb = sum(isw(x + dx, y + dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                f = 0.5 + 0.5 * math.sin(x * 0.83 + 1.3) * math.cos(y * 0.97 + 0.4)
                nc = 3 + int(round(1.7 * f)) + (1 if nb >= 3 else 0) - (1 if nb <= 1 else 0)
                nc = min(5, max(2, nc))
                X0, Y0 = x * cs, -(y + 1) * cs
                horiz = isw(x - 1, y) or isw(x + 1, y)
                PR.box(mb, X0 + 0.03, X0 + cs - 0.03, Y0 + 0.03, Y0 + cs - 0.03, -0.03, nc * ch - 0.03, dark)
                for k in range(nc):
                    z0, z1 = k * ch + 0.008, (k + 1) * ch - 0.008
                    last = k == nc - 1
                    j = 0.006

                    def blk(xa, xb, ya, yb):
                        col = PR.C(stones[int(rng.integers(0, len(stones)))], rng.uniform(0.93, 1.05) * (0.92 if k == 0 else 1.0))
                        topc = moss if (last and rng.random() < ROCK.get("mossy", 0.5)) else PR.C(col, 1.1)
                        PR.box(mb, xa + j, xb - j, ya + j, yb - j, z0, z1, col, top=topc,
                               rz=(rng.uniform(-0.07, 0.07) if last else 0.0))
                    if (k + x + y) % 2 == 0:
                        blk(X0, X0 + cs, Y0, Y0 + cs)
                    elif horiz:
                        blk(X0, X0 + cs / 2, Y0, Y0 + cs)
                        blk(X0 + cs / 2, X0 + cs, Y0, Y0 + cs)
                    else:
                        blk(X0, X0 + cs, Y0, Y0 + cs / 2)
                        blk(X0, X0 + cs, Y0 + cs / 2, Y0 + cs)
        for gid, mb in mbs.items():
            attach(mb.build("wall_%d" % gid, PR.mats(), coll), gid)
        return allw
    return set()


def build_field(rp, router, attach, coll, rng, aux):
    """场内专属的大件：喷泉、桥头桩。"""
    cs = rp.cell
    field = THEME.get("field", ())
    if "fountain" in field:
        H, W = rp.H, rp.W
        seen = set()
        for y in range(H):
            for x in range(W):
                if rp.grid[y][x] != 2 or (x, y) in seen:
                    continue
                st, comp, border = [(x, y)], [], False
                seen.add((x, y))
                while st:
                    q = st.pop()
                    comp.append(q)
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        r = (q[0] + dx, q[1] + dy)
                        if not (0 <= r[0] < W and 0 <= r[1] < H):
                            border = True
                        elif rp.grid[r[1]][r[0]] == 2 and r not in seen:
                            seen.add(r)
                            st.append(r)
                if border or not (6 <= len(comp) <= 80):
                    continue
                xs, ys = [c[0] for c in comp], [c[1] for c in comp]
                rx, ry = (max(xs) - min(xs) + 1) * cs / 2, (max(ys) - min(ys) + 1) * cs / 2
                mb = MeshBuilder()
                PR.fountain(mb, (min(xs) + max(xs) + 1) * cs / 2, -(min(ys) + max(ys) + 1) * cs / 2, rx, ry)
                mb.build("fountain", PR.mats(), coll)
    if "bridges" in field and "bridges" in aux:
        for bi, (bx, by, ax, ay, hl, hw) in enumerate(np.asarray(aux["bridges"]).reshape(-1, 6)):
            px, py = -ay, ax
            hl2 = max(0.2, hl - 0.12)
            for sa in (-1, 1):
                for sp in (-1, 1):
                    mx, my = bx + ax * hl2 * sa + px * (hw - 0.03) * sp, by + ay * hl2 * sa + py * (hw - 0.03) * sp
                    mb = MeshBuilder()
                    PR.bridge_post(mb, mx, -my)
                    attach(mb.build("post", PR.mats(), coll), router(mx - px * 0.15 * sp, my - py * 0.15 * sp))
            for sp in (-1, 1):                      # 两侧各一条垂在桥板下面的绳子（不做成栏杆：这里是会掉下去的）
                pts = []
                for i in range(9):
                    t = i / 8
                    u = (t * 2 - 1) * hl2
                    sag = 0.46 - 0.62 * (1 - (2 * t - 1) ** 2) ** 0.8
                    off = hw + 0.03 + 0.05 * (1 - (2 * t - 1) ** 2)
                    pts.append((bx + ax * u + px * off * sp, -(by + ay * u + py * off * sp), sag))
                mb = MeshBuilder()
                PR.tube(mb, pts, 0.022, PR.C("#E8D2A0"), seg=5)
                attach(mb.build("rope", PR.mats(), coll), router(bx, by))


# ---------------------------------------------------------------- 高草、点缀：按"归属"分组攒网格
class Groups:
    """每个归属（-1 = 永久陆地，>=0 = 塌陷碎块号）一个 MeshBuilder；材质槽统一。"""
    MATS = ("tall", "tuft", "flower", "pebble", "leaf")

    def __init__(self):
        self.mb = {}

    def get(self, gid):
        if gid not in self.mb:
            self.mb[gid] = MeshBuilder()
        return self.mb[gid]

    def materials(self):
        return [SL.mat_vcol("M_tallgrass", rough=0.7, spec=0.25), SL.mat_vcol("M_tufts", rough=0.8, spec=0.2),
                SL.mat_vcol("M_flowers", rough=0.6, spec=0.3, emis=0.1), SL.mat_vcol("M_pebble", rough=0.9, spec=0.15),
                SL.mat_vcol("M_leaf", rough=0.7, spec=0.25)]


def add_tall_grass(G, router, grid, cs, rng, density=230, blocks=None):
    """高草。blocks 给了就每个草丛格单独攒一份网格（以格心为原点），之后各建一个物体：角色、飞镖碰到会被压弯、抖动。"""
    H, W = len(grid), len(grid[0])
    base = np.array(hexc(PAL['tall_base']))
    midc = np.array(hexc(PAL['tall_mid']))
    tip = np.array(hexc(PAL['tall_tip']))
    cells = {(x, y) for y in range(H) for x in range(W) if grid[y][x] == 3}
    tflower = [np.array(hexc(c)) for c in PAL.get("tall_flower", [])]
    ico_v, ico_f = geom.icosphere(1)

    def open_dist(x, y, fx, fy):
        ds = []
        if (x - 1, y) not in cells: ds.append(fx)
        if (x + 1, y) not in cells: ds.append(1 - fx)
        if (x, y - 1) not in cells: ds.append(fy)
        if (x, y + 1) not in cells: ds.append(1 - fy)
        d = min(ds) if ds else 1.0
        cx_open = ((x - 1, y) not in cells and fx < 0.3) or ((x + 1, y) not in cells and fx > 0.7)
        cy_open = ((x, y - 1) not in cells and fy < 0.3) or ((x, y + 1) not in cells and fy > 0.7)
        if cx_open and cy_open:
            ex = fx if fx < 0.5 else 1 - fx
            ey = fy if fy < 0.5 else 1 - fy
            d = min(d, 0.3 - math.hypot(0.3 - ex, 0.3 - ey))
        return d * cs

    n = max(8, int(density * cs * cs))
    for (x, y) in sorted(cells):
        ctr = np.array([(x + 0.5) * cs, -(y + 0.5) * cs, 0.0])
        gb = blocks.setdefault((x, y), MeshBuilder()) if blocks is not None else None
        px = rng.uniform(-0.03, 1.03, n)
        py = rng.uniform(-0.03, 1.03, n)
        for i in range(n):
            fx, fy = px[i], py[i]
            ed = open_dist(x, y, fx, fy)
            if ed < -0.02 or (ed < 0.1 and rng.random() < 0.35):
                continue
            k_edge = min(1.0, max(0.0, ed) / 0.28)
            hgt = rng.uniform(0.38, 0.66) * (0.6 + 0.4 * k_edge)
            wid = rng.uniform(0.05, 0.09)
            ang = rng.uniform(0, 2 * math.pi)
            ox, oy = fx - 0.5, fy - 0.5
            lean = hgt * (rng.uniform(0.08, 0.2) + (1 - k_edge) * 0.35)
            la = math.atan2(-oy, ox) + rng.normal(0, 0.6)
            mx, my = (x + fx) * cs, (y + fy) * cs
            bx, by = mx, -my
            ca, sa = math.cos(ang), math.sin(ang)
            lx, ly = math.cos(la) * lean, math.sin(la) * lean
            V = np.array([
                (bx - ca * wid, by - sa * wid, -0.02), (bx + ca * wid, by + sa * wid, -0.02),
                (bx + lx * 0.3 - ca * wid * 0.75, by + ly * 0.3 - sa * wid * 0.75, hgt * 0.45),
                (bx + lx * 0.3 + ca * wid * 0.75, by + ly * 0.3 + sa * wid * 0.75, hgt * 0.45),
                (bx + lx * 0.75 - ca * wid * 0.35, by + ly * 0.75 - sa * wid * 0.35, hgt * 0.82),
                (bx + lx * 0.75 + ca * wid * 0.35, by + ly * 0.75 + sa * wid * 0.35, hgt * 0.82),
                (bx + lx, by + ly, hgt)])
            sh = 1 + rng.uniform(-0.07, 0.07)
            t2, m2 = tip * sh, midc * sh
            if tflower and k_edge > 0.3 and rng.random() < 0.085:      # 草尖上开一朵花
                fc = tflower[int(rng.integers(0, len(tflower)))]
                r_ = rng.uniform(0.04, 0.058)
                Vf = geom.transform(ico_v, scale=(r_, r_, r_ * 0.75), loc=(bx + lx, by + ly, hgt + 0.01))
                if gb is not None:
                    gb.add(Vf - ctr, ico_f, mat=2, vcolors=np.tile(fc, (len(Vf), 1)))
                else:
                    G.get(router(mx, my)).add(Vf, ico_f, mat=2, vcolors=np.tile(fc, (len(Vf), 1)))
            cols = [base, base, m2, m2, (m2 + t2) / 2, (m2 + t2) / 2, t2]
            cols = [tuple(c[:3]) + (1.0,) for c in cols]
            if gb is not None:
                gb.add(V - ctr, [(0, 1, 3, 2), (2, 3, 5, 4), (4, 5, 6)], mat=0, vcolors=cols)
            else:
                G.get(router(mx, my)).add(V, [(0, 1, 3, 2), (2, 3, 5, 4), (4, 5, 6)], mat=0, vcolors=cols)


def add_details(G, router, rp, aux, rng, rects):
    """草簇、花丛、小石子、落叶、石头脚下的叶丛、岛沿垂草。只放在实地（非墙、非草丛）上，都很矮，不挡判断。"""
    grid, cs = rp.grid, rp.cell
    H, W = rp.H, rp.W
    g_dark = np.array(hexc(PAL['tuft_dark']))
    g_light = np.array(hexc(PAL['tuft_light']))
    fl_main = [hexc(c) for c in PAL['flower_main']]
    fl_acc = [hexc(c) for c in PAL['flower_acc']]
    ico_v, ico_f = geom.icosphere(1)
    dirt, quiet, appm, amg = aux["dirt"], aux["quiet"], float(aux["ppm"]), float(aux["mg"])

    def aval(arr, mx, my):
        ix = int((mx + amg) * appm)
        iy = int((my + amg) * appm)
        if 0 <= iy < arr.shape[0] and 0 <= ix < arr.shape[1]:
            return float(arr[iy, ix])
        return 0.0

    def cell(mx, my):
        cx, cy = int(math.floor(mx / cs)), int(math.floor(my / cs))
        if 0 <= cx < W and 0 <= cy < H:
            return grid[cy][cx]
        return 2

    def ok(mx, my, margin=0.2):
        if cell(mx, my) != 0:
            return False
        for dx, dy in ((-margin, 0), (margin, 0), (0, -margin), (0, margin)):
            if cell(mx + dx, my + dy) in (1, 2):
                return False
        return True

    def tuft(mx, my, s=1.0, lean=None, mat=1, n=None):
        mb = G.get(router(mx, my))
        for k in range(n or int(rng.integers(4, 7))):
            a = rng.uniform(0, 2 * math.pi)
            h = rng.uniform(0.08, 0.16) * s
            w = 0.02 * s
            ox, oy = rng.normal(0, 0.03 * s, 2)
            lx, ly = math.cos(a) * h * 0.45, math.sin(a) * h * 0.45
            if lean is not None:
                lx += lean[0] * h * 0.7
                ly += lean[1] * h * 0.7
            bx, by = mx + ox, -(my + oy)
            V = [(bx - w * math.sin(a), by + w * math.cos(a), -0.01), (bx + w * math.sin(a), by - w * math.cos(a), -0.01),
                 (bx + lx, by - ly, h)]
            c = g_dark + (g_light - g_dark) * rng.uniform(0.2, 1)
            cols = [tuple(g_dark[:3]) + (1,), tuple(g_dark[:3]) + (1,), tuple(c[:3]) + (1,)]
            mb.add(np.array(V), [(0, 1, 2)], mat=mat, vcolors=cols)

    area = sum(1 for y in range(H) for x in range(W) if grid[y][x] == 0) * cs * cs
    det = dict(themes.BASE["detail"])
    det.update(THEME.get("detail", {}))
    # 主题专属的小物（都很矮很小，放在没人走的地方）
    for ex in det.get("extras", ()):
        fn = PR.DETAILS[ex]
        if ex in ("pots", "rubble"):                # 贴着墙脚放
            for (x0, y0, w, h) in rects:
                for k in range(1 + int(w * cs)):
                    if ex == "pots":
                        mx, my = (x0 + rng.uniform(0.1, 0.9) * w) * cs, (y0 + h) * cs + 0.13
                        if rng.random() < 0.45:
                            continue
                    else:
                        side = int(rng.integers(0, 4))
                        t = rng.uniform(0.05, 0.95)
                        mx, my = [((x0 + t * w) * cs, y0 * cs - 0.2), ((x0 + t * w) * cs, (y0 + h) * cs + 0.2),
                                  (x0 * cs - 0.2, (y0 + t * h) * cs), ((x0 + w) * cs + 0.2, (y0 + t * h) * cs)][side]
                        if rng.random() < 0.5:
                            continue
                    if ok(mx, my, 0.1):
                        fn(G.get(router(mx, my)), mx, -my, rng)
            continue
        dens = dict(cactus=0.06, dryshrub=0.14, mushroom=0.16, fern=0.16, heather=0.14).get(ex, 0.1)
        for _ in range(int(area * dens * 3)):
            mx, my = rng.uniform(0, rp.Wm), rng.uniform(0, rp.Hm)
            if not ok(mx, my, 0.22) or aval(dirt, mx, my) > 0.15 or rng.random() > 0.15 + 0.85 * aval(quiet, mx, my):
                continue
            if rng.random() < 0.67:
                continue
            fn(G.get(router(mx, my)), mx, -my, rng)
    # 草簇：到处都有，土路上少
    for _ in range(int(area * det["tuft"])):
        mx, my = rng.uniform(0, rp.Wm), rng.uniform(0, rp.Hm)
        if ok(mx, my, 0.12) and rng.random() > aval(dirt, mx, my) * 0.9:
            tuft(mx, my, s=rng.uniform(0.8, 1.3))
    # 花丛：成片地开在没人去的角落，主色白黄，少量珊瑚粉点缀
    for _ in range(int(area * det["flower"])):
        mx, my = rng.uniform(0, rp.Wm), rng.uniform(0, rp.Hm)
        if not ok(mx, my, 0.25) or aval(dirt, mx, my) > 0.2 or rng.random() > 0.25 + 0.75 * aval(quiet, mx, my):
            continue
        col = fl_main[int(rng.integers(0, 3))] if rng.random() < 0.8 else fl_acc[int(rng.integers(0, 3))]
        spread = rng.uniform(0.14, 0.3)
        for j in range(int(rng.integers(6, 16))):
            fx, fy = mx + rng.normal(0, spread), my + rng.normal(0, spread)
            if not ok(fx, fy, 0.1):
                continue
            r = rng.uniform(0.03, 0.046)
            z = rng.uniform(0.05, 0.12)
            mb = G.get(router(fx, fy))
            V = geom.transform(ico_v, scale=(r, r, r * 0.6), loc=(fx, -fy, z))
            mb.add(V, ico_f, mat=2, vcolors=np.tile(col, (len(V), 1)))
            V = geom.transform(ico_v, scale=(r * 0.4, r * 0.4, r * 0.5), loc=(fx, -fy, z + r * 0.35))
            mb.add(V, ico_f, mat=2, vcolors=np.tile(hexc(PAL['flower_core']), (len(V), 1)))
            tuft(fx, fy, s=0.55, n=2)
    # 小石子：土路边上
    for _ in range(int(area * det["pebble"])):
        mx, my = rng.uniform(0, rp.Wm), rng.uniform(0, rp.Hm)
        d = aval(dirt, mx, my)
        if not ok(mx, my, 0.15) or not (0.15 < d < 0.9) or rng.random() < 0.5:
            continue
        mb = G.get(router(mx, my))
        for j in range(int(rng.integers(1, 4))):
            ox, oy = rng.normal(0, 0.08, 2)
            r = rng.uniform(0.03, 0.065)
            V = geom.transform(ico_v, scale=(r * 1.25, r, r * 0.55), rot=(0, 0, rng.uniform(0, 6.28)),
                               loc=(mx + ox, -(my + oy), 0.005))
            c = np.array(hexc(PAL['stone_lit'])) * rng.uniform(0.85, 1.08)
            c[3] = 1
            mb.add(V, ico_f, mat=3, vcolors=np.tile(np.clip(c, 0, 1), (len(V), 1)), smooth=False)
    # 落叶：几片金黄 / 橙色的小叶子贴在地上
    lc = [hexc(c) for c in PAL['leaf_fall']]
    for _ in range(int(area * det["leaf"])):
        mx, my = rng.uniform(0, rp.Wm), rng.uniform(0, rp.Hm)
        if not ok(mx, my, 0.1):
            continue
        mb = G.get(router(mx, my))
        a = rng.uniform(0, 6.28)
        L, w = rng.uniform(0.07, 0.11), rng.uniform(0.03, 0.045)
        ca, sa = math.cos(a), math.sin(a)
        V = np.array([(mx - ca * L, -my - sa * L, 0.012), (mx - sa * w, -my + ca * w, 0.02),
                      (mx + ca * L, -my + sa * L, 0.012), (mx + sa * w, -my - ca * w, 0.02)])
        mb.add(V, [(0, 1, 2, 3)], mat=4, vcolors=np.tile(lc[int(rng.integers(0, 3))], (4, 1)), smooth=False)
    # 石头脚下的叶丛
    c0, c1 = np.array(hexc(PAL['plant0'])), np.array(hexc(PAL['plant1']))
    for (x0, y0, w, h) in rects:
        for k in range(2 + int((w + h) * cs * 1.2)):
            side = int(rng.integers(0, 4))
            t = rng.uniform(0.08, 0.92)
            if side == 0:
                mx, my = (x0 + t * w) * cs, y0 * cs - 0.03
            elif side == 1:
                mx, my = (x0 + t * w) * cs, (y0 + h) * cs + 0.03
            elif side == 2:
                mx, my = x0 * cs - 0.03, (y0 + t * h) * cs
            else:
                mx, my = (x0 + w) * cs + 0.03, (y0 + t * h) * cs
            if cell(mx, my) != 0:
                continue
            mb = G.get(router(mx, my))
            size = rng.uniform(0.16, 0.26)
            for j in range(int(rng.integers(5, 9))):
                a = rng.uniform(0, 2 * math.pi)
                L = size * rng.uniform(0.7, 1.1)
                wd = L * 0.22
                ca, sa = math.cos(a), math.sin(a)
                bx, by = mx, -my
                V = np.array([(bx, by, 0.01), (bx + ca * L * .5 - sa * wd, by + sa * L * .5 + ca * wd, L * 0.55),
                              (bx + ca * L, by + sa * L, L * 0.32),
                              (bx + ca * L * .5 + sa * wd, by + sa * L * .5 - ca * wd, L * 0.55)])
                c = c0 + (c1 - c0) * rng.uniform(0.3, 1.0)
                cols = [tuple(c0[:3]) + (1,), tuple(c[:3]) + (1,), tuple(c1[:3]) + (1,), tuple(c[:3]) + (1,)]
                mb.add(V, [(0, 1, 2, 3)], mat=4, vcolors=cols)
    # 岛沿垂下的小草
    for y in range(H):
        for x in range(W):
            if grid[y][x] in (1, 2):
                continue
            for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < W and 0 <= ny < H and grid[ny][nx] != 2:
                    continue
                if rng.random() > min(1.0, det["tuft"] / 1.2):
                    continue
                for k in range(max(1, int(round(rng.uniform(1.0, 2.6) * cs)))):
                    t = rng.uniform(0.12, 0.88)
                    if dx != 0:
                        mx, my = (x + (0.5 + dx * 0.5)) * cs - dx * 0.05, (y + t) * cs
                    else:
                        mx, my = (x + t) * cs, (y + (0.5 + dy * 0.5)) * cs - dy * 0.05
                    tuft(mx, my, s=rng.uniform(1.0, 1.5), lean=(dx, dy))


# ---------------------------------------------------------------- 场外：台地、树、云
def auto_decor(Wm, Hm):
    """按场地大小自动布置场外的岛：左右两座高台地框住画面，上方远处几座低一些的岛，下面两个角各一小块。
    (岛名, X, Y, 顶面高, 旋转度, 内容)。场地四周至少留 1.5 米虚空；台地比场地高，一看就不是能走的地方。"""
    out = [("m06", -5.5, -Hm / 2 + 0.3, 1.5, 0, 'mixed'), ("m07", Wm + 5.3, -Hm / 2 - 0.3, 1.2, 180, 'trees'),
           ("m00", 1.0 + Wm * 0.05, 4.8, -1.8, 90, 'trees'), ("m01", Wm - 1.0 - Wm * 0.05, 5.0, -2.2, 95, 'mixed'),
           ("m03", -2.2, -(Hm + 2.8), -0.3, 20, 'bushes'), ("m04", Wm + 2.4, -(Hm + 3.0), -0.5, -15, 'bushes'),
           ("s01", -1.0, 1.4, -3.0, 0, 'none'), ("s03", Wm + 1.0, 1.1, -2.5, 0, 'none'),
           ("s00", Wm * 0.3, -(Hm + 3.4), -2.2, 30, 'none')]
    if Wm > 20:
        out.append(("m02", Wm / 2, 6.6, -3.6, 0, 'mixed'))
    if Hm > 13.5:
        out.append(("m04", -5.6, -Hm - 0.5, 0.6, 60, 'trees'))
        out.append(("m03", Wm + 5.6, -Hm - 0.8, 0.4, 200, 'mixed'))
    return out


def place_decor(placements, coll, rng, dd, center=(0.0, 0.0)):
    trunk, leaf, tufts, flowers = MeshBuilder(), MeshBuilder(), MeshBuilder(), MeshBuilder()
    tp = PAL['tree_pals']
    wsum = sum(w for w, _ in tp)

    def pick_pal(r):
        acc = 0.0
        for w, pal in tp:
            acc += w / wsum
            if r <= acc:
                return pal
        return tp[-1][1]

    D = dict(themes.BASE["decor"])
    D.update(THEME.get("decor", {}))
    pine_ratio = D.get("pines", 0.4)
    mix = D.get("mix")
    solid = MeshBuilder()
    feats = list(D.get("features", ()))
    pri = sorted([i for i, p in enumerate(placements) if p[5] != 'none'],
                 key=lambda i: {"m01": 0, "m00": 1, "m07": 2, "m06": 3, "m02": 4}.get(placements[i][0], 9))
    feat_for = dict(zip(pri, feats))
    SPACE = dict(house=1.7, bigtree=1.25, spire=1.1, sakura=1.1, palm=1.0)

    def pick_kind(r):
        acc, tot = 0.0, sum(w for w, _ in mix)
        for w, k in mix:
            acc += w / tot
            if r <= acc:
                return k
        return mix[-1][1]

    for pi, (nm, X, Y, Z, rot, kind) in enumerate(placements):
        if nm in ("m06", "m07"):
            Z += D.get("dz", 0.0)
        ob = load_terrain(dd, nm, "decor_" + nm, coll, None, (0, 0, 1, 1), sharp_deg=33)
        ob.location = (X, Y, Z)
        ob.rotation_euler = (0, 0, math.radians(rot))
        V, S = dd[nm + "__V"], dd[nm + "__S"]
        top = V[(V[:, 2] > -0.01) & (S[:, 0] > 0.45)]
        if len(top) == 0:
            continue
        idx = rng.permutation(len(top))
        c, s = math.cos(math.radians(rot)), math.sin(math.radians(rot))
        placed = []
        span = (V[:, 0].max() - V[:, 0].min()) * (V[:, 1].max() - V[:, 1].min())
        ntree = max(1, int(span / 2.4 * D.get("density", 1.0))) if kind != 'none' else 0
        if pi in feat_for:                              # 这个主题只此一处的大件，放在岛的正中
            St = S[(V[:, 2] > -0.01) & (S[:, 0] > 0.45)]
            need = min(0.75, float(St[:, 0].max()) * 0.85)
            wxs, wys = X + c * top[:, 0] - s * top[:, 1], Y + s * top[:, 0] + c * top[:, 1]
            d2 = (wxs - center[0]) ** 2 + (wys - center[1]) ** 2 + 1e6 * (St[:, 0] < need)
            kk = int(np.argmin(d2))
            px, py = top[kk][0], top[kk][1]
            wx, wy = float(wxs[kk]), float(wys[kk])
            fname = feat_for[pi]
            PR.FEATURES[fname](wx, wy, Z, PR.FEATURE_SCALE.get(fname, 1.0), rng, trunk, leaf, solid)
            for (ox, oy) in ((0, 0), (1.2, 0), (-1.2, 0), (0, 0.9), (0, -0.9)):
                placed.append((px + ox, py + oy))
        ntree += len(placed)
        for k in idx:
            if len(placed) >= ntree:
                break
            px, py = top[k][0], top[k][1]
            r = rng.random()
            tk = None
            if mix and (kind != 'bushes' or not D.get("bushes", True)):
                tk = pick_kind(r)
            gap = SPACE.get(tk, 0.95)
            if any((px - qx) ** 2 + (py - qy) ** 2 < gap ** 2 for qx, qy in placed):
                continue
            placed.append((px, py))
            wx, wy = X + c * px - s * py, Y + s * px + c * py
            sc = rng.uniform(0.9, 1.4) * (0.75 if (kind == 'bushes' and tk) else 1.0)
            if tk == "pine" or (tk is None and (kind == 'pines' or (kind != 'bushes' and r < pine_ratio * (1.0 if kind == 'mixed' else 0.5)))):
                SL.make_pine(wx, wy, Z, sc * 1.15, rng, trunk, leaf, palette=PAL['pine_pal'])
            elif tk is None and kind == 'bushes':
                SL.make_bush(wx, wy, Z, sc * 1.4, rng, leaf, palette=PAL['bush_pal'])
            elif tk is None or tk == "round":
                SL.make_round_tree(wx, wy, Z, sc, rng, trunk, leaf, palette=pick_pal(rng.random()))
            else:
                PR.FAR[tk](wx, wy, Z, sc, rng, trunk, leaf, solid, pick_pal(rng.random()))
        if not D.get("bushes", True):
            idx = idx[:0]
        for k in idx[::3][:30]:
            px, py = top[k][0], top[k][1]
            if any((px - qx) ** 2 + (py - qy) ** 2 < 0.7 ** 2 for qx, qy in placed):
                continue
            wx, wy = X + c * px - s * py, Y + s * px + c * py
            SL.make_bush(wx, wy, Z, rng.uniform(0.7, 1.2), rng, leaf, palette=PAL['bush_pal'])
            placed.append((px, py))
        if kind != 'none':
            iv, iff = geom.icosphere(1)
            fcols = [hexc(c) for c in PAL['flower_main']]
            for k in rng.choice(len(top), size=min(len(top), int(span * 0.3)), replace=False):
                px, py = top[k][0], top[k][1]
                wx, wy = X + c * px - s * py, Y + s * px + c * py
                col = fcols[int(rng.integers(0, len(fcols)))]
                for j in range(int(rng.integers(3, 7))):
                    ox, oy = rng.normal(0, 0.1, 2)
                    r = rng.uniform(0.03, 0.045)
                    Vf = geom.transform(iv, scale=(r, r, r * 0.7), loc=(wx + ox, wy + oy, Z + rng.uniform(0.03, 0.1)))
                    flowers.add(Vf, iff, vcolors=np.tile(col, (len(Vf), 1)))
    if solid.n:
        solid.build("decor_solid", PR.far_mats(), coll)
    if flowers.n:
        flowers.build("decor_flowers", [SL.mat_vcol("M_flowers", rough=0.6, spec=0.3, emis=0.1)], coll)
    if trunk.n:
        trunk.build("decor_trunks", [SL.mat_vcol("M_trunk", rough=0.9, spec=0.1, haze=True)], coll)
    if leaf.n:
        leaf.build("decor_leaves", [SL.mat_vcol("M_leaves", rough=0.85, spec=0.15, haze=True)], coll)


def waterfall_from(dd, nm, X, Y, Z, rot, along, coll, width=0.6, drop=12.0, seed=4):
    V = dd[nm + "__V"]
    c, s = math.cos(math.radians(rot)), math.sin(math.radians(rot))
    top = V[(V[:, 2] > -0.01)]
    wx = X + c * top[:, 0] - s * top[:, 1]
    wy = Y + s * top[:, 0] + c * top[:, 1]
    sel = np.abs(wy - along) < 0.25
    if not sel.any():
        return None
    k = np.argmax(wx[sel])
    return SL.make_waterfall(wx[sel][k] - 0.05, wy[sel][k], Z, width, drop, out_dir=(1.0, 0.0), coll=coll, seed=seed)


def std_light(az=None, el=None):
    L = THEME.get("light", themes.BASE["light"])
    SL.setup_world(strength=L["ambient"])
    return SL.add_sun(azimuth_deg=az or L["az"], elevation_deg=el or L["el"], energy=L["energy"], color=L["color"],
                      angle_deg=5)


# ---------------------------------------------------------------- 总装
class Env:
    """场景句柄。states[k]：前 k 波塌掉后的岛体；pieces[i] = dict(ob, wave, cx, cy, d_in, ...)。"""

    def __init__(self):
        self.states = []
        self.pieces = []
        self.coll = None
        self.grass = []         # [物体, 格心 x, 格心 y, 相位, 是否被碰动过]
        self.clouds = []        # [(物体, 每步飘多少米)]
        self.fall = None        # 飘落物（花瓣、落叶、雪）


def build_env(rp, seed=5, decor=True, clouds=True):
    apply_theme(rp)
    d = os.path.join(CACHE, rp.map_key())
    assert os.path.exists(os.path.join(d, "ok.json")), "先跑 prep.py：" + d
    env = Env()
    coll = env.coll = new_coll("env")
    rng = np.random.default_rng(seed)
    mg = 0.6
    ground_png = os.path.join(d, "ground.png")
    uv_rect = (-mg, -(rp.Hm + mg), rp.Wm + 2 * mg, rp.Hm + 2 * mg)
    isl = np.load(os.path.join(d, "island.npz"))
    R = len(rp.wave_steps)
    for k in range(R + 1):
        ob = load_terrain(isl, "state%d" % k, "island_%d" % k, coll, ground_png, uv_rect)
        env.states.append(ob)
    pz = np.load(os.path.join(d, "pieces.npz"))
    meta = json.load(open(os.path.join(d, "pieces.json")))
    for m in meta:
        ob = load_terrain(pz, "p%d" % m["id"], "piece_%d" % m["id"], coll, ground_png, uv_rect,
                          center=(m["cx"], -m["cy"]), sharp_deg=40)
        m = dict(m)
        m["ob"] = ob
        m["rnd"] = rng.uniform(0, 1, 6).tolist()
        env.pieces.append(m)
    lab = np.load(os.path.join(d, "labels.npz"))
    labels, near, lorg, lres = lab["labels"], lab["near"], lab["origin"], float(lab["res"])

    def router(mx, my):
        ix = int((mx - lorg[0]) / lres)
        iy = int((my - lorg[1]) / lres)
        if 0 <= iy < labels.shape[0] and 0 <= ix < labels.shape[1]:
            g = int(labels[iy, ix])
            if g < 0 and rp.cell_at(mx, my)[1] >= 0:        # 这一格会塌，但像素落在圆角外：就近归块
                g = int(near[iy, ix])
            return g
        return -1

    def attach(ob, gid):
        if gid >= 0 and env.pieces[gid]["ob"] is not None:
            par = env.pieces[gid]["ob"]
            ob.parent = par
            ob.matrix_parent_inverse = Matrix.Translation((-par.location.x, -par.location.y, 0.0))

    # 石头
    cs = rp.cell
    rects = wall_groups(rp.grid, rp.wave, maxc=max(1, int(round(2.0 / cs))))
    used = build_structures(rp, router, attach, coll, rng)
    g2 = [[0 if (x, y) in used else v for x, v in enumerate(row)] for y, row in enumerate(rp.grid)]
    for (x0, y0, w, h) in wall_groups(g2, rp.wave, maxc=max(1, int(round(2.0 / cs)))):
        gid = router((x0 + w / 2) * cs, (y0 + h / 2) * cs)
        for ob in obstacle_rect(x0, y0, w, h, cs, rng, coll):
            attach(ob, gid)
    # 高草 + 点缀
    G = Groups()
    blocks = {}
    add_tall_grass(G, router, rp.grid, cs, rng, blocks=blocks)
    aux = np.load(os.path.join(d, "aux.npz"))
    build_field(rp, router, attach, coll, rng, aux)
    add_details(G, router, rp, aux, rng, rects)
    mats = G.materials()
    for gid, mb in G.mb.items():
        if mb.n:
            ob = mb.build("deco_%d" % gid, mats, coll)
            attach(ob, gid)
    for (gx, gy), mb in blocks.items():
        if mb.n:
            ob = mb.build("grass_%d_%d" % (gx, gy), mats, coll)
            ob.location = ((gx + 0.5) * cs, -(gy + 0.5) * cs, 0.0)
            attach(ob, router((gx + 0.5) * cs, (gy + 0.5) * cs))
            env.grass.append([ob, (gx + 0.5) * cs, (gy + 0.5) * cs, float(rng.uniform(0, 6.28)), False])
    amb = THEME.get("ambient")
    if amb:
        env.fall = Fall(coll, rp, amb)
    # 场外
    if decor:
        dd = np.load(os.path.join(CACHE, "decor.npz"))
        pl = auto_decor(rp.Wm, rp.Hm)
        place_decor(pl, coll, rng, dd, center=(rp.Wm / 2, -rp.Hm / 2))
        nm, X, Y, Z, rot, _ = pl[0]
        if THEME.get("decor", {}).get("waterfall", True):
            waterfall_from(dd, nm, X, Y, Z + THEME.get("decor", {}).get("dz", 0.0), rot, -rp.Hm * 0.62, coll)
    if clouds:
        cx, cy = rp.Wm / 2, -rp.Hm / 2
        c1 = SL.make_clouds(cx, cy, rp.Wm * 3.6 + 30, rp.Hm * 3.2 + 34, -13.0, seed=seed + 10, coll=coll, count=66,
                            size=(2.5, 5.5))
        c2 = SL.make_clouds(cx, cy + 6, rp.Wm * 3.0 + 20, rp.Hm * 2 + 20, -8.5, seed=seed + 12, coll=coll, count=14,
                            size=(1.6, 3.0))
        env.clouds = [(c1, 0.010), (c2, 0.017)]
    return env


class Fall:
    """漫天慢慢飘落的小片（花瓣、落叶、雪花）：一个网格，每帧改顶点。位置是游戏时间的纯函数。"""

    def __init__(self, coll, rp, spec, seed=11):
        rng = np.random.default_rng(seed)
        n = self.n = int(spec.get("n", 40) * (rp.Wm * rp.Hm) / 216.0)
        self.x0 = rng.uniform(-3.0, rp.Wm + 2.0, n)
        self.y0 = rng.uniform(-2.0, rp.Hm + 3.0, n)
        self.per = rng.uniform(80, 150, n) / spec.get("speed", 1.0)
        self.ph = rng.uniform(0, 1, n)
        self.rot = rng.uniform(0, 6.28, n)
        self.sz = rng.uniform(0.6, 1.0, n) * spec.get("size", 0.06)
        cols = [hexc(c) for c in spec["cols"]]
        C = np.repeat(np.array([cols[int(i)] for i in rng.integers(0, len(cols), n)]), 4, axis=0)
        mb = MeshBuilder()
        mb.add(np.zeros((4 * n, 3)), [(4 * i, 4 * i + 1, 4 * i + 2, 4 * i + 3) for i in range(n)], vcolors=C, smooth=False)
        self.ob = mb.build("fall", [SL.mat_vcol("M_fallbits", rough=0.6, spec=0.2, emis=0.3)], coll)
        try:
            self.ob.visible_shadow = False
        except Exception:
            pass
        self.co = np.zeros((4 * n, 3), dtype=np.float32)

    def update(self, gt):
        u = (gt / self.per + self.ph) % 1.0
        z = 3.6 * (1 - u) - 0.25
        x = self.x0 + 1.1 * u + 0.32 * np.sin(gt * 0.11 + self.rot * 3)
        y = -self.y0 + 0.25 * np.cos(gt * 0.09 + self.rot * 2)
        a = self.rot + gt * 0.19
        t = gt * 0.16 + self.rot * 1.7
        s = self.sz * np.clip(np.minimum(u / 0.06, (1 - u) / 0.08), 0, 1)
        e1 = np.stack([np.cos(a) * s, np.sin(a) * s, np.zeros_like(s)], 1)
        e2 = np.stack([-np.sin(a) * np.cos(t) * s * 0.6, np.cos(a) * np.cos(t) * s * 0.6, np.sin(t) * s * 0.6], 1)
        c = np.stack([x, y, z], 1)
        self.co[0::4] = c + e1
        self.co[1::4] = c + e2
        self.co[2::4] = c - e1
        self.co[3::4] = c - e2
        self.ob.data.vertices.foreach_set("co", self.co.ravel())
        self.ob.data.update()


def set_ambient(env, rp, gt, movers=()):
    """每帧的小动态：水往下流、云慢慢飘、草丛被角色和飞镖碰到会压弯抖动、花瓣落叶飘落。movers：[(x, y, 速度)]（地图坐标）。"""
    SL.set_time(gt / 20.0)
    for ob, v in env.clouds:
        ob.location.x = v * gt
    for g in env.grass:
        ob, gx, gy, ph, dirty = g
        best, ux, uy, sp = 0.0, 0.0, 0.0, 0.0
        for (mx, my, v) in movers:
            dx, dy = gx - mx, gy - my
            if abs(dx) > 0.9 or abs(dy) > 0.9:
                continue
            d = math.hypot(dx, dy)
            k = 1 - d / 0.88
            if k > best:
                best, sp = k, v
                ux, uy = (dx / d, -dy / d) if d > 1e-4 else (0.0, 0.0)
        if best > 0:
            k = best ** 1.3
            tilt = 0.32 * k + 0.08 * k * math.sin(gt * 2.3 + ph) * min(1.0, sp / 0.08)
            ob.rotation_euler = (-uy * tilt, ux * tilt, 0.0)
            ob.scale = (1 + 0.1 * k, 1 + 0.1 * k, 1 - 0.3 * k)
            g[4] = True
        elif dirty:
            ob.rotation_euler = (0.0, 0.0, 0.0)
            ob.scale = (1.0, 1.0, 1.0)
            g[4] = False
    if env.fall is not None:
        env.fall.update(gt)


def set_collapse(env, rp, gt):
    """按游戏时间 gt（步）摆好地形：哪个岛体状态可见，哪些碎块在抖 / 在掉 / 已经没了。"""
    warn_n = rp.cfg.get("COL_WARN", 30)
    ws = rp.wave_steps
    # 小局结束以后不再塌新的一波（结算镜头里胜者脚下的地不能没了）：结束时还没塌的波，预警也收回
    live = [not (gt > rp.T + 1.0 and s > rp.T) for s in ws]
    started = sum(1 for i, s in enumerate(ws) if gt >= s - warn_n and live[i])        # 已经开始预警（裂开）的波数
    for k, ob in enumerate(env.states):
        if ob is not None:
            hide = (k != started)
            if ob.hide_render != hide:
                ob.hide_render = hide
    dmax = {}
    for p in env.pieces:
        dmax[p["wave"]] = max(dmax.get(p["wave"], 0.1), p["d_in"])
    for p in env.pieces:
        ob = p["ob"]
        if ob is None:
            continue
        w = p["wave"]
        t_col = ws[w]
        r = p["rnd"]
        kids = True                                         # 块上的石头、草：块没掉出画面之前一直在
        if gt < t_col - warn_n or not live[w]:
            vis, loc, rot, col = False, (p["cx"], -p["cy"], 0.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0, 1.0)
        else:
            u = gt - t_col                                  # < 0 预警中；>= 0 已开始塌
            # 外沿的块先掉，裂缝往里传：最多晚 0.35 秒
            delay = (1.0 - p["d_in"] / dmax[w]) * 5.0 + r[0] * 2.0
            if u < delay:
                k = min(1.0, (u + warn_n) / (warn_n + delay))        # 0..1 预警进度
                amp = 0.006 + 0.03 * k * k
                ph = gt * (2.2 + 2.5 * k) + r[1] * 20
                dz = -0.02 - 0.06 * k * k + math.sin(ph * 2.1) * amp * 0.5
                loc = (p["cx"] + math.sin(ph) * amp, -p["cy"] + math.cos(ph * 1.3) * amp, dz)
                tilt = 0.012 + 0.05 * k * k
                rot = (math.sin(ph * 0.9 + r[2] * 6) * tilt, math.cos(ph * 1.1 + r[3] * 6) * tilt, 0.0)
                pulse = 0.5 + 0.5 * math.sin(gt * (0.5 + 0.9 * k) * 2 * math.pi / 3.0)
                f = (0.25 + 0.75 * pulse) * (0.4 + 0.6 * k)
                col = (1.0, 1.0, 1.0, 1.0 - 0.5 * f)
                vis = True
            else:
                tau = (u - delay) / 20.0
                drop = 0.5 * 9.8 * 1.6 * tau * tau + 0.6 * tau
                vis = kids = drop < 16.0
                ox, oy = p["cx"] - rp.Wm / 2, p["cy"] - rp.Hm / 2
                L = math.hypot(ox, oy) + 1e-6
                out = 0.5 * tau
                loc = (p["cx"] + ox / L * out, -(p["cy"] + oy / L * out), -0.08 - drop)
                ax = r[4] * 2 * math.pi
                ang = (0.3 + 1.6 * r[5]) * tau * 1.6
                rot = (math.cos(ax) * ang, math.sin(ax) * ang, (r[2] - 0.5) * tau)
                col = (1.0, 1.0, 1.0, 1.0)
        if ob.hide_render != (not vis):
            ob.hide_render = not vis
        for ch in ob.children:
            if ch.hide_render != (not kids):
                ch.hide_render = not kids
        ob.location = loc
        ob.rotation_euler = rot
        ob.color = col
