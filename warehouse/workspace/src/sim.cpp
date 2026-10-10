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
        ROID.push_back("");
    }
    int ne = 0;
    in >> tmp >> ne;   // "EVENTS"
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
    for (int t = 0; t < T; t++) {
        // ====================== 1. 事件 ======================
        for (int e = 0; e < (int)EVT.size(); e++) {
            vector<string> w = SPLIT(EVT[e]);
            if (w.size() < 2) continue;
            if (S2I(w[0]) != t) continue;
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
                vector<string> f;
                f.push_back(I2S(px));
                f.push_back(I2S(py));
                f.push_back(I2S(dx));
                f.push_back(I2S(dy));
                f.push_back(I2S(pr));
                f.push_back(I2S(t));
                f.push_back("PENDING");
                f.push_back("");
                ORD[id] = f;
                PEND.push_back(id);
            } else if (typ == "CANCEL") {
                string id = w[2];
                if (ORD.find(id) == ORD.end()) {
                    writeLog(t, "CANCEL_FAIL " + id);
                    continue;
                }
                CNT[3]++;
                if (ORD[id][6] == "PENDING") {
                    // 从待派里删掉
                    vector<string> np;
                    for (int k = 0; k < (int)PEND.size(); k++)
                        if (PEND[k] != id) np.push_back(PEND[k]);
                    PEND = np;
                    ORD[id][6] = "CANCELLED";
                    ORD[id][7] = "";
                    writeLog(t, "CANCEL " + id);
                } else if (ORD[id][6] == "ASSIGNED") {
                    int ri = findRobot(ORD[id][7]);
                    RST[ri] = "IDLE";
                    ROID[ri] = "";
                    RTX[ri] = -1;
                    RTY[ri] = -1;
                    RWS[ri] = 0;
                    ORD[id][6] = "CANCELLED";
                    ORD[id][7] = "";
                    writeLog(t, "CANCEL " + id);
                } else {
                    // 已经取货了 / 已经送完了，取消不了
                    writeLog(t, "CANCEL_FAIL " + id);
                }
            } else if (typ == "BLOCK") {
                int x = S2I(w[2]), y = S2I(w[3]);
                if (INMAP(x, y)) {
                    if (!ISWALL(x, y)) {
                        BLK.insert(make_pair(x, y));
                    }
                }
            } else if (typ == "UNBLOCK") {
                int x = S2I(w[2]), y = S2I(w[3]);
                if (INMAP(x, y)) {
                    if (BLK.count(make_pair(x, y))) BLK.erase(make_pair(x, y));
                }
            } else {
                // 不认识的事件，忽略
            }
        }

        // ====================== 2. 救援 ======================
        // 停机 100 tick 以后派人去救，充满电恢复
        for (int i = 0; i < NR; i++) {
            if (RST[i] == "DEAD") {
                if (t - RDS[i] >= 100) {
                    if (!OCC(RX[i], RY[i], i)) {
                        RST[i] = "IDLE";
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
            if (RST[i] != "CHARGING") continue;
            RB[i] = RB[i] + PRM[1];
            if (RB[i] > PRM[0]) RB[i] = PRM[0];
            if (RB[i] * 10 >= PRM[0] * 9) {   // 充满了就走
                RST[i] = "IDLE";
                RTX[i] = -1;
                RTY[i] = -1;
                writeLog(t, "CHARGED " + RNAME(i));
            }
        }

        // ====================== 4. 派单 ======================
#if USE_NEW_DISPATCH
        {
            vector<string> ids = PEND;
            sort(ids.begin(), ids.end(), [](const string& a, const string& b) {
                // 优先级高的先，然后来得早的先，然后编号小的先
                if (S2I(ORD[a][4]) != S2I(ORD[b][4])) return S2I(ORD[a][4]) > S2I(ORD[b][4]);
                if (S2I(ORD[a][5]) != S2I(ORD[b][5])) return S2I(ORD[a][5]) < S2I(ORD[b][5]);
                return S2I(a) < S2I(b);
            });
            for (int oi = 0; oi < (int)ids.size(); oi++) {
                string id = ids[oi];
                int px = S2I(ORD[id][0]), py = S2I(ORD[id][1]);
                int dx = S2I(ORD[id][2]), dy = S2I(ORD[id][3]);
                int best = BIGNUM, who = -1;
                for (int i = 0; i < NR; i++) {
                    if (RST[i] == "IDLE") {
                        if (RB[i] >= PRM[2]) {
                            int a = DIST(RX[i], RY[i], px, py);
                            if (a < BIGNUM) {
                                int b = DIST(px, py, dx, dy);
                                if (b < BIGNUM) {
                                    int c = CHG_DIST(dx, dy);
                                    if (c < BIGNUM) {
                                        if (RB[i] >= a + b + c + PRM[3]) {
                                            // 选最近的，一样近选编号小的
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
                RST[who] = "TO_PICKUP";
                ROID[who] = id;
                RTX[who] = px;
                RTY[who] = py;
                RWS[who] = 0;
                ORD[id][6] = "ASSIGNED";
                ORD[id][7] = RNAME(who);
                vector<string> np;
                for (int k = 0; k < (int)PEND.size(); k++)
                    if (PEND[k] != id) np.push_back(PEND[k]);
                PEND = np;
                writeLog(t, "ASSIGN " + id + " " + RNAME(who) + " " + I2S(best));
            }
        }
#else
        oldDispatch(t);
#endif

        // ====================== 5. 没电的去充电 ======================
        for (int i = 0; i < NR; i++) {
            if (RST[i] != "IDLE") continue;
            if (RB[i] >= PRM[2]) continue;
            vector<vector<int> > d = BFS(RX[i], RY[i]);
            vector<pair<int, int> > cs = ALL_CHG();
            int best = BIGNUM;
            int cx = -1, cy = -1;
            for (int k = 0; k < (int)cs.size(); k++) {
                int x = cs[k].first, y = cs[k].second;
                if (!OK(x, y)) continue;
                bool used = false;
                for (int j = 0; j < NR; j++) {
                    if (j == i) continue;
                    if (RST[j] == "CHARGING" && RX[j] == x && RY[j] == y) used = true;
                    if (RST[j] == "TO_CHARGER" && RTX[j] == x && RTY[j] == y) used = true;
                }
                if (used) continue;
                if (d[y][x] + PRM[3] > RB[i]) continue;
                if (d[y][x] < best) {
                    best = d[y][x];
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
            RST[i] = "TO_CHARGER";
            RTX[i] = cx;
            RTY[i] = cy;
            RWS[i] = 0;
            writeLog(t, "GO_CHARGE " + RNAME(i) + " " + I2S(cx) + " " + I2S(cy));
        }

        // ====================== 6. 走 ======================
        for (int i = 0; i < NR; i++) {
            if (RST[i] != "TO_PICKUP" && RST[i] != "DELIVERING" && RST[i] != "TO_CHARGER") continue;
            if (RX[i] == RTX[i] && RY[i] == RTY[i]) {
                // 已经在目标上了
                RWS[i] = 0;
                if (RST[i] == "TO_PICKUP") {
                    string oid = ROID[i];
                    ORD[oid][6] = "PICKED";
                    RST[i] = "DELIVERING";
                    RTX[i] = S2I(ORD[oid][2]);
                    RTY[i] = S2I(ORD[oid][3]);
                    writeLog(t, "PICK " + oid + " " + RNAME(i));
                } else if (RST[i] == "DELIVERING") {
                    string oid = ROID[i];
                    ORD[oid][6] = "DONE";
                    int lat = t - S2I(ORD[oid][5]);
                    writeLog(t, "DELIVER " + oid + " " + RNAME(i) + " " + I2S(lat));
                    RST[i] = "IDLE";
                    ROID[i] = "";
                    RTX[i] = -1;
                    RTY[i] = -1;
                } else if (RST[i] == "TO_CHARGER") {
                    RST[i] = "CHARGING";
                    writeLog(t, "CHARGE " + RNAME(i));
                }
                continue;
            }
            if (RB[i] == 0) {
                // 没电了，趴窝
                writeLog(t, "DEAD " + RNAME(i));
                if (ROID[i] != "") {
                    string oid = ROID[i];
                    if (RST[i] == "TO_PICKUP") {
                        ORD[oid][6] = "PENDING";
                        ORD[oid][7] = "";
                        PEND.push_back(oid);
                        writeLog(t, "REQUEUE " + oid);
                    } else if (RST[i] == "DELIVERING") {
                        ORD[oid][6] = "LOST";
                        writeLog(t, "LOST " + oid);
                    }
                }
                RST[i] = "DEAD";
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
            vector<vector<int> > d = BFS2(RTX[i], RTY[i]);
            int best = BIGNUM, bx = -1, by = -1;
            for (int k = 0; k < 4; k++) {
                int nx = RX[i] + DX4[k], ny = RY[i] + DY4[k];
                if (!OK(nx, ny)) continue;
                if (d[ny][nx] < best) {
                    best = d[ny][nx];
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
                if (RST[i] == "TO_PICKUP") {
                    string oid = ROID[i];
                    ORD[oid][6] = "PICKED";
                    RST[i] = "DELIVERING";
                    RTX[i] = S2I(ORD[oid][2]);
                    RTY[i] = S2I(ORD[oid][3]);
                    writeLog(t, "PICK " + oid + " " + RNAME(i));
                } else if (RST[i] == "DELIVERING") {
                    string oid = ROID[i];
                    ORD[oid][6] = "DONE";
                    int lat = t - S2I(ORD[oid][5]);
                    writeLog(t, "DELIVER " + oid + " " + RNAME(i) + " " + I2S(lat));
                    RST[i] = "IDLE";
                    ROID[i] = "";
                    RTX[i] = -1;
                    RTY[i] = -1;
                } else if (RST[i] == "TO_CHARGER") {
                    RST[i] = "CHARGING";
                    writeLog(t, "CHARGE " + RNAME(i));
                }
            }
        }

        // ====================== 7. 报表 ======================
        if ((t + 1) % PRM[4] == 0) {
            int c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0, c5 = 0;
            for (int i = 0; i < NR; i++) {
                if (RST[i] == "IDLE") c0++;
                if (RST[i] == "TO_PICKUP") c1++;
                if (RST[i] == "DELIVERING") c2++;
                if (RST[i] == "TO_CHARGER") c3++;
                if (RST[i] == "CHARGING") c4++;
                if (RST[i] == "DEAD") c5++;
            }
            // 热点（李: 运营要看哪里堵）
            int hx = -1, hy = -1, hc = -1;
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    if (MAP[y][x] == '#') continue;
                    int n = 0;
                    for (int i = 0; i < NR; i++) {
                        if (RST[i] == "DEAD") continue;
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
    for (map<string, vector<string> >::iterator it = ORD.begin(); it != ORD.end(); ++it) {
        string st = it->second[6];
        if (st == "PENDING" || st == "ASSIGNED" || st == "PICKED") open++;
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
