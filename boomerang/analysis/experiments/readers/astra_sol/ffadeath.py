import sys,glob,os,collections,math
sys.path.insert(0,os.path.dirname(__file__))
from rp import parse
def run(d):
    C=collections.defaultdict(collections.Counter); T=collections.defaultdict(list)
    for f in sorted(glob.glob(os.path.join(d,'g*.txt'))):
        G=parse(f);P=G['players']
        deaths=[(t,a) for t,ty,a in G['E'] if ty=='DEATH']
        first=min(t for t,a in deaths) if deaths else None
        for t,a in deaths:
            v,cause,killer=a[0],a[1],a[2]; nm=P[v]
            ps,ds=G['S'][t]
            C[nm]['死亡']+=1
            if t==first: C[nm]['第一个死']+=1; T[nm].append(t)
            C[nm]['自己镖:'+('在手' if ds[v][0]==0 else '飞行' if ds[v][0] in (1,2,6) else '地上/召回')]+=1
            if killer>=0 and killer!=v:
                alive=[k for k in range(len(P)) if k!=v and ps[k][0]]
                near=min(alive,key=lambda k:math.hypot(ps[k][1]-ps[v][1],ps[k][2]-ps[v][2]))
                C[nm]['凶手是最近的人' if killer==near else '凶手不是最近的人']+=1
                C[nm]['凶手距离≥4m' if math.hypot(ps[killer][1]-ps[v][1],ps[killer][2]-ps[v][2])>=4000 else '凶手距离<4m']+=1
                # 死前 1.5 秒内自己是否刚出过手
            if any(ty=='THROW' and a2[0]==v and t-30<=t2<=t for t2,ty,a2 in G['E']): C[nm]['死前1.5秒内扔过镖']+=1
            if any(ty=='DASH' and a2[0]==v and t-10<=t2<=t for t2,ty,a2 in G['E']): C[nm]['死前0.5秒内冲刺过']+=1
    for nm,c in C.items():
        tt=sorted(T[nm]); print(nm,dict(c),'第一个死的中位时刻 %.2fs'%(tt[len(tt)//2]/20) if tt else '')
run(sys.argv[1])
