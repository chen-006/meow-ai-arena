"""Strategy presentation sidecars: who generated a strategy (profile.json) and how it plays (showcase.json).

Both files sit next to a submission's manifest. They are display metadata only, so they are not part of the
content-addressed version: the code alone decides a strategy's identity and Elo.
"""
from .strategy_sources import bundled_examples
import json
import math
import re
from pathlib import Path

SHOWCASE_FILE = "showcase.json"
PROFILE_FILE = "profile.json"
SHOWCASE_FORMAT = "rmuc-showcase-v1"
PROFILE_FORMAT = "rmuc-strategy-profile-v1"
SHOWCASE_LIMIT = 64 * 1024
FIELD = (28.0, 15.0)
from .paths import ROOT
from .strategy_names import CLAUDE_AGENT, CLAUDE_PREFIX, LEGACY_CATEGORY, canonical_key

EXAMPLES = bundled_examples()

PROVIDERS = ("openai", "anthropic", "google", "meta", "spacexai", "moonshot", "zai", "xiaomi", "deepseek", "qwen", "tencent-hy", "bytedance-seed")
# Bundling a strategy is independent of who developed it.
AUTHORS = PROVIDERS + ("human", "unknown")


def example_agent(season):
    """Development attribution supplied by the project owner for both seasons."""
    return dict(CLAUDE_AGENT)
STYLE_KEYS = ("aggression", "defense", "economy", "objective", "teamwork", "adaptability")
PHASE_FOCUS = ("attack", "defense", "economy", "objective", "support")
TACTICS = ("balanced", "assault", "siege", "guard", "economy")
TAGS = ("rune", "assembly", "rescue", "aerial", "sniping", "mobility", "comms", "focus_fire", "outpost_rush", "base_rush")
# The bundled examples and the online library (github.com/RM-Arena/Arena-Strategies) are uploaded by the
# project's own account; a strategy someone else contributes names its uploader in profile.json.
LIBRARY_UPLOADER = "RM-Arena"
_GITHUB_LOGIN = re.compile(r"[A-Za-z0-9](?:[A-Za-z0-9]|-(?=[A-Za-z0-9])){0,38}")

# Ordered: the first matching provider wins, so specific product names come before generic company words.
_DETECT = (
    ("anthropic", r"claude|anthropic|opus|sonnet|haiku|fable"),
    ("openai", r"openai|chatgpt|gpt|codex|\bo[1-9](?:-|\b)|davinci"),
    ("google", r"gemini|gemma|google|bard|palm|deepmind"),
    ("meta", r"llama|meta\b|meta-|meta ai|facebook"),
    ("spacexai", r"grok|xai|x\.ai|spacex"),
    ("moonshot", r"kimi|moonshot|月之暗面"),
    ("zai", r"glm|zhipu|chatglm|z\.ai|\bzai\b|智谱|清言"),
    ("xiaomi", r"mimo|xiaomi|小米"),
    ("deepseek", r"deepseek|深度求索"),
    ("qwen", r"qwen|qwq|tongyi|通义|千问|alibaba|阿里"),
    ("tencent-hy", r"hunyuan|混元|tencent|腾讯|\bhy[-\s]?\d|\bhy\b"),
    ("bytedance-seed", r"doubao|豆包|seed|bytedance|字节|volc"),
)


def detect_provider(text):
    """Best-effort provider for a model name such as "Claude Opus 5.5" or "kimi-k2"; None when unknown."""
    value = str(text or "").lower()
    for provider, pattern in _DETECT:
        if re.search(pattern, value):
            return provider
    return None


def _text(value, field, limit, required=True):
    if value is None and not required:
        return None
    if not isinstance(value, str) or (required and not value.strip()):
        raise ValueError(f"showcase.json: {field} must be a non-empty string")
    value = value.strip()
    if len(value) > limit:
        raise ValueError(f"showcase.json: {field} must be at most {limit} characters")
    return value


def _number(value, field, low, high):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high:
        raise ValueError(f"showcase.json: {field} must be a number from {low} to {high}")
    return round(float(value), 2)


def _point(value, field):
    if not isinstance(value, (list, tuple)) or len(value) != 2:
        raise ValueError(f"showcase.json: {field} must be [x, y] in metres")
    return [_number(value[0], field + "[0]", 0, FIELD[0]), _number(value[1], field + "[1]", 0, FIELD[1])]


def _strings(value, field, count, limit):
    if value is None:
        return []
    if not isinstance(value, list) or len(value) > count:
        raise ValueError(f"showcase.json: {field} must be a list of at most {count} strings")
    return [_text(item, f"{field}[{i}]", limit) for i, item in enumerate(value)]


def agent(value, field="agent"):
    """Normalise {"provider", "model"}; an unknown provider is inferred from the model name when possible."""
    if value is None:
        return None
    if not isinstance(value, dict):
        raise ValueError(f"{field} must be an object with provider and model")
    model = value.get("model")
    if model is not None and (not isinstance(model, str) or len(model.strip()) > 64):
        raise ValueError(f"{field}.model must be a name of at most 64 characters")
    model = (model or "").strip() or None
    provider = value.get("provider") or detect_provider(model) or "unknown"
    if provider not in AUTHORS:
        provider = detect_provider(provider) or detect_provider(model) or "unknown"
    return {"provider": provider, **({"model": model} if model else {})}


def validate_showcase(data):
    """Check an agent-written showcase and return the normalised copy shown in the gallery."""
    if not isinstance(data, dict):
        raise ValueError("showcase.json must contain a JSON object")
    if data.get("format") != SHOWCASE_FORMAT:
        raise ValueError(f'showcase.json: format must be "{SHOWCASE_FORMAT}"')
    style = data.get("style")
    if not isinstance(style, dict) or set(style) != set(STYLE_KEYS):
        raise ValueError(f"showcase.json: style must score exactly {', '.join(STYLE_KEYS)} from 0 to 100")
    phases = data.get("phases")
    if not isinstance(phases, list) or not 1 <= len(phases) <= 8:
        raise ValueError("showcase.json: phases must list 1 to 8 match phases")
    result = {"format": SHOWCASE_FORMAT, "title": _text(data.get("title"), "title", 40),
              "tagline": _text(data.get("tagline"), "tagline", 80), "summary": _text(data.get("summary"), "summary", 600),
              "style": {key: int(round(_number(style[key], f"style.{key}", 0, 100))) for key in STYLE_KEYS}, "phases": []}
    last = 0.0
    for i, phase in enumerate(phases):
        if not isinstance(phase, dict):
            raise ValueError(f"showcase.json: phases[{i}] must be an object")
        start, end = _number(phase.get("from"), f"phases[{i}].from", 0, 420), _number(phase.get("to"), f"phases[{i}].to", 0, 420)
        if end <= start or start < last:
            raise ValueError(f"showcase.json: phases[{i}] must follow the previous phase and end after it starts")
        focus = phase.get("focus", "support")
        if focus not in PHASE_FOCUS:
            raise ValueError(f"showcase.json: phases[{i}].focus must be one of {', '.join(PHASE_FOCUS)}")
        result["phases"].append({"from": start, "to": end, "title": _text(phase.get("title"), f"phases[{i}].title", 24),
                                 "goal": _text(phase.get("goal"), f"phases[{i}].goal", 160), "focus": focus})
        last = end
    units = data.get("units") or []
    if not isinstance(units, list) or len(units) > 16:
        raise ValueError("showcase.json: units must list at most 16 robots or groups")
    result["units"] = []
    for i, unit in enumerate(units):
        if not isinstance(unit, dict):
            raise ValueError(f"showcase.json: units[{i}] must be an object")
        item = {"name": _text(unit.get("name"), f"units[{i}].name", 16), "role": _text(unit.get("role"), f"units[{i}].role", 20),
                "task": _text(unit.get("task"), f"units[{i}].task", 160)}
        route = unit.get("route") or []
        if not isinstance(route, list) or len(route) > 12:
            raise ValueError(f"showcase.json: units[{i}].route must list at most 12 points")
        item["route"] = [_point(p, f"units[{i}].route[{k}]") for k, p in enumerate(route)]
        if unit.get("anchor") is not None:
            item["anchor"] = _point(unit["anchor"], f"units[{i}].anchor")
        result["units"].append(item)
    rules = data.get("rules") or []
    if not isinstance(rules, list) or len(rules) > 10:
        raise ValueError("showcase.json: rules must list at most 10 decisions")
    result["rules"] = [{"when": _text(r.get("when") if isinstance(r, dict) else None, f"rules[{i}].when", 60),
                        "then": _text(r.get("then"), f"rules[{i}].then", 80)} for i, r in enumerate(rules)]
    result["priorities"] = _strings(data.get("priorities"), "priorities", 6, 24)
    result["strengths"] = _strings(data.get("strengths"), "strengths", 5, 80)
    result["weaknesses"] = _strings(data.get("weaknesses"), "weaknesses", 5, 80)
    if data.get("generated_by") is not None:
        result["generated_by"] = agent(data["generated_by"], "showcase.json: generated_by")
    return result


def parse_showcase(raw):
    if isinstance(raw, str):
        raw = raw.encode("utf-8")
    if len(raw) > SHOWCASE_LIMIT:
        raise ValueError(f"showcase.json must be at most {SHOWCASE_LIMIT // 1024} KiB")
    try:
        data = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"showcase.json is not valid JSON: {error}") from error
    return validate_showcase(data)


def github_login(value):
    """The uploader's GitHub username, given bare, as "@name" or as a profile URL; None when blank."""
    if value is None or (isinstance(value, str) and not value.strip()):
        return None
    if not isinstance(value, str):
        raise ValueError("github must be a GitHub username")
    text = value.strip().removeprefix("@")
    url = re.fullmatch(r"(?:https?://)?(?:www\.)?github\.com/([^/?#\s]+)/?", text, re.IGNORECASE)
    login = url.group(1) if url else text
    if not _GITHUB_LOGIN.fullmatch(login):
        raise ValueError("github must be a GitHub username such as RM-Arena")
    return login


def validate_profile(data):
    """The upload questionnaire: who generated the strategy and how it should be classified."""
    if not isinstance(data, dict):
        raise ValueError("Strategy profile must be an object")
    tactic = data.get("tactic") or "balanced"
    if tactic not in TACTICS:
        raise ValueError(f"tactic must be one of {', '.join(TACTICS)}")
    tags = data.get("tags") or []
    if not isinstance(tags, list) or len(tags) > len(TAGS) or any(tag not in TAGS for tag in tags):
        raise ValueError(f"tags must come from {', '.join(TAGS)}")
    def optional(key, limit):
        value = data.get(key)
        if value is None or value == "":
            return None
        if not isinstance(value, str) or len(value.strip()) > limit:
            raise ValueError(f"{key} must be at most {limit} characters")
        return value.strip() or None
    profile = {"format": PROFILE_FORMAT, "agent": agent(data.get("agent") or {"provider": "unknown"}),
               "tactic": tactic, "tags": sorted(set(tags), key=TAGS.index)}
    for key, limit in (("display_name", 40), ("author", 40), ("description", 600)):
        value = optional(key, limit)
        if value:
            profile[key] = value
    github = github_login(data.get("github"))
    if github:
        profile["github"] = github
    if isinstance(data.get("created_at"), (int, float)) and not isinstance(data.get("created_at"), bool):
        profile["created_at"] = float(data["created_at"])
    return profile


def _read(path, name):
    file = Path(path) / name
    if file.is_symlink() or not file.is_file() or file.stat().st_size > SHOWCASE_LIMIT:
        return None
    try:
        return json.loads(file.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        return None


def load_showcase(path):
    data = _read(path, SHOWCASE_FILE)
    try:
        return validate_showcase(data) if data is not None else None
    except ValueError:
        return None


def is_bundled(path):
    return Path(path).resolve().parent in (bundled_examples(), (ROOT / 'examples').resolve())


def load_profile(path, bundled=None):
    """Stored attribution, with compatibility for earlier bundled strategy profiles."""
    bundled = is_bundled(path) if bundled is None else bundled
    from .seasons import season_of_profile
    season = season_of_profile((_read(path, "manifest.json") or {}).get("rules_profile"))
    data = _read(path, PROFILE_FILE)
    if bundled and isinstance(data, dict) and isinstance(data.get("agent"), dict) and data["agent"].get("provider") in (LEGACY_CATEGORY, "bundled"):
        data = {**data, "agent": example_agent(season)}
    try:
        profile = validate_profile(data) if data is not None else None
    except ValueError:
        profile = None
    if profile is None:
        showcase = load_showcase(path)
        reported = (showcase or {}).get("generated_by")
        profile = validate_profile({"agent": reported or (example_agent(season) if bundled else {"provider": "unknown"})})
    if bundled and canonical_key(Path(path).name).startswith(CLAUDE_PREFIX):
        profile["agent"] = dict(CLAUDE_AGENT)
    if bundled:
        profile.setdefault("github", LIBRARY_UPLOADER)
    return profile


def agent_of(path, bundled=None):
    return load_profile(path, bundled)["agent"]


def team_agents(slots, sources, strategy_names):
    """Deployment slot -> the agent behind that robot's strategy, for the broadcast."""
    return {str(slot): {**agent_of(source), "strategy": name} for slot, source, name in zip(slots, sources, strategy_names)}
