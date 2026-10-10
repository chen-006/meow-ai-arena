// sim.cpp   仿真主体（优化版）
// 主循环的七个阶段与基线完全一致，仅做等价提速，所有怪行为原样保留（见 HANDOFF.md）：
//  * 事件读入时按 tick 分桶，主循环只看本 tick 的事件（基线每 tick 全量扫描+重新 SPLIT）；
//  * 派单：只有 PEND 变动时才重建有序快照；b/c 只与订单有关，提出机器人循环；
//  * 报表热点：|dx|+dy|<=r 等价于旋转坐标系下的矩形，用前缀和 O(1) 查询（基线 O(W*H*NR)）。
#include "common.h"

extern int DX4[4];
extern int DY4[4];

// 派单快照：PEND 的稳定副本 + 排序键（订单号唯一 => 严格全序，任何正确排序结果都一致）
namespace {
struct PKey { int prio, arrival, idnum; const OrderInts* oi; };
vector<string> g_pids;
vector<PKey> g_pkeys;
vector<int> g_perm;
vector<int> g_hotS;      // 报表热点的工作区（旋转坐标系计数的前缀和）

void rebuildPendCache() {
    PEND_DIRTY = false;
    g_pids = PEND;
    size_t n = g_pids.size();
    g_pkeys.resize(n);
    g_perm.resize(n);
    for (size_t i = 0; i < n; i++) {
        map<string, OrderInts>::const_iterator it = OINT.find(g_pids[i]);
        const OrderInts* oi = (it != OINT.end()) ? &it->second : 0;
        g_pkeys[i].oi = oi;
        g_pkeys[i].prio = oi ? oi->prio : 0;       // 防御默认值；合法输入 OINT 必命中
        g_pkeys[i].arrival = oi ? oi->arrival : 0;
        g_pkeys[i].idnum = S2I(g_pids[i]);
        g_perm[i] = (int)i;
    }
    // 优先级高的先，然后来得早的先，然后编号数值小的先（与基线排序键逐对等价）
    sort(g_perm.begin(), g_perm.end(), [](int u, int v) {
        const PKey& A = g_pkeys[u];
        const PKey& B = g_pkeys[v];
        if (A.prio != B.prio) return A.prio > B.prio;
        if (A.arrival != B.arrival) return A.arrival < B.arrival;
        return A.idnum < B.idnum;
    });
}
}   // namespace

// 读输入（token 序列与基线逐个对应，怪异输入的行为也一致）
void ReadAll(istream& in) {
    in >> W >> H >> T;
    MAP.resize(H);
    for (int i = 0; i < H; i++) in >> MAP[i];
    string tmp;
    in >> tmp;   // "PARAMS"
    for (int i = 0; i < 6; i++) in >> PRM[i];
    in >> tmp >> NR;   // "ROBOTS"
    RX.resize(NR); RY.resize(NR); RB.resize(NR); RTX.resize(NR); RTY.resize(NR);
    RWS.resize(NR); RDS.resize(NR); RSTI.resize(NR); RWT.resize(NR); RTR.resize(NR);
    RST.resize(NR); ROID.resize(NR);
    for (int i = 0; i < NR; i++) {
        int a, b;
        in >> a >> b;
        RX[i] = a;
        RY[i] = b;
        RB[i] = PRM[0];   // 满电出发
        RTX[i] = -1;
        RTY[i] = -1;
        RWS[i] = 0;
        RDS[i] = -1;
        RWT[i] = 0;
        RTR[i] = 0;
        RSTI[i] = ST_IDLE;
        RST[i] = "IDLE";
        ROID[i] = "";
    }
    int ne = 0;
    in >> tmp >> ne;   // "EVENTS"
    string line;
    getline(in, line);
    EVT.clear();
    EVT.resize(ne > 0 ? ne : 0);
    for (int i = 0; i < ne; i++) {
        getline(in, line);
        if (!line.empty() && line[line.size() - 1] == '\r') line = line.substr(0, line.size() - 1);
        EVT[i] = line;
    }

    // ===== 预处理（不改任何行为）=====
    // 事件：切好 token，按 tick 分桶。基线是"每 tick 全量扫 EVT、逐条 SPLIT、tick 不等就跳过"，
    // 这里等价于：每条事件在 atoi(tick) 恰好等于当前 t 时、按原始相对顺序处理一次。
    EVTOK.assign(EVT.size(), vector<string>());
    EVB.assign(T > 0 ? T : 0, vector<int>());
    for (size_t i = 0; i < EVT.size(); i++) {
        EVTOK[i] = SPLIT(EVT[i]);
        const vector<string>& w = EVTOK[i];
        if (w.size() < 2) continue;          // 基线：token 不足 2 个的事件永远跳过
        int et = S2I(w[0]);
        if (et < 0 || et >= T) continue;     // 基线：这些 tick 在主循环里永远等不到
        EVB[et].push_back((int)i);
    }
    // 地图镜像 / 充电桩 / 初始占用 / 距离场缓存
    WALLG.assign((size_t)W * H, 0);
    CHGS.clear();
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            char c = MAP[y][x];
            WALLG[(size_t)y * W + x] = (c == '#');
            if (c == 'C') CHGS.push_back(make_pair(x, y));   // 行优先，与旧 ALL_CHG 扫描序一致
        }
    BLK.clear();
    BLKG.assign((size_t)W * H, 0);
    BLKVER = 0;
    occInit();
    fieldSetup();
    PEND.clear();
    PEND_DIRTY = true;
    g_pids.clear(); g_pkeys.clear(); g_perm.clear();
    ORD.clear();
    OINT.clear();
    LOGBUF.clear();
    for (int i = 0; i < 8; i++) CNT[i] = 0;
}

// 到达目标后的状态迁移（基线里这段在"已在目标上"和"走完一步后到目标"两处重复，抽出等价函数）
static void arrive(int i, int t) {
    RWS[i] = 0;
    if (RSTI[i] == ST_TO_PICKUP) {
        const string& oid = ROID[i];
        ORD[oid][6] = "PICKED";
        setRST(i, ST_DELIVERING);
        RTX[i] = S2I(ORD[oid][2]);
        RTY[i] = S2I(ORD[oid][3]);
        writeLog(t, "PICK " + oid + " " + RNAME(i));
    } else if (RSTI[i] == ST_DELIVERING) {
        const string& oid = ROID[i];
        ORD[oid][6] = "DONE";
        int lat = t - S2I(ORD[oid][5]);
        writeLog(t, "DELIVER " + oid + " " + RNAME(i) + " " + I2S(lat));
        setRST(i, ST_IDLE);
        ROID[i] = "";
        RTX[i] = -1;
        RTY[i] = -1;
    } else {   // ST_TO_CHARGER
        setRST(i, ST_CHARGING);
        writeLog(t, "CHARGE " + RNAME(i));
    }
}

// 主循环 ----------------------------------------------------------------
void RunSim() {
    for (int t = 0; t < T; t++) {
        // ====================== 1. 事件 ======================
        if (t < (int)EVB.size()) {
            const vector<int>& evs = EVB[t];
            for (size_t ei = 0; ei < evs.size(); ei++) {
                const vector<string>& w = EVTOK[evs[ei]];
                const string& typ = w[1];
                if (typ == "ORDER") {
                    if (w.size() < 8) continue;   // 防御；合法输入事件 token 齐全
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
                    OrderInts oi;
                    oi.px = px; oi.py = py; oi.dx = dx; oi.dy = dy; oi.prio = pr; oi.arrival = t;
                    OINT[id] = oi;
                    PEND.push_back(id);
                    PEND_DIRTY = true;
                } else if (typ == "CANCEL") {
                    if (w.size() < 3) continue;
                    string id = w[2];
                    map<string, vector<string> >::iterator it = ORD.find(id);
                    if (it == ORD.end()) {
                        writeLog(t, "CANCEL_FAIL " + id);
                        continue;
                    }
                    CNT[3]++;   // 基线：只要订单存在就计数，哪怕后面取消失败
                    if (it->second[6] == "PENDING") {
                        PEND.erase(remove(PEND.begin(), PEND.end(), id), PEND.end());
                        PEND_DIRTY = true;
                        it->second[6] = "CANCELLED";
                        it->second[7] = "";
                        writeLog(t, "CANCEL " + id);
                    } else if (it->second[6] == "ASSIGNED") {
                        int ri = findRobot(it->second[7]);
                        if (ri >= 0) {   // 防御：合法输入里名字一定有效（基线对 -1 下标是越界 UB）
                            setRST(ri, ST_IDLE);
                            ROID[ri] = "";
                            RTX[ri] = -1;
                            RTY[ri] = -1;
                            RWS[ri] = 0;
                        }
                        it->second[6] = "CANCELLED";
                        it->second[7] = "";
                        writeLog(t, "CANCEL " + id);
                    } else {
                        // 已经取货了 / 已经送完了，取消不了
                        writeLog(t, "CANCEL_FAIL " + id);
                    }
                } else if (typ == "BLOCK") {
                    if (w.size() < 4) continue;
                    int x = S2I(w[2]), y = S2I(w[3]);
                    if (INMAP(x, y) && !ISWALL(x, y)) blkAdd(x, y);
                } else if (typ == "UNBLOCK") {
                    if (w.size() < 4) continue;
                    int x = S2I(w[2]), y = S2I(w[3]);
                    if (INMAP(x, y) && BLK.count(make_pair(x, y))) blkRemove(x, y);
                } else {
                    // 不认识的事件，忽略
                }
            }
        }

        // ====================== 2. 救援 ======================
        // 停机 100 tick 以后派人去救，充满电恢复
        for (int i = 0; i < NR; i++) {
            if (RSTI[i] != ST_DEAD) continue;
            if (t - RDS[i] < 100) continue;
            if (OCC(RX[i], RY[i], i)) continue;
            setRST(i, ST_IDLE);
            RB[i] = PRM[0] / 2;
            RWS[i] = 0;
            RDS[i] = -1;
            writeLog(t, "RESCUE " + RNAME(i));
        }

        // ====================== 3. 充电 ======================
        for (int i = 0; i < NR; i++) {
            if (RSTI[i] != ST_CHARGING) continue;
            RB[i] = RB[i] + PRM[1];
            if (RB[i] > PRM[0]) RB[i] = PRM[0];
            if (RB[i] * 10 >= PRM[0] * 9) {   // 充满了就走
                setRST(i, ST_IDLE);
                RTX[i] = -1;
                RTY[i] = -1;
                writeLog(t, "CHARGED " + RNAME(i));
            }
        }

        // ====================== 4. 派单 ======================
        {
            if (PEND_DIRTY) rebuildPendCache();
            for (size_t jj = 0; jj < g_perm.size(); jj++) {
                const PKey& pk = g_pkeys[g_perm[jj]];
                const string& id = g_pids[g_perm[jj]];
                if (!pk.oi) continue;   // 防御；合法输入必命中
                int px = pk.oi->px, py = pk.oi->py, dx = pk.oi->dx, dy = pk.oi->dy;
                // b/c 只和订单有关，与机器人无关，提出循环（基线是惰性才算，但它们无副作用，结果等价）
                int b = DIST(px, py, dx, dy);
                int c = (b < BIGNUM) ? CHG_DIST(dx, dy) : BIGNUM;
                // pickup 的距离场缓存引用（pickup 在图外时 FIELD 返回全 BIGNUM 场）
                const vector<vector<int> >& fpk = FIELD(px, py);
                int best = BIGNUM, who = -1;
                for (int i = 0; i < NR; i++) {
                    if (RSTI[i] != ST_IDLE) continue;
                    if (RB[i] < PRM[2]) continue;
                    int a;
                    if (RX[i] == px && RY[i] == py) a = 0;
                    else if (!OK(px, py)) a = BIGNUM;   // 基线 DIST：终点不可走直接 BIGNUM
                    else if (OK(RX[i], RY[i])) a = fpk[RY[i]][RX[i]];   // 两端可走 => 对称，吃缓存
                    else a = DIST(RX[i], RY[i], px, py);   // 机器人脚下被封锁：不对称，按基线方向算
                    if (a >= BIGNUM) continue;
                    if (b >= BIGNUM || c >= BIGNUM) continue;
                    if (RB[i] < a + b + c + PRM[3]) continue;
                    if (a <= best) {   // 注意基线是 <=：并列时取编号大的（原样保留）
                        best = a;
                        who = i;
                    }
                }
                if (who < 0) continue;
                setRST(who, ST_TO_PICKUP);
                ROID[who] = id;
                RTX[who] = px;
                RTY[who] = py;
                RWS[who] = 0;
                ORD[id][6] = "ASSIGNED";
                ORD[id][7] = RNAME(who);
                PEND.erase(remove(PEND.begin(), PEND.end(), id), PEND.end());
                PEND_DIRTY = true;   // 快照已过时，下轮派单前重建
                writeLog(t, "ASSIGN " + id + " " + RNAME(who) + " " + I2S(best));
            }
        }

        // ====================== 5. 没电的去充电 ======================
        for (int i = 0; i < NR; i++) {
            if (RSTI[i] != ST_IDLE) continue;
            if (RB[i] >= PRM[2]) continue;
            const vector<vector<int> >& d = FIELD(RX[i], RY[i]);
            int best = BIGNUM;
            int cx = -1, cy = -1;
            for (size_t k = 0; k < CHGS.size(); k++) {
                int x = CHGS[k].first, y = CHGS[k].second;
                if (!OK(x, y)) continue;
                bool used = false;
                for (int j = 0; j < NR; j++) {
                    if (j == i) continue;
                    if (RSTI[j] == ST_CHARGING && RX[j] == x && RY[j] == y) { used = true; break; }
                    if (RSTI[j] == ST_TO_CHARGER && RTX[j] == x && RTY[j] == y) { used = true; break; }
                }
                if (used) continue;
                int dv = d[y][x];
                if (dv + PRM[3] > RB[i]) continue;
                if (dv < best) {
                    best = dv;
                    cx = x;
                    cy = y;
                }
            }
            if (cx < 0) {
                // 都被占了，那就去最近的排队
                pair<int, int> p = CHG_NEAR(RX[i], RY[i]);   // 同一缓存场，不重算 BFS
                cx = p.first;
                cy = p.second;
            }
            if (cx < 0) continue;
            setRST(i, ST_TO_CHARGER);
            RTX[i] = cx;
            RTY[i] = cy;
            RWS[i] = 0;
            writeLog(t, "GO_CHARGE " + RNAME(i) + " " + I2S(cx) + " " + I2S(cy));
        }

        // ====================== 6. 走 ======================
        for (int i = 0; i < NR; i++) {
            int st = RSTI[i];
            if (st != ST_TO_PICKUP && st != ST_DELIVERING && st != ST_TO_CHARGER) continue;
            if (RX[i] == RTX[i] && RY[i] == RTY[i]) {
                arrive(i, t);   // 已经在目标上了
                continue;
            }
            if (RB[i] == 0) {
                // 没电了，趴窝
                writeLog(t, "DEAD " + RNAME(i));
                if (!ROID[i].empty()) {
                    const string oid = ROID[i];
                    if (st == ST_TO_PICKUP) {
                        ORD[oid][6] = "PENDING";
                        ORD[oid][7] = "";
                        PEND.push_back(oid);
                        PEND_DIRTY = true;
                        writeLog(t, "REQUEUE " + oid);
                    } else if (st == ST_DELIVERING) {
                        ORD[oid][6] = "LOST";
                        writeLog(t, "LOST " + oid);
                    }
                }
                setRST(i, ST_DEAD);
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
            const vector<vector<int> >& d = FIELD(RTX[i], RTY[i]);
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
                            occMove(i, nx, ny);
                            RB[i]--;
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
                occMove(i, bx, by);
                RB[i]--;
                RTR[i]++;
                RWS[i] = 0;
            }
            // 走完看看到没到
            if (RX[i] == RTX[i] && RY[i] == RTY[i]) arrive(i, t);
        }

        // ====================== 7. 报表 ======================
        if ((t + 1) % PRM[4] == 0) {
            int c0 = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0, c5 = 0;
            for (int i = 0; i < NR; i++) {
                switch (RSTI[i]) {
                    case ST_IDLE: c0++; break;
                    case ST_TO_PICKUP: c1++; break;
                    case ST_DELIVERING: c2++; break;
                    case ST_TO_CHARGER: c3++; break;
                    case ST_CHARGING: c4++; break;
                    default: c5++; break;
                }
            }
            // 热点（李: 运营要看哪里堵）
            // 基线：对每个非墙格数一遍 |RX-x|+|RY-y| <= PRM[5] 的活车，O(W*H*NR)。
            // 等价加速：|dx|+|dy|<=r  <=>  |du|<=r 且 |dv|<=r，其中 du=x+y、dv=x-y（旋转 45° 后是矩形），
            // 先在 (du,dv) 平面给每台活车 +1，做二维前缀和，再对每个非墙格 O(1) 查矩形和。
            // 平局规则不变：行优先扫描里取第一个严格更大的（n > hc）。
            int r = PRM[5];
            int A = W + H - 1;   // du ∈ [0, W+H-2]；dv+offset 后同范围
            g_hotS.assign((size_t)A * A, 0);
            for (int i = 0; i < NR; i++) {
                if (RSTI[i] == ST_DEAD) continue;
                int u = RX[i] + RY[i];
                int v = RX[i] - RY[i] + (H - 1);
                g_hotS[(size_t)u * A + v]++;
            }
            for (int u = 0; u < A; u++)
                for (int v = 0; v < A; v++) {
                    long long s = g_hotS[(size_t)u * A + v];
                    if (u > 0) s += g_hotS[(size_t)(u - 1) * A + v];
                    if (v > 0) s += g_hotS[(size_t)u * A + (v - 1)];
                    if (u > 0 && v > 0) s -= g_hotS[(size_t)(u - 1) * A + (v - 1)];
                    g_hotS[(size_t)u * A + v] = (int)s;
                }
            int hx = -1, hy = -1, hc = -1;
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    if (MAP[y][x] == '#') continue;
                    int u = x + y, v = x - y + (H - 1);
                    int u1 = u - r; if (u1 < 0) u1 = 0;
                    int u2 = u + r; if (u2 > A - 1) u2 = A - 1;
                    int v1 = v - r; if (v1 < 0) v1 = 0;
                    int v2 = v + r; if (v2 > A - 1) v2 = A - 1;
                    long long n = g_hotS[(size_t)u2 * A + v2];
                    if (u1 > 0) n -= g_hotS[(size_t)(u1 - 1) * A + v2];
                    if (v1 > 0) n -= g_hotS[(size_t)u2 * A + (v1 - 1)];
                    if (u1 > 0 && v1 > 0) n += g_hotS[(size_t)(u1 - 1) * A + (v1 - 1)];
                    if (n > hc) {
                        hc = (int)n;
                        hx = x;
                        hy = y;
                    }
                }
            }
            string s = "REPORT pending=" + I2S((long long)PEND.size()) + " idle=" + I2S(c0) + " to_pickup=" + I2S(c1) +
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
