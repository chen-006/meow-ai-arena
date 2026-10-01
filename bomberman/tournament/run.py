"""先把 C++ 选手编译好，再调用正式赛用的 evaluate.py。挑战脚本和复现正式赛都走这里。

  python tournament/run.py --output 输出目录 --sizes 2 5 10 --seeds 3 --bots 名字=程序.cpp ...
  python tournament/run.py --formal --output 输出目录        复现正式赛（8930 局，冻结赛程）
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
FORMAL = ["GPT-6 Astra=bots/gpt-6-astra.cpp", "GPT-6 Sol=bots/gpt-6-sol.cpp", "Opus 5.5=bots/opus-5.5.cpp", "Kimi K3=bots/kimi-k3.cpp",
          "GLM 5.3=bots/glm-5.3.cpp", "DeepSeek V4.1 Flash=bots/deepseek-v4.1-flash.cpp", "豆包 Seed 2.1=bots/doubao-seed-2.1.cpp",
          "Qwen 3.8 Max=bots/qwen-3.8-max.cpp", "MiMo V2.6 Pro=bots/mimo-v2.6-pro.cpp", "Base=workspace/baseline.cpp"]


def find_compiler():
    cxx = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not cxx and os.name == "nt":  # 没加进 PATH 的 w64devkit：仓库根目录下的 tools/，或 C 盘根目录
        for root in [*(a / "tools" for a in HERE.parents), Path("C:/")]:
            if (root / "w64devkit/bin/g++.exe").is_file():
                return str(root / "w64devkit/bin/g++.exe")
    return cxx


def compile_all(entries, build):
    build.mkdir(parents=True, exist_ok=True)
    out = []
    for e in entries:
        name, src = e.split("=", 1)
        src = (ROOT / src).resolve() if not Path(src).is_absolute() else Path(src)
        if src.suffix.lower() != ".cpp":
            out.append(f"{name}={src}"); continue
        digest = hashlib.sha256(src.read_bytes()).hexdigest()[:12]
        exe = build / f"{src.stem}-{digest}{'.exe' if os.name == 'nt' else ''}"
        if not exe.is_file():
            print(f"编译 {name}：{src.name}", flush=True)
            cxx = find_compiler()
            if not cxx:
                raise SystemExit("没有找到 C++ 编译器（g++ 或 clang++），安装方法见仓库根目录 README。")
            # 与正式赛相同的编译参数（见 workspace/engine/build.py）
            r = subprocess.run([cxx, *(["-static"] if os.name == "nt" else []), "-std=c++20", "-O2", str(src), "-o", str(exe)],
                               capture_output=True, text=True, encoding="utf-8", errors="replace")
            if r.returncode or not exe.is_file():
                raise SystemExit(f"{name} 编译失败：\n{r.stdout}\n{r.stderr}")
        out.append(f"{name}={exe}")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--bots", nargs="+")
    ap.add_argument("--sizes", type=int, nargs="+")
    ap.add_argument("--seeds", type=int, default=3)
    ap.add_argument("--formal", action="store_true", help="用冻结的正式赛赛程（2–10 人，8930 局）")
    ap.add_argument("--workers", type=int, default=max(1, min(8, (os.cpu_count() or 2) // 2)))
    a = ap.parse_args()
    bots = compile_all(FORMAL if a.formal else a.bots, ROOT / "workspace" / "build")
    args = [sys.executable, "-X", "utf8", "-u", str(HERE / "evaluate.py"), "--workspace", str(ROOT / "workspace"), "--bots", *bots,
            "--anchor", "Base", "--output", str(a.output), "--workers", str(a.workers), "--report-every", "100"]
    if a.formal:
        import gzip
        sched = a.output.parent / "schedule-2to10.json"
        sched.parent.mkdir(parents=True, exist_ok=True)
        sched.write_bytes(gzip.decompress((HERE / "schedule-2to10.json.gz").read_bytes()))
        args += ["--sizes", *map(str, range(2, 11)), "--schedule", str(sched)]
    else:
        args += ["--sizes", *map(str, a.sizes), "--seeds", str(a.seeds)]
    raise SystemExit(subprocess.call(args))


if __name__ == "__main__":
    main()
