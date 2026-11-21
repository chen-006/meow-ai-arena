import json, sys
from collections import Counter, defaultdict
R = json.load(open("<本机路径，未收录>", encoding="utf-8"))
print("total games", len(R), Counter(r["mode"] for r in R))
def h2h(a, b):
    tot = Counter(); bym = defaultdict(Counter); causes = Counter()
    for r in R:
        if r["mode"] != 0 or set(r["names"]) != {a, b}: continue
        P = {p["name"]: p for p in r["players"]}
        m = r["map"]
        res = "W" if P[a]["rank"] < P[b]["rank"] else ("L" if P[a]["rank"] > P[b]["rank"] else "D")
        tot[res] += 1; bym[m][res] += 1
        causes[(a, P[a]["cause"])] += 1; causes[(b, P[b]["cause"])] += 1
        bym[m]["steps"] += r["steps"]
    n = sum(tot[x] for x in "WLD")
    print(f"\n== {a} vs {b}: {tot['W']}:{tot['L']}:{tot['D']}  score%={(tot['W']+tot['D']/2)/max(1,n)*100:.1f}  n={n}")
    for m in sorted(bym):
        c = bym[m]; k = c['W']+c['L']+c['D']
        print(f"  {m:12s} {c['W']}:{c['L']}:{c['D']}  avgsteps={c['steps']/k:.0f}")
    print("  causes", dict(causes))
for a, b in [("astra","fable"),("fable","opus"),("astra","opus"),("carbon","hydrogen"),("fable","carbon"),("opus","carbon")]:
    h2h(a, b)
# FFA
ffa = defaultdict(Counter); ffam = defaultdict(lambda: defaultdict(Counter))
for r in R:
    if r["mode"] != 1: continue
    ps = r["players"]
    for p in ps:
        nm = p["name"]
        ffa[nm]["n"] += 1
        ffa[nm]["rank"+str(p["rank"])] += 1
        ffa[nm]["kills"] += p["kills"]
        ffa[nm]["cause_"+p["cause"]] += 1
        ffam[nm][r["map"]]["n"] += 1; ffam[nm][r["map"]]["kills"] += p["kills"]
print("\nFFA rank keys sample", list(ffa["fable"].keys()))
for nm in ["astra","fable","opus","carbon","hydrogen","sol"]:
    c = ffa[nm]; n = c["n"]
    print(nm, n, {k: round(v/n*100,1) for k, v in sorted(c.items()) if k!="n"})
