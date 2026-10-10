"""赛后：生成匿名的盲评包（6 份主程代码 + 6 份接手终版），每份一个编号。

  python 裁判/生成盲评包.py --seed 任意整数

输出：赛事/盲评/包/<编号>/{src/, CHANGE.md, 评审说明.md, 锚点/}
      赛事/盲评/对照表.json（编号 → 队伍和类型，**不要给裁判看**）
匿名化：把文本里出现的模型名、厂商名、工具名和队名替换成"某某"。
"""
import argparse
import json
import os
import random
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from 赛事工具 import EVENT, HERE, ROOT, WORKSPACE, lead_src, teams  # noqa: E402

NAMES = ['GPT', 'OpenAI', 'Codex', 'Astra', 'Sol', 'Claude', 'Anthropic', 'Fable', 'Opus', 'Sonnet', 'Haiku',
         'Grok', 'xAI', 'DeepSeek', 'Gemini', 'Google', 'Kimi', 'Moonshot', 'GLM', 'Zhipu', '智谱', '豆包', 'Doubao',
         'ByteDance', '字节', 'MiMo', 'Xiaomi', '小米', 'Qwen', '通义', 'ZCode', 'WorkBuddy', 'Cursor']
PAT = re.compile(r'\b(?:' + '|'.join(sorted(map(re.escape, NAMES), key=len, reverse=True)) + r')\b|'
                 + '|'.join(re.escape(n) for n in NAMES if not n.isascii()) + r'|队[A-F]', re.I)


def copy_anon(src, dst):
    for d, dirs, files in os.walk(src):
        dirs[:] = [x for x in dirs if x not in ('build', '__pycache__')]
        for f in files:
            p = os.path.join(d, f)
            q = os.path.join(dst, os.path.relpath(p, src))
            os.makedirs(os.path.dirname(q), exist_ok=True)
            try:
                with open(p, encoding='utf-8') as fh:
                    text = fh.read()
            except UnicodeDecodeError:
                continue  # 非文本文件不放进评审包
            with open(q, 'w', encoding='utf-8', newline='\n') as fh:
                fh.write(PAT.sub('某某', text))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--seed', type=int, required=True)
    args = ap.parse_args()
    subjects = []
    for t in teams():
        subjects.append((t['team'], '主程', lead_src(t['team'])))
        d = os.path.join(EVENT, '接手提交', t['team'])
        subjects.append((t['team'], '终版', os.path.join(d, 'src') if os.path.isdir(os.path.join(d, 'src')) else d))
    rng = random.Random(args.seed)
    codes = rng.sample(range(100, 1000), len(subjects))
    out = os.path.join(EVENT, '盲评', '包')
    shutil.rmtree(out, ignore_errors=True)
    table = {}
    for (team, kind, src), code in zip(subjects, codes):
        if not os.path.isdir(src):
            sys.exit('缺少 %s 的%s代码：%s' % (team, kind, src))
        pk = os.path.join(out, '代码%d' % code)
        copy_anon(src, os.path.join(pk, 'src'))
        shutil.copy(os.path.join(ROOT, '第二阶段', '包', 'workspace', 'CHANGE.md'), pk)
        shutil.copy(os.path.join(ROOT, '盲评模板', '评审说明.md'), pk)
        shutil.copytree(os.path.join(WORKSPACE, 'baseline'), os.path.join(pk, '锚点', '屎山基线'))
        shutil.copytree(os.path.join(HERE, '参考实现', 'v2'), os.path.join(pk, '锚点', '干净参考'))
        table['代码%d' % code] = {'team': team, 'kind': kind}
    with open(os.path.join(EVENT, '盲评', '对照表.json'), 'w', encoding='utf-8') as f:
        json.dump(table, f, ensure_ascii=False, indent=2)
    print('已生成 %d 个评审包：%s' % (len(table), '、'.join(sorted(table))))
    print('每个裁判对每个包各开一个新会话评审；评分.json 收回到 赛事/盲评/结果/<裁判名>/<编号>.json')


if __name__ == '__main__':
    main()
