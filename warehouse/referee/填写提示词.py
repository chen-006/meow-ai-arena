"""按抽签结果和历代战绩，把提示词模板填好，方便测试员直接复制。

  python 裁判/填写提示词.py 狠话      # → 赛事/赛前狠话_已填.md
  python 裁判/填写提示词.py 主程      # → 赛事/第1轮_主程提示词_已填.md

战绩数据来自 赛事/历代战绩.json。
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from 赛事工具 import EVENT, ROOT, teams  # noqa: E402

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')


def history():
    with open(os.path.join(EVENT, '历代战绩.json'), encoding='utf-8') as f:
        return json.load(f)


def record_line(h, model):
    rec = h['战绩'].get(model)
    if not rec or all(x == '未参赛' for x in rec):
        return '本期新面孔，前 4 期没有参赛'
    return '；'.join('%s：%s' % (ep.split('（')[0], r) for ep, r in zip(h['集'], rec))


def table(h, ts):
    L = ['| 队伍 | 角色 | 模型 | ' + ' | '.join(h['集']) + ' |', '|---|---|---|' + '---|' * len(h['集'])]
    for t in ts:
        for role in ('主程', '攻手'):
            m = t[role]
            L.append('| %s | %s | %s | %s |' % (t['team'], role, m, ' | '.join(h['战绩'].get(m, ['—'] * 4))))
    return '\n'.join(L)


def template(name):
    with open(os.path.join(ROOT, '提示词', name), encoding='utf-8') as f:
        return f.read().split('---\n')[1].strip('\n')


def fill_trash(h, ts):
    tpl = template('4_赛前狠话.md')
    tab = table(h, ts)
    out = ['# 赛前狠话（已按抽签和战绩填好，种子 %s）' % json.load(open(os.path.join(EVENT, '抽签结果.json'), encoding='utf-8'))['seed'],
           '', '每个模型**单独开一个新会话**（不在比赛文件夹里），复制对应的那一段粘贴发送，只取第一条回复。', '']
    for t in ts:
        for role, mate_role in (('主程', '攻手'), ('攻手', '主程')):
            me, mate = t[role], t[mate_role]
            text = (tpl.replace('{队伍}', t['team']).replace('{角色}', role).replace('{队友}', mate)
                    .replace('{你的战绩}', record_line(h, me)).replace('{队友战绩}', record_line(h, mate))
                    .replace('{全员战绩表}', tab))
            out += ['## %s（%s · %s）' % (me, t['team'], role), '', '```text', text, '```', '']
    return out


def fill_lead(h, ts):
    tpl = template('1_主程.md')
    out = ['# 第 1 轮 · 主程提示词（已按抽签和战绩填好）', '',
           '命令行席位（Kimi、豆包、Gemini、MiMo）由 `python 裁判/开跑.py <队>` 自动发题，不用手动复制。',
           '手动席位（GLM、DeepSeek）：`python 裁判/开局.py <队> 准备` 会在 `C:\\arena5\\第1轮 <队> 主程 <模型>` 里放好 '
           '`workspace.zip` 和 `提示词.txt`（内容就是下面对应的一段）；粘贴的同时运行 `python 裁判/开局.py <队> 开始`。', '']
    for t in ts:
        rivals = '\n'.join('- %s（%s）' % (o['攻手'], record_line(h, o['攻手'])) for o in ts if o['team'] != t['team'])
        text = (tpl.replace('{队伍}', t['team']).replace('{队友}', t['攻手']).replace('{对手攻手}', '见下方名单')
                .replace('{你的战绩}', record_line(h, t['主程'])).replace('{队友战绩}', record_line(h, t['攻手']))
                .replace('{对手攻手战绩}', rivals))
        out += ['## %s（%s · 主程，队友 %s）' % (t['主程'], t['team'], t['攻手']), '', '```text', text, '```', '']
    return out


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else ''
    h, ts = history(), teams()
    if what == '狠话':
        path, out = os.path.join(EVENT, '赛前狠话_已填.md'), fill_trash(h, ts)
    elif what == '主程':
        path, out = os.path.join(EVENT, '第1轮_主程提示词_已填.md'), fill_lead(h, ts)
    else:
        sys.exit(__doc__)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out) + '\n')
    print('已写入', path)


if __name__ == '__main__':
    main()
