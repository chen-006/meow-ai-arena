"""Round-robin tournament: freeze all sources, then play each seed on both sides."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import shutil
import unicodedata
from arena import play, frozen_strategy, check_environment, CONFIG, LINEUP, SOURCE
from replay import write_viewer
from rmuc_arena.worker import SOURCE_LIMIT
from build import require_host_python


def standings(games, names):
    rows = {name: {"name": name, "games": 0, "points": 0, "wins": 0, "draws": 0, "losses": 0} for name in names}
    for game in games:
        for side in ("red", "blue"):
            row = rows[game["strategies"][side]["name"]]
            row["games"] += 1
            result = "draws" if game["winner"] is None else "wins" if game["winner"] == side else "losses"
            row[result] += 1
            row["points"] += {"wins": 1, "draws": .5, "losses": 0}[result]
    # Equal scores share a rank; alphabetical order only stabilizes display.
    output = sorted(rows.values(), key=lambda r: (-r["points"], r["name"]))
    for i, row in enumerate(output):
        row["rank"] = i + 1 if i == 0 or row["points"] != output[i - 1]["points"] else output[i - 1]["rank"]
    return output


def validation_forfeit(red, blue, seed, errors, source_hashes, fingerprints):
    """Adjudicate a scheduled source-validation failure without starting a simulation."""
    strategies = {side: {"name": path.stem, "sha256": source_hashes[path.stem]}
                  for side, path in (("red", red), ("blue", blue))}
    failed = {side: errors[path.stem] for side, path in (("red", red), ("blue", blue))
              if path.stem in errors}
    winner = None if len(failed) == 2 else ("blue" if "red" in failed else "red")
    return {"winner": winner, "engine_winner": None, "reason": "startup_forfeit",
            "adjudication": "startup_forfeit", "simulation_started": False, "time": 0,
            "policy_errors": len(failed), "diagnostics": [
                {"team": side, "disabled": True, "time": 0, "stage": "source_validation", **error}
                for side, error in failed.items()],
            "strategies": strategies, "seed": seed, "season": "2026", "lineup": LINEUP,
            "ranked": True, "duration_s": CONFIG["duration_s"], "backend": "docker",
            "source_commit": SOURCE["commit"], "runtime_revision": SOURCE["runtime_revision"],
            **fingerprints}


def main():
    require_host_python()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("strategies", nargs="+", type=Path)
    parser.add_argument("--seeds", nargs="+", type=int, default=[11, 23])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    names = [p.stem for p in args.strategies]
    # Use one conservative naming rule on every host, before creating any output.
    # macOS can normalize Unicode/case; Windows also strips directory suffixes.
    portable_names = [unicodedata.normalize("NFC", name).casefold().rstrip(" .") for name in names]
    if len(names) < 2 or not all(portable_names) or len(set(portable_names)) != len(names):
        parser.error("at least two distinct team names required after Unicode normalization, case folding and trimming trailing spaces/dots")
    if not args.seeds or len(set(args.seeds)) != len(args.seeds):
        parser.error("distinct seeds required")
    if args.output.exists():
        parser.error("output exists; never overwrite/retry a scored technical loss")
    source_dir = args.output / "sources"
    source_dir.mkdir(parents=True)
    frozen, validation_errors = [], {}
    for source in args.strategies:
        if source.is_symlink() or source.suffix != ".py" or not source.is_file():
            parser.error("sources must be regular .py files")
        if source.stat().st_size > SOURCE_LIMIT:
            parser.error(f"sources must not exceed {SOURCE_LIMIT // 1024} KiB")
        # Snapshot bytes before compiling, including invalid entries. All scheduled
        # games refer to these exact sources, never a later edit to an input file.
        target = source_dir / source.name
        shutil.copyfile(source, target)
        try:
            frozen_strategy(target, args.output / "manifests" / source.stem)
        except SyntaxError as exc:
            validation_errors[source.stem] = {
                "type": "SyntaxError", "message": exc.msg, "line": exc.lineno, "offset": exc.offset}
        frozen.append(target)
    config = {"format": "meow-rmuc2026-tournament-v1", "seeds": args.seeds,
              "source_hashes": {p.stem: hashlib.sha256(p.read_bytes()).hexdigest() for p in frozen},
              "validation_errors": validation_errors,
              "score": {"win": 1, "draw": .5, "loss": 0}, "duration_s": 420,
              "note": "Example or user-supplied teams; not official model tournament results."}
    (args.output / "run.json").write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
    # Known host faults still abort before any source forfeits can be scored.
    fingerprints = check_environment(False) if validation_errors else {}
    games = []
    for a, b in itertools.combinations(frozen, 2):
        for seed in args.seeds:
            for red, blue in ((a, b), (b, a)):
                index = len(games)
                replay = f"{index:04d}.html"
                if red.stem in validation_errors or blue.stem in validation_errors:
                    result = validation_forfeit(red, blue, seed, validation_errors,
                                                config["source_hashes"], fingerprints)
                    write_viewer(args.output / replay, {"result": result, "arena": {}, "frames": []})
                else:
                    result = play(red, blue, seed=seed, replay_path=args.output / replay)
                result.update(index=index, replay=replay)
                games.append(result)
                with (args.output / "games.jsonl").open("a", encoding="utf-8") as stream:
                    stream.write(json.dumps(result, ensure_ascii=False) + "\n")
                print(f'{index+1}: {red.stem} vs {blue.stem} seed={seed} winner={result["winner"]}', flush=True)
    table = standings(games, names)
    (args.output / "standings.json").write_text(json.dumps(table, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(table, ensure_ascii=False))


if __name__ == "__main__":
    main()
