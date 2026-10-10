// dist.cpp —— 距离计算与缓存
//
// BFS 距离只取决于"墙 + 临时封锁"，与机器人、订单都无关，所以：
//   * 封锁不变时，同一出发点的距离表可以一直复用；
//   * 封锁一变（BLOCK/UNBLOCK 真正改动了状态）就整体失效。
// 距离表按 LRU 缓存，内存有上限（见 DistCache::capEntries）。
//
// 三个距离函数必须逐条对齐基线 util.cpp 的怪语义（详见 HANDOFF.md）：
//   DIST(a,b)   : a==b 时返回 0（哪怕 b 是墙/被封锁！）；b 不可通行返回 INF；
//                 否则从 a 出发四连通 BFS，a 无论能不能走都作为起点。
//   CHG_DIST(x,y): 没有 a==b 特判；起点强制是 0；取所有"可通行"充电桩的最小距离。
#include "common.h"

// 方向顺序：上 右 下 左 —— 所有平局都按这个顺序，不能改
static const int DX4[4] = {0, 1, 0, -1};
static const int DY4[4] = {-1, 0, 1, 0};

// ---------------- 地图查询 ----------------
bool INMAP(int x, int y) {
    return x >= 0 && y >= 0 && x < W && y < H;
}

bool ISWALL(int x, int y) {
    return MAP[y][x] == '#';
}

bool OK(int x, int y) {
    return INMAP(x, y) && MAP[y][x] != '#' && !BLOCKED[y * W + x];
}

// ---------------- 距离表缓存 ----------------
namespace {

struct CacheEntry {
    int key = -1;
    long long lastUse = 0;
    vector<int> d;
};

vector<CacheEntry> g_entries;
unordered_map<int, int> g_slot;    // key -> g_entries 下标
long long g_clock = 0;
int g_version = -1;
size_t g_capEntries = 64;
vector<int> g_queue;               // BFS 队列复用缓冲

void cacheReset() {
    g_entries.clear();
    g_slot.clear();
    g_version = BLOCK_VERSION;
    size_t cells = (size_t)W * H;
    if (cells == 0) cells = 1;
    // 距离表每张 W*H*4 字节；总内存控制在 ~256MB 以内（上限 1GB）
    size_t byMem = (size_t)256 * 1024 * 1024 / (cells * 4);
    g_capEntries = max<size_t>(16, min<size_t>(byMem, 8192));
}

}  // namespace

void DistInvalidate() {
    BLOCK_VERSION++;
    g_entries.clear();
    g_slot.clear();
}

const vector<int>& DistTable(int sx, int sy) {
    if (g_version != BLOCK_VERSION) cacheReset();
    int key = sy * W + sx;
    auto it = g_slot.find(key);
    if (it != g_slot.end()) {
        CacheEntry& e = g_entries[it->second];
        e.lastUse = ++g_clock;
        return e.d;
    }
    // 没有缓存：跑一遍 BFS（起点无论能不能走都是 0，之后只走可通行格）
    int cells = W * H;
    vector<int> d(cells, INF);
    if (g_queue.size() < (size_t)cells) g_queue.resize(cells);
    int qh = 0, qt = 0;
    d[key] = 0;
    g_queue[qt++] = key;
    while (qh < qt) {
        int cur = g_queue[qh++];
        int cx = cur % W, cy = cur / W;
        int base = d[cur] + 1;
        for (int k = 0; k < 4; k++) {
            int nx = cx + DX4[k], ny = cy + DY4[k];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            if (MAP[ny][nx] == '#') continue;
            if (BLOCKED[ny * W + nx]) continue;
            int nk = ny * W + nx;
            if (d[nk] != INF) continue;
            d[nk] = base;
            g_queue[qt++] = nk;
        }
    }
    // 找空位 / 淘汰最久未用的一张
    int slot;
    if (g_entries.size() < g_capEntries) {
        slot = (int)g_entries.size();
        g_entries.emplace_back();
    } else {
        slot = 0;
        for (size_t i = 1; i < g_entries.size(); i++)
            if (g_entries[i].lastUse < g_entries[slot].lastUse) slot = (int)i;
        g_slot.erase(g_entries[slot].key);
        g_entries[slot].d.clear();
    }
    g_entries[slot].key = key;
    g_entries[slot].lastUse = ++g_clock;
    g_entries[slot].d = std::move(d);
    g_slot[key] = slot;
    return g_entries[slot].d;
}

int DIST(int ax, int ay, int bx, int by) {
    if (ax == bx && ay == by) return 0;      // 基线：先判相等，哪怕目标是墙也返回 0
    if (!OK(bx, by)) return INF;
    return DistTable(ax, ay)[by * W + bx];
}

// 到最近"可通行"充电桩的距离；没有可达的返回 INF
int CHG_DIST(int x, int y) {
    const vector<int>& d = DistTable(x, y);
    const vector<pair<int, int> >& cs = ALL_CHG();
    int best = INF;
    for (size_t i = 0; i < cs.size(); i++) {
        int cx = cs[i].first, cy = cs[i].second;
        if (!OK(cx, cy)) continue;           // 被封锁的充电桩不算
        int v = d[cy * W + cx];
        if (v < best) best = v;
    }
    return best;
}

const vector<pair<int, int> >& ALL_CHG() {
    static vector<pair<int, int> > cs;
    static int cachedW = -1, cachedH = -1;
    if ((int)cs.empty() || cachedW != W || cachedH != H) {
        cs.clear();
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (MAP[y][x] == 'C') cs.push_back(make_pair(x, y));
        cachedW = W;
        cachedH = H;
    }
    return cs;
}
