"""Episode-specific regression tests; the vendored referee is unmodified."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from arena import ROOT, LINEUP, adjudicate, frozen_strategy
from rmuc_arena.engine import Match
from rmuc_arena.models import mirrored_lineups
from rmuc_arena.submissions import load_submission
from replay import write_viewer
from tournament import standings


class EpisodeTests(unittest.TestCase):
    def test_frozen_source_provenance_and_sdk_match(self):
        source = json.loads((ROOT / "SOURCE.json").read_text())
        for entry in source["files"]:
            self.assertEqual(hashlib.sha256((ROOT / "runtime" / entry["path"]).read_bytes()).hexdigest(), entry["sha256"])
        self.assertEqual((ROOT / "workspace/sdk.py").read_bytes(), (ROOT / "runtime/rmuc_arena/sdk.py").read_bytes())

    def test_actual_engine_has_symmetric_serial_leg_roster(self):
        with Match(duration=1, map_name="rmuc2026", lineups=mirrored_lineups(LINEUP, "2026")) as engine:
            robots = engine.snapshot()["robots"]
            for side in ("red", "blue"):
                actual = {str(r["slot"]): r["model_slot"] for r in robots if r["team"] == side}
                self.assertEqual(actual, LINEUP)
                for slot in ("3", "4", "7"):
                    self.assertEqual(actual[slot], 305)

    def test_snapshot_detects_source_tampering(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "submission"
            frozen_strategy(ROOT / "workspace/strategy.py", output)
            with (output / "strategy.py").open("a") as stream:
                stream.write("\n# changed after freeze\n")
            with self.assertRaisesRegex(ValueError, "changed"):
                load_submission(output)

    def test_faults_remain_scored_and_recoverable_timeout_does_not_forfeit(self):
        self.assertEqual(adjudicate("red", [{"team": "red", "disabled": False}]), ("red", "engine"))
        self.assertEqual(adjudicate("red", [{"team": "red", "disabled": True}]), ("blue", "policy_forfeit"))
        self.assertEqual(adjudicate("red", [{"team": s, "disabled": True} for s in ("red", "blue")]), (None, "double_policy_forfeit"))
        games = [{"winner": "blue", "strategies": {"red": {"name": "a"}, "blue": {"name": "b"}}},
                 {"winner": None, "strategies": {"red": {"name": "b"}, "blue": {"name": "a"}}}]
        table = standings(games, ["a", "b"])
        self.assertEqual([(r["name"], r["games"], r["points"]) for r in table], [("b", 2, 1.5), ("a", 2, .5)])

    def test_replay_escapes_player_controlled_html(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "replay.html"
            write_viewer(path, {"name": "</script><script>alert(1)</script>"})
            html = path.read_text()
            self.assertNotIn("</script><script>alert(1)", html)
            self.assertIn(r"\u003c/script>", html)


if __name__ == "__main__":
    unittest.main()
