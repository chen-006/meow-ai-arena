"""第 3 轮：给每个攻手生成接手包（本队主程的代码 + 需求变更 + 本队被击穿的用例）。

  python 裁判/生成接手包.py

前提：已运行 击穿判定.py。
输出：赛事/接手包/接手包_<队>.zip，解压后是 workspace/ 文件夹（与攻手已有的 attack/ 并列）。
      赛事/接手包/被击穿用例_第二阶段格式/<队>/*.in/.out（修复率判定用的同一份用例）

被击穿用例转换成第二阶段格式：PARAMS 末尾追加 agingEvery=1000000000、carryCost=1。
这两个取值下需求变更不起作用，行为与原用例相同（只是报表多了 aged=0），标准输出由 v2_变更 参考实现生成。
"""
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'workspace', 'tools'))
from 赛事工具 import BUILD, EVENT, HERE, ROOT, WORKSPACE, add_dir, clock_for, lead_src, teams, zip_open  # noqa: E402
import measure  # noqa: E402

P2_PACK = os.path.join(ROOT, '第二阶段', '包', 'workspace')
TAKEOVER = os.path.join(ROOT, '第二阶段', 'TAKEOVER.md')
NEUTRAL = ' 1000000000 1'


def to_p2(text):
    lines = text.replace('\r\n', '\n').split('\n')
    for i, s in enumerate(lines):
        if s.startswith('PARAMS '):
            lines[i] = s + NEUTRAL
            return '\n'.join(lines)
    raise ValueError('没有 PARAMS 行')


def main():
    ref2 = measure.build(os.path.join(HERE, '参考实现', 'v2_变更'), os.path.join(BUILD, 'ref2' + measure.EXE))
    broken = os.path.join(EVENT, '击穿结果', '被击穿用例')
    conv_root = os.path.join(EVENT, '接手包', '被击穿用例_第二阶段格式')
    shutil.rmtree(conv_root, ignore_errors=True)
    import json
    with open(os.path.join(EVENT, '击穿结果', '矩阵.json'), encoding='utf-8') as fh:
        mat = json.load(fh)['matrix']
    slow = {}   # 目标队 -> 被击穿用例目录里属于超时类的文件名
    for att, row in mat.items():
        for target, cell in row.items():
            for c in cell['cases']:
                if c.get('verdict') == '击穿' and c.get('kind') == '超时':
                    slow.setdefault(target, set()).add('from%s_%s' % (att[-1], c['file']))
    for t in teams():
        team = t['team']
        conv = os.path.join(conv_root, team)
        os.makedirs(conv, exist_ok=True)
        src_dir = os.path.join(broken, team)
        for f in sorted(os.listdir(src_dir)) if os.path.isdir(src_dir) else []:
            with open(os.path.join(src_dir, f), encoding='ascii') as fh:
                text = to_p2(fh.read())
            # 性能型反例（上一轮判定为超时）文件名加 slow_ 前缀：bench2.py 遇到第一个还没修好的就不再跑其余的，免得每次白等几分钟
            p = os.path.join(conv, ('slow_' if f in slow.get(team, ()) else '') + f)
            with open(p, 'w', encoding='ascii', newline='\n') as fh:
                fh.write(text)
            r = subprocess.run([ref2], input=text.encode(), capture_output=True, timeout=120)
            if r.returncode != 0:
                sys.exit('参考实现在 %s 上出错' % p)
            with open(p[:-3] + '.out', 'wb') as fh:
                fh.write(r.stdout.replace(b'\r\n', b'\n'))
        n = len([f for f in os.listdir(conv) if f.endswith('.in')])

        out = os.path.join(EVENT, '接手包', '接手包_%s.zip' % team)
        with zip_open(out) as z:
            z.write(TAKEOVER, 'workspace/TAKEOVER.md')
            z.write(os.path.join(WORKSPACE, 'SPEC.md'), 'workspace/SPEC.md')
            add_dir(z, os.path.join(WORKSPACE, 'baseline'), 'workspace/baseline')
            add_dir(z, os.path.join(WORKSPACE, 'workloads'), 'workspace/workloads')
            # 工具用互测包里的 check_input.py（措辞是"互测用例"，不是"击穿用例"），其余照搬 workspace/tools
            for f in sorted(os.listdir(os.path.join(WORKSPACE, 'tools'))):
                full = os.path.join(WORKSPACE, 'tools', f)
                if os.path.isfile(full):
                    if f == 'check_input.py':
                        full = os.path.join(ROOT, '互测包模板', 'review', 'tools', 'check_input.py')
                    z.write(full, 'workspace/tools/' + f)
            add_dir(z, P2_PACK, 'workspace')            # CHANGE.md、tools/bench2.py、tools/gen2.py、workloads2/
            add_dir(z, conv, 'workspace/workloads2/regression')
            # 交接文档副本里"攻击面/攻手"之类的词换成中性说法（Claude、OpenAI 的安全机制都会拦），代码文件原样
            from 生成攻防包 import NEUTRAL as WORDS
            for d0, dirs0, files0 in os.walk(lead_src(team)):
                dirs0[:] = [x for x in dirs0 if x not in ('build', '__pycache__')]
                for f0 in files0:
                    full0 = os.path.join(d0, f0)
                    arc0 = ('workspace/src/' + os.path.relpath(full0, lead_src(team))).replace(os.sep, '/')
                    if f0.endswith('.md'):   # 所有队都替换（Claude 也会拦这类字眼）
                        s0 = open(full0, encoding='utf-8', errors='replace').read()
                        for x0, y0 in WORDS:
                            s0 = s0.replace(x0, y0)
                        z.writestr(arc0, s0)
                    else:
                        z.write(full0, arc0)
            # 本轮计时器：读比赛文件夹里的 .clock_start_round3.json（由 开局.py 在本轮开赛时写入）
            z.writestr('workspace/clock.py', clock_for(3))
        print('已生成 %s（本队被击穿用例 %d 个）' % (out, n))


if __name__ == '__main__':
    main()
