"""用你的 bot 挑战"峡谷对决"：单人难度阶梯 + 5v5 挑战全部示例 bot。

  python challenge.py                      用 workspace/submission/bot.cpp（需要你先自己创建）
  python challenge.py 我的bot.cpp          单人挑战：从简单到困难逐档打引擎 NPC 队
  python challenge.py 我的bot.cpp --pvp    再和 5 个示例 bot 各打两场（换边）

需要 Python 3.10+ 和 g++（Windows 可用 w64devkit，见仓库根目录 README）。
"""
import json
import sys
import time
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
HERE = Path(__file__).resolve().parent
WS = HERE / "workspace"
sys.path.insert(0, str(WS))
import core  # noqa: E402
from engine import VERSION, Game, DIFF_LABELS  # noqa: E402

EXAMPLES = {
    "速推": "bots/rush.cpp", "发育": "bots/farm.cpp", "游走": "bots/gank.cpp",
    "抱团": "bots/siege.cpp", "均衡": "bots/balance.cpp", "基准": "workspace/baseline.cpp",
}


def play(mine_cmd, seed, difficulty, record=False):
    """单人挑战固定坐 0 号队。"""
    return core.run_match(Game, [mine_cmd], ["你的bot"], seed, record=record, difficulty=difficulty)


def pve_ladder(mine_cmd, seeds=(1, 2)):
    print("== 单人挑战：对抗引擎 NPC 队（你的 5 个英雄 vs 对面的 NPC 英雄 + 兵线 + 防御塔）==\n")
    cleared = -1
    rows = []
    for diff in (0, 1, 2):
        win = 0
        detail = []
        for seed in seeds:
            result, replay = play(mine_cmd, seed, diff, record=True)
            w = result["winner"]
            win += w == 0
            towers = result["stats"][0].get("towers", 0)
            kills = result["stats"][0].get("kills", 0)
            mark = "✅ 胜" if w == 0 else ("➖ 平" if w < 0 else "❌ 负")
            detail.append(f"种子 {seed}：{mark}（{result['turns']} 回合，推塔 {towers}，击杀 {kills}，{result['reason']}）")
            out = HERE / "challenge-runs" / time.strftime("%Y%m%d-%H%M%S") / f"pve-{DIFF_LABELS[diff]}-{seed}"
            core.save_replay(replay, out, WS / "viewer.html")
        rows.append((diff, win, detail))
        if win == len(seeds) and cleared == diff - 1:
            cleared = diff
    print()
    for diff, win, detail in rows:
        ok = "通过" if win == len(seeds) else "未通过"
        print(f"【{DIFF_LABELS[diff]}】{win}/{len(seeds)} 胜 —— {ok}")
        for d in detail:
            print(f"   {d}")
    print()
    if cleared >= 0:
        print(f"🏆 你打穿了 {DIFF_LABELS[cleared]} 档"
              + ("，下一档在等你。" if cleared < 2 else "，全部三档通关！"))
    else:
        print("简单档还没打过去。检查回放（challenge-runs/ 里有网页），修一修再来。")
    return cleared


def pvp_examples(mine_cmd, seeds=(1, 2)):
    print("== 5v5 挑战：和示例 bot 两两换边 ==\n")
    table = []
    for name, rel in EXAMPLES.items():
        path = (HERE / rel).resolve()
        if not path.is_file():
            print(f"{name}：文件缺失，跳过")
            continue
        cmd = core.command_for(path, WS / "build")
        w = l = d = 0
        for seed in seeds:
            for my_seat in (0, 1):
                progs = [mine_cmd, cmd] if my_seat == 0 else [cmd, mine_cmd]
                names = ["你的bot", name] if my_seat == 0 else [name, "你的bot"]
                result, replay = core.run_match(Game, progs, names, seed, record=True, difficulty=1)
                idx = 0 if my_seat == 0 else 1
                mark = result["winner"]
                if mark < 0:
                    d += 1
                elif mark == idx:
                    w += 1
                else:
                    l += 1
                out = HERE / "challenge-runs" / time.strftime("%Y%m%d-%H%M%S") / f"pvp-{name}-{seed}-{my_seat}"
                core.save_replay(replay, out, WS / "viewer.html")
        table.append((name, w, d, l))
        print(f"  vs {name}: {w} 胜 {d} 平 {l} 负")
    print()
    best = max(table, key=lambda r: (r[1], -r[3])) if table else None
    if best:
        print(f"最难缠的对手是 {best[0]}。全部回放在 challenge-runs/ 里，可以逐回合看。")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    mine = Path(args[0]).resolve() if args else WS / "submission" / "bot.cpp"
    if not mine.is_file():
        raise SystemExit(f"找不到你的程序：{mine}")
    cmd = core.command_for(mine, WS / "build")
    print(f"你的程序：{mine}\n")
    ladder = not ("--pvp-only" in sys.argv)
    if not ("--no-pve" in sys.argv):
        pve_ladder(cmd)
    if "--pvp" in sys.argv or "--pvp-only" in sys.argv:
        pvp_examples(cmd)
    print("\n打完想留个名次，可以把 bot 投到社区榜（见仓库根 README）。")


if __name__ == "__main__":
    main()
