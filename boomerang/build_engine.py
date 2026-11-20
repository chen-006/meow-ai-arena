"""把选手包补成比赛时的样子：编译裁判 workspace/arena.exe 和三档陪练 workspace/bots/weak.exe、mid.exe、strong.exe。

    python build_engine.py

比赛时选手包里这四个是编译好的程序；仓库不提交 exe，改为从源码编译：
- arena.exe：workspace/arena.cpp + dart.hpp（选手包原样），编译命令和 workspace/match.py 的 compile_arena 相同。
- 陪练：engine/sparring/*.cpp，用 workspace/build.py 的正式编译命令（g++ -std=c++17 -O2 -static -Wl,--stack,67108864）。
编译器：环境变量 CXX → PATH 里的 g++ → 仓库 tools/w64devkit/bin/g++。编完和比赛时的 SHA-256 比一下（见 source_manifest.json），
用比赛时同一版本的编译器（w64devkit GCC 16.2.0）通常逐字节相同；不同也不影响使用，规则全在 dart.hpp 里。
"""
import hashlib
import json
import os
import shutil
import sys
from pathlib import Path

EP = Path(__file__).resolve().parent
WS = EP / 'workspace'
if not os.environ.get('CXX') and not shutil.which('g++'):
    w64 = EP.parent / 'tools' / 'w64devkit' / 'bin' / 'g++.exe'
    if w64.is_file():
        os.environ['CXX'] = str(w64)
sys.path.insert(0, str(WS))
import build  # noqa: E402
import match  # noqa: E402


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    man = json.load(open(EP / 'source_manifest.json', encoding='utf-8'))['files']
    done = []
    if (WS / 'arena.exe').exists():
        (WS / 'arena.exe').unlink()
    match.compile_arena()
    done.append('arena.exe')
    (WS / 'bots').mkdir(exist_ok=True)
    for b in ('weak', 'mid', 'strong'):
        build.compile_cpp(EP / 'engine' / 'sparring' / f'{b}.cpp', WS / 'bots' / f'{b}.exe')
        done.append(f'bots/{b}.exe')
    for f in done:
        want = man.get(f'workspace/{f}', {}).get('original_sha256')
        got = sha(WS / f)
        print(f'{f:16} {got[:16]}…  ' + ('与比赛时逐字节相同' if got == want else f'与比赛时不同（比赛时 {str(want)[:16]}…）'))
    print('完成。之后在 workspace/ 下照 README.md 用 match.py 对战。')


if __name__ == '__main__':
    main()
