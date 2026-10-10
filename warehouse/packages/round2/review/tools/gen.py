"""仓储调度负载生成器（公开版，只包含公开负载的配置）。

可以用不同的种子生成大量同类输入，用来和基线对拍；也可以自行修改参数。

  python tools/gen.py 配置名 种子 > 输出.in
  python tools/gen.py --list            # 列出所有配置

同一个 (配置名, 种子) 总是生成完全相同的文件。
"""
import random
import sys

# 每个配置侧重不同的瓶颈
PROFILES = {
    # ---- 公开负载（给选手，基线几秒到几十秒）
    'pub_small':      dict(W=40,  H=31,  T=1500, R=24,  chargers=4,  rate=0.35, cancel=0.05, block=0.02, dur=(30, 150)),
    'pub_wide':       dict(W=90,  H=61,  T=1500, R=40,  chargers=6,  rate=0.25, cancel=0.03, block=0.01, dur=(50, 200)),
    'pub_crowded':    dict(W=50,  H=40,  T=1500, R=80,  chargers=14, rate=0.8,  cancel=0.03, block=0.02, dur=(30, 120)),
    'pub_dynamic':    dict(W=60,  H=46,  T=1500, R=40,  chargers=5,  rate=0.4,  cancel=0.05, block=0.25, dur=(10, 80)),
    'pub_flood':      dict(W=50,  H=37,  T=1500, R=30,  chargers=4,  rate=1.6,  cancel=0.10, block=0.02, dur=(30, 150)),
}

# 电量参数（满电、每 tick 充电量、低电量阈值、安全余量）默认按地图尺寸缩放，见 battery_for()


def battery_for(W, H):
    full = 8 * (W + H)
    return (full, max(10, full // 20), 2 * (W + H), 30)
REPORT_EVERY = 50
HOT_RADIUS = 3


def make_grid(W, H, n_chargers, rng):
    g = [['.'] * W for _ in range(H)]
    for x in range(W):
        g[0][x] = g[H - 1][x] = '#'
    for y in range(H):
        g[y][0] = g[y][W - 1] = '#'
    # 货架：每 3 行一排（1 行货架 + 2 行通道），左侧 x=1..4 是装卸区，
    # 每隔 7~10 格留一个 2 格宽的横向通道
    for y in range(4, H - 3, 3):
        x = 6
        while x < W - 4:
            seg = rng.randint(6, 9)
            for xx in range(x, min(x + seg, W - 4)):
                g[y][xx] = '#'
            x += seg + 2
    # 充电桩：均匀分布在顶部通道（y=1），避开左侧和底部的装卸区
    step = max(1, (W - 9) // max(1, n_chargers))
    spots = [(min(W - 2, 6 + i * step), 1) for i in range(n_chargers)]
    for x, y in spots:
        g[y][x] = 'C'
    return g


def floor_cells(g):
    return [(x, y) for y in range(len(g)) for x in range(len(g[0])) if g[y][x] == '.']


def generate(name, seed):
    return generate_from(name, PROFILES[name], seed)


def generate_from(name, p, seed):
    """按配置字典 p 生成；p 里有 extra 时把它追加到 PARAMS 行末尾（第二阶段用）。"""
    rng = random.Random('%s/%d' % (name, seed))
    W, H, T, R = p['W'], p['H'], p['T'], p['R']
    battery = p.get('battery') or battery_for(W, H)
    g = make_grid(W, H, p['chargers'], rng)
    floors = floor_cells(g)
    pickups = [(x, y) for x, y in floors
               if (y > 0 and g[y - 1][x] == '#' and y - 1 > 0) or (y < H - 1 and g[y + 1][x] == '#' and y + 1 < H - 1)]
    docks = [(x, y) for x, y in floors if x <= 3 or y >= H - 3]
    walls = [(x, y) for y in range(1, H - 1) for x in range(1, W - 1) if g[y][x] == '#']
    aisles = [(x, y) for x, y in floors if 5 <= x <= W - 5]
    starts = rng.sample(floors, R)

    events = []  # (tick, 序号, 文本)
    oid = 0
    for t in range(T):
        n = 0
        r = p['rate']
        while r > 0:
            if rng.random() < min(r, 1.0):
                n += 1
            r -= 1.0
        for _ in range(n):
            oid += 1
            if rng.random() < 0.01:  # 少量无效订单（取货点是墙）
                px, py = rng.choice(walls)
            else:
                px, py = rng.choice(pickups)
            dx, dy = rng.choice(docks)
            prio = rng.choices([0, 1, 2, 3], [60, 25, 10, 5])[0]
            events.append((t, len(events), 'ORDER %d %d %d %d %d %d' % (oid, px, py, dx, dy, prio)))
            if rng.random() < p['cancel']:
                ct = t + rng.randint(1, 200)
                if ct < T:
                    events.append((ct, len(events), 'CANCEL %d' % oid))
        if rng.random() < 0.003:  # 取消一个不存在的订单
            events.append((t, len(events), 'CANCEL %d' % (oid + 100000)))
        b = p['block']
        while b > 0:
            if rng.random() < min(b, 1.0):
                x, y = rng.choice(aisles)
                d = rng.randint(*p['dur'])
                events.append((t, len(events), 'BLOCK %d %d' % (x, y)))
                if t + d < T:
                    events.append((t + d, len(events), 'UNBLOCK %d %d' % (x, y)))
            b -= 1.0
    events.sort()

    out = ['%d %d %d' % (W, H, T)]
    out += [''.join(row) for row in g]
    out.append('PARAMS %d %d %d %d %d %d' % (battery + (REPORT_EVERY, HOT_RADIUS)) +
               ''.join(' %d' % v for v in p.get('extra', ())))
    out.append('ROBOTS %d' % R)
    out += ['%d %d' % s for s in starts]
    out.append('EVENTS %d' % len(events))
    out += ['%d %s' % (t, text) for t, _, text in events]
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    if len(sys.argv) == 2 and sys.argv[1] == '--list':
        for k, v in PROFILES.items():
            print(k, v)
        sys.exit(0)
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    sys.stdout.write(generate(sys.argv[1], int(sys.argv[2])))
