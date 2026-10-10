"""生成隐藏负载，并用标准程序测出标准答案摘要和基准 CPU 时间。每期每个阶段只需运行一次。

  python 裁判/准备隐藏负载.py --phase 1      # 第一阶段：hid_* 负载，标准程序 = workspace/baseline（屎山基线）
  python 裁判/准备隐藏负载.py --phase 2      # 第二阶段：h2_* 负载，标准程序 = 裁判/参考实现/v2_变更
  python 裁判/准备隐藏负载.py --phase 1 --repeat 1   # 省时间：只跑 1 次

结果写入 裁判/隐藏负载/ 或 裁判/隐藏负载2/（输入文件 + manifest.json）。
注意：本机 Windows 的 ASR 规则会拦截新编译的程序，所以编译产物放在 --build（默认 裁判/build/，需要"测试"文件夹已加 ASR 排除项）。
"""
import argparse
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'workspace', 'tools'))
sys.path.insert(0, HERE)
import measure  # noqa: E402
import gen  # noqa: E402
import gen2  # noqa: E402

DEFAULT_BUILD = os.path.join(HERE, 'build')
PHASES = {
    1: dict(src=os.path.join(ROOT, 'workspace', 'baseline'), out='隐藏负载', gen=gen, prefix='hid_'),
    2: dict(src=os.path.join(HERE, '参考实现', 'v2_变更'), out='隐藏负载2', gen=gen2, prefix='h2_'),
}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--phase', type=int, choices=[1, 2], required=True)
    ap.add_argument('--seed', type=int, default=20261006, help='本期种子')
    ap.add_argument('--repeat', type=int, default=3, help='标准程序运行次数（取中位数）')
    ap.add_argument('--only', help='只处理某一个负载')
    ap.add_argument('--build', default=DEFAULT_BUILD, help='编译产物目录')
    args = ap.parse_args()
    ph = PHASES[args.phase]

    out_dir = os.path.join(HERE, ph['out'])
    os.makedirs(out_dir, exist_ok=True)
    manifest_path = os.path.join(out_dir, 'manifest.json')
    manifest = {}
    if os.path.exists(manifest_path):
        with open(manifest_path, encoding='utf-8') as f:
            manifest = json.load(f)

    base = measure.build(ph['src'], os.path.join(args.build, 'standard%d' % args.phase + measure.EXE))
    names = [n for n in ph['gen'].PROFILES if n.startswith(ph['prefix'])]
    if args.only:
        names = [args.only]
    for n in names:
        path = os.path.join(out_dir, n + '.in')
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(ph['gen'].generate(n, args.seed))
        runs = []
        for i in range(args.repeat):
            t0 = time.time()
            r = measure.run(base, path)
            if r['code'] != 0:
                raise RuntimeError('%s：标准程序退出码 %d' % (n, r['code']))
            if runs and r['digest'] != runs[0]['digest']:
                raise RuntimeError('%s：标准程序两次输出不同，程序不确定！' % n)
            runs.append(r)
            print('%s 第 %d 次：CPU %.2f 秒（墙钟 %.2f 秒）' % (n, i + 1, r['cpu'], time.time() - t0), flush=True)
        cpus = sorted(r['cpu'] for r in runs)
        manifest[n] = {'seed': args.seed, 'digest': runs[0]['digest'], 'baseline_cpu': cpus[len(cpus) // 2],
                       'baseline_mem_mb': round(max(r['mem_mb'] for r in runs), 1),
                       'output_lines': runs[0]['output'].count(b'\n')}
        with open(manifest_path, 'w', encoding='utf-8') as f:
            json.dump(manifest, f, ensure_ascii=False, indent=2)
    print('完成，已写入', manifest_path)


if __name__ == '__main__':
    main()
