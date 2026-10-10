// sim.cpp   仿真主体
// 2025 重构：逐字节复刻基线行为。主要优化：
//   1. 事件按 tick 分桶（基线每 tick 全表扫描 O(T*E)）
//   2. 派单的 DIST / CHG_DIST 全部走缓存的 BFS 距离场（基线每对 订单x机器人 一次全图 BFS）
//   3. 移动寻路的 BFS2 同样走距离场缓存（目标不变、BLK 不变就复用）
//   4. 热点统计用 Manhattan->Chebyshev 坐标变换 + 二维差分（基线 O(W*H*NR)）
//   5. OCC 用占用网格 O(1)
// 基线的怪行为（必须保留，已逐一核对）见 HANDOFF.md。
#include "common.h"

static const int DX4[4] = {0, 1, 0, -1};
static const int DY4[4] = {-1, 0, 1, 0};

// 读输入（格式与基线一致；check_input 保证事件格式合法）
void ReadAll(istream& in) {
    in >> W >> H >> T;
    MAP.resize(H);
    for (int i = 0; i < H; i++) in >> MAP[i];
    BLK.assign((size_t)W * H, 0);
    BLKVER = 0;
    string tmp;
    in >> tmp;   // "PARAMS"
    for (int i = 0; i < 8; i++) in >> PRM[i];
    in >> tmp >> NR;   // "ROBOTS"
    RX.resize(NR);
    RY.resize(NR);
    RB.assign(NR, PRM[0]);   // 满电出发
    RTX.assign(NR, -1);
    RTY.assign(NR, -1);
    RWS.assign(NR, 0);
    RDS.assign(NR, -1);
    RWT.assign(NR, 0);
    RTR.assign(NR, 0);
    RST.assign(NR, RS_IDLE);
    ROID.assign(NR, -1);
    for (int i = 0; i < NR; i++) in >> RX[i] >> RY[i];
    utilReset();
    for (int i = 0; i < NR; i++) occAdd(i);
    int ne = 0;
    in >> tmp >> ne;   // "EVENTS"
    EVTAT.assign(T, vector<int>());
    EVS.clear();
    EVS.reserve(ne);
    string line;
    getline(in, line);
    for (int i = 0; i < ne; i++) {
        getline(in, line);
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        // 解析事件行（check_input 保证：ORDER 8 项 / CANCEL 3 项 / BLOCK、UNBLOCK 4 项，
        // tick 非递减且 0<=t<T，无其它类型）
        Event ev;
        ev.a = ev.b = ev.c = ev.d = ev.pr = 0;
        const char* p = line.c_str();
        // 手工切分，比 stringstream 快
        char* end = 0;
        long t = strtol(p, &end, 10);
        p = end;
        while (*p == ' ') p++;
        char typ[16];
        int tl = 0;
        while (p[tl] && p[tl] != ' ') { typ[tl] = p[tl]; tl++; }
        typ[tl] = 0;
        p += tl;
        if (typ[0] == 'O') {         // ORDER
            ev.type = EV_ORDER;
            while (*p == ' ') p++;
            const char* s = p;
            while (*p && *p != ' ') p++;
            ev.id.assign(s, p - s);
            ev.a = (int)strtol(p, &end, 10); p = end;
            ev.b = (int)strtol(p, &end, 10); p = end;
            ev.c = (int)strtol(p, &end, 10); p = end;
            ev.d = (int)strtol(p, &end, 10); p = end;
            ev.pr = (int)strtol(p, &end, 10);
        } else if (typ[0] == 'C') {  // CANCEL
            ev.type = EV_CANCEL;
            while (*p == ' ') p++;
            ev.id = p;
        } else if (typ[0] == 'B') {  // BLOCK
            ev.type = EV_BLOCK;
            ev.a = (int)strtol(p, &end, 10); p = end;
            ev.b = (int)strtol(p, &end, 10);
        } else {                     // UNBLOCK
            ev.type = EV_UNBLOCK;
            ev.a = (int)strtol(p, &end, 10); p = end;
            ev.b = (int)strtol(p, &end, 10);
        }
        EVS.push_back(ev);
        if (t >= 0 && t < T) EVTAT[(int)t].push_back((int)EVS.size() - 1);
    }
}

// 主循环 ----------------------------------------------------------------
void RunSim() {
    vector<Event>& evs = EVS;
    vector<int> ids;            // 派单排序缓冲
    struct DKey { long long neff; int arr; int idn; int idx; };
    vector<DKey> keys;

    for (int t = 0; t < T; t++) {
        // ====================== 1. 事件 ======================
        vector<int>& today = EVTAT[t];
        for (int e = 0; e < (int)today.size(); e++) {
            Event& ev = evs[today[e]];
            if (ev.type == EV_ORDER) {
                bool good = true;
                if (!INMAP(ev.a, ev.b)) good = false;
                if (good && !INMAP(ev.c, ev.d)) good = false;
                if (good && ISWALL(ev.a, ev.b)) good = false;
                if (good && ISWALL(ev.c, ev.d)) good = false;
                if (!good) {
                    writeLog(t, "REJECT " + ev.id);
                    continue;
                }
                Order o;
                o.id = ev.id;
                o.idNum = S2I(ev.id);
                o.px = ev.a; o.py = ev.b; o.dx = ev.c; o.dy = ev.d;
                o.prio = ev.pr; o.arrival = t;
                o.status = OS_PENDING;
                o.robot = -1;
                o.bVer = -1; o.bVal = BIGNUM;
                ORDIDX[o.id] = (int)ORDERS.size();
                ORDERS.push_back(o);
                PEND.push_back((int)ORDERS.size() - 1);
            } else if (ev.type == EV_CANCEL) {
                unordered_map<string, int>::iterator it = ORDIDX.find(ev.id);
                if (it == ORDIDX.end()) {
                    writeLog(t, "CANCEL_FAIL " + ev.id);
                    continue;
                }
                CNT[3]++;   // 基线：只要订单存在就先计数，哪怕后面 CANCEL_FAIL
                Order& o = ORDERS[it->second];
                if (o.status == OS_PENDING) {
                    // 从待派里删掉。订单号唯一、派单排序关键字无并列，
                    // 所以 PEND 的顺序不影响任何输出，找到后 swap-pop 即可（基线是整体重建）。
                    for (int k = 0; k < (int)PEND.size(); k++) {
                        if (PEND[k] == it->second) {
                            PEND[k] = PEND.back();
                            PEND.pop_back();
                            break;
                        }
                    }
                    o.status = OS_CANCELLED;
                    o.robot = -1;
                    writeLog(t, "CANCEL " + ev.id);
                } else if (o.status == OS_ASSIGNED) {
                    int ri = o.robot;
                    RST[ri] = RS_IDLE;
                    ROID[ri] = -1;
                    RTX[ri] = -1;
                    RTY[ri] = -1;
                    RWS[ri] = 0;
                    o.status = OS_CANCELLED;
                    o.robot = -1;
                    writeLog(t, "CANCEL " + ev.id);
                } else {
                    // 已经取货了 / 已经送完了，取消不了
                    writeLog(t, "CANCEL_FAIL " + ev.id);
                }
            } else if (ev.type == EV_BLOCK) {
                int x = ev.a, y = ev.b;
                if (INMAP(x, y) && !ISWALL(x, y)) {
                    int k = y * W + x;
                    if (!BLK[k]) { BLK[k] = 1; PASS[k] = 0; BLKLOG.push_back(make_pair(k, (char)1)); BLKVER++; }
                }
            } else {   // EV_UNBLOCK
                int x = ev.a, y = ev.b;
                if (INMAP(x, y)) {
                    int k = y * W + x;
                    if (BLK[k]) { BLK[k] = 0; PASS[k] = MAP[y][x] != '#'; BLKLOG.push_back(make_pair(k, (char)0)); BLKVER++; }
                }
            }
        }

        // ====================== 2. 救援 ======================
        // 停机 100 tick 以后派人去救，恢复半电（PRM[0]/2，整数除法）
        for (int i = 0; i < NR; i++) {
            if (RST[i] == RS_DEAD) {
                if (t - RDS[i] >= 100) {
                    if (!OCC(RX[i], RY[i], i)) {
                        RST[i] = RS_IDLE;
                        RB[i] = PRM[0] / 2;
                        RWS[i] = 0;
                        RDS[i] = -1;
                        occAdd(i);
                        writeLog(t, "RESCUE " + RNAME(i));
                    }
                }
            }
        }

        // ====================== 3. 充电 ======================
        for (int i = 0; i < NR; i++) {
            if (RST[i] != RS_CHARGING) continue;
            RB[i] = RB[i] + PRM[1];
            if (RB[i] > PRM[0]) RB[i] = PRM[0];
            if (RB[i] * 10 >= PRM[0] * 9) {   // 充到九成就走（基线如此）
                RST[i] = RS_IDLE;
                RTX[i] = -1;
                RTY[i] = -1;
                writeLog(t, "CHARGED " + RNAME(i));
            }
        }

        // ====================== 4. 派单 ======================
        // 先收集可接单机器人（IDLE 且电量不低于低电量阈值），一个都没有就整段跳过
        // （基线此时也不会产生任何 ASSIGN，跳过不影响输出，只是省掉白跑）。
        // 基线是对每单扫全部 NR 个机器人；这里只扫可接单的（派单过程中电量不变，
        // 被派走的机器人状态已变、自然跳过），集合等价。
        vector<int> elig;
        for (int i = 0; i < NR; i++)
            if (RST[i] == RS_IDLE && RB[i] >= PRM[2]) elig.push_back(i);
        int nEligible = (int)elig.size();
        if (!PEND.empty() && nEligible > 0) {
            {
                keys.clear();
                for (int k = 0; k < (int)PEND.size(); k++) {
                    const Order& q = ORDERS[PEND[k]];
                    long long eff = (long long)q.prio + (t - q.arrival) / PRM[6];
                    keys.push_back(DKey{-eff, q.arrival, q.idNum, PEND[k]});
                }
                sort(keys.begin(), keys.end(), [](const DKey& a, const DKey& b) {
                    if (a.neff != b.neff) return a.neff < b.neff;
                    if (a.arr != b.arr) return a.arr < b.arr;
                    return a.idn < b.idn;
                });
                ids.clear();
                for (size_t k = 0; k < keys.size(); k++) ids.push_back(keys[k].idx);
            }
            int idleLeft = nEligible;
            for (int oi = 0; oi < (int)ids.size() && idleLeft > 0; oi++) {
                Order& o = ORDERS[ids[oi]];
                int px = o.px, py = o.py, dx = o.dx, dy = o.dy;
                int best = BIGNUM, who = -1;
                bool pkOK = OK(px, py);
                for (int e2 = 0; e2 < (int)elig.size(); e2++) {
                    int i = elig[e2];
                    if (RST[i] != RS_IDLE) continue;   // 本轮已被派走
                    // a = DIST(机器人 -> 取货点)
                    int a;
                    if (RX[i] == px && RY[i] == py) {
                        a = 0;   // 基线 DIST 怪行为：同点直接 0，不看能不能走
                    } else if (!pkOK) {
                        a = BIGNUM;
                    } else {
                        a = fieldDist(fieldFor(RX[i], RY[i]), px, py);
                    }
                    if (a >= BIGNUM) continue;
                    // b = DIST(取货点 -> 送货点)，按订单缓存
                    if (o.bVer != BLKVER) {
                        o.bVal = DIST(px, py, dx, dy);
                        o.bVer = BLKVER;
                    }
                    int b = o.bVal;
                    if (b >= BIGNUM) continue;
                    // c = CHG_DIST(送货点)
                    int c = CHG_DIST(dx, dy);
                    if (c >= BIGNUM) continue;
                    if ((long long)RB[i] < (long long)a + (long long)b * PRM[7] + c + PRM[3]) continue;
                    // 基线怪行为：a <= best（不是 <），并列时取编号*大*的机器人，
                    // 与代码注释"一样近选编号小的"相反，以代码为准。
                    if (a <= best) {
                        best = a;
                        who = i;
                    }
                }
                if (who == -1) continue;
                idleLeft--;
                RST[who] = RS_TO_PICKUP;
                ROID[who] = ids[oi];
                RTX[who] = px;
                RTY[who] = py;
                RWS[who] = 0;
                o.status = OS_ASSIGNED;
                o.robot = who;
                int oidx = ids[oi];
                for (int k = 0; k < (int)PEND.size(); k++) {
                    if (PEND[k] == oidx) {
                        PEND[k] = PEND.back();
                        PEND.pop_back();
                        break;
                    }
                }
                // 注意：ids 是排序后的快照，本轮继续按快照处理，与基线一致；
                // PEND 变了但不重排（基线也是每 tick 开头才拷贝排序一次）。
                writeLog(t, "ASSIGN " + o.id + " " + RNAME(who) + " " + I2S(best));
            }
        }

        // ====================== 5. 没电的去充电 ======================
        // 基线对每个候选充电桩 O(NR) 查占用；这里先建占用计数，再在循环里增量更新。
        // 占用定义：别的机器人正在该桩 CHARGING，或 TO_CHARGER 目标是该桩。
        // （注意：本机器人是 IDLE，不会出现在占用里，所以"跳过自己"天然成立。）
        bool anyLow = false;
        for (int i = 0; i < NR; i++)
            if (RST[i] == RS_IDLE && RB[i] < PRM[2]) { anyLow = true; break; }
        if (anyLow) {
        static vector<int> chgUse;
        chgUse.assign((size_t)W * H, 0);
        for (int j = 0; j < NR; j++) {
            if (RST[j] == RS_CHARGING) chgUse[RY[j] * W + RX[j]]++;
            else if (RST[j] == RS_TO_CHARGER) chgUse[RTY[j] * W + RTX[j]]++;
        }
        vector<pair<int, int> >& cs = allChg();
        for (int i = 0; i < NR; i++) {
            if (RST[i] != RS_IDLE) continue;
            if (RB[i] >= PRM[2]) continue;
            const vector<int>& d = fieldFull(fieldFor(RX[i], RY[i]));
            int best = BIGNUM;
            int cx = -1, cy = -1;
            for (int k = 0; k < (int)cs.size(); k++) {
                int x = cs[k].first, y = cs[k].second;
                if (!OK(x, y)) continue;
                if (chgUse[y * W + x]) continue;
                int dv = d[y * W + x];
                if (dv + PRM[3] > RB[i]) continue;
                if (dv < best) {
                    best = dv;
                    cx = x;
                    cy = y;
                }
            }
            if (cx < 0) {
                // 都被占了，那就去最近的排队（基线此处不看电量也不看占用）
                pair<int, int> p = CHG_NEAR(RX[i], RY[i]);
                cx = p.first;
                cy = p.second;
            }
            if (cx < 0) continue;
            RST[i] = RS_TO_CHARGER;
            RTX[i] = cx;
            RTY[i] = cy;
            RWS[i] = 0;
            chgUse[cy * W + cx]++;
            writeLog(t, "GO_CHARGE " + RNAME(i) + " " + I2S(cx) + " " + I2S(cy));
        }
        }   // anyLow

        // ====================== 6. 走 ======================
        for (int i = 0; i < NR; i++) {
            if (RST[i] != RS_TO_PICKUP && RST[i] != RS_DELIVERING && RST[i] != RS_TO_CHARGER) continue;
            if (RX[i] == RTX[i] && RY[i] == RTY[i]) {
                // 已经在目标上了
                RWS[i] = 0;
                if (RST[i] == RS_TO_PICKUP) {
                    Order& o = ORDERS[ROID[i]];
                    o.status = OS_PICKED;
                    RST[i] = RS_DELIVERING;
                    RTX[i] = o.dx;
                    RTY[i] = o.dy;
                    writeLog(t, "PICK " + o.id + " " + RNAME(i));
                } else if (RST[i] == RS_DELIVERING) {
                    Order& o = ORDERS[ROID[i]];
                    o.status = OS_DONE;
                    int lat = t - o.arrival;
                    writeLog(t, "DELIVER " + o.id + " " + RNAME(i) + " " + I2S(lat));
                    RST[i] = RS_IDLE;
                    ROID[i] = -1;
                    RTX[i] = -1;
                    RTY[i] = -1;
                } else {   // TO_CHARGER
                    RST[i] = RS_CHARGING;
                    writeLog(t, "CHARGE " + RNAME(i));
                }
                continue;
            }
            int stepCost = (RST[i] == RS_DELIVERING) ? PRM[7] : 1;
            if (RB[i] < stepCost) {
                // 没电了，趴窝（基线：只有行进状态才会趴窝，IDLE/CHARGING 电量 0 不会死）
                writeLog(t, "DEAD " + RNAME(i));
                if (ROID[i] >= 0) {
                    Order& o = ORDERS[ROID[i]];
                    if (RST[i] == RS_TO_PICKUP) {
                        o.status = OS_PENDING;
                        o.robot = -1;
                        PEND.push_back(ROID[i]);
                        writeLog(t, "REQUEUE " + o.id);
                    } else if (RST[i] == RS_DELIVERING) {
                        o.status = OS_LOST;
                        writeLog(t, "LOST " + o.id);
                    }
                }
                occRemove(i);   // DEAD 不占格子
                RST[i] = RS_DEAD;
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
            // 基线每步从目标做一次全图 BFS2；这里走缓存（目标+BLK 不变即复用）
            int bx = -1, by = -1;
            if (!fieldBestStep(fieldFor(RTX[i], RTY[i]), RX[i], RY[i], bx, by)) {
                RWT[i]++;   // 走不过去
                continue;
            }
            if (OCC(bx, by, i)) {
                bool moved = false;
                if (RWS[i] >= 4 && !(bx == RTX[i] && by == RTY[i])) {
                    // 等太久了，让一让：第一个能走且没车的邻居，不看距离
                    for (int k = 0; k < 4; k++) {
                        int nx = RX[i] + DX4[k], ny = RY[i] + DY4[k];
                        if (OK(nx, ny) && !OCC(nx, ny, i)) {
                            occRemove(i);
                            RX[i] = nx;
                            RY[i] = ny;
                            occAdd(i);
                            RB[i] -= stepCost;
                            RTR[i]++;
                            RWS[i] = 0;
                            writeLog(t, "SIDESTEP " + RNAME(i));
                            moved = true;
                            break;
                        }
                    }
                }
                if (!moved) {
                    RWT[i]++;
                    RWS[i]++;
                    continue;
                }
            } else {
                occRemove(i);
                RX[i] = bx;
                RY[i] = by;
                occAdd(i);
                RB[i] -= stepCost;
                RTR[i]++;
                RWS[i] = 0;
            }
            // 走完看看到没到（让路也可能正好走到目标上）
            if (RX[i] == RTX[i] && RY[i] == RTY[i]) {
                RWS[i] = 0;
                if (RST[i] == RS_TO_PICKUP) {
                    Order& o = ORDERS[ROID[i]];
                    o.status = OS_PICKED;
                    RST[i] = RS_DELIVERING;
                    RTX[i] = o.dx;
                    RTY[i] = o.dy;
                    writeLog(t, "PICK " + o.id + " " + RNAME(i));
                } else if (RST[i] == RS_DELIVERING) {
                    Order& o = ORDERS[ROID[i]];
                    o.status = OS_DONE;
                    int lat = t - o.arrival;
                    writeLog(t, "DELIVER " + o.id + " " + RNAME(i) + " " + I2S(lat));
                    RST[i] = RS_IDLE;
                    ROID[i] = -1;
                    RTX[i] = -1;
                    RTY[i] = -1;
                } else {   // TO_CHARGER
                    RST[i] = RS_CHARGING;
                    writeLog(t, "CHARGE " + RNAME(i));
                }
            }
        }

        // ====================== 7. 报表 ======================
        if ((t + 1) % PRM[4] == 0) {
            int c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0, c5 = 0;
            for (int i = 0; i < NR; i++) {
                if (RST[i] == RS_IDLE) c0++;
                if (RST[i] == RS_TO_PICKUP) c1++;
                if (RST[i] == RS_DELIVERING) c2++;
                if (RST[i] == RS_TO_CHARGER) c3++;
                if (RST[i] == RS_CHARGING) c4++;
                if (RST[i] == RS_DEAD) c5++;
            }
            // 热点：每个非墙格子统计 Manhattan 距离 <= PRM[5] 内的活机器人数。
            // 基线是 O(W*H*NR) 三重循环；这里用 u=x+y, v=x-y 把菱形变成正方形
            // （|dx|+|dy| == max(|du|,|dv|)），二维差分 + 前缀和，O(NR + (W+H)^2)。
            // 并列时严格大于才更新，按 y 行优先扫描，与基线一致取第一个最大。
            int hx = -1, hy = -1, hc = -1;
            {
                int S = W + H + 1;            // u,v 取值范围都在 [-(H-1), W+H-2]，平移后够用
                int r = PRM[5];
                static vector<int> diff;
                diff.assign((size_t)(S + 2) * (S + 2), 0);   // +2：差分右边界 u1+1 可能到 S+1
                int stride = S + 2;
                int voff = H;                 // v = x - y + voff >= 1
                for (int i = 0; i < NR; i++) {
                    if (RST[i] == RS_DEAD) continue;
                    int u = RX[i] + RY[i] + 1;
                    int v = RX[i] - RY[i] + voff;
                    int u0 = u - r < 0 ? 0 : u - r, u1 = u + r > S ? S : u + r;
                    int v0 = v - r < 0 ? 0 : v - r, v1 = v + r > S ? S : v + r;
                    diff[(size_t)u0 * stride + v0]++;
                    diff[(size_t)u0 * stride + v1 + 1]--;
                    diff[(size_t)(u1 + 1) * stride + v0]--;
                    diff[(size_t)(u1 + 1) * stride + v1 + 1]++;
                }
                for (int u = 0; u <= S; u++) {
                    size_t base = (size_t)u * stride;
                    int run = 0;
                    for (int v = 0; v <= S; v++) {
                        run += diff[base + v];
                        diff[base + v] = run;
                    }
                }
                // 再按 u 方向累加
                for (int v = 0; v <= S; v++) {
                    int run = 0;
                    for (int u = 0; u <= S; u++) {
                        run += diff[(size_t)u * stride + v];
                        diff[(size_t)u * stride + v] = run;
                    }
                }
                for (int y = 0; y < H; y++) {
                    for (int x = 0; x < W; x++) {
                        if (MAP[y][x] == '#') continue;
                        int n = diff[(size_t)(x + y + 1) * stride + (x - y + voff)];
                        if (n > hc) {
                            hc = n;
                            hx = x;
                            hy = y;
                        }
                    }
                }
            }
            int aged = 0;
            for (int k = 0; k < (int)PEND.size(); k++)
                if ((t - ORDERS[PEND[k]].arrival) / PRM[6] >= 1) aged++;
            string s = "REPORT pending=" + I2S((long long)PEND.size()) + " aged=" + I2S(aged) + " idle=" + I2S(c0) + " to_pickup=" + I2S(c1) +
                       " delivering=" + I2S(c2) + " to_charger=" + I2S(c3) + " charging=" + I2S(c4) +
                       " dead=" + I2S(c5) + " hot=" + I2S(hx) + "," + I2S(hy) + "," + I2S(hc);
            writeLog(t, s);
        }
    }
}

// 收尾统计
void Finish() {
    long long open = 0;
    for (int i = 0; i < (int)ORDERS.size(); i++) {
        int st = ORDERS[i].status;
        if (st == OS_PENDING || st == OS_ASSIGNED || st == OS_PICKED) open++;
    }
    writeRaw("SUMMARY delivered=" + I2S(CNT[0]) + " lost=" + I2S(CNT[1]) + " rejected=" + I2S(CNT[2]) +
             " cancelled=" + I2S(CNT[3]) + " open=" + I2S(open));
    long long avg = 0;
    if (CNT[0] > 0) avg = (CNT[4] + CNT[0] / 2) / CNT[0];   // 平均延迟，整数除法（基线如此）
    writeRaw("LATENCY sum=" + I2S(CNT[4]) + " max=" + I2S(CNT[5]) + " avg=" + I2S(avg));
    for (int i = 0; i < NR; i++) {
        writeRaw("ROBOT " + RNAME(i) + " " + I2S(RX[i]) + " " + I2S(RY[i]) + " " + I2S(RB[i]) + " " +
                 RSTATE_NAME[RST[i]] + " " + I2S(RTR[i]) + " " + I2S(RWT[i]));
    }
}
