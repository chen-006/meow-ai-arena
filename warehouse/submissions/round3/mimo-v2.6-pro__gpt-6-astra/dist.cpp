// Distance cache with exact, lazy repair after BLOCK/UNBLOCK.
// A blocked source is still a legal BFS root; blocked destinations are not.
#include "common.h"

namespace {
struct Entry {
    int root=-1, epoch=0, prev=-1, next=-1;
    vector<Distance> d;
};
vector<Entry> entries;
struct BlockChange {int cell,x,y;};
vector<BlockChange> changes;
vector<int> slot, bfsQueue, marks;
vector<unsigned char> walk;
vector<pair<int,int>> invalid;
// Integer distances let the repair use a bucket queue instead of a binary heap.
struct QueueItem {int vertex,next;};
vector<QueueItem> repairQueue;
vector<int> bucketHeads;
int minBucket=INF,maxBucket=0;
void enqueue(int distance,int vertex) {
    if(distance>=(int)bucketHeads.size()) bucketHeads.resize(distance+1,-1);
    repairQueue.push_back({vertex,bucketHeads[distance]});
    bucketHeads[distance]=(int)repairQueue.size()-1;
    minBucket=min(minBucket,distance);maxBucket=max(maxBucket,distance);
}
void clearRepairQueue() {
    for(int i=minBucket;i<=maxBucket;++i) bucketHeads[i]=-1;
    repairQueue.clear();minBucket=INF;maxBucket=0;
}
vector<int> changed;
int offsets[4], cap=0, head=-1, tail=-1, stamp=0;

void init() {
    if (!slot.empty()) return;
    const int n=W*H;
    offsets[0]=-W; offsets[1]=1; offsets[2]=W; offsets[3]=-1;
    slot.assign(n,-1); bfsQueue.resize(n); marks.assign(n,0); walk.resize(n);
    for(int u=0;u<n;++u) walk[u]=MAP[u/W][u%W]!='#' && !BLOCKED[u];
    cap=max(1,min(n, int((256u<<20)/(n*sizeof(Distance)))));
    entries.reserve(cap);
}
void touch(int s) {
    if(head==s) return;
    Entry& e=entries[s];
    if(e.prev>=0) entries[e.prev].next=e.next;
    if(e.next>=0) entries[e.next].prev=e.prev;
    if(tail==s) tail=e.prev;
    e.prev=-1; e.next=head;
    if(head>=0) entries[head].prev=s;
    head=s;
    if(tail<0) tail=s;
}
void rebuild(Entry& e) {
    const int n=W*H;
    e.d.assign(n,INF);
    int h=0,t=0;
    if(e.root>=0) {bfsQueue[t++]=e.root;e.d[e.root]=0;}
    else for(auto [x,y]:ALL_CHG()) {
        int u=y*W+x;
        if(walk[u]) {bfsQueue[t++]=u;e.d[u]=0;}
    }
    while(h<t) {
        int u=bfsQueue[h++], nd=e.d[u]+1;
        // Legal inputs have an immutable wall border. Every queued cell is interior.
        auto visit=[&](int v) {
            if(walk[v] && e.d[v]==INF) {e.d[v]=nd;bfsQueue[t++]=v;}
        };
        visit(u-W);visit(u+1);visit(u+W);visit(u-1);
    }
    e.epoch=(int)changes.size();
}

// Remove exactly the old shortest-path DAG vertices whose last predecessor
// vanished. Then a multi-source Dijkstra repairs their distances and propagates
// improvements from newly opened cells. Large changes fall back to linear BFS.
void repair(Entry& e) {
    if(e.epoch==(int)changes.size()) return;
    const int limit=max(32,W*H/6);
    if((int)changes.size()-e.epoch>limit) {rebuild(e);return;}
    changed.clear(); invalid.clear(); clearRepairQueue(); ++stamp;
    for(int i=e.epoch;i<(int)changes.size();++i) {
        int v=changes[i].cell;
        if(marks[v]!=stamp) {marks[v]=stamp;changed.push_back(v);}
    }
    auto& d=e.d;
    for(int v:changed) if(!walk[v] && v!=e.root && d[v]!=INF) {
        invalid.emplace_back(d[v],v); d[v]=INF;
    }
    for(size_t h=0;h<invalid.size();++h) {
        auto [old,u]=invalid[h];
        for(int k=0;k<4;++k) {
            int v=u+offsets[k];
            if(!walk[v] || d[v]!=old+1) continue;
            bool supported=false;
            for(int j=0;j<4;++j) {
                int p=v+offsets[j];
                if((walk[p] || p==e.root) && d[p]==old) {supported=true;break;}
            }
            if(!supported) {invalid.emplace_back(d[v],v);d[v]=INF;}
        }
        if((int)invalid.size()>limit) {rebuild(e);return;}
    }
    auto seed=[&](int v) {
        if(!walk[v] || v==e.root) return;
        int best=(e.root<0 && MAP[v/W][v%W]=='C') ? 0 : INF;
        for(int k=0;k<4;++k) {
            int p=v+offsets[k];
            if(walk[p] || p==e.root) best=min(best,d[p]+1);
        }
        if(best<d[v]) {
            d[v]=best;enqueue(best,v);
        }
    };
    for(auto [old,v]:invalid) seed(v);
    for(int v:changed) seed(v);
    int count=0;
    while(minBucket<=maxBucket) {
        int item=bucketHeads[minBucket];
        if(item<0) {++minBucket;continue;}
        int du=minBucket,u=repairQueue[item].vertex;
        bucketHeads[minBucket]=repairQueue[item].next;
        if(du!=d[u]) continue;
        for(int k=0;k<4;++k) {
            int v=u+offsets[k];
            if(walk[v] && du+1<d[v]) {
                d[v]=du+1;enqueue(du+1,v);
            }
        }
        if(++count>limit) {clearRepairQueue();rebuild(e);return;}
    }
    e.epoch=(int)changes.size();
}
}

void InitDistances() {init();}
void DistInvalidate(int x,int y) {
    init();
    int u=y*W+x;
    walk[u]=!BLOCKED[u] && MAP[y][x]!='#';
    changes.push_back({u,x,y});
    ++BLOCK_VERSION;
}
const vector<Distance>& DistTable(int sx,int sy) {
    init();
    int root=sy*W+sx,s=slot[root];
    if(s>=0) {touch(s);repair(entries[s]);return entries[s].d;}
    if((int)entries.size()<cap) {s=(int)entries.size();entries.emplace_back();}
    else {s=tail;slot[entries[s].root]=-1;}
    touch(s); slot[root]=s; entries[s].root=root; rebuild(entries[s]);
    return entries[s].d;
}
// Movement only needs the minimum over the four neighbors, not every cell.
// A changed cell outside the Manhattan ellipse of an old shortest route cannot
// destroy that route or create an equally short alternative. In that case the
// old table still gives the exact next-step choice; keep its epoch unchanged so
// later queries continue to account for ALL outstanding changes.
const vector<Distance>& MoveTable(int gx,int gy,int rx,int ry) {
    init();
    int s=slot[gy*W+gx];
    if(s<0) return DistTable(gx,gy);
    touch(s);Entry& e=entries[s];
    int pending=(int)changes.size()-e.epoch;
    if(pending && pending<=32) {
        int best=INF,u=ry*W+rx;
        for(int k=0;k<4;++k) if(walk[u+offsets[k]]) best=min(best,int(e.d[u+offsets[k]]));
        bool unchanged=best<INF;
        for(int i=e.epoch;unchanged && i<(int)changes.size();++i) {
            int x=changes[i].x,y=changes[i].y;
            if(abs(x-rx)+abs(y-ry)+abs(x-gx)+abs(y-gy)<=best+1) unchanged=false;
        }
        if(unchanged) return e.d;
    }
    repair(e);
    return e.d;
}

int DIST(int ax,int ay,int bx,int by) {
    if(ax==bx && ay==by) return 0;
    if(!OK(bx,by)) return INF;
    return DistTable(ax,ay)[by*W+bx];
}

// Connectivity is independent of distance. Openings merge components; most
// closures preserve connectivity and can be certified by a small local search.
// If that certificate fails (a split or simply a large detour), rebuild all
// labels. Never infer connectivity from a bounded search that did not finish.
static int componentEpoch=-1, componentStamp=0;
static vector<int> component,parent,componentMarks;
static vector<unsigned char> componentWalk;
static int componentRoot(int c) {
    while(parent[c]!=c) {parent[c]=parent[parent[c]];c=parent[c];}
    return c;
}
static void rebuildComponents() {
    int n=W*H;
    component.assign(n,-1);parent.clear();componentWalk=walk;
    if(componentMarks.empty()) componentMarks.assign(n,0);
    for(int root=0;root<n;++root) {
        if(!walk[root] || component[root]>=0) continue;
        int id=(int)parent.size();parent.push_back(id);
        int h=0,t=0;bfsQueue[t++]=root;component[root]=id;
        while(h<t) {
            int u=bfsQueue[h++];
            for(int k=0;k<4;++k) {
                int v=u+offsets[k];
                if(walk[v] && component[v]<0) {component[v]=id;bfsQueue[t++]=v;}
            }
        }
    }
    componentEpoch=(int)changes.size();
}
static void topology() {
    init();
    if(componentEpoch==(int)changes.size()) return;
    if(componentEpoch<0 || (int)changes.size()-componentEpoch>W*H/8) {rebuildComponents();return;}
    for(int i=componentEpoch;i<(int)changes.size();++i) {
        int u=changes[i].cell;
        if(componentWalk[u]==walk[u]) continue;
        componentWalk[u]=walk[u];
        if(walk[u]) {
            int id=-1;
            for(int k=0;k<4;++k) {
                int v=u+offsets[k];
                if(!componentWalk[v]) continue;
                int r=componentRoot(component[v]);
                if(id<0) id=r;else parent[r]=id;
            }
            if(id<0) {id=(int)parent.size();parent.push_back(id);}
            component[u]=id;
        } else {
            component[u]=-1;
            int neighbors[4],count=0;
            for(int k=0;k<4;++k) if(componentWalk[u+offsets[k]]) neighbors[count++]=u+offsets[k];
            if(count<2) continue;
            ++componentStamp;
            int h=0,t=0;bfsQueue[t++]=neighbors[0];componentMarks[neighbors[0]]=componentStamp;
            int found=1;
            while(h<t && h<64 && found<count) {
                int v=bfsQueue[h++];
                for(int k=0;k<4;++k) {
                    int w=v+offsets[k];
                    if(!componentWalk[w] || componentMarks[w]==componentStamp) continue;
                    componentMarks[w]=componentStamp;bfsQueue[t++]=w;
                    for(int j=1;j<count;++j) if(w==neighbors[j]) ++found;
                }
            }
            if(found<count) {rebuildComponents();return;}
        }
    }
    componentEpoch=(int)changes.size();
}
bool Reachable(int ax,int ay,int bx,int by) {
    if(ax==bx && ay==by) return true;
    if(!OK(bx,by)) return false;
    topology();
    int a=ay*W+ax,c=componentRoot(component[by*W+bx]);
    if(walk[a]) return componentRoot(component[a])==c;
    for(int k=0;k<4;++k) {
        int u=a+offsets[k];
        if(walk[u] && componentRoot(component[u])==c) return true;
    }
    return false;
}
int CHG_DIST(int x,int y) {
    init();
    static Entry chargers;
    if(chargers.d.empty()) rebuild(chargers);else repair(chargers);
    int u=y*W+x;
    if(walk[u]) return chargers.d[u];
    int best=INF;
    for(int k=0;k<4;++k) if(walk[u+offsets[k]]) best=min(best,chargers.d[u+offsets[k]]+1);
    return best;
}
const vector<pair<int,int>>& ALL_CHG() {
    static vector<pair<int,int>> cs;
    static bool initialized=false;
    if(!initialized) {
        initialized=true;
        for(int y=0;y<H;++y) for(int x=0;x<W;++x) if(MAP[y][x]=='C') cs.emplace_back(x,y);
    }
    return cs;
}

// Permanent impossibility is independent of temporary blocks and robot charge.
// These orders still count as PENDING and can be cancelled, but never need to
// enter a dispatch heap. Distances without temporary blocks are lower bounds.
bool CanEverDispatch(int px,int py,int dx,int dy) {
    init();
    static vector<int> labels,hasRobot,charger;
    if(labels.empty()) {
        const int n=W*H;
        labels.assign(n,-1);charger.assign(n,INF);
        auto floor=[](int u){return MAP[u/W][u%W]!='#';};
        for(int root=0;root<n;++root) {
            if(!floor(root) || labels[root]>=0) continue;
            int id=(int)hasRobot.size();hasRobot.push_back(0);
            int h=0,t=0;bfsQueue[t++]=root;labels[root]=id;
            while(h<t) {
                int u=bfsQueue[h++];
                for(int k=0;k<4;++k) {
                    int v=u+offsets[k];
                    if(floor(v) && labels[v]<0) {labels[v]=id;bfsQueue[t++]=v;}
                }
            }
        }
        for(const Robot& r:ROB) hasRobot[labels[r.y*W+r.x]]=1;
        int h=0,t=0;
        for(auto [x,y]:ALL_CHG()) {int u=y*W+x;charger[u]=0;bfsQueue[t++]=u;}
        while(h<t) {
            int u=bfsQueue[h++];
            for(int k=0;k<4;++k) {
                int v=u+offsets[k];
                if(floor(v) && charger[v]==INF) {charger[v]=charger[u]+1;bfsQueue[t++]=v;}
            }
        }
    }
    int p=py*W+px,d=dy*W+dx;
    if(labels[p]!=labels[d] || !hasRobot[labels[p]] || charger[d]==INF) return false;
    return (abs(px-dx)+abs(py-dy))*PRM[7]+charger[d]+PRM[3]<=PRM[0];
}
