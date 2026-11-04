"""Organizer-owned variable lineups and adaptive single-robot strategies."""
import hashlib
import json
from pathlib import Path

from .submissions import load_submission, RULES_PROFILE
from .showcase import load_profile
from . import seasons as season_rules

def prepare_decks(decks, seed, team_strategies=None, season=season_rules.LEGACY_SEASON):
    profile_required = season_rules.rules_profile(season)
    if set(decks) != {"red", "blue"}:
        raise ValueError("Deck matches require red and blue decks")
    team_strategies = team_strategies or {}
    if not set(team_strategies) <= {"red", "blue"}:
        raise ValueError("Unknown free-battle controller team")
    lineups, policies, metadata = {}, {}, {}
    for team, cards in decks.items():
        if not isinstance(cards, list) or not cards:
            raise ValueError("Each team must contain at least one saved robot strategy")
        ids = range(1, len(cards) + 1)
        lineups[team], policies[team], versions, agents = [], [], [], []
        for slot, source in zip(ids, cards):
            manifest, entries = load_submission(source)
            if manifest["mode"] != "adaptive" or manifest["api_version"] != 3 or manifest["rules_profile"] != profile_required:
                raise ValueError(f"Free battle on the {season} field requires saved {season} adaptive single-robot strategies")
            model = manifest["robot"]
            if not season_rules.legal(model, season):
                raise ValueError(f"Model {model} does not pass the {season} construction rules")
            path = entries[str(model)]
            lineups[team].append({"slot": slot, "model": model})
            policies[team].append((slot, path, manifest["hashes"][path.name]))
            versions.append({"name": Path(source).name, "version": manifest["version"], "model": model})
            agents.append(load_profile(source))
        version = hashlib.sha256(json.dumps(versions, sort_keys=True).encode()).hexdigest()[:16]
        # Agents are display data and stay out of the version hash.
        cards = [{**card, "agent": profile["agent"], **({"display_name": profile["display_name"]} if profile.get("display_name") else {})}
                 for card, profile in zip(versions, agents)]
        controller = team_strategies.get(team)
        if controller is not None:
            manifest, entries = load_submission(controller)
            if manifest["mode"] != "team" or manifest["api_version"] != 3 or manifest["rules_profile"] != profile_required:
                raise ValueError(f"Whole-team free battle on the {season} field requires a saved {season} team strategy")
            path = entries["team"]
            policies[team] = [(None, path, manifest["hashes"][path.name])]
            profile = load_profile(controller)
            metadata[team] = {"name": Path(controller).name, "version": manifest["version"], "mode":"team",
                "controller":"team", "cards":cards, "agent":profile["agent"], **({"display_name":profile["display_name"]} if profile.get("display_name") else {})}
            continue
        metadata[team] = {"name": f"{'红' if team == 'red' else '蓝'}方卡组", "version": version,
                          "mode": "adaptive", "controller":"independent", "cards": cards}
    return lineups, policies, metadata
