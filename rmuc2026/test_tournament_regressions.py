"""Round robins keep syntax-invalid entrants, with no change to healthy pairings."""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

import arena
import tournament
from rmuc_arena.runner import Policy


VALID = "def act(obs, state):\n    return {}, state\n"
INVALID = "def act(:\n    pass\n"


def invoke(strategies, output, play_match):
    args = ["tournament.py", *map(str, strategies), "--seeds", "11", "--output", str(output)]
    with mock.patch.object(sys, "argv", args), \
         mock.patch.object(tournament, "play", side_effect=play_match), \
         contextlib.redirect_stdout(io.StringIO()):
        tournament.main()
    return [json.loads(line) for line in (output / "games.jsonl").read_text().splitlines()]


def healthy_result(red, blue, *, seed, replay_path):
    return {"winner": "red" if red.stem == "alpha" else "blue", "policy_errors": 0,
            "seed": seed, "reason": "timeout", "adjudication": "engine",
            "strategies": {side: {"name": path.stem, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                           for side, path in (("red", red), ("blue", blue))}}


class TournamentRegressions(unittest.TestCase):
    def test_invalid_source_keeps_all_forfeits_and_healthy_pair_runs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            entries = [root / name for name in ("alpha.py", "bravo.py", "broken.py")]
            for path, text in zip(entries, (VALID, VALID, INVALID)):
                path.write_text(text, encoding="utf-8")
            output = root / "run"
            calls = []

            def match(red, blue, **kwargs):
                calls.append((red.stem, blue.stem))
                return healthy_result(red, blue, **kwargs)

            with mock.patch.object(tournament, "check_environment", return_value={}):
                games = invoke(entries, output, match)
            self.assertEqual(calls, [("alpha", "bravo"), ("bravo", "alpha")])
            self.assertEqual(len(games), 6)
            forfeits = [game for game in games if game["reason"] == "startup_forfeit"]
            self.assertEqual(len(forfeits), 4)
            for game in forfeits:
                self.assertIn(game["strategies"][game["winner"]]["name"], ("alpha", "bravo"))
                self.assertFalse(game["simulation_started"])
                self.assertEqual(game["policy_errors"], 1)
                self.assertEqual(game["diagnostics"][0]["stage"], "source_validation")
                self.assertTrue((output / game["replay"]).exists())
            table = {row["name"]: row for row in json.loads((output / "standings.json").read_text())}
            self.assertEqual({name: row["games"] for name, row in table.items()}, {"alpha": 4, "bravo": 4, "broken": 4})
            self.assertEqual((table["broken"]["losses"], table["broken"]["points"]), (4, 0))
            config = json.loads((output / "run.json").read_text())
            self.assertIn("broken", config["validation_errors"])
            self.assertEqual((output / "sources/broken.py").read_text(), INVALID)
            self.assertEqual(config["source_hashes"]["broken"], hashlib.sha256(INVALID.encode()).hexdigest())

    def test_two_invalid_sources_are_recorded_as_paired_double_forfeits(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            entries = [root / name for name in ("one.py", "two.py")]
            for path in entries:
                path.write_text(INVALID, encoding="utf-8")
            match = mock.Mock(side_effect=AssertionError("invalid sources must not reach play"))
            with mock.patch.object(tournament, "check_environment", return_value={}):
                games = invoke(entries, root / "run", match)
            self.assertEqual(len(games), 2)
            self.assertTrue(all(game["winner"] is None and game["policy_errors"] == 2 for game in games))
            table = json.loads((root / "run/standings.json").read_text())
            self.assertTrue(all(row["draws"] == 2 and row["points"] == 1 for row in table))

    def test_host_freeze_failure_is_not_a_player_forfeit(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            entries = [root / name for name in ("alpha.py", "bravo.py")]
            for path in entries:
                path.write_text(VALID, encoding="utf-8")
            with mock.patch.object(tournament, "frozen_strategy", side_effect=OSError("host disk failure")):
                with self.assertRaisesRegex(OSError, "host disk failure"):
                    invoke(entries, root / "run", healthy_result)
            self.assertFalse((root / "run/games.jsonl").exists())

    def test_runtime_environment_failure_does_not_score_syntax_forfeits(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            entries = [root / name for name in ("one.py", "two.py")]
            for path in entries:
                path.write_text(INVALID, encoding="utf-8")
            with mock.patch.object(tournament, "check_environment", side_effect=RuntimeError("host runtime failure")):
                with self.assertRaisesRegex(RuntimeError, "host runtime failure"):
                    invoke(entries, root / "run", healthy_result)
            self.assertFalse((root / "run/games.jsonl").exists())

    def test_exception_or_invalid_output_disables_on_first_decision(self):
        sources = ['def act(obs,state):\n    raise RuntimeError("intentional act failure")\n',
                   'def act(obs,state):\n    return {"99": {"move": [0,0]}}, state\n']
        with tempfile.TemporaryDirectory() as temp:
            for index, text in enumerate(sources):
                with self.subTest(source=index):
                    path = Path(temp) / f"fault-{index}.py"
                    path.write_text(text, encoding="utf-8")
                    policy = Policy(path, 11, sandbox="trusted-local")
                    try:
                        actions, error = policy.decide({"controlled_ids": ["1"]})
                        self.assertEqual(actions, {})
                        self.assertIsNotNone(error)
                        self.assertTrue(policy.disabled)
                        self.assertEqual(policy.calls, 1)
                    finally:
                        policy.close()

    def test_only_response_timeouts_get_three_consecutive_attempts(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "slow.py"
            path.write_text('import time\ndef act(obs,state):\n    time.sleep(2)\n    return {}, state\n', encoding="utf-8")
            policy = Policy(path, 11, sandbox="trusted-local", timeout=.2)
            try:
                for attempt in (1, 2, 3):
                    actions, error = policy.decide({"controlled_ids": ["1"]})
                    self.assertEqual((actions, error), ({}, "decision exceeded deadline"))
                    self.assertEqual(policy.consecutive_timeouts, attempt)
                    self.assertEqual(policy.disabled, attempt == 3)
            finally:
                policy.close()


@unittest.skipUnless(os.environ.get("RMUC_DOCKER_TESTS") == "1", "set RMUC_DOCKER_TESTS=1 after building the sandbox")
class DockerTournamentRegressions(unittest.TestCase):
    def test_syntax_invalid_team_does_not_prevent_healthy_docker_games(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            entries = [root / name for name in ("alpha.py", "bravo.py", "broken.py")]
            for path, text in zip(entries, (VALID, VALID, INVALID)):
                path.write_text(text, encoding="utf-8")
            calls = []

            def short_match(red, blue, **kwargs):
                result = arena.play(red, blue, duration=1, **kwargs)
                calls.append(result)
                return result

            games = invoke(entries, root / "run", short_match)
            self.assertEqual(len(games), 6)
            self.assertEqual(len(calls), 2)
            self.assertTrue(all(game["time"] == 1 and game["policy_errors"] == 0 for game in calls))
            self.assertEqual(sum(game["reason"] == "startup_forfeit" for game in games), 4)


if __name__ == "__main__":
    unittest.main()
