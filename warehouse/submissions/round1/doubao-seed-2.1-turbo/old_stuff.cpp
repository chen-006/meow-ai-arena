// old_stuff.cpp
// 旧代码，先留着，万一要回滚  —— 张 2023-03
//
// 2023-11 张：加了 bfs_fast_cached，按起点缓存 BFS 结果，本地测快了十倍！
//            线上还没开（FLAG_FAST_BFS），等有空再全面测一下
// 2024-02 李：FIXME 开了之后有个负载结果对不上，没查出来，先关着
//
// 优化版注：当前 USE_NEW_DISPATCH=1，FLAG_FAST_BFS=0，本文件的函数不会被调用。
// 保留以保证编译通过，如有回滚需求再启用。
#include "common.h"

extern int DX4[4];
extern int DY4[4];

// 旧版 cached BFS（留作参考，不使用）
vector<vector<int>> bfs_fast_cached(int sx, int sy) {
    vector<vector<int>> d(H, vector<int>(W, BIGNUM));
    queue<pair<int, int>> q;
    d[sy][sx] = 0;
    q.push(make_pair(sx, sy));
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
    return d;
}

// 旧版派单（2022 老王）。新版在 sim.cpp，这个不用了
// 旧版是按订单到达顺序派的，没有优先级
void oldDispatch(int t) {
    vector<int> done;
    for (int oi = 0; oi < (int)PEND.size(); oi++) {
        int oidx = PEND[oi];
        Order& o = ORDS[oidx];
        int px = o.px, py = o.py;
        int best = BIGNUM, who = -1;
        for (int i = 0; i < NR; i++) {
            if (RST[i] != S_IDLE) continue;
            int a = DIST(RX[i], RY[i], px, py);
            if (a < best) {
                best = a;
                who = i;
            }
        }
        if (who < 0) continue;
        RST[who] = S_TO_PICKUP;
        ROID[who] = oidx;
        RTX[who] = px;
        RTY[who] = py;
        o.status = O_ASSIGNED;
        o.robotIdx = who;
        done.push_back(oidx);
        writeLog(t, "ASSIGN " + o.idStr + " " + RNAME(who) + " " + I2S(best));
    }
    for (int k = 0; k < (int)done.size(); k++) {
        PEND.erase(find(PEND.begin(), PEND.end(), done[k]));
    }
}
