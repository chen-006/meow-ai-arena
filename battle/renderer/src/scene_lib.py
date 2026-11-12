# -*- coding: utf-8 -*-
"""Blender 里用的场景工具：材质、地形导入、石块、草丛、地表点缀、云海、装饰小岛、树、灯光、相机。"""
import math
import random

import bpy
import numpy as np
from mathutils import Vector, Matrix

import geom
from geom import MeshBuilder


def hexc(h, a=1.0):
    h = h.lstrip("#")
    r, g, b = (int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))

    def lin(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    return (lin(r), lin(g), lin(b), a)


# ---------------------------------------------------------------- 调色板（全片统一）
PAL = dict(
    grass="#86C44E", lip="#7CC04A", soil="#B98558",
    rock_a="#F7D29A", rock_b="#EEBB84", rock_c="#E0A47A", rock_deep="#AA8FC8", rock_under="#837EC4",
    haze="#BFDDF8",
    stone_top="#F7D08E", stone_lit="#EDB67C", stone_side="#D6A080", stone_shade="#B08EAC", stone_base="#8E78A6",
    moss="#8CCB54", moss_dark="#6DB84C",
    tall_base="#2F8A4E", tall_mid="#56B45A", tall_tip="#B4E36E",
    cloud="#FFFFFF", cloud_shade="#D3E2FA",
    sky="#A9D6FA", sky_deep="#6DB6F2", ambient="#A3BCF5",
    # 以下各项主题可以换（themes.py）
    ledge_hi="#FFF0CC", grass2="#A6D658", grass_rim="#C4E26F",
    tuft_dark="#4FA348", tuft_light="#B9E36A", plant0="#4FA64E", plant1="#A4DA62",
    flower_main=["#FFFFFF", "#FFF4B8", "#FFD84A"], flower_acc=["#FF9C8A", "#F3B6F2", "#FF7E6B"], flower_core="#F7B733",
    leaf_fall=["#F2C14E", "#F59E42", "#E9D36A"],
    tree_pals=[(0.82, ("#45A862", "#66C46C", "#A4E07E")), (0.13, ("#D9A441", "#EFC45A", "#F9E28E")),
               (0.05, ("#E2796B", "#F29A8A", "#FCC7B8"))],
    pine_pal=("#2F8A66", "#44A878", "#8AD49A"), bush_pal=("#4BA85C", "#8AD468"), trunk="#9B7360",
)


def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


# ---------------------------------------------------------------- 节点小工具
class NB:
    def __init__(self, mat):
        self.mat = mat
        try:
            mat.use_nodes = True
        except Exception:
            pass
        self.nt = mat.node_tree
        self.nt.nodes.clear()
        self.x = 0

    def node(self, typ, **props):
        n = self.nt.nodes.new(typ)
        for k, v in props.items():
            setattr(n, k, v)
        n.location = (self.x, 0)
        self.x += 180
        return n

    @staticmethod
    def inp(n, name, typ=None):
        for s in n.inputs:
            if s.name == name and (typ is None or s.type == typ) and s.enabled:
                return s
        for s in n.inputs:
            if s.name == name:
                return s
        raise KeyError(name)

    @staticmethod
    def out(n, name, typ=None):
        for s in n.outputs:
            if s.name == name and (typ is None or s.type == typ) and s.enabled:
                return s
        for s in n.outputs:
            if s.name == name:
                return s
        raise KeyError(name)

    def link(self, a, b):
        self.nt.links.new(a, b)

    def val(self, v):
        n = self.node("ShaderNodeValue")
        n.outputs[0].default_value = v
        return n.outputs[0]

    def rgb(self, col):
        n = self.node("ShaderNodeRGB")
        n.outputs[0].default_value = col
        return n.outputs[0]

    def math(self, op, a, b=None, clamp=False):
        n = self.node("ShaderNodeMath", operation=op)
        n.use_clamp = clamp
        self._set(n.inputs[0], a)
        if b is not None:
            self._set(n.inputs[1], b)
        return n.outputs[0]

    def _set(self, sock, v):
        if isinstance(v, bpy.types.NodeSocket):
            self.link(v, sock)
        else:
            sock.default_value = v

    def smooth(self, x, a, b):
        """smoothstep(a, b, x)，a>b 时反向。"""
        n = self.node("ShaderNodeMapRange", interpolation_type='SMOOTHSTEP')
        self._set(n.inputs['Value'], x)
        n.inputs['From Min'].default_value = a
        n.inputs['From Max'].default_value = b
        n.inputs['To Min'].default_value = 0.0
        n.inputs['To Max'].default_value = 1.0
        return n.outputs['Result']

    def mix(self, fac, a, b):
        n = self.node("ShaderNodeMix", data_type='RGBA')
        self._set(self.inp(n, 'Factor', 'VALUE'), fac)
        self._set(self.inp(n, 'A', 'RGBA'), a)
        self._set(self.inp(n, 'B', 'RGBA'), b)
        return self.out(n, 'Result', 'RGBA')

    def noise(self, vec, scale, detail=2.0, rough=0.5, w=None):
        n = self.node("ShaderNodeTexNoise")
        if vec is not None:
            self.link(vec, n.inputs['Vector'])
        n.inputs['Scale'].default_value = scale
        n.inputs['Detail'].default_value = detail
        n.inputs['Roughness'].default_value = rough
        return n.outputs['Factor']

    def geo(self):
        return self.node("ShaderNodeNewGeometry")

    def sep(self, vec):
        n = self.node("ShaderNodeSeparateXYZ")
        self.link(vec, n.inputs[0])
        return n

    def attr(self, name):
        n = self.node("ShaderNodeAttribute", attribute_name=name)
        return n

    def principled(self, color, rough=0.9, spec=0.2, emission=None, emis_strength=0.0, alpha=None):
        p = self.node("ShaderNodeBsdfPrincipled")
        self._set(p.inputs['Base Color'], color)
        p.inputs['Roughness'].default_value = rough
        try:
            p.inputs['Specular IOR Level'].default_value = spec
        except KeyError:
            pass
        if emission is not None:
            self._set(p.inputs['Emission Color'], emission)
            p.inputs['Emission Strength'].default_value = emis_strength
        if alpha is not None:
            self._set(p.inputs['Alpha'], alpha)
        return p

    def output(self, shader):
        o = self.node("ShaderNodeOutputMaterial")
        self.link(shader.outputs[0] if hasattr(shader, "outputs") else shader, o.inputs['Surface'])
        return o


def _haze(nb, col, z_sock, near=-1.2, far=-10.0, amount=0.8):
    f = nb.smooth(z_sock, near, far)
    f = nb.math('MULTIPLY', f, amount)
    return nb.mix(f, col, nb.rgb(hexc(PAL['haze'])))


# ---------------------------------------------------------------- 材质
_MATS = {}


def _objcol(nb, col):
    """颜色 × 物体颜色 rgb；物体颜色 alpha < 1 时向白色混（1 - alpha = 闪白量）。物体颜色默认 (1,1,1,1) = 不变。"""
    oi = nb.node("ShaderNodeObjectInfo")
    mul = nb.node("ShaderNodeMix", data_type='RGBA', blend_type='MULTIPLY')
    nb.inp(mul, 'Factor', 'VALUE').default_value = 1.0
    nb.link(col, nb.inp(mul, 'A', 'RGBA'))
    nb.link(nb.out(oi, 'Color'), nb.inp(mul, 'B', 'RGBA'))
    flash = nb.math('SUBTRACT', 1.0, nb.out(oi, 'Alpha'), clamp=True)
    return nb.mix(flash, nb.out(mul, 'Result', 'RGBA'), nb.rgb((1.0, 1.0, 1.0, 1.0)))


def mat_vcol(name, rough=0.85, spec=0.2, emis=0.0, haze=False, backface=True, noise_var=0.0, objcol=False):
    key = ('vcol', name)
    if key in _MATS:
        return _MATS[key]
    m = bpy.data.materials.new(name)
    nb = NB(m)
    a = nb.attr('col')
    col = nb.out(a, 'Color')
    if objcol:
        col = _objcol(nb, col)
    if noise_var > 0:
        g = nb.geo()
        nn = nb.noise(nb.out(g, 'Position'), 3.0, 2.0, 0.5)
        f = nb.math('MULTIPLY', nb.math('SUBTRACT', nn, 0.5), noise_var)
        hsv = nb.node("ShaderNodeHueSaturation")
        nb.link(col, hsv.inputs['Color'])
        nb.link(nb.math('ADD', f, 1.0), hsv.inputs['Value'])
        col = hsv.outputs['Color']
    if haze:
        g2 = nb.geo()
        P = nb.sep(nb.out(g2, 'Position'))
        col = _haze(nb, col, P.outputs['Z'], -1.2, -11.0, 0.85)
    p = nb.principled(col, rough=rough, spec=spec, emission=col if emis > 0 else None, emis_strength=emis)
    nb.output(p)
    m.use_backface_culling = not backface
    _MATS[key] = m
    return m


def mat_toy(name, rough=0.48, spec=0.35, rim=0.35):
    key = ('toy', name)
    if key in _MATS:
        return _MATS[key]
    m = bpy.data.materials.new(name)
    nb = NB(m)
    col = _objcol(nb, nb.out(nb.attr('col'), 'Color'))
    lw = nb.node("ShaderNodeLayerWeight")
    lw.inputs['Blend'].default_value = 0.35
    f = nb.math('POWER', nb.out(lw, 'Facing'), 2.2)
    em = nb.mix(0.5, col, nb.rgb(hexc("#FFFFFF")))
    p = nb.principled(col, rough=rough, spec=spec, emission=em, emis_strength=0.0)
    nb.link(nb.math('MULTIPLY', f, rim), p.inputs['Emission Strength'])
    nb.output(p)
    _MATS[key] = m
    return m


def mat_emit_alpha(name, color_hex=None, strength=1.0, use_vcol=True, alpha=1.0, alpha_from_vcol=True):
    """拖尾、色环用：自发光 + 透明（顶点色 alpha）。"""
    key = ('ea', name)
    if key in _MATS:
        return _MATS[key]
    m = bpy.data.materials.new(name)
    nb = NB(m)
    if use_vcol:
        a = nb.attr('col')
        col = nb.out(a, 'Color')
        al = nb.out(a, 'Alpha') if alpha_from_vcol else nb.val(alpha)
    else:
        col = nb.rgb(hexc(color_hex))
        al = nb.val(alpha)
    oi = nb.node("ShaderNodeObjectInfo")
    mul = nb.node("ShaderNodeMix", data_type='RGBA', blend_type='MULTIPLY')
    nb.inp(mul, 'Factor', 'VALUE').default_value = 1.0
    nb.link(col, nb.inp(mul, 'A', 'RGBA'))
    nb.link(nb.out(oi, 'Color'), nb.inp(mul, 'B', 'RGBA'))
    col = nb.out(mul, 'Result', 'RGBA')
    al = nb.math('MULTIPLY', al, nb.out(oi, 'Alpha'))
    em = nb.node("ShaderNodeEmission")
    nb.link(col, em.inputs['Color'])
    em.inputs['Strength'].default_value = strength
    tr = nb.node("ShaderNodeBsdfTransparent")
    mx = nb.node("ShaderNodeMixShader")
    nb.link(al, mx.inputs['Factor'])
    nb.link(tr.outputs[0], mx.inputs[1])
    nb.link(em.outputs[0], mx.inputs[2])
    o = nb.node("ShaderNodeOutputMaterial")
    nb.link(mx.outputs[0], o.inputs['Surface'])
    try:
        m.surface_render_method = 'BLENDED'
    except Exception:
        m.blend_method = 'BLEND'
    m.use_backface_culling = False
    try:
        m.use_transparent_shadow = True
    except Exception:
        pass
    _MATS[key] = m
    return m


def mat_water():
    """瀑布、喷泉的水：自发光 + 透明，透明度上叠一层往下流的噪声。set_time() 每帧推进。"""
    if 'water' in _MATS:
        return _MATS['water']
    m = bpy.data.materials.new("M_water")
    nb = NB(m)
    a = nb.attr('col')
    g = nb.geo()
    tv = nb.node("ShaderNodeValue")
    tv.name = "TIME"
    cb = nb.node("ShaderNodeCombineXYZ")
    nb.link(tv.outputs[0], cb.inputs['Z'])
    ad = nb.node("ShaderNodeVectorMath", operation='ADD')
    nb.link(nb.out(g, 'Position'), ad.inputs[0])
    nb.link(cb.outputs[0], ad.inputs[1])
    mp = nb.node("ShaderNodeVectorMath", operation='MULTIPLY')
    nb.link(ad.outputs[0], mp.inputs[0])
    mp.inputs[1].default_value = (3.0, 3.0, 0.55)
    nn = nb.noise(mp.outputs[0], 1.0, 1.0, 0.5)
    flow = nb.math('ADD', nb.math('MULTIPLY', nb.smooth(nn, 0.36, 0.64), 0.6), 0.4)
    al = nb.math('MULTIPLY', nb.out(a, 'Alpha'), flow)
    em = nb.node("ShaderNodeEmission")
    nb.link(nb.out(a, 'Color'), em.inputs['Color'])
    em.inputs['Strength'].default_value = 1.0
    tr = nb.node("ShaderNodeBsdfTransparent")
    mx = nb.node("ShaderNodeMixShader")
    nb.link(al, mx.inputs['Factor'])
    nb.link(tr.outputs[0], mx.inputs[1])
    nb.link(em.outputs[0], mx.inputs[2])
    o = nb.node("ShaderNodeOutputMaterial")
    nb.link(mx.outputs[0], o.inputs['Surface'])
    try:
        m.surface_render_method = 'BLENDED'
    except Exception:
        m.blend_method = 'BLEND'
    m.use_backface_culling = False
    _MATS['water'] = m
    return m


def set_time(sec):
    """每帧调用：水往下流。"""
    m = _MATS.get('water')
    if m is not None:
        m.node_tree.nodes["TIME"].outputs[0].default_value = sec * 2.4


def mat_soft_puff(name="M_puff", color="#EFE3D2", alpha=0.45, strength=0.9):
    """灰尘、水雾：边缘渐隐的软团（按视线和法线的夹角淡出），避免像肥皂泡。"""
    key = ('puff', name)
    if key in _MATS:
        return _MATS[key]
    m = bpy.data.materials.new(name)
    nb = NB(m)
    a = nb.attr('col')
    lw = nb.node("ShaderNodeLayerWeight")
    lw.inputs['Blend'].default_value = 0.5
    facing = nb.math('SUBTRACT', 1.0, nb.out(lw, 'Facing'))
    soft = nb.math('POWER', facing, 2.0)
    oi = nb.node("ShaderNodeObjectInfo")
    al = nb.math('MULTIPLY', nb.math('MULTIPLY', soft, nb.out(a, 'Alpha')), nb.out(oi, 'Alpha'))
    em = nb.node("ShaderNodeEmission")
    nb.link(nb.out(a, 'Color'), em.inputs['Color'])
    em.inputs['Strength'].default_value = strength
    tr = nb.node("ShaderNodeBsdfTransparent")
    mx = nb.node("ShaderNodeMixShader")
    nb.link(al, mx.inputs['Factor'])
    nb.link(tr.outputs[0], mx.inputs[1])
    nb.link(em.outputs[0], mx.inputs[2])
    o = nb.node("ShaderNodeOutputMaterial")
    nb.link(mx.outputs[0], o.inputs['Surface'])
    try:
        m.surface_render_method = 'BLENDED'
    except Exception:
        pass
    m.use_backface_culling = True
    _MATS[key] = m
    return m


def mat_shadow_blob():
    key = 'blob'
    if key in _MATS:
        return _MATS[key]
    m = bpy.data.materials.new("M_blob")
    nb = NB(m)
    tc = nb.node("ShaderNodeTexCoord")
    sepn = nb.sep(nb.out(tc, 'Generated'))
    dx = nb.math('SUBTRACT', sepn.outputs['X'], 0.5)
    dy = nb.math('SUBTRACT', sepn.outputs['Y'], 0.5)
    d = nb.math('SQRT', nb.math('ADD', nb.math('MULTIPLY', dx, dx), nb.math('MULTIPLY', dy, dy)))
    a = nb.math('MULTIPLY', nb.smooth(d, 0.5, 0.12), 0.42)
    em = nb.node("ShaderNodeBsdfDiffuse")
    em.inputs['Color'].default_value = hexc("#3B3A5E")
    tr = nb.node("ShaderNodeBsdfTransparent")
    mx = nb.node("ShaderNodeMixShader")
    nb.link(a, mx.inputs['Factor'])
    nb.link(tr.outputs[0], mx.inputs[1])
    nb.link(em.outputs[0], mx.inputs[2])
    o = nb.node("ShaderNodeOutputMaterial")
    nb.link(mx.outputs[0], o.inputs['Surface'])
    try:
        m.surface_render_method = 'BLENDED'
    except Exception:
        pass
    _MATS[key] = m
    return m


def mat_cloud():
    if 'cloud' in _MATS:
        return _MATS['cloud']
    m = bpy.data.materials.new("M_cloud")
    nb = NB(m)
    g = nb.geo()
    P = nb.sep(nb.out(g, 'Position'))
    nz = nb.sep(nb.out(g, 'Normal')).outputs['Z']
    col = nb.mix(nb.smooth(nz, 0.2, -0.6), nb.rgb(hexc(PAL['cloud'])), nb.rgb(hexc(PAL['cloud_shade'])))
    col = _haze(nb, col, P.outputs['Z'], -9.0, -18.0, 0.55)
    p = nb.principled(col, rough=1.0, spec=0.0, emission=nb.rgb(hexc("#E9E6F7")), emis_strength=0.32)
    nb.output(p)
    _MATS['cloud'] = m
    return m


# ---------------------------------------------------------------- 树
def make_round_tree(x, y, z, s, rng, mb_trunk, mb_leaf, palette=("#5DAE73", "#7CC27F", "#A5D88E")):
    tv, tf = geom.lathe([(0.11 * s, 0), (0.09 * s, 0.6 * s), (0.07 * s, 1.0 * s)], seg=8)
    mb_trunk.add(geom.transform(tv, loc=(x, y, z)), tf, vcolors=np.tile(hexc(PAL['trunk']), (len(tv), 1)))
    iv, iff = geom.icosphere(2)
    nblob = rng.integers(4, 7)
    cA, cB, cC = (np.array(hexc(c)) for c in palette)
    for k in range(nblob):
        a = rng.uniform(0, 2 * math.pi)
        rr = rng.uniform(0.15, 0.42) * s
        r = rng.uniform(0.42, 0.62) * s
        cz = z + s * rng.uniform(1.05, 1.5)
        V = geom.transform(iv, scale=(r, r, r * 0.86), loc=(x + math.cos(a) * rr, y + math.sin(a) * rr, cz))
        # 上亮下暗
        cols = []
        for v in V:
            t = (v[2] - (cz - r)) / (2 * r)
            t = min(max(t, 0), 1)
            c = cA + (cB - cA) * min(1, t * 1.4)
            if t > 0.7:
                c = cB + (cC - cB) * ((t - 0.7) / 0.3)
            cols.append(c)
        mb_leaf.add(V, iff, vcolors=np.array(cols))


def make_pine(x, y, z, s, rng, mb_trunk, mb_leaf, palette=("#4A9273", "#5DAA82", "#98D0A2")):
    """松树：4～5 层星形截面的锥，层边下垂，上层小。"""
    tv, tf = geom.lathe([(0.07 * s, 0), (0.05 * s, 0.45 * s)], seg=7)
    mb_trunk.add(geom.transform(tv, loc=(x, y, z)), tf, vcolors=np.tile(hexc("#8A6352"), (len(tv), 1)))
    cA, cB, cC = (np.array(hexc(c)) for c in palette)
    tiers = int(rng.integers(4, 6))
    seg = 14
    for k in range(tiers):
        f = k / max(1, tiers - 1)
        r = (0.5 - 0.3 * f) * s
        h = (0.5 - 0.12 * f) * s
        z0 = z + (0.3 + 0.3 * k) * s
        rot = rng.uniform(0, 1)
        verts = []
        for j in range(seg):
            ph = 2 * math.pi * j / seg + rot
            rr = r * (1.0 if j % 2 == 0 else 0.78)
            verts.append((x + rr * math.cos(ph), y + rr * math.sin(ph), z0 - (0.06 * s if j % 2 == 0 else 0.0)))
        for j in range(seg):
            ph = 2 * math.pi * j / seg + rot
            rr = r * 0.45 * (1.0 if j % 2 == 0 else 0.85)
            verts.append((x + rr * math.cos(ph), y + rr * math.sin(ph), z0 + h * 0.5))
        verts.append((x, y, z0 + h))
        verts.append((x, y, z0 - 0.02 * s))
        F = []
        for j in range(seg):
            j2 = (j + 1) % seg
            F.append((j, j2, seg + j2, seg + j))
            F.append((seg + j, seg + j2, 2 * seg))
            F.append((j2, j, 2 * seg + 1))
        V = np.array(verts)
        cols = []
        for v in V:
            t = (v[2] - (z0 - 0.06 * s)) / (h + 0.06 * s)
            t = min(max(t, 0), 1)
            cols.append(cA + (cB - cA) * min(1, t * 1.6) if t < 0.6 else cB + (cC - cB) * ((t - 0.6) / 0.4))
        mb_leaf.add(V, F, vcolors=np.array(cols))


def make_waterfall(x, y, z_top, width, drop, out_dir=(0.0, -1.0), coll=None, seed=1, lip=0.35):
    """从岛沿流下的瀑布：先在岛面上一小段，越过岛沿后下落、略向外弧，越往下越淡。"""
    rng = np.random.default_rng(seed)
    dx, dy = out_dir
    L = math.hypot(dx, dy)
    dx, dy = dx / L, dy / L
    px, py = -dy, dx  # 宽度方向
    rows = 26
    cols_n = 6
    V = []
    C = []
    for i in range(rows):
        t = i / (rows - 1)
        if t < 0.12:
            # 岛面上的一小段
            along = -lip + (t / 0.12) * lip
            zz = z_top + 0.03
        else:
            tt = (t - 0.12) / 0.88
            along = 0.06 + 0.9 * tt ** 0.6
            zz = z_top - 0.05 - drop * tt ** 1.15
        wdt = width * (1.0 + 0.35 * max(0.0, t - 0.12))
        a = max(0.0, 1.0 - max(0.0, (t - 0.55) / 0.45)) * 0.92
        for j in range(cols_n):
            u = j / (cols_n - 1) - 0.5
            jit = rng.normal(0, 0.02) if 0 < j < cols_n - 1 else 0.0
            V.append((x + dx * along + px * u * wdt, y + dy * along + py * u * wdt + jit, zz))
            c = np.array(hexc("#EAF6FF")) if (j % 2 == 0) else np.array(hexc("#BFE2F5"))
            C.append((c[0], c[1], c[2], a * (0.85 if 0 < j < cols_n - 1 else 0.4)))
    F = []
    for i in range(rows - 1):
        for j in range(cols_n - 1):
            a0 = i * cols_n + j
            F.append((a0, a0 + 1, a0 + 1 + cols_n, a0 + cols_n))
    mb = MeshBuilder()
    mb.add(np.array(V), F, vcolors=np.array(C))
    ob = mb.build("waterfall", [mat_water()], coll)
    # 落点的水雾
    make_clouds(x + dx * 1.2, y + dy * 1.2, 1.0, 1.0, z_top - drop * 0.92, seed=seed + 3, coll=coll, count=2,
                size=(0.7, 1.1))
    return ob


def make_bush(x, y, z, s, rng, mb_leaf, palette=("#5FAE70", "#8FCB7E")):
    iv, iff = geom.icosphere(2)
    cA, cB = (np.array(hexc(c)) for c in palette)
    for k in range(rng.integers(2, 4)):
        a = rng.uniform(0, 2 * math.pi)
        rr = rng.uniform(0.0, 0.18) * s
        r = rng.uniform(0.18, 0.28) * s
        cz = z + r * 0.55
        V = geom.transform(iv, scale=(r, r, r * 0.8), loc=(x + math.cos(a) * rr, y + math.sin(a) * rr, cz))
        cols = [cA + (cB - cA) * min(1, max(0, (v[2] - (cz - r)) / (1.8 * r))) for v in V]
        mb_leaf.add(V, iff, vcolors=np.array(cols))


# ---------------------------------------------------------------- 云海
def make_clouds(cx, cy, w, h, z, seed=3, coll=None, count=34, size=(2.2, 4.5)):
    rng = random.Random(seed)
    mbd = bpy.data.metaballs.new("clouds_%d" % seed)
    mbd.resolution = 0.35
    mbd.render_resolution = 0.22
    mbd.threshold = 0.6
    for i in range(count):
        px = cx + rng.uniform(-w / 2, w / 2)
        py = cy + rng.uniform(-h / 2, h / 2)
        base = rng.uniform(*size)
        nb = rng.randint(4, 8)
        for k in range(nb):
            e = mbd.elements.new()
            e.co = (px + rng.uniform(-1.4, 1.4) * base * 0.6, py + rng.uniform(-1, 1) * base * 0.5,
                    z + rng.uniform(-0.3, 0.6) * base * 0.4)
            e.radius = base * rng.uniform(0.55, 1.0)
            e.stiffness = 2.0
    ob = bpy.data.objects.new("clouds_mb", mbd)
    mbd.materials.append(mat_cloud())
    (coll or bpy.context.scene.collection).objects.link(ob)
    # 元球每帧都要重新求面，转成普通网格
    dg = bpy.context.evaluated_depsgraph_get()
    dg.update()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    me.materials.clear()
    me.materials.append(mat_cloud())
    me.shade_smooth()
    ob2 = bpy.data.objects.new("clouds", me)
    (coll or bpy.context.scene.collection).objects.link(ob2)
    bpy.data.objects.remove(ob)
    bpy.data.metaballs.remove(mbd)
    return ob2


# ---------------------------------------------------------------- 灯光、世界、相机、渲染
def setup_world(color=None, strength=1.0):
    w = bpy.data.worlds.new("W")
    bpy.context.scene.world = w
    try:
        w.use_nodes = True
    except Exception:
        pass
    nt = w.node_tree
    nt.nodes.clear()
    # 照明用的环境光（偏冷）和镜头看到的天色（上浅下深的渐变）分开
    amb = nt.nodes.new("ShaderNodeBackground")
    amb.inputs['Color'].default_value = color or hexc(PAL['ambient'])
    amb.inputs['Strength'].default_value = strength
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sp = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(tc.outputs['Generated'], sp.inputs[0])
    mr = nt.nodes.new("ShaderNodeMapRange")
    mr.interpolation_type = 'SMOOTHSTEP'
    nt.links.new(sp.outputs['Z'], mr.inputs['Value'])
    mr.inputs['From Min'].default_value = -0.35
    mr.inputs['From Max'].default_value = -0.98
    mx = nt.nodes.new("ShaderNodeMix")
    mx.data_type = 'RGBA'
    nt.links.new(mr.outputs['Result'], [s for s in mx.inputs if s.name == 'Factor' and s.type == 'VALUE'][0])
    [s for s in mx.inputs if s.name == 'A' and s.type == 'RGBA'][0].default_value = hexc(PAL['sky'])
    [s for s in mx.inputs if s.name == 'B' and s.type == 'RGBA'][0].default_value = hexc(PAL['sky_deep'])
    cam = nt.nodes.new("ShaderNodeBackground")
    nt.links.new([s for s in mx.outputs if s.name == 'Result' and s.type == 'RGBA'][0], cam.inputs['Color'])
    cam.inputs['Strength'].default_value = 1.0
    lp = nt.nodes.new("ShaderNodeLightPath")
    ms = nt.nodes.new("ShaderNodeMixShader")
    nt.links.new(lp.outputs['Is Camera Ray'], ms.inputs[0])
    nt.links.new(amb.outputs[0], ms.inputs[1])
    nt.links.new(cam.outputs[0], ms.inputs[2])
    out = nt.nodes.new("ShaderNodeOutputWorld")
    nt.links.new(ms.outputs[0], out.inputs['Surface'])
    return w


def add_sun(azimuth_deg=135.0, elevation_deg=52.0, energy=3.2, color=(1.0, 0.95, 0.86), angle_deg=5.0):
    """azimuth：光照来向（从 +X 逆时针，度），135 = 从西北来。"""
    ld = bpy.data.lights.new("sun", 'SUN')
    ld.energy = energy
    ld.color = color
    ld.angle = math.radians(angle_deg)
    ob = bpy.data.objects.new("sun", ld)
    bpy.context.scene.collection.objects.link(ob)
    az = math.radians(azimuth_deg)
    el = math.radians(elevation_deg)
    to_light = Vector((math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)))
    ob.rotation_euler = (-to_light).to_track_quat('-Z', 'Y').to_euler()
    return ob


def add_camera(target, pitch_deg=58.0, yaw_deg=0.0, dist=32.0, lens=50.0, shift_y=0.0, name="cam"):
    cd = bpy.data.cameras.new(name)
    cd.lens = lens
    cd.sensor_width = 36.0
    cd.shift_y = shift_y
    cd.clip_start = 0.5
    cd.clip_end = 400.0
    ob = bpy.data.objects.new(name, cd)
    bpy.context.scene.collection.objects.link(ob)
    p = math.radians(pitch_deg)
    yw = math.radians(yaw_deg)
    back = Vector((math.sin(yw) * -math.cos(p), -math.cos(yw) * math.cos(p), math.sin(p)))
    ob.location = Vector(target) + back * dist
    ob.rotation_euler = (Vector(target) - ob.location).to_track_quat('-Z', 'Y').to_euler()
    bpy.context.scene.camera = ob
    return ob


def setup_render(width=1920, height=1080, samples=64, engine='BLENDER_EEVEE', out=None):
    sc = bpy.context.scene
    sc.render.engine = engine
    sc.render.resolution_x = width
    sc.render.resolution_y = height
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = False
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGB'
    try:
        sc.view_settings.view_transform = 'Standard'
        sc.view_settings.look = 'None'
    except Exception:
        pass
    sc.view_settings.exposure = 0.0
    sc.render.threads_mode = 'FIXED'
    sc.render.threads = 12
    if engine == 'BLENDER_EEVEE':
        ee = sc.eevee
        ee.taa_render_samples = samples
        ee.use_shadows = True
        ee.shadow_ray_count = 2
        ee.shadow_step_count = 8
        try:
            ee.shadow_resolution_scale = 1.0
        except Exception:
            pass
        ee.use_raytracing = True
        ee.ray_tracing_method = 'SCREEN'
        ee.use_fast_gi = True
        try:
            ee.fast_gi_method = 'GLOBAL_ILLUMINATION'
            ee.fast_gi_distance = 2.5
            ee.fast_gi_ray_count = 2
            ee.fast_gi_step_count = 8
        except Exception:
            pass
    else:
        cy = sc.cycles
        cy.device = 'GPU'
        cy.samples = samples
        cy.use_denoising = True
        prefs = bpy.context.preferences.addons['cycles'].preferences
        prefs.compute_device_type = 'OPTIX'
        prefs.get_devices()
        for d in prefs.devices:
            d.use = (d.type == 'OPTIX')
    if out:
        sc.render.filepath = out
