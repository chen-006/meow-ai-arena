"""Read-only application resources and writable per-user state.

Source checkouts keep their historical directory layout. Desktop launchers set
RMUC_USER_DATA so installed applications never write inside their bundle.
"""
import os
from pathlib import Path

ROOT = Path(os.environ.get("RMUC_RESOURCE_ROOT", Path(__file__).resolve().parent.parent)).resolve()


def data_path(name):
    return Path(os.environ.get("RMUC_USER_DATA", ROOT)).resolve() / name
