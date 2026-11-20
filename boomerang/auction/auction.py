"""纯拍卖状态机；询价接口只需实现 ask(captain, question)。"""
import hashlib
import json


def ceiling(team):
    return max(0, team['budget'] - (3 - len(team['members']) - 1)) if len(team['members']) < 3 else 0


class Auction:
    def __init__(self, candidates, captains, bidder, seed='AI赛事-2026'):
        if len(candidates) < 6 or len(captains) != 2:
            raise ValueError('必须至少 6 位候选和两位队长')
        if len({c['id'] for c in candidates}) != len(candidates) or len({c['id'] for c in captains}) != 2:
            raise ValueError('编号必须唯一')
        if len({c['rank'] for c in candidates}) != len(candidates):
            raise ValueError('海选名次不能重复')
        for c in candidates:
            if type(c['rank']) is not int or c['rank'] < 1 or 'summary' not in c:
                raise ValueError('候选需包含正整数名次与 summary')
        self.candidates = sorted(candidates, key=lambda c: c['rank'])
        self.teams = [dict(id=c['id'], name=c['name'], budget=1000, members=[]) for c in captains]
        self.bidder, self.seed, self.events = bidder, str(seed), []

    def decide(self, indices, candidate, tag):
        """预算优先、缺额优先、SHA256 固定种子决胜（不依赖随机数实现）。"""
        budgets = max(self.teams[i]['budget'] for i in indices)
        xs = [i for i in indices if self.teams[i]['budget'] == budgets]
        if len(xs) == 1: return xs[0], '剩余预算'
        gaps = max(3 - len(self.teams[i]['members']) for i in xs)
        xs = [i for i in xs if 3 - len(self.teams[i]['members']) == gaps]
        if len(xs) == 1: return xs[0], '缺额'
        winner = min(xs, key=lambda i: hashlib.sha256(json.dumps(
            [self.seed, candidate['id'], tag, self.teams[i]['id']], ensure_ascii=False,
            separators=(',', ':')).encode('utf-8')).hexdigest())
        return winner, '固定种子SHA256'

    def question(self, i, candidate, remaining, limit, round_no, mode):
        own, other = self.teams[i], self.teams[1-i]
        no_pass = len(remaining) + 1 == sum(3-len(t['members']) for t in self.teams)
        # 深复制，避免可替换接口意外修改状态；不包含对方本轮报价。
        return json.loads(json.dumps(dict(candidate=candidate, remaining_candidates=remaining,
            own=own, opponent=other, ceiling=limit, round=round_no, mode=mode,
            no_pass=no_pass,
            format={'出价': '0 到上限的整数（单方询价只允许 0 或 1）', '理由': '非空的一句话'},
            instruction='只返回 JSON 对象，不能有 Markdown 围栏。两次重答后仍无效按 0。' +
                ('本轮不允许流拍：照常暗标；双方均为0时，按余额、缺额、固定种子决胜，以1元成交。' if no_pass else '')), ensure_ascii=False))

    def bid(self, i, question):
        attempts = []
        for attempt in range(3):  # 首答 + 最多两次重答
            raw = None
            q = dict(question, retry=attempt)
            if attempts: q['validation_error'] = attempts[-1]['error']
            try:
                raw = self.bidder.ask(self.teams[i]['id'], q)
                answer = json.loads(raw) if isinstance(raw, str) else raw
                if not isinstance(answer, dict) or set(answer) != {'出价', '理由'}:
                    raise ValueError('必须且只能包含 出价、理由')
                price, reason = answer['出价'], answer['理由']
                if type(price) is not int or not 0 <= price <= question['ceiling']:
                    raise ValueError('出价必须是上限内的整数')
                if not isinstance(reason, str) or not reason.strip():
                    raise ValueError('理由不能为空')
                attempts.append(dict(raw=raw, valid=True))
                return dict(price=price, reason=reason, attempts=attempts)
            except Exception as e:
                attempts.append(dict(raw=raw, valid=False, error=str(e)))
        return dict(price=0, reason='两次重答后仍无效，按放弃', attempts=attempts)

    def award(self, i, c, price, reason):
        t = self.teams[i]
        assert len(t['members']) < 3 and 1 <= price <= ceiling(t)
        t['budget'] -= price
        t['members'].append(dict(candidate=c, price=price))
        self.events.append(dict(type='result', candidate=c['id'], winner=t['id'], price=price, reason=reason))

    def run(self):
        for pos, c in enumerate(self.candidates):
            remaining = self.candidates[pos:]
            active = [i for i, t in enumerate(self.teams) if len(t['members']) < 3]
            gaps = [3 - len(t['members']) for t in self.teams]
            if not active:
                self.events.append(dict(type='result', candidate=c['id'], winner=None, price=0, reason='两队已满'))
                continue
            forced = [i for i in active if len(active) == 1 and gaps[i] == len(remaining)]
            if forced:
                self.award(forced[0], c, 1, '剩余候选数等于该队缺额，强制成交')
                continue
            if len(active) == 1:
                i = active[0]
                b = self.bid(i, self.question(i, c, remaining[1:], 1, 1, '单方1元选择'))
                self.events.append(dict(type='bids', candidate=c['id'], round=1, bids={self.teams[i]['id']: b}))
                if b['price']: self.award(i, c, 1, '单方接受1元')
                else: self.events.append(dict(type='result', candidate=c['id'], winner=None, price=0, reason='单方放弃'))
                continue
            for r in range(4):  # 初拍 + 最多三次重新出价
                bids = [self.bid(i, self.question(i, c, remaining[1:], ceiling(self.teams[i]), r+1, '暗标')) for i in range(2)]
                self.events.append(dict(type='bids', candidate=c['id'], round=r+1,
                    bids={self.teams[i]['id']: bids[i] for i in range(2)}))
                a, b = (x['price'] for x in bids)
                if a == b == 0:
                    if len(remaining) == sum(gaps):
                        i, why = self.decide(active, c, '保底')
                        self.award(i, c, 1, '不允许流拍，双方0元后1元保底；' + why)
                    else:
                        self.events.append(dict(type='result', candidate=c['id'], winner=None, price=0, reason='双方放弃流拍'))
                    break
                if a != b:
                    i = 0 if a > b else 1
                    self.award(i, c, bids[i]['price'], '高价成交'); break
                if r == 3:
                    i, why = self.decide([0, 1], c, '平局')
                    self.award(i, c, bids[i]['price'], '三次重拍仍平局；' + why)
        bought = {x['candidate']['id'] for t in self.teams for x in t['members']}
        assert all(len(t['members']) == 3 and t['budget'] >= 0 for t in self.teams)
        return dict(seed=self.seed, rules=dict(budget=1000, seats=3, minimum=1, tie_rebids=3,
            invalid_retries=2, feasibility_guard='剩余候选等于总空缺时照常暗标但不允许流拍；双方0元才按余额、缺额、种子以1元成交；仅单方缺人且候选等于缺额时直接1元'),
            events=self.events, teams=[dict(t, spent=1000-t['budget']) for t in self.teams],
            rejected=[c for c in self.candidates if c['id'] not in bought])


def markdown(result):
    def safe(x): return str(x).replace('\\', '\\\\').replace('\n', '<br>').replace('|', '\\|').replace('<', '&lt;').replace('>', '&gt;')
    lines = ['# 拍卖记录', '', '种子：' + safe(result['seed']), '',
             '平局：初拍后最多三次重拍；仍平局依次比较剩余预算、缺额、固定种子 SHA256 最小值。',
             '保底：剩余候选等于总空缺时照常暗标但不允许流拍；双方0元才按余额、缺额、种子以1元成交。仅单方缺人且候选等于缺额时直接1元。', '',
             '| 候选 | 轮次/结果 | 队长 | 出价 | 理由/错误与重答 |', '|---|---|---|---:|---|']
    for e in result['events']:
        if e['type'] == 'bids':
            for who, b in e['bids'].items():
                details = b['reason'] + '；尝试：' + json.dumps(b['attempts'], ensure_ascii=False)
                lines.append(f"| {safe(e['candidate'])} | {e['round']} | {safe(who)} | {b['price']} | {safe(details)} |")
        else:
            lines.append(f"| {safe(e['candidate'])} | 成交/落选 | {safe(e['winner'])} | {e['price']} | {safe(e['reason'])} |")
    lines += ['', '## 最终名单', '']
    for t in result['teams']:
        lines.append(f"- {safe(t['name'])}：花费 {t['spent']}，余额 {t['budget']}；" + '、'.join(safe(x['candidate']['id']) + f"（{x['price']}元）" for x in t['members']))
    lines += ['- 落选：' + '、'.join(safe(c['id']) for c in result['rejected'])]
    return '\n'.join(lines) + '\n'
