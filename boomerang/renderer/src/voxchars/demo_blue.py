# -*- coding: utf-8 -*-
"""演示用的蓝色示范小人（第 11 集 P5 规则演示）：黑色短发（刘海往另一边分）、蓝色连帽衫、深灰长裤、白鞋。
没有名字、没有任何标志。造型积木和红色那个共用（demo_red.build_hoodie）。"""
from voxchars.demo_red import build_hoodie

KEY = "demo_blue"
LABEL = "蓝"
COLOR = "#3D7EFF"
NOTES = ("示范蓝", ["黑色短发", "蓝色连帽衫", "演示专用"])
PAL = dict(hair="#262A33", hair_d="#181B22", hair_hi="#4A5263", hood="#3B78F0", hood_d="#2856B8", hood_hi="#7FB0FF",
           pants="#3C4150", shoe="#F4F4F2", cord="#FFFFFF", eye="#2B3550")


def build(team=None):
    return build_hoodie(KEY, PAL, team, bangs=(-3, -2, 0, 1, 2), cowlick=False)
