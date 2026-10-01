// Territory bot: safe-loop planner + guaranteed-kill hunter.
#include <bits/stdc++.h>
using namespace std;
using Clock = chrono::steady_clock;

const int DX[4] = {0, 0, -1, 1};
const int DY[4] = {-1, 1, 0, 0};
const char DC[4] = {'U', 'D', 'L', 'R'};
int dirIndex(char c) { return string("UDLR").find(c); }

struct Player { int x, y, dir, trailLen, area, deaths; };

int W, H, MAX_TURNS, N, ME, MOVE_MS, INIT_MS, NC;
vector<pair<int, int>> spawn;
int turnNo; int ENDG = 20;
vector<Player> P;
vector<int> own, trl, prot;  // owner / trail per cell (-1 none)
vector<int> lastPos, stillCnt;
bool outP[4];
Clock::time_point T0;
double elapsedMs() { return chrono::duration<double, milli>(Clock::now() - T0).count(); }

inline int cid(int x, int y) { return y * W + x; }
inline bool inb(int x, int y) { return x >= 0 && x < W && y >= 0 && y < H; }

bool readState() {
    string tok;
    if (!(cin >> tok)) return false;
    T0 = Clock::now();
    cin >> turnNo;
    P.assign(N, {});
    for (int i = 0; i < N; i++) {
        int id; char d; Player q;
        cin >> tok >> id >> q.x >> q.y >> d >> q.trailLen >> q.area >> q.deaths;
        q.dir = dirIndex(d);
        P[id] = q;
    }
    cin >> tok;
    string row;
    for (int y = 0; y < H; y++) {
        cin >> row;
        for (int x = 0; x < W; x++) own[cid(x, y)] = row[x] == '.' ? -1 : row[x] - '0';
    }
    cin >> tok;
    for (int y = 0; y < H; y++) {
        cin >> row;
        for (int x = 0; x < W; x++) trl[cid(x, y)] = row[x] == '.' ? -1 : row[x] - '0';
    }
    cin >> tok;
    return true;
}

// ---------- BFS helpers ----------
// distance from player's head, first step cannot reverse; blocked cells: player's own trail
void bfsFromHead(int p, vector<int>& dist, vector<int>* par = nullptr, bool block = true) {
    dist.assign(NC, INT_MAX);
    if (par) par->assign(NC, -1);
    int s = cid(P[p].x, P[p].y);
    dist[s] = 0;
    static vector<int> q; q.clear();
    for (int d = 0; d < 4; d++) {
        if (d == (P[p].dir ^ 1)) continue;
        int nx = P[p].x + DX[d], ny = P[p].y + DY[d];
        if (!inb(nx, ny)) continue;
        int k = cid(nx, ny);
        if (block && trl[k] == p) continue;
        dist[k] = 1; if (par) (*par)[k] = s;
        q.push_back(k);
    }
    for (size_t h = 0; h < q.size(); h++) {
        int k = q[h], x = k % W, y = k / W;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inb(nx, ny)) continue;
            int nk = cid(nx, ny);
            if (dist[nk] != INT_MAX || (block && trl[nk] == p)) continue;
            dist[nk] = dist[k] + 1; if (par) (*par)[nk] = k;
            q.push_back(nk);
        }
    }
}

vector<int> enemyDist;       // min over enemies of distance
vector<int> enemyDistBy[4], headDistBy[4], spawnDist[4];
vector<int> myDist, myPar;
double oppW = 1.0;

// ---------- candidate evaluation ----------
vector<char> threat;
vector<int> mark;  int markId = 1;   // path cell marker
vector<int> fillSeen; int fillId = 1;
vector<int> bq;

// BFS from cell s (arrived with direction ld) back to own territory avoiding my trail and marked cells.
// Appends path cells to out. Returns false if impossible.
vector<int> bpar;
bool returnHome(int s, int ld, vector<int>& out) {
    // use fillSeen as visited marker
    fillId++;
    bq.clear();
    int sx = s % W, sy = s / W;
    for (int d = 0; d < 4; d++) {
        if (d == (ld ^ 1)) continue;
        int nx = sx + DX[d], ny = sy + DY[d];
        if (!inb(nx, ny)) continue;
        int k = cid(nx, ny);
        if (trl[k] == ME || mark[k] == markId) continue;
        fillSeen[k] = fillId; bpar[k] = s; bq.push_back(k);
    }
    int found = -1;
    for (size_t h = 0; h < bq.size(); h++) {
        int k = bq[h];
        if (own[k] == ME && !threat[k]) { found = k; break; }
        int x = k % W, y = k / W;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inb(nx, ny)) continue;
            int nk = cid(nx, ny);
            if (fillSeen[nk] == fillId || trl[nk] == ME || mark[nk] == markId) continue;
            fillSeen[nk] = fillId; bpar[nk] = k; bq.push_back(nk);
        }
    }
    if (found < 0) return false;
    vector<int> rev;
    for (int c = found; c != s; c = bpar[c]) rev.push_back(c);
    for (int i = (int)rev.size() - 1; i >= 0; i--) out.push_back(rev[i]);
    return true;
}

// gain of closing with trail = existing trail + newTrail cells (marked)
vector<char> wall;
double evalGain(const vector<int>& newTrail) {
    fillId++;
    // walls: own==ME or trl==ME or marked
    bq.clear();
    auto isWall = [&](int k) { return own[k] == ME || trl[k] == ME || mark[k] == markId; };
    auto push = [&](int k) {
        if (fillSeen[k] != fillId && !isWall(k)) { fillSeen[k] = fillId; bq.push_back(k); }
    };
    for (int x = 0; x < W; x++) { push(cid(x, 0)); push(cid(x, H - 1)); }
    for (int y = 0; y < H; y++) { push(cid(0, y)); push(cid(W - 1, y)); }
    for (size_t h = 0; h < bq.size(); h++) {
        int k = bq[h], x = k % W, y = k / W;
        if (y > 0) push(k - W);
        if (y < H - 1) push(k + W);
        if (x > 0) push(k - 1);
        if (x < W - 1) push(k + 1);
    }
    double g = 0;
    for (int k = 0; k < NC; k++) {
        if (own[k] == ME) continue;
        if (fillSeen[k] == fillId) continue;
        // enclosed or trail
        if (prot[k] >= 0 && prot[k] != ME) continue;
        g += own[k] < 0 ? 1.0 : oppW;
    }
    return g;
}

struct Cand {
    vector<int> dirs;  // step directions
    double est;
};

int bestMoveDir = -1;
double bestScore = -1e18;

// Simulate path from head along given dirs; returns false if invalid.
// Fills cells, closeIdx (step index at which we close, 1-based L) ; stops at closure.
bool simulate(const vector<int>& dirs, vector<int>& cells, int& L, bool& closed, int& lastDir) {
    int x = P[ME].x, y = P[ME].y, cd = P[ME].dir;
    bool hasTrail = P[ME].trailLen > 0;
    markId++;
    cells.clear();
    closed = false;
    for (size_t i = 0; i < dirs.size(); i++) {
        int d = dirs[i];
        if (d == (cd ^ 1)) return false;
        x += DX[d]; y += DY[d];
        if (!inb(x, y)) return false;
        int k = cid(x, y);
        if (trl[k] == ME || mark[k] == markId) return false;
        cd = d;
        if (own[k] == ME) {
            if (hasTrail) { closed = true; L = i + 1; lastDir = cd; return true; }
        } else {
            hasTrail = true;
            mark[k] = markId;
            cells.push_back(k);
        }
    }
    L = dirs.size();
    lastDir = cd;
    return true;
}

// ---------- attack ----------
int attackOne(int best, int bestD, int bestT);
int tryAttack() {
    vector<tuple<int, int, int>> tg;  // (my dist, cell, enemy)
    for (int e = 0; e < N; e++) {
        if (e == ME || outP[e] || P[e].trailLen == 0) continue;
        // enemy home distance
        vector<int> de;
        bfsFromHead(e, de);
        int homeE = INT_MAX;
        for (int k = 0; k < NC; k++) if (own[k] == e && de[k] < homeE) homeE = de[k];
        vector<pair<int, int>> ls;
        for (int k = 0; k < NC; k++) {
            if (trl[k] != e) continue;
            int dm = myDist[k];
            if (dm == INT_MAX || dm > homeE) continue;
            ls.push_back({dm, k});
        }
        sort(ls.begin(), ls.end());
        for (size_t i = 0; i < ls.size() && i < 6; i++) tg.push_back({ls[i].first, ls[i].second, e});
    }
    sort(tg.begin(), tg.end());
    for (size_t i = 0; i < tg.size() && i < 12; i++) {
        auto [dm, k, e] = tg[i];
        int r = attackOne(k, dm, e);
        if (r >= 0) return r;
    }
    return -1;
}

int attackOne(int best, int bestD, int bestT) {
    vector<int> path;
    int s = cid(P[ME].x, P[ME].y);
    for (int c = best; c != s; c = myPar[c]) path.push_back(c);
    reverse(path.begin(), path.end());
    if (path.empty()) return -1;
    // safety: my trail cells (existing + new path outside my territory)
    // exposure time: bestD + distance home from target cell (approx via BFS later) -> approximate
    int homeAfter = 0;
    {
        markId++;
        for (int c : path) if (own[c] != ME) mark[c] = markId;
        vector<int> tmp;
        int ld = 0;
        if (path.size() >= 2) {
            int a = path[path.size() - 2], b = path.back();
            int dx = b % W - a % W, dy = b / W - a / W;
            for (int d = 0; d < 4; d++) if (DX[d] == dx && DY[d] == dy) ld = d;
        } else {
            int dx = best % W - P[ME].x, dy = best / W - P[ME].y;
            for (int d = 0; d < 4; d++) if (DX[d] == dx && DY[d] == dy) ld = d;
        }
        bool newTrail = P[ME].trailLen > 0;
        for (int c : path) if (own[c] != ME) newTrail = true;
        if (newTrail && own[best] != ME) {
            if (!returnHome(best, ld, tmp)) homeAfter = 60; else homeAfter = tmp.size();
            for (int c : tmp) if (own[c] != ME) path.push_back(c);  // exposure after the kill
        }
    }
    int Ltot = bestD + homeAfter;
    auto check = [&](int k) {
        for (int e = 0; e < N; e++) {
            if (e == ME || outP[e]) continue;
            int de = enemyDistBy[e][k];
            if (e == bestT) { if (headDistBy[e][k] <= bestD || spawnDist[e][k] + bestD <= Ltot) return false; }
            else if (de <= Ltot) return false;
        }
        return true;
    };
    for (int k = 0; k < NC; k++) if (trl[k] == ME && !check(k)) return -1;
    for (int c : path) if (own[c] != ME && !check(c)) return -1;
    // head-on check first step
    int f = path[0];
    for (int e = 0; e < N; e++) {
        if (e == ME || outP[e] || e == bestT) continue;
        if (headDistBy[e][f] <= 1) return -1;
    }
    int dx = f % W - P[ME].x, dy = f / W - P[ME].y;
    for (int d = 0; d < 4; d++) if (DX[d] == dx && DY[d] == dy) return d;
    return -1;
}

// cells of mine an enemy would capture by closing its loop via the shortest way home
void computeThreat() {
    threat.assign(NC, 0);
    for (int e = 0; e < N; e++) {
        if (e == ME || outP[e] || P[e].trailLen == 0) continue;
        vector<int> de, par;
        bfsFromHead(e, de, &par);
        int best = -1;
        for (int k = 0; k < NC; k++) if (own[k] == e && (best < 0 || de[k] < de[best])) best = k;
        if (best < 0 || de[best] == INT_MAX) continue;
        vector<char> wl(NC, 0);
        for (int k = 0; k < NC; k++) if (own[k] == e || trl[k] == e) wl[k] = 1;
        int s = cid(P[e].x, P[e].y);
        for (int c = best; c != s && c >= 0; c = par[c]) wl[c] = 1;
        vector<char> seen(NC, 0);
        vector<int> q;
        auto push = [&](int k) { if (!wl[k] && !seen[k]) { seen[k] = 1; q.push_back(k); } };
        for (int x = 0; x < W; x++) { push(cid(x, 0)); push(cid(x, H - 1)); }
        for (int y = 0; y < H; y++) { push(cid(0, y)); push(cid(W - 1, y)); }
        for (size_t h = 0; h < q.size(); h++) {
            int k = q[h], x = k % W, y = k / W;
            if (y > 0) push(k - W);
            if (y < H - 1) push(k + W);
            if (x > 0) push(k - 1);
            if (x < W - 1) push(k + 1);
        }
        for (int k = 0; k < NC; k++)
            if (own[k] == ME && !seen[k] && prot[k] != ME) threat[k] = 1;
    }
}

// ---------- main decision ----------
mt19937 rng(12345);

int decide() {
    NC = W * H;
    // detect eliminated players (don't move)
    for (int i = 0; i < N; i++) {
        int k = cid(P[i].x, P[i].y);
        if (turnNo > 0 && k == lastPos[i] && k == cid(spawn[i].first, spawn[i].second)) stillCnt[i]++;
        else stillCnt[i] = 0;
        lastPos[i] = k;
        outP[i] = (i != ME && stillCnt[i] >= 2);
    }
    enemyDist.assign(NC, INT_MAX);
    for (int e = 0; e < N; e++) {
        if (e == ME) continue;
        if (outP[e]) { enemyDistBy[e].assign(NC, INT_MAX); headDistBy[e] = enemyDistBy[e]; continue; }
        bfsFromHead(e, enemyDistBy[e], nullptr, false);
        headDistBy[e] = enemyDistBy[e];
        if (P[e].trailLen > 0)
            for (int k = 0; k < NC; k++) enemyDistBy[e][k] = min(enemyDistBy[e][k], spawnDist[e][k] + 1);
        for (int k = 0; k < NC; k++) enemyDist[k] = min(enemyDist[k], enemyDistBy[e][k]);
    }
    bfsFromHead(ME, myDist, &myPar);
    oppW = (N == 2) ? 2.0 : 1.4;

    computeThreat();
    int atk = tryAttack();
    if (atk >= 0) return atk;

    const Player& me = P[ME];
    int remain = MAX_TURNS - turnNo;
    int head = cid(me.x, me.y);
    bool inside = own[head] == ME && me.trailLen == 0;

    // current min enemy distance to my existing trail
    int trailMinE = INT_MAX;
    for (int k = 0; k < NC; k++) if (trl[k] == ME) trailMinE = min(trailMinE, enemyDist[k]);

    // prefixes
    struct Pre { vector<int> dirs; int ex, ey, dir; };
    vector<Pre> pres;
    if (inside) {
        // BFS inside territory from head
        vector<int> dist(NC, INT_MAX), par(NC, -1), pd(NC, -1);
        vector<int> q;
        dist[head] = 0; q.push_back(head);
        for (size_t h = 0; h < q.size(); h++) {
            int k = q[h], x = k % W, y = k / W;
            for (int d = 0; d < 4; d++) {
                if (k == head && d == (me.dir ^ 1)) continue;
                int nx = x + DX[d], ny = y + DY[d];
                if (!inb(nx, ny)) continue;
                int nk = cid(nx, ny);
                if (own[nk] != ME || dist[nk] != INT_MAX) continue;
                dist[nk] = dist[k] + 1; par[nk] = k; pd[nk] = d;
                q.push_back(nk);
            }
        }
        vector<pair<int, int>> exits;  // (dist, cell*4+dir)
        for (int k : q) {
            int x = k % W, y = k / W;
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d], ny = y + DY[d];
                if (!inb(nx, ny)) continue;
                if (own[cid(nx, ny)] == ME) continue;
                if (k == head && d == (me.dir ^ 1)) continue;
                if (k != head && pd[k] == (d ^ 1)) continue;
                exits.push_back({dist[k], k * 4 + d});
            }
        }
        sort(exits.begin(), exits.end());
        int mind = exits.empty() ? 0 : exits[0].first;
        vector<pair<int, int>> pick, rest;
        for (auto& e : exits) (e.first <= mind + 4 ? pick : rest).push_back(e);
        shuffle(pick.begin(), pick.end(), rng);
        if (pick.size() > 30) pick.resize(30);
        shuffle(rest.begin(), rest.end(), rng);
        for (size_t i = 0; i < rest.size() && i < 30; i++) pick.push_back(rest[i]);
        for (auto& e : pick) {
            int k = e.second / 4, d = e.second % 4;
            Pre p;
            for (int c = k; c != head; c = par[c]) p.dirs.push_back(pd[c]);
            reverse(p.dirs.begin(), p.dirs.end());
            p.ex = k % W; p.ey = k / W;
            p.dir = d;  // the next direction to take (outward)
            pres.push_back(p);
        }
    } else {
        Pre p; p.ex = me.x; p.ey = me.y; p.dir = -1;
        pres.push_back(p);
    }

    // prefix sums of value for estimates
    vector<double> ps((W + 1) * (H + 1), 0);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int k = cid(x, y);
            double v = own[k] == ME ? 0 : (own[k] < 0 ? 1 : ((prot[k] >= 0) ? 0 : oppW));
            ps[(y + 1) * (W + 1) + x + 1] = v + ps[y * (W + 1) + x + 1] + ps[(y + 1) * (W + 1) + x] - ps[y * (W + 1) + x];
        }
    auto boxVal = [&](int x0, int y0, int x1, int y1) {
        if (x0 > x1) swap(x0, x1);
        if (y0 > y1) swap(y0, y1);
        x0 = max(x0, 0); y0 = max(y0, 0); x1 = min(x1, W - 1); y1 = min(y1, H - 1);
        return ps[(y1 + 1) * (W + 1) + x1 + 1] - ps[y0 * (W + 1) + x1 + 1] - ps[(y1 + 1) * (W + 1) + x0] + ps[y0 * (W + 1) + x0];
    };

    // home distance map (ignoring new path) for estimates
    vector<int> homeD(NC, INT_MAX);
    {
        vector<int> q;
        for (int k = 0; k < NC; k++) if (own[k] == ME && !threat[k]) { homeD[k] = 0; q.push_back(k); }
        for (size_t h = 0; h < q.size(); h++) {
            int k = q[h], x = k % W, y = k / W;
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d], ny = y + DY[d];
                if (!inb(nx, ny)) continue;
                int nk = cid(nx, ny);
                if (homeD[nk] != INT_MAX || trl[nk] == ME) continue;
                homeD[nk] = homeD[k] + 1; q.push_back(nk);
            }
        }
    }

    auto segMin = [&](int x, int y, int d, int len) {
        int m = INT_MAX;
        for (int i = 0; i < len; i++) {
            x += DX[d]; y += DY[d];
            int k = cid(x, y);
            if (own[k] != ME) m = min(m, enemyDist[k]);
        }
        return m;
    };
    int maxTrip = (N == 2) ? 90 : 80;
    static const int steps[] = {1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16, 19, 22, 26, 30};
    vector<Cand> cands;
    int elapsed = me.trailLen;
    for (auto& pr : pres) {
        vector<int> d1s;
        if (pr.dir >= 0) d1s.push_back(pr.dir);
        else for (int d = 0; d < 4; d++) if (d != (me.dir ^ 1)) d1s.push_back(d);
        for (int d1 : d1s) {
            for (int a : steps) {
                int ax = pr.ex + DX[d1] * a, ay = pr.ey + DY[d1] * a;
                if (!inb(ax, ay)) break;
                int pl = pr.dirs.size();
                // pure segment then return
                {
                    int Lest = pl + a + (homeD[cid(ax, ay)] == INT_MAX ? 99 : homeD[cid(ax, ay)]);
                    double est = boxVal(pr.ex, pr.ey, ax, ay) * 0.3;
                    Cand c; c.dirs = pr.dirs;
                    for (int i = 0; i < a; i++) c.dirs.push_back(d1);
                    c.est = est / (Lest + elapsed + 2);
                    cands.push_back(c);
                }
                for (int d2 = 0; d2 < 4; d2++) {
                    if (d2 == d1 || d2 == (d1 ^ 1)) continue;
                    for (int b : steps) {
                        int bx = ax + DX[d2] * b, by = ay + DY[d2] * b;
                        if (!inb(bx, by)) break;
                        // variants: return directly, or go back along d1^1 for c steps then return
                        for (int cv = 0; cv < 4; cv++) {
                            int cs = 0;
                            if (cv == 1) cs = a;
                            else if (cv == 2) cs = max(1, a / 2);
                            else if (cv == 3) cs = a + 2;
                            if (cv > 0 && cs == 0) continue;
                            int cx = bx + DX[d1 ^ 1] * cs, cy = by + DY[d1 ^ 1] * cs;
                            if (!inb(cx, cy)) continue;
                            int hd = homeD[cid(cx, cy)];
                            if (hd == INT_MAX) continue;
                            int Lest = pl + a + b + cs + hd;
                            if (Lest > remain) continue;
                            if (Lest + elapsed > maxTrip) continue;
                            {
                                int m = min(trailMinE, min(segMin(pr.ex, pr.ey, d1, a), segMin(ax, ay, d2, b)));
                                if (cs) m = min(m, segMin(bx, by, d1 ^ 1, cs));
                                if (m <= Lest) continue;
                            }
                            double est = boxVal(pr.ex, pr.ey, bx, by);
                            if (cv == 3) est = boxVal(pr.ex - DX[d1] * 2, pr.ey - DY[d1] * 2, bx, by);
                            est = est / (Lest + elapsed + 2);
                            Cand c; c.dirs = pr.dirs;
                            c.dirs.reserve(pl + a + b + cs);
                            for (int i = 0; i < a; i++) c.dirs.push_back(d1);
                            for (int i = 0; i < b; i++) c.dirs.push_back(d2);
                            for (int i = 0; i < cs; i++) c.dirs.push_back(d1 ^ 1);
                            c.est = est;
                            cands.push_back(c);
                        }
                    }
                }
            }
        }
    }
    // always include pure return home
    if (!inside) { Cand c; c.est = 1e9; cands.push_back(c); }
    sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.est > b.est; });
    double genMs = elapsedMs();

    double budget = (turnNo == 0) ? 300.0 : (N == 2 ? 14.0 : 12.0);
    int bestFirst = -1;
    double best = -1e18;
    int fbFirst = -1; double fbScore = -1e18;  // least-unsafe fallback
    vector<int> cells, full;
    int evals = 0;
    for (auto& c : cands) {
        if (evals > 0 && elapsedMs() > budget) break;
        int L; bool closed; int ld;
        if (!simulate(c.dirs, cells, L, closed, ld)) continue;
        full = c.dirs;
        if (!closed) {
            int endc;
            if (c.dirs.empty()) endc = head, ld = me.dir;
            else {
                int x = me.x, y = me.y;
                for (int d : c.dirs) x += DX[d], y += DY[d];
                endc = cid(x, y);
            }
            if (own[endc] == ME && me.trailLen == 0 && cells.empty()) continue;  // never left
            vector<int> ret;
            if (!returnHome(endc, ld, ret)) continue;
            // convert ret cells to dirs
            int px = endc % W, py = endc / W;
            for (int rc : ret) {
                int dx = rc % W - px, dy = rc / W - py;
                for (int d = 0; d < 4; d++) if (DX[d] == dx && DY[d] == dy) full.push_back(d);
                if (own[rc] != ME) { mark[rc] = markId; cells.push_back(rc); }
                px = rc % W; py = rc / W;
            }
            L = full.size();
        }
        if (full.empty()) continue;
        evals++;
        // safety
        int minE = trailMinE;
        for (int k : cells) minE = min(minE, enemyDist[k]);
        int firstCell = cid(me.x + DX[full[0]], me.y + DY[full[0]]);
        bool headRisk = enemyDist[firstCell] <= 1;
        double slack = (double)minE - L;
        if (L > remain) slack = min(slack, (double)(remain - L));
        if (slack < 1 || headRisk) {
            double fs = slack - (headRisk ? 5 : 0) - L * 0.01;
            if (fs > fbScore) { fbScore = fs; fbFirst = full[0]; }
            continue;
        }
        double g = evalGain(cells);
        double sc = g / (L + elapsed + 2.0);
        if (remain <= ENDG) sc = g - 0.01 * L;
        // prefer a bit of slack
        
        if (sc > best) { best = sc; bestFirst = full[0]; }
    }
    if (getenv("BOTDBG")) fprintf(stderr, "t%d in=%d tl=%d cands=%zu evals=%d best=%.3f fb=%.1f bf=%d gen=%.2f\n", turnNo, inside, me.trailLen, cands.size(), evals, best, fbScore, bestFirst, genMs);
    if (bestFirst >= 0) return bestFirst;
    if (inside) {
        // stay inside, move to cell maximizing enemy distance
        int bd = -1, bv = INT_MIN;
        for (int d = 0; d < 4; d++) {
            if (d == (me.dir ^ 1)) continue;
            int nx = me.x + DX[d], ny = me.y + DY[d];
            if (!inb(nx, ny)) continue;
            int k = cid(nx, ny);
            int v = (own[k] == ME ? 1000 : 0) + min(enemyDist[k], 50) * 2 - (enemyDist[k] <= 1 ? 500 : 0);
            // prefer cells with many own neighbors (keep maneuvering room)
            if (v > bv) { bv = v; bd = d; }
        }
        if (bd >= 0) return bd;
    }
    if (fbFirst >= 0) return fbFirst;
    for (int d : {me.dir, 0, 1, 2, 3}) {
        if (d == (me.dir ^ 1)) continue;
        int nx = me.x + DX[d], ny = me.y + DY[d];
        if (inb(nx, ny) && trl[cid(nx, ny)] != ME) return d;
    }
    return me.dir;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string tok;
    cin >> tok;
    cin >> W >> H >> MAX_TURNS >> N >> ME >> MOVE_MS >> INIT_MS;
    NC = W * H;
    spawn.resize(N);
    for (int i = 0; i < N; i++) {
        int id, x, y;
        cin >> tok >> id >> x >> y;
        spawn[id] = {x, y};
    }
    own.assign(NC, -1); trl.assign(NC, -1); prot.assign(NC, -1);
    for (int i = 0; i < N; i++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int x = spawn[i].first + dx, y = spawn[i].second + dy;
                if (inb(x, y)) prot[cid(x, y)] = i;
            }
    for (int i = 0; i < N; i++) {
        spawnDist[i].assign(NC, INT_MAX);
        vector<int> q{cid(spawn[i].first, spawn[i].second)};
        spawnDist[i][q[0]] = 0;
        for (size_t h = 0; h < q.size(); h++) {
            int k = q[h], x = k % W, y = k / W;
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d], ny = y + DY[d];
                if (!inb(nx, ny)) continue;
                int nk = cid(nx, ny);
                if (spawnDist[i][nk] != INT_MAX) continue;
                spawnDist[i][nk] = spawnDist[i][k] + 1; q.push_back(nk);
            }
        }
    }
    mark.assign(NC, 0); fillSeen.assign(NC, 0); bpar.assign(NC, -1);
    lastPos.assign(N, -1); stillCnt.assign(N, 0);
    while (readState()) {
        int d = decide();
        cout << turnNo << ' ' << DC[d] << endl;
    }
}
