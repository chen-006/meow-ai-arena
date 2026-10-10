// util.cpp  工具函数
// 2025 重构：与基线逐字节等价，内部全部改成 O(1)/缓存 实现。
#include "common.h"

// !!! 方向顺序不能改（基线注释原话）：上 右 下 左。
// 移动选邻居、让路等逻辑里并列时按这个顺序取第一个。
static const int DX4[4] = {0, 1, 0, -1};
static const int DY4[4] = {-1, 0, 1, 0};

int S2I(const string& s) {
    return atoi(s.c_str());
}

string I2S(long long v) {
    // 与 stringstream<<v 输出一致；v 全为非负或普通整数，to_string 结果相同
    return to_string(v);
}

string RNAME(int i) {
    return "R" + to_string(i);
}

bool INMAP(int x, int y) {
    return x >= 0 && y >= 0 && x < W && y < H;
}

bool ISWALL(int x, int y) {
    return MAP[y][x] == '#';
}

bool OK(int x, int y) {
    return INMAP(x, y) && MAP[y][x] != '#' && !BLK[y * W + x];
}

// ---- 占用网格：OCC 的 O(NR) 扫描改成 O(1) ------------------------
// 语义：DEAD 机器人不占格子；同一格可以有多个活机器人（基线初始位置不重复，
// 但运行中死人格子可以被踩）。cnt==1 且唯一占用者是 except 时视为无车。
static vector<int> occCnt;
static vector<int> occIdx;   // cnt>=1 时记录最后一个进入的机器人

static void occInit() {
    occCnt.assign(W * H, 0);
    occIdx.assign(W * H, -1);
}

void occAdd(int i) {
    int k = RY[i] * W + RX[i];
    occCnt[k]++;
    occIdx[k] = i;
}

void occRemove(int i) {
    occCnt[RY[i] * W + RX[i]]--;
}

bool OCC(int x, int y, int except) {
    int k = y * W + x;
    if (occCnt[k] == 0) return false;
    if (occCnt[k] == 1 && occIdx[k] == except) return false;
    return true;
}

// ---- BFS 距离场缓存 ----------------------------------------------
// 基线每次都重算整张 BFS。这里按 (x,y) 缓存，BLK 变化（BLKVER 递增）即失效。
// 缓存只省时间，命中与否不影响结果；满了整体清空，保证内存有界。
// 注意保留基线的怪行为：起点即使不可走，d[start]=0，且仍从起点向外扩展。
struct FieldEntry {
    int ver;
    shared_ptr<vector<int> > d;
};
static unordered_map<long long, FieldEntry> fieldCache;
static const size_t FIELD_CACHE_CAP = 512;   // 512 * W*H(<=40000) * 4B ≈ 80MB 上限

static shared_ptr<vector<int> > bfsCompute(int sx, int sy) {
    shared_ptr<vector<int> > dp(new vector<int>((size_t)W * H, BIGNUM));
    vector<int>& d = *dp;
    deque<int> q;
    d[sy * W + sx] = 0;
    q.push_back(sy * W + sx);
    while (!q.empty()) {
        int cur = q.front();
        q.pop_front();
        int cx = cur % W, cy = cur / W;
        int nd = d[cur] + 1;
        for (int k = 0; k < 4; k++) {
            int nx = cx + DX4[k], ny = cy + DY4[k];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            int nk = ny * W + nx;
            if (MAP[ny][nx] == '#' || BLK[nk]) continue;
            if (d[nk] != BIGNUM) continue;
            d[nk] = nd;
            q.push_back(nk);
        }
    }
    return dp;
}

// 返回的 shared_ptr 由调用方持有，缓存清空也不会悬垂
shared_ptr<vector<int> > getFieldPtr(int sx, int sy) {
    long long key = (long long)sy * W + sx;
    unordered_map<long long, FieldEntry>::iterator it = fieldCache.find(key);
    if (it != fieldCache.end() && it->second.ver == BLKVER) return it->second.d;
    shared_ptr<vector<int> > d = bfsCompute(sx, sy);
    if (fieldCache.size() >= FIELD_CACHE_CAP) fieldCache.clear();
    FieldEntry e;
    e.ver = BLKVER;
    e.d = d;
    fieldCache[key] = e;
    return d;
}

// 基线 DIST：同点直接 0（不管目标能不能走）；目标不可走 BIGNUM；否则 BFS
int DIST(int ax, int ay, int bx, int by) {
    if (ax == bx && ay == by) return 0;
    if (!OK(bx, by)) return BIGNUM;
    return (*getFieldPtr(ax, ay))[by * W + bx];
}

// ---- 充电桩 -------------------------------------------------------
vector<pair<int, int> >& allChg() {
    static vector<pair<int, int> > cs;
    static bool init = false;
    if (!init) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (MAP[y][x] == 'C') cs.push_back(make_pair(x, y));
        init = true;
    }
    return cs;
}

// 每格到最近可用充电桩的距离：多源 BFS 一次算完（源 = 当前 OK 的充电桩，
// 只经过 OK 格子；无向图距离对称，与基线"从 (x,y) 单向 BFS 再找最近桩"等价）。
static shared_ptr<vector<int> > chgFieldPtr;
static int chgFieldVer = -1;

static void chgFieldCompute() {
    chgFieldPtr.reset(new vector<int>((size_t)W * H, BIGNUM));
    vector<int>& d = *chgFieldPtr;
    deque<int> q;
    vector<pair<int, int> >& cs = allChg();
    for (int i = 0; i < (int)cs.size(); i++) {
        int x = cs[i].first, y = cs[i].second;
        if (!OK(x, y)) continue;
        int k = y * W + x;
        if (d[k] == 0) continue;
        d[k] = 0;
        q.push_back(k);
    }
    while (!q.empty()) {
        int cur = q.front();
        q.pop_front();
        int cx = cur % W, cy = cur / W;
        int nd = d[cur] + 1;
        for (int k = 0; k < 4; k++) {
            int nx = cx + DX4[k], ny = cy + DY4[k];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            int nk = ny * W + nx;
            if (MAP[ny][nx] == '#' || BLK[nk]) continue;
            if (d[nk] != BIGNUM) continue;
            d[nk] = nd;
            q.push_back(nk);
        }
    }
    chgFieldVer = BLKVER;
}

const vector<int>& chgField() {
    if (chgFieldVer != BLKVER) chgFieldCompute();
    return *chgFieldPtr;
}

// 基线 CHG_DIST 的怪行为：起点 (x,y) 即使不可走也记 0，并向 OK 邻居扩展；
// 充电桩列表里被封的桩跳过。对 OK 起点等价于多源场；对不可走起点等价于
// "OK 邻居的场值 +1"。
int CHG_DIST(int x, int y) {
    const vector<int>& f = chgField();
    if (OK(x, y)) return f[y * W + x];
    int best = BIGNUM;
    for (int k = 0; k < 4; k++) {
        int nx = x + DX4[k], ny = y + DY4[k];
        if (!OK(nx, ny)) continue;
        int v = f[ny * W + nx];
        if (v != BIGNUM && v + 1 < best) best = v + 1;
    }
    return best;
}

// 最近可用充电桩，并列时按 allChg() 扫描顺序（y 行优先）取第一个
pair<int, int> CHG_NEAR(int x, int y) {
    shared_ptr<vector<int> > d = getFieldPtr(x, y);
    int best = BIGNUM;
    pair<int, int> res = make_pair(-1, -1);
    vector<pair<int, int> >& cs = allChg();
    for (int i = 0; i < (int)cs.size(); i++) {
        if (!OK(cs[i].first, cs[i].second)) continue;
        int v = (*d)[cs[i].second * W + cs[i].first];
        if (v < best) {
            best = v;
            res = cs[i];
        }
    }
    return res;
}

// ---- 日志 ----------------------------------------------------------
// 基线在 writeLog 里顺手解析消息做统计（DELIVER/LOST/REJECT），保留。
// CNT[3]（取消数）在事件处理处自增，不在这里。
void writeLog(int t, const string& msg) {
    LOGBUF += I2S(t);
    LOGBUF += ' ';
    LOGBUF += msg;
    LOGBUF += '\n';
    // 解析第一个单词
    size_t sp = msg.find(' ');
    string w0 = (sp == string::npos) ? msg : msg.substr(0, sp);
    if (w0 == "DELIVER") {
        size_t last = msg.rfind(' ');
        long long lat = atoll(msg.c_str() + (last == string::npos ? 0 : last + 1));
        CNT[0]++;
        CNT[4] += lat;
        if (lat > CNT[5]) CNT[5] = lat;
    } else if (w0 == "LOST") {
        CNT[1]++;
    } else if (w0 == "REJECT") {
        CNT[2]++;
    }
}

void writeRaw(const string& msg) {
    LOGBUF += msg;
    LOGBUF += '\n';
}

void dumpLog() {
    fwrite(LOGBUF.data(), 1, LOGBUF.size(), stdout);
}

// 仿真初始化时调用一次（sim.cpp ReadAll 里）
void utilReset() {
    occInit();
}
