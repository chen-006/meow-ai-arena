"""圈地对战循环赛：1 对 1 单挑 + 4 人混战。

每个参赛者是一个含 bot.cpp 的文件夹，文件夹名即 bot 编号，格式为 <模型>@<工具>#<序号>
（例如 claude-opus-5.5@claude-code#1）。'#' 前面相同的属于同一模型，互相不对战。
三个基准 bot（anchor-random、anchor-greedy、anchor-sparring 出题方参考 bot）自动加入。

单挑：每对选手使用同一批地图（种子 seed_base 起共 maps 张），每张图交换出生位各打一局。
混战：共 melee_rounds 轮。每轮把全部 bot 随机分成若干桌、每桌 4 人（人数不是 4 的倍数时，
      随机抽几个 bot 多打一桌补齐），同一桌在同一张图上轮换座位打 4 局。
结果逐局追加到 JSONL 文件；中断后重新运行同一命令，会从中断处继续。

  python tournament/round_robin.py --entries bots --out results/rerun.jsonl --maps 200 --melee-rounds 300 --jobs 12
"""
import argparse
import json
import os
import random
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed

HERE = os.path.dirname(os.path.abspath(__file__))
WORKSPACE = os.path.join(HERE, '..', 'workspace')
sys.path.insert(0, os.path.join(WORKSPACE, 'engine'))
import build  # noqa: E402
import referee  # noqa: E402

ANCHORS = {'anchor-random': os.path.join(WORKSPACE, 'anchors', 'random.cpp'),
           'anchor-greedy': os.path.join(WORKSPACE, 'anchors', 'greedy.cpp'),
           # 出题方的参考 bot，不发给选手，只作为每期固定的强度参照
           'anchor-sparring': os.path.join(HERE, 'sparring.cpp')}
MELEE_SEED_OFFSET = 5000


def model_of(bot_id):
    return bot_id.split('#')[0]


def play(job):
    key, seats, exes, seed, replay = job
    r = referee.run_match(exes, seats, seed, replay_path=replay)
    r['key'] = key
    if len(seats) == 2:
        r['swap'] = key[3]
    return r


def melee_tables(ids, rnd, seed_base):
    """每轮的分桌：[[bot × 4], ...]。同一模型的多次运行不同桌。"""
    rng = random.Random(seed_base * 1000 + rnd)
    for _ in range(10000):
        order = ids[:]
        rng.shuffle(order)
        tables = [order[i:i + 4] for i in range(0, len(order), 4)]
        last = tables[-1]
        while len(last) < 4:
            cand = [b for b in ids if b not in last and all(model_of(b) != model_of(x) for x in last)]
            if not cand:
                break
            last.append(rng.choice(cand))
        if all(len(t) == 4 and len({model_of(b) for b in t}) == 4 for t in tables):
            return tables
    raise RuntimeError('无法分桌：不同模型的 bot 少于 4 个')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--entries', required=True, help='参赛文件夹（<编号>/bot.cpp）')
    ap.add_argument('--out', required=True, help='对局结果 JSONL 文件')
    ap.add_argument('--maps', type=int, default=5, help='单挑：每对选手的地图数，每张打两局（默认 5，即 10 局）')
    ap.add_argument('--melee-rounds', type=int, default=10, help='混战轮数（默认 10），0 = 不打混战')
    ap.add_argument('--seed-base', type=int, default=1000)
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) // 2 - 1),
                    help='同时进行的单挑局数；每局运行 2 个 bot，2×jobs 应小于 CPU 核数。'
                         '混战每局运行 4 个 bot，同时进行的局数自动减半')
    ap.add_argument('--replays', help='保存回放的文件夹')
    ap.add_argument('--replay-maps', type=int, default=1, help='只保存前 K 张地图（单挑）/ 前 K 轮（混战）的回放')
    ap.add_argument('--only', choices=['duel', 'melee'], help='只打单挑或只打混战')
    ap.add_argument('--no-anchor', action='append', default=[], metavar='ID', help='不加入某个基准 bot（可重复）')
    ap.add_argument('--low-priority', action='store_true',
                    help='以低于正常的优先级运行（Windows 上 bot 进程会继承），把 CPU 优先让给其他程序')
    args = ap.parse_args()
    if args.low_priority and sys.platform == 'win32':
        import ctypes
        k32 = ctypes.windll.kernel32
        k32.GetCurrentProcess.restype = ctypes.c_void_p
        k32.SetPriorityClass(ctypes.c_void_p(k32.GetCurrentProcess()), 0x4000)  # BELOW_NORMAL_PRIORITY_CLASS

    build_dir = os.path.join(os.path.dirname(os.path.abspath(args.out)), 'build')
    bots = {}
    for bid, f in ANCHORS.items():
        if bid in args.no_anchor:
            continue
        bots[bid] = build.compile_bot(f, build_dir)
    for name in sorted(os.listdir(args.entries)):
        src = os.path.join(args.entries, name, 'bot.cpp')
        if not os.path.isfile(src):
            continue
        try:
            bots[name] = build.compile_bot(src, build_dir)
        except RuntimeError as e:
            print('[编译失败，该次运行按最弱处理] %s\n%s' % (name, e))
    print('bot 数量：%d' % len(bots))

    done = set()
    if os.path.exists(args.out):
        with open(args.out, encoding='utf-8') as f:
            for line in f:
                r = json.loads(line)
                if 'key' in r:
                    done.add(tuple(tuple(x) if isinstance(x, list) else x for x in r['key']))
                else:  # 旧格式（只有单挑）
                    a, b = r['names'][::-1] if r['swap'] else r['names']
                    done.add(('duel', a, b, r['swap'], r['seed']))

    ids = sorted(bots)
    duel_jobs, melee_jobs = [], []
    if args.only != 'melee':
        for i, a in enumerate(ids):
            for b in ids[i + 1:]:
                if model_of(a) == model_of(b):
                    continue
                for m in range(args.maps):
                    seed = args.seed_base + m
                    for swap in (False, True):
                        # 注意：key 的顺序与旧格式兼容 ('duel', a, b, swap, seed)
                        key = ('duel', a, b, swap, seed)
                        if key in done:
                            continue
                        replay = None
                        if args.replays and m < args.replay_maps:
                            replay = os.path.join(args.replays, 'duel__%s__vs__%s__%d%s.json' % (
                                a.replace('#', '-'), b.replace('#', '-'), seed, '_swap' if swap else ''))
                        seats = [b, a] if swap else [a, b]
                        duel_jobs.append((key, seats, [bots[x] for x in seats], seed, replay))
        duel_jobs.sort(key=lambda j: j[3])  # 按地图轮次推进：中途停下时各对阵局数均衡
    if args.only != 'duel' and args.melee_rounds > 0:
        for rnd in range(args.melee_rounds):
            seed = args.seed_base + MELEE_SEED_OFFSET + rnd
            for t, table in enumerate(melee_tables(ids, rnd, args.seed_base)):
                for rot in range(4):
                    key = ('melee', tuple(table), rot, seed)
                    if key in done:
                        continue
                    seats = [table[(i + rot) % 4] for i in range(4)]
                    replay = None
                    if args.replays and rnd < args.replay_maps:
                        replay = os.path.join(args.replays, 'melee__r%02d_t%d_rot%d__%s.json' % (
                            rnd, t, rot, '__'.join(x.replace('#', '-') for x in seats)))
                    melee_jobs.append((key, seats, [bots[x] for x in seats], seed, replay))
    print('待进行：单挑 %d 局，混战 %d 局（已完成：%d）' % (len(duel_jobs), len(melee_jobs), len(done)))

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, 'a', encoding='utf-8') as out:
        for label, jobs, n_jobs in (('单挑', duel_jobs, args.jobs), ('混战', melee_jobs, max(1, args.jobs // 2))):
            if not jobs:
                continue
            t0 = time.time()
            with ProcessPoolExecutor(n_jobs) as pool:
                futures = [pool.submit(play, j) for j in jobs]
                for n, fut in enumerate(as_completed(futures), 1):
                    r = fut.result()
                    out.write(json.dumps(r, ensure_ascii=False) + '\n')
                    out.flush()
                    if n % 20 == 0 or n == len(jobs):
                        el = time.time() - t0
                        print('%s %d/%d 局，已用 %.0f 秒，预计还需 %.0f 秒' % (
                            label, n, len(jobs), el, el / n * (len(jobs) - n)), flush=True)


if __name__ == '__main__':
    main()
