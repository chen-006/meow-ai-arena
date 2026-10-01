// Territory bot: plan-based rectangle loop capture with dynamic cut-race safety.
// Duel (N=2) and melee (N=4) capable.
#include <bits/stdc++.h>
using namespace std;

const int DX[4] = {0, 0, -1, 1};
const int DY[4] = {-1, 1, 0, 0};
const char DC[4] = {'U', 'D', 'L', 'R'};
const int INF = 1000000000;

int W, H, MAX_TURNS, N, ME;

struct Player { int x, y, dir, trailLen, area, deaths; };
Player pl[4];
int curTurn = 0;
vector<int8_t> owner, trailg, prot;
int lastX[4], lastY[4], staticTurns[4];
bool haveLast = false;

deque<int> plan;
bool wasOutside = false;
int huntE = -1, huntK = -1;   // active hunt target (enemy id, cell)
int spawnX[4], spawnY[4];
mt19937 rng(0xC0FFEE);

inline bool inb(int x, int y) { return x >= 0 && x < W && y >= 0 && y < H; }
inline int id(int x, int y) { return y * W + x; }
inline int opp(int d) { return d ^ 1; }

bool readState() {
    string tok;
    if (!(cin >> tok)) return false;
    cin >> curTurn;
    for (int i = 0; i < N; i++) {
        int idn; char d;
        cin >> tok >> idn >> pl[idn].x >> pl[idn].y >> d >> pl[idn].trailLen >> pl[idn].area >> pl[idn].deaths;
        pl[idn].dir = (int)string("UDLR").find(d);
    }
    cin >> tok; // OWNER
    owner.assign(W * H, -1);
    {
        string row;
        for (int y = 0; y < H; y++) {
            cin >> row;
            for (int x = 0; x < W; x++) owner[id(x, y)] = (row[x] == '.') ? (int8_t)-1 : (int8_t)(row[x] - '0');
        }
    }
    cin >> tok; // TRAIL
    trailg.assign(W * H, -1);
    {
        string row;
        for (int y = 0; y < H; y++) {
            cin >> row;
            for (int x = 0; x < W; x++) trailg[id(x, y)] = (row[x] == '.') ? (int8_t)-1 : (int8_t)(row[x] - '0');
        }
    }
    cin >> tok; // END
    if (!haveLast) {
        for (int i = 0; i < N; i++) { lastX[i] = pl[i].x; lastY[i] = pl[i].y; staticTurns[i] = 0; }
        haveLast = true;
    } else {
        for (int i = 0; i < N; i++) {
            if (pl[i].x == lastX[i] && pl[i].y == lastY[i]) staticTurns[i]++;
            else { staticTurns[i] = 0; lastX[i] = pl[i].x; lastY[i] = pl[i].y; }
        }
    }
    return true;
}

// BFS distance field from (sx,sy); blocked(k) = cannot enter cell k.
template <class Block>
vector<int> bfsFrom(int sx, int sy, Block blocked) {
    vector<int> dist(W * H, -1);
    if (!inb(sx, sy)) return dist;
    int s = id(sx, sy);
    dist[s] = 0;
    deque<int> q; q.push_back(s);
    while (!q.empty()) {
        int k = q.front(); q.pop_front();
        int x = k % W, y = k / W;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inb(nx, ny)) continue;
            int nk = id(nx, ny);
            if (dist[nk] != -1 || blocked(nk)) continue;
            dist[nk] = dist[k] + 1;
            q.push_back(nk);
        }
    }
    return dist;
}

// Shortest path (dirs) from head to first popped cell satisfying goal; first step cannot reverse.
template <class Goal, class Block>
vector<int> bfsPath(int sx, int sy, int curdir, Goal goal, Block blocked) {
    vector<int> par(W * H, -1), pdir(W * H, -1);
    int s = id(sx, sy);
    par[s] = s;
    deque<int> q;
    for (int d = 0; d < 4; d++) {
        if (d == opp(curdir)) continue;
        int nx = sx + DX[d], ny = sy + DY[d];
        if (!inb(nx, ny)) continue;
        int nk = id(nx, ny);
        if (blocked(nk)) continue;
        if (par[nk] != -1) continue;
        par[nk] = s; pdir[nk] = d; q.push_back(nk);
    }
    while (!q.empty()) {
        int k = q.front(); q.pop_front();
        if (goal(k)) {
            vector<int> path;
            for (int c = k; c != s; c = par[c]) path.push_back(pdir[c]);
            reverse(path.begin(), path.end());
            return path;
        }
        int x = k % W, y = k / W;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inb(nx, ny)) continue;
            int nk = id(nx, ny);
            if (par[nk] != -1 || blocked(nk)) continue;
            par[nk] = k; pdir[nk] = d; q.push_back(nk);
        }
    }
    return {};
}

int anySafeMove(int x, int y, int curdir) {
    int best = -1, bestScore = -1;
    for (int d = 0; d < 4; d++) {
        if (d == opp(curdir)) continue;
        int nx = x + DX[d], ny = y + DY[d];
        if (!inb(nx, ny)) continue;
        if (trailg[id(nx, ny)] == ME) continue;
        int sc = (d == curdir) ? 2 : 0;
        for (int e = 0; e < 4; e++) {
            int ox = nx + DX[e], oy = ny + DY[e];
            if (inb(ox, oy) && trailg[id(ox, oy)] != ME) sc++;
        }
        if (sc > bestScore) { bestScore = sc; best = d; }
    }
    return best >= 0 ? best : curdir;
}

void makePlan() {
    Player& me = pl[ME];
    int remaining = MAX_TURNS - curTurn;
    int s = id(me.x, me.y);
    // parent BFS from head, first step no reverse, no blocks (we are inside, trail empty)
    vector<int> par(W * H, -1), pdirv(W * H, -1), distH(W * H, -1);
    par[s] = s; distH[s] = 0;
    deque<int> q;
    for (int d = 0; d < 4; d++) {
        if (d == opp(me.dir)) continue;
        int nx = me.x + DX[d], ny = me.y + DY[d];
        if (!inb(nx, ny)) continue;
        int nk = id(nx, ny);
        if (par[nk] != -1) continue;
        par[nk] = s; pdirv[nk] = d; q.push_back(nk);
    }
    while (!q.empty()) {
        int k = q.front(); q.pop_front();
        distH[k] = distH[par[k]] + 1;
        int x = k % W, y = k / W;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inb(nx, ny)) continue;
            int nk = id(nx, ny);
            if (par[nk] != -1) continue;
            par[nk] = k; pdirv[nk] = d; q.push_back(nk);
        }
    }
    // enemy threat fields (blocked by their own trail: entering own trail kills them)
    vector<vector<int>> ef(N);
    for (int e = 0; e < N; e++) {
        if (e == ME || staticTurns[e] >= 40) continue;
        ef[e] = bfsFrom(pl[e].x, pl[e].y, [&](int k) { return trailg[k] == (int8_t)e; });
    }
    auto enemyAt = [&](int k) {
        int m = INF;
        for (int e = 0; e < N; e++) {
            if (e == ME || staticTurns[e] >= 40) continue;
            if (ef[e][k] >= 0) m = min(m, ef[e][k]);
        }
        return m;
    };
    // prefix sums: neutral gainable, enemy gainable (excl. protected)
    vector<int> pN((W + 1) * (H + 1), 0), pE((W + 1) * (H + 1), 0);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int k = id(x, y), o = owner[k];
        int cN = (o == -1) ? 1 : 0;
        int cE = (o != -1 && o != ME && (int)prot[k] != o) ? 1 : 0;
        int r = (y + 1) * (W + 1) + (x + 1);
        pN[r] = pN[r - 1] + pN[r - (W + 1)] - pN[r - (W + 1) - 1] + cN;
        pE[r] = pE[r - 1] + pE[r - (W + 1)] - pE[r - (W + 1) - 1] + cE;
    }
    auto rectN = [&](int x0, int y0, int x1, int y1) {
        return pN[(y1 + 1) * (W + 1) + x1 + 1] - pN[y0 * (W + 1) + x1 + 1]
             - pN[(y1 + 1) * (W + 1) + x0] + pN[y0 * (W + 1) + x0];
    };
    auto rectE = [&](int x0, int y0, int x1, int y1) {
        return pE[(y1 + 1) * (W + 1) + x1 + 1] - pE[y0 * (W + 1) + x1 + 1]
             - pE[(y1 + 1) * (W + 1) + x0] + pE[y0 * (W + 1) + x0];
    };
    // exit candidates: non-mine cells adjacent to my territory
    vector<int> exits;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int k = id(x, y);
        if (owner[k] == ME) continue;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (inb(nx, ny) && owner[id(nx, ny)] == ME) { exits.push_back(k); break; }
        }
    }
    shuffle(exits.begin(), exits.end(), rng);
    if ((int)exits.size() > 40) exits.resize(40);

    const int maxTrail = (N == 2) ? 52 : 28;
    const int safetyMargin = (N == 2) ? 0 : 4;
    const int amax = 16, bmax = 16;

    double bestScore = 0.0;
    vector<int> bestPlan;

    for (int E : exits) {
        int ex = E % W, ey = E / W;
        int Tk = -1, d0 = -1, distT = -1;
        vector<int> pathT;
        for (int d = 0; d < 4 && d0 < 0; d++) {
            int tx = ex - DX[d], ty = ey - DY[d]; // T + dir d -> E
            if (!inb(tx, ty)) continue;
            int tk = id(tx, ty);
            if (owner[tk] != ME || par[tk] == -1) continue;
            int lastdir = (tk == s) ? me.dir : pdirv[tk];
            if (d == opp(lastdir)) continue;
            d0 = d; Tk = tk; distT = distH[tk];
            vector<int> p;
            for (int c = tk; c != s; c = par[c]) p.push_back(pdirv[c]);
            reverse(p.begin(), p.end());
            pathT = p;
        }
        if (d0 < 0 || distT > 22) continue;
        for (int si = 0; si < 2; si++) {
            int sd = (d0 < 2) ? (2 + si) : si;
            int cx = DX[d0], cy = DY[d0], vx = DX[sd], vy = DY[sd];

            // ---- U-shaped loop: territory border acts as the 4th side ----
            // territory side cells: E - d0 + j*s (j=0..b) must all be mine
            int maxB = 0;
            while (maxB < bmax) {
                int px = ex - cx + (maxB + 1) * vx, py = ey - cy + (maxB + 1) * vy;
                if (!inb(px, py) || owner[id(px, py)] != ME) break;
                maxB++;
            }
            for (int a = 1; a <= amax; a++) {
                for (int b = 1; b <= maxB; b++) {
                    int steps = distT + 2 * a + b + 2;      // total moves incl. capture step
                    if (steps + 3 > remaining) continue;
                    // box bounds: i=0..a (d0 axis), j=0..b (s axis) from E
                    int xA = ex + a * cx, yA = ey + a * cy;
                    int xB2 = ex + b * vx, yB2 = ey + b * vy;
                    int x0 = min(min(ex, xA), xB2), x1 = max(max(ex, xA), xB2);
                    int y0 = min(min(ey, yA), yB2), y1 = max(max(ey, yA), yB2);
                    // full box corner for gain: E + a*d0 + b*s
                    int xC = ex + a * cx + b * vx, yC = ey + a * cy + b * vy;
                    x0 = min(x0, min(ex, xC)); x1 = max(x1, max(ex, xC));
                    y0 = min(y0, min(ey, yC)); y1 = max(y1, max(ey, yC));
                    if (x0 < 0 || y0 < 0 || x1 >= W || y1 >= H) continue;
                    // walk path cells: leg1 (j=0, i=0..a), leg2 (i=a, j=1..b), leg3 (j=b, i=a-1..0)
                    int trailCount = 0;
                    int minEnemy = INF;
                    bool pathOk = true;
                    auto chk = [&](int px, int py) {
                        int k = id(px, py);
                        if (owner[k] == ME) { pathOk = false; return; }
                        trailCount++;
                        minEnemy = min(minEnemy, enemyAt(k));
                    };
                    for (int i = 0; i <= a; i++) chk(ex + i * cx, ey + i * cy);
                    if (!pathOk) break; // bigger a only adds more of the same
                    for (int j = 1; j <= b; j++) chk(ex + a * cx + j * vx, ey + a * cy + j * vy);
                    if (!pathOk) continue;
                    for (int i = a - 1; i >= 0; i--) chk(ex + i * cx + b * vx, ey + i * cy + b * vy);
                    if (!pathOk) continue;
                    int need = steps;
                    int slackOk = (N == 2) ? 8 : 2;
                    if (minEnemy < 4 || minEnemy < need - slackOk) continue;
                    if (trailCount > maxTrail) continue;
                    double gain = rectN(x0, y0, x1, y1) + 2.0 * rectE(x0, y0, x1, y1);
                    if (gain < 3.0) continue;
                    double score = gain - 0.12 * steps
                                 + 0.5 * (double)max(0, minEnemy - need)
                                 + (double)(rng() % 1000) * 1e-6 + 0.001;
                    if (score > bestScore) {
                        bestScore = score;
                        bestPlan = pathT;
                        bestPlan.push_back(d0);              // T -> E
                        for (int i = 0; i < a; i++) bestPlan.push_back(d0);
                        for (int j = 0; j < b; j++) bestPlan.push_back(sd);
                        for (int i = 0; i < a; i++) bestPlan.push_back(opp(d0));
                        // now at E + b*s ; step into territory
                        bestPlan.push_back(opp(d0));
                    }
                }
            }

            // ---- closed rectangle loop ----
            for (int a = 2; a <= amax; a++) {
                for (int b = 2; b <= bmax; b++) {
                    int L = 2 * (a + b);
                    int cost = distT + L + 2;
                    if (cost + 3 > remaining) continue;
                    int xB = ex + a * cx + b * vx, yB = ey + a * cy + b * vy;
                    int x0 = min(ex, xB), x1 = max(ex, xB), y0 = min(ey, yB), y1 = max(ey, yB);
                    if (x0 < 0 || y0 < 0 || x1 >= W || y1 >= H) continue;
                    int trailCount = 0;
                    int minEnemy = INF;
                    auto chk = [&](int px, int py) {
                        int k = id(px, py);
                        if (owner[k] != ME) trailCount++;
                        minEnemy = min(minEnemy, enemyAt(k));
                    };
                    for (int i = 0; i <= a; i++) chk(ex + i * cx, ey + i * cy);
                    for (int j = 1; j <= b; j++) chk(ex + a * cx + j * vx, ey + a * cy + j * vy);
                    for (int i = 1; i <= a; i++) chk(ex + (a - i) * cx + b * vx, ey + (a - i) * cy + b * vy);
                    for (int j = 1; j < b; j++) chk(ex + (b - j) * vx, ey + (b - j) * vy);
                    if (trailCount > maxTrail || trailCount == 0) continue;
                    double gain = rectN(x0, y0, x1, y1) + 2.0 * rectE(x0, y0, x1, y1);
                    if (gain < 3.0) continue;
                    int need = distT + L + 2;
                    int slackOk = (N == 2) ? 8 : 2;
                    if (minEnemy < 4 || minEnemy < need - slackOk) continue;
                    double score = gain - 0.12 * cost
                                 + 0.5 * (double)max(0, minEnemy - need)
                                 + (double)(rng() % 1000) * 1e-6;
                    if (score > bestScore) {
                        bestScore = score;
                        bestPlan = pathT;
                        bestPlan.push_back(d0);              // T -> E
                        for (int i = 0; i < a; i++) bestPlan.push_back(d0);
                        for (int j = 0; j < b; j++) bestPlan.push_back(sd);
                        for (int i = 0; i < a; i++) bestPlan.push_back(opp(d0));
                        for (int j = 0; j < b; j++) bestPlan.push_back(opp(sd));
                        bestPlan.push_back(opp(d0));         // E -> T : capture
                    }
                }
            }
        }
    }
    if (!bestPlan.empty()) {
        plan.assign(bestPlan.begin(), bestPlan.end());
        fprintf(stderr, "T%d PLAN len=%zu score=%.3f\n", curTurn, plan.size(), bestScore);
    } else {
        fprintf(stderr, "T%d NOPLAN exits=%zu\n", curTurn, exits.size());
    }
}

int decide() {
    Player& me = pl[ME];
    int hx = me.x, hy = me.y, hd = me.dir;
    int hk = id(hx, hy);
    bool inside = (owner[hk] == ME);
    int remaining = MAX_TURNS - curTurn;

    if (inside && wasOutside) plan.clear(); // captured or respawned
    wasOutside = !inside;

    // invalidate stale hunt target
    if (huntE >= 0 && (plan.empty() || trailg[huntK] != (int8_t)huntE)) {
        if (!plan.empty()) plan.clear();
        huntE = -1;
    }

    auto validMove = [&](int d) {
        if (d == opp(hd)) return false;
        int nx = hx + DX[d], ny = hy + DY[d];
        return inb(nx, ny) && trailg[id(nx, ny)] != ME;
    };

    // A. immediate adjacent cut of an enemy trail (entering it kills its owner)
    for (int d = 0; d < 4; d++) {
        if (!validMove(d)) continue;
        int nk = id(hx + DX[d], hy + DY[d]);
        int e = trailg[nk];
        if (e >= 0 && e != ME && pl[e].trailLen >= 2) {
            bool ok = true;
            if (N > 2) {
                for (int o = 0; o < N && ok; o++) {
                    if (o == ME || o == e || staticTurns[o] >= 40) continue;
                    if (abs(pl[o].x - (hx + DX[d])) + abs(pl[o].y - (hy + DY[d])) < 8) ok = false;
                }
            }
            if (ok) { plan.clear(); return d; }
        }
    }

    if (!inside) {
        vector<int> home = bfsPath(hx, hy, hd, [&](int k) { return owner[k] == ME; },
                                   [&](int k) { return trailg[k] == ME; });
        if (home.empty()) return anySafeMove(hx, hy, hd);
        int homeDist = (int)home.size();
        vector<int> myf = bfsFrom(hx, hy, [&](int k) { return trailg[k] == ME; });
        int minThreat = INF;
        int bestCut = INF, cutE = -1;
        for (int e = 0; e < N; e++) {
            if (e == ME || staticTurns[e] >= 40) continue;
            vector<int> ef = bfsFrom(pl[e].x, pl[e].y, [&](int k) { return trailg[k] == (int8_t)e; });
            int t = INF;
            for (int k = 0; k < W * H; k++)
                if (trailg[k] == ME && ef[k] >= 0 && ef[k] < t) t = ef[k];
            minThreat = min(minThreat, t);
            if (pl[e].trailLen > 0) {
                int c = INF;
                for (int k = 0; k < W * H; k++)
                    if (trailg[k] == (int8_t)e && myf[k] >= 0 && myf[k] < c) c = myf[k];
                if (c < bestCut) { bestCut = c; cutE = e; }
            }
        }
        int margin = (minThreat == INF) ? INF : minThreat - homeDist;
        int retreatAt = (N == 2) ? 1 : 2;
        if (margin <= retreatAt) {
            fprintf(stderr, "T%d DANGER margin=%d home=%d threat=%d planlen=%zu\n", curTurn, margin, homeDist, minThreat, plan.size());
            // counter-cut if the race is clearly won
            if (cutE >= 0 && bestCut <= 25 && bestCut + 1 <= minThreat) {
                vector<int> cp = bfsPath(hx, hy, hd, [&](int k) { return trailg[k] == (int8_t)cutE; },
                                         [&](int k) { return trailg[k] == ME; });
                if (!cp.empty() && (int)cp.size() + 1 <= minThreat) { plan.clear(); return cp[0]; }
            }
            // if we cannot outrun them anyway, race to complete the loop instead of retreating
            if (margin < 0 && !plan.empty()) {
                int d = plan.front();
                if (validMove(d)) { plan.pop_front(); return d; }
            }
            plan.clear();
            return home[0]; // bank what we can
        }
        // endgame: make sure we capture before the clock runs out
        if ((int)plan.size() + 1 > remaining || homeDist + 2 > remaining) {
            plan.clear();
            return home[0];
        }
        if (!plan.empty()) {
            int d = plan.front();
            if (validMove(d)) { plan.pop_front(); return d; }
            plan.clear();
        }
        return home[0];
    }

    // inside
    // hunt: walk out and cut a nearby enemy trail (we have no trail => low risk)
    if (plan.empty() && huntE < 0) {
        int maxHunt = (N == 2) ? 15 : 11;
        int minLen = (N == 2) ? 10 : 8;
        vector<int> hp;
        int bd = INF;
        for (int e = 0; e < N; e++) {
            if (e == ME || staticTurns[e] >= 40 || pl[e].trailLen < minLen) continue;
            // melee: skip if some other enemy is close to us
            if (N > 2) {
                bool crowded = false;
                for (int o = 0; o < N; o++) {
                    if (o == ME || o == e || staticTurns[o] >= 40) continue;
                    if (abs(pl[o].x - hx) + abs(pl[o].y - hy) < 6) crowded = true;
                }
                if (crowded) continue;
            }
            vector<int> p = bfsPath(hx, hy, hd, [&](int k) { return trailg[k] == (int8_t)e; },
                                    [&](int k) { return false; });
            if (p.empty() || (int)p.size() > maxHunt) continue;
            int tx = hx, ty = hy;
            for (int d : p) { tx += DX[d]; ty += DY[d]; }
            if (abs(tx - spawnX[e]) <= 3 && abs(ty - spawnY[e]) <= 3) continue; // pointless kill near their spawn
            if ((int)p.size() < bd) { bd = (int)p.size(); hp = p; huntE = e; huntK = id(tx, ty); }
        }
        if (!hp.empty()) {
            plan.assign(hp.begin(), hp.end());
            fprintf(stderr, "T%d HUNT e=%d dist=%d\n", curTurn, huntE, bd);
            int d = plan.front(); plan.pop_front();
            return d;
        }
        huntE = -1;
    }
    if (plan.empty() && remaining > 12) makePlan();
    if (!plan.empty()) {
        int d = plan.front();
        if (validMove(d)) { plan.pop_front(); return d; }
        plan.clear();
    }
    // patrol inside territory
    if (validMove(hd) && owner[id(hx + DX[hd], hy + DY[hd])] == ME) return hd;
    for (int d = 0; d < 4; d++) {
        if (!validMove(d)) continue;
        if (owner[id(hx + DX[d], hy + DY[d])] == ME) return d;
    }
    return anySafeMove(hx, hy, hd);
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string tok;
    int mv, im;
    cin >> tok; // INIT
    cin >> W >> H >> MAX_TURNS >> N >> ME >> mv >> im;
    prot.assign(W * H, -1);
    for (int i = 0; i < N; i++) {
        int idn, x, y;
        cin >> tok >> idn >> x >> y;
        spawnX[idn] = x; spawnY[idn] = y;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (inb(x + dx, y + dy)) prot[id(x + dx, y + dy)] = idn;
    }
    while (readState()) {
        int d = decide();
        cout << curTurn << ' ' << DC[d] << endl;
    }
    return 0;
}
