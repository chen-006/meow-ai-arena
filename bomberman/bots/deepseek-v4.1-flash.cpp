// Bomberman bot for 2-10 players (bomb-arena-0.5-public-state).
//
// Strategy, in one paragraph per layer:
//
// 1. Danger timeline. For every future turn k the bot rebuilds the exact blast
//    geometry of the rules: shrink layers turning cells into walls (bombs, crates
//    and flames on them removed), flame expiry, the full explosion phase over one
//    pre-explosion snapshot (fuse expiry or a bomb already sitting in flame, rays
//    of three cells, crates burned and blocking, a hit bomb chaining immediately and
//    blocking that ray), and crate removal. That yields, per turn, which cells block
//    movement, which hold bombs, and which kill anyone standing on them. This
//    timeline was validated cell-for-cell against the reference engine.
// 2. Survival horizon. A time-expanded search over the timeline gives the number of
//    turns the bot can keep itself alive, the smallest number of distinct escape
//    cells available along the way, and the size of the reachable set at the end.
//    The pessimistic pass assumes every living rival drops a bomb next turn on the
//    cell it occupies and on the passable neighbour that closes the distance to us,
//    and that rival cells cannot be entered; the optimistic pass uses only the bombs
//    already on the board with a long horizon so that wanderers still walk towards
//    the shrinking centre.
// 3. Choice. Each of the ten (move, place?) pairs is scored on both horizons plus a
//    positional term: depth band a couple of layers inside the current wall,
//    local openness, crowding penalty near rivals, revisit penalty. Because scoring
//    is joint, a bomb that would trap the bot - including one that chains an adjacent
//    bomb straight back onto its own cell - is simply never selected.
// 4. Offense. A placement is credited by how far it cuts each rival's own survival
//    horizon and escape set (recomputed with the bomb added), with a large bonus when
//    a rival is trapped outright and when the bomb sits next to a rival bomb, where a
//    chain detonation does the work. Crates come for free: the timeline burns them,
//    so bombing is also how the bot opens its own escape routes when boxed in.
//
// Public state only, no randomness, no file or network access.
#include <bits/stdc++.h>
#include <chrono>
using namespace std;

static int N = 13, P = 2, ME = 0, T = 0, LIMIT = 300, FUSE = 4, RANGE = 3, CAP = 2, FIRE = 2,
           SHRINK = 150, EVERY = 20;
static const int DX[5] = {0, 0, -1, 1, 0};
static const int DY[5] = {-1, 1, 0, 0, 0};
static const char DC[5] = {'U', 'D', 'L', 'R', 'S'};

struct Pl { int x = 0, y = 0, alive = 1; };
struct Bm { int owner = 0, x = 0, y = 0, at = 0; };

static vector<string> board;
static vector<Pl> players;
static vector<Bm> bombs;
static vector<array<int, 3>> flames;  // x,y,last lethal turn

static inline int cell(int x, int y) { return y * N + x; }
static inline int CX(int c) { return c % N; }
static inline int CY(int c) { return c / N; }
static inline int edgeOf(int c) { int x = CX(c), y = CY(c); return min(min(x, y), min(N - 1 - x, N - 1 - y)); }

// ---------------------------------------------------------------- timeline
// For each future turn T+k we record, at movement phase and at end of turn:
//   blk  : cell blocks movement (permanent wall or standing crate)
//   occ  : bomb occupies the cell during that turn's movement phase
//   leak : standing on the cell at the end of the turn kills you (wall or flame)
struct TL {
    int H = 0, NN = 0;
    vector<unsigned char> blk, occ, leak;
    void init(int h, int nn) {
        H = h; NN = nn;
        blk.assign((size_t)h * nn, 0);
        occ.assign((size_t)h * nn, 0);
        leak.assign((size_t)h * nn, 0);
    }
};

static void buildTL(const vector<Bm>& bin, int H, TL& out) {
    int NN = N * N;
    out.init(H, NN);
    vector<char> g(NN);
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) g[cell(x, y)] = board[y][x];
    vector<int> fexp(NN, -1);
    for (auto& f : flames) {
        int c = cell(f[0], f[1]);
        fexp[c] = max(fexp[c], f[2]);
    }
    vector<Bm> bs = bin;
    vector<int> at(NN, -1);
    vector<char> gone, hit;
    vector<int> pending;
    for (int k = 0; k < H; k++) {
        int now = T + k;
        if (now >= SHRINK && (now - SHRINK) % EVERY == 0) {
            int layer = 1 + (now - SHRINK) / EVERY;
            for (int y = 0; y < N; y++)
                for (int x = 0; x < N; x++) {
                    if (min(min(x, y), min(N - 1 - x, N - 1 - y)) <= layer) {
                        int c = cell(x, y);
                        g[c] = '#';
                        fexp[c] = -1;
                    }
                }
            size_t w = 0;
            for (size_t i = 0; i < bs.size(); i++)
                if (g[cell(bs[i].x, bs[i].y)] != '#') bs[w++] = bs[i];
            bs.resize(w);
        }
        for (int c = 0; c < NN; c++)
            if (fexp[c] < now) fexp[c] = -1;
        unsigned char* pb = &out.blk[(size_t)k * NN];
        unsigned char* po = &out.occ[(size_t)k * NN];
        unsigned char* pl = &out.leak[(size_t)k * NN];
        fill(po, po + NN, 0);
        for (int c = 0; c < NN; c++) {
            char ch = g[c];
            pb[c] = (ch == '.') ? 0 : 1;
            pl[c] = (ch == '#') ? 1 : 0;
        }
        fill(at.begin(), at.end(), -1);
        for (size_t i = 0; i < bs.size(); i++) {
            int c = cell(bs[i].x, bs[i].y);
            at[c] = (int)i;
            po[c] = 1;
        }
        // explosion phase (single pre-explosion snapshot for crates and bombs)
        pending.clear();
        for (size_t i = 0; i < bs.size(); i++)
            if (bs[i].at <= now || fexp[cell(bs[i].x, bs[i].y)] >= now) pending.push_back((int)i);
        gone.assign(bs.size(), 0);
        hit.assign(NN, 0);
        vector<int> destroyed;
        while (!pending.empty()) {
            int i = pending.back();
            pending.pop_back();
            if (gone[i]) continue;
            gone[i] = 1;
            const Bm b = bs[i];
            hit[cell(b.x, b.y)] = 1;
            for (int d = 0; d < 4; d++) {
                int x = b.x, y = b.y;
                for (int r = 1; r <= RANGE; r++) {
                    x += DX[d]; y += DY[d];
                    if (x < 0 || y < 0 || x >= N || y >= N) break;
                    int c = cell(x, y);
                    if (g[c] == '#') break;
                    hit[c] = 1;
                    if (g[c] == '+') { destroyed.push_back(c); break; }
                    if (at[c] >= 0) { pending.push_back(at[c]); break; }
                }
            }
        }
        size_t w = 0;
        for (size_t i = 0; i < bs.size(); i++)
            if (!gone[i]) bs[w++] = bs[i];
        bs.resize(w);
        for (int c : destroyed) g[c] = '.';
        for (int c = 0; c < NN; c++)
            if (hit[c]) fexp[c] = max(fexp[c], now + FIRE - 1);
        for (int c = 0; c < NN; c++)
            if (fexp[c] >= now) pl[c] = 1;
    }
}

// ------------------------------------------------------------- reachability
struct Reach {
    int life = 0;      // number of turns (T..T+life-1) that can be survived
    int endCount = 0;  // distinct cells reachable alive at the horizon end
    int minCount = 0;  // smallest reachable-set size along the way
};

static Reach survive(const TL& tl, int start, const vector<char>* eblk, int blockUntil) {
    int NN = N * N;
    Reach r;
    vector<char> cur(NN, 0), nxt(NN, 0);
    if (start < 0 || start >= NN || tl.leak[start]) return r;
    cur[start] = 1;
    r.life = 1;
    r.minCount = 1;
    for (int k = 1; k < tl.H; k++) {
        fill(nxt.begin(), nxt.end(), 0);
        const unsigned char* pb = &tl.blk[(size_t)k * NN];
        const unsigned char* po = &tl.occ[(size_t)k * NN];
        const unsigned char* pl = &tl.leak[(size_t)k * NN];
        int cnt = 0;
        for (int c = 0; c < NN; c++) {
            if (!cur[c]) continue;
            int x = CX(c), y = CY(c);
            for (int d = 0; d < 5; d++) {
                int nx = x + DX[d], ny = y + DY[d];
                if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
                int b = cell(nx, ny);
                if (b != c) {
                    if (pb[b] || po[b]) continue;
                    if (eblk && (k <= blockUntil) && (*eblk)[b]) continue;
                }
                if (pl[b]) continue;
                if (!nxt[b]) { nxt[b] = 1; cnt++; }
            }
        }
        if (!cnt) break;
        cur.swap(nxt);
        r.life = k + 1;
        r.minCount = min(r.minCount, cnt);
        r.endCount = cnt;
    }
    return r;
}

// ----------------------------------------------------------------- helpers
static int aliveCount() {
    int c = 0;
    for (auto& p : players) c += p.alive;
    return c;
}

static double openness(int c, int rad) {
    int x0 = CX(c), y0 = CY(c), cnt = 0;
    for (int y = max(0, y0 - rad); y <= min(N - 1, y0 + rad); y++)
        for (int x = max(0, x0 - rad); x <= min(N - 1, x0 + rad); x++)
            if (board[y][x] == '.') cnt++;
    return cnt;
}

// ---------------------------------------------------------------- decision
static bool DBG = false;
static vector<int> visitHist;

static pair<int, int> decide() {
    auto t0 = chrono::steady_clock::now();
    int NN = N * N;
    int mx = players[ME].x, my = players[ME].y;
    int myCell = cell(mx, my);
    visitHist.push_back(myCell);
    if (visitHist.size() > 40) visitHist.erase(visitHist.begin());

    vector<char> eblk(NN, 0);
    vector<int> enemies;
    for (int i = 0; i < P; i++) {
        if (i == ME || !players[i].alive) continue;
        eblk[cell(players[i].x, players[i].y)] = 1;
        enemies.push_back(i);
    }

    // ------- placement legality
    int myBombs = 0;
    bool bombUnderMe = false, shared = false;
    for (auto& b : bombs) {
        if (b.owner == ME) myBombs++;
        if (b.x == mx && b.y == my) bombUnderMe = true;
    }
    for (int i = 0; i < P; i++)
        if (i != ME && players[i].alive && players[i].x == mx && players[i].y == my) shared = true;
    bool canPlace = (myBombs < CAP) && !bombUnderMe && !shared;

    // Terrain-obeying target for the current turn. Enemy cells are not treated as
    // hard blockers: standing still is scored separately, so a move into a cell a
    // rival is vacating stays available when standing still means dying.
    vector<char> occNow(NN, 0);
    for (auto& b : bombs) occNow[cell(b.x, b.y)] = 1;
    auto myTarget = [&](int d) {
        int nx = mx + DX[d], ny = my + DY[d];
        if (nx < 0 || ny < 0 || nx >= N || ny >= N) return myCell;
        if (board[ny][nx] != '.') return myCell;
        int c = cell(nx, ny);
        if (c != myCell && occNow[c]) return myCell;
        return c;
    };
    // my own cell is always legal to hold (unless shrink, handled by timeline)

    // ------- timelines per placement option
    const int HP = 22;   // pessimistic horizon
    const int HO = 46;   // optimistic horizon (shrink planning)
    TL tlNoP, tlYesP, tlNoO, tlYesO;
    vector<Bm> basePred = bombs;
    for (int i : enemies) {
        // Rivals bomb while closing in, so model the two most likely bomb sites:
        // where they stand now, and the passable neighbour that gets closest to us.
        int cnt = 0;
        bool under = false;
        for (auto& b : bombs) {
            if (b.owner == i) cnt++;
            if (b.x == players[i].x && b.y == players[i].y) under = true;
        }
        if (cnt < CAP && !under) basePred.push_back(Bm{i, players[i].x, players[i].y, T + 1 + FUSE});
        int bx = players[i].x, by = players[i].y;
        int bestd = abs(bx - mx) + abs(by - my), bestc = -1;
        for (int d = 0; d < 4; d++) {
            int nx = bx + DX[d], ny = by + DY[d];
            if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
            if (board[ny][nx] != '.') continue;
            int c = cell(nx, ny);
            bool occ = false;
            for (auto& b : bombs)
                if (b.x == nx && b.y == ny) occ = true;
            if (occ) continue;
            int dist = abs(nx - mx) + abs(ny - my);
            if (dist < bestd) { bestd = dist; bestc = c; }
        }
        if (bestc >= 0) basePred.push_back(Bm{i, CX(bestc), CY(bestc), T + 2 + FUSE});
    }
    buildTL(basePred, HP, tlNoP);  // pessimistic: everyone may bomb immediately
    buildTL(bombs, HO, tlNoO);     // optimistic: current bombs only, long horizon
    if (canPlace) {
        vector<Bm> yes = bombs;
        yes.push_back(Bm{ME, mx, my, T + FUSE});
        vector<Bm> yesPred = basePred;
        yesPred.push_back(Bm{ME, mx, my, T + FUSE});
        buildTL(yesPred, HP, tlYesP);
        buildTL(yes, HO, tlYesO);
    }

    // enemy baseline horizons (neutral timeline: current bombs only)
    struct EnInfo { int baseLife, baseCnt, baseMin; };
    vector<EnInfo> enBase(enemies.size());
    for (size_t j = 0; j < enemies.size(); j++) {
        int e = enemies[j];
        Reach rr = survive(tlNoO, cell(players[e].x, players[e].y), &eblk, 0);
        enBase[j] = {rr.life, rr.endCount, rr.minCount};
    }

    // ------- evaluate (placement, direction) pairs jointly
    int curLayer = (T < SHRINK) ? 0 : 1 + (T - SHRINK) / EVERY;
    int aliveNow = aliveCount();
    // depth band: stay a couple of layers inside the current wall, deeper as it closes
    int want = curLayer + (aliveNow > 4 ? 2 : 3);

    double bestScore = -1e18;
    int bestPlace = 0, bestD = 4;
    for (int place = 0; place <= 1; place++) {
        if (place && !canPlace) continue;
        const TL& tp = place ? tlYesP : tlNoP;
        const TL& to = place ? tlYesO : tlNoO;
        // offensive value of this bomb (independent of my own destination)
        double off = 0;
        if (place) {
            for (size_t j = 0; j < enemies.size(); j++) {
                int ec = cell(players[enemies[j]].x, players[enemies[j]].y);
                Reach rb = survive(to, ec, &eblk, 0);
                if (rb.life == 0) continue;  // already doomed by other bombs
                int dLife = enBase[j].baseLife - rb.life;
                int dMin = enBase[j].baseMin - rb.minCount;
                int dCnt = enBase[j].baseCnt - rb.endCount;
                if (rb.life <= 1) off += 700;  // trapped outright by this bomb
                off += 110.0 * max(0, dLife);
                off += 26.0 * max(0, dMin);
                off += 7.0 * max(0, dCnt);
                // chain bonus: my bomb detonating an enemy bomb is a strong trap
                for (auto& b : bombs)
                    if (b.owner != ME && (int)abs(b.x - mx) + (int)abs(b.y - my) <= RANGE + 1) off += 60;
            }
            off -= 2.0;  // placement inertia
        }
        for (int d = 0; d < 5; d++) {
            int dest = myTarget(d);
            Reach rp = survive(tp, dest, &eblk, HP);
            Reach ro = survive(to, dest, &eblk, 0);
            double sc = 900.0 * rp.life + 25.0 * ro.life + 44.0 * min(6, rp.minCount) +
                        3.0 * min(14, ro.endCount);
            int e = edgeOf(dest);
            sc -= 26.0 * abs(e - want);
            sc += 1.6 * openness(dest, 2);
            for (int i : enemies) {
                int dist = abs(players[i].x - CX(dest)) + abs(players[i].y - CY(dest));
                if (dist == 0) sc -= 500;
                else if (dist == 1) sc -= 30;
                else if (dist == 2) sc -= 13;
                else if (dist == 3) sc -= 5;
            }
            int seen = 0;
            for (int v : visitHist)
                if (v == dest) seen++;
            sc -= 1.5 * seen;
            if (d == 4) sc -= 0.4;
            if (dest == myCell && d != 4) sc -= 4;  // requested a blocked move
            sc += off;
            if (DBG)
                fprintf(stderr, "place=%d d=%c dest=(%d,%d) rp.life=%d rp.min=%d ro.life=%d edge=%d sc=%.1f off=%.1f\n",
                        place, DC[d], CX(dest), CY(dest), rp.life, rp.minCount, ro.life, e, sc, off);
            if (sc > bestScore) { bestScore = sc; bestPlace = place; bestD = d; }
        }
    }
    if (DBG) fprintf(stderr, "  select d=%c place=%d compute=%.2fms\n", DC[bestD], bestPlace,
                      chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count());
    return {bestD, bestPlace};
}

// ------------------------------------------------------------------- input
static bool readState() {
    string w;
    if (!(cin >> w)) return false;
    if (w != "TURN") return false;
    cin >> T;
    cin >> w;  // BOARD
    if (w != "BOARD") return false;
    for (int y = 0; y < N; y++) cin >> board[y];
    players.assign(P, Pl{});
    for (int i = 0; i < P; i++) {
        int id, x, y, a;
        cin >> w >> id >> x >> y >> a;
        players[id].x = x; players[id].y = y; players[id].alive = a;
    }
    int k;
    cin >> w >> k;  // BOMBS
    bombs.clear();
    for (int i = 0; i < k; i++) {
        Bm b;
        cin >> w >> b.owner >> b.x >> b.y >> b.at;
        bombs.push_back(b);
    }
    cin >> w >> k;  // FLAMES
    flames.clear();
    for (int i = 0; i < k; i++) {
        array<int, 3> f;
        cin >> w >> f[0] >> f[1] >> f[2];
        flames.push_back(f);
    }
    // STATUS / SOURCES carry score bookkeeping this bot does not need; drain
    // whatever follows so an added section cannot desynchronise the stream.
    while (cin >> w)
        if (w == "END") return true;
    return false;
}

int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string w;
    if (!(cin >> w >> N >> P >> ME >> LIMIT >> FUSE >> RANGE >> CAP >> FIRE >> SHRINK >> EVERY)) return 0;
    DBG = (argc > 1 && string(argv[1]) == "--dbg");
    bool DUMP = (argc > 1 && string(argv[1]) == "--dump");
    board.assign(N, string(N, '.'));
    players.assign(P, Pl{});
    while (readState()) {
        if (DUMP) {
            TL tl;
            buildTL(bombs, 12, tl);
            for (int k = 0; k < tl.H; k++) {
                string a, b;
                for (int c = 0; c < N * N; c++) {
                    a += ('0' + tl.leak[(size_t)k * N * N + c]);
                    b += ('0' + tl.occ[(size_t)k * N * N + c]);
                }
                fprintf(stderr, "TL %d %s %s\n", T + k, a.c_str(), b.c_str());
            }
            fflush(stderr);
        }
        auto ans = decide();
        cout << T << ' ' << DC[ans.first] << ' ' << ans.second << '\n' << flush;
    }
    return 0;
}
