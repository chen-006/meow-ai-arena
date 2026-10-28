// ============================================================================
// Hybrid
// ============================================================================
#include <bits/stdc++.h>
using namespace std;

#ifdef LBP_DEBUG
#define LBPT(stage) fprintf(stderr, "S%s\n", stage)
#else
#define LBPT(stage)
#endif

struct Unit { int id, team, x, y, hp, flag, respawn_at; };
struct Flag { int id; string status; int x, y, holder, return_at; };
struct Event { string type; vector<int> args; };

struct Game {
    int teams = 0, me = 0, size = 0, turns = 0, hp = 0, damage = 0, respawn = 0, flag_return = 0, flag_cooldown = 0;
    vector<string> map;
    vector<pair<int, int>> bases;
    vector<pair<int, int>> spots;
    int turn = 0;
    vector<int> score;
    vector<Unit> units;
    vector<Flag> flags;
    vector<Event> events;
};

bool read_turn(Game& g) {
    string w;
    while (cin >> w) {
        if (w == "INIT") {
            cin >> g.teams >> g.me >> g.size >> g.turns >> g.hp >> g.damage >> g.respawn >> g.flag_return >> g.flag_cooldown;
        } else if (w == "MAP") {
            g.map.assign(g.size, "");
            for (auto& row : g.map) cin >> row;
        } else if (w == "BASES") {
            int k; cin >> k; g.bases.resize(k);
            for (auto& b : g.bases) cin >> b.first >> b.second;
        } else if (w == "SPOTS") {
            int k; cin >> k; g.spots.resize(k);
            for (auto& s : g.spots) cin >> s.first >> s.second;
        } else if (w == "TURN") {
            cin >> g.turn;
        } else if (w == "SCORE") {
            g.score.resize(g.teams);
            for (auto& s : g.score) cin >> s;
        } else if (w == "UNITS") {
            int k; cin >> k; g.units.resize(k);
            for (auto& u : g.units) { string tag; cin >> tag >> u.id >> u.team >> u.x >> u.y >> u.hp >> u.flag >> u.respawn_at; }
        } else if (w == "FLAGS") {
            int k; cin >> k; g.flags.resize(k);
            for (auto& f : g.flags) { string tag; cin >> tag >> f.id >> f.status >> f.x >> f.y >> f.holder >> f.return_at; }
        } else if (w == "EVENTS") {
            int k; cin >> k; g.events.assign(k, {});
            string line; getline(cin, line);
            for (auto& e : g.events) {
                getline(cin, line);
                istringstream in(line);
                string tag; in >> tag >> e.type;
                for (int v; in >> v;) e.args.push_back(v);
            }
        } else if (w == "END") {
            return true;
        }
    }
    return false;
}

namespace bot_final {

#if defined(LBP_NN) && __has_include("lbp_nn.h")
#include "lbp_nn.h"
#define LBP_HAS_NN 1
#else
#define LBP_HAS_NN 0
#endif

struct Params {

    double W_HIT_CARRIER = 2.1, W_HIT_PLAIN = 1.1;
    double LIFE_LOWHP_BONUS = 8.0;
    double TW_CARRIER = 3.0, TW_LOWHP = 1.6, TW_PLAIN = 1.0;

    int K10_CARRIER_CHASED = 3, K10_CARRIER_CALM_DUEL = 19, K10_CARRIER_MELEE = 3, K10_OTHER = 12, K10_LOWHP = 15;

    double LAMBDA_DUEL = 2.57, LAMBDA_MID = 0.97, LAMBDA_MELEE = 0.56;
    double DUEL_CENTER_BIAS = 49.0;
    double AGGR_MELEE = 0.68;
    double AGGR_DUEL = 1.0;
    int    MELEE_GUARD_R = 13;
    double MELEE_DOOR_BIAS = 8.1, MELEE_ZONE_PEN = 6.5;
    double ESCORT_BASE_DUEL = 21.6, ESCORT_PER_THREAT = 22.2;

    double GATE_DEATH_CARRIER = 0.30;
    double GATE_DEATH_PLAIN = 0.99;
    double GATE_DEATH_LOWHP = 0.99;
    int    GATE_SOLO_MID_ENEMY_D = 6;
    int    GATE_SOLO_MID_ALLY_D = 4;
    double GATE_MELEE_CROWD = 2.0;

    double HUNT_BASE_MULTI = 3.7;
    double INTERCEPT_DENY_MULTI = 0.42;
    double NN_WEIGHT_SMALL = 0.0;
    double NN_WEIGHT_MULTI = 1.0;

    double DMG_WEIGHT = 0.60;
    double LIFE_CARRIER = 21.2, LIFE_PLAIN = 5.4, LIFE_NEARHOME = 6.0, LIFE_CARRIER_NEAR = 10.0;
};
static Params P;

static int N = 0, NN = 0;
static vector<char> W;
static vector<int16_t> Dall;
static bool pre = false;
static const int INF = 9999;
static const int DX[5] = {0, 0, 0, -1, 1}, DY[5] = {0, -1, 1, 0, 0};
static const char MVC[5] = {'S', 'U', 'D', 'L', 'R'};

inline int cid(int x, int y) { return y * N + x; }
inline int cx(int c) { return c % N; }
inline int cy(int c) { return c / N; }
inline int D(int a, int b) { if (a < 0 || b < 0) return INF; return Dall[(size_t)a * NN + b]; }
inline int manh(int a, int b) { return abs(cx(a) - cx(b)) + abs(cy(a) - cy(b)); }

void precompute(const Game& g) {
    N = g.size; NN = N * N; W.assign(NN, 0);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) W[cid(x, y)] = (g.map[y][x] == '#');
    Dall.assign((size_t)NN * NN, INF);
    vector<int> q(NN);
    for (int s = 0; s < NN; s++) {
        if (W[s]) continue;
        int16_t* d = &Dall[(size_t)s * NN];
        int h = 0, t = 0; q[t++] = s; d[s] = 0;
        while (h < t) {
            int c = q[h++]; int x = cx(c), y = cy(c);
            for (int k = 1; k < 5; k++) {
                int nx = x + DX[k], ny = y + DY[k];
                if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
                int nc = cid(nx, ny);
                if (W[nc] || d[nc] != INF) continue;
                d[nc] = d[c] + 1; q[t++] = nc;
            }
        }
    }
    pre = true;
}

bool canAtk(int a, int b) {
    if (a < 0 || b < 0) return false;
    int ax = cx(a), ay = cy(a), bx = cx(b), by = cy(b);
    int dx = bx - ax, dy = by - ay;
    int md = abs(dx) + abs(dy);
    if (md > 2) return false;
    if (abs(dx) == 2 && dy == 0) return !W[cid(ax + dx / 2, ay)];
    if (abs(dy) == 2 && dx == 0) return !W[cid(ax, ay + dy / 2)];
    if (abs(dx) == 1 && abs(dy) == 1) return !W[cid(ax + dx, ay)] || !W[cid(ax, ay + dy)];
    return true;
}

struct EInfo {
    int id, team, cell, hp, flag; bool immune;
    int pn;
    int pc[5]; double pr[5];
    double pStay;
};
struct MU { int id, cell, hp, flag; bool alive, spawning, canAtk; };
struct Task { int type; int key; int target; double val; int eta; int hits; };

static vector<int> occG;
static vector<int> thrG;

struct Ctx {
    const Game* g;
    int me, T, nTeams, remaining, centerCell;
    vector<vector<int>> bcells;
    vector<MU> M;
    vector<EInfo> E;
    vector<int> flagCell, flagAvail;
    int myCarrier = -1, myCarrierIdx = -1;
    int myDoorSpot = -1, eDoorSpotMin = INF;
    int phase = 0;
    int holdUntil = 0;
    int attackTarget = -1, targetFlag = -1;
    double lambda = 2.0, aggr = 1.0;
    vector<int> spawnOrder;

    double tw[3]; int needMy[3];
    double otherW[45][5];

    static const int MAXE = 45;
    unsigned char hitAfter[3][5][MAXE][5];
};

void buildSpawn(Ctx& X) {
    X.spawnOrder = X.bcells[X.me];
    int bx, by; tie(bx, by) = X.g->bases[X.me];
    sort(X.spawnOrder.begin(), X.spawnOrder.end(), [&](int a, int b) {
        int da = manh(a, X.centerCell), db = manh(b, X.centerCell);
        if (da != db) return da < db;
        int ka = abs(cx(a) - bx) + 2 * abs(cy(a) - by), kb = abs(cx(b) - bx) + 2 * abs(cy(b) - by);
        return ka < kb;
    });
}

vector<int> ctgTo(const Ctx& X, const vector<int>& targets, int K10) {
    vector<int> cost(NN, INT_MAX);
    priority_queue<pair<int, int>, vector<pair<int, int>>, greater<>> pq;
    for (int t : targets) if (t >= 0 && !W[t]) { cost[t] = 0; pq.push({0, t}); }
    while (!pq.empty()) {
        auto [d, c] = pq.top(); pq.pop();
        if (d > cost[c]) continue;
        int x = cx(c), y = cy(c);
        for (int k = 1; k < 5; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
            int nc = cid(nx, ny);
            if (W[nc]) continue;
            int step = 10 + K10 * thrG[nc];
            if (occG[nc] >= 0 && occG[nc] / 3 != X.me) step += 25;
            int nd = d + step;
            if (nd < cost[nc]) { cost[nc] = nd; pq.push({nd, nc}); }
        }
    }
    return cost;
}

int distToBase(const Ctx& X, int c, int team) {
    int best = INF; for (int b : X.bcells[team]) best = min(best, D(c, b)); return best;
}
int nearestBaseCell(const Ctx& X, int c, int team) {
    int best = INF, bc = X.bcells[team][4];
    for (int b : X.bcells[team]) if (D(c, b) < best) { best = D(c, b); bc = b; }
    return bc;
}

void predictEnemy(Ctx& X, EInfo& e) {
    e.pn = 0; e.pStay = 1.0;
    if (e.immune) { e.pc[0] = e.cell; e.pr[0] = 1.0; e.pn = 1; return; }
    int x = cx(e.cell), y = cy(e.cell);
    int goal = -1, curG = INF, myNear = -1, curM = INF;
    if (e.flag >= 0) {
        goal = nearestBaseCell(X, e.cell, e.team); curG = D(e.cell, goal);
    } else {
        for (size_t i = 0; i < X.g->flags.size(); i++) if (X.flagAvail[i]) { int d = D(e.cell, X.flagCell[i]); if (d < curG) { curG = d; goal = X.flagCell[i]; } }
        for (auto& m : X.M) if (m.alive) { int d = D(e.cell, m.cell); if (d < curM) { curM = d; myNear = m.cell; } }
    }
    double w[5]; int cells[5]; int cnt = 0; double tot = 0;
    for (int k = 0; k < 5; k++) {
        int nx = x + DX[k], ny = y + DY[k];
        if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
        int nc = cid(nx, ny);
        if (W[nc]) continue;
        double ww;
        if (e.flag >= 0) ww = (k == 0) ? 0.5 : ((D(nc, goal) < curG) ? 5.0 : 0.4);
        else {
            ww = (k == 0) ? 1.2 : 1.0;
            if (goal >= 0 && k != 0 && D(nc, goal) < curG) ww += 2.0;
            if (myNear >= 0 && curM <= 4 && k != 0 && D(nc, myNear) < curM) ww += 1.5;
            if (k != 0 && occG[nc] >= 0) ww *= 0.3;
        }
        w[cnt] = ww; cells[cnt] = nc; tot += ww; cnt++;
    }
    for (int i = 0; i < cnt; i++) { e.pc[i] = cells[i]; e.pr[i] = w[i] / tot; if (cells[i] == e.cell) e.pStay = e.pr[i]; }
    e.pn = cnt;
}

inline void afterCells(const Ctx& X, int k, int mv, int j, int h, int& myC, int& eC) {
    const MU& m = X.M[k]; const EInfo& e = X.E[j];
    int myWant = (mv == 0) ? m.cell : -1;
    { int nx = cx(m.cell) + DX[mv], ny = cy(m.cell) + DY[mv];
      if (nx >= 0 && ny >= 0 && nx < N && ny < N) { int nc = cid(nx, ny); if (!W[nc]) myWant = nc; } }
    if (myWant < 0) myWant = m.cell;
    int eWant = e.pc[h];
    if (myWant == eWant) { myC = m.cell; eC = e.cell; return; }
    if (myWant == e.cell) {
        if (eWant != e.cell) { myC = myWant; eC = eWant; return; }
        myC = m.cell; eC = e.cell; return;
    }
    if (eWant == m.cell && mv != 0) { myC = myWant; eC = eWant; return; }
    myC = myWant; eC = eWant;
}

struct ComboLog { double feat[64]; int chosen; int nCombo; };

string decide(const Game& g) {
    if (!pre) precompute(g);
    LBPT("0-start");
    auto t0 = chrono::steady_clock::now();
    Ctx X; X.g = &g; X.me = g.me; X.T = g.turn; X.nTeams = g.teams;
    X.remaining = g.turns - X.T + 1;
    X.centerCell = cid(N / 2, N / 2);
    X.lambda = (X.nTeams <= 5) ? P.LAMBDA_MID : P.LAMBDA_MELEE;
    X.aggr = P.AGGR_MELEE;

    for (int t = 0; t < X.nTeams; t++) {
        vector<int> v; auto [bx, by] = g.bases[t];
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) v.push_back(cid(bx + dx, by + dy));
        X.bcells.push_back(v);
    }
    LBPT("0a-bcells");
    occG.assign(NN, -1);
    for (auto& u : g.units) if (u.x >= 0) occG[cid(u.x, u.y)] = u.id;
    buildSpawn(X);

    LBPT("0b-spawn");

    X.M.assign(3, {});
    for (int k = 0; k < 3; k++) {
        int uid = 3 * X.me + k; const Unit* u = nullptr;
        for (auto& x : g.units) if (x.id == uid) u = &x;
        MU m{}; m.id = uid; m.cell = -1; m.hp = 0; m.flag = -1; m.alive = false; m.spawning = false; m.canAtk = false;
        if (u) {
            if (u->x >= 0) { m.cell = cid(u->x, u->y); m.hp = u->hp; m.flag = u->flag; m.alive = true; m.canAtk = (u->flag < 0); }
            else if (u->respawn_at == X.T) {
                for (int c : X.spawnOrder) if (occG[c] < 0) { m.cell = c; occG[c] = uid; break; }
                if (m.cell >= 0) { m.alive = true; m.spawning = true; m.hp = g.hp; m.canAtk = false; }
            }
        }
        X.M[k] = m;
    }
    for (int k = 0; k < 3; k++) if (X.M[k].alive && X.M[k].flag >= 0) { X.myCarrierIdx = k; break; }

    LBPT("0c-mine");

    for (auto& u : g.units) {
        if (u.team == X.me) continue;
        EInfo e{}; e.id = u.id; e.team = u.team; e.hp = u.hp; e.flag = u.flag; e.immune = false;
        if (u.x >= 0) e.cell = cid(u.x, u.y);
        else if (u.respawn_at == X.T) { e.cell = X.bcells[u.team][4]; e.immune = true; e.hp = g.hp; }
        else continue;
        X.E.push_back(e);
    }

    LBPT("0d-enemy");

    X.flagCell.assign(g.flags.size(), -1);
    X.flagAvail.assign(g.flags.size(), 0);
    for (size_t i = 0; i < g.flags.size(); i++) {
        auto& f = g.flags[i];
        if ((f.status == "home" || f.status == "dropped") && f.x >= 0) { X.flagCell[i] = cid(f.x, f.y); X.flagAvail[i] = 1; }
        if ((int)i == X.me) X.myDoorSpot = cid(g.spots[i].first, g.spots[i].second);
    }

    for (auto& e : X.E) predictEnemy(X, e);
    LBPT("1-pred");

    LBPT("0e-pred");

    thrG.assign(NN, 0);
    for (auto& e : X.E) {
        if (e.immune || e.flag >= 0) continue;
        int ex = cx(e.cell), ey = cy(e.cell);
        for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++) {
            if (abs(dx) + abs(dy) > 3) continue;
            int x = ex + dx, y = ey + dy;
            if (x < 0 || y < 0 || x >= N || y >= N) continue;
            thrG[cid(x, y)]++;
        }
    }

    for (int k = 0; k < 3; k++) {
        const MU& m = X.M[k];
        int lives = m.hp <= 0 ? 0 : (m.hp + g.damage - 1) / g.damage;
        X.needMy[k] = lives;
        double twv = P.TW_PLAIN;
        if (m.flag >= 0) twv = P.TW_CARRIER;
        else if (lives <= 1) twv = P.TW_LOWHP;
        X.tw[k] = twv;
    }

    for (size_t j = 0; j < X.E.size() && j < 45; j++) {
        const EInfo& e = X.E[j];
        for (int h = 0; h < e.pn; h++) {
            double others = 0;
            for (auto& u : g.units) {
                if (u.x < 0 || u.team == e.team) continue;
                if (u.id / 3 == X.me) continue;
                int uc = cid(u.x, u.y);
                if (!canAtk(e.pc[h], uc)) continue;
                others += (u.flag >= 0) ? P.TW_CARRIER : ((u.hp + g.damage - 1) / g.damage <= 1 ? P.TW_LOWHP : P.TW_PLAIN);
            }
            X.otherW[j][h] = others;
        }
        for (int h = e.pn; h < 5; h++) X.otherW[j][h] = 0;
    }

    LBPT("0f-threat");

    vector<Task> tasks[3];
    int nAliveMe = 0; for (auto& m : X.M) if (m.alive) nAliveMe++;
    int nAliveE = 0, livesE = 0, livesMe = 0;
    for (auto& e : X.E) if (!e.immune) { nAliveE++; livesE += (e.hp + g.damage - 1) / g.damage; }
    for (auto& m : X.M) if (m.alive) livesMe += (m.hp + g.damage - 1) / g.damage;
    int enemyCarrierJ = -1;
    for (size_t j = 0; j < X.E.size(); j++) if (X.E[j].flag >= 0) enemyCarrierJ = (int)j;

    auto minETA = [&](int cell, bool mine) {
        int best = INF;
        if (mine) { for (auto& m : X.M) if (m.alive && m.flag < 0) best = min(best, D(m.cell, cell)); }
        else for (auto& e : X.E) if (!e.immune && e.flag < 0) best = min(best, D(e.cell, cell));
        return best;
    };

    {

        if (X.myCarrierIdx >= 0) {

            tasks[X.myCarrierIdx].push_back({0, 0, -1, 1000, 0, 0});
        }
        for (int k = 0; k < 3; k++) {
            auto& m = X.M[k];
            if (!m.alive) { if (tasks[k].empty()) tasks[k].push_back({-1, 0, -1, 0, 0, 0}); continue; }
            if (m.flag >= 0) { tasks[k].clear(); tasks[k].push_back({0, 0, -1, 1000, 0, 0}); continue; }

            if (X.myCarrierIdx >= 0) {
                int ci = X.myCarrierIdx;
                int threatCell = -1, threatD = INF;
                for (auto& e : X.E) if (!e.immune && e.flag < 0) { int d = manh(e.cell, X.M[ci].cell); if (d < threatD) { threatD = d; threatCell = e.cell; } }
                double ev = 30.0 + 14.0 * (threatCell >= 0 && threatD <= 8 ? 1 : 0) - 1.5 * D(m.cell, X.M[ci].cell);
                tasks[k].push_back({3, ci, threatCell >= 0 ? threatCell : X.M[ci].cell, ev, D(m.cell, X.M[ci].cell), 0});
            }

            for (size_t i = 0; i < g.flags.size(); i++) {
                if (!X.flagAvail[i]) continue;
                int fc = X.flagCell[i];
                int eta = D(m.cell, fc); if (eta >= INF) continue;
                if (g.flags[i].status == "dropped" && g.flags[i].return_at >= 0 && eta > g.flags[i].return_at - X.T) continue;
                int ret = distToBase(X, fc, X.me); if (ret >= INF) continue;
                if (eta + ret > X.remaining) continue;
                int eEta = INF; int nearEnemies = 0;
                for (auto& e : X.E) {
                    if (e.flag >= 0) continue;
                    int d = D(e.cell, fc) + (e.immune ? 1 : 0);
                    eEta = min(eEta, d);
                    if (!e.immune && d <= 5) nearEnemies++;
                }
                double pget;
                if (eta + 2 < eEta) pget = 0.97; else if (eta < eEta) pget = 0.85;
                else if (eta == eEta) pget = 0.45; else if (eta == eEta + 1) pget = 0.25; else pget = 0.1;
                pget = max(pget, eta < eEta ? 0.9 : eta == eEta ? 0.55 : eta <= eEta + 2 ? 0.35 : 0.2);
                int corridor = 0;
                for (auto& e : X.E) if (!e.immune && e.flag < 0 && D(e.cell, fc) + distToBase(X, e.cell, X.me) <= ret + 4) corridor++;
                double pret = 0.95 - 0.12 * min(3, nearEnemies) - 0.08 * min(4, corridor);
                double homeCloseness = max(0.0, (double)P.MELEE_GUARD_R - distToBase(X, fc, X.me)) / P.MELEE_GUARD_R;
                double zoneBoost = 1.0 + P.MELEE_DOOR_BIAS / 10.0 * homeCloseness;
                double val = 100.0 * pget * pret * zoneBoost - X.lambda * (eta + ret);
                bool near = distToBase(X, fc, X.me) <= P.MELEE_GUARD_R;
                if (!near) val *= (0.4 + X.aggr);
                val += 6.0 * ((m.hp + g.damage - 1) / g.damage - 1);
                tasks[k].push_back({1, (int)i, fc, val, eta, 0});
            }

            for (auto& e : X.E) {
                if (e.flag < 0 || e.immune) continue;
                int tb = nearestBaseCell(X, e.cell, e.team);
                int tfirst = INF, tgt = -1;
                int c = e.cell;
                for (int step = 0; step <= 14 && c != tb; step++) {
                    if (D(m.cell, c) <= step + 1) { tfirst = step; tgt = c; break; }
                    int nxt = -1;
                    for (int q = 1; q < 5; q++) {
                        int nx = cx(c) + DX[q], ny = cy(c) + DY[q];
                        if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
                        int nc = cid(nx, ny);
                        if (W[nc]) continue;
                        if (D(nc, tb) == D(c, tb) - 1) { nxt = nc; break; }
                    }
                    if (nxt < 0) break; c = nxt;
                }
                if (tgt < 0) continue;
                if (X.nTeams >= 6 && tfirst > 6) continue;
                if (X.nTeams < 6 && tfirst > 12) continue;
                int hits = 14 - tfirst + 1;
                double deny = P.INTERCEPT_DENY_MULTI;
                double val = 100.0 * (deny + 0.45) * 0.85 - X.lambda * tfirst;
                tasks[k].push_back({2, e.id, tgt, val, tfirst, hits});
            }

            for (auto& e : X.E) {
                if (e.immune || e.flag >= 0) continue;
                int d = D(m.cell, e.cell);
                if (d > 7) continue;
                int support = 0, mysup = 0;
                for (auto& e2 : X.E) if (e2.id != e.id && !e2.immune && e2.flag < 0 && D(e2.cell, e.cell) <= 3) support++;
                for (int j = 0; j < 3; j++) if (j != k && X.M[j].alive && X.M[j].canAtk && D(X.M[j].cell, e.cell) <= 4) mysup++;
                double val = P.HUNT_BASE_MULTI - X.lambda * d + (e.hp <= 68 ? 12 : 0) + (e.hp <= 34 ? 12 : 0) - 18 * support + 12 * mysup;
                if (d > 4) continue;
                if (X.nTeams >= 6 && !(d <= 2 && (e.hp + g.damage - 1) / g.damage <= 2)) continue;
                if ((m.hp + g.damage - 1) / g.damage == 1 && (e.hp + g.damage - 1) / g.damage >= 2 && mysup == 0) continue;
                if (val <= 0) continue;
                tasks[k].push_back({4, e.id, e.cell, val, d, 0});
            }

            {
                int myBase = X.bcells[X.me][4];
                for (size_t si = 0; si < g.spots.size(); si++) {
                    int sc = cid(g.spots[si].first, g.spots[si].second);
                    bool taken = false;
                    for (size_t f = 0; f < g.flags.size(); f++) if (X.flagAvail[f] && X.flagCell[f] == sc) taken = true;
                    if (taken) continue;
                    int d = D(m.cell, sc); if (d >= INF) continue;
                    int dHome = D(sc, myBase);

                    int dOther = INF;
                    for (int t = 0; t < X.nTeams; t++) if (t != X.me) dOther = min(dOther, D(sc, X.bcells[t][4]));
                    double v = 10.0 - 0.5 * dHome - 1.5 * thrG[sc];
                    if ((int)si == X.me) v += P.MELEE_DOOR_BIAS;
                    if (sc == X.centerCell && X.nTeams <= 5) v += 3.0;
                    if (dOther + 3 < dHome) v -= P.MELEE_ZONE_PEN;
                    for (auto& f : g.flags) if (f.status == "cooldown" && f.return_at >= 0) {
                        int wait = max(d, f.return_at - X.T);
                        v += max(0.0, 0.25 * (30.0 - X.lambda * (wait + D(sc, myBase))));
                    }
                    if (v <= 0) continue;
                    tasks[k].push_back({5, (int)sc, sc, v, d, 0});
                }
                if (tasks[k].empty()) tasks[k].push_back({5, myBase, myBase, 1.0, 0, 0});
            }
        }
    }

    LBPT("2-tasks");
    auto evalCombo = [&](const array<int, 3>& idx) {
        double total = 0;
        vector<pair<int, int>> gets;
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 1) gets.push_back({t.eta, k}); }
        sort(gets.begin(), gets.end());
        map<int, int> cnt;
        for (auto& [eta, k] : gets) {
            auto& t = tasks[k][idx[k]];
            int c = cnt[t.key]++;
            double f = 1.0;
            if (c == 1) f = 0.5;
            if (c >= 2) f = 0.35;
            total += t.val * f;
        }
        map<int, int> inter;
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 2) { int c = inter[t.key]++; if (c == 0) total += t.val; else total += t.val * 0.4; } }
        int esc = 0;
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 3) { total += t.val * (esc == 0 ? 1.0 : 0.5); esc++; } }
        map<int, int> hcnt;
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 4) { int c = hcnt[t.key]++; total += t.val * (c == 0 ? 1.0 : c == 1 ? 0.8 : 0.5); } }
        map<int, int> rcnt;
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 5) { int c = rcnt[t.key]++; total += t.val * (c == 0 ? 1.0 : 0.15); } }
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 0) total += t.val; }
        return total;
    };
    array<int, 3> best{0, 0, 0}; double bestV = -1e18;
    for (int a = 0; a < (int)tasks[0].size(); a++)
        for (int b = 0; b < (int)tasks[1].size(); b++)
            for (int c = 0; c < (int)tasks[2].size(); c++) {
                array<int, 3> idx = {a, b, c};
                double v = evalCombo(idx);
                if (v > bestV) { bestV = v; best = idx; }
            }

    LBPT("3-combo");

    int nE = (int)X.E.size();
    for (int k = 0; k < 3; k++) for (int mv = 0; mv < 5; mv++) for (int j = 0; j < nE; j++) for (int h = 0; h < X.E[j].pn; h++) {
        int myC, eC; afterCells(X, k, mv, j, h, myC, eC);
        X.hitAfter[k][mv][j][h] = canAtk(myC, eC) ? 1 : 0;
    }
    auto enemyCanHitMe = [&](int j, int h, int k, int mv) { return X.hitAfter[k][mv][j][h] != 0; };

    auto incomingCost = [&](const array<int, 3>& mv, double* deathOut) -> double {

        double dp[3][4] = {};
        for (int k = 0; k < 3; k++) dp[k][0] = 1;
        for (int j = 0; j < nE; j++) {
            const EInfo& e = X.E[j];
            if (e.immune || e.flag >= 0) continue;
            double prob[3] = {0, 0, 0};
            for (int h = 0; h < e.pn; h++) {
                double w[3] = {0, 0, 0}, sum = 0;
                for (int k = 0; k < 3; k++) {
                    if (!X.M[k].alive || X.M[k].spawning) continue;
                    if (enemyCanHitMe(j, h, k, mv[k])) {
                        w[k] = X.tw[k];
                        sum += w[k];
                    }
                }

                double others = X.otherW[j][h];
                if (sum <= 0) continue;
                for (int k = 0; k < 3; k++) prob[k] += e.pr[h] * w[k] / (sum + others);
            }
            for (int k = 0; k < 3; k++) {
                double z[4] = {};
                for (int a = 0; a < 4; a++) { z[a] += dp[k][a] * (1 - prob[k]); if (a < 3) z[a + 1] += dp[k][a] * prob[k]; }
                for (int a = 0; a < 4; a++) dp[k][a] = z[a];
            }
        }
        double cost = 0;
        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive) { if (deathOut) deathOut[k] = 0; continue; }
            const MU& m = X.M[k];
            int lives = X.needMy[k];
            double death = 0, dmg = 0;
            for (int a = 0; a < 4; a++) { if (a >= lives) death += dp[k][a]; dmg += dp[k][a] * min(a, lives); }
            if (deathOut) deathOut[k] = death;
            int baseD = distToBase(X, m.cell, X.me);
            double life = (m.flag >= 0) ? P.LIFE_CARRIER + max(0, 12 - baseD) * 0.5 : (baseD < 4 ? P.LIFE_NEARHOME : P.LIFE_PLAIN);
            if ((m.flag >= 0) && baseD < 4) life = P.LIFE_CARRIER_NEAR + max(0, 12 - baseD) * 0.5;
            if (lives <= 1) life += P.LIFE_LOWHP_BONUS;
            cost += P.DMG_WEIGHT * (dmg * (m.flag >= 0 ? P.W_HIT_CARRIER : P.W_HIT_PLAIN) + life * death);
        }
        return cost;
    };

    auto gateBlocked = [&](int k, int cell, int myCellAfter[3]) {
        const MU& m = X.M[k];
        if (!m.alive || m.spawning) return false;

        if (X.nTeams >= 3) {
            int eNear = 0, aNear = 0;
            for (auto& e : X.E) if (!e.immune && e.flag < 0 && manh(cell, e.cell) <= 3) eNear++;
            for (int j = 0; j < 3; j++) if (j != k && X.M[j].alive && manh(cell, X.M[j].cell) <= 3) aNear++;
            if (eNear - aNear >= (int)P.GATE_MELEE_CROWD && (m.flag >= 0 ? eNear >= 1 : aNear == 0)) return true;
        }
        return false;
    };

    vector<vector<int>> ctg(3);
    for (int k = 0; k < 3; k++) {
        if (!X.M[k].alive) { ctg[k].clear(); continue; }
        auto& t = tasks[k][best[k]];
        vector<int> targets;
        if (t.type == 0) targets = X.bcells[X.me];
        else if (t.type == 3) {

            int ci = t.key;
            int threatCell = -1, threatD = INF;
            for (auto& e : X.E) if (!e.immune && e.flag < 0) { int d = manh(e.cell, X.M[ci].cell); if (d < threatD) { threatD = d; threatCell = e.cell; } }
            if (threatCell >= 0 && threatD <= 8) targets = {threatCell};
            else targets = {X.M[ci].cell};
        }
        else if (t.type >= 0) targets = {t.target};
        else targets = {X.M[k].cell};
        bool chased = false;
        for (auto& e : X.E) if (!e.immune && e.flag < 0 && manh(e.cell, X.M[k].cell) <= 2) chased = true;
        int K10;
        if (X.M[k].flag >= 0) K10 = chased ? P.K10_CARRIER_CHASED : P.K10_CARRIER_MELEE;
        else K10 = P.K10_OTHER;
        if (X.M[k].hp <= g.damage && !chased && X.M[k].flag < 0) K10 += P.K10_LOWHP;
        ctg[k] = ctgTo(X, targets, K10);
    }

    double bestScore = -1e18; array<int, 3> bestMv{0, 0, 0};
    int gatedCount = 0;
    double comboScore[125];
    double comboFeat[125][40];
    for (int c = 0; c < 125; c++) for (int q = 0; q < 40; q++) comboFeat[c][q] = 0;
    array<int, 3> comboMv;
    int ci = 0;

    double gf[16];
    {
        int myCarry = 0, eCarry = 0;
        for (auto& m : X.M) if (m.alive && m.flag >= 0) myCarry++;
        for (auto& e : X.E) if (e.flag >= 0) eCarry++;
        int nFlagsAvail = 0; bool centerAvail = false, doorAvail = false;
        for (size_t i = 0; i < g.flags.size(); i++) if (X.flagAvail[i]) {
            nFlagsAvail++;
            if (X.flagCell[i] == X.centerCell) centerAvail = true;
            if (X.flagCell[i] == X.myDoorSpot) doorAvail = true;
        }
        int beyond = 0, alive = 0;
        int otherBase = X.bcells[(X.me + 1) % X.nTeams][4];
        for (auto& m : X.M) if (m.alive) {
            alive++;
            if (D(m.cell, X.bcells[X.me][4]) * 2 > D(X.bcells[X.me][4], otherBase)) beyond++;
        }
        int bestOther = 0; for (int t = 0; t < X.nTeams; t++) if (t != X.me) bestOther = max(bestOther, g.score[t]);
        gf[0] = 1.0;
        gf[1] = X.nTeams / 15.0;
        gf[2] = X.T / 400.0;
        gf[3] = min(40, g.score[X.me]) / 40.0;
        gf[4] = min(40, bestOther) / 40.0;
        gf[5] = max(-40, min(40, g.score[X.me] - bestOther)) / 40.0;
        gf[6] = alive / 3.0;
        gf[7] = (double)X.E.size() / 9.0;
        gf[8] = myCarry / 3.0;
        gf[9] = eCarry / 9.0;
        gf[10] = livesMe / 9.0;
        gf[11] = livesE / 9.0;
        gf[12] = nFlagsAvail / 8.0;
        gf[13] = centerAvail ? 1.0 : 0.0;
        gf[14] = doorAvail ? 1.0 : 0.0;
        gf[15] = alive ? beyond / 3.0 : 0.0;
    }

    int goalCell[3], goalD[3];
    for (int k = 0; k < 3; k++) {
        goalCell[k] = -1; goalD[k] = INF;
        if (!X.M[k].alive) continue;
        if (X.M[k].flag >= 0) {
            int best = INF, bc = -1;
            for (int b : X.bcells[X.me]) if (D(X.M[k].cell, b) < best) { best = D(X.M[k].cell, b); bc = b; }
            goalCell[k] = bc; goalD[k] = best;
        } else {
            int best = INF, fc = -1;
            for (size_t i = 0; i < g.flags.size(); i++) if (X.flagAvail[i]) {
                int d = D(X.M[k].cell, X.flagCell[i]);
                if (d < best) { best = d; fc = X.flagCell[i]; }
            }
            goalCell[k] = fc; goalD[k] = best;
        }
    }
    for (int m0 = 0; m0 < 5; m0++) for (int m1 = 0; m1 < 5; m1++) for (int m2 = 0; m2 < 5; m2++, ci++) {
        array<int, 3> mv = {m0, m1, m2};
        comboMv = mv;
        double s = 0;
        int cellAfter[3] = {-1, -1, -1};

        int want[3];
        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive) { want[k] = -1; continue; }
            int nx = cx(X.M[k].cell) + DX[mv[k]], ny = cy(X.M[k].cell) + DY[mv[k]];
            if (nx < 0 || ny < 0 || nx >= N || ny >= N) { want[k] = X.M[k].cell; continue; }
            int nc = cid(nx, ny);
            want[k] = W[nc] ? X.M[k].cell : nc;
        }
        int eff[3];
        for (int k = 0; k < 3; k++) eff[k] = (want[k] < 0) ? -1 : want[k];
        for (int it = 0; it < 4; it++) {
            bool changed = false;
            for (int k = 0; k < 3; k++) {
                if (eff[k] < 0 || eff[k] == X.M[k].cell) continue;

                for (int l = 0; l < 3; l++) if (l != k && eff[l] == eff[k]) { eff[k] = X.M[k].cell; changed = true; break; }
                if (eff[k] == X.M[k].cell) continue;

                for (int l = 0; l < 3; l++) if (l != k && X.M[l].alive && X.M[l].cell == eff[k] && eff[l] == X.M[l].cell) { eff[k] = X.M[k].cell; changed = true; break; }
                if (eff[k] == X.M[k].cell) continue;

                for (auto& e : X.E) if (!e.immune && e.cell == eff[k] && e.pStay >= 0.5) { eff[k] = X.M[k].cell; changed = true; break; }
            }
            if (!changed) break;
        }
        for (int k = 0; k < 3; k++) cellAfter[k] = eff[k];

        double deathP[3] = {0, 0, 0};
        double inCost = incomingCost(mv, deathP);

        bool gated = false;
        for (int k = 0; k < 3; k++) {
            if (cellAfter[k] < 0) continue;
            const MU& m = X.M[k];
            double thr = (m.flag >= 0) ? P.GATE_DEATH_CARRIER
                       : ((m.hp + g.damage - 1) / g.damage <= 1 ? P.GATE_DEATH_LOWHP : P.GATE_DEATH_PLAIN);
            if (deathP[k] > thr) gated = true;
            if (cellAfter[k] >= 0 && gateBlocked(k, cellAfter[k], cellAfter)) gated = true;
        }
        if (gated) { s -= 1e6; gatedCount++; }

        for (int k = 0; k < 3; k++) {
            if (cellAfter[k] < 0) continue;
            if (ctg[k].empty()) continue;
            double roleW = (X.M[k].flag >= 0) ? 1.3 : 1.0;
            if (ctg[k][cellAfter[k]] >= INT_MAX) s -= 60;
            else s -= roleW * ctg[k][cellAfter[k]] / 10.0;
        }

        s -= inCost;

        for (int k = 0; k < 3; k++) {
            if (cellAfter[k] < 0 || !X.M[k].canAtk || X.M[k].spawning) continue;
            double atk = 0;
            for (int j = 0; j < nE; j++) {
                const EInfo& e = X.E[j];
                if (e.immune) continue;
                double p = 0;
                for (int h = 0; h < e.pn; h++) {
                    int myC, eC; afterCells(X, k, mv[k], j, h, myC, eC);
                    if (canAtk(cellAfter[k], eC)) p += e.pr[h];
                }
                if (p <= 0) continue;
                double v = 2.5 + (e.flag >= 0 ? 2.5 : 0) + (e.hp <= g.damage ? 3.0 : e.hp <= 2 * g.damage ? 1.0 : 0);
                if (X.myCarrierIdx >= 0 && D(e.cell, X.M[X.myCarrierIdx].cell) <= 3) v += 1.0;
                atk = max(atk, p * v);
            }
            s += (X.nTeams >= 6 ? 0.5 : 1.0) * atk;
        }

        if (X.nTeams >= 6) {
            for (int a = 0; a < 3; a++) for (int b = a + 1; b < 3; b++)
                if (cellAfter[a] >= 0 && cellAfter[b] >= 0 && manh(cellAfter[a], cellAfter[b]) < 2) s -= 0.8;
        }

        {
            double* F = comboFeat[ci];
            for (int q = 0; q < 16; q++) F[q] = gf[q];
            for (int k = 0; k < 3; k++) {
                double* U = F + 16 + k * 8;
                int c = want[k];
                if (c < 0) { for (int q = 0; q < 8; q++) U[q] = 0; continue; }
                double prog = 0;
                if (goalCell[k] >= 0 && goalD[k] < INF && D(c, goalCell[k]) < INF) prog = goalD[k] - D(c, goalCell[k]);
                int eNear = 0, aNear = 0;
                for (auto& e : X.E) if (!e.immune && manh(c, e.cell) <= 3) eNear++;
                for (int j = 0; j < 3; j++) if (j != k && X.M[j].alive && manh(c, X.M[j].cell) <= 3) aNear++;
                bool onSpot = false;
                for (auto& sp : g.spots) if (cid(sp.first, sp.second) == c) onSpot = true;
                U[0] = prog;
                U[1] = thrG[c];
                U[2] = eNear;
                U[3] = aNear;
                U[4] = X.M[k].hp / 100.0 * (X.M[k].flag >= 0 ? 2.0 : 1.0);
                U[5] = min(16, distToBase(X, c, X.me)) / 16.0;
                U[6] = min(16, manh(c, X.centerCell)) / 16.0;
                U[7] = onSpot ? 1.0 : 0.0;
            }
        }
#if LBP_HAS_NN

        s += (X.nTeams <= 3 ? P.NN_WEIGHT_SMALL : P.NN_WEIGHT_MULTI) * nn_score(comboFeat[ci]);
#endif
        comboScore[ci] = s;
        if (s > bestScore) { bestScore = s; bestMv = mv; }
#ifdef LBP_DEBUG
        
#endif
    }

    

    LBPT("4-act");
    int chosenCell[3];
    for (int k = 0; k < 3; k++) {
        if (!X.M[k].alive) { chosenCell[k] = -1; continue; }
        int nx = cx(X.M[k].cell) + DX[bestMv[k]], ny = cy(X.M[k].cell) + DY[bestMv[k]];
        chosenCell[k] = (nx < 0 || ny < 0 || nx >= N || ny >= N) ? X.M[k].cell : (W[cid(nx, ny)] ? X.M[k].cell : cid(nx, ny));
    }
    vector<string> act(3, "-");
    map<int, int> dmgOn;
    auto threatAt = [&](int c) {
        double best = 0; int cnt = 0;
        for (auto& e : X.E) { if (e.immune || e.flag >= 0) continue; double p = 0; for (int h = 0; h < e.pn; h++) if (canAtk(c, e.pc[h])) p += e.pr[h]; best = max(best, p); if (p >= 0.45) cnt++; }
        return make_pair(best, cnt);
    };
    for (int k = 0; k < 3; k++) {
        if (!X.M[k].alive) continue;
        const MU& m = X.M[k];
        int c = chosenCell[k];

        if (m.flag >= 0 && !m.spawning) {
            auto [pt, cnt] = threatAt(c);
            int dist = distToBase(X, c, X.me);
            int escorts = 0;
            for (int j = 0; j < 3; j++) if (j != k && X.M[j].alive && X.M[j].canAtk && !X.M[j].spawning && manh(X.M[j].cell, c) <= 3) escorts++;
            if (cnt == 1 && dist > (m.hp + g.damage - 1) / g.damage + 1 && (m.hp + g.damage - 1) / g.damage >= 2 && escorts <= 1 && X.remaining > dist + 6)
                act[k] = "X";
            continue;
        }

        if (m.flag < 0) {
            int onFlag = -1;
            for (size_t i = 0; i < g.flags.size(); i++) if (X.flagAvail[i] && X.flagCell[i] == c) onFlag = (int)i;
            if (onFlag >= 0) {
                auto [pt, cnt] = threatAt(c);
                bool timerOk = !(g.flags[onFlag].status == "dropped" && g.flags[onFlag].return_at >= 0 && g.flags[onFlag].return_at - X.T <= 2);
                bool guard = (pt >= 0.45) && (m.hp + g.damage - 1) / g.damage >= 2 && timerOk && m.canAtk && !m.spawning && X.remaining > distToBase(X, c, X.me) + 4;
                if (!guard) { act[k] = "P"; continue; }
            }
        }
        if (!m.canAtk || m.spawning) continue;
        double bestE = 0; int bestId = -1;
        for (auto& e : X.E) {
            if (e.immune) continue;
            double p = 0;
            for (int h = 0; h < e.pn; h++) if (canAtk(c, e.pc[h])) p += e.pr[h];
            if (p <= 0.001) continue;
            double v = 2.5 + (e.flag >= 0 ? 2.5 : 0) + (e.hp <= g.damage ? 3.0 : e.hp <= 2 * g.damage ? 1.0 : 0);
            if (X.myCarrierIdx >= 0 && D(e.cell, X.M[X.myCarrierIdx].cell) <= 3) v += 1.0;
            int needed = (e.hp + g.damage - 1) / g.damage;
            int already = dmgOn.count(e.id) ? dmgOn[e.id] : 0;
            if (already + 1 >= needed) v += 3.0;
            else if (already > 0) v += 0.8;
            if (p * v > bestE) { bestE = p * v; bestId = e.id; }
        }
        if (bestId >= 0) { act[k] = to_string(bestId); dmgOn[bestId]++; }
        else if (m.flag < 0) {
            for (auto& sp : g.spots) if (cid(sp.first, sp.second) == c) act[k] = "P";
        }
    }

    LBPT("5-out");
    string out = to_string(X.T);
    for (int k = 0; k < 3; k++) {
        if (!X.M[k].alive) { out += " S -"; continue; }
        out += ' '; out += MVC[bestMv[k]]; out += ' '; out += act[k];
    }
    return out;
}

}

namespace v10 {

#if defined(LBP_35_VALUE) && __has_include("lbp_value.h")
#include "lbp_value.h"
#define LBP_HAS_VALUE 1
#else
#define LBP_HAS_VALUE 0
#endif

#if defined(LBP_NN) && __has_include("lbp_nn.h")
#include "lbp_nn.h"
#define LBP_HAS_NN 1
#else
#define LBP_HAS_NN 0
#endif

#ifndef LBP_JA_ENABLED
#define LBP_JA_ENABLED 1
#endif
#ifndef LBP_JA_ALLOW_PICK
#define LBP_JA_ALLOW_PICK 0
#endif
#ifndef LBP_JA_PROTECT_PICK
#define LBP_JA_PROTECT_PICK 0
#endif
#ifndef LBP_JA_HIT_VALUE
#define LBP_JA_HIT_VALUE 2.0
#endif
#ifndef LBP_JA_KILL_VALUE
#define LBP_JA_KILL_VALUE 16.0
#endif
#ifndef LBP_JA_CARRIER_HIT
#define LBP_JA_CARRIER_HIT 2.0
#endif
#ifndef LBP_JA_CARRIER_KILL
#define LBP_JA_CARRIER_KILL 10.0
#endif
#ifndef LBP_JA_DENY
#define LBP_JA_DENY 6.0
#endif
#ifndef LBP_JA_LOWHP_KILL
#define LBP_JA_LOWHP_KILL 2.0
#endif
#ifndef LBP_JA_PICK_BASE
#define LBP_JA_PICK_BASE 20.0
#endif
#ifndef LBP_JA_PICK_OTHER
#define LBP_JA_PICK_OTHER 8.5
#endif
#ifndef LBP_JA_SURVIVAL
#define LBP_JA_SURVIVAL 0.5
#endif
#ifndef LBP_JA_FOCUS_HIT
#define LBP_JA_FOCUS_HIT 0.5
#endif
#ifndef LBP_JA_FOCUS_KILL
#define LBP_JA_FOCUS_KILL 1.0
#endif
#ifndef LBP_JA_THREAT_KILL
#define LBP_JA_THREAT_KILL 10.0
#endif
#ifndef LBP_JA_FLAG_HIT
#define LBP_JA_FLAG_HIT 0.0
#endif
#ifndef LBP_JA_FLAG_KILL
#define LBP_JA_FLAG_KILL 0.0
#endif
#ifndef LBP_JA_STACK_BONUS
#define LBP_JA_STACK_BONUS 0.0
#endif

#ifndef LBP_30_2P_FUTURE
#define LBP_30_2P_FUTURE 0
#endif

#ifndef LBP_31_2P_HORIZON
#define LBP_31_2P_HORIZON 1
#endif

#ifndef LBP_32_2P_SEARCH
#define LBP_32_2P_SEARCH 1
#endif
#ifndef LBP_32_2P_K
#define LBP_32_2P_K 8
#endif
#ifndef LBP_32_2P_GAMMA
#define LBP_32_2P_GAMMA 1.0
#endif

#ifndef LBP_34_RECURS
#define LBP_34_RECURS 0
#endif
#ifndef LBP_34_GAMMA
#define LBP_34_GAMMA 0.70
#endif
#ifndef LBP_34_SCEN
#define LBP_34_SCEN 2
#endif
#ifndef LBP_34_SOFT_MS
#define LBP_34_SOFT_MS 24
#endif
#ifndef LBP_34_DEPTH
#define LBP_34_DEPTH 1
#endif
static int    g_evalDepth = 0;
static double g_evalScore = 0.0;
static chrono::steady_clock::time_point g_34deadline;

#ifndef LBP_33_RETALIATE
#define LBP_33_RETALIATE 1
#endif

#ifndef LBP_2P_JOINT_MA
#define LBP_2P_JOINT_MA 1
#endif

#ifndef LBP_2P_JOINT_W
#define LBP_2P_JOINT_W 0.50
#endif

#ifndef LBP_2P_JOINT_RAWCELL
#define LBP_2P_JOINT_RAWCELL 0
#endif

#ifndef LBP_2P_JOINT_ACT
#define LBP_2P_JOINT_ACT 1
#endif

#ifndef LBP_2P_JOINT_KEEP_ATK
#define LBP_2P_JOINT_KEEP_ATK 1
#endif

#ifndef LBP_2P_JOINT_REL
#define LBP_2P_JOINT_REL 1
#endif

#ifndef LBP_2P_JOINT_ACT_MIN
#define LBP_2P_JOINT_ACT_MIN 0.05
#endif

#ifndef LBP_25_JOINT_ATTACK_ALL
#define LBP_25_JOINT_ATTACK_ALL 1
#endif

#ifndef LBP_4P_MARKS
#define LBP_4P_MARKS 0
#endif

#ifndef LBP_25_2P_FORMATION
#define LBP_25_2P_FORMATION 1
#endif

#ifndef LBP_25_2P_COUNT_ONLY
#define LBP_25_2P_COUNT_ONLY 1
#endif

#ifndef LBP_25_2P_SOLO_HOME
#define LBP_25_2P_SOLO_HOME 1
#endif

#ifndef LBP_25_2P_FLAG_PRIO
#define LBP_25_2P_FLAG_PRIO 1
#endif

#ifndef LBP_25_2P_DYNAMIC_COVER
#define LBP_25_2P_DYNAMIC_COVER 0
#endif

#ifndef LBP_25_2P_HARD_RETREAT
#define LBP_25_2P_HARD_RETREAT 0
#endif

#ifndef LBP_25_2P_POSTWIN_FEARLESS
#define LBP_25_2P_POSTWIN_FEARLESS 1
#endif

#ifndef LBP_25_2P_POSTWIN_DEATH_BASE
#define LBP_25_2P_POSTWIN_DEATH_BASE 0.6
#endif

#ifndef LBP_25_2P_POSTWIN_K10
#define LBP_25_2P_POSTWIN_K10 8
#endif

#ifndef LBP_25_2P_POSTWIN_FLAG_PRIO
#define LBP_25_2P_POSTWIN_FLAG_PRIO 1
#endif

#ifndef LBP_25_2P_CENTER_C_ROW
#define LBP_25_2P_CENTER_C_ROW 0
#endif

#ifndef LBP_25_2P_FIRE_SWAP
#define LBP_25_2P_FIRE_SWAP 0
#endif
#ifndef LBP_25_2P_FIRE_COVER_W
#define LBP_25_2P_FIRE_COVER_W 2.0
#endif
#ifndef LBP_25_2P_FIRE_SWAP_W
#define LBP_25_2P_FIRE_SWAP_W 3.0
#endif

#ifndef LBP_25_2P_PROTECT_PICK
#define LBP_25_2P_PROTECT_PICK 1
#endif

#ifndef LBP_25_2P_CARRIER_COMBAT
#define LBP_25_2P_CARRIER_COMBAT 1
#endif

#ifndef LBP_25_2P_CENTER_CONTEST
#define LBP_25_2P_CENTER_CONTEST 1
#endif

#ifndef LBP_25_2P_WIPE_CAMP
#define LBP_25_2P_WIPE_CAMP 1
#endif

#ifndef LBP_25_2P_IMPORTANT_FIGHT
#define LBP_25_2P_IMPORTANT_FIGHT 1
#endif
#ifndef LBP_25_2P_IMPORTANT_RETREAT
#define LBP_25_2P_IMPORTANT_RETREAT 1
#endif

#ifndef LBP_25_2P_OWN_SPAWN
#define LBP_25_2P_OWN_SPAWN 1
#endif

#ifndef LBP_25_2P_IMPORTANT_STANDING
#define LBP_25_2P_IMPORTANT_STANDING 1
#endif

#ifndef LBP_25_2P_IMPORTANT_RADIUS
#define LBP_25_2P_IMPORTANT_RADIUS 4
#endif

#ifndef LBP_25_2P_LOCAL_DISADV
#define LBP_25_2P_LOCAL_DISADV 1
#endif

#ifndef LBP_25_2P_CENTER_ETA_TOL
#define LBP_25_2P_CENTER_ETA_TOL 2
#endif
#ifndef LBP_25_2P_FLAG_PRIO_BONUS
#define LBP_25_2P_FLAG_PRIO_BONUS 18.0
#endif
#ifndef LBP_25_2P_SOFT_CENTER_BONUS
#define LBP_25_2P_SOFT_CENTER_BONUS 90.0
#endif
#ifndef LBP_25_2P_SOFT_POSTWIN_ENEMY_BONUS
#define LBP_25_2P_SOFT_POSTWIN_ENEMY_BONUS 60.0
#endif
#ifndef LBP_25_2P_SOFT_OWN_DISADV_BONUS
#define LBP_25_2P_SOFT_OWN_DISADV_BONUS 25.0
#endif
#ifndef LBP_25_2P_SOFT_OWN_ADV_PENALTY
#define LBP_25_2P_SOFT_OWN_ADV_PENALTY 25.0
#endif
#ifndef LBP_25_2P_SOFT_ENEMY_DISADV_PENALTY
#define LBP_25_2P_SOFT_ENEMY_DISADV_PENALTY 30.0
#endif
#ifndef LBP_25_2P_IMPORTANT_RETREAT_PENALTY
#define LBP_25_2P_IMPORTANT_RETREAT_PENALTY 35.0
#endif

#ifndef LBP_25_2P_FLAG_SOFT
#define LBP_25_2P_FLAG_SOFT 1
#endif

#ifndef LBP_25_2P_FLAG_RANK
#define LBP_25_2P_FLAG_RANK 1
#endif

#ifndef LBP_25_2P_CENTER_C_RUNNER
#define LBP_25_2P_CENTER_C_RUNNER 1
#endif

#ifndef LBP_25_2P_WIPE_RESPAWN
#define LBP_25_2P_WIPE_RESPAWN 10
#endif

#ifndef LBP_25_2P_CONTEST_HOLD
#define LBP_25_2P_CONTEST_HOLD 0
#endif

#ifndef LBP_25_2P_FORMATION_CALM_W
#define LBP_25_2P_FORMATION_CALM_W 1.0
#endif

#ifndef LBP_25_2P_A_FORWARD_DIST
#define LBP_25_2P_A_FORWARD_DIST 6
#endif

#ifndef LBP_25_2P_LOCAL_RADIUS

#define LBP_25_2P_LOCAL_RADIUS 5
#endif

#ifndef LBP_25_2P_FORMATION_W
#define LBP_25_2P_FORMATION_W 2.2
#endif

#ifndef LBP_2P_ADV_LOG
#define LBP_2P_ADV_LOG 0
#endif

#ifndef LBP_2P_ADV
#define LBP_2P_ADV 0
#endif

#ifndef LBP_2P_ADV_RISK
#define LBP_2P_ADV_RISK 0.0
#endif

#ifndef LBP_W_FOCUS_PAIR
#define LBP_W_FOCUS_PAIR 0.0
#endif
#ifndef LBP_W_FOCUS_TRIPLE
#define LBP_W_FOCUS_TRIPLE 0.0
#endif
#ifndef LBP_W_LETHAL
#define LBP_W_LETHAL 0.0
#endif
#ifndef LBP_W_CROSS_PAIR
#define LBP_W_CROSS_PAIR 0.0
#endif
#ifndef LBP_W_CROSS_ANGLE
#define LBP_W_CROSS_ANGLE 0.0
#endif
#ifndef LBP_W_COHESION
#define LBP_W_COHESION 0.0
#endif
#ifndef LBP_STRIKE_COND
#define LBP_STRIKE_COND 0
#endif
#ifndef LBP_LIFE_LOWHP_BONUS
#define LBP_LIFE_LOWHP_BONUS 8.0
#endif
#ifndef LBP_2P_LOWHP_MULT
#define LBP_2P_LOWHP_MULT 0.0
#endif
#ifndef LBP_2P_DEFEND_FOCUS
#define LBP_2P_DEFEND_FOCUS 0
#endif
#ifndef LBP_2P_DEFEND_LOWHP
#define LBP_2P_DEFEND_LOWHP 1
#endif
#ifndef LBP_2P_GUARD_MOVE
#define LBP_2P_GUARD_MOVE 0
#endif
#ifndef LBP_2P_GUARD_NEAR
#define LBP_2P_GUARD_NEAR 4.0
#endif
#ifndef LBP_2P_GUARD_HIT
#define LBP_2P_GUARD_HIT 1.5
#endif
#ifndef LBP_2P_FOCUS_2V1
#define LBP_2P_FOCUS_2V1 0.0
#endif

struct Params {

    double W_HIT_CARRIER = 2.1, W_HIT_PLAIN = 1.1;
    double LIFE_LOWHP_BONUS = LBP_LIFE_LOWHP_BONUS;
    double TW_CARRIER = 3.0, TW_LOWHP = 1.6, TW_PLAIN = 1.0;

    int K10_CARRIER_CHASED = 3, K10_CARRIER_CALM_DUEL = 19, K10_CARRIER_MELEE = 3, K10_OTHER = 12, K10_LOWHP = 15;

    double LAMBDA_DUEL = 2.57, LAMBDA_MID = 0.97, LAMBDA_MELEE = 0.56;
    double DUEL_CENTER_BIAS = 49.0;
    double AGGR_MELEE = 0.68;
    double AGGR_DUEL = 1.0;
    int    MELEE_GUARD_R = 13;
    double MELEE_DOOR_BIAS = 8.1, MELEE_ZONE_PEN = 6.5;
    double ESCORT_BASE_DUEL = 21.6, ESCORT_PER_THREAT = 22.2;

    double GATE_DEATH_CARRIER = 0.30;
    double GATE_DEATH_PLAIN = 0.99;
    double GATE_DEATH_LOWHP = 0.99;
    int    GATE_SOLO_MID_ENEMY_D = 6;
    int    GATE_SOLO_MID_ALLY_D = 4;
    double GATE_MELEE_CROWD = 2.0;

    double HUNT_BASE_MULTI = 3.7;
    double INTERCEPT_DENY_MULTI = 0.42;
#ifndef LBP_NN_WEIGHT
#define LBP_NN_WEIGHT 0.0
#endif
    double NN_WEIGHT_SMALL = LBP_NN_WEIGHT;
    double NN_WEIGHT_MULTI = 1.0;
    double W_XFIRE = 0.0;

    double W_STACK = 0.0;

    double DMG_WEIGHT = 0.60;
    double LIFE_CARRIER = 21.2, LIFE_PLAIN = 5.4, LIFE_NEARHOME = 6.0, LIFE_CARRIER_NEAR = 10.0;

    double INTERCEPT_AVOID = 3.0;
    double DUEL_MARKS = 0.0;
    double COVER_RADIUS = 3.0;

    double VERTICAL_WEIGHT = 1.0;

    double W_FOCUS_PAIR = LBP_W_FOCUS_PAIR;
    double W_FOCUS_TRIPLE = LBP_W_FOCUS_TRIPLE;
    double W_LETHAL = LBP_W_LETHAL;
    double W_CROSS_PAIR = LBP_W_CROSS_PAIR;
    double W_CROSS_ANGLE = LBP_W_CROSS_ANGLE;
    double W_COHESION = LBP_W_COHESION;
    double W_LINE_ADV = 2.0;
    int    COHESION_MAX = 6;

    int    JA_ENABLED = LBP_JA_ENABLED;
    int    JA_ALLOW_PICK = LBP_JA_ALLOW_PICK;
    int    JA_PROTECT_PICK = LBP_JA_PROTECT_PICK;
    double JA_HIT_VALUE = LBP_JA_HIT_VALUE;
    double JA_KILL_VALUE = LBP_JA_KILL_VALUE;
    double JA_CARRIER_HIT = LBP_JA_CARRIER_HIT;
    double JA_CARRIER_KILL = LBP_JA_CARRIER_KILL;
    double JA_DENY = LBP_JA_DENY;
    double JA_LOWHP_KILL = LBP_JA_LOWHP_KILL;
    double JA_PICK_BASE = LBP_JA_PICK_BASE;
    double JA_PICK_OTHER = LBP_JA_PICK_OTHER;
    double JA_SURVIVAL = LBP_JA_SURVIVAL;
    double JA_FOCUS_HIT = LBP_JA_FOCUS_HIT;
    double JA_FOCUS_KILL = LBP_JA_FOCUS_KILL;
    double JA_THREAT_KILL = LBP_JA_THREAT_KILL;
    double JA_FLAG_HIT = LBP_JA_FLAG_HIT;
    double JA_FLAG_KILL = LBP_JA_FLAG_KILL;
    double JA_STACK_BONUS = LBP_JA_STACK_BONUS;

};
static Params P;

static int v10_prevCell[3] = {-1, -1, -1};
static int v10_prevPost[3] = {-1, -1, -1};
struct V10Mark { int cell = -1; int until = -1; };
static vector<V10Mark> v10_marks;
static vector<pair<int,int>> v10_myDeaths;
static int v10_lastDeathCell = -1;
static int v11_lastKillTurn = -1;
static int v33_lastAtkTarget[3] = {-1, -1, -1};

static double v11_distR[15][5];
static int    v11_distH[15], v11_distN[15];
static double v11_deathR[15][3];
static int    v11_deathH[15], v11_deathN[15];
static double v11_flagRef[15];
static bool   v11_refReady = false;

static int N = 0, NN = 0;
static vector<char> W;
static vector<int16_t> Dall;
static bool pre = false;
static const int INF = 9999;
static const int DX[5] = {0, 0, 0, -1, 1}, DY[5] = {0, -1, 1, 0, 0};
static const char MVC[5] = {'S', 'U', 'D', 'L', 'R'};

inline int cid(int x, int y) { return y * N + x; }
inline int cx(int c) { return c % N; }
inline int cy(int c) { return c / N; }
inline int D(int a, int b) { if (a < 0 || b < 0) return INF; return Dall[(size_t)a * NN + b]; }
inline int manh(int a, int b) { return abs(cx(a) - cx(b)) + abs(cy(a) - cy(b)); }

void precompute(const Game& g) {
    N = g.size; NN = N * N; W.assign(NN, 0);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) W[cid(x, y)] = (g.map[y][x] == '#');
    Dall.assign((size_t)NN * NN, INF);
    vector<int> q(NN);
    for (int s = 0; s < NN; s++) {
        if (W[s]) continue;
        int16_t* d = &Dall[(size_t)s * NN];
        int h = 0, t = 0; q[t++] = s; d[s] = 0;
        while (h < t) {
            int c = q[h++]; int x = cx(c), y = cy(c);
            for (int k = 1; k < 5; k++) {
                int nx = x + DX[k], ny = y + DY[k];
                if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
                int nc = cid(nx, ny);
                if (W[nc] || d[nc] != INF) continue;
                d[nc] = d[c] + 1; q[t++] = nc;
            }
        }
    }
    pre = true;
}

bool canAtk(int a, int b) {
    if (a < 0 || b < 0) return false;
    int ax = cx(a), ay = cy(a), bx = cx(b), by = cy(b);
    int dx = bx - ax, dy = by - ay;
    int md = abs(dx) + abs(dy);
    if (md > 2) return false;
    if (abs(dx) == 2 && dy == 0) return !W[cid(ax + dx / 2, ay)];
    if (abs(dy) == 2 && dx == 0) return !W[cid(ax, ay + dy / 2)];
    if (abs(dx) == 1 && abs(dy) == 1) return !W[cid(ax + dx, ay)] || !W[cid(ax, ay + dy)];
    return true;
}

struct EInfo {
    int id, team, cell, hp, flag; bool immune;
    int pn;
    int pc[5]; double pr[5];
    double pStay;
};
struct MU { int id, cell, hp, flag; bool alive, spawning, canAtk; };
struct Task { int type; int key; int target; double val; int eta; int hits; };

static vector<int> occG;
static vector<int> thrG;
static vector<int> thrBase;

struct Ctx {
    const Game* g;
    int me, T, nTeams, remaining, centerCell;

    int preset = 1;
    double duelW = 0.0;

    int preyId = -1;
    bool allTurtle = false;

    vector<vector<int>> bcells;
    vector<MU> M;
    vector<EInfo> E;
    vector<int> flagCell, flagAvail;
    int myCarrier = -1, myCarrierIdx = -1;
    int myDoorSpot = -1, eDoorSpotMin = INF;
    int phase = 0;
    int holdUntil = 0;
    int attackTarget = -1, targetFlag = -1;

    int squadMode = 0;
    int squadFocus = -1;
    int squadFocusCell = -1;
    int squadFocusHp = 0;
    int squadFocusId = -1;

    double lambda = 2.0, aggr = 1.0;
    vector<int> spawnOrder;

    double tw[3]; int needMy[3];
    double otherW[45][5];

    static const int MAXE = 45;
    unsigned char hitAfter[3][5][MAXE][5];
};

void buildSpawn(Ctx& X) {
    X.spawnOrder = X.bcells[X.me];
    int bx, by; tie(bx, by) = X.g->bases[X.me];
    sort(X.spawnOrder.begin(), X.spawnOrder.end(), [&](int a, int b) {
        int da = manh(a, X.centerCell), db = manh(b, X.centerCell);
        if (da != db) return da < db;
        int ka = abs(cx(a) - bx) + 2 * abs(cy(a) - by), kb = abs(cx(b) - bx) + 2 * abs(cy(b) - by);
        return ka < kb;
    });
}

vector<int> ctgTo(const Ctx& X, const vector<int>& targets, int K10, const vector<int>* thrField = nullptr) {
    vector<int> cost(NN, INT_MAX);
    priority_queue<pair<int, int>, vector<pair<int, int>>, greater<>> pq;
    for (int t : targets) if (t >= 0 && !W[t]) { cost[t] = 0; pq.push({0, t}); }
    while (!pq.empty()) {
        auto [d, c] = pq.top(); pq.pop();
        if (d > cost[c]) continue;
        int x = cx(c), y = cy(c);
        for (int k = 1; k < 5; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
            int nc = cid(nx, ny);
            if (W[nc]) continue;
            int step = 10 + K10 * (thrField ? (*thrField)[nc] : thrG[nc]);
            if (occG[nc] >= 0 && occG[nc] / 3 != X.me) step += 25;
            int nd = d + step;
            if (nd < cost[nc]) { cost[nc] = nd; pq.push({nd, nc}); }
        }
    }
    return cost;
}

int distToBase(const Ctx& X, int c, int team) {
    int best = INF; for (int b : X.bcells[team]) best = min(best, D(c, b)); return best;
}
int nearestBaseCell(const Ctx& X, int c, int team) {
    int best = INF, bc = X.bcells[team][4];
    for (int b : X.bcells[team]) if (D(c, b) < best) { best = D(c, b); bc = b; }
    return bc;
}

void valueFeatures(const Ctx& X, const Game& g, double* f) {
    for (int i = 0; i < 40; i++) f[i] = 0.0;
    int avail[16], nAvail = 0;
    for (size_t i = 0; i < g.flags.size() && i < 16; i++) {
        const Flag& fl = g.flags[i];
        if (fl.x >= 0 && (fl.status == "home" || fl.status == "dropped")) avail[nAvail++] = cid(fl.x, fl.y);
    }
    auto nearestFlag = [&](int c) { int best = INF; for (int i = 0; i < nAvail; i++) best = min(best, D(c, avail[i])); return best; };
    for (int k = 0; k < 3; k++) {
        double* u = f + 8 * k;
        const MU& m = X.M[k];
        if (!m.alive) continue;
        u[0] = 1.0;
        u[1] = m.hp / 100.0;
        u[2] = (m.flag >= 0) ? 1.0 : 0.0;
        if (m.flag >= 0) u[3] = min(16, distToBase(X, m.cell, X.me)) / 16.0;
        else { int d = nearestFlag(m.cell); u[3] = (d >= INF) ? 2.0 : min(16, d) / 16.0; }
        int ne = INF; for (auto& e : X.E) if (e.cell >= 0) ne = min(ne, D(m.cell, e.cell));
        u[4] = (ne >= INF) ? 2.0 : min(16, ne) / 16.0;
        int e3 = 0, a3 = 0;
        for (auto& e : X.E) if (e.cell >= 0 && manh(m.cell, e.cell) <= 3) e3++;
        for (int j = 0; j < 3; j++) if (j != k && X.M[j].alive && manh(m.cell, X.M[j].cell) <= 3) a3++;
        u[5] = min(3, e3) / 3.0;
        u[6] = min(2, a3) / 2.0;
        bool onSpot = false;
        for (int i = 0; i < nAvail; i++) if (avail[i] == m.cell) onSpot = true;
        u[7] = onSpot ? 1.0 : 0.0;
    }
    double* ef = f + 24;
    int eAlive = 0, eCarrier = -1, eHp = 0, eFlagDist = INF;
    for (auto& e : X.E) {
        if (e.cell < 0) continue;
        eAlive++; eHp += e.hp;
        if (e.flag >= 0) eCarrier = e.cell;
        else { int d = nearestFlag(e.cell); if (d < eFlagDist) eFlagDist = d; }
    }
    ef[0] = eAlive / 3.0;
    ef[1] = (eCarrier >= 0) ? 1.0 : 0.0;
    if (eCarrier >= 0) ef[2] = min(16, distToBase(X, eCarrier, 1 - X.me)) / 16.0;
    ef[3] = (eFlagDist >= INF) ? 2.0 : min(16, eFlagDist) / 16.0;
    (void)eHp;
    double* gf = f + 28;
    int myAlive = 0, myHp = 0, myCarrier = -1, myFlagDist = INF;
    for (int k = 0; k < 3; k++) {
        if (!X.M[k].alive) continue;
        myAlive++; myHp += X.M[k].hp;
        if (X.M[k].flag >= 0) myCarrier = X.M[k].cell;
        else myFlagDist = min(myFlagDist, nearestFlag(X.M[k].cell));
    }
    gf[0] = myAlive / 3.0;
    gf[1] = myHp / 300.0;
    gf[2] = (g.score[X.me] - g.score[1 - X.me]) / 10.0;
    gf[3] = nAvail / 4.0;
    gf[4] = (myFlagDist >= INF) ? 2.0 : min(16, myFlagDist) / 16.0;
    gf[5] = (myCarrier >= 0) ? 1.0 : 0.0;
    if (myCarrier >= 0) gf[6] = min(16, distToBase(X, myCarrier, X.me)) / 16.0;
    gf[7] = (g.turns - g.turn) / 400.0;
    double bal = 0;
    for (int i = 0; i < nAvail; i++) {
        int dm = INF, de = INF;
        for (int k = 0; k < 3; k++) if (X.M[k].alive && X.M[k].flag < 0) dm = min(dm, D(X.M[k].cell, avail[i]));
        for (auto& e : X.E) if (e.cell >= 0 && e.flag < 0) de = min(de, D(e.cell, avail[i]));
        bal += (min(dm, 16) - min(de, 16)) / 16.0;
    }
    gf[8] = (nAvail ? bal / nAvail : 0.0);
    gf[9] = (double)X.nTeams / 15.0;
}

static double astraProfile[15][2][4];
static double astraLastPr[48][4][5];
static int    astraLastEp[48][5];
static int    astraLastClass[48];
static int    astraPrevPos[48];
static bool   astraHasPrev[48];

void predictEnemiesAstra(Ctx& X, bool save) {
    const Game& g = *X.g;

    if (save && X.T > 1) {
        for (auto& e : X.E) {
            int i = e.id;
            if (i < 0 || i >= 48 || e.cell < 0 || !astraHasPrev[i] || e.immune) continue;
            if (manh(e.cell, astraPrevPos[i]) > 1) continue;
            int cls = (e.flag >= 0) ? 1 : 0;
            for (int model = 0; model < 4; model++) {
                double q = 0;
                for (int m = 0; m < 5; m++)
                    if (astraLastEp[i][m] == e.cell) q += astraLastPr[i][model][m];
                astraProfile[e.team][cls][model] =
                    astraProfile[e.team][cls][model] * 0.987 + 0.16 * log(0.05 + 0.95 * q);
            }
        }
    }

    int eg[48];
    int doorSpot[15];
    for (int t = 0; t < g.teams && t < 15; t++) {
        int bc = X.bcells[t][4], bd = INF;
        doorSpot[t] = cid(N / 2, N / 2);
        for (auto& sp : g.spots) {
            int sc = cid(sp.first, sp.second), d = D(sc, bc);
            if (d < bd) { bd = d; doorSpot[t] = sc; }
        }
    }
    for (auto& e : X.E) {
        int i = e.id;
        if (e.cell < 0) continue;
        int p = e.cell;
        eg[i] = doorSpot[e.team];
        if (e.flag >= 0) { eg[i] = X.bcells[e.team][4]; continue; }
        double best = 1e9;
        for (size_t fi = 0; fi < g.flags.size(); fi++)
            if (X.flagAvail[fi] && (g.flags[fi].return_at < 0 || g.flags[fi].return_at > X.T)) {
                int q = X.flagCell[fi];
                double v = D(p, q) + 0.18 * distToBase(X, q, e.team);
                for (auto& e2 : X.E)
                    if (e2.id < i && e2.cell >= 0 && e2.flag < 0 && eg[e2.id] == q) v += 3.5;
                if (v < best) { best = v; eg[i] = q; }
            }
        for (auto& m2 : X.M)
            if (m2.alive && m2.flag >= 0 && D(p, m2.cell) <= 5) {
                double v = D(p, m2.cell) - 2.0;
                if (v < best) { best = v; eg[i] = m2.cell; }
            }
    }

    for (auto& e : X.E) {
        int i = e.id;
        if (e.cell < 0 || i < 0 || i >= 48) { e.pn = 0; e.pStay = 1.0; continue; }
        int p = e.cell;
        int ep[5];
        double pr[5] = {0, 0, 0, 0, 0};
        for (int m = 0; m < 5; m++) {
            int nx = cx(p) + DX[m], ny = cy(p) + DY[m];
            ep[m] = (nx < 0 || ny < 0 || nx >= N || ny >= N) ? p : cid(nx, ny);
        }
        bool carrying = (e.flag >= 0);
        int cls = carrying ? 1 : 0;
        double ps[4][5];
        bool atkHere = false;
        for (auto& m2 : X.M)
            if (m2.alive && !m2.spawning && canAtk(p, m2.cell)) atkHere = true;
        for (int m = 0; m < 5; m++) {
            for (int model = 0; model < 4; model++) ps[model][m] = 0;
            int q = ep[m];
            if (m != 0 && q == p) { for (int model = 0; model < 4; model++) ps[model][m] = -50; continue; }
            double prog = 0;
            if (carrying) prog = (double)(distToBase(X, p, e.team) - distToBase(X, q, e.team));
            else if (D(p, eg[i]) < INF && D(q, eg[i]) < INF) prog = (double)(D(p, eg[i]) - D(q, eg[i]));
            double danger = 0, attack = 0, ally = 0;
            for (auto& m2 : X.M) {
                if (!m2.alive || m2.cell < 0) continue;
                int d = manh(q, m2.cell);
                if (m2.flag < 0 && !m2.spawning)
                    danger += d <= 1 ? 1.0 : d == 2 ? 0.9 : d == 3 ? 0.5 : d == 4 ? 0.12 : 0;
                if (!m2.spawning) {
                    double val = (m2.hp <= 34 ? 2.8 : m2.hp <= 68 ? 1.8 : 1.2) + (m2.flag >= 0 ? 1.8 : 0);
                    attack = max(attack, val * (canAtk(q, m2.cell) ? 1.0 : (d == 3 ? 0.25 : 0.0)));
                }
            }
            for (auto& e2 : X.E)
                if (e2.cell >= 0 && e2.team == e.team && e2.flag < 0) {
                    int d = manh(q, e2.cell);
                    ally += d <= 2 ? 0.65 : d == 4 ? 0.3 : 0;
                }
            double vals[4];
            if (carrying) {
                vals[0] = 3.2 * prog;
                vals[1] = 2.3 * prog - 2.1 * danger;
                vals[2] = 1.8 * prog - 4.0 * danger;
                vals[3] = 3.2 * prog - 0.7 * danger;
            } else {
                vals[0] = atkHere ? ((m == 0) ? 3.8 : attack * 0.65) : (2.1 * prog + 0.15 * attack);
                vals[1] = 1.1 * prog + 1.5 * attack - 1.0 * danger - max(0.0, danger - ally - 1.0);
                vals[2] = 1.2 * prog + 1.7 * attack - 2.0 * danger - max(0.0, danger - ally - 0.5) * 1.5;
                vals[3] = 0.6 * prog + 2.2 * attack - 0.45 * danger;
            }
            for (int model = 0; model < 4; model++) {
                if (occG[q] >= 0 && occG[q] != i) vals[model] -= 1.4;
                if (m == 0) vals[model] += 0.08;
                if (astraHasPrev[i] && astraPrevPos[i] >= 0 && astraPrevPos[i] != p && q - p == p - astraPrevPos[i])
                    vals[model] += 0.35;
                ps[model][m] = vals[model];
            }
        }

        {
            double wm = -1e9;
            for (int model = 0; model < 4; model++) wm = max(wm, astraProfile[e.team][cls][model]);
            static double weights_dbg[4];
            double weights[4], ws = 0;
            for (int model = 0; model < 4; model++) {
                weights[model] = exp(astraProfile[e.team][cls][model] - wm);
                weights_dbg[model] = weights[model];
                ws += weights[model];
            }
            for (int model = 0; model < 4; model++) {
                double mx = -1e9;
                for (int m2 = 0; m2 < 5; m2++) mx = max(mx, ps[model][m2]);
                double sum = 0, psm[5];
                for (int m2 = 0; m2 < 5; m2++) { psm[m2] = exp((ps[model][m2] - mx) / 0.65); sum += psm[m2]; }
                for (int m2 = 0; m2 < 5; m2++) pr[m2] += (0.92 * weights[model] / ws + 0.02) * psm[m2] / sum;
                if (save)
                    for (int m2 = 0; m2 < 5; m2++) astraLastPr[i][model][m2] = psm[m2] / sum;
            }
        }
        for (int m = 0; m < 5; m++) { e.pc[m] = ep[m]; e.pr[m] = pr[m]; }
        e.pn = 5;
        e.pStay = pr[0];
        if (save) {
            for (int m = 0; m < 5; m++) astraLastEp[i][m] = ep[m];
            astraLastClass[i] = cls;
            astraPrevPos[i] = p;
            astraHasPrev[i] = true;
        }
    }
}

void predictEnemy(Ctx& X, EInfo& e) {
    e.pn = 0; e.pStay = 1.0;
    if (e.immune) { e.pc[0] = e.cell; e.pr[0] = 1.0; e.pn = 1; return; }
    int x = cx(e.cell), y = cy(e.cell);
    int goal = -1, curG = INF, myNear = -1, curM = INF;
    if (e.flag >= 0) {
        goal = nearestBaseCell(X, e.cell, e.team); curG = D(e.cell, goal);
    } else {
        for (size_t i = 0; i < X.g->flags.size(); i++) if (X.flagAvail[i]) { int d = D(e.cell, X.flagCell[i]); if (d < curG) { curG = d; goal = X.flagCell[i]; } }
        for (auto& m : X.M) if (m.alive) { int d = D(e.cell, m.cell); if (d < curM) { curM = d; myNear = m.cell; } }
    }
    double w[5]; int cells[5]; int cnt = 0; double tot = 0;
    for (int k = 0; k < 5; k++) {
        int nx = x + DX[k], ny = y + DY[k];
        if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
        int nc = cid(nx, ny);
        if (W[nc]) continue;
        double ww;
        if (e.flag >= 0) ww = (k == 0) ? 0.5 : ((D(nc, goal) < curG) ? 5.0 : 0.4);
        else {
            ww = (k == 0) ? 1.2 : 1.0;
            if (goal >= 0 && k != 0 && D(nc, goal) < curG) ww += 2.0;
            if (myNear >= 0 && curM <= 4 && k != 0 && D(nc, myNear) < curM) ww += 1.5;
            if (k != 0 && occG[nc] >= 0) ww *= 0.3;
        }
        w[cnt] = ww; cells[cnt] = nc; tot += ww; cnt++;
    }
    for (int i = 0; i < cnt; i++) { e.pc[i] = cells[i]; e.pr[i] = w[i] / tot; if (cells[i] == e.cell) e.pStay = e.pr[i]; }
    e.pn = cnt;
}

inline void afterCells(const Ctx& X, int k, int mv, int j, int h, int& myC, int& eC) {
    const MU& m = X.M[k]; const EInfo& e = X.E[j];
    int myWant = (mv == 0) ? m.cell : -1;
    { int nx = cx(m.cell) + DX[mv], ny = cy(m.cell) + DY[mv];
      if (nx >= 0 && ny >= 0 && nx < N && ny < N) { int nc = cid(nx, ny); if (!W[nc]) myWant = nc; } }
    if (myWant < 0) myWant = m.cell;
    int eWant = e.pc[h];
    if (myWant == eWant) { myC = m.cell; eC = e.cell; return; }
    if (myWant == e.cell) {
        if (eWant != e.cell) { myC = myWant; eC = eWant; return; }
        myC = m.cell; eC = e.cell; return;
    }
    if (eWant == m.cell && mv != 0) { myC = myWant; eC = eWant; return; }
    myC = myWant; eC = eWant;
}

struct ComboLog { double feat[64]; int chosen; int nCombo; };

#if LBP_37_ROLLOUT
string decide(const Game& g);
static int    g_rolloutDepth = 0;
static bool   roll34On = false;
static FILE*  roll34F = nullptr;
static int    roll34Stride = 4, roll34W = 12;

struct RollSnapshot {
    vector<V10Mark> marks;
    vector<pair<int, int>> myDeaths;
    int lastDeathCell, lastKillTurn, prevCell[3], prevPost[3], lastAtk[3];
    double distR[15][5], deathR[15][3];
    int distH[15], distN[15], deathH[15], deathN[15];
    void save() {
        marks = v10_marks; myDeaths = v10_myDeaths;
        lastDeathCell = v10_lastDeathCell; lastKillTurn = v11_lastKillTurn;
        for (int k = 0; k < 3; k++) { prevCell[k] = v10_prevCell[k]; prevPost[k] = v10_prevPost[k]; lastAtk[k] = v33_lastAtkTarget[k]; }
        memcpy(distR, v11_distR, sizeof(distR)); memcpy(deathR, v11_deathR, sizeof(deathR));
        memcpy(distH, v11_distH, sizeof(distH)); memcpy(distN, v11_distN, sizeof(distN));
        memcpy(deathH, v11_deathH, sizeof(deathH)); memcpy(deathN, v11_deathN, sizeof(deathN));
    }
    void restore() {
        v10_marks = marks; v10_myDeaths = myDeaths;
        v10_lastDeathCell = lastDeathCell; v11_lastKillTurn = lastKillTurn;
        for (int k = 0; k < 3; k++) { v10_prevCell[k] = prevCell[k]; v10_prevPost[k] = prevPost[k]; v33_lastAtkTarget[k] = lastAtk[k]; }
        memcpy(v11_distR, distR, sizeof(distR)); memcpy(v11_deathR, deathR, sizeof(deathR));
        memcpy(v11_distH, distH, sizeof(distH)); memcpy(v11_distN, distN, sizeof(distN));
        memcpy(v11_deathH, deathH, sizeof(deathH)); memcpy(v11_deathN, deathN, sizeof(deathN));
    }
};

static bool rollParseOut(const string& o, int mv[3], int act[3]) {

    istringstream in(o);
    int t; string a, b;
    if (!(in >> t)) return false;
    static const string MC = "SUDLR";
    for (int k = 0; k < 3; k++) {
        if (!(in >> a >> b)) return false;
        size_t p = MC.find(a[0]);
        mv[k] = (p == string::npos) ? 0 : (int)p;
        if (b == "-") act[k] = -1;
        else if (b == "P") act[k] = -2;
        else if (b == "X") act[k] = -3;
        else act[k] = atoi(b.c_str());
    }
    return true;
}

static void resolveRollTurn(Game& g, int me, const int mvM[3], const int acM[3], const int mvE[3], const int acE[3]) {
    const int T = g.turn;
    const int nu = g.teams * 3;
    int orig[6], want[6], fin[6], teamof[6], mvof[6], acof[6];
    bool immune[6] = {};

    for (auto& f : g.flags) {
        if (f.status != "carried" && f.return_at >= 0 && f.return_at <= T) {
            int empt[16], ne2 = 0;
            for (size_t si = 0; si < g.spots.size() && si < 16; si++) {
                bool occ2 = false;
                for (auto& f3 : g.flags)
                    if (&f3 != &f && f3.status == "home" && f3.x >= 0 && f3.x == g.spots[si].first && f3.y == g.spots[si].second) occ2 = true;
                if (!occ2) empt[ne2++] = (int)si;
            }
            if (ne2 > 0) {
                unsigned long long h = (unsigned long long)T * 1315423911ull ^ ((unsigned long long)f.id * 2654435761ull);
                h ^= h << 13; h ^= h >> 7; h ^= h << 17;
                int si = empt[h % (unsigned)ne2];
                f.x = g.spots[si].first; f.y = g.spots[si].second;
            } else { f.x = -1; f.y = -1; }
            f.status = "home"; f.holder = -1; f.return_at = T + g.flag_return;
        }
    }

    for (auto& u : g.units) {
        const int id = u.id;
        if (u.x >= 0 || u.respawn_at < 0 || u.respawn_at > T) continue;
        int bx = g.bases[id / 3].first, by = g.bases[id / 3].second;
        vector<pair<pair<int, int>, int>> sp;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) sp.push_back({{manh(cid(bx + dx, by + dy), N / 2 + (N / 2) * N), abs(dx) + 2 * abs(dy)}, cid(bx + dx, by + dy)});
        sort(sp.begin(), sp.end());
        for (auto& [k2, cell] : sp) {
            bool occ2 = false;
            for (auto& u2 : g.units) if (u2.x >= 0 && u2.y * N + u2.x == cell) occ2 = true;
            for (auto& u2 : g.units) if (u2.id != id && u2.x < 0 && u2.respawn_at >= 0 && u2.respawn_at <= T && u2.id < id) {

            }
            if (!occ2) { u.x = cell % N; u.y = cell / N; u.hp = 100; u.flag = -1; u.respawn_at = -1; immune[id] = true; break; }
        }
    }

    for (int id = 0; id < nu && id < 6; id++) {
        Unit& u = g.units[id];
        teamof[id] = id / 3;
        if (u.x < 0) { orig[id] = want[id] = fin[id] = -1; mvof[id] = 0; acof[id] = -1; continue; }
        orig[id] = cid(u.x, u.y);
        const int* mv = (id / 3 == me) ? mvM : mvE;
        const int* ac = (id / 3 == me) ? acM : acE;
        mvof[id] = mv[id % 3];
        acof[id] = ac[id % 3];
        if (acof[id] >= 0) {
            if (u.flag >= 0 || immune[id]) acof[id] = -1;
        }
        int m = mvof[id];
        int nx = cx(orig[id]) + DX[m], ny = cy(orig[id]) + DY[m];
        want[id] = (nx < 0 || ny < 0 || nx >= N || ny >= N) ? orig[id] : (W[cid(nx, ny)] ? orig[id] : cid(nx, ny));
    }

    {
        int cnt[NN];
        for (int it = 0; it < 4; it++) {
            bool changed = false;
            for (int c2 = 0; c2 < NN; c2++) cnt[c2] = 0;
            for (int id = 0; id < nu && id < 6; id++) if (fin[id] < 0) fin[id] = orig[id];
            for (int id = 0; id < nu && id < 6; id++) if (orig[id] >= 0) cnt[want[id]]++;
            for (int id = 0; id < nu && id < 6; id++) {
                if (orig[id] < 0) continue;
                fin[id] = want[id];
                if (cnt[want[id]] > 1 && want[id] != orig[id]) { fin[id] = orig[id]; changed = true; continue; }
                for (int j = 0; j < nu && j < 6; j++) {
                    if (j == id || orig[j] < 0) continue;
                    if (want[id] == orig[j] && want[j] == orig[id] && teamof[j] != teamof[id]) { fin[id] = orig[id]; changed = true; break; }
                    if (want[id] == orig[j] && fin[j] == orig[j] && j < id) { fin[id] = orig[id]; changed = true; break; }
                }
            }
            if (!changed) break;
        }
    }

    int dmg[6] = {};
    for (int id = 0; id < nu && id < 6; id++) {
        if (orig[id] < 0 || immune[id] || acof[id] < 0) continue;
        int tid = acof[id];
        if (tid < 0 || tid >= nu || tid / 3 == id / 3) continue;
        if (g.units[tid].x < 0 || immune[tid]) continue;
        int fc = -1;
        for (int j = 0; j < nu && j < 6; j++) if (g.units[j].id == tid) fc = j;
        if (fc < 0 || fin[fc] < 0) continue;
        if (canAtk(fin[id], fin[fc])) dmg[fc] += 34;
    }
    for (int id = 0; id < nu && id < 6; id++) {
        Unit& u = g.units[id];
        if (orig[id] < 0) continue;
        u.x = fin[id] % N; u.y = fin[id] / N;
        u.hp -= dmg[id];
        if (u.hp <= 0) {
            if (u.flag >= 0) {
                Flag& f = g.flags[u.flag];
                f.status = "dropped"; f.holder = -1; f.x = u.x; f.y = u.y; f.return_at = T + g.flag_return;
            }
            u.x = u.y = -1; u.hp = 0; u.flag = -1; u.respawn_at = T + g.respawn;
        }
    }

    for (int id = 0; id < nu && id < 6; id++) {
        Unit& u = g.units[id];
        if (u.x < 0 || immune[id] || acof[id] != -2 || u.flag >= 0 || fin[id] < 0) continue;
        for (auto& f : g.flags)
            if ((f.status == "home" || f.status == "dropped") && f.x >= 0 && cid(f.x, f.y) == fin[id]) {
                u.flag = f.id; f.status = "carried"; f.x = f.y = -1; f.holder = id; f.return_at = -1;
                break;
            }
    }

    for (int id = 0; id < nu && id < 6; id++) {
        Unit& u = g.units[id];
        if (u.x < 0 || acof[id] != -3 || u.flag < 0) continue;
        Flag& f = g.flags[u.flag];
        f.status = "dropped"; f.holder = -1; f.x = u.x; f.y = u.y; f.return_at = T + g.flag_return;
        u.flag = -1;
    }

    for (auto& u : g.units) {
        if (u.x < 0 || u.flag < 0) continue;
        int bc = cid(g.bases[u.id / 3].first, g.bases[u.id / 3].second);
        if (manh(u.y * N + u.x, bc) <= 1) {
            Flag& f = g.flags[u.flag];
            u.flag = -1; f.status = "cooldown"; f.holder = -1; f.x = f.y = -1; f.return_at = T + g.flag_cooldown;
            g.score[u.id / 3]++;
        }
    }
}

static int rolloutValue(const Game& g0, int me, const int mvC[3], const int acC[3], int W) {
    Game g = g0;
    RollSnapshot snap; snap.save();
    g_rolloutDepth++;

    int mvM[3], acM[3];
    for (int k = 0; k < 3; k++) {
        mvM[k] = mvC[k];
        acM[k] = acC[k];
        if (acM[k] >= 0) acM[k] = 3 * (1 - me) + acM[k] % 3;
    }
    int mvE[3] = {0, 0, 0}, acE[3] = {-1, -1, -1};
    { Game t = g; t.me = 1 - me;
      if (!rollParseOut(decide(t), mvE, acE)) { g_rolloutDepth--; snap.restore(); return g.score[me] - g.score[1 - me]; } }
    g.turn++;
    resolveRollTurn(g, me, mvM, acM, mvE, acE);
    for (int w = 1; w < W && g.turn < g.turns; w++) {
        int mvA[3], acA[3], mvB[3], acB[3];
        { Game t = g; t.me = me;    if (!rollParseOut(decide(t), mvA, acA)) break; }
        { Game t = g; t.me = 1 - me; if (!rollParseOut(decide(t), mvB, acB)) break; }
        g.turn++;
        resolveRollTurn(g, me, mvA, acA, mvB, acB);
    }
    int v = g.score[me] - g.score[1 - me];
    g_rolloutDepth--;
    snap.restore();
    return v;
}
#endif

string decide(const Game& g) {
    if (!pre) precompute(g);
    LBPT("0-start");
    auto t0 = chrono::steady_clock::now();
    Ctx X; X.g = &g; X.me = g.me; X.T = g.turn; X.nTeams = g.teams;
    X.remaining = g.turns - X.T + 1;
    X.centerCell = cid(N / 2, N / 2);

    if (X.nTeams <= 3) { X.preset = 0; }
    else if (X.nTeams <= 11) { X.preset = 1; }
    else { X.preset = 2; }
    X.duelW = (X.nTeams >= 4 && X.nTeams <= 11) ? (12 - X.nTeams) / 10.0 : 0.0;

    auto v10_ms = [&]() { return chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - t0).count(); };
    X.aggr = (X.nTeams == 2) ? P.AGGR_DUEL : P.AGGR_MELEE;

    static const double lam_tbl[] = {
        0.56, 0.56,
        0.60, 0.90,
        0.97, 0.95, 0.88, 0.82,
        0.76, 0.70, 0.63,
        0.56, 0.56, 0.56, 0.56, 0.56
    };
    int idx = (X.nTeams < 2) ? 2 : (X.nTeams > 15) ? 15 : X.nTeams;
    double lam = lam_tbl[idx];
#ifdef LBP_30_LAM2
    if (idx == 2) lam = LBP_30_LAM2;
#endif
    X.lambda = std::clamp(lam, P.LAMBDA_MELEE, P.LAMBDA_MID);

#if defined(LBP_30_JA_KILL_2P) || defined(LBP_30_JA_HIT_2P)
    if (X.nTeams == 2) {
#ifdef LBP_30_JA_KILL_2P
        P.JA_KILL_VALUE = LBP_30_JA_KILL_2P;
#endif
#ifdef LBP_30_JA_HIT_2P
        P.JA_HIT_VALUE = LBP_30_JA_HIT_2P;
#endif
    }
#endif

    for (int t = 0; t < X.nTeams; t++) {
        vector<int> v; auto [bx, by] = g.bases[t];
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) v.push_back(cid(bx + dx, by + dy));
        X.bcells.push_back(v);
    }
    LBPT("0a-bcells");
    occG.assign(NN, -1);
    for (auto& u : g.units) if (u.x >= 0) occG[cid(u.x, u.y)] = u.id;
    buildSpawn(X);

    LBPT("0b-spawn");

    X.M.assign(3, {});
    for (int k = 0; k < 3; k++) {
        int uid = 3 * X.me + k; const Unit* u = nullptr;
        for (auto& x : g.units) if (x.id == uid) u = &x;
        MU m{}; m.id = uid; m.cell = -1; m.hp = 0; m.flag = -1; m.alive = false; m.spawning = false; m.canAtk = false;
        if (u) {
            if (u->x >= 0) { m.cell = cid(u->x, u->y); m.hp = u->hp; m.flag = u->flag; m.alive = true; m.canAtk = (u->flag < 0); }
            else if (u->respawn_at == X.T) {
                for (int c : X.spawnOrder) if (occG[c] < 0) { m.cell = c; occG[c] = uid; break; }
                if (m.cell >= 0) { m.alive = true; m.spawning = true; m.hp = g.hp; m.canAtk = false; }
            }
        }
        X.M[k] = m;
    }
    for (int k = 0; k < 3; k++) if (X.M[k].alive && X.M[k].flag >= 0) { X.myCarrierIdx = k; break; }

    LBPT("0c-mine");

    for (auto& u : g.units) {
        if (u.team == X.me) continue;
        EInfo e{}; e.id = u.id; e.team = u.team; e.hp = u.hp; e.flag = u.flag; e.immune = false;
        if (u.x >= 0) e.cell = cid(u.x, u.y);
        else if (u.respawn_at == X.T) { e.cell = X.bcells[u.team][4]; e.immune = true; e.hp = g.hp; }
        else continue;
        X.E.push_back(e);
    }

    LBPT("0d-enemy");

    X.flagCell.assign(g.flags.size(), -1);
    X.flagAvail.assign(g.flags.size(), 0);
    for (size_t i = 0; i < g.flags.size(); i++) {
        auto& f = g.flags[i];
        if ((f.status == "home" || f.status == "dropped") && f.x >= 0) { X.flagCell[i] = cid(f.x, f.y); X.flagAvail[i] = 1; }
        if ((int)i == X.me) X.myDoorSpot = cid(g.spots[i].first, g.spots[i].second);
    }

#if LBP_3P_ASTRA

    bool savePred3P = (g_evalDepth == 0);
#if LBP_37_ROLLOUT
    savePred3P = savePred3P && (g_rolloutDepth == 0);
#endif
    predictEnemiesAstra(X, savePred3P);
#else
    for (auto& e : X.E) predictEnemy(X, e);
#endif
    LBPT("1-pred");

    

    LBPT("0e-pred");

    thrG.assign(NN, 0);

    {
        auto flagDist = [&](int cell) {
            int best = INF;
            for (size_t i = 0; i < g.flags.size(); i++) if (X.flagAvail[i]) best = min(best, D(cell, X.flagCell[i]));
            return best;
        };
        if (!v11_refReady) {
            for (int t = 0; t < g.teams && t < 15; t++) {
                int bc = cid(g.bases[t].first, g.bases[t].second), d = INF;
                for (auto& sp : g.spots) d = min(d, D(bc, cid(sp.first, sp.second)));
                v11_flagRef[t] = max(1, d);
            }
            v11_refReady = true;
        }
        if (g_evalDepth == 0)
        for (int t = 0; t < g.teams && t < 15; t++) {
            if (t == X.me) continue;
            double sum = 0; int cnt = 0;
            for (auto& e : X.E) if (e.team == t && !e.immune) {
                int d = flagDist(e.cell); if (d < INF) { sum += d; cnt++; }
            }
            if (cnt) {
                v11_distR[t][v11_distH[t]] = (sum / cnt) / (double)v11_flagRef[t];
                v11_distH[t] = (v11_distH[t] + 1) % 5;
                if (v11_distN[t] < 5) v11_distN[t]++;
            }
        }
        if (g_evalDepth == 0)
        for (auto& ev : g.events) {
            if (ev.type != "death" || (int)ev.args.size() < 3) continue;
            int t = ev.args[0] / 3;
            if (t == X.me || t >= 15 || t >= g.teams) continue;
            int dx = ev.args[1], dy = ev.args[2];
            if (dx < 0 || dy < 0 || dx >= N || dy >= N) continue;
            v11_deathR[t][v11_deathH[t]] = flagDist(cid(dx, dy)) / (double)v11_flagRef[t];
            v11_deathH[t] = (v11_deathH[t] + 1) % 3;
            if (v11_deathN[t] < 3) v11_deathN[t]++;
        }
        auto mean = [](const double* a, int n) { double s = 0; for (int i = 0; i < n; i++) s += a[i]; return n ? s / n : -1.0; };
        X.preyId = -1; X.allTurtle = true; bool anyT = false;
        for (int t = 0; t < g.teams && t < 15; t++) {
            if (t == X.me) continue;
            anyT = true;
            double fm = mean(v11_distR[t], v11_distN[t]);
            double dm = mean(v11_deathR[t], v11_deathN[t]);
            if (dm >= 0 && fm > 0 && dm < 0.4 * fm) X.preyId = t;
            if (fm < 0 || fm <= 0.7) X.allTurtle = false;
        }
        if (!anyT) X.allTurtle = false;
    }

    if (g_evalDepth == 0) {
    for (auto& e : g.events) {
        if (e.type == "death" && (int)e.args.size() >= 3) {
            int deadId = e.args[0];
            if (deadId / 3 == X.me) {
                int dx = e.args[1], dy = e.args[2];
                if (dx >= 0 && dy >= 0 && dx < N && dy < N) {
                    int dc = cid(dx, dy);
                    v10_lastDeathCell = dc;
                    v10_myDeaths.push_back({dc, X.T});
                    if ((int)v10_myDeaths.size() > 12) v10_myDeaths.erase(v10_myDeaths.begin());
                    int same = 0;
                    for (auto& d : v10_myDeaths) if (manh(d.first, dc) <= 5) same++;
                    bool markFire = (LBP_4P_MARKS != 0 && X.nTeams >= 4 && same >= 2);
            if (markFire) v10_marks.push_back({dc, X.T + 30});
                }
            }

            if ((int)e.args.size() >= 5 && e.args[4] > 0) {
                bool myKill = false;
                for (int ki = 5; ki < (int)e.args.size() && ki < 5 + e.args[4]; ki++) {
                    if (e.args[ki] / 3 == X.me) { myKill = true; break; }
                }
                if (myKill) v11_lastKillTurn = X.T;
            }
        }
    }
    vector<V10Mark> keep;
    for (auto& m : v10_marks) if (m.until >= X.T) keep.push_back(m);
    v10_marks.swap(keep);
    }

    for (auto& e : X.E) {
        if (e.immune || e.flag >= 0) continue;
        int ex = cx(e.cell), ey = cy(e.cell);
        for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++) {
            if (abs(dx) + abs(dy) > 3) continue;
            int x = ex + dx, y = ey + dy;
            if (x < 0 || y < 0 || x >= N || y >= N) continue;
            thrG[cid(x, y)]++;
        }
    }
    thrBase = thrG;

    for (auto& m : v10_marks) {
        if (m.cell < 0 || m.cell >= NN || W[m.cell]) continue;
        int mx = cx(m.cell), my = cy(m.cell);
        for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
            if (abs(dx) + abs(dy) > 2) continue;
            int x = mx + dx, y = my + dy;
            if (x < 0 || y < 0 || x >= N || y >= N) continue;
            thrG[cid(x, y)] += 2;
        }
    }

    for (auto& m : v10_marks) {
        if (m.cell < 0 || m.cell >= NN || W[m.cell]) continue;
        int mx = cx(m.cell), my = cy(m.cell);
        for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
            if (abs(dx) + abs(dy) > 2) continue;
            int x = mx + dx, y = my + dy;
            if (x < 0 || y < 0 || x >= N || y >= N) continue;
            thrG[cid(x, y)] -= 2;
            if (manh(m.cell, X.centerCell) <= N / 4) thrG[cid(x, y)] += 1;
        }
    }

    for (int k = 0; k < 3; k++) {
        const MU& m = X.M[k];
        int lives = m.hp <= 0 ? 0 : (m.hp + g.damage - 1) / g.damage;
        X.needMy[k] = lives;
        double twv = P.TW_PLAIN;
        if (m.flag >= 0) twv = P.TW_CARRIER;
        else if (lives <= 1) twv = P.TW_LOWHP;
        X.tw[k] = twv;
    }

    for (size_t j = 0; j < X.E.size() && j < 45; j++) {
        const EInfo& e = X.E[j];
        for (int h = 0; h < e.pn; h++) {
            double others = 0;
            for (auto& u : g.units) {
                if (u.x < 0 || u.team == e.team) continue;
                if (u.id / 3 == X.me) continue;
                int uc = cid(u.x, u.y);
                if (!canAtk(e.pc[h], uc)) continue;
                others += (u.flag >= 0) ? P.TW_CARRIER : ((u.hp + g.damage - 1) / g.damage <= 1 ? P.TW_LOWHP : P.TW_PLAIN);
            }
            X.otherW[j][h] = others;
        }
        for (int h = e.pn; h < 5; h++) X.otherW[j][h] = 0;
    }

    LBPT("0f-threat");

    vector<Task> tasks[3];
    int nAliveMe = 0; for (auto& m : X.M) if (m.alive) nAliveMe++;
    int nAliveE = 0, livesE = 0, livesMe = 0;
    for (auto& e : X.E) if (!e.immune) { nAliveE++; livesE += (e.hp + g.damage - 1) / g.damage; }
    for (auto& m : X.M) if (m.alive) livesMe += (m.hp + g.damage - 1) / g.damage;
    int enemyCarrierJ = -1;
    for (size_t j = 0; j < X.E.size(); j++) if (X.E[j].flag >= 0) enemyCarrierJ = (int)j;

    int nCombatMe = nAliveMe, nCombatE = nAliveE;
    if (LBP_25_2P_CARRIER_COMBAT) {
        nCombatMe = max(0, nAliveMe - (X.myCarrierIdx >= 0 ? 1 : 0));
        nCombatE = max(0, nAliveE - (enemyCarrierJ >= 0 ? 1 : 0));
    }
    bool v25Advantage = (nCombatMe > nCombatE);
    bool v25Disadvantage = (nCombatMe < nCombatE);

    int v25EnemyDoorSpot = -1;
    bool v25EnemyFlagHome = false;
    if ((int)g.spots.size() > 1 - X.me) v25EnemyDoorSpot = cid(g.spots[1 - X.me].first, g.spots[1 - X.me].second);
    else v25EnemyDoorSpot = X.bcells[1 - X.me][4];
    auto nearCenter = [&](int c) { return manh(c, X.centerCell) <= 2; };
    auto nearEnemyDoor = [&](int c) { return v25EnemyDoorSpot >= 0 && manh(c, v25EnemyDoorSpot) <= 2; };
    int v25CenterFlag = -1, v25OwnFlag = -1, v25EnemyFlag = -1;
    if (X.nTeams == 2) {
        for (size_t i = 0; i < g.flags.size(); i++) if (X.flagAvail[i]) {
            int fc = X.flagCell[i];
            if (nearCenter(fc)) v25CenterFlag = (int)i;
            if (fc == X.myDoorSpot) v25OwnFlag = (int)i;
            if (nearEnemyDoor(fc)) { v25EnemyFlag = (int)i; v25EnemyFlagHome = true; }
        }
    }
    bool v25OwnFlagHome = (v25OwnFlag >= 0) || !LBP_25_2P_OWN_SPAWN;

    bool v25CenterContest = false;
    if (LBP_25_2P_CENTER_CONTEST && X.nTeams == 2 && v25CenterFlag >= 0) {
        int mEta = INF, eEta = INF;
        for (auto& m : X.M) if (m.alive && m.flag < 0) mEta = min(mEta, D(m.cell, X.centerCell));
        for (auto& e : X.E) if (!e.immune && e.flag < 0 && e.cell >= 0) eEta = min(eEta, D(e.cell, X.centerCell));
        v25CenterContest = (mEta < INF && (eEta == INF || mEta <= eEta + LBP_25_2P_CENTER_ETA_TOL));
    }
    bool v25PostWin = (X.nTeams == 2 && LBP_25_2P_POSTWIN_FEARLESS && v25EnemyFlagHome
                       && (nCombatE == 0 || nCombatMe > nCombatE));
    double v25DeathScale = 1.0;
    if (v25PostWin) {
        int diff = max(1, nCombatMe - nCombatE);
        if (nCombatE == 0) diff = max(1, nCombatMe);
        v25DeathScale = pow(LBP_25_2P_POSTWIN_DEATH_BASE, diff);
        v25DeathScale = clamp(v25DeathScale, 0.04, 1.0);
    }

    auto minETA = [&](int cell, bool mine) {
        int best = INF;
        if (mine) { for (auto& m : X.M) if (m.alive && m.flag < 0) best = min(best, D(m.cell, cell)); }
        else for (auto& e : X.E) if (!e.immune && e.flag < 0) best = min(best, D(e.cell, cell));
        return best;
    };

    if (X.nTeams == 2) {

        int enemyDoorSpot = -1;
        if ((int)g.spots.size() > 1 - X.me) enemyDoorSpot = cid(g.spots[1 - X.me].first, g.spots[1 - X.me].second);
        else enemyDoorSpot = X.bcells[1 - X.me][4];

        auto v25FlagPrio = [&](int fi) {
            int fc = X.flagCell[fi];
            if (nearCenter(fc)) return 3;
            if (nearEnemyDoor(fc)) return 2;
            if (fc == X.myDoorSpot) return 1;
            return 0;
        };

        auto v25FlagRank = [&](int fi) {
            int fc = X.flagCell[fi];
            if (v25CenterContest && fc == X.centerCell) return 6;
            if (v25PostWin && fc == enemyDoorSpot) return 5;
            if (fc == X.centerCell) return 4;
            if (fc == enemyDoorSpot && !v25Disadvantage) return 3;
            if (fc == X.myDoorSpot && v25Disadvantage) return 3;
            if (fc == X.myDoorSpot) return 1;
            return (g.flags[fi].status == "dropped") ? 2 : 1;
        };
        if (X.myCarrierIdx >= 0) {
            X.phase = 1;
            for (int k = 0; k < 3; k++) {
                if (!X.M[k].alive) { tasks[k].push_back({-1, 0, -1, 0, 0, 0}); continue; }
                if (X.M[k].flag >= 0) { tasks[k].push_back({0, 0, -1, 1000, 0, 0}); continue; }
                int threats = 0;
                for (auto& e : X.E) if (!e.immune && e.flag < 0 && D(e.cell, X.M[X.myCarrierIdx].cell) <= 5) threats++;
                tasks[k].push_back({3, X.myCarrierIdx, X.M[X.myCarrierIdx].cell,
                                    P.ESCORT_BASE_DUEL + 14.0 + P.ESCORT_PER_THREAT * min(3, threats), 0, 0});

                for (size_t i = 0; i < g.flags.size(); i++) if (X.flagAvail[i]) {
                    int fc = X.flagCell[i];
                    int det = D(X.M[k].cell, fc) + D(fc, X.M[X.myCarrierIdx].cell) - D(X.M[k].cell, X.M[X.myCarrierIdx].cell);
                    bool carrierSafe = true;
                    for (auto& e : X.E) if (!e.immune && e.flag < 0 && D(e.cell, X.M[X.myCarrierIdx].cell) <= 4) carrierSafe = false;
                    if (det <= 3 && carrierSafe && D(fc, X.bcells[X.me][4]) < 8)
                        tasks[k].push_back({1, (int)i, fc, 90.0, D(X.M[k].cell, fc), 0});
                }
            }
        } else {

            int lmx = 0, lmy = 0, lmn = 0;
            for (auto& m : X.M) if (m.alive && m.cell != -1) { lmn++; lmx += cx(m.cell); lmy += cy(m.cell); }
            int localCenter = (lmn > 0) ? cid(lmx / lmn, lmy / lmn) : X.bcells[X.me][4];
            int localMe = 0, localE = 0;
            for (auto& m : X.M) if (m.alive && m.flag < 0 && m.cell != -1
                                     && manh(m.cell, localCenter) <= LBP_25_2P_LOCAL_RADIUS) localMe++;
            for (auto& e : X.E) if (!e.immune && e.flag < 0 && e.cell != -1
                                     && manh(e.cell, localCenter) <= LBP_25_2P_LOCAL_RADIUS) localE++;
            bool countDisadvantage = (localE > localMe);

            if (countDisadvantage) {
                v25CenterContest = false;
                if (LBP_25_2P_LOCAL_DISADV) v25Disadvantage = true;
            }

            int bestFlag = -1; double bestV = -1e18; int bestEta = INF;
            for (size_t i = 0; i < g.flags.size(); i++) {
                if (!X.flagAvail[i]) continue;
                int fc = X.flagCell[i];
                int eta = INF; for (auto& m : X.M) if (m.alive && m.flag < 0) eta = min(eta, D(m.cell, fc));
                if (eta >= INF) continue;
                if (g.flags[i].status == "dropped" && g.flags[i].return_at >= 0 && eta > g.flags[i].return_at - X.T) continue;
                int ret = distToBase(X, fc, X.me); if (ret >= INF) continue;
                if (eta + ret > X.remaining) continue;

                if (v25CenterContest && fc == X.myDoorSpot) continue;
                int eEta = minETA(fc, false);
                double pget;
                if (eta + 2 < eEta) pget = 0.97; else if (eta < eEta) pget = 0.85;
                else if (eta == eEta) pget = 0.45; else if (eta == eEta + 1) pget = 0.25; else pget = 0.1;
                double pret = 0.95 - 0.10 * min(3, nAliveE);
                double val = 100.0 * pget * pret - X.lambda * (eta + ret);
                if (X.allTurtle) val += 15.0;
                if (nearCenter(fc)) val += P.DUEL_CENTER_BIAS;
                if (LBP_25_2P_FLAG_PRIO) val += LBP_25_2P_FLAG_PRIO_BONUS * v25FlagPrio((int)i);
#if LBP_25_2P_FLAG_SOFT
                if (v25CenterContest && nearCenter(fc)) val += LBP_25_2P_SOFT_CENTER_BONUS;
                if (v25PostWin && nearEnemyDoor(fc)) val += LBP_25_2P_SOFT_POSTWIN_ENEMY_BONUS;
                if (!v25Disadvantage && nearEnemyDoor(fc)) val += 20.0;
                if (fc == X.myDoorSpot) val += v25Disadvantage ? LBP_25_2P_SOFT_OWN_DISADV_BONUS : -LBP_25_2P_SOFT_OWN_ADV_PENALTY;
                if (nearEnemyDoor(fc) && v25Disadvantage) val -= LBP_25_2P_SOFT_ENEMY_DISADV_PENALTY;
#else

                if (v25CenterContest && nearCenter(fc)) val += 25.0;
#endif
                if (g.flags[i].status == "dropped") val += 10;

                if (v25Disadvantage && fc != X.myDoorSpot) val *= 0.25;

                {
                    int sup = 0, opp = 0;
                    for (int j = 0; j < 3; j++)
                        if (X.M[j].alive && X.M[j].flag < 0 && X.M[j].cell >= 0
                            && manh(X.M[j].cell, fc) <= 7) sup++;
                    for (auto& e : X.E)
                        if (!e.immune && e.flag < 0 && e.cell >= 0
                            && manh(e.cell, fc) <= 5) opp++;

                    if ((v25Disadvantage && sup < opp) || opp >= sup + 2) val *= 0.25;
                }
                if (val > bestV) { bestV = val; bestFlag = (int)i; bestEta = eta; }
            }
            int eEta = (bestFlag >= 0) ? minETA(X.flagCell[bestFlag], false) : INF;
            int myEta = (bestFlag >= 0) ? minETA(X.flagCell[bestFlag], true) : INF;
            bool enemyThreatensHome = false;
            if (enemyCarrierJ >= 0) {
                int eb = nearestBaseCell(X, X.E[enemyCarrierJ].cell, X.E[enemyCarrierJ].team);
                if (D(X.E[enemyCarrierJ].cell, eb) <= 10) enemyThreatensHome = true;
            }
#if LBP_25_2P_COUNT_ONLY
            bool fightOk = !countDisadvantage;
#else
            bool fightOk = (livesMe + 1 >= livesE) || (nAliveMe > nAliveE);
#endif
            bool marginOk = (myEta != INF && eEta != INF && myEta <= eEta + 2) || eEta == INF;

            bool holdKeep = (X.T < X.holdUntil) && !(myEta != INF && eEta != INF && myEta + 2 <= eEta);

            int aliveEnemyNow = 0;
            for (auto& e : X.E) if (!e.immune && e.cell != -1) aliveEnemyNow++;
            bool enemyWiped = (aliveEnemyNow == 0);
            bool killWindow = (v11_lastKillTurn >= 0 && X.T - v11_lastKillTurn <= 10);
            bool postWin = enemyWiped || (killWindow && nCombatMe > nCombatE);
            int aliveMeNow = 0, sx = 0, sy = 0;
            for (auto& m : X.M) if (m.alive && m.cell != -1) { aliveMeNow++; sx += cx(m.cell); sy += cy(m.cell); }
            int squadCell = (aliveMeNow > 0) ? cid(sx / aliveMeNow, sy / aliveMeNow) : X.bcells[X.me][4];

            int focusJ = -1; double focusScore = -1e18;
            for (int j = 0; j < (int)X.E.size(); j++) {
                auto& e = X.E[j];
                if (e.immune || e.cell == -1) continue;
                double v = 0;
                if (e.flag >= 0) v += 1000.0;
                if (e.cell == X.centerCell) v += 300.0;
                if (e.cell == X.myDoorSpot) v += 250.0;
                for (auto& sp : g.spots) if (cid(sp.first, sp.second) == e.cell) { v += 150.0; break; }
                int lives = (e.hp + g.damage - 1) / g.damage;
                if (lives <= 1) v += 80.0; else if (e.hp <= 68) v += 40.0;
                int support = 0;
                for (auto& e2 : X.E) if (e2.id != e.id && !e2.immune && e2.cell != -1 && manh(e2.cell, e.cell) <= 3) support++;
                if (support == 0) v += 30.0;
                v -= 2.0 * manh(e.cell, squadCell);
                if (v > focusScore) { focusScore = v; focusJ = j; }
            }
            X.squadFocus = focusJ;
            X.squadFocusCell = (focusJ >= 0) ? X.E[focusJ].cell : -1;
            X.squadFocusHp = (focusJ >= 0) ? X.E[focusJ].hp : 0;
            X.squadFocusId = (focusJ >= 0) ? X.E[focusJ].id : -1;

            int enemyOnSpot = 0;
            for (auto& e : X.E) {
                if (e.immune || e.cell == -1) continue;
                for (auto& sp : g.spots) if (cid(sp.first, sp.second) == e.cell) { enemyOnSpot++; break; }
            }
            int nearestEnemy = INF;
            for (auto& e : X.E) if (!e.immune && e.cell != -1) nearestEnemy = min(nearestEnemy, manh(e.cell, squadCell));

            bool defendFocus = false;
            bool defendThreat = false;
#if LBP_2P_DEFEND_FOCUS
            if (X.nTeams == 2 && focusJ >= 0 && nAliveMe >= 2 && nAliveE <= 2 && nearestEnemy <= 6) {
                const EInfo& fe = X.E[focusJ];
                bool nearDefSpot = (X.myDoorSpot >= 0 && manh(fe.cell, X.myDoorSpot) <= 2)
                                || (D(fe.cell, X.bcells[X.me][4]) <= 5);
                bool allyLow = false;
                for (auto& m : X.M) if (m.alive && m.flag < 0) {
                    int lives = (m.hp + g.damage - 1) / g.damage;
                    if (lives <= 1 && manh(m.cell, fe.cell) <= 4) allyLow = true;
                }

                bool canJoin = false;
                for (int h = 0; h < fe.pn && !canJoin; h++) {
                    if (fe.pr[h] < 0.15) continue;
                    int shooters = 0;
                    for (int k = 0; k < 3; k++) {
                        if (!X.M[k].alive || X.M[k].spawning) continue;
                        for (int mv = 0; mv < 5; mv++) {
                            int nx = cx(X.M[k].cell) + DX[mv], ny = cy(X.M[k].cell) + DY[mv];
                            int nc = (nx < 0 || ny < 0 || nx >= N || ny >= N) ? X.M[k].cell
                                     : (W[cid(nx, ny)] ? X.M[k].cell : cid(nx, ny));
                            if (canAtk(nc, fe.pc[h])) { shooters++; break; }
                        }
                    }
                    if (shooters >= 2) canJoin = true;
                }

                defendThreat = nearDefSpot && allyLow;
                defendFocus = defendThreat && LBP_2P_DEFEND_LOWHP != 3
                           && (LBP_2P_DEFEND_LOWHP == 2 || canJoin);
            }
#endif

            bool carrierAtDoor = false, carrierIsolatedNear = false;
            if (enemyCarrierJ >= 0) {
                int eb = nearestBaseCell(X, X.E[enemyCarrierJ].cell, X.E[enemyCarrierJ].team);
                carrierAtDoor = (D(X.E[enemyCarrierJ].cell, eb) <= 3);
                int sup = 0;
                for (auto& e2 : X.E) if (e2.id != X.E[enemyCarrierJ].id && !e2.immune && e2.cell != -1
                                          && manh(e2.cell, X.E[enemyCarrierJ].cell) <= 3) sup++;
                carrierIsolatedNear = (sup == 0 && D(X.E[enemyCarrierJ].cell, squadCell) <= 4);
            }
            bool advantage = !carrierAtDoor && !carrierIsolatedNear
                          && postWin && fightOk;
            bool strike = false;
#if LBP_STRIKE_COND

            {
                bool focusOnSpot = false;
                if (focusJ >= 0) {
                    for (auto& sp : g.spots) {
                        if (cid(sp.first, sp.second) == X.E[focusJ].cell) { focusOnSpot = true; break; }
                    }
                }
                int focusLives = (focusJ >= 0) ? (X.E[focusJ].hp + g.damage - 1) / g.damage : 99;
                bool localFight = (focusJ >= 0 && nearestEnemy <= 6 && !countDisadvantage);
                bool worthFight = (focusOnSpot || enemyOnSpot > 0 || focusLives <= 2
                                   || X.E[focusJ].flag >= 0);
                strike = localFight && worthFight && !holdKeep;
            }
#endif

            if (defendFocus) {

                int lowA = -1;
                for (int k = 0; k < 3; k++) {
                    if (!X.M[k].alive || X.M[k].flag >= 0) continue;
                    int lives = (X.M[k].hp + g.damage - 1) / g.damage;
                    if (lives <= 1 && manh(X.M[k].cell, X.E[focusJ].cell) <= 4) { lowA = k; break; }
                }
                X.phase = 2; X.squadMode = 1;
                for (int k = 0; k < 3; k++) {
                    if (!X.M[k].alive) { tasks[k].push_back({-1, 0, -1, 0, 0, 0}); continue; }
                    if (X.M[k].flag >= 0) { tasks[k].push_back({0, 0, -1, 1000, 0, 0}); continue; }
                    int lives = (X.M[k].hp + g.damage - 1) / g.damage;
                    if (LBP_2P_DEFEND_LOWHP && lives <= 1) {
                        tasks[k].push_back({0, 0, -1, 1000.0, 0, 0});
                    } else if (LBP_2P_DEFEND_LOWHP == 2 && lowA >= 0) {
                        int gc = X.M[lowA].cell;
                        tasks[k].push_back({5, gc, gc,
                                            700.0 - 2.0 * D(X.M[k].cell, gc),
                                            D(X.M[k].cell, gc), 0});
                    } else {
                        tasks[k].push_back({6, X.squadFocusId, X.squadFocusCell,
                                            800.0 - 2.0 * D(X.M[k].cell, X.squadFocusCell),
                                            D(X.M[k].cell, X.squadFocusCell), 0});
                    }
                }
            } else if (advantage) {

                X.phase = 2; X.squadMode = 3;
                int spotsArr[3]; int nSpots = 0;
                spotsArr[nSpots++] = X.centerCell;

                if (v25OwnFlagHome && X.myDoorSpot >= 0) spotsArr[nSpots++] = X.myDoorSpot;
                for (auto& sp : g.spots) {
                    int sc = cid(sp.first, sp.second);
                    if (sc != X.centerCell && sc != X.myDoorSpot) { spotsArr[nSpots++] = sc; break; }
                }
                vector<int> avail;
                for (size_t i = 0; i < g.flags.size(); i++) {
                    if (!X.flagAvail[i]) continue;
#if LBP_25_2P_FLAG_SOFT

                    if (v25CenterContest && X.flagCell[i] == X.myDoorSpot) continue;
#endif
                    avail.push_back((int)i);
                }
#if LBP_25_2P_FLAG_RANK
                sort(avail.begin(), avail.end(), [&](int a, int b) {
                    int ta = v25FlagRank(a), tb = v25FlagRank(b);
                    if (ta != tb) return ta > tb;
                    int da = D(squadCell, X.flagCell[a]);
                    int db = D(squadCell, X.flagCell[b]);
                    if (da != db) return da < db;
                    return (X.flagCell[a] == X.centerCell) > (X.flagCell[b] == X.centerCell);
                });
#else
                auto postWinRank = [&](int fi) {
                    int fc = X.flagCell[fi];
                    if (fc == enemyDoorSpot) return 3;
                    if (fc == X.centerCell) return 2;
                    if (fc == X.myDoorSpot) return 1;
                    return 0;
                };
                sort(avail.begin(), avail.end(), [&](int a, int b) {
                    if (LBP_25_2P_FLAG_PRIO && (postWin || (LBP_25_2P_POSTWIN_FLAG_PRIO && v25PostWin))) {
                        int ra = postWinRank(a), rb = postWinRank(b);
                        if (ra != rb) return ra > rb;
                    } else if (LBP_25_2P_FLAG_PRIO) {
                        int pa = v25FlagPrio(a), pb = v25FlagPrio(b);
                        if (pa != pb) return pa > pb;
                    }
                    int da = D(squadCell, X.flagCell[a]);
                    int db = D(squadCell, X.flagCell[b]);
                    if (da != db) return da < db;
                    return (X.flagCell[a] == X.centerCell) > (X.flagCell[b] == X.centerCell);
                });
#endif
#if LBP_25_2P_DYNAMIC_COVER

                bool unitUsed[3] = {false, false, false};
                for (int fi : avail) {
                    int fc = X.flagCell[fi];
                    int pick = -1, bd = INF;
                    for (int k = 0; k < 3; k++) {
                        if (!X.M[k].alive || X.M[k].flag >= 0 || unitUsed[k]) continue;
                        int d = D(X.M[k].cell, fc);
                        if (d < bd) { bd = d; pick = k; }
                    }
                    if (pick < 0) continue;
                    unitUsed[pick] = true;
                    tasks[pick].push_back({1, fi, fc, 400.0 - 2.0 * bd, bd, 0});
                }
                bool spotTaken[3] = {false, false, false};
                for (int k = 0; k < 3; k++) {
                    if (!X.M[k].alive) { tasks[k].push_back({-1, 0, -1, 0, 0, 0}); continue; }
                    if (X.M[k].flag >= 0) { tasks[k].push_back({0, 0, -1, 1000, 0, 0}); continue; }
                    if (unitUsed[k]) continue;
                    int bi = -1, bd = INF;
                    for (int s = 0; s < nSpots; s++) if (!spotTaken[s]) {
                        int d = D(X.M[k].cell, spotsArr[s]);
                        if (d < bd) { bd = d; bi = s; }
                    }
                    if (bi >= 0) {
                        spotTaken[bi] = true;
                        tasks[k].push_back({5, spotsArr[bi], spotsArr[bi], 250.0 - 2.0 * bd, bd, 0});
                    } else {
                        tasks[k].push_back({5, X.centerCell, X.centerCell, 120.0, 0, 0});
                    }
                }
#else
                bool spotTaken[3] = {false, false, false};
                int ai = 0;
                for (int k = 0; k < 3; k++) {
                    if (!X.M[k].alive) { tasks[k].push_back({-1, 0, -1, 0, 0, 0}); continue; }
                    if (X.M[k].flag >= 0) { tasks[k].push_back({0, 0, -1, 1000, 0, 0}); continue; }
                    if (ai < (int)avail.size()) {
                        int fi = avail[ai++];
                        int fc = X.flagCell[fi];
                        tasks[k].push_back({1, fi, fc, 400.0 - 2.0 * D(X.M[k].cell, fc), D(X.M[k].cell, fc), 0});
                    } else {
                        int bi = -1, bd = INF;
                        for (int s = 0; s < nSpots; s++) if (!spotTaken[s]) {
                            int d = D(X.M[k].cell, spotsArr[s]);
                            if (d < bd) { bd = d; bi = s; }
                        }
                        if (bi >= 0) {
                            spotTaken[bi] = true;
                            tasks[k].push_back({5, spotsArr[bi], spotsArr[bi], 250.0 - 2.0 * bd, bd, 0});
                        } else {
                            tasks[k].push_back({5, X.centerCell, X.centerCell, 120.0, 0, 0});
                        }
                    }
                }
#endif
            } else if (strike && focusJ >= 0) {

                X.phase = 2; X.squadMode = 1;
                int focusLives = (X.squadFocusHp + g.damage - 1) / g.damage;
                double focusVal = 420.0 + 30.0 * (3 - min(3, focusLives));
                for (int k = 0; k < 3; k++) {
                    if (!X.M[k].alive) { tasks[k].push_back({-1, 0, -1, 0, 0, 0}); continue; }
                    if (X.M[k].flag >= 0) { tasks[k].push_back({0, 0, -1, 1000, 0, 0}); continue; }
                    tasks[k].push_back({6, X.squadFocusId, X.squadFocusCell,
                                        focusVal - 1.5 * D(X.M[k].cell, X.squadFocusCell),
                                        D(X.M[k].cell, X.squadFocusCell), 0});
                }
            } else if (enemyThreatensHome || !marginOk || !fightOk || holdKeep) {
                X.phase = 2; X.squadMode = 0;
                X.holdUntil = X.T + 6;
                int nGuards = 0;
                for (int k = 0; k < 3; k++) {
                    if (!X.M[k].alive) { tasks[k].push_back({-1, 0, -1, 0, 0, 0}); continue; }
                    if (X.M[k].flag >= 0) { tasks[k].push_back({0, 0, -1, 1000, 0, 0}); continue; }
                }

                int fetcher = -1, fetchFlag = -1, fetchD = INF;
#if LBP_25_2P_CONTEST_HOLD

                if (v25CenterContest && v25CenterFlag >= 0) {
                    for (int k = 0; k < 3; k++) {
                        if (!X.M[k].alive || X.M[k].flag >= 0) continue;
                        int d = D(X.M[k].cell, X.centerCell);
                        if (d < fetchD) { fetchD = d; fetcher = k; fetchFlag = v25CenterFlag; }
                    }
                }
#endif
                for (int pass = 0; pass < 2 && fetcher < 0; pass++) {
                    for (int k = 0; k < 3; k++) {
                        if (!X.M[k].alive || X.M[k].flag >= 0) continue;
                        for (size_t i = 0; i < g.flags.size(); i++) if (X.flagAvail[i]) {
                            int fc = X.flagCell[i];
                            if (pass == 0) {
                                if (D(fc, X.bcells[X.me][4]) > 14) continue;
                                if (D(fc, X.bcells[X.me][4]) < D(fc, X.bcells[1 - X.me][4]) - 2) continue;
                            } else {

                                int eEta2 = minETA(fc, false);
                                int mEta2 = minETA(fc, true);
                                if (eEta2 != INF && mEta2 != INF && mEta2 + 2 > eEta2) continue;
                            }
                            int d = D(X.M[k].cell, fc);
                            if (d < fetchD) { fetchD = d; fetcher = k; fetchFlag = (int)i; }
                        }
                    }
                }
                if (fetcher >= 0) tasks[fetcher].push_back({1, fetchFlag, X.flagCell[fetchFlag], 200.0, fetchD, 0});
                for (int k = 0; k < 3; k++) {
                    if (!X.M[k].alive || X.M[k].flag >= 0 || k == fetcher) continue;
                    if (enemyCarrierJ >= 0) {

                        const EInfo& e = X.E[enemyCarrierJ];
                        int tb = nearestBaseCell(X, e.cell, e.team);

                        int tfirst = INF, tgt = -1;
                        int c = e.cell;
                        for (int step = 0; step <= 14 && c != tb; step++) {
                            if (D(X.M[k].cell, c) <= step + 1) { tfirst = step; tgt = c; break; }
                            int nxt = -1;
                            for (int q = 1; q < 5; q++) {
                                int nx = cx(c) + DX[q], ny = cy(c) + DY[q];
                                if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
                                int nc = cid(nx, ny);
                                if (W[nc]) continue;
                                if (D(nc, tb) == D(c, tb) - 1) { nxt = nc; break; }
                            }
                            if (nxt < 0) break; c = nxt;
                        }
                        if (tgt >= 0) { tasks[k].push_back({2, e.id, tgt, 150.0, tfirst, 0}); continue; }
                    }

                    int post = (nGuards == 0 && v25OwnFlagHome) ? X.myDoorSpot : X.centerCell;
                    nGuards++;
                    tasks[k].push_back({5, post, post, 40.0 - 2.0 * D(X.M[k].cell, post), 0, 0});
                }

#if LBP_25_2P_COUNT_ONLY
                if (LBP_25_2P_HARD_RETREAT && countDisadvantage) {
                    for (int k = 0; k < 3; k++) {
                        if (!X.M[k].alive) continue;
                        if (X.M[k].flag >= 0) { tasks[k].clear(); tasks[k].push_back({0, 0, -1, 1000, 0, 0}); continue; }
                        tasks[k].clear();
                        int post = (k == 0 && v25OwnFlagHome) ? X.myDoorSpot : X.bcells[X.me][4];
                        tasks[k].push_back({5, post, post, 900.0 - 2.0 * D(X.M[k].cell, post), 0, 0});
                    }
                }
#endif
            } else if (bestFlag >= 0) {
                X.phase = 0;
                int runner = -1; double bestR = 1e18;
                for (int k = 0; k < 3; k++) {
                    if (!X.M[k].alive || X.M[k].flag >= 0) continue;
                    double r = D(X.M[k].cell, X.flagCell[bestFlag]) + (g.hp - X.M[k].hp) * 0.02;
                    if (r < bestR) { bestR = r; runner = k; }
                }
                for (int k = 0; k < 3; k++) {
                    if (!X.M[k].alive) { tasks[k].push_back({-1, 0, -1, 0, 0, 0}); continue; }
                    if (k == runner) tasks[k].push_back({1, bestFlag, X.flagCell[bestFlag], 300.0, D(X.M[k].cell, X.flagCell[bestFlag]), 0});
                    else {

                        int threats = 0;
                        for (auto& e : X.E) if (!e.immune && e.flag < 0 && D(e.cell, X.M[runner].cell) <= 5) threats++;
                        double escortVal = P.ESCORT_BASE_DUEL + P.ESCORT_PER_THREAT * min(3, threats);
#if LBP_25_2P_CONTEST_HOLD

                        if (v25CenterContest && X.flagCell[bestFlag] == X.centerCell) escortVal += 35.0;
#endif
                        tasks[k].push_back({3, runner, X.flagCell[bestFlag], escortVal, 0, 0});

                        for (size_t i = 0; i < g.flags.size(); i++) {
                            if (!X.flagAvail[i] || (int)i == bestFlag) continue;
                            int fc = X.flagCell[i];
#if LBP_25_2P_FLAG_SOFT
                            if (v25CenterContest && fc == X.myDoorSpot) continue;
                            if (fc == X.myDoorSpot && !v25Disadvantage) continue;
                            if (fc == enemyDoorSpot && v25Disadvantage) continue;
#endif
                            int eta = D(X.M[k].cell, fc); if (eta >= INF) continue;
                            if (g.flags[i].status == "dropped" && g.flags[i].return_at >= 0 && eta > g.flags[i].return_at - X.T) continue;
                            int ret = distToBase(X, fc, X.me); if (ret >= INF) continue;
                            if (eta + ret > X.remaining) continue;
                            int eEta2 = minETA(fc, false);
                            double pget2 = (eta + 2 < eEta2) ? 0.97 : (eta < eEta2) ? 0.85 : (eta == eEta2) ? 0.45 : 0.15;
                            double pret2 = 0.95 - 0.10 * min(3, nAliveE);
                            double val2 = 100.0 * pget2 * pret2 - X.lambda * (eta + ret);
                            if (!v25Disadvantage) val2 += 40.0;
                            if (fc == X.centerCell) val2 += P.DUEL_CENTER_BIAS * 0.5;
                            if (LBP_25_2P_FLAG_PRIO) val2 += 18.0 * v25FlagPrio((int)i);
                            tasks[k].push_back({1, (int)i, fc, val2, eta, 0});
                        }
                    }
                }
            } else {
                X.phase = 2;
                for (int k = 0; k < 3; k++) {
                    if (!X.M[k].alive) { tasks[k].push_back({-1, 0, -1, 0, 0, 0}); continue; }
                    int post = (v25OwnFlagHome) ? X.myDoorSpot : X.centerCell;
                    tasks[k].push_back({5, post, post, 10.0, 0, 0});
                }
            }
#if LBP_2P_DEFEND_FOCUS

            if (LBP_2P_DEFEND_LOWHP == 3 && defendThreat) {
                for (int k = 0; k < 3; k++) {
                    if (!X.M[k].alive || X.M[k].flag >= 0) continue;
                    int lives = (X.M[k].hp + g.damage - 1) / g.damage;
                    if (lives <= 1) {
                        tasks[k].clear();
                        tasks[k].push_back({0, 0, -1, 1000.0, 0, 0});
                    }
                }
            }
#endif

            if (LBP_25_2P_SOLO_HOME && nAliveMe == 1 && X.myCarrierIdx < 0) {
                int solo = -1;
                for (int k = 0; k < 3; k++) if (X.M[k].alive) { solo = k; break; }
                int ownFlag = -1;
                for (size_t i = 0; i < g.flags.size(); i++) {
                    if (X.flagAvail[i] && X.flagCell[i] == X.myDoorSpot) { ownFlag = (int)i; break; }
                }
                if (solo >= 0 && ownFlag >= 0) {
                    int fc = X.flagCell[ownFlag];
                    tasks[solo].clear();
                    tasks[solo].push_back({1, ownFlag, fc, 1200.0, D(X.M[solo].cell, fc), 0});
                }
            }
        }
    }

    bool v25EnemyWipedNow = (X.nTeams == 2 && nAliveE == 0);
    int v25EnemyRespawnIn = INF;
    for (auto& u : g.units) {
        if (u.team == X.me || u.x >= 0) continue;
        if (u.respawn_at >= X.T) v25EnemyRespawnIn = min(v25EnemyRespawnIn, u.respawn_at - X.T);
    }

    bool v25WipeCampNow = (v25EnemyFlag >= 0) || (v25EnemyRespawnIn <= LBP_25_2P_WIPE_RESPAWN);
    if (LBP_25_2P_WIPE_CAMP && v25EnemyWipedNow && v25WipeCampNow) {

        int campCell = (v25EnemyDoorSpot >= 0) ? v25EnemyDoorSpot : X.bcells[1 - X.me][4];

        int campSpots3Q[3];
        campSpots3Q[0] = campCell;
        campSpots3Q[1] = X.centerCell;
        campSpots3Q[2] = -1;
        for (auto& sp : g.spots) {
            int sc = cid(sp.first, sp.second);
            if (sc != campSpots3Q[0] && sc != campSpots3Q[1]) campSpots3Q[2] = sc;
        }
        bool claimed[16] = {false};
        for (int k = 0; k < 3; k++) tasks[k].clear();
        int camper3Q = 0;
        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive) { tasks[k].push_back({-1, 0, -1, 0, 0, 0}); continue; }
            if (X.M[k].flag >= 0) { tasks[k].push_back({0, 0, -1, 1000.0, 0, 0}); continue; }
            int bF = -1, bFd = INF; double bV = -1e18;
            for (size_t i = 0; i < g.flags.size() && i < 16; i++) {
                if (claimed[i] || !X.flagAvail[i]) continue;
                int fc = X.flagCell[i];
                int d = D(X.M[k].cell, fc);
                if (d >= INF) continue;
                if (g.flags[i].status == "dropped" && g.flags[i].return_at >= 0
                    && g.flags[i].return_at - X.T < d) continue;
                double v = 1100.0 - 2.0 * d;

                if (fc == campCell) v += 60.0;
                else if (fc == X.centerCell) v += 30.0;
                if (v > bV) { bV = v; bF = (int)i; bFd = d; }
            }
            if (bF >= 0) {
                claimed[bF] = true;
                int fc = X.flagCell[bF];
                tasks[k].push_back({1, bF, fc, bV, bFd, 0});
            } else {
                int cc3Q = campSpots3Q[min(camper3Q, 2)];
                camper3Q++;
                tasks[k].push_back({5, cc3Q, cc3Q, 850.0 - 2.0 * D(X.M[k].cell, cc3Q), 0, 0});
            }
        }
    }

    if (X.nTeams == 2) {
        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive || X.M[k].flag >= 0 || X.M[k].cell < 0) continue;
            int focused2 = 0;
            for (auto& e : X.E)
                if (!e.immune && e.cell >= 0 && manh(e.cell, X.M[k].cell) <= 2) focused2++;
            if (focused2 >= 1)
                for (auto& tk : tasks[k]) tk.val *= 0.1;
        }
    }

    if (X.nTeams == 2 && X.squadFocusId >= 0 && X.squadFocusCell >= 0) {
        int mateJ = -1;
        for (int j = 0; j < 3; j++)
            if (X.M[j].alive && X.M[j].cell >= 0 && manh(X.M[j].cell, X.squadFocusCell) <= 2) { mateJ = j; break; }
        if (mateJ >= 0) {
            for (int k = 0; k < 3; k++) {
                if (k == mateJ || !X.M[k].alive || X.M[k].flag >= 0 || X.M[k].cell < 0) continue;
                int ds = manh(X.M[k].cell, X.squadFocusCell);
                if (ds > 8) continue;
                bool onPick = false;
                for (size_t i = 0; i < g.flags.size(); i++)
                    if (X.flagAvail[i] && X.flagCell[i] == X.M[k].cell) { onPick = true; break; }
                if (onPick) continue;
                bool already = false;
                for (auto& tk : tasks[k]) if (tk.type == 2 && tk.key == X.squadFocusId) already = true;
                if (!already) tasks[k].push_back({2, X.squadFocusId, X.squadFocusCell, 1500.0, ds, 0});
            }
        }
    }

    LBPT("2-tasks");
    auto evalCombo = [&](const array<int, 3>& idx) {
        double total = 0;
        vector<pair<int, int>> gets;
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 1) gets.push_back({t.eta, k}); }
        sort(gets.begin(), gets.end());
        map<int, int> cnt;
        for (auto& [eta, k] : gets) {
            auto& t = tasks[k][idx[k]];
            int c = cnt[t.key]++;
            double f = 1.0;
            if (c == 1) f = X.nTeams == 2 ? 0.15 : 0.5;
            if (c >= 2) f = X.nTeams == 2 ? 0.03 : 0.35;
            total += t.val * f;
        }
        map<int, int> inter;
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 2) { int c = inter[t.key]++; if (c == 0) total += t.val; else total += t.val * 0.4; } }
        int esc = 0;
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 3) { total += t.val * (esc == 0 ? 1.0 : 0.5); esc++; } }
        map<int, int> hcnt;
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 4) { int c = hcnt[t.key]++; total += t.val * (c == 0 ? 1.0 : c == 1 ? 0.8 : 0.5); } }
        map<int, int> rcnt;
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 5) { int c = rcnt[t.key]++; total += t.val * (c == 0 ? 1.0 : 0.15); } }

        map<int, int> scnt;
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 6) { int c = scnt[t.key]++; total += t.val * (c == 0 ? 1.0 : 1.0 + 0.15 * c); } }
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 0) total += t.val; }
        return total;
    };
    array<int, 3> best{0, 0, 0}; double bestV = -1e18;
    for (int a = 0; a < (int)tasks[0].size(); a++)
        for (int b = 0; b < (int)tasks[1].size(); b++)
            for (int c = 0; c < (int)tasks[2].size(); c++) {
                array<int, 3> idx = {a, b, c};
                double v = evalCombo(idx);
                if (v > bestV) { bestV = v; best = idx; }
            }

    bool v25Fearless[3] = {false, false, false};
    if (v25PostWin) {
        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive) continue;
            const Task& bt = tasks[k][best[k]];
            if (bt.type == 1 && bt.target == v25EnemyDoorSpot) v25Fearless[k] = true;
        }
    }

    int v25_b = -1, v25_a = -1, v25_c = -1;
    int v25_formA = -1, v25_formB = -1, v25_formC = -1;
    bool v25_combatNear = false;
    bool v25_centerForm = false;

    if (LBP_25_2P_FORMATION && X.nTeams == 2 && X.myCarrierIdx < 0 && enemyCarrierJ < 0
        && nAliveE > 0) {
        double bv = -1e18;
        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive || X.M[k].flag >= 0) continue;
            double tv = tasks[k][best[k]].val;
            if (tv > bv) { bv = tv; v25_b = k; }
        }
        if (v25_b >= 0) {
            int bx = cx(X.M[v25_b].cell), by = cy(X.M[v25_b].cell);
            int nearestEnemy = INF;
            for (auto& e : X.E) if (!e.immune && e.cell != -1)
                nearestEnemy = min(nearestEnemy, manh(X.M[v25_b].cell, e.cell));
            v25_combatNear = (nearestEnemy <= LBP_25_2P_A_FORWARD_DIST);

            int dx = cx(X.centerCell) - bx, dy = cy(X.centerCell) - by;
            int fwd = 0;
            bool forwardX = (abs(dx) >= abs(dy));
            if (forwardX) fwd = (dx > 0 ? 1 : (dx < 0 ? -1 : 1));
            else          fwd = (dy > 0 ? 1 : (dy < 0 ? -1 : 1));

            auto validCell = [&](int x, int y) -> int {
                if (x < 0 || y < 0 || x >= N || y >= N) return -1;
                int c = cid(x, y);
                return W[c] ? -1 : c;
            };
            int upDy = -1;
            if (by == 0 || validCell(bx, by - 1) < 0) upDy = 1;
            int downDy = -upDy;
            int aBase = validCell(bx, by + upDy);
            int cBase = validCell(bx, by + downDy);

            if (LBP_25_2P_CENTER_C_ROW && v25CenterFlag >= 0) {
                int fx = cx(X.centerCell), fy = cy(X.centerCell);
                int col = (bx <= fx) ? fx - 1 : fx + 1;
                if (validCell(col, fy) < 0) col = (bx <= fx) ? fx + 1 : fx - 1;
                if (validCell(col, fy) >= 0) {
                    v25_centerForm = true;
                    v25_formC = validCell(col, fy);
                    v25_formB = validCell(col, fy - 1);
                    v25_formA = validCell(col, fy - 2);
                    if (v25_formA < 0) v25_formA = validCell(col, fy + 1);
                    if (v25_formB < 0) v25_formB = validCell(col, fy + 1);
                }
            }
            int slotA = (v25_centerForm && v25_formA >= 0) ? v25_formA : aBase;
            int slotC = (v25_centerForm && v25_formC >= 0) ? v25_formC : cBase;

            int other[2], no = 0;
            for (int k = 0; k < 3; k++) {
                if (k == v25_b || !X.M[k].alive || X.M[k].flag >= 0) continue;
                other[no++] = k;
            }
            if (no == 1) {

                if (v25_centerForm) v25_c = other[0];
                else                v25_a = other[0];
            } else if (no >= 2) {
                int o0 = other[0], o1 = other[1];
                int d00 = (slotA >= 0) ? manh(X.M[o0].cell, slotA) : INF;
                int d01 = (slotC >= 0) ? manh(X.M[o0].cell, slotC) : INF;
                int d10 = (slotA >= 0) ? manh(X.M[o1].cell, slotA) : INF;
                int d11 = (slotC >= 0) ? manh(X.M[o1].cell, slotC) : INF;
                if (d00 + d11 <= d01 + d10) { v25_a = o0; v25_c = o1; }
                else                        { v25_a = o1; v25_c = o0; }
            }
            if (v25_a >= 0) {
                if (v25_centerForm) {
                    v25_formA = slotA;
                } else {
                    int ax = bx, ay = by + upDy;
                    if (v25_combatNear) {
                        if (forwardX) ax += fwd;
                        else          ay += fwd;
                    }
                    v25_formA = validCell(ax, ay);
                    if (v25_formA < 0) v25_formA = aBase;
                }
            }
            if (v25_c >= 0) v25_formC = slotC;
        }
    }

#if LBP_25_2P_CENTER_C_RUNNER
    if (v25_centerForm && v25CenterFlag >= 0 && v25_c >= 0
        && X.M[v25_c].alive && X.M[v25_c].flag < 0) {
        tasks[v25_c].clear();
        tasks[v25_c].push_back({1, v25CenterFlag, X.centerCell, 1200.0,
                                D(X.M[v25_c].cell, X.centerCell), 0});
        best[v25_c] = 0;
        if (v25_b >= 0 && X.M[v25_b].alive && X.M[v25_b].flag < 0) {
            tasks[v25_b].clear();
            tasks[v25_b].push_back({3, v25_c, X.centerCell, 700.0, 0, 0});
            best[v25_b] = 0;
        }
        if (v25_a >= 0 && X.M[v25_a].alive && X.M[v25_a].flag < 0) {
            tasks[v25_a].clear();
            tasks[v25_a].push_back({3, v25_c, X.centerCell, 650.0, 0, 0});
            best[v25_a] = 0;
        }
    }
#endif

    LBPT("3-combo");

    int nE = (int)X.E.size();
    for (int k = 0; k < 3; k++) for (int mv = 0; mv < 5; mv++) for (int j = 0; j < nE; j++) for (int h = 0; h < X.E[j].pn; h++) {
        int myC, eC; afterCells(X, k, mv, j, h, myC, eC);
        X.hitAfter[k][mv][j][h] = canAtk(myC, eC) ? 1 : 0;
    }
    auto enemyCanHitMe = [&](int j, int h, int k, int mv) { return X.hitAfter[k][mv][j][h] != 0; };

    double advState = 0.0;
    if (X.nTeams == 2 && LBP_2P_ADV_RISK != 0.0) {
        int aliveM = 0, aliveE = 0;
        double hpM = 0, hpE = 0, obj = 0;
        for (int k = 0; k < 3; k++) if (X.M[k].alive) { aliveM++; hpM += X.M[k].hp; }
        for (const auto& e : X.E) if (e.cell >= 0) { aliveE++; hpE += e.hp; }
        if (X.myCarrierIdx >= 0) {
            obj += max(0, 20 - distToBase(X, X.M[X.myCarrierIdx].cell, X.me)) / 20.0;
        }
        for (const auto& e : X.E) {
            if (e.flag >= 0 && e.cell >= 0) {
                obj -= max(0, 20 - distToBase(X, e.cell, e.team)) / 20.0;
            }
        }
        int bestOther = 0;
        for (int t = 0; t < X.nTeams; t++) if (t != X.me) bestOther = max(bestOther, g.score[t]);
        double scoreDiff = g.score[X.me] - bestOther;
        advState = tanh(0.60 * obj + 0.25 * scoreDiff / 10.0
                       + 0.20 * (hpM - hpE) / (3.0 * max(1, g.hp))
                       + 0.40 * (aliveM - aliveE));
    }

    auto incomingCost = [&](const array<int, 3>& mv, double* deathOut) -> double {

        double dp[3][4] = {};
        for (int k = 0; k < 3; k++) dp[k][0] = 1;
        for (int j = 0; j < nE; j++) {
            const EInfo& e = X.E[j];
            if (e.immune || e.flag >= 0) continue;
            double prob[3] = {0, 0, 0};
            for (int h = 0; h < e.pn; h++) {
                double w[3] = {0, 0, 0}, sum = 0;
                for (int k = 0; k < 3; k++) {
                    if (!X.M[k].alive || X.M[k].spawning) continue;
                    if (enemyCanHitMe(j, h, k, mv[k])) {
                        w[k] = X.tw[k];
                        sum += w[k];
                    }
                }

                double others = X.otherW[j][h];
                if (sum <= 0) continue;
                for (int k = 0; k < 3; k++) prob[k] += e.pr[h] * w[k] / (sum + others);
            }
            for (int k = 0; k < 3; k++) {
                double z[4] = {};
                for (int a = 0; a < 4; a++) { z[a] += dp[k][a] * (1 - prob[k]); if (a < 3) z[a + 1] += dp[k][a] * prob[k]; }
                for (int a = 0; a < 4; a++) dp[k][a] = z[a];
            }
        }
        double cost = 0;
        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive) { if (deathOut) deathOut[k] = 0; continue; }
            const MU& m = X.M[k];
            int lives = X.needMy[k];
            double death = 0, dmg = 0;
            for (int a = 0; a < 4; a++) { if (a >= lives) death += dp[k][a]; dmg += dp[k][a] * min(a, lives); }
            if (deathOut) deathOut[k] = death;
            int baseD = distToBase(X, m.cell, X.me);
            double life = (m.flag >= 0) ? P.LIFE_CARRIER + max(0, 12 - baseD) * 0.5 : (baseD < 4 ? P.LIFE_NEARHOME : P.LIFE_PLAIN);
            if ((m.flag >= 0) && baseD < 4) life = P.LIFE_CARRIER_NEAR + max(0, 12 - baseD) * 0.5;
            if (lives <= 1) life += P.LIFE_LOWHP_BONUS * (X.nTeams == 2 ? LBP_2P_LOWHP_MULT : 1.0);
            if (X.nTeams == 2 && LBP_2P_ADV_RISK != 0.0) {
                life *= clamp(1.0 + LBP_2P_ADV_RISK * advState, 0.5, 1.5);
            }

            double deathW = life * death * (v25Fearless[k] ? v25DeathScale : 1.0);
            cost += P.DMG_WEIGHT * (dmg * (m.flag >= 0 ? P.W_HIT_CARRIER : P.W_HIT_PLAIN) + deathW);
        }
        return cost;
    };

    auto evalJointAttack = [&](const int cellAfter[3], const double deathP[3], int outAct[3]) -> double {
        const int NE = (int)X.E.size();
        int opt[3][10]; int nOpt[3] = {0, 0, 0};
        int onFlag[3] = {-1, -1, -1};
        outAct[0] = outAct[1] = outAct[2] = -1;

        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive || X.M[k].spawning || cellAfter[k] < 0 || X.M[k].flag >= 0) {
                opt[k][nOpt[k]++] = -1;
                continue;
            }
            opt[k][nOpt[k]++] = -1;
            for (int j = 0; j < NE && j < 8; j++) {
                const EInfo& e = X.E[j];
                if (e.immune || e.cell < 0) continue;
                bool can = false;
                for (int h = 0; h < e.pn && h < 5; h++) {
                    int myC = cellAfter[k], eC = e.pc[h];
                    if (myC == eC) { myC = X.M[k].cell; eC = e.cell; }
                    if (canAtk(myC, eC)) { can = true; break; }
                }
                if (can && nOpt[k] < 10) opt[k][nOpt[k]++] = j;
            }
            for (size_t i = 0; i < g.flags.size(); i++) {
                if (X.flagAvail[i] && X.flagCell[i] == cellAfter[k]) {
                    onFlag[k] = (int)i;
                    if (P.JA_ALLOW_PICK && nOpt[k] < 10) opt[k][nOpt[k]++] = -2;
                    break;
                }
            }
        }

        const double res = min(1.0, max(0.0, (g.turns - X.T) / 10.0));
        auto pickVal = [&](int k) {
            if (onFlag[k] < 0) return 0.0;
            double base = (distToBase(X, cellAfter[k], X.me) == 0
                           ? P.JA_PICK_BASE : P.JA_PICK_OTHER);
            double survival = max(0.0, 1.0 - P.JA_SURVIVAL * deathP[k]);
            return base * res * survival;
        };

        bool canHit[3][8][5] = {};
        for (int k = 0; k < 3; k++) {
            for (int j = 0; j < NE && j < 8; j++) {
                for (int h = 0; h < X.E[j].pn && h < 5; h++) {
                    int myC = cellAfter[k], eC = X.E[j].pc[h];
                    if (myC == eC) { myC = X.M[k].cell; eC = X.E[j].cell; }
                    canHit[k][j][h] = canAtk(myC, eC);
                }
            }
        }

        auto enemyVal = [&](int j) {
            const EInfo& e = X.E[j];
            int need = (e.hp + g.damage - 1) / g.damage;
            double hitV = P.JA_HIT_VALUE + (e.flag >= 0 ? P.JA_CARRIER_HIT : 0.0);
            double killV = P.JA_KILL_VALUE
                         + (e.flag >= 0 ? P.JA_CARRIER_KILL : 0.0)
                         + (need <= 1 ? P.JA_LOWHP_KILL : 0.0);
            if (e.flag >= 0 && distToBase(X, e.cell, e.team) <= 6) {
                killV += P.JA_DENY;
            }
            double threat = 0.0;
            for (int k = 0; k < 3; k++) {
                if (!X.M[k].alive || X.M[k].spawning || cellAfter[k] < 0) continue;
                for (int h = 0; h < e.pn; h++) {
                    int myC = cellAfter[k], eC = e.pc[h];
                    if (myC == eC) continue;
                    if (!canAtk(eC, myC)) continue;
                    double w = 1.0;
                    if (X.M[k].hp <= g.damage) w += 0.75;
                    if (X.M[k].flag >= 0) w += 0.5;
                    threat += e.pr[h] * w;
                }
            }
            killV += P.JA_THREAT_KILL * threat;
            double flagThreat = 0.0;
            for (size_t i = 0; i < g.flags.size(); i++) {
                if (!X.flagAvail[i]) continue;
                int fd = manh(e.cell, X.flagCell[i]);
                if (fd <= 1) flagThreat = max(flagThreat, fd == 0 ? 1.0 : 0.7);
            }
            hitV += P.JA_FLAG_HIT * flagThreat;
            killV += P.JA_FLAG_KILL * flagThreat;
            if (j == X.squadFocus) {
                hitV += P.JA_FOCUS_HIT;
                killV += P.JA_FOCUS_KILL;
            }
            return pair<double, double>{hitV, killV};
        };

        auto valueOf = [&](const int a[3]) -> double {
            double val = 0.0;
            if (P.JA_ALLOW_PICK) {
                for (int k = 0; k < 3; k++) {
                    if (onFlag[k] < 0) continue;
                    val += (a[k] == -2 ? pickVal(k) : -pickVal(k));
                }
            }
            for (int j = 0; j < NE; j++) {
                const EInfo& e = X.E[j];
                if (e.immune || e.cell < 0) continue;
                int mask = 0;
                for (int k = 0; k < 3; k++) if (a[k] == j) mask |= (1 << k);
                if (!mask) continue;

                int need = (e.hp + g.damage - 1) / g.damage;
                double expHits = 0.0, killP = 0.0;
                for (int h = 0; h < e.pn; h++) {
                    int cnt = 0;
                    for (int k = 0; k < 3; k++) {
                        if ((mask >> k & 1) && canHit[k][j][h]) cnt++;
                    }
                    expHits += e.pr[h] * min(cnt, need);
                    if (cnt >= need) killP += e.pr[h];

                    if (cnt >= 2 && cnt < need) val += P.JA_STACK_BONUS * (cnt - 1) * e.pr[h];
                }
                auto [hitV, killV] = enemyVal(j);
                val += hitV * expHits + killV * killP;
            }
            return val;
        };

        double bestJA = -1e18;
        int bestA[3] = {-1, -1, -1};
        for (int i0 = 0; i0 < nOpt[0]; i0++) for (int i1 = 0; i1 < nOpt[1]; i1++) for (int i2 = 0; i2 < nOpt[2]; i2++) {
            int a[3] = {opt[0][i0], opt[1][i1], opt[2][i2]};
            double val = valueOf(a);
            if (val > bestJA) {
                bestJA = val;
                bestA[0] = a[0]; bestA[1] = a[1]; bestA[2] = a[2];
            }
        }

        int greedyA[3] = {-1, -1, -1};
        for (int k = 0; k < 3; k++) {
            double bestSolo = -1e18;
            for (int i = 0; i < nOpt[k]; i++) {
                int a[3] = {-1, -1, -1};
                a[k] = opt[k][i];
                double v = valueOf(a);
                if (v > bestSolo) { bestSolo = v; greedyA[k] = opt[k][i]; }
            }
        }
        double greedyVal = valueOf(greedyA);

        outAct[0] = bestA[0]; outAct[1] = bestA[1]; outAct[2] = bestA[2];
#if LBP_2P_JOINT_REL
        return bestJA - greedyVal;
#else
        return bestJA;
#endif
    };

    auto gateBlocked = [&](int k, int cell, int myCellAfter[3]) {
        const MU& m = X.M[k];
        if (!m.alive || m.spawning) return false;

        if (X.nTeams == 2 && m.flag < 0) {
            int myBase = X.bcells[X.me][4], eBase = X.bcells[1 - X.me][4];
            bool beyondMid = D(cell, myBase) * 2 > D(myBase, eBase);
            if (beyondMid) {
                int nearestAlly = INF, allyCell = -1;
                bool anyAlly = false;
                for (int j = 0; j < 3; j++)
                    if (j != k && X.M[j].alive && X.M[j].cell >= 0) {
                        int d = manh(cell, X.M[j].cell);
                        if (d < nearestAlly) { nearestAlly = d; allyCell = X.M[j].cell; }
                        anyAlly = true;
                    }
                int nearestEnemy = INF;
                for (auto& e : X.E) if (!e.immune && e.flag < 0 && e.cell >= 0) nearestEnemy = min(nearestEnemy, manh(cell, e.cell));
                if (nearestEnemy <= P.GATE_SOLO_MID_ENEMY_D && nearestAlly >= P.GATE_SOLO_MID_ALLY_D) {

                    if (!anyAlly) return true;
                    if (manh(cell, allyCell) >= manh(m.cell, allyCell)) return true;
                }
            }

            if (X.nTeams == 2 && m.flag < 0) {
                int eNear = INF, eCnt = 0, myCnt = 0, eNearCell = -1;
                for (auto& e : X.E) {
                    if (e.immune || e.cell < 0 || e.flag >= 0) continue;
                    int d = manh(cell, e.cell);
                    if (d < eNear) { eNear = d; eNearCell = e.cell; }
                    if (d <= 5) eCnt++;
                }
                int nearAllyNow = INF;
                for (int j = 0; j < 3; j++)
                    if (j != k && X.M[j].alive && X.M[j].flag < 0 && X.M[j].cell >= 0) {
                        nearAllyNow = min(nearAllyNow, manh(X.M[j].cell, m.cell));
                        if (manh(X.M[j].cell, cell) <= 3) myCnt++;
                    }

                if (eNear <= 2 && (nearAllyNow > 3 || myCnt < eCnt)) {
                    bool oneKill = false;
                    for (auto& e : X.E)
                        if (!e.immune && e.cell >= 0 && e.flag < 0 && manh(cell, e.cell) <= 2
                            && manh(m.cell, e.cell) <= 2
                            && e.hp <= g.damage) oneKill = true;
                    if (!oneKill) {
                        int eNow = INF;
                        for (auto& e : X.E) if (!e.immune && e.cell >= 0) eNow = min(eNow, manh(m.cell, e.cell));

                        if (manh(cell, eNearCell) <= eNow) return true;
                    }
                }

                {
                    int eCross = 0, myCntR = 0;
                    for (auto& e : X.E) {
                        if (e.immune || e.cell < 0 || e.flag >= 0) continue;
                        for (int h = 0; h < e.pn && h < 5; h++)
                            if (e.pr[h] >= 0.15 && e.pc[h] >= 0 && manh(e.pc[h], cell) <= 2) { eCross++; break; }
                    }
                    for (int j = 0; j < 3; j++)
                        if (j != k && X.M[j].alive && X.M[j].flag < 0 && X.M[j].cell >= 0
                            && manh(X.M[j].cell, cell) <= 3) myCntR++;
                    if (eCross >= 2 && myCntR < eCross) {
                        bool oneKillR = false;
                        for (auto& e : X.E)
                            if (!e.immune && e.cell >= 0 && e.flag < 0 && manh(cell, e.cell) <= 2
                                && manh(m.cell, e.cell) <= 2
                                && e.hp <= g.damage) oneKillR = true;
                        if (!oneKillR) {
                            int eNowR = INF, eNearCellR = -1;
                            for (auto& e : X.E)
                                if (!e.immune && e.cell >= 0 && e.flag < 0) {
                                    int dd = manh(m.cell, e.cell);
                                    if (dd < eNowR) { eNowR = dd; eNearCellR = e.cell; }
                                }
                            if (eNearCellR >= 0 && manh(cell, eNearCellR) <= eNowR) return true;
                        }
                    }
                }
            }
        }

        if (X.nTeams >= 3) {
            int eNear = 0, aNear = 0;
            for (auto& e : X.E) if (!e.immune && e.flag < 0 && manh(cell, e.cell) <= 3) eNear++;
            for (int j = 0; j < 3; j++) if (j != k && X.M[j].alive && manh(cell, X.M[j].cell) <= 3) aNear++;
            if (eNear - aNear >= (int)P.GATE_MELEE_CROWD && (m.flag >= 0 ? eNear >= 1 : aNear == 0)) return true;
        }
        return false;
    };

    vector<vector<int>> ctg(3);
    for (int k = 0; k < 3; k++) {
        if (!X.M[k].alive) { ctg[k].clear(); continue; }
        auto& t = tasks[k][best[k]];
        vector<int> targets;
        if (t.type == 0) targets = X.bcells[X.me];
        else if (t.type == 3) {

            int ci = t.key;
            int threatCell = -1, threatD = INF;
            for (auto& e : X.E) if (!e.immune && e.flag < 0) { int d = manh(e.cell, X.M[ci].cell); if (d < threatD) { threatD = d; threatCell = e.cell; } }
            if (threatCell >= 0 && threatD <= 8) targets = {threatCell};
            else targets = {X.M[ci].cell};
        }
        else if (t.type >= 0) targets = {t.target};
        else targets = {X.M[k].cell};
        bool chased = false;
        for (auto& e : X.E) if (!e.immune && e.flag < 0 && manh(e.cell, X.M[k].cell) <= 2) chased = true;
        int K10;
        if (X.M[k].flag >= 0) K10 = chased ? P.K10_CARRIER_CHASED : (X.nTeams == 2 ? P.K10_CARRIER_CALM_DUEL : P.K10_CARRIER_MELEE);
        else K10 = P.K10_OTHER;

        if (X.nTeams != 2 && X.M[k].hp <= g.damage && !chased && X.M[k].flag < 0) K10 += P.K10_LOWHP;

        if (v25Fearless[k]) K10 = min(K10, LBP_25_2P_POSTWIN_K10);

        if (X.M[k].alive && v10_lastDeathCell >= 0 && D(X.M[k].cell, v10_lastDeathCell) < INF) {
            bool isRelay = (X.myCarrierIdx == k);
            if (isRelay && manh(X.M[k].cell, v10_lastDeathCell) <= 6) K10 += (int)P.INTERCEPT_AVOID;
        }

        if (X.M[k].alive && v10_lastDeathCell >= 0 && D(X.M[k].cell, v10_lastDeathCell) < INF
            && X.myCarrierIdx == k && manh(X.M[k].cell, v10_lastDeathCell) <= 6) {
            K10 -= (int)P.INTERCEPT_AVOID;
            int same = 0;
            for (auto& d : v10_myDeaths) if (manh(d.first, v10_lastDeathCell) <= 5) same++;
            if (X.nTeams <= 3 && !X.allTurtle) {
                if (same >= 3) K10 += (int)P.INTERCEPT_AVOID;
                else           K10 += (int)(P.INTERCEPT_AVOID / 2);
            }
        }

        ctg[k] = ctgTo(X, targets, K10, (X.M[k].flag >= 0) ? &thrBase : nullptr);
    }

#if LBP_30_2P_FUTURE
    double futExp[NN];
    for (int c = 0; c < NN; c++) futExp[c] = 0.0;
    bool futOn = false;
    if (LBP_30_2P_FUTURE > 0 && X.nTeams == 2 && X.myCarrierIdx >= 0
        && X.M[X.myCarrierIdx].alive && X.M[X.myCarrierIdx].cell >= 0) {
        const int cc = X.M[X.myCarrierIdx].cell;
        const int d0 = distToBase(X, cc, X.me);
        if (d0 > 0 && d0 < INF) {
            struct FTJ { int j, q[5], need; double pr[5], sup[5]; };
            FTJ th[16];
            int nTh = 0;
            for (auto& e : X.E) {
                if (e.immune || e.flag >= 0 || e.cell < 0 || e.pn <= 0) continue;
                if (nTh >= 16) break;
                FTJ& t = th[nTh++];
                t.j = (int)(&e - &X.E[0]);
                t.need = (e.hp + 33) / 34;
                for (int h = 0; h < 5; h++) {
                    if (h < e.pn) {
                        t.q[h] = e.pc[h]; t.pr[h] = e.pr[h];
                        double sup = 0;
                        for (int s2 = 0; s2 < 3; s2++) {
                            if (s2 == X.myCarrierIdx || !X.M[s2].alive
                                || X.M[s2].flag >= 0 || X.M[s2].cell < 0) continue;
                            int d = D(X.M[s2].cell, e.pc[h]);
                            sup += (d <= 2) ? 1.0 : (d == 3) ? 0.95 : (d == 4) ? 0.60
                                 : (d == 5) ? 0.25 : 0.0;
                        }
                        t.sup[h] = sup;
                    } else { t.q[h] = -1; t.pr[h] = 0.0; t.sup[h] = 0.0; }
                }
            }
            if (nTh > 0) {
                const MU& cm = X.M[X.myCarrierIdx];
                const double myW = (cm.hp <= 34) ? 15.0 : (cm.hp <= 68) ? 8.0 : 5.0;
                futOn = true;
                for (int c = 0; c < NN; c++) {
                    if (W[c]) continue;
                    const int dc = distToBase(X, c, X.me);
                    if (dc > d0 || D(c, cc) > 8) continue;
                    const int tPass = d0 - dc;
                    if (tPass < 0) continue;
                    double ex = 0.0;
                    for (int i = 0; i < nTh; i++) {
                        const FTJ& t = th[i];
                        for (int h = 0; h < 5; h++) {
                            if (t.pr[h] <= 0 || t.q[h] < 0) continue;
                            const int first = max(1, D(t.q[h], c) - 2);
                            const int kill = t.sup[h] > 0.15
                                           ? (int)ceil(t.need / t.sup[h]) : 99;
                            const int last = min(min(d0, kill), 7);
                            const int start = max(first, tPass);
                            if (start <= last)
                                ex += t.pr[h] * 0.85 * myW / (myW + X.otherW[t.j][h]);
                        }
                    }
                    if (ex > 0) futExp[c] = ex;
                }
            }
        }
    }
#endif

#if LBP_31_2P_HORIZON
    double horGain[NN];
    for (int c = 0; c < NN; c++) horGain[c] = 0.0;
    bool horOn = false;
    if (LBP_31_2P_HORIZON > 0 && X.nTeams == 2 && X.myCarrierIdx >= 0
        && X.M[X.myCarrierIdx].alive && X.M[X.myCarrierIdx].cell >= 0) {
        const int cs = X.M[X.myCarrierIdx].cell;
        const int dHs = distToBase(X, cs, X.me);
        if (dHs > 0 && dHs < INF) {
            const int H = 8;
            const int hpc = (X.M[X.myCarrierIdx].hp + 33) / 34;
            struct THZ { int p, delay, immuneTo; double weight, life; };
            THZ th[45];
            int nTh = 0;
            for (auto& u : g.units) {
                if (u.team == X.me) continue;
                THZ t{};
                if (u.x >= 0) {
                    t.p = cid(u.x, u.y); t.delay = 0; t.immuneTo = 0; t.weight = 0.75; t.life = 100.0;
                    if (u.flag >= 0) {
                        t.delay = distToBase(X, t.p, u.team);
                        if (t.delay >= H) continue;
                        int dest = X.bcells[u.team][4];
                        for (int bi = 0; bi < 9; bi++)
                            if (D(X.bcells[u.team][bi], t.p) < D(dest, t.p)) dest = X.bcells[u.team][bi];
                        t.p = dest; t.immuneTo = t.delay; t.weight = 0.6;
                    }
                } else {
                    t.delay = u.respawn_at - X.T;
                    if (t.delay < 0 || t.delay >= H) continue;
                    t.p = X.bcells[u.team][4]; t.immuneTo = t.delay + 1; t.weight = 0.65;
                }
                int d = manh(t.p, cs);
                if (d > H + 8) continue;
                if (t.delay == 0) {
                    t.weight = (d <= 5) ? 0.82 : (d <= 8) ? 0.50 : 0.30;
                    double support = 0;
                    for (int s2 = 0; s2 < 3; s2++)
                        if (X.M[s2].alive && X.M[s2].flag < 0 && X.M[s2].cell >= 0) {
                            int dd = manh(t.p, X.M[s2].cell);
                            support += (dd <= 2) ? 0.9 : (dd == 3) ? 0.4 : 0.0;
                        }
                    if (support > 0.1) t.life = (u.hp + 33) / 34.0 / support + 1.0;
                }
                th[nTh++] = t;
                if (nTh >= 45) break;
            }
            int cells[300];
            double dHc[300];
            int nC = 0, cellIdx[NN];
            for (int c = 0; c < NN; c++) cellIdx[c] = -1;
            for (int c = 0; c < NN; c++) {
                if (W[c]) continue;
                if (manh(c, cs) > H) continue;
                int dh = distToBase(X, c, X.me);
                if (dh >= INF || dh > dHs + H) continue;
                cellIdx[c] = nC; cells[nC] = c; dHc[nC] = dh; nC++;
            }
            if (nC > 0 && hpc >= 1 && hpc <= 3) {
                static double Wdp[10][300][4];
                static double risk[300][4];
                for (int ci = 0; ci < nC; ci++)
                    for (int h = 1; h <= 3; h++)
                        Wdp[H & 1][ci][h] = 24.0 - 1.25 * dHc[ci] + 0.35 * (h - 3);
                for (int t = H - 1; t >= 1; t--) {
                    for (int ci = 0; ci < nC; ci++) {
                        double pr4[4] = {1, 0, 0, 0};
                        int c = cells[ci];
                        for (int i = 0; i < nTh; i++) {
                            const THZ& tz = th[i];
                            int reach = t + 1 - tz.delay;
                            int d = manh(tz.p, c);
                            if (t + 1 <= tz.immuneTo || d > reach + 2) continue;
                            double q = tz.weight * (d <= reach + 1 ? 1.0 : 0.55);
                            if (t + 1 > tz.life) q *= pow(0.55, t + 1 - tz.life);
                            for (int h2 = 3; h2 >= 0; h2--) {
                                double z = pr4[h2] * (1 - q) + (h2 ? pr4[h2 - 1] * q : 0.0);
                                if (h2 == 3) z += pr4[3] * q;
                                pr4[h2] = z;
                            }
                        }
                        for (int h2 = 0; h2 < 4; h2++) risk[ci][h2] = pr4[h2];
                    }
                    for (int ci = 0; ci < nC; ci++) {
                        int c = cells[ci];
                        if (manh(c, cs) > t + 1) continue;
                        if (dHc[ci] == 0) {
                            for (int h = 1; h <= 3; h++) Wdp[t & 1][ci][h] = 24.0;
                            continue;
                        }
                        int x = cx(c), y = cy(c);
                        for (int h = 1; h <= 3; h++) {
                            double best = -1e18;
                            for (int mv = 0; mv < 5; mv++) {
                                static const int DXH[5] = {0, 0, 0, -1, 1}, DYH[5] = {0, -1, 1, 0, 0};
                                int nx = x + DXH[mv], ny = y + DYH[mv];
                                if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
                                int q = cid(nx, ny);
                                int qi = (mv == 0) ? ci : cellIdx[q];
                                if (qi < 0) continue;
                                double z = -0.45;
                                for (int h2 = 0; h2 < 4; h2++)
                                    z += risk[qi][h2] * ((h2 >= h) ? -10.0
                                         : Wdp[(t + 1) & 1][qi][h - h2] - 0.3 * h2);
                                if (z > best) best = z;
                            }
                            if (best > -1e17) Wdp[t & 1][ci][h] = best;
                        }
                    }
                }
                double W1s = (cellIdx[cs] >= 0) ? Wdp[1][cellIdx[cs]][hpc] : 0.0;
                horOn = true;
                for (int ci = 0; ci < nC; ci++) {
                    int c = cells[ci];
                    if (manh(c, cs) > 2) continue;
                    horGain[c] = 0.6 * (dHs - dHc[ci]) + 0.4 * (Wdp[1][ci][hpc] - W1s);
                }
            }
        }
    }
#endif

    bool jointMA = (LBP_2P_JOINT_MA != 0) && P.JA_ENABLED
                && (X.nTeams == 2 ? (X.myCarrierIdx < 0)
                                  : (LBP_25_JOINT_ATTACK_ALL != 0));
    double bestScore = -1e18; array<int, 3> bestMv{0, 0, 0};
    array<int, 3> bestCells{-1, -1, -1};
    int bestJointAct[3] = {-1, -1, -1};
    double bestJointVal = 0.0;
    int gatedCount = 0;
    double comboScore[125];
    double comboFeat[125][40];
    int allMv[125][3], allCell[125][3], allAct[125][3];
    for (int c = 0; c < 125; c++) for (int q = 0; q < 40; q++) comboFeat[c][q] = 0;
    array<int, 3> comboMv;
    int ci = 0;

    double gf[16];
    {
        int myCarry = 0, eCarry = 0;
        for (auto& m : X.M) if (m.alive && m.flag >= 0) myCarry++;
        for (auto& e : X.E) if (e.flag >= 0) eCarry++;
        int nFlagsAvail = 0; bool centerAvail = false, doorAvail = false;
        for (size_t i = 0; i < g.flags.size(); i++) if (X.flagAvail[i]) {
            nFlagsAvail++;
            if (X.flagCell[i] == X.centerCell) centerAvail = true;
            if (X.flagCell[i] == X.myDoorSpot) doorAvail = true;
        }
        int beyond = 0, alive = 0;
        int otherBase = X.bcells[(X.me + 1) % X.nTeams][4];
        for (auto& m : X.M) if (m.alive) {
            alive++;
            if (D(m.cell, X.bcells[X.me][4]) * 2 > D(X.bcells[X.me][4], otherBase)) beyond++;
        }
        int bestOther = 0; for (int t = 0; t < X.nTeams; t++) if (t != X.me) bestOther = max(bestOther, g.score[t]);
        gf[0] = 1.0;
        gf[1] = X.nTeams / 15.0;
        gf[2] = X.T / 400.0;
        gf[3] = min(40, g.score[X.me]) / 40.0;
        gf[4] = min(40, bestOther) / 40.0;
        gf[5] = max(-40, min(40, g.score[X.me] - bestOther)) / 40.0;
        gf[6] = alive / 3.0;
        gf[7] = (double)X.E.size() / 9.0;
        gf[8] = myCarry / 3.0;
        gf[9] = eCarry / 9.0;
        gf[10] = livesMe / 9.0;
        gf[11] = livesE / 9.0;
        gf[12] = nFlagsAvail / 8.0;
        gf[13] = centerAvail ? 1.0 : 0.0;
        gf[14] = doorAvail ? 1.0 : 0.0;
        gf[15] = alive ? beyond / 3.0 : 0.0;
    }

    int goalCell[3], goalD[3];
    for (int k = 0; k < 3; k++) {
        goalCell[k] = -1; goalD[k] = INF;
        if (!X.M[k].alive) continue;
        if (X.M[k].flag >= 0) {
            int best = INF, bc = -1;
            for (int b : X.bcells[X.me]) if (D(X.M[k].cell, b) < best) { best = D(X.M[k].cell, b); bc = b; }
            goalCell[k] = bc; goalD[k] = best;
        } else {
            int best = INF, fc = -1;
            for (size_t i = 0; i < g.flags.size(); i++) if (X.flagAvail[i]) {
                int d = D(X.M[k].cell, X.flagCell[i]);
                if (d < best) { best = d; fc = X.flagCell[i]; }
            }
            goalCell[k] = fc; goalD[k] = best;
        }
    }

    bool advLineActive = false;
    int advRunner = -1, advEscort[2] = {-1, -1}, nAdvEscort = 0;
    int advSide[3] = {0, 0, 0};
    double advUx = 0, advUy = 0;
    if (!LBP_25_2P_FORMATION && P.W_LINE_ADV != 0.0 && X.nTeams == 2 && X.phase == 0 && X.squadMode == 0 && X.myCarrierIdx < 0) {
        int aliveEnemy = 0, aliveMe = 0;
        double ecx = 0, ecy = 0, scx = 0, scy = 0;
        for (auto& e : X.E) if (!e.immune && e.cell != -1) {
            aliveEnemy++; ecx += cx(e.cell); ecy += cy(e.cell);
        }
        for (auto& m : X.M) if (m.alive && m.cell != -1) {
            aliveMe++; scx += cx(m.cell); scy += cy(m.cell);
        }
        if (aliveEnemy > 0 && aliveMe >= 2) {
            ecx /= aliveEnemy; ecy /= aliveEnemy;
            scx /= aliveMe; scy /= aliveMe;
            int nearest = INF;
            for (auto& e : X.E) if (!e.immune && e.cell != -1)
                nearest = min(nearest, (int)(fabs(cx(e.cell) - scx) + fabs(cy(e.cell) - scy)));
            if (nearest <= 12) {
                for (int k = 0; k < 3; k++) if (X.M[k].alive) {
                    int tt = tasks[k][best[k]].type;
                    if (tt == 1 && advRunner < 0) advRunner = k;
                }
                if (advRunner >= 0) {
                    for (int k = 0; k < 3; k++) if (k != advRunner && X.M[k].alive && nAdvEscort < 2) {
                        int tt = tasks[k][best[k]].type;
                        if (tt == 3) advEscort[nAdvEscort++] = k;
                    }
                }
            }
            if (advRunner >= 0 && nAdvEscort > 0) {
                int mb = X.bcells[X.me][4];
                double mx = cx(mb), my = cy(mb);
                double ex = ecx, ey = ecy;
                if (hypot(mx - ex, my - ey) <= 3.0) {
                    int eb = X.bcells[(X.me + 1) % X.nTeams][4];
                    ex = cx(eb); ey = cy(eb);
                }
                double ux = mx - ex, uy = my - ey;
                double ul = hypot(ux, uy);
                if (ul > 0.5) {
                    advLineActive = true;
                    advUx = ux / ul; advUy = uy / ul;
                    if (nAdvEscort == 2) {
                        auto vOf = [&](int k) {
                            double dx = cx(X.M[k].cell) - cx(X.M[advRunner].cell);
                            double dy = cy(X.M[k].cell) - cy(X.M[advRunner].cell);
                            return dx * (-advUy) + dy * advUx;
                        };
                        if (vOf(advEscort[0]) > vOf(advEscort[1])) swap(advEscort[0], advEscort[1]);
                        advSide[advEscort[0]] = -1;
                        advSide[advEscort[1]] = +1;
                    } else {
                        double dx = cx(X.M[advEscort[0]].cell) - cx(X.M[advRunner].cell);
                        double dy = cy(X.M[advEscort[0]].cell) - cy(X.M[advRunner].cell);
                        double v = dx * (-advUy) + dy * advUx;
                        advSide[advEscort[0]] = (v >= 0 ? +1 : -1);
                    }
                }
            }
        }
    }
    for (int m0 = 0; m0 < 5; m0++) for (int m1 = 0; m1 < 5; m1++) for (int m2 = 0; m2 < 5; m2++, ci++) {
        array<int, 3> mv = {m0, m1, m2};
        comboMv = mv;
        double s = 0;
        int cellAfter[3] = {-1, -1, -1};

        int want[3];
        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive) { want[k] = -1; continue; }
            int nx = cx(X.M[k].cell) + DX[mv[k]], ny = cy(X.M[k].cell) + DY[mv[k]];
            if (nx < 0 || ny < 0 || nx >= N || ny >= N) { want[k] = X.M[k].cell; continue; }
            int nc = cid(nx, ny);
            want[k] = W[nc] ? X.M[k].cell : nc;
        }
        int eff[3];
        for (int k = 0; k < 3; k++) eff[k] = (want[k] < 0) ? -1 : want[k];
        for (int it = 0; it < 4; it++) {
            bool changed = false;
            for (int k = 0; k < 3; k++) {
                if (eff[k] < 0 || eff[k] == X.M[k].cell) continue;

                for (int l = 0; l < 3; l++) if (l != k && eff[l] == eff[k]) { eff[k] = X.M[k].cell; changed = true; break; }
                if (eff[k] == X.M[k].cell) continue;

                for (int l = 0; l < 3; l++) if (l != k && X.M[l].alive && X.M[l].cell == eff[k] && eff[l] == X.M[l].cell) { eff[k] = X.M[k].cell; changed = true; break; }
                if (eff[k] == X.M[k].cell) continue;

                for (auto& e : X.E) if (!e.immune && e.cell == eff[k] && e.pStay >= 0.5) { eff[k] = X.M[k].cell; changed = true; break; }
            }
            if (!changed) break;
        }
        for (int k = 0; k < 3; k++) cellAfter[k] = eff[k];

        double deathP[3] = {0, 0, 0};
        double inCost = incomingCost(mv, deathP);

        bool gated = false;
        for (int k = 0; k < 3; k++) {
            if (cellAfter[k] < 0) continue;
            const MU& m = X.M[k];
            double thr = (m.flag >= 0) ? P.GATE_DEATH_CARRIER
                       : ((m.hp + g.damage - 1) / g.damage <= 1 ? P.GATE_DEATH_LOWHP : P.GATE_DEATH_PLAIN);
            if (!v25Fearless[k] && deathP[k] > thr) gated = true;
            if (cellAfter[k] >= 0 && gateBlocked(k, cellAfter[k], cellAfter)) gated = true;
        }
        if (gated) { s -= 1e6; gatedCount++; }

        double jointVal = 0.0;
        int jointAct[3] = {-1, -1, -1};
        if (jointMA && !gated) jointVal = evalJointAttack(cellAfter, deathP, jointAct);

        for (int k = 0; k < 3; k++) {
            if (cellAfter[k] < 0) continue;
            if (ctg[k].empty()) continue;
            double roleW = (X.M[k].flag >= 0) ? 1.3 : 1.0;
            if (ctg[k][cellAfter[k]] >= INT_MAX) s -= 60;
            else s -= roleW * ctg[k][cellAfter[k]] / 10.0;

#if LBP_30_2P_FUTURE
            if (futOn && X.M[k].flag >= 0 && cellAfter[k] >= 0 && futExp[cellAfter[k]] > 0)
                s -= LBP_30_2P_FUTURE * futExp[cellAfter[k]] / 10.0;
#endif

#if LBP_31_2P_HORIZON
            if (horOn && X.M[k].flag >= 0 && cellAfter[k] >= 0 && horGain[cellAfter[k]] != 0.0)
                s += LBP_31_2P_HORIZON * horGain[cellAfter[k]];
#endif

            if (X.nTeams == 2 && X.M[k].flag < 0 && cellAfter[k] >= 0) {
                for (size_t i2 = 0; i2 < g.flags.size(); i2++)
                    if (X.flagAvail[i2] && X.flagCell[i2] == cellAfter[k]) {
                        bool unsafe = false;
                        for (auto& e : X.E)
                            if (!e.immune && e.cell >= 0 && manh(e.cell, cellAfter[k]) <= 3) unsafe = true;
                        if (!unsafe || v25PostWin || v25EnemyWipedNow) s += 8.0;
                        break;
                    }
            }

            if (X.nTeams == 2 && X.M[k].flag < 0 && cellAfter[k] >= 0) {
                int myB2 = X.bcells[X.me][4], eB2 = X.bcells[1 - X.me][4];
                for (int j = 0; j < 3; j++) {
                    if (j == k || !X.M[j].alive || X.M[j].cell < 0) continue;
                    int ne = INF;
                    for (auto& e : X.E) if (!e.immune && e.cell >= 0) ne = min(ne, manh(e.cell, X.M[j].cell));
                    bool mateEngaged = (ne <= 2) || (D(X.M[j].cell, eB2) < D(X.M[j].cell, myB2));
                    if (!mateEngaged) continue;
                    int gap = manh(cellAfter[k], X.M[j].cell);
                    if (gap > 2) s -= 3.0 * (gap - 2);
                }
            }

            if (X.nTeams == 2 && X.M[k].flag < 0 && cellAfter[k] >= 0) {
                int focused = 0;
                for (auto& e : X.E)
                    if (!e.immune && e.cell >= 0 && manh(e.cell, X.M[k].cell) <= 2) focused++;
                if (focused >= 2) {
                    bool safe = true;
                    for (auto& e : X.E)
                        if (!e.immune && e.cell >= 0 && manh(e.cell, cellAfter[k]) <= 3) safe = false;
                    if (safe) s += 6.0;
                    else s -= 3.0;
                }
            }

            if (X.nTeams == 2 && X.M[k].flag < 0 && cellAfter[k] >= 0) {
                for (int j2 = 0; j2 < 3; j2++) {
                    if (j2 == k || !X.M[j2].alive || X.M[j2].cell < 0 || X.M[j2].flag >= 0) continue;
                    if (manh(X.M[j2].cell, cellAfter[k]) > 3) continue;
                    for (auto& e : X.E) {
                        if (e.immune || e.cell < 0 || e.flag >= 0) continue;
                        bool myHit = false, mateHit = false;
                        for (int h = 0; h < e.pn && h < 5; h++) if (canAtk(cellAfter[k], e.pc[h])) myHit = true;
                        for (int h = 0; h < e.pn && h < 5; h++) if (canAtk(X.M[j2].cell, e.pc[h])) mateHit = true;
                        if (myHit && mateHit) s += 5.0;
                    }
                }
            }

            if (X.nTeams == 2 && X.M[k].flag < 0 && v33_lastAtkTarget[k] >= 0 && cellAfter[k] >= 0) {
                int naC = INF;
                for (int j = 0; j < 3; j++)
                    if (j != k && X.M[j].alive && X.M[j].flag < 0 && X.M[j].cell >= 0) naC = min(naC, manh(X.M[j].cell, X.M[k].cell));
                if (naC <= 3) {
                int ltId = v33_lastAtkTarget[k];
                int ej = -1;
                for (int q = 0; q < (int)X.E.size() && q < 8; q++)
                    if (X.E[q].id == ltId) { ej = q; break; }
                if (ej >= 0 && X.E[ej].cell >= 0 && !X.E[ej].immune && X.E[ej].hp < 100) {
                    bool keep = false, had = false;
                    for (int h = 0; h < X.E[ej].pn && h < 5; h++) {
                        if (canAtk(cellAfter[k], X.E[ej].pc[h])) keep = true;
                        if (canAtk(X.M[k].cell, X.E[ej].pc[h])) had = true;
                    }
                    if (keep) s += 3.0;
                    else if (had) s -= 4.0;
                }
                }
            }
        }

        s -= inCost;

        if (LBP_25_2P_FORMATION && X.nTeams == 2 && v25_b >= 0
            && X.myCarrierIdx < 0 && enemyCarrierJ < 0 && nAliveE > 0) {
            for (int k = 0; k < 3; k++) {
                if (cellAfter[k] < 0 || !X.M[k].alive || X.M[k].flag >= 0) continue;
                if (k == v25_b) {
                    if (!v25_centerForm || v25_formB < 0) continue;

                    s -= 0.7 * LBP_25_2P_FORMATION_W * manh(cellAfter[k], v25_formB);
                    continue;
                }
                if (k != v25_a && k != v25_c) continue;
                int ft = (k == v25_a) ? v25_formA : v25_formC;
                if (ft < 0) continue;

                double fw = (v25_centerForm || v25_combatNear)
                          ? LBP_25_2P_FORMATION_W : LBP_25_2P_FORMATION_CALM_W;

                if (tasks[k][best[k]].type == 1 && tasks[k][best[k]].target >= 0
                    && manh(X.M[k].cell, tasks[k][best[k]].target) <= 2) fw *= 0.15;
                s -= fw * manh(cellAfter[k], ft);

                if (k == v25_a && cellAfter[k] == X.M[v25_b].cell) s -= fw * 3.0;
            }
        }

        if (LBP_25_2P_FIRE_SWAP && X.nTeams == 2 && X.myCarrierIdx < 0
            && !v25EnemyWipedNow && X.squadMode != 3 && X.squadFocus >= 0) {
            const EInfo& fe = X.E[X.squadFocus];
            auto v25CanShoot = [&](int c) {
                if (c < 0) return false;
                for (int h = 0; h < fe.pn; h++) if (canAtk(c, fe.pc[h])) return true;
                return false;
            };
            int shootBefore = 0, shootAfter = 0;
            for (int k = 0; k < 3; k++) {
                if (!X.M[k].alive || X.M[k].flag >= 0 || !X.M[k].canAtk || X.M[k].spawning) continue;
                if (cellAfter[k] < 0) continue;
                if (v25CanShoot(X.M[k].cell)) shootBefore++;
                if (v25CanShoot(cellAfter[k])) shootAfter++;
            }
            s += LBP_25_2P_FIRE_COVER_W * (shootAfter - shootBefore);
            for (int a = 0; a < 3; a++) for (int b = a + 1; b < 3; b++) {
                if (!X.M[a].alive || !X.M[b].alive || X.M[a].flag >= 0 || X.M[b].flag >= 0) continue;
                if (cellAfter[a] < 0 || cellAfter[b] < 0) continue;
                bool bothFire = v25CanShoot(cellAfter[a]) && v25CanShoot(cellAfter[b]);
                if (!bothFire) continue;
                bool swap = (cellAfter[a] == X.M[b].cell && cellAfter[b] == X.M[a].cell);
                bool vacate = (cellAfter[b] == X.M[a].cell && cellAfter[a] != X.M[a].cell)
                           || (cellAfter[a] == X.M[b].cell && cellAfter[b] != X.M[b].cell);
                if (swap) s += LBP_25_2P_FIRE_SWAP_W;
                else if (vacate) s += LBP_25_2P_FIRE_SWAP_W * 0.9;
            }
        }

        if (LBP_25_2P_IMPORTANT_RETREAT && X.nTeams == 2 && X.myCarrierIdx < 0 && !v25EnemyWipedNow) {
            for (int k = 0; k < 3; k++) {
                if (!X.M[k].alive || X.M[k].spawning || X.M[k].flag >= 0 || cellAfter[k] < 0) continue;
                int c = cellAfter[k];
                int onFlag = -1;
                for (size_t i = 0; i < g.flags.size(); i++) {
                    if (X.flagAvail[i] && X.flagCell[i] == c) { onFlag = (int)i; break; }
                }
                if (onFlag < 0) continue;
                int fc = X.flagCell[onFlag];
                if (fc != X.centerCell && fc != v25EnemyDoorSpot) continue;
                if (LBP_25_2P_IMPORTANT_STANDING && X.M[k].cell != c) continue;
                bool inRange = false;
                for (auto& e : X.E) {
                    if (e.immune || e.cell < 0) continue;
                    for (int h = 0; h < e.pn; h++) if (canAtk(c, e.pc[h])) { inRange = true; break; }
                    if (inRange) break;
                    if (canAtk(c, e.cell)) { inRange = true; break; }
                }
                if (!inRange) continue;
                int lm = 0, le = 0;
                for (auto& m : X.M) if (m.alive && m.flag < 0
                                         && manh(m.cell, c) <= LBP_25_2P_IMPORTANT_RADIUS) lm++;
                for (auto& e : X.E) if (!e.immune && e.flag < 0 && e.cell >= 0
                                         && manh(e.cell, c) <= LBP_25_2P_IMPORTANT_RADIUS) le++;
                if (lm < le) s -= LBP_25_2P_IMPORTANT_RETREAT_PENALTY;
            }
        }

        if (!LBP_25_2P_FORMATION && P.VERTICAL_WEIGHT > 0.0 && X.nTeams == 2 && X.phase == 2) {
            int mb = X.bcells[X.me][4];
            int bx = cx(mb), by = cy(mb);
            int ux = cx(X.centerCell) - bx, uy = cy(X.centerCell) - by;
            double ulen = sqrt((double)ux * ux + (double)uy * uy);
            if (ulen > 0.5) {
                for (int k = 0; k < 3; k++) {
                    if (cellAfter[k] < 0 || !X.M[k].alive) continue;
                    if (tasks[k][best[k]].type != 5) continue;
                    double perp = fabs((double)ux * (cy(cellAfter[k]) - by)
                                     - (double)uy * (cx(cellAfter[k]) - bx)) / ulen;
                    s += P.VERTICAL_WEIGHT * min(perp, 2.0) / 2.0;
                }
            }
        }

        if (advLineActive && cellAfter[advRunner] >= 0) {
            int rx = cx(cellAfter[advRunner]), ry = cy(cellAfter[advRunner]);
            for (int q = 0; q < nAdvEscort; q++) {
                int k = advEscort[q];
                if (cellAfter[k] < 0 || !X.M[k].alive) continue;
                double dx = cx(cellAfter[k]) - rx, dy = cy(cellAfter[k]) - ry;
                double u = dx * advUx + dy * advUy;
                double v = dx * (-advUy) + dy * advUx;
                double targetV = 2.0 * advSide[k];
                double latErr = fabs(v - targetV);
                double longErr = fabs(u);
                double bonus = max(0.0, 2.0 - min(latErr, 2.0)) - 0.75 * min(longErr, 2.0);
                s += P.W_LINE_ADV * bonus;
            }
        }

        bool focusEnabled = (X.nTeams == 2 && X.squadMode != 3 && X.myCarrierIdx < 0 && X.squadFocus >= 0);
        int focusShooters = 0;
        double focusDamage = 0.0;
        bool focusEligible[3] = {false, false, false};
        if (focusEnabled) {
            for (int k = 0; k < 3; k++) {
                int tt = tasks[k][best[k]].type;
                focusEligible[k] = (tt != 1 && tt != 0 && tt != -1);
            }
        }
        if (!jointMA || LBP_2P_JOINT_KEEP_ATK) {

            for (int k = 0; k < 3; k++) {
                if (cellAfter[k] < 0 || !X.M[k].canAtk || X.M[k].spawning) continue;
                double atk = 0;
                for (int j = 0; j < nE; j++) {
                    const EInfo& e = X.E[j];
                    if (e.immune) continue;
                    double p = 0;
                    for (int h = 0; h < e.pn; h++) {
                        int myC, eC; afterCells(X, k, mv[k], j, h, myC, eC);
                        if (canAtk(cellAfter[k], eC)) p += e.pr[h];
                    }
                    if (p <= 0) continue;
                    if (focusEnabled && focusEligible[k] && j == X.squadFocus) {
                        if (p > 0.25) focusShooters++;
                        focusDamage += (double)g.damage * p;
                    }
                    double v = 2.5 + (e.flag >= 0 ? 2.5 : 0) + (e.hp <= g.damage ? 3.0 : e.hp <= 2 * g.damage ? 1.0 : 0);
                    if (X.myCarrierIdx >= 0 && D(e.cell, X.M[X.myCarrierIdx].cell) <= 3) v += 1.0;
                    atk = max(atk, p * v);
                }
                s += (X.nTeams >= 6 ? 0.5 : 1.0) * atk;
            }
        }
        if (jointMA) {

            double jointMoveW = (X.nTeams == 2) ? LBP_2P_JOINT_W : 0.0;
            if (!LBP_2P_JOINT_REL || jointVal > 0.0) s += jointMoveW * jointVal;
        }
        if (focusEnabled) {
            if (focusShooters >= 2) s += P.W_FOCUS_PAIR;
            if (focusShooters >= 3) s += P.W_FOCUS_TRIPLE;
            bool lethal = (focusShooters >= 3 && X.squadFocusHp <= 3 * g.damage)
                       || (focusShooters >= 2 && X.squadFocusHp <= 2 * g.damage)
                       || (focusDamage >= X.squadFocusHp);
            if (X.squadFocusHp > 0 && lethal) s += P.W_LETHAL;

            int crossPairs = 0;
            for (int a = 0; a < 3; a++) for (int b = a + 1; b < 3; b++) {
                if (!focusEligible[a] || !focusEligible[b]) continue;
                if (cellAfter[a] < 0 || cellAfter[b] < 0) continue;
                if (canAtk(cellAfter[a], X.squadFocusCell) && canAtk(cellAfter[b], X.squadFocusCell)
                    && manh(cellAfter[a], cellAfter[b]) >= 2) crossPairs++;
            }
            s += P.W_CROSS_PAIR * crossPairs;

            int quadMask = 0;
            for (int k = 0; k < 3; k++) {
                if (!focusEligible[k]) continue;
                if (cellAfter[k] < 0 || !canAtk(cellAfter[k], X.squadFocusCell)) continue;
                int vx = cx(cellAfter[k]) - cx(X.squadFocusCell);
                int vy = cy(cellAfter[k]) - cy(X.squadFocusCell);
                int q = (vx >= 0 ? 0 : 1) + (vy >= 0 ? 0 : 2);
                quadMask |= (1 << q);
            }
            int nq = __builtin_popcount((unsigned)quadMask);
            if (nq >= 2) s += P.W_CROSS_ANGLE * (nq - 1);
        }
#ifdef LBP_2P_FOCUS_2V1

        if (X.nTeams == 2 && X.myCarrierIdx < 0 && X.squadFocus >= 0) {
            int lowA = -1;
            for (int j = 0; j < 3; j++) {
                if (!X.M[j].alive || X.M[j].flag >= 0) continue;
                int lives = (X.M[j].hp + g.damage - 1) / g.damage;
                if (lives <= 1 && manh(X.M[j].cell, X.squadFocusCell) <= 4) { lowA = j; break; }
            }
            const EInfo& fe0 = X.E[X.squadFocus];
            bool nearDefSpot = (X.myDoorSpot >= 0 && manh(fe0.cell, X.myDoorSpot) <= 2)
                            || (D(fe0.cell, X.bcells[X.me][4]) <= 5);
            if (lowA >= 0 && nearDefSpot) {
                const EInfo& fe = X.E[X.squadFocus];
                for (int h = 0; h < fe.pn; h++) {
                    if (fe.pr[h] < 0.10) continue;
                    int cnt = 0;
                    for (int k = 0; k < 3; k++) {
                        if (cellAfter[k] < 0 || !X.M[k].alive || X.M[k].spawning) continue;
                        int tt = tasks[k][best[k]].type;
                        if (tt == 0 || tt == 1 || tt == -1) continue;
                        if (canAtk(cellAfter[k], fe.pc[h])) cnt++;
                    }
                    if (cnt >= 2) {
                        double mult = (cnt * g.damage >= fe.hp) ? 1.5 : 1.0;
                        s += LBP_2P_FOCUS_2V1 * fe.pr[h] * (cnt - 1) * mult;
                    }
                }
            }
        }
#endif

        if (X.nTeams == 2 && X.myCarrierIdx < 0 && X.squadMode != 3) {
            for (int k = 0; k < 3; k++) {
                if (cellAfter[k] < 0) continue;
                int nd = INF;
                for (int j = 0; j < 3; j++) if (j != k && cellAfter[j] >= 0)
                    nd = min(nd, manh(cellAfter[k], cellAfter[j]));
                if (nd < INF && nd > P.COHESION_MAX) s -= P.W_COHESION * (nd - P.COHESION_MAX);
            }
        }
#if LBP_2P_GUARD_MOVE

        if (X.nTeams == 2 && X.myCarrierIdx < 0 && X.squadFocus >= 0) {
            int lowA = -1;
            for (int j = 0; j < 3; j++) {
                if (!X.M[j].alive || X.M[j].flag >= 0) continue;
                int lives = (X.M[j].hp + g.damage - 1) / g.damage;
                if (lives <= 1 && manh(X.M[j].cell, X.squadFocusCell) <= 4) { lowA = j; break; }
            }
            if (lowA >= 0) {
                for (int k = 0; k < 3; k++) {
                    if (k == lowA || cellAfter[k] < 0 || !X.M[k].alive || X.M[k].flag >= 0) continue;
                    int curD = manh(X.M[k].cell, X.M[lowA].cell);
                    int newD = manh(cellAfter[k], X.M[lowA].cell);
                    s += LBP_2P_GUARD_NEAR * (curD - newD);
                    if (newD <= 3) {
                        const EInfo& fe = X.E[X.squadFocus];
                        double hitP = 0.0;
                        for (int h = 0; h < fe.pn; h++)
                            if (canAtk(cellAfter[k], fe.pc[h])) hitP += fe.pr[h];
                        s += LBP_2P_GUARD_HIT * hitP;
                    }
                }
            }
        }
#endif

        if (X.nTeams >= 6) {
            for (int a = 0; a < 3; a++) for (int b = a + 1; b < 3; b++)
                if (cellAfter[a] >= 0 && cellAfter[b] >= 0 && manh(cellAfter[a], cellAfter[b]) < 2) s -= 0.8;
        }

        {
            double* F = comboFeat[ci];
            for (int q = 0; q < 16; q++) F[q] = gf[q];
            for (int k = 0; k < 3; k++) {
                double* U = F + 16 + k * 8;
                int c = want[k];
                if (c < 0) { for (int q = 0; q < 8; q++) U[q] = 0; continue; }
                double prog = 0;
                if (goalCell[k] >= 0 && goalD[k] < INF && D(c, goalCell[k]) < INF) prog = goalD[k] - D(c, goalCell[k]);
                int eNear = 0, aNear = 0;
                for (auto& e : X.E) if (!e.immune && manh(c, e.cell) <= 3) eNear++;
                for (int j = 0; j < 3; j++) if (j != k && X.M[j].alive && manh(c, X.M[j].cell) <= 3) aNear++;
                bool onSpot = false;
                for (auto& sp : g.spots) if (cid(sp.first, sp.second) == c) onSpot = true;
                U[0] = prog;
                U[1] = thrG[c];
                U[2] = eNear;
                U[3] = aNear;
                U[4] = X.M[k].hp / 100.0 * (X.M[k].flag >= 0 ? 2.0 : 1.0);
                U[5] = min(16, distToBase(X, c, X.me)) / 16.0;
                U[6] = min(16, manh(c, X.centerCell)) / 16.0;
                U[7] = onSpot ? 1.0 : 0.0;
            }
        }
#if LBP_HAS_NN

        s += (X.nTeams <= 3 ? P.NN_WEIGHT_SMALL : P.NN_WEIGHT_MULTI) * nn_score(comboFeat[ci]);
#endif

        if (X.nTeams == 2 && P.W_XFIRE > 0) {
            for (auto& e : X.E) {
                if (e.immune || e.cell < 0) continue;
                for (int h = 0; h < e.pn && h < 5; h++) {
                    if (e.pr[h] <= 0.001 || e.pc[h] < 0) continue;
                    int cov = 0, axv[3], ayv[3];
                    for (int k = 0; k < 3; k++) {
                        if (cellAfter[k] < 0 || !X.M[k].alive || X.M[k].flag >= 0 || X.M[k].spawning) continue;
                        if (canAtk(cellAfter[k], e.pc[h])) {
                            axv[cov] = cx(cellAfter[k]) - cx(e.pc[h]);
                            ayv[cov] = cy(cellAfter[k]) - cy(e.pc[h]);
                            cov++;
                        }
                    }
                    for (int i = 1; i < cov; i++) {
                        double d0 = sqrt((double)axv[0] * axv[0] + (double)ayv[0] * ayv[0]);
                        double d1 = sqrt((double)axv[i] * axv[i] + (double)ayv[i] * ayv[i]);
                        double dot = (axv[0] * axv[i] + ayv[0] * ayv[i]) / max(1e-6, d0 * d1);
                        if (dot < 0.5) s += P.W_XFIRE * e.pr[h];
                    }
                }
            }
        }
        comboScore[ci] = s;
        allMv[ci][0] = mv[0]; allMv[ci][1] = mv[1]; allMv[ci][2] = mv[2];
        for (int k = 0; k < 3; k++) { allCell[ci][k] = cellAfter[k]; allAct[ci][k] = jointAct[k]; }
        if (s > bestScore) {
            bestScore = s;
            bestMv = mv;
            for (int k = 0; k < 3; k++) {
                bestCells[k] = cellAfter[k];
                bestJointAct[k] = jointAct[k];
            }
            bestJointVal = jointVal;
        }
#ifdef LBP_DEBUG
        
#endif
    }

    

#if LBP_32_2P_SEARCH
    bool contact34 = false;
    if (LBP_34_RECURS > 0)
        for (int k = 0; k < 3 && !contact34; k++) {
            if (!X.M[k].alive) continue;
            for (size_t j = 0; j < X.E.size(); j++)
                if (!X.E[j].immune && X.E[j].cell >= 0 && D(X.M[k].cell, X.E[j].cell) <= 5) { contact34 = true; break; }
        }
    if (LBP_32_2P_SEARCH > 0 && X.nTeams == 2 && g_evalDepth < LBP_34_DEPTH && (LBP_34_RECURS == 0 || contact34)) {
        const int KP = (LBP_32_2P_K < 125) ? LBP_32_2P_K : 125;
        const int KP34 = (g_evalDepth > 0) ? (KP < 3 ? KP : 3) : KP;
        const int SC34 = (g_evalDepth > 0) ? 1 : LBP_34_SCEN;
        if (g_evalDepth == 0)
            g_34deadline = chrono::steady_clock::now() + chrono::milliseconds(LBP_34_SOFT_MS);
        int idxs[125];
        for (int c = 0; c < 125; c++) idxs[c] = c;
        for (int i = 0; i < KP34; i++) {
            int bi = i;
            for (int c = i + 1; c < 125; c++)
                if (comboScore[idxs[c]] > comboScore[idxs[bi]]) bi = c;
            int tmp = idxs[i]; idxs[i] = idxs[bi]; idxs[bi] = tmp;
        }
        const int nE = (int)X.E.size();
        int eNext[8];
        for (int j = 0; j < nE && j < 8; j++) {
            int bmj = 0; double bpj = -1.0;
            for (int h = 0; h < X.E[j].pn && h < 5; h++)
                if (X.E[j].pr[h] > bpj) { bpj = X.E[j].pr[h]; bmj = h; }
            eNext[j] = (bpj > 0 && X.E[j].pc[bmj] >= 0) ? X.E[j].pc[bmj] : X.E[j].cell;
        }
        int eNextS[8];
        for (int j = 0; j < nE && j < 8; j++) {
            unsigned long long h34 = (unsigned long long)X.T * 7577ull + (unsigned long long)X.E[j].id * 31337ull + 293983ull;
            h34 ^= h34 << 13; h34 ^= h34 >> 7; h34 ^= h34 << 17;
            double r34 = (double)(h34 % 100000) / 100000.0;
            int bmj = 0;
            for (int h = 0; h < X.E[j].pn && h < 5; h++) { r34 -= X.E[j].pr[h]; if (r34 <= 0) { bmj = h; break; } }
            eNextS[j] = (X.E[j].pc[bmj] >= 0) ? X.E[j].pc[bmj] : X.E[j].cell;
        }
        const int eMe = 1 - X.me;
        double bestS2 = bestScore;
        int bMv2[3] = {bestMv[0], bestMv[1], bestMv[2]};
        int bCell2[3] = {bestCells[0], bestCells[1], bestCells[2]};
        int bAct2[3] = {bestJointAct[0], bestJointAct[1], bestJointAct[2]};
        for (int ki = 0; ki < KP34; ki++) {
            if (LBP_34_RECURS > 0) {
                if (chrono::steady_clock::now() > g_34deadline) break;
            } else if (v10_ms() > 30.0) break;
            const int c = idxs[ki];
            if (comboScore[c] < -1e5) continue;
            int orig[8], want[8], teamof[8], uh[8], ufl[8];
            int nU = 0, ourIdx[3] = {-1, -1, -1}, eIdx[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
            for (int k = 0; k < 3; k++) {
                if (!X.M[k].alive) continue;
                orig[nU] = X.M[k].cell; want[nU] = allCell[c][k];
                teamof[nU] = X.me; uh[nU] = X.M[k].hp; ufl[nU] = X.M[k].flag;
                ourIdx[k] = nU; nU++;
            }
            for (int j = 0; j < nE && j < 8; j++) {
                if (X.E[j].cell < 0 || X.E[j].immune) continue;
                orig[nU] = X.E[j].cell; want[nU] = -1;
                teamof[nU] = eMe; uh[nU] = X.E[j].hp; ufl[nU] = X.E[j].flag;
                eIdx[j] = nU; nU++;
            }
            int fin[8], dmg[8];
            bool acted[8];
            bool deadU[3], deadE[8];
            int ourCap = 0, eCap = 0;
            auto simRound34 = [&](int scen) {
                const int* eW34 = (scen == 0) ? eNext : eNextS;
                for (int j = 0; j < nE && j < 8; j++) if (eIdx[j] >= 0) want[eIdx[j]] = eW34[j];
                int cnt[NN];
                for (int c2 = 0; c2 < NN; c2++) cnt[c2] = 0;
                for (int i = 0; i < nU; i++) cnt[want[i]]++;
                for (int i = 0; i < nU; i++) {
                    if (cnt[want[i]] > 1) { fin[i] = orig[i]; continue; }
                    bool blocked = false;
                    for (int j2 = 0; j2 < nU; j2++)
                        if (j2 != i && want[i] == orig[j2] && want[j2] == orig[i] && teamof[j2] != teamof[i])
                            blocked = true;
                    fin[i] = blocked ? orig[i] : want[i];
                }
                for (int i = 0; i < 8; i++) { dmg[i] = 0; acted[i] = false; }
                for (int k = 0; k < 3; k++) {
                    int i = ourIdx[k];
                    if (i < 0) continue;
                    int a = allAct[c][k];
                    if (a >= 0) {
                        for (int q = 0; q < nE && q < 8; q++) {
                            if (eIdx[q] < 0 || X.E[q].id != a) continue;
                            int ti = eIdx[q];
                            if (canAtk(fin[i], fin[ti])) { dmg[ti] += 34; acted[i] = true; }
                            break;
                        }
                    }
                }
                for (int q = 0; q < nE && q < 8; q++) {
                    int i = eIdx[q];
                    if (i < 0 || ufl[i] >= 0) continue;
                    bool onflag = false;
                    for (auto& f : g.flags)
                        if (f.x >= 0 && (f.return_at < 0 || f.return_at > X.T) && cid(f.x, f.y) == fin[i]) onflag = true;
                    if (onflag) continue;
                    int tgt = -1; double bv = -1e18;
                    for (int k = 0; k < 3; k++) {
                        int j2 = ourIdx[k];
                        if (j2 < 0) continue;
                        if (!canAtk(fin[i], fin[j2])) continue;
                        double v = (X.M[k].flag >= 0 ? 100.0 : 0.0)
                                 + (X.M[k].hp <= 34 ? 50.0 : X.M[k].hp <= 68 ? 20.0 : 0.0)
                                 - 0.5 * manh(fin[i], fin[j2]);
                        if (v > bv) { bv = v; tgt = j2; }
                    }
                    if (tgt >= 0) { dmg[tgt] += 34; acted[i] = true; }
                }
                for (int k = 0; k < 3; k++) {
                    int i = ourIdx[k];
                    if (i < 0) continue;
                    deadU[k] = (uh[i] - dmg[i] <= 0);
                }
                for (int q = 0; q < nE && q < 8; q++) {
                    int i = eIdx[q];
                    if (i < 0) continue;
                    deadE[q] = (uh[i] - dmg[i] <= 0);
                }
            };
            simRound34(0);
            for (int k = 0; k < 3; k++) {
                int i = ourIdx[k];
                if (i < 0 || deadU[k] || acted[i] || X.M[k].flag >= 0) continue;
                for (auto& f : g.flags)
                    if (f.x >= 0 && (f.status == "home" || f.status == "dropped") && cid(f.x, f.y) == fin[i]) {
                        if (distToBase(X, fin[i], X.me) == 0) ourCap++;
                        break;
                    }
            }
            for (int q = 0; q < nE && q < 8; q++) {
                int i = eIdx[q];
                if (i < 0 || deadE[q] || ufl[i] < 0) continue;
                if (distToBase(X, fin[i], eMe) == 0) eCap++;
            }
            double s2;
#if LBP_34_RECURS
            if (LBP_34_RECURS > 0) {

                int uIdx[24], fIdx[16];
                for (int i2 = 0; i2 < 24; i2++) uIdx[i2] = -1;
                for (int i2 = 0; i2 < (int)g.units.size() && i2 < 24; i2++) uIdx[g.units[i2].id] = i2;
                for (int i2 = 0; i2 < 16; i2++) fIdx[i2] = -1;
                for (int i2 = 0; i2 < (int)g.flags.size() && i2 < 16; i2++) fIdx[g.flags[i2].id] = i2;
                double esum = 0.0, eworst = 1e18;
                for (int scen = 0; scen < SC34; scen++) {
                    if (scen > 0) simRound34(scen);
                    Game g2 = g;
                    g2.turn = X.T + 1;
                    g2.events.clear();

                    for (int k2 = 0; k2 < 3; k2++) {
                        int i = ourIdx[k2];
                        if (i < 0) continue;
                        Unit& u2 = g2.units[uIdx[X.M[k2].id]];
                        if (deadU[k2]) {
                            if (u2.flag >= 0 && fIdx[u2.flag] >= 0) {
                                Flag& f2 = g2.flags[fIdx[u2.flag]];
                                f2.status = "dropped"; f2.holder = -1; f2.x = cx(fin[i]); f2.y = cy(fin[i]); f2.return_at = X.T + g.flag_return;
                            }
                            u2.x = -1; u2.y = -1; u2.hp = 0; u2.flag = -1; u2.respawn_at = X.T + 10;
                        } else {
                            u2.x = cx(fin[i]); u2.y = cy(fin[i]); u2.hp = uh[i] - dmg[i]; u2.respawn_at = -1;
                        }
                    }
                    for (int q2 = 0; q2 < nE && q2 < 8; q2++) {
                        int i = eIdx[q2];
                        if (i < 0) continue;
                        Unit& u2 = g2.units[uIdx[X.E[q2].id]];
                        if (deadE[q2]) {
                            if (u2.flag >= 0 && fIdx[u2.flag] >= 0) {
                                Flag& f2 = g2.flags[fIdx[u2.flag]];
                                f2.status = "dropped"; f2.holder = -1; f2.x = cx(fin[i]); f2.y = cy(fin[i]); f2.return_at = X.T + g.flag_return;
                            }
                            u2.x = -1; u2.y = -1; u2.hp = 0; u2.flag = -1; u2.respawn_at = X.T + 10;
                        } else {
                            u2.x = cx(fin[i]); u2.y = cy(fin[i]); u2.hp = uh[i] - dmg[i]; u2.respawn_at = -1;
                        }
                    }

                    auto reSpot34 = [&](Flag& f2) {
                        int empt[16], ne2 = 0;
                        for (size_t si = 0; si < g.spots.size() && si < 16; si++) {
                            bool occ2 = false;
                            for (auto& f3 : g2.flags)
                                if (f3.status == "home" && f3.x >= 0
                                    && f3.x == g.spots[si].first && f3.y == g.spots[si].second) occ2 = true;
                            if (!occ2) empt[ne2++] = (int)si;
                        }
                        if (ne2 > 0) {
                            unsigned long long h34 = (unsigned long long)X.T * 1315423911ull ^ ((unsigned long long)f2.id * 2654435761ull);
                            h34 ^= h34 << 13; h34 ^= h34 >> 7; h34 ^= h34 << 17;
                            int si = empt[h34 % (unsigned)ne2];
                            f2.x = g.spots[si].first; f2.y = g.spots[si].second;
                        } else { f2.x = -1; f2.y = -1; }
                        f2.status = "home"; f2.holder = -1; f2.return_at = X.T + 1 + g.flag_return;
                    };
                    for (auto& f2 : g2.flags)
                        if (f2.status != "carried" && f2.return_at >= 0 && f2.return_at <= X.T + 1) reSpot34(f2);

                    for (int k2 = 0; k2 < 3; k2++) {
                        int i = ourIdx[k2];
                        if (i < 0 || deadU[k2] || allAct[c][k2] >= 0 || X.M[k2].flag >= 0) continue;
                        for (auto& f2 : g2.flags)
                            if ((f2.status == "home" || f2.status == "dropped") && f2.x >= 0
                                && f2.x == cx(fin[i]) && f2.y == cy(fin[i])) {
                                Unit& u2 = g2.units[uIdx[X.M[k2].id]];
                                u2.flag = f2.id; f2.status = "carried"; f2.x = -1; f2.y = -1; f2.holder = u2.id; f2.return_at = -1;
                                break;
                            }
                    }
                    for (int q2 = 0; q2 < nE && q2 < 8; q2++) {
                        int i = eIdx[q2];
                        if (i < 0 || deadE[q2] || acted[i] || X.E[q2].flag >= 0) continue;
                        for (auto& f2 : g2.flags)
                            if ((f2.status == "home" || f2.status == "dropped") && f2.x >= 0
                                && f2.x == cx(fin[i]) && f2.y == cy(fin[i])) {
                                Unit& u2 = g2.units[uIdx[X.E[q2].id]];
                                u2.flag = f2.id; f2.status = "carried"; f2.x = -1; f2.y = -1; f2.holder = u2.id; f2.return_at = -1;
                                break;
                            }
                    }

                    for (auto& u2 : g2.units) {
                        if (u2.x < 0 || u2.flag < 0 || fIdx[u2.flag] < 0) continue;
                        int t2 = u2.id / 3;
                        if (t2 < 0 || t2 >= (int)g2.score.size()) continue;
                        if (distToBase(X, u2.y * N + u2.x, t2) == 0) {
                            Flag& f2 = g2.flags[fIdx[u2.flag]];
                            u2.flag = -1; f2.status = "cooldown"; f2.holder = -1; f2.x = -1; f2.y = -1; f2.return_at = X.T + g.flag_cooldown;
                            g2.score[t2]++;
                        }
                    }

                    g_evalDepth++;
                    g_evalScore = 0.0;
                    decide(g2);
                    g_evalDepth--;
                    esum += g_evalScore;
                    if (g_evalScore < eworst) eworst = g_evalScore;
                }
                simRound34(0);
                double future34 = esum / (double)SC34;
                s2 = comboScore[c] + (double)LBP_34_GAMMA * (0.8 * future34 + 0.2 * eworst);
            } else
#endif
            {
                double nxt = 0.0;
                for (int k = 0; k < 3; k++) {
                    int i = ourIdx[k];
                    if (i < 0) continue;
                    if (deadU[k]) { nxt -= (X.M[k].flag >= 0) ? P.LIFE_CARRIER : P.LIFE_PLAIN; continue; }
                    if (fin[i] >= 0 && fin[i] < NN)
                        nxt -= 0.5 * thrG[fin[i]] * ((X.M[k].flag >= 0) ? 1.6 : 1.0);
                    if (X.M[k].flag >= 0 && fin[i] >= 0)
                        nxt += 8.0 - 0.5 * distToBase(X, fin[i], X.me);
                    if (uh[i] - dmg[i] <= 34) nxt -= 4.0;
                }
                for (int q = 0; q < nE && q < 8; q++) {
                    int i = eIdx[q];
                    if (i < 0) continue;
                    if (deadE[q]) { nxt += 10.0; continue; }
                    if (ufl[i] >= 0 && fin[i] >= 0)
                        nxt -= 8.0 - 0.5 * distToBase(X, fin[i], eMe);
                }

                double nxtAtk = 0.0, nxtInc = 0.0;
                for (int q2 = 0; q2 < nE && q2 < 8; q2++) {
                    int ei2 = eIdx[q2];
                    if (ei2 < 0 || deadE[q2]) continue;
                    int eHp2 = uh[ei2] - dmg[ei2];
                    if (eHp2 <= 0) continue;
                    for (int k2 = 0; k2 < 3; k2++) {
                        int i2 = ourIdx[k2];
                        if (i2 < 0 || deadU[k2]) continue;
                        if (canAtk(fin[i2], fin[ei2]))
                            nxtAtk += 3.0 + (eHp2 <= 34 ? 6.0 : 0.0);
                    }
                }
                for (int k2 = 0; k2 < 3; k2++) {
                    int i2 = ourIdx[k2];
                    if (i2 < 0 || deadU[k2]) continue;
                    int cov = 0;
                    for (int q2 = 0; q2 < nE && q2 < 8; q2++) {
                        int ei2 = eIdx[q2];
                        if (ei2 < 0 || deadE[q2]) continue;
                        if (canAtk(fin[ei2], fin[i2])) cov++;
                    }
                    if (cov >= 1)
                        nxtInc += 34.0 * min(cov, 3) * 0.6 * ((X.M[k2].flag >= 0) ? 2.0 : 1.0);
                }
                nxt += 0.5 * nxtAtk - 0.35 * nxtInc;
                nxt += 25.0 * ourCap - 25.0 * eCap;
                s2 = comboScore[c] + LBP_32_2P_GAMMA * nxt;
            }
            if (s2 > bestS2) {
                bestS2 = s2;
                for (int k = 0; k < 3; k++) {
                    bMv2[k] = allMv[c][k]; bCell2[k] = allCell[c][k]; bAct2[k] = allAct[c][k];
                }
            }
#if LBP_37_ROLLOUT
            if (g_rolloutDepth == 0 && X.nTeams == 2) {
                static bool rollInit = false;
                if (!rollInit) {
                    rollInit = true;
                    
                }
                if (roll34On && roll34F && (X.T % roll34Stride) == 0) {
                    if (ki == 0) fprintf(roll34F, "RT %d\n", X.T);
                    int v34 = rolloutValue(g, X.me, allMv[c], allAct[c], roll34W);
                    fprintf(roll34F, "RF %d %d", v34, c);
                    for (int q = 0; q < 40; q++) fprintf(roll34F, " %.5f", comboFeat[c][q]);
                    fputc('\n', roll34F);
                    fflush(roll34F);
                }
            }
#endif
        }
        if (bestS2 > bestScore) {
            bestScore = bestS2;
            for (int k = 0; k < 3; k++) {
                bestMv[k] = bMv2[k]; bestCells[k] = bCell2[k]; bestJointAct[k] = bAct2[k];
            }
        }
    }
#endif

    if (g_evalDepth > 0) {
#if LBP_35_VALUE
        if (LBP_35_VALUE > 0 && LBP_HAS_VALUE) {
            double vf34[40]; valueFeatures(X, g, vf34);
            g_evalScore = value_score(vf34);
        } else
#endif
        { g_evalScore = bestScore; }
        return string();
    }

#if LBP_36_VALUE_DUMP

    
#endif

    

    auto v10_fallback = [&]() -> string {
        string o = to_string(X.T);
        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive) { o += " S -"; continue; }
            int bestM = 0, bestC = INT_MAX;
            auto& t = tasks[k][best[k]];
            int tgt = (t.type >= 0) ? t.target : X.M[k].cell;
            if (t.type == 3) tgt = X.M[t.key].cell;
            if (t.type == 0) tgt = X.bcells[X.me][4];
            for (int m = 0; m < 5; m++) {
                int nx = cx(X.M[k].cell) + DX[m], ny = cy(X.M[k].cell) + DY[m];
                int nc = (nx < 0 || ny < 0 || nx >= N || ny >= N) ? X.M[k].cell : (W[cid(nx, ny)] ? X.M[k].cell : cid(nx, ny));
                int c = (tgt >= 0 && tgt < NN) ? D(nc, tgt) : 0;
                if (c < bestC) { bestC = c; bestM = m; }
            }
            o += ' '; o += MVC[bestM]; o += ' ';
            int fcell = -1;
            { int nx = cx(X.M[k].cell) + DX[bestM], ny = cy(X.M[k].cell) + DY[bestM];
              fcell = (nx < 0 || ny < 0 || nx >= N || ny >= N) ? X.M[k].cell : (W[cid(nx, ny)] ? X.M[k].cell : cid(nx, ny)); }
            bool pf = false;
            for (size_t i = 0; i < g.flags.size(); i++) if (X.flagAvail[i] && X.flagCell[i] == fcell && X.M[k].flag < 0) pf = true;
            o += pf ? "P" : "-";
            v10_prevCell[k] = fcell;
        }
        v33_lastAtkTarget[0] = v33_lastAtkTarget[1] = v33_lastAtkTarget[2] = -1;
        return o;
    };
    if (v10_ms() > 35) return v10_fallback();

    LBPT("4-act");
    int chosenCell[3];
    for (int k = 0; k < 3; k++) {
        if (!X.M[k].alive) { chosenCell[k] = -1; continue; }
        if (jointMA && !LBP_2P_JOINT_RAWCELL && bestCells[k] >= 0) {

            chosenCell[k] = bestCells[k];
            continue;
        }
        int nx = cx(X.M[k].cell) + DX[bestMv[k]], ny = cy(X.M[k].cell) + DY[bestMv[k]];
        chosenCell[k] = (nx < 0 || ny < 0 || nx >= N || ny >= N) ? X.M[k].cell : (W[cid(nx, ny)] ? X.M[k].cell : cid(nx, ny));
    }

    for (int k = 0; k < 3; k++) v10_prevCell[k] = chosenCell[k];

    vector<string> act(3, "-");
    map<int, int> dmgOn;
    auto threatAt = [&](int c) {
        double best = 0; int cnt = 0;
        for (auto& e : X.E) { if (e.immune || e.flag >= 0) continue; double p = 0; for (int h = 0; h < e.pn; h++) if (canAtk(c, e.pc[h])) p += e.pr[h]; best = max(best, p); if (p >= 0.45) cnt++; }
        return make_pair(best, cnt);
    };
    for (int k = 0; k < 3; k++) {
        if (!X.M[k].alive) continue;
        const MU& m = X.M[k];
        int c = chosenCell[k];

        if (m.flag >= 0 && !m.spawning) {
            auto [pt, cnt] = threatAt(c);
            int dist = distToBase(X, c, X.me);
            int escorts = 0;
            for (int j = 0; j < 3; j++) if (j != k && X.M[j].alive && X.M[j].canAtk && !X.M[j].spawning && manh(X.M[j].cell, c) <= 3) escorts++;
            bool cornered = (cnt == 1 && dist > (m.hp + g.damage - 1) / g.damage + 1 && (m.hp + g.damage - 1) / g.damage >= 2 && escorts <= 1 && X.remaining > dist + 6);

            bool huntKill = false;
            if (!cornered) {
                int oneHit = 0, otherNear = 0;
                for (auto& e : X.E) {
                    if (e.immune || e.cell < 0 || e.flag >= 0) continue;
                    int d = manh(c, e.cell);
                    if (d <= 3 && e.hp <= g.damage) oneHit++;
                    else if (d <= 3) otherNear++;
                }
                huntKill = (oneHit >= 1 && otherNear == 0 && dist > (m.hp + g.damage - 1) / g.damage + 2
                            && escorts <= 1 && X.remaining > dist + 8);
            }
            if (cornered || huntKill) {
                act[k] = "X";
                if (huntKill) bestMv[k] = 0;
            }
            continue;
        }

        if (m.flag < 0) {
            int onFlag = -1;
            for (size_t i = 0; i < g.flags.size(); i++) if (X.flagAvail[i] && X.flagCell[i] == c) onFlag = (int)i;
            if (onFlag >= 0) {
                auto [pt, cnt] = threatAt(c);
                bool timerOk = !(g.flags[onFlag].status == "dropped" && g.flags[onFlag].return_at >= 0 && g.flags[onFlag].return_at - X.T <= 2);
                bool guard = (pt >= 0.45) && (m.hp + g.damage - 1) / g.damage >= 2 && timerOk && m.canAtk && !m.spawning && X.remaining > distToBase(X, c, X.me) + 4;

                bool dying3T = ((m.hp + g.damage - 1) / g.damage == 1 && pt > 0.20);
                if (!guard && !dying3T) { act[k] = "P"; continue; }
            }
        }
        if (!m.canAtk || m.spawning) continue;
        double bestE = 0; int bestId = -1;
        for (auto& e : X.E) {
            if (e.immune) continue;
            double p = 0;
            for (int h = 0; h < e.pn; h++) if (canAtk(c, e.pc[h])) p += e.pr[h];
            if (p <= 0.001) continue;
            double v = 2.5 + (e.flag >= 0 ? 2.5 : 0) + (e.hp <= g.damage ? 3.0 : e.hp <= 2 * g.damage ? 1.0 : 0);
            if (X.myCarrierIdx >= 0 && D(e.cell, X.M[X.myCarrierIdx].cell) <= 3) v += 1.0;
            int needed = (e.hp + g.damage - 1) / g.damage;
            int already = dmgOn.count(e.id) ? dmgOn[e.id] : 0;
            if (already + 1 >= needed) v += 3.0;
            else if (already > 0) v += 0.8;
            if (p * v > bestE) { bestE = p * v; bestId = e.id; }
        }
        if (bestId >= 0) { act[k] = to_string(bestId); dmgOn[bestId]++; }
        else if (m.flag < 0) {
            for (auto& sp : g.spots) if (cid(sp.first, sp.second) == c) {
                bool av2 = false;
                for (size_t f2 = 0; f2 < g.flags.size(); f2++)
                    if (X.flagAvail[f2] && X.flagCell[f2] == c) av2 = true;
                auto [pt3T, cnt3T] = threatAt(c);
                if (av2 && !((m.hp + g.damage - 1) / g.damage == 1 && pt3T > 0.20)) act[k] = "P";
            }
        }
    }

    bool jointActOK = jointMA && LBP_2P_JOINT_ACT
                   && (!LBP_2P_JOINT_REL || bestJointVal > LBP_2P_JOINT_ACT_MIN);
    if (jointActOK) {
        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive || X.M[k].flag >= 0) continue;
            bool wasPick = (act[k] == "P");

            bool killPick = (bestJointAct[k] >= 0 && bestJointAct[k] < (int)X.E.size()
                             && X.E[bestJointAct[k]].hp <= g.damage && !X.E[bestJointAct[k]].immune);

            if (P.JA_PROTECT_PICK && !P.JA_ALLOW_PICK && wasPick && bestJointAct[k] != -2 && !killPick) continue;
            if (bestJointAct[k] == -2) {

                auto [ptW, cntW] = threatAt(bestCells[k]);
                int lvW = (X.M[k].hp + g.damage - 1) / g.damage;
                bool riskyW = (ptW >= 0.45) || (lvW == 1 && ptW > 0.20);
                if (!riskyW) {
                    act[k] = "P";
                } else {
                    int btW = -1; double bvW = 0;
                    for (auto& e : X.E) {
                        if (e.immune || e.cell < 0) continue;
                        double pw = 0;
                        for (int h = 0; h < e.pn; h++) if (canAtk(bestCells[k], e.pc[h])) pw += e.pr[h];
                        if (pw <= 0.001) continue;
                        double vw = 2.5 + (e.flag >= 0 ? 2.5 : 0) + (e.hp <= g.damage ? 3.0 : 0);
                        if (pw * vw > bvW) { bvW = pw * vw; btW = e.id; }
                    }
                    if (btW >= 0) act[k] = to_string(btW);
                    else if (wasPick) act[k] = "P";
                    else act[k] = "-";
                }
            } else if (bestJointAct[k] >= 0 && bestJointAct[k] < (int)X.E.size()) {
                act[k] = to_string(X.E[bestJointAct[k]].id);
            } else {
                bool onF = false;
                for (size_t i = 0; i < g.flags.size(); i++) {
                    if (X.flagAvail[i] && X.flagCell[i] == bestCells[k]) { onF = true; break; }
                }
                if (!P.JA_ALLOW_PICK && onF && wasPick) act[k] = "P";
                else act[k] = "-";
            }
        }
#ifdef LBP_DEBUG
        
#endif
    }

#if LBP_33_RETALIATE
    if (LBP_33_RETALIATE > 0 && X.nTeams == 2) {

        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive || X.M[k].flag >= 0) continue;
            if (act[k] != "-") continue;
            int cc = (chosenCell[k] >= 0) ? chosenCell[k] : bestCells[k];
            if (cc < 0) continue;
            int bt = -1; double bv = -1e18;
            for (int j = 0; j < (int)X.E.size() && j < 8; j++) {
                if (X.E[j].cell < 0 || X.E[j].immune) continue;
                bool can = false;
                for (int h = 0; h < X.E[j].pn && h < 5; h++)
                    if (canAtk(cc, X.E[j].pc[h])) { can = true; break; }
                if (!can) continue;
                double v = (X.E[j].flag >= 0 ? 100.0 : 0.0)
                         + (X.E[j].hp <= 34 ? 50.0 : X.E[j].hp <= 68 ? 20.0 : 0.0)
                         - 0.5 * manh(cc, X.E[j].cell);
                if (v > bv) { bv = v; bt = j; }
            }
            if (bt >= 0) act[k] = to_string(X.E[bt].id);
        }
    }
#endif

    if (!jointMA && P.JA_ENABLED && X.nTeams == 2 && X.myCarrierIdx < 0) {
        const int NE = (int)X.E.size();
        vector<int> opt[3];
        int onFlag[3] = {-1, -1, -1};

        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive || X.M[k].spawning) { opt[k] = {-1}; continue; }
            opt[k].push_back(-1);

            for (int j = 0; j < NE; j++) {
                const EInfo& e = X.E[j];
                if (e.immune || e.cell < 0) continue;
                bool can = false;
                for (int h = 0; h < e.pn; h++) {
                    if (canAtk(chosenCell[k], e.pc[h])) { can = true; break; }
                }
                if (can) opt[k].push_back(j);
            }

            for (size_t i = 0; i < g.flags.size(); i++) {
                if (X.flagAvail[i] && X.flagCell[i] == chosenCell[k]) {
                    onFlag[k] = (int)i;
                    if (P.JA_ALLOW_PICK) opt[k].push_back(-2);
                    break;
                }
            }
        }

        double deathP[3] = {0, 0, 0};
        incomingCost(bestMv, deathP);
        const double res = min(1.0, max(0.0, (g.turns - X.T) / 10.0));
        auto pickVal = [&](int k) {
            if (onFlag[k] < 0) return 0.0;
            double base = (distToBase(X, chosenCell[k], X.me) == 0
                           ? P.JA_PICK_BASE : P.JA_PICK_OTHER);
            double survival = max(0.0, 1.0 - P.JA_SURVIVAL * deathP[k]);
            return base * res * survival;
        };

        bool canHit[3][8][5] = {};
        for (int k = 0; k < 3; k++) {
            for (int j = 0; j < NE && j < 8; j++) {
                for (int h = 0; h < X.E[j].pn && h < 5; h++) {
                    canHit[k][j][h] = canAtk(chosenCell[k], X.E[j].pc[h]);
                }
            }
        }

        auto enemyVal = [&](int j) {
            const EInfo& e = X.E[j];
            int need = (e.hp + g.damage - 1) / g.damage;
            double hitV = P.JA_HIT_VALUE + (e.flag >= 0 ? P.JA_CARRIER_HIT : 0.0);
            double killV = P.JA_KILL_VALUE
                         + (e.flag >= 0 ? P.JA_CARRIER_KILL : 0.0)
                         + (need <= 1 ? P.JA_LOWHP_KILL : 0.0);
            if (e.flag >= 0 && distToBase(X, e.cell, e.team) <= 6) {
                killV += P.JA_DENY;
            }

            double threat = 0.0;
            for (int k = 0; k < 3; k++) {
                if (!X.M[k].alive || X.M[k].spawning) continue;
                for (int h = 0; h < e.pn; h++) {
                    if (!canAtk(e.pc[h], chosenCell[k])) continue;
                    double w = 1.0;
                    if (X.M[k].hp <= g.damage) w += 0.75;
                    if (X.M[k].flag >= 0) w += 0.5;
                    threat += e.pr[h] * w;
                }
            }
            killV += P.JA_THREAT_KILL * threat;

            double flagThreat = 0.0;
            for (size_t i = 0; i < g.flags.size(); i++) {
                if (!X.flagAvail[i]) continue;
                int fd = manh(e.cell, X.flagCell[i]);
                if (fd <= 1) flagThreat = max(flagThreat, fd == 0 ? 1.0 : 0.7);
            }
            hitV += P.JA_FLAG_HIT * flagThreat;
            killV += P.JA_FLAG_KILL * flagThreat;
            if (j == X.squadFocus) {
                hitV += P.JA_FOCUS_HIT;
                killV += P.JA_FOCUS_KILL;
            }
            return pair<double, double>{hitV, killV};
        };

        double bestJA = -1e18;
        int bestA[3] = {-1, -1, -1};

        for (int a0 : opt[0]) for (int a1 : opt[1]) for (int a2 : opt[2]) {
            int a[3] = {a0, a1, a2};
            double val = 0.0;

            if (P.JA_ALLOW_PICK) {
                for (int k = 0; k < 3; k++) {
                    if (onFlag[k] < 0) continue;
                    val += (a[k] == -2 ? pickVal(k) : -pickVal(k));
                }
            }

            for (int j = 0; j < NE; j++) {
                const EInfo& e = X.E[j];
                if (e.immune || e.cell < 0) continue;
                int mask = 0;
                for (int k = 0; k < 3; k++) if (a[k] == j) mask |= (1 << k);
                if (!mask) continue;

                int need = (e.hp + g.damage - 1) / g.damage;
                double expHits = 0.0, killP = 0.0;
                for (int h = 0; h < e.pn; h++) {
                    int cnt = 0;
                    for (int k = 0; k < 3; k++) {
                        if ((mask >> k & 1) && canHit[k][j][h]) cnt++;
                    }
                    expHits += e.pr[h] * min(cnt, need);
                    if (cnt >= need) killP += e.pr[h];

                    if (cnt >= 2) val += P.JA_STACK_BONUS * (cnt - 1) * e.pr[h];
                }
                auto [hitV, killV] = enemyVal(j);
                val += hitV * expHits + killV * killP;
            }

            if (val > bestJA) {
                bestJA = val;
                bestA[0] = a0; bestA[1] = a1; bestA[2] = a2;
            }
        }

        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive) continue;

            if (P.JA_PROTECT_PICK && !P.JA_ALLOW_PICK && onFlag[k] >= 0 && act[k] == "P") continue;
            if (bestA[k] == -2) act[k] = "P";
            else if (bestA[k] >= 0) act[k] = to_string(X.E[bestA[k]].id);
            else if (P.JA_ALLOW_PICK || act[k] != "P") act[k] = "-";

        }

#ifdef LBP_DEBUG
        
#endif
    }

#if (LBP_2P_ADV_LOG || LBP_2P_ADV)
    if (X.nTeams == 2) {
        int aliveM = 0, aliveE = 0, lifeM = 0, lifeE = 0;
        double hpM = 0, hpE = 0, forceM = 0, forceE = 0;
        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive) continue;
            aliveM++;
            hpM += X.M[k].hp;
            lifeM += (X.M[k].hp + g.damage - 1) / g.damage;
        }
        for (const auto& e : X.E) {
            if (e.cell < 0) continue;
            aliveE++;
            hpE += e.hp;
            lifeE += (e.hp + g.damage - 1) / g.damage;
        }

        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive) continue;
            for (const auto& e : X.E) {
                if (e.immune || e.cell < 0) continue;
                double p = 0;
                for (int h = 0; h < e.pn; h++) {
                    int myC = chosenCell[k], eC = e.pc[h];
                    if (myC == eC) continue;
                    if (canAtk(myC, eC)) p += e.pr[h];
                }
                forceM += p * (1.0 + (e.hp <= g.damage ? 0.5 : 0.0) + (e.flag >= 0 ? 1.0 : 0.0));
            }
        }
        for (const auto& e : X.E) {
            if (e.immune || e.cell < 0) continue;
            for (int h = 0; h < e.pn; h++) {
                for (int k = 0; k < 3; k++) {
                    if (!X.M[k].alive) continue;
                    int myC = chosenCell[k], eC = e.pc[h];
                    if (myC == eC) continue;
                    if (canAtk(eC, myC)) {
                        forceE += e.pr[h] * (1.0 + (X.M[k].hp <= g.damage ? 0.5 : 0.0)
                                                  + (X.M[k].flag >= 0 ? 1.0 : 0.0));
                    }
                }
            }
        }
        double obj = 0.0;
        if (X.myCarrierIdx >= 0) {
            obj += max(0, 20 - distToBase(X, X.M[X.myCarrierIdx].cell, X.me)) / 20.0;
        }
        for (const auto& e : X.E) {
            if (e.flag >= 0 && e.cell >= 0) {
                obj -= max(0, 20 - distToBase(X, e.cell, e.team)) / 20.0;
            }
        }
        int bestOther = 0;
        for (int t = 0; t < X.nTeams; t++) if (t != X.me) bestOther = max(bestOther, g.score[t]);
        double scoreDiff = g.score[X.me] - bestOther;
        double force = (forceM - forceE) / 3.0;
        double tempo = (aliveM - aliveE) * 0.5 + (lifeM - lifeE) * 0.2;
        double advScore = tanh(0.55 * tempo + 0.30 * force + 0.60 * obj
                             + 0.25 * scoreDiff / 10.0
                             + 0.20 * (hpM - hpE) / (3.0 * max(1, g.hp)));
        fprintf(stderr,
                "ADV T%d ph%d adv%.3f force%.2f obj%.2f life%d/%d alive%d/%d hp%.0f/%.0f score%+d\n",
                X.T, X.phase, advScore, force, obj, lifeM, lifeE, aliveM, aliveE,
                hpM, hpE, scoreDiff);
    }
#endif

    if (X.nTeams == 2 && X.myCarrierIdx < 0) {
        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive || X.M[k].spawning || X.M[k].flag >= 0 || chosenCell[k] < 0) continue;
            int c = chosenCell[k];
            int onFlag = -1;
            for (size_t i = 0; i < g.flags.size(); i++) {
                if (X.flagAvail[i] && X.flagCell[i] == c) { onFlag = (int)i; break; }
            }
            if (onFlag < 0) continue;
            int fc = X.flagCell[onFlag];
            bool important = (fc == X.centerCell || fc == v25EnemyDoorSpot);

            bool inRange = false;
            for (auto& e : X.E) {
                if (e.immune || e.cell < 0) continue;
                for (int h = 0; h < e.pn; h++) {
                    if (canAtk(c, e.pc[h])) { inRange = true; break; }
                }
                if (inRange) break;
                if (canAtk(c, e.cell)) { inRange = true; break; }
            }

            if (LBP_25_2P_PROTECT_PICK && !inRange) { act[k] = "P"; continue; }
            if (!important) continue;
            if (!LBP_25_2P_IMPORTANT_FIGHT && !LBP_25_2P_IMPORTANT_RETREAT) continue;
            if (LBP_25_2P_IMPORTANT_STANDING && X.M[k].cell != c) continue;

            int lm = 0, le = 0;
            for (auto& m : X.M) if (m.alive && m.flag < 0
                                     && manh(m.cell, c) <= LBP_25_2P_IMPORTANT_RADIUS) lm++;
            for (auto& e : X.E) if (!e.immune && e.flag < 0 && e.cell >= 0
                                     && manh(e.cell, c) <= LBP_25_2P_IMPORTANT_RADIUS) le++;
            if (lm >= le) {
                if (!LBP_25_2P_IMPORTANT_FIGHT) continue;

                if (act[k] == "P") {
                    double bestAtkV = 0.0; int bestAtkId = -1;
                    for (auto& e : X.E) {
                        if (e.immune || e.cell < 0) continue;
                        double p = 0;
                        for (int h = 0; h < e.pn; h++) if (canAtk(c, e.pc[h])) p += e.pr[h];
                        if (p <= 0.001) continue;
                        double v = 2.5 + (e.flag >= 0 ? 2.5 : 0.0)
                                 + (e.hp <= g.damage ? 3.0 : e.hp <= 2 * g.damage ? 1.0 : 0.0);
                        if (p * v > bestAtkV) { bestAtkV = p * v; bestAtkId = e.id; }
                    }
                    act[k] = (bestAtkId >= 0) ? to_string(bestAtkId) : "-";
                }
            } else if (LBP_25_2P_IMPORTANT_RETREAT) {

                act[k] = "-";
            }
        }
    }

#if LBP_33_RETALIATE
    if (LBP_33_RETALIATE > 0 && X.nTeams == 2) {
        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive || X.M[k].flag >= 0) continue;
            int nearAllyR = INF;
            for (int j = 0; j < 3; j++)
                if (j != k && X.M[j].alive && X.M[j].flag < 0 && X.M[j].cell >= 0)
                    nearAllyR = min(nearAllyR, manh(X.M[j].cell, X.M[k].cell));
            if (nearAllyR > 3) continue;
            if (act[k] != "-") continue;
            int cc = (chosenCell[k] >= 0) ? chosenCell[k] : bestCells[k];
            if (cc < 0) continue;
            int bt = -1; double bv = -1e18;
            for (int j = 0; j < (int)X.E.size() && j < 8; j++) {
                if (X.E[j].cell < 0 || X.E[j].immune) continue;
                bool can = false;
                for (int h = 0; h < X.E[j].pn && h < 5; h++)
                    if (canAtk(cc, X.E[j].pc[h])) { can = true; break; }
                if (!can) continue;
                double v = (X.E[j].flag >= 0 ? 100.0 : 0.0)
                         + (X.E[j].hp <= 34 ? 50.0 : X.E[j].hp <= 68 ? 20.0 : 0.0)
                         - 0.5 * manh(cc, X.E[j].cell);
                if (v > bv) { bv = v; bt = j; }
            }
            if (bt >= 0) act[k] = to_string(X.E[bt].id);
        }
    }
#endif

    if (X.nTeams == 2) {
        for (int k = 0; k < 3; k++) {
            if (!X.M[k].alive || X.M[k].flag >= 0) continue;
            if (act[k] == "P") continue;
            int cc = (chosenCell[k] >= 0) ? chosenCell[k] : bestCells[k];
            if (cc < 0) continue;
            bool onFlag = false;
            for (size_t i = 0; i < g.flags.size(); i++)
                if (X.flagAvail[i] && X.flagCell[i] == cc) { onFlag = true; break; }
            if (!onFlag) continue;
            bool unsafe = false;
            for (auto& e : X.E)
                if (!e.immune && e.cell >= 0 && manh(e.cell, cc) <= 2) unsafe = true;
            if (!unsafe || v25PostWin || v25EnemyWipedNow) act[k] = "P";
        }
    }

    LBPT("5-out");
    string out = to_string(X.T);
    for (int k = 0; k < 3; k++) {
        if (!X.M[k].alive) { out += " S -"; continue; }
        out += ' '; out += MVC[bestMv[k]]; out += ' '; out += act[k];
    }

    

    for (int k = 0; k < 3; k++) {
        v33_lastAtkTarget[k] = -1;
        if (!X.M[k].alive) continue;
        const char* cs = act[k].c_str();
        char* endp = nullptr;
        long idv = strtol(cs, &endp, 10);
        if (endp != cs && *endp == '\0' && idv >= 0) v33_lastAtkTarget[k] = (int)idv;
    }
    return out;
}

}

static string decideDispatch(const Game& g) {
    if (g.teams == 2) return v10::decide(g);

    return bot_final::decide(g);
}

int main() {
    ios::sync_with_stdio(false);
    Game g;
    while (read_turn(g)) {
        cout << decideDispatch(g) << endl;
    }
}
