# 通用对局批跑（bot解析_Hydrogen/Grok/GLM 用）：
# python run_specs.py <输出json> <duel|ffa> <名=路径,...> <种子起> <种子数> [--replays 目录] [--maps a,b] [--stderr 目录]
# duel：前两个程序互打，每图每种子两座各一次；ffa：四个程序，每图每种子做 4 次座位轮转。
import sys, json, time, types, collections
from pathlib import Path
from _paths import MAPS
import match


def main():
    out, mode, specs, s0, ns = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
    rest = sys.argv[6:]; rdir = None; sdir = None; maps = list(MAPS)
    if '--replays' in rest: rdir = rest[rest.index('--replays')+1]
    if '--stderr' in rest: sdir = rest[rest.index('--stderr')+1]
    if '--maps' in rest: maps = rest[rest.index('--maps')+1].split(',')
    names, paths = [], []
    for kv in specs.split(','):
        k, v = kv.split('='); names.append(k); paths.append(v)
    progs = [match.resolve_prog(p) for p in paths]
    entries, seats = match.entries_for(progs)
    extra = match.common_args(types.SimpleNamespace(step_ms=10, sandbox='none', cfg=None, no_stderr=(sdir is None)))
    batch, meta = [], []; gid = 0
    for mp in maps:
        for seed in range(s0, s0+ns):
            if mode == 'duel':
                for order in ((0, 1), (1, 0)):
                    batch.append((gid, seed, [seats[order[0]], seats[order[1]]], 0, str(MAPS[mp])))
                    meta.append((mp, seed, [names[i] for i in order])); gid += 1
            else:
                for r in range(4):
                    order = [(i + r) % 4 for i in range(4)]
                    batch.append((gid, seed, [seats[i] for i in order], 1, str(MAPS[mp])))
                    meta.append((mp, seed, [names[i] for i in order])); gid += 1
    t0 = time.time()
    res = {r['id']: r for r in match.run_batch(entries, batch, 2, extra, rdir, sdir)}
    games = []
    for i, (mp, seed, order) in enumerate(meta):
        r = res[i]; pl = sorted(r['players'], key=lambda p: p['seat'])
        games.append({'id': i, 'map': mp, 'seed': seed, 'names': order, 'rank': [p['rank'] for p in pl],
                      'kills': [p['kills'] for p in pl], 'cause': [p['cause_name'] for p in pl], 'steps': r.get('steps'),
                      'faults': [len(p['faults']) if isinstance(p.get('faults'), list) else int(p.get('faults') or 0) for p in pl]})
    summ = {}
    for nm in names:
        c = collections.Counter(); bymap = collections.defaultdict(collections.Counter)
        for gm in games:
            if nm not in gm['names']: continue
            s = gm['names'].index(nm); rk = gm['rank'][s]
            if mode == 'duel':
                o = 1 - s; ro = gm['rank'][o]
                w = 'w' if rk < ro else ('l' if rk > ro else 'd'); c[w] += 1; bymap[gm['map']][w] += 1
                if gm['cause'][s] == 'fall' and gm['kills'][o] == 0: c['fall_opp0k'] += 1
            else:
                worse = sum(1 for x in gm['rank'] if x > rk); eq = sum(1 for x in gm['rank'] if x == rk) - 1
                pts = worse + 0.5*eq + gm['kills'][s]; c['pts'] += pts; c['n'] += 1; c['rank%d' % rk] += 1; c['kills'] += gm['kills'][s]
                bymap[gm['map']]['pts'] += pts; bymap[gm['map']]['n'] += 1
            c['cause_' + gm['cause'][s]] += 1
            c['faults'] += gm['faults'][s]
        summ[nm] = {'all': dict(c), 'bymap': {k: dict(v) for k, v in bymap.items()}}
        if mode == 'duel':
            n = c['w']+c['l']+c['d']
            print('%-14s %d:%d:%d  得分率 %.1f%%  落坑 %d（对手零击杀 %d） 故障 %d' % (nm, c['w'], c['l'], c['d'], 100*(c['w']+c['d']/2)/max(1, n), c['cause_fall'], c['fall_opp0k'], c['faults']))
        else:
            print('%-14s 场均 %.3f 分  击杀 %.3f  名次1-4 %d/%d/%d/%d  出局 镖%d 砍%d 坑%d 活%d 故障 %d' % (nm, c['pts']/c['n'], c['kills']/c['n'], c['rank1'], c['rank2'], c['rank3'], c['rank4'], c['cause_dart'], c['cause_slash'], c['cause_fall'], c['cause_none'], c['faults']))
    json.dump({'mode': mode, 'specs': specs, 'seeds': [s0, s0+ns-1], 'maps': maps, 'sec': round(time.time()-t0, 1), 'summary': summ, 'games': games},
              open(out, 'w', encoding='utf-8'), ensure_ascii=False)
    print('用时 %.1f 秒，%d 局' % (time.time()-t0, len(games)))


main()
