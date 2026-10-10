// sim.cpp   仿真主体（第二阶段重写版）
// 每个 tick 的 7 个阶段顺序与基线完全一致：事件 → 救援 → 充电 → 派单 → 去充电 → 走 → 报表
#include "common.h"

static const int DX4[4] = {0, 1, 0, -1};
static const int DY4[4] = {-1, 0, 1, 0};   // 上 右 下 左

// ---------------- 输出 ----------------
static inline void logBegin(int t) { appendInt(OUTBUF, t); OUTBUF.push_back(' '); }
static inline void logRobot(int i) { OUTBUF.push_back('R'); appendInt(OUTBUF, i); }
static inline void logEnd() { OUTBUF.push_back('\n'); }

static inline void logSimple(int t, const char* what, int robot) {   // "t WHAT Ri"
    logBegin(t); OUTBUF += what; OUTBUF.push_back(' '); logRobot(robot); logEnd();
}
static inline void logOrder(int t, const char* what, uint32_t id) {   // "t WHAT id"
    logBegin(t); OUTBUF += what; OUTBUF.push_back(' '); appendInt(OUTBUF, id); logEnd();
}

// ---------------- 读输入 ----------------
static inline long long parseInt(const char*& p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\v' || *p == '\f') p++;
    bool neg = false;
    if (*p == '-') { neg = true; p++; } else if (*p == '+') p++;
    long long v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    return neg ? -v : v;
}
static inline void skipWs(const char*& p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\v' || *p == '\f') p++;
}

void ReadAll() {
    ios::sync_with_stdio(false);
    cin >> W >> H >> T;
    N = W * H;
    MAPC.resize(N);
    CELL_OK.assign(N, 0);
    for (int y = 0; y < H; y++) {
        string row;
        cin >> row;
        for (int x = 0; x < W; x++) {
            char ch = row[x];
            MAPC[y * W + x] = ch;
            CELL_OK[y * W + x] = (ch != '#');
            if (ch == 'C') CHG_CELLS.push_back(y * W + x);
        }
    }
    string tmp;
    cin >> tmp;   // PARAMS
    for (int i = 0; i < 8; i++) cin >> PRM[i];
    cin >> tmp >> NR;   // ROBOTS
    RB.resize(NR);
    OCC_CNT.assign(N, 0);
    for (int i = 0; i < NR; i++) {
        Robot& r = RB[i];
        cin >> r.x >> r.y;
        r.bat = (int)PRM[0];
        r.tx = r.ty = -1;
        r.ws = 0; r.ds = -1; r.wt = 0; r.tr = 0;
        r.st = S_IDLE; r.oid = -1;
        OCC_CNT[r.y * W + r.x]++;
    }
    int ne = 0;
    cin >> tmp >> ne;   // EVENTS
    string line;
    getline(cin, line);
    EVTS.reserve(ne);
    for (int i = 0; i < ne; i++) {
        if (!getline(cin, line)) break;
        const char* p = line.c_str();
        Event ev;
        ev.tick = (int)parseInt(p);
        skipWs(p);
        const char* ts = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r') p++;
        string typ(ts, p);
        ev.id = 0; ev.a = ev.b = ev.c = ev.d = ev.e = 0;
        if (typ == "ORDER") {
            ev.type = E_ORDER;
            ev.id = (uint32_t)parseInt(p);
            ev.a = (int)parseInt(p); ev.b = (int)parseInt(p);
            ev.c = (int)parseInt(p); ev.d = (int)parseInt(p);
            ev.e = (int)parseInt(p);
        } else if (typ == "CANCEL") {
            ev.type = E_CANCEL;
            ev.id = (uint32_t)parseInt(p);
        } else if (typ == "BLOCK") {
            ev.type = E_BLOCK;
            ev.a = (int)parseInt(p); ev.b = (int)parseInt(p);
        } else if (typ == "UNBLOCK") {
            ev.type = E_UNBLOCK;
            ev.a = (int)parseInt(p); ev.b = (int)parseInt(p);
        } else {
            ev.type = E_OTHER;
        }
        EVTS.push_back(ev);
    }
    ORD_IDX.reserve(ne * 2 + 16);
    ORDS.reserve(ne + 16);
    OUTBUF.reserve(1 << 24);
}

// ---------------- 距离工具（语义同基线） ----------------
static inline int fieldVal(const uint16_t* f, int cell) {
    uint16_t v = f[cell];
    return v == DIST_INF ? BIGNUM : (int)v;
}

// 基线 CHG_DIST(d)：从 d 出发 BFS（起点不论可否通行），到最近可通行充电桩的距离
static int chgDist(int dcell) {
    if (CELL_OK[dcell]) return chargerDistAt(dcell);
    int x = dcell % W, y = dcell / W, best = BIGNUM;
    for (int k = 0; k < 4; k++) {
        int nx = x + DX4[k], ny = y + DY4[k];
        if (!OK(nx, ny)) continue;
        int v = chargerDistAt(ny * W + nx);
        if (v < BIGNUM && v + 1 < best) best = v + 1;
    }
    return best;
}

static void removePending(int oidx) {
    for (size_t k = 0; k < PEND.size(); k++)
        if (PEND[k] == oidx) { PEND.erase(PEND.begin() + k); break; }
}

// 机器人到达目标格后的状态切换（取货 / 送达 / 开始充电）
static void arrive(int t, int i) {
    Robot& r = RB[i];
    r.ws = 0;
    if (r.st == S_TO_PICKUP) {
        Order& o = ORDS[r.oid];
        o.st = O_PICKED;
        r.st = S_DELIVERING;
        r.tx = o.dx; r.ty = o.dy;
        logBegin(t); OUTBUF += "PICK "; appendInt(OUTBUF, o.id); OUTBUF.push_back(' '); logRobot(i); logEnd();
    } else if (r.st == S_DELIVERING) {
        Order& o = ORDS[r.oid];
        o.st = O_DONE;
        long long lat = t - o.arrival;
        CNT[0]++; CNT[4] += lat; if (lat > CNT[5]) CNT[5] = lat;
        logBegin(t); OUTBUF += "DELIVER "; appendInt(OUTBUF, o.id); OUTBUF.push_back(' '); logRobot(i);
        OUTBUF.push_back(' '); appendInt(OUTBUF, lat); logEnd();
        r.st = S_IDLE; r.oid = -1; r.tx = r.ty = -1;
    } else if (r.st == S_TO_CHARGER) {
        r.st = S_CHARGING;
        logSimple(t, "CHARGE", i);
    }
}

// ---------------- 主循环 ----------------
void RunSim() {
    size_t evp = 0;
    vector<int> idleK;                       // 派单候选：IDLE 且电量 ≥ 低电量阈值
    vector<pair<uint64_t, int>> heap;        // 派单排序堆
    vector<int> chgUsed(N, 0);               // 去充电阶段：充电桩被占/被预定的计数
    vector<int> diff;                        // 报表热点差分数组
    const long long carry = PRM[7], aging = PRM[6];

    for (int t = 0; t < T; t++) {
        // ====================== 1. 事件 ======================
        for (; evp < EVTS.size() && EVTS[evp].tick == t; evp++) {
            const Event& ev = EVTS[evp];
            if (ev.type == E_ORDER) {
                int px = ev.a, py = ev.b, dx = ev.c, dy = ev.d;
                bool good = INMAP(px, py) && INMAP(dx, dy) && !ISWALL(px, py) && !ISWALL(dx, dy);
                if (!good) {
                    CNT[2]++;
                    logOrder(t, "REJECT", ev.id);
                    continue;
                }
                Order o;
                o.px = px; o.py = py; o.dx = dx; o.dy = dy; o.prio = ev.e; o.arrival = t;
                o.st = O_PENDING; o.robot = -1; o.id = ev.id; o.bcVer = -1; o.bVal = o.cVal = BIGNUM;
                int idx = (int)ORDS.size();
                ORDS.push_back(o);
                ORD_IDX[ev.id] = idx;
                PEND.push_back(idx);
            } else if (ev.type == E_CANCEL) {
                auto it = ORD_IDX.find(ev.id);
                if (it == ORD_IDX.end()) { logOrder(t, "CANCEL_FAIL", ev.id); continue; }
                CNT[3]++;
                Order& o = ORDS[it->second];
                if (o.st == O_PENDING) {
                    removePending(it->second);
                    o.st = O_CANCELLED; o.robot = -1;
                    logOrder(t, "CANCEL", ev.id);
                } else if (o.st == O_ASSIGNED) {
                    Robot& r = RB[o.robot];
                    r.st = S_IDLE; r.oid = -1; r.tx = r.ty = -1; r.ws = 0;
                    o.st = O_CANCELLED; o.robot = -1;
                    logOrder(t, "CANCEL", ev.id);
                } else {
                    logOrder(t, "CANCEL_FAIL", ev.id);
                }
            } else if (ev.type == E_BLOCK) {
                if (INMAP(ev.a, ev.b) && !ISWALL(ev.a, ev.b)) setBlocked(ev.a, ev.b, true);
            } else if (ev.type == E_UNBLOCK) {
                if (INMAP(ev.a, ev.b) && !ISWALL(ev.a, ev.b)) setBlocked(ev.a, ev.b, false);
            }
        }

        // ====================== 2. 救援 ======================
        for (int i = 0; i < NR; i++) {
            Robot& r = RB[i];
            if (r.st != S_DEAD) continue;
            if (t - r.ds >= 100 && !OCC(r.x, r.y, i)) {
                r.st = S_IDLE;
                r.bat = (int)(PRM[0] / 2);
                r.ws = 0; r.ds = -1;
                OCC_CNT[r.y * W + r.x]++;
                logSimple(t, "RESCUE", i);
            }
        }

        // ====================== 3. 充电 ======================
        for (int i = 0; i < NR; i++) {
            Robot& r = RB[i];
            if (r.st != S_CHARGING) continue;
            r.bat += (int)PRM[1];
            if (r.bat > PRM[0]) r.bat = (int)PRM[0];
            if ((long long)r.bat * 10 >= PRM[0] * 9) {
                r.st = S_IDLE; r.tx = r.ty = -1;
                logSimple(t, "CHARGED", i);
            }
        }

        // ====================== 4. 派单 ======================
        // 没有候选机器人时整个阶段无任何效果，直接跳过（省掉排序）
        idleK.clear();
        for (int i = 0; i < NR; i++)
            if (RB[i].st == S_IDLE && RB[i].bat >= PRM[2]) idleK.push_back(i);
        if (!idleK.empty() && !PEND.empty()) {
            // 排序键：有效优先级大 → 到达早 → 编号小；打包成 uint64 做大顶堆
            heap.clear();
            heap.reserve(PEND.size());
            for (int oidx : PEND) {
                const Order& o = ORDS[oidx];
                uint64_t eff = (uint64_t)(o.prio + (t - o.arrival) / aging);
                uint64_t key = (eff << 48) | ((uint64_t)(65535 - o.arrival) << 32) | (uint64_t)(0xFFFFFFFFu - o.id);
                heap.push_back(make_pair(key, oidx));
            }
            make_heap(heap.begin(), heap.end());
            while (!heap.empty() && !idleK.empty()) {
                pop_heap(heap.begin(), heap.end());
                int oidx = heap.back().second;
                heap.pop_back();
                Order& o = ORDS[oidx];
                int pcell = o.py * W + o.px, dcell = o.dy * W + o.dx;
                // b、c 只和封锁版本有关，缓存在订单里
                if (o.bcVer != BLK_VER) {
                    o.bcVer = BLK_VER;
                    if (pcell == dcell) o.bVal = 0;
                    else if (!CELL_OK[dcell]) o.bVal = BIGNUM;
                    else o.bVal = distFrom(pcell, dcell);
                    o.cVal = chgDist(dcell);
                }
                if (o.bVal >= BIGNUM || o.cVal >= BIGNUM) continue;
                long long need = (long long)o.bVal * carry + o.cVal + PRM[3];
                if (need > PRM[0]) continue;   // 任何机器人电量都不够
                bool pOk = CELL_OK[pcell] != 0;
                int best = BIGNUM, who = -1, bestPos = -1;
                for (size_t k = 0; k < idleK.size(); k++) {
                    int i = idleK[k];
                    const Robot& r = RB[i];
                    int rcell = r.y * W + r.x;
                    int a;
                    if (rcell == pcell) a = 0;
                    else if (!pOk) a = BIGNUM;                       // 基线 DIST：终点不可通行直接 BIGNUM
                    else if (CELL_OK[rcell]) a = distFrom(pcell, rcell);   // 两端都可通行时距离对称，复用取货点的场
                    else a = distFrom(rcell, pcell);                 // 机器人站在封锁格上：按基线从机器人出发算
                    if (a >= BIGNUM) continue;
                    if (r.bat >= a + need) {
                        if (a <= best) { best = a; who = i; bestPos = (int)k; }   // 相同距离取编号大的（基线行为）
                    }
                }
                if (who == -1) continue;
                Robot& r = RB[who];
                r.st = S_TO_PICKUP; r.oid = oidx; r.tx = o.px; r.ty = o.py; r.ws = 0;
                o.st = O_ASSIGNED; o.robot = who;
                idleK.erase(idleK.begin() + bestPos);
                removePending(oidx);
                logBegin(t); OUTBUF += "ASSIGN "; appendInt(OUTBUF, o.id); OUTBUF.push_back(' ');
                logRobot(who); OUTBUF.push_back(' '); appendInt(OUTBUF, best); logEnd();
            }
        }

        // ====================== 5. 没电的去充电 ======================
        {
            bool prepared = false;
            for (int i = 0; i < NR; i++) {
                Robot& r = RB[i];
                if (r.st != S_IDLE || r.bat >= PRM[2]) continue;
                if (!prepared) {
                    prepared = true;
                    for (int c : CHG_CELLS) chgUsed[c] = 0;
                    for (int j = 0; j < NR; j++) {
                        const Robot& q = RB[j];
                        if (q.st == S_CHARGING) chgUsed[q.y * W + q.x]++;
                        else if (q.st == S_TO_CHARGER) chgUsed[q.ty * W + q.tx]++;
                    }
                }
                int rcell = r.y * W + r.x;
                int best = BIGNUM, ccell = -1;
                for (int c : CHG_CELLS) {
                    if (!CELL_OK[c]) continue;
                    if (chgUsed[c]) continue;
                    int d = distFrom(rcell, c);
                    if ((long long)d + PRM[3] > r.bat) continue;
                    if (d < best) { best = d; ccell = c; }
                }
                if (ccell < 0) {
                    // 都被占了，去最近的排队
                    best = BIGNUM;
                    for (int c : CHG_CELLS) {
                        if (!CELL_OK[c]) continue;
                        int d = distFrom(rcell, c);
                        if (d < best) { best = d; ccell = c; }
                    }
                }
                if (ccell < 0) continue;
                r.st = S_TO_CHARGER; r.tx = ccell % W; r.ty = ccell / W; r.ws = 0;
                chgUsed[ccell]++;
                logBegin(t); OUTBUF += "GO_CHARGE "; logRobot(i); OUTBUF.push_back(' ');
                appendInt(OUTBUF, r.tx); OUTBUF.push_back(' '); appendInt(OUTBUF, r.ty); logEnd();
            }
        }

        // ====================== 6. 走 ======================
        for (int i = 0; i < NR; i++) {
            Robot& r = RB[i];
            if (r.st != S_TO_PICKUP && r.st != S_DELIVERING && r.st != S_TO_CHARGER) continue;
            if (r.x == r.tx && r.y == r.ty) { arrive(t, i); continue; }
            int cost = (r.st == S_DELIVERING) ? (int)carry : 1;
            if (r.bat < cost) {
                // 电量不够走这一步：趴窝
                logSimple(t, "DEAD", i);
                if (r.oid >= 0) {
                    Order& o = ORDS[r.oid];
                    if (r.st == S_TO_PICKUP) {
                        o.st = O_PENDING; o.robot = -1;
                        PEND.push_back(r.oid);
                        logOrder(t, "REQUEUE", o.id);
                    } else if (r.st == S_DELIVERING) {
                        o.st = O_LOST;
                        CNT[1]++;
                        logOrder(t, "LOST", o.id);
                    }
                }
                r.st = S_DEAD; r.ds = t; r.oid = -1; r.tx = r.ty = -1;
                OCC_CNT[r.y * W + r.x]--;
                continue;
            }
            if (!OK(r.tx, r.ty)) { r.wt++; continue; }   // 目标被封了，等着
            const uint16_t* f = fieldForMove(r.ty * W + r.tx, r.x, r.y);
            int best = BIGNUM, bx = -1, by = -1;
            for (int k = 0; k < 4; k++) {
                int nx = r.x + DX4[k], ny = r.y + DY4[k];
                if (!OK(nx, ny)) continue;
                int d = fieldVal(f, ny * W + nx);
                if (d < best) { best = d; bx = nx; by = ny; }
            }
            if (best >= BIGNUM) { r.wt++; continue; }   // 走不过去
            if (OCC(bx, by, i)) {
                bool moved = false;
                if (r.ws >= 4 && !(bx == r.tx && by == r.ty)) {
                    for (int k = 0; k < 4; k++) {
                        int nx = r.x + DX4[k], ny = r.y + DY4[k];
                        if (OK(nx, ny) && !OCC(nx, ny, i)) {
                            OCC_CNT[r.y * W + r.x]--;
                            r.x = nx; r.y = ny;
                            OCC_CNT[r.y * W + r.x]++;
                            r.bat -= cost; r.tr++; r.ws = 0;
                            logSimple(t, "SIDESTEP", i);
                            moved = true;
                            break;
                        }
                    }
                }
                if (!moved) { r.wt++; r.ws++; continue; }
            } else {
                OCC_CNT[r.y * W + r.x]--;
                r.x = bx; r.y = by;
                OCC_CNT[r.y * W + r.x]++;
                r.bat -= cost; r.tr++; r.ws = 0;
            }
            if (r.x == r.tx && r.y == r.ty) arrive(t, i);
        }

        // ====================== 7. 报表 ======================
        if ((t + 1) % PRM[4] == 0) {
            int c[6] = {0, 0, 0, 0, 0, 0};
            for (int i = 0; i < NR; i++) c[RB[i].st]++;
            // 热点：每个非墙格统计曼哈顿半径内的活机器人数；用逐行差分代替逐格枚举
            int rad = (int)PRM[5];
            int WS = W + 1;
            diff.assign((size_t)WS * H, 0);
            for (int i = 0; i < NR; i++) {
                const Robot& r = RB[i];
                if (r.st == S_DEAD) continue;
                int dy0 = max(-rad, -r.y), dy1 = min(rad, H - 1 - r.y);
                for (int dy = dy0; dy <= dy1; dy++) {
                    int span = rad - (dy < 0 ? -dy : dy);
                    int x0 = max(0, r.x - span), x1 = min(W - 1, r.x + span);
                    if (x0 > x1) continue;
                    diff[(r.y + dy) * WS + x0]++;
                    diff[(r.y + dy) * WS + x1 + 1]--;
                }
            }
            int hx = -1, hy = -1, hc = -1;
            for (int y = 0; y < H; y++) {
                int run = 0;
                const int* row = &diff[(size_t)y * WS];
                for (int x = 0; x < W; x++) {
                    run += row[x];
                    if (MAPC[y * W + x] == '#') continue;
                    if (run > hc) { hc = run; hx = x; hy = y; }
                }
            }
            long long aged = 0;
            for (int oidx : PEND)
                if ((t - ORDS[oidx].arrival) / aging >= 1) aged++;
            logBegin(t);
            OUTBUF += "REPORT pending="; appendInt(OUTBUF, (long long)PEND.size());
            OUTBUF += " aged="; appendInt(OUTBUF, aged);
            OUTBUF += " idle="; appendInt(OUTBUF, c[S_IDLE]);
            OUTBUF += " to_pickup="; appendInt(OUTBUF, c[S_TO_PICKUP]);
            OUTBUF += " delivering="; appendInt(OUTBUF, c[S_DELIVERING]);
            OUTBUF += " to_charger="; appendInt(OUTBUF, c[S_TO_CHARGER]);
            OUTBUF += " charging="; appendInt(OUTBUF, c[S_CHARGING]);
            OUTBUF += " dead="; appendInt(OUTBUF, c[S_DEAD]);
            OUTBUF += " hot="; appendInt(OUTBUF, hx); OUTBUF.push_back(','); appendInt(OUTBUF, hy);
            OUTBUF.push_back(','); appendInt(OUTBUF, hc);
            logEnd();
        }
    }
}

// ---------------- 收尾统计 ----------------
void Finish() {
    long long open = 0;
    for (const Order& o : ORDS)
        if (o.st == O_PENDING || o.st == O_ASSIGNED || o.st == O_PICKED) open++;
    OUTBUF += "SUMMARY delivered="; appendInt(OUTBUF, CNT[0]);
    OUTBUF += " lost="; appendInt(OUTBUF, CNT[1]);
    OUTBUF += " rejected="; appendInt(OUTBUF, CNT[2]);
    OUTBUF += " cancelled="; appendInt(OUTBUF, CNT[3]);
    OUTBUF += " open="; appendInt(OUTBUF, open); logEnd();
    long long avg = 0;
    if (CNT[0] > 0) avg = (CNT[4] + CNT[0] / 2) / CNT[0];
    OUTBUF += "LATENCY sum="; appendInt(OUTBUF, CNT[4]);
    OUTBUF += " max="; appendInt(OUTBUF, CNT[5]);
    OUTBUF += " avg="; appendInt(OUTBUF, avg); logEnd();
    for (int i = 0; i < NR; i++) {
        const Robot& r = RB[i];
        OUTBUF += "ROBOT "; logRobot(i);
        OUTBUF.push_back(' '); appendInt(OUTBUF, r.x);
        OUTBUF.push_back(' '); appendInt(OUTBUF, r.y);
        OUTBUF.push_back(' '); appendInt(OUTBUF, r.bat);
        OUTBUF.push_back(' '); OUTBUF += robotStateStr(r.st);
        OUTBUF.push_back(' '); appendInt(OUTBUF, r.tr);
        OUTBUF.push_back(' '); appendInt(OUTBUF, r.wt);
        logEnd();
    }
}
