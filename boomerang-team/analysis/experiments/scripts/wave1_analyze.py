"""从裁判逐局结果复算胜率、同种子差值、故障和消息样本。"""
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
from statistics import mean, stdev

from run import ROOT, WORKERS, STEP_MS, assert_game, game_rows, read, sha, verify, write
from prepare import A, F, MAPS, OLD_MAPS
from match import summary as official_summary

NAMES = dict(zip(A + F, ("Astra", "Sol", "Hydrogen", "Grok", "Kimi",
                        "Fable", "Opus", "Carbon", "GLM", "DeepSeek")))
MAP_LABEL = dict(zip(MAPS, ("歼灭 / caldera", "占点 / piers", "刺王 / keeps")))
CASE_LABEL = {
    "2-base": "原 A 队 vs 原 F 队",
    "2-fable-a4": "Fable 带 A4 vs 原 F 队",
    "2-astra-f4": "Astra 带 F4 vs 原 A 队",
    "2-both-swapped": "Astra 带 F4 vs Fable 带 A4",
    "1v1-final": "团队终版 Astra vs 团队终版 Fable",
    "1v1-astra": "团队终版 Astra vs 海选版 Astra",
    "1v1-fable": "团队终版 Fable vs 海选版 Fable",
    "1v1-old-control": "海选版 Astra vs 海选版 Fable",
}

def estimate(values):
    if isinstance(values, dict):
        clusters = defaultdict(list)
        for (_, seed), value in values.items():
            clusters[seed].append(value)
        values = [mean(v) for v in clusters.values()]
    else:
        values = list(values)
    return dict(n=len(values), rate=mean(values), se=stdev(values) / math.sqrt(len(values)))

def points(g, focus):
    return .5 if g["winner"] < 0 else float(g["winner"] == ((g["id"] % 2) ^ focus))

class Result:
    def __init__(self, games, focus):
        self.games = games
        self.focus = focus
        blocks = defaultdict(dict)
        self.wld = Counter()
        self.faults = [Counter(), Counter()]
        self.players_faults = Counter()
        self.fault_games = 0
        self.non_ok_status = Counter()
        self.opp_kills = [0, 0]
        self.friendly_kills = [0, 0]
        self.piers_scores = [0, 0]
        self.piers_n = 0
        for g in games:
            flip = g["id"] % 2
            v = points(g, focus)
            block = blocks[g["map"], g["seed"]]
            verify(flip in block, False, "同侧重复")
            block[flip] = v
            self.wld[v] += 1
            self.fault_games += any(p["faults_total"] for p in g["players"])
            if g["mode"] == 4:
                self.piers_n += 1
                for side, val in enumerate(g["score"]):
                    self.piers_scores[side ^ flip ^ focus] += val
            size = len(g["players"]) // 2
            for p in g["players"]:
                t = p["seat"] // size ^ flip ^ focus
                if p["status"] != "ok":
                    self.non_ok_status[p["status"]] += 1
                self.opp_kills[t] += p["kills"] - p["k_team"]
                self.friendly_kills[t] += p["k_team"]
                if p["faults_total"]:
                    self.players_faults[t, p["name"]] += p["faults_total"]
                    self.faults[t]["合计"] += p["faults_total"]
                    self.faults[t]["超时"] += p["timeouts"]
                    self.faults[t]["非法"] += p["invalid"]
                    for _, label, _ in p["faults"]:
                        if label not in ("timeout", "invalid"):
                            self.faults[t][label] += 1
        verify(all(set(pair) == {0, 1} for pair in blocks.values()), True, "镜像完整性")
        self.pairs = {k: mean(v.values()) for k, v in blocks.items()}
        self.est = estimate(self.pairs)
        self.n = len(games)

    def out(self):
        return dict(games=self.n, pairs=len(self.pairs), **self.est, wins=self.wld[1], losses=self.wld[0],
                    draws=self.wld[.5], enemy_kills=[x/self.n for x in self.opp_kills],
                    friendly_kills=[x/self.n for x in self.friendly_kills],
                    piers_scores=[x/self.piers_n for x in self.piers_scores] if self.piers_n else None,
                    faults=[dict(x) for x in self.faults], fault_games=self.fault_games,
                    non_ok_status=dict(self.non_ok_status),
                    fault_players=[dict(side=k[0], name=k[1], count=v) for k, v in sorted(self.players_faults.items())])

def difference(base, other):
    keys = base.keys() & other.keys()
    return estimate({k: base[k] - other[k] for k in keys})

def player_averages(games, name):
    players = [p for g in games for p in g["players"] if p["name"] == name]
    verify(len(players), len(games), "反事实目标席位")
    n = len(games)
    return dict(friendly=sum(p["k_team"] for p in players) / n,
                opponents=sum(p["kills"] - p["k_team"] for p in players) / n,
                throws=sum(p["throws"] for p in players) / n,
                faults=sum(p["faults_total"] for p in players))

def load_main(require_full=True):
    folder = ROOT / "main"
    by_name = {}
    expected = {}
    for b in read(folder / "manifest.json")["cases"]:
        obs = []
        by_name[b["name"]] = b, obs
        for j in range(2 * b["n"]):
            expected[b["offset"] + j] = b, obs, j
    seen = set()
    for g in game_rows(folder):
        gid = g["id"]
        verify(gid in seen, False, "重复局号")
        b, obs, j = expected[gid]
        assert_game(g, b, j // 2, j % 2)
        obs.append(g)
        seen.add(gid)
    if require_full:
        verify(len(seen), len(expected), "主批完整性")
    return by_name, len(seen), len(expected)

def gather_legacy():
    games = [
        g for g in read(ROOT / "ep11/official_games.json")
        if g["mode"] == 0 and set(g["names"]) == {"astra", "fable"}
    ]
    scores = defaultdict(list)
    faults = 0
    wld = Counter()
    for g in games:
        ranks = {p["name"]: p["rank"] for p in g["players"]}
        v = 1 if ranks["astra"] < ranks["fable"] else 0 if ranks["astra"] > ranks["fable"] else .5
        scores[g["map"], g["seed"]].append((g["names"][0], v))
        wld[v] += 1
        faults += sum(p["faults"] for p in g["players"])
    verify(all({s for s, _ in rows} == {"astra", "fable"} and len(rows) == 2 for rows in scores.values()),
           True, "海选历史对局配对")
    schedule = read(ROOT / "ep11/schedule.json")
    verify(len(games), 2 * len(schedule["maps"]) * len(schedule["duel_seeds"]), "海选头对头对局数")
    return dict(games=len(games), pairs=len(scores), wins=wld[1], losses=wld[0], draws=wld[.5],
                faults=faults, **estimate({k: mean(v for _, v in pairs) for k, pairs in scores.items()}))

def inspect_packets(path, seat, protocol):
    metrics = Counter()
    with path.open(encoding="utf-8") as f:
        sender = None
        nx = ny = 0
        for line in f:
            if line.startswith("MAP "):
                _, w, h, _ = line.split()
                nx, ny = int(w), int(h)
            elif line.startswith("S "):
                fields = line.split("|", seat + 2)[seat + 1].split()
                sender = tuple(map(int, fields[:3]))
            elif line.startswith("A ") and sender[0]:
                parts = line.split("|", seat + 2)
                _, x, y = sender
                t = int(parts[0].split()[1])
                raw = int(parts[seat + 1].split()[5])
                if not raw:
                    continue
                metrics["nonzero"] += 1
                flag = raw >> 14
                if protocol == "A":
                    cx, cy = raw & 63, (raw >> 6) & 63
                    if flag != t % 4 or not (0 <= cx < nx and 0 <= cy < ny):
                        continue
                    pos = (500 * cx + 250, 500 * cy + 250)
                else:
                    if not 1 <= flag <= 3:
                        continue
                    metrics[f"F{flag}"] += 1
                    pos = (250 * (raw & 127) + 125, 250 * ((raw >> 7) & 127) + 125)
                metrics["format"] += 1
                if protocol == "A" or flag == 1:
                    metrics["position"] += 1
                    if math.dist((x, y), pos) > 1000:
                        metrics["error_gt_1m"] += 1
    return metrics

def packet_audit(main):
    folder = ROOT / "main"
    cases = {
        "2-base": [("A 原队", 0, "A"), ("F 原队", 1, "F")],
        "2-fable-a4": [("Fable → A4", 0, "A")],
        "2-astra-f4": [("Astra → F4", 0, "F")],
        "2-both-swapped": [("Astra → F4", 0, "F"), ("Fable → A4", 1, "A")],
    }
    metrics = defaultdict(Counter)
    examples = {}
    for key, hints in cases.items():
        for m in MAPS:
            b, _ = main[f"{key}-{m}"]
            for j in range(4):
                gid = b["offset"] + j
                paths = list(folder.glob(f"replays-*/g{gid}.txt"))
                verify(len(paths), 1, f"消息回放 {gid}")
                for label, side, protocol in hints:
                    seat = 5 * (side ^ (j % 2))
                    data = inspect_packets(paths[0], seat, protocol)
                    metrics[label].update(data)
                    metrics[label]["samples"] += 1
                    examples.setdefault(label, []).append(dict(id=gid, case=f"{key}-{m}", seat=seat,
                                                               replay=paths[0].relative_to(ROOT).as_posix(),
                                                               counts=dict(data)))
    return dict(metrics={k: dict(v) for k, v in metrics.items()}, examples=examples)

def integrity():
    for local, record in read(ROOT / "inputs.json").items():
        verify(sha(ROOT / local), record["sha256"], local)
        verify(sha(Path(record["source"])), record["sha256"], record["source"])
    for local, record in read(ROOT / "evidence/source_manifest.json").items():
        verify(sha(ROOT / "evidence" / local), record["sha256"], local)
        verify(sha(Path(record["source"])), record["sha256"], record["source"])
    for r in (3, 4):
        for record in read(ROOT / f"rounds/r{r}/official_first200_manifest.json"):
            verify(sha(Path(record["source"])), record["sha256"], record["source"])
    for phase in ("pilot", "main"):
        batch = read(ROOT / phase / "manifest.json")
        verify((batch["workers"], batch["step_ms"]), (WORKERS, STEP_MS), phase)
        for local, digest in batch["input_sha256"].items():
            verify(sha(ROOT / local), digest, local)
    build = read(ROOT / "patches/build.json")
    verify(sha(ROOT / "patches/hydrogen_roam.exe"), build["sha256"], "Hydrogen 编译产物")

    spec = read(ROOT / "main/manifest.json")
    cases = {b["name"]: b for b in spec["cases"]}
    for r, case, player in ((3, "3a-r3-glm", "glm"), (4, "3b-r4-roam", "hydrogen")):
        block = cases[case]
        official = read(ROOT / f"rounds/r{r}/official_batch.json")
        inputs = spec["input_sha256"]
        for field, official_field in (("formation", "布阵"), ("plan", "战术")):
            verify([inputs[p] for p in block[field]], official[official_field], case)
        verify(inputs["kit/maps/piers.txt"], official["地图"], case)
        verify(inputs[spec["engine"]], official["裁判"], case)
        changed = []
        for observed, expected in zip(block["teams"], official["程序"]):
            verify([n for n, _ in observed], [n for n, _ in expected], case)
            changed.extend(n for (n, path), (_, digest) in zip(observed, expected) if inputs[path] != digest)
        verify(changed, [player], case)

    for m in MAPS:
        baseline = cases[f"2-base-{m}"]
        flat = [entry for roster in baseline["teams"] for entry in roster]
        for i, name in enumerate(A + F):
            replacement = cases[f"4-{name}-{m}"]
            changed = [entry for roster in replacement["teams"] for entry in roster]
            expected = flat[:i] + [[name, "kit/bots/strong.exe"]] + flat[i+1:]
            verify(changed, expected, replacement["name"])
            for field in ("formation", "plan"):
                manifest = spec["input_sha256"]
                verify([manifest.get(p) for p in replacement[field]],
                       [manifest.get(p) for p in baseline[field]], replacement["name"])

def aggregate(main):
    cell = {name: Result(games, b["focus"]) for name, (b, games) in main.items()}
    for name, result in cell.items():
        # 正赛统计脚本是独立口径参照，单图或单挑都可直接复算。
        rate, se = official_summary(result.games)
        if result.focus:
            rate = 1 - rate
        for field, expected in (("rate", rate), ("se", se)):
            verify(math.isclose(result.est[field], expected, rel_tol=0, abs_tol=1e-12), True, (name, field))
    def team_group(prefix):
        choices = [cell[f"{prefix}-{m}"] for m in MAPS]
        return Result([g for c in choices for g in c.games], choices[0].focus)

    group = {prefix: team_group(prefix) for prefix in (
        "1-clones", "2-base", "2-fable-a4", "2-astra-f4", "2-both-swapped", *(f"4-{n}" for n in A + F)
    )}
    base = group["2-base"]
    final, old = cell["1v1-final"], cell["1v1-old-control"]
    data = {
        "cells": {k: v.out() for k, v in cell.items()},
        "groups": {k: v.out() for k, v in sorted(group.items())},
        "duel_map_summaries": {
            label: {m: Result([g for g in cell[label].games if g["map"] == m], 0).out()
                    for m in OLD_MAPS}
            for label in ("1v1-final", "1v1-old-control")
        },
        "legacy_historical": gather_legacy(),
        "duel_delta": difference(final.pairs, old.pairs),
        "captain_delta": {},
        "counterfactual": {},
        "contribution": [],
        "packet_audit": packet_audit(main),
    }
    for m in (*MAPS, "3map"):
        r, f, a = ((group[k] for k in ("2-base", "2-fable-a4", "2-astra-f4")) if m == "3map"
                   else (cell[f"{k}-{m}"] for k in ("2-base", "2-fable-a4", "2-astra-f4")))
        data["captain_delta"][m] = {
            "A4_replace_f_with_a": difference(r.pairs, f.pairs),
            "F4_replace_f_with_a": difference(a.pairs, {k: 1-v for k, v in r.pairs.items()}),
        }
    for r, key in ((3, "3a-r3-glm"), (4, "3b-r4-roam")):
        folder = ROOT / f"rounds/r{r}"
        observed = cell[key]
        control = Result(read(folder / "official_first200.json"), observed.focus)
        official = read(folder / "official_score.json")
        historical_rate = official["胜率"][observed.focus]
        name = "glm" if r == 3 else "hydrogen"
        data["counterfactual"][str(r)] = {
            "control": control.out(),
            "delta": difference(observed.pairs, control.pairs),
            "full_baseline": historical_rate,
            "full_se": official["配对标准误"],
            "full_delta": observed.est["rate"] - historical_rate,
            "player_before": player_averages(control.games, name),
            "player_after": player_averages(observed.games, name),
        }
    for name in A + F:
        result = group[f"4-{name}"]
        focus = result.focus
        orient = lambda val: val if focus == 0 else 1-val
        base_pairs = {k: orient(v) for k, v in base.pairs.items() if k in result.pairs}
        by_map = {
            m: difference({k: v for k, v in base_pairs.items() if k[0] == m}, cell[f"4-{name}-{m}"].pairs)
            for m in MAPS
        }
        data["contribution"].append(dict(name=name, team="A" if focus == 0 else "F", maps=by_map,
                                         matched_baseline=estimate(base_pairs), full_baseline=orient(base.est["rate"]),
                                         effect=difference(base_pairs, result.pairs),
                                         full_effect=orient(base.est["rate"]) - result.est["rate"],
                                         result=result.out()))
    data["contribution"].sort(key=lambda c: (-c["effect"]["rate"], c["name"]))
    return data

