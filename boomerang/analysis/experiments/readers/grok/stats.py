import json,collections,sys
d=json.load(open("<本机路径，未收录>",encoding='utf-8'))
print('modes',collections.Counter(g['mode'] for g in d))
names=['hydrogen','carbon','grok','glm']
def duel_pair(a,b):
    tot=collections.Counter(); bymap=collections.defaultdict(collections.Counter)
    for g in d:
        if g['mode']!=0: continue
        n=g['names']
        if set(n)!={a,b}: continue
        pa=[p for p in g['players'] if p['name']==a][0]; pb=[p for p in g['players'] if p['name']==b][0]
        r='W' if pa['rank']<pb['rank'] else ('L' if pa['rank']>pb['rank'] else 'D')
        tot[r]+=1; bymap[g['map']][r]+=1
        if pa['cause']!='none': tot['a_'+pa['cause']]+=1
        if pb['cause']!='none': tot['b_'+pb['cause']]+=1
    print(a,'vs',b,dict(tot))
    for m,c in sorted(bymap.items()): print('  ',m,c['W'],c['L'],c['D'], '%.1f%%'%(100*(c['W']+c['D']/2)/sum(c.values())))
for a,b in [('hydrogen','carbon'),('grok','glm')]: duel_pair(a,b)
for nm in names:
    du=collections.Counter(); ff=collections.Counter(); ranks=collections.Counter(); kills=0; nff=0; pts=0
    ffmap=collections.defaultdict(list); first_death=0
    for g in d:
        me=[p for p in g['players'] if p['name']==nm]
        if not me: continue
        me=me[0]
        if g['mode']==0:
            o=[p for p in g['players'] if p['name']!=nm][0]
            du['W' if me['rank']<o['rank'] else ('L' if me['rank']>o['rank'] else 'D')]+=1
            du['c_'+me['cause']]+=1
            du['k']+=me['kills']
        elif g['mode']==1:
            nff+=1; ranks[me['rank']]+=1; kills+=me['kills']; ff['c_'+me['cause']]+=1
            # score: shared rank points: count players strictly worse
            worse=sum(1 for p in g['players'] if p['rank']>me['rank']); eq=sum(1 for p in g['players'] if p['rank']==me['rank'])-1
            s=worse+eq*0.5+me['kills']; pts+=s; ffmap[g['map']].append(s)
    print(nm,'DUEL',dict(du), 'n',du['W']+du['L']+du['D'],'scorerate %.2f%%'%(100*(du['W']+du['D']/2)/(du['W']+du['L']+du['D'])))
    print(nm,'FFA n',nff,'ranks',dict(sorted(ranks.items())),'kills/g %.3f'%(kills/nff),'pts/g %.3f'%(pts/nff),dict(ff))
    print('   ffa by map',{m:round(sum(v)/len(v),3) for m,v in sorted(ffmap.items())})
