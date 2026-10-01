"""Evaluate a roster with explicit 2–10-player matches, no filler bots."""
import argparse
import hashlib
import itertools
import json
import random
import secrets
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from collections import deque
from pathlib import Path
from bt import fit


def run_match(job):
    """One isolated match; preserve the frozen referee and its time limits."""
    number,game,workspace,output,resume,bot_hashes=job
    sys.path.insert(0,str(Path(workspace)/'engine'))
    from arena import play
    from verify_replay import verify
    stem=Path(output)/'replays'/f'{number:05d}'
    start=time.perf_counter()
    if resume and stem.with_suffix('.json').exists():
        verify(stem.with_suffix('.json'))
        result=json.loads(stem.with_suffix('.json').read_text(encoding='utf-8'))['result']
        if result['seed']!=game['seed'] or result['names']!=[n for n,_ in game['roster']]:
            raise ValueError('Recovered match differs')
        if any(result['hashes'].get(f'bot_{i}')!=bot_hashes[name] for i,(name,_) in enumerate(game['roster'])):
            raise ValueError('Recovered bot hashes differ')
    else:
        result=play([n+'='+p for n,p in game['roster']],game['seed'],str(stem))
        verify(stem.with_suffix('.json'))
    return dict(match=number,size=game['size'],result=result),time.perf_counter()-start


def match_results(jobs,workers):
    if workers==1:
        for job in jobs:yield run_match(job)
        return
    # Keep only a small bounded queue; checkpoint in original schedule order.
    with ProcessPoolExecutor(max_workers=workers) as pool:
        jobs=iter(jobs);pending=deque()
        for _ in range(workers*2):
            job=next(jobs,None)
            if job is None:break
            pending.append(pool.submit(run_match,job))
        while pending:
            yield pending.popleft().result()
            job=next(jobs,None)
            if job is not None:pending.append(pool.submit(run_match,job))


def make_schedule(entrants, sizes, seed_count, seed):
    count=len(entrants)
    if count<2: raise ValueError('at least two entrants required')
    if seed_count<1: raise ValueError('seed count must be positive')
    if not sizes or len(set(sizes))!=len(sizes) or any(n<2 or n>min(10,count) for n in sizes):
        raise ValueError('each match size must be unique and between 2 and min(10, entrant count)')
    rng=random.Random(seed)
    result=[]
    for size in sizes:
        seeds=rng.sample(range(1,2**31),seed_count)
        for group in itertools.combinations(entrants,size):
            for game_seed in seeds:
                # The same roster orders and maps apply to each entrant group.
                forward=list(group)
                orders=[forward] if size<=2 else [forward,list(reversed(forward))]
                for order in orders:
                    for shift in range(size):
                        result.append(dict(size=size,seed=game_seed,roster=order[shift:]+order[:shift]))
    return result


def report(rows, entrants, sizes, anchor=None):
    lines=['# 对战统计','','单局分＝生存分＋0.5×击杀份额；第一率按单局合计分排名。',
           '', '|人数|选手|有效局|场均生存分|场均击杀分|场均合计分|独占第一率|含并列第一率|故障局|',
           '|---:|---|---:|---:|---:|---:|---:|---:|---:|']
    for size in sizes:
        for name,_ in entrants:
            values=[]
            for row in rows:
                r=row['result']
                if row['size']!=size or not r['valid_for_scoring'] or name not in r['names']:continue
                i=r['names'].index(name);rank=r['ranks'][i];ties=r['ranks'].count(rank)
                values.append((r['survival_points'][i],r['kill_points'][i],r['points'][i],
                               rank==1 and ties==1,rank==1,r['errors'][i]>0))
            if not values:continue
            av=[sum(v[k] for v in values)/len(values) for k in range(5)]
            lines.append(f'|{size}|{name}|{len(values)}|{av[0]:.3f}|{av[1]:.3f}|{av[2]:.3f}|{av[3]:.1%}|{av[4]:.1%}|{sum(v[5] for v in values)}|')
    if anchor:
        lines+=['','## BT 实力值','','各人数总权重相等；基准为 0，固定尺度，不按最高分重缩放。','']
        try:
            result=fit(rows,[n for n,_ in entrants],sizes,anchor)
            lines+=['|选手|BT 实力值|','|---|---:|']
            for name,value in sorted(result['ratings'].items(),key=lambda x:-x[1]):
                lines.append(f'|{name}|{value:.2f}|')
        except ValueError as e:lines.append('暂不可汇总：'+str(e))
    invalid=sum(not r['result']['valid_for_scoring'] for r in rows)
    lines+=['',f'完成 {len(rows)} 局，启动失败不计分 {invalid} 局。',
            '名次不能单独证明没有作弊；故障详情、源代码及运行记录需保留。']
    return '\n'.join(lines)+'\n'


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--workspace',type=Path,required=True,help='clean referee workspace')
    ap.add_argument('--bots',nargs='+',required=True,help='unique Name=program entries; order is frozen slot order')
    ap.add_argument('--sizes',type=int,nargs='+',required=True,help='explicit match sizes, no fillers')
    ap.add_argument('--anchor',required=True,help='name of the public baseline included in --bots')
    ap.add_argument('--seeds',type=int,default=5,help='number of maps per size')
    ap.add_argument('--seed',type=int,help='schedule seed; otherwise generated and recorded')
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--plan-only',action='store_true',help='save schedule without running bots')
    ap.add_argument('--schedule',type=Path,help='frozen slot schedule; bot order defines slot IDs')
    ap.add_argument('--resume',action='store_true',help='resume completed matches only after hash checks')
    ap.add_argument('--workers',type=int,default=1,help='concurrent matches; calibrate on the host before increasing')
    ap.add_argument('--report-every',type=int,default=25,help='refresh aggregate report every N games; results checkpoint every game')
    args=ap.parse_args()
    if args.workers<1 or args.report_every<1:ap.error('workers and report-every must be positive')
    entrants=[]
    for entry in args.bots:
        name,sep,path=entry.partition('=')
        if not sep or not name.strip() or not Path(path).is_file():ap.error('Use Name=existing-program entries')
        if any(c in name for c in '|\n\r'):ap.error('Names cannot contain pipes or line breaks')
        entrants.append((name,str(Path(path).resolve())))
    if len({name for name,_ in entrants})!=len(entrants):ap.error('Names must be unique')
    if args.anchor not in {name for name,_ in entrants}:ap.error('--anchor must name a bot included in --bots')
    seed=args.seed if args.seed is not None else secrets.randbits(63)
    if not args.schedule:
        try:schedule=make_schedule(entrants,args.sizes,args.seeds,seed)
        except ValueError as e:ap.error(str(e))
    if args.schedule:
        frozen=json.loads(args.schedule.read_text(encoding='utf-8'))
        if frozen['sizes']!=args.sizes:ap.error('Frozen schedule sizes differ')
        slot_ids={slot for g in frozen['entries'] for slot in g['slots']}
        if slot_ids!=set(range(len(entrants))):ap.error('Frozen roster size differs')
        if entrants[frozen['baseline_slot']][0]!=args.anchor:ap.error('Baseline must occupy its frozen slot')
        schedule=[]
        for g in frozen['entries']:
            if not 2<=g['size']<=10 or len(g['slots'])!=g['size'] or len(set(g['slots']))!=g['size']:ap.error('Invalid frozen match')
            schedule.append(dict(size=g['size'],seed=g['seed'],roster=[entrants[i] for i in g['slots']]))
        seed=frozen['schedule_seed']
    if args.output.exists() and not args.resume:ap.error('Output exists; use --resume or a new directory')
    if args.resume and not args.output.exists():ap.error('Nothing to resume')
    workspace=args.workspace.resolve()
    manifest=json.loads((workspace/'MANIFEST.json').read_text(encoding='utf-8'))
    for name,digest in manifest.items():
        file=(workspace/name).resolve()
        if not file.is_relative_to(workspace):ap.error('Invalid manifest path')
        if hashlib.sha256(file.read_bytes()).hexdigest()!=digest:ap.error('Referee package modified: '+name)
    sys.path.insert(0,str(workspace/'engine'))
    from arena import play
    from engine import VERSION
    from verify_replay import verify
    if args.schedule and frozen['version']!=VERSION:ap.error('Frozen schedule engine version differs')
    identity=dict(version=VERSION,anchor=args.anchor,sizes=args.sizes,execution=dict(workers=args.workers),
                  scoring_code={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (Path(__file__),Path(__file__).with_name('bt.py'))},
                  engine_manifest=manifest,bot_hashes={name:hashlib.sha256(Path(path).read_bytes()).hexdigest() for name,path in entrants},
                  schedule=dict(schedule_seed=seed,entries=schedule))
    identity=json.loads(json.dumps(identity))
    rows=[]
    if args.resume:
        if json.loads((args.output/'identity.json').read_text(encoding='utf-8'))!=identity:ap.error('Referee, bots or schedule changed; resume rejected')
        saved=args.output/'results.json'
        if saved.exists():rows=json.loads(saved.read_text(encoding='utf-8'))
        if len(rows)>len(schedule):ap.error('Too many saved results')
        for number,row in enumerate(rows,1):
            g=schedule[number-1];r=row['result']
            if row['match']!=number or row['size']!=g['size'] or r['seed']!=g['seed'] or r['names']!=[n for n,_ in g['roster']]:ap.error('Saved match differs')
            path=args.output/'replays'/f'{number:05d}.json';verify(path)
            if json.loads(path.read_text(encoding='utf-8'))['result']!=r:ap.error('Saved result/replay mismatch')
    else:
        args.output.mkdir(parents=True)
        (args.output/'identity.json').write_text(json.dumps(identity,ensure_ascii=False,indent=2),encoding='utf-8')
        (args.output/'schedule.json').write_text(json.dumps(identity['schedule'],ensure_ascii=False,indent=2),encoding='utf-8')
    print(f'{len(entrants)} entrants; sizes={args.sizes}; {len(schedule)} games; no fillers.',flush=True)
    if args.plan_only:return
    started=time.perf_counter();report_seconds=0;checkpoint_seconds=0;match_seconds=0
    jobs=[(i,g,str(workspace),str(args.output),args.resume,identity['bot_hashes']) for i,g in enumerate(schedule,1) if i>len(rows)]
    for row,duration in match_results(jobs,args.workers):
        number=row['match'];result=row['result'];rows.append(row);match_seconds+=duration
        tick=time.perf_counter()
        temp=args.output/'results.tmp'
        temp.write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8');temp.replace(args.output/'results.json')
        checkpoint_seconds+=time.perf_counter()-tick
        if number%args.report_every==0 or number==len(schedule):
            tick=time.perf_counter()
            (args.output/'汇总.md').write_text(report(rows,entrants,args.sizes,args.anchor),encoding='utf-8')
            report_seconds+=time.perf_counter()-tick
        print(f'{number}/{len(schedule)} ranks={result["ranks"]} errors={result["errors"]}',flush=True)
    try:rating=fit(rows,[n for n,_ in entrants],args.sizes,args.anchor)
    except ValueError as e:rating={'unavailable':str(e)}
    (args.output/'bt.json').write_text(json.dumps(rating,ensure_ascii=False,indent=2),encoding='utf-8')
    (args.output/'performance.json').write_text(json.dumps(dict(workers=args.workers,wall_seconds=time.perf_counter()-started,
        match_worker_seconds=match_seconds,checkpoint_seconds=checkpoint_seconds,report_seconds=report_seconds),indent=2),encoding='utf-8')


if __name__=='__main__':main()
