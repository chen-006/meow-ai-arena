"""Regression cases from PR #39: stalled stdin, failed runtime launch, output collisions."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

import arena
from arena import ROOT, play, validate_output_paths
from build import IMAGE
import rmuc_arena.runner as runner
from rmuc_arena.runner import EncodedObservation, Policy, PolicyRuntimeError, PolicyStartupError


NON_READER = '''import sys, time
class Unread:
    def __iter__(self):
        while True:
            time.sleep(60)
        yield ""
sys.stdin = Unread()
def act(obs, state):
    return {}, state
'''


def stalled_input_case(backend):
    # Run in a separately timed process: a regression must fail CI, not hang unittest.
    with tempfile.TemporaryDirectory() as temp:
        source = Path(temp) / "nonreader.py"
        source.write_text(NON_READER, encoding="utf-8")
        policy = Policy(source, 42, sandbox=backend, timeout=.2)
        try:
            payload = b'{"controlled_ids":["1"],"padding":"' + b'x' * (2 * 1024 * 1024) + b'"}\n'
            started = time.monotonic()
            actions, error = policy.decide(EncodedObservation(payload, ["1"], False))
            elapsed = time.monotonic() - started
            assert actions == {} and error == "policy input exceeded decision deadline", (actions, error)
            assert policy.disabled and policy.process.poll() is not None
            policy.input_writer.join(timeout=2)
            assert not policy.input_writer.is_alive(), "input writer survived policy shutdown"
            # The .2 s input window is followed by bounded process/container cleanup.
            assert .15 <= elapsed < 5, elapsed
            print(json.dumps({"backend": backend, "error": error, "elapsed": elapsed}), flush=True)
        finally:
            policy.close()


class TransportRegressions(unittest.TestCase):
    def test_nonreading_policy_input_has_bounded_deadline_and_cleanup(self):
        result = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--stall-input", "trusted-local"],
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("policy input exceeded decision deadline", result.stdout)

    def test_nonreader_cannot_hang_headless_battle(self):
        result = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--stall-battle"],
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("policy_forfeit", result.stdout)

    def test_import_error_after_handshake_is_a_contestant_fault(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "bad.py"
            source.write_text('raise RuntimeError("intentional import failure")\n', encoding="utf-8")
            with self.assertRaises(PolicyStartupError):
                Policy(source, 1, sandbox="trusted-local")

    def test_contestant_import_error_is_scored(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "bad.py"
            source.write_text('raise RuntimeError("intentional import failure")\n', encoding="utf-8")
            result = play(source, ROOT / "bots/guard.py", duration=1, trusted_local=True)
            self.assertEqual((result["winner"], result["adjudication"]), ("blue", "startup_forfeit"))

    def test_missing_worker_handshake_aborts_even_when_image_and_engine_checks_pass(self):
        popen = subprocess.Popen

        def rejected_launch(command, *args, **kwargs):
            if command[0] == "docker":
                command = [sys.executable, "-c", "import sys; sys.stderr.write('runtime rejected launch'); sys.exit(125)"]
            return popen(command, *args, **kwargs)

        with tempfile.TemporaryDirectory() as temp:
            replay = Path(temp) / "must-not-exist.html"
            with mock.patch.object(arena, "check_environment", return_value={"engine_hash": "ok", "rules_hash": "ok"}), \
                 mock.patch.object(runner.subprocess, "Popen", side_effect=rejected_launch):
                with self.assertRaises(PolicyRuntimeError):
                    play(ROOT / "bots/assault.py", ROOT / "bots/guard.py", replay_path=replay)
            self.assertFalse(replay.exists())

    def test_cli_rejects_equal_and_nested_outputs_before_running(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            pairs = [(root / "same", root / "same"),
                     (root / "result", root / "result/replay.html"),
                     (root / "replay/result.json", root / "replay"),
                     (root / "x/../same", root / "same")]
            for output, replay in pairs:
                with self.subTest(output=output, replay=replay):
                    result = subprocess.run([sys.executable, str(ROOT / "arena.py"), "--output", str(output),
                                             "--replay", str(replay)], capture_output=True, text=True, timeout=5)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertIn("no ancestor/descendant relationship", result.stderr)
            self.assertEqual(list(root.iterdir()), [])

    def test_output_alias_through_symlink_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            target, alias = root / "real", root / "alias"
            target.mkdir()
            try:
                alias.symlink_to(target, target_is_directory=True)
            except OSError:
                self.skipTest("symlinks unavailable")
            with self.assertRaisesRegex(ValueError, "distinct files"):
                validate_output_paths(target / "same", alias / "same")

    def test_distinct_outputs_with_shared_directory_are_allowed(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            self.assertEqual(validate_output_paths(root / "result.json", root / "replay.html"),
                             (root / "result.json", root / "replay.html"))

    def test_existing_file_cannot_be_an_output_parent(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "file").touch()
            with self.assertRaisesRegex(ValueError, "not a directory"):
                validate_output_paths(root / "file/result.json", root / "replay.html")


@unittest.skipUnless(os.environ.get("RMUC_DOCKER_TESTS") == "1", "set RMUC_DOCKER_TESTS=1 after building the sandbox")
class DockerRegressions(unittest.TestCase):
    def test_nonreading_policy_cannot_block_docker_transport(self):
        result = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--stall-input", "docker"],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rejected_entrypoint_is_host_failure_not_double_forfeit(self):
        popen = subprocess.Popen

        def broken_entrypoint(command, *args, **kwargs):
            if command[:2] == ["docker", "run"]:
                index = command.index(IMAGE)
                command = command[:index] + ["--entrypoint", "/missing-rmuc-runtime"] + command[index:]
            return popen(command, *args, **kwargs)

        with tempfile.TemporaryDirectory() as temp, \
             mock.patch.object(runner.subprocess, "Popen", side_effect=broken_entrypoint):
            replay = Path(temp) / "must-not-exist.html"
            with self.assertRaises(PolicyRuntimeError):
                play(ROOT / "bots/assault.py", ROOT / "bots/guard.py", replay_path=replay)
            self.assertFalse(replay.exists())

    def test_normal_docker_match_still_runs(self):
        result = play(ROOT / "bots/assault.py", ROOT / "bots/guard.py", duration=2)
        self.assertEqual(result["policy_errors"], 0)
        self.assertFalse(result["ranked"])
        self.assertEqual(result["time"], 2)


def stalled_battle_case():
    with tempfile.TemporaryDirectory() as temp:
        source = Path(temp) / "nonreader.py"
        source.write_text(NON_READER, encoding="utf-8")
        result = play(source, ROOT / "bots/guard.py", duration=2, trusted_local=True)
        assert result["winner"] == "blue" and result["adjudication"] == "policy_forfeit", result
        print(json.dumps({"winner": result["winner"], "adjudication": result["adjudication"]}), flush=True)


if __name__ == "__main__":
    if sys.argv[1:2] == ["--stall-input"]:
        stalled_input_case(sys.argv[2])
    elif sys.argv[1:] == ["--stall-battle"]:
        stalled_battle_case()
    else:
        unittest.main()
