# -*- coding: utf-8 -*-
"""Qwen（v2，照 千问_大.png）：淡紫色长发，头顶盘一个发髻横插银色发簪，头侧一枚紫色六角发饰垂着流苏，
一条粗辫子从右肩垂到腰；白色交领上衣、半透明的宽袖，紫色宽腰带垂下一条长飘带，齐腰长裙从白渐变到紫；手里一把团扇。"""
from voxkit import Model, head, face, legs, arms, hair_back, team_scarf, FRONT, TOP

KEY = "qwen"
LABEL = "Qwen"
COLOR = "#8E6BE8"
NOTES = ("淡紫", ["淡紫长发 · 发髻发簪", "紫色六角发饰 · 粗辫", "白衣紫裙 · 团扇"])
PAL = dict(hair="#CDB2F0", hair_d="#A98BDB", hair_hi="#E8DAFB", white="#FBF8FF", lav="#DCCBF6", lav2="#B597EC",
           purple="#8459D8", deep="#6A42C0", silver="#DADDE8", fan="#FFFFFF", shoe="#8E6BE8", pink="#E7A8D8", wood="#C9A04A")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=6)
    hz = M.hz
    B = M.part("body")
    B.box(-4, 4, -3, 4, 1, 2, c["lav2"]).chamfer(-4, 4, -3, 4, 1, 2)      # 裙摆：最下面一排紫
    B.slab(-4.07, 4.07, -3.07, 4.07, 1.0, 1.3, c["purple"])
    B.box(-3, 3, -2, 3, 2, 3, c["lav"])                                   # 往上浅紫 → 白
    B.box(-2, 2, -1, 2, 3, 6, c["white"])                                 # 白上衣
    B.tint(-2, 2, -1, 2, 3, 4, c["purple"])                               # 紫色宽腰带
    B.face(-1, -1, 3, FRONT, c["deep"])
    B.faces(-1, 0, -3, -1, 1, 3, FRONT, c["purple"])                      # 腰带垂下的长飘带
    B.faces(-1, 0, -3, -1, 1, 3, TOP, c["purple"])
    B.face(-1, -1, 5, FRONT, c["lav2"]).face(0, -1, 4, FRONT, c["lav2"])  # 交领
    legs(M, c["shoe"], c["white"])
    arms(M, c["white"], x=2)
    for nm, x0 in (("armA", 2), ("armB", -3)):                            # 半透明的宽袖：手臂后面多垂一片
        P = M.parts[nm]
        P.box(x0, x0 + 1, 1, 2, hz - 3, hz - 1, c["lav"])
        P.tint(x0, x0 + 1, 0, 1, hz - 2, hz - 1, c["lav"])
    A = M.parts["armA"]                                                   # 团扇：圆扇面（白，紫边）+ 扇柄
    for x in range(3, 6):
        for z in range(hz - 3, hz):
            corner = x in (3, 5) and z in (hz - 3, hz - 1)
            A.put(x, -1, z, c["lav2"] if corner else c["fan"])
    A.face(4, -1, hz - 2, FRONT, c["pink"]).face(3, -1, hz - 2, FRONT, c["lav"])
    A.put(3, 0, hz - 3, c["wood"])
    H = head(M, c["hair"], c["hair_hi"], bangs=(-3, -2, 2), bangs2=(-3, 2))
    H.box(-1, 1, 0, 2, hz + 7, hz + 9, c["hair"])                         # 头顶的发髻
    H.put(-1, 1, hz + 9, c["hair"]).put(0, 0, hz + 9, c["hair"])
    H.slab(-3.2, 3.6, 0.3, 0.7, hz + 8.3, hz + 8.6, c["silver"])           # 横插的银发簪
    H.box(2, 5, -4, -3, hz + 4, hz + 7, c["purple"])                      # 紫色六角发饰（去角，中间白）
    H.cut(2, 3, -4, -3, hz + 6, hz + 7).cut(4, 5, -4, -3, hz + 4, hz + 5)
    H.put(3, -4, hz + 5, c["white"])
    H.box(4, 5, -4, -3, hz + 1, hz + 5, c["lav2"])                        # 流苏
    T = M.part("tailR", (-4, -1, hz + 0.5))                               # 右肩垂到腰的粗辫子
    for z in range(hz - 3, hz + 1):
        T.box(-5, -3, -2, 0, z, z + 1, c["hair"] if (z - hz) % 2 else c["hair_d"])
    T.box(-5, -3, -2, 0, hz - 4, hz - 3, c["purple"])
    hair_back(M, c["hair"], -4, 4, z0=2, wavy=True, shade=c["hair_d"], shade_rows=2)
    face(M, eye="#7A4FC8")
    team_scarf(M, team)
    M.pal = c
    return M
