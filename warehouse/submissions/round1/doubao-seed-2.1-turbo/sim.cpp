// sim.cpp   仿真主体（优化版）
// 行为与基线完全一致，仅数据结构和算法优化
#include "common.h"

extern int DX4[4];
extern int DY4[4];

// 读输入
void ReadAll(istream& in) {
    in >> W >> H >> T;
    MAP.resize(H);
    for (int i = 0; i < H; i++) in >> MAP[i];
    BLK_ARR.assign(H, vector<bool>(W, false));

    // 预计算充电桩位置
    CHG_LIST.clear();
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (MAP[y][x] == 'C') CHG_LIST.push_back({x, y});
        }
    }

    string tmp;
    in >> tmp;   // "PARAMS"
    for (int i = 0; i < 6; i++) in >> PRM[i];
    in >> tmp >> NR;   // "ROBOTS"
    RNAME_CACHE.resize(NR);
    for (int i = 0; i < NR; i++) {
        int a, b;
        in >> a >> b;
        RX.push_back(a);
        RY.push_back(b);
        RB.push_back(PRM[0]);
        RTX.push_back(-1);
        RTY.push_back(-1);
        RWS.push_back(0);
        RDS.push_back(-1);
        RWT.push_back(0);
        RTR.push_back(0);
        RST.push_back(S_IDLE);
        ROID.push_back(-1);
        RNAME_CACHE[i] = "R" + I2S(i);
    }
    int ne = 0;
    in >> tmp >> ne;   // "EVENTS"
    string line;
    getline(in, line);
    EVENTS.reserve(ne);
    for (int i = 0; i < ne; i++) {
        getline(in, line);
        if (line.size() > 0 && line[line.size() - 1] == '\r') line = line.substr(0, line.size() - 1);
        // 预解析事件
        Event ev;
        ev.raw = line;
        vector<string> w = SPLIT(line);
        if (w.size() < 2) {
            ev.typ = EV_OTHER;
            ev.t = 0;
            EVENTS.push_back(ev);
            continue;
        }
        ev.t = S2I(w[0]);
        string typ = w[1];
        if (typ == "ORDER") {
            ev.typ = EV_ORDER;
            ev.id = w[2];
            ev.a = S2I(w[3]); ev.b = S2I(w[4]);
            ev.c = S2I(w[5]); ev.d = S2I(w[6]);
            ev.prio = S2I(w[7]);
        } else if (typ == "CANCEL") {
            ev.typ = EV_CANCEL;
            ev.id = w[2];
        } else if (typ == "BLOCK") {
            ev.typ = EV_BLOCK;
            ev.a = S2I(w[2]); ev.b = S2I(w[3]);
        } else if (typ == "UNBLOCK") {
            ev.typ = EV_UNBLOCK;
            ev.a = S2I(w[2]); ev.b = S2I(w[3]);
        } else {
            ev.typ = EV_OTHER;
        }
        EVENTS.push_back(ev);
    }
}

// 主循环 ----------------------------------------------------------------
void RunSim() {
    // 预分配 BFS 缓冲区
    BFS_DIST.assign(W * H, 0);
    BFS_VISIT.assign(W * H, 0);
    BFS_GEN = 0;

    for (int t = 0; t < T; t++) {
        // ====================== 1. 事件 ======================
        while (EVT_PTR < (int)EVENTS.size() && EVENTS[EVT_PTR].t == t) {
            Event& ev = EVENTS[EVT_PTR];
            EVT_PTR++;
            if (ev.typ == EV_ORDER) {
                int px = ev.a, py = ev.b, dx = ev.c, dy = ev.d;
                int pr = ev.prio;
                bool good = true;
                if (!INMAP(px, py)) good = false;
                if (good && !INMAP(dx, dy)) good = false;
                if (good && ISWALL(px, py)) good = false;
                if (good && ISWALL(dx, dy)) good = false;
                if (!good) {
                    writeLog(t, "REJECT " + ev.id);
                    continue;
                }
                int oidx = (int)ORDS.size();
                Order o;
                o.px = px; o.py = py; o.dx = dx; o.dy = dy;
                o.prio = pr; o.arrival = t;
                o.status = O_PENDING; o.robotIdx = -1;
                o.idStr = ev.id;
                ORDS.push_back(o);
                ORD_MAP[ev.id] = oidx;
                PEND.push_back(oidx);
            } else if (ev.typ == EV_CANCEL) {
                string id = ev.id;
                auto it = ORD_MAP.find(id);
                if (it == ORD_MAP.end()) {
                    writeLog(t, "CANCEL_FAIL " + id);
                    continue;
                }
                int oidx = it->second;
                Order& o = ORDS[oidx];
                CNT[3]++;
                if (o.status == O_PENDING) {
                    vector<int> np;
                    for (int k = 0; k < (int)PEND.size(); k++)
                        if (PEND[k] != oidx) np.push_back(PEND[k]);
                    PEND = np;
                    o.status = O_CANCELLED;
                    o.robotIdx = -1;
                    writeLog(t, "CANCEL " + id);
                } else if (o.status == O_ASSIGNED) {
                    int ri = o.robotIdx;
                    RST[ri] = S_IDLE;
                    ROID[ri] = -1;
                    RTX[ri] = -1;
                    RTY[ri] = -1;
                    RWS[ri] = 0;
                    o.status = O_CANCELLED;
                    o.robotIdx = -1;
                    writeLog(t, "CANCEL " + id);
                } else {
                    writeLog(t, "CANCEL_FAIL " + id);
                }
            } else if (ev.typ == EV_BLOCK) {
                int x = ev.a, y = ev.b;
                if (INMAP(x, y)) {
                    if (!ISWALL(x, y)) {
                        BLK_ARR[y][x] = true;
                        BLK_VERSION++;
                    }
                }
            } else if (ev.typ == EV_UNBLOCK) {
                int x = ev.a, y = ev.b;
                if (INMAP(x, y)) {
                    if (BLK_ARR[y][x]) {
                        BLK_ARR[y][x] = false;
                        BLK_VERSION++;
                    }
                }
            } else {
                // 不认识的事件，忽略
            }
        }

        // ====================== 2. 救援 ======================
        for (int i = 0; i < NR; i++) {
            if (RST[i] == S_DEAD) {
                if (t - RDS[i] >= 100) {
                    if (!OCC(RX[i], RY[i], i)) {
                        RST[i] = S_IDLE;
                        RB[i] = PRM[0] / 2;
                        RWS[i] = 0;
                        RDS[i] = -1;
                        writeLog(t, "RESCUE " + RNAME(i));
                    }
                }
            }
        }

        // ====================== 3. 充电 ======================
        for (int i = 0; i < NR; i++) {
            if (RST[i] != S_CHARGING) continue;
            RB[i] = RB[i] + PRM[1];
            if (RB[i] > PRM[0]) RB[i] = PRM[0];
            if (RB[i] * 10 >= PRM[0] * 9) {
                RST[i] = S_IDLE;
                RTX[i] = -1;
                RTY[i] = -1;
                writeLog(t, "CHARGED " + RNAME(i));
            }
        }

        // ====================== 4. 派单 ======================
#if USE_NEW_DISPATCH
        {
            vector<int> ids = PEND;
            sort(ids.begin(), ids.end(), [](int a, int b) {
                const Order& oa = ORDS[a];
                const Order& ob = ORDS[b];
                if (oa.prio != ob.prio) return oa.prio > ob.prio;
                if (oa.arrival != ob.arrival) return oa.arrival < ob.arrival;
                return S2I(oa.idStr) < S2I(ob.idStr);
            });
            for (int oi = 0; oi < (int)ids.size(); oi++) {
                int oidx = ids[oi];
                Order& o = ORDS[oidx];
                int px = o.px, py = o.py;
                int dx = o.dx, dy = o.dy;
                int best = BIGNUM, who = -1;
                for (int i = 0; i < NR; i++) {
                    if (RST[i] == S_IDLE) {
                        if (RB[i] >= PRM[2]) {
                            int a = DIST(RX[i], RY[i], px, py);
                            if (a < BIGNUM) {
                                int b = DIST(px, py, dx, dy);
                                if (b < BIGNUM) {
                                    int c = CHG_DIST(dx, dy);
                                    if (c < BIGNUM) {
                                        if (RB[i] >= a + b + c + PRM[3]) {
                                            if (a <= best) {
                                                best = a;
                                                who = i;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (who == -1) continue;
                RST[who] = S_TO_PICKUP;
                ROID[who] = oidx;
                RTX[who] = px;
                RTY[who] = py;
                RWS[who] = 0;
                o.status = O_ASSIGNED;
                o.robotIdx = who;
                vector<int> np;
                for (int k = 0; k < (int)PEND.size(); k++)
                    if (PEND[k] != oidx) np.push_back(PEND[k]);
                PEND = np;
                writeLog(t, "ASSIGN " + o.idStr + " " + RNAME(who) + " " + I2S(best));
            }
        }
#else
        oldDispatch(t);
#endif

        // ====================== 5. 没电的去充电 ======================
        for (int i = 0; i < NR; i++) {
            if (RST[i] != S_IDLE) continue;
            if (RB[i] >= PRM[2]) continue;
            BFS_FILL(RX[i], RY[i]);
            int best = BIGNUM;
            int cx = -1, cy = -1;
            for (size_t k = 0; k < CHG_LIST.size(); k++) {
                int x = CHG_LIST[k].first;
                int y = CHG_LIST[k].second;
                if (!OK(x, y)) continue;
                bool used = false;
                for (int j = 0; j < NR; j++) {
                    if (j == i) continue;
                    if (RST[j] == S_CHARGING && RX[j] == x && RY[j] == y) used = true;
                    if (RST[j] == S_TO_CHARGER && RTX[j] == x && RTY[j] == y) used = true;
                }
                if (used) continue;
                int d = _bfs_get(x, y);
                if (d + PRM[3] > RB[i]) continue;
                if (d < best) {
                    best = d;
                    cx = x;
                    cy = y;
                }
            }
            if (cx < 0) {
                pair<int, int> p = CHG_NEAR(RX[i], RY[i]);
                cx = p.first;
                cy = p.second;
            }
            if (cx < 0) continue;
            RST[i] = S_TO_CHARGER;
            RTX[i] = cx;
            RTY[i] = cy;
            RWS[i] = 0;
            writeLog(t, "GO_CHARGE " + RNAME(i) + " " + I2S(cx) + " " + I2S(cy));
        }

        // ====================== 6. 走 ======================
        for (int i = 0; i < NR; i++) {
            if (RST[i] != S_TO_PICKUP && RST[i] != S_DELIVERING && RST[i] != S_TO_CHARGER) continue;
            if (RX[i] == RTX[i] && RY[i] == RTY[i]) {
                // 已经在目标上了
                RWS[i] = 0;
                if (RST[i] == S_TO_PICKUP) {
                    int oidx = ROID[i];
                    Order& o = ORDS[oidx];
                    o.status = O_PICKED;
                    RST[i] = S_DELIVERING;
                    RTX[i] = o.dx;
                    RTY[i] = o.dy;
                    writeLog(t, "PICK " + o.idStr + " " + RNAME(i));
                } else if (RST[i] == S_DELIVERING) {
                    int oidx = ROID[i];
                    Order& o = ORDS[oidx];
                    o.status = O_DONE;
                    int lat = t - o.arrival;
                    writeLog(t, "DELIVER " + o.idStr + " " + RNAME(i) + " " + I2S(lat));
                    RST[i] = S_IDLE;
                    ROID[i] = -1;
                    RTX[i] = -1;
                    RTY[i] = -1;
                } else if (RST[i] == S_TO_CHARGER) {
                    RST[i] = S_CHARGING;
                    writeLog(t, "CHARGE " + RNAME(i));
                }
                continue;
            }
            if (RB[i] == 0) {
                // 没电了，趴窝
                writeLog(t, "DEAD " + RNAME(i));
                if (ROID[i] != -1) {
                    int oidx = ROID[i];
                    Order& o = ORDS[oidx];
                    if (RST[i] == S_TO_PICKUP) {
                        o.status = O_PENDING;
                        o.robotIdx = -1;
                        PEND.push_back(oidx);
                        writeLog(t, "REQUEUE " + o.idStr);
                    } else if (RST[i] == S_DELIVERING) {
                        o.status = O_LOST;
                        writeLog(t, "LOST " + o.idStr);
                    }
                }
                RST[i] = S_DEAD;
                RDS[i] = t;
                ROID[i] = -1;
                RTX[i] = -1;
                RTY[i] = -1;
                continue;
            }
            if (!OK(RTX[i], RTY[i])) {
                RWT[i]++;   // 目标被封了，等着
                continue;
            }
            // 用 BFS 目标缓存：同目标 + 同地形版本 -> 复用
            int tx = RTX[i], ty = RTY[i];
            int* dist_ptr;
            if (MOVE_BFS_CACHE.tx == tx && MOVE_BFS_CACHE.ty == ty && MOVE_BFS_CACHE.blk_ver == BLK_VERSION) {
                dist_ptr = MOVE_BFS_CACHE.dist.data();
            } else {
                BFS_FILL(tx, ty);
                dist_ptr = BFS_DIST.data();
                // 缓存起来（其他机器人可能也用同一个目标）
                MOVE_BFS_CACHE.tx = tx;
                MOVE_BFS_CACHE.ty = ty;
                MOVE_BFS_CACHE.blk_ver = BLK_VERSION;
                MOVE_BFS_CACHE.dist = BFS_DIST;  // 复制
            }
            auto bfs_get_at = [&](int x, int y) -> int {
                return dist_ptr[y * W + x];
            };
            int best = BIGNUM, bx = -1, by = -1;
            for (int k = 0; k < 4; k++) {
                int nx = RX[i] + DX4[k], ny = RY[i] + DY4[k];
                if (!OK(nx, ny)) continue;
                int d = bfs_get_at(nx, ny);
                if (d < best) {
                    best = d;
                    bx = nx;
                    by = ny;
                }
            }
            if (best >= BIGNUM) {
                RWT[i]++;   // 走不过去
                continue;
            }
            if (OCC(bx, by, i)) {
                bool moved = false;
                if (RWS[i] >= 4 && !(bx == RTX[i] && by == RTY[i])) {
                    // 等太久了，让一让
                    for (int k = 0; k < 4; k++) {
                        int nx = RX[i] + DX4[k], ny = RY[i] + DY4[k];
                        if (OK(nx, ny) && !OCC(nx, ny, i)) {
                            RX[i] = nx;
                            RY[i] = ny;
                            RB[i]--;
                            RTR[i]++;
                            RWS[i] = 0;
                            writeLog(t, "SIDESTEP " + RNAME(i));
                            moved = true;
                            break;
                        }
                    }
                }
                if (moved == false) {
                    RWT[i]++;
                    RWS[i]++;
                    continue;
                }
            } else {
                RX[i] = bx;
                RY[i] = by;
                RB[i]--;
                RTR[i]++;
                RWS[i] = 0;
            }
            // 走完看看到没到
            if (RX[i] == RTX[i] && RY[i] == RTY[i]) {
                RWS[i] = 0;
                if (RST[i] == S_TO_PICKUP) {
                    int oidx = ROID[i];
                    Order& o = ORDS[oidx];
                    o.status = O_PICKED;
                    RST[i] = S_DELIVERING;
                    RTX[i] = o.dx;
                    RTY[i] = o.dy;
                    writeLog(t, "PICK " + o.idStr + " " + RNAME(i));
                } else if (RST[i] == S_DELIVERING) {
                    int oidx = ROID[i];
                    Order& o = ORDS[oidx];
                    o.status = O_DONE;
                    int lat = t - o.arrival;
                    writeLog(t, "DELIVER " + o.idStr + " " + RNAME(i) + " " + I2S(lat));
                    RST[i] = S_IDLE;
                    ROID[i] = -1;
                    RTX[i] = -1;
                    RTY[i] = -1;
                } else if (RST[i] == S_TO_CHARGER) {
                    RST[i] = S_CHARGING;
                    writeLog(t, "CHARGE " + RNAME(i));
                }
            }
        }

        // ====================== 7. 报表 ======================
        if ((t + 1) % PRM[4] == 0) {
            int c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0, c5 = 0;
            for (int i = 0; i < NR; i++) {
                switch (RST[i]) {
                    case S_IDLE: c0++; break;
                    case S_TO_PICKUP: c1++; break;
                    case S_DELIVERING: c2++; break;
                    case S_TO_CHARGER: c3++; break;
                    case S_CHARGING: c4++; break;
                    case S_DEAD: c5++; break;
                }
            }
            // 热点
            int hx = -1, hy = -1, hc = -1;
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    if (MAP[y][x] == '#') continue;
                    int n = 0;
                    for (int i = 0; i < NR; i++) {
                        if (RST[i] == S_DEAD) continue;
                        if (abs(RX[i] - x) + abs(RY[i] - y) <= PRM[5]) n++;
                    }
                    if (n > hc) {
                        hc = n;
                        hx = x;
                        hy = y;
                    }
                }
            }
            string s = "REPORT pending=" + I2S(PEND.size()) + " idle=" + I2S(c0) + " to_pickup=" + I2S(c1) +
                       " delivering=" + I2S(c2) + " to_charger=" + I2S(c3) + " charging=" + I2S(c4) +
                       " dead=" + I2S(c5) + " hot=" + I2S(hx) + "," + I2S(hy) + "," + I2S(hc);
            writeLog(t, s);
        }
    }
}

// 收尾统计
void Finish() {
    long long open = 0;
    for (size_t i = 0; i < ORDS.size(); i++) {
        int st = ORDS[i].status;
        if (st == O_PENDING || st == O_ASSIGNED || st == O_PICKED) open++;
    }
    writeRaw("SUMMARY delivered=" + I2S(CNT[0]) + " lost=" + I2S(CNT[1]) + " rejected=" + I2S(CNT[2]) +
             " cancelled=" + I2S(CNT[3]) + " open=" + I2S(open));
    long long avg = 0;
    if (CNT[0] > 0) avg = (CNT[4] + CNT[0] / 2) / CNT[0];
    writeRaw("LATENCY sum=" + I2S(CNT[4]) + " max=" + I2S(CNT[5]) + " avg=" + I2S(avg));
    for (int i = 0; i < NR; i++) {
        writeRaw("ROBOT " + RNAME(i) + " " + I2S(RX[i]) + " " + I2S(RY[i]) + " " + I2S(RB[i]) + " " + robotStatusStr(RST[i]) + " " +
                 I2S(RTR[i]) + " " + I2S(RWT[i]));
    }
}
