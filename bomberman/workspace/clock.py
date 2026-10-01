"""Read-only extraction clock; never uses archived modification time."""
import argparse, datetime, json, os, time
from pathlib import Path

def read_clock():
    st=Path(__file__).resolve().stat()
    birth=getattr(st,'st_birthtime',None)
    start=birth if birth is not None else st.st_ctime
    now=time.time(); elapsed=max(0,now-start)
    return dict(start_utc=datetime.datetime.fromtimestamp(start,datetime.timezone.utc).isoformat(),
                elapsed_seconds=round(elapsed,3),remaining_seconds=round(max(0,5400-elapsed),3),
                hard_stop_reached=elapsed>=5400,
                source='file_birthtime' if birth is not None or os.name=='nt' else 'file_ctime')

def main():
    ap=argparse.ArgumentParser(description='Read elapsed time since extraction; writes no files.')
    ap.add_argument('--json',action='store_true')
    ap.add_argument('--start',action='store_true',help=argparse.SUPPRESS)
    args=ap.parse_args(); data=read_clock()
    if args.json: print(json.dumps(data,indent=2)); return
    print('Elapsed: %.1f min | Remaining: %.1f min' % (data['elapsed_seconds']/60,data['remaining_seconds']/60))
    if data['elapsed_seconds'] >= 3600 and not data['hard_stop_reached']:
        print('60-minute target reached; finish validation and submit by 90 minutes.')
    if data['hard_stop_reached']: print('90-minute deadline reached: submit your current bot.')
if __name__=='__main__': main()
