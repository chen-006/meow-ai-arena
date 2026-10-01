"""用你的 bot 挑战第 1 集全部 AI：和 9 个模型的交卷代码 + 3 个基准（random、greedy、出题方陪练）打一轮小型循环赛。
用的就是正式赛的循环赛和评分脚本，只是局数少很多。

  python challenge.py                      用 workspace/starter/bot.cpp（模板）
  python challenge.py 我的bot.cpp          用你自己的 C++ 程序（约 380 局，四分钟左右）
  python challenge.py 我的bot.cpp --full   更多局数，结果更稳（约 1100 局，十几分钟）

需要 Python 3.10+ 和 g++（Windows 可用 w64devkit，见仓库根目录 README）。
"""
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
HERE = Path(__file__).resolve().parent


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    full = "--full" in sys.argv
    mine = Path(args[0]).resolve() if args else HERE / "workspace" / "starter" / "bot.cpp"
    if not mine.is_file():
        raise SystemExit(f"找不到你的程序：{mine}")
    out = HERE / "challenge-runs" / time.strftime("%Y%m%d-%H%M%S")
    entries = out / "entries"
    for d in (HERE / "bots").iterdir():
        if (d / "bot.cpp").is_file():
            (entries / d.name).mkdir(parents=True)
            shutil.copy(d / "bot.cpp", entries / d.name / "bot.cpp")
    (entries / "your-bot@you#1").mkdir(parents=True)
    shutil.copy(mine, entries / "your-bot@you#1" / "bot.cpp")
    jobs = str(max(1, min(8, (os.cpu_count() or 2) // 2)))
    maps, rounds = ("6", "12") if full else ("2", "4")
    print(f"你的程序：{mine}\n单挑每对 {maps} 张图（各换边打两局），混战 {rounds} 轮；结果保存在 {out}\n（第一次运行要编译 13 个程序，稍等一会儿）\n", flush=True)
    py = [sys.executable, "-X", "utf8"]
    r = subprocess.run(py + [str(HERE / "tournament" / "round_robin.py"), "--entries", str(entries), "--out", str(out / "results.jsonl"),
                             "--maps", maps, "--melee-rounds", rounds, "--jobs", jobs])
    if r.returncode:
        raise SystemExit("比赛出错了，请看上面的报错。")
    r = subprocess.run(py + [str(HERE / "tournament" / "rate.py"), str(out / "results.jsonl"), "--entries", str(entries), "--out", str(out / "ratings")])
    if r.returncode:
        raise SystemExit("评分出错了，请看上面的报错。")
    md = (out / "ratings.md").read_text(encoding="utf-8")
    print("\n" + md.split("\n## ")[0])
    print(f"完整成绩表：{out / 'ratings.md'}")
    print("局数少时排名会有起伏；加 --full 再跑一次更稳。正式赛（单挑 26400 局 + 混战 3600 局）的成绩见 results/ratings.md。")


if __name__ == "__main__":
    main()
