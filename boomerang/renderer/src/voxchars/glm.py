# -*- coding: utf-8 -*-
"""GLM（正式形象 v2）：墨蓝黑微乱长发（到臀）、一根大弯钩呆毛，头侧一枚黑底白字的方形 Z 发夹，另一侧两颗冰蓝小星发卡；
敞怀过膝白大褂（萌袖），里面深炭黑落肩毛衣短装、胸前挂门禁卡（冰蓝进度条），黑半筒袜、黑面白绒边毛绒拖鞋；
手里端一只白马克杯。"""
from voxkit import Model, head, face, legs, arms, hair_back, team_scarf, FRONT, TOP

KEY = "glm"
LABEL = "GLM"
COLOR = "#8892A8"
NOTES = ("石板灰", ["墨蓝黑微乱长发 · 呆毛", "方形 Z 发夹 · 冰蓝星卡", "敞怀白大褂 · 黑毛衣 · 拖鞋"])
PAL = dict(hair="#1E2230", hair_hi="#3A4562", coat="#F0F3F8", coat_d="#C9D1DE", knit="#23262F", sock="#1E212B",
           ice="#7DD3FC", logo="#15161C", white="#FFFFFF", mug="#FAFAFA")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=6)
    hz = M.hz
    B = M.part("body")
    B.box(-3, 3, -1, 3, 1, 6, c["coat"]).chamfer(-3, 3, -1, 3, 1, 3)     # 过膝白大褂（敞怀）
    B.box(-2, 2, -2, 0, 2, 6, c["knit"])                                  # 深炭黑毛衣短装（露在敞口里）
    B.faces(-3, 3, -1, 0, 1, 6, FRONT, c["coat_d"])                       # 大褂敞口的翻边
    B.faces(-2, 2, -2, -1, 2, 3, FRONT, c["knit"])
    B.box(-1, 1, -3, -2, 4, 5, c["coat"])                                 # 门禁卡
    B.face(-1, -3, 4, FRONT, c["ice"])
    B.faces(-1, 1, -2, -1, 5, 6, FRONT, c["knit"])                        # 高领
    legs(M, c["sock"], c["sock"], sole=c["white"])                        # 黑半筒袜 + 黑面拖鞋
    for nm in ("footA", "footB"):                                         # 拖鞋口一圈蓬松白绒边（比脚宽一圈）
        P = M.parts[nm]
        x0 = min(k[0] for k in P.vox)
        P.slab(x0 - 0.22, x0 + 1.22, -1.22, 0.62, 0.55, 0.95, c["white"])
    arms(M, c["coat"], x=3, cuff=c["coat"])
    A = M.parts["armB"]                                                   # 白马克杯
    A.box(-5, -3, -1, 0, 3, 5, c["mug"])
    A.face(-5, -1, 4, FRONT, "#6B4A32")
    H = head(M, c["hair"], c["hair_hi"], bangs=(-3, -2, -1, 1, 2), bangs2=(-3, 2))
    H.boxm(4, 5, -1, 3, hz - 1, hz + 3, c["hair"])                        # 两侧蓬乱的头发
    H.put(0, 0, hz + 7, c["hair"]).put(0, 0, hz + 8, c["hair"]).put(1, 0, hz + 9, c["hair"])   # 大弯钩呆毛
    H.put(2, 0, hz + 9, c["hair"]).put(2, 0, hz + 8, c["hair"])
    H.box(2, 4, -4, -3, hz + 5, hz + 7, c["logo"])                        # 方形 Z 发夹（头侧，只有一枚）
    H.face(2, -4, hz + 6, FRONT, c["white"]).face(3, -4, hz + 5, FRONT, c["white"])
    H.put(-3, -4, hz + 5, c["ice"]).put(-2, -4, hz + 4, c["ice"])         # 另一侧两颗冰蓝小星
    hair_back(M, c["hair"], -5, 5, z0=2, wavy=True)
    face(M, eye="#4A5470")
    team_scarf(M, team)
    M.pal = c
    return M
