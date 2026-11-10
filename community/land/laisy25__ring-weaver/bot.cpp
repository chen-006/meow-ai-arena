// 圈地 bot —— 短环稳定扩张 + 主动切线
//
// 规则要点（决定了策略）：
//   * 圈地的填充是"从棋盘边界向内泛洪，泛洪到不了的非墙格全部归自己"，
//     所以一条贴边走满一圈、最后回到自己领地的闭环能一次吞下整块棋盘。
//   * 但轨迹在收口前一直暴露：任何对手踩到我的轨迹，我就阵亡、轨迹清空（领地保留，
//     这一趟白干）。轨迹越长、拖得越久，越躲不开对手 —— 实测整盘大环必被踩断。
//     所以主线是"短环"：轨迹只有几十步、紧贴自己的领地，几回合就收口。
//   * 反过来，踩到对手的轨迹会让对手阵亡、轨迹清空。对手一旦拖出长轨迹（想吞整盘），
//     立刻去切断它，一次能让他白干上百回合 —— 对付强手比自己去圈地更值钱。
//
// 每回合用内置规则模拟器评估候选路线（U 形绕行 / 贴边环 / 追杀），
// 打分 = 收益 × 存活概率 + 击杀收益 - 长度代价。执行中一旦对手逼近我的轨迹，
// 立刻改道回家收口，把已经围住的区域先吃下来。
//
// 1v1 和 4 人混战用两套参数（MELEE_* 宏）：混战里 3 个对手同时能切线，
// 环要更短（MELEE_MAXPATH），风险按 1/(N-1) 分摊（MELEE_AGGR），
// 而且起环的起点要先沿自家领地走到最近的边界格（FRONT_N）——
// 头部落在领地内部时直接从头部起环，出腿和回腿都在自己地里，等于白走。
//
// 踩过的坑：依赖 N（人数）的常量不能写成 static const，
// N 是运行时从 stdin 读的，静态初始化阶段它还是 0。

#include <bits/stdc++.h>
using namespace std;

static const int MAXP = 4;
static const int MAXC = 64 * 64;
static const int DX[4] = {0, 0, -1, 1};
static const int DY[4] = {-1, 1, 0, 0};
static const char DC[4] = {'U', 'D', 'L', 'R'};

int W, H, TURNS, N, ME;
int SPX[MAXP], SPY[MAXP];
int PX[MAXP], PY[MAXP], PD[MAXP], PLEN[MAXP], PAREA[MAXP], PDEATH[MAXP];

int OWN[MAXC], TRL[MAXC], PROT[MAXC];
int DTH[MAXC];  // 每格到自己领地的最短距离（BFS）

#ifdef DBG
static int ABA = 0, ABB = 0, ABB_REST = 0, ABD = 0, PLN = 0;
static int KAA = 0, KAB = 0, KAC = 0;
#endif

static void buildHomeDist() {
    for (int k = 0; k < W * H; k++) DTH[k] = (OWN[k] == ME) ? 0 : (1 << 20);
    static int q[MAXC];
    int qh = 0, qt = 0;
    for (int k = 0; k < W * H; k++) if (DTH[k] == 0) q[qt++] = k;
    while (qh < qt) {
        int k = q[qh++], x = k % W;
        if (k >= W && DTH[k - W] > DTH[k] + 1) { DTH[k - W] = DTH[k] + 1; q[qt++] = k - W; }
        if (k < W * (H - 1) && DTH[k + W] > DTH[k] + 1) { DTH[k + W] = DTH[k] + 1; q[qt++] = k + W; }
        if (x > 0 && DTH[k - 1] > DTH[k] + 1) { DTH[k - 1] = DTH[k] + 1; q[qt++] = k - 1; }
        if (x < W - 1 && DTH[k + 1] > DTH[k] + 1) { DTH[k + 1] = DTH[k] + 1; q[qt++] = k + 1; }
    }
}

static bool inb(int x, int y) { return x >= 0 && x < W && y >= 0 && y < H; }

// ---------------------------------------------------------------- 模拟器
struct Sim {
    int own[MAXC], trl[MAXC];
    int x, y, d, len;
    int gain, kills, egain;
    bool dead;
};

static void simCapture(Sim& S) {
    int got = 0;
    for (int k = 0; k < W * H; k++)
        if (S.trl[k] == ME) {
            S.trl[k] = -1;
            if (S.own[k] != ME) {
                got++;
                if (S.own[k] >= 0) S.egain++;
            }
            S.own[k] = ME;
        }
    S.len = 0;
    S.gain += got;

    static int q[MAXC];
    static char vis[MAXC];
    memset(vis, 0, W * H);
    int qh = 0, qt = 0;
    auto wall = [&](int k) { return S.own[k] == ME; };
    for (int x = 0; x < W; x++) {
        int a = x, b = (H - 1) * W + x;
        if (!wall(a) && !vis[a]) { vis[a] = 1; q[qt++] = a; }
        if (!wall(b) && !vis[b]) { vis[b] = 1; q[qt++] = b; }
    }
    for (int y = 0; y < H; y++) {
        int a = y * W, b = y * W + W - 1;
        if (!wall(a) && !vis[a]) { vis[a] = 1; q[qt++] = a; }
        if (!wall(b) && !vis[b]) { vis[b] = 1; q[qt++] = b; }
    }
    while (qh < qt) {
        int k = q[qh++], x = k % W;
        if (k >= W && !wall(k - W) && !vis[k - W]) { vis[k - W] = 1; q[qt++] = k - W; }
        if (k < W * (H - 1) && !wall(k + W) && !vis[k + W]) { vis[k + W] = 1; q[qt++] = k + W; }
        if (x > 0 && !wall(k - 1) && !vis[k - 1]) { vis[k - 1] = 1; q[qt++] = k - 1; }
        if (x < W - 1 && !wall(k + 1) && !vis[k + 1]) { vis[k + 1] = 1; q[qt++] = k + 1; }
    }
    for (int k = 0; k < W * H; k++) {
        if (wall(k) || vis[k]) continue;
        if (PROT[k] != -1 && PROT[k] != ME) continue;
        if (S.own[k] >= 0) S.egain++;
        S.own[k] = ME;
        S.gain++;
    }
}

static bool simStep(Sim& S, int nd) {
    if (nd < 0 || nd > 3) return true;
    if (nd == (S.d ^ 1)) nd = S.d;
    int nx = S.x + DX[nd], ny = S.y + DY[nd];
    if (!inb(nx, ny)) { S.dead = true; return false; }
    int t = ny * W + nx;
    for (int j = 0; j < N; j++)
        if (j != ME && PX[j] == nx && PY[j] == ny) { S.dead = true; return false; }
    if (S.trl[t] == ME) { S.dead = true; return false; }
    int c = S.trl[t];
    if (c >= 0 && c != ME) {
        for (int k = 0; k < W * H; k++)
            if (S.trl[k] == c) S.trl[k] = -1;
        S.kills++;
    }
    S.x = nx; S.y = ny; S.d = nd;
    if (S.own[t] == ME) {
        if (S.len > 0) simCapture(S);
    } else {
        S.trl[t] = ME;
        S.len++;
    }
    return true;
}

static bool simPath(Sim& S, const vector<int>& path) {
    for (int k : path) {
        int x = k % W, y = k / W;
        int nd = -1;
        for (int d = 0; d < 4; d++)
            if (S.x + DX[d] == x && S.y + DY[d] == y) { nd = d; break; }
        if (nd < 0) return false;
        if (!simStep(S, nd)) return false;
    }
    return true;
}

// ---------------------------------------------------------------- 路径
// 从 (S.x,S.y) 回到自己领地的最短路
static vector<int> bfsHome(const Sim& S) {
    static int prev[MAXC];
    static char vis[MAXC];
    static int q[MAXC];
    memset(vis, 0, W * H);
    int qh = 0, qt = 0;
    int start = S.y * W + S.x;
    vis[start] = 1;
    prev[start] = -1;
    q[qt++] = start;
    int goal = -1;
    while (qh < qt) {
        int k = q[qh++];
        if (S.own[k] == ME && k != start) { goal = k; break; }
        int x = k % W;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = k / W + DY[d];
            if (!inb(nx, ny)) continue;
            int nk = ny * W + nx;
            if (vis[nk] || S.trl[nk] == ME) continue;
            bool bad = false;
            for (int j = 0; j < N; j++)
                if (j != ME && PX[j] == nx && PY[j] == ny) { bad = true; break; }
            if (bad) continue;
            vis[nk] = 1;
            prev[nk] = k;
            q[qt++] = nk;
        }
    }
    vector<int> path;
    if (goal < 0) return path;
    for (int k = goal; k != start && k >= 0; k = prev[k]) path.push_back(k);
    reverse(path.begin(), path.end());
    return path;
}

// 到"任意满足 goal 的格子"的最短路
template <class F>
static vector<int> bfsTo(int sx, int sy, F goal, bool avoidMyTrail) {
    static int prev[MAXC];
    static char vis[MAXC];
    static int q[MAXC];
    memset(vis, 0, W * H);
    int qh = 0, qt = 0;
    int start = sy * W + sx;
    vis[start] = 1;
    prev[start] = -1;
    q[qt++] = start;
    int found = -1;
    while (qh < qt) {
        int k = q[qh++];
        if (k != start && goal(k)) { found = k; break; }
        int x = k % W;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = k / W + DY[d];
            if (!inb(nx, ny)) continue;
            int nk = ny * W + nx;
            if (vis[nk]) continue;
            if (avoidMyTrail && TRL[nk] == ME) continue;
            vis[nk] = 1;
            prev[nk] = k;
            q[qt++] = nk;
        }
    }
    vector<int> path;
    if (found < 0) return path;
    for (int k = found; k != start && k >= 0; k = prev[k]) path.push_back(k);
    reverse(path.begin(), path.end());
    return path;
}


// 从 (sx,sy) 沿自家领地走到 (tx,ty)（只走 OWN==ME，避开对手头部），返回路径格
static vector<int> bfsOwn(int sx, int sy, int tx, int ty) {
    static int prev[MAXC];
    static char vis[MAXC];
    static int q[MAXC];
    memset(vis, 0, W * H);
    int qh = 0, qt = 0;
    int start = sy * W + sx, goal = ty * W + tx;
    vis[start] = 1;
    prev[start] = -1;
    q[qt++] = start;
    while (qh < qt) {
        int k = q[qh++];
        if (k == goal) break;
        int x = k % W;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = k / W + DY[d];
            if (!inb(nx, ny)) continue;
            int nk = ny * W + nx;
            if (vis[nk] || OWN[nk] != ME) continue;
            bool bad = false;
            for (int j = 0; j < N; j++)
                if (j != ME && PX[j] == nx && PY[j] == ny) { bad = true; break; }
            if (bad) continue;
            vis[nk] = 1;
            prev[nk] = k;
            q[qt++] = nk;
        }
    }
    vector<int> path;
    if (!vis[goal]) return path;
    for (int k = goal; k != start && k >= 0; k = prev[k]) path.push_back(k);
    reverse(path.begin(), path.end());
    return path;
}

static vector<int> straight(int x, int y, int d, int steps) {
    vector<int> v;
    for (int i = 0; i < steps; i++) {
        x += DX[d];
        y += DY[d];
        if (!inb(x, y)) break;
        v.push_back(y * W + x);
    }
    return v;
}

static int manh(int j, int cell) {
    return abs(PX[j] - cell % W) + abs(PY[j] - cell / W);
}

// ---------------------------------------------------------------- 候选
struct Cand {
    vector<int> path;
    double score = -1e18;
    int gain = 0, len = 0, kills = 0, egain = 0;
    bool valid = false;
};

static Sim baseSim() {
    Sim S;
    memcpy(S.own, OWN, sizeof(int) * W * H);
    memcpy(S.trl, TRL, sizeof(int) * W * H);
    S.x = PX[ME]; S.y = PY[ME]; S.d = PD[ME];
    S.len = PLEN[ME];
    S.gain = 0; S.kills = 0; S.egain = 0; S.dead = false;
    return S;
}

#ifndef AGGR_V
#define AGGR_V 0.70
#endif
#ifndef HK_V
#define HK_V 0.90
#endif
#ifndef DK_V
#define DK_V 0.50
#endif
#ifndef BUSY_V
#define BUSY_V 0.50
#endif
#ifndef LEN_W_V
#define LEN_W_V 0.45
#endif
#ifndef KILL_W_V
#define KILL_W_V 55.0
#endif
#ifndef CHASE_K_V
#define CHASE_K_V 2.6
#endif
#ifndef CHASE_B_V
#define CHASE_B_V 70.0
#endif
#ifndef ANCHOR_N_V
#define ANCHOR_N_V 0
#endif
#ifndef AGG_MODE_V
#define AGG_MODE_V 0.0
#endif
#ifndef MELEE_LEN_K_V
#define MELEE_LEN_K_V 1.0
#endif
#ifndef MELEE_AGGR_V
#define MELEE_AGGR_V 3.0
#endif
#ifndef SAFE_M_V
#define SAFE_M_V 8.0
#endif
#ifndef SAFE_S_V
#define SAFE_S_V 12.0
#endif
#ifndef MAXLEN_V
#define MAXLEN_V 110
#endif
#ifndef BAIL_MAX_V
#define BAIL_MAX_V 14
#endif
// 一条路线的总长上限。实测强手每个环只有 20~40 步（收口快 = 不被踩断），
// 而"一次吞一大块"的长环对任何会动的对手都必断。
#ifndef MAXPATH_V
#define MAXPATH_V 999
#endif
// 风险模型：0 = 只看轨迹长度和最近距离；1 = 逐格推演对手能否赶在收口前踩到
// （实测 1 反而更差：见小结 NOTES；默认用 0）
#ifndef RISK_MODE_V
#define RISK_MODE_V 0.0
#endif
// 逐格模型的"来得及"判据：余量/IC_K 折算成危险度
#ifndef IC_K_V
#define IC_K_V 6.0
#endif
// 混战专属：风险模型、收口阈值的倍率（4 人局和 1v1 的战术差别很大）
#ifndef MELEE_RISK_MODE_V
#define MELEE_RISK_MODE_V 0.0
#endif
#ifndef MELEE_BAIL_K_V
#define MELEE_BAIL_K_V 0.01
#endif
// 混战专属的路线长度上限（4 人局里第三方随时能切线，长环更容易白干）
#ifndef MELEE_MAXPATH_V
#define MELEE_MAXPATH_V 36
#endif
// 扩张方向偏好：往远离最近对手的方向走（每远 1 格加这么多分）
#ifndef AWAY_V
#define AWAY_V 0.0
#endif
// 起环的起点：除了头部，再取最近的几个自家边界格（在自家领地上走过去不留轨迹）
#ifndef FRONT_N_V
#define FRONT_N_V 3
#endif
#ifndef FRONT_SEP_V
#define FRONT_SEP_V 5
#endif
#ifndef FRONT_MAXP_V
#define FRONT_MAXP_V 14
#endif
// 混战评分换成"每回合收益"（吞吐）而不是加性收益：实测明显更差（面积 240 vs 484），
// 留着开关但默认关闭 —— 加性形式里 LEN_W 其实已经隐含了吞吐的权衡。
#ifndef MELEE_RATIO_V
#define MELEE_RATIO_V 0.0
#endif

static const double AGGR = AGGR_V;
static const double HK = HK_V;
static const double DK = DK_V;
static const double BUSYF = BUSY_V;
static const int MAXLEN = MAXLEN_V;
static const double AGGR_MELEE_K = MELEE_AGGR_V;
static const double MELEE_LEN_K = MELEE_LEN_K_V;
static const double AGG_MODE = AGG_MODE_V;
static const int ANCHOR_N = ANCHOR_N_V;
static const double CHASE_K = CHASE_K_V;
static const double CHASE_B = CHASE_B_V;
static const double SAFE_M = SAFE_M_V;
static const double SAFE_S = SAFE_S_V;    // 对手来切线的倾向
static const int MAXPATH = MAXPATH_V;     // 一条路线的总长上限
static const double RISK_MODE = RISK_MODE_V;
static const double IC_K = IC_K_V;
static const double LEN_W = LEN_W_V;  // 每步轨迹的机会成本
static const double KILL_W = KILL_W_V; // 击杀基础价值
static const double AWAY = AWAY_V;     // 远离最近对手的权重
static const int FRONT_N = FRONT_N_V;  // 额外起点个数
static const int FRONT_SEP = FRONT_SEP_V;
static const int FRONT_MAXP = FRONT_MAXP_V;

// 注意：N（人数）是运行时从 stdin 读进来的，任何依赖 N 的常量都必须是函数，
// 不能写成 static const —— 那会在静态初始化阶段（N 还是 0）就把值算死。
// 曾经就是这样：AGGR_eff 被算成 0.7*1.0/(0-1) = -0.7，导致混战里越危险的路越被加分。
static inline double maxLenEff() { return (N > 2) ? MAXLEN * MELEE_LEN_K : MAXLEN; }
static inline double aggrEff() { return (N > 2) ? AGGR * AGGR_MELEE_K / (N - 1) : AGGR; }
static inline double riskModeEff() { return (N > 2) ? MELEE_RISK_MODE_V : RISK_MODE; }
static inline int bailEff() { return (N > 2) ? (int)(BAIL_MAX_V * MELEE_BAIL_K_V) : BAIL_MAX_V; }
static inline int maxPathEff() { return (N > 2) ? (int)MELEE_MAXPATH_V : MAXPATH; }
// 从最近的边界格起环：混战里主角（头部在领地内部时这招很关键），单挑里实测没用
static inline int frontEff() { return (N > 2) ? FRONT_N : 0; }
static inline double ratioEff() { return (N > 2) ? MELEE_RATIO_V : 0.0; }

static double survivalOf(const vector<int>& trail, int len) {
    double survive = 1.0;
    double worst = 0.0;
    const double rmode = riskModeEff();
    const double ag = aggrEff();
    const double maxlen = maxLenEff();
    for (int j = 0; j < N; j++) {
        if (j == ME) continue;
        int best = 1 << 20;
        double q = 0;
        if (rmode > 0.5) {
            // 逐格检查"他能不能在我收口前踩到这一格"：
            // 第 i 步留下的轨迹会一直留到第 len 步；他要 d 步走过来，
            // 那么在 max(i+1, d) 时刻他就能踩到 —— 早于 len 就是死。
            int margin = 1 << 20;
            for (int i = 0; i < (int)trail.size() && i < len; i++) {
                int d = manh(j, trail[i]);
                if (d < best) best = d;
                int arrive = (i + 1 > d) ? i + 1 : d;
                int m = arrive - len;
                if (m < margin) margin = m;
            }
            if (best > (1 << 19)) continue;
            double s = (double)margin / IC_K;   // margin<=0 表示他来得及
            if (s < 0) s = 0;
            if (s > 1) s = 1;
            q = 1.0 - s;
        } else {
            for (int k : trail) {
                int d = manh(j, k);
                if (d < best) best = d;
            }
            if (best > (1 << 19)) continue;
            q = (HK * len - best) / (DK * len + 1.0);
            if (q < 0) q = 0;
            if (q > 1) q = 1;
            // 轨迹一长，空闲的对手总能在收口前踩到它：实测这种长环几乎必断
            if (len > maxlen && best < len) survive *= 0.02;
        }
        survive *= (1.0 - ag * q);
        if (q > worst) worst = q;
    }
    if (N > 2 && AGG_MODE > 0.5) survive = 1.0 - ag * worst;
    return survive;
}

static Cand evalShape(Sim S, const vector<int>& shape, int remain) {
    Cand c;
    if ((int)shape.size() + 1 > remain) return c;
    if (!simPath(S, shape)) return c;
    vector<int> home = bfsHome(S);
    if (home.empty()) return c;
    if (!simPath(S, home)) return c;
    if (S.dead) return c;
    c.path = shape;
    for (int k : home) c.path.push_back(k);
    c.gain = S.gain;
    c.egain = S.egain;
    c.len = (int)c.path.size();
    c.kills = S.kills;
    // 上限只看"真正会留下轨迹的格子"：在自家领地上走的那段（pre）不留轨迹，
    // 只是花几回合，不该挤掉环本身的空间。
    {
        int tl = 0;
        for (int k : shape)
            if (OWN[k] != ME) tl++;
        if (tl > maxPathEff()) return c;
    }
    double sv = survivalOf(shape, c.len);
    // 吃掉对手的领地是双倍价值：自己 +1，对手 -1
    if (ratioEff() > 0.5) {
        // 吞吐：围到的格子 / 花掉的回合
        c.score = (c.gain + c.egain) * sv / (c.len + 6.0) * 10.0 + KILL_W * c.kills;
    } else {
        c.score = (c.gain + c.egain) * sv - LEN_W * c.len + KILL_W * c.kills;
    }
    // 往"远离最近对手"的方向扩张：4 人局里这决定了是吃自家那一角还是去中间挨刀
    if (AWAY > 0 && !shape.empty()) {
        int tip = shape.back();
        int dnear = 1 << 20, dtip = 1 << 20;
        for (int j = 0; j < N; j++) {
            if (j == ME) continue;
            int a = manh(j, tip), b = manh(j, PY[ME] * W + PX[ME]);
            if (a < dtip) dtip = a;
            if (b < dnear) dnear = b;
        }
        if (dtip < (1 << 19)) c.score += AWAY * (dtip - dnear);
    }
    c.valid = true;
    return c;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    string tok;
    if (!(cin >> tok)) return 0;
    cin >> W >> H >> TURNS >> N >> ME >> tok >> tok;
    for (int i = 0; i < N; i++) {
        int id, x, y;
        cin >> tok >> id >> x >> y;
        SPX[id] = x; SPY[id] = y;
    }
    memset(PROT, -1, sizeof(PROT));
    for (int i = 0; i < N; i++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (inb(SPX[i] + dx, SPY[i] + dy)) PROT[(SPY[i] + dy) * W + SPX[i] + dx] = i;

    vector<int> cyc;
    for (int x = 0; x < W; x++) cyc.push_back(x);
    for (int y = 1; y < H; y++) cyc.push_back(y * W + W - 1);
    for (int x = W - 2; x >= 0; x--) cyc.push_back((H - 1) * W + x);
    for (int y = H - 2; y >= 1; y--) cyc.push_back(y * W);
    int L = (int)cyc.size();
    vector<int> cycIdx(W * H, -1);
    for (int i = 0; i < L; i++) cycIdx[cyc[i]] = i;

    vector<int> route;
    size_t ri = 0;
    int planKind = 0;  // 0 扩张, 2 追杀

    while (true) {
        if (!(cin >> tok)) break;
        int turn;
        cin >> turn;
        bool bad = false;
        for (int i = 0; i < N; i++) {
            int id; char dc;
            if (!(cin >> tok >> id >> PX[id] >> PY[id] >> dc >> PLEN[id] >> PAREA[id] >> PDEATH[id])) { bad = true; break; }
            PD[id] = string("UDLR").find(dc);
        }
        if (bad) return 0;
        cin >> tok;
        for (int y = 0; y < H; y++) {
            string row;
            cin >> row;
            if ((int)row.size() < W) return 0;
            for (int x = 0; x < W; x++) {
                char ch = row[x];
                OWN[y * W + x] = (ch == '.') ? -1 : ch - '0';
            }
        }
        cin >> tok;
        for (int y = 0; y < H; y++) {
            string row;
            cin >> row;
            if ((int)row.size() < W) return 0;
            for (int x = 0; x < W; x++) {
                char ch = row[x];
                TRL[y * W + x] = (ch == '.') ? -1 : ch - '0';
            }
        }
        cin >> tok;

        int remain = TURNS - turn;

        // 路线是否还能走
        bool keep = false;
        if (!route.empty() && ri < route.size()) {
            int c = route[ri];
            int cx = c % W, cy = c / W;
            bool adj = (abs(cx - PX[ME]) + abs(cy - PY[ME]) == 1);
            if (adj && TRL[cy * W + cx] != ME) {
                keep = true;
                for (int j = 0; j < N; j++)
                    if (j != ME && PX[j] == cx && PY[j] == cy) keep = false;
#ifdef DBG
                if (!keep) KAC++;   // 下一格被对手头挡住
            } else if (adj) {
                KAB++;              // 下一格是自己刚画下的轨迹（不该发生）
            } else {
                KAA++;              // 头部已经偏离路线（被兜底走法带偏）
#endif
            }
        }
        if (!keep) { route.clear(); ri = 0; planKind = 0;
#ifdef DBG
            ABA++;
#endif
        }

        // 追杀目标已消失（对手被切了）则重新规划
        if (keep && planKind == 2 && TRL[route.back()] < 0) { route.clear(); ri = 0; planKind = 0; }

        // 危险时立刻收口：对手能在我走完剩余路线前摸到轨迹，就先回家把已有区域吃下来
        if (keep && planKind != 2 && PLEN[ME] > 4) {
            int rest = (int)route.size() - (int)ri;
            int near = 1 << 20;
            for (int j = 0; j < N; j++) {
                if (j == ME) continue;
                for (int k = 0; k < W * H; k++) {
                    if (TRL[k] != ME) continue;
                    int d = manh(j, k);
                    if (d < near) near = d;
                }
            }
            if (near + 1 < rest && near <= bailEff()) {
                Sim S = baseSim();
                vector<int> home = bfsHome(S);
                if (!home.empty() && simPath(S, home) && !S.dead) {
                    route = home;
                    ri = 0;
                    planKind = 1;
#ifdef DBG
                    ABB++; ABB_REST += rest;
#endif
                } else { route.clear(); ri = 0; planKind = 0; }
            }
        }

        if (route.empty()) {
            buildHomeDist();
            Sim S0 = baseSim();
            Cand best;
            int bestKind = 0;

#ifdef DBG
            int vn = 0, mg = 0, mgl = 0;
#endif
            auto consider = [&](const vector<int>& shape) {
                Cand c = evalShape(S0, shape, remain);
#ifdef DBG
                if (c.valid) { vn++; if (c.gain > mg) { mg = c.gain; mgl = c.len; } }
#endif
                if (c.valid && c.score > best.score) { best = c; bestKind = 0; }
            };

            // A) 追杀：切断对手的长轨迹
            for (int j = 0; j < N; j++) {
                if (j == ME || PLEN[j] < 10) continue;
                int jj = j;
                vector<int> path = bfsTo(PX[ME], PY[ME], [&](int k) { return TRL[k] == jj; }, true);
                if (path.empty() || (int)path.size() >= remain) continue;
                int myd = (int)path.size();
                vector<int> theirHome = bfsTo(PX[j], PY[j],
                    [&](int k) { return OWN[k] == jj && !(k == PY[j] * W + PX[j]); }, false);
                int th = theirHome.empty() ? 999 : (int)theirHome.size();
                if (myd > th + 4) continue;  // 赶不上
                Sim S = S0;
                if (!simPath(S, path)) continue;
                // 切完还要能回家收口：否则这一趟轨迹就晾在外面，白送对手一次机会
                vector<int> home = bfsHome(S);
                if (home.empty()) continue;
                if (!simPath(S, home)) continue;
                if (S.dead) continue;
                Cand c;
                c.path = path;
                for (int k : home) c.path.push_back(k);
                c.len = (int)c.path.size();
                c.kills = 1;
                c.gain = S.gain;
                c.egain = S.egain;
                // 对手轨迹越长，说明他正在憋一个大圈，切断它的价值越高
                c.score = CHASE_K * PLEN[j] + CHASE_B + (PLEN[j] > 25 ? 120.0 : 0.0)
                          - LEN_W * c.len + (c.gain + c.egain);
                c.valid = true;
                if (c.score > best.score) { best = c; bestKind = 2; }
            }

            // B) 扩张：U 形绕行（主要手段）
            //    起点不一定是头部：可以先沿自家领地"免费"走到更有利的边界点再出发
            //    （在自家领地上走不留轨迹，安全），这样被围住时也能换个方向切进去。
            struct Start { vector<int> pre; int cell; };
            vector<Start> starts;
            starts.push_back(Start{{}, PY[ME] * W + PX[ME]});
            {
                // 从头部沿自家领地 BFS，取最近的几个"边界格"（相邻有非自家格）当起点。
                // 头部一旦落在领地内部，直接从头起环等于白走：出腿和回腿都在自己的地里，
                // 什么都没围住（实测混战里每环只赚 14~27 格，就是这个原因）。
                static char visc[MAXC];
                static int qc[MAXC];
                memset(visc, 0, W * H);
                int start = PY[ME] * W + PX[ME];
                int qh = 0, qt = 0;
                visc[start] = 1;
                qc[qt++] = start;
                vector<int> chosen;
                while (qh < qt && (int)chosen.size() < frontEff()) {
                    int k = qc[qh++];
                    int x = k % W;
                    bool edge = false;
                    if (k >= W && OWN[k - W] != ME) edge = true;
                    if (k < W * (H - 1) && OWN[k + W] != ME) edge = true;
                    if (x > 0 && OWN[k - 1] != ME) edge = true;
                    if (x < W - 1 && OWN[k + 1] != ME) edge = true;
                    if (edge && k != start) {
                        bool far = true;
                        for (int c : chosen)
                            if (abs(c % W - x) + abs(c / W - k / W) < FRONT_SEP) far = false;
                        // 前缀太长就没意义了（这段时间里对手能动很多步）
                        if (far && (int)(abs(k % W - PX[ME]) + abs(k / W - PY[ME])) <= FRONT_MAXP)
                            chosen.push_back(k);
                    }
                    for (int d = 0; d < 4; d++) {
                        int nx = x + DX[d], ny = k / W + DY[d];
                        if (!inb(nx, ny)) continue;
                        int nk = ny * W + nx;
                        if (visc[nk] || OWN[nk] != ME) continue;
                        visc[nk] = 1;
                        qc[qt++] = nk;
                    }
                }
                for (int k : chosen) {
                    vector<int> pre = bfsOwn(PX[ME], PY[ME], k % W, k / W);
                    if (!pre.empty() && (int)pre.size() + 4 <= remain)
                        starts.push_back(Start{pre, k});
                }
            }

            static const int AS[] = {3, 4, 6, 8, 10, 13, 16, 20, 25, 30};
            static const int BS[] = {2, 3, 5, 7, 9, 12, 15, 19, 24};
            static const int AS2[] = {3, 4, 6, 8, 10, 13};
            static const int BS2[] = {3, 5, 7, 9, 12};
            for (size_t si = 0; si < starts.size(); si++) {
                const vector<int>& pre = starts[si].pre;
                int cell = starts[si].cell;
                int sx = cell % W, sy = cell / W;
                bool isHead = (si == 0);
                // 起点处选最"朝外"的方向（DTH 越大越远离自家领地）
                int ds[4] = {0, 1, 2, 3};
                int dv[4];
                for (int d = 0; d < 4; d++) {
                    int nx = sx + DX[d], ny = sy + DY[d];
                    dv[d] = inb(nx, ny) ? DTH[ny * W + nx] : -1;
                }
                for (int i = 0; i < 4; i++)
                    for (int j = i + 1; j < 4; j++)
                        if (dv[ds[j]] > dv[ds[i]]) swap(ds[i], ds[j]);
                int nd = isHead ? 4 : 2;
                for (int di = 0; di < nd; di++) {
                    int d = ds[di];
                    for (int tw = 0; tw < 2; tw++) {
                        int nA = isHead ? 10 : 6, nB = isHead ? 9 : 5;
                        for (int ai = 0; ai < nA; ai++) {
                            int a = isHead ? AS[ai] : AS2[ai];
                            if (2 * a + 1 + (int)pre.size() > remain) continue;
                            vector<int> s1 = straight(sx, sy, d, a);
                            if ((int)s1.size() < a) continue;
                            for (int bi = 0; bi < nB; bi++) {
                                int b = isHead ? BS[bi] : BS2[bi];
                                if (2 * a + b + 2 + (int)pre.size() > remain) continue;
                                int x1 = s1.back() % W, y1 = s1.back() / W;
                                int r = (d + (tw ? 3 : 1)) % 4;
                                vector<int> s2 = straight(x1, y1, r, b);
                                if ((int)s2.size() < b) continue;
                                int x2 = s2.back() % W, y2 = s2.back() / W;
                                vector<int> s3 = straight(x2, y2, d ^ 1, a + 1);
                                vector<int> shape = pre;
                                for (int k : s1) shape.push_back(k);
                                for (int k : s2) shape.push_back(k);
                                for (int k : s3) shape.push_back(k);
                                consider(shape);
                            }
                        }
                    }
                }
            }

            // C) 贴边环（对手远时才可能被选中）
            for (int side = 0; side < 4; side++) {
                int d = side;
                int dist = 0;
                if (d == 0) dist = PY[ME];
                else if (d == 1) dist = H - 1 - PY[ME];
                else if (d == 2) dist = PX[ME];
                else dist = W - 1 - PX[ME];
                if (dist > remain) continue;
                vector<int> corr;
                int entry;
                if (dist == 0) entry = PY[ME] * W + PX[ME];
                else {
                    corr = straight(PX[ME], PY[ME], d, dist);
                    if ((int)corr.size() != dist) continue;
                    entry = corr.back();
                }
                int ei = cycIdx[entry];
                if (ei < 0) continue;
                for (int dir2 = 0; dir2 < 2; dir2++) {
                    for (int frac = 4; frac >= 1; frac--) {
                        int cnt = L * frac / 4;
                        if (cnt >= L) cnt = L - 1;
                        vector<int> shape = corr;
                        for (int i = 1; i <= cnt; i++) {
                            int idx = dir2 == 0 ? (ei + i) % L : (ei - i + L * 2) % L;
                            shape.push_back(cyc[idx]);
                        }
                        consider(shape);
                    }
                }
            }

            if (best.valid) {
                route = best.path;
                ri = 0;
                planKind = bestKind;
#ifdef DBG
                PLN++;
#endif
#ifdef DBG
                cerr << "T " << turn << " kind=" << bestKind << " gain=" << best.gain
                     << " eg=" << best.egain
                     << " len=" << best.len << " sc=" << (int)best.score
                     << " area=" << PAREA[ME] << " opp=" << PAREA[1 - ME]
                     << " dth=" << PDEATH[ME]
                     << " vn=" << vn << " mg=" << mg << "/" << mgl
                     << " | P" << PLN << " D" << ABD << " K" << ABA << " B" << ABB
                     << "/" << ABB_REST << " aa" << KAA << " ab" << KAB << " ac" << KAC << endl;
#endif
            }
        }

        int move = -1;
        if (!route.empty() && ri < route.size()) {
            int c = route[ri];
            int cx = c % W, cy = c / W;
            int nd = -1;
            for (int d = 0; d < 4; d++)
                if (PX[ME] + DX[d] == cx && PY[ME] + DY[d] == cy) { nd = d; break; }
            bool safe = nd >= 0 && nd != (PD[ME] ^ 1) && TRL[cy * W + cx] != ME;
            for (int j = 0; j < N && safe; j++)
                if (j != ME && PX[j] == cx && PY[j] == cy) safe = false;
            if (safe) {
                move = nd;
                ri++;
#ifdef DBG
                if (ri == route.size()) ABD++;
#endif
            }
        }
        // 计划里的下一格被对手的头占了：绕到"仍与它相邻"的格子继续走，
        // 不要因为一格被挡就把整条路线扔掉（那等于把已经画了一半的轨迹白送）。
        if (move < 0 && !route.empty() && ri < route.size()) {
            int c = route[ri];
            int cx = c % W, cy = c / W;
            for (int d = 0; d < 4; d++) {
                if (d == (PD[ME] ^ 1)) continue;
                int nx = PX[ME] + DX[d], ny = PY[ME] + DY[d];
                if (!inb(nx, ny)) continue;
                if (abs(nx - cx) + abs(ny - cy) != 1) continue;
                int t = ny * W + nx;
                if (TRL[t] == ME) continue;
                bool occ = false;
                for (int j = 0; j < N; j++)
                    if (j != ME && PX[j] == nx && PY[j] == ny) occ = true;
                if (occ) continue;
                move = d;
                break;
            }
        }
        if (move < 0) {
            int bestd = -1;
            double bestv = -1e18;
            for (int d = 0; d < 4; d++) {
                if (d == (PD[ME] ^ 1)) continue;
                int nx = PX[ME] + DX[d], ny = PY[ME] + DY[d];
                if (!inb(nx, ny)) continue;
                int t = ny * W + nx;
                if (TRL[t] == ME) continue;
                bool occ = false;
                for (int j = 0; j < N; j++)
                    if (j != ME && PX[j] == nx && PY[j] == ny) occ = true;
                double v = 0;
                if (occ) v -= 1000;
                if (OWN[t] == ME) v += 6;
                for (int s = 1; s <= 8; s++) {
                    int mx = PX[ME] + DX[d] * s, my = PY[ME] + DY[d] * s;
                    if (!inb(mx, my)) break;
                    if (TRL[my * W + mx] == ME) break;
                    v += 1;
                }
                if (v > bestv) { bestv = v; bestd = d; }
            }
            if (bestd < 0) {
                for (int d = 0; d < 4; d++) {
                    if (d == (PD[ME] ^ 1)) continue;
                    int nx = PX[ME] + DX[d], ny = PY[ME] + DY[d];
                    if (inb(nx, ny)) { bestd = d; break; }
                }
            }
            if (bestd < 0) bestd = PD[ME];
            move = bestd;
            route.clear();
            ri = 0;
            planKind = 0;
        }

        cout << turn << ' ' << DC[move] << endl;
    }
#ifdef DBG
    cerr << "STAT plans=" << PLN << " done=" << ABD << " abortKeep=" << ABA
         << " abortBail=" << ABB << " bailRest=" << ABB_REST << endl;
#endif
    return 0;
}
