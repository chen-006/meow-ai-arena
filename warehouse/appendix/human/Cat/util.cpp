// util.cpp  工具函数
#include "common.h"

//Cat 10/8 4:08, 为了极致性能... 还是选择了分别更改原本代码中的每一处.
// - //Cat 10/8 3:56, 简单函数, 根据BFS距离缓存表立即获取距离(目的是阻止对缓存表的长期持有), 不应以任何形式检查输入, 用以替换原本代码中所有对bfs距离表的查找
// - int bfsDistance(int sx, int sy, int dx, int dy){
// -     const vector<vector<int>>& bfs_dist_table = BFS(sx, sy);
// -     return bfs_dist_table[dy][dx];
// - }

// BFS 距离表  d[y][x]   起点不管能不能走都是 0 
//Cat 10/6 22:32, 主要优化点, 未保留原版本
//Cat 10/8 2:25, 原程序逻辑能保证输入的 sx,sy 不会在地图外
const vector<vector<int>>& BFS(int sx, int sy) {
    int index = sy*W+sx;

    auto it = bfs_cache.find(index);
    if (it != bfs_cache.end()) {
        return it->second;
    }

    if (bfs_cache.size() >= max_bfs_cache_size) {
        int oldest_index = bfs_cache_order.front();
        bfs_cache.erase(oldest_index);
        bfs_cache_order.pop();
    }

    auto [new_it, inserted] = bfs_cache.try_emplace(index, H, vector<int>(W, BIGNUM));
    vector<vector<int>>& d = new_it->second;
    bfs_cache_order.push(index);
    queue<pair<int,int>> q;

    d[sy][sx] = 0;
    q.emplace(sx, sy);

    while (!q.empty()) {
        pair<int,int> p = q.front();
        q.pop();
        
        int center_x = p.first;
        int center_y = p.second;

        for (int k = 0; k < 4; k++) {
            int nx = center_x + DX4[k];
            int ny = center_y + DY4[k];
            if (!OK(nx, ny)) continue;
            if (d[ny][nx] != BIGNUM) continue;
            d[ny][nx] = d[center_y][center_x] + 1;
            q.emplace(nx, ny);
        }
    }

    return d;
}

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

// 能不能走(在图内 不是墙 没封)
bool OK(int x, int y) {
    if (!INMAP(x, y)) return false;
    if (ISWALL(x, y)) return false;
    //Cat 10/8 1:28: 已修改
    // - 原本为:
    // - if (BLK.find(make_pair(x, y)) != BLK.end()) return false; //Cat 10/6 21:37:BLK是set类型, find O(logN) 问题不大
    //新的如下
    if (blocked_map[y*W+x]) return false;
    
    return true;
}

// 格子上有没有别的车（死了的不算）
//Cat 10/6 22:36, 当车较多时, 这里需要优化
//Cat 10/7 21:36, 优化思路, H*W的缓存, 记录机器人的位置, 在机器人移动或"DEAD"状态发生改变时更新
bool OCC(int x, int y, int except) {
    for (int i = 0; i < NR; i++) {
        if (i == except) continue;
        if (RST[i] == "DEAD") continue;
        if (RX[i] == x && RY[i] == y) return true;
    }
    return false;
}

//Cat: 原本代码为遍历整张地图找充电桩
//Cat 10/6 21:45: 已删除,原本调用的地方改成了返回同一个初始化过的数组(CHARGER_POS)
/*
vector<pair<int, int> > ALL_CHG() {
    vector<pair<int, int> > v;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (MAP[y][x] == 'C') v.push_back(make_pair(x, y));
        }
    }
    return v;
}
*/

// a 到 b 的距离
//Cat 10/6 21:39, BFS开销大
int DIST(int ax, int ay, int bx, int by) {
    if (ax == bx && ay == by) return 0;
    if (!OK(bx, by)) return BIGNUM;
    const vector<vector<int>>& d = BFS(ax, ay);
    return d[by][bx];
}

// 到最近充电桩的距离
//Cat 10/6 23:04, BFS开销大
int CHG_DIST(int x, int y) {
    const vector<vector<int>>& d = BFS(x, y);
    int best = BIGNUM;
    for (int i = 0; i < (int)CHARGERS_POS.size(); i++) {
        int x = CHARGERS_POS[i].first;
        int y = CHARGERS_POS[i].second;
        if (!OK(x, y)) continue;
        if (d[y][x] < best) best = d[y][x];
    }
    return best;
}

// 最近的充电桩，没有就 (-1,-1)
//Cat, 10/6:20:57 : BFS比较耗时
pair<int, int> CHG_NEAR(int x, int y) {
    const vector<vector<int>>& d = BFS(x, y);
    int best = BIGNUM;
    pair<int, int> res = make_pair(-1, -1);
    for (int i = 0; i < (int)CHARGERS_POS.size(); i++) {
        int x = CHARGERS_POS[i].first;
        int y = CHARGERS_POS[i].second;
        if (!OK(x, y)) continue;
        if (d[y][x] < best) {
            best = d[y][x];
            res = CHARGERS_POS[i];
        }
    }
    return res;
}

//Cat 10/6 21:42, findRobot的实现是莫名其妙的遍历, 但性能影响不大
//Cat 10/7 21:07, 可选优化, 直接对照 RNAME函数 写一个反函数, 直接根据名字得到机器人序号
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
}

void writeRaw(const string& msg) {
    LOGBUF.push_back(msg);
}

void dumpLog() {
    for (int i = 0; i < (int)LOGBUF.size(); i++) {
        cout << LOGBUF[i] << endl;
    }
}
