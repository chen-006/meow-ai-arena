// Harrier - a territory-battle bot for meow-ai-arena, episode 1 (land).
//
// Architecture (all original, written from scratch for this submission):
//   * Plan whole capture loops, not single steps. A candidate is a full path
//     from the current position that ends by re-entering our own territory,
//     which is exactly when the engine triggers a capture.
//   * Candidate families: rectangles of every depth/width in the three legal
//     opening directions, plus shortest-path runs home when we are exposed.
//     Kills do not win games (victims respawn with their territory intact),
//     so opponents are only cut opportunistically when their trail is right
//     next to us.
//   * Every candidate is scored by simulating the exact engine capture
//     (trail cells + boundary flood-fill enclosure) and dividing gained
//     cells by total time; enemy land taken counts extra.
//   * A beeline safety model filters candidates: while our trail is alive,
//     any live opponent that can reach a trail cell no earlier than we lay
//     it and no later than we capture is assumed to cut us. Plans are
//     re-checked against fresh opponent positions every turn; when a plan
//     goes bad we switch to the shortest way home.
//
// Communicates only over stdin/stdout as the protocol requires.

#include <bits/stdc++.h>
using namespace std;

// Direction encoding: 0=U 1=D 2=L 3=R; opposite = d^1.
static const int DX[4] = {0, 0, -1, 1};
static const int DY[4] = {-1, 1, 0, 0};
static const char DC[4] = {'U', 'D', 'L', 'R'};

static int dirIndex(char c) { return c == 'U' ? 0 : c == 'D' ? 1 : c == 'L' ? 2 : 3; }

// ---------------- tunables ----------------
namespace cfg {
const int stealW = 12;   // per-10 extra weight for enemy cells: 12 -> 1.2x
const int timePref = 2;  // constant added to loop time in the denominator
const int margin = 1;    // extra turns of safety beyond the capture turn
const int maxDepth = 13; // how far past the border rectangles may reach
const int shiftK = 8;    // how far a loop anchor may slide through our land
const int planMs = 12;   // soft planning time budget per turn
const int staticLim = 8; // turns of total stasis before an enemy is ignored
const int riskW = 10;    // value units charged per exposure unit
const int fear = 9;      // an enemy this close to a cell when we lay it is acute
const int hazardK = 100; // hazard charged as K / (detour beyond the fear window)
const int chaseClose = 3;   // consecutive closing turns before an enemy is a chaser
const int chaseRange = 16;  // and it must be this close to our trail to count
const int chaserHazardMul = 4; // chasers get this multiple of the hazard price
const int chaserMaxT = 26; // loop length cap while a chaser is on us
const int bailWin = 10;   // bail only when a chaser is this close to our trail
const int meleeMaxT = 40; // melee loop cap (0 = none)
const int meleeSlack = 6; // melee: veto unless loop closes this far before any arrival (0 = off)
const int killBase = 120;// tempo value of cutting a victim mid-loop, plus
const int killPer = 8;   // this much per trail cell it loses (capped below)
const int killCap = 320;
} // namespace cfg

// ---------------- game state ----------------
static int W, H, NC, MAXTURNS, N, ME, MOVE_MS, INIT_MS;
static int SX[4], SY[4];            // spawns
static int8_t protOwner[48 * 48];   // protected-zone owner per cell (-1 none)

struct St {
    int turn;
    int px[4], py[4], pdir[4], ptrail[4], parea[4], pdeaths[4];
    // owner/trail per cell: -1 empty, else player id
    vector<int8_t> owner, trail;
};
static St st;

static inline int idx(int x, int y) { return y * W + x; }
static inline bool inb(int x, int y) { return x >= 0 && x < W && y >= 0 && y < H; }
static inline int man(int ax, int ay, int bx, int by) {
    return abs(ax - bx) + abs(ay - by);
}
static inline bool protOther(int k) {
    return protOwner[k] >= 0 && protOwner[k] != ME;
}

// per-turn derived data
static vector<int> myTrailCells;    // cells with trail == ME
static bool threat[4];              // enemies that can still move/cut
static int staticTurns[4];
static int closing[4];              // consecutive turns an enemy moved toward us
static bool chaser[4];              // sustained pursuit: it is hunting, not racing

static void refreshDerived() {
    myTrailCells.clear();
    myTrailCells.reserve(256);
    for (int k = 0; k < NC; k++)
        if (st.trail[k] == ME) myTrailCells.push_back(k);
}

// Any opponent currently hunting us (drives defensive loop sizing).
static bool anyChaser() {
    for (int e = 0; e < N; e++)
        if (e != ME && chaser[e]) return true;
    return false;
}

// enemies frozen for many turns (forfeited in melee) cannot cut us; enemies
// whose heading keeps closing on us are chasing rather than racing
static void updateEnemies() {
    static int lx[4], ly[4], ld[4], lt[4], la[4], ldd[4];
    static bool init = false;
    for (int e = 0; e < N; e++) {
        if (e == ME) continue;
        bool same = init && lx[e] == st.px[e] && ly[e] == st.py[e] && ld[e] == st.pdir[e] &&
                    lt[e] == st.ptrail[e] && la[e] == st.parea[e] && ldd[e] == st.pdeaths[e];
        staticTurns[e] = same ? staticTurns[e] + 1 : 0;
        lx[e] = st.px[e]; ly[e] = st.py[e]; ld[e] = st.pdir[e];
        lt[e] = st.ptrail[e]; la[e] = st.parea[e]; ldd[e] = st.pdeaths[e];
        threat[e] = staticTurns[e] < cfg::staticLim;
    }
    init = true;
}

// Chaser detection, phase 2 (after refreshDerived): an enemy counts as
// closing when its heading points at our nearest trail cell (or our head when
// we have no trail). It only registers as a chaser while actually committed
// outside its own land and close enough to matter - a farmer circling its own
// side points at us all the time and converts nothing.
static void updateChasers() {
    for (int e = 0; e < N; e++) {
        if (e == ME) continue;
        int tx = st.px[ME], ty = st.py[ME], d = 1 << 30;
        for (int c : myTrailCells) {
            int m = man(st.px[e], st.py[e], c % W, c / W);
            if (m < d) { d = m; tx = c % W; ty = c / W; }
        }
        int vx = tx - st.px[e], vy = ty - st.py[e];
        int dot = DX[st.pdir[e]] * vx + DY[st.pdir[e]] * vy;
        if (dot > 0) closing[e] = min(closing[e] + 1, 30);
        else closing[e] = max(closing[e] - 2, 0);
        bool outside = st.owner[idx(st.px[e], st.py[e])] != e || st.ptrail[e] > 0;
        chaser[e] = closing[e] >= cfg::chaseClose && outside && d <= cfg::chaseRange;
    }
}

// ---------------- capture simulation (mirrors engine/game.py) ----------------
// walls = our territory + the given trail cells; flood from the board border;
// count cells that would become ours (not already ours, not protected by
// someone else). Returns (gain, steal).
static void gainFromCapture(const vector<int>& trailCells, int& gain, int& steal) {
    static vector<uint8_t> wall, seen;
    wall.assign(NC, 0);
    seen.assign(NC, 0);
    for (int k = 0; k < NC; k++)
        if (st.owner[k] == ME) wall[k] = 1;
    for (int k : trailCells) wall[k] = 1;
    static vector<int> stackv;
    stackv.clear();
    auto seed = [&](int k) {
        if (!wall[k] && !seen[k]) { seen[k] = 1; stackv.push_back(k); }
    };
    for (int x = 0; x < W; x++) { seed(x); seed((H - 1) * W + x); }
    for (int y = 0; y < H; y++) { seed(y * W); seed(y * W + W - 1); }
    while (!stackv.empty()) {
        int k = stackv.back(); stackv.pop_back();
        int x = k % W;
        if (k >= W) { int n = k - W; if (!seen[n]) { seen[n] = 1; stackv.push_back(n); } }
        if (k < W * (H - 1)) { int n = k + W; if (!seen[n]) { seen[n] = 1; stackv.push_back(n); } }
        if (x > 0) { int n = k - 1; if (!seen[n]) { seen[n] = 1; stackv.push_back(n); } }
        if (x < W - 1) { int n = k + 1; if (!seen[n]) { seen[n] = 1; stackv.push_back(n); } }
    }
    gain = 0; steal = 0;
    auto count = [&](int k) {
        if (st.owner[k] != ME && !protOther(k)) {
            gain++;
            if (st.owner[k] >= 0) steal++;
        }
    };
    for (int k : trailCells) count(k);
    for (int k = 0; k < NC; k++)
        if (!wall[k] && !seen[k]) count(k);
}

// ---------------- path evaluation ----------------
struct Eval {
    bool valid = false;   // path is walkable and (if it laid a trail) captures
    bool safe = false;    // passes the beeline safety model
    int R = 0;            // 1-based arrival turn of the capture (0: no trail laid)
    int T = 0;            // path length in turns
    int gain = 0, steal = 0;
    double score = -1e18;
};

// Walk `dirs` from the current position (pass 1: find capture turn R and the
// cells the path would add to our trail). Returns an invalid Eval if the path
// leaves the board, steps on our own trail, or never re-enters our territory
// after laying one.
static Eval walkPath(const vector<int>& dirs) {
    Eval ev;
    int x = st.px[ME], y = st.py[ME];
    bool laid = false;
    int arrival = 0;
    for (int d : dirs) {
        arrival++;
        x += DX[d]; y += DY[d];
        if (!inb(x, y)) return ev;
        int k = idx(x, y);
        if (st.trail[k] == ME) return ev;          // suicide on own trail
        bool mine = st.owner[k] == ME;
        if (mine && laid) { ev.R = arrival; ev.T = arrival; break; }
        if (!mine) laid = true;
        if (arrival == (int)dirs.size()) { ev.T = arrival; break; }
    }
    if (!laid) {                                    // never left home
        ev.valid = true; ev.R = 0; ev.T = arrival;
        return ev;
    }
    if (ev.R == 0) return ev;                       // exposed with no way home
    ev.valid = true;
    return ev;
}

// Beeline exposure model, per opponent. For each opponent we take the
// tightest "gap" between when we lay a trail cell and when that opponent
// could step on it (laid cells have gap = distance now). Gaps inside `fear`
// are acute and vetoed elsewhere; beyond it, converting a gap into a cut
// requires a deliberate detour, so the hazard is priced as hazardK divided by
// the detour size - one number per opponent, independent of loop length.
// Simultaneous arrival on a future territory cell (head-on) also counts.
static double pathRisk(const vector<int>& dirs, const Eval& ev) {
    if (ev.R == 0) return 0;
    double risk = 0;
    int x = st.px[ME], y = st.py[ME];
    int arrival = 0;
    int headOn = 0;
    static int gapMin[4];
    for (int e = 0; e < N; e++) gapMin[e] = 1 << 30;
    for (int d : dirs) {
        arrival++;
        x += DX[d]; y += DY[d];
        if (arrival > ev.R) break;
        int k = idx(x, y);
        bool trailCell = st.owner[k] != ME;
        for (int e = 0; e < N; e++) {
            if (e == ME || !threat[e]) continue;
            int m = man(st.px[e], st.py[e], x, y);
            if (trailCell) {
                int gap = m - arrival;
                if (gap >= 0 && gap < gapMin[e]) gapMin[e] = gap;
            } else if (m == arrival) {
                headOn = 1;
            }
        }
    }
    for (int c : myTrailCells)
        for (int e = 0; e < N; e++) {
            if (e == ME || !threat[e]) continue;
            int gap = man(st.px[e], st.py[e], c % W, c / W);
            if (gap < gapMin[e]) gapMin[e] = gap;
        }
    for (int e = 0; e < N; e++) {
        if (e == ME || !threat[e]) continue;
        if (gapMin[e] == 1 << 30) continue;
        int detour = gapMin[e] - cfg::fear;
        if (detour > 0)
            risk += (double)cfg::hazardK * (chaser[e] ? cfg::chaserHazardMul : 1) / detour;
    }
    return risk + headOn;
}

// Earliest turn at which a pursuing opponent could step on one of our
// already-laid trail cells (a lower bound: straight-line motion). A farmer that happens to stand
// near our trail almost never converts the possibility; bailing for it just
// truncates every loop (measured: 25 bails per melee game, half the area).
static int earliestChaserHit() {
    int best = 1 << 30;
    for (int c : myTrailCells)
        for (int e = 0; e < N; e++) {
            if (e == ME || !threat[e] || !chaser[e]) continue;
            best = min(best, man(st.px[e], st.py[e], c % W, c / W));
        }
    return best;
}

// No hard veto on candidate loops. Passive opponents almost never convert
// "possible" beelines into cuts, and vetoing speculation starves the whole
// game (measured: tiny loops + standing still 7% of turns). Threats are
// handled reactively - the planner's hazard pricing biases loops away from
// enemies, and plan-following bails or rides once a pursuer is imminent.

// ---------------- BFS helpers ----------------
// Shortest path from (fx,fy) to a cell satisfying goal(), never stepping on
// our own trail. firstNoRev restricts the very first move (our position only).
// Returns direction list; empty if unreachable. Cells with our trail are
// deadly walls; everything else (enemy trails included) is passable.
template <class F>
static vector<int> bfsPath(int fx, int fy, int curDir, bool firstNoRev, F goal) {
    static vector<int> dist, via, vdir;
    dist.assign(NC, -1); via.assign(NC, -1); vdir.assign(NC, -1);
    int start = idx(fx, fy);
    dist[start] = 0;
    static int q[48 * 48];
    int qh = 0, qt = 0;
    if (goal(fx, fy)) return {};
    for (int d = 0; d < 4; d++) {
        if (firstNoRev && d == (curDir ^ 1)) continue;
        int nx = fx + DX[d], ny = fy + DY[d];
        if (!inb(nx, ny) || st.trail[idx(nx, ny)] == ME) continue;
        int k = idx(nx, ny);
        if (dist[k] != -1) continue;
        dist[k] = 1; via[k] = start; vdir[k] = d;
        q[qt++] = k;
    }
    int found = -1;
    while (qh < qt && found == -1) {
        int k = q[qh++];
        int x = k % W, y = k / W;
        if (goal(x, y)) { found = k; break; }
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inb(nx, ny) || st.trail[idx(nx, ny)] == ME) continue;
            int nk = idx(nx, ny);
            if (dist[nk] != -1) continue;
            dist[nk] = dist[k] + 1; via[nk] = k; vdir[nk] = d;
            q[qt++] = nk;
        }
    }
    if (found == -1) return {};
    // BFS discovers goals one level at a time; scan the whole current frontier
    // for the closest goal cell
    int bestK = -1, bestD = 1 << 30;
    for (int k = 0; k < NC; k++)
        if (dist[k] > 0 && goal(k % W, k / W) && dist[k] < bestD) { bestD = dist[k]; bestK = k; }
    if (bestK == -1) return {};
    vector<int> path;
    for (int c = bestK; c != start; c = via[c]) path.push_back(vdir[c]);
    reverse(path.begin(), path.end());
    return path;
}

static vector<int> bfsHome(int fx, int fy, int curDir, bool firstNoRev) {
    return bfsPath(fx, fy, curDir, firstNoRev, [](int x, int y) {
        return st.owner[idx(x, y)] == ME;
    });
}

// ---------------- candidate generation ----------------
// Prefix sums (rebuilt each turn) of cells not owned by us, for O(1) upper
// bounds on what a rectangle loop could possibly capture.
static vector<int> pfxNotMine;   // (W+1)*(H+1)

static void buildPrefix() {
    pfxNotMine.assign((W + 1) * (H + 1), 0);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int v = (st.owner[idx(x, y)] != ME) ? 1 : 0;
            pfxNotMine[(y + 1) * (W + 1) + (x + 1)] = pfxNotMine[y * (W + 1) + (x + 1)] +
                pfxNotMine[(y + 1) * (W + 1) + x] - pfxNotMine[y * (W + 1) + x] + v;
        }
}

static int boxNotMine(int x0, int y0, int x1, int y1) {
    x0 = max(x0, 0); y0 = max(y0, 0); x1 = min(x1, W - 1); y1 = min(y1, H - 1);
    if (x0 > x1 || y0 > y1) return 0;
    int W1 = W + 1;
    return pfxNotMine[(y1 + 1) * W1 + (x1 + 1)] - pfxNotMine[y0 * W1 + (x1 + 1)] -
           pfxNotMine[(y1 + 1) * W1 + x0] + pfxNotMine[y0 * W1 + x0];
}

// Build one rectangle path: `a` steps along dir, `b` steps along side,
// `a` steps back, then retrace the side leg to close the loop at the start
// cell (capture fires at the first own-territory cell on the way in).
static void buildRect(int dir, int a, int side, int b, vector<int>& out) {
    out.clear();
    for (int i = 0; i < a; i++) out.push_back(dir);
    for (int i = 0; i < b; i++) out.push_back(side);
    for (int i = 0; i < a; i++) out.push_back(dir ^ 1);
    for (int i = 0; i < b; i++) out.push_back(side ^ 1);
}

// ---------------- planner ----------------
static vector<int> plan;      // remaining committed directions
static int planKind = 0;      // 0 none, 1 loop, 2 run home
static int expX = -1, expY = -1;
static int lastDeathTurn = -400; // when we were last cut (adaptive aggression)
static int lastDeaths = 0;       // pdeaths[ME] as of the previous turn
static bool endgameGuard = false;

// Risk price per exposure unit, lowered while opponents leave our trails
// alone (pure racers) and restored the moment we are cut (hunters).
static int adaptiveRiskW() {
    int streak = lastDeathTurn < 0 ? st.turn : st.turn - lastDeathTurn;
    double f = 1.0 - streak / 300.0;
    if (f < 0.35) f = 0.35;
    double w = cfg::riskW * f;
    if (endgameGuard) w *= 2;
    return (int)max(1.0, w);
}

// Directions that keep us alive right now (in bounds, not our own trail).
static int anySafe() {
    int bestD = st.pdir[ME];
    long long bestScore = -1;
    for (int d = 0; d < 4; d++) {
        if (d == (st.pdir[ME] ^ 1)) continue;
        int nx = st.px[ME] + DX[d], ny = st.py[ME] + DY[d];
        if (!inb(nx, ny) || st.trail[idx(nx, ny)] == ME) continue;
        long long sc = 0;
        for (int e = 0; e < N; e++) {
            if (e == ME || !threat[e]) continue;
            int m = man(st.px[e], st.py[e], nx, ny);
            sc += (m <= 2) ? -1000 : m;
        }
        if (sc > bestScore) { bestScore = sc; bestD = d; }
    }
    return bestD;
}

// Wait inside our territory: pick the in-territory move farthest from enemies.
static int stayInside() {
    int bestD = -1;
    long long bestScore = -1;
    for (int d = 0; d < 4; d++) {
        if (d == (st.pdir[ME] ^ 1)) continue;
        int nx = st.px[ME] + DX[d], ny = st.py[ME] + DY[d];
        if (!inb(nx, ny) || st.trail[idx(nx, ny)] == ME) continue;
        if (st.owner[idx(nx, ny)] != ME) continue;
        long long sc = 1;
        for (int e = 0; e < N; e++) {
            if (e == ME || !threat[e]) continue;
            sc += 100 * man(st.px[e], st.py[e], nx, ny);
        }
        if (sc > bestScore) { bestScore = sc; bestD = d; }
    }
    if (bestD >= 0) return bestD;
    return anySafe();
}

// Immediate cut: an opponent trail cell right next to us with no risk of a
// simultaneous head-on arrival.
static int adjacentCut() {
    for (int d = 0; d < 4; d++) {
        if (d == (st.pdir[ME] ^ 1)) continue;
        int nx = st.px[ME] + DX[d], ny = st.py[ME] + DY[d];
        if (!inb(nx, ny)) continue;
        int k = idx(nx, ny);
        int e = st.trail[k];
        if (e < 0 || e == ME || !threat[e]) continue;
        bool risky = false;
        for (int f = 0; f < N; f++) {
            if (f == ME || !threat[f]) continue;
            if (man(st.px[f], st.py[f], nx, ny) == 1) { risky = true; break; }
        }
        if (!risky) return d;
    }
    return -1;
}

// Cheapest Manhattan distance from enemy e's head to its own territory: a
// lower bound on how soon it can capture and dissolve its trail.
static int enemyDistHome(int e) {
    int best = 1 << 30;
    for (int k = 0; k < NC; k++)
        if (st.owner[k] == e) {
            best = min(best, man(st.px[e], st.py[e], k % W, k / W));
            if (best <= 1) break;
        }
    return best;
}

static int decide() {
    // death detection: if we are not where our last move should have put us,
    // we were cut and respawned; any committed plan is void
    if (st.pdeaths[ME] > lastDeaths) lastDeathTurn = st.turn;
    lastDeaths = st.pdeaths[ME];
    endgameGuard = false;
    if (expX >= 0 && (st.px[ME] != expX || st.py[ME] != expY)) {
        plan.clear(); planKind = 0;
    }
    expX = -1;

    int remaining = MAXTURNS - st.turn;
    bool inside = st.owner[idx(st.px[ME], st.py[ME])] == ME;

    // Guard an area lead at the very end: with a clear lead, double the risk
    // price instead of freezing (a stationary leader just gets wrapped up and
    // stripped by one big enemy loop).
    int maxOther = 0;
    for (int e = 0; e < N; e++) if (e != ME) maxOther = max(maxOther, st.parea[e]);
    if (remaining < 70 && (long long)st.parea[ME] * 4 >= (long long)maxOther * 5)
        endgameGuard = true;

    int cut = adjacentCut();
    if (cut >= 0) { plan.clear(); planKind = 0; return cut; }

    // Follow a committed plan while it stays valid and safe.
    if (!plan.empty()) {
        if (plan.front() == (st.pdir[ME] ^ 1)) { plan.clear(); planKind = 0; }
        else if (planKind == 2) {
            // runs home are recomputed every turn against fresh positions
            plan = bfsHome(st.px[ME], st.py[ME], st.pdir[ME], true);
            if (!plan.empty()) return plan.front();
            plan.clear(); planKind = 0;
            return anySafe();
        } else {
            Eval ev = walkPath(plan);
            if (ev.valid && ev.R > 0 && ev.R <= remaining - 1) {
                // Beeline test against actual pursuers: nobody hunting us can
                // touch a laid cell before our capture at R, so ride it out.
                // Otherwise bail for home only if that re-enters before the
                // earliest possible hit; if running cannot save us, finish.
                int earliest = earliestChaserHit();
                if (earliest > cfg::bailWin) return plan.front();
                {
                    vector<int> home = bfsHome(st.px[ME], st.py[ME], st.pdir[ME], true);
                    if (!home.empty() && (int)home.size() + 1 < earliest) {
                        plan = home; planKind = 2;
                        return plan.front();
                    }
                    return plan.front();
                }
            } else {
                plan.clear(); planKind = 0;
            }
        }
        if (!inside) {
            plan = bfsHome(st.px[ME], st.py[ME], st.pdir[ME], true);
            if (!plan.empty()) { planKind = 2; return plan.front(); }
            return anySafe();
        }
    }

    // ---------------- full replan ----------------
    auto t0 = chrono::steady_clock::now();
    auto elapsedMs = [&] {
        return chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - t0).count();
    };
    vector<int> bestDirs;
    double bestScore = 0.0001;   // stayInside baseline
    int bestKind = 0;

    // Evaluate one candidate path. `boundCells` (>=0) is an upper bound on
    // captured cells used to skip the flood fill when it cannot win.
    auto consider = [&](vector<int>& dirs, int kind, long long bonus, int boundCells) {
        if (dirs.empty()) return;
        Eval ev = walkPath(dirs);
        if (!ev.valid || ev.R == 0 || ev.R > remaining - 1) return;
        // while hunted, keep the exposure window short instead of farming
        if (kind == 1 && anyChaser() && ev.R > cfg::chaserMaxT) return;
        if (kind == 1 && N > 2 && cfg::meleeMaxT > 0 && ev.R > cfg::meleeMaxT) return;
        // melee: with three opponents someone converts a possible cut, so
        // require the loop to close before any of them could arrive
        if (N > 2 && kind == 1 && cfg::meleeSlack > 0) {
            int x = st.px[ME], y = st.py[ME], arrival = 0;
            for (int d : dirs) {
                arrival++;
                x += DX[d]; y += DY[d];
                if (arrival > ev.R) break;
                if (st.owner[idx(x, y)] != ME)
                    for (int e = 0; e < N; e++) {
                        if (e == ME || !threat[e]) continue;
                        if (man(st.px[e], st.py[e], x, y) <= ev.R - cfg::meleeSlack) return;
                    }
            }
            for (int c : myTrailCells)
                for (int e = 0; e < N; e++) {
                    if (e == ME || !threat[e]) continue;
                    if (man(st.px[e], st.py[e], c % W, c / W) <= ev.R - cfg::meleeSlack) return;
                }
        }
        if (boundCells >= 0) {
            double boundScore = (double)(10.0 * boundCells + 12.0 * boundCells + bonus) / (ev.T + cfg::timePref);
            if (boundScore <= bestScore) return;
        }
        double risk = pathRisk(dirs, ev);
        vector<int> all = myTrailCells;
        int x = st.px[ME], y = st.py[ME], arrival = 0;
        for (int d : dirs) {
            arrival++; x += DX[d]; y += DY[d];
            int k = idx(x, y);
            if (st.owner[k] != ME) all.push_back(k);
            if (arrival >= ev.R) break;
        }
        gainFromCapture(all, ev.gain, ev.steal);
        long long value = (long long)ev.gain * 10 + (long long)ev.steal * cfg::stealW + bonus
                          - (long long)(risk * adaptiveRiskW());
        double score = (double)value / (ev.T + cfg::timePref);
        if (score > bestScore) { bestScore = score; bestDirs = dirs; bestKind = kind; }
    };

    static vector<int> dirs;
    if (inside) {
        // Rectangle loops in every legal opening direction, optionally
        // preceded by a free run through our own territory along `side`,
        // which slides the rectangle anchor toward open land. Candidates are
        // first enumerated with an O(1) upper bound on the capture, then
        // evaluated best-bound-first, so the time budget goes to the
        // promising big loops instead of whichever small ones enumerate first.
        struct Cand { int dir, side, k, a, b, bound; double bs; };
        static vector<Cand> cands;
        cands.clear();
        for (int dir = 0; dir < 4; dir++) {
            if (dir == (st.pdir[ME] ^ 1)) continue;
            for (int s = 0; s < 2; s++) {
                int side = (dir < 2) ? (s ? 3 : 2) : (s ? 1 : 0);
                for (int k = 0; k <= cfg::shiftK; k++) {
                    int ax = st.px[ME] + DX[side] * k, ay = st.py[ME] + DY[side] * k;
                    if (k > 0) {
                        if (side == (st.pdir[ME] ^ 1)) break;   // cannot open with a reverse
                        if (!inb(ax, ay) || st.owner[idx(ax, ay)] != ME) break;
                    }
                    // first non-ours cell along the ray = where a loop can exit
                    int aMin = 0;
                    {
                        int x = ax, y = ay;
                        while (true) {
                            x += DX[dir]; y += DY[dir];
                            if (!inb(x, y)) { aMin = -1; break; }
                            aMin++;
                            if (st.owner[idx(x, y)] != ME) break;
                        }
                    }
                    if (aMin < 0) continue;
                    for (int a = aMin; a <= aMin + cfg::maxDepth; a++) {
                        if (!inb(ax + DX[dir] * a, ay + DY[dir] * a)) break;
                        for (int b = 1; b < 64; b++) {
                            int fx = ax + DX[dir] * a, fy = ay + DY[dir] * a;
                            int cx = fx + DX[side] * b, cy = fy + DY[side] * b;
                            if (!inb(cx, cy)) break;
                            if (k + 2 * a + b > remaining - 1) break;
                            int bound = boxNotMine(min(ax, min(fx, cx)), min(ay, min(fy, cy)),
                                                   max(ax, max(fx, cx)), max(ay, max(fy, cy)));
                            double bs = (double)bound * (10 + cfg::stealW)
                                      / (k + 2 * a + b + cfg::timePref);
                            cands.push_back({dir, side, k, a, b, bound, bs});
                        }
                    }
                }
            }
        }
        sort(cands.begin(), cands.end(),
             [](const Cand& x, const Cand& y) { return x.bs > y.bs; });
        for (const Cand& c : cands) {
            if (c.bs <= bestScore) continue;   // cannot beat the leader even in theory
            buildRect(c.dir, c.a, c.side, c.b, dirs);
            if (c.k > 0) dirs.insert(dirs.begin(), c.k, c.side);
            consider(dirs, 1, 0, c.bound);
            if (elapsedMs() > cfg::planMs) break;
        }
        // dives at exposed opponents: reaching a trail cell before the victim
        // can get home kills it and cancels its whole loop
        if (elapsedMs() <= cfg::planMs) {
            for (int e = 0; e < N; e++) {
                if (e == ME || !threat[e] || st.ptrail[e] == 0) continue;
                vector<int> p1 = bfsPath(st.px[ME], st.py[ME], st.pdir[ME], true,
                    [e](int x, int y) { return st.trail[idx(x, y)] == e; });
                if (p1.empty() || (int)p1.size() + 1 >= enemyDistHome(e)) continue;
                int ex = st.px[ME], ey = st.py[ME];
                for (int d : p1) { ex += DX[d]; ey += DY[d]; }
                vector<int> p2 = bfsHome(ex, ey, p1.back(), true);
                if (p2.empty()) continue;
                vector<int> full = p1;
                full.insert(full.end(), p2.begin(), p2.end());
                long long bonus = min((long long)cfg::killCap,
                    cfg::killBase + (long long)st.ptrail[e] * cfg::killPer);
                consider(full, 3, bonus, -1);   // kind 3: dive, exempt from loop caps
            }
        }
        // counter-raids: while an enemy is extended on its own loop, route a
        // rectangle into its undefended home land and run home
        if (elapsedMs() <= cfg::planMs) {
            for (int e = 0; e < N; e++) {
                if (e == ME || !threat[e] || st.ptrail[e] < 6) continue;
                vector<int> p1 = bfsPath(st.px[ME], st.py[ME], st.pdir[ME], true,
                    [e](int x, int y) { return st.owner[idx(x, y)] == e; });
                if (p1.empty() || (int)p1.size() > 20) continue;
                int ex = st.px[ME], ey = st.py[ME];
                for (int d : p1) { ex += DX[d]; ey += DY[d]; }
                int d0 = p1.back();
                for (int s = 0; s < 2; s++) {
                    int side = (d0 < 2) ? (s ? 3 : 2) : (s ? 1 : 0);
                    for (int a : {3, 6}) {
                        for (int b : {6, 10}) {
                            int fx = ex + DX[d0] * a, fy = ey + DY[d0] * a;
                            int cx = fx + DX[side] * b, cy = fy + DY[side] * b;
                            if (!inb(fx, fy) || !inb(cx, cy)) continue;
                            static vector<int> raid;
                            raid = p1;
                            for (int i = 0; i < a; i++) raid.push_back(d0);
                            for (int i = 0; i < b; i++) raid.push_back(side);
                            for (int i = 0; i < a; i++) raid.push_back(d0 ^ 1);
                            for (int i = 0; i < b; i++) raid.push_back(side ^ 1);
                            if (raid.size() > 90) continue;
                            int rx = ex, ry = ey;
                            for (int d : raid) { rx += DX[d]; ry += DY[d]; }   // back at entry
                            (void)rx; (void)ry;
                            vector<int> p2 = bfsHome(ex, ey, side ^ 1, true);
                            if (p2.empty()) continue;
                            vector<int> full = raid;
                            full.insert(full.end(), p2.begin(), p2.end());
                            consider(full, 1, 0, -1);
                            if (elapsedMs() > cfg::planMs) break;
                        }
                        if (elapsedMs() > cfg::planMs) break;
                    }
                    if (elapsedMs() > cfg::planMs) break;
                }
            }
        }
    } else {
        vector<int> home = bfsHome(st.px[ME], st.py[ME], st.pdir[ME], true);
        if (!home.empty()) consider(home, 1, 0, -1);
        // shortened loops may still pay off while heading back
        for (int dir = 0; dir < 4; dir++) {
            if (dir == (st.pdir[ME] ^ 1)) continue;
            for (int s = 0; s < 2; s++) {
                int side = (dir < 2) ? (s ? 3 : 2) : (s ? 1 : 0);
                for (int a = 1; a <= cfg::maxDepth; a++) {
                    if (!inb(st.px[ME] + DX[dir] * a, st.py[ME] + DY[dir] * a)) break;
                    for (int b = 1; b < 64; b++) {
                        int fx = st.px[ME] + DX[dir] * a, fy = st.py[ME] + DY[dir] * a;
                        if (!inb(fx + DX[side] * b, fy + DY[side] * b)) break;
                        if (2 * a + b > remaining - 1) break;
                        buildRect(dir, a, side, b, dirs);
                        int bound = boxNotMine(min(st.px[ME], min(fx, fx + DX[side] * b)),
                                               min(st.py[ME], min(fy, fy + DY[side] * b)),
                                               max(st.px[ME], max(fx, fx + DX[side] * b)),
                                               max(st.py[ME], max(fy, fy + DY[side] * b)));
                        consider(dirs, 1, 0, bound);
                    }
                    if (elapsedMs() > cfg::planMs) break;
                }
                if (elapsedMs() > cfg::planMs) break;
            }
            if (elapsedMs() > cfg::planMs) break;
        }
        // exposed with nothing worth doing: minimize exposure instead
        if (bestDirs.empty() && !home.empty()) { plan = home; planKind = 2; return plan.front(); }
    }

    if (bestDirs.empty()) return stayInside();
    plan = bestDirs;
    planKind = bestKind;
    return plan.front();
}

// ---------------- I/O ----------------
static bool readState() {
    string tok;
    if (!(cin >> tok)) return false;
    cin >> st.turn;
    for (int i = 0; i < N; i++) {
        int id; char d;
        cin >> tok >> id >> st.px[id] >> st.py[id] >> d
            >> st.ptrail[id] >> st.parea[id] >> st.pdeaths[id];
        st.pdir[id] = dirIndex(d);
    }
    cin >> tok; // OWNER
    for (int k = 0; k < NC; k++) { char c; cin >> c; st.owner[k] = c == '.' ? -1 : c - '0'; }
    cin >> tok; // TRAIL
    for (int k = 0; k < NC; k++) { char c; cin >> c; st.trail[k] = c == '.' ? -1 : c - '0'; }
    cin >> tok; // END
    return true;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string tok;
    cin >> tok; // INIT
    cin >> W >> H >> MAXTURNS >> N >> ME >> MOVE_MS >> INIT_MS;
    NC = W * H;
    st.owner.assign(NC, -1);
    st.trail.assign(NC, -1);
    memset(protOwner, -1, sizeof(protOwner));
    for (int i = 0; i < N; i++) {
        int id, x, y;
        cin >> tok >> id >> x >> y;
        SX[id] = x; SY[id] = y;
    }
    for (int p = 0; p < N; p++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int x = SX[p] + dx, y = SY[p] + dy;
                if (inb(x, y)) protOwner[idx(x, y)] = (int8_t)p;
            }
    while (readState()) {
        updateEnemies();
        refreshDerived();
        updateChasers();
        buildPrefix();
        int d;
        try { d = decide(); } catch (...) { d = anySafe(); }
        if (d == (st.pdir[ME] ^ 1) || !inb(st.px[ME] + DX[d], st.py[ME] + DY[d])) d = anySafe();
        expX = st.px[ME] + DX[d]; expY = st.py[ME] + DY[d];
        if (!plan.empty()) {
            if (d == plan.front()) plan.erase(plan.begin());
            else { plan.clear(); planKind = 0; }
        }
        cout << st.turn << ' ' << DC[d] << endl;
    }
    return 0;
}
