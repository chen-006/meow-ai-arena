"""Bradley–Terry, equal total weight per match size, explicit baseline anchor."""
import math
from collections import Counter,defaultdict


def comparisons(rows,names,sizes):
    valid=[row for row in rows if row['size'] in sizes and row['result']['valid_for_scoring']]
    counts=Counter(row['size'] for row in valid)
    if any(not counts[size] for size in sizes):raise ValueError('部分人数尚无有效对局')
    pairs=defaultdict(lambda:[0.0,0.0])
    total=len(valid)
    for row in valid:
        r=row['result'];n=row['size'];weight=total/(len(sizes)*counts[n]*(n*(n-1)/2))
        for i in range(n):
            for j in range(i+1,n):
                a,b=r['names'][i],r['names'][j]
                win=1.0 if r['ranks'][i]<r['ranks'][j] else 0.5 if r['ranks'][i]==r['ranks'][j] else 0.0
                if a>b:a,b,win=b,a,1-win
                pairs[a,b][0]+=weight;pairs[a,b][1]+=weight*win
    if any(a not in names or b not in names for a,b in pairs):raise ValueError('未知选手')
    return pairs,dict(counts)


def fit(rows,names,sizes,anchor):
    if anchor not in names:raise ValueError('基准必须作为真实参赛者包含在名单中')
    pairs,counts=comparisons(rows,names,sizes)
    reached={anchor}
    while True:
        new=reached|{b for a,b in pairs if a in reached}|{a for a,b in pairs if b in reached}
        if new==reached:break
        reached=new
    if reached!=set(names):raise ValueError('尚有选手未通过交手关系连接到基准')
    # Same finite-estimate convention as the earlier territory contest:
    # one virtual draw per observed pair, after size weighting.
    games={pair:(g+1,w+.5) for pair,(g,w) in pairs.items()}
    wins={name:0.0 for name in names}
    for (a,b),(g,w) in games.items():wins[a]+=w;wins[b]+=g-w
    strength={name:1.0 for name in names}
    for iteration in range(20000):
        denom={name:0.0 for name in names}
        for (a,b),(g,w) in games.items():
            v=g/(strength[a]+strength[b]);denom[a]+=v;denom[b]+=v
        updated={name:wins[name]/denom[name] for name in names}
        center=sum(math.log(v) for v in updated.values())/len(names)
        updated={name:math.exp(math.log(v)-center) for name,v in updated.items()}
        delta=max(abs(math.log(updated[n]/strength[n])) for n in names)
        strength=updated
        if delta<1e-11:break
    else:raise ValueError('BT 未收敛，暂不输出实力值')
    origin=math.log(strength[anchor])
    rating={n:400/math.log(10)*(math.log(strength[n])-origin) for n in names}
    rating={n:0.0 if abs(v)<1e-10 else v for n,v in rating.items()}
    rating[anchor]=0.0
    return dict(anchor=anchor,ratings=rating,valid_games_by_size=counts,iterations=iteration+1,
                size_weight='equal_total',scale='400 * log10(strength / baseline_strength)',
                smoothing='one virtual draw per observed pair after weighting')
