import sys,glob,collections
def run(name,dirs):
    c=collections.Counter()
    for d in dirs:
        for f in glob.glob(d+'/g*.txt'):
            seats={}
            for l in open(f,encoding='utf-8'):
                if l.startswith('PLAYER '): p=l.split(); seats[p[1]]=p[3]
                elif l.startswith('E '):
                    p=l.split()
                    if p[2]=='SWING' and seats.get(p[3],'').startswith(name):
                        kind='砍' if p[4]=='1' else '踢'; c[kind]+=1; c[kind+('中' if p[5]!='0' else '空')]+=1
                    if p[2]=='BLOCK' and seats.get(p[3],'').startswith(name): c['挡下镖']+=1
                    if p[2]=='CUT' and seats.get(p[3],'').startswith(name): c['砍死人']+=1
    print(name,dict(c))
run(sys.argv[1],sys.argv[2:])
