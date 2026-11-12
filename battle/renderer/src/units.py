# -*- coding: utf-8 -*-
"""《纸上谈兵》的体素兵种和道具（纯 Python + numpy，不依赖 Blender）。

每个兵种 / 道具 = 一个 voxkit.Model：若干可动部件，每个部件是一堆整数格子上的方块。
坐标（单位 = 一格）：脚底中心在原点，面朝 -Y，Z 向上，+X 是角色的左手边。
颜色可以写记号：T / T- / T-- / T+ = 队伍色（渲染时乘物体颜色，同一份网格四个队共用），"!#RRGGBB" = 自发光。

比例原则（全景里要看得清）：
  * 头小（4 格宽，身体 6 格宽），队伍色占身体最大的面（上衣、裙摆、盾面、马披、旗）；
  * 兵种靠一件夸张的大件认：新兵 = 斗笠、盾卫 = 大方盾、长枪兵 = 竖着的长枪带队伍色枪缨、弓手 = 大弓 + 背上箭袋、
    骑兵 = 马、大锤 = 扛在肩上的大铁锤、炮仗兵 = 举过头顶的火药桶（和场上的火药桶同一个样子）、投石车、大块头 = 巨人。
部件名（装配和动作认这些名字）：
  body 身体（含头）  footA / footB 两条腿（+X / -X）  armA / armB 两只手臂（+X 左手 = 盾、弓；-X 右手 = 兵器）
  wpnA / wpnB 手里的东西（挂在手上，枢轴在手心）  keg 炮仗兵头顶的桶（挂在身体上）
  骑兵另有 horse 马身、hlegFA hlegFB hlegBA hlegBB 四条马腿；投石车 base / arm；镇山兽 body / head / 四条腿。
"""
import math
import os

import numpy as np

import voxkit as VK
from voxkit import Model, VPart

SU = 0.1            # 普通兵一格 = 0.1 米
SKIN = "#F2C9A0"
SKIN_D = "#D9A57C"
EYE = "#2B2230"
WOOD = "#9A6A3E"
WOOD_D = "#6B4426"
WOOD_L = "#C08A52"
STEEL = "#B9C2CC"
STEEL_D = "#7D8794"
STEEL_L = "#E6ECF2"
IRON = "#4A505C"
IRON_L = "#7E8796"
LEATHER = "#7A4A2A"
LEATHER_D = "#553220"
PANTS = "#4E4656"
BOOTS = "#3A2C26"
GOLD = "#F0C040"
KEG = "#E58A2C"          # 火药桶：橙色桶身（和设计稿配图一致）
KEG_D = "#B8621C"
HOOP = "#3E2A1E"
SPARK = "!#FFE27A"
SPARK2 = "!#FF9A2E"

KINDS = ["militia", "shield", "spear", "archer", "cavalry", "hammer", "bomber", "catapult", "giant"]
ZH = dict(militia="新兵", shield="盾卫", spear="长枪兵", archer="弓手", cavalry="骑兵", hammer="大锤", bomber="炮仗兵",
          catapult="投石车", giant="大块头", boss="镇山兽", lord="主帅")


# ================================================================ 小工具
def disc(P, cx, cy, z, r, c, r_in=-1.0):
    """z 这一层放一个圆盘（圆心是格子角点坐标 cx, cy）。"""
    R = int(math.ceil(r)) + 1
    for x in range(int(math.floor(cx)) - R, int(math.ceil(cx)) + R):
        for y in range(int(math.floor(cy)) - R, int(math.ceil(cy)) + R):
            d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
            if r_in < d <= r:
                P.put(x, y, z, c)


def cyl_x(P, x0, x1, cy, cz, r, colf):
    """沿 X 的圆柱，colf(x, y, z, 到轴的距离) -> 颜色。"""
    R = int(math.ceil(r)) + 1
    for x in range(x0, x1):
        for y in range(int(cy) - R, int(cy) + R + 1):
            for z in range(int(cz) - R, int(cz) + R + 1):
                d = math.hypot(y + 0.5 - cy, z + 0.5 - cz)
                if d <= r:
                    P.put(x, y, z, colf(x, y, z, d))


def cyl_z(P, cx, cy, z0, z1, r, colf):
    for z in range(z0, z1):
        R = int(math.ceil(r)) + 1
        for x in range(int(cx) - R, int(cx) + R + 1):
            for y in range(int(cy) - R, int(cy) + R + 1):
                d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
                if d <= r:
                    P.put(x, y, z, colf(x, y, z, d))


def hsh(*a):
    """确定性的小哈希 -> [0, 1)。"""
    h = 2166136261
    for v in a:
        h = ((h ^ (int(v) & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
    return (h >> 8) / float(1 << 24)


# ================================================================ 人
def human(key, tunic="T", sleeve="T-", pants=PANTS, boots=BOOTS, belt=LEATHER, skin=SKIN, hair="#4A3426", tw=6,
          arm_w=2, unit=SU):
    """标准小兵：高约 1.6 米（不算帽子），头 4 格、身体 tw 格宽。上衣和裙摆是队伍色。"""
    M = Model(key)
    M.unit = unit
    M.meta = {}
    hw = tw // 2
    # 两条腿：枢轴在胯
    for nm, x0 in (("footA", 0), ("footB", -2)):
        P = M.part(nm, (x0 + 1, 0, 6))
        P.box(x0, x0 + 2, -1, 1, 0, 6, pants)
        P.box(x0, x0 + 2, -1, 1, 0, 2, boots)
        P.box(x0, x0 + 2, -2, -1, 0, 1, boots)
    B = M.part("body", (0, 0, 6))
    B.box(-hw, hw, -2, 2, 6, 12, tunic)               # 上身
    B.box(-hw, hw, -2, 2, 4, 6, tunic)                # 裙摆（盖住大腿根）
    B.tint(-hw, hw, -2, 2, 4, 5, "T-", only=tunic) if tunic == "T" else None
    B.box(-hw, hw, -2, 2, 7, 8, belt)                 # 腰带
    B.put(0, -2, 7, GOLD)                             # 带扣
    B.box(-hw, hw, -2, 2, 11, 12, tunic)
    # 头：4 × 4 × 4，脸朝 -Y
    B.box(-2, 2, -2, 2, 12, 16, skin)
    B.box(-2, 2, 0, 2, 13, 16, hair)                  # 后脑的头发
    B.box(-2, 2, -2, 2, 15, 16, hair)                 # 头顶
    B.put(-2, -2, 14, hair).put(1, -2, 14, hair)      # 鬓角
    B.slab(-1.45, -0.75, -2.08, -1.96, 13.25, 14.35, EYE)
    B.slab(0.75, 1.45, -2.08, -1.96, 13.25, 14.35, EYE)
    # 手臂：从肩往下 5 格，下面 2 格是手。枢轴在肩
    for nm, sg in (("armA", 1), ("armB", -1)):
        x0 = hw if sg > 0 else -hw - arm_w
        P = M.part(nm, (x0 + arm_w / 2.0, 0, 11.5))
        P.box(x0, x0 + arm_w, -1, 1, 7, 12, sleeve)
        P.box(x0, x0 + arm_w, -1, 1, 7, 9, skin)
        M.meta["grip_" + nm[-1]] = (x0 + arm_w / 2.0, 0.0, 8.0)
    M.meta["shoulder_z"] = 11.5
    M.meta["hip_z"] = 6
    M.meta["height"] = 1.6
    return M


def weapon(M, nm, arm):
    """手里的东西：枢轴在手心。"""
    g = M.meta["grip_" + arm]
    return M.part(nm, g)


def helmet(B, col, crest=None, rim=None, z=15):
    """一顶贴着头的盔：盖住头顶和后脑，前面留出脸。"""
    B.box(-2, 2, -2, 2, z, z + 2, col)
    B.box(-2, 2, 0, 2, 13, z, col)
    B.put(-2, -2, 14, col).put(1, -2, 14, col)
    if rim:
        B.box(-2, 2, -2, -1, z, z + 1, rim)
    B.cut(-2, -1, -2, -1, z + 1, z + 2).cut(1, 2, -2, -1, z + 1, z + 2)
    B.cut(-2, -1, 1, 2, z + 1, z + 2).cut(1, 2, 1, 2, z + 1, z + 2)
    if crest:
        B.box(-1, 1, -2, 2, z + 2, z + 3, crest)       # 盔顶一道队伍色的冠


def m_militia():
    M = human("militia", pants="#7A6450", boots="#5E4632", belt="#8A6440", hair="#3E2C22")
    B = M.parts["body"]
    # 斗笠：宽檐、尖顶，草黄色；檐口一圈暗一号，顶上一个队伍色的布疙瘩
    straw, straw_d, straw_l = "#E9C46A", "#C49A44", "#F6DE96"
    disc(B, 0, 0, 16, 3.9, "T")                       # 帽檐外圈一道队伍色（俯视时一圈色环）
    disc(B, 0, 0, 16, 3.0, straw_d)
    disc(B, 0, 0, 16, 2.4, straw)
    disc(B, 0, 0, 17, 2.75, straw)
    disc(B, 0, 0, 18, 1.6, "T")                       # 斗笠尖顶染成队伍色（俯视时帽顶一块色）
    B.box(-1, 1, -1, 1, 19, 20, "T+")
    B.box(-2, 2, -2, 2, 15, 16, "T-")                # 斗笠下面一圈队伍色的头巾
    B.slab(-1.45, -0.75, -2.08, -1.96, 13.25, 14.35, EYE)
    # 右手一根短木棒，棒头粗
    W = weapon(M, "wpnB", "B")
    gx, gy, gz = M.meta["grip_B"]
    W.box(-4, -3, -2, -1, 3, 9, WOOD)
    W.box(-5, -2, -3, 0, 0, 3, WOOD_D)
    W.put(-4, -2, 9, WOOD_D)
    M.meta["wpn_len"] = 0.9
    return M


def m_shield():
    M = human("shield", belt=LEATHER_D)
    B = M.parts["body"]
    helmet(B, STEEL, crest="T", rim=STEEL_D)
    B.box(-3, 3, -2, 2, 10, 12, STEEL_D)             # 护肩
    B.box(-3, 3, -2, 2, 11, 12, STEEL)
    for nm in ("armA", "armB"):
        M.parts[nm].tint(-6, 6, -2, 2, 10, 12, STEEL)
    # 左手：大方盾，正对前方，队伍色盾面、浅色包边、金色盾心
    S = weapon(M, "wpnA", "A")
    x0, x1, z0, z1 = -3, 6, 2, 14
    S.box(x0, x1, -4, -3, z0, z1, "T")
    S.box(x0, x1, -4, -3, z0, z0 + 1, STEEL_L)
    S.box(x0, x1, -4, -3, z1 - 1, z1, STEEL_L)
    S.box(x0, x0 + 1, -4, -3, z0, z1, STEEL_L)
    S.box(x1 - 1, x1, -4, -3, z0, z1, STEEL_L)
    S.box(1, 3, -5, -4, 7, 9, GOLD)                   # 盾心
    S.box(x0 + 1, x1 - 1, -4, -3, 12, 13, "T+")
    S.box(1, 3, -3, -1, 7, 9, WOOD_D)                 # 把手
    # 右手：短剑，剑尖朝下
    W = weapon(M, "wpnB", "B")
    W.box(-5, -2, -2, -1, 8, 9, GOLD)                 # 护手
    W.box(-4, -3, -2, -1, 9, 10, LEATHER_D)
    W.box(-4, -3, -2, -1, 2, 8, STEEL_L)
    M.meta["wpn_len"] = 0.7
    return M


def m_spear():
    M = human("spear", belt=LEATHER)
    B = M.parts["body"]
    # 尖顶盔
    helmet(B, STEEL, rim=STEEL_D)
    B.box(-1, 1, -1, 1, 17, 18, STEEL)
    B.put(-1, -1, 18, "T").put(0, -1, 18, "T")
    B.box(-3, 3, 0, 2, 8, 11, LEATHER)                # 背上的皮甲
    # 右手：2.6 米长枪，竖着拿；枪头下面一圈队伍色的枪缨
    W = weapon(M, "wpnB", "B")
    sx, sy = -4, -2
    W.box(sx, sx + 1, sy, sy + 1, 0, 25, WOOD)
    W.box(sx, sx + 1, sy, sy + 1, 0, 1, IRON)
    W.box(sx - 1, sx + 2, sy, sy + 1, 25, 27, STEEL_L)
    W.box(sx, sx + 1, sy, sy + 1, 27, 29, STEEL_L)
    W.box(sx - 1, sx + 2, sy - 1, sy + 2, 23, 25, "T")
    W.box(sx - 1, sx + 2, sy - 1, sy + 2, 22, 23, "T-")
    W.put(sx, sy - 1, 21, "T-").put(sx - 1, sy + 1, 21, "T-")
    M.meta["wpn_len"] = 2.9
    return M


def m_archer():
    M = human("archer", belt=LEATHER, hair="#5A3A24")
    B = M.parts["body"]
    # 软帽（队伍色）+ 一根白羽毛
    B.box(-2, 2, -2, 2, 15, 17, "T")
    B.box(-2, 2, 0, 2, 13, 15, "T-")
    B.box(-2, 2, -2, -1, 15, 16, "T-")
    B.cut(-2, -1, -2, -1, 16, 17).cut(1, 2, -2, -1, 16, 17)
    B.box(1, 2, 1, 2, 17, 21, "#FFFFFF")
    B.box(1, 2, 2, 3, 19, 21, "#FFFFFF")
    # 背上的箭袋，箭羽从右肩后面露出来（俯视看得见）
    B.box(-2, 1, 2, 4, 6, 13, LEATHER)
    B.box(-2, 1, 2, 4, 12, 13, LEATHER_D)
    for (x, y) in ((-2, 2), (0, 2), (-1, 3), (0, 3)):
        B.box(x, x + 1, y, y + 1, 13, 15, "#EDE6D2")
        B.put(x, y, 15, "T+")
    B.box(-3, 3, -2, 2, 9, 10, LEATHER_D)             # 斜挎的带子（简化成一道）
    # 左手：大弓（1.5 米），弓背朝前弯；一根细弦
    W = weapon(M, "wpnA", "A")
    gx = 3
    pts = []
    for z in range(0, 16):
        t = (z + 0.5) / 16.0
        y = -2 - int(round(2.4 * math.sin(math.pi * t)))
        pts.append((y, z))
        c = "#E0A060" if 5 <= z <= 10 else "#C98A4A"
        W.put(gx, y, z, c).put(gx + 1, y, z, c)
        if 2 <= z <= 13:
            W.put(gx, y - 1, z, c).put(gx + 1, y - 1, z, c)
    W.box(gx, gx + 2, -6, -4, 7, 9, LEATHER_D)        # 握把
    W.slab(gx + 0.9, gx + 1.1, -1.75, -1.55, 0.5, 15.5, "#F4F0E0")
    M.meta["wpn_len"] = 1.5
    return M


def m_hammer():
    M = human("hammer", tw=8, belt=LEATHER_D, hair="#2E2420", skin="#E8B890")
    B = M.parts["body"]
    B.box(-2, 2, -2, 2, 14, 15, "T")                  # 头巾
    B.box(-1, 1, 2, 3, 13, 15, "T-")                  # 头巾在脑后打的结
    B.box(-4, 4, -2, 2, 10, 12, LEATHER)              # 皮护肩
    B.box(-4, 4, -2, 2, 11, 12, LEATHER_D)
    B.box(-2, 2, -3, -2, 8, 10, LEATHER)              # 胸前一块皮甲
    # 右手：大锤。锤柄 1.6 米，锤头 0.4 × 0.7 × 0.4 米的铁块，中间一道队伍色
    W = weapon(M, "wpnB", "B")
    gx, gy, gz = M.meta["grip_B"]
    sx = int(gx - 0.5)
    W.box(sx, sx + 1, -1, 0, 2, 18, WOOD)
    W.box(sx, sx + 1, -1, 0, 7, 9, LEATHER_D)
    W.box(sx - 2, sx + 3, -5, 4, 17, 21, IRON)
    W.box(sx - 2, sx + 3, -5, -4, 17, 21, IRON_L)
    W.box(sx - 2, sx + 3, 3, 4, 17, 21, IRON_L)
    W.box(sx - 2, sx + 3, -1, 0, 17, 21, "T")
    M.meta["wpn_len"] = 2.1
    return M


def keg_vox(P, cx, cy, cz, r, half_len, axis="x", fuse=True, scale=1):
    """火药桶（炮仗兵手里的和场上的是同一个样子）：橙色桶身、深色铁箍、桶盖，顶上一根引信和火星。"""
    hoop_at = {-half_len + 1, half_len - 2} if half_len <= 3 else {-half_len + 1, half_len - 2, 0}

    def colf(a, d, k):
        if k in hoop_at:
            return HOOP
        return KEG

    if axis == "x":
        for x in range(cx - half_len, cx + half_len):
            for y in range(cy - int(r) - 1, cy + int(r) + 2):
                for z in range(cz - int(r) - 1, cz + int(r) + 2):
                    d = math.hypot(y + 0.5 - cy, z + 0.5 - cz)
                    if d <= r:
                        P.put(x, y, z, colf(None, d, x - cx))
        # 桶两头的盖子
        for x in (cx - half_len, cx + half_len - 1):
            for y in range(cy - int(r) - 1, cy + int(r) + 2):
                for z in range(cz - int(r) - 1, cz + int(r) + 2):
                    if (x, y, z) in P.vox and math.hypot(y + 0.5 - cy, z + 0.5 - cz) < r - 1.0:
                        P.vox[(x, y, z)] = WOOD_D
        if fuse:
            top = cz + int(math.ceil(r))
            P.put(cx, cy, top, "#2A2220").put(cx, cy, top + 1, "#2A2220")
            P.put(cx, cy, top + 2, SPARK)
            P.put(cx - 1, cy, top + 2, SPARK2).put(cx, cy - 1, top + 3, SPARK2)
    else:
        for z in range(cz - half_len, cz + half_len):
            for x in range(cx - int(r) - 1, cx + int(r) + 2):
                for y in range(cy - int(r) - 1, cy + int(r) + 2):
                    k = z - cz
                    bulge = 0.6 * (1 - (k / float(half_len)) ** 2)
                    d = math.hypot(x + 0.5 - cx, y + 0.5 - cy)
                    if d <= r - 0.6 + bulge:
                        P.put(x, y, z, colf(None, d - bulge, k))
        top = cz + half_len
        for x in range(cx - int(r) - 1, cx + int(r) + 2):
            for y in range(cy - int(r) - 1, cy + int(r) + 2):
                if math.hypot(x + 0.5 - cx, y + 0.5 - cy) <= r - 1.4:
                    P.put(x, y, top - 1, WOOD_D)
        if fuse:
            P.put(cx, cy, top, "#2A2220").put(cx, cy, top + 1, "#2A2220")
            P.put(cx, cy, top + 2, SPARK)
            P.put(cx - 1, cy, top + 2, SPARK2).put(cx, cy - 1, top + 3, SPARK2).put(cx, cy, top + 3, SPARK2)


def m_bomber():
    M = human("bomber", belt=LEATHER, hair="#3A2A22")
    B = M.parts["body"]
    # 皮帽 + 一副护目镜
    B.box(-2, 2, -2, 2, 15, 16, LEATHER)
    B.box(-2, 2, 0, 2, 13, 16, LEATHER)
    B.box(-2, 2, -3, -2, 14, 15, "#2A2A30")
    B.slab(-1.6, -0.6, -3.08, -2.95, 14.15, 14.85, "#9ED8F0")
    B.slab(0.6, 1.6, -3.08, -2.95, 14.15, 14.85, "#9ED8F0")
    B.box(-3, 3, -2, 2, 9, 10, LEATHER_D)             # 斜挎带
    # 头顶的火药桶：挂在身体上，双手举着（姿势里把两只手臂举起来）
    K = M.part("keg", (0, 0, 20))
    keg_vox(K, 0, 0, 20, 3.1, 3, axis="x", fuse=True)
    M.meta["arms_up"] = True
    M.meta["wpn_len"] = 0.0
    return M


def m_giant():
    """大块头：3.5 米的巨人。和小兵同一套做法，但一格 0.2 米，比例更壮、头更小、手臂长到膝盖。"""
    M = Model("giant")
    M.unit = 0.2
    M.meta = {}
    skin, skin_d = "#C99A72", "#A87A56"
    pants = "#5A4E44"
    for nm, x0 in (("footA", 0), ("footB", -3)):
        P = M.part(nm, (x0 + 1.5, 0.5, 5))
        P.box(x0, x0 + 3, -1, 2, 0, 5, pants)
        P.box(x0, x0 + 3, -1, 2, 0, 2, "#3A2C24")
        P.box(x0, x0 + 3, -2, -1, 0, 1, "#3A2C24")
    B = M.part("body", (0, 0.5, 5))
    B.box(-4, 4, -2, 3, 5, 12, "T")                   # 上身（队伍色的坎肩）
    B.box(-4, 4, -2, 3, 4, 6, "T-")
    B.box(-4, 4, -2, 3, 6, 7, LEATHER_D)              # 宽腰带
    B.box(-1, 1, -3, -2, 6, 7, GOLD)
    B.box(1, 4, -2, 3, 10, 12, skin)                  # 露出的左肩
    B.box(-4, -1, -2, 3, 11, 12, "T+")
    B.box(-2, 2, -2, 2, 12, 15, skin)                 # 小脑袋
    B.box(-2, 2, 0, 2, 13, 15, "#3A2A22")
    B.box(-2, 2, -2, 2, 14, 15, "#3A2A22")
    B.box(-1, 1, 0, 2, 15, 16, "#3A2A22")             # 头顶扎个小髻
    B.put(-2, -2, 13, skin_d).put(1, -2, 13, skin_d)
    B.slab(-1.4, -0.7, -2.06, -1.96, 13.2, 13.8, EYE)
    B.slab(0.7, 1.4, -2.06, -1.96, 13.2, 13.8, EYE)
    B.slab(-1.2, 1.2, -2.06, -1.96, 12.3, 12.55, "#7A4A3A")
    for nm, sg in (("armA", 1), ("armB", -1)):
        x0 = 4 if sg > 0 else -7
        P = M.part(nm, (x0 + 1.5, 0.5, 11.5))
        P.box(x0, x0 + 3, -1, 2, 3, 12, skin)
        P.box(x0, x0 + 3, -1, 2, 3, 5, skin_d)
        P.box(x0, x0 + 3, -1, 2, 7, 8, LEATHER)       # 护腕
        if sg < 0:
            P.box(x0, x0 + 3, -1, 2, 10, 12, "T")
        M.meta["grip_" + nm[-1]] = (x0 + 1.5, 0.5, 4.0)
    # 右手：一根原木当棒子
    W = M.part("wpnB", M.meta["grip_B"])
    gx = -6
    for z in range(0, 13):
        r = 1.0 if z < 9 else 1.4
        for x in range(gx - 2, gx + 2):
            for y in range(-2, 3):
                if math.hypot(x + 0.5 - (gx + 0.0), y + 0.5 - 0.5) <= r + 0.2:
                    c = "#8A5E3A" if hsh(x, y, z) > 0.25 else "#6E4628"
                    W.put(x, y, z, c)
    disc(W, gx, 0.5, 13, 1.2, "#D9B07A")
    M.meta["shoulder_z"] = 11.5
    M.meta["hip_z"] = 5
    M.meta["height"] = 3.2
    M.meta["wpn_len"] = 2.6
    return M


def m_cavalry():
    """骑兵：马 + 骑手。马披是队伍色（侧面一大片），骑手头盔上一根队伍色的翎子。"""
    M = Model("cavalry")
    M.unit = SU
    M.meta = {}
    coat, coat_d, mane = "#8E5A36", "#6C4026", "#33221A"
    hoof = "#2A2220"
    H = M.part("horse", (0, 0, 8))
    H.box(-3, 3, -7, 7, 8, 14, coat)                  # 马身
    H.chamfer(-3, 3, -7, 7, 8, 9)
    H.box(-2, 2, -10, -6, 11, 17, coat)               # 脖子
    H.box(-2, 2, -9, -7, 17, 18, coat)
    H.box(-2, 2, -14, -9, 15, 19, coat)               # 头
    H.box(-2, 2, -15, -13, 15, 17, coat_d)            # 嘴
    H.put(-2, -12, 19, coat).put(1, -12, 19, coat)    # 耳朵
    H.put(-2, -12, 20, coat_d).put(1, -12, 20, coat_d)
    H.box(-1, 1, -9, -5, 17, 19, mane)                # 鬃毛
    H.box(-1, 1, -6, -4, 14, 16, mane)
    H.slab(-2.08, -1.96, -12.6, -11.9, 17.2, 17.9, EYE)
    H.slab(1.96, 2.08, -12.6, -11.9, 17.2, 17.9, EYE)
    H.box(-1, 1, 7, 9, 10, 14, mane)                  # 尾巴
    H.box(-1, 1, 9, 10, 7, 12, mane)
    # 马披：比马身宽一格，垂到肚子下面
    H.box(-4, 4, -6, 6, 7, 14, "T")
    H.box(-4, 4, -6, 6, 7, 8, "T-")
    H.box(-4, 4, -6, -5, 7, 14, "T+")
    H.box(-4, 4, 5, 6, 7, 14, "T+")
    H.box(-3, 3, -2, 3, 14, 15, LEATHER)              # 马鞍
    # 骑手（和马身一体，坐在鞍上）
    H.box(-4, -3, -1, 1, 9, 14, BOOTS)                # 两条腿垂在马两侧
    H.box(3, 4, -1, 1, 9, 14, BOOTS)
    H.box(-4, -3, -1, 1, 12, 15, PANTS)
    H.box(3, 4, -1, 1, 12, 15, PANTS)
    R = M.part("body", (0, 0, 14))
    R.box(-3, 3, -2, 2, 14, 20, "T")
    R.box(-3, 3, -2, 2, 15, 16, LEATHER)
    R.box(-3, 3, -2, 2, 19, 20, STEEL)
    R.box(-2, 2, -2, 2, 20, 24, SKIN)
    R.slab(-1.45, -0.75, -2.08, -1.96, 21.25, 22.35, EYE)
    R.slab(0.75, 1.45, -2.08, -1.96, 21.25, 22.35, EYE)
    R.box(-2, 2, -2, 2, 23, 25, STEEL)
    R.box(-2, 2, 0, 2, 21, 23, STEEL)
    R.box(-2, 2, -2, -1, 23, 24, STEEL_D)
    R.box(-1, 1, -1, 3, 25, 27, "T")                  # 翎子
    R.box(-1, 1, 2, 4, 26, 28, "T+")
    for nm, sg in (("armA", 1), ("armB", -1)):
        x0 = 3 if sg > 0 else -5
        P = M.part(nm, (x0 + 1.0, 0, 19.5))
        P.box(x0, x0 + 2, -1, 1, 15, 20, "T-")
        P.box(x0, x0 + 2, -1, 1, 15, 17, SKIN)
        M.meta["grip_" + nm[-1]] = (x0 + 1.0, 0.0, 16.0)
    # 马刀：举在右手里，刀尖朝前上方
    W = M.part("wpnB", M.meta["grip_B"])
    W.box(-5, -3, -2, -1, 16, 17, GOLD)
    W.box(-4, -3, -2, -1, 17, 25, STEEL_L)
    W.put(-4, -1, 24, STEEL_L)
    # 四条马腿：枢轴在腿根
    for nm, x0, y0 in (("hlegFA", 1, -6), ("hlegFB", -3, -6), ("hlegBA", 1, 4), ("hlegBB", -3, 4)):
        P = M.part(nm, (x0 + 1, y0 + 1, 9))
        P.box(x0, x0 + 2, y0, y0 + 2, 1, 10, coat)
        P.box(x0, x0 + 2, y0, y0 + 2, 0, 1, hoof)
        P.box(x0, x0 + 2, y0, y0 + 2, 1, 2, "#F2E8D8")   # 白袜子
    M.meta["height"] = 2.8
    M.meta["wpn_len"] = 1.0
    return M


def m_catapult():
    """投石车：2.2 × 3 米的木架子，四个轮子，一根带勺的抛臂；后角插一面队伍色的小旗。"""
    M = Model("catapult")
    M.unit = SU
    M.meta = {}
    Bs = M.part("base", (0, 0, 0))
    for x0 in (-10, 8):
        Bs.box(x0, x0 + 2, -15, 15, 3, 6, WOOD)          # 两根纵梁
        Bs.box(x0, x0 + 2, -15, 15, 5, 6, WOOD_L)
        xo = x0 if x0 < 0 else x0 + 1
        Bs.box(xo, xo + 1, -13, 13, 3, 5, "T")
    for y0 in (-15, -1, 13):
        Bs.box(-10, 10, y0, y0 + 2, 3, 5, WOOD_D)        # 横梁
    # 四个轮子（在纵梁外侧）
    for xw in (-12, 10):
        for yc in (-10, 10):
            for y in range(yc - 4, yc + 4):
                for z in range(0, 8):
                    d = math.hypot(y + 0.5 - yc, z + 0.5 - 3.5)
                    if d <= 3.6:
                        c = IRON if d > 2.8 else (WOOD_D if d < 1.0 else WOOD)
                        Bs.put(xw, y, z, c).put(xw + 1, y, z, c)
    # 两侧立柱 + 横档（抛臂打到这里停）
    for x0 in (-10, 8):
        Bs.box(x0, x0 + 2, -3, -1, 6, 21, WOOD)
        Bs.box(x0, x0 + 2, 3, 5, 6, 11, WOOD_D)          # 斜撑
        Bs.box(x0, x0 + 2, 1, 3, 6, 9, WOOD_D)
    Bs.box(-10, 10, -3, -1, 19, 21, WOOD_L)
    Bs.box(-8, 8, -3, -1, 19, 20, "T")                   # 横档上包一道队伍色的布
    # 绞盘（抛臂的轴）
    Bs.box(-8, 8, 2, 4, 6, 8, IRON)
    # 小旗
    Bs.box(9, 10, 13, 14, 6, 30, WOOD_D)
    for z in range(23, 30):
        for x in range(10, 17):
            y = 13 + (1 if (x // 2) % 2 else 0)
            Bs.put(x, y, z, "T" if z > 23 else "T-")
    # 抛臂：轴在 (0, 3, 7)，平时往后躺（上弦），发射时往前上方甩到竖直
    A = M.part("arm", (0, 3, 7))
    A.box(-1, 1, 3, 20, 6, 8, WOOD)
    A.box(-1, 1, 3, 20, 7, 8, WOOD_L)
    A.box(-3, 3, 16, 22, 7, 9, WOOD_D)                   # 勺
    A.box(-3, 3, 16, 17, 9, 10, WOOD_D)
    A.box(-3, 3, 21, 22, 9, 10, WOOD_D)
    A.box(-3, -2, 16, 22, 9, 10, WOOD_D)
    A.box(2, 3, 16, 22, 9, 10, WOOD_D)
    A.box(-2, 2, 17, 21, 9, 11, "#8C8A86")               # 勺里的石头
    A.box(-1, 1, 18, 20, 11, 12, "#A8A6A0")
    M.meta["height"] = 2.8
    return M


def m_boss():
    """镇山兽：六吨的石兽（引擎里的碰撞盒 2.6 宽 × 4.8 长 × 2.6 高，一格 0.2 米；造型比盒子略大一圈）。
    设计要点（全景里一眼认得出、看得出头朝哪边）：
      * 深青黑的玄武岩身子，和场上浅灰的立石、绿草地都拉开；肩上高高隆起一个驼峰，往后收窄、压低，俯视是个"前宽后窄"的楔形；
      * 头前一整块骨白色的撞额，上面刻一个发光的"山"字；头两侧一对骨白大角往前上方掠出去——亮色全在前头；
      * 肩胛和后胯有几道发橙光的裂纹，眼睛同色；驼峰上一排骨板；
      * 背上披一块队伍色的毯子（金边），俯视时是一大块队伍色；尾巴末端一个骨锤。
    部件：body、head（枢轴在脖子）、四条腿 legFA legFB legBA legBB、tail、stars（撞晕时头顶转的星星，平时不显示）。"""
    M = Model("boss")
    M.unit = 0.2
    M.meta = {"optional": ["stars"]}
    st, st_l, st_d, st_dd = "#50545F", "#747A88", "#3B3E4A", "#2A2C35"
    bone, bone_l, bone_d = "#EDE3C8", "#FFF8E4", "#BFAE84"
    glow, glow2 = "!#FF9A22", "!#FFD45E"
    lichen = "#5F8F7C"
    # ---- 身子：按前后位置给（半宽, 顶, 底）
    prof = {-8: (6, 14, 4), -7: (7, 15, 4), -6: (7, 16, 4), -5: (7, 16, 4), -4: (7, 16, 4), -3: (7, 16, 4), -2: (7, 15, 4),
            -1: (6, 14, 5), 0: (6, 13, 5), 1: (6, 13, 5), 2: (6, 13, 5), 3: (6, 13, 5), 4: (6, 13, 5), 5: (6, 13, 5),
            6: (6, 13, 4), 7: (6, 13, 4), 8: (6, 13, 4), 9: (5, 12, 4), 10: (4, 11, 5)}
    B = M.part("body", (0, 0, 6))

    def inset(z, top, bot):
        return 2 if z == top - 1 else (1 if (z == top - 2 or z == bot) else 0)

    for y, (hw, top, bot) in prof.items():
        for z in range(bot, top):
            ins = inset(z, top, bot)
            for x in range(-hw + ins, hw - ins):
                c = st_l if z >= top - 2 else (st_d if z <= bot + 1 else st)
                v = hsh(x, y, z, 3)
                if v < 0.10:
                    c = st_dd if z < top - 2 else st
                elif v > 0.95 and z >= top - 2 and y < 0:
                    c = lichen
                B.put(x, y, z, c)
    # 驼峰上的骨板
    for (y0, h) in ((-7, 2), (-5, 3), (-3, 3), (-1, 2)):
        zt = prof[y0][1]
        B.box(-1, 1, y0, y0 + 1, zt - 1, zt + h, bone)
        B.box(-1, 1, y0, y0 + 1, zt + h - 1, zt + h, bone_l)
        B.box(-1, 1, y0 + 1, y0 + 2, zt - 1, zt + h - 1, bone_d)
    # 发光的裂纹：肩胛、后胯
    for sx in (6, -7):
        for (y, z) in ((-7, 12), (-6, 12), (-6, 11), (-5, 10), (-5, 9), (-4, 8), (-4, 7), (-3, 10), (-2, 11), (-6, 8), (-7, 7)):
            if (sx, y, z) in B.vox:
                B.vox[(sx, y, z)] = glow if (y + z) % 3 else glow2
    for sx in (4, -5):
        for (y, z) in ((9, 9), (9, 8), (9, 7)):
            if (sx, y, z) in B.vox:
                B.vox[(sx, y, z)] = glow
    # 背上的毯子（队伍色、金边）：贴着背的外面再包一层
    gold = GOLD
    for y in range(1, 9):
        hw, top, bot = prof[y]
        edge = y in (1, 8)
        mid = y in (4, 5)
        cells = [(x, top) for x in range(-hw + 2, hw - 2)]
        cells += [(hw - 2, top - 1), (-hw + 1, top - 1), (hw - 1, top - 2), (-hw, top - 2)]
        for z in range(8, top - 2):
            cells += [(hw, z), (-hw - 1, z)]
        for (x, z) in cells:
            c = "T+" if mid else "T"
            if edge or z == 8:
                c = gold
            elif z == 9:
                c = "T-"
            B.put(x, y, z, c)
        if edge:
            B.put(hw, y, 7, gold).put(-hw - 1, y, 7, gold)
    # ---- 头
    Hd = M.part("head", (0, -8, 10))
    Hd.box(-5, 5, -13, -8, 4, 12, st)
    Hd.box(-5, 5, -13, -8, 11, 12, st_l)
    Hd.box(-4, 4, -13, -9, 3, 5, st_d)                   # 下巴
    Hd.box(-4, 4, -14, -12, 4, 8, st)                    # 口鼻
    Hd.box(-4, 4, -14, -12, 4, 5, st_d)
    Hd.put(-2, -14, 5, st_dd).put(1, -14, 5, st_dd)      # 鼻孔
    Hd.box(-6, 6, -14, -12, 8, 13, bone)                 # 撞额：一整块骨白的板
    Hd.box(-6, 6, -14, -12, 12, 13, bone_l)
    Hd.box(-6, 6, -14, -13, 8, 9, bone_d)
    for (x, z) in ((-6, 12), (5, 12), (-6, 8), (5, 8)):
        Hd.cut(x, x + 1, -14, -12, z, z + 1)
    for x in range(-4, 4):                               # "山"字：一横三竖，中间一竖高
        Hd.vox[(x, -14, 9)] = glow
    for (x, zt) in ((-4, 11), (-1, 12), (0, 12), (3, 11)):
        for z in range(10, zt):
            Hd.vox[(x, -14, z)] = glow
    for sx in (-4, 2):                                   # 眼睛（正面）
        Hd.box(sx, sx + 2, -14, -13, 6, 8, st_dd)
        Hd.box(sx, sx + 2, -14, -13, 7, 8, glow2)
    for sx in (-5, 4):                                   # 眼睛（侧面）
        Hd.vox[(sx, -12, 7)] = glow2
        Hd.vox[(sx, -11, 7)] = glow
    Hd.boxm(4, 5, -15, -13, 3, 6, bone)                  # 獠牙
    Hd.boxm(4, 5, -15, -14, 6, 7, bone_l)
    Hd.boxm(5, 8, -12, -9, 9, 12, bone_d)                # 大角：从头两侧往外、往前、往上
    Hd.boxm(7, 10, -14, -11, 10, 13, bone)
    Hd.boxm(9, 11, -16, -13, 12, 15, bone)
    Hd.boxm(9, 11, -17, -15, 14, 17, bone_l)
    Hd.boxm(9, 10, -18, -17, 16, 18, bone_l)
    # ---- 四条腿（前腿粗），骨色的蹄
    for nm, x0, w in (("legFA", 2, 5), ("legFB", -7, 5)):
        P = M.part(nm, (x0 + w / 2.0, -5.5, 6))
        P.box(x0, x0 + w, -8, -3, 0, 7, st)
        P.box(x0, x0 + w, -8, -3, 2, 3, st_d)
        P.box(x0, x0 + w, -8, -3, 0, 2, bone_d)
        P.box(x0, x0 + w, -9, -8, 0, 2, bone)
        P.vox[(x0 + (w - 1 if x0 > 0 else 0), -6, 5)] = glow
    for nm, x0, w in (("legBA", 2, 4), ("legBB", -6, 4)):
        P = M.part(nm, (x0 + w / 2.0, 7.5, 5))
        P.box(x0, x0 + w, 5, 10, 0, 6, st)
        P.box(x0, x0 + w, 5, 10, 2, 3, st_d)
        P.box(x0, x0 + w, 5, 10, 0, 2, bone_d)
        P.box(x0, x0 + w, 4, 5, 0, 2, bone)
    # ---- 尾巴：末端一个骨锤
    T = M.part("tail", (0, 10, 10))
    T.box(-1, 1, 10, 13, 8, 11, st)
    T.box(-1, 1, 13, 15, 9, 11, st_d)
    T.box(-2, 2, 15, 18, 8, 12, bone)
    T.box(-2, 2, 15, 18, 11, 12, bone_l)
    T.boxm(2, 3, 16, 17, 9, 11, bone_d)
    T.box(-1, 1, 16, 17, 12, 13, bone_l)
    # ---- 撞晕时头顶的星星（三颗，绕着转）
    S = M.part("stars", (0, -10, 21))
    for k in range(3):
        a = 2 * math.pi * k / 3
        cx, cy = int(round(6 * math.cos(a))), -10 + int(round(6 * math.sin(a)))
        c = "!#FFE45A"
        S.box(cx - 1, cx + 1, cy - 1, cy + 1, 20, 22, c)
        for (dx, dy, dz) in ((-2, 0, 0), (1, 0, 0), (0, -2, 0), (0, 1, 0)):
            S.box(cx + dx, cx + dx + 1, cy + dy, cy + dy + 1, 20, 22, "!#FFF3B0")
        S.box(cx - 1, cx + 1, cy - 1, cy + 1, 22, 23, "!#FFF3B0")
        S.box(cx - 1, cx + 1, cy - 1, cy + 1, 19, 20, "!#FFF3B0")
    M.meta["height"] = 3.2
    return M


BUILD = dict(militia=m_militia, shield=m_shield, spear=m_spear, archer=m_archer, cavalry=m_cavalry, hammer=m_hammer,
             bomber=m_bomber, catapult=m_catapult, giant=m_giant, boss=m_boss)


def build(kind):
    return BUILD[kind]()


# ================================================================ 道具
def p_keg():
    """场上的火药桶：半径 0.6 米、高 1 米，竖着放。"""
    M = Model("keg")
    M.unit = SU
    M.meta = {}
    P = M.part("body", (0, 0, 0))
    keg_vox(P, 0, 0, 5, 6.0, 5, axis="z", fuse=True)
    return M


def p_snowball():
    """大雪球：半径 1.5 米。白色，背光一侧和缝里偏蓝，夹着几块石头和枯枝。"""
    M = Model("snowball")
    M.unit = 0.15
    M.meta = {}
    P = M.part("body", (0, 0, 0))
    r = 10.0
    for x in range(-11, 11):
        for y in range(-11, 11):
            for z in range(-11, 11):
                d = math.sqrt((x + 0.5) ** 2 + (y + 0.5) ** 2 + (z + 0.5) ** 2)
                if d <= r:
                    v = hsh(x, y, z)
                    c = "#FFFFFF" if v > 0.3 else ("#E6F0FA" if v > 0.08 else "#C8DCEE")
                    if d > r - 1.2 and hsh(x * 7, y * 3, z * 5) < 0.025:
                        c = "#7A7066"
                    if d > r - 1.2 and hsh(x * 5, y * 11, z * 3) < 0.012:
                        c = "#6B4A30"
                    P.put(x, y, z, c)
    return M


def p_flag():
    """军旗：4.2 米的旗杆，2 × 1.3 米的队伍色旗面，脚下一圈石头压着。"""
    M = Model("flag")
    M.unit = SU
    M.meta = {}
    P = M.part("body", (0, 0, 0))
    P.box(0, 1, 0, 1, 0, 42, WOOD_D)
    P.box(0, 1, 0, 1, 42, 43, GOLD)
    disc(P, 0.5, 0.5, 0, 3.2, "#9A9690")
    disc(P, 0.5, 0.5, 1, 2.2, "#B5B0A8")
    F = M.part("cloth", (1, 0.5, 41))
    for x in range(1, 21):
        w = int(round(1.2 * math.sin(x * 0.45)))
        for z in range(28, 41):
            c = "T"
            if z in (28, 40):
                c = "T-"
            if abs(x - 10.5) + abs(z - 34) <= 3.2:
                c = "#FFFFFF" if abs(x - 10.5) + abs(z - 34) > 1.2 else GOLD
            F.put(x, w, z, c)
    return M


def p_barrier():
    """木栅：9.8 × 0.4 × 1.6 米（第 4 关横贯冰道、两头嵌进崖壁的那种），一排削尖的木桩，前后两道横木。"""
    M = Model("barrier")
    M.unit = SU
    M.meta = {}
    P = M.part("body", (0, 0, 0))
    for i, x in enumerate(range(-49, 49, 4)):
        h = 15 + int(hsh(i, 5) * 3)
        c = WOOD if i % 2 else "#8A5E36"
        P.box(x, x + 3, -1, 2, 0, h, c)
        P.box(x + 1, x + 2, 0, 1, h, h + 2, WOOD_L)
    P.box(-49, 49, -2, -1, 4, 6, WOOD_D)
    P.box(-49, 49, -2, -1, 10, 12, WOOD_D)
    P.box(-49, 49, 2, 3, 6, 8, WOOD_D)
    return M


def p_gate():
    """木栅门：6 × 2.5 × 0.5 米，两扇竖木板门，三道铁箍，门中间一根门闩。"""
    M = Model("gate")
    M.unit = SU
    M.meta = {}
    P = M.part("body", (0, 0, 0))
    for i, x in enumerate(range(-30, 30, 3)):
        c = WOOD if i % 2 else "#8A5E36"
        top = 25 - (1 if i % 3 == 0 else 0)
        P.box(x, x + 3, -2, 3, 0, top, c)
    for z in (3, 12, 21):
        P.box(-30, 30, -3, -2, z, z + 2, IRON)
    P.box(-1, 1, -3, 3, 0, 25, WOOD_D)
    P.box(-6, 6, -4, -3, 12, 14, WOOD_D)
    return M


def p_pillar():
    """立石：1.8 × 1.8 × 3 米的石柱，上半截收窄一圈，腰上一道深色刻纹带（嵌着浅黄的纹），顶上长苔，棱角磨圆、有裂缝。"""
    M = Model("pillar")
    M.unit = 0.2
    M.meta = {}
    P = M.part("body", (0.5, 0.5, 0))
    st, st_l, st_d, band, rune = "#9AA2AE", "#C4CAD4", "#6E7684", "#5E6674", "#E2CF8E"
    for z in range(0, 15):
        lo, hi = -4, 4
        for x in range(lo, hi + 1):
            for y in range(lo, hi + 1):
                ex, ey = x in (lo, hi), y in (lo, hi)
                if ex and ey and (z > 1 or hsh(x, y, z) < 0.5):
                    continue
                c = st
                if z >= 12 and (ex or ey) and hsh(x, y, z, 5) < 0.35:
                    c = st_l
                if z in (5, 6) and (ex or ey):
                    c = rune if (x + y) % 3 == 0 and z == 5 else band
                if z <= 0:
                    c = st_d
                if z == 14:
                    if (ex or ey) and hsh(x, y, 9) < 0.3:
                        continue
                    c = "#6E9A4A" if hsh(x, y, 77) < 0.5 else ("#94BE5C" if hsh(x, y, 78) < 0.4 else st_l)
                P.put(x, y, z, c)
    for z in range(1, 9):                         # 正面一道裂缝
        P.vox[(1 if z < 5 else 2, -4, z)] = st_d
    return M


PROPS = dict(keg=p_keg, snowball=p_snowball, flag=p_flag, barrier=p_barrier, gate=p_gate, pillar=p_pillar)


def build_prop(kind):
    return PROPS[kind]()


# ================================================================ 主帅
# 主帅的造型是可替换的一块：
#   * 默认（kind = "lord"）是下面这个通用的体素将领（原创，随代码公开）；
#   * 指定某家 AI 时（kind = "lord:<名字>"）换成那家的体素形象：一份 voxkit 的 JSON 模型文件。
#     去哪里找：--lord 直接给 .json 路径；或者给名字，按环境变量 A12_LORD_DIR、再按本仓库顶层的
#     characters/<名字>.json 找。
LORD_DIRS = [os.environ.get("A12_LORD_DIR", ""),
             os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "characters")]


def m_lord():
    """通用主帅：比小兵高一头的将领。认法：高耸的金盔 + 一根很高的队伍色盔缨、背上两面队伍色的令旗、
    队伍色的长披风（俯视时背上一大片）、金色肩甲，右手一把宽刃大刀竖着拿。"""
    M = human("lord", tunic="T", sleeve=STEEL, belt="#3A2A22", pants="#3E3446", boots="#2E2420", hair="#2A2026")
    B = M.parts["body"]
    gold_d = "#B8862A"
    # 金盔：比普通盔高两格，前面一道护额，两侧护耳
    B.box(-2, 2, -2, 2, 15, 18, GOLD)
    B.box(-2, 2, 0, 2, 13, 15, GOLD)
    B.box(-2, 2, -2, -1, 15, 16, gold_d)
    B.put(-2, -2, 14, gold_d).put(1, -2, 14, gold_d)
    B.cut(-2, -1, -2, -1, 17, 18).cut(1, 2, -2, -1, 17, 18).cut(-2, -1, 1, 2, 17, 18).cut(1, 2, 1, 2, 17, 18)
    B.box(-1, 1, -1, 1, 18, 19, gold_d)
    B.box(-1, 1, -1, 1, 19, 24, "T")                   # 高盔缨
    B.box(-1, 1, 0, 2, 22, 24, "T+")
    B.put(0, 1, 24, "T+")
    # 胸甲：上身正面一块浅钢色，中间护心镜
    B.box(-3, 3, -3, -2, 8, 12, STEEL)
    B.box(-1, 1, -4, -3, 9, 11, GOLD)
    # 大肩甲：队伍色，金边（俯视时肩上两大块队伍色）
    B.box(-5, -2, -2, 3, 11, 13, "T+").box(2, 5, -2, 3, 11, 13, "T+")
    B.box(-5, -2, -2, 3, 11, 12, GOLD).box(2, 5, -2, 3, 11, 12, GOLD)
    # 长披风：从肩后垂到小腿，略往后飘
    B.box(-3, 3, 2, 3, 3, 12, "T")
    B.box(-3, 3, 3, 4, 3, 8, "T-")
    B.box(-3, 3, 2, 4, 3, 4, "T--")
    # 背上两面令旗（三角小旗，旗杆从肩后伸出）
    for sx in (-2, 1):
        B.box(sx, sx + 1, 3, 4, 10, 25, WOOD_D)
        B.box(sx, sx + 1, 3, 4, 25, 26, GOLD)
        for k, zz in enumerate(range(18, 25)):
            w = 1 + (6 - k) // 2 if sx < 0 else 1 + k // 2
            x0 = sx - w if sx < 0 else sx + 1
            x1 = sx if sx < 0 else sx + 1 + w
            B.box(x0, x1, 3, 4, zz, zz + 1, "T" if k % 3 else "T+")
    # 右手：宽刃大刀，竖着拿，刀背朝后
    W = weapon(M, "wpnB", "B")
    sx, sy = -4, -2
    W.box(sx, sx + 1, sy, sy + 1, -1, 14, WOOD_D)
    W.box(sx - 1, sx + 2, sy, sy + 1, 14, 15, GOLD)
    W.box(sx, sx + 1, sy - 2, sy + 1, 15, 23, STEEL_L)
    W.box(sx, sx + 1, sy + 1, sy + 2, 16, 21, STEEL_D)
    W.box(sx, sx + 1, sy - 1, sy, 23, 24, STEEL_L)
    W.put(sx, sy, 14, "T").put(sx, sy, 13, "T")
    M.meta["wpn_len"] = 2.3
    M.meta["height"] = 1.8
    return M


def lord_path(key):
    import os as _os
    if key.lower().endswith(".json") and _os.path.exists(key):
        return key
    for d in LORD_DIRS:
        if d:
            p = _os.path.join(d, key + ".json")
            if _os.path.exists(p):
                return p
    return None


def lord_model(key):
    """指定 AI 的主帅造型（体素小人 JSON）。找不到文件就用通用主帅，并打印一行提示。"""
    import json
    p = lord_path(key)
    if p is None:
        print("主帅造型 %s 找不到，用通用主帅" % key, flush=True)
        return m_lord()
    M = VK.from_json(json.load(open(p, encoding="utf-8")))
    M.unit = VK.U
    M.meta = dict(height=1.5, figure=True)
    return M
