"""一条命令从公开的逐局结果重算第 12 集全部 6 回合正式比分与 6 张 3×3 赛后矩阵，并与 results/ 逐项核对。

    cd boomerang-team
    python tournament/score.py
"""
import gzip
import json
import math
import re
import statistics
import sys
from pathlib import Path

EP = Path(__file__).resolve().parent.parent
RES = EP / "results"
SLOTS = ("正式", "备选1", "备选2")


def game_score(g):
    """偶数局号 Astra 坐左（winner==0 胜），奇数局号 Astra 坐右（winner==1 胜），平局 0.5。"""
    if g["winner"] < 0:
        return 0.5
    return float(g["winner"] == (g["id"] % 2))


def paired_scores(games):
    pairs = {}
    for g in games:
        pair = pairs.setdefault(g["seed"], {})
        side = g["id"] % 2
        if side in pair:
            raise ValueError(f"同一种子重复计入同一侧：seed={g['seed']}")
        pair[side] = game_score(g)
    if not pairs or any(len(p) != 2 for p in pairs.values()):
        raise ValueError("缺少同种子左右配对局")
    return [sum(p.values()) / 2.0 for _, p in sorted(pairs.items())]


def summarize(games):
    vals = paired_scores(games)
    rate = statistics.mean(vals)
    se = statistics.stdev(vals) / math.sqrt(len(vals)) if len(vals) > 1 else 0.0
    return rate, se


def round_points(games):
    """与比赛时主办方复盘脚本完全一致的整数精确判定：偏离 50% 严格小于两倍配对标准误时各得 0.5 分。"""
    units = [int(round(x * 4)) for x in paired_scores(games)]
    n, s = len(units), sum(units)
    if s == 2 * n or (s - 2 * n) ** 2 * (n - 1) < 4 * (n * sum(x * x for x in units) - s * s):
        return [0.5, 0.5]
    return [int(s > 2 * n), int(s < 2 * n)]


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    # 1. Load & verify formal 10,600 games
    formal_by_round = {r: [] for r in range(1, 7)}
    with gzip.open(RES / "formal_games.jsonl.gz", "rt", encoding="utf-8") as f:
        for line in f:
            g = json.loads(line)
            formal_by_round[g["round"]].append(g)

    total_formal = sum(len(v) for v in formal_by_round.values())
    print(f"已读取正式赛逐局数据：{total_formal:,} 局")
    print()
    print("| 回合 | 地图 | 局数 | Astra 胜/负/平 | Astra 胜率 | 配对 SE | 本回合分 | 累计分 | 故障 |")
    print("|---|---|---:|---|---:|---:|---|---|---:|")

    cumul = [0.0, 0.0]
    total_obj = json.loads((RES / "总成绩.json").read_text(encoding="utf-8"))
    errors = []

    for r in range(1, 7):
        games = formal_by_round[r]
        rate, se = summarize(games)
        pts = round_points(games)
        cumul = [cumul[0] + pts[0], cumul[1] + pts[1]]
        w = sum(game_score(g) == 1.0 for g in games)
        l = sum(game_score(g) == 0.0 for g in games)
        d = len(games) - w - l
        faults = sum(p["faults_total"] for g in games for p in g["players"])
        mname = games[0]["map"]

        print(
            f"| R{r} | {mname:7} | {len(games):4d} | {w:4d}/{l:3d}/{d:2d} | "
            f"{rate:7.3%} | ±{se*100:.3f} pp | {pts[0]:g} : {pts[1]:g} | {cumul[0]:g} : {cumul[1]:g} | {faults:2d} |"
        )

        ref = json.loads((RES / "rounds" / f"R{r}_成绩.json").read_text(encoding="utf-8"))
        if [w, l, d] != ref["胜负平"]:
            errors.append(f"R{r} 胜负平不一致：重算 {[w,l,d]} vs 存档 {ref['胜负平']}")
        if abs(rate - ref["胜率"][0]) > 1e-12 or abs((1 - rate) - ref["胜率"][1]) > 1e-12:
            errors.append(f"R{r} 胜率不一致")
        if abs(se - ref["配对标准误"]) > 1e-12:
            errors.append(f"R{r} 配对标准误不一致：{se} vs {ref['配对标准误']}")
        if pts != ref["得分"]:
            errors.append(f"R{r} 得分不一致：{pts} vs {ref['得分']}")
        if cumul != ref["累计得分"]:
            errors.append(f"R{r} 累计得分不一致：{cumul} vs {ref['累计得分']}")
        if faults != ref["故障合计"]:
            errors.append(f"R{r} 故障合计不一致：{faults} vs {ref['故障合计']}")

    # 2. Load & verify 54,000 matrix games across 6 rounds x 9 cells
    matrix_by_cell = {}
    total_matrix = 0
    with gzip.open(RES / "matrix_games.jsonl.gz", "rt", encoding="utf-8") as f:
        for line in f:
            g = json.loads(line)
            key = (g["round"], g["slot_a"], g["slot_f"])
            matrix_by_cell.setdefault(key, []).append(g)
            total_matrix += 1

    print()
    print(f"已读取赛后矩阵逐局数据：{total_matrix:,} 局（6 张 3×3 = 54 格）")
    cell_pat = re.compile(r"(\d+\.\d+)% ± (\d+\.\d+)%")
    for r in range(1, 7):
        md_text = (RES / "rounds" / f"R{r}_矩阵.md").read_text(encoding="utf-8")
        md_cells = {}
        for ln in md_text.splitlines():
            parts = [x.strip() for x in ln.strip().strip("|").split("|")]
            if len(parts) == 4 and parts[0] in SLOTS:
                sa = parts[0]
                for sf, cell_str in zip(SLOTS, parts[1:]):
                    md_cells[(sa, sf)] = cell_str

        print(f"\nR{r} 矩阵：")
        for sa in SLOTS:
            row_strs = []
            for sf in SLOTS:
                games = matrix_by_cell[(r, sa, sf)]
                rate, se = summarize(games)
                disp = f"{rate:.2%} ± {se:.2%}"
                row_strs.append(f"{disp:>15}")
                ref_c = total_obj["matrices"][f"R{r}"][sa][sf]
                if abs(rate - ref_c["astra_rate"]) > 1e-12 or abs(se - ref_c["se"]) > 1e-12:
                    errors.append(f"R{r} 矩阵 {sa}×{sf} 与总成绩.json 精度不一致")
                if md_cells.get((sa, sf)) != disp:
                    errors.append(f"R{r} 矩阵 {sa}×{sf} 与 R{r}_矩阵.md 显示不一致：{disp} vs {md_cells.get((sa, sf))}")
            print(f"  {sa:4} | " + " | ".join(row_strs))

    print()
    if errors:
        print("核对失败：")
        for e in errors:
            print(" -", e)
        sys.exit(1)

    print(
        f"核对通过：6 回合正式比分（Astra队 {cumul[0]:g} : {cumul[1]:g} Fable队，共 {total_formal:,} 局）"
        f"与 6 张 3×3 赛后矩阵（共 54 格 / {total_matrix:,} 局）与 results/ 存档完全一致。"
    )


if __name__ == "__main__":
    main()
