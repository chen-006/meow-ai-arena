"""比赛进行中巡检手动席位的会话记录（Claude Code、Codex）：进度、是否被安全机制拦截、有没有越界。只读。

  python 裁判/巡检.py --round 2

检查：子代理 / 联网工具、按名字批量结束进程、访问比赛文件夹以外的路径（出题目录、别的比赛文件夹、存档）、
安全拦截字样、编译失败。会话按工作目录里的"第N轮重赛 队X"匹配，取最新的一个。
"""
import argparse
import glob
import json
import os
import re
import sys
import time
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from 赛事工具 import ROUND_LABEL, contest_folder, player, teams  # noqa: E402

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')
HOME = os.path.expanduser('~')
KILL = re.compile(r'taskkill\s+/f\s+/im|taskkill\s+/im|stop-process\s+-name|get-process\s+[\w,\*]+\s*\|\s*stop-process|pkill|killall', re.I)
OUTSIDE = re.compile(r'Desktop|裁判|隐藏负载|参考实现|存档|第1轮 队|运行记录|\.claude[/\\]+projects|\.codex[/\\]+sessions', re.I)
BLOCK = re.compile(r'cyber_policy|flagged for possible|usage policy|无法协助|不能协助|I can.t help with|unable to assist', re.I)
SUBAGENT = {'Agent', 'Task', 'WebSearch', 'WebFetch', 'spawn_agent', 'web_search'}


def loc(ts):
    return datetime.fromisoformat(ts.replace('Z', '+00:00')).astimezone().strftime('%H:%M:%S')


def claude_session(folder):
    best = None
    for f in glob.glob(os.path.join(HOME, '.claude', 'projects', '*', '*.jsonl')):
        if time.time() - os.path.getmtime(f) > 6 * 3600:
            continue
        try:
            with open(f, encoding='utf-8') as fh:
                head = [json.loads(next(fh)) for _ in range(3)]
        except (StopIteration, ValueError):
            continue
        if any(os.path.normcase(str(r.get('cwd', ''))) == os.path.normcase(folder) for r in head):
            if best is None or os.path.getmtime(f) > os.path.getmtime(best):
                best = f
    return best


def codex_session(folder):
    best = None
    for f in glob.glob(os.path.join(HOME, '.codex', 'sessions', '*', '*', '*', '*.jsonl')):
        if time.time() - os.path.getmtime(f) > 6 * 3600:
            continue
        try:
            with open(f, encoding='utf-8') as fh:
                first = json.loads(fh.readline())
        except ValueError:
            continue
        p = first.get('payload', {})
        if os.path.normcase(str(p.get('cwd', ''))) == os.path.normcase(folder) and p.get('source') in ('vscode', 'cli', 'exec', None):
            if best is None or os.path.getmtime(f) > os.path.getmtime(best):
                best = f
    return best


def scan(f, own_folder):
    calls, alarms, last_say, first_ts, last_ts, done = 0, [], '', None, None, False
    own = os.path.basename(own_folder)
    for line in open(f, encoding='utf-8'):
        try:
            r = json.loads(line)
        except ValueError:
            continue
        ts = r.get('timestamp')
        if ts:
            first_ts, last_ts = first_ts or ts, ts
        p = r.get('payload') or {}
        m = r.get('message') or {}
        cmds, says, name = [], [], None
        if isinstance(m.get('content'), list):          # Claude Code
            for x in m['content']:
                if x.get('type') == 'tool_use':
                    name = x.get('name')
                    cmds.append(json.dumps(x.get('input'), ensure_ascii=False))
                    if name in SUBAGENT:
                        alarms.append('%s 调用 %s' % (loc(ts), name))
                elif x.get('type') == 'text' and r.get('type') == 'assistant':
                    says.append(x['text'])
        if p.get('type') in ('custom_tool_call', 'function_call', 'local_shell_call'):   # Codex
            cmds.append(str(p.get('input') or p.get('arguments') or p.get('action')))
        if p.get('type') == 'message' and p.get('role') == 'assistant':
            says.append(' '.join(c.get('text', '') for c in p.get('content', []) if isinstance(c, dict)))
        if p.get('type') in ('task_complete', 'turn_aborted', 'error', 'stream_error'):
            done = p.get('type')
            if p.get('type') != 'task_complete':
                alarms.append('%s %s %s' % (loc(ts), p.get('type'), json.dumps(p, ensure_ascii=False)[:160]))
        if r.get('isApiErrorMessage'):
            alarms.append('%s API 报错 %s' % (loc(ts), json.dumps(m, ensure_ascii=False)[:160]))
        for c in cmds:
            calls += 1
            if KILL.search(c):
                alarms.append('%s 按名字结束进程：%s' % (loc(ts), KILL.search(c).group(0)))
            for hit in OUTSIDE.finditer(c):
                ctx = c[max(0, hit.start() - 40):hit.end() + 40]
                if own in ctx and hit.group(0) not in ('Desktop',):
                    continue
                if 'tool-results' in c[hit.start():hit.end() + 160]:
                    continue    # Claude Code 把过长的命令输出存到本会话自己的 tool-results 里再读回来，不算越界
                alarms.append('%s 可能越界（%s）：…%s…' % (loc(ts), hit.group(0), ctx.replace('\\\\', '\\')[:110]))
                break
        for s in says:
            if s.strip():
                last_say = s.strip()
            if BLOCK.search(s):
                alarms.append('%s 疑似被拦截：%s' % (loc(ts), s[:120]))
    return dict(calls=calls, alarms=alarms, last=last_say, first=first_ts, last_ts=last_ts, done=done)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--round', type=int, default=2)
    rnd = ap.parse_args().round
    print('巡检 %s  %s' % (ROUND_LABEL[rnd], datetime.now().strftime('%H:%M:%S')))
    for t in teams():
        model = player(t, '攻手', rnd)
        folder = contest_folder(t['team'], '攻手', model, rnd)
        f = claude_session(folder) or codex_session(folder)
        sub = 'review' if rnd == 2 else 'workspace'
        cases = [os.path.join(r, x) for r, _, fs in os.walk(os.path.join(folder, sub, 'cases')) for x in fs if x.endswith('.in')]
        per = {}
        for c in cases:
            per[os.path.basename(os.path.dirname(c))] = per.get(os.path.basename(os.path.dirname(c)), 0) + 1
        extra = ('用例 ' + (' '.join('%s:%d' % kv for kv in sorted(per.items())) or '0')) if rnd == 2 else ''
        if not f:
            print('%s %-12s 没找到会话记录（命令行席位看 进度.py） %s' % (t['team'], model, extra))
            continue
        s = scan(f, folder)
        idle = time.time() - os.path.getmtime(f)
        print('%s %-12s 工具调用 %3d | 最近活动 %3.0f 秒前%s | %s' % (
            t['team'], model, s['calls'], idle, '（已结束：%s）' % s['done'] if s['done'] else '', extra))
        print('      最近说：%s' % s['last'].replace('\n', ' ')[:150])
        for a in s['alarms'][:6]:
            print('      !!! ' + a)


if __name__ == '__main__':
    main()
