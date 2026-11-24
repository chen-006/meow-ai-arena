# -*- coding: utf-8 -*-
"""地图文件（选手包 maps 的格式，细格子版带 // CELL 0.5）-> 一份只有开局状态的"回放"，这样没有对局也能按成片画风出静帧。
格式扩展（都写在注释行里，旧的读法照常工作）：// CELL <米> 格子边长；// THEME <主题名>；// TITLE <中文名>。"""
import math
import os

CH = {".": "0", "#": "1", "~": "2", '"': "3"}


def read_map(path):
    m = dict(cell=1.0, theme=None, title=None, name=os.path.splitext(os.path.basename(path))[0], modes=[], spawn={},
             grid=[], wave=[], W=0, H=0)
    lines = [ln.rstrip("\r\n") for ln in open(path, encoding="utf-8")]
    i = 0
    while i < len(lines):
        ln = lines[i].strip()
        if ln.startswith("//"):
            p = ln[2:].split()
            if len(p) >= 2 and p[0] in ("CELL", "THEME", "TITLE"):
                m[p[0].lower()] = float(p[1]) if p[0] == "CELL" else p[1]
        elif ln.startswith("NAME "):
            m["name"] = ln.split()[1]
        elif ln.startswith("MODES"):
            m["modes"] = [int(v) for v in ln.split()[1:]]
        elif ln.startswith("SPAWN "):
            p = ln.split()
            v = [float(x) for x in p[2:]]
            m["spawn"][int(p[1])] = list(zip(v[0::2], v[1::2]))
        elif ln.startswith("GRID "):
            p = ln.split()
            m["W"] = int(p[1])
            m["H"] = int(p[2]) if len(p) > 2 else int(p[1])
            m["grid"] = [lines[i + 1 + j] for j in range(m["H"])]
            i += m["H"]
        elif ln == "COLLAPSE":
            m["wave"] = [lines[i + 1 + j] for j in range(m["H"])]
            i += m["H"]
        i += 1
    if not m["wave"]:
        m["wave"] = ["." * m["W"]] * m["H"]
    return m


def to_replay(map_path, out_path, mode=None):
    """写一份只有 S 0 的回放。mode 不给时取地图支持的人数最多的形式。"""
    m = read_map(map_path)
    if mode is None:
        mode = max(m["spawn"]) if m["spawn"] else 1
    sp = m["spawn"][mode]
    n = len(sp)
    cx, cy = m["W"] * m["cell"] / 2, m["H"] * m["cell"] / 2
    teams = [0 if (mode < 2 or k < n // 2) else 1 for k in range(n)]
    ch, dr = [], []
    for (x, y) in sp:
        face = int(math.degrees(math.atan2(cy - y, cx - x)) * 100) % 36000
        ch.append("1 %d %d 0 0 %d 0 0 0 0 0 0 0 0 255 -1 0 -1" % (x * 1000, y * 1000, face))
        dr.append("0 %d %d 0 0 0 0" % (x * 1000, y * 1000))
    waves = sorted({int(c, 36) for row in m["wave"] for c in row if c != "."})
    R = (max(waves) + 1) if waves else 0
    L = ["DART-REPLAY 2", "ENGINE dartfu-map",
         "CFG MODE=%d STEPS=800 CELL=%d COL_WARN=30" % (mode, round(m["cell"] * 1000)),
         "SEED 0", "MODE %d %s" % (mode, ["duel", "ffa", "team", "elimination", "control", "king"][mode]), "MAPNAME " + m["name"]]
    if m["theme"]:
        L.append("THEME " + m["theme"])
    L.append("MAP %d %d" % (m["W"], m["H"]))
    L += ["".join(CH.get(c, "2") for c in row.ljust(m["W"], "~")[:m["W"]]) for row in m["grid"]]
    L += ["WAVE"] + [row.ljust(m["W"], ".")[:m["W"]] for row in m["wave"]]
    L.append("WAVES %d %s" % (R, " ".join(str(240 + 30 * w) for w in range(R))))
    L.append("PLAYERS %d" % n)
    L += ["PLAYER %d %d p%d" % (k, teams[k], k) for k in range(n)]
    L += ["S 0 | %s | %s | 0 -1" % (" | ".join(ch), " | ".join(dr)), "END 0 -1 0 time"]
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    open(out_path, "w", encoding="utf-8").write("\n".join(L) + "\n")
    return out_path, m
