# -*- coding: utf-8 -*-
"""豆包：保持写实气质——深棕齐耳短发、侧分刘海，不带兽耳和夸张饰物，只有头顶一枚红豆发夹；
黑短外套、深色短裙压一道红边，黑短靴，腿长、身形比别人高挑。"""
from voxkit import Model, head, face, legs, arms, team_scarf, FRONT

KEY = "doubao"
LABEL = "豆包"
COLOR = "#F0504A"
NOTES = ("豆红", ["深棕齐耳短发", "头顶一枚红豆发夹", "黑外套 · 红裙边"])
PAL = dict(hair="#5A3B2C", hair_hi="#7C5642", jacket="#2A2A30", skirt="#3A3A44", inner="#56565F", red="#E8473F",
           boot="#1F1F24", pin="#F08A3C")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=7)
    hz = M.hz
    B = M.part("body")
    B.box(-3, 3, -2, 3, 3, 4, c["skirt"])
    B.slab(-3.07, 3.07, -2.07, 3.07, 3.0, 3.42, c["red"])        # 红裙边
    B.box(-2, 2, -1, 2, 4, 7, c["jacket"])
    B.tint(-2, 2, -1, 2, 4, 5, c["skirt"])
    B.faces(-1, 1, -1, 0, 5, 7, FRONT, c["inner"])               # 外套敞开露出的内搭
    B.face(-2, -1, 6, FRONT, c["pin"])                           # 胸前的豆子别针
    legs(M, c["boot"], M.skin, h=3, shoe_h=2)
    arms(M, c["jacket"], x=2)
    H = head(M, c["hair"], c["hair_hi"], bangs=(-3, -2, -1, 2))
    H.box(1, 3, -2, -1, hz + 7, hz + 8, c["red"])                # 红豆发夹
    face(M, eye="#3A2A22")
    team_scarf(M, team)
    M.pal = c
    return M
