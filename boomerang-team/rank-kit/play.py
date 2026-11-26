"""编译 C++17 Bot 和陪练，打完一局后输出回放。"""
from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
WINDOWS = os.name == "nt"
EXE = ".exe" if WINDOWS else ""


def bot_command(path: Path) -> str:
    command = [f"./{path.as_posix()}"]
    return subprocess.list2cmdline(command) if WINDOWS else shlex.join(command)


def play() -> None:
    config = json.loads((ROOT / "config.json").read_text(encoding="utf-8"))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vs", choices=config["levels"], default="weak", help="陪练强度")
    parser.add_argument("--map", choices=config["maps"], default=config["maps"][0], help="地图")
    parser.add_argument("--seed", type=int, default=1, help="地图种子")
    parser.add_argument("--source", type=Path, default=ROOT / "player.cpp", help="选手的 C++17 源码")
    parser.add_argument("--swap", action="store_true", help="让选手坐 1 号位")
    args = parser.parse_args()
    if not 1 <= args.seed < (1 << 64):
        parser.error("--seed 必须在 1 到 2^64-1 之间")
    gxx = shutil.which("g++")
    if not gxx:
        parser.error("找不到 g++，请按 README.md 安装支持 C++17 的 GCC 并加入 PATH")

    runs = ROOT / "runs"
    runs.mkdir(exist_ok=True)
    # A distinct directory keeps old results and avoids collisions when running in parallel.
    run = Path(tempfile.mkdtemp(prefix="duel-", dir=runs)).relative_to(ROOT)
    player, sparring, referee = (run / (name + EXE) for name in ("player", "sparring", "arena"))
    # Use paths relative to the extracted folder for GCC and the Windows referee.
    source = run / "player.cpp"
    source_path = args.source.resolve()
    if not source_path.is_file():
        parser.error(f"找不到源码 {source_path}")
    shutil.copyfile(source_path, ROOT / source)

    def compile_cpp(flags: list[str], source: str | Path, dest: Path) -> None:
        subprocess.run([gxx, *flags, "-I", ".", str(source), "-o", str(dest)], cwd=ROOT, check=True)

    compile_cpp(config["compile_flags"], source, player)
    compile_cpp(config["compile_flags"], f"sparring/{args.vs}.cpp", sparring)
    compile_cpp(["-std=c++17", "-O2"], "arena.cpp", referee)
    replay, result = run / "replay.txt", run / "result.json"
    entrants = [(player, "Player"), (sparring, args.vs)]
    if args.swap:
        entrants.reverse()
    limits = config["limits"]
    command = [
        str(ROOT / referee), "--mode", "0", "--map", f"maps/{args.map}.txt", "--seed", str(args.seed),
        "--step-ms", str(limits["step_cpu_ms"]), "--first-ms", str(limits["first_cpu_ms"]),
        "--match-cpu-ms", str(round(limits["match_cpu_limit_sec"] * 1000)),
        "--match-wall-ms", str(round(limits["match_wall_limit_sec"] * 1000)),
        "--mem-mb", str(limits["mem_mb"]), "--stack-mb", str(limits["stack_mb"]),
        "--replay", str(replay), "--result", str(result),
    ]
    for program, name in entrants:
        command += ["--bot", bot_command(program), "--name", name]
    subprocess.run(command, cwd=ROOT, check=True, timeout=limits["match_wall_limit_sec"] + 20)

    summary = json.loads((ROOT / result).read_text(encoding="utf-8"))
    winner = summary["winner"]
    print("结果", "平局" if winner == -1 else f"{entrants[winner][1]} 获胜", f"（{summary['steps']} 步）")
    print("回放", ROOT / replay)
    print("在浏览器打开 viewer.html，选择上面的回放文件。")


if __name__ == "__main__":
    try:
        play()
    except (OSError, subprocess.SubprocessError) as exc:
        raise SystemExit(f"对局未完成。{exc}") from exc
