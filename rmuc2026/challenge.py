"""Challenge both example teams with paired red/blue games; keep technical losses."""
import argparse
import json
from pathlib import Path
import shutil
from arena import ROOT, play, frozen_strategy


def score(result, candidate_side):
    return .5 if result["winner"] is None else float(result["winner"] == candidate_side)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("strategy", nargs="?", type=Path, default=ROOT / "workspace/strategy.py")
    parser.add_argument("--seeds", nargs="+", type=int, default=[11, 23])
    parser.add_argument("--trusted-local", action="store_true", help="trusted developer code only; unranked")
    parser.add_argument("--duration", type=int, default=420, help="short practice is explicitly unranked")
    parser.add_argument("--output", type=Path, default=ROOT / "practice/challenge")
    args = parser.parse_args()
    if not args.seeds or len(set(args.seeds)) != len(args.seeds):
        parser.error("provide distinct seeds")
    if args.output.exists():
        parser.error("output already exists; choose a new directory")
    args.output.mkdir(parents=True)
    snapshot = args.output / "candidate"
    frozen_strategy(args.strategy, snapshot)
    candidate = args.output / args.strategy.name
    shutil.copyfile(snapshot / "strategy.py", candidate)
    results = []
    for opponent in sorted((ROOT / "bots").glob("*.py")):
        for seed in args.seeds:
            for side in ("red", "blue"):
                index = len(results)
                result = play(candidate if side == "red" else opponent,
                              opponent if side == "red" else candidate,
                              seed=seed, duration=args.duration, trusted_local=args.trusted_local,
                              replay_path=args.output / f"{index:04d}.html")
                result.update(candidate_side=side, opponent=opponent.stem, points=score(result, side),
                              replay=f"{index:04d}.html")
                results.append(result)
                with (args.output / "games.jsonl").open("a", encoding="utf-8") as stream:
                    stream.write(json.dumps(result, ensure_ascii=False) + "\n")
                print(f'{index+1}: {opponent.stem} seed={seed} {side} points={result["points"]}', flush=True)
    summary = {"games": len(results), "points": sum(r["points"] for r in results),
               "ranked": all(r["ranked"] for r in results),
               "policy_errors": sum(r["policy_errors"] for r in results),
               "note": "Example opponents only; not results from the AI Algorithm Arena show."}
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary))


if __name__ == "__main__":
    main()
