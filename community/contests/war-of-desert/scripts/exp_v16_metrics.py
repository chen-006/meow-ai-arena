# /// script
# v1.6 实验统计：结果.jsonl + 可选回放。
# 用法：py -3 -X utf8 scripts/exp_v16_metrics.py experiment-records/rule-experiments/exp-v16-500 [--replay-sample 5]
# ///
import json
import sys
import glob
import os
import collections
import statistics


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "experiment-records/rule-experiments/exp-v16-500"
    sample = 1
    if "--replay-sample" in sys.argv:
        sample = max(1, int(sys.argv[sys.argv.index("--replay-sample") + 1]))

    rows = []
    with open(os.path.join(root, "结果.jsonl"), encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    if not rows:
        print("没有结果：", root)
        return

    invalid = sum(1 for r in rows if r.get("invalid"))
    valid = [r for r in rows if not r.get("invalid")]
    seats = collections.Counter(r.get("winnerSeat") for r in valid)
    reasons = collections.Counter(r.get("reason") for r in valid)
    levels = collections.Counter(r.get("level") for r in valid)
    turns = [r.get("turn", 0) for r in valid]
    totals = [sum(r.get("totals", [])) / 2 for r in valid if len(r.get("totals", [])) == 2]
    field_names = ["VS", "Kill", "Preserve", "Mothership", "KD", "FlagCtrl", "Relic"]
    field_avg = []
    for i, name in enumerate(field_names):
        vals = [r["fields"][s][i] for r in valid if len(r.get("fields", [])) == 2 for s in (0, 1)]
        field_avg.append((name, statistics.mean(vals) if vals else 0))

    print(f"总对局 {len(rows)} ｜ 有效 {len(valid)} ｜ 无效 {invalid}")
    print(f"时间到 {sum(1 for r in valid if '时间到' in r.get('reason',''))} "
          f"({sum(1 for r in valid if '时间到' in r.get('reason',''))/max(1,len(valid)):.1%}) ｜ "
          f"平均 {statistics.mean(turns) if turns else 0:.0f} tick")
    print(f"决胜座位 seat0={seats.get(0,0)} seat1={seats.get(1,0)} draw={seats.get(-1,0)}")
    print("判定：", dict(reasons))
    print("层级：", dict(levels))
    print(f"七领域总分均值 {statistics.mean(totals) if totals else 0:.2f}")
    print("领域均值：" + " ｜ ".join(f"{n} {v:.1f}" for n, v in field_avg))

    if not os.path.isdir(os.path.join(root, "replays")):
        return
    replays = sorted(glob.glob(os.path.join(root, "replays", "*.json")))
    replays = replays[::sample]
    tech_score = collections.Counter()
    research_seen = 0
    ship_games = 0
    flags_destroyed_games = 0
    relic_delivered = []
    for path in replays:
        try:
            d = json.load(open(path, encoding="utf-8"))
        except Exception:
            continue
        for g in d.get("games", []):
            fr = g.get("frames") or []
            if any(u.get("kind") == "RESEARCH" for f in fr for u in f.get("units", [])):
                ship_games += 1
            if any(f.get("research") for f in fr):
                research_seen += 1
            if fr:
                techs = fr[-1].get("techs", [[], []])
                for side in (0, 1):
                    for t in techs[side]:
                        if t.startswith("R"):
                            tech_score[t] += 1
                # 旗数量下降视为发生过拔旗
                counts = []
                for f in fr:
                    counts.append(sum(1 for x in (f.get("flags") or [])))
                if counts and max(counts) > min(counts):
                    flags_destroyed_games += 1
                vs = g.get("result", {}).get("vs") or []
                for x in vs:
                    relic_delivered.append(x.get("delivered", 0))
    n = max(1, len(replays) * 2)
    print(f"回放抽样 {len(replays)} 场 / {n} 局："
          f"研究舰出现 {ship_games/n:.0%} ｜ 研究中 {research_seen/n:.0%} ｜ "
          f"旗被拔过 {flags_destroyed_games/n:.0%}")
    print("遗产科技完成：" + (", ".join(f"{k} {v}" for k, v in sorted(tech_score.items())) or "无"))
    if relic_delivered:
        print(f"场均唯一送达遗产 {statistics.mean(relic_delivered):.2f}")


if __name__ == "__main__":
    main()
