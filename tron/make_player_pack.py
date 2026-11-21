"""编译两个陪练，并（可选）打一份和比赛时一样的选手包。

  python make_player_pack.py          把 sparring/ 里的两个陪练编成 workspace/bots/<名字>.exe 和 .dll
                                      （arena.py 和 challenge.py 要用；第一次运行 challenge.py 时会自动做这一步）
  python make_player_pack.py --zip    另外按比赛时的文件清单打出 out/workspace.zip（发给新模型用）

比赛时选手包里的陪练只有编译好的程序（sparring.exe/.dll、sparring_hard.exe/.dll），没有源码。
开源版放的是源码 sparring/*.cpp，用比赛时的编译参数（同 workspace/arena.py 的 build_bot）编出来。
需要 g++（Windows 推荐 w64devkit，见仓库根目录 README）。选手包和裁判只支持 Windows。
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
HERE = Path(__file__).resolve().parent
WS = HERE / "workspace"
SRC = HERE / "sparring"
NAMES = ("sparring", "sparring_hard")
FLAGS = ["-std=c++20", "-O2", "-static"]
KINDS = {"exe": ["-Wl,--stack,67108864", "-I.", "bot.cpp", "tron_main.cpp"],
         "dll": ["-shared", "-DTRON_DLL", "-I.", "bot.cpp", "tron_main.cpp"]}


def sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def find_compiler():
    cxx = os.environ.get("CXX") or shutil.which("g++")
    if cxx:
        return cxx
    for guess in (HERE.parent / "tools" / "w64devkit" / "bin" / "g++.exe", Path("C:/w64devkit/bin/g++.exe")):
        if guess.is_file():
            return str(guess)
    raise SystemExit("没找到 C++ 编译器（g++）。Windows 推荐 w64devkit，放法见仓库根目录 README。")


def build_sparring(quiet=False):
    cxx = find_compiler()
    env = dict(os.environ)
    env["PATH"] = str(Path(cxx).resolve().parent) + os.pathsep + env.get("PATH", "")
    with tempfile.TemporaryDirectory() as tmp:      # 在临时目录里用相对路径编译，避开路径里的中文
        work = Path(tmp)
        shutil.copyfile(WS / "tron.hpp", work / "tron.hpp")
        shutil.copyfile(WS / "tron_main.cpp", work / "tron_main.cpp")
        for name in NAMES:
            shutil.copyfile(SRC / f"{name}.cpp", work / "bot.cpp")
            for kind, extra in KINDS.items():
                subprocess.run([cxx, *FLAGS, *extra, "-o", f"out.{kind}"], cwd=work, check=True, env=env)
                dst = WS / "bots" / f"{name}.{kind}"
                shutil.copyfile(work / f"out.{kind}", dst)
                if not quiet:
                    print(f"workspace/bots/{name}.{kind}  sha256 {sha(dst)[:16]}")


def make_zip():
    manifest = json.loads((HERE / "workspace_manifest.json").read_text(encoding="utf-8"))
    out = HERE / "out" / "workspace.zip"
    out.parent.mkdir(exist_ok=True)
    differ = []
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for rel, info in manifest["files"].items():
            p = WS / rel
            if not p.is_file():
                raise SystemExit(f"缺文件：workspace/{rel}")
            if sha(p) != info.get("open_source_sha256", info["sha256"]):   # 开源时改过的文件核对开源版哈希
                differ.append(rel)
            z.write(p, f"workspace/{rel}")
    print(f"\n已生成 {out.relative_to(HERE)}，{len(manifest['files'])} 个文件，和比赛时的文件清单相同。")
    src = [r for r in differ if not r.endswith((".exe", ".dll"))]
    if src:
        print("注意：这些文件和比赛时的选手包不一样（可能被改过）：" + "、".join(src))
    else:
        print("除陪练的可执行文件外，每个文件的 sha256 都和比赛时的选手包一致（arena.py 是开源版，只多一句找不到陪练时的提示）。")
    if differ:
        print("陪练是现编的，可执行文件的 sha256 不一定和比赛时相同（编译器版本、时间戳等），"
              "行为相同：sparring_hard.exe 用 GCC 16.2 编出来和比赛时逐字节相同。")


if __name__ == "__main__":
    build_sparring()
    if "--zip" in sys.argv:
        make_zip()
