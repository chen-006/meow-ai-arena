# -*- coding: utf-8 -*-
"""地图主题：每张地图指定一个主题，主题决定地表、崖壁、障碍物样式、植被、场外布景、天空和光照的配色与样式。
角色、镖、特效、信息栏不随主题变（同一个世界、同一种造型语言）。纯 Python，Blender 和普通 Python 都用。

主题从哪来（优先级从高到低）：环境变量 DART_THEME（命令行 --theme 会设它）→ 地图文件 / 回放里的 THEME 行 → 下面的 MAP_THEME（按地图名）→ meadow。
加新主题：在 THEMES 里加一条，只写和 meadow 不一样的键。键的含义：
  pal     崖壁 / 石头 / 高草 / 草簇 / 花 / 树 / 云 / 天空 的颜色（并进 scene_lib.PAL）
  ground  地面贴图的颜色（并进 ground_paint.C）
  rock    障碍物样式：h 高度倍数、top 顶面收多小、n 边数、p 方圆程度（越大越方）、mossy 顶面换成 moss 色的比例
  light   太阳方位角 az / 高度角 el / 强度 energy / 颜色 color，环境光强度 ambient
  decor   场外台地上的树：pines 松树比例；树冠配色在 pal['tree_pals']
"""
import os

BASE = dict(
    title="山间草甸", line="亮绿的草地、暖砂岩崖壁、蓝天白云；土路、野花、圆树和松树",
    pal={}, ground={}, rock=dict(h=1.0, top=(0.5, 0.68), n=(5, 8), p=3.2, mossy=0.55),
    light=dict(az=150, el=55, energy=4.6, color=(1.0, 0.925, 0.8), ambient=0.6),
    decor=dict(pines=0.4, mix=None, features=(), waterfall=True, bushes=True, density=1.0, dz=0.0),
    gstyle={},                       # 地面贴图的专属画法（ground_paint.paint 的 style）
    detail=dict(tuft=1.5, flower=0.16, pebble=0.3, leaf=0.22, extras=()),   # 场内贴地点缀的密度和专属小物
    field=(),                        # 场内专属的大件：fountain bridges
    ambient=dict(cols=["#FFFFFF", "#FFF6C8"], n=14, size=0.035, speed=0.7),   # 漫天飘落的小片（None = 没有）
)

THEMES = {
    "meadow": {},
    # 草甸黄昏变体：场地原样，只把天、云、雾、阳光换成低角度橙色斜阳；用 --theme meadow_dusk 点名。
    "meadow_dusk": dict(
        title="草甸·黄昏", line="草甸的地形和摆设不变，天和光换成黄昏",
        pal=dict(cloud="#FFE6D2", cloud_shade="#E89A9A", sky="#FFC59A", sky_deep="#E0708A", ambient="#B48CD0",
                 haze="#FFC9B0"),
        light=dict(az=210, el=16, energy=4.4, color=(1.0, 0.62, 0.38), ambient=0.5),
        ambient=dict(cols=["#FFD27A", "#FFB48A"], n=18, size=0.04, speed=0.6),
    ),
    "glacier": dict(
        title="冰原", line="雪白的冰面、深蓝的冰崖、金色的枯芦苇；障碍是尖冰晶，场外是披雪的松林",
        pal=dict(lip="#EEF6FD", soil="#9CC8E6", rock_a="#C6E4F4", rock_b="#93C7E8", rock_c="#62A2D8", rock_deep="#4B70C4",
                 rock_under="#3E5CAE", haze="#CFE4F8", ledge_hi="#FFFFFF",
                 stone_top="#F6FCFF", stone_lit="#C4E8F9", stone_side="#8FCAEF", stone_shade="#5F90DA", stone_base="#4C74C2",
                 moss="#FFFFFF", tall_base="#8A6A3A", tall_mid="#C9A05A", tall_tip="#F6E3AC",
                 tuft_dark="#93B6CC", tuft_light="#F6FBFF", plant0="#9DBFD4", plant1="#F2F8FC",
                 flower_main=["#FFFFFF", "#DFF3FF", "#BFE6FF"], flower_acc=["#FF8FA8", "#FFC2D1", "#FF6F91"], flower_core="#9AD0F0",
                 leaf_fall=["#FFFFFF", "#D6EAF8", "#BFD9EE"], grass="#E6F1FA", grass2="#F8FCFF", grass_rim="#FFFFFF",
                 tree_pals=[(0.7, ("#C2DAEC", "#E2EFF8", "#FFFFFF")), (0.3, ("#7FB0C8", "#B4D6E6", "#F4FBFF"))],
                 pine_pal=("#2E6F7A", "#4E9CA2", "#F2FAFF"), bush_pal=("#BCD6E8", "#F4FAFF"), trunk="#7A6A70",
                 cloud="#FFFFFF", cloud_shade="#C9DBF2", sky="#CFE6F7", sky_deep="#6FA9DC", ambient="#B4CCF2"),
        ground=dict(mid=(226, 238, 248), light=(246, 251, 255), dark=(198, 220, 240), lush=(178, 208, 234), rim=(255, 255, 255),
                    dirt=(150, 205, 234), dirt_hi=(192, 230, 246), dirt_lo=(118, 178, 218), tall=(186, 206, 220),
                    clover=(204, 228, 246), dirt_rim=(170, 200, 226), tick_light=(255, 255, 255, 120),
                    tick_dark=(150, 186, 216, 90), pebble_a=(110, 160, 205, 200), pebble_b=(235, 248, 255, 220)),
        rock=dict(h=1.35, top=(0.2, 0.36), n=(5, 7), p=2.6, mossy=0.25),
        light=dict(az=150, el=46, energy=4.2, color=(1.0, 0.96, 0.9), ambient=0.78),
        decor=dict(pines=0.75),
        ambient=dict(cols=["#FFFFFF", "#EAF6FF"], n=90, size=0.04, speed=0.8),
    ),
    "blossom": dict(
        title="春樱", line="嫩绿草地上铺着粉色花瓣，场外一圈樱花树；浅粉、嫩绿、天蓝",
        pal=dict(tall_base="#3E9E58", tall_mid="#6CC462", tall_tip="#C4EC84", tall_flower=["#FF9CC0", "#FFFFFF", "#FFC6DC", "#FF7FA8"],
                 moss="#FFC4D8",
                 flower_main=["#FFFFFF", "#FFD6E4", "#FFA9C6"], flower_acc=["#FF7FA8", "#FFE27A", "#FFFFFF"],
                 leaf_fall=["#FFB7D0", "#FFD6E4", "#FF9CBC"],
                 tree_pals=[(0.7, ("#F08FA8", "#F9B4C6", "#FFE0EA")), (0.3, ("#45A862", "#66C46C", "#A4E07E"))],
                 stone_top="#F8E6DC", stone_lit="#EDC8B8", stone_side="#D3A6A2", sky="#C4E6FA", sky_deep="#7FC4F2"),
        ground=dict(mid=(150, 204, 96), light=(186, 222, 110), dark=(112, 180, 88), lush=(248, 176, 196),
                    clover=(255, 150, 182), dirt=(232, 206, 150), tall=(120, 184, 100)),
        rock=dict(kind="garden", h=0.95, top=(0.5, 0.66), mossy=0.85),
        decor=dict(pines=0.0, mix=[(0.85, "sakura"), (0.15, "round")], features=("torii", "lantern", "lantern")),
        gstyle=dict(petals=[(255, 190, 212), (255, 226, 236), (255, 160, 192)]),
        detail=dict(tuft=1.2, flower=0.2, pebble=0.2, leaf=0.9, extras=()),
        ambient=dict(cols=["#FFB7D0", "#FFD6E4", "#FF9CBC", "#FFFFFF"], n=70, size=0.07),
    ),
    "heath": dict(
        title="石阵荒原", line="灰绿的高地草甸上开着紫色石楠，障碍是高高的灰蓝立石；灰绿、石楠紫、冷灰",
        pal=dict(lip="#93B060", soil="#8A7A6A", rock_a="#D9D6CC", rock_b="#B8B6B0", rock_c="#9A9AA2", rock_deep="#7F84A8",
                 rock_under="#6A70A0", stone_top="#E2E7EE", stone_lit="#BCC6D4", stone_side="#909CB2", stone_shade="#6C7896",
                 stone_base="#5A6484", moss="#A7B86A", tall_base="#5A4A7A", tall_mid="#8E6CB4", tall_tip="#D9A6E8",
                 tuft_dark="#6E8F4A", tuft_light="#C6D07A", flower_main=["#D9A6E8", "#B987D6", "#FFFFFF"],
                 flower_acc=["#FFE27A", "#F3B6F2", "#FFFFFF"], grass="#9CB866", grass2="#BCCB78", grass_rim="#C8D488",
                 tree_pals=[(0.6, ("#4E8A5A", "#6EA868", "#A8CC84")), (0.4, ("#B58ACB", "#CFA6DE", "#EBD2F2"))],
                 sky="#D9E4EE", sky_deep="#9DB7D4", cloud_shade="#C4CEE2", ambient="#B0BEDC", haze="#D2DCEA"),
        ground=dict(mid=(150, 178, 96), light=(186, 200, 110), dark=(120, 150, 84), lush=(152, 112, 182), rim=(196, 208, 128),
                    clover=(178, 124, 198), dirt=(196, 170, 128), dirt_hi=(214, 192, 150), dirt_lo=(166, 140, 108),
                    tall=(96, 84, 128), dirt_rim=(120, 140, 80)),
        rock=dict(kind="menhir", h=1.0, top=(0.62, 0.8), n=(4, 5), p=4.5, mossy=0.0),
        light=dict(az=150, el=50, energy=3.9, color=(1.0, 0.95, 0.88), ambient=0.72),
        decor=dict(pines=0.5, mix=[(0.45, "pine"), (0.2, "bare"), (0.35, "heather")], features=("menhirs", "menhirs", "menhirs"),
                   bushes=False, density=0.55),
        gstyle=dict(heather=[(170, 110, 196), (206, 150, 224), (140, 92, 170)], ring=True),
        detail=dict(tuft=1.3, flower=0.3, pebble=0.5, leaf=0.05, extras=("heather",)),
        ambient=dict(cols=["#D9A6E8", "#FFFFFF", "#B987D6"], n=22, size=0.045),
    ),
    "sunset": dict(
        title="晚霞", line="夕阳把草地染成金绿、崖壁烧成橙红，天是桃粉色；金、橙红、桃粉、淡紫",
        pal=dict(lip="#B8B84A", soil="#B8704A", rock_a="#FFC98A", rock_b="#F5A26A", rock_c="#E07F5E", rock_deep="#A65E9A",
                 rock_under="#7A4E9E", haze="#FFD9C2", stone_top="#FFE0A0", stone_lit="#F7B070", stone_side="#D98660",
                 stone_shade="#9A5E96", stone_base="#7A4E92", moss="#C9C24A", tall_base="#7A7A2E", tall_mid="#C9B24A",
                 tall_tip="#FFE58A", tuft_dark="#8A9A3A", tuft_light="#F0DC70", grass="#BCC050", grass2="#E0D060",
                 grass_rim="#F0E080", tree_pals=[(0.5, ("#D98A3A", "#F0A84A", "#FFD27A")), (0.5, ("#C8604A", "#E8825E", "#FFB48A"))],
                 pine_pal=("#5A6E3E", "#8A9A4A", "#E0C870"), cloud="#FFF1E0", cloud_shade="#F7B7A8", sky="#FFD2A8",
                 sky_deep="#F59A8C", ambient="#C9A6E0"),
        ground=dict(mid=(170, 190, 84), light=(214, 206, 98), dark=(128, 160, 78), lush=(110, 150, 84), rim=(236, 222, 120),
                    dirt=(236, 190, 130), dirt_hi=(250, 212, 150), dirt_lo=(206, 150, 100), tall=(110, 120, 60)),
        light=dict(az=205, el=34, energy=5.0, color=(1.0, 0.76, 0.52), ambient=0.6),
        decor=dict(pines=0.0, mix=[(0.5, "round"), (0.3, "cypress"), (0.2, "bare")], features=("lamp", "lamp", "lamp", "lamp")),
        rock=dict(kind="crates"),
        gstyle=dict(planks=0.8, wood=[(178, 120, 78), (150, 98, 66), (206, 146, 96)]),
        detail=dict(tuft=1.3, flower=0.12, pebble=0.3, leaf=0.5, extras=()),
        field=("bridges",),
        ambient=dict(cols=["#F0A84A", "#E8825E", "#FFD27A"], n=34, size=0.075),
    ),
    "ruins": dict(
        title="古垣", line="长了青苔的石板地，障碍是方正的断墙残柱；石灰白、苔绿、灰紫",
        pal=dict(lip="#86BE62", soil="#A08A6E", rock_a="#EADCB8", rock_b="#D2C09A", rock_c="#B4A088", rock_deep="#8A80A8",
                 rock_under="#6E6AA0", stone_top="#F2DFB0", stone_lit="#DEC896", stone_side="#C2A678", stone_shade="#9A8C9A",
                 stone_base="#7E6A70", moss="#86B24E", grass="#8EC068", grass2="#B0D27A", grass_rim="#C4DE8C",
                 tree_pals=[(0.8, ("#3E8A56", "#5EAA64", "#98CC7C")), (0.2, ("#D9A441", "#EFC45A", "#F9E28E"))],
                 sky="#C6E4EC", sky_deep="#86BFD6", haze="#CDE2EA"),
        ground=dict(mid=(142, 192, 104), light=(180, 212, 116), dark=(106, 168, 92), lush=(80, 150, 88), rim=(200, 224, 136),
                    clover=(96, 160, 96), dirt=(214, 198, 152), dirt_hi=(230, 216, 172), dirt_lo=(180, 164, 124),
                    tall=(66, 134, 76), dirt_rim=(120, 160, 90), slab=(222, 216, 196), slab_hi=(238, 234, 216),
                    slab_lo=(196, 190, 170)),
        rock=dict(kind="masonry", h=1.0, mossy=0.2),
        decor=dict(pines=0.3, mix=[(0.6, "round"), (0.25, "cypress"), (0.15, "pine")], features=("columns", "arch", "columns")),
        gstyle=dict(pave="near"),
        ambient=dict(cols=["#B4D080", "#EFC45A", "#98CC7C"], n=22, size=0.065),
        detail=dict(tuft=1.6, flower=0.2, pebble=0.5, leaf=0.25, extras=("rubble",)),
    ),
    "dunes": dict(
        title="沙洲", line="金黄的沙地，角落里有几丛绿洲草，障碍是橙色的风蚀砂岩；沙金、橙、湛蓝",
        pal=dict(lip="#F2D896", soil="#D9A86A", rock_a="#FFD79A", rock_b="#F2B26E", rock_c="#DB8A58", rock_deep="#A066A0",
                 rock_under="#7E549C", stone_top="#FFE4AC", stone_lit="#F5BA7A", stone_side="#DB905E", stone_shade="#A8688C",
                 stone_base="#8A5A90", moss="#FFEFC0", tuft_dark="#8AA24A", tuft_light="#D8E07A", grass="#F0D890",
                 grass2="#FAE8B0", grass_rim="#FFF2C8", leaf_fall=["#FFF2C8", "#F2C078", "#E8D48A"],
                 tree_pals=[(1.0, ("#3E9A58", "#6CC060", "#B4E47A"))], sky="#A4DAF8", sky_deep="#4FA8EE",
                 spire_bands=("#FFE2A8", "#F2B470", "#E0905A", "#FBD08C")),
        ground=dict(mid=(238, 214, 150), light=(250, 232, 176), dark=(222, 190, 124), lush=(150, 190, 96), rim=(255, 240, 196),
                    clover=(130, 180, 90), dirt=(214, 176, 112), dirt_hi=(230, 196, 132), dirt_lo=(190, 150, 96),
                    tall=(120, 160, 80), dirt_rim=(200, 170, 110), tick_light=(255, 246, 210, 120), tick_dark=(200, 164, 104, 90)),
        rock=dict(kind="hoodoo", h=1.0, bands=("#FFE2A8", "#F2B470", "#E0905A", "#FBD08C")),
        light=dict(az=150, el=58, energy=4.9, color=(1.0, 0.93, 0.78), ambient=0.62),
        decor=dict(pines=0.0, mix=[(0.55, "palm"), (0.3, "cactus"), (0.15, "spire")], features=("arch_rock",), waterfall=False,
                   bushes=False, density=0.5),
        gstyle=dict(ripples=True, ticks="none"),
        ambient=None,
        detail=dict(tuft=0.25, flower=0.03, pebble=0.5, leaf=0.0, extras=("cactus", "dryshrub")),
    ),
    "canyon": dict(
        title="红岩峡谷", line="赤陶色的地面上一条浅色沙路，崖壁是一层层的红岩，点缀灰绿的鼠尾草；赤陶红、沙白、灰绿",
        pal=dict(lip="#E8A070", soil="#C0623E", rock_a="#F0A070", rock_b="#DC7A52", rock_c="#C05A44", rock_deep="#8A4A80",
                 rock_under="#6A3E84", haze="#F2D2C4", stone_top="#F9B884", stone_lit="#E88E5E", stone_side="#C46642",
                 stone_shade="#8A4A6E", stone_base="#70407A", moss="#B8BE6A", tall_base="#6E7A3A", tall_mid="#A8B25A",
                 tall_tip="#E6E08A", tuft_dark="#7E8A46", tuft_light="#D6D680", grass="#E09868", grass2="#F0B480",
                 grass_rim="#F6C494", flower_main=["#FFE27A", "#FFFFFF", "#FFC24A"],
                 tree_pals=[(0.6, ("#7E8A46", "#A8B25A", "#DADC8A")), (0.4, ("#D9A441", "#EFC45A", "#F9E28E"))],
                 pine_pal=("#5A7A4A", "#86A45E", "#C8D88A"), sky="#A8D8F5", sky_deep="#58AEEA",
                 spire_bands=("#F2A070", "#D8704A", "#B8503E", "#E89A6C", "#C8603F")),
        ground=dict(mid=(226, 150, 96), light=(242, 178, 116), dark=(202, 122, 84), lush=(160, 174, 96), rim=(250, 198, 138),
                    clover=(140, 160, 84), dirt=(246, 210, 156), dirt_hi=(254, 228, 180), dirt_lo=(222, 180, 128),
                    tall=(130, 130, 70), dirt_rim=(190, 110, 76), tick_light=(250, 200, 140, 110), tick_dark=(170, 90, 60, 90),
                    strata=(172, 92, 68), pebble_a=(150, 84, 64, 200), pebble_b=(250, 214, 170, 210)),
        rock=dict(kind="hoodoo", h=1.12, bands=("#F2A070", "#D8704A", "#B8503E", "#E89A6C", "#C8603F")),
        light=dict(az=150, el=56, energy=4.8, color=(1.0, 0.92, 0.78), ambient=0.6),
        decor=dict(pines=0.0, mix=[(0.45, "spire"), (0.3, "juniper"), (0.25, "cactus")], features=("arch_rock", "spires"),
                   waterfall=False, bushes=False, density=0.55, dz=0.6),
        gstyle=dict(strata=True, ticks="none", planks=1.4, wood=[(190, 136, 86), (160, 108, 70), (214, 160, 104)]),
        detail=dict(tuft=0.35, flower=0.05, pebble=0.7, leaf=0.0, extras=("dryshrub", "cactus")),
        field=("bridges",),
        ambient=None,
    ),
    "oldtown": dict(
        title="老城", line="暖灰的石板广场，障碍是一栋栋赤陶屋顶的小房子，墙角长着草；米白、赤陶红、暖灰",
        pal=dict(lip="#D8CDB4", soil="#A89884", rock_a="#E6D8BC", rock_b="#D0BE9E", rock_c="#B6A08C", rock_deep="#9084A8",
                 rock_under="#746EA0", stone_top="#EC7A52", stone_lit="#F8EBCF", stone_side="#E3D0AC", stone_shade="#B6A0AC",
                 stone_base="#8E7C96", moss="#D55E3E", grass="#CFC4A8", grass2="#E0D6BA", grass_rim="#EAE0C6",
                 tree_pals=[(0.75, ("#45A862", "#66C46C", "#A4E07E")), (0.25, ("#E2796B", "#F29A8A", "#FCC7B8"))]),
        ground=dict(mid=(206, 192, 170), light=(226, 214, 190), dark=(180, 166, 150), lush=(120, 170, 96), rim=(236, 226, 204),
                    clover=(110, 160, 90), dirt=(232, 222, 200), dirt_hi=(244, 236, 216), dirt_lo=(200, 188, 168),
                    tall=(70, 136, 76), dirt_rim=(176, 164, 146), tick_light=(240, 232, 212, 110), tick_dark=(150, 138, 124, 90),
                    slab=(224, 206, 174), slab_hi=(240, 226, 198), slab_lo=(200, 180, 152), joint=(150, 132, 114)),
        rock=dict(kind="houses"),
        decor=dict(pines=0.0, mix=[(0.55, "house"), (0.3, "round"), (0.15, "cypress")], features=("tower", "tower"), waterfall=False,
                   bushes=True, density=0.8),
        gstyle=dict(pave="all", ring=True, ticks="none"),
        detail=dict(tuft=0.3, flower=0.05, pebble=0.15, leaf=0.3, extras=("pots",)),
        field=("fountain",),
        ambient=None,
    ),
    "forest": dict(
        title="深林", line="深绿的林间空地，蕨丛、苔石、红白小蘑菇，场外是密密的大树；墨绿、苔绿、土褐",
        pal=dict(lip="#5AA050", soil="#7A5A44", rock_a="#C9B894", rock_b="#A8946E", rock_c="#8A7860", rock_deep="#6E6690",
                 rock_under="#585488", stone_top="#CFC5AC", stone_lit="#AEA28C", stone_side="#8A7F76", stone_shade="#6A6480",
                 stone_base="#575272", moss="#5EAE4E", tall_base="#1F6A44", tall_mid="#3C9A56", tall_tip="#8AD06A",
                 tuft_dark="#2F7E46", tuft_light="#8AD06A", grass="#62A656", grass2="#86BE62", grass_rim="#96C86A",
                 flower_main=["#FFFFFF", "#FF6A5A", "#FFD84A"], flower_acc=["#FF6A5A", "#FFFFFF", "#F3B6F2"],
                 leaf_fall=["#D9A441", "#B8743A", "#E8C25A"],
                 tree_pals=[(0.7, ("#2F8050", "#479E5A", "#84C672")), (0.3, ("#3E9A6A", "#62B87A", "#A6DC96"))],
                 pine_pal=("#1F6652", "#2F8462", "#6CB886"), sky="#B9DCE8", sky_deep="#6FAFC8", ambient="#96B6DC",
                 haze="#BEDCE4", crown_pal=("#2A7A4C", "#46A05A", "#9AD476")),
        ground=dict(mid=(96, 160, 84), light=(140, 190, 96), dark=(66, 130, 76), lush=(50, 112, 72), rim=(150, 200, 104),
                    clover=(60, 130, 80), dirt=(150, 120, 84), dirt_hi=(176, 146, 104), dirt_lo=(120, 94, 70),
                    tall=(40, 104, 66), dirt_rim=(70, 110, 64), tick_light=(170, 214, 120, 100), tick_dark=(40, 100, 60, 90),
                    root=(110, 82, 58)),
        rock=dict(kind="trees"),
        light=dict(az=150, el=52, energy=4.4, color=(1.0, 0.9, 0.72), ambient=0.52),
        decor=dict(pines=0.0, mix=[(0.7, "bigtree"), (0.3, "pine")], features=("shrooms", "shrooms"), density=1.5),
        ambient=dict(cols=["#84C672", "#D9A441", "#A6DC96", "#E8C25A"], n=36, size=0.08),
        gstyle=dict(litter=[(222, 160, 72), (186, 112, 60), (236, 198, 96)], roots=True),
        detail=dict(tuft=1.8, flower=0.1, pebble=0.2, leaf=1.2, extras=("mushroom", "fern")),
    ),
    # ---------------- 第 12 集的三张图（新加的键：liquid 液面、gstyle 的 ash / deck / castle、rock.kind 的 basalt / harbor / castle、
    #                  decor.waterfall="lava"、ambient 的 rise / glow / extra）
    "caldera": dict(
        title="火山口", line="一圈深色火山岩围着发光的岩浆湖，地上有灰烬和发光的裂缝，障碍是玄武岩柱；炭灰、熔岩橙、烟紫",
        pal=dict(lip="#7A6A6E", soil="#5E4A4E", rock_a="#8E7A7C", rock_b="#6E5E68", rock_c="#58506A", rock_deep="#46426E",
                 rock_under="#3A3868", haze="#E8C4C0", ledge_hi="#C9A896",
                 stone_top="#B4AABC", stone_lit="#76728A", stone_side="#5E5C78", stone_shade="#4A4862", stone_base="#3A3850",
                 moss="#FF9A4A", tall_base="#5E3A34", tall_mid="#C0603E", tall_tip="#FFC05A",
                 tall_flower=["#FFD46A", "#FF9A4A", "#FFE9A8"],
                 tuft_dark="#6E5A50", tuft_light="#C8A070", plant0="#6E5A50", plant1="#B8946A",
                 flower_main=["#FFC24A", "#FF8A3A", "#FFE08A"], flower_acc=["#FF6A3A", "#FFD46A", "#FFFFFF"], flower_core="#FFF2C0",
                 leaf_fall=["#8A7E80", "#A89A98", "#6E6466"], grass="#8A7A78", grass2="#A08E88", grass_rim="#B8A498",
                 tree_pals=[(0.6, ("#C8503A", "#E8784A", "#FFB060")), (0.4, ("#8A6A5A", "#A88A6A", "#D6B88A"))],
                 pine_pal=("#4A4A5A", "#6A6676", "#A89CA0"), bush_pal=("#A8503E", "#E8884E"), trunk="#3E3440",
                 cloud="#FBEAE6", cloud_shade="#C8A0B4", sky="#FFD2B8", sky_deep="#E0909A", ambient="#C8A8D0"),
        ground=dict(mid=(132, 116, 112), light=(178, 164, 152), dark=(92, 78, 84), lush=(104, 90, 92), rim=(184, 166, 152),
                    clover=(110, 100, 104), dirt=(170, 156, 146), dirt_hi=(190, 176, 164), dirt_lo=(140, 126, 120),
                    tall=(96, 70, 66), dirt_rim=(110, 98, 100), tick_light=(190, 176, 168, 90), tick_dark=(70, 62, 70, 80),
                    pebble_a=(70, 64, 78, 210), pebble_b=(196, 180, 170, 200),
                    glow=(255, 150, 60), glow_hi=(255, 226, 140), heat=(196, 104, 76)),
        rock=dict(kind="basalt", h=1.0),
        light=dict(az=150, el=50, energy=4.5, color=(1.0, 0.86, 0.72), ambient=0.6),
        decor=dict(pines=0.0, mix=[(0.4, "basalt"), (0.35, "charred"), (0.25, "emberbush")], features=("volcano", "volcano", "smoke"),
                   waterfall="lava", bushes=False, density=0.6),
        gstyle=dict(ash=True, ticks="none"),
        detail=dict(tuft=0.35, flower=0.05, pebble=0.6, leaf=0.15, extras=("obsidian", "vent")),
        liquid=dict(kind="lava", z=-0.5, where="enclosed", min_cells=1),
        ambient=dict(cols=["#FFB24A", "#FF7A3A", "#FFE08A"], n=46, size=0.035, speed=0.9, rise=True, glow=3.0,
                     extra=dict(cols=["#B8AEB0", "#8E8488", "#D8D0D0"], n=26, size=0.05, speed=0.6)),
    ),
    "harbor": dict(
        title="港湾", line="两岸的沙滩码头之间三座木栈桥，桥下是碧蓝的海水，障碍是渔具堆；沙白、海蓝、木色、浮标红",
        pal=dict(lip="#F0DEB0", soil="#C8A478", rock_a="#F2DCB0", rock_b="#E0C09A", rock_c="#C8A08C", rock_deep="#8A86B0",
                 rock_under="#6E74AA", haze="#CDE6F4",
                 stone_top="#F4E6CA", stone_lit="#E2CCA6", stone_side="#C8AC8A", stone_shade="#9A8EA2", stone_base="#7E7494",
                 moss="#9CC870", tall_base="#8A8A46", tall_mid="#C8C070", tall_tip="#F4ECB0",
                 tuft_dark="#8AA05A", tuft_light="#D8DE8A", grass="#F2E2B4", grass2="#FAEED0", grass_rim="#FFF6DE",
                 leaf_fall=["#FFFFFF", "#F2F2F2", "#E4ECF2"],
                 tree_pals=[(0.7, ("#3E9A58", "#62B862", "#A6DC7E")), (0.3, ("#2E8A6A", "#4EAA7A", "#94D49A"))],
                 sky="#A6DCF8", sky_deep="#4CA4EC", cloud_shade="#CBDDF4", ambient="#A8C4F2"),
        ground=dict(mid=(240, 222, 172), light=(250, 238, 200), dark=(226, 204, 152), lush=(156, 186, 106), rim=(255, 246, 216),
                    clover=(150, 182, 100), dirt=(222, 196, 146), dirt_hi=(236, 214, 168), dirt_lo=(200, 172, 124),
                    tall=(160, 168, 96), dirt_rim=(214, 190, 140), tick_light=(255, 250, 226, 110), tick_dark=(206, 180, 128, 80),
                    slab=(222, 214, 196), slab_hi=(238, 232, 216), slab_lo=(196, 188, 172), joint=(160, 150, 136)),
        rock=dict(kind="harbor"),
        light=dict(az=150, el=55, energy=4.7, color=(1.0, 0.94, 0.84), ambient=0.64),
        decor=dict(pines=0.0, mix=[(0.35, "house"), (0.4, "round"), (0.25, "seastack")], features=("lighthouse", "boat", "boat"),
                   waterfall=True, bushes=True, density=0.7),
        gstyle=dict(deck=2.0, ripples=True, ticks="none", wood=[(196, 150, 104), (170, 124, 86), (216, 174, 124)]),
        detail=dict(tuft=0.6, flower=0.05, pebble=0.4, leaf=0.0, extras=("shells", "drygrass")),
        field=("piers",),
        liquid=dict(kind="water", z=-0.4, where="enclosed", min_cells=1),
        ambient=dict(cols=["#FFFFFF", "#F2F6FA"], n=8, size=0.05, speed=0.5),
    ),
    "keeps": dict(
        title="双堡", line="草坡上东西两座带垛口和角楼的小城堡，中间是石板广场和小池塘；城堡挂着红蓝两队的旗；草绿、石灰、红、蓝",
        pal=dict(lip="#86C060", soil="#A88A6E",
                 stone_top="#EEE6D6", stone_lit="#DCD2BE", stone_side="#BEB4A4", stone_shade="#968EA0", stone_base="#7A7290",
                 moss="#7CB44E", grass="#8CC85E", grass2="#A8D86A", grass_rim="#C2E480",
                 leaf_fall=["#9AD06A", "#F2C14E", "#FFB7D0"],
                 tree_pals=[(0.75, ("#3E9A58", "#5EB862", "#A0DC7E")), (0.25, ("#E08A9A", "#F2AABA", "#FFD6E0"))]),
        ground=dict(slab=(220, 214, 200), slab_hi=(236, 232, 220), slab_lo=(196, 190, 178), joint=(150, 144, 136),
                    dirt=(214, 204, 180), dirt_hi=(228, 220, 198), dirt_lo=(186, 176, 154)),
        rock=dict(kind="castle", red="#FF5B4D", blue="#2E8CFF"),
        decor=dict(pines=0.0, mix=[(0.4, "round"), (0.3, "cypress"), (0.3, "house")], features=("keep_tower", "keep_tower", "keep_tower"),
                   waterfall=True, bushes=True, density=0.9),
        gstyle=dict(castle=True),
        detail=dict(tuft=1.3, flower=0.18, pebble=0.25, leaf=0.3, extras=()),
        field=("castle",),
        liquid=dict(kind="water", z=-0.35, where="enclosed", min_cells=1),
        ambient=dict(cols=["#9AD06A", "#F2C14E", "#FFB7D0", "#FFFFFF"], n=26, size=0.06),
    ),
}

# 定稿 10 张图各自的主题（地图文件里也写了 // THEME 行；这里是给只有地图名的回放用的）
MAP_THEME = {
    "twinlakes": "meadow", "yinyang": "blossom", "stonecircle": "heath", "skybridge": "sunset", "ruins": "ruins",
    "icefloes": "glacier", "hourglass": "dunes", "canyon": "canyon", "oldtown": "oldtown", "forest": "forest",
    "caldera": "caldera", "piers": "harbor", "keeps": "keeps",
    "wheatfield": "sunset", "fjord": "glacier", "shards": "heath", "fortress": "ruins", "isles": "dunes",
}


def get(name):
    t = dict(BASE)
    ov = THEMES.get(name, {})
    for k, v in ov.items():
        if isinstance(v, dict) and isinstance(BASE.get(k), dict):
            d = dict(BASE[k])
            d.update(v)
            t[k] = d
        else:
            t[k] = v
    t["name"] = name if name in THEMES else "meadow"
    return t


def resolve(rp):
    """定下这份回放 / 地图用哪个主题，写进 rp.theme 并返回主题字典。"""
    name = os.environ.get("DART_THEME") or getattr(rp, "theme", None) or MAP_THEME.get(rp.mapname, "meadow")
    if name not in THEMES:
        name = "meadow"
    rp.theme = name
    return get(name)
