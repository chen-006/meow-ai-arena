"""复核分路、无线电字段、实际送达和 GLM 解包，从回放留存逐局证据。"""
from collections import Counter
import math
import sys

from prepare import ROOT, A, F, write, read, verify, sha
from run import game_rows

WAVE1 = ROOT.parent / "_ep12_exp"

def replay(path):
    states, actions, names = {}, {}, []
    cfg = {}
    limits = None
    for line in path.open(encoding="utf-8"):
        if line.startswith("CFG "):
            cfg = dict((k, int(v)) for k,v in (field.split("=") for field in line.split()[1:]))
        elif line.startswith("MAP "):
            _, nx, ny, cell = line.split()
            cfg.update(nx=int(nx), ny=int(ny), cell=int(cell))
        elif line.startswith("LIMITS "):
            limits = tuple(map(float, line.split()[1:]))
        elif line.startswith("PLAYER "):
            names.append(line.split()[3])
        elif line.startswith(("S ", "A ")):
            fields = line.split("|", 11)
            tag, step = fields[0].split()
            rows = [tuple(map(int, s.split())) for s in fields[1:11]]
            (states if tag == "S" else actions)[int(step)] = rows
    verify(limits, (10.0, 1000.0), path)
    return cfg, names, states, actions

def lane_audit():
    cases = read(WAVE1 / "main/manifest.json")["cases"]
    base = next(c["offset"] for c in cases if c["name"] == "1-clones-piers")
    old = [next((WAVE1 / "main").glob(f"replays-*/g{k}.txt")) for k in range(base,base+4)]
    pilots = [next((ROOT / "pilot5").glob(f"replays-*/g{k}.txt")) for k in range(2)]
    bulk = [next((ROOT / "lanes").glob(f"replays-*/g{k}.txt"), None) for k in (200,201)]
    result = []
    for label, paths, order in (("无战术", old, None), ("5a", pilots, "ABBCC"), ("5b", bulk, "AABCC")):
        for p in filter(None, paths):
            _, names, states, _ = replay(p)
            seats = []
            for k, name in enumerate(names):
                first = None
                for t, rows in states.items():
                    alive,x,y = rows[k][:3]
                    if not alive:
                        continue
                    for world, py in zip("ABC", (3250,10000,16750)):
                        if (x-16000)**2 + (y-py)**2 <= 2000**2:
                            local = world if k<5 else "CBA"["ABC".index(world)]
                            first = dict(step=t, world=world, local=local, x=x, y=y)
                            break
                    if first:
                        break
                expected = order[k % 5] if order else ("B" if name.startswith("astra") else None)
                seats.append(dict(name=name, seat=k, first=first, expected=expected,
                                  match=(first["local"] if first else None) == expected if expected else None))
            result.append(dict(group=label, replay=str(p), sha256=sha(p), seats=seats))
    verify(sum(s["match"] is True for r in result if r["group"]=="5a" for s in r["seats"]), 20, "5a 首次触点")
    write(ROOT / "audit5.json", result)

def alive_packets(states, actions, cfg, sender, receiver):
    for step, act in actions.items():
        now = states.get(step+1)
        if now is None:
            continue
        then = states[step]
        delayed = states[max(0,step+1-cfg["DELAY"])][sender]
        m = act[sender][5]
        if m and then[sender][0] and now[receiver][0] and delayed[0] and now[sender][13] == m:
            yield step,m,then[sender],delayed,act[sender]

def a_decode(m):
    return m & 63, (m>>6)&63, (m>>12)&3, m>>14

def f_decode(m):
    return m & 127, (m>>7)&127, m>>14

def between(cfg, states, actions, sender, receiver, language):
    count = Counter()
    previous = None
    for step,m,p,seen,action in alive_packets(states,actions,cfg,sender,receiver):
        _, px, py = p[:3]
        count["packets"] += 1
        if language == "A":
            x,y,intent,phase = a_decode(m)
            count[f"intent{intent}"] += 1
            count["format_errors"] += not (phase==step%4 and x<cfg["nx"] and y<cfg["ny"])
            count["exact_position_cells"] += (x,y)==(px//500,py//500)
            count["position_packets"] += 1
            if receiver % 5 == 0 and (intent in (1,3)) and (
                previous is None or previous != (step-1,intent)
            ):
                count["synthetic_events"] += 1
                if action[2] == intent and (action[3] or action[4]):
                    count["aim_comparable"] += 1
                    hint = seen[5] * math.pi/18000
                    actual = math.atan2(action[4],action[3])
                    count["aim_gt45deg"] += abs(math.remainder(actual-hint,2*math.pi)) > math.pi/4
            previous = (step,intent)
        else:
            x,y,kind = f_decode(m)
            count[f"F{kind}"] += 1
            count["format_errors"] += kind not in (1,2,3)
            if kind == 1:
                count["position_packets"] += 1
                count["exact_position_cells"] += (x,y)==(px//250,py//250)
            if receiver % 5 == 3:
                # 未改的 GLM 接收器按转置坐标落点。
                if kind == 1:
                    count["glm_pos_gt1m"] += math.hypot(y*250+125-px,x*250+125-py)>1000
    count["coordinate_cell_errors"] = count["position_packets"] - count["exact_position_cells"]
    return dict(count)

def translate_audit():
    folder = ROOT / "pilot6"
    games = {g["id"]:g for g in game_rows(folder)}
    verify(len(games),6,"翻译验收局数")
    summaries = []
    for gid,g in sorted(games.items()):
        path = next(folder.glob(f"replays-*/g{gid}.txt"))
        cfg,names,states,actions = replay(path)
        for k,captain in enumerate(names):
            if captain not in ("astra","fable"):
                continue
            language = "F" if captain == "astra" else "A"
            for peer in range(k+1,k+5):
                for sender,receiver in ((k,peer),(peer,k)):
                    values = between(cfg,states,actions,sender,receiver,language)
                    verify(values.get("packets",0)>0,True,"有双向发包")
                    verify(values.get("format_errors",0),0,(g["map"],names[sender],names[receiver]))
                    verify(values.get("coordinate_cell_errors",0),0,(g["map"],names[sender],names[receiver]))
                    summaries.append(dict(replay=str(path),gid=gid,map=g["map"],side=k//5,
                                          sender=names[sender],receiver=names[receiver],language=language,**values))
    write(ROOT / "audit6.json",summaries)

def glm_audit():
    folder = ROOT / "pilot8"
    games = {g["id"]:g for g in game_rows(folder)}
    cases = read(folder / "manifest.json")["cases"]
    verify(len(games),12,"GLM 验收局数")
    result = []
    for case in cases:
        fixed = case["name"].startswith("8-default")
        for flip in (0,1):
            gid = case["offset"] + flip
            path = next(folder.glob(f"replays-*/g{gid}.txt"))
            cfg,names,states,actions = replay(path)
            glm = names.index("glm")
            count = Counter()
            error_mm = 0
            for k in range(glm-3,glm+2):
                if k == glm:
                    continue
                for _,m,p,_,_ in alive_packets(states,actions,cfg,k,glm):
                    x,y,kind = f_decode(m)
                    if kind not in (1,2,3):
                        continue
                    count[f"F{kind}"] += 1
                    rx,ry = (x,y) if fixed else (y,x)
                    count["decoder_cell_errors"] += (rx,ry)!=(x,y)
                    if kind != 1:
                        continue
                    dist = math.hypot(rx*250+125-p[1],ry*250+125-p[2])
                    count["position_gt1m"] += dist > 1000
                    count["sender_cell_errors"] += (x,y)!=(p[1]//250,p[2]//250)
                    error_mm += dist
            verify(count["sender_cell_errors"],0,case["name"])
            if fixed:
                verify(count["decoder_cell_errors"],0,case["name"])
                verify(count["position_gt1m"],0,case["name"])
            result.append(dict(case=case["name"],id=gid,side=glm//5,replay=str(path),
                               mean_physical_position_error_mm=error_mm/count["F1"],**count))
    write(ROOT / "audit8.json",result)

if __name__ == "__main__":
    for phase in sys.argv[1:]:
        {"lanes":lane_audit,"translate":translate_audit,"glm":glm_audit}[phase]()
        print(f"{phase} 验收通过", flush=True)

