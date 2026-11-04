"""Build the pinned Rust referee and, optionally, the policy sandbox."""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
IMAGE = "meow-rmuc2026-policy:e70c1f0-r2"


def require_host_python():
    if sys.version_info[:2] < (3, 12):
        version = ".".join(map(str, sys.version_info[:3]))
        raise RuntimeError(f"RMUC 2026 requires host Python 3.12+ before validating strategies; found {version}")


def main():
    require_host_python()
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
