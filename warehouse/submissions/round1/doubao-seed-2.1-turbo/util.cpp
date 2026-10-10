// util.cpp  工具函数（优化版）
#include "common.h"

int DX4[4] = {0, 1, 0, -1};
int DY4[4] = {-1, 0, 1, 0};   // 上 右 下 左  !!!顺序不能改!!!

// BFS 全局缓冲区（定义在 main.cpp）

const char* orderStatusStr(int s) {
    switch(s) {
        case O_PENDING:   return "PENDING";
        case O_ASSIGNED:  return "ASSIGNED";
        case O_PICKED:    return "PICKED";
        case O_DONE:      return "DONE";
        case O_CANCELLED: return "CANCELLED";
        case O_LOST:      return "LOST";
    }
    return "???";
}

const char* robotStatusStr(int s) {
    switch(s) {
        case S_IDLE:       return "IDLE";
        case S_TO_PICKUP:  return "TO_PICKUP";
        case S_DELIVERING: return "DELIVERING";
        case S_TO_CHARGER: return "TO_CHARGER";
        case S_CHARGING:   return "CHARGING";
        case S_DEAD:       return "DEAD";
    }
    return "???";
}

vector<string> SPLIT(const string& s) {
    vector<string> res;
    string cur;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
            if (!cur.empty()) {
                res.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) res.push_back(cur);
    return res;
}

int S2I(const string& s) {
    return atoi(s.c_str());
}

// 手写 I2S，比 stringstream 快得多
string I2S(long long v) {
    if (v == 0) return "0";
    bool neg = false;
    if (v < 0) { neg = true; v = -v; }
    char buf[32];
    int n = 0;
    while (v > 0) {
        buf[n++] = '0' + (v % 10);
        v /= 10;
    }
    if (neg) buf[n++] = '-';
    string s;
    s.resize(n);
    for (int i = 0; i < n; i++) s[i] = buf[n - 1 - i];
    return s;
}

const string& RNAME(int i) {
    return RNAME_CACHE[i];
}

// 格子上有没有别的车（死了的不算）
bool OCC(int x, int y, int except) {
    for (int i = 0; i < NR; i++) {
        if (i == except) continue;
        if (RST[i] == S_DEAD) continue;
        if (RX[i] == x && RY[i] == y) return true;
    }
    return false;
}

// BFS 距离场：从 (sx,sy) 出发，写入全局 BFS_DIST
// 用 generation counter 避免 memset
void BFS_FILL(int sx, int sy) {
    BFS_GEN++;
    int WH = W * H;
    if ((int)BFS_DIST.size() < WH) {
        BFS_DIST.resize(WH);
        BFS_VISIT.assign(WH, 0);
    }
    int* dist = BFS_DIST.data();
    int* visit = BFS_VISIT.data();
    int gen = BFS_GEN;
    static vector<pair<int, int>> qv;
    qv.clear();
    int head = 0;
    dist[sy * W + sx] = 0;
    visit[sy * W + sx] = gen;
    qv.push_back({sx, sy});
    while (head < (int)qv.size()) {
        int cx = qv[head].first;
        int cy = qv[head].second;
        head++;
        int cd = dist[cy * W + cx];
        for (int k = 0; k < 4; k++) {
            int nx = cx + DX4[k];
            int ny = cy + DY4[k];
            if (!OK(nx, ny)) continue;
            int idx = ny * W + nx;
            if (visit[idx] == gen) continue;
            visit[idx] = gen;
            dist[idx] = cd + 1;
            qv.push_back({nx, ny});
        }
    }
}

// BFS2：和 BFS 等价，但保持函数名/语义（从目标往回搜）
void BFS_FILL_FROM_TARGET(int tx, int ty) {
    BFS_FILL(tx, ty);
}

// a 到 b 的距离
int DIST(int ax, int ay, int bx, int by) {
    if (ax == bx && ay == by) return 0;
    if (!OK(bx, by)) return BIGNUM;
    BFS_FILL(ax, ay);
    return _bfs_get(bx, by);
}

vector<pair<int, int>> ALL_CHG() {
    vector<pair<int, int>> v;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (MAP[y][x] == 'C') v.push_back(make_pair(x, y));
        }
    }
    return v;
}

// 到最近充电桩的距离
int CHG_DIST(int x, int y) {
    BFS_FILL(x, y);
    int best = BIGNUM;
    for (size_t i = 0; i < CHG_LIST.size(); i++) {
        int cx = CHG_LIST[i].first, cy = CHG_LIST[i].second;
        if (!OK(cx, cy)) continue;
        int d = _bfs_get(cx, cy);
        if (d < best) best = d;
    }
    return best;
}

// 最近的充电桩，没有就 (-1,-1)
pair<int, int> CHG_NEAR(int x, int y) {
    BFS_FILL(x, y);
    int best = BIGNUM;
    pair<int, int> res = make_pair(-1, -1);
    for (size_t i = 0; i < CHG_LIST.size(); i++) {
        int cx = CHG_LIST[i].first, cy = CHG_LIST[i].second;
        if (!OK(cx, cy)) continue;
        int d = _bfs_get(cx, cy);
        if (d < best) {
            best = d;
            res = make_pair(cx, cy);
        }
    }
    return res;
}

int findRobot(const string& name) {
    // name 形如 "R0", "R1"...
    if (name.size() < 2 || name[0] != 'R') return -1;
    int v = atoi(name.c_str() + 1);
    if (v < 0 || v >= NR) return -1;
    return v;
}

// 写日志
void writeLog(int t, const string& msg) {
    string line = I2S(t) + " " + msg;
    LOGBUF.push_back(line);
    // 顺便统计一下
    size_t sp = msg.find(' ');
    string kw = (sp == string::npos) ? msg : msg.substr(0, sp);
    if (kw == "DELIVER") {
        size_t lastsp = msg.rfind(' ');
        long long lat = S2I(msg.substr(lastsp + 1));
        CNT[0]++;
        CNT[4] += lat;
        if (lat > CNT[5]) CNT[5] = lat;
    } else if (kw == "LOST") {
        CNT[1]++;
    } else if (kw == "REJECT") {
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
