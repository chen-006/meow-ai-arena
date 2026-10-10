// sim.cpp —— 仿真主循环
//
// 每个 tick 的阶段顺序与基线 sim.cpp 完全一致：
//   1 事件 -> 2 救援 -> 3 充电 -> 4 派单 -> 5 低电量去充电 -> 6 移动 -> 7 报表
// 阶段内部的编号顺序、平局规则、日志顺序都不能变（基线的怪行为见 HANDOFF.md）。
#include "common.h"

// 方向顺序：上 右 下 左（所有平局按此顺序）
static const int DX4[4] = {0, 1, 0, -1};
static const int DY4[4] = {-1, 0, 1, 0};

// input.cpp 提供按 tick 分桶的事件：AllEvents()[t] 是本 tick 要处理的事件（输入顺序）。

// ---------------- 占据 ----------------
// 除 DEAD 外每个机器人占 1 格；用每格计数维护，避免基线那样每次 O(R) 扫描。
static vector<int> OCCN;
static int occupancyVersion=0;

static void occAdd(int x, int y, int delta) { OCCN[y * W + x] += delta; ++occupancyVersion; }

bool OCC(int x, int y, int except) {
    int n = OCCN[y * W + x];
    if (except >= 0 && ROB[except].st != R_DEAD && ROB[except].x == x && ROB[except].y == y) n--;
    return n > 0;
}

// ---------------- 待派订单 ----------------
// Within a fixed original priority, arrival/id order NEVER changes under aging.
// Dispatch merges only the nonempty priority buckets at the current tick. This
// avoids re-sorting the full backlog (and handles agingEvery=1 without updates).
struct ArrivalCmp {
    bool operator()(int a,int b) const {
        const Order &A=ORDERS[a], &B=ORDERS[b];
        if(A.arrival!=B.arrival) return A.arrival<B.arrival;
        return A.id<B.id;
    }
};
using PendingBucket=set<int,ArrivalCmp>;
struct PendingOrders {
    PendingBucket buckets[1001];
    set<int> priorities;
    vector<int> fenwick;
    int count=0;
    int scanAfter[1001];
    PendingOrders() {resetScan();}
    void resetScan() {fill(begin(scanAfter),end(scanAfter),-1);}
    void addCount(int arrival,int delta) {
        if(fenwick.empty()) fenwick.assign(T+1,0);
        for(int i=arrival+1;i<=T;i+=i&-i) fenwick[i]+=delta;
        count+=delta;
    }
    void insert(int oi) {
        if(!ORDERS[oi].possible) {addCount(ORDERS[oi].arrival,1);return;}
        auto& b=buckets[ORDERS[oi].prio];
        if(b.insert(oi).second) {
            int& after=scanAfter[ORDERS[oi].prio];
            // Requeued old orders may precede the previous infeasible frontier.
            if(after>=0 && !ArrivalCmp()(after,oi)) after=-1;
            priorities.insert(ORDERS[oi].prio);
            addCount(ORDERS[oi].arrival,1);
        }
    }
    void erase(int oi) {
        if(!ORDERS[oi].possible) {addCount(ORDERS[oi].arrival,-1);return;}
        auto& b=buckets[ORDERS[oi].prio];
        if(b.erase(oi)) {
            if(b.empty()) priorities.erase(ORDERS[oi].prio);
            addCount(ORDERS[oi].arrival,-1);
        }
    }
    int size() const {return count;}
    int aged(int t) const {
        if(fenwick.empty() || t<PRM[6]) return 0;
        int sum=0;
        for(int i=t-PRM[6]+1;i>0;i-=i&-i) sum+=fenwick[i];
        return sum;
    }
};
static PendingOrders PEND;
struct PendingHead {
    PendingBucket::const_iterator it;
    int effective;
};
struct HeadCmp {
    bool operator()(const PendingHead& a,const PendingHead& b) const {
        if(a.effective!=b.effective) return a.effective<b.effective;
        const Order &A=ORDERS[*a.it], &B=ORDERS[*b.it];
        if(A.arrival!=B.arrival) return A.arrival>B.arrival;
        return A.id>B.id;
    }
};

// ---------------- 订单级缓存（b、c 只随封锁变化） ----------------
struct OrderDist {
    int version = -1;
    int b = INF;       // dist(取货点, 送货点)
    int c = INF;       // 送货点到最近可通行充电桩
};
static vector<OrderDist> OCACHE;

// Reverse distance lookup preserves the baseline exception for a blocked
// robot cell: it may exit to an open neighbor, even though a reverse BFS cannot
// enter that blocked cell. A blocked pickup is legal only when already there.
static int distRev(const vector<Distance>& tab, int ax, int ay, int bx, int by) {
    if (ax == bx && ay == by) return 0;
    if (!OK(bx, by)) return INF;
    if (OK(ax, ay)) return tab[ay * W + ax];
    // a 站在被封锁的格子上（基线允许）：第一步必须踩到可通行邻格
    int best = INF;
    for (int k = 0; k < 4; k++) {
        int nx = ax + DX4[k], ny = ay + DY4[k];
        if (!OK(nx, ny)) continue;
        int v = tab[ny * W + nx];
        if (v < best) best = v;
    }
    return best >= INF ? INF : best + 1;
}

// ---------------- 到达目标后的处理（基线移动阶段第 6 步 / 第 1 步） ----------------
static void arrive(int i, int t) {
    Robot& r = ROB[i];
    r.runWait = 0;
    if (r.st == R_TO_PICKUP) {
        Order& o = ORDERS[r.order];
        o.st = O_PICKED;
        r.st = R_DELIVERING;
        r.tx = o.dx;
        r.ty = o.dy;
        OutLine(t, "PICK " + to_string(o.id) + " R" + to_string(i));
    } else if (r.st == R_DELIVERING) {
        Order& o = ORDERS[r.order];
        o.st = O_DONE;
        int lat = t - o.arrival;
        OutLine(t, "DELIVER " + to_string(o.id) + " R" + to_string(i) + " " + to_string(lat));
        r.st = R_IDLE;
        r.order = -1;
        r.tx = r.ty = -1;
    } else if (r.st == R_TO_CHARGER) {
        r.st = R_CHARGING;      // 注意基线不清目标，保持一致
        OutLine(t, "CHARGE R" + to_string(i));
    }
}

// ---------------- 停机（基线移动阶段第 2 步 / 3.8） ----------------
static void die(int i, int t) {
    Robot& r = ROB[i];
    OutLine(t, "DEAD R" + to_string(i));
    if (r.order >= 0) {
        int oi = r.order;
        Order& o = ORDERS[oi];
        if (r.st == R_TO_PICKUP) {
            o.st = O_PENDING;
            o.robot = -1;
            PEND.insert(oi);
            OutLine(t, "REQUEUE " + to_string(o.id));
        } else if (r.st == R_DELIVERING) {
            o.st = O_LOST;
            o.robot = -1;
            OutLine(t, "LOST " + to_string(o.id));
        }
    }
    r.st = R_DEAD;
    r.deadAt = t;
    r.order = -1;
    r.tx = r.ty = -1;
    occAdd(r.x, r.y, -1);       // 停机的机器人不再占据格子
}

// ---------------- 派单（基线 3.4） ----------------
// 基线对每个订单扫全部机器人：选 a 最小的，a 相同取"编号大"的（基线用 a <= best，
// 后面的覆盖前面的；SPEC 说取编号小的，是错的，必须保留基线行为）。
static void dispatch(int t) {
    // Failed orders stay failed while the eligible idle pool only shrinks.
    // A new/recharged/rescued/cancelled robot, or a topology change, resets the
    // per-priority scan frontier. Aging alone never changes feasibility.
    struct Snapshot {bool available=false;int x=0,y=0,battery=0;};
    static vector<Snapshot> previous;
    static int previousVersion=-1;
    static vector<int> candidates;
    if(previous.empty()) previous.resize(NROB);
    candidates.clear();
    bool changed=previousVersion!=BLOCK_VERSION;
    for(int i=0;i<NROB;++i) {
        const Robot& r=ROB[i];auto& old=previous[i];
        bool available=r.st==R_IDLE && r.battery>=PRM[2];
        if(available) {
            if(!old.available || old.x!=r.x || old.y!=r.y || old.battery!=r.battery) changed=true;
            candidates.push_back(i);
        }
        old={available,r.x,r.y,r.battery};
    }
    previousVersion=BLOCK_VERSION;
    if(changed) PEND.resetScan();
    int pool=(int)candidates.size();
    if(!pool || !PEND.size()) return;

    static vector<PendingHead> heads;
    heads.clear();
    for(int p:PEND.priorities) {
        auto it=PEND.scanAfter[p]<0 ? PEND.buckets[p].begin() : PEND.buckets[p].upper_bound(PEND.scanAfter[p]);
        if(it==PEND.buckets[p].end()) continue;
        heads.push_back({it,p+(t-ORDERS[*it].arrival)/PRM[6]});
    }
    make_heap(heads.begin(),heads.end(),HeadCmp());
    while(!heads.empty() && pool>0) {
        pop_heap(heads.begin(),heads.end(),HeadCmp());
        auto cursor=heads.back().it;heads.pop_back();
        int oi=*cursor;
        Order& o=ORDERS[oi];
        PEND.scanAfter[o.prio]=oi;
        ++cursor;
        if(cursor!=PEND.buckets[o.prio].end()) {
            heads.push_back({cursor,o.prio+(t-ORDERS[*cursor].arrival)/PRM[6]});
            push_heap(heads.begin(),heads.end(),HeadCmp());
        }

        const int lowerB=abs(o.px-o.dx)+abs(o.py-o.dy);
        int budget=-1;
        for(int i:candidates) {
            const Robot& r=ROB[i];
            if(r.st!=R_IDLE) continue;
            int left=r.battery-abs(r.x-o.px)-abs(r.y-o.py);
            budget=max(budget,left);
        }
        if(budget<lowerB*PRM[7]+PRM[3]) continue;
        bool reachable=false;
        for(int i:candidates) {
            const Robot& r=ROB[i];
            if(r.st==R_IDLE && Reachable(r.x,r.y,o.px,o.py)) {reachable=true;break;}
        }
        if(!reachable) continue;

        OrderDist& od=OCACHE[oi];
        if(od.version!=BLOCK_VERSION) {
            od.c=CHG_DIST(o.dx,o.dy);
            // Cheap rejection before computing an entire pickup distance map.
            if(od.c>=INF || budget<lowerB*PRM[7]+od.c+PRM[3]) continue;
            if(!Reachable(o.px,o.py,o.dx,o.dy)) continue;
            od.b=DIST(o.px,o.py,o.dx,o.dy);
            od.version=BLOCK_VERSION;
        }
        int b=od.b,c=od.c;
        if(b>=INF || c>=INF) continue;
        const int cost=b*PRM[7]+c+PRM[3];
        if(budget<cost) continue;

        const vector<Distance>& tab=DistTable(o.px,o.py);
        int best=INF,who=-1;
        for(int i:candidates) {
            Robot& r=ROB[i];
            if(r.st!=R_IDLE) continue;
            int lowerA=abs(r.x-o.px)+abs(r.y-o.py);
            if(lowerA>best || r.battery<lowerA+cost) continue;
            int a=distRev(tab,r.x,r.y,o.px,o.py);
            if(a<INF && r.battery>=a+cost && a<=best) {best=a;who=i;}
        }
        if(who<0) continue;
        previous[who].available=false;

        Robot& r = ROB[who];
        r.st = R_TO_PICKUP;
        r.order = oi;
        r.tx = o.px;
        r.ty = o.py;
        r.runWait = 0;
        o.st = O_ASSIGNED;
        o.robot = who;
        PEND.erase(oi);        // 已派的订单不再是候选
        pool--;
        OutLine(t, "ASSIGN " + to_string(o.id) + " R" + to_string(who) + " " + to_string(best));
    }
}

// ---------------- 低电量去充电（基线 3.5） ----------------
static void goCharge(int t) {
    bool needed=false;
    for(const Robot& r:ROB) if(r.st==R_IDLE && r.battery<PRM[2]) {needed=true;break;}
    if(!needed) return;
    static vector<int> reserved,seen;
    if(reserved.empty()) {reserved.resize(W*H);seen.resize(W*H);}
    auto reserve=[&](int x,int y) {
        int u=y*W+x;
        if(seen[u]!=t+1) {seen[u]=t+1;reserved[u]=0;}
        ++reserved[u];
    };
    for(const Robot& r:ROB) {
        if(r.st==R_CHARGING) reserve(r.x,r.y);
        else if(r.st==R_TO_CHARGER) reserve(r.tx,r.ty);
    }
    const vector<pair<int, int> >& cs = ALL_CHG();
    for (int i = 0; i < NROB; i++) {
        Robot& r = ROB[i];
        if (r.st != R_IDLE) continue;
        if (r.battery >= PRM[2]) continue;

        const vector<Distance>& d = DistTable(r.x, r.y);
        int best = INF, cx = -1, cy = -1;
        // 第 1 步：没被占用、且电量够到的最近充电桩（行优先平局取先出现的）
        for (size_t k = 0; k < cs.size(); k++) {
            int x = cs[k].first, y = cs[k].second;
            if (!OK(x, y)) continue;
            if (seen[y*W+x]==t+1 && reserved[y*W+x]) continue;
            int v = d[y * W + x];
            if (v + PRM[3] > r.battery) continue;
            if (v < best) { best = v; cx = x; cy = y; }
        }
        // 第 2 步：都不可用就去最近的排队（不看占用、不看电量；到不了则不动）
        if (cx < 0) {
            int bb = INF;
            for (size_t k = 0; k < cs.size(); k++) {
                int x = cs[k].first, y = cs[k].second;
                if (!OK(x, y)) continue;
                int v = d[y * W + x];
                if (v < bb) { bb = v; cx = x; cy = y; }
            }
        }
        if (cx < 0) continue;
        reserve(cx,cy);
        r.st = R_TO_CHARGER;
        r.tx = cx;
        r.ty = cy;
        r.runWait = 0;
        OutLine(t, "GO_CHARGE R" + to_string(i) + " " + to_string(cx) + " " + to_string(cy));
    }
}

// ---------------- 移动（基线 3.6） ----------------
static void movePhase(int t) {
    for (int i = 0; i < NROB; i++) {
        Robot& r = ROB[i];
        if (r.st != R_TO_PICKUP && r.st != R_DELIVERING && r.st != R_TO_CHARGER) continue;

        if (r.x == r.tx && r.y == r.ty) { arrive(i, t); continue; }
        int stepCost = r.st == R_DELIVERING ? PRM[7] : 1;
        if (r.battery < stepCost) { die(i, t); continue; }
        if (!OK(r.tx, r.ty)) { r.totWait++; continue; }   // 目标被封，干等

        const vector<Distance>& d = MoveTable(r.tx, r.ty, r.x, r.y);      // 从目标往回搜（基线 BFS2）
        int best = INF, bx = -1, by = -1;
        for (int k = 0; k < 4; k++) {
            int nx = r.x + DX4[k], ny = r.y + DY4[k];
            if (!OK(nx, ny)) continue;
            int v = d[ny * W + nx];
            if (v < best) { best = v; bx = nx; by = ny; }  // 平局按方向顺序（严格 <）
        }
        if (best >= INF) { r.totWait++; continue; }        // 走不过去

        if (OCC(bx, by, i)) {
            bool moved = false;
            if (r.runWait >= 4 && !(bx == r.tx && by == r.ty)) {
                // 等太久了，按方向顺序找第一个空的邻格让一让
                for (int k = 0; k < 4; k++) {
                    int nx = r.x + DX4[k], ny = r.y + DY4[k];
                    if (OK(nx, ny) && !OCC(nx, ny, i)) {
                        occAdd(r.x, r.y, -1);
                        occAdd(nx, ny, 1);
                        r.x = nx; r.y = ny;
                        r.battery -= stepCost;
                        r.moves++;
                        r.runWait = 0;
                        OutLine(t, "SIDESTEP R" + to_string(i));
                        moved = true;
                        break;
                    }
                }
            }
            if (!moved) {
                r.totWait++;
                r.runWait++;
                continue;
            }
        } else {
            occAdd(r.x, r.y, -1);
            occAdd(bx, by, 1);
            r.x = bx; r.y = by;
            r.battery -= stepCost;
            r.moves++;
            r.runWait = 0;
        }
        // 走完看看到没到（基线第 6 步）
        if (r.x == r.tx && r.y == r.ty) arrive(i, t);
    }
}

// ---------------- 报表 ----------------
struct Hotspot {int x,y,count;};
static Hotspot hotspot(int live) {
    static int version=-1;
    static Hotspot cached{-1,-1,-1};
    if(version==occupancyVersion) return cached;
    const int R=PRM[5];
    static int first=-2;
    if(first==-2) {
        first=-1;
        for(int u=0;u<W*H;++u) if(MAP[u/W][u%W]!='#') {first=u;break;}
    }
    int hx=first<0 ? -1 : first%W, hy=first<0 ? -1 : first/W, hc=first<0 ? -1 : 0;
    if(R>=W+H-2 || !live) {
        if(first>=0) hc=live;
    } else if(1LL*live*(2LL*R*R+2*R+1) < 1LL*W*H+1LL*live*min(min(W,H),2*R+1)) {
        // Sparse diamonds win for the usual small radius. Timestamped cells
        // avoid clearing or scanning the map; update the row-major argmax as
        // counts increase. BLOCKED cells still participate, walls never do.
        static vector<int> counts,seen;
        static int epoch=0;
        if(counts.empty()) {counts.resize(W*H);seen.resize(W*H);}
        ++epoch;
        int best=first;
        for(const Robot& r:ROB) if(r.st!=R_DEAD) {
            for(int y=max(0,r.y-R);y<=min(H-1,r.y+R);++y) {
                int reach=R-abs(y-r.y);
                for(int x=max(0,r.x-reach);x<=min(W-1,r.x+reach);++x) {
                    if(MAP[y][x]=='#') continue;
                    int u=y*W+x;
                    if(seen[u]!=epoch) {seen[u]=epoch;counts[u]=0;}
                    int value=++counts[u];
                    if(value>hc || (value==hc && u<best)) {hc=value;best=u;}
                }
            }
        }
        if(best>=0) {hx=best%W;hy=best/W;}
    } else {
        // Each diamond intersects a row in one interval. Add its two interval
        // endpoints, then scan each row once. Sweep the shorter map dimension
        // to bound work by WH + robots * min(W,H,2R+1).
        const bool rows=H<=W;
        int major=rows ? H : W, minor=rows ? W : H, stride=minor+1;
        static vector<int> diff;
        diff.assign(major*stride,0);
        for(const Robot& r:ROB) if(r.st!=R_DEAD) {
            int a0=rows ? r.y : r.x,b0=rows ? r.x : r.y;
            for(int a=max(0,a0-R);a<=min(major-1,a0+R);++a) {
                int reach=R-abs(a-a0),lo=max(0,b0-reach),hi=min(minor-1,b0+reach);
                ++diff[a*stride+lo];--diff[a*stride+hi+1];
            }
        }
        int best=first;
        for(int a=0;a<major;++a) {
            int value=0;
            for(int b=0;b<minor;++b) {
                value+=diff[a*stride+b];
                int x=rows ? b : a,y=rows ? a : b,u=y*W+x;
                if(MAP[y][x]=='#') continue;
                if(value>hc || (value==hc && u<best)) {hc=value;best=u;}
            }
        }
        if(best>=0) {hx=best%W;hy=best/W;}
    }

    version=occupancyVersion;
    cached={hx,hy,hc};
    return cached;
}

static void report(int t) {
    int c[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < NROB; i++) {
        switch (ROB[i].st) {
            case R_IDLE: c[0]++; break;
            case R_TO_PICKUP: c[1]++; break;
            case R_DELIVERING: c[2]++; break;
            case R_TO_CHARGER: c[3]++; break;
            case R_CHARGING: c[4]++; break;
            case R_DEAD: c[5]++; break;
        }
    }

    auto [hx,hy,hc]=hotspot(NROB-c[5]);
    int aged = PEND.aged(t);
    OutLine(t, "REPORT pending=" + to_string((long long)PEND.size()) +
                   " aged=" + to_string(aged) + " idle=" + to_string(c[0]) + " to_pickup=" + to_string(c[1]) +
                   " delivering=" + to_string(c[2]) + " to_charger=" + to_string(c[3]) +
                   " charging=" + to_string(c[4]) + " dead=" + to_string(c[5]) +
                   " hot=" + to_string(hx) + "," + to_string(hy) + "," + to_string(hc));
}

// ---------------- 事件（基线 3.1） ----------------
static void handleEvents(int t) {
    // No phase inside event handling queries dynamic routes. Commit only net
    // block changes after the tick's events, avoiding invalidation for toggles
    // that cancel one another in the same tick.
    static vector<int> touched,seen;
    static vector<char> before;
    if(seen.empty()) {seen.resize(W*H);before.resize(W*H);}
    touched.clear();
    auto setBlocked=[&](int x,int y,char value) {
        int u=y*W+x;
        if(seen[u]!=t+1) {seen[u]=t+1;before[u]=BLOCKED[u];touched.push_back(u);}
        BLOCKED[u]=value;
    };
    const vector<vector<Evt> >& ev = AllEvents();
    for (size_t k = 0; k < ev[t].size(); k++) {
        const Evt& e = ev[t][k];
        if (e.type == 0) {                              // ORDER
            int px = e.a, py = e.b, dx = e.c, dy = e.d;
            bool good = INMAP(px, py) && INMAP(dx, dy) && !ISWALL(px, py) && !ISWALL(dx, dy);
            if (!good) {
                OutLine(t, "REJECT " + to_string(e.id));
                continue;
            }
            int oi = (int)ORDERS.size();
            Order o;
            o.id = e.id; o.px = px; o.py = py; o.dx = dx; o.dy = dy;
            o.prio = e.prio; o.arrival = t; o.st = O_PENDING; o.robot = -1;
            o.possible=CanEverDispatch(px,py,dx,dy);
            ORDERS.push_back(o);
            OCACHE.emplace_back();
            OID[e.id] = oi;
            PEND.insert(oi);
        } else if (e.type == 1) {                       // CANCEL
            auto it = OID.find(e.id);
            if (it == OID.end()) {
                OutLine(t, "CANCEL_FAIL " + to_string(e.id));
                continue;
            }
            CNT_CANCELLED++;        // 基线：订单存在就先计数，哪怕后面取消失败（怪行为，保留）
            Order& o = ORDERS[it->second];
            if (o.st == O_PENDING) {
                PEND.erase(it->second);
                o.st = O_CANCELLED;
                o.robot = -1;
                OutLine(t, "CANCEL " + to_string(e.id));
            } else if (o.st == O_ASSIGNED) {
                Robot& r = ROB[o.robot];
                r.st = R_IDLE;
                r.order = -1;
                r.tx = r.ty = -1;
                r.runWait = 0;
                o.st = O_CANCELLED;
                o.robot = -1;
                OutLine(t, "CANCEL " + to_string(e.id));
            } else {
                OutLine(t, "CANCEL_FAIL " + to_string(e.id));
            }
        } else if (e.type == 2) {                       // BLOCK
            int x = e.a, y = e.b;
            if (INMAP(x, y) && MAP[y][x] != '#') {
                if (!BLOCKED[y * W + x]) {
                    setBlocked(x,y,1);
                }
            }
        } else if (e.type == 3) {                       // UNBLOCK
            int x = e.a, y = e.b;
            if (INMAP(x, y) && BLOCKED[y * W + x]) {
                setBlocked(x,y,0);
            }
        }
    }
    for(int u:touched) if(before[u]!=BLOCKED[u]) DistInvalidate(u%W,u/W);
}

// ---------------- 主循环 ----------------
void RunSim() {
    InitDistances();
    OCCN.assign((size_t)W * H, 0);
    for (int i = 0; i < NROB; i++) occAdd(ROB[i].x, ROB[i].y, 1);

    for (int t = 0; t < T; t++) {
        handleEvents(t);

        // 救援：停机满 100 tick 且所在格没被占 -> 半血复活（基线是 PRM[0]/2，不是满电）
        for (int i = 0; i < NROB; i++) {
            Robot& r = ROB[i];
            if (r.st != R_DEAD) continue;
            if (t - r.deadAt >= 100 && !OCC(r.x, r.y, i)) {
                r.st = R_IDLE;
                r.battery = PRM[0] / 2;
                r.runWait = 0;
                r.deadAt = -1;
                occAdd(r.x, r.y, 1);
                OutLine(t, "RESCUE R" + to_string(i));
            }
        }

        // 充电：加 chargeRate，到 batteryMax 的 90% 就走（基线 RB*10 >= PRM[0]*9）
        for (int i = 0; i < NROB; i++) {
            Robot& r = ROB[i];
            if (r.st != R_CHARGING) continue;
            r.battery += PRM[1];
            if (r.battery > PRM[0]) r.battery = PRM[0];
            if (r.battery * 10 >= PRM[0] * 9) {
                r.st = R_IDLE;
                r.tx = r.ty = -1;
                OutLine(t, "CHARGED R" + to_string(i));
            }
        }

        dispatch(t);
        goCharge(t);
        movePhase(t);

        if ((t + 1) % PRM[4] == 0) report(t);
    }
}

// ---------------- 收尾（基线 Finish） ----------------
void Finish() {
    long long open = 0;
    for (size_t i = 0; i < ORDERS.size(); i++) {
        OState st = ORDERS[i].st;
        if (st == O_PENDING || st == O_ASSIGNED || st == O_PICKED) open++;
    }
    OutRaw("SUMMARY delivered=" + to_string(CNT_DELIVERED) + " lost=" + to_string(CNT_LOST) +
           " rejected=" + to_string(CNT_REJECTED) + " cancelled=" + to_string(CNT_CANCELLED) +
           " open=" + to_string(open));
    long long avg = 0;
    if (CNT_DELIVERED > 0) avg = (LAT_SUM + CNT_DELIVERED / 2) / CNT_DELIVERED;   // 四舍五入的整数除法（基线行为）
    OutRaw("LATENCY sum=" + to_string(LAT_SUM) + " max=" + to_string(LAT_MAX) + " avg=" + to_string(avg));
    for (int i = 0; i < NROB; i++) {
        const Robot& r = ROB[i];
        OutRaw("ROBOT R" + to_string(i) + " " + to_string(r.x) + " " + to_string(r.y) + " " +
               to_string(r.battery) + " " +
               (r.st == R_IDLE ? "IDLE" : r.st == R_TO_PICKUP ? "TO_PICKUP" : r.st == R_DELIVERING ? "DELIVERING" :
                r.st == R_TO_CHARGER ? "TO_CHARGER" : r.st == R_CHARGING ? "CHARGING" : "DEAD") +
               " " + to_string(r.moves) + " " + to_string(r.totWait));
    }
}
