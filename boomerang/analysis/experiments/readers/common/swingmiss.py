import sys,glob,collections,math
name=sys.argv[1]; dists=[]; angs=[]; ex=[]
for d in sys.argv[2:]:
    for f in sorted(glob.glob(d+'/g*.txt')):
        seats={}; S={}; slash={}; np=0
        L=open(f,encoding='utf-8').read().splitlines()
        for l in L:
            if l.startswith('PLAYERS'): np=int(l.split()[1])
            elif l.startswith('PLAYER '): p=l.split(); seats[int(p[1])]=p[3]
            elif l.startswith('S '): S[int(l.split()[1])]=l
        me=[k for k,v in seats.items() if v.startswith(name)][0]
        for l in L:
            if l.startswith('E '):
                p=l.split(); t=int(p[1])
                if p[2]=='SLASH' and int(p[3])==me: slash[t]=(int(p[4]),int(p[5]))
                if p[2]=='SWING' and int(p[3])==me and p[4]=='1' and p[5]=='0':
                    st=S.get(t)
                    if not st: continue
                    parts=st.split('|'); m=parts[1+me].split()
                    for o in range(np):
                        if o==me: continue
                        q=parts[1+o].split()
                        if q[0]!='1': continue
                        dx=int(q[1])-int(m[1]); dy=int(q[2])-int(m[2]); dd=math.hypot(dx,dy)
                        a=slash.get(t-1,(0,0)); 
                        ang=abs(math.degrees(math.atan2(dy,dx)-math.atan2(a[1],a[0])))%360; ang=min(ang,360-ang)
                        dists.append(dd); angs.append(ang)
                        if len(ex)<6: ex.append((f[-30:],t/20,round(dd),round(ang)))
dists.sort(); angs.sort()
n=len(dists)
print(name,'missed slashes',n,'dist median',round(dists[n//2]),'>1500:',sum(d>1500 for d in dists),'angle>60:',sum(a>60 for a in angs))
print(ex)
