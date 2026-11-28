# /// script
# 统计 v1.4 / v1.5 实验对局的关键指标。
# 用法：py -3 -X utf8 scripts/exp_metrics.py experiment-records/rule-experiments/exp-v15
# 读取 结果.jsonl + replays/*.json；回放缺帧时自动跳过对应指标。
# ///

import json
import sys
import glob
import os
import collections
import statistics


def load_games(root):
    games = []
    for f in sorted(glob.glob(os.path.join(root, "replays", "*.json"))):
        d = json.load(open(f, encoding="utf-8"))
        for g in d.get("games", []):
            games.append((f, g))
    return games


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "experiment-records/rule-experiments/exp-v15"
    games = load_games(root)
    if not games:
        print("没有回放：", root)
        return
    reasons = collections.Counter()
    levels = collections.Counter()
    kills = []
    shots = []
    turns = []
    vs_totals = [[], []]
    vs_kills = [[], []]
    vs_relics = [[], []]
    vs_flags = [[], []]
    ms_end = []
    comp = collections.Counter()
    levels_end = collections.Counter()
    tech_c = collections.Counter()
    flag_end = collections.Counter()
    relic_end = collections.Counter()
    timeup = 0
    ms_kill = 0
    for path, g in games:
        r = g["result"]
        reasons[r["reason"]] += 1
        levels[r["level"]] += 1
        turns.append(r["turn"])
        if "时间到" in r["reason"]:
            timeup += 1
        if "母舰被摧毁" in r["reason"]:
            ms_kill += 1
        kills.append(sum(c["kills"] for c in r["cards"]))
        shots.append(sum(r.get("shots", [0, 0])))
        vs = r.get("vs")
        if vs:
            for s in range(2):
                vs_totals[s].append(vs[s]["total"])
                vs_kills[s].append(vs[s]["kills"])
                vs_relics[s].append(vs[s]["relics"])
                vs_flags[s].append(vs[s]["flags"])
        last = g["frames"][-1] if g.get("frames") else None
        if last:
            for u in last["units"]:
                comp[u["kind"]] += 1
                if u.get("lvl", 0) >= 2:
                    levels_end[(u["kind"], u["lvl"])] += 1
                if u["kind"] == "MOTHERSHIP":
                    ms_end.append(u["hp"] / u["maxHp"])
            for t in last.get("techs", [[], []]):
                for tid in t:
                    if tid.startswith("C"):
                        tech_c[tid] += 1
            for f in last.get("flags", []):
                flag_end[f["side"]] += 1
            for rel in last.get("relics", []):
                relic_end[rel["state"]] += 1
    n = len(games)
    print(f"局数 {n} ｜ 时间到 {timeup} ({timeup/n:.0%}) ｜ 母舰被毁 {ms_kill} ｜ 平均 {statistics.mean(turns):.0f} tick")
    print("判定：", dict(reasons))
    print("层级：", dict(levels))
    print(f"场均击杀 {statistics.mean(kills):.2f} ｜ 场均开火 {statistics.mean(shots):.1f}")
    if vs_totals[0]:
        for s, name in [(0, "红"), (1, "蓝")]:
            print(f"{name}方 VS 均值 {statistics.mean(vs_totals[s]):.0f} "
                  f"(击杀 {statistics.mean(vs_kills[s]):.0f} / 遗产 {statistics.mean(vs_relics[s]):.0f} / 插旗 {statistics.mean(vs_flags[s]):.0f})")
    if ms_end:
        print(f"母舰终局 HP 均值 {statistics.mean(ms_end):.0%} ｜ 最低 {min(ms_end):.0%}")
    print("终局兵种：", dict(comp))
    print("终局 2 级以上：", {f"{k} L{v}": c for (k, v), c in levels_end.most_common(10)})
    if tech_c:
        print("火控科技完成：", dict(tech_c))
    if flag_end:
        print("终局旗数：", dict(flag_end))
    if relic_end:
        print("终局遗产：", dict(relic_end))
    print("\n逐局：")
    for i, (path, g) in enumerate(games):
        r = g["result"]
        vs = r.get("vs") or [{}, {}]
        print(f"{i:02d} {os.path.basename(path)} {g.get('seats')} "
              f"{r['winner']}/{r['level']} {r['reason']} t={r['turn']} "
              f"VS={vs[0].get('total','-')}/{vs[1].get('total','-')} "
              f"K={sum(c['kills'] for c in r['cards'])} S={sum(r.get('shots',[0,0]))}")


if __name__ == "__main__":
    main()
