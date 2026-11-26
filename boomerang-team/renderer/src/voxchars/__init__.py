# -*- coding: utf-8 -*-
"""体素小人的角色定义：一人一个文件（<key>.py），互不依赖，只用 voxkit 里的积木。
每个文件给出 KEY（内部名）、LABEL（显示名）、COLOR（主色：脚下色环、镖、信息栏）、NOTES（阵容图上的三行说明）、
PAL（这个角色用到的全部颜色）和 build(team=None) -> voxkit.Model。
加新角色：照着抄一个文件，再把名字加进下面的 KEYS。"""
import importlib

KEYS = ["astra", "sol", "fable", "opus", "gemini", "grok", "kimi", "deepseek", "doubao", "qwen", "mimo", "glm"]
# 不在正赛 12 人名单里、但能用 --roster 点名上场 / 出预览的扩展角色
EXTRA = ["gemini_s"]
# 规则演示用的示范小人：不是选手，不进信息栏、阵容图；--roster 能点名
DEMO = ["demo_red", "demo_blue"]
ALL = KEYS + EXTRA + DEMO
_MODS = {}


def mod(key):
    if key not in _MODS:
        _MODS[key] = importlib.import_module("voxchars." + key)
    return _MODS[key]


def build(key, team=None):
    return mod(key).build(team)


def color(key):
    return mod(key).COLOR


def label(key):
    return mod(key).LABEL


def notes(key):
    return mod(key).NOTES
