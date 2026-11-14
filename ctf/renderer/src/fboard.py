"""夺旗回放渲染：读回放（.json / .json.gz，或仓库 replays/ 里打包了查看器的 .html），按任意小数回合画出棋盘（带补间动画）。
    rp = FReplay(path, mine='Opus')     # 开发回放里自己的程序（bot、v7、submission/bot.cpp…）算谁
    img = rp.draw(f, size)              # f 可以是小数：12.5 = 第 12 回合结束后、第 13 回合进行到一半
    sb = rp.scoreboard(f, w, h)         # 右侧比分栏
回放帧约定：frames[t] 是第 t 回合结算完的局面；frames[t]['events'] / ['orders'] 是第 t 回合里的事件和指令。
所以画 f∈[t, t+1) 时，从 frames[t] 过渡到 frames[t+1]，用 frames[t+1] 的事件和指令。
"""
import json, math, os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import common

# 正赛里的全名 → 片中称呼
NAME = {'GPT-6 Astra': 'Astra', 'GPT-6.1 Sol': 'Sol', 'Opus 5.5': 'Opus', 'Sonnet 5.5': 'Sonnet',
        'Fable 5.1': 'Fable', 'DeepSeek V4.1 Flash': 'DeepSeek', 'Qwen 3.8 Max': '千问', 'GLM 5.3': 'GLM',
        'MiMo V2.6 Pro': 'MiMo', 'Kimi K3': 'Kimi', '豆包 Seed 2.1 Turbo': '豆包', '基准': '基准'}
# 12 个阵营要在一张图上分得开：色相尽量拉开，头像负责最终辨认
CHAR = {'Astra': (100, 205, 130), 'Sol': (240, 210, 70), 'Opus': (240, 150, 60), 'Sonnet': (255, 185, 150),
        'Fable': (225, 110, 230), 'DeepSeek': (70, 140, 255), '千问': (135, 115, 250), 'Kimi': (215, 215, 240),
        'GLM': (60, 200, 210), 'MiMo': (235, 70, 60), '豆包': (250, 120, 150), '基准': (130, 136, 150)}

FLOOR_A, FLOOR_B = (30, 38, 54), (34, 43, 60)
WALL, WALL_HI, WALL_LO = (74, 82, 104), (104, 114, 140), (46, 52, 68)
FLAG = (255, 226, 92)            # 中立旗：金黄
FLAG_POLE = (230, 232, 240)
DIRS = {'N': (0, -1), 'S': (0, 1), 'E': (1, 0), 'W': (-1, 0)}
# 选手写在指令里的角色注释 → 片中显示的中文（Astra 写的是 "3:25,31" 这类坐标代码，不显示）
NOTE_ZH = {'camp': '蹲点', 'escort': '护送', 'carry': '送旗', 'intercept': '拦截', 'hunt': '追猎', 'run': '抢旗',
           'home': '回家', 'def': '防守', 'grab': '抢旗', 'pos': '站位', 'esc': '护送', 'dead': '',
           # GLM 的缩写，按它源码里的 rname[] 和 enum 对应
           'cy': '送旗', 'fe': '取旗', 'cl': '清场', 'su': '支援', 'es': '护送', 'hu': '追猎', 'ca': '蹲点',
           'id': '待命', 'xx': ''}


def note_text(s):
    s = s.strip()
    if not s or any(ch.isdigit() for ch in s):
        return ''
    return NOTE_ZH.get(s, s)

font = common.font


def ease(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3 - 2 * x)


def lerp(a, b, k):
    return a + (b - a) * k


def mix(c1, c2, k):
    return tuple(int(lerp(a, b, k)) for a, b in zip(c1, c2))


_av = {}


def avatar(who, px):
    """圆形头像。基准是灰色小圆脸；还没有素材的角色（Fable）用首字母占位。"""
    key = (who, px)
    if key in _av:
        return _av[key]
    src = None if who == '基准' else common.avatar_path(who)       # renderer/assets/avatars/<名>.png
    if src:
        im = Image.open(src).convert('RGBA').resize((px, px), Image.LANCZOS)
    else:
        S = 128
        im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        if who == '基准':
            d.ellipse([0, 0, S - 1, S - 1], fill=(88, 94, 108))
            d.ellipse([S * .28, S * .3, S * .42, S * .44], fill=(220, 224, 232))
            d.ellipse([S * .58, S * .3, S * .72, S * .44], fill=(220, 224, 232))
            d.rounded_rectangle([S * .3, S * .6, S * .7, S * .68], S * .04, fill=(220, 224, 232))
        else:
            d.ellipse([0, 0, S - 1, S - 1], fill=mix(CHAR.get(who, (150, 150, 150)), (20, 24, 36), .35))
            d.text((S / 2, S / 2), who[:1], font=font(70, 'Black'), fill=(255, 255, 255), anchor='mm')
        im = im.resize((px, px), Image.LANCZOS)
    _av[key] = im
    return im


def flag_icon(px, col=FLAG, tilt=0.0):
    """一面小旗：白色旗杆 + 三角旗面。tilt：掉在地上时歪一点。"""
    S = 96
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([S * .3, S * .1, S * .38, S * .92], S * .03, fill=FLAG_POLE)
    d.polygon([(S * .38, S * .12), (S * .9, S * .3), (S * .38, S * .5)], fill=col)
    d.polygon([(S * .38, S * .12), (S * .9, S * .3), (S * .38, S * .3)], fill=mix(col, (255, 255, 255), .35))
    d.ellipse([S * .2, S * .84, S * .48, S * .96], fill=(0, 0, 0, 90))
    if tilt:
        im = im.rotate(tilt, resample=Image.BICUBIC, center=(S * .34, S * .9))
    return im.resize((px, px), Image.LANCZOS)


class FReplay:
    def __init__(self, path, mine=None, names=None):
        r = common.load_json_or_html(path)
        self.path = path
        self.h = r['header']
        self.frames = r['frames']
        self.res = r.get('result', {})
        self.N = self.h['size']
        self.map = self.h['map']
        self.teams = self.h['teams']
        self.raw_names = names or r['names']
        self.who, self.dup = [], []        # dup：同名第几份（正赛 9–15 人局有分身，开发回放有多个基准）
        seen = {}
        for n in self.raw_names:
            base = n.split('#')[0].strip()
            if base in NAME:
                w = NAME[base]
            elif 'baseline' in base.lower() or base == '基准':
                w = '基准'
            elif mine:
                w = mine
            else:
                w = '基准'
            seen[w] = seen.get(w, 0) + 1
            self.who.append(w)
            # 正赛名字自带 "#2" 后缀时，按官方编号；否则按出现顺序
            suf = n.split('#')[1].strip() if '#' in n and base in NAME else ''
            self.dup.append(int(suf) if suf.isdigit() else (1 if base in NAME else seen[w]))
        self.multi = {w for w, k in seen.items() if k > 1}
        self.T = len(self.frames) - 1
        self.bases = [[tuple(c) for c in b] for b in self.h['bases']]
        self.spots = [tuple(s) for s in self.h['flag_spots']]

    # ---------- 取状态 ----------
    def frame(self, t):
        return self.frames[max(0, min(self.T, t))]

    def team_of(self, uid):
        return uid // 3

    def score(self, t):
        return self.frame(t)['score']

    def label(self, team):
        w = self.who[team]
        if w not in self.multi:
            return w
        return f'{w}#{self.dup[team]}'

    # ---------- 棋盘 ----------
    def draw(self, f, size, view=None, dim=None, ring=None, notes=None, hp=True, focus=None, ss=2):
        """f：小数回合。view：(左, 上, 边长) 以格为单位的镜头，可以是小数（做推拉）；默认整张图。
        dim：要压暗的阵营集合；focus：只突出这些阵营（其余自动压暗变灰，多人局用）；
        ring：要加高亮圈的角色 id 集合；notes：要显示指令注释气泡的阵营集合。
        ss：超采样倍数，先按 ss 倍画再缩小，线条和头像边缘更平滑。"""
        if focus is not None:
            dim = set(range(self.teams)) - set(focus)
        self._ss = max(1, ss)
        if ss <= 1:
            return self._draw(f, size, view, dim, ring, notes, hp)
        return self._draw(f, size * ss, view, dim, ring, notes, hp).resize((size, size), Image.LANCZOS)

    def _draw(self, f, size, view=None, dim=None, ring=None, notes=None, hp=True):
        N = self.N
        vx, vy, vs = view if view else (0, 0, N)
        c = size / vs
        cd = c / getattr(self, '_ss', 1)        # 最终显示时每格的像素，用来判断画不画细节
        self._cd = cd
        t = int(math.floor(f))
        a = f - t
        if t >= self.T:
            t, a = self.T, 0.0
        cur, nxt = self.frame(t), self.frame(t + 1)
        ev = nxt['events'] if t < self.T else []
        orders = nxt.get('orders', {}) if t < self.T else {}
        img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        ox, oy = -vx * c, -vy * c
        P = lambda x, y: (ox + x * c, oy + y * c)          # 格左上角 → 像素
        xr = range(max(0, int(vx) - 1), min(N, int(vx + vs) + 2))
        yr = range(max(0, int(vy) - 1), min(N, int(vy + vs) + 2))

        # 地面和墙
        for y in yr:
            for x in xr:
                x0, y0 = P(x, y)
                if self.map[y][x] == '#':
                    b = max(1, int(c / 8))
                    d.rectangle([x0, y0, x0 + c, y0 + c], fill=WALL_LO)
                    d.rectangle([x0, y0, x0 + c - 1, y0 + c - b], fill=WALL)
                    d.rectangle([x0, y0, x0 + c - 1, y0 + b], fill=WALL_HI)
                else:
                    d.rectangle([x0, y0, x0 + c, y0 + c], fill=FLOOR_A if (x + y) % 2 else FLOOR_B)

        # 基地：3×3 染成阵营色，中间格淡淡印一个头像
        base_layer = Image.new('RGBA', (size, size), (0, 0, 0, 0))
        bd = ImageDraw.Draw(base_layer)
        for k, cells in enumerate(self.bases):
            col = CHAR[self.who[k]]
            if dim and k in dim:
                col = mix(col, (110, 114, 126), .7)
            al = 40 if dim and k in dim else 95
            xs = [p[0] for p in cells]
            ys = [p[1] for p in cells]
            x0, y0 = P(min(xs), min(ys))
            x1, y1 = P(max(xs) + 1, max(ys) + 1)
            bd.rounded_rectangle([x0 + 1, y0 + 1, x1 - 1, y1 - 1], c * .25, fill=col + (al,),
                                 outline=col + (min(255, al + 120),), width=max(1, int(c / 10)))
            cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
            av = avatar(self.who[k], max(8, int(c * 1.9)))
            av = av.copy()
            aa = 45 if dim and k in dim else (85 if self.teams > 8 else 120)
            av.putalpha(av.getchannel('A').point(lambda v: v * aa // 255))
            base_layer.alpha_composite(av, (int(cx - av.width / 2), int(cy - av.height / 2)))
            if self.who[k] in self.multi and cd >= 14:
                bd.text((x1 - c * .2, y0 + c * .2), str(self.dup[k]), font=font(max(8, int(c * .6)), 'Black'),
                        fill=(255, 255, 255, 220), anchor='rt')
        img.alpha_composite(base_layer)
        d = ImageDraw.Draw(img)

        # 旗点：地上一个小圆环
        for (x, y) in self.spots:
            x0, y0 = P(x, y)
            m = c * .18
            d.ellipse([x0 + m, y0 + m, x0 + c - m, y0 + c - m], outline=(255, 226, 92, 80 if self.teams > 8 else 120),
                      width=max(1, int(c / 12)))

        # 角色位置（补间）
        cu = {u['id']: u for u in cur['units']}
        nu = {u['id']: u for u in nxt['units']}
        ordmap = {}
        for team, lst in orders.items():
            for o in lst:
                ordmap[o['id']] = o
        k_move = a                      # 整个回合匀速走完，连续播放时不顿挫
        pos = {}
        for uid, u in cu.items():
            v = nu.get(uid, u)
            if u['pos'] and v['pos']:
                px = lerp(u['pos'][0], v['pos'][0], k_move)
                py = lerp(u['pos'][1], v['pos'][1], k_move)
                o = ordmap.get(uid)
                if o and o.get('move') in DIRS and tuple(u['pos']) == tuple(v['pos']):
                    dx, dy = DIRS[o['move']]       # 想走没走成（抢格、撞墙、被挡）：顶一下再弹回
                    b = .26 * math.sin(math.pi * a)
                    px += dx * b
                    py += dy * b
                pos[uid] = (px, py)
            elif u['pos']:                         # 本回合阵亡：留在原地（下面淡出）
                pos[uid] = tuple(u['pos'])
            elif v['pos']:                         # 本回合重生
                pos[uid] = tuple(v['pos'])

        # 旗子：在地上的（home / dropped）
        cf = {g['id']: g for g in cur['flags']}
        nf = {g['id']: g for g in nxt['flags']}
        fpx = max(8, int(c * 1.05))
        for gid, g in cf.items():
            h = nf.get(gid, g)
            if g['status'] in ('home', 'dropped'):
                if h['status'] == 'carried':           # 本回合被捡走：后半段消失（交给携旗者画）
                    if a > .6:
                        continue
                if h['status'] in ('home', 'dropped') and h['pos'] != g['pos']:   # 回位：原地淡出，新点弹出
                    alpha = 1 - ease(a / .4)
                    self._put_flag(img, g, P, c, fpx, t, alpha=alpha)
                    continue
                self._put_flag(img, g, P, c, fpx, t + a)
        for gid, h in nf.items():
            g = cf.get(gid, h)
            appear = h['status'] in ('home', 'dropped') and (g['status'] not in ('home', 'dropped') or g['pos'] != h['pos'])
            if appear and g['status'] != 'carried':    # 新刷出来 / 回位：后半段弹出
                k = ease((a - .5) / .4)
                if k > 0:
                    self._put_flag(img, h, P, c, fpx, t + a, pop=k)
            elif appear and g['status'] == 'carried' and a > .55:   # 携旗者阵亡，旗掉地上
                self._put_flag(img, h, P, c, fpx, t + a, pop=ease((a - .55) / .3))

        # 角色
        died = {e['unit'] for e in ev if e['type'] == 'death'}
        held = []
        s = c * .9
        order = sorted(pos, key=lambda uid: (ring is not None and uid in ring, (cu[uid].get('flag') is not None)))
        for uid in order:
            u, v = cu[uid], nu.get(uid, cu[uid])
            team = self.team_of(uid)
            who = self.who[team]
            px, py = pos[uid]
            alpha, scale = 1.0, 1.0
            if not u['pos'] and v['pos']:               # 重生：后半段弹出
                k = ease((a - .5) / .35)
                if k <= 0:
                    continue
                scale, alpha = .4 + .6 * k, k
            if uid in died:                              # 阵亡：攻击命中后缩小淡出
                k = ease((a - .72) / .26)
                if k >= 1:
                    continue
                scale, alpha = 1 - .5 * k, 1 - k
            if dim and team in dim:
                alpha *= .45
            cx, cy = P(px + .5, py + .5)
            ss = max(6, int(s * scale))
            col = CHAR[who] if not (dim and team in dim) else mix(CHAR[who], (110, 114, 126), .7)
            if ring and uid in ring:
                rr = ss / 2 + c * .18
                d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], outline=(255, 214, 90), width=max(2, int(c / 7)))
            bw = max(2, int(ss / 9))
            dot = Image.new('RGBA', (ss + 2 * bw, ss + 2 * bw), (0, 0, 0, 0))
            dd = ImageDraw.Draw(dot)
            dd.ellipse([0, 0, ss + 2 * bw - 1, ss + 2 * bw - 1], fill=col)
            dot.alpha_composite(avatar(who, ss), (bw, bw))
            if alpha < 1:
                dot.putalpha(dot.getchannel('A').point(lambda q: int(q * alpha)))
            img.alpha_composite(dot, (int(cx - dot.width / 2), int(cy - dot.height / 2)))
            if hp and cd >= 22 and alpha > .3:            # 血量：脚下三格，挨一下少一格（三下必死）
                hpv = u['hp'] if a < .62 else v['hp']
                pips = math.ceil(hpv / 34) if hpv > 0 else 0
                if 0 < pips < 3:                         # 满血不画，省得满屏小格子
                    pw, ph, gap = c * .2, max(2, c * .09), c * .05
                    x0 = cx - (3 * pw + 2 * gap) / 2
                    y0 = cy + dot.height / 2 + c * .02
                    hc = (250, 200, 70) if pips == 2 else (250, 80, 70)
                    for j in range(3):
                        d.rectangle([x0 + j * (pw + gap), y0, x0 + j * (pw + gap) + pw, y0 + ph],
                                    fill=hc if j < pips else (60, 66, 82))
            # 携旗：头顶一面旗（交旗那一回合，旗在后半段沉进基地）
            carrying = u.get('flag') if u.get('flag') is not None else (v.get('flag') if a > .6 and v['pos'] else None)
            if carrying is not None and alpha > .05:
                g_next = nf.get(carrying)
                cap = g_next and g_next['status'] == 'cooldown'
                fk = 1.0
                if cap:
                    fk = 1 - ease((a - .55) / .35)
                if fk > 0:
                    held.append((cx, cy, ss, fk * alpha, fk))
            d = ImageDraw.Draw(img)
        # 携旗者头顶的旗最后画，免得被旁边后画的队友盖住
        for cx, cy, ss, al, fk in held:
            fi = flag_icon(max(8, int(c * .95)))
            fi.putalpha(fi.getchannel('A').point(lambda q: int(q * al)))
            img.alpha_composite(fi, (int(cx - c * .15), int(cy - ss / 2 - c * .75 + (1 - fk) * c * .4)))
        d = ImageDraw.Draw(img)

        # 攻击（画在角色上层）：一枚光弹飞向目标，命中处炸开白圈
        atk = [e for e in ev if e['type'] == 'attack']
        k_atk = (a - .35) / .45
        if 0 < k_atk < 1.25 and atk:
            fx = Image.new('RGBA', (size, size), (0, 0, 0, 0))
            fd = ImageDraw.Draw(fx)
            for e in atk:
                s_, tg = e['unit'], e['target']
                if s_ not in pos or tg not in pos:
                    continue
                col = CHAR[self.who[self.team_of(s_)]]
                al = int(255 * max(0.0, min(1.0, (1.25 - k_atk) / .45)))
                if dim and self.team_of(s_) in dim:          # 压暗的阵营：光束也淡下去
                    col, al = mix(col, (110, 114, 126), .7), int(al * .35)
                sx, sy = P(pos[s_][0] + .5, pos[s_][1] + .5)
                tx, ty = P(pos[tg][0] + .5, pos[tg][1] + .5)
                kk = min(1.0, k_atk / .4)
                if kk < 1:                       # 飞行中的光弹 + 拖尾
                    ex, ey = lerp(sx, tx, kk), lerp(sy, ty, kk)
                    bx, by = lerp(sx, tx, max(0, kk - .35)), lerp(sy, ty, max(0, kk - .35))
                    fd.line([(bx, by), (ex, ey)], fill=col + (al,), width=max(2, int(c / 6)))
                    r = c * .16
                    fd.ellipse([ex - r, ey - r, ex + r, ey + r], fill=(255, 255, 255, al))
                else:                            # 命中：白圈扩散
                    q = min(1.0, (k_atk - .4) / .5)
                    r = c * (.3 + .35 * q)
                    fd.ellipse([tx - r, ty - r, tx + r, ty + r], outline=(255, 255, 255, int(al * (1 - q * .7))),
                               width=max(2, int(c / 8)))
                    fd.ellipse([tx - r * .5, ty - r * .5, tx + r * .5, ty + r * .5], fill=col + (int(al * .45 * (1 - q)),))
                    if cd >= 30:                 # 镜头推近时标出伤害
                        fd.text((tx + c * .35, ty - c * (.45 + .3 * q)), '-34', font=font(int(c * .42), 'Black'),
                                fill=(255, 96, 86, al), anchor='lm', stroke_width=2, stroke_fill=(10, 12, 20, al))
            img.alpha_composite(fx.filter(ImageFilter.GaussianBlur(max(1, c * .12))))
            img.alpha_composite(fx)
            d = ImageDraw.Draw(img)

        # 阵亡标记：原地一个 ×
        for e in ev:
            if e['type'] == 'death' and a > .72:
                k = ease((a - .72) / .18)
                x, y = e['pos']
                cx, cy = P(x + .5, y + .5)
                r = c * .32
                al = int(220 * (1 - max(0, a - .9) * 6))
                col = CHAR[self.who[self.team_of(e['unit'])]]
                d.line([(cx - r * k, cy - r * k), (cx + r * k, cy + r * k)], fill=col + (al,), width=max(2, int(c / 6)))
                d.line([(cx + r * k, cy - r * k), (cx - r * k, cy + r * k)], fill=col + (al,), width=max(2, int(c / 6)))

        # 交旗：基地上方飘起 "+1"
        for e in ev:
            if e['type'] == 'capture' and a > .6:
                k = ease((a - .6) / .4)
                cells = self.bases[e['team']]
                xs = [p[0] for p in cells]
                ys = [p[1] for p in cells]
                cx, cy = P(sum(xs) / 9 + .5, min(ys) - .2 - k * .8)
                fs = max(10, int(c * 1.3))
                col = CHAR[self.who[e['team']]]
                d.text((cx, cy), '+1', font=font(fs, 'Black'), fill=col + (int(255 * min(1, 2 - 2 * k + .6)),),
                       anchor='mm', stroke_width=max(1, fs // 9), stroke_fill=(10, 12, 20))

        # 指令注释气泡（选手写在输出里的 "# 抢旗;护送;拦截"）
        if notes:
            for team in notes:
                for o in orders.get(str(team), []):
                    note = note_text(o.get('note') or '')
                    if not note or o['id'] not in pos:
                        continue
                    self._bubble(img, P(pos[o['id']][0] + .5, pos[o['id']][1] - .1), note, c, CHAR[self.who[team]])
        return img

    def _put_flag(self, img, g, P, c, fpx, tf, alpha=1.0, pop=1.0):
        x, y = g['pos']
        x0, y0 = P(x, y)
        dropped = g['status'] == 'dropped'
        px = max(6, int(fpx * (.5 + .5 * pop)))
        fi = flag_icon(px, tilt=-28 if dropped else 0)
        if alpha * pop < 1:
            fi.putalpha(fi.getchannel('A').point(lambda q: int(q * alpha * min(1, pop * 1.5))))
        img.alpha_composite(fi, (int(x0 + c / 2 - px * .38), int(y0 + c * .92 - px * .92)))
        if dropped and g.get('return_at') and self._cd >= 14:     # 落地旗：倒计时圆环（15 回合无人捡就回位）
            left = max(0.0, (g['return_at'] - tf) / 15)
            d = ImageDraw.Draw(img)
            m = c * .06
            d.arc([x0 + m, y0 + m, x0 + c - m, y0 + c - m], -90, -90 + 360 * left,
                  fill=(255, 226, 92, int(200 * alpha)), width=max(1, int(c / 9)))

    def _bubble(self, img, xy, text, c, col):
        fs = max(12, int(c * .7))
        f = font(fs)
        w = int(f.getlength(text)) + fs
        h = int(fs * 1.5)
        x, y = xy
        lay = Image.new('RGBA', (w + 4, h + int(fs * .5) + 4), (0, 0, 0, 0))
        d = ImageDraw.Draw(lay)
        d.rounded_rectangle([2, 2, w + 1, h + 1], fs * .4, fill=(250, 250, 255, 235), outline=col + (255,), width=2)
        d.polygon([(w / 2 - fs * .3, h), (w / 2 + fs * .3, h), (w / 2, h + fs * .5)], fill=(250, 250, 255, 235))
        d.text((w / 2 + 1, h / 2 + 2), text, font=f, fill=(20, 24, 36), anchor='mm')
        img.alpha_composite(lay, (int(x - lay.width / 2), int(y - lay.height)))

    # ---------- 比分栏 ----------
    def scoreboard(self, f, w, h, highlight=None):
        """竖排比分：按当前得分排序，头像 + 名字 + 分数；正在携旗的阵营后面挂小旗。"""
        t = min(self.T, int(f))
        fr = self.frame(t)
        sc = fr['score']
        carry = {}
        for u in fr['units']:
            if u.get('flag') is not None:
                carry[self.team_of(u['id'])] = carry.get(self.team_of(u['id']), 0) + 1
        img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        d.rounded_rectangle([0, 0, w - 1, h - 1], 22, fill=(12, 16, 28, 215), outline=(255, 255, 255, 40), width=2)
        d.text((24, 22), f'第 {t} 回合', font=font(30), fill=(235, 238, 245), anchor='lt')
        d.text((w - 24, 26), f'/ {self.T}', font=font(22, 'Medium'), fill=(150, 156, 172), anchor='rt')
        n = self.teams
        top = 76
        row = min(64, (h - top - 12) / n)
        av = int(row * .78)
        order = sorted(range(n), key=lambda k: (-sc[k], k))
        for i, k in enumerate(order):
            y = top + i * row
            col = CHAR[self.who[k]]
            if highlight and k in highlight:
                d.rounded_rectangle([10, y + 1, w - 10, y + row - 2], 12, fill=col + (60,))
            img.alpha_composite(avatar(self.who[k], av), (20, int(y + (row - av) / 2)))
            d = ImageDraw.Draw(img)
            fs = int(min(30, row * .46))
            d.text((32 + av, y + row / 2), self.label(k), font=font(fs), fill=(235, 238, 245), anchor='lm')
            d.text((w - 24, y + row / 2), str(sc[k]), font=font(int(fs * 1.15), 'Black'), fill=col, anchor='rm')
            if carry.get(k):
                fi = flag_icon(int(row * .62))
                for j in range(carry[k]):
                    img.alpha_composite(fi, (int(w - 90 - j * row * .4), int(y + row * .16)))
                d = ImageDraw.Draw(img)
        return img
