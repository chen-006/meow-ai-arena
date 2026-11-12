# -*- coding: utf-8 -*-
"""Blender 入口：按"时间表"渲染——每个输出帧给一个回放时刻（可以是小数帧）和一个机位，用来做慢放、快进、跳剪、手摆跟拍。
    blender.exe -b --factory-startup -P render_timeline.py -- <时间表.json> --out <目录> [--frames a:b] [--samples 12]
时间表（由 timeline.py 之类的普通 Python 脚本生成）：
    {"replay": 回放路径, "res": [宽, 高], "lord": "default", "no_fx": false,
     "frames": [[回放帧(小数), 目标x, 目标z, 目标高度y, 画面宽度, 俯角, 方位, 震动右移, 震动上移], ...]}
输出帧编号 = 在 frames 里的下标（00000.jpg 起）。已经渲好的帧跳过（断点续渲）。
小数帧：单位、道具、箭、石头的位置和朝向在前后两帧之间插值（慢放不卡顿）；肢体动作、受击闪亮按前一帧；粒子按小数时间算。
"""
import json
import math
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Vector  # noqa: E402

import replay as RP  # noqa: E402
import render_replay as RR  # noqa: E402
import stage as STG  # noqa: E402


def parse_args():
    a = sys.argv[sys.argv.index("--") + 1:]
    opt = dict(plan=a[0], out=None, frames=None, samples=12)
    i = 1
    while i < len(a):
        opt[a[i].lstrip("-")] = a[i + 1]
        i += 2
    return opt


def px_apply_f(px, t):
    """fxsys.Particles.apply 的小数帧版（照抄，只是 age 用小数时间、候选取前后两帧）。"""
    import fxsys as FXS
    now = set()
    cand = set(px.by_frame.get(int(math.floor(t)), ())) | set(px.by_frame.get(int(math.ceil(t)), ()))
    for j in cand:
        (kind, f0, life, p, v, g, drag, rise, s0, s1, color, rot, spin, floor, pop) = px.sp[j]
        age = (t - f0) * px.fdt
        if age < 0 or age > life:
            continue
        key = px.assign[j]
        ob = px.obs[key]
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
        ob.scale = (s, s, 1.0) if kind in FXS.FLAT else (s, s, s)
        ob.rotation_euler = ((rot[0] + spin[0] * age) * FXS.DEG, (rot[1] + spin[1] * age) * FXS.DEG,
                             (rot[2] + spin[2] * age) * FXS.DEG)
        if color is not None and px.cols.get(key) != color:
            c = FXS.hexc(color)
            ob.color = (c[0], c[1], c[2], 1.0)
            px.cols[key] = color
        ob.hide_render = False
    for key in px.live - now:
        px.obs[key].hide_render = True
    px.live = now


def apply_f(pl, t, cam6, shake):
    rp = pl.rp
    i0 = int(min(max(math.floor(t), 0), rp.nx - 1))
    i1 = min(i0 + 1, rp.nx - 1)
    u = float(t - math.floor(t)) if i1 > i0 else 0.0
    pl.apply(i0)
    if u > 1e-4:
        for oid, ob in pl.objs.items():
            k = rp.col[oid]
            if not (ob.visible and rp.present[i0, k] and rp.present[i1, k]):
                continue
            p0, p1 = rp.P[i0, k].astype(float), rp.P[i1, k].astype(float)
            if np.linalg.norm(p1 - p0) > 3.0:          # 瞬移（不该插值）
                continue
            q0, q1 = rp.Q[i0, k].astype(float), rp.Q[i1, k].astype(float)
            if np.dot(q0, q1) < 0:
                q1 = -q1
            q = q0 * (1 - u) + q1 * u
            q /= max(np.linalg.norm(q), 1e-9)
            ob.place_q(p0 * (1 - u) + p1 * u, q)
        for k, o in pl.hang.items():
            if rp.present[i0, k] and rp.present[i1, k]:
                x, y, z = rp.P[i0, k] * (1 - u) + rp.P[i1, k] * u
                o.location = (float(x), float(z), float(y))
    if not pl.opt.get("no_fx"):
        px_apply_f(pl.px, float(t))
    cx, cz, cy, wd, pt, yw = cam6
    cam = STG.camera((cx, cz, cy), pitch=pt, yaw=yw, dist=STG.fit_dist(wd), lens=50)
    ox, oy = shake
    if ox or oy:
        _, r, up, _ = RP._basis(pt, yw)
        cam.location = cam.location + Vector(r) * ox + Vector(up) * oy


def main():
    opt = parse_args()
    t0 = time.time()
    plan = json.load(open(opt["plan"], encoding="utf-8"))
    rp = RP.load(plan["replay"])
    F = plan["frames"]
    a, b = 0, len(F)
    if opt["frames"]:
        a, b = [int(v) for v in opt["frames"].split(":")]
        b = min(b, len(F))
    out = opt["out"]
    os.makedirs(out, exist_ok=True)
    todo = [j for j in range(a, b) if not os.path.exists(os.path.join(out, "%05d.jpg" % j))]
    if not todo:
        print("NOTHING TO DO", flush=True)
        return
    ts = [F[j][0] for j in todo]
    lo, hi = int(math.floor(min(ts))), int(math.ceil(max(ts))) + 2
    popt = dict(replay=plan["replay"], samples=int(opt["samples"]), lord=plan.get("lord", "default"), pitch=None, shots=None,
                no_close=True, no_fx=bool(plan.get("no_fx")), no_shake=True)
    pl = RR.Player(rp, popt, max(0, lo - 2), min(rp.nx, hi))
    w, h = plan.get("res", [1920, 1080])
    STG.setup_render(int(opt["samples"]), png=False, fast=True, w=int(w), h=int(h))
    sc = bpy.context.scene
    print("BUILD_SEC %.2f todo %d" % (time.time() - t0, len(todo)), flush=True)
    t1 = time.time()
    for n, j in enumerate(todo):
        row = F[j]
        apply_f(pl, row[0], row[1:7], (row[7], row[8]) if len(row) > 8 else (0.0, 0.0))
        sc.render.filepath = os.path.join(out, "%05d.jpg" % j)
        bpy.ops.render.render(write_still=True)
        if n % 20 == 19:
            print("PROGRESS %d/%d %.2f s/frame" % (n + 1, len(todo), (time.time() - t1) / (n + 1)), flush=True)
    print("DONE %d frames %.2f s/frame" % (len(todo), (time.time() - t1) / max(1, len(todo))), flush=True)


if __name__ == "__main__":
    main()
