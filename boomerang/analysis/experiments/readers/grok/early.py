import sys, glob, os, collections, math
sys.path.insert(0, os.path.dirname(__file__)); from rp import parse
d, tg = sys.argv[1], sys.argv[2]; lim = int(sys.argv[3]) if len(sys.argv) > 3 else 60
agg = collections.Counter(); rows = []
for fn in sorted(glob.glob(os.path.join(d, 'g*.txt')), key=lambda f: int(os.path.basename(f)[1:-4])):
    R = parse(fn); seat = [s for s, n in R['names'].items() if n == tg][0]
    for t in sorted(R['E']):
        for typ, a in R['E'][t]:
            if typ == 'DEATH' and a[0] == seat and t < lim:
                killer = a[2]; cause = a[1]
                # 找凶手最近一次 RELEASE
                rel = None
                for tt in range(t, -1, -1):
                    for ty2, b in R['E'].get(tt, []):
                        if ty2 == 'RELEASE' and b[0] == killer: rel = (tt, b[1], b[2]); break
                    if rel: break
                ps, ds = R['S'][t]
                me = ps[seat]
                # 死前 1 秒（20 步）时 6 米内持镖敌人数
                tb = max(0, t - 20); psb, dsb = R['S'][tb]
                near = sum(1 for k in range(R['n']) if k != seat and psb[k][0] and dsb[k][0] == 0 and math.hypot(psb[k][1]-psb[seat][1], psb[k][2]-psb[seat][2]) < 6000)
                dist_rel = None
                if rel:
                    pr, dr = R['S'][rel[0]]
                    dist_rel = math.hypot(pr[seat][1]-rel[1], pr[seat][2]-rel[2])
                own = ds[seat][0]
                # 自己是否刚扔过
                myrel = [tt for tt in range(0, t) for ty2, b in R['E'].get(tt, []) if ty2 == 'RELEASE' and b[0] == seat]
                rows.append((os.path.basename(fn), R['map'], t, cause, R['names'].get(killer, '-'), rel[0] if rel else None, round(dist_rel) if dist_rel else None, near, own, myrel[:3]))
                agg['n'] += 1; agg['near%d' % min(near, 3)] += 1
                if dist_rel: agg['dsum'] += dist_rel; agg['dn'] += 1
                if myrel: agg['threw_before'] += 1
                agg['killer_' + R['names'].get(killer, '-')] += 1
for r in rows[:25]: print(r)
print(dict(agg)); print('凶手出手时平均距离 %.2f 米' % (agg['dsum']/max(1, agg['dn'])/1000))
