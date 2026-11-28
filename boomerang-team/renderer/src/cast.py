# -*- coding: utf-8 -*-
"""选手名单（纯 Python）：正赛 12 位的内部名、显示名、主色、立绘文件；新旧两套角色造型之间的对应；默认上场名单。
角色造型用环境变量 DART_CHARS 切换：voxel（默认，像素 / 体素小人，12 位）或 smooth（旧的圆润小人，8 位）。"""
import os

import voxchars

ORDER12 = list(voxchars.KEYS)
EXTRA = list(voxchars.EXTRA)          # 名单外、可点名上场的角色（小 Gemini）
SMOOTH8 = ["claude", "gpt", "gemini", "doubao", "kimi", "deepseek", "grok", "hunyuan"]
TO_VOXEL = {"claude": "opus", "gpt": "astra"}       # 旧名字在像素造型里对应谁
TO_SMOOTH = {"opus": "claude", "astra": "gpt"}      # 新名字在旧造型里对应谁
ART12 = {"astra": "GPT_大_Astra.png", "sol": "GPT_小_Sol.png", "fable": "Fable.png", "opus": "Claude_大_Opus.png",
         "gemini": "Gemini.png", "grok": "Grok.png", "kimi": "Kimi.png", "deepseek": "DeepSeek.png", "doubao": "豆包.png",
         "qwen": "Qwen_千问.png", "mimo": "小米MiMo.png", "glm": "GLM.png", "gemini_s": "Gemini_小.png"}
# 头像裁切微调：(横向偏移, 纵向偏移, 缩放)，单位是自动估出来的头的大小
ART_TWEAK12 = {"astra": (0.05, 0.08, 0.8), "sol": (0.0, 0.17, 0.95), "fable": (0.0, 0.1, 0.84), "opus": (0.03, 0.04, 0.84),
               "gemini": (0.0, 0.08, 0.82), "grok": (0.0, 0.07, 0.82), "kimi": (0.0, 0.07, 0.82),
               "deepseek": (0.3, 0.16, 0.82), "doubao": (0.0, 0.0, 0.9), "qwen": (0.22, 0.08, 0.84),
               "mimo": (0.2, 0.03, 0.88), "glm": (0.0, 0.12, 0.84),
               "gemini_s": (0.0, 0.08, 0.82)}


def style():
    s = os.environ.get("DART_CHARS", "voxel").strip().lower()
    return "smooth" if s in ("smooth", "old", "旧") else "voxel"


def vox_key(key):
    """这个名字在像素造型里用哪个定义；没有就返回 None（混元只有旧造型）。"""
    k = TO_VOXEL.get(key, key)
    return k if k in voxchars.ALL else None


def smooth_key(key):
    k = TO_SMOOTH.get(key, key)
    return k if k in SMOOTH8 else None


def use_voxel(key):
    """这个角色实际用哪套造型：按 DART_CHARS，另一套里没有这个人时退回有的那套。"""
    if style() == "voxel":
        return vox_key(key) is not None
    return smooth_key(key) is None


def default_roster(n):
    if style() == "voxel":
        d = {2: ["opus", "astra"], 4: ["opus", "astra", "deepseek", "grok"],
             8: ["opus", "astra", "gemini", "doubao", "kimi", "deepseek", "grok", "qwen"]}
        if n == 10:
            return default_roster_10()
        return d.get(n, (ORDER12 * 2)[:n])
    d = {2: ["claude", "gpt"], 4: ["claude", "gpt", "deepseek", "grok"], 8: SMOOTH8}
    return d.get(n, (SMOOTH8 * 2)[:n])


# ---------------------------------------------------------------- 第 12 集 5v5
# 两队（第 11 集拍卖结果）：Astra 队 = Astra、Sol、Hydrogen、Grok、Kimi；Fable 队 = Fable、Opus、Carbon、GLM、DeepSeek。
# 造型：Carbon 用 gemini、Hydrogen 用 gemini_s（显示名由 hud 覆盖成 Carbon / Hydrogen）。
TEAM_ASTRA = ["astra", "sol", "gemini_s", "grok", "kimi"]
TEAM_FABLE = ["fable", "opus", "gemini", "glm", "deepseek"]
LABEL12 = {"gemini": "Carbon", "gemini_s": "Hydrogen"}
# 回放里 PLAYER 的名字（不分大小写）→ 造型；对不上的名字不自动对应
NAME_KEY = {"astra": "astra", "sol": "sol", "hydrogen": "gemini_s", "grok": "grok", "kimi": "kimi",
            "fable": "fable", "opus": "opus", "carbon": "gemini", "glm": "glm", "deepseek": "deepseek",
            "qwen": "qwen", "千问": "qwen", "mimo": "mimo", "doubao": "doubao", "豆包": "doubao"}


def roster_from_names(players):
    """回放的 PLAYER 名字全都认得 → 按名字给出造型名单；否则 None。"""
    keys = []
    for (_, _, nm) in players:
        k = NAME_KEY.get(nm.strip().lower()) or NAME_KEY.get(nm.strip())
        if k is None:
            return None
        keys.append(k)
    return keys


def default_roster_10():
    """没有名单时的 10 人：左队 Astra 队、右队 Fable 队。"""
    return TEAM_ASTRA + TEAM_FABLE


# 队伍色跟着队伍身份走：Astra 队永远蓝、Fable 队永远红，不管这一局谁在左边。
# 回放里 team 0 永远是左边出生的那队；换边局（奇数局号）是 Fable 队当 team 0，所以不能按 team 号给颜色。
BLUE = "#2E8CFF"
RED = "#FF5B4D"
LEGACY = (RED, BLUE)        # 认不出队伍时的兜底：第 11 集 4v4 的老规矩（team 0 红、team 1 蓝）
COLOR_NAME = {RED: "红队", BLUE: "蓝队"}


def _team_keys(roster, players, team):
    """这一队的人：造型名 + 回放 PLAYER 名字（小写、按 NAME_KEY 归一），两样都算。"""
    ks = set()
    for (s, t, nm) in players:
        if t != team:
            continue
        if roster and s < len(roster):
            ks.add(roster[s])
        ks.add(NAME_KEY.get(nm.strip().lower(), nm.strip().lower()))
    return ks


def _team_id(roster, players, team):
    ks = _team_keys(roster, players, team)
    if "astra" in ks:
        return "astra"
    if "fable" in ks:
        return "fable"
    return None


def team_colors(roster, players):
    """(team 0 的颜色, team 1 的颜色)：有 Astra 的队蓝、有 Fable 的队红；只认出一队，另一队给另一种颜色；都认不出用 LEGACY。"""
    ids = [_team_id(roster, players, t) for t in (0, 1)]
    want = {"astra": BLUE, "fable": RED}
    c = [want.get(i) for i in ids]
    if c[0] and c[1] and c[0] != c[1]:
        return (c[0], c[1])
    if c[0] and not c[1]:
        return (c[0], RED if c[0] == BLUE else BLUE)
    if c[1] and not c[0]:
        return (RED if c[1] == BLUE else BLUE, c[1])
    return LEGACY


def team_names(roster, players):
    """两队的显示名：队里有 Astra / Fable 就叫"Astra 队 / Fable 队"，否则按这队的颜色叫红队 / 蓝队。"""
    cols = team_colors(roster, players)
    out = []
    for team in (0, 1):
        i = _team_id(roster, players, team)
        if i == "astra":
            out.append("Astra 队")
        elif i == "fable":
            out.append("Fable 队")
        else:
            out.append(COLOR_NAME.get(cols[team], "红队" if team == 0 else "蓝队"))
    return tuple(out)


def roster_for(rp, roster=None):
    """名单优先级：给了就用 → 回放 PLAYER 名字全认得就按名字 → 默认名单。"""
    if roster:
        return list(roster)
    return roster_from_names(rp.players) or default_roster(rp.n)
