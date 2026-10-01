"""对战平台核心：编译并启动选手程序，逐回合同时收发，运行单局并保存回放。

所有游戏共用。游戏模块需要提供一个 Game 类：
    Game(n, seed)            n 个参赛方，seed 决定地图
    game.done、game.turn     是否结束、已结算的回合数
    game.encode(i)           发给第 i 方的本回合输入（文本，以换行结尾）
    game.decode(i, line)     把第 i 方的一行回复解析成指令（dict）；格式不对时抛出 ValueError
    game.step(orders)        orders[i] 为第 i 方的指令（缺席为 {}），返回本回合帧（dict）
    game.header()            回放头部：地图等静态信息（dict）
    game.frame()             （可选）当前局面，用作回放的第 0 帧
    game.result()            终局：至少含 "score"（每方得分，越高越好）
    Game.TIME_LIMITS         （可选）(首回合秒数, 之后每回合秒数)，默认 (2.0, 0.05)

选手程序可以是 .cpp（自动编译）、可执行文件或 .py。
"""
from __future__ import annotations

import hashlib
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from collections import deque
from pathlib import Path

DEFAULT_LIMITS = (2.0, 0.05)
MAX_STRIKES = 3          # 连续失败次数达到此值即判故障出局
MAX_LINE = 65536
CXX_FLAGS = ["-std=c++20", "-O2"] + (["-static"] if os.name == "nt" else [])


def find_compiler():
    cxx = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if cxx:
        return cxx
    if os.name == "nt":   # 没加进 PATH 的 w64devkit：仓库根目录下的 tools/，或 C 盘根目录
        here = Path(__file__).resolve()
        for base in [*(p / "tools" for p in here.parents), Path("C:/")]:
            guess = base / "w64devkit" / "bin" / "g++.exe"
            if guess.is_file():
                return str(guess)
    return None


def compile_cpp(source, build_dir):
    """编译 C++ 源码，按内容哈希缓存。返回可执行文件路径；失败时抛出 RuntimeError 并附编译器输出。"""
    source = Path(source)
    cxx = find_compiler()
    if not cxx:
        raise RuntimeError("没有找到 C++ 编译器（g++ 或 clang++）")
    digest = hashlib.sha256(source.read_bytes() + " ".join(CXX_FLAGS).encode()).hexdigest()[:12]
    build_dir = Path(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)
    exe = build_dir / f"{source.stem}-{digest}{'.exe' if os.name == 'nt' else ''}"
    if exe.is_file():
        return exe
    tmp = exe.with_name(exe.stem + ".tmp" + exe.suffix)
    try:
        r = subprocess.run([cxx, *CXX_FLAGS, str(source), "-o", str(tmp)], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=120)
    except subprocess.TimeoutExpired:
        raise RuntimeError("编译超过 120 秒")
    if r.returncode != 0:
        raise RuntimeError("编译失败：\n" + (r.stderr or r.stdout)[-3000:])
    os.replace(tmp, exe)
    return exe


def command_for(program, build_dir):
    p = Path(program)
    if p.suffix.lower() in (".cpp", ".cc", ".cxx"):
        return [str(compile_cpp(p, build_dir))]
    if p.suffix.lower() in (".py", ".pyc"):
        return [sys.executable, "-X", "utf8", str(p)]
    return [str(p)]


class Bot:
    """一个选手进程。每局以一个全新的临时目录作为工作目录。"""

    def __init__(self, command, name, limits):
        self.name = name
        self.first_limit, self.turn_limit = limits
        self.strikes = 0
        self.dead = None          # 出局原因
        self.faults = []          # [(回合, 原因)]
        self.calls = 0
        self.stale = 0            # 已超时、尚未读到的迟到回复数，读到后丢弃
        self.stderr = deque(maxlen=20)
        self.first_seconds = 0.0  # 第一回合（含启动）单独记录
        self.total_seconds = 0.0
        self.max_seconds = 0.0
        self.lines = queue.Queue()
        self.dir = Path(tempfile.mkdtemp(prefix="arena_"))
        try:
            self.proc = subprocess.Popen(command, cwd=self.dir, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace",
                                         bufsize=1)
        except OSError as exc:
            self.proc = None
            self.dead = f"无法启动：{exc}"
            return
        threading.Thread(target=self._read_stdout, daemon=True).start()
        threading.Thread(target=self._read_stderr, daemon=True).start()

    def _read_stdout(self):
        try:
            while True:
                line = self.proc.stdout.readline(MAX_LINE + 1)
                if not line:
                    break
                self.lines.put((time.perf_counter(), line))
        except (OSError, ValueError):
            pass
        self.lines.put((time.perf_counter(), None))

    def _read_stderr(self):
        try:
            while True:
                line = self.proc.stderr.readline(4096)
                if not line:
                    break
                self.stderr.append(line.rstrip())
        except (OSError, ValueError):
            pass

    def send(self, text):
        """写入本回合输入，返回开始计时的时刻。"""
        if self.dead:
            return None
        try:
            self.proc.stdin.write(text)
            self.proc.stdin.flush()
        except (OSError, ValueError):
            self._kill(f"进程已退出（代码 {self.proc.poll()}）")
            return None
        return time.perf_counter()

    def receive(self, started, turn):
        """在时限内读取一行回复，返回这一行；失败时返回 None 并记一次失败。"""
        if self.dead or started is None:
            return None
        limit = self.first_limit if self.calls == 0 else self.turn_limit
        self.calls += 1
        deadline = started + limit
        while True:
            try:
                arrived, line = self.lines.get(timeout=max(0.0, deadline - time.perf_counter()))
            except queue.Empty:
                self.stale += 1
                return self.strike(turn, f"超时（>{limit * 1000:g} 毫秒）")
            if line is None:
                self._kill(f"进程已退出（代码 {self.proc.poll()}）", turn)
                return None
            if self.stale:
                self.stale -= 1
                continue
            if arrived > deadline:
                return self.strike(turn, f"超时（>{limit * 1000:g} 毫秒）")
            break
        spent = arrived - started
        if self.calls == 1:
            self.first_seconds = spent
        else:
            self.total_seconds += spent
            self.max_seconds = max(self.max_seconds, spent)
        if len(line) > MAX_LINE:
            return self.strike(turn, "输出一行超过 64 KiB")
        return line

    def ok(self):
        self.strikes = 0

    def strike(self, turn, reason):
        self.strikes += 1
        self.faults.append((turn, reason))
        if self.strikes >= MAX_STRIKES:
            self._kill(f"连续 {MAX_STRIKES} 次失败，最后一次：{reason}", turn)
        return None

    def _kill(self, reason, turn=None):
        if not self.dead:
            self.dead = reason
            if turn is not None:
                self.faults.append((turn, reason))
        self.close()

    def close(self):
        p = self.proc
        if p is not None and p.poll() is None:
            p.kill()
            try:
                p.wait(timeout=2)
            except subprocess.TimeoutExpired:
                pass
        if p is not None:
            for pipe in (p.stdin, p.stdout, p.stderr):
                try:
                    pipe.close()
                except (OSError, ValueError):
                    pass
        shutil.rmtree(self.dir, ignore_errors=True)

    def report(self):
        return {"name": self.name, "dead": self.dead, "faults": self.faults,
                "first_ms": round(1000 * self.first_seconds, 1),
                "avg_ms": round(1000 * self.total_seconds / max(1, self.calls - 1), 2),
                "max_ms": round(1000 * self.max_seconds, 2),
                "stderr_tail": list(self.stderr)[-5:]}


def run_match(game_cls, commands, names, seed, record=True):
    """运行一局。commands[i]（由 command_for 得到）坐第 i 号位。返回 (结果, 回放或 None)。"""
    game = game_cls(len(commands), seed)
    limits = getattr(game_cls, "TIME_LIMITS", DEFAULT_LIMITS)
    bots = [Bot(c, n, limits) for c, n in zip(commands, names)]
    frames = [game.frame()] if record and hasattr(game, "frame") else []
    try:
        while not game.done:
            turn = game.turn + 1
            starts = [b.send(game.encode(i)) for i, b in enumerate(bots)]
            orders = {}
            for i, (b, s) in enumerate(zip(bots, starts)):
                line = b.receive(s, turn)
                if line is None:
                    orders[i] = {}
                    continue
                try:
                    orders[i] = game.decode(i, line)
                    b.ok()
                except ValueError as exc:
                    orders[i] = {}
                    b.strike(turn, f"输出格式错误：{exc}")
            frame = game.step(orders)
            for i, b in enumerate(bots):
                for t, reason in b.faults:
                    if t == turn:
                        frame.setdefault("faults", []).append({"team": i, "reason": reason})
            if record:
                frames.append(frame)
    finally:
        for b in bots:
            b.close()
    result = game.result()
    result.update(seed=seed, names=list(names), bots=[b.report() for b in bots])
    if not record:
        return result, None
    replay = {"header": game.header(), "names": list(names), "result": result, "frames": frames}
    return result, replay


def save_replay(replay, path, viewer=None):
    """保存回放 JSON；给出 viewer 模板时，另存一份可双击打开的 HTML。"""
    import json
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    raw = json.dumps(replay, ensure_ascii=False, separators=(",", ":"))
    path.with_suffix(".json").write_text(raw, encoding="utf-8")
    if viewer:
        html = Path(viewer).read_text(encoding="utf-8").replace("/*__REPLAY__*/null", raw.replace("</", "<\\/"))
        path.with_suffix(".html").write_text(html, encoding="utf-8")
