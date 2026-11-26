"""会话元数据后备查询；不加载消息/思考字段，不读取auth或config。"""
import json
import os
from pathlib import Path
import sqlite3
from datetime import datetime


def pathkey(p): return os.path.normcase(os.path.abspath(str(p)))
def epoch(t):
    if isinstance(t,(float,int)): return t/1000 if t>100000000000 else t
    try: return datetime.fromisoformat(t.replace('Z','+00:00')).timestamp()
    except (ValueError,TypeError,AttributeError): return 0


class Metadata:
    """流式JSON投影：未知值按字节跳过，不解码、不构造消息及思考字符串。"""
    fields={'id','session_id','sessionId','cwd','timestamp','type'}
    def __init__(self,f): self.f=f; self.c=f.read(1)
    def next(self): self.c=self.f.read(1)
    def whitespace(self):
        while self.c and self.c in b' \r\n\t': self.next()
    def string(self,keep=False):
        value=bytearray(b'"') if keep else None
        self.next(); escaped=False
        while self.c:
            b=self.c
            if keep: value.extend(b)
            self.next()
            if b==b'"' and not escaped: break
            escaped=(not escaped) if b==b'\\' else False
        return json.loads(value.decode('utf-8')) if keep else None
    def skip(self):
        self.whitespace()
        if self.c==b'"': self.string(); return
        if self.c in (b'{',b'['):
            end=b'}' if self.c==b'{' else b']'; self.next()
            while self.c and self.c!=end:
                if self.c in b' \t\r\n,:': self.next()
                else: self.skip()
            if self.c: self.next()
        else:
            while self.c and self.c not in b',}]\r\n ': self.next()
    def obj(self,payload=False):
        self.whitespace()
        if self.c!=b'{': self.skip(); return {}
        out={}; self.next(); self.whitespace()
        while self.c and self.c!=b'}':
            if self.c!=b'"': raise ValueError('元数据JSON结构无效')
            key=self.string(True); self.whitespace()
            if self.c!=b':': raise ValueError('元数据JSON结构无效')
            self.next(); self.whitespace()
            if key=='payload' and not payload: out[key]=self.obj(True)
            elif key in self.fields and self.c==b'"': out[key]=self.string(True)
            else: self.skip()
            self.whitespace()
            if self.c==b',': self.next(); self.whitespace()
            else: break
        if self.c==b'}': self.next()
        return out


def roots(m,env):
    if 'session_roots' in m: return [Path(x) for x in m['session_roots']]
    home=Path.home()
    if m['tool']=='manual': return []
    if m['tool']=='grok': return [Path(env.get('GROK_HOME',home/'.grok'))/'sessions']
    if m['tool']=='codex': return [Path(env.get('CODEX_HOME',home/'.codex'))/'sessions']
    if m['tool']=='claude':
        configured=env.get('CLAUDE_CONFIG_DIR')
        return [Path(configured)/'projects'] if configured else [p/'projects' for p in home.glob('.claude*') if p.is_dir()]
    return [Path(env.get('XDG_DATA_HOME',home/'.local/share'))/'opencode'/'opencode.db']


def lookup(m,env,started,finished,expected=None):
    if m['tool'] in ('kimi','dsh','agy','fake'):
        # 团队赛新增的几家：会话编号从输出里取（kimi 续接时就是传进去的编号），这里不查落盘记录。
        return dict(session_id=None,status='not_supported',candidates=[],roots=[],errors=[])
    if m['tool']=='manual':
        # 人工席位没有可查的会话文件；编号固定，主办人在同一个对话里续。
        sid='manual-'+m['name']
        return dict(session_id=sid,status='found',candidates=[sid],roots=[],errors=[])
    places=roots(m,env); found={}; errors=[]; cwd=pathkey(m['cwd'])
    def accept(sid,directory,created,updated,source):
        if not sid or not directory or pathkey(directory)!=cwd: return
        if not started-2<=epoch(updated)<=finished+2: return
        if sid!=expected and not started-2<=epoch(created)<=finished+2: return
        found[sid]=source
    for root in places:
        try:
            if m['tool']=='grok':
                # ~/.grok/sessions/<URL 编码的工作目录>/<会话编号>/；只看目录名和修改时间，不读会话内容。
                if not root.is_dir(): continue
                from urllib.parse import unquote
                for folder in root.iterdir():
                    if not folder.is_dir(): continue
                    for s in folder.iterdir():
                        if not s.is_dir(): continue
                        modified=max([s.stat().st_mtime]+[f.stat().st_mtime for f in s.glob('*.jsonl')])
                        accept(s.name,unquote(folder.name),s.stat().st_ctime,modified,str(s))
            elif m['tool']=='opencode':
                if root.suffix!='.db' or not root.is_file(): continue
                # 只查询会话表四个元数据列；绝不访问message/part/auth。
                db=sqlite3.connect(root.resolve().as_uri()+'?mode=ro',uri=True,timeout=1)
                try:
                    db.execute('PRAGMA query_only=ON')
                    columns={r[1] for r in db.execute('PRAGMA table_info(session)')}
                    if not {'id','directory','time_created','time_updated'}<=columns: raise ValueError('会话表结构变化')
                    for sid,directory,created,updated in db.execute('SELECT id,directory,time_created,time_updated FROM session WHERE time_updated >= ?',(int((started-2)*1000),)):
                        accept(sid,directory,created,updated,str(root))
                finally: db.close()
            else:
                if not root.is_dir(): continue
                paths=root.rglob('rollout-*.jsonl') if m['tool']=='codex' else root.glob('*/*.jsonl')
                for p in paths:
                    modified=p.stat().st_mtime
                    if not started-2<=modified<=finished+2: continue
                    with p.open('rb') as f:
                        reader=Metadata(f)
                        for _ in range(32 if m['tool']=='claude' else 1):
                            if not reader.c: break
                            data=reader.obj(); reader.whitespace()
                            if m['tool']=='codex':
                                if data.get('type')!='session_meta': break
                                data=data.get('payload',{})
                            sid=data.get('sessionId',data.get('session_id',data.get('id')))
                            if sid and data.get('cwd'):
                                accept(sid,data['cwd'],data.get('timestamp'),modified,str(p)); break
        except (OSError,ValueError,sqlite3.Error): errors.append(str(root)+':元数据缺失/格式变化/读取失败')
    candidates=sorted(found)
    # 多个匹配编号不能猜最新；已明确续接的编号可唯一消歧。
    chosen=expected if expected in found else (candidates[0] if len(candidates)==1 else None)
    return dict(session_id=chosen,status='found' if chosen else ('ambiguous' if candidates else 'not_found'),
                candidates=candidates,roots=[str(p) for p in places],errors=errors)
