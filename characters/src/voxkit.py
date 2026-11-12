# -*- coding: utf-8 -*-
"""体素小人的公共积木（纯 Python + numpy，不依赖 Blender，也不依赖本集的任何东西）。

一个角色 = 一个 Model：若干可动部件（VPart），每个部件是一堆整数格子上的方块，外加少量"贴片"（slab，
不占格子的薄片，用来画眼睛、裙边这类比一格更细的东西）和"面漆"（paint，只改某个方块某一面的颜色）。

坐标（单位 = 一个方块的边长）：脚底中心在原点，面朝 -Y，Z 向上，+X 是角色的左手边（镜头正对角色时在画面右侧）。
标准身材：头 8 宽 × 7 深 × 7 高（x -4..4，y -3..4），脖子在 z = hz；身体中心在 y = 0.5。

部件名（装配和动作都认这些名字）：
  body 身体（裙子、披风）   head 头（含头顶饰物）   hairback 脑后长发（会甩）
  tailL / tailR 双马尾、侧辫（挂在头上，会甩）   tail 尾巴（挂在身体上）
  armA / armB 左右手臂（A 是拿东西的那只，+X 侧）   footA / footB 两条腿
  face_open / face_tight / face_happy 三套表情（薄片，贴在脸上，同一时刻只显示一套）
"""
import numpy as np

U = 0.07            # 一个方块的边长（角色局部单位；整体还要乘渲染器的 CHAR_SCALE）

# 六个方向：+X -X +Y -Y(正面) +Z(顶) -Z
DIRS = ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1))
PX, NX, PY, NY, PZ, NZ = range(6)
FRONT, BACK, TOP = NY, PY, PZ
_QUAD = (
    ((1, 0, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1)),
    ((0, 0, 0), (0, 0, 1), (0, 1, 1), (0, 1, 0)),
    ((0, 1, 0), (0, 1, 1), (1, 1, 1), (1, 1, 0)),
    ((0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1)),
    ((0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)),
    ((0, 0, 0), (0, 1, 0), (1, 1, 0), (1, 0, 0)),
)

SKIN = "#FFDCC2"
BLUSH = "#FF9FA8"
EYE = "#3B2B3D"


def hex_lin(h):
    h = h.lstrip("#")
    out = []
    for i in (0, 2, 4):
        c = int(h[i:i + 2], 16) / 255.0
        out.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
    return (out[0], out[1], out[2], 1.0)


class VPart:
    def __init__(self, name, pivot=(0, 0, 0), outline=True):
        self.name = name
        self.pivot = tuple(float(v) for v in pivot)
        self.vox = {}           # (x, y, z) -> "#RRGGBB"
        self.paint = {}         # (x, y, z, 方向) -> "#RRGGBB"
        self.slabs = []         # (x0, x1, y0, y1, z0, z1, 颜色)，可以是小数
        self.outline = outline

    # ---- 方块
    def box(self, x0, x1, y0, y1, z0, z1, c):
        for x in range(x0, x1):
            for y in range(y0, y1):
                for z in range(z0, z1):
                    self.vox[(x, y, z)] = c
        return self

    def boxm(self, x0, x1, y0, y1, z0, z1, c):
        """左右对称地各放一块（x0..x1 写 +X 那一侧）。"""
        self.box(x0, x1, y0, y1, z0, z1, c)
        return self.box(-x1, -x0, y0, y1, z0, z1, c)

    def put(self, x, y, z, c):
        self.vox[(x, y, z)] = c
        return self

    def putm(self, x, y, z, c):
        self.vox[(x, y, z)] = c
        self.vox[(-x - 1, y, z)] = c
        return self

    def cut(self, x0, x1, y0, y1, z0, z1):
        for x in range(x0, x1):
            for y in range(y0, y1):
                for z in range(z0, z1):
                    self.vox.pop((x, y, z), None)
        return self

    def chamfer(self, x0, x1, y0, y1, z0, z1):
        """把一个盒子的四条竖棱削掉一格（裙摆、披风看着圆一点）。"""
        for x in (x0, x1 - 1):
            for y in (y0, y1 - 1):
                self.cut(x, x + 1, y, y + 1, z0, z1)
        return self

    def tint(self, x0, x1, y0, y1, z0, z1, c, only=None):
        """把范围内已有的方块换色（only：只换原来是这个颜色的）。"""
        for x in range(x0, x1):
            for y in range(y0, y1):
                for z in range(z0, z1):
                    if (x, y, z) in self.vox and (only is None or self.vox[(x, y, z)] == only):
                        self.vox[(x, y, z)] = c
        return self

    # ---- 面漆、贴片
    def face(self, x, y, z, d, c):
        self.paint[(x, y, z, d)] = c
        return self

    def faces(self, x0, x1, y0, y1, z0, z1, d, c):
        for x in range(x0, x1):
            for y in range(y0, y1):
                for z in range(z0, z1):
                    if (x, y, z) in self.vox:
                        self.paint[(x, y, z, d)] = c
        return self

    def slab(self, x0, x1, y0, y1, z0, z1, c):
        self.slabs.append((float(x0), float(x1), float(y0), float(y1), float(z0), float(z1), c))
        return self

    def bounds(self):
        ks = list(self.vox)
        if not ks:
            return None
        a = np.array(ks)
        return a.min(axis=0), a.max(axis=0) + 1

    # ---- 网格
    def mesh(self, outline_w=0.0, unit=U):
        """返回 dict(V, F, C[, OV, OF, OC])：顶点（已减去枢轴、乘了 unit）、四边面、每顶点线性 RGBA；
        outline_w > 0 时另给一层向外推开的反向外壳（描边）。"""
        V, F, C = [], [], []
        quads = []
        vox = self.vox
        lin = {}

        def col(h):
            if h not in lin:
                lin[h] = hex_lin(h)
            return lin[h]

        for (x, y, z), c in vox.items():
            for d, (dx, dy, dz) in enumerate(DIRS):
                if (x + dx, y + dy, z + dz) in vox:
                    continue
                cc = col(self.paint.get((x, y, z, d), c))
                base = len(V)
                corners = [(x + a, y + b, z + e) for (a, b, e) in _QUAD[d]]
                V.extend(corners)
                C.extend([cc] * 4)
                F.append((base, base + 1, base + 2, base + 3))
                quads.append((corners, d, cc))
        for (x0, x1, y0, y1, z0, z1, c) in self.slabs:
            cc = col(c)
            for d in range(6):
                base = len(V)
                for (a, b, e) in _QUAD[d]:
                    V.append((x1 if a else x0, y1 if b else y0, z1 if e else z0))
                C.extend([cc] * 4)
                F.append((base, base + 1, base + 2, base + 3))
        piv = np.array(self.pivot)
        out = dict(V=(np.array(V, dtype=float).reshape(-1, 3) - piv) * unit, F=F,
                   C=np.array(C, dtype=float).reshape(-1, 4))
        if outline_w > 0 and self.outline and quads:
            nsum = {}
            for corners, d, _ in quads:
                for p in corners:
                    nsum.setdefault(p, set()).add(d)
            OV, OF, OC = [], [], []
            for corners, d, cc in quads:
                base = len(OV)
                for p in corners:
                    off = np.zeros(3)
                    for dd in nsum[p]:
                        off += DIRS[dd]
                    OV.append((np.array(p, dtype=float) - piv) * unit + np.clip(off, -1, 1) * outline_w)
                    OC.append(cc)
                OF.append((base + 3, base + 2, base + 1, base))
            out.update(OV=np.array(OV), OF=OF, OC=np.array(OC))
        return out


class Model:
    """一个角色的全部部件 + 装配要用的几个数。"""

    def __init__(self, key, hz=6, skin=SKIN):
        self.key = key
        self.hz = hz                    # 脖子高度（头的枢轴）
        self.skin = skin
        self.parts = {}
        self.scale = 1.0
        self.arm_len = 3.0              # 手臂长（方块数），算手里的东西放哪用
        self.hair_swing = 1.0           # 脑后长发跟随的幅度
        self.tail_rest = 0.0            # 尾巴静止时往上翘多少（弧度）
        self.tail_swing = 1.0
        self.pal = {}

    def part(self, name, pivot=(0, 0, 0), outline=True):
        p = self.parts.get(name)
        if p is None:
            p = self.parts[name] = VPart(name, pivot, outline)
        return p

    def to_json(self):
        """导出成不依赖任何代码的数据（给别的工具、别的集复用）。"""
        out = dict(key=self.key, unit=U, hz=self.hz, scale=self.scale, arm_len=self.arm_len,
                   hair_swing=self.hair_swing, tail_rest=self.tail_rest, palette=self.pal, parts={})
        for nm, p in self.parts.items():
            out["parts"][nm] = dict(
                pivot=list(p.pivot), outline=p.outline,
                voxels=[[x, y, z, c] for (x, y, z), c in sorted(p.vox.items())],
                paint=[[x, y, z, d, c] for (x, y, z, d), c in sorted(p.paint.items())],
                slabs=[list(s) for s in p.slabs])
        return out


def from_json(data):
    m = Model(data["key"], hz=data["hz"])
    m.scale = data.get("scale", 1.0)
    m.arm_len = data.get("arm_len", 3.0)
    m.hair_swing = data.get("hair_swing", 1.0)
    m.tail_rest = data.get("tail_rest", 0.0)
    m.pal = data.get("palette", {})
    for nm, pd in data["parts"].items():
        p = m.part(nm, pd["pivot"], pd.get("outline", True))
        for x, y, z, c in pd["voxels"]:
            p.vox[(x, y, z)] = c
        for x, y, z, d, c in pd["paint"]:
            p.paint[(x, y, z, d)] = c
        p.slabs = [tuple(s) for s in pd["slabs"]]
    return m


# ================================================================ 标准积木
def head(M, hair, hair_hi=None, bangs=(), bangs2=(), h=7, side=True, shine=True):
    """标准头：8 × 7 × h，四条竖棱和顶面一圈削掉一格。正面 x -3..3、z hz..hz+4 是脸。
    bangs：刘海在 hz+3 这一排盖住哪几列（x）；bangs2：再往下一排（hz+2）盖住哪几列（脸两侧的长鬓）。
    side=False：两侧不留鬓角（耳朵位置露皮肤）。"""
    hz = M.hz
    P = M.part("head", (0, 0.5, hz))
    P.box(-4, 4, -3, 4, hz, hz + h, hair)
    for x in (-4, 3):
        for y in (-3, 3):
            P.cut(x, x + 1, y, y + 1, hz, hz + h)
    P.cut(-4, 4, -3, 4, hz + h - 1, hz + h)
    P.box(-3, 3, -2, 3, hz + h - 1, hz + h, hair)
    P.cut(-4, -3, -3, 4, hz, hz + 1)
    P.cut(3, 4, -3, 4, hz, hz + 1)
    P.cut(-4, 4, 3, 4, hz, hz + 1)
    # 脸
    P.box(-3, 3, -3, -1, hz, hz + 4, M.skin)
    P.tint(-3, 3, -1, 2, hz, hz + 1, M.skin)
    if not side:
        P.tint(-4, -3, -2, 0, hz + 1, hz + 3, M.skin)
        P.tint(3, 4, -2, 0, hz + 1, hz + 3, M.skin)
    for x in bangs:
        P.put(x, -3, hz + 3, hair)
    for x in bangs2:
        P.put(x, -3, hz + 2, hair)
    # 腮红
    P.face(-3, -3, hz, FRONT, BLUSH)
    P.face(2, -3, hz, FRONT, BLUSH)
    if shine and hair_hi:
        t = hz + h - 1
        for (x, y) in ((-2, -1), (-1, -1), (1, -1)):
            P.face(x, y, t, TOP, hair_hi)
    return P


def face(M, eye=EYE, mouth="#C7686C", glasses=None):
    """三套表情（薄片）：睁眼 = 1 × 2 的深色块 + 一点高光；紧闭 = 一道横杠；开心 = 拱形 + 张开的嘴。"""
    hz = M.hz
    y0, y1 = -3.11, -2.9
    piv = (0, 0.5, hz)
    fo = M.part("face_open", piv, outline=False)
    ft = M.part("face_tight", piv, outline=False)
    fh = M.part("face_happy", piv, outline=False)
    for cx in (-2, 1):                         # 眼睛所在的列
        fo.slab(cx, cx + 1, y0, y1, hz + 1, hz + 3, eye)
        hx = cx + 0.52 if cx < 0 else cx + 0.08
        fo.slab(hx, hx + 0.4, y0 - 0.03, y1, hz + 2.45, hz + 2.85, "#FFFFFF")
        ft.slab(cx - 0.3, cx + 1.3, y0, y1, hz + 1.55, hz + 2.2, eye)
        fh.slab(cx - 0.3, cx + 1.3, y0, y1, hz + 2.1, hz + 2.7, eye)
        fh.slab(cx - 0.3, cx + 0.25, y0, y1, hz + 1.5, hz + 2.1, eye)
        fh.slab(cx + 0.75, cx + 1.3, y0, y1, hz + 1.5, hz + 2.1, eye)
    fo.slab(-0.45, 0.45, y0, y1, hz + 0.42, hz + 0.72, mouth)
    ft.slab(-0.6, 0.6, y0, y1, hz + 0.4, hz + 0.68, mouth)
    fh.slab(-0.6, 0.6, y0, y1, hz + 0.18, hz + 0.9, "#D9535F")
    if glasses:                                # 眼镜：三套表情都带着
        for P in (fo, ft, fh):
            for cx in (-2, 1):
                a, b = cx - 0.42, cx + 1.42
                P.slab(a, b, y0 - 0.05, y1, hz + 3.12, hz + 3.34, glasses)
                P.slab(a, b, y0 - 0.05, y1, hz + 0.72, hz + 0.94, glasses)
                P.slab(a, a + 0.22, y0 - 0.05, y1, hz + 0.72, hz + 3.34, glasses)
                P.slab(b - 0.22, b, y0 - 0.05, y1, hz + 0.72, hz + 3.34, glasses)
            P.slab(-0.6, 0.6, y0 - 0.05, y1, hz + 2.25, hz + 2.47, glasses)
    return fo, ft, fh


def legs(M, shoe, leg=None, h=2, x=1, shoe_h=1, sole=None):
    """两条腿：小腿一根方柱（最下面 shoe_h 格是鞋 / 靴子），脚尖往前多伸一格。x：腿的内侧在第几列。枢轴在胯。"""
    leg = leg or M.skin
    for nm, sg in (("footA", 1), ("footB", -1)):
        x0 = x if sg > 0 else -x - 1
        P = M.part(nm, (x0 + 0.5, 0.5, h))
        P.box(x0, x0 + 1, 0, 1, 0, h, leg)
        P.box(x0, x0 + 1, 0, 1, 0, shoe_h, shoe)
        P.put(x0, -1, 0, shoe)
        if sole:
            P.slab(x0 - 0.07, x0 + 1.07, -1.07, 1.07, 0, 0.28, sole)
    return M.parts["footA"], M.parts["footB"]


def arms(M, sleeve, hand=None, x=2, z_top=None, n=3, cuff=None, bare_top=False, deep=1):
    """两只手臂：从肩往下 n 格，最下面一格是手。枢轴在肩。x：手臂内侧所在的列（身体宽 4 时是 2）。"""
    hand = hand or M.skin
    z1 = M.hz if z_top is None else z_top
    M.arm_len = float(n) - 0.5
    for nm, sg in (("armA", 1), ("armB", -1)):
        x0 = x if sg > 0 else -x - 1
        P = M.part(nm, (x0 + 0.5, 0.5, z1 - 0.5))
        P.box(x0, x0 + 1, 0, deep, z1 - n, z1, sleeve)
        P.tint(x0, x0 + 1, 0, deep, z1 - n, z1 - n + 1, hand)
        if cuff:
            P.tint(x0, x0 + 1, 0, deep, z1 - n + 1, z1 - n + 2, cuff)
        if bare_top:
            P.tint(x0, x0 + 1, 0, deep, z1 - 1, z1, M.skin)
    return M.parts["armA"], M.parts["armB"]


def hair_back(M, col, x0=-4, x1=4, z0=2, z1=None, y0=3, y1=5, wavy=False, tip=None, tip_rows=2, shade=None,
              shade_rows=0):
    """脑后的长发：一整片挂在头后面，枢轴在后脑勺上沿。wavy：两侧和下摆一格一格错开（波浪卷）。
    tip：最下面 tip_rows 排换成发梢色；shade：再往上 shade_rows 排用暗一号的颜色。"""
    hz = M.hz
    z1 = hz + 2 if z1 is None else z1
    P = M.part("hairback", (0, y0 + 0.5, z1 - 0.5))
    for z in range(z0, z1):
        ins = 1 if (wavy and (z - z0) % 2 == 1) else 0
        c = col
        tr = tip_rows if tip else 0
        if tip and z < z0 + tr:
            c = tip
        elif shade and z < z0 + tr + shade_rows:
            c = shade
        P.box(x0 + ins, x1 - ins, y0, y1, z, z + 1, c)
    if wavy:
        for x in range(x0, x1):
            if (x - x0) % 2 == 1:
                P.cut(x, x + 1, y0, y1, z0, z0 + 1)
        tidy(P)
    return P


def tidy(P):
    """去掉和部件主体不相连、只有一两块的散块（波浪发下摆两角会留下这种浮空的小块）。"""
    seen = set()
    for v0 in list(P.vox):
        if v0 in seen:
            continue
        comp, stack = [], [v0]
        seen.add(v0)
        while stack:
            v = stack.pop()
            comp.append(v)
            for dx, dy, dz in DIRS:
                n = (v[0] + dx, v[1] + dy, v[2] + dz)
                if n in P.vox and n not in seen:
                    seen.add(n)
                    stack.append(n)
        if len(comp) <= 2 and len(comp) < len(P.vox):
            for v in comp:
                P.vox.pop(v)
    return P


def team_scarf(M, color):
    """4v4 的队伍色围脖：脖子下面一圈薄片 + 背后垂一截。"""
    if not color:
        return
    B = M.part("body")
    hz = M.hz
    B.slab(-2.55, 2.55, -1.6, 2.6, hz - 0.85, hz + 0.02, color)
    B.slab(0.4, 1.6, 2.5, 3.05, hz - 2.4, hz - 0.4, color)
