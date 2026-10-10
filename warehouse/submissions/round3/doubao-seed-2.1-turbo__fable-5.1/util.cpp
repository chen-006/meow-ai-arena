// util.cpp  工具函数 + BFS 距离场缓存池（带"陈旧距离场仍可用"判定）
#include "common.h"

static const int DX4[4] = {0, 1, 0, -1};
static const int DY4[4] = {-1, 0, 1, 0};   // 上 右 下 左  顺序不能改（移动选邻格时取第一个最小）

void appendInt(string& s, long long v) {
    char buf[24];
    int n = 0;
    if (v < 0) { s.push_back('-'); v = -v; }
    if (v == 0) { s.push_back('0'); return; }
    while (v > 0) { buf[n++] = char('0' + v % 10); v /= 10; }
    while (n > 0) s.push_back(buf[--n]);
}

string I2S(long long v) {
    string s;
    appendInt(s, v);
    return s;
}

const char* robotStateStr(int s) {
    switch (s) {
        case S_IDLE: return "IDLE";
        case S_TO_PICKUP: return "TO_PICKUP";
        case S_DELIVERING: return "DELIVERING";
        case S_TO_CHARGER: return "TO_CHARGER";
        case S_CHARGING: return "CHARGING";
        default: return "DEAD";
    }
}

// ---------------- 封锁变更日志 ----------------
// CHLOG[v] 是把版本 v 变成 v+1 的那次变更
struct Change { int cell; bool block; };
static vector<Change> CHLOG;

void setBlocked(int x, int y, bool blocked) {
    int c = y * W + x;
    uint8_t v = blocked ? 0 : 1;
    if (CELL_OK[c] != v) {
        CELL_OK[c] = v;
        CHLOG.push_back({c, blocked});
        BLK_VER++;
    }
}

// 判断版本 ver 时算出的距离场 F，在给定查询格上现在是否仍精确。
// 不变式（对 val 而言）：只要每次 UNBLOCK 的格子 c 的所有邻格都有 F ≥ val，
//   则任何格子 x 的真实距离 ≥ min(F[x], val+1)（BLOCK 只会让距离变大；经 c 的新路径长度 ≥ val+2）。
// 于是：F[q] == val 的格子 q，只要现在还存在一条长度 val 的路径（沿 F 梯度逐格 -1、每格当前可通行）
//   就仍然精确；F > val 或 F == INF 的格子真实距离 ≥ val+1，不会掺进来。
// val == DIST_INF（查询格不可达）：BLOCK 不会让它变得可达，UNBLOCK 按上面的规则（邻格都不可达）也不会。
static vector<int> NBR;   // NBR[cell*4+k] = 第 k 个邻格，出界为 -1
static const int MAX_GAP = 256;
static bool hasPathOfLength(const uint16_t* F, int q) {
    int cur = q;
    int k = F[q];
    while (k > 0) {
        const int* nb = &NBR[(size_t)cur * 4];
        int next = -1;
        for (int j = 0; j < 4; j++) {
            int nc = nb[j];
            if (nc >= 0 && CELL_OK[nc] && F[nc] == k - 1) { next = nc; break; }
        }
        if (next < 0) return false;
        cur = next;
        k--;
    }
    return true;
}
// verify[0..nv) 是需要精确的格子（它们的 F 都等于 val）
static bool fieldValid(const uint16_t* F, int ver, int val, const int* verify, int nv) {
    if (ver == BLK_VER) return true;
    if (BLK_VER - ver > MAX_GAP) return false;
    bool ballHit = false;
    for (int v = ver; v < BLK_VER; v++) {
        const Change& ch = CHLOG[v];
        if (ch.block) {
            if (val != DIST_INF && F[ch.cell] < val) ballHit = true;
        } else {
            const int* nb = &NBR[(size_t)ch.cell * 4];
            for (int k = 0; k < 4; k++)
                if (nb[k] >= 0 && F[nb[k]] < val) return false;
        }
    }
    if (!ballHit) return true;
    for (int i = 0; i < nv; i++)
        if (!hasPathOfLength(F, verify[i])) return false;
    return true;
}

// ---------------- 距离场缓存池 ----------------
// 每个槽存一张 W*H 的 uint16 距离表，按起点格索引；槽满时轮转覆盖。
// 槽可以是"截断"的：SLOT_LIM[s] = R 表示距离 ≤ R 的格子都已出队展开，因此距离 ≤ R+1 的格子值精确，
// 其余格子为 DIST_INF（未知，真实距离 > R+1）。SLOT_LIM == LIM_FULL 表示整张表完整。
static const int LIM_FULL = 1 << 30;
static int POOL_CAP = 0;
static vector<uint16_t> POOL;
static vector<int> SLOT_CELL, SLOT_VER, SLOT_LIM, SLOT_OF;   // SLOT_OF[cell] = 槽号或 -1
static vector<vector<int>> SLOT_LIST;   // 每个槽上次 BFS 发现过的格子（重算时只清这些）
static int NEXT_SLOT = 0;
static vector<int> BFSQ;

static vector<uint16_t> CHG_FIELD;
static int CHG_FIELD_VER = -1;

void initFieldPool() {
    long long perSlot = (long long)N * 2;   // 控制在 ~48MB 以内
    POOL_CAP = (int)max(16LL, min(1024LL, (48LL << 20) / max(1LL, perSlot)));
    POOL.assign((size_t)POOL_CAP * N, DIST_INF);
    SLOT_CELL.assign(POOL_CAP, -1);
    SLOT_VER.assign(POOL_CAP, -1);
    SLOT_LIM.assign(POOL_CAP, LIM_FULL);
    SLOT_OF.assign(N, -1);
    SLOT_LIST.assign(POOL_CAP, vector<int>());
    BFSQ.resize(N);
    CHG_FIELD.assign(N, DIST_INF);
    NBR.assign((size_t)N * 4, -1);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            for (int k = 0; k < 4; k++) {
                int nx = x + DX4[k], ny = y + DY4[k];
                if (INMAP(nx, ny)) NBR[(y * W + x) * 4 + k] = ny * W + nx;
            }
}

// 从 start 出发的 BFS。watch[0..3] 是"观察格"（机器人的四邻格，-1 表示无）：
// 一旦所有距离 ≤ m 的格子都已展开（m = 已发现的观察格的最小距离）就停止，返回截断半径 m；
// 队列耗尽则返回 LIM_FULL。watch 全为 -1 时就是完整 BFS。
static int bfsFill(uint16_t* d, int start, const int* watch, vector<int>& list) {
    for (int c : list) d[c] = DIST_INF;
    int head = 0, tail = 0;
    int m = LIM_FULL;
    d[start] = 0;
    BFSQ[tail++] = start;
    if (watch) for (int k = 0; k < 4; k++) if (watch[k] == start) m = 0;
    while (head < tail) {
        int c = BFSQ[head++];
        int cd = d[c];
        if (cd > m) { list.assign(BFSQ.begin(), BFSQ.begin() + tail); return m; }
        uint16_t nd = (uint16_t)(cd + 1);
        const int* nb = &NBR[(size_t)c * 4];
        for (int k = 0; k < 4; k++) {
            int nc = nb[k];
            if (nc < 0 || !CELL_OK[nc] || d[nc] != DIST_INF) continue;
            d[nc] = nd;
            BFSQ[tail++] = nc;
            if (watch && nd < m && (nc == watch[0] || nc == watch[1] || nc == watch[2] || nc == watch[3])) m = nd;
        }
    }
    list.assign(BFSQ.begin(), BFSQ.begin() + tail);
    return LIM_FULL;
}

static inline void computeSlot(int s, const int* watch) {
    SLOT_VER[s] = BLK_VER;
    SLOT_LIM[s] = bfsFill(&POOL[(size_t)s * N], SLOT_CELL[s], watch, SLOT_LIST[s]);
}

// 取 cell 的槽（可能是旧版本 / 截断的），没有就分配并按 watch 计算
static int slotFor(int cell, const int* watch) {
    int s = SLOT_OF[cell];
    if (s >= 0 && SLOT_CELL[s] == cell) return s;
    s = NEXT_SLOT;
    NEXT_SLOT = (NEXT_SLOT + 1) % POOL_CAP;
    if (SLOT_CELL[s] >= 0 && SLOT_OF[SLOT_CELL[s]] == s) SLOT_OF[SLOT_CELL[s]] = -1;
    SLOT_CELL[s] = cell;
    SLOT_OF[cell] = s;
    computeSlot(s, watch);
    return s;
}

int distFrom(int srcCell, int cell) {
    int s = slotFor(srcCell, nullptr);
    uint16_t* F = &POOL[(size_t)s * N];
    int v = F[cell];
    // 截断表里的 INF 只代表"未知"，需要完整表；已发现的值总是精确的
    bool need = (v == DIST_INF && SLOT_LIM[s] != LIM_FULL) || !fieldValid(F, SLOT_VER[s], v, &cell, 1);
    if (need) {
        computeSlot(s, nullptr);
        v = F[cell];
    }
    return v == DIST_INF ? BIGNUM : v;
}

const uint16_t* fieldForMove(int target, int rx, int ry) {
    int watch[4];
    for (int k = 0; k < 4; k++) {
        int nx = rx + DX4[k], ny = ry + DY4[k];
        watch[k] = INMAP(nx, ny) ? ny * W + nx : -1;
    }
    int s = slotFor(target, watch);
    uint16_t* F = &POOL[(size_t)s * N];
    if (SLOT_VER[s] != BLK_VER || SLOT_LIM[s] != LIM_FULL) {
        int m = DIST_INF;
        for (int k = 0; k < 4; k++)
            if (watch[k] >= 0 && CELL_OK[watch[k]] && F[watch[k]] < m) m = F[watch[k]];
        int verify[4], nv = 0;
        for (int k = 0; k < 4; k++)
            if (watch[k] >= 0 && CELL_OK[watch[k]] && F[watch[k]] == m) verify[nv++] = watch[k];
        // 截断表：邻格全未发现时（m == INF）说明机器人跑到了半径之外，必须重算
        bool need = (m == DIST_INF && SLOT_LIM[s] != LIM_FULL) || !fieldValid(F, SLOT_VER[s], m, verify, nv);
        if (need) computeSlot(s, watch);
    }
    return F;
}

// 多源 BFS：从所有可通行充电桩出发。对可通行格 c，CHG_FIELD[c] == 基线 CHG_DIST(c)。
static void rebuildChargerField() {
    CHG_FIELD_VER = BLK_VER;
    uint16_t* d = CHG_FIELD.data();
    for (int i = 0; i < N; i++) d[i] = DIST_INF;
    int head = 0, tail = 0;
    for (int c : CHG_CELLS) {
        if (!CELL_OK[c]) continue;
        d[c] = 0;
        BFSQ[tail++] = c;
    }
    while (head < tail) {
        int c = BFSQ[head++];
        uint16_t nd = (uint16_t)(d[c] + 1);
        const int* nb = &NBR[(size_t)c * 4];
        for (int k = 0; k < 4; k++) {
            int nc = nb[k];
            if (nc < 0 || !CELL_OK[nc] || d[nc] != DIST_INF) continue;
            d[nc] = nd;
            BFSQ[tail++] = nc;
        }
    }
}

// 多源场的有效性：除了通用规则，UNBLOCK 一个充电桩会新增一个源（距离 0），必须重算
static bool chargerFieldValid(int val, int cell) {
    if (CHG_FIELD_VER == BLK_VER) return true;
    if (CHG_FIELD_VER < 0) return false;
    if (BLK_VER - CHG_FIELD_VER > MAX_GAP) return false;
    for (int v = CHG_FIELD_VER; v < BLK_VER; v++) {
        const Change& ch = CHLOG[v];
        if (!ch.block && MAPC[ch.cell] == 'C') return false;
    }
    return fieldValid(CHG_FIELD.data(), CHG_FIELD_VER, val, &cell, 1);
}

int chargerDistAt(int cell) {
    int v = CHG_FIELD[cell];
    if (!chargerFieldValid(v, cell)) {
        rebuildChargerField();
        v = CHG_FIELD[cell];
    }
    return v == DIST_INF ? BIGNUM : v;
}
