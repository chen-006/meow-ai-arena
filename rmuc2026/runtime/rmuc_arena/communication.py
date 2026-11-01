"""Bounded per-robot messages for all control modes, delivered next round."""
import json
import math

from .submissions import SLOT_IDS

SLOTS = tuple(str(slot) for slot in SLOT_IDS)
MAX_MESSAGE_BYTES = 512
_BASE_LOSS_DIVISOR_FALLBACK = 20000


def message_byte_limit(base=None, rules=None):
    """Effective `messages[*].data` budget for a team at its current base HP (table 5-19).

    ``rules`` is the season's public rule summary. The 2027 season scales the 512-byte
    interaction bandwidth by the logistics coefficient λ = 1 − lost base HP / 20000; other
    seasons and malformed frames keep the full budget.
    """
    logistics = (rules or {}).get("logistics") if isinstance(rules, dict) else None
    if not isinstance(logistics, dict) or not isinstance(base, dict):
        return MAX_MESSAGE_BYTES
    cap = logistics.get("interaction_bandwidth_bytes", MAX_MESSAGE_BYTES)
    divisor = logistics.get("base_loss_divisor_hp", _BASE_LOSS_DIVISOR_FALLBACK)
    hp, max_hp = base.get("hp"), base.get("max_hp")
    if not isinstance(cap, (int, float)) or not isinstance(divisor, (int, float)) or divisor <= 0:
        return MAX_MESSAGE_BYTES
    if not isinstance(hp, (int, float)) or not isinstance(max_hp, (int, float)) or max_hp <= 0:
        return MAX_MESSAGE_BYTES
    lam = min(1.0, max(0.05, 1.0 - max(0.0, max_hp - hp) / divisor))
    return max(0, math.floor(cap * lam))


def message_size(data):
    """Encoded bytes of one message payload, matching what the wire counts."""
    return len(json.dumps(data, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode())


def limit_messages(messages, limit):
    """Messages that fit the live interaction bandwidth; the rest never reach the wire."""
    return [message for message in messages if message_size(message["data"]) <= limit]


def validate_messages(messages, sender, dynamic=False, limit=MAX_MESSAGE_BYTES):
    if not isinstance(messages, list) or len(messages) > 4:
        raise ValueError("messages must be a list of at most four messages")
    for message in messages:
        if not isinstance(message, dict) or set(message) != {"to", "data"}:
            raise ValueError("Each message must contain only to and data")
        recipient = message["to"]
        valid = isinstance(recipient, str) and ((recipient.isascii() and recipient.isdigit() and 0 < int(recipient) <= 2**32-1) if dynamic else recipient in SLOTS)
        if recipient != "all" and (not valid or recipient == sender):
            raise ValueError("Message recipient must be all or another own robot slot")
        if message_size(message["data"]) > limit:
            raise ValueError(f"Message data must be <={limit} bytes")


class TeamCommunication:
    def __init__(self, rosters=None, slots=SLOTS):
        self.slots = tuple(str(slot) for slot in slots)
        self.rosters = rosters or {team: self.slots for team in ("red", "blue")}
        self.inboxes = self.empty_inboxes()

    def empty_inboxes(self):
        return {team: {str(slot): [] for slot in slots} for team, slots in self.rosters.items()}

    def route(self, outgoing, tick, delivery_tick):
        """Replace consumed inboxes atomically, in fixed team/slot/message order."""
        next_inboxes, records = self.empty_inboxes(), []
        for team, sender, messages in sorted(outgoing,key=lambda entry:(0 if entry[0]=="red" else 1,int(entry[1]))):
            for message in messages:
                recipients = [str(slot) for slot in self.rosters[team] if str(slot) != sender] if message["to"] == "all" else [message["to"]] if message["to"] in next_inboxes[team] else []
                common = {"from": f"{team}-{sender}", "tick": tick,
                          "delivery_tick": delivery_tick, "data": message["data"]}
                for recipient in recipients:
                    next_inboxes[team][recipient].append({**common, "to": f"{team}-{recipient}"})
                records.append({**common, "to": [f"{team}-{recipient}" for recipient in recipients]})
        self.inboxes = next_inboxes
        return records
