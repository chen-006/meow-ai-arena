# -*- coding: utf-8 -*-
"""小 Gemini（正式形象 v2 小版，Gemini 的妹妹）：和姐姐同一套设计缩小一号——同样的宝蓝 → 星云紫 → 玫粉渐变发、
蓝色尖猫耳（内侧粉白、一只耳边小金铃）、两侧丸子髻、炫彩四角星发夹、蓝紫玫粉大猫尾；
区别：个子更矮、头身更 Q，头发只到肩下（姐姐垂到大腿），白色泡泡短袖 + 深藏青短款水手披肩夹克、胸前藏青蝴蝶结，
藏青背带百褶短裙（两道白线），一腿藏青过膝袜、一腿白过膝袜（袜口紫粉条纹），魔术贴球鞋。主色取樱粉（姐姐是粉品红）。"""
from voxkit import Model, head, face, legs, arms, hair_back, team_scarf, FRONT

KEY = "gemini_s"
LABEL = "小 Gemini"
COLOR = "#FFA6D6"
NOTES = ("樱粉", ["蓝紫粉渐变及肩发 · 猫耳丸子", "四角星发夹 · 大猫尾", "泡泡袖 · 背带短裙 · 个子小"])
PAL = dict(blue="#2C55E0", vio="#7C3AED", pink="#EC4899", pink_l="#F472B6", ear_in="#FFC4E1", navy="#141D38",
           white="#F6F8FC", hi="#6A86FF", gold="#FCD34D", teal="#2DD4BF", sky="#38BDF8", amber="#FBBF24")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=5)
    M.scale = 0.9                                                        # 整体再小一号：场上一眼看出是妹妹
    hz = M.hz
    B = M.part("body")
    B.box(-3, 3, -2, 3, 2, 3, c["navy"]).chamfer(-3, 3, -2, 3, 2, 3)    # 百褶短裙
    B.slab(-2.6, 2.6, -2.07, 3.07, 2.1, 2.22, c["white"])               # 裙底两道白线
    B.slab(-2.6, 2.6, -2.07, 3.07, 2.36, 2.48, c["white"])
    B.box(-2, 2, -1, 2, 3, 5, c["white"])                                # 白衬衫
    B.faces(-2, -1, -1, 0, 3, 4, FRONT, c["navy"]).faces(1, 2, -1, 0, 3, 4, FRONT, c["navy"])   # 背带
    B.box(-3, 3, -1, 3, 4, 5, c["navy"])                                 # 短款水手披肩夹克（只罩到胸口）
    B.faces(-1, 1, -1, 0, 4, 5, FRONT, c["white"])
    B.slab(-3.07, 3.07, 2.93, 3.07, 4.0, 4.14, c["white"])               # 披肩背后白边
    B.face(-1, -1, 4, FRONT, c["navy"]).face(0, -1, 4, FRONT, c["navy"])  # 胸前藏青蝴蝶结
    B.slab(-0.25, 0.25, -1.12, -1.0, 4.1, 4.6, c["gold"])               # 蝴蝶结中间的小金扣
    legs(M, c["white"], c["white"], h=2, sole=c["white"])
    for (nm, sock) in (("footA", c["white"]), ("footB", c["navy"])):     # 鸳鸯过膝袜
        P = M.parts[nm]
        x0 = min(k[0] for k in P.vox)
        P.box(x0, x0 + 1, 0, 1, 1, 2, sock)
        P.put(x0, 0, 0, c["navy"]).put(x0, -1, 0, c["navy"])             # 魔术贴球鞋
    M.parts["footA"].face(1, 0, 1, FRONT, c["vio"])                      # 白袜口的紫条纹
    arms(M, c["white"], x=2, n=2)                                        # 白色泡泡短袖
    H = head(M, c["blue"], c["hi"], bangs=(-3, -2, 1, 2), bangs2=(-3, 2))
    H.tint(-4, 4, -3, 4, hz, hz + 3, c["vio"], only=c["blue"])           # 往下渐变成紫
    H.tint(-4, 4, -3, 4, hz, hz + 1, c["pink"], only=c["vio"])
    H.face(-3, -3, hz + 2, FRONT, c["amber"]).face(2, -3, hz + 2, FRONT, c["teal"])   # 脸侧内层的一点彩虹
    for sg in (1, -1):
        def X(x):
            return x if sg > 0 else -x - 1
        H.box(X(4), X(4) + 1, 0, 3, hz + 4, hz + 6, c["vio"])            # 丸子髻
        H.put(X(4), 1, hz + 6, c["vio"])
        for x in (1, 2, 3):                                              # 尖猫耳（和姐姐一样大）
            H.put(X(x), -1, hz + 7, c["blue"])
            H.put(X(x), 0, hz + 7, c["blue"])
        for x in (2, 3):
            H.put(X(x), -1, hz + 8, c["blue"])
        H.put(X(3), -1, hz + 9, c["blue"])
        for (x, z) in ((2, hz + 7), (3, hz + 7), (3, hz + 8)):
            H.face(X(x), -1, z, FRONT, c["ear_in"])
    H.put(4, -1, hz + 7, c["gold"])                                      # 耳边的小金铃
    for (x, z, k) in ((2, 5, "gold"), (2, 6, "teal"), (1, 5, "sky"), (3, 5, "pink_l"), (2, 4, "amber")):
        H.put(x, -4, hz + z, c[k])                                       # 炫彩四角星
    H.boxm(4, 5, -1, 3, hz, hz + 3, c["vio"])                            # 两侧蓬起的及肩发
    H.tint(-5, 5, -1, 3, hz, hz + 1, c["pink"], only=c["vio"])
    hair_back(M, c["vio"], -5, 5, z0=hz - 2, z1=hz + 3, wavy=True, tip=c["pink"], tip_rows=1)   # 后面只到肩下
    P = M.parts["hairback"]
    P.tint(-5, 5, 3, 5, hz + 1, hz + 3, c["blue"])
    T = M.part("tail", (0, 3, 2.5))                                      # 大猫尾：蓝 → 紫 → 玫粉（按个子缩一号）
    T.box(-1, 1, 3, 7, 2, 3, c["blue"])
    T.box(-1, 1, 5, 7, 3, 7, c["vio"])
    T.tint(-1, 1, 5, 7, 5, 7, c["pink"])
    T.boxm(1, 2, 5, 7, 5, 7, c["pink"])
    M.tail_swing = 1.3
    face(M, eye="#C2477E")
    team_scarf(M, team)
    M.pal = c
    return M
