# -*- coding: utf-8 -*-
"""回放 -> 每一刻的姿势（纯 Python，不依赖 Blender）。

所有东西都是"游戏时间 gt（步，可带小数）"的纯函数：任意一帧可以单独算，所以能分段并行、断点续渲。
回放每秒 20 步、只给位置 / 朝向 / 状态；这里补出中间姿态：
  * 预备动作利用"回放已知未来"提前起势（扔镖前约 0.18 秒就开始抬手），收势按自己的节奏放长；
  * 压扁拉伸、跟随（头发、尾巴、双马尾按速度往后甩）；
  * gt < 0 是入场，gt > 结束步 是胜者定格。
pose 的字段：x y z heading(度) lean roll spin flip sx sy sz scale body_dz twist head(pitch,yaw,roll) head_dz
  hand_a hand_b foot_a foot_b（各 (前, 侧, 高)，角色局部）hair(back, side) tail face dart tint flash ground ring visible
"""
import math

from replay import (ST_FREE, ST_THROW, ST_THROWN, ST_SLASH, ST_SLASHED, ST_KICK, ST_KICKED, ST_DASH, ST_STUN,
                    ST_RECALL, ST_DEAD, D_HAND, D_OUT, D_BACK, D_GROUND, D_RISE, D_GONE, D_CALL, FLYING, CELL_GRASS, CELL_PIT)

import cast
import liquid

FLY_Z = 0.66
INTRO = 46          # 入场用多少步（gt 从 -INTRO 开始）
OUTRO = 56          # 结束后再演多少步
SLICE_LIFE = 19.0   # 像素小人被镖切成两截后，两截在场上留多少步
SLICE_TILT = 0.0    # 切面相对水平面的角度（11-16 改成 0：水平地拦腰切成上下两截）
SLICE_POOF = 16.0   # 两截各自"噗"地消失的时刻（切开后多少步）
SLICE_UP_GO = 1.3  # 上半截被镖带着沿镖的方向滑出去多远（米）
SLICE_LOW_GO = 0.32  # 下半截倒向哪儿（沿 s 方向，米）：用来查那儿有没有地、放"噗"的位置
FALL_G = 0.05        # 落坑下坠的重力（米/步²，原来就是这个值）
FALL_HANG = dict(walk=6.0, kick=0.4, ground=0.0)    # 落坑前在坑沿上挣扎多久（原来 1.8 / 1.8 / 0）
FALL_SPLASH_Z = -10.5   # 掉到这个高度算没入云海（主云层在 -13，稀疏的一层在 -8.5）


def clamp(x, a=0.0, b=1.0):
    return a if x < a else (b if x > b else x)


def ease(t):
    t = clamp(t)
    return t * t * (3 - 2 * t)


def eout(t):
    t = clamp(t)
    return 1 - (1 - t) ** 3


def bump(t):
    return math.sin(math.pi * clamp(t))


def lerp(a, b, w):
    if isinstance(a, tuple):
        return tuple(x + (y - x) * w for x, y in zip(a, b))
    return a + (b - a) * w


def mix(p, target, w):
    """把 target 里的字段按权重 w 混进 p。"""
    if w <= 0:
        return
    for k, v in target.items():
        if isinstance(v, (int, float, tuple)) and not isinstance(v, bool):
            if k in p:
                p[k] = lerp(p[k], v, w)
            else:
                p[k] = lerp((0.0,) * len(v) if isinstance(v, tuple) else (1.0 if k in ("sx", "sy", "sz") else 0.0), v, w)
        elif w > 0.5:
            p[k] = v


def slice_curve(u):
    """被镖拦腰切开之后的运动曲线（u = 切开后过了几步；11-16 从竖劈改成横切）。
    上半截：被镖"啪"地带起一点、沿镖的方向错开一截（顿帧正好定在这一下：下半截平整的切面露出来），
            接着带着惯性滑出去、从下半截上掉下来，一边往远离镜头的一侧翻倒（切面翻过来对着镜头），落地弹一下、再滑一点停住；
    下半截：原地顿住、晃两下，然后朝镜头一侧倒下（切面跟着转向镜头）。最后两截各自缩小"噗"地消失。
    返回标量，方向由装配那边按镖的方向换算：
      carry 上半截沿镖的方向走了多少米   lift 上半截被带起多高   th_up 上半截侧翻的角度   yaw_up 上半截在地上打转
      leave 上半截离开下半截切面的时刻（之后才受重力）   th_low 下半截倒下的角度   scale 消失前的缩小   flash 闪白"""
    u = max(0.0, u)
    carry = 0.11 * (1 - math.exp(-u / 0.3)) + (SLICE_UP_GO - 0.11) * (1 - math.exp(-max(0.0, u - 0.4) / 2.6))
    lift = 0.08 * bump(u / 1.8)
    # 上半截：离开切面后翻倒，落地（约 u=4.5）时过一点再弹回
    v = u - 0.9
    if v <= 0:
        th_up = 0.05 * clamp(u / 0.6)
    elif v < 3.6:
        th_up = 0.05 + (math.pi / 2 + 0.12 - 0.05) * ease(v / 3.6) ** 1.3
    else:
        th_up = math.pi / 2 + 0.12 * math.cos((v - 3.6) * 1.6) * math.exp(-(v - 3.6) / 1.5)
    yaw_up = 0.35 * eout(max(0.0, u - 0.9) / 6.0)
    # 下半截：顿一下（轻微晃），约 u=3 起倒，u≈6 落地，弹一下
    w = u - 3.0
    if w <= 0:
        th_low = 0.045 * math.sin(u * 2.6) * clamp(u / 0.8)
    elif w < 2.8:
        th_low = (math.pi / 2) * ease(w / 2.8) ** 1.7
    else:
        th_low = math.pi / 2 - 0.1 * abs(math.sin((w - 2.8) * 1.5)) * math.exp(-(w - 2.8) / 1.2)
    return dict(carry=carry, lift=lift, th_up=th_up, yaw_up=yaw_up, leave=0.9, th_low=th_low,
                scale=1 - ease((u - SLICE_POOF) / (SLICE_LIFE - SLICE_POOF)),
                flash=0.75 * clamp(1 - (u - 0.2) / 0.9))


def ang_of(dx, dy):
    return math.degrees(math.atan2(dy, dx))


def ang_lerp(a, b, w):
    d = (b - a + 180.0) % 360.0 - 180.0
    return a + d * w


class Animator:
    def __init__(self, rp, roster=None, life=0, lives=None):
        """life：第几条命（第 12 集占点有复活）。主 Animator 是第 0 条命，另外每条命各建一个子 Animator，
        按"这个人此刻在第几条命"把 char_pose 等分派过去；各条命只是 deaths 不同，回放本身是同一份。"""
        self.rp = rp
        self.n = rp.n
        self.cfg = rp.cfg
        self.roster = roster
        self.life = life
        self.deaths = rp.deaths_of_life(life) if hasattr(rp, "deaths_of_life") else dict(rp.deaths)
        self.teams = [p[1] for p in rp.players]
        self.team_mode = rp.mode in (2, 3, 4, 5)
        self.respawn_mode = getattr(rp, "max_lives", 1) > 1
        self._acts = [self._build_acts(k) for k in range(self.n)]
        self._land = [self._land_times(k) for k in range(self.n)]
        # 入场顺序：两队交替
        order = list(range(self.n))
        if self.team_mode:
            a = [k for k in order if self.teams[k] == 0]
            b = [k for k in order if self.teams[k] == 1]
            order = [x for pair in zip(a, b) for x in pair] + a[len(b):] + b[len(a):]
        gap = 2.2 if self.n > 4 else 3.2
        self.t_land = {k: -INTRO + 5 + i * gap for i, k in enumerate(order)}
        self.winners = self._winners()
        # 像素小人被镖打中 = 被切成两半（旧的圆润小人仍是被打飞）
        keys = cast.roster_for(rp, roster)
        self.sliced = {k for k, dth in self.deaths.items() if dth[1] == 1 and cast.use_voxel(keys[k])}
        if lives is None:
            self.lives = [self]
            for i in range(1, getattr(rp, "max_lives", 1)):
                self.lives.append(Animator(rp, roster, life=i, lives=self.lives))
            self.static_fx = self._build_fx()
            if len(self.lives) > 1:
                for A in self.lives[1:]:
                    self.static_fx += A.static_fx
                self.static_fx.sort(key=lambda e: e[0])
        else:
            self.lives = lives
            self.static_fx = self._build_fx()

    def L(self, k, gt):
        """座位 k 在 gt 时那条命的 Animator。"""
        if len(self.lives) == 1:
            return self
        return self.lives[min(self.rp.life_index(k, gt), len(self.lives) - 1)]

    def all_deaths(self):
        """[(该条命的 Animator, 座位)]：每一次出局。"""
        return [(A, k) for A in self.lives for k in A.deaths]

    # ------------------------------------------------------------ 预处理
    def _winners(self):
        rp = self.rp
        last = rp.states[rp.T]["chars"]
        if rp.winner < 0:
            return set()
        if self.team_mode:
            return {k for k in range(self.n) if last[k]["alive"] and self.teams[k] == rp.winner}
        return {rp.winner} if last[rp.winner]["alive"] else set()

    def _build_acts(self, k):
        rp, ev, cf = self.rp, self.rp.ev[k], self.cfg
        A = dict(throw=[], slash=[], kick=[], dash=[], recall=[], stun=[], catch=[], pick=[])
        rel = {e[0] for e in ev["release"]}
        for (t0, ax, ay) in ev["throw"]:
            tr = t0 + cf.get("THROW_PRE", 2) - 1
            A["throw"].append(dict(t0=t0, T=(tr + 1.0) if tr in rel else None, dir=(ax, ay)))
        sw = {(e[0], e[1]): e[2] for e in ev["swing"]}
        for (t0, ax, ay) in ev["slash"]:
            ts = t0 + cf.get("SLASH_PRE", 2) - 1
            A["slash"].append(dict(t0=t0, T=(ts + 1.0) if (ts, 1) in sw else None, dir=(ax, ay), hit=sw.get((ts, 1), 0)))
        for (t0, ax, ay) in ev["kick"]:
            ts = t0 + cf.get("KICK_PRE", 2) - 1
            A["kick"].append(dict(t0=t0, T=(ts + 1.0) if (ts, 0) in sw else None, dir=(ax, ay), hit=sw.get((ts, 0), 0)))
        stops = sorted([e[0] for e in ev["stop"]] + [e[0] - 1 for e in ev["slash"]] + [e[0] - 1 for e in ev["kick"]] +
                       [e[0] for e in ev["kicked"]])
        for (t0, ax, ay) in ev["dash"]:
            # 11-16 规则：DASH 事件是"接受冲刺、开始前摇"，前摇 DASH_PRE 步原地不动，之后才冲 DASH_T 步。
            # 这里的 T0 / T1 是真正在冲的那一段；前摇正好落在下面"先蹲一下"的预备姿势里，收势落在"急停"里。
            pre = cf.get("DASH_PRE", 0)
            m0 = t0 + pre
            cut = next((s for s in stops if t0 <= s < m0), None)
            if cut is not None:          # 前摇里就被打断（被踢）：没冲出去
                A["dash"].append(dict(T0=cut + 1.0, T1=cut + 1.0, dir=(ax, ay)))
                continue
            t1 = next((s for s in stops if m0 <= s <= m0 + cf.get("DASH_T", 4)), m0 + cf.get("DASH_T", 4) - 1)
            A["dash"].append(dict(T0=float(m0), T1=t1 + 1.0, dir=(ax, ay)))
        ends = sorted([(e[0], True) for e in ev["call"]] + [(e[0], False) for e in ev["cancel"]])
        for (t0, dx, dy) in ev["recall"]:
            en = next(((t, ok) for (t, ok) in ends if t > t0), (t0 + cf.get("RECALL_PRE", 12), False))
            A["recall"].append(dict(T0=t0 + 1.0, T1=en[0] + 1.0, ok=en[1], dart=(dx, dy)))
        for (t0, kicker) in ev["kicked"]:
            A["stun"].append(dict(T0=t0 + 1.0, T1=t0 + 1.0 + cf.get("KICK_STUN", 12), kicker=kicker))
        A["catch"] = [e[0] + 1.0 for e in ev["catch"]]
        A["pick"] = [e[0] + 1.0 for e in ev["pick"]]
        return A

    def _land_times(self, k):
        """镖每次落地的时刻（变成 ground 的那一步）。"""
        rp = self.rp
        out = []
        for t in range(1, rp.T + 1):
            if rp.states[t]["darts"][k]["st"] in (D_GROUND, D_RISE) and rp.states[t - 1]["darts"][k]["st"] in FLYING:
                out.append(t)
        return out

    # ------------------------------------------------------------ 小工具
    def face_vec(self, k, t):
        rp = self.rp
        t = clamp(t, 0, rp.T)
        i = int(t)
        a = t - i
        f0 = rp.states[i]["chars"][k]["face"] / 100.0
        f1 = rp.states[min(i + 1, rp.T)]["chars"][k]["face"] / 100.0
        f = math.radians(ang_lerp(f0, f1, a))
        return math.cos(f), math.sin(f)

    def heading(self, k, gt):
        sx = sy = 0.0
        for dt, w in ((-1.2, 0.2), (-0.6, 0.3), (0.0, 0.3), (0.5, 0.2)):
            x, y = self.face_vec(k, gt + dt)
            sx += x * w
            sy += y * w
        return ang_of(sx, sy)

    def speed(self, k, gt):
        """这一刻的移动速度（米/步），前后各看半步，平滑一点。"""
        x0, y0 = self.rp.char_pos(k, gt - 0.6)
        x1, y1 = self.rp.char_pos(k, gt + 0.6)
        return math.hypot(x1 - x0, y1 - y0) / 1.2

    def hidden(self, k, gt):
        """藏在草丛里（对手都看不见他）的程度 0..1。"""
        rp = self.rp
        tot = 0.0
        for dt in (-1.5, -0.5, 0.5, 1.5):
            t = int(clamp(gt + dt, 0, rp.T))
            c = rp.states[t]["chars"][k]
            if not c["alive"]:
                continue
            opp = 0
            for j in range(self.n):
                if j != k and (not self.team_mode or self.teams[j] != self.teams[k]) and rp.states[t]["chars"][j]["alive"]:
                    opp |= 1 << j
            if opp and (c["vis"] & opp) == 0 and rp.cell_at(c["x"] / 1000.0, c["y"] / 1000.0)[0] == CELL_GRASS:
                tot += 0.25
        return tot

    def dart_mode(self, k, gt):
        rp = self.rp
        if gt >= rp.T:
            st = rp.states[rp.T]["darts"][k]["st"]
        else:
            st = rp.states[max(0, int(gt))]["darts"][k]["st"]
        return st

    # ------------------------------------------------------------ 角色
    def char_pose(self, k, gt):
        A = self.L(k, gt)
        if A is not self:
            return A.char_pose(k, gt)
        rp = self.rp
        death = self.deaths.get(k)
        if death is not None and gt >= death[0] + 1.0:
            p = self._dead_pose(k, gt, death)
            p["life"] = self.life
            return p
        x, y = rp.char_pos(k, gt)
        v = self.speed(k, gt) if 0 < gt < rp.T else 0.0
        st = rp.S(gt)["chars"][k]["st"] if gt > 0 else ST_FREE
        rf = clamp(v / 0.25, 0, 1.15) if st in (ST_FREE, ST_THROW, ST_THROWN) else 0.0
        ph = rp.dist_at(k, gt) / 0.8 * math.pi
        s, c = math.sin(ph), math.cos(ph)
        br = math.sin(gt * 0.42 + k * 1.7)
        p = dict(x=x, y=y, z=0.0, heading=self.heading(k, gt), lean=0.2 * rf, roll=0.05 * s * rf, spin=0.0,
                 sx=1.0, sy=1.0, sz=1.0 + 0.012 * br * (1 - rf), body_dz=0.028 * abs(s) * rf - 0.008 * rf, twist=0.0,
                 head=(-0.06 * rf, 0.0, 0.05 * s * rf), head_dz=0.0,
                 hand_a=(-0.11 * s * rf, 0.25 - 0.03 * rf, 0.29 + 0.02 * rf + 0.006 * br),
                 hand_b=(0.11 * s * rf, -0.25 + 0.03 * rf, 0.29 + 0.02 * rf + 0.006 * br),
                 foot_a=(0.12 * s * rf, 0.085, 0.04 + 0.075 * max(0.0, c) * rf),
                 foot_b=(-0.12 * s * rf, -0.085, 0.04 + 0.075 * max(0.0, -c) * rf),
                 hair=(0.5 * rf + 0.07 * math.sin(2 * ph) * rf + 0.03 * br, 0.08 * s * rf),
                 face="open", tint=(1.0, 1.0, 1.0), flash=0.0, ground=1.0, ring=1.0, visible=True,
                 hop=abs(s) * rf)
        holding = self.dart_mode(k, gt) == D_HAND
        dart = dict(off=(0.07, 0.07, 0.09), rot=(math.radians(75), 0.0, math.radians(15))) if holding else None
        A = self._acts[k]
        hd = [None, 0.0]      # 动作方向（度）和权重

        def face_dir(d, w):
            if w > hd[1]:
                hd[0], hd[1] = ang_of(d[0], d[1]), w

        # ---- 扔镖：提前抬手蓄势 -> 甩出 -> 收势
        for a in A["throw"]:
            T = a["T"] if a["T"] is not None else a["t0"] + 2.0
            if T - 3.8 < gt < T + 9.0:
                # 起势：重心压到后脚、身体拧过去、持镖的手举到脑后，另一只手往前伸出去找平衡
                wind = dict(hand_a=(-0.3, 0.22, 0.76), hand_b=(0.24, -0.18, 0.4), twist=0.78, lean=-0.17, sz=0.9, sx=1.05,
                            body_dz=-0.028, foot_a=(0.07, 0.085, 0.04), foot_b=(-0.15, -0.085, 0.04),
                            head=(0.05, -0.35, 0.0), hair=(0.05, 0.3))
                # 甩出：前脚踏出去踩实、整个人往前压，后脚被带得离地
                thrown = dict(hand_a=(0.46, 0.02, 0.4), hand_b=(-0.22, -0.28, 0.34), twist=-0.78, lean=0.42, sz=1.07,
                              sy=1.12, body_dz=0.012, head=(-0.12, 0.22, 0.0), foot_a=(0.23, 0.085, 0.04),
                              foot_b=(-0.15, -0.085, 0.09), foot_b_pitch=0.55, hair=(0.42, -0.35))
                if gt < T:
                    w = eout((gt - (T - 3.8)) / 3.0)
                    mix(p, wind, w)
                    if dart:
                        dart = dict(off=(-0.05, 0.05, 0.12), rot=(math.radians(25), 0.0, gt * 0.9))
                    face_dir(a["dir"], w)
                elif a["T"] is not None:
                    r = gt - T
                    whip = eout(r / 1.4)
                    rec = ease((r - 3.2) / 5.6)
                    mix(p, wind, (1 - whip) * (1 - rec))
                    mix(p, thrown, whip * (1 - rec))
                    # 出手那一下的过冲：身体多探出去一点再弹回来，脚下小小腾一下
                    ov = bump(r / 3.0) * (1 - rec)
                    p["lean"] += 0.13 * ov
                    p["z"] += 0.035 * bump(r / 2.2)
                    p["sz"] *= 1 - 0.05 * bump((r - 1.6) / 2.6)
                    face_dir(a["dir"], 1 - rec)
        # ---- 挥砍：横拉到身侧 -> 一刀扫过身前 -> 定住再收
        for a in A["slash"]:
            T = a["T"] if a["T"] is not None else a["t0"] + 2.0
            if T - 3.8 < gt < T + 7.5:
                th = math.radians(105)
                if gt < T - 0.6:
                    w = eout((gt - (T - 3.8)) / 2.6)
                    mix(p, dict(hand_a=(-0.16, 0.38, 0.52), hand_b=(0.18, 0.0, 0.4), twist=1.05, lean=-0.1, sz=0.9, sx=1.05,
                                body_dz=-0.025, foot_b=(-0.13, -0.085, 0.04), head=(0.0, -0.5, 0.0), hair=(0.1, 0.4)), w)
                    if dart:
                        dart = dict(off=(-0.12, 0.2, 0.03), rot=(0.0, 0.0, gt * 0.5), scale=1.25)
                    face_dir(a["dir"], w)
                elif a["T"] is not None:
                    sw = ease((gt - (T - 0.6)) / 1.7)
                    rec = ease((gt - (T + 3.0)) / 4.5)
                    th = math.radians(105 - 190 * sw)
                    w = 1 - rec
                    mix(p, dict(hand_a=(0.4 * math.cos(th), 0.4 * math.sin(th), 0.44), hand_b=(-0.12, -0.28, 0.36),
                                twist=1.05 - 2.2 * sw, lean=0.38 * bump((gt - (T - 0.6)) / 4.0), sz=1.0 - 0.06 * bump(sw),
                                sy=1.0 + 0.12 * bump(sw), body_dz=-0.02 * bump(sw),
                                head=(0.0, 0.3 * sw, 0.0), foot_a=(0.2, 0.085, 0.04), foot_b=(-0.14, -0.085, 0.04),
                                hair=(0.2, 0.4 - 0.9 * sw)), w)
                    if dart:
                        dart = dict(off=(0.2 * math.cos(th), 0.2 * math.sin(th), 0.02), rot=(0.0, 0.0, -th * 2), scale=1.25)
                    face_dir(a["dir"], w)
        # ---- 踢：下蹲 -> 腾起蹬腿、身体后仰 -> 落地
        for a in A["kick"]:
            T = a["T"] if a["T"] is not None else a["t0"] + 2.0
            if T - 3.4 < gt < T + 6.5:
                if gt < T - 0.5:
                    w = eout((gt - (T - 3.4)) / 2.4)
                    mix(p, dict(sz=0.86, sx=1.08, lean=0.1, foot_a=(-0.12, 0.085, 0.06), hand_a=(0.1, 0.24, 0.4),
                                hand_b=(0.1, -0.24, 0.4)), w)
                    face_dir(a["dir"], w)
                elif a["T"] is not None:
                    kk = eout((gt - (T - 0.5)) / 1.3)
                    rec = ease((gt - (T + 2.2)) / 4.0)
                    w = kk * (1 - rec)
                    mix(p, dict(z=0.12 * bump((gt - (T - 0.5)) / 3.5), lean=-0.42, sz=1.04, sy=1.12,
                                foot_a=(0.44, 0.03, 0.34), foot_a_pitch=-1.85, foot_b=(-0.04, -0.085, 0.04),
                                hand_a=(-0.2, 0.3, 0.42), hand_b=(-0.2, -0.3, 0.42), head=(0.25, 0.0, 0.0),
                                hair=(-0.3, 0.0)), w)
                    face_dir(a["dir"], 1 - rec)
        # ---- 冲刺：先蹲一下 -> 整个人拉长前扑 -> 急停
        for a in A["dash"]:
            T0, T1 = a["T0"], a["T1"]
            if T0 - 2.4 < gt < T1 + 3.5:
                if gt < T0:
                    w = eout((gt - (T0 - 2.4)) / 2.0)
                    mix(p, dict(sz=0.82, sx=1.1, lean=0.14), w)
                    face_dir(a["dir"], w)
                elif gt < T1:
                    w = eout((gt - T0) / 0.8)
                    mix(p, dict(lean=0.9, sz=0.9, sx=0.92, sy=1.28, hand_a=(-0.26, 0.2, 0.4), hand_b=(-0.26, -0.2, 0.4),
                                foot_a=(-0.16, 0.085, 0.12), foot_b=(-0.1, -0.085, 0.18), head=(-0.55, 0.0, 0.0),
                                hair=(1.2, 0.0), body_dz=0.0, roll=0.0), w)
                    face_dir(a["dir"], 1.0)
                else:
                    r = (gt - T1) / 3.5
                    q = math.exp(-r * 3.2) * math.cos(r * 5.5)
                    mix(p, dict(lean=p["lean"] - 0.3 * q, sz=1 - 0.12 * q, sx=1 + 0.1 * q,
                                hair=(p["hair"][0] - 0.5 * q, 0.0)), 1.0)
        # ---- 召回：站定举手
        for a in A["recall"]:
            T0, T1 = a["T0"], a["T1"]
            if T0 - 0.5 < gt < T1 + 2.5:
                w = ease((gt - (T0 - 0.5)) / 2.0) * (1 - ease((gt - T1) / 2.5))
                mix(p, dict(hand_a=(0.1, 0.2, 0.86 + 0.03 * math.sin(gt * 1.7)), hand_b=(0.06, -0.24, 0.33),
                            head=(-0.25, 0.0, 0.0), sz=1.04 + 0.015 * math.sin(gt * 1.7), lean=-0.04,
                            hair=(0.12 + 0.06 * math.sin(gt * 1.1), 0.0)), w)
        # ---- 接镖 / 捡镖
        for T in A["catch"]:
            if T - 3.0 < gt < T + 3.5:
                if gt < T:
                    mix(p, dict(hand_a=(0.2, 0.2, 0.74), head=(-0.15, 0.0, 0.0)), ease((gt - (T - 3.0)) / 2.4) * 0.9)
                else:
                    r = (gt - T) / 3.5
                    q = math.exp(-r * 3.5) * math.cos(r * 6.0)
                    mix(p, dict(sz=p["sz"] * (1 - 0.12 * q), sx=1 + 0.08 * q), 1.0)
        for T in A["pick"]:
            if T - 1.8 < gt < T + 2.2:
                b = bump((gt - (T - 1.8)) / 4.0)
                mix(p, dict(sz=1 - 0.24 * b, sx=1 + 0.1 * b, lean=p["lean"] + 0.25 * b, hand_a=(0.16, 0.2, 0.12)), b)
        # ---- 被踢：先压扁，再往后飞出去、手脚乱挥
        for a in A["stun"]:
            T0, T1 = a["T0"], a["T1"]
            if T0 - 0.3 < gt < T1 + 2.5:
                u = gt - T0
                w = eout((u + 0.3) / 0.8) * (1 - ease((gt - (T1 - 2.0)) / 4.5))
                sq = math.exp(-max(0.0, u) / 1.3)
                fl = math.sin(u * 2.3)
                mix(p, dict(z=0.24 * bump(u / 6.0), lean=-0.75, sz=1 - 0.26 * sq + 0.06 * (1 - sq), sx=1 + 0.2 * sq,
                            hand_a=(-0.06, 0.32, 0.7 + 0.07 * fl), hand_b=(-0.06, -0.32, 0.7 - 0.07 * fl),
                            foot_a=(0.18, 0.09, 0.16 + 0.04 * fl), foot_b=(0.14, -0.09, 0.1 - 0.04 * fl),
                            head=(0.3, 0.0, 0.1 * fl), hair=(-0.6, 0.15 * fl), roll=0.12 * fl), w)
                if w > 0.3:
                    p["face"] = "tight"
                kx, ky = rp.char_pos(a["kicker"], T0 - 1.0)
                vx, vy = rp.char_pos(k, T0 - 1.0)
                if math.hypot(kx - vx, ky - vy) > 1e-3:
                    face_dir((kx - vx, ky - vy), w)
                p["flash"] = max(p["flash"], clamp(1 - u / 1.4) * 0.8 if u >= 0 else 0.0)
        if hd[0] is not None:
            p["heading"] = ang_lerp(p["heading"], hd[0], hd[1])
        # ---- 11-16 规则：被踢滑行 / 冲刺途中经过坑不掉。人照常画（不下坠），只是脚下没地：接触影和色环淡掉
        if 0 < gt < rp.T:
            cell, wv = rp.cell_at(x, y)
            if cell == CELL_PIT or (wv >= 0 and rp.wave_steps[wv] <= gt):
                p["ground"] = 0.0
        # ---- 藏在草丛里：蹲低、整个人暗下来、脚下色环变淡
        hf = self.hidden(k, gt) if 0 < gt < rp.T - 1 else 0.0
        if hf > 0:
            p["sz"] *= 1 - 0.2 * hf
            p["lean"] += 0.1 * hf
            p["tint"] = lerp((1.0, 1.0, 1.0), (0.55, 0.78, 0.7), hf)
            p["ring"] = 1 - 0.8 * hf
        # ---- 入场：从天而降，落地一压一弹
        if gt < 0:
            u = gt - self.t_land[k]
            if u < -9:
                p["visible"] = False
            elif u < 0:
                mix(p, dict(z=-u * 1.15, sz=1.22, sx=0.9, hand_a=(0.0, 0.3, 0.72), hand_b=(0.0, -0.3, 0.72),
                            hair=(1.5, 0.0), foot_a=(0.05, 0.085, 0.1), foot_b=(-0.05, -0.085, 0.06)), 1.0)
                p["ground"] = clamp(1 + u / 6.0)
            else:
                q = math.exp(-u / 2.4) * math.cos(u * 1.25)
                mix(p, dict(sz=1 - 0.4 * q, sx=1 + 0.3 * q, hair=(0.03 - 0.5 * q, 0.0)), 1.0)
                if u < 9:
                    p["face"] = "happy"
            # 开打前一起蓄一下势
            if gt > -7:
                b = bump((gt + 7) / 7.0)
                p["sz"] *= 1 - 0.1 * b
                p["sx"] *= 1 + 0.05 * b
        # ---- 胜者：转向镜头，举手跳
        if gt > rp.T and k in self.winners:
            u = gt - rp.T - 4.0
            if u > 0:
                w = ease(u / 3.0)
                jp = (u % 9.0) / 9.0
                jz = math.sin(math.pi * jp)
                land = math.exp(-jp * 9.0) + math.exp(-(1 - jp) * 9.0)
                mix(p, dict(z=0.34 * jz, sz=1 + 0.1 * jz - 0.16 * land, sx=1 - 0.05 * jz + 0.1 * land,
                            hand_a=(0.02, 0.28, 0.84), hand_b=(0.02, -0.28, 0.84), head=(-0.2, 0.0, 0.08 * math.sin(u * 0.7)),
                            hair=(0.25 * jz, 0.0), lean=-0.05), w)
                p["heading"] = ang_lerp(p["heading"], 90.0, w)
                p["face"] = "happy"
                if dart:
                    dart = dict(off=(0.0, 0.05, 0.14), rot=(math.radians(90), 0.0, u * 0.5))
        # ---- 第 12 集：复活（从一道光里落下来，落地一压一弹）、国王挨打后的无敌闪烁
        if self.life > 0:
            a0, _ = rp.life_span(k, self.life)
            u = gt - a0
            if 0 <= u < 12:
                if u < 3.0:
                    w = 1 - u / 3.0
                    mix(p, dict(z=1.4 * w * w, sz=1.18, sx=0.9, hand_a=(0.0, 0.3, 0.72), hand_b=(0.0, -0.3, 0.72),
                                hair=(1.3, 0.0)), w)
                    p["scale"] = p.get("scale", 1.0) * (0.55 + 0.45 * eout(u / 3.0))
                else:
                    q = math.exp(-(u - 3) / 2.2) * math.cos((u - 3) * 1.3)
                    p["sz"] *= 1 - 0.3 * q
                    p["sx"] *= 1 + 0.22 * q
                p["flash"] = max(p.get("flash", 0.0), 0.7 * clamp(1 - u / 7.0))
                p["face"] = "happy" if u < 10 else p["face"]
        kl = rp.king_lives(k, gt) if hasattr(rp, "king_lives") else None
        if kl is not None and kl[1] > 0 and 0 < gt < rp.T:
            on = (int(math.floor(gt / 1.5)) % 2) == 0
            p["flash"] = max(p.get("flash", 0.0), 0.62 if on else 0.08)
        p["life"] = self.life
        if dart:
            ha = p["hand_a"]
            p["dart"] = dict(pos=(ha[0] + dart["off"][0], ha[1] + dart["off"][1], ha[2] + dart["off"][2]),
                             rot=dart["rot"], scale=dart.get("scale", 1.0))
        return p

    def death_info(self, k):
        """(时刻 Td, 方式, 击飞方向, 是否随塌陷坠落)。"""
        rp = self.rp
        t, cause, killer = self.deaths[k]
        Td = t + 1.0
        px, py = rp.char_pos(k, Td)
        d = (0.0, 1.0)
        with_ground = False
        if cause == 1 and killer >= 0:
            a = rp.states[t]["darts"][killer]
            b = rp.states[min(t + 1, rp.T)]["darts"][killer]
            dx, dy = b["x"] - a["x"], b["y"] - a["y"]
            if b["st"] == D_HAND or math.hypot(dx, dy) < 1:
                dx, dy = px * 1000 - a["x"], py * 1000 - a["y"]
            L = math.hypot(dx, dy)
            d = (dx / L, dy / L) if L > 1e-6 else d
        elif cause == 2 and killer >= 0:
            kx, ky = rp.char_pos(killer, Td)
            L = math.hypot(px - kx, py - ky)
            d = ((px - kx) / L, (py - ky) / L) if L > 1e-6 else d
        else:
            x0, y0 = rp.char_pos(k, Td - 1.0)
            d = (px - x0, py - y0)            # 落坑：保留最后一步的位移（米/步）
            w = rp.cell_at(px, py)[1]
            if w >= 0 and Td - 2.5 <= rp.wave_steps[w] <= Td + 1:
                with_ground = True
        return Td, cause, d, with_ground

    def slice_info(self, k):
        """被镖切成两半的那一刻：None，或 dict(Td, base 被切前一瞬的姿势, pos, d 镖的方向, s 上半截滑向哪一侧, tilt)。
        坐标都是地图坐标（y 朝屏幕下方）。s 取垂直于 d、朝屏幕下方（朝镜头）的那一侧，切面朝上的那半截正好对着镜头。"""
        if k not in self.sliced:
            return None
        Td, cause, d, _ = self.death_info(k)
        base = self.char_pose(k, Td - 0.02)
        # 被切的一瞬：站直、双手一扬——切面是水平的，站直时正好落在一层方块的缝上，是一个平整的面；朝向不用再拧
        base.update(face="tight", flash=0.0, tint=(1.0, 1.0, 1.0), ground=0.0, ring=0.0, dart=None,
                    z=0.0, lean=0.0, roll=0.0, spin=0.0, twist=0.0, sx=1.0, sy=1.0, sz=1.0, body_dz=0.0, hop=0.0,
                    head=(0.0, 0.0, 0.0), head_dz=0.0, hair=(0.0, 0.0), hand_a=(0.0, 0.36, 0.6), hand_b=(0.0, -0.36, 0.6),
                    foot_a=(0.0, 0.085, 0.04), foot_b=(0.0, -0.085, 0.04), foot_a_pitch=0.0, foot_b_pitch=0.0, scale=1.0)
        base.pop("flip", None)
        base.pop("tail", None)
        s = (-d[1], d[0])
        if s[1] < -1e-3 or (abs(s[1]) <= 1e-3 and k % 2):
            s = (d[1], -d[0])
        # 两截各自落下去的地方还有没有地：没有（坑、已塌或马上要塌）就让那截接着掉下去
        rp = self.rp
        px, py = rp.char_pos(k, Td)
        land = []
        for (ox, oy) in ((d[0] * SLICE_UP_GO, d[1] * SLICE_UP_GO), (s[0] * SLICE_LOW_GO, s[1] * SLICE_LOW_GO)):
            cell, w = rp.cell_at(px + ox, py + oy)
            land.append(cell != CELL_PIT and not (w >= 0 and rp.wave_steps[w] <= Td + 7))
        return dict(Td=Td, base=base, pos=(px, py), d=d, s=s, tilt=SLICE_TILT, land=tuple(land))

    def slice_spots(self, k):
        """两截最后停下的地面位置（地图坐标）：[(x, y, 有没有地)]，上半截在前。"""
        si = self.slice_info(k)
        px, py = si["pos"]
        d, s = si["d"], si["s"]
        return [(px + d[0] * SLICE_UP_GO, py + d[1] * SLICE_UP_GO, si["land"][0]),
                (px + s[0] * SLICE_LOW_GO * 0.6, py + s[1] * SLICE_LOW_GO * 0.6, si["land"][1])]

    def _dead_pose(self, k, gt, death):
        rp = self.rp
        Td, cause, d, with_ground = self.death_info(k)
        u = gt - Td
        px, py = rp.char_pos(k, Td)
        fl = math.sin(u * 2.2)
        p = dict(x=px, y=py, z=0.0, heading=self.heading(k, Td), lean=0.0, roll=0.0, spin=0.0, sx=1.0, sy=1.0, sz=1.0,
                 body_dz=0.0, twist=0.0, head=(0.2, 0.0, 0.1 * fl), head_dz=0.0,
                 hand_a=(0.0, 0.32, 0.68 + 0.08 * fl), hand_b=(0.0, -0.32, 0.68 - 0.08 * fl),
                 foot_a=(0.1 * fl, 0.09, 0.08), foot_b=(-0.1 * fl, -0.09, 0.08), hair=(0.9, 0.2 * fl), face="tight",
                 tint=(1.0, 1.0, 1.0), flash=clamp(1 - (u - 0.65) / 0.7), ground=0.0, ring=0.0, visible=True, dart=None)
        if k in self.sliced:
            # 被镖切成两半：两半各自怎么动由装配那边按 slice_curve 摆，这里只给时间和接触影
            if u > SLICE_LIFE:
                p["visible"] = False
                return p
            p["slice"] = dict(u=u)
            p["ground"] = clamp(1 - (u - SLICE_POOF) / 2.5) * 0.6
            p["ring"] = 0.0
            p["scale"] = 1.0
            return p
        if cause in (1, 2):
            # 被打飞：沿受击方向抛出去，翻着跟头，半秒后"噗"地消失
            if u > 10.5:
                p["visible"] = False
                return p
            dist = 2.7 * (1 - math.exp(-u / 4.5))
            p["x"] = px + d[0] * dist
            p["y"] = py + d[1] * dist
            p["z"] = max(0.0, 0.40 * u - 0.026 * u * u)
            p["heading"] = ang_of(-d[0], -d[1])
            p["flip"] = -0.62 * u
            p["scale"] = 1 - ease((u - 7.5) / 3.0)
            sq = math.exp(-u / 0.9)
            p["sz"], p["sx"] = 1 - 0.2 * sq, 1 + 0.2 * sq
            p["ground"] = clamp(1 - u / 9.0) * 0.8
            p["gx"], p["gy"] = p["x"], p["y"]
            p["ring"] = 0.0
        else:
            # 落坑（11-16 细化）：三种分开演——
            #   walk  自己走下去：在坑沿上失去平衡，前后晃、两手抡圈、一只脚悬空，晃不回来才往前栽下去；
            #   kick  被踢下去（滑行结束停在坑上）：带着滑行的劲儿直接仰面往后翻下去，没有坑沿上的挣扎；
            #   ground 随塌陷：脚下地块先裂开下沉（env 那边），人吓得一缩、双手举起，跟着地块一起掉。
            # 下坠时翻着跟头越来越小，没入云海时翻起一团云雾（fx "splash"）。
            kind = self.fall_kind(k)
            hang = FALL_HANG[kind]
            f = max(0.0, u - hang)
            z = -0.5 * FALL_G * f * f - (0.05 * f if kind == "ground" else 0.0)
            lz = self.liquid_z(k)
            if lz is not None and z < lz:
                # 掉进液面：没过去以后再沉 0.6 米就看不见了（液面本身挡着）
                ts = self.fall_splash_t(k)
                z = lz - 0.06 * max(0.0, u - ts)
                if z < lz - 0.6:
                    p["visible"] = False
                    return p
            if z < -13.5:
                p["visible"] = False
                return p
            drift = (2.2 if kind != "kick" else 3.2) * (1 - math.exp(-u / 3.0))
            p["x"] = px + d[0] * drift
            p["y"] = py + d[1] * drift
            p["flash"] = 0.0
            p["hair"] = (1.4, 0.2 * fl)
            p["scale"] = 1 - 0.55 * clamp(-z / 12.0)
            if kind == "walk" and u < hang:
                # 坑沿上：身体前后晃（越晃越大，最后一下栽出去），两手抡圈，后脚抬起来
                a = u / hang
                sw = math.sin(u * 3.4) * (0.25 + 0.35 * a)
                p["z"] = 0.02 * bump(a)
                p["lean"] = sw + 0.5 * a * a
                p["sz"], p["sx"] = 1.04, 0.97
                p["hand_a"] = (0.12 * math.cos(u * 4.2), 0.36, 0.62 + 0.24 * math.sin(u * 4.2))
                p["hand_b"] = (0.12 * math.cos(u * 4.2 + 2.6), -0.36, 0.62 + 0.24 * math.sin(u * 4.2 + 2.6))
                p["foot_a"] = (0.06, 0.085, 0.04)
                p["foot_b"] = (-0.16 - 0.06 * a, -0.085, 0.14 + 0.05 * math.sin(u * 3.4))
                p["head"] = (0.25 * sw, 0.0, 0.08 * fl)
                p["hair"] = (0.3 - sw, 0.2 * fl)
            else:
                p["z"] = z
                p["sz"], p["sx"] = 1.06, 0.94
                p["roll"] = 0.25 * fl
                p["hand_a"] = (0.0, 0.3, 0.8 + 0.08 * fl)
                p["hand_b"] = (0.0, -0.3, 0.8 - 0.08 * fl)
                if kind == "walk":            # 往前栽：脸朝下翻跟头
                    p["lean"] = 0.5
                    p["flip"] = 0.5 + 0.2 * f + 0.004 * f * f
                    p["spin"] = 0.05 * f
                elif kind == "kick":          # 仰面往后翻，手脚乱蹬
                    p["lean"] = -0.75
                    p["flip"] = -(0.15 * u + 0.004 * u * u)
                    p["foot_a"] = (0.18, 0.09, 0.16 + 0.05 * fl)
                    p["foot_b"] = (0.14, -0.09, 0.1 - 0.05 * fl)
                    p["head"] = (0.3, 0.0, 0.1 * fl)
                else:                         # 随地块：先一缩（吓一跳），再双手举高跟着掉
                    q = math.exp(-u / 1.6)
                    p["sz"], p["sx"] = 1.06 - 0.2 * q, 0.94 + 0.12 * q
                    p["lean"] = -0.1 - 0.02 * f
                    p["spin"] = 0.08 * f
                    p["hand_a"] = (0.05, 0.26, 0.9 + 0.06 * fl)
                    p["hand_b"] = (0.05, -0.26, 0.9 - 0.06 * fl)
        return p

    def fall_kind(self, k):
        """落坑的三种：walk 自己走下去 / kick 被踢下去 / ground 随塌陷。"""
        _, cause, killer = self.deaths[k]
        _, _, _, wg = self.death_info(k)
        if wg:
            return "ground"
        return "kick" if killer >= 0 else "walk"

    def liquid_z(self, k):
        """这次落坑掉进的是不是液面（岩浆湖、水道）：液面高度或 None。按出局时的位置查。"""
        if not hasattr(self, "_lz"):
            self._lz = {}
        if k not in self._lz:
            t, cause, killer = self.deaths[k]
            px, py = self.rp.char_pos(k, t + 1.0)
            self._lz[k] = liquid.z_at(self.rp, px, py) if cause == 3 else None
        return self._lz[k]

    def fall_splash_t(self, k):
        """没入云海（z 到 FALL_SPLASH_Z）或液面的时刻（相对出局时刻 Td 的步数）。"""
        kind = self.fall_kind(k)
        g, v = FALL_G, (0.05 if kind == "ground" else 0.0)
        lz = self.liquid_z(k)
        z = -FALL_SPLASH_Z if lz is None else max(0.05, -lz)
        f = (-v + math.sqrt(v * v + 2 * g * z)) / g
        return FALL_HANG[kind] + f

    # ------------------------------------------------------------ 镖
    def dart_state(self, k, gt):
        """返回 None（不画：在手里由角色负责，或已消失）或 dict(mode, x, y, z, spin, tilt, st, scale, glow)。"""
        rp = self.rp
        if gt < 0:
            return None
        i = min(int(gt), rp.T)
        a = gt - i if i < rp.T else 0.0
        d0 = rp.states[i]["darts"][k]
        d1 = rp.states[min(i + 1, rp.T)]["darts"][k]
        st = d0["st"]
        if st in (D_HAND, D_GONE):
            return None
        x0, y0 = d0["x"] / 1000.0, d0["y"] / 1000.0
        x1, y1 = d1["x"] / 1000.0, d1["y"] / 1000.0
        spin = 2.15 * gt + k * 1.3
        if st in FLYING:
            z = FLY_Z
            sc = 1.0
            hang = 0.0
            if st == D_BACK and d0["age"] < self.cfg.get("DART_HANG", 3) + 1:
                hang = bump((d0["age"] + a) / (self.cfg.get("DART_HANG", 3) + 0.6))
                z += 0.22 * hang
            if d1["st"] == D_HAND:                     # 这一步被接住：飞进手里
                ox, oy = rp.char_pos(k, i + 1.0)
                x1, y1 = ox, oy
                z = lerp(FLY_Z, 0.5, a)
            elif d1["st"] in (D_GROUND, D_RISE):       # 这一步落地（撞墙 / 撞镖）
                z = lerp(FLY_Z, 0.42, a)
            elif d1["st"] == D_GONE:
                x1, y1 = x0, y0
                sc = 1 - a
            dx, dy = x1 - x0, y1 - y0
            return dict(mode="fly", x=x0 + dx * a, y=y0 + dy * a, z=z, spin=spin, tilt=0.0, st=st, scale=sc,
                        glow=0.0, hang=hang)
        if st == D_GROUND:
            tl = max([t for t in self._land[k] if t <= i] or [-99])
            u = gt - tl
            z = 0.045
            tilt = 0.0
            if u < 9:                                   # 落地弹两下
                e = math.exp(-u / 3.2)
                z += 0.38 * abs(math.cos(u * 0.85)) * e
                tilt = 0.5 * math.sin(u * 1.4) * e
                spin = 2.15 * tl + k * 1.3 + 6.0 * (1 - math.exp(-u / 3.0))
            else:
                spin = 2.15 * tl + k * 1.3 + 6.0
            return dict(mode="ground", x=x0, y=y0, z=z, spin=spin, tilt=tilt, st=st, scale=1.0, glow=0.0, hang=0.0)
        if st == D_RISE:
            n = float(self.cfg.get("RECALL_PRE", 12))
            pr = clamp(1 - (d0["left"] - a) / n)
            tl = max([t for t in self._land[k] if t <= i] or [0])
            z = 0.06 + 0.34 * pr * pr + 0.03 * math.sin(gt * 3.0)
            x, y = x0, y0
            if d1["st"] == D_CALL:
                x, y = x0 + (x1 - x0) * a, y0 + (y1 - y0) * a
                z = lerp(z, FLY_Z, a)
            return dict(mode="rise", x=x, y=y, z=z, spin=2.15 * tl + k * 1.3 + 6.0 + 10.0 * pr * pr,
                        tilt=0.22 * math.sin(gt * 2.7) * (1 - pr), st=st, scale=1.0, glow=0.35 + 0.35 * math.sin(gt * 2.4) ** 2,
                        hang=0.0)
        return None

    def trail(self, k, gt, n=16, dt=0.42):
        """镖最近几步的轨迹点（从新到旧）：[(x, y, z, st)]，只取连续在飞的一段。"""
        pts = []
        for j in range(n + 1):
            t = gt - j * dt
            if t < 0:
                break
            d = self.dart_state(k, t)
            if d is None or d["mode"] != "fly":
                break
            pts.append((d["x"], d["y"], d["z"], d["st"]))
        return pts

    def dash_trail(self, k, gt, n=10, dt=0.4):
        """冲刺时身后的气流带：最近的位置点（只在冲刺中和刚停下的一小会儿有）。返回 (点, 强度)。"""
        A = self.L(k, gt)
        if A is not self:
            return A.dash_trail(k, gt, n, dt)
        for a in self._acts[k]["dash"]:
            if a["T0"] < gt < a["T1"] + 3.0:
                rp = self.rp
                if gt >= self.deaths.get(k, (1e9,))[0] + 1:
                    return [], 0.0
                if self.life > 0 and a["T0"] < rp.life_span(k, self.life)[0]:
                    return [], 0.0
                pts = []
                for j in range(n + 1):
                    t = max(a["T0"], gt - j * dt)
                    x, y = rp.char_pos(k, min(t, a["T1"] + 0.0) if gt > a["T1"] else t)
                    pts.append((x, y, 0.45, 0))
                    if t <= a["T0"]:
                        break
                return pts, 1 - ease((gt - a["T1"]) / 3.0)
        return [], 0.0

    # ------------------------------------------------------------ 特效
    def _build_fx(self):
        """固定时刻触发的特效：[(时刻, 类型, 参数)]。类型见 fx.py。"""
        rp = self.rp
        out = []
        for k in range(self.n if self.life == 0 else 0):
            ev = rp.ev[k]
            out.append((self.t_land[k], "land", dict(pos=rp.char_pos(k, 0), seat=k)))
            for (t, x, y) in ev["turn"]:
                out.append((t + 1.0, "turn", dict(pos=(x, y), seat=k)))
            for (t,) in ev["catch"]:
                out.append((t + 1.0, "catch", dict(pos=rp.char_pos(k, t + 1.0), seat=k)))
            for (t,) in ev["pick"]:
                out.append((t + 1.0, "catch", dict(pos=rp.char_pos(k, t + 1.0), seat=k)))
            for (t, x, y) in ev["drop"]:
                out.append((t + 1.0, "drop", dict(pos=(x, y), seat=k)))
            for (t, x, y) in ev["lost"]:
                out.append((t + 1.0, "poof", dict(pos=(x, y), z=FLY_Z, seat=k, size=0.5)))
            for (t, ax, ay) in ev["dash"]:
                t = t + rp.cfg.get("DASH_PRE", 0) if hasattr(rp, "cfg") else t      # 尘土在真正冲出去的那一步起
                out.append((float(t), "dust", dict(pos=rp.char_pos(k, t), seat=k)))
            for (t,) in ev["stop"]:
                out.append((t + 1.0, "dust", dict(pos=rp.char_pos(k, t + 1.0), seat=k, size=0.7)))
            for (t, x, y) in ev["call"]:
                out.append((t + 1.0, "turn", dict(pos=(x, y), seat=k)))
            for a in self._acts[k]["slash"]:
                if a["T"] is not None:
                    out.append((a["T"] - 0.5, "slash", dict(pos=rp.char_pos(k, a["T"]), dir=a["dir"], seat=k)))
            for a in self._acts[k]["kick"]:
                if a["T"] is not None:
                    out.append((a["T"] - 0.3, "kick", dict(pos=rp.char_pos(k, a["T"]), dir=a["dir"], seat=k, hit=a["hit"])))
        if self.life > 0:
            for k in range(self.n):
                if self.life <= len(rp.respawns[k]):
                    a0, _ = rp.life_span(k, self.life)
                    out.append((a0, "respawn", dict(pos=rp.char_pos(k, a0), seat=k, team=self.teams[k])))
        for (t, x, y, a, b) in (rp.clashes if self.life == 0 else ()):
            out.append((t + 1.0, "clash", dict(pos=(x, y), seats=(a, b))))
        for (t, x, y, a, b) in (getattr(rp, "blocks", []) if self.life == 0 else ()):      # 挥砍打落飞镖：和镖撞镖用同一个火花
            out.append((t + 1.0, "clash", dict(pos=(x, y), seats=(a, b))))
        for (t, i, old, new) in (getattr(rp, "captures", []) if self.life == 0 else ()):
            nm, px, py, r = rp.points[i]
            out.append((t + 1.0, "capture", dict(pos=(px, py), seat=0, team=new, r=r)))
        kl_t = {(t, k) for (t, k, left, by, why) in getattr(rp, "lifeloss", [])}
        for (t, k, left, by, why) in (getattr(rp, "lifeloss", []) if self.life == 0 else ()):
            out.append((t + 1.0, "kinghit", dict(pos=rp.char_pos(k, t + 1.0), seat=by if by >= 0 else k, victim=k,
                                                 left=left, team=self.teams[k])))
        for (t, killer, victim, how, dst) in (rp.hits if self.life == 0 else ()):
            if (t, victim) in kl_t:
                out.append((t + 1.0, "hit", dict(pos=rp.char_pos(victim, t + 1.0), seat=killer, victim=victim, how=how,
                                                 sliced=True)))
                continue
            out.append((t + 1.0, "hit", dict(pos=rp.char_pos(victim, t + 1.0), seat=killer, victim=victim, how=how,
                                             sliced=victim in self.L(victim, t + 0.5).sliced)))
        for (t, kicker, victim) in (rp.kickhits if self.life == 0 else ()):
            out.append((t + 1.0, "kickhit", dict(pos=rp.char_pos(victim, t + 1.0), seat=kicker, victim=victim)))
        for k, (t, cause, killer) in self.deaths.items():
            Td, cause, d, wg = self.death_info(k)
            if k in self.sliced:
                si = self.slice_info(k)
                px, py = si["pos"]
                out.append((Td, "slice", dict(pos=(px, py), dir=d, side=si["s"], seat=killer if killer >= 0 else k, victim=k)))
                for (qx, qy, ok) in self.slice_spots(k):
                    if not ok:
                        continue
                    out.append((Td + SLICE_POOF, "poof", dict(pos=(qx, qy), z=0.25, seat=k, size=0.7)))
            elif cause in (1, 2):
                pp = self._dead_pose(k, Td + 8.6, self.deaths[k])
                out.append((Td + 8.6, "poof", dict(pos=(pp["x"], pp["y"]), z=pp["z"] + 0.5, seat=k, size=1.0)))
            else:
                out.append((Td + (1.2 if self.fall_kind(k) == "walk" else 0.0), "fallcry",
                            dict(pos=rp.char_pos(k, Td), seat=k)))
                ts = self.fall_splash_t(k)
                pp = self._dead_pose(k, Td + ts, self.deaths[k])
                lz = self.liquid_z(k)
                out.append((Td + ts, "splash", dict(pos=(pp["x"], pp["y"]), z=FALL_SPLASH_Z if lz is None else lz, seat=k,
                                                    liquid=liquid.kind(rp) if lz is not None else None)))
        if self.winners and self.life == 0:
            for k in self.winners:
                out.append((rp.T + 5.0, "confetti", dict(pos=rp.char_pos(k, rp.T), seat=k)))
        out.sort(key=lambda e: e[0])
        return out

    def continuous_fx(self, gt):
        """随状态持续的特效：被踢时头顶转的星星、召回时的光圈。[(类型, 参数)]"""
        rp = self.rp
        out = []
        for k in range(self.n):
            A = self.L(k, gt)
            dth = A.deaths.get(k)
            if dth is not None and gt >= dth[0] + 1:
                continue
            if A.life > 0 and gt < rp.life_span(k, A.life)[0] + 1:
                continue
            for a in self._acts[k]["stun"]:
                if a["T0"] + 1 < gt < a["T1"] + 1:
                    out.append(("stun", dict(pos=rp.char_pos(k, gt), seat=k, age=gt - a["T0"])))
            for a in self._acts[k]["recall"]:
                if a["T0"] < gt < a["T1"]:
                    out.append(("recall", dict(pos=rp.char_pos(k, gt), seat=k, age=gt - a["T0"], dart=a["dart"],
                                               prog=(gt - a["T0"]) / max(1.0, a["T1"] - a["T0"]))))
        return out
