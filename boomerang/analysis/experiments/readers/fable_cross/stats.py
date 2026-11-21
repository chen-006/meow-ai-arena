import json
from collections import defaultdict, Counter
from pathlib import Path

R62 = Path("<本机路径，未收录>")
SUPER = Path("<本机路径，未收录>")

res62 = json.load(open(R62 / "海选成绩.json", encoding="utf-8"))
rows62 = json.load(open(R62 / "逐局结果.json", encoding="utf-8"))
obs62 = json.load(open(R62 / "obs.json", encoding="utf-8"))
win21 = json.load(open(SUPER / "胜率表.json", encoding="utf-8"))
super_res = json.load(open(SUPER / "全员榜.json", encoding="utf-8"))

print("=== 1. 海选比赛_62 Fable / Astra / Opus 成绩 ===")
for k in ("astra", "fable", "opus", "carbon", "sol"):
    p = res62["players"][k]
    f = res62["faults"][k]
    print(f"{k:8s}: rank={p['rank']} rating={p['rating']:+.2f} CI=[{p['rating_ci95'][0]:+.2f},{p['rating_ci95'][1]:+.2f}] "
          f"duel={p['duel_rating']:+.2f} ffa={p['ffa_rating']:+.2f} faults={f['faults']}/{f['dead']}/maxms={f['maxms']}")

# 1v1 stats for fable in 海选比赛_62
duel_tot = [0, 0, 0] # W, L, D
duel_causes = Counter()
duel_opp = defaultdict(lambda: [0, 0, 0])
duel_map = defaultdict(lambda: [0, 0, 0])
duel_pair_map = defaultdict(lambda: defaultdict(lambda: [0, 0, 0]))
duel_pair_cause = defaultdict(lambda: [Counter(), Counter()]) # [fable_deaths, opp_deaths]
duel_faults = 0

ffa_games = 0
ffa_ranks_incl_tie = Counter()
ffa_ranks_sole = Counter()
ffa_causes = Counter()
ffa_kills = 0
ffa_pts = 0.0
ffa_map_pts = defaultdict(list)
ffa_map_4th_incl = Counter()
ffa_map_4th_sole = Counter()
ffa_faults = 0

# Also FFA stats for astra and opus for comparison
bot_ffa = defaultdict(lambda: {"games":0, "r1_incl":0, "r1_sole":0, "r4_incl":0, "r4_sole":0, "kills":0, "pts":0.0, "causes":Counter(), "map_pts":defaultdict(list), "map_r4_incl":Counter()})

for g, (ob_meta, ob_val) in zip(rows62, obs62):
    pls = {p["name"]: p for p in g["players"]}
    if g["mode"] == 0:
        if "fable" not in pls:
            continue
        fb = pls["fable"]
        op_name = [n for n in g["names"] if n != "fable"][0]
        op = pls[op_name]
        duel_faults += fb["faults"]
        if not fb["alive"]:
            duel_causes[fb["cause"]] += 1
            duel_pair_cause[op_name][0][fb["cause"]] += 1
        if not op["alive"]:
            duel_pair_cause[op_name][1][op["cause"]] += 1
        if fb["rank"] < op["rank"]:
            idx = 0
        elif fb["rank"] > op["rank"]:
            idx = 1
        else:
            idx = 2
        duel_tot[idx] += 1
        duel_opp[op_name][idx] += 1
        duel_map[g["map"]][idx] += 1
        duel_pair_map[op_name][g["map"]][idx] += 1
    elif g["mode"] == 1:
        rc = Counter(p["rank"] for p in g["players"])
        max_r = max(p["rank"] for p in g["players"])
        # Note: in dart arena, rank is 1..4; tied for last might have rank=3 if 2 players died same step at end!
        # Let's check how rank is assigned in rows62!
        for bname in ("astra", "fable", "opus", "carbon", "sol"):
            if bname in pls:
                bp = pls[bname]
                st = bot_ffa[bname]
                st["games"] += 1
                st["kills"] += bp["kills"]
                st["pts"] += ob_val[bname]
                st["map_pts"][g["map"]].append(ob_val[bname])
                if not bp["alive"]:
                    st["causes"][bp["cause"]] += 1
                # Check 1st and last (4th)
                # How many players beat bp? rank - 1! How many tied with bp? rc[bp["rank"]]
                # Wait: let's inspect rank values!
                if bp["rank"] == 1:
                    st["r1_incl"] += 1
                    if rc[1] == 1:
                        st["r1_sole"] += 1
                # Last place: bp["rank"] + rc[bp["rank"]] - 1 == 4 (i.e. occupies slot 4)
                if bp["rank"] + rc[bp["rank"]] - 1 == 4:
                    st["r4_incl"] += 1
                    st["map_r4_incl"][g["map"]] += 1
                if bp["rank"] == 4 and rc[4] == 1:
                    st["r4_sole"] += 1
        if "fable" in pls:
            ffa_faults += pls["fable"]["faults"]

w, l, d = duel_tot
n_duel = w + l + d
print(f"\nFable 1v1 总计 ({n_duel} 局): {w}:{l}:{d} | 纯胜率={w/n_duel*100:.2f}% | 得分率={(w+0.5*d)/n_duel*100:.2f}% | faults={duel_faults}")
print("Fable 1v1 死因:", dict(duel_causes))
print("\nFable 1v1 对各对手:")
for op in res62["ranking"]:
    if op == "fable": continue
    ww, ll, dd = duel_opp[op]
    tot = ww + ll + dd
    print(f"  vs {op:10s}: {ww:3d}:{ll:3d}:{dd:2d} | 纯胜率={ww/tot*100:5.2f}% | 得分率={(ww+0.5*dd)/tot*100:5.2f}% | Fable死={dict(duel_pair_cause[op][0])} | 对手死={dict(duel_pair_cause[op][1])}")

print("\nFable 1v1 分地图:")
for mp, (ww, ll, dd) in sorted(duel_map.items()):
    tot = ww + ll + dd
    print(f"  {mp:12s}: {ww:3d}:{ll:3d}:{dd:2d} | 得分率={(ww+0.5*dd)/tot*100:5.2f}%")

for target in ("astra", "opus", "sol"):
    print(f"\nFable vs {target} 分地图 (每图38局):")
    for mp, (ww, ll, dd) in sorted(duel_pair_map[target].items()):
        tot = ww + ll + dd
        print(f"  {mp:12s}: {ww:2d}:{ll:2d}:{dd:2d} (Fable得分率={(ww+0.5*dd)/tot*100:5.2f}%, {target}得分率={(ll+0.5*dd)/tot*100:5.2f}%)")

print(f"\n=== 2. 海选比赛_62 四人局对比 (fable faults={ffa_faults}) ===")
for bname in ("astra", "fable", "opus", "carbon", "sol"):
    st = bot_ffa[bname]
    g = st["games"]
    print(f"{bname:8s}: {g}局 | 夺冠(含并列)={st['r1_incl']}({st['r1_incl']/g*100:.2f}%), 独享={st['r1_sole']}({st['r1_sole']/g*100:.2f}%) | "
          f"垫底(占第4档/含并列)={st['r4_incl']}({st['r4_incl']/g*100:.2f}%), 独享第4={st['r4_sole']}({st['r4_sole']/g*100:.2f}%) | "
          f"场均分={st['pts']/g:.3f} | 场均杀={st['kills']/g:.3f} | 死因={dict(st['causes'])}")
    mp_str = ", ".join(f"{m}:{sum(v)/len(v):.3f}(垫底{st['map_r4_incl'][m]})" for m, v in sorted(st["map_pts"].items()))
    print(f"   分图均分: {mp_str}")

# Pairwise FFA in 海选比赛_62
print("\n=== 3. 海选比赛_62 四人局同场两两比较 (Fable视角) ===")
for target in ("astra", "opus", "carbon", "sol"):
    fw = fd = fl = 0
    for g, (ob_meta, ob_val) in zip(rows62, obs62):
        if g["mode"] == 1 and "fable" in ob_val and target in ob_val:
            if ob_val["fable"] > ob_val[target]: fw += 1
            elif ob_val["fable"] < ob_val[target]: fl += 1
            else: fd += 1
    tot = fw + fd + fl
    print(f"  Fable vs {target:8s} (同桌 {tot} 局): Fable {fw}胜 {fd}平 {fl}负 | Fable得分率={(fw+0.5*fd)/tot*100:.2f}% ({target}得分率={(fl+0.5*fd)/tot*100:.2f}%)")

print("\n=== 4. 全员大混战 (21人) 精确两两数据 ===")
for mode_k in ("一对一", "四人局"):
    for a, b in (("astra62", "fable"), ("fable", "opus"), ("fable", "astra"), ("fable", "sol"), ("fable", "sol62")):
        w, d, l = win21[mode_k][a][b]
        tot = w + d + l
        print(f"  [{mode_k}] {a:8s} vs {b:8s} ({tot:4d}): {w}胜 {d}平 {l}负 | {a}纯胜率={w/tot*100:.2f}% | {a}得分率={(w+0.5*d)/tot*100:.2f}%")
