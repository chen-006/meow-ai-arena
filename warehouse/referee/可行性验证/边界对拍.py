"""边界情况对拍：随机生成大量小型极端输入，比较两个程序的输出是否逐字节一致。

  python 裁判/可行性验证/边界对拍.py 基线.exe 待测.exe [用例数] [--p2]

--p2：生成第二阶段格式（PARAMS 末尾多 agingEvery carryCost）。
"""
import os
import random
import subprocess
import sys


P2 = False


def case(seed):
    rng = random.Random(seed)
    W, H = rng.randint(3, 12), rng.randint(3, 10)
    T = rng.randint(1, 400)
    g = [['.'] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            if x in (0, W - 1) or y in (0, H - 1) or rng.random() < rng.choice([0, 0.1, 0.25]):
                g[y][x] = '#'
    inner = [(x, y) for y in range(H) for x in range(W) if g[y][x] == '.']
    if len(inner) < 2:
        g[1][1] = '.'
        inner = [(1, 1)]
    for _ in range(rng.choice([0, 1, 1, 2, 3])):
        x, y = rng.choice(inner)
        g[y][x] = 'C'
    floors = [(x, y) for y in range(H) for x in range(W) if g[y][x] != '#']
    R = rng.randint(1, min(len(floors), 12))
    starts = rng.sample(floors, R)
    full = rng.randint(1, 80)
    params = (full, rng.randint(1, 30), rng.randint(0, full), rng.randint(0, 10), rng.randint(1, 60), rng.randint(0, 4))
    events = []
    oid = 0
    allcells = [(x, y) for y in range(H) for x in range(W)]
    for t in range(T):
        for _ in range(rng.choice([0, 0, 0, 1, 1, 2, 3])):
            k = rng.random()
            if k < 0.45:
                oid += 1
                p = rng.choice(floors if rng.random() < 0.9 else allcells)
                d = rng.choice(floors if rng.random() < 0.9 else allcells)
                if rng.random() < 0.05:
                    d = p
                if rng.random() < 0.05:
                    p = rng.choice(starts)
                events.append('%d ORDER %d %d %d %d %d %d' % (t, oid, p[0], p[1], d[0], d[1], rng.randint(0, 3)))
            elif k < 0.6:
                events.append('%d CANCEL %d' % (t, rng.randint(1, oid + 2)))
            elif k < 0.85:
                x, y = rng.choice(allcells)
                events.append('%d BLOCK %d %d' % (t, x, y))
            else:
                x, y = rng.choice(allcells)
                events.append('%d UNBLOCK %d %d' % (t, x, y))
    lines = ['%d %d %d' % (W, H, T)] + [''.join(r) for r in g]
    if P2:
        params = params + (rng.choice([1, 1, 2, 5, 20, 100]), rng.choice([1, 2, 2, 3, 5]))
    lines.append('PARAMS ' + ' '.join('%d' % v for v in params))
    lines.append('ROBOTS %d' % R)
    lines += ['%d %d' % s for s in starts]
    lines.append('EVENTS %d' % len(events))
    lines += events
    return '\n'.join(lines) + '\n'


def run(exe, text):
    r = subprocess.run([exe], input=text.encode(), capture_output=True, timeout=60)
    return r.returncode, r.stdout.replace(b'\r\n', b'\n')


if __name__ == '__main__':
    argv = [x for x in sys.argv[1:] if x != '--p2']
    P2 = '--p2' in sys.argv
    a, b = os.path.abspath(argv[0]), os.path.abspath(argv[1])
    n = int(argv[2]) if len(argv) > 2 else 500
    bad = 0
    kinds = {}
    for s in range(n):
        text = case(s)
        ca, oa = run(a, text)
        cb, ob = run(b, text)
        for w in oa.split():
            if w.isupper() and w.isalpha():
                kinds[w.decode()] = kinds.get(w.decode(), 0) + 1
        if ca != 0 or cb != 0 or oa != ob:
            bad += 1
            if bad <= 3:
                path = 'diff_case_%d.in' % s
                open(path, 'w').write(text)
                la, lb = oa.split(b'\n'), ob.split(b'\n')
                i = next((i for i in range(max(len(la), len(lb))) if (la[i:i + 1] != lb[i:i + 1])), -1)
                print('用例 %d 不一致（已保存 %s），退出码 %d/%d，第 %d 行：\n  基线：%s\n  待测：%s' % (
                    s, path, ca, cb, i + 1, la[i:i + 1], lb[i:i + 1]))
    print('共 %d 个用例，不一致 %d 个' % (n, bad))
    print('覆盖到的事件类型：', ' '.join('%s=%d' % kv for kv in sorted(kinds.items())))
