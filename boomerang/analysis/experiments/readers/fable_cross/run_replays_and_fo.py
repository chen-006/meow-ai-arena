import json, sys, types
from collections import Counter, defaultdict
from pathlib import Path
sys.dont_write_bytecode = True

PEEK = Path("<本机路径，未收录>")
sys.path.insert(0, str(PEEK))
import match, build

OUT = PEEK / "_bot" / "fable"
BIN = OUT / "bin"
BIN.mkdir(parents=True, exist_ok=True)

# Compile fable, opus, astra62 into _bot/fable/bin so we don't touch anything outside _bot/fable
cxx = build.find_compiler()
print("Compiler:", cxx)

def build_local(src_path, exe_name):
    exe = BIN / exe_name
    if exe.is_file():
        return exe
    import subprocess, shutil, tempfile, os
    work = Path(tempfile.mkdtemp(prefix="b-", dir=BIN))
    try:
        shutil.copyfile(src_path, work / "player.cpp")
        shutil.copyfile(PEEK / "dart.hpp", work / "dart.hpp")
        env = dict(os.environ)
        env["PATH"] = str(Path(cxx).resolve().parent) + os.pathsep + env.get("PATH", "")
        r = subprocess.run([cxx, *build.FLAGS, "player.cpp", "-o", "player.exe"], cwd=work,
                           capture_output=True, text=True, encoding="utf-8", errors="replace", env=env, timeout=120)
        if r.returncode != 0:
            raise RuntimeError(r.stderr or r.stdout)
        shutil.copyfile(work / "player.exe", exe)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return exe

fable_exe = build_local("<本机路径，未收录>", "fable.exe")
opus_exe = build_local("<本机路径，未收录>", "opus.exe")
astra62_exe = build_local("<本机路径，未收录>", "astra62.exe")

A_ROOT = Path("<本机路径，未收录>")
MAPS = [A_ROOT / "选手包" / "maps" / "twinlakes.txt", A_ROOT / "选手包" / "maps" / "yinyang.txt"] + \
       [A_ROOT / "原型" / "地图" / "隐藏" / f for f in ("icefloes.txt", "ruins.txt", "skybridge.txt", "stonecircle.txt")]
extra = match.common_args(types.SimpleNamespace(step_ms=10, sandbox="none", cfg=None))

# 1) 228 games Fable vs Opus telemetry + save replays for seeds 11001..11003 (36 replays)
entries_fo = [("fable", fable_exe), ("opus", opus_exe)]
games_fo_rep = []
games_fo_rest = []
gid = 0
for mp in MAPS:
    for seed in range(11001, 11020):
        for seats in ([0, 1], [1, 0]):
            item = (gid, seed, seats, 0, str(mp))
            if seed <= 11003:
                games_fo_rep.append(item)
            else:
                games_fo_rest.append(item)
            gid += 1

rep_fo_dir = OUT / "replays_fable_opus"
rows_fo = list(match.run_batch(entries_fo, games_fo_rep, 2, extra, replay_dir=rep_fo_dir)) + \
          list(match.run_batch(entries_fo, games_fo_rest, 2, extra))
(OUT / "fable_opus_228.json").write_text(json.dumps(rows_fo, ensure_ascii=False), encoding="utf-8")
print(f"Fable vs Opus 228 done: {len(rows_fo)} games")

# 2) Save sample replays of Astra62 vs Fable across all 6 maps (seeds 11001..11005 = 60 games)
entries_af = [("astra62", astra62_exe), ("fable", fable_exe)]
games_af_rep = []
gid = 0
for mp in MAPS:
    for seed in range(11001, 11006):
        for seats in ([0, 1], [1, 0]):
            games_af_rep.append((gid, seed, seats, 0, str(mp)))
            gid += 1
rep_af_dir = OUT / "replays_astra62_fable"
rows_af_rep = list(match.run_batch(entries_af, games_af_rep, 2, extra, replay_dir=rep_af_dir))
(OUT / "astra62_fable_60_meta.json").write_text(json.dumps(rows_af_rep, ensure_ascii=False), encoding="utf-8")
print(f"Astra62 vs Fable 60 replays done")
