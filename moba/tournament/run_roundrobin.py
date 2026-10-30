"""示例赛：5 个示例 bot 两两对阵的循环赛，覆盖三种难度，产出成绩表和精选回放。

  python run_roundrobin.py [--games-per-pair N] [--workers W] [--replays]

每个对阵在每档难度默认打 2 局（同种子换边），用 --games-per-pair 按偶数扩展轮次。
成绩按积分排（平局算半分），同分依次比较小循环积分和对手分，仍相同则并列。
--replays 会把普通难度的每一局都在正式对局里顺便存成回放到 ../replays/，
文件名带种子，换边的两局不会互相覆盖。
"""
import argparse
import hashlib
import json
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "workspace"))
import core  # noqa: E402
from engine import VERSION, Game, DIFF_LABELS  # noqa: E402

BOTS = {
    "速推": "../bots/rush.cpp",
    "发育": "../bots/farm.cpp",
    "游走": "../bots/gank.cpp",
    "抱团": "../bots/siege.cpp",
    "均衡": "../bots/balance.cpp",
}


def _one(job):
    """跑一局。给了 stem 就顺便把这一局的回放写到磁盘（回放和成绩来自同一次对局）。"""
    programs, names, seed, difficulty, stem, rep_dir, viewer = job
    result, replay = core.run_match(Game, programs, names, seed, record=stem is not None,
                                    difficulty=difficulty)
    if stem is not None:
        core.save_replay(replay, Path(rep_dir) / stem, Path(viewer))
    return names, result


def esc_h(t):
    return str(t).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games-per-pair", type=int, default=2,
                    help="每个对阵每档难度打的局数（≥2 的偶数，成对换边且使用同一地图种子）")
    ap.add_argument("--workers", type=int, default=0)
    ap.add_argument("--replays", action="store_true", help="把普通难度的每场都存成回放（文件名带种子）")
    ap.add_argument("--seed", type=int, default=101)
    a = ap.parse_args()
    a.games_per_pair = max(2, a.games_per_pair)
    if a.games_per_pair % 2:
        ap.error("--games-per-pair 必须是偶数，保证同种子换边")

    root = HERE.parent
    bots = {n: str((HERE / p).resolve()) for n, p in BOTS.items()}
    missing = [n for n, p in bots.items() if not Path(p).is_file()]
    if missing:
        raise SystemExit(f"示例 bot 缺失：{missing}（先让它们交卷）")
    digests = {n: hashlib.sha256(Path(p).read_bytes()).hexdigest() for n, p in bots.items()}
    programs = {}
    for n, p in bots.items():
        try:
            programs[n] = core.command_for(Path(p), root / "workspace" / "build")
        except RuntimeError as exc:
            print(f"跳过 {n}：编译失败\n{exc}")
    names = sorted(programs)
    if len(names) < 2:
        raise SystemExit("可参赛的 bot 不足 2 个")

    rep_dir = root / "replays"
    viewer = root / "workspace" / "viewer.html"
    if a.replays:
        rep_dir.mkdir(exist_ok=True)

    jobs, plan = [], []
    per = a.games_per_pair
    for diff in (0, 1, 2):
        for rnd in range(per // 2):
            # 同一轮次所有对阵共用种子；每对选手换边两局也共用这张地图。
            seed = a.seed + diff * 10_000 + rnd
            for i in range(len(names)):
                for j in range(i + 1, len(names)):
                    for side in (0, 1):
                        home = side == 0
                        seat = [names[i], names[j]] if home else [names[j], names[i]]
                        stem = f"示例-{seat[0]}-vs-{seat[1]}-seed{seed}" if a.replays and diff == 1 else None
                        jobs.append(([programs[seat[0]], programs[seat[1]]], seat, seed, diff, stem,
                                     str(rep_dir), str(viewer)))
                        plan.append({"a": seat[0], "b": seat[1], "difficulty": diff, "seed": seed,
                                     "round": rnd, "home_is_a": home, "replay": stem})
    workers = a.workers or max(1, min(8, (__import__("os").cpu_count() or 4) // 2))
    print(f"{VERSION} 示例赛：{len(names)} 个 bot 循环赛，{len(jobs)} 局，并行 {workers}")
    t0 = time.time()
    results = []
    with ProcessPoolExecutor(workers) as pool:
        for pl, (seat, res) in zip(plan, pool.map(_one, jobs)):
            results.append({**pl, "winner": res["winner"], "reason": res["reason"], "turns": res["turns"],
                            "score": res["score"], "kda": res["kda"], "stats": res["stats"]})
            w = "平" if res["winner"] < 0 else seat[res["winner"]]
            print(f"  {pl['a']} vs {pl['b']}  {DIFF_LABELS[pl['difficulty']]}  种子 {pl['seed']}  →  {w}"
                  f"（{res['turns']} 回合）")
    print(f"打完用时 {time.time() - t0:.0f}s")

    # 汇总
    wins = {n: 0.0 for n in names}
    breakdown = {n: {d: 0.0 for d in range(3)} for n in names}
    matrix = {n: {m: 0.0 for m in names} for n in names}
    totals = {n: {"kills": 0, "assists": 0, "deaths": 0} for n in names}
    for r in results:
        if r["winner"] < 0:
            for n in (r["a"], r["b"]):
                wins[n] += 0.5
                breakdown[n][r["difficulty"]] += 0.5
                matrix[n][r["b"] if n == r["a"] else r["a"]] += 0.5
        else:
            w = r["a"] if r["winner"] == 0 else r["b"]
            l = r["b"] if r["winner"] == 0 else r["a"]
            wins[w] += 1
            breakdown[w][r["difficulty"]] += 1
            matrix[w][l] += 1
        for idx, n in ((0, r["a"]), (1, r["b"])):
            s = r["stats"][idx]
            for key in totals[n]:
                totals[n][key] += s.get(key, 0)

    # 同分判定：先算同分组内的小循环积分，再算全部对手的最终积分之和。
    groups = {}
    for n in names:
        groups.setdefault(wins[n], []).append(n)
    h2h = {n: 0.0 for n in names}
    for group in groups.values():
        if len(group) < 2:
            continue
        for n in group:
            for r in results:
                if r["a"] not in group or r["b"] not in group or n not in (r["a"], r["b"]):
                    continue
                if r["winner"] < 0:
                    h2h[n] += 0.5
                elif (r["winner"] == 0 and n == r["a"]) or (r["winner"] == 1 and n == r["b"]):
                    h2h[n] += 1
    buchholz = {n: sum(wins[m] for m in names if m != n) for n in names}
    order = sorted(names, key=lambda n: (-wins[n], -h2h[n], -buchholz[n], n))
    ranks, last_key, rank = {}, None, 0
    for pos, n in enumerate(order, 1):
        key = (wins[n], h2h[n], buchholz[n])
        if key != last_key:
            rank, last_key = pos, key
        ranks[n] = rank

    out = root / "results"
    out.mkdir(exist_ok=True)
    games_path = out / "示例赛-对局.json"
    games_path.write_text(json.dumps(results, ensure_ascii=False, indent=1), encoding="utf-8")

    lines = [f"# 峡谷对决 · 示例赛成绩（{VERSION}）", "",
             f"{len(names)} 个示例 bot 循环赛，每个对阵在每档难度换边各打 {per} 局，共 {len(results)} 局。",
             "胜 1 分、平 0.5 分。这是**主办方代写的示例 bot 之间的表演赛**，用来演示赛制，不计入正式成绩。", "",
             "排名依次比较：总积分 → 同分者小循环积分 → 对手分（所有对手最终积分之和）→ 并列。",
             "同一轮次的所有对阵、以及同一对阵的换边两局使用相同地图种子。", "",
             "## 参赛文件", "",
             "| bot | SHA-256 前 16 位 |", "|---|---|"]
    lines += [f"| {n} | `{digests[n][:16]}` |" for n in names]
    lines += ["", "## 总榜", "",
             "| 名次 | bot | 胜-平-负 | 积分 | 小循环 | 对手分 | 简单 | 普通 | 困难 | K/A/D | KDA |",
             "|---:|---|---|---:|---:|---:|---:|---:|---:|---:|---:|"]
    games_per_diff = per * (len(names) - 1)
    for n in order:
        d = breakdown[n]
        win = sum(1 for r in results if r["winner"] >= 0 and (r["a"] if r["winner"] == 0 else r["b"]) == n)
        draw = sum(1 for r in results if r["winner"] < 0 and n in (r["a"], r["b"]))
        lose = sum(1 for r in results if r["winner"] >= 0 and (r["b"] if r["winner"] == 0 else r["a"]) == n)
        t = totals[n]
        kda = (t["kills"] + t["assists"]) / max(1, t["deaths"])
        lines.append(f"| {ranks[n]} | {n} | {win}-{draw}-{lose} | {wins[n]:g} | {h2h[n]:g} | "
                     f"{buchholz[n]:g} | {d[0]:g}/{games_per_diff} | {d[1]:g}/{games_per_diff} | "
                     f"{d[2]:g}/{games_per_diff} | {t['kills']}/{t['assists']}/{t['deaths']} | {kda:.2f} |")
    lines += ["", "## 两两交锋（行对列的胜场）", "", "| | " + " | ".join(names) + " |",
              "|---|" + "---:|" * len(names)]
    for n in names:
        rows = []
        for m in names:
            rows.append("—" if m == n else f"{matrix[n][m]:g}")
        lines.append(f"| {n} | " + " | ".join(rows) + " |")

    lines += ["", "## 逐局结果", "",
              "| 难度 | 轮次 | 种子 | 先手 | 后手 | 胜者 | 回合 | 先手KDA | 后手KDA | 终局原因 |",
              "|---|---:|---:|---|---|---|---:|---:|---:|---|"]
    for r in results:
        winner = "平局" if r["winner"] < 0 else (r["a"] if r["winner"] == 0 else r["b"])
        lines.append(f"| {DIFF_LABELS[r['difficulty']]} | {r['round']} | {r['seed']} | {r['a']} | {r['b']} "
                     f"| {winner} | {r['turns']} | {r['kda'][0]:.2f} | {r['kda'][1]:.2f} | {r['reason']} |")
    (out / "示例赛-成绩.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"成绩表：{out / '示例赛-成绩.md'}")

    if a.replays:
        # 回放已经在正式对局里一起落盘了，这里只按成绩生成索引页
        items = []
        for r in results:
            if not r.get("replay"):
                continue
            w = "平局" if r["winner"] < 0 else (r["a"] if r["winner"] == 0 else r["b"]) + " 胜"
            items.append((r["replay"], f"{r['a']} vs {r['b']}（{w}，{r['turns']} 回合，种子 {r['seed']}）"))
        links = "\n".join(f'<option value="{s}.html">{esc_h(t)}</option>' for s, t in items)
        (rep_dir / "index.html").write_text(
            """<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><title>峡谷对决 · 示例赛回放</title>
<style>body{font-family:"Segoe UI","Microsoft YaHei",sans-serif;background:#0d1219;color:#e6ecf5;display:grid;place-items:center;min-height:90vh;margin:0}
main{background:#161e2a;border:1px solid #2a3547;border-radius:12px;padding:28px 34px;max-width:560px}
h1{font-size:20px;margin:0 0 6px}p{color:#93a1b5;font-size:13px;margin:0 0 18px}
select,button{background:#243149;color:#e6ecf5;border:1px solid #3c4d68;border-radius:8px;padding:9px 12px;font:inherit}
select{width:100%}button{margin-top:12px;cursor:pointer;width:100%}</style></head><body><main>
<h1>峡谷对决 · 示例赛回放</h1>
<p>5 个示例 bot 循环赛（普通难度）的每一局。文件名带种子，换边的两局分开存放。回放里所有图形都是 SVG 现场画的。</p>
<select id="pick">__LINKS__</select>
<button onclick="location.href=document.getElementById('pick').value">打开这份回放</button>
</main></body></html>""".replace("__LINKS__", links),
            encoding="utf-8",
        )
        print(f"回放：{len(items)} 份普通难度回放（文件名带种子，互不覆盖）")
        print(f"索引页：{rep_dir / 'index.html'}")


if __name__ == "__main__":
    main()
