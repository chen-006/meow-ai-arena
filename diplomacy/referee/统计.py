"""对局统计：局势、补给中心变化、各家唤醒耗时、用量与额度、最近的外交消息。只读。

  python 统计.py [--match 对局目录] [--messages 条数]
"""
import argparse
from collections import defaultdict
from datetime import datetime
import glob
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent
NAMES = dict(AUSTRIA='奥地利', ENGLAND='英国', FRANCE='法国', GERMANY='德国', ITALY='意大利', RUSSIA='俄国', TURKEY='土耳其')


def session_total(values, start=0):
    """Claude 报的花费是会话累计值：逐次取增量相加（数值变小说明换了会话，从头累计）。"""
    total, prev = 0, start or 0  # 续局沿用上一局的会话，从上一局结束时的累计值算起
    for v in values:
        if v is None:
            continue
        total += v - prev if v >= prev else v
        prev = v
    return total


def codex_quota(match):
    first = last = None
    for f in sorted(glob.glob(str(match / 'codex_home' / 'sessions' / '**' / '*.jsonl'), recursive=True)):
        for line in open(f, encoding='utf-8'):
            if '"rate_limits"' in line:
                e = json.loads(line)
                rl = (e.get('payload') or {}).get('rate_limits')
                if rl and rl.get('primary'):
                    if not last or e['timestamp'] > last[0]:
                        last = (e['timestamp'], rl['primary'])
                    if not first or e['timestamp'] < first[0]:
                        first = (e['timestamp'], rl['primary'])
    return last and (last[0], last[1], first[1])


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    a = argparse.ArgumentParser()
    a.add_argument('--match')
    a.add_argument('--messages', type=int, default=8)
    o = a.parse_args()
    matches = ROOT / '管理员' / '对局'
    match = Path(o.match) if o.match else matches / (matches / '当前对局.txt').read_text(encoding='utf-8').strip()
    state = json.loads((match / 'state.json').read_text(encoding='utf-8'))
    sched = json.loads((match / 'scheduler.json').read_text(encoding='utf-8'))
    players = sched['players']
    wakes = [json.loads(l) for l in (match / '唤醒记录.jsonl').read_text(encoding='utf-8').splitlines()] \
        if (match / '唤醒记录.jsonl').exists() else []

    history = state['history']
    phase = history[-1]['phase'] if history else 'S1901M'
    result = state.get('result')
    status = '已结束' if result else ('已冻结' if state.get('paused') else '进行中')
    print(f'# {match.name}  最近结算 {phase}  {status}')
    if result:
        ranked = sorted((result.get('scores') or {}).items(), key=lambda kv: -kv[1])
        print('  结果：', result.get('reason'), '  得分：', '；'.join(f'{NAMES[p]} {v:g}' for p, v in ranked if v))
    if wakes:
        t0 = datetime.fromisoformat(wakes[0]['start'])
        t1 = datetime.fromisoformat(max(w.get('end', w['start']) for w in wakes))
        years = max(1, int(phase[1:5]) - 1900)
        print(f'  已用时 {(t1 - t0).total_seconds() / 60:.0f} 分钟，约 {(t1 - t0).total_seconds() / 60 / years:.0f} 分钟/年（含冻结时间）')

    print('\n## 补给中心（每年冬季后）')
    powers = list(players)
    print('年份  ' + ' '.join(f'{NAMES[p]:>4}' for p in powers))
    for h in history:
        if h['phase'].startswith('W') or h is history[-1]:
            print(f'{h["phase"]:6}' + ' '.join(f'{len(h["centers"].get(p, [])):>5}' for p in powers))

    print('\n## 各家唤醒与用量')
    by = defaultdict(list)
    for w in wakes:
        by[w['power']].append(w)
    for power, p in players.items():
        ws = by.get(power, [])
        secs = [w.get('seconds') or 0 for w in ws]
        recent = secs[-20:]
        fails = sum(1 for w in ws if not w.get('ok'))
        base = p.get('usage_base') or {}
        line = (f'{NAMES[power]:4} {p["label"]:22} 唤醒 {len(ws):4} 次  失败 {fails:3}  '
                f'平均 {sum(secs) / max(1, len(secs)):6.1f} 秒  最近20次平均 {sum(recent) / max(1, len(recent)):6.1f}  '
                f'最长 {max(secs or [0]):6.1f}')
        if p['harness'] == 'claude':
            line += f'  API折算 ${session_total((w.get("cost_usd") for w in ws), base.get('cost_usd')):.2f}'
        elif p['harness'] == 'codex':
            u = next((w['usage'] for w in reversed(ws) if w.get('usage')), None)
            b = base.get('usage') or {}
            if u:
                u = {k: u.get(k, 0) - b.get(k, 0) for k in ('input_tokens', 'cached_input_tokens', 'output_tokens')}
                line += f'  累计输入 {u["input_tokens"] / 1e6:.2f}M（缓存 {u["cached_input_tokens"] / max(1, u["input_tokens"]):.0%}）输出 {u["output_tokens"] / 1e3:.0f}k'
        elif p['harness'] == 'opencode':
            cost = sum((w.get('usage') or {}).get('cost_usd') or 0 for w in ws)  # 调度器记录的已是每次增量
            line += f'  实际花费 ${cost:.3f}'
        if p.get('model_history'):
            line += '  （换人：' + ' → '.join(h.get('label') or h['model'] for h in p['model_history']) + f' → {p["label"]}）'
        print(line)
    quota = codex_quota(match)
    if quota:
        used, start = quota[1].get('used_percent') or 0, quota[2].get('used_percent') or 0
        print(f'  Codex 额度：本局约消耗 {used - start:g}%（{quota[1].get("window_minutes", 0) // 1440} 天窗口开局时 {start:g}%，'
              f'现在 {used:g}%，含你在别处的使用；UTC {quota[0][:16]}）')

    if o.messages:
        print(f'\n## 最近 {o.messages} 条外交消息（共 {len(state["messages"])} 条）')
        for m in state['messages'][-o.messages:]:
            to = ','.join(NAMES.get(t, t) for t in m['to'])
            print(f'{m["phase"]} {NAMES[m["sender"]]}→{to}：{m["text"]}')


if __name__ == '__main__':
    main()
