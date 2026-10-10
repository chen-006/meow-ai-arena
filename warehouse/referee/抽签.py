"""分档抽签：强档 6 人当攻手，弱档 6 人当主程，两档之间随机配对成 6 队。

  python 裁判/抽签.py --seed 20261010          # 用公开种子抽签（建议用录制当天的日期）
  python 裁判/抽签.py --seed 20261010 --dry     # 只显示，不写文件

同一个种子总是得到同样的结果，观众可以自己复现。
结果写入 赛事/抽签结果.json 和 赛事/抽签结果.md。
"""
import argparse
import json
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# 按历史战绩分档（2026-10-06 由用户确认）
STRONG = ['GPT-6 Astra', 'Fable 5.1', 'Opus 5.5', 'Grok 4.7', 'GPT-6.1 Sol', 'Sonnet 5.5']
WEAK = ['DeepSeek V4.1 Flash', 'Gemini 3.8 Flash', 'Kimi K3', 'GLM 5.3', '豆包 Seed 2.1 Turbo', 'MiMo V2.6 Pro']
TEAMS = ['队A', '队B', '队C', '队D', '队E', '队F']


def draw(seed):
    rng = random.Random('A02-第5期/%d' % seed)
    attackers = STRONG[:]
    leads = WEAK[:]
    rng.shuffle(attackers)
    rng.shuffle(leads)
    teams = [{'team': t, '主程': l, '攻手': a} for t, l, a in zip(TEAMS, leads, attackers)]
    for t in teams:
        t['攻击对象'] = [o['team'] for o in teams if o['team'] != t['team']]
    return teams


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--seed', type=int, required=True)
    ap.add_argument('--dry', action='store_true')
    args = ap.parse_args()
    teams = draw(args.seed)
    L = ['# 抽签结果（种子 %d）' % args.seed, '', '| 队伍 | 主程（弱档） | 攻手（强档） |', '|---|---|---|']
    L += ['| %s | %s | %s |' % (t['team'], t['主程'], t['攻手']) for t in teams]
    L += ['', '每个攻手攻击除本队以外的 5 支队伍的主程代码。', '',
          '复现：`python 裁判/抽签.py --seed %d --dry`' % args.seed]
    print('\n'.join(L))
    if not args.dry:
        out = os.environ.get('A02_EVENT') or os.path.join(ROOT, '赛事')
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, '抽签结果.json'), 'w', encoding='utf-8') as f:
            json.dump({'seed': args.seed, 'teams': teams}, f, ensure_ascii=False, indent=2)
        with open(os.path.join(out, '抽签结果.md'), 'w', encoding='utf-8') as f:
            f.write('\n'.join(L) + '\n')


if __name__ == '__main__':
    main()
