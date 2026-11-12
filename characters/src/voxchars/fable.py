# -*- coding: utf-8 -*-
"""Fable（v2，照 Claude_特大_Fable.png）：个子最高。杏橙色长发垂到膝、头侧半挽一个发髻插橙色花形发簪、垂一条白丝带，
圆框金丝眼镜；米白长裙压赤陶色裙边，外披赤陶色连帽长斗篷（金边、米色里子），胸前抱一本赤陶色的书，棕靴。"""
from voxkit import Model, head, face, legs, arms, hair_back, team_scarf, FRONT, TOP, PY

KEY = "fable"
LABEL = "Fable"
COLOR = "#C0583C"
NOTES = ("赤陶", ["杏橙长发 · 侧髻花簪", "圆框眼镜 · 抱一本书", "赤陶连帽长斗篷"])
PAL = dict(hair="#F4AA7C", hair_d="#D9845A", hair_hi="#FDCBAA", cape="#B9533A", cape_d="#93402C", gold="#E0B04E",
           cream="#F5EBDC", lining="#EADBC3", boot="#7A4A2C", book="#A8432F", flower="#EE6A2C", ribbon="#FFFFFF",
           frame="#C99A3E")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=7)
    hz = M.hz
    B = M.part("body")
    # 长斗篷：从肩垂到脚踝，前面敞开露出米白长裙
    B.box(-4, 4, -1, 3, 1, 3, c["cape"]).chamfer(-4, 4, -1, 3, 1, 3)
    B.box(-3, 3, -1, 3, 3, 7, c["cape"])
    B.tint(-4, 4, -1, 3, 1, 2, c["cape_d"])
    B.slab(-4.07, 4.07, -1.07, 3.07, 1.0, 1.3, c["gold"])                  # 斗篷下摆金边
    B.faces(-4, 4, -1, 0, 1, 3, FRONT, c["lining"])                        # 敞口露出的米色里子
    B.box(-3, 3, -2, 0, 1, 3, c["cream"])                                  # 米白长裙
    B.box(-2, 2, -2, 2, 3, 7, c["cream"])
    B.slab(-3.06, 3.06, -2.06, -0.5, 1.2, 1.5, c["cape"])                  # 裙边赤陶花纹带
    B.box(-3, 3, -1, 3, 6, 7, c["cape"])                                   # 肩上的斗篷披肩
    B.faces(-3, 3, -1, 3, 6, 7, TOP, c["cape"])
    B.faces(-3, 3, -1, 0, 6, 7, FRONT, c["gold"])                          # 披肩前沿金边
    B.face(-1, -2, 6, FRONT, c["cream"]).face(0, -2, 6, FRONT, c["cream"])
    B.box(-3, 3, 2, 3, 5, 7, c["cape_d"])                                  # 脖子后面堆着的兜帽（再往后是长发）
    B.box(-1, 2, -3, -2, 3, 6, c["book"])                                  # 抱在胸前的书
    B.faces(-1, 2, -3, -2, 3, 6, FRONT, c["book"])
    B.face(0, -3, 4, FRONT, c["gold"])
    B.faces(-1, 2, -3, -2, 5, 6, TOP, c["cream"])                          # 书页
    legs(M, c["boot"], c["cream"])
    arms(M, c["cream"], x=3, cuff=c["cream"])
    for nm in ("armA", "armB"):                                            # 袖子上半截被斗篷罩着
        P = M.parts[nm]
        x0 = min(k[0] for k in P.vox)
        P.tint(x0, x0 + 1, 0, 1, hz - 1, hz, c["cape"])
    H = head(M, c["hair"], c["hair_hi"], bangs=(-3, 2), bangs2=(-3, 2))
    H.put(-1, -3, hz + 3, c["hair"])                                       # 偏分的刘海
    H.boxm(3, 4, -2, 0, hz - 4, hz + 1, c["hair"])                         # 垂到胸前的两缕长发
    H.tint(-4, 4, -2, 0, hz - 4, hz - 2, c["hair_d"], only=c["hair"])
    H.box(3, 5, 0, 3, hz + 4, hz + 7, c["hair"])                           # 侧面半挽的发髻
    H.cut(4, 5, 0, 1, hz + 6, hz + 7)
    H.box(4, 5, -1, 1, hz + 5, hz + 7, c["flower"])                        # 橙色花形发簪
    H.put(4, -1, hz + 6, c["gold"])
    H.box(4, 5, 2, 3, hz + 2, hz + 4, c["ribbon"])                         # 发髻上垂下的白丝带
    hair_back(M, c["hair"], -3, 3, z0=1, z1=hz + 4, wavy=True, shade=c["hair_d"], shade_rows=2)
    face(M, eye="#8A4A22", glasses=c["frame"])
    team_scarf(M, team)
    M.pal = c
    return M
