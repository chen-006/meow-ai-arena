# -*- coding: utf-8 -*-
"""顶部信息栏、击杀播报、塌陷预警、入场名牌、胜者横幅（普通 Python + PIL）。每帧一张透明 PNG，由 ffmpeg 叠到画面上。
场上不放常驻文字：名字只在入场落地后的一小会儿出现在头顶，之后只在信息栏里。
画面内容相同的连续帧只画一次，后面的帧用硬链接指向同一个文件。"""
import math
import os
import shutil
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import replay  # noqa: E402
import timeline as TLM  # noqa: E402
from pose import ease, clamp  # noqa: E402

ART = os.environ.get("DART_AVATARS") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "avatars")  # 正式形象 v2 的头像源图
ART_FILES = {"claude": "Claude_大_Opus.png", "gpt": "GPT_大_Astra.png", "gemini": "Gemini.png", "doubao": "豆包.png",
             "kimi": "Kimi.png", "deepseek": "DeepSeek.png", "grok": "Grok.png", "hunyuan": "混元.png"}
ART_TWEAK = {"claude": (0.03, 0.04, 0.84), "gpt": (0.05, 0.08, 0.8), "gemini": (0.0, 0.08, 0.82),
             "doubao": (0.0, 0.0, 0.9), "kimi": (0.0, 0.07, 0.82), "deepseek": (0.0, 0.06, 0.82),
             "grok": (0.0, 0.07, 0.82), "hunyuan": (-0.02, 0.06, 0.82)}
LABEL = {"claude": "Claude", "gpt": "GPT", "gemini": "Gemini", "doubao": "豆包", "kimi": "Kimi",
         "deepseek": "DeepSeek", "grok": "Grok", "hunyuan": "混元"}
COLOR = {"claude": "#FF8A1F", "gpt": "#10C08A", "gemini": "#7B6BFF", "doubao": "#FF5A4E", "kimi": "#35C6F0",
         "deepseek": "#2F6BFF", "grok": "#FFC21A", "hunyuan": "#C64FD6"}
import cast  # noqa: E402
import voxchars  # noqa: E402
for _k in cast.ORDER12 + cast.EXTRA:    # 正赛 12 位（+ 名单外可点名的小 Gemini）：名字、立绘、头像裁切
    LABEL[_k] = voxchars.label(_k)
    ART_FILES[_k] = cast.ART12[_k]
    ART_TWEAK[_k] = cast.ART_TWEAK12[_k]
    if _k not in COLOR or cast.style() == "voxel":
        COLOR[_k] = voxchars.color(_k)
# 显示名临时覆盖（11-18 第 11 集对局渲染加）：环境变量 DART_LABELS="gemini=Carbon,gemini_s=Hydrogen"；不设时和原来完全一样
for _kv in os.environ.get("DART_LABELS", "").split(","):
    if "=" in _kv:
        _k, _v = _kv.split("=", 1)
        LABEL[_k.strip()] = _v.strip()
if cast.style() == "voxel":             # 旧名字在像素造型下跟着换主色
    for _old, _new in cast.TO_VOXEL.items():
        COLOR[_old] = voxchars.color(_new)
TEAM = ("#FF5B4D", "#2E8CFF")
TEAM_NAME = ("红队", "蓝队")
MODE_NAME = {0: "单挑", 1: "四人乱斗", 2: "4 v 4"}
CAUSE_SHORT = {1: "镖", 2: "砍", 3: "坠落"}
DEFAULT_ROSTER = {n: cast.default_roster(n) for n in (2, 4, 8)}

FONT_VF = os.environ.get("DART_FONT") or r"C:\Windows\Fonts\NotoSansSC-VF.ttf"   # 可变字重的 Noto Sans SC；别的字体用 DART_FONT 指定
INK = (46, 40, 36)
SUB = (128, 118, 108)
PANEL = (255, 252, 246, 238)
W, H = 1920, 1080
_F = {}


def font(size, weight="Bold"):
    k = (size, weight)
    if k not in _F:
        f = ImageFont.truetype(FONT_VF, size)
        try:
            f.set_variation_by_name(weight)
        except Exception:
            try:
                f.set_variation_by_axes([{"Bold": 700, "Medium": 500, "Regular": 400, "Black": 900}.get(weight, 700)])
            except Exception:
                pass
        _F[k] = f
    return _F[k]


def rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


_AV = {}


def avatar(key, size, dead=False):
    k = (key, size, dead)
    if k in _AV:
        return _AV[k]
    im = Image.open(os.path.join(ART, ART_FILES[key])).convert("RGBA")
    a = np.array(im)[:, :, 3]
    ys, xs = np.where(a > 40)
    top, bot = ys.min(), ys.max()
    hh = bot - top
    band = ys < top + 0.16 * hh
    cx = xs[band].mean()
    cy = top + 0.1 * hh
    s = 0.24 * hh
    dx, dy, sc = ART_TWEAK[key]
    cx += dx * s
    cy += dy * s
    s *= sc
    crop = im.crop((int(cx - s / 2), int(cy - s / 2), int(cx + s / 2), int(cy + s / 2))).resize((size * 3, size * 3), Image.LANCZOS)
    bg = Image.new("RGBA", crop.size, (250, 246, 238, 255))
    bg.alpha_composite(crop)
    if dead:
        g = bg.convert("L").convert("RGBA")
        bg = Image.blend(g, Image.new("RGBA", g.size, (240, 236, 230, 255)), 0.35)
    mask = Image.new("L", bg.size, 0)
    ImageDraw.Draw(mask).ellipse((0, 0, bg.size[0] - 1, bg.size[1] - 1), fill=255)
    out = Image.new("RGBA", bg.size, (0, 0, 0, 0))
    out.paste(bg, (0, 0), mask)
    out = out.resize((size, size), Image.LANCZOS)
    _AV[k] = out
    return out


def paste_clip(canvas, layer, x, y):
    """把 layer 叠到 canvas 的 (x, y)，超出画布的部分裁掉。"""
    x, y = int(x), int(y)
    sx, sy = max(0, -x), max(0, -y)
    ex, ey = min(layer.size[0], canvas.size[0] - x), min(layer.size[1], canvas.size[1] - y)
    if ex <= sx or ey <= sy:
        return
    canvas.alpha_composite(layer.crop((sx, sy, ex, ey)), (x + sx, y + sy))


def panel(canvas, box, r, fill=PANEL, blur=10, alpha=55):
    box = tuple(int(v) for v in box)
    pad = blur * 3
    sh = Image.new("RGBA", (box[2] - box[0] + pad * 2, box[3] - box[1] + pad * 2), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle((pad, pad + 4, sh.size[0] - pad, sh.size[1] - pad + 4), r, fill=(40, 50, 90, alpha))
    sh = sh.filter(ImageFilter.GaussianBlur(blur))
    paste_clip(canvas, sh, box[0] - pad, box[1] - pad)
    p = Image.new("RGBA", (box[2] - box[0], box[3] - box[1]), (0, 0, 0, 0))
    ImageDraw.Draw(p).rounded_rectangle((0, 0, p.size[0] - 1, p.size[1] - 1), r, fill=fill)
    paste_clip(canvas, p, box[0], box[1])


def card(canvas, x, y, w, h, key, alive, status, ring_col, av=62, name_size=26, sub_size=18, tint=None, center=False,
         pop=0.0):
    d = ImageDraw.Draw(canvas)
    if tint is not None:
        d.rounded_rectangle((x, y, x + w, y + h), 16, fill=tint)
    fn = font(name_size, "Bold")
    fs = font(sub_size, "Medium")
    name = LABEL[key]
    if center:
        cw_ = av + 12 + max(d.textlength(name, font=fn), d.textlength(status, font=fs) + 16)
        ax = int(x + max(10, (w - cw_) / 2))
    else:
        ax = x + 10
    ay = y + (h - av) // 2
    rc = rgb(ring_col)
    g = int(4 + 5 * pop)
    d.ellipse((ax - g, ay - g, ax + av + g - 1, ay + av + g - 1), fill=rc + (255,) if alive else (196, 190, 184, 255))
    d.ellipse((ax - 1, ay - 1, ax + av, ay + av), fill=(255, 255, 255, 255))
    paste_clip(canvas, avatar(key, av, dead=not alive), ax, ay)
    tx = ax + av + 12
    nc = INK if alive else (168, 160, 152)
    while d.textlength(name, font=fn) > w - (tx - x) - 8 and fn.size > 14:
        fn = font(fn.size - 1, "Bold")
    d.text((tx, y + h // 2 - name_size + 1), name, font=fn, fill=nc)
    if not alive:
        tw = d.textlength(name, font=fn)
        d.line((tx, y + h // 2 - name_size // 2 + 2, tx + tw, y + h // 2 - name_size // 2 + 2), fill=(168, 160, 152), width=2)
    sy = y + h // 2 + 4
    if alive:
        d.ellipse((tx, sy + 5, tx + 9, sy + 14), fill=rgb(COLOR[key]))
        d.text((tx + 15, sy - 1), status, font=fs, fill=SUB)
    else:
        d.rounded_rectangle((tx - 1, sy, tx + d.textlength(status, font=fs) + 13, sy + sub_size + 6), 8, fill=(236, 230, 224))
        d.text((tx + 6, sy + 1), status, font=fs, fill=(150, 140, 130))


class Hud:
    def __init__(self, rp, roster=None):
        self.rp = rp
        self.roster = roster or cast.default_roster(rp.n)
        self.tl = TLM.Timeline(rp, self.roster)
        A = self.tl.anim
        self.team_mode = rp.mode == 2
        # 播报：(时刻, 文字段落)
        self.feed = []
        for k, (t, cause, killer) in sorted(rp.deaths.items(), key=lambda kv: kv[1][0]):
            Td, cause, d, wg = A.death_info(k)
            if cause == 1:
                self.feed.append((Td, [("av", killer), ("name", killer), ("txt", "的飞镖击中"), ("av", k), ("name", k)]))
            elif cause == 2:
                self.feed.append((Td, [("av", killer), ("name", killer), ("txt", "一刀砍中"), ("av", k), ("name", k)]))
            elif killer >= 0:
                self.feed.append((Td, [("av", killer), ("name", killer), ("txt", "把"), ("av", k), ("name", k), ("txt", "踢下了悬崖")]))
            elif wg:
                self.feed.append((Td, [("av", k), ("name", k), ("txt", "随地块坠落")]))
            else:
                self.feed.append((Td, [("av", k), ("name", k), ("txt", "失足坠落")]))

    # ---- 某一刻的状态
    def state(self, gt):
        rp = self.rp
        t = int(clamp(gt, 0, rp.T))
        st = rp.states[t]
        alive, kills, cause = [], [], []
        for k in range(rp.n):
            dth = rp.deaths.get(k)
            dead = dth is not None and gt >= dth[0] + 1.0
            alive.append(not dead)
            cause.append(dth[1] if dead else 0)
            # 击杀数：按已经发生的出局算
            kills.append(sum(1 for v, (tt, c, kl) in rp.deaths.items() if kl == k and v != k and gt >= tt + 1.0))
        left = max(0.0, rp.cfg.get("STEPS", 800) - clamp(gt, 0, rp.T)) / 20.0
        return alive, kills, cause, int(math.ceil(left - 1e-6))

    def draw(self, f):
        """返回 (缓存键, 画图函数)。"""
        rp, tl = self.rp, self.tl
        gt = tl.frames[f]["gt"]
        alive, kills, cause, left = self.state(gt)
        # 信息栏滑入
        first_land = min(tl.anim.t_land.values())
        slide = ease((gt - (first_land - 6)) / 7.0) if gt < 0 else 1.0
        key = [tuple(alive), tuple(kills), left, round(slide, 3)]
        # 最近一次出局：头像圈放大一下
        pops = []
        for k in range(rp.n):
            dth = rp.deaths.get(k)
            pops.append(round(clamp(1 - (gt - dth[0] - 1.0) / 8.0), 2) if (dth and gt >= dth[0] + 1.0) else 0.0)
        key.append(tuple(pops))
        # 播报
        toasts = []
        for (T, segs) in self.feed:
            u = gt - T
            if 0 <= u < 52:
                a = ease(u / 3.0) * (1 - ease((u - 44) / 8.0)) * (1 - ease((gt - rp.T - 4) / 5.0))
                if a < 0.02:
                    continue
                toasts.append((segs, round(a, 2), round(1 - ease(u / 4.0), 2)))
        toasts = toasts[-2:]
        key.append(tuple((id(s), a, o) for s, a, o in toasts))
        # 塌陷预警
        warn = None
        wn = rp.cfg.get("COL_WARN", 30)
        for w, s in enumerate(rp.wave_steps):
            if s - wn <= gt < s + 8 and (s <= rp.T or gt <= rp.T + 1.0):
                if gt < s:
                    warn = ("地块即将崩落", round(0.5 + 0.5 * math.sin(gt * 0.9) ** 2, 1), False)
                else:
                    warn = ("第 %d 波崩落" % (w + 1), round(1 - ease((gt - s - 3) / 5.0), 2), True)
        key.append(warn)
        # 入场名牌
        tags = []
        if gt < 0:
            for k in range(rp.n):
                u = gt - tl.anim.t_land[k]
                a = ease((u - 2.0) / 3.0) * (1 - ease((gt + 9.0) / 5.0))
                if a > 0.02:
                    x, y = rp.char_pos(k, 0)
                    px, py = tl.screen(f, (x, -y, 1.55))
                    tags.append((k, int(px), int(py), round(a, 2)))
        key.append(tuple(tags))
        # 胜者横幅
        banner = None
        if gt > rp.T + 8:
            a = round(ease((gt - rp.T - 8) / 6.0), 2)
            if rp.winner < 0:
                banner = ("平局", "#8A8078", a, None)
            elif self.team_mode:
                banner = (TEAM_NAME[rp.winner] + " 获胜", TEAM[rp.winner], a, None)
            else:
                winner_key = self.roster[rp.winner]
                winner_name = LABEL[winner_key]
                title = "千问获胜" if winner_name == "千问" else winner_name + " 获胜"
                banner = (title, COLOR[winner_key], a, rp.winner)
        key.append(banner)

        def paint():
            cv = Image.new("RGBA", (W, H), (0, 0, 0, 0))
            oy = int(-130 * (1 - slide))
            if self.team_mode:
                self._bar_team(cv, oy, alive, kills, cause, left, pops)
            else:
                self._bar_ffa(cv, oy, alive, kills, cause, left, pops)
            y = 122
            if warn:
                self._warn(cv, y, *warn)
                y += 58
            for segs, a, off in toasts:
                self._toast(cv, y - int(14 * off), segs, a)
                y += 60
            for (k, px, py, a) in tags:
                self._tag(cv, k, px, py, a)
            if banner:
                self._banner(cv, *banner)
            return cv
        return tuple(key), paint

    # ---- 各部分
    def _bar_ffa(self, cv, oy, alive, kills, cause, left, pops):
        n = self.rp.n
        box = (24, 14 + oy, W - 24, 106 + oy)
        panel(cv, box, 26)
        d = ImageDraw.Draw(cv)
        mid_w = 190
        cw = (box[2] - box[0] - mid_w - 30) // n
        x = box[0] + 12
        for i in range(n):
            if i == n // 2:
                x += mid_w
            key = self.roster[i]
            st = ("击杀 %d" % kills[i]) if alive[i] else ("出局 · " + CAUSE_SHORT.get(cause[i], ""))
            card(cv, x, box[1] + 8, cw - 10, box[3] - box[1] - 16, key, alive[i], st, COLOR[key], center=True, pop=pops[i])
            x += cw
        cx = box[0] + 12 + cw * (n // 2) + mid_w // 2
        d.rounded_rectangle((cx - 74, box[1] + 14, cx + 74, box[3] - 14), 20, fill=(246, 240, 230))
        tt = "%d:%02d" % (left // 60, left % 60)
        f1, f2 = font(34, "Bold"), font(15, "Medium")
        d.text((cx - d.textlength(tt, font=f1) / 2, box[1] + 18), tt, font=f1, fill=INK)
        lab = MODE_NAME.get(self.rp.mode, "")
        d.text((cx - d.textlength(lab, font=f2) / 2, box[1] + 60), lab, font=f2, fill=SUB)

    def _bar_team(self, cv, oy, alive, kills, cause, left, pops):
        """4v4：左右各一整块队伍色的分区（上沿一条实色带 + 队名），中间是存活比分。"""
        rp = self.rp
        box = (20, 12 + oy, W - 20, 108 + oy)
        panel(cv, box, 24)
        d = ImageDraw.Draw(cv)
        mid_w = 214
        per = rp.n // 2
        cw = (box[2] - box[0] - mid_w - 20) // rp.n
        cnt = [0, 0]
        idx = [0, 0]
        for team in (0, 1):
            tc = rgb(TEAM[team])
            zx0 = box[0] + 8 + (cw * per + mid_w if team == 1 else 0)
            zx1 = zx0 + cw * per + 2
            zone = Image.new("RGBA", (zx1 - zx0, box[3] - box[1] - 12), (0, 0, 0, 0))
            zd = ImageDraw.Draw(zone)
            zd.rounded_rectangle((0, 0, zone.size[0] - 1, zone.size[1] - 1), 18, fill=tuple(int(255 * 0.8 + v * 0.2) for v in tc) + (255,))
            band = Image.new("RGBA", zone.size, (0, 0, 0, 0))
            ImageDraw.Draw(band).rectangle((0, 0, zone.size[0], 7), fill=tc + (255,))
            zone.alpha_composite(Image.composite(band, Image.new("RGBA", zone.size, (0, 0, 0, 0)), zone.getchannel("A")))
            paste_clip(cv, zone, zx0, box[1] + 6)
        d = ImageDraw.Draw(cv)
        for k in range(rp.n):
            team = rp.players[k][1]
            if alive[k]:
                cnt[team] += 1
            i = idx[team]
            idx[team] += 1
            x = box[0] + 10 + cw * (i + (per if team == 1 else 0)) + (mid_w if team == 1 else 0)
            key = self.roster[k]
            st = ("击杀 %d" % kills[k]) if alive[k] else ("出局 · " + CAUSE_SHORT.get(cause[k], ""))
            card(cv, x, box[1] + 14, cw - 8, box[3] - box[1] - 22, key, alive[k], st, TEAM[team], av=54, name_size=22,
                 sub_size=16, tint=None, pop=pops[k])
        cx = box[0] + 10 + cw * per + mid_w // 2 - 4
        f1, f2, f3 = font(42, "Black"), font(15, "Medium"), font(17, "Bold")
        a, b = str(cnt[0]), str(cnt[1])
        d.text((cx - 18 - d.textlength(a, font=f1), box[1] + 3), a, font=f1, fill=rgb(TEAM[0]))
        d.text((cx - 7, box[1] + 5), ":", font=font(36, "Bold"), fill=(170, 160, 150))
        d.text((cx + 18, box[1] + 3), b, font=f1, fill=rgb(TEAM[1]))
        for team, sg in ((0, -1), (1, 1)):
            nm = TEAM_NAME[team]
            tw = d.textlength(nm, font=f3)
            px = cx - 62 - tw / 2 if sg < 0 else cx + 62 - tw / 2
            d.rounded_rectangle((px - 9, box[1] + 58, px + tw + 9, box[1] + 84), 12, fill=rgb(TEAM[team]) + (255,))
            d.text((px, box[1] + 59), nm, font=f3, fill=(255, 255, 255))
        lab = "%d:%02d" % (left // 60, left % 60)
        d.text((cx - d.textlength(lab, font=f2) / 2, box[1] + 62), lab, font=f2, fill=SUB)

    def _layer(self, cv, layer, a):
        if a < 0.999:
            al = layer.getchannel("A").point(lambda v: int(v * a))
            layer.putalpha(al)
        cv.alpha_composite(layer)

    def _toast(self, cv, y, segs, a):
        lay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(lay)
        f, ft = font(24, "Bold"), font(22, "Medium")
        av = 36
        wd = 0
        for kind, v in segs:
            wd += (av + 8) if kind == "av" else (d.textlength(LABEL[self.roster[v]] if kind == "name" else v,
                                                               font=f if kind == "name" else ft) + 10)
        x0 = W / 2 - wd / 2
        panel(lay, (x0 - 22, y, x0 + wd + 14, y + 50), 25, blur=8, alpha=45)
        d = ImageDraw.Draw(lay)
        x = x0
        for kind, v in segs:
            if kind == "av":
                key = self.roster[v]
                col = TEAM[self.rp.players[v][1]] if self.team_mode else COLOR[key]
                d.ellipse((x - 3, y + 4, x + av + 2, y + 9 + av), fill=rgb(col))
                lay.alpha_composite(avatar(key, av), (int(x), y + 7))
                d = ImageDraw.Draw(lay)
                x += av + 8
            elif kind == "name":
                s = LABEL[self.roster[v]]
                d.text((x, y + 8), s, font=f, fill=INK)
                x += d.textlength(s, font=f) + 10
            else:
                d.text((x, y + 10), v, font=ft, fill=SUB)
                x += d.textlength(v, font=ft) + 10
        self._layer(cv, lay, a)

    def _warn(self, cv, y, text, a, done):
        lay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(lay)
        f = font(24, "Bold")
        tw = d.textlength(text, font=f)
        x0 = W / 2 - tw / 2 - 16
        panel(lay, (x0 - 24, y, x0 + tw + 56, y + 48), 24, fill=(255, 244, 232, 240) if not done else PANEL, blur=8, alpha=45)
        d = ImageDraw.Draw(lay)
        cx, cy = x0 + 2, y + 24
        d.polygon([(cx - 12, cy + 10), (cx + 12, cy + 10), (cx, cy - 12)], fill=(255, 122, 60))
        d.text((cx - 3.5, cy - 9), "!", font=font(17, "Black"), fill=(255, 255, 255))
        d.text((x0 + 26, y + 7), text, font=f, fill=(200, 80, 40) if not done else INK)
        self._layer(cv, lay, a if done else 0.6 + 0.4 * a)

    def _tag(self, cv, k, px, py, a):
        lay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(lay)
        key = self.roster[k]
        f = font(22, "Bold")
        s = LABEL[key]
        tw = d.textlength(s, font=f)
        col = TEAM[self.rp.players[k][1]] if self.team_mode else COLOR[key]
        x0, y0 = px - tw / 2 - 14, py - 40
        d.rounded_rectangle((x0, y0, x0 + tw + 28, y0 + 34), 17, fill=rgb(col) + (245,))
        d.polygon([(px - 7, y0 + 33), (px + 7, y0 + 33), (px, y0 + 42)], fill=rgb(col) + (245,))
        d.text((x0 + 14, y0 + 1), s, font=f, fill=(255, 255, 255))
        self._layer(cv, lay, a)

    def _banner(self, cv, text, col, a, seat):
        lay = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(lay)
        f = font(58, "Black")
        tw = d.textlength(text, font=f)
        av = 84 if seat is not None else 0
        wd = tw + (av + 22 if av else 0)
        x0 = W / 2 - wd / 2
        y = 136 + int(18 * (1 - a))
        panel(lay, (x0 - 44, y, x0 + wd + 44, y + 116), 58, blur=14, alpha=70)
        d = ImageDraw.Draw(lay)
        d.rounded_rectangle((x0 - 44, y + 104, x0 + wd + 44, y + 116), 6, fill=rgb(col))
        x = x0
        if av:
            d.ellipse((x - 5, y + 11, x + av + 4, y + 20 + av), fill=rgb(col))
            lay.alpha_composite(avatar(self.roster[seat], av), (int(x), y + 16))
            d = ImageDraw.Draw(lay)
            x += av + 22
        d.text((x, y + 12), text, font=f, fill=INK)
        self._layer(cv, lay, a)


def render_range(rp_path, out_dir, roster=None, a=0, b=None):
    rp = replay.load(rp_path)
    hud = Hud(rp, roster)
    b = hud.tl.n if b is None else min(b, hud.tl.n)
    os.makedirs(out_dir, exist_ok=True)
    last_key, last_path = None, None
    for f in range(a, b):
        dst = os.path.join(out_dir, "%05d.png" % f)
        key, paint = hud.draw(f)
        if key == last_key and last_path and os.path.exists(last_path):
            if os.path.exists(dst):
                os.remove(dst)
            try:
                os.link(last_path, dst)
            except OSError:
                shutil.copyfile(last_path, dst)
            continue
        paint().save(dst, compress_level=1)
        last_key, last_path = key, dst
    return b - a


def still(rp_path, f, out_png, roster=None):
    rp = replay.load(rp_path)
    hud = Hud(rp, roster)
    key, paint = hud.draw(f)
    paint().save(out_png)


if __name__ == "__main__":
    n = render_range(sys.argv[1], sys.argv[2], sys.argv[3].split(",") if len(sys.argv) > 3 and sys.argv[3] else None)
    print("hud frames", n)
