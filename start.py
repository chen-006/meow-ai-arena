"""meow-ai-arena 入口菜单：看回放、用你的 bot 挑战 AI、投稿、检查环境。

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
    "5": ("第 5 集 · 仓储调度", "warehouse", None,
          [("最终成绩（三张榜和全部明细）", "results/最终成绩.md"), ("第 2 轮找到的 7 个缺陷点", "results/round2/缺陷点归类.md"),
           ("比赛条件与异常记录", "CONDITIONS.md")]),
    "6": ("第 6 集 · 坦克大战", "tank", "workspace/submission/bot.cpp",
          [("冠亚军 Astra vs SolUF（终场赛）", "replays/final-astra-vs-soluf.html"),
           ("冠军对出题方参照（终场赛）", "replays/final-sparring-vs-astra.html"),
           ("Sol vs Opus（终场赛）", "replays/final-sol-vs-opus.html"),
           ("豆包下半场版本对自己的上半场版本（进步赛）", "replays/progress-doubao2-vs-doubao1.html")]),
    "7": ("第 7 集 · 光轮对战", "tron", "workspace/submission/bot.cpp",
          [("12 人局（正式榜，随机抽的一局）", "replays/25-12p-official-m05-rot01.html"),
           ("4 人局（正式榜，随机抽的一局）", "replays/19-4p-official-Pangu-Dots-Doubao-Mistral.html"),
           ("1v1（随机抽的一局）", "replays/06-1v1-Mistral-vs-LongCat.html"),
           ("全部 30 局的清单", "replays/README.md")]),
    "7b": ("第 7 集 · 屎山优化（仓储调度）", "warehouse", None,
           [("名次和说明（README 文末的第 7 集一节）", "README.md"), ("统一评测主表", "results/ep7/汇总.md"),
            ("比赛条件与异常记录（文末一节）", "CONDITIONS.md")]),
    "9": ("第 9 集 · 峡谷对决（MOBA）", "moba", "workspace/submission/bot.cpp",
          [("看全部示例赛回放（下拉切换）", "replays/index.html"),
           ("示例赛成绩", "results/示例赛-成绩.md")]),
}
# 第 4 集是模型直接当玩家，第 5 集和第 7 集屎山优化是优化代码，都没有 bot 可挑战；它们也没有回放（第 4 集除外），列的是成绩文档
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
    if (ROOT / g[1] / "replays").is_dir():
        print(f"正在用浏览器打开 {path}（{g[1]}/replays/ 里还有更多）")
    else:
        print(f"这一集没有回放，正在打开 {path}（Markdown 文档；打不开的话用任意文本编辑器看，更多材料见 {g[1]}/README.md）")
    webbrowser.open(path.as_uri())


def challenge():
    if not find_compiler():
        print("挑战需要 C++ 编译器，先按下面的提示装好：")
        check(); return
    for k, (title, _, _, _) in CHALLENGES.items():
        print(f"  {k}. {title}")
    title, folder, template, _ = CHALLENGES[ask("挑战哪一集的 AI？", list(CHALLENGES))]
    if folder == "tron" and os.name != "nt":
        print("第 7 集光轮对战的裁判用到 Windows 的 CPU 计时接口，只能在 Windows 上挑战。回放用浏览器就能看。")
        return
    print(f"把你的 bot（.cpp 文件）拖进这个窗口再回车；直接回车则用模板 {folder}/{template}。规则见 {folder}/workspace/。")
    raw = input("你的 bot：").strip().strip('"').strip("'")
    args = [sys.executable, "-X", "utf8", str(ROOT / folder / "challenge.py")] + ([raw] if raw else [])
    if ask("快速模式（几分钟）还是完整模式（更稳、更久）？输入 q 或 f：", ["q", "f"]) == "f":
        args.append("--full")
    subprocess.call(args, cwd=ROOT / folder)
    print("打完想留个名次，可以把这个 bot 投到社区榜：在菜单里选“投稿”，或者看 community/README.md。")


def submit():
    print("你的 bot（第 1、2、3、6 集）和优化过的仓储调度代码（第 5 集）可以投到这个仓库，合并后进社区榜 community/LEADERBOARD.md。")
    print("  交给 AI 办：把 community/AGENT_PROMPT.md 里的提示词整段复制给你的 coding agent。")
    print("  自己动手：")
    print("    1. fork 本仓库，新建 community/<集>/<你的GitHub用户名>__<作品名>/，放进 bot.cpp（第 5 集是 src/）和 meta.json")
    print("       第 6 集目录名用 tank；bot.cpp 只写 init/decide，使用官方 tank.hpp，不要自带 main 或头文件")
    print("    2. 运行 python community/check.py community/<集>/<你的文件夹>")
    print("    3. 只提交这一个文件夹，开 Pull Request，把自测摘要贴进描述")
    print("Tank 投稿支持需先合并到目标分支；PR 评测使用目标分支的可信脚本。")
    print("规则和 meta.json 的写法见 community/README.md。")
    if ask("现在打开投稿指南吗？输入 y 或 n：", ["y", "n"]) == "y":
        path = ROOT / "community" / "README.md"
        print(f"正在打开 {path}（Markdown 文档；打不开的话用任意文本编辑器看）")
        webbrowser.open(path.as_uri())


def main():
    print("=" * 56 + "\n  meow-ai-arena · AI 算法对抗大赛\n" + "=" * 56)
    while True:
        print("\n  1. 看 AI 对战回放\n  2. 用你的 bot 挑战 AI\n  3. 检查环境\n  4. 投稿（把你的作品交到社区榜）\n  0. 退出")
        c = ask("选择：", ["1", "2", "3", "4", "0"])
        if c == "0":
            return
        try:
            {"1": watch, "2": challenge, "3": check, "4": submit}[c]()
        except (KeyboardInterrupt, EOFError):
            print("\n已取消。")


if __name__ == "__main__":
    try:
        main()
    except (KeyboardInterrupt, EOFError):
        pass
