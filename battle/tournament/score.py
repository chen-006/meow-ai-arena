#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""复现《纸上谈兵》正式成绩。计分全部由比赛时的 scoring.py（原样）完成，这个脚本只负责摆文件和核对。

  python tournament/score.py
      读 results/eval/ 里的 945 场逐局结果，用 scoring.py 重算总榜、闯关榜、对抗榜，
      和 results/ 里的成绩表逐项核对。几秒钟，不需要编译器和 war.exe。

  python tournament/score.py --rerun [--workers 8] [--replays] [--keep]
      从头重打：先 python build_engine.py 编出 workspace/war.exe；
      再用 workspace/build.py 同一条命令编译九位选手的指挥程序，检查 63 份布阵，
      用 war.exe batch 跑完 45 场闯关、144 场 1v1、756 场混战，逐场和 results/eval/results/ 核对，
      最后出榜并核对。8 线程两三分钟。--replays 同时存下全部回放（约 3 GB）。

比赛时的目录结构是 <赛事目录>/kit（选手包）、sealed/<席位>/submission（收卷）、eval（评测）、成绩（成绩表）；
这里在 rerun/ 下临时搭一份同样的结构再交给 scoring.py，results/ 里的路径也都是相对这个结构写的。
"""
import argparse
import datetime
import importlib.util
import json
import os
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
EP = HERE.parent
RESULTS = EP / "results"
VOLATILE = {"first_ms", "avg_ms", "max_ms", "cpu_total_ms",   # 指挥程序的 CPU 计时
            "t_phys", "t_logic"}                                # 引擎自己的物理、逻辑耗时（秒）；每次跑都不一样
BOARDS = ["总榜", "闯关榜", "对抗榜"]


def load_scoring():
    spec = importlib.util.spec_from_file_location("a12_scoring", HERE / "scoring.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def read(p):
    return json.loads(Path(p).read_text(encoding="utf-8-sig"))


def say(c, stage, who="", msg="", **kw):
    print(f"[{stage}] {who + '：' if who else ''}{msg}", flush=True)


def rel(o, root, cxx=None):
    """把临时赛事目录里的绝对路径换回相对路径（和 results/ 里的写法一致）。"""
    prefixes = [str(root) + os.sep, str(root).replace("\\", "/") + "/"]
    if isinstance(o, dict):
        return {k: rel(v, root, cxx) for k, v in o.items()}
    if isinstance(o, list):
        return [rel(v, root, cxx) for v in o]
    if isinstance(o, str):
        if cxx and o == cxx:
            return "g++"
        for p in prefixes:
            if o.startswith(p):
                return o[len(p):].replace("\\", "/")
    return o


def drop(o, keys):
    if isinstance(o, dict):
        return {k: drop(v, keys) for k, v in o.items() if k not in keys}
    if isinstance(o, list):
        return [drop(v, keys) for v in o]
    return o


def diff(a, b, path=""):
    """返回第一处不同的位置说明；相同返回 None。"""
    if type(a) is not type(b) and not (isinstance(a, (int, float)) and isinstance(b, (int, float))):
        return f"{path}: {a!r} ≠ {b!r}"
    if isinstance(a, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a or k not in b:
                return f"{path}/{k}: 只在一边有"
            d = diff(a[k], b[k], f"{path}/{k}")
            if d:
                return d
        return None
    if isinstance(a, list):
        if len(a) != len(b):
            return f"{path}: 长度 {len(a)} ≠ {len(b)}"
        for i, (x, y) in enumerate(zip(a, b)):
            d = diff(x, y, f"{path}[{i}]")
            if d:
                return d
        return None
    return None if a == b else f"{path}: {a!r} ≠ {b!r}"


def check_boards(root):
    """root/成绩 下新算出的三张榜 vs results/ 里的正式成绩表（不比生成时间和回放路径）。"""
    bad = []
    for n in BOARDS:
        mine = drop(rel(read(root / "成绩" / f"{n}.json"), root), {"generated", "replay"})
        ref = drop(read(RESULTS / f"{n}.json"), {"generated", "replay"})
        d = diff(mine, ref)
        if d:
            bad.append(f"{n}.json {d}")
        if (root / "成绩" / f"{n}.md").read_text(encoding="utf-8") != (RESULTS / f"{n}.md").read_text(encoding="utf-8"):
            bad.append(f"{n}.md 文字不同")
    return bad


def new_root(tag):
    root = EP / "rerun" / f"{tag}_{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}"
    root.mkdir(parents=True)
    return root


def quick(c, scoring, keep):
    root = new_root("score")
    shutil.copytree(RESULTS / "eval", root / "eval")
    c["root"] = str(root)
    scoring.boards(c)
    bad = check_boards(root)
    finish(root, bad, keep, "三张成绩表")


def find_cxx():
    if os.environ.get("CXX"):
        return os.environ["CXX"]
    for cand in (EP.parent / "tools" / "w64devkit" / "bin" / "g++.exe", EP.parent / "tools" / "w64devkit" / "bin" / "g++"):
        if cand.is_file():
            return str(cand)
    return shutil.which("g++")


def rerun(c, scoring, args):
    war = EP / "workspace" / "war.exe"
    if not war.is_file():
        sys.exit("workspace/war.exe 不存在：先运行 python build_engine.py")
    cxx = find_cxx()
    if not cxx:
        sys.exit("找不到 g++：设置环境变量 CXX，或把 w64devkit 放进仓库的 tools/w64devkit/")
    os.environ["CXX"] = cxx                      # 选手包 build.py 的 find_compiler 先看 CXX
    root = new_root("rerun")
    shutil.copytree(EP / "workspace", root / "kit", ignore=shutil.ignore_patterns("build", "replays", "__pycache__"))
    for p in c["players"]:
        shutil.copytree(EP / "bots" / p["dir"], root / "sealed" / p["name"] / "submission")
    c["root"] = str(root)
    c["judge"] = dict(c.get("judge", {}), workers=args.workers, replays=bool(args.replays))
    scoring.judge(c, say)
    ev, ref = root / "eval", RESULTS / "eval"
    bad = []
    # 布阵检查和编译结果（不比编译命令和 exe 路径）
    chk = rel(read(ev / "检查.json"), root, cxx)
    for n, v in chk.items():
        v["commander"].pop("cmd", None)
    ref_chk = read(ref / "检查.json")
    for n, v in ref_chk.items():
        v["commander"].pop("cmd", None)
    d = diff(chk, ref_chk)
    if d:
        bad.append("检查.json " + d)
    # 赛程（不比回放路径：没开 --replays 时是空的）
    plan = drop(rel(read(ev / "对局索引.json"), root), {"replay"})
    d = diff(plan, drop(read(ref / "对局索引.json"), {"replay"}))
    if d:
        bad.append("对局索引.json " + d)
    # 逐场结果（不比耗时类字段和批次内序号 id）
    same = 0
    for key in sorted(read(ref / "对局索引.json")):
        mine = drop(rel(read(ev / "results" / f"{key}.json"), root), VOLATILE | {"id"})
        d = diff(mine, drop(read(ref / "results" / f"{key}.json"), VOLATILE | {"id"}))
        if d:
            bad.append(f"{key} {d}")
        else:
            same += 1
    print(f"逐场结果：{same} 场和比赛时相同，{len(bad)} 处不同", flush=True)
    scoring.boards(c)
    bad += check_boards(root)
    finish(root, bad, args.keep, "945 场逐场结果和三张成绩表")


def finish(root, bad, keep, what):
    if bad:
        print("\n核对不通过：")
        for b in bad[:40]:
            print("  " + b)
        if len(bad) > 40:
            print(f"  ……共 {len(bad)} 处")
        print(f"\n本次输出留在 {root}")
        sys.exit(1)
    print(f"\n核对通过：{what}与比赛时的正式成绩逐项相同。")
    for n in BOARDS:
        print(f"  {n}：{root / '成绩' / (n + '.md')}" if keep else f"  {n}：results/{n}.md")
    if not keep:
        shutil.rmtree(root, onerror=_force_remove)


def _force_remove(func, path, _exc):
    """Windows 上只读文件删不掉：去掉只读再删一次。"""
    os.chmod(path, 0o666)
    func(path)


def main():
    ap = argparse.ArgumentParser(description="复现《纸上谈兵》正式成绩")
    ap.add_argument("--rerun", action="store_true", help="从选手作品重新编译、重打全部 945 场再核对")
    ap.add_argument("--workers", type=int, default=8, help="--rerun 时 war.exe batch 的线程数（比赛时 8）")
    ap.add_argument("--replays", action="store_true", help="--rerun 时存下全部回放（约 3 GB）")
    ap.add_argument("--keep", action="store_true", help="核对通过也保留 rerun/ 下的输出")
    args = ap.parse_args()
    c = read(HERE / "config.json")
    scoring = load_scoring()
    if args.rerun:
        rerun(c, scoring, args)
    else:
        quick(c, scoring, args.keep)


if __name__ == "__main__":
    main()
