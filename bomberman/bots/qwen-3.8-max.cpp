// Bomb Arena bot — survival-first exact simulation + escape-denial hunting.
// Strategy:
//  * Exact deterministic forward simulation of the public rules engine
//    (chains, flame-triggered early detonation, crate ray blocking, shrink,
//     bomb cells blocking movement, flame expiry) over a 9-11 turn horizon.
//  * Survival dominates scoring: time-expanded reachability with the first
//    move forced; checked under three bomb scenarios (current bombs, nearest
//    rival placing now, every rival placing now).
//  * Offense = escape denial: after a hypothetical placement, each enemy's
//    best-case reachable set is recomputed (my destination also blocks their
//    escape via the engine's collision rule). Empty set = guaranteed kill.
//  * Potential field + persistent goal for coherent positional play:
//    early game opens crates and keeps distance; aggression rises as the
//    circle shrinks / players drop, turning into corner-and-squeeze hunting.
//  * Hard time guard: every phase checks a deadline and falls back to the
//    best move found so far, so output is always valid and on time.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <chrono>

typedef unsigned char u8;

static const int MAXN = 26, MAXC = 676, MAXH = 12, MAXB = 96, MAXP = 10;
static const int DX[5] = {0, 0, -1, 1, 0}, DY[5] = {-1, 1, 0, 0, 0};
static const char DS[6] = "UDLRS";

static int N, P, ME, LIMIT, FUSE, RANGE, CAP, FIRE, SHRINK, EVERY;
static int T, H, NN;
static char board[MAXN][MAXN];
static int pl_x[MAXP], pl_y[MAXP], pl_alive[MAXP];
struct Bomb { int owner, x, y, at; };
static Bomb bombs[MAXB];
static int nb;
static int flameIn[MAXC];

static inline int cellOf(int x, int y) { return y * N + x; }

// ---------------- fast input ----------------
// NOTE: fread() on a pipe blocks trying to fill the whole buffer; getchar()
// refills with a single read() call and returns as soon as data is available.
static inline int gc() { return getchar(); }
static inline bool tok(char* out) {
    int c;
    do { c = gc(); if (c == -1) return false; }
    while (c == ' ' || c == '\n' || c == '\r' || c == '\t');
    int n = 0;
    while (c != -1 && c != ' ' && c != '\n' && c != '\r' && c != '\t') {
        if (n < 60) out[n++] = (char)c;
        c = gc();
    }
    out[n] = 0;
    return true;
}
static inline bool tokInt(int& v) {
    char b[64];
    if (!tok(b)) return false;
    v = atoi(b);
    return true;
}

// ---------------- timing ----------------
static long long deadlineNS = 0;
static inline long long nowNS() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
static inline bool timeUp() { return nowNS() > deadlineNS; }

// ---------------- forward simulation ----------------
struct Scenario {
    u8 moveOpen[MAXH][MAXC];  // '.' after this turn's shrink, before blast
    u8 bombOcc[MAXH][MAXC];   // bomb present during this turn's move phase
    u8 safeEnd[MAXH][MAXC];   // '.' after blast and not lethal at turn end
};
static Scenario SC[6];

static char simGrid[MAXC];
static int simExp[MAXC];
static Bomb simB[MAXB];
static int simAt[MAXC];
static u8 simGone[MAXB], simHit[MAXC];
static int qB[MAXB * 16];

static void simulate(Scenario& out, const Bomb* extras, int nextra) {
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) simGrid[cellOf(x, y)] = board[y][x];
    for (int c = 0; c < NN; c++) simExp[c] = flameIn[c];
    int nb2 = 0;
    for (int i = 0; i < nb && nb2 < MAXB; i++) simB[nb2++] = bombs[i];
    for (int i = 0; i < nextra && nb2 < MAXB; i++) simB[nb2++] = extras[i];

    for (int k = 0; k < H; k++) {
        int tau = T + k;
        int layer = (tau < SHRINK) ? 0 : 1 + (tau - SHRINK) / EVERY;
        if (layer > 0) {
            for (int y = 0; y < N; y++) {
                int ry = (y < N - 1 - y) ? y : N - 1 - y;
                if (ry > layer) continue;
                for (int x = 0; x < N; x++) {
                    int rx = (x < N - 1 - x) ? x : N - 1 - x;
                    if ((rx < ry ? rx : ry) <= layer) {
                        int c = cellOf(x, y);
                        simGrid[c] = '#';
                        simExp[c] = -1;
                    }
                }
            }
            int w = 0;
            for (int i = 0; i < nb2; i++)
                if (simGrid[cellOf(simB[i].x, simB[i].y)] != '#') simB[w++] = simB[i];
            nb2 = w;
        }
        for (int c = 0; c < NN; c++)
            if (simExp[c] >= 0 && simExp[c] < tau) simExp[c] = -1;

        for (int c = 0; c < NN; c++) {
            out.moveOpen[k][c] = (simGrid[c] == '.');
            out.bombOcc[k][c] = 0;
            simAt[c] = -1;
        }
        for (int i = 0; i < nb2; i++) {
            int c = cellOf(simB[i].x, simB[i].y);
            out.bombOcc[k][c] = 1;
            simAt[c] = i;
        }

        memset(simGone, 0, nb2);
        memset(simHit, 0, NN);
        int qh = 0, qt = 0;
        for (int i = 0; i < nb2; i++) {
            int c = cellOf(simB[i].x, simB[i].y);
            if (simB[i].at <= tau || simExp[c] >= tau) qB[qt++] = i;
        }
        while (qh < qt) {
            int bi = qB[qh++];
            if (simGone[bi]) continue;
            simGone[bi] = 1;
            Bomb b = simB[bi];
            simHit[cellOf(b.x, b.y)] = 1;
            for (int d = 0; d < 4; d++)
                for (int r = 1; r <= RANGE; r++) {
                    int x = b.x + DX[d] * r, y = b.y + DY[d] * r;
                    if (x < 0 || y < 0 || x >= N || y >= N) break;
                    int c = cellOf(x, y);
                    if (simGrid[c] == '#') break;
                    simHit[c] = 1;
                    if (simGrid[c] == '+') break;
                    if (simAt[c] >= 0) {
                        if (!simGone[simAt[c]] && qt < MAXB * 16) qB[qt++] = simAt[c];
                        break;
                    }
                }
        }
        int w = 0;
        for (int i = 0; i < nb2; i++)
            if (!simGone[i]) simB[w++] = simB[i];
        nb2 = w;
        for (int c = 0; c < NN; c++)
            if (simHit[c]) {
                if (simGrid[c] == '+') simGrid[c] = '.';
                int e = tau + FIRE - 1;
                if (simExp[c] < e) simExp[c] = e;
            }
        for (int c = 0; c < NN; c++)
            out.safeEnd[k][c] = (simGrid[c] == '.' && simExp[c] < tau);
    }
}

// ---------------- reachability ----------------
static double potential[MAXC];
static u8 rCur[MAXC], rNxt[MAXC];
struct Reach { int life, count, count2; double util; };

static Reach reach(const Scenario& s, int start, int forced, int block) {
    Reach R{0, 0, 0, -1e9};
    memset(rCur, 0, NN);
    if (start >= 0 && start < NN) rCur[start] = 1;
    for (int k = 0; k < H; k++) {
        for (int c = 0; c < NN; c++)
            if (rCur[c] && !s.moveOpen[k][c]) rCur[c] = 0;  // shrink death
        memset(rNxt, 0, NN);
        if (k == 0 && forced >= 0) {
            if (rCur[start] && s.moveOpen[0][forced] &&
                (forced == start || !s.bombOcc[0][forced]) && s.safeEnd[0][forced])
                rNxt[forced] = 1;
        } else {
            for (int c = 0; c < NN; c++) {
                if (!rCur[c]) continue;
                int x = c % N, y = c / N;
                for (int d = 0; d < 5; d++) {
                    int nx = x + DX[d], ny = y + DY[d], nc = c;
                    if (nx >= 0 && ny >= 0 && nx < N && ny < N) {
                        int z = cellOf(nx, ny);
                        bool blockedByMe = (k == 0 && block >= 0 && z == block && z != c);
                        if (!blockedByMe && s.moveOpen[k][z] && (z == c || !s.bombOcc[k][z]))
                            nc = z;
                    }
                    if (s.safeEnd[k][nc]) rNxt[nc] = 1;
                }
            }
        }
        memcpy(rCur, rNxt, NN);
        int cnt = 0;
        double u = -1e9;
        for (int c = 0; c < NN; c++)
            if (rCur[c]) {
                cnt++;
                if (potential[c] > u) u = potential[c];
            }
        if (cnt == 0) break;
        R.life = k + 1;
        R.count = cnt;
        R.util = u;
        if (k == 1) R.count2 = cnt;
    }
    return R;
}

static u8 regCur[MAXC], regNxt[MAXC];
static void reachRegion2(const Scenario& s, int start, u8* out) {
    memset(out, 0, NN);
    memset(regCur, 0, NN);
    if (start >= 0 && start < NN) regCur[start] = 1;
    for (int k = 0; k < 2; k++) {
        for (int c = 0; c < NN; c++)
            if (regCur[c] && !s.moveOpen[k][c]) regCur[c] = 0;
        memset(regNxt, 0, NN);
        for (int c = 0; c < NN; c++) {
            if (!regCur[c]) continue;
            int x = c % N, y = c / N;
            for (int d = 0; d < 5; d++) {
                int nx = x + DX[d], ny = y + DY[d], nc = c;
                if (nx >= 0 && ny >= 0 && nx < N && ny < N) {
                    int z = cellOf(nx, ny);
                    if (s.moveOpen[k][z] && (z == c || !s.bombOcc[k][z])) nc = z;
                }
                if (s.safeEnd[k][nc]) regNxt[nc] = 1;
            }
        }
        memcpy(regCur, regNxt, NN);
        for (int c = 0; c < NN; c++)
            if (regCur[c]) out[c] = 1;
    }
}

// ---------------- potential field ----------------
static int baseCnt[MAXP], baseLife[MAXP];
static u8 nearReg[MAXP][MAXC];
static double aggr = 0;

static int crateRay(int x, int y) {
    int v = 0;
    for (int d = 0; d < 4; d++)
        for (int r = 1; r <= RANGE; r++) {
            int nx = x + DX[d] * r, ny = y + DY[d] * r;
            if (nx < 0 || ny < 0 || nx >= N || ny >= N) break;
            char g = board[ny][nx];
            if (g == '#') break;
            if (g == '+') { v++; break; }
            if (g == '.') {
                bool bombThere = false;
                for (int i = 0; i < nb; i++)
                    if (bombs[i].x == nx && bombs[i].y == ny) { bombThere = true; break; }
                if (bombThere) break;
            }
        }
    return v;
}

static void make_potential() {
    static u8 done[MAXC];
    for (int c = 0; c < NN; c++) { potential[c] = -1e6; done[c] = 0; }
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            if (board[y][x] != '.') continue;
            int c = cellOf(x, y);
            int edge = x;
            if (y < edge) edge = y;
            if (N - 1 - x < edge) edge = N - 1 - x;
            if (N - 1 - y < edge) edge = N - 1 - y;
            double v = 1.3 * crateRay(x, y);
            int openN = 0;
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d], ny = y + DY[d];
                if (nx >= 0 && ny >= 0 && nx < N && ny < N && board[ny][nx] == '.') openN++;
            }
            v += 0.4 * openN;
            // hunting spots: blast cells overlapping a cornered enemy's region
            for (int i = 0; i < P; i++) {
                if (i == ME || !pl_alive[i] || baseCnt[i] < 1 || baseCnt[i] > 8) continue;
                int overlap = 0;
                if (nearReg[i][c]) overlap++;
                for (int dd = 0; dd < 4; dd++)
                    for (int r = 1; r <= RANGE; r++) {
                        int nx = x + DX[dd] * r, ny = y + DY[dd] * r;
                        if (nx < 0 || ny < 0 || nx >= N || ny >= N) break;
                        char g = board[ny][nx];
                        if (g == '#') break;
                        if (nearReg[i][cellOf(nx, ny)]) overlap++;
                        if (g == '+') break;
                    }
                double add = aggr * 3.0 * overlap / baseCnt[i];
                if (add > 5.0) add = 5.0;
                v += add;
            }
            // enemy distance shaping
            int dmin = 100;
            for (int i = 0; i < P; i++)
                if (i != ME && pl_alive[i]) {
                    int dd = abs(pl_x[i] - x) + abs(pl_y[i] - y);
                    if (dd < dmin) dmin = dd;
                }
            if (dmin < 100) {
                v += (1.0 - aggr) * (dmin > 6 ? 3.0 : 0.5 * dmin);
                if (dmin <= 5) v += aggr * 0.9 * (6 - dmin);
                else v -= aggr * 0.3 * (dmin > 13 ? 8 : dmin - 5);
            }
            // shrink safety
            int until = SHRINK + (edge - 1) * EVERY - T;
            if (until < 8) v -= 40;
            else if (until < 20) v -= 10;
            else if (until < 45) v -= 2;
            potential[c] = v;
        }
    // distance transform (max-propagation with 0.8 per step)
    for (int iter = 0; iter < NN; iter++) {
        int best = -1;
        double bv = -1e9;
        for (int c = 0; c < NN; c++)
            if (!done[c] && potential[c] > bv) { bv = potential[c]; best = c; }
        if (best < 0 || bv <= -1e5) break;
        done[best] = 1;
        int x = best % N, y = best / N;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (nx < 0 || ny < 0 || nx >= N || ny >= N || board[ny][nx] != '.') continue;
            int nc = cellOf(nx, ny);
            if (bv - 0.8 > potential[nc]) potential[nc] = bv - 0.8;
        }
    }
}

// ---------------- goal system ----------------
static int goal = -1, goalTurn = -1000;
static int distMe[MAXC], distGoal[MAXC];
static int qCells[MAXC];
static int recent[40];
static int recentN = 0;
static int regionSize = 0;

static void bfsDist(int start, int* dist) {
    for (int c = 0; c < NN; c++) dist[c] = 1000000;
    if (start < 0 || board[start / N][start % N] != '.') return;
    int qh = 0, qt = 0;
    dist[start] = 0;
    qCells[qt++] = start;
    while (qh < qt) {
        int c = qCells[qh++];
        int x = c % N, y = c / N;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (nx < 0 || ny < 0 || nx >= N || ny >= N || board[ny][nx] != '.') continue;
            int z = cellOf(nx, ny);
            if (dist[z] > dist[c] + 1) { dist[z] = dist[c] + 1; qCells[qt++] = z; }
        }
    }
}

static double visits(int c) {
    double s = 0;
    for (int k = 0; k < recentN; k++)
        if (recent[k] == c) s += 1.0 / (1.0 + 0.15 * (recentN - 1 - k));
    return s;
}

static void update_goal() {
    int me = cellOf(pl_x[ME], pl_y[ME]);
    bfsDist(me, distMe);
    regionSize = 0;
    for (int c = 0; c < NN; c++)
        if (distMe[c] < 1000000) regionSize++;
    int layer = (T < SHRINK) ? 0 : 1 + (T - SHRINK) / EVERY;
    bool bad = goal < 0 || board[goal / N][goal % N] != '.' || distMe[goal] >= 1000000 ||
               T - goalTurn >= 12 || goal == me;
    if (!bad) {
        int gx = goal % N, gy = goal / N;
        int e = gx;
        if (gy < e) e = gy;
        if (N - 1 - gx < e) e = N - 1 - gx;
        if (N - 1 - gy < e) e = N - 1 - gy;
        if (e <= layer + 1) bad = true;
    }
    if (bad) {
        goal = -1;
        double best = -1e18;
        for (int c = 0; c < NN; c++) {
            if (board[c / N][c % N] != '.' || distMe[c] >= 1000000) continue;
            double v = potential[c] - 0.65 * distMe[c] - 0.9 * visits(c);
            if (v > best) { best = v; goal = c; }
        }
        goalTurn = T;
    }
    bfsDist(goal < 0 ? me : goal, distGoal);
}

// ---------------- enemy move model & destination prediction ----------------
static int assumeDir[MAXP];
static int stayLifeE[MAXP];
static int threatDist[MAXC];

static void modelEnemies(const Scenario& s0) {
    // threat cells: bombs about to explode, active flames
    int thr[MAXC], nt = 0;
    for (int i = 0; i < nb; i++)
        if (bombs[i].at <= T + 2 && nt < MAXC) thr[nt++] = cellOf(bombs[i].x, bombs[i].y);
    for (int c = 0; c < NN; c++)
        if (flameIn[c] >= T && nt < MAXC) thr[nt++] = c;
    for (int c = 0; c < NN; c++) {
        threatDist[c] = 1000;
        for (int i = 0; i < nt; i++) {
            int d = abs(c % N - thr[i] % N) + abs(c / N - thr[i] / N);
            if (d < threatDist[c]) threatDist[c] = d;
        }
    }
    for (int i = 0; i < P; i++) {
        assumeDir[i] = 4;
        if (i == ME || !pl_alive[i]) continue;
        int c = cellOf(pl_x[i], pl_y[i]);
        // An enemy whose cell becomes unsafe within a few turns will move NOW
        // (strong bots relocate early); assuming "stay" there wrongly blocks
        // our own escape through their cell.
        int stayLife = 0;
        while (stayLife < H && s0.safeEnd[stayLife][c]) stayLife++;
        stayLifeE[i] = stayLife;
        bool danger = stayLife <= 3;
        if (!danger) { assumeDir[i] = 4; continue; }
        int bestD = 4, bestV = -1 << 30;
        for (int d = 0; d < 5; d++) {
            int nx = pl_x[i] + DX[d], ny = pl_y[i] + DY[d], nc = c;
            if (nx >= 0 && ny >= 0 && nx < N && ny < N) {
                int z = cellOf(nx, ny);
                if (s0.moveOpen[0][z] && (z == c || !s0.bombOcc[0][z])) nc = z;
            }
            if (!s0.safeEnd[0][nc]) continue;
            int v = threatDist[nc] * 2 + (nc != c ? 3 : 0);
            if (v > bestV) { bestV = v; bestD = d; }
        }
        assumeDir[i] = bestD;  // 4 if no safe option (doomed: assume stay)
    }
}

static int predictDest(int myDir, const Scenario& s) {
    int orig[MAXP], targ[MAXP], res[MAXP], n = 0, myIdx = 0;
    for (int i = 0; i < P; i++) {
        if (!pl_alive[i]) continue;
        int o = cellOf(pl_x[i], pl_y[i]);
        int d = (i == ME) ? myDir : assumeDir[i];
        int nx = pl_x[i] + DX[d], ny = pl_y[i] + DY[d], t = o;
        if (nx >= 0 && ny >= 0 && nx < N && ny < N) {
            int z = cellOf(nx, ny);
            if (s.moveOpen[0][z] && (z == o || !s.bombOcc[0][z])) t = z;
        }
        orig[n] = o;
        targ[n] = t;
        if (i == ME) myIdx = n;
        n++;
    }
    for (int i = 0; i < n; i++) res[i] = targ[i];
    for (int i = 0; i < n; i++) {
        int cnt = 0;
        for (int j = 0; j < n; j++)
            if (targ[j] == targ[i]) cnt++;
        if (cnt > 1) res[i] = orig[i];
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < n; i++) {
            if (res[i] == orig[i]) continue;
            for (int j = 0; j < n; j++)
                if (res[j] == orig[j] && orig[j] == res[i]) {
                    res[i] = orig[i];
                    changed = true;
                    break;
                }
        }
    }
    return res[myIdx];
}

// ---------------- decision ----------------
static int blast_value(int x, int y) {
    int v = 0;
    for (int d = 0; d < 4; d++)
        for (int r = 1; r <= RANGE; r++) {
            int nx = x + DX[d] * r, ny = y + DY[d] * r;
            if (nx < 0 || ny < 0 || nx >= N || ny >= N || board[ny][nx] == '#') break;
            if (board[ny][nx] == '+') { v += 3; break; }
            bool bombThere = false;
            for (int i = 0; i < nb; i++)
                if (bombs[i].x == nx && bombs[i].y == ny) { bombThere = true; break; }
            if (bombThere) break;
        }
    return v;
}

static int decide() {
    int meCell = cellOf(pl_x[ME], pl_y[ME]);
    int aliveCnt = 0;
    for (int i = 0; i < P; i++)
        if (pl_alive[i]) aliveCnt++;
    H = (aliveCnt <= 4) ? 11 : 9;
    if (H > LIMIT - T) H = LIMIT - T;
    if (H > MAXH) H = MAXH;
    if (H < 1) H = 1;

    simulate(SC[0], nullptr, 0);
    if (timeUp()) return 4;

    // enemy baseline reach & near regions
    aggr = 1.15 - 0.13 * (aliveCnt - 1) + (T >= SHRINK - 50 ? 0.3 : 0.0);
    for (int i = 0; i < P; i++) {
        baseCnt[i] = 0;
        baseLife[i] = 0;
        memset(nearReg[i], 0, NN);
        if (i == ME || !pl_alive[i]) continue;
        Reach r = reach(SC[0], cellOf(pl_x[i], pl_y[i]), -1, -1);
        baseCnt[i] = r.count;
        baseLife[i] = r.life;
        if (r.count > 0 && r.count <= 8) aggr += 0.1;
        reachRegion2(SC[0], cellOf(pl_x[i], pl_y[i]), nearReg[i]);
    }
    if (aggr > 1.0) aggr = 1.0;
    if (aggr < 0.0) aggr = 0.0;

    make_potential();
    update_goal();
    modelEnemies(SC[0]);
    if (timeUp()) return 4;

    // placement eligibility
    int myBombs = 0;
    bool cellFree = true;
    for (int i = 0; i < nb; i++) {
        if (bombs[i].owner == ME) myBombs++;
        if (cellOf(bombs[i].x, bombs[i].y) == meCell) cellFree = false;
    }
    bool placeOK = myBombs < CAP && cellFree;
    bool myBombHere = false;
    for (int i = 0; i < nb; i++)
        if (bombs[i].owner == ME && cellOf(bombs[i].x, bombs[i].y) == meCell) myBombHere = true;
    Bomb mine{ME, pl_x[ME], pl_y[ME], T + FUSE};

    // threat: nearest eligible rival; all-place paranoia scenario
    int threat = -1, threatD = 1000, nAll = 0;
    Bomb allB[MAXP];
    for (int i = 0; i < P; i++) {
        if (i == ME || !pl_alive[i]) continue;
        int cnt = 0;
        bool onBomb = false;
        for (int j = 0; j < nb; j++) {
            if (bombs[j].owner == i) cnt++;
            if (cellOf(bombs[j].x, bombs[j].y) == cellOf(pl_x[i], pl_y[i])) onBomb = true;
        }
        if (cnt >= CAP || onBomb) continue;
        Bomb b{i, pl_x[i], pl_y[i], T + FUSE};
        allB[nAll++] = b;
        int d = abs(pl_x[i] - pl_x[ME]) + abs(pl_y[i] - pl_y[ME]);
        if (d <= 8 && d < threatD) { threatD = d; threat = i; }
    }

    Scenario *Sb = &SC[0], *Sm = &SC[1], *St = &SC[2], *Smt = &SC[3], *Sa = &SC[4], *Smat = &SC[5];
    if (placeOK) {
        simulate(SC[1], &mine, 1);
    } else {
        Sm = Sb;
    }
    if (threat >= 0) {
        Bomb tb{threat, pl_x[threat], pl_y[threat], T + FUSE};
        simulate(SC[2], &tb, 1);
        if (placeOK) {
            Bomb both[2] = {mine, tb};
            simulate(SC[3], both, 2);
        } else {
            Smt = St;
        }
    } else {
        St = Sb;
        Smt = Sm;
    }
    if (nAll > 0) {
        simulate(SC[4], allB, nAll);
        if (placeOK) {
            Bomb allMine[MAXP + 1];
            allMine[0] = mine;
            for (int i = 0; i < nAll; i++) allMine[i + 1] = allB[i];
            simulate(SC[5], allMine, nAll + 1);
        } else {
            Smat = Sa;
        }
    } else {
        Sa = Sb;
        Smat = Sm;
    }
    if (timeUp()) return 4;

    // per-enemy denial baseline under my placement (no body block)
    int scenCnt[MAXP];
    for (int i = 0; i < P; i++) {
        scenCnt[i] = baseCnt[i];
        if (i == ME || !pl_alive[i] || !placeOK) continue;
        int d = abs(pl_x[i] - pl_x[ME]) + abs(pl_y[i] - pl_y[ME]);
        if (d <= 10) scenCnt[i] = reach(*Sm, cellOf(pl_x[i], pl_y[i]), -1, -1).count;
    }

    // quick fallback: safest immediate move by potential
    int fbD = 4;
    double fbV = -1e30;
    for (int d = 0; d < 5; d++) {
        int nx = pl_x[ME] + DX[d], ny = pl_y[ME] + DY[d], nc = meCell;
        if (nx >= 0 && ny >= 0 && nx < N && ny < N) {
            int z = cellOf(nx, ny);
            if (SC[0].moveOpen[0][z] && (z == meCell || !SC[0].bombOcc[0][z])) nc = z;
        }
        if (!SC[0].safeEnd[0][nc]) continue;
        double v = potential[nc] - (nc == meCell ? 0.5 : 0);
        if (v > fbV) { fbV = v; fbD = d; }
    }

    int bestAct = fbD, bestPlace = 0, evaluated = 0;
    double bestScore = -1e30;
    for (int place = 0; place <= 1; place++) {
        if (place && !placeOK) continue;
        const Scenario& scen = place ? *Sm : *Sb;
        const Scenario& cautS = place ? *Smt : *St;
        const Scenario& parS = place ? *Smat : *Sa;
        for (int d = 0; d < 5; d++) {
            if (timeUp()) goto done;
            int destPred = predictDest(d, scen);
            // raw terrain target ignoring player collisions
            int terrT = meCell;
            {
                int nx = pl_x[ME] + DX[d], ny = pl_y[ME] + DY[d];
                if (nx >= 0 && ny >= 0 && nx < N && ny < N) {
                    int z = cellOf(nx, ny);
                    if (scen.moveOpen[0][z] && (z == meCell || !scen.bombOcc[0][z])) terrT = z;
                }
            }
            Reach own = reach(scen, meCell, destPred, -1);
            bool hedged = false;
            double hedgePen = 0;
            // Free-option hedge: if an enemy assumed to stay blocks terrT, the
            // blocked outcome equals standing still, while the swap outcome can
            // save us. Enemies whose cell turns unsafe soon will relocate, so
            // take the better branch with a small uncertainty penalty.
            if (destPred != terrT && terrT != meCell && d != 4) {
                for (int i = 0; i < P; i++)
                    if (i != ME && pl_alive[i] && cellOf(pl_x[i], pl_y[i]) == terrT) {
                        if (stayLifeE[i] <= 6) {
                            Reach sw = reach(scen, meCell, terrT, -1);
                            if (sw.life > own.life) {
                                own = sw;
                                destPred = terrT;
                                hedged = true;
                                hedgePen = 4.0;
                            }
                        }
                        break;
                    }
            }
            double score = own.life * 10000.0 - hedgePen;
            if (own.count > 0) score += 0.25 * own.util + 1.1 * log(1.0 + own.count);
            // anti-pocket: few reachable cells next turn = trap-prone
            if (own.life >= 2) {
                if (own.count2 <= 1) score -= 6.0;
                else if (own.count2 == 2) score -= 2.0;
                else if (own.count2 == 3) score -= 0.8;
            }
            // never stand on a burning fuse: leaving now buys a turn of slack;
            // "stay and flee later" dies when a rival plugs the corridor.
            if (destPred == meCell && (place == 1 || myBombHere)) score -= 15.0;

            int destC = predictDest(d, cautS);
            Reach caut = reach(cautS, meCell, destC, -1);
            if (caut.life == 0 && own.life > 0) score -= 35 + 5 * own.life;
            else if (caut.life < own.life) score -= 3.0 * (own.life - caut.life);

            int destP = predictDest(d, parS);
            Reach par = reach(parS, meCell, destP, -1);
            if (par.life == 0 && own.life > 0) score -= 18 + 3 * own.life;
            else if (par.life < own.life) score -= 1.5 * (own.life - par.life);

            // swap risk: destination occupied by an enemy assumed to move away
            if (!hedged) {
                int nx = pl_x[ME] + DX[d], ny = pl_y[ME] + DY[d];
                if (nx >= 0 && ny >= 0 && nx < N && ny < N) {
                    int z = cellOf(nx, ny);
                    if (destPred == z && z != meCell) {
                        for (int i = 0; i < P; i++)
                            if (i != ME && pl_alive[i] && cellOf(pl_x[i], pl_y[i]) == z) {
                                Reach stayR = reach(scen, meCell, meCell, -1);
                                if (stayR.life < own.life) score -= 12.0 * (own.life - stayR.life);
                                break;
                            }
                    }
                }
            }

            // denial / body block
            double den = 0;
            if (place) {
                den += 0.7 * blast_value(pl_x[ME], pl_y[ME]) - 2.2;
                if (regionSize < 10) den += 0.8 * (10 - regionSize);
            }
            for (int i = 0; i < P; i++) {
                if (i == ME || !pl_alive[i]) continue;
                int base = baseCnt[i];
                if (base <= 0) continue;
                int ec = cellOf(pl_x[i], pl_y[i]);
                int dd = abs(pl_x[i] - pl_x[ME]) + abs(pl_y[i] - pl_y[ME]);
                if (dd > 10 && abs(ec % N - destPred % N) + abs(ec / N - destPred / N) > 2)
                    continue;
                int sc = place ? scenCnt[i] : baseCnt[i];
                if (abs(ec % N - destPred % N) + abs(ec / N - destPred / N) <= 2)
                    sc = reach(scen, ec, -1, destPred).count;
                if (sc == 0) den += 130;
                else {
                    double ratio = 1.0 - (double)sc / base;
                    if (ratio > 0) den += 5.0 * ratio;
                    if (sc <= 3) den += 6.0 * (4 - sc);
                }
            }
            score += den;

            int gd = distGoal[destPred];
            if (gd > 1000000) gd = 60;
            if (gd > 20) gd = 20;
            score -= 1.3 * gd + 1.8 * visits(destPred);
            if (destPred == meCell) score -= 0.6;
            score -= 0.002 * ((d + T) % 5);

            if (score > bestScore) {
                bestScore = score;
                bestAct = d;
                bestPlace = place;
                evaluated = 1;
            }
        }
    }
done:
    if (!evaluated) return fbD;  // place stays 0
    if (bestPlace) goal = -1;
    return bestAct + 10 * bestPlace;
}

// ---------------- main ----------------
int main() {
    char w[64];
    if (!tok(w) || strcmp(w, "INIT") != 0) return 0;
    if (!tokInt(N) || !tokInt(P) || !tokInt(ME) || !tokInt(LIMIT) || !tokInt(FUSE) ||
        !tokInt(RANGE) || !tokInt(CAP) || !tokInt(FIRE) || !tokInt(SHRINK) || !tokInt(EVERY))
        return 0;
    if (N > MAXN - 1) N = MAXN - 1;
    if (P > MAXP) P = MAXP;
    NN = N * N;
    goal = -1;
    goalTurn = -1000;
    recentN = 0;

    while (true) {
        if (!tok(w)) break;
        if (strcmp(w, "TURN") != 0) break;
        if (!tokInt(T)) break;
        deadlineNS = nowNS() + (T == 0 ? 900LL : 34LL) * 1000000LL;
        bool ok = true;
        if (!tok(w) || strcmp(w, "BOARD") != 0) break;
        for (int y = 0; y < N; y++) {
            if (!tok(w)) { ok = false; break; }
            strncpy(board[y], w, MAXN - 1);
            board[y][N] = 0;
        }
        for (int i = 0; i < P && ok; i++) {
            int id;
            ok = tok(w) && tokInt(id) && tokInt(pl_x[id]) && tokInt(pl_y[id]) && tokInt(pl_alive[id]);
        }
        int k;
        if (ok) ok = tok(w) && tokInt(k);
        nb = 0;
        for (int i = 0; i < k && ok; i++) {
            Bomb b;
            ok = tok(w) && tokInt(b.owner) && tokInt(b.x) && tokInt(b.y) && tokInt(b.at);
            if (nb < MAXB) bombs[nb++] = b;
        }
        for (int c = 0; c < NN; c++) flameIn[c] = -1;
        if (ok) ok = tok(w) && tokInt(k);
        for (int i = 0; i < k && ok; i++) {
            int x, y, e;
            ok = tok(w) && tokInt(x) && tokInt(y) && tokInt(e);
            if (x >= 0 && y >= 0 && x < N && y < N) flameIn[cellOf(x, y)] = e;
        }
        if (ok) ok = tok(w) && tokInt(k);  // STATUS
        for (int i = 0; i < k && ok; i++) {
            int a, b2, c2, d2;
            ok = tok(w) && tokInt(a) && tokInt(b2) && tokInt(c2) && tokInt(d2);
        }
        if (ok) ok = tok(w) && tokInt(k);  // SOURCES
        for (int i = 0; i < k && ok; i++) {
            int a, b2, c2, d2;
            ok = tok(w) && tokInt(a) && tokInt(b2) && tokInt(c2) && tokInt(d2);
        }
        if (ok) ok = tok(w) && strcmp(w, "END") == 0;
        if (!ok) break;

        int act = 4, place = 0;
        if (pl_alive[ME]) {
            int r = decide();
            if (r >= 10) { act = r - 10; place = 1; }
            else act = r;
        }
        // remember position for anti-oscillation
        if (recentN >= 32) {
            memmove(recent, recent + 1, (recentN - 1) * sizeof(int));
            recentN--;
        }
        recent[recentN++] = cellOf(pl_x[ME], pl_y[ME]);

        char out[64];
        int len = snprintf(out, sizeof(out), "%d %c %d\n", T, DS[act], place);
        fwrite(out, 1, len, stdout);
        fflush(stdout);
    }
    return 0;
}
