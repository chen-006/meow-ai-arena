"""Bomb Arena v0.1. Pure deterministic public rules engine; Python 3.10+."""
from __future__ import annotations
from collections import deque
from dataclasses import dataclass, asdict
import copy
import random
import math
from fractions import Fraction

VERSION = 'bomb-arena-0.5-public-state'
DIRS = {'U': (0,-1), 'D': (0,1), 'L': (-1,0), 'R': (1,0), 'S': (0,0)}

@dataclass(frozen=True)
class Config:
    size: int = 13
    players: int = 2
    max_turns: int = 300
    shrink_start: int = 150
    shrink_every: int = 20
    fuse: int = 4
    radius: int = 3
    capacity: int = 2
    flame_turns: int = 2

def config_for(n):
    if not 2 <= n <= 10: raise ValueError('players must be between 2 and 10')
    if n == 2: return Config()
    if n == 4: return Config(17,4,450,250,25)
    size = 2*math.ceil(math.sqrt(14*n))+1
    start = 150+25*(n-2)
    every = 20 if n <= 3 else 25
    return Config(size,n,start+every*(size//2)+25,start,every)

def make_map(seed, cfg):
    n = cfg.size
    board = [['.' for _ in range(n)] for _ in range(n)]
    for y in range(n):
        for x in range(n):
            if x in (0,n-1) or y in (0,n-1) or (x%2 == 0 and y%2 == 0):
                board[y][x] = '#'
    rng = random.Random(seed)
    if cfg.players == 2: spawns = [(1,1),(n-2,n-2)]
    elif cfg.players == 4: spawns = [(1,1),(n-2,1),(n-2,n-2),(1,n-2)]
    else:
        # Equally spaced along the perimeter's odd-coordinate cells. Corner/edge
        # positions need seat rotation across matches; do not claim exact symmetry.
        ring = ([(x,1) for x in range(1,n-2,2)] + [(n-2,y) for y in range(1,n-2,2)]
                + [(x,n-2) for x in range(n-2,1,-2)] + [(1,y) for y in range(n-2,1,-2)])
        offset = rng.randrange(len(ring))
        spawns = [ring[(offset+(i*len(ring)+cfg.players//2)//cfg.players)%len(ring)] for i in range(cfg.players)]
    safe = {(x,y) for sx,sy in spawns for y in range(n) for x in range(n)
            if abs(x-sx)+abs(y-sy) <= 2}
    used = set()
    for y in range(1,n-1):
        for x in range(1,n-1):
            if (x,y) in used or board[y][x] == '#': continue
            orbit = {(x,y)}
            if cfg.players in (2,4): orbit.add((n-1-x,n-1-y))
            if cfg.players == 4: orbit |= {(n-1-y,x),(y,n-1-x)}
            used |= orbit
            crate = rng.random() < .42
            if crate and not (orbit & safe):
                for xx,yy in orbit: board[yy][xx] = '+'
    return board, spawns


def resolve_moves(origins, targets, allow_swap=True):
    """Alive players only, terrain already applied. No player-order priority."""
    if len(set(origins))!=len(origins):raise ValueError('exclusive mode requires unique starting cells')
    result=list(targets)
    counts={p:targets.count(p) for p in targets}
    for i,p in enumerate(targets):
        if counts[p]>1:result[i]=origins[i]
    if not allow_swap:
        for i in range(len(origins)):
            for j in range(i):
                if result[i]==origins[j] and result[j]==origins[i]:result[i],result[j]=origins[i],origins[j]
    while True:
        stopped={origins[i] for i in range(len(origins)) if result[i]==origins[i]}
        updated=[origins[i] if result[i] in stopped else result[i] for i in range(len(origins))]
        if updated==result:break
        result=updated
    assert len(set(result))==len(result)
    return result

class Game:
    move_mode="swap"
    def __init__(self, seed=0, cfg=None):
        self.cfg = cfg or Config()
        self.seed, self.turn, self.next_bomb = seed, 0, 0
        self.board, spawns = make_map(seed, self.cfg)
        self.players = [dict(x=x,y=y,alive=True,death_turn=None,reason='',bombs=0,crates=0)
                        for x,y in spawns]
        self.bombs = [] # id, owner, x, y, explode_at (absolute zero-based action turn)
        self.flames = {} # (x,y) -> last lethal action turn, inclusive
        self.events = []
        self.flame_sources = {}
        self.kill_credit = [Fraction(0) for _ in self.players]

    def clone(self): return copy.deepcopy(self)
    def over(self):
        return self.turn >= self.cfg.max_turns or sum(p['alive'] for p in self.players) <= 1

    def _kill(self, i, reason):
        p = self.players[i]
        if p['alive']:
            p.update(alive=False, death_turn=self.turn, reason=reason)
            self.events.append(dict(type='death',player=i,reason=reason,turn=self.turn))
            if reason=='blast':
                owners=sorted(owner for owner,expiry in self.flame_sources.get((p['x'],p['y']),{}).items() if expiry>=self.turn and owner!=i)
                if owners:
                    share=Fraction(1,len(owners))
                    for owner in owners:self.kill_credit[owner]+=share
                    self.events.append(dict(type='kill_credit',victim=i,owners=owners,share=float(share)))

    def shrink_layer(self, t=None):
        t = self.turn if t is None else t
        return 0 if t < self.cfg.shrink_start else 1+(t-self.cfg.shrink_start)//self.cfg.shrink_every

    def step(self, actions, forfeits=None):
        if self.over(): raise RuntimeError('game already finished')
        if len(actions) != self.cfg.players: raise ValueError('one action per player')
        self.events = []
        t, n = self.turn, self.cfg.size
        # 1. Expire flames, simultaneous forfeits, then deterministic shrink.
        self.flames = {k:v for k,v in self.flames.items() if v >= t}
        for i in forfeits or []: self._kill(i, 'forfeit')
        layer = self.shrink_layer()
        if t >= self.cfg.shrink_start and (t-self.cfg.shrink_start)%self.cfg.shrink_every == 0:
            for y in range(n):
                for x in range(n):
                    if min(x,y,n-1-x,n-1-y) <= layer:
                        self.board[y][x] = '#'
                        self.flames.pop((x,y), None)
            self.bombs = [b for b in self.bombs if self.board[b['y']][b['x']] != '#']
            for i,p in enumerate(self.players):
                if self.board[p['y']][p['x']] == '#': self._kill(i, 'shrink')
            self.events.append(dict(type='shrink',layer=layer))
        # 2. Place at the pre-move position. Co-located simultaneous requests:
        # exactly one requester succeeds; >=2 requesters all fail (no seat priority).
        occupied = {(b['x'],b['y']) for b in self.bombs}
        requests = {}
        for i,(p,a) in enumerate(zip(self.players, actions)):
            if not p['alive']: continue
            d,place = a
            if d not in DIRS or place not in (0,1): raise ValueError('invalid action')
            cell = (p['x'],p['y'])
            count = sum(b['owner']==i for b in self.bombs)
            if place and cell not in occupied and count < self.cfg.capacity:
                requests.setdefault(cell,[]).append(i)
        for (x,y), ids in sorted(requests.items()):
            if len(ids) != 1: continue
            i = ids[0]
            self.bombs.append(dict(id=self.next_bomb,owner=i,x=x,y=y,explode_at=t+self.cfg.fuse))
            self.next_bomb += 1
            self.players[i]['bombs'] += 1
            self.events.append(dict(type='place',player=i,x=x,y=y))
            occupied.add((x,y))
        # Terrain and all destinations are computed before resolving player conflicts.
        ids=[i for i,p in enumerate(self.players) if p['alive']]
        origins=[(self.players[i]['x'],self.players[i]['y']) for i in ids];targets=[]
        for i,old in zip(ids,origins):
            dx,dy=DIRS[actions[i][0]];x,y=old[0]+dx,old[1]+dy
            targets.append((x,y) if 0<=x<n and 0<=y<n and self.board[y][x]=='.' and ((x,y)==old or (x,y) not in occupied) else old)
        destinations=targets if self.move_mode=='overlap' else resolve_moves(origins,targets,self.move_mode=='swap')
        for i,old,want,dest in zip(ids,origins,targets,destinations):
            self.players[i]['x'],self.players[i]['y']=dest
            if want!=old and dest==old:self.events.append(dict(type='blocked',player=i,target=list(want)))
        # 4. All explosions use ONE pre-explosion crate/bomb snapshot. A hit bomb
        # triggers immediately AND blocks that ray. A hit crate also blocks it.
        at = {(b['x'],b['y']):b for b in self.bombs}
        pending = deque(b for b in self.bombs if b['explode_at']<=t or (b['x'],b['y']) in self.flames)
        exploded, hit, destroyed = set(), set(), {}
        hit_sources = {}
        while pending:
            b = pending.popleft()
            if b['id'] in exploded: continue
            exploded.add(b['id']); hit.add((b['x'],b['y']))
            hit_sources.setdefault((b['x'],b['y']),set()).add(b['owner'])
            self.events.append(dict(type='explode',player=b['owner'],x=b['x'],y=b['y']))
            for dx,dy in list(DIRS.values())[:4]:
                for r in range(1,self.cfg.radius+1):
                    x,y = b['x']+dx*r,b['y']+dy*r
                    if not (0<=x<n and 0<=y<n) or self.board[y][x]=='#': break
                    hit.add((x,y))
                    hit_sources.setdefault((x,y),set()).add(b['owner'])
                    if self.board[y][x]=='+':
                        destroyed.setdefault((x,y),set()).add(b['owner']); break
                    if (x,y) in at:
                        pending.append(at[x,y]); break
        self.bombs = [b for b in self.bombs if b['id'] not in exploded]
        for (x,y), owners in destroyed.items():
            self.board[y][x]='.'
            # A shared crate is credited to every hitting player, display only.
            for i in owners: self.players[i]['crates'] += 1
        for cell in hit: self.flames[cell] = max(self.flames.get(cell,-1),t+self.cfg.flame_turns-1)
        for cell,owners in hit_sources.items():
            provenance=self.flame_sources.setdefault(cell,{})
            for owner in owners:provenance[owner]=max(provenance.get(owner,-1),t+self.cfg.flame_turns-1)
        # 5. Kill on old or newly created flames. A dead owner's bombs remain.
        for i,p in enumerate(self.players):
            if (p['x'],p['y']) in self.flames: self._kill(i,'blast')
        self.turn += 1
        return self.snapshot()

    def survival_ranks(self):
        stamps = [self.cfg.max_turns+1 if p['alive'] else p['death_turn'] for p in self.players]
        return [1+sum(v>s for v in stamps) for s in stamps]

    def score_parts(self):
        ranks = self.survival_ranks()
        survival = [Fraction(self.cfg.players-r)-Fraction(ranks.count(r)-1,2) for r in ranks]
        kills = [k/2 for k in self.kill_credit]
        return survival, kills, [s+k for s,k in zip(survival,kills)]

    def ranks(self):
        points = self.score_parts()[2]
        return [1+sum(other>p for other in points) for p in points]

    def snapshot(self):
        return dict(turn=self.turn,board=[''.join(r) for r in self.board],
                    players=copy.deepcopy(self.players),bombs=copy.deepcopy(self.bombs),
                    flames=[[x,y,e] for (x,y),e in sorted(self.flames.items())],events=copy.deepcopy(self.events))

    def header(self, me):
        c = self.cfg
        return f'INIT {c.size} {c.players} {me} {c.max_turns} {c.fuse} {c.radius} {c.capacity} {c.flame_turns} {c.shrink_start} {c.shrink_every}\n'

    def state_text(self):
        lines = [f'TURN {self.turn}','BOARD']+[''.join(r) for r in self.board]
        for i,p in enumerate(self.players):
            lines.append(f"P {i} {p['x']} {p['y']} {int(p['alive'])}")
        lines.append(f'BOMBS {len(self.bombs)}')
        for b in self.bombs: lines.append(f"B {b['owner']} {b['x']} {b['y']} {b['explode_at']}")
        lines.append(f'FLAMES {len(self.flames)}')
        for (x,y),expiry in sorted(self.flames.items()): lines.append(f'F {x} {y} {expiry}')
        lines.append(f'STATUS {len(self.players)}')
        for i,p in enumerate(self.players):
            k=self.kill_credit[i]
            death=-1 if p['alive'] else p['death_turn']
            lines.append(f'S {i} {death} {k.numerator} {k.denominator}')
        sources=[(x,y,owner,expiry) for (x,y),owners in sorted(self.flame_sources.items())
                 if (x,y) in self.flames and self.flames[x,y]>=self.turn
                 for owner,expiry in sorted(owners.items()) if expiry>=self.turn]
        lines.append(f'SOURCES {len(sources)}')
        for x,y,owner,expiry in sources:lines.append(f'O {x} {y} {owner} {expiry}')
        return '\n'.join(lines+['END',''])

    def result(self):
        survival, kills, points = self.score_parts()
        return dict(kill_credit=list(map(float,self.kill_credit)),
                    survival_ranks=self.survival_ranks(),survival_points=list(map(float,survival)),
                    kill_points=list(map(float,kills)),points=list(map(float,points)),
                    points_exact=list(map(str,points)),kill_credit_exact=list(map(str,self.kill_credit)),
                    seed=self.seed,config=asdict(self.cfg),turns=self.turn,ranks=self.ranks(),
                    survivors=[i for i,p in enumerate(self.players) if p['alive']],players=copy.deepcopy(self.players))
