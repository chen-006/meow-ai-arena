import sys,glob,os,collections
PTS=[3,2,1,0]
def game(f):
    P=[];R={}
    for line in open(f,encoding='utf-8'):
        if line.startswith('PLAYER '): P.append(line.split()[3])
        elif line.startswith('RESULT '):
            a=list(map(int,line.split()[1:])); R[a[0]]=a  # seat rank deadstep cause killer kills
    return P,R
def summ(d,name):
    n=0;sole4=0;first=0;score=0;kills=0;keys={}
    for f in sorted(glob.glob(os.path.join(d,'g*.txt'))):
        P,R=game(f)
        s=[i for i,p in enumerate(P) if p==name or p.startswith(name)][0]
        n+=1
        # 名次分：按 rank 并列平分
        ranks=collections.Counter(R[k][1] for k in R)
        r=R[s][1]; cnt=ranks[r]; pts=sum(PTS[r-1:r-1+cnt])/cnt
        sc=pts+R[s][5]; score+=sc; kills+=R[s][5]
        if r==1: first+=1
        if r==4 and ranks[4]==1: sole4+=1
        keys[os.path.basename(f)]=sc
    print(f"{name:14s} 局数 {n} 第一(含并列) {first} ({first/n:.1%})  独享第四 {sole4} ({sole4/n:.1%})  场均分 {score/n:.3f}  场均击杀 {kills/n:.3f}")
    return keys
if __name__=='__main__':
    for d,nm in zip(sys.argv[1::2],sys.argv[2::2]): summ(d,nm)
