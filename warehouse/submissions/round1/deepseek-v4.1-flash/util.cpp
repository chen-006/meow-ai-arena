// util.cpp  工具函数
#include "common.h"

int DX4[4] = {0, 1, 0, -1};
int DY4[4] = {-1, 0, 1, 0};   // 上 右 下 左  !!!顺序不能改!!!

// ===========================================================================
// BFS 内核
// ===========================================================================
// 一张 H*W 的距离表，用"时间戳"代替每次清零：
//   g_stamp[c] == g_stampNow  <=>  本次 BFS 访问过格子 c，此时 g_dist[c] 有效。
// 队列是平数组（两行分别存 x/y），不再用 std::list —— 旧版每个入队点都 malloc 一次。
// 观察点用 g_mark[c] == g_markNow 标记；一个格子一旦被赋值，距离就是最终值，
// 所以"所有观察点都拿到值"之后可以立刻停。
static vector<int> g_dist, g_stamp, g_mark, g_rank;
static vector<int> g_qx, g_qy;
static int g_stampNow = 0;   // 每次 BFS +1
static int g_markNow = 0;    // 每次设置观察点 +1
static int g_hit = 0;        // bfsAll 已命中的观察点数

// 时间戳/标记号会一直自增，理论上可能溢出 int；到阈值就整体重置一次（很少发生）。
static inline void stampGuard() {
    if (g_stampNow > 1000000000 || g_markNow > 1000000000) {
        fill(g_stamp.begin(), g_stamp.end(), 0);
        fill(g_mark.begin(), g_mark.end(), 0);
        g_stampNow = 0;
        g_markNow = 0;
    }
}

// 由调用方保证 g_qx/g_qy 容量 >= W*H
static inline void bfsInit() {
    size_t n = (size_t)W * H;
    if (g_dist.size() < n) {
        g_dist.resize(n);
        g_stamp.assign(n, 0);
        g_mark.assign(n, 0);
        g_rank.assign(n, 0);
        g_qx.resize(n);
        g_qy.resize(n);
    }
    stampGuard();
}

// 从 (sx,sy) 做 BFS，直到 nWatch 个观察点全部拿到距离（或搜完整个连通块）。
// 调用前请用 g_mark[cell] = g_markNow 标好观察点。
static void bfsAll(int sx, int sy, int nWatch) {
    ++g_stampNow;
    g_hit = 0;
    int s = sy * W + sx;
    g_stamp[s] = g_stampNow;
    g_dist[s] = 0;
    if (g_mark[s] == g_markNow) {
        if (++g_hit >= nWatch) return;
    }
    int head = 0, tail = 0;
    g_qx[tail] = sx;
    g_qy[tail] = sy;
    tail++;
    while (head < tail) {
        int cx = g_qx[head], cy = g_qy[head];
        head++;
        int nd = g_dist[cy * W + cx] + 1;
        for (int k = 0; k < 4; k++) {
            int nx = cx + DX4[k], ny = cy + DY4[k];
            if (!OK(nx, ny)) continue;
            int nb = ny * W + nx;
            if (g_stamp[nb] == g_stampNow) continue;
            g_stamp[nb] = g_stampNow;
            g_dist[nb] = nd;
            g_qx[tail] = nx;
            g_qy[tail] = ny;
            tail++;
            if (g_mark[nb] == g_markNow) {
                if (++g_hit >= nWatch) return;
            }
        }
    }
}

// 从 (sx,sy) 做 BFS，最多搜到距离 maxDist；返回"最近的观察点"。
// 结果放在 g_firstDist / g_firstCell。并列（同一层有多个观察点）时：
//   useRank 为真 -> 取 g_rank 小的那个（充电桩的扫描顺序）；否则取先遇到的。
static int g_firstDist, g_firstCell, g_firstRank;

static void bfsFirst(int sx, int sy, int maxDist, bool useRank) {
    ++g_stampNow;
    g_firstDist = BIGNUM;
    g_firstCell = -1;
    g_firstRank = 0;
    // 起点距离是 0，所以 maxDist < 0 时连起点都不算数（pickCharger 会传负数 limit）
    if (maxDist < 0) return;
    int s = sy * W + sx;
    g_stamp[s] = g_stampNow;
    g_dist[s] = 0;
    if (g_mark[s] == g_markNow) {
        g_firstDist = 0;
        g_firstCell = s;
        g_firstRank = useRank ? g_rank[s] : 0;
        return;
    }
    if (maxDist == 0) return;
    int head = 0, tail = 0;
    g_qx[tail] = sx;
    g_qy[tail] = sy;
    tail++;
    int level = 0;
    while (head < tail) {
        if (g_firstDist != BIGNUM) return;   // 上一层已经命中，本层也处理完了
        int levelEnd = tail;
        level++;
        if (level > maxDist) return;
        while (head < levelEnd) {
            int cx = g_qx[head], cy = g_qy[head];
            head++;
            for (int k = 0; k < 4; k++) {
                int nx = cx + DX4[k], ny = cy + DY4[k];
                if (!OK(nx, ny)) continue;
                int nb = ny * W + nx;
                if (g_stamp[nb] == g_stampNow) continue;
                g_stamp[nb] = g_stampNow;
                g_dist[nb] = level;
                g_qx[tail] = nx;
                g_qy[tail] = ny;
                tail++;
                if (g_mark[nb] == g_markNow) {
                    int rk = useRank ? g_rank[nb] : 0;
                    if (g_firstDist == BIGNUM || rk < g_firstRank) {
                        g_firstDist = level;
                        g_firstCell = nb;
                        g_firstRank = rk;
                    }
                }
            }
        }
    }
}

// 把 CHGS 里"可走"的桩标成观察点，返回个数。
static int markChargers() {
    ++g_markNow;
    int n = 0;
    for (size_t i = 0; i < CHGS.size(); i++) {
        int x = CHGS[i].first, y = CHGS[i].second;
        if (OK(x, y)) {
            g_mark[y * W + x] = g_markNow;
            n++;
        }
    }
    return n;
}

// CHGS 建好之后调用一次：把每个桩在 CHGS 里的序号记进 g_rank，供"并列取先"使用。
void bfsBuildRank() {
    bfsInit();
    for (size_t i = 0; i < CHGS.size(); i++) {
        int x = CHGS[i].first, y = CHGS[i].second;
        g_rank[y * W + x] = (int)i;
    }
}

// ===========================================================================
// 基础工具
// ===========================================================================
void SPLIT_INTO(const string& s, vector<string>& out) {
    out.clear();
    size_t i = 0, n = s.size();
    while (i < n) {
        while (i < n && ISSP(s[i])) i++;
        if (i >= n) break;
        size_t j = i;
        while (j < n && !ISSP(s[j])) j++;
        out.push_back(s.substr(i, j - i));
        i = j;
    }
}

vector<string> SPLIT(const string& s) {
    vector<string> res;
    SPLIT_INTO(s, res);
    return res;
}

int S2I(const string& s) { return atoi(s.c_str()); }

string I2S(long long v) { return to_string(v); }

string RNAME(int i) { return "R" + to_string(i); }

// 格子上有没有别的车（死了的不算）
// 旧版每次遍历所有机器人；现在 LIVE[] 直接给出该格上的活车。
// 活着的车两两不同格（移动时 OCC 会挡住），所以最多一个候选，语义完全一致。
bool OCC(int x, int y, int except) {
    if (!INMAP(x, y)) return false;
    int r = LIVE[y * W + x];
    return r >= 0 && r != except;
}

// ===========================================================================
// 距离查询（全部走 BFS 内核，只算需要的那几个点）
// ===========================================================================

// a 到 b 的距离；旧版语义：起点=终点直接 0（不看起点能不能走），终点不可走则 BIGNUM。
int DIST(int ax, int ay, int bx, int by) {
    if (ax == bx && ay == by) return 0;
    if (!OK(bx, by)) return BIGNUM;
    bfsInit();
    ++g_markNow;
    g_mark[by * W + bx] = g_markNow;
    bfsAll(ax, ay, 1);
    if (g_hit > 0) return g_dist[by * W + bx];
    return BIGNUM;
}

// 到最近的可走充电桩的距离；没有可走的桩就是 BIGNUM。
int CHG_DIST(int x, int y) {
    bfsInit();
    if (markChargers() == 0) return BIGNUM;
    bfsFirst(x, y, BIGNUM, false);
    return g_firstDist;
}

// 最近的可走充电桩坐标，并列取扫描顺序靠前的；没有就是 (-1,-1)。
pair<int, int> CHG_NEAR(int x, int y) {
    bfsInit();
    if (markChargers() == 0) return make_pair(-1, -1);
    bfsFirst(x, y, BIGNUM, true);
    if (g_firstCell < 0) return make_pair(-1, -1);
    return make_pair(g_firstCell % W, g_firstCell / W);
}

// 选空桩：可走 + 没被占 + d <= limit，并列取扫描顺序靠前的。
pair<int, int> pickCharger(int x, int y, int limit, const vector<char>& usedMark) {
    bfsInit();
    ++g_markNow;
    int n = 0;
    for (size_t i = 0; i < CHGS.size(); i++) {
        int cx = CHGS[i].first, cy = CHGS[i].second;
        if (!OK(cx, cy)) continue;
        if (usedMark[cy * W + cx]) continue;
        g_mark[cy * W + cx] = g_markNow;
        n++;
    }
    if (n == 0) return make_pair(-1, -1);
    bfsFirst(x, y, limit, true);
    if (g_firstCell < 0) return make_pair(-1, -1);
    return make_pair(g_firstCell % W, g_firstCell / W);
}

// 机器人 (rx,ry) 四个邻居到 (tx,ty) 的距离。旧版这里是 BFS2(从目标往回搜) + 取 min，
// 等价于"目标到邻居的距离"，所以直接以目标为起点搜，只盯这 4 个点。
void bfsRobotNeighborDist(int tx, int ty, int rx, int ry, int out[4]) {
    bfsInit();
    ++g_markNow;
    int nw = 0;
    for (int k = 0; k < 4; k++) {
        int nx = rx + DX4[k], ny = ry + DY4[k];
        out[k] = BIGNUM;
        if (!OK(nx, ny)) continue;
        int nb = ny * W + nx;
        if (g_mark[nb] != g_markNow) {   // 4 个邻居不会重复，这里只是保险
            g_mark[nb] = g_markNow;
            nw++;
        }
    }
    if (nw == 0) return;
    bfsAll(tx, ty, nw);
    for (int k = 0; k < 4; k++) {
        int nx = rx + DX4[k], ny = ry + DY4[k];
        if (!OK(nx, ny)) continue;
        int nb = ny * W + nx;
        if (g_stamp[nb] == g_stampNow) out[k] = g_dist[nb];
    }
}

// ===========================================================================
// 日志
// ===========================================================================
void writeLog(int t, const string& msg) {
    LOGBUF += I2S(t);
    LOGBUF += ' ';
    LOGBUF += msg;
    LOGBUF += '\n';
    // 顺便统计一下，省得到处加（与旧版 writeLog 的 SPLIT 判定逐字对应：
    // 看消息的第一个词；DELIVER 再看最后一个词）
    size_t n = msg.size(), i = 0;
    while (i < n && ISSP(msg[i])) i++;
    if (i >= n) return;
    size_t j = i;
    while (j < n && !ISSP(msg[j])) j++;
    size_t len = j - i;
    if (len == 7 && msg.compare(i, 7, "DELIVER") == 0) {
        size_t e = n;
        while (e > 0 && ISSP(msg[e - 1])) e--;
        size_t s2 = e;
        while (s2 > 0 && !ISSP(msg[s2 - 1])) s2--;
        long long lat = atoi(msg.c_str() + s2);
        CNT[0]++;
        CNT[4] += lat;
        if (lat > CNT[5]) CNT[5] = lat;
    } else if (len == 4 && msg.compare(i, 4, "LOST") == 0) {
        CNT[1]++;
    } else if (len == 6 && msg.compare(i, 6, "REJECT") == 0) {
        CNT[2]++;
    }
}

void writeRaw(const string& msg) {
    LOGBUF += msg;
    LOGBUF += '\n';
}

void dumpLog() {
    // 一次性写出（旧版是逐行 cout << ... << endl，每个 endl 都 flush 一次）
    if (!LOGBUF.empty()) fwrite(LOGBUF.data(), 1, LOGBUF.size(), stdout);
}
