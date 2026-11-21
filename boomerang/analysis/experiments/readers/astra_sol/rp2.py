import sys,glob,os,collections,math
sys.path.insert(0,os.path.dirname(__file__))
from rp import parse
# 1v1：按"我有镖/对手有镖"四种情形统计平均距离；列出挡镖后击杀的例子
def run(d,focus):
    dist=collections.defaultdict(list); ex=[]
    for f in sorted(glob.glob(os.path.join(d,'g*.txt'))):
        G=parse(f);P=G['players']
        if focus not in P: continue
        me=P.index(focus); op=1-me
        for t,(ps,ds) in G['S'].items():
            if not(ps[me][0] and ps[op][0]) or t<20: continue
            a=ds[me][0]==0; b=ds[op][0]==0
            dist[(a,b)].append(math.hypot(ps[me][1]-ps[op][1],ps[me][2]-ps[op][2])/1000)
        deaths={a[0]:(t,a[1],a[2]) for t,ty,a in G['E'] if ty=='DEATH'}
        for t,ty,a in G['E']:
            if ty=='BLOCK' and a[0]==me:
                ps,ds=G['S'][t]; o=a[1]; r=math.hypot(ps[o][1]-a[2],ps[o][2]-a[3])/1000
                k=deaths.get(o)
                if k and k[2]==me and k[0]-t<=40: ex.append((k[0]-t,round(r,2),ds[o][0],G['map'],G['seed'],os.path.basename(f),t,k[0],k[1]))
    lab={(True,True):'双方有镖',(True,False):'我有镖/对手空手',(False,True):'我空手/对手有镖',(False,False):'双方空手'}
    for k,v in sorted(dist.items()):
        v.sort(); print(focus,lab[k],'样本步',len(v),'平均距离 %.2f m 中位 %.2f'%(sum(v)/len(v),v[len(v)//2]))
    ex.sort()
    print('挡镖→击杀 例子（相隔步, 落点离主人m, 镖状态, 图, 种子, 文件, 挡镖步, 击杀步, 方式）:')
    for e in ex[:12]: print(' ',e)
    if ex:
        gaps=[e[0] for e in ex]; gaps.sort(); print('  相隔步中位',gaps[len(gaps)//2],'共',len(ex))
if __name__=='__main__': run(sys.argv[1],sys.argv[2])
