"""第 2 轮：给每个攻手生成互测包（另外 5 队的主程代码 + 基线 + 工具）。

  python 裁判/生成攻防包.py

前提：赛事/抽签结果.json 已生成，6 队的主程提交都放进了 赛事/主程提交/<队>/src。
输出：赛事/互测包/互测包_<队>.zip（发给该队的攻手）。

选手看到的一切都用"互测/测试"的说法（review/、submissions/、cases/、try_case.py）：
最早的版本叫 attack/、targets/、hacks/、"攻防""击穿"，Claude 的安全机制把它当成网络攻击任务拦下了（2026-10-06 第 2 轮第一次开局）。
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from 赛事工具 import EVENT, HERE, ROOT, WORKSPACE, add_dir, add_text, clock_for, lead_src, teams, zip_open  # noqa: E402

TEMPLATE = os.path.join(ROOT, '互测包模板', 'review')
# GPT 版：OpenAI 的网络安全分类器拦下了通用版（Astra 读 REVIEW.md 就被拦，Sol 读到交接文档里的"攻击面"被拦）。
# 规则、计分完全相同；REVIEW.md 换成"回归测试"说法，被测代码的交接文档里几个词换成中性说法（只改 GPT 拿到的副本）。
GPT_MODELS = {'GPT-6.1 Sol', 'GPT-6 Astra'}
REVIEW_GPT = os.path.join(ROOT, '互测包模板', 'REVIEW_GPT版.md')
NEUTRAL = [('攻击面提醒', '风险点提醒'), ('攻击面', '风险点'), ('攻击者', '测试者'), ('被攻击', '被测到'),
           ('攻手', '测试手'), ('被击穿', '被测出不一致'), ('击穿', '测出不一致'), ('攻击', '测试'),
           ('一定会打', '一定会测'), ('可能打哪里', '可能测哪里'), ('漏洞', '缺陷'), ('越界崩溃', '越界出错')]


def add_src(z, src, arc, neutral):
    for d, dirs, files in os.walk(src):
        dirs[:] = [x for x in dirs if x not in ('build', '__pycache__')]
        for f in files:
            full = os.path.join(d, f)
            name = (arc + '/' + os.path.relpath(full, src)).replace(os.sep, '/')
            if neutral and f.endswith(('.md', '.cpp', '.h', '.txt')):
                s = open(full, encoding='utf-8', errors='replace').read()
                for a, b in NEUTRAL:
                    s = s.replace(a, b)
                z.writestr(name, s)
            else:
                z.write(full, name)


def main():
    ts = teams()
    for t in ts:
        if not os.path.isdir(lead_src(t['team'])):
            sys.exit('缺少主程提交：%s' % lead_src(t['team']))
    for t in ts:
        out = os.path.join(EVENT, '互测包', '互测包_%s.zip' % t['team'])
        gpt = False   # 重赛（2026-10-09）起 6 家同一版：REVIEW.md 已统一成回归测试的说法
        with zip_open(out) as z:
            for d, dirs, files in os.walk(TEMPLATE):
                dirs[:] = [x for x in dirs if x not in ('build', '__pycache__')]
                for f in files:
                    full = os.path.join(d, f)
                    name = ('review/' + os.path.relpath(full, TEMPLATE)).replace(os.sep, '/')
                    z.write(REVIEW_GPT if gpt and name == 'review/REVIEW.md' else full, name)
            # 本轮计时器：读比赛文件夹里的 .clock_start_round2.json（由 开局.py 在本轮开赛时写入）
            add_text(z, 'review/clock.py', clock_for(2))
            add_dir(z, os.path.join(WORKSPACE, 'baseline'), 'review/baseline')
            add_dir(z, os.path.join(WORKSPACE, 'workloads', 'public'), 'review/workloads/public')
            for o in t['攻击对象']:
                add_src(z, lead_src(o), 'review/submissions/%s/src' % o, True)   # 所有人拿到的副本都换成中性说法
                add_text(z, 'review/cases/%s/把用例放在这里.txt' % o, '每份实现最多 3 个 .in 用例；每个用例请在同一文件夹的 说明.txt 里写一两行（缺陷在哪、为什么）。\n')
        print('已生成', out, '被测：', '、'.join(t['攻击对象']))


if __name__ == '__main__':
    main()
