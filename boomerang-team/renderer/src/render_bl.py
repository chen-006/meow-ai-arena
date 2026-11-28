# -*- coding: utf-8 -*-
"""Blender 入口：建场景 -> 逐帧摆放 -> 渲染。
    blender.exe -b --factory-startup -P render_bl.py -- <回放> --out <目录> [--frames a:b] [--samples 12]
        [--roster claude,gpt,...] [--still 名字 --f 帧号 | --gt 步] [--cam tx,ty,dist,pitch] [--png] [--pct 100]
已经渲好的帧会跳过（断点续渲）；--frames 用来分段并行。
"""
import math
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bpy  # noqa: E402
import numpy as np  # noqa: E402

import cast  # noqa: E402
import chars as CH  # noqa: E402
import env as ENV  # noqa: E402
import fx as FXM  # noqa: E402
import replay  # noqa: E402
import scene_lib as SL  # noqa: E402
import timeline as TLM  # noqa: E402
from geom import MeshBuilder  # noqa: E402
from replay import D_OUT  # noqa: E402

# 像素小人的肢体动作每 VOX_HOLD 帧才换一次姿势（"一拍二"，像素动画的顿挫感）；位置、朝向、闪白仍然逐帧。1 = 关掉。
VOX_HOLD = int(os.environ.get("DART_VOX_HOLD", "2"))
HELD_KEYS = ("lean", "roll", "twist", "sx", "sy", "sz", "body_dz", "head", "head_dz", "hand_a", "hand_b", "foot_a",
             "foot_b", "foot_a_pitch", "foot_b_pitch", "hair", "tail", "hop")


def parse_args():
    a = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    opt = dict(replay=a[0], out=None, frames=None, samples=12, roster=None, still=None, f=None, gt=None, cam=None,
               png=False, pct=100, fast=1, nodecor=False, sun=None, face=None, shots=None)
    i = 1
    while i < len(a):
        k = a[i].lstrip("-")
        if k in ("png", "nodecor"):
            opt[k] = True
            i += 1
        else:
            opt[k] = a[i + 1]
            i += 2
    return opt


class Ribbon:
    """镖的拖尾 / 冲刺气流：固定拓扑的带子，每帧改顶点位置和颜色。"""
    ROWS = ((-1.0, 0.0), (-0.45, 1.0), (0.0, 1.0), (0.45, 1.0), (1.0, 0.0))

    def __init__(self, name, coll, n=17):
        self.n = n
        m = len(self.ROWS)
        V = np.zeros((n * m, 3))
        F = []
        for i in range(n - 1):
            for j in range(m - 1):
                a0 = i * m + j
                F.append((a0, a0 + 1, a0 + 1 + m, a0 + m))
        mb = MeshBuilder()
        mb.add(V, F, vcolors=np.zeros((n * m, 4)), smooth=False)
        self.ob = mb.build(name, [SL.mat_emit_alpha("M_trail", strength=1.35)], coll)
        self.me = self.ob.data
        self.co = np.zeros((n * m, 3), dtype=np.float32)
        self.cl = np.zeros((n * m, 4), dtype=np.float32)

    def update(self, pts, col, style):
        """pts：从新到旧 [(x, y, z, st)]（地图坐标）。style(i, t, st) -> (半宽, 透明度, 芯的白度)。"""
        if len(pts) < 2:
            if not self.ob.hide_render:
                self.ob.hide_render = True
            return
        self.ob.hide_render = False
        n, m = self.n, len(self.ROWS)
        k = len(pts)
        for i in range(n):
            j = min(i, k - 1)
            x, y, z, st = pts[j]
            a = pts[max(j - 1, 0)]
            b = pts[min(j + 1, k - 1)]
            dx, dy = a[0] - b[0], -(a[1] - b[1])
            L = math.hypot(dx, dy)
            if L < 1e-6:
                px, py = 0.0, 0.0
            else:
                px, py = -dy / L, dx / L
            t = j / max(1, k - 1)
            hw, al, core = style(j, t, st) if i < k else (0.0, 0.0, 0.0)
            for r, (u, e) in enumerate(self.ROWS):
                idx = i * m + r
                self.co[idx] = (x + px * hw * u, -y + py * hw * u, z + (0.004 if r == 2 else 0.0))
                if r == 2:
                    w = core
                    self.cl[idx] = (col[0] + (1 - col[0]) * w, col[1] + (1 - col[1]) * w, col[2] + (1 - col[2]) * w, al)
                else:
                    self.cl[idx] = (col[0], col[1], col[2], al * e * 0.8)
        self.me.vertices.foreach_set("co", self.co.ravel())
        self.me.color_attributes["col"].data.foreach_set("color", self.cl.ravel())
        self.me.update()


def trail_style(j, t, st):
    """去程：宽、实、白芯；回程 / 召回：窄、淡、没有白芯、一节一节的（像虚线）。"""
    fade = (1 - t) ** 1.25
    if st == D_OUT:
        return 0.27 * (0.25 + 0.75 * (1 - t) ** 0.8), 0.92 * fade, 0.7
    dash = 0.45 + 0.55 * (1.0 if (j // 2) % 2 == 0 else 0.25)
    return 0.15 * (0.3 + 0.7 * (1 - t) ** 0.8), 0.75 * fade * dash, 0.15


def dash_style(j, t, st):
    return 0.3 * (1 - t) ** 0.6, 0.5 * (1 - t) ** 1.4, 0.9


class Stage:
    def __init__(self, rp, roster=None, decor=True):
        self.rp = rp
        n = rp.n
        self.roster = cast.roster_for(rp, roster)
        demo_path = os.environ.get("DART_DEMO")          # 演示模式：导演表决定游戏时间和镜头（见 demo.py）
        if demo_path:
            import demo as DEMO
            self.tl = DEMO.DemoTimeline(rp, self.roster, DEMO.load(demo_path))
            self.demo = self.tl.demo
        else:
            self.tl = TLM.Timeline(rp, self.roster)
            self.demo = None
        dm = self.demo or {}
        self.offs = {int(k): float(v) for k, v in dm.get("offset", {}).items()}
        self.hide = set(int(k) for k in dm.get("hide", []))
        self.ghost = dm.get("ghost")
        self.no_fx = set(dm.get("no_fx", []))
        self.anim = self.tl.anim
        SL.clear_scene()
        # 队伍色按队伍身份（Astra 队蓝、Fable 队红），换边局也不变；小人、地上的圈、特效、城堡都用这一份
        self.team_colors = cast.team_colors(self.roster, rp.players)
        self.env = ENV.build_env(rp, decor=decor, team_colors=self.team_colors)
        ENV.std_light()
        cd = bpy.data.cameras.new("cam")
        cd.lens = TLM.LENS
        cd.sensor_width = TLM.SENSOR
        cd.clip_start = 0.5
        cd.clip_end = 500.0
        self.cam = bpy.data.objects.new("cam", cd)
        bpy.context.scene.collection.objects.link(self.cam)
        bpy.context.scene.camera = self.cam
        self.set_cam_pitch(TLM.PITCH)
        coll = ENV.new_coll("actors")
        team = rp.mode in (2, 3, 4, 5)
        kings = getattr(rp, "kings", (-1, -1))
        self.rigs = []
        self.colors = []
        for k in range(n):
            key = self.roster[k]
            tc = self.team_colors[rp.players[k][1]] if team else None
            self.rigs.append(CH.make_rig(key, coll, "c%d" % k, team_color=tc, ring_color=tc, crown=k in kings))
            self.colors.append(CH.rig_color(key))
        for k in range(n):                      # 像素小人：被镖打中的，预先把"切成两半"建好（占点有复活：每条命各一套）
            for A in self.anim.lives:
                si = A.slice_info(k)
                if si is not None and hasattr(self.rigs[k], "build_slice"):
                    if A.life == 0:
                        self.rigs[k].build_slice(si)
                    else:
                        self.rigs[k].build_slice(si, life=A.life)
        self.points = None
        if getattr(rp, "points", None):         # 第 12 集占点：地上三个圈
            import modes_bl
            self.points = modes_bl.Points(rp, ENV.new_coll("points"), self.team_colors)
        self.darts = [CH.make_dart_any(self.colors[k], coll, "dart%d" % k, scale=1.42) for k in range(n)]
        self.blurs = [CH.make_blur_disc(self.colors[k], coll, "blur%d" % k) for k in range(n)]
        self.trails = [Ribbon("trail%d" % k, coll) for k in range(n)]
        self.dashes = [Ribbon("dash%d" % k, coll, n=11) for k in range(n)]
        self.lin = [SL.hexc(c)[:3] for c in self.colors]
        self.fx = FXM.FX(ENV.new_coll("fx"), self.colors, TLM.PITCH, team_colors=self.team_colors)
        if self.ghost:
            self._ghost_mats(int(self.ghost["seat"]))

    def _ghost_mats(self, k):
        """演示模式：这个角色换成能半透明的材质（物体颜色的 alpha = 不透明度，不再当闪白用）。"""
        cache = {}

        def ghost(m):
            if m is None:
                return m
            if m.name not in cache:
                g = bpy.data.materials.new(m.name + "_ghost")
                nb = SL.NB(g)
                oi = nb.node("ShaderNodeObjectInfo")
                mul = nb.node("ShaderNodeMix", data_type='RGBA', blend_type='MULTIPLY')
                nb.inp(mul, 'Factor', 'VALUE').default_value = 1.0
                nb.link(nb.out(nb.attr('col'), 'Color'), nb.inp(mul, 'A', 'RGBA'))
                nb.link(nb.out(oi, 'Color'), nb.inp(mul, 'B', 'RGBA'))
                col = nb.out(mul, 'Result', 'RGBA')
                if "outline" in m.name:
                    p = nb.principled(col, rough=1.0, spec=0.0, emission=col, emis_strength=1.0, alpha=nb.out(oi, 'Alpha'))
                    g.use_backface_culling = True
                else:
                    p = nb.principled(col, rough=0.92, spec=0.04, emission=col, emis_strength=0.16, alpha=nb.out(oi, 'Alpha'))
                nb.output(p)
                try:
                    g.surface_render_method = 'DITHERED'
                except Exception:
                    pass
                cache[m.name] = g
            return cache[m.name]
        r = self.rigs[k]
        for ob in getattr(r, "objs", []):
            for i, slot in enumerate(ob.material_slots):
                ob.material_slots[i].material = ghost(slot.material)

    def ghost_alpha(self, gt):
        """演示模式：viewer 看不见 seat 的程度（回放的 vis 位，前后 1.5 步平均）→ 不透明度。"""
        g = self.ghost
        k, v, lo = int(g["seat"]), int(g["viewer"]), float(g.get("alpha", 0.2))
        rp = self.rp
        tot = 0.0
        for dt in (-1.5, -0.5, 0.5, 1.5):
            t = int(min(max(gt + dt, 0), rp.T))
            c = rp.states[t]["chars"][k]
            if c["alive"] and not (c["vis"] >> v) & 1:
                tot += 0.25
        return 1.0 - (1.0 - lo) * tot

    def set_cam_pitch(self, pitch, yaw=0.0):
        p = math.radians(pitch)
        self.cam.rotation_euler = (math.radians(90.0) - p, 0.0, math.radians(yaw))

    def apply_gt(self, gt, gt_hold=None):
        rp, A = self.rp, self.anim
        ENV.set_collapse(self.env, rp, gt)
        movers = []
        gt0, gh0 = gt, gt_hold
        for k in range(rp.n):
            gt = gt0 - self.offs.get(k, 0.0)                 # 演示模式：这个人（和他的镖）晚几步
            gt_hold = None if gh0 is None else gh0 - self.offs.get(k, 0.0)
            if k in self.hide:                               # 演示模式：整个不渲染
                self.rigs[k].set_visible(False)
                self.darts[k].hide_render = True
                self.blurs[k].hide_render = True
                self.trails[k].update([], self.lin[k], trail_style)
                self.dashes[k].update([], (1.0, 1.0, 1.0), dash_style)
                continue
            pose = A.char_pose(k, gt)
            if gt_hold is not None and gt_hold != gt and hasattr(self.rigs[k], "model"):
                held = A.char_pose(k, gt_hold)
                if held.get("visible", True) and pose.get("visible", True) and ("flip" in held) == ("flip" in pose):
                    for key_ in HELD_KEYS:
                        if key_ in held:
                            pose[key_] = held[key_]
                        else:
                            pose.pop(key_, None)
            self.rigs[k].apply(pose)
            if self.ghost and int(self.ghost["seat"]) == k and hasattr(self.rigs[k], "objs"):
                ga = self.ghost_alpha(gt)
                r = self.rigs[k]
                for ob in r.objs:
                    c = ob.color
                    ob.color = (c[0], c[1], c[2], ga)
                r.ring.color = (1, 1, 1, r.ring.color[3] * ga)
                r.blob.color = (1, 1, 1, r.blob.color[3] * ga)
            if pose.get("visible", True) and abs(pose.get("z", 0.0)) < 0.5 and pose.get("ground", 1.0) > 0.5:
                movers.append((pose["x"], pose["y"], A.speed(k, gt) if 0 < gt < rp.T else 0.0))
            d = A.dart_state(k, gt)
            ob, bl = self.darts[k], self.blurs[k]
            if d is not None and d["mode"] == "fly" and d["z"] < 0.9:
                movers.append((d["x"], d["y"], 0.3))
            if d is None:
                ob.hide_render = True
                bl.hide_render = True
            else:
                ob.hide_render = False
                s = d["scale"] * (1.0 if d["mode"] == "fly" else 0.78)
                ob.location = (d["x"], -d["y"], d["z"])
                ob.rotation_euler = (d["tilt"], 0.12 if d["mode"] == "fly" else d["tilt"] * 0.6, d["spin"])
                ob.scale = (s, s, s)
                g = d["glow"]
                ob.color = (1.0, 1.0, 1.0, 1.0 - g * 0.6)
                fly = d["mode"] == "fly"
                bl.hide_render = not fly
                if fly:
                    bl.location = (d["x"], -d["y"], d["z"] - 0.01)
                    bl.rotation_euler = (0, 0, d["spin"] * 0.37)
                    bl.color = (1, 1, 1, 1.0)
            self.trails[k].update(A.trail(k, gt), self.lin[k], trail_style)
            pts, w = A.dash_trail(k, gt)
            self.dashes[k].update(pts if w > 0.02 else [], (1.0, 1.0, 1.0), lambda j, t, st, w=w: tuple(
                v * (w if i == 1 else 1.0) for i, v in enumerate(dash_style(j, t, st))))
        gt = gt0
        if self.points is not None:
            self.points.apply(gt)
        ENV.set_ambient(self.env, rp, gt, movers)
        if self.demo is not None:
            self.fx.apply(A, gt, self.env, offs=self.offs, hide=self.hide, no_fx=self.no_fx)
        else:
            self.fx.apply(A, gt, self.env)

    def apply_frame(self, f):
        fh = f - (f % VOX_HOLD) if VOX_HOLD > 1 else f
        self.apply_gt(self.tl.frames[f]["gt"], self.tl.frames[fh]["gt"])
        self.set_cam_pitch(self.tl.frames[f].get("pitch", TLM.PITCH), self.tl.frames[f].get("yaw", 0.0))
        self.cam.location = self.tl.cam_location(f)
        if "lens" in self.tl.frames[f]:
            self.cam.data.lens = self.tl.frames[f]["lens"]

    def set_cam(self, tx, ty, dist, pitch, yaw=0.0, lens=None, tz=0.0, fstop=None, focus=None):
        self.set_cam_pitch(pitch, yaw)
        if lens:
            self.cam.data.lens = lens
        if fstop:
            self.cam.data.dof.use_dof = True
            self.cam.data.dof.aperture_fstop = fstop
            self.cam.data.dof.focus_distance = focus or dist
        p, yw = math.radians(pitch), math.radians(yaw)
        self.cam.location = (tx + math.sin(yw) * math.cos(p) * dist, ty - math.cos(yw) * math.cos(p) * dist,
                             tz + math.sin(p) * dist)


def setup_render(samples=12, png=False, pct=100, fast=1):
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_EEVEE'
    sc.render.resolution_x = 1920
    sc.render.resolution_y = 1080
    sc.render.resolution_percentage = pct
    sc.render.film_transparent = False
    if png:
        sc.render.image_settings.file_format = 'PNG'
        sc.render.image_settings.color_mode = 'RGB'
        sc.render.image_settings.compression = 20
    else:
        sc.render.image_settings.file_format = 'JPEG'
        sc.render.image_settings.quality = 96
    sc.view_settings.view_transform = 'Standard'
    sc.view_settings.look = 'None'
    sc.render.threads_mode = 'FIXED'
    sc.render.threads = 4
    sc.render.use_persistent_data = True
    ee = sc.eevee
    ee.taa_render_samples = samples
    ee.use_shadows = True
    ee.shadow_ray_count = 1 if fast else 2
    ee.shadow_step_count = 6 if fast else 8
    ee.use_raytracing = True
    ee.ray_tracing_method = 'SCREEN'
    try:
        ee.ray_tracing_options.resolution_scale = '2' if fast else '1'
    except Exception:
        pass
    ee.use_fast_gi = True
    try:
        ee.fast_gi_method = 'GLOBAL_ILLUMINATION'
        ee.fast_gi_distance = 2.5
        ee.fast_gi_ray_count = 2
        ee.fast_gi_step_count = 6 if fast else 8
        ee.fast_gi_resolution = '4' if fast else '2'
    except Exception:
        pass


def main():
    opt = parse_args()
    rp = replay.load(opt["replay"])
    roster = opt["roster"].split(",") if opt["roster"] else None
    t0 = time.time()
    st = Stage(rp, roster, decor=not opt["nodecor"])
    setup_render(int(opt["samples"]), png=bool(opt["png"]) or bool(opt["still"]) or bool(opt["shots"]), pct=int(opt["pct"]),
                 fast=int(opt["fast"]))
    sc = bpy.context.scene
    out = opt["out"]
    os.makedirs(out, exist_ok=True)
    print("BUILD_SEC %.2f frames %d" % (time.time() - t0, st.tl.n), flush=True)
    if opt["shots"]:
        # 一次建场景出多张静帧：JSON 列表 [{"name":, "gt": 或 "f":, "cam": [tx,ty,dist,pitch,yaw,lens,tz]（可省）}]
        import json
        sc.render.image_settings.file_format = 'PNG'
        sc.render.image_settings.color_mode = 'RGB'
        for sh in json.load(open(opt["shots"], encoding="utf-8")):
            st.cam.data.lens = TLM.LENS
            if sh.get("gt") is not None:
                gt = float(sh["gt"])
                f = min(range(st.tl.n), key=lambda i: abs(st.tl.frames[i]["gt"] - gt))
                st.apply_frame(f)
                st.apply_gt(gt)
            else:
                st.apply_frame(int(sh["f"]))
            if sh.get("cam"):
                st.set_cam(*[float(x) for x in sh["cam"]])
            sc.render.filepath = os.path.join(out, sh["name"] + ".png")
            bpy.ops.render.render(write_still=True)
            print("STILL", sc.render.filepath, flush=True)
        return
    if opt["still"]:
        if opt["gt"] is not None:
            gt = float(opt["gt"])
            f = min(range(st.tl.n), key=lambda i: abs(st.tl.frames[i]["gt"] - gt))
            st.apply_frame(f)
            st.apply_gt(gt)
        else:
            f = int(opt["f"])
            st.apply_frame(f)
        if opt["cam"]:
            # tx,ty,dist,pitch[,yaw,lens,tz,fstop,focus]
            v = [float(x) for x in opt["cam"].split(",")]
            st.set_cam(*v)
        if opt.get("sun"):
            az, el = [float(x) for x in opt["sun"].split(",")]
            sun = bpy.data.objects["sun"]
            from mathutils import Vector
            a_, e_ = math.radians(az), math.radians(el)
            to_l = Vector((math.cos(a_) * math.cos(e_), math.sin(a_) * math.cos(e_), math.sin(e_)))
            sun.rotation_euler = (-to_l).to_track_quat('-Z', 'Y').to_euler()
        if opt.get("face"):
            for r in st.rigs:
                p = st.anim.char_pose(st.rigs.index(r), 0.0)
                p["heading"] = float(opt["face"])
                r.apply(p)
        sc.render.filepath = os.path.join(out, opt["still"] + ".png")
        bpy.ops.render.render(write_still=True)
        print("STILL", sc.render.filepath, flush=True)
        return
    a, b = 0, st.tl.n
    if opt["frames"]:
        a, b = [int(v) for v in opt["frames"].split(":")]
        b = min(b, st.tl.n)
    # 用 Blender 的"渲染动画"一次跑完一段：静态的大网格只同步一次，比逐帧调用渲染快一倍多。
    # 每帧渲染前由回调把角色、镖、特效、镜头摆到位；已存在的帧自动跳过（断点续渲）。
    marks = []

    def pre(scene, *args):
        f = scene.frame_current
        marks.append(time.time())
        st.apply_frame(min(max(f, 0), st.tl.n - 1))
        if len(marks) > 3 and len(marks) % 20 == 0:
            print("PROGRESS %d/%d  %.2f s/frame" % (f - a + 1, b - a, (marks[-1] - marks[2]) / (len(marks) - 3)), flush=True)

    bpy.app.handlers.frame_change_pre.append(pre)
    sc.frame_start = a
    sc.frame_end = b - 1
    sc.frame_step = 1
    sc.render.use_overwrite = False
    sc.render.use_placeholder = False
    sc.render.use_file_extension = True
    sc.render.filepath = os.path.join(out, "#####")
    t1 = time.time()
    bpy.ops.render.render(animation=True)
    n = max(1, len(marks) - 3)
    print("DONE %d frames in %.1f s, %.2f s/frame (不含前 3 帧)" % (b - a, time.time() - t1, (time.time() - marks[2]) / n if len(marks) > 3 else 0), flush=True)


if __name__ == "__main__":
    main()
