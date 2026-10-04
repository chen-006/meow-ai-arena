"""事件调度：有事才唤醒选手，每次唤醒 = 启动一次对应 Agent 的命令行进程并续接它自己的会话。

没有事件时没有任何选手进程在运行，所以“交卷后仍在思考/轮询”在结构上不可能发生。
"""
import glob
import json
import os
from pathlib import Path
import queue
import re
import shutil
import sqlite3
import subprocess
import sys
import threading
import time
import urllib.parse
import urllib.request
import uuid
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parent

# 开局消息直接带上规则和操作说明，保证每家起点相同（本目录的 RULES.md、README.md 是同样内容）
FIRST_PROMPT = ('你在一局七人《外交》（Diplomacy）中代表 {power}。以下是比赛规则和操作说明，'
                '本目录的 RULES.md、README.md 内容相同。\n\n{rules}\n\n{ops}\n\n---\n'
                '之后每当有新消息或进入新阶段，你都会收到一份这样的简报。处理完就结束回复，不需要等待、轮询或保持运行。\n\n')
WAKE_SUFFIX = '\n\n（处理完就结束回复。）'
NUDGE_SUFFIX = '\n\n所有国家都已停下，只差你还没提交本阶段军令。请决定后提交，然后结束回复。'
# 调度器可能从桌面版会话里启动：清掉宿主注入的变量，选手必须走自己的登录，不能借用宿主的内部通道
HOST_ENV_PREFIXES = ('CLAUDE', 'ANTHROPIC_BASE_URL', 'USE_LOCAL_OAUTH', 'USE_STAGING_OAUTH', 'CODEX_THREAD', 'CODEX_SANDBOX')
# 各家统一按 Codex 的原生窗口计算上下文，满了用各自原生方式自动压缩
CONTEXT_WINDOW = 256000
# 只有新消息时先等对话停一会儿再唤醒，把陆续到达的消息合并送达：安静 QUIET 秒，或最早一条已等 MAX_WAIT 秒
MESSAGE_QUIET, MESSAGE_MAX_WAIT = 10, 40
# 额度用完时不冻结，隔一段时间自动重试；累计等满上限仍失败才冻结
QUOTA_RE = re.compile(r'rate.?limit|quota|usage.?limit|limit.?reached|too many requests|insufficient|429|额度|限额|余额', re.I)
QUOTA_RETRY, QUOTA_MAX_WAIT = 900, 8 * 3600
RETRY_DELAYS = [10, 30, 120]  # 唤醒失败后自动重试的等待秒数；再失败就冻结
RETRY_PREFIX = '（上次处理被中断。以下内容重新送达；你之前已经执行的操作仍然有效，可用 python game.py brief 查看最新状态。）\n'


def now():
    return datetime.now(timezone.utc).isoformat()


def find_codex():
    found = sorted(glob.glob(os.path.expandvars(r'%LOCALAPPDATA%\OpenAI\Codex\bin\*\codex.exe')), key=os.path.getmtime)
    return found[-1] if found else shutil.which('codex')


def find_opencode():
    path = os.path.expandvars(r'%LOCALAPPDATA%\Programs\@opencodedesktop\resources\opencode-cli.exe')
    return path if os.path.exists(path) else shutil.which('opencode')


# ---------------- 适配器 ----------------
class Claude:
    """Claude Code 无头模式。不加载用户级设置、记忆和 MCP，只允许本目录内读写和运行 python。"""
    def command(self, p, first):
        cmd = [shutil.which('claude') or 'claude', '-p', '--output-format', 'stream-json', '--verbose',
               '--model', p['model'], '--permission-mode', 'acceptEdits',
               '--setting-sources', 'project,local', '--strict-mcp-config',
               # 只额外放行 python；读写默认仅限当前目录，工作区外的路径在无头模式下会被拒绝
               '--allowedTools', 'Bash(python:*)',
               '--disallowedTools', 'WebFetch', 'WebSearch', 'Task']
        if p.get('effort'):
            cmd += ['--effort', p['effort']]
        cmd += ['--session-id', p['session_id']] if first else ['--resume', p['session_id']]
        return cmd, True, {'CLAUDE_CODE_AUTO_COMPACT_WINDOW': str(CONTEXT_WINDOW)}

    def parse(self, lines):
        out = {}
        for line in lines:
            try:
                e = json.loads(line)
            except ValueError:
                continue
            if e.get('type') == 'result':
                out.update(ok=not e.get('is_error') and e.get('subtype') == 'success',
                           usage=e.get('usage'), cost_usd=e.get('total_cost_usd'),
                           session_id=e.get('session_id'), error=None if not e.get('is_error') else e.get('result'))
        return out


class Codex:
    """Codex CLI exec。使用独立的 CODEX_HOME，不读取用户的全局 AGENTS.md 和配置。"""
    def __init__(self, home):
        self.home = Path(home)

    def env(self, workspace):
        self.home.mkdir(parents=True, exist_ok=True)
        auth = Path.home() / '.codex' / 'auth.json'
        if auth.exists() and not (self.home / 'auth.json').exists():
            shutil.copy2(auth, self.home / 'auth.json')
        # 工作区必须受信任，否则 exec 会退回只读并拦截所有命令。
        # Windows 沙箱用 unelevated（用户确认）：elevated 会反复弹 UAC 授权窗口。
        config = self.home / 'config.toml'
        text = config.read_text(encoding='utf-8') if config.exists() else (
            'sandbox_mode = "workspace-write"\napproval_policy = "never"\n\n'
            '[sandbox_workspace_write]\nnetwork_access = true\n\n[windows]\nsandbox = "unelevated"\n')
        text = text.replace('sandbox = "elevated"', 'sandbox = "unelevated"')
        key = f"[projects.'{str(workspace).lower()}']"
        if key not in text:
            text += f'\n{key}\ntrust_level = "trusted"\n'
        config.write_text(text, encoding='utf-8')
        return {'CODEX_HOME': str(self.home)}

    def command(self, p, first):
        opts = ['--json', '--skip-git-repo-check', '-m', p['model'],
                '-c', f'model_reasoning_effort="{p.get("effort") or "high"}"']
        exe = find_codex()
        if first:
            return [exe, 'exec', *opts, '-C', p['workspace'], '-'], True, self.env(p['workspace'])
        return [exe, 'exec', 'resume', p['session_id'], *opts, '-'], True, self.env(p['workspace'])

    def parse(self, lines):
        out = dict(ok=False)
        for line in lines:
            try:
                e = json.loads(line)
            except ValueError:
                continue
            kind = e.get('type')
            if kind == 'thread.started':
                out['session_id'] = e.get('thread_id')
            elif kind == 'turn.completed':
                out.update(ok=True, usage=e.get('usage'))
            elif kind in ('turn.failed', 'error'):
                out.update(ok=False, error=json.dumps(e, ensure_ascii=False)[:500])
        return out


class OpenCode:
    """OpenCode CLI run。独立私有服务，权限由选手目录下的 opencode.json 限制。"""
    def command(self, p, first):
        model = p['model'] + (f'#{p["effort"]}' if p.get('effort') else '')
        return [find_opencode(), 'run', '--standalone', '--session', p['session_id'], '--model', model,
                '--format', 'json', '--auto'], True, {}

    def parse(self, lines):
        # run --format json 只输出文字、工具、步骤事件，不含用量；用量由 totals() 从 OpenCode 会话库读取。
        out = dict(ok=True)
        for line in lines:
            try:
                e = json.loads(line)
            except ValueError:
                continue
            if e.get('sessionID'):
                out['session_id'] = e['sessionID']
            if e.get('type') == 'error':
                out.update(ok=False, error=json.dumps(e, ensure_ascii=False)[:500])
        return out

    def totals(self, session_id):
        """会话累计用量（只读 OpenCode 本地库）。"""
        db = Path.home() / '.local' / 'share' / 'opencode' / 'opencode.db'
        try:
            con = sqlite3.connect(f'file:{db.as_posix()}?mode=ro', uri=True, timeout=10)
            try:
                row = con.execute('select cost, tokens_input, tokens_output, tokens_reasoning, tokens_cache_read, '
                                  'tokens_cache_write from session_v2 where id = ?', (session_id,)).fetchone()
            finally:
                con.close()
        except sqlite3.Error:
            return None
        if not row:
            return None
        return dict(zip(('cost_usd', 'input_tokens', 'output_tokens', 'reasoning_tokens', 'cache_read_tokens',
                         'cache_write_tokens'), row))


class Script:
    """测试用脚本选手：读取简报，全部驻守/解散/放弃增兵。"""
    def command(self, p, first):
        return [sys.executable, str(ROOT / 'bots' / f'{p["model"]}_bot.py')], True, {}

    def parse(self, lines):
        return dict(ok=True)


# ---------------- 调度 ----------------
class Court:
    def __init__(self, url, token):
        self.url, self.token = url, token

    def call(self, route, payload=None, timeout=70):
        req = urllib.request.Request(self.url + route,
            data=json.dumps(payload).encode() if payload is not None else None,
            headers={'Authorization': 'Bearer ' + self.token, 'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.load(r)

    def wait(self, after, seconds):
        return self.call('/admin/wait?' + urllib.parse.urlencode(dict(after=after, seconds=seconds)))

    def brief(self, power, msg_after, hist_after, full):
        return self.call('/admin/brief?' + urllib.parse.urlencode(
            dict(power=power, msg_after=msg_after, hist_after=hist_after, full=int(full))))

    def control(self, action, reason=''):
        return self.call('/admin', dict(action=action, reason=reason))


class Scheduler:
    def __init__(self, match_dir, court, log=print):
        self.dir = Path(match_dir)
        self.court, self.log = court, log
        self.path = self.dir / 'scheduler.json'
        self.data = json.loads(self.path.read_text(encoding='utf-8'))
        self.adapters = dict(claude=Claude(), codex=Codex(self.dir / 'codex_home'), opencode=OpenCode(), script=Script())
        self.done = queue.Queue()
        self.threads = {}
        self.stopping = set()
        self.unread = {}  # 国家 -> [已知收件数, 首次发现时间, 最近变化时间]
        self.timeout = 60 * float(self.data.get('wake_timeout_minutes', 45))
        for p in self.data['players'].values():
            if p.get('busy'):  # 上次调度中断在运行中：本次重新送达
                p.update(busy=False, interrupted=True)
        self.save()

    def save(self):
        tmp = self.path.with_suffix('.tmp')
        tmp.write_text(json.dumps(self.data, ensure_ascii=False, indent=2), encoding='utf-8')
        os.replace(tmp, self.path)

    def alert(self, text):
        line = f'{now()} {text}'
        with (self.dir / '警报.log').open('a', encoding='utf-8') as f:
            f.write(line + '\n')
        self.log('【警报】' + text)
        toast(f'外交 {self.dir.name}', text)

    def record(self, entry):
        with (self.dir / '唤醒记录.jsonl').open('a', encoding='utf-8') as f:
            f.write(json.dumps(entry, ensure_ascii=False) + '\n')

    def first_prompt(self, power):
        ws = Path(self.data['players'][power]['workspace'])
        read = lambda name: (ws / name).read_text(encoding='utf-8-sig').strip()
        intro = self.data.get('intro', '').format(power=power)  # 续局：先交代上一局结果和重新抽签
        return intro + FIRST_PROMPT.format(power=power, rules=read('RULES.md'), ops=read('README.md'))

    # 是否有需要送达的新内容
    def pending(self, power, st):
        p = self.data['players'][power]
        s = st['players'][power]
        if not s['active'] or st['game_over']:
            return False
        if p.get('interrupted'):
            return time.time() >= p.get('retry_at', 0)
        if not p.get('started'):
            return True
        if st['history_len'] > p['hist_cursor'] and s['required']:
            return True
        if s['incoming'] <= p['msg_cursor']:
            self.unread.pop(power, None)
            return False
        t = time.time()
        u = self.unread.get(power)
        if not u:
            u = self.unread[power] = [s['incoming'], t, t]
        elif s['incoming'] != u[0]:
            u[0], u[2] = s['incoming'], t
        return True if t - u[2] >= MESSAGE_QUIET or t - u[1] >= MESSAGE_MAX_WAIT else 'hold'

    def launch(self, power, st, reason):
        p = self.data['players'][power]
        first = not p.get('started')
        full = first or st['history_len'] > p['hist_cursor'] or reason == 'nudge' or p.get('interrupted')
        b = self.court.brief(power, p['msg_cursor'], p['hist_cursor'], full)
        notice = self.data.get('notice', '') + '\n\n' if p.get('notice_pending') else ''  # 管理员通知，送达一次
        prompt = ((RETRY_PREFIX if p.get('interrupted') else '') + notice
                  + (self.first_prompt(power) if first else '') + b['text']
                  + (NUDGE_SUFFIX if reason == 'nudge' else WAKE_SUFFIX))
        if not p.get('session_id') and p['harness'] in ('claude', 'opencode', 'script'):
            p['session_id'] = str(uuid.uuid4()) if p['harness'] != 'opencode' else 'ses_' + uuid.uuid4().hex[:24]
        adapter = self.adapters[p['harness']]
        cmd, use_stdin, extra_env = adapter.command(p, not p.get('session_ready'))
        p['wakes'] = p.get('wakes', 0) + 1
        wake_id = f'{power}_{p["wakes"]:04d}'
        logdir = self.dir / '唤醒日志' / power
        logdir.mkdir(parents=True, exist_ok=True)
        (logdir / f'{wake_id}.prompt.txt').write_text(prompt, encoding='utf-8')
        p.update(busy=True, pending_cursor=[b['msg_cursor'], b['hist_cursor']], last_reason=reason)
        self.save()
        entry = dict(wake=wake_id, power=power, harness=p['harness'], model=p['model'], reason=reason,
                     phase=st['phase'], start=now(), prompt_chars=len(prompt))
        self.log(f'唤醒 {power}（{p["harness"]}/{p["model"]}）原因：{reason} 阶段 {st["phase"]}')

        def run():
            started = time.monotonic()
            out_path, err_path = logdir / f'{wake_id}.out.jsonl', logdir / f'{wake_id}.err.txt'
            # PWD 必须指向选手工作区：OpenCode 等工具按 PWD 而不是进程目录判断项目根目录
            env = {k: v for k, v in os.environ.items() if not k.upper().startswith(HOST_ENV_PREFIXES)}
            env.update(extra_env, PYTHONIOENCODING='utf-8', PWD=p['workspace'])
            env.pop('OLDPWD', None)
            # 选手统一用项目 .venv 的 python：Codex 沙箱无权运行用户目录下安装的 Python
            env['PATH'] = str(ROOT / '.venv' / 'Scripts') + os.pathsep + env.get('PATH', '')
            status = dict(ok=False)
            try:
                with out_path.open('w', encoding='utf-8') as out, err_path.open('w', encoding='utf-8') as err:
                    proc = subprocess.Popen(cmd + ([] if use_stdin else [prompt]), cwd=p['workspace'],
                                            stdin=subprocess.PIPE if use_stdin else subprocess.DEVNULL,
                                            stdout=out, stderr=err, env=env, text=True, encoding='utf-8',
                                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                    self.threads[power] = proc
                    if use_stdin:
                        try:
                            proc.stdin.write(prompt)
                            proc.stdin.close()
                        except OSError:
                            pass  # 进程没读完输入就退出了（如额度用完）：以它自己的输出和报错为准

                    try:
                        code = proc.wait(timeout=self.timeout)
                    except subprocess.TimeoutExpired:
                        kill_tree(proc)
                        code = 'timeout'
                lines = out_path.read_text(encoding='utf-8', errors='replace').splitlines()
                status = adapter.parse(lines)
                if code != 0:
                    status['ok'] = False
                    status.setdefault('error', f'退出码 {code}；' + err_path.read_text(encoding='utf-8', errors='replace')[-400:])
            except Exception as exc:
                status = dict(ok=False, error=f'{type(exc).__name__}: {exc}')
            status['seconds'] = round(time.monotonic() - started, 1)
            self.done.put((power, entry, status))
        threading.Thread(target=run, daemon=True).start()

    def finish(self, power, entry, status):
        p = self.data['players'][power]
        self.threads.pop(power, None)
        p['busy'] = False
        entry.update(end=now(), **{k: v for k, v in status.items() if k in ('ok', 'usage', 'cost_usd', 'seconds', 'error')})
        if p['harness'] == 'opencode' and (status.get('session_id') or p.get('session_id')):
            total = self.adapters['opencode'].totals(status.get('session_id') or p['session_id'])
            if total:
                last = p.get('usage_total') or {}
                entry['usage'] = {k: round(v - (last.get(k) or 0), 6) for k, v in total.items() if v is not None}
                p['usage_total'] = total
        self.record(entry)
        if status.get('session_id'):  # Agent 确认会话已建立，以后一律续接
            p.update(session_id=status['session_id'], session_ready=True)
        elif not p.get('session_ready') and p['harness'] != 'codex':
            p['session_id'] = None  # 会话未建立，下次换新编号重建
        if status.get('ok'):
            p['msg_cursor'], p['hist_cursor'] = p.pop('pending_cursor')
            p.update(started=True, session_ready=True, interrupted=False, failures=0)
            p.pop('notice_pending', None)
            p.pop('retry_at', None)
            p.pop('quota_since', None)
            self.log(f'完成 {power}，用时 {status.get("seconds")} 秒')
        elif power in self.stopping:
            self.stopping.discard(power)
            p['interrupted'] = True
            self.log(f'已中止 {power}，恢复后重新送达')
        else:
            p['interrupted'] = True
            if (p['model'].startswith('deepseek/') and BALANCE_RE.search(status.get('error') or '')
                    and swap_deepseek_key(self.log)):
                p['retry_at'] = time.time()
                self.save()
                return
            if QUOTA_RE.search(status.get('error') or ''):
                p.setdefault('quota_since', time.time())
                if time.time() - p['quota_since'] < QUOTA_MAX_WAIT:
                    p['retry_at'] = time.time() + QUOTA_RETRY
                    self.save()
                    self.log(f'{power} 疑似额度用完，{QUOTA_RETRY // 60} 分钟后自动重试：{status.get("error")}')
                    return
            p['failures'] = p.get('failures', 0) + 1
            if p['failures'] <= len(RETRY_DELAYS):
                delay = RETRY_DELAYS[p['failures'] - 1]
                p['retry_at'] = time.time() + delay
                self.save()
                self.log(f'{power} 唤醒失败（第 {p["failures"]} 次），{delay} 秒后自动重试：{status.get("error")}')
                return
            p['failures'] = 0  # 管理员恢复后重新计数
            p.pop('quota_since', None)
            self.save()
            self.alert(f'{power} 本次唤醒失败，已冻结对局：{status.get("error")}')
            self.court.control('pause', f'{power} 唤醒失败')
            return
        self.save()

    def stop_all(self):
        for power, proc in list(self.threads.items()):
            self.stopping.add(power)
            kill_tree(proc)

    def run(self, until_pause=False, stop_at=None):
        st = self.court.control('status')
        self.log(f'调度开始：{st["phase"]}')
        while True:
            try:
                while True:
                    self.finish(*self.done.get_nowait())
            except queue.Empty:
                pass
            st = self.court.wait(st['revision'], 2)
            if stop_at and st['phase'] == stop_at and not self.threads and self.done.empty() and not st['paused']:
                self.court.control('pause', f'到达预定停止阶段 {stop_at}')
                self.log(f'到达 {stop_at}，已冻结。')
                return st
            if st['game_over']:
                if not self.threads:
                    self.log('整局结束。')
                    return st
                continue
            if st['paused']:
                if (until_pause or st.get('stop_requested')) and not self.threads and self.done.empty():
                    if st.get('stop_requested'):
                        self.log('收到停止指令，选手进程已全部停下，调度器退出。')
                    return st
                if self.threads:
                    self.alert('对局冻结，停止正在运行的选手进程。')
                    self.stop_all()
                time.sleep(1)
                continue
            busy = {k for k, p in self.data['players'].items() if p.get('busy')}
            for power in self.data['players']:
                due = power not in busy and self.pending(power, st)
                if due == 'hold':  # 有消息在合并等待：它还没读到，不能结算
                    busy.add(power)
                elif due:
                    self.unread.pop(power, None)
                    self.launch(power, st, 'interrupted' if self.data['players'][power].get('interrupted')
                                else 'start' if not self.data['players'][power].get('started') else 'event')
                    busy.add(power)
            # 有选手在等待重试时不结算：它可能还要根据没送达的消息改军令
            if busy or any(p.get('interrupted') for p in self.data['players'].values()):
                continue
            waiting = [k for k, s in st['players'].items() if s['required'] and not s['ready'] and k in self.data['players']]
            if not waiting:
                if any(s['required'] for s in st['players'].values()):
                    self.court.control('settle')
                    self.log('结算 ' + st['phase'])
                continue
            fresh = [k for k in waiting if self.data['players'][k].get('nudged_rev') != st['revision']]
            if not fresh:
                self.alert('所有选手都已停下但仍有国家未交军令：' + ', '.join(waiting))
                self.court.control('pause', '有国家未交军令且全部停下')
                continue
            for power in fresh:
                self.data['players'][power]['nudged_rev'] = st['revision']
                self.launch(power, st, 'nudge')


BALANCE_RE = re.compile(r'insufficient.?balance|\b402\b|余额不足', re.I)
BACKUP_KEY = ROOT / '管理员' / '备用key' / 'deepseek.txt'


def swap_deepseek_key(log):
    """DeepSeek 余额不足时，换用管理员事先放好的备用 key（写入 OpenCode 的凭据文件，原文件先备份）。"""
    if not BACKUP_KEY.exists():
        return False
    key = BACKUP_KEY.read_text(encoding='utf-8-sig').strip()
    auth_path = Path.home() / '.local' / 'share' / 'opencode' / 'auth.json'
    auth = json.loads(auth_path.read_text(encoding='utf-8'))
    if not key or (auth.get('deepseek') or {}).get('key') == key:
        return False
    shutil.copy2(auth_path, auth_path.with_name(f'auth.json.{datetime.now():%Y%m%d-%H%M%S}.bak'))
    auth['deepseek'] = dict(type='api', key=key)
    tmp = auth_path.with_suffix('.tmp')
    tmp.write_text(json.dumps(auth, indent=2), encoding='utf-8')
    os.replace(tmp, auth_path)
    BACKUP_KEY.rename(BACKUP_KEY.with_name('deepseek.已启用.txt'))
    log('DeepSeek 余额不足，已换用备用 key，立即重试。')
    toast('外交', 'DeepSeek 余额不足，已自动换用备用 key')
    return True


TOAST = r'''
$t = [Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime]
$x = [Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent([Windows.UI.Notifications.ToastTemplateType]::ToastText02)
$n = $x.GetElementsByTagName('text'); $n.Item(0).InnerText = $env:TOAST_TITLE; $n.Item(1).InnerText = $env:TOAST_TEXT
$id = '{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\WindowsPowerShell\v1.0\powershell.exe'
[Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier($id).Show([Windows.UI.Notifications.ToastNotification]::new($x))
'''


def toast(title, text):
    """Windows 桌面通知；失败不影响比赛。"""
    if os.name != 'nt' or os.environ.get('DIPLOMACY_NO_TOAST'):
        return
    try:
        subprocess.Popen(['powershell', '-NoProfile', '-NonInteractive', '-Command', TOAST],
                         env=dict(os.environ, TOAST_TITLE=title, TOAST_TEXT=text[:200]),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    except OSError:
        pass


def kill_tree(proc):
    if proc.poll() is not None:
        return
    if os.name == 'nt':
        subprocess.run(['taskkill', '/T', '/F', '/PID', str(proc.pid)], capture_output=True)
    else:
        proc.kill()
