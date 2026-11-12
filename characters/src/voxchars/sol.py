# -*- coding: utf-8 -*-
"""Sol（v2，照 GPT_小_Sol.png）：个子最小。极浅的薄荷色短发、呆毛、两侧低扎的小辫系绿蝴蝶结；
头侧一枚绿白相间的大圆形发饰 + 绿蝴蝶结；绿开衫、白水手领、绿领结，白色短裙压一道绿边，白袜绿鞋。"""
from voxkit import Model, head, face, legs, arms, team_scarf, FRONT, TOP

KEY = "sol"
LABEL = "Sol"
COLOR = "#3DBE78"
NOTES = ("薄荷绿", ["浅薄荷短发 · 呆毛", "头侧绿白大发饰", "绿开衫 · 白水手裙"])
PAL = dict(hair="#D3F2E0", hair_d="#A6DDBF", green="#2EAF66", card="#4CB878", white="#FFFFFF", stripe="#3DBE78",
           shoe="#2E9E5E")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=5)
    hz = M.hz
    B = M.part("body")
    B.box(-3, 3, -2, 3, 2, 3, c["white"])                                # 白短裙
    B.slab(-3.06, 3.06, -2.06, 3.06, 2.12, 2.36, c["stripe"])            # 裙边绿线
    B.box(-2, 2, -1, 2, 3, 5, c["card"])                                 # 绿开衫
    B.faces(-1, 1, -1, 0, 3, 5, FRONT, c["white"])                       # 里面的白裙
    B.faces(-2, 2, -1, 2, 4, 5, TOP, c["white"])                         # 白水手领（肩上）
    B.face(-2, -1, 4, FRONT, c["white"]).face(1, -1, 4, FRONT, c["white"])
    B.face(-1, -1, 4, FRONT, c["green"]).face(0, -1, 4, FRONT, c["green"])  # 绿领结
    legs(M, c["shoe"], c["white"])
    arms(M, c["card"], x=2, n=2)
    H = head(M, c["hair"], None, bangs=(-3, -2, -1, 2), bangs2=(-3, 2))
    H.tint(-4, 4, 0, 4, hz, hz + 2, c["hair_d"], only=c["hair"])         # 后脑勺下沿暗一号
    H.put(0, 0, hz + 7, c["hair"]).put(0, 0, hz + 8, c["hair"]).put(-1, 0, hz + 8, c["hair"])  # 呆毛
    # 头侧的大圆发饰：4 × 4 去角，外圈白、里面绿，上沿冒出头顶
    for x in range(1, 5):
        for z in range(hz + 4, hz + 8):
            if (x in (1, 4)) and (z in (hz + 4, hz + 7)):
                continue
            inner = x in (2, 3) and z in (hz + 5, hz + 6)
            H.put(x, -4, z, c["green"] if inner else c["white"])
    H.face(2, -4, hz + 5, FRONT, c["white"]).face(3, -4, hz + 6, FRONT, c["white"])  # 发饰上的白纹
    H.box(4, 5, -3, -1, hz + 3, hz + 4, c["green"])                      # 发饰下的绿蝴蝶结
    H.put(5, -2, hz + 3, c["green"])
    for nm, sg in (("tailL", 1), ("tailR", -1)):                         # 两侧低扎的小辫
        x0 = 4 if sg > 0 else -6
        T = M.part(nm, (4 * sg, 1, hz + 1.5))
        T.box(x0, x0 + 2, 0, 2, hz - 1, hz + 2, c["hair"])
        T.cut(x0 if sg > 0 else x0 + 1, (x0 if sg > 0 else x0 + 1) + 1, 0, 2, hz - 1, hz)
        xb = 4 if sg > 0 else -5
        T.box(xb, xb + 1, -1, 2, hz + 2, hz + 3, c["green"])
    face(M, eye="#1F8A55")
    team_scarf(M, team)
    M.pal = c
    return M
