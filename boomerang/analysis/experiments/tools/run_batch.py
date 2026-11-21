"""单变量对照小批量跑：python run_batch.py TAG progA progB MAP[,MAP] SEED0 SEED1 [--replays] [--ffa progC progD]
结果追加到 results.json（键 TAG|map），每批前台 <4 分钟。"""
import json, sys, time, types
from pathlib import Path
from _paths import WORKSPACE, MAPS
import match, build
HERE = Path(__file__).resolve().parent
build.BUILD = HERE / 'build'
match.REPLAYS = HERE / 'replays'
def main():
    av = sys.argv[1:]
    rep = '--replays' in av; se = '--stderr' in av; av = [x for x in av if x not in ('--replays','--stderr')]
    tag, pa, pb, maps, s0, s1 = av[:6]
    extra = av[6:]
    args = types.SimpleNamespace(step_ms=10.0, sandbox='none', cfg=None, replays=rep, no_stderr=not se, no_replay=not rep)
    progs = [match.resolve_prog(pa), match.resolve_prog(pb)] + [match.resolve_prog(x) for x in extra]
    entries, seats = match.entries_for(progs)
    ffa = len(progs) == 4
    games, meta = [], {}
    gid = 0
    for m in maps.split(','):
        for seed in range(int(s0), int(s1) + 1):
            if ffa:
                for r in range(4):
                    order = [(i + r) % 4 for i in range(4)]
                    games.append((gid, seed, [seats[i] for i in order], 1, MAPS[m])); meta[gid] = (order, m, seed); gid += 1
            else:
                for order in ([0, 1], [1, 0]):
                    games.append((gid, seed, [seats[i] for i in order], 0, MAPS[m])); meta[gid] = (order, m, seed); gid += 1
    t0 = time.time()
    res, inc, rdir, _ = match.execute(entries, games, args, len(games), tag, 2)
    assert not inc, inc
    out = {}
    for g in res:
        order, m, seed = meta[g['id']]
        d = out.setdefault(m, {'n': 0, 'w': [0]*len(progs), 'draw': 0, 'fall': [0]*len(progs), 'dart': [0]*len(progs),
                               'slash': [0]*len(progs), 'kills': [0]*len(progs), 'pts': [0.0]*len(progs), 'faults': [0]*len(progs),
                               'acts': [{k: 0 for k, _ in match.ACTS} for _ in progs]})
        d['n'] += 1
        if ffa:
            pts = match.pts_for_ranks(g['players'])
        else:
            w = g['winner']
            if w < 0: d['draw'] += 1
            else: d['w'][order[w]] += 1
        for seat, p in enumerate(g['players']):
            i = order[seat]
            c = p['cause_name']
            if c in ('fall', 'dart', 'slash'): d[c][i] += 1
            d['kills'][i] += p['kills']; d['faults'][i] += p['faults_total']
            for k, _ in match.ACTS: d['acts'][i][k] += p[k]
            if ffa: d['pts'][i] += pts[seat] + p['kills'] * match.KILL_POINTS
    rf = HERE / 'results.json'
    allr = json.loads(rf.read_text(encoding='utf-8')) if rf.exists() else {}
    for m, d in out.items():
        allr[f'{tag}|{m}|{s0}-{s1}'] = d
    rf.write_text(json.dumps(allr, ensure_ascii=False, indent=1), encoding='utf-8')
    for m, d in out.items():
        print(tag, m, 'n', d['n'], 'W', d['w'], 'D', d['draw'], 'fall', d['fall'], 'dart', d['dart'], 'slash', d['slash'], 'kills', d['kills'],
              ('pts ' + str([round(x / d['n'], 3) for x in d['pts']])) if ffa else '', 'faults', d['faults'])
    print('replays', rdir, 'sec', round(time.time() - t0, 1))
main()
