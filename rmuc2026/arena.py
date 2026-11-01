"""Fixed-roster RMUC 2026 matches, using the unmodified Arena-1 referee."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent
RUNTIME = ROOT / "runtime"
os.environ["RMUC_RESOURCE_ROOT"] = str(RUNTIME)
os.environ.pop("RMUC_SIM_BINARY", None)
sys.path.insert(0, str(RUNTIME))
from rmuc_arena.engine import Match
from rmuc_arena.models import normalize_team_lineup
from rmuc_arena.runner import Battle, Policy
import rmuc_arena.runner as runner
from rmuc_arena.submissions import save_submission
from replay import write_viewer
from build import IMAGE

runner.IMAGE = IMAGE
CONFIG = json.loads((ROOT / "lineup.json").read_text(encoding="utf-8"))
LINEUP = normalize_team_lineup(CONFIG["lineup"], "2026")
SOURCE_COMMIT = json.loads((ROOT / "SOURCE.json").read_text())["commit"]


def check_environment(trusted_local):
    if not trusted_local:
        subprocess.run(["docker", "image", "inspect", IMAGE], check=True,
                       stdout=subprocess.DEVNULL)
    # Check embedded rules and the actual binary before attributing any fault to a bot.
    with Match(duration=1, map_name=CONFIG["map"]) as engine:
        return {key: engine.snapshot()[key] for key in ("engine_hash", "rules_hash")}


def frozen_strategy(source, destination):
    """Compile and hash a single source; importing/running it happens only in Policy."""
    return save_submission(destination, "team", {"team": Path(source)},
                           team_lineup=LINEUP, season="2026")


def adjudicate(winner, diagnostics):
    """Recoverable timeouts remain in-play; a disabled team forfeits, never vanishes."""
    disabled = {entry["team"] for entry in diagnostics if entry.get("disabled")}
    if len(disabled) == 2:
        return None, "double_policy_forfeit"
    if disabled:
        return ("blue" if "red" in disabled else "red"), "policy_forfeit"
    return winner, "engine"


def sample(frame):
    keys = ("slot", "team", "x", "y", "z", "hp", "max_hp", "alive", "role", "aim")
    structures = ("x", "y", "z", "hp", "max_hp", "owner", "ever_destroyed")
    return {"time": frame["time"],
            "robots": [{k: robot.get(k) for k in keys} for robot in frame["robots"]],
            **{name: {team: {key: value[key] for key in structures if key in value}
                       for team, value in frame[name].items()} for name in ("bases", "outposts")},
            "teams": {team: {key: value[key] for key in ("resources", "attack_damage", "kills")}
                      for team, value in frame["teams"].items()}}


def play(red, blue, *, seed=42, duration=420, trusted_local=False, replay_path=None):
    if isinstance(seed, bool) or not -(2**31) <= seed < 2**31:
        raise ValueError("seed must be a signed 32-bit integer")
    if not 1 <= duration <= 420:
        raise ValueError("duration must be 1..420")
    fingerprints = check_environment(trusted_local)
    backend = "trusted-local" if trusted_local else "docker"
    started = time.monotonic()
    paths = {"red": Path(red).resolve(), "blue": Path(blue).resolve()}
    identity = {}
    with tempfile.TemporaryDirectory(prefix="meow-rmuc2026-") as temp:
        submissions = {team: Path(temp) / team for team in paths}
        for team, path in paths.items():
            manifest = frozen_strategy(path, submissions[team])
            identity[team] = {"name": path.stem, "sha256": manifest["hashes"]["strategy.py"]}
        # Isolated imports are attributed to the offending side, before constructing Battle.
        startup_errors = {}
        for team in paths:
            try:
                policy = Policy(submissions[team] / "strategy.py", seed * 100 + (10 if team == "blue" else 0),
                                sandbox=backend, timeout=CONFIG["decision_timeout_s"])
                policy.close()
            except RuntimeError as exc:
                # A Docker/host failure must not be mistaken for a player loss.
                check_environment(trusted_local)
                startup_errors[team] = str(exc)[-1500:]
        frames = []
        arena = {}
        if startup_errors:
            winner = None if len(startup_errors) == 2 else ("blue" if "red" in startup_errors else "red")
            result = {"winner": winner, "engine_winner": None, "reason": "startup_forfeit", "adjudication": "startup_forfeit",
                      "policy_errors": len(startup_errors), "diagnostics": [
                          {"team": team, "disabled": True, "time": 0, "message": message}
                          for team, message in startup_errors.items()]}
        else:
            with Battle(red=submissions["red"], blue=submissions["blue"], seed=seed,
                        duration=duration, map_name=CONFIG["map"], sandbox=backend,
                        timeout=CONFIG["decision_timeout_s"], team_lineup=LINEUP,
                        record=False) as battle:
                arena = battle.match.snapshot()["arena"]
                if replay_path:
                    frames.append(sample(battle.match.snapshot()))
                while battle.match.status != "finished":
                    frame = battle.step()
                    if replay_path and (battle.match.tick % 20 == 0 or battle.match.status == "finished"):
                        frames.append(sample(frame))
                raw = battle.result()
                winner, rule = adjudicate(raw["winner"], battle.diagnostics)
                result = {**raw, "engine_winner": raw["winner"], "winner": winner,
                          "adjudication": rule, "diagnostics": battle.diagnostics}
        result.update(seed=seed, season="2026", lineup=LINEUP, strategies=identity,
                      ranked=duration == 420 and not trusted_local,
                      duration_s=duration, backend=backend, source_commit=SOURCE_COMMIT,
                      wall_seconds=round(time.monotonic() - started, 3), **fingerprints)
        if replay_path:
            write_viewer(Path(replay_path), {"result": result, "arena": arena, "frames": frames})
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--red", type=Path, default=ROOT / "workspace/strategy.py")
    parser.add_argument("--blue", type=Path, default=ROOT / "bots/guard.py")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--duration", type=int, default=420)
    parser.add_argument("--trusted-local", action="store_true", help="ONLY for your own trusted code; unranked")
    parser.add_argument("--output", type=Path, default=ROOT / "practice/match.json")
    parser.add_argument("--replay", type=Path, help="write a standalone silent HTML replay of this exact match")
    args = parser.parse_args()
    if args.output.exists() or (args.replay and args.replay.exists()):
        parser.error("output already exists; select new filenames")
    result = play(args.red, args.blue, seed=args.seed, duration=args.duration,
                  trusted_local=args.trusted_local, replay_path=args.replay)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
