"""The shared action envelope for strategy processes and external training actors."""
import json
from .communication import validate_messages
from .worker import ACTIONS_LIMIT

# Every field a strategy or a pilot may send for one robot.
ACTION_FIELDS = frozenset({
    "move", "aim", "pitch", "fire", "power", "profile", "buy_ammo", "remote_heal", "revive", "deploy", "support",
    "altitude", "pick_unit", "assembly", "assembly_insert", "assembly_translate", "assembly_rotate", "assembly_confirm",
    "rune_start", "heading", "crouch", "rescue", "yaw_rate", "yaw_boost", "brake", "leg_height", "jump", "climb", "lean", "arm", "arm2",
    "pickup_arm", "transform",
    # 2027: heavy launcher and assembly steps, sentry posture, aerial system.
    "install_launcher", "assembly_core", "assembly_lift", "assembly_flip", "assembly_rotate2", "assembly_extract",
    "assembly_cancel", "posture",
    "posture_enhanced", "drones",
    # A pilot's lobbed shot: the world point under the crosshair, solved into a high arc server-side.
    "aim_point",
})


def validate_action(action, has_messages=False):
    """Reject unknown fields and payloads larger than the 1 KiB action budget."""
    if not isinstance(action, dict):
        raise ValueError("Invalid action object")
    allowed = ACTION_FIELDS | {"messages"} if has_messages else ACTION_FIELDS
    if not set(action) <= allowed:
        raise ValueError("Action contains unsupported fields")
    if len(json.dumps(action, allow_nan=False).encode()) > 1024:
        raise ValueError("Each robot action must be <=1 KiB")
    return action


def validate_actions(actions, controlled, has_messages=True, dynamic=False):
    if not isinstance(actions, dict):
        raise ValueError('Invalid action object')
    if not set(actions) <= set(controlled):
        raise ValueError('Attempt to control an unowned robot')
    if len(json.dumps(actions, allow_nan=False, separators=(',', ':')).encode()) > ACTIONS_LIMIT:
        raise ValueError('Team actions exceed 16 KiB')
    for slot, action in actions.items():
        validate_action(action, has_messages)
        if 'messages' in action:
            validate_messages(action['messages'], slot, dynamic)
    return actions
