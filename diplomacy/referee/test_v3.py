"""v3 回归测试：裁判规则与外交流程 + 调度器端到端（脚本选手，不调用任何模型）。"""
import json
import os
os.environ["DIPLOMACY_NO_TOAST"] = "1"  # 测试时不弹桌面通知
from pathlib import Path
import shutil
import tempfile
import threading
import unittest
import uuid

from arena import Arena, OrderError, POWERS
import admin
import match
import scheduler

START = {'AUSTRIA': ['A VIE H', 'A BUD H', 'F TRI H'], 'ENGLAND': ['F LON H', 'F EDI H', 'A LVP H'],
         'FRANCE': ['A PAR H', 'A MAR H', 'F BRE H'], 'GERMANY': ['A BER H', 'A MUN H', 'F KIE H'],
         'ITALY': ['A VEN H', 'A ROM H', 'F NAP H'], 'RUSSIA': ['A MOS H', 'A WAR H', 'F SEV H', 'F STP/SC H'],
         'TURKEY': ['A CON H', 'A SMY H', 'F ANK H']}


class ArenaTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.arena = Arena.create(self.tmp / 'state.json')

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def act(self, power, action, **kw):
        return self.arena.act(power, dict(action=action, request_id=str(uuid.uuid4()), **kw))

    def submit_all(self, overrides=None):
        for p in self.arena.required():
            self.act(p, 'orders', orders=(overrides or {}).get(p, START.get(p)))

    def test_message_privacy_and_public(self):
        self.act('FRANCE', 'send', to='ENGLAND', text='秘密提议')
        self.act('GERMANY', 'send', to='GLOBAL', text='公开声明')
        eng, _, _ = self.arena.brief('ENGLAND')
        ita, _, _ = self.arena.brief('ITALY')
        self.assertIn('秘密提议', eng)
        self.assertNotIn('秘密提议', ita)
        self.assertIn('公开声明', ita)
        self.assertIn('公开声明', eng)
        self.assertEqual(len(self.arena.incoming('FRANCE')), 1)  # 自己发的不算收件

    def test_message_limits(self):
        with self.assertRaises(ValueError):
            self.act('FRANCE', 'send', to='ENGLAND', text='字' * 301)
        self.act('FRANCE', 'send', to='GLOBAL', text='字' * 400)
        with self.assertRaises(ValueError):
            self.act('FRANCE', 'send', to='FRANCE', text='给自己')

    def test_order_validation_and_normalize(self):
        with self.assertRaises(OrderError) as ctx:
            self.act('FRANCE', 'orders', orders=['A PAR - MUN', 'A MAR H', 'F BRE H'])
        self.assertIn('PAR 可选', str(ctx.exception))
        with self.assertRaises(OrderError) as ctx:
            self.act('FRANCE', 'orders', orders=['A PAR H'])
        self.assertIn('缺少命令', str(ctx.exception))
        r = self.act('FRANCE', 'orders', orders=['a par  -  bur', 'A MAR H', 'F BRE H'])
        self.assertTrue(r['ok'])
        self.assertEqual(self.arena.data['orders']['FRANCE'][0], 'A PAR - BUR')

    def test_settle_waits_for_everyone_and_marks_bounce(self):
        self.act('FRANCE', 'orders', orders=['A PAR - BUR', 'A MAR H', 'F BRE H'])
        with self.assertRaises(ValueError):
            self.arena.settle()
        self.submit_all({'FRANCE': ['A PAR - BUR', 'A MAR H', 'F BRE H'],
                         'GERMANY': ['A MUN - BUR', 'A BER H', 'F KIE H']})
        self.assertTrue(self.arena.settle())
        self.assertEqual(self.arena.game().get_current_phase(), 'F1901M')
        text, _, hist = self.arena.brief('FRANCE')
        self.assertIn('A PAR - BUR（受阻）', text)
        self.assertIn('【部队】', text)
        self.assertEqual(hist, 1)
        # 增量简报：不重复已送达的结算，不带整张局面
        text2, _, _ = self.arena.brief('FRANCE', 0, hist, full=False)
        self.assertNotIn('结算', text2)
        self.assertNotIn('【部队】', text2)

    def test_retreat_phase_only_asks_dislodged(self):
        self.submit_all({'FRANCE': ['A PAR - BUR', 'A MAR H', 'F BRE H'],
                         'GERMANY': ['A MUN - RUH', 'A BER - MUN', 'F KIE H']})
        self.arena.settle()
        self.submit_all({'FRANCE': ['A BUR H', 'A MAR H', 'F BRE H'],
                         'GERMANY': ['A RUH - BUR', 'A MUN S A RUH - BUR', 'F KIE H'],
                         **{p: START[p] for p in POWERS if p not in ('FRANCE', 'GERMANY')}})
        self.arena.settle()
        self.assertEqual(self.arena.game().get_current_phase(), 'F1901R')
        self.assertEqual(self.arena.required(), ['FRANCE'])
        text, _, _ = self.arena.brief('FRANCE')
        self.assertIn('被击退', text)
        self.assertIn('A BUR R PAR', text)
        with self.assertRaises(ValueError):
            self.act('FRANCE', 'send', to='GERMANY', text='撤退阶段不能发信')
        self.act('FRANCE', 'orders', orders=['A BUR R PAR'])
        self.arena.settle()
        self.assertEqual(self.arena.game().get_current_phase(), 'S1902M')  # 无人需调整，冬季自动跳过

    def test_agreed_draw(self):
        for p in POWERS[:-1]:
            self.act(p, 'draw')
        self.assertEqual(self.arena.data['stage'], 'play')
        self.act('TURKEY', 'draw')
        self.assertEqual(self.arena.data['stage'], 'done')
        self.assertEqual(self.arena.data['result']['reason'], 'agreed_draw')

    def test_sos_scores(self):
        from simulate import scores
        s = scores({'FRANCE': ['PAR'] * 10, 'GERMANY': ['BER'] * 5, 'ITALY': [], 'AUSTRIA': ['VIE'] * 5}, [])
        self.assertEqual(s['FRANCE'], round(100 * 100 / 150, 2))
        self.assertEqual(s['ITALY'], 0)
        self.assertEqual(scores({'FRANCE': ['PAR'] * 18, 'GERMANY': ['BER']}, ['FRANCE']), {'FRANCE': 100.0, 'GERMANY': 0.0})
        for p in POWERS:
            self.act(p, 'draw')
        self.assertAlmostEqual(sum(self.arena.data['result']['scores'].values()), 100, places=1)
        self.assertIn('得分：RUSSIA', self.arena.result_text())

    def test_idempotent_request(self):
        rid = str(uuid.uuid4())
        self.arena.act('FRANCE', dict(action='send', to='ENGLAND', text='一次', request_id=rid))
        self.arena.act('FRANCE', dict(action='send', to='ENGLAND', text='一次', request_id=rid))
        self.assertEqual(len(self.arena.data['messages']), 1)


class EndToEndTest(unittest.TestCase):
    """真实启动裁判 HTTP 服务与调度器，七国由脚本选手扮演。"""
    def make(self, bots, port):
        self.tmp = Path(tempfile.mkdtemp())
        roster = dict(players=[dict(power=p, harness='script', model=bots.get(p, 'hold')) for p in POWERS])
        path = self.tmp / 'roster.json'
        path.write_text(json.dumps(roster), encoding='utf-8')
        return match.new(path, self.tmp / 'ws', port, matches=self.tmp / 'matches')

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_full_game_to_1920(self):
        match_dir = self.make({}, 18801)
        match.run(match_dir)
        state = json.loads((match_dir / 'state.json').read_text(encoding='utf-8'))
        self.assertEqual(state['result']['reason'], 'turn_limit')
        self.assertEqual(state['result']['phase'], 'F1920M')
        wakes = [json.loads(l) for l in (match_dir / '唤醒记录.jsonl').read_text(encoding='utf-8').splitlines()]
        self.assertTrue(all(w['ok'] for w in wakes))
        # 全员驻守：每个移动阶段每国恰好唤醒一次，没有多余唤醒
        self.assertEqual(len(wakes), 7 * 40)

    def test_lazy_player_is_nudged_then_frozen(self):
        match_dir = self.make({'ITALY': 'lazy'}, 18802)
        st = match.run(match_dir, until_pause=True)
        self.assertTrue(st['paused'])
        wakes = [json.loads(l) for l in (match_dir / '唤醒记录.jsonl').read_text(encoding='utf-8').splitlines()]
        italy = [w['reason'] for w in wakes if w['power'] == 'ITALY']
        self.assertEqual(italy, ['start', 'nudge'])
        self.assertIn('未交军令', (match_dir / '警报.log').read_text(encoding='utf-8'))

    def test_failed_wake_retries_then_freezes(self):
        match_dir = self.make({'ITALY': 'crash'}, 18803)
        old, scheduler.RETRY_DELAYS = scheduler.RETRY_DELAYS, [0, 0]
        try:
            st = match.run(match_dir, until_pause=True)
        finally:
            scheduler.RETRY_DELAYS = old
        self.assertTrue(st['paused'])
        self.assertEqual(st['phase'], 'S1901M')  # 重试期间不结算
        wakes = [json.loads(l) for l in (match_dir / '唤醒记录.jsonl').read_text(encoding='utf-8').splitlines()]
        self.assertEqual([w['reason'] for w in wakes if w['power'] == 'ITALY'], ['start', 'interrupted', 'interrupted'])
        self.assertIn('ITALY 本次唤醒失败', (match_dir / '警报.log').read_text(encoding='utf-8'))

    def test_quota_error_keeps_retrying_before_freeze(self):
        match_dir = self.make({'ITALY': 'quota'}, 18804)
        saved = scheduler.RETRY_DELAYS, scheduler.QUOTA_RETRY, scheduler.QUOTA_MAX_WAIT
        scheduler.RETRY_DELAYS, scheduler.QUOTA_RETRY, scheduler.QUOTA_MAX_WAIT = [0, 0], 0, 3
        try:
            st = match.run(match_dir, until_pause=True)
        finally:
            scheduler.RETRY_DELAYS, scheduler.QUOTA_RETRY, scheduler.QUOTA_MAX_WAIT = saved
        self.assertTrue(st['paused'])
        italy = [w for w in (json.loads(l) for l in (match_dir / '唤醒记录.jsonl').read_text(encoding='utf-8').splitlines())
                 if w['power'] == 'ITALY']
        self.assertTrue(italy)
        # 额度等待期内反复重试（不冻结），等待期过后才按普通失败处理
        self.assertGreaterEqual((match_dir / '调度.log').read_text(encoding='utf-8').count('疑似额度用完'), 2)

    def test_stop_swap_and_rerun(self):
        import admin, time, urllib.request
        match_dir = self.make({}, 18806)
        t = threading.Thread(target=match.run, args=(match_dir,))
        t.start()
        for _ in range(100):
            if (match_dir / '唤醒记录.jsonl').exists():
                break
            time.sleep(0.2)
        admin.request(match_dir, 'stop', '测试停止')
        t.join(60)
        self.assertFalse(t.is_alive())  # 调度器按停止指令退出
        self.assertFalse(admin.running(match_dir))
        admin.swap(match_dir, 'ITALY', 'lazy', None, '偷懒脚本')
        sched = json.loads((match_dir / 'scheduler.json').read_text(encoding='utf-8'))
        self.assertEqual(sched['players']['ITALY']['model'], 'lazy')
        self.assertEqual(sched['players']['ITALY']['model_history'][0]['model'], 'hold')
        st = match.run(match_dir, until_pause=True)  # 重新运行自动解冻，换上的偷懒脚本最终被催促后冻结
        self.assertTrue(st['paused'])
        wakes = [json.loads(l) for l in (match_dir / '唤醒记录.jsonl').read_text(encoding='utf-8').splitlines()]
        self.assertIn('lazy', {w['model'] for w in wakes if w['power'] == 'ITALY'})


    def test_rematch_keeps_sessions_and_reshuffles(self):
        prev = self.make({}, 18806)
        sched = json.loads((prev / 'scheduler.json').read_text(encoding='utf-8'))
        for i, (power, p) in enumerate(sched['players'].items()):
            p.update(model=f'hold', label=f'选手{i}', session_id=f'sid-{power}', session_ready=True)
            (Path(p['workspace']) / '笔记.md').write_text(power, encoding='utf-8')
        sched['players']['ENGLAND']['usage_total'] = dict(cost_usd=1.5)
        (prev / 'scheduler.json').write_text(json.dumps(sched, ensure_ascii=False), encoding='utf-8')
        state = json.loads((prev / 'state.json').read_text(encoding='utf-8'))
        state['result'] = dict(phase='S1906M', reason='agreed_draw', scores={'ENGLAND': 60.0, 'FRANCE': 40.0})
        (prev / 'state.json').write_text(json.dumps(state), encoding='utf-8')
        (prev / '唤醒记录.jsonl').write_text(json.dumps(dict(power='ENGLAND', ok=True, usage=dict(cost_usd=1.5))) + '\n', encoding='utf-8')
        new = match.rematch(prev, 18807, seed=7, matches=self.tmp / 'matches2', draw_from=1910, final=True)
        s2 = json.loads((new / 'scheduler.json').read_text(encoding='utf-8'))
        old_by_label = {p['label']: (k, p) for k, p in sched['players'].items()}
        for power, p in s2['players'].items():
            old_power, old = old_by_label[p['label']]
            self.assertEqual(p['previous_power'], old_power)
            self.assertEqual((p['workspace'], p['session_id']), (old['workspace'], old['session_id']))
            self.assertTrue(p['session_ready'])
            self.assertEqual((Path(p['workspace']) / '笔记.md').read_text(encoding='utf-8'), old_power)  # 笔记保留
            conn = json.loads((Path(p['workspace']) / 'connection.json').read_text(encoding='utf-8'))
            self.assertTrue(conn['url'].endswith(':18807'))
            self.assertNotEqual(power, old_power)  # 避开坐过的国家
            self.assertEqual(p['past_powers'], [old_power])
        eng = next(p for p in s2['players'].values() if p['previous_power'] == 'ENGLAND')
        self.assertEqual(eng['usage_total']['cost_usd'], 1.5)  # OpenCode 会话累计用量带到新局
        prompt = scheduler.Scheduler(new, None, log=lambda t: None).first_prompt('FRANCE')
        self.assertTrue(prompt.startswith('【第 2 局，也是最后一局】'))
        self.assertIn('1910 年春季之前不能表决协议和局', prompt)
        self.assertIn('本局 **1910 年春季起**', prompt)
        self.assertIn('你的目标：按计分规则', prompt)
        a2 = Arena(new / 'state.json')
        try:
            r = a2.act('FRANCE', dict(action='draw', request_id='x'))
            self.assertFalse(r.get('ok'))
        except ValueError as e:
            self.assertIn('1910', str(e))
        self.assertEqual(a2.data['draw_votes'], [])
        self.assertIn('你这局代表 FRANCE', prompt)
        self.assertIn('英国 60', prompt)
        admin.notice(new, '【管理员通知】测试通知')
        st = match.run(new, stop_at='F1901M')
        wakes = [json.loads(l) for l in (new / '唤醒记录.jsonl').read_text(encoding='utf-8').splitlines()]
        self.assertTrue(all(w['ok'] for w in wakes) and len(wakes) == 7)
        first = (new / '唤醒日志' / 'ITALY' / 'ITALY_0001.prompt.txt').read_text(encoding='utf-8')
        self.assertIn('你这局代表 ITALY', first)
        self.assertTrue(first.startswith('【管理员通知】测试通知'))
        match.run(new, stop_at='S1902M')  # 通知只送达一次
        second = (new / '唤醒日志' / 'ITALY' / 'ITALY_0002.prompt.txt').read_text(encoding='utf-8')
        self.assertNotIn('测试通知', second)


class DebounceTest(unittest.TestCase):
    def test_messages_are_merged_until_quiet(self):
        tmp = Path(tempfile.mkdtemp())
        try:
            roster = dict(players=[dict(power=p, harness='script', model='hold') for p in POWERS])
            (tmp / 'r.json').write_text(json.dumps(roster), encoding='utf-8')
            match_dir = match.new(tmp / 'r.json', tmp / 'ws', 18805, matches=tmp / 'm')
            sch = scheduler.Scheduler(match_dir, None, log=lambda t: None)
            sch.data['players']['FRANCE'].update(started=True)
            st = dict(game_over=False, history_len=0,
                      players={'FRANCE': dict(active=True, required=True, incoming=1)})
            self.assertEqual(sch.pending('FRANCE', st), 'hold')
            st['players']['FRANCE']['incoming'] = 2
            self.assertEqual(sch.pending('FRANCE', st), 'hold')
            sch.unread['FRANCE'][2] -= scheduler.MESSAGE_QUIET  # 模拟安静了足够久
            self.assertIs(sch.pending('FRANCE', st), True)
            sch.unread['FRANCE'] = [2, 0, 1e18]  # 一直有新消息，但最早一条已等太久
            self.assertIs(sch.pending('FRANCE', st), True)
            st['history_len'] = 1  # 新阶段需要行动：立即唤醒
            sch.unread.clear()
            self.assertIs(sch.pending('FRANCE', st), True)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
