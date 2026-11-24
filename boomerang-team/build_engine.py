"""把选手包补成可运行状态：编译裁判 workspace/arena.exe 和三档陪练 workspace/bots/weak.exe、mid.exe、strong.exe。

    python build_engine.py

比赛时选手包里这四个是编译好的程序；开源仓库不提交 .exe，改为从源码一键编译：
- arena.exe：engine/arena.cpp + engine/json.hpp + workspace/dart.hpp
- 陪练：engine/sparring/{weak,mid,strong}.cpp + engine/sparring/objective.hpp + workspace/dart.hpp
编译器查找顺序：环境变量 CXX → PATH 中的 g++ → 仓库 tools/w64devkit/bin/g++。
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

EP = Path(__file__).resolve().parent
WS = EP / "workspace"
ENG = EP / "engine"
SPARRING = ENG / "sparring"

ARENA_FLAGS = ["-std=c++17", "-O2", "-static"]
ARENA_LIBS = ["-lshell32", "-luserenv", "-ladvapi32"]
BOT_FLAGS = ["-std=c++17", "-O2", "-static", "-s", "-Wl,--stack,67108864"]


def find_cxx():
    cxx = os.environ.get("CXX")
    if cxx and (shutil.which(cxx) or Path(cxx).is_file()):
        return shutil.which(cxx) or cxx
    p = shutil.which("g++")
    if p:
        return p
    for base in EP.parents:
        for guess in (
            base / "tools" / "w64devkit" / "bin" / "g++.exe",
            Path("C:/w64devkit/bin/g++.exe"),
            Path("C:/msys64/ucrt64/bin/g++.exe"),
            Path("C:/msys64/mingw64/bin/g++.exe"),
        ):
            if guess.is_file():
                return str(guess)
    raise SystemExit("没有找到 g++ 编译器。请安装 w64devkit 或 MinGW-w64，把 bin 目录加入 PATH，或设置环境变量 CXX。")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run_compile(cxx, args, cwd, label):
    env = dict(os.environ)
    env["PATH"] = str(Path(cxx).resolve().parent) + os.pathsep + env.get("PATH", "")
    r = subprocess.run([cxx, *args], cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace", env=env)
    if r.returncode != 0:
        raise SystemExit(f"编译失败（{label}）：\n{(r.stderr or r.stdout)[-4000:]}")


def build_arena(cxx):
    with tempfile.TemporaryDirectory(prefix="build_arena_") as td:
        work = Path(td)
        shutil.copy2(ENG / "arena.cpp", work / "arena.cpp")
        shutil.copy2(ENG / "json.hpp", work / "json.hpp")
        shutil.copy2(WS / "dart.hpp", work / "dart.hpp")
        run_compile(cxx, [*ARENA_FLAGS, "arena.cpp", "-o", "arena.exe", *ARENA_LIBS], work, "arena.exe")
        shutil.copy2(work / "arena.exe", WS / "arena.exe")


def build_sparring(cxx, name):
    with tempfile.TemporaryDirectory(prefix=f"build_{name}_") as td:
        work = Path(td)
        shutil.copy2(SPARRING / f"{name}.cpp", work / f"{name}.cpp")
        shutil.copy2(SPARRING / "objective.hpp", work / "objective.hpp")
        shutil.copy2(WS / "dart.hpp", work / "dart.hpp")
        run_compile(cxx, [*BOT_FLAGS, f"{name}.cpp", "-o", f"{name}.exe"], work, f"bots/{name}.exe")
        (WS / "bots").mkdir(exist_ok=True)
        shutil.copy2(work / f"{name}.exe", WS / "bots" / f"{name}.exe")


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    cxx = find_cxx()
    print(f"编译器：{cxx}")
    manifest_path = EP / "source_manifest.json"
    man = json.loads(manifest_path.read_text(encoding="utf-8")).get("contest_binaries", {}) if manifest_path.is_file() else {}

    build_arena(cxx)
    done = ["arena.exe"]
    for b in ("weak", "mid", "strong"):
        build_sparring(cxx, b)
        done.append(f"bots/{b}.exe")

    for f in done:
        got = sha(WS / f)
        want = man.get(f"workspace/{f}")
        if want:
            note = "与比赛时逐字节相同" if got == want else f"编译成功（比赛时原件 {want[:16]}…）"
        else:
            note = "编译成功"
        print(f"{f:16} {got[:16]}…  {note}")
    print("完成。之后可在 workspace/ 下照 README.md 用 match.py 进行本地 5v5 对战。")


if __name__ == "__main__":
    main()
