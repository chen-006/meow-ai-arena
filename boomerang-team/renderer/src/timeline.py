# -*- coding: utf-8 -*-
"""回放 -> 成片的逐帧时间表（纯 Python）：每一帧对应的游戏时间 gt、镜头、震动。
  * 30 帧/秒，正常速度每帧走 20/30 步；
  * 顿帧：每次击杀（镖 / 砍）停 3 帧，镖撞镖、踢中停 2 帧，终结一击停 5 帧；
  * 慢放：只在每小局的最后一击前后各一小段（0.3 倍速）；
  * 镜头：始终装得下还没塌的全部场地（上方给信息栏留出位置），塌一波就收紧一点；击杀时轻轻一顿，
    最后一击向出局者推近，结束后推向胜者。全程只有平移和推拉，不转不摇。
"""
import math

from pose import Animator, INTRO, OUTRO, ease, clamp

FPS = 30
STEP_PER_FRAME = 20.0 / FPS
PITCH = 58.0
LENS = 50.0
SENSOR = 36.0
ASPECT = 16.0 / 9.0
SAFE = (-0.94, 0.94, -0.90, 0.70)       # NDC：左 右 下 上（上面留给信息栏）
DIST_MIN = 21.0
ACT_DMIN = 17.0
SAFE12 = (-0.94, 0.94, -0.92, 0.62)     # 第 12 集 10 人：信息栏高一些（还挂着占点的小圆片），上沿多留一点         # 第 12 集 5v5 自动运镜：最多推到多近（全场取景之外，按活着的人收紧）


def cam_basis(pitch=PITCH):
    p = math.radians(pitch)
    back = (0.0, -math.cos(p), math.sin(p))
    fwd = (-back[0], -back[1], -back[2])
    right = (1.0, 0.0, 0.0)
    up = (0.0, math.sin(p), math.cos(p))
    return back, fwd, right, up


CLOSE_YAW = 48.0        # 终结一击时最多绕着目标转多少度
CLOSE_PITCH = 34.0      # 终结一击慢放时的俯角（全景是 58 度）
WIN_PITCH = 27.0        # 结算镜头的俯角：压低到看得见脸


def project(cam, P, pitch=PITCH):
    """世界点（Blender 坐标）-> NDC (-1..1, y 向上)。cam = (tx, ty, dist)。"""
    back, fwd, right, up = cam_basis(pitch)
    cx, cy, cz = cam[0] + back[0] * cam[2], cam[1] + back[1] * cam[2], back[2] * cam[2]  # noqa
    v = (P[0] - cx, P[1] - cy, P[2] - cz)
    zc = v[0] * fwd[0] + v[1] * fwd[1] + v[2] * fwd[2]
    xc = v[0]
    yc = v[1] * up[1] + v[2] * up[2]
    th = (SENSOR / 2) / LENS
    return xc / zc / th, yc / zc / (th / ASPECT)


def to_px(ndc, w=1920, h=1080):
    return (ndc[0] * 0.5 + 0.5) * w, (0.5 - ndc[1] * 0.5) * h


def _proj_np(cam, P, pitch=PITCH):
    """project() 的 numpy 版：P (N, 3) -> (nx, ny)。和 project 逐点结果一致。"""
    import numpy as np
    back, fwd, right, up = cam_basis(pitch)
    cx, cy, cz = cam[0] + back[0] * cam[2], cam[1] + back[1] * cam[2], back[2] * cam[2]
    vx, vy, vz = P[:, 0] - cx, P[:, 1] - cy, P[:, 2] - cz
    zc = vx * fwd[0] + vy * fwd[1] + vz * fwd[2]
    yc = vy * up[1] + vz * up[2]
    th = (SENSOR / 2) / LENS
    return vx / zc / th, yc / zc / (th / ASPECT)


class _PL(list):
    pass


def fit(points, safe=SAFE, dmin=DIST_MIN):
    """找最近的机位，让所有点都落在安全框里。points：[(X, Y, Z)]（Blender 坐标）。返回 (tx, ty, dist)。
    （第 12 集：投影改成 numpy 一次算完，结果和原来一样，只是快几十倍。）"""
    import numpy as np
    P = np.asarray(points, dtype=float).reshape(-1, 3)

    def project_all(c):
        nx, ny = _proj_np(c, P)
        return nx, ny
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    tx = (min(xs) + max(xs)) / 2
    ty = (min(ys) + max(ys)) / 2
    lo, hi = 6.0, 140.0
    best = (tx, ty, hi)
    for _ in range(26):
        d = (lo + hi) / 2
        cx, cy = tx, ty
        ok = False
        for _ in range(4):
            nx, ny = project_all((cx, cy, d))
            x0, x1 = float(nx.min()), float(nx.max())
            y0, y1 = float(ny.min()), float(ny.max())
            # 把投影挪到安全框中间
            ex = ((x0 + x1) / 2 - (safe[0] + safe[1]) / 2)
            ey = ((y0 + y1) / 2 - (safe[2] + safe[3]) / 2)
            th = (SENSOR / 2) / LENS
            cx += ex * th * d
            cy += ey * (th / ASPECT) * d / math.sin(math.radians(PITCH))
            ok = (x1 - x0) <= (safe[1] - safe[0]) and (y1 - y0) <= (safe[3] - safe[2])
        if ok:
            best = (cx, cy, d)
            hi = d
        else:
            lo = d
    if best[2] < dmin:
        # 不再推近，但仍然把场地放在安全框中间
        cx, cy, d = best[0], best[1], dmin
        for _ in range(4):
            nx, ny = project_all((cx, cy, d))
            ex = ((float(nx.min()) + float(nx.max())) / 2 - (safe[0] + safe[1]) / 2)
            ey = ((float(ny.min()) + float(ny.max())) / 2 - (safe[2] + safe[3]) / 2)
            th = (SENSOR / 2) / LENS
            cx += ex * th * d
            cy += ey * (th / ASPECT) * d / math.sin(math.radians(PITCH))
        best = (cx, cy, d)
    return best


def land_points(rp, collapsed):
    m = rp.solid_mask(collapsed)
    cs = rp.cell
    pts = []
    for y in range(rp.H):
        for x in range(rp.W):
            if m[y][x]:
                for (ax, ay) in ((0, 0), (1, 0), (0, 1), (1, 1)):
                    pts.append(((x + ax) * cs, -(y + ay) * cs, 0.0))
                pts.append(((x + 0.5) * cs, -y * cs, 1.35))       # 站在上沿的人的头顶
    if not pts:
        pts = [(rp.Wm / 2, -rp.Hm / 2, 0.0)]
    return pts


class Timeline:
    def _action_fits(self, gts, fits):
        """第 12 集 5v5（10 人）的自动运镜：先按全场取景（同第 11 集），再看活着的人（和在飞的镖）挤在多大的地方——
        装得下所有活人（外加 2.2 米边）就往里收，但不比全场更远、不近于 ACT_DMIN。占点图把三个点也算进去，
        三条线都有事时自然就是全景。按 0.5 步取样后插值，避免每帧算一遍。"""
        rp, A = self.rp, self.anim
        T = rp.T
        pad = 2.2
        cache = {}

        def at(g):
            gi = round(min(max(g, 0.0), T) * 2) / 2.0
            if gi in cache:
                return cache[gi]
            c = sum(1 for s in rp.wave_steps if gi >= s + 14)
            base = fits[c]
            pts = []
            t = int(gi)
            for k in range(rp.n):
                ch = rp.states[t]["chars"][k]
                if not ch["alive"]:
                    continue
                x, y = ch["x"] / 1000.0, ch["y"] / 1000.0
                for (ox, oy) in ((-pad, -pad), (pad, -pad), (-pad, pad), (pad, pad)):
                    pts.append((x + ox, -(y + oy), 0.0))
                pts.append((x, -y + pad, 1.4))
                d = rp.states[t]["darts"][k]
                if d["st"] in (1, 2, 6):
                    pts.append((d["x"] / 1000.0, -d["y"] / 1000.0, 0.7))
            for (nm, px, py, r) in getattr(rp, "points", []):
                for (ox, oy) in ((-r, -r), (r, r)):
                    pts.append((px + ox, -(py + oy), 0.0))
            if len(pts) < 2:
                cache[gi] = base
                return base
            q = fit(pts, safe=SAFE12, dmin=ACT_DMIN)
            if q[2] >= base[2]:
                res = base
            else:
                # 收紧时镜头中心也往人群挪，但离全场中心不远于场地的一半宽 / 高（不让场地大面积出画）
                res = (q[0], q[1], q[2])
            cache[gi] = res
            return res

        out = []
        for g in gts:
            if g < 0:
                out.append(at(0.0))
            elif g > T:
                out.append(at(T))
            else:
                out.append(at(g))
        return out

    def __init__(self, rp, roster=None, intro=True, outro=True):
        self.rp = rp
        self.anim = Animator(rp, roster)
        A = self.anim
        T = rp.T
        # ---- 关键时刻
        AD = A.all_deaths()                     # 第 12 集：占点有复活，同一个人可以出局好几次
        kills = sorted((B.death_info(k)[0], k) for B, k in AD if B.deaths[k][1] in (1, 2))
        falls = sorted((B.death_info(k)[0], k) for B, k in AD if B.deaths[k][1] == 3)
        self.final = None
        self.final_anim = A
        if rp.reason == "ko" and AD:
            kings = [k for k in getattr(rp, "kings", (-1, -1)) if k >= 0]
            cand = [(B.death_info(k)[0], k in kings, k, B) for B, k in AD]
            td, _, k, B = max(cand, key=lambda c: (c[0], c[1], c[2]))      # 同一步出局时终结镜头给国王
            if td >= T - 1:
                self.final = (td, k)
                self.final_anim = B
        stops = {}
        short = rp.mode == 4                    # 占点一局几十次击杀：顿帧短一点
        for td, k in kills:
            stops[td + 0.6] = 5 if (self.final and self.final[1] == k and abs(self.final[0] - td) < 0.01) else (2 if short else 3)
        for (t, kk, left, by, why) in getattr(rp, "lifeloss", []):       # 国王掉命：顿一下
            stops[t + 1.0 + 0.6] = 5
        for (t, x, y, a, b) in rp.clashes:
            stops.setdefault(t + 1.0 + 0.5, 2)
        for (t, a, b) in rp.kickhits:
            stops.setdefault(t + 1.0 + 0.5, 2)
        stop_ts = sorted(stops)
        # ---- 逐帧的游戏时间
        gts = []
        gt = -float(INTRO) if intro else 0.0
        end = T + (OUTRO if outro else 0)
        si = 0
        while gt < end:
            gts.append(gt)
            sp = 1.0
            if self.final:
                td = self.final[0]
                if td - 6.0 < gt < td + 8.0:
                    sp = 0.3 + 0.7 * max(ease((td - 2.0 - gt) / 4.0), ease((gt - td - 3.5) / 4.5))
            nxt = gt + STEP_PER_FRAME * sp
            while si < len(stop_ts) and stop_ts[si] <= gt:
                si += 1
            if si < len(stop_ts) and stop_ts[si] <= nxt:
                gts.append(stop_ts[si])
                gts.extend([stop_ts[si]] * stops[stop_ts[si]])
                nxt = stop_ts[si] + 0.02
                si += 1
            gt = nxt
        self.gts = gts
        n = len(gts)
        # ---- 镜头：每个塌陷状态的全场取景 -> 逐帧目标 -> 时间上做高斯平滑
        R = len(rp.wave_steps)
        safe = SAFE12 if rp.n > 8 else SAFE
        fits = [fit(land_points(rp, c), safe=safe) for c in range(R + 1)]
        act = self._action_fits(gts, fits) if rp.n > 8 else None
        raw = []
        for f, g in enumerate(gts):
            c = sum(1 for s in rp.wave_steps if g >= s + 14)      # 塌完一小会儿再收紧
            tx, ty, d = fits[c] if act is None else act[f]
            pitch = PITCH
            yaw = 0.0
            if g < 0:                                             # 入场：从稍远处推进来
                d *= 1.0 + 0.16 * (1 - ease((g + INTRO) / (INTRO - 12.0)))
            if self.final:
                td, k = self.final
                w = ease((g - (td - 5.0)) / 4.0) * (1 - ease((g - (td + 6.0)) / 6.0))
                if w > 0:
                    # 最后一击：机位压低、拉近，对着出手的人和中招的人之间（脸和动作露出来）
                    vx, vy = rp.char_pos(k, td)
                    vx0, vy0 = vx, vy
                    kl = self.final_anim.deaths[k][2]
                    sep = 0.0
                    if kl is not None and kl >= 0 and kl != k:
                        kx, ky = rp.char_pos(kl, td)
                        sep = math.hypot(kx - vx, ky - vy)
                        if sep < 7.0:
                            vx, vy = vx * 0.6 + kx * 0.4, vy * 0.6 + ky * 0.4
                        else:
                            sep = 0.0
                    tx += (vx - tx) * 0.9 * w
                    ty += (-vy + 0.35 - ty) * 0.9 * w
                    d += (min(d, 10.5 + sep * 1.5) - d) * w
                    pitch += (CLOSE_PITCH - pitch) * w
                    if sep > 0.3:
                        # 绕到两人连线的侧面去拍（最多转 CLOSE_YAW 度），免得只拍到出手那个人的后脑勺
                        ph = math.degrees(math.atan2(-(vy0 - ky), vx0 - kx))
                        ph = (ph + 90.0) % 180.0 - 90.0
                        yaw = clamp(ph, -CLOSE_YAW, CLOSE_YAW) * w
            if g > T + 6 and A.winners:
                w = ease((g - (T + 6)) / 16.0)
                ws = [rp.char_pos(k, T) for k in A.winners]
                wx = sum(p[0] for p in ws) / len(ws)
                wy = sum(p[1] for p in ws) / len(ws)
                spread = max([math.hypot(p[0] - wx, p[1] - wy) for p in ws] + [0.0])
                dw = max(8.6, min(d, 8.6 + spread * 2.4))
                tx += (wx - tx) * w
                ty += (-wy + 2.0 - ty) * w        # 胜者放在画面偏下，头顶的饰物不被横幅挡住
                d += (dw - d) * w
                pitch += (WIN_PITCH - pitch) * w
            raw.append((tx, ty, d, pitch, yaw))
        sig = 13.0 if act is None else 20.0
        rad = int(sig * 3)
        ker = [math.exp(-0.5 * (j / sig) ** 2) for j in range(-rad, rad + 1)]
        ks = sum(ker)
        cams = []
        for f in range(n):
            acc = [0.0, 0.0, 0.0, 0.0, 0.0]
            for j in range(-rad, rad + 1):
                q = raw[min(max(f + j, 0), n - 1)]
                w = ker[j + rad] / ks
                acc[0] += q[0] * w
                acc[1] += q[1] * w
                acc[2] += q[2] * w
                acc[3] += q[3] * w
                acc[4] += q[4] * w
            cams.append(acc)
        # ---- 震动和击杀时的一顿（按帧算，不受顿帧影响）
        imp = []     # (帧号, 强度)
        first = {}
        for f, g in enumerate(gts):
            first.setdefault(round(g, 4), f)

        def frame_of(t):
            lo = 0
            for f, g in enumerate(gts):
                if g >= t:
                    return f
            return n - 1

        for td, k in kills:
            imp.append((frame_of(td + 0.6), 0.16 if (self.final and self.final[1] == k) else 0.1, 0.022))
        for (t, kk, left, by, why) in getattr(rp, "lifeloss", []):
            imp.append((frame_of(t + 1.6), 0.14, 0.03))
        for (t, x, y, a, b) in rp.clashes:
            imp.append((frame_of(t + 1.5), 0.05, 0.0))
        for (t, a, b) in rp.kickhits:
            imp.append((frame_of(t + 1.5), 0.06, 0.008))
        for s in rp.wave_steps:
            if s <= T:
                imp.append((frame_of(s), 0.07, 0.0))
        self.frames = []
        for f in range(n):
            sx = sy = 0.0
            zoom = 1.0
            for (f0, amp, punch) in imp:
                a = f - f0
                if 0 <= a < 14:
                    e = amp * math.exp(-a / 3.2)
                    sx += e * math.sin(a * 2.9 + f0)
                    sy += e * math.cos(a * 3.7 + f0 * 1.3)
                    zoom -= punch * math.exp(-a / 4.5)
            for s in rp.wave_steps:                               # 塌陷前的隆隆声：越来越抖
                u = gts[f] - s
                if -14 < u < 0 and s <= T:
                    e = 0.012 * (1 + u / 14.0)
                    sx += e * math.sin(f * 2.3)
                    sy += e * math.cos(f * 3.1)
            c = cams[f]
            self.frames.append(dict(gt=gts[f], cam=(c[0], c[1], c[2] * zoom), shake=(sx, sy), pitch=c[3], yaw=c[4]))
        self.n = n
        import os as _os
        if _os.environ.get("DART_TLPATCH"):            # 慢放 / 近景时间表补丁（见 tlpatch.py）
            import tlpatch
            tlpatch.apply(self, _os.environ["DART_TLPATCH"])

    def cam_location(self, f):
        fr = self.frames[f]
        back, fwd, right, up = cam_basis(fr.get("pitch", PITCH))
        tx, ty, d = fr["cam"]
        sx, sy = fr["shake"]
        yw = math.radians(fr.get("yaw", 0.0))
        c, s_ = math.cos(yw), math.sin(yw)
        ox, oy = sx, back[1] * d + up[1] * sy             # 先按不转的机位算偏移，再绕目标点转 yaw
        return (tx + ox * c - oy * s_, ty + ox * s_ + oy * c, back[2] * d + up[2] * sy)

    def screen_exact(self, f, P, w=1920, h=1080):
        """和 Blender 相机完全一致的投影（含俯角、转角、震动、焦距）：世界点 → 像素坐标；在镜头后面返回 None。"""
        fr = self.frames[f]
        C = self.cam_location(f)
        rx = math.radians(90.0 - fr.get("pitch", PITCH))
        rz = math.radians(fr.get("yaw", 0.0))
        v = (P[0] - C[0], P[1] - C[1], P[2] - C[2])
        # R = Rz(rz) @ Rx(rx)；相机坐标 = R^T v
        cz_, sz_ = math.cos(rz), math.sin(rz)
        x1 = cz_ * v[0] + sz_ * v[1]
        y1 = -sz_ * v[0] + cz_ * v[1]
        z1 = v[2]
        cx_, sx_ = math.cos(rx), math.sin(rx)
        xc = x1
        yc = cx_ * y1 + sx_ * z1
        zc = -sx_ * y1 + cx_ * z1
        if zc > -0.05:
            return None
        th = (SENSOR / 2) / fr.get("lens", LENS)
        nx, ny = xc / -zc / th, yc / -zc / (th / ASPECT)
        return to_px((nx, ny), w, h)

    def screen(self, f, P, w=1920, h=1080):
        """世界点在第 f 帧画面上的像素坐标。"""
        return to_px(project(self.frames[f]["cam"], P, self.frames[f].get("pitch", PITCH)), w, h)


if __name__ == "__main__":
    import sys
    import replay
    rp = replay.load(sys.argv[1])
    tl = Timeline(rp)
    print("frames", tl.n, "sec %.1f" % (tl.n / FPS), "final", tl.final, "winners", tl.anim.winners)
    for f in range(0, tl.n, max(1, tl.n // 12)):
        fr = tl.frames[f]
        print(f, "gt %.1f" % fr["gt"], "cam %.1f %.1f %.1f" % fr["cam"])
