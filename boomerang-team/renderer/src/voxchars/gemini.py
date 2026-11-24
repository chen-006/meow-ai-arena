# -*- coding: utf-8 -*-
"""Gemini（正式形象 v2）：宝蓝 → 星云紫 → 珊瑚玫粉渐变的大波浪长发（脸侧内层藏两三缕金、青、天青的微彩虹挑染），
头顶两侧一对丸子髻，一对蓝色尖猫耳（内侧粉白，一只耳边挂小金铃），头侧一枚炫彩四角星发夹；
深藏青水手连帽短夹克、白内搭、藏青箱褶短裙（裙底两道白线），一条腿藏青过膝袜、一条腿白色过膝袜，球鞋；
身后一条蓝 → 紫 → 玫粉的蓬松大猫尾。"""
from voxkit import Model, head, face, legs, arms, hair_back, team_scarf, FRONT

KEY = "gemini"
LABEL = "Gemini"
COLOR = "#E8459B"
NOTES = ("粉品红", ["蓝紫粉渐变长发 · 一点彩虹", "猫耳 · 丸子髻 · 四角星", "藏青水手夹克 · 鸳鸯长袜"])
PAL = dict(blue="#2C55E0", vio="#7C3AED", pink="#EC4899", pink_l="#F472B6", ear_in="#FFC4E1", navy="#141D38",
           white="#F6F8FC", hi="#6A86FF", gold="#FCD34D", teal="#2DD4BF", sky="#38BDF8", amber="#FBBF24")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=7)
    hz = M.hz
    B = M.part("body")
    B.box(-3, 3, -2, 3, 3, 4, c["navy"]).chamfer(-3, 3, -2, 3, 3, 4)    # 箱褶短裙
    B.slab(-2.6, 2.6, -2.07, 3.07, 3.12, 3.24, c["white"])              # 裙底两道白线
    B.slab(-2.6, 2.6, -2.07, 3.07, 3.38, 3.5, c["white"])
    B.box(-2, 2, -1, 2, 4, 7, c["navy"])                                 # 水手短夹克
    B.faces(-1, 1, -1, 0, 4, 7, FRONT, c["white"])                       # 白内搭
    B.box(-2, 2, 1, 3, 6, 7, c["navy"])                                  # 背后的兜帽 / 水手领
    B.slab(-2.07, 2.07, 2.93, 3.07, 6.0, 6.15, c["white"])
    B.face(0, -1, 6, FRONT, c["gold"])                                   # 颈带小金坠
    legs(M, c["white"], c["white"], h=3, sole=c["white"])
    for (nm, sock) in (("footA", c["white"]), ("footB", c["navy"])):     # 鸳鸯过膝袜
        P = M.parts[nm]
        x0 = min(k[0] for k in P.vox)
        P.box(x0, x0 + 1, 0, 1, 1, 3, sock)
        P.put(x0, 0, 0, c["navy"]).put(x0, -1, 0, c["navy"])             # 球鞋
    M.parts["footA"].face(1, 0, 2, FRONT, c["pink"])                     # 白袜上的细条纹
    arms(M, c["navy"], x=2, cuff=c["sky"])
    H = head(M, c["blue"], c["hi"], bangs=(-3, -2, 1, 2), bangs2=(-3, 2))
    H.tint(-4, 4, -3, 4, hz, hz + 3, c["vio"], only=c["blue"])           # 往下渐变成紫
    H.tint(-4, 4, -3, 4, hz, hz + 1, c["pink"], only=c["vio"])
    H.face(-3, -3, hz + 2, FRONT, c["amber"]).face(2, -3, hz + 2, FRONT, c["teal"])   # 脸侧内层的一点彩虹
    for sg in (1, -1):
        def X(x):
            return x if sg > 0 else -x - 1
        H.box(X(4), X(4) + 1, 0, 3, hz + 4, hz + 6, c["vio"])            # 丸子髻
        H.put(X(4), 1, hz + 6, c["vio"])
        for x in (1, 2, 3):                                              # 尖猫耳
            H.put(X(x), -1, hz + 7, c["blue"])
            H.put(X(x), 0, hz + 7, c["blue"])
        for x in (2, 3):
            H.put(X(x), -1, hz + 8, c["blue"])
        H.put(X(3), -1, hz + 9, c["blue"])
        for (x, z) in ((2, hz + 7), (3, hz + 7), (3, hz + 8)):
            H.face(X(x), -1, z, FRONT, c["ear_in"])
    H.put(4, -1, hz + 7, c["gold"])                                      # 左耳边的小金铃
    for (x, z, k) in ((2, 5, "gold"), (2, 6, "teal"), (1, 5, "sky"), (3, 5, "pink_l"), (2, 4, "amber")):
        H.put(x, -4, hz + z, c[k])                                       # 炫彩四角星（头侧，只有一枚）
    hair_back(M, c["vio"], -5, 5, z0=3, z1=hz + 3, wavy=True, tip=c["pink"], tip_rows=2, shade=c["vio"], shade_rows=0)
    P = M.parts["hairback"]
    P.tint(-5, 5, 3, 5, hz + 1, hz + 3, c["blue"])                       # 后脑上段还是宝蓝
    for (x, z, k) in ((-5, 5, "teal"), (4, 6, "amber"), (-4, 7, "sky")):  # 中下段内侧的两三缕彩虹丝
        if (x, 3, z) in P.vox:
            P.face(x, 3, z, 0 if x > 0 else 1, c[k])
    T = M.part("tail", (0, 3, 3.5))                                      # 蓬松大猫尾：蓝 → 紫 → 玫粉
    T.box(-1, 1, 3, 8, 3, 4, c["blue"])
    T.box(-1, 1, 6, 8, 4, 9, c["vio"])
    T.tint(-1, 1, 6, 8, 7, 9, c["pink"])
    T.boxm(1, 2, 6, 8, 7, 9, c["pink"])
    M.tail_swing = 1.2
    face(M, eye="#C2477E")
    team_scarf(M, team)
    M.pal = c
    return M
