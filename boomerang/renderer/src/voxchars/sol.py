# -*- coding: utf-8 -*-
"""Sol：个子最小。极浅的薄荷色短发、头顶一根呆毛、两侧各一撮扎绿结的小辫、刘海上一枚绿发夹；绿开衫、白短裙、白袜绿鞋。"""
from voxkit import Model, head, face, legs, arms, team_scarf, FRONT

KEY = "sol"
LABEL = "Sol"
COLOR = "#7FE0A8"
NOTES = ("浅薄荷", ["浅薄荷短发 · 呆毛", "两侧小辫 · 绿发夹", "绿开衫 · 个子最小"])
PAL = dict(hair="#C2F2D8", hair_d="#93DDB8", green="#2FB573", card="#5FCB8C", white="#FFFFFF", stripe="#8FDDB0")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=5)
    hz = M.hz
    B = M.part("body")
    B.box(-3, 3, -2, 3, 2, 3, c["white"])
    B.slab(-3.06, 3.06, -2.06, 3.06, 2.2, 2.5, c["stripe"])
    B.box(-2, 2, -1, 2, 3, 5, c["card"])
    B.faces(-1, 1, -1, 0, 3, 5, FRONT, c["white"])          # 开衫里的白衣
    B.face(-1, -1, 4, FRONT, c["green"]).face(0, -1, 4, FRONT, c["green"])  # 领结
    legs(M, c["green"], c["white"])
    arms(M, c["card"], x=2, n=2)
    H = head(M, c["hair"], None, bangs=(-3, -2, -1, 2), bangs2=(-3, 2))
    H.tint(-4, 4, 0, 4, hz, hz + 2, c["hair_d"], only=c["hair"])    # 后脑勺下沿暗一号
    H.put(0, 0, hz + 7, c["hair"]).put(1, 0, hz + 8, c["hair"])     # 呆毛
    H.box(1, 3, -4, -3, hz + 4, hz + 6, c["green"])                 # 发夹
    for nm, sg in (("tailL", 1), ("tailR", -1)):
        x0 = 4 if sg > 0 else -6
        T = M.part(nm, (4 * sg, 1, hz + 1.5))
        T.box(x0, x0 + 2, 0, 2, hz, hz + 2, c["hair"])
        xb = 4 if sg > 0 else -5
        T.box(xb, xb + 1, 0, 2, hz + 2, hz + 3, c["green"])
    face(M, eye="#1F8A55")
    team_scarf(M, team)
    M.pal = c
    return M
