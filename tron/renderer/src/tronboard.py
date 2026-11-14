"""光轮对战（Tron）回放渲染：读回放（tron-replay-1；.json，或仓库 replays/ 里打包了查看器的 .html），按任意小数回合画出棋盘（带补间动画）。
接口：
    rp = TronReplay(path, style='mix')       # 载入时按 viewer.html 的 simulate()（= tron.hpp 的 step）逐回合重算，并和回放里记的结果核对
    img = rp.draw(f, size)                   # 透明 RGBA，size×size；f 可以是小数
    sb  = rp.scoreboard(f, w, h)             # 侧栏：回合数、存活名单、每人长度 / 死因 / 名次，vor=True 时加地盘条
    hud = rp.hud(f, h)                       # 角标：回合数 + 存活人数（全屏棋盘时贴角上用）
回合约定（同 viewer.html）：f = t 表示已经结算完 t 个回合；f∈[t, t+1) 播放第 t 步（moves[t]）的动画，头从格心匀速走到下一格心。
    第 t 步撞死的人（death_turn = t）在 τ = f - t = 0.5 时撞上（头冲出半格停下），爆点、轨迹变暗从这一刻开始；
    抢格、互换也在 τ = 0.5 发生。画面上的"第 N 回合"写 floor(f)，和查看器、交接文档的死亡回合一致（爆点出现时计数正好是 death_turn）。
风格（STYLES）：'neon' 霓虹发光 Tron 风、'flat' 扁平清爽、'avatar' 角色头像标记。配色、片中称呼都是可配置的表（PALETTE / LABEL，
    或 load_palette('配色.json')），不写死在画法里。
命令行出片用同目录的 make_video.py。
"""
import argparse, json, math, os, sys, time
import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageOps

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import common

W1080, H1080, FPS = 1920, 1080, 30
EPISODE = 'AI 算法对抗大赛 · 第 7 集 · 光轮对战'

# ---------------------------------------------------------------- 配色 / 称呼（可配置）
# 视频里用的棋盘配色。键用回放里的名字（players 字段）。
# 换配色：照下面 load_palette() 说明的格式写一个 JSON，用 load_palette() 或 make_video.py --palette 载入；新增选手直接加键。
PALETTE = {
    'Mercury': (0, 255, 0),
    'Mistral': (255, 115, 0), 'LongCat': (255, 205, 0), 'Step': (0, 210, 210), 'Pangu': (225, 30, 45),
    'Hunyuan': (215, 70, 215), 'Wenxin': (150, 220, 50), 'Dots': (245, 245, 245), 'Doubao': (250, 120, 150),
    'Qwen27B': (135, 115, 250), 'AstraUF': (100, 205, 130), 'DSNew': (70, 140, 255), 'sparring_hard': (176, 142, 104),
    'greedy': (130, 136, 150), 'greedy_fill': (108, 114, 128),
    'greedy_fill#2': (160, 132, 112),          # 同一局第 2 个同名座位用 名字#2 的颜色（没写就自动调亮）
}
LABEL = {'LongCat': '龙猫', 'Doubao': '豆包', 'Step': '阶跃', 'Pangu': '盘古', 'Hunyuan': '混元', 'Wenxin': '文心', 'Dots': 'dots',
         'Qwen27B': '千问 27B', 'AstraUF': 'Astra', 'DSNew': 'DeepSeek', 'sparring_hard': '陪练'}
# 头像文件（renderer/assets/avatars/<名>.png），按顺序找第一个存在的；都没有就画首字母圆片占位
AVATAR = {'AstraUF': ['Astra'], 'DSNew': ['DeepSeek'], 'Qwen27B': ['千问27B', '千问'], 'Doubao': ['豆包'],
          'Step': ['阶跃', 'Step'], 'Pangu': ['盘古', 'Pangu'], 'Hunyuan': ['混元', 'Hunyuan'], 'Wenxin': ['文心', 'Wenxin'],
          'Dots': ['dots', 'Dots'], 'LongCat': ['龙猫', 'LongCat'], 'Mistral': ['Mistral'], 'Mercury': ['Mercury']}
BOT_FACE = {'greedy', 'greedy_fill', 'sparring_hard'}                  # 程序画的小圆脸（往期惯例）
BG_BOTS = {'greedy', 'greedy_fill'}       # 只补位、不计分的 bot：霓虹类风格里画得更淡、光晕更弱（背景角色）
# 回放里出现了表里没有的名字时依次取用
SPARE = [(255, 140, 40), (60, 200, 210), (235, 70, 60), (240, 210, 70), (110, 175, 255), (225, 110, 230),
         (175, 225, 70), (255, 185, 150), (200, 160, 255), (120, 230, 190)]


def _rgb(v):
    if isinstance(v, str):
        v = v.lstrip('#')
        return tuple(int(v[i:i + 2], 16) for i in (0, 2, 4))
    return tuple(int(c) for c in v)


def load_palette(path):
    """读配色表 JSON：{"colors": {"Mistral": [255,115,0] 或 "#ff7300", ...}, "labels": {...}, "avatars": {"名": ["文件名", ...]}}。
    合并进全局 PALETTE / LABEL / AVATAR（之后新建的 TronReplay 生效）。"""
    with open(path, encoding='utf-8') as fp:
        cfg = json.load(fp)
    for k, v in cfg.get('colors', {}).items():
        PALETTE[k] = _rgb(v)
    LABEL.update(cfg.get('labels', {}))
    for k, v in cfg.get('avatars', {}).items():
        AVATAR[k] = [v] if isinstance(v, str) else list(v)
    if 'background_bots' in cfg:
        BG_BOTS.clear()
        BG_BOTS.update(cfg['background_bots'])
    return cfg


# ---------------------------------------------------------------- 风格
STYLES = {
    # 霓虹：近黑底 + 青色网格，轨迹是发光光墙（白热芯），头是光轮；死者轨迹闪白后"碎裂"成一格格暗方块
    'neon': dict(name='霓虹发光', floor=(6, 9, 18), grid=(18, 44, 62), grid5=(26, 62, 86), edge=(60, 190, 230),
                 wall=(30, 38, 56), wall_edge=(78, 92, 122), trail_w=.30, core=True, glow=1.0, dead_mix=.55,
                 panel=(8, 12, 22, 232), panel_line=(60, 190, 230, 90), accent=(90, 220, 255), vor_a=.20, head='cycle'),
    # 扁平：石板蓝底，轨迹是粗色带（占格宽 72%），头是带编号的圆角方块；死者轨迹褪色变细
    'flat': dict(name='扁平清爽', floor=(26, 32, 46), grid=(32, 39, 55), grid5=None, edge=(58, 68, 90),
                 wall=(70, 79, 99), wall_edge=None, trail_w=.72, core=False, glow=0.0, dead_mix=.64,
                 panel=(18, 23, 35, 235), panel_line=(255, 255, 255, 30), accent=(255, 206, 84), vor_a=.24, head='block'),
    # 头像：深蓝底（第 6 集解说版的底色），中等宽度轨迹带淡光，头是角色圆头像 + 色环；死者头像变灰打叉
    'avatar': dict(name='头像标记', floor=(19, 25, 39), grid=(27, 34, 51), grid5=None, edge=(60, 74, 104),
                   wall=(52, 60, 80), wall_edge=(84, 96, 124), trail_w=.44, core=False, glow=.45, dead_mix=.62,
                   panel=(16, 21, 34, 228), panel_line=(255, 206, 84, 70), accent=(255, 206, 84), vor_a=.22, head='avatar'),
}
# D 混搭（用户 10-20 定稿）：霓虹的底和光墙 + 角色头像做头。头像直径 av_cells 格，且不超过 av_max 成片像素；
# 12 人局 41×41 时 1.2 格：头像连光环半径约 0.68 格，碰不到隔壁通道的光墙（光墙边在 0.85 格处）
STYLES['mix'] = dict(STYLES['neon'], name='霓虹头像混搭', head='avatar_neon', av_cells=1.2, av_max=44)
for _k in ('neon', 'mix'):
    STYLES[_k]['bot_quiet'] = True
DEFAULT_STYLE = 'mix'
AVATAR_KINDS = ('avatar', 'avatar_neon')
# 抢格输家撞上后，头（头像 / ✕）沿来的方向往回退多少格，免得和赢家的头叠在一起；0 = 不退（停在撞点）。
# 用 0.3 回合缓出退到位。爆点、碎片仍在撞点。TronReplay(headon_back=...) 可按局改
HEADON_BACK = 0.5
GOLD, RED, INK, GREY, MUTE = (255, 210, 74), (255, 90, 77), (235, 238, 245), (150, 158, 176), (110, 118, 136)
SWAPC = (120, 200, 255)
DIRS = 'URDL'
DX, DY = (0, 1, 0, -1), (-1, 0, 1, 0)
CAUSE_CN = {'out': '出界', 'obstacle': '撞障碍', 'self': '撞自己', 'trail': '撞轨迹', 'headon': '抢格输', 'alive': '存活'}


# ---------------------------------------------------------------- 小工具
font = common.font


def ease(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3 - 2 * x)


def ease_out(x):
    x = max(0.0, min(1.0, x))
    return 1 - (1 - x) ** 3


def lerp(a, b, k):
    return a + (b - a) * k


def mix(c1, c2, k):
    return tuple(int(round(lerp(a, b, k))) for a, b in zip(c1, c2))


def clamp(v, a=0.0, b=1.0):
    return a if v < a else b if v > b else v


def rnd(i, j):
    v = math.sin(i * 127.1 + j * 311.7) * 43758.5453
    return v - math.floor(v)


def redmean(c1, c2):
    r = (c1[0] + c2[0]) / 2
    dr, dg, db = c1[0] - c2[0], c1[1] - c2[1], c1[2] - c2[2]
    return math.sqrt((2 + r / 256) * dr * dr + 4 * dg * dg + (2 + (255 - r) / 256) * db * db)


def find_avatar(names):
    for n in names:
        p = common.avatar_path(n)
        if p:
            return p
    return None


_av = {}


def avatar(name, px, col=None, gray=False):
    """圆形头像（回放里的名字）；没有素材的用首字母圆片，greedy / 陪练用程序画的小圆脸。"""
    px = max(4, int(px))
    key = (name, px, col, gray)
    if key in _av:
        return _av[key]
    col = col or PALETTE.get(name, (150, 150, 150))
    src = None if name in BOT_FACE else find_avatar(AVATAR.get(name, []) + [name])
    if src:
        im = common.round_avatar(src, px)
    else:
        S = 128
        im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        if name in BOT_FACE:
            d.ellipse([0, 0, S - 1, S - 1], fill=mix(col, (40, 44, 56), .3))
            d.ellipse([S * .28, S * .3, S * .42, S * .44], fill=(225, 228, 236))
            d.ellipse([S * .58, S * .3, S * .72, S * .44], fill=(225, 228, 236))
            d.rounded_rectangle([S * .3, S * .6, S * .7, S * .68], S * .04, fill=(225, 228, 236))
        else:
            d.ellipse([0, 0, S - 1, S - 1], fill=mix(col, (20, 24, 36), .35))
            lab = LABEL.get(name, name)
            d.text((S / 2, S / 2 + 2), lab[:1].upper() if lab[:1].isascii() else lab[:1], font=font(70, 'Black'),
                   fill=(255, 255, 255), anchor='mm')
        im = im.resize((px, px), Image.LANCZOS)
    if gray:
        a = im.getchannel('A')
        g = ImageOps.grayscale(im.convert('RGB')).point(lambda v: int(40 + v * .45))
        im = Image.merge('RGBA', (g, g, g, a))
    _av[key] = im
    return im


# ---------------------------------------------------------------- 结算（照 viewer.html simulate()，即 tron.hpp 的 step）
def simulate(R):
    W, H, N = R['W'], R['H'], R['n']
    wall = np.array([[c == '#' for c in row] for row in R['rows']], bool)
    owner = np.full((H, W), -1, np.int16)
    claimT = np.full((H, W), 1 << 30, np.int32)
    x = [s[0] for s in R['spawns']]
    y = [s[1] for s in R['spawns']]
    alive, last, dt, cause = [True] * N, [0] * N, [-1] * N, ['alive'] * N
    path = [[(x[k], y[k])] for k in range(N)]
    mdir = [[] for _ in range(N)]                 # 每一步的方向（含撞死的那一步）
    target = [None] * N
    for k in range(N):
        owner[y[k], x[k]] = k
        claimT[y[k], x[k]] = 0
    events, turn = [], 0
    for t, s in enumerate(R['moves']):
        tx, ty, why = [0] * N, [0] * N, ['alive'] * N
        for k in range(N):
            if not alive[k]:
                continue
            d = DIRS.find(s[k]) if k < len(s) else -1
            if d < 0:
                d = last[k]
            last[k] = d
            mdir[k].append(d)
            tx[k], ty[k] = x[k] + DX[d], y[k] + DY[d]
            if not (0 <= tx[k] < W and 0 <= ty[k] < H):
                why[k] = 'out'
            elif wall[ty[k], tx[k]]:
                why[k] = 'obstacle'
            elif owner[ty[k], tx[k]] == k:
                why[k] = 'self'
            elif owner[ty[k], tx[k]] >= 0:
                why[k] = 'trail'
        pri = lambda k: (k - turn) % N
        contest = {}
        for k in range(N):
            if not alive[k] or why[k] != 'alive':
                continue
            best, rivals = k, 1
            for j in range(N):
                if j == k or not alive[j] or tx[j] != tx[k] or ty[j] != ty[k]:
                    continue
                rivals += 1
                if pri(j) < pri(best):
                    best = j
            if rivals >= 2:
                contest[k] = best
                if best != k:
                    why[k] = 'headon'
        seen = set()
        for k in sorted(contest):
            c = (tx[k], ty[k])
            if c in seen:
                continue
            seen.add(c)
            allk = [j for j in sorted(contest) if (tx[j], ty[j]) == c]
            events.append(dict(type='contest', t=t, x=c[0], y=c[1], winner=contest[k], losers=[j for j in allk if j != contest[k]]))
        swap = {}
        for a in range(N):
            for b in range(a + 1, N):
                if alive[a] and alive[b] and tx[a] == x[b] and ty[a] == y[b] and tx[b] == x[a] and ty[b] == y[a]:
                    events.append(dict(type='swap', t=t, a=a, b=b))
                    swap[a], swap[b] = b, a
        for k in range(N):
            if not alive[k] or why[k] == 'alive':
                continue
            inb = 0 <= tx[k] < W and 0 <= ty[k] < H
            hit = int(owner[ty[k], tx[k]]) if inb else -1
            events.append(dict(type='death', t=t, k=k, cause=why[k], tx=tx[k], ty=ty[k], fx=x[k], fy=y[k], hit=hit,
                               head=hit >= 0 and hit != k and x[hit] == tx[k] and y[hit] == ty[k], swap=swap.get(k, -1),
                               winner=contest.get(k, -1) if why[k] == 'headon' else -1))
        for k in range(N):
            if not alive[k]:
                continue
            if why[k] != 'alive':
                alive[k], dt[k], cause[k], target[k] = False, t, why[k], (tx[k], ty[k])
            else:
                x[k], y[k] = tx[k], ty[k]
                owner[y[k], x[k]] = k
                claimT[y[k], x[k]] = t + 1
                path[k].append((x[k], y[k]))
        turn += 1
        if sum(alive) <= 1 or turn >= W * H:
            break
    key = [W * H + 1 if alive[k] else dt[k] for k in range(N)]
    rank, score = [], []
    for k in range(N):
        ahead = sum(1 for j in range(N) if key[j] > key[k])
        behind = sum(1 for j in range(N) if key[j] < key[k])
        tie = N - 1 - ahead - behind
        rank.append(ahead)
        score.append((behind + .5 * tie) / (N - 1) if N > 1 else 0)
    return dict(W=W, H=H, N=N, wall=wall, owner=owner, claimT=claimT, path=path, mdir=mdir, dt=dt, cause=cause,
                target=target, turns=turn, final=[(x[k], y[k]) for k in range(N)], rank=rank, score=score, events=events)


# ---------------------------------------------------------------- 回放
class TronReplay:
    def __init__(self, path, style=DEFAULT_STYLE, names=None, palette=None, check=True, headon_back=None):
        """path：tron-replay-1 的 .json。names：替换选手名（列表）。palette：额外的配色 JSON 路径。
        check：把重算结果和回放里记的 turns / death_turn / cause / final / rank 核对，不一致时报错。"""
        if palette:
            load_palette(palette)
        self.path = path
        self.raw = common.load_json_or_html(path)
        if self.raw.get('format') not in (None, 'tron-replay-1'):
            raise ValueError(f'不认识的回放格式 {self.raw.get("format")}（只认 tron-replay-1）')
        R = self.raw
        self.style = style
        self.headon_back = HEADON_BACK if headon_back is None else headon_back
        self.who = list(names or R['players'])
        S = simulate(R)
        self.S = S
        self.W, self.H, self.N, self.T = S['W'], S['H'], S['N'], S['turns']
        self.wall, self.path, self.mdir, self.dt = S['wall'], S['path'], S['mdir'], S['dt']
        self.cause, self.target, self.rank, self.claimT = S['cause'], S['target'], S['rank'], S['claimT']
        self.events = S['events']
        self.deaths = {e['k']: e for e in self.events if e['type'] == 'death'}
        self.ev_t = {}
        for e in self.events:
            self.ev_t.setdefault(e['t'], []).append(e)
        if check:
            bad = []
            if R.get('turns') != self.T:
                bad.append(f'turns {R.get("turns")} ≠ {self.T}')
            for k in range(self.N):
                for fld, mine in (('death_turn', self.dt[k]), ('cause', self.cause[k]), ('rank', self.rank[k]),
                                  ('final', list(S['final'][k]))):
                    if fld in R and R[fld][k] != mine:
                        bad.append(f'{fld}[{k}] 回放 {R[fld][k]} ≠ 重算 {mine}')
            if bad:
                raise ValueError(f'{os.path.basename(path)} 重算和回放不一致：' + '；'.join(bad[:8]))
        # 每个座位的颜色、称呼
        self.col, self.labels, used = [], [], {}
        spare = iter(SPARE)
        for k, n in enumerate(self.who):
            base = PALETTE.get(n) or next(spare, (150, 150, 150))
            i = used.get(n, 0)
            used[n] = i + 1
            c = base if i == 0 else PALETTE.get(f'{n}#{i + 1}') or (mix(base, (255, 255, 255), .32) if i == 1 else mix(base, (0, 0, 0), .3))
            self.col.append(c)
            lab = LABEL.get(n, n)
            self.labels.append(lab if self.who.count(n) == 1 else f'{lab} {"①②③④⑤"[i]}')
        self.close_pairs = [(a, b, round(redmean(self.col[a], self.col[b])))
                            for a in range(self.N) for b in range(a + 1, self.N) if redmean(self.col[a], self.col[b]) < 90]
        self._vor = {}
        self._lay = {}

    # ---------- 派生量
    def label(self, k):
        return self.labels[k]

    def alive_at(self, k, f):
        """f 时刻 k 是否还活着（撞上那一刻 = death_turn + 0.5 算出局，和爆点同步）。"""
        return self.dt[k] < 0 or f < self.dt[k] + .5

    def length_at(self, k, f):
        return min(int(math.floor(f)), len(self.path[k]) - 1) + 1

    def head(self, k, f):
        """(x, y, 方向, 状态)：x / y 是格坐标（小数，格心 = 整数）；状态 'alive' / 'dying'（本回合撞，τ<.5）/ 'dead'。"""
        p, n = self.path[k], len(self.path[k]) - 1
        f = clamp(f, 0, self.T)
        t = int(math.floor(f))
        tau = f - t
        md = self.mdir[k]
        if t < n:
            (x0, y0), (x1, y1) = p[t], p[t + 1]
            return x0 + (x1 - x0) * tau, y0 + (y1 - y0) * tau, md[t], 'alive'
        x0, y0 = p[n]
        if self.dt[k] >= 0:
            d = md[self.dt[k]]
            if t == self.dt[k] and tau < .5:
                return x0 + DX[d] * tau * .9, y0 + DY[d] * tau * .9, d, 'dying'
            off = .45
            if self.cause[k] == 'headon' and self.headon_back:     # 抢格输家：撞上后往回退
                off -= self.headon_back * ease((f - (self.dt[k] + .5)) / .3)
            return x0 + DX[d] * off, y0 + DY[d] * off, d, 'dead'
        d = md[n - 1] if n > 0 else (md[0] if md else 0)
        return x0, y0, d, 'alive'

    def death_age(self, k, f):
        """撞上之后过了多少回合（没死或还没撞返回 -1）。"""
        if self.dt[k] < 0:
            return -1
        a = f - (self.dt[k] + .5)
        return a if a >= 0 else -1

    def vor(self, v):
        """第 v 回合结算完时的地盘：多源 BFS，每个空格归最先到达的活人，同时到达的不归任何人（照 viewer.html vorAt）。
        返回 (own: H×W int8，-1 无主 / -2 并列，cnt: 每人格数，free: 空格总数)。"""
        v = int(clamp(v, 0, self.T))
        if v in self._vor:
            return self._vor[v]
        W, H, N = self.W, self.H, self.N
        blocked = self.wall | (self.claimT <= v)
        dist = np.full((H, W), -1, np.int32)
        own = np.full((H, W), -1, np.int8)
        q = []
        for k in range(N):
            if self.dt[k] < 0 or self.dt[k] >= v:
                hx, hy = self.path[k][min(v, len(self.path[k]) - 1)]
                dist[hy, hx] = 0
                own[hy, hx] = k
                q.append((hx, hy))
        d = 0
        while q:
            nq = []
            for cx, cy in q:
                o = own[cy, cx]
                if o < 0:
                    continue
                for dd in range(4):
                    nx, ny = cx + DX[dd], cy + DY[dd]
                    if not (0 <= nx < W and 0 <= ny < H) or blocked[ny, nx]:
                        continue
                    if dist[ny, nx] < 0:
                        dist[ny, nx] = d + 1
                        own[ny, nx] = o
                        nq.append((nx, ny))
                    elif dist[ny, nx] == d + 1 and own[ny, nx] != o:
                        own[ny, nx] = -2
            q = nq
            d += 1
        own = np.where(blocked, -1, own)
        cnt = [int((own == k).sum()) for k in range(N)]
        r = (own, cnt, int((~blocked).sum()))
        self._vor[v] = r
        return r

    def key_turns(self):
        """值得慢放的回合（事件在 t + 0.5 发生）：有人出局、抢格、互换。"""
        return sorted({e['t'] for e in self.events})

    def play_times(self, t0, t1, fps=FPS, tps=4.0, slow_tps=1.5, keys=None, radius=.8, hold=1.0):
        """视频每一帧对应的小数回合。平时每秒 tps 回合（查看器 1× = 4），keys 里的回合（事件时刻 t+.5）前后 radius 回合内
        平滑降到 slow_tps。末尾停 hold 秒。"""
        keys = self.key_turns() if keys is None else keys
        evt = [k + .5 for k in keys]

        def rate(f):
            dist = min([abs(f - e) for e in evt] + [99])
            return lerp(tps, slow_tps, ease(1 - dist / max(.01, radius)))
        out, f = [], float(t0)
        while f < t1:
            out.append(f)
            f += rate(f) / fps
        out.append(float(t1))
        out += [float(t1)] * int(hold * fps)
        return out

    # ---------- 棋盘
    def draw(self, f, size, view=None, dim=None, focus=None, ring=None, vor=False, tags=None, ss=2, style=None, notes=None):
        """f：小数回合。size：输出边长（像素，正方形，透明圆角）。view：(左, 上, 边长) 以格为单位的镜头（可小数，做推拉）。
        dim：要压暗的座位集合；focus：只突出这些座位（其余压暗）；ring：头上加金色高亮圈的座位集合。
        vor：叠加地盘（Voronoi）热区。tags：头旁边写名字（默认 ≤4 人时写；头像风格不写）。ss：超采样倍数。
        style：'neon' / 'flat' / 'avatar'，默认用 TronReplay(style=...) 定的。notes：保留（和 tboard 同形），忽略。"""
        St = STYLES[style or self.style]
        if focus is not None:
            dim = set(range(self.N)) - set(focus)
        dim = set(dim or ())
        ring = set(ring or ())
        if tags is None:
            tags = self.N <= 4 and St['head'] not in AVATAR_KINDS
        ss = max(1, int(ss))
        self._ss = ss
        S = size * ss
        side = view[2] if view else max(self.W, self.H) + .7
        c = S / side
        if view:
            ox, oy = -view[0] * c, -view[1] * c
        else:
            ox, oy = (S - self.W * c) / 2, (S - self.H * c) / 2
        P = lambda x, y: (ox + (x + .5) * c, oy + (y + .5) * c)          # 格坐标（格心整数）→ 像素
        img = Image.new('RGB', (S, S), St['floor'])
        self._floor(img, St, c, ox, oy, S)
        d = ImageDraw.Draw(img, 'RGBA')
        v = int(clamp(math.floor(f + .5), 0, self.T))
        if vor:
            own = self.vor(v)[0]
            for k in range(self.N):
                col = self.col[k] + (int(255 * St['vor_a'] * (.35 if k in dim else 1)),)
                ys, xs = np.nonzero(own == k)
                for cx, cy in zip(xs.tolist(), ys.tolist()):
                    x0, y0 = ox + cx * c, oy + cy * c
                    d.rectangle([x0, y0, x0 + c - 1, y0 + c - 1], fill=col)
        self._walls(d, St, c, ox, oy)
        # 轨迹：先死者后活人
        glow = Image.new('RGB', (S // 4, S // 4), (0, 0, 0)) if St['glow'] > 0 else None
        gd = ImageDraw.Draw(glow) if glow else None
        order = sorted(range(self.N), key=lambda k: (self.alive_at(k, f), k))
        heads = []
        for k in order:
            heads.append(self._trail(d, gd, k, f, St, c, P, k in dim))
        if glow is not None:
            self._effects_glow(gd, f, St, c, P)
            gl = glow.filter(ImageFilter.GaussianBlur(max(1.0, c / 4 * .55))).resize((S, S), Image.BILINEAR)
            if St['glow'] != 1:
                gl = Image.eval(gl, lambda q: int(q * St['glow']))
            img = ImageChops.screen(img, gl)
            d = ImageDraw.Draw(img, 'RGBA')
        self._effects(img, d, f, St, c, P)
        for h in heads:
            self._head(img, d, h, f, St, c, P, ring)
        d = ImageDraw.Draw(img, 'RGBA')
        self._labels(d, f, St, c, P, heads, tags, ss)
        # 外框
        d.rounded_rectangle([ox - c * .3, oy - c * .3, ox + (self.W + .3) * c, oy + (self.H + .3) * c], c * .35,
                            outline=St['edge'] + (200,), width=max(2, int(c * .07)))
        out = img.convert('RGBA')
        if ss > 1:
            out = out.resize((size, size), Image.LANCZOS)
        m = Image.new('L', (size * 2, size * 2), 0)
        ImageDraw.Draw(m).rounded_rectangle([0, 0, size * 2 - 1, size * 2 - 1], 28, fill=255)
        out.putalpha(m.resize((size, size), Image.LANCZOS))
        return out

    def _floor(self, img, St, c, ox, oy, S):
        d = ImageDraw.Draw(img)
        W, H = self.W, self.H
        lw = max(1, int(c * .045))
        for i in range(W + 1):
            col = St['grid5'] if St['grid5'] and i % 5 == 0 else St['grid']
            x = ox + i * c
            d.line([(x, oy), (x, oy + H * c)], fill=col, width=lw)
        for j in range(H + 1):
            col = St['grid5'] if St['grid5'] and j % 5 == 0 else St['grid']
            y = oy + j * c
            d.line([(ox, y), (ox + W * c, y)], fill=col, width=lw)

    def _walls(self, d, St, c, ox, oy):
        wall = self.wall
        H, W = wall.shape
        ys, xs = np.nonzero(wall)
        for x, y in zip(xs.tolist(), ys.tolist()):
            x0, y0 = ox + x * c, oy + y * c
            # 和相邻障碍连成片：朝有邻居的方向不留缝
            l = 0 if x > 0 and wall[y, x - 1] else c * .06
            rr = 0 if x < W - 1 and wall[y, x + 1] else c * .06
            t = 0 if y > 0 and wall[y - 1, x] else c * .06
            b = 0 if y < H - 1 and wall[y + 1, x] else c * .06
            d.rectangle([x0 + l, y0 + t, x0 + c - rr, y0 + c - b], fill=St['wall'])
        e = St['wall_edge']
        if e:
            lw = max(1, int(c * .07))
            for x, y in zip(xs.tolist(), ys.tolist()):
                x0, y0 = ox + x * c, oy + y * c
                g = c * .06
                if not (x > 0 and wall[y, x - 1]):
                    d.line([(x0 + g, y0), (x0 + g, y0 + c)], fill=e + (230,), width=lw)
                if not (x < W - 1 and wall[y, x + 1]):
                    d.line([(x0 + c - g, y0), (x0 + c - g, y0 + c)], fill=e + (230,), width=lw)
                if not (y > 0 and wall[y - 1, x]):
                    d.line([(x0, y0 + g), (x0 + c, y0 + g)], fill=e + (230,), width=lw)
                if not (y < H - 1 and wall[y + 1, x]):
                    d.line([(x0, y0 + c - g), (x0 + c, y0 + c - g)], fill=e + (230,), width=lw)

    def _pts(self, k, f):
        """f 时刻 k 的轨迹折线（格坐标）和头。"""
        p = self.path[k]
        t = int(math.floor(clamp(f, 0, self.T)))
        hx, hy, hd, st = self.head(k, f)
        n = min(t, len(p) - 1)
        pts = list(p[:n + 1])
        if (hx, hy) != pts[-1]:
            pts.append((hx, hy))
        return pts, (hx, hy, hd, st)

    def _trail(self, d, gd, k, f, St, c, P, dimmed):
        pts, (hx, hy, hd, st) = self._pts(k, f)
        base = self.col[k]
        floor = St['floor']
        age = self.death_age(k, f)
        dead_p = clamp(age / .9) if age >= 0 else 0.0          # 0 活 → 1 完全变成死者样式
        flash = (1 - age / .3) if 0 <= age < .3 else 0.0       # 撞上后 0.3 回合内整条轨迹闪白
        dimk = .55 if dimmed else 0.0
        quiet = bool(St.get('bot_quiet')) and self.who[k] in BG_BOTS     # 补位 bot：颜色压暗、光晕减到三成
        if quiet:
            base = mix(base, floor, .4)
        gq = .3 if quiet else 1.0
        w = St['trail_w'] * c
        live_col = mix(mix(base, (255, 255, 255), .75 * flash * gq), floor, dimk)
        dead_col = mix(mix(base, floor, St['dead_mix']), floor, dimk * .6)
        neon = St['head'] in ('cycle', 'avatar_neon')
        if neon:
            # 活：发光光墙；死：闪白后碎成一格格暗方块（"derez"）
            if dead_p < 1:
                self._poly(d, P, pts, w * (1 - .5 * dead_p), mix(live_col, dead_col, dead_p))
                if not dimmed:
                    self._poly(d, P, pts, max(1.0, w * .3), mix(mix(base, (255, 255, 255), .5 * gq), dead_col, dead_p))
                if gd is not None:
                    gc = mix((0, 0, 0), mix(base, (255, 255, 255), flash * .6), (1 - dead_p) * (.35 if dimmed else 1) * gq)
                    self._poly(gd, lambda x, y: tuple(v / 4 for v in P(x, y)), pts, w * 1.9 / 4, gc)
                    if st != 'dead' and not dimmed:                    # 头部光晕
                        gx, gy = P(hx, hy)
                        r = c * (1.15 if St['head'] == 'cycle' else .95) / 4
                        gd.ellipse([gx / 4 - r, gy / 4 - r, gx / 4 + r, gy / 4 + r],
                                   fill=mix((0, 0, 0), mix(base, (255, 255, 255), .35), .85 * gq))
            if gd is not None and St['head'] == 'avatar_neon' and st == 'dead':   # 出局头像上的红叉带一点光
                gx, gy = (v / 4 for v in P(hx, hy))
                r = self._av_px(St, c) * .26 / 4
                inten = (.55 + .45 * clamp(1 - age / .5)) * (.4 if dimmed else 1)
                gcol = mix((0, 0, 0), (255, 60, 50), inten)
                gw = max(1, int(c * .16 / 4))
                gd.line([(gx - r, gy - r), (gx + r, gy + r)], fill=gcol, width=gw)
                gd.line([(gx - r, gy + r), (gx + r, gy - r)], fill=gcol, width=gw)
            if dead_p > 0:
                a = int(255 * dead_p)
                q = c * .2
                cells = self.path[k][:len(pts)]
                for i, (x, y) in enumerate(cells):
                    jx = (rnd(k, i) - .5) * c * .12 * dead_p
                    jy = (rnd(i, k) - .5) * c * .12 * dead_p
                    cx, cy = P(x, y)
                    s = q * (1 + .25 * rnd(k + 7, i))
                    d.rectangle([cx - s + jx, cy - s + jy, cx + s + jx, cy + s + jy], fill=dead_col + (a,))
        else:
            ww = w * (1 - (.68 if St['head'] == 'block' else .3) * dead_p)
            col = mix(live_col, dead_col, dead_p)
            if St['glow'] > 0 and gd is not None and dead_p < 1 and not dimmed:
                self._poly(gd, lambda x, y: tuple(v / 4 for v in P(x, y)), pts, w * 1.6 / 4, mix((0, 0, 0), base, (1 - dead_p) * .8))
            self._poly(d, P, pts, ww, col)
            if St['head'] == 'block' and dead_p < 1 and not dimmed:     # 扁平：色带中线一道浅色，轨迹走向更清楚
                self._poly(d, P, pts, max(1.0, w * .16), mix(col, (255, 255, 255), .35))
        return dict(k=k, x=hx, y=hy, d=hd, st=st, age=age, dimmed=dimmed, quiet=quiet)

    @staticmethod
    def _poly(d, P, pts, w, col):
        """轴向折线画成一串矩形（方角相接），w = 线宽（像素）。"""
        h = w / 2
        if len(pts) == 1:
            x, y = P(*pts[0])
            d.rectangle([x - h, y - h, x + h, y + h], fill=col)
            return
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            a, b = P(x0, y0), P(x1, y1)
            d.rectangle([min(a[0], b[0]) - h, min(a[1], b[1]) - h, max(a[0], b[0]) + h, max(a[1], b[1]) + h], fill=col)

    def _effects_glow(self, gd, f, St, c, P):
        """发光层上的特效（只有 neon / avatar 有发光层）：爆点白光、抢格金光。"""
        q = lambda x, y: tuple(v / 4 for v in P(x, y))
        for k, e in self.deaths.items():
            a = f - (e['t'] + .5)
            if 0 <= a < .6:
                bx, by = self._crash_xy(e)
                cx, cy = q(bx, by)
                r = c / 4 * (.6 + 2.6 * ease_out(a / .6))
                col = mix(mix(self.col[k], (255, 255, 255), .6), (0, 0, 0), a / .6)
                gd.ellipse([cx - r, cy - r, cx + r, cy + r], fill=col)
        for e in self.events:
            if e['type'] != 'contest':
                continue
            a = f - (e['t'] + .5)
            if 0 <= a < .8:
                cx, cy = q(e['x'], e['y'])
                r = c / 4 * (.5 + 1.2 * ease_out(a / .8))
                gd.ellipse([cx - r, cy - r, cx + r, cy + r], fill=mix(GOLD, (0, 0, 0), a / .8))

    @staticmethod
    def _crash_xy(e):
        """撞点（格坐标）：从最后一格往目标格走 0.45 格。"""
        return e['fx'] + (e['tx'] - e['fx']) * .45, e['fy'] + (e['ty'] - e['fy']) * .45

    def _effects(self, img, d, f, St, c, P):
        """爆点（扩散环 + 碎片方块）、抢格（金色菱形 + "抢格"）、互换（蓝环）。"""
        for k, e in self.deaths.items():
            a = f - (e['t'] + .5)
            if not 0 <= a < 1.4:
                continue
            bx, by = self._crash_xy(e)
            cx, cy = P(bx, by)
            col = self.col[k]
            u = ease_out(a / 1.4)
            al = int(255 * (1 - a / 1.4))
            r = c * (.5 + 2.2 * u)
            d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=mix(col, (255, 255, 255), .4) + (al,), width=max(2, int(c * .12)))
            if a < .25:
                r2 = c * (.4 + 1.4 * a / .25)
                d.ellipse([cx - r2, cy - r2, cx + r2, cy + r2], fill=(255, 255, 255, int(220 * (1 - a / .25))))
            for i in range(14):                               # 碎片：小方块向外飞、转、淡出
                ang = 2 * math.pi * (i / 14 + rnd(k, i) * .07)
                dist = c * (.4 + (1.2 + 1.6 * rnd(i, k)) * u)
                px, py = cx + math.cos(ang) * dist, cy + math.sin(ang) * dist
                s = c * (.16 + .1 * rnd(k + 3, i)) * (1 - .5 * u)
                rot = ang + a * 6
                pts = [(px + math.cos(rot + j * math.pi / 2) * s, py + math.sin(rot + j * math.pi / 2) * s) for j in range(4)]
                d.polygon(pts, fill=mix(col, (255, 255, 255), .25 * (1 - u)) + (al,))
        for e in self.events:
            a = f - (e['t'] + .5)
            if not 0 <= a < 2.2:
                continue
            if e['type'] == 'contest':
                cx, cy = P(e['x'], e['y'])
                al = int(255 * (1 - a / 2.2))
                neon = St['head'] in ('cycle', 'avatar_neon')
                for j, rr in enumerate((.75 + .6 * ease_out(a / .6), 1.25 + 1.2 * ease_out(a / 1.2))):
                    r = c * rr
                    dia = [(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)]
                    d.polygon(dia, outline=GOLD + (int(al * (1 if j == 0 else .5)),), width=max(2, int(c * (.1 if neon else .13))))
                    if neon and j == 0:                        # 霓虹：金色菱形里加一道白热芯，和光墙同一画法
                        d.polygon(dia, outline=(255, 248, 220, int(al * .9)), width=max(1, int(c * .035)))
            elif e['type'] == 'swap':
                (ax, ay), (bx, by) = self.path[e['a']][-1], self.path[e['b']][-1]
                cx, cy = P((ax + bx) / 2, (ay + by) / 2)
                r = c * (.8 + 1.0 * ease_out(a / 1.0))
                d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=SWAPC + (int(255 * (1 - a / 2.2)),), width=max(2, int(c * .13)))

    def _head(self, img, d, h, f, St, c, P, ring):
        k, st = h['k'], h['st']
        cx, cy = P(h['x'], h['y'])
        col = self.col[k]
        dd = h['d']
        dimmed = h['dimmed']
        kind = St['head']
        if kind == 'avatar_neon':
            return self._head_avneon(img, d, h, St, c, cx, cy, ring)
        if st == 'dead':
            age = h['age']
            if kind == 'avatar':
                s = int(c * (1.15 if age > .6 else lerp(1.7, 1.15, ease(age / .6))))
                av = avatar(self.who[k], s, col, gray=True)
                if dimmed:
                    av = av.copy()
                    av.putalpha(av.getchannel('A').point(lambda q: q // 2))
                img.paste(av, (int(cx - s / 2), int(cy - s / 2)), av)
                d.ellipse([cx - s / 2, cy - s / 2, cx + s / 2, cy + s / 2], outline=mix(col, St['floor'], .4) + (255,),
                          width=max(1, int(c * .08)))
                self._cross(d, cx, cy, s * .32, RED, max(2, int(c * .12)))
            else:
                s = c * .62
                fill = (6, 8, 12) if kind == 'cycle' else mix(St['floor'], (0, 0, 0), .4)
                d.rounded_rectangle([cx - s / 2, cy - s / 2, cx + s / 2, cy + s / 2], s * .2, fill=fill,
                                    outline=mix(col, St['floor'], .25 if not dimmed else .6) + (255,), width=max(1, int(c * .08)))
                self._cross(d, cx, cy, s * .26, mix(col, (255, 255, 255), .2) if not dimmed else mix(col, St['floor'], .5),
                            max(1, int(c * .09)))
            return
        hc = mix(col, (255, 255, 255), .25)
        if dimmed:
            hc = mix(hc, St['floor'], .55)
        if k in ring:
            r = c * 1.25
            d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=GOLD + (255,), width=max(2, int(c * .14)))
        if kind == 'cycle':
            # 光轮：沿行进方向的胶囊，白热芯 + 前灯
            L, Wd = c * .78, c * .46
            ux, uy = DX[dd], DY[dd]
            x0, x1 = sorted((cx - ux * L * .55, cx + ux * L * .55))
            y0, y1 = sorted((cy - uy * L * .55, cy + uy * L * .55))
            box = [x0 - (Wd if ux == 0 else 0), y0 - (Wd if uy == 0 else 0), x1 + (Wd if ux == 0 else 0), y1 + (Wd if uy == 0 else 0)]
            d.rounded_rectangle(box, Wd * .8, fill=hc, outline=(255, 255, 255, 255 if not dimmed else 110), width=max(1, int(c * .06)))
            core = c * .17
            d.ellipse([cx - core, cy - core, cx + core, cy + core], fill=(255, 255, 255, 255 if not dimmed else 120))
            tx, ty = cx + ux * L * .7, cy + uy * L * .7
            g = c * .13
            d.ellipse([tx - g, ty - g, tx + g, ty + g], fill=(255, 255, 255, 240 if not dimmed else 100))
        elif kind == 'block':
            s = c * 1.12
            d.rounded_rectangle([cx - s / 2, cy - s / 2, cx + s / 2, cy + s / 2], s * .24, fill=hc,
                                outline=(255, 255, 255, 235 if not dimmed else 90), width=max(1, int(c * .08)))
            ux, uy, w_ = DX[dd], DY[dd], s * .2
            tx, ty = cx + ux * s * .5, cy + uy * s * .5
            px, py = -uy, ux
            d.polygon([(tx + ux * w_ * 1.1, ty + uy * w_ * 1.1), (tx + px * w_, ty + py * w_), (tx - px * w_, ty - py * w_)],
                      fill=(255, 255, 255, 235 if not dimmed else 90))
            fs = c * (.56 if k < 10 else .46)
            d.text((cx, cy + c * .03), str(k), font=font(fs, 'Black'), fill=(8, 10, 16, 255 if not dimmed else 140), anchor='mm')
        else:
            s = int(c * 1.75)
            rw = max(2, int(c * .15))
            ux, uy = DX[dd], DY[dd]
            # 行进方向的小三角在色环外
            tx, ty = cx + ux * (s / 2 + rw * .6), cy + uy * (s / 2 + rw * .6)
            px, py, w_ = -uy, ux, s * .2
            d.polygon([(tx + ux * w_ * 1.2, ty + uy * w_ * 1.2), (tx + px * w_, ty + py * w_), (tx - px * w_, ty - py * w_)],
                      fill=col + (255 if not dimmed else 110,))
            d.ellipse([cx - s / 2 - rw, cy - s / 2 - rw, cx + s / 2 + rw, cy + s / 2 + rw], fill=hc + (255,))
            av = avatar(self.who[k], s, col)
            if dimmed:
                av = av.copy()
                av.putalpha(av.getchannel('A').point(lambda q: q // 2))
            img.paste(av, (int(cx - s / 2), int(cy - s / 2)), av)

    def _av_px(self, St, c):
        """混搭风格的头像直径（超采样后的像素）：av_cells 格，且不超过 av_max 成片像素。"""
        return max(8, int(min(c * St['av_cells'], St['av_max'] * getattr(self, '_ss', 1))))

    def _head_avneon(self, img, d, h, St, c, cx, cy, ring):
        """霓虹头像混搭的头：角色头像 + 选手色细光环（外圈光晕在发光层）+ 行进方向小光点；位置按回合间插值平滑移动。
        出局：头像留在撞点、缩到 0.9、变灰，光环变暗，红色细线叉（白芯，发光层上另有一层红光）；撞上瞬间光环闪白。"""
        k, st, dimmed, quiet = h['k'], h['st'], h['dimmed'], h['quiet']
        col = self.col[k]
        if quiet:
            col = mix(col, St['floor'], .4)
        ss = getattr(self, '_ss', 1)
        s = self._av_px(St, c)
        hair = max(1, int(round(ss * .9)))                      # 发丝线 ≈ 成片 1 像素
        rw = max(hair * 2, int(c * .07))                       # 光环宽
        if st == 'dead':
            age = h['age']
            s = int(s * lerp(1.0, .9, ease(age / .5)))
            r = s / 2
            fl = clamp(1 - age / .35)                            # 撞上瞬间光环闪白
            ringc = mix(mix(col, St['floor'], .55), (255, 255, 255), fl)
            if dimmed:
                ringc = mix(ringc, St['floor'], .5)
            d.ellipse([cx - r - rw, cy - r - rw, cx + r + rw, cy + r + rw], fill=(8, 10, 16, 255))
            av = avatar(self.who[k], s, col, gray=True).copy()
            a = .55 if dimmed else .85
            av.putalpha(av.getchannel('A').point(lambda q: int(q * a)))
            img.paste(av, (int(cx - s / 2), int(cy - s / 2)), av)
            d.ellipse([cx - r - rw / 2, cy - r - rw / 2, cx + r + rw / 2, cy + r + rw / 2], outline=ringc + (255,),
                      width=max(hair, rw // 2))
            xr = r * .52
            red = (255, 92, 80) if not dimmed else (150, 70, 66)
            w = max(hair * 2, int(c * .075))
            for sgn in (1, -1):
                seg = [(cx - xr, cy - sgn * xr), (cx + xr, cy + sgn * xr)]
                d.line(seg, fill=red + (255,), width=w)
                d.line(seg, fill=(255, 225, 220, 200), width=hair)
            return
        r = s / 2
        bright = mix(col, (255, 255, 255), .2)
        if dimmed:
            bright = mix(bright, St['floor'], .55)
        if k in ring:
            rr = r + rw * 3.2
            d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], outline=GOLD + (255,), width=max(hair * 2, int(c * .1)))
        ux, uy = DX[h['d']], DY[h['d']]                         # 行进方向：光环外一颗小光点
        tx, ty = cx + ux * (r + rw * 2.2), cy + uy * (r + rw * 2.2)
        g = max(hair * 1.5, c * .09)
        d.ellipse([tx - g, ty - g, tx + g, ty + g], fill=mix(bright, (255, 255, 255), .5) + (230 if not dimmed else 110,))
        d.ellipse([cx - r - rw, cy - r - rw, cx + r + rw, cy + r + rw], fill=bright + (255,))
        av = avatar(self.who[k], s, col)
        if dimmed or quiet:
            av = av.copy()
            a = .5 if dimmed else .7
            av.putalpha(av.getchannel('A').point(lambda q: int(q * a)))
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(10, 12, 20, 255))
        img.paste(av, (int(cx - s / 2), int(cy - s / 2)), av)
        d.ellipse([cx - r - hair, cy - r - hair, cx + r + hair, cy + r + hair], outline=(255, 255, 255, 150 if not dimmed else 60),
                  width=hair)

    @staticmethod
    def _cross(d, cx, cy, r, col, w):
        d.line([(cx - r, cy - r), (cx + r, cy + r)], fill=col + (255,) if len(col) == 3 else col, width=w)
        d.line([(cx - r, cy + r), (cx + r, cy - r)], fill=col + (255,) if len(col) == 3 else col, width=w)

    def _labels(self, d, f, St, c, P, heads, tags, ss):
        """文字：抢格 / 互换 / 出局原因的小字条，名字标签。"""
        fs = max(13 * ss, c * .78)
        for e in self.events:
            a = f - (e['t'] + .5)
            if not 0 <= a < 2.2:
                continue
            al = int(255 * min(1, (2.2 - a) / .6))
            if e['type'] == 'contest':
                cx, cy = P(e['x'], e['y'])
                self._pill(d, cx, cy - c * (2.75 if tags else 1.7) - c * .3 * ease_out(a), '抢格', fs, GOLD, al)
            elif e['type'] == 'swap':
                (ax, ay), (bx, by) = self.path[e['a']][-1], self.path[e['b']][-1]
                cx, cy = P((ax + bx) / 2, (ay + by) / 2)
                self._pill(d, cx, cy - c * (2.9 if tags else 1.9), '互换', fs, SWAPC, al)
        if tags:
            for h in heads:
                if h['st'] == 'dead':                    # 出局后名字标签立即撤掉，只留 ✕（避免和抢格 / 赢家的标签叠在一起）
                    continue
                cx, cy = P(h['x'], h['y'])
                al = 255
                col = self.col[h['k']] if not h['dimmed'] else mix(self.col[h['k']], St['floor'], .5)
                self._pill(d, cx, cy - c * 1.25, self.label(h['k']), fs * .82, col, al, bg=(8, 10, 16))

    @staticmethod
    def _pill(d, cx, cy, s, fs, col, al, bg=(10, 12, 20)):
        fnt = font(fs, 'Bold')
        w = fnt.getlength(s)
        h = fs * 1.35
        d.rounded_rectangle([cx - w / 2 - fs * .45, cy - h / 2, cx + w / 2 + fs * .45, cy + h / 2], h * .35,
                            fill=bg + (int(al * .85),), outline=col + (al,), width=max(1, int(fs * .09)))
        d.text((cx, cy), s, font=fnt, fill=col + (al,), anchor='mm')

    # ---------- 侧栏 / 角标
    def shown_turn(self, f):
        return int(clamp(math.floor(f + 1e-6), 0, self.T))

    def death_text(self, k):
        e = self.deaths.get(k)
        if not e:
            return ''
        c = e['cause']
        if c == 'trail':
            if e['swap'] >= 0:
                return f'与 {self.label(e["swap"])} 互换'
            return f'撞 {self.label(e["hit"])}' + ('的头' if e['head'] else '')
        if c == 'headon':
            return f'抢格输给 {self.label(e["winner"])}'
        return CAUSE_CN.get(c, c)

    def scoreboard(self, f, w, h, highlight=None, vor=False, style=None, sort=False):
        """侧栏，返回 w×h 透明 RGBA。顶部：第 N 回合 / 共 T、存活 a / N；下面每人一行：色条、头像、名字、
        状态（存活 · 长度，或 第 t 回合出局 · 死因），终局写名次；vor=True 时每行加地盘格数和占比条。
        highlight：要高亮的座位集合。sort=True：活人在前（按地盘或长度），死者按出局先后排在后面。"""
        St = STYLES[style or self.style]
        highlight = set(highlight or ())
        img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        k_ = h / 816
        kf = max(k_, min(1.0, k_ ** .6))
        d.rounded_rectangle([0, 0, w - 1, h - 1], int(18 * k_), fill=St['panel'], outline=St['panel_line'], width=2)
        pad = int(24 * min(1, w / 520))
        t = self.shown_turn(f)
        end = f >= self.T
        alive = [k for k in range(self.N) if self.alive_at(k, f)]
        d.text((pad, 40 * k_), f'第 {t} 回合', font=font(36 * kf), fill=INK, anchor='lm')
        d.text((pad + font(36 * kf).getlength(f'第 {t} 回合') + 12 * kf, 44 * k_), f'/ {self.T}',
               font=font(22 * kf, 'Medium'), fill=MUTE, anchor='lm')
        s = f'存活 {len(alive)} / {self.N}'
        fw = font(24 * kf).getlength(s)
        d.rounded_rectangle([w - pad - fw - 26 * kf, 40 * k_ - 19 * kf, w - pad, 40 * k_ + 19 * kf], 10 * kf,
                            fill=(255, 255, 255, 14), outline=St['accent'] + (150,), width=2)
        d.text((w - pad - 13 * kf, 40 * k_), s, font=font(24 * kf), fill=St['accent'], anchor='rm')
        top = 80 * k_
        d.line([(pad, top), (w - pad, top)], fill=St['accent'] + (120,), width=max(1, int(2 * k_)))
        top += 10 * k_
        vr = self.vor(int(clamp(math.floor(f + .5), 0, self.T))) if vor else None
        rows = list(range(self.N))
        if sort:
            al = [k for k in rows if self.alive_at(k, f)]
            al.sort(key=lambda k: -(vr[1][k] if vr else self.length_at(k, f)))
            dead = sorted([k for k in rows if not self.alive_at(k, f)], key=lambda k: -self.dt[k])
            rows = al + dead
        nrow = max(self.N, 4)
        rh = min((h - top - 10 * k_) / nrow, 150 * k_)
        big = rh > 90 * k_
        for i, k in enumerate(rows):
            y0 = top + i * rh
            self._row(img, k, f, pad, y0, w - pad, y0 + rh - 6 * k_, kf, St, vr, end, k in highlight, big)
        return img

    def _row(self, img, k, f, x0, y0, x1, y1, kf, St, vr, end, hl, big):
        d = ImageDraw.Draw(img)
        col = self.col[k]
        al = self.alive_at(k, f)
        age = self.death_age(k, f)
        rh = y1 - y0
        flash = 0 <= age < 1.6 and int(age * 5) % 2 == 0
        bg = (255, 255, 255, 10) if al else (0, 0, 0, 40)
        if flash:
            bg = col + (70,)
        if hl:
            bg = GOLD + (40,)
        d.rounded_rectangle([x0, y0, x1, y1], int(rh * .18), fill=bg,
                            outline=(GOLD + (220,)) if hl or (end and self.rank[k] == 0) else None, width=2)
        d.rounded_rectangle([x0, y0, x0 + max(4, rh * .09), y1], int(rh * .1), fill=col if al else mix(col, (30, 34, 44), .55))
        ah = int(rh * (.72 if big else .78))
        ax = int(x0 + rh * .18 + 4)
        av = avatar(self.who[k], ah, col, gray=not al)
        img.alpha_composite(av, (ax, int((y0 + y1) / 2 - ah / 2)))
        d = ImageDraw.Draw(img)
        nx = ax + ah + rh * .16
        fs = min(rh * (.40 if not big else .3), 32 * kf)
        name_c = mix(col, (255, 255, 255), .35) if al else (120, 126, 140)
        cy1, cy2 = y0 + rh * .33, y0 + rh * .72
        d.text((nx, cy1), self.label(k), font=font(fs), fill=name_c, anchor='lm')
        sfs = fs * .8
        if al:
            st = ('活到最后' if end else '存活') + f' · 长 {self.length_at(k, f)}'
            sc = (130, 220, 160)
        else:
            st = f'第 {self.dt[k]} 回合 · {self.death_text(k)}'
            sc = (255, 150, 150) if flash or age < 1.6 else (170, 120, 124)
        d.text((nx, cy2), st, font=font(sfs, 'Medium'), fill=sc, anchor='lm')
        right = x1 - rh * .15
        if end:
            tie = sum(1 for j in range(self.N) if j != k and self.rank[j] == self.rank[k])
            rt = f'{"并列" if tie else ""}第 {self.rank[k] + 1} 名'
            d.text((right, cy1), rt, font=font(fs * .8), fill=GOLD if self.rank[k] == 0 else GREY, anchor='rm')
        if vr is not None:
            own, cnt, free = vr
            n = cnt[k] if al else 0
            bw = (x1 - x0) * .26
            bx1 = right
            bx0 = bx1 - bw
            by = cy2 if not end else cy2
            d.rounded_rectangle([bx0, by - sfs * .32, bx1, by + sfs * .32], sfs * .3, fill=(255, 255, 255, 18))
            if n and free:
                d.rounded_rectangle([bx0, by - sfs * .32, bx0 + max(sfs * .6, bw * n / free), by + sfs * .32], sfs * .3,
                                    fill=col + (230,))
            if not end:
                d.text((bx0 - 8 * kf, cy1), f'地盘 {n}', font=font(sfs, 'Medium'), fill=(200, 206, 220) if al else MUTE, anchor='lm')
            pct = f'{100 * n / free:.0f}%' if free and al else '-'
            d.text((bx0 - 8 * kf, by), pct, font=font(sfs, 'Medium'), fill=(200, 206, 220) if al else MUTE, anchor='rm')

    def hud(self, f, h=56, style=None, vor=False):
        """角标：一条横向小胶囊"第 N 回合 / T · 存活 a / N"（vor=True 时加"地盘视图"标），高 h，宽按内容。"""
        St = STYLES[style or self.style]
        t = self.shown_turn(f)
        alive = sum(self.alive_at(k, f) for k in range(self.N))
        parts = [(f'第 {t} 回合', INK), (f'/ {self.T}', MUTE), ('·', MUTE), (f'存活 {alive} / {self.N}', St['accent'])]
        if vor:
            parts += [('·', MUTE), ('地盘视图', GOLD)]
        fs = h * .46
        ws = [font(fs).getlength(s) for s, _ in parts]
        w = int(sum(ws) + fs * .5 * (len(parts) - 1) + h * .8)
        img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        d.rounded_rectangle([0, 0, w - 1, h - 1], h * .3, fill=St['panel'][:3] + (220,), outline=St['panel_line'], width=2)
        x = h * .4
        for (s, cc), ww in zip(parts, ws):
            d.text((x, h / 2), s, font=font(fs), fill=cc, anchor='lm')
            x += ww + fs * .5
        return img


# ---------------------------------------------------------------- 1920×1080 整帧（样张 / 命令行出片用）
_bg = {}


def background():
    if 'bg' not in _bg:
        yy, xx = np.mgrid[0:H1080, 0:W1080].astype(np.float32)
        base = np.stack([12 + yy / 140, 16 + yy / 110, 28 + yy / 70], -1)
        g = np.exp(-(((xx - 700) / 900) ** 2 + ((yy - 420) / 600) ** 2))
        base += g[..., None] * np.array([10, 16, 36], np.float32)
        _bg['bg'] = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8), 'RGB').convert('RGBA')
    return _bg['bg']


BRAND_H = 46
SAFE_BOTTOM = 936                     # 第 6 集惯例：y 936 以下是字幕区和节目条


def brand(img):
    if 'brand' not in _bg:
        bar = Image.new('RGBA', (W1080, BRAND_H), (8, 10, 18, 235))
        dd = ImageDraw.Draw(bar)
        dd.line([(0, 0), (W1080, 0)], fill=(255, 200, 80, 160), width=2)
        dd.text((W1080 - 28, BRAND_H // 2), EPISODE, font=font(24), fill=INK, anchor='rm')
        _bg['brand'] = bar
    img.alpha_composite(_bg['brand'], (0, H1080 - BRAND_H))


def title(img, rp, sub=None, note=None):
    d = ImageDraw.Draw(img)
    cy = 56
    if rp.N == 2:
        l, r = rp.label(0), rp.label(1)
        fs = 44
        wl, wr, wv = font(fs).getlength(l), font(fs).getlength(r), font(32, 'Medium').getlength('vs')
        tot = 64 + 16 + wl + 30 + wv + 30 + wr + 16 + 64
        x = W1080 / 2 - tot / 2
        for k, ax in ((0, x), (1, x + tot - 64)):
            img.alpha_composite(avatar(rp.who[k], 64, rp.col[k]), (int(ax), cy - 32))
        d = ImageDraw.Draw(img)
        d.text((x + 80, cy), l, font=font(fs), fill=rp.col[0], anchor='lm')
        d.text((x + 80 + wl + 30 + wv / 2, cy + 2), 'vs', font=font(32, 'Medium'), fill=GREY, anchor='mm')
        d.text((x + 80 + wl + 60 + wv, cy), r, font=font(fs), fill=rp.col[1], anchor='lm')
    else:
        s = sub or f'{rp.N} 人局'
        d.text((W1080 / 2, cy), s, font=font(44), fill=INK, anchor='mm')
    if note:
        d.text((W1080 - 28, cy), note, font=font(22, 'Medium'), fill=GREY, anchor='rm')


def frame(rp, f, style=None, vor=False, sub=None, note=None, board_size=816, sbw=560, ss=2, view=None, ring=None, focus=None):
    """整帧版式（同第 6 集 match_full）：顶部标题，左大棋盘，右侧栏，底部节目条；y 936 以下留给字幕。"""
    img = background().copy()
    title(img, rp, sub=sub, note=note)
    gap = 48
    x0 = int((W1080 - board_size - gap - sbw) / 2)
    y0 = 112
    size = min(board_size, SAFE_BOTTOM - 8 - y0)
    sh = Image.new('RGBA', (size + 80, size + 80), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([40, 52, size + 40, size + 52], 18, fill=(0, 0, 0, 150))
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(16)), (x0 - 40, y0 - 40))
    img.alpha_composite(rp.draw(f, size, vor=vor, style=style, ss=ss, view=view, ring=ring, focus=focus), (x0, y0))
    img.alpha_composite(rp.scoreboard(f, sbw, size, vor=vor, style=style), (x0 + size + gap, y0))
    brand(img)
    return img
