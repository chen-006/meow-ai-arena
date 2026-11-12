# -*- coding: utf-8 -*-
"""v3 小样（开场与版式）的时间表：每个输出帧 = 回放时刻（可小数）+ 机位 + 震动，交给 render_timeline.py 渲。
    python v3_plans.py cold  <L4_ernie.wrp>  <输出 plan.json>
    python v3_plans.py bridge <L2_sol.wrp>   <输出 plan.json>
    python v3_plans.py grid  <L1_x.wrp>      <输出 plan.json> [--speed 5] [--res 640x360] [--len 秒]
普通 Python（numpy），不进 Blender。
"""
import json
import math
import sys

import numpy as np

import replay as RP

FPS = 30.0


def smooth(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3 - 2 * x)


# ------------------------------------------------------------------ 变速：速度随回放时间分段线性 → 每个输出帧的回放时刻
def retime(t0, t1, keys):
    """keys：[(回放秒, 速度)]，速度在相邻两个关键点之间按回放时间线性插值（1 = 原速，0.2 = 慢五倍，6 = 快六倍）。
    返回每个输出帧对应的回放秒。"""
    ks = sorted(keys)
    xs = [k[0] for k in ks]
    vs = [k[1] for k in ks]
    out, t = [], t0
    while t < t1:
        out.append(t)
        t += float(np.interp(t, xs, vs)) / FPS
    return out


class Track:
    """几个物体的平均位置（引擎坐标），按回放帧平滑过，可以在小数帧取值。"""

    def __init__(self, rp, ids, sig_s=0.25, lift=0.0):
        P = np.mean([RP._track(rp, rp.col[i]) for i in ids], axis=0)
        P = RP._gauss(P, sig_s * FPS)
        P[:, 1] += lift
        self.P = P

    def at(self, t_sec):
        f = t_sec * FPS
        i0 = int(np.clip(math.floor(f), 0, len(self.P) - 1))
        i1 = min(i0 + 1, len(self.P) - 1)
        u = f - math.floor(f)
        return self.P[i0] * (1 - u) + self.P[i1] * u


def cam_keys(t, keys):
    """keys：[(回放秒, 目标 (x,y,z) 或 callable(t)→(x,y,z), 宽, 俯角, 方位, 过渡)]；过渡 = 从上一个关键点过来的方式：
    "cut" 硬切（到这个时刻之前保持上一个），"ease" 从上一个关键点开始平滑过渡到这个。返回 (x, z, y, 宽, 俯角, 方位)。"""
    def ev(k, tt):
        tg = k[1](tt) if callable(k[1]) else k[1]
        return np.array([tg[0], tg[2], tg[1], k[2], k[3], k[4]], float)
    if t <= keys[0][0]:
        return ev(keys[0], t)
    for a, b in zip(keys, keys[1:]):
        if a[0] <= t < b[0]:
            if b[5] == "cut":
                return ev(a, t)
            u = smooth((t - a[0]) / max(b[0] - a[0], 1e-6))
            va, vb = ev(a, t), ev(b, t)
            v = va * (1 - u) + vb * u
            v[3] = math.exp(math.log(va[3]) * (1 - u) + math.log(vb[3]) * u)
            return v
    return ev(keys[-1], t)


def shake_at(t_out, hits):
    """hits：[(输出秒, 强度, 画面宽)]；和 render_replay 一样的衰减抖动，但按输出时间算（慢放里不被拉长）。"""
    ox = oy = 0.0
    for (f, amp, wd) in hits:
        age = t_out - f
        if 0 <= age < 0.6:
            d = amp * math.exp(-age / 0.15) * wd / 30.0
            ox += d * math.sin(age * 61.0 + f)
            oy += d * math.sin(age * 73.0 + f * 1.7)
    return ox, oy


# ------------------------------------------------------------------ 1. 冷开场：L4_ernie
def cold(rp_path):
    """文心的北边两个推球兵（25、26 号）：0.98 秒推第一颗球（北近），球一动就把两人撞倒（45→9 血）；爬起来走去推第二颗（北远），
    9.30 秒松手、9.32 秒球把两人一起撞死；球接着往下滚，11.3 秒撞进路障甲的守军。"""
    rp = RP.load(rp_path)
    pushers = Track(rp, [25, 26], 0.3, 1.0)
    ball2 = Track(rp, [2], 0.35, -0.3)
    # 速度：0～2.2 原速（第一次被撞倒）→ 快进 6 倍（爬起来走过去）→ 7.7 起原速（走到第二颗球、推）→ 9.27 起 0.2 倍慢放（撞上）
    # → 9.8 起回到原速，跟着球滚下去
    speed = [(0.0, 1.0), (2.15, 1.0), (2.6, 6.0), (7.1, 6.0), (7.75, 1.0), (9.22, 1.0), (9.27, 0.2), (9.72, 0.2),
             (10.05, 1.0), (12.0, 1.0)]
    ts = retime(0.15, 12.15, speed)
    YAW = -160.0
    hit2 = (12.75, 13.9, 7.05)
    # 最后一个镜头：硬切到球的身后（东北偏东）顺着它滚的方向看，跟一段后镜头停住，球滚远、撞进下面的守军
    ahead = np.array([-2.6, -0.6, -1.0])
    stop = ball2.at(10.95) + ahead
    keys = [
        (0.15, (29.9, 16.1, 7.25), 11.5, 28.0, YAW, "cut"),
        (2.1, (29.6, 16.0, 7.1), 12.5, 29.0, YAW, "ease"),
        (2.6, lambda t: pushers.at(t), 14.0, 30.0, YAW, "ease"),
        (7.3, lambda t: pushers.at(t), 14.0, 30.0, YAW, "ease"),
        (8.3, lambda t: (pushers.at(t) * 0.6 + np.array([12.0, 14.6, 6.5]) * 0.4), 11.5, 28.0, YAW, "ease"),
        (9.22, hit2, 9.6, 27.0, YAW, "ease"),
        (9.75, hit2, 8.4, 26.0, YAW, "ease"),
        (9.84, lambda t: ball2.at(t) + ahead, 12.0, 30.0, -110.0, "cut"),
        (10.6, lambda t: ball2.at(t) + ahead, 13.0, 31.0, -110.0, "ease"),
        (11.25, stop, 15.0, 32.0, -110.0, "ease"),
        (12.15, stop, 16.0, 32.0, -110.0, "ease"),
    ]
    # 撞击时刻（输出秒）
    def out_of(tr):
        return float(np.searchsorted(ts, tr)) / FPS
    hits = [(out_of(1.0), 0.18, 12.0), (out_of(9.32), 0.32, 9.0)]
    F = []
    for j, t in enumerate(ts):
        c = cam_keys(t, keys)
        sx, sy = shake_at(j / FPS, hits)
        F.append([t * FPS] + [float(v) for v in c] + [sx, sy])
    return dict(replay=rp_path, res=[1920, 1080], frames=F,
                notes=dict(hit_out_sec=[h[0] for h in hits], seconds=len(F) / FPS))


# ------------------------------------------------------------------ 2. 两名弓手守桥：L2_sol
def bridge(rp_path, hold_s=2.2):
    """三段硬切：0～3 秒交代两名弓手站在桥头两侧的石壁上；7～21 秒敌军挤上桥、炮仗兵自爆、成片掉下深谷；
    139～144.45 秒最后一名盾卫在桥头被射死，军旗满血 → 结果牌。信息栏全程开着（军旗血条看得见）。"""
    rp = RP.load(rp_path)
    last = rp.nx - 1
    segs = [
        # (回放起, 回放止, 起机位, 止机位)：机位 = (目标 x, y, z), 宽, 俯角, 方位
        (0.0, 3.0, ((4.0, 1.0, 2.0), 34.0, 34.0, -35.0), ((5.5, 1.2, 1.5), 30.0, 33.0, -38.0)),
        (7.0, 21.0, ((6.0, 0.2, -0.5), 25.0, 38.0, 50.0), ((5.0, -0.5, -0.8), 28.0, 39.0, 44.0)),
        (139.0, last / FPS + 1e-6, ((15.0, 1.5, 0.0), 24.0, 30.0, 85.0), ((14.0, 1.3, -0.3), 20.5, 29.0, 88.0)),
    ]
    F, H = [], []
    for (a, b, c0, c1) in segs:
        fa, fb = int(round(a * FPS)), min(last, int(round(b * FPS)))
        for f in range(fa, fb + 1):
            u = smooth((f - fa) / max(fb - fa, 1)) * 0.6 + (f - fa) / max(fb - fa, 1) * 0.4
            tg = np.array(c0[0]) * (1 - u) + np.array(c1[0]) * u
            wd = math.exp(math.log(c0[1]) * (1 - u) + math.log(c1[1]) * u)
            pt = c0[2] * (1 - u) + c1[2] * u
            yw = c0[3] * (1 - u) + c1[3] * u
            F.append([float(f), tg[0], tg[2], tg[1], wd, pt, yw, 0.0, 0.0])
            H.append(f)
    # 结尾定格：最后一帧不动，信息栏放结果牌
    for j in range(int(hold_s * FPS)):
        F.append(list(F[-1]))
        H.append(rp.nx + j)
    return dict(replay=rp_path, res=[1920, 1080], frames=F, hud=H, names=["GPT-6.2 Sol", "守军"])


def main():
    mode, rp_path, out = sys.argv[1], sys.argv[2], sys.argv[3]
    plan = dict(cold=cold, bridge=bridge)[mode](rp_path)
    json.dump(plan, open(out, "w", encoding="utf-8"))
    print(mode, "frames", len(plan["frames"]), "seconds %.2f" % (len(plan["frames"]) / FPS), plan.get("notes", ""))


if __name__ == "__main__":
    main()
