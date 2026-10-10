"""对比两个程序在一组输入上的输出与 CPU 时间；可选再和一组旧 .out 比，统计差异行数。

  python 裁判/可行性验证/对比.py a.exe b.exe 输入1.in 输入2.in ... [--old 旧输出目录]
"""
import os
import sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'workspace', 'tools'))
import measure  # noqa: E402

args = sys.argv[1:]
old = None
if '--old' in args:
    i = args.index('--old')
    old = args[i + 1]
    del args[i:i + 2]
a, b, inputs = os.path.abspath(args[0]), os.path.abspath(args[1]), args[2:]
for inp in inputs:
    ra, rb = measure.run(a, inp), measure.run(b, inp)
    same = ra['digest'] == rb['digest'] and ra['code'] == 0 and rb['code'] == 0
    extra = ''
    if old:
        n = os.path.splitext(os.path.basename(inp))[0]
        o = open(os.path.join(old, n + '.out'), 'rb').read()
        la, lo = measure.normalized(ra['output']).split(b'\n'), measure.normalized(o).split(b'\n')
        extra = '  与旧输出不同的行 %d/%d' % (sum(1 for x, y in zip(la, lo) if x != y) + abs(len(la) - len(lo)), len(lo))
    print('%-20s %s  A %.2fs  B %.2fs%s' % (os.path.basename(inp), '一致' if same else '不一致!', ra['cpu'], rb['cpu'], extra), flush=True)
