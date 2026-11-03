"""Locations for the bundled example and independently maintained strategy sources."""
import os
from pathlib import Path
from .paths import ROOT

STRATEGY_REPOSITORY = 'https://github.com/RM-Arena/Arena-Strategies'
SOL_MAX = 'gpt61_sol_max_2027'


def bundled_examples():
    # Tests explicitly supply the separately versioned historical fixtures.
    return Path(os.environ.get('RMUC_TEST_STRATEGIES', ROOT / 'examples')).resolve()


def authoring_examples():
    """Generators target a separate authoring checkout or ignored staging directory."""
    return Path(os.environ.get('RMUC_TEST_STRATEGIES') or os.environ.get('RMUC_STRATEGY_OUTPUT')
                or ROOT / 'build/strategy-repository/strategies').resolve()


def bundled_manifests():
    """Regression fixtures may supplement the current example, never replace it."""
    examples = ROOT / 'examples'
    fixtures = bundled_examples()
    paths = list(examples.glob('*/manifest.json'))
    if fixtures != examples.resolve():
        paths += [p for p in fixtures.glob('*/manifest.json') if p.parent.name != SOL_MAX]
    return sorted(paths)
