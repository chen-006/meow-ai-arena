#include "common.h"

namespace {
// For short aging periods, orders are partitioned by arrival % period. Within
// each bucket priority - arrival / period is immutable. Dispatch merges only
// bucket heads. For long periods we update an ordered set on scheduled birthdays.
vector<int> rank_value, generations;
vector<uint8_t> present, parked;
vector<Distance> static_charge;
vector<int> static_component;
struct Rank {
    bool operator()(int a,int b) const {
        if(rank_value[a]!=rank_value[b]) return rank_value[a]>rank_value[b];
        if(ORDERS[a].arrival_tick!=ORDERS[b].arrival_tick)
            return ORDERS[a].arrival_tick<ORDERS[b].arrival_tick;
        return ORDERS[a].id<ORDERS[b].id;
    }
};
using Bucket=set<int,Rank>;
vector<Bucket> buckets;
set<int> active_buckets;
vector<vector<pair<int,int>>> birthdays;
vector<int> pending_by_arrival;
int pending_count=0;
bool initialized=false, residue_mode=false, no_aging=false;
void init() {
    if(initialized) return;
    initialized=true;
    no_aging=PRM[6]>=T;
    residue_mode=PRM[6]<=1024 || no_aging;
    buckets.resize(residue_mode && !no_aging ? PRM[6] : 1);
    if(!residue_mode) birthdays.resize(T);
    pending_by_arrival.resize(T+1);
    // Distances with all temporary blocks removed are permanent lower bounds.
    static_component.assign(W*H,-1);
    vector<int> component_queue;
    int component=0;
    for(int y=1;y<H-1;y++) for(int x=1;x<W-1;x++) {
        int cell=y*W+x;
        if(MAP[y][x]=='#' || static_component[cell]>=0) continue;
        component_queue.clear();component_queue.push_back(cell);static_component[cell]=component;
        for(size_t head=0;head<component_queue.size();head++) {
            int at=component_queue[head];
            for(int off:{-W,1,W,-1}) {
                int n=at+off;
                if(MAP[n/W][n%W]!='#' && static_component[n]<0) {
                    static_component[n]=component;component_queue.push_back(n);
                }
            }
        }
        ++component;
    }
    static_charge.assign(W*H,NO_DISTANCE);
    vector<int> queue;
    for(auto [x,y]:chargers) {int cell=y*W+x;static_charge[cell]=0;queue.push_back(cell);}
    for(size_t head=0;head<queue.size();head++) {
        int cell=queue[head],nd=static_charge[cell]+1;
        for(int off:{-W,1,W,-1}) {
            int n=cell+off;
            if(MAP[n/W][n%W]!='#' && static_charge[n]==NO_DISTANCE) {
                static_charge[n]=nd;queue.push_back(n);
            }
        }
    }
}
int bucket_of(int idx) {
    return residue_mode && !no_aging ? ORDERS[idx].arrival_tick%PRM[6] : 0;
}
void count_arrival(int tick,int delta) {
    for(int i=tick+1;i<=T;i+=i&-i) pending_by_arrival[i]+=delta;
}
void schedule(int idx,int tick) {
    int next=ORDERS[idx].arrival_tick+((tick-ORDERS[idx].arrival_tick)/PRM[6]+1)*PRM[6];
    if(next<T) birthdays[next].push_back({idx,generations[idx]});
}
void age_orders(int tick) {
    if(residue_mode) return;
    for(auto [idx,gen]:birthdays[tick]) {
        if(!present[idx] || generations[idx]!=gen) continue;
        buckets[0].erase(idx);
        rank_value[idx]=ORDERS[idx].priority+(tick-ORDERS[idx].arrival_tick)/PRM[6];
        buckets[0].insert(idx);
        schedule(idx,tick);
    }
    birthdays[tick].clear();
}
struct Head {
    int bucket,idx,priority;
    Bucket::iterator it;
};
struct Later {
    bool operator()(const Head& a,const Head& b) const {
        if(a.priority!=b.priority) return a.priority<b.priority;
        if(ORDERS[a.idx].arrival_tick!=ORDERS[b.idx].arrival_tick)
            return ORDERS[a.idx].arrival_tick>ORDERS[b.idx].arrival_tick;
        return ORDERS[a.idx].id>ORDERS[b.idx].id;
    }
};
int effective(int idx,int bucket,int tick) {
    return rank_value[idx] - (residue_mode && !no_aging && bucket>tick%PRM[6]);
}
}

void pending_add(int idx,int tick) {
    init();
    if(rank_value.size()<ORDERS.size()) {
        rank_value.resize(ORDERS.size());generations.resize(ORDERS.size());present.resize(ORDERS.size());parked.resize(ORDERS.size());
    }
    if(present[idx]) return;
    rank_value[idx]=residue_mode ? ORDERS[idx].priority-ORDERS[idx].arrival_tick/PRM[6]
                                : ORDERS[idx].priority+(tick-ORDERS[idx].arrival_tick)/PRM[6];
    present[idx]=1;++generations[idx];++pending_count;
    count_arrival(ORDERS[idx].arrival_tick,1);
    const auto& order=ORDERS[idx];
    int lower=(abs(order.px-order.dx)+abs(order.py-order.dy))*PRM[7]+
              distance_value(static_charge[order.dy*W+order.dx])+PRM[3];
    parked[idx]=lower>PRM[0] || static_component[order.py*W+order.px]!=static_component[order.dy*W+order.dx];
    if(parked[idx]) return;
    int bucket=bucket_of(idx);
    buckets[bucket].insert(idx);active_buckets.insert(bucket);
    if(!residue_mode) schedule(idx,tick);
}
void pending_remove(int idx) {
    if(idx>=int(present.size()) || !present[idx]) return;
    int bucket=bucket_of(idx);
    if(!parked[idx]) {
        buckets[bucket].erase(idx);
        if(buckets[bucket].empty()) active_buckets.erase(bucket);
    }
    present[idx]=0;++generations[idx];--pending_count;
    count_arrival(ORDERS[idx].arrival_tick,-1);
}
pair<int,int> pending_report(int tick) {
    init();int aged=0;
    for(int i=tick-PRM[6]+1;i>0;i-=i&-i) aged+=pending_by_arrival[i];
    return {pending_count,aged};
}

void dispatch_orders(int tick) {
    init();age_orders(tick);
    if(active_buckets.empty()) return;
    vector<int> candidates;
    int max_battery=0;
    for(int i=0;i<NR;i++) if(ROBOTS[i].state==STATE_IDLE && ROBOTS[i].battery>=PRM[2]) {
        candidates.push_back(i);max_battery=max(max_battery,ROBOTS[i].battery);
    }
    if(candidates.empty()) return;
    vector<Head> heap;
    heap.reserve(active_buckets.size());
    for(int bucket:active_buckets) {
        auto it=buckets[bucket].begin();
        heap.push_back({bucket,*it,effective(*it,bucket,tick),it});
    }
    make_heap(heap.begin(),heap.end(),Later{});
    while(!heap.empty() && !candidates.empty()) {
        pop_heap(heap.begin(),heap.end(),Later{});
        Head head=heap.back();heap.pop_back();
        auto next=std::next(head.it);
        if(next!=buckets[head.bucket].end()) {
            heap.push_back({head.bucket,*next,effective(*next,head.bucket,tick),next});
            push_heap(heap.begin(),heap.end(),Later{});
        }
        int oidx=head.idx;
        auto& order=ORDERS[oidx];
        int px=order.px,py=order.py,dx=order.dx,dy=order.dy;
        // Manhattan distance is a cheap lower bound even when the source is blocked.
        if((abs(px-dx)+abs(py-dy))*PRM[7]+PRM[3]>max_battery) continue;
        if(order.tail_version!=grid_version) {
            int b=calc_dist(px,py,dx,dy);
            int c=b<BIGNUM ? get_chg_dist(dx,dy) : BIGNUM;
            order.tail_cost=b<BIGNUM && c<BIGNUM ? b*PRM[7]+c+PRM[3] : BIGNUM;
            order.tail_version=grid_version;
        }
        int tail=order.tail_cost;
        if(tail>max_battery || tail>=BIGNUM) continue;
        const Distance* pickup=get_cached_bfs(px,py);
        int best=BIGNUM,who=-1;
        for(int i:candidates) {
            const auto& r=ROBOTS[i];
            if(abs(r.x-px)+abs(r.y-py)+tail>r.battery) continue;
            int a;
            if(r.x==px && r.y==py) a=0;
            else if(!OK(px,py)) continue;
            else if(OK(r.x,r.y)) a=distance_value(pickup[r.y*W+r.x]);
            else {
                a=BIGNUM;
                for(int k=0;k<4;k++) {
                    int nx=r.x+DX4[k],ny=r.y+DY4[k];
                    if(OK(nx,ny)) a=min(a,distance_value(pickup[ny*W+nx])+1);
                }
            }
            if(a<BIGNUM && r.battery>=a+tail && a<=best) {best=a;who=i;}
        }
        if(who<0) continue;
        auto& r=ROBOTS[who];
        r.state=STATE_TO_PICKUP;r.order_id=order.id;
        r.target_x=px;r.target_y=py;r.wait_streak=0;
        order.status=ORD_ASSIGNED;order.assigned_robot=who;
        pending_remove(oidx);
        candidates.erase(find(candidates.begin(),candidates.end(),who));
        max_battery=0;
        for(int i:candidates) max_battery=max(max_battery,ROBOTS[i].battery);
        writeLog(tick,"ASSIGN "+to_string(order.id)+" "+RNAME(who)+" "+to_string(best));
    }
}
