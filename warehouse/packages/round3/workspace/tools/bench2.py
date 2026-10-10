"""第二阶段本地测试：编译 src/，跑 workloads2/ 下的用例，逐字节比对，报告 CPU 时间。

  python tools/bench2.py                # 查对错：小型公开负载 + 边界用例 + 本队的回归用例（几秒）
  python tools/bench2.py --big          # 看快慢：3 组接近正式规模的大负载，显示相对参考实现的加速比
  python tools/bench2.py --only p2_small
  python tools/bench2.py --skip-edge    # 只跑 4 组小型公开负载

加速比 = 参考实现的 CPU 时间 / 你的 CPU 时间（参考实现是一份干净但没有优化的写法，时间见 workloads2/big/reference_cpu.json）。
正式评测用同样的算法，只是换成隐藏负载；超过参考实现用时 3 倍 + 10 秒判超时。
为了不浪费比赛时间，--big 在某一组比参考实现还慢时会提前中止这一组。可以配合 --only 只跑一组。
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import measure  # noqa: E402
from bench import first_diff  # noqa: E402
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')  # Windows 控制台默认 GBK，统一输出 UTF-8

DIRS = [os.path.join(ROOT, 'workloads2', d) for d in ('public', 'edge', 'regression')]
BIG_DIR = os.path.join(ROOT, 'workloads2', 'big')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--only', help='只测某个用例 / only this case')
    ap.add_argument('--skip-edge', action='store_true', help='不跑边界用例 / skip edge cases')
    ap.add_argument('--big', action='store_true', help='只跑大负载，显示相对参考实现的加速比 / big workloads with speedup')
    args = ap.parse_args()

    exe = measure.build(os.path.join(ROOT, 'src'), os.path.join(ROOT, 'build', 'sim2' + measure.EXE))
    ref = {}
    if os.path.exists(os.path.join(BIG_DIR, 'reference_cpu.json')):
        with open(os.path.join(BIG_DIR, 'reference_cpu.json'), encoding='utf-8') as f:
            ref = json.load(f)
    cases = []
    for d in [BIG_DIR] if args.big else DIRS[:1] if args.skip_edge else DIRS:
        if os.path.isdir(d):
            cases += [(d, f[:-3]) for f in sorted(os.listdir(d)) if f.endswith('.in')]
    if args.big:
        cases.sort(key=lambda c: ref.get(c[1], 0))   # 参考用时短的先跑，先看到结果
    if args.only:
        cases = [(d, f[:-3]) for d in DIRS + [BIG_DIR] if os.path.isdir(d)
                 for f in sorted(os.listdir(d)) if f[:-3] == args.only and f.endswith('.in')]

    bad, total, slow_failed, skipped = [], 0.0, False, 0
    for d, n in cases:
        if n.startswith('slow_') and slow_failed and not args.only:
            skipped += 1      # 性能型回归用例：第一个还没过，其余的这次不跑（每个要等 60 秒）
            bad.append(n)
            continue
        inp = os.path.join(d, n + '.in')
        with open(os.path.join(d, n + '.out'), 'rb') as f:
            expected = f.read()
        limit = ref[n] + 5 if n in ref else None     # 大负载：比参考实现还慢就提前中止，不浪费比赛时间
        if limit is None and os.path.basename(d) == 'regression':
            limit = 60                                # 回归用例：和上一轮的判定一样，60 秒内没结束就算没修好
        r = measure.run(exe, inp, timeout=limit)
        ok = r['code'] == 0 and not r['timeout'] and r['digest'] == measure.digest(expected)
        total += r['cpu']
        if n in ref:
            if r['timeout']:
                note = '已中止：比参考实现（%.1f 秒）还慢，速度分接近 0（正式评测超过 3 倍 + 10 秒判超时）' % ref[n]
            elif ok:
                note = '参考实现 %.1f 秒，加速比 %.1f 倍' % (ref[n], ref[n] / max(r['cpu'], 0.01))
            else:
                note = '输出有错，这一组速度分为 0'
            print('%-14s %-6s %8.3f 秒   %s' % (n, '正确' if ok else '错误', r['cpu'], note), flush=True)
        elif n.startswith('p2_') or not ok:
            print('%-14s %-6s %8.3f 秒' % (n, '正确' if ok else '错误', r['cpu']))
        if not ok:
            bad.append(n)
            if r['timeout']:
                slow_failed = slow_failed or n.startswith('slow_')
                if n not in ref:
                    print('    60 秒内没有结束（回归用例里有性能型反例：输出没错，但慢到超时也算没修好）')
                continue
            if r['code'] != 0:
                print('    退出码 / exit code %d' % r['code'])
            diff = first_diff(expected, r['output'])
            if diff:
                print('    第 %d 行不同 / first difference at line %d' % (diff[0], diff[0]))
                print('    期望 expected: %s' % diff[1][:200])
                print('    实际 actual:   %s' % diff[2][:200])
    if skipped:
        print('另有 %d 个性能型回归用例（slow_ 开头）这次没跑：第一个还没在 60 秒内跑完。修好后会自动全跑，也可以用 --only 单独跑。' % skipped)
    print('共 %d 个用例，错误 %d 个；合计 CPU 秒 %.3f' % (len(cases), len(bad), total))
    print('全部正确 / ALL CORRECT' if not bad else '存在错误 / SOME OUTPUTS ARE WRONG: ' + ' '.join(bad))
    if not args.big and not args.only and os.path.isdir(BIG_DIR):
        print('（以上用例规模很小，只能查对错。看快慢请运行：python tools/bench2.py --big）')
    sys.exit(0 if not bad else 1)


if __name__ == '__main__':
    main()
