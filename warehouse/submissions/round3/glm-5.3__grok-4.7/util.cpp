// util.cpp  工具函数（优化版）
// 变化点（详见 HANDOFF.md）：
//  * BFS/BFS2/bfs_fast_cached 合并成一个带缓存的 FIELD()（三者距离值完全一致，见 HANDOFF 论证）；
//  * BLK 增删走 blkAdd/blkRemove，联动网格镜像 + 距离场缓存失效；
//  * OCC 由每请求 O(NR) 线性扫改为占用计数网格 OCCG；
//  * I2S 改用 to_string；writeLog 不再对每条日志做 SPLIT；
//  * dumpLog 用 fwrite 批量输出（基线的 endl 逐行 flush 是大头之一）。
#include "common.h"

int DX4[4] = {0, 1, 0, -1};
int DY4[4] = {-1, 0, 1, 0};   // 上 右 下 左  !!!顺序不能改!!!

vector<string> SPLIT(const string& s) {
    vector<string> res;
    string cur;
    for (int i = 0; i < (int)s.size(); i++) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
            if (!cur.empty()) { res.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) res.push_back(cur);
    return res;
}

int S2I(const string& s) {
    return atoi(s.c_str());
}

string I2S(long long v) {
    return to_string(v);
}

string RNAME(int i) {
    return "R" + to_string(i);
}

bool INMAP(int x, int y) {
    return (unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H;
}

bool ISWALL(int x, int y) {
    return WALLG[(size_t)y * W + x] != 0;
}

// 能不能走（图内 不是墙 没封锁）
bool OK(int x, int y) {
    if (!INMAP(x, y)) return false;
    size_t k = (size_t)y * W + x;
    if (WALLG[k]) return false;
    if (BLKG[k]) return false;
    return true;
}

// ---- 封锁格 + 距离场缓存 --------------------------------------------------

namespace {
struct FieldEntry { vector<vector<int> > d; };
map<pair<int, int>, FieldEntry> g_fields;   // (起点) -> 距离场；版本不合即整体清空
size_t g_fieldCap = 64;                      // 条数上限，按地图面积换算，防内存超限
vector<int> g_dist, g_q;                     // BFS 复用工作区
vector<vector<int> > g_noField;              // 越界起点的防御用全 BIGNUM 场
}

void fieldSetup() {
    g_fields.clear();
    g_fieldCap = max<size_t>(64, (size_t)40000000 / ((size_t)W * (size_t)H + 1));
    g_noField.assign(H, vector<int>(W, BIGNUM));
}

void fieldInvalidate() {
    g_fields.clear();
}

// 实际新增封锁才调这里（墙/已在集合里的情况由调用方或 insert 判掉）
void blkAdd(int x, int y) {
    if (BLK.insert(make_pair(x, y)).second) {
        BLKG[(size_t)y * W + x] = 1;
        BLKVER++;
        fieldInvalidate();
    }
}

void blkRemove(int x, int y) {
    if (BLK.erase(make_pair(x, y)) > 0) {
        BLKG[(size_t)y * W + x] = 0;
        BLKVER++;
        fieldInvalidate();
    }
}

const vector<vector<int> >& FIELD(int sx, int sy) {
    if (!INMAP(sx, sy)) return g_noField;   // 合法输入不会走到；防御，避免越界崩溃
    pair<int, int> key(sx, sy);
    map<pair<int, int>, FieldEntry>::iterator it = g_fields.find(key);
    if (it != g_fields.end()) return it->second.d;
    if (g_fields.size() >= g_fieldCap) g_fields.clear();   // 满 则整体清空（ correctness 不受影响）
    // BFS：起点恒为 0，其余只经过 OK 格。邻居扩展顺序不影响距离值，故与基线 BFS/BFS2 等价。
    g_dist.assign((size_t)W * H, BIGNUM);
    g_q.clear();
    g_dist[(size_t)sy * W + sx] = 0;
    g_q.push_back((size_t)sy * W + sx);
    for (size_t h = 0; h < g_q.size(); h++) {
        size_t c = g_q[h];
        int cx = (int)(c % W), cy = (int)(c / W);
        int nd = g_dist[c] + 1;
        if (cy > 0 && OK(cx, cy - 1) && g_dist[c - W] == BIGNUM) { g_dist[c - W] = nd; g_q.push_back(c - W); }
        if (cx + 1 < W && OK(cx + 1, cy) && g_dist[c + 1] == BIGNUM) { g_dist[c + 1] = nd; g_q.push_back(c + 1); }
        if (cy + 1 < H && OK(cx, cy + 1) && g_dist[c + W] == BIGNUM) { g_dist[c + W] = nd; g_q.push_back(c + W); }
        if (cx > 0 && OK(cx - 1, cy) && g_dist[c - 1] == BIGNUM) { g_dist[c - 1] = nd; g_q.push_back(c - 1); }
    }
    FieldEntry e;
    e.d.resize(H);
    for (int y = 0; y < H; y++)
        e.d[y].assign(g_dist.begin() + (size_t)y * W, g_dist.begin() + (size_t)(y + 1) * W);
    return g_fields.insert(make_pair(key, move(e))).first->second.d;
}

// a 到 b 的距离。基线语义：相等为 0；b 不可走为 BIGNUM；否则从 a 做 BFS（a 本身可以不可走）。
// 优化：两端都可走时无向图距离对称，直接查"从 b 出发"的缓存场，省掉从 a 重建场。
int DIST(int ax, int ay, int bx, int by) {
    if (ax == bx && ay == by) return 0;
    if (!OK(bx, by)) return BIGNUM;
    if (OK(ax, ay)) return FIELD(bx, by)[ay][ax];
    return FIELD(ax, ay)[by][bx];   // a 不可走（如机器人站在被封锁的格子上）时距离不对称，按基线方向算
}

int CHG_DIST(int x, int y) {
    if (!INMAP(x, y)) return BIGNUM;   // 防御（合法输入不会出现）
    const vector<vector<int> >& d = FIELD(x, y);
    int best = BIGNUM;
    for (size_t i = 0; i < CHGS.size(); i++) {
        int cx = CHGS[i].first, cy = CHGS[i].second;
        if (!OK(cx, cy)) continue;
        if (d[cy][cx] < best) best = d[cy][cx];
    }
    return best;
}

// 最近的可用充电桩，没有就 (-1,-1)；距离并列时取行优先扫描里靠前的（与基线一致，用严格 <）
pair<int, int> CHG_NEAR(int x, int y) {
    pair<int, int> res = make_pair(-1, -1);
    if (!INMAP(x, y)) return res;   // 防御（合法输入不会出现）
    const vector<vector<int> >& d = FIELD(x, y);
    int best = BIGNUM;
    for (size_t i = 0; i < CHGS.size(); i++) {
        int cx = CHGS[i].first, cy = CHGS[i].second;
        if (!OK(cx, cy)) continue;
        if (d[cy][cx] < best) {
            best = d[cy][cx];
            res = CHGS[i];
        }
    }
    return res;
}

vector<pair<int, int> > ALL_CHG() {
    return CHGS;
}

// ---- 机器人状态 / 占格计数 -------------------------------------------------

const char* const STNAME[6] = {"IDLE", "TO_PICKUP", "DELIVERING", "TO_CHARGER", "CHARGING", "DEAD"};

// 状态迁移唯一入口：同步 RST/RSTI，并维护 OCCG（停机的车不占格）
void setRST(int i, int state) {
    if (RSTI[i] != state) {
        size_t k = (size_t)RY[i] * W + RX[i];
        if (RSTI[i] == ST_DEAD) OCCG[k]++;
        else if (state == ST_DEAD) OCCG[k]--;
        RSTI[i] = state;
    }
    RST[i] = STNAME[state];
}

void occInit() {
    OCCG.assign((size_t)W * H, 0);
    for (int i = 0; i < NR; i++) OCCG[(size_t)RY[i] * W + RX[i]]++;   // 初始全是 IDLE（活车）
}

// 机器人 i 挪窝的唯一入口（调用方保证 i 未停机）
void occMove(int i, int nx, int ny) {
    OCCG[(size_t)RY[i] * W + RX[i]]--;
    RX[i] = nx;
    RY[i] = ny;
    OCCG[(size_t)ny * W + nx]++;
}

// 格子上有没有别的活车（except 自己不算、停机的车不算；语义与基线逐条一致）
bool OCC(int x, int y, int except) {
    if (!INMAP(x, y)) return false;
    int c = OCCG[(size_t)y * W + x];
    if (except >= 0 && except < NR && RX[except] == x && RY[except] == y && RSTI[except] != ST_DEAD) c--;
    return c > 0;
}

int findRobot(const string& name) {
    for (int i = 0; i < NR; i++)
        if (RNAME(i) == name) return i;
    return -1;
}

// ---- 日志 -----------------------------------------------------------------

static bool isSpaceCh(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

// 首个 token 是否恰好等于 w（等价于基线 SPLIT 后比较 w[0]）
static bool firstTokenIs(const string& msg, const char* w) {
    size_t L = strlen(w);
    if (msg.size() < L || memcmp(msg.data(), w, L) != 0) return false;
    return msg.size() == L || isSpaceCh(msg[L]);
}

void writeLog(int t, const string& msg) {
    LOGBUF.push_back(I2S(t) + " " + msg);
    // 统计：只看首 token（基线对每条日志做 SPLIT，这里改成前缀判断）
    if (firstTokenIs(msg, "DELIVER")) {
        size_t sp = msg.rfind(' ');
        long long lat = (sp == string::npos) ? 0 : atoll(msg.c_str() + sp + 1);
        CNT[0]++;
        CNT[4] += lat;
        if (lat > CNT[5]) CNT[5] = lat;
    } else if (firstTokenIs(msg, "LOST")) {
        CNT[1]++;
    } else if (firstTokenIs(msg, "REJECT")) {
        CNT[2]++;
    }
}

void writeRaw(const string& msg) {
    LOGBUF.push_back(msg);
}

void dumpLog() {
    // 逐行 endl flush（基线）改为攒块 fwrite，输出字节完全相同
    string buf;
    buf.reserve(1 << 20);
    for (size_t i = 0; i < LOGBUF.size(); i++) {
        buf += LOGBUF[i];
        buf += '\n';
        if (buf.size() >= (1u << 20)) {
            fwrite(buf.data(), 1, buf.size(), stdout);
            buf.clear();
        }
    }
    if (!buf.empty()) fwrite(buf.data(), 1, buf.size(), stdout);
    fflush(stdout);
}
