"""第 11 集海选：重新生成赛程、用 bots/ 里的十三份程序重打对局，再出榜。

    python tournament/rerun.py --schedule-only     只重新生成赛程，和 results/schedule.json.gz 逐局核对（几秒）
    python tournament/rerun.py --limit 600         按赛程均匀抽 600 局重打，报告和比赛时逐局结果的一致率（几分钟）
    python tournament/rerun.py [--workers 16]      重打全部 32,904 局并出榜（16 线程约 15～20 分钟），结果写到 rerun/

赛程的生成和比赛时 formal.py 逐行相同（一对一：每对 × 6 张图 × 19 个种子 × 两边各坐一次 0 号位；
四人局：420 轮，13 人每轮轮空 1 位、其余 12 人分 3 组，每组同一张图同一个种子打 12 局）。
对局用选手包里的 match.py 和 arena.exe（与比赛时同一个裁判），每步 10 毫秒 CPU，不进沙箱，只做进程管控。

注意：规则引擎没有随机数，但有的程序会看时钟决定算多深，同一局重打时不一定逐步相同（纯享版重打的 117 局里
有 45 局结果和比赛时相同）。所以重打全部对局得到的是一份新的抽样，实力值会有几分的出入；
正式成绩以 results/ 里比赛时的逐局结果为准，用 tournament/score.py 核对。
"""
import argparse
import gzip
import itertools
import json
import random
import sys
import time
import types
from pathlib import Path

HERE = Path(__file__).resolve().parent
EP = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(EP / 'workspace'))
import match  # noqa: E402
import rating  # noqa: E402
from score import NAMES, DRAW_SEED, SHOW, observations  # noqa: E402

PROG = {'astra': 'gpt-6.2-astra', 'sol': 'gpt-6.2-sol', 'fable': 'claude-fable-5.2', 'opus': 'claude-opus-5.5',
        'carbon': 'gemini-4-carbon', 'hydrogen': 'gemini-4-hydrogen', 'grok': 'grok-4.7', 'glm': 'glm-5.3',
        'deepseek': 'deepseek-v4.1-flash', 'kimi': 'kimi-k3.1-pro', 'qwen': 'qwen4-max', 'mimo': 'mimo-v2.6-pro',
        'doubao': 'doubao-seed-evolving'}
DUEL_SEEDS = list(range(11001, 11020))
FFA_ROUNDS, FFA_SEED_BASE = 420, 2401
MAPS = ['workspace/maps/twinlakes.txt', 'workspace/maps/yinyang.txt'] + \
       [f'tournament/hidden/{m}.txt' for m in ('icefloes', 'ruins', 'skybridge', 'stonecircle')]


def schedule(names=NAMES):
    """照比赛时 formal.py 生成赛程（mapfile 写成仓库里的相对路径）。"""
    label = {m: Path(m).stem for m in MAPS}
    games = []

    def add(mode, ns, mp, seed, block, **kw):
        games.append(dict(id=len(games), mode=mode, names=list(ns), map=label[mp], mapfile=mp, seed=seed, block=block, **kw))
    for a, b in itertools.combinations(names, 2):
        for mp in MAPS:
            for seed in DUEL_SEEDS:
                for ns in ((a, b), (b, a)):
                    add(0, ns, mp, seed, f'{a}:{b}:{label[mp]}:{seed}')
    rng = random.Random(DRAW_SEED)
    bye_order = names.copy()
    rng.shuffle(bye_order)
    pair_counts = {tuple(sorted(p)): 0 for p in itertools.combinations(names, 2)}
    for rd in range(FFA_ROUNDS):
        bye = bye_order[rd % len(bye_order)]
        pool = [n for n in names if n != bye]
        cands = []
        for _ in range(400):
            ns = pool.copy()
            rng.shuffle(ns)
            cost = sum(pair_counts[tuple(sorted(p))] for i in range(0, 12, 4) for p in itertools.combinations(ns[i:i + 4], 2))
            cands.append((cost, ns))
        ns = min(cands, key=lambda x: x[0])[1]
        while True:
            best, delta = None, 0
            for i in range(12):
                for j in range(i + 1, 12):
                    if i // 4 == j // 4:
                        continue
                    aa = ns[i // 4 * 4:i // 4 * 4 + 4]
                    bb = ns[j // 4 * 4:j // 4 * 4 + 4]
                    d = sum(pair_counts[tuple(sorted((ns[j], x)))] - pair_counts[tuple(sorted((ns[i], x)))] for x in aa if x != ns[i]) + \
                        sum(pair_counts[tuple(sorted((ns[i], x)))] - pair_counts[tuple(sorted((ns[j], x)))] for x in bb if x != ns[j])
                    if d < delta:
                        delta, best = d, (i, j)
            if best is None:
                break
            i, j = best
            ns[i], ns[j] = ns[j], ns[i]
        for i in range(0, 12, 4):
            for p in itertools.combinations(ns[i:i + 4], 2):
                pair_counts[tuple(sorted(p))] += 1
        mp = MAPS[rd % len(MAPS)]
        seed = FFA_SEED_BASE + rd
        for i in range(0, 12, 4):
            g4 = ns[i:i + 4]
            for diag, order in enumerate((g4, [g4[0], g4[1], g4[3], g4[2]], [g4[0], g4[2], g4[1], g4[3]])):
                for rot in range(4):
                    add(1, order[rot:] + order[:rot], mp, seed, f'{rd}:{i}:{diag}', round=rd, bye=bye)
    return dict(draw_seed=DRAW_SEED, names=names, maps=[label[m] for m in MAPS], duel_seeds=DUEL_SEEDS, ffa_rounds=FFA_ROUNDS,
                bye_order=bye_order, pair_counts={f'{a}|{b}': v for (a, b), v in pair_counts.items()}, games=games)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--schedule-only', action='store_true')
    ap.add_argument('--limit', type=int, default=0, help='只重打均匀抽出的这么多局')
    ap.add_argument('--workers', type=int, default=16)
    ap.add_argument('--out', default=str(EP / 'rerun'))
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding='utf-8')
    sch = schedule()
    with gzip.open(EP / 'results' / 'schedule.json.gz', 'rt', encoding='utf-8') as f:
        ref = json.load(f)
    same = sch == ref
    print(f'赛程 {len(sch["games"])} 局（一对一 {sum(g["mode"] == 0 for g in sch["games"])}、四人局 '
          f'{sum(g["mode"] == 1 for g in sch["games"])}），和 results/schedule.json.gz ' + ('逐局相同' if same else '不一致！'))
    if not same:
        sys.exit(1)
    if a.schedule_only:
        return
    games = sch['games']
    if a.limit:
        step = max(1, len(games) // a.limit)
        games = games[::step][:a.limit]
    progs = [match.resolve_prog(str(EP / 'bots' / PROG[n] / 'player.cpp')) for n in NAMES]
    entries, seats = match.entries_for(progs)
    idx = {n: seats[i] for i, n in enumerate(NAMES)}
    extra = match.common_args(types.SimpleNamespace(step_ms=10, sandbox='none', cfg=None))
    t0 = time.time()
    results = {}
    for s in range(0, len(games), 1200):
        chunk = games[s:s + 1200]
        batch = [(g['id'], g['seed'], [idx[n] for n in g['names']], g['mode'], str(EP / g['mapfile'])) for g in chunk]
        for r in match.run_batch(entries, batch, a.workers, extra):
            results[r['id']] = r
        print(f'  已打 {len(results)} / {len(games)} 局，{time.time() - t0:.0f} 秒', flush=True)
    rows = []
    for g in games:
        r = results[g['id']]
        pl = sorted(r['players'], key=lambda p: p['seat'])
        rows.append(dict(id=g['id'], mode=g['mode'], map=g['map'], seed=g['seed'], names=g['names'], steps=r['steps'],
                         players=[dict(name=g['names'][p['seat']], rank=p['rank'], kills=p['kills'], alive=p['alive'], cause=p['cause_name'],
                                       faults=p['faults_total'], status=p['status'], max_ms=p['max_ms']) for p in pl]))
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    with gzip.open(out / 'games.jsonl.gz', 'wt', encoding='utf-8') as f:
        for r in rows:
            f.write(json.dumps(r, ensure_ascii=False) + '\n')
    with gzip.open(EP / 'results' / 'games.jsonl.gz', 'rt', encoding='utf-8') as f:
        official = {r['id']: r for r in map(json.loads, f)}
    agree = sum(1 for r in rows if [p['rank'] for p in r['players']] == [p['rank'] for p in official[r['id']]['players']])
    print(f'重打 {len(rows)} 局，名次和比赛时完全相同的 {agree} 局（{agree / len(rows):.0%}）；逐局结果写到 {out / "games.jsonl.gz"}')
    if a.limit:
        return
    lot = NAMES.copy()
    random.Random(DRAW_SEED + ':ranking').shuffle(lot)
    res = rating.individual(observations(rows), NAMES, lot, DRAW_SEED, boot=2000)
    json.dump(res, open(out / '海选成绩.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    off = json.load(open(EP / 'results' / '海选成绩.json', encoding='utf-8'))['players']
    print('名次  选手                  重打实力值  比赛时')
    for n in res['ranking']:
        print(f"{res['players'][n]['rank']:>3}   {SHOW[n]:<20} {res['players'][n]['rating']:+8.1f}  {off[n]['rating']:+8.1f}")


if __name__ == '__main__':
    main()
