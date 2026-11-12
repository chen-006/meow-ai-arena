# -*- coding: utf-8 -*-
"""定妆照：9 个兵种 × 四个队伍色并排（近景 + 真实全景距离），主办方单位和道具。
blender.exe -b --factory-startup -P stills_units.py -- <输出目录> [close wide host poses] [--samples 32]
"""
import math
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bpy  # noqa: E402

import army as AR  # noqa: E402
import scene_lib as SL  # noqa: E402
import stage as STG  # noqa: E402
import units as UN  # noqa: E402

args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = args[0] if args else os.path.join(HERE, "..", "renders", "stills")
WHAT = [a for a in args[1:] if not a.startswith("--") and not a[0].isdigit()] or ["close", "wide", "host"]
SAMPLES = int(args[args.index("--samples") + 1]) if "--samples" in args else 32
os.makedirs(OUT, exist_ok=True)

WIDTH = dict(militia=1.6, shield=1.8, spear=1.6, archer=1.6, hammer=1.9, bomber=1.6, cavalry=2.2, giant=3.2,
             catapult=3.6)


def ground(col="#8CC456", size=200):
    me = bpy.data.meshes.new("ground")
    s = size / 2
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    m = bpy.data.materials.new("M_ground")
    nb = SL.NB(m)
    g = nb.geo()
    n = nb.noise(nb.out(g, 'Position'), 0.25, 3.0, 0.6)
    c = nb.mix(nb.smooth(n, 0.35, 0.65), nb.rgb(SL.hexc(col)), nb.rgb(SL.hexc("#A6D466")))
    p = nb.principled(c, rough=0.95, spec=0.05)
    nb.output(p)
    me.materials.append(m)
    ob = bpy.data.objects.new("ground", me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def scene_base():
    SL.clear_scene()
    SL._MATS.clear()
    AR.KIT = None
    STG.setup_render(SAMPLES, png=True, fast=False)
    SL.setup_world(strength=0.75)
    STG.sun(az=145, el=50, energy=4.2)
    ground()


def lineup(coll, y0=0.0, pose="stand"):
    """9 个兵种排成一列一列，四个队一队一排（北边是 0 队）。"""
    us = []
    x = 0.0
    xs = {}
    total = sum(WIDTH[k] for k in UN.KINDS)
    x = -total / 2
    for k in UN.KINDS:
        xs[k] = x + WIDTH[k] / 2
        x += WIDTH[k]
    for team in range(4):
        y = y0 + (1.5 - team) * 3.6
        for k in UN.KINDS:
            u = AR.Unit(k, team, coll, "%s_%d" % (k, team))
            u.place(xs[k], y, 0.0, yaw_deg=180.0)          # 面朝南（镜头）
            u.pose(pose, 0.3)
            us.append(u)
    return us


def run_close():
    scene_base()
    coll = bpy.context.scene.collection
    lineup(coll)
    STG.camera((0.0, 0.6, 0.9), pitch=30, yaw=0, dist=31, lens=50)
    STG.render_to(os.path.join(OUT, "定妆_兵种_近景_四队.png"))


def run_wide():
    """真实全景距离：和第 1 关全景同样的机位（装下 64 米宽、俯角 55 度）。"""
    scene_base()
    coll = bpy.context.scene.collection
    lineup(coll)
    d = STG.fit_dist(66.0)
    STG.camera((0.0, 0.0, 0.0), pitch=55, yaw=0, dist=d, lens=50)
    STG.render_to(os.path.join(OUT, "定妆_兵种_全景距离.png"))


def run_host():
    scene_base()
    coll = bpy.context.scene.collection
    # 镇山兽、主帅（两位）、大雪球、火药桶、军旗、木栅、木栅门、立石
    b = AR.Unit("boss", 1, coll, "boss", ring=False)
    b.place(-7.0, 1.5, 0.0, yaw_deg=200)
    b.pose("stand")
    for i in range(2):                                  # 通用主帅（蓝、红）
        u = AR.Unit("lord", i, coll, "lord_%d" % i)
        u.place(-1.4 + i * 1.6, -3.2, 0.0, yaw_deg=200, scale=1.3)
    s = AR.Unit("snowball", None, coll, "snow", ring=False)
    s.place(2.2, 2.6, 1.5, yaw_deg=0)
    k1 = AR.Unit("keg", None, coll, "keg1", ring=False)
    k1.place(1.6, -2.4, 0.0)
    k2 = AR.Unit("keg", None, coll, "keg2", ring=False)
    k2.place(2.9, -2.0, 0.0, yaw_deg=40)
    f = AR.Unit("flag", 0, coll, "flag", ring=False)
    f.place(5.5, 3.8, 0.0, yaw_deg=200)
    br = AR.Unit("barrier", None, coll, "barrier", ring=False)
    br.place(8.5, -3.6, 0.0, yaw_deg=180, scale=0.6)
    g = AR.Unit("gate", None, coll, "gate", ring=False)
    g.place(10.0, 4.6, 0.0, yaw_deg=180, scale=0.7)
    p = AR.Unit("pillar", None, coll, "pillar", ring=False)
    p.place(4.8, -0.2, 0.0, yaw_deg=180)
    STG.camera((1.2, 1.0, 1.0), pitch=30, yaw=0, dist=36, lens=50)
    STG.render_to(os.path.join(OUT, "定妆_主办方单位和道具.png"))


def run_lord():
    """主帅：通用将领（默认）和换成某家 AI 体素形象的样子，旁边站普通兵比大小；近景 + 全景距离各一张。"""
    scene_base()
    coll = bpy.context.scene.collection
    items = [("lord", 0, 1.3), ("lord", 1, 1.3), ("lord:fable", 0, 1.7)]
    for i, (k, team, sc) in enumerate(items):
        u = AR.Unit(k, team, coll, "lord_%d" % i)
        u.place(-3.0 + i * 3.0, 0.0, 0.0, yaw_deg=200, scale=sc)
        for j, kk in enumerate(("militia", "spear", "shield")):
            m = AR.Unit(kk, team, coll, "g_%d_%d" % (i, j))
            m.place(-3.9 + i * 3.0 + j * 0.9, 1.6, 0.0, yaw_deg=200)
    STG.camera((0.0, -0.5, 1.0), pitch=26, yaw=0, dist=17, lens=50)
    STG.render_to(os.path.join(OUT, "定妆_主帅_近景.png"))
    STG.camera((0.0, -0.5, 0.0), pitch=55, yaw=0, dist=STG.fit_dist(40.0), lens=50)
    STG.render_to(os.path.join(OUT, "定妆_主帅_全景距离.png"))


def run_poses():
    """每个兵种的几个关键姿势（站、走、出手、倒地；长枪多一个架枪）。蓝队。"""
    scene_base()
    coll = bpy.context.scene.collection
    rows = [("stand", 0.0), ("walk", 0.25), ("attack", 0.3), ("attack", 0.52), ("knock", 0.3)]
    total = sum(WIDTH[k] for k in UN.KINDS)
    for ri, (pn, t) in enumerate(rows):
        x = -total / 2
        y = (2 - ri) * 3.4
        for k in UN.KINDS:
            u = AR.Unit(k, 0, coll, "%s_%d" % (k, ri))
            name = pn
            if k == "spear" and pn == "knock":
                name = "brace"
            u.place(x + WIDTH[k] / 2, y, 0.0, yaw_deg=210)
            u.pose(name, t)
            x += WIDTH[k]
    STG.camera((0.0, 0.0, 0.9), pitch=32, yaw=0, dist=36, lens=50)
    STG.render_to(os.path.join(OUT, "定妆_兵种_姿势.png"))


def run_boss():
    """镇山兽定妆：六个姿势（站、走、刨地、冲撞、撞晕、甩头），旁边放立石和几个兵比大小；近景 + 全景距离各一张。"""
    scene_base()
    coll = bpy.context.scene.collection
    poses = [("stand", 0.0, 205), ("walk", 0.2, 150), ("paw", 0.31, 205), ("charge", 0.2, 250), ("stun", 0.4, 180),
             ("attack", 0.55, 130)]
    for i, (pn, t, yaw) in enumerate(poses):
        b = AR.Unit("boss", 1, coll, "boss_%d" % i, ring=False)
        b.place(-11.0 + (i % 3) * 11.0, 5.0 - (i // 3) * 10.0, 0.0, yaw_deg=yaw)
        b.pose(pn, t)
    p = AR.Unit("pillar", None, coll, "pillar", ring=False)
    p.place(-5.5, 0.0, 0.0, yaw_deg=180)
    for j, kk in enumerate(("militia", "spear", "shield", "cavalry")):
        m = AR.Unit(kk, j % 2, coll, "g_%d" % j)
        m.place(4.0 + j * 1.3, 0.0, 0.0, yaw_deg=200)
    STG.camera((0.0, -1.0, 1.2), pitch=28, yaw=0, dist=STG.fit_dist(36.0), lens=50)
    STG.render_to(os.path.join(OUT, "定妆_镇山兽_近景.png"))
    STG.camera((0.0, 0.0, 0.0), pitch=40, yaw=0, dist=STG.fit_dist(48.0), lens=50)
    STG.render_to(os.path.join(OUT, "定妆_镇山兽_全景距离.png"))


t0 = time.time()
for w in WHAT:
    globals()["run_" + w]()
    print("DONE", w, "%.1f s" % (time.time() - t0), flush=True)
