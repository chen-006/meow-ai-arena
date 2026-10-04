"""管理员操作当前对局（或 --match 指定的对局）。

  admin.py status                     查看阶段、各国交令情况
  admin.py pause / resume             冻结 / 恢复（调度器继续运行）
  admin.py stop                       冻结，等选手进程停下后调度器退出；之后 match.py run 续跑（自动解冻）
  admin.py swap 国家 模型 [--effort 档位] [--label 名字]
                                      对局停止时给某国换模型（同一种 Agent 内），保留原会话接着打
  admin.py notice "通知内容"           对局停止时登记一条管理员通知，续跑后随各国下一次唤醒原样送达一次
"""
import argparse
import json
from pathlib import Path
import socket
import sys
import urllib.request

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
from scheduler import CONTEXT_WINDOW  # noqa: E402


def request(match, action, reason):
    config = json.loads((match / 'admin.json').read_text(encoding='utf-8'))
    body = None if action == 'status' else json.dumps(dict(action=action, reason=reason)).encode()
    req = urllib.request.Request(config['url'] + '/admin', data=body,
        headers={'Authorization': 'Bearer ' + config['token'], 'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def running(match):
    port = json.loads((match / 'scheduler.json').read_text(encoding='utf-8'))['port']
    try:
        socket.create_connection(('127.0.0.1', port), timeout=1).close()
        return True
    except OSError:
        return False


def swap(match, power, model, effort, label):
    if running(match):
        raise SystemExit('对局正在运行，先 admin.py stop，等调度器退出后再换。')
    power = power.upper()
    path = match / 'scheduler.json'
    sched = json.loads(path.read_text(encoding='utf-8'))
    if power not in sched['players']:
        raise SystemExit('没有这个国家：' + power)
    p = sched['players'][power]
    state = json.loads((match / 'state.json').read_text(encoding='utf-8'))
    phase = state['history'][-1]['phase'] if state['history'] else 'S1901M'
    p.setdefault('model_history', []).append(dict(model=p['model'], effort=p.get('effort'), label=p.get('label'),
                                                  until=phase))
    p.update(model=model, effort=effort, label=label or model, failures=0)
    p.pop('retry_at', None)
    p.pop('quota_since', None)
    if p['harness'] == 'opencode':  # 新模型也要写明上下文长度，才会按统一窗口自动压缩
        ws = Path(p['workspace']) / 'opencode.json'
        config = json.loads(ws.read_text(encoding='utf-8'))
        provider, name = model.split('/', 1)
        config['provider'] = {provider: {'models': {name: {'limit': dict(context=CONTEXT_WINDOW, output=32000)}}}}
        ws.write_text(json.dumps(config, indent=2), encoding='utf-8')
    path.write_text(json.dumps(sched, ensure_ascii=False, indent=2), encoding='utf-8')
    roster_path = match / '名单.json'
    roster = json.loads(roster_path.read_text(encoding='utf-8'))
    for r in roster['players']:
        if r['power'] == power:
            r.setdefault('换人记录', []).append(dict(旧=r.get('label', r['model']), 新=p['label'], 结算至=phase))
            r.update(model=model, effort=effort, label=p['label'])
    roster_path.write_text(json.dumps(roster, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'{power} 已换成 {p["label"]}（{model}，{effort or "默认档位"}），{phase} 之后生效，沿用原会话。')


def notice(match, text):
    if running(match):
        raise SystemExit('请先 admin.py stop，等调度器退出后再登记通知。')
    path = match / 'scheduler.json'
    sched = json.loads(path.read_text(encoding='utf-8'))
    state = json.loads((match / 'state.json').read_text(encoding='utf-8'))
    out = state.get('eliminated') or []
    sched['notice'] = text
    for power, p in sched['players'].items():
        if power not in out:
            p['notice_pending'] = True
    path.write_text(json.dumps(sched, ensure_ascii=False, indent=2), encoding='utf-8')
    print('已登记管理员通知，续跑后随各国下一次唤醒送达：\n' + text)


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    a = argparse.ArgumentParser()
    a.add_argument('action', choices=['status', 'pause', 'resume', 'stop', 'swap', 'notice'])
    a.add_argument('args', nargs='*')
    a.add_argument('--reason', default='')
    a.add_argument('--match')
    a.add_argument('--effort')
    a.add_argument('--label')
    o = a.parse_args()
    matches = ROOT / '管理员' / '对局'
    match = Path(o.match) if o.match else matches / (matches / '当前对局.txt').read_text(encoding='utf-8').strip()
    if o.action == 'swap':
        if len(o.args) != 2:
            raise SystemExit('用法：admin.py swap 国家 模型 [--effort 档位] [--label 名字]')
        return swap(match, o.args[0], o.args[1], o.effort, o.label)
    if o.action == 'notice':
        if len(o.args) != 1:
            raise SystemExit('用法：admin.py notice "通知内容"')
        return notice(match, o.args[0])
    if not running(match):
        raise SystemExit('对局没有在运行（裁判未启动）。续跑用 match.py run。')
    s = request(match, o.action, o.reason)
    print(f"{s['phase']}  {'已冻结' if s['paused'] else '进行中'}{'  已结束' if s['game_over'] else ''}"
          + ('  调度器将在选手停下后退出' if s.get('stop_requested') else ''))
    for power, v in s['players'].items():
        if v['active']:
            print(f"  {power:8} {'需行动' if v['required'] else '      '} {'已交令' if v['ready'] else ''}")


if __name__ == '__main__':
    main()
