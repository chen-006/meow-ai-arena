# 回放解析：python rp.py <回放目录> <名字> [<名字>...]
import sys, glob, os, collections, json
def parse(fn):
    names = {}; S = {}; E = collections.defaultdict(list); A = {}; mode=None; mapname=None; n=0
    for ln in open(fn, encoding='utf-8', errors='replace'):
        if ln.startswith('PLAYER '):
            w = ln.split(); names[int(w[1])] = w[3]
        elif ln.startswith('MODE '): mode=int(ln.split()[1])
        elif ln.startswith('MAPNAME'): mapname=ln.split()[1]
        elif ln.startswith('PLAYERS'): n=int(ln.split()[1])
        elif ln.startswith('S '):
            seg = ln.strip().split('|'); t = int(seg[0].split()[1])
            ps = [list(map(int, s.split())) for s in seg[1:1+n]]
            ds = [list(map(int, s.split())) for s in seg[1+n:1+2*n]]
            S[t] = (ps, ds)
        elif ln.startswith('E '):
            w = ln.split(); E[int(w[1])].append((w[2], list(map(int, w[3:]))))
        elif ln.startswith('A '):
            seg = ln.strip().split('|'); t=int(seg[0].split()[1])
            A[t] = [list(map(int, s.split())) for s in seg[1:1+n]]
    return dict(names=names, S=S, E=E, A=A, mode=mode, map=mapname, n=n)
def stats(dirs, targets):
    out = {}
    for tg in targets:
        out[tg] = collections.Counter()
    for d in dirs:
        for fn in sorted(glob.glob(os.path.join(d, 'g*.txt'))):
            R = parse(fn)
            for seat, nm in R['names'].items():
                if nm not in targets: continue
                c = out[nm]; c['games'] += 1
                ev = R['E']; first_throw = None; death_t = None
                for t in sorted(ev):
                    for typ, a in ev[t]:
                        if typ == 'THROW' and a[0] == seat:
                            c['throws'] += 1
                            if first_throw is None: first_throw = t
                        if typ == 'BLOCK' and a[0] == seat: c['blocks'] += 1
                        if typ == 'CLASH' and a[0] == seat: c['clashes'] += 1
                        if typ == 'KICKHIT' and a[0] == seat: c['kickhits'] += 1
                        if typ == 'CUT' and a[0] == seat: c['cuts'] += 1
                        if typ == 'HIT' and a[0] == seat and a[1] != seat: c['dart_hits'] += 1
                        if typ == 'DASH' and a[0] == seat: c['dashes'] += 1
                        if typ == 'RECALL' and a[0] == seat: c['recalls'] += 1
                        if typ == 'SLASH' and a[0] == seat: c['slashes'] += 1
                        if typ == 'DEATH' and a[0] == seat: death_t = t; cause = a[1]; killer = a[2]
                if first_throw is not None:
                    c['first_throw_sum'] += first_throw; c['first_throw_n'] += 1
                    if first_throw < 40: c['first_throw_lt2s'] += 1
                if death_t is not None:
                    c['deaths'] += 1; c['death_t_sum'] += death_t
                    if death_t < 60: c['death_lt3s'] += 1
                    ps, ds = R['S'].get(death_t, (None, None))
                    if ds:
                        st = ds[seat][0]
                        c['death_dart_' + ('hand' if st == 0 else 'out' if st in (1,2,6) else 'ground' if st in (3,4) else str(st))] += 1
                    c['death_cause_%d' % cause] += 1
                    if cause == 3 and killer < 0: c['selffall'] += 1
    for tg, c in out.items():
        g = max(1, c['games'])
        print(tg, 'games', c['games'], 'per game: throws %.2f dashes %.2f slashes %.2f blocks %.2f clashes %.2f kickhits %.2f cuts %.2f dart_hits %.2f recalls %.2f' % tuple(c[k]/g for k in ['throws','dashes','slashes','blocks','clashes','kickhits','cuts','dart_hits','recalls']))
        if c['first_throw_n']: print('   first throw avg %.2fs, <2s in %d/%d games' % (c['first_throw_sum']/c['first_throw_n']/20, c['first_throw_lt2s'], c['games']))
        if c['deaths']: print('   deaths %d, avg t %.2fs, <3s %d; dart at death: hand %d out %d ground %d; causes dart %d slash %d fall %d (selffall %d)' % (c['deaths'], c['death_t_sum']/c['deaths']/20, c['death_lt3s'], c['death_dart_hand'], c['death_dart_out'], c['death_dart_ground'], c['death_cause_1'], c['death_cause_2'], c['death_cause_3'], c['selffall']))
if __name__ == '__main__':
    d = sys.argv[1].split(',')
    stats(d, sys.argv[2:])
