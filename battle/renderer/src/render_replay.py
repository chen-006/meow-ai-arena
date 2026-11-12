# -*- coding: utf-8 -*-
"""Blender 入口：一份回放 -> 一段画面帧（不含信息栏，信息栏由 make_video.py 后期叠上）。
    blender.exe -b --factory-startup -P render_replay.py -- <回放.wrp> --out <目录> [--frames a:b] [--samples 12]
        [--lord default|<名字>|<造型.json>] [--pitch 40] [--shots 机位表.json] [--no-close] [--still 帧号] [--png]
        [--cam x,z,y,宽度,俯角,方位] [--name 文件名] [--no-fx] [--no-shake]
已经渲好的帧会跳过（断点续渲）；--frames 用来分段并行。

每一帧怎么摆（引擎给的是刚体的位置和朝向，照着摆）：
  * 单位：位置、四元数直接用（倒地翻滚、被撞飞、尸体都在里面）；肢体动作按状态补：走路（按走过的路程摆腿）、
    起手（aux 进度）+ 出手落下后的收势、长枪架枪、倒地 / 腾空时手脚甩开、阵亡压暗；
  * 镇山兽：按 boss_state 事件换刨地、冲撞、晕；
  * 箭、石头、雪球、火药桶、木栅、门、立石、军旗：位置朝向照摆；飞行中的箭带拖尾；
  * 从回放里消失的东西不凭空闪没：尸体 1.2 秒沉进地面，雪球碎成雪块化掉，石头、箭缩没；
  * 特效全部是粒子（fxsys.py）：爆炸、命中、扬尘、雪雾、碎木碎石……；
  * 桥断：terrain_removed 事件后把那段桥藏起来，换成两截往下折、往下掉的断桥；挂在桥身外沿的火药桶各有一根短吊绳，
    桥塌时跟着桶一起往下掉；
  * 第 1 关脚上带泥的单位（replay.mud）：腿的下半截、马披下沿换成糊了泥的网格；
  * 镜头：replay.director() 算好的机位（全景跟拍 + 按事件设计的近景 + 手动机位表），每帧摆；大的撞击带一下镜头震动。
"""
import math
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Quaternion, Vector  # noqa: E402

import army as AR  # noqa: E402
import fxsys as FXS  # noqa: E402
import replay as RP  # noqa: E402
import scene as SC  # noqa: E402
import stage as STG  # noqa: E402

DEG = math.pi / 180.0
SINK_S = 1.2                                  # 尸体沉进地面用几秒
HIT_S = float(os.environ.get("A12_HIT_S", "0.2"))                                  # 受击闪亮持续几秒
SINK_DEPTH = dict(cavalry=1.7, giant=2.6, boss=3.8, catapult=3.2)
FLAGS = ("png", "no-close", "no-fx", "no-shake")


def parse_args():
    a = sys.argv[sys.argv.index("--") + 1:]
    opt = dict(replay=a[0], out=None, frames=None, samples=12, lord="default", pitch=None, still=None, png=False, shots=None,
               no_close=False, cam=None, name=None, no_fx=False, no_shake=False)
    i = 1
    while i < len(a):
        k = a[i].lstrip("-")
        if k in FLAGS:
            opt[k.replace("-", "_")] = True
            i += 1
            continue
        opt[k] = a[i + 1]
        i += 2
    return opt


def _smooth(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3 - 2 * x)


class Player:
    def __init__(self, rp, opt, a=0, b=None):
        self.rp = rp
        self.opt = opt
        self.B = B = SC.Battle(rp.scenario, samples=int(opt["samples"]))
        self.hang = self._hangers()
        B.finish_decor()
        self.has_mud = bool(getattr(rp, "mud", None) is not None and rp.mud.any())
        self.fdt = rp.dt * rp.rec_every
        self.objs = {}
        self.gone = {}                        # 物体列号 -> (最后在场的帧, 消失方式, 用几帧)
        N = rp.n
        for i in rp.ids:
            inf = rp.info[i]
            typ, kind = inf["type"], inf.get("kind")
            team = inf.get("team", -1)
            k = rp.col[i]
            if typ == "unit":
                kd = kind
                if kind == "lord" and opt["lord"] not in ("", "default", None):
                    kd = "lord:" + opt["lord"]
                u = AR.Unit(kd, team, B.coll, "r%04d_%s" % (i, kind), ring=kind not in ("boss",))
                if kind == "lord":
                    u.scale = 1.7 if u.meta.get("figure") else 1.3
            elif typ == "prop":
                u = AR.Unit(kind, team if kind == "flag" else None, B.coll, "r%04d_%s" % (i, kind), ring=False)
            elif typ == "arrow":
                u = AR.Unit("arrow", None, B.coll, "r%04d_arrow" % i, ring=False)
                u.scale = 1.8
            else:
                u = AR.Unit("stone", None, B.coll, "r%04d_stone" % i, ring=False)
            u.set_visible(False)
            u.visible = False
            u.alive = True
            self.objs[i] = u
            idx = np.where(rp.present[:N, k])[0]
            if len(idx) and idx[-1] < N - 1:
                last = int(idx[-1])
                if typ == "unit" and rp.S[last, k] == 3:
                    self.gone[k] = (last, "sink", int(SINK_S / self.fdt))
                elif typ in ("arrow", "stone"):
                    self.gone[k] = (last, "shrink", 8 if typ == "arrow" else 10)
        self._bridges()
        self.px = FXS.Particles(B.coll, self.fdt)
        if not opt.get("no_fx"):
            FXS.scan(rp, self.px, B.L, B.h, a, rp.nx if b is None else b)
        self.n_px = self.px.build()
        # 机位：地图的 render.json 里 "camera" 段给默认俯角、方位、全景宽度；--pitch 覆盖；--shots 手动机位表
        cfg = dict(B.mp.get("render", {}).get("camera", {}))
        if opt.get("pitch") is not None:
            cfg["pitch"] = float(opt["pitch"])
        shots = None
        if opt.get("shots"):
            import json
            shots = json.load(open(opt["shots"], encoding="utf-8"))
            if isinstance(shots, dict):
                cfg.update(shots.get("auto", {}))
                shots = shots.get("shots", [])
        if opt.get("no_close"):
            cfg["auto_close"] = False
        self.cam = RP.director(rp, shots=shots, **cfg)
        self.shk = [] if opt.get("no_shake") else RP.shakes(rp)

    # ------------------------------------------------------------ 挂在桥边的火药桶：一根短绳把桶系在桥身外沿
    def _hangers(self):
        """第 2 关桥中段两侧挂着的火药桶（关卡里 y < 0、带 breaks 的桶）：每只桶一根吊绳 + 搭在桥面上的一截绳和一个木栓。
        吊绳跟着桶走（桥塌时桶跟着往下掉，绳也一起掉；桶炸了绳也没了）。返回 {桶的物体列号: 吊绳物体}。"""
        import builders as BLD
        import mats as MT
        from geom import MeshBuilder
        rp, B = self.rp, self.B
        out = {}
        br = [p for p in B.mp.get("prims", []) if "bridge" in str(p.get("mat", ""))]
        kegs = [p for p in B.mp.get("props", []) if p.get("breaks") and p.get("y") is not None and p.get("y") < 0]
        if not br or not kegs:
            return out
        top = max(p["c"][1] + p["h"][1] for p in br)
        rope, peg = "#6B4A2E", "#8A5A34"
        for p in kegs:
            # 回放里对应的那只桶：开场位置最近的 keg
            cand = [i for i in rp.ids if rp.info[i].get("kind") == "keg" and "pos" in rp.info[i]]
            if not cand:
                continue
            oid = min(cand, key=lambda i: (rp.info[i]["pos"][0] - p["x"]) ** 2 + (rp.info[i]["pos"][2] - p["z"]) ** 2)
            # 桥身在 z 方向的边：桶在哪一侧就系在哪一侧
            edge = None
            for q in br:
                if abs(p["x"] - q["c"][0]) <= q["h"][0] + 0.01:
                    edge = q["c"][2] + math.copysign(q["h"][2], p["z"] - q["c"][2])
            if edge is None:
                continue
            ky = p["y"]
            dz = edge - p["z"]                             # 桶心到桥边（桶在桥外，往桥里方向）
            sg = math.copysign(1.0, dz)
            h1 = top + 0.10 - ky                           # 绳子走到桥面上方一点
            b = MeshBuilder()
            BLD.vbox(b, -0.05, 0.05, -0.05, 0.05, 0.90, h1 + 0.05, rope)                         # 竖着的吊绳
            z0, z1 = sorted((0.0, dz + sg * 0.35))
            BLD.vbox(b, -0.05, 0.05, z0, z1, h1 - 0.03, h1 + 0.06, rope)                          # 搭过桥边
            zp = dz + sg * 0.35
            BLD.vbox(b, -0.13, 0.13, zp - 0.13 * sg - 0.13, zp - 0.13 * sg + 0.13, h1 - 0.12, h1 + 0.12, peg, top="#C08A52")
            BLD.vbox(b, -0.26, 0.26, -0.04, 0.04, 0.86, 0.96, rope)                              # 桶口上的绳箍
            o = b.build("hang_%d" % oid, MT.all_mats(B.L), B.coll)
            o.hide_render = True
            out[rp.col[oid]] = o
        return out

    # ------------------------------------------------------------ 桥断：两截断桥（平时藏着）
    def _bridges(self):
        rp, B = self.rp, self.B
        self.spans = []
        for e, f in zip(rp.events, rp.ev_frame):
            if e["t"] != "terrain_removed":
                continue
            tag = e["tag"]
            ob = B.terrain.get(("tag", tag))
            prim = [p for p in B.mp["prims"] if p.get("tag") == tag]
            if not prim:
                continue
            p = prim[0]
            c, h = p["c"], p["h"]
            along_x = h[0] >= h[2]
            L = h[0] if along_x else h[2]
            halves = []
            for sgn in (-1, 1):
                hinge = c[0] + sgn * L if along_x else c[2] + sgn * L
                o = self._half_span(p, sgn, along_x)
                o.hide_render = True
                halves.append((o, sgn, hinge, o.location.z))
            self.spans.append((f, ob, halves, along_x))

    def _half_span(self, p, sgn, along_x):
        """桥断后的半截：以靠岸的一头为轴（物体原点放在轴上），之后只转、只掉。"""
        import builders as BLD
        import mats as MT
        from geom import MeshBuilder
        c, h = p["c"], p["h"]
        L = h[0] if along_x else h[2]
        b = MeshBuilder()
        half = dict(type="box", c=list(c), h=list(h), yaw=0.0)
        if along_x:
            half["c"] = [c[0] + sgn * L / 2, c[1], c[2]]
            half["h"] = [L / 2 - 0.05, h[1], h[2]]
            piv = np.array([c[0] + sgn * L, c[2], c[1] + h[1]])
        else:
            half["c"] = [c[0], c[1], c[2] + sgn * L / 2]
            half["h"] = [h[0], h[1], L / 2 - 0.05]
            piv = np.array([c[0], c[2] + sgn * L, c[1] + h[1]])
        BLD.bridge(b, half, self.B.L)
        for i in range(len(b.V)):
            b.V[i] = b.V[i] - piv
        o = b.build("span_%d" % sgn, MT.all_mats(self.B.L), self.B.coll)
        o.location = tuple(piv)
        o.rotation_mode = 'XYZ'
        return o

    # ------------------------------------------------------------ 每帧
    def pose_of(self, u, kind, i, k):
        rp = self.rp
        st, fl = int(rp.S[i, k]), int(rp.F[i, k])
        tsec = i * self.fdt
        if st == 3:
            return "dead", 0.0
        if st == 1:
            return "knock", (tsec * 1.3) % 1.0
        if kind == "boss":
            bs = int(rp.boss_state[i, k])
            if bs == 1:
                return "paw", tsec
            if bs == 3:
                return "stun", tsec
            if bs == 2:
                return "charge", (tsec * 2.3) % 1.0
            if fl & 1:
                return "attack", min(rp.A[i, k] / 1000.0, 1.0) * 0.45
            if rp.strike_age[i, k] < 0.5:
                return "attack", 0.45 + rp.strike_age[i, k] / 0.5 * 0.55
            if rp.speed[i, k] > 0.3:
                return "walk", float(rp.walk_phase[i, k])
            return "stand", tsec
        if kind == "catapult":
            a = rp.strike_age[i, k]
            if a < 0.8:
                return "attack", 0.45 + a / 0.8 * 0.55
            return "attack", 0.0
        if fl & 1:
            return "attack", min(rp.A[i, k] / 1000.0, 1.0) * 0.45
        if rp.strike_age[i, k] < 0.45:
            return "attack", 0.45 + rp.strike_age[i, k] / 0.45 * 0.55
        if kind == "spear" and fl & 2:
            return "brace", 0.0
        if st == 2:
            return "getup", (tsec * 1.5) % 1.0
        if rp.speed[i, k] > 0.35 or fl & 16:
            if kind == "cavalry" and (fl & 32):
                return "gallop", float(rp.walk_phase[i, k])
            return "walk", float(rp.walk_phase[i, k])
        return "stand", tsec + k * 0.37

    def apply(self, i):
        rp, B = self.rp, self.B
        i = int(min(max(i, 0), rp.nx - 1))
        for oid, u in self.objs.items():
            k = rp.col[oid]
            vis = bool(rp.present[i, k])
            j, out_t = i, None
            if not vis and k in self.gone:
                last, how, dur = self.gone[k]
                if last < i <= last + dur:
                    vis, j, out_t = True, last, (i - last) / float(dur)
            if vis != u.visible:
                u.set_visible(vis)
                u.visible = vis
            if not vis:
                continue
            u.place_q(rp.P[j, k], rp.Q[j, k])
            sc = u.scale
            inf = rp.info[oid]
            if out_t is not None:
                if self.gone[k][1] == "sink":
                    depth = SINK_DEPTH.get(inf.get("kind"), 1.1)
                    u.root.location.z -= depth * _smooth(out_t) ** 1.5
                else:
                    sc = sc * max(0.02, 1.0 - _smooth(out_t))
            if sc != 1.0 or u.root.scale[0] != 1.0:
                u.root.scale = (sc,) * 3
            if inf["type"] == "unit":
                kind = inf["kind"]
                alive = bool(rp.S[j, k] < 3)
                if alive != u.alive:
                    u.set_alive(alive)
                    u.alive = alive
                if u.ring is not None:
                    hide = (not alive) or (rp.S[j, k] == 1) or bool(rp.F[j, k] & 128)
                    u.ring.hide_render = bool(hide)
                pn, t = self.pose_of(u, kind, j, k)
                u.pose(pn, t)
                if self.has_mud:
                    u.set_mud(bool(rp.mud[j, k]))
                # 受击：挨打那一帧整个人提亮成暖白（0.2 秒内退掉），身子往后一仰
                fl = 0.0
                if alive and out_t is None:
                    ha = float(rp.hurt_age[j, k])
                    if ha < HIT_S:
                        fl = (1.0 - ha / HIT_S) ** 1.5 * (0.45 if kind == "boss" else 1.0)
                u.set_flash(fl)
                if fl > 0.02 and "body" in u.parts and not kind.startswith("lord") and u.kind != "lord" and pn != "knock":
                    bd = u.parts["body"]
                    bd.rotation_euler.x -= math.radians((5.0 if kind == "boss" else 16.0) * fl)
            elif inf["type"] == "arrow" and u.optional:
                u.show(("trail",) if rp.S[j, k] == 0 and out_t is None else ())
        for k, o in self.hang.items():                # 桥边火药桶的吊绳跟着桶
            vis = bool(rp.present[i, k])
            o.hide_render = not vis
            if vis:
                x, y, z = rp.P[i, k]
                o.location = (float(x), float(z), float(y))
        self.px.apply(i)
        if getattr(B, "tide", None):
            B.set_tide(float(rp.steps[min(i, rp.n - 1)]) * rp.dt)
        # 断桥
        for (f, ob, halves, along_x) in self.spans:
            age = (i - f) * self.fdt
            broken = age >= 0
            if ob is not None:
                ob.hide_render = broken
            for (o, sgn, hinge, z0) in halves:
                o.hide_render = not broken or age > 3.0
                if broken:
                    ang = min(age * 2.2 + age * age * 1.5, 1.45)
                    drop = max(0.0, age - 0.35) ** 2 * 4.9
                    if along_x:
                        o.rotation_euler = (0.0, -sgn * ang, 0.0)
                    else:
                        o.rotation_euler = (sgn * ang, 0.0, 0.0)
                    o.location.z = z0 - drop
        # 镜头（加震动：撞击后 0.6 秒内衰减的小幅抖动，幅度按画面宽度算）
        cx, cz, cy, wd, pt, yw = self.cam[i]
        cam = STG.camera((cx, cz, cy), pitch=pt, yaw=yw, dist=STG.fit_dist(wd), lens=50)
        ox = oy = 0.0
        for (f, amp) in self.shk:
            age = (i - f) * self.fdt
            if 0 <= age < 0.6:
                d = amp * math.exp(-age / 0.15) * wd / 30.0
                ox += d * math.sin(age * 61.0 + f)
                oy += d * math.sin(age * 73.0 + f * 1.7)
        if ox or oy:
            _, r, up, _ = RP._basis(pt, yw)
            cam.location = cam.location + Vector(r) * ox + Vector(up) * oy


def main():
    opt = parse_args()
    t0 = time.time()
    rp = RP.load(opt["replay"])
    a, b = 0, rp.nx
    if opt["frames"]:
        a, b = [int(v) for v in opt["frames"].split(":")]
        b = min(b, rp.nx)
    if opt["still"] is not None:
        a = b = None
        st = int(opt["still"])
        pl = Player(rp, opt, st, st + 1)
    else:
        pl = Player(rp, opt, a, b)
    sc = bpy.context.scene
    STG.setup_render(int(opt["samples"]), png=bool(opt["png"]) or opt["still"] is not None, fast=True)
    out = opt["out"]
    os.makedirs(out, exist_ok=True)
    print("BUILD_SEC %.2f frames %d objects %d particles %d/%d" % (time.time() - t0, rp.nx, len(bpy.data.objects), pl.n_px,
                                                                   len(pl.px.sp)), flush=True)
    if opt["still"] is not None:
        f = int(opt["still"])
        if opt.get("cam"):                     # 静帧指定机位：x,z,y,宽度[,俯角[,方位]]
            v = [float(t) if t.strip() else None for t in opt["cam"].split(",")]     # 空着的项沿用自动机位
            v += [None] * (6 - len(v))
            c = pl.cam[f].copy()
            for j in range(6):
                if v[j] is not None:
                    c[j] = v[j]
            pl.cam[f] = c
        pl.apply(f)
        sc.render.filepath = os.path.join(out, opt.get("name") or "still_%05d.png" % f)
        bpy.ops.render.render(write_still=True)
        print("STILL", sc.render.filepath, flush=True)
        return
    marks = []

    def pre(scene, *args):
        f = scene.frame_current
        marks.append(time.time())
        pl.apply(f)
        if len(marks) > 3 and len(marks) % 30 == 0:
            print("PROGRESS %d/%d  %.2f s/frame" % (f - a + 1, b - a, (marks[-1] - marks[2]) / (len(marks) - 3)), flush=True)

    pl.apply(a)                                   # 先摆一次（建好相机），之后每帧由回调重摆
    bpy.app.handlers.frame_change_pre.append(pre)
    sc.frame_start = a
    sc.frame_end = b - 1
    sc.render.use_overwrite = False
    sc.render.use_placeholder = False
    sc.render.filepath = os.path.join(out, "#####")
    t1 = time.time()
    bpy.ops.render.render(animation=True)
    n = max(1, len(marks) - 3)
    print("DONE %d frames in %.1f s, %.2f s/frame" % (b - a, time.time() - t1, (time.time() - marks[2]) / n if len(marks) > 3 else 0),
          flush=True)


if __name__ == "__main__":
    main()
