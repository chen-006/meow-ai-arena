"""对局管理。

  python match.py new --roster 名单.json      建新局：裁判存档、七国工作区、调度名单
  python match.py rematch --prev 上一局目录      同一批选手带着记忆再打一局（国家重新抽签）
  python match.py run                          运行当前对局（裁判 + 调度器），可随时 Ctrl+C 后再次 run 续跑
"""
import argparse
from datetime import datetime
import json
import random
from pathlib import Path
import shutil
import socket
import sys
import threading

from arena import Arena, POWERS, make_server
from scheduler import CONTEXT_WINDOW, Court, Scheduler

ROOT = Path(__file__).resolve().parent
MATCHES = ROOT / '管理员' / '对局'
CURRENT = MATCHES / '当前对局.txt'
PLAYER_FILES = [('client.py', 'game.py'), ('simulate.py', 'simulate.py'), ('RULES.md', 'RULES.md'),
                ('选手操作.md', 'README.md')]
REFERENCE = ['地图索引.md', '地图邻接.json', '官方完整规则.pdf']
OPENCODE_PERMISSION = {'$schema': 'https://opencode.ai/config.json',
                       'permission': {'edit': 'allow', 'bash': 'allow', 'webfetch': 'deny',
                                      'websearch': 'deny', 'external_directory': 'deny'}}


DRAW_RULE = '- 春、秋移动阶段可以表决协议和局'


def setup_workspace(ws, p, token, port, draw_from=None):
    """放入（或刷新）规则、说明、客户端、地图资料和本局连接信息。工作区里选手自己的文件不动。"""
    for src, dst in PLAYER_FILES:
        if dst.endswith('.md'):  # 带 BOM：Windows PowerShell 5.1 的 Get-Content 才能正确识别 UTF-8
            text = (ROOT / src).read_text(encoding='utf-8')
            if draw_from and dst == 'RULES.md':
                assert DRAW_RULE in text
                text = text.replace(DRAW_RULE, f'- 本局 **{draw_from} 年春季起**，春、秋移动阶段才可以表决协议和局')
            (ws / dst).write_text(text, encoding='utf-8-sig')
        else:
            shutil.copyfile(ROOT / src, ws / dst)
    (ws / '资料').mkdir(exist_ok=True)
    for f in REFERENCE:
        if not (ROOT / '资料' / f).exists():  # 开源版不附带官方规则 PDF（版权属于原出版方）
            continue
        if f.endswith('.md'):
            (ws / '资料' / f).write_text((ROOT / '资料' / f).read_text(encoding='utf-8-sig'), encoding='utf-8-sig')
        else:
            shutil.copyfile(ROOT / '资料' / f, ws / '资料' / f)
    (ws / 'connection.json').write_text(json.dumps(dict(url=f'http://127.0.0.1:{port}', token=token,
        sim_python=sys.executable)), encoding='utf-8')  # 推演要用装了 diplomacy 包的解释器，即运行本程序的这个
    if p['harness'] == 'opencode':
        # 写明模型上下文长度：不写时 OpenCode 不知道何时该压缩；超过统一窗口的按统一窗口算
        provider, model = p['model'].split('/', 1)
        limit = dict(context=min(p.get('context', CONTEXT_WINDOW), CONTEXT_WINDOW), output=p.get('max_output', 32000))
        config = dict(OPENCODE_PERMISSION, compaction={'auto': True},
                      provider={provider: {'models': {model: {'limit': limit}}}})
        (ws / 'opencode.json').write_text(json.dumps(config, indent=2), encoding='utf-8')


def new(roster_path, root, port, matches=MATCHES):
    roster = json.loads(Path(roster_path).read_text(encoding='utf-8'))
    players = {p['power']: p for p in roster['players']}
    if set(players) != set(POWERS):
        raise SystemExit('名单必须恰好包含七个国家：' + ', '.join(POWERS))
    for p in players.values():
        if p['harness'] not in ('claude', 'codex', 'opencode', 'script'):
            raise SystemExit('未知 harness：' + p['harness'])
    name = datetime.now().strftime('%Y%m%d_%H%M')
    match_dir = Path(matches) / name
    if match_dir.exists():
        raise SystemExit('对局目录已存在：' + str(match_dir))
    arena = Arena.create(match_dir / 'state.json')
    workspace_root = Path(root) / name
    sched = dict(match_id=arena.data['match_id'], port=port,
                 wake_timeout_minutes=roster.get('wake_timeout_minutes', 45), players={})
    for power in POWERS:
        ws = workspace_root / power
        ws.mkdir(parents=True)
        setup_workspace(ws, players[power], arena.data['tokens'][power], port)
        p = players[power]
        sched['players'][power] = dict(harness=p['harness'], model=p['model'], effort=p.get('effort'),
            label=p.get('label', p['model']), workspace=str(ws), session_id=None,
            msg_cursor=0, hist_cursor=0, wakes=0)
    (match_dir / 'scheduler.json').write_text(json.dumps(sched, ensure_ascii=False, indent=2), encoding='utf-8')
    (match_dir / '名单.json').write_text(json.dumps(roster, ensure_ascii=False, indent=2), encoding='utf-8')
    (Path(matches) / '当前对局.txt').write_text(name, encoding='utf-8')
    print(f'已建新局 {name}\n  管理员目录：{match_dir}\n  选手工作区：{workspace_root}')
    return match_dir


CN = dict(AUSTRIA='奥地利', ENGLAND='英国', FRANCE='法国', GERMANY='德国', ITALY='意大利', RUSSIA='俄国', TURKEY='土耳其')
REASONS = dict(victory='单独获胜', agreed_draw='全体同意协议和局', turn_limit='打满 1920 年到期和局')
REMATCH_INTRO = (
    '【第 {number} 局{final}】上一局已经结束：{prev}\n'
    '现在开始新的一局。七位玩家与之前完全相同，但国家已重新随机抽签：你这局代表 {{power}}，'
    '不知道其他玩家这局各在哪国。你对之前各局的记忆和留下的笔记都可以利用；上一局的约定、命令和消息不再有效。{changes}'
    '你的工作目录沿用之前的（目录名是你第一局的国家，与本局身份无关），规则、说明、客户端和连接信息已更新为本局。\n\n')
FINAL_NOTE = '，也是最后一局'
FINAL_CHANGES = '这是最后一局，之后不会再有对局。'


def rematch(prev_dir, port, seed=None, matches=MATCHES, draw_from=None, final=False, avoid_seats=True):
    """同一批选手带着之前的会话记忆再打一局：国家重新抽签，各自沿用原工作目录和会话原地续接。
    （OpenCode 续接会话时总回到会话最初的目录，所以工作目录不能换。）
    avoid_seats：每个模型不再抽到它在之前各局坐过的国家。"""
    prev_dir = Path(prev_dir).resolve()
    prev = json.loads((prev_dir / 'scheduler.json').read_text(encoding='utf-8'))
    prev_state = json.loads((prev_dir / 'state.json').read_text(encoding='utf-8'))
    result = prev_state.get('result')
    if not result:
        raise SystemExit('上一局还没结束。')
    seed = seed if seed is not None else random.SystemRandom().randrange(2 ** 32)
    olds = [dict(v, old_power=k, past_powers=sorted(set(v.get('past_powers') or [v.get('previous_power')]) - {None} | {k}))
            for k, v in prev['players'].items()]
    rng = random.Random(seed)
    for _ in range(100000):
        rng.shuffle(olds)
        if not avoid_seats or all(power not in o['past_powers'] for power, o in zip(POWERS, olds)):
            break
    else:
        raise SystemExit('找不到让每个模型都避开旧座位的抽签结果。')
    number = prev.get('game_number', 1 + bool(prev.get('rematch_of'))) + 1
    last = {}
    for line in (prev_dir / '唤醒记录.jsonl').read_text(encoding='utf-8').splitlines():
        w = json.loads(line)
        if w.get('ok'):
            last[w['power']] = w
    name = datetime.now().strftime('%Y%m%d_%H%M')
    match_dir = Path(matches) / name
    if match_dir.exists():
        raise SystemExit('对局目录已存在：' + str(match_dir))
    arena = Arena.create(match_dir / 'state.json', draw_from=draw_from)
    if (prev_dir / 'codex_home').exists():  # Codex 会话存放在每局独立的 CODEX_HOME 里，整体带过来
        shutil.copytree(prev_dir / 'codex_home', match_dir / 'codex_home')
    scores = sorted(result.get('scores', {}).items(), key=lambda kv: -kv[1])
    prev_text = (f'{result["phase"]} {REASONS.get(result["reason"], result["reason"])}。得分：'
                 + '、'.join(f'{CN[k]} {v:g}' for k, v in scores) + '。')
    changes = (f'本局规则有一处变化：{draw_from} 年春季之前不能表决协议和局。' if draw_from else '规则与计分不变。')
    intro = REMATCH_INTRO.format(number=number, final=FINAL_NOTE if final else '', prev=prev_text,
                                 changes=changes + (FINAL_CHANGES if final else ''))
    sched = dict(match_id=arena.data['match_id'], port=port, wake_timeout_minutes=prev.get('wake_timeout_minutes', 45),
                 rematch_of=prev_dir.name, game_number=number, final=final, draw_from=draw_from, seed=seed,
                 intro=intro, players={})
    roster = dict(rematch_of=prev_dir.name, game_number=number, seed=seed, players=[])
    for power, o in zip(POWERS, olds):
        ws = Path(o['workspace'])
        setup_workspace(ws, o, arena.data['tokens'][power], port, draw_from)
        w = last.get(o['old_power'], {})
        base = dict(cost_usd=w.get('cost_usd') if o['harness'] == 'claude' else None,
                    usage=w.get('usage') if o['harness'] == 'codex' else None)
        sched['players'][power] = dict(harness=o['harness'], model=o['model'], effort=o.get('effort'), label=o['label'],
            workspace=str(ws), session_id=o['session_id'], session_ready=True, previous_power=o['old_power'],
            past_powers=o['past_powers'], usage_base=base, msg_cursor=0, hist_cursor=0, wakes=0)
        if o.get('usage_total'):  # OpenCode 按会话累计值求每次增量，接着上一局的累计值算
            sched['players'][power]['usage_total'] = o['usage_total']
        roster['players'].append(dict(power=power, harness=o['harness'], model=o['model'], effort=o.get('effort'),
                                      label=o['label'], previous_power=o['old_power'], past_powers=o['past_powers']))
    (match_dir / 'scheduler.json').write_text(json.dumps(sched, ensure_ascii=False, indent=2), encoding='utf-8')
    (match_dir / '名单.json').write_text(json.dumps(roster, ensure_ascii=False, indent=2), encoding='utf-8')
    (Path(matches) / '当前对局.txt').write_text(name, encoding='utf-8')
    print(f'已建续局 {name}（接 {prev_dir.name}，抽签种子 {seed}）')
    for x in roster['players']:
        print(f'  {x["power"]:8} {x["label"]:22} 之前坐过 {"、".join(x["past_powers"])}')
    return match_dir


def run(match_dir, until_pause=False, stop_at=None):
    match_dir = Path(match_dir).resolve()  # 选手进程在各自工作区运行，传给它们的路径必须是绝对路径
    sched = json.loads((match_dir / 'scheduler.json').read_text(encoding='utf-8'))
    port = sched['port']
    try:
        socket.create_connection(('127.0.0.1', port), timeout=1).close()
        raise SystemExit(f'端口 {port} 已被占用，可能已有裁判在运行。')
    except OSError:
        pass
    arena = Arena(match_dir / 'state.json')
    server = make_server(arena, port)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    (match_dir / 'admin.json').write_text(json.dumps(dict(url=f'http://127.0.0.1:{port}',
        token=arena.data['admin_token'])), encoding='utf-8')
    def log(text):
        line = f'[{datetime.now():%H:%M:%S}] {text}'
        print(line, flush=True)
        with (match_dir / '调度.log').open('a', encoding='utf-8') as f:
            f.write(line + '\n')
    if arena.data['paused'] and not arena.data.get('result'):
        log('对局处于冻结状态，重新运行即自动解冻。')
        arena.control('resume', '重新运行')
    scheduler = Scheduler(match_dir, Court(f'http://127.0.0.1:{port}', arena.data['admin_token']), log)
    try:
        result = scheduler.run(until_pause, stop_at)
        log('结果：' + arena.result_text())
        return result
    except KeyboardInterrupt:
        log('手动停止：冻结对局并结束选手进程。')
        arena.control('pause', '管理员手动停止')
        scheduler.stop_all()
    finally:
        server.shutdown()


def main():
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding='utf-8')
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest='cmd', required=True)
    n = sub.add_parser('new')
    n.add_argument('--roster', required=True)
    n.add_argument('--root', required=True, help=r'选手工作区的上级目录，不要放在本目录里（比赛时用的是 C:\bench\外交）')
    n.add_argument('--port', type=int, default=18790)
    m = sub.add_parser('rematch', help='同一批选手带着上一局的记忆再打一局，国家重新抽签')
    m.add_argument('--prev', required=True, help='上一局的对局目录')
    m.add_argument('--port', type=int, default=18790)
    m.add_argument('--seed', type=int)
    m.add_argument('--draw-from', type=int, help='该年春季起才能表决协议和局，例如 1910')
    m.add_argument('--final', action='store_true', help='告诉选手这是最后一局')
    r = sub.add_parser('run')
    r.add_argument('--match')
    r.add_argument('--stop-at', help='到达该阶段时自动冻结，例如 S1902M')
    a = p.parse_args()
    if a.cmd == 'new':
        new(a.roster, a.root, a.port)
    elif a.cmd == 'rematch':
        rematch(a.prev, a.port, a.seed, draw_from=a.draw_from, final=a.final)
    else:
        run(Path(a.match) if a.match else MATCHES / CURRENT.read_text(encoding='utf-8').strip(), stop_at=a.stop_at)


if __name__ == '__main__':
    main()
