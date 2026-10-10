// sim.cpp   仿真主体
// 2022 老王 初版
// 2023 张   改派单、加救援
// 2024 李   加了报表、改充电（按运营要求）
// 注意：这个文件很重要，改之前跟我说一声 —— 张
#include "common.h"

extern int DX4[4];
extern int DY4[4];


// 读输入
void ReadAll(istream& in) {
    in >> W >> H >> T;
    MAP.resize(H);
    for (int i = 0; i < H; i++) in >> MAP[i];
    string tmp;
    in >> tmp;   // "PARAMS"
    for (int i = 0; i < 6; i++) in >> PRM[i];
    in >> tmp >> NR;   // "ROBOTS"
    RX.reserve(NR);
    RY.reserve(NR);
    RB.reserve(NR);
    RTX.reserve(NR);
    RTY.reserve(NR);
    RWS.reserve(NR);
    RDS.reserve(NR);
    RWT.reserve(NR);
    RTR.reserve(NR);
    RST.reserve(NR);
    RSTATE.reserve(NR);
    ROID.reserve(NR);
    for (int i = 0; i < NR; i++) {
        int a, b;
        in >> a >> b;
        RX.push_back(a);
        RY.push_back(b);
        RB.push_back(PRM[0]);   // 满电出发
        RTX.push_back(-1);
        RTY.push_back(-1);
        RWS.push_back(0);
        RDS.push_back(-1);
        RWT.push_back(0);
        RTR.push_back(0);
        RST.push_back("IDLE");
        RSTATE.push_back(RST_IDLE);
        ROID.push_back("");
    }
    int ne = 0;
    in >> tmp >> ne;   // "EVENTS"
    EVT.reserve(ne);
    ORD.reserve(ne);
    LOGBUF.reserve(100000);
    string line;
    getline(in, line);
    for (int i = 0; i < ne; i++) {
        getline(in, line);
        if (line.size() > 0 && line[line.size() - 1] == '\r') line = line.substr(0, line.size() - 1);
        EVT.push_back(line);
    }
}

// 主循环 ----------------------------------------------------------------
void RunSim() {
    clearBfsCache();
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            g_cellOk[y * W + x] = (MAP[y][x] != '#') ? 1 : 0;
        }
    }
    for (const auto& p : BLK) {
        if (INMAP(p.first, p.second)) {
            g_cellOk[p.second * W + p.first] = 0;
        }
    }
    int evPtr = 0;
    for (int t = 0; t < T; t++) {
        updateAdaptiveMode(t);
        // ====================== 1. 事件 ======================
        bool mapChangedThisTick = false;
        while (evPtr < (int)EVT.size()) {
            vector<string> w = SPLIT(EVT[evPtr]);
            if (w.size() < 2) { evPtr++; continue; }
            int evT = S2I(w[0]);
            if (evT > t) break;
            evPtr++;
            if (evT < t) continue;
            string typ = w[1];
            if (typ == "ORDER") {
                string id = w[2];
                int px = S2I(w[3]), py = S2I(w[4]), dx = S2I(w[5]), dy = S2I(w[6]);
                int pr = S2I(w[7]);
                bool good = true;
                if (!INMAP(px, py)) good = false;
                if (good && !INMAP(dx, dy)) good = false;
                if (good && ISWALL(px, py)) good = false;
                if (good && ISWALL(dx, dy)) good = false;
                if (!good) {
                    writeLog(t, "REJECT " + id);
                    continue;
                }
                int intId = S2I(id);
                ORD[id] = Order{px, py, dx, dy, pr, t, intId, OST_PENDING, "PENDING", ""};
                PEND.insert(id);
            } else if (typ == "CANCEL") {
                string id = w[2];
                auto it = ORD.find(id);
                if (it == ORD.end()) {
                    writeLog(t, "CANCEL_FAIL " + id);
                    continue;
                }
                CNT[3]++;
                if (it->second.status_code == OST_PENDING) {
                    PEND.erase(id);
                    it->second.status_code = OST_CANCELLED;
                    it->second.status = "CANCELLED";
                    it->second.robotName = "";
                    writeLog(t, "CANCEL " + id);
                } else if (it->second.status_code == OST_ASSIGNED) {
                    int ri = findRobot(it->second.robotName);
                    setRobotState(ri, RST_IDLE, "IDLE");
                    ROID[ri] = "";
                    RTX[ri] = -1;
                    RTY[ri] = -1;
                    RWS[ri] = 0;
                    it->second.status_code = OST_CANCELLED;
                    it->second.status = "CANCELLED";
                    it->second.robotName = "";
                    writeLog(t, "CANCEL " + id);
                } else {
                    // 已经取货了 / 已经送完了，取消不了
                    writeLog(t, "CANCEL_FAIL " + id);
                }
            } else if (typ == "BLOCK") {
                int x = S2I(w[2]), y = S2I(w[3]);
                if (INMAP(x, y)) {
                    if (!ISWALL(x, y)) {
                        if (BLK.insert(make_pair(x, y)).second) {
                            g_cellOk[y * W + x] = 0;
                            mapChangedThisTick = true;
                        }
                    }
                }
            } else if (typ == "UNBLOCK") {
                int x = S2I(w[2]), y = S2I(w[3]);
                if (INMAP(x, y)) {
                    if (BLK.erase(make_pair(x, y))) {
                        if (!ISWALL(x, y)) {
                            g_cellOk[y * W + x] = 1;
                        }
                        mapChangedThisTick = true;
                    }
                }
            } else {
                // 不认识的事件，忽略
            }
        }
        if (mapChangedThisTick) {
            onMapChanged(t);
        }

        // ====================== 2. 救援 ======================
        // 停机 100 tick 以后派人去救，充满电恢复
        for (int i = 0; i < NR; i++) {
            if (RSTATE[i] == RST_DEAD) {
                if (t - RDS[i] >= 100) {
                    if (!OCC(RX[i], RY[i], i)) {
                        setRobotState(i, RST_IDLE, "IDLE");
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
            if (RSTATE[i] != RST_CHARGING) continue;
            RB[i] = RB[i] + PRM[1];
            if (RB[i] > PRM[0]) RB[i] = PRM[0];
            if (RB[i] * 10 >= PRM[0] * 9) {   // 充满了就走
                setRobotState(i, RST_IDLE, "IDLE");
                RTX[i] = -1;
                RTY[i] = -1;
                writeLog(t, "CHARGED " + RNAME(i));
            }
        }

        // ====================== 4. 派单 ======================
#if USE_NEW_DISPATCH
        {
            int idleCount = 0;
            for (int i = 0; i < NR; i++) {
                if (RSTATE[i] == RST_IDLE && RB[i] >= PRM[2]) idleCount++;
            }
            if (idleCount > 0) {
                vector<string> done;
                done.reserve(idleCount);
                for (const string& id : PEND) {
                    if (idleCount == 0) break;
                    int px = ORD[id].px, py = ORD[id].py;
                    int dx = ORD[id].dx, dy = ORD[id].dy;

                    if (!OK(px, py)) continue;

                    const int* dPick = getDistField(px, py);

                    int b = (!OK(dx, dy)) ? BIGNUM : ((px == dx && py == dy) ? 0 : dPick[dy * W + dx]);
                    if (b >= BIGNUM) continue;

                    int c = CHG_DIST(dx, dy);
                    if (c >= BIGNUM) continue;

                    int req = b + c + PRM[3];
                    int best = BIGNUM, who = -1;

                    for (int i = 0; i < NR; i++) {
                        if (RSTATE[i] == RST_IDLE && RB[i] >= PRM[2]) {
                            int a;
                            if (!OK(RX[i], RY[i])) {
                                a = DIST(RX[i], RY[i], px, py);
                            } else {
                                a = (RX[i] == px && RY[i] == py) ? 0 : dPick[RY[i] * W + RX[i]];
                            }
                            if (a < BIGNUM && RB[i] >= a + req) {
                                if (a <= best) {
                                    best = a;
                                    who = i;
                                }
                            }
                        }
                    }
                    if (who == -1) continue;
                    setRobotState(who, RST_TO_PICKUP, "TO_PICKUP");
                    ROID[who] = id;
                    RTX[who] = px;
                    RTY[who] = py;
                    RWS[who] = 0;
                    ORD[id].status_code = OST_ASSIGNED;
                    ORD[id].status = "ASSIGNED";
                    ORD[id].robotName = RNAME(who);
                    done.push_back(id);
                    writeLog(t, "ASSIGN " + id + " " + RNAME(who) + " " + I2S(best));
                    idleCount--;
                }
                for (int k = 0; k < (int)done.size(); k++) {
                    PEND.erase(done[k]);
                }
            }
        }
#else
        oldDispatch(t);
#endif

        // ====================== 5. 没电的去充电 ======================
        for (int i = 0; i < NR; i++) {
            if (RSTATE[i] != RST_IDLE) continue;
            if (RB[i] >= PRM[2]) continue;
            const int* d = getDistField(RX[i], RY[i]);
            vector<pair<int, int> > cs = ALL_CHG();
            int best = BIGNUM;
            int cx = -1, cy = -1;
            for (int k = 0; k < (int)cs.size(); k++) {
                int x = cs[k].first, y = cs[k].second;
                if (!OK(x, y)) continue;
                bool used = false;
                for (int j = 0; j < NR; j++) {
                    if (j == i) continue;
                    if (RSTATE[j] == RST_CHARGING && RX[j] == x && RY[j] == y) used = true;
                    if (RSTATE[j] == RST_TO_CHARGER && RTX[j] == x && RTY[j] == y) used = true;
                }
                if (used) continue;
                int curD = d[y * W + x];
                if (curD + PRM[3] > RB[i]) continue;
                if (curD < best) {
                    best = curD;
                    cx = x;
                    cy = y;
                }
            }
            if (cx < 0) {
                // 都被占了，那就去最近的排队
                pair<int, int> p = CHG_NEAR(RX[i], RY[i]);
                cx = p.first;
                cy = p.second;
            }
            if (cx < 0) continue;
            setRobotState(i, RST_TO_CHARGER, "TO_CHARGER");
            RTX[i] = cx;
            RTY[i] = cy;
            RWS[i] = 0;
            writeLog(t, "GO_CHARGE " + RNAME(i) + " " + I2S(cx) + " " + I2S(cy));
        }

        // ====================== 6. 走 ======================
        for (int i = 0; i < NR; i++) {
            if (RSTATE[i] != RST_TO_PICKUP && RSTATE[i] != RST_DELIVERING && RSTATE[i] != RST_TO_CHARGER) continue;
            if (RX[i] == RTX[i] && RY[i] == RTY[i]) {
                // 已经在目标上了
                RWS[i] = 0;
                if (RSTATE[i] == RST_TO_PICKUP) {
                    string oid = ROID[i];
                    ORD[oid].status_code = OST_PICKED;
                    ORD[oid].status = "PICKED";
                    setRobotState(i, RST_DELIVERING, "DELIVERING");
                    RTX[i] = ORD[oid].dx;
                    RTY[i] = ORD[oid].dy;
                    writeLog(t, "PICK " + oid + " " + RNAME(i));
                } else if (RSTATE[i] == RST_DELIVERING) {
                    string oid = ROID[i];
                    ORD[oid].status_code = OST_DONE;
                    ORD[oid].status = "DONE";
                    int lat = t - ORD[oid].arrival;
                    writeLog(t, "DELIVER " + oid + " " + RNAME(i) + " " + I2S(lat));
                    setRobotState(i, RST_IDLE, "IDLE");
                    ROID[i] = "";
                    RTX[i] = -1;
                    RTY[i] = -1;
                } else if (RSTATE[i] == RST_TO_CHARGER) {
                    setRobotState(i, RST_CHARGING, "CHARGING");
                    writeLog(t, "CHARGE " + RNAME(i));
                }
                continue;
            }
            if (RB[i] == 0) {
                // 没电了，趴窝
                writeLog(t, "DEAD " + RNAME(i));
                if (ROID[i] != "") {
                    string oid = ROID[i];
                    if (RSTATE[i] == RST_TO_PICKUP) {
                        ORD[oid].status_code = OST_PENDING;
                        ORD[oid].status = "PENDING";
                        ORD[oid].robotName = "";
                        PEND.insert(oid);
                        writeLog(t, "REQUEUE " + oid);
                    } else if (RSTATE[i] == RST_DELIVERING) {
                        ORD[oid].status_code = OST_LOST;
                        ORD[oid].status = "LOST";
                        writeLog(t, "LOST " + oid);
                    }
                }
                setRobotState(i, RST_DEAD, "DEAD");
                RDS[i] = t;
                ROID[i] = "";
                RTX[i] = -1;
                RTY[i] = -1;
                continue;
            }
            if (!OK(RTX[i], RTY[i])) {
                RWT[i]++;   // 目标被封了，等着
                continue;
            }
            const int* d = getDistField(RTX[i], RTY[i]);
            int best = BIGNUM, bx = -1, by = -1;
            for (int k = 0; k < 4; k++) {
                int nx = RX[i] + DX4[k], ny = RY[i] + DY4[k];
                if (!OK(nx, ny)) continue;
                int curD = d[ny * W + nx];
                if (curD < best) {
                    best = curD;
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
                if (RSTATE[i] == RST_TO_PICKUP) {
                    string oid = ROID[i];
                    ORD[oid].status_code = OST_PICKED;
                    ORD[oid].status = "PICKED";
                    setRobotState(i, RST_DELIVERING, "DELIVERING");
                    RTX[i] = ORD[oid].dx;
                    RTY[i] = ORD[oid].dy;
                    writeLog(t, "PICK " + oid + " " + RNAME(i));
                } else if (RSTATE[i] == RST_DELIVERING) {
                    string oid = ROID[i];
                    ORD[oid].status_code = OST_DONE;
                    ORD[oid].status = "DONE";
                    int lat = t - ORD[oid].arrival;
                    writeLog(t, "DELIVER " + oid + " " + RNAME(i) + " " + I2S(lat));
                    setRobotState(i, RST_IDLE, "IDLE");
                    ROID[i] = "";
                    RTX[i] = -1;
                    RTY[i] = -1;
                } else if (RSTATE[i] == RST_TO_CHARGER) {
                    setRobotState(i, RST_CHARGING, "CHARGING");
                    writeLog(t, "CHARGE " + RNAME(i));
                }
            }
        }

        // ====================== 7. 报表 ======================
        if ((t + 1) % PRM[4] == 0) {
            int c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0, c5 = 0;
            for (int i = 0; i < NR; i++) {
                if (RSTATE[i] == RST_IDLE) c0++;
                else if (RSTATE[i] == RST_TO_PICKUP) c1++;
                else if (RSTATE[i] == RST_DELIVERING) c2++;
                else if (RSTATE[i] == RST_TO_CHARGER) c3++;
                else if (RSTATE[i] == RST_CHARGING) c4++;
                else if (RSTATE[i] == RST_DEAD) c5++;
            }
            // 热点（李: 运营要看哪里堵）
            int hx = -1, hy = -1, hc = -1;
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    if (MAP[y][x] == '#') continue;
                    int n = 0;
                    for (int i = 0; i < NR; i++) {
                        if (RSTATE[i] == RST_DEAD) continue;
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
    for (auto it = ORD.begin(); it != ORD.end(); ++it) {
        if (it->second.status_code == OST_PENDING || it->second.status_code == OST_ASSIGNED || it->second.status_code == OST_PICKED) open++;
    }
    writeRaw("SUMMARY delivered=" + I2S(CNT[0]) + " lost=" + I2S(CNT[1]) + " rejected=" + I2S(CNT[2]) +
             " cancelled=" + I2S(CNT[3]) + " open=" + I2S(open));
    long long avg = 0;
    if (CNT[0] > 0) avg = (CNT[4] + CNT[0] / 2) / CNT[0];   // 平均延迟，整数除法
    writeRaw("LATENCY sum=" + I2S(CNT[4]) + " max=" + I2S(CNT[5]) + " avg=" + I2S(avg));
    for (int i = 0; i < NR; i++) {
        writeRaw("ROBOT " + RNAME(i) + " " + I2S(RX[i]) + " " + I2S(RY[i]) + " " + I2S(RB[i]) + " " + RST[i] + " " +
                 I2S(RTR[i]) + " " + I2S(RWT[i]));
    }
}
