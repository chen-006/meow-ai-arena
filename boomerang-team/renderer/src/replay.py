# -*- coding: utf-8 -*-
"""读 DART-REPLAY 2 回放（格式见 A11_飞镖乱斗\\引擎\\回放格式.md）。纯 Python，Blender 和普通 Python 都能用。

渲染器对地图不做任何硬编码假设：宽、高、格子边长（CFG 里有 CELL 就用，没有按 1000 毫米）都从回放里读。
世界坐标一律用"米"：mx = x / 1000，my = y / 1000（y 向下）；Blender 里 X = mx，Y = -my。
"""
import hashlib
import math

CELL_GROUND, CELL_WALL, CELL_PIT, CELL_GRASS = 0, 1, 2, 3
ST_FREE, ST_THROW, ST_THROWN, ST_SLASH, ST_SLASHED, ST_KICK, ST_KICKED, ST_DASH, ST_STUN, ST_RECALL, ST_DEAD = range(11)
D_HAND, D_OUT, D_BACK, D_GROUND, D_RISE, D_GONE, D_CALL = range(7)
FLYING = (D_OUT, D_BACK, D_CALL)


class Replay:
    def __init__(self, path):
        self.path = path
        self.cfg = {}
        self.grid = []      # grid[y][x] 0 地面 1 墙 2 坑 3 草
        self.wave = []      # wave[y][x] -1 永不塌，否则波号
        self.wave_steps = []
        self.players = []   # (seat, team, name)
        self.states = {}    # t -> dict
        self.events = {}    # t -> [ (type, a,b,c,d) ]
        self.mode = 0
        self.mapname = ""
        self.W = self.H = 0
        self.end = 0
        self.winner = -1
        self.reason = ""
        self.theme = None
        self.points = []    # 占点（MODE 4）：[(名字, x 米, y 米, 半径 米)]
        self.kings = (-1, -1)   # 刺王（MODE 5）：左、右队国王座位号
        self.ctl = {}       # 占点：t -> dict(owner=[3], taking=[3], prog=[3], score=[2])（score 是积分单位，显示除以 20）
        self.kst = {}       # 刺王：t -> (左王命数, 左王无敌步数, 右王命数, 右王无敌步数)
        self._parse()
        # 格子边长（米）：CFG 里有 CELL（毫米）就用；没有的话，格子数明显超过 24×15 的按 0.5 米（细格子图是 36×24、48×30）
        if "CELL" in self.cfg:
            self.cell = self.cfg["CELL"] / 1000.0
        else:
            self.cell = 0.5 if self.W * self.H > 600 else 1.0
        self.Wm = self.W * self.cell
        self.Hm = self.H * self.cell
        self.n = len(self.players)
        self.blueleft = False
        if blueleft_wanted(self):
            _rotate180(self)
            self.blueleft = True
        self.T = max(self.states)
        self._index_events()

    # ------------------------------------------------------------ 解析
    def _parse(self):
        with open(self.path, encoding="utf-8") as f:
            lines = [ln.rstrip("\r\n") for ln in f]
        i = 0
        while i < len(lines):
            ln = lines[i]
            if ln.startswith("CFG "):
                for kv in ln[4:].split():
                    k, v = kv.split("=")
                    self.cfg[k] = int(v)
            elif ln.startswith("MODE "):
                self.mode = int(ln.split()[1])
            elif ln.startswith("MAPNAME "):
                self.mapname = ln.split()[1]
            elif ln.startswith("CELL "):
                self.cfg["CELL"] = int(float(ln.split()[1]))
            elif ln.startswith("THEME "):
                self.theme = ln.split()[1]
            elif ln.startswith("MAP "):
                p = ln.split()
                self.W = int(p[1])
                self.H = int(p[2]) if len(p) > 2 else int(p[1])
                if len(p) > 3 and "CELL" not in self.cfg:
                    self.cfg["CELL"] = int(p[3])
                for j in range(self.H):
                    self.grid.append([int(c) for c in lines[i + 1 + j]])
                i += self.H
            elif ln == "WAVE":
                for j in range(self.H):
                    self.wave.append([-1 if c == "." else int(c, 36) for c in lines[i + 1 + j]])
                i += self.H
            elif ln.startswith("WAVES "):
                p = [int(v) for v in ln.split()[1:]]
                self.wave_steps = p[1:1 + p[0]]
            elif ln.startswith("POINTS "):
                p = ln.split()
                for j in range(int(p[1])):
                    nm, x, y, r = p[2 + 4 * j], int(p[3 + 4 * j]), int(p[4 + 4 * j]), int(p[5 + 4 * j])
                    self.points.append((nm, x / 1000.0, y / 1000.0, r / 1000.0))
            elif ln.startswith("KING "):
                p = ln.split()
                self.kings = (int(p[1]), int(p[2]))
            elif ln.startswith("PLAYER "):
                p = ln.split()
                self.players.append((int(p[1]), int(p[2]), p[3]))
            elif ln.startswith("S "):
                self._parse_state(ln)
            elif ln.startswith("E "):
                p = ln.split()
                self.events.setdefault(int(p[1]), []).append((p[2],) + tuple(int(v) for v in p[3:]))
            elif ln.startswith("END "):
                p = ln.split()
                self.end = int(p[1])
                self.winner = int(p[2])
                self.reason = p[4] if len(p) > 4 else ""
            i += 1
        if not self.wave:
            self.wave = [[-1] * self.W for _ in range(self.H)]

    def _parse_state(self, ln):
        groups = [g.split() for g in ln.split("|")]
        t = int(groups[0][1])
        n = len(self.players)
        chars = []
        for k in range(n):
            v = [int(x) for x in groups[1 + k]]
            chars.append(dict(alive=v[0], x=v[1], y=v[2], vx=v[3], vy=v[4], face=v[5], st=v[6],
                              timer=v[7], ax=v[8], ay=v[9], cd=v[10], expose=v[11], kills=v[12],
                              msg=v[13], vis=v[14], dead_t=v[15], cause=v[16], killer=v[17]))
        darts = []
        for k in range(n):
            v = [int(x) for x in groups[1 + n + k]]
            darts.append(dict(st=v[0], x=v[1], y=v[2], vx=v[3], vy=v[4], left=v[5], age=v[6]))
        tail = [int(x) for x in groups[1 + 2 * n]]
        self.states[t] = dict(t=t, chars=chars, darts=darts, collapsed=tail[0], warn=tail[1])
        if len(groups) > 2 + 2 * n:          # 第 12 集 5v5：占点 11 个数 / 刺王 4 个数
            ex = [int(x) for x in groups[2 + 2 * n]]
            if self.mode == 4 and len(ex) >= 11:
                self.ctl[t] = dict(owner=ex[0:9:3], taking=ex[1:9:3], prog=ex[2:9:3], score=ex[9:11])
            elif self.mode == 5 and len(ex) >= 4:
                self.kst[t] = tuple(ex[:4])

    # ------------------------------------------------------------ 事件索引（按座位）
    def _index_events(self):
        n = self.n
        ev = [dict(throw=[], release=[], slash=[], kick=[], swing=[], dash=[], stop=[], recall=[], cancel=[],
                   call=[], catch=[], pick=[], drop=[], clash=[], turn=[], kicked=[], lost=[]) for _ in range(n)]
        self.hits = []      # (t, 凶手, 受害者, 方式 1 镖 2 砍, 镖状态)
        self.deaths = {}    # seat -> (t, cause, killer)
        self.kickhits = []  # (t, 踢的人, 被踢的人)
        self.clashes = []   # (t, x, y, a, b) 每对一条
        self.blocks = []    # (t, x, y, 挥砍者, 镖的主人)：挥砍把飞镖打落（11-16 规则默认开）
        self.collapses = [] # (t, wave)
        self.warns = []
        self.death_list = []    # 每一次出局（占点有复活，同一个人可以出局好几次）：(t, 座位, 方式, 凶手)
        self.respawns = [[] for _ in range(n)]   # 每人每次复活的事件步（S t+1 起在场）
        self.captures = []      # (t, 点序号, 原归属, 新归属)
        self.lifeloss = []      # 国王掉命：(t, 国王座位, 剩余命数, 命中者, 原因 1 镖 2 刀)
        seen_clash = set()
        for t in sorted(self.events):
            for e in self.events[t]:
                ty, a = e[0], e[1:] + (0, 0, 0, 0)
                if ty == "THROW":
                    ev[a[0]]["throw"].append((t, a[1] / 1000.0, a[2] / 1000.0))
                elif ty == "RELEASE":
                    ev[a[0]]["release"].append((t, a[1] / 1000.0, a[2] / 1000.0))
                elif ty == "SLASH":
                    ev[a[0]]["slash"].append((t, a[1] / 1000.0, a[2] / 1000.0))
                elif ty == "KICK":
                    ev[a[0]]["kick"].append((t, a[1] / 1000.0, a[2] / 1000.0))
                elif ty == "SWING":
                    ev[a[0]]["swing"].append((t, a[1], a[2]))
                elif ty == "DASH":
                    ev[a[0]]["dash"].append((t, a[1] / 1000.0, a[2] / 1000.0))
                elif ty == "STOP":
                    ev[a[0]]["stop"].append((t,))
                elif ty == "RECALL":
                    ev[a[0]]["recall"].append((t, a[1] / 1000.0, a[2] / 1000.0))
                elif ty == "CANCEL":
                    ev[a[0]]["cancel"].append((t,))
                elif ty == "CALL":
                    ev[a[0]]["call"].append((t, a[1] / 1000.0, a[2] / 1000.0))
                elif ty == "CATCH":
                    ev[a[0]]["catch"].append((t,))
                elif ty == "PICK":
                    ev[a[0]]["pick"].append((t,))
                elif ty == "TURN":
                    ev[a[0]]["turn"].append((t, a[1] / 1000.0, a[2] / 1000.0))
                elif ty == "LOST":
                    ev[a[0]]["lost"].append((t, a[1] / 1000.0, a[2] / 1000.0))
                elif ty in ("DROP", "BLOCK"):
                    o = a[0] if ty == "DROP" else a[1]
                    x, y = (a[1], a[2]) if ty == "DROP" else (a[2], a[3])
                    ev[o]["drop"].append((t, x / 1000.0, y / 1000.0))
                    if ty == "BLOCK":
                        self.blocks.append((t, x / 1000.0, y / 1000.0, a[0], a[1]))
                elif ty == "CLASH":
                    ev[a[0]]["clash"].append((t, a[2] / 1000.0, a[3] / 1000.0, a[1]))
                    key = (t, min(a[0], a[1]), max(a[0], a[1]))
                    if key not in seen_clash:
                        seen_clash.add(key)
                        self.clashes.append([t, a[2] / 1000.0, a[3] / 1000.0, a[0], a[1]])
                    else:
                        for c in self.clashes:
                            if (c[0], min(c[3], c[4]), max(c[3], c[4])) == key:
                                c[1] = (c[1] + a[2] / 1000.0) / 2
                                c[2] = (c[2] + a[3] / 1000.0) / 2
                elif ty == "HIT":
                    self.hits.append((t, a[0], a[1], 1, a[2]))
                elif ty == "CUT":
                    self.hits.append((t, a[0], a[1], 2, 0))
                elif ty == "KICKHIT":
                    self.kickhits.append((t, a[0], a[1]))
                    ev[a[1]]["kicked"].append((t, a[0]))
                elif ty == "DEATH":
                    self.deaths[a[0]] = (t, a[1], a[2])
                    self.death_list.append((t, a[0], a[1], a[2]))
                elif ty == "RESPAWN":
                    self.respawns[a[0]].append(t)
                elif ty == "CAPTURE":
                    self.captures.append((t, a[0], a[1], a[2]))
                elif ty == "LIFE":
                    self.lifeloss.append((t, a[0], a[1], a[2], a[3]))
                elif ty == "WARN":
                    self.warns.append((t, a[0]))
                elif ty == "COLLAPSE":
                    self.collapses.append((t, a[0]))
        self.ev = ev
        # 每人每条命的出局：deaths_by_seat[k][i] = 第 i 条命（0 起）的 (t, 方式, 凶手)；最后一条命可能没有出局
        self.deaths_by_seat = [[(t, c, kl) for (t, kk, c, kl) in self.death_list if kk == k] for k in range(n)]
        self.max_lives = max([len(r) + 1 for r in self.respawns] + [1])
        # 每人累计走过的路程（米），给跑步循环用
        self.dist = [[0.0] * (self.T + 1) for _ in range(n)]
        for k in range(n):
            acc = 0.0
            for t in range(1, self.T + 1):
                a, b = self.states[t - 1]["chars"][k], self.states[t]["chars"][k]
                if b["st"] in (ST_FREE, ST_THROW, ST_THROWN) and a["alive"] and b["alive"]:
                    acc += math.hypot(b["x"] - a["x"], b["y"] - a["y"]) / 1000.0
                self.dist[k][t] = acc

    # ------------------------------------------------------------ 地图
    def collapsed_at(self, t):
        """第 t 步开始时已经塌了几波。"""
        return sum(1 for s in self.wave_steps if t >= s)

    def solid_mask(self, collapsed):
        """已塌 collapsed 波之后哪些格子是实地（非坑）。"""
        return [[1 if (self.grid[y][x] != CELL_PIT and not (0 <= self.wave[y][x] < collapsed)) else 0
                 for x in range(self.W)] for y in range(self.H)]

    def cell_at(self, mx, my):
        cx, cy = int(math.floor(mx / self.cell)), int(math.floor(my / self.cell))
        if 0 <= cx < self.W and 0 <= cy < self.H:
            return self.grid[cy][cx], self.wave[cy][cx]
        return CELL_PIT, -1

    def map_key(self):
        """地图（地形 + 塌陷图）的指纹，地形缓存用。"""
        s = "%d,%d,%g|" % (self.W, self.H, self.cell) + "|".join("".join(map(str, r)) for r in self.grid)
        s += "|" + "|".join(",".join(map(str, r)) for r in self.wave)
        th = ("_" + self.theme) if (self.theme and self.theme != "meadow") else ""
        return self.mapname + "_" + hashlib.md5(s.encode()).hexdigest()[:8] + th

    # ------------------------------------------------------------ 第 12 集：复活（每条命）、模式状态
    def life_index(self, k, gt):
        """座位 k 在游戏时间 gt 处在第几条命（0 起）：复活事件在第 r 步，S r+1 起是新的一条命。"""
        return sum(1 for r in self.respawns[k] if gt >= r + 1.0)

    def life_span(self, k, i):
        """第 i 条命从哪一刻开始（gt）、到下一条命开始为止（没有下一条 = None）。"""
        r = self.respawns[k]
        a = -1e9 if i == 0 else r[i - 1] + 1.0
        b = r[i] + 1.0 if i < len(r) else None
        return a, b

    def deaths_of_life(self, i):
        """{座位: 第 i 条命的出局 (t, 方式, 凶手)}。"""
        return {k: self.deaths_by_seat[k][i] for k in range(self.n) if i < len(self.deaths_by_seat[k])}

    def team_of(self, k):
        return self.players[k][1]

    def ctl_at(self, gt):
        if not self.ctl:
            return None
        return self.ctl[min(max(int(gt), 0), self.T)] if self.ctl else None

    def kst_at(self, gt):
        if not self.kst:
            return None
        return self.kst[min(max(int(gt), 0), self.T)]

    def king_lives(self, k, gt):
        """国王座位 k 在 gt 时的剩余命数和无敌步数；不是国王返回 None。"""
        if k not in self.kings or not self.kst:
            return None
        s = self.kst_at(gt)
        j = 0 if k == self.kings[0] else 1
        return s[2 * j], s[2 * j + 1]

    def spawns(self):
        return [(c["x"] / 1000.0, c["y"] / 1000.0) for c in self.states[0]["chars"]]

    # ------------------------------------------------------------ 插值
    def S(self, t):
        return self.states[min(max(int(t), 0), self.T)]

    def char_pos(self, k, gt):
        """座位 k 在游戏时间 gt（步，可带小数，可超出两端）的位置（米）。"""
        if gt <= 0:
            c = self.states[0]["chars"][k]
            return c["x"] / 1000.0, c["y"] / 1000.0
        if gt >= self.T:
            c = self.states[self.T]["chars"][k]
            return c["x"] / 1000.0, c["y"] / 1000.0
        i = int(gt)
        a = gt - i
        c0, c1 = self.states[i]["chars"][k], self.states[i + 1]["chars"][k]
        return ((c0["x"] + (c1["x"] - c0["x"]) * a) / 1000.0, (c0["y"] + (c1["y"] - c0["y"]) * a) / 1000.0)

    def dart_pos(self, k, gt):
        if gt <= 0:
            d = self.states[0]["darts"][k]
            return d["x"] / 1000.0, d["y"] / 1000.0
        if gt >= self.T:
            d = self.states[self.T]["darts"][k]
            return d["x"] / 1000.0, d["y"] / 1000.0
        i = int(gt)
        a = gt - i
        d0, d1 = self.states[i]["darts"][k], self.states[i + 1]["darts"][k]
        return ((d0["x"] + (d1["x"] - d0["x"]) * a) / 1000.0, (d0["y"] + (d1["y"] - d0["y"]) * a) / 1000.0)

    def dist_at(self, k, gt):
        if gt <= 0:
            return 0.0
        if gt >= self.T:
            return self.dist[k][self.T]
        i = int(gt)
        a = gt - i
        return self.dist[k][i] + (self.dist[k][i + 1] - self.dist[k][i]) * a


def load(path):
    return Replay(path)


# ------------------------------------------------------------ 蓝左视角
# 环境变量 DART_BLUELEFT=1 时：换边局（team 0 是 Fable 队、Astra 队在右出生）读进来后，把全部坐标绕地图中心转 180 度
# （三张图的 MAP / WAVE 绕中心转 180 度完全一致，所以地图不变），速度、朝向、动作方向一起转。座位号、队伍号、时间、事件步都不变，
# 画面上 = 同一局从对面看过去：Astra 队（蓝）在左、Fable 队（红）在右，灯光布景和常规局一样。信息栏按 rp.blueleft 把 Astra 队排在左。
# 不设环境变量时行为和原来完全一样。DART_BLUELEFT=0 / 不设 = 关。
_ASTRA_NAMES = {"astra", "sol", "hydrogen", "gemini_s", "grok", "kimi"}
_FABLE_NAMES = {"fable", "opus", "carbon", "gemini", "glm", "deepseek"}


def blueleft_wanted(rp):
    import os
    if os.environ.get("DART_BLUELEFT", "0") not in ("1", "true", "yes", "on"):
        return False
    # Astra 本人（名字以 astra 开头，含实验里的 astra2..）在 team 1、team 0 里没有 Astra 本人 = 换边局（含赛后实验的换边局）
    t0 = [nm.lower() for (s, tm, nm) in rp.players if tm == 0]
    t1 = [nm.lower() for (s, tm, nm) in rp.players if tm == 1]
    return any(n.startswith("astra") for n in t1) and not any(n.startswith("astra") for n in t0)


def _rotate180(rp):
    Wmm = int(round(rp.Wm * 1000))
    Hmm = int(round(rp.Hm * 1000))
    rp.grid = [list(reversed(r)) for r in reversed(rp.grid)]
    rp.wave = [list(reversed(r)) for r in reversed(rp.wave)]
    rp.points = [(nm, rp.Wm - x, rp.Hm - y, r) for (nm, x, y, r) in rp.points]
    for t, S in rp.states.items():
        for c in S["chars"]:
            c["x"], c["y"] = Wmm - c["x"], Hmm - c["y"]
            c["vx"], c["vy"] = -c["vx"], -c["vy"]
            c["ax"], c["ay"] = -c["ax"], -c["ay"]
            c["face"] = (c["face"] + 18000) % 36000
        for d in S["darts"]:
            d["x"], d["y"] = Wmm - d["x"], Hmm - d["y"]
            d["vx"], d["vy"] = -d["vx"], -d["vy"]
    DIR_BC = ("THROW", "SLASH", "KICK", "DASH")
    POS_BC = ("RELEASE", "TURN", "DROP", "RECALL", "CALL", "LOST", "STOP", "RESPAWN")
    POS_CD = ("CLASH", "BLOCK", "FALL")
    for t, L in rp.events.items():
        out = []
        for e in L:
            ty, v = e[0], list(e[1:])
            if ty in DIR_BC and len(v) >= 3:
                v[1], v[2] = -v[1], -v[2]
            elif ty in POS_BC and len(v) >= 3:
                v[1], v[2] = Wmm - v[1], Hmm - v[2]
            elif ty in POS_CD and len(v) >= 4:
                v[2], v[3] = Wmm - v[2], Hmm - v[3]
            out.append((ty,) + tuple(v))
        rp.events[t] = out
