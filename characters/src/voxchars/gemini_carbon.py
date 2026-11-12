# -*- coding: utf-8 -*-
"""Gemini Carbon（v2，照 Gemini_大.png）：彩虹渐变的大波浪长发（头顶红橙 → 黄 → 绿 → 发梢蓝），橙色猫耳（内侧白），
头侧一枚彩虹四角星；白短外套（一边红袖口、一边蓝袖口），蓝腰带、背后蓝色大蝴蝶结，红黄绿蓝拼色短裙压白荷叶边；
一条腿黄白条纹长袜、一条腿蓝色长袜，白靴；身后一条彩虹猫尾（根部橙、尖上蓝、往上卷）。"""
from voxkit import Model, head, face, legs, arms, team_scarf, tidy, FRONT, TOP

KEY = "gemini_carbon"
LABEL = "Gemini Carbon"
COLOR = "#4A7BF0"
NOTES = ("彩虹", ["彩虹渐变大波浪长发", "猫耳 · 四角星 · 彩虹猫尾", "拼色短裙 · 一条纹一蓝长袜"])
PAL = dict(red="#F0605A", orange="#F79A48", yellow="#F6D454", green="#8ACB6A", blue="#5B8BEE", deep="#3F5FD8",
           ear_in="#FFF4EC", white="#FFFFFF", gold="#E8B83A", hi="#FF9A8A", boot="#F4F4F8")

# 头发按高度分色：从下往上 蓝 → 绿 → 黄 → 橙（头顶是红）
def band(c, z):
    if z <= 2:
        return c["blue"]
    if z <= 4:
        return c["green"]
    if z <= 6:
        return c["yellow"]
    return c["orange"]


def build(team=None):
    c = PAL
    M = Model(KEY, hz=7)
    hz = M.hz
    B = M.part("body")
    B.box(-3, 3, -2, 3, 3, 4, c["deep"])                                  # 拼色短裙
    for x, col in zip(range(-3, 3), (c["red"], c["red"], c["yellow"], c["green"], c["blue"], c["blue"])):
        B.tint(x, x + 1, -2, 0, 3, 4, col)
        B.faces(x, x + 1, -2, 0, 3, 4, TOP, col)
    B.slab(-3.07, 3.07, -2.07, 3.07, 2.75, 3.12, c["white"])               # 白荷叶边
    B.box(-2, 2, -1, 2, 4, 7, c["white"])                                 # 白短外套
    B.tint(-2, 2, -1, 2, 4, 5, c["deep"])                                 # 蓝腰带
    B.face(-1, -1, 4, FRONT, c["gold"]).face(0, -1, 4, FRONT, c["gold"])
    B.face(-1, -1, 6, FRONT, c["yellow"]).face(0, -1, 6, FRONT, c["yellow"])  # 黄领结
    B.face(-2, -1, 5, FRONT, c["red"]).face(1, -1, 6, FRONT, c["green"])  # 外套上的彩色斜纹
    B.box(-3, 3, 2, 3, 4, 6, c["deep"])                                   # 背后的蓝色大蝴蝶结
    B.cut(-1, 1, 2, 3, 4, 6).box(-1, 1, 2, 3, 4, 5, c["deep"])
    legs(M, c["boot"], c["blue"], h=3)
    P = M.parts["footB"]                                                  # 右腿：黄白条纹长袜
    x0 = min(k[0] for k in P.vox)
    P.put(x0, 0, 1, c["yellow"]).put(x0, 0, 2, c["white"])
    P.slab(x0 - 0.04, x0 + 1.04, -0.04, 1.04, 2.35, 2.65, c["yellow"])
    P.slab(x0 - 0.04, x0 + 1.04, -0.04, 1.04, 1.35, 1.65, c["white"])
    arms(M, c["white"], x=2)
    A, Bm = M.parts["armA"], M.parts["armB"]
    A.tint(2, 3, 0, 1, hz - 2, hz - 1, c["deep"])                         # 左袖口蓝
    Bm.tint(-3, -2, 0, 1, hz - 2, hz - 1, c["red"])                       # 右袖口红
    H = head(M, c["red"], c["hi"], bangs=(-3, -2, 1, 2), bangs2=(-3, 2))
    H.tint(-4, 4, -3, 4, hz, hz + 2, c["orange"], only=c["red"])
    H.boxm(3, 4, -2, 0, hz - 4, hz + 1, c["orange"])                      # 脸两侧垂下的长发
    for z in range(hz - 4, hz + 1):
        H.tint(-4, 4, -2, 0, z, z + 1, band(c, z), only=c["orange"])
    H.tint(3, 4, -2, 0, hz, hz + 1, c["orange"])
    H.tint(-4, -3, -2, 0, hz, hz + 1, c["orange"])
    for sg in (1, -1):                                                    # 猫耳
        def X(x):
            return x if sg > 0 else -x - 1
        H.put(X(3), -1, hz + 6, c["orange"]).put(X(3), 0, hz + 6, c["orange"])
        for x in (1, 2, 3):
            H.put(X(x), -1, hz + 7, c["orange"]).put(X(x), 0, hz + 7, c["orange"])
        for x in (2, 3):
            H.put(X(x), -1, hz + 8, c["orange"])
        H.put(X(3), -1, hz + 9, c["orange"])
        for (x, z) in ((2, hz + 7), (3, hz + 7), (3, hz + 8)):
            H.face(X(x), -1, z, FRONT, c["ear_in"])
    # 头侧的彩虹四角星（十字形，四个尖四种颜色）
    H.put(3, -4, hz + 5, c["white"])
    H.put(3, -4, hz + 6, c["red"]).put(4, -4, hz + 5, c["blue"]).put(3, -4, hz + 4, c["green"]).put(2, -4, hz + 5, c["yellow"])
    # 脑后的大波浪长发：比头宽，按高度分色
    Hb = M.part("hairback", (0, 3.5, hz + 1.5))
    for z in range(1, hz + 2):
        ins = 1 if z % 2 == 0 else 0
        Hb.box(-5 + ins, 5 - ins, 3, 5, z, z + 1, band(c, z))
    for x in range(-5, 5, 2):
        Hb.cut(x, x + 1, 3, 5, 1, 2)
    tidy(Hb)
    T = M.part("tail", (0, 3, 3.5))                                       # 彩虹猫尾：往后伸、往上卷
    T.box(-1, 1, 3, 7, 3, 4, c["orange"])
    T.box(-1, 1, 6, 8, 4, 6, c["yellow"])
    T.box(-1, 1, 6, 8, 6, 8, c["green"])
    T.box(-1, 1, 6, 8, 8, 10, c["blue"])
    T.box(-1, 1, 5, 6, 9, 10, c["blue"])
    M.tail_swing = 1.2
    face(M, eye="#6A3FC0")
    team_scarf(M, team)
    M.pal = c
    return M
