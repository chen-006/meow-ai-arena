# -*- coding: utf-8 -*-
"""飞行物和特效（方块的，和兵同一套材质）：箭、投石车的石头、爆炸（火球 + 烟）、尘土、碎木片。
网格只建一次，用的时候建一个物体指向它。爆炸的大小、透明随时间变化在回放出片时由 fx_state() 给出。"""
import math

import numpy as np

import voxkit as VK
from units import hsh

FX_UNIT = {"arrow": 0.05, "stone": 0.15, "boom": 0.25, "smoke": 0.25, "dust": 0.12, "splinter": 0.08, "shock": 0.2}


def m_arrow():
    M = VK.Model("arrow")
    P = M.part("body", (0, 0, 0))
    P.box(0, 1, -8, 8, 0, 1, "#B8874E")          # 箭杆 0.8 米（一格 0.05）
    P.box(0, 1, -10, -8, 0, 1, "#D8DEE6")
    P.put(0, -11, 0, "#D8DEE6")
    P.box(-1, 2, -9, -8, 0, 1, "#D8DEE6")
    P.box(0, 1, 6, 9, 1, 2, "#FFFFFF")           # 箭羽
    P.box(0, 1, 6, 9, -1, 0, "#FFFFFF")
    P.box(-1, 0, 6, 9, 0, 1, "#F2F2F2")
    P.box(1, 2, 6, 9, 0, 1, "#F2F2F2")
    # 飞行中的拖尾（插在地上以后不显示）：箭尾后面一道发亮的细线，全景里才看得见箭
    T = M.part("trail", (0, 0, 0))
    T.box(0, 1, 9, 30, 0, 1, "!#FFF6D8")
    T.box(-1, 2, 9, 16, 0, 1, "!#FFF6D8")
    M.meta_optional = ["trail"]
    return M


def m_stone():
    """250 公斤的石头（半径 0.45 米）：一团灰色方块。"""
    M = VK.Model("stone")
    P = M.part("body", (0, 0, 0))
    for x in range(-3, 3):
        for y in range(-3, 3):
            for z in range(-3, 3):
                d = math.sqrt((x + 0.5) ** 2 + (y + 0.5) ** 2 + (z + 0.5) ** 2)
                if d <= 3.0 - 0.6 * hsh(x, y, z):
                    P.put(x, y, z, "#8C8A86" if hsh(x, y, z, 2) > 0.3 else "#6E6C6A")
    for x in range(-2, 2):
        for y in range(-2, 2):
            if (x, y, 2) in P.vox:
                P.vox[(x, y, 2)] = "#B4B0A8"
    return M


def m_boom():
    """爆炸的火球：发光的黄、橙、红方块，中心最亮。半径约 1.6 米（一格 0.25）。"""
    M = VK.Model("boom")
    P = M.part("body", (0, 0, 0))
    for x in range(-7, 7):
        for y in range(-7, 7):
            for z in range(-6, 8):
                d = math.sqrt((x + 0.5) ** 2 + (y + 0.5) ** 2 + ((z + 0.5) * 0.9) ** 2)
                v = hsh(x, y, z)
                if d <= 6.4 - 2.2 * v:
                    c = "!#FFF1A8" if d < 2.6 else ("!#FFC23A" if d < 4.2 else ("!#FF7A1E" if v < 0.6 else "!#E8401C"))
                    P.put(x, y, z, c)
    return M


def m_smoke():
    M = VK.Model("smoke")
    P = M.part("body", (0, 0, 0))
    for x in range(-8, 8):
        for y in range(-8, 8):
            for z in range(-3, 10):
                d = math.sqrt((x + 0.5) ** 2 + (y + 0.5) ** 2 + ((z - 2.5) * 1.1) ** 2)
                v = hsh(x, y, z, 9)
                if 4.5 < d <= 8.2 - 2.5 * v:
                    P.put(x, y, z, "#9A939E" if v < 0.5 else "#C4BEC6")
    return M


def m_dust():
    M = VK.Model("dust")
    P = M.part("body", (0, 0, 0))
    for x in range(-5, 5):
        for y in range(-5, 5):
            for z in range(0, 4):
                d = math.sqrt((x + 0.5) ** 2 + (y + 0.5) ** 2 + (z * 1.4) ** 2)
                v = hsh(x, y, z, 4)
                if 2.5 < d <= 5.0 - 1.8 * v:
                    P.put(x, y, z, "#E8DCC4" if v < 0.5 else "#D4C6AA")
    return M


def m_splinter():
    """碎木片（木栅、木栅门、桥被打碎时）。"""
    M = VK.Model("splinter")
    P = M.part("body", (0, 0, 0))
    P.box(-3, 3, 0, 1, 0, 1, "#9A6A3E")
    P.box(-2, 2, 0, 1, 1, 2, "#C08A52")
    return M


BUILD = dict(arrow=m_arrow, stone=m_stone, boom=m_boom, smoke=m_smoke, dust=m_dust, splinter=m_splinter)


def build(kind):
    M = BUILD[kind]()
    M.unit = FX_UNIT[kind]
    M.meta = {"optional": getattr(M, "meta_optional", [])}
    return M


def boom_state(age):
    """爆炸开始后 age 秒：(火球缩放, 火球可见, 烟缩放, 烟可见)。火球 0.35 秒内胀大再缩没，烟慢慢散开升起。"""
    if age < 0:
        return 0, False, 0, False
    f = 0.35 + 1.2 * min(age / 0.12, 1.0) - 1.4 * max(0.0, age - 0.18)
    # 烟：0.4 秒胀到最大，之后边升边缩，1.3 秒散完（不长时间挡住画面）
    s = 0.75 * (0.6 + 0.8 * min(age / 0.4, 1.0) ** 0.5) * (1.0 - max(0.0, age - 0.5) / 0.8)
    return max(f, 0.0), f > 0.05, max(s, 0.0), 0 <= age < 1.3


# ================================================================ 粒子用的小模型（精修）
# 回放出片时的特效都是"粒子"：一小团方块，按简单的运动公式飞出去、长大、缩没（fxsys.py）。方块不透明，
# "烟散开"靠的是一团团小烟各自飘开、缩小，不是半透明。带 T 记号的颜色由物体颜色决定（尘土随地图换色、碎片分木 / 石 / 雪）。
def _blob(key, n, r, cols, seed, squash=1.0):
    M = VK.Model(key)
    P = M.part("body", (0, 0, 0))
    k = 0
    for x in range(-r, r + 1):
        for y in range(-r, r + 1):
            for z in range(-r, r + 1):
                d = math.sqrt(x * x + y * y + (z / squash) ** 2)
                v = hsh(x, y, z, seed)
                if d <= r * (0.55 + 0.5 * v):
                    P.put(x, y, z, cols[int(hsh(x, y, z, seed + 1) * len(cols)) % len(cols)])
                    k += 1
    return M


def m_p_smoke():
    return _blob("p_smoke", 0, 2, ("#5C5762", "#77717C", "#8E8894", "#6A6470"), 21)


def m_p_puff():
    return _blob("p_puff", 0, 2, ("T", "T", "T-", "T+"), 33, squash=0.8)


def m_p_fire():
    return _blob("p_fire", 0, 2, ("!#FFE9A0", "!#FFB030", "!#FF7A1E", "!#FFC850"), 45)


def m_p_flash():
    return _blob("p_flash", 0, 3, ("!#FFFBE0", "!#FFF0B0"), 57)


def m_p_spark():
    M = VK.Model("p_spark")
    P = M.part("body", (0, 0, 0))
    P.box(0, 1, -2, 3, 0, 1, "!#FFF2B0")
    return M


def m_p_chip():
    M = VK.Model("p_chip")
    P = M.part("body", (0, 0, 0))
    P.box(-2, 2, 0, 1, 0, 1, "T")
    P.box(-1, 2, 0, 1, 1, 2, "T+")
    P.put(-2, 1, 0, "T-")
    return M


def m_p_ring():
    """贴地的一圈冲击波（半径 2 米，用的时候放大）。"""
    M = VK.Model("p_ring")
    P = M.part("body", (0, 0, 0))
    for x in range(-11, 11):
        for y in range(-11, 11):
            d = math.hypot(x + 0.5, y + 0.5)
            if 8.6 < d <= 10.2 and hsh(x, y, 5) > 0.12:
                P.put(x, y, 0, "T+" if hsh(x, y, 6) > 0.5 else "T")
    return M


def m_p_slash():
    """兵器命中的一下闪光：一个发亮的四角星。"""
    M = VK.Model("p_slash")
    P = M.part("body", (0, 0, 0))
    P.box(-4, 5, 0, 1, 0, 1, "!#FFFFFF")
    P.box(0, 1, 0, 1, -4, 5, "!#FFFFFF")
    P.box(-1, 2, 0, 1, -1, 2, "!#FFF6C8")
    return M


def m_p_lump():
    """雪块（雪球碎开时）：白的一坨。"""
    return _blob("p_lump", 0, 2, ("#FFFFFF", "#EEF5FC", "#DCEAF6"), 71, squash=0.8)


FX_UNIT.update(p_smoke=0.2, p_puff=0.15, p_fire=0.17, p_flash=0.3, p_spark=0.07, p_chip=0.1, p_ring=0.2, p_slash=0.09,
               p_lump=0.16)
BUILD.update(p_smoke=m_p_smoke, p_puff=m_p_puff, p_fire=m_p_fire, p_flash=m_p_flash, p_spark=m_p_spark, p_chip=m_p_chip,
             p_ring=m_p_ring, p_slash=m_p_slash, p_lump=m_p_lump)
