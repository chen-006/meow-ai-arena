"""Policy isolation, deadlines, action ownership, and replay recording."""
from concurrent.futures import ThreadPoolExecutor, TimeoutError as FutureTimeout
from collections import deque
from collections.abc import Sequence
import hashlib
import json
import math
import os
from pathlib import Path
import queue
import shutil
from .platform_support import kill_process_tree
import subprocess
import sys
import tempfile
import threading
import time
import uuid

from .engine import Match, POLICY_INTERVAL, DT, SLOT_IDS, RULES_PROFILE, STATIC_FRAME_KEYS
from .referee import validate_referee
from .submissions import load_submission
from .communication import MAX_MESSAGE_BYTES, TeamCommunication, limit_messages, message_byte_limit
from .action_protocol import ACTION_FIELDS, validate_action, validate_actions
from .decks import prepare_decks
from .showcase import load_profile
from .worker import MEMORY_LIMIT
from . import seasons as season_rules

IMAGE = "rmuc-arena-policy:0.5"
CPU_LIMIT = 1.0
# Seconds for module import and global initialization before the first decision.
STARTUP_LIMIT = {"docker": 60, "trusted-local": 15}
# A stalled process is stopped after three consecutive missed decision windows.
POLICY_TIMEOUT_LIMIT = 3
# A pilot owns the robot until their commands stop: brief gaps coast, long silence hands back.
PLAYER_COAST = 0.35
PLAYER_TIMEOUT = 2.5
# First-person focus: the eye sits FPV_EYE_LIFT muzzle heights above the barrel (the camera in
# `ArenaScene.tsx` uses the same lift), and the aim assist only considers plates inside the lens.
# 1.27 is 15 % below the previous 1.495, matching the lowered cockpit camera.
FPV_EYE_LIFT = 1.27
# A lobbed shell only lands on the reticle once the launcher has climbed to the solved elevation:
# the gun slews in the engine (turret_pitch_rate_rad_s), so the trigger is held until it is there.
LOB_READY_RAD = 0.012
ASSIST_YAW = 0.80
ASSIST_PITCH = 0.56
# Beyond this range the assist still solves the elevation, but only applies 80 % of it: the
# normal assisted aim point is 20 % higher than the far shot wants, and a full long-range arc
# reads as a floating crosshair instead of a stable aim point.
ASSIST_PITCH_RANGE_M = 10.0
ASSIST_FAR_PITCH_SCALE = 0.8
def sight_blocked(arena, start, end):
    """True when a wall, a deck or a ramp stands on the line between two 3-D points.

    Walls are the referee's own obstacle boxes and the ground is the same height field the
    simulation runs on, so an assist can never reach through cover — or through the central
    highland — that the pilot cannot see past.
    """
    if not arena:
        return False
    from . import sdk
    (ax, ay, az), (bx, by, bz) = start, end
    dx, dy, dz = bx - ax, by - ay, bz - az
    # Exact segment/box test for every barrier: walking the ray in fixed steps could step over a
    # 0.15 m wall and report a plate behind it as visible.
    for w in arena.get("obstacles", ()):
        angle = w.get("rotation", 0.0)
        c, s = math.cos(angle), math.sin(angle)
        cx, cy = w["x"] + w["w"] / 2, w["y"] + w["h"] / 2
        x0, y0 = (ax - cx) * c + (ay - cy) * s, -(ax - cx) * s + (ay - cy) * c
        x1, y1 = (bx - cx) * c + (by - cy) * s, -(bx - cx) * s + (by - cy) * c
        bottom = w.get("bottom", 0.0)
        t0, t1 = 0.0, 1.0
        for p0, p1, lo, hi in (
            (x0, x1, -w["w"] / 2, w["w"] / 2),
            (y0, y1, -w["h"] / 2, w["h"] / 2),
            (az, bz, bottom, bottom + w.get("height", 1.0)),
        ):
            delta = p1 - p0
            if abs(delta) < 1e-9:
                if not lo <= p0 <= hi:
                    break
            else:
                near, far = (lo - p0) / delta, (hi - p0) / delta
                if near > far:
                    near, far = far, near
                t0, t1 = max(t0, near), min(t1, far)
                if t0 > t1:
                    break
        else:
            return True
    # Terrain has no simple box form, so sample it; the step is small enough for the 0.15 m rims.
    steps = max(1, int(math.ceil(math.sqrt(dx * dx + dy * dy + dz * dz) / 0.1)))
    for i in range(1, steps):
        t = i / steps
        x, y, z = ax + dx * t, ay + dy * t, az + dz * t
        if sdk.terrain_height(arena, x, y) > z + 0.12:
            return True
    return False


def next_shot_overheats(robot):
    """True when firing one more round would push the barrel past its published heat limit.

    The engine adds the round's heat after the shot and locks the gun once the total crosses the
    limit, so a held trigger can overheat on a frame the cockpit has not received yet. A sentry in
    enhanced attack posture and a heavy's launcher-install shot add no heat, exactly as in the
    engine.
    """
    if robot.get("posture") == "attack" and robot.get("posture_enhanced"):
        return False
    if robot.get("role") == "heavy" and robot.get("launcher") == "fitted":
        return False
    from . import sdk
    heat = 100.0 if sdk.is_42(robot) else 10.0
    return robot.get("heat", 0.0) + heat > robot.get("max_heat", 0.0) + 1e-8


def lob_climb_seconds(robot, elevation):
    """How long the launcher needs to slew onto a solved lob elevation, plus a settling margin.

    The gun climbs at the design's `turret_pitch_rate_rad_s`, and the published elevation it starts
    from is accurate enough for timing: a couple of degrees of chassis attitude is a few hundredths
    of a second.
    """
    from . import sdk
    rate = float(sdk._design(robot).get("turret_pitch_rate_rad_s") or 2.5)
    return abs(elevation - float(robot.get("pitch") or 0.0)) / max(rate, 0.2) + 0.12


def aim_assist(robot, target_id, robots, arena=None, view=None, structures=None, pitch=None):
    """World yaw and ballistic pitch that put `robot`'s muzzle on `target_id`'s armour plate.

    Uses the same solver the public SDK gives policies, so a pilot's assisted shot obeys
    the engine's ballistics and aims at the plate facing the shooter rather than the hull
    centre. Robots, outposts and bases all carry armour plates. The plate must be inside the
    pilot's lens and clear of cover — a target behind a wall, a deck or the camera is not
    assisted at all.

    An outpost belongs to whichever team last rebuilt it (`owner`), not to the half of the
    field it stands on, so a red outpost rebuilt by blue is never locked by a red pilot.

    Inside ten metres the assist solves yaw and elevation; past that it still solves the elevation
    but returns 80 % of it, 20 % below the normal assisted aim point, so a distant shot does not
    float above the target.
    """
    if not robot:
        return None
    target = None
    parts = target_id.rsplit("_", 1)
    if structures and target_id in structures:
        target = structures[target_id]
        owner = (target.get("owner") if target.get("kind") == "outpost" else None) or parts[0]
        if owner == robot["team"] or target.get("hp", 0.0) <= 0.0:
            target = None
    else:
        tokens = target_id.split("-")
        target = robots.get((tokens[0], tokens[1])) if len(tokens) > 1 else None
        if target and (target["team"] == robot["team"] or not target.get("alive", True)):
            target = None
    if not target:
        return None
    from . import sdk
    try:
        plate = sdk._aim_target(robot, target)
    except (KeyError, TypeError, ValueError, ZeroDivisionError):
        return None
    muzzle_height = robot.get("muzzle_height_m", 0.45)
    # The pilot looks from the raised first-person eye; the barrel sits lower. The plate has to
    # be visible from the eye and reachable from the muzzle.
    eye = (robot["x"], robot["y"], robot.get("z", 0.0) + muzzle_height * FPV_EYE_LIFT)
    muzzle = (robot["x"], robot["y"], robot.get("z", 0.0) + muzzle_height + 0.08)
    dx, dy = plate[0] - robot["x"], plate[1] - robot["y"]
    distance = math.hypot(dx, dy)
    bearing = math.atan2(dy, dx)
    if view is not None:
        offset = (bearing - view + math.pi) % math.tau - math.pi
        if abs(offset) > ASSIST_YAW:
            return None
        if pitch is not None and distance > 1e-6:
            elevation = math.atan2(plate[2] - eye[2], distance)
            if abs(elevation - pitch) > ASSIST_PITCH:
                return None
    if sight_blocked(arena, eye, plate) or sight_blocked(arena, muzzle, plate):
        return None
    try:
        yaw = sdk.aim_at(robot, target)
        elevation = sdk.aim_pitch(robot, target)
        # Past ten metres the assist trims the solved elevation instead of handing the aim back to
        # the pilot: keeping the pitch fully solved made the crosshair float above a far target.
        if distance > ASSIST_PITCH_RANGE_M:
            elevation *= ASSIST_FAR_PITCH_SCALE
        return yaw, elevation
    except (KeyError, TypeError, ValueError, ZeroDivisionError):
        return None


class FrameLog(Sequence):
    """Spool immutable frames to disk instead of retaining a full match's heap.

    Keeping thousands of nested frame dictionaries triggers long GC pauses that
    count against agents' wall-clock deadlines. Only offsets and events stay in
    memory; the public sequence interface remains available to replay callers.
    """
    def __init__(self, initial):
        self.file = tempfile.TemporaryFile(mode="w+b")
        self.offsets, self.events = [], []
        self.lock = threading.RLock()
        self.static = {key:initial[key] for key in STATIC_FRAME_KEYS if key in initial}
        self.append(initial)

    def __len__(self):
        return len(self.offsets)

    def __del__(self):
        if hasattr(self, "file"):
            self.file.close()

    def _write(self, frame, raw=None):
        if raw is None:
            raw = json.dumps({key:value for key,value in frame.items() if key not in self.static}, ensure_ascii=False, separators=(",", ":"), allow_nan=False).encode()
        self.file.seek(0, 2)
        offset = self.file.tell()
        self.file.write(raw)
        return offset, len(raw)

    def append(self, frame, raw=None):
        """`raw` is the frame already encoded without its static fields, if available."""
        with self.lock:
            self.offsets.append(self._write(frame, raw))
            self.events.append(list(frame["events"]))

    def __getitem__(self, index):
        if isinstance(index, slice):
            return [self[i] for i in range(*index.indices(len(self))) ]
        with self.lock:
            offset, size = self.offsets[index]
            self.file.seek(offset)
            return {**self.static,**json.loads(self.file.read(size))}

    def __setitem__(self, index, frame):
        with self.lock:
            # Preserve indexing semantics while appending a replacement record.
            self.offsets[index] = self._write(frame)
            self.events[index] = list(frame["events"])

    def events_since(self, tick):
        with self.lock:
            start = max(1, tick + 1, len(self) - 1000)
            return [event for events in self.events[start:] for event in events]

    def encoded(self):
        for index in range(len(self)):
            with self.lock:
                offset, size = self.offsets[index]
                self.file.seek(offset)
                raw = self.file.read(size)
            yield raw

    def encoded_range(self, start, count=None):
        """Static-free frames in `[start, start+count)`, for callers that merge the static half once.

        A highlight reel only needs the handful of five-second windows it plays, so it must not be
        handed a whole match: a full seven-minute replay is hundreds of megabytes.
        """
        stop = len(self.offsets) if count is None else min(len(self.offsets), start + max(0, count))
        with self.lock:
            for index in range(max(0, start), stop):
                offset, size = self.offsets[index]
                self.file.seek(offset)
                yield self.file.read(size)


class LatestFrameLog:
    """A single current frame, with no replay spool or per-tick action history."""
    def __init__(self, initial):
        self.current = initial
        self.static = {key: initial[key] for key in STATIC_FRAME_KEYS if key in initial}

    def append(self, frame, raw=None):
        self.current = frame

    def __getitem__(self, index):
        if index not in (0, -1):
            raise IndexError(index)
        return self.current

    def __setitem__(self, index, frame):
        self[index]
        self.current = frame

    def __len__(self):
        return 1


class EncodedObservation:
    """An engine observation line forwarded to a policy without decoding it.

    `static` is the match-constant prefix (the arena map) and is non-empty only on the first
    observation a policy receives: the worker keeps it and merges it into every later line, so the
    unchanged 66 KB map is neither piped nor decoded again.
    """
    def __init__(self, payload, controlled_ids, has_messages, static=b""):
        self.payload, self.controlled_ids, self.has_messages = payload, controlled_ids, has_messages
        self.static = static


class Policy:
    def __init__(self, path, seed, sandbox="docker", timeout=0.2, expected_digest=None, deadline=None, stop_event=None, dynamic_messages=False):
        self.timeout, self.sandbox, self.disabled = timeout, sandbox, False
        self.deadline, self.stop_event = deadline, stop_event
        self.dynamic_messages = dynamic_messages
        self.logs, self.lines, self.calls, self.errors = deque(maxlen=32), queue.Queue(maxsize=2), 0, 0
        self.pending_response, self.consecutive_timeouts = None, 0
        self.name, self.temp = "rmuc-" + uuid.uuid4().hex[:16], tempfile.TemporaryDirectory(prefix="rmuc-policy-")
        stage = Path(self.temp.name)
        stage.chmod(0o755)
        source = path.read_bytes()
        self.digest = hashlib.sha256(source).hexdigest()
        if expected_digest is not None and self.digest != expected_digest:
            self.temp.cleanup()
            raise ValueError("Submitted strategy changed before staging")
        (stage / "strategy.py").write_bytes(source)
        (stage / "strategy.py").chmod(0o444)
        worker = Path(__file__).with_name("worker.py")
        if sandbox == "docker":
            command = ["docker", "run", "--rm", "--name", self.name, "--network=none", "--read-only",
                       "--cap-drop=ALL", "--security-opt=no-new-privileges", f"--memory={MEMORY_LIMIT}", f"--memory-swap={MEMORY_LIMIT}",
                       f"--cpus={CPU_LIMIT}", "--pids-limit=1", "--user=65534:65534", "--ipc=none",
                       "--ulimit", "nofile=64:64", "--mount", f"type=bind,source={stage},target=/submission,readonly",
                       "-i", IMAGE, "/submission/strategy.py", str(seed)]
        elif sandbox == "trusted-local":
            # Copy only the public runtime. Local mode is for trusted developer code.
            shutil.copyfile(worker, stage / "worker.py")
            shutil.copyfile(worker.with_name("sdk.py"), stage / "sdk.py")
            shutil.copyfile(worker.parent.parent / "config/rules.json", stage / "rules.json")
            command = [sys.executable, "-I", str(stage / "worker.py"), str(stage / "strategy.py"), str(seed)]
        else:
            self.temp.cleanup()
            raise ValueError("Unsupported sandbox")
        env = {"PATH": os.environ.get("PATH", ""), "HOME": os.environ.get("HOME", ""), "PYTHONHASHSEED": "0",
               "PYTHONDONTWRITEBYTECODE": "1", "PYTHONUTF8": "1", "LANG": "C.UTF-8"}
        # Docker client needs its configured socket; never propagate arbitrary secrets.
        for key in ("DOCKER_HOST", "DOCKER_CONTEXT", "SYSTEMROOT", "WINDIR", "COMSPEC", "PATHEXT", "TEMP", "TMP", "USERPROFILE", "LOCALAPPDATA", "APPDATA"):
            if key in os.environ:
                env[key] = os.environ[key]
        try:
            self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                            cwd=stage, env=env, start_new_session=True)
        except BaseException:
            self.temp.cleanup()
            raise
        threading.Thread(target=self._read_output, daemon=True).start()
        threading.Thread(target=self._read_errors, daemon=True).start()
        try:
            ready_until = min(time.monotonic() + STARTUP_LIMIT[sandbox], deadline or float("inf"))
            while True:
                remaining = ready_until - time.monotonic()
                if remaining <= 0 or (stop_event and stop_event.is_set()):
                    raise TimeoutError("Policy initialization deadline reached")
                try:
                    ready = self.lines.get(timeout=min(.1, remaining))
                    break
                except queue.Empty:
                    continue
            if json.loads(ready) != {"ready": True}:
                raise ValueError("Invalid ready response")
        except BaseException as exc:
            details = " ".join(self.logs)[-1500:]
            self.close()
            raise RuntimeError(f"Policy did not initialize: {details or str(exc)}") from exc

    def _read_output(self):
        try:
            while True:
                line = self.process.stdout.readline(16386)
                if not line:
                    break
                if len(line) > 16385 or not line.endswith(b"\n"):
                    line = b'{"error":"oversized protocol output"}\n'
                try:
                    self.lines.put_nowait(line)
                except queue.Full:
                    break
        finally:
            try:
                self.lines.put_nowait(b'{"error":"policy process exited"}\n')
            except queue.Full:
                pass

    def _read_errors(self):
        while True:
            chunk = self.process.stderr.read(512)
            if not chunk:
                return
            self.logs.append(chunk.decode("utf-8", errors="replace"))

    def decide(self, observation, message_limit=MAX_MESSAGE_BYTES):
        if self.disabled:
            return {}, None
        self.calls += 1
        try:
            if isinstance(observation, EncodedObservation):
                payload, controlled, has_messages = observation.payload, set(observation.controlled_ids), observation.has_messages
            else:
                payload = json.dumps(observation, allow_nan=False, separators=(",", ":")).encode() + b"\n"
                controlled, has_messages = set(observation["controlled_ids"]), "messages" in observation
            started = time.monotonic()
            if (self.deadline is not None and started >= self.deadline) or (self.stop_event and self.stop_event.is_set()):
                return {}, None
            until = min(started + self.timeout, self.deadline if self.deadline is not None else float("inf"))
            if self.pending_response is not None:
                # Only one request may be in flight. Discard its late response before
                # sending fresh observations, preserving private state without ever
                # applying old actions to a new tick or building a stdin backlog.
                self._receive(until, message_limit)
            if time.monotonic() >= until:
                raise queue.Empty
            self.pending_response = controlled, has_messages
            self.process.stdin.write(payload)
            self.process.stdin.flush()
            actions = self._receive(until, message_limit)
            self.consecutive_timeouts = 0
            return actions, None
        except queue.Empty:
            self.errors += 1
            self.consecutive_timeouts += 1
            message = "decision exceeded deadline"
            self.logs.append(message)
            if self.consecutive_timeouts >= POLICY_TIMEOUT_LIMIT:
                self.close()
            return {}, message
        except Exception as exc:
            self.errors += 1
            message = str(exc)
            self.logs.append(message)
            self.close()
            return {}, message

    def _receive(self, until, message_limit=MAX_MESSAGE_BYTES):
        remaining = until - time.monotonic()
        if remaining <= 0:
            raise queue.Empty
        raw = self.lines.get(timeout=remaining)
        controlled, has_messages = self.pending_response
        self.pending_response = None
        response = json.loads(raw, parse_constant=lambda s: (_ for _ in ()).throw(ValueError("non-finite JSON")))
        if response.get("error"):
            raise ValueError(response["error"])
        actions = response.get("actions")
        actions = validate_actions(actions, controlled, has_messages, getattr(self, 'dynamic_messages', False))
        for slot, action in list(actions.items()):
            if "messages" in action:
                # 512 bytes is the protocol ceiling and stays a hard error. The 2027 logistics
                # bandwidth can be lower, and a message that no longer fits is dropped rather than
                # disabling the policy: the team chose to spend the base HP that carried that data.
                kept = limit_messages(action["messages"], message_limit)
                if len(kept) != len(action["messages"]):
                    self.logs.append(f"bandwidth: dropped {len(action['messages']) - len(kept)} message(s) over {message_limit} bytes")
                    action = {**action, "messages": kept}
                    actions[slot] = action
        # Queue.get can return an item after its timeout if the host thread was descheduled.
        # The response is consumed, so the next call can send a fresh observation; late actions
        # still must not be applied to this tick.
        if time.monotonic() >= until:
            raise queue.Empty
        return actions

    def close(self):
        self.disabled = True
        if hasattr(self, "process"):
            if self.sandbox == "docker":
                # Killing docker's client alone does not always kill its container.
                try:
                    subprocess.run(["docker", "rm", "-f", self.name], stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL, timeout=5)
                except (OSError, subprocess.TimeoutExpired):
                    pass
                try:
                    self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    pass
            if self.process.poll() is None:
                try:
                    if self.sandbox == "docker":
                        self.process.kill()
                    else:
                        kill_process_tree(self.process)
                except (ProcessLookupError, PermissionError):
                    # Container deletion above is authoritative. Never turn a
                    # completed match into a failure by killing an exited group.
                    try:
                        self.process.terminate()
                    except (ProcessLookupError, PermissionError):
                        pass
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                pass
            for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
                try:
                    stream.close()
                except (OSError, ValueError):
                    pass
        self.temp.cleanup()


class Battle:
    def __init__(self, red, blue, seed=42, duration=420, map_name=None, sandbox="docker", timeout=0.2, deadline=None, stop_event=None, decks=None, team_lineup=None, team_lineups=None, practice_lineup=False, free_team_strategies=None, battle_settings=None, record=True):
        # The field follows the strategies' rules season unless the caller names it.
        if map_name is None:
            from .submissions import manifest_season
            sources = [s for s in (red, blue) if s is not None] or [cards[0] for cards in (decks or {}).values() if cards]
            found = {manifest_season(load_submission(s)[0]) for s in sources}
            map_name = season_rules.map_of(found.pop() if len(found) == 1 else season_rules.LEGACY_SEASON)
        season = season_rules.season_of_map(map_name)
        self.season, rules_profile, slot_ids = season, season_rules.rules_profile(season), season_rules.slots(season)
        lineups, deck_policies, deck_metadata = prepare_decks(decks, seed, free_team_strategies, season) if decks is not None else (None, None, None)
        if decks is None:
            if free_team_strategies:raise ValueError("Free controllers require free battle")
            from .models import normalize_team_lineup,lineup_deployments
            manifests=[load_submission(source)[0] for source in (red,blue)]
            if any(m.get("rules_profile") != rules_profile for m in manifests):
                raise ValueError(f"Both strategies must be written for the {season} season to play on {map_name}")
            # Each side keeps the lineup its own strategy was developed on; a side without one runs
            # the requested lineup. The two sides never need the same hardware, only a complete
            # lineup for the season.
            shared=normalize_team_lineup(team_lineup,season) if team_lineup is not None else None
            chosen={}
            for team,manifest in zip(("red","blue"),manifests):
                locked=manifest.get('team_lineup')
                requested=(team_lineups or {}).get(team)
                lineup=(shared if practice_lineup and shared is not None else
                        locked if locked is not None else
                        requested if requested is not None else shared)
                chosen[team]=normalize_team_lineup(lineup,season)
            if season!=season_rules.LEGACY_SEASON or any(chosen[team]!=season_rules.default_lineup(season) for team in ("red","blue")):
                lineups={team:lineup_deployments(chosen[team],season) for team in ("red","blue")}
        elif team_lineup is not None or team_lineups:raise ValueError('Decks and team lineups cannot be combined')
        from .battle_settings import normalize
        battle_settings = normalize(battle_settings or {}, season)
        self.match, self.sandbox, self.policies, self.closed = Match(seed, duration, map_name, lineups=lineups, battle_settings=battle_settings), sandbox, [], False
        self.deck_mode = decks is not None
        self.deadline, self.stop_event = deadline, stop_event
        self.pool = ThreadPoolExecutor(max_workers=24 if self.deck_mode else 12)
        # One decision in flight per policy, keyed by the worker: a slow agent is skipped on the next
        # policy tick instead of being asked twice, which keeps its single-request protocol intact.
        self.inflight = {}
        # Policies that already received the match-constant part of an observation (the arena).
        self.static_sent = set()
        self.metadata = {"api_version": 3, "engine": "rust", "rules": rules_profile, "season": season, "decision_timeout_s":timeout, "policy_timeout_limit":POLICY_TIMEOUT_LIMIT, "roster":list(slot_ids), "test_match":duration!=420, "dt": DT, "policy_interval": POLICY_INTERVAL, "seed": seed, "duration": duration,
                         "map": map_name, "sandbox": sandbox, "match_mode":"deck" if self.deck_mode else "standard", "teams": {}}
        if battle_settings:
            self.metadata.update({"battle_settings": battle_settings, "test_match": True})
        self.record = record
        self.frames = (FrameLog if record else LatestFrameLog)(self.match.snapshot())
        self.actions, self.diagnostics = [], []
        self.metadata["rules_hash"] = self.frames[0]["rules_hash"]
        self.metadata["engine_hash"] = self.frames[0]["engine_hash"]
        self.held_commands = {"red": {}, "blue": {}}
        # A human pilot's live commands, keyed (team, slot) and re-applied until they go stale.
        self.manual, self.manual_lock = {}, threading.Lock()
        # Ammunition orders wait here until a tick the rulebook actually reads (every second tick).
        self.pending_orders = {}
        # Last published gun elevation per piloted robot: a lobbed round is only released once the
        # launcher has stopped climbing, otherwise the frame's one-tick lag is a few degrees of arc.
        self.lob_ready = {}
        # The solved lob aim a `lob_ready` deadline belongs to, so a moving reticle re-arms the climb.
        self.lob_ready_aim = {}
        self.communication = TeamCommunication({team:[d["slot"] for d in items] for team, items in lineups.items()} if lineups else None, slot_ids)
        if self.deck_mode:
            self.metadata.update({"lineups":lineups,"test_match":True,"roster":{team:[d["slot"] for d in items] for team,items in lineups.items()}})
        else:
            if lineups:
                self.metadata.update({"lineups":lineups,"roster":{team:[d["slot"] for d in items] for team,items in lineups.items()}})
            # A replay needs one lineup per side; keep the shared key only when both happen to match.
            if chosen["red"]==chosen["blue"]:
                self.metadata['team_lineup']=chosen["red"]
            else:
                self.metadata['red_lineup'],self.metadata['blue_lineup']=chosen["red"],chosen["blue"]
            self.metadata['test_match']=duration!=420 or practice_lineup or bool(battle_settings) or "lineups" in self.metadata
        self.pending_referee = []
        self.metadata["agents"] = {}
        try:
            for team, source in (("red", red), ("blue", blue)):
                if self.deck_mode:
                    self.metadata["teams"][team] = deck_metadata[team]
                    controller = (free_team_strategies or {}).get(team)
                    cards = deck_metadata[team]["cards"]
                    self.metadata["agents"][team] = {str(d["slot"]): ({**deck_metadata[team]["agent"], "strategy": deck_metadata[team].get("display_name") or Path(controller).name}
                        if controller else {**card["agent"], "strategy": card.get("display_name") or card["name"]}) for d, card in zip(lineups[team], cards)}
                    for slot, path, digest in deck_policies[team]:
                        if self.cancelled():
                            raise TimeoutError("Practice development window ended")
                        policy = Policy(path, seed*100+(0 if team == "red" else 10000)+(slot or 0), sandbox, timeout, expected_digest=digest,
                                        deadline=deadline,stop_event=stop_event,dynamic_messages=True)
                        self.policies.append((team,None if slot is None else [slot],policy))
                    continue
                manifest, entries = load_submission(source)
                if manifest.get("api_version") != 3 or manifest.get("rules_profile") != rules_profile:
                    raise ValueError(f"This submission does not use the {season} rules; create and submit a {season} strategy workspace")
                if manifest["mode"] not in ("team", "squad", "distributed"):
                    raise ValueError("Assemble robot submissions into a squad before matching")
                profile = load_profile(source)
                self.metadata["teams"][team] = {"name": Path(source).name, "version": manifest["version"], "mode": manifest["mode"], "agent": profile["agent"],
                                                **({"display_name": profile["display_name"]} if profile.get("display_name") else {})}
                self.metadata["agents"][team] = {str(slot): {**profile["agent"], "strategy": profile.get("display_name") or Path(source).name} for slot in slot_ids}
                # Each process sees exactly one strategy. Robot policy seed is stable per slot.
                for key, path in sorted(entries.items()):
                    if self.cancelled():
                        raise TimeoutError("Practice development window ended")
                    slots = None if manifest["mode"] == "team" else [int(key)]
                    policy = Policy(path, seed * 100 + (0 if team == "red" else 10) + (int(key) if slots else 0), sandbox, timeout,
                                    expected_digest=manifest["hashes"][manifest["entries"][key]],deadline=deadline,stop_event=stop_event)
                    self.policies.append((team, slots, policy))
        except BaseException:
            self.close()
            raise

    def cancelled(self):
        return (self.deadline is not None and time.monotonic() >= self.deadline) or bool(self.stop_event and self.stop_event.is_set())

    def set_manual(self, team, slot, action, assist=None):
        """Hold `action` for one robot of `team`; `action=None` hands it back to its strategy.

        `assist` names an enemy robot the pilot is locking onto: the gimbal then follows the
        engine's own ballistic solution, exactly like `sdk.aim_at`/`sdk.aim_pitch` in a policy.
        """
        if action is None:
            with self.manual_lock:
                self.manual.pop((team, str(slot)), None)
            return
        robot = next((item for item in self.match.robots
                      if item["team"] == team and str(item["slot"]) == str(slot)), None)
        if robot is None:
            raise ValueError("Robot is not on the field")
        if robot.get("role") == "aerial":
            raise ValueError("空中机器人由自动飞行系统控制，不能手动驾驶")
        validated = validate_action(action)
        # The rulebook reads orders every second tick, so an order that only rides on one 50 ms
        # command would be dropped half the time. Queue it and let `_manual_commands` deliver it on
        # a tick the engine reads, exactly once.
        key = (team, str(slot))
        order = validated.pop("buy_ammo", None)
        if order is not None:
            with self.manual_lock:
                self.pending_orders[key] = (order, time.monotonic())
        with self.manual_lock:
            self.manual[key] = (validated, time.monotonic(), assist)

    def _manual_commands(self):
        """Live pilot actions by team. A piloted robot never falls back to its strategy while the
        pilot is still connected: a brief gap only brakes it, and silence for `PLAYER_TIMEOUT`
        hands it back."""
        now = time.monotonic()
        with self.manual_lock:
            for key, (_, sent, _assist) in list(self.manual.items()):
                if now - sent > PLAYER_TIMEOUT:
                    del self.manual[key]
            held = dict(self.manual)
        if not held:
            return {}
        robots = {(item["team"], str(item["slot"])): item for item in self.match.robots}
        # Outposts and bases are targets too: their published armour plates are ordinary plates.
        structures = {f"{team}_base": {**base, "kind": "base"} for team, base in self.match.bases.items()}
        for team, outpost in (self.match.outposts or {}).items():
            structures[f"{team}_outpost"] = {**outpost, "kind": "outpost"}
        commands = {}
        for (team, slot), (action, sent, assist) in held.items():
            if now - sent > PLAYER_COAST:
                # Keep the gun where the pilot left it, but stop driving and shooting.
                action = {key: action[key] for key in ("aim", "pitch") if key in action}
                action.update({"move": [0.0, 0.0], "fire": False, "brake": True})
            else:
                action = dict(action)
            live = robots.get((team, slot))
            if live is not None and action.get("fire") and next_shot_overheats(live):
                # The engine adds heat after a shot and locks the barrel when the total crosses its
                # limit, so a held trigger could overheat on a frame the cockpit has not seen yet.
                # Drop the shot before that round: the last line of defence behind the cockpit's
                # own heat guard.
                action["fire"] = False
            if assist:
                aimed = aim_assist(robots.get((team, slot)), assist, robots, self.match.arena,
                                   action.get("aim"), structures, action.get("pitch"))
                if aimed:
                    action["aim"], action["pitch"] = aimed
            # Lobbed fire: the cockpit sends the world point under its crosshair and the shell has to
            # land there, so the elevation is solved on the steep branch. The engine never reads the
            # point itself, so it is dropped before the action reaches it.
            point = action.pop("aim_point", None)
            if point:
                from . import sdk
                solved = sdk.lob_aim(live, point) if live is not None else None
                if solved:
                    action["aim"], action["pitch"] = solved
                    # The launcher still has to arrive on the solved elevation: a round fired on the
                    # same tick would leave at the angle the gun still had, and the drop solve would
                    # be wrong — that is the shell that falls into the ground a few metres short. A
                    # reticle that moves solves a new arc, so the climb clock starts over.
                    key = (team, slot)
                    issued = self.lob_ready_aim.get(key)
                    if issued is None or abs(issued[0] - solved[0]) > LOB_READY_RAD or abs(issued[1] - solved[1]) > LOB_READY_RAD:
                        self.lob_ready_aim[key] = solved
                        self.lob_ready[key] = now + lob_climb_seconds(live, solved[1])
                    ready = self.lob_ready.get(key)
                    if action.get("fire") and ready is not None and now < ready:
                        action["fire"] = False
                else:
                    # No elevation puts the shell on that point any more: the flat arc is out of
                    # reach there, so the trigger waits instead of lobbing a round into the ground.
                    action["fire"] = False
            # An ammunition order waits for a tick the rulebook reads (`tick % 2 == 0`), so a single
            # 50 ms command can never be skipped, and it is delivered only once.
            order = self.pending_orders.get((team, slot))
            if order is not None:
                if now - order[1] > 2.0:
                    del self.pending_orders[(team, slot)]
                elif self.match.tick % 2 == 0:
                    action["buy_ammo"] = order[0]
                    del self.pending_orders[(team, slot)]
            commands.setdefault(team, {})[slot] = action
        return commands

    def queue_referee(self, command):
        value = validate_referee(command, [robot["id"] for robot in self.match.robots])
        if self.match.status == "finished":
            raise ValueError("The match has ended")
        if len(self.pending_referee) >= 16:
            raise ValueError("Too many pending referee decisions")
        self.pending_referee.append(value)

    def observation(self, team, slots):
        observation = self.match.observation(team, slots)
        inboxes={slot:self.communication.inboxes[team][slot] for slot in observation["controlled_ids"]}
        observation["message_inboxes"] = inboxes
        observation["messages"] = [message for inbox in inboxes.values() for message in inbox]
        return observation

    def message_byte_limit(self, team):
        """The team's current per-message data budget (table 5-19 scales it with base HP)."""
        return message_byte_limit(self.match.bases.get(team), self.frames.static.get("rulebook_rules"))

    def encoded_observations(self, policies, hoist_static=True):
        """The same observations as `observation`, fetched together and left encoded.

        With `hoist_static` the arena is sent once per policy and cached in its sandbox: it is
        identical for every decision of a match, and resending it cost 66 KB of pipe traffic and
        about 2 ms of decoding per decision per policy. `hoist_static=False` is the previous
        behaviour, kept so a test can prove both transports feed policies the same observation.
        """
        controlled, lines = self.match.observations([(team, slots) for team, slots, _ in policies], static=not hoist_static)
        encoded = []
        for (team, _, policy), ids, line in zip(policies, controlled, lines):
            inboxes = {slot: self.communication.inboxes[team][slot] for slot in ids}
            messages = [message for inbox in inboxes.values() for message in inbox]
            # Append both keys last, in the order `observation` adds them.
            tail = ',"message_inboxes":' + json.dumps(inboxes, allow_nan=False, separators=(",", ":")) + ',"messages":' + json.dumps(messages, allow_nan=False, separators=(",", ":"))
            payload = line.rstrip(b"\n")[:-1] + tail.encode() + b"}\n"
            static = b""
            if hoist_static and policy not in self.static_sent:
                self.static_sent.add(policy)
                # `{"__static__":{...arena...},` + the observation's own keys.
                static = b'{"__static__":' + self.match.observation_static().encode() + b","
                payload = static + payload[1:]
            encoded.append(EncodedObservation(payload, ids, True, static))
        return encoded

    def step(self, budget: float | None = None):
        """Advance one engine tick.

        `budget` bounds how long a policy decision may hold this tick. Left unset, a decision is
        awaited to completion, which is what headless runs, tests and replay verification want. The
        live loop passes the tick's own budget: an agent that needs longer keeps its previous command
        for this tick and the clock keeps running, instead of the whole field freezing for the
        decision (a 200 ms LLM call used to stall four ticks every other tick).
        """
        if self.match.status == "finished":
            return self.frames[-1]
        if self.cancelled():
            self.match.finish(None, "stopped")
            self.frames[-1] = self.match.snapshot()
            return self.frames[-1]
        policy_tick = bool(self.policies) and self.match.tick % POLICY_INTERVAL == 0
        # Disabled policies return no actions, so they need no observation. A policy that is still
        # deciding is skipped as well: `PolicyWorker` owns one subprocess and one in-flight request,
        # so a second concurrent `decide` would corrupt its protocol. Skipping keeps that invariant
        # while the clock keeps running.
        deciding = []
        if policy_tick:
            for item in self.policies:
                p = item[2]
                if p.disabled:
                    continue
                pending = self.inflight.get(p)
                if pending is not None:
                    if not pending.done():
                        continue
                    # Its answer missed the tick it was asked for, so it is not applied at all.
                    self.inflight.pop(p, None)
                deciding.append(item)
        futures = []
        if deciding:
            for (team, slots, p), observation in zip(deciding, self.encoded_observations(deciding)):
                future = self.pool.submit(p.decide, observation, self.message_byte_limit(team))
                self.inflight[p] = future
                futures.append((team, slots, p, future, time.monotonic()))
        commands, failures = ({"red": {}, "blue": {}} if policy_tick else self.held_commands), []
        outgoing = []
        for team, slots, p, future, submitted in futures:
            wait = None if budget is None else max(0.0, submitted + budget - time.monotonic())
            try:
                actions, error = future.result(timeout=wait)
                self.inflight.pop(p, None)
            except FutureTimeout:
                # The team keeps the command it had — for this policy's own robots only, so a
                # per-robot policy that did answer this tick keeps its fresh action. The worker
                # discards its late answer before the next request, so nothing stale is applied.
                held = self.held_commands.get(team, {})
                for key, action in held.items():
                    if slots is None or str(key) in {str(slot) for slot in slots}:
                        commands[team].setdefault(key, action)
                if not getattr(p, "lag_reported", False):
                    p.lag_reported = True
                    self.diagnostics.append({"type": "policy_lag", "id": f"policy-lag:{team}:{slots}:{self.match.tick}",
                        "tick": self.match.tick, "time": self.match.time, "team": team, "slots": slots,
                        "budget_s": budget})
                continue
            for slot, action in actions.items():
                action = dict(action)
                messages = action.pop("messages", [])
                if messages:
                    outgoing.append((team, slot, messages))
                commands[team][slot] = action
            if error:
                scope = "team" if slots is None else "-".join(map(str, slots))
                detail = {"type": "policy_error", "id": f"policy:{team}:{scope}:{self.match.tick}",
                          "tick": self.match.tick, "time": self.match.time, "team": team, "slots": slots,
                          "disabled": p.disabled, "consecutive_timeouts": p.consecutive_timeouts,
                          "message": error, "logs": list(p.logs)[-8:]}
                self.diagnostics.append(detail)
                failures.append(detail)
        self.held_commands = commands
        # A pilot overrides the strategy for their own robot only, every tick, so the
        # policy's held actions keep driving the rest of the team.
        manual = self._manual_commands()
        if manual:
            commands = {team: {**slots, **manual.get(team, {})} for team, slots in commands.items()}
        communications = self.communication.route(outgoing, self.match.tick, self.match.tick + POLICY_INTERVAL) if policy_tick else []
        if self.pending_referee:
            commands = {**commands, "referee": self.pending_referee}
            self.pending_referee = []
        # Communication is recorded alongside actions, but never sent to physics
        # or repeated during the held-action tick. Replay verification uses the
        # recorded robot commands; strategies are not re-executed.
        if self.record:
            self.actions.append({**commands, "communication": communications} if communications else commands)
        frame = self.match.step(commands)
        frame["events"].extend(failures)
        # Reuse the engine's encoding unless policy errors were added to the events.
        self.frames.append(frame, None if failures else self.match.raw_frame)
        return frame

    def run(self):
        while self.match.status != "finished":
            self.step()
        return self.result()

    def result(self):
        return {"winner": self.match.winner, "reason": self.match.reason, "time": self.match.time,
                "teams": self.match.snapshot()["teams"], "digest": getattr(self, "_digest_cache", None) or self.match.digest(), "policy_errors": len(self.diagnostics)}

    def replay(self):
        if not self.record:
            raise ValueError("Replay recording was disabled for this match")
        return {"format": "rmuc-replay-v4", "metadata": self.metadata, "static":self.frames.static,"frames": [json.loads(raw) for raw in self.frames.encoded()],
                "actions": self.actions, "diagnostics": self.diagnostics, "result": self.result()}

    def save(self, path):
        if not self.record:
            raise ValueError("Replay recording was disabled for this match")
        from .replays import write_replay
        write_replay(path, self.metadata, self.frames, self.actions, self.diagnostics, self.result())

    def close(self):
        if self.closed:
            return
        self._digest_cache = self.match.digest()
        self.closed = True
        with self.manual_lock:
            self.manual.clear()
            self.pending_orders.clear()
        self.pool.shutdown(wait=True, cancel_futures=True)
        if self.policies:
            with ThreadPoolExecutor(max_workers=12) as pool:
                list(pool.map(lambda item: item[2].close(), self.policies))
        self.match.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
