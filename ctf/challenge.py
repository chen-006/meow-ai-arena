"""用你的 bot 挑战本期全部 AI：和 11 个模型的交卷代码 + 基准打一个小型联赛，给出实力值排名。

  python challenge.py                      用 workspace/submission/bot.cpp（模板，角色原地不动）
  python challenge.py 我的bot.cpp          用你自己的程序（.cpp 自动编译；也可以是 .py 或可执行文件）
  python challenge.py 我的bot.cpp --full   更多局数，结果更稳（约 2000 局，几分钟）

需要 Python 3.10+ 和 g++（Windows 可用 w64devkit，见仓库根目录 README）。
"""
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
HERE = Path(__file__).resolve().parent
ARENA = HERE.parent / "arena"
BOTS = {
    "基准": "workspace/baseline.cpp",
    "GPT-6 Astra": "bots/gpt-6-astra.cpp", "GPT-6.1 Sol": "bots/gpt-6.1-sol.cpp", "Fable 5.1": "bots/fable-5.1.cpp",
    "Sonnet 5.5": "bots/sonnet-5.5.cpp", "Opus 5.5": "bots/opus-5.5.cpp", "DeepSeek V4.1 Flash": "bots/deepseek-v4.1-flash.cpp",
    "Qwen 3.8 Max": "bots/qwen-3.8-max.cpp", "MiMo V2.6 Pro": "bots/mimo-v2.6-pro.cpp", "Kimi K3": "bots/kimi-k3.cpp",
    "GLM 5.3": "bots/glm-5.3.cpp", "豆包 Seed 2.1 Turbo": "bots/doubao-seed-2.1-turbo.cpp",
}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    full = "--full" in sys.argv
    mine = Path(args[0]).resolve() if args else HERE / "workspace" / "submission" / "bot.cpp"
    if not mine.is_file():
        raise SystemExit(f"找不到你的程序：{mine}")
    players = {"你的 bot": str(mine), **{n: str(HERE / p) for n, p in BOTS.items()}}
    sizes = [2, 3, 4, 5, 6, 8, 10, 13] if full else [2, 3, 5, 8]
    out = HERE / "challenge-runs" / time.strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True)
    cfg = {"game": str(HERE / "workspace" / "engine.py"), "anchor": "基准", "players": players, "sizes": sizes,
           "maps_per_size": 8 if full else 3, "copies": 1, "replays": False, "build_dir": str(HERE / "workspace" / "build")}
    (out / "config.json").write_text(json.dumps(cfg, ensure_ascii=False, indent=1), encoding="utf-8")
    workers = max(1, min(8, (os.cpu_count() or 2) // 2))
    py = sys.executable
    print(f"你的程序：{mine}\n对手：11 个 AI + 基准；人数 {sizes}；结果保存在 {out}\n（第一次运行要编译 13 个程序，稍等一会儿）\n", flush=True)
    for step in (["plan", str(out / "config.json"), str(out / "games")], ["run", str(out / "games"), "--workers", str(workers)], ["rate", str(out / "games")]):
        r = subprocess.run([py, "-X", "utf8", str(ARENA / "tournament.py"), *step])
        if r.returncode:
            raise SystemExit(f"出错了（{step[0]}），请看上面的报错。")
    print(f"\n完整成绩表：{out / 'games' / '成绩.md'}")
    print("局数少时排名会有起伏；想要更稳的结果，加 --full 再跑一次。正式赛（32010 局）的成绩见 results/ratings.md。")


if __name__ == "__main__":
    main()
