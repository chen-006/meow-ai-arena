"""七国外交本地裁判。不调用任何模型。

结算由调度器触发：所有需行动国家都已交军令，且没有国家正在处理消息时，调度器调用 settle。
选手交军令即视为确认；收到新消息不会撤回确认，调度器保证收件人处理完新消息后才结算。
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import threading
import traceback
from urllib.parse import urlsplit, parse_qs
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from diplomacy import Game
from simulate import END_YEAR, final_result, finish_after_fall

POWERS = ['AUSTRIA', 'ENGLAND', 'FRANCE', 'GERMANY', 'ITALY', 'RUSSIA', 'TURKEY']
SEASONS = {'S': '春', 'F': '秋', 'W': '冬'}
KINDS = {'M': '外交与移动', 'R': '撤退', 'A': '增兵与裁军'}
RESULT_LABELS = {'bounce': '受阻', 'dislodged': '被击退', 'cut': '支援被切断', 'void': '无效',
                 'disrupted': '海运中断', 'no convoy': '无有效海运', 'disband': '解散'}
MSG_LIMIT = {'private': 300, 'public': 400}


def now():
    return datetime.now(timezone.utc).isoformat()


def phase_name(phase):
    if not phase or phase in ('COMPLETED', 'FORMING'):
        return phase or ''
    return f'{phase[1:5]}年{SEASONS.get(phase[0], phase[0])}·{KINDS.get(phase[-1], phase[-1])}（{phase}）'


def normalize_order(text):
    return ' '.join(str(text).upper().split())


class OrderError(ValueError):
    pass


class Arena:
    def __init__(self, path):
        self.path = Path(path)
        self.lock = threading.RLock()
        self.changed = threading.Condition(self.lock)
        self.data = json.loads(self.path.read_text(encoding='utf-8'))
        if self.data.get('mode') != 'async-v3':
            raise ValueError('不是 v3 存档；旧存档请用备份的旧程序查看。')

    @classmethod
    def create(cls, path, auto_settle=False, draw_from=None):
        path = Path(path)
        if path.exists():
            raise ValueError('比赛文件已存在，拒绝覆盖')
        game = Game()
        data = dict(mode='async-v3', match_id=game.game_id, game=game.to_dict(), stage='play',
                    tokens={p: secrets.token_urlsafe(32) for p in POWERS},
                    admin_token=secrets.token_urlsafe(32), auto_settle=auto_settle,
                    ready=[], orders={}, messages=[], history=[], requests={}, draw_votes=[],
                    paused=False, revision=1, admin_events=[], faults=[], result=None,
                    created=now(), draw_from=draw_from)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data, ensure_ascii=False), encoding='utf-8')
        return cls(path)

    # ---------- 基础 ----------
    def game(self):
        return Game.from_dict(self.data['game'])

    def save(self):
        tmp = self.path.with_suffix('.tmp')
        with tmp.open('w', encoding='utf-8') as f:
            json.dump(self.data, f, ensure_ascii=False)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, self.path)

    def bump(self):
        self.data['revision'] += 1
        self.changed.notify_all()

    def identity(self, token):
        for power, value in self.data['tokens'].items():
            if secrets.compare_digest(token, value):
                return power
        raise PermissionError('身份凭据无效')

    def active(self, game=None):
        game = game or self.game()
        return [p for p in POWERS if game.get_units(p) or game.get_centers(p)]

    def required(self, game=None):
        if self.data['stage'] == 'done':
            return []
        game = game or self.game()
        return [p for p in self.active(game) if game.get_orderable_locations(p)]

    def incoming(self, power):
        return [m for m in self.data['messages'] if m['sender'] != power
                and (power in m['to'] or 'GLOBAL' in m['to'])]

    # ---------- 文字简报 ----------
    def _board(self, game, power):
        centers, units = game.get_centers(), game.get_units()
        owned = {c for cs in centers.values() for c in cs}
        lines = ['【补给中心】（18 个单独获胜）']
        for p in POWERS:
            if centers.get(p) or units.get(p):
                tag = '（你）' if p == power else ''
                lines.append(f'{p}{tag} {len(centers.get(p, []))}：{" ".join(sorted(centers.get(p, []))) or "无"}')
        neutral = sorted(set(game.map.scs) - owned)
        lines.append(f'中立 {len(neutral)}：{" ".join(neutral) or "无"}')
        lines.append('【部队】（* 表示被击退、等待撤退）')
        for p in POWERS:
            if units.get(p):
                tag = '（你）' if p == power else ''
                lines.append(f'{p}{tag} {len(units[p])}：{", ".join(units[p])}')
        return lines

    def _settlement(self, record, power):
        phase = record['phase']
        if record.get('event') == 'agreed_draw':
            return [f'—— {phase_name(phase)}：全体同意和局，本阶段命令未执行 ——']
        lines = [f'—— {phase_name(phase)} 结算 ——']
        results = record.get('results') or {}
        for p in POWERS:
            orders = (record.get('orders') or {}).get(p)
            if not orders:
                continue
            marks = []
            status = results.get(p, {})
            for order in orders:
                unit = ' '.join(order.split()[:2])
                failed = [RESULT_LABELS.get(r, r) for r in status.get(unit, []) if r]
                marks.append(order + (f'（{"、".join(failed)}）' if failed else ''))
            tag = '（你）' if p == power else ''
            lines.append(f'{p}{tag}：' + '；'.join(marks))
        before = (record.get('before') or {}).get('centers', {})
        after = record.get('centers') or {}
        for p in POWERS:
            gained = sorted(set(after.get(p, [])) - set(before.get(p, [])))
            lost = sorted(set(before.get(p, [])) - set(after.get(p, [])))
            if gained or lost:
                lines.append(f'中心变化 {p}：' + (f'+{" ".join(gained)} ' if gained else '')
                             + (f'-{" ".join(lost)}' if lost else ''))
        return lines

    def _task(self, game, power):
        if self.data['stage'] == 'done':
            return ['【整局结束】' + self.result_text()]
        if power not in self.active(game):
            return ['【你已被淘汰】无需再操作。']
        phase = game.get_current_phase()
        own = self.data['orders'].get(power)
        if power not in self.required(game):
            return ['【本阶段你无需行动】' + ('可以继续外交。' if phase.endswith('M') else '等待其他国家处理。')]
        lines = []
        if phase.endswith('M'):
            lines.append('【本阶段你需要】给每支部队下令（未下令的部队不会自动驻守）。')
        elif phase.endswith('R'):
            possible = game.get_all_possible_orders()
            lines.append('【本阶段你需要】为被击退的部队选择撤退或解散：')
            for loc in game.get_orderable_locations(power):
                lines.append(f'  {loc}：' + ' | '.join(sorted(possible[loc])))
        else:
            delta = len(game.get_centers(power)) - len(game.get_units(power))
            possible = game.get_all_possible_orders()
            options = sorted({o for loc in game.get_orderable_locations(power) for o in possible[loc]})
            if delta > 0:
                lines.append(f'【本阶段你需要】最多增兵 {delta} 支（也可少建或不建，提交空命令即放弃）。可选：')
            else:
                lines.append(f'【本阶段你需要】必须裁军 {-delta} 支。可选：')
            lines.append('  ' + ' | '.join(options))
        if own is not None:
            lines.append('你已提交：' + ('；'.join(own) if own else '（空命令）') + '。需要修改就重新提交。')
        else:
            lines.append('你尚未提交。')
        others = [p for p in self.required(game) if p != power]
        lines.append(f'其他需行动国家已交 {sum(p in self.data["ready"] for p in others)}/{len(others)}。')
        if phase.endswith('M') and self.data['draw_votes']:
            lines.append('和局赞成：' + ', '.join(sorted(self.data['draw_votes'])))
        return lines

    def result_text(self):
        r = self.data.get('result') or {}
        if not r:
            return ''
        reasons = {'victory': '18 中心单独胜利', 'agreed_draw': '全体同意和局', 'turn_limit': f'{END_YEAR} 年秋季到期和局'}
        who = r.get('winners') or r.get('draw_participants') or []
        ranked = sorted((r.get('scores') or {}).items(), key=lambda kv: -kv[1])
        points = '；'.join(f'{p} {v:g}' for p, v in ranked if v)
        return f'{reasons.get(r.get("reason"), r.get("reason"))}：{", ".join(who)}' + (f'。得分：{points}' if points else '')

    def brief(self, power, msg_after=0, hist_after=0, full=True):
        """返回 (文字, 新消息游标, 新历史游标)。full=False 时省略整张局面。"""
        game = self.game()
        phase = game.get_current_phase()
        lines = [f'==== 你是 {power} · 当前 {phase_name(phase) if self.data["stage"] != "done" else "已结束"} ====']
        history = self.data['history']
        for record in history[hist_after:]:
            lines += self._settlement(record, power)
        incoming = self.incoming(power)
        new = incoming[msg_after:]
        if new:
            lines.append(f'【新消息 {len(new)} 条】')
            for m in new:
                target = '全体' if 'GLOBAL' in m['to'] else '你'
                lines.append(f'[{m["phase"]}] {m["sender"]} → {target}：{m["text"]}')
        elif msg_after == 0 and not history[hist_after:]:
            lines.append('【暂无消息】')
        if full:
            lines += self._board(game, power)
        lines += self._task(game, power)
        return '\n'.join(lines), len(incoming), len(history)

    def legal(self, power, locs=None):
        game = self.game()
        possible = game.get_all_possible_orders()
        mine = game.get_orderable_locations(power)
        wanted = [l.upper() for l in locs] if locs else mine
        lines = []
        for loc in mine:
            if any(loc.split('/')[0] == w.split('/')[0] for w in wanted):
                lines.append(f'{loc}：' + ' | '.join(sorted(possible[loc])))
        return '\n'.join(lines) or '本阶段你没有可下令的部队。'

    def inbox(self, power, other=None, phase=None):
        items = [m for m in self.data['messages'] if m['sender'] == power or power in m['to'] or 'GLOBAL' in m['to']]
        if other:
            other = other.upper()
            items = [m for m in items if m['sender'] == other or other in m['to']]
        if phase:
            items = [m for m in items if m['phase'] == phase.upper()]
        out = []
        for m in items:
            src = '你' if m['sender'] == power else m['sender']
            dst = '全体' if 'GLOBAL' in m['to'] else ('你' if power in m['to'] else m['to'][0])
            out.append(f'[{m["phase"]}] {src} → {dst}：{m["text"]}')
        return '\n'.join(out) or '没有符合条件的消息。'

    def history_text(self, power, phase=None, last=4):
        items = self.data['history']
        items = [h for h in items if h['phase'] == phase.upper()] if phase else items[-last:]
        return '\n'.join(l for h in items for l in self._settlement(h, power)) or '尚无结算记录。'

    # ---------- 操作 ----------
    def validate_orders(self, power, orders, game):
        possible = game.get_all_possible_orders()
        locs = game.get_orderable_locations(power)
        allowed = {o for loc in locs for o in possible[loc]}
        errors = []
        for order in orders:
            if order not in allowed:
                src = order.split()[1].split('/')[0] if len(order.split()) > 1 else ''
                hint = [l for l in locs if l.split('/')[0] == src]
                errors.append(f'不合法：{order}' + (f'。{hint[0]} 可选：' + ' | '.join(sorted(possible[hint[0]])) if hint else '。该地点没有你的可下令部队。'))
        sources = [o.split()[1].split('/')[0] for o in orders if len(o.split()) > 1]
        for s in sorted(set(sources)):
            if sources.count(s) > 1:
                errors.append(f'{s} 重复下令，只能保留一条。')
        phase = game.get_current_phase()
        if phase.endswith(('M', 'R')):
            given = {o.split()[1].split('/')[0] for o in orders if o in allowed}
            missing = [l for l in locs if l.split('/')[0] not in given]
            if missing:
                errors.append('缺少命令：' + ' '.join(missing) + '（每支需要行动的部队都要下令）')
        else:
            delta = len(game.get_centers(power)) - len(game.get_units(power))
            if delta < 0 and len(orders) != -delta:
                errors.append(f'需要裁军 {-delta} 支，你提交了 {len(orders)} 条。')
            if delta >= 0 and len(orders) > delta:
                errors.append(f'最多增兵 {delta} 支，你提交了 {len(orders)} 条。')
        if errors:
            raise OrderError('\n'.join(errors))

    def act(self, power, payload):
        with self.lock:
            request_id = payload.get('request_id')
            if not isinstance(request_id, str) or not 1 <= len(request_id) <= 128:
                raise ValueError('缺少 request_id')
            key = power + ':' + request_id
            digest = hashlib.sha256(json.dumps(payload, sort_keys=True, ensure_ascii=False).encode()).hexdigest()
            if key in self.data['requests']:
                old = self.data['requests'][key]
                if old['digest'] != digest:
                    raise ValueError('request_id 重复')
                return old['result']
            if self.data['stage'] == 'done':
                return dict(ok=False, text='整局已结束。' + self.result_text())
            if self.data['paused']:
                return dict(ok=False, text='对局已冻结，本次操作未执行。结束回复，等待恢复。')
            game = self.game()
            phase = game.get_current_phase()
            action = payload.get('action')
            if power not in self.active(game):
                return dict(ok=False, text='你已被淘汰。')
            rollback = json.dumps(self.data)
            try:
                if action == 'send':
                    if not phase.endswith('M'):
                        raise ValueError('只有春、秋移动阶段可以发消息。')
                    to = str(payload.get('to', '')).upper()
                    text = payload.get('text')
                    if to != 'GLOBAL' and (to not in self.active(game) or to == power):
                        raise ValueError('收件人必须是其他在场国家，或 GLOBAL（全体公开）。')
                    limit = MSG_LIMIT['public' if to == 'GLOBAL' else 'private']
                    if not isinstance(text, str) or not text.strip():
                        raise ValueError('消息不能为空。')
                    if len(text) > limit:
                        raise ValueError(f'消息 {len(text)} 字，超过上限 {limit} 字（含标点）。请缩短后重发。')
                    self.data['messages'].append(dict(id=secrets.token_hex(8), sender=power, to=[to],
                                                      text=text.strip(), phase=phase, time=now()))
                    result = dict(ok=True, text=f'已发送给 {"全体" if to == "GLOBAL" else to}。')
                elif action == 'orders':
                    if power not in self.required(game):
                        raise ValueError('本阶段你无需下令。')
                    orders = payload.get('orders')
                    if not isinstance(orders, list) or not all(isinstance(o, str) for o in orders):
                        raise ValueError('命令格式错误。')
                    orders = [normalize_order(o) for o in orders if str(o).strip()]
                    self.validate_orders(power, orders, game)
                    self.data['orders'][power] = orders
                    if power not in self.data['ready']:
                        self.data['ready'].append(power)
                    result = dict(ok=True, text='军令已提交：' + ('；'.join(orders) if orders else '（空命令）')
                                  + '。收到新消息时你会被唤醒，可以再改。')
                elif action in ('draw', 'undraw'):
                    if not phase.endswith('M'):
                        raise ValueError('只有春、秋移动阶段可以表决和局。')
                    draw_from = self.data.get('draw_from')
                    if action == 'draw' and draw_from and int(phase[1:5]) < draw_from:
                        raise ValueError(f'本局 {draw_from} 年春季起才能表决协议和局。')
                    votes = self.data['draw_votes']
                    if action == 'draw' and power not in votes:
                        votes.append(power)
                    if action == 'undraw' and power in votes:
                        votes.remove(power)
                    if action == 'draw' and set(self.active(game)) <= set(votes):
                        before = game.get_state()
                        game.draw(winners=self.active(game))
                        self.data['result'] = final_result(game, 'agreed_draw', phase)
                        self.data['history'].append(dict(phase=phase, orders={}, before=before, after=game.get_state(), results={},
                            units=game.get_units(), centers=game.get_centers(), time=now(), event='agreed_draw'))
                        self.data.update(game=game.to_dict(), stage='done', orders={}, ready=[])
                        result = dict(ok=True, text='全体同意，和局成立。' + self.result_text())
                    else:
                        result = dict(ok=True, text=('已赞成和局' if action == 'draw' else '已撤回和局赞成')
                                      + f'。当前赞成：{", ".join(sorted(votes)) or "无"}；需要全部在场国家同意。')
                else:
                    raise ValueError('未知操作')
                self.data['requests'][key] = dict(digest=digest, result=result, time=now())
                if self.data['auto_settle']:
                    self.settle(quiet=True)
                self.bump()
                self.save()
                return result
            except Exception:
                self.data = json.loads(rollback)
                raise

    def settle(self, quiet=False):
        """所有需行动国家都已交军令时结算，并跳过无人需要行动的阶段。"""
        with self.lock:
            if self.data['stage'] == 'done' or self.data['paused']:
                return False
            required = self.required()
            if not set(required) <= set(self.data['ready']):
                if quiet:
                    return False
                raise ValueError('还有国家没交军令：' + ', '.join(p for p in required if p not in self.data['ready']))
            while True:
                game = self.game()
                phase = game.get_current_phase()
                before = game.get_state()
                orders = {p: o for p, o in self.data['orders'].items()}
                for p, o in orders.items():
                    game.set_orders(p, o)
                game.process()
                results = json.loads(json.dumps(game.get_order_status(), default=str))
                ending = finish_after_fall(game, phase)
                self.data['history'].append(dict(phase=phase, orders=orders, before=before, after=game.get_state(),
                                                 results=results, units=game.get_units(), centers=game.get_centers(),
                                                 time=now()))
                self.data.update(game=game.to_dict(), orders={}, ready=[], draw_votes=[])
                if ending or game.is_game_done:
                    self.data['result'] = ending or final_result(game, 'victory', phase)
                    self.data['stage'] = 'done'
                    break
                if self.required(game):
                    break
            self.bump()
            if not quiet:
                self.save()
            return True

    # ---------- 管理 ----------
    def admin_state(self):
        game = self.game()
        active, required = self.active(game), self.required(game)
        return dict(match_id=self.data['match_id'], revision=self.data['revision'],
                    phase=game.get_current_phase(), paused=self.data['paused'],
                    game_over=self.data['stage'] == 'done', result=self.data.get('result'),
                    history_len=len(self.data['history']), stop_requested=bool(self.data.get('stop_requested')),
                    players={p: dict(active=p in active, required=p in required, ready=p in self.data['ready'],
                                     incoming=len(self.incoming(p))) for p in POWERS})

    def admin_wait(self, after, seconds):
        with self.changed:
            self.changed.wait_for(lambda: self.data['revision'] != after, timeout=max(0, min(seconds, 50)))
            return self.admin_state()

    def control(self, action, reason=''):
        with self.lock:
            if action == 'settle':
                self.settle()
            elif action in ('pause', 'resume', 'stop'):
                # stop = 冻结并请调度器在选手进程都停下后退出
                self.data['paused'] = action != 'resume'
                self.data['stop_requested'] = action == 'stop'
                self.data['admin_events'].append(dict(action=action, reason=str(reason), time=now()))
                self.bump()
                self.save()
            elif action != 'status':
                raise ValueError('无效管理员操作')
            return self.admin_state()


def make_server(arena, port):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def reply(self, code, result):
            body = json.dumps(result, ensure_ascii=False).encode('utf-8')
            try:
                self.send_response(code)
                self.send_header('Content-Type', 'application/json; charset=utf-8')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def body(self):
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= 100000:
                raise ValueError('请求大小无效')
            data = json.loads(self.rfile.read(length))
            if not isinstance(data, dict):
                raise ValueError('请求必须是 JSON 对象')
            return data

        def handle_request(self):
            power = None
            try:
                token = self.headers.get('Authorization', '').removeprefix('Bearer ')
                url = urlsplit(self.path)
                q = {k: v[0] for k, v in parse_qs(url.query).items()}
                if url.path.startswith('/admin'):
                    if not secrets.compare_digest(token, arena.data['admin_token']):
                        raise PermissionError('需要管理员身份')
                    if url.path == '/admin/wait':
                        result = arena.admin_wait(int(q.get('after', 0)), float(q.get('seconds', 20)))
                    elif url.path == '/admin/brief':
                        with arena.lock:
                            text, mc, hc = arena.brief(q['power'], int(q.get('msg_after', 0)),
                                                       int(q.get('hist_after', 0)), q.get('full', '1') == '1')
                        result = dict(text=text, msg_cursor=mc, hist_cursor=hc)
                    elif url.path == '/admin' and self.command == 'POST':
                        b = self.body()
                        result = arena.control(b.get('action'), b.get('reason', ''))
                    elif url.path == '/admin':
                        result = arena.control('status')
                    else:
                        raise ValueError('未知接口')
                    return self.reply(200, result)
                power = arena.identity(token)
                with arena.lock:
                    if url.path == '/brief':
                        result = dict(text=arena.brief(power)[0])
                    elif url.path == '/legal':
                        result = dict(text=arena.legal(power, [x for x in q.get('locs', '').split(',') if x]))
                    elif url.path == '/inbox':
                        result = dict(text=arena.inbox(power, q.get('with'), q.get('phase')))
                    elif url.path == '/history':
                        result = dict(text=arena.history_text(power, q.get('phase')))
                    elif url.path == '/snapshot':
                        result = dict(phase=arena.game().get_current_phase(), state=arena.game().get_state())
                    elif url.path == '/action' and self.command == 'POST':
                        result = arena.act(power, self.body())
                    else:
                        raise ValueError('未知接口')
                self.reply(200, result)
            except PermissionError as exc:
                self.reply(403, dict(ok=False, text=str(exc)))
            except (OrderError, ValueError, KeyError) as exc:
                self.reply(400, dict(ok=False, text=str(exc)))
            except Exception:
                with arena.lock:
                    arena.data['faults'].append(dict(time=now(), power=power, trace=traceback.format_exc()))
                    arena.data['paused'] = True
                    arena.bump()
                    try:
                        arena.save()
                    except Exception:
                        pass
                self.reply(500, dict(ok=False, text='裁判内部错误，对局已冻结。结束回复，等待管理员处理。'))

        do_GET = handle_request
        do_POST = handle_request

    return ThreadingHTTPServer(('127.0.0.1', port), Handler)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--state', required=True)
    parser.add_argument('--port', type=int, default=18790)
    args = parser.parse_args()
    server = make_server(Arena(args.state), args.port)
    print(f'裁判：http://127.0.0.1:{server.server_port}', flush=True)
    server.serve_forever()
