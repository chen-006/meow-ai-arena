"""赛后两项评审的包（2026-10-08 用户决定）：

  python 裁判/生成赛后评审包.py --seed 20261009

1. 盲评（同一个对话评 12 份）：给 3 个裁判各一个文件夹 C:/arena5/盲评 <裁判>/，
   里面是 12 个 代码XXX/src、锚点/、CHANGE.md、评审说明.md、评审顺序.txt（每个裁判顺序不同）、提示词.txt。
   裁判在每个 代码XXX/ 里写 评分.json，用 --collect 收回到 赛事/盲评/结果/<裁判>/。
   前提：已运行 生成盲评包.py（赛事/盲评/包、对照表.json）。
2. 终版复测（额外项目，不计入总分）：Astra、Opus 各开一个对话，给别队的 5 份终版找反例（参考实现公开给他们）。
   C:/arena5/终版复测 <裁判>/终版复测包.zip + 提示词.txt；对照表在 赛事/终版复测/对照表.json（不要给裁判看）。

  python 裁判/生成赛后评审包.py --collect      # 收回盲评的 评分.json
"""
import argparse
import json
import os
import random
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from 赛事工具 import ARENA, EVENT, HERE, ROOT, WORKSPACE, add_dir, add_text, clock_for, teams, zip_open  # noqa: E402
from 生成盲评包 import PAT  # noqa: E402

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')
JUDGES = ['GPT-6 Astra', 'Opus 5.5', 'DeepSeek V4.1 Flash']
RETEST = ['GPT-6 Astra', 'Opus 5.5']
BLIND_PROMPT = '''请阅读当前文件夹里的 `评审说明.md`，然后完全按照它的要求，在这一个对话里评审 12 份代码（`代码XXX/src/`）的可维护性。

- 按 `评审顺序.txt` 的顺序逐份读完，每读完一份先在 `草稿.md` 里记下初步分数和证据；12 份都读完后横向比较、回头调整，再给每一份写出 `代码XXX/评分.json`，最后写 `总览.md`；
- 只读当前文件夹里的内容，不要联网，不要编译运行，不要修改任何 `src/`；
- 每个维度都要引用这一份代码自己的文件和代码作为证据；
- 不能求助其他模型（包括启动子代理），全部由你自己在这个对话里完成；
- 不要向我提问，直接完成。
'''
RETEST_PROMPT = '''这是一场 AI 编程比赛结束后的额外项目：给参赛队伍交上来的终版代码做回归测试。

当前目录里有 `终版复测包.zip`（如果已经解压过，就直接使用 `review` 文件夹）。请先自行解压，进入 `review` 文件夹，阅读 `REVIEW.md`，然后完全按照它的要求独立完成任务：为 `submissions/` 下的每份终版编写回归测试，找出它们和参考实现（`reference/`）不一致的输入。

要点（详见 `REVIEW.md`）：
- 找到的反例放在 `cases/<finalN>/`，每份最多 3 个，并在 `cases/<finalN>/说明.txt` 里写清楚缺陷在哪、为什么；用 `tools/try_case.py` 本地测试；
- 判定为"不一致"的三种情况：输出不同、异常退出、60 秒内没有结束；
- 按"缺陷点"统计（一份终版里的一个独立缺陷算一个点）。这些终版很可能有的确实找不到反例：找不到就如实说，并写下你检查过什么，不要提交没有在本地确认过的用例；
- 时间上限 45 分钟，到点强制停止；提前结束没有加分。用 `python clock.py`（在 `review` 里）查看已用时间；
- 只能读写当前文件夹里的文件；不能联网搜索，不能求助其他模型（包括启动子代理：subagent、Task / Agent 工具、后台并行代理之类）；
- 这台机器上同时运行着其他程序：只能结束你自己启动的进程（按进程号），不要按名字批量结束进程；
- 不要向我提问，也不要等待我确认，直到完成为止。

完成后请告诉我：你找到的每一个缺陷点（哪份终版、缺陷在哪里、为什么），以及哪几份没找到反例、你检查过什么。
'''


def wipe(d):
    """删掉旧文件夹再重建。锚点里的基线文件是只读的，Windows 上要先去掉只读属性。"""
    import stat

    def on_error(func, path, _):
        os.chmod(path, stat.S_IWRITE)
        func(path)
    if os.path.isdir(d):
        shutil.rmtree(d, onerror=on_error)
    os.makedirs(d)


def anon_text(path):
    try:
        with open(path, encoding='utf-8') as f:
            return PAT.sub('某某', f.read())
    except UnicodeDecodeError:
        return None


def blind(seed):
    pack = os.path.join(EVENT, '盲评', '包')
    codes = sorted(os.listdir(pack))
    for i, j in enumerate(JUDGES):
        d = os.path.join(ARENA, '盲评 ' + j)
        wipe(d)
        for c in codes:
            shutil.copytree(os.path.join(pack, c, 'src'), os.path.join(d, c, 'src'))
        shutil.copytree(os.path.join(pack, codes[0], '锚点'), os.path.join(d, '锚点'))
        shutil.copy(os.path.join(pack, codes[0], 'CHANGE.md'), d)
        shutil.copy(os.path.join(ROOT, '盲评模板', '评审说明_同一对话版.md'), os.path.join(d, '评审说明.md'))
        order = codes[:]
        random.Random('%d/%s' % (seed, j)).shuffle(order)
        with open(os.path.join(d, '评审顺序.txt'), 'w', encoding='utf-8') as f:
            f.write('请按下面的顺序评审（每个裁判拿到的顺序不同）：\n' + '\n'.join(order) + '\n')
        with open(os.path.join(d, '提示词.txt'), 'w', encoding='utf-8') as f:
            f.write(BLIND_PROMPT)
        print('盲评：', d, '顺序', ' '.join(x[2:] for x in order))


def retest(seed):
    ts = teams()
    names = [t['team'] for t in ts]
    order = names[:]
    random.Random('终版复测/%d' % seed).shuffle(order)
    table = {'final%d' % (i + 1): t for i, t in enumerate(order)}
    os.makedirs(os.path.join(EVENT, '终版复测'), exist_ok=True)
    with open(os.path.join(EVENT, '终版复测', '对照表.json'), 'w', encoding='utf-8') as f:
        json.dump(table, f, ensure_ascii=False, indent=2)
    tools = os.path.join(ROOT, '互测包模板', 'review', 'tools')
    try_case = open(os.path.join(tools, 'try_case.py'), encoding='utf-8').read()
    for a, b in (("os.path.join(ROOT, 'baseline')", "os.path.join(ROOT, 'reference')"),
                 ("check_input.check(data.decode('ascii'))", "check_input.check(data.decode('ascii'), p2=True)"),
                 ('基线', '参考实现'), ('互测用例上限', '回归用例上限，新格式')):
        assert a in try_case, a
        try_case = try_case.replace(a, b)
    for j in RETEST:
        own = next(t['team'] for t in ts if t['攻手'] == j)
        d = os.path.join(ARENA, '终版复测 ' + j)
        wipe(d)
        with zip_open(os.path.join(d, '终版复测包.zip')) as z:
            z.write(os.path.join(ROOT, '互测包模板', 'REVIEW_终版复测.md'), 'review/REVIEW.md')
            z.write(os.path.join(WORKSPACE, 'SPEC.md'), 'review/SPEC.md')
            z.write(os.path.join(ROOT, '第二阶段', '包', 'workspace', 'CHANGE.md'), 'review/CHANGE.md')
            add_text(z, 'review/clock.py', clock_for(2))
            add_dir(z, os.path.join(HERE, '参考实现', 'v2_变更'), 'review/reference')
            for f in ('check_input.py', 'measure.py', 'gen.py'):
                z.write(os.path.join(tools, f), 'review/tools/' + f)
            z.write(os.path.join(ROOT, '第二阶段', '包', 'workspace', 'tools', 'gen2.py'), 'review/tools/gen2.py')
            add_text(z, 'review/tools/try_case.py', try_case)
            for code, team in table.items():
                if team == own:
                    continue       # 不测本队的终版
                src = os.path.join(EVENT, '接手提交', team, 'src')
                for dd, dirs, files in os.walk(src):
                    dirs[:] = [x for x in dirs if x not in ('build', '__pycache__', 'work', 'tests')]
                    for f in files:
                        text = anon_text(os.path.join(dd, f))
                        if text is not None:
                            rel = os.path.relpath(os.path.join(dd, f), src).replace(os.sep, '/')
                            z.writestr('review/submissions/%s/src/%s' % (code, rel), text)
                add_text(z, 'review/cases/%s/把用例放在这里.txt' % code, '每份终版最多 3 个 .in 用例；每个用例请在同一文件夹的 说明.txt 里写一两行。\n')
        with open(os.path.join(d, '提示词.txt'), 'w', encoding='utf-8') as f:
            f.write(RETEST_PROMPT)
        print('终版复测：', d, '被测', ' '.join(c for c, t in table.items() if t != own), '（不含本队 %s）' % own)


def collect():
    for j in JUDGES:
        d = os.path.join(ARENA, '盲评 ' + j)
        out = os.path.join(EVENT, '盲评', '结果', j)
        os.makedirs(out, exist_ok=True)
        got = 0
        for c in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            p = os.path.join(d, c, '评分.json')
            if os.path.exists(p):
                shutil.copy(p, os.path.join(out, c + '.json'))
                got += 1
        for f in ('总览.md', '草稿.md'):
            if os.path.exists(os.path.join(d, f)):
                shutil.copy(os.path.join(d, f), os.path.join(out, f))
        print('%s：收回 %d / 12 份评分' % (j, got))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--seed', type=int, default=20261009)
    ap.add_argument('--collect', action='store_true')
    args = ap.parse_args()
    if args.collect:
        return collect()
    blind(args.seed)
    retest(args.seed)


if __name__ == '__main__':
    main()
