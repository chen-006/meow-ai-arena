# -*- coding: utf-8 -*-
"""特效（Blender 里用）：全部是"粒子"——一小团方块，按简单的公式飞出去、长大、缩没。

  px = Particles(coll, fdt)
  scan(rp, px, L, 地面高度函数, a, b)      # 把回放里要出特效的地方全部变成粒子（只算会落在第 a～b 帧里的）
  px.build()                                # 建物体（同一种粒子的物体轮流用，物体数 = 同时在场的最多个数）
  px.apply(i)                               # 每帧摆一次

出什么特效（都是为了"看得懂发生了什么"，个头小、散得快，不盖住主体）：
  爆炸        一下白闪 + 贴地一圈冲击波 + 往外喷的火团（0.5 秒内缩没）+ 一团团小烟各自飘开升起（2 秒内散完）+ 火星 + 碎块
  兵器命中    一个小四角星 + 几点火星；被盾挡住只有火星；镇山兽撞人、骑兵撞人的大一号，带尘土
  冲锋        骑兵、镇山兽冲起来脚后扬尘（颜色随地图：草地是土色、雪地是白的）；镇山兽刨地时往后刨土
  雪球        滚动时贴地一路雪雾，撞到人炸一小团雪；停稳消失时碎成一堆雪块化掉
  石头        飞行中拖一串小灰团，落地一圈尘土 + 碎石
  桥塌 / 木栅、门碎   碎木片 + 木屑尘；立石碎、镇山兽撞上东西：碎石 + 尘土 + 冲击波
  倒下 / 落地  被打飞的人落地一小团尘土；阵亡一小团；尸体消失前（沉入地面）也有一点
  泥塘        在泥里走：脚下溅泥水（一小团泥色的雾 + 往上溅的泥点）；出了泥塘还带着泥：一路往下掉泥块；
              带泥的时间到了（又能跑快了）：腿上的泥一下掉下几块
"""
import math

import bpy
import numpy as np

import army as AR
from scene_lib import hexc

DEG = math.pi / 180.0
FLAT = ("p_ring",)


class Particles:
    def __init__(self, coll, fdt):
        self.coll = coll
        self.fdt = fdt
        self.sp = []
        self.slots = {}
        self.by_frame = {}
        self.live = set()
        self.cols = {}

    def emit(self, kind, f0, life, p, v=(0.0, 0.0, 0.0), g=0.0, drag=0.0, rise=0.0, s0=1.0, s1=None, color=None,
             rot=(0.0, 0.0, 0.0), spin=(0.0, 0.0, 0.0), floor=None, pop=0.06):
        """kind 粒子模型；f0 第几帧出现（可以是小数）；life 活几秒；p、v 引擎坐标（y 朝上）；g 重力；drag 阻力（越大停得越快）；
        rise 匀速上升；s0 → s1 大小（最后三成时间缩没）；color 给带 T 记号的方块上色；rot、spin 初始角度和转速（度）；
        floor 落到这个高度就停。"""
        self.sp.append((kind, float(f0), float(life), tuple(float(c) for c in p), tuple(float(c) for c in v), float(g),
                        float(drag), float(rise), float(s0), float(s0 if s1 is None else s1), color, rot, spin, floor,
                        float(pop)))

    def build(self):
        order = sorted(range(len(self.sp)), key=lambda j: self.sp[j][1])
        ends = {}
        self.assign = [None] * len(self.sp)
        for j in order:
            kind, f0, life = self.sp[j][0], self.sp[j][1], self.sp[j][2]
            f1 = f0 + life / self.fdt
            lst = ends.setdefault(kind, [])
            for n, e in enumerate(lst):
                if e < f0 - 1:
                    lst[n] = f1
                    self.assign[j] = (kind, n)
                    break
            else:
                lst.append(f1)
                self.assign[j] = (kind, len(lst) - 1)
            for fr in range(int(math.ceil(f0)), int(math.floor(f1)) + 1):
                self.by_frame.setdefault(fr, []).append(j)
        K = AR.kit()
        self.obs = {}
        for kind, lst in ends.items():
            me = K.mesh(kind, "body")
            for n in range(len(lst)):
                ob = bpy.data.objects.new("px_%s_%03d" % (kind, n), me)
                ob.hide_render = True
                ob.rotation_mode = 'XYZ'
                self.coll.objects.link(ob)
                self.obs[(kind, n)] = ob
        return len(self.obs)

    def apply(self, i):
        now = set()
        for j in self.by_frame.get(i, ()):
            (kind, f0, life, p, v, g, drag, rise, s0, s1, color, rot, spin, floor, pop) = self.sp[j]
            age = (i - f0) * self.fdt
            if age < 0 or age > life:
                continue
            key = self.assign[j]
            ob = self.obs[key]
            now.add(key)
            u = age / life
            tau = (1.0 - math.exp(-drag * age)) / drag if drag > 1e-6 else age
            x = p[0] + v[0] * tau
            z = p[2] + v[2] * tau
            y = p[1] + v[1] * tau + rise * age - 0.5 * g * age * age
            if floor is not None and y < floor:
                y = floor
            s = (s0 + (s1 - s0) * u) * min(1.0, 0.35 + 0.65 * age / max(pop, 1e-3)) * (1.0 - u ** 3 if u > 0.0 else 1.0)
            s = max(s, 0.001)
            ob.location = (x, z, y)
            ob.scale = (s, s, 1.0) if kind in FLAT else (s, s, s)
            ob.rotation_euler = ((rot[0] + spin[0] * age) * DEG, (rot[1] + spin[1] * age) * DEG, (rot[2] + spin[2] * age) * DEG)
            if color is not None and self.cols.get(key) != color:
                c = hexc(color)
                ob.color = (c[0], c[1], c[2], 1.0)
                self.cols[key] = color
            ob.hide_render = False
        for key in self.live - now:
            self.obs[key].hide_render = True
        self.live = now


# ================================================================ 各种特效怎么喷
WOOD, WOOD2, STONE, STONE2, DARK, SNOW = "#9A6A3E", "#C08A52", "#A8B0BC", "#7C8494", "#3A3234", "#F4FAFF"
MUDS = ("#4A3422", "#5E432A", "#6E5034")


def _dir(rg, up_lo=0.1, up_hi=1.0):
    a = rg.uniform(0, 2 * math.pi)
    u = rg.uniform(up_lo, up_hi)
    h = math.sqrt(max(0.0, 1 - u * u))
    return np.array([h * math.cos(a), u, h * math.sin(a)])


def _rot(rg):
    return tuple(rg.uniform(0, 360, 3))


def _spin(rg, k=500.0):
    return tuple(rg.uniform(-k, k, 3))


def puffs(px, rg, f, p, n, col, spread=0.6, speed=1.5, size=(0.5, 0.9), life=(0.5, 0.9), up=0.8, v0=(0, 0, 0), kind="p_puff"):
    for _ in range(n):
        d = _dir(rg, 0.0, 0.5)
        q = np.array(p, float) + np.array([rg.uniform(-spread, spread), rg.uniform(0, 0.25), rg.uniform(-spread, spread)])
        v = d * rg.uniform(0.4, 1.0) * speed + np.array(v0, float)
        s = rg.uniform(*size)
        px.emit(kind, f + rg.uniform(0, 2), rg.uniform(*life), q, v, drag=3.0, rise=up * rg.uniform(0.5, 1.2), s0=s * 0.6,
                s1=s * 1.25, color=col, rot=_rot(rg), spin=_spin(rg, 60), pop=0.1)


def chips(px, rg, f, p, n, cols, spread=0.5, speed=(3.0, 7.0), up=(3.0, 7.0), size=(0.8, 1.6), life=(0.9, 1.5), floor=None,
          v0=(0, 0, 0)):
    for _ in range(n):
        a = rg.uniform(0, 2 * math.pi)
        sp = rg.uniform(*speed)
        q = np.array(p, float) + np.array([rg.uniform(-spread, spread), rg.uniform(0, 0.6), rg.uniform(-spread, spread)])
        v = np.array([sp * math.cos(a), rg.uniform(*up), sp * math.sin(a)]) + np.array(v0, float)
        px.emit("p_chip", f, rg.uniform(*life), q, v, g=14.0, s0=rg.uniform(*size), color=cols[int(rg.integers(len(cols)))],
                rot=_rot(rg), spin=_spin(rg), floor=floor, pop=0.01)


def sparks(px, rg, f, p, n, speed=(6.0, 12.0), size=(0.8, 1.4), life=(0.18, 0.34)):
    for _ in range(n):
        d = _dir(rg, -0.2, 0.9)
        px.emit("p_spark", f, rg.uniform(*life), p, d * rg.uniform(*speed), g=12.0, drag=2.0, s0=rg.uniform(*size),
                rot=_rot(rg), spin=_spin(rg, 300), pop=0.01)


def explosion(px, rg, f, p, dust, ground=None, big=1.0):
    p = np.array(p, float)
    c = p + np.array([0, 0.7, 0])
    px.emit("p_flash", f, 0.13, c, s0=1.0 * big, s1=2.2 * big, rot=_rot(rg), pop=0.03)
    if ground is not None and abs(ground - p[1]) < 1.6:
        px.emit("p_ring", f, 0.5, (p[0], ground + 0.08, p[2]), s0=0.35 * big, s1=2.4 * big, color=dust, rot=(0, 0, rg.uniform(0, 360)),
                pop=0.01)
    for _ in range(int(10 * big)):
        d = _dir(rg, 0.15, 1.0)
        s = rg.uniform(0.9, 1.5) * big
        px.emit("p_fire", f + rg.uniform(0, 1.5), rg.uniform(0.3, 0.55), c + d * 0.3, d * rg.uniform(5, 10) * big, drag=5.0, rise=1.5,
                s0=s, s1=s * 0.3, rot=_rot(rg), spin=_spin(rg, 200), pop=0.04)
    for _ in range(int(11 * big)):
        d = _dir(rg, 0.0, 0.7)
        s = rg.uniform(0.55, 0.95) * big
        px.emit("p_smoke", f + rg.uniform(2, 6), rg.uniform(1.1, 2.0), c + d * rg.uniform(0.2, 0.9), d * rg.uniform(2.0, 5.0),
                drag=2.6, rise=rg.uniform(1.0, 2.2), s0=s, s1=s * 0.7, rot=_rot(rg), spin=_spin(rg, 50), pop=0.15)
    sparks(px, rg, f, c, int(9 * big), speed=(8, 16), life=(0.25, 0.5))
    chips(px, rg, f, c, int(7 * big), (DARK, WOOD, "#5A4A40"), speed=(3, 8), up=(4, 9), floor=ground)


def scan(rp, px, L, hfun, a=0, b=None):
    """把回放变成粒子。只算会落进第 a～b 帧的（分段并行时各段各算各的；随机数按事件号 / 帧号定种子，各段算出来一样）。"""
    N, NX = rp.n, rp.nx
    b = NX if b is None else b
    fdt = rp.dt * rp.rec_every
    fps = 1.0 / fdt
    lo, hi = a - int(2.6 * fps), b
    dust = L.get("dust", "#E4D4B0")
    kinds = [rp.info[i].get('kind') for i in rp.ids]
    K = len(rp.ids)
    is_unit = np.array([rp.info[i]['type'] == 'unit' for i in rp.ids])

    def gy(x, z):
        try:
            return hfun(x, z)
        except Exception:
            return None

    def heading(i, k):
        j = max(i - 2, 0)
        d = rp.P[i, k] - rp.P[j, k]
        d[1] = 0
        n = np.linalg.norm(d)
        return d / n if n > 1e-4 else np.array([0.0, 0.0, 1.0])

    last_ball_hit = {}
    for n_ev, (e, f) in enumerate(zip(rp.events, rp.ev_frame)):
        if f < lo or f > hi:
            continue
        t = e["t"]
        rg = np.random.default_rng(7000 + n_ev)
        if t == "explode" and "p" in e:
            explosion(px, rg, f, e["p"], dust, gy(e["p"][0], e["p"][2]))
        elif t == "hit":
            v = e.get("v", -1)
            if v not in rp.col or not rp.present[f, rp.col[v]]:
                continue
            k = rp.col[v]
            w = e.get("w", "")
            hgt = {"boss": 1.8, "giant": 2.6, "cavalry": 1.7, "catapult": 1.4}.get(kinds[k], 1.1)
            p = rp.P[f, k].astype(float) + np.array([rg.uniform(-0.2, 0.2), hgt, rg.uniform(-0.2, 0.2)])
            if w in ("explosion", "snowball"):
                continue
            if w == "arrow":
                sparks(px, rg, f, p, 2, speed=(3, 6), size=(0.6, 1.0))
            elif w in ("boss_charge", "charge", "brace", "stone", "sweep", "boss", "hammer"):
                px.emit("p_slash", f, 0.16, p, s0=1.4, s1=2.6, rot=(rg.uniform(-30, 30), rg.uniform(0, 360), 0), pop=0.02)
                sparks(px, rg, f, p, 5)
                puffs(px, rg, f, rp.P[f, k].astype(float), 2, dust, size=(0.4, 0.7))
            elif e.get("blk"):
                sparks(px, rg, f, p, 4, speed=(5, 9))
            else:
                px.emit("p_slash", f, 0.13, p, s0=0.8, s1=1.5, rot=(rg.uniform(-30, 30), rg.uniform(0, 360), 0), pop=0.02)
                sparks(px, rg, f, p, 2, speed=(4, 8), size=(0.6, 1.0))
        elif t == "snowball_hit":
            bk = e.get("ball")
            if f - last_ball_hit.get(bk, -99) < 4:
                continue
            last_ball_hit[bk] = f
            v = e.get("v", -1)
            if v in rp.col and rp.present[f, rp.col[v]]:
                p = rp.P[f, rp.col[v]].astype(float) + np.array([0, 0.8, 0])
                puffs(px, rg, f, p, 3, SNOW, size=(0.5, 0.9), speed=3.0)
                chips(px, rg, f, p, 2, (SNOW, "#DCEAF6"), speed=(2, 5), up=(2, 5), size=(1.0, 1.8))
        elif t == "death" and e.get("cause") not in ("fell", "drown", "explode"):
            u = e.get("u", -1)
            if u in rp.col and rp.present[f, rp.col[u]] and rp.P[f, rp.col[u], 1] > -3:
                puffs(px, rg, f, rp.P[f, rp.col[u]].astype(float), 2, dust, size=(0.4, 0.7), life=(0.4, 0.7))
        elif t == "terrain_removed":
            for p_ in rp.scenario.get("terrain", []):
                if p_.get("tag") == e.get("tag") and "min" in p_:
                    mn, mx = np.array(p_["min"], float), np.array(p_["max"], float)
                    for _ in range(26):
                        q = mn + (mx - mn) * rg.uniform(0, 1, 3)
                        chips(px, rg, f + rg.uniform(0, 6), q, 1, (WOOD, WOOD2, "#6B4426"), speed=(0.5, 3.5), up=(0.5, 5.0),
                              size=(1.4, 2.6), life=(1.4, 2.2))
                    for _ in range(8):
                        q = mn + (mx - mn) * rg.uniform(0, 1, 3)
                        puffs(px, rg, f, q, 1, "#D8B88C", size=(0.6, 1.0), life=(0.6, 1.1), up=0.3)
        elif t == "destroyed" and e.get("kind") in ("barrier", "gate", "pillar") and e.get("u") in rp.col:
            k = rp.col[e["u"]]
            p = rp.P[max(0, f - 1), k].astype(float)
            g0 = gy(p[0], p[2])
            if e["kind"] == "pillar":
                for _ in range(16):
                    q = p + np.array([rg.uniform(-0.8, 0.8), rg.uniform(0.2, 2.8), rg.uniform(-0.8, 0.8)])
                    chips(px, rg, f, q, 1, (STONE, STONE2, "#C4CAD4"), speed=(1.5, 6), up=(1, 6), size=(1.6, 3.2), life=(1.2, 1.9),
                          floor=g0)
                puffs(px, rg, f, p + np.array([0, 0.8, 0]), 8, "#C8CCD4", spread=1.0, speed=3.0, size=(0.7, 1.2), life=(0.7, 1.3))
            else:
                q4 = rp.Q[max(0, f - 1), k]
                yaw = 2 * math.atan2(q4[1], q4[3])
                ax = np.array([math.cos(yaw), 0.0, -math.sin(yaw)])
                half = 4.6 if e["kind"] == "barrier" else 3.2
                for _ in range(24):
                    q = p + ax * rg.uniform(-half, half) + np.array([0, rg.uniform(0.2, 1.8), 0])
                    chips(px, rg, f, q, 1, (WOOD, WOOD2, "#6B4426"), speed=(1.5, 6.5), up=(2, 7), size=(1.4, 2.8), life=(1.1, 1.8),
                          floor=g0)
                puffs(px, rg, f, p + np.array([0, 0.6, 0]), 6, dust, spread=2.0, speed=2.5, size=(0.6, 1.0))
        elif t == "boss_stun" and e.get("u") in rp.col:
            k = rp.col[e["u"]]
            hd = np.array([0.0, 0.0, 1.0])
            for e2 in reversed(rp.events[:n_ev + 1]):
                if e2["t"] == "boss_state" and e2.get("state") in (2, 3) and e2.get("u") == e["u"]:
                    hd = np.array([e2.get("dx", 0.0), 0.0, e2.get("dz", 1.0)])
                    break
            p = rp.P[f, k].astype(float) + hd * 2.7
            g0 = gy(p[0], p[2])
            c = p + np.array([0, 1.6, 0])
            px.emit("p_slash", f, 0.2, c, s0=3.0, s1=5.5, rot=(0, rg.uniform(0, 360), 0), pop=0.02)
            if g0 is not None:
                px.emit("p_ring", f, 0.55, (p[0], g0 + 0.08, p[2]), s0=0.4, s1=2.8, color=dust, pop=0.01)
            sparks(px, rg, f, c, 10, speed=(7, 14), life=(0.25, 0.45))
            chips(px, rg, f, c, 10, (STONE, STONE2, "#C4CAD4"), speed=(2, 7), up=(2, 7), size=(1.2, 2.4), floor=g0, v0=-hd * 2.0)
            puffs(px, rg, f, p + np.array([0, 0.4, 0]), 8, dust, spread=1.2, speed=3.5, size=(0.7, 1.3), life=(0.7, 1.2))
    # ---- 每帧连续的：冲锋扬尘、刨地、雪球雪雾、石头拖尾、落地、泥塘溅泥
    mud = getattr(rp, "mud", None)
    mudmap = mud is not None and mud.any()
    for i in range(max(lo, 1), min(hi, NX)):
        pres = rp.present[i]
        st = rp.S[i]
        fl = rp.F[i]
        if mudmap:
            for k in np.where(pres & is_unit & (st == 0) & mud[i] & (rp.speed[i] > 0.6))[0]:
                inm = bool(rp.in_mud[i, k])
                if (i + int(k) * 7) % (3 if inm else 6):
                    continue
                cav = kinds[k] == "cavalry"
                rg = np.random.default_rng(200000 + i * 131 + int(k))
                hd = heading(i, k)
                side = np.array([hd[2], 0.0, -hd[0]]) * rg.uniform(-1, 1) * (0.45 if cav else 0.2)
                p = rp.P[i, k].astype(float) + side - hd * (0.4 if cav else 0.1)
                g0 = gy(p[0], p[2])
                if inm:                                   # 泥水溅起来
                    puffs(px, rg, i, p + np.array([0, 0.1, 0]), 1, "#6E5034", spread=0.3, speed=0.8, size=(0.35, 0.6),
                          life=(0.35, 0.6), up=0.3)
                    chips(px, rg, i, p + np.array([0, 0.15, 0]), 2 if cav else 1, MUDS, spread=0.2, speed=(0.4, 1.6),
                          up=(1.5, 3.2), size=(0.55, 1.0), life=(0.45, 0.75), floor=g0, v0=-hd * 1.0)
                else:                                     # 带着泥走：一路往下掉泥块
                    chips(px, rg, i, p + np.array([0, 0.45 if cav else 0.25, 0]), 1, MUDS, spread=0.15, speed=(0.2, 0.9),
                          up=(0.3, 1.2), size=(0.6, 1.0), life=(0.7, 1.0), floor=g0, v0=-hd * 0.4)
            # 带泥的时间到了：腿上的泥掉下来几块
            for k in np.where(pres & is_unit & (st < 3) & mud[i - 1] & ~mud[i])[0]:
                rg = np.random.default_rng(210000 + i * 131 + int(k))
                p = rp.P[i, k].astype(float)
                cav = kinds[k] == "cavalry"
                chips(px, rg, i, p + np.array([0, 0.5 if cav else 0.3, 0]), 4 if cav else 3, MUDS, spread=0.4 if cav else 0.2,
                      speed=(0.5, 1.8), up=(0.5, 2.0), size=(0.6, 1.1), life=(0.7, 1.0), floor=gy(p[0], p[2]))
        chg = np.where(pres & is_unit & (st == 0) & ((fl & 32) > 0))[0]
        for k in chg:
            boss = kinds[k] == "boss"
            if not boss and (i + k) % 3:
                continue
            rg = np.random.default_rng(900000 + i * 131 + int(k))
            hd = heading(i, k)
            p = rp.P[i, k].astype(float) - hd * (2.2 if boss else 0.8)
            if boss:
                puffs(px, rg, i, p, 2, dust, spread=1.1, speed=1.5, size=(0.8, 1.4), life=(0.6, 1.1), v0=-hd * 1.5)
                if i % 3 == 0:
                    chips(px, rg, i, p, 1, (dust, "#8A7A5E"), speed=(1, 3), up=(2, 5), size=(0.8, 1.4), life=(0.6, 0.9), v0=-hd * 3)
            else:
                puffs(px, rg, i, p, 1, dust, spread=0.4, speed=1.0, size=(0.45, 0.8), life=(0.45, 0.8), v0=-hd * 1.0)
        for k in np.where(pres & is_unit & (rp.boss_state[i] == 1) & (st == 0))[0]:
            ph = (i * fdt * 2.6) % 1.0
            if 0.62 < ph < 0.92 and i % 2 == 0:
                rg = np.random.default_rng(800000 + i * 131 + int(k))
                qd = rp.Q[i, k]
                yaw = 2 * math.atan2(qd[1], qd[3])
                hd = np.array([math.sin(yaw), 0.0, math.cos(yaw)])
                p = rp.P[i, k].astype(float) + hd * 0.6
                puffs(px, rg, i, p, 2, dust, spread=0.6, speed=1.0, size=(0.6, 1.0), life=(0.5, 0.9), v0=-hd * 4.0)
                chips(px, rg, i, p, 2, (dust, "#8A7A5E", "#6E8A4A"), speed=(0.5, 2), up=(2, 5), size=(0.8, 1.5), life=(0.6, 1.0),
                      v0=-hd * 5.0)
        if i % 2 == 0:
            for k in np.where(pres & (rp.OT == 3) & (st == 1) & (rp.speed[i] > 1.0))[0]:
                if kinds[k] != "snowball":
                    continue
                rg = np.random.default_rng(700000 + i * 131 + int(k))
                hd = heading(i, k)
                p = rp.P[i, k].astype(float) + np.array([0, -1.4, 0]) - hd * 0.8
                puffs(px, rg, i, p, 2, SNOW, spread=0.9, speed=1.2, size=(0.5, 1.0), life=(0.5, 0.9), v0=-hd * 0.5, up=1.2)
                if i % 4 == 0:
                    chips(px, rg, i, p, 1, (SNOW, "#DCEAF6"), speed=(1, 3), up=(2, 4), size=(0.8, 1.4), life=(0.5, 0.8))
            for k in np.where(pres & (rp.OT == 2) & (st == 0) & (rp.speed[i] > 4.0))[0]:
                rg = np.random.default_rng(600000 + i * 131 + int(k))
                px.emit("p_puff", i, 0.45, rp.P[i, k], s0=0.75, s1=0.3, color="#C8C4BC", rot=_rot(rg), pop=0.01)
        # 石头落地（往下的速度突然没了）
        for k in np.where(pres & rp.present[i - 1] & (rp.OT == 2))[0]:
            if i < 2 or not rp.present[i - 2, k]:
                continue
            v0 = (rp.P[i - 1, k, 1] - rp.P[i - 2, k, 1]) / fdt
            v1 = (rp.P[i, k, 1] - rp.P[i - 1, k, 1]) / fdt
            if v0 < -4.0 and v1 > v0 + 5.0:
                rg = np.random.default_rng(500000 + i * 131 + int(k))
                p = rp.P[i, k].astype(float)
                g0 = gy(p[0], p[2])
                base = np.array([p[0], g0 if g0 is not None and abs(g0 - p[1]) < 1.5 else p[1] - 0.4, p[2]])
                big = min(1.0, -v0 / 12.0)
                px.emit("p_ring", i, 0.45, base + np.array([0, 0.08, 0]), s0=0.3, s1=1.2 + 0.8 * big, color=dust, pop=0.01)
                puffs(px, rg, i, base, int(3 + 4 * big), dust, spread=0.7, speed=3.0, size=(0.5, 1.0))
                chips(px, rg, i, base, int(2 + 4 * big), (STONE2, "#8C8A86", dust), speed=(2, 5), up=(2, 6), floor=base[1])
        # 被打飞的人落地
        land = np.where(pres & is_unit & ((rp.F[i - 1] & 128) > 0) & ((fl & 128) == 0) & (st > 0) & (rp.P[i, :, 1] > -2.5))[0]
        for k in land[:6]:
            rg = np.random.default_rng(400000 + i * 131 + int(k))
            puffs(px, rg, i, rp.P[i, k].astype(float), 2, dust, size=(0.4, 0.75), life=(0.4, 0.7))
    # ---- 消失：雪球碎成雪块化掉；尸体沉下去时带一点土；石头缩没时一小团灰
    for k in range(K):
        idx = np.where(rp.present[:N, k])[0]
        if len(idx) == 0:
            continue
        last = int(idx[-1])
        if last >= N - 1 or last < lo or last > hi:
            continue
        rg = np.random.default_rng(300000 + k)
        p = rp.P[last, k].astype(float)
        if kinds[k] == "snowball":
            g0 = p[1] - 1.5
            for _ in range(16):
                d = _dir(rg, -0.6, 0.9)
                q = p + d * rg.uniform(0.2, 1.2)
                v = np.array([d[0], 0, d[2]]) * rg.uniform(0.8, 3.0) + np.array([0, rg.uniform(0.5, 3.0), 0])
                s = rg.uniform(1.4, 2.6)
                px.emit("p_lump", last + 1, rg.uniform(1.6, 2.6), q, v, g=12.0, drag=0.0, s0=s, s1=s * 0.8, rot=_rot(rg),
                        spin=_spin(rg, 120), floor=g0 + 0.2, pop=0.01)
            puffs(px, rg, last + 1, p - np.array([0, 0.8, 0]), 8, SNOW, spread=1.2, speed=2.5, size=(0.7, 1.2), life=(0.7, 1.3))
        elif is_unit[k] and rp.S[last, k] == 3 and p[1] > -3:
            puffs(px, rg, last + 4, p, 3, dust, spread=0.5, speed=0.6, size=(0.4, 0.7), life=(0.6, 1.0), up=0.4)
        elif rp.OT[k] == 2:
            puffs(px, rg, last + 1, p, 3, "#C8C4BC", spread=0.3, speed=0.8, size=(0.4, 0.7), life=(0.4, 0.7))
