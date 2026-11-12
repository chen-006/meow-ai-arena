# -*- coding: utf-8 -*-
"""DeepSeek（v2，照 DeepSeek_大.png）：冰蓝白色的大波浪长发、呆毛，两侧低扎的辫子系蓝蝴蝶结，头顶别一只蓝鲸发夹；
白色水手服配深蓝领和蓝领巾，外面一件宽大的深蓝鲸鱼兜帽外套（白色罗纹边，兜帽堆在脖子后面、两边伸出小鳍），
藏青百褶短裙压白线，白长袜、深蓝皮鞋，腰边挂一只鲸鱼小包。"""
from voxkit import Model, head, face, legs, arms, hair_back, team_scarf, FRONT, TOP

KEY = "deepseek"
LABEL = "DeepSeek"
COLOR = "#2F5FE0"
NOTES = ("海蓝", ["冰蓝白大波浪长发", "蓝鲸发夹 · 低扎辫", "鲸鱼兜帽外套 · 百褶短裙"])
PAL = dict(hair="#D8E6FC", shade="#B5CBF2", tip="#94B4EE", jacket="#3159D8", jacket_d="#2443AE", navy="#22306E",
           white="#FFFFFF", scarf="#3E6BF0", shoe="#26357E", belly="#F2F6FF")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=7)
    hz = M.hz
    B = M.part("body")
    B.box(-3, 3, -2, 3, 3, 4, c["navy"])                                  # 藏青百褶短裙
    B.slab(-3.07, 3.07, -2.07, 3.07, 3.15, 3.38, c["white"])
    B.box(-2, 2, -2, 2, 4, 5, c["jacket"])                                # 外套下摆（罗纹白边）
    B.slab(-2.07, 2.07, -2.07, 2.07, 4.0, 4.3, c["white"])
    B.box(-2, 2, -1, 2, 5, 7, c["jacket"])                                # 外套
    B.faces(-1, 1, -2, 0, 4, 7, FRONT, c["white"])                        # 敞开露出的白水手服
    B.faces(-1, 1, -1, 0, 6, 7, FRONT, c["navy"])                         # 深蓝水手领
    B.face(-1, -1, 5, FRONT, c["scarf"]).face(0, -1, 5, FRONT, c["scarf"])  # 蓝领巾
    B.faces(-2, 2, -1, 2, 6, 7, TOP, c["navy"])
    B.box(-3, 3, 1, 3, 6, 7, c["jacket"])                                 # 脖子后面堆着的鲸鱼兜帽
    B.box(-4, -3, 2, 3, 5, 7, c["jacket_d"]).box(3, 4, 2, 3, 5, 7, c["jacket_d"])  # 兜帽两边的小鳍
    B.box(3, 4, -1, 1, 3, 4, c["jacket"])                                 # 腰边的鲸鱼小包
    B.face(3, -1, 3, FRONT, c["belly"])
    legs(M, c["shoe"], c["white"], h=3)
    for nm in ("footA", "footB"):
        P = M.parts[nm]
        x0 = min(k[0] for k in P.vox)
        P.put(x0, 0, 2, M.skin)                                           # 长袜上面露一截腿
    arms(M, c["jacket"], x=2, cuff=c["white"])
    H = head(M, c["hair"], None, bangs=(-3, -2, 0, 2), bangs2=(-3, 2))
    H.tint(-4, 4, 0, 4, hz, hz + 2, c["shade"], only=c["hair"])
    H.put(0, 0, hz + 7, c["hair"]).put(0, 0, hz + 8, c["hair"]).put(-1, 0, hz + 8, c["hair"])         # 呆毛
    # 头顶的蓝鲸发夹：鲸身横趴在头顶右前方，白肚皮朝前，尾鳍翘起
    H.box(1, 4, -2, 0, hz + 7, hz + 8, c["jacket"])
    H.box(1, 3, -2, -1, hz + 8, hz + 9, c["jacket"])
    H.faces(1, 4, -2, -1, hz + 7, hz + 8, FRONT, c["belly"])
    H.put(0, -2, hz + 8, c["jacket"]).put(0, -2, hz + 9, c["jacket_d"]).put(-1, -2, hz + 9, c["jacket_d"])
    H.put(3, -3, hz + 4, c["scarf"])                                      # 刘海上的小发卡
    for nm, sg in (("tailL", 1), ("tailR", -1)):                          # 两侧低扎的辫子，系蓝蝴蝶结，垂在肩前
        x0 = 4 if sg > 0 else -5
        T = M.part(nm, (4 * sg, -1, hz + 0.5))
        for z in range(hz - 4, hz + 1):
            T.box(x0, x0 + 1, -2, 0, z, z + 1, c["hair"] if z > hz - 3 else c["shade"])
            if (z - hz) % 2:
                T.box(x0 + sg, x0 + sg + 1, -2, 0, z, z + 1, c["hair"] if z > hz - 3 else c["shade"])
        xb = 4 if sg > 0 else -6
        T.box(xb, xb + 2, -2, -1, hz, hz + 1, c["scarf"])
    hair_back(M, c["hair"], -5, 5, z0=2, wavy=True, tip=c["tip"], tip_rows=1, shade=c["shade"], shade_rows=2)
    face(M, eye="#2A55D0")
    team_scarf(M, team)
    M.pal = c
    return M
