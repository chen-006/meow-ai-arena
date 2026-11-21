"""用你的球员程序挑战第 8 集 2v2 足球的全部 AI：你的 bot.cpp 同时踢本队两个座位，
和个人赛的 12 支个人队（各选手下半场的 player.cpp + partner.cpp）、3 支参考队（基准、中档陪练、陪练）
逐一对踢（正式计时：每步 10 CPU 毫秒，和比赛时同一个 arena 裁判、同一条编译命令），
再按个人榜的 BT 实力值把你放到同一把尺上。

  python challenge.py                      用 workspace/template.cpp（选手拿到的起步模板，很弱）
  python challenge.py 我的bot.cpp          用你自己的程序（每个对手 SEEDS 个种子 × 换边）
  python challenge.py 我的bot.cpp --full   局数是上面的 4 倍，结果更稳

你的程序和比赛选手的 player.cpp 一样，规则和接口见 workspace/README.md、workspace/RULES.md。
需要 Python 3.10+ 和 g++（Windows 可用 w64devkit，见仓库根目录 README）。裁判只支持 Windows。
"""
import argparse
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile
import time

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
HERE = Path(__file__).resolve().parent
WS = HERE / "workspace"
REFERENCE = "soccer/results/individual/results.json.gz"     # 冻结的个人赛逐局数据，含 BT 点估计（键 bt）
# 个人队：短名 → (半场席位, 是否交了 partner.cpp)。没交就用 player.cpp 踢两个位置（见 bots/README.md）。
TEAMS = {
    "Grok": ("team1_a", True), "Hunyuan": ("team1_b", True), "Kimi": ("team2_a", True), "Step": ("team2_b", True),
    "Gemini": ("team3_a", True), "Doubao": ("team3_b", False), "Astra": ("team4_a", True), "Terra": ("team4_b", True),
    "Opus": ("team5_a", False), "GLM": ("team5_b", True), "DeepSeek": ("team6_a", True), "Haiku": ("team6_b", True),
}
REFS = {"ref-baseline": "baseline", "ref-midfield": "midfield", "ref-sparring": "sparring"}
OPPONENTS = list(TEAMS) + list(REFS)
SEED = 8001                       # 种子从这里起，和正式赛（9 位数的随机种子）不是同一批
SEEDS = 4                         # 默认每个对手 4 个种子 × 换边 = 8 局；--full 为 16 个种子，和个人赛每对 32 局一样
STEPS = 1200                      # 一局的步数（soccer.hpp 的 Config::steps）
STATUS = ("ok", "start_fail", "crash", "lost", "timeout_limit", "output_limit")


def require_windows():
    if os.name != "nt":
        raise SystemExit("足球正式裁判 arena.cpp 只支持 Windows；社区评测使用标准 Windows runner，不使用快速模式或放宽预算。")


def load_tools():
    sys.path.insert(0, str(WS))
    sys.dont_write_bytecode = True
    import build                                # 可信选手包：正式编译命令
    import match                                # 可信选手包：调用原生裁判
    return build, match


def ensure_compiler(build):
    cxx = build.find_compiler()
    if not cxx:
        raise SystemExit("没找到 C++ 编译器（g++）。Windows 推荐 w64devkit，放法见仓库根目录 README。")
    os.environ.setdefault("CXX", cxx)


def budget_info(ws=None):
    """从可信裁判源码读正式预算（默认值就是赛时设置），不改动裁判。"""
    text = Path(ws or WS, "arena.cpp").read_text(encoding="utf-8")
    m = re.search(r"struct\s+Limits\s*\{\s*double\s+step_ms\s*=\s*(\d+)\s*,\s*first_ms\s*=\s*(\d+)\s*,"
                  r"[^;]*;\s*int\s+max_timeouts\s*=\s*(\d+)\s*;", text)
    if not m:
        raise SystemExit("读不懂官方足球 CPU 预算，不生成成绩。")
    return {"budget_kind": "cpu", "step_ms": int(m.group(1)), "first_ms": int(m.group(2)), "max_timeouts": int(m.group(3))}


def official_budget(budget):
    return budget == {"budget_kind": "cpu", "step_ms": 10, "first_ms": 1000, "max_timeouts": 30}


def load_reference(root):
    """读冻结的个人赛数据，核对个人队组成，返回 (每个对手的固定 BT 实力值, 显示名, 程序清单)。"""
    with gzip.open(Path(root) / REFERENCE, "rt", encoding="utf-8") as f:
        data = json.load(f)
    if data.get("teams") != list(TEAMS) or [r.get("id") for r in data.get("references", [])] != list(REFS):
        raise ValueError("个人赛的队伍名单和挑战脚本对不上")
    ratings = {}
    for name in OPPONENTS:
        value = (data.get("bt") or {}).get(name, {}).get("rating")
        if not isinstance(value, (int, float)) or isinstance(value, bool) or not math.isfinite(value):
            raise ValueError("个人榜缺少 %s 的 BT 实力值" % name)
        ratings[name] = float(value)
    if ratings["ref-baseline"] != 0:
        raise ValueError("个人榜 BT 的基准不是 ref-baseline = 0")
    display = {r["team"]: r["model"] for r in data["standings"]}
    display.update({r["id"]: r["display"] for r in data["references"]})
    programs = {}
    for name, (seat, partner) in TEAMS.items():
        for slot, src in (("a", f"bots/half2/{seat}.cpp"), ("b", f"bots/half2/{seat}{'.partner' if partner else ''}.cpp")):
            row = data["programs"].get(f"{name}/{slot}") or {}
            if row.get("src") != src:
                raise ValueError("个人队 %s/%s 的上场程序和赛时记录不符" % (name, slot))
            programs[f"{name}/{slot}"] = (src, row.get("src_sha256"))
    return ratings, display, programs


def fit(rows):
    """对手的实力值固定，只解你一个人的实力值（Bradley–Terry，胜 1 平 0.5，强 10 倍 = +400，
    每个对手加一局虚拟平局防止发散）。"""
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


def schedule(full):
    """返回 [(对手, 种子, 你在哪边 0=A 队/1=B 队, 4 个座位的标签)]。"me" 是你（两个座位都是你）。
    和个人赛一样：每个种子换边各一局、都由 A 队开球；对手队内 0 号按种子轮换（偶数 a 在前，奇数 b 在前）。"""
    games = []
    for k in range(SEEDS * (4 if full else 1)):
        for opp in OPPONENTS:
            pair = [f"{opp}/a", f"{opp}/b"] if k % 2 == 0 else [f"{opp}/b", f"{opp}/a"]
            for side in (0, 1):
                games.append((opp, SEED + k, side, ["me", "me"] + pair if side == 0 else pair + ["me", "me"]))
    return games


def entry_index():
    labels = {"me": 0}
    for i, opp in enumerate(OPPONENTS):
        labels[f"{opp}/a"] = 1 + 2 * i
        labels[f"{opp}/b"] = 2 + 2 * i
    return labels


def entry_names():
    return ["me"] + [f"{opp}_{slot}" for opp in OPPONENTS for slot in "ab"]


def number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def validate_records(games, recs):
    """不把截断、缺座位或主办方启动故障的结果当作完整评测。"""
    if not isinstance(recs, list) or len(recs) != len(games):
        raise ValueError("对局没有全部完成")
    index, names = entry_index(), entry_names()
    for i, ((opp, seed, side, seats), rec) in enumerate(zip(games, recs)):
        if not isinstance(rec, dict) or type(rec.get("id")) is not int or rec["id"] != i:
            raise ValueError("对局编号缺失或不一致")
        if type(rec.get("seed")) is not int or rec["seed"] != seed or type(rec.get("first_kick")) is not int or rec["first_kick"] != 0:
            raise ValueError("对局种子或开球队与赛程不一致")
        score = rec.get("score")
        if not isinstance(score, list) or len(score) != 2 or any(type(v) is not int or v < 0 for v in score):
            raise ValueError("对局比分无效")
        if type(rec.get("steps")) is not int or rec["steps"] != STEPS:
            raise ValueError("对局没有踢满 %d 步" % STEPS)
        players = rec.get("players")
        if not isinstance(players, list) or len(players) != 4 or any(not isinstance(p, dict) for p in players):
            raise ValueError("对局缺少座位")
        for label, p in zip(seats, players):
            if p.get("name") != names[index[label]]:
                raise ValueError("对局座位与赛程不一致")
            if p.get("status") not in STATUS or p["status"] == "start_fail":
                raise ValueError("程序启动失败（主办方故障），不当作完整评测")
            if (any(type(p.get(k)) is not int or p[k] < 0 for k in ("timeouts", "invalid")) or
                    type(p.get("dead_step")) is not int or p["dead_step"] < -1 or
                    any(not number(p.get(k)) or p[k] < 0 for k in ("first_ms", "max_ms"))):
                raise ValueError("对局含无效的故障或计时统计")


def make_result(games, recs, ratings, display, source_sha, full, budget):
    validate_records(games, recs)
    tally = {n: [0, 0, 0, 0, 0] for n in OPPONENTS}             # 胜、平、负、进球、失球
    stats = {"timeouts": 0, "invalid": 0, "crash": 0, "first_ms": 0.0, "max_ms": 0.0,
             "opponent_timeouts": 0, "opponent_crash": 0}
    for (opp, seed, side, seats), r in zip(games, recs):
        mine, theirs = r["score"][side], r["score"][1 - side]
        t = tally[opp]
        t[0 if mine > theirs else 1 if mine == theirs else 2] += 1
        t[3] += mine
        t[4] += theirs
        me = [p for label, p in zip(seats, r["players"]) if label == "me"]
        other = [p for label, p in zip(seats, r["players"]) if label != "me"]
        stats["timeouts"] += sum(p["timeouts"] for p in me)
        stats["invalid"] += sum(p["invalid"] for p in me)
        stats["crash"] += any(p["status"] != "ok" for p in me)
        stats["first_ms"] = max([stats["first_ms"]] + [p["first_ms"] for p in me])
        stats["max_ms"] = max([stats["max_ms"]] + [p["max_ms"] for p in me])
        stats["opponent_timeouts"] += sum(p["timeouts"] for p in other)
        stats["opponent_crash"] += any(p["status"] != "ok" for p in other)
    rating = fit([(ratings[n], *tally[n][:3]) for n in OPPONENTS])
    rank = 1 + sum(ratings[n] > rating for n in TEAMS)
    opponents = [{"name": n, "display": display[n], "ranked": n in TEAMS, "rating": ratings[n],
                  "wins": tally[n][0], "draws": tally[n][1], "losses": tally[n][2],
                  "goals_for": tally[n][3], "goals_against": tally[n][4]} for n in OPPONENTS]
    return {"schema": 1, "complete": True, "mode": "formal", "rating_method": "fixed-individual", "sandbox": "none",
            "budget": budget, "reference": {"path": REFERENCE, "board": "个人榜", "field": "bt", "anchor": "ref-baseline"},
            "source_sha256": source_sha, "seed": SEED, "full": full, "games": len(games),
            "seeds_per_opponent": SEEDS * (4 if full else 1),
            "opponents": opponents, "rating": rating, "rank": rank, "stats": stats}


def render_report(doc):
    s = doc["stats"]
    lines = ["# 2v2 足球挑战成绩", "",
             "固定个人榜参照（基准 = 0，强 10 倍 = +400），只拟合你的实力值：**%+.1f**。" % doc["rating"], "",
             "放进个人榜的 12 支个人队里相当于第 **%d / 13** 名。3 支参考队参加对战和拟合，只作参考线、不占名次。" % doc["rank"], "",
             "正式模式，你的程序踢本队两个座位；共 %d 局（15 个对手 × %d 个种子 × 换边），种子从 %d 起。"
             % (doc["games"], doc["seeds_per_opponent"], doc["seed"]),
             "官方 CPU 预算：第 0 步 %d 毫秒，之后每步 %d 毫秒，累计超时 %d 次结束程序。"
             % (doc["budget"]["first_ms"], doc["budget"]["step_ms"], doc["budget"]["max_timeouts"]),
             "你的 bot：第 0 步 CPU 最多 %.1f 毫秒，之后每步最多 %.3f 毫秒；超时 %d 次，非法输出 %d 次，崩溃或被结束 %d 局。"
             % (s["first_ms"], s["max_ms"], s["timeouts"], s["invalid"], s["crash"]), "",
             "| 对手 | 个人榜 BT（固定） | 胜-平-负 | 进:失 |", "|---|---:|---|---|"]
    for row in doc["opponents"]:
        tag = "" if row["ranked"] else "（参考队）"
        lines.append("| %s%s | %+.0f | %d-%d-%d | %d:%d |" % (row["display"], tag, row["rating"], row["wins"], row["draws"],
                                                            row["losses"], row["goals_for"], row["goals_against"]))
    lines += ["", "对手分数是固定的历史个人榜参照，不是本次重新测出的官方成绩。局数少，机器和种子也不同，"
              "这个估计不能当作正式赛名次；更稳可加 --full。",
              "正式成绩见 soccer/results/个人榜.md。", ""]
    return "\n".join(lines)


def validate_result(doc, root):
    """只读校验 result.json：不编译、不运行投稿；按可信赛程和冻结个人榜重算局数、胜平负总数、
    固定参照分、你的实力值和名次，任何一项不符就 ValueError。返回 (field, extra)。"""
    root = Path(root)
    ratings, display, _ = load_reference(root)
    budget = doc.get("budget") if isinstance(doc, dict) else None
    if (not isinstance(doc, dict) or type(doc.get("schema")) is not int or doc["schema"] != 1 or
            doc.get("complete") is not True or doc.get("mode") != "formal" or doc.get("rating_method") != "fixed-individual" or
            not isinstance(budget, dict) or not official_budget(budget) or budget != budget_info(root / "soccer" / "workspace") or
            type(doc.get("full")) is not bool or type(doc.get("seed")) is not int or doc["seed"] != SEED or
            doc.get("reference") != {"path": REFERENCE, "board": "个人榜", "field": "bt", "anchor": "ref-baseline"} or
            not isinstance(doc.get("source_sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", doc["source_sha256"])):
        raise ValueError("足球成绩缺少正式 CPU 预算、固定个人榜口径或源码哈希")
    games = schedule(doc["full"])
    per = len(games) // len(OPPONENTS)
    if (type(doc.get("games")) is not int or doc["games"] != len(games) or
            type(doc.get("seeds_per_opponent")) is not int or doc["seeds_per_opponent"] * 2 != per):
        raise ValueError("足球成绩局数不完整")
    opponents = doc.get("opponents")
    if (not isinstance(opponents, list) or len(opponents) != len(OPPONENTS) or
            any(not isinstance(r, dict) for r in opponents) or [r.get("name") for r in opponents] != OPPONENTS):
        raise ValueError("足球成绩对手不完整、重复或顺序不对")
    for row in opponents:
        name = row["name"]
        wdl = [row.get(k) for k in ("wins", "draws", "losses")]
        if (row.get("ranked") is not (name in TEAMS) or row.get("display") != display[name] or
                not number(row.get("rating")) or row["rating"] != ratings[name]):
            raise ValueError("足球固定参照评分或排名身份不一致")
        if any(type(v) is not int or v < 0 for v in wdl) or sum(wdl) != per:
            raise ValueError("足球逐对手胜平负和固定赛程不符")
    rating = fit([(ratings[r["name"]], r["wins"], r["draws"], r["losses"]) for r in opponents])
    rank = 1 + sum(ratings[n] > rating for n in TEAMS)
    if (not number(doc.get("rating")) or not math.isclose(doc["rating"], rating, abs_tol=1e-7) or
            type(doc.get("rank")) is not int or doc["rank"] != rank):
        raise ValueError("足球实力值或参照名次与可信重算不符")
    stats = doc.get("stats")
    if (not isinstance(stats, dict) or
            any(type(stats.get(k)) is not int or stats[k] < 0 for k in ("timeouts", "invalid", "crash")) or
            any(not number(stats.get(k)) or stats[k] < 0 for k in ("first_ms", "max_ms"))):
        raise ValueError("足球成绩缺少你的故障与计时统计")
    field = [{"name": display[n], "is_entry": False, "score": round(ratings[n], 1)} for n in TEAMS]
    field.append({"name": "你的 bot", "is_entry": True, "score": round(rating, 1)})
    keys = ("schema", "complete", "rating_method", "mode", "budget", "reference", "source_sha256", "seed", "full",
            "games", "seeds_per_opponent", "rating", "rank", "opponents", "stats")
    extra = {k: doc[k] for k in keys}
    extra["games_note"] = "%d 局（15 个对手 × %d 个种子 × 换边）" % (len(games), doc["seeds_per_opponent"])
    return field, extra


def prepare(build, match, mine, programs):
    """用正式编译命令编译你的程序和 24 份官方程序（先核对封存哈希），从公开源码重编参考队和裁判。"""
    try:
        exes = {"me": build.compile_cpp(mine)}
        for label, (src, sha) in programs.items():
            path = HERE / src
            if hashlib.sha256(path.read_bytes()).hexdigest() != sha:
                raise SystemExit(f"官方程序 {src} 和赛时封存的哈希不符，不评测。")
            exes[label] = build.compile_cpp(path, quiet=True)
        sys.path.insert(0, str(HERE))
        import build_reference                  # 每次从公开源码重编参考队和裁判
        build_reference.main()
        for ref, bot in REFS.items():
            exes[f"{ref}/a"] = exes[f"{ref}/b"] = WS / "bots" / f"{bot}.exe"
        match.arena_exe()
    except build.BuildError as e:
        raise SystemExit(f"编译出错：{e}")
    return exes


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bot", nargs="?", help="bot.cpp；默认使用选手包模板 workspace/template.cpp")
    ap.add_argument("--full", action="store_true", help="局数是默认的 4 倍（%d 局）" % (len(OPPONENTS) * SEEDS * 8))
    ap.add_argument("--out", help="结果目录；默认新建 soccer/challenge-runs/ 下的目录")
    ap.add_argument("--require-cpu", action="store_true", help="社区评测：要求官方每步 10 / 第 0 步 1000 CPU 毫秒预算")
    args = ap.parse_args()
    require_windows()
    build, match = load_tools()
    ensure_compiler(build)
    mine = Path(args.bot).resolve() if args.bot else WS / "template.cpp"
    if not mine.is_file():
        raise SystemExit(f"找不到你的程序：{mine}")
    budget = budget_info()
    if args.require_cpu and not official_budget(budget):
        raise SystemExit("社区足球评测必须使用官方的每步 10 CPU 毫秒预算（第 0 步 1000 毫秒，累计超时 30 次结束）。")
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
    try:
        ratings, display, programs = load_reference(HERE.parent)
    except (OSError, ValueError, KeyError, TypeError) as e:
        raise SystemExit(f"个人榜参照读不懂或和赛时记录对不上，不评测：{e}")
    games = schedule(args.full)
    workers = max(1, min(4, (os.cpu_count() or 2) // 2))
    print(f"你的程序：{mine}（踢本队两个座位）\n对手：12 支个人队 + 3 支参考队（共 15 个）\n"
          f"共 {len(games)} 局（每个对手 {SEEDS * (4 if args.full else 1)} 个种子 × 换边），正式计时，并行 {workers}\n"
          f"（第一次运行要编译 30 个程序，稍等一会儿）\n", flush=True)
    exes = prepare(build, match, mine, programs)
    entries = list(zip(entry_names(), [exes["me"]] + [exes[f"{o}/{s}"] for o in OPPONENTS for s in "ab"]))
    index = entry_index()
    start = time.time()
    try:
        recs = match.run_batch(entries, [(i, seed, 0, [index[s] for s in seats]) for i, (_, seed, _, seats) in enumerate(games)],
                               workers, ["--step-ms", str(budget["step_ms"]), "--first-ms", str(budget["first_ms"]),
                                         "--max-timeouts", str(budget["max_timeouts"]), "--sandbox", "none"])
    except match.ArenaError as e:
        raise SystemExit(f"出错了：{e}")
    recs = sorted(recs, key=lambda r: r.get("id") if type(r.get("id")) is int else -1)
    print(f"打完了，用时 {time.time() - start:.0f} 秒。\n")
    if hashlib.sha256(mine.read_bytes()).hexdigest() != source_sha:
        raise SystemExit("评测途中 bot 源码发生了变化，不生成成绩。")
    try:
        doc = make_result(games, recs, ratings, display, source_sha, args.full, budget)
    except (ValueError, TypeError, KeyError, IndexError, OverflowError) as e:
        raise SystemExit(f"对局结果不完整或无效，不生成成绩：{e}")
    report = render_report(doc)
    (run_dir / "report.md").write_text(report, encoding="utf-8")
    (run_dir / "result.json").write_text(json.dumps(doc, ensure_ascii=False, indent=1, allow_nan=False) + "\n", encoding="utf-8")
    print(report)
    print(f"结果文件：{run_dir / 'result.json'}\n成绩表：{run_dir / 'report.md'}")


if __name__ == "__main__":
    main()
