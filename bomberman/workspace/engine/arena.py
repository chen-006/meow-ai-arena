"""Match CLI: built-in bots or external persistent text-protocol processes."""
import argparse, hashlib, json, os, platform, queue, re, subprocess, sys, threading, time, signal
from collections import deque
from pathlib import Path
from engine import Game, config_for, VERSION

BASE=Path(__file__).resolve().parent
BUILTINS=set()

class ProcessBot:
    def __init__(self,cmd,header):
        self.q=queue.Queue(maxsize=32);self.bad=threading.Event();self.writeq=queue.Queue(maxsize=2);self.failure=''
        self.stderr_tail=deque(maxlen=16)
        self.p=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                                creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0),bufsize=0,
                                start_new_session=os.name!='nt')
        def read_stderr():
            try:
                while True:
                    chunk=self.p.stderr.read(512)
                    if not chunk: break
                    self.stderr_tail.append(chunk)
            except (OSError,ValueError): pass
        threading.Thread(target=read_stderr,daemon=True).start()
        def reader():
            try:
                while True:
                    line=self.p.stdout.readline(257)
                    if not line:
                        # Preserve already-received valid output, then report EOF immediately.
                        try:self.q.put_nowait((time.perf_counter(),None))
                        except queue.Full:pass
                        break
                    if len(line)>256 or not line.endswith(b'\n'):self.failure='stdout_line_too_long';self.bad.set();break
                    self.q.put_nowait((time.perf_counter(),line))
            except (OSError,ValueError,queue.Full) as e:self.failure=type(e).__name__+' while reading stdout';self.bad.set()
        threading.Thread(target=reader,daemon=True).start()
        def writer():
            try:
                while True:
                    data=self.writeq.get()
                    if data is None:return
                    data=memoryview(data)
                    while data:
                        written=self.p.stdin.write(data)
                        if not written:raise OSError('stdin closed')
                        data=data[written:]
                    self.p.stdin.flush()
            except (OSError,ValueError) as e:self.failure=type(e).__name__+' while writing stdin';self.bad.set()
        threading.Thread(target=writer,daemon=True).start()
        self.header=header
    def send(self,text):
        self.sent=time.perf_counter()
        data=(self.header+text).encode('ascii');self.header=''
        # A non-reading bot must not block the whole match on a full stdin pipe.
        try:self.writeq.put_nowait(data)
        except queue.Full:self.failure='stdin_backpressure';self.bad.set()
    def receive(self,turn,limit):
        deadline=self.sent+limit/1000
        try:
            if self.bad.is_set():return None,'io:'+self.failure,0
            stamp,line=self.q.get(timeout=max(0,deadline-time.perf_counter()))
            if line is None:return None,'stdout_closed',max(0,(stamp-self.sent)*1000)
            if stamp<self.sent or stamp>deadline:return None,'timing',max(0,(stamp-self.sent)*1000)
            match=re.fullmatch(rb'(0|[1-9][0-9]{0,5}) ([UDLRS]) ([01])\r?\n',line)
            if not match or int(match[1])!=turn:return None,'protocol',(stamp-self.sent)*1000
            return (match[2].decode(),int(match[3])),'',(stamp-self.sent)*1000
        except queue.Empty:
            if self.bad.is_set():return None,'io:'+self.failure,limit
            if self.p.poll() is not None:return None,'process_exit',limit
            return None,'timeout',limit
    def diagnostic(self):
        return dict(returncode=self.p.poll(),stderr_tail=b''.join(self.stderr_tail).decode('utf-8',errors='replace'))

    def close(self):
        # Terminate the bot's process group/tree, not unrelated shell or agent processes.
        try:
            if os.name!='nt':
                try:os.killpg(self.p.pid,signal.SIGKILL)
                except ProcessLookupError:pass
            elif self.p.poll() is None:
                try:subprocess.run(['taskkill','/PID',str(self.p.pid),'/T','/F'],stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL,timeout=3,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
                except (OSError,subprocess.TimeoutExpired):pass
            if self.p.poll() is None:self.p.kill()
            self.p.wait(timeout=3)
        except (OSError,subprocess.TimeoutExpired):
            pass
        try:self.writeq.put_nowait(None)
        except queue.Full:pass
        for stream in (self.p.stdin,self.p.stdout,self.p.stderr):
            try:stream.close()
            except (OSError,ValueError):pass

def command(label):
    p=Path(label).resolve()
    if not p.exists():raise FileNotFoundError(p)
    return [sys.executable,str(p)] if p.suffix=='.py' else [str(p)]

def play(labels,seed=0,replay=None,move_ms=50,initial_ms=2000):
    if not 2<=len(labels)<=10:raise ValueError('between 2 and 10 bots required')
    if move_ms<=0 or initial_ms<=0:raise ValueError('time limits must be positive')
    pairs=[s.split('=',1) if '=' in s else [s,s] for s in labels]
    names=[p[0] for p in pairs];labels=[p[1] for p in pairs]
    if any(not n for n in names) or len(set(names))!=len(names):raise ValueError('bot names must be nonempty and unique')
    g=Game(seed,config_for(len(labels)));frames=[g.snapshot()]
    processes={};errors=[0]*len(labels);max_ms=[0.0]*len(labels);diagnostics=[]
    started=time.perf_counter();initial_failures=[]
    manifest={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [BASE/'engine.py',BASE/'arena.py']}
    for i,label in enumerate(labels):
        if label not in BUILTINS:
            try:manifest[f'bot_{i}']=hashlib.sha256(Path(label).read_bytes()).hexdigest()
            except OSError as e:manifest[f'bot_{i}']=dict(unavailable=str(e))
    try:
        for i,label in enumerate(labels):
            if label not in BUILTINS:
                try:processes[i]=ProcessBot(command(label),g.header(i))
                except OSError as e:
                    errors[i]+=1
                    initial_failures.append(i);diagnostics.append(dict(turn=0,player=i,error='launch',detail=str(e)))
        while not g.over():
            actions=[('S',0)]*len(labels);forfeits=initial_failures[:] if g.turn==0 else []
            requested=[None]*len(labels);timings=[None]*len(labels);turn_diagnostics=[]
            state=g.state_text()
            for i,b in processes.items():
                if g.players[i]['alive']:b.send(state)
            for i,b in processes.items():
                if not g.players[i]['alive']:continue
                a,err,ms=b.receive(g.turn,initial_ms if g.turn==0 else move_ms)
                timings[i]=ms
                max_ms[i]=max(max_ms[i],ms)
                if err:
                    errors[i]+=1
                    detail=dict(turn=g.turn,player=i,error=err,**b.diagnostic())
                    diagnostics.append(detail);turn_diagnostics.append(detail)
                    # Fail closed on any protocol/timeout error. No stale-output resync ambiguity.
                    forfeits.append(i)
                else:actions[i]=a;requested[i]=a
            frame=g.step(actions,forfeits);frame['actions']=actions;frame['forfeits']=forfeits;frames.append(frame)
            frame.update(requested_actions=requested,response_ms=timings,diagnostics=turn_diagnostics,input_text=state)
            for i in list(processes):
                if not g.players[i]['alive']:processes.pop(i).close()
        result=g.result();result.update(names=names,bot_specs=labels,errors=errors,max_ms=max_ms,diagnostics=diagnostics,
                                       elapsed_seconds=time.perf_counter()-started,version=VERSION)
        result['hashes']=manifest
        result['valid_for_scoring']=not initial_failures
        result['environment']=dict(platform=platform.platform(),python=sys.version.split()[0],executable=sys.executable,
                                   clock='time.perf_counter (monotonic wall time)',move_ms=move_ms,initial_ms=initial_ms)
        if replay:
            payload=dict(version=VERSION,result=result,frames=frames)
            path=Path(replay);path.parent.mkdir(parents=True,exist_ok=True)
            path.with_suffix('.json').write_text(json.dumps(payload,ensure_ascii=False,separators=(',',':')),encoding='utf-8')
            template=(BASE.parent/'viewer/index.html').read_text(encoding='utf-8')
            safe=json.dumps(payload,ensure_ascii=False,separators=(',',':')).replace('<','\\u003c')
            path.with_suffix('.html').write_text(template.replace('/*REPLAY_DATA*/null',safe),encoding='utf-8')
        return result
    finally:
        for b in processes.values():b.close()

def inspect_replay(path, turn=0, context=2):
    """Plain-text replay inspection; never executes a bot or loads its code."""
    data=json.loads(Path(path).read_text(encoding='utf-8'))
    frames=data['frames'];names=data['result']['names']
    if context<0 or context>20:raise ValueError('context must be between 0 and 20')
    if not 0<=turn<len(frames)-1:raise ValueError(f'action turn must be 0..{len(frames)-2}')
    lines=[f"REPLAY {data.get('version','?')} | seed={data['result']['seed']}",
           'Coordinates: x right, y down, zero-based. Each block shows the board BEFORE the action.',
           'Requested action is distinct from actual movement. Internal intentions/search are not recorded.']
    for i in range(max(0,turn-context),min(len(frames)-1,turn+context+1)):
        before,after=frames[i],frames[i+1]
        lines += ['',f'ACTION {before["turn"]} -> STATE {after["turn"]}',
                  '    '+''.join(str(x%10) for x in range(len(before['board'][0])))]
        lines += [f'{y:02d}  {row}' for y,row in enumerate(before['board'])]
        for j,(a,b) in enumerate(zip(before['players'],after['players'])):
            if not a['alive']:continue
            action=after.get('requested_actions',after.get('actions',[]))
            request=f'{action[j][0]} place={action[j][1]}' if j<len(action) and action[j] is not None else 'no valid response'
            lines.append(f'P{j} {names[j]}: ({a["x"]},{a["y"]}) requested {request}; actual ({b["x"]},{b["y"]}); alive={b["alive"]}; reason={b.get("reason","")}')
        lines.append('BOMBS before: '+json.dumps(before['bombs'],ensure_ascii=False))
        lines.append('FLAMES before [x,y,last_lethal_turn]: '+json.dumps(before['flames'],ensure_ascii=False))
        lines.append('EVENTS after: '+json.dumps(after.get('events',[]),ensure_ascii=False))
        lines.append('DIAGNOSTICS: '+json.dumps(after.get('diagnostics',[]),ensure_ascii=False))
    return '\n'.join(lines)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--bots',nargs='+',default=[])
    ap.add_argument('--seed',type=int,default=1);ap.add_argument('--replay',default='replays/demo.html')
    ap.add_argument('--move-ms',type=int,default=50);ap.add_argument('--initial-ms',type=int,default=2000)
    ap.add_argument('--inspect',help='inspect a saved replay as plain text; does not run a match')
    ap.add_argument('--turn',type=int,default=0);ap.add_argument('--context',type=int,default=2)
    args=ap.parse_args()
    if args.inspect:
        try:print(inspect_replay(args.inspect,args.turn,args.context))
        except (OSError,ValueError,KeyError,IndexError) as e:ap.error(str(e))
        return
    if not 2<=len(args.bots)<=10:ap.error('between 2 and 10 bots required')
    print(json.dumps(play(args.bots,args.seed,args.replay,args.move_ms,args.initial_ms),ensure_ascii=False,indent=2))
if __name__=='__main__':main()
