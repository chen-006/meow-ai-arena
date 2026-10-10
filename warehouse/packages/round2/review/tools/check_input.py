"""输入合法性检查器：检查一个输入文件是否符合 SPEC.md 的输入约定，以及互测用例的规模上限。

  python tools/check_input.py 用例.in              # 按互测用例的上限检查
  python tools/check_input.py 用例.in --full       # 按正式负载的上限检查（W,H ≤ 200 等）
  python tools/check_input.py 用例.in --p2         # 第二阶段格式（PARAMS 末尾多 agingEvery carryCost）

合法时输出 OK 并以 0 退出；否则输出第一个问题并以 1 退出。

格式要求（比 SPEC 更严格的地方是为了避免歧义）：
  * 每一项之间恰好一个空格，行首行尾没有空格，没有空行；换行用 \\n 或 \\r\\n；
  * 整数写成标准十进制（可以有负号，没有前导零、没有加号）。
"""
import re
import sys
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')  # Windows 控制台默认 GBK，统一输出 UTF-8

INT = re.compile(r'^(0|-?[1-9][0-9]*)$')
POS = re.compile(r'^[1-9][0-9]*$')

LIMITS_CASE = dict(WH=60, T=3000, R=40, E=20000)
LIMITS_FULL = dict(WH=200, T=20000, R=300, E=200000)


class Bad(Exception):
    pass


def check(text, full=False, p2=False):
    lim = LIMITS_FULL if full else LIMITS_CASE
    lines = text.replace('\r\n', '\n').split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    pos = [0]

    def nxt(what):
        if pos[0] >= len(lines):
            raise Bad('第 %d 行：文件提前结束，缺少%s' % (pos[0] + 1, what))
        s = lines[pos[0]]
        pos[0] += 1
        if s != s.strip(' ') or '  ' in s or '\t' in s:
            raise Bad('第 %d 行：多余的空白' % pos[0])
        return s

    def ints(tokens, lo, hi, names):
        out = []
        for t, (a, b), n in zip(tokens, zip(lo, hi), names):
            if not INT.match(t):
                raise Bad('第 %d 行：%s 不是标准整数：%r' % (pos[0], n, t))
            v = int(t)
            if not a <= v <= b:
                raise Bad('第 %d 行：%s=%d 超出范围 [%d, %d]' % (pos[0], n, v, a, b))
            out.append(v)
        return out

    def expect(s, k, what):
        tok = s.split(' ')
        if len(tok) != k:
            raise Bad('第 %d 行：%s 应有 %d 项，实际 %d 项' % (pos[0], what, k, len(tok)))
        return tok

    W, H, T = ints(expect(nxt('首行'), 3, '首行 W H T'), [3, 3, 1], [lim['WH'], lim['WH'], lim['T']], ['W', 'H', 'T'])
    g = []
    for y in range(H):
        row = nxt('地图')
        if len(row) != W or set(row) - set('.#C'):
            raise Bad('第 %d 行：地图行必须恰好 %d 个字符，只能是 . # C' % (pos[0], W))
        g.append(row)
    for x in range(W):
        if g[0][x] != '#' or g[H - 1][x] != '#':
            raise Bad('地图四周必须是墙')
    for y in range(H):
        if g[y][0] != '#' or g[y][W - 1] != '#':
            raise Bad('地图四周必须是墙')

    tok = expect(nxt('PARAMS'), 9 if p2 else 7, 'PARAMS 行')
    if tok[0] != 'PARAMS':
        raise Bad('第 %d 行：应以 PARAMS 开头' % pos[0])
    names = ['batteryMax', 'chargeRate', 'lowThreshold', 'safetyMargin', 'reportEvery', 'hotRadius']
    lo, hi = [1, 1, 0, 0, 1, 0], [100000, 100000, 100000, 1000, 1000000, 400]
    if p2:
        names += ['agingEvery', 'carryCost']
        lo += [1, 1]
        hi += [1000000000, 100]
    pv = ints(tok[1:], lo, hi, names)
    bmax = pv[0]
    if pv[1] > bmax or pv[2] > bmax:
        raise Bad('第 %d 行：chargeRate 和 lowThreshold 不能超过 batteryMax' % pos[0])

    tok = expect(nxt('ROBOTS'), 2, 'ROBOTS 行')
    if tok[0] != 'ROBOTS':
        raise Bad('第 %d 行：应为 ROBOTS R' % pos[0])
    R, = ints(tok[1:], [1], [lim['R']], ['R'])
    seen = set()
    for i in range(R):
        x, y = ints(expect(nxt('机器人位置'), 2, '机器人位置'), [0, 0], [W - 1, H - 1], ['x', 'y'])
        if g[y][x] == '#':
            raise Bad('第 %d 行：机器人初始位置是墙' % pos[0])
        if (x, y) in seen:
            raise Bad('第 %d 行：机器人初始位置重复' % pos[0])
        seen.add((x, y))

    tok = expect(nxt('EVENTS'), 2, 'EVENTS 行')
    if tok[0] != 'EVENTS':
        raise Bad('第 %d 行：应为 EVENTS E' % pos[0])
    E, = ints(tok[1:], [0], [lim['E']], ['E'])
    last = 0
    ids = set()
    C = 1000  # 事件里的坐标可以在地图外，但限制在 [-1000, 1000]
    for i in range(E):
        s = nxt('事件')
        tok = s.split(' ')
        if len(tok) < 2:
            raise Bad('第 %d 行：事件格式错误' % pos[0])
        t, = ints(tok[:1], [0], [T - 1], ['tick'])
        if t < last:
            raise Bad('第 %d 行：事件的 tick 必须非递减' % pos[0])
        last = t
        typ = tok[1]
        if typ == 'ORDER':
            expect(s, 8, 'ORDER 事件')
            if not POS.match(tok[2]) or int(tok[2]) > 10 ** 9:
                raise Bad('第 %d 行：订单编号必须是 1..10^9 的正整数，没有前导零' % pos[0])
            if tok[2] in ids:
                raise Bad('第 %d 行：订单编号重复' % pos[0])
            ids.add(tok[2])
            ints(tok[3:], [-C] * 4 + [0], [C] * 4 + [1000], ['px', 'py', 'dx', 'dy', 'priority'])
        elif typ == 'CANCEL':
            expect(s, 3, 'CANCEL 事件')
            if not POS.match(tok[2]) or int(tok[2]) > 10 ** 9:
                raise Bad('第 %d 行：CANCEL 的编号必须是 1..10^9 的正整数，没有前导零' % pos[0])
        elif typ in ('BLOCK', 'UNBLOCK'):
            expect(s, 4, typ + ' 事件')
            ints(tok[2:], [-C, -C], [C, C], ['x', 'y'])
        else:
            raise Bad('第 %d 行：未知事件类型 %r' % (pos[0], typ))
    if pos[0] != len(lines):
        raise Bad('第 %d 行：EVENTS 之后还有多余内容' % (pos[0] + 1))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) != 1:
        print(__doc__)
        sys.exit(2)
    with open(args[0], 'rb') as f:
        data = f.read()
    try:
        text = data.decode('ascii')
        check(text, full='--full' in sys.argv, p2='--p2' in sys.argv)
    except UnicodeDecodeError:
        print('不合法：文件里有非 ASCII 字符')
        sys.exit(1)
    except Bad as e:
        print('不合法：%s' % e)
        sys.exit(1)
    print('OK')


if __name__ == '__main__':
    main()
