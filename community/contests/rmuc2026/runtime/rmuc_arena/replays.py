"""Streaming replay export and deterministic verification."""
import json
from pathlib import Path
from .engine import Match


def write_replay(path, metadata, frames, actions, diagnostics, result):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    encode = lambda value: json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode()
    with path.open("wb") as stream:
        stream.write(b'{"format":"rmuc-replay-v4","metadata":')
        stream.write(encode(metadata))
        stream.write(b',"static":'); stream.write(encode(frames.static))
        stream.write(b',"frames":[')
        for index, raw in enumerate(frames.encoded()):
            if index:
                stream.write(b",")
            stream.write(raw)
        stream.write(b'],"actions":'); stream.write(encode(actions))
        stream.write(b',"diagnostics":'); stream.write(encode(diagnostics))
        stream.write(b',"result":'); stream.write(encode(result))
        stream.write(b"}")


def verify_replay(path):
    replay = json.loads(Path(path).read_text(encoding="utf-8"))
    if replay.get("format") not in ("rmuc-replay-v1", "rmuc-replay-v2", "rmuc-replay-v3", "rmuc-replay-v4"):
        raise ValueError("Unsupported replay format")
    if replay["format"]=="rmuc-replay-v4":
        replay["frames"]=[{**replay["static"],**frame} for frame in replay["frames"]]
    meta = replay["metadata"]
    if replay["format"] == "rmuc-replay-v1":
        from .legacy_engine import Match as ReplayEngine
    else:
        ReplayEngine = Match
    options = {"replay_version":2} if replay["format"]=="rmuc-replay-v2" else {"lineups":meta["lineups"]} if meta.get("lineups") else {}
    if meta.get("battle_settings"):
        options["battle_settings"] = meta["battle_settings"]
    if meta.get("training_scenario") is not None:
        options["training_scenario"] = meta["training_scenario"]
    match = ReplayEngine(meta["seed"], meta["duration"], meta["map"], **options)
    try:
        if replay["format"] in ("rmuc-replay-v2", "rmuc-replay-v3", "rmuc-replay-v4") and meta.get("engine_hash") != match.snapshot().get("engine_hash"):
            raise ValueError("Replay was recorded with a different Rust engine build")
        if len(replay["frames"]) != len(replay["actions"]) + 1:
            raise ValueError("Frame/action count mismatch")
        # Full state verification compares every recorded frame, excluding runtime errors.
        def canonical(frame):
            frame = dict(frame)
            frame["events"] = [e for e in frame["events"] if e["type"] != "policy_error"]
            # JSON consumers may serialize 0.0 as 0; compare numeric values.
            return frame
        if not replay["actions"] and replay["result"]["reason"] == "stopped":
            match.finish(None, "stopped")
        if canonical(match.snapshot()) != canonical(replay["frames"][0]):
            raise ValueError("Initial frame mismatch")
        for i, commands in enumerate(replay["actions"], 1):
            frame = match.step(commands)
            if i == len(replay["actions"]) and replay["result"]["reason"] == "stopped":
                match.finish(None, "stopped")
                frame = match.snapshot()
            if canonical(frame) != canonical(replay["frames"][i]):
                raise ValueError(f"Replay diverges at tick {i}")
        if match.digest() != replay["result"]["digest"]:
            raise ValueError("Final digest mismatch")
        result = {"verified_ticks": len(replay["actions"]), "digest": match.digest(), "winner": match.winner}
        return result

    finally:
        if hasattr(match, "close"):
            match.close()
