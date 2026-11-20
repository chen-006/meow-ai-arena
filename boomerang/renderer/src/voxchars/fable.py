# -*- coding: utf-8 -*-
"""Fable（正式形象 v2）：个子最高。深赤铜红棕发，脑后右侧一个低圆髻、一条粗辫垂在背后（头发全在身后），
髻边一枚橙红 Claude 星芒 + 一小枝古铜橄榄叶；金色细圆框眼镜；勃艮第酒红连帽长斗篷（墨蓝里子、金边），
暖白及踝长裙（裙底一道酒红边），酒红短靴；手里摊开一本古籍，腰侧一盏小提灯。"""
from voxkit import Model, head, face, legs, arms, hair_back, team_scarf, FRONT, TOP, PY

KEY = "fable"
LABEL = "Fable"
COLOR = "#9E2340"
NOTES = ("酒红", ["赤铜发 · 后髻粗辫", "金圆眼镜 · 摊开的古籍", "酒红连帽长斗篷"])
PAL = dict(hair="#8E3524", hair_d="#6A2618", hair_hi="#B85434", wine="#6B1D2B", wine_l="#8A2638", lining="#1B2838",
           gold="#D4A84A", cream="#FAF3E8", boot="#5A1E22", book="#4A251D", star="#F0602A", star_c="#FFB25A",
           leaf="#B8893A", lamp="#F59E0B", frame="#E0B24A")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=7)
    hz = M.hz
    B = M.part("body")
    B.box(-4, 4, -1, 4, 1, 3, c["wine"]).chamfer(-4, 4, -1, 4, 1, 3)       # 长斗篷下摆
    B.box(-3, 3, -1, 3, 3, 7, c["wine"])
    B.slab(-4.07, 4.07, -1.07, 4.07, 1.0, 1.3, c["gold"])                   # 斗篷金边
    B.faces(-4, 4, -1, 0, 1, 7, FRONT, c["lining"])                         # 敞口露出墨蓝里子
    B.box(-3, 3, -2, 0, 1, 3, c["cream"])                                   # 暖白及踝长裙
    B.box(-2, 2, -2, 2, 3, 7, c["cream"])
    B.slab(-3.07, 3.07, -2.07, -0.6, 1.15, 1.45, c["wine"])                 # 裙底酒红边
    B.tint(-2, 2, -2, 0, 4, 5, c["book"])                                   # 皮腰带
    B.box(-3, 3, -1, 3, 6, 7, c["wine"])                                    # 肩上的斗篷
    B.faces(-3, 3, -1, 0, 6, 7, FRONT, c["wine_l"])
    B.face(-1, -2, 6, FRONT, c["gold"]).face(0, -2, 6, FRONT, c["gold"])    # 胸前古铜扣
    B.box(-3, 3, 3, 4, 5, 7, c["wine_l"])                                   # 脖子后面堆着的兜帽
    B.box(0, 3, -3, -2, 4, 5, c["book"])                                    # 摊开的古籍（+X 一侧）
    B.faces(0, 3, -3, -2, 4, 5, TOP, c["cream"])
    B.put(-3, -2, 3, c["lamp"]).face(-3, -2, 3, FRONT, c["star_c"])         # 腰侧小提灯
    legs(M, c["boot"], c["cream"])
    arms(M, c["cream"], x=3)
    for nm, x0 in (("armA", 3), ("armB", -4)):                              # 上臂罩在斗篷里
        P = M.parts[nm]
        P.tint(x0, x0 + 1, 0, 1, hz - 2, hz, c["wine"])
    H = head(M, c["hair"], c["hair_hi"], bangs=(-3, 1, 2), bangs2=(-3, 2))
    H.put(0, -3, hz + 3, c["hair"])                                         # 中分偏一点的刘海
    H.box(2, 5, 2, 5, hz + 1, hz + 4, c["hair"])                            # 脑后右侧的低圆髻
    H.cut(4, 5, 4, 5, hz + 1, hz + 4)
    H.faces(2, 5, 2, 5, hz + 3, hz + 4, TOP, c["hair_hi"])
    for (x, z, k) in ((4, 5, "star_c"), (4, 6, "star"), (4, 4, "star"), (5, 5, "star")):
        H.put(x, 1, hz + z, c[k])                                           # 髻上方的 Claude 星芒（只有一枚）
    H.put(4, 2, hz + 4, c["leaf"]).put(5, 2, hz + 3, c["leaf"])            # 古铜橄榄枝
    hair_back(M, c["hair"], -1, 1, z0=2, z1=hz + 4, y0=4, y1=6, shade=c["hair_d"], shade_rows=0)
    P = M.parts["hairback"]                                                 # 背后的粗辫：一节深一节浅
    for z in range(2, hz + 4):
        if z % 2 == 0:
            P.tint(-1, 1, 4, 6, z, z + 1, c["hair_d"])
    P.box(-1, 1, 4, 6, 2, 3, c["wine"])                                     # 辫尾酒红丝带
    face(M, eye="#5A3418", glasses=c["frame"])                            # 深琥珀眼 + 细金圆框，眼睛露出来
    team_scarf(M, team)
    M.pal = c
    return M
