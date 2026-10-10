"""本地测试：检查用例是否合法，并在基线和被测代码上各跑一次，比较输出是否一致。

  python tools/try_case.py cases/队B/case1.in       # 测试一个用例（被测队由所在文件夹决定）
  python tools/try_case.py --all                     # 测试 cases/ 下的全部用例
  python tools/try_case.py --team 队B 任意.in        # 用任意位置的输入测试某一队
  python tools/try_case.py --build                   # 先把基线和全部被测代码编译好（第一次比较慢，建议开局先跑）

判定规则和正式评测相同：用例必须通过 check_input.py（互测用例上限）；基线必须在 10 秒内跑完；
被测程序输出与基线不一致、退出码非 0，或运行超过 60 秒，就判"不一致"。
"""
import argparse
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import measure  # noqa: E402
import check_input  # noqa: E402
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')  # Windows 控制台默认 GBK，统一输出 UTF-8

BASE_LIMIT, SUB_LIMIT = 10, 60


def exe_for(name, src):
    out = os.path.join(ROOT, 'build', name + measure.EXE)
    newest = max(os.path.getmtime(os.path.join(src, f)) for f in os.listdir(src))
    if not os.path.exists(out) or os.path.getmtime(out) < newest:
        measure.build(src, out)
    return out


def baseline_result(data, case):
    """基线在同一个用例上的结果按内容缓存：同一个用例试打多家时，基线只跑一次。跑不完返回 None。"""
    base = exe_for('baseline', os.path.join(ROOT, 'baseline'))
    cdir = os.path.join(ROOT, 'build', 'baseline_cache')
    key = hashlib.sha256(data.replace(b'\r\n', b'\n')).hexdigest()
    path = os.path.join(cdir, key + '.out')
    if os.path.exists(path) and os.path.getmtime(path) >= os.path.getmtime(base):
        with open(path, 'rb') as f:
            out = f.read()
        return {'output': out, 'digest': measure.digest(out), 'cpu': 0.0}
    b = measure.run(base, case, timeout=BASE_LIMIT)
    if b['timeout'] or b['code'] != 0:
        return None
    os.makedirs(cdir, exist_ok=True)
    with open(path, 'wb') as f:
        f.write(b['output'])
    return b


def judge(case, team):
    """返回 (结论, 说明)。结论：不一致 / 一致 / 无效。"""
    with open(case, 'rb') as f:
        data = f.read()
    try:
        check_input.check(data.decode('ascii'))
    except (UnicodeDecodeError, check_input.Bad) as e:
        return '无效', '用例不合法：%s' % e
    b = baseline_result(data, case)
    if b is None:
        return '无效', '基线没能在 %d 秒内正常跑完' % BASE_LIMIT
    tdir = os.path.join(ROOT, 'submissions', team, 'src')
    if not os.path.isdir(tdir):
        return '无效', '没有这一队：%s' % team
    try:
        t_exe = exe_for('sub_' + team, tdir)
    except RuntimeError as e:
        return '不一致', '被测代码编译失败：%s' % str(e)[:200]
    r = measure.run(t_exe, case, timeout=SUB_LIMIT)
    if r['timeout']:
        return '不一致', '被测程序运行超过 %d 秒' % SUB_LIMIT
    if r['code'] != 0:
        return '不一致', '被测程序退出码 %d' % r['code']
    if r['digest'] != b['digest']:
        la = measure.normalized(b['output']).split(b'\n')
        lb = measure.normalized(r['output']).split(b'\n')
        i = next((i for i in range(max(len(la), len(lb))) if la[i:i + 1] != lb[i:i + 1]), 0)
        return '不一致', '第 %d 行不同：基线 %r / 被测 %r' % (i + 1, la[i:i + 1], lb[i:i + 1])
    return '一致', '输出一致（被测程序 %.2f 秒）' % r['cpu']


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cases', nargs='*')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--team')
    ap.add_argument('--build', action='store_true', help='只预编译基线和全部被测代码（第一次编译比较慢）')
    args = ap.parse_args()
    if args.build:
        exe_for('baseline', os.path.join(ROOT, 'baseline'))
        print('基线编译完成', flush=True)
        for t in sorted(os.listdir(os.path.join(ROOT, 'submissions'))):
            try:
                exe_for('sub_' + t, os.path.join(ROOT, 'submissions', t, 'src'))
                print('%s 编译完成' % t, flush=True)
            except RuntimeError as e:
                print('%s 编译失败（任何合法用例都判不一致）：%s' % (t, str(e)[:300]), flush=True)
        return
    jobs = []
    if args.all:
        hdir = os.path.join(ROOT, 'cases')
        for t in sorted(os.listdir(hdir)):
            files = sorted(f for f in os.listdir(os.path.join(hdir, t)) if f.endswith('.in'))
            if len(files) > 3:
                print('注意：cases/%s 下有 %d 个用例，正式判定只取按文件名排序的前 3 个' % (t, len(files)))
            jobs += [(os.path.join(hdir, t, f), t) for f in files[:3]]
    else:
        for c in args.cases:
            t = args.team or os.path.basename(os.path.dirname(os.path.abspath(c)))
            jobs.append((c, t))
    if not jobs:
        if args.all:
            print('cases/ 下还没有任何 .in 用例。把用例放进 cases/<队名>/ 再运行。')
        else:
            print(__doc__)
        sys.exit(1)
    for c, t in jobs:
        verdict, why = judge(c, t)
        print('[%s] %-30s %s  %s' % (t, os.path.basename(c), verdict, why), flush=True)


if __name__ == '__main__':
    main()
