"""观战窗口：实时显示七国选手每次唤醒里的思考、命令、结果和回复，以及调度器的结算记录。只读，不影响比赛。

  python 观战.py [--match 对局目录] [--full]     --full 显示完整内容（默认每条截断）
"""
import argparse
import json
import os
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parent
COLORS = dict(AUSTRIA=31, ENGLAND=34, FRANCE=36, GERMANY=90, ITALY=32, RUSSIA=35, TURKEY=33)
NAMES = dict(AUSTRIA='奥地利', ENGLAND='英国', FRANCE='法国', GERMANY='德国', ITALY='意大利', RUSSIA='俄国', TURKEY='土耳其')


def events(harness, e):
    """把一行原始输出转成 (类别, 文字) 列表。"""
    out = []
    if harness == 'claude':
        if e.get('type') == 'assistant':
            for c in e['message'].get('content', []):
                if c.get('type') == 'thinking' and c.get('thinking'):
                    out.append(('思考', c['thinking']))
                elif c.get('type') == 'text':
                    out.append(('回复', c['text']))
                elif c.get('type') == 'tool_use':
                    out.append(('命令', c['input'].get('command') or json.dumps(c['input'], ensure_ascii=False)))
        elif e.get('type') == 'user':
            for c in e['message'].get('content', []):
                if isinstance(c, dict) and c.get('type') == 'tool_result':
                    r = c.get('content')
                    out.append(('结果', r if isinstance(r, str) else ' '.join(x.get('text', '') for x in r or [])))
    elif harness == 'codex':
        it = e.get('item') or {}
        if e.get('type') == 'item.completed':
            if it.get('type') == 'reasoning':
                out.append(('思考', it.get('text', '')))
            elif it.get('type') == 'agent_message':
                out.append(('回复', it.get('text', '')))
            elif it.get('type') == 'command_execution':
                out.append(('命令', it.get('command', '')))
                out.append(('结果', it.get('aggregated_output', '')))
    elif harness == 'opencode':
        p = e.get('part') or {}
        if e.get('type') == 'reasoning' or p.get('type') == 'reasoning':
            out.append(('思考', p.get('text', '')))
        elif e.get('type') == 'text':
            out.append(('回复', p.get('text', '')))
        elif e.get('type') == 'tool_use':
            st = p.get('state') or {}
            inp = st.get('input') or {}
            out.append(('命令', inp.get('command') or json.dumps(inp, ensure_ascii=False)))
            out.append(('结果', str(st.get('output', ''))))
    return [(k, v.strip()) for k, v in out if v and v.strip()]


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    os.system('')  # 打开 Windows 控制台的颜色支持
    a = argparse.ArgumentParser()
    a.add_argument('--match')
    a.add_argument('--full', action='store_true')
    args = a.parse_args()
    matches = ROOT / '管理员' / '对局'
    match = Path(args.match) if args.match else matches / (matches / '当前对局.txt').read_text(encoding='utf-8').strip()
    sched = json.loads((match / 'scheduler.json').read_text(encoding='utf-8'))
    players = sched['players']
    print(f'观战：{match.name}（只读）')
    for power, p in players.items():
        print(f'\x1b[{COLORS[power]}m  {NAMES[power]} = {p["label"]}\x1b[0m')
    print()
    offsets = {}
    log = match / '调度.log'
    offsets[log] = 0
    while True:
        files = sorted((match / '唤醒日志').glob('*/*.out.jsonl'), key=lambda f: f.stat().st_mtime)
        for f in [log] + files:
            if not f.exists():
                continue
            pos = offsets.get(f, 0)
            if f.stat().st_size <= pos:
                continue
            with f.open('rb') as fh:
                fh.seek(pos)
                data = fh.read()
            end = data.rfind(b'\n') + 1
            if not end:
                continue
            offsets[f] = pos + end
            for line in data[:end].decode('utf-8', errors='replace').splitlines():
                if f == log:
                    if '结算' in line or '警报' in line or '到达' in line or '结束' in line:
                        print(f'\x1b[1m{line}\x1b[0m')
                    continue
                power = f.parent.name
                try:
                    e = json.loads(line)
                except ValueError:
                    continue
                for kind, text in events(players[power]['harness'], e):
                    if not args.full:
                        limit = 300 if kind in ('回复', '思考') else 160
                        text = text if len(text) <= limit else text[:limit] + '…'
                    text = text.replace('\r', '').replace('\n', '\n      ')
                    dim = '\x1b[2m' if kind in ('结果', '思考') else ''
                    print(f'\x1b[{COLORS[power]}m{NAMES[power]}·{kind}\x1b[0m {dim}{text}\x1b[0m')
        time.sleep(1)


if __name__ == '__main__':
    main()
