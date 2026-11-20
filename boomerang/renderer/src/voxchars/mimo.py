# -*- coding: utf-8 -*-
"""MiMo（正式形象 v2）：蓬松齐下巴的黑巧棕波波短发、发尾外翘，额前一侧一缕亮橙挑染，另一侧一枚橙底白字的 MI 发夹；
宽松短款白色连帽冲锋衣（橙色肩袖织带），兜帽上一对越过肩头垂到身体两侧的长兔耳（外白内橙），颈挂白橙耳机（橙圈白芯），
黑色立领内搭、黑色工装运动短裤系橙色腰带，一条腿过膝白橙条纹袜、一条腿短袜，白橙跑鞋。"""
from voxkit import Model, head, face, legs, arms, team_scarf, FRONT, PX, NX, PY

KEY = "mimo"
LABEL = "MiMo"
COLOR = "#FF6900"
NOTES = ("小米橙", ["黑巧棕波波头 · 橙挑染", "MI 发夹 · 兔耳兜帽", "白冲锋衣 · 黑工装短裤"])
PAL = dict(hair="#3B261E", hair_hi="#5A3A2C", orange="#FF6900", white="#F5F6F8", ink="#22252D", sole="#3A3A40")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=6)
    hz = M.hz
    B = M.part("body")
    B.box(-3, 3, -2, 3, 3, 4, c["ink"])                                   # 黑色工装短裤
    B.face(-3, 0, 3, NX, c["orange"]).face(2, 0, 3, PX, c["orange"])      # 裤侧橙条
    B.box(-3, 3, -1, 3, 4, 6, c["white"])                                 # 短款白冲锋衣
    B.faces(-1, 1, -1, 0, 4, 6, FRONT, c["ink"])                          # 敞开露黑内搭
    B.slab(-1.5, 1.5, -2.08, -0.9, 3.78, 4.0, c["orange"])                # 机能编织腰带
    B.box(-3, 3, 3, 4, 5, 6, c["white"])                                  # 堆在背后的兜帽
    for sg in (1, -1):                                                    # 兜帽上的一对长兔耳：越过肩头垂到两侧（外白内橙）
        x0 = 4 if sg > 0 else -5
        B.box(x0, x0 + 1, 1, 4, 6, 7, c["white"])                         # 耳根：从兜帽两角伸出来
        B.box(x0, x0 + 1, -1, 1, 1, 7, c["white"])                        # 垂下的长耳朵（到大腿）
        B.cut(x0, x0 + 1, -1, 0, 1, 2)                                    # 耳尖圆一点
        B.faces(x0, x0 + 1, -1, 0, 2, 7, FRONT, c["orange"])              # 正面亮橙内侧
    B.slab(-2.3, 2.3, -2.12, -1.9, 5.5, 5.75, "#3A3A40")                  # 颈挂耳机：头梁
    for cx in (-2.75, 1.55):                                              # 两只白橙耳罩（橙圈白芯）
        B.slab(cx, cx + 1.2, -2.4, -1.0, 4.75, 5.95, c["orange"])
        B.slab(cx + 0.3, cx + 0.9, -2.48, -2.3, 5.05, 5.65, c["white"])
    legs(M, c["white"], M.skin, h=3, sole=c["sole"])
    P = M.parts["footB"]                                                  # -X：过膝白橙条纹长袜
    P.box(-2, -1, 0, 1, 1, 3, c["white"])
    P.face(-2, 0, 2, FRONT, c["orange"]).face(-2, 0, 1, FRONT, c["orange"])
    M.parts["footA"].face(1, 0, 0, FRONT, c["orange"])                   # +X：短袜 + 跑鞋橙道
    M.parts["footA"].face(1, 0, 2, PX, c["ink"])                          # 大腿上的小战术包
    arms(M, c["white"], x=3, cuff=c["ink"])
    M.parts["armA"].face(3, 0, 5, PX, c["orange"]).face(3, 0, 4, PX, c["orange"])   # 袖子橙织带
    M.parts["armB"].face(-4, 0, 5, NX, c["orange"]).face(-4, 0, 4, NX, c["orange"])
    H = head(M, c["hair"], c["hair_hi"], bangs=(-3, -2, -1, 1, 2), bangs2=(-3, 2))
    H.tint(-2, -1, -3, -2, hz + 3, hz + 6, c["orange"])                   # 额前一缕亮橙挑染（-X 一侧）
    H.tint(-2, -1, -2, 0, hz + 6, hz + 7, c["orange"])
    H.boxm(4, 5, -1, 2, hz, hz + 2, c["hair"])                            # 齐下巴、往外翘的发尾
    H.boxm(4, 5, 2, 3, hz + 1, hz + 3, c["hair"])
    H.box(-3, 3, 3, 4, hz - 1, hz, c["hair"])                             # 后颈的发尾
    H.box(2, 4, -4, -3, hz + 5, hz + 7, c["orange"])                      # MI 发夹（头侧，只有一枚）
    H.face(2, -4, hz + 5, FRONT, c["white"]).face(3, -4, hz + 6, FRONT, c["white"])
    face(M, eye="#C2660E")
    team_scarf(M, team)
    M.pal = c
    return M
