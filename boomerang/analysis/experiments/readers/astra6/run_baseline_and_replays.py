import json, sys, time, types, collections
from pathlib import Path

KIT = Path('<本机路径，未收录>')
sys.path.insert(0, str(KIT))
import match

OUT = KIT / '_bot' / 'astra6'
OUT.mkdir(parents=True, exist_ok=True)

A = Path('<本机路径，未收录>')
maps = [A / '选手包' / 'maps' / 'twinlakes.txt', A / '选手包' / 'maps' / 'yinyang.txt'] + \
       [A / '原型' / '地图' / '隐藏' / f for f in ('icefloes.txt', 'ruins.txt', 'skybridge.txt', 'stonecircle.txt')]

p_a6 = match.resolve_prog(str(KIT / 'ctl' / 'astra6.cpp'))
p_a62 = match.resolve_prog(str(KIT / 'all62' / 'astra.cpp'))
p_fable = match.resolve_prog(str(KIT / 'all62' / 'fable.cpp') if (KIT / 'all62' / 'fable.cpp').is_file() else str(KIT / 'all' / 'fable.cpp'))
p_opus = match.resolve_prog(str(KIT / 'all62' / 'opus.cpp') if (KIT / 'all62' / 'opus.cpp').is_file() else str(KIT / 'all' / 'opus.cpp'))

entries = [p_a6, p_a62, p_fable, p_opus]
extra = ['--step-ms', '10', '--sandbox', 'none']

# 1. 72 duel games: 6 maps x seeds 11001..11006 x 2 seats
duel_games = []
duel_meta = {}
for mp in maps:
    for seed in range(11001, 11007):
        for seats in ([0, 1], [1, 0]):
            gid = len(duel_games)
            duel_games.append((gid, seed, seats, 0, str(mp)))
            duel_meta[gid] = {'map': mp.stem, 'seed': seed, 'seats': seats}

t0 = time.time()
print(f'Running {len(duel_games)} duel games with workers=2...', flush=True)
duel_res = match.run_batch(entries, duel_games, 2, extra, replay_dir=OUT / 'replays' / 'duel_vs_astra62')
for r in duel_res:
    r['meta'] = duel_meta[r['id']]
json.dump(duel_res, open(OUT / 'duel_vs_astra62.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(f'Duel done in {time.time()-t0:.1f}s', flush=True)

# Summarize duel
w = d = l = 0
stats = {0: collections.Counter(), 1: collections.Counter()} # 0=astra6, 1=astra62
causes = {0: collections.Counter(), 1: collections.Counter()}
by_map = collections.defaultdict(lambda: [0,0,0])
for r in duel_res:
    mp = r['meta']['map']
    seats = r['meta']['seats']
    pl_by_bot = {}
    for p in r['players']:
        bot_id = seats[p['seat']]
        pl_by_bot[bot_id] = p
        for k in ('throws', 'slashes', 'kicks', 'dashes', 'recalls', 'blocks', 'clashes', 'dash_melee', 'ret_kills', 'curve_kills', 'gap_dash', 'timeouts', 'invalid'):
            if k in p:
                stats[bot_id][k] += p[k]
        causes[bot_id][p['cause']] += 1
    rk6, rk62 = pl_by_bot[0]['rank'], pl_by_bot[1]['rank']
    if rk6 < rk62:
        w += 1; by_map[mp][0] += 1
    elif rk6 > rk62:
        l += 1; by_map[mp][2] += 1
    else:
        d += 1; by_map[mp][1] += 1

print(f'72-game Duel astra6 vs astra62: {w}W {d}D {l}L (score rate {(w+0.5*d)/72*100:.2f}%)')
print('By map:', dict(by_map))
print('astra6 avg actions/game:', {k: round(v/72, 2) for k, v in stats[0].items()})
print('astra62 avg actions/game:', {k: round(v/72, 2) for k, v in stats[1].items()})
print('astra6 causes:', dict(causes[0]))
print('astra62 causes:', dict(causes[1]))
print('Sample player keys:', list(duel_res[0]['players'][0].keys()))

# 2. 24 FFA games: {astra6, astra62, fable, opus} on 6 maps x seed 11001 x 4 rotations
ffa_games = []
ffa_meta = {}
base_order = [0, 1, 2, 3]
for mp in maps:
    seed = 11001
    for rot in range(4):
        seats = base_order[rot:] + base_order[:rot]
        gid = len(ffa_games)
        ffa_games.append((gid, seed, seats, 1, str(mp)))
        ffa_meta[gid] = {'map': mp.stem, 'seed': seed, 'seats': seats}

t1 = time.time()
print(f'Running {len(ffa_games)} FFA games with workers=2...', flush=True)
ffa_res = match.run_batch(entries, ffa_games, 2, extra, replay_dir=OUT / 'replays' / 'ffa_vs_top')
for r in ffa_res:
    r['meta'] = ffa_meta[r['id']]
json.dump(ffa_res, open(OUT / 'ffa_vs_top.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(f'FFA done in {time.time()-t1:.1f}s', flush=True)
