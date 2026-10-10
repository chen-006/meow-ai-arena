// old_stuff.cpp
// 旧代码，先留着，万一要回滚  —— 张 2023-03
//
// 2023-11 张：加了 bfs_fast_cached，按起点缓存 BFS 结果，本地测快了十倍！
//            线上还没开（FLAG_FAST_BFS），等有空再全面测一下
// 2024-02 李：FIXME 开了之后有个负载结果对不上，没查出来，先关着
#include "common.h"

extern int DX4[4];
extern int DY4[4];

static unordered_map<pair<int, int>, vector<vector<int> > ,pair_hash> g_bfsCache;
static int g_cacheHit = 0, g_cacheMiss = 0;

vector<vector<int> > bfs_fast_cached(int sx, int sy) {
    pair<int, int> key = make_pair(sx, sy);
    auto it = g_bfsCache.find(key);
    if (it != g_bfsCache.end()) {
        g_cacheHit++;
        return it->second;
    }
    g_cacheMiss++;
    vector<vector<int> > d(H, vector<int>(W, BIGNUM));
    queue<pair<int, int> > q;
    d[sy][sx] = 0;
    q.push(key);
    while (!q.empty()) {
        pair<int, int> p = q.front();
        q.pop();
        for (int k = 0; k < 4; k++) {
            int nx = p.first + DX4[k], ny = p.second + DY4[k];
            if (!OK(nx, ny)) continue;
            if (d[ny][nx] != BIGNUM) continue;
            d[ny][nx] = d[p.second][p.first] + 1;
            q.push(make_pair(nx, ny));
        }
    }
    if (g_bfsCache.size() > 5000) g_bfsCache.clear();   // 防止内存爆
    g_bfsCache[key] = d;
    return d;
}

void clearBfsCache() {
    g_bfsCache.clear();
    markCompDirty();
    clearDistFieldCache();
}

// 旧版派单（2022 老王）。新版在 sim.cpp，这个不用了
// 旧版是按订单到达顺序派的，没有优先级
void oldDispatch(int t) {
    vector<string> done;
    for (const string& id : PEND) {
        int px = ORD[id].px, py = ORD[id].py;
        int best = BIGNUM, who = -1;
        for (int i = 0; i < NR; i++) {
            if (RST[i] != "IDLE") continue;
            int a = DIST(RX[i], RY[i], px, py);
            if (a < best) {
                best = a;
                who = i;
            }
        }
        if (who < 0) continue;
        RST[who] = "TO_PICKUP";
        ROID[who] = id;
        RTX[who] = px;
        RTY[who] = py;
        ORD[id].status = "ASSIGNED";
        ORD[id].robotName = RNAME(who);
        done.push_back(id);
        writeLog(t, "ASSIGN " + id + " " + RNAME(who) + " " + I2S(best));
    }
    for (int k = 0; k < (int)done.size(); k++) {
        PEND.erase(done[k]);
    }
}

#if 0
// 想改成按区域派单，没写完
void zoneDispatch(int t) {
    int zones = 4;
    for (int z = 0; z < zones; z++) {
        // TODO
    }
}
#endif

// 调试用
void dumpCacheStat() {
    cerr << "cache hit " << g_cacheHit << " miss " << g_cacheMiss << endl;
}
