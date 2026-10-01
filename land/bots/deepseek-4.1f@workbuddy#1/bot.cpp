// Territory game bot.
//
// Core idea
// ---------
// `Game._enclosed(i)` floods inward from every *border* cell that is not a wall,
// where a wall is any cell owned by i or carrying i's trail.  If EVERY border
// cell is a wall, the flood has no seed at all, so `enclosed` degenerates to
// "every non-wall cell on the board".  Capturing then hands over the whole map
// (minus opponents' protected squares).
//
// So every strong plan is: walk out, lay a wall along a stretch of the border,
// come back home.  The longer the wall the more we take - but the longer our
// trail lives, and ANY opponent head stepping on it kills us.  So the bot
// enumerates a handful of candidate loops (a full lap, four half-plane
// sweeps, and a generic "close the remaining gap in the ring" route),
// simulates the exact capture for each, and picks the best
// gain / (length x length-penalty).
#include <bits/stdc++.h>
using namespace std;

static const int DX[4] = {0, 0, -1, 1};   // U D L R
static const int DY[4] = {-1, 1, 0, 0};
static const char DC[4] = {'U', 'D', 'L', 'R'};
static inline int oppd(int d) { return d ^ 1; }

int W, H, MAXT, N, ME, MOVE_MS, INIT_MS;
int CX, CY;
int PX[8], PY[8], PD[8], TL[8], AR[8], DT[8];
vector<string> OWN, TR;
int TURN = 0;
int tx0, tx1, ty0, ty1;                   // bounding box of my territory

vector<pair<int, int> > ring;
vector<vector<int> > ridx;

static inline bool inB(int x, int y) { return x >= 0 && x < W && y >= 0 && y < H; }
static inline bool isMine(int x, int y) { return OWN[y][x] == (char)('0' + ME); }

void buildRing() {
    ring.clear();
    for (int x = 0; x < W; x++) ring.push_back(make_pair(x, 0));
    for (int y = 1; y < H; y++) ring.push_back(make_pair(W - 1, y));
    for (int x = W - 2; x >= 0; x--) ring.push_back(make_pair(x, H - 1));
    for (int y = H - 2; y >= 1; y--) ring.push_back(make_pair(0, y));
    ridx.assign(W, vector<int>(H, -1));
    for (int i = 0; i < (int)ring.size(); i++) ridx[ring[i].first][ring[i].second] = i;
}

void computeBBox() {
    tx0 = W; tx1 = -1; ty0 = H; ty1 = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (isMine(x, y)) {
                tx0 = min(tx0, x); tx1 = max(tx1, x);
                ty0 = min(ty0, y); ty1 = max(ty1, y);
            }
    if (tx1 < 0) { tx0 = tx1 = CX; ty0 = ty1 = CY; }
}

int dirOf(int ax, int ay, int bx, int by) {
    if (bx > ax) return 3;
    if (bx < ax) return 2;
    if (by > ay) return 1;
    return 0;
}

bool readState() {
    string tok;
    if (!(cin >> tok)) return false;
    if (tok != "TURN") return false;
    cin >> TURN;
    for (int i = 0; i < N; i++) {
        int id, x, y, tl, ar, dt;
        char d;
        cin >> tok >> id >> x >> y >> d >> tl >> ar >> dt;
        int di = (d == 'U') ? 0 : (d == 'D') ? 1 : (d == 'L') ? 2 : 3;
        if (id < 0 || id >= N) return false;
        PX[id] = x; PY[id] = y; PD[id] = di; TL[id] = tl; AR[id] = ar; DT[id] = dt;
    }
    cin >> tok;
    OWN.assign(H, "");
    for (int y = 0; y < H; y++) cin >> OWN[y];
    cin >> tok;
    TR.assign(H, "");
    for (int y = 0; y < H; y++) cin >> TR[y];
    cin >> tok;
    return true;
}

// ---------------------------------------------------------------------------
// Candidate route construction
// ---------------------------------------------------------------------------
void addLine(vector<pair<int, int> >& r, int& x, int& y, int dir, int steps) {
    for (int i = 0; i < steps; i++) {
        x += DX[dir]; y += DY[dir];
        r.push_back(make_pair(x, y));
    }
}

// Walk from (sx,sy) inside my territory to (gx,gy), staying on my cells.
// Returns the cell sequence (including start) or empty.
vector<pair<int, int> > walkInside(int sx, int sy, int gx, int gy, int sd, int forbidLast) {
    vector<int> from(W * H, -1), fdir(W * H, -1);
    deque<int> q;
    int st = sy * W + sx;
    from[st] = st;
    fdir[st] = -2;
    q.push_back(st);
    int goal = -1;
    while (!q.empty()) {
        int k = q.front(); q.pop_front();
        int x = k % W, y = k / W;
        if (x == gx && y == gy) { goal = k; break; }
        for (int d = 0; d < 4; d++) {
            if (k == st && d == oppd(sd)) continue;         // cannot reverse on the first step
            int nx = x + DX[d], ny = y + DY[d];
            if (!inB(nx, ny) || !isMine(nx, ny)) continue;
            int nk = ny * W + nx;
            if (from[nk] != -1) continue;
            from[nk] = k; fdir[nk] = d;
            q.push_back(nk);
        }
    }
    if (goal < 0) return vector<pair<int, int> >();
    vector<int> path;
    int c = goal;
    while (c != st) { path.push_back(fdir[c]); c = from[c]; }
    reverse(path.begin(), path.end());
    if (!path.empty() && path.back() == oppd(forbidLast)) return vector<pair<int, int> >();
    vector<pair<int, int> > r;
    r.push_back(make_pair(sx, sy));
    int x = sx, y = sy;
    for (size_t i = 0; i < path.size(); i++) {
        x += DX[path[i]]; y += DY[path[i]];
        r.push_back(make_pair(x, y));
    }
    return r;
}

// Full lap: out to the border, all the way round the ring, back home.
vector<pair<int, int> > buildLap(int sx, int sy, int D, int P) {
    vector<pair<int, int> > r;
    r.push_back(make_pair(sx, sy));
    int x = sx, y = sy;
    while (true) {
        int nx = x + DX[D], ny = y + DY[D];
        if (!inB(nx, ny)) break;
        x = nx; y = ny;
        r.push_back(make_pair(x, y));
    }
    int B = ridx[x][y];
    if (B < 0) return vector<pair<int, int> >();
    int pd;
    if (D == 0 || D == 1) pd = (P > 0) ? 3 : 2;
    else                  pd = (P > 0) ? 1 : 0;
    int bx = x + DX[pd], by = y + DY[pd];
    if (!inB(bx, by)) return vector<pair<int, int> >();
    int B2 = ridx[bx][by];
    if (B2 < 0) return vector<pair<int, int> >();
    int Sz = (int)ring.size();
    vector<int> order;
    if ((B + 1) % Sz == B2) {
        int i = B;
        do { i = (i - 1 + Sz) % Sz; order.push_back(i); } while (i != B2);
    } else if ((B - 1 + Sz) % Sz == B2) {
        int i = B;
        do { i = (i + 1) % Sz; order.push_back(i); } while (i != B2);
    } else return vector<pair<int, int> >();
    for (size_t k = 0; k < order.size(); k++) {
        int c = order[k];
        int cx = ring[c].first, cy = ring[c].second;
        if (cx == x && cy == y) continue;
        if (abs(cx - x) + abs(cy - y) != 1) return vector<pair<int, int> >();
        x = cx; y = cy;
        r.push_back(make_pair(x, y));
    }
    int RD = oppd(D), guard = 0;
    while (true) {
        int nx = x + DX[RD], ny = y + DY[RD];
        if (!inB(nx, ny)) return vector<pair<int, int> >();
        x = nx; y = ny;
        r.push_back(make_pair(x, y));
        if (isMine(x, y)) break;
        if (++guard > W + H + 4) return vector<pair<int, int> >();
    }
    return r;
}

// Half-plane sweep: cut along one edge of my bounding box, wall off the whole
// border on that side, and walk back home.
vector<pair<int, int> > buildAnnex(int D) {
    int X0, Y0, FD;
    if (D == 0)      { X0 = tx0; Y0 = ty0; FD = 2; }   // north
    else if (D == 1) { X0 = tx0; Y0 = ty1; FD = 2; }   // south
    else if (D == 2) { X0 = tx0; Y0 = ty0; FD = 0; }   // west
    else             { X0 = tx1; Y0 = ty0; FD = 0; }   // east
    if (!isMine(X0, Y0)) return vector<pair<int, int> >();

    for (int ord = 0; ord < 2; ord++) {
        int sx = PX[ME], sy = PY[ME];
        int dxs = (X0 > sx) ? 3 : (X0 < sx ? 2 : -1);
        int dys = (Y0 > sy) ? 1 : (Y0 < sy ? 0 : -1);
        vector<pair<int, int> > pre;
        bool ok = true;
        int x = sx, y = sy;
        int seq[2][2] = {{dxs, dys}, {dys, dxs}};
        pre.push_back(make_pair(x, y));
        for (int s = 0; s < 2 && ok; s++) {
            int d = seq[ord][s];
            if (d < 0) continue;
            int steps = (d == 2 || d == 3) ? abs(X0 - x) : abs(Y0 - y);
            for (int i = 0; i < steps; i++) {
                x += DX[d]; y += DY[d];
                if (!isMine(x, y)) { ok = false; break; }
                pre.push_back(make_pair(x, y));
            }
        }
        if (!ok || x != X0 || y != Y0) continue;
        int lastD = -1;
        if (pre.size() >= 2)
            lastD = dirOf(pre[pre.size() - 2].first, pre[pre.size() - 2].second,
                          pre[pre.size() - 1].first, pre[pre.size() - 1].second);
        if (lastD == oppd(FD)) continue;

        vector<pair<int, int> > r = pre;
        int cx = X0, cy = Y0;
        if (D == 0) {
            addLine(r, cx, cy, 2, X0);
            addLine(r, cx, cy, 0, cy);
            addLine(r, cx, cy, 3, W - 1 - cx);
            addLine(r, cx, cy, 1, ty0);
        } else if (D == 1) {
            addLine(r, cx, cy, 2, X0);
            addLine(r, cx, cy, 1, H - 1 - cy);
            addLine(r, cx, cy, 3, W - 1 - cx);
            addLine(r, cx, cy, 0, cy - ty1);
        } else if (D == 2) {
            addLine(r, cx, cy, 0, cy);
            addLine(r, cx, cy, 2, cx);
            addLine(r, cx, cy, 1, H - 1 - cy);
            addLine(r, cx, cy, 3, X0);
        } else {
            addLine(r, cx, cy, 0, cy);
            addLine(r, cx, cy, 3, W - 1 - cx);
            addLine(r, cx, cy, 1, H - 1 - cy);
            addLine(r, cx, cy, 2, W - 1 - X0);
        }
        // final segment: straight back into my territory
        int RD = (D == 0 || D == 1) ? 2 : 0;
        int guard = 0;
        while (true) {
            int nx = cx + DX[RD], ny = cy + DY[RD];
            if (!inB(nx, ny)) { ok = false; break; }
            cx = nx; cy = ny;
            r.push_back(make_pair(cx, cy));
            if (isMine(cx, cy)) break;
            if (++guard > W + H + 4) { ok = false; break; }
        }
        if (ok) return r;
    }
    return vector<pair<int, int> >();
}

// Interior sweep: enclose a band next to my bounding box.  The cut side is my
// own territory, so the loop never has to reach the border at all - short trail,
// little exposure.
vector<pair<int, int> > buildSweep(int D) {
    int X0, Y0, FD;
    if (D == 0)      { X0 = tx0; Y0 = ty0; FD = 0; }
    else if (D == 1) { X0 = tx0; Y0 = ty1; FD = 1; }
    else if (D == 2) { X0 = tx0; Y0 = ty0; FD = 2; }
    else             { X0 = tx1; Y0 = ty0; FD = 3; }
    if (!isMine(X0, Y0)) return vector<pair<int, int> >();
    vector<pair<int, int> > pre = walkInside(PX[ME], PY[ME], X0, Y0, PD[ME], oppd(FD));
    if (pre.empty()) return vector<pair<int, int> >();
    vector<pair<int, int> > r = pre;
    int cx = X0, cy = Y0;
    if (D == 0) {
        addLine(r, cx, cy, 0, cy);
        addLine(r, cx, cy, 3, tx1 - cx);
        addLine(r, cx, cy, 1, ty0);
    } else if (D == 1) {
        addLine(r, cx, cy, 1, H - 1 - cy);
        addLine(r, cx, cy, 3, tx1 - cx);
        addLine(r, cx, cy, 0, H - 1 - ty1);
    } else if (D == 2) {
        addLine(r, cx, cy, 2, cx);
        addLine(r, cx, cy, 1, ty1 - cy);
        addLine(r, cx, cy, 3, tx0);
    } else {
        addLine(r, cx, cy, 3, W - 1 - cx);
        addLine(r, cx, cy, 1, ty1 - cy);
        addLine(r, cx, cy, 2, W - 1 - tx1);
    }
    return r;
}

vector<pair<int, int> > buildSweepD(int D, int d) {
    int X0, Y0, FD;
    if (D == 0)      { X0 = tx0; Y0 = ty0; FD = 0; }
    else if (D == 1) { X0 = tx0; Y0 = ty1; FD = 1; }
    else if (D == 2) { X0 = tx0; Y0 = ty0; FD = 2; }
    else             { X0 = tx1; Y0 = ty0; FD = 3; }
    if (!isMine(X0, Y0)) return vector<pair<int, int> >();
    if (d <= 0) return vector<pair<int, int> >();
    int w = (D < 2) ? (tx1 - tx0) : (ty1 - ty0);          // perpendicular span
    int maxd;
    if (D == 0) maxd = Y0;
    else if (D == 1) maxd = H - 1 - Y0;
    else if (D == 2) maxd = X0;
    else maxd = W - 1 - X0;
    if (d > maxd) d = maxd;
    if (d <= 0 || w <= 0) return vector<pair<int, int> >();
    vector<pair<int, int> > pre = walkInside(PX[ME], PY[ME], X0, Y0, PD[ME], oppd(FD));
    if (pre.empty()) return vector<pair<int, int> >();
    vector<pair<int, int> > r = pre;
    int cx = X0, cy = Y0;
    if (D == 0) {
        addLine(r, cx, cy, 0, d);
        addLine(r, cx, cy, 3, w);
        addLine(r, cx, cy, 1, d);
    } else if (D == 1) {
        addLine(r, cx, cy, 1, d);
        addLine(r, cx, cy, 3, w);
        addLine(r, cx, cy, 0, d);
    } else if (D == 2) {
        addLine(r, cx, cy, 2, d);
        addLine(r, cx, cy, 1, ty1 - ty0);
        addLine(r, cx, cy, 3, d);
    } else {
        addLine(r, cx, cy, 3, d);
        addLine(r, cx, cy, 1, ty1 - ty0);
        addLine(r, cx, cy, 2, d);
    }
    return r;
}

// Generic: close whatever stretch of the ring is still not mine.
vector<pair<int, int> > buildCover() {
    int Sz = (int)ring.size();
    int i1 = -1;
    for (int i = 0; i < Sz; i++)
        if (!isMine(ring[i].first, ring[i].second) &&
            isMine(ring[(i - 1 + Sz) % Sz].first, ring[(i - 1 + Sz) % Sz].second)) { i1 = i; break; }
    if (i1 < 0) return vector<pair<int, int> >();
    int i2 = i1;
    while (!isMine(ring[(i2 + 1) % Sz].first, ring[(i2 + 1) % Sz].second)) i2 = (i2 + 1) % Sz;
    int a = (i1 - 1 + Sz) % Sz;
    int ax = ring[a].first, ay = ring[a].second;
    vector<pair<int, int> > pre = walkInside(PX[ME], PY[ME], ax, ay, PD[ME],
                                             dirOf(ring[a].first, ring[a].second,
                                                   ring[i1].first, ring[i1].second));
    if (pre.empty()) return vector<pair<int, int> >();
    vector<pair<int, int> > r = pre;
    int x = ax, y = ay;
    int i = a;
    while (true) {
        i = (i + 1) % Sz;
        int nx = ring[i].first, ny = ring[i].second;
        if (abs(nx - x) + abs(ny - y) != 1) return vector<pair<int, int> >();
        x = nx; y = ny;
        r.push_back(make_pair(x, y));
        if (isMine(x, y)) break;
    }
    return r;
}

// ---------------------------------------------------------------------------
bool routeValid(const vector<pair<int, int> >& r) {
    int n = (int)r.size();
    if (n < 4) return false;
    if (r[0].first != PX[ME] || r[0].second != PY[ME]) return false;
    for (int i = 0; i < n; i++) {
        if (!inB(r[i].first, r[i].second)) return false;
        if (i) {
            int dx = abs(r[i].first - r[i - 1].first), dy = abs(r[i].second - r[i - 1].second);
            if (dx + dy != 1) return false;
        }
    }
    if (dirOf(r[0].first, r[0].second, r[1].first, r[1].second) == oppd(PD[ME])) return false;
    for (int i = 1; i + 1 < n; i++) {
        int a = dirOf(r[i - 1].first, r[i - 1].second, r[i].first, r[i].second);
        int b = dirOf(r[i].first, r[i].second, r[i + 1].first, r[i + 1].second);
        if (b == oppd(a)) return false;
    }
    int k = -1;
    for (int i = 1; i < n; i++) if (!isMine(r[i].first, r[i].second)) { k = i; break; }
    if (k < 0) return false;
    for (int i = k; i < n - 1; i++) if (isMine(r[i].first, r[i].second)) return false;
    if (!isMine(r[n - 1].first, r[n - 1].second)) return false;
    return true;
}

// Exact capture simulation: walls = my land + the trail this route would lay.
long long simulateGain(const vector<pair<int, int> >& r) {
    vector<char> wall(W * H, 0);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (isMine(x, y)) wall[y * W + x] = 1;
    long long trailCells = 0;
    for (size_t i = 1; i < r.size(); i++) {
        int k = r[i].second * W + r[i].first;
        if (!wall[k]) { wall[k] = 1; trailCells++; }
    }
    vector<char> seen(W * H, 0);
    deque<int> q;
    for (int x = 0; x < W; x++) {
        int a = x, b = (H - 1) * W + x;
        if (!wall[a] && !seen[a]) { seen[a] = 1; q.push_back(a); }
        if (!wall[b] && !seen[b]) { seen[b] = 1; q.push_back(b); }
    }
    for (int y = 0; y < H; y++) {
        int a = y * W, b = y * W + W - 1;
        if (!wall[a] && !seen[a]) { seen[a] = 1; q.push_back(a); }
        if (!wall[b] && !seen[b]) { seen[b] = 1; q.push_back(b); }
    }
    while (!q.empty()) {
        int k = q.front(); q.pop_front();
        int x = k % W;
        int nb[4] = {k - W, k + W, k - 1, k + 1};
        bool okb[4] = {k >= W, k < W * (H - 1), x > 0, x < W - 1};
        for (int t = 0; t < 4; t++)
            if (okb[t] && !wall[nb[t]] && !seen[nb[t]]) { seen[nb[t]] = 1; q.push_back(nb[t]); }
    }
    long long enclosed = 0;
    for (int k = 0; k < W * H; k++) if (!wall[k] && !seen[k]) enclosed++;
    return trailCells + enclosed;
}

// ---------------------------------------------------------------------------
vector<pair<int, int> > route;
int rstep = 0;
int wanderPhase = 0;

double scoreRoute(long long gain, int len) {
    if (len <= 0) return -1;
    double penalty = 1.0 + max(0, len - 60) / 25.0;
    return (double)gain / (len * penalty);
}

void replan() {
    route.clear();
    rstep = 0;
    if (!isMine(PX[ME], PY[ME]) || TL[ME] != 0) return;
    computeBBox();

    vector<pair<int, int> > best;
    double bestScore = -1;
    int md = PD[ME];

    vector<vector<pair<int, int> > > cands;
    for (int D = 0; D < 4; D++) {
        if (D == oppd(md)) continue;
        for (int P = -1; P <= 1; P += 2) {
            vector<pair<int, int> > r = buildLap(PX[ME], PY[ME], D, P);
            if (routeValid(r)) cands.push_back(r);
        }
    }
    for (int D = 0; D < 4; D++) {
        vector<pair<int, int> > r = buildAnnex(D);
        if (routeValid(r)) cands.push_back(r);
    }
    for (int D = 0; D < 4; D++) {
        vector<pair<int, int> > r = buildSweep(D);
        if (routeValid(r)) cands.push_back(r);
        static const int depths[12] = {1, 2, 3, 4, 6, 8, 11, 15, 20, 26, 34, 44};
        for (int t = 0; t < 12; t++) {
            vector<pair<int, int> > q = buildSweepD(D, depths[t]);
            if (routeValid(q)) cands.push_back(q);
        }
    }
    {
        vector<pair<int, int> > r = buildCover();
        if (routeValid(r)) cands.push_back(r);
    }

    int cap = getenv("TCAP") ? atoi(getenv("TCAP")) : (N >= 4 ? 150 : 400);
    double L0 = getenv("TL0") ? atof(getenv("TL0")) : (N >= 4 ? 60.0 : 170.0);
    vector<pair<int, int> > bestAny;
    double anyScore = -1;
    for (size_t i = 0; i < cands.size(); i++) {
        int L = (int)cands[i].size();
        if (L > cap) continue;
        long long gain = simulateGain(cands[i]);
        // extra safety: discount routes whose trail runs right past an opponent
        double near = 0;
        for (int k = 1; k < L; k++) {
            int dm = 1000000;
            for (int o = 0; o < N; o++) {
                if (o == ME) continue;
                dm = min(dm, abs(PX[o] - cands[i][k].first) + abs(PY[o] - cands[i][k].second));
            }
            if (dm <= 5) near += 1;
        }
        near = L > 1 ? near / (L - 1) : 0.0;
        double x = L / L0;
        double pen = 1.0 + x * x * x;
        double sc = (double)gain / (L + 1) / pen * (1.0 - 0.85 * near);
        if (sc > bestScore) { bestScore = sc; best = cands[i]; }
        double raw = (double)gain / (L + 1);
        if (raw > anyScore) { anyScore = raw; bestAny = cands[i]; }
    }
    if (best.empty()) best = bestAny;   // never stall: take the best raw route
    route = best;
}

int wanderDir() {
    int mx = PX[ME], my = PY[ME], md = PD[ME];
    char mt = (char)('0' + ME);
    static const int cyc[4] = {3, 1, 2, 0};
    for (int k = 0; k < 4; k++) {
        int d = cyc[(wanderPhase + k) % 4];
        if (d == oppd(md)) continue;
        int nx = mx + DX[d], ny = my + DY[d];
        if (!inB(nx, ny)) continue;
        if (TR[ny][nx] == mt) continue;
        wanderPhase = (wanderPhase + k + 1) % 4;
        return d;
    }
    return md;
}

bool legalMove(int d) {
    if (d < 0 || d > 3) return false;
    if (d == oppd(PD[ME])) return false;
    int nx = PX[ME] + DX[d], ny = PY[ME] + DY[d];
    if (!inB(nx, ny)) return false;
    if (TR[ny][nx] == (char)('0' + ME)) return false;
    return true;
}

int fallbackDir() {
    int mx = PX[ME], my = PY[ME], md = PD[ME];
    char mt = (char)('0' + ME);
    vector<int> from(W * H, -1), fdir(W * H, -1);
    deque<int> q;
    int st = my * W + mx;
    from[st] = st;
    q.push_back(st);
    int goal = -1;
    while (!q.empty()) {
        int k = q.front(); q.pop_front();
        int x = k % W, y = k / W;
        if (!(x == mx && y == my) && isMine(x, y)) { goal = k; break; }
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inB(nx, ny)) continue;
            if (TR[ny][nx] == mt) continue;
            int nk = ny * W + nx;
            if (from[nk] != -1) continue;
            from[nk] = k; fdir[nk] = d;
            q.push_back(nk);
        }
    }
    if (goal >= 0) {
        vector<int> path;
        int c = goal;
        while (c != st) { path.push_back(fdir[c]); c = from[c]; }
        reverse(path.begin(), path.end());
        if (!path.empty() && legalMove(path[0])) return path[0];
    }
    for (int d = 0; d < 4; d++) if (legalMove(d)) return d;
    return md;
}

int routeDir() {
    int mx = PX[ME], my = PY[ME];
    if (!route.empty() && rstep < (int)route.size() &&
        route[rstep].first == mx && route[rstep].second == my &&
        rstep + 1 < (int)route.size()) {
        int d = dirOf(mx, my, route[rstep + 1].first, route[rstep + 1].second);
        if (legalMove(d)) { rstep++; return d; }
    }
    replan();
    if ((int)route.size() >= 2 && route[0].first == mx && route[0].second == my) {
        int d = dirOf(mx, my, route[1].first, route[1].second);
        if (legalMove(d)) { rstep = 1; return d; }
    }
    route.clear();
    return fallbackDir();
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    string tok;
    if (!(cin >> tok)) return 0;
    cin >> W >> H >> MAXT >> N >> ME >> MOVE_MS >> INIT_MS;
    if (N > 8) N = 8;
    for (int i = 0; i < N; i++) {
        int id, x, y;
        cin >> tok >> id >> x >> y;
        if (id >= 0 && id < N) { PX[id] = x; PY[id] = y; }
    }
    CX = PX[ME]; CY = PY[ME];
    buildRing();

    while (readState()) {
        int d;
        long long big = (long long)W * H * 6 / 10;
        if (AR[ME] > big) d = wanderDir();
        else d = routeDir();
        if (!legalMove(d)) d = PD[ME];
        cout << TURN << ' ' << DC[d] << endl;
    }
    return 0;
}
