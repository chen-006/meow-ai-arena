"""Soccer challenge schedule, fixed individual reference and fail-closed result contract.

All match results here are synthetic; these tests do not compile or run submission code.
Native Windows judge coverage belongs to the Windows smoke job.
"""
import contextlib
import copy
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
import types
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("soccer_challenge", ROOT / "soccer" / "challenge.py")
challenge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(challenge)
with gzip.open(ROOT / "soccer/results/individual/results.json.gz", "rt", encoding="utf-8") as f:
    FROZEN = json.load(f)
RATINGS = {name: row["rating"] for name, row in FROZEN["bt"].items()}
BUDGET = {"budget_kind": "cpu", "step_ms": 10, "first_ms": 1000, "max_timeouts": 30}


def records(games, score=(2, 2)):
    """A deterministic fixture: every game ends with the same score, nobody faults."""
    index, names = challenge.entry_index(), challenge.entry_names()
    return [{"id": i, "seed": seed, "first_kick": 0, "score": list(score), "steps": 1200,
             "players": [{"name": names[index[label]], "status": "ok", "dead_step": -1, "timeouts": 0, "invalid": 0,
                          "first_ms": 1.0, "max_ms": 2.0} for label in seats]}
            for i, (_, seed, _, seats) in enumerate(games)]


def valid_doc(full=False, score=(2, 2)):
    games = challenge.schedule(full)
    _, display, _ = challenge.load_reference(ROOT)
    return challenge.make_result(games, records(games, score), RATINGS, display, "a" * 64, full, BUDGET)


class ScheduleTests(unittest.TestCase):
    def test_default_and_full_schedule(self):
        self.assertEqual(len(challenge.OPPONENTS), 15)
        formal = {g["seed"] for phase in ("halftime", "final", "individual")
                  for g in json.load(gzip.open(ROOT / f"soccer/results/{phase}/results.json.gz", "rt", encoding="utf-8"))["games"]}
        for full, seeds in ((False, 4), (True, 16)):
            games = challenge.schedule(full)
            self.assertEqual(games, challenge.schedule(full))
            self.assertEqual(len(games), 15 * seeds * 2)
            for opp in challenge.OPPONENTS:
                self.assertEqual(sum(g[0] == opp for g in games), seeds * 2)
            for a, b in zip(games[::2], games[1::2]):          # 每个种子换边各一局
                self.assertEqual((a[0], a[1], a[2], b[2]), (b[0], b[1], 0, 1))
                self.assertEqual(a[3][:2], ["me", "me"])
                self.assertEqual(b[3][2:], ["me", "me"])
                self.assertEqual(a[3][2:], b[3][:2])
            for opp, seed, side, seats in games:
                self.assertEqual(len(seats), 4)
                self.assertEqual(seats.count("me"), 2)
                self.assertNotIn(seed, formal)
                pair = seats[2:] if side == 0 else seats[:2]
                self.assertEqual(pair, [f"{opp}/a", f"{opp}/b"] if (seed - challenge.SEED) % 2 == 0 else [f"{opp}/b", f"{opp}/a"])

    def test_reference_matches_individual_board(self):
        ratings, display, programs = challenge.load_reference(ROOT)
        self.assertEqual(ratings, {n: RATINGS[n] for n in challenge.OPPONENTS})
        self.assertEqual(ratings["ref-baseline"], 0)
        self.assertEqual(programs["Opus/b"][0], "bots/half2/team5_a.cpp")      # 没交 partner：player 踢两个位置
        self.assertEqual(programs["Doubao/b"][0], "bots/half2/team3_b.cpp")
        self.assertEqual(programs["Astra/b"][0], "bots/half2/team4_a.partner.cpp")
        for src, sha in programs.values():
            self.assertEqual(hashlib.sha256((ROOT / "soccer" / src).read_bytes()).hexdigest(), sha)

    def test_bt_fit_preserves_fixed_reference_calibration(self):
        self.assertAlmostEqual(challenge.fit([(100, 0, 20, 0)]), 100, places=4)
        self.assertGreater(challenge.fit([(100, 20, 0, 0)]), 100)
        self.assertLess(challenge.fit([(100, 0, 0, 20)]), 100)

    def test_result_uses_side_score_and_refs_do_not_take_rank(self):
        games = challenge.schedule(False)
        recs = records(games)
        recs[0]["score"] = [3, 1]          # 你在 A 队：赢
        recs[1]["score"] = [3, 1]          # 你在 B 队：输
        _, display, _ = challenge.load_reference(ROOT)
        with mock.patch.object(challenge, "fit", return_value=250.0):
            doc = challenge.make_result(games, recs, RATINGS, display, "a" * 64, False, BUDGET)
        self.assertEqual(doc["rank"], 5)   # +291 / +207 参考队不占名次，前面只有 Astra、Terra、Gemini、Opus
        first = doc["opponents"][0]
        self.assertEqual((first["wins"], first["draws"], first["losses"]), (1, 6, 1))
        self.assertEqual((first["goals_for"], first["goals_against"]), (3 + 1 + 12, 1 + 3 + 12))
        for row in doc["opponents"]:
            self.assertEqual(row["rating"], RATINGS[row["name"]])
            self.assertEqual(row["ranked"], not row["name"].startswith("ref-"))


class ValidateTests(unittest.TestCase):
    def test_valid_result_passes(self):
        for full in (False, True):
            doc = json.loads(json.dumps(valid_doc(full)))
            field, extra = challenge.validate_result(doc, ROOT)
            self.assertEqual(len(field), 13)
            self.assertEqual(sum(r["is_entry"] for r in field), 1)
            self.assertEqual(extra["games"], 240 * (4 if full else 1) // 2)
            self.assertIs(extra["full"], full)
            for key in ("games_note", "rating", "rank", "source_sha256", "opponents", "budget"):
                self.assertIn(key, extra)
            self.assertAlmostEqual(extra["rating"], challenge.fit([(RATINGS[n], 0, 8 * (4 if full else 1), 0)
                                                                    for n in challenge.OPPONENTS]))

    def test_tampered_results_are_rejected(self):
        def opp(i, **kw):
            return lambda d: d["opponents"][i].update(kw)
        mutations = {
            "rating": lambda d: d.update(rating=d["rating"] + 1),
            "rank": lambda d: d.update(rank=1),
            "games": lambda d: d.update(games=d["games"] - 1),
            "seeds": lambda d: d.update(seeds_per_opponent=8),
            "full": lambda d: d.update(full=True),
            "seed": lambda d: d.update(seed=1),
            "schema": lambda d: d.update(schema=2),
            "complete": lambda d: d.update(complete=1),
            "mode": lambda d: d.update(mode="fast"),
            "method": lambda d: d.update(rating_method="refit"),
            "budget": lambda d: d["budget"].update(step_ms=500),
            "budget-kind": lambda d: d["budget"].update(budget_kind="wall"),
            "reference": lambda d: d["reference"].update(board="团队榜"),
            "hash": lambda d: d.update(source_sha256="A" * 64),
            "hash-short": lambda d: d.update(source_sha256="a" * 63),
            "drop-opponent": lambda d: d["opponents"].pop(),
            "dup-opponent": lambda d: d["opponents"].__setitem__(1, copy.deepcopy(d["opponents"][0])),
            "rename-opponent": opp(0, name="Mistral"),
            "opp-rating": opp(0, rating=1000.0),
            "opp-ranked": opp(12, ranked=True),
            "opp-display": opp(0, display="x"),
            "wdl-sum": opp(0, wins=1),
            "wdl-shift": lambda d: d["opponents"][0].update(wins=8, draws=0),
            "wdl-bool": opp(0, draws=True),
            "stats": lambda d: d["stats"].update(timeouts=-1),
        }
        for name, mutate in mutations.items():
            doc = json.loads(json.dumps(valid_doc()))
            mutate(doc)
            with self.subTest(name=name), self.assertRaises(ValueError):
                challenge.validate_result(doc, ROOT)

    def test_reweighted_but_consistent_tally_is_rescored(self):
        doc = json.loads(json.dumps(valid_doc()))
        doc["opponents"][6].update(wins=8, draws=0, losses=0)           # 改了胜负却没改分数
        with self.assertRaises(ValueError):
            challenge.validate_result(doc, ROOT)
        doc["rating"] = challenge.fit([(RATINGS[r["name"]], r["wins"], r["draws"], r["losses"]) for r in doc["opponents"]])
        doc["rank"] = 1 + sum(RATINGS[n] > doc["rating"] for n in challenge.TEAMS)
        challenge.validate_result(doc, ROOT)                              # 自洽的胜负由可信重算给分


class ChallengeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.source = self.directory / "bot.cpp"
        self.source.write_text("trusted synthetic fixture", encoding="utf-8")
        self.output = self.directory / "run"
        self.ws = self.directory / "workspace"
        self.ws.mkdir()
        shutil.copy(ROOT / "soccer/workspace/arena.cpp", self.ws / "arena.cpp")
        self.native_calls = []

    def run_challenge(self, full=False, mutate_records=None, mutate_source=False, budget=None, output=True, here=None):
        def run_batch(entries, games, workers, extra, replay_dir=None, stderr_dir=None):
            self.native_calls.append((entries, games, workers, extra))
            result = records(challenge.schedule(full))[::-1]               # 并行完成的顺序不固定
            if mutate_records:
                mutate_records(result)
            if mutate_source:
                self.source.write_text("changed source", encoding="utf-8")
            return result

        match = types.SimpleNamespace(run_batch=run_batch, ArenaError=RuntimeError)
        exes = {label: Path(label) for label in challenge.entry_index()}
        argv = ["challenge.py", str(self.source), "--require-cpu"]
        if output:
            argv += ["--out", str(self.output)]
        if full:
            argv.append("--full")
        with contextlib.ExitStack() as stack:
            stack.enter_context(mock.patch.object(sys, "argv", argv))
            stack.enter_context(mock.patch.object(challenge, "require_windows"))
            stack.enter_context(mock.patch.object(challenge, "ensure_compiler"))
            stack.enter_context(mock.patch.object(challenge, "load_tools", return_value=(None, match)))
            stack.enter_context(mock.patch.object(challenge, "prepare", return_value=exes))
            stack.enter_context(mock.patch.object(challenge, "WS", self.ws))
            stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
            if budget is not None:
                stack.enter_context(mock.patch.object(challenge, "budget_info", return_value=budget))
            if here is not None:
                stack.enter_context(mock.patch.object(challenge, "HERE", here))
            challenge.main()

    def test_default_formal_fixed_reference_result(self):
        self.run_challenge()
        doc = json.loads((self.output / "result.json").read_text(encoding="utf-8"))
        self.assertEqual((doc["schema"], doc["complete"], doc["mode"], doc["rating_method"]), (1, True, "formal", "fixed-individual"))
        self.assertEqual(doc["games"], 120)
        self.assertEqual(doc["budget"], BUDGET)
        self.assertEqual(doc["source_sha256"], hashlib.sha256(self.source.read_bytes()).hexdigest())
        self.assertFalse(doc["full"])
        self.assertEqual(len(self.native_calls), 1)
        entries, games, _, extra = self.native_calls[0]
        self.assertEqual(len(entries), 31)
        self.assertEqual(len(games), 120)
        self.assertEqual(extra, ["--step-ms", "10", "--first-ms", "1000", "--max-timeouts", "30", "--sandbox", "none"])
        self.assertTrue(all(g[2] == 0 and g[3].count(0) == 2 for g in games))
        challenge.validate_result(doc, ROOT)
        report = (self.output / "report.md").read_text(encoding="utf-8")
        for text in ("固定", "不占名次", "不是本次重新测出"):
            self.assertIn(text, report)

    def test_full_is_four_times_and_keeps_budget(self):
        self.run_challenge(full=True)
        doc = json.loads((self.output / "result.json").read_text(encoding="utf-8"))
        self.assertEqual(doc["games"], 480)
        self.assertTrue(doc["full"])
        self.assertEqual(doc["budget"], BUDGET)
        challenge.validate_result(doc, ROOT)

    def test_incomplete_or_malformed_matches_do_not_produce_result(self):
        def player(key, value, game=0, seat=0):
            return lambda r: sorted(r, key=lambda x: x["id"])[game]["players"][seat].update({key: value})

        def game(key, value):
            return lambda r: next(x for x in r if x["id"] == 0).update({key: value})
        mutations = [lambda r: r.pop(), game("id", 999), game("seed", 1), game("first_kick", 1), game("steps", 600),
                     game("score", [1]), game("score", [-1, 0]), game("score", [True, 0]),
                     lambda r: next(x for x in r if x["id"] == 0)["players"].pop(),
                     lambda r: next(x for x in r if x["id"] == 0)["players"].reverse(),
                     player("status", "start_fail"), player("status", "weird"), player("timeouts", -1),
                     player("invalid", True), player("dead_step", -2), player("first_ms", float("nan")),
                     player("max_ms", float("inf"))]
        for mutation in mutations:
            with self.subTest(mutation=mutation), self.assertRaises(SystemExit):
                self.run_challenge(mutate_records=mutation)
            self.assertFalse((self.output / "result.json").exists())
            self.assertFalse((self.output / "report.md").exists())

    def test_source_change_does_not_produce_result(self):
        with self.assertRaises(SystemExit):
            self.run_challenge(mutate_source=True)
        self.assertFalse((self.output / "result.json").exists())

    def test_existing_result_or_report_is_not_silently_reused(self):
        self.output.mkdir()
        for name in ("result.json", "report.md"):
            path = self.output / name
            path.write_text("old result", encoding="utf-8")
            with self.subTest(name=name), self.assertRaises(SystemExit):
                self.run_challenge()
            self.assertEqual(path.read_text(encoding="utf-8"), "old result")
            path.unlink()
        self.assertEqual(self.native_calls, [])

    def test_cpu_gate_fails_before_matches(self):
        for budget in ({**BUDGET, "budget_kind": "wall"}, {**BUDGET, "step_ms": 500}, {**BUDGET, "max_timeouts": 0}):
            with self.subTest(budget=budget), self.assertRaises(SystemExit):
                self.run_challenge(budget=budget)
        self.assertEqual(self.native_calls, [])
        self.assertFalse((self.output / "result.json").exists())

    def test_budget_info_reads_official_limits(self):
        self.assertEqual(challenge.budget_info(ROOT / "soccer/workspace"), BUDGET)
        (self.ws / "arena.cpp").write_text("missing limits", encoding="utf-8")
        with self.assertRaises(SystemExit):
            challenge.budget_info(self.ws)

    def test_default_runs_use_unique_directories(self):
        here = self.directory / "soccer"
        (here / "results/individual").mkdir(parents=True)
        shutil.copy(ROOT / "soccer/results/individual/results.json.gz", here / "results/individual/results.json.gz")
        self.run_challenge(output=False, here=here)
        self.run_challenge(output=False, here=here)
        self.assertEqual(len(list((here / "challenge-runs").glob("*/result.json"))), 2)

    @unittest.skipIf(sys.platform == "win32", "non-Windows gate")
    def test_non_windows_explicitly_rejected(self):
        with self.assertRaisesRegex(SystemExit, "Windows"):
            challenge.require_windows()


if __name__ == "__main__":
    unittest.main()
