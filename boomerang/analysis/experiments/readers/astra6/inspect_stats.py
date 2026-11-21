import json, collections
from pathlib import Path

R = Path('<本机路径，未收录>')
ctl = json.load(open(R / '对照赛/obs_新打的.json', encoding='utf-8'))
sup = json.load(open(R / '全员大混战/obs_补打.json', encoding='utf-8'))
all_a6 = [x for x in (ctl + sup) if 'astra6' in x[1]]
print('Total games involving astra6 in super21:', len(all_a6))

duel_maps = collections.defaultdict(lambda: [0,0,0])
ffa_maps = collections.defaultdict(lambda: [0,0,0])
duel_all_maps = collections.defaultdict(lambda: [0,0,0])
ffa_all_maps = collections.defaultdict(lambda: [0,0,0])

duel_total = [0,0,0]
ffa_total = [0,0,0]
ffa_games_count = 0

for g, v in all_a6:
    mp = g['map']
    if g['mode'] == 0:
        opp = [k for k in v if k != 'astra6'][0]
        idx = 0 if v['astra6'] > v[opp] else (2 if v['astra6'] < v[opp] else 1)
        duel_all_maps[mp][idx] += 1
        duel_total[idx] += 1
        if opp == 'astra62':
            duel_maps[mp][idx] += 1
    else:
        ffa_games_count += 1
        for opp in v:
            if opp == 'astra6': continue
            idx = 0 if v['astra6'] > v[opp] else (2 if v['astra6'] < v[opp] else 1)
            ffa_all_maps[mp][idx] += 1
            ffa_total[idx] += 1
            if opp == 'astra62':
                ffa_maps[mp][idx] += 1

print('duel_total [W,D,L]:', duel_total, sum(duel_total), f'{(duel_total[0]+0.5*duel_total[1])/sum(duel_total)*100:.2f}%')
print('ffa_games_count:', ffa_games_count, 'ffa_pairwise_total [W,D,L]:', ffa_total, sum(ffa_total), f'{(ffa_total[0]+0.5*ffa_total[1])/sum(ffa_total)*100:.2f}%')

print('--- astra6 vs astra62 DUEL by map [W, D, L] ---')
for m, c in sorted(duel_maps.items()):
    print(f'  {m}: {c} -> {(c[0]+0.5*c[1])/sum(c)*100:.2f}%')
print('--- astra6 vs astra62 FFA pairwise by map [W, D, L] ---')
for m, c in sorted(ffa_maps.items()):
    print(f'  {m}: {c} -> {(c[0]+0.5*c[1])/sum(c)*100:.2f}%')
print('--- astra6 vs ALL DUEL by map [W, D, L] ---')
for m, c in sorted(duel_all_maps.items()):
    print(f'  {m}: {c} -> {(c[0]+0.5*c[1])/sum(c)*100:.2f}%')
print('--- astra6 vs ALL FFA pairwise by map [W, D, L] ---')
for m, c in sorted(ffa_all_maps.items()):
    print(f'  {m}: {c} -> {(c[0]+0.5*c[1])/sum(c)*100:.2f}%')

lines = open(R / 'logs/astra6_stdout.jsonl', encoding='utf-8').read().splitlines()
print('stdout lines:', len(lines))
for i, ln in enumerate(lines, 1):
    obj = json.loads(ln)
    t = obj.get('type')
    it = obj.get('item', {})
    if t in ('thread.started', 'turn.started', 'turn.completed') or it.get('type') == 'agent_message':
        txt = it.get('text', str(obj))
        print(f'L{i}: {t} | {it.get("type")} | {txt[:200]}')
