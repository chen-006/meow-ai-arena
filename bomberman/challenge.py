"""用你的 bot 挑战第 2 集全部 AI：和 9 个模型的封存代码 + 公开基准，用正式赛的评测脚本打一轮小比赛。

  python challenge.py                      用 workspace/baseline.cpp 当"你的 bot"（演示用）
  python challenge.py 我的bot.cpp          用你自己的 C++ 程序（330 局，约 4 分钟）
  python challenge.py 我的bot.cpp --full   加上 3 人局，1320 局，约 15 分钟，结果更稳

需要 Python 3.10+ 和 g++（Windows 可用 w64devkit，见仓库根目录 README）。
"""
import subprocess
import sys
import time
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "tournament"))
from run import FORMAL  # noqa: E402


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    full = "--full" in sys.argv
    mine = Path(args[0]).resolve() if args else HERE / "workspace" / "baseline.cpp"
    if not mine.is_file():
        raise SystemExit(f"找不到你的程序：{mine}")
    out = HERE / "challenge-runs" / time.strftime("%Y%m%d-%H%M%S")
    sizes, seeds = (["2", "3", "10"], "1") if full else (["2", "10"], "1")
    print(f"你的程序：{mine}\n人数 {sizes}，每种人数 {seeds} 张图；结果保存在 {out}\n（第一次运行要编译 11 个程序，稍等一会儿）\n", flush=True)
    r = subprocess.call([sys.executable, "-X", "utf8", str(HERE / "tournament" / "run.py"), "--output", str(out),
                         "--bots", f"你的 bot={mine}", *FORMAL, "--sizes", *sizes, "--seeds", seeds])
    if r:
        raise SystemExit("出错了，请看上面的报错。")
    md = out / "汇总.md"
    if md.is_file():
        text = md.read_text(encoding="utf-8")
        i = text.find("## BT")
        print("\n" + (text[i:] if i >= 0 else text))
    print(f"\n完整结果：{out}（汇总.md、每局 HTML 回放在 replays/）")
    print("局数少时排名会有起伏；加 --full 再跑一次更稳。正式赛（8930 局）的成绩见 results/。")


if __name__ == "__main__":
    main()
