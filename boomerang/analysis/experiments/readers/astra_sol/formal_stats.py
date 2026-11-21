import json,collections,sys
def load(p): return json.load(open(p,encoding='utf-8'))
def stats(rows,name):
    for mode in (0,1):
        c=collections.Counter(); n=0; ranks=collections.Counter(); steps_win=[];kills=0
        for r in rows:
            if r['mode']!=mode: continue
            for p in r['players']:
                if p['name']!=name: continue
                n+=1; c[p['cause']]+=1; ranks[p['rank']]+=1; kills+=p['kills']
                if p['rank']==1 and mode==0 and p['alive']: steps_win.append(r['steps'])
        if n: print(name,'mode',mode,'games',n,'cause',dict(c),'ranks',dict(sorted(ranks.items())),'kills/g',round(kills/n,3),'median win steps',sorted(steps_win)[len(steps_win)//2] if steps_win else None)
rows=load("<本机路径，未收录>")
for nm in ['astra','fable','sol']: stats(rows,nm)
import glob
for f in glob.glob("<本机路径，未收录>"):
    print(f); rr=load(f)
    for nm in ['astra6','sol61']: stats(rr,nm)
