"""本地对战与复盘。

  arena.py check                                  检查环境
  arena.py play 程序 [程序] [--seed N] [--difficulty 普通|简单|困难] [--out 名字]
                                                  对战 1–2 个程序（.cpp 自动编译）。
                                                  只给 1 个程序时打单人挑战：对方是引擎 NPC 队
  arena.py batch 程序 程序 --games N [--seed N] [--difficulty 普通] [--workers W]
                                                  多局对战并汇总，每张地图轮换座位
  arena.py show 回放.json 回合 [--around K]      打印该回合前后的棋盘、单位和指令
"""
import argparse
import json
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import core  # noqa: E402
from engine import VERSION, Game, DIFF_LABELS  # noqa: E402

TEAM_CHARS = "01"
BUILD = HERE / "build"


def difficulty_of(text):
    if text is None:
        return 1
    table = {"简单": 0, "普通": 1, "困难": 2, "0": 0, "1": 1, "2": 2,
             "easy": 0, "normal": 1, "hard": 2}
    if str(text) not in table:
        raise SystemExit(f"难度应是 简单/普通/困难，收到 {text}")
    return table[str(text)]


def program(spec):
    """返回运行命令。.cpp 会先编译（结果缓存在 build/，源码不变就不重复编译）。"""
    if spec == "baseline":   # 开源版：没有编译好的基准时，用 baseline.cpp 源码自动编译
        path = HERE / ("baseline.exe" if os.name == "nt" else "baseline")
        if not path.is_file():
            path = HERE / "baseline.cpp"
    else:
        path = Path(spec).resolve()
    if not path.is_file():
        raise SystemExit(f"找不到程序：{spec}")
    try:
        return core.command_for(path, BUILD)
    except RuntimeError as exc:
        raise SystemExit(f"{spec}：{exc}")


def names_for(specs):
    names, seen = [], {}
    for s in specs:
        stem = "baseline" if s == "baseline" else Path(s).stem
        seen[stem] = seen.get(stem, 0) + 1
        names.append(stem if specs.count(s) == 1 and seen[stem] == 1 else f"{stem}#{seen[stem]}")
    return names


def result_line(result, names, difficulty):
    w = result["winner"]
    head = f"{VERSION}  难度 {DIFF_LABELS[difficulty]}  种子 {result['seed']}  "
    if len(names) == 1:
        if w == 0:
            tail = f"{names[0]} 获胜"
        elif w < 0:
            tail = "平局"
        else:
            tail = f"{names[0]} 的水晶被 NPC 队摧毁"
        return head + tail + f"（{result['reason']}）"
    if w < 0:
        return head + f"平局（{result['reason']}）"
    return head + f"{names[w]} 获胜（{result['reason']}）"


def play(specs, seed, out, difficulty):
    if not 1 <= len(specs) <= 2:
        raise SystemExit("需要 1（单人挑战 NPC 队）或 2（5v5 对战）个程序")
    programs = [program(s) for s in specs]
    names = names_for(specs)
    result, replay = core.run_match(Game, programs, names, seed, difficulty=difficulty)
    path = HERE / "replays" / out
    if replay is not None:
        core.save_replay(replay, path, HERE / "viewer.html")
    ranks = [1 + sum(s > x for s in result["score"]) for x in result["score"]]
    print(result_line(result, names, difficulty))
    for i, (n, b) in enumerate(zip(names, result["bots"])):
        extra = f"  故障：{b['dead']}" if b["dead"] else f"  失败 {len(b['faults'])} 次" if b["faults"] else ""
        print(f"  {TEAM_CHARS[i]}号队 {n:14s} 得分 {result['score'][i]:.1f}  "
              f"击杀 {result['stats'][i].get('kills', 0)}  推塔 {result['stats'][i].get('towers', 0)}  "
              f"第 {ranks[i]} 名  每回合平均 {b['avg_ms']:.1f}ms、最慢 {b['max_ms']:.1f}ms（首回合 {b['first_ms']:.0f}ms）{extra}")
        for line in b["stderr_tail"]:
            print(f"      stderr: {line}")
    if replay is not None:
        print(f"回放：{path.with_suffix('.html')}（网页）  {path.with_suffix('.json')}（数据）")


def _one(job):
    programs, names, seed, difficulty = job
    result, _ = core.run_match(Game, programs, names, seed, record=False, difficulty=difficulty)
    return names, result["score"], result["stats"], result["bots"]


def batch(specs, games, seed, difficulty, workers):
    """多局对战：2 个程序时每张地图把座位完整轮换一圈（2 局）；1 个程序时轮流挑战 NPC 队。"""
    from concurrent.futures import ProcessPoolExecutor
    if not 1 <= len(specs) <= 2:
        raise SystemExit("需要 1 或 2 个程序")
    programs, names = [program(s) for s in specs], names_for(specs)
    n = max(2, len(specs))
    jobs = []
    for k in range(games):
        r = k % n
        seats = programs[r:] + programs[:r] if len(specs) == 2 else programs
        sname = names[r:] + names[:r] if len(specs) == 2 else names
        jobs.append((seats, sname, seed + k // n, difficulty))
    workers = workers or max(1, min(8, (os.cpu_count() or 4) // 2))
    stats = {x: {"games": 0, "score": 0.0, "kills": 0, "towers": 0, "faults": 0, "max_ms": 0.0} for x in names}
    with ProcessPoolExecutor(workers) as pool:
        for seats, score, st, bots in pool.map(_one, jobs):
            for i, x in enumerate(seats):
                d = stats[x]
                d["games"] += 1
                d["score"] += score[i]
                d["kills"] += st[i].get("kills", 0)
                d["towers"] += st[i].get("towers", 0)
                d["faults"] += len(bots[i]["faults"])
                d["max_ms"] = max(d["max_ms"], bots[i]["max_ms"])
    print(f"{VERSION}  难度 {DIFF_LABELS[difficulty]}  {games} 局（地图种子 {seed}–{seed + (games - 1) // n}）  并行 {workers}")
    for x, d in sorted(stats.items(), key=lambda kv: -kv[1]["score"]):
        gms = d["games"]
        print(f"  {x:14s} 平均得分 {d['score'] / gms:4.2f}  场均击杀 {d['kills'] / gms:5.1f}  "
              f"场均推塔 {d['towers'] / gms:4.1f}  故障 {d['faults']}  最慢 {d['max_ms']:.0f}ms")
    print(f"并行局数越多，程序的每回合用时越容易被拉长；判断是否接近 {Game.TIME_LIMITS[1] * 1000:g} 毫秒的限时，请以单局 play 的结果为准。")


def show(path, turn, around):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    head, frames = data["header"], data["frames"]
    for f in frames:
        if abs(f["turn"] - turn) > around:
            continue
        grid = [list(row) for row in head["map"]]
        tw = {t["id"]: t for t in head["towers"]}
        for t in head["towers"]:
            hp = next(x["hp"] for x in f["towers"] if x["id"] == t["id"])
            if hp > 0:
                grid[t["pos"][1]][t["pos"][0]] = "T" if t["team"] == 0 else "t"
        for c in head["camps"]:
            hp = next(x["hp"] for x in f["camps"] if x["id"] == c["id"])
            grid[c["pos"][1]][c["pos"][0]] = "M" if hp > 0 else "."
        for m in f["minions"]:
            grid[m["pos"][1]][m["pos"][0]] = "i" if m["team"] == 0 else "!"
        for h in f["heroes"]:
            if h["pos"]:
                grid[h["pos"][1]][h["pos"][0]] = TEAM_CHARS[h["id"] // 5]
        print(f"\n===== 第 {f['turn']} 回合  水晶 {f['crystal'][0]} / {f['crystal'][1]} =====")
        print("图例：# 墙  T/t 塔  N 水晶  M 野怪  i/! 小兵  0/1 英雄\n")
        print("\n".join("".join(r) for r in grid))
        print("英雄：" + "  ".join(
            f"{h['id']}({TEAM_CHARS[h['id'] // 5]})" +
            (f"@{h['pos'][0]},{h['pos'][1]} {h['hp']}血 {h['level']}级" if h["pos"] else " 阵亡")
            for h in f["heroes"]))
        print("事件：" + ("；".join(f"{e['type']}:{e.get('line', '')}" for e in f["events"]) or "无"))
        for t, cmds in sorted(f["orders"].items(), key=lambda kv: int(kv[0])):
            print(f"指令 {t}：{json.dumps(cmds, ensure_ascii=False)}")
        for fault in f.get("faults", []):
            print(f"故障 {fault['team']}：{fault['reason']}")


def check():
    if sys.version_info < (3, 10):
        raise SystemExit(f"需要 Python 3.10 以上，当前是 {sys.version.split()[0]}")
    compiler = core.find_compiler()
    if not compiler:
        raise SystemExit("没有找到 C++ 编译器（g++ 或 clang++）。请把情况告诉组织方。")
    result, _ = core.run_match(Game, [program("baseline"), program("baseline")],
                               ["baseline", "baseline"], 1, record=False, difficulty=0)
    bad = [b for b in result["bots"] if b["faults"] or b["dead"]]
    if bad:
        raise SystemExit(f"试跑出错：{bad}")
    print(f"正常：Python {sys.version.split()[0]}，编译器 {compiler}，基准能完整打一局。")


def main():
    ap = argparse.ArgumentParser(description="峡谷对决本地对战与复盘")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("check")
    p = sub.add_parser("play")
    p.add_argument("programs", nargs="+")
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--difficulty", default="普通")
    p.add_argument("--out", default="latest")
    p = sub.add_parser("batch")
    p.add_argument("programs", nargs="+")
    p.add_argument("--games", type=int, required=True)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--difficulty", default="普通")
    p.add_argument("--workers", type=int, default=0)
    p = sub.add_parser("show")
    p.add_argument("replay")
    p.add_argument("turn", type=int)
    p.add_argument("--around", type=int, default=0)
    a = ap.parse_args()
    if a.cmd == "check":
        check()
    elif a.cmd == "play":
        play(a.programs, a.seed, a.out, difficulty_of(a.difficulty))
    elif a.cmd == "batch":
        batch(a.programs, a.games, a.seed, difficulty_of(a.difficulty), a.workers)
    else:
        show(a.replay, a.turn, a.around)


if __name__ == "__main__":
    main()
