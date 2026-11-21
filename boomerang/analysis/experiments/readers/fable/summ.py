"""汇总：python summ.py file.json name1 name2 （1v1 给 name1 视角胜负平+分图；四人局给每人均分）"""
import json, sys
from collections import Counter, defaultdict
rows = json.load(open(sys.argv[1], encoding="utf-8")); names = sys.argv[2:]
if rows[0]["mode"] == 0:
    a, b = names; tot = Counter(); bym = defaultdict(Counter); ev = defaultdict(Counter)
    for r in rows:
        P = {p["name"]: p for p in r["players"]}
        res = "W" if P[a]["rank"] < P[b]["rank"] else "L" if P[a]["rank"] > P[b]["rank"] else "D"
        tot[res] += 1; bym[r["map"]][res] += 1
        for nm in (a, b):
            for k in ("throws","k_dart","blocks","recalls","slashes","kicks","kick_hits","dashes","picks","faults_total"):
                ev[nm][k] += P[nm].get(k, 0)
            if not P[nm]["alive"]: ev[nm]["died_"+P[nm]["cause_name"]] += 1
    n = sum(tot.values())
    print(f"{a} vs {b}: {tot['W']}:{tot['L']}:{tot['D']} n={n} 得分率={(tot['W']+tot['D']/2)/n*100:.1f}%")
    print("  " + "  ".join(f"{m} {c['W']}:{c['L']}:{c['D']}" for m, c in sorted(bym.items())))
    for nm in (a, b): print("  ", nm, dict(ev[nm]))
else:
    sc = defaultdict(list); r4 = Counter(); kills = Counter()
    for r in rows:
        ps = r["players"]
        # 名次分（并列平分）+ 击杀
        ranks = sorted(set(p["rank"] for p in ps))
        for p in ps:
            same = [q for q in ps if q["rank"] == p["rank"]]
            better = sum(1 for q in ps if q["rank"] < p["rank"])
            pts = sum([3,2,1,0][better:better+len(same)]) / len(same)
            sc[p["name"]].append(pts + p["kills"])
            if p["rank"] == max(q["rank"] for q in ps) and len(same) < 4: r4[p["name"]] += 1
            kills[p["name"]] += p["kills"]
    for nm in sc:
        print(f"  {nm:14s} n={len(sc[nm])} 均分={sum(sc[nm])/len(sc[nm]):.3f} 垫底={r4[nm]} 击杀/局={kills[nm]/len(sc[nm]):.2f}")
