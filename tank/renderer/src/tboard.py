"""坦克大战回放渲染：读回放，按任意小数回合画出棋盘（带补间动画）。
回放可以是比赛工具存的 tank-replay-1（.json / .json.gz），也可以是仓库 replays/ 里的 .html
（里面是 arena.py expand 展开的 tank-frames-1，载入时先还原成起始局面 + 每回合命令）。
接口和第 3 集夺旗的渲染器同形：
    rp = TReplay(path)                  # 回放只存起始局面和命令，载入时用 tsim.py 逐回合重算（与官方引擎逐字一致）
    img = rp.draw(f, size)              # f 可以是小数：12.5 = 第 12 回合结算完、第 13 回合进行到一半
    sb = rp.scoreboard(f, w, h)         # 比分栏：双方基地血条、各车血量等级、击毁 / 吃星、回合数
回放帧约定：frames[t] 是第 t 回合结算完的局面；frames[t]['events'] / ['orders'] 是第 t 回合里的事件和命令。
画 f∈[t, t+1) 时从 frames[t] 过渡到 frames[t+1]，用第 t+1 回合的小步轨迹 traces[t+1]：
    回合内时刻 τ = f - t。τ∈[0, .5) 是小步 1，[.5, 1) 是小步 2（子弹每小步 1 格，坦克每小步最多 1 步），
    回合末的事（被毁、复活、吃星、出星）发生在 τ = 1，特效延续到下一回合开头。
"""
import gzip, json, math, os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:                     # python -I 不把脚本目录放进 sys.path
    sys.path.insert(0, HERE)
import tsim
import common

# 回放里的名字 → 片中称呼（label 用）。没列出的原样显示
NAME = {'Qwen': '千问', 'Doubao': '豆包', 'baseline': '基准', 'sparring': '陪练', 'SolUF': 'Sol Ultrafast'}
# 选手代表色。前 12 个沿用第 3 集夺旗的配色（千问、豆包、基准改用回放里的名字作键，旧键也保留）
CHAR = {'Astra': (100, 205, 130), 'Sol': (240, 210, 70), 'Opus': (240, 150, 60), 'Sonnet': (255, 185, 150),
        'Fable': (225, 110, 230), 'DeepSeek': (70, 140, 255), 'Qwen': (135, 115, 250), 'Kimi': (215, 215, 240),
        'GLM': (60, 200, 210), 'MiMo': (235, 70, 60), 'Doubao': (250, 120, 150), 'baseline': (130, 136, 150),
        # 本期新增
        'SolUF': (255, 236, 160), 'Haiku': (175, 225, 70), 'Muse': (225, 110, 230), 'Gemini': (110, 175, 255),
        'Grok': (235, 235, 245), 'sparring': (176, 142, 104)}
CHAR.update({'千问': CHAR['Qwen'], '豆包': CHAR['Doubao'], '基准': CHAR['baseline']})
# 同一局两方颜色太近时，1 号方改用下面这组里离 0 号方最远的一个
SPARE = [(235, 70, 60), (70, 140, 255), (100, 205, 130), (240, 210, 70), (225, 110, 230), (60, 200, 210),
         (250, 250, 250), (255, 140, 40)]
# 头像文件名（renderer/assets/avatars/<名>.png）；没有头像的用首字母圆片占位
AVATAR_FILE = {'Qwen': '千问', 'Doubao': '豆包'}

TYPE_CN = ['快速', '火力', '装甲']
FLOOR_A, FLOOR_B = (28, 35, 50), (32, 40, 56)
GRID = (40, 49, 68)
GOLD = (255, 210, 74)
RED = (255, 90, 77)
DV = tsim.DV
ANG = {'U': 0, 'L': 90, 'D': 180, 'R': -90}           # PIL rotate 的角度（逆时针为正），精灵默认朝上

font = common.font


def ease(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3 - 2 * x)


def lerp(a, b, k):
    return a + (b - a) * k


def mix(c1, c2, k):
    return tuple(int(lerp(a, b, k)) for a, b in zip(c1, c2))


def clamp(v, a=0.0, b=1.0):
    return a if v < a else b if v > b else v


def fade(im, a):
    if a >= 1:
        return im
    im = im.copy()
    im.putalpha(im.getchannel('A').point(lambda q: int(q * max(0.0, a))))
    return im


def rnd(i, j):
    v = math.sin(i * 127.1 + j * 311.7) * 43758.5453
    return v - math.floor(v)


def canon(n):
    """回放里的名字 → CHAR 的键。"""
    base = n.split('#')[0].strip()
    if base in CHAR:
        return base
    low = base.lower()
    for k in CHAR:
        if k.lower() == low:
            return k
    return base


def color_of(who):
    return CHAR.get(who, (150, 150, 150))


_av = {}


def avatar(who, px):
    """圆形头像；没有素材的角色用首字母圆片。"""
    px = max(4, int(px))
    key = (who, px)
    if key in _av:
        return _av[key]
    src = None if who in ('baseline', '基准', 'sparring') else common.avatar_path(AVATAR_FILE.get(who, who))
    if src:
        im = common.round_avatar(src, px)
    else:
        S = 128
        im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        if who in ('baseline', '基准', 'sparring'):
            d.ellipse([0, 0, S - 1, S - 1], fill=mix(color_of(who), (40, 44, 56), .3))
            d.ellipse([S * .28, S * .3, S * .42, S * .44], fill=(225, 228, 236))
            d.ellipse([S * .58, S * .3, S * .72, S * .44], fill=(225, 228, 236))
            d.rounded_rectangle([S * .3, S * .6, S * .7, S * .68], S * .04, fill=(225, 228, 236))
        else:
            d.ellipse([0, 0, S - 1, S - 1], fill=mix(color_of(who), (20, 24, 36), .35))
            d.text((S / 2, S / 2), who[:1], font=font(70, 'Black'), fill=(255, 255, 255), anchor='mm')
        im = im.resize((px, px), Image.LANCZOS)
    _av[key] = im
    return im


def star_poly(cx, cy, r, rot=0.0, inner=.45):
    return [(cx + (r if k % 2 == 0 else r * inner) * math.cos(math.pi / 5 * k - math.pi / 2 + rot),
             cy + (r if k % 2 == 0 else r * inner) * math.sin(math.pi / 5 * k - math.pi / 2 + rot)) for k in range(10)]


# ---------------------------------------------------------------- 坦克精灵
_spr = {}


def tank_sprite(kind, col, px, variant='', flat=False):
    """俯视坦克，朝上。kind 0/1/2 = 快速/火力/装甲：
    快速 = 窄车身尖头、单根细炮管、车尾两道速度条纹；火力 = 圆炮塔双炮管；装甲 = 最宽、方炮塔粗短炮管、前后装甲板。
    variant：'' 正常；'flash' 中弹闪白；'dead' 烧黑。"""
    px = max(8, int(px))
    key = (kind, col, px, variant, flat)
    if key in _spr:
        return _spr[key]
    S = px * 2                                     # 先画两倍再缩，边缘平滑
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    X = lambda u: (u + .5) * S
    R = lambda x0, y0, x1, y1: [X(x0), X(y0), X(x1), X(y1)]
    lw = max(1, int(S * .03))
    if variant == 'flash':
        body = turret = barrel = track = (255, 255, 255)
        dark = (230, 230, 230)
    elif variant == 'dead':
        body, turret, barrel, track, dark = (58, 54, 52), (74, 68, 64), (90, 86, 82), (30, 28, 28), (20, 18, 18)
    else:
        body = col
        turret = mix(col, (255, 255, 255), .3)
        barrel = (226, 230, 238)
        track = (34, 38, 50)
        dark = mix(col, (8, 10, 18), .65)
    ink = (10, 12, 20)
    tw = {0: (.21, .33, -.34, .36), 1: (.27, .41, -.36, .38), 2: (.32, .48, -.42, .44)}[kind]
    for sx in (-1, 1):                              # 履带
        a, b = sorted((sx * tw[0], sx * tw[1]))
        d.rounded_rectangle(R(a, tw[2], b, tw[3]), S * .03, fill=track, outline=ink, width=lw)
        y = tw[2] + .05
        while y < tw[3] - .03 and not flat:
            d.line([(X(a) + lw, X(y)), (X(b) - lw, X(y))], fill=mix(track, (0, 0, 0), .5) if variant != 'flash' else (220, 220, 220),
                   width=max(1, int(S * .022)))
            y += .09
    if kind == 0:
        hull = [(-.2, .36), (-.2, -.06), (0, -.36), (.2, -.06), (.2, .36)]
        d.polygon([(X(a), X(b)) for a, b in hull], fill=body, outline=ink, width=lw)
        for k in (0, 1):                           # 车尾速度条纹
            y = .3 - k * .1
            d.line([(X(-.14), X(y)), (X(0), X(y - .07)), (X(.14), X(y))], fill=dark, width=max(1, int(S * .035)))
        d.rectangle(R(-.035, -.52, .035, -.02), fill=barrel, outline=ink, width=max(1, lw // 2))
        d.ellipse(R(-.13, -.07, .13, .19), fill=turret, outline=ink, width=lw)
    elif kind == 1:
        d.rounded_rectangle(R(-.27, -.32, .27, .36), S * .04, fill=body, outline=ink, width=lw)
        d.line([(X(-.2), X(.29)), (X(.2), X(.29))], fill=dark, width=max(1, int(S * .03)))
        for sx in (-1, 1):
            a, b = sorted((sx * .05, sx * .12))
            d.rectangle(R(a, -.53, b, -.02), fill=barrel, outline=ink, width=max(1, lw // 2))
            a2, b2 = sorted((sx * .035, sx * .135))
            d.rectangle(R(a2, -.55, b2, -.47), fill=barrel, outline=ink, width=max(1, lw // 2))
        d.ellipse(R(-.2, -.13, .2, .27), fill=turret, outline=ink, width=lw)
        d.ellipse(R(-.07, .0, .07, .14), fill=dark)
    else:
        d.rectangle(R(-.35, -.41, .35, .43), fill=body, outline=ink, width=lw)
        d.rectangle(R(-.35, -.41, .35, -.31), fill=dark, outline=ink, width=lw)
        d.rectangle(R(-.35, .33, .35, .43), fill=dark, outline=ink, width=lw)
        for sx in (-.27, .27):
            for sy in (-.22, .24):
                d.ellipse(R(sx - .03, sy - .03, sx + .03, sy + .03), fill=dark)
        d.rectangle(R(-.075, -.5, .075, -.08), fill=barrel, outline=ink, width=lw)
        d.rounded_rectangle(R(-.22, -.16, .22, .26), S * .05, fill=turret, outline=ink, width=lw)
        d.line([(X(-.12), X(.17)), (X(.12), X(.17))], fill=dark, width=max(1, int(S * .03)))
    im = im.resize((px, px), Image.LANCZOS)
    _spr[key] = im
    return im


def rotated(im, ang):
    a = ang % 360
    if abs(a) < .01:
        return im
    for q, op in ((90, Image.ROTATE_90), (180, Image.ROTATE_180), (270, Image.ROTATE_270)):
        if abs(a - q) < .01:
            return im.transpose(op)
    return im.rotate(ang, resample=Image.BICUBIC, expand=False)


def angle_tween(a0, a1, k):
    d = (a1 - a0 + 180) % 360 - 180
    return a0 + d * k


# ---------------------------------------------------------------- 三种画面风格
# A 扁平极简：纯色块、墙体连片只描外轮廓、坦克只带柔和投影。
# B 复古像素：致敬 FC《坦克大战》，每格 16×16 像素画，整张图按最近邻放大；字和血量格在放大后再画，保持清晰。
# C 解说版（默认）：深蓝底 + 金色点缀（同第 3 集），墙体带一点 2.5D 厚度和投影，坦克有队伍色外发光，特效最醒目。
STYLES = {
    'A': dict(name='A', pixel=False, floor=(24, 30, 44), grid=None, vignette=False,
              brick=((66, 74, 92), (96, 106, 128), (56, 62, 78)), steel=((132, 144, 164), (178, 188, 206), (100, 110, 128)),
              water=((30, 58, 100), (62, 102, 158)), depth=False, glow=0, shadow=110, tank_scale=1.0, flat=True,
              panel=(16, 21, 33, 235), panel_line=(255, 255, 255, 30), accent=(255, 206, 84)),
    'B': dict(name='B', pixel=True, floor=(14, 14, 18), grid=None, vignette=False, depth=False, glow=0, shadow=0,
              tank_scale=1.0, flat=False, panel=(10, 10, 14, 240), panel_line=(150, 150, 160, 200), accent=(252, 196, 60)),
    'C': dict(name='C', pixel=False, floor=(19, 25, 39), grid=(255, 255, 255, 9), vignette=True,
              brick=((150, 96, 74), (178, 122, 96), (96, 60, 46)), steel=((124, 138, 160), (170, 182, 200), (72, 82, 100)),
              water=((28, 70, 124), (84, 140, 206)), depth=True, glow=95, shadow=130, tank_scale=1.04, flat=False,
              panel=(16, 21, 34, 225), panel_line=(255, 206, 84, 70), accent=(255, 206, 84)),
}
DEFAULT_STYLE = 'C'
PIX = 16                                     # B 风格每格的像素数

# B 风格的坦克像素画（朝上）。k 描边，b 车身（队伍色），l 亮部，d 暗部，t 履带，g 炮管
PIX_TANK = {
    0: ["......kggk......", "......kggk......", "......kggk......", "...tt.kggk.tt...",
        "...ttkbllbktt...", "...ttkbllbktt...", "...tkbbllbbkt...", "...tkbbbbbbkt...",
        "...tkbdbbdbkt...", "...tkbbddbbkt...", "...tkbbbbbbkt...", "...tkbdbbdbkt...",
        "...tkbbddbbkt...", "...tkkbbbbkkt...", "...tt.kkkk.tt...", "................"],
    1: [".....g....g.....", ".....g....g.....", ".....g....g.....", "..tt.g....g.tt..",
        "..ttkgkkkkgktt..", "..ttkbbllbbktt..", "..ttkbllllbktt..", "..ttkbllllbktt..",
        "..ttkbbllbbktt..", "..ttkbbbbbbktt..", "..ttkbdbbdbktt..", "..ttkbbbbbbktt..",
        "..ttkbdbbdbktt..", "..ttkkkkkkkktt..", "..tt........tt..", "................"],
    2: ["................", ".....kggggk.....", ".....kggggk.....", ".tttkkggggkkttt.",
        ".tttkddddddkttt.", ".tttkbbbbbbkttt.", ".tttkbllllbkttt.", ".tttkbllllbkttt.",
        ".tttkbllllbkttt.", ".tttkbbbbbbkttt.", ".tttkbdbbdbkttt.", ".tttkbbbbbbkttt.",
        ".tttkddddddkttt.", ".tttkkkkkkkkttt.", ".ttt........ttt.", "................"],
}


def pix_tank(kind, col, variant=''):
    key = ('pix', kind, col, variant)
    if key in _spr:
        return _spr[key]
    if variant == 'flash':
        pal = dict(k=(255, 255, 255), b=(255, 255, 255), l=(255, 255, 255), d=(230, 230, 230), t=(220, 220, 220), g=(255, 255, 255))
    elif variant == 'dead':
        pal = dict(k=(10, 10, 10), b=(70, 64, 60), l=(90, 84, 80), d=(44, 40, 38), t=(36, 34, 34), g=(100, 96, 92))
    else:
        pal = dict(k=(8, 8, 12), b=col, l=mix(col, (255, 255, 255), .45), d=mix(col, (0, 0, 0), .45), t=(96, 96, 104),
                   g=(232, 232, 236))
    im = Image.new('RGBA', (PIX, PIX), (0, 0, 0, 0))
    px = im.load()
    for y, row in enumerate(PIX_TANK[kind]):
        for x, ch in enumerate(row):
            if ch == '.':
                continue
            c = pal[ch]
            if ch == 't' and y % 2:
                c = mix(c, (0, 0, 0), .45)
            px[x, y] = c + (255,)
    _spr[key] = im
    return im


def pix_tile(kind, gx=0, gy=0):
    """B 风格的地形贴图（16×16）。"""
    key = ('tile', kind, (gx + gy) % 2 if kind == '~' else 0)
    if key in _spr:
        return _spr[key]
    im = Image.new('RGBA', (PIX, PIX), (0, 0, 0, 0))
    px = im.load()
    for y in range(PIX):
        for x in range(PIX):
            if kind == '#':                      # 砖：两排错缝，压暗的砖红
                row = y // 4
                mortar = y % 4 == 3 or (x + (4 if row % 2 else 0)) % 8 == 7
                c = (70, 66, 64) if mortar else ((176, 92, 56) if y % 4 == 0 else (136, 66, 40))
            elif kind == '@':                    # 钢：四块凸起的钢板
                sx, sy = x % 8, y % 8
                if sx == 7 or sy == 7:
                    c = (70, 74, 84)
                elif sx == 0 or sy == 0:
                    c = (236, 238, 244)
                elif 2 <= sx <= 5 and 2 <= sy <= 5:
                    c = (210, 214, 222)
                else:
                    c = (160, 166, 178)
            else:                                # 水：深蓝底 + 浅色波纹点
                ph = key[2] * 4
                wave = (y % 8 == 2 and (x + ph) % 8 in (1, 2, 3)) or (y % 8 == 6 and (x + ph + 4) % 8 in (1, 2, 3))
                c = (110, 170, 240) if wave else (32, 72, 168)
            px[x, y] = c + (255,)
    _spr[key] = im
    return im


# ---------------------------------------------------------------- 回放
def frames_to_replay(fr):
    """tank-frames-1（arena.py expand 的逐帧展开，仓库 replays/*.html 里内嵌的就是它）→ tank-replay-1。
    只取第 0 帧拼出起始局面文本（格式同 tank.hpp 的 TANK-STATE 1），再取每一帧的双方命令；
    逐帧局面留着，载入后用来和 tsim 的重算逐回合核对。"""
    P, f0 = fr['params'], fr['frames'][0]
    rows = ['TANK-STATE 1']
    for k, v in P.items():
        rows.append(f'P {k} {len(v)} ' + ' '.join(v) if isinstance(v, list) else f'P {k} {v}')
    rows.append('BASES ' + ' '.join(f'{x} {y}' for x, y in fr['bases']))
    rows.append('SPAWNS ' + ' '.join(f'{x} {y}' for x, y in fr['spawns']))
    rows.append(f'SUPPLY {len(fr["supply"])} ' + ' '.join(f'{x} {y}' for x, y in fr['supply']))
    if f0['turn'] != 0 or f0['bullets'] or f0['stars'] or not all(t['alive'] for t in f0['tanks']):
        raise ValueError('tank-frames-1 的第 0 帧不是开局局面，无法还原')
    rows.append('TURN 0 0 -1 none')
    for word, key in (('BASEHP', 'base_hp'), ('KILLS', 'kills'), ('STARCOUNT', 'stars_eaten'), ('BASEDMG', 'base_dmg')):
        rows.append(f'{word} {f0[key][0]} {f0[key][1]}')
        if word == 'BASEHP':
            rows.append('BASEQUIET 0 0')
    rows.append('MAP')
    rows += f0['map']
    rows.append(f'TANKS {len(f0["tanks"])}')
    for t in f0['tanks']:
        rows.append(f"T {t['id']} {t['x']} {t['y']} {t['hp']} {t['level']} 1 0 {tsim.NEVER_FIRED}")
    rows += ['BULLETS 0', 'STARS 0', 'END']
    orders = [[' '.join(o[0]), ' '.join(o[1])] for o in (f['orders'] for f in fr['frames'][1:])]
    return {'format': 'tank-replay-1', 'start': chr(10).join(rows) + chr(10), 'orders': orders, 'names': fr['names'],
            'seed': fr.get('seed'), 'style': fr.get('style'), 'result': fr.get('result', {}), '_frames': fr['frames']}


class TReplay:
    def __init__(self, path, mine=None, names=None, style=DEFAULT_STYLE):
        self.style = style
        r = common.load_json_or_html(path)
        if r.get('format') == 'tank-frames-1':
            r = frames_to_replay(r)
        if r.get('format') != 'tank-replay-1':
            raise ValueError('只认 tank-replay-1 / tank-frames-1 格式的回放')
        self.path = path
        self.raw = r
        self.res = r.get('result', {})
        self.raw_names = names or r['names']
        self.who = [canon(n) if (canon(n) in CHAR or not mine) else mine for n in self.raw_names]
        self.teams = 2
        self.multi = {w for w in self.who if self.who.count(w) > 1}
        st = tsim.State.parse(r['start'])
        self.P = st.P
        self.N = st.n
        self.bases = list(st.base_pos)            # [(7,14), (7,0)]：0 号方在下
        self.spawns = list(st.spawn)
        self.supply = list(st.supply)
        self.spots = self.supply                  # 和夺旗渲染器同名
        self.maxhp_base = st.P['base_hp']
        self.frames = [self._snap(st, None, [])]
        self.traces = [None]
        for pair in r['orders']:
            tr = st.step(pair)
            self.traces.append(tr)
            self.frames.append(self._snap(st, pair, tr['events']))
        self.T = len(self.frames) - 1
        self.final_text = st.turn_text()
        if '_frames' in r:                        # 从逐帧展开还原来的：逐回合和展开里记的局面核对
            ref = r['_frames']
            if len(ref) != len(self.frames):
                raise ValueError(f'重算 {self.T} 回合，逐帧展开里是 {len(ref) - 1} 回合')
            for t, (a, b) in enumerate(zip(self.frames, ref)):
                mine = (a['map'], a['base_hp'], [(k['x'], k['y'], k['hp'], k['level']) if k['alive'] else None for k in a['tanks']])
                theirs = (b['map'], b['base_hp'], [(k['x'], k['y'], k['hp'], k['level']) if k['alive'] else None for k in b['tanks']])
                if mine != theirs:
                    raise ValueError(f'第 {t} 回合重算的局面和逐帧展开不一致')
        self._build_motion()
        # 两方颜色太近（含同一个选手自己打自己）时，1 号方换色
        c0, c1 = color_of(self.who[0]), color_of(self.who[1])
        if self._dist(c0, c1) < 115:
            c1 = max(SPARE, key=lambda c: self._dist(c, c0))
        self.col = [c0, c1]

    @staticmethod
    def _dist(a, b):
        """redmean 色差（比直接算 RGB 距离更接近人眼）。"""
        r = (a[0] + b[0]) / 2
        dr, dg, db = (x - y for x, y in zip(a, b))
        return math.sqrt((2 + r / 256) * dr * dr + 4 * dg * dg + (2 + (255 - r) / 256) * db * db)

    def _snap(self, st, pair, events):
        tanks = []
        for i, t in enumerate(st.tank):
            tanks.append(dict(id=i, seat=i // 3, type=i % 3, x=t['x'], y=t['y'], pos=(t['x'], t['y']) if t['alive'] else None,
                              hp=t['hp'], level=t['level'], alive=t['alive'],
                              maxhp=st.types[i % 3]['hp'] + t['level'] * st.P['level_hp'],
                              respawn_in=st.respawn_in(i), reload_left=st.reload_left(i) if t['alive'] else 0,
                              bullets_out=st.bullets_out(i), cap=st.bullet_cap(i), can_fire=st.can_fire(i)))
        return dict(turn=st.turn, map=[''.join(r) for r in st.grid], tanks=tanks,
                    bullets=[dict(x=b['x'], y=b['y'], dir=b['dir'], owner=b['owner'], steel=b['steel']) for b in st.bullet],
                    stars=[p for p, on in zip(st.supply, st.star) if on], base_hp=list(st.base_hp), kills=list(st.kills),
                    stars_eaten=list(st.stars_eaten), base_dmg=list(st.base_dmg), done=st.done, winner=st.winner,
                    reason=tsim.REASONS[st.reason] if st.done else 'none',
                    orders=pair.copy() if pair else None, events=[self._ev(e) for e in events])

    @staticmethod
    def _ev(text):
        w = text.split()
        fields = {"fire": ["tank", "dir"], "blocked": ["tank", "sub"], "cancel": ["how", "x", "y", "sub", "owner1", "owner2"],
                  "hit": ["tank", "by", "x", "y", "sub", "swap"], "base_hit": ["seat", "by", "sub"],
                  "wall": ["x", "y", "kind", "sub"], "death": ["tank", "x", "y", "by"], "respawn": ["tank", "x", "y"],
                  "bullet_cleared": ["x", "y", "tank"], "star": ["tank", "x", "y", "level"], "supply": ["x", "y"]}
        e = {'type': w[0], 'text': text}
        for k, v in zip(fields.get(w[0], []), w[1:]):
            e[k] = int(v) if v.lstrip('-').isdigit() else v
        return e

    def _build_motion(self):
        """朝向（规则里坦克没有朝向，按最近一次移动或开炮的方向画）、致命一击的时刻、特效时间表。"""
        default = ['U'] * 3 + ['D'] * 3
        self.facing = [default[:]]
        self.dirs = [None]                         # dirs[k][i] = (小步 1 朝向, 小步 2 朝向)
        self.lethal = [None]                       # lethal[k][i] = 第 k 回合里被打到 0 血的时刻（回合内）
        self.fx = [[]]                             # fx[k] = 第 k 回合的特效，A 是绝对时刻（回合 k 内 τ → k-1+τ）
        for k in range(1, self.T + 1):
            tr, cur, nxt = self.traces[k], self.frames[k - 1], self.frames[k]
            prev = self.facing[-1]
            cmds = [c for o in nxt['orders'] for c in o.split()]
            dirs, face = [], prev[:]
            for i in range(6):
                if not tr['alive0'][i]:
                    dirs.append((default[i], default[i]))
                    face[i] = default[i]
                    continue
                c = cmds[i]
                aim = c[1] if c[0] == 'F' and len(c) == 2 else None
                m0, m1 = tr['mv'][i]
                d1 = aim or m0 or prev[i]
                d2 = aim or m1 or d1
                dirs.append((d1, d2))
                face[i] = d2 if nxt['tanks'][i]['alive'] else default[i]
            self.dirs.append(dirs)
            self.facing.append(face)
            leth = {}
            for i in range(6):
                if not tr['alive0'][i]:
                    continue
                hp = cur['tanks'][i]['hp']
                for h in sorted(tr['hits'], key=lambda h: h['t']):
                    if h['tank'] == i:
                        hp -= 1
                        if hp <= 0:
                            leth[i] = h['t']
                            break
            self.lethal.append(leth)
            A0 = k - 1
            fx = []
            for i in range(6):
                if tr['fire'][i]:
                    x, y = tr['pos'][i][0]
                    fx.append(dict(A=A0 + .02, type='muzzle', x=x, y=y, dir=tr['fire'][i]))
            for h in tr['hits']:
                fx.append(dict(A=A0 + h['t'], type='hit', x=h['x'], y=h['y'], tank=h['tank'],
                               lethal=leth.get(h['tank']) == h['t']))
            for w in tr['walls']:
                fx.append(dict(A=A0 + w['t'], type='debris', x=w['x'], y=w['y'], kind=w['kind']))
            for b in tr['bullets']:
                if b['end'] is None or b['kind'] not in ('ding', 'out', 'brick', 'steel'):
                    continue
                dx, dy = DV[b['dir']]
                x, y = b['x0'] + dx * b['end'], b['y0'] + dy * b['end']
                fx.append(dict(A=A0 + b['end'] / 2, type='spark' if b['kind'] != 'out' else 'out', x=x, y=y, dir=b['dir'],
                               kind=b['kind']))
            for c in tr['cancels']:
                fx.append(dict(A=A0 + c['t'], type='cancel', x=c['x'], y=c['y']))
            for h in tr['base_hits']:
                bx, by = self.bases[h['seat']]
                fx.append(dict(A=A0 + h['t'], type='basehit', x=bx, y=by, seat=h['seat'], dmg=h['dmg']))
            for e in tr['ends']:
                if e['type'] == 'death':
                    i = e['tank']
                    x, y, t0 = e['x'], e['y'], 1.0
                    if e['by'] < 0 and i in leth:          # 从致命一击的时刻、在那一刻的位置炸开
                        t0 = leth[i]
                        u = 2 * t0
                        si = min(1, int(u))
                        p0, p1 = tr['pos'][i][si], tr['pos'][i][si + 1]
                        x, y = lerp(p0[0], p1[0], u - si), lerp(p0[1], p1[1], u - si)
                    fx.append(dict(A=A0 + t0, type='death', x=x, y=y, tank=i, by=e['by']))
                else:
                    fx.append(dict(A=A0 + 1.0, **e))
            self.fx.append(fx)

    # ---------- 取状态（与夺旗渲染器同名的几个）----------
    def frame(self, t):
        return self.frames[max(0, min(self.T, t))]

    def team_of(self, uid):
        return uid // 3

    def score(self, t):
        """双方基地血量（坦克大战里"比分"就是基地血）。"""
        return self.frame(t)['base_hp']

    def label(self, team):
        w = self.who[team]
        s = NAME.get(w, w)
        if w in self.multi:
            s += f'#{team + 1}'
        return s

    def _split(self, f):
        t = int(math.floor(f))
        a = f - t
        if t >= self.T:
            t, a = self.T, 0.0
        if t < 0:
            t, a = 0, 0.0
        return t, a

    def live(self, f):
        """f 时刻（含回合内的进度）的基地血、各车血量 / 等级 / 是否在场，比分栏和外部画面用。"""
        t, a = self._split(f)
        cur = self.frames[t]
        base = list(cur['base_hp'])
        tanks = [dict(tk) for tk in cur['tanks']]
        if t < self.T and a > 0:
            tr = self.traces[t + 1]
            for h in tr['base_hits']:
                if h['dmg'] and h['t'] <= a:
                    base[h['seat']] -= 1
            for h in tr['hits']:
                if h['t'] <= a:
                    tanks[h['tank']]['hp'] -= 1
        return dict(turn=t, base_hp=base, tanks=tanks, kills=cur['kills'], stars_eaten=cur['stars_eaten'])

    # ---------- 播放节奏 ----------
    def key_turns(self):
        """值得慢放的回合：有车被毁、吃星、基地归零的回合。"""
        ks = set()
        for k in range(1, self.T + 1):
            ty = {e['type'] for e in self.frames[k]['events']}
            if ty & {'death', 'star'} or (self.frames[k]['done'] and k == self.T):
                ks.add(k)
        return sorted(ks)

    def play_times(self, t0, t1, fps=30, tps=2.2, slow_tps=1.0, keys=None, radius=.6, hold=1.0):
        """视频每一帧对应的小数回合。平时每秒 tps 回合；keys（默认 key_turns()）里的回合
        前后 radius 回合内降到 slow_tps，速度平滑过渡。末尾停 hold 秒。"""
        keys = self.key_turns() if keys is None else keys

        def rate(f):
            dist = min([abs(f - (k - .5)) for k in keys] + [99])
            w = ease(1 - (dist - .5) / max(.01, radius)) if dist > .5 else 1.0
            return lerp(tps, slow_tps, w)
        out, f = [], float(t0)
        while f < t1:
            out.append(f)
            f += rate(f) / fps
        out.append(float(t1))
        out += [float(t1)] * int(hold * fps)
        return out

    # ---------- 棋盘 ----------
    def draw(self, f, size, view=None, dim=None, ring=None, notes=None, hp=True, focus=None, ss=2, style=None):
        """f：小数回合。view：(左, 上, 边长) 以格为单位的镜头，可以是小数（做推拉）；默认整张图。
        dim：要压暗的座位集合；focus：只突出这些座位（其余压暗）；ring：要加金色高亮圈的坦克编号集合（0–5）。
        notes：保留参数（坦克回放里没有指令注释），忽略。hp：是否画车上的血量格。
        ss：超采样倍数（B 像素风格不用）。style：'A' / 'B' / 'C'，默认用 TReplay(style=...) 定的。"""
        S = STYLES[style or self.style]
        if focus is not None:
            dim = set(range(2)) - set(focus)
        vs = view[2] if view else self.N
        if S['pixel']:
            low = max(PIX, int(round(PIX * vs)))
            self._ss = low / size
            img, marks, texts = self._draw(f, low, view, dim, ring, hp, S)
            img = img.resize((size, size), Image.NEAREST)
            k = size / low
        else:
            ss = max(1, ss)
            self._ss = ss
            img, marks, texts = self._draw(f, size * ss, view, dim, ring, hp, S)
            if ss > 1:
                img = img.resize((size, size), Image.LANCZOS)
            k = 1 / ss
        self._overlay(img, marks, texts, k, S, hp)
        return img

    def _colr(self, seat, dim):
        c = self.col[seat]
        return mix(c, (110, 114, 126), .7) if dim and seat in dim else c

    def _draw(self, f, size, view, dim, ring, hp, S):
        N = self.N
        pix = S['pixel']
        vx, vy, vs = view if view else (0, 0, N)
        c = size / vs
        cd = c / self._ss
        t, a = self._split(f)
        cur = self.frames[t]
        tr = self.traces[t + 1] if t < self.T else None
        anim = tr is not None
        img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
        ox, oy = -vx * c, -vy * c
        P = lambda x, y: (ox + x * c, oy + y * c)
        C = lambda x, y: (ox + (x + .5) * c, oy + (y + .5) * c)
        marks, texts = [], []

        # ---- 地形（当回合被打掉的墙在被打中的那一刻消失）
        grid = [list(r) for r in cur['map']]
        if anim:
            for w in tr['walls']:
                if w['t'] <= a:
                    grid[w['y']][w['x']] = '.'
        mkey = (S['name'], ''.join(''.join(r) for r in grid), size, view)
        img.alpha_composite(self._terrain(mkey, grid, size, c, ox, oy, S))
        d = ImageDraw.Draw(img)

        # ---- 补给点：很淡的底座（出星前一回合开始发亮提示）；出生点不画，车不在场时画虚影
        nxt_supply = None
        P_ = self.P
        if t + 1 >= P_['supply_first']:
            k0 = max(0, -(-(t + 1 - P_['supply_first']) // P_['supply_period']))
            nxt_supply = P_['supply_first'] + k0 * P_['supply_period']
        soon = nxt_supply is not None and nxt_supply - f <= 1.5
        for (x, y) in self.supply:
            cx, cy = C(x, y)
            glow = .5 + .5 * math.sin(f * 9) if soon and (x, y) not in cur['stars'] else 0
            if pix:
                col = mix((120, 100, 40), GOLD, glow)
                for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
                    qx, qy = cx + sx * c * .38, cy + sy * c * .38
                    d.rectangle([qx - 1, qy - 1, qx + 1, qy + 1], fill=col)
            else:
                r = c * .34
                al = int(55 + 120 * glow)
                d.rounded_rectangle([cx - r, cy - r, cx + r, cy + r], c * .1, outline=GOLD + (al,), width=max(1, int(c * .03)))
                r2 = c * .07
                d.ellipse([cx - r2, cy - r2, cx + r2, cy + r2], fill=GOLD + (al,))

        # ---- 基地
        live = self.live(f)
        for seat, (x, y) in enumerate(self.bases):
            flash = shake = 0.0
            if anim:
                for h in tr['base_hits']:
                    if h['seat'] == seat and 0 <= a - h['t'] < .16:
                        flash = 1 - (a - h['t']) / .16
                        shake = flash if h['dmg'] else flash * .4
            self._base(img, C(x, y), c, seat, max(0, live['base_hp'][seat]), flash, shake, dim, f, S, marks)
        d = ImageDraw.Draw(img)

        # ---- 星星
        prev_tr = self.traces[t] if t >= 1 else None
        born = {(e['x'], e['y']) for e in prev_tr['ends'] if e['type'] == 'supply'} if prev_tr else set()
        eaten = {(e['x'], e['y']): e['tank'] for e in tr['ends'] if e['type'] == 'star'} if anim else {}
        for (x, y) in cur['stars']:
            cx, cy = C(x, y)
            k = 1.0
            if (x, y) in born and t < self.T:
                k = ease(a / .35)
                k = k * (1 + .35 * math.sin(math.pi * k))
            if (x, y) in eaten and a > .7:
                k *= 1 - ease((a - .7) / .3)
            if k <= .02:
                continue
            r = c * .3 * k * (1 + .06 * math.sin(f * 5))
            if pix:
                d.polygon(star_poly(cx, cy, r * 1.1, 0, .5), fill=GOLD)
            else:
                glow = c * .46 * k
                d.ellipse([cx - glow, cy - glow, cx + glow, cy + glow], fill=(255, 210, 74, 40))
                d.polygon(star_poly(cx, cy, r, .15 * math.sin(f * 2)), fill=GOLD, outline=(122, 90, 0), width=max(1, int(c * .03)))
                d.polygon(star_poly(cx, cy, r * .42, .15 * math.sin(f * 2)), fill=(255, 246, 200))

        # ---- 不在场的车：出生点上的虚影 + 复活倒计时
        for i, tk in enumerate(cur['tanks']):
            if tk['alive']:
                continue
            if anim and any(e['type'] == 'respawn' and e['tank'] == i for e in tr['ends']) and a > .85:
                continue
            x, y = self.spawns[i]
            if any(o['alive'] and o['pos'] == (x, y) for o in cur['tanks']):
                continue
            cx, cy = C(x, y)
            spr = self._sprite(S, i % 3, self._colr(i // 3, dim), c * .86)
            spr = rotated(spr, ANG['U' if i < 3 else 'D'])
            img.alpha_composite(fade(spr, .2), (int(round(cx - spr.width / 2)), int(round(cy - spr.height / 2))))
            texts.append((cx, cy, str(tk['respawn_in']), c * .42, (230, 232, 240), .85, (10, 12, 20)))
        d = ImageDraw.Draw(img)

        # ---- 坦克（先算好每辆车的位置、朝向、状态）
        sub = 2 * a
        si = min(1, int(sub))
        sf = sub - si
        sfe = ease(sf)                               # 车在每个小步里缓入缓出
        drawn = []
        for i, tk in enumerate(cur['tanks']):
            if not tk['alive']:
                continue
            seat, kind = i // 3, i % 3
            col = self._colr(seat, dim)
            x, y = tk['x'], tk['y']
            ang = ANG[self.facing[t][i]]
            alpha, scale, variant = 1.0, 1.0, ''
            hpv, lvl = tk['hp'], tk['level']
            if anim:
                p0, p1 = tr['pos'][i][si], tr['pos'][i][si + 1]
                x, y = lerp(p0[0], p1[0], sfe), lerp(p0[1], p1[1], sfe)
                bd = tr['bump'][i][si]
                if bd:
                    off = .14 * math.sin(math.pi * min(1, sf * 1.6))
                    x += DV[bd][0] * off
                    y += DV[bd][1] * off
                d1, d2 = self.dirs[t + 1][i]
                a_from = ANG[self.facing[t][i]] if si == 0 else ANG[d1]
                a_to = ANG[d1] if si == 0 else ANG[d2]
                ang = angle_tween(a_from, a_to, ease(sf / .4))
                hits = [h['t'] for h in tr['hits'] if h['tank'] == i]
                hpv = tk['hp'] - sum(1 for q in hits if q <= a)
                if any(0 <= a - q < .08 for q in hits):
                    variant = 'flash'
                th = self.lethal[t + 1].get(i)
                if th is not None and a > th:
                    if a - th >= .08:
                        variant = 'dead'
                    alpha = 1 - ease((a - th - .1) / .45)
                if alpha <= .02:
                    continue
            if prev_tr and any(e['type'] == 'respawn' and e['tank'] == i for e in prev_tr['ends']) and t < self.T:
                k = ease(a / .35)
                scale, alpha = .5 + .5 * k, alpha * k
                if k <= .02:
                    continue
            if dim and seat in dim:
                alpha *= .5
            if pix:
                ang = round(ang / 90) * 90
            drawn.append((i, kind, seat, col, C(x, y), ang, alpha, scale, variant, hpv, tk['maxhp'], lvl))

        if not pix and (S['shadow'] or S['glow']) and drawn:     # 投影和队伍色外发光（一次模糊）
            under = Image.new('RGBA', (size, size), (0, 0, 0, 0))
            ud = ImageDraw.Draw(under)
            for i, kind, seat, col, (cx, cy), ang, alpha, scale, variant, *_ in drawn:
                if variant == 'dead':
                    continue
                r = c * .42 * scale
                if S['shadow']:
                    ud.rounded_rectangle([cx - r + c * .06, cy - r + c * .1, cx + r + c * .06, cy + r + c * .1], c * .12,
                                         fill=(0, 0, 0, int(S['shadow'] * alpha)))
                if S['glow']:
                    g = r * 1.05
                    ud.ellipse([cx - g, cy - g, cx + g, cy + g], fill=col + (int(S['glow'] * alpha),))
            img.alpha_composite(under.filter(ImageFilter.GaussianBlur(c * .14)))
            d = ImageDraw.Draw(img)
        for i, kind, seat, col, (cx, cy), ang, alpha, scale, variant, hpv, mh, lvl in drawn:
            if ring and i in ring:
                r = c * .62
                d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(255, 214, 90, 255), width=max(2, int(c / 8)))
            if lvl >= 2 and variant != 'dead' and not pix:
                r = c * .54 * scale
                d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=GOLD + (int(170 * alpha),), width=max(1, int(c * .04)))
            spr = rotated(self._sprite(S, kind, col, c * S['tank_scale'] * scale, variant), ang)
            if pix and scale < 1:
                spr = spr.resize((max(2, int(PIX * scale)),) * 2, Image.NEAREST)
            img.alpha_composite(fade(spr, alpha), (int(round(cx - spr.width / 2)), int(round(cy - spr.height / 2))))
            d = ImageDraw.Draw(img)
            marks.append((cx, cy, c, hpv, mh, lvl, alpha))

        # ---- 子弹（匀速，不缓动）
        if anim:
            for b in tr['bullets']:
                if b['end'] is not None and sub >= b['end']:
                    continue
                if b['fresh'] and sub < .32:
                    continue
                dx, dy = DV[b['dir']]
                cx, cy = C(b['x0'] + dx * sub, b['y0'] + dy * sub)
                tl = min(sub - (.32 if b['fresh'] else 0), 1.0)
                self._bullet(img, cx, cy, dx, dy, tl, self._colr(b['owner'] // 3, dim), b['steel'], c, S)
        else:
            for b in cur['bullets']:
                dx, dy = DV[b['dir']]
                cx, cy = C(b['x'], b['y'])
                self._bullet(img, cx, cy, dx, dy, .8, self._colr(b['owner'] // 3, dim), b['steel'], c, S)

        # ---- 特效
        self._effects(img, f, t, c, C, P, dim, cd, S, texts)
        return img, marks, texts

    def _sprite(self, S, kind, col, px, variant=''):
        if S['pixel']:
            return pix_tank(kind, col, variant)
        return tank_sprite(kind, col, px, variant, flat=S['flat'])

    def _bullet(self, img, cx, cy, dx, dy, tl, col, steel, c, S):
        d = ImageDraw.Draw(img)
        if S['pixel']:
            x, y = int(cx), int(cy)
            for j in range(1, 4):                       # 拖尾：两三个渐淡的像素
                d.rectangle([x - dx * j * 2 - 1, y - dy * j * 2 - 1, x - dx * j * 2, y - dy * j * 2], fill=col + (200 - 55 * j,))
            d.rectangle([x - 1, y - 1, x + 1, y + 1], fill=(255, 255, 255))
            if steel:
                d.rectangle([x - 2, y - 2, x + 2, y + 2], outline=col)
            return
        tx, ty = cx - dx * tl * c * .9, cy - dy * tl * c * .9
        steps = 6
        for j in range(steps):                          # 渐淡拖尾
            k0, k1 = j / steps, (j + 1) / steps
            d.line([(lerp(tx, cx, k0), lerp(ty, cy, k0)), (lerp(tx, cx, k1), lerp(ty, cy, k1))],
                   fill=col + (int(220 * k1),), width=max(1, int(c * (.05 + .1 * k1))))
        r = c * (.2 if S['name'] == 'C' else .16)
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=col + (90,))
        r = c * .11
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 255, 255), outline=col, width=max(1, int(c * .035)))
        if steel:
            r2 = c * .2
            d.ellipse([cx - r2, cy - r2, cx + r2, cy + r2], outline=(255, 255, 255, 230), width=max(1, int(c * .03)))

    # ---- 放大之后再画的东西：血量格、等级星、文字（像素风格下也保持清晰）
    def _overlay(self, img, marks, texts, k, S, hp):
        d = ImageDraw.Draw(img)
        cd = None
        for cx, cy, c, hpv, mh, lvl, alpha in marks:
            cx, cy, c = cx * k, cy * k, c * k
            cd = c
            if alpha < .3:
                continue
            al = int(255 * alpha)
            if hp and mh >= 2 and c >= 14:              # 1 血的车不画血量格，免得满屏小格子
                n = mh
                pw = min(c * .13, c * .74 / n - c * .03)
                ph = max(2, c * .075)
                gap = c * .03
                tw = n * pw + (n - 1) * gap
                x0, y0 = cx - tw / 2, cy - c * .5 - ph * .2
                if S['pixel']:
                    d.rectangle([x0 - 2, y0 - 2, x0 + tw + 2, y0 + ph + 2], fill=(0, 0, 0, al))
                else:
                    d.rounded_rectangle([x0 - 2, y0 - 2, x0 + tw + 2, y0 + ph + 2], 3, fill=(6, 8, 14, int(190 * alpha)))
                hc = (110, 225, 150) if hpv >= 2 else (250, 200, 70) if hpv == 1 else RED
                for j in range(n):
                    bx = x0 + j * (pw + gap)
                    d.rectangle([bx, y0, bx + pw, y0 + ph], fill=(hc if j < max(0, hpv) else (60, 66, 82)) + (al,))
            if lvl > 0 and c >= 10:
                r = c * .12
                for j in range(lvl):
                    sx = cx + c * .5 - r * 1.0 - j * r * 1.9
                    sy = cy + c * .5 - r * 1.0
                    d.polygon(star_poly(sx, sy, r * 1.25), fill=(10, 12, 20, al))
                    d.polygon(star_poly(sx, sy, r), fill=GOLD + (al,))
        for item in texts:
            x, y, s, fs, col, al = item[:6]
            stroke = item[6] if len(item) > 6 else (10, 12, 20)
            if al <= 0:
                continue
            fs = max(15, int(fs * k))
            d.text((x * k, y * k), s, font=font(fs, 'Black'), fill=col + (int(255 * al),), anchor='mm',
                   stroke_width=max(1, fs // 7), stroke_fill=stroke + (int(230 * al),))

    # ---- 地形图层（按地图内容缓存）。墙体同类相邻连成整块，只在外轮廓描边
    _tc = {}

    def _terrain(self, key, grid, size, c, ox, oy, S):
        if key in self._tc:
            return self._tc[key]
        if len(self._tc) > 24:
            self._tc.clear()
        N = self.N
        im = Image.new('RGBA', (size, size), S['floor'] + (255,))
        d = ImageDraw.Draw(im)
        cell = lambda x, y: grid[y][x] if 0 <= x < N and 0 <= y < N else 'X'
        if S['pixel']:
            for y in range(N):
                for x in range(N):
                    ch = grid[y][x]
                    if ch in '#@~':
                        im.alpha_composite(pix_tile(ch, x, y), (int(round(ox + x * c)), int(round(oy + y * c))))
            self._tc[key] = im
            return im
        if S['vignette']:                          # 中间略亮、四角略暗
            v = Image.new('L', (64, 64), 0)
            vd = ImageDraw.Draw(v)
            for r in range(32, 0, -1):
                vd.ellipse([32 - r * 1.4, 32 - r * 1.4, 32 + r * 1.4, 32 + r * 1.4], fill=int(18 * (1 - r / 32)))
            lay = Image.new('RGBA', (size, size), (60, 80, 120, 255))
            lay.putalpha(v.resize((size, size), Image.BICUBIC))
            im.alpha_composite(lay)
            d = ImageDraw.Draw(im)
        if S['grid']:
            for k in range(N + 1):
                d.line([(ox + k * c, oy), (ox + k * c, oy + N * c)], fill=S['grid'], width=1)
                d.line([(ox, oy + k * c), (ox + N * c, oy + k * c)], fill=S['grid'], width=1)
        lw = max(1, int(c * .045))
        depth = S['depth']
        # 投影：墙下方的地面压暗一条（C）
        if depth:
            sh = Image.new('RGBA', (size, size), (0, 0, 0, 0))
            sd = ImageDraw.Draw(sh)
            for y in range(N):
                for x in range(N):
                    if grid[y][x] in '#@' and cell(x, y + 1) not in '#@':
                        x0, y0 = ox + x * c, oy + (y + 1) * c
                        sd.rectangle([x0, y0, x0 + c, y0 + c * .22], fill=(0, 0, 0, 120))
            im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(c * .08)))
            d = ImageDraw.Draw(im)
        for ch, key2 in (('~', 'water'), ('#', 'brick'), ('@', 'steel')):
            pal = S[key2]
            top, edge = pal[0], pal[1]
            front = pal[2] if len(pal) > 2 else None
            for y in range(N):
                for x in range(N):
                    if grid[y][x] != ch:
                        continue
                    x0, y0 = ox + x * c, oy + y * c
                    d.rectangle([x0, y0, x0 + c, y0 + c], fill=top)
                    if ch == '#':                   # 砖缝：很淡，错缝两排
                        sc = mix(top, (0, 0, 0), .16)
                        w = max(1, int(c * .03))
                        d.line([(x0, y0 + c * .5), (x0 + c, y0 + c * .5)], fill=sc, width=w)
                        d.line([(x0 + c * .5, y0), (x0 + c * .5, y0 + c * .5)], fill=sc, width=w)
                        d.line([(x0 + c * .25, y0 + c * .5), (x0 + c * .25, y0 + c)], fill=sc, width=w)
                        d.line([(x0 + c * .75, y0 + c * .5), (x0 + c * .75, y0 + c)], fill=sc, width=w)
                    elif ch == '@':                 # 钢板：一块内嵌方板 + 四颗铆钉
                        m = c * .2
                        d.rectangle([x0 + m, y0 + m, x0 + c - m, y0 + c - m], fill=mix(top, (255, 255, 255), .12))
                        r = c * .045
                        for sx in (.1, .9):
                            for sy in (.1, .9):
                                d.ellipse([x0 + c * sx - r, y0 + c * sy - r, x0 + c * sx + r, y0 + c * sy + r],
                                          fill=mix(top, (255, 255, 255), .35))
                    else:                           # 水：两道很淡的波纹
                        w = max(1, int(c * .035))
                        for r_ in range(2):
                            yy = y0 + c * (.35 + r_ * .32)
                            pts = [(x0 + c * (.12 + i * .095), yy + math.sin(i * 1.57 + r_ * 1.7 + x) * c * .04) for i in range(9)]
                            d.line(pts, fill=mix(top, edge, .55), width=w, joint='curve')
                    if depth and front and cell(x, y + 1) != ch:   # 2.5D：下沿一条暗色"侧面"
                        d.rectangle([x0, y0 + c * .8, x0 + c, y0 + c], fill=front)
            # 外轮廓：只在和不同地形相邻的那一边描线
            for y in range(N):
                for x in range(N):
                    if grid[y][x] != ch:
                        continue
                    x0, y0 = ox + x * c, oy + y * c
                    h = lw / 2
                    if cell(x, y - 1) != ch:
                        d.line([(x0, y0 + h), (x0 + c, y0 + h)], fill=edge, width=lw)
                    if cell(x - 1, y) != ch:
                        d.line([(x0 + h, y0), (x0 + h, y0 + c)], fill=edge, width=lw)
                    if cell(x + 1, y) != ch:
                        d.line([(x0 + c - h, y0), (x0 + c - h, y0 + c)], fill=mix(edge, top, .5), width=lw)
                    if cell(x, y + 1) != ch and not depth:
                        d.line([(x0, y0 + c - h), (x0 + c, y0 + c - h)], fill=mix(edge, top, .5), width=lw)
        self._tc[key] = im
        return im

    def _base(self, img, center, c, seat, hp, flash, shake, dim, f, S, marks):
        cx, cy = center
        if shake:
            cx += math.sin(f * 90) * c * .06 * shake
            cy += math.cos(f * 77) * c * .04 * shake
        col = self._colr(seat, dim)
        x0, y0 = cx - c / 2, cy - c / 2
        d = ImageDraw.Draw(img)
        k = clamp(hp / self.maxhp_base)
        alive = hp > 0
        if S['pixel']:                              # 像素堡垒：城垛 + 门 + 小旗
            body = mix(col, (0, 0, 0), .35) if alive else (60, 60, 64)
            hi = col if alive else (90, 90, 96)
            if flash:
                body = hi = (255, 255, 255)
            u = c / PIX
            X0, Y0 = int(round(x0)), int(round(y0))

            def R(a, b, c_, d_, fill):
                xa, ya = X0 + int(a * u), Y0 + int(b * u)
                xb, yb = X0 + int(c_ * u) - 1, Y0 + int(d_ * u) - 1
                if xb >= xa and yb >= ya:
                    d.rectangle([xa, ya, xb, yb], fill=fill)
            R(2, 6, 14, 14, body)
            for m in (2, 6, 10):
                R(m, 4, m + 3, 6, hi)
            R(6, 10, 10, 14, (0, 0, 0))
            if alive:
                R(7, 0, 8, 6, (220, 220, 220))
                R(8, 0, 12, 3, hi)
            R(1, 15, 15, 16, (40, 40, 44))
            R(1, 15, 1 + 14 * k, 16, (110, 225, 150) if k > .5 else (250, 200, 70) if k > .25 else RED)
            return
        r = c * .44
        if alive:
            plate = mix(col, (255, 255, 255), flash) if flash else mix(col, (16, 20, 32), .55)
            if S['name'] == 'C':                      # 堡垒：圆角方台 + 三个城垛
                for j in (-1, 0, 1):
                    mx = cx + j * c * .3
                    d.rectangle([mx - c * .09, y0 + c * .02, mx + c * .09, y0 + c * .16], fill=mix(col, (16, 20, 32), .25))
                d.rounded_rectangle([x0 + c * .05, y0 + c * .12, x0 + c * .95, y0 + c * .97], c * .14, fill=plate,
                                    outline=col, width=max(1, int(c * .05)))
            else:
                d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=plate, outline=col, width=max(1, int(c * .04)))
            av = avatar(self.who[seat], c * .54)
            ay = cy + (c * .05 if S['name'] == 'C' else 0)
            img.alpha_composite(fade(av, .55 if dim and seat in dim else 1), (int(cx - av.width / 2), int(ay - av.height / 2)))
            d = ImageDraw.Draw(img)
            # 环形血量：队伍色，从 12 点钟顺时针
            rr = c * .33
            w = max(2, int(c * .075))
            box = [cx - rr, ay - rr, cx + rr, ay + rr]
            d.arc(box, 0, 360, fill=(30, 34, 46), width=w)
            if k > 0:
                d.arc(box, -90, -90 + 360 * k, fill=col if k > .25 else RED, width=w)
        else:
            d.rounded_rectangle([x0 + c * .05, y0 + c * .12, x0 + c * .95, y0 + c * .97], c * .14, fill=(44, 40, 42),
                                outline=(96, 92, 94), width=max(1, int(c * .04)))
            w = max(2, int(c * .08))
            d.line([(cx - c * .22, cy - c * .15), (cx + c * .22, cy + c * .29)], fill=(150, 146, 150), width=w)
            d.line([(cx + c * .22, cy - c * .15), (cx - c * .22, cy + c * .29)], fill=(150, 146, 150), width=w)

    # ---- 特效：每个特效是 (类型, 位置, 绝对时刻 A)，画成 年龄 = f - A 的函数（单位：回合）
    def _effects(self, img, f, t, c, C, P, dim, cd, S, texts):
        lst = []
        for k in (t, t + 1):
            if 1 <= k <= self.T:
                lst += self.fx[k]
        lst = [e for e in lst if 0 <= f - e['A'] < 1.4]
        if not lst:
            return
        pix = S['pixel']
        big = 1.15 if S['name'] == 'C' else 1.0
        lay = Image.new('RGBA', img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(lay)
        W = lambda q: max(1, int(c * q))
        for e in lst:
            age = f - e['A']
            ty = e['type']
            cx, cy = C(e['x'], e['y'])
            if ty == 'muzzle':
                if age > .25:
                    continue
                u = age / .25
                dx, dy = DV[e['dir']]
                mx, my = cx + dx * c * .55, cy + dy * c * .55
                r = c * (.15 + .2 * u) * big
                d.ellipse([mx - r, my - r, mx + r, my + r], fill=(255, 236, 160, int(255 * (1 - u))))
                r2 = r * .55
                d.ellipse([mx - r2, my - r2, mx + r2, my + r2], fill=(255, 255, 255, int(255 * (1 - u))))
            elif ty == 'hit':
                if age < .45:
                    u = age / .45
                    al = int(255 * (1 - u))
                    for j in range(8):
                        an = j * math.pi / 4 + .3
                        r1, r2 = c * (.12 + .3 * u) * big, c * (.26 + .45 * u) * big
                        d.line([(cx + math.cos(an) * r1, cy + math.sin(an) * r1), (cx + math.cos(an) * r2, cy + math.sin(an) * r2)],
                               fill=(255, 255, 255, al), width=W(.06))
                    r = c * (.2 + .4 * u) * big
                    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(255, 120, 90, al), width=W(.05))
                if age < 1.1 and cd >= 12 and not e['lethal']:      # 伤害数字：小号，上浮淡出
                    u = age / 1.1
                    texts.append((cx + c * .32, cy - c * (.3 + .75 * ease(u)), '-1', c * .34, (255, 120, 100),
                                  clamp(1.6 - 1.6 * u)))
            elif ty == 'death':
                if age > 1.3:
                    continue
                u = age / 1.3
                for r0, col, d0 in ((.8, (255, 122, 26), 0), (.58, (255, 210, 74), .04), (.34, (255, 255, 255), .08)):
                    v = clamp((u - d0) / .45)
                    if 0 < v < 1:
                        r = c * r0 * (.35 + .95 * v) * big
                        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=col + (int(255 * (1 - v * v)),))
                al = int(255 * clamp(1.2 - u * 1.4))
                for j in range(12):
                    an = j * .5236 + .2
                    r1, r2 = c * (.3 + .9 * u) * big, c * (.45 + 1.1 * u) * big
                    d.line([(cx + math.cos(an) * r1, cy + math.sin(an) * r1), (cx + math.cos(an) * r2, cy + math.sin(an) * r2)],
                           fill=(255, 210, 74, al), width=W(.06))
                for j in range(9):                     # 碎片
                    an = rnd(e['tank'] * 7 + j, e['x']) * 6.283
                    rr = c * (.2 + 1.1 * u * (.5 + .5 * rnd(j, e['y'])))
                    sz = c * (.06 + .05 * rnd(j, 3))
                    px_, py_ = cx + math.cos(an) * rr, cy + math.sin(an) * rr + c * .4 * u * u
                    d.rectangle([px_ - sz, py_ - sz, px_ + sz, py_ + sz], fill=mix(self.col[e['tank'] // 3], (40, 40, 40), .4) + (al,))
                sa = clamp((u - .2) / .3) * (1 - u)
                if sa > 0:
                    for j in range(5):
                        an = j * 1.257
                        sx, sy = cx + math.cos(an) * c * .35 * u, cy + math.sin(an) * c * .35 * u - c * .4 * u
                        r = c * .26
                        d.ellipse([sx - r, sy - r, sx + r, sy + r], fill=(70, 70, 78, int(190 * sa)))
                if cd >= 12:
                    txt = '压出生点' if e['by'] >= 0 else '击毁'
                    texts.append((cx, cy - c * (.7 + .3 * ease(u)), txt, c * .36, (255, 255, 255), clamp(2.0 - u * 2.0),
                                  mix(self.col[1 - e['tank'] // 3], (0, 0, 0), .55)))
            elif ty == 'debris':
                if age > .7:
                    continue
                u = age / .7
                if e['kind'] == 'brick':
                    pal = S.get('brick') or ((176, 92, 56), (136, 66, 40))
                else:
                    pal = S.get('steel') or ((210, 214, 222), (160, 166, 178))
                cols = [pal[0], pal[1], mix(pal[0], (0, 0, 0), .4)]
                if u < .2:
                    x0, y0 = P(e['x'], e['y'])
                    d.rectangle([x0, y0, x0 + c, y0 + c], fill=(255, 255, 255, int(170 * (1 - u / .2))))
                al = int(255 * (1 - u * u))
                for j in range(12):
                    an = rnd(e['x'] * 17 + j, e['y'] * 31) * 6.283
                    rr = c * (.1 + (.35 + .55 * rnd(j, e['x'] + e['y'])) * u)
                    sz = c * (.05 + .05 * rnd(j, 7))
                    px_, py_ = cx + math.cos(an) * rr, cy + math.sin(an) * rr + c * .3 * u * u
                    d.rectangle([px_ - sz, py_ - sz, px_ + sz, py_ + sz], fill=cols[j % 3] + (al,))
            elif ty == 'spark':
                if age > .3 or e['kind'] == 'brick':
                    continue
                u = age / .3
                dx, dy = DV[e['dir']]
                base = math.atan2(-dy, -dx)
                for j in range(-2, 3):
                    an = base + j * .5
                    r1, r2 = c * .06, c * (.14 + .3 * u)
                    d.line([(cx + math.cos(an) * r1, cy + math.sin(an) * r1), (cx + math.cos(an) * r2, cy + math.sin(an) * r2)],
                           fill=(235, 240, 248, int(255 * (1 - u))), width=W(.045))
            elif ty == 'out':
                if age > .25:
                    continue
                r = c * .12
                d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 255, 255, int(255 * (1 - age / .25))))
            elif ty == 'cancel':
                if age > .5:
                    continue
                u = age / .5
                al = int(255 * (1 - u))
                r = c * (.1 + .2 * u)
                d.line([(cx - r, cy - r), (cx + r, cy + r)], fill=(255, 255, 255, al), width=W(.06))
                d.line([(cx + r, cy - r), (cx - r, cy + r)], fill=(255, 255, 255, al), width=W(.06))
                r2 = c * (.15 + .4 * u)
                d.ellipse([cx - r2, cy - r2, cx + r2, cy + r2], outline=(190, 233, 255, al), width=W(.04))
            elif ty == 'basehit':
                if age < .6:
                    u = age / .6
                    al = int(255 * (1 - u))
                    r = c * (.5 + .5 * ease(u))
                    col = self._colr(e['seat'], dim) if e['dmg'] else (200, 205, 215)
                    d.rounded_rectangle([cx - r, cy - r, cx + r, cy + r], c * .2, outline=col + (al,), width=W(.07))
                if e['dmg'] and age < 1.2 and cd >= 10:          # 小号伤害数字，往棋盘内侧飘
                    u = age / 1.2
                    up = 1 if e['y'] < self.N / 2 else -1
                    texts.append((cx + c * .62, cy + up * c * (.2 + .9 * ease(u)), '-1', c * .4, (255, 110, 100),
                                  clamp(1.6 - 1.6 * u)))
            elif ty == 'respawn':
                if age > .8:
                    continue
                u = age / .8
                col = self.col[e['tank'] // 3]
                for k in (0, .33, .66):
                    v = (u + k) % 1
                    r = c * (.95 - .55 * v)
                    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=col + (int(255 * (1 - u)),), width=W(.05))
                if cd >= 12:
                    texts.append((cx, cy - c * .72, '复活', c * .32, col, 1 - u))
            elif ty == 'star':
                if age > 1.2:
                    continue
                u = age / 1.2
                al = int(255 * (1 - u))
                r = c * (.3 + .6 * u)
                d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=GOLD + (al,), width=W(.07))
                for j in range(5):
                    an = j * 1.257 - 1.57 + u
                    d.polygon(star_poly(cx + math.cos(an) * c * (.35 + .6 * u), cy + math.sin(an) * c * (.35 + .6 * u), c * .11),
                              fill=GOLD + (al,))
                if cd >= 10:
                    lab = f'Lv{e["level"]}' + (' 破钢' if e['level'] >= 2 and self.P.get('level2_steel') else '')
                    texts.append((cx, cy - c * (.72 + .3 * ease(u)), lab, c * .34, GOLD, clamp(2.0 - u * 2.0)))
            elif ty == 'supply':
                if age > .7:
                    continue
                u = age / .7
                r = c * (.95 - .55 * u)
                d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=GOLD + (int(255 * (1 - u)),), width=W(.05))
            elif ty == 'cleared':
                if age > .3:
                    continue
                r = c * .2
                d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(200, 200, 200, int(255 * (1 - age / .3))))
        if not pix:
            img.alpha_composite(lay.filter(ImageFilter.GaussianBlur(max(1, c * .08))))
        img.alpha_composite(lay)

    # ---------- 比分栏 ----------
    def scoreboard(self, f, w, h, highlight=None, style=None):
        """竖排比分栏：顶部回合数；上半是 1 号方（棋盘上方），下半是 0 号方。每方：队伍色竖条 + 头像 + 名字，
        右侧大号基地血量（队伍色），下面一条队伍色基地血条（24 格），击毁 / 吃星，三辆车各一行
        （车型图标、名称、血量格、两个等级星位；不在场时显示几回合后复活）。highlight：要高亮的座位。"""
        S = STYLES[style or self.style]
        lv = self.live(f)
        t = lv['turn']
        _t, _a = self._split(f)
        if _t < self.T and _a > 0:             # 正在播第 t+1 回合的动画：写 t+1（不是少一）
            t = _t + 1
        img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        k = h / 1000
        kf = max(k, min(1.0, k ** .6))          # 字号缩得比版面慢一些：小比分栏里字也看得清
        rad = 0 if S['pixel'] else int(20 * k)
        d.rounded_rectangle([0, 0, w - 1, h - 1], rad, fill=S['panel'], outline=S['panel_line'], width=2)
        pad = int(26 * min(1, w / 560))
        d.text((pad, 40 * k), f'第 {t} 回合', font=font(34 * kf), fill=(235, 238, 245), anchor='lm')
        d.text((w - pad, 40 * k), f'共 {self.T}', font=font(22 * kf, 'Medium'), fill=(140, 148, 166), anchor='rm')
        d.line([(pad, 78 * k), (w - pad, 78 * k)], fill=S['accent'] + (150,), width=max(1, int(2 * k)))
        top = 92 * k
        ph = (h - top - 14 * k) / 2
        end = t >= self.T and self.frames[self.T]['done']
        for slot, seat in enumerate((1, 0)):
            y0 = top + slot * ph
            self._panel(img, seat, pad, y0, w - pad, y0 + ph - 12 * k, k, kf, lv, highlight, end, S)
        return img

    def _panel(self, img, seat, x0, y0, x1, y1, k, kf, lv, highlight, end, S):
        d = ImageDraw.Draw(img)
        col = self.col[seat]
        if highlight and seat in highlight:
            d.rounded_rectangle([x0 - 12 * k, y0, x1 + 12 * k, y1], 14 * k, fill=col + (40,))
        d.rectangle([x0 - 12 * k, y0 + 8 * k, x0 - 12 * k + max(3, 6 * k), y1 - 8 * k], fill=col)      # 队伍色竖条
        av = int(70 * kf)
        img.alpha_composite(avatar(self.who[seat], av), (int(x0 + 6 * k), int(y0 + 12 * k)))
        d = ImageDraw.Draw(img)
        nx = x0 + av + 20 * k
        d.text((nx, y0 + 30 * k), self.label(seat), font=font(32 * kf), fill=(240, 242, 248), anchor='lm')
        d.text((nx, y0 + 30 * k + 34 * kf), f'击毁 {lv["kills"][seat]}  ·  吃星 {lv["stars_eaten"][seat]}',
               font=font(20 * kf, 'Medium'), fill=(150, 158, 176), anchor='lm')
        hp = max(0, lv['base_hp'][seat])
        n = self.maxhp_base
        hc = col if hp > n / 4 else RED
        if end:
            win = self.frames[self.T]['winner']
            tag = '胜' if win == seat else '平' if win < 0 else '负'
            tc = GOLD if win == seat else (140, 148, 166)
            d.text((x1 - 118 * kf, y0 + 30 * k), tag, font=font(30 * kf, 'Black'), fill=tc, anchor='rm')
        d.text((x1, y0 + 14 * k), '基地', font=font(18 * kf, 'Medium'), fill=(150, 158, 176), anchor='rt')
        d.text((x1, y0 + 64 * k), str(hp), font=font(52 * kf, 'Black'), fill=hc, anchor='rm')
        # 基地血条：24 格，队伍色
        by = y0 + 108 * k
        gap = max(1, 2 * k)
        sw = (x1 - x0 - (n - 1) * gap) / n
        bh = 16 * kf
        for j in range(n):
            sx = x0 + j * (sw + gap)
            d.rectangle([sx, by - bh / 2, sx + sw, by + bh / 2], fill=hc if j < hp else (40, 46, 62))
        # 三辆车
        rt = by + 22 * k
        rh = min((y1 - rt) / 3, 90 * k)
        star_x = x1 - 12 * kf
        for i in range(3):
            tid = seat * 3 + i
            tk = lv['tanks'][tid]
            cy = rt + i * rh + rh / 2
            ip = int(min(rh * .78, 58 * kf))
            on = tk['alive']
            spr = self._sprite(S, i, col if on else (90, 94, 106), ip)
            if S['pixel']:
                spr = spr.resize((ip, ip), Image.NEAREST)
            img.alpha_composite(fade(spr, 1 if on else .45), (int(x0), int(cy - ip / 2)))
            d = ImageDraw.Draw(img)
            tx = x0 + ip + 14 * k
            d.text((tx, cy), TYPE_CN[i], font=font(25 * kf), fill=(230, 233, 240) if on else (120, 126, 140), anchor='lm')
            px0 = tx + 64 * kf
            if on:
                mh = tk['maxhp']
                pw, gap2 = 20 * kf, 5 * kf
                for j in range(mh):
                    sx = px0 + j * (pw + gap2)
                    c2 = col if j < max(0, tk['hp']) else (44, 50, 66)
                    d.rounded_rectangle([sx, cy - 9 * kf, sx + pw, cy + 9 * kf], 3 * kf, fill=c2)
                for j in range(2):                    # 两个固定的等级星位：吃到的亮金色，没吃到的暗轮廓
                    sx = star_x - j * 30 * kf
                    lit = tk['level'] >= 2 - j
                    d.polygon(star_poly(sx, cy, 13 * kf), fill=GOLD if lit else None, outline=None if lit else (70, 76, 94),
                              width=max(1, int(2 * kf)))
            else:
                d.text((px0, cy), f'{tk["respawn_in"]} 回合后复活', font=font(21 * kf, 'Medium'), fill=(140, 148, 166), anchor='lm')


# 和夺旗渲染器的类名对照着用
FReplay = TReplay
