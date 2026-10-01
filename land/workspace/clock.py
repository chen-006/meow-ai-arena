"""Development timer; never infer the unpack time from file mtime.
Explicit organizer start is preferred. Legacy birthtime/ctime is frozen once.
"""
import argparse
import datetime
import json
import os
import platform
import time
from pathlib import Path

MARKER = '.development-clock.json'

def boot_id():
    try:return Path('/proc/sys/kernel/random/boot_id').read_text().strip()
    except OSError:return None

def initialize(root, explicit=False, wall=None, monotonic=None):
    root=Path(root);marker=root/MARKER
    if marker.exists():return json.loads(marker.read_text(encoding='utf-8'))
    now=time.time();mono=time.monotonic()
    if explicit:
        start=now if wall is None else wall
        start_mono=mono if monotonic is None else monotonic
        source='explicit_start'
    else:
        st=root.stat();start=getattr(st,'st_birthtime',None) or st.st_ctime
        source='directory_birthtime' if getattr(st,'st_birthtime',None) or os.name=='nt' else 'directory_ctime_estimate'
        start_mono=mono-max(0,now-start)
    state=dict(version=1,start_unix=start,start_monotonic=start_mono,source=source,
               host=platform.node(),system=platform.system(),boot_id=boot_id(),boot_epoch=now-mono,
               suggested_seconds=3600,hard_stop_seconds=5400)
    try:
        with marker.open('x',encoding='utf-8') as f:json.dump(state,f,indent=2)
    except FileExistsError:return json.loads(marker.read_text(encoding='utf-8'))
    return state

def read_clock(root, explicit=False):
    state=initialize(root,explicit);now=time.time();mono=time.monotonic();current_boot=boot_id()
    same_host=state['host']==platform.node() and state['system']==platform.system()
    same_boot=(state['boot_id']==current_boot if state['boot_id'] and current_boot
               else abs((now-mono)-state['boot_epoch'])<120)
    use_mono=same_host and same_boot and mono>=state['start_monotonic']
    elapsed=max(0,mono-state['start_monotonic'] if use_mono else now-state['start_unix'])
    return dict(now_utc=datetime.datetime.fromtimestamp(now,datetime.timezone.utc).isoformat(),
                start_utc=datetime.datetime.fromtimestamp(state['start_unix'],datetime.timezone.utc).isoformat(),
                elapsed_seconds=round(elapsed,3),remaining_seconds=round(max(0,5400-elapsed),3),
                hard_stop_reached=elapsed>=5400,source=state['source'],
                measurement='monotonic' if use_mono else 'persisted_UTC_fallback',
                approximate_start=state['source']=='directory_ctime_estimate')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--start',action='store_true',help='start once; never resets an existing timer')
    ap.add_argument('--json',action='store_true');args=ap.parse_args()
    data=read_clock(Path(__file__).resolve().parent,args.start)
    if args.json:print(json.dumps(data,ensure_ascii=True,indent=2));return
    print('Current UTC: '+data['now_utc'])
    print('Elapsed: %.1f min | Remaining until 90-minute deadline: %.1f min | Suggested: 60 min.' %
          (data['elapsed_seconds']/60,data['remaining_seconds']/60))
    print('Start: %s | Clock: %s | Source: %s' % (data['start_utc'],data['measurement'],data['source']))
    if data['approximate_start']:print('NOTE: legacy Linux ctime start is approximate; prefer the organizer unpack launcher.')
    if data['hard_stop_reached']:print('HARD STOP REACHED: submit the current version now.')

if __name__=='__main__':main()
