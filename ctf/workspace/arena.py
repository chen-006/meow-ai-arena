"""本地对战与复盘。

  arena.py check                                  检查环境
  arena.py play 程序 程序 ... [--seed N] [--out 名字]
                                                  对战 2–15 个程序（.cpp 会自动编译），baseline 表示练习基准
  arena.py batch 程序 程序 ... --games N [--seed N] [--workers W]
                                                  多局对战并汇总，每张地图轮换全部座位
  arena.py show 回放.json 回合 [--around K]      打印该回合前后 K 回合的棋盘、角色和指令
"""
import argparse
import json
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import core  # noqa: E402
from engine import VERSION, Game  # noqa: E402

TEAM_CHARS = "0123456789abcde"


BUILD = HERE / "build"


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


def play(specs, seed, out):
    if not 2 <= len(specs) <= 15:
        raise SystemExit("需要 2–15 个程序")
    programs = [program(s) for s in specs]
    names = names_for(specs)
    result, replay = core.run_match(Game, programs, names, seed)
    path = HERE / "replays" / out
    core.save_replay(replay, path, HERE / "viewer.html")
    ranks = [1 + sum(s > x for s in result["score"]) for x in result["score"]]
    print(f"{VERSION}  {len(specs)} 方  种子 {seed}")
    for i, (n, b) in enumerate(zip(names, result["bots"])):
        extra = f"  故障：{b['dead']}" if b["dead"] else f"  失败 {len(b['faults'])} 次" if b["faults"] else ""
        print(f"  {i}号位 {n:14s} 交旗 {result['score'][i]:3d}  第 {ranks[i]} 名  "
              f"每回合平均 {b['avg_ms']:.1f}ms、最慢 {b['max_ms']:.1f}ms（首回合 {b['first_ms']:.0f}ms）{extra}")
        for line in b["stderr_tail"]:
            print(f"      stderr: {line}")
    print(f"回放：{path.with_suffix('.html')}（网页）  {path.with_suffix('.json')}（数据）")


def _one(job):
    programs, names, seed = job
    result, _ = core.run_match(Game, programs, names, seed, record=False)
    return names, result["score"], result["bots"]


def batch(specs, games, seed, workers):
    """多局对战：每张地图把座位完整轮换一圈（n 方就是 n 局），再换下一张地图。"""
    import os
    from concurrent.futures import ProcessPoolExecutor
    n = len(specs)
    if not 2 <= n <= 15:
        raise SystemExit("需要 2–15 个程序")
    programs, names = [program(s) for s in specs], names_for(specs)
    jobs = []
    for k in range(games):
        r = k % n
        jobs.append((programs[r:] + programs[:r], names[r:] + names[:r], seed + k // n))
    workers = workers or max(1, min(8, (os.cpu_count() or 4) // 2 // n))
    stats = {x: {"games": 0, "score": 0, "rank": 0, "first": 0, "faults": 0, "max_ms": 0.0} for x in names}
    with ProcessPoolExecutor(workers) as pool:
        for seats, score, bots in pool.map(_one, jobs):
            ranks = [1 + sum(s > x for s in score) for x in score]
            for i, x in enumerate(seats):
                st = stats[x]
                st["games"] += 1
                st["score"] += score[i]
                st["rank"] += ranks[i]
                st["first"] += ranks[i] == 1
                st["faults"] += len(bots[i]["faults"]) + (1 if bots[i]["dead"] else 0)
                st["max_ms"] = max(st["max_ms"], bots[i]["max_ms"])
    print(f"{VERSION}  {n} 方  {games} 局（地图种子 {seed}–{seed + (games - 1) // n}，每张图轮换全部座位）  并行 {workers}")
    for x, st in sorted(stats.items(), key=lambda kv: -kv[1]["score"]):
        g = st["games"]
        print(f"  {x:14s} 平均交旗 {st['score'] / g:5.1f}  平均名次 {st['rank'] / g:4.2f}  "
              f"第一 {st['first']}/{g}  故障 {st['faults']}  最慢 {st['max_ms']:.0f}ms")
    if games % n:
        print(f"注意：局数不是 {n} 的整数倍，最后一张地图没有轮换完所有座位。")
    print(f"并行局数越多，程序的每回合用时越容易被拉长；判断是否接近 {Game.TIME_LIMITS[1] * 1000:g} 毫秒的限时，请以单局 play 的结果为准。")


def show(path, turn, around):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    head, frames = data["header"], data["frames"]
    walls = head["map"]
    base_of = {tuple(p): t for t, b in enumerate(head["bases"]) for p in b}
    for f in frames:
        if abs(f["turn"] - turn) > around:
            continue
        grid = [list(row) for row in walls]
        for (x, y), t in base_of.items():
            grid[y][x] = "+"
        for fl in f["flags"]:
            if fl["pos"]:
                grid[fl["pos"][1]][fl["pos"][0]] = "*"
        for u in f["units"]:
            if u["pos"]:
                grid[u["pos"][1]][u["pos"][0]] = TEAM_CHARS[u["id"] // 3]
        print(f"\n===== 第 {f['turn']} 回合结束  得分 {f['score']} =====")
        print("图例：# 墙  + 基地  * 地上的旗  数字/字母 = 该阵营的角色\n")
        print("\n".join("".join(r) for r in grid))
        print("\n角色：" + "  ".join(
            f"{u['id']}({TEAM_CHARS[u['id'] // 3]})" +
            (f"@{u['pos'][0]},{u['pos'][1]} {u['hp']}血" + (f" 携旗{u['flag']}" if u["flag"] is not None else "")
             if u["pos"] else " 阵亡") for u in f["units"]))
        print("事件：" + ("；".join(json.dumps(e, ensure_ascii=False) for e in f["events"]) or "无"))
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
    result, _ = core.run_match(Game, [program("baseline"), program(str(HERE / "submission" / "bot.cpp"))],
                               ["baseline", "bot"], 1, record=False)
    bad = [b for b in result["bots"] if b["faults"] or b["dead"]]
    if bad:
        raise SystemExit(f"试跑出错：{bad}")
    print(f"正常：Python {sys.version.split()[0]}，编译器 {compiler}，模板和基准都能运行。")


def main():
    ap = argparse.ArgumentParser(description="夺旗本地对战与复盘")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("check")
    p = sub.add_parser("play")
    p.add_argument("programs", nargs="+")
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--out", default="latest")
    p = sub.add_parser("batch")
    p.add_argument("programs", nargs="+")
    p.add_argument("--games", type=int, required=True)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--workers", type=int, default=0)
    p = sub.add_parser("show")
    p.add_argument("replay")
    p.add_argument("turn", type=int)
    p.add_argument("--around", type=int, default=0)
    a = ap.parse_args()
    if a.cmd == "check":
        check()
    elif a.cmd == "play":
        play(a.programs, a.seed, a.out)
    elif a.cmd == "batch":
        batch(a.programs, a.games, a.seed, a.workers)
    else:
        show(a.replay, a.turn, a.around)


if __name__ == "__main__":
    main()
