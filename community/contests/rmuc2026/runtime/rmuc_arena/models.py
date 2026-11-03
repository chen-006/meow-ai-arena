"""Public hardware catalog and immutable per-side lineups for each rules season."""
import json
from pathlib import Path

from . import seasons as season_rules

# 2026 names kept for existing callers; season-aware callers pass `season`.
SLOTS=(1,2,3,4,6,7)
ROLES={1:'hero',2:'engineer',3:'infantry',4:'infantry',6:'aerial',7:'infantry'}
DEFAULT_TEAM_LINEUP={str(s):s for s in SLOTS}
from .paths import ROOT

def robot_models():
    return json.loads((ROOT/'config/rules.json').read_text(encoding="utf-8"))['robot_designs']

def model_ids():
    return tuple(sorted(int(k) for k in robot_models()))

def default_team_lineup(season=season_rules.LEGACY_SEASON):
    return season_rules.default_lineup(season)

def initial_model_stats(design,rules,season=None):
    """Level-one preselected referee performance, using the engine's public tables."""
    role=design['role']
    season=season or design.get('season',season_rules.LEGACY_SEASON)
    if season=='2027':
        rulebook=rules[rules['seasons']['2027']['rules_key']];specs=rules['seasons']['2027']['specs']
        stats=dict(specs[role])
        if role=='heavy':stats.update(rulebook['heavy']['stages'][0])
        elif role=='infantry':
            defaults = rulebook['profiles']['defaults']
            stats.update(rulebook['profiles']['infantry_chassis'][defaults['infantry_chassis']][0])
            stats.update(rulebook['profiles']['infantry_weapon'][defaults['infantry_weapon']][0])
        elif role=='sentry':stats.update(rulebook['sentry']['automatic']);stats['ammo']=rulebook['initial_ammo']['sentry']
        stats.setdefault('max_heat',0);stats.setdefault('cooling',0);stats.setdefault('power_w',0)
        return {key:stats.get(key,0) for key in ('hp','power_w','max_heat','cooling','damage','ammo')}
    profiles=rules['rulebook']['profiles'];stats=dict(rules['specs'][role])
    if role=='hero':stats.update(profiles['hero'][design['hero_profile']][0])
    elif role=='infantry':
        stats.update(profiles['infantry_chassis'][design['chassis_profile']][0])
        stats.update(profiles['infantry_weapon'][design['weapon_profile']][0])
    elif role=='aerial':stats.update(profiles['aerial'][0])
    else:stats.update(profiles['engineer'])
    return {key:stats[key] for key in ('hp','power_w','max_heat','cooling','damage','ammo')}

def model_catalog():
    rules=json.loads((ROOT/'config/rules.json').read_text(encoding="utf-8"))
    catalog={}
    for key,d in rules['robot_designs'].items():
        legal=[s for s,v in d.get('seasons',{}).items() if v.get('legal')]
        catalog[key]={**d,'initial_stats':initial_model_stats(d,rules),
            'initial_stats_by_season':{s:initial_model_stats(d,rules,s) for s in legal}}
    return catalog

def normalize_team_lineup(lineup=None,season=None):
    """Validate a lineup for its season; without a season the slots decide (2026 by default)."""
    if season is None:
        season=season_rules.infer_season(lineup.keys()) if lineup else season_rules.LEGACY_SEASON
    slots=season_rules.slots(season);roles=season_rules.roles(season)
    value=dict(season_rules.default_lineup(season) if lineup is None else lineup)
    value={str(k):v for k,v in value.items()}
    if set(value)!={str(s) for s in slots}:
        raise ValueError(f"Team lineup must select exactly slots {','.join(map(str,slots))}")
    models=robot_models()
    for slot,model in value.items():
        if isinstance(model,bool) or not isinstance(model,int) or str(model) not in models:
            raise ValueError(f'Unknown robot model for slot {slot}')
        if models[str(model)]['role']!=roles[int(slot)]:
            raise ValueError(f'Slot {slot} requires a {roles[int(slot)]} model')
        if not season_rules.legal(model,season):
            raise ValueError(f'Model {model} does not pass the {season} construction rules')
    return {str(s):value[str(s)] for s in slots}

def lineup_season(lineup):
    return season_rules.infer_season(lineup.keys())

def lineup_deployments(lineup,season=None):
    """One side's lineup as engine deployments, in slot order."""
    value=normalize_team_lineup(lineup,season)
    return [{'slot':int(s),'model':value[s]} for s in value]

def mirrored_lineups(lineup,season=None):
    deployments=lineup_deployments(lineup,season)
    return {team:[dict(item) for item in deployments] for team in ('red','blue')}
