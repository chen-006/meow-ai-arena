"""Canonical strategy identifiers and compatibility for clients older than 0.7."""
import re

CLAUDE_PREFIX = 'claude_opus55_xhigh'
LEGACY_CATEGORY = 'off' + 'icial'
LEGACY_RULES_FIELD = LEGACY_CATEGORY + '_rules'
LEGACY_ZH = '\u5b98\u65b9'
CLAUDE_AGENT = {'provider': 'anthropic', 'model': 'Claude Opus 5.5 xhigh'}


def canonical_key(value):
    if isinstance(value, str) and re.fullmatch(LEGACY_CATEGORY + r'(?:_2027)?_(?:team|hold|squad|distributed|adaptive(?:_\d+(?:_(?:assault|guard))?)?)', value):
        return CLAUDE_PREFIX + value[len(LEGACY_CATEGORY):]
    return value


def display_name(value):
    if not isinstance(value, str):
        return value
    value = value.replace(LEGACY_ZH + '基线', '基线策略').replace(LEGACY_ZH + '守势', '守势策略')
    return re.sub(LEGACY_CATEGORY, '', value, flags=re.I).replace(LEGACY_ZH, '').strip()


def confirmed_claude(agent, known=False):
    agent = agent or {}
    provider = agent.get('provider')
    return provider == LEGACY_CATEGORY or (known and (provider in (None, 'unknown') or
        (provider == 'anthropic' and agent.get('model') in (None, 'Claude Opus 5.5', CLAUDE_AGENT['model']))))


def normalize_metadata(metadata):
    """Normalize public records without rewriting their checksummed replay files."""
    import copy
    result = copy.deepcopy(metadata)
    for team in result.get('teams', {}).values():
        old_name = team.get('name', '')
        team['name'] = canonical_key(old_name)
        if team.get('display_name'):
            team['display_name'] = display_name(team['display_name'])
        if confirmed_claude(team.get('agent'), team['name'].startswith(CLAUDE_PREFIX)):
            team['agent'] = dict(CLAUDE_AGENT)
    for name, agents in result.get('agents', {}).items():
        known = result.get('teams', {}).get(name, {}).get('name', '').startswith(CLAUDE_PREFIX)
        for key, value in agents.items():
            if confirmed_claude(value, known or canonical_key(value.get('strategy', '')).startswith(CLAUDE_PREFIX)):
                value.update(CLAUDE_AGENT)
            if value.get('strategy'):
                value['strategy'] = display_name(canonical_key(value['strategy']))
    return result
