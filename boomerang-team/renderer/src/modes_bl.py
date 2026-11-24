# -*- coding: utf-8 -*-
"""第 12 集 5v5 的模式物件（Blender 里用）：占点的三个地上圆圈。
每个点三层，都是贴地的自发光半透明网格（和脚下色环同一种材质）：
  底    整个圈里一层很淡的归属色（中立 = 白）
  边    圈的边线，归属色、实；翻点完成那一下闪白
  进度  边线内侧一道弧，从 12 点钟方向顺时针长出来，颜色 = 正在翻点的队伍；进度清零就收回去
字母（A B C）和顶部比分由信息栏画。"""
import math

import numpy as np

import scene_lib as SL
from geom import MeshBuilder

TEAM_LIN = None
SEG = 72


def _lin(h):
    c = SL.hexc(h)
    return (c[0], c[1], c[2])


def _annulus(name, r_in, r_out, coll, z, seg=SEG, alpha=1.0):
    V, F, C = [], [], []
    for k in range(seg):
        a = 2 * math.pi * k / seg
        V.append((r_out * math.cos(a), r_out * math.sin(a), z))
        V.append((r_in * math.cos(a), r_in * math.sin(a), z))
        C += [(1.0, 1.0, 1.0, alpha)] * 2
    for k in range(seg):
        k2 = (k + 1) % seg
        F.append((2 * k, 2 * k2, 2 * k2 + 1, 2 * k + 1))
    mb = MeshBuilder()
    mb.add(np.array(V), F, vcolors=np.array(C), smooth=False)
    return mb.build(name, [SL.mat_emit_alpha("M_point", strength=1.4)], coll)


def _disc(name, r, coll, z, seg=SEG, alpha=1.0):
    V = [(0.0, 0.0, z)] + [(r * math.cos(2 * math.pi * k / seg), r * math.sin(2 * math.pi * k / seg), z) for k in range(seg)]
    F = [(0, 1 + k, 1 + (k + 1) % seg) for k in range(seg)]
    C = [(1.0, 1.0, 1.0, alpha)] * len(V)
    mb = MeshBuilder()
    mb.add(np.array(V), F, vcolors=np.array(C), smooth=False)
    return mb.build(name, [SL.mat_emit_alpha("M_point", strength=1.4)], coll)


class Arc:
    """一道可变长度的弧（固定拓扑，每帧改顶点）：从 12 点钟方向（屏幕上方 = 世界 +Y）顺时针。"""

    def __init__(self, name, r_in, r_out, coll, z, seg=SEG):
        self.seg, self.r_in, self.r_out, self.z = seg, r_in, r_out, z
        V = np.zeros(((seg + 1) * 2, 3))
        F = [(2 * k, 2 * k + 2, 2 * k + 3, 2 * k + 1) for k in range(seg)]
        mb = MeshBuilder()
        mb.add(V, F, vcolors=np.ones(((seg + 1) * 2, 4)), smooth=False)
        self.ob = mb.build(name, [SL.mat_emit_alpha("M_point", strength=1.4)], coll)
        self.me = self.ob.data
        self.co = np.zeros(((seg + 1) * 2, 3), dtype=np.float32)
        self.frac = -1.0

    def set(self, frac):
        if frac <= 0.004:
            self.ob.hide_render = True
            self.frac = 0.0
            return
        self.ob.hide_render = False
        if abs(frac - self.frac) < 1e-4:
            return
        self.frac = frac
        for k in range(self.seg + 1):
            a = math.pi / 2 - 2 * math.pi * frac * k / self.seg
            c, s = math.cos(a), math.sin(a)
            self.co[2 * k] = (self.r_out * c, self.r_out * s, self.z)
            self.co[2 * k + 1] = (self.r_in * c, self.r_in * s, self.z)
        self.me.vertices.foreach_set("co", self.co.ravel())
        self.me.update()


class Points:
    def __init__(self, rp, coll, team_colors):
        self.rp = rp
        self.tc = [_lin(c) for c in team_colors]
        self.items = []
        for i, (nm, x, y, r) in enumerate(rp.points):
            base = _disc("pt%d_base" % i, r, coll, 0.006)
            edge = _annulus("pt%d_edge" % i, r - 0.11, r + 0.02, coll, 0.010)
            edge2 = _annulus("pt%d_edge2" % i, r + 0.02, r + 0.07, coll, 0.009)      # 外面一道白边：在什么地面上都分得出
            arc = Arc("pt%d_arc" % i, r - 0.42, r - 0.17, coll, 0.012)
            for ob in (base, edge, edge2, arc.ob):
                ob.location = (x, -y, 0.0)
            self.items.append(dict(base=base, edge=edge, edge2=edge2, arc=arc, r=r))

    def apply(self, gt):
        rp = self.rp
        c = rp.ctl_at(gt) if gt >= 0 else None
        cap = float(rp.cfg.get("CAPTURE", 20))
        for i, it in enumerate(self.items):
            own = c["owner"][i] if c else -1
            tk = c["taking"][i] if c else -1
            pr = (c["prog"][i] + (gt - int(gt) if (c and tk >= 0 and c["prog"][i] > 0) else 0.0)) / cap if c else 0.0
            col = self.tc[own] if own >= 0 else (1.0, 1.0, 1.0)
            # 翻点完成：边线闪白、底色一亮
            fl = 0.0
            for (t, j, old, new) in rp.captures:
                u = gt - (t + 1.0)
                if j == i and 0 <= u < 12:
                    fl = max(fl, 1 - u / 12.0)
            ec = tuple(col[q] + (1 - col[q]) * fl * 0.8 for q in range(3))
            it["base"].color = col + ((0.2 if own >= 0 else 0.1) + 0.25 * fl,)
            it["edge"].color = ec + (0.95,)
            it["edge2"].color = (1.0, 1.0, 1.0, 0.7)
            if tk >= 0:
                it["arc"].ob.color = self.tc[tk] + (0.95,)
            it["arc"].set(min(1.0, pr) if tk >= 0 else 0.0)
