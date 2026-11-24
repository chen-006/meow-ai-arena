"""编译飞镖乱斗程序（与正式比赛相同的编译方式）。

  python build.py player.cpp              编译成 player.exe（放在源文件旁边）
  python build.py player.cpp -o x.exe     指定输出
  python build.py --show                  只显示找到的编译器和正式编译命令

正式编译命令（在只含你的源文件和 dart.hpp 的临时目录里执行）：
  g++ -std=c++17 -O2 -static -s -Wl,--stack,67108864 <你的文件>.cpp -o player.exe
- 只能是一个 .cpp 文件；可以 #include "dart.hpp"（裁判用的同一份引擎）和标准库，不能有别的本地头文件。
- 编译前先做源码检查（check_source，与正式编译相同）：拒绝系统 / 进程类头文件（windows.h、unistd.h 等）、
  除 dart.hpp 以外的本地头文件和绝对路径包含、内联汇编、dllimport。标准库（含 <bits/stdc++.h>、SIMD 头文件）不受影响。
  这只是辅助检查；运行时由裁判限制资源和子进程，文件和网络使用范围见 RULES.md。
- 编译器找法：环境变量 CXX → PATH 里的 g++ → PATH 里的 clang++ → 常见的 w64devkit 位置。
- 结果按"源码 + dart.hpp + 编译命令"的哈希缓存在 build/ 里，没改过就不重新编译。
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

KIT = Path(__file__).resolve().parent
BUILD = KIT / 'build'
FLAGS = ['-std=c++17', '-O2', '-static', '-s', '-Wl,--stack,67108864']


class BuildError(Exception):
    pass


# ---------------------------------------------------------------- 源码检查（正式编译也用这一份）

# 明显的系统 / 进程类头文件（小写比较）。另外：以 win 或 nt 开头、以 api.h 结尾的头文件，sys/ netinet/ arpa/ ddk/ 下的头文件，也拒绝。
BLOCKED_HEADERS = {
    'tlhelp32.h', 'psapi.h', 'shellapi.h', 'shlobj.h', 'shlwapi.h', 'ws2tcpip.h', 'ws2def.h', 'iphlpapi.h', 'aclapi.h',
    'sddl.h', 'userenv.h', 'objbase.h', 'ole2.h', 'accctrl.h', 'lmcons.h', 'wtsapi32.h', 'dbghelp.h', 'imagehlp.h',
    'io.h', 'direct.h', 'process.h', 'conio.h', 'dos.h', 'share.h', 'unistd.h', 'fcntl.h', 'dlfcn.h', 'spawn.h',
    'dirent.h', 'pwd.h', 'grp.h', 'poll.h', 'netdb.h', 'ifaddrs.h', 'termios.h', 'mmsystem.h',
}
BLOCKED_PREFIX_DIRS = ('sys/', 'netinet/', 'arpa/', 'ddk/', 'linux/', 'asm/')


def _strip_comments(text, blank_strings):
    """去掉注释、把反斜杠续行接起来；blank_strings=True 时字符串 / 字符字面量的内容也去掉。"""
    text = text.replace('\r\n', '\n').replace('\r', '\n')
    text = re.sub(r'\\\n', '', text)
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            out.append(' ')
            i = n if j < 0 else j + 2
        elif c == 'R' and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            m = re.match(r'R"([^()\\ \n]{0,16})\(', text[i:])
            if m:
                end = text.find(')' + m.group(1) + '"', i + m.end())
                j = n if end < 0 else end + len(m.group(1)) + 2
                out.append('""' + '\n' * text.count('\n', i, j))
                i = j
            else:
                out.append(c)
                i += 1
        elif c == '"' or (c == "'" and not (i > 0 and text[i - 1].isalnum())):  # 1'000'000 的数字分隔符不算
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            out.append(c + c if blank_strings else text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def check_source(text):
    """检查选手源码。返回问题列表（空 = 通过）。只拦明显的系统 / 进程类用法，不管标准库。"""
    probs = []
    code = _strip_comments(text, False)
    for no, line in enumerate(code.split('\n'), 1):
        s = line.strip()
        if s.startswith('%:'):          # 二连符 %: 等于 #
            s = '#' + s[2:]
        m = re.match(r'#\s*(include_next|include|import)\b\s*(.*)$', s)
        if not m:
            continue
        arg = m.group(2).strip()
        if m.group(1) != 'include':
            probs.append(f'第 {no} 行：不能用 #{m.group(1)}')
            continue
        q = re.match(r'"([^"]*)"', arg)
        a = re.match(r'<([^>]*)>', arg)
        if q:
            if q.group(1).strip() != 'dart.hpp':
                probs.append(f'第 {no} 行：只能 #include "dart.hpp"，不能包含别的本地文件（"{q.group(1)}"）')
        elif a:
            h = a.group(1).strip().replace('\\', '/')
            low = h.lower()
            base = low.rsplit('/', 1)[-1]
            if ':' in h or low.startswith('/') or '..' in low:
                probs.append(f'第 {no} 行：不能用绝对路径或上级目录包含（<{h}>）')
            elif (low in BLOCKED_HEADERS or low.startswith(BLOCKED_PREFIX_DIRS) or
                  (('/' not in low) and (base.startswith('win') or base.startswith('nt') or base.endswith('api.h')))):
                probs.append(f'第 {no} 行：不能包含系统 / 进程类头文件 <{h}>（只能用标准库和 dart.hpp）')
        else:
            probs.append(f'第 {no} 行：#include 只能写成 <标准库头文件> 或 "dart.hpp"（不能用宏）')
    flat = re.sub(r'\s+', ' ', _strip_comments(text, True))
    if re.search(r'(?<![A-Za-z0-9_])(asm|__asm|__asm__)(?![A-Za-z0-9_])', flat):
        probs.append('不能用内联汇编（asm / __asm__）')
    if re.search(r'(?<![A-Za-z0-9_])dllimport(?![A-Za-z0-9_])', flat):
        probs.append('不能用 dllimport（直接调用系统函数）')
    return probs


def find_compiler():
    cxx = os.environ.get('CXX')
    if cxx and (shutil.which(cxx) or Path(cxx).is_file()):
        return shutil.which(cxx) or cxx
    for name in ('g++', 'clang++'):
        p = shutil.which(name)
        if p:
            return p
    for base in KIT.parents:
        guess = base / 'tools' / 'w64devkit' / 'bin' / 'g++.exe'
        if guess.is_file():
            return str(guess)
    for guess in (Path('C:/w64devkit/bin/g++.exe'), Path('C:/msys64/ucrt64/bin/g++.exe'), Path('C:/msys64/mingw64/bin/g++.exe')):
        if guess.is_file():
            return str(guess)
    return None


def compile_cpp(src, out=None, quiet=False):
    """编译一个 .cpp，返回 exe 路径（Path）。失败抛 BuildError（带编译器输出）。"""
    src = Path(src).resolve()
    if not src.is_file():
        raise BuildError(f'找不到源文件：{src}')
    cxx = find_compiler()
    if not cxx:
        raise BuildError('没有找到 C++ 编译器（g++ 或 clang++）。请把编译器的 bin 目录加进 PATH，或设置环境变量 CXX。')
    data = src.read_bytes()
    probs = check_source(data.decode('utf-8', errors='replace'))
    if probs:
        raise BuildError(f'源码检查没通过（{src.name}）：\n  ' + '\n  '.join(probs))
    if not (KIT / 'dart.hpp').is_file():
        raise BuildError(f'找不到 dart.hpp（应该和 build.py 放在同一个目录：{KIT}）')
    hdr = (KIT / 'dart.hpp').read_bytes()
    key = hashlib.sha256(data + hdr + ' '.join(FLAGS).encode()).hexdigest()[:16]
    BUILD.mkdir(exist_ok=True)
    cached = BUILD / f'{src.stem}-{key}.exe'
    if not cached.is_file():
        work = Path(tempfile.mkdtemp(prefix='c-', dir=BUILD))
        try:
            (work / 'player.cpp').write_bytes(data)
            (work / 'dart.hpp').write_bytes(hdr)
            env = dict(os.environ)
            env['PATH'] = str(Path(cxx).resolve().parent) + os.pathsep + env.get('PATH', '')
            # 在临时目录里用相对路径编译，避开路径里的中文和空格
            try:
                r = subprocess.run([cxx, *FLAGS, 'player.cpp', '-o', 'player.exe'], cwd=work, capture_output=True,
                                   text=True, encoding='utf-8', errors='replace', env=env, timeout=300)
            except subprocess.TimeoutExpired:
                raise BuildError('编译超过 300 秒')
            if r.returncode != 0 or not (work / 'player.exe').is_file():
                msg = (r.stderr or r.stdout).replace('player.cpp', src.name)
                raise BuildError(f'编译失败（{src.name}）：\n' + msg[-4000:].rstrip())
            os.replace(work / 'player.exe', cached)
        finally:
            if not os.environ.get('DART_KEEP_BUILD'):
                shutil.rmtree(work, ignore_errors=True)
    if out is None:
        return cached
    out = Path(out)
    shutil.copyfile(cached, out)
    return out


def main():
    ap = argparse.ArgumentParser(description='编译飞镖乱斗程序（与正式比赛相同的编译方式）')
    ap.add_argument('src', nargs='?')
    ap.add_argument('-o', '--out')
    ap.add_argument('--show', action='store_true')
    a = ap.parse_args()
    if a.show or not a.src:
        print('编译器：', find_compiler() or '没找到')
        print('正式编译命令：', 'g++ ' + ' '.join(FLAGS) + ' <你的文件>.cpp -o player.exe   （同目录放 dart.hpp）')
        return
    try:
        out = compile_cpp(a.src, a.out or Path(a.src).with_suffix('.exe'))
    except BuildError as e:
        print(e, file=sys.stderr)
        sys.exit(1)
    print(f'编译成功：{out}')


if __name__ == '__main__':
    main()

