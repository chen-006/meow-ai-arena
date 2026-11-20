# -*- coding: utf-8 -*-
"""Opus（正式形象 v2）：亮橘色直长发全部收在身后（长到大腿），头侧一枚赤陶橙 Claude 放射星芒发夹（细芒，不要画成雏菊）；暖白立领衬衣、
深摩卡棕短斗篷 + 马甲，赤陶橘及小腿风琴百褶中长裙（裙底一道深棕边），深棕连裤袜和皮鞋；胸前抱一本深墨蓝活页夹。"""
from voxkit import Model, head, face, legs, arms, hair_back, team_scarf, FRONT, TOP

KEY = "opus"
LABEL = "Opus"
COLOR = "#FFA436"
NOTES = ("杏橙", ["亮橘直长发全在身后", "头侧赤陶橙星芒发夹", "摩卡短斗篷 · 赤陶橘中裙"])
PAL = dict(hair="#F07A22", hair_d="#D45B24", hair_hi="#FFAA5C", cream="#FAF3E8", mocha="#3A2620", mocha_l="#5A3A2E",
           skirt="#C9612A", skirt_d="#A84C20", band="#2B1D19", tights="#4A2E24", shoe="#3A2420", navy="#1E2B45",
           gold="#C8923B", star="#A63F22", star_c="#8A311A")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=6)
    hz = M.hz
    B = M.part("body")
    B.box(-3, 3, -2, 3, 1, 3, c["skirt"]).chamfer(-3, 3, -2, 3, 1, 3)    # 及小腿的百褶中长裙
    B.slab(-3.07, 3.07, -2.07, 3.07, 1.0, 1.32, c["band"])               # 裙底深棕边
    for x in (-2, 0, 2):                                                  # 百褶的暗线
        B.face(x, -2, 2, FRONT, c["skirt_d"]).face(x, -2, 1, FRONT, c["skirt_d"])
    B.box(-2, 2, -1, 2, 3, 6, c["cream"])                                # 暖白衬衣
    B.tint(-2, 2, -1, 2, 3, 4, c["mocha"])                               # 束腰马甲
    B.faces(-2, 2, -1, 0, 4, 5, FRONT, c["mocha"])
    B.box(-3, 3, -1, 3, 5, 6, c["mocha"])                                # 短斗篷（罩住肩和背）
    B.faces(-3, 3, -1, 3, 5, 6, TOP, c["mocha_l"])
    B.face(-1, -1, 5, FRONT, c["cream"]).face(0, -1, 5, FRONT, c["cream"])   # 立领
    B.face(0, -1, 4, FRONT, c["gold"])                                    # 星轨胸针
    B.box(-3, 0, -3, -1, 3, 5, c["navy"])                                # 抱在胸前的深墨蓝活页夹
    B.faces(-3, 0, -3, -1, 4, 5, TOP, c["cream"])
    B.face(-2, -3, 4, FRONT, c["gold"])
    legs(M, c["shoe"], c["tights"])
    arms(M, c["cream"], x=2)
    for nm, x0 in (("armA", 2), ("armB", -3)):                           # 肩头被短斗篷罩着
        M.parts[nm].tint(x0, x0 + 1, 0, 1, hz - 1, hz, c["mocha"])
    H = head(M, c["hair"], c["hair_hi"], bangs=(-3, -2, 2), bangs2=(-3, 2))
    H.face(3, -1, hz + 3, 0, c["hair_d"]).face(3, -1, hz + 4, 0, c["hair_hi"])   # 头侧细编发的纹路
    sx, sz, yy = 1.9, hz + 5.0, (-3.24, -3.0)                            # Claude 放射星芒（头侧，只有一枚）：赤陶橙细芒
    H.slab(sx - 0.38, sx + 0.38, yy[0], yy[1], sz - 0.38, sz + 0.38, c["star_c"])
    H.slab(sx - 1.15, sx + 1.15, yy[0], yy[1], sz - 0.15, sz + 0.15, c["star"])       # 横芒
    H.slab(sx - 0.15, sx + 0.15, yy[0], yy[1], sz - 1.15, sz + 1.15, c["star"])       # 竖芒
    for (dx, dz) in ((1, 1), (1, -1), (-1, 1), (-1, -1)):                               # 斜芒：两节小方块
        for r in (0.42, 0.68):
            H.slab(sx + dx * r - 0.13, sx + dx * r + 0.13, yy[0], yy[1], sz + dz * r - 0.13, sz + dz * r + 0.13, c["star"])
    hair_back(M, c["hair"], -5, 5, z0=2, shade=c["hair_d"], shade_rows=2)  # 长直发全在身后，到大腿
    face(M, eye="#B5641C")
    team_scarf(M, team)
    M.pal = c
    return M
