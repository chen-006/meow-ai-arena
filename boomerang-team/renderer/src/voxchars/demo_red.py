# -*- coding: utf-8 -*-
"""演示用的红色示范小人（第 11 集 P5 规则演示）：深棕短发、红色连帽衫（帽子垂在背后、两根白抽绳、腹前口袋）、
深灰长裤、白鞋。没有名字、没有任何标志。不是选手，不进正赛名单（voxchars.DEMO）。
蓝色那个（demo_blue.py）用同一个 build_hoodie，只换颜色和发型细节。"""
from voxkit import Model, head, face, legs, arms, team_scarf, FRONT

KEY = "demo_red"
LABEL = "红"
COLOR = "#F04A3E"
NOTES = ("示范红", ["深棕短发", "红色连帽衫", "演示专用"])
PAL = dict(hair="#4A3328", hair_d="#33231C", hair_hi="#7A5644", hood="#E8483D", hood_d="#B8332B", hood_hi="#FF7A6E",
           pants="#3C4150", shoe="#F4F4F2", cord="#FFFFFF", eye="#3B2B3D")


def build_hoodie(key, c, team=None, bangs=(-3, -2, -1, 1, 2), cowlick=True):
    M = Model(key, hz=6)
    hz = M.hz
    legs(M, c["shoe"], c["pants"], h=2)
    B = M.part("body")
    B.box(-2, 2, -1, 2, 2, hz, c["hood"])                       # 卫衣身子 4 宽 × 3 深
    B.box(-2, 2, -1, 2, 2, 3, c["hood_d"])                      # 下摆罗纹
    B.faces(-1, 1, -1, 0, 3, 4, FRONT, c["hood_d"])             # 腹前口袋
    B.box(-2, 2, 2, 3, hz - 2, hz, c["hood_d"])                 # 帽子垂在背后
    B.box(-1, 1, 2, 3, hz - 3, hz - 2, c["hood_d"])
    B.slab(-0.62, -0.42, -1.12, -1.0, hz - 1.9, hz - 0.05, c["cord"])   # 两根抽绳
    B.slab(0.42, 0.62, -1.12, -1.0, hz - 1.9, hz - 0.05, c["cord"])
    B.slab(-1.98, 1.98, -1.1, -1.0, hz - 0.35, hz - 0.05, c["hood_hi"])  # 领口亮边
    arms(M, c["hood"], x=2, n=3, cuff=c["hood_d"])
    H = head(M, c["hair"], c["hair_hi"], bangs=bangs, bangs2=(-3, 2))
    H.tint(-4, 4, 0, 4, hz, hz + 2, c["hair_d"], only=c["hair"])          # 后脑勺下沿暗一号（短发收在脖子上）
    if cowlick:
        H.put(0, 0, hz + 7, c["hair"]).put(-1, 1, hz + 7, c["hair"])      # 头顶翘起的一小撮
    face(M, eye=c["eye"])
    team_scarf(M, team)
    M.pal = c
    return M


def build(team=None):
    return build_hoodie(KEY, PAL, team)
