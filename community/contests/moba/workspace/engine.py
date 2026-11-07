"""峡谷对决（MOBA）v1.0 规则引擎。只用标准库，结算完全确定。

模仿英雄联盟的 5v5：三路兵线、每路两座防御塔、野区营地、水晶。一个程序控制一队 5 个英雄
（上单战士、打野刺客、中单法师、射手、辅助），摧毁对方水晶获胜。
n=1 时对方队伍由引擎内置 AI 控制（单人挑战），n=2 时两队都由选手程序控制。
难度（0 简单 / 1 普通 / 2 困难）决定地图大小、小兵波次、野怪与防御塔的参数，
以及单人挑战时对方英雄的数量和强度。
"""
from __future__ import annotations

import random
from collections import Counter, deque

VERSION = "moba-1.3"
DIFF_LABELS = ("简单", "普通", "困难")
MOVES = {"N": (0, -1), "S": (0, 1), "W": (-1, 0), "E": (1, 0), "WAIT": (0, 0)}
TEXT_MOVES = {"U": "N", "D": "S", "L": "W", "R": "E", "S": "WAIT"}
DIR_ORDER = ("N", "S", "W", "E")          # 寻路的固定并列裁决顺序

RULES = {"respawn": 14, "regen": 2, "fountain": 8, "minion_cap": 6, "wave_start": 3, "wave_gap": 12,
         "level_max": 12, "level_hp": 12, "level_atk": 2, "aggro": 2, "tower_range": 2, "camp_respawn": 45,
         "xp_minion": 10, "xp_hero_kill": 60, "xp_hero_near": 30,
         "xp_camp_kill": 35, "xp_camp_near": 15, "xp_tower": 30}

PRESETS = [
    dict(size=33, npc_heroes=3, npc_mult=0.8, wave_size=2, minion_hp=55, minion_atk=14,
         tower_hp=300, tower_atk=32, crystal_hp=300, camps=2, camp_hp=90, camp_atk=12, turns=700),
    dict(size=41, npc_heroes=4, npc_mult=1.0, wave_size=3, minion_hp=70, minion_atk=18,
         tower_hp=380, tower_atk=40, crystal_hp=380, camps=3, camp_hp=110, camp_atk=16, turns=900),
    dict(size=49, npc_heroes=5, npc_mult=1.25, wave_size=4, minion_hp=85, minion_atk=22,
         tower_hp=460, tower_atk=48, crystal_hp=460, camps=4, camp_hp=130, camp_atk=20, turns=1100),
]

CLASSES = [
    dict(pos="上单", cls="战士", hp=150, atk=22, rng=1, skill="回春术", cd=8),
    dict(pos="打野", cls="刺客", hp=100, atk=30, rng=1, skill="突袭", cd=5),
    dict(pos="中单", cls="法师", hp=80, atk=32, rng=2, skill="火球", cd=6),
    dict(pos="射手", cls="射手", hp=100, atk=26, rng=2, skill="穿云箭", cd=6),
    dict(pos="辅助", cls="辅助", hp=120, atk=20, rng=2, skill="治疗术", cd=8),
]
LEVEL_XP = [40 + 20 * k for k in range(RULES["level_max"] - 1)]   # 升到 2..12 级各需要的经验


def distance(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1])


def rot(p, s):
    """地图 180° 旋转：左下角基地 ↔ 右上角基地。"""
    return (s - 1 - p[0], s - 1 - p[1])


def line_cells(a, b):
    """直线（同行或同列）从 a 到 b 的中间格；不是直线时返回 None。"""
    if a[0] == b[0] and a[1] != b[1]:
        step = 1 if b[1] > a[1] else -1
        return [(a[0], y) for y in range(a[1] + step, b[1], step)]
    if a[1] == b[1] and a[0] != b[0]:
        step = 1 if b[0] > a[0] else -1
        return [(x, a[1]) for x in range(a[0] + step, b[0], step)]
    return None


class Game:
    TIME_LIMITS = (2.0, 0.05)   # 首回合 2 秒，之后每回合 50 毫秒

    def __init__(self, n, seed=1, difficulty=1):
        if n not in (1, 2):
            raise ValueError("支持 1（单人挑战 NPC 队）或 2（5v5 对战）个程序")
        if not 0 <= difficulty <= 2:
            raise ValueError("难度是 0 简单 / 1 普通 / 2 困难")
        self.n, self.seed, self.difficulty = n, seed, difficulty
        self.rng = random.Random(seed)
        self.preset = dict(PRESETS[difficulty])
        self.size = self.preset["size"]
        self.walls, self.base_rects = self._gen_map()
        self._place()
        self.turn, self.done = 0, False
        self.winner, self.reason = -1, ""
        self.stats = [Counter() for _ in range(2)]
        self.events = []
        self._skill_used = set()
        self._sent_init = set()
        self._field_cache = {}      # 距离场缓存：键是 (障碍版本, 目标集合)
        self._block_ver = 0         # 静态障碍（塔/营地）版本号，变了就清缓存
        self.check()

    # ---------- 地图生成 ----------

    def _gen_map(self):
        S = self.size
        walls = {(x, y) for x in range(S) for y in range(S)}
        base0 = {(x, y) for x in range(2, 8) for y in range(S - 8, S - 2)}
        base1 = {rot(p, S) for p in base0}
        laneA = ({(x, y) for x in (2, 3) for y in range(2, S - 7)} |
                 {(x, y) for y in (2, 3) for x in range(2, S - 7)})
        laneB = {rot(p, S) for p in laneA}
        laneC = {(x, y) for x in range(2, S - 2) for y in range(2, S - 2) if x + y in (S - 2, S - 1, S)}
        roads = base0 | base1 | laneA | laneB | laneC
        walls -= roads
        # 野区：成对的格子一起决定留墙还是打通，保证地图 180° 对称
        pairs = []
        for y in range(1, S - 1):
            for x in range(1, S - 1):
                p, q = (x, y), rot((x, y), S)
                if p < q and p in walls:
                    pairs.append((p, q))
        for p, q in pairs:
            if self.rng.random() >= 0.45:
                walls.discard(p)
                walls.discard(q)
        # 野区营地：一条对角线上 k 个 + 旋转对称的 k 个，清出 3×3 营地并修一条通往最近车道的窄路。
        # 路和营地都成对挖（一起挖 180° 旋转像），保证地图对称
        half = self._camp_centers()[:self.preset["camps"]]
        for c in half:
            for p in (c, rot(c, S)):
                walls -= {(x, y) for x in range(p[0] - 1, p[0] + 2) for y in range(p[1] - 1, p[1] + 2)}
            near = min(roads, key=lambda p: (distance(p, c), p))
            path = [(c[0], c[1])]
            for xx in range(c[0], near[0], 1 if near[0] > c[0] else -1):
                path.append((xx, c[1]))
            for yy in range(c[1], near[1], 1 if near[1] > c[1] else -1):
                path.append((near[0], yy))
            for p in path:
                walls.discard(p)
                walls.discard(rot(p, S))
        walls |= {(x, 0) for x in range(S)} | {(x, S - 1) for x in range(S)}
        walls |= {(0, y) for y in range(S)} | {(S - 1, y) for y in range(S)}
        assert all(rot(p, S) in walls for p in walls), "地图必须 180° 对称"
        return walls, [sorted(base0), sorted(base1)]

    def _camp_centers(self):
        S, k = self.size, self.preset["camps"]
        half = []
        for i in range(k):
            t = (i + 0.5) / k
            cx = round((0.28 + 0.27 * t) * S)
            cy = round((0.55 - 0.27 * t) * S)
            half.append((min(max(cx, 4), S - 5), min(max(cy, 4), S - 5)))
        return half + [rot(p, S) for p in half]

    def _place(self):
        S, P = self.size, self.preset
        # 兵线从己方水晶出发，沿车道走到对方水晶（最后一段在基地里，直线走到水晶旁）
        railA = ([(3, y) for y in range(S - 4, 3, -1)] + [(3, 3)] + [(x, 3) for x in range(4, S - 3)])
        railC = ([(3, y) for y in range(S - 4, S - 9, -1)] + [(x, S - 8) for x in range(4, 7)] +
                 [(x, S - 1 - x) for x in range(7, S - 7)] +
                 [(x, 7) for x in range(S - 7, S - 3)] + [(S - 4, y) for y in range(6, 2, -1)])
        rotA = [rot(p, S) for p in railA]
        # 每队的每条路都从自己的基地出发：走廊 A、B 两队共用，方向相反
        self.rails = {("A", 0): railA, ("A", 1): railA[::-1],
                      ("B", 0): rotA[::-1], ("B", 1): rotA,
                      ("C", 0): railC, ("C", 1): railC[::-1]}
        # 防御塔：每队每路内塔 22%、外塔 42% 处，站在车道里兵线不走的另一格上
        def off_cell(p, team):
            x, y = p
            if x == 3:
                return (2, y)
            if x == S - 4:
                return (S - 3, y)
            if y == 3:
                return (x, 2)
            if y == S - 4:
                return (x, S - 3)
            return (x, y + 1 if team == 0 else y - 1)   # 中路：两队各让到带子相反的一侧，互为旋转像
        self.towers, tid = [], 10
        for team in range(2):
            for lane in range(3):
                rail = self.rails[("ABC"[lane], team)]
                for tier, f in ((0, 0.22), (1, 0.42)):
                    p = rail[int(f * (len(rail) - 1))]
                    self.towers.append({"kind": "tower", "id": tid, "team": team, "lane": lane, "tier": tier,
                                        "pos": off_cell(p, team), "hp": P["tower_hp"], "maxhp": P["tower_hp"],
                                        "atk": P["tower_atk"]})
                    tid += 1
        self.crystal = [{"kind": "crystal", "id": 22 + t, "team": t, "pos": None,
                         "hp": P["crystal_hp"], "maxhp": P["crystal_hp"]} for t in range(2)]
        self.crystal[0]["pos"] = (3, S - 4)
        self.crystal[1]["pos"] = rot((3, S - 4), S)
        self.camps = [{"kind": "camp", "id": 24 + i, "pos": p, "hp": P["camp_hp"], "maxhp": P["camp_hp"],
                       "atk": P["camp_atk"], "alive": True, "respawn_at": None}
                      for i, p in enumerate(self._camp_centers())]
        # 英雄与出生点：出生点避开全部兵线（x=3 列、y=S-4 行、中路接驳段），否则会堵住自己第一波兵
        spawn0 = [(2, S - 5), (4, S - 5), (5, S - 5), (4, S - 6), (5, S - 6)]
        self.spawns = [spawn0, [rot(p, S) for p in spawn0]]
        mult = P["npc_mult"] if self.n == 1 else 1.0
        npc_count = P["npc_heroes"] if self.n == 1 else 5
        self.heroes = []
        for team, count in ((0, 5), (1, npc_count)):
            for k2 in range(count):
                c = CLASSES[k2]
                hp = round(c["hp"] * (mult if team == 1 else 1.0))
                self.heroes.append({"kind": "hero", "id": len(self.heroes), "team": team, "cls": k2, "pos": None,
                                    "hp": hp, "maxhp": hp, "atk": round(c["atk"] * (mult if team == 1 else 1.0)),
                                    "rng": c["rng"], "level": 1, "xp": 0, "cd": 0, "respawn_at": 0})
        for h in self.heroes:
            h["pos"] = list(self.spawns[h["team"]][h["id"] % 5])
        self.minions, self.minion_next_id = [], 24 + len(self.camps)
        self.units = ({h["id"]: h for h in self.heroes} | {t["id"]: t for t in self.towers} |
                      {c["id"]: c for c in self.crystal} | {c["id"]: c for c in self.camps})
        # 结构检查：从出生点出发，每个营地和对方水晶都要能走到
        floor = {(x, y) for x in range(S) for y in range(S)} - self.walls
        seen, q = {tuple(self.spawns[0][0])}, deque([tuple(self.spawns[0][0])])
        while q:
            x, y = q.popleft()
            for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                p = (x + dx, y + dy)
                if p in floor and p not in seen:
                    seen.add(p)
                    q.append(p)
        for c in self.camps:
            assert c["pos"] in seen, f"营地 {c['pos']} 不连通"
        assert self.crystal[1]["pos"] in seen, "对方水晶不可达"
        assert {rot(t["pos"], S) for t in self.towers if t["team"] == 0} == \
            {t["pos"] for t in self.towers if t["team"] == 1}, "防御塔必须 180° 对称"

    # ---------- 静态占位 ----------

    def _static_block(self):
        return (self.walls | {t["pos"] for t in self.towers if t["hp"] > 0} |
                {c["pos"] for c in self.camps if c["alive"]} | {c["pos"] for c in self.crystal})

    def _block_changed(self):
        """塔被拆、营地死亡或重生：静态障碍变了，缓存的距离场随之失效。"""
        self._block_ver += 1
        self._field_cache.clear()

    # ---------- 文本协议 ----------

    def map_rows(self):
        if not hasattr(self, "_rows"):
            self._rows = ["".join("#" if (x, y) in self.walls else "." for x in range(self.size))
                          for y in range(self.size)]
        return self._rows

    def encode(self, team):
        out = []
        if team not in self._sent_init:
            self._sent_init.add(team)
            P, R = self.preset, RULES
            out.append(f"INIT {self.n} {team} {self.size} {P['turns']} {self.difficulty} "
                       f"{P['wave_size']} {R['wave_gap']} {P['minion_hp']} {P['minion_atk']} "
                       f"{R['tower_range']} {R['respawn']} {R['regen']} {R['fountain']} "
                       f"{R['minion_cap']} {R['aggro']} {P['npc_heroes'] if self.n == 1 else 5}")
            out.append("MAP")
            out.extend(self.map_rows())
            out.append(f"CLASSES {len(CLASSES)}")
            for i, c in enumerate(CLASSES):
                out.append(f"K {i} {c['hp']} {c['atk']} {c['rng']} {c['cd']} {c['pos']} {c['cls']} {c['skill']}")
            out.append(f"LEVELS {len(LEVEL_XP)}")
            out.append(" ".join(map(str, LEVEL_XP)))
            out.append(f"SPAWNS {len(self.spawns[0]) * 2}")
            for t in range(2):
                out.extend(f"P {x} {y}" for x, y in self.spawns[t])
            out.append(f"BASES {len(self.base_rects)}")
            for b in self.base_rects:
                out.append(f"B {b[0][0]} {b[0][1]} {b[-1][0]} {b[-1][1]}")
            out.append(f"TOWERS {len(self.towers)}")
            out.extend(f"T {t['id']} {t['team']} {t['lane']} {t['tier']} {t['pos'][0]} {t['pos'][1]} "
                       f"{t['maxhp']} {t['atk']}" for t in self.towers)
            out.append(f"NEXUS {len(self.crystal)}")
            out.extend(f"N {c['id']} {c['team']} {c['pos'][0]} {c['pos'][1]} {c['maxhp']}" for c in self.crystal)
            out.append(f"CAMPS {len(self.camps)}")
            out.extend(f"J {c['id']} {c['pos'][0]} {c['pos'][1]} {c['maxhp']} {c['atk']} {R['xp_camp_kill']}"
                       for c in self.camps)
        out.append(f"TURN {self.turn + 1}")
        out.append("CRYSTAL " + " ".join(str(c["hp"]) for c in self.crystal))
        out.append(f"TOWERS {len(self.towers)}")
        out.extend(f"T {t['id']} {t['hp']}" for t in self.towers)
        out.append(f"CAMPS {len(self.camps)}")
        out.extend(f"J {c['id']} {c['hp'] if c['alive'] else 0} {-1 if c['respawn_at'] is None else c['respawn_at']}"
                   for c in self.camps)
        out.append(f"MINIONS {sum(1 for m in self.minions if m['alive'])}")
        out.extend(f"I {m['id']} {m['team']} {m['lane']} {m['pos'][0]} {m['pos'][1]} {m['hp']}"
                   for m in self.minions if m["alive"])
        out.append(f"HEROES {len(self.heroes)}")
        for h in self.heroes:
            x, y = h["pos"] if h["pos"] else (-1, -1)
            out.append(f"H {h['id']} {h['team']} {x} {y} {h['hp']} {h['maxhp']} {h['level']} {h['xp']} "
                       f"{h['cd']} {-1 if h['pos'] is not None else h['respawn_at']}")
        out.append(f"EVENTS {len(self.events)}")
        for e in self.events:
            out.append("E " + e["line"])
        out.append("END")
        return "\n".join(out) + "\n"

    def decode(self, team, line):
        """解析一行回复：回合号 + 己方每个英雄各一个移动和一个动作（法师/射手的 Q 后跟目标编号）。

        协议是严格的：每个英雄必须正好一组「移动 + 动作」，写多了、写少了都不算合法；
        行尾 '#' 之后是注释，注释段数不得超过英雄数。多余的 token 一律判格式错误。
        """
        body, _, note = line.partition("#")
        tok = body.split()
        if not tok or tok[0] != str(self.turn + 1):
            raise ValueError(f"行首应为本回合编号 {self.turn + 1}")
        mine = [h for h in self.heroes if h["team"] == team]
        notes = [s.strip()[:20] for s in note.split(";")] if note.strip() else []
        if len(notes) > len(mine):
            raise ValueError(f"注释段数过多：最多 {len(mine)} 段，收到 {len(notes)} 段")
        it = iter(tok[1:])
        heroes = []
        for k, h in enumerate(mine):
            mv, act = next(it, None), next(it, None)
            if mv is None or act is None:
                raise ValueError(f"指令不足：{len(mine)} 个英雄各要一组移动和动作，第 {k + 1} 个不完整")
            if mv not in TEXT_MOVES:
                raise ValueError(f"移动只能是 U D L R S，收到 {mv}")
            cmd = {"id": h["id"], "move": TEXT_MOVES[mv]}
            if act == "-":
                cmd["act"] = "wait"
            elif act == "Q":
                cmd["act"] = "skill"
                if CLASSES[h["cls"]]["skill"] in ("火球", "穿云箭"):
                    t = next(it, None)
                    if t is None or not t.isdigit():
                        raise ValueError(f"{CLASSES[h['cls']]['skill']} 需要一个目标编号")
                    cmd["target"] = int(t)
            elif act.isdigit():
                cmd.update(act="attack", target=int(act))
            else:
                raise ValueError(f"动作只能是 - Q 或目标编号，收到 {act}")
            if k < len(notes) and notes[k]:
                cmd["note"] = notes[k]
            heroes.append(cmd)
        extra = next(it, None)
        if extra is not None:
            raise ValueError(f"多余的指令内容：{extra}（每个英雄只能写一组移动和动作）")
        return {"heroes": heroes}

    def header(self):
        return {"version": VERSION, "seed": self.seed, "difficulty": self.difficulty,
                "difficulty_label": DIFF_LABELS[self.difficulty], "teams": self.n,
                "npc_heroes": self.preset["npc_heroes"] if self.n == 1 else 0, "size": self.size,
                "map": self.map_rows(), "base_rects": self.base_rects, "spawns": self.spawns,
                "towers": [{k: t[k] for k in ("id", "team", "lane", "tier", "pos", "maxhp", "atk")}
                           for t in self.towers],
                "crystal": [{k: c[k] for k in ("id", "team", "pos", "maxhp")} for c in self.crystal],
                "camps": [{k: c[k] for k in ("id", "pos", "maxhp", "atk")} for c in self.camps],
                "classes": CLASSES, "rules": dict(RULES)}

    # ---------- 回合结算 ----------

    def step(self, orders):
        if self.done:
            raise RuntimeError("比赛已结束")
        self.turn += 1
        ev = self.events = []

        # 1. 回合开始：重生、回血、冷却、营地刷新、小兵增援
        occupied = self._static_block() | {tuple(m["pos"]) for m in self.minions if m["alive"]} | \
            {tuple(h["pos"]) for h in self.heroes if h["pos"] is not None}
        for h in self.heroes:
            if h["pos"] is None and self.turn >= h["respawn_at"]:
                for p in self.spawns[h["team"]]:
                    if p not in occupied:
                        h.update(pos=list(p), hp=h["maxhp"], respawn_at=None)
                        occupied.add(p)
                        ev.append({"line": f"respawn {h['id']} {p[0]} {p[1]}", "type": "respawn",
                                   "id": h["id"], "pos": list(p)})
                        break
                else:
                    h["respawn_at"] = self.turn + 1
        base0, base1 = set(self.base_rects[0]), set(self.base_rects[1])
        for h in self.heroes:
            if h["pos"] is not None:
                home = base0 if h["team"] == 0 else base1
                h["hp"] = min(h["maxhp"], h["hp"] + RULES["regen"] +
                              (RULES["fountain"] if tuple(h["pos"]) in home else 0))
            h["cd"] = max(0, h["cd"] - 1)
        for c in self.camps:
            if not c["alive"] and self.turn >= c["respawn_at"] and \
                    not any(tuple(u["pos"]) == c["pos"] for u in self.heroes + self.minions
                            if u.get("pos") is not None and u.get("alive", True)):
                c.update(alive=True, hp=c["maxhp"], respawn_at=None)
                self._block_changed()
        if self.turn >= RULES["wave_start"] and (self.turn - RULES["wave_start"]) % RULES["wave_gap"] == 0:
            taken = self._static_block() | {tuple(h["pos"]) for h in self.heroes if h["pos"] is not None} | \
                {tuple(m["pos"]) for m in self.minions if m["alive"]}
            for team in range(2):
                for lane in range(3):
                    rail = self.rails[("ABC"[lane], team)]
                    alive = sum(1 for m in self.minions if m["alive"] and m["team"] == team and m["lane"] == lane)
                    room = max(0, RULES["minion_cap"] - alive)
                    if room == 0:
                        continue
                    at = 0
                    for _ in range(min(self.preset["wave_size"], room)):
                        while at < len(rail) and rail[at] in taken:
                            at += 1
                        if at >= len(rail):
                            break
                        m = {"kind": "minion", "id": self.minion_next_id, "team": team, "lane": lane,
                             "pos": list(rail[at]), "hp": self.preset["minion_hp"],
                             "maxhp": self.preset["minion_hp"], "atk": self.preset["minion_atk"], "alive": True}
                        self.minions.append(m)
                        self.units[m["id"]] = m
                        self.minion_next_id += 1
                        taken.add(rail[at])
                        at += 1

        # 2. 意图：选手的命令 + 单人挑战时引擎替 NPC 队下命令；技能在声明时就进冷却
        intents = {}
        for team in range(self.n):
            intents[team] = orders.get(team) if isinstance(orders.get(team), dict) else {"heroes": []}
        if self.n == 1:
            intents[1] = self._npc_orders()
        self._skill_used = set()
        for h in self.heroes:
            if h["pos"] is None:
                continue
            cmd = self._cmd(intents, h)
            if cmd.get("act") == "skill" and h["cd"] == 0:
                self._skill_used.add(h["id"])
                h["cd"] = CLASSES[h["cls"]]["cd"]

        # 3. 英雄同时移动（刺客突袭 = 直线两格）
        self._move_heroes(intents, ev)
        # 4. 小兵依次移动（编号顺序；抢不过格子就原地等）
        self._move_minions()
        # 5. 治疗技能（战士回春、辅助治疗）先生效
        self._heals(intents, ev)
        # 6. 所有攻击同时结算：普攻、火球、穿云箭、小兵、防御塔、野怪
        self._combat(intents, ev)
        # 定期回收死亡小兵：长局里尸体越积越多，每 20 回合清一次，遍历成本才不会一路涨
        if self.turn % 20 == 0:
            self._compact_minions()

        if not self.done:
            self.done = self.turn >= self.preset["turns"]
            if self.done:
                self._decide_by_score()
        self.check()

        shown = {}
        for team in range(2):
            shown[team] = [{k: c[k] for k in ("id", "move", "act", "target", "note") if k in c}
                           for c in intents.get(team, {}).get("heroes", []) if isinstance(c, dict)]
        return self.frame(shown)

    def _cmd(self, intents, h):
        for c in intents.get(h["team"], {}).get("heroes", []):
            if isinstance(c, dict) and c.get("id") == h["id"]:
                return c
        return {}

    def _move_heroes(self, intents, ev):
        live = [h for h in self.heroes if h["pos"] is not None]
        start = {h["id"]: tuple(h["pos"]) for h in live}
        static = self._static_block() | {tuple(m["pos"]) for m in self.minions if m["alive"]}
        want, dashed, via, moves = {}, {}, {}, {}
        for h in live:
            cmd = self._cmd(intents, h)
            d = MOVES.get(cmd.get("move", "WAIT"), (0, 0))
            p0 = start[h["id"]]
            p1 = (p0[0] + d[0], p0[1] + d[1])
            moves[h["id"]] = d
            want[h["id"]] = p0 if p1 in static else p1
        for h in live:
            hid, p0, d = h["id"], start[h["id"]], moves[h["id"]]
            p1 = want[hid]
            if hid not in self._skill_used or CLASSES[h["cls"]]["skill"] != "突袭" or p1 == p0:
                continue
            p2 = (p1[0] + d[0], p1[1] + d[1])
            occupant = next((i for i, p in start.items() if p == p1 and i != hid), None)
            if p2 not in static and (occupant is None or want[occupant] != start[occupant]):
                want[hid] = p2
                dashed[hid] = p1
                via[hid] = p1

        while True:
            count = Counter(want.values())
            blocked = {i for i, p in want.items() if count[p] > 1}
            owner = {p: i for i, p in start.items()}
            for i, p in want.items():
                j = owner.get(p)
                if j is not None and j != i and want[j] == start[i] and self.heroes[i]["team"] != self.heroes[j]["team"]:
                    blocked.update((i, j))
            while True:
                more = {i for i, p in want.items() if i not in blocked and p != start[i] and p in owner
                        and (owner[p] in blocked or want[owner[p]] == start[owner[p]])}
                if not more:
                    break
                blocked |= more

            broken = {i for i in dashed if i not in blocked and via[i] in owner
                      and (owner[via[i]] in blocked or want[owner[via[i]]] == start[owner[via[i]]])}
            if not broken:
                break
            for i in broken:
                dashed.pop(i)
                want[i] = via[i]
        for h in live:
            h["pos"] = list(start[h["id"]] if h["id"] in blocked else want[h["id"]])
            if h["id"] in dashed and tuple(h["pos"]) == want[h["id"]]:
                ev.append({"line": f"dash {h['id']} {dashed[h['id']][0]} {dashed[h['id']][1]}",
                           "type": "dash", "id": h["id"], "via": list(dashed[h["id"]])})

    def _move_minions(self):
        blocked = self._static_block() | {tuple(h["pos"]) for h in self.heroes if h["pos"] is not None} | \
            {tuple(m["pos"]) for m in self.minions if m["alive"]}
        # 奇偶回合交替正反序，抵消编号带来的先手优势
        for m in (self.minions if self.turn % 2 == 0 else self.minions[::-1]):
            if not m["alive"]:
                continue
            p = tuple(m["pos"])
            foes = [h for h in self.heroes if h["pos"] is not None and h["team"] != m["team"]]
            foes += [o for o in self.minions if o["alive"] and o["team"] != m["team"]]
            foes += [t for t in self.towers if t["hp"] > 0 and t["team"] != m["team"]]
            foes += [c for c in self.crystal if c["hp"] > 0 and c["team"] != m["team"]]
            near = min(foes, key=lambda u: (distance(u["pos"], p), u["id"]))
            d = distance(near["pos"], p)
            if d <= RULES["aggro"]:
                if d > 1:
                    step = self._step_toward(p, near["pos"], blocked)
                    if step:
                        m["pos"] = list(step)
                        blocked.add(step)
                        blocked.discard(p)
                continue                # 贴脸或够不到时不再走，交给攻击阶段
            rail = self.rails[("ABC"[m["lane"]], m["team"])]
            idx = min(range(len(rail)), key=lambda i: (distance(rail[i], p), i))
            if idx + 1 < len(rail):
                nxt = rail[idx + 1]
                if nxt not in blocked:
                    m["pos"] = list(nxt)
                    blocked.add(nxt)
                    blocked.discard(p)

    def _step_toward(self, p, target, blocked):
        best, bd = None, distance(p, target)
        for mv in DIR_ORDER:
            dx, dy = MOVES[mv]
            q = (p[0] + dx, p[1] + dy)
            if q not in blocked and distance(q, target) < bd:
                best, bd = q, distance(q, target)
        return best

    def _heals(self, intents, ev):
        for h in self.heroes:
            if h["pos"] is None or h["id"] not in self._skill_used:
                continue
            skill = CLASSES[h["cls"]]["skill"]
            if skill == "回春术":
                amount = 50 + 6 * h["level"]
                h["hp"] = min(h["maxhp"], h["hp"] + amount)
                ev.append({"line": f"heal {h['id']} {h['id']} {amount}", "type": "heal",
                           "src": h["id"], "tgt": h["id"], "amount": amount})
            elif skill == "治疗术":
                allies = [o for o in self.heroes if o["team"] == h["team"] and o["pos"] is not None
                          and distance(o["pos"], h["pos"]) <= 2]
                if allies:
                    t = min(allies, key=lambda o: (o["hp"] / o["maxhp"], o["id"]))
                    amount = 40 + 5 * h["level"]
                    t["hp"] = min(t["maxhp"], t["hp"] + amount)
                    ev.append({"line": f"heal {h['id']} {t['id']} {amount}", "type": "heal",
                               "src": h["id"], "tgt": t["id"], "amount": amount})

    def _combat(self, intents, ev):
        pool = []          # (来源单位, 目标 id, 伤害)

        def add(src, tgt, dmg, kind="attack"):
            pool.append((src, tgt["id"], dmg))
            if src["kind"] in ("hero", "tower", "camp") or tgt["kind"] in ("hero", "tower", "crystal"):
                ev.append({"line": f"{kind} {src['id']} {tgt['id']} {dmg}", "type": kind,
                           "src": src["id"], "tgt": tgt["id"], "amount": dmg})

        targets = [u for u in self.units.values() if self._alive(u)]
        for h in self.heroes:
            if h["pos"] is None:
                continue
            cmd = self._cmd(intents, h)
            p = tuple(h["pos"])
            skill = CLASSES[h["cls"]]["skill"]
            tgt = self.units.get(cmd.get("target")) if isinstance(cmd.get("target"), int) else None
            if h["id"] in self._skill_used and skill == "火球":
                if tgt is not None and self._alive(tgt) and self._hostile(h, tgt) and \
                        distance(p, tgt["pos"]) <= 5 and (mid := line_cells(p, tgt["pos"])) is not None and \
                        all(q not in self.walls for q in mid):
                    add(h, tgt, 45 + 6 * h["level"], "fireball")
            elif h["id"] in self._skill_used and skill == "穿云箭":
                if tgt is not None and self._alive(tgt) and self._hostile(h, tgt) and \
                        distance(p, tgt["pos"]) <= 4 and line_cells(p, tgt["pos"]) is not None:
                    dx = (tgt["pos"][0] > p[0]) - (tgt["pos"][0] < p[0])
                    dy = (tgt["pos"][1] > p[1]) - (tgt["pos"][1] < p[1])
                    cells = {p}
                    for step in range(1, 5):
                        q = (p[0] + dx * step, p[1] + dy * step)
                        if q in self.walls:
                            break
                        cells.add(q)
                    for u in targets:
                        if u["id"] != h["id"] and tuple(u["pos"]) in cells and self._hostile(h, u):
                            add(h, u, h["atk"], "pierce")
            elif cmd.get("act") == "attack" and tgt is not None and self._alive(tgt) and self._hostile(h, tgt) and \
                    self._can_hit(p, tgt["pos"], h["rng"]):
                add(h, tgt, h["atk"])
        for m in self.minions:
            if not m["alive"]:
                continue
            p = tuple(m["pos"])
            adj = [u for u in targets if u.get("team") is not None and u["team"] != m["team"] and
                   distance(u["pos"], p) <= 1]
            if adj:
                t = min(adj, key=lambda u: ({"minion": 0, "hero": 1}.get(u["kind"], 2), u["id"]))
                add(m, t, m["atk"])
        for t in self.towers:
            if t["hp"] <= 0:
                continue
            cands = [u for u in targets if u["kind"] in ("hero", "minion") and u["team"] != t["team"] and
                     self._can_hit(t["pos"], u["pos"], RULES["tower_range"])]
            if cands:
                t2 = min(cands, key=lambda u: (distance(u["pos"], t["pos"]), 1 if u["kind"] == "hero" else 0, u["id"]))
                add(t, t2, t["atk"])
        for c in self.camps:
            if not c["alive"]:
                continue
            adj = [u for u in targets if u["kind"] in ("hero", "minion") and distance(u["pos"], c["pos"]) <= 1]
            if adj:
                add(c, min(adj, key=lambda u: u["id"]), c["atk"])

        dmg, hitters = Counter(), {}
        for src, tid, d in pool:
            dmg[tid] += d
            hitters.setdefault(tid, []).append(src)
        live_ids = {u["id"] for u in targets}      # 攻击宣言时的存活名单（塔和水晶扣血后 _alive 会变）
        for tid, d in dmg.items():
            u = self.units.get(tid)
            if u is not None and tid in live_ids:
                u["hp"] -= d
        for tid in dmg:
            u = self.units.get(tid)
            if u is None or tid not in live_ids or u["hp"] > 0:
                continue
            pos = list(u["pos"]) if u.get("pos") is not None else None
            by = [s["id"] for s in hitters[tid]]
            ev.append({"line": f"death {tid} {pos[0] if pos else -1} {pos[1] if pos else -1} {len(by)} " +
                               " ".join(map(str, by)),
                       "type": "death", "id": tid, "pos": pos, "by": by, "kind": u["kind"]})
            if u["kind"] == "hero":
                u["pos"], u["respawn_at"] = None, self.turn + RULES["respawn"]
                if any(s.get("team") == 1 - u["team"] for s in hitters[tid]):
                    self.stats[1 - u["team"]]["kills"] += 1     # 中立野怪击杀只记 D 不记 K
                self.stats[u["team"]]["deaths"] += 1
                helpers = {s["id"] for s in hitters[tid]
                           if s.get("team") == 1 - u["team"] and s["kind"] == "hero"}
                self.stats[1 - u["team"]]["assists"] += max(0, len(helpers) - 1)
                killers = [s for s in hitters[tid] if s.get("team") == 1 - u["team"] and s["kind"] == "hero"]
                for s in killers:
                    self._give_xp(s, RULES["xp_hero_kill"], ev)
                self._xp_near(pos, 1 - u["team"], RULES["xp_hero_near"], ev, skip={s["id"] for s in killers})
            elif u["kind"] == "minion":
                u["alive"] = False
                self._xp_near(pos, 1 - u["team"], RULES["xp_minion"], ev)
                self.stats[1 - u["team"]]["minions"] += 1
            elif u["kind"] == "tower":
                u["hp"] = 0
                self._block_changed()
                team = 1 - u["team"]
                self.stats[team]["towers"] += 1
                ev.append({"line": f"tower {u['id']} {team} {u['pos'][0]} {u['pos'][1]}", "type": "tower",
                           "id": u["id"], "team": team, "pos": list(u["pos"])})
                for h in self.heroes:
                    if h["team"] == team and h["pos"] is not None:
                        self._give_xp(h, RULES["xp_tower"], ev)
            elif u["kind"] == "camp":
                u["alive"] = False
                u["respawn_at"] = self.turn + RULES["camp_respawn"]
                self._block_changed()
                for s in hitters[tid]:
                    if s["kind"] == "hero":
                        self.stats[s["team"]]["camps"] += 1
                        self._give_xp(s, RULES["xp_camp_kill"], ev)
                        self._xp_near(pos, s["team"], RULES["xp_camp_near"], ev, skip={s["id"]})
                        break
            elif u["kind"] == "crystal":
                u["hp"] = 0
                ev.append({"line": f"crystal {u['team']} {u['pos'][0]} {u['pos'][1]}", "type": "crystal",
                           "team": u["team"], "pos": list(u["pos"])})
        self._finish_combat()

    def _finish_combat(self):
        destroyed = [c for c in self.crystal if c["hp"] <= 0]
        if not destroyed:
            return
        if len(destroyed) == 1:
            self.done = True
            self.winner = 1 - destroyed[0]["team"]
            self.reason = f"{self.turn} 回合，{self.winner} 号队摧毁水晶"
            return

        self.done = True
        kdas = (self._kda(0), self._kda(1))
        if abs(kdas[0] - kdas[1]) > 1e-9:
            self.winner = 0 if kdas[0] > kdas[1] else 1
        elif self.stats[0]["kills"] != self.stats[1]["kills"]:
            self.winner = 0 if self.stats[0]["kills"] > self.stats[1]["kills"] else 1
        elif self.stats[0]["deaths"] != self.stats[1]["deaths"]:
            self.winner = 0 if self.stats[0]["deaths"] < self.stats[1]["deaths"] else 1
        else:
            fracs = (self._building_frac(0), self._building_frac(1))
            if abs(fracs[0] - fracs[1]) > 1e-9:
                self.winner = 0 if fracs[0] > fracs[1] else 1
        verdict = "平局" if self.winner < 0 else f"{self.winner} 号队获胜"
        self.reason = (f"{self.turn} 回合，双方水晶同时被毁，按 KDA 判定："
                       f"0 队 {kdas[0]:.3f} vs 1 队 {kdas[1]:.3f}，{verdict}")

    def _kda(self, team):
        s = self.stats[team]
        return (s["kills"] + s["assists"]) / max(1, s["deaths"])

    def _building_frac(self, team):
        total = sum(t["maxhp"] for t in self.towers if t["team"] == team) + self.crystal[team]["maxhp"]
        left = sum(t["hp"] for t in self.towers if t["team"] == team and t["hp"] > 0) + \
            max(0, self.crystal[team]["hp"])
        return left / total

    def _compact_minions(self):
        """清掉已经死亡的小兵（同时从 units 表里移除），保持小兵列表不会无限增长。

        只删死亡的小兵，存活小兵的相对顺序不变，所以小兵移动的先后次序和以前完全一致。
        """
        dead = [m for m in self.minions if not m["alive"]]
        if not dead:
            return
        for m in dead:
            self.units.pop(m["id"], None)
        self.minions = [m for m in self.minions if m["alive"]]

    def _can_hit(self, a, b, rng):
        d = distance(a, b)
        if d == 0 or d > rng:
            return False
        if d == 1:
            return True
        dx, dy = b[0] - a[0], b[1] - a[1]
        if dx == 0:
            return (a[0], a[1] + dy // 2) not in self.walls
        if dy == 0:
            return (a[0] + dx // 2, a[1]) not in self.walls
        return (a[0] + dx, a[1]) not in self.walls or (a[0], a[1] + dy) not in self.walls

    def _hostile(self, u, t):
        return t.get("team") is None or t["team"] != u["team"]      # 中立营地也算可攻击

    def _alive(self, u):
        if u["kind"] == "hero":
            return u["pos"] is not None
        if u["kind"] in ("minion", "camp"):
            return u["alive"]
        return u["hp"] > 0                          # 塔、水晶

    def _xp_near(self, pos, team, amount, ev, skip=()):
        if pos is None:
            return
        for h in self.heroes:
            if h["team"] == team and h["pos"] is not None and h["id"] not in skip and \
                    distance(h["pos"], pos) <= 2:
                self._give_xp(h, amount, ev)

    def _give_xp(self, h, amount, ev):
        if h["level"] >= RULES["level_max"]:
            return
        h["xp"] += amount
        while h["level"] < RULES["level_max"] and h["xp"] >= LEVEL_XP[h["level"] - 1]:
            h["xp"] -= LEVEL_XP[h["level"] - 1]
            h["level"] += 1
            h["maxhp"] += RULES["level_hp"]
            if h["pos"] is not None:            # 阵亡当回合的升级不回血
                h["hp"] += RULES["level_hp"]
            h["atk"] += RULES["level_atk"]
            ev.append({"line": f"levelup {h['id']} {h['level']}", "type": "levelup",
                       "id": h["id"], "level": h["level"]})

    def _decide_by_score(self):
        frac = [(self._building_frac(0), self.stats[0]["kills"]),
                (self._building_frac(1), self.stats[1]["kills"])]
        if abs(frac[0][0] - frac[1][0]) > 1e-9:
            self.winner = 0 if frac[0][0] > frac[1][0] else 1
        elif frac[0][1] != frac[1][1]:
            self.winner = 0 if frac[0][1] > frac[1][1] else 1
        self.reason = f"打满 {self.turn} 回合，按剩余建筑血量与击杀判定"

    def frame(self, orders=None):
        return {"turn": self.turn, "crystal": [c["hp"] for c in self.crystal],
                "towers": [{"id": t["id"], "hp": t["hp"]} for t in self.towers],
                "camps": [{"id": c["id"], "hp": c["hp"] if c["alive"] else 0} for c in self.camps],
                "minions": [{"id": m["id"], "team": m["team"], "lane": m["lane"], "pos": list(m["pos"]),
                             "hp": m["hp"]} for m in self.minions if m["alive"]],
                "heroes": [{"id": h["id"], "pos": list(h["pos"]) if h["pos"] else None, "hp": h["hp"],
                            "maxhp": h["maxhp"], "level": h["level"], "cd": h["cd"]} for h in self.heroes],
                "events": list(self.events), "orders": orders or {}}

    def result(self):
        score = [0.5, 0.5] if self.winner < 0 else ([1.0, 0.0] if self.winner == 0 else [0.0, 1.0])
        stats = []
        for s in self.stats:
            row = {k: v for k, v in s.items()}
            for key in ("kills", "assists", "deaths", "towers", "minions", "camps"):
                row.setdefault(key, 0)
            stats.append(row)
        return {"score": score, "winner": self.winner, "reason": self.reason, "turns": self.turn,
                "kda": [self._kda(0), self._kda(1)], "stats": stats}

    def check(self):
        at = [tuple(u["pos"]) for u in self.heroes if u["pos"] is not None]
        at += [tuple(m["pos"]) for m in self.minions if m["alive"]]
        at += [t["pos"] for t in self.towers if t["hp"] > 0]
        at += [c["pos"] for c in self.camps if c["alive"]]
        at += [c["pos"] for c in self.crystal if c["hp"] > 0]
        assert len(at) == len(set(at)), "两个单位占了同一格"
        assert not set(at) & self.walls, "单位站在墙上"
        for h in self.heroes:
            assert (h["pos"] is not None) == (h["hp"] > 0), "英雄血量与生死不一致"
            assert h["hp"] <= h["maxhp"] and 1 <= h["level"] <= RULES["level_max"] and h["cd"] >= 0
            if h["pos"] is None:
                assert h["respawn_at"] > self.turn, "重生时间必须在未来"

    # ---------- 单人挑战的 NPC 队 ----------

    def _field(self, targets):
        """到 targets 的 BFS 距离场（墙、塔、营地、水晶都绕开）。目标格本身允许是 blocked。

        结果按（障碍版本, 目标集合）缓存：塔和营地的状态不变时，同一个目标的距离场算一次就够。
        """
        key = (self._block_ver, tuple(targets))
        hit = self._field_cache.get(key)
        if hit is not None:
            return hit
        S = self.size
        blocked = self._static_block()
        dist, q = {}, deque()
        for p in targets:
            dist[p] = 0
            q.append(p)
        while q:
            x, y = q.popleft()
            for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                p = (x + dx, y + dy)
                if 0 <= p[0] < S and 0 <= p[1] < S and p not in blocked and p not in dist:
                    dist[p] = dist[(x, y)] + 1
                    q.append(p)
        self._field_cache[key] = dist
        return dist

    def _npc_orders(self):
        """单人挑战时引擎替 NPC 队下命令：按职业分路推进（战士 A、刺客 B、法师 C、射手 B、辅助 C），
        贴脸就打，残血就撤，会用技能。"""
        if not any(h["team"] == 1 and h["pos"] is not None for h in self.heroes):
            return {"heroes": []}
        home = self._field(self.spawns[1])
        goal = self._field([self.crystal[0]["pos"]])
        busy = {tuple(u["pos"]) for u in self.heroes + self.minions
                if u.get("pos") is not None and u.get("alive", True)}
        npc = [h for h in self.heroes if h["team"] == 1]
        lanes = [0, 1, 2, 1, 2]
        # 每条路的推进目标：该路上离 NPC 基地最远的存活敌方塔（打完一座换下一座），没有塔了就直奔水晶
        lane_goal, lane_field = {}, {}
        for lane in range(3):
            towers_alive = [t for t in self.towers if t["team"] == 0 and t["hp"] > 0 and t["lane"] == lane]
            if towers_alive:
                lane_goal[lane] = max(towers_alive, key=lambda t: distance(t["pos"], self.crystal[1]["pos"]))["pos"]
                lane_field[lane] = self._field([lane_goal[lane]])
            else:
                lane_goal[lane] = self.crystal[0]["pos"]
                lane_field[lane] = goal
        orders = []
        for h in npc:
            if h["pos"] is None:
                continue
            p = tuple(h["pos"])
            skill = CLASSES[h["cls"]]["skill"]
            foes_h = [o for o in self.heroes if o["team"] == 0 and o["pos"] is not None]
            foes_all = foes_h + [m for m in self.minions if m["alive"] and m["team"] == 0] + \
                [t for t in self.towers if t["hp"] > 0 and t["team"] == 0] + [self.crystal[0]]
            cmd = {"id": h["id"], "move": "S", "act": "-"}
            eh = min(foes_h, key=lambda o: distance(o["pos"], p)) if foes_h else None
            if h["hp"] < 0.4 * h["maxhp"] and eh is not None and distance(eh["pos"], p) <= 6:
                cmd["move"] = self._greedy(p, home, busy)
            elif h["hp"] < 0.6 * h["maxhp"] and skill == "回春术" and h["cd"] == 0:
                cmd["act"] = "skill"
            else:
                in_rng = [u for u in foes_all if self._alive(u) and self._can_hit(p, u["pos"], h["rng"])]
                t = None
                acted = retreating = False
                if in_rng:
                    t = min(in_rng, key=lambda u: ({"hero": 0, "minion": 1}.get(u["kind"], 2),
                                                   u["hp"] / max(1, u["maxhp"]), u["id"]))
                    if t["kind"] in ("tower", "crystal") and h["hp"] < 0.45 * h["maxhp"]:
                        eh_d = distance(eh["pos"], p) if eh else 99
                        if eh_d >= 4:               # 血量不健康就不硬扛建筑火力
                            cmd["move"] = self._away(p, t["pos"])
                            t = None
                            retreating = True
                if t is not None:
                    cmd["act"], cmd["target"] = "attack", t["id"]
                    acted = True
                elif h["cd"] == 0 and skill == "火球":
                    line_t = [o for o in foes_h if (m := line_cells(p, o["pos"])) is not None and
                              distance(o["pos"], p) <= 5 and all(q not in self.walls for q in m)]
                    if line_t:
                        cmd["act"], cmd["target"] = "skill", min(line_t, key=lambda o: o["hp"])["id"]
                        acted = True
                elif h["cd"] == 0 and skill == "治疗术":
                    allies = [o for o in npc if o["pos"] is not None and o["id"] != h["id"] and
                              distance(o["pos"], p) <= 2 and o["hp"] < 0.7 * o["maxhp"]]
                    if allies:
                        cmd["act"] = "skill"
                        acted = True
                if not acted and not retreating:
                    if h["cd"] == 0 and skill == "突袭" and (eh is None or distance(eh["pos"], p) > 2):
                        cmd["act"] = "skill"        # 突袭让本回合的移动翻倍
                    cmd["move"] = self._greedy(p, lane_field[lanes[h["cls"]]], busy)
            orders.append(cmd)
        return {"heroes": orders}

    def _greedy(self, p, field, avoid=frozenset()):
        best, bv, alt, av = "S", field.get(p, 10 ** 9), "S", 10 ** 18
        for mv in DIR_ORDER:
            dx, dy = MOVES[mv]
            q = (p[0] + dx, p[1] + dy)
            if q in avoid or q not in field:
                continue
            if field[q] < bv:                 # 先找能缩短距离的
                best, bv = mv, field[q]
            if field[q] < av:                 # 走不动时侧移到离目标最近的空格，别卡死
                alt, av = mv, field[q]
        return best if best != "S" else alt

    def _away(self, p, threat):
        best, bd = "S", distance(p, threat)
        static = self._static_block()
        for mv in DIR_ORDER:
            dx, dy = MOVES[mv]
            q = (p[0] + dx, p[1] + dy)
            if q not in static and distance(q, threat) > bd:
                best, bd = mv, distance(q, threat)
        return best
