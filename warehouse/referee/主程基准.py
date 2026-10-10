"""增值的基准：把第二阶段每组隐藏负载还原成旧格式（去掉 PARAMS 末尾的 agingEvery carryCost），让每队主程的原版代码各跑一次，记 CPU 时间。

  python 裁判/主程基准.py [--repeat 1]

同一张图、同一批事件：终版（新格式，新需求生效）和主程原版（旧格式）各跑一次，r = 主程原版 CPU / 终版 CPU，就是"接手后快了多少倍"。
主程原版输出对不对这里不管（第 1 轮已经评过），只要时间；超过参考实现用时 3 倍 + 10 秒的按这个上限记（和终版的超时线相同）。
输出：赛事/结果/主程基准.jsonl（团队计分.py 读）。评测时不要在这台电脑上运行其他耗 CPU 的程序。
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'workspace', 'tools'))
from 赛事工具 import BUILD, EVENT, HERE, lead_src, teams  # noqa: E402
import measure  # noqa: E402

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')


def to_old(text):
    lines = text.split('\n')
    for i, s in enumerate(lines):
        if s.startswith('PARAMS '):
            lines[i] = ' '.join(s.split(' ')[:7])
            return '\n'.join(lines)
    raise ValueError('没有 PARAMS 行')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--repeat', type=int, default=1)
    ap.add_argument('--min-total', type=float, default=0.0, help='精确计时：累计 CPU 不到这么多秒就继续跑（最多 80 遍），取平均；和 评测.py 相同')
    ap.add_argument('--skip', nargs='*', default=[], help='不测的队（终版全错、增值必为 0 的）')
    args = ap.parse_args()
    hidden = os.path.join(HERE, '隐藏负载2')
    with open(os.path.join(hidden, 'manifest.json'), encoding='utf-8') as f:
        manifest = json.load(f)
    old_dir = os.path.join(BUILD, '隐藏负载2_旧格式')
    os.makedirs(old_dir, exist_ok=True)
    for w in manifest:
        with open(os.path.join(hidden, w + '.in'), encoding='utf-8') as f:
            text = to_old(f.read())
        with open(os.path.join(old_dir, w + '.in'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
    out_path = os.path.join(EVENT, '结果', '主程基准.jsonl')
    done = set()
    if os.path.exists(out_path):
        with open(out_path, encoding='utf-8') as f:
            done = {(r['entry'], r['workload']) for r in map(json.loads, f)}
    with open(out_path, 'a', encoding='utf-8') as out:
        for t in teams():
            team = t['team']
            if team in args.skip:
                continue
            exe = measure.build(lead_src(team), os.path.join(BUILD, 'leadbase', team + measure.EXE))
            for w, m in manifest.items():
                if (team, w) in done:
                    continue
                limit = m['baseline_cpu'] * 3 + 10
                cpus, status = [], 'ok'
                for _ in range(args.repeat):
                    r = measure.run(exe, os.path.join(old_dir, w + '.in'), timeout=limit)
                    if r['timeout']:
                        status = 'timeout'
                        cpus.append(limit)
                        break
                    if r['code'] != 0:
                        status = 'crash'
                    cpus.append(r['cpu'])
                    if r['cpu'] > 10:
                        break
                cpus.sort()
                cpu, n = cpus[len(cpus) // 2], len(cpus)
                if status == 'ok' and args.min_total and cpu * n < args.min_total:
                    total = sum(cpus)
                    while total < args.min_total and n < 80:
                        total, n = total + measure.run(exe, os.path.join(old_dir, w + '.in'), timeout=limit)['cpu'], n + 1
                    cpu = total / n
                rec = {'entry': team, 'lead': t['主程'], 'workload': w, 'status': status, 'cpu': round(cpu, 4), 'runs': n}
                out.write(json.dumps(rec, ensure_ascii=False) + '\n')
                out.flush()
                print('[%s %s] %-14s %-8s %.2f 秒' % (team, t['主程'], w, status, rec['cpu']), flush=True)


if __name__ == '__main__':
    main()
