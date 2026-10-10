"""核对出题目录里的 workspace/ 和发给选手的 workspace.zip 是否完全一致（防止被误改）。

  python 裁判/核对工作区.py
"""
import hashlib
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')

z = zipfile.ZipFile(os.path.join(ROOT, 'workspace.zip'))
names = {n for n in z.namelist() if not n.endswith('/')}
bad = []
for n in sorted(names):
    p = os.path.join(ROOT, n)
    if not os.path.exists(p) or hashlib.sha256(open(p, 'rb').read()).digest() != hashlib.sha256(z.read(n)).digest():
        bad.append(n)
disk = set()
for d, _, files in os.walk(os.path.join(ROOT, 'workspace')):
    for f in files:
        disk.add(os.path.relpath(os.path.join(d, f), ROOT).replace(os.sep, '/'))
extra = sorted(x for x in disk - names if '/build/' not in x and '__pycache__' not in x)
print('压缩包里 %d 个文件；与压缩包不一致：%s；磁盘上多出来的：%s' % (len(names), bad or '无', extra or '无'))
sys.exit(1 if bad or extra else 0)
