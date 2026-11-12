# -*- coding: utf-8 -*-
"""Gemini Hydrogen（v2，照 Gemini_小.png）：Gemini Carbon 的年少版，个子小。彩虹短发（头顶红 → 橙黄 → 两侧低扎的小辫绿蓝），
呆毛、橙色猫耳、头侧彩虹四角星，小辫系黄蝴蝶结；同款白短外套、拼色短裙、一条纹一蓝长袜、彩虹猫尾。"""
from voxkit import Model, head, face, legs, arms, team_scarf, FRONT, TOP

KEY = "gemini_hydrogen"
LABEL = "Gemini Hydrogen"
COLOR = "#F2A33A"
NOTES = ("彩虹", ["彩虹短发 · 低扎小辫", "猫耳 · 四角星 · 彩虹猫尾", "拼色短裙 · 个子小"])
PAL = dict(red="#F0605A", orange="#F79A48", yellow="#F6D454", green="#8ACB6A", blue="#5B8BEE", deep="#3F5FD8",
           ear_in="#FFF4EC", white="#FFFFFF", gold="#E8B83A", hi="#FF9A8A", boot="#F4F4F8", bow="#F5C531")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=5)
    hz = M.hz
    B = M.part("body")
    B.box(-3, 3, -2, 3, 2, 3, c["deep"])                                  # 拼色短裙
    for x, col in zip(range(-3, 3), (c["red"], c["red"], c["yellow"], c["green"], c["blue"], c["blue"])):
        B.tint(x, x + 1, -2, 0, 2, 3, col)
        B.faces(x, x + 1, -2, 0, 2, 3, TOP, col)
    B.slab(-3.07, 3.07, -2.07, 3.07, 1.8, 2.14, c["white"])                # 白荷叶边
    B.box(-2, 2, -1, 2, 3, 5, c["white"])                                 # 白短外套
    B.tint(-2, 2, -1, 2, 3, 4, c["deep"])                                 # 蓝腰带
    B.face(-1, -1, 3, FRONT, c["gold"]).face(0, -1, 3, FRONT, c["gold"])
    B.face(-1, -1, 4, FRONT, c["yellow"]).face(0, -1, 4, FRONT, c["yellow"])  # 黄领结
    B.face(1, -1, 4, FRONT, c["green"]).face(-2, -1, 4, FRONT, c["red"])
    legs(M, c["boot"], c["blue"], h=2)
    P = M.parts["footB"]                                                  # 右腿：黄白条纹长袜
    x0 = min(k[0] for k in P.vox)
    P.put(x0, 0, 1, c["yellow"])
    P.slab(x0 - 0.04, x0 + 1.04, -0.04, 1.04, 1.4, 1.7, c["white"])
    arms(M, c["white"], x=2, n=2)
    M.parts["armA"].tint(2, 3, 0, 1, hz - 1, hz, c["deep"])               # 左袖口蓝
    M.parts["armB"].tint(-3, -2, 0, 1, hz - 1, hz, c["red"])              # 右袖口红
    H = head(M, c["red"], c["hi"], bangs=(-3, -2, -1, 1, 2), bangs2=(-3, 2))
    H.tint(-4, 4, -3, 4, hz, hz + 2, c["orange"], only=c["red"])
    H.tint(-4, 4, -3, 4, hz, hz + 1, c["yellow"], only=c["orange"])
    H.put(0, 0, hz + 7, c["red"]).put(0, 0, hz + 8, c["red"]).put(1, 0, hz + 8, c["red"])  # 呆毛
    for sg in (1, -1):                                                    # 猫耳
        def X(x):
            return x if sg > 0 else -x - 1
        H.put(X(3), -1, hz + 6, c["orange"]).put(X(3), 0, hz + 6, c["orange"])
        for x in (2, 3):
            H.put(X(x), -1, hz + 7, c["orange"]).put(X(x), 0, hz + 7, c["orange"])
        H.put(X(3), -1, hz + 8, c["orange"])
        for (x, z) in ((2, hz + 7), (3, hz + 7)):
            H.face(X(x), -1, z, FRONT, c["ear_in"])
    H.put(3, -4, hz + 5, c["white"])                                      # 彩虹四角星
    H.put(3, -4, hz + 6, c["red"]).put(4, -4, hz + 5, c["blue"]).put(3, -4, hz + 4, c["green"]).put(2, -4, hz + 5, c["yellow"])
    for nm, sg in (("tailL", 1), ("tailR", -1)):                          # 两侧低扎的小辫：黄 → 绿 → 蓝
        x0 = 4 if sg > 0 else -6
        T = M.part(nm, (4 * sg, 0, hz + 1.5))
        T.box(x0, x0 + 2, -1, 1, hz - 2, hz - 1, c["blue"])
        T.box(x0, x0 + 2, -1, 1, hz - 1, hz + 1, c["green"])
        T.box(x0, x0 + 2, -1, 1, hz + 1, hz + 2, c["yellow"])
        T.cut(x0 if sg < 0 else x0 + 1, (x0 if sg < 0 else x0 + 1) + 1, -1, 1, hz - 2, hz - 1)
        xb = 4 if sg > 0 else -5
        T.box(xb, xb + 1, -2, 1, hz + 2, hz + 3, c["bow"])
    T = M.part("tail", (0, 3, 2.5))                                       # 彩虹猫尾
    T.box(-1, 1, 3, 5, 2, 3, c["orange"])
    T.box(-1, 1, 4, 6, 3, 5, c["yellow"])
    T.box(-1, 1, 4, 6, 5, 6, c["green"])
    T.box(-1, 1, 4, 6, 6, 7, c["blue"])
    T.box(-1, 1, 6, 7, 6, 7, c["blue"])
    M.tail_swing = 1.3
    face(M, eye="#6A3FC0")
    team_scarf(M, team)
    M.pal = c
    return M
