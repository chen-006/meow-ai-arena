#!/usr/bin/env python3
"""Desert Arena 正式赛 · 组织方菜单。

等价于在本目录逐条执行 go run ./cmd/arena ...；
给不熟悉命令行的组织者一个入口，不改变任何执行逻辑。
"""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
ARENA = ["go", "run", "./cmd/arena"]
# 只有这一处指定赛季。输出目录从配置的 output 字段读，不在这里另写一份，
# 否则换了赛季配置还得记得同步改第二处（v1.2 时代就是这样漂掉的）。
CONFIG = "tournament/formal_v19.json"
DEFAULT_OUT = "results/formal-v19"


def out_dir() -> str:
    """结果目录以赛季配置的 output 为准。"""
    try:
        cfg = json.loads((ROOT / CONFIG).read_text(encoding="utf-8"))
        return str(cfg.get("output") or DEFAULT_OUT)
    except (OSError, ValueError):
        return DEFAULT_OUT

MENU = [
    ("1", "build", "编译选手并封存源码哈希"),
    ("2", "plan", "冻结赛程"),
    ("3", "run", "跑完所有对局（可用 --resume 续跑）"),
    ("4", "rate", "出 BT 实力值与加权总分"),
    ("5", "check", "单场自检（模板 vs 首位参考 Bot）"),
    ("6", "selftest", "验证超时 kill / 崩溃重启 / 非法结构红牌"),
    ("7", "season", "整季流水线：build → plan → run → rate"),
]


def run(args: list[str]) -> int:
    full = ARENA + [args[0], "-config", CONFIG] + args[1:]
    print("$", " ".join(full), flush=True)
    return subprocess.call(full, cwd=ROOT)


def season() -> int:
    for cmd in ("build", "plan", "run", "rate"):
        code = run([cmd])
        if code != 0:
            print(f"流水线在 {cmd} 阶段中止（退出码 {code}）")
            return code
    print(f"整季完成：结果在 {out_dir()}/", flush=True)
    return 0


def status() -> None:
    cfg_path = ROOT / CONFIG
    if not cfg_path.exists():
        print(f"赛季配置缺失：{CONFIG}")
        return
    cfg = json.loads(cfg_path.read_text(encoding="utf-8"))
    print(f"赛季：{cfg.get('season', '?')} · 规则 v{cfg.get('version', '?')}")
    print(f"配置：{CONFIG}")
    print(f"输出：{out_dir()}/")
    result = ROOT / out_dir() / "结果.jsonl"
    if result.exists():
        lines = [x for x in result.read_text(encoding="utf-8").splitlines() if x.strip()]
        print(f"对局：{len(lines)} 局已记录")
    else:
        print("对局：尚无结果（还没跑过 run）")


def main() -> int:
    args = sys.argv[1:]
    if args and args[0] in {"-h", "--help"}:
        print("用法：python start.py [build|plan|run|rate|check|selftest|season|status]")
        return 0
    if args:
        cmd = args[0]
        if cmd == "status":
            status()
            return 0
        if cmd == "season":
            return season()
        if cmd in {name for _, name, _ in MENU}:
            return run([cmd] + args[1:])
        print("未知命令：", cmd)
        return 2

    while True:
        print("\nDesert Arena 正式赛")
        for key, name, desc in MENU:
            print(f"  {key}. {name:<9} {desc}")
        print("  s. status")
        print("  0. 退出")
        choice = input("选择：").strip().lower()
        if choice in {"0", "q", ""}:
            return 0
        if choice == "s":
            status()
            continue
        for key, name, _ in MENU:
            if choice == key:
                season() if name == "season" else run([name])
                break
        else:
            print("无效选择")


if __name__ == "__main__":
    raise SystemExit(main())
