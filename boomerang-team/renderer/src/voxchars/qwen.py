# -*- coding: utf-8 -*-
"""Qwen（正式形象 v2）：蓝紫色半挽大波浪长发，一条粗麻花辫垂在左肩前（看画面在右边，和团扇分在两侧），头侧一枚紫白六角发饰、另一侧一枚小银发梳；
月白立领盘扣短衫 + 深绀紫偏裁云肩（银流苏），深绀紫马面中长裙（裙门亮一号、银滚边），白丝袜、深紫单鞋；
手执烟紫圆团扇。"""
from voxkit import Model, head, face, legs, arms, hair_back, team_scarf, FRONT, TOP

KEY = "qwen"
LABEL = "千问"
COLOR = "#8A6AF2"
NOTES = ("长春花紫", ["蓝紫半挽长发 · 肩前粗辫", "紫白六角发饰", "深绀紫云肩 · 马面裙 · 团扇"])
PAL = dict(hair="#6A62E6", hair_d="#4E46C4", hair_hi="#9A90FA", indigo="#2A2470", skirt="#3B31A0", panel="#5A4CC8",
           blouse="#DCD6F6", pearl="#F4F2FA", silver="#D8DCE8", fan="#A99CF0", logo="#6F5CF2", shoe="#2A2470")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=6)
    hz = M.hz
    B = M.part("body")
    B.box(-3, 3, -2, 3, 1, 3, c["skirt"]).chamfer(-3, 3, -2, 3, 1, 3)    # 马面中长裙（到小腿肚）
    B.tint(-1, 1, -2, -1, 1, 3, c["panel"])                               # 平挺裙门
    B.slab(-3.07, 3.07, -2.07, 3.07, 1.0, 1.2, c["silver"])               # 银滚边
    B.box(-2, 2, -1, 2, 3, 6, c["blouse"])                                # 月白立领短衫
    B.tint(-2, 2, -1, 2, 3, 4, c["indigo"])                               # 高腰宽封腰
    B.face(0, -1, 3, FRONT, c["silver"])
    B.box(-3, 3, -1, 3, 5, 6, c["indigo"])                                # 偏裁云肩
    B.faces(-3, 3, -1, 3, 5, 6, TOP, c["indigo"])
    B.face(-1, -1, 5, FRONT, c["pearl"]).face(0, -1, 5, FRONT, c["pearl"])   # 立领
    B.face(1, -1, 4, FRONT, c["silver"]).face(-2, -1, 4, FRONT, c["silver"])  # 银流苏
    legs(M, c["shoe"], c["pearl"])
    arms(M, c["blouse"], x=2, cuff=c["indigo"])
    for nm, x0 in (("armA", 2), ("armB", -3)):
        M.parts[nm].tint(x0, x0 + 1, 0, 1, hz - 1, hz, c["indigo"])      # 云肩罩住的肩头
    A = M.parts["armB"]                                                   # 圆团扇（-X 那只手举着）
    A.box(-7, -4, 0, 1, 4, 7, c["fan"])
    A.cut(-7, -6, 0, 1, 4, 5).cut(-5, -4, 0, 1, 4, 5).cut(-7, -6, 0, 1, 6, 7).cut(-5, -4, 0, 1, 6, 7)
    A.face(-6, 0, 5, FRONT, c["pearl"])
    A.put(-4, 0, 3, c["indigo"]).put(-5, 0, 3, c["indigo"])               # 扇柄
    H = head(M, c["hair"], c["hair_hi"], bangs=(-3, -2, 1, 2), bangs2=(-3, 2))
    H.box(2, 4, -4, -3, hz + 5, hz + 7, c["logo"])                        # 六角发饰（头侧，只有一枚）
    H.face(2, -4, hz + 6, FRONT, c["pearl"]).face(3, -4, hz + 5, FRONT, c["pearl"])
    H.put(-4, 0, hz + 6, c["silver"]).put(-5, 0, hz + 6, c["silver"])    # 另一侧小银发梳
    T = M.part("tailL", (4, -2, hz + 0.5))                                # 肩前的粗麻花辫（+X，画面右边；团扇在另一边）
    T.box(3, 5, -3, -1, hz, hz + 2, c["hair"])                            # 从耳后绕到肩前
    for z in range(hz - 4, hz):
        T.box(3, 5, -3, -1, z, z + 1, c["hair_hi"] if (z - hz) % 2 else c["hair_d"])   # 一节亮一节暗，看得出是麻花辫
    T.box(3, 5, -3, -1, hz - 5, hz - 4, c["silver"])                      # 辫尾银坠
    hair_back(M, c["hair"], -5, 5, z0=1, wavy=True, shade=c["hair_d"], shade_rows=2)
    face(M, eye="#6A4FD0")
    team_scarf(M, team)
    M.pal = c
    return M
