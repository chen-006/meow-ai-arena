"""Validated, per-season simulation choices. Defaults always come from the shipped rules."""
import copy
import json
import math
from pathlib import Path
import threading

from .engine import _config
from .paths import ROOT

FIELDS = json.loads((ROOT / 'config/battle_settings.json').read_text(encoding='utf-8'))


def schema(season):
    if season not in ('2026', '2027'):
        raise ValueError('Unknown season')
    return [f for f in FIELDS if season in f.get('seasons', ['2026', '2027'])]


def defaults(season):
    sim = copy.deepcopy(_config['simulation_assumptions'])
    sim.update(_config['seasons'][season].get('simulation_overrides', {}))
    values = {}
    for field in schema(season):
        value = sim
        for key in field['path'].split('.'):
            value = value[int(key)] if isinstance(value, list) else value[key]
        values[field['path']] = value
    return values


def normalize(values, season):
    if not isinstance(values, dict):
        raise ValueError('Battle settings must be an object')
    allowed = {f['path']: f for f in schema(season)}
    baseline = defaults(season)
    result = {}
    for key, value in values.items():
        field = allowed.get(key)
        if not field or isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
            raise ValueError(f'Invalid battle setting: {key}')
        if not field['min'] <= value <= field['max']:
            raise ValueError(f"{field['label']} must be in [{field['min']}, {field['max']}]")
        if value != baseline[key]:
            result[key] = value
    return result


class BattleSettings:
    def __init__(self, path):
        self.path = Path(path)
        self.lock = threading.RLock()

    def _read(self):
        return json.loads(self.path.read_text(encoding='utf-8')) if self.path.exists() else {}

    def get(self, season):
        with self.lock:
            overrides = normalize(self._read().get(season, {}), season)
        return {'season': season, 'fields': schema(season), 'defaults': defaults(season),
                'values': defaults(season) | overrides, 'overrides': overrides}

    def save(self, season, values):
        overrides = normalize(values, season)
        with self.lock:
            saved = self._read()
            saved[season] = overrides
            self.path.parent.mkdir(parents=True, exist_ok=True)
            temporary = self.path.with_suffix('.tmp')
            temporary.write_text(json.dumps(saved, ensure_ascii=False, indent=2), encoding='utf-8')
            temporary.replace(self.path)
            return self.get(season)
