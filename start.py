"""meow-ai-arena 入口菜单：看回放、用你的 bot 挑战 AI、检查环境。

Windows 双击 start.cmd，macOS / Linux 运行 sh start.sh，或直接 python start.py。
"""
import os
import shutil
import subprocess
import sys
import webbrowser
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
ROOT = Path(__file__).resolve().parent
GAMES = {
    "1": ("第 1 集 · 圈地", "land", "workspace/starter/bot.cpp",
          [("看全部精选回放（单挑 + 混战，下拉切换）", "replays/index.html")]),
    "2": ("第 2 集 · 炸弹人", "bomberman", "workspace/baseline.cpp",
          [("十人混战", "replays/10p-melee.html"), ("冠亚军单挑 Astra vs Opus", "replays/2p-astra-vs-opus.html"), ("八人混战", "replays/8p-melee.html")]),
    "3": ("第 3 集 · 夺旗", "ctf", "workspace/submission/bot.cpp",
          [("十五人混战", "replays/15p-melee.html"), ("冠亚军单挑 Astra vs Sol", "replays/2p-astra-vs-sol.html"),
           ("八人混战", "replays/8p-melee.html"), ("豆包 vs 基准", "replays/2p-doubao-vs-baseline.html")]),
    "4": ("第 4 集 · 外交", "diplomacy", None,
          [("第 3 局（最后一局，意大利单独获胜）", "replays/game3.html"), ("第 2 局", "replays/game2.html"),
           ("第 1 局", "replays/game1.html")]),
}
# 第 4 集是模型直接当玩家，没有 bot 可挑战
CHALLENGES = {k: v for k, v in GAMES.items() if v[2]}


def find_compiler():
    cxx = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not cxx and os.name == "nt":
        for guess in (ROOT / "tools" / "w64devkit" / "bin" / "g++.exe", Path("C:/w64devkit/bin/g++.exe")):
            if guess.is_file():
                return str(guess)
    return cxx


def check():
    ok = True
    print(f"Python：{sys.version.split()[0]}  {'✓' if sys.version_info >= (3, 10) else '✗ 需要 3.10 以上'}")
    ok &= sys.version_info >= (3, 10)
    cxx = find_compiler()
    print(f"C++ 编译器：{cxx or '✗ 没找到'}")
    if not cxx:
        ok = False
        if os.name == "nt":
            print("  Windows 推荐 w64devkit（免安装）：到 https://github.com/skeeto/w64devkit/releases 下载最新的 x64 版，"
                  "解压后把 w64devkit 文件夹放到本仓库的 tools/ 下（即 tools\\w64devkit\\bin\\g++.exe），或放到 C:\\w64devkit。")
        else:
            print("  macOS：xcode-select --install；Ubuntu/Debian：sudo apt install g++")
    print("看回放只需要浏览器；挑战 AI 需要 Python 和 C++ 编译器。" if ok else "")
    return ok


def ask(prompt, choices):
    while True:
        c = input(prompt).strip().lower()
        if c in choices:
            return c
        print("请输入：" + " / ".join(choices))


def watch():
    for k, (title, _, _, _) in GAMES.items():
        print(f"  {k}. {title}")
    g = GAMES[ask("看哪一集？", list(GAMES))]
    for i, (name, _) in enumerate(g[3], 1):
        print(f"  {i}. {name}")
    i = int(ask("选一局：", [str(i) for i in range(1, len(g[3]) + 1)])) - 1
    path = ROOT / g[1] / g[3][i][1]
    print(f"正在用浏览器打开 {path}（{g[1]}/replays/ 里还有更多）")
    webbrowser.open(path.as_uri())


def challenge():
    if not find_compiler():
        print("挑战需要 C++ 编译器，先按下面的提示装好：")
        check(); return
    for k, (title, _, _, _) in CHALLENGES.items():
        print(f"  {k}. {title}")
    title, folder, template, _ = CHALLENGES[ask("挑战哪一集的 AI？", list(CHALLENGES))]
    print(f"把你的 bot（.cpp 文件）拖进这个窗口再回车；直接回车则用模板 {folder}/{template}。规则见 {folder}/workspace/。")
    raw = input("你的 bot：").strip().strip('"').strip("'")
    args = [sys.executable, "-X", "utf8", str(ROOT / folder / "challenge.py")] + ([raw] if raw else [])
    if ask("快速模式（几分钟）还是完整模式（更稳、更久）？输入 q 或 f：", ["q", "f"]) == "f":
        args.append("--full")
    subprocess.call(args, cwd=ROOT / folder)


def main():
    print("=" * 56 + "\n  meow-ai-arena · AI 算法对抗大赛\n" + "=" * 56)
    while True:
        print("\n  1. 看 AI 对战回放\n  2. 用你的 bot 挑战 AI\n  3. 检查环境\n  0. 退出")
        c = ask("选择：", ["1", "2", "3", "0"])
        if c == "0":
            return
        try:
            {"1": watch, "2": challenge, "3": check}[c]()
        except (KeyboardInterrupt, EOFError):
            print("\n已取消。")


if __name__ == "__main__":
    try:
        main()
    except (KeyboardInterrupt, EOFError):
        pass
