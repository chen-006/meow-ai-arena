# -*- coding: utf-8 -*-
"""特效（Blender 里跑）：预先建好几种小网格的对象池，每帧按"事件发生后过了几步"摆位置、大小、透明度。
全部是自发光的半透明片 / 软团，颜色和透明度走物体颜色（ob.color），不用关键帧，任意一帧可单独算。"""
import math

import bpy
import numpy as np

import geom
from geom import MeshBuilder
from scene_lib import hexc, mat_emit_alpha, mat_soft_puff, mat_vcol
from pose import FLY_Z, clamp, ease, eout, bump

WHITE = (1.0, 1.0, 1.0)
SLICE_FX_Z = 0.36       # 横切的白光、碎方块从多高划过（约等于体素小人腰的高度）


def _lin(hex_):
    c = hexc(hex_)
    return (c[0], c[1], c[2])


TEAMC = None     # 两队的队伍色（线性），第一次用时从 chars.TEAM_COLORS 取


def _mixc(a, b, w):
    return tuple(x + (y - x) * w for x, y in zip(a, b))


# ---------------------------------------------------------------- 基本形
def _ring_mesh(name, r_in=0.9):
    seg = 56
    V, F, C = [], [], []
    for (r, a) in ((r_in - 0.08, 0.0), (r_in, 1.0), (1.0, 1.0), (1.06, 0.0)):
        for k in range(seg):
            t = 2 * math.pi * k / seg
            V.append((r * math.cos(t), r * math.sin(t), 0))
            C.append((1, 1, 1, a))
    for j in range(3):
        for k in range(seg):
            k2 = (k + 1) % seg
            F.append((j * seg + k, j * seg + k2, (j + 1) * seg + k2, (j + 1) * seg + k))
    return V, F, C


def _beam_mesh(seg=14):
    """复活的光柱：开口圆筒（半径 1、高 1），顶点 alpha 从底下 1 渐到顶上 0。"""
    V, F, C = [], [], []
    rows = (0.0, 0.25, 0.6, 1.0)
    alph = (1.0, 0.7, 0.3, 0.0)
    for zi, z in enumerate(rows):
        for k in range(seg):
            a = 2 * math.pi * k / seg
            V.append((math.cos(a), math.sin(a), z))
            C.append((1.0, 1.0, 1.0, alph[zi]))
    for zi in range(len(rows) - 1):
        for k in range(seg):
            k2 = (k + 1) % seg
            F.append((zi * seg + k, zi * seg + k2, (zi + 1) * seg + k2, (zi + 1) * seg + k))
    return V, F, C


def _disc_mesh():
    seg = 40
    V, F, C = [(0, 0, 0)], [], [(1, 1, 1, 1)]
    for (r, a) in ((0.5, 0.75), (1.0, 0.0)):
        for k in range(seg):
            t = 2 * math.pi * k / seg
            V.append((r * math.cos(t), r * math.sin(t), 0))
            C.append((1, 1, 1, a))
    for k in range(seg):
        k2 = (k + 1) % seg
        F.append((0, 1 + k, 1 + k2))
        F.append((1 + k, 1 + seg + k, 1 + seg + k2, 1 + k2))
    return V, F, C


def _star_mesh(spikes=8, r_in=0.3, alt=0.62):
    V, F, C = [(0, 0, 0)], [], [(1, 1, 1, 1)]
    n = spikes * 2
    for k in range(n):
        t = 2 * math.pi * k / n
        r = (1.0 if (k // 2) % 2 == 0 else alt) if k % 2 == 0 else r_in
        V.append((r * math.cos(t), r * math.sin(t), 0))
        C.append((1, 1, 1, 1.0 if k % 2 else 0.85))
    for k in range(n):
        F.append((0, 1 + k, 1 + (k + 1) % n))
    return V, F, C


def _arc_mesh(half=math.radians(62), r0=0.5):
    """月牙形刀光：圆心在原点，朝 +X，外沿半径 1。头（+角度一端）亮、尾巴淡。"""
    seg = 28
    V, F, C = [], [], []
    for i in range(seg + 1):
        u = i / seg
        t = -half + 2 * half * u
        wid = math.sin(math.pi * u) ** 0.7
        ri = 1.0 - (1.0 - r0) * wid
        a = (0.15 + 0.85 * u) * min(1.0, wid * 3)
        for (r, al) in ((ri, 0.0), (ri * 0.5 + 0.5, a * 0.75), (0.985, a), (1.03, 0.0)):
            V.append((r * math.cos(t), r * math.sin(t), 0))
            C.append((1, 1, 1, al))
    for i in range(seg):
        for j in range(3):
            a0 = i * 4 + j
            F.append((a0, a0 + 1, a0 + 5, a0 + 4))
    return V, F, C


def _streak_mesh():
    """一道细长的光：沿 X，两头尖、两头淡。"""
    V, F, C = [], [], []
    n = 12
    for i in range(n + 1):
        u = i / n
        x = -1.0 + 2.0 * u
        w = math.sin(math.pi * u) ** 0.6
        for (yy, a) in ((-1.0, 0.0), (0.0, 1.0), (1.0, 0.0)):
            V.append((x, yy * w, 0))
            C.append((1, 1, 1, a * min(1.0, w * 2.5)))
    for i in range(n):
        for j in range(2):
            a0 = i * 3 + j
            F.append((a0, a0 + 3, a0 + 4, a0 + 1))
    return V, F, C


def _cube_mesh():
    V = [(x, y, z) for x in (-0.5, 0.5) for y in (-0.5, 0.5) for z in (-0.5, 0.5)]
    F = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    return V, F, [(1, 1, 1, 1)] * 8


def _quad_mesh(w=1.0, h=0.5):
    return [(-w, -h, 0), (w, -h, 0), (w, h, 0), (-w, h, 0)], [(0, 1, 2, 3)], [(1, 1, 1, 1)] * 4


class Pool:
    def __init__(self, name, n, coll, mesh=None, mat=None, obj_fn=None):
        self.obs = []
        self.i = 0
        me = None
        if mesh is not None:
            V, F, C = mesh
            mb = MeshBuilder()
            mb.add(np.array(V, dtype=float), F, vcolors=np.array(C, dtype=float), smooth=False)
            first = mb.build(name + "_0", [mat], coll)
            me = first.data
            self.obs.append(first)
        for k in range(len(self.obs), n):
            ob = obj_fn(k) if obj_fn else bpy.data.objects.new("%s_%d" % (name, k), me)
            if not obj_fn:
                coll.objects.link(ob)
            self.obs.append(ob)
        for ob in self.obs:
            ob.hide_render = True

    def reset(self):
        for ob in self.obs[:self.i]:
            ob.hide_render = True
        self.i = 0

    def take(self):
        if self.i >= len(self.obs):
            return None
        ob = self.obs[self.i]
        self.i += 1
        ob.hide_render = False
        return ob


class FX:
    def __init__(self, coll, colors, pitch_deg=58.0):
        """colors：每个座位的主色（#RRGGBB）。"""
        self.col = [_lin(c) for c in colors]
        m = mat_emit_alpha("M_fx", strength=1.25)
        mhot = mat_emit_alpha("M_fx_hot", strength=2.2)
        self.ring = Pool("fx_ring", 28, coll, _ring_mesh("r"), m)
        self.disc = Pool("fx_disc", 12, coll, _disc_mesh(), mhot)
        self.star = Pool("fx_star", 12, coll, _star_mesh(), mhot)
        self.spark = Pool("fx_spark", 90, coll, _star_mesh(4, 0.22, 1.0), mhot)
        self.arc = Pool("fx_arc", 6, coll, _arc_mesh(), mhot)
        self.conf = Pool("fx_conf", 180, coll, _quad_mesh(0.1, 0.05), mat_emit_alpha("M_fx_conf", strength=1.0))
        self.streak = Pool("fx_streak", 8, coll, _streak_mesh(), mhot)
        self.beam = Pool("fx_beam", 8, coll, _beam_mesh(), m)          # 第 12 集：复活光柱
        self.cube = Pool("fx_cube", 48, coll, _cube_mesh(), mat_vcol("M_fx_cube", rough=0.9, spec=0.04, emis=0.25, objcol=True))
        iv, iff = geom.icosphere(2)
        pm = mat_soft_puff("M_puff", color="#FFFFFF", alpha=0.6)
        self.puff = Pool("fx_puff", 110, coll, (iv, [tuple(f) for f in iff], [(1.0, 0.97, 0.9, 0.85)] * len(iv)), pm)
        for ob in self.puff.obs:
            for p in ob.data.polygons:
                p.use_smooth = True
        self.tilt = math.radians(90.0 - pitch_deg)      # 让片状特效稍微朝镜头立起来一点

    def reset(self):
        for p in (self.ring, self.disc, self.star, self.spark, self.arc, self.conf, self.puff, self.streak, self.cube, self.beam):
            p.reset()

    # ---- 摆一个
    def put(self, pool, x, y, z, s, col, a, rot=0.0, flat=True, sy=None):
        ob = pool.take()
        if ob is None or a <= 0.003 or s <= 0:
            if ob is not None:
                ob.hide_render = True
            return
        ob.location = (x, -y, z)
        ob.scale = (s, s if sy is None else sy, s)
        ob.rotation_euler = (0.0 if flat else self.tilt, 0.0, rot)
        ob.color = (col[0], col[1], col[2], a)

    def sparks(self, x, y, z, u, n, speed, col, seed=0, life=7.0, size=0.12, col2=None, up=0.5):
        for j in range(n):
            h = math.sin(seed * 12.9898 + j * 78.233) * 43758.5453
            r1 = h - math.floor(h)
            h2 = math.sin(seed * 39.3 + j * 11.7) * 24634.63
            r2 = h2 - math.floor(h2)
            ang = 2 * math.pi * (j + r1) / n
            d = speed * (0.6 + 0.6 * r2) * (1 - math.exp(-u / 2.5)) * 2.5
            zz = z + up * (0.5 + r1) * u * 0.25 - 0.012 * u * u
            a = clamp(1 - u / life) ** 0.7
            c = col2 if (col2 is not None and j % 2) else col
            self.put(self.spark, x + math.cos(ang) * d, y + math.sin(ang) * d, max(0.05, zz),
                     size * (0.7 + 0.6 * r2) * (0.4 + 0.6 * a), c, a, rot=u * 0.7 + j, flat=False)

    def puffs(self, x, y, z, u, n, spread, size, life=9.0, seed=0, rise=0.03, alpha=0.8):
        for j in range(n):
            h = math.sin(seed * 7.13 + j * 91.7) * 15731.743
            r1 = h - math.floor(h)
            ang = 2 * math.pi * (j + r1 * 0.7) / n
            d = spread * (1 - math.exp(-u / 3.0)) * (0.6 + 0.8 * r1)
            s = size * (0.55 + 0.45 * r1) * (0.5 + 0.5 * eout(u / 3.0)) * (1 + 0.04 * u)
            a = alpha * clamp(1 - u / life) ** 1.3
            self.put(self.puff, x + math.cos(ang) * d, y + math.sin(ang) * d, z + s * 0.5 + rise * u, s, WHITE, a)

    # ---- 每帧
    def apply(self, anim, gt, env=None, offs=None, hide=(), no_fx=()):
        """offs / hide / no_fx 只在演示模式（demo.py）用：某座位的特效晚几步、某座位的特效不画、某类特效不画。"""
        self.reset()
        rp = anim.rp
        global TEAMC
        if TEAMC is None:
            import chars as _CH
            TEAMC = [_lin(c) for c in _CH.TEAM_COLORS]
        Y = _lin("#FFE066")
        offs = offs or {}
        for (T, ty, P) in anim.static_fx:
            if no_fx or hide or offs:
                own = P.get("victim", P.get("seat"))
                if ty in no_fx or own in hide:
                    continue
                u = gt - offs.get(own, 0.0) - T
            else:
                u = gt - T
            if ty == "respawn" and -12 <= u < 0:
                # 复活前：复活点上落下一道队伍色的光柱，越来越亮、越来越细，地上一圈往里收
                x, y = P["pos"]
                tc = TEAMC[P.get("team", 0) % 2]
                w = clamp((u + 12) / 10.0)
                ob = self.beam.take()
                if ob is not None:
                    ob.location = (x, -y, 0.0)
                    rr = 0.75 - 0.35 * w
                    ob.scale = (rr, rr, 4.5)
                    ob.rotation_euler = (0, 0, 0)
                    cc = _mixc(tc, WHITE, 0.35)
                    ob.color = (cc[0], cc[1], cc[2], 0.55 * w)
                self.put(self.ring, x, y, 0.04, 1.3 - 0.75 * w, _mixc(tc, WHITE, 0.3), 0.9 * w)
                continue
            if u < 0 or u > 44:
                continue
            x, y = P["pos"]
            seat = P.get("seat", 0)
            c = self.col[seat] if seat < len(self.col) else WHITE
            cl = _mixc(c, WHITE, 0.45)
            if ty == "land" and u < 10:
                self.put(self.ring, x, y, 0.03, 0.4 + 1.1 * eout(u / 6), WHITE, 0.75 * clamp(1 - u / 7))
                self.puffs(x, y, 0.0, u, 5, 0.7, 0.34, life=9, seed=seat)
            elif ty == "dust" and u < 9:
                self.puffs(x, y, 0.0, u, 3, 0.35, 0.3 * P.get("size", 1.0), life=8, seed=T)
            elif ty == "turn" and u < 6:
                self.put(self.ring, x, y, FLY_Z + 0.2, 0.3 + 0.55 * eout(u / 5), cl, 0.8 * clamp(1 - u / 6))
            elif ty == "catch" and u < 6:
                self.put(self.ring, x, y, 0.75, 0.75 - 0.5 * eout(u / 4), cl, 0.85 * clamp(1 - u / 5))
                self.sparks(x, y, 0.8, u, 4, 0.22, WHITE, seed=T, life=5, size=0.1)
            elif ty == "drop" and u < 9:
                self.put(self.star, x, y, 0.3, 0.42 * bump(u / 3.0) ** 0.5, _mixc(Y, WHITE, 0.5), clamp(1 - u / 3), rot=T, flat=False)
                self.puffs(x, y, 0.0, u, 3, 0.3, 0.24, life=8, seed=T)
            elif ty == "clash" and u < 10:
                a, b = P["seats"]
                self.put(self.disc, x, y, FLY_Z, 0.95 * eout(u / 1.0), WHITE, 0.6 * clamp(1 - u / 2.6))
                self.put(self.star, x, y, FLY_Z + 0.02, 1.05 * eout(u / 1.2) * (1 - 0.3 * ease((u - 2) / 4)), _mixc(Y, WHITE, 0.6),
                         clamp(1 - (u - 2.2) / 3.2), rot=0.4 + T, flat=False)
                self.put(self.ring, x, y, FLY_Z, 0.4 + 1.3 * eout(u / 5), WHITE, 0.8 * clamp(1 - u / 6) ** 1.5)
                self.sparks(x, y, FLY_Z, u, 10, 0.5, _mixc(self.col[a], WHITE, 0.25), seed=T, life=9, size=0.17,
                            col2=_mixc(self.col[b], WHITE, 0.25))
            elif ty == "hit" and u < 9:
                z = 0.6
                if not P.get("sliced"):          # 被切开的那种：不盖星爆，免得挡住切口
                    self.put(self.disc, x, y, z, 1.05 * eout(u / 1.0), WHITE, 0.38 * clamp(1 - u / 2.8))
                    self.put(self.star, x, y, z + 0.02, 1.3 * eout(u / 1.0), _mixc(c, WHITE, 0.2), clamp(1 - (u - 2.4) / 3.0),
                             rot=T * 1.7, flat=False)
                    self.put(self.star, x, y, z + 0.05, 0.72 * eout(u / 1.0), WHITE, clamp(1 - (u - 2.0) / 2.6),
                             rot=T * 1.7 + 0.39, flat=False)
                self.put(self.ring, x, y, 0.05, 0.5 + 1.5 * eout(u / 5), cl, 0.85 * clamp(1 - u / 6.5) ** 1.5)
                self.sparks(x, y, z, u, 8, 0.55, cl, seed=T, life=8, size=0.17, col2=WHITE)
            elif ty == "slice" and u < 13:
                # 被镖切开：沿镖的方向一道白光 + 几粒小方块从切口飞出去
                d = P["dir"]
                sd = P["side"]
                ang = math.atan2(-d[1], d[0])
                vc = self.col[P["victim"]] if P["victim"] < len(self.col) else WHITE
                # 横切（11-16）：白光贴着腰的高度水平划过去，小方块从切口沿镖的方向喷出去
                zc = SLICE_FX_Z
                L = 0.5 + 1.9 * eout(u / 0.7)
                a = clamp(1 - (u - 1.4) / 2.6)
                self.put(self.streak, x + d[0] * 0.25, y + d[1] * 0.25, zc, L, _mixc(c, WHITE, 0.55), a * 0.9, rot=ang, sy=0.2)
                self.put(self.streak, x + d[0] * 0.25, y + d[1] * 0.25, zc + 0.02, L * 0.92, WHITE, a, rot=ang, sy=0.075)
                self.put(self.ring, x, y, zc, 0.35 + 0.75 * eout(u / 2.2), WHITE, 0.7 * clamp(1 - u / 3.2) ** 1.4)
                pale = _mixc(vc, WHITE, 0.7)
                for j in range(9):
                    h = math.sin(T * 5.3 + j * 12.9898) * 43758.5453
                    r1 = h - math.floor(h)
                    h = math.sin(T * 2.1 + j * 4.51) * 9631.77
                    r2 = h - math.floor(h)
                    along = (r1 - 0.3) * 0.25 + 0.11 * u * (0.4 + r2)
                    sg = 1.0 if j % 2 else -1.0
                    out = sg * (0.04 + 0.05 * u * (0.3 + r2))
                    zz = zc + (0.08 + 0.1 * r1) * u - 0.016 * u * u
                    ob = self.cube.take()
                    if ob is None:
                        break
                    if zz < 0.05:
                        zz = 0.05
                    k = clamp(1 - (u - 8) / 4.5)
                    if k <= 0.01:
                        ob.hide_render = True
                        continue
                    sz = 0.088 * (0.6 + 0.6 * r2) * k
                    ob.location = (x + d[0] * along + sd[0] * out, -(y + d[1] * along + sd[1] * out), zz)
                    ob.rotation_euler = (u * (0.3 + r1), u * (0.2 + r2), j) if zz > 0.06 else (0, 0, j)
                    ob.scale = (sz, sz, sz)
                    cc = pale if j % 3 else vc
                    ob.color = (cc[0], cc[1], cc[2], 1.0)
            elif ty == "kickhit" and u < 7:
                self.put(self.star, x, y, 0.55, 0.8 * eout(u / 1.2), WHITE, clamp(1 - (u - 1.8) / 2.5), rot=T, flat=False)
                self.put(self.ring, x, y, 0.05, 0.4 + 1.0 * eout(u / 5), WHITE, 0.7 * clamp(1 - u / 6))
                self.sparks(x, y, 0.55, u, 5, 0.4, Y, seed=T, life=6, size=0.13)
            elif ty == "slash" and u < 7:
                d = P["dir"]
                ang = math.atan2(-d[1], d[0])
                reach = rp.cfg.get("SLASH_REACH", 1500) / 1000.0
                sw = eout(u / 2.0)
                a = clamp(u / 0.5) * clamp(1 - (u - 2.0) / 4.5)
                self.put(self.arc, x, y, 0.55, reach * (0.86 + 0.14 * sw), WHITE, a, rot=ang + math.radians(38 - 60 * sw))
                self.put(self.arc, x, y, 0.5, reach * (0.74 + 0.16 * sw), cl, a * 0.8, rot=ang + math.radians(52 - 60 * sw))
            elif ty == "kick" and u < 5:
                d = P["dir"]
                ang = math.atan2(-d[1], d[0])
                a = clamp(u / 0.4) * clamp(1 - (u - 1.2) / 3.2)
                self.put(self.arc, x, y, 0.4, 1.15 * (0.7 + 0.3 * eout(u / 2.0)), WHITE, a * 0.85, rot=ang - 0.15, sy=0.75)
            elif ty == "poof" and u < 10:
                z = P.get("z", 0.5)
                sz = P.get("size", 1.0)
                self.puffs(x, y, z - 0.25, u, 6, 0.55 * sz, 0.5 * sz, life=9, seed=T, rise=0.02, alpha=0.95)
                self.put(self.ring, x, y, z, (0.3 + 1.0 * eout(u / 6)) * sz, cl, 0.8 * clamp(1 - u / 7), flat=False)
                self.sparks(x, y, z, u, 7, 0.45 * sz, cl, seed=T, life=9, size=0.16 * sz, col2=WHITE)
            elif ty == "splash" and u < 16 and not P.get("liquid"):
                # 没入云海：一圈白色云团往外翻起再慢慢散开，中间一道冲击环
                z = P.get("z", -10.5)
                self.puffs(x, y, z - 0.4, u, 9, 1.6, 1.15, life=15, seed=T, rise=0.05, alpha=0.95)
                self.puffs(x, y, z - 0.2, u * 0.7, 4, 0.5, 0.8, life=11, seed=T + 3, rise=0.09, alpha=0.9)
                self.put(self.ring, x, y, z + 0.1, 0.6 + 2.2 * eout(u / 6), WHITE, 0.75 * clamp(1 - u / 8) ** 1.3)
            elif ty == "respawn" and u < 12:
                # 复活落地：光柱散开、地上一圈冲出去、几粒光点
                tc = TEAMC[P.get("team", 0) % 2]
                ob = self.beam.take()
                if ob is not None and u < 5:
                    ob.location = (x, -y, 0.0)
                    rr = 0.4 + 0.5 * eout(u / 5)
                    ob.scale = (rr, rr, 4.5 * (1 - u / 5))
                    ob.rotation_euler = (0, 0, 0)
                    cc = _mixc(tc, WHITE, 0.5)
                    ob.color = (cc[0], cc[1], cc[2], 0.6 * (1 - u / 5))
                elif ob is not None:
                    ob.hide_render = True
                self.put(self.ring, x, y, 0.04, 0.4 + 1.5 * eout(u / 6), _mixc(tc, WHITE, 0.4), 0.9 * clamp(1 - u / 8) ** 1.3)
                self.sparks(x, y, 0.3, u, 8, 0.4, _mixc(tc, WHITE, 0.5), seed=T, life=10, size=0.13, col2=WHITE, up=1.2)
            elif ty == "kinghit" and u < 16:
                # 国王掉一条命：金色的大冲击环 + 金星 + 金色火花（比普通命中更响）
                G = _lin("#FFC93A")
                self.put(self.ring, x, y, 0.05, 0.6 + 2.4 * eout(u / 7), G, 0.95 * clamp(1 - u / 10) ** 1.3)
                self.put(self.ring, x, y, 0.9, 0.4 + 1.4 * eout(u / 5), WHITE, 0.8 * clamp(1 - u / 7), flat=False)
                self.put(self.star, x, y, 1.0, 1.5 * eout(u / 1.2), _mixc(G, WHITE, 0.3), clamp(1 - (u - 2.5) / 3.5), rot=T, flat=False)
                self.sparks(x, y, 1.0, u, 12, 0.7, G, seed=T, life=12, size=0.18, col2=WHITE, up=1.0)
            elif ty == "capture" and u < 22:
                # 翻点完成：圈的边上冲出一圈新主人颜色的环，绕圈一串光点
                tc = TEAMC[P.get("team", 0) % 2]
                r = P.get("r", 2.0)
                self.put(self.ring, x, y, 0.05, r * (1.0 + 0.5 * eout(u / 8)), _mixc(tc, WHITE, 0.2), 0.95 * clamp(1 - u / 14) ** 1.2)
                self.put(self.disc, x, y, 0.03, r * 1.05, _mixc(tc, WHITE, 0.5), 0.35 * clamp(1 - u / 6))
                for j in range(10):
                    a = 2 * math.pi * j / 10 + u * 0.08
                    zz = 0.1 + 0.09 * u - 0.004 * u * u
                    if zz > 0.04:
                        self.put(self.spark, x + r * math.cos(a), y + r * math.sin(a), zz, 0.17 * clamp(1 - u / 20),
                                 _mixc(tc, WHITE, 0.4), clamp(1 - u / 20), rot=u * 0.5 + j, flat=False)
            elif ty == "splash" and u < 16 and P.get("liquid"):
                # 掉进液面（第 12 集）：岩浆 = 橙红的火星 + 暗红的烟；水 = 白色水花 + 一圈涟漪
                z = P.get("z", -0.55)
                if P["liquid"] == "lava":
                    O, R2 = _lin("#FF8A2A"), _lin("#FFD45A")
                    self.sparks(x, y, z + 0.1, u, 14, 0.5, O, seed=T, life=13, size=0.16, col2=R2, up=1.6)
                    self.put(self.ring, x, y, z + 0.04, 0.4 + 1.5 * eout(u / 6), R2, 0.9 * clamp(1 - u / 9) ** 1.3)
                    self.put(self.disc, x, y, z + 0.03, 0.9 * eout(u / 2), _lin("#FFF0B0"), 0.7 * clamp(1 - u / 5))
                    self.puffs(x, y, z, u, 5, 0.6, 0.55, life=15, seed=T, rise=0.07, alpha=0.6)
                else:
                    Wt = _lin("#E8F6FF")
                    self.sparks(x, y, z + 0.1, u, 12, 0.35, Wt, seed=T, life=11, size=0.15, col2=WHITE, up=1.8)
                    self.put(self.ring, x, y, z + 0.03, 0.4 + 1.8 * eout(u / 7), WHITE, 0.85 * clamp(1 - u / 11) ** 1.3)
                    self.put(self.ring, x, y, z + 0.03, 0.2 + 1.0 * eout(u / 9), WHITE, 0.6 * clamp(1 - (u - 3) / 10))
                    self.puffs(x, y, z, u * 0.8, 6, 0.5, 0.45, life=12, seed=T, rise=0.05, alpha=0.85)
            elif ty == "fallcry" and u < 7:
                self.sparks(x, y, 1.35, u, 3, 0.16, WHITE, seed=T, life=6, size=0.11, up=1.0)
            elif ty == "confetti":
                cols = [c, WHITE, Y, _lin("#FF8FA8"), _lin("#7FD8FF")]
                for j in range(40):
                    h = math.sin(seat * 3.1 + j * 12.9898) * 43758.5453
                    r1 = h - math.floor(h)
                    h = math.sin(seat * 1.7 + j * 4.51) * 9631.77
                    r2 = h - math.floor(h)
                    ang = 2 * math.pi * r1
                    sp = 0.05 + 0.13 * r2
                    d = sp * 12 * (1 - math.exp(-u / 9.0))
                    zz = 1.2 + (0.42 + 0.3 * r1) * u - 0.013 * u * u * (0.7 + 0.5 * r2)
                    if zz < 0.03:
                        zz = 0.03
                    ob = self.conf.take()
                    if ob is None:
                        break
                    ob.location = (x + math.cos(ang) * d, -(y + math.sin(ang) * d), zz)
                    ob.rotation_euler = (u * (0.5 + r1), u * (0.3 + r2), ang) if zz > 0.04 else (0, 0, ang)
                    ob.scale = (1, 1, 1)
                    cc = cols[j % len(cols)]
                    ob.color = (cc[0], cc[1], cc[2], clamp(1 - (u - 34) / 10))
        cfx = anim.continuous_fx(gt)
        if offs or hide or no_fx:
            cfx = [e for e in cfx if e[1]["seat"] not in offs and e[1]["seat"] not in hide and e[0] not in no_fx]
            for k_, o_ in offs.items():
                cfx += [e for e in anim.continuous_fx(gt - o_) if e[1]["seat"] == k_ and e[0] not in no_fx]
        for (ty, P) in cfx:
            x, y = P["pos"]
            seat = P["seat"]
            c = self.col[seat]
            u = P["age"]
            if ty == "stun":
                for j in range(3):
                    a = u * 0.55 + j * 2.094
                    self.put(self.spark, x + 0.34 * math.cos(a), y + 0.34 * math.sin(a) * 0.9, 1.5 + 0.05 * math.sin(a * 2),
                             0.14, Y, 0.95, rot=u * 0.4 + j, flat=False)
            elif ty == "recall":
                cl = _mixc(c, WHITE, 0.4)
                for j in range(2):
                    ph = ((u / 6.0) + j * 0.5) % 1.0
                    self.put(self.ring, x, y, 0.04 + 1.0 * ph, 0.85 - 0.6 * ph, cl, 0.75 * math.sin(math.pi * ph))
                dx, dy = P["dart"]
                ph = (u / 5.0) % 1.0
                self.put(self.ring, dx, dy, 0.03, 0.25 + 0.5 * ph, cl, 0.8 * (1 - ph))
                self.put(self.disc, dx, dy, 0.02, 0.55, cl, 0.25 + 0.25 * P["prog"])
        # 塌陷：碎块断开那一刻冒一团尘土
        if env is not None:
            ws = rp.wave_steps
            warn_n = rp.cfg.get("COL_WARN", 30)
            dmax = {}
            for p in env.pieces:
                dmax[p["wave"]] = max(dmax.get(p["wave"], 0.1), p["d_in"])
            for p in env.pieces:
                w = p["wave"]
                u = gt - ws[w]
                r = p["rnd"]
                if -warn_n <= u < -warn_n + 5:                    # 裂开的一下
                    self.puffs(p["cx"], p["cy"], 0.0, u + warn_n, 1, 0.1, 0.3, life=5, seed=p["id"], alpha=0.5)
                delay = (1.0 - p["d_in"] / dmax[w]) * 5.0 + r[0] * 2.0
                v = u - delay
                if 0 <= v < 10:
                    self.puffs(p["cx"], p["cy"], -0.15, v, 2, 0.5, 0.5 + 0.25 * r[1], life=10, seed=p["id"], rise=-0.02,
                               alpha=0.75)
