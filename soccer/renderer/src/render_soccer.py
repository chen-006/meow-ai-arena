"""第 8 集足球 · 转播风渲染器（主程序）。

一条命令把一局回放渲成 1920×1080 / 60fps 的 MP4（无声）：
  python render_soccer.py 回放.txt -o 输出.mp4 [--start 0] [--end 1200] [--replay/--no-replay] [其它参数见 -h]

分工：本文件读回放、排时间线（直播 → 进球庆祝特写 → 转场 → 慢动作回放 → 转场 → 开球）、算每帧机位；
stage/ 下的网页（three.js）按帧描述画画面和转播包装；用无头 Chrome 逐帧截图，管道送进 ffmpeg。
全程不读时钟、不用随机数：同样的参数，同样的画面。
"""
import argparse, base64, colorsys, json, math, mimetypes, os, shutil, subprocess, sys, time
from urllib.parse import unquote, urlparse
from camera_safety import protect_shots, constrain_player

HERE = os.path.dirname(os.path.abspath(__file__))
RENDERER = os.path.normpath(os.path.join(HERE, '..'))
AVATARS = os.environ.get('A12_AVATAR_DIR') or os.path.join(RENDERER, 'assets', 'avatars')   # 选手头像（players.json 的 avatar 是这里的文件名）
CONFIG = os.path.join(HERE, 'players.json')
THREE_DIR = os.environ.get('A12_THREE_DIR') or os.path.join(RENDERER, 'node_modules', 'three')
FFMPEG = os.environ.get('A12_FFMPEG') or shutil.which('ffmpeg') or 'ffmpeg'
ORIGIN = 'http://stage.local'

# 字体：A12_FONT 指定字体文件；不给时依次找下面这些系统字体，都没有就交给浏览器按名字找（Noto Sans SC / 微软雅黑 / 苹方）
FONT_CANDIDATES = [r'C:\Windows\Fonts\NotoSansSC-VF.ttf', '/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc',
                   '/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc']


def font_file():
    f = os.environ.get('A12_FONT')
    if f:
        if not os.path.isfile(f):
            raise SystemExit(f'A12_FONT 指的字体文件不存在：{f}')
        return f
    return next((p for p in FONT_CANDIDATES if os.path.isfile(p)), None)


# 不用 Chrome 时的启动参数（GPU 用法和第 8 集制作时一样，只是不再强制要求某块显卡）
BASE_ARGS = ['--use-angle=d3d11', '--enable-gpu', '--ignore-gpu-blocklist', '--force-device-scale-factor=1',
             '--disable-background-timer-throttling', '--disable-renderer-backgrounding']
HIGH_PERF_ARGS = ['--force-high-performance-gpu']
LOW_MEM_ARGS = ['--renderer-process-limit=1', '--disable-extensions', '--disable-background-networking',
                '--js-flags=--max-old-space-size=256']
if sys.platform != 'win32':
    BASE_ARGS = [a for a in BASE_ARGS if not a.startswith('--use-angle')]


def nvenc_ok():
    try:
        r = subprocess.run([FFMPEG, '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'color=c=black:s=256x256:r=30:d=0.2',
                            '-c:v', 'h264_nvenc', '-f', 'null', '-'], capture_output=True, timeout=60)
        return r.returncode == 0
    except Exception:
        return False


def webgl_renderer(page):
    try:
        return page.evaluate('''() => { const g = document.createElement('canvas').getContext('webgl2');
          if (!g) return '没有 WebGL2'; const d = g.getExtension('WEBGL_debug_renderer_info');
          return d ? g.getParameter(d.UNMASKED_RENDERER_WEBGL) : g.getParameter(g.RENDERER); }''')
    except Exception as e:
        return f'读取失败：{e}'
NS = 27   # 每步状态：t、比分 2 个、球 x y vx vy、4 名球员 ×（x y vx vy 消息）＝ 27 个数

# ============================================================ 读回放


class Match:
    def __init__(self, path):
        self.path = path
        self.names = ['?'] * 4
        S = {}
        self.goals, self.kicks, self.kickoffs = [], [], []
        self.seed = None
        self.steps = 1200
        with open(path, 'rb') as f:
            for bl in f.read().split(b'\n'):
                line = bl.decode('utf-8', errors='replace').rstrip('\r')
                if line.startswith('S '):
                    v = [int(x) for x in line.split()[1:]]
                    S.setdefault(v[0], v)
                elif line.startswith('EV '):
                    p = line.split()
                    t, k, a = int(p[1]), p[2], [int(x) for x in p[3:]]
                    if k == 'goal':
                        self.goals.append(dict(t=t, team=a[0], last=a[1], sA=a[2], sB=a[3], mask=a[4] if len(a) > 4 else (1 << a[1] if a[1] >= 0 else 0)))
                    elif k == 'kick':
                        self.kicks.append(dict(t=t, p=a[0], vx=a[3], vy=a[4]))
                    elif k == 'kickoff':
                        self.kickoffs.append(t)
                elif line.startswith('PLAYER '):
                    q = line.split(maxsplit=4)
                    self.names[int(q[1])] = q[4] if len(q) > 4 else '?'
                elif line.startswith('SEED '):
                    self.seed = int(line.split()[1])
                elif line.startswith('STEPS '):
                    self.steps = int(line.split()[1])
        n = max(S)
        self.S = [S[t] for t in range(n + 1)]
        self.n = n
        self.resets = sorted(t for t in self.kickoffs if t > 0)
        self.goal_by_t = {g['t']: i for i, g in enumerate(self.goals)}
        kick_v = {}
        for k in self.kicks:
            st = self.S[k['t']]
            k['bx'], k['by'] = st[3], st[4]
            kick_v[k['t']] = (k['vx'], k['vy'])     # 同一步多人踢，引擎记的是合成后的球速，取哪条都一样
        for g in self.goals:
            st = self.S[g['t']]
            vx, vy = kick_v.get(g['t'], (st[5], st[6]))
            ex, ey = st[3] + vx, st[4] + vy
            if g['team'] == 0 and ex <= 12000:
                ex = 12010
            if g['team'] == 1 and ex >= 0:
                ex = -10
            g['endB'] = [ex, min(4900, max(3100, ey)), vx * 97 // 100, vy * 97 // 100]
            g['endP'] = []
            for p in range(4):
                x, y, pvx, pvy = st[7 + p * 5: 11 + p * 5]
                g['endP'].append([x + pvx, y + pvy, pvx * 9 // 10, pvy * 9 // 10])
            touch = [p for p in range(4) if g['mask'] >> p & 1]
            scorer, own = -1, False
            if len(touch) == 1:
                scorer = touch[0]
                own = (scorer // 2) != g['team']
            g['scorer'], g['own'] = scorer, own

    # ---- 给机位用的近似位置（米，场地中心为原点）
    def ball(self, tau, gi=None):
        if gi is not None:
            g = self.goals[gi]
            if tau >= g['t'] + 1:
                u = tau - g['t'] - 1
                s = 1 if g['team'] == 0 else -1
                x = g['endB'][0] / 1000 - 6 + g['endB'][2] / 1000 * (1 - 0.97 ** u) / 0.03
                x = s * min(s * x, 6.55)
                return x, g['endB'][1] / 1000 - 4
        tau = max(0.0, min(self.n, tau))
        i = min(int(tau), self.n - 1) if tau < self.n else self.n - 1
        f = tau - i
        a = self.S[i]
        if i in self.goal_by_t:
            b = self.goals[self.goal_by_t[i]]['endB']
            bx, by = b[0], b[1]
        elif (i + 1) in self.resets:
            bx, by = a[3], a[4]
        else:
            bx, by = self.S[i + 1][3], self.S[i + 1][4]
        return (a[3] + (bx - a[3]) * f) / 1000 - 6, (a[4] + (by - a[4]) * f) / 1000 - 4

    def players(self, tau, gi=None):
        if gi is not None:
            g = self.goals[gi]
            if tau >= g['t'] + 1:
                u = tau - g['t'] - 1
                out = []
                for p in range(4):
                    x, y, vx, vy = g['endP'][p]
                    k = (1 - 0.9 ** u) / 0.1
                    out.append(constrain_player((x + vx * k) / 1000 - 6, (y + vy * k) / 1000 - 4))
                return out
        i = max(0, min(self.n, int(round(tau))))
        a = self.S[i]
        return [(a[7 + p * 5] / 1000 - 6, a[8 + p * 5] / 1000 - 4) for p in range(4)]

    def seg_start(self, tau):
        s = 0
        for r in self.resets:
            if r <= tau + 1e-9:
                s = r
        return s


# ============================================================ 队色、选手


def hue(hexs):
    r, g, b = (int(hexs[i:i + 2], 16) / 255 for i in (1, 3, 5))
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    return h * 360, s, l


PARTNER = '·搭档'   # 个人赛：一名选手的程序同时控制两名球员，回放里第二名叫"<名字>·搭档"


def resolve_people(m, cfg, override=None):
    players, teams = [], []
    tkeys = []
    for p in range(4):
        nm = m.names[p]
        info = cfg['players'].get(nm)
        if info is None and nm.endswith(PARTNER):
            info = cfg['players'].get(nm[:-len(PARTNER)])
        if info is None:
            info = {'label': nm.replace(PARTNER, '')[:8], 'avatar': None, 'team': None}
        players.append(dict(info))
    for side in (0, 1):
        tk = players[side * 2]['team']
        tkeys.append(tk)
        solo = m.names[side * 2].replace(PARTNER, '') == m.names[side * 2 + 1].replace(PARTNER, '')
        t = cfg['teams'].get(tk, {'name': ' × '.join(players[side * 2 + k]['label'] for k in (0, 1)), 'color': '#cbd5e1', 'alt': '#f1f5f9'})
        name = players[side * 2]['label'] if solo else t['name']    # 个人赛：队名就是选手名
        teams.append(dict(name=name, color=t['color'], alt=t.get('alt', '#f1f5f9')))
    # 撞色：色相差 < 45° 或两边都是低饱和灰，右边那队换备用色
    (h0, s0, _), (h1, s1, _) = hue(teams[0]['color']), hue(teams[1]['color'])
    dh = min(abs(h0 - h1), 360 - abs(h0 - h1))
    if (s0 > 0.25 and s1 > 0.25 and dh < 45) or (s0 <= 0.25 and s1 <= 0.25) or teams[0]['color'] == teams[1]['color']:
        teams[1]['color'] = teams[1]['alt']
    out = []
    for p in range(4):
        a = players[p].get('avatar') or '基准.png'
        out.append(dict(label=players[p]['label'], avatar=f'/avatar/{a}',
                        color=teams[p // 2]['color'], name=m.names[p]))
    return out, teams, tkeys


# ============================================================ 时间线与机位

def spring(prev, vel, target, w, dt):
    """临界阻尼弹簧一步（半隐式），返回 (新值, 新速度)。"""
    a = w * w * (target - prev) - 2 * w * vel
    vel = vel + a * dt
    return prev + vel * dt, vel


class Timeline:
    def __init__(self, m, a):
        self.m, self.a = m, a
        self.fps = a.fps
        self.frames = []

    def add(self, **kw):
        kw['i'] = len(self.frames)
        kw['wall'] = len(self.frames) / self.fps
        self.frames.append(kw)
        return kw

    def build(self):
        m, a, fps = self.m, self.a, self.fps
        dt_live = a.sps / fps
        tau = float(a.start)
        end = float(min(a.end, m.n))
        shot = 0
        score = lambda t: (m.S[min(m.n, int(t))][1], m.S[min(m.n, int(t))][2])
        replay_set = None if a.replay_goals is None else {int(x) for x in a.replay_goals.split(',') if x.strip()}
        # 开头若正好在一次开球上，先停半秒
        pending_hold = a.lead_in
        while tau < end - 1e-9:
            nxt = [i for i, g in enumerate(m.goals) if g['t'] + 1 > tau + 1e-9]
            gi = nxt[0] if nxt else None
            stop = end if gi is None else min(end, m.goals[gi]['t'] + 1)
            # ---- 直播
            for k in range(int(round(pending_hold * fps))):
                sL, sR = score(tau)
                self.add(t=tau, goal=None, shot=shot, kind='live', hud=dict(sL=sL, sR=sR, step=tau, mode='live'))
            pending_hold = 0
            while tau < stop - 1e-9:
                sL, sR = score(tau)
                f = self.add(t=tau, goal=None, shot=shot, kind='live', hud=dict(sL=sL, sR=sR, step=tau, mode='live'))
                # 久无踢球重开：硬切 + 压黑
                for r in m.resets:
                    if r not in [g['t'] + 1 for g in m.goals]:
                        dd = (tau - r) * fps / a.sps     # 帧
                        if -10 < dd < 10:
                            f['fade'] = max(0.0, 1 - abs(dd) / 10)
                            if dd >= 0 and dd < dt_live * fps / a.sps + 1e-9:
                                shot += 1
                                f['shot'] = shot
                tau += dt_live
            if gi is None or tau < m.goals[gi]['t'] + 1 - 1e-9 or m.goals[gi]['t'] + 1 > end + 1e-9:
                break
            g = m.goals[gi]
            gt1 = g['t'] + 1
            do_replay = a.replay and (replay_set is None or (gi + 1) in replay_set)
            # ---- 进球后：直播镜头跟进 1 秒
            sc = (g['sA'], g['sB'])
            u = 0.0
            n_post = int(a.post_live * fps)
            n_close = int(a.celebrate * fps)
            cap_start = int(0.3 * fps)
            cap_len = n_post + n_close - cap_start + int(0.25 * fps)
            pop_len = int(0.7 * fps)
            hlp = g['scorer'] if (g['scorer'] >= 0 and not g['own']) else None
            for k in range(n_post + n_close):
                if k == n_post:
                    shot += 1
                kind = 'live' if k < n_post else 'close'
                f = self.add(t=gt1 + u, goal=gi, shot=shot, kind=kind,
                             hud=dict(sL=sc[0], sR=sc[1], step=gt1, mode='live', pop=max(0.0, 1 - k / pop_len) if k < pop_len else 0),
                             goalFx=dict(gi=gi, u=u))
                if k < int(0.5 * fps):
                    f['flash'] = 0.07 * math.exp(-k / 4)
                    f['shake'] = 0.8 * math.exp(-k / 14)
                    f['bloomAdd'] = 0.25 * math.exp(-k / 10)
                if cap_start <= k:
                    f['cap'] = dict(gi=gi, k=(k - cap_start) / cap_len)
                if hlp is not None:
                    f['hl'] = dict(p=hlp, k=min(1.0, k / (0.6 * fps)))
                if kind == 'close':
                    f['dof'] = 1.0
                    f['avMax'] = 170
                    f['avFocus'] = hlp if hlp is not None else -1
                    f['topLimF'] = 112
                u += dt_live * (1.0 if kind == 'live' else 0.55)
            close_u_end = u
            # 字幕剩下的尾巴放进转场前几帧
            tail = cap_len - (n_post + n_close - cap_start)
            n_st = int(a.sting * fps)
            # ---- 转场 → 回放 → 转场
            spd_txt = '慢动作'
            if do_replay:
                # 回放总长 ≈ --replay-seconds：进球前 pre_s 秒 + 进球后 tail 秒。
                # 起点 = 最后一脚（若前一脚离进球不远，从前一脚）往前几步；慢放倍率按起点自动算，夹在 [slowmo-min, slowmo-max]
                t0 = m.seg_start(g['t'])
                prev_k = [k['t'] for k in m.kicks if t0 <= k['t'] <= g['t']]
                rs = (prev_k[-1] - 4) if prev_k else g['t'] - 12
                if len(prev_k) >= 2 and prev_k[-2] >= g['t'] - 14:
                    rs = prev_k[-2] - 3
                rs = max(t0, min(rs, gt1 - 8))
                pre_s = max(0.5, a.replay_seconds - a.replay_tail)
                if a.slowmo:
                    slow = a.slowmo
                else:
                    slow = max(a.slowmo_min, min(a.slowmo_max, (gt1 - rs) / (pre_s * a.sps)))
                rs = max(t0, gt1 - pre_s * a.sps * slow)
                dt_slow = a.sps * slow / fps
                n_pre = int(math.ceil((gt1 - rs) / dt_slow))
                n_tail = int(a.replay_tail * fps)
                spd_txt = '慢动作 ×' + f'{slow:.2f}'.rstrip('0').rstrip('.')
                team_c = self.colors[g['team']]
                for k in range(n_st):
                    p = (k + 0.5) / n_st
                    if p < 0.5:
                        f = self.add(t=gt1 + close_u_end + k * dt_live * 0.55, goal=gi, shot=shot, kind='close',
                                     hud=dict(sL=sc[0], sR=sc[1], step=gt1, mode='live'), goalFx=dict(gi=gi, u=close_u_end + k * dt_live * 0.55), dof=1.0, avMax=170, avFocus=hlp if hlp is not None else -1, topLimF=112)
                        if hlp is not None:
                            f['hl'] = dict(p=hlp, k=1.0)
                        if k < tail:
                            f['cap'] = dict(gi=gi, k=(n_post + n_close - cap_start + k) / cap_len)
                    else:
                        if k == n_st // 2 + (n_st % 2):
                            shot += 1
                        f = self.add(t=rs, goal=None, shot=shot, kind='replay', hud=dict(sL=sc[0], sR=sc[1], step=gt1, mode='replay', speedText=spd_txt),
                                     sat=0.88, warm=0.35, letterbox=1.0)
                    f['sting'] = dict(p=p, c1=team_c, c2='#ffffff')
                    if p >= 0.5 and f['kind'] == 'replay':
                        pass
                    f['hud']['show'] = 1
                if n_st == 0:
                    shot += 1
                rt = float(rs)
                for k in range(n_pre):
                    self.add(t=rt, goal=None, shot=shot, kind='replay', hud=dict(sL=sc[0], sR=sc[1], step=gt1, mode='replay', speedText=spd_txt),
                             sat=0.88, warm=0.35, letterbox=1.0)
                    rt += dt_slow
                uu = rt - gt1 if rt > gt1 else 0.0
                for k in range(n_tail):
                    f = self.add(t=gt1 + uu, goal=gi, shot=shot, kind='replay', hud=dict(sL=sc[0], sR=sc[1], step=gt1, mode='replay', speedText=spd_txt),
                                 sat=0.88, warm=0.35, letterbox=1.0, goalFx=dict(gi=gi, u=uu))
                    if k < 4:
                        f['flash'] = 0.12 * math.exp(-k / 3)
                    if hlp is not None:
                        f['hl'] = dict(p=hlp, k=min(1.0, k / (0.4 * fps)))
                    uu += dt_slow
                last_kind = ('replay', gt1 + uu, gi, uu)
            else:
                last_kind = ('close', gt1 + close_u_end, gi, close_u_end)
            # ---- 转场 → 开球
            tau = float(gt1)
            sL, sR = sc
            for k in range(n_st):
                p = (k + 0.5) / n_st
                if p < 0.5:
                    kind, tt, gg, uu2 = last_kind
                    if kind == 'replay':
                        f = self.add(t=tt, goal=gg, shot=shot, kind='replay', hud=dict(sL=sL, sR=sR, step=gt1, mode='replay', speedText=spd_txt),
                                     sat=0.88, warm=0.35, letterbox=1.0, goalFx=dict(gi=gg, u=uu2))
                    else:
                        f = self.add(t=tt, goal=gg, shot=shot, kind='close', hud=dict(sL=sL, sR=sR, step=gt1, mode='live'), goalFx=dict(gi=gg, u=uu2), dof=1.0, avMax=170, avFocus=hlp if hlp is not None else -1, topLimF=112)
                        if k < tail and not do_replay:
                            f['cap'] = dict(gi=gi, k=(n_post + n_close - cap_start + k) / cap_len)
                else:
                    if k == n_st // 2 + (n_st % 2):
                        shot += 1
                    f = self.add(t=tau, goal=None, shot=shot, kind='live', hud=dict(sL=sL, sR=sR, step=tau, mode='live'))
                f['sting'] = dict(p=p, c1=self.colors[g['team']], c2='#ffffff')
            if n_st == 0:
                shot += 1
            pending_hold = a.kickoff_hold
        # 片尾不补帧
        return self.frames

    # ---------------------------------------------------- 机位
    def cameras(self):
        m, a, fps = self.m, self.a, self.fps
        dt = 1 / fps
        frames = self.frames
        i = 0
        while i < len(frames):
            j = i
            while j < len(frames) and frames[j]['shot'] == frames[i]['shot']:
                j += 1
            kind = frames[i]['kind']
            if kind == 'close':
                self._cam_close(frames[i:j])
            else:
                self._cam_follow(frames[i:j], kind)
            i = j

        protect_shots(frames, self.camera_subjects)

    def camera_subjects(self, f):
        if f['kind'] == 'close' and f.get('avFocus', -1) >= 0:
            x, z = self.m.players(f['t'], f['goal'])[f['avFocus']]
            return [(x, 0.15, z, 0.3)]
        # 广角/慢放校验实际 lookAt 焦点；球在门柱附近的瞬时遮挡
        # 属于场景几何，不把近似球轨迹当成相机穿墙的证据。
        return []

    def _desired_live(self, f):
        m, a = self.m, self.a
        tau, gi = f['t'], f['goal']
        # 往前看 0.6 秒（我们知道未来，镜头会"预判"，像老练的转播导播）
        look = []
        lim = None
        if gi is None:
            nxt = [g['t'] + 1 for g in m.goals if g['t'] + 1 > tau]
            lim = min(nxt) if nxt else m.n
            rs = [r for r in m.resets if r > tau]
            if rs:
                lim = min(lim, rs[0] - 0.001)
        for k in range(0, 9):
            tt = tau + k
            if lim is not None:
                tt = min(tt, lim - 0.001)
            look.append(m.ball(tt, gi))
        bx = sum(p[0] for p in look) / len(look) * 0.7 + look[0][0] * 0.3
        bz = sum(p[1] for p in look) / len(look) * 0.7 + look[0][1] * 0.3
        ps = m.players(tau, gi)
        cx = sum(p[0] for p in ps) / 4
        cz = sum(p[1] for p in ps) / 4
        fx, fz = 0.78 * bx + 0.22 * cx, 0.78 * bz + 0.22 * cz
        tx = max(-a.pan_limit, min(a.pan_limit, fx * 0.92))
        tz = max(-0.5, min(0.5, fz * 0.18)) + a.tz_base
        d = a.wide_d
        if abs(bx) > 3.6:
            d *= 1 - 0.10 * min(1.0, (abs(bx) - 3.6) / 1.6)
        pitch = math.radians(a.pitch)
        yaw = math.radians(-2.5) * (tx / 3)       # 机位在看台中线附近，左右摇时有一点点透视变化
        if gi is not None and tau >= m.goals[gi]['t'] + 1:   # 进球后推近球门
            s = 1 if m.goals[gi]['team'] == 0 else -1
            tx, tz, d = s * 4.6, 0.2, a.wide_d * 0.72
        return dict(tx=tx, ty=0.0, tz=tz, d=d, pitch=pitch, yaw=yaw, fov=a.fov)

    def _desired_replay(self, f):
        m, a = self.m, self.a
        tau, gi = f['t'], f['goal']
        gidx = gi if gi is not None else None
        # 回放属于哪个进球
        if gidx is None:
            gidx = min((i for i, g in enumerate(m.goals) if g['t'] + 1 > tau - 1e-9), default=len(m.goals) - 1)
        g = m.goals[gidx]
        s = 1 if g['team'] == 0 else -1
        look = [m.ball(min(tau + k, g['t'] + 0.999) if gi is None else tau + k, gi) for k in range(0, 4)]
        bx = sum(p[0] for p in look) / len(look)
        bz = sum(p[1] for p in look) / len(look)
        ps = m.players(tau, gi)
        tx = 0.75 * bx + 0.25 * s * 6
        tz = 0.7 * bz
        dx, dz = s * 0.88, -0.47                  # 从进攻方身后、略偏近侧看向球门
        yaw = math.atan2(-dx, -dz)
        return dict(tx=tx, ty=0.1, tz=tz, d=a.replay_d, pitch=math.radians(36), yaw=yaw, fov=a.fov + 4)

    def _cam_follow(self, fr, kind):
        dt = 1 / self.fps
        des = [self._desired_live(f) if kind == 'live' else self._desired_replay(f) for f in fr]
        keys = ['tx', 'tz', 'd', 'yaw']
        w = {'tx': 2.4, 'tz': 1.6, 'd': 1.3, 'yaw': 1.5} if kind == 'live' else {'tx': 3.4, 'tz': 2.8, 'd': 2.0, 'yaw': 2.0}
        cur = dict(des[0])
        vel = {k: 0.0 for k in keys}
        # 镜头开头若接在同类镜头之后，用上一个镜头末尾的位置起步（避免跳）——这里简单从目标起步
        for f, d in zip(fr, des):
            for k in keys:
                cur[k], vel[k] = spring(cur[k], vel[k], d[k], w[k], dt)
            cam = dict(d)
            for k in keys:
                cam[k] = cur[k]
            f['cam'] = cam

    def _cam_close(self, fr):
        m, a = self.m, self.a
        gi = fr[0]['goal']
        g = m.goals[gi]
        s = 1 if g['team'] == 0 else -1
        n = len(fr)
        hl = g['scorer'] if (g['scorer'] >= 0 and not g['own']) else None
        # 焦点：进球人（没有明确进球人就对准球门）
        u_end = fr[-1]['t'] - g['t'] - 1
        if hl is not None:
            px, pz = m.players(fr[-1]['t'], gi)[hl]
        else:
            px, pz = s * 5.6, 0.0
        gx, gz = s * 6.0, 0.0
        others = [q for q in range(4) if q != hl] if hl is not None else []
        pend = m.players(fr[-1]['t'], gi)
        pitch0 = math.radians(24)
        d0 = 3.1

        def score(y):
            # 视线方向 v（从机位指向进球人）、横向 l
            vx, vz = -math.sin(y), -math.cos(y)
            lx, lz = math.cos(y), -math.sin(y)
            sc = 0.0
            for q in others:
                rx, rz = pend[q][0] - px, pend[q][1] - pz
                along, lat = rx * vx + rz * vz, rx * lx + rz * lz
                if along > -0.6 and abs(lat) < 1.2:     # 别人在进球人身后或身前、横向挨得近：头像牌会挤在一起
                    sc -= (1.2 - abs(lat)) * 2.5
            gxr, gzr = gx - px, gz - pz
            gl = math.hypot(gxr, gzr) or 1
            sc += 0.6 * max(0.0, (gxr * vx + gzr * vz) / gl)   # 球门在背景里更好
            sc += 0.5 * math.cos(y)                            # 机位偏近侧（和主机位同一侧，观众不迷向）
            cx, cz = px + d0 * math.cos(pitch0) * math.sin(y), pz + d0 * math.cos(pitch0) * math.cos(y)
            if abs(cx) > 8.5 or abs(cz) > 6.5:
                sc -= 2
            return sc

        cands = [math.radians(a_) for a_ in range(-180, 180, 10)]
        best = max(cands, key=lambda y: (score(y), -abs(y)))
        # 往分数更高的一侧慢慢环绕
        side = 1 if score(best + 0.25) >= score(best - 0.25) else -1
        for k, f in enumerate(fr):
            q = k / max(1, n - 1)
            e = q * q * (3 - 2 * q)
            tau = f['t']
            ppx, ppz = (m.players(tau, gi)[hl] if hl is not None else (px, pz))
            yaw = best + side * (-0.12 + 0.24 * e)
            d = d0 + 0.3 - 0.5 * e
            pitch = pitch0 + math.radians(4) * (1 - e)
            f['cam'] = dict(tx=ppx, ty=0.32, tz=ppz, d=d, pitch=pitch, yaw=yaw, fov=a.fov + 4,
                            roll=0.012 * math.sin(k / self.fps * 1.3))


# ============================================================ 浏览器与编码


FONT = font_file()


def serve_route(route):
    url = urlparse(route.request.url)
    path = unquote(url.path)
    roots = [('/three/', THREE_DIR), ('/avatar/', AVATARS), ('/stage/', os.path.join(HERE, 'stage'))]
    fp = None
    if path in ('/', '/index.html'):
        fp = os.path.join(HERE, 'stage', 'index.html')
    elif path == '/font/main' and FONT:
        fp = FONT
    else:
        for pre, root in roots:
            if path.startswith(pre):
                fp = os.path.normpath(os.path.join(root, path[len(pre):]))
                if not fp.startswith(os.path.normpath(root)):
                    fp = None
                break
    if not fp or not os.path.isfile(fp):
        route.fulfill(status=404, body='not found ' + path)
        return
    ct = mimetypes.guess_type(fp)[0] or 'application/octet-stream'
    if fp.endswith('.js'):
        ct = 'text/javascript'
    elif path == '/font/main':
        ct = 'font/ttf'
    with open(fp, 'rb') as f:
        route.fulfill(status=200, body=f.read(), headers={'Content-Type': ct, 'Access-Control-Allow-Origin': '*'})


def make_data(m, people, teams, cfg, a):
    flat = []
    for row in m.S:
        flat.extend(row[:NS] + [0] * (NS - len(row)))
    if a.stage_text is not None:
        stage_txt = a.stage_text
    else:   # 回放路径里带 individual 的当个人赛，其余当终场团队赛
        label = '个人赛' if 'individual' in os.path.abspath(m.path).split(os.sep) else cfg.get('stage_label', '')
        stage_txt = f"{label}  ·  种子 {m.seed}" if label else f"种子 {m.seed}"
    return dict(S=flat, steps=m.steps, goals=m.goals, kicks=m.kicks, resets=m.resets, players=people, teams=teams,
                brand=cfg.get('brand', 'AI 算法对抗大赛'), stageText=stage_txt)





def argument_parser():
    ap = argparse.ArgumentParser(description='第 8 集足球 · 转播风渲染器（输出无声 mp4）')
    ap.add_argument('replay', help='回放文件（SOCCER2V2-REPLAY 2，例如 soccer/replays/final/*.txt）')
    ap.add_argument('-o', '--out', default=None, help='输出 mp4 路径（默认 renderer/renders/<回放名>.mp4）')
    ap.add_argument('--start', type=float, default=0, help='起始步（默认 0）')
    ap.add_argument('--end', type=float, default=1e9, help='结束步（默认到终场）')
    ap.add_argument('--replay', dest='replay', action='store_true', default=True, help='进球后插入慢动作回放（默认开）')
    ap.add_argument('--no-replay', dest='replay', action='store_false')
    ap.add_argument('--replay-goals', default=None, help='只给这些进球做回放，例如 "3,5"（按全场进球序号，从 1 数）')
    ap.add_argument('--sps', type=float, default=12, help='直播速度：每秒多少步（默认 12，1200 步 = 100 秒）')
    ap.add_argument('--replay-seconds', type=float, default=4.0, help='每个慢动作回放的总长（秒，默认 4，不含前后转场）')
    ap.add_argument('--replay-tail', type=float, default=0.8, help='回放里进球后再放多少秒（含在总长里，默认 0.8）')
    ap.add_argument('--slowmo', type=float, default=None, help='固定慢放倍率（不给 = 按回放长度自动算）')
    ap.add_argument('--slowmo-min', type=float, default=0.25, help='自动慢放倍率下限（默认 0.25）')
    ap.add_argument('--slowmo-max', type=float, default=0.5, help='自动慢放倍率上限（默认 0.5；最后一脚离进球太远时截掉开头）')
    ap.add_argument('--post-live', type=float, default=1.1, help='进球后直播镜头再跟多少秒（默认 1.1）')
    ap.add_argument('--celebrate', type=float, default=2.6, help='进球特写多少秒（默认 2.6）')
    ap.add_argument('--sting', type=float, default=0.7, help='转场多少秒（默认 0.7，0 = 硬切）')
    ap.add_argument('--lead-in', type=float, default=0.0, help='开头先停多少秒')
    ap.add_argument('--kickoff-hold', type=float, default=0.4, help='进球后重新开球前停多少秒')
    ap.add_argument('--fps', type=int, default=60)
    ap.add_argument('--fov', type=float, default=28, help='主机位竖直视角（度）')
    ap.add_argument('--tz-base', type=float, default=0.95, help='主机位对准点往近侧偏多少（米），让近侧边线留在画面里')
    ap.add_argument('--pitch', type=float, default=45, help='主机位俯角（度）')
    ap.add_argument('--wide-d', type=float, default=13.6, help='主机位距离（米，越大看到的场地越多）')
    ap.add_argument('--pan-limit', type=float, default=1.5, help='主机位左右最多摇到哪（米，离中线）')
    ap.add_argument('--replay-d', type=float, default=7.4, help='回放机位距离')
    ap.add_argument('--stage-text', default=None, help='比分条下方的小字（默认"终场赛 · 种子 N"，个人赛回放是"个人赛 · 种子 N"）')
    ap.add_argument('--crf', type=int, default=16, help='x264 画质（默认 16）')
    ap.add_argument('--preset', default='medium', help='x264 速度档（默认 medium）')
    ap.add_argument('--nvenc', action='store_true', help='用 NVIDIA 硬件编码（h264_nvenc，CQ 16）；不可用时退回 x264')
    ap.add_argument('--jpeg-quality', type=int, default=95, help='逐帧截图的 JPEG 质量（默认 95）')
    ap.add_argument('--browser', default=os.environ.get('A12_BROWSER', 'chrome'),
                    help='Playwright 的浏览器通道：chrome（默认，用系统装的 Chrome）、msedge，或 chromium（先 playwright install chromium）')
    ap.add_argument('--low-memory', action='store_true', help='限制 Chrome 渲染进程和 V8 堆（同时跑多条时用）')
    ap.add_argument('--no-high-performance-gpu', dest='high_performance_gpu', action='store_false', default=True,
                    help='不向 Chrome 请求高性能显卡')
    ap.add_argument('--still', type=str, default=None, help='只导出这些帧号的 PNG（逗号分隔），用于看样张')
    ap.add_argument('--every', type=int, default=1, help='每隔几帧渲一帧（预览用，输出帧率随之降低）')
    ap.add_argument('--max-frames', type=int, default=0, help='只渲前 N 帧（试渲用，默认不限）')
    ap.add_argument('--headed', action='store_true', help='显示浏览器窗口（调试用）')
    ap.add_argument('--plan', action='store_true', help='只打印时间线，不渲染')
    return ap


def main():
    a = argument_parser().parse_args()
    if a.out is None:
        a.out = os.path.join(os.environ.get('A12_WORK') or os.path.join(RENDERER, 'renders'),
                             os.path.splitext(os.path.basename(a.replay))[0] + '.mp4')

    with open(CONFIG, encoding='utf-8') as f:
        cfg = json.load(f)
    m = Match(a.replay)
    people, teams, tkeys = resolve_people(m, cfg)
    tl = Timeline(m, a)
    tl.colors = [teams[0]['color'], teams[1]['color']]
    frames = tl.build()
    tl.cameras()
    kinds = {}
    for f in frames:
        kinds[f['kind']] = kinds.get(f['kind'], 0) + 1
    print(f'回放 {os.path.basename(a.replay)}：{teams[0]["name"]}（左） vs {teams[1]["name"]}（右），终场 {m.S[-1][1]}:{m.S[-1][2]}')
    print(f'片段 {a.start:g}~{min(a.end, m.n):g} 步，{len(frames)} 帧 = {len(frames) / a.fps:.1f} 秒  ' + '  '.join(f'{k}:{v}' for k, v in kinds.items()))
    if a.plan:
        fixed = [f for f in frames if f.get('cameraSafety')]
        print(f'机位安全：自动调整 {len(fixed)} 帧 / {len(set(f["shot"] for f in fixed))} 个镜头')
        for gi, g in enumerate(m.goals):
            if a.start <= g['t'] < a.end:
                who = people[g['scorer']]['label'] if g['scorer'] >= 0 else '-'
                print(f"  进球 #{gi + 1} 第 {g['t']} 步  {teams[g['team']]['name']}  {g['sA']}:{g['sB']}  最后触球 {who}{'（乌龙）' if g['own'] else ''}")
        return
    if not os.path.isfile(os.path.join(THREE_DIR, 'build', 'three.module.js')):
        raise SystemExit(f'找不到 three.js：{THREE_DIR}\n在 soccer/renderer/ 下运行 npm install（见 README）。')

    from playwright.sync_api import sync_playwright
    data = make_data(m, people, teams, cfg, a)
    still = None if a.still is None else [int(x) for x in a.still.split(',')]
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    t_start = time.time()
    with sync_playwright() as pw:
        browser_args = BASE_ARGS + (LOW_MEM_ARGS if a.low_memory else []) + (HIGH_PERF_ARGS if a.high_performance_gpu else [])
        channel = None if a.browser == 'chromium' else a.browser
        br = pw.chromium.launch(channel=channel, headless=not a.headed, args=browser_args)
        ctx = br.new_context(viewport={'width': 1920, 'height': 1080}, device_scale_factor=1)
        page = ctx.new_page()
        logs = []
        page.on('console', lambda msg: logs.append(f'[{msg.type}] {msg.text}'))
        page.on('pageerror', lambda e: logs.append(f'[pageerror] {e}'))
        page.route(ORIGIN + '/**', serve_route)
        page.goto(ORIGIN + '/index.html')
        try:
            page.wait_for_function('window.stageReady === true', timeout=30000)
            page.evaluate('d => window.setup(d)', data)
        except Exception:
            print('\n'.join(logs[-30:]))
            raise
        cdp = ctx.new_cdp_session(page)
        print(f'场景就绪 {time.time() - t_start:.1f}s；WebGL：{webgl_renderer(page)}；字体：{FONT or "按名字找系统字体"}', flush=True)

        def grab(fmt='jpeg'):
            params = {'format': fmt, 'optimizeForSpeed': True, 'captureBeyondViewport': False}
            if fmt == 'jpeg':
                params['quality'] = a.jpeg_quality
            r = cdp.send('Page.captureScreenshot', params)
            return base64.b64decode(r['data'])

        if still is not None:
            base = os.path.splitext(a.out)[0]
            for k in still:
                k = max(0, min(len(frames) - 1, k))
                page.evaluate('f => window.renderFrame(f)', frames[k])
                p = f'{base}_帧{k:05d}.png'
                with open(p, 'wb') as fo:
                    fo.write(grab('png'))
                print('写出', p)
            if any('error' in l.lower() for l in logs):
                print('\n'.join(logs[-20:]))
            br.close()
            return
        sel = frames[::a.every]
        if a.max_frames > 0:
            sel = sel[:a.max_frames]
        out_fps = a.fps / a.every
        cmd = [FFMPEG, '-y', '-loglevel', 'error', '-f', 'image2pipe', '-framerate', f'{out_fps:g}', '-c:v', 'mjpeg', '-i', '-']
        if a.nvenc and nvenc_ok():
            venc = ['-c:v', 'h264_nvenc', '-preset', 'p5', '-tune', 'hq', '-rc', 'vbr', '-cq', '16', '-b:v', '0', '-pix_fmt', 'yuv420p']
        else:
            venc = ['-c:v', 'libx264', '-preset', a.preset, '-crf', str(a.crf), '-pix_fmt', 'yuv420p']
        cmd += venc + ['-movflags', '+faststart', '-r', f'{out_fps:g}', a.out]
        ff = subprocess.Popen(cmd, stdin=subprocess.PIPE)
        t0 = time.time()
        for k, f in enumerate(sel):
            page.evaluate('f => window.renderFrame(f)', f)
            ff.stdin.write(grab())
            if k % 120 == 0 or k == len(sel) - 1:
                el = time.time() - t0
                print(f'  {k + 1}/{len(sel)} 帧  {el:.0f}s  {(k + 1) / max(el, 1e-6):.2f} 帧/秒', flush=True)
        ff.stdin.close()
        rc = ff.wait()
        errs = [l for l in logs if 'error' in l.lower()]
        if errs:
            print('页面报错：\n' + '\n'.join(errs[-20:]))
        br.close()
    if rc != 0:
        raise SystemExit(f'ffmpeg 退出码 {rc}')
    el = time.time() - t_start
    print(f'完成 {a.out}  {len(sel)} 帧，用时 {el:.0f}s（{len(sel) / el:.2f} 帧/秒，{el / (len(sel) / out_fps):.1f} 秒渲 1 秒成片）')


if __name__ == '__main__':
    main()
