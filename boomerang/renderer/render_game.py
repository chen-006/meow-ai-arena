"""第 11 集的对局回放一条命令出片：按回放里的选手名自动选体素小人和顶栏名字，再交给 src/make_video.py。

    python renderer/render_game.py replays/g428.txt [-o g428.mp4] [其余参数原样传给 make_video.py，例如 --procs 3 --samples 12]

回放里的选手名是比赛时的席位名（astra、carbon、hydrogen……）。两位 Gemini 共用 Gemini 家的造型：
Carbon 用大的 `gemini`，Hydrogen 用小的 `gemini_s`，顶栏分别写 Carbon、Hydrogen（和视频里一样）。
名单里没有的名字（例如自己写的程序）按座位轮流借用 12 位的造型。
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
KEY = {'astra': 'astra', 'sol': 'sol', 'fable': 'fable', 'opus': 'opus', 'carbon': 'gemini', 'hydrogen': 'gemini_s',
       'grok': 'grok', 'glm': 'glm', 'deepseek': 'deepseek', 'kimi': 'kimi', 'qwen': 'qwen', 'mimo': 'mimo', 'doubao': 'doubao'}
SPARE = ['opus', 'astra', 'deepseek', 'grok', 'kimi', 'qwen', 'glm', 'mimo']


def players(path):
    names = {}
    with open(path, encoding='utf-8') as f:
        for ln in f:
            if ln.startswith('PLAYER '):
                p = ln.split()
                names[int(p[1])] = p[3] if len(p) > 3 else ''
            elif ln.startswith('S '):
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
    env.setdefault('DART_LABELS', 'gemini=Carbon,gemini_s=Hydrogen')
    cmd = [os.environ.get('DART_PYTHON') or sys.executable, os.path.join(HERE, 'src', 'make_video.py'), rp]
    if '--roster' not in rest:
        cmd += ['--roster', ','.join(roster)]
    print('角色：' + ' / '.join(roster))
    sys.exit(subprocess.call(cmd + rest, env=env))


if __name__ == '__main__':
    main()
