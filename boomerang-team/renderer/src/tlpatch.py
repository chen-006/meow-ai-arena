# -*- coding: utf-8 -*-
"""时间表补丁：环境变量 DART_TLPATCH=补丁.json 时，timeline.Timeline 建好后再按补丁调整。
Blender 渲染和信息栏都各自建 Timeline，所以两边拿到的是同一份改过的时间表（慢放时信息栏的计时、播报跟着一起慢）。
补丁（秒都是游戏秒）：
{
  "slow": [[a, b, 倍速], ...],         游戏秒 a～b 之间按倍速重新取帧（0.35 = 慢 3 倍），进出各有 ramp 帧缓变
  "ramp": 8,                           倍速缓变的帧数
  "cam":  [{"a": 22.6, "b": 23.6, "ramp": 0.5,          镜头接管区间（游戏秒）与进出缓变（游戏秒）
            "x": 15, "y": 9, "dist": 12, "pitch": 40, "yaw": 0, "lens": 50,
            "track": [座位或座位列表, 权重, dx, dy]}],  看向点往某人（几人取中点）身上拉（权重 0..1，dx/dy 米偏移）
  "shake": 1.0                         接管区间里震动的保留比例
}
"""
import json
import math


def _sm(t):
    t = min(1.0, max(0.0, t))
    return t * t * (3 - 2 * t)


def _lerp_frame(A, B, w):
    out = {}
    for k, v in A.items():
        u = B.get(k, v)
        if isinstance(v, (int, float)) and isinstance(u, (int, float)):
            out[k] = v + (u - v) * w
        elif isinstance(v, tuple) and isinstance(u, tuple) and len(v) == len(u):
            out[k] = tuple(p + (q - p) * w for p, q in zip(v, u))
        else:
            out[k] = v
    return out


def apply(tl, path):
    P = json.load(open(path, encoding="utf-8"))
    fr = tl.frames
    n = len(fr)
    slow = [(a * 20.0, b * 20.0, s) for a, b, s in P.get("slow", [])]
    if slow:
        ramp = int(P.get("ramp", 8))
        target = []
        for f in range(n):
            g = fr[f]["gt"]
            s = 1.0
            for a, b, sp in slow:
                if a <= g <= b:
                    s = min(s, sp)
            target.append(s)
        # 倍速缓变：对 1/倍速 做滑动平均
        inv = [1.0 / s for s in target]
        sm = []
        for f in range(n):
            lo, hi = max(0, f - ramp), min(n, f + ramp + 1)
            sm.append(sum(inv[lo:hi]) / (hi - lo))
        new = []
        u = 0.0
        while u <= n - 1:
            i = int(u)
            w = u - i
            new.append(_lerp_frame(fr[i], fr[min(i + 1, n - 1)], w) if w > 1e-6 else dict(fr[i]))
            u += 1.0 / sm[min(i, n - 1)]
        fr = new
        n = len(fr)
    for c in P.get("cam", []):
        a, b, r = c["a"] * 20.0, c["b"] * 20.0, max(0.01, c.get("ramp", 0.5) * 20.0)
        for f in range(n):
            g = fr[f]["gt"]
            w = _sm((g - (a - r)) / r) * (1 - _sm((g - b) / r))
            if w <= 0:
                continue
            tx, ty = c.get("x", 0.0), c.get("y", 0.0)
            fl = getattr(tl.rp, "blueleft", False)      # 蓝左视角：补丁按原回放坐标写，这里跟着转 180 度
            if fl and ("x" in c or "y" in c):
                tx, ty = tl.rp.Wm - tx, tl.rp.Hm - ty
            tr = c.get("track")
            if tr:
                ks = tr[0] if isinstance(tr[0], list) else [tr[0]]
                tw = float(tr[1])
                ps = [tl.rp.char_pos(int(k), max(0.0, min(tl.rp.T, g))) for k in ks]
                px, py = sum(p[0] for p in ps) / len(ps), sum(p[1] for p in ps) / len(ps)
                ddx, ddy = (tr[2] if len(tr) > 2 else 0.0), (tr[3] if len(tr) > 3 else 0.0)
                if fl:
                    ddx, ddy = -ddx, -ddy
                tx += (px + ddx - tx) * tw
                ty += (py + ddy - ty) * tw
            ox, oy, od = fr[f]["cam"]
            d = math.exp(math.log(od) * (1 - w) + math.log(c.get("dist", od)) * w)
            fr[f] = dict(fr[f])
            fr[f]["cam"] = (ox + (tx - ox) * w, oy + (-ty - oy) * w, d)
            fr[f]["pitch"] = fr[f].get("pitch", 58.0) * (1 - w) + c.get("pitch", fr[f].get("pitch", 58.0)) * w
            fr[f]["yaw"] = fr[f].get("yaw", 0.0) * (1 - w) + c.get("yaw", 0.0) * w
            if "lens" in c:
                fr[f]["lens"] = fr[f].get("lens", 50.0) * (1 - w) + c["lens"] * w
            k = 1 - w * (1 - c.get("shake", 1.0))
            fr[f]["shake"] = (fr[f]["shake"][0] * k, fr[f]["shake"][1] * k)
    tl.frames = fr
    tl.n = n
    tl.gts = [x["gt"] for x in fr]
