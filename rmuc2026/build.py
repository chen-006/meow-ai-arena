"""Build the pinned Rust referee and, optionally, the policy sandbox."""
import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
IMAGE = "meow-rmuc2026-policy:e70c1f0-r2"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--docker", action="store_true", help="also build the isolated Python 3.12 runtime")
    args = parser.parse_args()
    subprocess.run(["cargo", "build", "--release", "--locked", "--manifest-path",
                    str(ROOT / "runtime/simulation/Cargo.toml")], check=True)
    if args.docker:
        subprocess.run(["docker", "build", "-t", IMAGE, "-f", str(ROOT / "Dockerfile"),
                        str(ROOT / "runtime")], check=True)


if __name__ == "__main__":
    main()
