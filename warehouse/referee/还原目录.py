"""把开源仓库里的第 5 集材料还原成比赛时的目录结构，裁判脚本不用改就能运行。

  python 还原目录.py <目标文件夹>        # 目标文件夹必须不存在或为空

这个脚本是开源时新写的，比赛时没有。裁判脚本按比赛时的目录（裁判/、赛事/、workspace/、
互测包模板/、第二阶段/、盲评模板/、提示词/）找文件，仓库里换成了英文目录名，所以先复制还原。
只复制，不改任何文件内容；对应关系见同目录 README.md 的"目录对应"。
"""
import json
import os
import shutil
import sys

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)                      # warehouse/
JUDGES = ['GPT-6 Astra', 'Opus 5.5', 'DeepSeek V4.1 Flash']
OWN = {'README.md', '还原目录.py'}                 # 开源时新增的文件，不属于比赛时的 裁判/


def slug(model):
    return model.lower().replace('豆包 seed', 'doubao-seed').replace(' ', '-')


def copy(src, dst):
    if os.path.isdir(src):
        shutil.copytree(src, dst, dirs_exist_ok=True, ignore=shutil.ignore_patterns('__pycache__', 'build'))
    else:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = os.path.abspath(sys.argv[1])
    if os.path.exists(out) and os.listdir(out):
        sys.exit('目标文件夹已存在且不为空：%s' % out)
    R = lambda *a: os.path.join(REPO, *a)
    O = lambda *a: os.path.join(out, *a)
    with open(R('results', '抽签结果.json'), encoding='utf-8') as f:
        teams = json.load(f)['teams']
    lead = {t['team']: slug(t['主程']) for t in teams}
    att = {t['team']: slug(t['攻手']) for t in teams}

    # 选手包和各轮包的固定部分
    copy(R('workspace'), O('workspace'))
    copy(R('packages', 'round2', 'review'), O('互测包模板', 'review'))
    copy(R('packages', 'retest', 'REVIEW.md'), O('互测包模板', 'REVIEW_终版复测.md'))
    copy(R('packages', 'round3', 'TAKEOVER.md'), O('第二阶段', 'TAKEOVER.md'))
    copy(R('packages', 'round3', 'workspace'), O('第二阶段', '包', 'workspace'))
    copy(R('packages', 'blind-review'), O('盲评模板'))
    # 裁判程序
    for f in os.listdir(HERE):
        if f in OWN or f in ('__pycache__', 'build'):
            continue
        copy(os.path.join(HERE, f), O('提示词') if f == '提示词' else O('裁判', f))
    # 各轮提交
    for t in lead:
        copy(R('submissions', 'round1', lead[t]), O('赛事', '主程提交', t, 'src'))
        copy(R('submissions', 'round3', lead[t] + '__' + att[t]), O('赛事', '接手提交', t, 'src'))
        for o in lead:
            d = R('submissions', 'round2', att[t], lead[o])
            if o != t and os.path.isdir(d):
                copy(d, O('赛事', '击穿提交', t, 'hacks', o))
        d = R('packages', 'round3', 'regression', lead[t])
        os.makedirs(O('赛事', '接手包', '被击穿用例_第二阶段格式', t), exist_ok=True)
        if os.path.isdir(d):
            copy(d, O('赛事', '接手包', '被击穿用例_第二阶段格式', t))
    # 成绩和判定结果
    for f in os.listdir(R('results')):
        p = R('results', f)
        if os.path.isfile(p) and f != 'README.md':
            copy(p, O('赛事', f) if f.startswith(('抽签结果', '历代战绩')) else O('赛事', '结果', f))
    copy(R('results', 'round2'), O('赛事', '击穿结果'))
    for f in os.listdir(R('results', 'blind-review')):
        p = R('results', 'blind-review', f)
        if os.path.isfile(p):
            copy(p, O('赛事', '盲评', f))
    for j in JUDGES:
        copy(R('results', 'blind-review', slug(j)), O('赛事', '盲评', '结果', j))
        if os.path.isdir(R('results', 'retest', slug(j))):
            copy(R('results', 'retest', slug(j)), O('赛事', '终版复测', '提交', j))
    for f in ('判定.md', '判定.json', '对照表.json'):
        copy(R('results', 'retest', f), O('赛事', '终版复测', f))
    for f in ('性能探针.md', '性能探针.json'):
        copy(R('results', 'final-check', f), O('赛事', '结果', '终版体检', f))
    copy(R('results', 'final-check', '赛后流水线.log'), O('赛事', '运行记录', '赛后流水线.log'))
    copy(R('notes', '赛前狠话.md'), O('赛事', '赛前狠话.md'))
    # 击穿结果/被击穿用例：成立的反例，文件名 from<测试手队字母>_<原文件名>（击穿判定.py 也会重新生成）
    with open(R('results', 'round2', '矩阵.json'), encoding='utf-8') as f:
        matrix = json.load(f)['matrix']
    for a, row in matrix.items():
        for target, cell in row.items():
            for c in cell['cases']:
                if c.get('verdict') == '击穿':
                    copy(O('赛事', '击穿提交', a, 'hacks', target, c['file']),
                         O('赛事', '击穿结果', '被击穿用例', target, 'from%s_%s' % (a[-1], c['file'])))
    print('已还原到', out)
    print('接下来在这个文件夹里按 README.md 的顺序运行，例如：')
    print('  python 裁判/评测.py --phase 2 --entries 赛事/接手提交 --out 复算/results2.jsonl')


if __name__ == '__main__':
    main()
