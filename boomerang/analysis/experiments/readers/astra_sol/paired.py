import sys,glob,os
def res(d,me):
    out={}
    for f in glob.glob(os.path.join(d,'g*.txt')):
        P=[];w=None
        for line in open(f,encoding='utf-8'):
            if line.startswith('PLAYER '): P.append(line.split()[3])
            elif line.startswith('END '): w=int(line.split()[2])
        m=(P.index(me) if me in P else [i for i,p in enumerate(P) if p.startswith(me)][0])
        out[os.path.basename(f)]= 0.5 if w<0 else (1.0 if w==m else 0.0)
    return out
base=res(sys.argv[1],sys.argv[2])
for d,nm in zip(sys.argv[3::2],sys.argv[4::2]):
    v=res(d,nm); keys=sorted(set(base)&set(v))
    up=sum(1 for k in keys if v[k]>base[k]); dn=sum(1 for k in keys if v[k]<base[k])
    print(nm,'局数',len(keys),'基线得分率 %.1f%%'%(100*sum(base[k] for k in keys)/len(keys)),'变体 %.1f%%'%(100*sum(v[k] for k in keys)/len(keys)),'变好',up,'变差',dn,'不变',len(keys)-up-dn)
