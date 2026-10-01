// 圈地对战 bot
// 策略核心：
//  1) 圈地判定是「从边界泛洪、够不到的格子全归我」，所以画一个尽量大的闭合回路就能一次吞掉整块区域。
//     本 bot 每次都规划一个「矩形回路」：矩形必须严格包住自己的领地 bbox，
//     从 bbox 的某个角出发绕矩形一周再回到该角 → 回路内部（= 整个矩形）全部变成我的领地。
//     领地因此像滚雪球一样扩张，几步之内吃掉大半个棋盘。
//  2) 分数只看名次，所以优先选「收益/步数」最大且能在剩余回合内跑完的矩形。
//  3) 对手可能反过来用一个大圈把我包住（领地会被抢走，只剩保护区），
//     所以每回合检测「某个对手这回合圈地是否会吞掉我的领地」，一旦成立立刻冲过去切断它的轨迹。
//  4) 顺手切对手：轨迹是它这轮全部积累，踩一脚就让它白跑，所以近身有机会必切。
//  5) 画圈期间被切断 = 阵亡回到出生点（领地保留），损失只是时间；计划每回合校验，非法即重排。
#include <bits/stdc++.h>
using namespace std;

static int W, H, MAXT, N, ME, MOVE_MS, INIT_MS;
static const int DX[4] = {0, 0, -1, 1};
static const int DY[4] = {-1, 1, 0, 0};
static const char DCH[4] = {'U', 'D', 'L', 'R'};
static int MEc;

static vector<signed char> own, trl;
static int spx[8], spy[8];
static int pX[8], pY[8], pD[8], pTL[8], pA[8], pDe[8];
static int TURN;

static int bfsD[4096], bfsF[4096], qb[4096];
static int lastDist;
static bool bfsOwnOnly = false;   // 只走自己的领地（画圈前的回家路径）
static int edist[4096];            // 到最近对手头部的纯网格距离
static vector<signed char> seen;

static vector<int> plan;           // 剩余执行的方向序列
static bool planActive = false;    // 计划是否已开始执行
static bool wasOutside = false;    // 本轮计划中是否已经离开过领地

static inline bool inb(int x, int y) { return x >= 0 && x < W && y >= 0 && y < H; }
static inline int dOf(char c) { return c == 'U' ? 0 : c == 'D' ? 1 : c == 'L' ? 2 : 3; }

// ---------------------------------------------------------------- BFS
// 从头部出发，禁止踩自己的轨迹，禁止第一步掉头。
// mode 0: 全程；mode 1: 找最近的对手轨迹格；mode 2: 找最近的自有领地格
static int bfsRun(int mode, int targetPlayer) {
    lastDist = -1;
    int n = W * H;
    int s = pY[ME] * W + pX[ME];
    for (int k = 0; k < n; k++) bfsD[k] = -1;
    bfsD[s] = 0;
    bfsF[s] = -1;
    int qh = 0, qt = 0;
    qb[qt++] = s;
    int back = pD[ME] ^ 1;
    int found = -1;
    while (qh < qt) {
        int k = qb[qh++];
        if (mode == 1 && trl[k] >= 0 && trl[k] != MEc) { found = k; break; }
        if (mode == 2 && own[k] == MEc) { found = k; break; }
        int x = k % W, y = k / W;
        for (int d = 0; d < 4; d++) {
            if (k == s && d == back) continue;
            int nx = x + DX[d], ny = y + DY[d];
            if (!inb(nx, ny)) continue;
            int nk = ny * W + nx;
            if (bfsD[nk] != -1) continue;
            if (trl[nk] == MEc) continue;  // 踩自己的轨迹 = 自杀
            if (bfsOwnOnly && own[nk] != MEc) continue;
            bfsD[nk] = bfsD[k] + 1;
            bfsF[nk] = (k == s ? d : bfsF[k]);
            qb[qt++] = nk;
        }
    }
    if (mode == 0) return 0;
    if (found < 0) return -1;
    lastDist = bfsD[found];
    return bfsF[found];
}

// -------------------------------------------------- 对手是否要把我围死
// 用对手的领地+轨迹当墙，从边界泛洪；泛洪不到的我的领地格 = 它这一圈会吞掉的地方。
static bool enemyEnclosesMe(int j) {
    int n = W * H;
    int ch = j;
    for (int k = 0; k < n; k++)
        seen[k] = (own[k] == ch || trl[k] == ch) ? 1 : 0;
    int qh = 0, qt = 0;
    for (int x = 0; x < W; x++) {
        int a = x, b = (H - 1) * W + x;
        if (!seen[a]) { seen[a] = 1; qb[qt++] = a; }
        if (!seen[b]) { seen[b] = 1; qb[qt++] = b; }
    }
    for (int y = 0; y < H; y++) {
        int a = y * W, b = y * W + W - 1;
        if (!seen[a]) { seen[a] = 1; qb[qt++] = a; }
        if (!seen[b]) { seen[b] = 1; qb[qt++] = b; }
    }
    while (qh < qt) {
        int k = qb[qh++];
        int x = k % W, y = k / W;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inb(nx, ny)) continue;
            int nk = ny * W + nx;
            if (seen[nk]) continue;
            seen[nk] = 1;
            qb[qt++] = nk;
        }
    }
    int hx = pX[ME], hy = pY[ME];
    if (!seen[hy * W + hx]) return true;
    for (int k = 0; k < n; k++)
        if (own[k] == MEc && !seen[k]) return true;
    return false;
}

// ---------------------------------------------------------------- 规划
// 一个「圈」= 从自己领地上某个角 s 出发，绕一个矩形一整圈，再踩回 s。
// 矩形的一条近边必须紧贴 s（否则回路接不回领地），远边任意（只要整条边都不是自己的格子）。
// 绕完一圈后回路内部（含自己领地）全部被围住 = 一次吃掉整块区域。
static int pref[64][64];      // 自己领地的前缀和，用来 O(1) 算矩形里有多少自己的格子

static void buildPlan() {
    plan.clear();
    planActive = false;
    wasOutside = false;

    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (own[y * W + x] == MEc) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    if (x1 < 0) return;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            pref[y + 1][x + 1] = pref[y][x + 1] + pref[y + 1][x] - pref[y][x] + (own[y * W + x] == MEc);

    int rem = MAXT - TURN;
    if (rem < 5) return;
    // 手上有轨迹（人在外面）时不画圈：先回家。回路要靠这条路径接回领地，
    // 路径和回路一旦撞上就是自杀，限制成只走自己的领地就永远不会撞。
    if (pTL[ME] > 0 || own[pY[ME] * W + pX[ME]] != MEc) return;

    bfsOwnOnly = true;
    bfsRun(0, 0);
    bfsOwnOnly = false;
    int head = pY[ME] * W + pX[ME];

    // 候选起点：自己领地边界上的「凸角」——某一侧相邻的两个格子都空着。
    // 先放 bbox 的四个角（向外扩收益最大），再从整条边界上均匀取样，
    // 否则只会一路往上顶， territory 长到顶以后就没圈可画了。
    int cand[4][12], nc[4] = {0, 0, 0, 0};
    for (int o = 0; o < 4; o++) {
        int ex = o & 1, ey = o >> 1;
        int k = (o == 0 ? y0 * W + x0 : o == 1 ? y0 * W + x1 : o == 2 ? y1 * W + x0 : y1 * W + x1);
        int x = k % W, y = k / W;
        int ax = ex ? x + 1 : x - 1, ay = ey ? y + 1 : y - 1;
        if (inb(ax, ay) && own[ay * W + ax] != MEc) cand[o][nc[o]++] = k;
    }
    for (int o = 0; o < 4; o++) {
        int ex = o & 1, ey = o >> 1;
        int all[400], na = 0;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                if (own[y * W + x] != MEc) continue;
                int ax = ex ? x + 1 : x - 1, ay = ey ? y + 1 : y - 1;
                if (!inb(ax, ay) || own[ay * W + ax] == MEc) continue;
                if (na < 400) all[na++] = y * W + x;
            }
        int room = 12 - nc[o];
        if (room <= 0) continue;
        if (na <= room) {
            for (int q = 0; q < na; q++) cand[o][nc[o]++] = all[q];
        } else {
            for (int q = 0; q < room; q++)
                cand[o][nc[o]++] = all[(int)((long)na * q / room)];
        }
    }

    // 开局领地小，一个大圈就能吃掉半个棋盘，这时值得押上全部剩余回合；
    // 领地大了以后再画超长圈只是白送人头，所以收紧。
    const int CAP = (N == 2) ? 140 : 60;
    const double ALPHA = 1.0;
    double bestScore = -1e18;
    int bS = -1, bEx = 0, bEy = 0, bBw = 0, bBh = 0, bStop = 0;
    const int SAMP = 9;

    for (int o = 0; o < 4; o++) {
        int ex = o & 1, ey = o >> 1;
        for (int ci = 0; ci < nc[o]; ci++) {
            int sIdx = cand[o][ci], sx = sIdx % W, sy = sIdx / W;
            if (bfsD[sIdx] < 0) continue;
            int approach = bfsD[sIdx];
            int xlo = ex ? sx + 1 : sx - 1, ylo = ey ? sy + 1 : sy - 1;
            if (!inb(xlo, sy) || !inb(sx, ylo)) continue;
            int fx0 = ex ? 0 : sx, fx1 = ex ? sx - 1 : W - 1;
            int fy0 = ey ? 0 : sy, fy1 = ey ? sy - 1 : H - 1;
            if (fx0 > fx1 || fy0 > fy1) continue;
            int dIn = ex ? 3 : 2, dA1 = ey ? 0 : 1, dH2 = ex ? 2 : 3, dUp = ey ? 1 : 0, dH5 = ex ? 3 : 2;
            for (int a = 0; a < SAMP; a++) {
                int farX = fx0 + (int)((long)(fx1 - fx0) * a / (SAMP - 1));
                int bw = abs(farX - sx);
                for (int b = 0; b < SAMP; b++) {
                    int farY = fy0 + (int)((long)(fy1 - fy0) * b / (SAMP - 1));
                    int bh = abs(farY - sy);
                    int P = 2 * bw + 2 * bh + 4;
                    int rx0 = min(xlo, farX), rx1 = max(xlo, farX);
                    int ry0 = min(ylo, farY), ry1 = max(ylo, farY);
                    int mine = pref[ry1 + 1][rx1 + 1] - pref[ry0][rx1 + 1] - pref[ry1 + 1][rx0] + pref[ry0][rx0];
                    int pc = 0;
                    for (int j = 0; j < N; j++) {
                        if (j == ME) continue;
                        int ox = min(rx1, spx[j] + 1) - max(rx0, spx[j] - 1) + 1;
                        int oy = min(ry1, spy[j] + 1) - max(ry0, spy[j] - 1) + 1;
                        if (ox > 0 && oy > 0) pc += ox * oy;
                    }
                    int gain = (rx1 - rx0 + 1) * (ry1 - ry0 + 1) - mine - pc;
                    if (gain <= 0) continue;
                    // 走一遍回路。撞到自己领地 = 这一圈到此为止（踩进去就结算，顺带把这条带子圈下来），
                    // 撞到自己的轨迹/回家路径 = 同格自杀，这个候选作废。
                    int risk = 0, kill = 0, stopT = 0, t = 0, x = sx, y = sy;
                    auto stepTo = [&](int d) {
                        x += DX[d]; y += DY[d]; t++;
                        int k = y * W + x;
                        if (own[k] == MEc) { stopT = t; return; }
                        // 会被切断 = 白跑 t 回合，代价按已投入的工作量算
                        if (edist[k] < t) risk += t;
                        if (trl[k] >= 0 && pTL[trl[k]] >= 3) kill += 2 * pTL[trl[k]] + 20;
                    };
                    stepTo(dIn);
                    for (int i = 0; i < bh && !stopT; i++) stepTo(dA1);
                    for (int i = 0; i < bw + 1 && !stopT; i++) stepTo(dH2);
                    for (int i = 0; i < bh + 1 && !stopT; i++) stepTo(dUp);
                    for (int i = 0; i < bw && !stopT; i++) stepTo(dH5);
                    int planLen = stopT ? stopT : P;
                    int total = approach + planLen;
                    if (total + 2 > rem) continue;
                    if (total > CAP) continue;
                    // 半途截断的圈只能吃到已经围住的那条带子，收益按比例打折
                    int gain2 = stopT ? (int)(gain * pow((double)stopT / (P + 1), ALPHA)) : gain;
                    if (gain2 <= 0) continue;
                    const double PWR = 0.85, KLP = 0.35, QRSK = 0.02;
                    double score = (double)gain2 / pow((double)(total + 6), PWR) - QRSK * risk + KLP * kill;
                    if (score > bestScore) {
                        bestScore = score;
                        bS = sIdx; bEx = ex; bEy = ey; bBw = bw; bBh = bh; bStop = stopT;
                    }
                }
            }
        }
    }
    if (bS < 0) return;

    // 拼出：回家路径 + 回路
    {
        vector<int> rev;
        int k = bS;
        while (k != head) {
            int d = bfsD[k], x = k % W, y = k / W, pk = -1;
            for (int dd = 0; dd < 4; dd++) {
                int nx = x + DX[dd], ny = y + DY[dd];
                if (inb(nx, ny) && bfsD[ny * W + nx] == d - 1) { pk = ny * W + nx; break; }
            }
            if (pk < 0) { plan.clear(); return; }
            rev.push_back(pk);
            k = pk;
        }
        int x = pX[ME], y = pY[ME];
        for (int i = (int)rev.size() - 1; i >= 0; i--) {
            int tx = rev[i] % W, ty = rev[i] / W;
            for (int dd = 0; dd < 4; dd++)
                if (x + DX[dd] == tx && y + DY[dd] == ty) { plan.push_back(dd); break; }
            x = tx; y = ty;
        }
    }
    int dIn = bEx ? 3 : 2, dA1 = bEy ? 0 : 1, dH2 = bEx ? 2 : 3, dUp = bEy ? 1 : 0, dH5 = bEx ? 3 : 2;
    vector<int> ring;
    ring.push_back(dIn);
    for (int i = 0; i < bBh; i++) ring.push_back(dA1);
    for (int i = 0; i < bBw + 1; i++) ring.push_back(dH2);
    for (int i = 0; i < bBh + 1; i++) ring.push_back(dUp);
    for (int i = 0; i < bBw; i++) ring.push_back(dH5);
    ring.push_back(dA1);
    int nRing = bStop ? bStop : (int)ring.size();
    for (int i = 0; i < nRing && i < (int)ring.size(); i++) plan.push_back(ring[i]);
}

// ---------------------------------------------------------------- 决策
static int decide() {
    int hx = pX[ME], hy = pY[ME], hd = pD[ME];
    int back = hd ^ 1;

    // 合法：不能掉头、不能出界、不能踩自己的轨迹
    auto legal = [&](int d, int& nx, int& ny) {
        if (d == back) return false;
        nx = hx + DX[d]; ny = hy + DY[d];
        if (!inb(nx, ny)) return false;
        if (trl[ny * W + nx] == MEc) return false;
        return true;
    };
    // 走上去会和一个贴脸的首领换位 = 同归于尽
    auto swapDie = [&](int nx, int ny) {
        for (int j = 0; j < N; j++) {
            if (j == ME) continue;
            if (pX[j] == nx && pY[j] == ny && abs(pX[j] - hx) + abs(pY[j] - hy) == 1) return true;
        }
        return false;
    };
    auto tryStep = [&](int d) {
        int nx, ny;
        return legal(d, nx, ny) && !swapDie(nx, ny);
    };
    int nx, ny;

    // 提前圈地（半路踩回自己领地）：原计划作废
    if (planActive && wasOutside && pTL[ME] == 0 && own[hy * W + hx] == MEc) {
        plan.clear();
        planActive = false;
    }

    // 紧急：某个对手这一圈下来会把我整块吞掉，只能去踩断它的轨迹
    for (int j = 0; j < N; j++) {
        if (j == ME || pTL[j] == 0) continue;
        if (!enemyEnclosesMe(j)) continue;
        int d = bfsRun(1, j);
        if (d >= 0 && tryStep(d)) {
            plan.clear();
            planActive = false;
            return d;
        }
        break;
    }

    // 切人：对手轨迹就是它这一轮全部积累，踩一脚等于让它白跑几十回合。
    // 手上有东西（正在画圈）时只有贴脸才切；空手时可以为了一口大鱼多跑几步。
    if (plan.size() <= 3 || pTL[ME] <= 1) {
        bool idle = (plan.empty() || plan.size() <= 3) && pTL[ME] <= 1;
        int maxD = idle ? 6 : 3, minTL = idle ? 4 : 6;
        for (int j = 0; j < N; j++) {
            if (j == ME || pTL[j] < minTL) continue;
            int d = bfsRun(1, j);
            if (d < 0 || lastDist > maxD) continue;
            if (tryStep(d)) { plan.clear(); planActive = false; return d; }
        }
    }

    if (plan.empty()) {
        buildPlan();
        planActive = !plan.empty();
        wasOutside = false;
    }
    if (!plan.empty()) {
        int d = plan[0];
        if (tryStep(d)) {
            plan.erase(plan.begin());
            int tx = hx + DX[d], ty = hy + DY[d];
            if (own[ty * W + tx] != MEc) wasOutside = true;
            if (plan.empty()) planActive = false;
            return d;
        }
        plan.clear();       // 计划被堵死，重排
        planActive = false;
        buildPlan();
        planActive = !plan.empty();
        wasOutside = false;
        if (!plan.empty()) {
            int d = plan[0];
            if (tryStep(d)) {
                plan.erase(plan.begin());
                int tx = hx + DX[d], ty = hy + DY[d];
                if (own[ty * W + tx] != MEc) wasOutside = true;
                if (plan.empty()) planActive = false;
                return d;
            }
            plan.clear();
            planActive = false;
        }
    }

    // 兜底：回家；在领地里就沿着领地晃（领地内移动不掉血）
    {
        int d = bfsRun(2, 0);
        if (d >= 0 && tryStep(d)) return d;
    }
    if (own[hy * W + hx] == MEc) {
        for (int d = 0; d < 4; d++)
            if (tryStep(d) && own[(hy + DY[d]) * W + hx + DX[d]] == MEc) return d;
    }
    for (int d = 0; d < 4; d++) if (tryStep(d)) return d;
    return hd;
}

static bool readState() {
    string tok;
    if (!(cin >> tok)) return false;
    if (!(cin >> TURN)) return false;
    for (int i = 0; i < N; i++) {
        int id, x, y, tl, ar, de; char dc;
        cin >> tok >> id >> x >> y >> dc >> tl >> ar >> de;
        pX[id] = x; pY[id] = y; pD[id] = dOf(dc); pTL[id] = tl; pA[id] = ar; pDe[id] = de;
    }
    cin >> tok;  // OWNER
    {
        string row;
        for (int y = 0; y < H; y++) {
            cin >> row;
            signed char* o = own.data() + y * W;
            for (int x = 0; x < W; x++) o[x] = (row[x] == '.') ? -1 : (signed char)(row[x] - '0');
        }
    }
    cin >> tok;  // TRAIL
    {
        string row;
        for (int y = 0; y < H; y++) {
            cin >> row;
            signed char* o = trl.data() + y * W;
            for (int x = 0; x < W; x++) o[x] = (row[x] == '.') ? -1 : (signed char)(row[x] - '0');
        }
    }
    cin >> tok;  // END
    return true;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string tok;
    if (!(cin >> tok)) return 0;  // INIT
    if (!(cin >> W >> H >> MAXT >> N >> ME >> MOVE_MS >> INIT_MS)) return 0;
    MEc = ME;
    for (int i = 0; i < N; i++) {
        int id, x, y;
        cin >> tok >> id >> x >> y;
        spx[id] = x; spy[id] = y;
    }
    own.assign(W * H, -1);
    trl.assign(W * H, -1);
    seen.assign(W * H, 0);

    while (readState()) {
        // 到最近对手头部的距离（纯网格 BFS，取最小）
        for (int k = 0; k < W * H; k++) edist[k] = 1 << 20;
        for (int j = 0; j < N; j++) {
            if (j == ME) continue;
            int qh = 0, qt = 0;
            int s = pY[j] * W + pX[j];
            if (edist[s] != 0) { edist[s] = 0; qb[qt++] = s; }
            while (qh < qt) {
                int k = qb[qh++];
                int x = k % W, y = k / W;
                for (int dd = 0; dd < 4; dd++) {
                    int ax = x + DX[dd], ay = y + DY[dd];
                    if (!inb(ax, ay)) continue;
                    int nk = ay * W + ax;
                    if (edist[nk] != 0) continue;
                    edist[nk] = edist[k] + 1;
                    qb[qt++] = nk;
                }
            }
        }
        int d = decide();
        cout << TURN << ' ' << DCH[d] << endl;
    }
    return 0;
}
