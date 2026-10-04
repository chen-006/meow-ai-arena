"""承诺核对：从外交消息里找出"不进 / 不攻 / 不碰某地"这类承诺，对照发言方当季和下一季的实际命令，标出疑似违约。

  python 承诺核对.py [--match 对局目录]      结果写入 对局目录/承诺核对.md 和 承诺核对.json

规则匹配只做初筛：会漏掉换种说法的承诺，也会误标有条件的承诺（"若你不进 X，我就不进 Y"）。结论以人工看原文为准。
"""
import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent
NAMES = dict(AUSTRIA='奥地利', ENGLAND='英国', FRANCE='法国', GERMANY='德国', ITALY='意大利', RUSSIA='俄国', TURKEY='土耳其')
CN = {
    '北大西洋': 'NAO', '挪威海': 'NWG', '巴伦支海': 'BAR', '北海': 'NTH', '爱尔兰海': 'IRI', '英吉利海峡': 'ENG', '海峡': 'ENG',
    '中大西洋': 'MAO', '里昂湾': 'LYO', '西地中海': 'WES', '第勒尼安海': 'TYS', '伊奥尼亚海': 'ION', '亚得里亚海': 'ADR',
    '爱琴海': 'AEG', '东地中海': 'EAS', '黑海': 'BLA', '波罗的海': 'BAL', '波的尼亚湾': 'BOT', '赫尔戈兰湾': 'HEL', '斯卡格拉克': 'SKA',
    '克莱德': 'CLY', '爱丁堡': 'EDI', '利物浦': 'LVP', '约克': 'YOR', '威尔士': 'WAL', '伦敦': 'LON',
    '布雷斯特': 'BRE', '皮卡第': 'PIC', '巴黎': 'PAR', '勃艮第': 'BUR', '加斯科涅': 'GAS', '马赛': 'MAR',
    '西班牙': 'SPA', '葡萄牙': 'POR', '北非': 'NAF', '突尼斯': 'TUN',
    '比利时': 'BEL', '荷兰': 'HOL', '鲁尔': 'RUH', '基尔': 'KIE', '柏林': 'BER', '慕尼黑': 'MUN', '普鲁士': 'PRU', '西里西亚': 'SIL',
    '丹麦': 'DEN', '瑞典': 'SWE', '挪威': 'NWY', '芬兰': 'FIN', '圣彼得堡': 'STP', '莫斯科': 'MOS', '利沃尼亚': 'LVN',
    '华沙': 'WAR', '乌克兰': 'UKR', '塞瓦斯托波尔': 'SEV', '罗马尼亚': 'RUM', '加利西亚': 'GAL', '波希米亚': 'BOH',
    '维也纳': 'VIE', '布达佩斯': 'BUD', '的里雅斯特': 'TRI', '蒂罗尔': 'TYR', '塞尔维亚': 'SER', '阿尔巴尼亚': 'ALB',
    '希腊': 'GRE', '保加利亚': 'BUL', '君士坦丁堡': 'CON', '安卡拉': 'ANK', '士麦那': 'SMY', '亚美尼亚': 'ARM', '叙利亚': 'SYR',
    '威尼斯': 'VEN', '皮埃蒙特': 'PIE', '托斯卡纳': 'TUS', '罗马': 'ROM', '那不勒斯': 'NAP', '阿普利亚': 'APU',
}
# "我不进/不会攻击/绝不碰 X" 这类否定承诺；只取一个小句
PROMISE = re.compile(r'(不会|不再|绝不|决不|永不|保证不|承诺不|不)(?:会)?(?:[^，。；！？\n]{0,6}?)(进|碰|攻|打|占|夺|动|染指|踏入|支援)')
NOT_PROMISE = ('不能', '不用', '不必', '不得', '不如', '不管', '不论')
CODE = re.compile(r'\b([A-Z]{3})(?:/[NSE]C)?\b')
CLAUSE = re.compile(r'[^。；！？，,\n]+')
# 要求对方的话（"请确认你不进 X"）不算发言方自己的承诺
REQUEST = re.compile(r'^\s*(请|希望|要求|麻烦|能否|是否)|你[^我]{0,8}?(不会|不再|绝不|不)')
CONDITION = re.compile(r'如果|若|只要|除非|前提|条件')


def places(text):
    found = {m.group(1) for m in CODE.finditer(text)}
    found |= {code for name, code in CN.items() if name in text}
    return found


def hits(orders, place, recipient_units):
    """自己部队进攻该地，或支援收信方以外的部队进攻该地。"""
    out = []
    for o in orders:
        move = re.match(r'^[AF] [A-Z/]{3,6} - ([A-Z]{3})', o)
        support = re.match(r'^[AF] [A-Z/]{3,6} S ([AF] [A-Z]{3})\S* - ([A-Z]{3})', o)
        if move and move.group(1) == place:
            out.append(o)
        elif support and support.group(2) == place and support.group(1) not in recipient_units:
            out.append(o)
    return out


def check(state):
    history = [h for h in state['history'] if h['phase'].endswith('M')]
    index = {h['phase']: i for i, h in enumerate(history)}
    items = []
    for m in state['messages']:
        if m['phase'] not in index:
            continue
        i = index[m['phase']]
        for clause in CLAUSE.findall(m['text']):
            found = PROMISE.search(clause)
            if not found or REQUEST.search(clause) or clause[found.start():found.start() + 2] in NOT_PROMISE:
                continue
            head = clause[:found.start()]
            if any(name in head for p, name in NAMES.items() if p != m['sender']):
                continue  # 主语是别国（"德国不进勃艮第"）
            targets = places(clause[found.start():])
            if not targets:
                continue
            item = dict(phase=m['phase'], sender=m['sender'], to=m['to'], clause=clause.strip(), text=m['text'],
                        places=sorted(targets), conditional=bool(CONDITION.search(clause)), broken=[])
            for offset, label in ((0, '当季'), (1, '下一季')):
                if i + offset >= len(history):
                    continue
                h = history[i + offset]
                units = (h.get('before') or {}).get('units') or {}
                theirs = {u.lstrip('*') for t in m['to'] if t in units for u in units[t]}
                for place in sorted(targets):
                    for o in hits(h['orders'].get(m['sender'], []), place, theirs):
                        item['broken'].append(dict(phase=h['phase'], when=label, order=o))
            items.append(item)
    return items


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    a = argparse.ArgumentParser()
    a.add_argument('--match')
    o = a.parse_args()
    matches = ROOT / '管理员' / '对局'
    match = Path(o.match) if o.match else matches / (matches / '当前对局.txt').read_text(encoding='utf-8').strip()
    state = json.loads((match / 'state.json').read_text(encoding='utf-8'))
    labels = {p: v['label'] for p, v in json.loads((match / 'scheduler.json').read_text(encoding='utf-8'))['players'].items()}
    items = check(state)
    (match / '承诺核对.json').write_text(json.dumps(items, ensure_ascii=False, indent=1), encoding='utf-8')
    lines = [f'# 承诺核对 · {match.name}', '',
             '规则匹配初筛："不进/不攻/不碰某地"类承诺 vs 发言方当季、下一季的实际命令。有条件的承诺另行标注，结论以原文为准。', '',
             '| 国家 | 选手 | 承诺句数 | 疑似违约 |', '|---|---|---|---|']
    for p in NAMES:
        mine = [x for x in items if x['sender'] == p]
        if mine:
            lines.append(f'| {NAMES[p]} | {labels.get(p, "")} | {len(mine)} | {sum(1 for x in mine if x["broken"])} |')
    lines += ['', '## 疑似违约', '']
    for x in items:
        if not x['broken']:
            continue
        to = '、'.join(NAMES.get(t, t) for t in x['to'])
        lines.append(f'- **{x["phase"]} {NAMES[x["sender"]]}→{to}**{"（有条件）" if x["conditional"] else ""}：“{x["clause"]}”')
        for b in x['broken']:
            lines.append(f'  - {b["when"]} {b["phase"]}：`{b["order"]}`')
    (match / '承诺核对.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'找到 {len(items)} 句承诺，其中 {sum(1 for x in items if x["broken"])} 句疑似违约。详见 {match / "承诺核对.md"}')


if __name__ == '__main__':
    main()
