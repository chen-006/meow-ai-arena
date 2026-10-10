// sim.cpp   仿真主体
// 2022 老王 初版
// 2023 张   改派单、加救援
// 2024 李   加了报表、改充电（按运营要求）
// 2026 主程 性能重构：事件预解析分桶、状态枚举化、距离缓存、报表热点用差分数组。
//           所有分支顺序与判定条件都刻意保持与旧版一致 —— 改逻辑前请先读 HANDOFF.md。
#include "common.h"

extern int DX4[4];
extern int DY4[4];

// ===========================================================================
// 读输入
// ===========================================================================
// 与旧版一致：头部用 >> 读，事件行用 getline 读（末位 \r 去掉）。
// 区别只有一个：事件在这里就切好词、解析成整数，并按 tick 分桶（稳定计数排序）。
// 旧版是主循环每个 tick 把所有事件行重新 SPLIT 一遍，O(T*E) 次字符串切分。
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
        RST.push_back(ST_IDLE);
        ROID.push_back(-1);
    }

    // 地图平表 + 充电桩表
    WALL.assign((size_t)W * H, 0);
    BLK.assign((size_t)W * H, 0);
    LIVE.assign((size_t)W * H, -1);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (x < (int)MAP[y].size() && MAP[y][x] == '#') WALL[y * W + x] = 1;
        }
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (x < (int)MAP[y].size() && MAP[y][x] == 'C') CHGS.push_back(make_pair(x, y));
        }
    }
    bfsBuildRank();
    for (int i = 0; i < NR; i++) LIVE[RY[i] * W + RX[i]] = i;

    int ne = 0;
    in >> tmp >> ne;   // "EVENTS"
    string line;
    getline(in, line);

    vector<string> tok;
    vector<Event> evs;
    vector<int> tickOf;
    evs.reserve(ne > 0 ? ne : 0);
    tickOf.reserve(ne > 0 ? ne : 0);
    for (int i = 0; i < ne; i++) {
        if (!getline(in, line)) line.clear();
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        SPLIT_INTO(line, tok);
        if (tok.size() < 2) continue;
        int tk = atoi(tok[0].c_str());
        if (tk < 0 || tk >= T) continue;   // 旧版也只在 t in [0,T) 时才处理
        const string& ty = tok[1];
        Event ev;
        ev.a = ev.b = ev.c = ev.d = ev.e = 0;
        if (ty == "ORDER") {
            ev.type = EV_ORDER;
            if (tok.size() > 2) ev.id = tok[2];
            if (tok.size() > 3) ev.a = atoi(tok[3].c_str());
            if (tok.size() > 4) ev.b = atoi(tok[4].c_str());
            if (tok.size() > 5) ev.c = atoi(tok[5].c_str());
            if (tok.size() > 6) ev.d = atoi(tok[6].c_str());
            if (tok.size() > 7) ev.e = atoi(tok[7].c_str());
        } else if (ty == "CANCEL") {
            ev.type = EV_CANCEL;
            if (tok.size() > 2) ev.id = tok[2];
        } else if (ty == "BLOCK") {
            ev.type = EV_BLOCK;
            if (tok.size() > 2) ev.a = atoi(tok[2].c_str());
            if (tok.size() > 3) ev.b = atoi(tok[3].c_str());
        } else if (ty == "UNBLOCK") {
            ev.type = EV_UNBLOCK;
            if (tok.size() > 2) ev.a = atoi(tok[2].c_str());
            if (tok.size() > 3) ev.b = atoi(tok[3].c_str());
        } else {
            continue;   // 不认识的事件，忽略
        }
        evs.push_back(ev);
        tickOf.push_back(tk);
    }

    // 按 tick 稳定分桶（旧版是每 tick 按文件顺序扫一遍，这里结果一样）
    int n = (int)evs.size();
    vector<int> cnt(T + 1, 0);
    for (int k = 0; k < n; k++) cnt[tickOf[k] + 1]++;
    for (int t = 0; t < T; t++) cnt[t + 1] += cnt[t];
    g_evBegin.assign(T + 1, 0);
    for (int t = 0; t <= T; t++) g_evBegin[t] = cnt[t];
    g_events.resize(n);
    vector<int> cursor(cnt.begin(), cnt.end() - 1);
    for (int k = 0; k < n; k++) {
        int t = tickOf[k];
        g_events[cursor[t]++] = evs[k];
    }
}

// 到位处理（旧版在"已经在目标上"和"刚走到目标"两处各写了一遍，内容完全相同）
static void onArrive(int t, int i) {
    RWS[i] = 0;
    if (RST[i] == ST_TO_PICKUP) {
        int oid = ROID[i];
        ORDS[oid].status = OS_PICKED;
        RST[i] = ST_DELIVERING;
        RTX[i] = ORDS[oid].dx;
        RTY[i] = ORDS[oid].dy;
        writeLog(t, "PICK " + ORDS[oid].id + " " + RNAME(i));
    } else if (RST[i] == ST_DELIVERING) {
        int oid = ROID[i];
        ORDS[oid].status = OS_DONE;
        int lat = t - ORDS[oid].arrival;
        writeLog(t, "DELIVER " + ORDS[oid].id + " " + RNAME(i) + " " + I2S(lat));
        RST[i] = ST_IDLE;
        ROID[i] = -1;
        RTX[i] = -1;
        RTY[i] = -1;
    } else if (RST[i] == ST_TO_CHARGER) {
        RST[i] = ST_CHARGING;
        writeLog(t, "CHARGE " + RNAME(i));
    }
}

// ===========================================================================
// 主循环
// ===========================================================================
void RunSim() {
    vector<char> usedMark((size_t)W * H, 0);   // 充电桩被别的车占了没有（只在充电那一步用）
    vector<int> hdiff((size_t)H * (W + 1), 0); // 报表热点用的行差分表
    int nb[4];

    for (int t = 0; t < T; t++) {
        // ====================== 1. 事件 ======================
        for (int e = g_evBegin[t]; e < g_evBegin[t + 1]; e++) {
            const Event& ev = g_events[e];
            if (ev.type == EV_ORDER) {
                const string& id = ev.id;
                int px = ev.a, py = ev.b, dx = ev.c, dy = ev.d, pr = ev.e;
                bool good = true;
                if (!INMAP(px, py)) good = false;
                if (good && !INMAP(dx, dy)) good = false;
                if (good && ISWALL(px, py)) good = false;
                if (good && ISWALL(dx, dy)) good = false;
                if (!good) {
                    writeLog(t, "REJECT " + id);
                    continue;
                }
                Order o;
                o.id = id;
                o.idNum = atoi(id.c_str());
                o.px = px;
                o.py = py;
                o.dx = dx;
                o.dy = dy;
                o.prio = pr;
                o.arrival = t;
                o.status = OS_PENDING;
                o.robot = -1;
                o.memoGen = -1;
                o.bMemo = o.cMemo = BIGNUM;
                int oi;
                unordered_map<string, int>::iterator it = ORDIDX.find(id);
                if (it == ORDIDX.end()) {
                    oi = (int)ORDS.size();
                    ORDIDX[id] = oi;
                    ORDS.push_back(o);
                } else {
                    // 订单号重复（合法输入里不会出现）：旧版是直接覆盖 ORD[id]
                    oi = it->second;
                    ORDS[oi] = o;
                }
                PEND.insert(oi);
            } else if (ev.type == EV_CANCEL) {
                const string& id = ev.id;
                unordered_map<string, int>::iterator it = ORDIDX.find(id);
                if (it == ORDIDX.end()) {
                    writeLog(t, "CANCEL_FAIL " + id);
                    continue;
                }
                int oi = it->second;
                CNT[3]++;
                if (ORDS[oi].status == OS_PENDING) {
                    PEND.erase(oi);
                    ORDS[oi].status = OS_CANCELLED;
                    ORDS[oi].robot = -1;
                    writeLog(t, "CANCEL " + id);
                } else if (ORDS[oi].status == OS_ASSIGNED) {
                    int ri = ORDS[oi].robot;
                    RST[ri] = ST_IDLE;
                    ROID[ri] = -1;
                    RTX[ri] = -1;
                    RTY[ri] = -1;
                    RWS[ri] = 0;
                    ORDS[oi].status = OS_CANCELLED;
                    ORDS[oi].robot = -1;
                    writeLog(t, "CANCEL " + id);
                } else {
                    // 已经取货了 / 已经送完了，取消不了
                    writeLog(t, "CANCEL_FAIL " + id);
                }
            } else if (ev.type == EV_BLOCK) {
                int x = ev.a, y = ev.b;
                if (INMAP(x, y)) {
                    if (!ISWALL(x, y)) {
                        int c = y * W + x;
                        if (!BLK[c]) {
                            BLK[c] = 1;
                            g_blkGen++;   // 地图变了，订单距离缓存作废
                        }
                    }
                }
            } else if (ev.type == EV_UNBLOCK) {
                int x = ev.a, y = ev.b;
                if (INMAP(x, y)) {
                    int c = y * W + x;
                    if (BLK[c]) {
                        BLK[c] = 0;
                        g_blkGen++;
                    }
                }
            }
        }

        // ====================== 2. 救援 ======================
        // 停机 100 tick 以后派人去救，充满一半电恢复
        for (int i = 0; i < NR; i++) {
            if (RST[i] == ST_DEAD) {
                if (t - RDS[i] >= 100) {
                    if (!OCC(RX[i], RY[i], i)) {
                        RST[i] = ST_IDLE;
                        RB[i] = PRM[0] / 2;
                        RWS[i] = 0;
                        RDS[i] = -1;
                        LIVE[RY[i] * W + RX[i]] = i;
                        writeLog(t, "RESCUE " + RNAME(i));
                    }
                }
            }
        }

        // ====================== 3. 充电 ======================
        for (int i = 0; i < NR; i++) {
            if (RST[i] != ST_CHARGING) continue;
            RB[i] = RB[i] + PRM[1];
            if (RB[i] > PRM[0]) RB[i] = PRM[0];
            if (RB[i] * 10 >= PRM[0] * 9) {   // 充满了就走
                RST[i] = ST_IDLE;
                RTX[i] = -1;
                RTY[i] = -1;
                writeLog(t, "CHARGED " + RNAME(i));
            }
        }

        // ====================== 4. 派单 ======================
        // 旧版每 tick 都把整个 PEND 排序一次；现在 PEND 本身就是按同一比较器有序的
        // multiset（排序键 = 优先级降序, 到达时间升序, 订单号升序，合法输入下是全序，
        // 所以顺序与旧版 std::sort 的结果逐项相同）。
        int idleCnt = 0, maxIdleBat = -1;
        for (int i = 0; i < NR; i++) {
            if (RST[i] == ST_IDLE) {
                idleCnt++;
                if (RB[i] > maxIdleBat) maxIdleBat = RB[i];
            }
        }
        if (idleCnt > 0) {
            for (multiset<int, OrdCmp>::iterator it = PEND.begin(); it != PEND.end();) {
                int oi = *it;
                Order& o = ORDS[oi];
                int px = o.px, py = o.py, dx = o.dx, dy = o.dy;
                int who = -1, best = BIGNUM;
                int man = abs(px - dx) + abs(py - dy);
                // 下面三个都是"保守剪枝"：不满足就一定没人能接单，满足则照旧走全流程
                if (maxIdleBat >= PRM[2] && maxIdleBat >= man + PRM[3]) {
                    if (o.memoGen != g_blkGen) {
                        // 旧版是对每个候选机器人各算一遍 b、c；它们只跟订单和地图有关，
                        // 所以按 (订单, 封路版本) 缓存，值完全相同。
                        o.bMemo = DIST(px, py, dx, dy);
                        o.cMemo = (o.bMemo < BIGNUM) ? CHG_DIST(dx, dy) : BIGNUM;
                        o.memoGen = g_blkGen;
                    }
                    int b = o.bMemo, c = o.cMemo;
                    if (b < BIGNUM && c < BIGNUM) {
                        int need = b + c + PRM[3];
                        if (maxIdleBat >= need) {
                            for (int i = 0; i < NR; i++) {
                                if (RST[i] != ST_IDLE) continue;
                                if (RB[i] < PRM[2]) continue;
                                if (RB[i] < need) continue;   // a >= 0，电量不够一定不行
                                // 曼哈顿距离是真实距离的下界；比当前 best 还大就赢不了
                                // （旧版是 a <= best 才更新，所以跳过它结果不变）
                                if (abs(RX[i] - px) + abs(RY[i] - py) > best) continue;
                                int a = DIST(RX[i], RY[i], px, py);
                                if (a >= BIGNUM) continue;
                                if (RB[i] >= a + need) {
                                    if (a <= best) {   // 一样近取编号大的（与旧版一致）
                                        best = a;
                                        who = i;
                                    }
                                }
                            }
                        }
                    }
                }
                if (who < 0) {
                    ++it;
                    continue;
                }
                RST[who] = ST_TO_PICKUP;
                ROID[who] = oi;
                RTX[who] = px;
                RTY[who] = py;
                RWS[who] = 0;
                o.status = OS_ASSIGNED;
                o.robot = who;
                writeLog(t, "ASSIGN " + o.id + " " + RNAME(who) + " " + I2S(best));
                it = PEND.erase(it);
            }
        }

        // ====================== 5. 没电的去充电 ======================
        int anyLow = 0;
        for (int i = 0; i < NR; i++) {
            if (RST[i] == ST_IDLE && RB[i] < PRM[2]) { anyLow = 1; break; }
        }
        if (anyLow) {
            // 旧版对每个桩都遍历所有机器人问"有没有被占"，而且是在每个机器人身上现算的，
            // 所以同一 tick 里前面刚被派去充电的车也会算"占位"。这里同样边派边更新。
            for (int j = 0; j < NR; j++) {
                if (RST[j] == ST_CHARGING) usedMark[RY[j] * W + RX[j]] = 1;
                else if (RST[j] == ST_TO_CHARGER) usedMark[RTY[j] * W + RTX[j]] = 1;
            }
            for (int i = 0; i < NR; i++) {
                if (RST[i] != ST_IDLE) continue;
                if (RB[i] >= PRM[2]) continue;
                // limit = 电量 - 安全余量：超过这个距离的桩旧版也会 continue 掉
                pair<int, int> p = pickCharger(RX[i], RY[i], RB[i] - PRM[3], usedMark);
                int cx = p.first, cy = p.second;
                if (cx < 0) {
                    // 都被占了，那就去最近的排队
                    p = CHG_NEAR(RX[i], RY[i]);
                    cx = p.first;
                    cy = p.second;
                }
                if (cx < 0) continue;
                RST[i] = ST_TO_CHARGER;
                RTX[i] = cx;
                RTY[i] = cy;
                RWS[i] = 0;
                usedMark[cy * W + cx] = 1;   // 后面的车要看到它已经占了
                writeLog(t, "GO_CHARGE " + RNAME(i) + " " + I2S(cx) + " " + I2S(cy));
            }
            // 清掉标记（只有充电桩格子会被置位）
            for (size_t k = 0; k < CHGS.size(); k++) usedMark[CHGS[k].second * W + CHGS[k].first] = 0;
        }

        // ====================== 6. 走 ======================
        for (int i = 0; i < NR; i++) {
            if (RST[i] != ST_TO_PICKUP && RST[i] != ST_DELIVERING && RST[i] != ST_TO_CHARGER) continue;
            if (RX[i] == RTX[i] && RY[i] == RTY[i]) {
                onArrive(t, i);
                continue;
            }
            if (RB[i] == 0) {
                // 没电了，趴窝
                writeLog(t, "DEAD " + RNAME(i));
                if (ROID[i] >= 0) {
                    int oid = ROID[i];
                    if (RST[i] == ST_TO_PICKUP) {
                        ORDS[oid].status = OS_PENDING;
                        ORDS[oid].robot = -1;
                        PEND.insert(oid);
                        writeLog(t, "REQUEUE " + ORDS[oid].id);
                    } else if (RST[i] == ST_DELIVERING) {
                        ORDS[oid].status = OS_LOST;
                        writeLog(t, "LOST " + ORDS[oid].id);
                    }
                }
                RST[i] = ST_DEAD;
                RDS[i] = t;
                ROID[i] = -1;
                RTX[i] = -1;
                RTY[i] = -1;
                LIVE[RY[i] * W + RX[i]] = -1;
                continue;
            }
            if (!OK(RTX[i], RTY[i])) {
                RWT[i]++;   // 目标被封了，等着
                continue;
            }
            // 旧版从目标往回 BFS 再取四个邻居里 d 最小的；只关心这 4 个点，提前退出
            bfsRobotNeighborDist(RTX[i], RTY[i], RX[i], RY[i], nb);
            int best = BIGNUM, bx = -1, by = -1;
            for (int k = 0; k < 4; k++) {
                if (nb[k] < best) {
                    best = nb[k];
                    bx = RX[i] + DX4[k];
                    by = RY[i] + DY4[k];
                }
            }
            if (best >= BIGNUM) {
                RWT[i]++;   // 走不过去
                continue;
            }
            if (OCC(bx, by, i)) {
                bool moved = false;
                if (RWS[i] >= 4 && !(bx == RTX[i] && by == RTY[i])) {
                    // 等太久了，让一让（注意：旧版这里只要求邻居可走且没人，
                    // 不要求它离目标更近，这个"怪行为"必须原样保留）
                    for (int k = 0; k < 4; k++) {
                        int nx = RX[i] + DX4[k], ny = RY[i] + DY4[k];
                        if (OK(nx, ny) && !OCC(nx, ny, i)) {
                            LIVE[RY[i] * W + RX[i]] = -1;
                            RX[i] = nx;
                            RY[i] = ny;
                            LIVE[ny * W + nx] = i;
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
                LIVE[RY[i] * W + RX[i]] = -1;
                RX[i] = bx;
                RY[i] = by;
                LIVE[by * W + bx] = i;
                RB[i]--;
                RTR[i]++;
                RWS[i] = 0;
            }
            // 走完看看到没到
            if (RX[i] == RTX[i] && RY[i] == RTY[i]) {
                onArrive(t, i);
            }
        }

        // ====================== 7. 报表 ======================
        if ((t + 1) % PRM[4] == 0) {
            int c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0, c5 = 0;
            for (int i = 0; i < NR; i++) {
                if (RST[i] == ST_IDLE) c0++;
                if (RST[i] == ST_TO_PICKUP) c1++;
                if (RST[i] == ST_DELIVERING) c2++;
                if (RST[i] == ST_TO_CHARGER) c3++;
                if (RST[i] == ST_CHARGING) c4++;
                if (RST[i] == ST_DEAD) c5++;
            }
            // 热点（李: 运营要看哪里堵）
            // 旧版对每个格子数一遍所有机器人（H*W*NR）；现在对每个机器人把它的
            // 曼哈顿菱形用一维差分加到每一行上，再逐行前缀和。判定与并列规则不变：
            // 从 y=0 起、每行 x 从小到大，只有严格更大才更新。
            int hx = -1, hy = -1, hc = -1;
            int R = PRM[5];
            if (R < 0) R = 0;
            for (int i = 0; i < NR; i++) {
                if (RST[i] == ST_DEAD) continue;
                int x0 = RX[i], y0 = RY[i];
                int ylo = y0 - R;
                if (ylo < 0) ylo = 0;
                int yhi = y0 + R;
                if (yhi > H - 1) yhi = H - 1;
                for (int y = ylo; y <= yhi; y++) {
                    int rem = R - abs(y0 - y);
                    int xlo = x0 - rem;
                    if (xlo < 0) xlo = 0;
                    int xhi = x0 + rem;
                    if (xhi > W - 1) xhi = W - 1;
                    if (xlo > xhi) continue;
                    int base = y * (W + 1);
                    hdiff[base + xlo]++;
                    hdiff[base + xhi + 1]--;
                }
            }
            for (int y = 0; y < H; y++) {
                int base = y * (W + 1), run = 0;
                int cell = y * W;
                for (int x = 0; x < W; x++) {
                    run += hdiff[base + x];
                    if (WALL[cell + x]) continue;
                    if (run > hc) {
                        hc = run;
                        hx = x;
                        hy = y;
                    }
                }
            }
            for (int y = 0; y < H; y++) {
                int base = y * (W + 1);
                for (int x = 0; x <= W; x++) hdiff[base + x] = 0;
            }
            string s = "REPORT pending=" + I2S((long long)PEND.size()) + " idle=" + I2S(c0) +
                       " to_pickup=" + I2S(c1) + " delivering=" + I2S(c2) + " to_charger=" + I2S(c3) +
                       " charging=" + I2S(c4) + " dead=" + I2S(c5) + " hot=" + I2S(hx) + "," + I2S(hy) +
                       "," + I2S(hc);
            writeLog(t, s);
        }
    }
}

// 收尾统计
void Finish() {
    long long open = 0;
    for (size_t i = 0; i < ORDS.size(); i++) {
        int st = ORDS[i].status;
        if (st == OS_PENDING || st == OS_ASSIGNED || st == OS_PICKED) open++;
    }
    writeRaw("SUMMARY delivered=" + I2S(CNT[0]) + " lost=" + I2S(CNT[1]) + " rejected=" + I2S(CNT[2]) +
             " cancelled=" + I2S(CNT[3]) + " open=" + I2S(open));
    long long avg = 0;
    if (CNT[0] > 0) avg = (CNT[4] + CNT[0] / 2) / CNT[0];   // 平均延迟，整数除法
    writeRaw("LATENCY sum=" + I2S(CNT[4]) + " max=" + I2S(CNT[5]) + " avg=" + I2S(avg));
    for (int i = 0; i < NR; i++) {
        writeRaw("ROBOT " + RNAME(i) + " " + I2S(RX[i]) + " " + I2S(RY[i]) + " " + I2S(RB[i]) + " " + ST_NAME[RST[i]] +
                 " " + I2S(RTR[i]) + " " + I2S(RWT[i]));
    }
}
