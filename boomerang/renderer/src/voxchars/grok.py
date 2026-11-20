# -*- coding: utf-8 -*-
"""Grok（正式形象 v2）：头顶两侧高扎的璀璨金色蓬松大波浪双马尾（垂过臀），两根马尾根各系一只纯黑缎带蝴蝶结，
额前一侧一枚黑底白圈的 Ø 发夹；短款黑皮机车夹克、白衬衫歪系暗红领带、左臂暗红袖章，
黑色短裙裤（一侧拼暗红格纹），过膝黑丝（一条腿有破洞）、厚底黑马丁靴；手里一把黑银短戒尺。"""
from voxkit import Model, head, face, legs, arms, team_scarf, FRONT, PX

KEY = "grok"
LABEL = "Grok"
COLOR = "#FFD43B"
NOTES = ("金", ["金色大波浪双马尾", "黑蝴蝶结 · Ø 发夹", "黑皮机车夹克 · 红领带"])
PAL = dict(gold="#F5C23E", gold_d="#D9A21E", tip="#FFE07A", ink="#18181B", ink_l="#2E2E34", red="#991B1B",
           plaid="#B52A2A", white="#F4F4F6", silver="#CBD5E1", sole="#55555C")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=6)
    hz = M.hz
    B = M.part("body")
    B.box(-3, 3, -2, 3, 3, 4, c["ink"]).chamfer(-3, 3, -2, 3, 3, 4)       # 短裙裤
    B.tint(0, 3, -2, 3, 3, 4, c["plaid"])                                 # +X 一侧拼暗红格纹
    for (x, y) in ((1, -2), (2, -1), (2, 1)):
        B.face(x, y, 3, FRONT, c["ink"])
    B.box(-2, 2, -1, 2, 4, 6, c["ink"])                                   # 黑皮机车夹克
    B.faces(-1, 1, -1, 0, 4, 6, FRONT, c["white"])                        # 敞开露白衬衫
    B.face(0, -1, 5, FRONT, c["red"]).face(0, -1, 4, FRONT, c["red"])      # 歪系的红领带
    B.face(-1, -1, 4, FRONT, c["silver"])                                 # 银星腰链
    legs(M, c["ink"], c["ink"], h=3, shoe_h=1, sole=c["sole"])           # 过膝黑丝 + 厚底马丁靴
    A = M.parts["footA"]
    A.face(1, 0, 2, FRONT, M.skin)                                        # 一条腿的破洞
    arms(M, c["ink"], x=2)
    M.parts["armA"].face(2, 0, 5, PX, c["red"]).face(2, 0, 4, PX, c["red"])   # 左臂暗红袖章
    G = M.parts["armB"]                                                   # 黑银短戒尺
    G.box(-3, -2, -1, 0, 0, 3, c["silver"])
    G.put(-3, -1, 3, c["ink"])
    H = head(M, c["gold"], c["tip"], bangs=(-3, -2, -1, 1, 2), bangs2=(-3, 2))
    H.put(0, 0, hz + 7, c["gold"]).put(1, 0, hz + 8, c["gold"])          # 呆毛
    H.box(2, 4, -4, -3, hz + 4, hz + 6, c["ink"])                         # Ø 发夹（额前一侧，只有一枚）
    H.face(2, -4, hz + 5, FRONT, c["white"]).face(3, -4, hz + 4, FRONT, c["white"])
    for sg in (1, -1):
        def X(x):
            return x if sg > 0 else -x - 1
        for (x, z) in ((4, 6), (4, 7), (5, 7), (5, 5), (4, 5), (3, 6)):  # 马尾根的黑缎带蝴蝶结
            H.put(X(x), 1, hz + z, c["ink"])
        H.put(X(4), 0, hz + 6, c["ink"])
        T = M.part("tailL" if sg > 0 else "tailR", (5 * sg, 1, hz + 5.5))
        for z in range(1, hz + 6):                                        # 蓬松大波浪：一层往外鼓、一层往里收
            w = 1 if (z % 2 == 0) else 0
            for x in range(4, 7 + w):
                for y in (0, 1, 2):
                    if x == 6 + w and y != 1:
                        continue
                    col = c["gold"] if z > 2 else c["tip"]
                    if z % 4 == 1 and x >= 6:
                        col = c["gold_d"]
                    T.put(X(x), y, z, col)
    face(M, eye="#2BA8E8")
    team_scarf(M, team)
    M.pal = c
    return M
