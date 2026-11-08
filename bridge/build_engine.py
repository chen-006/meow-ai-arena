"""编译物理内核：engine/bridge_sim.cpp → workspace/bridge_sim.exe（bridge.py 默认在同目录找这个文件名）。

  python build_engine.py

比赛时选手包里的 bridge_sim.exe 由 `g++ -O2 -std=c++17 -static -municode` 编译（w64devkit，GCC 16.2.0），
sha256 = 4186445eec8989b344126c6e60eb8df8de0a3ca6543f83d06868a623c87209e7。同版本编译器可得到逐字节相同的文件；
别的编译器版本或平台不保证字节相同，但引擎只用加减乘除、sqrt 和 fabs，不要加 -ffast-math，结果应与比赛一致
（用 python tournament/score.py 核对）。macOS / Linux 也输出名为 bridge_sim.exe 的可执行文件，bridge.py 不用改。
"""
import hashlib, os, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OFFICIAL = '4186445eec8989b344126c6e60eb8df8de0a3ca6543f83d06868a623c87209e7'


def find_compiler():
    cxx = os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++')
    if not cxx and os.name == 'nt':
        for guess in (ROOT.parent / 'tools' / 'w64devkit' / 'bin' / 'g++.exe', Path('C:/w64devkit/bin/g++.exe')):
            if guess.is_file():
                return str(guess)
    return cxx


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    cxx = find_compiler()
    if not cxx:
        raise SystemExit('没有找到 g++：设置 CXX、加入 PATH，或把 w64devkit 放到仓库的 tools/w64devkit/。')
    out = ROOT / 'workspace' / 'bridge_sim.exe'
    flags = ['-O2', '-std=c++17'] + (['-static', '-municode'] if os.name == 'nt' else [])
    env = dict(os.environ)
    env['PATH'] = str(Path(cxx).resolve().parent) + os.pathsep + env.get('PATH', '')
    subprocess.run([cxx, *flags, '-o', str(out), str(ROOT / 'engine' / 'bridge_sim.cpp')], env=env, check=True, timeout=300)
    sha = hashlib.sha256(out.read_bytes()).hexdigest()
    print(f'已编译：{out}\nsha256：{sha}' + ('（与比赛时的 exe 逐字节相同）' if sha == OFFICIAL else '（与比赛时的 exe 字节不同，可用 tournament/score.py 核对结果）'))


if __name__ == '__main__':
    main()
