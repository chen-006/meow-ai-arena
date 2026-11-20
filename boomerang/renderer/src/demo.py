# -*- coding: utf-8 -*-
"""演示模式（第 11 集 P5 规则演示、P4 素材用）：一份"导演表"（json）代替 timeline.Timeline 决定每一帧的
游戏时间、镜头，并且可以：给某个角色加时间偏移、按"某方看不看得见"把某个角色变成半透明、整个藏掉某些座位、
去掉某些特效。信息栏、击杀播报不画（make_video --demo 自动 --no-hud），脚下色环保留。纯 Python，不依赖 Blender。

导演表（所有键都可省，除了 gt_keys）：
{
  "frames": 360,                          成片帧数（省略 = 最后一个关键帧 + 1）
  "gt_keys": [[0, 9.0], [42, 23.0], [100, 25.5], [130, 25.5], ...],
                                          [帧号, 游戏步] 关键帧；中间用保形三次插值（不会冲过头），
                                          两个关键帧 gt 相同 = 定格。速度 = 每帧走几步（正常 2/3 步/帧）。
  "interp": "pchip" | "linear",
  "cam_keys": [{"f": 0, "x": 6.5, "y": 4.5, "dist": 11, "pitch": 58, "yaw": 0, "lens": 50,
                "track": [座位, 权重], "z": 0}, ...],
                                          x / y = 镜头看向的地面点（游戏坐标，米，y 向下）；dist = 机位到这一点的距离；
                                          pitch = 俯角；yaw = 绕竖轴转（度，正 = 机位往 +x 那边绕）；
                                          track = 看向点往某个角色身上拉（权重 0..1）；两个关键帧之间缓入缓出；
                                          某个关键帧写 "ease": "linear" 则它到下一个关键帧匀速。
  "cam_smooth": 0,                        镜头路径再做高斯平滑（帧；跟拍时用 4～8 去抖）
  "offset": {"0": 4},                     座位 → 晚几步渲染（这个人、他的镖、他的特效都晚）
  "hide": [2, 3],                         整个不渲染的座位（停在场边的陪衬程序）
  "ghost": {"seat": 0, "viewer": 1, "alpha": 0.2},
                                          viewer 看不见 seat 时，seat 变成半透明（按回放里的 vis 位）
  "no_fx": ["confetti", "land"]           不要的特效类型（fx.py 里的类型名）
}
"""
import json
import math

from pose import Animator

FPS = 30
W, H = 1920, 1080
SENSOR = 36.0


def load(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def _pchip(xs, ys):
    n = len(xs)
    if n == 1:
        return lambda x: ys[0]
    h = [xs[i + 1] - xs[i] for i in range(n - 1)]
    d = [(ys[i + 1] - ys[i]) / h[i] for i in range(n - 1)]
    m = [0.0] * n
    m[0], m[-1] = d[0], d[-1]
    for i in range(1, n - 1):
        if d[i - 1] * d[i] <= 0:
            m[i] = 0.0
        else:
            w1, w2 = 2 * h[i] + h[i - 1], h[i] + 2 * h[i - 1]
            m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])
    # 两端：同号才保留，免得冲过头
    if d[0] == 0:
        m[0] = 0.0
    if d[-1] == 0:
        m[-1] = 0.0

    def f(x):
        if x <= xs[0]:
            return ys[0]
        if x >= xs[-1]:
            return ys[-1]
        i = 0
        while xs[i + 1] < x:
            i += 1
        t = (x - xs[i]) / h[i]
        t2, t3 = t * t, t * t * t
        return ((2 * t3 - 3 * t2 + 1) * ys[i] + (t3 - 2 * t2 + t) * h[i] * m[i] + (-2 * t3 + 3 * t2) * ys[i + 1] +
                (t3 - t2) * h[i] * m[i + 1])
    return f


def _lin(xs, ys):
    def f(x):
        if x <= xs[0]:
            return ys[0]
        if x >= xs[-1]:
            return ys[-1]
        i = 0
        while xs[i + 1] < x:
            i += 1
        w = (x - xs[i]) / (xs[i + 1] - xs[i])
        return ys[i] + (ys[i + 1] - ys[i]) * w
    return f


def smooth01(t):
    t = min(1.0, max(0.0, t))
    return t * t * (3 - 2 * t)


CAM_DEF = dict(x=9.0, y=6.0, dist=12.0, pitch=58.0, yaw=0.0, lens=50.0, z=0.0)


class DemoTimeline:
    """和 timeline.Timeline 一样的接口：n、frames[f]（gt、cam、pitch、yaw、shake、lens）、anim、cam_location、screen。"""

    def __init__(self, rp, roster, director):
        self.rp = rp
        self.demo = director
        self.anim = Animator(rp, roster)
        self.final = None
        keys = sorted(director["gt_keys"], key=lambda k: k[0])
        n = int(director.get("frames", keys[-1][0] + 1))
        fx = [float(k[0]) for k in keys]
        fy = [float(k[1]) for k in keys]
        g = (_lin if director.get("interp") == "linear" else _pchip)(fx, fy)
        self.offset = {int(k): float(v) for k, v in director.get("offset", {}).items()}
        gts = [g(f) for f in range(n)]
        ck = sorted(director.get("cam_keys") or [dict(f=0)], key=lambda k: k["f"])
        raw = []
        for f in range(n):
            i = 0
            while i + 1 < len(ck) and ck[i + 1]["f"] <= f:
                i += 1
            a = self._resolve(ck[i], gts[f])
            if i + 1 < len(ck) and f > ck[i]["f"]:
                b = self._resolve(ck[i + 1], gts[f])
                u = (f - ck[i]["f"]) / float(ck[i + 1]["f"] - ck[i]["f"])
                w = u if ck[i].get("ease") == "linear" else smooth01(u)
                c = {}
                for key in a:
                    if key == "dist":
                        c[key] = math.exp(math.log(a[key]) * (1 - w) + math.log(b[key]) * w)
                    else:
                        c[key] = a[key] * (1 - w) + b[key] * w
                a = c
            raw.append(a)
        sig = float(director.get("cam_smooth", 0))
        if sig > 0:
            rad = int(sig * 3)
            ker = [math.exp(-0.5 * (j / sig) ** 2) for j in range(-rad, rad + 1)]
            sm = []
            for f in range(n):
                acc, ws = {k: 0.0 for k in raw[f]}, 0.0
                for j in range(-rad, rad + 1):
                    q = raw[min(max(f + j, 0), n - 1)]
                    w = ker[j + rad]
                    ws += w
                    for k in acc:
                        acc[k] += q[k] * w
                sm.append({k: v / ws for k, v in acc.items()})
            raw = sm
        self.frames = []
        for f in range(n):
            c = raw[f]
            self.frames.append(dict(gt=gts[f], cam=(c["x"], -c["y"], c["dist"]), pitch=c["pitch"], yaw=c["yaw"],
                                    lens=c["lens"], tz=c["z"], shake=(0.0, 0.0)))
        self.n = n

    def seat_gt(self, k, gt):
        return gt - self.offset.get(k, 0.0)

    def _resolve(self, key, gt):
        c = {k: float(key.get(k, CAM_DEF[k])) for k in CAM_DEF}
        tr = key.get("track")
        if tr:
            k, w = int(tr[0]), float(tr[1])
            px, py = self.rp.char_pos(k, max(0.0, min(self.rp.T, self.seat_gt(k, gt))))
            c["x"] += (px + float(tr[2] if len(tr) > 2 else 0.0) - c["x"]) * w
            c["y"] += (py + float(tr[3] if len(tr) > 3 else 0.0) - c["y"]) * w
        return c

    # ---- 镜头（Blender 坐标：X = 游戏 x，Y = -游戏 y，Z 向上）
    def cam_location(self, f):
        fr = self.frames[f]
        tx, ty, d = fr["cam"]
        p, yw = math.radians(fr["pitch"]), math.radians(fr["yaw"])
        return (tx + math.sin(yw) * math.cos(p) * d, ty - math.cos(yw) * math.cos(p) * d, fr["tz"] + math.sin(p) * d)

    def cam_params(self, f):
        fr = self.frames[f]
        return dict(loc=self.cam_location(f), pitch=fr["pitch"], yaw=fr["yaw"], lens=fr["lens"])

    def screen(self, f, P, w=W, h=H):
        """世界点（Blender 坐标）在第 f 帧画面上的像素坐标。"""
        return project(self.cam_params(f), P, w, h)


def project(cam, P, w=W, h=H):
    """cam = dict(loc, pitch, yaw, lens)；P = Blender 坐标 (X, Y, Z)。返回像素 (x, y)；在镜头背后时返回 None。
    和 Blender 相机一致：rotation_euler = (90° - pitch, 0, yaw)，传感器 36 毫米按宽度。"""
    lx, ly, lz = cam["loc"]
    vx, vy, vz = P[0] - lx, P[1] - ly, P[2] - lz
    yw = math.radians(cam["yaw"])
    a = math.radians(90.0 - cam["pitch"])
    # 先绕 Z 转 -yaw，再绕 X 转 -a（把世界向量转进相机坐标）
    cz_, sz_ = math.cos(-yw), math.sin(-yw)
    x1, y1, z1 = vx * cz_ - vy * sz_, vx * sz_ + vy * cz_, vz
    cx_, sx_ = math.cos(-a), math.sin(-a)
    x2, y2, z2 = x1, y1 * cx_ - z1 * sx_, y1 * sx_ + z1 * cx_
    if z2 >= -1e-6:
        return None
    th = (SENSOR / 2) / cam["lens"]
    nx = (x2 / -z2) / th
    ny = (y2 / -z2) / (th * h / w)
    return ((nx * 0.5 + 0.5) * w, (0.5 - ny * 0.5) * h)


def game_pt(x, y, z=0.0):
    """游戏坐标（米，y 向下）→ Blender 坐标。"""
    return (x, -y, z)


def export_coords(tl, path, a=0, b=None):
    """逐帧写出镜头参数和每个角色（脚下、胸口、头顶）、每把镖的屏幕坐标（像素，1920×1080）。
    叠加图形用：world→screen 可以直接用 project(frames[f]["cam"], (X, Y, Z))。"""
    rp, A = tl.rp, tl.anim
    b = tl.n if b is None else b
    out = dict(_说明="逐帧：gt = 游戏步；cam = Blender 相机（loc 米、pitch/yaw 度、lens 毫米，传感器宽 36）；"
                     "chars[k] = 座位 k 的 [脚下, 胸口 0.6 米, 头顶 1.25 米] 像素坐标；darts[k] = 镖的像素坐标"
                     "（null = 不在场上 / 在手里）。座位的时间偏移已经算进去。",
               w=W, h=H, fps=FPS, offset=tl.offset, frames=[])
    for f in range(a, b):
        fr = tl.frames[f]
        cam = tl.cam_params(f)
        chars, darts = [], []
        for k in range(rp.n):
            g = tl.seat_gt(k, fr["gt"])
            gc = max(0.0, min(rp.T, g))
            x, y = rp.char_pos(k, gc)
            chars.append([project(cam, (x, -y, z)) for z in (0.0, 0.6, 1.25)])
            d = A.dart_state(k, g)
            if d is None or d.get("mode") == "hand":
                darts.append(None)
            else:
                darts.append(project(cam, (d["x"], -d["y"], d["z"])))
        out["frames"].append(dict(f=f, gt=round(fr["gt"], 4), cam=dict(loc=[round(v, 5) for v in cam["loc"]],
                                                                         pitch=round(cam["pitch"], 4),
                                                                         yaw=round(cam["yaw"], 4),
                                                                         lens=round(cam["lens"], 4)),
                                  chars=[[None if p is None else [round(p[0], 1), round(p[1], 1)] for p in c] for c in chars],
                                  darts=[None if p is None else [round(p[0], 1), round(p[1], 1)] for p in darts]))
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(out, fh, ensure_ascii=False)
    return path
