"""第 3 轮接手包里的大负载：生成 3 组接近正式规模的公开负载，用参考实现算出标准输出和基准 CPU 时间。

  python 裁判/生成大型公开负载.py [--seed 1] [--repeat 3]

输出：第二阶段/包/workspace/workloads2/big/<配置名>.in、.out、reference_cpu.json
      （生成接手包.py 会把整个 第二阶段/包/workspace 打进接手包）

为什么要有：第一次比赛包里只有小负载（优化过的代码合计不到 1 秒），攻手看不出快慢，做对就收工了。
配置在 第二阶段/包/workspace/tools/gen2.py 的 BIG 里，和隐藏负载的参数不同（隐藏负载的配置只在 裁判/gen.py）。
基准时间要在比赛用的这台机器上测，测的时候不要跑别的耗 CPU 的程序。
"""
import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PUB_TOOLS = os.path.join(ROOT, 'workspace', 'tools')
P2_TOOLS = os.path.join(ROOT, '第二阶段', '包', 'workspace', 'tools')
sys.path.insert(0, PUB_TOOLS)          # 公开版 gen.py、measure.py、check_input.py
import check_input  # noqa: E402
import measure  # noqa: E402

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')


def load_public_gen2():
    """包里的 gen2.py（公开版）。不能直接 import：裁判/ 下有同名的完整版。"""
    spec = importlib.util.spec_from_file_location('gen2_public', os.path.join(P2_TOOLS, 'gen2.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--repeat', type=int, default=3, help='参考实现运行次数（取中位数）')
    args = ap.parse_args()
    gen2 = load_public_gen2()
    ref = measure.build(os.path.join(HERE, '参考实现', 'v2_变更'), os.path.join(HERE, 'build', 'ref2' + measure.EXE))
    out_dir = os.path.join(ROOT, '第二阶段', '包', 'workspace', 'workloads2', 'big')
    os.makedirs(out_dir, exist_ok=True)
    times = {}
    for name in gen2.BIG:
        text = gen2.generate(name, args.seed)
        check_input.check(text, full=True, p2=True)
        path = os.path.join(out_dir, name + '.in')
        with open(path, 'w', encoding='ascii', newline='\n') as f:
            f.write(text)
        runs = [measure.run(ref, path) for _ in range(args.repeat)]
        if any(r['code'] != 0 or r['digest'] != runs[0]['digest'] for r in runs):
            sys.exit('%s：参考实现出错或两次输出不同' % name)
        with open(os.path.join(out_dir, name + '.out'), 'wb') as f:
            f.write(measure.normalized(runs[0]['output']))
        cpus = sorted(r['cpu'] for r in runs)
        times[name] = round(cpus[len(cpus) // 2], 2)
        print('%-16s 参考实现 CPU %s 秒（中位数 %.2f），输出 %d 行，输入 %d 字节' % (
            name, ' / '.join('%.2f' % c for c in cpus), times[name], runs[0]['output'].count(b'\n'), len(text)), flush=True)
    with open(os.path.join(out_dir, 'reference_cpu.json'), 'w', encoding='utf-8') as f:
        json.dump(times, f, ensure_ascii=False, indent=2)
    print('完成：', out_dir)


if __name__ == '__main__':
    main()
