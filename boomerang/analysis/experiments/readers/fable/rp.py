import json, sys, re
from collections import Counter, defaultdict
from pathlib import Path
DST = ["HAND","OUT","BACK","GROUND","RISE","GONE","CALL"]
def parse(path):
    hdr = {}; players = []; S = {}; A = {}; E = defaultdict(list)
    for ln in Path(path).read_text(encoding="utf-8").splitlines():
        if ln.startswith("PLAYER "):
            t = ln.split(); players.append(t[3])
        elif ln.startswith("MAPNAME"): hdr["map"] = ln.split()[1]
        elif ln.startswith("SEED"): hdr["seed"] = int(ln.split()[1])
        elif ln.startswith("S "):
            parts = ln.split("|"); t = int(parts[0].split()[1])
            n = len(players)
            ps = [list(map(int, parts[1+i].split())) for i in range(n)]
            ds = [list(map(int, parts[1+n+i].split())) for i in range(n)]
            S[t] = (ps, ds)
        elif ln.startswith("A "):
            parts = ln.split("|"); t = int(parts[0].split()[1])
            A[t] = [list(map(int, p.split())) for p in parts[1:]]
        elif ln.startswith("E "):
            t = ln.split(); E[int(t[1])].append((t[2], [int(x) for x in t[3:]]))
        elif ln.startswith("END"):
            hdr["end"] = ln.split()
    hdr["players"] = players
    return hdr, S, A, E
