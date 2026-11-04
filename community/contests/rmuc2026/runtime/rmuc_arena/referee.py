"""Validate organizer decisions separately from contestant robot actions."""
from .engine import SLOT_IDS


def validate_referee(command, robot_ids=None):
    if not isinstance(command, dict) or set(command) - {"kind", "robot", "team", "reason"}:
        raise ValueError("Invalid referee command")
    value = dict(command)
    reason = value.get("reason", "referee_decision")
    if not isinstance(reason, str) or len(reason) > 128:
        raise ValueError("Referee reason must be at most 128 characters")
    kind = value.get("kind")
    if kind in ("yellow", "red"):
        targets = set(robot_ids) if robot_ids is not None else {f"{team}-{slot}" for team in ("red", "blue") for slot in SLOT_IDS}
        if not isinstance(value.get("robot"), str) or value["robot"] not in targets or "team" in value:
            raise ValueError("A card requires one valid robot")
    elif kind == "yellow_both":
        if "robot" in value or "team" in value:
            raise ValueError("A simultaneous yellow has no individual target")
    elif kind == "forfeit":
        if value.get("team") not in ("red", "blue") or "robot" in value:
            raise ValueError("A forfeit requires one valid team")
    else:
        raise ValueError("Unknown referee decision")
    value["reason"] = reason
    return value
