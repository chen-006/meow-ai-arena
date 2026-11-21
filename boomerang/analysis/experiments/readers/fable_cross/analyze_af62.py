import json, sys
from collections import Counter, defaultdict
from pathlib import Path
sys.dont_write_bytecode = True

rows = json.load(open("<本机路径，未收录>", encoding="utf-8"))
print("Player fields:", list(rows[0]["players"][0].keys()))

agg = {n: Counter() for n in ("astra62", "fable")}
by_map = defaultdict(lambda: {n: Counter() for n in ("astra62", "fable")})
wld = [0, 0, 0] # fable W, L, D

for r in rows:
    mp = r["map"]
    p_by_name = {}
    for p in r["players"]:
        nm = "fable" if p["name"] == "fable" else "astra62"
        p_by_name[nm] = p
        agg[nm]["games"] += 1
        agg[nm]["steps"] += r["steps"]
        by_map[mp][nm]["games"] += 1
        for k, v in p.items():
            if isinstance(v, (int, float)) and k not in ("seat", "team", "rank", "dead_t", "cause", "killer"):
                agg[nm][k] += v
                by_map[mp][nm][k] += v
        if not p["alive"]:
            agg[nm][f"died_{p['cause_name']}"] += 1
            by_map[mp][nm][f"died_{p['cause_name']}"] += 1
    if p_by_name["fable"]["rank"] < p_by_name["astra62"]["rank"]: wld[0] += 1; by_map[mp]["fable"]["wins"] += 1
    elif p_by_name["fable"]["rank"] > p_by_name["astra62"]["rank"]: wld[1] += 1; by_map[mp]["astra62"]["wins"] += 1
    else: wld[2] += 1

print(f"\nTotal 228 games Fable W:L:D = {wld}")
keys = ["alive", "kills", "k_dart", "k_slash", "k_fall", "ret_kills", "curve_kills",
        "throws", "slashes", "blocks", "kicks", "kick_hits", "dashes", "dash_melees", "gap_dash",
        "recalls", "call_done", "catches", "picks", "drops", "clashes",
        "empty_steps", "grass_steps", "hidden_steps", "died_dart", "died_slash", "died_fall"]

print(f"\n{'Metric':15s} | {'Astra62 Total (Avg)':22s} | {'Fable Total (Avg)':22s}")
print("-" * 65)
for k in keys:
    av = agg["astra62"][k]
    fv = agg["fable"][k]
    print(f"{k:15s} | {av:7.0f} ({av/228:6.2f}/g)     | {fv:7.0f} ({fv/228:6.2f}/g)")

print("\nBy map key comparisons (Astra62 vs Fable):")
for mp in sorted(by_map.keys()):
    A = by_map[mp]["astra62"]
    F = by_map[mp]["fable"]
    print(f"\n[{mp}] Fable {F['wins']}:{A['wins']} | "
          f"Throws A={A['throws']}/F={F['throws']} | Blocks A={A['blocks']}/F={F['blocks']} | "
          f"Recalls(done) A={A['recalls']}({A['call_done']})/F={F['recalls']}({F['call_done']}) | "
          f"Dashes(gap) A={A['dashes']}({A['gap_dash']})/F={F['dashes']}({F['gap_dash']}) | "
          f"EmptySteps A={A['empty_steps']/38:.1f}/F={F['empty_steps']/38:.1f} | "
          f"HiddenSteps A={A['hidden_steps']/38:.1f}/F={F['hidden_steps']/38:.1f} | "
          f"Fable died dart/slash/fall={F['died_dart']}/{F['died_slash']}/{F['died_fall']} | "
          f"RetKills A={A['ret_kills']}/F={F['ret_kills']}")

# Also inspect the 12 replays in astra62_fable_twin
rep_dir = Path("<本机路径，未收录>")
print("\n=== Replays in astra62_fable_twin ===")
for rf in sorted(rep_dir.glob("g*.txt"), key=lambda p: int(p.stem[1:])):
    txt = rf.read_text(encoding="utf-8").splitlines()
    hdr = [ln for ln in txt[:15] if ln.startswith(("MAP ", "SEED ", "BOT ", "END ", "RESULT "))]
    tail = [ln for ln in txt[-10:] if ln.startswith(("END ", "RESULT "))]
    blocks = [ln for ln in txt if " BLOCK " in ln]
    recalls = [ln for ln in txt if " RECALL " in ln]
    cuts = [ln for ln in txt if " CUT " in ln]
    hits = [ln for ln in txt if " HIT " in ln]
    falls = [ln for ln in txt if " FALL " in ln]
    print(f"{rf.name}: blocks={len(blocks)} recalls={len(recalls)} cuts={len(cuts)} hits={len(hits)} falls={len(falls)} | {tail}")
