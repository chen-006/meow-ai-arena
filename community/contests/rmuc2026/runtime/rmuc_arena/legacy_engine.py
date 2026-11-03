"""Authoritative fixed-step simulation, independent of rendering and policies."""
from dataclasses import dataclass, asdict
import copy
import hashlib
import json
import math
import random

DT = 0.1
ROLES = ("hero", "engineer", "infantry", "infantry", "infantry", "sentry", "scout")
SPECS = {
    "hero": {"hp": 420, "speed": 2.8, "damage": 60, "cooldown": 0.9, "ammo": 36, "vision": 14, "range": 18},
    "engineer": {"hp": 380, "speed": 3.2, "damage": 12, "cooldown": 0.5, "ammo": 90, "vision": 11, "range": 12},
    "infantry": {"hp": 260, "speed": 3.8, "damage": 22, "cooldown": 0.3, "ammo": 120, "vision": 13, "range": 15},
    "sentry": {"hp": 480, "speed": 2.6, "damage": 18, "cooldown": 0.2, "ammo": 180, "vision": 16, "range": 17},
    "scout": {"hp": 190, "speed": 4.8, "damage": 14, "cooldown": 0.25, "ammo": 120, "vision": 16, "range": 13},
}


def arena_map(name="foundry"):
    if name not in ("foundry", "crossroads"):
        raise ValueError("Unknown map")
    obstacles = ([{"x": 12, "y": 5, "w": 2, "h": 5}, {"x": 26, "y": 14, "w": 2, "h": 5},
                  {"x": 17, "y": 14, "w": 6, "h": 1.8}, {"x": 17, "y": 8.2, "w": 6, "h": 1.8}]
                 if name == "foundry" else
                 [{"x": 11, "y": 8, "w": 3, "h": 8}, {"x": 26, "y": 8, "w": 3, "h": 8},
                  {"x": 18, "y": 3, "w": 4, "h": 3}, {"x": 18, "y": 18, "w": 4, "h": 3}])
    return {"name": name, "width": 40, "height": 24, "obstacles": obstacles,
            "zones": [{"id": "center", "x": 20, "y": 12, "radius": 2.5},
                      {"id": "north", "x": 20, "y": 4, "radius": 2},
                      {"id": "south", "x": 20, "y": 20, "radius": 2}],
            "terrain": [{"x": 6, "y": 9, "w": 3, "h": 6, "speed_multiplier": 0.65},
                        {"x": 31, "y": 9, "w": 3, "h": 6, "speed_multiplier": 0.65}]}


def segment_box(x, y, dx, dy, box, padding=0.0):
    """First t in [0, 1] where a segment intersects an AABB."""
    lo, hi = 0.0, 1.0
    for p, d, a, b in ((x, dx, box["x"] - padding, box["x"] + box["w"] + padding),
                       (y, dy, box["y"] - padding, box["y"] + box["h"] + padding)):
        if abs(d) < 1e-10:
            if p < a or p > b:
                return None
        else:
            aa, bb = sorted(((a - p) / d, (b - p) / d))
            lo, hi = max(lo, aa), min(hi, bb)
        if lo > hi:
            return None
    return lo


def segment_circle(x, y, dx, dy, cx, cy, radius):
    ox, oy = x - cx, y - cy
    c = ox * ox + oy * oy - radius * radius
    if c <= 0:
        return 0.0
    a, b = dx * dx + dy * dy, 2 * (ox * dx + oy * dy)
    discriminant = b * b - 4 * a * c
    if a < 1e-12 or discriminant < 0:
        return None
    t = (-b - math.sqrt(discriminant)) / (2 * a)
    return t if 0 <= t <= 1 else None


@dataclass
class Robot:
    id: str
    slot: int
    team: str
    role: str
    x: float
    y: float
    hp: float
    max_hp: int
    ammo: int
    aim: float
    heading: float = 0.0
    radius: float = 0.48
    cooldown: float = 0.0
    skill_cooldown: float = 0.0
    shield: float = 0.0
    level: int = 1
    kills: int = 0
    damage: float = 0.0
    alive: bool = True


class Match:
    def __init__(self, seed=42, duration=180, map_name="foundry"):
        if not 1 <= duration <= 600:
            raise ValueError("duration must be in [1, 600]")
        self.seed, self.duration, self.arena = int(seed), duration, arena_map(map_name)
        self.rng = random.Random(seed)
        self.tick, self.status, self.winner, self.reason = 0, "ready", None, None
        self.robots = []
        for team in ("red", "blue"):
            for slot, role in enumerate(ROLES, 1):
                spec = SPECS[role]
                self.robots.append(Robot(f"{team}-{slot}", slot, team, role,
                                         3.5 if team == "red" else 36.5, 3 + (slot - 1) * 3,
                                         spec["hp"], spec["hp"], spec["ammo"], 0 if team == "red" else math.pi))
        self.bases = {"red": {"x": 1.6, "y": 12, "hp": 1500, "max_hp": 1500, "radius": 1.25},
                      "blue": {"x": 38.4, "y": 12, "hp": 1500, "max_hp": 1500, "radius": 1.25}}
        self.teams = {team: {"score": 0.0, "resources": 0.0, "kills": 0} for team in ("red", "blue")}
        self.zones = {z["id"]: {"owner": None, "progress": 0.0} for z in self.arena["zones"]}
        self.projectiles, self.events = [], []
        self.next_projectile = 1

    @property
    def time(self):
        return round(self.tick * DT, 4)

    def visible(self, a, b):
        if math.hypot(a.x - b.x, a.y - b.y) > SPECS[a.role]["vision"]:
            return False
        return all(segment_box(a.x, a.y, b.x - a.x, b.y - a.y, w) is None for w in self.arena["obstacles"])

    def observation(self, team, slots=None):
        friends = [r for r in self.robots if r.team == team]
        observers = [r for r in friends if r.alive and (slots is None or r.slot in slots)]
        # Team mode shares all seven fields of view; robot mode sees only its own.
        enemies = [{k: v for k, v in asdict(r).items() if k in
                    ("id", "team", "slot", "role", "x", "y", "hp", "max_hp", "alive", "radius")}
                   for r in self.robots if r.team != team and r.alive and any(self.visible(a, r) for a in observers)]
        visible_bullets = [p for p in self.projectiles if p["team"] == team or any(
            math.hypot(a.x - p["x"], a.y - p["y"]) < SPECS[a.role]["vision"] and
            all(segment_box(a.x, a.y, p["x"] - a.x, p["y"] - a.y, w) is None for w in self.arena["obstacles"])
            for a in observers)]
        return {"api_version": 1, "seed": self.seed, "tick": self.tick, "time": self.time, "dt": DT,
                "remaining": max(0, self.duration - self.time), "team": team,
                "controlled_ids": [str(r.slot) for r in friends if slots is None or r.slot in slots],
                "robots": [asdict(r) for r in friends], "enemies": enemies,
                "arena": copy.deepcopy(self.arena), "bases": copy.deepcopy(self.bases),
                "teams": copy.deepcopy(self.teams), "zones": copy.deepcopy(self.zones),
                "projectiles": copy.deepcopy(visible_bullets)}

    def event(self, kind, **fields):
        self.events.append({"type": kind, "tick": self.tick, **fields})

    def normalize(self, raw, robot):
        idle = {"move": [0.0, 0.0], "aim": robot.aim, "fire": False, "skill": False}
        if not isinstance(raw, dict):
            self.event("invalid_action", robot=robot.id, reason="action must be an object")
            return idle
        def number(v):
            if not isinstance(v, (int, float)) or isinstance(v, bool):
                return False
            try:
                return math.isfinite(v)
            except (OverflowError, ValueError):
                return False
        move = raw.get("move", [0, 0])
        if not isinstance(move, (list, tuple)) or len(move) != 2 or not all(number(v) for v in move):
            self.event("invalid_action", robot=robot.id, reason="move must contain two finite numbers")
        else:
            vx, vy = [max(-1.0, min(1.0, v)) for v in move]
            magnitude = max(1.0, math.hypot(vx, vy))
            idle["move"] = [vx / magnitude, vy / magnitude]
        aim = raw.get("aim")
        if aim is not None:
            if number(aim):
                idle["aim"] = aim % math.tau
            else:
                self.event("invalid_action", robot=robot.id, reason="aim must be finite")
        for flag in ("fire", "skill"):
            if type(raw.get(flag, False)) is bool:
                idle[flag] = raw.get(flag, False)
            else:
                self.event("invalid_action", robot=robot.id, reason=f"{flag} must be boolean")
        return idle

    def blocked(self, x, y, radius):
        if not radius <= x <= self.arena["width"] - radius or not radius <= y <= self.arena["height"] - radius:
            return True
        for w in self.arena["obstacles"]:
            cx = max(w["x"], min(x, w["x"] + w["w"]))
            cy = max(w["y"], min(y, w["y"] + w["h"]))
            if math.hypot(x - cx, y - cy) < radius - 1e-8:
                return True
        return any(math.hypot(x - b["x"], y - b["y"]) < radius + b["radius"] for b in self.bases.values())

    def damage_target(self, target, amount, source):
        if isinstance(target, Robot):
            amount *= 0.4 if target.shield > 0 else 1
            actual = min(target.hp, amount)
            target.hp = max(0, target.hp - amount)
            self.event("hit", x=target.x, y=target.y, robot=target.id, source=source.id, team=source.team, damage=round(actual, 2))
            if target.hp <= 0 and target.alive:
                target.alive = False
                source.kills += 1
                self.teams[source.team]["kills"] += 1
                self.teams[source.team]["score"] += 100
                self.teams[source.team]["resources"] += 25
                self.event("destroy", x=target.x, y=target.y, robot=target.id, killer=source.id)
        else:
            actual = min(target["hp"], amount)
            target["hp"] = max(0, target["hp"] - amount)
            self.event("base_hit", x=target["x"], y=target["y"], team=source.team, robot=source.id, damage=round(actual, 2))
        source.damage += actual
        self.teams[source.team]["score"] += actual * 0.05

    def step(self, commands):
        if self.status == "finished":
            return self.snapshot()
        self.status, self.events = "running", []
        actions = {}
        for team in ("red", "blue"):
            team_actions = commands.get(team, {}) if isinstance(commands, dict) else {}
            if not isinstance(team_actions, dict):
                team_actions = {}
                self.event("invalid_action", team=team, reason="actions must be keyed by slot")
            for key in team_actions:
                if key not in {str(i) for i in range(1, 8)}:
                    self.event("invalid_action", team=team, reason=f"uncontrolled slot: {str(key)[:32]}")
            for r in self.robots:
                if r.team == team:
                    actions[r.id] = self.normalize(team_actions.get(str(r.slot), {}), r)
        old = {r.id: (r.x, r.y) for r in self.robots}
        for r in self.robots:
            if not r.alive:
                continue
            r.cooldown = max(0, r.cooldown - DT)
            r.skill_cooldown = max(0, r.skill_cooldown - DT)
            r.shield = max(0, r.shield - DT)
            a = actions[r.id]
            r.aim = a["aim"]
            speed = SPECS[r.role]["speed"]
            for t in self.arena["terrain"]:
                if t["x"] <= r.x <= t["x"] + t["w"] and t["y"] <= r.y <= t["y"] + t["h"]:
                    speed *= t["speed_multiplier"]
            dx, dy = [v * speed * DT for v in a["move"]]
            if abs(dx) + abs(dy) > 1e-8:
                r.heading = math.atan2(dy, dx)
            # Substeps keep movement inside thin geometry even if speed changes.
            count = max(1, math.ceil(math.hypot(dx, dy) / (r.radius * 0.5)))
            for _ in range(count):
                if not self.blocked(r.x + dx / count, r.y, r.radius):
                    r.x += dx / count
                if not self.blocked(r.x, r.y + dy / count, r.radius):
                    r.y += dy / count
        # Resolve both moving circles at once. Never use team update order as priority.
        for _ in range(12):
            collided = False
            for i, a in enumerate(self.robots):
                for b in self.robots[i + 1:]:
                    if not a.alive or not b.alive:
                        continue
                    dx, dy = b.x - a.x, b.y - a.y
                    dist, needed = math.hypot(dx, dy), a.radius + b.radius
                    if dist < needed - 1e-7:
                        collided = True
                        ux, uy = (dx / dist, dy / dist) if dist > 1e-8 else (0.0, 1.0)
                        push = (needed - dist + 1e-6) / 2
                        for r, sign in ((a, -1), (b, 1)):
                            xx, yy = r.x + ux * push * sign, r.y + uy * push * sign
                            if not self.blocked(xx, yy, r.radius):
                                r.x, r.y = xx, yy
            if not collided:
                break
        # If a crowded wall still leaves overlap, revert movement for involved robots.
        conflicts = set()
        for i, a in enumerate(self.robots):
            for b in self.robots[i + 1:]:
                if a.alive and b.alive and math.hypot(a.x - b.x, a.y - b.y) < a.radius + b.radius - 1e-5:
                    conflicts.update((a.id, b.id))
        if conflicts:
            # Expand to anyone overlapping a reverted position, until stable.
            for _ in range(len(self.robots)):
                for r in self.robots:
                    if r.id in conflicts:
                        r.x, r.y = old[r.id]
                before = len(conflicts)
                for i, a in enumerate(self.robots):
                    for b in self.robots[i + 1:]:
                        if a.alive and b.alive and math.hypot(a.x - b.x, a.y - b.y) < a.radius + b.radius - 1e-5:
                            conflicts.update((a.id, b.id))
                if len(conflicts) == before:
                    break
        for r in self.robots:
            if not r.alive:
                continue
            a, spec = actions[r.id], SPECS[r.role]
            if a["skill"] and r.skill_cooldown <= 1e-8:
                r.skill_cooldown = 15.0
                if r.role == "engineer":
                    for friend in self.robots:
                        if friend.alive and friend.team == r.team and math.hypot(r.x - friend.x, r.y - friend.y) <= 3:
                            friend.hp = min(friend.max_hp, friend.hp + 45)
                else:
                    r.shield = 2.5
                self.event("skill", x=r.x, y=r.y, robot=r.id, team=r.team)
            base = self.bases[r.team]
            if math.hypot(r.x - base["x"], r.y - base["y"]) < 4:
                r.hp = min(r.max_hp, r.hp + 8 * DT)
                if self.tick % 10 == 0:
                    r.ammo = min(spec["ammo"], r.ammo + 5)
            if a["fire"] and r.cooldown <= 1e-8 and r.ammo > 0:
                r.ammo -= 1
                r.cooldown = spec["cooldown"]
                angle = r.aim + self.rng.uniform(-0.015, 0.015)
                vx, vy = math.cos(angle) * 22, math.sin(angle) * 22
                self.projectiles.append({"id": self.next_projectile, "source": r.id, "team": r.team,
                                         "x": r.x + math.cos(angle) * 0.65, "y": r.y + math.sin(angle) * 0.65,
                                         "vx": vx, "vy": vy, "remaining": spec["range"],
                                         "damage": spec["damage"] * (1 + 0.15 * (r.level - 1))})
                self.next_projectile += 1
                self.event("shot", x=r.x, y=r.y, angle=angle, robot=r.id, team=r.team, role=r.role)
        living_projectiles = []
        by_id = {r.id: r for r in self.robots}
        for p in self.projectiles:
            length = min(22 * DT, p["remaining"])
            dx, dy = p["vx"] / 22 * length, p["vy"] / 22 * length
            hits = []
            for w in self.arena["obstacles"]:
                t = segment_box(p["x"], p["y"], dx, dy, w)
                if t is not None:
                    hits.append((t, 0, "wall", None))
            for r in self.robots:
                if r.alive and r.team != p["team"]:
                    t = segment_circle(p["x"], p["y"], dx, dy, r.x, r.y, r.radius)
                    if t is not None:
                        hits.append((t, 1, r.id, r))
            enemy = "blue" if p["team"] == "red" else "red"
            b = self.bases[enemy]
            t = segment_circle(p["x"], p["y"], dx, dy, b["x"], b["y"], b["radius"])
            if t is not None and b["hp"] > 0:
                hits.append((t, 2, enemy, b))
            if hits:
                t, _, _, target = min(hits, key=lambda h: h[:3])
                if target is not None:
                    self.damage_target(target, p["damage"], by_id[p["source"]])
                else:
                    self.event("impact", x=p["x"] + dx * t, y=p["y"] + dy * t)
                continue
            p["x"] += dx
            p["y"] += dy
            p["remaining"] -= length
            if p["remaining"] > 0 and 0 <= p["x"] <= 40 and 0 <= p["y"] <= 24:
                living_projectiles.append(p)
        self.projectiles = living_projectiles
        for z in self.arena["zones"]:
            occupants = {r.team for r in self.robots if r.alive and math.hypot(r.x - z["x"], r.y - z["y"]) <= z["radius"]}
            state = self.zones[z["id"]]
            delta = DT / 3
            if len(occupants) == 1:
                team = next(iter(occupants))
                state["progress"] = max(-1.0, min(1.0, state["progress"] + (delta if team == "red" else -delta)))
                owner = "red" if state["progress"] >= 1 - 1e-8 else "blue" if state["progress"] <= -1 + 1e-8 else None
                if owner != state["owner"]:
                    state["owner"] = owner
                    self.event("capture", zone=z["id"], team=owner, x=z["x"], y=z["y"])
            if state["owner"] and len(occupants) < 2:
                team = state["owner"]
                self.teams[team]["score"] += 4 * DT
                self.teams[team]["resources"] += 3 * DT
        # Shared economy auto-spends 60 resources on the lowest-level living robot.
        for team in ("red", "blue"):
            candidates = sorted((r for r in self.robots if r.team == team and r.alive and r.level < 3), key=lambda r: (r.level, r.slot))
            if self.teams[team]["resources"] >= 60 and candidates:
                self.teams[team]["resources"] -= 60
                candidates[0].level += 1
                self.event("upgrade", robot=candidates[0].id, team=team)
        self.tick += 1
        destroyed = [team for team in ("red", "blue") if self.bases[team]["hp"] <= 0]
        eliminated = [team for team in ("red", "blue") if not any(r.alive and r.team == team for r in self.robots)]
        if destroyed or eliminated:
            losers = destroyed or eliminated
            self.finish(None if len(losers) == 2 else ("blue" if losers[0] == "red" else "red"), "base_destroyed" if destroyed else "elimination")
        elif self.time >= self.duration:
            # Public tiebreak order: score, base HP, surviving robot HP.
            metrics = {team: (round(self.teams[team]["score"], 6), self.bases[team]["hp"],
                              sum(r.hp for r in self.robots if r.team == team)) for team in ("red", "blue")}
            self.finish(None if metrics["red"] == metrics["blue"] else max(metrics, key=metrics.get), "timeout")
        return self.snapshot()

    def finish(self, winner, reason):
        self.status, self.winner, self.reason = "finished", winner, reason
        self.event("finish", winner=winner, reason=reason)

    def snapshot(self):
        return copy.deepcopy({"tick": self.tick, "time": self.time, "remaining": max(0, self.duration - self.time),
                              "duration": self.duration, "seed": self.seed, "status": self.status,
                              "winner": self.winner, "reason": self.reason, "arena": self.arena,
                              "robots": [asdict(r) for r in self.robots], "bases": self.bases, "teams": self.teams,
                              "zones": self.zones, "projectiles": self.projectiles, "events": self.events})

    def digest(self):
        payload = json.dumps(self.snapshot(), sort_keys=True, separators=(",", ":"), allow_nan=False)
        return hashlib.sha256(payload.encode()).hexdigest()
