# -*- coding: utf-8 -*-
"""Kimi（正式形象 v2）：珍珠月白的姬发切超长卷发（头顶压一层银灰、发尾冰蓝挑染），头顶正中一顶纯银星芒小王冠（中间一颗蓝宝石），
头侧一枚黑底白字的方形 K 发夹（缀一点蓝）；不对称月白观星短披肩，午夜深靛及膝连衣裙（裙底一圈银色月相），
珍珠白过膝长袜、深靛玛丽珍鞋；手里托着一卷垂到脚边、在地上盘卷的超长羊皮文稿。"""
from voxkit import Model, head, face, legs, arms, hair_back, team_scarf, FRONT, TOP, PX, NX, PY

KEY = "kimi"
LABEL = "Kimi"
COLOR = "#5DC8F8"
NOTES = ("冰蓝", ["月白长卷发 · 冰蓝发尾", "银星芒小王冠 · K 发夹", "深靛及膝裙 · 垂地长卷"])
PAL = dict(hair="#EEF2FA", shade="#CBD5E1", tip="#9AD8FA", navy="#18224B", navy_l="#26336E", cape="#F1F5FB",
           silver="#A9B4C8", gem="#1D4ED8", ink="#16171D", white="#FFFFFF", sock="#F4F6FB", scroll="#F2E8D2",
           scroll_d="#D9CBAA")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=6)
    hz = M.hz
    B = M.part("body")
    B.box(-3, 3, -2, 3, 2, 3, c["navy_l"]).chamfer(-3, 3, -2, 3, 2, 3)   # 及膝风琴裙
    B.box(-3, 3, -2, 3, 2, 3, c["navy_l"])
    B.cut(-3, -2, -2, -1, 2, 3).cut(2, 3, -2, -1, 2, 3)
    for (x, y, d) in ((-2, -2, FRONT), (0, -2, FRONT), (2, -1, PX), (-3, 1, NX), (1, 2, PY)):
        B.face(x, y, 2, d, c["silver"])                                   # 裙底银色月相
    B.box(-2, 2, -1, 2, 3, 6, c["navy"])
    B.box(-3, 2, -1, 3, 5, 6, c["cape"])                                  # 不对称月白短披肩（偏 -X 一侧）
    B.box(-3, -1, -1, 3, 4, 5, c["cape"])
    B.face(0, -1, 4, FRONT, c["silver"])                                  # 弯月领扣
    legs(M, c["navy"], c["sock"])                                          # 珍珠白过膝长袜 + 深靛鞋
    M.parts["footA"].face(1, 0, 1, FRONT, c["navy"])                      # 右腿袜口深靛结
    arms(M, c["navy"], x=2, cuff=c["white"])
    M.parts["armB"].tint(-3, -2, 0, 1, hz - 1, hz, c["cape"])            # 披肩罩住的肩头
    A = M.parts["armB"]                                                   # 垂到地上盘卷的长卷
    A.box(-5, -3, -1, 0, 1, 4, c["scroll"])
    A.box(-5, -3, -1, 0, 4, 5, c["scroll_d"])
    A.box(-6, -3, -3, 0, 0, 1, c["scroll"])
    A.box(-6, -5, -3, -2, 1, 2, c["scroll_d"])
    for z in (1, 2, 3):
        A.face(-5, -1, z, FRONT, c["scroll_d"])
    H = head(M, c["hair"], None, bangs=(-3, -2, -1, 0, 1, 2), bangs2=(-3, 2))
    H.tint(-3, 3, -2, 3, hz + 6, hz + 7, c["shade"])                      # 头顶一层银灰
    H.boxm(3, 4, -3, -1, hz - 2, hz + 3, c["hair"])                       # 姬发切：脸两侧齐剪的长鬓
    H.tint(-4, 4, -3, -1, hz - 2, hz - 1, c["tip"], only=c["hair"])
    for x in (-2, -1, 0, 1):                                              # 纯银星芒小王冠（头顶正中，只有一顶）
        H.put(x, -2, hz + 7, c["silver"])
    H.put(-2, -2, hz + 8, c["silver"]).put(1, -2, hz + 8, c["silver"]).put(-1, -2, hz + 8, c["gem"])
    H.face(0, -2, hz + 7, FRONT, c["gem"])
    H.box(3, 5, -2, 0, hz + 4, hz + 6, c["ink"])                          # 方形 K 发夹（头侧，只有一枚）
    H.face(3, -2, hz + 5, FRONT, c["white"]).face(4, -2, hz + 4, FRONT, c["white"]).face(4, -2, hz + 5, FRONT, c["gem"])
    hair_back(M, c["hair"], -5, 5, z0=1, wavy=True, tip=c["tip"], tip_rows=2, shade=c["shade"], shade_rows=1)
    face(M, eye="#6A60C8")
    team_scarf(M, team)
    M.pal = c
    return M
