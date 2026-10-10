"""正式评测：编译每份提交，在隐藏负载上运行，检查输出、记录 CPU 时间。

  python 裁判/评测.py --phase 1 --entries 提交/第一阶段 --out 结果/results1.jsonl
  python 裁判/评测.py --phase 2 --entries 提交/第二阶段 --out 结果/results2.jsonl

第一阶段：比基线（屎山）还慢的提前终止，记 0 分。
第二阶段：基准是出题方的参考实现；比它慢也不扣"正确分"，超过基准 3 倍 + 10 秒才判超时。

每份提交是一个文件夹，名字为 <模型>@<工具>#<序号>，里面是模型修改后的 src/ 文件夹
（也接受直接把 .cpp/.h 放在提交文件夹里）。
结果逐条追加到 JSONL；中断后只有版本摘要相同才跳过已完成的部分，历史结果无摘要时请使用新的 --out。
评测时请不要在这台电脑上运行其他耗 CPU 的程序，并且不要并行评测。
"""
import argparse
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'workspace', 'tools'))
import measure  # noqa: E402

MEM_LIMIT_MB = 1024


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--phase', type=int, choices=[1, 2], required=True)
    ap.add_argument('--entries', required=True, help='提交文件夹')
    ap.add_argument('--build', default=os.path.join(HERE, 'build'),
                    help='编译产物目录（本机 ASR 会拦截别处新编译的程序）')
    ap.add_argument('--out', required=True, help='结果 JSONL')
    ap.add_argument('--repeat', type=int, default=3, help='每组负载最多运行几次（取中位数）；单次超过 10 秒的只跑 1 次')
    ap.add_argument('--min-total', type=float, default=0.0,
                    help='精确计时：单次 CPU 用时很短时，连跑多遍直到累计 CPU 达到这么多秒（最多 80 遍），取平均。'
                         'Windows 进程 CPU 计时的刻度约 0.016 秒，单次只有 0.03～0.05 秒的程序测一次误差能到 1.5 倍')
    args = ap.parse_args()

    hidden = os.path.join(HERE, '隐藏负载' if args.phase == 1 else '隐藏负载2')
    with open(os.path.join(hidden, 'manifest.json'), encoding='utf-8') as f:
        manifest = json.load(f)
    # 开源修复：仅允许在源码、负载、工具和参数相同的情况下续评。
    def sha(path):
        with open(path, 'rb') as f:
            return hashlib.sha256(f.read()).hexdigest()

    config = {'phase': args.phase, 'repeat': args.repeat, 'min_total': args.min_total,
              'compiler': os.environ.get('CXX', 'g++'), 'flags': measure.FLAGS,
              'platform': sys.platform, 'evaluator': sha(__file__), 'measure': sha(measure.__file__),
              'manifest': manifest}
    inputs = {w: sha(os.path.join(hidden, w + '.in')) for w in manifest}
    fingerprints = {}
    for entry in sorted(os.listdir(args.entries)):
        folder = os.path.join(args.entries, entry)
        if not os.path.isdir(folder):
            continue
        src = os.path.join(folder, 'src') if os.path.isdir(os.path.join(folder, 'src')) else folder
        sources = {}
        for d, dirs, files in os.walk(src):
            dirs[:] = sorted(x for x in dirs if x not in ('build', '__pycache__'))
            for name in sorted(files):
                path = os.path.join(d, name)
                sources[os.path.relpath(path, src).replace(os.sep, '/')] = sha(path)
        for w in manifest:
            data = json.dumps([config, sources, w, inputs[w]], sort_keys=True, ensure_ascii=False).encode('utf-8')
            fingerprints[entry, w] = hashlib.sha256(data).hexdigest()
    done = set()
    if os.path.exists(args.out):
        with open(args.out, encoding='utf-8') as f:
            for line in f:
                r = json.loads(line)
                key = (r['entry'], r['workload'])
                if not r.get('fingerprint') or r['fingerprint'] != fingerprints.get(key):
                    sys.exit('结果缺少版本摘要或与当前输入不符，请使用新的 --out 路径：%s' % args.out)
                done.add(key)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    build_dir = os.path.join(args.build or os.path.dirname(os.path.abspath(args.out)), 'phase%d' % args.phase)

    for entry in sorted(os.listdir(args.entries)):
        folder = os.path.join(args.entries, entry)
        if not os.path.isdir(folder):
            continue
        todo = [w for w in manifest if (entry, w) not in done]
        if not todo:
            continue
        src = os.path.join(folder, 'src') if os.path.isdir(os.path.join(folder, 'src')) else folder
        exe = os.path.join(build_dir, entry.replace('#', '-') + measure.EXE)
        try:
            measure.build(src, exe)
            build_error = None
        except RuntimeError as e:
            build_error = str(e)
            print('[%s] 编译失败，全部负载记 0 分\n%s' % (entry, e))
        with open(args.out, 'a', encoding='utf-8') as out:
            for w in todo:
                m = manifest[w]
                rec = {'phase': args.phase, 'entry': entry, 'workload': w, 'baseline_cpu': m['baseline_cpu'],
                       'fingerprint': fingerprints[entry, w], 'runs': []}
                if build_error:
                    rec['status'] = 'compile_error'
                else:
                    if args.phase == 1:
                        limit = m['baseline_cpu'] * 1.2 + 5  # 比基线还慢就没有分，不必等它跑完
                    else:
                        limit = m['baseline_cpu'] * 3 + 10   # 第二阶段：慢一点也有正确分
                    status = 'ok'
                    for i in range(args.repeat):
                        r = measure.run(exe, os.path.join(hidden, w + '.in'), timeout=limit)
                        rec['runs'].append({'cpu': round(r['cpu'], 4), 'wall': round(r['wall'], 4),
                                            'mem_mb': round(r['mem_mb'], 1)})
                        if r['timeout']:
                            status = 'timeout'
                        elif r['code'] != 0:
                            status = 'crash'
                        elif r['digest'] != m['digest']:
                            status = 'wrong_output'
                        elif r['mem_mb'] > MEM_LIMIT_MB:
                            status = 'memory'
                        if status != 'ok' or r['cpu'] > 10:
                            break
                    rec['status'] = status
                    cpus = sorted(x['cpu'] for x in rec['runs'])
                    rec['cpu'] = cpus[len(cpus) // 2]
                    if status == 'ok' and args.min_total and rec['cpu'] * len(cpus) < args.min_total:
                        # 精确计时：补跑到累计 CPU 够长，取全部运行的平均（输出仍然每次都核对）
                        total, n = sum(cpus), len(cpus)
                        rec['peak_mem_mb'] = max(x['mem_mb'] for x in rec['runs'])
                        while total < args.min_total and n < 80:
                            r = measure.run(exe, os.path.join(hidden, w + '.in'), timeout=limit)
                            rec['peak_mem_mb'] = max(rec['peak_mem_mb'], r['mem_mb'])
                            if r['mem_mb'] > MEM_LIMIT_MB:
                                rec['status'] = 'memory'
                                break
                            if r['timeout'] or r['code'] != 0 or r['digest'] != m['digest']:
                                rec['status'] = 'unstable'      # 同一个程序这次输出不对：不确定行为
                                break
                            total, n = total + r['cpu'], n + 1
                        rec['cpu'] = total / n
                        rec['precise'] = {'runs': n, 'total_cpu': round(total, 4)}
                        rec['runs'] = rec['runs'][:3]
                out.write(json.dumps(rec, ensure_ascii=False) + '\n')
                out.flush()
                sp = '' if rec['status'] != 'ok' else '加速 %.1f 倍' % (m['baseline_cpu'] / max(rec['cpu'], 0.01))
                print('[%s] %-15s %-12s %s' % (entry, w, rec['status'], sp), flush=True)


if __name__ == '__main__':
    main()
