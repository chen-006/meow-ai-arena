# -*- coding: utf-8 -*-
"""兵种在 Blender 里的装配、上色、摆姿势。

做法（为了 200 个单位也跑得动）：
  * 每个兵种的每个部件只建一份网格（Kit 缓存），所有同兵种的单位、四个队都共用这一份；
  * 队伍色不在网格里：方块记号 T 的顶点存"明暗倍数"，材质里乘物体颜色（ob.color）。所以换队只改 ob.color；
  * 一个单位 = 一个根空物体 + 6～9 个部件物体（父子关系：手里的东西挂在手臂上）。200 个单位约 1500 个物体；
  * 描边是反向外壳（和部件同一个网格、材质 1），深色；
  * 物体颜色的 alpha < 1 = 阵亡（压暗、去饱和）；alpha > 1 = 受击的那一下（1 + 亮度，整个人提亮成暖白、略发光）。

坐标：Blender 里 X = 引擎 x（东），Y = 引擎 z（北），Z = 引擎 y（上）。体素模型面朝 -Y，摆的时候转 (pi - yaw)。
引擎的 yaw：0 = 朝 +z（北），正方向见 scenario.cpp（x 方向 = sin(yaw)）。
"""
import math

import bpy
import numpy as np
from mathutils import Euler, Matrix, Quaternion, Vector

import units as UN
import voxkit as VK
from scene_lib import NB, hexc, _MATS

OUTLINE_COL = np.array(hexc("#241C2C"))
OUTLINE_W = {0.1: 0.022, 0.2: 0.04, 0.15: 0.03, VK.U: 0.012}
EMIT = 0.14
FLASH_MIX, FLASH_COL, FLASH_EMIT = 0.62, "#FFF2D8", 0.9      # 受击闪亮：往暖白混多少、发光多少

# 四个队的颜色：闯关 = 0 我方（蓝）、1 敌方（红）；1v1 = 0、1；混战 = 0～3
TEAM_HEX = ["#2F6BFF", "#F0302C", "#18B45A", "#A64BF0"]
TEAM_NAME = ["蓝", "红", "绿", "紫"]
NEUTRAL = "#E8E2D6"


def team_rgba(team, alive=True):
    if team is None or team < 0:
        c = hexc(NEUTRAL)
    else:
        c = hexc(TEAM_HEX[team % 4])
    return (c[0], c[1], c[2], 1.0 if alive else 0.45)


# ---------------------------------------------------------------- 材质
def _team_color(nb):
    """顶点色 × (种类 == 1 时乘物体颜色)；物体 alpha < 1 时压暗去饱和。返回 (颜色, 种类)。"""
    a = nb.attr('col')
    k = nb.attr('kind')
    col = nb.out(a, 'Color')
    kind = nb.out(k, 'Factor')
    oi = nb.node("ShaderNodeObjectInfo")
    mul = nb.node("ShaderNodeMix", data_type='RGBA', blend_type='MULTIPLY')
    nb.inp(mul, 'Factor', 'VALUE').default_value = 1.0
    nb.link(col, nb.inp(mul, 'A', 'RGBA'))
    nb.link(nb.out(oi, 'Color'), nb.inp(mul, 'B', 'RGBA'))
    is_team = nb.math('COMPARE', kind, 1.0)
    nb.nt.nodes[-1].inputs[2].default_value = 0.1
    c = nb.mix(is_team, col, nb.out(mul, 'Result', 'RGBA'))
    # 阵亡：压暗 + 去饱和
    dead = nb.math('SUBTRACT', 1.0, nb.out(oi, 'Alpha'), clamp=True)
    hsv = nb.node("ShaderNodeHueSaturation")
    nb.link(c, hsv.inputs['Color'])
    nb.link(nb.math('SUBTRACT', 1.0, nb.math('MULTIPLY', dead, 1.2)), hsv.inputs['Saturation'])
    nb.link(nb.math('SUBTRACT', 1.0, nb.math('MULTIPLY', dead, 0.8)), hsv.inputs['Value'])
    # 受击：alpha 超出 1 的部分 = 闪亮的强度
    flash = nb.math('SUBTRACT', nb.out(oi, 'Alpha'), 1.0, clamp=True)
    c2 = nb.mix(nb.math('MULTIPLY', flash, FLASH_MIX), hsv.outputs['Color'], nb.rgb(hexc(FLASH_COL)))
    nb.flash = flash
    return c2, kind


def mat_unit():
    if 'unit' in _MATS:
        return _MATS['unit']
    m = bpy.data.materials.new("M_unit")
    nb = NB(m)
    col, kind = _team_color(nb)
    glow = nb.math('COMPARE', kind, 2.0)
    nb.nt.nodes[-1].inputs[2].default_value = 0.1
    p = nb.principled(col, rough=0.9, spec=0.05, emission=col, emis_strength=EMIT)
    nb.link(nb.math('ADD', nb.math('ADD', EMIT, nb.math('MULTIPLY', glow, 2.2)), nb.math('MULTIPLY', nb.flash, FLASH_EMIT)),
            p.inputs['Emission Strength'])
    nb.output(p)
    _MATS['unit'] = m
    return m


def mat_outline():
    if 'uoutline' in _MATS:
        return _MATS['uoutline']
    m = bpy.data.materials.new("M_unit_outline")
    nb = NB(m)
    col, kind = _team_color(nb)
    c = nb.mix(0.78, col, nb.rgb(tuple(OUTLINE_COL)))
    em = nb.node("ShaderNodeEmission")
    nb.link(c, em.inputs['Color'])
    em.inputs['Strength'].default_value = 1.0
    nb.output(em)
    m.use_backface_culling = True
    try:
        m.use_backface_culling_shadow = True
    except Exception:
        pass
    _MATS['uoutline'] = m
    return m


def mat_ring():
    """脚下的队伍色小圆环：不透明自发光，乘物体颜色。"""
    if 'uring' in _MATS:
        return _MATS['uring']
    m = bpy.data.materials.new("M_unit_ring")
    nb = NB(m)
    oi = nb.node("ShaderNodeObjectInfo")
    em = nb.node("ShaderNodeEmission")
    nb.link(nb.out(oi, 'Color'), em.inputs['Color'])
    em.inputs['Strength'].default_value = 0.9
    nb.output(em)
    _MATS['uring'] = m
    return m


# ---------------------------------------------------------------- 网格
def part_mesh(part, unit, name, outline=True):
    w = OUTLINE_W.get(unit, unit * 0.2) if outline else 0.0
    d = part.mesh(outline_w=w, unit=unit)
    V = [d["V"]]
    F = list(d["F"])
    C = [d["C"]]
    K = [d["K"]]
    M = [0] * len(d["F"])
    if "OV" in d:
        n0 = len(d["V"])
        V.append(d["OV"])
        F += [tuple(i + n0 for i in f) for f in d["OF"]]
        C.append(d["OC"])
        K.append(d["OK"])
        M += [1] * len(d["OF"])
    V = np.concatenate(V)
    C = np.concatenate(C)
    K = np.concatenate(K)
    me = bpy.data.meshes.new(name)
    me.from_pydata(V.tolist(), [], F)
    me.update()
    me.materials.append(mat_unit())
    me.materials.append(mat_outline())
    me.polygons.foreach_set("material_index", M)
    a = me.color_attributes.new("col", 'FLOAT_COLOR', 'POINT')
    a.data.foreach_set("color", C.astype(np.float32).ravel())
    b = me.attributes.new("kind", 'FLOAT', 'POINT')
    b.data.foreach_set("value", K.astype(np.float32))
    return me


def ring_mesh(r_out=0.42, r_in=0.32, seg=24, name="ring"):
    V, F = [], []
    for k in range(seg):
        a = 2 * math.pi * k / seg
        V.append((r_out * math.cos(a), r_out * math.sin(a), 0.0))
        V.append((r_in * math.cos(a), r_in * math.sin(a), 0.0))
    for k in range(seg):
        k2 = (k + 1) % seg
        F.append((2 * k, 2 * k2, 2 * k2 + 1, 2 * k + 1))
    me = bpy.data.meshes.new(name)
    me.from_pydata(V, [], F)
    me.materials.append(mat_ring())
    return me


# 部件的父子关系：手里的东西挂在手臂上，桶挂在身体上，骑手挂在马身上
PARENT = dict(wpnA="armA", wpnB="armB", keg="body", armA="body", armB="body", head=None)
RING = dict(militia=(0.42, 0.33), shield=(0.46, 0.36), spear=(0.42, 0.33), archer=(0.42, 0.33), hammer=(0.48, 0.38),
            bomber=(0.42, 0.33), cavalry=(0.95, 0.8), catapult=(1.9, 1.7), giant=(1.1, 0.92), boss=(2.6, 2.35),
            lord=(0.5, 0.4))


# ---------------------------------------------------------------- 脚上带泥（第 1 关泥塘）
# 带泥的单位把这几个部件换成"糊了泥"的那一份网格（同一份体素，下半截换成泥色）：腿的下半截全是泥（马的白袜子看不见了），
# 马披、裙摆的下沿溅了几块泥点。网格也是每个兵种只建一份，换的时候只换物体用哪份网格。
MUD_COLS = ("#3A2C22", "#4B3A2C", "#5C4836")
MUD_WET = "#2A211B"


def _hh(*a):
    h = 2166136261
    for v in a:
        h = ((h ^ (int(v) & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
    return (h % 10007) / 10007.0


def _mud_part(p, nm, cav=False):
    """一个部件的"带泥"版本（VPart 的拷贝）；不需要糊泥的部件返回 None。"""
    if not p.vox:
        return None
    ks = np.array(list(p.vox))
    zmin, zmax = int(ks[:, 2].min()), int(ks[:, 2].max())
    new = {}
    if nm in ("footA", "footB") or nm.startswith("hleg"):
        h = zmax - zmin + 1
        for (x, y, z), c in p.vox.items():
            zt = zmin + 0.62 * h + (_hh(x, y, 11) - 0.5) * 2.2
            if z < zt:
                new[(x, y, z)] = MUD_WET if z == zmin else MUD_COLS[int(_hh(x, y, z) * 3)]
            elif z < zt + 2 and _hh(x, y, z, 5) < 0.3:
                new[(x, y, z)] = MUD_COLS[2]
    elif nm == "horse" or (nm == "body" and not cav):
        prob = (0.62, 0.42, 0.16)                  # 最下面一层溅得最多，往上越来越少
        for (x, y, z), c in p.vox.items():
            dz = z - zmin
            if dz < 3 and _hh(x, y, z, 7) < prob[dz] * (1.0 if nm == "horse" else 0.85):
                new[(x, y, z)] = MUD_COLS[int(_hh(x, y, z, 3) * 3)]
    if not new:
        return None
    q = VK.VPart(p.name, p.pivot, p.outline)
    q.vox = dict(p.vox)
    q.vox.update(new)
    q.paint = {k: v for k, v in p.paint.items() if k[:3] not in new}
    q.slabs = list(p.slabs)
    return q


MUD_KINDS = ("militia", "shield", "spear", "archer", "cavalry", "hammer", "bomber", "giant", "lord")


class Kit:
    """每个兵种的部件网格只建一次。"""

    def __init__(self):
        self.models = {}
        self.meshes = {}
        self.rings = {}
        self.mud = {}

    def mesh_mud(self, kind, part):
        """kind 的 part 部件"带泥"的网格；这个部件不糊泥时返回 None。"""
        key = (kind, part)
        if key not in self.mud:
            M = self.model(kind)
            q = _mud_part(M.parts[part], part, kind == "cavalry") if kind in MUD_KINDS else None
            self.mud[key] = part_mesh(q, M.unit, "%s_%s_mud" % (kind, part)) if q is not None else None
        return self.mud[key]

    def model(self, kind):
        if kind not in self.models:
            import fx as FXM
            if kind == "lord":
                self.models[kind] = UN.m_lord()
            elif kind.startswith("lord:"):
                self.models[kind] = UN.lord_model(kind[5:])
            elif kind in FXM.BUILD:
                self.models[kind] = FXM.build(kind)
            elif kind in UN.BUILD:
                self.models[kind] = UN.build(kind)
            else:
                self.models[kind] = UN.build_prop(kind)
        return self.models[kind]

    def mesh(self, kind, part):
        key = (kind, part)
        if key not in self.meshes:
            M = self.model(kind)
            p = M.parts[part]
            import fx as FXM
            self.meshes[key] = part_mesh(p, M.unit, "%s_%s" % (kind, part), outline=kind not in FXM.BUILD)
        return self.meshes[key]

    def ring(self, kind):
        k = kind if kind in RING else ("lord" if kind.startswith("lord:") else "militia")
        if k not in self.rings:
            ro, ri = RING[k]
            self.rings[k] = ring_mesh(ro, ri, name="ring_" + k)
        return self.rings[k]


KIT = None


def kit():
    global KIT
    if KIT is None:
        KIT = Kit()
    return KIT


def _parent_of(kind, nm, parts):
    if kind == "cavalry":
        if nm in ("body", "hlegFA", "hlegFB", "hlegBA", "hlegBB"):
            return "horse"
        if nm in ("armA", "armB"):
            return "body"
    if kind == "boss":
        return "body" if nm != "body" else None
    if kind.startswith("lord:") and nm not in ("wpnA", "wpnB", "footA", "footB"):
        if nm in ("hairback", "tailL", "tailR", "face_open", "face_tight", "face_happy"):
            return "head"
        if nm in ("armA", "armB", "tail"):
            return "body"
        return None
    p = PARENT.get(nm)
    if p and p in parts:
        return p
    return None


class Unit:
    """场上的一个单位（或道具）。place() 摆位置朝向，pose() 摆姿势。"""

    def __init__(self, kind, team, coll, name, ring=True, outline=True):
        K = kit()
        M = K.model(kind)
        self.kind = kind
        self.team = team
        self.unit = M.unit
        self.meta = getattr(M, "meta", {})
        self.root = bpy.data.objects.new(name, None)
        self.root.rotation_mode = 'QUATERNION'
        coll.objects.link(self.root)
        col = team_rgba(team)
        self.parts = {}
        self.rest = {}
        for nm, p in M.parts.items():
            if kind.startswith("lord:") and nm in ("face_tight", "face_happy"):
                continue
            ob = bpy.data.objects.new(name + "_" + nm, K.mesh(kind, nm))
            ob.color = col
            coll.objects.link(ob)
            self.parts[nm] = ob
            self.rest[nm] = np.array(p.pivot, dtype=float) * M.unit
        for nm, ob in self.parts.items():
            par = _parent_of(kind, nm, self.parts)
            if par:
                ob.parent = self.parts[par]
                ob.location = tuple(self.rest[nm] - self.rest[par])
            else:
                ob.parent = self.root
                ob.location = tuple(self.rest[nm])
            ob.rotation_mode = 'XYZ'
        self.ring = None
        if ring and team is not None and team >= 0:
            self.ring = bpy.data.objects.new(name + "_ring", K.ring(kind))
            self.ring.color = col
            self.ring.parent = self.root
            self.ring.location = (0, 0, 0.03)
            coll.objects.link(self.ring)
        self.objs = list(self.parts.values()) + ([self.ring] if self.ring else [])
        self.scale = 1.0
        self._dz_last = False
        # 平时不显示的部件（镇山兽头顶的星星、箭的拖尾……），由姿势的 "_show" 点名才显示
        self.optional = set(n for n in self.meta.get("optional", []) if n in self.parts)
        self.shown = set()
        self.vis = True
        for n in self.optional:
            self.parts[n].hide_render = True

    # ---------------------------------------------------------- 位置
    def place(self, x, z, y=0.0, yaw_deg=0.0, scale=1.0):
        """引擎坐标（x 东、z 北、y 上），yaw：0 = 朝北（+z），度。"""
        self.root.location = (x, z, y)
        a = math.pi - math.radians(yaw_deg)
        self.root.rotation_quaternion = Quaternion((0, 0, 1), a)
        if scale != 1.0:
            self.root.scale = (scale, scale, scale)

    def place_q(self, pos, quat):
        """引擎给的刚体位置和朝向（四元数 x, y, z, w，Y 朝上）。"""
        x, y, z = pos
        self.root.location = (x, z, y)
        qx, qy, qz, qw = quat
        q = Quaternion((qw, -qx, -qz, -qy))      # 交换 y、z 两个轴（镜像）后的同一个转动
        self.root.rotation_quaternion = q @ Quaternion((0, 0, 1), math.pi)

    def set_alive(self, alive):
        self._flash = 0.0
        col = team_rgba(self.team, alive)
        for ob in self.objs:
            ob.color = col if ob is not self.ring else (col[0], col[1], col[2], 1.0)
        if self.ring:
            self.ring.hide_render = not alive

    def set_flash(self, f):
        """受击闪亮（0～1）：部件物体颜色的 alpha 写成 1 + f（阵亡的不闪）。"""
        f = round(float(f), 3)
        if f == getattr(self, "_flash", 0.0):
            return
        self._flash = f
        col = team_rgba(self.team, True)
        for ob in self.parts.values():
            ob.color = (col[0], col[1], col[2], 1.0 + f)

    def set_mud(self, on):
        """脚上带泥：腿的下半截、马披 / 裙摆下沿换成糊了泥的网格。"""
        on = bool(on)
        if on == getattr(self, "_mud", False):
            return
        self._mud = on
        K = kit()
        for nm, ob in self.parts.items():
            m = K.mesh_mud(self.kind, nm)
            if m is not None:
                ob.data = m if on else K.mesh(self.kind, nm)

    def set_visible(self, vis):
        vis = bool(vis)
        self.vis = vis
        for ob in self.objs:
            ob.hide_render = not vis
        for n in self.optional:
            self.parts[n].hide_render = not (vis and n in self.shown)

    def show(self, names):
        """点名显示哪些"平时不显示的部件"。"""
        names = set(names) & self.optional
        if names != self.shown:
            self.shown = names
            for n in self.optional:
                self.parts[n].hide_render = not (self.vis and n in names)

    # ---------------------------------------------------------- 姿势
    def pose(self, name="stand", t=0.0, **kw):
        rots = POSES.get(name, pose_stand)(self, t, **kw)
        if self.kind.startswith("lord:"):           # 外来的体素形象：头不挂在身子上，不做身子的前后俯仰
            rots.pop("body", None)
            rots.pop("_dy", None)
        for nm, ob in self.parts.items():
            r = rots.get(nm, (0.0, 0.0, 0.0))
            ob.rotation_euler = (math.radians(r[0]), math.radians(r[1]), math.radians(r[2]))
        if self.optional:
            self.show(rots.get("_show", ()))
        # 整体起伏：_dz 加在"根部件"上（人 = body，骑兵 = horse，镇山兽 = body；它们的子部件跟着走）
        off = rots.get("_dz", 0.0)
        rootp = "horse" if self.kind == "cavalry" else "body"
        if rootp in self.parts and (off or self._dz_last):
            ob = self.parts[rootp]
            ob.location = tuple(self.rest[rootp] + np.array([0, rots.get("_dy", 0.0), off]))
        self._dz_last = bool(off)


# ================================================================ 姿势
# 每个姿势是 (单位, 相位 t, 参数) -> {部件名: (绕 X, 绕 Y, 绕 Z) 度}。绕 X 为负 = 手臂往前抬。
def _base(u):
    r = {}
    k = u.kind
    if k == "hammer":
        r["armB"] = (-40, 0, -8)
        r["wpnB"] = (-25, 0, 0)
    elif k == "bomber":
        r["armA"] = (-172, -8, 0)
        r["armB"] = (-172, 8, 0)
    elif k == "giant":
        r["armB"] = (-30, 0, 0)
        r["wpnB"] = (-30, 0, 0)
    elif k == "shield":
        r["armA"] = (-14, 0, 0)
    elif k == "archer":
        r["armA"] = (-10, 0, 6)
    elif k == "cavalry":
        r["armB"] = (-40, 0, 0)
        r["wpnB"] = (10, 0, 0)
    return r


def pose_stand(u, t, **kw):
    """站着：一点呼吸起伏（t = 秒，各人错开）。"""
    if u.kind == "boss":
        return pose_boss_stand(u, t)
    r = _base(u)
    if u.kind in UN.KINDS or u.kind.startswith("lord"):
        s = math.sin(t * 2 * math.pi * 0.45)
        if "armA" not in r:
            r["armA"] = (2 * s, 0, 4)
        if "armB" not in r:
            r["armB"] = (-2 * s, 0, -4)
        if u.kind not in ("catapult", "cavalry"):
            r["_dz"] = 0.012 * s + 0.0001
    return r


def pose_walk(u, t, speed=1.0, **kw):
    """t = 走过的步相位（0～1 一个周期）。步子大、身子前倾、一步一颠，全景里看得出在走。"""
    s = math.sin(2 * math.pi * t)
    c = math.cos(2 * math.pi * t)
    r = _base(u)
    k = u.kind
    if k == "cavalry":
        a = 30 * speed
        r["hlegFA"] = (a * s, 0, 0)
        r["hlegBB"] = (a * s, 0, 0)
        r["hlegFB"] = (-a * s, 0, 0)
        r["hlegBA"] = (-a * s, 0, 0)
        r["horse"] = (3 * c * speed, 0, 0)
        r["_dz"] = 0.05 * abs(c) + 0.0001
        return r
    if k == "boss":
        return pose_boss_walk(u, t)
    if k == "catapult":
        return r
    a = 38
    r["footA"] = (a * s, 0, 0)
    r["footB"] = (-a * s, 0, 0)
    if "armA" not in r:
        r["armA"] = (-30 * s, 0, 5)
    if "armB" not in r:
        r["armB"] = (30 * s, 0, -5)
    lean = 12 if k == "giant" else 8
    r["body"] = (lean, 4 * s, 3 * s)
    r["_dz"] = 0.075 * abs(c) + 0.0001
    return r


def pose_gallop(u, t, **kw):
    """骑兵冲锋：马前后腿成对地蹬、身子起伏，骑手前倾、马刀前指。"""
    a = 2 * math.pi * t
    r = _base(u)
    f, b = 50 * math.sin(a), 50 * math.sin(a + 2.3)
    hx = 6 * math.sin(a + 0.6)
    r["horse"] = (hx, 0, 0)
    r["hlegFA"] = (f - hx, 0, 0)
    r["hlegFB"] = (f * 0.8 + 8 - hx, 0, 0)
    r["hlegBA"] = (b - hx, 0, 0)
    r["hlegBB"] = (b * 0.8 - 8 - hx, 0, 0)
    r["body"] = (16, 0, 0)
    r["armB"] = (-95, 0, -10)
    r["wpnB"] = (60, 0, 0)
    r["_dz"] = 0.06 + 0.14 * abs(math.sin(a)) + 0.0001
    return r


def pose_getup(u, t, **kw):
    """正在起身：蜷着腿、手撑地。"""
    r = _base(u)
    if u.kind in ("cavalry", "catapult", "boss"):
        return r
    r["body"] = (30, 0, 0)
    r["armA"] = (-50, 0, 12)
    r["armB"] = (-50, 0, -12)
    r["footA"] = (-40, 0, 0)
    r["footB"] = (25, 0, 0)
    r["_dz"] = -0.12
    return r


def pose_attack(u, t, **kw):
    """t = 出手一轮里的相位：0～0.45 起手（蓄力，身子后仰、拧过去），0.45～0.6 出手（整个人扑出去），之后收势。
    幅度有意夸张：全景里一个兵只有几十个像素高，要靠整个身子的前后摆动看出"他砍了一下"。"""
    r = _base(u)
    k = u.kind

    def ease(a, b, x):
        x = min(max(x, 0.0), 1.0)
        x = x * x * (3 - 2 * x)
        return a + (b - a) * x
    if t < 0.45:
        f = t / 0.45
        ph = 0
    elif t < 0.6:
        f = (t - 0.45) / 0.15
        ph = 1
    else:
        f = (t - 0.6) / 0.4
        ph = 2

    def seq(a, b, c, d=None):
        """起手 a→b，出手 b→c，收势 c→d（d 默认回到 a）。"""
        d = a if d is None else d
        return [ease(a, b, f), ease(b, c, f), ease(c, d, f)][ph]

    if k in ("militia", "shield", "lord") or k.startswith("lord:"):
        r["armB"] = (seq(0, -205, -25), 0, -12)
        r["body"] = (seq(0, -12, 26), 0, seq(0, 24, -22))
        r["footA"] = (seq(0, 10, -34), 0, 0)
        r["footB"] = (seq(0, -8, 26), 0, 0)
        r["_dz"] = seq(0, 0.05, -0.05) + 0.0001
        r["_dy"] = seq(0, 0.06, -0.16)
        if k == "shield":
            r["armA"] = (seq(-14, -30, -55), 0, 0)
        elif not k.startswith("lord:"):
            r["armA"] = (seq(0, 30, -30), 0, 8)
    elif k == "spear":
        # 往后一收，整个人往前一个弓步捅出去；枪始终放平（手臂转多少，枪反着转回来）
        a = seq(-30, 28, -84, -30)
        r["armB"] = (a, 0, 0)
        r["wpnB"] = (90 - a, 0, 0)
        r["armA"] = (seq(-40, -20, -70, -40), 0, -10)
        r["body"] = (seq(4, -8, 28, 4), 0, seq(0, 26, -18))
        r["footA"] = (seq(0, 12, -40), 0, 0)
        r["footB"] = (seq(0, -6, 30), 0, 0)
        r["_dz"] = seq(0, 0.02, -0.07) + 0.0001
        r["_dy"] = seq(0, 0.10, -0.28)
    elif k == "hammer":
        a = seq(-40, -185, -35)
        r["armB"] = (a, 0, -8)
        r["wpnB"] = (seq(-25, -45, 75, -25), 0, 0)
        r["armA"] = (a * 0.85, 0, 10)
        r["body"] = (seq(0, -20, 38), 0, 0)
        r["footA"] = (seq(0, 8, -30), 0, 0)
        r["footB"] = (seq(0, -6, 22), 0, 0)
        r["_dz"] = seq(0, 0.09, -0.12) + 0.0001
        r["_dy"] = seq(0, 0.05, -0.14)
    elif k == "archer":
        # 侧身开弓：弓臂前伸，另一只手拉到耳后；撒放时手往后弹
        r["armA"] = (-88, 0, 0)
        r["wpnA"] = (88, 0, 0)
        d = seq(-20, -92, -60, -20)
        r["armB"] = (d, 0, seq(0, -42, -70, 0))
        r["body"] = (seq(0, -8, 4), 0, seq(0, 30, 22))
        r["footB"] = (seq(0, 22, 22), 0, 0)
        r["_dz"] = seq(0, -0.02, 0.03) + 0.0001
    elif k == "giant":
        # 横扫：整个上身从右拧到左，棒子抡一大圈
        r["armB"] = (seq(-30, -85, -85), 0, seq(0, 80, -95))
        r["wpnB"] = (seq(-30, -40, 25), 0, 0)
        r["body"] = (seq(0, -6, 16), 0, seq(0, 46, -52))
        r["armA"] = (seq(0, -30, 30), 0, 14)
        r["footA"] = (seq(0, 10, -24), 0, 0)
        r["_dz"] = seq(0, 0.05, -0.08) + 0.0001
    elif k == "cavalry":
        r["armB"] = (seq(-40, -190, -30), 0, -14)
        r["wpnB"] = (seq(10, 0, 50), 0, 0)
        r["body"] = (seq(0, -10, 22), 0, seq(0, 18, -18))
        r["horse"] = (seq(0, -5, 4), 0, 0)
    elif k == "catapult":
        a = [0, ease(0, -105, f), ease(-105, -100, f)][ph]
        r["arm"] = (a, 0, 0)
    elif k == "boss":
        # 甩头顶人：先低头蓄力，再猛地往上挑
        h = [ease(8, 34, f), ease(34, -34, f), ease(-34, 8, f)][ph]
        bx = [ease(0, 6, f), ease(6, -5, f), ease(-5, 0, f)][ph]
        r["head"] = (h, 0, 0)
        r["body"] = (bx, 0, 0)
        r["tail"] = (-30, 0, 0)
        _boss_legs(r, bx)
    return r


def pose_brace(u, t, **kw):
    """长枪兵架枪：手往前送、枪放平、半蹲。"""
    r = _base(u)
    r["armB"] = (-55, 0, 6)
    r["wpnB"] = (145, 0, 0)
    r["armA"] = (-60, 0, -8)
    r["footA"] = (-25, 0, 0)
    r["footB"] = (20, 0, 0)
    r["_dz"] = -0.06
    return r


def pose_knock(u, t, **kw):
    """被打飞 / 倒地：整个人的转动跟引擎的刚体走，这里只把手脚甩开。"""
    s = math.sin(t * 9.0)
    r = _base(u)
    if u.kind in ("cavalry",):
        r["hlegFA"] = (40, 0, 0)
        r["hlegFB"] = (-30, 0, 0)
        r["hlegBA"] = (25, 0, 0)
        r["hlegBB"] = (-35, 0, 0)
        r["armA"] = (-150, 0, 30)
        r["armB"] = (-150, 0, -30)
        return r
    r["armA"] = (-120 + 25 * s, -30, 0)
    r["armB"] = (-120 - 25 * s, 30, 0)
    r["footA"] = (30 * s, 0, 8)
    r["footB"] = (-30 * s, 0, -8)
    return r


def pose_dead(u, t, **kw):
    r = _base(u)
    if u.kind == "cavalry":
        r["hlegFA"] = (35, 0, 0)
        r["hlegFB"] = (20, 0, 0)
        r["hlegBA"] = (-25, 0, 0)
        r["hlegBB"] = (-40, 0, 0)
        return r
    r["armA"] = (-160, -20, 0)
    r["armB"] = (-150, 25, 0)
    r["footA"] = (10, 0, 10)
    r["footB"] = (-6, 0, -12)
    return r


def _boss_legs(r, body_x, fa=0.0, fb=0.0, ba=0.0, bb=0.0):
    """四条腿挂在身子上：身子前后俯仰时腿反着转回来（脚还踩在地上），再加各自的摆动。"""
    r["legFA"] = (fa - body_x, 0, 0)
    r["legFB"] = (fb - body_x, 0, 0)
    r["legBA"] = (ba - body_x, 0, 0)
    r["legBB"] = (bb - body_x, 0, 0)


def pose_boss_stand(u, t, **kw):
    s = math.sin(t * 2 * math.pi * 0.45)
    r = {"body": (0, 0, 0), "head": (6 + 3 * s, 0, 0), "tail": (-8, 0, 14 * math.sin(t * 2 * math.pi * 0.7))}
    _boss_legs(r, 0)
    return r


def pose_boss_walk(u, t, **kw):
    s, c = math.sin(2 * math.pi * t), math.cos(2 * math.pi * t)
    r = {"body": (1.5 * c, 2.5 * s, 0), "head": (8 + 4 * c, 0, -5 * s), "tail": (-10, 0, 18 * s),
         "_dz": 0.06 * abs(c)}
    a = 24
    _boss_legs(r, 1.5 * c, a * s, -a * s, -a * s, a * s)
    return r


def pose_paw(u, t, **kw):
    """镇山兽刨地：身子前压、头压得很低左右甩，一条前腿大幅度往后刨，尾巴竖起来抽。"""
    ph = (t * 2.6) % 1.0
    # 前腿：慢慢抬到前面（0～0.6），猛地往后刨（0.6～1）
    leg = -48 + 20 * (ph / 0.6) if ph < 0.6 else -28 + 78 * ((ph - 0.6) / 0.4)
    if ph >= 0.6:
        leg = -28 + 78 * math.sin((ph - 0.6) / 0.4 * math.pi)
    bx = 7 + 2.5 * math.sin(ph * 2 * math.pi)
    r = {"body": (bx, 0, 0), "head": (26 + 6 * math.sin(t * 9), 0, 9 * math.sin(t * 13)),
         "tail": (-55 + 14 * math.sin(t * 17), 0, 22 * math.sin(t * 11)), "_dz": -0.05}
    _boss_legs(r, bx, fa=leg, fb=6, ba=-8, bb=-8)
    return r


def pose_charge(u, t, **kw):
    """镇山兽冲撞：低头把撞额顶在最前面，前后腿成对地蹬（t = 步相位），身子前后起伏，尾巴甩直。"""
    a = 2 * math.pi * t
    bx = 6 + 6 * math.sin(a)
    r = {"body": (bx, 0, 0), "head": (20 - 6 * math.sin(a), 0, 0), "tail": (-70 + 8 * math.sin(a * 2), 0, 0),
         "_dz": 0.10 + 0.22 * abs(math.sin(a))}
    f, b = 46 * math.sin(a), 46 * math.sin(a + 2.2)
    _boss_legs(r, bx, fa=f, fb=f * 0.85 + 6, ba=b, bb=b * 0.85 - 6)
    return r


def pose_stun(u, t, **kw):
    """撞晕：身子左右晃，头耷拉着画圈，前腿叉开，头顶三颗星星转。"""
    s = math.sin(t * 2 * math.pi * 0.8)
    c = math.cos(t * 2 * math.pi * 0.8)
    r = {"body": (3 + 2 * c, 7 * s, 0), "head": (30 + 6 * c, 0, 16 * s), "tail": (24, 0, 10 * s),
         "stars": (0, 0, (t * 260.0) % 360.0), "_show": ("stars",), "_dz": -0.04}
    r["legFA"] = (-3, -14, 0)
    r["legFB"] = (-3, 14, 0)
    r["legBA"] = (-3, -8, 0)
    r["legBB"] = (-3, 8, 0)
    return r


POSES = dict(stand=pose_stand, walk=pose_walk, attack=pose_attack, brace=pose_brace, knock=pose_knock, dead=pose_dead,
             paw=pose_paw, stun=pose_stun, charge=pose_charge, gallop=pose_gallop, getup=pose_getup)
