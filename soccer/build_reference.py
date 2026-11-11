"""从公开源码编译三个参考 bot 和比赛裁判；只在本地运行。"""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'workspace'))
import build
import match

BOTS = {'baseline': 'plan16', 'midfield': 'plan32_b1.5', 'sparring': 'pro_b0_n128'}


def main():
    if os.name != 'nt':
        raise SystemExit('裁判 arena.cpp 直接调用 Windows API（进程、管道、Job Object、CPU 计时），目前只能在 Windows 上编译运行。')
    compiler = build.find_compiler()
    if not compiler:
        raise SystemExit('没有找到 g++；请设置 CXX 或把编译器加入 PATH。')
    target = ROOT / 'workspace' / 'bots'
    target.mkdir(exist_ok=True)
    env = dict(os.environ)
    env['PATH'] = str(Path(compiler).resolve().parent) + os.pathsep + env.get('PATH', '')
    for name, variant in BOTS.items():
        subprocess.run([compiler, '-std=c++17', '-O2', '-static', '-s',
                        f'-DBOT_NAME="{variant}"', 'bot_main.cpp', '-o', str(target / (name + '.exe'))],
                       cwd=ROOT / 'sparring', env=env, check=True, timeout=300)
        print(f'{name}: {variant}')
    print(match.arena_exe())


if __name__ == '__main__':
    main()
