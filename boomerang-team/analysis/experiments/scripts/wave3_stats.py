import argparse
from collections import Counter, defaultdict
import sys
from math import exp, log, sqrt
from random import Random
from statistics import mean, variance

from audit3 import cases
from prepare3 import A, F, MAPS, ROOT, read, require, write

SCALE = 400 / log(10)

def pairs(values):
    """Map-stratified SE across seed blocks, after averaging seat rotations within each block."""
    strata = defaultdict(list)
    for key, values_for_key in values.items():
        strata[key[0]].append(mean(values_for_key))
    n = sum(map(len,strata.values()))
    se = sqrt(sum((len(v)/n)**2 * variance(v)/len(v) for v in strata.values() if len(v)>1))
    return dict(value=mean(v for group in strata.values() for v in group), se=se, blocks=n,
                games=sum(map(len,values.values())))

def metrics(rows, value):
    result = defaultdict(list)
    for key, row in rows:
        result[key].append(value(row))
    return pairs(result)

def diff(left, right, f=lambda x:x):
    require(left.keys()==right.keys(),"paired support")
    return pairs({key: [f(left[key])-f(right[key])] for key in left})

def phase_team(phase):
    _, data = cases(phase)
    out = defaultdict(dict)
    for s,g in data:
        label=s["label"]
        key=s["map"],s["seed"],s["rotation"]
        require(key not in out[label], (label,key))
        out[label][key]=g
    return out

def sample_sets(data):
    return [(m,{k:v for k,v in data.items() if k[0]==m}) for m in MAPS]+[("total",data)]

def team_win(g,key,side=0):
    return int(g["winner"]==(key[2]^side))

def team_points(g,key):
    return 0.5 if g["winner"]<0 else team_win(g,key)

def person(g,name):
    return next(p for p in g["players"] if p["name"]==name)

def personal(g,name,field):
    p=person(g,name)
    if field=="enemy_kills":
        return p["kills"]-p["k_team"]
    if field=="attacks":
        return p["throws"]+p["slashes"]+p["kicks"]
    return p[field]

def team_report(data, pnames=()):
    fields=("enemy_kills","k_team","throws","slashes","kicks","attacks","alive")
    result={}
    for m, subset in sample_sets(data):
        block=defaultdict(list)
        wins=[0,0,0]
        for key,g in subset.items():
            block[key[:2]].append(team_points(g,key))
            wins[0 if team_win(g,key) else 1 if team_win(g,key,1) else 2]+=1
        vals={}
        vals["wl_d"]=wins
        vals["points"]=pairs(block)
        for side in (0,1):
            vals[f"win{side}"]=metrics(((k[:2],team_win(g,k,side)) for k,g in subset.items()),lambda x:x)
        vals["player"]={n:{f:metrics(((k[:2],personal(g,n,f)) for k,g in subset.items()),lambda x:x)
                           for f in fields} for n in pnames}
        vals["faults"]=fault_summary(subset.values())
        vals["teamkills"]=[sum(sum(p["k_team"] for p in g["players"] if p["team"]==(k[2]^s))
                               for k,g in subset.items()) for s in range(2)]
        result[m]=vals
    return result

def team_delta(treatment, control, pnames=()):
    require(treatment.keys()==control.keys(),"same seeds & rotations")
    out={}
    for m,subset in sample_sets(treatment):
        def estimate(value):
            return metrics(((k[:2],value(g,k)-value(control[k],k)) for k,g in subset.items()),lambda x:x)
        out[m]=dict(points=estimate(team_points),
                    win0=estimate(team_win),
                    win1=estimate(lambda g,k:team_win(g,k,1)),
                    player={n:{f:estimate(lambda g,k:personal(g,n,f))
                               for f in ("enemy_kills","k_team","throws","slashes","kicks","attacks","alive")}
                            for n in pnames})
    return out

def clean_delta(new, old):
    require(new.keys()==old.keys(),"clean pair support")
    bad=set()
    for condition in (old,new):
        for k,g in condition.items():
            if any(p["status"]!="ok" or p["faults_total"] for p in g["players"]):
                bad.add(k[:2])
    results=team_delta({k:g for k,g in new.items() if k[:2] not in bad},
                       {k:g for k,g in old.items() if k[:2] not in bad})
    return dict(omitted=len(bad), results=results)

def fault_summary(games):
    events=Counter()
    seats=Counter()
    for g in games:
        for p in g["players"]:
            if p["faults_total"]:
                events[p["name"]]+=p["faults_total"]
            if p["status"]!="ok":
                seats[f"{p['name']}:{p['status']}"]+=1
    return dict(events=dict(events), bad_seats=dict(seats))

def short_pilot():
    original=read(ROOT/"ep11/official_games.json")
    out={}
    for phase in ("smoke9","smoke9b"):
        _,data=cases(phase)
        faults=fault_summary(g for _,g in data)
        det=Counter()
        for spec,g in data:
            if spec["label"]=="control":
                old=original[spec["official"]]
                det[f"{spec['mode']}.total"]+=1
                det[f"{spec['mode']}.exact"]+=(
                    old["steps"]==g["steps"] and all(
                        all(p[k]==q[k] for k in ("rank","kills","alive")) for p,q in zip(g["players"],old["players"])))
        out[phase]=dict(games=len(data),faults=faults,archive=dict(det))
    return out

def head_to_head():
    _,data=cases("self9")
    output={}
    for n in A+F:
        hist=defaultdict(list)
        for s,g in data:
            if s["focal"]==n:
                hist[s["map"],s["seed"]].append(
                    0.5 if g["winner"]<0 else int(g["winner"]==s["rotation"]))
        require(len(hist)==60 and all(len(v)==2 for v in hist.values()),n)
        summary={}
        for m in list(dict.fromkeys(k[0] for k in hist))+["total"]:
            r={k:v for k,v in hist.items() if m=="total" or k[0]==m}
            count=Counter(x for v in r.values() for x in v)
            summary[m]=dict(points=pairs(r),win=pairs({k:[int(x==1) for x in vals] for k,vals in r.items()}),
                            wld=[count[1],count[0],count[0.5]])
        output[n]=summary
    return dict(people=output,faults=fault_summary(g for _,g in data))

def ffa_values(players):
    by_rank=Counter(p["rank"] for p in players)
    ranking=(3,2,1,0)
    return [mean(ranking[p["rank"]-1:p["rank"]-1+by_rank[p["rank"]]])+p["kills"] for p in players]

def compare(a,b):
    return 1.0 if a>b else 0.5 if a==b else 0.0

def compare_focal(spec,g):
    focus=spec["focal"]
    ix=next(i for i,p in enumerate(g["players"]) if p["name"]==focus)
    if spec["mode"]==0:
        scores=[-x["rank"] for x in g["players"]]
    else:
        scores=ffa_values(g["players"])
    return {p["name"]:compare(scores[ix],scores[i]) for i,p in enumerate(g["players"]) if i!=ix}

def bt_fixed(blocks, levels):
    totals=defaultdict(lambda:[0,0.])
    for outcomes in blocks:
        for outcomes_one in outcomes:
            for name,value in outcomes_one.items():
                totals[name][0]+=1
                totals[name][1]+=value
    require(totals,"no rating comparisons")
    lo, hi=-2000.,2000.
    real = sum(s+.5 for _,s in totals.values())
    for _ in range(34):
        trial=(lo+hi)/2
        expected=sum((n+1)/(1+exp((levels[k]-trial)/SCALE)) for k,(n,_) in totals.items())
        if expected<real:
            lo=trial
        else:
            hi=trial
    return (lo+hi)/2

def joint_pool(data, official):
    # Keep the original 13 identities in their full archived network. Only the new
    # identity's edges are added from each substituted slot, because the other three
    # FFA edges are already represented in the original-opponent reference network.
    sys.path.insert(0,str(ROOT/"ep11/host"))
    import bt
    import rating
    obs=read(ROOT/"ep11/official_obs.json")
    all_rows=rating.pair_rows(obs)
    names=list(official)
    newcomers=[n+"-final" for n in A+F]
    schedule_maps=read(ROOT/"ep11/schedule.json")["maps"]
    for mode,rr in enumerate(all_rows):
        field=("duel_rating","ffa_rating")[mode]
        old=bt.fit(bt.merge(bt.tally(rr),schedule_maps),names,None)
        require(max(abs(old[n]-official[n][field]) for n in names)<1e-5,"official BT replay")
    for spec,g in data:
        if spec["label"]=="pool":
            all_rows[spec["mode"]].extend(dict(map=spec["map"],a=spec["focal"]+"-final",b=b,score=s)
                                           for b,s in compare_focal(spec,g).items())
    tables=[]
    for rr in all_rows:
        fit=bt.fit(bt.merge(bt.tally(rr),schedule_maps),names+newcomers,None)
        center=mean(fit[n] for n in names)
        tables.append({n:v-center for n,v in fit.items()})
    return {n:dict(duel=tables[0][n],ffa=tables[1][n],
                   combined=mean(t[n] for t in tables)) for n in names+newcomers}

def pool():
    _,data=cases("pool9")
    old_score=read(ROOT/"ep11/official_score.json")["players"]
    history=read(ROOT/"ep11/official_games.json")
    archive=Counter()
    repeat=defaultdict(list)
    paired=defaultdict(dict)
    for s,g in data:
        key=s["focal"],s["mode"],s["map"],s["block"],s["rotation"]
        group=paired[key]
        require(s["label"] not in group,key)
        group[s["label"]]=compare_focal(s,g)
        if s["label"]=="control":
            arch=history[s["official"]]
            repeat[s["mode"],s["official"]].append((g["steps"],g["digest"]))
            archive[f"{s['mode']}.games"]+=1
            archive[f"{s['mode']}.matches"]+=g["steps"]==arch["steps"] and all(
                all(p[k]==q[k] for k in ("rank","kills","alive")) for p,q in zip(g["players"],arch["players"]))
            archive[f"{s['mode']}.focal_equivalent"]+=group[s["label"]]==compare_focal(s,arch)
    duplicate=Counter()
    for (mode,_),record in repeat.items():
        if len(record)>1:
            duplicate[f"{mode}.duplicate_ids"]+=1
            duplicate[f"{mode}.same_state"]+=len(set(record))==1
    write(ROOT/"control_repeat_audit.json",dict(duplicate))
    card={}
    for name in A+F:
        modes=[]
        for mode,field in enumerate(("duel_rating","ffa_rating")):
            levels={n:p[field] for n,p in old_score.items()}
            cases_by_block=defaultdict(lambda:defaultdict(list))
            for (focal,md,m,block,rot),rec in paired.items():
                if focal!=name or md!=mode:
                    continue
                require(rec.keys()=={"pool","control"} and rec["pool"].keys()==rec["control"].keys(),rec)
                for condition, result in rec.items():
                    cases_by_block[(m,block)][condition].append(result)
            base=bt_fixed([row["control"] for row in cases_by_block.values()],levels)
            improved=bt_fixed([row["pool"] for row in cases_by_block.values()],levels)
            bymap=defaultdict(list)
            for key in cases_by_block:
                bymap[key[0]].append(key)
            # Same maps and seat blocks in both conditions. For 1v1 keep opponent mix and map fixed.
            rng=Random(120903)
            boots=[]
            for _ in range(400):
                selected=rng.choices(list(bymap),k=len(bymap))
                idx=[key for m in selected for key in bymap[m]]
                boots.append(bt_fixed([cases_by_block[key]["pool"] for key in idx],levels)-
                             bt_fixed([cases_by_block[key]["control"] for key in idx],levels))
            boot_se=sqrt(variance(boots))
            comp=lambda outcomes:mean(outcomes.values())
            def paired_value(fnc):
                return pairs({k: [fnc(x,y) for x,y in zip(row["pool"],row["control"])]
                              for k,row in cases_by_block.items()})
            modes.append(dict(official=levels[name], sample_old=base, sample_final=improved,
                              calibrated=levels[name]+improved-base, delta=improved-base, bootstrap_se=boot_se,
                              old_points=paired_value(lambda x,y:comp(y)),
                              final_points=paired_value(lambda x,y:comp(x)),
                              points_delta=paired_value(lambda x,y:comp(x)-comp(y)),
                              old_win=paired_value(lambda x,y:mean(v==1 for v in y.values())),
                              final_win=paired_value(lambda x,y:mean(v==1 for v in x.values())),
                              win_delta=paired_value(lambda x,y:mean(v==1 for v in x.values())-
                                                    mean(v==1 for v in y.values())),
                              seen=sorted({n for row in cases_by_block.values() for outcome in row["pool"] for n in outcome}),
                              blocks=len(cases_by_block)))
        card[name]=dict(duel=modes[0],ffa=modes[1],
                        combined={k:mean(x[k] for x in modes) for k in ("official","calibrated","delta")},
                        combined_bootstrap_se=sqrt(sum(x["bootstrap_se"]**2 for x in modes))/2)
    return dict(people=card,archive=dict(archive),joint=joint_pool(data,old_score),
                faults=fault_summary(g for _,g in data))

def team_summaries():
    exp10=phase_team("main10")["stars"]
    exp11=phase_team("main11")
    on=phase_team("main12-on")["normal"]
    off=phase_team("main12-off")["noff"]
    exp13=phase_team("main13")
    noff_names=("glm","deepseek","astra","carbon")
    require(all(p["k_team"]==0 for g in off.values() for p in g["players"]),"noff friendly kills")
    return dict(
        exp10=team_report(exp10,("fable","opus","carbon","hydrogen","sol","astra","astra2","astra3","astra4","astra5")),
        exp11={**{label:team_report(games) for label,games in exp11.items()},
               "delta":team_delta(exp11["swap"],exp11["original"]),
               "clean":clean_delta(exp11["swap"],exp11["original"])},
        exp12=dict(normal=team_report(on,noff_names), noff=team_report(off,noff_names),
                   delta=team_delta(off,on,noff_names),clean=clean_delta(off,on)),
        exp13={**{label:team_report(games) for label,games in exp13.items()},
               "strong_delta":team_delta(exp13["astra-strong"],exp13["astra-weak"])})

def main(mode):
    if mode in ("individual","all"):
        write(ROOT/"individual.json",dict(smoke=short_pilot(), self=head_to_head(), pool=pool()))
    if mode in ("team","all"):
        write(ROOT/"team_stats.json",team_summaries())

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("mode",choices=("individual","team","all"))
    main(parser.parse_args().mode)
