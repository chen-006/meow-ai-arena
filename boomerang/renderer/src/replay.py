# -*- coding: utf-8 -*-
"""读 DART-REPLAY 2 回放（格式见 boomerang/workspace/回放格式.md）。纯 Python，Blender 和普通 Python 都能用。

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
        self._parse()
        # 格子边长（米）：CFG 里有 CELL（毫米）就用；没有的话，格子数明显超过 24×15 的按 0.5 米（细格子图是 36×24、48×30）
        if "CELL" in self.cfg:
            self.cell = self.cfg["CELL"] / 1000.0
        else:
            self.cell = 0.5 if self.W * self.H > 600 else 1.0
        self.Wm = self.W * self.cell
        self.Hm = self.H * self.cell
        self.n = len(self.players)
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
                elif ty == "WARN":
                    self.warns.append((t, a[0]))
                elif ty == "COLLAPSE":
                    self.collapses.append((t, a[0]))
        self.ev = ev
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
