// Bomberman bot: exact time-expanded danger simulation + robust escape checks
// against hidden simultaneous enemy placements, trap/kill detection, crate and
// shrink-aware navigation.
#include <bits/stdc++.h>
using namespace std;

int N, P, ME, LIMIT, FUSE, RANGE_, CAP, FIRE, SHRINK, EVERY, T, NN;
vector<string> board;
struct Pl { int x, y, alive; };
vector<Pl> pl;
vector<uint8_t> occ1;
struct Bomb { int owner, c, at, placed; };  // placed: step index from which it exists (0 = now)
vector<Bomb> bombs;
vector<int> flameEnd;
const int DX[5] = {0, 0, -1, 1, 0}, DY[5] = {-1, 1, 0, 0, 0};
const char* DS = "UDLRS";
chrono::steady_clock::time_point tStart;
double elapsedMs() { return chrono::duration<double, milli>(chrono::steady_clock::now() - tStart).count(); }

inline int edgeOf(int c) { int x = c % N, y = c / N; return min(min(x, y), min(N - 1 - x, N - 1 - y)); }
inline int layerAt(int t) { return t < SHRINK ? 0 : 1 + (t - SHRINK) / EVERY; }
inline int nb(int c, int d) {
    int x = c % N + DX[d], y = c / N + DY[d];
    if (x < 0 || y < 0 || x >= N || y >= N) return -1;
    return y * N + x;
}
// turn at which cell c becomes wall by shrink
inline int wallTurn(int c) { int e = edgeOf(c); if (e == 0) return -1; return SHRINK + (e - 1) * EVERY; }

const int HMAX = 12;
const int BODYK = 4;
#ifndef PEN_NEAR
#define PEN_NEAR 0.0
#endif
#ifndef PEN_FAR
#define PEN_FAR 0.0
#endif
#ifndef D2FLAT
#define D2FLAT 15
#endif
#ifndef OCC
#define OCC 1
#endif
#ifndef DBG
#define DBG 0
#endif
#ifndef PARTK
#define PARTK 0.0
#endif
#ifndef AGG
#define AGG 1.0
#endif
struct Sim {
    int H;
    vector<uint8_t> wall[HMAX], pass[HMAX], leth[HMAX];
    vector<int> crateGone;  // step index when crate destroyed, or INT_MAX
};

Sim simulate(const vector<Bomb>& bsIn, int H) {
    Sim s; s.H = H;
    vector<char> g(NN);
    for (int c = 0; c < NN; c++) g[c] = board[c / N][c % N];
    vector<int> fl = flameEnd;
    vector<Bomb> bs = bsIn;
    s.crateGone.assign(NN, INT_MAX);
    vector<int> at(NN, -1);
    for (int k = 0; k < H; k++) {
        int t = T + k;
        if (t >= SHRINK && (t - SHRINK) % EVERY == 0) {
            int L = layerAt(t);
            for (int c = 0; c < NN; c++) if (edgeOf(c) <= L) { g[c] = '#'; fl[c] = -1; }
            vector<Bomb> nbs;
            for (auto& b : bs) if (g[b.c] != '#') nbs.push_back(b);
            bs.swap(nbs);
        }
        s.wall[k].assign(NN, 0);
        for (int c = 0; c < NN; c++) s.wall[k][c] = g[c] == '#';
        s.pass[k].assign(NN, 0);
        for (int c = 0; c < NN; c++) s.pass[k][c] = g[c] == '.';
        fill(at.begin(), at.end(), -1);
        for (int i = 0; i < (int)bs.size(); i++) if (bs[i].placed <= k) { at[bs[i].c] = i; s.pass[k][bs[i].c] = 0; }
        // explosions
        vector<char> gone(bs.size(), 0), hit(NN, 0);
        deque<int> q;
        for (int i = 0; i < (int)bs.size(); i++) if (bs[i].placed <= k && (bs[i].at <= t || fl[bs[i].c] >= t)) q.push_back(i);
        vector<int> destroyed;
        while (!q.empty()) {
            int i = q.front(); q.pop_front();
            if (gone[i]) continue;
            gone[i] = 1; hit[bs[i].c] = 1;
            int bx = bs[i].c % N, by = bs[i].c / N;
            for (int d = 0; d < 4; d++) for (int r = 1; r <= RANGE_; r++) {
                int x = bx + DX[d] * r, y = by + DY[d] * r;
                if (x < 0 || y < 0 || x >= N || y >= N) break;
                int c = y * N + x;
                if (g[c] == '#') break;
                hit[c] = 1;
                if (g[c] == '+') { destroyed.push_back(c); break; }
                if (at[c] >= 0) { q.push_back(at[c]); break; }
            }
        }
        if (!destroyed.empty() || count(gone.begin(), gone.end(), 1)) {
            vector<Bomb> nbs;
            for (int i = 0; i < (int)bs.size(); i++) if (!gone[i]) nbs.push_back(bs[i]);
            bs.swap(nbs);
        }
        for (int c : destroyed) { g[c] = '.'; s.crateGone[c] = min(s.crateGone[c], k); }
        for (int c = 0; c < NN; c++) if (hit[c]) fl[c] = max(fl[c], t + FIRE - 1);
        s.leth[k].assign(NN, 0);
        for (int c = 0; c < NN; c++) s.leth[k][c] = fl[c] >= t;
    }
    return s;
}

// From a set of positions at end of step k0 (alive), BFS to end of horizon.
// Returns number of distinct cells alive at end (0 = dead), and last alive step.
struct ReachRes { int area; int life; };
ReachRes reachFrom(const Sim& s, int startCell, int k0, const vector<uint8_t>* blk = nullptr) {
    vector<uint8_t> cur(NN, 0), nxt(NN, 0);
    cur[startCell] = 1;
    int life = k0;
    for (int k = k0 + 1; k < s.H; k++) {
        fill(nxt.begin(), nxt.end(), 0);
        bool any = false;
        for (int c = 0; c < NN; c++) if (cur[c]) {
            if (s.wall[k][c]) continue;
            for (int d = 0; d < 5; d++) {
                int n = d == 4 ? c : nb(c, d);
                if (n < 0) continue;
                if (d != 4 && !s.pass[k][n]) continue;
                if (d != 4 && blk && k <= BODYK && blk[k - 1][n]) continue;
                if (OCC && d != 4 && k == 1 && !occ1.empty() && occ1[n]) continue;
                if (s.leth[k][n]) continue;
                nxt[n] = 1; any = true;
            }
        }
        if (!any) return {0, life};
        life = k;
        swap(cur, nxt);
    }
    int a = 0;
    for (int c = 0; c < NN; c++) a += cur[c];
    return {a, life};
}
// full evaluation for a player from start cell, free first move
ReachRes reachFree(const Sim& s, int c0) {
    int best = 0, life = -1;
    if (s.wall[0][c0]) return {0, -1};
    vector<uint8_t> cur(NN, 0), nxt(NN, 0);
    cur[c0] = 1;
    for (int k = 0; k < s.H; k++) {
        fill(nxt.begin(), nxt.end(), 0);
        bool any = false;
        for (int c = 0; c < NN; c++) if (cur[c]) {
            if (s.wall[k][c]) continue;
            for (int d = 0; d < 5; d++) {
                int n = d == 4 ? c : nb(c, d);
                if (n < 0) continue;
                if (d != 4 && !s.pass[k][n]) continue;
                if (s.leth[k][n]) continue;
                nxt[n] = 1; any = true;
            }
        }
        if (!any) return {0, life};
        life = k;
        swap(cur, nxt);
    }
    for (int c = 0; c < NN; c++) best += cur[c];
    return {best, life};
}

// outcome cells of my move d at step 0 given sim
int moveTarget(const Sim& s, int c, int d) {
    if (d == 4) return c;
    int n = nb(c, d);
    if (n < 0 || !s.pass[0][n]) return c;
    return n;
}

vector<int> recent;
int lastReq = 4, lastCell = -1, blockedStreak = 0;
unsigned rngState = 12345;
int myCell() { return pl[ME].y * N + pl[ME].x; }

vector<int> bfsDist(int src, const vector<uint8_t>& ok) {
    vector<int> d(NN, 1 << 20);
    deque<int> q; d[src] = 0; q.push_back(src);
    while (!q.empty()) {
        int c = q.front(); q.pop_front();
        for (int k = 0; k < 4; k++) {
            int n = nb(c, k);
            if (n < 0 || !ok[n] || d[n] <= d[c] + 1) continue;
            d[n] = d[c] + 1; q.push_back(n);
        }
    }
    return d;
}

int aliveCount() { int a = 0; for (auto& p : pl) a += p.alive; return a; }

pair<int, int> decide() {
    int me = myCell();
    int H = min(HMAX, max(2, LIMIT - T));
    H = min(H, 11);
    int alive = aliveCount();
    int myActive = 0; bool onBomb = false;
    vector<int> bombCnt(P, 0);
    vector<uint8_t> bombAt(NN, 0);
    for (auto& b : bombs) { bombCnt[b.owner]++; bombAt[b.c] = 1; }
    myActive = bombCnt[ME]; onBomb = bombAt[me];
    bool canPlace = myActive < CAP && !onBomb;

    vector<int> enemies;
    for (int i = 0; i < P; i++) if (i != ME && pl[i].alive) enemies.push_back(i);
    auto ecell = [&](int i) { return pl[i].y * N + pl[i].x; };
    auto manh = [&](int a, int b) { return abs(a % N - b % N) + abs(a / N - b / N); };

    // threatening enemies: can place now, close enough
    vector<int> threat;
    for (int e : enemies) {
        int c = ecell(e);
        if (bombCnt[e] < CAP && !bombAt[c] && manh(c, me) <= RANGE_ + 5) threat.push_back(e);
    }
    // nearby enemies for depth-2 threats
    vector<int> near2;
    for (int e : enemies) if (manh(ecell(e), me) <= RANGE_ + 3) near2.push_back(e);

    occ1.assign(NN, 0);
    for (int e : enemies) occ1[ecell(e)] = 1;
    Sim base = simulate(bombs, H);
    vector<uint8_t> bodyMask[BODYK];
    for (int k = 0; k < BODYK; k++) bodyMask[k].assign(NN, 0);
    bool hasBody = false;
    for (int e : enemies) {
        int ec = ecell(e);
        if (manh(ec, me) > 5) continue;
        hasBody = true;
        for (int c = 0; c < NN; c++) {
            int m = manh(c, ec);
            for (int k = 0; k < BODYK; k++) if (m <= k + 1) bodyMask[k][c] = 1;
        }
    }
    // doomed crates from existing bombs
    vector<uint8_t> doomed(NN, 0);
    for (int c = 0; c < NN; c++) if (base.crateGone[c] != INT_MAX) doomed[c] = 1;

    // baseline enemy escape areas
    vector<int> enemyBase(P, 0);
    for (int e : enemies) enemyBase[e] = reachFree(base, ecell(e)).area;

    // ---- navigation potential ----
    vector<uint8_t> walk(NN, 0);
    for (int c = 0; c < NN; c++) walk[c] = board[c / N][c % N] == '.' && !bombAt[c];
    walk[me] = 1;
    vector<double> val(NN, -1e9), shrinkPen(NN, 0);
    double aggress = AGG * (alive <= 2 ? 1.0 : (alive <= 3 ? 0.7 : 0.4));
    vector<vector<int>> edist;
    for (int e : enemies) edist.push_back(bfsDist(ecell(e), walk));
    vector<int> mydist = bfsDist(me, walk);
    int cratesLeft = 0;
    for (int c = 0; c < NN; c++) if (board[c / N][c % N] == '+' && !doomed[c]) cratesLeft++;
    for (int c = 0; c < NN; c++) {
        if (!walk[c]) continue;
        double v = 0;
        // crates this spot would blow
        int cr = 0;
        int x0 = c % N, y0 = c / N;
        for (int d = 0; d < 4; d++) for (int r = 1; r <= RANGE_; r++) {
            int x = x0 + DX[d] * r, y = y0 + DY[d] * r;
            if (x < 0 || y < 0 || x >= N || y >= N) break;
            char ch = board[y][x];
            if (ch == '#') break;
            if (ch == '+') { if (!doomed[y * N + x]) cr++; break; }
        }
        v += 1.6 * cr;
        // openness
        int deg = 0;
        for (int d = 0; d < 4; d++) { int n = nb(c, d); if (n >= 0 && board[n / N][n % N] != '#') deg++; }
        if (deg <= 1) v -= 0.8;
        // shrink safety
        int wt = wallTurn(c);
        int dd = mydist[c] < (1 << 20) ? mydist[c] : 50;
        if (wt >= 0 && wt - T < dd + 18) { v -= 60; shrinkPen[c] = 60; }
        else if (wt >= 0 && wt - T < dd + 45) { v -= 8; shrinkPen[c] = 8; }
        // centrality as shrink approaches
        double phase = T > SHRINK - 80 ? min(1.0, (T - (SHRINK - 80)) / 80.0) : 0.0;
        v += phase * 1.2 * edgeOf(c) + 0.08 * edgeOf(c);
        // enemy proximity
        for (size_t i = 0; i < enemies.size(); i++) {
            int ed = edist[i][c];
            if (ed >= (1 << 20)) {
                int m = manh(c, ecell(enemies[i]));
                if (m <= 3) v -= 0.5;
                continue;
            }
            if (ed <= 1) v -= 1.0 * (1.0 - aggress);
            else if (ed <= 4) v += aggress * (cratesLeft < 6 ? 3.0 : 1.2) * (5 - ed) / 3.0;
            else if (ed <= 10) v += aggress * (cratesLeft < 6 ? 1.5 : 0.3) * (11 - ed) / 6.0;
        }
        // danger from existing bombs: cells lethal soon
        for (int k = 0; k < min(H, 6); k++) if (base.leth[k][c]) { v -= 2.0; break; }
        val[c] = v;
    }
    // propagate potential
    vector<double> pot(NN, -1e9);
    {
        priority_queue<pair<double, int>> q;
        for (int c = 0; c < NN; c++) if (walk[c]) { pot[c] = val[c]; q.push({val[c], c}); }
        while (!q.empty()) {
            auto [v, c] = q.top(); q.pop();
            if (v < pot[c] - 1e-9) continue;
            for (int d = 0; d < 4; d++) {
                int n = nb(c, d);
                if (n < 0 || !walk[n]) continue;
                double nv = v - 0.75;
                if (nv > pot[n] + 1e-9) { pot[n] = nv; q.push({nv, n}); }
            }
        }
    }

    for (int c = 0; c < NN; c++) if (walk[c]) pot[c] -= shrinkPen[c];
    // ---- matrix search vs nearest enemies ----
    // mat[place*5+d] accumulated score adjustments
    vector<double> mat(10, 0.0);
    {
        vector<pair<int,int>> cand;
        for (int e : enemies) { int m = manh(ecell(e), me); if (m <= 6) cand.push_back({m, e}); }
        sort(cand.begin(), cand.end());
        int maxE = alive <= 3 ? 2 : 1;
        for (int ci = 0; ci < (int)cand.size() && ci < maxE; ci++) {
            if (elapsedMs() > 12) break;
            int e = cand[ci].second, ec = ecell(e);
            bool eCan = bombCnt[e] < CAP && !bombAt[ec];
            Sim sims[2][2];
            bool have[2][2] = {{false, false}, {false, false}};
            for (int p1 = 0; p1 < 2; p1++) for (int p2 = 0; p2 < 2; p2++) {
                if (p1 && !canPlace) continue;
                if (p2 && !eCan) continue;
                vector<Bomb> b = bombs;
                if (p1) b.push_back({ME, me, T + FUSE, 0});
                if (p2) b.push_back({e, ec, T + FUSE, 0});
                sims[p1][p2] = simulate(b, H); have[p1][p2] = true;
            }
            for (int p1 = 0; p1 < 2; p1++) for (int d1 = 0; d1 < 5; d1++) {
                if (!have[p1][0]) continue;
                int t1raw = moveTarget(sims[p1][0], me, d1);
                if (d1 != 4 && t1raw == me) continue;
                int total = 0, iDie = 0, heDies = 0, killOk = 0;
                for (int p2 = 0; p2 < 2; p2++) for (int d2i = 0; d2i < 5; d2i++) {
                    if (!have[p1][p2]) continue;
                    const Sim& sm = sims[p1][p2];
                    int t1 = moveTarget(sm, me, d1), t2 = moveTarget(sm, ec, d2i);
                    if (d2i != 4 && t2 == ec) continue;
                    int f1 = t1, f2 = t2;
                    if (t1 == t2) { f1 = me; f2 = ec; }
                    else {
                        if (t1 == ec && t2 == ec) f1 = me;
                        if (t2 == me && t1 == me) f2 = ec;
                    }
                    total++;
                    bool md = sm.leth[0][f1] || reachFrom(sm, f1, 0).area == 0;
                    bool ed = sm.leth[0][f2] || reachFrom(sm, f2, 0).area == 0;
                    if (md) iDie++;
                    if (ed) heDies++;
                    if (ed && !md) killOk++;
                }
                if (!total) continue;
                double adj = 0;
                if (iDie) adj -= 3e4 + 3e4 * iDie / total;
                if (killOk == total) adj += 2.5e5;
                else adj += PARTK * killOk / total;
                mat[p1 * 5 + d1] += adj;
            }
        }
    }
    // ---- evaluate actions ----
    double bestScore = -1e18; pair<int, int> best = {4, 0};
    double allSc[2][5]; for (auto& r : allSc) for (auto& v : r) v = -1e18;
    for (int place = 0; place <= 1; place++) {
        if (place && !canPlace) continue;
        vector<Bomb> bs = bombs;
        if (place) bs.push_back({ME, me, T + FUSE, 0});
        Sim sNone = place ? simulate(bs, H) : base;
        // union threat scenario
        vector<Bomb> bu = bs;
        for (int e : threat) bu.push_back({e, ecell(e), T + FUSE, 0});
        Sim sUnion = threat.empty() ? sNone : simulate(bu, H);
        vector<Sim> sInd;
        if (threat.size() > 1)
            for (int e : threat) { auto b1 = bs; b1.push_back({e, ecell(e), T + FUSE, 0}); sInd.push_back(simulate(b1, H)); }

        // bomb value
        double bombVal = 0;
        if (place) {
            int cr = 0;
            for (int c = 0; c < NN; c++) if (sNone.crateGone[c] != INT_MAX && !doomed[c]) cr++;
            bombVal += (cratesLeft > 0 ? 2.2 : 0) * cr - 1.0;
            for (int e : enemies) {
                int after = reachFree(sNone, ecell(e)).area;
                int bef = enemyBase[e];
                if (bef > 0 && after == 0) bombVal += 120;
                else if (bef > 0) {
                    double red = 1.0 - double(after) / bef;
                    bombVal += 12.0 * red * (0.5 + aggress);
                    if (after <= 2) bombVal += 10 * aggress;
                }
            }
        }

        vector<pair<int, Sim>> d2;
        for (int e : near2) {
            int ec = ecell(e);
            for (int ed = 0; ed < 5; ed++) {
                if (elapsedMs() > 14) break;
                int q = ed == 4 ? ec : nb(ec, ed);
                if (q < 0 || (!sNone.pass[0][q] && q != ec)) continue;
                if (q == me || manh(q, me) > RANGE_ + 2) continue;
                auto b2 = bs;
                bool canNow = bombCnt[e] == 0 && !bombAt[ec];
                if (canNow) b2.push_back({e, ec, T + FUSE, 0});
                if (!(canNow && q == ec)) b2.push_back({e, q, T + 1 + FUSE, 1});
                d2.push_back({q, simulate(b2, H)});
            }
        }
        for (int d = 0; d < 5; d++) {
            int tgt = moveTarget(sNone, me, d);
            if (d != 4 && tgt == me) continue;  // illegal move = stay (dup)
            // possible outcomes
            vector<int> outs = {tgt};
            if (tgt != me) {
                bool contested = false;
                for (int e : enemies) if (manh(ecell(e), tgt) <= 1) contested = true;
                if (contested) outs.push_back(me);
            }
            int bestArea = 0;
            auto evalSim = [&](const Sim& s, int& area, int& life, const vector<uint8_t>* blk = nullptr) {
                area = INT_MAX; life = INT_MAX; bestArea = 0;
                for (int o : outs) {
                    int oc = o;
                    // target could be blocked by a newly placed enemy bomb
                    if (oc != me && !s.pass[0][oc]) oc = me;
                    if (s.leth[0][oc]) { area = 0; life = min(life, -1); continue; }
                    auto r = reachFrom(s, oc, 0, blk);
                    area = min(area, r.area); life = min(life, r.life); bestArea = max(bestArea, r.area);
                }
            };
            double score = 0;
            int aN, lN, aU, lU;
            evalSim(sNone, aN, lN);
            int bestN = bestArea;
            if (hasBody) {
                int aB, lB; evalSim(sNone, aB, lB, bodyMask);
                if (aB == 0) score -= 4e4;
                else score += 3.0 * log(1.0 + min(aB, 30));
            }
            evalSim(sUnion, aU, lU);
            double indFrac = aU > 0 ? 1.0 : 0.0;
            int minInd = aU;
            if (!sInd.empty()) {
                int ok = 0; minInd = INT_MAX;
                for (auto& s : sInd) { int a, l; evalSim(s, a, l); ok += a > 0; minInd = min(minInd, a); }
                indFrac = double(ok) / sInd.size();
            }
            score += aN > 0 ? 1e7 : (bestN > 0 ? 5e6 : lN * 1e5);
            double unionW = threat.size() <= 2 ? 2e5 : 5e4;
            score += aU > 0 ? unionW : lU * unionW / 200;
            score += indFrac * 1e5;
            score += 6.0 * log(1.0 + min(aN, 40)) + 5.0 * log(1.0 + min(minInd, 40)) + 3.0 * log(1.0 + min(aU, 40));
            if (minInd <= 3) score -= 10;
            // depth-2: enemies near may place next turn after moving
            if (aN > 0 && !d2.empty()) {
                int bad = 0, tot = 0;
                for (auto& pr : d2) {
                    if (pr.first == tgt || manh(pr.first, tgt) > RANGE_ + 1) continue;
                    int a, l; evalSim(pr.second, a, l);
                    tot++; if (a == 0) bad++;
                }
                if (tot) score -= 3e4 * bad / tot + (bad ? D2FLAT : 0);
            }
            // navigation
            score += 1.0 * pot[tgt];
            int rc = 0;
            for (size_t i = 0; i < recent.size(); i++) if (recent[i] == tgt) rc++;
            score -= 0.35 * rc;
            if (d == lastReq && d != 4 && blockedStreak > 0) score -= 1.5 * blockedStreak + ((rngState >> (d + 3)) & 1) * 2.0;
            if (place) {
                score += bombVal;
                bool enemyNear = false;
                for (int e : enemies) if (manh(ecell(e), me) <= 7) enemyNear = true;
                int ar = min(aN, minInd);
                if (ar < 12) score -= (12 - ar) * (enemyNear ? PEN_NEAR : PEN_FAR);
            }
            score += mat[place * 5 + d];
            score += 0.001 * ((d * 7 + T) % 5);
            if (DBG) cerr << "  a=" << DS[d] << place << " aN=" << aN << " lN=" << lN << " aU=" << aU << " lU=" << lU << " ind=" << indFrac << " pot=" << pot[tgt] << " bv=" << bombVal << " sc=" << score << "\n";
            allSc[place][d] = score;
            if (score > bestScore) { bestScore = score; best = {d, place}; }
        }
    }
    // break symmetric collision deadlocks: sometimes yield instead of repeating a blocked move
    if (blockedStreak >= 1 && best.first == lastReq && best.first != 4 && ((rngState >> 7) & 1)) {
        double alt = -1e18; pair<int, int> ab = best;
        for (int d = 0; d < 5; d++)
            if (d != lastReq && allSc[0][d] > alt) { alt = allSc[0][d]; ab = {d, 0}; }
        if (bestScore < 9e6 && alt > 2e5 && alt > bestScore - 6e6) best = ab;
    }
    return best;
}

bool readTurn() {
    string w;
    if (!(cin >> w)) return false;
    if (w != "TURN") return false;
    cin >> T;
    cin >> w;  // BOARD
    for (auto& r : board) cin >> r;
    for (int i = 0; i < P; i++) { int id; cin >> w >> id; cin >> pl[id].x >> pl[id].y >> pl[id].alive; }
    int k; cin >> w >> k;
    bombs.clear();
    for (int i = 0; i < k; i++) { int o, x, y, a; cin >> w >> o >> x >> y >> a; bombs.push_back({o, y * N + x, a, 0}); }
    cin >> w >> k;
    flameEnd.assign(NN, -1);
    for (int i = 0; i < k; i++) { int x, y, e; cin >> w >> x >> y >> e; flameEnd[y * N + x] = max(flameEnd[y * N + x], e); }
    while (cin >> w) { if (w == "END") return true; string line; getline(cin, line); }
    return false;
}

int main() {
    ios::sync_with_stdio(false); cin.tie(nullptr);
    string w;
    if (!(cin >> w >> N >> P >> ME >> LIMIT >> FUSE >> RANGE_ >> CAP >> FIRE >> SHRINK >> EVERY)) return 0;
    NN = N * N;
    board.resize(N); pl.resize(P);
    while (readTurn()) {
        tStart = chrono::steady_clock::now();
        pair<int, int> a = {4, 0};
        if (lastCell == myCell() && lastReq != 4) blockedStreak++; else blockedStreak = 0;
        rngState = rngState * 1103515245u + 12345u + T;
        if (pl[ME].alive) a = decide();
        lastReq = a.first; lastCell = myCell();
        recent.push_back(myCell());
        if (recent.size() > 12) recent.erase(recent.begin());
        cout << T << ' ' << DS[a.first] << ' ' << a.second << '\n';
        cout.flush();
        if (DBG) cerr << "T" << T << " ms " << elapsedMs() << "\n";
    }
    return 0;
}
