"""在合法性检查器允许的整个输入空间里随机生成极端输入（模拟攻手），比较多个程序的输出。

  python 裁判/可行性验证/极端对拍.py 标准.exe 待测1.exe [待测2.exe ...] [--n 2000] [--p2]

每个用例都先过 tools/check_input.py；不一致的用例保存为 extreme_fail_<种子>.in。
"""
import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'workspace', 'tools'))
import check_input  # noqa: E402


def case(seed, p2):
    rng = random.Random('extreme/%d' % seed)
    W, H = rng.randint(3, rng.choice([5, 12, 30, 60])), rng.randint(3, rng.choice([5, 12, 30, 60]))
    T = rng.choice([1, 2, 50, 300, 1000, 3000])
    wall_p = rng.choice([0, 0.05, 0.2, 0.4])
    g = [['#' if x in (0, W - 1) or y in (0, H - 1) or rng.random() < wall_p else '.' for x in range(W)] for y in range(H)]
    inner = [(x, y) for y in range(1, H - 1) for x in range(1, W - 1)]
    if all(g[y][x] == '#' for x, y in inner):
        x, y = inner[0]
        g[y][x] = '.'
    floors = [(x, y) for x, y in inner if g[y][x] != '#']
    for _ in range(rng.choice([0, 1, 2, 5, 20])):
        x, y = rng.choice(floors)
        g[y][x] = 'C'
    R = rng.randint(1, min(40, len(floors)))
    starts = rng.sample(floors, R)
    bmax = rng.choice([1, 2, 5, 30, 300, 3000, 100000])
    params = [bmax, rng.randint(1, bmax), rng.choice([0, bmax, rng.randint(0, bmax)]), rng.choice([0, 1, 10, 1000]),
              rng.choice([1, 1, 7, 50, 1000000]), rng.choice([0, 1, 3, 400])]
    if p2:
        params += [rng.choice([1, 2, 30, 1000000000]), rng.choice([1, 2, 3, 100])]
    allc = [(x, y) for y in range(H) for x in range(W)]
    ev = []
    oid = 0
    ids = []
    density = rng.choice([0.2, 1, 3])
    budget = 20000
    for t in range(T):
        k = int(density) + (1 if rng.random() < density - int(density) else 0)
        for _ in range(k):
            if len(ev) >= budget:
                break
            r = rng.random()
            if r < 0.45:
                oid += rng.choice([1, 1, 1, 7, 900])
                if oid > 10 ** 9:
                    continue
                ids.append(oid)
                pick = lambda: rng.choice(floors) if rng.random() < 0.8 else (rng.choice(allc) if rng.random() < 0.7 else (rng.randint(-1000, 1000), rng.randint(-1000, 1000)))
                p, d = pick(), pick()
                if rng.random() < 0.1:
                    d = p
                if rng.random() < 0.05:
                    p = rng.choice(starts)
                pr = rng.choice([0, 0, 1, 3, 1000, rng.randint(0, 1000)])
                ev.append('%d ORDER %d %d %d %d %d %d' % (t, oid, p[0], p[1], d[0], d[1], pr))
            elif r < 0.6:
                c = rng.choice(ids) if ids and rng.random() < 0.8 else rng.randint(1, 10 ** 9)
                ev.append('%d CANCEL %d' % (t, c))
            elif r < 0.85:
                x, y = rng.choice(allc) if rng.random() < 0.9 else (rng.randint(-1000, 1000), rng.randint(-1000, 1000))
                if rng.random() < 0.2:
                    x, y = rng.choice(starts)
                ev.append('%d BLOCK %d %d' % (t, x, y))
            else:
                x, y = rng.choice(allc)
                ev.append('%d UNBLOCK %d %d' % (t, x, y))
    lines = ['%d %d %d' % (W, H, T)] + [''.join(r) for r in g]
    lines.append('PARAMS ' + ' '.join(map(str, params)))
    lines.append('ROBOTS %d' % R)
    lines += ['%d %d' % s for s in starts]
    lines.append('EVENTS %d' % len(ev))
    lines += ev
    return '\n'.join(lines) + '\n'


def run(exe, text, timeout=300):
    r = subprocess.run([exe], input=text.encode(), capture_output=True, timeout=timeout)
    return r.returncode, r.stdout.replace(b'\r\n', b'\n')


if __name__ == '__main__':
    p2 = '--p2' in sys.argv
    argv = [a for a in sys.argv[1:] if a != '--p2']
    n = 2000
    if '--n' in argv:
        i = argv.index('--n')
        n = int(argv[i + 1])
        del argv[i:i + 2]
    exes = [os.path.abspath(a) for a in argv]
    bad = [0] * len(exes)
    skipped = 0
    for s in range(n):
        text = case(s, p2)
        check_input.check(text, p2=p2)
        try:
            ref = run(exes[0], text, timeout=5)   # 太慢的用例跳过（击穿用例要求基线 10 秒内跑完）
        except subprocess.TimeoutExpired:
            skipped += 1
            continue
        if ref[0] != 0:
            print('用例 %d：标准程序退出码 %d' % (s, ref[0]))
        for j, e in enumerate(exes[1:], 1):
            if run(e, text) != ref:
                bad[j] += 1
                if bad[j] <= 2:
                    open('extreme_fail_%d.in' % s, 'w', newline='\n').write(text)
                    print('用例 %d：%s 与标准不一致' % (s, os.path.basename(e)))
    print('共 %d 个用例（跳过太慢的 %d 个）；' % (n, skipped) + '，'.join('%s 不一致 %d' % (os.path.basename(e), b) for e, b in zip(exes[1:], bad[1:])))
