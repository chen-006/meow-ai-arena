# -*- coding: utf-8 -*-
"""八张图的美术主题（纯 Python）：地面、崖壁、石头、木头、水、植物、天空、阳光的颜色，以及场外布景用什么。
配色沿用节目以前做过的主题里对应的那几套，改成这一集的地形用法。

地图名 -> 主题：
  草甸迎击 = meadow 山间草甸    断龙桥 = canyon 红岩峡谷    拔炮台 = ruins 古垣         一线天 = glacier 冰原峡谷
  镇山兽 = heath 石阵荒原       樱丘 = blossom 春樱         河心洲 = oldtown 老城       涨潮沙洲 = dunes 沙洲
"""

BASE = dict(
    title="山间草甸",
    ground=("#6EA548", "#84B655", "#A6CA6A"),        # 地面：暗、中、亮
    ground2=("#C8A06A", "#D8B47E"),                    # 土路 / 裸地
    cliff=("#E8C08A", "#D9A272", "#C08A6E", "#8E78A6"),  # 崖壁：顶、中、下、最深（紫灰）
    rock=("#F2D29E", "#D9AE80", "#A88E98"),            # 石头：亮、中、暗
    wall=("#E2D6BC", "#C4B496", "#9A8E80"),            # 砌墙
    wood=("#B07A48", "#8A5A34", "#D8A060"),
    water=("#5AB4E0", "#3A88C8", "#BFE8F8"),
    deep="#3E3A66",                                    # 深谷底下融进去的颜色
    tree=("pine", "round"),
    leaf=("#3E8A4E", "#5AAE5C", "#9AD47A"),
    trunk="#8A6048",
    accent=("#FFFFFF", "#FFE27A", "#FF9C8A"),          # 小花
    sky=("#A9D6FA", "#6DB6F2"),
    ambient="#A8BEF0",
    sun=dict(az=150, el=50, energy=4.4, color=(1.0, 0.93, 0.82)),
    amb_strength=0.75,
)

LOOKS = {
    "meadow": dict(),
    "canyon": dict(
        title="红岩峡谷",
        ground=("#D88A58", "#E49C66", "#F0B47E"), ground2=("#EAB888", "#F2C89A"),
        cliff=("#F0A070", "#D8704A", "#B8503E", "#6A3E70"), rock=("#F5B486", "#D97850", "#8A4A62"),
        wall=("#E8A878", "#C87850", "#8A4A50"), deep="#40304E",
        tree=("juniper",), leaf=("#4E5E30", "#66763A", "#8A9A4E"), trunk="#6A4430",
        accent=("#FFE27A", "#FFFFFF", "#FFC24A"),
        sky=("#A8D8F5", "#58AEEA"), sun=dict(az=150, el=54, energy=4.6, color=(1.0, 0.92, 0.78)),
    ),
    "ruins": dict(
        title="古垣",
        ground=("#7AA65C", "#8FB667", "#ADCB80"), ground2=("#D8CCA8", "#E8DEC0"),
        plateau=("#B3AC7C", "#C4BC8C", "#D8D0A6"),
        cliff=("#E2D6B8", "#D2C4A2", "#A89A82", "#7A6E8A"), cliff_style="masonry", rock=("#E2D8BE", "#BFB090", "#8E8494"),
        wall=("#E8DEC2", "#CDBF9C", "#A0927E"),
        tree=("round", "cypress"), leaf=("#3E8A56", "#5EAA64", "#98CC7C"),
        sky=("#C6E4EC", "#86BFD6"),
    ),
    "glacier": dict(
        title="冰原峡谷",
        ground=("#CCDDEE", "#DCE8F3", "#EEF4FA"), ground2=("#A8D6F0", "#C8E8F8"),
        cliff=("#BFDDF0", "#8FC0E4", "#5F95CC", "#3E5CAE"), cliff_style="columns", rock=("#F2FAFF", "#B4D8EE", "#5F90DA"),
        wall=("#D6E6F2", "#A6C4DC", "#7894B4"), deep="#2E3E86",
        ice=("#A8D4EE", "#94C6E6", "#DDF1FC"),
        tree=("snowpine",), leaf=("#2E6F7A", "#4E9CA2", "#F2FAFF"), trunk="#6A5A60",
        accent=("#FFFFFF", "#DFF3FF", "#BFE6FF"),
        sky=("#CFE6F7", "#6FA9DC"), ambient="#B4CCF2",
        sun=dict(az=150, el=44, energy=3.3, color=(1.0, 0.95, 0.88)), amb_strength=0.75,
    ),
    "heath": dict(
        title="石阵荒原",
        ground=("#7D9A5C", "#91AB68", "#ADBF7E"), ground2=("#C4AA80", "#D6BE96"),
        cliff=("#C9C6BC", "#A8A6A0", "#86889A", "#5A5E84"), rock=("#D8DEE6", "#A8B2C2", "#6A7492"),
        wall=("#BCC2CC", "#9AA2B0", "#707A90"), deep="#3A3E66",
        tree=("pine", "bare"), leaf=("#4E8A5A", "#6EA868", "#A8CC84"),
        accent=("#D9A6E8", "#B987D6", "#FFFFFF"),
        sky=("#D9E4EE", "#9DB7D4"), ambient="#B0BEDC",
        sun=dict(az=150, el=48, energy=4.0, color=(1.0, 0.95, 0.88)),
    ),
    "blossom": dict(
        title="春樱",
        ground=("#82BA60", "#97C86C", "#B8DA84"), ground2=("#E8CE96", "#F2DCAE"),
        cliff=("#F2DCC8", "#E2BEA6", "#C49A92", "#9A7EA6"), rock=("#F8E6DC", "#E2C2B4", "#B49AA6"),
        wall=("#EAD8C8", "#CDB4A4", "#A08A90"),
        tree=("sakura", "round"), leaf=("#F08FA8", "#F9B4C6", "#FFE0EA"),
        accent=("#FFFFFF", "#FFD6E4", "#FFA9C6"),
        sky=("#C4E6FA", "#7FC4F2"),
    ),
    "oldtown": dict(
        title="老城",
        ground=("#CDBEA4", "#DACCB2", "#E6DAC4"), ground2=("#C2B296", "#B4A486"),
        cliff=("#D8CCB4", "#BCAE96", "#968A7E", "#6A6280"), rock=("#E6DCC8", "#C4B8A4", "#8E8494"),
        wall=("#F0E6D2", "#D8C8AC", "#A89880"), deep="#2E4A66",
        water=("#4A94A8", "#2E6E8A", "#A8D8E0"), pave=("#D6C8AC", "#CBBC9E", "#C2B294"), mortar="#ADA084",
        tree=("round", "cypress"), leaf=("#45A862", "#66C46C", "#A4E07E"),
        roof=("#D8603E", "#B84A34", "#4E6A8A"),
        sky=("#B8DCF4", "#6DB0E6"),
    ),
    "dunes": dict(
        title="沙洲",
        ground=("#DEBE7C", "#E8CC8E", "#F2DCA6"), ground2=("#D6B47A", "#C8A06A"),
        cliff=("#F2D896", "#DCB474", "#B88A5C", "#7E5A84"), rock=("#5E5A66", "#46424E", "#2E2C36"),
        wall=("#E8D0A0", "#C8A878", "#9A7A5E"), deep="#1E5A7A",
        water=("#34A2C4", "#1B78A6", "#C9EEF2"), shallow="#86E0DC", wet="#B58E58",
        tree=("palm",), leaf=("#3E9A58", "#6CC060", "#B4E47A"), trunk="#A07850",
        accent=("#FFFFFF", "#F0E0B0", "#E8C890"),
        sky=("#A4DAF8", "#4FA8EE"), sun=dict(az=150, el=58, energy=4.8, color=(1.0, 0.93, 0.78)),
    ),
}

MAP_LOOK = {"草甸迎击": "meadow", "断龙桥": "canyon", "拔炮台": "ruins", "一线天": "glacier", "镇山兽": "heath",
            "樱丘": "blossom", "河心洲": "oldtown", "涨潮沙洲": "dunes", "平地": "meadow"}
THEME_ZH = {"山间草甸": "meadow", "红岩峡谷": "canyon", "古垣": "ruins", "冰原": "glacier", "冰原峡谷": "glacier",
            "石阵荒原": "heath", "春樱": "blossom", "老城": "oldtown", "沙洲": "dunes"}


def get(name):
    t = dict(BASE)
    t.update(LOOKS.get(name, {}))
    t["name"] = name
    t.setdefault("ice", t["ground"])
    t.setdefault("dust", {"glacier": "#F4FAFF", "canyon": "#F4CFA4", "dunes": "#F4E2B4", "oldtown": "#E6DCC6"}.get(name, "#E4D4B0"))
    t.setdefault("roof", ("#D8603E", "#B84A34", "#4E6A8A"))
    return t


def for_map(mp):
    """地图 dict -> 主题。优先地图名，其次地图里的 theme 字段（中文）。"""
    nm = MAP_LOOK.get(mp.get("key", "")) or MAP_LOOK.get(mp.get("name", ""))
    if not nm:
        nm = THEME_ZH.get(mp.get("theme", ""), "meadow")
    return get(nm)
