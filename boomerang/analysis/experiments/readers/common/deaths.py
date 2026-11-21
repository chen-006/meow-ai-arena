"""死因现场：python deaths.py 名字前缀 回放目录... → 出局那一步自己在什么状态、手里有没有镖"""
import sys, glob, collections
ST = {0:'自由',1:'蓄势',2:'出镖硬直',3:'砍起手',4:'砍硬直',5:'踢起手',6:'踢硬直',7:'冲刺',8:'被踢晕',9:'召回'}
DS = {0:'持镖',1:'镖去程',2:'镖回程',3:'镖在地上',4:'召回中',5:'无',6:'召回飞行'}
def run(name, dirs):
    c = collections.Counter(); cm = collections.Counter(); n = 0; tm = []
    for d in dirs:
        for f in glob.glob(d + '/g*.txt'):
            seats = {}; prevS = None; np = 0
            for l in open(f, encoding='utf-8'):
                if l.startswith('PLAYERS'): np = int(l.split()[1])
                elif l.startswith('PLAYER '):
                    p = l.split(); seats[int(p[1])] = p[3]
                elif l.startswith('S '):
                    prevS = l
                elif l.startswith('E ') and ' DEATH ' in l:
                    p = l.split(); who = int(p[3])
                    if not seats.get(who, '').startswith(name): continue
                    t = int(p[1]); how = {'1':'镖','2':'砍','3':'坑'}[p[4]]
                    parts = prevS.split('|')
                    pl = parts[1 + who].split(); dt = parts[1 + np + who].split()
                    st = ST.get(int(pl[6]), pl[6]); dst = DS.get(int(dt[0]), dt[0])
                    c[(how, st)] += 1; cm[(how, dst)] += 1; n += 1; tm.append(t)
    print(name, '出局', n, '次；中位出局步', sorted(tm)[len(tm)//2] if tm else '-', '；4 秒内出局', sum(1 for t in tm if t < 80))
    print('  死法×当时状态', dict(c.most_common(8)))
    print('  死法×镖状态', dict(cm.most_common(8)))
if __name__ == '__main__':
    run(sys.argv[1], sys.argv[2:])
