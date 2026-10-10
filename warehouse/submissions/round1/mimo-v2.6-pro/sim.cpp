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

static void occAdd(int x, int y, int delta) { OCCN[y * W + x] += delta; }

bool OCC(int x, int y, int except) {
    int n = OCCN[y * W + x];
    if (except >= 0 && ROB[except].st != R_DEAD && ROB[except].x == x && ROB[except].y == y) n--;
    return n > 0;
}

// ---------------- 待派订单 ----------------
// 基线的 PEND 是 vector，按"优先级降序 -> 到达升序 -> 编号升序"排序后依次派单。
// 由于订单号互不相同，这个排序是全序，结果与 PEND 原本的顺序无关，
// 所以这里直接用 std::set 维护有序集合（插入/删除 O(log n)）。
struct PendCmp {
    bool operator()(int a, int b) const {
        const Order& A = ORDERS[a];
        const Order& B = ORDERS[b];
        if (A.prio != B.prio) return A.prio > B.prio;
        if (A.arrival != B.arrival) return A.arrival < B.arrival;
        return A.id < B.id;
    }
};
static set<int, PendCmp> PEND;

// ---------------- 订单级缓存（b、c 只随封锁变化） ----------------
struct OrderDist {
    int version = -1;
    int b = INF;       // dist(取货点, 送货点)
    int c = INF;       // 送货点到最近可通行充电桩
};
static vector<OrderDist> OCACHE;

// 两个距离查表助手，对应基线 DIST(a,b) 的两种用法：
//
// distSrc : tab 是"从 a 出发"的距离表 —— 直接查 b。
//           （BFS 起点 a 无论能不能走都是 0，正好就是基线的语义）
// distRev : tab 是"从 b 出发"的距离表 —— 用对称性反查 a。
//           a 可通行时 dist(a,b) == dist(b,a)；a 站在被封锁格子上时
//           基线允许 a 作为 BFS 起点，此时 dist(a,b) = 1 + min(可通行邻格到 b 的距离)。
static int distSrc(const vector<int>& tab, int ax, int ay, int bx, int by) {
    if (ax == bx && ay == by) return 0;          // 基线先判相等：哪怕 b 是墙也是 0
    if (!OK(bx, by)) return INF;
    return tab[by * W + bx];
}

static int distRev(const vector<int>& tab, int ax, int ay, int bx, int by) {
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
    int pool = 0;
    for (int i = 0; i < NROB; i++)
        if (ROB[i].st == R_IDLE && ROB[i].battery >= PRM[2]) pool++;
    if (pool <= 0) return;      // 没有候选机器人，全部订单都派不出去

    for (auto it = PEND.begin(); it != PEND.end() && pool > 0;) {
        int oi = *it;
        Order& o = ORDERS[oi];

        // b、c 与机器人无关，只随封锁变化 —— 记下来反复用
        OrderDist& od = OCACHE[oi];
        if (od.version != BLOCK_VERSION) {
            const vector<int>& tab = DistTable(o.px, o.py);   // 从取货点出发的距离表
            od.b = distSrc(tab, o.px, o.py, o.dx, o.dy);      // dist(取货点, 送货点)
            od.c = CHG_DIST(o.dx, o.dy);
            od.version = BLOCK_VERSION;
        }
        int b = od.b, c = od.c;
        if (b >= INF || c >= INF) { ++it; continue; }   // 任何机器人都不合格

        const vector<int>& tab = DistTable(o.px, o.py);   // 从取货点出发的距离表
        int best = INF, who = -1;
        for (int i = 0; i < NROB; i++) {
            Robot& r = ROB[i];
            if (r.st != R_IDLE) continue;
            if (r.battery < PRM[2]) continue;
            int a = distRev(tab, r.x, r.y, o.px, o.py);   // dist(机器人, 取货点)
            if (a >= INF) continue;
            if (r.battery >= a + b + c + PRM[3]) {
                if (a <= best) {          // 注意是 <=：相同 a 取编号大的（基线行为）
                    best = a;
                    who = i;
                }
            }
        }
        if (who < 0) { ++it; continue; }

        Robot& r = ROB[who];
        r.st = R_TO_PICKUP;
        r.order = oi;
        r.tx = o.px;
        r.ty = o.py;
        r.runWait = 0;
        o.st = O_ASSIGNED;
        o.robot = who;
        it = PEND.erase(it);        // 已派的订单不再是候选
        pool--;
        OutLine(t, "ASSIGN " + to_string(o.id) + " R" + to_string(who) + " " + to_string(best));
    }
}

// ---------------- 低电量去充电（基线 3.5） ----------------
static bool chargerUsed(int x, int y, int self) {
    for (int j = 0; j < NROB; j++) {
        if (j == self) continue;
        const Robot& r = ROB[j];
        if (r.st == R_CHARGING && r.x == x && r.y == y) return true;
        if (r.st == R_TO_CHARGER && r.tx == x && r.ty == y) return true;
    }
    return false;
}

static void goCharge(int t) {
    const vector<pair<int, int> >& cs = ALL_CHG();
    for (int i = 0; i < NROB; i++) {
        Robot& r = ROB[i];
        if (r.st != R_IDLE) continue;
        if (r.battery >= PRM[2]) continue;

        const vector<int>& d = DistTable(r.x, r.y);
        int best = INF, cx = -1, cy = -1;
        // 第 1 步：没被占用、且电量够到的最近充电桩（行优先平局取先出现的）
        for (size_t k = 0; k < cs.size(); k++) {
            int x = cs[k].first, y = cs[k].second;
            if (!OK(x, y)) continue;
            if (chargerUsed(x, y, i)) continue;
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
        if (r.battery == 0) { die(i, t); continue; }
        if (!OK(r.tx, r.ty)) { r.totWait++; continue; }   // 目标被封，干等

        const vector<int>& d = DistTable(r.tx, r.ty);      // 从目标往回搜（基线 BFS2）
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
                        r.battery--;
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
            r.battery--;
            r.moves++;
            r.runWait = 0;
        }
        // 走完看看到没到（基线第 6 步）
        if (r.x == r.tx && r.y == r.ty) arrive(i, t);
    }
}

// ---------------- 报表（基线 3.7） ----------------
// 热点：曼哈顿距离 <= hotRadius 的非停机机器人数最多的非墙格（行优先平局取先）。
// |dx|+|dy| <= R 等价于旋转坐标 u=x+y, v=x-y 上的 |du|<=R 且 |dv|<=R，
// 所以用二维差分 + 前缀和，把基线的 O(W*H*R) 变成 O(W*H + R)。
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

    int N = W + H;                       // u、v 的取值范围是 [0, W+H-2]，多留一格便于写差分
    int NV = N + 1;                      // 差分数组行距：下标 u*NV+v，u、v 最大到 N
    static vector<int> diff;
    if ((int)diff.size() < NV * NV) diff.assign(NV * NV, 0);
    else fill(diff.begin(), diff.end(), 0);

    int R = PRM[5];
    for (int i = 0; i < NROB; i++) {
        if (ROB[i].st == R_DEAD) continue;
        int u0 = ROB[i].x + ROB[i].y;
        int v0 = ROB[i].x - ROB[i].y + (H - 1);
        int u1 = max(0, u0 - R), u2 = min(N - 1, u0 + R);
        int v1 = max(0, v0 - R), v2 = min(N - 1, v0 + R);
        diff[u1 * NV + v1]++;
        diff[u1 * NV + v2 + 1]--;
        diff[(u2 + 1) * NV + v1]--;
        diff[(u2 + 1) * NV + v2 + 1]++;
    }
    for (int u = 0; u < N; u++)            // 先按 v 前缀和
        for (int v = 1; v < N; v++) diff[u * NV + v] += diff[u * NV + v - 1];
    for (int v = 0; v < N; v++)            // 再按 u 前缀和
        for (int u = 1; u < N; u++) diff[u * NV + v] += diff[(u - 1) * NV + v];

    int hx = -1, hy = -1, hc = -1;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (MAP[y][x] == '#') continue;
            int n = diff[(x + y) * NV + (x - y + (H - 1))];
            if (n > hc) { hc = n; hx = x; hy = y; }
        }
    }

    OutLine(t, "REPORT pending=" + to_string((long long)PEND.size()) +
                   " idle=" + to_string(c[0]) + " to_pickup=" + to_string(c[1]) +
                   " delivering=" + to_string(c[2]) + " to_charger=" + to_string(c[3]) +
                   " charging=" + to_string(c[4]) + " dead=" + to_string(c[5]) +
                   " hot=" + to_string(hx) + "," + to_string(hy) + "," + to_string(hc));
}

// ---------------- 事件（基线 3.1） ----------------
static void handleEvents(int t) {
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
            ORDERS.push_back(o);
            OCACHE.emplace_back();
            OID[e.id] = oi;
            PEND.insert(oi);
        } else if (e.type == 1) {                       // CANCEL
            map<int, int>::iterator it = OID.find(e.id);
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
                    BLOCKED[y * W + x] = 1;
                    DistInvalidate();
                }
            }
        } else if (e.type == 3) {                       // UNBLOCK
            int x = e.a, y = e.b;
            if (INMAP(x, y) && BLOCKED[y * W + x]) {
                BLOCKED[y * W + x] = 0;
                DistInvalidate();
            }
        }
    }
}

// ---------------- 主循环 ----------------
void RunSim() {
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
