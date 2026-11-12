# -*- coding: utf-8 -*-
"""把一张地图搭成 Blender 场景：地形、场外布景、道具、守军；再按"场面"往上摆兵、特效、机位。
静帧（第一阶段的假想战场）和回放出片（第二阶段）都用它。"""
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Euler, Quaternion, Vector

import army as AR
import builders as BLD
import looks as LK
import mats as MT
import scene_lib as SL
import stage as STG
import terrain as TR
from geom import MeshBuilder

DEG = math.pi / 180.0
HERE = os.path.dirname(os.path.abspath(__file__))
LEVEL_DIR = os.path.join(HERE, "..", "themes")
# 正式的关卡和对抗图：默认是本仓库的选手包 battle/workspace（levels/、maps/）。别处的用环境变量 A12_ENGINE 指过去。
ENGINE = os.environ.get("A12_ENGINE") or os.path.join(HERE, "..", "..", "workspace")


def find_map(name):
    """地图名（"一线天"、"第4关_一线天"、"樱丘"）-> 引擎的正式文件。渲染器自己只放附加布景（关卡\\<名字>.render.json）。"""
    import glob
    import re
    key = re.sub(r"^第\d+关_", "", name)
    for p in [os.path.join(ENGINE, "maps", key + ".json")] + glob.glob(os.path.join(ENGINE, "levels", "*_" + key + ".json")) + glob.glob(os.path.join(ENGINE, "关卡", "*_" + key + ".json")):
        if os.path.exists(p):
            return p
    raise FileNotFoundError(name)


def expand_units(lst, default_team=1):
    """引擎的 units 列表（含 group 方阵）-> 一个个单位 dict。方阵的算法和 scenario.cpp 一致。"""
    out = []
    for u in lst:
        if "group" in u:
            g = u["group"]
            rows, cols = int(g.get("rows", 1)), int(g.get("cols", 1))
            dx, dz = float(g.get("dx", 1.0)), float(g.get("dz", 1.0))
            yaw = float(g.get("yaw", 0)) * DEG
            c, s = math.cos(yaw), math.sin(yaw)
            for r in range(rows):
                for k in range(cols):
                    lx, lz = (k - (cols - 1) * 0.5) * dx, -r * dz
                    d = dict(g)
                    d["x"] = g.get("x", 0) + lx * c + lz * s
                    d["z"] = g.get("z", 0) - lx * s + lz * c
                    d.setdefault("team", default_team)
                    out.append(d)
        else:
            d = dict(u)
            d.setdefault("team", default_team)
            out.append(d)
    return out


KIND_ID = {"新兵": "militia", "盾卫": "shield", "长枪兵": "spear", "枪兵": "spear", "弓手": "archer", "骑兵": "cavalry",
           "大锤": "hammer", "炮仗兵": "bomber", "炮仗": "bomber", "投石车": "catapult", "大块头": "giant", "镇山兽": "boss",
           "主帅": "lord"}


def kind_id(t):
    return KIND_ID.get(t, t)


class Battle:
    def __init__(self, map_name, samples=24, decor=True, fast=True, tide_t=50.0):
        SL.clear_scene()
        SL._MATS.clear()
        AR.KIT = None
        if isinstance(map_name, dict):
            self.mp = TR.prepare(map_name)
        else:
            self.mp = TR.load_map(find_map(map_name)) if not map_name.endswith(".json") else TR.load_map(map_name)
        self.L = L = LK.for_map(self.mp)
        for (ix, iz, ir) in self.mp.get("render", {}).get("islets", []):
            self.mp["surround"].append(dict(type="mound", c=[ix, iz], r0=ir, r1=ir * 0.35, y0=-6, y1=4.5, mat="sand"))
            self.mp["render"].setdefault("decor", []).append(dict(type="trees", style="palm", region=[ix - ir * 0.4, iz - ir * 0.4, ix + ir * 0.4, iz + ir * 0.4], n=4, seed=abs(int(ix)) + 1, min_y=3.0))
        STG.setup_render(samples, png=True, fast=fast)
        SL.PAL["sky"], SL.PAL["sky_deep"] = L["sky"]
        SL.PAL["ambient"] = L["ambient"]
        SL.setup_world(strength=L["amb_strength"])
        sn = L["sun"]
        self.sun = STG.sun(az=sn["az"], el=sn["el"], energy=sn["energy"], color=sn["color"])
        self.coll = bpy.context.scene.collection
        self.terrain = TR.build(self.mp, L, self.coll)
        self.decor_b = MeshBuilder()
        if decor:
            for spec in self.mp.get("render", {}).get("decor", []):
                BLD.scatter(self.decor_b, self.mp, L, spec, self.h)
        self._water(tide_t)
        self._zones()
        self._extras()
        self.units = []
        self.props = []
        self.fx = []
        self._n = 0

    # ------------------------------------------------------------ 地形高度
    def h(self, x, z):
        return TR.height_at(self.mp, x, z)

    def hh(self, x, z, default=0.0):
        v = self.h(x, z)
        return default if v is None else v

    # ------------------------------------------------------------ 水面（运河、潮水）
    def _water(self, tide_t):
        w = self.mp.get("water")
        if not w:
            return
        if "tide" in w:
            t = w["tide"]
            a = min(max((tide_t - t["t0"]) / (t["t1"] - t["t0"]), 0.0), 1.0)
            level = t["h0"] + (t["h1"] - t["h0"]) * a
        else:
            level = w.get("level", 0.0)
        x0, z0, x1, z1 = self.mp.get("bounds", [-50, -50, 50, 50])
        pad = 140
        b = MeshBuilder()
        b.add(np.array([(x0 - pad, z0 - pad, 0.0), (x1 + pad, z0 - pad, 0.0), (x1 + pad, z1 + pad, 0.0),
                        (x0 - pad, z1 + pad, 0.0)]), [(0, 1, 2, 3)], mat=7, smooth=False)
        self.water_ob = b.build("water", MT.all_mats(self.L), self.coll)
        self.tide = w.get("tide")
        self.set_water(level)

    def set_water(self, level):
        """把水面放到 level 这个高度；岸线跟着走（湿沙带、浪花、浅水圈都按它算）。"""
        self.water_level = level
        self.water_ob.location.z = level
        mats = MT.all_mats(self.L)
        nd = mats[0].node_tree.nodes.get("tide_level")
        if nd is not None:
            nd.outputs[0].default_value = level
        nt = mats[7].node_tree.nodes
        if nt.get("shore_r") is not None:
            best = None
            for p in self.mp.get("prims", []):
                if p.get("type") == "mound" and p.get("y0", 0) <= level <= p.get("y1", 0) + 1e-6:
                    r0, r1 = p.get("r0", p.get("r", 1)), p.get("r1", p.get("r", 1))
                    f = (level - p["y0"]) / max(p["y1"] - p["y0"], 1e-6)
                    best = (r0 + (r1 - r0) * f, p["c"][0], p["c"][1])
            if best:
                nt["shore_r"].outputs[0].default_value = best[0]
                nt["shore_cx"].outputs[0].default_value = best[1]
                nt["shore_cy"].outputs[0].default_value = best[2]

    def set_tide(self, t):
        """涨潮的图：第 t 秒的水位。"""
        td = getattr(self, "tide", None)
        if not td:
            return
        a = min(max((t - td["t0"]) / (td["t1"] - td["t0"]), 0.0), 1.0)
        self.set_water(td["h0"] + (td["h1"] - td["h0"]) * a)

    # ------------------------------------------------------------ 区域（泥塘）和渲染器自己的小物
    def _zones(self):
        L = self.L
        for zn in self.mp.get("zones", []):
            if zn.get("speed_mul", 1.0) >= 1.0:
                continue
            if "ellipse" in zn:
                cx, cz, rx, rz = zn["ellipse"]
            elif "circle" in zn:
                cx, cz, rx = zn["circle"]
                rz = rx
            else:
                x0, z0, x1, z1 = zn["rect"]
                cx, cz, rx, rz = (x0 + x1) / 2, (z0 + z1) / 2, (x1 - x0) / 2, (z1 - z0) / 2
            seg = 64
            V = [(cx, cz, self.hh(cx, cz) + 0.03)]
            rg = np.random.default_rng(3)
            sq = 0.5 if "rect" in zn else 1.0       # 矩形的区域画成圆角矩形（基本铺满），不是内接椭圆：慢的地方都看得见是泥
            rim, R1 = [], []
            for k in range(seg):
                a = 2 * math.pi * k / seg
                wob = 1.0 + 0.035 * math.sin(5 * a) + rg.uniform(-0.02, 0.02)
                ca, sa = math.cos(a), math.sin(a)
                ux = math.copysign(abs(ca) ** sq, ca)
                uz = math.copysign(abs(sa) ** sq, sa)
                x, z = cx + rx * wob * ux, cz + rz * wob * uz
                V.append((x, z, self.hh(x, z) + 0.03))
                x2, z2 = cx + (rx * wob + 1.5) * ux, cz + (rz * wob + 1.5) * uz
                rim.append((x, z, self.hh(x, z) + 0.022))
                R1.append((x2, z2, self.hh(x2, z2) + 0.022))
            F = [(0, k + 1, (k + 1) % seg + 1) for k in range(seg)]
            b = MeshBuilder()
            b.add(np.array(V), F, mat=9, smooth=False)
            b.build("mud", MT.all_mats(L), self.coll)
            # 泥塘外面一圈踩烂的泥地，往外淡进草里（土路材质，顶点色 = 浓度）
            Vr = rim + R1
            Cr = [(1.0, 1.0, 1.0, 1.0)] * seg + [(0.0, 0.0, 0.0, 1.0)] * seg
            Fr = [(k, (k + 1) % seg, seg + (k + 1) % seg, seg + k) for k in range(seg)]
            b2 = MeshBuilder()
            b2.add(np.array(Vr), Fr, mat=10, smooth=False, vcolors=np.array(Cr))
            b2.build("mud_rim", MT.all_mats(L), self.coll)
            # 泥塘边上一圈小水洼和芦苇
            for k in range(14):
                a = rg.uniform(0, 2 * math.pi)
                ca, sa = math.cos(a), math.sin(a)
                x = cx + rx * 1.03 * math.copysign(abs(ca) ** sq, ca)
                z = cz + rz * 1.03 * math.copysign(abs(sa) ** sq, sa)
                BLD.reeds(self.decor_b, x, z, self.hh(x, z), rg)

    def _extras(self):
        rd = self.mp.get("render", {})
        rg2 = np.random.default_rng(9)
        for (rx, rz) in rd.get("ruins", []):
            BLD.ruin(self.decor_b, rx, rz, self.hh(rx, rz), self.L, rg2)
        for pth in rd.get("paths", []):
            self._path(pth["pts"], pth.get("w", 3.0), pth.get("seed", 1))
        # 营地布景（拔炮台的炮兵阵地）：{"t": ammo|crate|tent|flag|stones|rack|fence, "x", "z", "a"（度）, ...}
        rg4 = np.random.default_rng(41)
        for it in rd.get("camp", []):
            t = it["t"]
            if t == "fence":
                x0, z0, x1, z1 = it["pts"]
                BLD.fence(self.decor_b, x0, z0, x1, z1, self.hh((x0 + x1) / 2, (z0 + z1) / 2), rg4)
                continue
            kw = {k: v for k, v in it.items() if k not in ("t", "x", "z", "a")}
            if "a" in it:
                kw["a"] = it["a"] * DEG
            BLD.CAMP[t](self.decor_b, it["x"], it["z"], self.hh(it["x"], it["z"]), rg4, **kw)
        # 场外海里的礁柱、搁浅的破船（只在镜头拍到岛边时入画，当前景）
        rg3 = np.random.default_rng(21)
        for (sx, sz, sh, ss) in rd.get("sea_stacks", []):
            BLD.sea_stack(self.decor_b, sx, sz, -0.6, sh, ss, self.L, rg3)
        for (wx, wz, wa) in rd.get("wrecks", []):
            BLD.shipwreck(self.decor_b, wx, wz, -0.4, wa * DEG, self.L, rg3)
        # 灯塔的灯室：一块发光的暖黄方块（terrain._lighthouse 记下的位置）
        lamp = self.mp.get("_lamp")
        if lamp:
            cx, cz, zl, zr = lamp
            m = bpy.data.materials.new("M_lamp")
            nb = SL.NB(m)
            em = nb.node("ShaderNodeEmission")
            em.inputs['Color'].default_value = SL.hexc("#FFD36A")
            em.inputs['Strength'].default_value = 3.0
            nb.output(em)
            b = MeshBuilder()
            BLD.vbox(b, cx - 0.42, cx + 0.42, cz - 0.42, cz + 0.42, zl + 0.08, zr - 0.04, "#FFFFFF", mat=0)
            b.build("lamp", [m], self.coll)
        # 雪球的挡木：横在球和崖边之间的一根木头
        for p in self.mp.get("props", []):
            if p.get("type") in ("snowball", "雪球"):
                a = p.get("yaw", 0) * DEG
                dx, dz = math.sin(a), math.cos(a)
                r = p.get("r", 1.5)
                cx, cz = p["x"] + dx * (r + 0.25), p["z"] + dz * (r + 0.25)
                y = self.hh(cx, cz)
                BLD.vbox(self.decor_b, cx - 0.95, cx + 0.95, cz - 0.16, cz + 0.16, y, y + 0.34, "#8A5A34", top="#C08A52",
                         rz=-a + math.pi / 2 - math.pi / 2, about=(cx, cz))
                for s in (-1, 1):
                    px, pz = cx + s * 0.85 * math.cos(a), cz - s * 0.85 * math.sin(a)
                    BLD.vbox(self.decor_b, px - 0.1, px + 0.1, pz - 0.1, pz + 0.1, y - 0.3, y + 0.6, "#6B4426")

    def _path(self, pts, w, seed):
        """一条踩出来的土路：沿折线铺一条贴地的带子（宽度有一点起伏），材质 10。"""
        rg = np.random.default_rng(seed)
        P = []
        for i in range(len(pts) - 1):
            a, b = np.array(pts[i], float), np.array(pts[i + 1], float)
            n = max(2, int(np.linalg.norm(b - a) / 1.0))
            for k in range(n):
                P.append(a + (b - a) * k / n)
        P.append(np.array(pts[-1], float))
        V, F, C = [], [], []
        F2 = []                                    # 走在台地上的那几段（底色是台地的干草色，不是草地）
        plat = [q for q in self.mp.get("prims", []) if MT.kind_of(q.get("mat", "")) == "plateau" and q.get("type") == "box"]

        def on_plateau(x, z):
            for q in plat:
                if abs(x - q["c"][0]) <= q["h"][0] and abs(z - q["c"][2]) <= q["h"][2]:
                    y = self.h(x, z)
                    if y is not None and abs(y - (q["c"][1] + q["h"][1])) < 0.3:
                        return True
            return False

        prof = ((-1.0, 0.0), (-0.55, 0.85), (0.0, 1.0), (0.55, 0.85), (1.0, 0.0))
        for i, p in enumerate(P):
            d = P[min(i + 1, len(P) - 1)] - P[max(i - 1, 0)]
            d = d / (np.linalg.norm(d) + 1e-9)
            nrm = np.array([-d[1], d[0]])
            ww = w / 2 * (1 + 0.18 * math.sin(i * 0.7 + seed) + rg.uniform(-0.08, 0.08))
            for (s_, f) in prof:
                q = p + nrm * ww * s_ * 1.3
                y = self.h(q[0], q[1])
                V.append((q[0], q[1], (y if y is not None else -50) + 0.025))
                C.append((f, f, f, 1.0))
            if i:
                j = 5 * i
                dst = F2 if on_plateau(p[0], p[1]) else F
                for k in range(4):
                    dst.append((j - 5 + k, j - 5 + k + 1, j + k + 1, j + k))
        b = MeshBuilder()
        if F:
            b.add(np.array(V), F, mat=10, smooth=False, vcolors=np.array(C))
        if F2:
            b.add(np.array(V), F2, mat=12, smooth=False, vcolors=np.array(C))
        b.build("path", MT.all_mats(self.L), self.coll)

    def finish_decor(self):
        if self.decor_b.V:
            self.decor_b.build("decor", MT.all_mats(self.L), self.coll)
            self.decor_b = MeshBuilder()

    # ------------------------------------------------------------ 单位
    def add(self, kind, team, x, z, yaw=0.0, pose="stand", t=0.0, y=None, air=None, scale=1.0, alive=True, ring=True):
        """air = (离地多高, 绕前后轴转角, 绕左右轴转角, 绕竖轴转角)（度）：被打飞在半空的人。"""
        self._n += 1
        kind = kind_id(kind)
        if y is None:
            y = self.hh(x, z)
        u = AR.Unit(kind, team, self.coll, "u%03d_%s" % (self._n, kind), ring=ring)
        u.place(x, z, y, yaw_deg=yaw, scale=scale)
        if air:
            lift, rx, ry = air[0], air[1], air[2]
            rz = air[3] if len(air) > 3 else 0.0
            u.root.location = (x, z, y + lift)
            q = u.root.rotation_quaternion.copy()
            u.root.rotation_quaternion = q @ Euler((rx * DEG, ry * DEG, rz * DEG)).to_quaternion()
            if u.ring:
                u.ring.hide_render = True
        u.pose(pose, t)
        if not alive:
            u.set_alive(False)
        self.units.append(u)
        return u

    def add_lord(self, key, team, x, z, yaw=0.0, pose="stand"):
        u = self.add("lord:" + key, team, x, z, yaw=yaw, pose=pose, scale=1.7)
        return u

    def place_props(self, skip=()):
        out = []
        for i, p in enumerate(self.mp.get("props", [])):
            t = {"雪球": "snowball", "火药桶": "keg", "军旗": "flag", "木栅": "barrier", "木栅门": "gate", "立石": "pillar"}.get(
                p["type"], p["type"])
            if i in skip or p.get("tag") in skip:
                continue
            x, z = p["x"], p["z"]
            if t == "snowball":
                y = p.get("y", self.hh(x, z) + p.get("r", 1.5))
                if y < 0:
                    y = self.hh(x, z) + p.get("r", 1.5)
            else:
                y = p.get("y", -1)
                if y is None or y < 0:
                    y = self.hh(x, z)
            team = p.get("team", 0 if t == "flag" else -1)
            u = AR.Unit(t, team if t == "flag" else None, self.coll, "prop%02d_%s" % (i, t), ring=False)
            u.place(x, z, y, yaw_deg=p.get("yaw", 0) if t != "snowball" else 0.0)
            out.append((p, u))
        self.props = out
        return out

    def place_host(self, skip=None, edit=None):
        """关卡里的守军照原样摆上（原地守的长枪兵摆成架枪）。edit(i, d) 可以改某一个（返回 None = 不摆）。"""
        lst = expand_units(self.mp.get("units", []))
        for i, d in enumerate(lst):
            if edit:
                d = edit(i, dict(d))
                if d is None:
                    continue
            k = kind_id(d["type"])
            pose = d.get("pose") or ("brace" if (k == "spear" and d.get("behavior") == "hold") else "stand")
            self.add(k, d.get("team", 1), d["x"], d["z"], yaw=d.get("yaw", 0), pose=pose, t=d.get("t", 0.0),
                     air=d.get("air"), alive=d.get("alive", True))
        return lst

    # ------------------------------------------------------------ 特效
    def add_fx(self, kind, x, y, z, scale=1.0, rot=(0, 0, 0)):
        self._n += 1
        u = AR.Unit(kind, None, self.coll, "fx%03d_%s" % (self._n, kind), ring=False)
        u.root.location = (x, z, y)
        u.root.rotation_mode = 'XYZ'
        u.root.rotation_euler = tuple(r * DEG for r in rot)
        u.root.scale = (scale, scale, scale)
        self.fx.append(u)
        return u

    def arrow(self, p0, p1, t, arc=None):
        """一支在飞的箭：从 p0 射向 p1（引擎坐标 x, y, z），t = 飞了几成。抛物线，箭头顺着速度方向。"""
        x0, y0, z0 = p0
        x1, y1, z1 = p1
        D = math.hypot(x1 - x0, z1 - z0)
        arc = arc if arc is not None else D * 0.18
        x = x0 + (x1 - x0) * t
        z = z0 + (z1 - z0) * t
        y = y0 + (y1 - y0) * t + 4 * arc * t * (1 - t)
        vx, vz = (x1 - x0), (z1 - z0)
        vy = (y1 - y0) + 4 * arc * (1 - 2 * t)
        u = self.add_fx("arrow", x, y, z, scale=1.5)
        fwd = Vector((vx, vz, vy)).normalized()
        u.root.rotation_mode = 'QUATERNION'
        u.root.rotation_quaternion = (-fwd).to_track_quat('Y', 'Z')       # 箭的网格朝 -Y
        return u

    def boom(self, x, z, y=None, age=0.1):
        import fx as FXM
        y = self.hh(x, z) if y is None else y
        fs, fv, ss, sv = FXM.boom_state(age)
        if fv:
            self.add_fx("boom", x, y + 0.9, z, scale=fs)
        if sv:
            self.add_fx("smoke", x, y + 0.6, z, scale=ss)
        self.add_fx("dust", x, y, z, scale=1.6)

    # ------------------------------------------------------------ 机位、出图
    def shot(self, path, target, pitch=52.0, yaw=0.0, width=None, dist=None, lens=50.0):
        self.finish_decor()
        d = dist if dist is not None else STG.fit_dist(width or 50.0, lens)
        tx, tz, ty = target[0], target[1], target[2] if len(target) > 2 else self.hh(target[0], target[1])
        STG.camera((tx, tz, ty), pitch=pitch, yaw=yaw, dist=d, lens=lens)
        STG.render_to(path)
