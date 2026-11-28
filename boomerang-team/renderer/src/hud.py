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

ART = os.environ.get("DART_AVATARS") or os.path.join(HERE, "..", "..", "..", "boomerang", "renderer", "assets", "avatars")
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
for _k in voxchars.DEMO:                # 示范小人上信息栏时补齐颜色和默认名（可用 DART_LABELS 覆盖显示名）
    COLOR.setdefault(_k, voxchars.color(_k)); LABEL.setdefault(_k, voxchars.label(_k))
LABEL.update(cast.LABEL12)              # 第 12 集：Carbon / Hydrogen 的显示名
# 显示名覆盖：环境变量 DART_LABELS="gemini=Carbon,gemini_s=Hydrogen"；不设时使用默认名
for _kv in os.environ.get("DART_LABELS", "").split(","):
    if "=" in _kv:
        _k, _v = _kv.split("=", 1)
        LABEL[_k.strip()] = _v.strip()
if cast.style() == "voxel":             # 旧名字在像素造型下跟着换主色
    for _old, _new in cast.TO_VOXEL.items():
        COLOR[_old] = voxchars.color(_new)
TEAM = ("#FF5B4D", "#2E8CFF")       # 只是兜底；画的时候一律用 Hud.tcol（按队伍身份算的）
TEAM_NAME = ("红队", "蓝队")
MODE_NAME = {0: "单挑", 1: "四人乱斗", 2: "4 v 4", 3: "歼灭", 4: "占点", 5: "刺王"}
NEUTRAL = (196, 190, 182)
GOLD = (255, 196, 46)
CAUSE_SHORT = {1: "镖", 2: "砍", 3: "坠落"}
DEFAULT_ROSTER = {n: cast.default_roster(n) for n in (2, 4, 8)}

FONT_VF = os.environ.get("DART_FONT") or r"C:\Windows\Fonts\NotoSansSC-VF.ttf"
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


# 环境变量 DART_AVATAR_FILES="demo_blue=/path/to/avatar.png"：给某个造型直接指定现成圆头像（不按立绘裁切）
_AVF = dict(kv.split("=", 1) for kv in os.environ.get("DART_AVATAR_FILES", "").split(";") if "=" in kv)


def avatar(key, size, dead=False):
    k = (key, size, dead)
    if k in _AV:
        return _AV[k]
    if key in _AVF:
        im = Image.open(_AVF[key]).convert("RGBA").resize((size * 3, size * 3), Image.LANCZOS)
        if dead:
            g = im.convert("L").convert("RGBA")
            im = Image.blend(g, Image.new("RGBA", g.size, (240, 236, 230, 255)), 0.35)
        mask = Image.new("L", im.size, 0)
        ImageDraw.Draw(mask).ellipse((0, 0, im.size[0] - 1, im.size[1] - 1), fill=255)
        out = Image.new("RGBA", im.size, (0, 0, 0, 0))
        out.paste(im, (0, 0), mask)
        _AV[k] = out.resize((size, size), Image.LANCZOS)
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


def heart(cv, x, y, s, filled=True, col="#FF5B4D", alpha=1.0, broken=False, glow=False):
    """画一颗心（中心 x, y，宽约 s）：实心 = 队伍色 + 白边；空心 = 灰底。broken = 中间裂开。"""
    ss = 4
    S = int(s * 1.6)
    lay = Image.new("RGBA", (S * ss, S * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)

    def shape(r, off=0.0):
        pts = []
        for i in range(60):
            t = 2 * math.pi * i / 60
            hx = 16 * math.sin(t) ** 3
            hy = -(13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t))
            pts.append((S * ss / 2 + hx * r + off, S * ss / 2 + hy * r))
        return pts
    r = s * ss / 34.0
    d.polygon(shape(r * 1.18), fill=(255, 255, 255, 255))
    if filled:
        c = rgb(col)
        if glow:
            c = tuple(min(255, int(v * 0.45 + 255 * 0.55)) for v in c)
        d.polygon(shape(r), fill=c + (255,))
        d.ellipse((S * ss * 0.3, S * ss * 0.3, S * ss * 0.42, S * ss * 0.42), fill=(255, 255, 255, 150))
    else:
        d.polygon(shape(r), fill=(206, 198, 190, 255))
    if broken:
        w = S * ss
        d.line([(w * 0.5, w * 0.25), (w * 0.44, w * 0.45), (w * 0.56, w * 0.58), (w * 0.5, w * 0.8)], fill=(255, 255, 255, 255),
               width=int(ss * 2.5))
    lay = lay.resize((S, S), Image.LANCZOS)
    if alpha < 0.999:
        lay.putalpha(lay.getchannel("A").point(lambda v: int(v * alpha)))
    paste_clip(cv, lay, x - S / 2, y - S / 2)


def crown_icon(cv, x, y, s, dim=False):
    """小王冠（中心 x、上沿 y，宽 s），国王头像上用。"""
    ss = 4
    w, h = int(s * ss), int(s * 0.75 * ss)
    lay = Image.new("RGBA", (w + 8 * ss, h + 8 * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    o = 4 * ss
    pts = [(o, o + h), (o, o + h * 0.3), (o + w * 0.25, o + h * 0.6), (o + w * 0.5, o), (o + w * 0.75, o + h * 0.6),
           (o + w, o + h * 0.3), (o + w, o + h)]
    gold = (180, 172, 164) if dim else GOLD
    d.polygon(pts, fill=(255, 255, 255, 255), outline=(255, 255, 255, 255), width=3 * ss)
    d.polygon(pts, fill=gold + (255,))
    d.rectangle((o, o + h * 0.78, o + w, o + h), fill=tuple(int(v * 0.82) for v in gold) + (255,))
    for cx_ in (0.0, 0.5, 1.0):
        rr = 2.2 * ss
        yy = o + (h * 0.3 if cx_ != 0.5 else 0)
        d.ellipse((o + w * cx_ - rr, yy - rr, o + w * cx_ + rr, yy + rr), fill=(255, 255, 255, 255))
    lay = lay.resize((lay.size[0] // ss, lay.size[1] // ss), Image.LANCZOS)
    paste_clip(cv, lay, x - lay.size[0] / 2, y - 4)


class Hud:
    def __init__(self, rp, roster=None):
        self.rp = rp
        self.roster = roster or cast.roster_for(rp)
        self.tl = TLM.Timeline(rp, self.roster)
        A = self.tl.anim
        self.team_mode = rp.mode in (2, 3, 4, 5)
        # 队伍色按队伍身份（cast.team_colors：Astra 队蓝、Fable 队红），换边局也不变；队名跟着颜色
        self.tcol = cast.team_colors(self.roster, rp.players)
        self.tname = (cast.team_names(self.roster, rp.players) if rp.mode >= 3
                      else tuple(cast.COLOR_NAME.get(c, d) for c, d in zip(self.tcol, TEAM_NAME)))
        # 环境变量 DART_TEAMNAMES="左队名,右队名" 覆盖队名（team 0, team 1）；不设时使用默认队名
        if os.environ.get("DART_TEAMNAMES", "").count(",") == 1:
            self.tname = tuple(os.environ["DART_TEAMNAMES"].split(","))
        # 播报：(时刻, 文字段落)
        self.feed = []
        for (t_, k, B) in sorted(((B.deaths[k][0], k, B) for B, k in A.all_deaths()), key=lambda e: (e[0], e[1])):
            t, cause, killer = B.deaths[k]
            Td, cause, d, wg = B.death_info(k)
            if k in getattr(rp, "kings", ()):
                segs = None
                if cause == 1 and killer >= 0:
                    segs = [("av", killer), ("name", killer), ("txt", "的飞镖终结了国王"), ("av", k), ("name", k)]
                elif cause == 2 and killer >= 0:
                    segs = [("av", killer), ("name", killer), ("txt", "一刀终结了国王"), ("av", k), ("name", k)]
                elif killer >= 0:
                    segs = [("av", killer), ("name", killer), ("txt", "把国王"), ("av", k), ("name", k), ("txt", "踢下了悬崖")]
                else:
                    segs = [("av", k), ("name", k), ("txt", "国王坠落")]
                self.feed.append((Td, segs))
                continue
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
        for (t, i, old, new) in getattr(rp, "captures", []):
            nm = rp.points[i][0] if i < len(rp.points) else "?"
            self.feed.append((t + 1.0, [("team", new), ("txt", "占领了 %s 点" % nm)]))
        for (t, k, left, by, why) in getattr(rp, "lifeloss", []):
            if left <= 0:                 # 最后一条命：上面"终结了国王"那条已经说了
                continue
            if by >= 0 and by != k and rp.players[by][1] == rp.players[k][1]:
                self.feed.append((t + 1.0, [("av", by), ("name", by), ("txt", "误伤自家国王"), ("av", k), ("name", k),
                                            ("txt", "还剩 %d 条命" % left)]))
            elif by >= 0 and by != k:
                self.feed.append((t + 1.0, [("av", by), ("name", by), ("txt", "击中国王"), ("av", k), ("name", k),
                                            ("txt", "还剩 %d 条命" % left)]))
            else:
                self.feed.append((t + 1.0, [("av", k), ("name", k), ("txt", "误伤，国王还剩 %d 条命" % left)]))
        self.feed.sort(key=lambda e: e[0])

    # ---- 某一刻的状态
    def state(self, gt):
        rp = self.rp
        t = int(clamp(gt, 0, rp.T))
        st = rp.states[t]
        alive, kills, cause = [], [], []
        A = self.tl.anim
        for k in range(rp.n):
            B = A.L(k, gt)
            dth = B.deaths.get(k)
            dead = dth is not None and gt >= dth[0] + 1.0
            alive.append(not dead)
            cause.append(dth[1] if dead else 0)
            if rp.mode >= 3:
                # 第 12 集：回放里的累计击杀（含误伤队友，和规则一致），在出局生效那一步加上
                kills.append(rp.states[int(clamp(gt - 1.0, 0, rp.T))]["chars"][k]["kills"] if gt >= 1 else 0)
            else:
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
            dth = tl.anim.L(k, gt).deaths.get(k)
            pops.append(round(clamp(1 - (gt - dth[0] - 1.0) / 8.0), 2) if (dth and gt >= dth[0] + 1.0) else 0.0)
        key.append(tuple(pops))
        mode_key, mode_paint = self._mode_parts(f, gt, alive, slide)
        key.append(mode_key)
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
                txt = "平局"
                if rp.mode == 4 and rp.ctl:
                    sc = rp.ctl[rp.T]["score"]
                    txt += "  %d : %d" % (sc[0] // 20, sc[1] // 20)
                banner = (txt, "#8A8078", a, None)
            elif self.team_mode:
                txt = self.tname[rp.winner] + " 获胜"
                if rp.mode == 4 and rp.ctl:
                    sc = rp.ctl[rp.T]["score"]
                    txt += "  %d : %d" % (sc[0] // 20, sc[1] // 20)
                banner = (txt, self.tcol[rp.winner], a, None)
            else:
                winner_key = self.roster[rp.winner]
                winner_name = LABEL[winner_key]
                title = "千问获胜" if winner_name == "千问" else winner_name + " 获胜"
                banner = (title, COLOR[winner_key], a, rp.winner)
        key.append(banner)

        def paint():
            cv = Image.new("RGBA", (W, H), (0, 0, 0, 0))
            oy = int(-130 * (1 - slide))
            mode_paint(cv, "under")
            if rp.mode >= 3:
                self._bar_5v5(cv, oy, alive, kills, cause, left, pops, gt)
            elif self.team_mode:
                self._bar_team(cv, oy, alive, kills, cause, left, pops)
            else:
                self._bar_ffa(cv, oy, alive, kills, cause, left, pops)
            mode_paint(cv, "over")
            y = 122 if rp.mode != 4 else 166
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
            tc = rgb(self.tcol[team])
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
            card(cv, x, box[1] + 14, cw - 8, box[3] - box[1] - 22, key, alive[k], st, self.tcol[team], av=54, name_size=22,
                 sub_size=16, tint=None, pop=pops[k])
        cx = box[0] + 10 + cw * per + mid_w // 2 - 4
        f1, f2, f3 = font(42, "Black"), font(15, "Medium"), font(17, "Bold")
        a, b = str(cnt[0]), str(cnt[1])
        d.text((cx - 18 - d.textlength(a, font=f1), box[1] + 3), a, font=f1, fill=rgb(self.tcol[0]))
        d.text((cx - 7, box[1] + 5), ":", font=font(36, "Bold"), fill=(170, 160, 150))
        d.text((cx + 18, box[1] + 3), b, font=f1, fill=rgb(self.tcol[1]))
        for team, sg in ((0, -1), (1, 1)):
            nm = self.tname[team]
            tw = d.textlength(nm, font=f3)
            px = cx - 62 - tw / 2 if sg < 0 else cx + 62 - tw / 2
            d.rounded_rectangle((px - 9, box[1] + 58, px + tw + 9, box[1] + 84), 12, fill=rgb(self.tcol[team]) + (255,))
            d.text((px, box[1] + 59), nm, font=f3, fill=(255, 255, 255))
        lab = "%d:%02d" % (left // 60, left % 60)
        d.text((cx - d.textlength(lab, font=f2) / 2, box[1] + 62), lab, font=f2, fill=SUB)

    # ================================================================ 第 12 集 5v5
    def _bar_5v5(self, cv, oy, alive, kills, cause, left, pops, gt):
        """10 人信息栏：左右各一整块队伍色分区（同第 11 集 4v4），中间按模式放：歼灭 = 存活比分，占点 = 积分和三个点，
        刺王 = 两位国王的命数。出局的头像变灰；占点里出局的人写复活倒计时；国王的头像上戴一顶小王冠。"""
        rp = self.rp
        box = (16, 10 + oy, W - 16, 110 + oy)
        panel(cv, box, 24)
        mid_w = 300
        per = rp.n // 2
        cw = (box[2] - box[0] - mid_w - 16) // rp.n
        sd = (lambda tm: 1 - tm) if getattr(rp, "blueleft", False) else (lambda tm: tm)   # 蓝左视角：Astra 队（team 1）排在左
        LT, RT = sd(0), sd(1)   # 左边、右边分别是哪个队伍号
        for team in (0, 1):
            tc = rgb(self.tcol[team])
            zx0 = box[0] + 6 + (cw * per + mid_w if sd(team) == 1 else 0)
            zx1 = zx0 + cw * per + 2
            zone = Image.new("RGBA", (zx1 - zx0, box[3] - box[1] - 12), (0, 0, 0, 0))
            zd = ImageDraw.Draw(zone)
            zd.rounded_rectangle((0, 0, zone.size[0] - 1, zone.size[1] - 1), 18, fill=tuple(int(255 * 0.8 + v * 0.2) for v in tc) + (255,))
            band = Image.new("RGBA", zone.size, (0, 0, 0, 0))
            ImageDraw.Draw(band).rectangle((0, 0, zone.size[0], 7), fill=tc + (255,))
            zone.alpha_composite(Image.composite(band, Image.new("RGBA", zone.size, (0, 0, 0, 0)), zone.getchannel("A")))
            paste_clip(cv, zone, zx0, box[1] + 6)
        cnt = [0, 0]
        idx = [0, 0]
        kings = getattr(rp, "kings", (-1, -1))
        A = self.tl.anim
        for k in range(rp.n):
            team = rp.players[k][1]
            if alive[k]:
                cnt[team] += 1
            i = idx[team]
            idx[team] += 1
            x = box[0] + 8 + cw * (i + (per if sd(team) == 1 else 0)) + (mid_w if sd(team) == 1 else 0)
            key = self.roster[k]
            if alive[k]:
                st = "击杀 %d" % kills[k]
            elif rp.mode == 4:
                B = A.L(k, gt)
                _, nxt = rp.life_span(k, B.life)
                st = ("复活 %d" % max(1, int(math.ceil((nxt - gt) / 20.0 - 1e-6)))) if nxt is not None else "出局"
            else:
                st = "出局 · " + CAUSE_SHORT.get(cause[k], "")
            card(cv, x, box[1] + 15, cw - 6, box[3] - box[1] - 24, key, alive[k], st, self.tcol[team], av=50, name_size=20,
                 sub_size=15, tint=None, pop=pops[k])
            if k in kings:
                ay = box[1] + 15 + (box[3] - box[1] - 24 - 50) // 2
                crown_icon(cv, x + 10 + 25, ay - 6, 30, dim=not alive[k])
        cx = box[0] + 8 + cw * per + mid_w // 2 - 2
        d = ImageDraw.Draw(cv)
        f3 = font(17, "Bold")
        # 两队队名牌
        for team, sg in ((LT, -1), (RT, 1)):
            nm = self.tname[team]
            tw = d.textlength(nm, font=f3)
            px = cx - 80 - tw / 2 if sg < 0 else cx + 80 - tw / 2
            d.rounded_rectangle((px - 9, box[1] + 62, px + tw + 9, box[1] + 88), 12, fill=rgb(self.tcol[team]) + (255,))
            d.text((px, box[1] + 63), nm, font=f3, fill=(255, 255, 255))
        f2 = font(15, "Medium")
        if rp.mode == 4:
            c = rp.ctl_at(gt) or dict(owner=[-1] * 3, taking=[-1] * 3, prog=[0] * 3, score=[0, 0])
            a, b = str(c["score"][LT] // 20), str(c["score"][RT] // 20)
            f1 = font(40, "Black")
            d.text((cx - 16 - d.textlength(a, font=f1), box[1] + 2), a, font=f1, fill=rgb(self.tcol[LT]))
            d.text((cx + 16, box[1] + 2), b, font=f1, fill=rgb(self.tcol[RT]))
            lab = "%d:%02d" % (left // 60, left % 60)
            d.text((cx - d.textlength(lab, font=f2) / 2, box[1] + 66), lab, font=f2, fill=SUB)
            d.text((cx - 6, box[1] + 4), ":", font=font(32, "Bold"), fill=(170, 160, 150))
        elif rp.mode == 5:
            for j, team in enumerate((0, 1)):
                kl = rp.king_lives(kings[team], gt) if kings[team] >= 0 else None
                n_ = kl[0] if (kl and alive[kings[team]]) else 0
                tot = rp.cfg.get("KING_LIVES", 3)
                hs = 22
                gap = 26
                wtot = gap * (tot - 1)
                x0 = (cx - 30 - wtot) if sd(team) == 0 else (cx + 30)
                for h_ in range(tot):
                    heart(cv, x0 + h_ * gap, box[1] + 30, hs, filled=h_ < n_, col=self.tcol[team])
            d.text((cx - d.textlength("VS", font=font(18, "Black")) / 2, box[1] + 19), "VS", font=font(18, "Black"), fill=(170, 160, 150))
        else:
            f1 = font(42, "Black")
            a, b = str(cnt[LT]), str(cnt[RT])
            d.text((cx - 18 - d.textlength(a, font=f1), box[1] + 3), a, font=f1, fill=rgb(self.tcol[LT]))
            d.text((cx - 7, box[1] + 5), ":", font=font(36, "Bold"), fill=(170, 160, 150))
            d.text((cx + 18, box[1] + 3), b, font=f1, fill=rgb(self.tcol[RT]))
        lab = MODE_NAME.get(rp.mode, "")
        if rp.mode != 4:
            d.text((cx - d.textlength(lab, font=f2) / 2, box[1] + 66), lab, font=f2, fill=SUB)

    def _mode_parts(self, f, gt, alive, slide):
        """模式专属、画在场上（跟着镜头）的部分：占点的点名牌和翻点进度、复活点的倒计时、国王头顶的命数。
        返回 (缓存键, paint(cv, 层))；层 = "under" 画在信息栏下面，"over" 画在上面。"""
        rp, tl = self.rp, self.tl
        A = tl.anim
        items = []          # (种类, 参数...)
        if rp.mode == 4 and rp.points:
            c = rp.ctl_at(gt) if gt >= 0 else None
            for i, (nm, px, py, r) in enumerate(rp.points):
                q = tl.screen_exact(f, (px, -(py - r - 0.15), 0.0))
                if q is None:
                    continue
                own = c["owner"][i] if c else -1
                tk = c["taking"][i] if c else -1
                pr = (c["prog"][i] / float(rp.cfg.get("CAPTURE", 20))) if c else 0.0
                # 翻点刚完成：名牌一跳
                pop = 0.0
                for (t, j, old, new) in rp.captures:
                    if j == i and 0 <= gt - (t + 1.0) < 10:
                        pop = 1 - (gt - t - 1.0) / 10.0
                items.append(("pt", nm, int(q[0]), int(q[1]), own, tk, round(pr, 2), round(pop, 2)))
        if rp.mode == 4 and gt >= 0:
            # 复活倒计时：出局动画演完以后，在这个人下一次复活的位置上挂一个头像 + 倒计时圈
            for k in range(rp.n):
                B = A.L(k, gt)
                dth = B.deaths.get(k)
                if dth is None or gt < dth[0] + 1.0:
                    continue
                _, nxt = rp.life_span(k, B.life)
                if nxt is None:
                    continue
                a = ease((gt - dth[0] - 12.0) / 6.0) * (1 - ease((gt - nxt + 1.0) / 2.0))
                if a < 0.02:
                    continue
                x, y = rp.char_pos(k, nxt)
                q = tl.screen_exact(f, (x, -y, 0.9))
                if q is None:
                    continue
                tot = float(rp.cfg.get("RESPAWN", 60))
                rem = clamp((nxt - gt) / tot)
                items.append(("rs", k, int(q[0]), int(q[1]), round(rem, 2), max(1, int(math.ceil((nxt - gt) / 20.0 - 1e-6))),
                              round(a, 2)))
        if rp.mode == 5 and gt >= -6:
            for team in (0, 1):
                k = rp.kings[team]
                if k < 0:
                    continue
                B = A.L(k, gt)
                dth = B.deaths.get(k)
                kl = rp.king_lives(k, gt)
                dead = dth is not None and gt >= dth[0] + 1.0
                if dead and gt > dth[0] + 14:
                    continue
                pz = A.char_pose(k, gt)
                if not pz.get("visible", True):
                    continue
                q = tl.screen_exact(f, (pz["x"], -pz["y"], pz.get("z", 0.0) + 1.62))
                if q is None:
                    continue
                n_ = 0 if dead else (kl[0] if kl else rp.cfg.get("KING_LIVES", 3))
                # 刚掉的那颗心：碎开、往上飘
                brk = 0.0
                for (t, kk, left, by, why) in rp.lifeloss:
                    if kk == k and 0 <= gt - (t + 1.0) < 14:
                        brk = 1 - (gt - t - 1.0) / 14.0
                if dead:
                    brk = max(brk, 1 - (gt - dth[0] - 1.0) / 14.0)
                inv = kl[1] if (kl and not dead) else 0
                a = clamp((gt + 6) / 6.0)
                items.append(("kh", team, int(q[0]), int(q[1]), n_, round(brk, 2), inv > 0 and int(math.floor(gt / 1.5)) % 2 == 0,
                              round(a, 2)))
        key = tuple(items)

        def paint(cv, layer):
            if layer == "under":
                for it in items:
                    if it[0] == "pt":
                        self._point_badge(cv, *it[1:])
                    elif it[0] == "rs":
                        self._respawn_badge(cv, *it[1:])
                    elif it[0] == "kh":
                        self._king_hearts(cv, *it[1:])
            elif layer == "over" and rp.mode == 4:
                c = rp.ctl_at(max(gt, 0)) or dict(owner=[-1] * 3, taking=[-1] * 3, prog=[0] * 3, score=[0, 0])
                oy = int(-130 * (1 - slide))
                for i in range(len(rp.points)):
                    self._point_chip(cv, W // 2 - 2 + (i - 1) * 44, 112 + oy + 20, rp.points[i][0], c, i, 17)
        return key, paint

    def _point_chip(self, cv, x, y, nm, c, i, r, solo=True):
        """信息栏下沿挂着的三个小圆片：A B C，底色 = 归属，外圈弧 = 翻点进度（翻点队伍的颜色）。"""
        d = ImageDraw.Draw(cv)
        own, tk = c["owner"][i], c["taking"][i]
        pr = c["prog"][i] / float(self.rp.cfg.get("CAPTURE", 20))
        fill = rgb(self.tcol[own]) if own >= 0 else NEUTRAL
        d.ellipse((x - r - 4, y - r - 4, x + r + 4, y + r + 4), fill=(255, 252, 246))
        d.ellipse((x - r, y - r, x + r, y + r), fill=fill)
        if tk >= 0 and pr > 0:
            d.arc((x - r - 4, y - r - 4, x + r + 4, y + r + 4), -90, -90 + 360 * pr, fill=rgb(self.tcol[tk]), width=5)
        f = font(int(r * 1.25), "Black")
        d.text((x - d.textlength(nm, font=f) / 2, y - r * 0.86), nm, font=f, fill=(255, 255, 255))

    def _point_badge(self, cv, nm, px, py, own, tk, pr, pop):
        """场上每个点的名牌（挂在圈的上沿）：字母 + 归属色；翻点时外面一圈进度。"""
        lay = Image.new("RGBA", (120, 120), (0, 0, 0, 0))
        d = ImageDraw.Draw(lay)
        r = 22 + 6 * pop
        c0 = 60
        fill = rgb(self.tcol[own]) if own >= 0 else NEUTRAL
        d.ellipse((c0 - r - 5, c0 - r - 5, c0 + r + 5, c0 + r + 5), fill=(255, 252, 246, 235))
        d.ellipse((c0 - r, c0 - r, c0 + r, c0 + r), fill=fill + (255,))
        if tk >= 0 and pr > 0:
            d.arc((c0 - r - 5, c0 - r - 5, c0 + r + 5, c0 + r + 5), -90, -90 + 360 * pr, fill=rgb(self.tcol[tk]) + (255,), width=7)
        f = font(int(r * 1.3), "Black")
        d.text((c0 - d.textlength(nm, font=f) / 2, c0 - r * 0.9), nm, font=f, fill=(255, 255, 255))
        sh = Image.new("RGBA", lay.size, (0, 0, 0, 0))
        ImageDraw.Draw(sh).ellipse((c0 - r - 4, c0 - r + 1, c0 + r + 4, c0 + r + 9), fill=(30, 40, 70, 70))
        sh = sh.filter(ImageFilter.GaussianBlur(4))
        paste_clip(cv, sh, px - c0, py - c0 - 30)
        paste_clip(cv, lay, px - c0, py - c0 - 30)

    def _respawn_badge(self, cv, k, px, py, rem, sec, a):
        """复活点上的倒计时：头像 + 一圈还剩多少的弧 + 秒数。"""
        key = self.roster[k]
        team = self.rp.players[k][1]
        tc = rgb(self.tcol[team])
        S = 130
        lay = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        d = ImageDraw.Draw(lay)
        c0, r = S // 2, 30
        d.ellipse((c0 - r - 8, c0 - r - 8, c0 + r + 8, c0 + r + 8), fill=(255, 252, 246, 230))
        d.arc((c0 - r - 6, c0 - r - 6, c0 + r + 6, c0 + r + 6), -90, -90 + 360 * rem, fill=tc + (255,), width=7)
        av = avatar(key, 2 * r, dead=True)
        lay.alpha_composite(av, (c0 - r, c0 - r))
        d = ImageDraw.Draw(lay)
        f = font(30, "Black")
        s_ = str(sec)
        tw = d.textlength(s_, font=f)
        d.ellipse((c0 + r - 14, c0 + r - 22, c0 + r + 16, c0 + r + 8), fill=tc + (255,))
        d.text((c0 + r + 1 - tw / 2, c0 + r - 26), s_, font=f, fill=(255, 255, 255))
        if a < 0.999:
            lay.putalpha(lay.getchannel("A").point(lambda v: int(v * a)))
        paste_clip(cv, lay, px - c0, py - c0 - 40)

    def _king_hearts(self, cv, team, px, py, n_, brk, blink, a):
        """国王头顶：一排心（剩几条命），刚掉的那颗碎开往上飘；无敌期间整排一闪一闪。"""
        tot = self.rp.cfg.get("KING_LIVES", 3)
        hs, gap = 26, 30
        S = (gap * tot + 40, 90)
        lay = Image.new("RGBA", S, (0, 0, 0, 0))
        x0 = S[0] / 2 - gap * (tot - 1) / 2
        for i in range(tot):
            if i < n_:
                heart(lay, x0 + i * gap, 56, hs, filled=True, col=self.tcol[team], glow=blink)
            elif i == n_ and brk > 0:
                u = 1 - brk
                heart(lay, x0 + i * gap, 56 - 34 * u, hs * (1 + 0.5 * u), filled=True, col=self.tcol[team], alpha=brk, broken=True)
            else:
                heart(lay, x0 + i * gap, 56, hs, filled=False, col=self.tcol[team])
        if a < 0.999:
            lay.putalpha(lay.getchannel("A").point(lambda v: int(v * a)))
        paste_clip(cv, lay, px - S[0] // 2, py - 70)

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
            if kind == "team":
                wd += d.textlength(self.tname[v], font=f) + 30
                continue
            wd += (av + 8) if kind == "av" else (d.textlength(LABEL[self.roster[v]] if kind == "name" else v,
                                                               font=f if kind == "name" else ft) + 10)
        x0 = W / 2 - wd / 2
        panel(lay, (x0 - 22, y, x0 + wd + 14, y + 50), 25, blur=8, alpha=45)
        d = ImageDraw.Draw(lay)
        x = x0
        for kind, v in segs:
            if kind == "av":
                key = self.roster[v]
                col = self.tcol[self.rp.players[v][1]] if self.team_mode else COLOR[key]
                d.ellipse((x - 3, y + 4, x + av + 2, y + 9 + av), fill=rgb(col))
                lay.alpha_composite(avatar(key, av), (int(x), y + 7))
                d = ImageDraw.Draw(lay)
                x += av + 8
            elif kind == "name":
                s = LABEL[self.roster[v]]
                d.text((x, y + 8), s, font=f, fill=INK)
                x += d.textlength(s, font=f) + 10
            elif kind == "team":
                s = self.tname[v]
                tw = d.textlength(s, font=f)
                d.rounded_rectangle((x - 2, y + 8, x + tw + 18, y + 42), 14, fill=rgb(self.tcol[v]))
                d.text((x + 8, y + 8), s, font=f, fill=(255, 255, 255))
                x += tw + 30
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
        col = self.tcol[self.rp.players[k][1]] if self.team_mode else COLOR[key]
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
