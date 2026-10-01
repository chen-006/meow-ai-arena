#!/bin/sh
set -eu
cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
if [ -n "${BOMB_PYTHON:-}" ]; then
    runtime=$BOMB_PYTHON
elif [ -x .runtime/bin/python3 ]; then
    runtime=.runtime/bin/python3
elif command -v python3 >/dev/null 2>&1; then
    runtime=$(command -v python3)
else
    printf '%s\n' 'Python 3.10+ not found. Report the missing runtime to the organizer, or set BOMB_PYTHON to an existing Python path.' >&2
    exit 1
fi
"$runtime" -c 'import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)'
if [ "$#" -eq 0 ]; then set -- clock.py; fi
exec "$runtime" -X utf8 "$@"
