import sys
sys.path.insert(0, "<本机路径，未收录>")
from rp import parse, DST
from collections import Counter
from pathlib import Path
import math
ST = ["FREE","THROW","THROWN","SLASH","SLASHED","KICK","KICKED","DASH","STUN","RECALL","DEAD"]
def run(D, me_name, opp_name):
    out = Counter(); rows = []
    for f in sorted(Path(D).glob("g*.txt"), key=lambda p: int(p.stem[1:])):
        h, S, A, E = parse(f)
        pl = h["players"]
        if me_name not in pl or opp_name not in pl: continue
        me = pl.index(me_name); op = pl.index(opp_name)
        last_rel = None; last_rel_t = None
        for t in sorted(E):
            for typ, a in E[t]:
                if typ == "RELEASE" and a[0] == op:
                    last_rel = (a[1], a[2]); last_rel_t = t
                    mp = S[t][0][me]
                    last_rel_d = math.hypot(mp[1]-a[1], mp[2]-a[2])
                if typ == "HIT" and a[1] == me:
                    ps, ds = S[t]
                    mst = ST[ps[me][6]]
                    armed = DST[ds[me][0]]
                    dstate = {1:"OUT",2:"BACK",6:"CALL"}.get(a[2], str(a[2]))
                    flight = t - last_rel_t if last_rel_t is not None else -1
                    rows.append((f.stem, h["map"], t, mst, armed, dstate, flight, int(last_rel_d) if last_rel else -1))
    return rows
if __name__ == "__main__":
    rows = run(sys.argv[1], sys.argv[2], sys.argv[3])
    c1 = Counter(r[3] for r in rows); c2 = Counter(r[4] for r in rows); c3 = Counter(r[5] for r in rows)
    print("n hits", len(rows)); print(" my state", c1); print(" my dart", c2); print(" killing dart", c3)
    outs = [r for r in rows if r[5] == "OUT"]
    import statistics
    if outs:
        ds = sorted(r[7] for r in outs)
        print(" OUT throw dist at release: median", statistics.median(ds), "quartiles", ds[len(ds)//4], ds[3*len(ds)//4])
        print(" <4500:", sum(d < 4500 for d in ds), ">=4500:", sum(d >= 4500 for d in ds))
    for r in rows[:60]: print("  ", r)
