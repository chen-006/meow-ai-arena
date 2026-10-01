// 圈地对战 bot —— "bulldozer"
// 策略概要：
//  - 在家(领地内、无轨迹)时：BFS 搜遍领地边界，枚举「出门 a 步、侧移 b 步、回家」的矩形圈地方案，
//    按 (中立收益 + 2.2*敌方领地收益 + 切断奖励) / 总步数 打分，并用敌人 BFS 距离场做暴露度检查。
//  - 在外(带轨迹)时：威胁 = 敌人到我轨迹的最短 BFS 距离；若 威胁 <= 回家距离+1 立即撤退。
//  - 机会主义切断：邻近敌轨迹直接切；敌轨迹在攻击半径内且自身安全时主动猎杀。
//  - 撞头规避：根据目标格与敌头的曼哈顿距离加权惩罚，轨迹越长越保守。
//  - 终局：剩余回合不足以往返时立即回家，最后几回合在领地内安全打转。
#include <bits/stdc++.h>
using namespace std;

static const int DX[4] = {0, 0, -1, 1};
static const int DY[4] = {-1, 1, 0, 0};
static const char DC[4] = {'U', 'D', 'L', 'R'};
int dirIndex(char c) { return (int)string("UDLR").find(c); }

struct Player { int x, y, dir, trailLen, area, deaths; };

int W, H, MAXT, N, ME, MOVE_MS, INIT_MS;
vector<pair<int, int>> spawnPos;
vector<Player> ps;
vector<int> ownerG, trailG;  // 每格: -1 中立/无, 否则玩家编号
int TURN = 0;

uint64_t rngState = 88172645463325252ULL;
static inline uint64_t xr() { rngState ^= rngState << 13; rngState ^= rngState >> 7; rngState ^= rngState << 17; return rngState; }

static inline bool inb(int x, int y) { return x >= 0 && x < W && y >= 0 && y < H; }
static inline int manh(int x1, int y1, int x2, int y2) { return abs(x1 - x2) + abs(y1 - y2); }

vector<int> enemyDist, distHome;
vector<char> grudge;  // 曾被敌方抢走的格子：优先抢回

// 多源 BFS：所有敌头的最短距离场（全图可通过，对敌人乐观 = 对我保守）
void bfsEnemy() {
    enemyDist.assign(W * H, -1);
    deque<int> q;
    for (int i = 0; i < N; i++) {
        if (i == ME) continue;
        int k = ps[i].y * W + ps[i].x;
        if (enemyDist[k] != 0) { enemyDist[k] = 0; q.push_back(k); }
    }
    while (!q.empty()) {
        int k = q.front(); q.pop_front();
        int x = k % W, y = k / W;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inb(nx, ny)) continue;
            int nk = ny * W + nx;
            if (enemyDist[nk] == -1) { enemyDist[nk] = enemyDist[k] + 1; q.push_back(nk); }
        }
    }
}

// 多源 BFS：从我的全部领地出发、不穿过自己轨迹的距离场 = 每格回家距离
void bfsHome() {
    distHome.assign(W * H, -1);
    deque<int> q;
    for (int k = 0; k < W * H; k++)
        if (ownerG[k] == ME) { distHome[k] = 0; q.push_back(k); }
    while (!q.empty()) {
        int k = q.front(); q.pop_front();
        int x = k % W, y = k / W;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inb(nx, ny)) continue;
            int nk = ny * W + nx;
            if (distHome[nk] == -1 && trailG[nk] != ME) { distHome[nk] = distHome[k] + 1; q.push_back(nk); }
        }
    }
}

// BFS：从头出发（第一步禁止掉头、不穿自己轨迹）到满足 goal 的最近格，返回首步方向；失败 -1
template <class F>
int bfsFirstStep(F goal, int& outDist, int* outCell = nullptr) {
    int SZ = W * H;
    vector<int> dist(SZ, -1), first(SZ, -1);
    deque<int> q;
    int sx = ps[ME].x, sy = ps[ME].y;
    for (int d = 0; d < 4; d++) {
        if (d == (ps[ME].dir ^ 1)) continue;
        int nx = sx + DX[d], ny = sy + DY[d];
        if (!inb(nx, ny)) continue;
        int nk = ny * W + nx;
        if (trailG[nk] == ME || dist[nk] != -1) continue;
        dist[nk] = 1; first[nk] = d; q.push_back(nk);
    }
    while (!q.empty()) {
        int k = q.front(); q.pop_front();
        int x = k % W, y = k / W;
        if (goal(x, y, k)) { outDist = dist[k]; if (outCell) *outCell = k; return first[k]; }
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inb(nx, ny)) continue;
            int nk = ny * W + nx;
            if (dist[nk] == -1 && trailG[nk] != ME) { dist[nk] = dist[k] + 1; first[nk] = first[k]; q.push_back(nk); }
        }
    }
    outDist = 1e9;
    return -1;
}

deque<int> plan;  // 计划的方向序列

struct MV { int d, tx, ty, k; };

int decide() {
    bfsEnemy();
    bfsHome();
    const Player& me = ps[ME];
    bool outside = me.trailLen > 0;

    vector<MV> legal;
    for (int d = 0; d < 4; d++) {
        if (d == (me.dir ^ 1)) continue;
        int nx = me.x + DX[d], ny = me.y + DY[d];
        if (!inb(nx, ny)) continue;
        int k = ny * W + nx;
        if (trailG[k] == ME) continue;
        legal.push_back({d, nx, ny, k});
    }
    if (legal.empty()) return me.dir;  // 必死，随缘

    int myEscape = 1e9;
    for (auto& m : legal) {
        int dh = distHome[m.k];
        if (dh >= 0) myEscape = min(myEscape, dh + 1);
    }
    int threat = 1e9;
    if (outside) {
        for (int k = 0; k < W * H; k++)
            if (trailG[k] == ME) threat = min(threat, enemyDist[k]);
    }
    // 混战敌人多、威胁场饱和，放宽撤退阈值；单挑保持谨慎
    int retreatMargin = (N == 2) ? 1 : 0;
    bool danger = outside && threat <= myEscape + retreatMargin;

    // 目标格撞头/对换风险评分（越小越安全）
    auto headRisk = [&](const MV& m) -> double {
        double r = 0;
        for (int e = 0; e < N; e++) {
            if (e == ME) continue;
            int md = manh(m.tx, m.ty, ps[e].x, ps[e].y);
            if (md == 0) {
                r += 100;  // 目标格是敌头：可能互换双亡（但也可能切死对方）
                // 敌方若直行恰好走进我当前格 → 对换风险极大
                if (ps[e].x + DX[ps[e].dir] == me.x && ps[e].y + DY[ps[e].dir] == me.y) r += 60;
            } else if (md == 1) {
                r += 40;   // 敌头可能同回合走进该格
                // 敌方直行预测格就是我的目标格 → 风险加倍
                if (ps[e].x + DX[ps[e].dir] == m.tx && ps[e].y + DY[ps[e].dir] == m.ty) r += 30;
            }
        }
        return r;
    };

    // 回家（撤退）选步：最小化回家距离，并列时远离敌人、避开敌头邻近格
    auto retreatMove = [&]() -> int {
        double best = -1e18; int bd = legal[0].d;
        for (auto& m : legal) {
            int dh = distHome[m.k];
            double sc;
            if (dh < 0) sc = -1e12;
            else {
                sc = -dh * 100.0 + min(enemyDist[m.k], 12) * 4.0;
                double hr = headRisk(m);
                sc -= hr * (me.trailLen > 6 ? 5.0 : 1.5);
                sc += (int)(xr() % 3);
            }
            if (sc > best) { best = sc; bd = m.d; }
        }
        return bd;
    };

    // ---- 终局：来不及往返立刻回家 ----
    if (outside && TURN + myEscape + 2 >= MAXT) return retreatMove();

    // ---- 紧急撤退 ----
    if (danger) { plan.clear(); return retreatMove(); }

    // ---- 机会主义切断：一步可及的敌轨迹 ----
    {
        double bestVal = 0; int bd = -1;
        for (auto& m : legal) {
            int c = trailG[m.k];
            if (c < 0 || c == ME) continue;
            double val = ps[c].trailLen + 4.0;
            bool isHead = (ps[c].x == m.tx && ps[c].y == m.ty);
            if (isHead && me.trailLen == 0) val += 60;   // 无轨迹头槌：对换也不亏
            if (isHead && me.trailLen > 8) val -= 100;   // 长轨迹时别赌对换
            val -= headRisk(m) * (me.trailLen > 6 ? 1.5 : 0.3);
            if (val > bestVal) { bestVal = val; bd = m.d; }
        }
        if (bd >= 0 && (!outside || me.trailLen <= 6 || threat > myEscape + 2)) {
            plan.clear();
            return bd;
        }
    }

    // ---- 主动猎杀：攻击半径内的敌轨迹 ----
    auto tryAttack = [&]() -> int {
        int adist, tgtCell = -1;
        int tgtEnemy = -1;
        int d = bfsFirstStep([&](int x, int y, int k) {
            int c = trailG[k];
            if (c >= 0 && c != ME) { tgtEnemy = c; return true; }
            return false;
        }, adist, &tgtCell);
        if (d < 0) return -1;
        int radius;
        if (N == 2) {
            radius = min(4 + ps[tgtEnemy].trailLen / 2, 10);
        } else {
            // 混战：追杀不产领地，只打「保卫国土」（敌轨迹贴近我领地）和「超长轨迹」的仗
            radius = 2;
            if (ps[tgtEnemy].trailLen >= 20) radius = max(radius, 6);
            if (tgtCell >= 0 && distHome[tgtCell] >= 0) {
                if (distHome[tgtCell] <= 2)
                    radius = max(radius, 10);  // 敌轨迹深入我领地：全力扑杀
                else if (distHome[tgtCell] <= 4)
                    radius = max(radius, 5 + ps[tgtEnemy].trailLen / 4);
            }
        }
        if (TURN > MAXT - 40) radius = min(radius, 5);
        if (adist > radius) return -1;
        if (outside && me.trailLen > 4) return -1;  // 带着长轨迹不追杀，先保收割
        if (outside && threat <= myEscape + 2) return -1;  // 自身难保不追杀
        return d;
    };

    if (outside) {
        int ad = tryAttack();
        if (ad >= 0) { plan.clear(); return ad; }
        // 执行计划
        while (!plan.empty()) {
            int d = plan.front();
            int nx = me.x + DX[d], ny = me.y + DY[d];
            bool ok = d != (me.dir ^ 1) && inb(nx, ny) && trailG[ny * W + nx] != ME;
            if (!ok) { plan.clear(); break; }
            // 长轨迹时避开敌头邻近格
            MV m{d, nx, ny, ny * W + nx};
            if (me.trailLen > 10 && headRisk(m) >= 60) { plan.clear(); return retreatMove(); }
            plan.pop_front();
            return d;
        }
        return retreatMove();  // 无计划：直接回家收割
    }

    // ---- 在家（无轨迹） ----
    int ad = tryAttack();
    if (ad >= 0) return ad;

    if (TURN >= MAXT - 10) {
        // 终局打转：留在领地内，远离敌人
        double best = -1e18; int bd = legal[0].d;
        for (auto& m : legal) {
            double sc = (ownerG[m.k] == ME ? 50 : 0) + min(enemyDist[m.k], 12) * 4.0 - headRisk(m) * 2.0 + (int)(xr() % 3);
            if (sc > best) { best = sc; bd = m.d; }
        }
        return bd;
    }

    if (plan.empty()) {
        // ============ 圈地规划搜索 ============
        int SZ = W * H;
        vector<int> dT(SZ, -1), from(SZ, -1), fdir(SZ, -1);
        deque<int> q;
        int start = me.y * W + me.x;
        dT[start] = 0; from[start] = start; q.push_back(start);
        while (!q.empty()) {
            int k = q.front(); q.pop_front();
            int x = k % W, y = k / W;
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d], ny = y + DY[d];
                if (!inb(nx, ny)) continue;
                int nk = ny * W + nx;
                if (ownerG[nk] == ME && dT[nk] == -1) { dT[nk] = dT[k] + 1; from[nk] = k; fdir[nk] = d; q.push_back(nk); }
            }
        }
        // 局势判断
        bool behind = false;
        if (N == 2) behind = ps[ME].area < ps[1 - ME].area;
        else { int rk = 0; for (int e = 0; e < N; e++) if (e != ME && ps[e].area > ps[ME].area) rk++; behind = rk >= 2; }
        bool endgameLead = false;
        if (TURN > MAXT - 100) {
            if (N == 2) endgameLead = ps[ME].area > ps[1 - ME].area;
            else { int rk = 0; for (int e = 0; e < N; e++) if (e != ME && ps[e].area > ps[ME].area) rk++; endgameLead = rk == 0; }
        }
        int turnBudget = MAXT - 6 - TURN;

        double bestScore = 0, bestValue = 0;
        int bF = -1, bD = 0, bA = 0, bS = 0, bB = 0;
        // 两档暴露度搜索：先安全档，找不到再放宽（混战整体放宽一档）
        int exp0 = (N == 2) ? (behind ? 1 : 2) : 1;
        for (int needExposure = exp0; needExposure >= 1 && bF < 0; needExposure--)
        for (int f = 0; f < SZ; f++) {
            if (dT[f] < 0) continue;
            int fx = f % W, fy = f / W;
            // 局部威胁决定该出口的圈地尺寸上限（远处放心大圈，近敌小圈快跑）
            int lt = enemyDist[f];
            bool late = TURN > MAXT - 120;
            int aCap = 22;
            if (late) aCap = endgameLead ? 6 : 24;  // 残局：领先收小圈保胜，落后开大圈豪赌
            int scale = (N == 2) ? lt / 2 : lt;  // 混战死亡便宜，尺寸钳制放宽
            int amax = clamp(scale, 2, aCap);
            int bmax = clamp(scale, 2, 15);
            for (int d = 0; d < 4; d++) {
                int ex = fx + DX[d], ey = fy + DY[d];
                if (!inb(ex, ey) || ownerG[ey * W + ex] == ME) continue;
                int perp0 = (d < 2) ? 2 : 0, perp1 = (d < 2) ? 3 : 1;
                for (int a = 1; a <= amax; a++) {
                    int farx = ex + DX[d] * (a - 1), fary = ey + DY[d] * (a - 1);
                    if (!inb(farx, fary)) break;
                    for (int sp = 0; sp < 2; sp++) {
                        int s = sp ? perp1 : perp0;
                        for (int b = 1; b <= bmax; b++) {
                            int cx = farx + DX[s] * b, cy = fary + DY[s] * b;
                            if (!inb(cx, cy)) break;
                            int rx = fx + DX[s] * b, ry = fy + DY[s] * b;
                            if (!inb(rx, ry) || ownerG[ry * W + rx] != ME) continue;
                            int total = dT[f] + 2 * a + b;
                            if (total > turnBudget) continue;
                            // 暴露度：敌人到轨迹格的距离 - 我到达该格的回合数
                            int exposure = 1e9, t = dT[f];
                            int cutBonus = 0;
                            for (int k2 = 0; k2 < a; k2++) {
                                int px = ex + DX[d] * k2, py = ey + DY[d] * k2; t++;
                                int pk = py * W + px;
                                exposure = min(exposure, enemyDist[pk] - t);
                                int c = trailG[pk]; if (c >= 0 && c != ME) cutBonus += ps[c].trailLen + 8;
                            }
                            for (int j = 1; j <= b; j++) {
                                int px = farx + DX[s] * j, py = fary + DY[s] * j; t++;
                                int pk = py * W + px;
                                exposure = min(exposure, enemyDist[pk] - t);
                                int c = trailG[pk]; if (c >= 0 && c != ME) cutBonus += ps[c].trailLen + 8;
                            }
                            for (int k2 = 1; k2 <= a - 1; k2++) {
                                int px = cx - DX[d] * k2, py = cy - DY[d] * k2; t++;
                                int pk = py * W + px;
                                exposure = min(exposure, enemyDist[pk] - t);
                                int c = trailG[pk]; if (c >= 0 && c != ME) cutBonus += ps[c].trailLen + 8;
                            }
                            if (exposure < needExposure) continue;
                            // 收益：矩形框内非我格数，敌领地双倍价值
                            int x0 = min(ex, cx), x1 = max(ex, cx), y0 = min(ey, cy), y1 = max(ey, cy);
                            double value = cutBonus;
                            for (int yy = y0; yy <= y1; yy++)
                                for (int xx = x0; xx <= x1; xx++) {
                                    int kk = yy * W + xx;
                                    int o = ownerG[kk];
                                    if (o == ME) continue;
                                    if (o == -1) value += 1.0;
                                    else {
                                        // 抢领头者价值更高（混战压名次）
                                        double w = (N > 2 && grudge[kk]) ? 3.2 : 2.2;  // 单挑不记仇：回抢低效
                                        if (N > 2 && ps[o].area >= ps[ME].area) w += 0.6;
                                        value += w;
                                    }
                                }
                            if (value <= 0) continue;
                            double score = value / (total + 1.0);
                            if (score > bestScore) { bestScore = score; bestValue = value; bF = f; bD = d; bA = a; bS = s; bB = b; }
                        }
                    }
                }
            }
        }
        if (bF >= 0) {
            // —— 验收：对选中方案做真实泛洪圈地模拟，防止估计失真（领地被撕裂/凹陷）——
            auto simulateGain = [&]() -> int {
                int fx = bF % W, fy = bF / W;
                vector<char> wall(SZ, 0);
                for (int k = 0; k < SZ; k++)
                    if (ownerG[k] == ME || trailG[k] == ME) wall[k] = 1;
                int px = fx, py = fy;
                auto step = [&](int d) { px += DX[d]; py += DY[d]; wall[py * W + px] = 1; };
                for (int i = 0; i < bA; i++) step(bD);
                for (int i = 0; i < bB; i++) step(bS);
                for (int i = 0; i < bA - 1; i++) step(bD ^ 1);  // 最后一步踏入领地，不算墙
                vector<char> seen(SZ, 0);
                deque<int> qq;
                auto seed = [&](int k) { if (!wall[k] && !seen[k]) { seen[k] = 1; qq.push_back(k); } };
                for (int x = 0; x < W; x++) { seed(x); seed((H - 1) * W + x); }
                for (int y = 0; y < H; y++) { seed(y * W); seed(y * W + W - 1); }
                while (!qq.empty()) {
                    int k = qq.front(); qq.pop_front();
                    int x = k % W, y = k / W;
                    for (int d = 0; d < 4; d++) {
                        int nx = x + DX[d], ny = y + DY[d];
                        if (!inb(nx, ny)) continue;
                        int nk = ny * W + nx;
                        if (!wall[nk] && !seen[nk]) { seen[nk] = 1; qq.push_back(nk); }
                    }
                }
                int gain = 0;
                for (int k = 0; k < SZ; k++)
                    if (!wall[k] && !seen[k] && ownerG[k] != ME) gain++;
                return gain + 2 * bA + bB;  // 加上轨迹本身变领地
            };
            int realGain = simulateGain();
            if (realGain <= 2) bF = -1;  // 只会漏水的方案才放弃（防止领地被撕裂后白跑）
        }
        if (bF >= 0) {
            vector<int> path;
            for (int c = bF; c != start; c = from[c]) path.push_back(fdir[c]);
            reverse(path.begin(), path.end());
            for (int d2 : path) plan.push_back(d2);
            for (int i = 0; i < bA; i++) plan.push_back(bD);
            for (int i = 0; i < bB; i++) plan.push_back(bS);
            for (int i = 0; i < bA; i++) plan.push_back(bD ^ 1);
        }
    }
    if (!plan.empty()) {
        int d = plan.front();
        int nx = me.x + DX[d], ny = me.y + DY[d];
        if (d != (me.dir ^ 1) && inb(nx, ny) && trailG[ny * W + nx] != ME) {
            plan.pop_front();
            return d;
        }
        plan.clear();
    }
    // 游荡兜底：走向安全的非我格
    double best = -1e18; int bd = legal[0].d;
    for (auto& m : legal) {
        double sc = min(enemyDist[m.k], 12) * 3.0 + (ownerG[m.k] != ME ? 8 : 0) - headRisk(m) + (int)(xr() % 4);
        if (sc > best) { best = sc; bd = m.d; }
    }
    return bd;
}

bool readState() {
    string tok;
    if (!(cin >> tok)) return false;  // TURN
    cin >> TURN;
    ps.assign(N, {});
    for (int i = 0; i < N; i++) {
        int id; char d; Player q;
        cin >> tok >> id >> q.x >> q.y >> d >> q.trailLen >> q.area >> q.deaths;
        q.dir = dirIndex(d);
        ps[id] = q;
    }
    vector<int> prev;
    prev.swap(ownerG);
    ownerG.assign(W * H, -1);
    trailG.assign(W * H, -1);
    cin >> tok;  // OWNER
    string row;
    for (int y = 0; y < H; y++) {
        cin >> row;
        for (int x = 0; x < W; x++) {
            if (row[x] == '.') continue;
            int k = y * W + x;
            ownerG[k] = row[x] - '0';
            if (!prev.empty()) {
                if (prev[k] == ME && ownerG[k] != ME) grudge[k] = 1;        // 被抢，记仇
                else if (ownerG[k] == ME && prev[k] != ME) grudge[k] = 0;  // 已抢回
            }
        }
    }
    cin >> tok;  // TRAIL
    for (int y = 0; y < H; y++) {
        cin >> row;
        for (int x = 0; x < W; x++) if (row[x] != '.') trailG[y * W + x] = row[x] - '0';
    }
    cin >> tok;  // END
    return true;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string tok;
    cin >> tok;  // INIT
    cin >> W >> H >> MAXT >> N >> ME >> MOVE_MS >> INIT_MS;
    spawnPos.resize(N);
    for (int i = 0; i < N; i++) {
        int id, x, y;
        cin >> tok >> id >> x >> y;
        spawnPos[id] = {x, y};
    }
    grudge.assign(W * H, 0);
    rngState ^= (uint64_t)(ME * 7919 + spawnPos[ME].first * 131 + spawnPos[ME].second * 313 + 17);
    bool wasOutside = false;
    while (readState()) {
        bool outside = ps[ME].trailLen > 0;
        if (!outside && wasOutside) plan.clear();  // 刚完成收割或刚复活
        wasOutside = outside;
        int d = decide();
        cout << TURN << ' ' << DC[d] << endl;
    }
}
