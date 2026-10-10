"""赛后复盘：给 12 个模型各生成一份填好成绩的复盘提示词；命令行席位可以直接跑。

  python 裁判/复盘.py              # 生成 赛事/复盘/提示词/<队>_<角色>_<模型>.txt
  python 裁判/复盘.py --run        # 再用命令行跑 Kimi、豆包、Gemini、MiMo、Grok（各开一个空文件夹的新会话，只要一条文字回复）
  python 裁判/复盘.py --run --only "Kimi K3"

攻手（Claude、GPT 5 位）：用户在各自**第 3 轮重赛的会话里继续**，粘贴提示词（它记得自己第 3 轮做了什么；第 2 轮的事提示词里有摘要）。
主程：第 1 轮的会话是几天前的，一律开新会话；提示词里附了它自己当时写的交接文档，帮它想起做过什么。
DeepSeek、GLM 没有可用的命令行：用户在 dsh web / ZCode 里开新会话粘贴。
回复收到 赛事/复盘/回复/<模型>.txt（命令行席位自动存原始输出，文字要人工摘一下）。

措辞：发给模型的文字一律用"测试手 / 找反例"，不用"攻手 / 击穿 / 攻防"（会被 Claude、OpenAI 的安全机制拦）。
"""
import argparse
import json
import math
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from 赛事工具 import ARENA, EVENT, lead_src, teams  # noqa: E402

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')
NEUTRAL = [('团体攻防接力赛 成绩', '团体赛成绩'), ('攻手榜（强档）', '测试手榜'), ('主程榜（弱档）', '主程榜'), ('抗击穿', '抗反例'),
           ('被打穿', '被找到反例'), ('攻击', '找反例'), ('攻手', '测试手'), ('击穿', '找到反例'), ('攻防', '互测'), ('漏洞', '缺陷')]


def neutral(s):
    for a, b in NEUTRAL:
        s = s.replace(a, b)
    return s


def load(p):
    with open(os.path.join(EVENT, p), encoding='utf-8') as f:
        return json.load(f) if p.endswith('.json') else f.read()


def rank(rows, key, team):
    return sorted(rows, key=lambda t: -rows[t][key]).index(team) + 1


def talk():
    out = {}
    for line in load('赛前狠话.md').split('\n'):
        m = re.match(r'\| (队.) \| (主程|攻手) \| (.+?) \| (.+) \|$', line)
        if m:
            out[m.group(3)] = m.group(4)
    return out


def build():
    ts = teams()
    res = load('结果/团队成绩.json')['teams']
    pts = load('击穿结果/缺陷点归类.json')['points']
    r1 = {}
    for line in load('结果/第1轮_速度成绩.md').split('\n'):
        c = [x.strip() for x in line.split('|')]
        if len(c) > 6 and c[2].startswith('队'):
            r1[c[2]] = (c[1], c[5])                       # 名次、几何平均加速
    rec1 = {t['team']: json.load(open(os.path.join(EVENT, '运行记录', t['team'] + '_主程', '元数据.json'), encoding='utf-8')) for t in ts}
    said = talk()
    table = neutral(load('结果/团队成绩.md'))
    names = {t['team']: t for t in ts}
    ptab = ['## 第 2 轮找到的缺陷点（7 个）', '', '| 被测代码（主程） | 类型 | 找到的测试手 | 是什么 |', '|---|---|---|---|']
    for p in pts:
        ptab.append('| %s（%s） | %s | %s | %s |' % (p['target'], names[p['target']]['主程'], p['kind'],
                                                '、'.join(names[a]['攻手'] for a in p['found_by']), p['desc']))
    ptab = '\n'.join(ptab)
    ask = '''请做一个简短复盘，**总共不超过 200 字**：
1. 你做得最对的一个决定；
2. 你最大的失误，以及原因；
3. 回应一下你的赛前狠话；
4. 对队友或对手说一句话。

不要修改任何文件，也不要再运行代码，直接用文字回答。'''
    out = {}
    for t in ts:
        team, r = t['team'], res[t['team']]
        tr = rank(res, 'team_total', team)
        mine = [p for p in pts if p['target'] == team]
        found = [p for p in pts if team in p['found_by']]
        missed = [p for p in pts if team not in p['found_by'] and p['target'] != team]
        gain = ('同一批数据上比你交卷时的版本平均快 %s 倍' % r['gain_geomean']) if r['correct_rate'] > 0 and r['gain_geomean'] else ''
        # ---- 主程
        m1 = rec1[team]
        s = '你所在的%s（你是主程，队友是测试手 %s）团队总分 %.1f，6 队里第 %d 名。' % (team, t['攻手'], r['team_total'], tr)
        s += '你在第 1 轮%s：隐藏负载上平均加速 %s（速度分 %.0f，6 个主程里第 %s 名）。' % (
            '用了 %.1f 分钟自己交卷' % m1['用时分钟'] if not m1.get('强制停止') else '用满 45 分钟被强制停止', r1[team][1] if r1[team][1] != '—' else '有一组比基线还慢被判超时', r['S1'], r1[team][0])
        if mine:
            s += '第 2 轮有 %d 位测试手在你的代码里找到了反例，共 %d 个缺陷点：%s。' % (
                r['broken_by'], len(mine), '；'.join('（%s，%s找到）%s' % (p['kind'], '、'.join(names[a]['攻手'] for a in p['found_by']), p['desc']) for p in mine))
        else:
            s += '第 2 轮 5 位测试手都没能在你的代码里找到反例，你是唯一一个。'
        if r['correct_rate'] > 0:
            s += '第 3 轮你的队友接手了你的代码：终版 9 组隐藏负载全部正确，%s，回归用例修复 %s。' % (gain, r['fixed'])
        else:
            s += '第 3 轮你的队友接手了你的代码，但 45 分钟到点时 `src/` 没有任何改动，终版在新格式的输入上全部出错。'
        s += '主程个人榜你是第 %d 名（%.1f 分），可维护性盲评 %.1f / 10。' % (rank(res, 'lead', team), r['lead'], r['M1'] / 10)
        handoff = os.path.join(lead_src(team), 'HANDOFF.md')
        memo = neutral(open(handoff, encoding='utf-8', errors='replace').read()) if os.path.exists(handoff) else '（没有交接文档）'
        out[(team, '主程', t['主程'])] = '''几天前你参加了一场 AI 编程团体赛，你是%s的主程：在 45 分钟内优化一个又慢又乱的仓储调度仿真程序，要求输出与原始基线逐字节相同；交卷后别队的测试手给你的代码找反例，然后你的队友接手你的代码实现需求变更并继续优化。当时的会话已经结束。为了帮你回忆，最后附了你当时写给队友的交接文档。

比赛结束了，成绩已经出来（完整成绩表和缺陷点列表附在下面）。

你的成绩：%s

你赛前说过："%s"

%s

%s

%s

## 你当时写的交接文档（节选自你的提交）

%s
''' % (team, s, said.get(t['主程'], ''), ask, table, ptab, memo[:6000])
        # ---- 测试手
        a = '你所在的%s（你是测试手，队友是主程 %s）团队总分 %.1f，6 队里第 %d 名。' % (team, t['主程'], r['team_total'], tr)
        a += '第 2 轮（给别队代码找反例）你找到了 %d 个缺陷点：%s；找反例得分 %.0f，6 位测试手里第 %d 名。' % (
            len(found), '、'.join('%s的"%s…"' % (names[p['target']]['主程'], p['desc'][:18]) for p in found), r['A'], rank(res, 'A', team))
        if missed:
            a += '别人找到而你没找到的：%s。' % '、'.join('%s（%s，%s找到）' % (names[p['target']]['主程'], p['kind'], '、'.join(names[x]['攻手'] for x in p['found_by'])) for p in missed)
        if r['correct_rate'] > 0:
            a += '第 3 轮（接手队友的代码）：终版 9 组隐藏负载全部正确，回归用例修复 %s，接手速度分 %.0f（第 %d 名），%s（增值分 %.0f），可维护性盲评 %.1f / 10。' % (
                r['fixed'], r['S2'], rank(res, 'S2', team), gain.replace('比你交卷时的版本', '比队友交卷时的版本'), r['V'], r['M2'] / 10)
        else:
            a += '第 3 轮（接手队友的代码）：45 分钟到点时你的 `src/` 和队友的原版完全相同，没有任何改动，所以终版在新格式的输入上全部出错，接手正确、接手速度、增值都是 0 分，4 个回归用例也没有修。'
        a += '测试手个人榜你是第 %d 名（%.1f 分）。' % (rank(res, 'attacker', team), r['attacker'])
        out[(team, '攻手', t['攻手'])] = '''比赛全部结束了，成绩已经出来（完整成绩表和缺陷点列表附在下面）。这场比赛你参加了两轮：先给别队主程的代码做回归测试、找反例，再接手本队主程的代码实现需求变更并优化。

你的成绩：%s

你赛前说过："%s"

%s

%s

%s
''' % (a, said.get(t['攻手'], ''), ask, table, ptab)
    d = os.path.join(EVENT, '复盘', '提示词')
    os.makedirs(d, exist_ok=True)
    for (team, role, model), text in out.items():
        with open(os.path.join(d, '%s_%s_%s.txt' % (team, '测试手' if role == '攻手' else role, model)), 'w', encoding='utf-8') as f:
            f.write(text)
    print('已生成 %d 份提示词：%s' % (len(out), d))
    return out


def run(out, only):
    from 开跑 import TOOLS, kill_shared_services, tool_env
    for (team, role, model), text in out.items():
        if model not in TOOLS or (only and model != only):
            continue
        folder = os.path.join(ARENA, '复盘 ' + model)
        os.makedirs(folder, exist_ok=True)
        desc, cmd, mode = TOOLS[model]
        argv = [text if a == '{prompt}' else folder if a == '{folder}' else a for a in cmd]
        env = dict(os.environ, PYTHONUTF8='1', PYTHONIOENCODING='utf-8', PWD=folder, INIT_CWD=folder,
                   OPENCODE_DISABLE_AUTOUPDATE='1', **tool_env(model))
        kill_shared_services()
        rd = os.path.join(EVENT, '复盘', '原始输出')
        os.makedirs(rd, exist_ok=True)
        print('[%s] %s …' % (model, desc), flush=True)
        try:
            r = subprocess.run(argv, cwd=folder, env=env, capture_output=True, timeout=420, stdin=subprocess.DEVNULL)
            data, code = r.stdout, r.returncode
        except subprocess.TimeoutExpired as e:
            data, code = e.stdout or b'', '超时'
        with open(os.path.join(rd, model + '.jsonl'), 'wb') as f:
            f.write(data)
        print('    退出码 %s，输出 %d 字节 → %s' % (code, len(data), os.path.join(rd, model + '.jsonl')), flush=True)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--run', action='store_true')
    ap.add_argument('--only')
    args = ap.parse_args()
    o = build()
    if args.run:
        run(o, args.only)
