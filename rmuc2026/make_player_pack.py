"""Copy a self-contained, silent episode source package; never include build/output folders."""
import argparse
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "player-pack")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or output == ROOT or ROOT.is_relative_to(output):
        parser.error("select a new output directory")
    if any(output.is_relative_to(ROOT / name) for name in ("runtime", "workspace", "bots", "results")):
        parser.error("output cannot be inside a source directory")
    output.mkdir(parents=True)
    episode = output / "rmuc2026"
    episode.mkdir()
    for name in ("arena.py", "build.py", "make_player_pack.py", "Dockerfile", ".gitignore", "replay.py", "challenge.py", "tournament.py", "test_episode.py", "test_transport_regressions.py", "test_tournament_regressions.py", "lineup.json",
                 "SOURCE.json", "LICENSE", "THIRD_PARTY_NOTICES.md", "prompt.md", "README.md", "README_en.md", "CONDITIONS.md"):
        shutil.copyfile(ROOT / name, episode / name)
    for name in ("runtime", "workspace", "bots", "results"):
        shutil.copytree(ROOT / name, episode / name,
                        ignore=shutil.ignore_patterns("target", "__pycache__", "*.pyc"))
    print(output)


if __name__ == "__main__":
    main()
