# 回放解析：统计挡镖、挡镖后多久拿下、出镖与命中、死亡方式
import sys,glob,os,collections,math,json
def parse(path):
    G={'players':[], 'S':{}, 'E':[], 'A':{}}
    for line in open(path,encoding='utf-8'):
        if line.startswith('PLAYER '):
            a=line.split(); G['players'].append(a[3])
        elif line.startswith('MAPNAME'): G['map']=line.split()[1]
        elif line.startswith('SEED'): G['seed']=int(line.split()[1])
        elif line.startswith('S '):
            parts=line.strip().split('|'); t=int(parts[0].split()[1]); n=len(G['players'])
            ps=[list(map(int,parts[1+i].split())) for i in range(n)]
            ds=[list(map(int,parts[1+n+i].split())) for i in range(n)]
            G['S'][t]=(ps,ds)
        elif line.startswith('A '):
            parts=line.strip().split('|'); t=int(parts[0].split()[1])
            G['A'][t]=[list(map(int,x.split())) for x in parts[1:]]
        elif line.startswith('E '):
            a=line.split(); G['E'].append((int(a[1]),a[2],[int(x) for x in a[3:]]))
        elif line.startswith('END'):
            a=line.split(); G['end']=(int(a[1]),int(a[2]))
    return G
def analyze(dirs, W=40):
    st=collections.defaultdict(lambda: collections.Counter()); blocks=collections.defaultdict(list)
    for d in dirs:
        for f in sorted(glob.glob(os.path.join(d,'g*.txt'))):
            G=parse(f); P=G['players']
            deaths={}
            for t,ty,a in G['E']:
                if ty=='DEATH': deaths[a[0]]=(t,a[1],a[2])
            for t,ty,a in G['E']:
                if ty=='THROW': st[P[a[0]]]['throw']+=1
                if ty=='HIT' and a[0]!=a[1]: st[P[a[0]]]['hit']+=1
                if ty=='SLASH': st[P[a[0]]]['slash']+=1
                if ty=='KICK': st[P[a[0]]]['kick']+=1
                if ty=='DASH': st[P[a[0]]]['dash']+=1
                if ty=='RECALL': st[P[a[0]]]['recall']+=1
                if ty=='BLOCK':
                    b,o,x,y=a[:4]; ps,ds=G['S'][t]
                    ox,oy=ps[o][1],ps[o][2]; dist=math.hypot(ox-x,oy-y)/1000
                    dstate=ds[o][0]
                    win=deaths.get(o); killed= win is not None and win[2]==b and win[0]-t<=W
                    died= deaths.get(b) is not None and deaths[b][0]-t<=W
                    blocks[P[b]].append((round(dist,2),dstate,killed,died,os.path.basename(f),t,G['map']))
            for k,nm in enumerate(P):
                st[nm]['games']+=1
                if k in deaths: st[nm]['death_'+{1:'dart',2:'slash',3:'fall'}[deaths[k][1]]]+=1
    return st,blocks
if __name__=='__main__':
    st,bl=analyze(sys.argv[1:])
    for nm,c in st.items(): print(nm,dict(c))
    for nm,L in bl.items():
        n=len(L); k=sum(x[2] for x in L); dd=sum(x[3] for x in L)
        far=[x for x in L if x[0]>=3.5]; near=[x for x in L if x[0]<2.0]
        def rate(S): return f"{sum(x[2] for x in S)}/{len(S)}"
        print(f"{nm}: 挡镖 {n} 次；40步内砍掉/打死镖主 {k}（{k/max(1,n):.0%}）；40步内自己死 {dd}；落点离主人≥3.5m {rate(far)}，<2m {rate(near)}；镖状态分布 {collections.Counter(x[1] for x in L)}")
