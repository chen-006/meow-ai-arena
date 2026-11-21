import sys, json
sys.path.insert(0, "<本机路径，未收录>")
from rp import parse, DST
from collections import Counter, defaultdict
from pathlib import Path
D = Path("<本机路径，未收录>")
tot = Counter(); bym = defaultdict(Counter)
lst = []
for f in sorted(D.glob("g*.txt"), key=lambda p: int(p.stem[1:])):
    h, S, A, E = parse(f)
    pl = h["players"]; fs = pl.index("fable"); as_ = pl.index("astra62")
    winner = int(h["end"][2])
    res = "W" if winner == fs else ("L" if winner == as_ else "D")
    tot[res] += 1; bym[h["map"]][res] += 1
    c = Counter()
    death = None
    for t in sorted(E):
        for typ, a in E[t]:
            if typ == "BLOCK":
                blk, own = a[0], a[1]
                st = DST[S[t][1][own][0]] if t in S else "?"
                # did the blocker move in previous 6 steps?
                mv = 0
                if t-6 in S:
                    p0 = S[t-6][0][blk]; p1 = S[t][0][blk]
                    mv = abs(p0[1]-p1[1]) + abs(p0[2]-p1[2])
                who = "A_blocks_F" if blk == as_ else "F_blocks_A"
                c[f"{who}_{st}"] += 1
                c[f"{who}_moved" if mv > 300 else f"{who}_still"] += 1
            elif typ in ("THROW","RECALL","CALL","CANCEL","SLASH","KICK","DASH","PICK","CLASH","DROP","CATCH"):
                who = "A" if a[0] == as_ else "F"
                c[f"{who}_{typ}"] += 1
            elif typ == "DEATH":
                if a[0] == fs: death = (t, ["","dart","slash","fall"][a[1]])
    lst.append((f.stem, h["map"], res, death, dict(c)))
    tot.update({k: v for k, v in c.items()})
print("result", tot["W"], tot["L"], tot["D"])
for m in sorted(bym): print(" ", m, bym[m]["W"], bym[m]["L"], bym[m]["D"])
keys = sorted(k for k in tot if k not in "WLD")
for k in keys: print(f"  {k:22s} {tot[k]}")
dc = Counter(d[1] for _,_,_,d,_ in lst if d)
print("fable death causes", dc)
for g, m, r, d, c in lst:
    if c.get("A_blocks_F_RISE",0)+c.get("A_blocks_F_CALL",0) >= 2 or c.get("A_blocks_F_OUT",0) >= 2:
        print(g, m, r, d, {k:v for k,v in c.items() if "block" in k.lower() or "RECALL" in k or "CALL" in k})
