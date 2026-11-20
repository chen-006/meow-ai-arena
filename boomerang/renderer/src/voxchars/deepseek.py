# -*- coding: utf-8 -*-
"""DeepSeek（正式形象 v2）：宝蓝 → 天青渐变的大波浪长发、一根呆毛，头侧一枚蓝白小鲸鱼发夹，两侧横着一对浅青鲸鳍耳，
身后一条上翘分叉的大鲸尾（宝蓝背、白肚）；白色水手衬衫 + 宝蓝波浪边水手小披肩（胸前一颗橙海星），
深藏青高腰花苞灯笼短裤，白中筒袜、藏青乐福鞋；腰侧斜挎一只白米饭团小包。"""
from voxkit import Model, head, face, legs, arms, hair_back, team_scarf, FRONT, TOP

KEY = "deepseek"
LABEL = "DeepSeek"
COLOR = "#1B4FD8"
NOTES = ("宝蓝", ["宝蓝渐天青长发 · 鲸鱼发夹", "鲸鳍耳 · 上翘鲸尾", "水手披肩 · 灯笼短裤"])
PAL = dict(hair="#1F52DC", mid="#2C7FEA", tip="#45C2F5", hi="#5C86FF", fin="#5CC6F2", fin_d="#2E8FD8",
           whale="#1F52DC", belly="#F5F8FC", white="#F5F8FC", navy="#172B5C", orange="#FF8A2B", nori="#1E2228")


def build(team=None):
    c = PAL
    M = Model(KEY, hz=6)
    hz = M.hz
    B = M.part("body")
    B.box(-3, 3, -2, 3, 3, 4, c["navy"]).chamfer(-3, 3, -2, 3, 3, 4)      # 花苞灯笼短裤（鼓出去）
    B.slab(-2.6, 2.6, -2.07, 3.07, 3.0, 3.22, c["white"])                 # 裤口白道
    B.box(-2, 2, -1, 2, 4, 6, c["white"])                                 # 水手衬衫
    B.tint(-2, 2, -1, 2, 4, 5, c["navy"])                                 # 高腰
    B.face(-2, -1, 4, FRONT, c["white"]).face(1, -1, 4, FRONT, c["white"])  # 背带扣
    B.box(-3, 3, -1, 3, 5, 6, c["hair"])                                  # 宝蓝水手小披肩
    B.slab(-3.07, 3.07, -1.07, 3.07, 5.0, 5.2, c["white"])                # 披肩白边
    B.face(-1, -1, 5, FRONT, c["white"]).face(0, -1, 5, FRONT, c["orange"])   # 领口 + 橙海星
    B.box(2, 4, -2, 0, 3, 5, c["white"])                                  # 白米饭团斜挎包（+X 腰侧）
    B.tint(2, 4, -2, 0, 3, 4, c["nori"])                                  # 海苔
    B.face(2, -2, 4, FRONT, c["nori"])
    legs(M, c["navy"], M.skin, h=3)
    for nm in ("footA", "footB"):                                         # 白中筒袜
        P = M.parts[nm]
        x0 = min(k[0] for k in P.vox)
        P.put(x0, 0, 1, c["white"])
    M.parts["footA"].face(min(k[0] for k in M.parts["footA"].vox), 0, 1, FRONT, c["hair"])   # 袜子上的小蓝鲸
    arms(M, c["white"], x=2, cuff=c["hair"])
    H = head(M, c["hair"], c["hi"], bangs=(-3, -2, 0, 2), bangs2=(-3, 2))
    H.tint(-4, 4, -3, 4, hz, hz + 2, c["mid"], only=c["hair"])            # 发色往下渐浅
    H.put(0, 0, hz + 7, c["hair"]).put(1, 0, hz + 8, c["hair"])           # 呆毛
    H.box(2, 4, -4, -3, hz + 5, hz + 6, c["whale"])                       # 小鲸鱼发夹（头侧，只有一枚）
    H.put(3, -4, hz + 6, c["fin"])                                        # 喷的水
    H.face(2, -4, hz + 5, FRONT, c["belly"])
    for sg in (1, -1):                                                    # 鲸鳍耳：横着往外伸，尖朝后上
        def X(x):
            return x if sg > 0 else -x - 1
        for y in (-1, 0, 1):
            H.put(X(4), y, hz + 3, c["fin"])
        H.put(X(5), 0, hz + 3, c["fin"]).put(X(5), 1, hz + 3, c["fin"]).put(X(5), 1, hz + 4, c["fin_d"])
        H.put(X(6), 1, hz + 4, c["fin_d"]).put(X(6), 2, hz + 4, c["fin_d"])
    hair_back(M, c["hair"], -5, 5, z0=2, wavy=True, tip=c["tip"], tip_rows=2, shade=c["mid"], shade_rows=2)
    T = M.part("tail", (0, 3, 3.5))                                       # 鲸尾：从腰后伸向 +X 一侧、往上翘、末端分叉
    T.box(0, 4, 4, 6, 3, 4, c["whale"])
    T.box(3, 6, 5, 7, 3, 5, c["whale"])
    T.box(5, 7, 5, 7, 5, 8, c["whale"])
    T.faces(0, 7, 4, 7, 3, 4, 5, c["belly"])                              # 肚皮白（底面）
    T.faces(5, 7, 5, 6, 5, 8, FRONT, c["belly"])
    T.box(4, 8, 5, 7, 8, 9, c["whale"])                                   # 分叉的尾鳍
    T.put(3, 5, 9, c["whale"]).put(3, 6, 9, c["whale"]).put(8, 5, 9, c["whale"]).put(8, 6, 9, c["whale"])
    T.faces(4, 8, 5, 6, 8, 9, FRONT, c["belly"])
    M.tail_rest = 0.0
    face(M, eye="#1E4FD0")
    team_scarf(M, team)
    M.pal = c
    return M
