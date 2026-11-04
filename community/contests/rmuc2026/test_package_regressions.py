"""Community contest packaging, portable team names and documented CLI contracts."""
import contextlib
import io
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from arena import ROOT
import make_player_pack
import tournament


class PackageRegressions(unittest.TestCase):
    def assert_names_rejected(self, first, second):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            entries = []
            for directory, name in (("one", first), ("two", second)):
                parent = root / directory
                parent.mkdir()
                source = parent / name
                source.write_text("def act(obs,state):\n    return {},state\n", encoding="utf-8")
                entries.append(source)
            output = root / "results"
            stderr = io.StringIO()
            with mock.patch.object(sys, "argv", ["tournament.py", *map(str, entries), "--output", str(output)]), \
                 mock.patch.object(tournament, "play", side_effect=AssertionError("colliding names reached the game loop")), \
                 contextlib.redirect_stderr(stderr):
                with self.assertRaises(SystemExit) as failure:
                    tournament.main()
            self.assertEqual(failure.exception.code, 2)
            self.assertIn("distinct", stderr.getvalue())
            self.assertFalse(output.exists())

    def test_case_equivalent_team_names_rejected_before_output_creation(self):
        self.assert_names_rejected("GPT.py", "gpt.py")

    def test_unicode_equivalent_team_names_rejected_before_output_creation(self):
        self.assert_names_rejected("Caf\u00e9.py", "Cafe\u0301.py")

    def test_windows_directory_aliases_rejected_before_output_creation(self):
        for name in ("GPT .py", "GPT..py"):
            with self.subTest(name=name):
                self.assert_names_rejected("GPT.py", name)

    def test_mechanics_shell_examples_match_the_shipped_cli(self):
        result = subprocess.run([sys.executable, str(ROOT / "arena.py"), "--help"],
                                capture_output=True, text=True, check=True, timeout=5)
        options = set(re.findall(r"--[a-z][a-z-]*", result.stdout))
        guide = (ROOT / "workspace/MECHANICS.md").read_text(encoding="utf-8")
        blocks = re.findall(r"```(?:sh|bash)\n(.*?)```", guide, re.S)
        checked = 0
        for block in blocks:
            for line in block.splitlines():
                words = shlex.split(line, comments=True)
                if "arena.py" not in words:
                    continue
                args = words[words.index("arena.py") + 1:]
                self.assertTrue(args and args[0].startswith("--"), line)
                for arg in args:
                    if arg.startswith("--"):
                        self.assertIn(arg.split("=")[0], options, line)
                checked += 1
        self.assertGreater(checked, 0)
        self.assertNotIn("arena.json", guide)
        self.assertNotIn("--swap-sides", guide)

    def test_exported_player_pack_is_a_standalone_contest_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "player-pack"
            with mock.patch.object(sys, "argv", ["make_player_pack.py", "--output", str(output)]), \
                 contextlib.redirect_stdout(io.StringIO()):
                make_player_pack.main()
            for path in ("arena.py", "build.py", "workspace/RULES.md", "runtime/config/rules.json", Path(__file__).name):
                self.assertTrue((output / path).is_file(), path)
            self.assertFalse((output / "rmuc2026").exists())
            self.assertFalse((output / "runtime/simulation/target").exists())
            result = subprocess.run([sys.executable, "arena.py", "--help"], cwd=output,
                                    capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("--replay", result.stdout)


if __name__ == "__main__":
    unittest.main()
