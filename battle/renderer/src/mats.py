# -*- coding: utf-8 -*-
"""地形材质（Blender）。所有地形物体共用同一张材质表，材质号固定：
  0 地面顶（草 / 沙 / 雪，按主题）  1 崖壁侧面（一层层的岩层，越深越暗、融进深谷色）  2 顶点色（桥、房子、树、石头等程序生成的物件）
  3 冰面  4 铺石（石板、卵石）  5 岩石顶面  6 砌墙  7 水  8 深谷底  9 泥塘  10 土路  11 台地顶（干草色）  12 台地上的土路
材质全是程序化的（世界坐标噪声），没有贴图。
"""
import bpy

from scene_lib import NB, hexc, _MATS

KIND = {
    "grass": "ground", "ground": "ground", "grass_hill": "ground", "meadow": "ground", "turf": "ground", "dirt": "ground",
    "sand": "ground", "sand_wet": "sand_wet", "snow": "ground", "plateau": "ground", "mesa": "ground",
    "ice": "ice", "ice_road": "ice",
    "cobble": "pave", "flagstone": "pave", "pave": "pave", "street": "pave",
    "rock": "rock", "cliff": "rock", "stone": "rock", "red_rock": "rock", "crag": "rock",
    "stone_wall": "wall", "wall": "wall", "rampart": "wall", "masonry": "wall",
    "bridge_wood": "bridge", "bridge": "bridge", "plank": "plank", "wood": "bridge",
    "house": "house", "bell_tower": "tower", "tower": "tower",
    "tree": "tree", "cherry_tree": "tree", "pine": "tree", "trunk": "tree",
    "reef": "reef", "seabed": "deepfloor", "riverbed": "deepfloor", "abyss": "deepfloor",
    "water": "water", "mud": "mud",
    # 引擎正式关卡文件里的材质名
    "stone_plateau": "plateau", "stone_ground": "ground", "dirt_path": "ground", "stone_ramp": "pave",
    "city_wall": "wall", "rock_wall": "rock", "rubble": "rock",
}

# kind -> (顶面材质号, 侧面材质号)
SLOTS = {"ground": (0, 1), "sand_wet": (0, 1), "ice": (3, 1), "pave": (4, 1), "rock": (5, 1), "wall": (6, 6), "water": (7, 7),
         "deepfloor": (8, 8), "mud": (9, 1), "mound": (0, 1), "plateau": (11, 1)}


def kind_of(m):
    if m in KIND:
        return KIND[m]
    if "tree" in m:
        return "tree"
    if "wall" in m:
        return "wall"
    if "rock" in m or "stone" in m:
        return "rock"
    return "ground"


def _P(nb):
    return nb.out(nb.geo(), 'Position')


def _wallvec(nb, P):
    """竖墙上铺砖用的坐标：(x + y, z)，东西向、南北向的墙面都是横着的砖缝。"""
    sp = nb.sep(P)
    cb = nb.node("ShaderNodeCombineXYZ")
    nb.link(nb.math('ADD', sp.outputs['X'], sp.outputs['Y']), cb.inputs['X'])
    nb.link(sp.outputs['Z'], cb.inputs['Y'])
    return cb.outputs[0]


def _c(L, key, i):
    return nb_rgb_cache(L[key][i])


def nb_rgb_cache(h):
    return hexc(h)


def m_ground(L, name="M_ground", cols=None, rough=0.95, path=None, shore=False):
    """地面：三种颜色按世界坐标噪声混。path = (路色1, 路色2)：顶点色 R 通道当"路的浓度"，边上 0 中间 1，和地面无缝过渡。"""
    m = bpy.data.materials.new(name)
    nb = NB(m)
    g0, g1, g2 = cols or L["ground"]
    P = _P(nb)
    n1 = nb.noise(P, 0.07, 2.0, 0.55)
    n2 = nb.noise(P, 0.45, 3.0, 0.6)
    n3 = nb.noise(P, 0.018, 1.0, 0.5)
    col = nb.mix(nb.smooth(n1, 0.38, 0.62), nb.rgb(hexc(g0)), nb.rgb(hexc(g1)))
    col = nb.mix(nb.math('MULTIPLY', nb.smooth(n2, 0.58, 0.74), 0.85), col, nb.rgb(hexc(g2)))
    col = nb.mix(nb.math('MULTIPLY', nb.smooth(n3, 0.3, 0.75), 0.25), col, nb.rgb(hexc(g2)))
    # 大块的深浅（十几米一片）+ 很细的颗粒：远看有层次，近看不是一块平色；幅度小，不抢兵
    n5 = nb.noise(P, 0.035, 2.0, 0.5)
    hsv = nb.node("ShaderNodeHueSaturation")
    nb.link(col, hsv.inputs['Color'])
    n6 = nb.noise(P, 2.6, 1.0, 0.5)
    nb.link(nb.math('ADD', nb.math('ADD', 0.86, nb.math('MULTIPLY', nb.smooth(n5, 0.35, 0.7), 0.16)),
                    nb.math('MULTIPLY', nb.math('SUBTRACT', n6, 0.5), 0.07)), hsv.inputs['Value'])
    col = hsv.outputs['Color']
    # 坡面稍暗
    nz = nb.sep(nb.out(nb.geo(), 'Normal')).outputs['Z']
    col = nb.mix(nb.math('MULTIPLY', nb.smooth(nz, 0.99, 0.75), 0.35), col, nb.rgb(hexc(g0)))
    # 低处（崖下、远处的谷地）罩一层天色的薄雾，拉开远近
    zz = nb.sep(P).outputs['Z']
    col = nb.mix(nb.math('MULTIPLY', nb.smooth(zz, -8.0, -34.0), 0.45), col, nb.rgb(hexc(L["sky"][0])))
    wet = None
    if shore:
        # 沙洲：① 风吹出来的沙纹（一道道弯曲的细条，近看才明显）；② 越往岛顶越干越白，低处偏黄偏深；
        # ③ 岸线：水面以上一米来宽的湿沙（深一号、发亮），贴水一道白浪花和一圈更深的"刚退下去"的水印。
        #    水面高度由 "tide_level" 这个值节点给，每帧更新（潮水涨，湿沙带跟着往岛中间走）
        wv = nb.node("ShaderNodeTexWave", wave_type='BANDS', bands_direction='DIAGONAL', wave_profile='SIN')
        nb.link(P, wv.inputs['Vector'])
        wv.inputs['Scale'].default_value = 0.55
        wv.inputs['Distortion'].default_value = 7.0
        wv.inputs['Detail'].default_value = 1.5
        wv.inputs['Detail Scale'].default_value = 0.6
        rip = nb.out(wv, 'Factor')
        ripk = nb.smooth(nb.noise(P, 0.06, 1.0, 0.5), 0.3, 0.6)            # 沙纹有的地方明显、有的地方淡
        col = nb.mix(nb.math('MULTIPLY', nb.smooth(rip, 0.62, 0.95), nb.math('MULTIPLY', ripk, 0.22)), col,
                     nb.rgb(hexc(L.get("ripple", "#C9A066"))))
        col = nb.mix(nb.math('MULTIPLY', nb.smooth(rip, 0.25, 0.02), nb.math('MULTIPLY', ripk, 0.16)), col,
                     nb.rgb(hexc(L["ground"][2])))
        col = nb.mix(nb.math('MULTIPLY', nb.smooth(zz, 1.2, 5.2), 0.22), col, nb.rgb(hexc(L.get("dry", "#F0DCA8"))))
        col = nb.mix(nb.math('MULTIPLY', nb.smooth(zz, 1.4, -0.4), 0.35), col, nb.rgb(hexc(L["ground"][0])))
        tv = nb.node("ShaderNodeValue")
        tv.name = "tide_level"
        dd = nb.math('SUBTRACT', zz, tv.outputs[0])
        edge = nb.math('ADD', dd, nb.math('MULTIPLY', nb.math('SUBTRACT', nb.noise(P, 0.5, 2.0, 0.5), 0.5), 0.25))
        wet = nb.smooth(edge, 0.85, 0.1)
        col = nb.mix(nb.math('MULTIPLY', wet, 0.9), col, nb.rgb(hexc(L.get("wet", "#B08650"))))
        col = nb.mix(nb.math('MULTIPLY', nb.smooth(edge, 0.3, 0.05), 0.6), col, nb.rgb(hexc(L.get("wet2", "#8A6840"))))
        fn = nb.noise(P, 1.3, 2.0, 0.6)
        foam = nb.math('MULTIPLY', nb.smooth(dd, 0.13, 0.04), nb.smooth(fn, 0.3, 0.5))
        col = nb.mix(nb.math('MULTIPLY', foam, 0.9), col, nb.rgb((1.0, 1.0, 1.0, 1.0)))
    if path:
        f = nb.sep(nb.out(nb.attr('col'), 'Color')).outputs['X']
        n4 = nb.noise(P, 0.6, 2.0, 0.6)
        f = nb.math('MULTIPLY', f, nb.math('ADD', 0.55, nb.math('MULTIPLY', n4, 0.6)), clamp=True)
        pc = nb.mix(nb.smooth(n2, 0.4, 0.6), nb.rgb(hexc(path[0])), nb.rgb(hexc(path[1])))
        col = nb.mix(nb.smooth(f, 0.25, 0.65), col, pc)
    p = nb.principled(col, rough=rough, spec=0.08, emission=col, emis_strength=0.04)
    if wet is not None:                       # 湿沙发亮
        nb.link(nb.math('SUBTRACT', rough, nb.math('MULTIPLY', wet, 0.62)), p.inputs['Roughness'])
        nb.link(nb.math('ADD', 0.08, nb.math('MULTIPLY', wet, 0.4)), p.inputs['Specular IOR Level'])
    nb.output(p)
    return m


def m_cliff(L):
    """崖壁：一层层的岩层（按高度分带、带一点噪声起伏），越往下越暗，深处融进深谷色。"""
    m = bpy.data.materials.new("M_cliff")
    nb = NB(m)
    c0, c1, c2, c3 = L["cliff"]
    P = _P(nb)
    sp = nb.sep(P)
    z = sp.outputs['Z']
    if L.get("cliff_style") == "masonry":
        br = nb.node("ShaderNodeTexBrick")
        nb.link(_wallvec(nb, P), br.inputs['Vector'])
        br.inputs['Color1'].default_value = hexc(c0)
        br.inputs['Color2'].default_value = hexc(c1)
        br.inputs['Mortar'].default_value = hexc(c2)
        br.inputs['Scale'].default_value = 1.2
        br.inputs['Mortar Size'].default_value = 0.03
        br.inputs['Brick Width'].default_value = 0.75
        br.inputs['Row Height'].default_value = 0.42
        col = br.outputs['Color']
        n = nb.noise(P, 0.35, 2.0, 0.5)
        col = nb.mix(nb.math('MULTIPLY', nb.smooth(n, 0.62, 0.78), 0.6), col, nb.rgb(hexc("#8FB25A")))
    elif L.get("cliff_style") == "columns":
        # 冰崖：竖着的一根根冰柱（Voronoi 在竖直方向拉长），柱缝暗一号
        sc = nb.node("ShaderNodeVectorMath", operation='MULTIPLY')
        nb.link(P, sc.inputs[0])
        sc.inputs[1].default_value = (0.55, 0.55, 0.07)
        vor = nb.node("ShaderNodeTexVoronoi")
        nb.link(sc.outputs[0], vor.inputs['Vector'])
        vor.inputs['Scale'].default_value = 1.0
        cell = nb.out(vor, 'Color')
        cv = nb.sep(cell).outputs['X']
        col = nb.mix(nb.smooth(cv, 0.3, 0.45), nb.rgb(hexc(c0)), nb.rgb(hexc(c1)))
        col = nb.mix(nb.smooth(cv, 0.75, 0.85), col, nb.rgb(hexc(c2)))
        ve = nb.node("ShaderNodeTexVoronoi", feature='DISTANCE_TO_EDGE')
        nb.link(sc.outputs[0], ve.inputs['Vector'])
        ve.inputs['Scale'].default_value = 1.0
        col = nb.mix(nb.math('MULTIPLY', nb.smooth(nb.out(ve, 'Distance'), 0.06, 0.0), 0.7), col, nb.rgb(hexc(c3)))
        lip = nb.smooth(nb.noise(P, 0.4, 2.0, 0.5), 0.6, 0.7)
        col = nb.mix(nb.math('MULTIPLY', lip, 0.3), col, nb.rgb(hexc(c0)))
    else:
        wob = nb.math('MULTIPLY', nb.noise(P, 0.12, 2.0, 0.5), 1.6)
        band = nb.math('FRACT', nb.math('DIVIDE', nb.math('ADD', z, wob), 1.15))
        col = nb.mix(nb.smooth(band, 0.30, 0.36), nb.rgb(hexc(c0)), nb.rgb(hexc(c1)))
        col = nb.mix(nb.smooth(band, 0.70, 0.76), col, nb.rgb(hexc(c2)))
    fine = nb.noise(P, 1.6, 2.0, 0.6)
    col = nb.mix(nb.math('MULTIPLY', nb.smooth(fine, 0.55, 0.75), 0.25), col, nb.rgb(hexc(c2)))
    # 往下越来越暗，深处融进深谷色
    d = nb.smooth(z, -0.5, -9.0)
    col = nb.mix(nb.math('MULTIPLY', d, 0.55), col, nb.rgb(hexc(c3)))
    d2 = nb.smooth(z, -6.0, -18.0)
    col = nb.mix(d2, col, nb.rgb(hexc(L["deep"])))
    p = nb.principled(col, rough=0.9, spec=0.05, emission=col, emis_strength=0.05)
    nb.output(p)
    return m


def m_ice(L):
    m = bpy.data.materials.new("M_ice")
    nb = NB(m)
    i0, i1, i2 = L.get("ice", L["ground"])
    P = _P(nb)
    n1 = nb.noise(P, 0.12, 2.0, 0.5)
    col = nb.mix(nb.smooth(n1, 0.4, 0.6), nb.rgb(hexc(i0)), nb.rgb(hexc(i1)))
    vor = nb.node("ShaderNodeTexVoronoi", feature='DISTANCE_TO_EDGE')
    nb.link(P, vor.inputs['Vector'])
    vor.inputs['Scale'].default_value = 0.35
    crack = nb.smooth(nb.out(vor, 'Distance'), 0.02, 0.0)
    col = nb.mix(nb.math('MULTIPLY', crack, 0.3), col, nb.rgb(hexc(i2)))
    # 顺着冰道方向的划痕（拉长的噪声）
    st = nb.node("ShaderNodeVectorMath", operation='MULTIPLY')
    nb.link(P, st.inputs[0])
    st.inputs[1].default_value = (0.08, 1.6, 1.0)
    streak = nb.noise(st.outputs[0], 1.0, 2.0, 0.5)
    col = nb.mix(nb.math('MULTIPLY', nb.smooth(streak, 0.56, 0.7), 0.55), col, nb.rgb(hexc(i2)))
    p = nb.principled(col, rough=0.22, spec=0.55, emission=col, emis_strength=0.08)
    nb.output(p)
    return m


def m_brick(name, cols, mortar, scale=1.0, bw=0.5, rh=0.25, emis=0.04, top_noise=True, vertical=False):
    m = bpy.data.materials.new(name)
    nb = NB(m)
    P = _P(nb)
    br = nb.node("ShaderNodeTexBrick")
    nb.link(_wallvec(nb, P) if vertical else P, br.inputs['Vector'])
    br.inputs['Color1'].default_value = hexc(cols[0])
    br.inputs['Color2'].default_value = hexc(cols[1])
    br.inputs['Mortar'].default_value = hexc(mortar)
    br.inputs['Scale'].default_value = scale
    br.inputs['Mortar Size'].default_value = 0.025
    br.inputs['Brick Width'].default_value = bw
    br.inputs['Row Height'].default_value = rh
    br.offset = 0.5
    col = br.outputs['Color']
    if top_noise:
        n = nb.noise(P, 0.3, 2.0, 0.5)
        col = nb.mix(nb.math('MULTIPLY', nb.smooth(n, 0.6, 0.8), 0.35), col, nb.rgb(hexc(cols[2])))
    p = nb.principled(col, rough=0.9, spec=0.06, emission=col, emis_strength=emis)
    nb.output(p)
    return m


def m_flat(name, h, rough=0.9, emis=0.05):
    m = bpy.data.materials.new(name)
    nb = NB(m)
    c = nb.rgb(hexc(h))
    p = nb.principled(c, rough=rough, spec=0.05, emission=c, emis_strength=emis)
    nb.output(p)
    return m


def m_mud(L):
    """泥塘：深浅不一的烂泥，中间一片片积水（反光、偏冷色），不是一块平的棕色。"""
    m = bpy.data.materials.new("M_mud")
    nb = NB(m)
    P = _P(nb)
    n1 = nb.noise(P, 0.35, 3.0, 0.6)
    n2 = nb.noise(P, 1.4, 2.0, 0.6)
    col = nb.mix(nb.smooth(n1, 0.35, 0.65), nb.rgb(hexc("#5E4630")), nb.rgb(hexc("#7C5E3E")))
    col = nb.mix(nb.math('MULTIPLY', nb.smooth(n2, 0.55, 0.7), 0.6), col, nb.rgb(hexc("#94744C")))
    pud = nb.smooth(nb.noise(P, 0.22, 2.0, 0.5), 0.52, 0.58)
    col = nb.mix(nb.math('MULTIPLY', pud, 0.85), col, nb.rgb(hexc("#56646E")))
    p = nb.principled(col, rough=0.6, spec=0.3, emission=col, emis_strength=0.03)
    nb.link(nb.math('SUBTRACT', 0.62, nb.math('MULTIPLY', pud, 0.55)), p.inputs['Roughness'])
    nb.output(p)
    return m


def m_water(L):
    m = bpy.data.materials.new("M_water_t")
    nb = NB(m)
    w0, w1, w2 = L["water"]
    P = _P(nb)
    lw = nb.node("ShaderNodeLayerWeight")
    lw.inputs['Blend'].default_value = 0.35
    col = nb.mix(nb.out(lw, 'Facing'), nb.rgb(hexc(w0)), nb.rgb(hexc(w1)))
    n = nb.noise(P, 0.6, 2.0, 0.5)
    col = nb.mix(nb.math('MULTIPLY', nb.smooth(n, 0.70, 0.74), 0.35), col, nb.rgb(hexc(w2)))
    if L.get("shallow"):
        # 岛周围一圈浅水（颜色浅、发绿），贴岸一道碎浪。岸线半径由 "shore_r" 值节点给（潮水涨了岸线就收），圆心 "shore_cx/cy"
        sp = nb.sep(P)
        vs = {}
        for nm_, dv in (("shore_r", 35.0), ("shore_cx", 0.0), ("shore_cy", 0.0)):
            v = nb.node("ShaderNodeValue")
            v.name = nm_
            v.outputs[0].default_value = dv
            vs[nm_] = v.outputs[0]
        dx = nb.math('SUBTRACT', sp.outputs['X'], vs["shore_cx"])
        dy = nb.math('SUBTRACT', sp.outputs['Y'], vs["shore_cy"])
        dist = nb.math('SQRT', nb.math('ADD', nb.math('MULTIPLY', dx, dx), nb.math('MULTIPLY', dy, dy)))
        off = nb.math('SUBTRACT', dist, vs["shore_r"])
        col = nb.mix(nb.math('MULTIPLY', nb.smooth(off, 16.0, 0.0), 0.85), col, nb.rgb(hexc(L["shallow"])))
        n2 = nb.noise(P, 0.9, 2.0, 0.6)
        foam = nb.math('MULTIPLY', nb.smooth(off, 1.6, 0.2), nb.smooth(n2, 0.42, 0.56))
        col = nb.mix(nb.math('MULTIPLY', foam, 0.85), col, nb.rgb((1.0, 1.0, 1.0, 1.0)))
    p = nb.principled(col, rough=0.08, spec=0.6, emission=col, emis_strength=0.12)
    nb.output(p)
    return m


def m_vcol():
    import scene_lib as SL
    return SL.mat_vcol("M_tvcol", rough=0.88, spec=0.08, emis=0.07)


def all_mats(L):
    key = ("terrain", L["name"])
    if key in _MATS:
        return _MATS[key]
    wall = L["wall"]
    lst = [
        m_ground(L, shore=bool(L.get("shallow"))),
        m_cliff(L),
        m_vcol(),
        m_ice(L),
        m_brick("M_pave", L.get("pave", (L["wall"][0], L["wall"][1], L["wall"][1])), L.get("mortar", L["wall"][2]), scale=1.8, bw=0.9, rh=0.6),
        m_ground(L, "M_rocktop", cols=L["rock"][::-1][1:] + (L["rock"][0],), rough=0.9),
        m_brick("M_wall", (wall[0], wall[1], wall[2]), wall[2], scale=1.6, bw=0.55, rh=0.28, vertical=True),
        m_water(L),
        m_flat("M_deepfloor", L["deep"], emis=0.2),
        m_mud(L),
        m_ground(L, "M_path", path=L["ground2"]),
        m_ground(L, "M_plateau", cols=L.get("plateau", ("#B3AC7C", "#C4BC8C", "#D8D0A6"))),
        m_ground(L, "M_path_plateau", cols=L.get("plateau", ("#B3AC7C", "#C4BC8C", "#D8D0A6")), path=L["ground2"]),
    ]
    _MATS[key] = lst
    return lst


def materials_for(key, L):
    return all_mats(L)
