// 圈地对战 bot：威胁感知的圈地规划器
// 核心：候选回路（矩形/封口路径）+ 精确围地收益评估 + 对手可达性风险模型 + 切断/追击战术
#include <bits/stdc++.h>
using namespace std;

static const int DX[4] = {0, 0, -1, 1};
static const int DY[4] = {-1, 1, 0, 0};
static const char DC[4] = {'U', 'D', 'L', 'R'};
static const int INF = 1 << 28;

// ---- tuning ----
static int MARGIN = 1;         // 安全余量（回合）
static double LEN_W = 1.05;    // 权重：回合成本
static double RISK_W = 1.15;   // 权重：风险
static int AMAX = 16, BMAX = 14;
static const int PURSUIT_MAX = 6;

int W, H, MAX_TURNS, N, ME, MOVE_MS, INIT_MS;
vector<pair<int, int>> spawnPos;
vector<int> spawnDir;
vector<signed char> own, trl, prot;
int curTurn;

struct PInfo { int x, y, dir, trailLen, area, deaths; };
vector<PInfo> P;

inline int IDX(int x, int y) { return y * W + x; }
inline bool inb(int x, int y) { return x >= 0 && x < W && y >= 0 && y < H; }

// ---------------- protocol ----------------
bool readState() {
    string tok;
    if (!(cin >> tok)) return false;
    cin >> curTurn;
    P.assign(N, {});
    for (int i = 0; i < N; i++) {
        int id; char d; PInfo q;
        cin >> tok >> id >> q.x >> q.y >> d >> q.trailLen >> q.area >> q.deaths;
        q.dir = string("UDLR").find(d);
        P[id] = q;
    }
    cin >> tok;  // OWNER
    own.assign(W * H, -1);
    string row;
    for (int y = 0; y < H; y++) {
        cin >> row;
        for (int x = 0; x < W; x++) own[IDX(x, y)] = (row[x] == '.') ? -1 : (row[x] - '0');
    }
    cin >> tok;  // TRAIL
    trl.assign(W * H, -1);
    for (int y = 0; y < H; y++) {
        cin >> row;
        for (int x = 0; x < W; x++) trl[IDX(x, y)] = (row[x] == '.') ? -1 : (row[x] - '0');
    }
    cin >> tok;  // END
    return true;
}

// ---------------- BFS ----------------
void bfsBlocked(const vector<signed char>& blocked, int src,
                vector<int>& dist, vector<int>* par, vector<int>* pdir) {
    int n = W * H;
    dist.assign(n, INF);
    if (par) par->assign(n, -1);
    if (pdir) pdir->assign(n, -1);
    static vector<int> q;
    q.clear();
    dist[src] = 0;
    q.push_back(src);
    for (size_t i = 0; i < q.size(); i++) {
        int k = q[i];
        int x = k % W, y = k / W;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inb(nx, ny)) continue;
            int nk = IDX(nx, ny);
            if (blocked[nk] || dist[nk] < INF) continue;
            dist[nk] = dist[k] + 1;
            if (par) (*par)[nk] = k;
            if (pdir) (*pdir)[nk] = d;
            q.push_back(nk);
        }
    }
}

// ---------------- per-turn fields ----------------
vector<int> threat;            // 任一对手到达该格的最少步数
vector<vector<int>> oppD;      // 每个对手的到达距离
vector<int> myDist;

void computeFields() {
    int n = W * H;
    vector<signed char> blk(n);
    for (int k = 0; k < n; k++) blk[k] = (trl[k] == ME) ? 1 : 0;
    bfsBlocked(blk, IDX(P[ME].x, P[ME].y), myDist, nullptr, nullptr);
    threat.assign(n, INF);
    oppD.assign(N, {});
    for (int p = 0; p < N; p++) {
        if (p == ME) continue;
        oppD[p].assign(n, INF);
        for (int k = 0; k < n; k++) blk[k] = (trl[k] == p) ? 1 : 0;
        bfsBlocked(blk, IDX(P[p].x, P[p].y), oppD[p], nullptr, nullptr);
        for (int k = 0; k < n; k++) if (oppD[p][k] < threat[k]) threat[k] = oppD[p][k];
    }
}

// ---------------- 路径模拟 ----------------
struct PathInfo {
    vector<pair<int, int>> trailCells;  // (格, 第几步踩上)
    int captureLen = -1;
    int cutVal = 0;
    int endX = 0, endY = 0, endDir = 0;
};

bool walkPath(const vector<int>& dirs, PathInfo& info, vector<uint8_t>& used) {
    const PInfo& me = P[ME];
    used.assign(W * H, 0);
    used[IDX(me.x, me.y)] = 1;
    int x = me.x, y = me.y, dir = me.dir;
    info.trailCells.clear();
    info.captureLen = -1;
    info.cutVal = 0;
    for (int i = 0; i < (int)dirs.size(); i++) {
        int d = dirs[i];
        if (d == (dir ^ 1)) return false;
        dir = d;
        int nx = x + DX[d], ny = y + DY[d];
        if (!inb(nx, ny)) return false;
        int k = IDX(nx, ny);
        if (trl[k] == ME) return false;
        if (used[k]) return false;
        used[k] = 1;
        x = nx; y = ny;
        if (trl[k] >= 0 && trl[k] != ME) {
            info.cutVal += 20 + 3 * P[trl[k]].trailLen;
        }
        if (own[k] == ME) {
            if (me.trailLen > 0 || !info.trailCells.empty()) {
                info.captureLen = i;
                info.endX = x; info.endY = y; info.endDir = dir;
                return true;
            }
            continue;
        }
        info.trailCells.push_back({k, i});
    }
    info.endX = x; info.endY = y; info.endDir = dir;
    return true;
}

// 从路径末端补一条回到领地的最短路（不穿自己轨迹）
int closePath(vector<int>& dirs, PathInfo& info, vector<uint8_t>& used) {
    int n = W * H;
    int start = IDX(info.endX, info.endY);
    if (own[start] == ME && (P[ME].trailLen > 0 || !info.trailCells.empty())) {
        info.captureLen = (int)dirs.size() - 1;
        return info.captureLen;
    }
    vector<signed char> blk(n);
    for (int k = 0; k < n; k++) blk[k] = (trl[k] == ME || used[k]) ? 1 : 0;
    vector<int> dist, par, pdir;
    bfsBlocked(blk, start, dist, &par, &pdir);
    int goal = -1, best = INF;
    for (int k = 0; k < n; k++) {
        if (own[k] == ME && dist[k] < best && dist[k] > 0) { best = dist[k]; goal = k; }
    }
    if (goal < 0) return -1;
    vector<int> rev;
    for (int k = goal; k != start; k = par[k]) rev.push_back(pdir[k]);
    reverse(rev.begin(), rev.end());
    for (int d : rev) dirs.push_back(d);
    PathInfo info2;
    if (!walkPath(dirs, info2, used)) return -1;
    info = info2;
    return info.captureLen;
}

// ---------------- 圈地收益（模拟引擎的围地泛洪） ----------------
int computeCapture(const vector<pair<int, int>>& newTrail) {
    int n = W * H;
    static vector<uint8_t> wall, seen;
    static vector<int> st;
    wall.assign(n, 0);
    for (int k = 0; k < n; k++) wall[k] = (own[k] == ME || trl[k] == ME) ? 1 : 0;
    for (auto& pr : newTrail) wall[pr.first] = 1;
    seen.assign(n, 0);
    st.clear();
    auto push = [&](int k) {
        if (!wall[k] && !seen[k]) { seen[k] = 1; st.push_back(k); }
    };
    for (int x = 0; x < W; x++) { push(x); push((H - 1) * W + x); }
    for (int y = 0; y < H; y++) { push(y * W); push(y * W + W - 1); }
    while (!st.empty()) {
        int k = st.back(); st.pop_back();
        int x = k % W, y = k / W;
        if (x > 0) push(k - 1);
        if (x + 1 < W) push(k + 1);
        if (y > 0) push(k - W);
        if (y + 1 < H) push(k + W);
    }
    int gain = 0;
    for (int k = 0; k < n; k++) {
        if (own[k] == ME) continue;
        if (prot[k] >= 0 && prot[k] != ME) continue;
        if (wall[k] || !seen[k]) gain++;
    }
    return gain;
}

// ---------------- 风险 ----------------
// 撞头预测：stepIdx 时我从 fromK 走到 toK
int headonRisk(int fromK, int toK, int stepIdx) {
    int risk = 0;
    for (int p = 0; p < N; p++) {
        if (p == ME) continue;
        int t = oppD[p][toK];
        if (stepIdx == 0) {
            int tx = P[p].x + DX[P[p].dir], ty = P[p].y + DY[P[p].dir];
            int tgt = IDX(tx, ty);
            if (t == 1) {
                risk += (tgt == toK) ? 12 : 3;      // 对方正走向我的目标格 → 同目标撞头
            } else if (t == 0) {
                if (tgt == fromK) risk += 12;        // 互换位置
                else risk -= 6;                      // 对方离开原格（若在其领地外我反而切断它）
            }
        } else {
            if (t == stepIdx + 1) risk += 8;         // 同目标
            else if (t == stepIdx) risk += 3;        // 可能互换
        }
    }
    return risk;
}

int evalRisk(const PathInfo& info, int C, int& minTh) {
    int sum = 0, mx = 0;
    minTh = INF;
    auto consider = [&](int k) {
        int t = threat[k];
        minTh = min(minTh, t);
        // 只有真正贴近的对手才算风险：远处对手要绕路来切代价很高
        if (t <= C + MARGIN && t <= 18) {
            int v = C + MARGIN - t + 1;
            sum += v;
            mx = max(mx, v);
        }
    };
    for (int k = 0; k < W * H; k++) if (trl[k] == ME) consider(k);
    int head = IDX(P[ME].x, P[ME].y);
    int prev = head;
    for (auto& pr : info.trailCells) {
        int k = pr.first, step = pr.second;
        consider(k);
        sum += headonRisk(prev, k, step);
        prev = k;
    }
    // 最危险的格主导，格数只占少量权重（否则大回路被系统性低估）
    return (int)(mx * 2.2 + 0.08 * sum);
}

// ---------------- 候选 ----------------
struct Cand {
    vector<int> dirs;
    int gain = 0, risk = 0, len = 0, cutVal = 0, minThreat = INF;
    double score = -1e18;
};

static vector<Cand> candPool;

int maxOppArea() {
    int m = 0;
    for (int p = 0; p < N; p++) if (p != ME) m = max(m, P[p].area);
    return m;
}

double riskWeight() {
    double riskW = RISK_W;
    const PInfo& me = P[ME];
    int turnsLeft = MAX_TURNS - curTurn;
    if (N == 2) {
        int myA = me.area, oppA = P[1 - ME].area;
        if (myA + 25 < oppA) riskW *= 0.72;
        else if (myA > oppA + 70) riskW *= 1.35;
    } else {
        riskW *= 0.75;
        int lead = maxOppArea();
        if (me.area + 30 < lead) riskW *= 0.85;
        else if (me.area > lead + 80) riskW *= 1.35;
    }
    // 终局：领先保平安，落后放手一搏
    if (turnsLeft < 40) {
        bool ahead = (N == 2) ? (me.area > P[1 - ME].area) : (me.area >= maxOppArea());
        riskW *= ahead ? 1.5 : 0.85;
    }
    return riskW;
}

void genCandidates(vector<Cand>& out) {
    out.clear();
    const PInfo& me = P[ME];
    int n = W * H;
    int turnsLeft = MAX_TURNS - curTurn;
    int head = IDX(me.x, me.y);
    bool inside = (own[head] == ME);
    double riskW = riskWeight();
    auto t0 = chrono::steady_clock::now();

    struct RawCand {
        vector<int> dirs;
        double cheap;
        int len;
        bool needClose;
    };
    vector<RawCand> raw;

    auto cheapScore = [&](const PathInfo& info, int len) {
        int minx = W, maxx = 0, miny = H, maxy = 0;
        for (auto& pr : info.trailCells) {
            int k = pr.first;
            int x = k % W, y = k / W;
            minx = min(minx, x); maxx = max(maxx, x);
            miny = min(miny, y); maxy = max(maxy, y);
        }
        double area = (info.trailCells.empty() ? 0 : (maxx - minx + 1) * (maxy - miny + 1));
        return area * 0.55 - LEN_W * len + 0.6 * info.cutVal;
    };

    PathInfo tmpInfo;
    vector<uint8_t> tmpUsed;

    if (inside && me.trailLen == 0) {
        // --- 新回路：在自己领地内走到出口，出去画矩形，回来封口 ---
        vector<signed char> blk(n);
        for (int k = 0; k < n; k++) blk[k] = (own[k] == ME) ? 0 : 1;
        vector<int> dist, par, pdir;
        bfsBlocked(blk, head, dist, &par, &pdir);
        vector<array<int, 3>> exits;  // {dist, T, e}
        for (int k = 0; k < n; k++) {
            if (dist[k] == INF) continue;
            int x = k % W, y = k / W;
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d], ny = y + DY[d];
                if (!inb(nx, ny)) continue;
                if (own[IDX(nx, ny)] == ME) continue;
                exits.push_back({dist[k], k, d});
            }
        }
        sort(exits.begin(), exits.end());
        vector<uint8_t> cellTaken(n, 0);
        int perDir[4] = {0, 0, 0, 0};
        int usedExits = 0;
        for (auto& ex : exits) {
            if (usedExits >= 8) break;
            int T = ex[1], e = ex[2];
            if (cellTaken[T] || perDir[e] >= 3) continue;
            cellTaken[T] = 1; perDir[e]++; usedExits++;
            vector<int> path0;
            for (int k = T; k != head; k = par[k]) path0.push_back(pdir[k]);
            reverse(path0.begin(), path0.end());
            for (int a = 2; a <= AMAX; a++) {
                for (int b = 1; b <= BMAX; b++) {
                    for (int si = 0; si < 2; si++) {
                        int s = (e < 2) ? 2 + si : si;
                        vector<int> dirs = path0;
                        for (int i = 0; i < a; i++) dirs.push_back(e);
                        for (int i = 0; i < b; i++) dirs.push_back(s);
                        for (int i = 0; i < a; i++) dirs.push_back(e ^ 1);
                        if (!walkPath(dirs, tmpInfo, tmpUsed)) continue;
                        int len = (tmpInfo.captureLen >= 0 ? tmpInfo.captureLen + 1
                                                          : (int)dirs.size() + 6);
                        raw.push_back({move(dirs), cheapScore(tmpInfo, len), len,
                                       tmpInfo.captureLen < 0});
                    }
                }
            }
        }
    } else {
        // --- 在外带轨迹：封口回路，必要时先延伸再封 ---
        for (int d = 0; d < 4; d++) {
            if (d == (me.dir ^ 1)) continue;
            for (int x = 1; x <= 8; x++) {
                vector<int> base(x, d);
                if (!walkPath(base, tmpInfo, tmpUsed) || tmpInfo.captureLen >= 0) break;
                for (int si = 0; si < 2; si++) {
                    int s = (d < 2) ? 2 + si : si;
                    for (int y = 1; y <= 8; y++) {
                        vector<int> dirs = base;
                        for (int i = 0; i < y; i++) dirs.push_back(s);
                        if (!walkPath(dirs, tmpInfo, tmpUsed)) break;
                        int len = (tmpInfo.captureLen >= 0 ? tmpInfo.captureLen + 1
                                                          : (int)dirs.size() + 6);
                        raw.push_back({move(dirs), cheapScore(tmpInfo, len), len,
                                       tmpInfo.captureLen < 0});
                    }
                }
            }
        }
    }

    // 便宜排序，只对前 K 名做精确评估
    sort(raw.begin(), raw.end(), [](const RawCand& a, const RawCand& b) {
        return a.cheap > b.cheap;
    });
    const int TOPK = 140;
    for (int i = 0; i < (int)raw.size() && i < TOPK; i++) {
        if ((i & 15) == 15) {
            auto ms = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
            if (ms > 16.0) break;
        }
        vector<int> dirs = move(raw[i].dirs);
        PathInfo info;
        vector<uint8_t> used2;
        if (!walkPath(dirs, info, used2)) continue;
        int C;
        if (info.captureLen >= 0) {
            C = info.captureLen;
            dirs.resize(C + 1);
        } else {
            C = closePath(dirs, info, used2);
            if (C < 0) continue;
            dirs.resize(C + 1);
        }
        int len = C + 1;
        int gain = computeCapture(info.trailCells);
        if (gain <= 0 && info.cutVal == 0) continue;
        int minTh = INF;
        int risk = evalRisk(info, C, minTh);
        Cand cd;
        cd.dirs = move(dirs);
        cd.gain = gain; cd.risk = risk; cd.len = len;
        cd.cutVal = info.cutVal; cd.minThreat = minTh;
        cd.score = gain + 0.85 * info.cutVal - LEN_W * len - riskW * risk;
        if (len > turnsLeft) cd.score -= (turnsLeft < 40 ? 25.0 : 8.0) * (len - turnsLeft + 1);
        out.push_back(move(cd));
    }
}

// ---------------- 决策 ----------------
vector<int> plan;
int lastDeaths = -1;
int planRisk0 = INF;   // 计划采纳时的风险基准

int tryImmediateCut() {
    const PInfo& me = P[ME];
    int bestD = -1, bestVal = -1;
    int leadArea = maxOppArea();
    for (int d = 0; d < 4; d++) {
        if (d == (me.dir ^ 1)) continue;
        int nx = me.x + DX[d], ny = me.y + DY[d];
        if (!inb(nx, ny)) continue;
        int k = IDX(nx, ny);
        if (trl[k] == ME || trl[k] < 0) continue;
        int p = trl[k];
        // 对方头部就在该格且正走向我 → 互换同亡，不划算
        if (P[p].x == nx && P[p].y == ny) {
            int tx = P[p].x + DX[P[p].dir], ty = P[p].y + DY[P[p].dir];
            if (tx == me.x && ty == me.y) continue;
        }
        // 该格上其他人能下一回合到达 → 撞头风险
        int th = INF;
        for (int q = 0; q < N; q++) {
            if (q == ME || q == p) continue;
            th = min(th, oppD[q][k]);
        }
        if (th <= 1) continue;
        int val = 20 + 3 * P[p].trailLen;
        if (N == 4 && P[p].area >= leadArea) val = val * 3 / 2;
        if (val > bestVal) { bestVal = val; bestD = d; }
    }
    return bestD;
}

// 追击对手轨迹（含其暴露的头部），距离内且路径安全时出击
int tryCutPursuit() {
    const PInfo& me = P[ME];
    if (me.trailLen > 8) return -1;   // 自己拖着长尾时先顾自己
    int n = W * H;
    vector<signed char> blk(n);
    for (int k = 0; k < n; k++) blk[k] = (trl[k] == ME) ? 1 : 0;
    vector<int> dist, par, pdir;
    bfsBlocked(blk, IDX(me.x, me.y), dist, &par, &pdir);
    int goal = -1, best = INF, victim = -1;
    for (int k = 0; k < n; k++) {
        if (trl[k] < 0 || trl[k] == ME) continue;
        if (dist[k] <= 0 || dist[k] > PURSUIT_MAX) continue;
        int p = trl[k];
        int val = 20 + 3 * P[p].trailLen;
        if (N == 4 && P[p].area >= maxOppArea()) val = val * 3 / 2;
        // 略偏好更近的
        int key = val - 2 * dist[k];
        if (goal < 0 || key > best) { best = key; goal = k; victim = p; }
    }
    if (goal < 0) return -1;
    // 还原路径并检查安全性
    vector<int> rev;
    for (int k = goal; k != IDX(me.x, me.y); k = par[k]) rev.push_back(pdir[k]);
    reverse(rev.begin(), rev.end());
    int x = me.x, y = me.y;
    for (int i = 0; i < (int)rev.size(); i++) {
        x += DX[rev[i]]; y += DY[rev[i]];
        int k = IDX(x, y);
        // 撞头/被其他人切的风险
        for (int p = 0; p < N; p++) {
            if (p == ME || p == victim) continue;
            if (oppD[p][k] <= i + 1) return -1;
        }
    }
    int d = rev[0];
    if (d == (me.dir ^ 1)) return -1;
    int nx = me.x + DX[d], ny = me.y + DY[d];
    if (!inb(nx, ny) || trl[IDX(nx, ny)] == ME) return -1;
    return d;
}

int safeFallback() {
    const PInfo& me = P[ME];
    int bestD = -1, bestScore = -INF;
    for (int d = 0; d < 4; d++) {
        if (d == (me.dir ^ 1)) continue;
        int nx = me.x + DX[d], ny = me.y + DY[d];
        if (!inb(nx, ny)) continue;
        int k = IDX(nx, ny);
        if (trl[k] == ME) continue;
        int sc = 0;
        if (own[k] == ME) sc += 20;
        if (threat[k] <= 1) sc -= 60;
        if (threat[k] <= 2) sc -= 10;
        if (trl[k] >= 0 && trl[k] != ME) sc += 40;
        if (sc > bestScore) { bestScore = sc; bestD = d; }
    }
    return bestD >= 0 ? bestD : me.dir;
}

// 撞头过滤器：即将撞头时改为更安全的合法移动
int applyCollisionFilter(int d) {
    const PInfo& me = P[ME];
    int cur = IDX(me.x, me.y);
    int nx = me.x + DX[d], ny = me.y + DY[d];
    if (!inb(nx, ny)) return d;
    int risk = headonRisk(cur, IDX(nx, ny), 0);
    if (risk < 8) return d;
    int bestD = -1, bestScore = INF;
    for (int dd = 0; dd < 4; dd++) {
        if (dd == (me.dir ^ 1)) continue;
        int tx = me.x + DX[dd], ty = me.y + DY[dd];
        if (!inb(tx, ty)) continue;
        int k = IDX(tx, ty);
        if (trl[k] == ME) continue;
        int sc = headonRisk(cur, k, 0) * 10;
        if (dd == me.dir) sc -= 3;
        if (own[k] == ME) sc -= 6;
        if (threat[k] <= 1) sc += 25;
        if (sc < bestScore) { bestScore = sc; bestD = dd; }
    }
    return bestD >= 0 ? bestD : d;
}

int decide() {
    const PInfo& me = P[ME];
    int turnsLeft = MAX_TURNS - curTurn;

    if (me.deaths != lastDeaths) {
        plan.clear();
        lastDeaths = me.deaths;
    }

    computeFields();

    int chosen = -1;

    // 1) 立即切断机会
    int cutDir = tryImmediateCut();
    if (cutDir >= 0) {
        plan.clear();
        chosen = cutDir;
    }

    // 2) 校验并执行既有计划
    if (chosen < 0 && !plan.empty()) {
        PathInfo info;
        vector<uint8_t> used;
        bool okPath = walkPath(plan, info, used) && info.captureLen == (int)plan.size() - 1;
        if (okPath) {
            int C = (int)plan.size() - 1;
            int d = plan[0];
            int nx = me.x + DX[d], ny = me.y + DY[d];
            bool legal = (d != (me.dir ^ 1)) && inb(nx, ny) && trl[IDX(nx, ny)] != ME;
            int minTh = INF;
            int risk = evalRisk(info, C, minTh);
            // 风险没有显著恶化就继续执行；恶化才重新规划
            bool safe = legal && (risk <= planRisk0 * 1.5 + 10) && risk <= 400;
            if (safe) {
                plan.erase(plan.begin());
                chosen = d;
            }
        }
        if (chosen < 0) plan.clear();
    }

    // 2b) 追击对手轨迹
    if (chosen < 0) {
        int pursuitDir = tryCutPursuit();
        if (pursuitDir >= 0) chosen = pursuitDir;
    }

    // 3) 重新规划
    if (chosen < 0) {
        candPool.clear();
        genCandidates(candPool);
        if (!candPool.empty()) {
            Cand* best = &candPool[0];
            for (auto& c : candPool) if (c.score > best->score) best = &c;
            plan = best->dirs;
            planRisk0 = best->risk;
            chosen = plan.front();
            plan.erase(plan.begin());
        }
    }

    // 4) 兜底
    if (chosen < 0) chosen = safeFallback();

    int finalD = applyCollisionFilter(chosen);
    if (finalD != chosen) plan.clear();
    return finalD;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string tok;
    cin >> tok;  // INIT
    cin >> W >> H >> MAX_TURNS >> N >> ME >> MOVE_MS >> INIT_MS;
    spawnPos.resize(N);
    spawnDir.assign(N, 0);
    for (int i = 0; i < N; i++) {
        int id, x, y;
        cin >> tok >> id >> x >> y;
        spawnPos[id] = {x, y};
    }
    prot.assign(W * H, -1);
    for (int i = 0; i < N; i++) {
        int sx = spawnPos[i].first, sy = spawnPos[i].second;
        for (int y = sy - 1; y <= sy + 1; y++)
            for (int x = sx - 1; x <= sx + 1; x++)
                if (inb(x, y)) prot[IDX(x, y)] = i;
    }
    while (readState()) {
        int d = decide();
        cout << curTurn << ' ' << DC[d] << endl;
    }
    return 0;
}
