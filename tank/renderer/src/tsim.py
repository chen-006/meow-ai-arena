"""坦克大战的结算，逐行照 tank.hpp（tank-cpp-0.1）的 step() 移植成 Python，给回放渲染用。
回放（tank-replay-1）只存起始局面文本 start 和每回合双方的命令，这里从头重算每一回合，
同时记下两个小步里每辆车、每发子弹的位置和下场（动画要用，局面文本里没有）。

    st = State.parse(replay['start'])
    tr = st.step(['UU U U', 'DL FD U'])     # 返回这一回合的小步轨迹；st 变成回合末的局面
    st.turn_text()                          # 与 tank.hpp 的 turn_text() 逐字相同
    tr['events']                            # 与 tank.hpp 的 event_text() 逐条相同（顺序也相同）

只实现本届比赛用到的参数组合（第二组参数 base_armor / base_regen / proto_* 都是 0），遇到别的值直接报错。
"""
import copy

DIRS = 'UDLR'
DV = {'U': (0, -1), 'D': (0, 1), 'L': (-1, 0), 'R': (1, 0)}
OPP = {'U': 'D', 'D': 'U', 'L': 'R', 'R': 'L'}
TYPES = ['fast', 'fire', 'armor']
NEVER_FIRED = -99
MAX_BULLETS = 24
REASONS = ['none', 'base', 'hp', 'kills', 'stars', 'damage', 'draw']
TB_NAMES = ['base', 'kills', 'stars', 'damage']


class State:
    @classmethod
    def parse(cls, text):
        s = cls()
        P, rows = {}, text.splitlines()
        k = 0
        assert rows[0].split() == ['TANK-STATE', '1']
        k = 1
        while rows[k].startswith('P '):
            w = rows[k].split()
            P[w[1]] = w[3:3 + int(w[2])] if w[1] == 'tiebreak' else int(w[2])
            k += 1
        for key in ('base_armor', 'base_regen', 'proto_base_dmg_by_bullet_order', 'proto_supply_feeds_tank',
                    'proto_pickup_at_max_level', 'start_level0', 'start_level1'):
            if P.get(key, 0):
                raise ValueError(f'不支持的参数 {key}={P[key]}')
        s.P = P
        s.n = P['size']
        s.types = [dict(hp=P[t + '_hp'], speed=P[t + '_speed'], bullets=P[t + '_bullets'], reload=P[t + '_reload'])
                   for t in TYPES]
        s.layout = '\n'.join(rows[:k + 3]) + '\n'      # 布局段原文（P / BASES / SPAWNS / SUPPLY）
        v = list(map(int, rows[k].split()[1:]))
        s.base_pos = [(v[0], v[1]), (v[2], v[3])]
        v = list(map(int, rows[k + 1].split()[1:]))
        s.spawn = [(v[j], v[j + 1]) for j in range(0, 12, 2)]
        v = list(map(int, rows[k + 2].split()[1:]))
        s.supply = [(v[1 + j], v[2 + j]) for j in range(0, 2 * v[0], 2)]
        s.read_turn(rows[k + 3:])
        return s

    def read_turn(self, rows):
        k = 0
        while k < len(rows):
            w = rows[k].split()
            k += 1
            if not w:
                continue
            h = w[0]
            if h == 'TURN':
                self.turn, self.done, self.winner = int(w[1]), w[2] == '1', int(w[3])
                r = w[4][5:] if w[4].startswith('both_') else w[4]
                self.reason = REASONS.index(r)
            elif h in ('BASEHP', 'BASEQUIET', 'KILLS', 'STARCOUNT', 'BASEDMG'):
                setattr(self, {'BASEHP': 'base_hp', 'BASEQUIET': 'base_quiet', 'KILLS': 'kills',
                               'STARCOUNT': 'stars_eaten', 'BASEDMG': 'base_dmg'}[h], [int(w[1]), int(w[2])])
            elif h == 'MAP':
                self.grid = [list(rows[k + y].strip()) for y in range(self.n)]
                k += self.n
            elif h == 'T':
                pass
            elif h == 'TANKS':
                self.tank = []
                for j in range(int(w[1])):
                    t = list(map(int, rows[k + j].split()[1:]))
                    self.tank.append(dict(x=t[1], y=t[2], hp=t[3], level=t[4], alive=t[5] == 1, respawn_turn=t[6],
                                          last_fire=t[7]))
                k += int(w[1])
            elif h == 'BULLETS':
                self.bullet = []
                for j in range(int(w[1])):
                    b = rows[k + j].split()
                    self.bullet.append(dict(x=int(b[1]), y=int(b[2]), dir=b[3], owner=int(b[4]), steel=b[5] == '1',
                                            level=int(b[6])))
                k += int(w[1])
            elif h == 'STARS':
                self.star = [False] * len(self.supply)
                for j in range(int(w[1])):
                    x, y = map(int, rows[k + j].split())
                    self.star[self.supply.index((x, y))] = True
                k += int(w[1])
            elif h == 'END':
                break

    def copy(self):
        return copy.deepcopy(self)

    # ---------- 查询（同 tank.hpp 第 4 节）----------
    def cell(self, x, y):
        if x < 0 or y < 0 or x >= self.n or y >= self.n:
            return 'X'
        return self.grid[y][x]

    def tank_at(self, x, y):
        for i, t in enumerate(self.tank):
            if t['alive'] and t['x'] == x and t['y'] == y:
                return i
        return -1

    def bullets_out(self, i):
        return sum(1 for b in self.bullet if b['owner'] == i)

    def bullet_cap(self, i):
        tp = self.types[i % 3]
        if self.tank[i]['level'] >= 1 and (not self.P['level1_no_reload'] or tp['reload'] == 0):
            return tp['bullets'] + self.P['level1_bullets']
        return tp['bullets']

    def reload_turns(self, i):
        rl = self.types[i % 3]['reload']
        if rl and self.tank[i]['level'] >= 1 and self.P['level1_no_reload']:
            rl = 0
        return rl

    def reload_left(self, i):
        return max(0, self.reload_turns(i) - (self.turn - self.tank[i]['last_fire']))

    def can_fire(self, i):
        return (self.tank[i]['alive'] and self.reload_left(i) == 0 and self.bullets_out(i) < self.bullet_cap(i)
                and len(self.bullet) < MAX_BULLETS)

    def respawn_in(self, i):
        if self.tank[i]['alive']:
            return 0
        return max(1, self.tank[i]['respawn_turn'] - self.turn)

    # ---------- 文本（同 tank.hpp 的 turn_text）----------
    def turn_text(self):
        both = self.done and self.base_hp[0] <= 0 and self.base_hp[1] <= 0
        reason = (('both_' if both else '') + REASONS[self.reason]) if self.done else 'none'
        out = [f'TURN {self.turn} {1 if self.done else 0} {self.winner} {reason}']
        for word, v in (('BASEHP', self.base_hp), ('BASEQUIET', self.base_quiet), ('KILLS', self.kills),
                        ('STARCOUNT', self.stars_eaten), ('BASEDMG', self.base_dmg)):
            out.append(f'{word} {v[0]} {v[1]}')
        out.append('MAP')
        out += [''.join(r) for r in self.grid]
        out.append('TANKS 6')
        for i, t in enumerate(self.tank):
            x, y = (t['x'], t['y']) if t['alive'] else (-1, -1)
            out.append(f"T {i} {x} {y} {t['hp']} {t['level']} {1 if t['alive'] else 0} {t['respawn_turn']} {t['last_fire']}")
        out.append(f'BULLETS {len(self.bullet)}')
        for b in self.bullet:
            out.append(f"B {b['x']} {b['y']} {b['dir']} {b['owner']} {1 if b['steel'] else 0} {b['level']}")
        st = [p for p, on in zip(self.supply, self.star) if on]
        out.append(f'STARS {len(st)}')
        out += [f'{x} {y}' for x, y in st]
        out.append('END')
        return '\n'.join(out) + '\n'

    # ---------- 结算一回合（同 tank.hpp 的 step）----------
    def step(self, orders):
        """orders = [0 号方命令, 1 号方命令]，每个是 "UU FU S" 这样的三条命令。返回本回合的轨迹（见文件头）。"""
        if self.done:
            raise ValueError('对局已结束')
        P = self.P
        turn = self.turn = self.turn + 1
        ev = []
        cmds = [c for o in orders for c in o.split()]
        assert len(cmds) == 6, orders
        tr = dict(turn=turn, pos=[], bump=[[None, None] for _ in range(6)], mv=[[None, None] for _ in range(6)],
                  fire=[None] * 6, hits=[], base_hits=[], walls=[], cancels=[], bullets=[], ends=[], events=ev,
                  alive0=[t['alive'] for t in self.tank])
        move = [[None, None] for _ in range(6)]
        base_hits_now = [0, 0]
        # 每发子弹的轨迹记录（与 self.bullet 里的项一一对应，按引用跟着走）
        for b in self.bullet:
            b['_tr'] = dict(x0=b['x'], y0=b['y'], dir=b['dir'], owner=b['owner'], steel=b['steel'], fresh=False,
                            end=None, kind=None)
            tr['bullets'].append(b['_tr'])
        # 开炮
        for i in range(6):
            t = self.tank[i]
            if not t['alive']:
                continue
            c = cmds[i]
            if c == 'S':
                continue
            if c[0] == 'F' and len(c) == 2:
                if (self.bullets_out(i) < self.bullet_cap(i) and turn - t['last_fire'] > self.reload_turns(i)
                        and len(self.bullet) < MAX_BULLETS):
                    t['last_fire'] = turn
                    b = dict(x=t['x'], y=t['y'], dir=c[1], owner=i, steel=t['level'] >= 2 and bool(P['level2_steel']),
                             level=t['level'])
                    b['_tr'] = dict(x0=b['x'], y0=b['y'], dir=b['dir'], owner=i, steel=b['steel'], fresh=True,
                                    end=None, kind=None)
                    tr['bullets'].append(b['_tr'])
                    self.bullet.append(b)
                    tr['fire'][i] = c[1]
                    ev.append(f'fire {i} {c[1]}')
            elif len(c) == 1:
                move[i][0] = c
            else:
                move[i][0] = c[0]
                if self.types[i % 3]['speed'] >= 2:
                    move[i][1] = c[1]
        tr['mv'] = [list(m) for m in move]
        tr['pos'] = [[(t['x'], t['y'])] if t['alive'] else None for t in self.tank]

        for sub in (1, 2):
            # (a) 坦克移动
            want = []
            for i in range(6):
                d = move[i][sub - 1]
                if d is None:
                    continue
                t = self.tank[i]
                tx, ty = t['x'] + DV[d][0], t['y'] + DV[d][1]
                if self.cell(tx, ty) == '.' and self.tank_at(tx, ty) < 0:
                    want.append((i, tx, ty))
                else:
                    ev.append(f'blocked {i} {sub}')
                    tr['bump'][i][sub - 1] = d
            moved = []
            for a, (i, tx, ty) in enumerate(want):
                if any(b != a and (w[1], w[2]) == (tx, ty) for b, w in enumerate(want)):
                    ev.append(f'blocked {i} {sub}')
                    tr['bump'][i][sub - 1] = move[i][sub - 1]
                else:
                    moved.append((i, (self.tank[i]['x'], self.tank[i]['y']), (tx, ty)))
            for i, _, (tx, ty) in moved:
                self.tank[i]['x'], self.tank[i]['y'] = tx, ty
            for i, t in enumerate(self.tank):
                if tr['pos'][i] is not None:
                    tr['pos'][i].append((t['x'], t['y']))

            nb = len(self.bullet)
            if nb == 0:
                continue
            bl = self.bullet
            # (b) 子弹前进
            for b in bl:
                b['x'] += DV[b['dir']][0]
                b['y'] += DV[b['dir']][1]
            gone = [False] * nb

            def end(k, kind, back):
                tb = bl[k]['_tr']
                if tb['end'] is None:
                    tb['end'] = sub - back       # 以"格"为单位的飞行距离：本回合飞到 end 格时消失
                    tb['kind'] = kind
                return (sub - back) / 2           # 回合内时刻（0–1）

            # 判定 1：子弹对穿
            if P['bullet_cancel'] and nb > 1:
                for a in range(nb):
                    ba = bl[a]
                    for b in range(a + 1, nb):
                        bb = bl[b]
                        if bb['dir'] == OPP[ba['dir']] and (ba['x'] - DV[ba['dir']][0], ba['y'] - DV[ba['dir']][1]) == (bb['x'], bb['y']):
                            gone[a] = gone[b] = True
                            ev.append(f"cancel cross {ba['x']} {ba['y']} {sub} {ba['owner']} {bb['owner']}")
                            end(a, 'cross', .5)
                            tm = end(b, 'cross', .5)
                            tr['cancels'].append(dict(t=tm, x=(ba['x'] + bb['x']) / 2, y=(ba['y'] + bb['y']) / 2))
            # 判定 1：子弹与坦克互换位置
            for i, (fx, fy), _ in moved:
                t = self.tank[i]
                for k in range(nb):
                    b = bl[k]
                    if not gone[k] and (b['x'], b['y']) == (fx, fy) and \
                            (b['x'] - DV[b['dir']][0], b['y'] - DV[b['dir']][1]) == (t['x'], t['y']):
                        gone[k] = True
                        t['hp'] -= 1
                        ev.append(f"hit {i} {b['owner']} {t['x']} {t['y']} {sub} 1")
                        tm = end(k, 'swap', .5)
                        tr['hits'].append(dict(t=tm, tank=i, by=b['owner'], x=(b['x'] + t['x']) / 2,
                                               y=(b['y'] + t['y']) / 2))
            # 判定 2–5
            brick, steel = [], []
            own_took = [0, 0]
            for k in range(nb):
                if gone[k]:
                    continue
                b = bl[k]
                c = self.cell(b['x'], b['y'])
                if c == '.':
                    v = self.tank_at(b['x'], b['y'])
                    if v >= 0:
                        gone[k] = True
                        self.tank[v]['hp'] -= 1
                        ev.append(f"hit {v} {b['owner']} {b['x']} {b['y']} {sub} 0")
                        tm = end(k, 'hit', .3)
                        tr['hits'].append(dict(t=tm, tank=v, by=b['owner'], x=b['x'], y=b['y']))
                elif c == '~':
                    pass
                elif c == '#':
                    gone[k] = True
                    brick.append((b['x'], b['y']))
                    end(k, 'brick', .45)
                elif c == '@':
                    gone[k] = True
                    if b['steel']:
                        steel.append((b['x'], b['y']))
                    end(k, 'steel' if b['steel'] else 'ding', .45)
                elif c == 'X':
                    gone[k] = True
                    end(k, 'out', .5)
                else:
                    seat = int(c)
                    enemy = b['owner'] // 3 != seat
                    gone[k] = True
                    base_hits_now[seat] += 1
                    dmg = False
                    if self.base_hp[seat] > 0 and not (P['base_hit_cap'] and base_hits_now[seat] > P['base_hit_cap']):
                        self.base_hp[seat] -= 1
                        dmg = True
                        if enemy:
                            self.base_dmg[1 - seat] += 1
                        else:
                            own_took[seat] += 1
                    elif enemy and own_took[seat] > 0:
                        own_took[seat] -= 1
                        self.base_dmg[1 - seat] += 1
                    ev.append(f"base_hit {seat} {b['owner']} {sub}")
                    tm = end(k, 'base', .4)
                    tr['base_hits'].append(dict(t=tm, seat=seat, by=b['owner'], dmg=dmg, x=b['x'], y=b['y']))
            for (x, y) in brick:
                if self.grid[y][x] == '#':
                    self.grid[y][x] = '.'
                    ev.append(f'wall {x} {y} brick {sub}')
                    tr['walls'].append(dict(x=x, y=y, kind='brick', sub=sub, t=(sub - .45) / 2))
            for (x, y) in steel:
                if self.grid[y][x] == '@':
                    self.grid[y][x] = '.'
                    ev.append(f'wall {x} {y} steel {sub}')
                    tr['walls'].append(dict(x=x, y=y, kind='steel', sub=sub, t=(sub - .45) / 2))
            # 判定 6：同格抵消
            if P['bullet_cancel']:
                left = [not g for g in gone]
                for k in range(1, nb):
                    if not left[k]:
                        continue
                    for j in range(k):
                        if left[j] and (bl[j]['x'], bl[j]['y']) == (bl[k]['x'], bl[k]['y']):
                            gone[k] = gone[j] = True
                            ev.append(f"cancel same {bl[k]['x']} {bl[k]['y']} {sub}")
                            end(k, 'same', 0)
                            end(j, 'same', 0)
                            tr['cancels'].append(dict(t=sub / 2, x=bl[k]['x'], y=bl[k]['y']))
                            break
            self.bullet = [b for k, b in enumerate(bl) if not gone[k]]

        # ---------- 回合末 ----------
        for i, t in enumerate(self.tank):
            if t['alive'] and t['hp'] <= 0:
                ev.append(f"death {i} {t['x']} {t['y']} -1")
                tr['ends'].append(dict(type='death', tank=i, x=t['x'], y=t['y'], by=-1))
                self._destroy(i)
                self.kills[1 - i // 3] += 1
        plan = []
        for i, t in enumerate(self.tank):
            if t['alive'] or t['respawn_turn'] > turn:
                continue
            v = self.tank_at(*self.spawn[i])
            if v >= 0 and (v // 3 == i // 3 or not P['respawn_telefrag']):
                continue
            plan.append((i, v))
        for i, v in plan:
            if v < 0:
                continue
            t = self.tank[v]
            ev.append(f"death {v} {t['x']} {t['y']} {i}")
            tr['ends'].append(dict(type='death', tank=v, x=t['x'], y=t['y'], by=i))
            self._destroy(v)
            self.kills[i // 3] += 1
        for i, _ in plan:
            t = self.tank[i]
            t.update(alive=True, x=self.spawn[i][0], y=self.spawn[i][1], hp=self.types[i % 3]['hp'], level=0,
                     last_fire=NEVER_FIRED, respawn_turn=0)
            ev.append(f"respawn {i} {t['x']} {t['y']}")
            tr['ends'].append(dict(type='respawn', tank=i, x=t['x'], y=t['y']))
            if P['respawn_clear_bullets'] and self.bullet:
                keep = [b for b in self.bullet if (b['x'], b['y']) != (t['x'], t['y'])]
                if len(keep) != len(self.bullet):
                    for b in self.bullet:
                        if (b['x'], b['y']) == (t['x'], t['y']):
                            b['_tr']['cleared'] = True
                    self.bullet = keep
                    ev.append(f"bullet_cleared {t['x']} {t['y']} {i}")
                    tr['ends'].append(dict(type='cleared', tank=i, x=t['x'], y=t['y']))
        if any(self.star):
            for i, t in enumerate(self.tank):
                if not t['alive'] or t['level'] >= P['max_level']:
                    continue
                for k, (x, y) in enumerate(self.supply):
                    if self.star[k] and (x, y) == (t['x'], t['y']):
                        self.star[k] = False
                        t['level'] += 1
                        t['hp'] += P['level_hp']
                        self.stars_eaten[i // 3] += 1
                        ev.append(f"star {i} {x} {y} {t['level']}")
                        tr['ends'].append(dict(type='star', tank=i, x=x, y=y, level=t['level']))
        if turn >= P['supply_first'] and (turn - P['supply_first']) % P['supply_period'] == 0:
            for k, (x, y) in enumerate(self.supply):
                if self.star[k]:
                    continue
                self.star[k] = True
                ev.append(f'supply {x} {y}')
                tr['ends'].append(dict(type='supply', x=x, y=y))
        h0, h1 = self.base_hp
        if h0 <= 0 or h1 <= 0 or turn >= P['max_turns']:
            self.done = True
            if h0 <= 0 < h1:
                self.winner, self.reason = 1, 1
            elif h1 <= 0 < h0:
                self.winner, self.reason = 0, 1
            else:
                self.winner, self.reason = -1, 6
                for key in P['tiebreak']:
                    a, b = {'base': self.base_hp, 'kills': self.kills, 'stars': self.stars_eaten,
                            'damage': self.base_dmg}[key]
                    if a != b:
                        self.winner = 0 if a > b else 1
                        self.reason = 2 + TB_NAMES.index(key)
                        break
        for b in self.bullet:
            b.pop('_tr', None)
        return tr

    def _destroy(self, i):
        t = self.tank[i]
        t.update(alive=False, x=-1, y=-1, hp=0, level=0, last_fire=NEVER_FIRED, respawn_turn=self.turn + self.P['respawn'])
