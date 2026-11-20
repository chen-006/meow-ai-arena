# -*- coding: utf-8 -*-
"""Astra：薄荷绿的大波浪长发（比头宽出一圈）、头侧一只深绿大蝴蝶结；露肩白长裙、绿饰带。"""
from voxkit import Model, head, face, legs, arms, hair_back, team_scarf, FRONT

KEY = "astra"
LABEL = "Astra"
COLOR = "#12B886"
NOTES = ("翡翠绿", ["薄荷绿大波浪长发", "头侧深绿大蝴蝶结", "露肩白长裙 · 绿边"])
PAL = dict(hair="#48D8AA", hair_d="#1FAE86", hair_hi="#B6F6DE", bow="#0C8F68", white="#FFFFFF", trim="#12A87C",
           pale="#C9F2E1")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=6)
    hz = M.hz
    B = M.part("body")
    B.box(-4, 4, -2, 4, 1, 2, c["white"]).chamfer(-4, 4, -2, 4, 1, 2)
    B.slab(-4.07, 4.07, -2.07, 4.07, 1.0, 1.36, c["trim"])              # 裙底绿边
    B.box(-3, 3, -2, 3, 2, 3, c["pale"])
    B.box(-2, 2, -1, 2, 3, 6, c["white"])
    B.tint(-2, 2, -1, 2, 3, 4, c["trim"])                   # 腰带
    B.face(-1, -1, 5, FRONT, c["trim"]).face(0, -1, 5, FRONT, c["trim"])
    legs(M, c["white"], M.skin)
    arms(M, c["white"], x=2, bare_top=True)
    H = head(M, c["hair"], c["hair_hi"], bangs=(-3, 2), bangs2=(-3, 2))
    H.boxm(4, 5, -1, 3, hz, hz + 3, c["hair"])              # 两侧蓬出去的卷发
    H.box(-5, -3, 0, 2, hz + 6, hz + 9, c["bow"])           # 蝴蝶结（角色右侧）
    H.box(-2, 0, 0, 2, hz + 7, hz + 9, c["bow"])
    H.box(-3, -2, 0, 2, hz + 7, hz + 8, c["bow"])
    H.put(-3, -1, hz + 7, c["white"])
    hair_back(M, c["hair"], -5, 5, z0=1, z1=hz + 3, y0=3, y1=6, wavy=True, tip=c["hair_hi"], tip_rows=1,
              shade=c["hair_d"], shade_rows=3)
    M.hair_swing = 0.7
    face(M, eye="#1F6B4E")
    team_scarf(M, team)
    M.pal = c
    return M
