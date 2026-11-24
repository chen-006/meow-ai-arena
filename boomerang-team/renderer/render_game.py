"""第 12 集 5v5 回放一条命令出片：按回放里的选手名自动选体素小人、顶栏标签与地图主题，调用 src/make_video.py。

    python renderer/render_game.py replays/R1_g366_astra_1v5.txt -o R1_g366.mp4 [--procs 3 --samples 12]
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
KEY = {
    "astra": "astra",
    "sol": "sol",
    "fable": "fable",
    "opus": "opus",
    "carbon": "gemini",
    "hydrogen": "gemini_s",
    "grok": "grok",
    "glm": "glm",
    "deepseek": "deepseek",
    "kimi": "kimi",
    "qwen": "qwen",
    "mimo": "mimo",
    "doubao": "doubao",
}
SPARE = ["astra", "sol", "gemini_s", "grok", "kimi", "fable", "opus", "gemini", "glm", "deepseek"]


def players(path):
    names = {}
    with open(path, encoding="utf-8") as f:
        for ln in f:
            if ln.startswith("PLAYER "):
                p = ln.split()
                names[int(p[1])] = p[3] if len(p) > 3 else ""
            elif ln.startswith("S "):
                break
    return [names[k] for k in sorted(names)]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    rp = sys.argv[1]
    rest = sys.argv[2:]
    roster = []
    for i, n in enumerate(players(rp)):
        k = KEY.get(n.lower())
        roster.append(k if k and k not in roster else SPARE[i % len(SPARE)])
    env = dict(os.environ)
    env.setdefault("DART_LABELS", "gemini=Carbon,gemini_s=Hydrogen")
    cmd = [os.environ.get("DART_PYTHON") or sys.executable, os.path.join(HERE, "src", "make_video.py"), rp]
    if "--roster" not in rest:
        cmd += ["--roster", ",".join(roster)]
    print("角色：" + " / ".join(roster))
    sys.exit(subprocess.call(cmd + rest, env=env))


if __name__ == "__main__":
    main()
