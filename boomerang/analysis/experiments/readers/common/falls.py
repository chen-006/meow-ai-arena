"""落坑分类：python falls.py 名字 回放目录...  → 统计这个名字的每次 FALL 属于哪一类"""
import sys, re, collections
from pathlib import Path
def parse(path):
    L = Path(path).read_text(encoding='utf-8', errors='replace').splitlines()
    names = {}; grid = []; wave = []; waves = []; i = 0; W = H = 0
    while i < len(L):
        s = L[i]
        if s.startswith('MAP '):
            p = s.split(); W, H = int(p[1]), int(p[2]); grid = L[i+1:i+1+H]; i += H
        elif s == 'WAVE':
            wave = L[i+1:i+1+H]; i += H
        elif s.startswith('WAVES '):
            p = s.split(); waves = [int(x) for x in p[2:]]
        elif s.startswith('PLAYER '):
            p = s.split(); names[int(p[1])] = p[3]
        elif s.startswith('S '):
            break
        i += 1
    ev = collections.defaultdict(list)
    for s in L[i:]:
        if s.startswith('E '):
            p = s.split(); ev[int(p[1])].append(p[2:])
    return names, grid, wave, waves, ev
def cell(grid, wave, waves, x, y, t):
    cx, cy = x // 500, y // 500
    if cy < 0 or cy >= len(grid) or cx < 0 or cx >= len(grid[0]): return 'out'
    c = grid[cy][cx]
    if c == '2': return 'pit'
    w = wave[cy][cx] if wave else '.'
    if w != '.':
        wi = int(w, 36)
        if wi < len(waves) and waves[wi] <= t: return 'collapsed'
    return 'floor'
def classify(path, who):
    names, grid, wave, waves, ev = parse(path)
    seats = [k for k, v in names.items() if v == who]
    out = []
    for t, es in ev.items():
        for e in es:
            if e[0] != 'FALL': continue
            s = int(e[1])
            if s not in seats: continue
            killer = int(e[2]); x, y = int(e[3]), int(e[4])
            recent = lambda typ, back: any(any(q[0] == typ and int(q[1]) == s for q in ev.get(tt, [])) for tt in range(t - back, t + 1))
            kind = cell(grid, wave, waves, x, y, t)
            if killer >= 0: c = '被踢下坑'
            elif recent('DASH', 9) and (recent('SLASH', 4) or recent('KICK', 4)): c = '冲刺中接近战后落坑'
            elif recent('CANCEL', 1): c = '取消召回同帧落坑'
            elif recent('DASH', 9): c = '冲刺落坑'
            elif recent('KICKHIT', 0) or recent('SLASH', 3) or recent('KICK', 3): c = '出招后落坑'
            elif kind == 'collapsed': c = '塌陷区落坑'
            else: c = '自己走进坑/场外'
            out.append((c, path, t))
    return out
if __name__ == '__main__':
    who = sys.argv[1]; res = []
    for d in sys.argv[2:]:
        for f in sorted(Path(d).glob('g*.txt')): res += classify(f, who)
    cnt = collections.Counter(c for c, _, _ in res)
    print(who, '共', len(res), dict(cnt.most_common()))
    ex = {}
    for c, p, t in res: ex.setdefault(c, []).append(f'{Path(p).parent.name}/{Path(p).name}@{t/20:.2f}s')
    for c, l in ex.items(): print(' ', c, l[:4])
