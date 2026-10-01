"""Territory game (圈地) rules engine.

Pure Python, no dependencies, fully deterministic. This file is the single
source of truth for the rules; RULES.md describes the same behaviour in prose.
"""
import random

DIRS = {'U': (0, -1), 'D': (0, 1), 'L': (-1, 0), 'R': (1, 0)}
OPPOSITE = {'U': 'D', 'D': 'U', 'L': 'R', 'R': 'L'}

DEFAULT_CONFIG = {
    'width': 40,
    'height': 40,
    'max_turns': 600,
    'start_radius': 2,      # initial territory: (2r+1)x(2r+1) square around spawn
    'protect_radius': 1,    # protected zone: (2r+1)x(2r+1) square, can never be captured
    'spawn_margin': 4,      # min distance from spawn to the border
    'min_spawn_dist': 16,   # min Manhattan distance between any two spawns
    'players': 2,
}

# 4 人混战：正方形棋盘，四个出生点互为 90° 旋转
MELEE_CONFIG = dict(DEFAULT_CONFIG, width=48, height=48, players=4, spawn_margin=5)

ROT90 = {'U': 'R', 'R': 'D', 'D': 'L', 'L': 'U'}  # clockwise


def config_for(players):
    return MELEE_CONFIG if players == 4 else DEFAULT_CONFIG


def make_start(seed, cfg=DEFAULT_CONFIG):
    """Spawns and initial directions for a seed.
    2 players: player 1 is player 0 rotated 180 degrees.
    4 players: player k is player 0 rotated k x 90 degrees clockwise (square board)."""
    rng = random.Random(seed)
    W, H, m = cfg['width'], cfg['height'], cfg['spawn_margin']
    if cfg.get('players', 2) == 4:
        assert W == H
        while True:
            x, y = rng.randint(m, W - 1 - m), rng.randint(m, H - 1 - m)
            pts = [(x, y)]
            for _ in range(3):
                px, py = pts[-1]
                pts.append((W - 1 - py, px))
            if all(abs(a[0] - b[0]) + abs(a[1] - b[1]) >= cfg['min_spawn_dist']
                   for i, a in enumerate(pts) for b in pts[i + 1:]):
                break
        d = rng.choice('UDLR')
        dirs = [d]
        for _ in range(3):
            dirs.append(ROT90[dirs[-1]])
        return pts, dirs
    while True:
        x, y = rng.randint(m, W - 1 - m), rng.randint(m, H - 1 - m)
        x2, y2 = W - 1 - x, H - 1 - y
        if abs(x - x2) + abs(y - y2) >= cfg['min_spawn_dist']:
            break
    d = rng.choice('UDLR')
    return [(x, y), (x2, y2)], [d, OPPOSITE[d]]


class Game:
    def __init__(self, cfg, spawns, dirs):
        self.cfg = cfg
        self.W, self.H = cfg['width'], cfg['height']
        self.N = len(spawns)
        n = self.W * self.H
        self.owner = [-1] * n       # territory owner per cell
        self.trail = [-1] * n       # trail owner per cell
        self.protected = [-1] * n   # protected-zone owner per cell
        self.spawns = list(spawns)
        self.init_dirs = list(dirs)
        self.pos = list(spawns)
        self.dir = list(dirs)
        self.trails = [[] for _ in range(self.N)]  # ordered trail cell indices
        self.deaths = [0] * self.N
        self.kills = [0] * self.N
        self.out = [False] * self.N  # 被淘汰（程序崩溃或错误过多）的玩家：停在出生点，不再行动
        self.turn = 0
        for i, (sx, sy) in enumerate(spawns):
            r = cfg['start_radius']
            for y in range(sy - r, sy + r + 1):
                for x in range(sx - r, sx + r + 1):
                    self.owner[y * self.W + x] = i
            r = cfg['protect_radius']
            for y in range(sy - r, sy + r + 1):
                for x in range(sx - r, sx + r + 1):
                    self.protected[y * self.W + x] = i

    def over(self):
        return self.turn >= self.cfg['max_turns']

    def areas(self):
        a = [0] * self.N
        for o in self.owner:
            if o >= 0:
                a[o] += 1
        return a

    def eliminate(self, i):
        """Remove a forfeited player from play (melee only): its trail is cleared, it stays
        on its spawn and never moves again. Its territory stays on the board."""
        for k in self.trails[i]:
            self.trail[k] = -1
        self.trails[i] = []
        self.pos[i] = self.spawns[i]
        self.out[i] = True

    def _enclosed(self, i):
        """Cells not owned by i and not on i's trail that cannot reach the border
        without crossing i's territory or trail."""
        W, H = self.W, self.H
        owner, trail = self.owner, self.trail
        wall = bytearray(W * H)
        for k in range(W * H):
            if owner[k] == i or trail[k] == i:
                wall[k] = 1
        seen = bytearray(W * H)
        stack = []
        for x in range(W):
            for k in (x, (H - 1) * W + x):
                if not wall[k] and not seen[k]:
                    seen[k] = 1
                    stack.append(k)
        for y in range(H):
            for k in (y * W, y * W + W - 1):
                if not wall[k] and not seen[k]:
                    seen[k] = 1
                    stack.append(k)
        while stack:
            k = stack.pop()
            x = k % W
            for nk, ok in ((k - W, k >= W), (k + W, k < W * (H - 1)),
                           (k - 1, x > 0), (k + 1, x < W - 1)):
                if ok and not wall[nk] and not seen[nk]:
                    seen[nk] = 1
                    stack.append(nk)
        return [k for k in range(W * H) if not wall[k] and not seen[k]]

    def step(self, moves):
        """Advance one turn. moves[i] is 'U'/'D'/'L'/'R' or None (keep direction).
        Returns a frame dict describing what changed (used for replays)."""
        W, H, N = self.W, self.H, self.N
        dirs, new = [], []
        dead = [None] * N  # (reason, other_player)

        # 1. intended moves; reversing or invalid input keeps the current direction
        for i in range(N):
            d = self.dir[i]
            m = moves[i]
            if self.out[i]:
                dirs.append(d)
                new.append(None)
                continue
            if m in DIRS and m != OPPOSITE[d]:
                d = m
            dirs.append(d)
            x, y = self.pos[i]
            dx, dy = DIRS[d]
            nx, ny = x + dx, y + dy
            if 0 <= nx < W and 0 <= ny < H:
                new.append(ny * W + nx)
            else:
                new.append(None)
                dead[i] = ('wall', None)

        # 2. head-on collisions (same target cell, or swapping cells)
        cur = [py * W + px for px, py in self.pos]
        for i in range(N):
            for j in range(i + 1, N):
                if new[i] is None or new[j] is None:
                    continue
                if new[i] == new[j] or (new[i] == cur[j] and new[j] == cur[i]):
                    dead[i] = dead[i] or ('head', j)
                    dead[j] = dead[j] or ('head', i)

        # 3. trail cuts: entering a trail cell kills the trail's owner
        for i in range(N):
            if new[i] is None:
                continue
            c = self.trail[new[i]]
            if c != -1 and dead[c] is None:
                dead[c] = ('self', i) if c == i else ('cut', i)

        frame = {'events': [], 'owner': [], 'trail_clear': [], 'trail_add': []}

        # 4. deaths: drop trail, teleport to spawn, keep territory
        for i in range(N):
            if dead[i] is None:
                continue
            for k in self.trails[i]:
                self.trail[k] = -1
            self.trails[i] = []
            self.pos[i] = self.spawns[i]
            self.dir[i] = self.init_dirs[i]
            self.deaths[i] += 1
            reason, by = dead[i]
            if reason == 'cut':
                self.kills[by] += 1
            frame['events'].append({'type': 'death', 'player': i, 'reason': reason, 'by': by})
            frame['trail_clear'].append(i)

        # 5. survivors move
        capturers = []
        for i in range(N):
            if dead[i] is not None or self.out[i]:
                continue
            k = new[i]
            self.dir[i] = dirs[i]
            self.pos[i] = (k % W, k // W)
            if self.owner[k] == i:
                if self.trails[i]:
                    capturers.append(i)
            else:
                self.trails[i].append(k)
                self.trail[k] = i
                frame['trail_add'].append([i, k])

        # 6. captures, computed on the pre-capture board and applied together
        if capturers:
            enclosed = {i: self._enclosed(i) for i in capturers}
            trail_cells = {}
            for i in capturers:
                for k in self.trails[i]:
                    trail_cells[k] = i
            claims = {}
            for i in capturers:
                for k in enclosed[i]:
                    claims.setdefault(k, []).append(i)
            gained = {i: 0 for i in capturers}

            def take(k, i):
                if self.protected[k] not in (-1, i) or self.owner[k] == i:
                    return
                self.owner[k] = i
                gained[i] += 1
                frame['owner'].append([k, i])

            for k, i in trail_cells.items():
                take(k, i)
            for k, cl in claims.items():
                if len(cl) == 1 and k not in trail_cells:
                    take(k, cl[0])
            for i in capturers:
                for k in self.trails[i]:
                    self.trail[k] = -1
                self.trails[i] = []
                frame['trail_clear'].append(i)
                frame['events'].append({'type': 'capture', 'player': i, 'cells': gained[i]})

        self.turn += 1
        frame['pos'] = [[x, y, d] for (x, y), d in zip(self.pos, self.dir)]
        return frame

    def state_text(self, turn):
        """Per-turn message sent to bots (see RULES.md)."""
        areas = self.areas()
        lines = ['TURN %d' % turn]
        for i in range(self.N):
            x, y = self.pos[i]
            lines.append('P %d %d %d %s %d %d %d' % (
                i, x, y, self.dir[i], len(self.trails[i]), areas[i], self.deaths[i]))
        W = self.W
        ch = '.0123456789'
        lines.append('OWNER')
        row = ''.join(ch[o + 1] for o in self.owner)
        lines.extend(row[y * W:(y + 1) * W] for y in range(self.H))
        lines.append('TRAIL')
        row = ''.join(ch[o + 1] for o in self.trail)
        lines.extend(row[y * W:(y + 1) * W] for y in range(self.H))
        lines.append('END')
        return '\n'.join(lines) + '\n'
