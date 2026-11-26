"""私有OpenCode服务：显式就绪、可诊断启动失败、真实usage输出；不落盘服务口令。"""
import subprocess,threading,queue,json,time,os,secrets,re,sys
def safe_error(e):
 """结构化错误只留 type、status、message（截短），不带响应体、请求头或模型输出。"""
 if not isinstance(e,dict):return dict(type='unknown',message=str(e or '')[:300])
 return {k:(e[k] if k=='status' else str(e[k])[:300]) for k in ('type','status','message') if e.get(k) is not None}
class Server:
 def __init__(self,exe,cwd,env):
  self.exe=exe;self.cwd=cwd;self.env=env.copy();self.password=secrets.token_urlsafe(32);self.env['OPENCODE_PASSWORD']=self.password;self.env['OPENCODE_SERVER_PASSWORD']=self.password;self.q=queue.Queue();self.p=subprocess.Popen([exe,'serve','--hostname','127.0.0.1','--port','0'],cwd=cwd,env=self.env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
  def read():
   for l in self.p.stdout:self.q.put(l.decode('utf-8','replace'))
  self.reader=threading.Thread(target=read,daemon=True);self.reader.start();end=time.monotonic()+20;self.url=None;lines=[]
  try:
   while time.monotonic()<end:
    if self.p.poll() is not None and self.q.empty():raise RuntimeError('私有服务提前退出：'+''.join(lines)[-600:])
    try:line=self.q.get(timeout=.2)
    except queue.Empty:continue
    lines.append(line);m=re.search(r'http://127\.0\.0\.1:\d+',line)
    if m:self.url=m[0];break
   if not self.url:raise RuntimeError('私有服务20秒未就绪')
  except BaseException:self.close();raise
 def api(self,op,sid,body=None,params=None):
  cmd=[self.exe,'api','--server',self.url,op,'--param','sessionID='+sid]
  for key,value in (params or {}).items():cmd+=['--param',key+'='+str(value)]
  if body is not None:cmd+=['--data',json.dumps(body)]
  p=subprocess.run(cmd,cwd=self.cwd,env=self.env,capture_output=True,text=True,encoding='utf-8',timeout=30)
  if p.returncode:raise RuntimeError(p.stderr[:400]+p.stdout[:400])
  return json.loads(p.stdout)
 def close(self):
  if getattr(self,'p',None) and self.p.poll() is None:self.p.terminate();self.p.wait(timeout=10)
  if getattr(self,'reader',None):
   self.reader.join(2)
   if not self.reader.is_alive():self.p.stdout.close()
 def run(self,args,text,output=True):
  args=[a for a in args if a!='--standalone'];args+=['--server',self.url]
  prior={}
  if '--session' in args:
   try:prior=self.api('session.get',args[args.index('--session')+1])['data']
   except Exception:pass
  p=subprocess.Popen([self.exe,*args],cwd=self.cwd,env=self.env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE);p.stdin.write(text.encode());p.stdin.close();sid=None;events=[];tail=[]
  def err():
   for l in p.stderr:
    clean=l.decode('utf-8','replace').replace(self.password,'[REDACTED]');sys.stderr.write(clean);tail.append(clean);del tail[:-20]
  err_reader=threading.Thread(target=err,daemon=True);err_reader.start()
  stop=threading.Event();emit_lock=threading.Lock()
  def emit(e):
   with emit_lock:print(json.dumps(e,ensure_ascii=False),flush=True)
  def poll_usage():
   while not stop.wait(2):
    if not sid:continue
    try:
     meta=self.api('session.get',sid)['data'];cost=meta.get('cost')
     if isinstance(cost,(int,float)) and output:emit(dict(type='usage_snapshot',session_id=sid,total_cost_usd=max(0,cost-prior.get('cost',0)),usage_scope='this_invocation_delta',usage=meta.get('tokens',{})))
    except Exception:pass
  poll=threading.Thread(target=poll_usage,daemon=True);poll.start()
  for l in p.stdout:
   try:e=json.loads(l)
   except ValueError:continue
   sid=e.get('sessionID',sid)
   if e.get('type') not in ('reasoning','reasoning_delta'):
    if output:emit(e)
    events.append(e)
  code=p.wait();stop.set();poll.join(35);p.stdout.close();err_reader.join(5)
  if not err_reader.is_alive():p.stderr.close()
  else:raise RuntimeError('子CLI标准错误管道未关闭')
  # 本次内层 CLI 的终态：退出码、结构化错误（只留 type/status/message）、脱敏后的 stderr 末尾。
  self.last_run=dict(exit_code=code,errors=[safe_error(e.get('error')) for e in events if e.get('type')=='error'],stderr_tail=''.join(tail)[-600:])
  if sid:
   try:
    meta=self.api('session.get',sid)['data']
    def delta(after,before):
     if isinstance(after,dict):return {k:delta(v,before.get(k,0) if isinstance(before,dict) else 0) for k,v in after.items()}
     return max(0,after-before) if isinstance(after,(int,float)) and isinstance(before,(int,float)) else after
    event=dict(type='result',usage=delta(meta.get('tokens',{}),prior.get('tokens',{})),total_cost_usd=delta(meta.get('cost'),prior.get('cost',0)),session_id=sid,usage_scope='this_invocation_delta')
    if output:emit(event)
   except Exception:pass
  return code,sid,events
if __name__=='__main__':
 server=None
 try:
  sys.stdout.reconfigure(encoding='utf-8');sys.stderr.reconfigure(encoding='utf-8')
  server=Server(sys.argv[1],os.getcwd(),os.environ);code,_,_=server.run(sys.argv[2:],sys.stdin.buffer.read().decode('utf-8-sig'));raise SystemExit(code)
 except Exception as e:
  print(json.dumps(dict(type='error',error=dict(type='private_server_start',message=str(e))),ensure_ascii=False));raise SystemExit(1)
 finally:
  if server:server.close()
