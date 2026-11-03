"""Thin Python transport to Rust; game rules live in simulation/src/lib.rs."""
import copy
import hashlib
import json
import os
from pathlib import Path
import queue
import subprocess
import threading

from .paths import ROOT
DT = 0.05
POLICY_INTERVAL = 2
_config = json.loads((ROOT / "config" / "rules.json").read_text(encoding="utf-8"))
SPECS = _config["specs"]
ROLES = ("hero", "engineer", "infantry", "infantry", "aerial", "infantry")
SLOT_IDS = (1, 2, 3, 4, 6, 7)
RULES_PROFILE = _config["version"]
MATCH_DURATION = 420
# Frame fields that never change during a match; compact engine steps leave them out.
STATIC_FRAME_KEYS = ("arena", "rulebook_rules", "simulation_assumptions", "robot_models")


def arena_map(name="rmuc2026"):
    try:
        return copy.deepcopy(_config["maps"][name])
    except KeyError:
        raise ValueError("Unknown map") from None


def binary_path():
    path = Path(os.environ.get("RMUC_SIM_BINARY", ROOT / "simulation/target/release" / ("rmuc-sim.exe" if os.name == "nt" else "rmuc-sim")))
    if not path.is_file():
        raise RuntimeError("Build Rust first: cargo build --release --manifest-path simulation/Cargo.toml")
    return path


class Match:
    def __init__(self, seed=42, duration=420, map_name="rmuc2026", replay_version=None, lineups=None, battle_settings=None, training_scenario=None):
        self.lock = threading.RLock()
        binary = ROOT / "legacy/v2/simulation/target/release" / ("rmuc-sim.exe" if os.name == "nt" else "rmuc-sim") if replay_version == 2 else binary_path()
        if not binary.is_file():
            raise RuntimeError("Build legacy/v2/simulation/Cargo.toml to verify a v2 replay")
        self.process = subprocess.Popen([str(binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL)
        self.closed = False
        self._responses = queue.Queue()
        # The reader must not retain Match itself: callers also use temporary
        # Match().observation(...) instances whose destructor closes the engine.
        self._reader = threading.Thread(target=self._read_output, args=(self.process.stdout, self._responses), daemon=True)
        self._reader.start()
        try:
            self._frame = self._request({"op": "init", "seed": int(seed), "duration": duration, "map": map_name, "lineups": lineups, "battle_settings": battle_settings, "training_scenario":training_scenario})
            self._static = {key: self._frame[key] for key in STATIC_FRAME_KEYS if key in self._frame}
            # The latest compact step's JSON, for callers that store frames without static fields.
            self.raw_frame = None
            rule_path = ROOT / "legacy/v2/config/rules.json" if replay_version == 2 else ROOT / "config/rules.json"
            expected = hashlib.sha256(rule_path.read_bytes()).hexdigest()
            if self._frame.get("rules_hash") != expected:
                raise RuntimeError("Rust engine contains old rules; rebuild simulation/Cargo.toml")
        except BaseException:
            self.close()
            raise

    def reset(self, seed=42, duration=420, map_name="rmuc2026", lineups=None, battle_settings=None, training_scenario=None):
        """Reset an existing simulator process for the next training episode."""
        with self.lock:
            frame = self._request({"op": "init", "seed": int(seed), "duration": duration,
                                   "map": map_name, "lineups": lineups, "battle_settings": battle_settings,
                                   "training_scenario":training_scenario})
            self._frame = frame
            self._static = {key: frame[key] for key in STATIC_FRAME_KEYS if key in frame}
            self._observation_static = None
            self.raw_frame = None
            return self.snapshot()

    def _request(self, request, raw=False):
        with self.lock:
            if self.closed:
                raise RuntimeError("Rust engine is closed")
            payload = json.dumps(request, allow_nan=False, separators=(",", ":")).encode() + b"\n"
            self.process.stdin.write(payload)
            self.process.stdin.flush()
            line = self._read_line()
            value = json.loads(line)
            if value.get("error"):
                raise ValueError(value["error"])
            return (value, line) if raw else value

    @staticmethod
    def _read_output(stream, responses):
        try:
            while True:
                line = stream.readline(4 * 1024 * 1024)
                responses.put(line)
                if not line:
                    return
        except (OSError, ValueError):
            responses.put(b"")

    def _read_line(self):
        try:
            line = self._responses.get(timeout=10)
        except queue.Empty:
            raise RuntimeError("Rust engine response timed out") from None
        if not line:
            raise RuntimeError("Rust engine exited unexpectedly")
        return line

    def __getattr__(self, name):
        if name in ("tick", "time", "status", "winner", "reason", "seed", "duration", "arena", "robots", "bases", "outposts", "teams", "zones", "events", "projectiles"):
            return self._frame[name]
        raise AttributeError(name)

    def step(self, commands):
        frame, line = self._request({"op": "step", "commands": commands, "static": False}, raw=True)
        if any(key in frame for key in STATIC_FRAME_KEYS):
            # Engines without compact steps (legacy v2) still send whole frames.
            self._frame, self.raw_frame = frame, None
        else:
            # Restore the static fields, shared between frames, in the engine's sorted key order.
            self._frame = dict(sorted({**frame, **self._static}.items()))
            # A trailing newline is legal JSON whitespace and the replay log writes these records as
            # array elements, so the line is kept exactly as read instead of copying 45 KB per tick.
            self.raw_frame = line
        # Frames are treated as immutable once returned; callers may still extend their events.
        return {**self._frame, "events": list(self._frame["events"])}

    def observation(self, team, slots=None):
        return self._request({"op": "observation", "team": team, "slots": slots})

    def observation_static(self):
        """The match-constant part of an observation as one encoded key set.

        A policy receives this once; every later observation then arrives without the arena and
        the two are merged in the sandbox, so 66 KB of unchanged map stops crossing the pipe (and
        being decoded) on every decision.
        """
        static = getattr(self, "_observation_static", None)
        if static is None:
            static = self._observation_static = json.dumps(
                {"arena": self._static["arena"]},
                allow_nan=False, separators=(",", ":"))
        return static

    def observations(self, viewers, static=True):
        """Encoded observations for (team, slots) viewers in a single engine round trip.

        Returns each observation's controlled ids and its JSON line, which policies can
        receive as is instead of being decoded and encoded again. `static=False` omits the
        arena map: it never changes during a match, so a transport that has already sent it
        once (see `observation_static`) need not resend 66 KB with every decision.
        """
        with self.lock:
            request = {"op": "observations", "requests": [{"team": team, "slots": slots} for team, slots in viewers]}
            if not static:
                request["static"] = False
            header = self._request(request)
            # The engine builds every observation before replying, so the lines follow at once.
            return header["controlled_ids"], [self._read_line() for _ in viewers]

    def snapshot(self):
        return copy.deepcopy(self._frame)

    def digest(self):
        return self._request({"op": "digest"})["digest"]

    def finish(self, winner, reason):
        self._frame = self._request({"op": "finish", "winner": winner, "reason": reason})

    def close(self):
        if self.closed:
            return
        self.closed = True
        try:
            self.process.stdin.close()
        except (OSError, BrokenPipeError):
            pass
        try:
            self.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self._reader.join(timeout=2)
        self.process.stdout.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def __del__(self):
        if hasattr(self, "closed"):
            self.close()
