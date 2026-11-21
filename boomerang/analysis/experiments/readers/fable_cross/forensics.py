import json, sys, math
from collections import Counter, defaultdict
from pathlib import Path
sys.dont_write_bytecode = True

OUT = Path("<本机路径，未收录>")

# 1. Analyze Fable vs Opus 228 games
rows_fo = json.load(open(OUT / "fable_opus_228.json", encoding="utf-8"))
agg_fo = {n: Counter() for n in ("fable", "opus")}
map_fo = defaultdict(lambda: {n: Counter() for n in ("fable", "opus")})
wld_fo = [0, 0, 0]

A_ROOT = Path("<本机路径，未收录>")
for r in rows_fo:
    mp = r["map"]
    pb = {p["name"]: p for p in r["players"]}
    for nm, p in pb.items():
        agg_fo[nm]["games"] += 1
        for k, v in p.items():
            if isinstance(v, (int, float)) and k not in ("seat", "team", "rank", "dead_t", "cause", "killer"):
                agg_fo[nm][k] += v
                map_fo[mp][nm][k] += v
        if not p["alive"]:
            agg_fo[nm][f"died_{p['cause_name']}"] += 1
            map_fo[mp][nm][f"died_{p['cause_name']}"] += 1
    if pb["fable"]["rank"] < pb["opus"]["rank"]: wld_fo[0] += 1; map_fo[mp]["fable"]["wins"] += 1
    elif pb["fable"]["rank"] > pb["opus"]["rank"]: wld_fo[1] += 1; map_fo[mp]["opus"]["wins"] += 1
    else: wld_fo[2] += 1

print(f"=== Fable vs Opus 228 (re-run telemetry) W:L:D = {wld_fo} ===")
keys = ["alive", "kills", "k_dart", "k_slash", "k_fall", "ret_kills",
        "throws", "slashes", "blocks", "kicks", "kick_hits", "dashes", "dash_melees",
        "recalls", "picks", "drops", "clashes", "empty_steps", "grass_steps", "hidden_steps",
        "died_dart", "died_slash", "died_fall"]
print(f"{'Metric':15s} | {'Fable Total (Avg)':22s} | {'Opus Total (Avg)':22s}")
print("-" * 65)
for k in keys:
    fv, ov = agg_fo["fable"][k], agg_fo["opus"][k]
    print(f"{k:15s} | {fv:7.0f} ({fv/228:6.2f}/g)     | {ov:7.0f} ({ov/228:6.2f}/g)")

print("\nFable vs Opus by map:")
for mp in sorted(map_fo.keys()):
    F, O = map_fo[mp]["fable"], map_fo[mp]["opus"]
    print(f"  [{mp:11s}] Fable {F['wins']:2d}:{O['wins']:2d} | Throws F={F['throws']:3d}/O={O['throws']:3d} | "
          f"Blocks F={F['blocks']:2d}/O={O['blocks']:2d} | Recalls F={F['recalls']:3d}/O={O['recalls']:3d} | "
          f"Picks F={F['picks']:2d}/O={O['picks']:2d} | Drops F={F['drops']:2d}/O={O['drops']:2d} | "
          f"Opus died dart/slash/fall={O['died_dart']}/{O['died_slash']}/{O['died_fall']}")

# 2. Parse replay events & death circumstances in Astra62 vs Fable (60 replays)
ST_NAME = ["FREE", "THROW", "THROWN", "SLASH", "SLASHED", "KICK", "KICKED", "DASH", "STUN", "RECALL", "DEAD"]
DST_NAME = ["HAND", "OUT", "BACK", "GROUND", "RISE", "GONE", "CALL"]

def parse_replay(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    mp = seed = ""
    bots = {}
    states = {}
    actions = {}
    events = defaultdict(list)
    end_line = ""
    results = {}
    for ln in lines:
        if ln.startswith("MAPNAME "): mp = ln.split()[1]
        elif ln.startswith("SEED "): seed = int(ln.split()[1])
        elif ln.startswith("PLAYER "):
            w = ln.split()
            bots[int(w[1])] = w[3]
        elif ln.startswith("S "):
            parts = ln.split(" | ")
            t = int(parts[0].split()[1])
            p0 = [int(x) for x in parts[1].split()]
            p1 = [int(x) for x in parts[2].split()]
            d0 = [int(x) for x in parts[3].split()]
            d1 = [int(x) for x in parts[4].split()]
            states[t] = ([p0, p1], [d0, d1])
        elif ln.startswith("A "):
            parts = ln.split(" | ")
            t = int(parts[0].split()[1])
            a0 = [int(x) for x in parts[1].split()]
            a1 = [int(x) for x in parts[2].split()]
            actions[t] = [a0, a1]
        elif ln.startswith("E "):
            w = ln.split()
            events[int(w[1])].append(w[2:])
        elif ln.startswith("END "):
            end_line = ln
        elif ln.startswith("RESULT "):
            w = ln.split()
            results[int(w[1])] = [int(x) for x in w[2:]]
    return dict(path=path, map=mp, seed=seed, bots=bots, states=states, actions=actions, events=events, end=end_line, results=results)

print("\n=== Forensic Analysis of 60 Replays: Astra62 vs Fable ===")
af_dir = OUT / "replays_astra62_fable"
death_ctx = Counter()
interesting_af = []

for rf in sorted(af_dir.glob("g*.txt"), key=lambda p: int(p.stem[1:])):
    rep = parse_replay(rf)
    fseat = 0 if rep["bots"][0] == "fable" else 1
    aseat = 1 - fseat
    res_f = rep["results"][fseat] # rank, dead_t, cause, killer, kills
    blocks_a = sum(1 for t, evs in rep["events"].items() for e in evs if e[0] == "BLOCK" and int(e[1]) == aseat)
    blocks_f = sum(1 for t, evs in rep["events"].items() for e in evs if e[0] == "BLOCK" and int(e[1]) == fseat)
    recalls_f = sum(1 for t, evs in rep["events"].items() for e in evs if e[0] == "RECALL" and int(e[1]) == fseat)
    if res_f[0] == 1:
        interesting_af.append((rf.name, rep["map"], rep["seed"], "FABLE_WIN", res_f, blocks_a, blocks_f, recalls_f))
        continue
    dt = res_f[1]
    cause = res_f[2] # 1=dart, 2=slash, 3=fall
    if dt >= 0 and dt in rep["states"]:
        ps, ds = rep["states"][dt]
        fp, ap = ps[fseat], ps[aseat]
        fd, ad = ds[fseat], ds[aseat]
        fst = ST_NAME[fp[6]]
        fdst = DST_NAME[fd[0]]
        dist = math.hypot(fp[1] - ap[1], fp[2] - ap[2]) / 1000.0
        # Check if fable threw in last 6 steps or recalled in last 15 steps or Astra caught dart in last 8 steps
        recent_f_throw = any(e[0] == "THROW" and int(e[1]) == fseat for tt in range(max(0, dt-8), dt+1) for e in rep["events"].get(tt, []))
        recent_f_recall = any(e[0] == "RECALL" and int(e[1]) == fseat for tt in range(max(0, dt-15), dt+1) for e in rep["events"].get(tt, []))
        recent_a_catch = any(e[0] in ("CATCH", "PICK") and int(e[1]) == aseat for tt in range(max(0, dt-10), dt+1) for e in rep["events"].get(tt, []))
        death_ctx[(cause, fst, fdst, recent_f_throw, recent_f_recall, recent_a_catch)] += 1
        if blocks_a >= 2 or recalls_f >= 3 or cause in (2, 3) or rep["map"] in ("icefloes", "stonecircle", "ruins"):
            interesting_af.append((rf.name, rep["map"], rep["seed"], f"DIED_c{cause}_t{dt}({dt/20:.2f}s)_{fst}_dart{fdst}_dist{dist:.2f}m",
                                   f"blkA={blocks_a},recF={recalls_f},fThr8={recent_f_throw},aCat10={recent_a_catch}"))

print("Fable death contexts (cause, f_state_at_death, f_dart_st, f_threw_last8, f_recall_last15, a_caught_last10):")
for k, cnt in death_ctx.most_common(20):
    print(f"  {cnt:2d}x : {k}")

print("\nSample notable Astra62 vs Fable games:")
for item in interesting_af[:25]:
    print(" ", item)

print("\n=== Sample notable Fable vs Opus games (36 replays) ===")
fo_dir = OUT / "replays_fable_opus"
for rf in sorted(fo_dir.glob("g*.txt"), key=lambda p: int(p.stem[1:])):
    rep = parse_replay(rf)
    fseat = 0 if rep["bots"][0] == "fable" else 1
    oseat = 1 - fseat
    res_f = rep["results"][fseat]
    res_o = rep["results"][oseat]
    blocks_f = sum(1 for t, evs in rep["events"].items() for e in evs if e[0] == "BLOCK" and int(e[1]) == fseat)
    recalls_o = sum(1 for t, evs in rep["events"].items() for e in evs if e[0] == "RECALL" and int(e[1]) == oseat)
    cancels_o = sum(1 for t, evs in rep["events"].items() for e in evs if e[0] == "CANCEL" and int(e[1]) == oseat)
    if blocks_f >= 2 or recalls_o >= 4 or rep["map"] == "ruins":
        print(f"  {rf.name} [{rep['map']} s={rep['seed']} F_seat={fseat}]: F_rank={res_f[0]} O_dead={res_o[1]}({res_o[1]/20:.2f}s) c={res_o[2]} | blkF={blocks_f} recO={recalls_o} canO={cancels_o}")
