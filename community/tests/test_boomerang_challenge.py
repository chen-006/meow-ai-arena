"""回旋镖社区挑战：固定赛程、固定海选参照和"不符就拒绝"的结果核对。

这里的对局结果都是伪造的，不编译、不运行任何程序；Linux 上也能跑。原生 Windows 裁判的覆盖靠 Windows 上的实跑。
"""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("boomerang_challenge", ROOT / "boomerang" / "challenge.py")
challenge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(challenge)
sys.path.insert(0, str(ROOT / "community"))
import check  # noqa: E402
import ci_tools  # noqa: E402

SHA = "ab" * 32


def records(games, last=False):
    """伪造逐局结果：你每局都是第 1 名（last=True 时每局最后一名），其余座位依次排后面的名次。"""
    recs = []
    for i, (mode, _, seed, seats) in enumerate(games):
        n = len(seats)
        me = seats.index("me")
        others = [s for s in range(n) if s != me]
        me_rank = n if last else 1
        ranks = {me: me_rank}
        rest = [r for r in range(1, n + 1) if r != me_rank]
        for s, r in zip(others, rest):
            ranks[s] = r
        recs.append({"id": i, "seed": seed, "mode": challenge.MODES.index(mode), "steps": 300,
                     "players": [{"seat": s, "rank": ranks[s], "kills": 0, "status": "ok", "timeouts": 0, "invalid": 0,
                                  "first_ms": 1.0, "max_ms": 2.0} for s in range(n)]})
    return recs


class ScheduleTests(unittest.TestCase):
    def test_schedule_is_deterministic_and_balanced(self):
        for full, k in ((False, 1), (True, 4)):
            games = challenge.schedule(full)
            self.assertEqual(games, challenge.schedule(full))
            self.assertEqual({m: sum(g[0] == m for g in games) for m in challenge.MODES}, {"duel": 312 * k, "ffa": 104 * k})
            exp = challenge.expected_counts(games)
            self.assertEqual(set(exp["duel"].values()), {24 * k})
            self.assertEqual(set(exp["ffa"].values()), {24 * k})
            for mode, mp, seed, seats in games:
                self.assertEqual(len(seats), 2 if mode == "duel" else 4)
                self.assertEqual(len(set(seats)), len(seats))
                self.assertEqual(seats.count("me"), 1)
                self.assertTrue((ROOT / "boomerang" / mp).is_file())
                self.assertNotIn(seed, range(11001, 11020))
            ffa = [g for g in games if g[0] == "ffa"]
            for i in range(0, len(ffa), 4):
                block = ffa[i:i + 4]
                self.assertEqual({g[2] for g in block}, {block[0][2]})
                self.assertEqual(sorted(g[3].index("me") for g in block), [0, 1, 2, 3])

    def test_reference_matches_official_board(self):
        ref = challenge.reference(ROOT)
        board = json.loads((ROOT / "boomerang/results/海选成绩.json").read_text(encoding="utf-8"))["players"]
        self.assertEqual(set(ref), set(board))
        self.assertAlmostEqual(sum(r["rating"] for r in ref.values()), 0, places=6)
        for n, r in ref.items():
            self.assertEqual(r["rating"], board[n]["rating"])
            self.assertTrue((ROOT / "boomerang/bots" / challenge.OPPONENTS[n][1] / "player.cpp").is_file())

    def test_fit_calibration(self):
        self.assertAlmostEqual(challenge.fit([(100, 0, 20, 0)]), 100, places=4)
        self.assertGreater(challenge.fit([(100, 20, 0, 0)]), 100)
        self.assertLess(challenge.fit([(100, 0, 0, 20)]), 100)


class ResultTests(unittest.TestCase):
    def setUp(self):
        self.games = challenge.schedule(False)
        self.doc = challenge.make_result(self.games, records(self.games), SHA, False, ROOT)

    def test_valid_result_round_trips(self):
        field, extra = challenge.validate_result(json.loads(json.dumps(self.doc)), ROOT)
        self.assertEqual(len(field), 14)
        self.assertEqual(extra["rank"], 1)
        self.assertGreater(extra["rating"], 306)
        self.assertEqual(extra["games"], 416)

    def test_losing_everything_ranks_last(self):
        doc = challenge.make_result(self.games, records(self.games, last=True), SHA, False, ROOT)
        self.assertEqual(doc["rank"], 14)
        self.assertLess(doc["rating"], -251)

    def test_tampering_is_rejected(self):
        cases = []
        for path, value in ((("rating",), 999.0), (("rank",), 1 if self.doc["rank"] != 1 else 2), (("ratings", "duel"), 0.0),
                            (("games",), 10), (("full",), True), (("seed",), 1), (("budget", "step_ms"), 50),
                            (("source_sha256",), "not-a-hash"), (("opponents", 0, "rating"), 0.0),
                            (("opponents", 0, "tally", "duel"), [99, 0, 0]), (("mode",), "fast")):
            doc = copy.deepcopy(self.doc)
            target = doc
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = value
            cases.append(doc)
        doc = copy.deepcopy(self.doc)
        doc["opponents"].pop()
        cases.append(doc)
        for doc in cases:
            with self.assertRaises((ValueError, KeyError, TypeError)):
                challenge.validate_result(doc, ROOT)

    def test_incomplete_records_are_rejected(self):
        recs = records(self.games)
        for bad in (recs[:-1], [dict(r, seed=r["seed"] + 1) if i == 3 else r for i, r in enumerate(recs)],
                    [dict(r, players=r["players"][:1]) if i == 0 else r for i, r in enumerate(recs)]):
            with self.assertRaises(ValueError):
                challenge.make_result(self.games, bad, SHA, False, ROOT)


class CommunityWiringTests(unittest.TestCase):
    def test_episode_is_registered(self):
        self.assertIn("boomerang", check.EPISODES)
        self.assertIn("boomerang", check.WINDOWS_EPISODES)
        self.assertIn("boomerang", check.BOT_SPEC)

    def test_example_entry_validates(self):
        entry = ROOT / "community/_example/boomerang/chen-006__starter"
        meta, errors, _, files = check.validate(entry, "boomerang")
        self.assertEqual(errors, [])
        self.assertEqual(set(files), {"bot.cpp"})
        kit = check.load_module("boomerang_build_for_test", ROOT / "boomerang/workspace/build.py")
        self.assertEqual(kit.check_source((entry / "bot.cpp").read_text(encoding="utf-8")), [])

    def test_publisher_recomputes_and_rejects_forged_score(self):
        import tempfile
        entry = ROOT / "community/_example/boomerang/chen-006__starter"
        _, _, _, files = check.validate(entry, "boomerang")
        doc = challenge.make_result(self.games_doc(), records(self.games_doc()), files["bot.cpp"], False, ROOT)
        field, extra = challenge.validate_result(doc, ROOT)
        raw = {"episode": "boomerang", "entry": "chen-006__starter", "status": "pass", "mode": "quick",
               "score": round(extra["rating"], 1), "files": files, "detail": dict(extra, field=field), "errors": []}
        old = ci_tools.HERE
        try:
            ci_tools.HERE = (ROOT / "community/_example").resolve()
            with tempfile.TemporaryDirectory() as tmp:
                art = Path(tmp) / "result-boomerang-chen-006__starter"
                art.mkdir()
                (art / "result.json").write_text(json.dumps(raw), encoding="utf-8")
                rec, _ = ci_tools.collect_one("boomerang", "chen-006__starter", tmp, "sha")
                self.assertEqual(rec["status"], "pass")
                self.assertEqual(rec["detail"]["rank"], extra["rank"])
                raw["score"] = 999.0
                raw["detail"]["rating"] = 999.0
                (art / "result.json").write_text(json.dumps(raw), encoding="utf-8")
                rec, _ = ci_tools.collect_one("boomerang", "chen-006__starter", tmp, "sha")
                self.assertEqual(rec["status"], "fail")
        finally:
            ci_tools.HERE = old

    @staticmethod
    def games_doc():
        return challenge.schedule(False)


if __name__ == "__main__":
    unittest.main()
