"""本地 5v5 对战。每个种子左右各打一局，输出胜负、折算胜率和故障数。

python match.py team template mid --vs weak --map 歼灭 --games 4
python match.py team weak --vs mid --map 刺王 --布阵 我的阵 对方阵 --战术 我的.txt 对方.txt
python match.py 推演 --map 占点 --冻结 冻结版 --名单 astra sol hydrogen grok kimi --布阵 正式 --对方布阵 对方正式

程序可写 weak、mid、strong、template 或 .exe/.cpp 路径。按队长、队员顺序填。
省略 --map 时三张图分别打。推演只用本队冻结程序和指定档陪练，换图时省略 --对方布阵。
"""
import argparse
from collections import Counter
import os
import json
import hashlib
import tempfile
import shutil
import stat
import math
from pathlib import Path
import statistics
import subprocess

import build
from 布阵 import KIT, MAPS, ROSTERS, Map, form_path, load_json, local_names, read_form


BUILD_HINT = "开源版不带编译产物：请先在上一层（boomerang-team/）运行 python build_engine.py 生成裁判 arena.exe 和三档陪练 bots/*.exe"


def exe(spec, compiled):
    presets = {name:KIT/"bots"/(name+".exe") for name in ("weak","mid","strong")}
    presets["template"] = KIT/"template.cpp"
    path = Path(presets.get(spec,spec)).resolve()
    if not path.is_file():
        if spec in ("weak", "mid", "strong"):
            raise ValueError(f"找不到陪练程序 {path}。\n{BUILD_HINT}")
        raise ValueError(f"找不到程序 {path}")
    if path.suffix.lower() != ".cpp":
        return path
    if path not in compiled:
        compiled[path] = build.compile_cpp(path)
    return compiled[path]


def simulation_exe(path):
    # 一次推演始终运行同一版本。Windows 运行中的 exe 会被锁住，冻结版正本须可接收亲手改。
    data = path.read_bytes()
    cache = KIT/"build"/"推演程序"
    cache.mkdir(parents=True,exist_ok=True)
    dest = cache/(hashlib.sha256(data).hexdigest()+".exe")
    if dest.is_file():
        return dest
    with tempfile.NamedTemporaryFile(dir=cache,suffix=".exe",delete=False) as f:
        f.write(data)
        staging = Path(f.name)
    try:
        staging.rename(dest)
    except FileExistsError:  # 另一场推演刚完成同一份缓存，当前副本也已写完。
        return staging
    return dest


def batch(m, teams, formations, plans, jobs, workers, ms, replays=None, *, run_root=None):
    """局号成对，偶数时甲队坐左。每局完成立即交给调用者存盘。"""
    lines = []
    roster = teams[0]+teams[1]
    for name, _ in roster:
        if not name or any(c.isspace() for c in name):
            raise ValueError("程序短名不能为空或含空白")
    if run_root is None:
        lines.extend(f"BOT {i} {name} {Path(path).resolve()}" for i,(name,path) in enumerate(roster))
    for gid,seed in jobs:
        left = gid % 2
        ids = list(range(10))
        if run_root is not None:
            # 每局每席独占目录。裁判在 exe 所在目录启动，同一 BOT 编号可在下一条 GAME 前重用。
            for i,(name,path) in enumerate(roster):
                dest = run_root/str(gid)/str(i)/"player.exe"
                dest.parent.mkdir(parents=True)
                # NTFS 的单文件硬链接数有限，为每 500 局创建独立的只读母本。
                master = run_root/"程序"/str(gid//500)/f"{i}.exe"
                if not master.exists():
                    master.parent.mkdir(parents=True,exist_ok=True)
                    shutil.copyfile(path,master)
                    master.chmod(stat.S_IREAD)
                os.link(master,dest)
                lines.append(f"BOT {i} {name} {dest.resolve()}")
        if left:
            ids = ids[5:]+ids[:5]
        options = [f"MODE={m.mode}"]
        for side in range(2):
            for label,paths in (("FORMATION",formations),("PLAN",plans)):
                path = paths[side ^ left]
                if path is not None:
                    options.append(f'{label}{side}="{Path(path).resolve()}"')
        lines.append(f"GAME {gid} {seed} "+",".join(map(str,ids))+" "+" ".join(options)+f" MAP={m.path}")
    if not (KIT/"arena.exe").is_file():
        raise ValueError(f"找不到裁判程序 {KIT/'arena.exe'}。\n{BUILD_HINT}")
    cmd = [KIT/"arena.exe","batch","--sandbox","none","--workers",str(workers),"--step-ms",str(ms)]
    if replays:
        replays.mkdir(parents=True,exist_ok=True)
        cmd.extend(["--replay-dir",replays.resolve()])
    proc = subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE)
    try:
        proc.stdin.write(("\n".join(lines)+"\n").encode())
        proc.stdin.close()
        for line in proc.stdout:
            if line.startswith(b"X "):
                raise ValueError(line.decode("utf-8","replace").strip())
            g = json.loads(line)
            if any(g["formation_errors"]):
                raise ValueError(f"裁判退回了已检查的布阵 {g['formation_errors']}")
            yield g
        if proc.wait():
            raise ValueError(f"裁判退出，返回码 {proc.returncode}")
    finally:
        if proc.poll() is None:
            proc.kill()
        proc.wait()
        proc.stdout.close()
        proc.stdin.close()


def score(g):
    if g["winner"] < 0:
        return .5
    return float(g["winner"] == g["id"]%2)


def paired_scores(games):
    pairs = {}
    for g in games:
        pair = pairs.setdefault(g["seed"],{})
        side = g["id"]%2
        if side in pair:
            raise ValueError("同一种子重复计入同一侧")
        pair[side] = score(g)
    if not pairs or any(len(p)!=2 for p in pairs.values()):
        raise ValueError("缺少同种子左右配对局")
    return [sum(p.values())/2 for p in pairs.values()]


def summary(games):
    values = paired_scores(games)
    rate = statistics.mean(values)
    se = statistics.stdev(values)/math.sqrt(len(values)) if len(values)>1 else None
    return rate,se


def fault_summary(games):
    by_player = {}
    for g in games:
        for p in g["players"]:
            if not p["faults_total"]:
                continue
            # 裁判的逐条故障列表最多保留 20 条，超时和非法另有完整计数。
            counts = Counter({"超时": p["timeouts"], "非法": p["invalid"]})
            counts["崩溃"] = sum(f[1] == "crash" for f in p["faults"])
            counts["其他"] = p["faults_total"] - sum(counts.values())
            team = p["team"] ^ (g["id"] % 2)
            by_player.setdefault((team, p["name"]), Counter()).update(counts)
    return "；".join(
        f"{('本队','对方')[team]} {name}×{sum(counts.values())}（"
        + " / ".join(f"{kind} {n}" for kind, n in counts.items() if n)
        + "）"
        for (team, name), counts in sorted(by_player.items())
    )


def plan_file(path):
    raw = Path(path).read_bytes()
    if len(raw)>2000:
        raise ValueError("战术单超过 2000 字节")
    raw.decode("utf-8")
    return Path(path)


def names_for(specs):
    names = []
    for s in specs:
        base = Path(s).stem
        n = base
        i = 1
        while n in names:
            i += 1
            n = f"{base}{i}"
        names.append(n)
    return names


def formation_names(path, programs):
    data = load_json(path)
    pos = data.get("站位",data.get("对方站位")) if isinstance(data,dict) else None
    if not isinstance(pos,dict):
        raise ValueError(f"{path} 的站位须为五人姓名到坐标的对象")
    positions = list(pos)
    for roster in (*ROSTERS,names_for(programs)):
        if len(roster)==5 and set(roster)==set(positions):
            return list(roster)
    return positions


def main():
    parser = argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command",required=True)
    team = sub.add_parser("team",aliases=["对战"])
    team.add_argument("a",nargs="+")
    team.add_argument("--vs",dest="b",nargs="+",required=True)
    team.add_argument("--fill-a")
    team.add_argument("--fill-b",default="mid")
    sim = sub.add_parser("推演")
    sim.add_argument("--冻结",type=Path,default=KIT/"冻结版")
    sim.add_argument("--陪练",choices=("weak","mid","strong"),default="mid")
    sim.add_argument("--对方布阵")
    sim.add_argument("--回合",type=int,choices=range(1,8),help="按赛程选图；新图首回合和加赛自动用对方默认阵")
    for p in (team,sim):
        p.add_argument("--名单","--names-a",nargs=5)
        p.add_argument("--对方名单","--names-b",nargs=5)
        p.add_argument("--map",nargs="+")
        p.add_argument("--布阵",nargs="+",default=[],help="先本队再对方。填文件夹时自动读取其中的战术.txt")
        p.add_argument("--战术",nargs="+",default=[],help="单独指定战术文件；用 - 表示空")
        p.add_argument("--games",type=int,default=40)
        p.add_argument("--seed-base",type=int,default=1)
        p.add_argument("--workers",type=int,default=4)
        p.add_argument("--step-ms",type=int,default=10)
        p.add_argument("--replays",type=Path)
    a = parser.parse_args()
    simulating = a.command == "推演"
    if simulating and a.回合:
        expected = ("caldera","caldera","piers","piers","keeps","keeps","keeps")[a.回合-1]
        if a.map and any(Map(x).path.stem!=expected for x in a.map):
            parser.error("回合号与地图不符")
        a.map = [expected]
        if a.回合 in (1,3,5,7):
            a.对方布阵 = None
    if not a.map:
        a.map = list(MAPS)
    limit = 1 if simulating else 2
    if min(a.games,a.workers,a.step_ms)<1 or max(len(a.布阵),len(a.战术))>limit:
        parser.error("局数、并发数和时间须为正，布阵与战术数量须符合队伍数")
    forms = [None,None]
    plans = [None,None]
    for i,s in enumerate(a.布阵):
        if s != "-":
            forms[i] = form_path(s)
            companion = forms[i].with_name("战术.txt")
            if i>=len(a.战术) and companion.exists():
                plans[i] = plan_file(companion)
    for i,s in enumerate(a.战术):
        plans[i] = None if s == "-" else plan_file(s)
    if simulating:
        if a.对方布阵:
            forms[1] = form_path(a.对方布阵)
        left = a.名单 or next((r for r in ROSTERS if all((a.冻结/(n+".exe")).is_file() for n in r)),None)
        if not left:
            left = formation_names(forms[0],[]) if forms[0] else local_names()
        specs = [[a.冻结/(n+".exe") for n in left],[a.陪练]*5]
    else:
        if max(len(a.a),len(a.b))>5:
            parser.error("每队最多五人")
        specs = [a.a+[a.fill_a or a.a[0]]*(5-len(a.a)),a.b+[a.fill_b]*(5-len(a.b))]
        left = a.名单
    opponent = a.对方名单
    if simulating and opponent is None and forms[1] is None:
        for i,roster in enumerate(ROSTERS):
            if set(left)==set(roster):
                opponent = ROSTERS[1-i]
    names = [left,opponent]
    teams = []
    compiled = {}
    for i,group in enumerate(specs):
        if names[i] is None:
            names[i] = formation_names(forms[i],group) if forms[i] else names_for(group)
        if len(set(names[i]))!=5 or len(names[i])!=5:
            parser.error("每队需要五个不同的名字")
        programs = [exe(s, compiled) for s in group]
        if simulating and i==0:
            programs = [simulation_exe(path) for path in programs]
        teams.append(list(zip(names[i],programs)))
    count = (a.games+1)//2*2
    for map_name in a.map:
        m = Map(map_name)
        match_forms = list(forms)
        if simulating and forms[1]:
            marker = forms[1].with_name("地图.txt")
            if marker.exists() and marker.read_text(encoding="utf-8").strip()!=m.path.stem:
                match_forms[1] = None
        for i,path in enumerate(match_forms):
            if path:
                read_form(path,m,names[i],opponent=(i==1))
        games = list(batch(m,teams,match_forms,plans,[(i,a.seed_base+i//2) for i in range(count)],
                           a.workers,a.step_ms,a.replays/m.path.stem if a.replays else None))
        if len(games)!=count:
            raise ValueError("裁判返回的局数不足")
        rate,_ = summary(games)
        w = sum(score(g)==1 for g in games)
        l = sum(score(g)==0 for g in games)
        faults = sum(p["faults_total"] for g in games for p in g["players"])
        print(f"{('歼灭','占点','刺王')[m.mode-3]}  本队 {w} 胜 {l} 负 {count-w-l} 平，胜率 {rate:.2%}，"
              f"平均局长 {statistics.mean(g['steps'] for g in games)/20:.1f} 秒，故障 {faults} 次。")
        if faults:
            print("故障：" + fault_summary(games))


if __name__ == "__main__":
    try:
        main()
    except (ValueError,OSError,build.BuildError) as e:
        raise SystemExit(str(e))
