import json,collections,sys
G=json.load(open("<本机路径，未收录>",encoding='utf-8'))
ME=['glm','deepseek','kimi','qwen','mimo','doubao']
out=[]
def P(*a): out.append(' '.join(str(x) for x in a))
# per bot per mode
for b in ME:
  for mode in (0,1):
    n=0;w=l=d=0;cause=collections.Counter();kills=0;rank=collections.Counter();bymap=collections.defaultdict(lambda:[0,0,0]);steps_death=[]
    for g in G:
      if g['mode']!=mode or b not in g['names']: continue
      n+=1
      me=[p for p in g['players'] if p['name']==b][0]
      kills+=me['kills']; cause[me['cause']]+=1; rank[me['rank']]+=1
      bm=bymap[g['map']]; bm[0]+=1
      if me['cause']=='fall': bm[1]+=1
      if mode==0:
        o=[p for p in g['players'] if p['name']!=b][0]
        if me['rank']<o['rank']: w+=1
        elif me['rank']>o['rank']: l+=1
        else: d+=1
    P(b,'mode',mode,'games',n,'WLD',(w,l,d) if mode==0 else '', 'kills/g',round(kills/n,3),'cause',dict(cause),'rank',dict(sorted(rank.items())))
    P('   fall by map',{m:(v[1],v[0]) for m,v in bymap.items()})
# head to head duel
for b in ME:
  row=[]
  opp=collections.defaultdict(lambda:[0,0,0])
  for g in G:
    if g['mode']!=0 or b not in g['names']: continue
    me=[p for p in g['players'] if p['name']==b][0]; o=[p for p in g['players'] if p['name']!=b][0]
    r=opp[o['name']]
    if me['rank']<o['rank']: r[0]+=1
    elif me['rank']>o['rank']: r[1]+=1
    else: r[2]+=1
  P(b,'H2H duel', {k:tuple(v) for k,v in sorted(opp.items())})
# duel slash/dart deaths by opponent? overall slash death counts all players duel and ffa
for mode in (0,1):
  c=collections.defaultdict(collections.Counter)
  for g in G:
    if g['mode']!=mode: continue
    for p in g['players']: c[p['name']][p['cause']]+=1
  P('mode',mode,'cause table'); 
  for k,v in sorted(c.items(),key=lambda kv:-kv[1]['slash']): P('  ',k,dict(v))
# pairwise duel per map for given pairs
pairs=[('deepseek','glm'),('kimi','qwen'),('qwen','mimo'),('mimo','doubao'),('kimi','deepseek')]
for a,b in pairs:
  bm=collections.defaultdict(lambda:[0,0,0])
  for g in G:
    if g['mode']!=0 or set(g['names'])!={a,b}: continue
    pa=[p for p in g['players'] if p['name']==a][0]; pb=[p for p in g['players'] if p['name']==b][0]
    r=bm[g['map']]
    if pa['rank']<pb['rank']: r[0]+=1
    elif pa['rank']>pb['rank']: r[1]+=1
    else: r[2]+=1
    r_c=None
  P(a,'vs',b,'by map',{k:tuple(v) for k,v in bm.items()})
  # causes in that pair
  ca=collections.Counter();cb=collections.Counter()
  for g in G:
    if g['mode']!=0 or set(g['names'])!={a,b}: continue
    for p in g['players']:
      (ca if p['name']==a else cb)[p['cause']]+=1
  P('   causes',a,dict(ca),b,dict(cb))
open("<本机路径，未收录>",'w',encoding='utf-8').write('\n'.join(out))
print('\n'.join(out))
