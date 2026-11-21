# 找连招：python seq.py 回放目录 名字 —— 列出 撞镖→捡镖→砍杀，以及 冲刺→挥砍→砍中
import sys, glob, os
sys.path.insert(0, os.path.dirname(__file__)); from rp import parse
d, tg = sys.argv[1], sys.argv[2]
n1 = n2 = 0
for fn in sorted(glob.glob(os.path.join(d, 'g*.txt')), key=lambda f: int(os.path.basename(f)[1:-4])):
    R = parse(fn); seat = [s for s, n in R['names'].items() if n == tg][0]
    ev = [(t, typ, a) for t in sorted(R['E']) for typ, a in R['E'][t]]
    for i, (t, typ, a) in enumerate(ev):
        if typ == 'CUT' and a[0] == seat:
            prev = [(tt, ty, b) for tt, ty, b in ev if t - 40 <= tt <= t and (b[0] == seat or (ty == 'CLASH' and b[1] == seat))]
            kinds = [ty for tt, ty, b in prev if b[0] == seat]
            tags = []
            if 'CLASH' in kinds and 'PICK' in kinds: tags.append('撞镖→捡镖→砍'); n1 += 1
            dash = [tt for tt, ty, b in prev if ty == 'DASH' and b[0] == seat and t - tt <= 10]
            if dash: tags.append('冲刺斩'); n2 += 1
            if tags:
                print(os.path.basename(fn), R['map'], '砍中 t=%d(%.2fs)' % (t, t/20), tags, [(tt, ty) for tt, ty, b in prev if b[0] == seat][-8:])
print('撞镖→捡镖→砍', n1, '冲刺斩', n2)
