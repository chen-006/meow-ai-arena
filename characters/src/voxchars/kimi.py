# -*- coding: utf-8 -*-
"""Kimi（v2，照 Kimi_大.png）：纯白的公主切长直发（齐刘海、脸两侧齐剪的长鬓垂到胸前、后面直直垂到脚边），
头侧一枚银色月牙发饰，下面一枚黑色方形发夹；深蓝星空长裙（银色星点），肩上一件白纱小披肩，胸前黑蝴蝶结配蓝宝石。"""
from voxkit import Model, head, face, legs, arms, hair_back, team_scarf, FRONT, TOP, PX, NX, PY

KEY = "kimi"
LABEL = "Kimi"
COLOR = "#3D47A8"
NOTES = ("星空蓝", ["纯白公主切长直发", "银月牙 · 黑色方发夹", "深蓝星空长裙 · 白纱披肩"])
PAL = dict(hair="#F8F9FD", shade="#DCE1EE", navy="#2B2F70", navy_l="#3E46A6", silver="#A9B3CC", star="#E8ECF8",
           veil="#ECEFF8", black="#22232B", gem="#2F6BFF", white="#FFFFFF")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=6)
    hz = M.hz
    B = M.part("body")
    B.box(-4, 4, -3, 4, 1, 2, c["navy_l"]).chamfer(-4, 4, -3, 4, 1, 2)   # 长裙：下摆亮一号
    B.slab(-4.07, 4.07, -3.07, 4.07, 1.2, 1.36, c["silver"])              # 裙边银线
    B.box(-3, 3, -2, 3, 2, 3, c["navy"])
    B.box(-2, 2, -1, 2, 3, 6, c["navy"])
    B.tint(-2, 2, -1, 2, 3, 4, c["black"])                                # 腰带
    for (x, y, z, d) in ((2, -1, 2, FRONT), (-3, 0, 2, FRONT), (-3, -3, 1, FRONT), (1, -3, 1, FRONT), (3, 1, 1, PX),
                         (-4, 2, 1, NX), (2, 2, 2, PX), (-3, 1, 2, NX), (-1, 3, 1, PY), (1, 2, 2, PY)):
        B.face(x, y, z, d, c["star"])                                     # 银色星点
    B.box(-2, 2, -1, 2, 5, 6, c["veil"])                                  # 肩上的白纱小披肩
    B.faces(-2, 2, -1, 0, 4, 5, FRONT, c["veil"])
    B.face(-1, -1, 5, FRONT, c["black"]).face(0, -1, 5, FRONT, c["black"])  # 黑蝴蝶结
    B.face(0, -1, 4, FRONT, c["gem"])
    legs(M, c["black"], c["white"])
    arms(M, c["navy"], x=2, cuff=c["white"])
    for nm, x0 in (("armA", 2), ("armB", -3)):                            # 披肩罩住的肩头
        M.parts[nm].tint(x0, x0 + 1, 0, 1, hz - 1, hz, c["veil"])
    H = head(M, c["hair"], None, bangs=(-3, -2, -1, 0, 1, 2), bangs2=())
    H.tint(-4, 4, 0, 4, hz, hz + 2, c["shade"], only=c["hair"])
    H.boxm(3, 4, -3, -1, hz - 3, hz + 3, c["hair"])                       # 公主切：脸两侧齐剪的长鬓
    H.tint(-4, 4, -3, -1, hz - 3, hz - 2, c["shade"], only=c["hair"])
    for (x, z) in ((3, 6), (4, 6), (4, 7), (4, 8), (3, 8)):              # 头顶一角的银色月牙（缺口朝里）
        H.put(x, -1, hz + z, c["silver"])
        H.put(x, 0, hz + z, c["silver"])
    H.box(3, 5, -3, -1, hz + 4, hz + 6, c["black"])                       # 黑色方形发夹
    H.face(3, -3, hz + 5, FRONT, c["white"]).face(4, -3, hz + 4, FRONT, c["white"]).face(4, -3, hz + 5, FRONT, c["gem"])
    hair_back(M, c["hair"], -4, 4, z0=2, z1=hz + 3, shade=c["shade"], shade_rows=2)
    face(M, eye="#5A4FB0")
    team_scarf(M, team)
    M.pal = c
    return M
