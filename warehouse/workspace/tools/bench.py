"""本地测试工具：编译 src/，运行公开负载，逐字节比对输出，报告 CPU 时间。

  python tools/bench.py                 # 编译并测试全部公开负载
  python tools/bench.py --baseline      # 同时报告加速比。基线耗时第一次实测（约 40 秒），之后用缓存
  python tools/bench.py --baseline --fresh   # 忽略缓存，重新测量基线耗时
  python tools/bench.py --only pub_small --repeat 3

Build and run all public workloads, compare output byte by byte, report CPU time.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import measure  # noqa: E402
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')  # Windows 控制台默认 GBK，统一输出 UTF-8

WORKLOADS = os.path.join(ROOT, 'workloads', 'public')


CACHE = os.path.join(ROOT, 'build', 'baseline_times.json')


def baseline_stamp():
    d = os.path.join(ROOT, 'baseline')
    return ';'.join('%s:%d:%d' % (f, os.path.getsize(os.path.join(d, f)), int(os.path.getmtime(os.path.join(d, f))))
                    for f in sorted(os.listdir(d)))


def load_cache():
    """基线不会变，测过一次的耗时就记下来；基线代码或负载文件有变化时自动重测。"""
    try:
        with open(CACHE, encoding='utf-8') as f:
            data = json.load(f)
        return data['times'] if data.get('stamp') == baseline_stamp() else {}
    except (OSError, ValueError, KeyError):
        return {}


def save_cache(times):
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    with open(CACHE, 'w', encoding='utf-8') as f:
        json.dump({'stamp': baseline_stamp(), 'times': times}, f, indent=1)


def first_diff(a, b):
    la, lb = measure.normalized(a).split(b'\n'), measure.normalized(b).split(b'\n')
    for i in range(max(len(la), len(lb))):
        x = la[i] if i < len(la) else b'<EOF>'
        y = lb[i] if i < len(lb) else b'<EOF>'
        if x != y:
            return i + 1, x.decode(errors='replace'), y.decode(errors='replace')
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--baseline', action='store_true', help='同时测量基线耗时并计算加速比 / also time baseline/')
    ap.add_argument('--only', help='只测某个负载 / only this workload')
    ap.add_argument('--repeat', type=int, default=1, help='每个负载运行次数，取中位数 / runs per workload (median)')
    ap.add_argument('--fresh', action='store_true', help='忽略缓存，重新测量基线耗时 / re-time baseline')
    args = ap.parse_args()

    names = sorted(f[:-3] for f in os.listdir(WORKLOADS) if f.endswith('.in'))
    if args.only:
        names = [n for n in names if n == args.only]
    exe = measure.build(os.path.join(ROOT, 'src'), os.path.join(ROOT, 'build', 'sim' + measure.EXE))
    base = None
    if args.baseline:  # 基线代码不会变，编译过就不再重新编译
        bsrc, base = os.path.join(ROOT, 'baseline'), os.path.join(ROOT, 'build', 'baseline' + measure.EXE)
        newest = max(os.path.getmtime(os.path.join(bsrc, f)) for f in os.listdir(bsrc))
        if not os.path.exists(base) or os.path.getmtime(base) < newest:
            measure.build(bsrc, base)
    cache = load_cache() if base else {}

    all_ok = True
    total, total_base = 0.0, 0.0
    print('%-14s %-6s %10s %10s %9s %s' % ('负载', '结果', 'CPU秒', '基线CPU秒', '加速比', '内存MB'))
    for n in names:
        inp = os.path.join(WORKLOADS, n + '.in')
        with open(os.path.join(WORKLOADS, n + '.out'), 'rb') as f:
            expected = f.read()
        runs = [measure.run(exe, inp) for _ in range(args.repeat)]
        runs.sort(key=lambda r: r['cpu'])
        r = runs[len(runs) // 2]
        ok = all(x['digest'] == measure.digest(expected) and x['code'] == 0 for x in runs)
        all_ok &= ok
        total += r['cpu']
        bcpu = ''
        speed = ''
        if base:
            key = '%s|%d|%d' % (n, os.path.getsize(inp), int(os.path.getmtime(inp)))
            if args.fresh or key not in cache:
                cache[key] = measure.run(base, inp)['cpu']
                save_cache(cache)
            total_base += cache[key]
            bcpu = '%.3f' % cache[key]
            speed = '%.1fx' % (cache[key] / max(r['cpu'], 1e-3))
        print('%-14s %-6s %10.3f %10s %9s %.0f' % (n, '正确' if ok else '错误', r['cpu'], bcpu, speed, r['mem_mb']))
        if not ok:
            bad = next(x for x in runs if x['digest'] != measure.digest(expected) or x['code'] != 0)
            if bad['code'] != 0:
                print('    退出码 / exit code %d' % bad['code'])
            d = first_diff(expected, bad['output'])
            if d:
                print('    第 %d 行不同 / first difference at line %d' % (d[0], d[0]))
                print('    期望 expected: %s' % d[1][:200])
                print('    实际 actual:   %s' % d[2][:200])
    print('合计 CPU 秒 / total CPU s: %.3f' % total + ('   基线 / baseline: %.3f' % total_base if base else ''))
    print('全部正确 / ALL CORRECT' if all_ok else '存在错误 / SOME OUTPUTS ARE WRONG')
    sys.exit(0 if all_ok else 1)


if __name__ == '__main__':
    main()
