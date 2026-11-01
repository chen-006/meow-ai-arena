"""JSON-lines policy process. Container isolation is enforced by the host."""
import contextlib
import importlib.util
import json
import math
import os
import random
import sys
import traceback

# Sandbox budgets. This file runs alone inside the policy container; the host imports the
# same values for its submission checks and container limits.
SOURCE_LIMIT = 512 * 1024
MEMORY_LIMIT = 1024 * 1024 * 1024
STATE_LIMIT = 256 * 1024
ACTIONS_LIMIT = 16 * 1024


def main():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    random.seed(int(sys.argv[2]))
    try:
        import resource
        resource.setrlimit(resource.RLIMIT_NOFILE, (64, 64))
        resource.setrlimit(resource.RLIMIT_FSIZE, (1024 * 1024, 1024 * 1024))
        if sys.platform == "linux":
            resource.setrlimit(resource.RLIMIT_AS, (MEMORY_LIMIT, MEMORY_LIMIT))
    except (ImportError, ValueError, OSError):
        pass
    output = sys.stdout
    state = None
    # Match-constant observation fields (the arena map), delivered once with the first
    # observation and merged into every later one by the host's request.
    static = {}
    try:
        with contextlib.redirect_stdout(sys.stderr):
            spec = importlib.util.spec_from_file_location("strategy", sys.argv[1])
            strategy = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(strategy)
            if not callable(getattr(strategy, "act", None)):
                raise ValueError("strategy.py must define act(observation, state)")
        output.write('{"ready":true}\n')
        output.flush()
    except BaseException:
        traceback.print_exc(file=sys.stderr)
        return
    for line in sys.stdin:
        try:
            observation = json.loads(line)
            chunk = observation.pop("__static__", None)
            if chunk is not None:
                static = chunk
            if static:
                observation = {**static, **observation}
            with contextlib.redirect_stdout(sys.stderr):
                result = strategy.act(observation, state)
            if not isinstance(result, (tuple, list)) or len(result) != 2:
                raise ValueError("act must return (actions, new_state)")
            actions, new_state = result
            if not isinstance(actions, dict):
                raise ValueError("actions must be a dict keyed by slot")
            if new_state is not None:
                state_json = json.dumps(new_state, allow_nan=False, separators=(",", ":"))
                if len(state_json.encode()) > STATE_LIMIT:
                    raise ValueError(f"state exceeds {STATE_LIMIT // 1024} KiB")
            response = json.dumps({"actions": actions}, allow_nan=False, separators=(",", ":"))
            if len(response.encode()) > ACTIONS_LIMIT:
                raise ValueError(f"actions exceed {ACTIONS_LIMIT // 1024} KiB")
            state = new_state
            output.write(response + "\n")
            output.flush()
        except BaseException as exc:
            traceback.print_exc(file=sys.stderr)
            output.write(json.dumps({"error": str(exc)[:512], "actions": {}}) + "\n")
            output.flush()


if __name__ == "__main__":
    main()
