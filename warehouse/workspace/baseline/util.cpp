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
    stringstream ss;
    ss << v;
    return ss.str();
}

string RNAME(int i) {
    return "R" + I2S(i);
}

bool INMAP(int x, int y) {
    if (x < 0) return false;
    if (y < 0) return false;
    if (x >= W) return false;
    if (y >= H) return false;
    return true;
}

bool ISWALL(int x, int y) {
    return MAP[y][x] == '#';
}

// 能不能走（在图内 不是墙 没封）
bool OK(int x, int y) {
    if (!INMAP(x, y)) return false;
    if (ISWALL(x, y)) return false;
    if (BLK.find(make_pair(x, y)) != BLK.end()) return false;
    return true;
}

// 格子上有没有别的车（死了的不算）
bool OCC(int x, int y, int except) {
    for (int i = 0; i < NR; i++) {
        if (i == except) continue;
        if (RST[i] == "DEAD") continue;
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

// BFS2: 给移动用的（从目标往回搜）。张: 和 BFS 一样，但是别合并，上次合并出过事
vector<vector<int> > BFS2(int sx, int sy) {
    vector<vector<int> > d(H, vector<int>(W, BIGNUM));
    list<pair<int, int> > q;
    d[sy][sx] = 0;
    q.push_back(make_pair(sx, sy));
    while (!q.empty()) {
        int cx = q.front().first, cy = q.front().second;
        q.pop_front();
        for (int k = 0; k < 4; k++) {
            int nx = cx + DX4[k], ny = cy + DY4[k];
            if (!OK(nx, ny)) continue;
            if (d[ny][nx] < BIGNUM) continue;
            d[ny][nx] = d[cy][cx] + 1;
            q.push_back(make_pair(nx, ny));
        }
    }
    return d;
}

// a 到 b 的距离
int DIST(int ax, int ay, int bx, int by) {
    if (ax == bx && ay == by) return 0;
    if (!OK(bx, by)) return BIGNUM;
    vector<vector<int> > d = BFS(ax, ay);
    return d[by][bx];
}

vector<pair<int, int> > ALL_CHG() {
    vector<pair<int, int> > v;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (MAP[y][x] == 'C') v.push_back(make_pair(x, y));
        }
    }
    return v;
}

// 到最近充电桩的距离（2023-03 张: 从 sim.cpp 挪过来的，BFS 先内联着，回头再改）
int CHG_DIST(int x, int y) {
    vector<vector<int> > d(H, vector<int>(W, BIGNUM));
    queue<pair<int, int> > q;
    d[y][x] = 0;
    q.push(make_pair(x, y));
    while (!q.empty()) {
        pair<int, int> p = q.front();
        q.pop();
        for (int k = 0; k < 4; k++) {
            int nx = p.first + DX4[k];
            int ny = p.second + DY4[k];
            if (!INMAP(nx, ny)) continue;
            if (MAP[ny][nx] == '#') continue;
            if (BLK.count(make_pair(nx, ny))) continue;
            if (d[ny][nx] != BIGNUM) continue;
            d[ny][nx] = d[p.second][p.first] + 1;
            q.push(p);
            q.back() = make_pair(nx, ny);
        }
    }
    int best = BIGNUM;
    vector<pair<int, int> > cs = ALL_CHG();
    for (int i = 0; i < (int)cs.size(); i++) {
        if (!OK(cs[i].first, cs[i].second)) continue;
        if (d[cs[i].second][cs[i].first] < best) best = d[cs[i].second][cs[i].first];
    }
    return best;
}

// 最近的充电桩，没有就 (-1,-1)
pair<int, int> CHG_NEAR(int x, int y) {
    vector<vector<int> > d = BFS(x, y);
    int best = BIGNUM;
    pair<int, int> res = make_pair(-1, -1);
    vector<pair<int, int> > cs = ALL_CHG();
    for (int i = 0; i < (int)cs.size(); i++) {
        if (!OK(cs[i].first, cs[i].second)) continue;
        if (d[cs[i].second][cs[i].first] < best) {
            best = d[cs[i].second][cs[i].first];
            res = cs[i];
        }
    }
    return res;
}

int findRobot(const string& name) {
    for (int i = 0; i < NR; i++) {
        if (RNAME(i) == name) return i;
    }
    return -1;
}

// 写日志
void writeLog(int t, const string& msg) {
    string line = I2S(t) + " " + msg;
    LOGBUF.push_back(line);
    // 顺便统计一下，省得到处加
    vector<string> w = SPLIT(msg);
    if (w.size() > 0) {
        if (w[0] == "DELIVER") {
            long long lat = S2I(w[w.size() - 1]);
            CNT[0]++;
            CNT[4] += lat;
            if (lat > CNT[5]) CNT[5] = lat;
        } else if (w[0] == "LOST") {
            CNT[1]++;
        } else if (w[0] == "REJECT") {
            CNT[2]++;
        }
    }
#if FLAG_VERBOSE
    cerr << line << endl;
#endif
}

void writeRaw(const string& msg) {
    LOGBUF.push_back(msg);
}

void dumpLog() {
    for (int i = 0; i < (int)LOGBUF.size(); i++) {
        cout << LOGBUF[i] << endl;
    }
}
