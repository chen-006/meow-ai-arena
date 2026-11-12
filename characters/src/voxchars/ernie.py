# -*- coding: utf-8 -*-
"""ERNIE / 文心（v2，照 文心_大.png）：亮蓝色齐肩短发，头顶两个小圆发髻系白丝带，头侧一枚蓝色六边形发夹；
蓝白千鸟格短披肩（深蓝镶边），白衬衫配深蓝领带，藏青高腰背带裤裙（白线），白长袜、棕色乐福鞋；
右手举一把金框放大镜，腰边挂一只棕色挎包。"""
from voxkit import Model, head, face, legs, arms, hair_back, team_scarf, FRONT, TOP

KEY = "ernie"
LABEL = "ERNIE"
COLOR = "#2F6EF0"
NOTES = ("亮蓝", ["亮蓝齐肩短发 · 双发髻", "蓝色六边形发夹 · 放大镜", "千鸟格披肩 · 背带裤裙"])
PAL = dict(hair="#2F6CF2", hair_d="#1F4FC8", hair_hi="#6E9CFF", check="#4F86F0", white="#FFFFFF", navy="#22306E",
           shirt="#F6F7FB", brown="#7A4A2C", gold="#E0B04E", lens="#CFEFFF", ribbon="#FFFFFF")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=6)
    hz = M.hz
    B = M.part("body")
    B.box(-3, 3, -2, 3, 2, 3, c["navy"])                                  # 藏青裤裙
    B.slab(-3.07, 3.07, -2.07, 3.07, 2.12, 2.34, c["white"])               # 白线
    B.box(-2, 2, -1, 2, 3, 6, c["shirt"])                                 # 白衬衫
    B.tint(-2, 2, -1, 2, 3, 4, c["navy"])                                 # 高腰
    B.face(-2, -1, 3, FRONT, c["gold"]).face(1, -1, 3, FRONT, c["gold"])
    B.face(0, -1, 4, FRONT, c["navy"])                                    # 领带
    for x in range(-2, 2):                                                # 千鸟格短披肩：蓝白相间
        for y in range(-1, 2):
            B.put(x, y, 5, c["check"] if (x + y) % 2 == 0 else c["white"])
    B.slab(-2.06, 2.06, -1.06, 2.06, 4.62, 4.86, c["navy"])                # 披肩深蓝镶边
    B.face(-1, -1, 5, FRONT, c["white"]).face(0, -1, 5, FRONT, c["navy"])
    B.box(3, 4, -1, 1, 2, 4, c["brown"])                                  # 棕色挎包
    B.face(3, -1, 3, FRONT, c["gold"])
    legs(M, c["brown"], c["white"])
    for nm in ("footA", "footB"):
        P = M.parts[nm]
        x0 = min(k[0] for k in P.vox)
        P.slab(x0 - 0.04, x0 + 1.04, -0.04, 1.04, 1.55, 1.75, c["check"])  # 袜口蓝线
    arms(M, c["shirt"], x=2)
    for nm, x0 in (("armA", 2), ("armB", -3)):                            # 肩头被披肩罩着
        M.parts[nm].put(x0, 0, hz - 1, c["check"] if nm == "armA" else c["white"])
    A = M.parts["armB"]                                                   # 放大镜：金框（六边形）+ 浅蓝镜片 + 棕柄
    for x in range(-6, -3):
        for z in range(hz - 2, hz + 1):
            if x in (-6, -4) and z in (hz - 2, hz):
                continue
            A.put(x, -1, z, c["lens"] if (x, z) == (-5, hz - 1) else c["gold"])
    A.put(-3, -1, hz - 3, c["brown"]).put(-4, -1, hz - 3, c["brown"]).put(-4, -1, hz - 2, c["brown"])
    H = head(M, c["hair"], c["hair_hi"], bangs=(-3, -2, -1, 0, 1, 2), bangs2=(-3, 2))
    H.boxm(3, 4, -2, 3, hz, hz + 1, c["hair"])                            # 齐肩的两侧
    H.boxm(3, 4, 0, 3, hz - 1, hz, c["hair_d"])
    H.boxm(1, 3, -1, 1, hz + 7, hz + 9, c["hair"])                        # 头顶两个小圆发髻
    H.cut(2, 3, -1, 0, hz + 8, hz + 9).cut(-3, -2, -1, 0, hz + 8, hz + 9)
    H.put(3, 0, hz + 7, c["ribbon"]).put(-4, 0, hz + 7, c["ribbon"])       # 发髻边的白丝带
    H.put(3, 1, hz + 6, c["ribbon"]).put(-4, 1, hz + 6, c["ribbon"])
    H.box(2, 5, -4, -3, hz + 4, hz + 7, c["white"])                       # 六边形发夹：白底（去两个角）、中间深蓝
    H.cut(2, 3, -4, -3, hz + 6, hz + 7).cut(4, 5, -4, -3, hz + 4, hz + 5)
    H.put(3, -4, hz + 5, c["hair_d"]).put(3, -4, hz + 6, c["hair_d"])
    hair_back(M, c["hair"], -4, 4, z0=hz - 1, shade=c["hair_d"], shade_rows=1)
    face(M, eye="#C07A1A")
    team_scarf(M, team)
    M.pal = c
    return M
