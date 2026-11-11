"""按公开赛程重打一局或一场。新增的本地入口，不是赛时总控脚本。"""
import argparse
import gzip
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'workspace'))
import match


def read(phase):
    with gzip.open(ROOT / 'results' / phase / 'results.json.gz', 'rt', encoding='utf-8') as f:
        return json.load(f)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('phase', choices=['halftime', 'final', 'individual'])
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument('--game', help='原始局号；见 results.json.gz 或 replays/selection.json')
    group.add_argument('--all', action='store_true', help='重打本场全部对局')
    ap.add_argument('--out', type=Path, required=True, help='新的输出目录，不能已经存在')
    ap.add_argument('--sandbox', choices=['none', 'appcontainer'], default='none',
                    help='默认 none；appcontainer = 赛时用的 Windows 身份隔离（复现成绩不需要它）')
    args = ap.parse_args()
    data = read(args.phase)
    selected = [g for g in data['games'] if args.all or g['id'] == args.game]
    if not selected:
        ap.error('局号不存在')
    if args.out.exists():
        ap.error('输出目录已经存在，请换一个目录')
    entries, indices = [], {}
    for seat in sorted({s for g in selected for s in g['seats']}):
        if seat.startswith('ref-'):
            spec = seat.split('/')[0][4:]
        else:
            spec = str(ROOT / data['programs'][seat]['src'])
        _, exe = match.resolve(spec)
        if not exe.is_file():
            ap.error('缺少参考程序，请先运行 python build_reference.py')
        indices[seat] = len(entries)
        entries.append((seat.replace('/', '_'), exe))
    match.arena_exe()
    args.out.mkdir(parents=True)
    replays = args.out / 'replays'
    replays.mkdir()
    # 单局顺序运行；任何启动失败都停止，不将故障当作平局。
    with (args.out / 'results.jsonl').open('w', encoding='utf-8') as f:
        for n, original in enumerate(selected):
            game = (n, original['seed'], original['first_kick'], [indices[s] for s in original['seats']])
            result = match.run_batch(entries, [game], 1, ['--step-ms', '10', '--sandbox', args.sandbox], replay_dir=replays)[0]
            if any(p['status'] == 'start_fail' for p in result['players']):
                raise RuntimeError('主办方启动故障：停止，故障局不计入结果；回放保留供排查。')
            record = dict(result, original_id=original['id'], seed=original['seed'],
                          seats=original['seats'], original_score=original['score'])
            f.write(json.dumps(record, ensure_ascii=False) + '\n')
            f.flush()
            print(original['id'], result['score'], '原赛', original['score'])


if __name__ == '__main__':
    main()
