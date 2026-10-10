// util.cpp  工具函数
#include "common.h"

int DX4[4] = {0, 1, 0, -1};
int DY4[4] = {-1, 0, 1, 0};   // 上 右 下 左  !!!顺序不能改!!!

vector<string> SPLIT(const string& s) {
    vector<string> res;
    string cur = "";
    for (int i = 0; i < (int)s.size(); i++) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
            if (cur != "") {
                res.push_back(cur);
                cur = "";
            }
        } else {
            cur = cur + c;
        }
    }
    if (cur != "") res.push_back(cur);
    return res;
}

int S2I(const string& s) {
    return atoi(s.c_str());
}

string I2S(long long v) {
    return to_string(v);
}

static string s_rname[1024];
static bool s_rnameInit = false;

string RNAME(int i) {
    if (!s_rnameInit) {
        for (int k = 0; k < 1024; k++) s_rname[k] = "R" + to_string(k);
        s_rnameInit = true;
    }
    if ((unsigned)i < 1024) return s_rname[i];
    return "R" + to_string(i);
}

bool INMAP(int x, int y) {
    return ((unsigned)x < (unsigned)W) && ((unsigned)y < (unsigned)H);
}

bool ISWALL(int x, int y) {
    return MAP[y][x] == '#';
}

// OK(x, y) 已经在 common.h 中内联基于 g_cellOk 实现

// 格子上有没有别的车（死了的不算）
bool OCC(int x, int y, int except) {
    for (int i = 0; i < NR; i++) {
        if (i == except) continue;
        if (RSTATE[i] == RST_DEAD) continue;
        if (RX[i] == x && RY[i] == y) return true;
    }
    return false;
}

// BFS 距离表  d[y][x]   起点不管能不能走都是 0
vector<vector<int> > BFS(int sx, int sy) {
#if FLAG_FAST_BFS
    return bfs_fast_cached(sx, sy);
#endif
    vector<vector<int> > d;
    for (int y = 0; y < H; y++) {
        vector<int> row;
        for (int x = 0; x < W; x++) row.push_back(BIGNUM);
        d.push_back(row);
    }
    queue<pair<int, int> > q;
    d[sy][sx] = 0;
    q.push(make_pair(sx, sy));
    while (q.size() > 0) {
        pair<int, int> p = q.front();
        q.pop();
        for (int k = 0; k < 4; k++) {
            int nx = p.first + DX4[k];
            int ny = p.second + DY4[k];
            if (OK(nx, ny) == false) continue;
            if (d[ny][nx] != BIGNUM) continue;
            d[ny][nx] = d[p.second][p.first] + 1;
            q.push(make_pair(nx, ny));
        }
    }
    return d;
}

static unordered_map<int, vector<int> > s_distFieldCache;

void clearDistFieldCache() {
    s_distFieldCache.clear();
}

const int* getDistField(int tx, int ty) {
    int key = ty * W + tx;
    auto it = s_distFieldCache.find(key);
    if (it != s_distFieldCache.end()) {
        return it->second.data();
    }

    int total = W * H;
    vector<int>& d = s_distFieldCache[key];
    d.assign(total, BIGNUM);

    static int q[512 * 512];
    int head = 0, tail = 0;

    d[key] = 0;
    q[tail++] = key;

    while (head < tail) {
        int curr = q[head++];
        int cx = curr % W, cy = curr / W;
        int curDist = d[curr];
        for (int k = 0; k < 4; k++) {
            int nx = cx + DX4[k], ny = cy + DY4[k];
            if (!OK(nx, ny)) continue;
            int nidx = ny * W + nx;
            if (d[nidx] != BIGNUM) continue;
            d[nidx] = curDist + 1;
            q[tail++] = nidx;
        }
    }
    return d.data();
}

// BFS2: 给移动用的（从目标往回搜）
vector<vector<int> > BFS2(int sx, int sy) {
    const int* f = getDistField(sx, sy);
    vector<vector<int> > d(H, vector<int>(W));
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            d[y][x] = f[y * W + x];
        }
    }
    return d;
}

// 双向 A* 结构与静态缓冲
struct AStarNode {
    int f, g, x, y;
    bool operator>(const AStarNode& other) const {
        if (f != other.f) return f > other.f;
        return g < other.g;
    }
};

static const int MAX_CELLS = 512 * 512;
static int s_tagF[MAX_CELLS], s_distF[MAX_CELLS];
static int s_tagB[MAX_CELLS], s_distB[MAX_CELLS];
static int s_epoch = 0;

inline int h_manhattan(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}

static int s_comp[MAX_CELLS];
static bool s_compDirty = true;

void markCompDirty() {
    s_compDirty = true;
}

static int s_windowTicks[64];
static int s_windowHead = 0, s_windowTail = 0;
static bool s_isHighDynamic = false;

void updateAdaptiveMode(int t) {
    while (s_windowHead < s_windowTail && t - s_windowTicks[s_windowHead % 64] > 20) {
        s_windowHead++;
    }
    int recentChanges = s_windowTail - s_windowHead;
    if (recentChanges >= 3) {
        s_isHighDynamic = true;
    } else if (recentChanges == 0) {
        s_isHighDynamic = false;
    }
}

void onMapChanged(int t) {
    clearBfsCache();
    s_windowTicks[(s_windowTail++) % 64] = t;
    updateAdaptiveMode(t);
}

bool isHighDynamicMode() {
    return s_isHighDynamic;
}

static void updateComponents() {
    if (!s_compDirty) return;
    if (s_isHighDynamic) return;
    s_compDirty = false;

    int total = W * H;
    for (int i = 0; i < total; i++) s_comp[i] = -1;

    int cid = 0;
    static int q[MAX_CELLS];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int idx = y * W + x;
            if (s_comp[idx] == -1 && OK(x, y)) {
                int head = 0, tail = 0;
                q[tail++] = idx;
                s_comp[idx] = cid;
                while (head < tail) {
                    int curr = q[head++];
                    int cx = curr % W, cy = curr / W;
                    for (int k = 0; k < 4; k++) {
                        int nx = cx + DX4[k], ny = cy + DY4[k];
                        if (OK(nx, ny)) {
                            int nidx = ny * W + nx;
                            if (s_comp[nidx] == -1) {
                                s_comp[nidx] = cid;
                                q[tail++] = nidx;
                            }
                        }
                    }
                }
                cid++;
            }
        }
    }
}

// a 到 b 的距离：双向 A* 启发式搜索
int DIST(int ax, int ay, int bx, int by) {
    if (ax == bx && ay == by) return 0;
    if (!OK(bx, by)) return BIGNUM;

    if (!s_isHighDynamic) {
        updateComponents();
        int goalComp = s_comp[by * W + bx];
        if (goalComp == -1) return BIGNUM;

        if (OK(ax, ay)) {
            if (s_comp[ay * W + ax] != goalComp) return BIGNUM;
        } else {
            bool canReach = false;
            for (int k = 0; k < 4; k++) {
                int nx = ax + DX4[k], ny = ay + DY4[k];
                if (INMAP(nx, ny) && OK(nx, ny) && s_comp[ny * W + nx] == goalComp) {
                    canReach = true;
                    break;
                }
            }
            if (!canReach) return BIGNUM;
        }
    }

    s_epoch++;
    if (s_epoch == 0) { // 溢出重置
        memset(s_tagF, 0, sizeof(s_tagF));
        memset(s_tagB, 0, sizeof(s_tagB));
        s_epoch = 1;
    }

    priority_queue<AStarNode, vector<AStarNode>, greater<AStarNode> > pqF;
    priority_queue<AStarNode, vector<AStarNode>, greater<AStarNode> > pqB;

    int startIdx = ay * W + ax;
    int goalIdx = by * W + bx;

    s_distF[startIdx] = 0;
    s_tagF[startIdx] = s_epoch;
    pqF.push({h_manhattan(ax, ay, bx, by), 0, ax, ay});

    s_distB[goalIdx] = 0;
    s_tagB[goalIdx] = s_epoch;
    pqB.push({h_manhattan(bx, by, ax, ay), 0, bx, by});

    int bestDist = BIGNUM;

    while (!pqF.empty() && !pqB.empty()) {
        if (pqF.top().f >= bestDist && pqB.top().f >= bestDist) {
            break;
        }

        bool expandF = true;
        if (pqF.top().f > pqB.top().f) {
            expandF = false;
        } else if (pqF.top().f == pqB.top().f) {
            expandF = (pqF.size() <= pqB.size());
        }

        if (expandF) {
            AStarNode curr = pqF.top();
            pqF.pop();
            int currIdx = curr.y * W + curr.x;
            if (curr.g > s_distF[currIdx]) continue;

            for (int k = 0; k < 4; k++) {
                int nx = curr.x + DX4[k];
                int ny = curr.y + DY4[k];
                if (!OK(nx, ny)) continue;
                int nIdx = ny * W + nx;
                int ng = curr.g + 1;

                if (s_tagB[nIdx] == s_epoch) {
                    int cand = ng + s_distB[nIdx];
                    if (cand < bestDist) bestDist = cand;
                }

                if (s_tagF[nIdx] != s_epoch || ng < s_distF[nIdx]) {
                    s_distF[nIdx] = ng;
                    s_tagF[nIdx] = s_epoch;
                    pqF.push({ng + h_manhattan(nx, ny, bx, by), ng, nx, ny});
                }
            }
        } else {
            AStarNode curr = pqB.top();
            pqB.pop();
            int currIdx = curr.y * W + curr.x;
            if (curr.g > s_distB[currIdx]) continue;

            for (int k = 0; k < 4; k++) {
                int nx = curr.x + DX4[k];
                int ny = curr.y + DY4[k];
                if (nx != ax || ny != ay) {
                    if (!OK(nx, ny)) continue;
                }
                int nIdx = ny * W + nx;
                int ng = curr.g + 1;

                if (s_tagF[nIdx] == s_epoch) {
                    int cand = ng + s_distF[nIdx];
                    if (cand < bestDist) bestDist = cand;
                }

                if (s_tagB[nIdx] != s_epoch || ng < s_distB[nIdx]) {
                    s_distB[nIdx] = ng;
                    s_tagB[nIdx] = s_epoch;
                    pqB.push({ng + h_manhattan(nx, ny, ax, ay), ng, nx, ny});
                }
            }
        }
    }

    return bestDist;
}

vector<pair<int, int> > ALL_CHG() {
    static vector<pair<int, int> > s_allChg;
    if (!s_allChg.empty()) return s_allChg;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (MAP[y][x] == 'C') s_allChg.push_back(make_pair(x, y));
        }
    }
    return s_allChg;
}

// 到最近充电桩的距离：直接查表各个充电桩的距离场
int CHG_DIST(int x, int y) {
    int best = BIGNUM;
    const vector<pair<int, int> >& cs = ALL_CHG();
    for (size_t i = 0; i < cs.size(); i++) {
        if (!OK(cs[i].first, cs[i].second)) continue;
        const int* d = getDistField(cs[i].first, cs[i].second);
        int cur = BIGNUM;
        if (OK(x, y)) {
            cur = d[y * W + x];
        } else {
            for (int k = 0; k < 4; k++) {
                int nx = x + DX4[k], ny = y + DY4[k];
                if (OK(nx, ny) && d[ny * W + nx] < BIGNUM) {
                    if (d[ny * W + nx] + 1 < cur) cur = d[ny * W + nx] + 1;
                }
            }
        }
        if (cur < best) best = cur;
    }
    return best;
}

// 最近的充电桩，没有就 (-1,-1)
pair<int, int> CHG_NEAR(int x, int y) {
    int best = BIGNUM;
    pair<int, int> res = make_pair(-1, -1);
    const vector<pair<int, int> >& cs = ALL_CHG();
    for (size_t i = 0; i < cs.size(); i++) {
        if (!OK(cs[i].first, cs[i].second)) continue;
        const int* d = getDistField(cs[i].first, cs[i].second);
        int cur = BIGNUM;
        if (OK(x, y)) {
            cur = d[y * W + x];
        } else {
            for (int k = 0; k < 4; k++) {
                int nx = x + DX4[k], ny = y + DY4[k];
                if (OK(nx, ny) && d[ny * W + nx] < BIGNUM) {
                    if (d[ny * W + nx] + 1 < cur) cur = d[ny * W + nx] + 1;
                }
            }
        }
        if (cur < best) {
            best = cur;
            res = cs[i];
        }
    }
    return res;
}

int findRobot(const string& name) {
    if (name.size() > 1 && name[0] == 'R') {
        int idx = atoi(name.c_str() + 1);
        if (idx >= 0 && idx < NR) return idx;
    }
    for (int i = 0; i < NR; i++) {
        if (RNAME(i) == name) return i;
    }
    return -1;
}

// 写日志
void writeLog(int t, const string& msg) {
    string line = to_string(t) + " " + msg;
    LOGBUF.push_back(line);
    const char* s = msg.c_str();
    if (strncmp(s, "DELIVER ", 8) == 0) {
        size_t pos = msg.rfind(' ');
        long long lat = atoll(s + pos + 1);
        CNT[0]++;
        CNT[4] += lat;
        if (lat > CNT[5]) CNT[5] = lat;
    } else if (strncmp(s, "LOST ", 5) == 0 || msg == "LOST") {
        CNT[1]++;
    } else if (strncmp(s, "REJECT ", 7) == 0 || msg == "REJECT") {
        CNT[2]++;
    }
#if FLAG_VERBOSE
    cerr << line << endl;
#endif
}

void writeRaw(const string& msg) {
    LOGBUF.push_back(msg);
}

void dumpLog() {
    for (size_t i = 0; i < LOGBUF.size(); i++) {
        cout << LOGBUF[i] << '\n';
    }
}

bool OrderCmp::operator()(const string& a, const string& b) const {
    if (a == b) return false;
    auto ita = ORD.find(a);
    auto itb = ORD.find(b);
    if (ita == ORD.end() || itb == ORD.end()) return a < b;
    if (ita->second.prio != itb->second.prio) return ita->second.prio > itb->second.prio;
    if (ita->second.arrival != itb->second.arrival) return ita->second.arrival < itb->second.arrival;
    return ita->second.int_id < itb->second.int_id;
}

