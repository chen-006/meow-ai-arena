# -*- coding: utf-8 -*-
"""8 个原创 Q 版小人（Blender 里程序化建模），拆成可动的部件：身体、头、后发 / 双马尾 / 尾巴、两只手、两只脚、手里的镖、三套表情。
每个部件带一层"反向外壳"描边（深色、背面剔除），全景里小人从草地上跳出来。
局部坐标：脚底中心在原点，面朝 -Y，Z 向上；整体再乘 CHAR_SCALE。

加新角色：在 CHARS 里加一条（主色、发色、发型 style、裙形 skirt、标志物 feats），需要新的标志物就在 _features 里加一段。
"""
import math

import bpy
import numpy as np
from mathutils import Matrix

import geom
from geom import MeshBuilder
from scene_lib import hexc, mat_vcol, mat_toy, mat_emit_alpha, mat_shadow_blob, NB, _MATS, _objcol

SKIN = "#FFD0B4"
EYE = "#2A2130"
CHAR_SCALE = 1.26
OUTLINE_W = 0.021
TEAM_W = 0.034          # 4v4：深色描边外面那圈队伍色的宽度
TEAM_PARTS = ("body", "head", "hairback", "tailL", "tailR", "tail")
OUTLINE_COL = np.array(hexc("#33233F"))

CHARS = {
    "claude": dict(label="Claude", color="#FF8A1F", hair=("#F0701C", "#FFA040"), style="long", h=1.0,
                   hs=1.0, bw=1.0, hairsc=(1.0, 1.18),
                   skirt="bell", dress=("#FBF0DE", "#2E2622", "#FF8F2E"), shoes="#EADCC8",
                   feats=["flower_clip", "back_bow"]),
    "gpt": dict(label="GPT", color="#10C08A", hair=("#2FCFA2", "#A4F2D6"), style="long_wavy", h=0.97,
                hs=1.03, bw=1.08, hairsc=(1.22, 0.95),
                skirt="tier", dress=("#FFFFFF", "#0E9E72", "#C8F3E2"), shoes="#FFFFFF",
                feats=["big_bow"]),
    "gemini": dict(label="Gemini", color="#7B6BFF", hair=("#5568F8", "#F07CEB"), style="long_wavy", h=0.95,
                   hs=1.02, bw=0.94, hairsc=(0.95, 0.74), tailsc=1.3,
                   skirt="short", dress=("#2B2A72", "#F4F1FF", "#8F7CF4"), shoes="#3B3A80",
                   feats=["cat_ears", "cat_tail"]),
    "doubao": dict(label="豆包", color="#FF5A4E", hair=("#553528", "#6B4534"), style="bob", h=1.12,
                   hs=0.88, bw=0.92,
                   skirt="slim", dress=("#26262C", "#74747E", "#26262C"), shoes="#1E1E22",
                   feats=["bean_pin", "jacket", "bean_clip"]),
    "kimi": dict(label="Kimi", color="#35C6F0", hair=("#F6F7FF", "#9CCDFB"), style="long_wavy", h=0.9,
                 hs=1.04, bw=1.0, hairsc=(1.05, 1.3),
                 skirt="bell", dress=("#27306E", "#FFFFFF", "#27306E"), shoes="#27306E",
                 feats=["crescent", "cape"]),
    "deepseek": dict(label="DeepSeek", color="#2F6BFF", hair=("#2152E6", "#34B8EC"), style="long", h=1.02,
                     hs=1.0, bw=1.04, hairsc=(0.92, 0.9), tailsc=1.45,
                     skirt="bell", dress=("#22306A", "#FFFFFF", "#3B6FD0"), shoes="#22306A",
                     feats=["whale_fins", "whale_tail", "maid_band", "apron"]),
    "grok": dict(label="Grok", color="#FFC21A", hair=("#FFCB3D", "#FFE58A"), style="twintails", h=0.88,
                 hs=1.1, bw=1.06, tailsc=1.22,
                 skirt="wide", dress=("#221D26", "#C4203A", "#221D26"), shoes="#221D26",
                 feats=["bat_wings", "crown"]),
    "hunyuan": dict(label="混元", color="#C64FD6", hair=("#221E28", "#F6F4FA"), style="long_split", h=1.14,
                    hs=0.93, bw=0.96, hairsc=(0.96, 1.36),
                    skirt="robe", dress=("#FAF8F5", "#A548CC", "#FAF8F5"), shoes="#221E28",
                    feats=["purple_ribbons", "taiji_pendant", "sleeves"]),
}
ORDER = ["claude", "gpt", "gemini", "doubao", "kimi", "deepseek", "grok", "hunyuan"]
TEAM_COLORS = ("#FF5B4D", "#2E8CFF")  # 4v4：珊瑚红队 / 天蓝队

HEAD_Z = 0.63
HEAD_R = 0.245
NECK_Z = 0.40


def _c(h):
    return np.array(hexc(h))


def ellipsoid(sx, sy, sz, loc=(0, 0, 0), R=None, sub=2):
    V, F = geom.icosphere(sub)
    return geom.transform(V, scale=(sx, sy, sz), loc=loc, R=R), F


def tube(pts, radii, seg=10, flat=1.0):
    """沿折线的一根粗细可变的管子（两头封口）。flat < 1 时把截面沿 Y 压扁。"""
    pts = [np.asarray(p, dtype=float) for p in pts]
    V, F = [], []
    n = len(pts)
    for i, p in enumerate(pts):
        d = pts[min(i + 1, n - 1)] - pts[max(i - 1, 0)]
        d /= np.linalg.norm(d)
        u = np.array([0.0, 1.0, 0.0])
        if abs(np.dot(u, d)) > 0.9:
            u = np.array([1.0, 0.0, 0.0])
        w = np.cross(d, u)
        w /= np.linalg.norm(w)
        u = np.cross(w, d)
        for j in range(seg):
            a = 2 * math.pi * j / seg
            V.append(p + radii[i] * (math.cos(a) * w + math.sin(a) * u * flat))
    for i in range(n - 1):
        for j in range(seg):
            j2 = (j + 1) % seg
            F.append((i * seg + j, i * seg + j2, (i + 1) * seg + j2, (i + 1) * seg + j))
    V.append(pts[0]); V.append(pts[-1])
    for j in range(seg):
        j2 = (j + 1) % seg
        F.append((len(V) - 2, j2, j))
        F.append((len(V) - 1, (n - 1) * seg + j, (n - 1) * seg + j2))
    return np.array(V), F


def basis_from_normal(n, up=(0, 0, 1)):
    n = np.asarray(n, dtype=float)
    n = n / np.linalg.norm(n)
    yax = -n
    xax = np.cross(yax, np.asarray(up, dtype=float))
    if np.linalg.norm(xax) < 1e-6:
        xax = np.array([1.0, 0, 0])
    xax /= np.linalg.norm(xax)
    zax = np.cross(xax, yax)
    return np.stack([xax, yax, zax], axis=1)


def on_head(x, z, r_off=0.0, zc=HEAD_Z, R=HEAD_R):
    dz = z - zc
    yy = -math.sqrt(max(1e-6, R * R - x * x - dz * dz))
    n = np.array([x, yy, dz]) / R
    return np.array([0.0, 0.0, zc]) + n * (R + r_off), n


def gradient_cols(V, c_top, c_bot, z_top, z_bot):
    t = np.clip((V[:, 2] - z_bot) / max(1e-6, z_top - z_bot), 0, 1)[:, None]
    return c_bot[None, :] * (1 - t) + c_top[None, :] * t


# ---------------------------------------------------------------- 材质
M_TOY, M_EYE, M_HAIR, M_OUT = 0, 1, 2, 3


def mat_outline():
    if 'outline' in _MATS:
        return _MATS['outline']
    m = bpy.data.materials.new("M_outline")
    nb = NB(m)
    col = _objcol(nb, nb.out(nb.attr('col'), 'Color'))
    em = nb.node("ShaderNodeEmission")
    nb.link(col, em.inputs['Color'])
    em.inputs['Strength'].default_value = 1.0
    nb.output(em)
    m.use_backface_culling = True
    try:
        m.use_backface_culling_shadow = True
    except Exception:
        pass
    _MATS['outline'] = m
    return m


def char_mats():
    return [mat_toy("M_toy", rough=0.5, spec=0.3, rim=0.2), mat_vcol("M_eye", rough=0.18, spec=0.6, objcol=True),
            mat_toy("M_hair", rough=0.42, spec=0.4, rim=0.3), mat_outline()]


class Part:
    """一个可动部件：攒几何（角色局部坐标），建网格时减去枢轴点，并加一层描边外壳。"""

    def __init__(self, name, pivot=(0, 0, 0)):
        self.name = name
        self.pivot = np.array(pivot, dtype=float)
        self.items = []     # (V, F, mat, cols, outline)

    def add(self, V, F, col=None, mat=M_TOY, cols=None, ol=True):
        V = np.asarray(V, dtype=float)
        if cols is None:
            cols = np.tile(_c(col) if isinstance(col, str) else np.asarray(col), (len(V), 1))
        self.items.append((V, [tuple(int(i) for i in f) for f in F], mat, np.asarray(cols, dtype=float), ol))
        return self

    def build(self, coll, obj_name, width=OUTLINE_W, team=None):
        """team：队伍色（线性 RGBA）。给了就在深色描边外面再包一圈队伍色（4v4 分队用）。"""
        mb = MeshBuilder()
        for V, F, mat, cols, ol in self.items:
            Vl = V - self.pivot
            mb.add(Vl, F, mat, vcolors=cols)
            if ol and width > 0:
                N = np.zeros_like(Vl)
                for f in F:
                    p = Vl[list(f)]
                    n = np.cross(p[1] - p[0], p[2] - p[0])
                    for i in f:
                        N[i] += n
                L = np.linalg.norm(N, axis=1, keepdims=True)
                N = N / np.maximum(L, 1e-9)
                oc = cols * 0.16 + OUTLINE_COL * 0.84
                oc[:, 3] = 1.0
                mb.add(Vl + N * width, [tuple(reversed(f)) for f in F], M_OUT, vcolors=oc)
                if team is not None:
                    mb.add(Vl + N * (width + TEAM_W), [tuple(reversed(f)) for f in F], M_OUT, vcolors=np.tile(team, (len(Vl), 1)))
        ob = mb.build(obj_name, char_mats(), coll)
        ob.location = tuple(self.pivot)
        return ob


# ---------------------------------------------------------------- 三叶回旋镖
def boomerang_geo(color_hex, scale=1.0, blades=3):
    """三片弯刃像风车一样朝同一方向扫，刃口一道浅色描边，中间白色圆毂。躺在局部 XY 平面里。"""
    col = _c(color_hex)
    light = np.clip(col * 0.4 + 0.6, 0, 1)
    deep = np.clip(col * 0.72, 0, 1)
    V_all, F_all, C_all = [], [], []
    segs = 9
    ring = [(1.0, 0.0), (0.6, 1.0), (-0.5, 1.0), (-1.0, 0.0), (-0.5, -1.0), (0.6, -1.0)]

    def pt(a0, t):
        r = 0.05 + 0.25 * t
        ang = a0 + 0.8 * t ** 1.25
        return np.array([r * math.cos(ang), r * math.sin(ang), 0.0])

    for b in range(blades):
        a0 = 2 * math.pi * b / blades
        base_i = len(V_all)
        for i in range(segs + 1):
            t = i / segs
            p = pt(a0, t)
            d = pt(a0, min(1.0, t + 0.01)) - pt(a0, max(0.0, t - 0.01))
            d /= np.linalg.norm(d)
            perp = np.array([-d[1], d[0], 0.0])
            w = 0.068 * (1.0 - 0.3 * t)
            if t > 0.8:
                w *= math.sqrt(max(0.0, 1.0 - ((t - 0.8) / 0.2) ** 2)) * 0.9 + 0.1
            th = 0.03 * (1.0 - 0.4 * t)
            for (pw, pz) in ring:
                V_all.append(p + perp * w * pw + np.array([0, 0, th * pz]))
                c = light if pw > 0.9 else (deep if pz < 0 else col)
                C_all.append(tuple(c[:3]) + (1.0,))
        n = len(ring)
        for i in range(segs):
            for k in range(n):
                k2 = (k + 1) % n
                a = base_i + i * n + k
                F_all.append((a, base_i + i * n + k2, base_i + (i + 1) * n + k2, base_i + (i + 1) * n + k))
        tip_c = base_i + segs * n
        V_all.append(np.mean(np.array(V_all[tip_c:tip_c + n]), axis=0))
        C_all.append(tuple(light[:3]) + (1.0,))
        for k in range(n):
            F_all.append((tip_c + k, tip_c + (k + 1) % n, len(V_all) - 1))
    n0 = len(V_all)
    V, F = geom.icosphere(2)
    V = geom.transform(V, scale=(0.09, 0.09, 0.038))
    for v in V:
        c = _c("#FFFFFF") if math.hypot(v[0], v[1]) > 0.045 or v[2] < 0 else col
        V_all.append(v)
        C_all.append(tuple(c[:3]) + (1.0,))
    for f in F:
        F_all.append(tuple(int(i) + n0 for i in f))
    return np.array(V_all) * scale, F_all, np.array(C_all)


def make_dart(color_hex, coll, name, scale=1.0, width=0.016):
    V, F, C = boomerang_geo(color_hex, scale=scale)
    p = Part(name)
    p.add(V, F, cols=C)
    mb = MeshBuilder()
    Vl, Fl, mat, cols, _ = p.items[0]
    mb.add(Vl, Fl, 0, vcolors=cols)
    N = np.zeros_like(Vl)
    for f in Fl:
        q = Vl[list(f)]
        n = np.cross(q[1] - q[0], q[2] - q[0])
        for i in f:
            N[i] += n
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-9)
    oc = cols * 0.2 + OUTLINE_COL * 0.8
    oc[:, 3] = 1
    mb.add(Vl + N * width, [tuple(reversed(f)) for f in Fl], 1, vcolors=oc)
    return mb.build(name, [mat_vcol("M_dart", rough=0.35, spec=0.5, emis=0.4, objcol=True), mat_outline()], coll)


def make_blur_disc(color_hex, coll, name, r=0.56):
    """旋转残影：一圈很淡的同色圆盘，三道稍亮的扇形。"""
    seg = 60
    col = np.array(hexc(color_hex))
    c2 = np.clip(col[:3] * 0.55 + 0.45, 0, 1)
    Vb, Fb, Cb = [], [], []
    radii = [(0.14, 0.0), (0.5, 0.26), (0.85, 0.32), (1.0, 0.0)]
    for j, (rr, a) in enumerate(radii):
        for k in range(seg):
            ang = 2 * math.pi * k / seg
            Vb.append((r * rr * math.cos(ang), r * rr * math.sin(ang), 0))
            fa = a * (0.45 + 0.55 * max(0.0, math.cos(ang * 3)) ** 2)
            Cb.append(tuple(c2) + (fa,))
    for j in range(len(radii) - 1):
        for k in range(seg):
            k2 = (k + 1) % seg
            Fb.append((j * seg + k, j * seg + k2, (j + 1) * seg + k2, (j + 1) * seg + k))
    mb = MeshBuilder()
    mb.add(np.array(Vb), Fb, vcolors=np.array(Cb), smooth=False)
    return mb.build(name, [mat_emit_alpha("M_fx", strength=1.25)], coll)


# ---------------------------------------------------------------- 头发壳
def hair_shell(style, n_az=96, n_el=18):
    hc = np.array([0, 0.012, HEAD_Z + 0.012])
    Rx, Ry, Rz = HEAD_R + 0.03, HEAD_R + 0.026, HEAD_R + 0.024
    front_w = 1.0 if style == "bob" else 1.12

    def z_bottom(ax):
        if abs(ax) < front_w:
            if style == "twintails":
                return 0.3 - 0.1 * abs(math.cos(ax * 5.0))
            ph = 0.0 if style == "long_split" else 0.5
            return 0.31 - 0.12 * abs(math.cos(ax * 4.2 + ph))
        return -0.62 if style != "bob" else -0.5

    V, rows = [], []
    for i in range(n_az):
        ax = -math.pi + 2 * math.pi * (i + 0.5) / n_az
        elb = math.asin(max(-0.95, min(0.95, z_bottom(ax))))
        col = []
        for j in range(n_el + 1):
            el = math.pi / 2 - (math.pi / 2 - elb) * j / n_el
            col.append(len(V))
            V.append(hc + np.array([Rx * math.cos(el) * math.sin(ax), -Ry * math.cos(el) * math.cos(ax), Rz * math.sin(el)]))
        v_in = hc + 0.93 * np.array([Rx * math.cos(elb) * math.sin(ax), -Ry * math.cos(elb) * math.cos(ax), Rz * math.sin(elb)])
        v_in[2] -= 0.004
        col.append(len(V))
        V.append(v_in)
        rows.append(col)
    F = []
    m = n_el + 2
    for i in range(n_az):
        i2 = (i + 1) % n_az
        for j in range(m - 1):
            F.append((rows[i][j], rows[i2][j], rows[i2][j + 1], rows[i][j + 1]))
    return np.array(V), F


SKIRTS = {
    "bell": [(0.225, 0.10), (0.215, 0.13), (0.175, 0.22), (0.135, 0.29), (0.125, 0.34), (0.1, 0.39), (0.05, 0.415)],
    "tier": [(0.24, 0.10), (0.235, 0.135), (0.19, 0.165), (0.2, 0.19), (0.165, 0.245), (0.135, 0.29), (0.125, 0.34),
             (0.1, 0.39), (0.05, 0.415)],
    "short": [(0.2, 0.165), (0.19, 0.19), (0.155, 0.25), (0.13, 0.29), (0.12, 0.34), (0.1, 0.39), (0.05, 0.415)],
    "slim": [(0.175, 0.13), (0.17, 0.16), (0.15, 0.23), (0.14, 0.29), (0.135, 0.34), (0.11, 0.39), (0.05, 0.415)],
    "wide": [(0.262, 0.09), (0.25, 0.125), (0.2, 0.21), (0.14, 0.29), (0.125, 0.34), (0.1, 0.39), (0.05, 0.415)],
    "robe": [(0.19, 0.07), (0.185, 0.11), (0.165, 0.22), (0.14, 0.29), (0.13, 0.34), (0.105, 0.39), (0.05, 0.415)],
}


def build_parts(key, team_color=None):
    """返回 {部件名: Part}。部件：body head hairback tailL tailR tail handA handB footA footB face_open face_tight face_happy"""
    spec = CHARS[key]
    skin = _c(SKIN)
    hair_top, hair_end = _c(spec["hair"][0]), _c(spec["hair"][1])
    dmain, dacc, dpanel = (_c(c) for c in spec["dress"])
    style, feats = spec["style"], spec["feats"]
    P = dict(body=Part("body"), head=Part("head", (0, 0, NECK_Z)),
             handA=Part("handA", (0.25, -0.02, 0.29)), handB=Part("handB", (-0.25, -0.02, 0.29)),
             footA=Part("footA", (0.085, -0.01, 0.04)), footB=Part("footB", (-0.085, -0.01, 0.04)),
             face_open=Part("face_open", (0, 0, NECK_Z)), face_tight=Part("face_tight", (0, 0, NECK_Z)),
             face_happy=Part("face_happy", (0, 0, NECK_Z)))
    body, head = P["body"], P["head"]

    # ---- 脚 + 一小截腿
    white_leg = key in ("kimi", "deepseek", "gpt", "gemini")
    for nm, sx in (("footA", 1), ("footB", -1)):
        fp = np.array([sx * 0.085, -0.01, 0.04])
        V, F = ellipsoid(0.068, 0.092, 0.052, fp)
        P[nm].add(V, F, spec["shoes"])
        V, F = geom.lathe([(0.036, 0.05), (0.036, 0.25)], seg=10)
        P[nm].add(geom.transform(V, loc=(fp[0], fp[1] + 0.012, 0)), F, "#FFFFFF" if white_leg else skin, ol=False)

    # ---- 身体（裙子）
    V, F = geom.lathe(SKIRTS[spec["skirt"]], seg=28)
    cols = np.tile(dmain, (len(V), 1))
    ang = np.arctan2(V[:, 1], V[:, 0])
    front = np.abs(ang + math.pi / 2) < 0.55
    z = V[:, 2]
    cols[(z > 0.265) & (z < 0.305)] = dacc
    if "apron" in feats:
        cols[front & (z < 0.27)] = _c("#FFFFFF")
    if key == "claude":
        cols[(np.abs(ang + math.pi / 2 - 0.9) < 0.38) & (z < 0.265)] = dpanel
        cols[(np.abs(ang + math.pi / 2 + 2.2) < 0.38) & (z < 0.265)] = dpanel
        cols[z < 0.125] = _c("#2E2622")
    if key == "grok":
        cols[(np.abs(ang + math.pi / 2 + 0.25) < 0.3) & (z < 0.25)] = dacc
        cols[z < 0.12] = dacc
    if key == "hunyuan":
        cols[z < 0.115] = dacc
    if key == "kimi":
        cols[front & (z < 0.265)] = _c("#FFFFFF")
        cols[z < 0.125] = _c("#F4D98A")
    if key == "gemini":
        cols[z < 0.2] = _c("#FFFFFF")
    if key == "gpt":
        cols[z < 0.14] = dacc
        cols[(z > 0.16) & (z < 0.2)] = dpanel
    if key == "doubao":
        cols[front & (z > 0.3) & (np.abs(ang + math.pi / 2) < 0.3)] = dacc
        cols[z < 0.27] = _c("#3A3A42")
        cols[z < 0.155] = _c("#FF5A4E")
    if key == "deepseek":
        cols[z < 0.125] = _c("#FFFFFF")
    body.add(V, F, cols=cols)
    if "cape" in feats:
        V, F = geom.lathe([(0.245, 0.17), (0.21, 0.27), (0.155, 0.355), (0.11, 0.41)], seg=24, close_top=False,
                          close_bottom=False)
        keep = [f for f in F if np.mean([V[i][1] for i in f]) > -0.06]
        body.add(V, keep, "#27306E", ol=False)
        for k in range(6):
            a = 0.45 + k * 0.45
            V2, F2 = ellipsoid(0.014, 0.014, 0.014, (0.215 * math.cos(a), 0.215 * math.sin(a), 0.235 + 0.03 * (k % 2)), sub=1)
            body.add(V2, F2, "#FFE28A", ol=False)
    if "jacket" in feats:
        for sx in (-1, 1):
            V, F = ellipsoid(0.07, 0.07, 0.1, (sx * 0.15, 0.0, 0.33))
            body.add(V, F, "#26262C")
    if "bean_pin" in feats:
        V, F = ellipsoid(0.034, 0.02, 0.026, sub=2)
        body.add(geom.transform(V, rot=(0, 0.5, 0), loc=(-0.07, -0.135, 0.335)), F, "#FF6A3C", ol=False)
    if "taiji_pendant" in feats:
        c = np.array([0.0, -0.17, 0.235])
        V, F = ellipsoid(0.036, 0.012, 0.036, c, sub=2)
        body.add(V, F, cols=np.where((V[:, 0] - c[0])[:, None] > 0, _c("#F4FFF6"), _c("#2D8A64")), ol=False)
    if team_color is not None:      # 队伍围巾
        V, F = geom.lathe([(0.118, 0.372), (0.14, 0.396), (0.125, 0.425)], seg=20)
        body.add(V, F, team_color)
        for k in range(2):
            c = np.array([0.05 - 0.02 * k, 0.17 + 0.07 * k, 0.37 - 0.03 * k])
            V, F = ellipsoid(0.045, 0.1, 0.022, sub=1)
            body.add(geom.transform(V, rot=(-0.35, 0.2, 0.15 * k), loc=c), F, team_color)

    # ---- 手
    for nm, sx in (("handA", 1), ("handB", -1)):
        hp = np.array([sx * 0.25, -0.02, 0.29])
        V, F = ellipsoid(0.05, 0.05, 0.05, hp)
        P[nm].add(V, F, SKIN)
        if "sleeves" in feats:      # 宽袖
            V, F = geom.lathe([(0.075, -0.035), (0.03, 0.09)], seg=12)
            P[nm].add(geom.transform(V, rot=(0, -sx * 0.5, 0), loc=hp + np.array([-sx * 0.02, 0, 0.02])), F, "#FAF8F5")
            V, F = geom.lathe([(0.078, -0.045), (0.074, -0.03)], seg=12)
            P[nm].add(geom.transform(V, rot=(0, -sx * 0.5, 0), loc=hp + np.array([-sx * 0.02, 0, 0.02])), F, "#A548CC", ol=False)

    # ---- 头 + 腮红 + 嘴
    V, F = ellipsoid(HEAD_R, HEAD_R * 0.98, HEAD_R * 0.96, (0, 0, HEAD_Z), sub=3)
    head.add(V, F, SKIN)
    for sx in (-1, 1):
        p, n = on_head(sx * 0.128, HEAD_Z - 0.085, -0.004)
        V, F = ellipsoid(0.032, 0.006, 0.02, sub=1)
        head.add(V @ basis_from_normal(n).T + p, F, "#FF9EA6", ol=False)
    p, n = on_head(0.0, HEAD_Z - 0.105, -0.003)
    V, F = ellipsoid(0.016, 0.006, 0.007, sub=1)
    head.add(V @ basis_from_normal(n).T + p, F, "#7A3A3A", mat=M_EYE, ol=False)
    # ---- 三套眼睛
    for sx in (-1, 1):
        p, n = on_head(sx * 0.082, HEAD_Z - 0.03, -0.008)
        Rm = basis_from_normal(n)
        V, F = ellipsoid(0.035, 0.018, 0.052)
        P["face_open"].add(V @ Rm.T + p, F, EYE, mat=M_EYE, ol=False)
        p2, n2 = on_head(sx * 0.082 + 0.012, HEAD_Z - 0.008, 0.008)
        V, F = ellipsoid(0.011, 0.006, 0.012, sub=1)
        P["face_open"].add(V @ basis_from_normal(n2).T + p2, F, "#FFFFFF", ol=False)
        for sgn in (1, -1):           # > <
            V, F = ellipsoid(0.03, 0.009, 0.0095)
            V = geom.transform(V, rot=(0, -sx * 0.6 * sgn, 0), loc=(0, 0, 0.0135 * sgn))
            P["face_tight"].add(V @ Rm.T + p + Rm @ np.array([0, -0.006, 0]), F, EYE, mat=M_EYE, ol=False)
        for k in (-1, 1):             # ^ ^
            V, F = ellipsoid(0.022, 0.009, 0.0095)
            V = geom.transform(V, rot=(0, k * 0.65, 0), loc=(k * 0.016, 0, 0.0))
            P["face_happy"].add(V @ Rm.T + p + Rm @ np.array([0, -0.006, 0.006]), F, EYE, mat=M_EYE, ol=False)

    # ---- 头发
    def hcols(V, top=None, bot=None):
        if style == "long_split":
            t = np.clip((V[:, 0] + 0.015) / 0.03, 0, 1)[:, None]
            return hair_top[None, :] * (1 - t) + hair_end[None, :] * t
        return gradient_cols(V, hair_top, hair_end, HEAD_Z + 0.26 if top is None else top, 0.2 if bot is None else bot)

    V, F = hair_shell(style)
    head.add(V, F, cols=hcols(V), mat=M_HAIR)
    for sx in (-1, 1):                # 鬓发
        if style == "bob":
            V, F = ellipsoid(0.07, 0.12, 0.13, (sx * 0.205, 0.0, HEAD_Z - 0.07))
        else:
            V, F = ellipsoid(0.055, 0.06, 0.16)
            V = geom.transform(V, rot=(0.15, -sx * 0.12, 0), loc=(sx * 0.215, -0.07, HEAD_Z - 0.13))
        head.add(V, F, cols=hcols(V), mat=M_HAIR)
    if style in ("long", "long_wavy", "long_split"):
        hb = P["hairback"] = Part("hairback", (0, 0.1, HEAD_Z + 0.06))
        V, F = ellipsoid(0.25, 0.16, 0.2, (0.0, 0.11, HEAD_Z - 0.03))
        head.add(V, F, cols=hcols(V), mat=M_HAIR)
        lobes = [(-0.1, 0.14, 0.36, 0.15, 0.12, 0.2), (0.1, 0.14, 0.36, 0.15, 0.12, 0.2), (0.0, 0.16, 0.3, 0.17, 0.11, 0.2)]
        if style == "long_wavy":
            lobes += [(-0.2, 0.1, 0.3, 0.12, 0.11, 0.16), (0.2, 0.1, 0.3, 0.12, 0.11, 0.16),
                      (-0.13, 0.2, 0.25, 0.1, 0.09, 0.12), (0.13, 0.2, 0.25, 0.1, 0.09, 0.12)]
        for (x, y, z_, a, b, c) in lobes:
            V, F = ellipsoid(a, b, c, (x, y, z_))
            hb.add(V, F, cols=hcols(V), mat=M_HAIR)
        for i, x in enumerate((-0.17, -0.085, 0.0, 0.085, 0.17)):
            V, F = ellipsoid(0.06, 0.062, 0.2)
            V[:, 2] = np.where(V[:, 2] < 0, V[:, 2] * 1.25, V[:, 2])
            V = geom.transform(V, rot=(0.12, x * 0.9, 0), loc=(x, 0.18 + 0.02 * abs(x) / 0.17, 0.36 - 0.03 * (i % 2)))
            hb.add(V, F, cols=hcols(V), mat=M_HAIR)
    elif style == "bob":
        V, F = ellipsoid(0.255, 0.175, 0.175, (0.0, 0.08, HEAD_Z - 0.06))
        head.add(V, F, cols=hcols(V), mat=M_HAIR)
    elif style == "twintails":
        V, F = ellipsoid(0.22, 0.14, 0.18, (0.0, 0.1, HEAD_Z - 0.04))
        head.add(V, F, cols=hcols(V), mat=M_HAIR)
        for nm, sx in (("tailL", 1), ("tailR", -1)):
            base = np.array([sx * 0.27, 0.07, HEAD_Z + 0.07])
            tp = P[nm] = Part(nm, tuple(base))
            for (off, r, hh, tilt) in [((0.04, 0.0, -0.07), 0.115, 0.19, 0.3), ((0.1, 0.03, -0.3), 0.1, 0.22, 0.2),
                                       ((0.13, 0.05, -0.5), 0.065, 0.14, 0.08)]:
                V, F = ellipsoid(r, r * 0.92, hh)
                V = geom.transform(V, rot=(0, -sx * tilt, 0), loc=base + np.array([sx * off[0], off[1], off[2]]))
                tp.add(V, F, cols=hcols(V, top=HEAD_Z + 0.2, bot=0.1), mat=M_HAIR)
            for s2 in (-1, 1):      # 发根的黑蝴蝶结
                V, F = ellipsoid(0.055, 0.03, 0.04, base + np.array([0, s2 * 0.04, 0.02]), sub=1)
                tp.add(V, F, "#1E1A20")

    # ---- 标志物（都做得比写实比例大，俯视也认得出）
    if "flower_clip" in feats:
        p, n = on_head(0.16, HEAD_Z + 0.12, 0.04)
        Rm = basis_from_normal(n + np.array([0.5, 0.1, 0.5]))
        for k in range(7):
            a = 2 * math.pi * k / 7
            V, F = ellipsoid(0.045, 0.016, 0.022, (0.05 * math.cos(a), 0, 0.05 * math.sin(a)), sub=1)
            head.add(geom.transform(V, rot=(0, -a, 0)) @ Rm.T + p, F, "#FFB347", ol=False)
        V, F = ellipsoid(0.03, 0.02, 0.03, (0, -0.008, 0), sub=1)
        head.add(V @ Rm.T + p, F, "#8A3F18", ol=False)
    if "back_bow" in feats:
        # 头侧后方一只黑色蝴蝶结（偏在一边，从背后看不会像一对眼睛）
        c = np.array([-0.2, 0.12, HEAD_Z + 0.1])
        for sg in (-1, 1):
            V, F = ellipsoid(0.03, 0.07, 0.05)
            head.add(geom.transform(V, rot=(sg * 0.5, 0.0, -0.5), loc=c + np.array([-0.03, sg * 0.06, 0.01 * sg])), F, "#2E2622")
        V, F = ellipsoid(0.03, 0.028, 0.03, c + np.array([-0.02, 0, 0]), sub=1)
        head.add(V, F, "#2E2622", ol=False)
    if "big_bow" in feats:
        c = np.array([0.0, 0.19, HEAD_Z + 0.14])
        for sx in (-1, 1):
            V, F = ellipsoid(0.135, 0.055, 0.095)
            head.add(geom.transform(V, rot=(0.3, 0, sx * 0.35), loc=c + np.array([sx * 0.125, 0.0, 0.015])), F, "#0E9E72")
        V, F = ellipsoid(0.05, 0.05, 0.05, c, sub=1)
        head.add(V, F, "#0A7A58")
    if "cat_ears" in feats:
        for sx in (-1, 1):
            base = np.array([sx * 0.14, 0.03, HEAD_Z + 0.19])
            V, F = geom.cone(0.095, 0.01, 0.2, seg=4)
            V = geom.transform(V, scale=(1.0, 0.55, 1.0), rot=(0, sx * 0.38, 0.785), loc=base)
            head.add(V, F, cols=hcols(V, top=HEAD_Z + 0.4, bot=HEAD_Z), mat=M_HAIR)
            V, F = geom.cone(0.058, 0.006, 0.13, seg=4)
            V = geom.transform(V, scale=(1.0, 0.4, 1.0), rot=(0, sx * 0.38, 0.785), loc=base + np.array([0, -0.032, 0.014]))
            head.add(V, F, "#FFB0DA", ol=False)
    if "cat_tail" in feats or "whale_tail" in feats:
        tl = P["tail"] = Part("tail", (0, 0.17, 0.2))
        if "cat_tail" in feats:
            pts = [(0, 0.2, 0.2), (0.02, 0.32, 0.17), (0.07, 0.42, 0.22), (0.11, 0.47, 0.34), (0.12, 0.47, 0.45)]
        else:
            pts = [(0, 0.19, 0.17), (0.0, 0.3, 0.14), (0.0, 0.4, 0.16), (0.0, 0.48, 0.24)]
        for i in range(len(pts) - 1):
            a, b = np.array(pts[i]), np.array(pts[i + 1])
            r = 0.048 if "cat_tail" in feats else 0.055 - 0.009 * i
            V, F = ellipsoid(r, r, np.linalg.norm(b - a) * 0.62, sub=1)
            d = (b - a) / np.linalg.norm(b - a)
            Rm = basis_from_normal(np.cross(d, [1, 0, 0]) if abs(d[0]) < 0.9 else np.cross(d, [0, 1, 0]), up=d)
            V = V @ Rm.T + (a + b) / 2
            if "cat_tail" in feats:
                tl.add(V, F, cols=gradient_cols(V, hair_end, hair_top, 0.48, 0.15), mat=M_HAIR)
            else:
                tl.add(V, F, "#2A47A0")
        if "whale_tail" in feats:
            tip = np.array(pts[-1])
            for sx in (-1, 1):
                V, F = ellipsoid(0.12, 0.03, 0.05, sub=1)
                tl.add(geom.transform(V, rot=(0, -sx * 0.5, 0), loc=tip + np.array([sx * 0.1, 0.02, 0.05])), F, "#2A47A0")
    if "whale_fins" in feats:
        for sx in (-1, 1):
            p, n = on_head(sx * 0.2, HEAD_Z + 0.03, 0.0)
            V, F = ellipsoid(0.12, 0.024, 0.055, sub=2)
            head.add(geom.transform(V, rot=(0, -sx * 0.55, sx * 0.35), loc=p + np.array([sx * 0.08, 0.03, -0.01])), F, "#2A47A0")
    if "maid_band" in feats:
        for k in range(9):
            a = math.pi * (0.12 + 0.76 * k / 8)
            V, F = ellipsoid(0.034, 0.028, 0.028, (0.235 * math.cos(a), -0.02 + 0.01 * math.sin(a), HEAD_Z + 0.2 + 0.035 * math.sin(a)), sub=1)
            head.add(V, F, "#FFFFFF", ol=False)
    if "crescent" in feats:           # 头顶一枚立着的新月
        c = np.array([0.02, 0.0, HEAD_Z + 0.36])
        n = 15
        pts, rad = [], []
        for k in range(n):
            a = math.radians(-60 + 250 * k / (n - 1))
            t = k / (n - 1)
            pts.append(c + np.array([0.1 * math.cos(a), 0.0, 0.1 * math.sin(a)]))
            rad.append(0.004 + 0.036 * math.sin(math.pi * t) ** 0.8)
        V, F = tube(pts, rad, flat=0.6)
        head.add(V, F, "#FFDC6E")
    if "bean_clip" in feats:
        p, n = on_head(0.15, HEAD_Z + 0.15, 0.035)
        V, F = ellipsoid(0.055, 0.035, 0.04, sub=2)
        head.add(geom.transform(V, rot=(0, 0.6, 0.3), loc=p), F, "#FF5A4E")
    if "bat_wings" in feats:
        for sx in (-1, 1):
            base = np.array([sx * 0.19, 0.04, HEAD_Z + 0.2])
            for k in range(3):
                V, F = ellipsoid(0.095 - 0.016 * k, 0.016, 0.048, sub=1)
                head.add(geom.transform(V, rot=(0, -sx * (0.45 + 0.35 * k), 0),
                                        loc=base + np.array([sx * (0.07 + 0.05 * k), 0.0, 0.07 - 0.05 * k])), F, "#231C2B")
    if "crown" in feats:
        c = np.array([0.0, -0.02, HEAD_Z + 0.25])
        V, F = geom.lathe([(0.062, 0.0), (0.07, 0.035)], seg=10, close_top=False)
        head.add(geom.transform(V, loc=c), F, "#FFB81C", ol=False)
        for k in range(5):
            a = 2 * math.pi * k / 5
            V, F = geom.cone(0.02, 0.0, 0.05, seg=4)
            head.add(geom.transform(V, loc=c + np.array([0.066 * math.cos(a), 0.066 * math.sin(a), 0.035])), F, "#FFB81C", ol=False)
    if "purple_ribbons" in feats:
        for sx in (-1, 1):
            c = np.array([sx * 0.21, 0.08, HEAD_Z + 0.1])
            V, F = ellipsoid(0.07, 0.03, 0.05, c, sub=1)
            head.add(V, F, "#A548CC")
            V, F = ellipsoid(0.024, 0.016, 0.11, c + np.array([sx * 0.025, 0.01, -0.12]), sub=1)
            head.add(V, F, "#A548CC")
    return P


# ---------------------------------------------------------------- 整只角色
class CharRig:
    """root(空物体) 下面挂各部件。apply(pose) 逐帧摆姿势；pose 的字段见 pose.py。"""

    def __init__(self, key, coll, name, team_color=None, ring_color=None):
        self.key = key
        spec = CHARS[key]
        self.color = spec["color"]
        self.scale = CHAR_SCALE * spec.get("h", 1.0)
        root = self.root = bpy.data.objects.new(name, None)
        root.rotation_mode = 'QUATERNION'
        coll.objects.link(root)
        self.parts = {}
        self.rest = {}
        P = build_parts(key, team_color)
        tcol = _c(team_color) if team_color is not None else None
        for nm, part in P.items():
            ob = part.build(coll, name + "_" + nm, team=tcol if nm in TEAM_PARTS else None)
            self.parts[nm] = ob
            self.rest[nm] = np.array(part.pivot)
        hd = self.parts["head"]
        for nm in ("hairback", "tailL", "tailR", "face_open", "face_tight", "face_happy"):
            if nm in self.parts:
                ob = self.parts[nm]
                ob.parent = hd
                ob.location = tuple(self.rest[nm] - self.rest["head"])
        for nm, ob in self.parts.items():
            if ob.parent is None:
                ob.parent = root
            ob.rotation_mode = 'XYZ'
        if "tail" in self.parts:
            self.parts["tail"].parent = self.parts["body"]
        # 体型差异：头的大小、身体的宽窄、头发的宽和长、双马尾 / 尾巴的大小（都只是整体缩放，动作不用跟着改）
        hs = spec.get("hs", 1.0)
        self.parts["head"].scale = (hs, hs, hs)
        bw = spec.get("bw", 1.0)
        self.parts["body"].scale = (bw, bw, 1.0)
        if "hairback" in self.parts:
            hw_, hl_ = spec.get("hairsc", (1.0, 1.0))
            self.parts["hairback"].scale = (hw_, 1.0 + (hw_ - 1.0) * 0.5, hl_)
        for nm in ("tailL", "tailR", "tail"):
            if nm in self.parts:
                ts = spec.get("tailsc", 1.0)
                self.parts[nm].scale = (ts, ts, ts)
        self.dart = make_dart(self.color, coll, name + "_held", scale=0.66, width=0.012)
        self.dart.parent = root
        # 脚下：柔和的接触影 + 色环（4v4 是队伍色，带一个朝向小尖角）
        self.blob = _blob(coll, name + "_blob")
        self.ring = _ring(coll, name + "_ring", ring_color or self.color, team=team_color is not None)
        self.objs = list(self.parts.values()) + [self.dart]

    def set_visible(self, vis):
        for ob in self.objs + [self.blob, self.ring]:
            if ob.hide_render != (not vis):
                ob.hide_render = not vis

    def apply(self, p):
        if not p.get("visible", True):
            self.set_visible(False)
            return
        self.set_visible(True)
        s = self.scale
        hz = math.radians(90.0 - p["heading"])
        M = (Matrix.Translation((p["x"], -p["y"], p.get("z", 0.0))) @ Matrix.Rotation(hz, 4, 'Z') @
             Matrix.Rotation(p.get("lean", 0.0), 4, 'X') @ Matrix.Rotation(p.get("roll", 0.0), 4, 'Y') @
             Matrix.Rotation(p.get("spin", 0.0), 4, 'Z'))
        if p.get("flip"):
            # 翻跟头：绕身体中心（而不是脚底）转
            c = Matrix.Translation((0, 0, 0.45 * s))
            M = M @ c @ Matrix.Rotation(p["flip"], 4, 'X') @ c.inverted()
        k = p.get("scale", 1.0)
        sx, sy, sz = p.get("sx", 1.0), p.get("sy", p.get("sx", 1.0)), p.get("sz", 1.0)
        M = M @ Matrix.Diagonal((s * k * sx, s * k * sy, s * k * sz, 1.0))
        loc, rot, _ = M.decompose()
        self.root.location = loc
        self.root.rotation_quaternion = rot
        self.root.scale = (s * k * sx, s * k * sy, s * k * sz)
        P = self.parts
        P["body"].location = (0, 0, p.get("body_dz", 0.0))
        P["body"].rotation_euler = (0, 0, p.get("twist", 0.0))
        hp, hy, hr = p.get("head", (0, 0, 0))
        P["head"].location = (0, 0, NECK_Z + p.get("body_dz", 0.0) + p.get("head_dz", 0.0))
        P["head"].rotation_euler = (hp, hr, hy + p.get("twist", 0.0) * 0.5)
        hb, hs = p.get("hair", (0, 0))
        if "hairback" in P:
            P["hairback"].rotation_euler = (-hb, 0, hs)
        for nm, sg in (("tailL", 1), ("tailR", -1)):
            if nm in P:
                P[nm].rotation_euler = (-hb * 1.25, sg * (0.1 + hb * 0.25) + hs * 0.5, hs)
        if "tail" in P:
            tb, ts = p.get("tail", (hb * 0.5, hs))
            P["tail"].rotation_euler = (-tb, 0, ts * 1.5)
        for nm, key_, sg in (("handA", "hand_a", 1), ("handB", "hand_b", -1), ("footA", "foot_a", 1), ("footB", "foot_b", -1)):
            f, sd, u = p[key_]
            P[nm].location = (sd, -f, u)
        P["footA"].rotation_euler = (p.get("foot_a_pitch", 0.0), 0, 0)
        P["footB"].rotation_euler = (p.get("foot_b_pitch", 0.0), 0, 0)
        face = p.get("face", "open")
        for nm in ("open", "tight", "happy"):
            ob = P["face_" + nm]
            hide = (nm != face)
            if ob.hide_render != hide:
                ob.hide_render = hide
        d = p.get("dart")
        if d is None:
            self.dart.hide_render = True
        else:
            self.dart.hide_render = False
            f, sd, u = d["pos"]
            self.dart.location = (sd, -f, u)
            self.dart.rotation_euler = d["rot"]
            self.dart.scale = (d.get("scale", 1.0),) * 3
        tint = p.get("tint", (1, 1, 1))
        col = (tint[0], tint[1], tint[2], 1.0 - p.get("flash", 0.0))
        for ob in self.objs:
            ob.color = col
        # 脚下
        gx, gy = p.get("gx", p["x"]), p.get("gy", p["y"])
        ga = p.get("ground", 1.0)
        if ga <= 0.01:
            self.blob.hide_render = True
            self.ring.hide_render = True
        else:
            hgt = max(0.0, p.get("z", 0.0))
            bs = max(0.3, 1.0 - hgt * 0.35)
            self.blob.location = (gx, -gy, 0.012)
            self.blob.scale = (bs, bs, 1)
            self.blob.color = (1, 1, 1, ga * max(0.25, 1.0 - hgt * 0.4))
            self.ring.location = (gx, -gy, 0.016)
            self.ring.rotation_euler = (0, 0, hz)
            self.ring.color = (1, 1, 1, ga * p.get("ring", 1.0))


def make_rig(key, coll, name, team_color=None, ring_color=None):
    """按 DART_CHARS（voxel 默认 / smooth）建角色：像素小人 VoxRig 或旧的圆润小人 CharRig。
    另一套造型里没有这个人时，退回有的那套（混元只有旧造型；Sol、Fable、Qwen、MiMo、GLM 只有像素造型）。"""
    import cast
    if cast.use_voxel(key):
        import chars_voxel
        return chars_voxel.VoxRig(key, coll, name, team_color=team_color, ring_color=ring_color)
    return CharRig(cast.smooth_key(key), coll, name, team_color=team_color, ring_color=ring_color)


def make_dart_any(color_hex, coll, name, scale=1.0):
    """场上飞的镖：像素造型下用像素镖。"""
    import cast
    if cast.style() == "voxel":
        import chars_voxel
        return chars_voxel.make_dart(color_hex, coll, name, scale=scale, width=0.016)
    return make_dart(color_hex, coll, name, scale=scale)


def rig_color(key):
    import cast
    if cast.use_voxel(key):
        import voxchars
        return voxchars.color(cast.vox_key(key))
    return CHARS[cast.smooth_key(key)]["color"]


def _blob(coll, name, size=1.0):
    me = bpy.data.meshes.new(name)
    s = size / 2
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    key = 'blob2'
    if key not in _MATS:
        m = bpy.data.materials.new("M_blob")
        nb = NB(m)
        tc = nb.node("ShaderNodeTexCoord")
        sp = nb.sep(nb.out(tc, 'Generated'))
        dx = nb.math('SUBTRACT', sp.outputs['X'], 0.5)
        dy = nb.math('SUBTRACT', sp.outputs['Y'], 0.5)
        d = nb.math('SQRT', nb.math('ADD', nb.math('MULTIPLY', dx, dx), nb.math('MULTIPLY', dy, dy)))
        oi = nb.node("ShaderNodeObjectInfo")
        a = nb.math('MULTIPLY', nb.math('MULTIPLY', nb.smooth(d, 0.5, 0.14), 0.5), nb.out(oi, 'Alpha'))
        df = nb.node("ShaderNodeBsdfDiffuse")
        df.inputs['Color'].default_value = hexc("#243A4E")
        tr = nb.node("ShaderNodeBsdfTransparent")
        mx = nb.node("ShaderNodeMixShader")
        nb.link(a, mx.inputs['Factor'])
        nb.link(tr.outputs[0], mx.inputs[1])
        nb.link(df.outputs[0], mx.inputs[2])
        o = nb.node("ShaderNodeOutputMaterial")
        nb.link(mx.outputs[0], o.inputs['Surface'])
        m.surface_render_method = 'BLENDED'
        _MATS[key] = m
    me.materials.append(_MATS[key])
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    return ob


def _ring(coll, name, color_hex, team=False, r_out=0.58, r_in=0.47):
    """脚下的色环：外圈实、内填充很淡；前方一个小尖角指示朝向（局部 -Y）。"""
    seg = 48
    V, F, C = [], [], []
    col = np.array(hexc(color_hex))
    a_ring = 1.0 if team else 0.8
    if team:
        r_out, r_in = 0.62, 0.45
    else:
        r_in = r_out - 0.06
    for k in range(seg):
        a = 2 * math.pi * k / seg
        V.append((r_out * math.cos(a), r_out * math.sin(a), 0))
        V.append((r_in * math.cos(a), r_in * math.sin(a), 0))
        C.append(tuple(col[:3]) + (a_ring,))
        C.append(tuple(col[:3]) + (a_ring,))
    for k in range(seg):
        k2 = (k + 1) % seg
        F.append((2 * k, 2 * k2, 2 * k2 + 1, 2 * k + 1))
    base = len(V)
    V.append((0, 0, -0.001))
    C.append(tuple(col[:3]) + ((0.2 if team else 0.03),))
    for k in range(seg):
        a = 2 * math.pi * k / seg
        V.append((r_in * math.cos(a), r_in * math.sin(a), -0.001))
        C.append(tuple(col[:3]) + ((0.42 if team else 0.1),))
    for k in range(seg):
        F.append((base, base + 1 + k, base + 1 + (k + 1) % seg))
    if team:                            # 外面一道白边：在红地、蓝冰上也分得出来
        b3 = len(V)
        for k in range(seg):
            a = 2 * math.pi * k / seg
            V.append(((r_out + 0.005) * math.cos(a), (r_out + 0.005) * math.sin(a), -0.0005))
            V.append(((r_out + 0.05) * math.cos(a), (r_out + 0.05) * math.sin(a), -0.0005))
            C.append((1.0, 1.0, 1.0, 0.95))
            C.append((1.0, 1.0, 1.0, 0.95))
        for k in range(seg):
            k2 = (k + 1) % seg
            F.append((b3 + 2 * k, b3 + 2 * k2, b3 + 2 * k2 + 1, b3 + 2 * k + 1))
    b2 = len(V)
    V += [(-0.13, -r_out + 0.015, 0), (0.13, -r_out + 0.015, 0), (0, -r_out - 0.17, 0)]
    C += [tuple(col[:3]) + (a_ring,)] * 3
    F.append((b2, b2 + 2, b2 + 1))
    mb = MeshBuilder()
    mb.add(np.array(V), F, vcolors=np.array(C), smooth=False)
    return mb.build(name, [mat_emit_alpha("M_ring", strength=1.5)], coll)
