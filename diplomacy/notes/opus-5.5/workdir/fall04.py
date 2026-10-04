import itertools, json, sys
from diplomacy import Game
UNITS = {
 'AUSTRIA': ['A GAL'],
 'ENGLAND': ['A BEL', 'F NTH', 'F DEN', 'F NWY', 'F EDI', 'F BAL', 'A HOL'],
 'FRANCE': ['A SPA', 'A BUR', 'F MAO', 'A PIC', 'F LYO'],
 'GERMANY': ['A MUN', 'A KIE', 'A BER', 'A RUH'],
 'ITALY': ['A TUN', 'F ADR', 'A TRI', 'F TYS', 'A VEN'],
 'RUSSIA': ['A FIN', 'F STP/SC', 'A SIL', 'A VIE', 'A UKR', 'A BUD'],
 'TURKEY': ['A SER', 'A RUM', 'F GRE', 'F BLA', 'A ARM', 'F CON'],
}
OTHERS = {'RUSSIA': ['A SIL - BER', 'A FIN H'], 'FRANCE': ['A BUR - MUN', 'A PIC H']}


def base():
    g = Game(); g.set_current_phase('F1904M')
    for p, u in UNITS.items():
        g.set_units(p, u, reset=True)
    return g


REL = {'HOL', 'BEL', 'DEN', 'KIE', 'BER', 'MUN', 'RUH'}
g = base(); poss = g.get_all_possible_orders()
opts = []
for loc in ['MUN', 'KIE', 'BER', 'RUH']:
    ger = {'MUN', 'KIE', 'BER', 'RUH'}
    opts.append([x for x in poss[loc] if ' C ' not in x and not x.endswith('VIA')
                 and (x.split()[2] != 'S' or x.split()[4][:3] in ger)
                 and x.split()[-1][:3] in REL | {'H'}])
combos = list(itertools.product(*opts)); print('combos', len(combos))
plans = json.load(open(sys.argv[1], encoding='utf-8'))
for name, mine in plans.items():
    stats = {}
    for c in combos:
        g = base(); g.set_orders('ENGLAND', mine)
        for p, o in OTHERS.items():
            g.set_orders(p, o)
        g.set_orders('GERMANY', list(c)); g.process()
        occ = {}
        for p in g.powers:
            for u in g.get_units(p):
                if not u.startswith('*'):
                    occ[u.split()[1][:3]] = p
        mineC = tuple(x for x in ['HOL', 'KIE', 'BEL', 'DEN'] if occ.get(x) == 'ENGLAND')
        k = (mineC, occ.get('BER'))
        stats[k] = stats.get(k, 0) + 1
    print('==', name)
    for k, v in sorted(stats.items(), key=lambda x: -x[1]):
        print('  ', k, v)
