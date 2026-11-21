import sys,re
from pathlib import Path
sys.path.insert(0,'.')
import falls
R=Path(sys.argv[1]); name=sys.argv[2]
res=[]
for f in sorted(R.glob('g*.txt')):
    for c,p,t in falls.classify(f,name):
        names,grid,wave,waves,ev=falls.parse(f)
        seat=[k for k,v in names.items() if v==name][0]
        se=(R/'stderr'/f'{f.stem}_p{seat}.txt')
        lines=se.read_text(encoding='utf-8',errors='replace').splitlines() if se.exists() else []
        S={};F={}
        for l in lines:
            m=re.match(r'S (\d+) best=(-?\d+) act=(\d+)',l)
            if m: S[int(m.group(1))]=(int(m.group(2)),int(m.group(3)))
            m=re.match(r'F (\d+) act=(\d+)',l)
            if m: F[int(m.group(1))]=int(m.group(2))
        # find last DASH by seat before t
        td=None
        for tt in range(t,t-12,-1):
            if any(e[0]=='DASH' and int(e[1])==seat for e in ev.get(tt,[])): td=tt;break
        info=''
        if td is not None:
            info=f'dash@{td} search={S.get(td)} final={F.get(td)}'
        res.append((c,f.name,t,info))
for r in res: print(r)
