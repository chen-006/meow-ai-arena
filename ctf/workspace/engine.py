"""多阵营夺旗 v1.0 规则引擎。只用标准库，结算完全确定。"""
from __future__ import annotations

import copy
import math
import random
import secrets
from collections import Counter

VERSION = "flag-1.0"
MOVES = {"N": (0, -1), "S": (0, 1), "W": (-1, 0), "E": (1, 0), "WAIT": (0, 0)}
RULES = {"turns": 400, "hp": 100, "damage": 34, "attack_range": 2,
         "respawn": 10, "flag_return": 15, "flag_cooldown": 8}
TEXT_MOVES = {"U": "N", "D": "S", "L": "W", "R": "E", "S": "WAIT"}


def distance(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1])


def map_size(n):
    return 23 if n == 2 else 31 if n <= 5 else 41 if n <= 10 else 49


def base_centers(n, c, r):
    """基地中心放在以地图中心为圆心、曼哈顿半径 r 的菱形上，避开四个顶点
    （这样每个基地到中心的距离分布完全相同），并让相邻基地的间距尽量相等。"""
    allowed = []
    for side in range(4):
        for s in range(2, r - 1):
            allowed.append(((c - r + s, c - s), (c + s, c - r + s), (c + r - s, c + s), (c - s, c + r - s))[side])
    m = len(allowed)

    def gaps(idx):
        pts = [allowed[i] for i in idx]
        return [distance(pts[i], pts[(i + 1) % n]) for i in range(n)]

    def cost(idx):
        g = gaps(idx)
        return (max(g) - min(g), sum((x - sum(g) / n) ** 2 for x in g))

    best = None
    for off in range(m):
        idx = [(off + round(i * m / n)) % m for i in range(n)]
        while True:
            improved = False
            for i in range(n):
                for d in (-1, 1):
                    trial = idx[:]
                    trial[i] = (trial[i] + d) % m
                    if len(set(trial)) == n and cost(trial) < cost(idx):
                        idx, improved = trial, True
            if not improved:
                break
        if best is None or cost(idx) < cost(best):
            best = idx
    return [allowed[i] for i in best]


def spots_between(bases, center):
    """每个基地前方的旗点：离基地中心正好 D 步（约 60% 路程）、离地图中心正好 r-D 步，
    横纵都严格落在两者之间，所以每个基地的出生点到自家旗点距离完全相同。"""
    cx, cy = center
    out = []
    for x, y in bases:
        ax, ay = abs(cx - x), abs(cy - y)
        r = ax + ay
        d = round(0.6 * r)
        dx = min(max(round(d * ax / r), 1), d - 1, ax - 1)
        dy = d - dx
        sx, sy = (1 if cx > x else -1), (1 if cy > y else -1)
        out.append((x + sx * dx, y + sy * dy))
    return out


class Game:
    TIME_LIMITS = (2.0, 0.05)   # 首回合 2 秒，之后每回合 50 毫秒

    def __init__(self, n, seed=1, flag_seed=None):
        if not 2 <= n <= 15:
            raise ValueError("支持 2–15 个阵营")
        self.n, self.seed = n, seed
        self.rng = random.Random(seed)   # 只用来生成地图
        # 旗子出现在哪个旗点用另一个随机数决定，不传时每局现取，不能从地图反推
        self.flag_seed = secrets.randbits(64) if flag_seed is None else flag_seed
        self.flag_rng = random.Random(self.flag_seed)
        self.size = map_size(n)
        c = self.size // 2
        self.center = (c, c)
        self.base_center = base_centers(n, c, c - 3)
        self.bases = [[(x + dx, y + dy) for dy in (-1, 0, 1) for dx in (-1, 0, 1)] for x, y in self.base_center]
        # 出生、重生优先使用离中心近的格子；每个基地的顺序在对称意义下相同
        self.spawn_order = [sorted(b, key=lambda p, bc=bc: (distance(p, self.center),
                                                            abs(p[0] - bc[0]) + 2 * abs(p[1] - bc[1])))
                            for b, bc in zip(self.bases, self.base_center)]
        self.walls = self.make_walls()
        self.turn, self.done = 0, False
        self.score = [0] * n
        # 旗点：每个基地正前方各一个，外加地图正中心一个。旗子每次出现在随机一个
        # 空旗点上，一局刷新几十次，任何座位都得不到固定的地利。
        self.flag_spots = [tuple(p) for p in spots_between(self.base_center, self.center)] + [self.center]
        self.flags = [{"id": i, "pos": None, "status": "home", "holder": None, "return_at": None}
                      for i in range(max(2, math.ceil(n / 2)))]
        for f in self.flags:
            f["pos"] = list(self.free_spot())
        self.units = []
        for team in range(n):
            for p in self.spawn_order[team][:3]:
                self.units.append({"id": len(self.units), "team": team, "pos": list(p), "hp": RULES["hp"],
                                   "flag": None, "respawn_at": None, "immune": False})
        self.stats = [Counter() for _ in range(n)]
        self.events = []
        self._sent_init = set()
        self.check()

    def free_spot(self):
        taken = {tuple(f["pos"]) for f in self.flags if f["pos"] is not None and f["status"] == "home"}
        return self.flag_rng.choice([p for p in self.flag_spots if p not in taken] or self.flag_spots)

    def make_walls(self):
        size = self.size
        walls = {(x, y) for x in range(size) for y in range(size) if x in (0, size - 1) or y in (0, size - 1)}
        keep = {self.center, *self.base_center, *spots_between(self.base_center, self.center)}
        keep = {(x + dx, y + dy) for x, y in keep for dx in range(-2, 3) for dy in range(-2, 3)}
        cells = [(x, y) for x in range(3, size - 3) for y in range(3, size - 3) if (x, y) not in keep]
        self.rng.shuffle(cells)
        pillars = []
        for p in cells:
            if self.rng.random() < 0.14 and all(distance(p, q) > 2 for q in pillars):
                pillars.append(p)
        return walls | set(pillars)

    # ---------- 选手看到的信息 ----------

    def observation(self, team):
        return {"turn": self.turn, "team": team, "teams": self.n, "size": self.size,
                "map": self.map_rows(), "center": list(self.center),
                "bases": [[list(p) for p in b] for b in self.bases],
                "flag_spots": [list(p) for p in self.flag_spots],
                "units": [{k: u[k] for k in ("id", "team", "pos", "hp", "flag", "respawn_at")}
                          for u in self.units],
                "flags": copy.deepcopy(self.flags), "score": self.score[:],
                "events": copy.deepcopy(self.events), "rules": dict(RULES)}

    # ---------- 文本协议 ----------

    def encode(self, team):
        """第 team 方本回合的输入。第一次调用时先附上 INIT 块。"""
        out = []
        if team not in self._sent_init:
            self._sent_init.add(team)
            r = RULES
            out.append(f"INIT {self.n} {team} {self.size} {r['turns']} {r['hp']} {r['damage']} "
                       f"{r['respawn']} {r['flag_return']} {r['flag_cooldown']}")
            out.append("MAP")
            out.extend(self.map_rows())
            out.append(f"BASES {self.n}")
            out.extend(f"{x} {y}" for x, y in self.base_center)
            out.append(f"SPOTS {len(self.flag_spots)}")
            out.extend(f"{x} {y}" for x, y in self.flag_spots)
        out.append(f"TURN {self.turn + 1}")
        out.append("SCORE " + " ".join(map(str, self.score)))
        out.append(f"UNITS {len(self.units)}")
        for u in self.units:
            x, y = u["pos"] if u["pos"] else (-1, -1)
            out.append(f"U {u['id']} {u['team']} {x} {y} {u['hp']} "
                       f"{-1 if u['flag'] is None else u['flag']} {-1 if u['respawn_at'] is None else u['respawn_at']}")
        out.append(f"FLAGS {len(self.flags)}")
        for f in self.flags:
            x, y = f["pos"] if f["pos"] else (-1, -1)
            out.append(f"F {f['id']} {f['status']} {x} {y} {-1 if f['holder'] is None else f['holder']} "
                       f"{-1 if f['return_at'] is None else f['return_at']}")
        out.append(f"EVENTS {len(self.events)}")
        for e in self.events:
            t = e["type"]
            if t == "death":
                by = e["by"]
                out.append(f"E death {e['unit']} {e['pos'][0]} {e['pos'][1]} "
                           f"{-1 if e['flag'] is None else e['flag']} {len(by)} " + " ".join(map(str, by)))
            elif t == "respawn":
                out.append(f"E respawn {e['unit']} {e['pos'][0]} {e['pos'][1]}")
            elif t == "attack":
                out.append(f"E attack {e['unit']} {e['target']}")
            elif t == "capture":
                out.append(f"E capture {e['unit']} {e['team']} {e['flag']}")
            elif t in ("pickup", "drop"):
                out.append(f"E {t} {e['unit']} {e['flag']}")
            else:
                out.append(f"E return {e['flag']}")
        out.append("END")
        return "\n".join(out) + "\n"

    def decode(self, team, line):
        """解析一行回复：回合号，然后己方 3 个角色各一个移动、一个动作，可选 # 注释。"""
        body, _, note = line.partition("#")
        tok = body.split()
        if not tok or tok[0] != str(self.turn + 1):
            raise ValueError(f"行首应为本回合编号 {self.turn + 1}")
        if len(tok) != 7:
            raise ValueError("应为：回合号 + 3 个角色各一个移动和一个动作")
        notes = [s.strip()[:20] for s in note.split(";")] if note.strip() else []
        units = []
        for k in range(3):
            mv, act = tok[1 + 2 * k], tok[2 + 2 * k]
            if mv not in TEXT_MOVES:
                raise ValueError(f"移动只能是 U D L R S，收到 {mv}")
            cmd = {"id": 3 * team + k, "move": TEXT_MOVES[mv]}
            if act == "-":
                cmd["act"] = "wait"
            elif act == "P":
                cmd["act"] = "pickup"
            elif act == "X":
                cmd["act"] = "drop"
            elif act.isdigit():
                cmd.update(act="attack", target=int(act))
            else:
                raise ValueError(f"动作只能是 - P X 或目标编号，收到 {act}")
            if k < len(notes) and notes[k]:
                cmd["note"] = notes[k]
            units.append(cmd)
        return {"units": units}

    def map_rows(self):
        if not hasattr(self, "_rows"):
            self._rows = ["".join("#" if (x, y) in self.walls else "." for x in range(self.size))
                          for y in range(self.size)]
        return self._rows

    def header(self):
        return {"version": VERSION, "seed": self.seed, "flag_seed": self.flag_seed, "teams": self.n, "size": self.size,
                "map": self.map_rows(), "center": list(self.center),
                "bases": [[list(p) for p in b] for b in self.bases],
                "flag_spots": [list(p) for p in self.flag_spots], "rules": dict(RULES)}

    # ---------- 结算 ----------

    def can_attack(self, a, b):
        if distance(a, b) > RULES["attack_range"]:
            return False
        dx, dy = b[0] - a[0], b[1] - a[1]
        if abs(dx) == 2 and dy == 0:
            return (a[0] + dx // 2, a[1]) not in self.walls
        if abs(dy) == 2 and dx == 0:
            return (a[0], a[1] + dy // 2) not in self.walls
        if abs(dx) == abs(dy) == 1:
            return (a[0] + dx, a[1]) not in self.walls or (a[0], a[1] + dy) not in self.walls
        return True

    def step(self, orders):
        if self.done:
            raise RuntimeError("比赛已结束")
        self.turn += 1
        events = []
        commands, shown = {}, {}
        for team in range(self.n):
            rows = orders.get(team, {}).get("units") if isinstance(orders.get(team), dict) else None
            for row in rows if isinstance(rows, list) else []:
                if not isinstance(row, dict):
                    continue
                uid = row.get("id")
                if type(uid) is int and 0 <= uid < len(self.units) and self.units[uid]["team"] == team:
                    commands[uid] = row
            shown[team] = []

        # 1. 旗帜回位，阵亡角色重生
        for f in self.flags:
            if f["status"] in ("dropped", "cooldown") and self.turn >= f["return_at"]:
                f.update(pos=list(self.free_spot()), status="home", holder=None, return_at=None)
                events.append({"type": "return", "flag": f["id"]})
        occupied = {tuple(u["pos"]) for u in self.units if u["pos"] is not None}
        for u in self.units:
            u["immune"] = False
            if u["pos"] is None and self.turn >= u["respawn_at"]:
                for p in self.spawn_order[u["team"]]:
                    if p not in occupied:
                        u.update(pos=list(p), hp=RULES["hp"], respawn_at=None, immune=True)
                        occupied.add(p)
                        events.append({"type": "respawn", "unit": u["id"], "pos": list(p)})
                        break

        # 2. 同时移动
        live = [u for u in self.units if u["pos"] is not None]
        start = {u["id"]: tuple(u["pos"]) for u in live}
        want = {}
        for u in live:
            move = commands.get(u["id"], {}).get("move", "WAIT")
            dx, dy = MOVES.get(move, (0, 0)) if isinstance(move, str) else (0, 0)
            p = (start[u["id"]][0] + dx, start[u["id"]][1] + dy)
            want[u["id"]] = start[u["id"]] if p in self.walls else p
        count = Counter(want.values())
        blocked = {i for i, p in want.items() if count[p] > 1}
        owner = {p: i for i, p in start.items()}
        for i, p in want.items():
            j = owner.get(p)
            if j is not None and j != i and want[j] == start[i] and self.units[i]["team"] != self.units[j]["team"]:
                blocked.update((i, j))
        while True:
            more = {i for i, p in want.items() if i not in blocked and p != start[i] and p in owner
                    and (owner[p] in blocked or want[owner[p]] == start[owner[p]])}
            if not more:
                break
            blocked |= more
        for u in live:
            u["pos"] = list(start[u["id"]] if u["id"] in blocked else want[u["id"]])

        # 3. 同时攻击，阵亡掉旗
        hits = {}
        for u in live:
            cmd = commands.get(u["id"], {})
            t = cmd.get("target")
            if cmd.get("act") != "attack" or u["flag"] is not None or u["immune"]:
                continue
            if type(t) is not int or not 0 <= t < len(self.units):
                continue
            v = self.units[t]
            if v["pos"] is not None and v["team"] != u["team"] and not v["immune"] and self.can_attack(u["pos"], v["pos"]):
                hits.setdefault(t, []).append(u["id"])
                events.append({"type": "attack", "unit": u["id"], "target": t})
        for t, attackers in hits.items():
            self.units[t]["hp"] -= RULES["damage"] * len(attackers)
        for u in live:
            if u["hp"] > 0:
                continue
            attackers = hits[u["id"]]
            for a in attackers:
                self.stats[self.units[a]["team"]]["kills"] += 1 / len(attackers)
                if u["flag"] is not None:
                    self.stats[self.units[a]["team"]]["carrier_kills"] += 1 / len(attackers)
            self.stats[u["team"]]["deaths"] += 1
            events.append({"type": "death", "unit": u["id"], "pos": u["pos"][:], "by": attackers, "flag": u["flag"]})
            if u["flag"] is not None:
                self.flags[u["flag"]].update(pos=u["pos"][:], status="dropped", holder=None,
                                             return_at=self.turn + RULES["flag_return"])
            u.update(pos=None, hp=0, flag=None, respawn_at=self.turn + RULES["respawn"])

        # 4. 拾旗（同一格有多面旗时拿编号最小的）
        for u in self.units:
            if u["pos"] is None or u["flag"] is not None or commands.get(u["id"], {}).get("act") != "pickup":
                continue
            for f in self.flags:
                if f["status"] in ("home", "dropped") and f["pos"] == u["pos"]:
                    u["flag"] = f["id"]
                    f.update(pos=None, status="carried", holder=u["id"], return_at=None)
                    self.stats[u["team"]]["pickups"] += 1
                    events.append({"type": "pickup", "unit": u["id"], "flag": f["id"]})
                    break

        # 5. 主动丢旗
        for u in self.units:
            if u["pos"] is not None and u["flag"] is not None and commands.get(u["id"], {}).get("act") == "drop":
                f = self.flags[u["flag"]]
                f.update(pos=u["pos"][:], status="dropped", holder=None, return_at=self.turn + RULES["flag_return"])
                events.append({"type": "drop", "unit": u["id"], "flag": f["id"]})
                u["flag"] = None

        # 6. 交旗
        for u in self.units:
            if u["pos"] is not None and u["flag"] is not None and tuple(u["pos"]) in self.bases[u["team"]]:
                f = self.flags[u["flag"]]
                f.update(status="cooldown", pos=None, holder=None, return_at=self.turn + RULES["flag_cooldown"])
                self.score[u["team"]] += 1
                self.stats[u["team"]]["captures"] += 1
                events.append({"type": "capture", "unit": u["id"], "team": u["team"], "flag": f["id"]})
                u["flag"] = None

        self.events = events
        self.done = self.turn >= RULES["turns"]
        self.check()
        for uid, cmd in commands.items():
            shown[self.units[uid]["team"]].append({k: cmd[k] for k in ("id", "move", "act", "target", "note")
                                                   if k in cmd and len(str(cmd[k])) <= 40})
        return self.frame(shown)

    def frame(self, orders=None):
        return {"turn": self.turn, "score": self.score[:], "events": copy.deepcopy(self.events),
                "units": [{k: u[k] for k in ("id", "pos", "hp", "flag")} for u in self.units],
                "flags": copy.deepcopy(self.flags), "orders": orders or {}}

    def check(self):
        at = [tuple(u["pos"]) for u in self.units if u["pos"] is not None]
        assert len(at) == len(set(at)) and not set(at) & self.walls
        for f in self.flags:
            holders = [u for u in self.units if u["flag"] == f["id"]]
            assert (f["status"] == "carried") == bool(holders) and len(holders) <= 1
        assert all(self.score[t] == self.stats[t]["captures"] for t in range(self.n))

    def result(self):
        return {"score": self.score[:], "turns": self.turn,
                "stats": [{k: round(v, 2) for k, v in s.items()} for s in self.stats]}
