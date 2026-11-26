r"""透明的固定 CLI 入口。不会导入任何模型 SDK。

第 12 集团队赛版：
- 多支持 kimi（Kimi Code）、dsh（DeepSeek）、agy（反重力 / Gemini）三家命令行，以及演练用的 fake。
- OpenCode 改走 opencode_runtime.py 私有服务（第 11 集正式赛的做法），不再用 --standalone。
- 配置里有 match_config 时，派活前经过 team12.before_ask：加队员前言、查任务书里有没有夹代码、加"队长亲手改过"的提醒。
- 队员进程的环境去掉调用方（队长）会话的 CLAUDE*/CODEX_* 变量（避免父进程的会话目录或认证环境串入队员进程），代理按配置的 proxy 设。
- 日志里的工具输出去掉思考类事件和字段。
"""
import argparse
import hashlib
import json
import os
# 子进程里的 Python 一律用 UTF-8 模式，避免包装脚本按系统默认编码（GBK）读取 UTF-8 提示词时乱码报错。
os.environ['PYTHONUTF8']='1'
import re
import subprocess
import sys
import time
import uuid
from pathlib import Path
from common import Busy, Lock, now, read_json, run_process, write_json, process_alive
from session_lookup import lookup


TOOLS = ('claude', 'codex', 'opencode', 'grok', 'manual', 'kimi', 'dsh', 'agy', 'fake')
ARGV_PROMPT = ('kimi', 'agy')   # 这两家的提示词只能放命令行参数里
HERE = Path(__file__).resolve().parent


def load(path):
    cfg = read_json(path)
    if not cfg.get('captain') or not cfg.get('members'):
        raise ValueError('必须指定 captain 和 members')
    names = set()
    for m in cfg['members']:
        if not re.fullmatch(r'[\w-]+', m['name']) or m['name'] in names:
            raise ValueError('队员名必须唯一，且仅含中文、字母、数字、下划线、短横线')
        names.add(m['name'])
        if m['tool'] not in TOOLS:
            raise ValueError('未知工具')
        # manual（人工席位）不调用可执行文件，启动的是本目录的传话窗口 relay_window.py。
        for key in (('cwd',) if m['tool'] == 'manual' else ('exe', 'cwd')):
            if not Path(m[key]).is_absolute():
                raise ValueError(key + ' 必须是绝对路径')
        if not m.get('model') or not m.get('effort') or float(m.get('timeout', 600)) <= 0:
            raise ValueError('模型、思考档位、超时配置无效')
        for name, src in m.get('env', {}).items():
            if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', name):
                raise ValueError('环境变量名无效')
            if set(src) not in ({'value_file'}, {'path_file'}):
                raise ValueError('环境变量来源只能为 value_file 或 path_file')
    for key in ('captain_dir', 'state_dir', 'log_dir'):
        if not Path(cfg[key]).is_absolute():
            raise ValueError(key + ' 必须是绝对路径')
    return cfg


def command(m, sid, prompt=''):
    if m['tool'] == 'fake':
        # 演练用假队员：exe 是一个 Python 脚本，输出和 Claude 一样的一行 JSON。
        return [sys.executable, '-B', m['exe']] + (['--resume', sid] if sid else [])
    if m['tool'] == 'kimi':
        # Kimi Code：kimi.exe [-S 编号] -m 型号 --output-format stream-json -p 提示词；思考档在 ~/.kimi-code/config.toml。
        return [m['exe']] + (['-S', sid] if sid else []) + ['-m', m['model'], '--output-format', 'stream-json', '-p', prompt]
    if m['tool'] == 'dsh':
        # dsh 标准模式：node bin.js headless --json --permission-mode danger-full-access [--session-id 编号] -；exe 写 bin.js 的路径。
        import shutil
        overlay = ['--patch', str(HERE / m['profile_patch'])] if m.get('profile_patch') else []
        return [shutil.which('node') or 'node', m['exe'], 'headless', *overlay, '--json', '--permission-mode', 'danger-full-access'] + \
               (['--session-id', sid] if sid else []) + ['-']
    if m['tool'] == 'agy':
        # 反重力命令行：agy [--conversation 编号] --model 型号（型号名里带档位）--dangerously-skip-permissions --output-format stream-json -p=提示词
        effort = ['--effort', m['effort']] if m['effort'] != 'default' else []
        return [m['exe']] + (['--conversation', sid] if sid else []) + ['--model', m['model'], *effort, '--dangerously-skip-permissions',
                '--output-format', 'stream-json', '-p=' + prompt]
    if m['tool'] == 'manual':
        # 人工席位：弹传话窗口，主办人在窗口之间复制粘贴；会话编号固定为 manual-<席位>。
        return [sys.executable, str(HERE / 'relay_window.py'), '--seat', m['name'],
                '--label', m.get('tool_label') or '人工席位', '--model', m.get('model', ''),
                '--cwd', m['cwd'], '--timeout', str(float(m.get('timeout', 600)))]
    if m['tool'] == 'grok':
        # Grok Build：提示词由包装脚本写临时文件走 --prompt-file；包装脚本丢掉输出里的思考字段。
        # 比赛不许联网、不许派子代理。新会话自己生成 UUID 传 -s，被超时打断也知道编号。
        a = [sys.executable, str(HERE / 'grok_cli.py'), m['exe'], '-m', m['model'], '--always-approve',
             '--no-subagents', '--disable-web-search', '--output-format', 'json', '--cwd', m['cwd']]
        if m['effort'] != 'default': a += ['--reasoning-effort', m['effort']]
        return a + (['-r', sid] if sid else ['-s', str(uuid.uuid4())])
    a = [m['exe']]
    if m['tool'] == 'claude':
        a += ['-p', '--dangerously-skip-permissions', '--model', m['model']]
        if m['effort']!='default': a += ['--effort', m['effort']]
        a += ['--output-format', 'json']
        if sid:
            a += ['--resume', sid]
    elif m['tool'] == 'codex':
        a += ['exec'] + (['resume'] if sid else [])
        a += ['--skip-git-repo-check', '--dangerously-bypass-approvals-and-sandbox',
              '--model', m['model'], '-c', 'model_reasoning_effort=' + json.dumps(m['effort']), '--json']
        if m.get('service_tier'):
            a += ['-c', 'service_tier=' + json.dumps(m['service_tier'])]
        a += ([sid] if sid else []) + ['-']
    else:
        # OpenCode：照第 11 集正式赛，经 opencode_runtime.py 起私有服务再 run；effort 写 default 表示不带 #档位（档位写死在 OpenCode 配置里）。
        a = [sys.executable, '-B', str(HERE / 'opencode_runtime.py'), m['exe'], 'run', '--auto', '--model',
             m['model'] + ('' if m['effort'] == 'default' else '#' + m['effort']), '--format', 'json']
        if sid:
            a += ['--session', sid]
    return a


def parse_output(tool, output):
    sid, replies, failed = None, [], False
    text_parts = {}
    last_message = None
    if tool in ('claude', 'grok', 'manual', 'fake'):
        events = [json.loads(output)]
    else:
        events = []
        for x in output.splitlines():
            if not x.strip(): continue
            try: events.append(json.loads(x))
            except ValueError:
                if tool not in ('kimi', 'dsh', 'agy'): raise   # 这三家的输出里偶尔夹着非 JSON 的提示行，跳过
    if tool == 'kimi':
        for e in events:
            if not isinstance(e, dict): continue
            sid = e.get('session_id', e.get('sessionId', sid))
            failed |= e.get('type') == 'error'
            if e.get('role') == 'assistant' and e.get('content'):
                c = e['content']
                text = c if isinstance(c, str) else ''.join(x.get('text', '') for x in c if isinstance(x, dict) and x.get('type') == 'text')
                if text: replies.append(text)
        return sid, (replies[-1] if replies else ''), failed
    if tool == 'dsh':
        for e in events:
            if not isinstance(e, dict): continue
            if e.get('type') == 'session': sid = e.get('sessionId', sid)
            failed |= e.get('type') == 'error'
            if e.get('type') == 'final':
                failed |= str(e.get('reason', '')).lower() in ('error', 'aborted', 'failed')
                if e.get('text'): replies.append(str(e['text']))
        return sid, (replies[-1] if replies else ''), failed
    if tool == 'agy':
        for e in events:
            if not isinstance(e, dict): continue
            r = e.get('result') if isinstance(e.get('result'), dict) else {}
            sid = e.get('conversation_id') or r.get('conversation_id') or sid
            failed |= e.get('event') == 'error' or str(r.get('status', '')).lower() in ('error', 'failed', 'cancelled')
            if e.get('event') == 'result' and r.get('response'): replies.append(str(r['response']))
        return sid, (replies[-1] if replies else ''), failed
    codex_completed = False
    for e in events:
        if not isinstance(e, dict):
            continue
        sid = e.get('session_id', e.get('sessionID', e.get('thread_id', e.get('sessionId', sid))))
        if tool == 'grok':
            failed |= e.get('type') == 'error' or e.get('stopReason') == 'cancelled'
            if isinstance(e.get('text'), str):
                replies.append(e['text'])
        elif tool in ('claude', 'manual', 'fake'):
            failed |= bool(e.get('is_error'))
            if isinstance(e.get('result'), str):
                replies.append(e['result'])
        elif tool == 'codex':
            kind = e.get('type')
            if kind == 'turn.started':
                codex_completed = False
                replies.clear()
            elif kind in ('turn.completed', 'turn.failed'):
                codex_completed = kind == 'turn.completed'
            elif kind == 'item.completed':
                item = e.get('item') or {}
                if item.get('type') == 'agent_message' and isinstance(item.get('text'), str):
                    replies.append(item['text'])
        else:
            failed |= e.get('type') == 'error'
            if e.get('type') == 'step_start':
                text_parts = {}; replies = []; last_message = None
            if e.get('type') == 'text':
                part = e.get('part', {})
                message = part.get('messageID')
                if last_message is not None and message != last_message:
                    text_parts = {}
                last_message = message
                key = part.get('id', str(len(text_parts)))
                text = part.get('text', '')
                previous = text_parts.get(key, '')
                text_parts[key] = text if text.startswith(previous) else previous + text
    reply = ''.join(text_parts.values()) if tool == 'opencode' else (replies[-1] if tool == 'codex' and replies else '\n'.join(replies))
    if tool == 'codex':
        failed = not codex_completed or not reply.strip()
    return sid, reply, failed


THOUGHT_KEYS = {'thought', 'thoughts', 'thinking', 'reasoning', 'reasoning_content', 'reasoningContent', 'reasoning_text'}


def _scrub(v):
    if isinstance(v, dict):
        return {k: _scrub(x) for k, x in v.items() if k not in THOUGHT_KEYS}
    if isinstance(v, list):
        return [_scrub(x) for x in v if not (isinstance(x, dict) and str(x.get('type', '')).lower() in THOUGHT_KEYS)]
    return v


def scrub_output(out):
    """日志里只留非思考内容：逐行 JSON 去掉思考类事件（reasoning 等）和字段；非 JSON 行原样留（截短）。"""
    lines = []
    for line in out.splitlines():
        try:
            e = json.loads(line)
        except ValueError:
            lines.append(line[:2000]); continue
        if isinstance(e, dict):
            item = e.get('item') if isinstance(e.get('item'), dict) else {}
            kinds = {str(e.get('type', '')).lower(), str(e.get('event', '')).lower(), str(item.get('type', '')).lower()}
            if kinds & (THOUGHT_KEYS | {'reasoning_delta', 'thinking_delta'}):
                continue
        lines.append(json.dumps(_scrub(e), ensure_ascii=False))
    return '\n'.join(lines)


KEEP_ENV = {'CLAUDE_CODE_GIT_BASH_PATH'}


def member_env(cfg, m):
    """队员进程的环境：继承系统环境，但去掉调用方会话自己的 CLAUDE*/CODEX_* 变量；代理按配置设（不新增 NO_PROXY 例外）。"""
    env = {k: v for k, v in os.environ.items()
           if k.upper() in KEEP_ENV or not (k.upper().startswith('CLAUDE') or k.upper().startswith(('CODEX_', 'ANTHROPIC_')))}
    if cfg.get('proxy'):
        env['HTTPS_PROXY'] = env['HTTP_PROXY'] = cfg['proxy']
    env.update(KIMI_CODE_NO_AUTO_UPDATE='1', DISABLE_AUTOUPDATER='1',
               GROK_DISABLE_AUTOUPDATER='1', AGY_CLI_DISABLE_AUTO_UPDATE='1')
    if m.get('tool') == 'kimi' and m.get('effort') not in (None, 'default'):
        env['KIMI_MODEL_THINKING_EFFORT'] = m['effort']
    env['PYTHONUTF8'] = '1'
    return env


def kimi_session(cwd):
    """Kimi 新会话的编号：~/.kimi-code/session_index.jsonl 里按工作目录找（只读这一行元数据）。"""
    sid = None
    try:
        for line in open(Path.home() / '.kimi-code' / 'session_index.jsonl', encoding='utf-8', errors='replace'):
            try: e = json.loads(line)
            except ValueError: continue
            if os.path.normcase(os.path.normpath(e.get('workDir', ''))) == os.path.normcase(os.path.normpath(str(cwd))):
                sid = e.get('sessionId')
    except OSError:
        pass
    return sid


def invoke(cfg, m, prompt, dry=False, runner=run_process, new_session=False, request_phase=None):
    ident = hashlib.sha256((cfg['captain'] + '\0' + m['name']).encode()).hexdigest()[:24]
    statefile = Path(cfg['state_dir']) / (ident + '.json')
    record = dict(time=now(), caller=cfg['captain'], callee=m['name'], prompt=prompt,
                  reply='', session_id=None, exit_code=0, dry_run=dry, new_session=new_session,
                  environment_names=sorted(set(m.get('env', {})) | {'PWD'}))
    secrets = [v for k,v in os.environ.items() if re.search(r'KEY|TOKEN|SECRET|PASSWORD', k, re.I) and len(v) >= 8]
    def redact(value):
        if isinstance(value, str):
            for secret in secrets:
                value = value.replace(secret, '[REDACTED]')
            return value
        if isinstance(value, dict): return {k:redact(v) for k,v in value.items()}
        if isinstance(value, list): return [redact(v) for v in value]
        return value
    start = time.monotonic()
    def audit():
        record['elapsed_seconds'] = time.monotonic() - start
        clean = redact(record)
        record.clear(); record.update(clean)
        write_json(Path(cfg['log_dir']) / (uuid.uuid4().hex + '.json'), record)
    try:
        with Lock(statefile.with_suffix('.lock')):
            state = read_json(statefile) if statefile.exists() else {}
            identity = {k:v for k,v in m.items() if k != 'timeout'}
            fingerprint = hashlib.sha256(json.dumps(identity, sort_keys=True, ensure_ascii=False).encode()).hexdigest()
            legacy = hashlib.sha256(json.dumps(m, sort_keys=True, ensure_ascii=False).encode()).hexdigest()
            if state and state.get('fingerprint') not in (fingerprint, legacy):
                raise ValueError('队员配置已变更；请主办方核对并归档旧会话状态')
            new_session = new_session or state.get('force_new',False)
            record['new_session']=new_session
            sid = None if new_session else state.get('session_id')
            if state.get('uncertain') and not new_session:
                record['session_resolution_required']=True
                raise ValueError('上次会话编号不明；请主办方选择：session --config 配置 队员 --new-session，或 --session-id 编号；禁止自动另开')
            if cfg.get('match_config') and not sid and not new_session and m['tool'] != 'fake':
                raise ValueError('未找到原会话编号，请主办方核对续接状态')
            argv = command(m, sid)
            record['session_id'] = sid
            record['argv'] = argv
            record['cwd'] = m['cwd']
            if dry:
                record['reply'] = json.dumps(dict(argv=argv, windows_command_line=subprocess.list2cmdline(argv),
                    cwd=os.path.realpath(m['cwd']), stdin=prompt, environment_names=sorted(set(m.get('env', {})) | {'PWD'})), ensure_ascii=False, indent=2)
            else:
                if cfg.get('match_config'):
                    import team12
                    try:
                        _, dispatch_phase = team12.window(team12.load_match(cfg['match_config']))
                    except team12.Reject as e:
                        raise ValueError(str(e)) from None
                    if request_phase is not None and dispatch_phase != request_phase:
                        raise ValueError('原派活窗口已结束')
                env = member_env(cfg, m)
                for name, source in m.get('env', {}).items():
                    # 仅真实调用时加载；dry-run 和测试不读取任何密钥。
                    env[name] = (Path(source['value_file']).read_text(encoding='utf-8-sig').strip()
                                 if 'value_file' in source else str(Path(source['path_file']).resolve()))
                    if 'value_file' in source and env[name]: secrets.append(env[name])
                env['PWD'] = m['cwd']
                if m['tool'] == 'manual':
                    # 传话窗口默认按"队长调用队员"显示；主办方自己的调用由 runner 覆盖成本阶段的模式。
                    for k in ('A11_RELAY_SIGNAL', 'A11_RELAY_STAGE', 'A11_RELAY_CEILING', 'A11_RELAY_RANK'):
                        env.pop(k, None)
                    env.update(A11_RELAY_MODE='member', A11_RELAY_CALLER=cfg['captain'],
                               A11_RELAY_SESSION='new' if sid is None else 'continue')
                # 先持久化不确定标记，启动器意外退出也不会悄悄另开新会话。

                call_started=time.time()
                limit=float(m.get('timeout', 600))
                if cfg.get('match_config'):
                    end = dispatch_phase.get('deadline')
                elif cfg.get('clock_file'):
                    end = read_json(Path(m['cwd'])/cfg['clock_file']).get('deadline')
                else:
                    end = None
                deadline_limited = end is not None and float(end) - call_started <= limit
                if deadline_limited:
                    limit = float(end) - call_started
                    if limit <= 0:
                        raise ValueError('派活已截止')
                    record['timeout_capped_by_deadline'] = round(limit,3)
                write_json(statefile, dict(fingerprint=fingerprint, session_id=sid, uncertain=True))
                if m['tool']=='manual' and limit!=float(m.get('timeout', 600)):
                    argv=command(dict(m,timeout=limit),sid); record['argv']=argv
                text=('主办方固定配置：模型 ' + m['model'] + '，思考档位 ' + m['effort'] + '。\n' +
                      '比赛规则：只能修改自己的文件夹：' + m['cwd'] + '\n\n' + prompt)
                if m['tool'] in ARGV_PROMPT and len(text.encode('utf-16-le')) > 16000:
                    task_file = Path(m['cwd']) / ('主办方任务_' + uuid.uuid4().hex + '.txt')
                    task_file.write_text(text, encoding='utf-8')
                    text = '请先完整阅读本目录的 ' + task_file.name + '，然后按其中的说明执行。'
                if m['tool'] in ARGV_PROMPT:
                    argv=command(m,sid,text); record['argv']=argv[:-1]+['<提示词见 prompt>']; text=''
                try:
                    run_cwd = os.path.realpath(m['cwd'])
                    code, out, err = runner(argv, run_cwd, env, text, limit)
                except (OSError,RuntimeError,subprocess.SubprocessError):
                    code,out,err=70,'','启动或通信失败，仍尝试落盘元数据恢复'
                record.update(exit_code=code, stdout=scrub_output(out), stderr=err[-4000:])
                if runner is run_process:
                    record['process'] = getattr(run_process, 'last_process', {})
                decision_ended = (deadline_limited and code in (70, 124)
                                  and (code == 124 or record.get('process', {}).get('timeout'))
                                  and cfg.get('match_config') and dispatch_phase.get('phase') == 'round')
                found,reply,failed=None,'',False
                try: found,reply,failed=parse_output(m['tool'],out)
                except (ValueError,KeyError,TypeError):
                    record['output_truncated_or_invalid']=True
                    # JSONL末行截断时，先从完整事件中找编号。
                    if m['tool']!='claude':
                        valid=[]
                        for line in out.splitlines():
                            try: json.loads(line); valid.append(line)
                            except ValueError: pass
                        try: found,reply,failed=parse_output(m['tool'],'\n'.join(valid))
                        except (ValueError,KeyError,TypeError): pass
                preset=argv[argv.index('-s')+1] if m['tool']=='grok' and '-s' in argv else None
                if m['tool']=='kimi' and not found and not sid: found=kimi_session(os.path.realpath(m['cwd']))
                recovery=lookup(m,env,call_started,time.time(),found or sid or preset)
                recovery['output_id_available']=bool(found)
                disk=recovery.get('session_id')
                conflict=(found and sid and found!=sid) or (disk and found and disk!=found)
                sid=None if conflict else (found or disk or sid)
                record.update(session_id=sid,reply=reply,session_recovery=recovery,
                              session_resolution_required=not bool(sid))
                if m['tool'] == 'codex' and record.get('output_truncated_or_invalid'):
                    failed = True
                if failed and code==0: record['exit_code']=1
                if m['tool'] == 'codex' and code and not failed and sid:
                    record.update(exit_code=0, process_exit_code=code)
                if not sid or (code==0 and not reply):
                    record['error']='输出缺少会话编号或最终回复；'+('请主办方选择新开会话或人工指定编号' if not sid else '已保存可续接的会话编号')
                    record['exit_code']=code or 65
                if conflict: record['error']='输出/落盘/预期会话编号冲突，请主办方选择新开或人工指定'
                write_json(statefile,dict(fingerprint=fingerprint,session_id=sid,uncertain=not bool(sid),session_recovery=recovery))
                if code == 124 and record['exit_code']:
                    record['error'] = f"调用超时（上限 {m.get('timeout',600)} 秒）；已清理本次启动的进程树。"
                    if record.get('process',{}).get('remaining_pids'):
                        record['error'] += '仍有残留 PID，请主办方按日志检查。'
                if record['exit_code'] and m['tool'] == 'manual':
                    try: record['error'] = record.get('error', '') + '传话失败：' + json.loads(out.strip().splitlines()[-1])['error']['message'] + '。'
                    except (ValueError, KeyError, TypeError, IndexError): pass
                if decision_ended and (failed or not reply.strip()):
                    record.update(exit_code=124, reply='', error='决策时间到，本次调用已结束（队员没有交回回复）。')
                elif record['exit_code']:
                    record['error'] = record.get('error', '') + f" 工具失败（退出码 {record['exit_code']}）；详见主办方日志。"
    except Busy as e:
        record.update(exit_code=75, error=str(e))
    except (OSError, ValueError, KeyError) as e:
        # 避免把可能包含凭证的底层异常文本回显。
        record.update(exit_code=70, error=str(e) if isinstance(e, ValueError) else '启动或文件操作失败，请主办方检查路径及权限')
    audit()
    return record


def resolve_session(cfg,name,session_id=None,new_session=False):
    if bool(session_id)==bool(new_session): raise ValueError('必须二选一：--new-session 或 --session-id 编号')
    if session_id and not re.fullmatch(r'[A-Za-z0-9_-]{1,160}',session_id): raise ValueError('会话编号格式无效')
    ident=hashlib.sha256((cfg['captain']+'\0'+name).encode()).hexdigest()[:24]
    path=Path(cfg['state_dir'])/(ident+'.json')
    with Lock(path.with_suffix('.lock')):
        if not path.exists(): raise ValueError('没有待恢复的会话状态')
        state=read_json(path)
        state.update(session_id=session_id,uncertain=False,force_new=new_session,resolved_at=now())
        write_json(path,state)
        write_json(Path(cfg['log_dir'])/(uuid.uuid4().hex+'.json'),dict(time=now(),caller=cfg['captain'],callee=name,
            command='人工选择新开会话' if new_session else '人工指定编号',session_id=session_id,exit_code=0))


def task_dir(cfg):
    return Path(cfg['state_dir']) / 'tasks'


def tasks(cfg, names):
    return [(p, read_json(p)) for p in sorted(task_dir(cfg).glob('*.json'))
            if read_json(p)['member'] in names]


def clear_busy(cfg,name):
    if name not in {m['name'] for m in cfg['members']}:
        raise ValueError('未知队员')
    with Lock(Path(cfg['state_dir'])/'dispatch.lock', wait=5):
        for path,task in tasks(cfg,[name]):
            if task['status'] == 'done':
                continue
            if process_alive(task['pid']):
                raise ValueError(f"任务进程 {task['pid']} 仍在运行")
            task.update(status='done',finished=now(),result=dict(exit_code=70,reply='',error='任务进程已退出，主办方清除忙态'))
            write_json(path,task)
            print(f"{name} 已清除忙态，保留原任务及会话状态")


def execute_task(config, path):
    cfg = load(config)
    with Lock(Path(cfg['state_dir'])/'dispatch.lock', wait=5):
        t = read_json(path)
        if t['status'] == 'done':
            return t['result']
        t.update(status='running', pid=os.getpid())
        write_json(path,t)
    m = next(m for m in cfg['members'] if m['name'] == t['member'])
    try:
        result = invoke(cfg, m, t['prompt'], new_session=t['new_session'], request_phase=t.get('request_phase'))
    except Exception:
        result = dict(exit_code=70, reply='', error='调用或日志写入失败，请主办方检查')
    if t.get('notices') and result['exit_code'] == 0 and result.get('reply'):
        import team12
        try:
            team12.acknowledge_notices(cfg, m['name'], t['notices'])
        except (Busy, OSError):
            pass  # 已有最终回复，未能核销的提醒留给下次派活。
    t.update(status='done', result=result, finished=now()); write_json(path, t)
    return result


def dispatch(config, cfg, m, prompt, background=False, new_session=False, request_phase=None, notices=None):
    with Lock(Path(cfg['state_dir']) / 'dispatch.lock', wait=2):
        if any(t['status'] != 'done' for _, t in tasks(cfg, [m['name']])):
            # 忙调用也通过 invoke 的审计格式留档，但不碰会话。
            r = dict(time=now(), caller=cfg['captain'], callee=m['name'], prompt=prompt,
                     reply='', elapsed_seconds=0, session_id=None, exit_code=75, error='队员正忙')
            write_json(Path(cfg['log_dir']) / (uuid.uuid4().hex + '.json'), r)
            return r
        path = task_dir(cfg) / (uuid.uuid4().hex + '.json')
        write_json(path, dict(member=m['name'], prompt=prompt, new_session=new_session,
                             status='queued', collected=False, created=now(), pid=os.getpid(), request_phase=request_phase, notices=notices))
        if background:
            try:
                flags = (subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP) if os.name == 'nt' else 0
                p = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), '_worker',
                    '--config', str(Path(config).resolve()), '--task', str(path)],
                    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                    creationflags=flags, start_new_session=os.name != 'nt', close_fds=True)
            except OSError:
                t = read_json(path); t.update(status='done', result=dict(exit_code=70, reply='', error='后台进程启动失败'))
                write_json(path, t)
                return t['result']
            task = read_json(path)
            task['pid'] = p.pid
            write_json(path,task)
            return dict(exit_code=0, queued=True, reply='已排队：' + m['name'] + '；用 wait 收取最终回复。')
    r = execute_task(config, path)
    with Lock(Path(cfg['state_dir']) / 'dispatch.lock', wait=2):
        t = read_json(path); t['collected'] = True; write_json(path, t)
    return r


def collect(cfg, names, timeout=600):
    deadline = time.monotonic() + timeout
    # 固定此次等待的任务集合；不会收走之后新派发的任务。
    selected = [p for p,t in tasks(cfg, names) if not t['collected']]
    while any(read_json(p)['status'] != 'done' for p in selected):
        if time.monotonic() >= deadline:
            return dict(exit_code=124, reply='', error='等待超时；任务仍在运行，可再次 wait，不会取消任务')
        time.sleep(.1)
    replies, code = [], 0
    with Lock(Path(cfg['state_dir']) / 'dispatch.lock', wait=2):
        for p in selected:
            t = read_json(p)
            if t['collected']: continue
            r = t['result']; code = code or r['exit_code']
            phase = t.get('request_phase') or {}
            stage = {'dev': '开发阶段', 'postgame': '赛后'}.get(phase.get('phase'))
            if phase.get('phase') == 'round' and phase.get('round') is not None:
                stage = f"第 {phase['round']} 回合"
            label = f'（{stage}派的活）' if stage else ''
            replies.append(t['member'] + label + '：\n' + (r.get('error', '') + '\n' if r.get('error') else '') + r['reply'])
            t['collected'] = True; write_json(p,t)
    return dict(exit_code=code, reply='\n\n'.join(replies) or '没有未取结果。')


def status(cfg):
    lines = []
    for m in cfg['members']:
        ts = [t for _,t in tasks(cfg, [m['name']])]
        busy = any(t['status'] != 'done' for t in ts)
        unread = sum(t['status'] == 'done' and not t['collected'] for t in ts)
        lines.append(f"{m['name']}：{'忙' if busy else '空闲'}；未取结果 {unread}")
    return '\n'.join(lines)


def generate(config):
    cfg = load(config)
    dest = Path(cfg['captain_dir'])
    dest.mkdir(parents=True, exist_ok=True)
    script = Path(__file__).resolve()
    # ASCII 批处理避免 cmd 的 UTF-8/GBK 脚本解码差异；中文绝对路径放进 Python 文件。
    (dest / 'ask.cmd').write_text('@echo off\r\npython "%~dp0ask_entry.py" %*\r\nexit /b %errorlevel%\r\n', encoding='ascii')
    (dest / 'ask_entry.py').write_text('import runpy,sys\n' +
        f'sys.path.insert(0, {str(script.parent)!r})\n' +
        'if any(x == "--config" or x.startswith("--config=") for x in sys.argv[1:]): raise SystemExit("配置由主办方固定")\n' +
        'action = sys.argv.pop(1) if len(sys.argv)>1 and sys.argv[1] in ("ask","wait","status") else "ask"\n' +
        f'sys.argv[1:1] = [action, "--config", {str(Path(config).resolve())!r}]\n' +
        f'runpy.run_path({str(script)!r}, run_name="__main__")\n', encoding='utf-8')
    lines = ['# 队长须知：你有 ' + str(len(cfg['members'])) + ' 个子代理', '', '队长：' + cfg['captain'], '', '子代理（模型 / 档位 / 工作目录 / 超时秒）：', '']
    lines += [f"- {m['name']}：{m['model']} / {m['effort']} / `{m['cwd']}` / {m.get('timeout', 600)}" for m in cfg['members']]
    lines += ['', '在本目录的 cmd 终端使用（文件/标准输入为 UTF-8）：', '```bat',
        'ask.cmd 队员名 "简短任务书"', 'ask.cmd 队员名 --file "长任务书.txt"',
        'type "长任务书.txt" | ask.cmd 队员名 --stdin',
        'ask.cmd 队员A --file a.txt --background', 'ask.cmd 队员B --file b.txt --background',
        'ask.cmd wait 队员A 队员B --timeout 600', 'ask.cmd status',
        'ask.cmd 队员名 --file 任务.txt --dry-run', '```', '',
        '子代理只收到任务书和自己的历史，不继承你的上下文；只交还最终回复。产物由你去队员目录只读查看，过程由主办方记录。',
        '默认同步派发并等待；默认续接上次会话，追加任务相当于 SendMessage。模型、档位和目录固定。',
        '并行可按示例后台派发后统一 wait，也可多个终端同时同步调用。wait 可指定多人，不填名字等全部；status 查看忙闲和未取结果。',
        '同一队员忙时不能追加或换会话，第二次立即报忙。wait 超时不取消任务，可再次收取；任务本身按上表超时。退出码75表示忙、124表示超时，其余非零也表示失败。',
        '每人只能修改自己的文件夹。不要修改入口、配置、状态或日志，不把密钥放进任务；遇到状态不明找主办方。']
    slow = [m['name'] for m in cfg['members'] if m['tool'] == 'manual']
    if slow:
        lines += ['', '子代理 ' + '、'.join(slow) + ' 的任务书由主办方转交、回复也要几分钟才回来。你的终端命令如果有时限，'
                  '派给他们时用 --background，再用 wait --timeout 收取（wait 超时不会取消任务，可以反复 wait）。']
    (dest / '队长须知_调用队员.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='action', required=True)
    g = sub.add_parser('generate'); g.add_argument('--config', required=True)
    a = sub.add_parser('ask',allow_abbrev=False); a.add_argument('--config', required=True); a.add_argument('member')
    src = a.add_mutually_exclusive_group(required=True)
    src.add_argument('prompt', nargs='?'); src.add_argument('--file'); src.add_argument('--stdin', action='store_true')
    a.add_argument('--dry-run', action='store_true')
    a.add_argument('--background', action='store_true', help='后台派发，用 wait 收结果')
    w = sub.add_parser('wait', allow_abbrev=False, help='收取最终回复，可指定多个队员')
    w.add_argument('--config', required=True); w.add_argument('members', nargs='*'); w.add_argument('--timeout', type=float, default=600)
    s = sub.add_parser('status', allow_abbrev=False, help='查看忙闲和未取结果'); s.add_argument('--config', required=True)
    recovery=sub.add_parser('session',help='主办方明确选择新开会话或人工编号')
    recovery.add_argument('--config',required=True); recovery.add_argument('member')
    choice=recovery.add_mutually_exclusive_group(required=True)
    choice.add_argument('--new-session',action='store_true'); choice.add_argument('--session-id')
    clear = sub.add_parser('清忙',help='主办方确认中断任务的进程已退出后清除忙态')
    clear.add_argument('--config',required=True)
    clear.add_argument('member')
    worker = sub.add_parser('_worker', help=argparse.SUPPRESS)
    worker.add_argument('--config', required=True); worker.add_argument('--task', required=True)
    args = p.parse_args()
    try:
        if args.action == 'generate':
            generate(args.config); return 0
        cfg = load(args.config)
        if args.action in ('ask', 'status', 'wait') and cfg.get('notice_dir'):
            # 可选：比赛期间给队长的一次性主办方通知（只显示一次）
            note = Path(cfg['notice_dir']) / (str(cfg.get('team', '')) + '.txt')
            shown = note.with_suffix('.shown')
            if note.exists() and not shown.exists():
                try:
                    print('【主办方通知】' + note.read_text(encoding='utf-8-sig').strip(), flush=True)
                    print('', flush=True)
                    shown.write_text(time.strftime('%Y-%m-%d %H:%M:%S'), encoding='utf-8')
                except OSError:
                    pass
        if args.action == '清忙':
            clear_busy(cfg,args.member); return 0
        if args.action=='session':
            if args.member not in {m['name'] for m in cfg['members']}: raise ValueError('未知队员')
            resolve_session(cfg,args.member,session_id=args.session_id,new_session=args.new_session)
            print('已保存主办方的会话选择'); return 0
        if args.action == '_worker': return execute_task(args.config, args.task)['exit_code']
        if args.action == 'status': print(status(cfg)); return 0
        if args.action == 'wait':
            names = args.members or [m['name'] for m in cfg['members']]
            if set(names) - {m['name'] for m in cfg['members']}: raise ValueError('未知队员')
            r = collect(cfg, names, args.timeout)
            print(r['reply'])
            if r.get('error'): print(r['error'], file=sys.stderr)
            return r['exit_code']
        m = next((m for m in cfg['members'] if m['name'] == args.member), None)
        if m is None:
            raise ValueError('未知队员')
        prompt = Path(args.file).read_text(encoding='utf-8-sig') if args.file else (sys.stdin.buffer.read().decode('utf-8-sig') if args.stdin else args.prompt)
        notices, raw, request_phase = None, prompt, None
        if cfg.get('match_config'):
            import team12
            try:
                request_phase = team12.phase(team12.load_match(cfg['match_config']))
                prompt, notices = team12.before_ask(cfg, m, prompt, args.dry_run)
            except team12.Reject as e:
                print(str(e), file=sys.stderr); return 67
        r = (invoke(cfg, m, prompt, True) if args.dry_run else
             dispatch(args.config, cfg, m, prompt, args.background, request_phase=request_phase, notices=notices))
        if cfg.get('match_config') and not args.dry_run:
            state = ('已排队，送达情况请用 wait 确认' if r.get('queued') else
                     '已收到队员回复' if r['exit_code'] == 0 else '调用未完成')
            # 命令行参数里的换行会被 cmd 截断（队长自己看不出来），所以报一下实际发出的长度
            print(f"（主办方：你的任务书 {raw.strip().count(chr(10)) + 1} 行、{len(raw.strip())} 字，{m['name']}：{state}；"
                  "多行的任务书请写进文件用 --file 传）", file=sys.stderr)
        print(r['reply'])
        if r.get('error'): print(r['error'], file=sys.stderr)
        return r['exit_code']
    except Busy:
        print('派发状态正被更新，请重试', file=sys.stderr); return 75
    except (ValueError, OSError) as e:
        print('配置或输入错误：' + str(e), file=sys.stderr); return 64


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8'); sys.stderr.reconfigure(encoding='utf-8')
    sys.exit(main())
