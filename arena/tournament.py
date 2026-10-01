"""正式赛：定赛程 → 跑比赛（可中断续跑）→ 出成绩。

  python tournament.py plan  赛事配置.json  赛事目录
  python tournament.py run   赛事目录 --workers 4
  python tournament.py rate  赛事目录

赛事配置（JSON）：
  game           游戏引擎文件（提供 Game 类）
  anchor         基准的名字，实力值以它为 0
  players        {名字: 程序路径}
  sizes          要打的单局人数列表
  maps_per_size  每种人数用几张地图
  seed           赛程随机种子（可选；不填则随机，写入赛程后冻结）
  replays        是否保存全部回放（默认 true，gzip 压缩）
  build_dir      编译结果放在哪里（可选；默认是赛事目录下的 build/）
  copies         每位选手（含基准）复制几份上场（可选，默认 1）。单局人数超过选手数时用，
                 复制品名为 名字#2、名字#3……，计分时合并，复制品之间不比较

rate 可以一次读多个赛事目录合并计分，比如 2–6 方和补跑的 7–15 方：
  python tournament.py rate 赛事目录A 赛事目录B
成绩写在第一个目录里。
"""
from __future__ import annotations

import argparse
import gzip
import importlib.util
import json
import os
import secrets
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import bt  # noqa: E402
import core  # noqa: E402
import schedule  # noqa: E402

_GAMES = {}


def load_game(path):
    if path not in _GAMES:
        spec = importlib.util.spec_from_file_location(f"game_{len(_GAMES)}", path)
        module = importlib.util.module_from_spec(spec)
        sys.path.insert(0, str(Path(path).parent))
        spec.loader.exec_module(module)
        _GAMES[path] = module.Game
    return _GAMES[path]


def plan(config_path, out):
    out = Path(out)
    if (out / "赛程.json").exists():
        raise SystemExit("赛程已存在，不覆盖。要重排请换一个赛事目录。")
    cfg = json.loads(Path(config_path).read_text(encoding="utf-8-sig"))  # 兼容记事本存的带 BOM 文件
    base = Path(config_path).resolve().parent
    players = {n: str((base / p).resolve()) for n, p in cfg["players"].items()}
    copies = cfg.get("copies", 1)   # 人数超过选手数时，每位选手复制几份上场
    players.update({f"{n}#{k}": p for n, p in list(players.items()) for k in range(2, copies + 1)})
    for n, p in players.items():
        if not Path(p).is_file():
            raise SystemExit(f"{n} 的程序不存在：{p}")
    seed = cfg.get("seed", secrets.randbits(48))
    games = schedule.make_schedule(sorted(players), cfg["sizes"], cfg["maps_per_size"], seed)
    doc = {"game": str((base / cfg["game"]).resolve()), "anchor": cfg["anchor"], "players": players,
           "sizes": cfg["sizes"], "maps_per_size": cfg["maps_per_size"], "seed": seed,
           "replays": cfg.get("replays", True), "build_dir": cfg.get("build_dir"), "audit": schedule.audit(games, sorted(players)), "games": games}
    out.mkdir(parents=True, exist_ok=True)
    (out / "赛程.json").write_text(json.dumps(doc, ensure_ascii=False, indent=1), encoding="utf-8")
    total = len(games)
    print(f"已冻结赛程：{total} 局")
    for k, a in doc["audit"].items():
        print(f"  {k} 人局 {a['games']} 局，每人出场 {a['appear_min']}–{a['appear_max']}，两两交手 {a['meet_min']}–{a['meet_max']}")


def play(game_path, commands, g, replay_dir):
    Game = load_game(game_path)
    t = time.time()
    result, replay = core.run_match(Game, [commands[n] for n in g["seats"]], g["seats"], g["map_seed"],
                                    record=replay_dir is not None)
    if replay is not None:
        path = Path(replay_dir) / f"{g['id']}.json.gz"
        path.parent.mkdir(parents=True, exist_ok=True)
        with gzip.open(path, "wt", encoding="utf-8") as f:
            json.dump(replay, f, ensure_ascii=False, separators=(",", ":"))
    return {"id": g["id"], "size": g["size"], "map_seed": g["map_seed"], "seats": g["seats"],
            "score": result["score"], "turns": result.get("turns"), "seconds": round(time.time() - t, 2),
            "faults": {b["name"]: b["dead"] or len(b["faults"]) for b in result["bots"] if b["faults"] or b["dead"]},
            "max_ms": {b["name"]: b["max_ms"] for b in result["bots"]},
            "stats": result.get("stats")}


def run(out, workers):
    out = Path(out)
    doc = json.loads((out / "赛程.json").read_text(encoding="utf-8"))
    done_file = out / "结果.jsonl"
    done = set()
    if done_file.exists():
        for line in done_file.read_text(encoding="utf-8").splitlines():
            if line.strip():
                done.add(json.loads(line)["id"])
    todo = [g for g in doc["games"] if g["id"] not in done]
    print(f"共 {len(doc['games'])} 局，已完成 {len(done)}，本次运行 {len(todo)}，并发 {workers}", flush=True)
    replay_dir = str(out / "回放") if doc["replays"] else None
    # 开赛前在主进程里统一编译一次
    build = Path(doc.get("build_dir") or out / "build")
    commands = {}
    for name, path in doc["players"].items():
        try:
            commands[name] = core.command_for(path, build)
        except RuntimeError as exc:
            raise SystemExit(f"{name}：{exc}")
    print(f"已编译 {len(commands)} 个程序，放在 {build}", flush=True)
    start = time.time()
    with ProcessPoolExecutor(workers) as pool, done_file.open("a", encoding="utf-8") as f:
        futures = [pool.submit(play, doc["game"], commands, g, replay_dir) for g in todo]
        for i, fut in enumerate(as_completed(futures), 1):
            row = fut.result()
            f.write(json.dumps(row, ensure_ascii=False) + "\n")
            f.flush()
            if i % 25 == 0 or i == len(todo):
                rate_ = i / (time.time() - start)
                print(f"  {len(done) + i}/{len(doc['games'])}  约剩 {(len(todo) - i) / rate_ / 60:.1f} 分钟", flush=True)


def rate(outs):
    outs = [Path(o) for o in outs]
    out = outs[0]
    rows, names, anchor = [], set(), None
    for o in outs:
        doc = json.loads((o / "赛程.json").read_text(encoding="utf-8"))
        part = [json.loads(x) for x in (o / "结果.jsonl").read_text(encoding="utf-8").splitlines() if x.strip()]
        if len(part) < len(doc["games"]):
            print(f"注意：{o.name} 只完成了 {len(part)}/{len(doc['games'])} 局，以下为中途成绩。")
        if anchor not in (None, doc["anchor"]):
            raise SystemExit("合并计分的几个赛事目录，基准必须相同")
        anchor = doc["anchor"]
        rows += part
        names |= {bt.base(n) for n in doc["players"]}
    names = sorted(names)
    doc = {"anchor": anchor}
    ci, per_size = bt.rate(rows, names, doc["anchor"])
    order = sorted(names, key=lambda n: -ci[n][0])
    sizes = sorted(per_size)
    lines = ["| 名次 | 选手 | 实力值 | 95% 区间 | " + " | ".join(f"{k}人" for k in sizes) + " | 故障局 |",
             "|---:|---|---:|---|" + "---:|" * len(sizes) + "---:|"]
    faults = {n: sum(1 for r in rows if any(bt.base(f) == n for f in r["faults"])) for n in names}
    rank = 0
    for n in order:
        if n != doc["anchor"]:
            rank += 1
        v, lo, hi = ci[n]
        lines.append(f"| {'—' if n == doc['anchor'] else rank} | {n} | {v:.0f} | {lo:.0f} ~ {hi:.0f} | " +
                     " | ".join(f"{per_size[k][n]:.0f}" for k in sizes) + f" | {faults[n]} |")
    text = (f"# 成绩\n\n{len(rows)} 局。实力值以{doc['anchor']}为 0，强 10 倍记 +400；各人数等权；"
            f"区间按地图重抽样 200 次。\n\n" + "\n".join(lines) + "\n")
    (out / "成绩.md").write_text(text, encoding="utf-8")
    (out / "成绩.json").write_text(json.dumps({"ratings": ci, "per_size": per_size}, ensure_ascii=False, indent=1),
                                   encoding="utf-8")
    print(text)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("plan"); p.add_argument("config"); p.add_argument("out")
    p = sub.add_parser("run"); p.add_argument("out"); p.add_argument("--workers", type=int, default=4)
    p = sub.add_parser("rate"); p.add_argument("out", nargs="+")
    a = ap.parse_args()
    if a.cmd == "plan":
        plan(a.config, a.out)
    elif a.cmd == "run":
        run(a.out, a.workers)
    else:
        rate(a.out)


if __name__ == "__main__":
    main()
