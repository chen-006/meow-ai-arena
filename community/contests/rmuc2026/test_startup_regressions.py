"""Ready policy processes are reused; old hosts never validate contestant syntax."""
from contextlib import ExitStack
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import arena
import build
import challenge
import make_player_pack
import tournament
import rmuc_arena.runner as runner
from rmuc_arena.runner import Policy, PolicyRuntimeError, PolicyStartupError


PYTHON312_SOURCE = "type State = dict[str, int]\ndef act(obs, state):\n    return {}, state\n"


class StartupRegressions(unittest.TestCase):
    def assert_closed(self, policies):
        for policy in policies:
            self.assertTrue(policy.disabled)
            self.assertIsNotNone(policy.process.poll())
            policy.input_writer.join(timeout=2)
            self.assertFalse(policy.input_writer.is_alive())

    def test_probed_instances_are_the_battle_instances_and_never_reimported(self):
        created, used = [], []
        battle_type = runner.Battle

        def start(*args, **kwargs):
            if len(created) >= 2:
                raise PolicyStartupError("a second import would fail")
            policy = Policy(*args, **kwargs)
            created.append(policy)
            return policy

        def battle(*args, **kwargs):
            instance = battle_type(*args, **kwargs)
            used.extend(policy for _, _, policy in instance.policies)
            return instance

        with mock.patch.object(arena, "Policy", side_effect=start), \
             mock.patch.object(runner, "Policy", side_effect=start), \
             mock.patch.object(arena, "Battle", side_effect=battle):
            result = arena.play(arena.ROOT / "bots/assault.py", arena.ROOT / "bots/guard.py",
                                seed=11, duration=1, trusted_local=True)
        self.assertEqual(result["policy_errors"], 0)
        self.assertEqual(len(created), 2)
        self.assertEqual(used, created)
        self.assertTrue(all(policy.calls > 0 for policy in created))
        self.assert_closed(created)

    def test_opponent_startup_failure_is_scored_and_ready_process_is_cleaned(self):
        created = []

        def start(path, *args, **kwargs):
            if path.parent.name == "blue":
                raise PolicyStartupError("intentional startup failure")
            policy = Policy(path, *args, **kwargs)
            created.append(policy)
            return policy

        with mock.patch.object(arena, "Policy", side_effect=start), \
             mock.patch.object(arena, "Battle", side_effect=AssertionError("failed team must not enter Battle")):
            result = arena.play(arena.ROOT / "bots/assault.py", arena.ROOT / "bots/guard.py",
                                duration=1, trusted_local=True)
        self.assertEqual((result["winner"], result["adjudication"]), ("red", "startup_forfeit"))
        self.assertEqual(len(created), 1)
        self.assert_closed(created)

    def test_host_launch_failure_propagates_and_cleans_ready_process(self):
        created = []

        def start(path, *args, **kwargs):
            if path.parent.name == "blue":
                raise PolicyRuntimeError("host launch failure")
            policy = Policy(path, *args, **kwargs)
            created.append(policy)
            return policy

        with mock.patch.object(arena, "Policy", side_effect=start):
            with self.assertRaisesRegex(PolicyRuntimeError, "host launch failure"):
                arena.play(arena.ROOT / "bots/assault.py", arena.ROOT / "bots/guard.py",
                           duration=1, trusted_local=True)
        self.assertEqual(len(created), 1)
        self.assert_closed(created)

    def test_battle_construction_failure_cleans_both_ready_processes(self):
        created = []

        def start(*args, **kwargs):
            policy = Policy(*args, **kwargs)
            created.append(policy)
            return policy

        with mock.patch.object(arena, "Policy", side_effect=start), \
             mock.patch.object(arena, "Battle", side_effect=RuntimeError("host battle failure")):
            with self.assertRaisesRegex(RuntimeError, "host battle failure"):
                arena.play(arena.ROOT / "bots/assault.py", arena.ROOT / "bots/guard.py",
                           duration=1, trusted_local=True)
        self.assertEqual(len(created), 2)
        self.assert_closed(created)

    def test_ready_policy_with_wrong_seed_is_not_reused(self):
        with tempfile.TemporaryDirectory() as temp, ExitStack() as cleanup:
            root = Path(temp)
            paths = {side: root / side for side in ("red", "blue")}
            ready = {}
            for side, seed in (("red", 1100), ("blue", 1111)):
                arena.frozen_strategy(arena.ROOT / "workspace/strategy.py", paths[side])
                ready[side] = Policy(paths[side] / "strategy.py", seed, sandbox="trusted-local")
                cleanup.callback(ready[side].close)
            with self.assertRaisesRegex(ValueError, "prepared policy"):
                runner.Battle(paths["red"], paths["blue"], seed=11, duration=1, sandbox="trusted-local",
                              map_name="rmuc2026", prepared_policies=ready, record=False)

    def test_old_host_fails_before_frozen_submission_is_created(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "python312.py"
            source.write_text(PYTHON312_SOURCE, encoding="utf-8")
            with mock.patch.object(sys, "version_info", (3, 11, 9)):
                with self.assertRaisesRegex(RuntimeError, "Python 3.12"):
                    arena.frozen_strategy(source, root / "submission")
            self.assertFalse((root / "submission").exists())

    def test_all_episode_entrypoints_reject_old_host_before_creating_outputs(self):
        for module in (arena, build, challenge, tournament, make_player_pack):
            with self.subTest(entrypoint=module.__name__), \
                 mock.patch.object(sys, "version_info", (3, 11, 9)), \
                 mock.patch.object(sys, "argv", [module.__name__, "--help"]):
                with self.assertRaisesRegex(RuntimeError, "Python 3.12"):
                    module.main()

    def test_old_host_cannot_record_python312_source_as_a_syntax_forfeit(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            sources = [root / "one.py", root / "two.py"]
            for source in sources:
                source.write_text(PYTHON312_SOURCE, encoding="utf-8")
            with mock.patch.object(sys, "version_info", (3, 11, 9)), \
                 mock.patch.object(sys, "argv", ["tournament.py", *map(str, sources), "--output", str(root / "run")]):
                with self.assertRaisesRegex(RuntimeError, "Python 3.12"):
                    tournament.main()
            self.assertFalse((root / "run").exists())

    def test_python312_type_alias_survives_host_freezing(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "python312.py"
            source.write_text(PYTHON312_SOURCE, encoding="utf-8")
            manifest = arena.frozen_strategy(source, root / "submission")
            self.assertEqual(manifest["hashes"]["strategy.py"], hashlib.sha256(PYTHON312_SOURCE.encode()).hexdigest())
            self.assertEqual((root / "submission/strategy.py").read_text(), PYTHON312_SOURCE)


@unittest.skipUnless(os.environ.get("RMUC_DOCKER_TESTS") == "1", "set RMUC_DOCKER_TESTS=1 after building the sandbox")
class DockerStartupRegressions(unittest.TestCase):
    def test_python312_type_alias_runs_with_only_one_launch_per_team(self):
        popen = subprocess.Popen
        launches = []

        def start(command, *args, **kwargs):
            if command[:2] == ["docker", "run"]:
                launches.append(command)
                if len(launches) > 2:
                    self.fail("a policy was launched a second time")
            return popen(command, *args, **kwargs)

        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "python312.py"
            source.write_text(PYTHON312_SOURCE, encoding="utf-8")
            with mock.patch.object(runner.subprocess, "Popen", side_effect=start):
                result = arena.play(source, source, duration=1)
        self.assertEqual(len(launches), 2)
        self.assertEqual((result["policy_errors"], result["time"]), (0, 1))


if __name__ == "__main__":
    unittest.main()
