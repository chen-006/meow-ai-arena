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
vector<int> deathT;      // STATUS: death turn per player, -1 alive
vector<double> killC;    // STATUS: accumulated kill credit
struct Src { int c, owner, end; };
vector<Src> sources;     // SOURCES: live flame provenance
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
#ifndef AGG2
#define AGG2 1.0
#endif
#ifndef AGG2_P3
#define AGG2_P3 AGG2  // AGG2 in games that start with 3 players
#endif
#ifndef AGG2_P8
#define AGG2_P8 AGG2  // AGG2 in games that start with 8 players
#endif
#ifndef AGG3
#define AGG3 -1.0
#endif
#ifndef AGG3_P3
#define AGG3_P3 AGG3  // AGG3 in games that start with 3 players
#endif
#ifndef LATE_AGG
#define LATE_AGG 0.0  // aggression in the shrink phase with 4..LATE_ALIVE alive
#endif
#ifndef LATE_MASK
#define LATE_MASK 1104  // starting player counts (bit P) that use LATE_AGG: 4p, 6p, 10p (0 = off)
#endif
#ifndef LATE_T
#define LATE_T 0
#endif
#ifndef LATE_ALIVE
#define LATE_ALIVE 6
#endif
// BAGG_MIN (undefined by default): floor on the aggression used in the bomb score (trapping enemies)
#ifndef BAGG_T
#define BAGG_T -100000  // BAGG_MIN from turn SHRINK + BAGG_T
#endif
#ifndef BAGG_PMIN
#define BAGG_PMIN 2  // BAGG_MIN only in games that start with this many players or more
#endif
#ifndef AGG_MANY
#define AGG_MANY -3.0
#endif
// AGG_MANY by starting player count (formal schedule, per-size points over two runs each)
#ifndef AGG_P7
#define AGG_P7 -2.5
#endif
#ifndef AGG_P8
#define AGG_P8 -3.0
#endif
#ifndef AGG_P9
#define AGG_P9 -2.0
#endif
#ifndef AGG_P10
#define AGG_P10 -2.0
#endif
#ifndef SHR_W
#define SHR_W 3e5  // score penalty for stepping onto a cell that becomes wall within SHM1 turns of reaching it
#endif
#ifndef SHR_W_BIG
#define SHR_W_BIG SHR_W  // SHR_W in games that start with 7+ players
#endif
#ifndef DEAD_W
#define DEAD_W 0  // penalty for ending the turn in a dead end (at most one open exit) with an enemy within DEAD_R
#endif
#ifndef DEAD_R
#define DEAD_R 2
#endif
#ifndef SHM1
#define SHM1 18
#endif
#ifndef SHM2
#define SHM2 45
#endif
#ifndef CRATE_K
#define CRATE_K 1.0
#endif
#ifndef CENT
#define CENT 0.0
#endif
#ifndef ROBW
#define ROBW 5e6
#endif
#ifndef ROBK
#define ROBK 2
#endif
#ifndef ROB_ER
#define ROB_ER 7
#endif
#ifndef ROB_QR
#define ROB_QR 4
#endif
#ifndef ROB_PAIR
#define ROB_PAIR 0
#endif
#ifndef ROB_PAIR_BIG
#define ROB_PAIR_BIG 0.6  // in the shrink phase of games that start with ROB_PAIR_P+ players
#endif
#ifndef ROB_PAIR_P
#define ROB_PAIR_P 8  // 7p got worse with it (screening 7-10p, 2026-11-04)
#endif
#ifndef ROB_PAIR_K
#define ROB_PAIR_K 1  // pair placements up to this many turns ahead (<= ROBK)
#endif
#ifndef RET_PEN
#define RET_PEN 1e6  // own bomb whose every reachable hiding cell is walled soon after the blast (must leave and return)
#endif
#ifndef RET_M
#define RET_M 6    // a hiding cell must stay open this many turns after the flames end
#endif
#ifndef RET_ALIVE
#define RET_ALIVE 2  // apply RET_PEN with at most this many players alive
#endif
#ifndef RET_A3_MASK
#define RET_A3_MASK 288  // starting player counts (bit P) that apply RET_PEN with 3 alive too
#endif
#ifndef ROB_PAIR_T
#define ROB_PAIR_T 0  // ROB_PAIR_BIG from turn SHRINK + ROB_PAIR_T
#endif
#ifndef ROB_PQ
#define ROB_PQ 3
#endif
#ifndef CORR_PEN
#define CORR_PEN 0
#endif
#ifndef CORR_R
#define CORR_R 3
#endif
#ifndef BODY_FIX
#define BODY_FIX 1
#endif
#ifndef BODY_PW
#define BODY_PW 4e4
#endif
#ifndef ROB_MS
#define ROB_MS 25
#endif
#ifndef D2W
#define D2W 3e4
#endif
#ifndef UNION_W
#define UNION_W 2e5
#endif
#ifndef IND_W
#define IND_W 1e5
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
                if (d != 4 && blk && k <= BODYK && blk[min(k - 1 + BODY_FIX, BODYK - 1)][n]) continue;  // moves are simultaneous: at step k an enemy may already be k+1 away
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

// ---- exact model for up to 3 players (mirrors the referee's step, phases 1-5) ----
// Model player i is global id gid[i] (model player 0 = me). Bombs and flames keep global owner ids, so bombs of
// players outside the model (dead ones) still work. The model must contain every live player.
// Fixed-size state: a search node copies only the used part, with no allocation.
#ifndef ENDG_FREE
#define ENDG_FREE 60
#endif
#ifndef ENDG3_FREE
#define ENDG3_FREE 21
#endif
#ifndef PRESS
#define PRESS 1
#endif
#ifndef PRESS_DEEPSAFE
#define PRESS_DEEPSAFE 0
#endif
#ifndef PRESS_MS
#define PRESS_MS 6
#endif
#ifndef PRESS_DEPTH
#define PRESS_DEPTH 3
#endif
#ifndef ENDG_MS
#define ENDG_MS 32
#endif
const int KMAX = 3, BMAX = 24, FMAX = 320, CMAX = 625;
struct Ent { int16_t a, b, c; };  // bomb: owner, cell, explode_at; flame: cell, owner, last lethal turn
struct G2 {
    int t, K, nb = 0, nf = 0;
    int pos[KMAX], death[KMAX];  // death: turn of death, -1 alive
    double kill[KMAX];
    bitset<CMAX> crate;
    Ent bomb[BMAX], flame[FMAX];
    G2() {}
    G2(const G2& o) { *this = o; }
    G2& operator=(const G2& o) {
        t = o.t; K = o.K; nb = o.nb; nf = o.nf;
        for (int i = 0; i < KMAX; i++) { pos[i] = o.pos[i]; death[i] = o.death[i]; kill[i] = o.kill[i]; }
        crate = o.crate;
        memcpy(bomb, o.bomb, nb * sizeof(Ent)); memcpy(flame, o.flame, nf * sizeof(Ent));
        return *this;
    }
    bool alive(int i) const { return death[i] < 0; }
    int aliveN() const { int n = 0; for (int i = 0; i < K; i++) n += alive(i); return n; }
    bool bombAt(int c) const { for (int i = 0; i < nb; i++) if (bomb[i].b == c) return true; return false; }
    bool flameOn(int c, int t) const { for (int i = 0; i < nf; i++) if (flame[i].a == c && flame[i].c >= t) return true; return false; }
};
int gid[KMAX];
vector<uint8_t> baseWall;
inline bool wallAt(int c, int t) { return baseWall[c] || edgeOf(c) <= layerAt(t); }
inline bool shrinkTurn(int t) { return t >= SHRINK && (t - SHRINK) % EVERY == 0; }

void g2Step(G2& s, const int dir[], const int place[]) {
    int t = s.t, K = s.K;
    auto keepFlames = [&](auto pred) { int n = 0; for (int i = 0; i < s.nf; i++) if (pred(s.flame[i])) s.flame[n++] = s.flame[i]; s.nf = n; };
    // 1. expire flames; shrink
    keepFlames([&](const Ent& f) { return f.c >= t; });
    if (shrinkTurn(t)) {
        int L = layerAt(t);
        keepFlames([&](const Ent& f) { return edgeOf(f.a) > L; });
        int n = 0;
        for (int i = 0; i < s.nb; i++) if (edgeOf(s.bomb[i].b) > L) s.bomb[n++] = s.bomb[i];
        s.nb = n;
        for (int i = 0; i < K; i++) if (s.alive(i) && edgeOf(s.pos[i]) <= L) s.death[i] = t;
        for (int c = 0; c < NN; c++) if (s.crate[c] && edgeOf(c) <= L) s.crate[c] = 0;
    }
    // 2. place at the pre-move cell (live players never share a cell)
    for (int i = 0; i < K; i++) {
        if (!s.alive(i) || !place[i]) continue;
        int c = s.pos[i], cnt = 0;
        for (int j = 0; j < s.nb; j++) cnt += s.bomb[j].a == gid[i];
        if (!s.bombAt(c) && cnt < CAP && s.nb < BMAX) s.bomb[s.nb++] = {(int16_t)gid[i], (int16_t)c, (int16_t)(t + FUSE)};
    }
    // 3. move: terrain first, then equal targets stop, then stopping propagates; swaps and cycles allowed
    int org[KMAX], tgt[KMAX];
    for (int i = 0; i < K; i++) {
        org[i] = tgt[i] = s.pos[i];
        if (!s.alive(i) || dir[i] == 4) continue;
        int n = nb(s.pos[i], dir[i]);
        if (n >= 0 && !wallAt(n, t) && !s.crate[n] && !s.bombAt(n)) tgt[i] = n;
    }
    int res[KMAX];
    for (int i = 0; i < K; i++) {
        res[i] = tgt[i];
        if (!s.alive(i)) continue;
        for (int j = 0; j < K; j++) if (j != i && s.alive(j) && tgt[j] == tgt[i]) res[i] = org[i];
    }
    for (bool ch = true; ch;) {
        ch = false;
        for (int i = 0; i < K; i++) {
            if (!s.alive(i) || res[i] == org[i]) continue;
            for (int j = 0; j < K; j++)
                if (j != i && s.alive(j) && res[j] == org[j] && res[i] == org[j]) { res[i] = org[i]; ch = true; break; }
        }
    }
    for (int i = 0; i < K; i++) if (s.alive(i)) s.pos[i] = res[i];
    // 4. explosions on one pre-explosion snapshot
    int nbmb = s.nb;
    char gone[BMAX] = {0};
    int q[BMAX * 4], qh = 0, qt = 0;
    for (int i = 0; i < nbmb; i++) if (s.bomb[i].c <= t || s.flameOn(s.bomb[i].b, t)) q[qt++] = i;
    static pair<int16_t, int16_t> hits[BMAX * 16];  // cell, owner
    int nh = 0;
    int destroyed[BMAX * 4], nd = 0;
    while (qh < qt) {
        int i = q[qh++];
        if (gone[i]) continue;
        gone[i] = 1;
        int o = s.bomb[i].a, c0 = s.bomb[i].b;
        hits[nh++] = {(int16_t)c0, (int16_t)o};
        for (int d = 0; d < 4; d++) {
            int c = c0;
            for (int r = 1; r <= RANGE_; r++) {
                c = nb(c, d);
                if (c < 0 || wallAt(c, t)) break;
                hits[nh++] = {(int16_t)c, (int16_t)o};
                if (s.crate[c]) { destroyed[nd++] = c; break; }
                int hb = -1;
                for (int j = 0; j < nbmb; j++) if (s.bomb[j].b == c) hb = j;
                if (hb >= 0) { if (qt < BMAX * 4) q[qt++] = hb; break; }
            }
        }
    }
    // 5. remove, add flames, kill
    {
        int n = 0;
        for (int i = 0; i < nbmb; i++) if (!gone[i]) s.bomb[n++] = s.bomb[i];
        s.nb = n;
    }
    for (int k = 0; k < nd; k++) s.crate[destroyed[k]] = 0;
    int end = t + FIRE - 1;
    for (int k = 0; k < nh; k++) {
        auto [c, o] = hits[k];
        bool found = false;
        for (int i = 0; i < s.nf; i++) if (s.flame[i].a == c && s.flame[i].b == o) { s.flame[i].c = max<int>(s.flame[i].c, end); found = true; }
        if (!found && s.nf < FMAX) s.flame[s.nf++] = {c, o, (int16_t)end};
    }
    int newDead[KMAX] = {0};
    for (int i = 0; i < K; i++) if (s.alive(i) && s.flameOn(s.pos[i], t)) newDead[i] = 1;
    for (int i = 0; i < K; i++) if (newDead[i]) {
        int owners[16], no = 0;
        for (int k = 0; k < s.nf; k++) {
            const Ent& f = s.flame[k];
            if (f.a != s.pos[i] || f.c < t || f.b == gid[i]) continue;
            bool dup = false; for (int m = 0; m < no; m++) dup |= owners[m] == f.b;
            if (!dup && no < 16) owners[no++] = f.b;
        }
        for (int m = 0; m < no; m++) for (int j = 0; j < K; j++) if (gid[j] == owners[m]) s.kill[j] += 1.0 / no;
        s.death[i] = t;
    }
    s.t = t + 1;
}

bool g2Over(const G2& s) { return s.t >= LIMIT || s.aliveN() <= 1; }
// Sum over opponents j of sign(my points - points of j); points = survival among model players + 0.5 x kills.
// Pairs where both are still alive count 0 unless the game is over.
int g2Util(const G2& s) {
    bool over = g2Over(s);
    double pts[KMAX];
    auto dt = [&](int i) { return s.alive(i) ? INT_MAX : s.death[i]; };
    for (int i = 0; i < s.K; i++) {
        pts[i] = 0.5 * s.kill[i];
        for (int k = 0; k < s.K; k++) if (k != i) pts[i] += dt(i) > dt(k) ? 1 : (dt(i) == dt(k) ? 0.5 : 0);
    }
    int u = 0;
    for (int j = 1; j < s.K; j++) {
        if (!over && s.alive(0) && s.alive(j)) continue;
        double d = pts[0] - pts[j];
        u += d > 1e-9 ? 1 : (d < -1e-9 ? -1 : 0);
    }
    return u;
}

G2 g2FromInput(const vector<int>& opps) {
    G2 s;
    s.t = T; s.K = 1 + opps.size(); gid[0] = ME;
    for (size_t i = 0; i < opps.size(); i++) gid[i + 1] = opps[i];
    for (int i = 0; i < s.K; i++) { s.pos[i] = pl[gid[i]].y * N + pl[gid[i]].x; s.death[i] = pl[gid[i]].alive ? -1 : deathT[gid[i]]; s.kill[i] = killC[gid[i]]; }
    for (auto& b : bombs) if (s.nb < BMAX) s.bomb[s.nb++] = {(int16_t)b.owner, (int16_t)b.c, (int16_t)b.at};
    for (auto& f : sources) if (s.nf < FMAX) s.flame[s.nf++] = {(int16_t)f.c, (int16_t)f.owner, (int16_t)f.end};
    baseWall.assign(NN, 0);
    for (int c = 0; c < NN; c++) { char ch = board[c / N][c % N]; s.crate[c] = ch == '+'; baseWall[c] = ch == '#'; }
    return s;
}

// ---- endgame search: paranoid maximin (the opponents answer jointly, knowing my action) ----
struct G2Search {
    bool timeout = false;
    long nodes = 0;
    unordered_map<uint64_t, pair<int, int>> tt;  // hash -> (depth, value)
    static uint64_t sm(uint64_t x) { x += 0x9e3779b97f4a7c15ULL; x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL; x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL; return x ^ (x >> 31); }
    static uint64_t pack(const Ent& e, uint64_t tag) { return tag << 60 | (uint64_t)(uint16_t)e.a << 32 | (uint64_t)(uint16_t)e.b << 16 | (uint16_t)e.c; }
    uint64_t hashOf(const G2& s) {
        uint64_t h = sm(s.t);
        for (int i = 0; i < s.K; i++) h = sm(h ^ ((uint64_t)s.pos[i] << 40 | (uint64_t)(s.death[i] + 7) << 20 | (uint64_t)llround(s.kill[i] * 1000)));
        uint64_t sb = 0, sf = 0;  // order-independent sums
        for (int i = 0; i < s.nb; i++) sb += sm(pack(s.bomb[i], 1));
        for (int i = 0; i < s.nf; i++) sf += sm(pack(s.flame[i], 2));
        h = sm(h ^ sb); h = sm(h ^ sf);
        return h ^ hash<bitset<CMAX>>()(s.crate);
    }
    // distinct actions for model player i: moves that stay put collapse into S
    vector<pair<int, int>> actions(const G2& s, int i) {
        vector<pair<int, int>> a;
        if (!s.alive(i)) { a.push_back({4, 0}); return a; }
        int cnt = 0;
        for (int j = 0; j < s.nb; j++) cnt += s.bomb[j].a == gid[i];
        bool canP = cnt < CAP && !s.bombAt(s.pos[i]);
        for (int p = canP ? 1 : 0; p >= 0; p--)
            for (int d = 0; d < 5; d++) {
                if (d != 4) {
                    int n = nb(s.pos[i], d);
                    if (n < 0 || wallAt(n, s.t) || s.crate[n] || s.bombAt(n)) continue;
                }
                a.push_back({d, p});
            }
        return a;
    }
    using Joint = array<pair<int, int>, KMAX>;
    // joint opponent actions: cartesian product over model players 1..K-1
    vector<Joint> joint(const G2& s) {
        vector<Joint> out(1);
        for (int i = 1; i < s.K; i++) {
            auto ai = actions(s, i);
            vector<Joint> nx;
            for (auto& o : out) for (auto& x : ai) { auto y = o; y[i] = x; nx.push_back(y); }
            out.swap(nx);
        }
        return out;
    }
    void apply(G2& c, const pair<int, int>& a, const Joint& b) {
        int dir[KMAX], pl2[KMAX];
        dir[0] = a.first; pl2[0] = a.second;
        for (int i = 1; i < c.K; i++) { dir[i] = b[i].first; pl2[i] = b[i].second; }
        g2Step(c, dir, pl2);
    }
    int value(const G2& s, int depth) {
        if (g2Over(s) || depth == 0) return g2Util(s);
        if ((++nodes & 31) == 0 && elapsedMs() > ENDG_MS) timeout = true;
        if (timeout) return 0;
        int full = s.K - 1;  // |value| == K-1 is a proof that deeper search cannot change
        uint64_t h = hashOf(s);
        auto it = tt.find(h);
        if (it != tt.end() && (it->second.first >= depth || abs(it->second.second) == full)) return it->second.second;
        auto A = actions(s, 0);
        auto B = joint(s);
        int best = -KMAX;
        G2 c;
        for (auto& a : A) {
            int worst = KMAX;
            for (auto& b : B) {
                c = s;
                apply(c, a, b);
                worst = min(worst, value(c, depth - 1));
                if (worst <= best || worst == -full) break;
            }
            best = max(best, worst);
            if (best == full) break;
        }
        if (!timeout) tt[h] = {depth, best};
        return best;
    }
};

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
    double aggress = AGG * (alive <= 2 ? (P == 3 ? AGG2_P3 : P == 8 ? AGG2_P8 : AGG2) : (alive <= 3 ? (P == 3 ? AGG3_P3 : AGG3) : (P >= 10 ? AGG_P10 : P == 9 ? AGG_P9 : P == 8 ? AGG_P8 : P == 7 ? AGG_P7 : AGG_MANY)));
    if (((LATE_MASK >> P) & 1) && T >= SHRINK + LATE_T && alive >= 4 && alive <= LATE_ALIVE) aggress = AGG * LATE_AGG;
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
        v += 1.6 * CRATE_K * cr;
        // openness
        int deg = 0;
        for (int d = 0; d < 4; d++) { int n = nb(c, d); if (n >= 0 && board[n / N][n % N] != '#') deg++; }
        if (deg <= 1) v -= 0.8;
        // shrink safety
        int wt = wallTurn(c);
        int dd = mydist[c] < (1 << 20) ? mydist[c] : 50;
        if (wt >= 0 && wt - T < dd + SHM1) { v -= 60; shrinkPen[c] = 60; }
        else if (wt >= 0 && wt - T < dd + SHM2) { v -= 8; shrinkPen[c] = 8; }
        // centrality as shrink approaches
        double phase = T > SHRINK - 80 ? min(1.0, (T - (SHRINK - 80)) / 80.0) : 0.0;
        v += phase * CENT * edgeOf(c) + 0.08 * edgeOf(c);
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
    // ---- robust check: enemy plans over the next ROBK turns ----
    // Each scenario adds enemy bombs placed at cells the enemy can reach by step k. All actions are scored on the
    // same scenario set (scenario-major loop), so a time cut-off never favours one action over another.
    double robOk[2][5] = {{0}}, robTot = 0;
    {
        Sim sP[2]; sP[0] = base;
        if (canPlace) { auto b = bombs; b.push_back({ME, me, T + FUSE, 0}); sP[1] = simulate(b, H); }
        int tg[2][5]; vector<int> outs[2][5];
        for (int p = 0; p < 2; p++) for (int d = 0; d < 5; d++) {
            tg[p][d] = -1;
            if (p && !canPlace) continue;
            int t = moveTarget(sP[p], me, d);
            if (d != 4 && t == me) continue;
            tg[p][d] = t; outs[p][d] = {t};
            if (t != me) for (int e : enemies) if (manh(ecell(e), t) <= 1) { outs[p][d].push_back(me); break; }
        }
        struct Scn { vector<Bomb> add; double w; int dist; };
        vector<Scn> scn;
        vector<pair<int, vector<Bomb>>> pairOpts;  // per enemy: single placements now or next turn, close to me
        const double robPair = (P >= ROB_PAIR_P && T >= SHRINK + ROB_PAIR_T) ? ROB_PAIR_BIG : ROB_PAIR;
        for (int e : enemies) {
            int ec = ecell(e), dist = manh(ec, me);
            if (dist > RANGE_ + ROB_ER) continue;
            auto avail = [&](int k) { int n = 0; for (auto& b : bombs) if (b.owner == e && b.at >= T + k) n++; return CAP - n; };
            // cells the enemy can stand on at step k (ignoring other players; pessimistic)
            vector<vector<int>> R(ROBK + 1);
            vector<int> seen(NN, -1);
            R[0] = {ec}; seen[ec] = 0;
            for (int k = 1; k <= ROBK; k++) {
                R[k] = R[k - 1];
                for (int c : R[k - 1]) for (int d = 0; d < 4; d++) {
                    int n = nb(c, d);
                    if (n < 0 || seen[n] >= 0 || !base.pass[k - 1][n] || base.leth[k - 1][n]) continue;
                    seen[n] = k; R[k].push_back(n);
                }
            }
            bool now = avail(0) >= 1 && !bombAt[ec];
            if (now) scn.push_back({{{e, ec, T + FUSE, 0}}, 1.0, dist});
            for (int k = 1; k <= ROBK; k++) for (int q : R[k]) {
                if (seen[q] != k && !(q == ec)) continue;  // first reachable at step k (staying put is covered too)
                if (manh(q, me) > RANGE_ + ROB_QR) continue;
                double w = k == 1 ? 0.8 : 0.5;
                if (avail(k) >= 1) scn.push_back({{{e, q, T + k + FUSE, k}}, w, dist + k});
                if (now && q != ec && avail(k) >= 2) scn.push_back({{{e, ec, T + FUSE, 0}, {e, q, T + k + FUSE, k}}, w * 0.8, dist + k});
            }
            if (robPair > 0 && dist <= RANGE_ + ROB_QR) {
                vector<Bomb> o;
                if (now && manh(ec, me) <= ROB_PQ) o.push_back({e, ec, T + FUSE, 0});
                for (int k = 1; k <= min(ROBK, ROB_PAIR_K); k++) if (avail(k) >= 1)
                    for (int q : R[k]) if ((seen[q] == k || q == ec) && manh(q, me) <= ROB_PQ) o.push_back({e, q, T + k + FUSE, k});
                if (!o.empty()) pairOpts.push_back({dist, o});
            }
        }
        // two enemies acting together (crossfire, both ends of a corridor): one placement each, now or next turn
        for (size_t i = 0; i < pairOpts.size(); i++)
            for (size_t j = i + 1; j < pairOpts.size(); j++)
                for (auto& a : pairOpts[i].second)
                    for (auto& b : pairOpts[j].second)
                        scn.push_back({{a, b}, robPair * pow(0.8, a.placed) * pow(0.8, b.placed),
                                       min(pairOpts[i].first, pairOpts[j].first) + a.placed + b.placed});
        stable_sort(scn.begin(), scn.end(), [](const Scn& a, const Scn& b) { return a.dist < b.dist; });
        for (auto& sc : scn) {
            if (elapsedMs() > ROB_MS) break;
            double got[2][5] = {{0}};
            for (int p = 0; p < 2; p++) {
                if (p && !canPlace) continue;
                auto b = bombs;
                if (p) b.push_back({ME, me, T + FUSE, 0});
                b.insert(b.end(), sc.add.begin(), sc.add.end());
                Sim s = simulate(b, H);
                for (int d = 0; d < 5; d++) {
                    if (tg[p][d] < 0) continue;
                    bool ok = true;
                    for (int o : outs[p][d]) {
                        int oc = (o != me && !s.pass[0][o]) ? me : o;
                        if (s.leth[0][oc] || reachFrom(s, oc, 0).area == 0) { ok = false; break; }
                    }
                    got[p][d] = ok ? sc.w : 0;
                }
            }
            for (int p = 0; p < 2; p++) for (int d = 0; d < 5; d++) robOk[p][d] += got[p][d];
            robTot += sc.w;
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
        // Return check: hiding from our own bomb must not need cells the shrink takes right after the blast; coming
        // back in is a contest the enemy wins by stepping into the entry cell (formal 8p duels lost at the shrink).
        bool noHaven = false;
        if (place && RET_PEN > 0 && alive <= (((RET_A3_MASK >> P) & 1) ? 3 : RET_ALIVE)) {
            const int jEnd = min(sNone.H - 1, FUSE + FIRE);
            vector<uint8_t> eCell(NN, 0);
            for (int e : enemies) eCell[ecell(e)] = 1;
            vector<int> cur = {me}; vector<uint8_t> vis(NN, 0); vis[me] = 1;
            noHaven = true;
            for (int k = 0; k <= FUSE && noHaven; k++) {
                for (int c : cur) {
                    int wt = wallTurn(c);
                    bool open = wt < 0 ? false : wt > T + FUSE + FIRE + RET_M;
                    if (!open) continue;
                    bool safe = true;
                    for (int j = max(0, k - 1); j <= jEnd && safe; j++) safe = !sNone.leth[j][c];
                    if (safe) { noHaven = false; break; }
                }
                if (k == FUSE || !noHaven) break;
                vector<int> nxt;
                for (int c : cur) {
                    if (!sNone.leth[k][c]) nxt.push_back(c);
                    for (int d = 0; d < 4; d++) {
                        int n = nb(c, d);
                        if (n < 0 || vis[n] || eCell[n] || !sNone.pass[k][n] || sNone.leth[k][n]) continue;
                        vis[n] = 1; nxt.push_back(n);
                    }
                }
                sort(nxt.begin(), nxt.end()); nxt.erase(unique(nxt.begin(), nxt.end()), nxt.end());
                cur = nxt;
            }
        }
        // union threat scenario
        vector<Bomb> bu = bs;
        for (int e : threat) bu.push_back({e, ecell(e), T + FUSE, 0});
        Sim sUnion = threat.empty() ? sNone : simulate(bu, H);
        vector<Sim> sInd;
        if (threat.size() > 1)
            for (int e : threat) { auto b1 = bs; b1.push_back({e, ecell(e), T + FUSE, 0}); sInd.push_back(simulate(b1, H)); }

        // bomb value
        double bombVal = 0;
#ifdef BAGG_MIN
        const double baggr = (P >= BAGG_PMIN && T >= SHRINK + BAGG_T) ? max(aggress, (double)BAGG_MIN) : aggress;
#else
        const double baggr = aggress;
#endif
        if (place) {
            int cr = 0;
            for (int c = 0; c < NN; c++) if (sNone.crateGone[c] != INT_MAX && !doomed[c]) cr++;
            bombVal += (cratesLeft > 0 ? 2.2 * CRATE_K : 0) * cr - 1.0;
            for (int e : enemies) {
                int after = reachFree(sNone, ecell(e)).area;
                int bef = enemyBase[e];
                if (bef > 0 && after == 0) bombVal += 120;
                else if (bef > 0) {
                    double red = 1.0 - double(after) / bef;
                    bombVal += 12.0 * red * (0.5 + baggr);
                    if (after <= 2) bombVal += 10 * baggr;
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
                if (aB == 0) score -= place ? BODY_PW : 4e4;  // a placed bomb is a commitment: an escape that needs an enemy to make way is no escape
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
            double unionW = threat.size() <= 2 ? UNION_W : UNION_W / 4;
            score += aU > 0 ? unionW : lU * unionW / 200;
            score += indFrac * IND_W;
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
                if (tot) score -= D2W * bad / tot + (bad ? D2FLAT : 0);
            }
            // navigation
            score += 1.0 * pot[tgt];
            if ((P >= 7 ? SHR_W_BIG : SHR_W) > 0 && shrinkPen[tgt] >= 60) score -= (P >= 7 ? SHR_W_BIG : SHR_W);
            if (DEAD_W > 0) {
                // a dead end next to an enemy is sealed by one bomb or one body: most early deaths start there
                int exits = 0, en = INT_MAX;
                for (int k = 0; k < 4; k++) { int n = nb(tgt, k); if (n >= 0 && walk[n] && n != me) exits++; }
                if (tgt != me) exits++;  // the cell we came from stays open unless we bomb it
                if (place && tgt != me) exits--;
                for (int e : enemies) en = min(en, manh(ecell(e), tgt));
                if (exits <= 1 && en <= DEAD_R) score -= DEAD_W;
            }
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
            if (robTot > 0) score += ROBW * robOk[place][d] / robTot;
            // Passage trap: with two enemies close, a cell whose exits are all within an enemy's reach can be sealed
            // from both ends on different turns even when each single bomb leaves an escape.
            if (CORR_PEN > 0 && enemies.size() >= 2) {
                int closeE = 0;
                for (int e : enemies) closeE += manh(ecell(e), tgt) <= CORR_R;
                if (closeE >= 2) {
                    int freeExits = 0;
                    for (int dd = 0; dd < 4; dd++) {
                        int n = nb(tgt, dd);
                        if (n < 0 || board[n / N][n % N] != '.' || bombAt[n]) continue;
                        bool guarded = false;
                        for (int e : enemies) guarded |= manh(ecell(e), n) <= 1;
                        freeExits += !guarded;
                    }
                    if (freeExits <= 1) score -= CORR_PEN * (2 - freeExits);
                }
            }
            if (noHaven) score -= RET_PEN;
            score += mat[place * 5 + d];
            score += 0.001 * ((d * 7 + T) % 5);
            if (DBG) cerr << "  a=" << DS[d] << place << " aN=" << aN << " lN=" << lN << " aU=" << aU << " lU=" << lU << " ind=" << indFrac << " pot=" << pot[tgt] << " bv=" << bombVal << " rob=" << (robTot > 0 ? robOk[place][d] / robTot : -1) << " mat=" << mat[place * 5 + d] << " sc=" << score << "\n";
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
    // ---- endgame: exact search when two (or, if enabled, three) players are left on a small board ----
    int freeCells = 0;
    for (int c = 0; c < NN; c++) freeCells += board[c / N][c % N] != '#';
    bool endg = (enemies.size() == 1 && ENDG_FREE > 0 && freeCells <= ENDG_FREE) ||
                (enemies.size() == 2 && ENDG3_FREE > 0 && freeCells <= ENDG3_FREE);
    if (endg) {
        G2 root = g2FromInput(enemies);
        G2Search S;
        int full = root.K - 1;
        auto A = S.actions(root, 0);
        auto B = S.joint(root);
        // val: deepest completed search. valP/pressP: the deepest search completed within PRESS_MS of search time
        // (at least depth PRESS_DEPTH); press is the sum of values over all opponent replies. Pressure must not use
        // deeper looks: they find traps a strong opponent never walks into, and acting on them gave such opponents
        // the game. The time cap keeps the pressure depth growing as the board shrinks.
        vector<int> val(A.size(), 0), valP(A.size(), 0), pressP(A.size(), 0);
        int doneDepth = 0;
        double searchStart = elapsedMs();
        for (int D = 1; D <= LIMIT - T; D++) {
            vector<int> v(A.size(), KMAX), w(A.size(), 0);
            for (size_t i = 0; i < A.size() && !S.timeout; i++)
                for (auto& b : B) {
                    G2 c = root;
                    S.apply(c, A[i], b);
                    int x = S.value(c, D - 1);
                    v[i] = min(v[i], x); w[i] += x;
                    if (v[i] == -full || S.timeout) break;
                }
            if (S.timeout) break;
            val = v; doneDepth = D;
            if (D <= PRESS_DEPTH || elapsedMs() - searchStart <= PRESS_MS) { valP = v; pressP = w; }
            bool decided = true;
            for (int x : val) decided &= abs(x) == full;
            if (decided || *max_element(val.begin(), val.end()) == full) break;
        }
        if (doneDepth > 0) {
            auto sc = [&](size_t i) { return allSc[A[i].second][A[i].first]; };
            int ci = -1;
            for (size_t i = 0; i < A.size(); i++) if (A[i] == best) ci = i;
            // a blocked move the heuristic picked counts as S
            if (ci < 0) for (size_t i = 0; i < A.size(); i++) if (A[i] == make_pair(4, best.second)) ci = i;
            int cur = ci >= 0 ? val[ci] : -KMAX;
            int want = cur;
            for (int x : val) want = max(want, x);
            auto pick = [&](const vector<int>& v, int target) {
                double bs = -1e300; size_t bi = 0;
                for (size_t i = 0; i < A.size(); i++) if (v[i] == target && sc(i) > bs) { bs = sc(i); bi = i; }
                return A[bi];
            };
            if (want == full && cur < full) best = pick(val, want);         // proven win
            else if (cur == -full && want > cur) best = pick(val, want);   // heuristic choice is a proven loss
            else if (PRESS && ci >= 0 && valP[ci] > -full) {
                // Nothing decisive: press the opponents as seen by a shallow search. Deeper searches find traps a
                // strong opponent never walks into, and acting on them gave strong opponents the game.
                int curP = valP[ci], bw = pressP[ci], bi = -1; double bs = -1e300;
                for (size_t i = 0; i < A.size(); i++)
                    if (valP[i] == curP && (!PRESS_DEEPSAFE || val[i] > -full) && (pressP[i] > bw || (pressP[i] == bw && bi >= 0 && sc(i) > bs))) { bw = pressP[i]; bs = sc(i); bi = i; }
                if (bi >= 0) best = A[bi];
            }
            if (DBG) cerr << "endgame T" << T << " K " << root.K << " depth " << doneDepth << " nodes " << S.nodes << " cur " << cur << " want " << want << "\n";
        }
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
    cin >> w >> k;  // STATUS
    deathT.assign(P, -1); killC.assign(P, 0);
    for (int i = 0; i < k; i++) { int id, dt; long long kn, kd; cin >> w >> id >> dt >> kn >> kd; deathT[id] = dt; killC[id] = kd ? double(kn) / kd : 0; }
    cin >> w >> k;  // SOURCES
    sources.clear();
    for (int i = 0; i < k; i++) { int x, y, o, e; cin >> w >> x >> y >> o >> e; sources.push_back({y * N + x, o, e}); }
    cin >> w;
    return w == "END";
}

#ifdef STEPTEST
// Reads INIT, then repeated (state, "ACT d0 p0 d1 p1 ...", next state) for a 2- or 3-player game; prints mismatches.
string g2Dump(const G2& s, int tmin) {
    ostringstream o;
    o << "t=" << s.t;
    for (int i = 0; i < s.K; i++) o << " p" << i << "=" << s.pos[i] << (s.alive(i) ? "" : "+dead@" + to_string(s.death[i])) << " k" << s.kill[i];
    vector<array<int, 3>> b, f;
    for (int i = 0; i < s.nb; i++) b.push_back({s.bomb[i].a, s.bomb[i].b, s.bomb[i].c});
    for (int i = 0; i < s.nf; i++) f.push_back({s.flame[i].a, s.flame[i].b, s.flame[i].c});
    sort(b.begin(), b.end()); sort(f.begin(), f.end());
    o << " bombs";
    for (auto& x : b) o << " " << x[0] << "@" << x[1] << ":" << x[2];
    o << " flames";
    for (auto& x : f) if (x[2] >= tmin) o << " " << x[0] << "/" << x[1] << ":" << x[2];
    o << " crates";
    for (int c = 0; c < NN; c++) if (s.crate[c]) o << " " << c;
    return o.str();
}
int main() {
    string w;
    cin >> w >> N >> P >> ME >> LIMIT >> FUSE >> RANGE_ >> CAP >> FIRE >> SHRINK >> EVERY;
    NN = N * N; board.resize(N); pl.resize(P); ME = 0;
    const string DSS = "UDLRS";
    int cases = 0, bad = 0;
    vector<int> opps;
    for (int i = 1; i < P; i++) opps.push_back(i);
    while (readTurn()) {
        G2 s = g2FromInput(opps);
        int d[KMAX], p[KMAX];
        cin >> w;
        for (int i = 0; i < P; i++) { char c; cin >> c >> p[i]; d[i] = DSS.find(c); }
        g2Step(s, d, p);
        if (!readTurn()) break;
        G2 e = g2FromInput(opps);
        string a = g2Dump(s, s.t), b = g2Dump(e, s.t);
        cases++;
        if (a != b) { bad++; if (bad <= 3) cerr << "MISMATCH\n got  " << a << "\n want " << b << "\n"; }
    }
    cout << cases << " " << bad << "\n";
}
#else
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
#endif
