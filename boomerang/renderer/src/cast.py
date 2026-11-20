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
# 头像裁切微调（11-17 按正式形象v2 重调 deepseek、qwen、mimo）：(横向偏移, 纵向偏移, 缩放)，单位是自动估出来的头的大小
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
        return d.get(n, (ORDER12 * 2)[:n])
    d = {2: ["claude", "gpt"], 4: ["claude", "gpt", "deepseek", "grok"], 8: SMOOTH8}
    return d.get(n, (SMOOTH8 * 2)[:n])
