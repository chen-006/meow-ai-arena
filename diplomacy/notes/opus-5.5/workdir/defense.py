"""枚举敌方命令组合，评估我方候选防守方案。用法：python defense.py plans.json"""
import itertools, sys, json
from diplomacy import Game

UNITS = {
    'ENGLAND': ['F NTH', 'F BAR', 'F NWY', 'F BOT', 'A STP', 'F ENG'],
    'FRANCE': ['A BUR', 'A PIC', 'A PIE', 'F WES', 'F LYO'],
    'GERMANY': ['A HOL', 'A BEL', 'A PRU', 'A SIL', 'F BAL', 'A BOH'],
    'ITALY': ['F ION', 'F TUN', 'A TRI', 'F TYS', 'A TYR', 'A APU'],
    'RUSSIA': ['A WAR', 'A BUD', 'A LVN', 'A GAL', 'F RUM'],
    'TURKEY': ['A BUL', 'A CON', 'F AEG', 'F GRE', 'A ALB', 'F EAS'],
}
PHASE = 'F1904M'
MYAREA = {'WAR', 'LVN', 'GAL', 'BUD', 'RUM', 'MOS', 'UKR', 'SEV', 'SIL', 'PRU', 'VIE', 'BOH'}
ENEMY = {'GERMANY': ['PRU', 'SIL', 'BOH', 'BAL']}


def base():
    g = Game()
    g.set_current_phase(PHASE)
    for p, us in UNITS.items():
        g.set_units(p, us, reset=True)
    return g


def enemy_options(g, power, locs):
    own = {u.split()[1][:3] for u in g.get_units(power)}
    poss = g.get_all_possible_orders()
    res = []
    for loc in locs:
        opts = []
        for o in poss[loc]:
            t = o.split()
            if ' C ' in o or o.endswith('VIA'):
                continue
            if t[2] == 'H':
                opts.append(o)
            elif t[2] == '-':
                if t[3][:3] in MYAREA:
                    opts.append(o)
            elif t[2] == 'S':
                if t[4][:3] in own and (len(t) == 5 or t[6][:3] in MYAREA):
                    opts.append(o)
        res.append(opts)
    return res


def run(mine, enemy):
    g = base()
    g.set_orders('RUSSIA', mine)
    for p, os_ in enemy.items():
        g.set_orders(p, os_)
    g.process()
    occ = {}
    for p in g.powers:
        for u in g.get_units(p):
            if not u.startswith('*'):
                occ[u.split()[1][:3]] = p
    dis = [u for u in g.get_units('RUSSIA') if u.startswith('*')]
    lost_c = tuple(c for c in ['WAR', 'BUD', 'RUM'] if occ.get(c) not in (None, 'RUSSIA'))
    return lost_c, len(dis)


if __name__ == '__main__':
    plans = json.loads(open(sys.argv[1], encoding='utf-8').read())
    g = base()
    allopts = []
    for p, locs in ENEMY.items():
        for opts in enemy_options(g, p, locs):
            allopts.append([(p, o) for o in opts])
    print('option counts', [len(o) for o in allopts])
    combos = list(itertools.product(*allopts))
    print('combos', len(combos))
    for name, mine in plans.items():
        stats, ex = {}, {}
        for c in combos:
            en = {}
            for p, o in c:
                en.setdefault(p, []).append(o)
            key = run(mine, en)
            stats[key] = stats.get(key, 0) + 1
            ex.setdefault(key, [o for _, o in c])
        print('==', name)
        for k, v in sorted(stats.items(), key=lambda x: (-len(x[0][0]), -x[0][1])):
            print('  lost', k[0], 'dislodged', k[1], 'n=', v, 'eg', ex[k])
