# -*- coding: utf-8 -*-
"""第 12 集纯享版选局：分层随机抽样，不看事件、不看精彩程度、不按名场面加分。

抽样框：六回合正式赛程全部 10,600 局（默认读取 ../results/formal_games.jsonl.gz）。
分层：每回合 PER_ROUND 局（默认 12），其中常规边（偶数局号，Astra 队在左）、换边（奇数局号，Fable 队在左）各一半；层内简单随机抽。
唯一的调整（照第 11 集纯享版 v2 的做法）：用固定种子序列 1, 2, 3… 依次抽，取第一个"每回合抽出来的 Astra 队得分
  和该回合真实胜率 × PER_ROUND 相差不超过 0.5 局"的种子（平局算半局，和正式成绩口径一致）。
顺序：按回合 1→6；回合内随机打乱（固定种子）。

用法：
  python select_pure.py                      # 默认每回合 12 局，输出到 stdout 并核对 pure_pick.json / pure_stats.json
  python select_pure.py 12 --write           # 重新写出 pure_pick.json 与 pure_stats.json
  python select_pure.py --games path/to/formal_games.jsonl.gz
"""
from __future__ import annotations

import argparse
import gzip
import json
import random
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_GAMES = HERE.parent / 'results' / 'formal_games.jsonl.gz'
TOL = 0.5


def load_rows(games_path: Path) -> list[dict]:
    rows = []
    opener = gzip.open if games_path.suffix == '.gz' else open
    with opener(games_path, 'rt', encoding='utf-8') as f:
        for line in f:
            if not line.strip():
                continue
            j = json.loads(line)
            t0 = [p['name'] for p in j['players'] if p['team'] == 0]
            blue = 0 if 'astra' in t0 else 1  # Astra 队对应的 team 号
            w = j['winner']
            bs = 0.5 if w not in (0, 1) else (1.0 if w == blue else 0.0)
            rows.append(dict(
                rnd=j['round'],
                id=j['id'],
                seed=j['seed'],
                map=j['map'],
                mode=j['mode'],
                steps=j['steps'],
                winner=w,
                swapped=(blue == 1),
                blue_score=bs,
            ))
    return rows


def est_frames(o: dict) -> int:
    # 渲染器时间线近似（10 局实测拟合：1.5 帧/步 + 入场和结算约 9 秒）
    return int(1.5 * o['steps'] + 270)


def run_selection(rows: list[dict], per_round: int) -> tuple[list[dict], dict]:
    by: dict[tuple[int, bool], list[dict]] = defaultdict(list)
    for o in rows:
        by[(o['rnd'], o['swapped'])].append(o)
    for k in by:
        by[k].sort(key=lambda o: o['id'])

    true_rate = {
        r: sum(o['blue_score'] for o in rows if o['rnd'] == r)
           / sum(1 for o in rows if o['rnd'] == r)
        for r in range(1, 7)
    }

    def draw(seed: int) -> dict[int, list[dict]]:
        rng = random.Random('ep12-purecut-%d' % seed)
        pick = {}
        for r in range(1, 7):
            h = per_round // 2
            a = rng.sample(by[(r, False)], per_round - h)
            b = rng.sample(by[(r, True)], h)
            pick[r] = a + b
        return pick

    def ok(pick: dict[int, list[dict]]) -> bool:
        return all(
            abs(sum(o['blue_score'] for o in pick[r]) - true_rate[r] * per_round) <= TOL + 1e-9
            for r in range(1, 7)
        )

    for seed in range(1, 100000):
        picked = draw(seed)
        if ok(picked):
            break
    else:
        raise SystemExit('找不到满足条件的随机种子')

    rng = random.Random('ep12-purecut-order-%d' % seed)
    order = []
    for r in range(1, 7):
        layer = [dict(o) for o in picked[r]]
        rng.shuffle(layer)
        order += layer
    for i, o in enumerate(order):
        o['no'] = i + 1
        o['est_frames'] = est_frames(o)

    stats = dict(
        seed=seed,
        seeds_tried=seed,
        per_round=per_round,
        n=len(order),
        est_game_sec=round(sum(o['est_frames'] for o in order) / 30, 1),
        rounds={},
    )
    for r in range(1, 7):
        layer = picked[r]
        stats['rounds'][str(r)] = dict(
            map=layer[0]['map'],
            true_blue=round(true_rate[r] * 100, 2),
            sample_blue=round(100 * sum(o['blue_score'] for o in layer) / len(layer), 2),
            sample_score=sum(o['blue_score'] for o in layer),
            n=len(layer),
            swapped=sum(o['swapped'] for o in layer),
            est_sec=round(sum(est_frames(o) for o in layer) / 30, 1),
        )
    allb = sum(o['blue_score'] for o in order) / len(order)
    stats['sample_blue_all'] = round(100 * allb, 2)
    stats['true_blue_all_round_avg'] = round(100 * sum(true_rate.values()) / 6, 2)
    stats['true_blue_all_games'] = round(100 * sum(o['blue_score'] for o in rows) / len(rows), 2)
    return order, stats


def main() -> None:
    parser = argparse.ArgumentParser(description='第 12 集纯享版分层随机抽局脚本')
    parser.add_argument('per_round', type=int, nargs='?', default=12, help='每回合抽取局数（默认 12）')
    parser.add_argument('--games', type=Path, default=DEFAULT_GAMES, help='正式逐局结果路径（formal_games.jsonl.gz）')
    parser.add_argument('--write', action='store_true', help='将抽样结果写回 pure_pick.json 与 pure_stats.json')
    args = parser.parse_args()

    rows = load_rows(args.games)
    order, stats = run_selection(rows, args.per_round)
    if args.write:
        (HERE / 'pure_pick.json').write_text(json.dumps(order, ensure_ascii=False, indent=1) + '\n', encoding='utf-8')
        (HERE / 'pure_stats.json').write_text(json.dumps(stats, ensure_ascii=False, indent=1) + '\n', encoding='utf-8')
    print(json.dumps(stats, ensure_ascii=False, indent=1))

    ref_pick = HERE / 'pure_pick.json'
    if not args.write and args.per_round == 12 and ref_pick.exists():
        expected = json.loads(ref_pick.read_text(encoding='utf-8'))
        if order == expected:
            print('核对通过：与 pure_pick.json 完全一致（72 局，seed=%d）' % stats['seed'])
        else:
            raise SystemExit('核对失败：抽样结果与 pure_pick.json 不一致')


if __name__ == '__main__':
    main()
