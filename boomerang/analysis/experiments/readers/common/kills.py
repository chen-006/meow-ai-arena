"""击杀现场：python kills.py 名字前缀 回放目录... → 这个 bot 每次击杀：方式、受害者当时状态、秒数"""
import sys, glob, collections
ST = {0:'自由',1:'蓄势',2:'出镖硬直',3:'砍起手',4:'砍硬直',5:'踢起手',6:'踢硬直',7:'冲刺',8:'被踢晕',9:'召回'}
def run(name, dirs, show=6):
    c = collections.Counter(); ex = collections.defaultdict(list)
    for d in dirs:
        for f in sorted(glob.glob(d + '/g*.txt')):
            seats = {}; prevS = None; np = 0
            for l in open(f, encoding='utf-8'):
                if l.startswith('PLAYERS'): np = int(l.split()[1])
                elif l.startswith('PLAYER '):
                    p = l.split(); seats[int(p[1])] = p[3]
                elif l.startswith('S '): prevS = l
                elif l.startswith('E ') and ' DEATH ' in l:
                    p = l.split(); who = int(p[3]); killer = int(p[5])
                    if killer < 0 or not seats.get(killer, '').startswith(name): continue
                    how = {'1':'镖','2':'砍','3':'踢下坑'}[p[4]]
                    pl = prevS.split('|')[1 + who].split(); st = ST.get(int(pl[6]), pl[6])
                    c[(how, st)] += 1; ex[(how, st)].append(f"{f.split(chr(92))[-2]}/{f.split(chr(92))[-1]}@{int(p[1])/20:.2f}s")
    print(name, '击杀', sum(c.values()), dict(c.most_common(10)))
    for k, v in sorted(ex.items(), key=lambda kv: -len(kv[1]))[:show]: print(' ', k, v[:3])
if __name__ == '__main__': run(sys.argv[1], sys.argv[2:])
