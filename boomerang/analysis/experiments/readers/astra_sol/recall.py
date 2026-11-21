import sys,glob,os,collections
sys.path.insert(0,os.path.dirname(__file__))
from rp import parse
for d in sys.argv[1:]:
    c=collections.defaultdict(collections.Counter)
    for f in sorted(glob.glob(os.path.join(d,'g*.txt'))):
        G=parse(f);P=G['players']
        last={}
        deaths={a[0]:t for t,ty,a in G['E'] if ty=='DEATH'}
        for t,ty,a in G['E']:
            if ty=='RECALL': c[P[a[0]]]['recall']+=1; last[a[0]]=t
            if ty=='CANCEL':
                c[P[a[0]]]['cancel']+=1
                if t-last.get(a[0],-99)<=3: c[P[a[0]]]['cancel<=3步']+=1
            if ty=='CALL': c[P[a[0]]]['call完成']+=1
        for k,nm in enumerate(P):
            if k in deaths:
                c[nm]['死亡']+=1
                if any(ty=='CANCEL' and a[0]==k and deaths[k]-40<=t<=deaths[k] for t,ty,a in G['E']): c[nm]['死前2秒内有取消召回']+=1
    print(d)
    for nm,x in c.items(): print(' ',nm,dict(x))
