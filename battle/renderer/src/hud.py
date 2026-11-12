# -*- coding: utf-8 -*-
"""信息栏（纯 Python + Pillow，不依赖 Blender）：每帧一张透明 PNG，出片时用 ffmpeg 叠在画面上。

全部放在画面最上面一条（下面留给战场和字幕），字够大，手机上看得清：
  左上    关卡名（对抗图写地图名 + 1v1 / 四方混战），下面一行小字是主题；
  上中    比分牌：左右各一方（队伍色底条、名字、剩余人数的大数字、造价 剩余 / 开场、一条造价余量），正中是计时；
          四方混战时四块并排，计时在右上；
  比分牌下  有军旗、主帅、镇山兽的关卡：它们的血条（名字 + 长条 + 数字；刚掉的血先留一截浅色再缩）。
  左上牌子下面  短暂出一行小字（replay.captions）：守军出击（"坡顶守军出击 · 16 人"）、山口集结的敌军出发；
          从左边滑进来，停 3 秒左右淡掉，同时有两条就往下排。
结束时（回放最后一帧、含片尾补帧之后，定格 HOLD_S 秒）出结果牌：整幅画面压暗，一块队伍色 / 结果色的斜牌从左滑进来，
上面是"过关 / 没过 / 某方获胜"，下面两行小字：怎么结束的、用时和剩余兵力。
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H = 1920, 1080
TEAM_HEX = ["#2F6BFF", "#F0302C", "#18B45A", "#A64BF0"]     # 和 army.TEAM_HEX 一致
FONT_FILES = [os.environ.get("A12_FONT", ""), r"C:\Windows\Fonts\NotoSansSC-VF.ttf", r"C:\Windows\Fonts\msyh.ttc",
              "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"]
PANEL = (14, 16, 24, 196)
TXT = (255, 255, 255, 255)
SUB = (206, 212, 224, 255)
HOLD_S = 3.0
CAP_S, CAP_IN, CAP_OUT = 3.6, 0.25, 0.45             # 小字提示：一共几秒、滑进来几秒、淡出几秒
WIN_COL, LOSE_COL = "#F2B01E", "#E5483A"
_FONTS = {}


def font(size, bold=False):
    key = (size, bold)
    if key in _FONTS:
        return _FONTS[key]
    for p in FONT_FILES:
        if p and os.path.exists(p):
            f = ImageFont.truetype(p, size)
            try:
                f.set_variation_by_name("Bold" if bold else "Medium")
            except Exception:
                pass
            _FONTS[key] = f
            return f
    _FONTS[key] = ImageFont.load_default()
    return _FONTS[key]


def hexrgb(h, a=255):
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


def mmss(s):
    s = max(0, int(s))
    return "%d:%02d" % (s // 60, s % 60)


# 各关"全歼"过关时结果牌上怎么说（引擎的结束原因都是 win:eliminate，过关条件各关不一样，见 引擎\关卡 的 desc）
WIN_ELIM = {"草甸迎击": "全歼来敌", "断龙桥": "全歼来敌，军旗未倒", "拔炮台": "攻下炮台", "一线天": "全歼峡谷守军"}


def reason_text(rp, reason):
    """结束原因 -> 一句话（认不出的返回空串）。"""
    r = str(reason or "")
    mp = str(rp.meta.get("map_name", ""))
    if r.startswith("win:destroy:"):
        tag = r.split(":", 2)[2]
        if tag == "镇山兽":
            return "镇山兽倒下"
        return "拔掉%s" % tag if "炮" in tag else "击破%s" % tag
    if r.startswith("lose:destroy:"):
        tag = r.split(":", 2)[2]
        return "%s被砍倒" % tag if "旗" in tag else "%s被毁" % tag
    if r.startswith("lose:dead:"):
        return "%s阵亡" % r.split(":", 2)[2]
    if r == "win:eliminate":
        for k, v in WIN_ELIM.items():
            if k in mp:
                return v
        return "全歼守军"
    return {"eliminated": "全军覆没", "timeout_lose": "时间到", "annihilation": "全歼对手", "timeout": "时间到"}.get(r, "")


class Hud:
    def __init__(self, rp, names=None):
        self.rp = rp
        m = rp.meta
        self.mode = m["mode"]
        mn = m["map_name"]
        if self.mode == "level":
            self.title = mn.replace("_", " · ").replace("关", " 关", 1).replace("第", "第 ", 1)
        else:
            self.title = "%s · %s" % (mn, {"duel": "1v1", "melee": "四方混战"}.get(self.mode, "试场"))
        self.theme = m.get("theme", "")
        # 左上角的牌子：四方混战时比分牌要占满一行，牌子收窄成"地图名 / 四方混战 · 主题"两行
        self.panel = (self.title, self.theme)
        if self.mode == "melee":
            self.panel = (mn, "四方混战 · %s" % self.theme if self.theme else "四方混战")
        # 涨潮：每帧的岸线半径（和引擎同一套：水面高度落在哪个圆台上，按高度插值半径）
        self.tide = None
        sc = rp.scenario
        td = (sc.get("water") or {}).get("tide")
        if td:
            mounds = [p for p in sc.get("terrain", []) if p.get("type") == "mound"]

            def shore(level):
                best = None
                for p in mounds:
                    y0, y1 = p.get("y0", 0.0), p.get("y1", 1.0)
                    if y0 <= level <= y1 + 1e-6:
                        r0, r1 = p.get("r0", p.get("r", 1.0)), p.get("r1", p.get("r", 1.0))
                        best = r0 + (r1 - r0) * (level - y0) / max(y1 - y0, 1e-6)
                return best
            r_full = shore(td["h0"])
            if r_full:
                self.tide = (td, shore, r_full)
        self.limit = m.get("time_limit_steps", 0) * rp.dt
        self.fps = 1.0 / (rp.dt * rp.rec_every)
        self.last = rp.n - 1                         # 数据到这一帧为止
        self.nx = getattr(rp, "nx", rp.n)            # 画面到这一帧为止（含片尾补帧），之后是定格
        cost = {k: v.get("cost", 0) for k, v in m["params"]["units"].items()}
        teams = sorted(set(int(o["team"]) for o in m["objects"] if o["type"] == "unit" and o["team"] >= 0))
        self.teams = teams
        seat_name = {s["team"]: s.get("name", "") for s in m["seats"]}
        self.names = {}
        for t in teams:
            if names and t < len(names) and names[t]:
                self.names[t] = names[t]
            elif self.mode == "level":
                self.names[t] = "闯关方" if t == 0 else "守军"
            else:
                self.names[t] = seat_name.get(t, "") or "第 %d 方" % (t + 1)
        # 每帧每队：活着的兵数、造价
        ucols = [k for k, i in enumerate(rp.ids) if rp.info[i]["type"] == "unit"]
        kinds = [rp.info[rp.ids[k]].get("kind") for k in ucols]
        tm = np.array([rp.info[rp.ids[k]].get("team", -1) for k in ucols])
        cst = np.array([cost.get(kd, 0) for kd in kinds], float)
        counted = np.array([kd not in ("boss", "lord") for kd in kinds])
        alive = rp.present[:, ucols] & (rp.S[:, ucols] < 3)
        self.cnt, self.cost = {}, {}
        for t in teams:
            m_t = (tm == t) & counted
            self.cnt[t] = alive[:, m_t].sum(axis=1)
            self.cost[t] = (alive[:, m_t] * cst[m_t][None, :]).sum(axis=1)
        self.cnt0 = {t: int(((tm == t) & counted).sum()) for t in teams}
        self.cost0 = {t: float(cst[(tm == t) & counted].sum()) for t in teams}
        # 血条：军旗、主帅、镇山兽
        self.bars = []
        for o in m["objects"]:
            kd = o.get("kind")
            if kd in ("flag", "lord", "boss") and o["id"] in rp.col:
                k = rp.col[o["id"]]
                nm = {"flag": "军旗", "lord": "主帅", "boss": "镇山兽"}[kd]
                tcol = TEAM_HEX[o["team"] % 4] if o.get("team", -1) >= 0 else "#DDDDDD"
                self.bars.append((nm, k, float(max(o.get("hp", 1), 1)), tcol))
        # 小字提示（守军出击、山口集结的敌军出发）
        import replay as RP
        self.caps = RP.captions(rp)
        # 结果
        self.result = None
        for e in rp.events:
            if e["t"] == "end":
                w = e.get("winner", -1)
                why = reason_text(rp, e.get("reason"))
                if self.mode == "level":
                    self.result = ("过关" if w == 0 else "没过", WIN_COL if w == 0 else LOSE_COL, why)
                elif w is None or w < 0:
                    self.result = ("平局", "#8C93A6", why)
                else:
                    self.result = ("%s 获胜" % self.names.get(w, ""), TEAM_HEX[w % 4], why)

    # ------------------------------------------------------------ 小零件
    def _hp(self, j, k):
        rp = self.rp
        return float(rp.H[j, k]) if rp.present[j, k] and (rp.OT[k] != 1 or rp.S[j, k] < 3) else 0.0

    def _team_block(self, d, x, y, w, tm, j, align):
        """一方的比分块。align = 'r'：大数字靠右（放在计时左边）；'l'：大数字靠左。"""
        col = hexrgb(TEAM_HEX[tm % 4])
        h = 104
        d.rounded_rectangle((x, y, x + w, y + h), 18, fill=PANEL)
        strip = (x, y, x + 14, y + h) if align == "r" else (x + w - 14, y, x + w, y + h)
        d.rounded_rectangle(strip, 7, fill=col)
        c, c0 = int(self.cnt[tm][j]), self.cnt0[tm]
        cs, cs0 = int(round(self.cost[tm][j])), int(round(self.cost0[tm]))
        fn, fbig, fsm = font(32, True), font(66, True), font(26)
        big = "%d" % c
        suf = "/%d" % c0
        fsuf = font(30, True)
        bw, sw = d.textlength(big, font=fbig), d.textlength(suf, font=fsuf)
        dead = c <= 0
        cost_s = "造价 %d / %d" % (cs, cs0)
        room = w - 32 - 28 - bw - 6 - sw - 16                 # 名字、造价能占多宽（放不下自动缩字号、再截断）
        name, fn = self._fit(d, self.names[tm], 32, room)
        if d.textlength(cost_s, font=fsm) > room:
            cost_s = "造价 %d/%d" % (cs, cs0)
        cost_s, fsm = self._fit(d, cost_s, 26, room, bold=False, min_size=18)
        if align == "r":
            bx = x + w - 28 - sw - 6 - bw
            d.text((x + 32, y + 8), name, font=fn, fill=TXT)
            d.text((x + 32, y + 52), cost_s, font=fsm, fill=SUB)
        else:
            bx = x + 28
            d.text((x + w - 32 - d.textlength(name, font=fn), y + 8), name, font=fn, fill=TXT)
            d.text((x + w - 32 - d.textlength(cost_s, font=fsm), y + 52), cost_s, font=fsm, fill=SUB)
        d.text((bx, y - 2), big, font=fbig, fill=SUB if dead else TXT)
        d.text((bx + bw + 6, y + 40), suf, font=fsuf, fill=SUB)
        # 造价余量条
        frac = cs / cs0 if cs0 > 0 else 0.0
        x0, x1 = x + 30, x + w - 30
        d.rounded_rectangle((x0, y + h - 14, x1, y + h - 7), 3, fill=(255, 255, 255, 46))
        if frac > 0:
            if align == "r":
                d.rounded_rectangle((x1 - (x1 - x0) * frac, y + h - 14, x1, y + h - 7), 3, fill=col)
            else:
                d.rounded_rectangle((x0, y + h - 14, x0 + (x1 - x0) * frac, y + h - 7), 3, fill=col)

    def _fit(self, d, txt, size, maxw, bold=True, min_size=20):
        """字号从 size 往下减到放得下；最小号还放不下就截断加省略号。"""
        f = font(size, bold)
        while d.textlength(txt, font=f) > maxw and size > min_size:
            size -= 2
            f = font(size, bold)
        if d.textlength(txt, font=f) > maxw:
            while len(txt) > 1 and d.textlength(txt + "…", font=f) > maxw:
                txt = txt[:-1]
            txt += "…"
        return txt, f

    def _team_block_small(self, d, x, y, w, tm, j):
        """四方混战的一块比分（窄一点）：左边队伍色竖条 + 大数字，右边名字和造价，底下造价余量条。"""
        col = hexrgb(TEAM_HEX[tm % 4])
        h = 104
        d.rounded_rectangle((x, y, x + w, y + h), 18, fill=PANEL)
        d.rounded_rectangle((x, y, x + 14, y + h), 7, fill=col)
        c, c0 = int(self.cnt[tm][j]), self.cnt0[tm]
        cs, cs0 = int(round(self.cost[tm][j])), int(round(self.cost0[tm]))
        fbig, fsuf, fsm = font(60, True), font(26, True), font(22)
        big, suf = "%d" % c, "/%d" % c0
        bx = x + 30
        bw_ = d.textlength(big, font=fbig)
        d.text((bx, y + 2), big, font=fbig, fill=SUB if c <= 0 else TXT)
        d.text((bx + bw_ + 4, y + 42), suf, font=fsuf, fill=SUB)
        left = bx + bw_ + 4 + d.textlength(suf, font=fsuf) + 14
        right = x + w - 22
        name, fn = self._fit(d, self.names[tm], 30, right - left)
        d.text((right - d.textlength(name, font=fn), y + 10), name, font=fn, fill=TXT)
        cost_s = "造价 %d/%d" % (cs, cs0)
        d.text((right - d.textlength(cost_s, font=fsm), y + 56), cost_s, font=fsm, fill=SUB)
        frac = cs / cs0 if cs0 > 0 else 0.0
        x0, x1 = x + 28, x + w - 22
        d.rounded_rectangle((x0, y + h - 14, x1, y + h - 7), 3, fill=(255, 255, 255, 46))
        if frac > 0:
            d.rounded_rectangle((x0, y + h - 14, x0 + (x1 - x0) * frac, y + h - 7), 3, fill=col)

    def _tide_bar(self, d, y, j):
        """涨潮图：比分牌下面一条"沙洲"：沙色 = 还露在水面上的半径，蓝色 = 被潮水吃掉的；右边写半径和涨潮的状态。"""
        rp = self.rp
        td, shore, r_full = self.tide
        t = rp.steps[j] * rp.dt
        a = min(max((t - td["t0"]) / max(td["t1"] - td["t0"], 1e-6), 0.0), 1.0)
        r = shore(td["h0"] + (td["h1"] - td["h0"]) * a) or r_full
        frac = max(0.0, min(1.0, r / r_full))
        fl, fnn, fs = font(30, True), font(26, True), font(22)
        nm = "沙洲"
        num = "半径 %d 米" % int(round(r))
        if t < td["t0"]:
            num2 = "%d 秒后涨潮" % int(math.ceil(td["t0"] - t))
        else:
            num2 = "涨潮中" if a < 1.0 else "潮水已满"
        lw = d.textlength(nm, font=fl)
        nw = d.textlength("半径 35 米", font=fnn)
        n2w = d.textlength("20 秒后涨潮", font=fs)
        bar_w = 460
        tot = lw + 18 + bar_w + 16 + nw + 14 + n2w
        x0 = W / 2 - tot / 2
        d.rounded_rectangle((x0 - 22, y, x0 + tot + 22, y + 50), 16, fill=PANEL)
        d.text((x0, y + 4), nm, font=fl, fill=TXT)
        bx0, bx1 = x0 + lw + 18, x0 + lw + 18 + bar_w
        d.rounded_rectangle((bx0, y + 15, bx1, y + 35), 10, fill=hexrgb("#2F8FCF", 235))
        d.rounded_rectangle((bx0, y + 15, bx0 + max(20, bar_w * frac), y + 35), 10, fill=hexrgb("#E8C77E"))
        d.text((bx1 + 16, y + 8), num, font=fnn, fill=TXT)
        d.text((bx1 + 16 + nw + 14, y + 12), num2, font=fs, fill=SUB)
        return y + 58

    def _timer_w(self, d, j):
        rp = self.rp
        fa, fb = font(54, True), font(24)
        s2 = "限时 " + mmss(self.limit) if self.limit else ""
        return max(d.textlength("8:88", font=fa), d.textlength(s2, font=fb),
                   d.textlength("0:00", font=fa)) + 44

    def _timer(self, d, cx, y, j):
        rp = self.rp
        s1 = mmss(rp.steps[j] * rp.dt)
        fa, fb = font(54, True), font(24)
        w1 = d.textlength(s1, font=fa)
        s2 = "限时 " + mmss(self.limit) if self.limit else ""
        w2 = d.textlength(s2, font=fb)
        pw = max(w1, w2) + 44
        d.rounded_rectangle((cx - pw / 2, y, cx + pw / 2, y + 104), 18, fill=PANEL)
        d.text((cx - w1 / 2, y + 2), s1, font=fa, fill=TXT)
        d.text((cx - w2 / 2, y + 66), s2, font=fb, fill=SUB)
        return pw

    # ------------------------------------------------------------ 一帧
    def frame(self, i):
        """i 可以超过最后一帧（片尾补帧、定格段）。"""
        rp = self.rp
        j = min(i, self.last)
        im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        top = 26
        # 左上：关卡名
        f1, f2 = font(40, True), font(26)
        pt, ps = self.panel
        bw = int(max(d.textlength(pt, font=f1), d.textlength(ps, font=f2)) + 56)
        d.rounded_rectangle((28, top, 28 + bw, top + 104), 18, fill=PANEL)
        d.text((56, top + 8), pt, font=f1, fill=TXT)
        d.text((56, top + 62), ps, font=f2, fill=SUB)
        # 上中：比分牌
        teams = self.teams
        if len(teams) == 2:
            tw = self._timer(d, W / 2, top, j)
            bwid = 372
            self._team_block(d, W / 2 - tw / 2 - 12 - bwid, top, bwid, teams[0], j, "r")
            self._team_block(d, W / 2 + tw / 2 + 12, top, bwid, teams[1], j, "l")
        else:
            # 四方混战：[关卡牌][甲][乙][丙][丁][计时]，比分块平分中间这一段
            gap = 12
            n = len(teams)
            tw = self._timer_w(d, j)
            x0, x1 = 28 + bw + gap, W - 28 - tw - gap
            bwid = (x1 - x0 - gap * (n - 1)) / n
            x = x0
            for tm in teams:
                self._team_block_small(d, x, top, bwid, tm, j)
                x += bwid + gap
            self._timer(d, W - 28 - tw / 2, top, j)
        # 血条
        y = top + 104 + 12
        bar_x0 = None                                  # 血条牌子的左边（小字提示要躲开它）
        back = int(0.6 * self.fps)
        for (nm, k, hp0, tcol) in self.bars:
            hp = self._hp(j, k)
            hp_old = max(hp, self._hp(min(max(0, i - back), self.last), k))
            frac, frac_old = max(0.0, min(1.0, hp / hp0)), max(0.0, min(1.0, hp_old / hp0))
            fl, fnn = font(30, True), font(26, True)
            lw = d.textlength(nm, font=fl)
            num = "%d" % int(round(hp))
            nw = d.textlength("%d" % int(hp0), font=fnn)
            bar_w = 560
            tot = lw + 18 + bar_w + 16 + nw
            x0 = W / 2 - tot / 2
            d.rounded_rectangle((x0 - 22, y, x0 + tot + 22, y + 50), 16, fill=PANEL)
            bar_x0 = x0 - 22 if bar_x0 is None else min(bar_x0, x0 - 22)
            d.text((x0, y + 4), nm, font=fl, fill=TXT)
            bx0, bx1 = x0 + lw + 18, x0 + lw + 18 + bar_w
            d.rounded_rectangle((bx0, y + 15, bx1, y + 35), 10, fill=(255, 255, 255, 50))
            if frac_old > frac:
                d.rounded_rectangle((bx0, y + 15, bx0 + max(20, bar_w * frac_old), y + 35), 10, fill=(255, 240, 200, 235))
            if frac > 0:
                d.rounded_rectangle((bx0, y + 15, bx0 + max(20, bar_w * frac), y + 35), 10, fill=hexrgb(tcol))
            d.text((bx1 + 16 + nw - d.textlength(num, font=fnn), y + 8), num, font=fnn, fill=TXT)
            y += 58
        # 涨潮：沙洲还剩多大（沙色是岛、蓝色是被潮水吃掉的部分）
        if self.tide:
            bar_x0 = W / 2 - 400 if bar_x0 is None else bar_x0
            y = self._tide_bar(d, y, j)
        # 左上牌子下面：小字提示（和血条撞上时挪到血条下面）
        cy = top + 104 + 12
        for (age, main, sub_, tm) in self.active_caps(i):
            if bar_x0 is not None and cy < y and 28 + self._cap_w(d, main, sub_) + 12 > bar_x0:
                cy = y
            cy = self._caption(im, d, 28, cy, age, main, sub_, tm)
        # 结束：结果牌
        if i > self.nx - 1 and self.result:
            im = Image.alpha_composite(im, self._result_card((i - (self.nx - 1)) / self.fps))
        return im

    def active_caps(self, i):
        """第 i 帧正在显示的小字提示：[(出现了几秒, 主句, 补充, 队伍号)]。"""
        out = []
        for (f, main, sub_, tm) in self.caps:
            age = (i - f) / self.fps
            if 0 <= age < CAP_S and i <= self.nx - 1:
                out.append((age, main, sub_, tm))
        return out

    def _cap_w(self, d, main, sub_):
        wm = d.textlength(main, font=font(30, True))
        return 26 + 40 + wm + ((16 + d.textlength(sub_, font=font(24))) if sub_ else 0) + 24

    def _caption(self, im, d, x, y, age, main, sub_, tm):
        """一行小字提示：深色圆角牌 + 左边队伍色竖条和两个往前的箭头 + 主句（粗）+ 补充（浅）。返回下一行的 y。"""
        a_in = min(1.0, age / CAP_IN)
        a_out = min(1.0, max(0.0, (CAP_S - age) / CAP_OUT))
        e = 1 - (1 - a_in) ** 3
        al = min(e, a_out)
        if al <= 0.01:
            return y + 60
        fm, fs = font(30, True), font(24)
        col = TEAM_HEX[tm % 4]
        wm = d.textlength(main, font=fm)
        ws = d.textlength(sub_, font=fs) if sub_ else 0
        h = 54
        w = 26 + 40 + wm + (16 + ws if sub_ else 0) + 24
        layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        dl = ImageDraw.Draw(layer)
        x0 = x - (1 - e) * 60
        dl.rounded_rectangle((x0, y, x0 + w, y + h), 16, fill=PANEL)
        dl.rounded_rectangle((x0, y, x0 + 12, y + h), 6, fill=hexrgb(col))
        # 两个往前冲的箭头（队伍色）
        cx, cyy = x0 + 30, y + h / 2
        for k in range(2):
            ax = cx + k * 12
            dl.polygon([(ax, cyy - 10), (ax + 10, cyy), (ax, cyy + 10), (ax + 5, cyy)], fill=hexrgb(col))
        tx = x0 + 26 + 40
        dl.text((tx, y + 6), main, font=fm, fill=TXT)
        if sub_:
            dl.text((tx + wm + 16, y + 14), sub_, font=fs, fill=SUB)
        if al < 1.0:
            a = layer.getchannel("A").point(lambda v: int(v * al))
            layer.putalpha(a)
        im.alpha_composite(layer)
        return y + h + 8

    def _result_card(self, age):
        rp = self.rp
        txt, col, why = self.result
        a = min(1.0, age / 0.3)
        e = 1 - (1 - a) ** 3                                   # 缓出
        card = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(card)
        d.rectangle((0, 0, W, H), fill=(8, 10, 16, int(96 * e)))
        cy = int(H * 0.47)
        d.rectangle((0, cy - 150, W, cy + 170), fill=(10, 12, 18, int(176 * e)))
        d.rectangle((0, cy - 150, W, cy - 144), fill=hexrgb(col, int(255 * e)))
        d.rectangle((0, cy + 164, W, cy + 170), fill=hexrgb(col, int(255 * e)))
        fbig = font(132, True)
        tw = d.textlength(txt, font=fbig)
        pw, ph, sk = tw + 190, 176, 34
        px = (W - pw) / 2 - (1 - e) * 520                      # 从左滑进来
        py = cy - 128
        d.polygon([(px + sk, py), (px + pw + sk, py), (px + pw - sk, py + ph), (px - sk, py + ph)], fill=hexrgb(col, int(255 * e)))
        tx = px + (pw - tw) / 2
        d.text((tx + 4, py + 2 + 4), txt, font=fbig, fill=(0, 0, 0, int(90 * e)))
        d.text((tx, py + 2), txt, font=fbig, fill=(255, 255, 255, int(255 * e)))
        # 两行小字
        a2 = min(1.0, max(0.0, (age - 0.25) / 0.3))
        fs1, fs2 = font(40, True), font(32)
        line1 = "%s%s" % (self.title, ("  ·  " + why) if why else "")
        d.text(((W - d.textlength(line1, font=fs1)) / 2, cy + 62), line1, font=fs1, fill=(255, 255, 255, int(255 * a2)))
        j = self.last
        parts = ["用时 %s" % mmss(rp.steps[j] * rp.dt)]
        for tm in self.teams:
            parts.append("%s 剩 %d / %d 人" % (self.names[tm], int(self.cnt[tm][j]), self.cnt0[tm]))
        line2 = "    ".join(parts)
        d.text(((W - d.textlength(line2, font=fs2)) / 2, cy + 116), line2, font=fs2, fill=(214, 220, 232, int(255 * a2)))
        return card


def hold_frames(rp):
    """结尾定格多少帧。"""
    return int(HOLD_S / (rp.dt * rp.rec_every))


def render_range(rp, out_dir, a, b, names=None, hold=True):
    """写 out_dir/%05d.png，帧号 a..b-1；hold=True 且 b 到了结尾（含片尾补帧），再多写 HOLD_S 秒的定格帧。返回最后一帧号 + 1。"""
    os.makedirs(out_dir, exist_ok=True)
    h = Hud(rp, names)
    end = b
    if hold and b >= h.nx:
        end = h.nx + hold_frames(rp)
    prev_key, prev_path = None, None
    import shutil
    back = int(0.6 * h.fps)
    for i in range(a, end):
        p = os.path.join(out_dir, "%05d.png" % i)
        j = min(i, rp.n - 1)
        key = (int(rp.steps[j] * rp.dt), tuple(int(h.cnt[t][j]) for t in h.teams),
               tuple(int(h.cost[t][j]) for t in h.teams), tuple(int(h._hp(j, k)) for (_, k, _, _) in h.bars),
               tuple(int(h._hp(min(max(0, i - back), h.last), k)) for (_, k, _, _) in h.bars),
               i if (i > h.nx - 1 and (i - h.nx) < 0.7 * h.fps) else (-2 if i > h.nx - 1 else -1),
               int(rp.steps[j] * rp.dt * 4) if h.tide else 0,
               i if h.active_caps(i) else -1)
        if key == prev_key and prev_path:
            shutil.copyfile(prev_path, p)
        else:
            h.frame(i).save(p, compress_level=1)
        prev_key, prev_path = key, p
    return end


if __name__ == "__main__":
    import sys
    import replay as RP
    rp = RP.load(sys.argv[1])
    h = Hud(rp)
    for i in (0, rp.n // 2, rp.nx + 40):
        h.frame(i).save(os.path.join(sys.argv[2], "hud_%05d.png" % i))
