"""光轮对战 · 本地对战工具。用法见 README.md，或运行 `arena.py -h`。

这里不含任何规则：编译、排对局、统计分数。对局由 judge/judge.cpp（规则来自 tron.hpp）完成。
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

KIT = Path(__file__).resolve().parent
BUILD = KIT / "build"
HISTORY = KIT / "history"
REPLAYS = KIT / "replays"
FLAGS = ["-std=c++20", "-O2", "-static"]
EXE_FLAGS = ["-Wl,--stack,67108864"]
BUILTIN = {"greedy": KIT / "bots" / "greedy.cpp", "random": KIT / "bots" / "random.cpp"}
SPARRING = {"sparring": KIT / "bots" / "sparring",              # 陪练（中等）：.exe 与 .dll，只有编译好的程序
            "sparring_hard": KIT / "bots" / "sparring_hard"}    # 陪练（强）
MODES = {"1v1": [2], "3-4": [3, 4], "12": [12]}
WEIGHT = {"1v1": 0.5, "3-4": 0.3, "12": 0.2}
DEFAULT_GAMES = {2: 40, 3: 30, 4: 40, 12: 24}


class Fail(Exception):
    pass


# ==================================================================== 编译

def find_compiler():
    cxx = os.environ.get("CXX") or shutil.which("g++")
    if cxx:
        return cxx
    for base in [*KIT.parents, Path.home() / "Desktop" / "测试"]:
        guess = base / "00_项目管理" / "联调" / "toolchain" / "w64devkit" / "bin" / "g++.exe"
        if guess.is_file():
            return str(guess)
    return None


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def compile_in_build(sources: dict, out_name: str, extra: list) -> Path:
    """把源码（文件名 -> 内容）写进 build/ 下的一个临时目录，用相对路径编译（避开路径里的中文），产物放进 build/。"""
    out = BUILD / out_name
    if out.is_file():
        return out
    cxx = find_compiler()
    if not cxx:
        raise Fail("没有找到 g++。请确认 w64devkit 的 bin 目录在 PATH 里。")
    BUILD.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="c-", dir=BUILD))
    try:
        for name, data in sources.items():
            (work / name).parent.mkdir(parents=True, exist_ok=True)
            (work / name).write_bytes(data)
        env = dict(os.environ)
        env["PATH"] = str(Path(cxx).resolve().parent) + os.pathsep + env.get("PATH", "")
        r = subprocess.run([cxx, *FLAGS, *extra, "-o", "out.bin"], cwd=work, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", env=env, timeout=300)
        if r.returncode != 0:
            raise Fail("编译失败：\n" + (r.stderr or r.stdout)[-4000:].rstrip())
        os.replace(work / "out.bin", out)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return out


def kit_files():
    return {"tron.hpp": (KIT / "tron.hpp").read_bytes(), "tron_main.cpp": (KIT / "tron_main.cpp").read_bytes()}


def build_bot(src: Path, kind: str) -> Path:
    """kind = exe（正式模式）或 dll（快速模式）。按内容缓存。"""
    data = src.read_bytes()
    files = kit_files()
    key = sha(data + files["tron.hpp"] + files["tron_main.cpp"] + " ".join(FLAGS).encode())[:16]
    files["bot.cpp"] = data
    if kind == "exe":
        return compile_in_build(files, f"bot-{key}.exe", [*EXE_FLAGS, "-I.", "bot.cpp", "tron_main.cpp"])
    return compile_in_build(files, f"bot-{key}.dll", ["-shared", "-DTRON_DLL", "-I.", "bot.cpp", "tron_main.cpp"])


def build_judge() -> Path:
    files = {"tron.hpp": (KIT / "tron.hpp").read_bytes(), "judge/judge.cpp": (KIT / "judge" / "judge.cpp").read_bytes()}
    key = sha(files["tron.hpp"] + files["judge/judge.cpp"])[:16]
    return compile_in_build(files, f"judge-{key}.exe", ["judge/judge.cpp", "-lshell32"])


# ==================================================================== 参赛者

class Entry:
    """一个参赛程序：内置名字（greedy / random / sparring / sparring_hard）、.cpp 源码或 .exe。"""

    def __init__(self, spec: str):
        self.spec = spec
        low = spec.lower()
        if low in BUILTIN:
            self.name, self.src, self.exe = low, BUILTIN[low], None
        elif low in SPARRING:
            self.name, self.src, self.exe = low, None, SPARRING[low].with_suffix(".exe")
            if not self.exe.is_file():   # 开源版加的提示
                raise Fail(f"找不到陪练 {low}（{self.exe}）。开源版不带编译好的陪练：先在上一层（tron\\）运行 python make_player_pack.py")
        else:
            p = Path(spec)
            if not p.is_file():
                raise Fail(f"找不到文件：{spec}（也可以写 greedy、random、sparring、sparring_hard）")
            self.name = p.stem if p.stem != "bot" else str(p.parent.name + "/" + p.name)
            self.src = p if p.suffix.lower() in (".cpp", ".cc", ".cxx") else None
            self.exe = None if self.src else p
        self.sha = sha(self.src.read_bytes())[:12] if self.src else None

    def program(self, formal: bool) -> Path:
        if self.src:
            return build_bot(self.src, "exe" if formal else "dll")
        if formal:
            return self.exe
        if self.name in SPARRING:    # 陪练另带一份动态库给快速模式用；复制成带哈希的名字
            dll = SPARRING[self.name].with_suffix(".dll")
            out = BUILD / f"{self.name}-{sha(dll.read_bytes())[:16]}.dll"
            if not out.is_file():
                BUILD.mkdir(exist_ok=True)
                shutil.copyfile(dll, out)
            return out
        raise Fail(f"{self.spec} 是编译好的程序，只能用正式模式（加 --formal）")


def archive(entries, command, summary, started):
    """把参与的源码按内容存进 history/，并记一行日志。"""
    HISTORY.mkdir(exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    seen = []
    for e in entries:
        if not e.src or e.src.resolve().is_relative_to(KIT / "bots") or e.sha in seen:
            continue
        seen.append(e.sha)
        if not list(HISTORY.glob(f"*-{e.sha}.cpp")):
            shutil.copyfile(e.src, HISTORY / f"{stamp}-{e.sha}.cpp")
    rec = {"time": stamp, "seconds": round(time.time() - started, 1), "command": command,
           "entries": [{"spec": e.spec, "sha": e.sha} for e in entries], "summary": summary}
    with open(HISTORY / "log.jsonl", "a", encoding="utf-8") as f:
        f.write(json.dumps(rec, ensure_ascii=False) + "\n")


# ==================================================================== 跑对局

def run_games(entries, games, formal, workers, stderr_dir=None):
    """games: [(人数, 种子, [各座位的参赛者下标])]。返回每局的结果字典（与 games 同顺序）。"""
    judge = build_judge()
    progs = [None] * len(entries)
    with ThreadPoolExecutor(4) as pool:
        for i, p in enumerate(pool.map(lambda e: e.program(formal), entries)):
            progs[i] = p
    lines = [f"BOT {i} {p.resolve()}" for i, p in enumerate(progs)]
    lines += [f"GAME {g} {n} {seed} {' '.join(map(str, seats))}" for g, (n, seed, seats) in enumerate(games)]
    cmd = [str(judge), "run", "formal" if formal else "fast", "--workers", str(workers), "--tmp", str(BUILD / "dll")]
    if stderr_dir:
        cmd += ["--stderr-dir", str(stderr_dir)]
    r = subprocess.run(cmd, input="\n".join(lines) + "\n", capture_output=True, text=True, encoding="utf-8",
                       errors="replace")
    out = [None] * len(games)
    for line in r.stdout.splitlines():
        if line.startswith("X "):
            raise Fail(line[2:])
        if line.startswith("{"):
            rec = json.loads(line)
            out[rec["id"]] = rec
    if r.returncode != 0 or any(o is None for o in out):
        raise Fail(f"裁判程序异常退出（代码 {r.returncode}）：{r.stderr[-2000:]}")
    return out


def scores(rec):
    """每人的名次分 (身后人数 + 0.5 × 并列人数) / (N - 1)：第一 1，末位 0，同回合死亡并列取平均。"""
    n = len(rec["rank"])
    key = [10 ** 9 if d < 0 else d for d in rec["death_turn"]]
    return [(sum(key[k] > key[j] for j in range(n)) + 0.5 * (sum(key[k] == key[j] for j in range(n)) - 1)) / (n - 1)
            for k in range(n)]


# ==================================================================== 命令

def cmd_run(a):
    started = time.time()
    me = Entry(a.bot)
    opps = [Entry(s) for s in a.opp.split(",") if s.strip()]
    if not opps:
        raise Fail("--opp 至少要有一个对手")
    base = Entry("greedy")
    entries = [me, base, *opps]
    ns = sorted({n for m in a.n.split(",") for n in (MODES[m] if m in MODES else [int(m)] if m != "all" else [2, 3, 4, 12])})
    for n in ns:
        if n not in (2, 3, 4, 12):
            raise Fail("人数只能是 2、3、4、12（或 1v1、3-4、12、all）")
    games, meta = [], []
    for n in ns:
        k = a.games or DEFAULT_GAMES[n]
        for i in range(k):
            seat_me = i % n
            others = [2 + (i + j) % len(opps) for j in range(n - 1)]
            for who in (0, 1):    # 0：你的 bot；1：同一位置换成 greedy（计分零点）
                seats = list(others)
                seats.insert(seat_me, who)
                games.append((n, a.seed + i // n, seats))
                meta.append((n, i, who, seat_me))
    mode = "正式模式" if a.formal else "快速模式"
    workers = a.workers or (4 if a.formal else min(8, os.cpu_count() or 4))
    print(f"{mode}：{me.spec} 对 {','.join(o.spec for o in opps)}，人数 {ns}，共 {len(games)} 局"
          f"（一半是把你换成 greedy 的对照局），并行 {workers}……", flush=True)
    recs = run_games(entries, games, a.formal, workers)
    print(f"用时 {time.time() - started:.1f} 秒\n")
    by_mode = {}
    print("人数  局数  第一名   你的名次分  greedy 替身  差值（95% 区间）")
    for n in ns:
        mine = {i: (rec, seat) for (nn, i, who, seat), rec in zip(meta, recs) if nn == n and who == 0}
        base_ = {i: (rec, seat) for (nn, i, who, seat), rec in zip(meta, recs) if nn == n and who == 1}
        sm = [scores(r)[s] for r, s in mine.values()]
        sb = [scores(base_[i][0])[base_[i][1]] for i in mine]
        first = sum(1 for r, s in mine.values() if r["rank"][s] == 0) / len(mine)
        d = [x - y for x, y in zip(sm, sb)]
        mean = sum(d) / len(d)
        sd = math.sqrt(sum((x - mean) ** 2 for x in d) / max(1, len(d) - 1))
        print(f"{n:>4}  {len(sm):>4}  {first:6.0%}   {sum(sm) / len(sm):9.3f}  {sum(sb) / len(sb):10.3f}  "
              f"{mean:+.3f} ± {1.96 * sd / math.sqrt(len(d)):.3f}")
        for m, lst in MODES.items():
            if n in lst:
                by_mode.setdefault(m, []).extend(d)
    total = None
    parts = []
    for m, d in by_mode.items():
        parts.append(f"{m} {100 * sum(d) / len(d):+.1f}")
    if set(by_mode) == set(MODES):
        total = 100 * sum(WEIGHT[m] * sum(d) / len(d) for m, d in by_mode.items())
    mine_recs = [(r, s) for (nn, i, who, s), r in zip(meta, recs) if who == 0]
    t = sum(r["timeouts"][s] for r, s in mine_recs)
    inv = sum(r["invalid"][s] for r, s in mine_recs)
    cr = sum(r["crash_turn"][s] >= 0 for r, s in mine_recs)
    mx = max(r["max_ms"][s] for r, s in mine_recs)
    first = max(r["first_ms"][s] for r, s in mine_recs)
    avg = sum(r["avg_ms"][s] for r, s in mine_recs) / len(mine_recs)
    causes = {}
    for r, s in mine_recs:
        causes[r["cause"][s]] = causes.get(r["cause"][s], 0) + 1
    print(f"\n你的 bot 的结局：" + "，".join(f"{CAUSE_ZH.get(c, c)} {v}" for c, v in sorted(causes.items(), key=lambda x: -x[1])))
    print(f"CPU：首回合最多 {first:.0f} 毫秒，之后平均 {avg:.1f}、最多 {mx:.1f} 毫秒（预算 50）。"
          f"超时 {t} 次，不合法 {inv} 次，崩溃 {cr} 局" + ("（快速模式只统计不判罚）" if not a.formal else ""))
    line = "分数（×100，greedy = 0）：" + "，".join(parts)
    if total is not None:
        line += f"；加权总分 {total:+.1f}（1v1 50%、3～4 人 30%、12 人 20%）"
    print(line)
    archive(entries, "run " + " ".join(sys.argv[2:]), line, started)


CAUSE_ZH = {"alive": "活到最后", "out": "出界", "obstacle": "撞障碍", "self": "撞自己的轨迹", "trail": "撞别人的轨迹",
            "headon": "抢格失败"}


def map_of(n, seed):
    r = subprocess.run([str(build_judge()), "map", str(n), str(seed)], capture_output=True, text=True, check=True)
    lines = r.stdout.split()
    W, H, N = map(int, lines[:3])
    rows = lines[3:3 + H]
    sp = [[int(lines[3 + H + 2 * k]), int(lines[4 + H + 2 * k])] for k in range(N)]
    return W, H, rows, sp


def render(n, seed, moves, upto):
    r = subprocess.run([str(build_judge()), "render"], input=f"{n} {seed} {upto}\n" + "\n".join(moves) + "\n",
                       capture_output=True, text=True, check=True)
    return r.stdout


def cmd_play(a):
    started = time.time()
    entries = [Entry(s) for s in a.bots]
    n = len(entries)
    if n not in (2, 3, 4, 12):
        raise Fail("参赛者个数（= 人数）只能是 2、3、4、12；同一个程序可以写多次")
    formal = not a.fast
    BUILD.mkdir(exist_ok=True)
    tmp = Path(tempfile.mkdtemp(prefix="err-", dir=BUILD)) if formal else None
    rec = run_games(entries, [(n, a.seed, list(range(n)))], formal, 1, tmp)[0]
    W, H, rows, sp = map_of(n, a.seed)
    sc = scores(rec)
    replay = {"format": "tron-replay-1", "engine": "tron-1.0", "mode": "formal" if formal else "fast", "n": n,
              "seed": a.seed, "W": W, "H": H, "rows": rows, "spawns": sp,
              "players": [e.spec for e in entries], "sha": [e.sha for e in entries],
              "turns": rec["turns"], "moves": rec["moves"], "rank": rec["rank"], "score": sc,
              "death_turn": rec["death_turn"], "cause": rec["cause"], "final": rec["final"],
              "first_ms": rec["first_ms"], "avg_ms": rec["avg_ms"], "max_ms": rec["max_ms"],
              "timeouts": rec["timeouts"], "invalid": rec["invalid"], "crash_turn": rec["crash_turn"]}
    print(f"{n} 人局，种子 {a.seed}，{'正式' if formal else '快速'}模式，共 {rec['turns']} 回合")
    print("座位  名次  名次分  结局                CPU 首回合/平均/最多（毫秒）  超时 不合法 崩溃  程序")
    for k, e in enumerate(entries):
        end = "活到最后" if rec["death_turn"][k] < 0 else f"第 {rec['death_turn'][k]} 回合{CAUSE_ZH[rec['cause'][k]]}"
        print(f"{k:>4}  {rec['rank'][k] + 1:>4}  {sc[k]:6.2f}  {end:<16}  {rec['first_ms'][k]:7.1f}/{rec['avg_ms'][k]:.1f}/"
              f"{rec['max_ms'][k]:.1f}  {rec['timeouts'][k]:>12} {rec['invalid'][k]:>6} "
              f"{'第' + str(rec['crash_turn'][k]) + '回合' if rec['crash_turn'][k] >= 0 else '-':>4}  {e.spec}")
    if tmp:
        logs = {}
        for k in range(n):
            f = tmp / f"g0_s{k}.txt"
            text = f.read_text(encoding="utf-8", errors="replace") if f.is_file() else ""
            logs[k] = text[-20000:]
            tail = text.strip().splitlines()[-a.tail:]
            if tail:
                print(f"\n{k} 号座位的调试输出（最后 {len(tail)} 行）：")
                print("\n".join("  " + t for t in tail))
        replay["stderr"] = logs
        shutil.rmtree(tmp, ignore_errors=True)
    print("\n终局（# 障碍，小写字母 = 各人轨迹，数字 = 活着的人的头，x = 死者的头）：")
    print(render(n, a.seed, rec["moves"], rec["turns"]))
    REPLAYS.mkdir(exist_ok=True)
    out = REPLAYS / f"{a.out or 'game-' + datetime.datetime.now().strftime('%H%M%S')}.json"
    out.write_text(json.dumps(replay, ensure_ascii=False), encoding="utf-8")
    print(f"回放：{out.relative_to(KIT)}（看某一回合：arena.py show {out.relative_to(KIT).as_posix()} 回合号）")
    archive(entries, "play " + " ".join(sys.argv[2:]), f"rank={rec['rank']} turns={rec['turns']}", started)


def cmd_show(a):
    rp = json.loads(Path(a.replay).read_text(encoding="utf-8"))
    t = rp["turns"] if a.turn is None else max(0, min(a.turn, rp["turns"]))
    print(f"{rp['n']} 人局，种子 {rp['seed']}，第 {t} 回合结算后（共 {rp['turns']} 回合）：")
    for k, name in enumerate(rp["players"]):
        dt = rp["death_turn"][k]
        st = "活着" if dt < 0 or dt >= t else f"已死（第 {dt} 回合{CAUSE_ZH[rp['cause'][k]]}）"
        nxt = rp["moves"][t][k] if t < rp["turns"] else "-"
        print(f"  {k} 号 {name}：{st}，下一步 {nxt}")
    print(render(rp["n"], rp["seed"], rp["moves"], t))


def cmd_check(a):
    started = time.time()
    print(f"Python {sys.version.split()[0]}")
    cxx = find_compiler()
    print(f"编译器：{cxx or '没有找到'}")
    if not cxx:
        raise Fail("没有找到 g++")
    me = Entry(a.bot)
    print("编译裁判程序……", flush=True)
    build_judge()
    print(f"编译 {a.bot}（正式、快速两种）……", flush=True)
    me.program(True)
    me.program(False)
    ok = True
    for n in (2, 3, 4, 12):
        lineup = [me] + [Entry("greedy")] * (n - 1)
        games = [(n, 1, [0] + list(range(1, n)))]
        f = run_games([me, *lineup[1:]], games, False, 1)[0]
        g = run_games([me, *lineup[1:]], games, True, 1)[0]
        same = f["moves"] == g["moves"]
        print(f"  {n:>2} 人局（对 greedy）：快速模式 {f['turns']} 回合、名次 {f['rank'][0] + 1}；"
              f"正式模式 {g['turns']} 回合、名次 {g['rank'][0] + 1}，CPU 最多 {g['max_ms'][0]:.1f} 毫秒，"
              f"超时 {g['timeouts'][0]}，不合法 {g['invalid'][0]}，"
              f"{'崩溃' if g['crash_turn'][0] >= 0 else '没有崩溃'}；两种模式{'逐回合相同' if same else '不同（按时间停的 bot 正常，否则请检查）'}")
        ok &= g["crash_turn"][0] < 0 and g["invalid"][0] == 0
    print("检查通过。" if ok else "检查发现问题：见上面。")
    archive([me], "check " + " ".join(sys.argv[2:]), "ok" if ok else "problem", started)


def main():
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    p = argparse.ArgumentParser(prog="arena.py", description="光轮对战本地对战工具")
    sub = p.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check", help="检查环境，确认提交能编译、能跑")
    c.add_argument("bot", nargs="?", default="submission/bot.cpp")
    r = sub.add_parser("run", help="批量对战并按正式公式估分")
    r.add_argument("bot", nargs="?", default="submission/bot.cpp")
    r.add_argument("--n", default="all", help="人数：2、3、4、12，或 1v1、3-4、12、all（默认），可用逗号连接")
    r.add_argument("--opp", default="greedy,sparring", help="对手，逗号分隔，轮流坐进其余座位（默认 greedy,sparring）")
    r.add_argument("--games", type=int, default=0, help="每种人数打几局（默认 2 人 40、3 人 30、4 人 40、12 人 24）")
    r.add_argument("--seed", type=int, default=1, help="第一张地图的种子（默认 1）")
    r.add_argument("--formal", action="store_true", help="正式模式（进程隔离、计时判罚）；默认快速模式")
    r.add_argument("--workers", type=int, default=0, help="并行数（默认快速 8、正式 4）")
    pl = sub.add_parser("play", help="打一局并存回放：参赛者按座位顺序列出，个数就是人数")
    pl.add_argument("bots", nargs="+")
    pl.add_argument("--seed", type=int, default=1)
    pl.add_argument("--fast", action="store_true", help="快速模式（默认正式模式）")
    pl.add_argument("--out", help="回放文件名（不含扩展名）")
    pl.add_argument("--tail", type=int, default=10, help="显示每个 bot 调试输出的最后几行")
    sh = sub.add_parser("show", help="看回放的某一回合")
    sh.add_argument("replay")
    sh.add_argument("turn", nargs="?", type=int)
    a = p.parse_args()
    os.chdir(KIT)
    try:
        {"check": cmd_check, "run": cmd_run, "play": cmd_play, "show": cmd_show}[a.cmd](a)
    except Fail as e:
        print(f"错误：{e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
