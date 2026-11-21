"""用你的程序挑战第 11 集回旋镖大作战的 13 位 AI：和每一位打一对一、和其中三位同桌打四人各自为战
（正式计时：每步 10 CPU 毫秒，第 0 步 1000 毫秒，和比赛时同一个裁判 arena.exe），再把你放到海选个人榜的同一把尺上。

  python challenge.py                      用 workspace/template.cpp（选手包里的起步模板，很弱）
  python challenge.py 我的bot.cpp          用你自己的程序（一对一 312 局 + 四人局 104 局，几分钟）
  python challenge.py 我的bot.cpp --full   局数是上面的 4 倍，结果更稳

程序的写法和比赛选手一样：一个 .cpp，#include "dart.hpp"，用 DART_MAIN 宏，见 workspace/README.md、workspace/RULES.md。
需要 Python 3.9+ 和 g++（Windows 推荐 w64devkit，见仓库根目录 README）。裁判用 Windows 的 Job Object 管进程，只支持 Windows。

计分（固定海选参照）：13 位选手的一对一、四人局实力值固定取 results/海选成绩.json，只拟合你一个人的两项实力值
（Bradley–Terry，刻度同 Elo：强 10 倍 = +400；每个对手加一局虚拟平局），个人实力值 = 两项平均，和海选个人榜同一口径。
一对一：名次高的赢、同名次算平；四人局：每局里你和三位对手两两比这局的得分（名次分 3/2/1/0 并列平分 + 每次击杀 1 分）。
地图是海选用的 6 张（2 张公开 + 4 张赛时保密），种子和海选不是同一批。
"""
import argparse
import hashlib
import json
import math
import os
import random
import re
import shutil
import sys
import tempfile
import time
import types
from pathlib import Path

HERE = Path(__file__).resolve().parent
WS = HERE / "workspace"

# 海选席位名 → 显示名、程序目录（和 tournament/score.py、tournament/rerun.py 相同）
OPPONENTS = {
    "astra": ("GPT-6.2 Astra", "gpt-6.2-astra"), "fable": ("Claude Fable 5.2", "claude-fable-5.2"),
    "opus": ("Claude Opus 5.5", "claude-opus-5.5"), "carbon": ("Gemini 4 Carbon", "gemini-4-carbon"),
    "sol": ("GPT-6.2 Sol", "gpt-6.2-sol"), "hydrogen": ("Gemini 4 Hydrogen", "gemini-4-hydrogen"),
    "grok": ("Grok 4.7", "grok-4.7"), "glm": ("GLM 5.3", "glm-5.3"),
    "deepseek": ("DeepSeek-V4.1-Flash", "deepseek-v4.1-flash"), "kimi": ("Kimi K3.1 Pro", "kimi-k3.1-pro"),
    "qwen": ("千问 Qwen4-Max", "qwen4-max"), "mimo": ("MiMo V2.6 Pro", "mimo-v2.6-pro"),
    "doubao": ("豆包 Seed-Evolving", "doubao-seed-evolving"),
}
MAPS = ["workspace/maps/twinlakes.txt", "workspace/maps/yinyang.txt"] + \
       ["tournament/hidden/%s.txt" % m for m in ("icefloes", "ruins", "skybridge", "stonecircle")]
MODES = ("duel", "ffa")
SEED = 61001                      # 海选用的是 11001～11019（一对一）和 2401 起（四人局），这里换一批
REFERENCE = {"path": "boomerang/results/海选成绩.json", "board": "海选个人榜", "anchor": "13 位选手平均 = 0"}
BUDGET = {"budget_kind": "cpu", "step_ms": 10, "first_ms": 1000, "max_timeouts": 20}
STATUSES = ("ok", "crash", "lost", "timeout_limit", "start_fail", "output_limit")


def schedule(full):
    """返回 [(局型, 地图相对路径, 种子, 座位上的名字列表)]。"me" 是你。确定性，不依赖机器。

    一对一：每个对手 × 6 张图 × k 个种子 × 两边各坐一次 0 号位（默认 k = 2，共 312 局）。
    四人局：26k 组，每组 3 位对手（每位对手出场次数相同），同一张图同一个种子，你轮流坐 4 个座位（默认 104 局）。
    """
    k = 4 if full else 1
    names = list(OPPONENTS)
    games = []
    for opp in names:
        for mp in MAPS:
            for s in range(2 * k):
                games.append(("duel", mp, SEED + s, ["me", opp]))
                games.append(("duel", mp, SEED + s, [opp, "me"]))
    rng = random.Random(SEED)
    count = dict.fromkeys(names, 0)
    for g in range(26 * k):
        order = names[:]
        rng.shuffle(order)
        group = sorted(order, key=lambda n: count[n])[:3]          # 出场最少的三位，平手按这次的洗牌顺序
        for n in group:
            count[n] += 1
        mp, seed = MAPS[g % len(MAPS)], SEED + 1000 + g
        for rot in range(4):
            seats = group[:]
            seats.insert(rot, "me")
            games.append(("ffa", mp, seed, seats))
    return games


def fit(rows):
    """对手的实力值固定，只解你一个人的实力值（Bradley–Terry，强 10 倍 = +400，每个对手加一局虚拟平局防止发散）。"""
    def loglik(r):
        total = 0.0
        for opp, w, d, l in rows:
            p = 1 / (1 + 10 ** ((opp - r) / 400))
            n, s = w + d + l + 1, w + 0.5 * d + 0.5
            total += s * math.log(p) + (n - s) * math.log(1 - p)
        return total
    lo, hi = -2000.0, 2500.0
    for _ in range(200):                       # 似然是单峰的，三分法就够
        a, b = lo + (hi - lo) / 3, hi - (hi - lo) / 3
        if loglik(a) < loglik(b):
            lo = a
        else:
            hi = b
    return (lo + hi) / 2


def reference(root):
    """固定参照：{席位名: {"rating", "duel", "ffa"}}，取自比赛时的海选成绩。"""
    players = json.loads((Path(root) / REFERENCE["path"]).read_text(encoding="utf-8"))["players"]
    return {n: {"rating": players[n]["rating"], "duel": players[n]["duel_rating"], "ffa": players[n]["ffa_rating"]}
            for n in OPPONENTS}


def expected_counts(games):
    """每个对手在每种局型里应有的两两比较次数。"""
    exp = {m: dict.fromkeys(OPPONENTS, 0) for m in MODES}
    for mode, _, _, seats in games:
        for n in seats:
            if n != "me":
                exp[mode][n] += 1
    return exp


def rate(tally, ref):
    """tally：{局型: {对手: [胜, 平, 负]}} → (两项实力值, 个人实力值, 参照名次)。"""
    ratings = {m: fit([(ref[n][m], *tally[m][n]) for n in OPPONENTS if sum(tally[m][n])]) for m in MODES}
    total = (ratings["duel"] + ratings["ffa"]) / 2
    rank = 1 + sum(ref[n]["rating"] > total for n in OPPONENTS)
    return ratings, total, rank


def ffa_points(players):
    """四人局得分：名次分（3/2/1/0，并列平分）+ 每次击杀 1 分（workspace/match.py 的 pts_for_ranks、KILL_POINTS）。"""
    if str(WS) not in sys.path:
        sys.path.insert(0, str(WS))
    from match import KILL_POINTS, pts_for_ranks
    pts = pts_for_ranks(players)
    return {p["seat"]: pts[p["seat"]] + KILL_POINTS * p["kills"] for p in players}


def validate_records(games, recs):
    """不把截断、缺座位或无效的结果当作完整评测。"""
    if not isinstance(recs, list) or len(recs) != len(games):
        raise ValueError("对局没有全部完成")
    for i, ((mode, _, seed, seats), r) in enumerate(zip(games, recs)):
        n = len(seats)
        if (not isinstance(r, dict) or type(r.get("id")) is not int or r["id"] != i or
                type(r.get("seed")) is not int or r["seed"] != seed or r.get("mode") != MODES.index(mode) or
                type(r.get("steps")) is not int or not 1 <= r["steps"] <= 2000):
            raise ValueError("第 %d 局的局号、种子、局型或步数与赛程不符" % i)
        pl = r.get("players")
        if not isinstance(pl, list) or len(pl) != n or sorted(p.get("seat") for p in pl if isinstance(p, dict)) != list(range(n)):
            raise ValueError("第 %d 局的座位不齐" % i)
        for p in pl:
            if (type(p.get("rank")) is not int or not 1 <= p["rank"] <= n or type(p.get("kills")) is not int or
                    not 0 <= p["kills"] < n or p.get("status") not in STATUSES or
                    any(type(p.get(k)) is not int or p[k] < 0 for k in ("timeouts", "invalid"))):
                raise ValueError("第 %d 局的名次、击杀或故障统计无效" % i)


def tally_games(games, recs):
    tally = {m: {n: [0, 0, 0] for n in OPPONENTS} for m in MODES}
    stats = {"timeouts": 0, "invalid": 0, "dropped": 0, "first_ms": 0.0, "max_ms": 0.0}
    for (mode, _, _, seats), r in zip(games, recs):
        pl = sorted(r["players"], key=lambda p: p["seat"])
        me = seats.index("me")
        value = ({p["seat"]: -p["rank"] for p in pl} if mode == "duel" else ffa_points(pl))
        for j, opp in enumerate(seats):
            if j != me:
                t = tally[mode][opp]
                t[0 if value[me] > value[j] else 1 if value[me] == value[j] else 2] += 1
        p = pl[me]
        stats["timeouts"] += p["timeouts"]
        stats["invalid"] += p["invalid"]
        stats["dropped"] += p["status"] != "ok"
        stats["first_ms"] = max(stats["first_ms"], float(p.get("first_ms") or 0))
        stats["max_ms"] = max(stats["max_ms"], float(p.get("max_ms") or 0))
    return tally, stats


def make_result(games, recs, source_sha, full, root=HERE.parent):
    validate_records(games, recs)
    tally, stats = tally_games(games, recs)
    ref = reference(root)
    ratings, total, rank = rate(tally, ref)
    opponents = [{"name": n, "display": OPPONENTS[n][0], "rating": ref[n]["rating"],
                  "ratings": {m: ref[n][m] for m in MODES}, "tally": {m: tally[m][n] for m in MODES}} for n in OPPONENTS]
    return {"schema": 1, "complete": True, "rating_method": "fixed-haixuan", "mode": "formal", "budget": dict(BUDGET),
            "reference": dict(REFERENCE), "source_sha256": source_sha, "seed": SEED, "full": full, "games": len(games),
            "games_by_mode": {m: sum(g[0] == m for g in games) for m in MODES},
            "ratings": ratings, "rating": total, "rank": rank, "opponents": opponents, "stats": stats}


def number(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


def validate_result(doc, root):
    """社区评测的发布端用：只读、不运行投稿。按可信赛程和固定参照重算一遍，不符就 ValueError。
    返回 (field, extra)：field 是 13 位选手 + "你的 bot" 的实力值，extra 是要写进社区结果的字段。"""
    ref = reference(root)
    if not isinstance(doc, dict):
        raise ValueError("回旋镖成绩不是对象")
    if (type(doc.get("schema")) is not int or doc["schema"] != 1 or doc.get("complete") is not True or
            doc.get("mode") != "formal" or doc.get("rating_method") != "fixed-haixuan" or doc.get("budget") != BUDGET or
            doc.get("reference") != REFERENCE or type(doc.get("full")) is not bool or
            type(doc.get("seed")) is not int or doc["seed"] != SEED or
            not isinstance(doc.get("source_sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", doc["source_sha256"])):
        raise ValueError("回旋镖成绩缺少正式 CPU 预算、固定海选参照口径或源码哈希")
    games = schedule(doc["full"])
    exp = expected_counts(games)
    counts = {m: sum(g[0] == m for g in games) for m in MODES}
    if (type(doc.get("games")) is not int or doc["games"] != len(games) or doc.get("games_by_mode") != counts or
            any(type(v) is not int for v in doc["games_by_mode"].values())):
        raise ValueError("回旋镖成绩局数不完整")
    opponents = doc.get("opponents")
    if (not isinstance(opponents, list) or len(opponents) != len(OPPONENTS) or
            any(not isinstance(r, dict) or not isinstance(r.get("name"), str) for r in opponents) or
            {r["name"] for r in opponents} != set(OPPONENTS)):
        raise ValueError("回旋镖成绩对手不完整或重复")
    tally = {m: {} for m in MODES}
    for row in opponents:
        n = row["name"]
        if (row.get("rating") != ref[n]["rating"] or row.get("ratings") != {m: ref[n][m] for m in MODES} or
                row.get("display") != OPPONENTS[n][0] or not isinstance(row.get("tally"), dict) or set(row["tally"]) != set(MODES)):
            raise ValueError("回旋镖固定参照分数或对手身份不一致")
        for m in MODES:
            t = row["tally"][m]
            if not isinstance(t, list) or len(t) != 3 or any(type(v) is not int or v < 0 for v in t) or sum(t) != exp[m][n]:
                raise ValueError("回旋镖逐对手胜平负和固定赛程不符")
            tally[m][n] = t
    ratings, total, rank = rate(tally, ref)
    claimed = doc.get("ratings")
    if (not isinstance(claimed, dict) or set(claimed) != set(MODES) or
            any(not number(claimed[m]) or not math.isclose(claimed[m], ratings[m], abs_tol=1e-7) for m in MODES) or
            not number(doc.get("rating")) or not math.isclose(doc["rating"], total, abs_tol=1e-7) or
            type(doc.get("rank")) is not int or doc["rank"] != rank):
        raise ValueError("回旋镖分项、个人实力值或参照名次与可信重算不符")
    field = [{"name": OPPONENTS[n][0], "is_entry": False, "score": round(ref[n]["rating"], 1)} for n in OPPONENTS]
    field.append({"name": "你的 bot", "is_entry": True, "score": round(total, 1)})
    keys = ("schema", "complete", "rating_method", "mode", "budget", "reference", "source_sha256", "seed", "full",
            "games", "games_by_mode", "ratings", "rating", "rank", "opponents")
    extra = {k: doc[k] for k in keys}
    extra["games_note"] = "%d 局（一对一 %d、四人局 %d）" % (len(games), counts["duel"], counts["ffa"])
    return field, extra


def render_report(doc):
    r, s = doc["ratings"], doc["stats"]
    lines = ["# 回旋镖大作战挑战成绩", "",
             "固定海选参照（13 位选手平均 = 0，强 10 倍 = +400），只拟合你的实力值：",
             f"一对一 **{r['duel']:+.1f}**、四人局 **{r['ffa']:+.1f}**；个人实力值 **{doc['rating']:+.1f}**（两项平均，和海选个人榜同一口径）。", "",
             f"放进海选个人榜的 13 位里相当于第 **{doc['rank']} / 14** 名。", "",
             f"正式计时（每步 {doc['budget']['step_ms']} CPU 毫秒，第 0 步 {doc['budget']['first_ms']} 毫秒，"
             f"累计超时 {doc['budget']['max_timeouts']} 次结束程序），共 {doc['games']} 局"
             f"（一对一 {doc['games_by_mode']['duel']}、四人局 {doc['games_by_mode']['ffa']}），种子从 {doc['seed']} 起，海选的 6 张图。",
             f"你的程序：第 0 步 CPU 最多 {s['first_ms']:.0f} 毫秒，之后单步最多 {s['max_ms']:.2f} 毫秒；"
             f"超时 {s['timeouts']} 步，非法输出 {s['invalid']} 次，{s['dropped']} 局里崩溃、失去响应或被结束。", "",
             "| 对手 | 海选个人实力值（固定） | 一对一 胜-平-负 | 四人局两两比 胜-平-负 |", "|---|---:|---|---|"]
    for row in doc["opponents"]:
        lines.append(f"| {row['display']} | {row['rating']:+.0f} | " + " | ".join("-".join(map(str, row["tally"][m])) for m in MODES) + " |")
    lines += ["", "四人局的“胜-平-负”是你和这位对手同桌时谁这局得分高。对手分数是比赛时的海选成绩，不是这次重新测出的；"
              "局数少、机器不同、种子不同，这个估计不能当作正式名次；更稳可加 --full。正式成绩见 results/海选榜.md。", ""]
    return "\n".join(lines)


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bot", nargs="?", help="你的 .cpp；默认用 workspace/template.cpp")
    ap.add_argument("--full", action="store_true", help="局数是默认的 4 倍（1664 局）")
    ap.add_argument("--out", help="结果目录；默认新建 challenge-runs/ 下的目录")
    ap.add_argument("--workers", type=int, default=0, help="并行局数；默认 CPU 逻辑核数的一半，最多 8")
    args = ap.parse_args()
    if os.name != "nt":
        raise SystemExit("回旋镖的裁判 arena.exe 用 Windows 的 Job Object 管进程，只支持 Windows；社区评测用 GitHub 的 Windows 虚拟机。")
    if not os.environ.get("CXX") and not shutil.which("g++"):
        w64 = HERE.parent / "tools" / "w64devkit" / "bin" / "g++.exe"
        if w64.is_file():
            os.environ["CXX"] = str(w64)
    mine = Path(args.bot).resolve() if args.bot else WS / "template.cpp"
    if not mine.is_file():
        raise SystemExit(f"找不到你的程序：{mine}")
    if args.out:
        run_dir = Path(args.out).resolve()
        run_dir.mkdir(parents=True, exist_ok=True)
        if any(os.path.lexists(run_dir / name) for name in ("result.json", "report.md")):
            raise SystemExit(f"结果目录已有成绩，请换一个新目录：{run_dir}")
    else:
        runs = HERE / "challenge-runs"
        runs.mkdir(exist_ok=True)
        run_dir = Path(tempfile.mkdtemp(prefix=time.strftime("%Y%m%d-%H%M%S-"), dir=runs))
    source_sha = hashlib.sha256(mine.read_bytes()).hexdigest()
    sys.path.insert(0, str(WS))
    sys.dont_write_bytecode = True
    import build                                  # 可信选手包：正式编译命令、源码检查
    import match                                  # 可信选手包：调用裁判 arena.exe
    games = schedule(args.full)
    workers = args.workers or max(1, min(8, (os.cpu_count() or 2) // 2))
    print(f"你的程序：{mine}\n对手：海选的 13 位选手\n共 {len(games)} 局（一对一 {sum(g[0] == 'duel' for g in games)}、"
          f"四人局 {sum(g[0] == 'ffa' for g in games)}），正式计时，并行 {workers}\n"
          "（第一次运行要编译裁判和 14 个程序，约一分钟）\n", flush=True)
    try:
        progs = [match.resolve_prog(str(mine))] + [match.resolve_prog(str(HERE / "bots" / d / "player.cpp"))
                                                   for _, d in OPPONENTS.values()]
    except build.BuildError as e:
        raise SystemExit(f"编译失败：{e}")
    entries, seats = match.entries_for(progs)
    if len(entries) != len(progs):
        raise SystemExit("你的程序和某位官方选手的程序完全相同，挑战没有意义；改一改再来。")
    index = {"me": 0, **{n: i + 1 for i, n in enumerate(OPPONENTS)}}
    batch = [(i, seed, [index[n] for n in names], MODES.index(mode), str(HERE / mp))
             for i, (mode, mp, seed, names) in enumerate(games)]
    extra = match.common_args(types.SimpleNamespace(step_ms=BUDGET["step_ms"], sandbox="none", cfg=None))
    start = time.time()
    recs = {}
    try:
        for s in range(0, len(batch), 400):
            for r in match.run_batch(entries, batch[s:s + 400], workers, extra):
                recs[r["id"]] = r
            print(f"  已打 {len(recs)} / {len(games)} 局，{time.time() - start:.0f} 秒", flush=True)
    except match.ArenaError as e:
        raise SystemExit(f"出错了：{e}")
    print(f"打完了，用时 {time.time() - start:.0f} 秒。\n")
    if hashlib.sha256(mine.read_bytes()).hexdigest() != source_sha:
        raise SystemExit("评测途中源码发生了变化，不生成成绩。")
    try:
        doc = make_result(games, [recs.get(i) for i in range(len(games))], source_sha, args.full)
    except (ValueError, TypeError, KeyError, IndexError, OverflowError) as e:
        raise SystemExit(f"对局结果不完整或无效，不生成成绩：{e}")
    report = render_report(doc)
    (run_dir / "report.md").write_text(report, encoding="utf-8")
    (run_dir / "result.json").write_text(json.dumps(doc, ensure_ascii=False, indent=1, allow_nan=False) + "\n", encoding="utf-8")
    print(report)
    print(f"结果文件：{run_dir / 'result.json'}\n成绩表：{run_dir / 'report.md'}")


if __name__ == "__main__":
    main()
