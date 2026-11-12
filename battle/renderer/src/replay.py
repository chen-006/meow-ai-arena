# -*- coding: utf-8 -*-
"""读引擎的回放（.wrp，格式见 引擎\\回放格式.md），整理成渲染要的样子（纯 Python + numpy，不依赖 Blender）。

  rp = load(路径)
  rp.meta / rp.events / rp.result / rp.scenario（关卡原文已 parse）
  rp.n 帧数；rp.steps[i] 第 i 帧是第几步；rp.secs(i) 第 i 帧是第几秒
  rp.ids 全部物体编号；rp.info[id] = 开场信息（单位、道具）或飞行物（箭、石头）的种类
  rp.P[i, k] 位置 (x, y, z)，rp.Q[i, k] 四元数 (x, y, z, w)，rp.S 状态，rp.F 标志，rp.H 血，rp.A aux，rp.T 阵营，
  rp.present[i, k] 这一帧在不在场上；k = rp.col[id]
  rp.walk_phase[i, k] 走路的步相位（按走过的路程算）
  rp.strike_age[i, k] 离上一次出手落下过了几秒（没有 = 大数）
  rp.hurt_age[i, k] 离上一次"受击闪亮"过了几秒（掉血的那一帧算一次；同一个人 0.4 秒内只闪一次，混战里不满屏乱闪）
  rp.mud[i, k] 这一帧"脚上带泥"（在泥塘里、或出泥塘不到 stick_secs 秒；只对带 stick_secs 的泥塘，见 _mud）
  rp.n 回放的帧数；rp.nx 画面的帧数（现在和 rp.n 相同；以前坠崖、主帅出门两种结局要补帧，关卡改了以后都没了）
  camera(rp) -> 每帧的 (目标 x, z, y, 宽度)：跟着场上的仗走、平滑过的机位；director(rp) 再叠上近景和手动机位
  captions(rp) -> 比分牌旁边短暂出的一行小字（守军出击、山口集结的敌军出发）
帧率：引擎每 2 步记一帧 = 30 帧每秒，成片也是 30 帧，一帧对一帧。
"""
import json
import struct

import numpy as np

REC = struct.Struct('<IBBBBHHfffhhhh')
OT = {1: 'unit', 2: 'stone', 3: 'prop', 4: 'arrow'}
PROP_KIND = {0: 'snowball', 1: 'keg', 2: 'flag', 3: 'barrier', 4: 'gate', 5: 'pillar'}
STRIDE = {"cavalry": 2.6, "boss": 2.4, "giant": 2.4, "lord": 1.0}
HURT_GAP = 0.4                  # 同一个人两次受击闪亮至少隔几秒


class Replay:
    pass


def load(path):
    data = open(path, 'rb').read()
    if not data.startswith(b'WARREPLAY1\n'):
        raise ValueError('不是 war 回放：' + path)
    pos = 11
    meta, raw_frames, events, result = None, [], [], None
    while pos + 8 <= len(data):
        tag = data[pos:pos + 4].decode('ascii')
        n = struct.unpack_from('<I', data, pos + 4)[0]
        body = data[pos + 8:pos + 8 + n]
        pos += 8 + n
        if tag == 'META':
            meta = json.loads(body.decode('utf-8'))
        elif tag == 'FRAM':
            step, cnt = struct.unpack_from('<II', body, 0)
            arr = np.frombuffer(body, dtype=np.uint8, offset=8, count=32 * cnt).reshape(cnt, 32)
            raw_frames.append((step, arr))
        elif tag == 'EVNT':
            events.extend(json.loads(body.decode('utf-8')))
        elif tag == 'RSLT':
            result = json.loads(body.decode('utf-8'))
        elif tag == 'ENDR':
            break
    rp = Replay()
    rp.path = path
    rp.meta, rp.events, rp.result = meta, events, result
    rp.scenario = json.loads(meta['scenario'])
    rp.dt = meta['dt']
    rp.rec_every = meta['rec_every']
    rp.steps = np.array([s for s, _ in raw_frames])
    rp.n = len(raw_frames)
    # 所有出现过的物体
    dt_rec = np.dtype([('id', '<u4'), ('ot', 'u1'), ('st', 'u1'), ('fl', 'u1'), ('tm', 'u1'), ('hp', '<u2'), ('aux', '<u2'),
                       ('p', '<f4', 3), ('q', '<i2', 4)])
    recs = [np.frombuffer(a.tobytes(), dtype=dt_rec) for _, a in raw_frames]
    ids = sorted(set(int(i) for r in recs for i in r['id']))
    rp.ids = ids
    rp.col = {i: k for k, i in enumerate(ids)}
    N, K = rp.n, len(ids)
    rp.P = np.zeros((N, K, 3), np.float32)
    rp.Q = np.zeros((N, K, 4), np.float32)
    rp.Q[..., 3] = 1
    rp.S = np.zeros((N, K), np.uint8)
    rp.F = np.zeros((N, K), np.uint8)
    rp.H = np.zeros((N, K), np.uint16)
    rp.A = np.zeros((N, K), np.uint16)
    rp.T = np.full((N, K), 255, np.uint8)
    rp.OT = np.zeros(K, np.uint8)
    rp.present = np.zeros((N, K), bool)
    for i, r in enumerate(recs):
        cols = np.array([rp.col[int(v)] for v in r['id']], dtype=int)
        if not len(cols):
            continue
        rp.P[i, cols] = r['p']
        rp.Q[i, cols] = r['q'].astype(np.float32) / 32767.0
        rp.S[i, cols] = r['st']
        rp.F[i, cols] = r['fl']
        rp.H[i, cols] = r['hp']
        rp.A[i, cols] = r['aux']
        rp.T[i, cols] = r['tm']
        rp.OT[cols] = r['ot']
        rp.present[i, cols] = True
    # 物体信息
    rp.info = {}
    for o in meta['objects']:
        rp.info[o['id']] = dict(o)
    for i in ids:
        if i not in rp.info:
            k = rp.col[i]
            ot = OT.get(int(rp.OT[k]), 'unit')
            first = int(np.argmax(rp.present[:, k]))
            kind = ot if ot in ('arrow', 'stone') else ('unit' if ot == 'unit' else PROP_KIND.get(int(rp.A[first, k]), 'keg'))
            rp.info[i] = dict(id=i, type=ot, kind=kind, team=int(rp.T[first, k]) if rp.T[first, k] != 255 else -1)
    _derive(rp)
    return rp


def step_of(rp, i):
    return int(rp.steps[i])


def secs(rp, i):
    return rp.steps[i] * rp.dt


def _derive(rp):
    N, K = rp.n, len(rp.ids)
    fps_dt = rp.dt * rp.rec_every
    # 走路相位：按水平走过的路程
    d = np.zeros((N, K), np.float32)
    if N > 1:
        step = np.linalg.norm(np.diff(rp.P[:, :, [0, 2]], axis=0), axis=2)
        step[~(rp.present[1:] & rp.present[:-1])] = 0
        step[step > 3.0] = 0
        d[1:] = np.cumsum(step, axis=0)
    stride = np.array([STRIDE.get(rp.info[i].get('kind'), 1.25) for i in rp.ids], np.float32)
    rp.walk_phase = (d / stride[None, :]) % 1.0
    rp.speed = np.zeros((N, K), np.float32)
    if N > 1:
        rp.speed[1:] = np.linalg.norm(np.diff(rp.P[:, :, [0, 2]], axis=0), axis=2) / fps_dt
        rp.speed[rp.speed > 40] = 0
    # 出手落下以后的时间
    rp.strike_age = np.full((N, K), 99.0, np.float32)
    frame_of_step = lambda s: int(np.clip(np.searchsorted(rp.steps, s), 0, N - 1))
    hits = {}
    for e in rp.events:
        if e['t'] in ('strike', 'shoot', 'launch') and e.get('u', -1) in rp.col:
            hits.setdefault(rp.col[e['u']], []).append(frame_of_step(e['s']))
    for k, fl in hits.items():
        last = -10 ** 9
        fl = sorted(fl)
        j = 0
        for i in range(N):
            while j < len(fl) and fl[j] <= i:
                last = fl[j]
                j += 1
            if last > -10 ** 8:
                rp.strike_age[i, k] = (i - last) * fps_dt
    # 受击：活着的单位血量比上一帧少 = 挨了一下
    rp.hurt_age = np.full((N, K), 99.0, np.float32)
    if N > 1:
        isu = np.array([rp.info[i]['type'] == 'unit' for i in rp.ids])
        drop = (rp.H[1:] < rp.H[:-1]) & rp.present[1:] & rp.present[:-1] & (rp.S[1:] < 3) & isu[None, :]
        gap = int(round(HURT_GAP / fps_dt))
        for k in np.where(drop.any(axis=0))[0]:
            last = -10 ** 9
            for i in np.where(drop[:, k])[0] + 1:
                if i - last < gap:
                    continue
                last = i
                e = min(N, i + gap + 4)
                rp.hurt_age[i:e, k] = np.minimum(rp.hurt_age[i:e, k], (np.arange(i, e) - i) * fps_dt)
    # 镇山兽状态：0 游走 1 刨地 2 冲撞 3 晕
    rp.boss_state = np.zeros((N, K), np.uint8)
    bs = {}
    for e in rp.events:
        if e['t'] == 'boss_state' and e.get('u') in rp.col:
            bs.setdefault(rp.col[e['u']], []).append((frame_of_step(e['s']), e['state']))
    for k, lst in bs.items():
        cur, j = 0, 0
        lst.sort()
        for i in range(N):
            while j < len(lst) and lst[j][0] <= i:
                cur = lst[j][1]
                j += 1
            rp.boss_state[i, k] = cur
    # 事件按帧
    rp.ev_frame = [frame_of_step(e['s']) for e in rp.events]
    _mud(rp)
    # 画面帧数：以前坠崖、主帅走出出口两种结局要往后补几十帧，关卡改成全歼以后这两种结局都没了，画面 = 回放
    rp.nx, rp.outro = N, None


# ================================================================ 脚上带泥（第 1 关的泥塘）
def _mud(rp):
    """rp.mud[i, k]：这一帧这个单位脚上带着泥（引擎里跑不快）。回放没有专门的字段，按 回放格式.md 的推法：
    身体中心在泥塘里的那几帧，加上最后一次在泥塘里之后 stick_secs 秒以内。只看带 stick_secs 的泥塘。"""
    N, K = rp.n, len(rp.ids)
    rp.mud = np.zeros((N, K), bool)
    rp.in_mud = np.zeros((N, K), bool)            # 身体中心正在泥塘里（溅泥水用）
    zones =[z for z in (rp.scenario.get("zones") or []) if z.get("stick_secs", 0) > 0 and z.get("speed_mul", 1.0) < 1.0]
    if not zones or N == 0:
        return
    fdt = rp.dt * rp.rec_every
    isu = np.array([rp.info[i]['type'] == 'unit' for i in rp.ids])
    x, z = rp.P[:, :, 0], rp.P[:, :, 2]
    for zn in zones:
        if "rect" in zn:
            x0, z0, x1, z1 = zn["rect"]
            inside = (x >= x0) & (x <= x1) & (z >= z0) & (z <= z1)
        elif "circle" in zn:
            cx, cz, r = zn["circle"]
            inside = (x - cx) ** 2 + (z - cz) ** 2 <= r * r
        elif "ellipse" in zn:
            cx, cz, rx, rz = zn["ellipse"]
            inside = ((x - cx) / rx) ** 2 + ((z - cz) / rz) ** 2 <= 1.0
        else:
            continue
        inside &= rp.present & isu[None, :]
        rp.in_mud |= inside
        stick =int(round(zn["stick_secs"] / fdt))
        last = np.where(inside, np.arange(N)[:, None], -10 ** 6)
        last = np.maximum.accumulate(last, axis=0)
        rp.mud |= (np.arange(N)[:, None] - last <= stick) & rp.present


def unit_cols(rp):
    return [rp.col[i] for i in rp.ids if rp.info[i]['type'] == 'unit']


# ================================================================ 机位
# 约定：每帧机位 = (目标 x, 目标 z, 目标高度 y, 画面宽度 米, 俯角 度, 方位 度)。方位 0 = 从南往北看（甲方 / 闯关方在下面），
# 正值 = 镜头绕到西南、往东北看。宽度 = 目标点所在深度上画面横向装下多少米（50 毫米镜头）。
FOV_T = 18.0 / 50.0                       # tan(水平视角的一半)，和 stage.fit_dist 一致
# 画面里留给战场的范围（信息栏在上面，占掉一条）：横向用中间 86%，往上到 62%、往下到 86%（以半幅为 1）
SAFE = dict(mx=0.86, top=0.62, bot=0.86)


def _basis(pitch, yaw):
    """Blender 坐标（X 东、Y 北、Z 上）里镜头的：前、右、上、水平的前。"""
    p, yw = np.radians(pitch), np.radians(yaw)
    f = np.array([np.sin(yw) * np.cos(p), np.cos(yw) * np.cos(p), -np.sin(p)])
    r = np.array([np.cos(yw), -np.sin(yw), 0.0])
    u = np.array([np.sin(yw) * np.sin(p), np.cos(yw) * np.sin(p), np.cos(p)])
    g = np.array([np.sin(yw), np.cos(yw), 0.0])
    return f, r, u, g


def _wq(x, w, q):
    """加权分位数。"""
    o = np.argsort(x)
    cw = np.cumsum(w[o])
    return float(x[o][min(np.searchsorted(cw, q * cw[-1]), len(x) - 1)])


def _gauss(a, sig, lead=0):
    """沿时间（第 0 维）做高斯平滑；lead > 0 = 往前看 lead 帧（镜头先一步到）。"""
    if sig < 0.5 or len(a) < 2:
        return a
    kn = int(sig * 3)
    ker = np.exp(-0.5 * (np.arange(-kn, kn + 1) / sig) ** 2)
    ker /= ker.sum()
    a2 = a.reshape(len(a), -1)
    pad = np.concatenate([np.repeat(a2[:1], kn, 0), a2, np.repeat(a2[-1:], kn + lead, 0)])
    out = np.stack([np.convolve(pad[:, j], ker, mode='valid')[lead:lead + len(a)] for j in range(a2.shape[1])], axis=1)
    return out.reshape(a.shape)


def _interest(rp):
    """每帧"值得看的点"：返回函数 f(i) -> (点 (M,3) Blender 坐标, 权重 (M,), 高度 (M,))。
    权重：主帅、镇山兽 5；爆炸 4（持续 1.5 秒）；滚动的雪球 4；在飞的石头 3；冲锋中的兵 2；刚出手、挨打、倒下的兵 +1.5；
    在走的兵 1；站着的兵 0.3；军旗 1.5。阵亡的、掉到场地以下的不算。"""
    N = rp.nx
    fps = 1.0 / (rp.dt * rp.rec_every)
    K = len(rp.ids)
    kinds = [rp.info[i].get('kind') for i in rp.ids]
    is_unit = np.array([rp.info[i]['type'] == 'unit' for i in rp.ids])
    vip = np.array([kd in ('lord', 'boss') for kd in kinds])
    is_ball = np.array([kd == 'snowball' for kd in kinds])
    is_flag = np.array([kd == 'flag' for kd in kinds])
    is_stone = rp.OT == 2
    HG = {'boss': 3.6, 'giant': 5.0, 'cavalry': 2.8, 'catapult': 3.0, 'lord': 2.4, 'snowball': 1.5, 'flag': 4.2}
    hgt = np.array([HG.get(kd, 1.9) for kd in kinds])
    hot = np.zeros((N + 60, K), np.float32)             # 事件给相关的兵加的"热度"
    booms = []
    back, fwd = int(1.0 * fps), int(0.5 * fps)
    for e, f in zip(rp.events, rp.ev_frame):
        t = e['t']
        if t == 'explode' and 'p' in e:
            booms.append((f, e['p']))
        elif t in ('hit', 'strike', 'brace', 'death', 'knock', 'snowball_hit', 'stone_hit', 'shoot', 'launch'):
            wv = 0.6 if t in ('shoot', 'launch') else 1.5
            for key in ('v', 'u', 'a'):
                uid = e.get(key, -1)
                if isinstance(uid, int) and uid in rp.col:
                    k = rp.col[uid]
                    a, b = max(0, f - fwd), min(N, f + back)
                    hot[a:b, k] = np.maximum(hot[a:b, k], wv)

    def f(i):
        pres = rp.present[i]
        low = rp.P[i, :, 1] < -3.0
        alive = pres & is_unit & (rp.S[i] < 3) & ~low
        w = np.zeros(K)
        w[alive] = 0.3
        w[alive & (rp.speed[i] > 0.8)] = 1.0
        w[alive & ((rp.F[i] & 32) > 0)] = 2.0
        w[alive] += hot[i, alive]
        w[alive & vip] = 5.0
        w[pres & is_ball & (rp.S[i] == 1) & (rp.speed[i] > 0.5) & ~low] = 4.0
        w[alive & ~vip] = np.minimum(w[alive & ~vip], 4.0)
        w[pres & is_stone & (rp.S[i] == 0) & (rp.P[i, :, 1] > -1.0)] = 3.0        # 飞出场外、往下掉个不停的石头不算
        w[pres & is_flag] = 1.5
        m = w > 0
        pts = rp.P[i, m][:, [0, 2, 1]].astype(float)
        ws, hs = w[m], hgt[m]
        ex = [(p[0], p[2], p[1]) for (fb, p) in booms if 0 <= i - fb < 1.5 * fps]
        if ex:
            pts = np.concatenate([pts, np.array(ex, float)])
            ws = np.concatenate([ws, np.full(len(ex), 4.0)])
            hs = np.concatenate([hs, np.full(len(ex), 2.5)])
        return pts, ws, hs

    return f


def _fit(P, W, Hh, basis, sp, min_w, max_w, prev):
    """一帧的取景：先定焦点（装不下全部时，取"最热闹的一窗"，并优先留在上一帧看的地方），再按透视把窗里的点装进画面。
    返回 (目标点 Blender 坐标 (3,), 宽度, 焦点的地面坐标)。"""
    f, r, u, g = basis
    A, B = P @ r, P @ g
    ha = max_w * SAFE['mx'] / 2
    hb = max_w * 9 / 16 * (SAFE['top'] + SAFE['bot']) / 2 / sp / 2

    def shift(c):
        m = None
        for _ in range(6):
            m = ((A - c[0]) / ha) ** 2 + ((B - c[1]) / hb) ** 2 <= 1.0
            if not m.any():
                return c, m, 0.0
            c2 = np.array([np.average(A[m], weights=W[m]), np.average(B[m], weights=W[m])])
            if np.abs(c2 - c).max() < 0.05:
                break
            c = c2
        m = ((A - c[0]) / ha) ** 2 + ((B - c[1]) / hb) ** 2 <= 1.0
        return c, m, float(W[m].sum())

    cands = [shift(np.array([np.average(A, weights=W), np.average(B, weights=W)]))]
    j = int(np.argmax(W))
    cands.append(shift(np.array([A[j], B[j]])))
    best = max(cands, key=lambda t: t[2])
    if prev is not None:
        c2 = shift(np.array(prev))
        if c2[2] * 1.4 >= best[2] and c2[2] > 0:
            best = c2
    c2d, m, _ = best
    if m is None or not m.any():
        m = np.ones(len(P), bool)
    Ps, Ws, Hs = P[m], W[m], Hh[m]
    # 窗里有足够多"在动手、在走"的点时，只按它们取景：原地站着的兵是背景，不把画面撑大
    act = Ws >= 0.95
    if act.sum() >= 3 and act.sum() < len(Ws):
        Ps, Ws, Hs = Ps[act], Ws[act], Hs[act]
    c = r * c2d[0] + g * c2d[1]
    c[2] = _wq(Ps[:, 2], Ws, 0.5)
    w_ = float(np.clip(max_w * 0.7, min_w, max_w))
    vr = 9 / 16 * (SAFE['top'] + SAFE['bot']) / 2
    for _ in range(3):
        dist = w_ / (2 * FOV_T)
        rel = Ps - c
        sc = dist / np.maximum(dist + rel @ f, dist * 0.35)
        px = (rel @ r) * sc
        py0 = (rel @ u) * sc
        py1 = py0 + Hs * u[2] * sc
        x0, x1 = _wq(px, Ws, 0.03), _wq(px, Ws, 0.97)
        y0, y1 = _wq(py0, Ws, 0.03), _wq(py1, Ws, 0.97)
        w_new = float(np.clip(max((x1 - x0) / SAFE['mx'] + 8.0, (y1 - y0 + 5.0) / vr), min_w, max_w))
        want = (SAFE['top'] - SAFE['bot']) / 2 * 9 / 16 * w_new / 2
        c = c + r * (x0 + x1) / 2 + g * (((y0 + y1) / 2 - want) / sp)
        w_ = w_new
    # 场上有主角（主帅、镇山兽）时，画面中心往它那边偏三成，宽度相应放大一点（别人不出画）
    vip = Ws >= 5.0
    if vip.any():
        d = Ps[vip].mean(axis=0) - c
        d[2] = 0.0
        c = c + 0.3 * d
        w_ = float(np.clip(w_ + 0.6 * abs(d @ r) + 0.6 * abs(d @ g) * sp * 16 / 9, min_w, max_w))
    return c, w_, (float(c @ r), float(c @ g))


def camera(rp, pitch=40.0, min_w=24.0, max_w=52.0, yaw=0.0, smooth_s=0.8, lead_s=0.35, **_):
    """全景跟拍。每帧：(目标 x, 目标 z, 目标高度 y, 画面宽度 米)。
    镜头贴着"正在发生的事"取景（见 _interest 的权重）：能全装下就全装下，装不下就取最热闹的一窗（不会停在两群人中间的空地上），
    兵在画面里尽量大（宽度下限 min_w、上限 max_w）；再在时间上平滑，往前看 lead_s 秒。"""
    N = rp.nx
    fps = 1.0 / (rp.dt * rp.rec_every)
    yaws = np.broadcast_to(np.asarray(yaw, float), (N,))       # yaw 可以是每帧一个（绕着岛转的机位）
    sp = max(np.sin(np.radians(pitch)), 0.3)
    it = _interest(rp)
    raw = np.zeros((N, 4))
    prev_c, basis, by = None, None, None
    for i in range(N):
        P, W, Hh = it(i)
        if len(P) == 0:
            raw[i] = raw[i - 1] if i else (0, 0, 0, max_w)
            continue
        if by is None or abs(yaws[i] - by) > 1e-6:
            basis, by = _basis(pitch, yaws[i]), yaws[i]
        prev = None if prev_c is None else (float(prev_c @ basis[1]), float(prev_c @ basis[3]))
        c, w_, _ = _fit(P, W, Hh, basis, sp, min_w, max_w, prev)
        prev_c = c
        raw[i] = (c[0], c[1], c[2], w_)
    # 宽度：先取前后 0.4 秒里的最大（场面突然散开时不切人），再平滑
    kn = int(0.4 * fps)
    wpad = np.concatenate([np.repeat(raw[:1, 3], kn), raw[:, 3], np.repeat(raw[-1:, 3], kn)])
    wmax = np.array([wpad[i:i + 2 * kn + 1].max() for i in range(N)])
    lead = int(lead_s * fps)
    out = np.zeros_like(raw)
    out[:, :3] = _gauss(_gauss(raw[:, :3], smooth_s * fps * 0.75, lead), smooth_s * fps * 0.5)
    out[:, 3] = np.exp(_gauss(_gauss(np.log(wmax), smooth_s * fps, lead), smooth_s * fps * 0.6))
    return out


# ================================================================ 导演：全景 + 按事件设计的近景 + 手动机位
BIG = {"terrain_removed": "桥断", "barrier": "路障被撞穿", "gate": "门被撞开", "pillar": "立石碎了",
       "boss_stun": "镇山兽撞晕", "flag": "军旗倒了", "stone": "石头落地", "brace": "骑兵撞枪阵", "sortie": "守军出击",
       "rally_go": "山口敌军压过来", "lord": "主帅阵亡"}


def sortie_members(rp, f):
    """sortie 事件那一帧出击的守军（列号）：前一帧还是"原地守"（flags 8）、之后不是了的主办方单位。"""
    N = rp.n
    a, b = max(0, f - 2), min(N - 1, f + 3)
    out = []
    for k in range(len(rp.ids)):
        if rp.OT[k] != 1 or not rp.present[a, k] or rp.S[a, k] >= 3:
            continue
        if (rp.F[a, k] & 8) and not (rp.F[b, k] & 8) and rp.present[b, k]:
            out.append(k)
    return out


def rally_members(rp, f):
    """rally_go 那一帧出发的敌军（列号）：改过道（reroute）、这一帧还活着的。"""
    ks = sorted(set(rp.col[e["u"]] for e in rp.events if e["t"] == "reroute" and e.get("u") in rp.col))
    return [k for k in ks if rp.present[f, k] and rp.S[f, k] < 3]


def squad_text(squad):
    """队名 -> 出击提示的主句：坡顶 -> 坡顶守军出击；西车护卫 -> 西车护卫出击；墙头西 -> 墙头西段守军出击。"""
    s = str(squad or "")
    if not s:
        return "守军出击"
    if s.endswith(("护卫", "阵", "手", "军", "兵")):
        return s + "出击"
    if s.startswith("墙头") and len(s) == 3:
        return s + "段守军出击"
    return s + "守军出击"


def captions(rp):
    """比分牌旁边短暂出的一行小字：[(帧, 主句, 补充, 队伍号)]。
    守军出击（sortie）：哪一队、几个人；改道的敌军从集结点出发（rally_go）：几个人、为什么出发（人到齐了 / 等够了时间 /
    挨了够不着的远程攻击）。集结时挨打出发的那一下，引擎同时记 sortie，只出 rally_go 那一行。"""
    out = []
    rallies = [f for e, f in zip(rp.events, rp.ev_frame) if e["t"] == "rally_go"]
    for e, f in zip(rp.events, rp.ev_frame):
        t = e["t"]
        if t == "sortie":
            if any(abs(f - fr) <= 15 for fr in rallies):
                continue
            n = e.get("n") or len(sortie_members(rp, f))
            team = rp.info.get(e.get("u"), {}).get("team", 1)
            out.append((f, squad_text(e.get("squad")), "%d 人" % n if n else "", team if team is not None and team >= 0 else 1))
        elif t == "rally_go":
            n = e.get("n") or len(rally_members(rp, f))
            why = {"gathered": "人到齐了", "time": "等够了时间", "sortie": "挨了箭，不等了"}.get(e.get("why"), "")
            out.append((f, "山口敌军压过来", "%d 人%s" % (n, (" · " + why) if why else ""), 1))
    return out


def _smooth(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3 - 2 * x)


def _track(rp, k, lift=0.0):
    """物体 k 每帧的位置（引擎坐标，含补帧）；不在场的帧用最近一次在场的位置。"""
    N = rp.nx
    out = rp.P[:N, k].astype(float).copy()
    pres = rp.present[:N, k]
    idx = np.where(pres)[0]
    if len(idx) == 0:
        return out
    last = np.maximum.accumulate(np.where(pres, np.arange(N), -1))
    last[last < 0] = idx[0]
    out = out[last]
    out[:, 1] += lift
    return out


def _side_yaw(dx, dz, yaw0, lim=38.0):
    """让 (dx, dz) 这个运动方向在画面上横着走的方位：和它垂直的两个方向里取离默认方位近的，偏转不超过 lim 度。"""
    hd = np.degrees(np.arctan2(dx, dz))
    best = None
    for c in (hd + 90.0, hd - 90.0):
        d = (c - yaw0 + 180.0) % 360.0 - 180.0
        if best is None or abs(d) < abs(best):
            best = d
    return yaw0 + float(np.clip(best, -lim, lim))


def big_events(rp):
    """大事件：[(帧, 名字, (x, y, z), 跟拍的物体列号或 None)]（给外面查关键帧用；近景怎么拍见 auto_shots）。"""
    out = []
    for s in auto_shots(rp):
        out.append((int(s["f"]), s["name"], tuple(float(v) for v in s["pos"][min(int(s["f"]), rp.nx - 1)]), s.get("k")))
    return out


def auto_shots(rp, yaw0=0.0):
    """按事件设计的近景。每条：dict(f 事件帧, a 开始帧, b 结束帧, name, pos (NX,3) 每帧的目标点（引擎坐标）,
    w 宽度（数或 (NX,) 数组）, pitch, yaw, ein / eout 进出过渡秒数)。
      桥断        低机位顺着峡谷看，目标点跟着往下落：看见人跟着桥掉下去
      雪球撞路障   从松开后跟着球走，撞穿后再跟一段
      镇山兽撞晕   从刨地开始跟着它，镜头转到冲撞方向的侧面：冲过来、撞上、晕
      石头落地     对着第一块砸中人的石头的落点
      骑兵撞枪阵   第一波架枪迎击，低机位
      军旗倒 / 木栅、门被砸开   原地近景
      守军出击     带一下冲出来的那一队（2.6 秒，宽度装下整队）；正赶上别的近景就不另切（那个镜头里已经看得见）
      山口集结出发 切到集结点看整群人压出来，跟着走 3.5 秒"""
    N, NX = rp.n, rp.nx
    fps = 1.0 / (rp.dt * rp.rec_every)

    def S(sec):
        return int(round(sec * fps))

    def const(x, y, z):
        return np.repeat(np.array([[x, y, z]], float), NX, 0)

    shots = []
    terr = {}
    for p in rp.scenario.get("terrain", []):
        if p.get("tag"):
            if "min" in p:
                c = [(p["min"][i] + p["max"][i]) / 2 for i in range(3)]
                top = p["max"][1]
                along_x = (p["max"][0] - p["min"][0]) >= (p["max"][2] - p["min"][2])
            else:
                c = list(p.get("c", [0, 0, 0]))
                hh = p.get("h", [1, 0, 0])
                top = c[1] + hh[1]
                along_x = hh[0] >= hh[2]
            terr.setdefault(p["tag"], (c, top, along_x))
    kinds = [rp.info[i].get('kind') for i in rp.ids]
    bstate = {}                                   # 镇山兽：每次换状态的 (帧, 状态, dx, dz)
    for e, f in zip(rp.events, rp.ev_frame):
        if e["t"] == "boss_state" and e.get("u") in rp.col:
            bstate.setdefault(rp.col[e["u"]], []).append((f, e["state"], e.get("dx", 0.0), e.get("dz", 1.0)))
    seen_stone, seen_brace = [], False
    for e, f in zip(rp.events, rp.ev_frame):
        t = e["t"]
        if t == "terrain_removed" and e.get("tag") in terr:
            c, top, along_x = terr[e["tag"]]
            pos = const(c[0], top - 2.0, c[2])
            age = np.clip((np.arange(NX) - f) / fps, 0, 2.2)
            pos[:, 1] -= 3.2 * _smooth(age / 2.2)
            shots.append(dict(f=f, a=f - S(1.8), b=f + S(3.4), name=BIG[t], pos=pos, w=26.0, pitch=31.0,
                              yaw=yaw0 if along_x else yaw0 + 60.0))
        elif t == "destroyed" and e.get("kind") in ("barrier", "gate", "pillar", "flag") and e.get("u") in rp.col:
            k = rp.col[e["u"]]
            p = rp.P[max(0, f - 1), k].astype(float)
            why = e.get("why")
            if why == "boss":
                continue                           # 镇山兽撞开的：归到它撞晕 / 坠崖的镜头里
            if why == "snowball":
                balls = [j for j in range(len(rp.ids)) if kinds[j] == "snowball" and rp.present[f, j]]
                if balls:
                    kb = min(balls, key=lambda j: float(np.linalg.norm(rp.P[f, j] - p)))
                    rel = [fr for (ee, fr) in zip(rp.events, rp.ev_frame) if ee["t"] == "release" and ee.get("u") == rp.ids[kb]]
                    a = max(f - S(3.0), (rel[0] + S(0.3)) if rel else 0)
                    shots.append(dict(f=f, a=a, b=f + S(2.4), name=BIG["barrier"], pos=_track(rp, kb, -0.5), w=25.0, pitch=29.0,
                                      yaw=yaw0, k=kb))
                    continue
            if e["kind"] == "flag":
                shots.append(dict(f=f, a=f - S(1.6), b=f + S(2.2), name=BIG["flag"], pos=const(p[0], p[1] + 1.0, p[2]), w=20.0,
                                  pitch=31.0, yaw=yaw0))
            elif e["kind"] != "pillar":
                shots.append(dict(f=f, a=f - S(1.2), b=f + S(1.8), name=BIG[e["kind"]], pos=const(p[0], p[1] + 0.8, p[2]),
                                  w=23.0, pitch=33.0, yaw=yaw0))
        elif t == "boss_stun" and e.get("u") in rp.col:
            k = rp.col[e["u"]]
            st = [s_ for s_ in bstate.get(k, []) if s_[0] <= f]
            chg = [s_ for s_ in st if s_[1] == 2]
            paw = [s_ for s_ in st if s_[1] == 1]
            dx, dz = (chg[-1][2], chg[-1][3]) if chg else (0.0, 1.0)
            a = f - S(3.0)
            if paw and f - paw[-1][0] < S(6.0):
                a = paw[-1][0] - S(0.4)
            shots.append(dict(f=f, a=a, b=f + S(2.4), name=BIG[t], pos=_track(rp, k, 1.3), w=25.0, pitch=28.0,
                              yaw=_side_yaw(dx, dz, yaw0), k=k))
        elif t == "death" and e.get("u") in rp.col and kinds[rp.col[e["u"]]] == "lord":
            # 主帅阵亡（第 4 关没过的结局）：倒下前 2.2 秒推到他身上，一直留到定格
            k = rp.col[e["u"]]
            shots.append(dict(f=f, a=f - S(2.2), b=NX + S(5), name=BIG["lord"], pos=_track(rp, k, 0.8), w=19.0, pitch=34.0,
                              yaw=yaw0, k=k, eout=0.1))
        elif t in ("sortie", "rally_go"):
            ks = sortie_members(rp, f) if t == "sortie" else rally_members(rp, f)
            if not ks:
                continue
            dur = S(2.6) if t == "sortie" else S(3.5)
            a0 = f - S(0.5) if t == "sortie" else f - S(1.0)
            seg = np.arange(max(0, a0), min(N, f + dur + 1))
            P = rp.P[seg][:, ks].astype(float)                       # (帧, 人, 3)
            live = rp.present[seg][:, ks] & (rp.S[seg][:, ks] < 3)
            cen = np.zeros((NX, 3))
            for j, fr in enumerate(seg):
                m = live[j] if live[j].any() else np.ones(len(ks), bool)
                cen[fr] = np.median(P[j, m], axis=0)
            cen[:seg[0]] = cen[seg[0]]
            cen[seg[-1] + 1:] = cen[seg[-1]]
            cen[:, 1] += 0.9
            # 宽度：装下整队（取出发那一刻和 1.5 秒后两个时候里更散的那个），近一点但不切掉人
            spread = 0.0
            for fr in (f, min(N - 1, f + S(1.5))):
                q = rp.P[fr, ks][:, [0, 2]].astype(float)
                d = np.hypot(q[:, 0] - np.median(q[:, 0]), q[:, 1] - np.median(q[:, 1]))
                spread = max(spread, float(np.percentile(d, 85)) if len(d) else 0.0)
            lo, hi = (22.0, 34.0) if t == "sortie" else (26.0, 44.0)
            wd = float(np.clip(2.6 * spread + 12.0, lo, hi))
            shots.append(dict(f=f, a=a0, b=f + dur, name=BIG[t], pos=cen, w=wd, pitch=34.0, yaw=yaw0, ein=0.7, eout=1.1,
                              soft=(t == "sortie")))
        elif t == "stone_hit" and e.get("stone") in rp.col:
            if (seen_stone and f - seen_stone[-1] < S(14.0)) or len(seen_stone) >= 3:
                continue
            seen_stone.append(f)
            # 对着落点拍（石头从画外飞进来砸中人）；原来从发射就跟着石头飞，镜头先甩到投石车、再甩回来，中间是空地
            k = rp.col[e["stone"]]
            p = rp.P[f, k].astype(float)
            first = int(np.argmax(rp.present[:, k]))
            shots.append(dict(f=f, a=max(first, f - S(1.4)), b=f + S(1.8), name=BIG["stone"], pos=const(p[0], p[1] - 0.6, p[2]),
                              w=27.0, pitch=33.0, yaw=yaw0, k=k))
        elif t == "brace" and not seen_brace:
            seen_brace = True
            pts, last = [], f
            for e2, f2 in zip(rp.events, rp.ev_frame):
                if e2["t"] == "brace" and f <= f2 <= f + S(1.5) and e2.get("u") in rp.col:
                    pts.append(rp.P[f2, rp.col[e2["u"]]])
                    last = f2
            if pts:
                c = np.mean(np.array(pts, float), axis=0)
                shots.append(dict(f=f, a=f - S(1.8), b=last + S(2.0), name=BIG["brace"], pos=const(c[0], c[1] + 0.8, c[2]),
                                  w=22.0, pitch=27.0, yaw=yaw0 + 18.0))
    # 守军出击的镜头是"带一下"：正赶上别的近景（例如雪球撞穿木栅、桶炸开的那一下就引出了这一队）就不另切
    hard = [s_ for s_ in shots if not s_.get("soft")]
    shots = hard + [s_ for s_ in shots if s_.get("soft") and not any(h["a"] - S(0.5) < s_["b"] and s_["a"] < h["b"] + S(0.5)
                                                                       for h in hard)]
    shots.sort(key=lambda s_: s_["a"])
    # 挨得近的两段连起来（中间不拉回全景又推进去）
    for s1, s2 in zip(shots, shots[1:]):
        if s2["a"] < s1["b"] + S(1.5):
            s1["b"] = max(s1["b"], s2["a"] + S(s2.get("ein", 0.8)))
    return shots


def _orbit_yaw(base, yaw0, fps, center=(0.0, 0.0), near=7.0):
    """绕着岛转的机位：镜头总在"正在打的地方"外侧、朝岛中间看（正中的灯塔在打斗背后，不挡人）。
    打斗离岛中心不到 near 米时方位不动；方位按 1.3 秒平滑，转得慢。"""
    tx, tz = base[:, 0] - center[0], base[:, 1] - center[1]
    d = np.hypot(tx, tz)
    ang = np.degrees(np.arctan2(tx, tz)) + 180.0
    out = np.zeros(len(base))
    cur = yaw0
    for i in range(len(base)):
        if d[i] >= near:
            a = ang[i]
            a = cur + ((a - cur + 180.0) % 360.0 - 180.0)
            k = min(1.0, (d[i] - near) / 5.0)
            cur = cur + (a - cur) * k
        out[i] = cur
    return _gauss(_gauss(out, 1.3 * fps), 0.8 * fps)


def director(rp, pitch=40.0, min_w=24.0, max_w=52.0, yaw=0.0, auto_close=True, shots=None, close_scale=1.0,
             close_pitch_min=0.0, orbit=False, **kw):
    """每帧机位 (N', 6)，N' = rp.nx（含片尾补帧）。全景跟拍（camera()）为底；大事件按 auto_shots() 的设计切近景；
    shots：手动机位表（见 README.md），每条 {"from": 秒, "to": 秒, "target": [x, z] 或 [x, z, 高度] 或 "unit": 物体编号,
    "width": 米, "pitch": 度, "yaw": 度, "ease": 秒}，盖在自动机位上面，按表里的顺序后写的优先。
    close_scale：自动近景的宽度统一乘一个数（想更近 / 更远）。
    close_pitch_min：自动近景的俯角下限（峡谷里的图要抬高机位，不然被崖壁挡住）。"""
    N = rp.nx
    fps = 1.0 / (rp.dt * rp.rec_every)
    base = camera(rp, pitch=pitch, min_w=min_w, max_w=max_w, yaw=yaw, **kw)
    Y = np.full(N, float(yaw))
    if orbit:                                 # render.json 的 camera 段 "orbit": true（涨潮沙洲：正中有灯塔）
        Y = _orbit_yaw(base, float(yaw), fps)
        base = camera(rp, pitch=pitch, min_w=min_w, max_w=max_w, yaw=Y, **kw)
    cam = np.zeros((N, 6))
    cam[:, :4] = base
    cam[:, 4] = pitch
    cam[:, 5] = Y
    idx = np.arange(N)

    def blend(w, tgt):
        """w (N,) 权重；tgt (N, 6) 目标机位。宽度按对数插值。"""
        w = w[:, None]
        lw = np.log(cam[:, 3:4]) * (1 - w) + np.log(np.maximum(tgt[:, 3:4], 1.0)) * w
        cam[:] = cam * (1 - w) + tgt * w
        cam[:, 3:4] = np.exp(lw)

    if auto_close:
        for s in auto_shots(rp, yaw0=yaw):
            tgt = np.zeros((N, 6))
            a, b = s["a"], s["b"]
            lo, hi = max(0, int(a) - 5), min(N, int(b) + 5)
            pos = s["pos"].copy()
            if hi - lo > 3:
                pos[lo:hi] = _gauss(pos[lo:hi], 0.22 * fps)
            tgt[:, 3] = np.asarray(s["w"]) * close_scale
            if orbit:                         # 近景的方位跟着当时的绕岛方位走（保留按事件设计的偏转）
                s["yaw"] = s["yaw"] - yaw + Y[int(np.clip(s["f"], 0, N - 1))]
            s["pitch"] = max(s["pitch"], close_pitch_min)
            tgt[:, 4] = s["pitch"]
            tgt[:, 5] = s["yaw"]
            # 主体放在画面中间偏下一点（上面一条是信息栏）：目标点沿镜头的水平前方挪过去
            _, _, _, g = _basis(s["pitch"], s["yaw"])
            fwd = 0.075 * tgt[:, 3] * 9 / 16 / max(np.sin(np.radians(s["pitch"])), 0.3)
            tgt[:, 0], tgt[:, 1], tgt[:, 2] = pos[:, 0] + g[0] * fwd, pos[:, 2] + g[1] * fwd, pos[:, 1]
            ein, eout = s.get("ein", 0.9) * fps, s.get("eout", 1.2) * fps
            w = np.minimum(_smooth((idx - a) / ein), _smooth((b - idx) / eout))
            blend(w, tgt)
    if auto_close and rp.meta.get("mode") == "melee":
        _melee_intro(rp, cam, blend, fps, orbit)
    for s in shots or []:
        a, b = s["from"] * fps, s["to"] * fps
        ez = s.get("ease", 0.8) * fps
        tgt = cam.copy()
        if "unit" in s and s["unit"] in rp.col:
            P = _track(rp, rp.col[s["unit"]])
            tgt[:, 0], tgt[:, 1], tgt[:, 2] = P[:, 0], P[:, 2], P[:, 1]
        elif "target" in s:
            t = s["target"]
            tgt[:, 0], tgt[:, 1] = t[0], t[1]
            if len(t) > 2:
                tgt[:, 2] = t[2]
        if "width" in s:
            tgt[:, 3] = s["width"]
        if "pitch" in s:
            tgt[:, 4] = s["pitch"]
        if "yaw" in s:
            tgt[:, 5] = s["yaw"]
        w = np.minimum(_smooth((idx - a) / max(ez, 1)), _smooth((b - idx) / max(ez, 1)))
        blend(w, tgt)
    return cam


def _melee_intro(rp, cam, blend, fps, orbit=False):
    """四方混战的开场：四队开局散在四个角，全景只能拍得很远。所以开打之前（第一次有人出手、放箭之前）先依次给每一队
    一个正面近景（镜头在岛中间那一侧、朝外看：兵脸朝镜头，背后是海），每队约 1 秒、硬切；再切到整个岛的高俯全景停一下，
    然后慢慢交给自动跟拍。开打得早、时间不够时只留全景，再不够就不加。"""
    N = rp.nx
    first = N
    for e, f in zip(rp.events, rp.ev_frame):
        if e["t"] in ("hit", "strike", "launch", "explode"):
            first = f
            break
    T = min(first / fps - 0.4, 6.0)          # 远处拉弓、放箭不算开打（箭在天上飞，全景里看得见）
    if T < 2.0:
        return
    teams = {}
    for i in rp.ids:
        inf = rp.info[i]
        if inf["type"] == "unit" and rp.present[0, rp.col[i]] and inf.get("team", -1) >= 0:
            teams.setdefault(inf["team"], []).append(rp.col[i])
    order = sorted(teams, key=lambda t: (np.degrees(np.arctan2(-np.mean(rp.P[0, teams[t], 0]), -np.mean(rp.P[0, teams[t], 2]))) + 360) % 360)
    wide_s = 1.3
    n = len(order)
    each = min(1.1, (T - wide_s) / max(n, 1))
    tgt = cam.copy()
    w = np.zeros(N)
    f0 = 0
    if each >= 0.75:
        for t in order:
            ks = teams[t]
            c = rp.P[0, ks].astype(float).mean(axis=0)
            yaw = float(np.degrees(np.arctan2(c[0], c[2])))          # 从岛中间朝外看这一队
            _, rr, _, _ = _basis(27.0, yaw)
            lat = rp.P[0, ks][:, [0, 2]].astype(float) @ rr[:2]
            wd0 = float(np.clip((lat.max() - lat.min()) * 1.12 + 5.0, 16.0, 34.0))   # 一队整排都装下
            pitch, wd1 = 27.0, wd0 * 0.86
            f1 = f0 + int(round(each * fps))
            _, _, _, g = _basis(pitch, yaw)
            for i in range(f0, min(f1, N)):
                u = (i - f0) / max(f1 - f0 - 1, 1)
                wd = wd0 + (wd1 - wd0) * _smooth(u)
                fwd = 0.075 * wd * 9 / 16 / max(np.sin(np.radians(pitch)), 0.3)
                tgt[i] = (c[0] + g[0] * fwd, c[2] + g[1] * fwd, c[1] + 0.9, wd, pitch, yaw)
                w[i] = 1.0
            f0 = f1
    else:
        wide_s = T
    # 全景：整个岛，高俯，方位转 45 度——四队在岛的东南西北，这样正好落在画面的四个角上（16:9 装得下、又不顶到信息栏）；
    # 之后交给自动跟拍时镜头顺势转回正南
    allk = [k for t in order for k in teams[t]]
    P0 = rp.P[0, allk][:, [0, 2]].astype(float)
    pw, yw = 50.0, 45.0
    _, rr, _, gg = _basis(pw, yw)
    lat, dep = P0 @ rr[:2], P0 @ gg[:2]
    wd = float(np.clip(max(lat.max() - lat.min() + 16.0, (dep.max() - dep.min()) * np.sin(np.radians(pw)) / (9 / 16 * 0.74) + 6.0),
                       50.0, 100.0))
    c2 = rr[:2] * (lat.max() + lat.min()) / 2 + gg[:2] * ((dep.max() + dep.min()) / 2 + 0.05 * wd)
    wide = (float(c2[0]), float(c2[1]), 1.0, wd, pw, yw)
    fw = int(round(T * fps))
    if orbit and fw < N:                      # 绕岛机位：全景的方位取离交接那一刻的自动方位最近的等价角，少转一点
        yw = float(cam[fw, 5] + ((yw - cam[fw, 5] + 180.0) % 360.0 - 180.0))
        wide = wide[:5] + (yw,)
    for i in range(f0, N):
        tgt[i] = wide
    w[f0:min(fw, N)] = 1.0
    ease = int((2.0 if orbit else 1.2) * fps)
    for i in range(fw, min(fw + ease, N)):
        w[i] = 1.0 - _smooth((i - fw) / ease)
    blend(w, tgt)


def shakes(rp):
    """镜头震动的来源：[(帧, 强度)]。爆炸、桥断、镇山兽撞上东西、路障 / 门 / 立石碎、石头落地。"""
    out = []
    for e, f in zip(rp.events, rp.ev_frame):
        t = e["t"]
        if t == "explode":
            out.append((f, 0.30))
        elif t == "terrain_removed":
            out.append((f, 0.45))
        elif t == "boss_stun":
            out.append((f, 0.60))
        elif t == "destroyed" and e.get("kind") in ("barrier", "gate", "pillar"):
            out.append((f, 0.40))
        elif t == "stone_hit":
            out.append((f, 0.16))
    return out


def summary(rp):
    import collections
    kinds = collections.Counter((rp.info[i]['kind'], rp.info[i].get('team')) for i in rp.ids)
    return "%s：%d 帧（%.1f 秒），%d 个物体 %s；事件 %d 条" % (rp.meta['map_name'], rp.n, rp.n * rp.dt * rp.rec_every,
                                                         len(rp.ids), dict(kinds), len(rp.events))


if __name__ == '__main__':
    import sys
    rp = load(sys.argv[1])
    print(summary(rp))
    cam = director(rp)
    for s_ in auto_shots(rp):
        print("近景 %-10s 第 %d～%d 帧（事件在 %d）" % (s_["name"], s_["a"], s_["b"], s_["f"]))
    for i in range(0, rp.nx, max(1, rp.nx // 12)):
        print("帧 %d  %.1f 秒  机位 %s" % (i, secs(rp, i), np.round(cam[i], 1)))
