"""通用对局脚本：python runner.py OUT.json --p name=path.cpp ... [--mode 0|1] [--seeds 11001 11019] [--maps all|twinlakes,...] [--replays DIR]
1v1：每图每种子两边各坐一次；四人局：4 个程序，每图每种子 4 次轮座。exe 编译到 tools/bin（按内容哈希缓存）。"""
import json, sys, types, argparse, hashlib, subprocess, shutil, tempfile, os, time
from pathlib import Path
sys.dont_write_bytecode = True
from _paths import WORKSPACE as PEEK, MAPS
import match, build
BIN = Path(__file__).resolve().parent / "bin"; BIN.mkdir(parents=True, exist_ok=True)
def build_local(src):
    src = Path(src)
    if src.suffix.lower() == ".exe": return src
    h = hashlib.md5(src.read_bytes() + (PEEK/"dart.hpp").read_bytes()).hexdigest()[:12]
    exe = BIN / f"{src.stem}_{h}.exe"
    if exe.is_file(): return exe
    cxx = build.find_compiler()
    work = Path(tempfile.mkdtemp(prefix="b-", dir=BIN))
    try:
        shutil.copyfile(src, work/"player.cpp"); shutil.copyfile(PEEK/"dart.hpp", work/"dart.hpp")
        env = dict(os.environ); env["PATH"] = str(Path(cxx).resolve().parent) + os.pathsep + env.get("PATH","")
        r = subprocess.run([cxx, *build.FLAGS, "player.cpp", "-o", "player.exe"], cwd=work, capture_output=True, text=True, encoding="utf-8", errors="replace", env=env, timeout=300)
        if r.returncode: raise RuntimeError(r.stderr or r.stdout)
        shutil.copyfile(work/"player.exe", exe)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return exe
ap = argparse.ArgumentParser()
ap.add_argument("out"); ap.add_argument("--p", action="append", required=True)
ap.add_argument("--mode", type=int, default=0); ap.add_argument("--seeds", type=int, nargs=2, default=[11001, 11019])
ap.add_argument("--maps", default="all"); ap.add_argument("--replays"); ap.add_argument("--workers", type=int, default=2)
a = ap.parse_args()
progs = [(s.split("=",1)[0], build_local(s.split("=",1)[1])) for s in a.p]
maps = list(MAPS) if a.maps == "all" else a.maps.split(",")
games = []; gid = 0
for m in maps:
    for sd in range(a.seeds[0], a.seeds[1]+1):
        if a.mode == 0:
            orders = [[0,1],[1,0]]
        else:
            orders = [[(i+r)%4 for i in range(4)] for r in range(4)]
        for od in orders:
            games.append((gid, sd, od, a.mode, str(MAPS[m]))); gid += 1
extra = match.common_args(types.SimpleNamespace(step_ms=10, sandbox="none", cfg=None))
t0 = time.time()
rows = match.run_batch(progs, games, a.workers, extra, replay_dir=a.replays)
for r in rows:
    for p in r["players"]: p.pop("path", None)
Path(a.out).write_text(json.dumps(rows, ensure_ascii=False), encoding="utf-8")
print(f"done {len(rows)} games in {time.time()-t0:.0f}s -> {a.out}")
