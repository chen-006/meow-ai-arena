"""本地对战与复盘。

  arena.py check                                        检查环境
  arena.py play  A B [--seed N] [--style S] [--out 名字]   正式模式打一局（A 坐 0 号位），存回放
  arena.py show  回放 回合 [--around K]                   在终端里看某几个回合
  arena.py batch A B [--games N] [--seed N] [--workers W]  正式模式批量，每张图换边各打一局
  arena.py fast  A B [--games N] [--seed N] [--workers W]  快速模式批量（只支持源码）
  arena.py vs    新版 旧版 [--games N] [--mode fast|formal] 两个版本对比：胜平负、得分率和误差范围、有效样本
  arena.py expand 回放 [--out 文件]                        把回放展开成逐回合的局面和事件（JSON）
  arena.py html  回放 [--out 文件]                        把回放做成一个打开就能看的网页（play 存回放时会自动做一份）
  arena.py cpus                                         列出本机的处理器拓扑（大核、小核、超线程）
  arena.py speed [--cpu N]                              测速：跑一段固定工作量，看机器状态有没有变

A、B 可以是 .cpp 源码（自动编译，源码没变不重编）或已经编译好的程序。
每次运行都会把参与的源码按内容存一份到 history/，并在 history/log.jsonl 记一行。
"""
import argparse
import json
import math
import os
import queue
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import core  # noqa: E402
import engine  # noqa: E402

HISTORY = HERE / "history"
REPLAYS = HERE / "replays"
PARALLEL_NOTE = ("提醒：并行越多，同样的预算能做的事越少（抢缓存、超线程、落到小核上），用量也会有出入。"
                 "判断是否接近 {turn_ms} {unit}的时限，请以单局 play 的结果为准。")


def unit_of(inf):
    return "CPU 毫秒" if inf["budget_kind"] == "cpu" else "毫秒"


def parse_pin(text):
    """"16:18" 或 "16,17:18,19" → [[16], [18]]：冒号前是 A 绑的逻辑处理器，冒号后是 B 的。"""
    try:
        a, b = text.split(":")
        return [[int(x) for x in a.split(",") if x != ""], [int(x) for x in b.split(",") if x != ""]]
    except ValueError:
        raise SystemExit(f"--pin 的写法是 A的处理器:B的处理器，比如 16:18 或 16,17:18,19；收到的是 {text}")


# ====================================================================
# 参与者与版本存档
# ====================================================================

class Entry:
    """一个参与者：源码或可执行文件。"""

    def __init__(self, spec):
        self.spec = spec
        self.path = find_program(spec)
        self.is_source = core.is_source(self.path)
        self.name = self.path.stem
        self.data = self.path.read_bytes()
        self.sha = core.sha_of(self.data)
        self.saved = None       # 存档里的文件名（只有源码才存）

    def version(self):
        return {"spec": self.spec, "name": self.name, "kind": "source" if self.is_source else "program",
                "sha256": self.sha, "short": self.sha[:12], "saved": self.saved}


def find_program(spec):
    tried = [Path(spec), HERE / spec]
    if os.name == "nt" and not str(spec).lower().endswith((".exe", ".cpp")):
        tried += [Path(spec + ".exe"), HERE / (spec + ".exe")]
    for p in tried:
        if p.is_file():
            return p.resolve()
    if Path(spec).name.lower() in ("baseline", "baseline.exe") and (HERE / "baseline.cpp").is_file():   # 开源版加的提示
        raise SystemExit(f"找不到程序：{spec}。开源版不带编译好的基准 baseline.exe，把它换成 baseline.cpp（会自动编译，对局和 exe 逐回合相同）")
    raise SystemExit(f"找不到程序：{spec}")


def unique_names(entries):
    names, seen = [], {}
    for e in entries:
        seen[e.name] = seen.get(e.name, 0) + 1
        names.append(e.name if seen[e.name] == 1 else f"{e.name}#{seen[e.name]}")
    return names


def history_lock():
    """history/ 的跨进程锁：锁住 history/.lock 的第一个字节，返回文件描述符；拿不到返回 None。用完交给 history_unlock。

    几个 arena.py 命令同时运行时，存档和追加运行记录都要一个一个来。锁挂在打开的文件上，进程不管怎么结束
    系统都会放掉它，不会留下死锁。最多等 5 秒，等不到就不加锁往下做（宁可偶尔出一次错，也不卡住命令）。"""
    fd = None
    try:
        HISTORY.mkdir(parents=True, exist_ok=True)
        fd = os.open(HISTORY / ".lock", os.O_RDWR | os.O_CREAT, 0o644)
        deadline = time.time() + 5.0
        while True:
            try:
                if os.name == "nt":
                    import msvcrt
                    os.lseek(fd, 0, os.SEEK_SET)
                    msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return fd
            except OSError:
                if time.time() > deadline:
                    break
                time.sleep(0.02)
    except Exception:
        pass
    if fd is not None:
        os.close(fd)
    return None


def history_unlock(fd):
    if fd is None:
        return
    try:
        if os.name == "nt":
            import msvcrt
            os.lseek(fd, 0, os.SEEK_SET)
            msvcrt.locking(fd, msvcrt.LK_UNLCK, 1)
    except OSError:
        pass
    finally:
        os.close(fd)        # 关掉文件，没放开的锁也随之放开


def store_version(e):
    """把一份源码存进 history/，返回存档里的文件名。同一内容已经存过（包括别的命令刚刚存的）就用已有的那份。"""
    short = e.sha[:12]
    tmp = HISTORY / f"tmp-{os.getpid()}-{short}"
    problem = None
    try:
        for attempt in range(8):
            old = sorted(HISTORY.glob(f"*-{short}-*.cpp"))
            if old:
                return old[0].name
            name = f"{time.strftime('%Y%m%d-%H%M%S')}-{short}-{core.safe_name(e.name, 'bot')}.cpp"
            try:
                tmp.write_bytes(e.data)
                os.replace(tmp, HISTORY / name)
                return name
            except OSError as exc:      # 文件正被别的进程开着（另一个命令、杀毒软件）时 Windows 会拒绝：等一下再试
                problem = exc
                time.sleep(0.05 * (attempt + 1))
        raise problem
    finally:
        try:
            tmp.unlink()
        except OSError:
            pass


def archive(entries):
    """把每份源码按内容存进 history/（同一内容只存一次）。任何失败都只提示，不影响命令本身。

    几个命令同时启动、带着同一份新源码时，会在同一秒往同一个文件名上写：加锁让它们一个一个来，
    后到的直接用先到的那份。"""
    for e in entries:
        if not e.is_source:
            continue
        lock = history_lock()
        try:
            e.saved = store_version(e)
        except Exception as exc:
            print(f"（版本存档没有成功：{exc}。不影响本次运行。）", file=sys.stderr)
        finally:
            history_unlock(lock)


def write_log(command, argv, entries, summary, started, error=None):
    """在 history/log.jsonl 追加一行。任何失败都只提示，不影响命令本身。

    追加时加锁：Windows 上"移到文件末尾"和"写"是两步，几个命令同时写完时会写到同一个位置、互相盖掉。"""
    try:
        record = {"time": time.strftime("%Y-%m-%dT%H:%M:%S%z", time.localtime(started)), "epoch": round(started, 3),
                  "seconds": round(time.time() - started, 2), "command": command, "argv": list(argv),
                  "versions": [e.version() for e in entries], "summary": summary}
        if error:
            record["error"] = str(error)[:500]
        HISTORY.mkdir(parents=True, exist_ok=True)
        line = (json.dumps(record, ensure_ascii=False) + "\n").encode("utf-8")
        lock = history_lock()
        try:
            fd = os.open(HISTORY / "log.jsonl", os.O_WRONLY | os.O_APPEND | os.O_CREAT | getattr(os, "O_BINARY", 0), 0o644)
            try:
                os.write(fd, line)
            finally:
                os.close(fd)
        finally:
            history_unlock(lock)
    except Exception as exc:
        print(f"（运行记录没有写成：{exc}。不影响本次运行。）", file=sys.stderr)


def build(entry, kind):
    """编译一个参与者。kind 为 "exe"（正式模式）或 "dll"（快速模式）。"""
    if not entry.is_source:
        if kind == "dll":
            raise SystemExit(f"快速模式只支持源码：{entry.spec} 是已经编译好的程序。和它对战请用 batch，或 vs --mode formal。")
        return str(entry.path)
    try:
        return str(core.build_bot(entry.path, kind))
    except RuntimeError as exc:
        raise SystemExit(f"{entry.spec}：{exc}")


# ====================================================================
# 批量对战
# ====================================================================

def make_jobs(games, seed, style):
    """每张图换边各打一局：第 k 张图的种子是 seed + k，先 A 坐 0 号位，再 B 坐 0 号位。"""
    return [(seed + k // 2, style, k % 2) for k in range(games)]


def run_formal(commands, names, jobs, workers, record=False, pins=None):
    """正式模式打一批。每个并行位是一个独立的 Python 进程（自己起的），逐局领任务。返回与 jobs 等长的列表。

    record=True 时每个结果另带 "orders"（逐回合实际执行的命令）、"final"（终局回合段）、"faults"、"stderr"。
    pins：可选，每个并行位一对 [A 的逻辑处理器列表, B 的]，这个并行位上的 bot 进程绑在这些处理器上。"""
    results = [None] * len(jobs)
    todo = queue.Queue()
    for k in range(len(jobs)):
        todo.put(k)
    procs, lock = [], threading.Lock()
    stop = threading.Event()

    def start():
        p = subprocess.Popen([sys.executable, "-X", "utf8", str(HERE / "arena.py"), "_worker"],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE, cwd=str(HERE))
        core.adopt(p)
        with lock:
            procs.append(p)
        return p

    def work(slot):
        proc = None
        pin = pins[slot] if pins else None
        while not stop.is_set():
            try:
                k = todo.get_nowait()
            except queue.Empty:
                break
            seed, style, swap = jobs[k]
            order = [1, 0] if swap else [0, 1]
            job = {"cmds": [commands[i] for i in order], "names": [names[i] for i in order], "seed": seed,
                   "style": style, "record": record,
                   "affinity": [pin[i] for i in order] if pin else None}
            try:
                if proc is None or proc.poll() is not None:
                    proc = start()
                proc.stdin.write((json.dumps(job) + "\n").encode("utf-8"))
                proc.stdin.flush()
                raw = proc.stdout.readline()
                if not raw:
                    raise RuntimeError(f"裁判进程异常退出（代码 {proc.poll()}）")
                out = json.loads(raw.decode("utf-8"))
            except Exception as exc:
                if stop.is_set():
                    break
                out = {"error": f"{type(exc).__name__}: {exc}"}
                core.stop_process(proc)
                proc = None
            results[k] = out
        if proc is not None:
            try:
                proc.stdin.close()
                proc.wait(timeout=5)
            except Exception:
                pass

    threads = [threading.Thread(target=work, args=(w,), daemon=True) for w in range(max(1, min(workers, len(jobs))))]
    try:
        for t in threads:
            t.start()
        while any(t.is_alive() for t in threads):
            for t in threads:
                t.join(timeout=0.2)
    except KeyboardInterrupt:
        stop.set()
        raise
    finally:
        stop.set()
        with lock:
            for p in procs:         # 只结束自己起的进程；它们起的 bot 进程会跟着结束
                core.stop_process(p)
    out = []
    for (seed, style, swap), r in zip(jobs, results):
        if r is None or "error" in r:
            out.append({"seed": seed, "style": style, "swap": swap, "error": (r or {}).get("error", "没有跑完")})
            continue
        order = [1, 0] if swap else [0, 1]          # order[座位] = 第几个参与者
        usage = [None, None]
        for seat, who in enumerate(order):
            b = r["bots"][seat]
            usage[who] = {"first_ms": b["first_ms"], "avg_ms": b["avg_ms"], "max_ms": b["max_ms"],
                          "over": b["timeouts"], "illegal": b["illegal"], "dead": b["dead"]}
        out.append({"seed": seed, "style": style, "swap": swap, "turns": r["turns"], "winner": r["winner"],
                    "reason": r["reason"], "sig": r["sig"], "usage": usage})
        if record:
            out[-1].update({k: r[k] for k in ("orders", "final", "faults", "stderr", "used_ms")})
    return out


def worker_main():
    """批量对战的一个并行位：从标准输入逐行读任务，打完一局回一行结果。"""
    out = sys.stdout.buffer
    try:
        for raw in sys.stdin.buffer:
            job = json.loads(raw.decode("utf-8"))
            try:
                record = bool(job.get("record"))
                result, replay = core.run_match(engine.Game, job["cmds"], job["names"], job["seed"], record=record,
                                                affinity=job.get("affinity"), style=job["style"])
                if record:
                    result.update({k: replay[k] for k in ("orders", "faults", "stderr", "used_ms")})
                else:
                    result.pop("final", None)
                answer = result
            except Exception as exc:        # 裁判这一侧的错：报给主进程，这一局不算数
                answer = {"error": f"{type(exc).__name__}: {exc}"}
            out.write((json.dumps(answer, ensure_ascii=False) + "\n").encode("utf-8"))
            out.flush()
    except KeyboardInterrupt:
        pass
    finally:
        engine.shutdown()


REASONS = {"base": "基地被毁", "hp": "到回合上限比基地血量", "kills": "到回合上限比击毁数", "stars": "到回合上限比吃星数",
           "damage": "到回合上限比基地伤害", "draw": "到回合上限平局", "fault": "故障判负"}


def reason_text(reason):
    if reason.startswith("both_"):
        return "两个基地同回合被毁，" + {"draw": "平局"}.get(reason[5:], "比" + {"kills": "击毁数", "stars": "吃星数",
                                                                   "damage": "基地伤害", "hp": "基地血量"}.get(reason[5:], reason[5:]))
    return REASONS.get(reason, reason)


def summarize(names, results):
    """汇总一批对局（站在第一个参与者 A 的角度）。返回 (摘要字典, 要打印的若干行)。"""
    good = [r for r in results if "error" not in r]
    errors = [r for r in results if "error" in r]
    win = draw = loss = 0
    seat_wdl = [[0, 0, 0], [0, 0, 0]]
    by_map, reasons, lost = {}, {}, []
    for r in good:
        seat_a = 1 if r["swap"] else 0
        pts = 0.5 if r["winner"] < 0 else (1.0 if r["winner"] == seat_a else 0.0)
        win, draw, loss = win + (pts == 1.0), draw + (pts == 0.5), loss + (pts == 0.0)
        seat_wdl[seat_a][0 if pts == 1.0 else 1 if pts == 0.5 else 2] += 1
        by_map.setdefault((r["seed"], r["style"]), {})[r["swap"]] = (pts, r["sig"])
        reasons[r["reason"]] = reasons.get(r["reason"], 0) + 1
        if pts == 0.0:
            lost.append(f"种子 {r['seed']}（{seat_a} 号位）")
    n = len(good)
    lines = []
    summary = {"games": n, "wins": win, "draws": draw, "losses": loss, "judge_errors": len(errors)}
    if n:
        # 误差范围按图算：一张图上换边的两局合成一个样本，图与图之间才是独立的。
        per_map = [sum(p for p, _ in m.values()) / len(m) for m in by_map.values()]
        mean = sum(per_map) / len(per_map)
        half = None
        if len(per_map) >= 2:
            var = sum((x - mean) ** 2 for x in per_map) / (len(per_map) - 1)
            half = 1.96 * math.sqrt(var / len(per_map))
        rate = (win + 0.5 * draw) / n
        full = [m for m in by_map.values() if len(m) == 2]
        same = sum(1 for m in full if m[0][1] == m[1][1])
        summary.update(score_rate=round(rate, 4), ci95=None if half is None else round(half, 4), maps=len(by_map),
                       paired_maps=len(full), identical_maps=same, reasons=reasons)
        ci = f" ± {100 * half:.1f}%（95% 区间，按图计）" if half is not None else "（图太少，算不出误差范围）"
        lines.append(f"  {names[0]}：胜 {win}  平 {draw}  负 {loss}   得分率 {100 * rate:.1f}%{ci}")
        lines.append(f"  {names[0]} 坐 0 号位：胜 {seat_wdl[0][0]} 平 {seat_wdl[0][1]} 负 {seat_wdl[0][2]}    "
                     f"坐 1 号位：胜 {seat_wdl[1][0]} 平 {seat_wdl[1][1]} 负 {seat_wdl[1][2]}")
        lines.append(f"  {len(full)} 张图里有 {same} 张换边后的两局逐回合完全相同"
                     f"（两个程序在这些图上的表现没有任何差别），能看出差别的有 {len(full) - same} 张")
        lines.append("  结束方式：" + "，".join(f"{reason_text(k)} {v}" for k, v in sorted(reasons.items(), key=lambda kv: -kv[1])))
        usage = []
        for who in range(2):
            us = [r["usage"][who] for r in good]
            u = {"first_ms": max(x["first_ms"] for x in us), "avg_ms": sum(x["avg_ms"] for x in us) / len(us),
                 "max_ms": max(x["max_ms"] for x in us), "over": sum(x["over"] for x in us),
                 "illegal": sum(x["illegal"] for x in us), "dead": sum(1 for x in us if x.get("dead"))}
            usage.append({k: round(v, 3) for k, v in u.items()})
        summary["usage"] = usage
        width = max(len(x) for x in names)
        for who in range(2):
            u = usage[who]
            lines.append(f"  用量 {names[who]:<{width}}：首回合最慢 {u['first_ms']:.1f}ms，每回合平均 {u['avg_ms']:.3f}ms、"
                         f"最慢 {u['max_ms']:.1f}ms，超预算 {u['over']} 回合，输出不合法 {u['illegal']} 回合"
                         + (f"，故障判负 {u['dead']} 局" if u["dead"] else ""))
        if lost:
            lines.append(f"  {names[0]} 输掉的局：" + "、".join(lost[:12]) + (f" 等 {len(lost)} 局" if len(lost) > 12 else ""))
    if errors:
        lines.append(f"  有 {len(errors)} 局因为裁判这一侧出错没有结果（不计入上面的统计）。第一条：{errors[0]['error'][:200]}")
    return summary, lines


def brief_line(names, s):
    """一行汇总：谁对谁、胜-平-负、得分率 ± 误差范围、局数、有无故障。批量命令把它放在输出的最后一行，方便截取。"""
    faults = []
    for who, u in enumerate(s.get("usage", [])):
        parts = [f"{text} {u[key]} {unit}" for key, text, unit in (("over", "超预算", "回合"), ("illegal", "输出不合法", "回合"),
                                                                 ("dead", "故障判负", "局")) if u[key]]
        if parts:
            faults.append(f"{names[who]} " + "、".join(parts))
    if s["judge_errors"]:
        faults.append(f"裁判出错没有结果 {s['judge_errors']} 局")
    fault_text = "故障：" + ("；".join(faults) if faults else "无")
    if not s["games"]:
        return f"汇总：{names[0]} 对 {names[1]}  没有打成的局  {fault_text}"
    ci = "" if s["ci95"] is None else f" ± {100 * s['ci95']:.1f}%"
    return (f"汇总：{names[0]} 对 {names[1]}  {s['wins']}-{s['draws']}-{s['losses']}（胜-平-负）  "
            f"得分率 {100 * s['score_rate']:.1f}%{ci}  {s['games']} 局  {fault_text}")


def series(mode, entries, games, seed, style, workers, pins=None, last_line=True):
    """打一批并打印汇总。返回摘要。last_line=False 时不打印最后那一行汇总（vs 后面还有话要说，由它自己最后打印）。"""
    inf = engine.info()
    names = unique_names(entries)
    jobs = make_jobs(games, seed, engine.style_number(style))
    began = time.perf_counter()
    if mode == "fast":
        libs = [build(e, "dll") for e in entries]
        workers = workers or max(1, min(8, (os.cpu_count() or 4) // 2))
        try:
            raw = engine.run_fast(libs[0], libs[1], jobs, workers=workers, names=names)
        except engine.FastError as exc:
            raise SystemExit(f"快速模式中止：{exc}\n（快速模式下两个 bot 在同一个进程里，一方出事整批都停。）")
        results = []
        for r in raw:
            for u in r["usage"]:
                u["dead"] = None
            seat_a = 1 if r["swap"] else 0
            if r["reason"] == "fault":
                loser_seats = [0, 1] if r["winner"] < 0 else [1 - r["winner"]]
                for s in loser_seats:
                    r["usage"][0 if s == seat_a else 1]["dead"] = "连续输出不合法"
            results.append(r)
    else:
        commands = [[build(e, "exe")] for e in entries]
        workers = len(pins) if pins else (workers or max(1, min(8, (os.cpu_count() or 4) // 4)))
        results = run_formal(commands, names, jobs, workers, pins=pins)
    seconds = time.perf_counter() - began
    maps = (games + 1) // 2
    label = "快速模式" if mode == "fast" else "正式模式"
    print(f"{inf['engine']}  {label}  {names[0]} 对 {names[1]}  {games} 局"
          f"（{maps} 张图，种子 {seed}–{seed + maps - 1}，每张图换边各打一局）  并行 {workers}  用时 {seconds:.1f} 秒")
    summary, lines = summarize(names, results)
    print("\n".join(lines))
    if games % 2:
        print("  注意：局数是奇数，最后一张图只打了一个座位。")
    if mode == "fast":
        print(f"  快速模式下预算只统计、不判罚；用量的单位是 {unit_of(inf)}，只算到调用 bot 的那个线程。")
    else:
        print(f"  用量的单位是 {unit_of(inf)}。" + PARALLEL_NOTE.format(turn_ms=inf["turn_ms"], unit=unit_of(inf)))
    summary.update(mode=mode, seed=seed, style=style, workers=workers, seconds=round(seconds, 2))
    if last_line:
        print(brief_line(names, summary))
    return summary


# ====================================================================
# 各个命令
# ====================================================================

def cmd_play(args, entries):
    inf = engine.info()
    names = unique_names(entries)
    commands = [[build(e, "exe")] for e in entries]
    style = engine.style_number(args.style)
    try:
        result, replay = core.run_match(engine.Game, commands, names, args.seed, style=style,
                                        affinity=parse_pin(args.pin) if args.pin else None)
    except core.MatchError as exc:
        raise SystemExit(f"裁判这一侧出错，这一局没有结果：{exc}")
    replay["versions"] = [e.version() for e in entries]
    path = core.save_replay(replay, REPLAYS / (core.safe_name(args.out, "latest") + ".json"))
    w = result["winner"]
    outcome = "平局" if w < 0 else f"{w} 号位 {names[w]} 获胜"
    print(f"{inf['engine']}  正式模式  种子 {args.seed}  风格 {args.style}")
    print(f"结果：{outcome}（{reason_text(result['reason'])}），共 {result['turns']} 回合。"
          f"基地血量 {result['base_hp'][0]}:{result['base_hp'][1]}，击毁 {result['kills'][0]}:{result['kills'][1]}，"
          f"吃星 {result['stars_eaten'][0]}:{result['stars_eaten'][1]}")
    for i, (n, b) in enumerate(zip(names, result["bots"])):
        extra = f"  判负：{b['dead']}" if b["dead"] else ""
        print(f"  {i} 号位 {n:12s} 首回合 {b['first_ms']:.1f}（预算 {inf['first_ms']}），之后每回合平均 {b['avg_ms']:.2f}、"
              f"最多 {b['max_ms']:.2f}（预算 {inf['turn_ms']}），超预算 {b['timeouts']} 回合，输出不合法 {b['illegal']} 回合{extra}")
    print(f"  用量的单位是 {unit_of(inf)}" + ("：bot 进程自己消耗的 CPU 时间，所有线程合计。" if inf["budget_kind"] == "cpu" else "。"))
    for f in replay["faults"][:10]:
        print(f"  故障：第 {f['turn']} 回合 {f['seat']} 号位 {f['detail']}")
    if len(replay["faults"]) > 10:
        print(f"  ……共 {len(replay['faults'])} 条故障，都在回放里")
    for i, n in enumerate(names):
        lines = replay["stderr"][i]
        if lines:
            print(f"  {i} 号位 {n} 的调试输出（最后 {min(10, len(lines))} 行，共 {len(lines)} 行，全部在回放里）：")
            for turn, text in lines[-10:]:
                print(f"      [回合 {turn}] {text}")
    rel = os.path.relpath(path, HERE).replace("\\", "/")
    print(f"回放：{rel}    看某一回合：arena.py show {rel} 回合号 --around 2")
    try:                                    # 网页版回放；做不成只提示，不影响这一局的结果
        page = write_html(engine.expand(replay), path.with_suffix(".html"))
        print(f"网页回放：{os.path.relpath(page, HERE).replace(chr(92), '/')}    用浏览器打开就能看")
    except Exception as exc:
        print(f"（网页回放没有生成：{exc}。不影响本次结果。）", file=sys.stderr)
    return {"seed": args.seed, "style": args.style, "winner": w, "reason": result["reason"], "turns": result["turns"],
            "sig": result["sig"], "replay": rel,
            "bots": [{k: b[k] for k in ("dead", "timeouts", "illegal", "first_ms", "avg_ms", "max_ms")}
                     for b in result["bots"]]}


TANK_CHARS = ["FGA", "fga"]                 # 0 号方大写，1 号方小写：F 快速、G 火力、A 装甲
TYPE_CN = {"fast": "快速", "fire": "火力", "armor": "装甲"}
ARROWS = {"U": "^", "D": "v", "L": "<", "R": ">"}


def describe_event(ev):
    t = ev["type"]
    tank = lambda i: f"{i} 号车"       # noqa: E731
    if t == "fire":
        return f"{tank(ev['tank'])}向 {ev['dir']} 开炮"
    if t == "blocked":
        return f"{tank(ev['tank'])}第 {ev['sub']} 小步没走成"
    if t == "cancel":
        return f"子弹在 ({ev['x']},{ev['y']}) {'对穿' if ev['how'] == 'cross' else '同格'}抵消"
    if t == "hit":
        return f"{tank(ev['tank'])}在 ({ev['x']},{ev['y']}) 被 {ev['by']} 号车的子弹打中"
    if t == "base_hit":
        return f"{ev['seat']} 号方基地被 {ev['by']} 号车打中"
    if t == "wall":
        return f"({ev['x']},{ev['y']}) 的{'砖墙' if ev['kind'] == 'brick' else '钢墙'}被打掉"
    if t == "death":
        return f"{tank(ev['tank'])}在 ({ev['x']},{ev['y']}) 被毁" + (f"（被 {ev['by']} 号车复活炸毁）" if ev["by"] >= 0 else "")
    if t == "respawn":
        return f"{tank(ev['tank'])}在 ({ev['x']},{ev['y']}) 复活"
    if t == "bullet_cleared":
        return f"{tank(ev['tank'])}复活，({ev['x']},{ev['y']}) 上的子弹消失"
    if t == "star":
        return f"{tank(ev['tank'])}在 ({ev['x']},{ev['y']}) 吃到星星，升到 {ev['level']} 级"
    if t == "supply":
        return f"({ev['x']},{ev['y']}) 出了一颗星星"
    return ev["text"]


def cmd_show(args):
    path = Path(args.replay)
    try:
        data = engine.expand(core.load_replay(path))
    except (OSError, ValueError, KeyError) as exc:
        raise SystemExit(f"回放读不了：{args.replay}（{exc}）")
    names, size, frames = data["names"], data["size"], data["frames"]
    res = data["result"]
    last = frames[-1]["turn"]
    print(f"回放 {args.replay}：{names[0]}（0 号位，在下方，大写）对 {names[1]}（1 号位，在上方，小写），共 {last} 回合，"
          f"{'平局' if res['winner'] < 0 else str(res['winner']) + ' 号位获胜'}（{reason_text(res['reason'])}）")
    print("图例：. 空地  # 砖墙  @ 钢墙  ~ 水  0/1 基地  * 星星  ^v<> 子弹（+ 表示一格里有几发）  "
          "F/G/A = 0 号方的快速/火力/装甲，f/g/a = 1 号方的")
    shown = 0
    for f in frames:
        if abs(f["turn"] - args.turn) > args.around:
            continue
        shown += 1
        grid = [list(row) for row in f["map"]]
        for x, y in f["stars"]:
            grid[y][x] = "*"
        for b in f["bullets"]:
            grid[b["y"]][b["x"]] = "+" if grid[b["y"]][b["x"]] in "^v<>+" else ARROWS[b["dir"]]
        for t in f["tanks"]:
            if t["alive"]:
                grid[t["y"]][t["x"]] = TANK_CHARS[t["seat"]][t["id"] % 3]
        print(f"\n===== 第 {f['turn']} 回合结算后  基地血量 {f['base_hp'][0]}:{f['base_hp'][1]}  击毁 {f['kills'][0]}:{f['kills'][1]}"
              f"  吃星 {f['stars_eaten'][0]}:{f['stars_eaten'][1]} =====")
        if f["orders"]:
            for seat in range(2):
                o = f["orders"][seat]
                ms = f["used_ms"][seat] if f["used_ms"] and f["used_ms"][seat] is not None else None
                print(f"命令 {seat} 号位：快速 {o[0]:3s} 火力 {o[1]:3s} 装甲 {o[2]:3s}" + (f"  用量 {ms:.2f}ms" if ms is not None else ""))
        for fault in f["faults"]:
            print(f"故障 {fault['seat']} 号位：{fault['detail']}")
        if f["events"]:
            print("事件：" + "；".join(describe_event(e) for e in f["events"]))
        print("    " + "".join(str(x % 10) for x in range(size)))
        for y, row in enumerate(grid):
            print(f"{y:3d} " + "".join(row))
        for t in f["tanks"]:
            who = f"{t['id']} 号车 {t['seat']} 号方{TYPE_CN[t['type']]}（{TANK_CHARS[t['seat']][t['id'] % 3]}）"
            if t["alive"]:
                print(f"  {who} 在 ({t['x']},{t['y']})  血 {t['hp']}  等级 {t['level']}  场上子弹 {t['bullets_out']}/{t['bullet_cap']}"
                      f"  {'可以开炮' if t['can_fire'] else '装填还要 ' + str(t['reload_left']) + ' 回合' if t['reload_left'] else '子弹已达上限'}")
            else:
                print(f"  {who} 不在场，还要 {t['respawn_in']} 回合复活")
        if f["bullets"]:
            print("  子弹：" + "  ".join(f"({b['x']},{b['y']}){ARROWS[b['dir']]} {b['owner']} 号车的" + ("（能破钢）" if b["steel"] else "")
                                      for b in f["bullets"]))
        for seat in range(2):
            lines = [text for turn, text in data["stderr"][seat] if turn == f["turn"]] if data.get("stderr") else []
            for text in lines[:8]:
                print(f"  调试输出 {seat} 号位：{text}")
    if not shown:
        print(f"\n这局只有第 0–{last} 回合，没有第 {args.turn} 回合。")
    if not data["consistent"]:
        print("\n注意：重新结算出的终局和回放里记的不一样，这份回放可能不是用当前的引擎打的。")


def cmd_expand(args):
    path = Path(args.replay)
    try:
        data = engine.expand(core.load_replay(path))
    except (OSError, ValueError, KeyError) as exc:
        raise SystemExit(f"回放读不了：{args.replay}（{exc}）")
    name = path.name[:-3] if path.name.endswith(".gz") else path.name
    out = Path(args.out) if args.out else path.with_name(Path(name).stem + ".frames.json")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(data, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    print(f"已展开 {len(data['frames'])} 帧（第 0–{data['frames'][-1]['turn']} 回合）：{out}")


# ---- 网页回放：把展开后的数据嵌进 viewer.html ----

VIEWER = HERE / "viewer.html"
VIEWER_MARK = "/*__TANK_DATA__*/null"       # viewer.html 里留给数据的位置，只出现一次


def write_html(frames, out):
    """把展开后的回放（engine.expand 的结果）嵌进 viewer.html，存成 out。返回 out。"""
    page = VIEWER.read_text(encoding="utf-8")
    if page.count(VIEWER_MARK) != 1:
        raise ValueError("viewer.html 里没有找到放数据的位置")
    text = json.dumps(frames, ensure_ascii=False, separators=(",", ":"))
    # 数据是直接写进 <script> 里的：把 < 和两个行分隔符转义掉，调试输出里出现 </script> 之类也不会把网页截断
    text = text.replace("<", "\\u003c").replace("\u2028", "\\u2028").replace("\u2029", "\\u2029")
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(page.replace(VIEWER_MARK, text), encoding="utf-8")
    return out


def cmd_html(args):
    path = Path(args.replay)
    try:
        data = core.load_replay(path)
        if data.get("format") != engine.FRAMES_FORMAT:      # 给的是原始回放就先展开；已经展开过的直接用
            data = engine.expand(data)
    except (OSError, ValueError, KeyError) as exc:
        raise SystemExit(f"回放读不了：{args.replay}（{exc}）")
    name = path.name[:-3] if path.name.endswith(".gz") else path.name
    stem = Path(name).stem
    stem = stem[:-7] if stem.endswith(".frames") else stem
    try:
        out = write_html(data, Path(args.out) if args.out else path.with_name(stem + ".html"))
    except (OSError, ValueError) as exc:
        raise SystemExit(f"网页没有生成：{exc}")
    print(f"网页回放（{len(data['frames']) - 1} 回合）：{out}    用浏览器打开就能看")


def cmd_cpus():
    cores = core.cpu_topology()
    if not cores:
        print(f"这个系统上读不到处理器拓扑。逻辑处理器共 {os.cpu_count()} 个。")
        return
    classes = sorted({c["class"] for c in cores}, reverse=True)
    print(f"物理核 {len(cores)} 个，逻辑处理器 {sum(len(c['cpus']) for c in cores)} 个。")
    for cls in classes:
        group = [c for c in cores if c["class"] == cls]
        label = "同构" if len(classes) == 1 else ("大核" if cls == classes[0] else "小核")
        ht = sum(1 for c in group if len(c["cpus"]) > 1)
        print(f"{label}（效率等级 {cls}）：{len(group)} 个物理核" + (f"，其中 {ht} 个开了超线程" if ht else "，没有超线程"))
        print("  每个物理核的逻辑处理器编号：" + "  ".join("+".join(map(str, c["cpus"])) for c in group))
    print("同一个物理核的两个逻辑处理器（用 + 连着的）会互相抢，绑核时一个物理核只放一个程序。")


def run_speed(cpu=None, repeat=5):
    try:
        exe = core.build_tool("speed")
    except RuntimeError as exc:
        raise SystemExit(f"测速程序编译不了：{exc}")
    cmd = [str(exe), "--repeat", str(repeat)] + (["--cpu", str(cpu)] if cpu is not None else [])
    r = subprocess.run(cmd, capture_output=True, encoding="utf-8", errors="replace", timeout=300)
    words = dict(w.split("=", 1) for w in r.stdout.split()[1:] if "=" in w) if r.returncode == 0 else None
    if not words:
        raise SystemExit(f"测速程序没有跑成：{(r.stderr or r.stdout).strip()[:200]}")
    return {"cpu": cpu, "ghz": float(words["ghz"]), "loop_ms": float(words["loop_ms"]),
            "engine_ms": float(words["engine_ms"]), "engine_cpu_ms": float(words["engine_cpu_ms"]),
            "units_per_ms": int(words["per_ms"]), "units_source": words["source"]}


def cmd_speed(args):
    targets = [(f"逻辑处理器 {args.cpu}", args.cpu)] if args.cpu is not None else [("不绑核", None)]
    if args.cpu is None:
        cores = core.cpu_topology()
        classes = sorted({c["class"] for c in cores}, reverse=True)
        for cls in classes:
            first = next(c for c in cores if c["class"] == cls)
            label = "同构核" if len(classes) == 1 else ("大核" if cls == classes[0] else "小核")
            targets.append((f"{label}（逻辑处理器 {first['cpus'][0]}）", first["cpus"][0]))
    rows = []
    for label, cpu in targets:
        m = run_speed(cpu, args.repeat)
        rows.append({"where": label, **m})
        print(f"{label}：实际主频约 {m['ghz']:.2f} GHz；固定工作量 墙上 {m['engine_ms']:.1f} 毫秒，CPU {m['engine_cpu_ms']:.1f} 毫秒")
    m = rows[0]
    print(f"1 CPU 毫秒 = {m['units_per_ms']} 个读数单位（来源：{m['units_source']}）。"
          "各项都是重复几次取最快的一次；和上一次测的差出 10% 以上，说明机器状态变了（电源模式、别的任务）。")
    return {"speed": rows}


def cmd_check(entries):
    if sys.version_info < (3, 10):
        raise SystemExit(f"需要 Python 3.10 以上，当前是 {sys.version.split()[0]}")
    compiler = core.find_compiler()
    if not compiler:
        raise SystemExit("没有找到 C++ 编译器（g++ 或 clang++）。请把这个情况报告出来。")
    if not all(ord(ch) < 128 for ch in str(HERE)):
        print(f"注意：当前目录的路径里有非英文字符（{HERE}），编译器可能打不开。建议放到纯英文路径下。")
    try:
        inf = engine.info()
        core.build_tool("fast")
    except (core.MatchError, RuntimeError) as exc:
        raise SystemExit(f"裁判程序编译或运行不了：{exc}\n"
                         "（如果是刚编译出的程序被系统拦截，请确认本目录在允许运行新程序的位置。）")
    print(f"Python {sys.version.split()[0]}；编译器 {compiler}；引擎 {inf['engine']}；"
          f"预算：首回合 {inf['first_ms']} {unit_of(inf)}，之后每回合 {inf['turn_ms']} {unit_of(inf)}"
          + (f"（本机 1 CPU 毫秒 = {inf['units_per_ms']} 个周期）。" if inf["budget_kind"] == "cpu" and os.name == "nt" else "。"))
    if not entries:
        print("没有找到 submission/bot.cpp，只检查了裁判程序。")
        return {"ok": True, "bot": False}
    e = entries[0]
    exe, dll = build(e, "exe"), build(e, "dll")
    try:
        result, _ = core.run_match(engine.Game, [[exe], [exe]], ["bot", "bot#2"], 1, record=False)
    except core.MatchError as exc:
        raise SystemExit(f"试跑出错：{exc}\n（如果是刚编译出的程序被系统拦截，请确认本目录在允许运行新程序的位置。）")
    bad = [b for b in result["bots"] if b["faults"] or b["dead"]]
    if bad:
        raise SystemExit(f"{e.spec} 自己打自己时出了故障：{bad[0]['dead'] or '有超时或不合法的输出'}。用 play 看细节。")
    try:
        fast = engine.run_fast(dll, dll, [(1, -1, 0)], names=("bot", "bot#2"))[0]
    except engine.FastError as exc:
        raise SystemExit(f"快速模式试跑出错：{exc}")
    same = fast["sig"] == result["sig"]
    print(f"正常：{e.spec} 能编译，正式模式和快速模式各自打完了一局（{result['turns']} 回合）"
          + ("，两种模式打出的对局逐回合相同。" if same else
             "。两种模式打出的对局不同：如果 bot 的决策依赖时间（比如按剩余预算决定搜多深）这是正常的，否则请检查有没有用到未初始化的变量。"))
    return {"ok": True, "bot": True, "same_in_both_modes": same}


def main():
    core.setup_console()
    if sys.argv[1:2] == ["_worker"]:
        return worker_main()
    ap = argparse.ArgumentParser(description="坦克大战本地对战与复盘", formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("check", help="检查环境")
    p = sub.add_parser("play", help="正式模式打一局")
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--style", default="auto", help="地图风格：auto（按种子轮换）、open、brick、water、steel")
    p.add_argument("--out", default="latest", help="回放存成 replays/名字.json")
    p.add_argument("--pin", default=None, help="把两个程序绑到指定的逻辑处理器：A的:B的，比如 16:18（默认不绑）")
    for name, games, text in (("batch", 20, "正式模式批量"), ("fast", 200, "快速模式批量"), ("vs", 100, "两个版本对比")):
        p = sub.add_parser(name, help=text)
        p.add_argument("a")
        p.add_argument("b")
        p.add_argument("--games", type=int, default=games, help=f"局数（默认 {games}；每张图换边各打一局）")
        p.add_argument("--seed", type=int, default=1, help="第一张图的种子")
        p.add_argument("--style", default="auto")
        p.add_argument("--workers", type=int, default=0, help="并行数")
        if name == "batch":
            p.add_argument("--pin", nargs="+", default=None,
                           help="每个并行位一对 A的:B的 逻辑处理器，比如 --pin 16:18 20:22；给了它并行数就是对数")
        if name == "vs":
            p.add_argument("--mode", choices=["auto", "fast", "formal"], default="auto",
                           help="auto：两边都是源码用快速模式，否则用正式模式")
    p = sub.add_parser("show", help="在终端里看某几个回合")
    p.add_argument("replay")
    p.add_argument("turn", type=int)
    p.add_argument("--around", type=int, default=0)
    p = sub.add_parser("expand", help="把回放展开成逐回合的局面和事件")
    p.add_argument("replay")
    p.add_argument("--out", default=None)
    p = sub.add_parser("html", help="把回放做成一个打开就能看的网页")
    p.add_argument("replay")
    p.add_argument("--out", default=None)
    sub.add_parser("cpus", help="列出本机的处理器拓扑")
    p = sub.add_parser("speed", help="测速")
    p.add_argument("--cpu", type=int, default=None, help="只测这个逻辑处理器")
    p.add_argument("--repeat", type=int, default=5)
    args = ap.parse_args()

    started = time.time()
    entries, summary, error = [], None, None
    try:
        if args.cmd in ("show", "expand", "html", "cpus"):
            return {"show": cmd_show, "expand": cmd_expand, "html": cmd_html, "cpus": lambda a: cmd_cpus()}[args.cmd](args)
        if args.cmd == "speed":
            summary = cmd_speed(args)
            return
        if args.cmd == "check":
            bot = HERE / "submission" / "bot.cpp"
            entries = [Entry("submission/bot.cpp")] if bot.is_file() else []
        else:
            entries = [Entry(args.a), Entry(args.b)]
            if args.cmd != "play" and args.games < 1:
                raise SystemExit("--games 至少为 1")
        archive(entries)
        if args.cmd == "check":
            summary = cmd_check(entries)
        elif args.cmd == "play":
            summary = cmd_play(args, entries)
        elif args.cmd == "batch":
            summary = series("formal", entries, args.games, args.seed, args.style, args.workers,
                             pins=[parse_pin(x) for x in args.pin] if args.pin else None)
        elif args.cmd == "fast":
            summary = series("fast", entries, args.games, args.seed, args.style, args.workers)
        else:
            mode = args.mode
            if mode == "auto":
                mode = "fast" if all(e.is_source for e in entries) else "formal"
                if mode == "formal":
                    print("有一方是已经编译好的程序，用正式模式对比。")
            summary = series(mode, entries, args.games, args.seed, args.style, args.workers, last_line=False)
            s = summary
            if s.get("games"):
                useful = s["paired_maps"] - s["identical_maps"]
                print(f"怎么读：得分率高于 50% 且误差范围不跨过 50%，才能说 {entries[0].name} 更强。"
                      f"这次真正提供信息的是 {useful} 张图；换边后两局完全相同的图上，两个版本各赢一局或都平，不说明谁强。")
            print(brief_line(unique_names(entries), s))         # 一行汇总放在最后
    except KeyboardInterrupt:
        error = "被中断"
        print("\n已中断，子进程已清理。", file=sys.stderr)
        raise SystemExit(130)
    except ValueError as exc:
        error = exc
        raise SystemExit(str(exc))
    except core.MatchError as exc:
        error = exc
        raise SystemExit(f"裁判这一侧出错：{exc}")
    except SystemExit as exc:
        error = exc.code if isinstance(exc.code, str) else None
        raise
    finally:
        if args.cmd not in ("show", "expand", "html", "cpus"):
            write_log(args.cmd, sys.argv[1:], entries, summary, started, error)
        engine.shutdown()


if __name__ == "__main__":
    main()
