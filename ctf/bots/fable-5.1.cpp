// 多阵营夺旗 bot
// 思路：全图预计算最短路；每回合预测敌人下一步分布；用带危险权重的 Dijkstra 规划路线；
// 枚举 3 个角色的任务组合（取旗/回家/截杀携旗者/护送/猎杀/巡游）取总价值最大；
// 最后按预测命中概率选攻击目标并集火。
#include <bits/stdc++.h>
using namespace std;

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

// ---------------- 预计算 ----------------
static int N = 0, NN = 0;
static vector<char> W;            // 墙
static vector<int16_t> Dall;      // 全图两两最短路
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

// ---------------- 每回合状态 ----------------
struct EInfo {
    int id, team, cell, hp, flag; bool immune;
    vector<pair<int, double>> pred; double pStay;
};
struct MU { int id, cell, hp, flag; bool alive, spawning, canAtk; };
struct Task { int type; int key; int target; double val; int eta; int hits; };
// type: 0 RETURN, 1 GET, 2 INTERCEPT, 3 ESCORT, 4 HUNT, 5 ROAM, -1 NONE

static double LAMBDA = 2.5;
inline int lives(int hp, int dmg) { return hp <= 0 ? 0 : (hp + dmg - 1) / dmg; }

string decide(const Game& g) {
    if (!pre) precompute(g);
    const int me = g.me, T = g.turn, nTeams = g.teams;
    const int remaining = g.turns - T + 1;
    const int centerCell = cid(N / 2, N / 2);
    LAMBDA = (nTeams == 2) ? 3.0 : (nTeams <= 5) ? 1.5 : 0.8;

    auto baseCells = [&](int team) {
        vector<int> v; auto [bx, by] = g.bases[team];
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) v.push_back(cid(bx + dx, by + dy));
        return v;
    };
    vector<vector<int>> bcells(nTeams);
    for (int t = 0; t < nTeams; t++) bcells[t] = baseCells(t);
    auto distToBase = [&](int c, int team) {
        int best = INF; for (int b : bcells[team]) best = min(best, D(c, b)); return best;
    };
    auto nearestBaseCell = [&](int c, int team) {
        int best = INF, bc = bcells[team][4];
        for (int b : bcells[team]) if (D(c, b) < best) { best = D(c, b); bc = b; }
        return bc;
    };

    // 占用
    vector<int> occ(NN, -1);
    for (auto& u : g.units) if (u.x >= 0) occ[cid(u.x, u.y)] = u.id;

    // 我方重生顺序
    vector<int> spawnOrder = bcells[me];
    {
        auto [bx, by] = g.bases[me];
        sort(spawnOrder.begin(), spawnOrder.end(), [&](int a, int b) {
            int da = manh(a, centerCell), db = manh(b, centerCell);
            if (da != db) return da < db;
            int ka = abs(cx(a) - bx) + 2 * abs(cy(a) - by), kb = abs(cx(b) - bx) + 2 * abs(cy(b) - by);
            return ka < kb;
        });
    }

    // 我方角色
    vector<MU> M(3);
    for (int k = 0; k < 3; k++) {
        int uid = 3 * me + k; const Unit* u = nullptr;
        for (auto& x : g.units) if (x.id == uid) u = &x;
        MU m; m.id = uid; m.cell = -1; m.hp = 0; m.flag = -1; m.alive = false; m.spawning = false; m.canAtk = false;
        if (u) {
            if (u->x >= 0) { m.cell = cid(u->x, u->y); m.hp = u->hp; m.flag = u->flag; m.alive = true; m.canAtk = (u->flag < 0); }
            else if (u->respawn_at == T) {
                for (int c : spawnOrder) if (occ[c] < 0) { m.cell = c; occ[c] = uid; break; }
                if (m.cell >= 0) { m.alive = true; m.spawning = true; m.hp = g.hp; m.canAtk = false; }
            }
        }
        M[k] = m;
    }
    int myCarrierIdx = -1;
    for (int k = 0; k < 3; k++) if (M[k].alive && M[k].flag >= 0) myCarrierIdx = k;

    // 敌人
    vector<EInfo> E;
    for (auto& u : g.units) {
        if (u.team == me) continue;
        EInfo e; e.id = u.id; e.team = u.team; e.hp = u.hp; e.flag = u.flag; e.immune = false; e.pStay = 1;
        if (u.x >= 0) e.cell = cid(u.x, u.y);
        else if (u.respawn_at == T) { e.cell = bcells[u.team][4]; e.immune = true; e.hp = g.hp; }
        else continue;
        E.push_back(e);
    }
    // 可用旗子
    vector<int> flagCell(g.flags.size(), -1);
    vector<char> flagAvail(g.flags.size(), 0);
    for (size_t i = 0; i < g.flags.size(); i++) {
        auto& f = g.flags[i];
        if ((f.status == "home" || f.status == "dropped") && f.x >= 0) { flagCell[i] = cid(f.x, f.y); flagAvail[i] = 1; }
    }
    vector<int> myCells;
    for (auto& m : M) if (m.alive) myCells.push_back(m.cell);

    // 威胁图
    vector<int> thr(NN, 0);
    for (auto& e : E) {
        if (e.immune || e.flag >= 0) continue;
        int ex = cx(e.cell), ey = cy(e.cell);
        for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++) {
            if (abs(dx) + abs(dy) > 3) continue;
            int x = ex + dx, y = ey + dy;
            if (x < 0 || y < 0 || x >= N || y >= N) continue;
            thr[cid(x, y)]++;
        }
    }

    // 敌人下一步预测
    for (auto& e : E) {
        e.pred.clear();
        if (e.immune) { e.pred.push_back({e.cell, 1.0}); e.pStay = 1.0; continue; }
        vector<pair<int, double>> cand;
        int x = cx(e.cell), y = cy(e.cell);
        int goal = -1, curG = INF;
        int myNear = -1, curM = INF;
        if (e.flag >= 0) {
            goal = nearestBaseCell(e.cell, e.team); curG = D(e.cell, goal);
        } else {
            for (size_t i = 0; i < g.flags.size(); i++) if (flagAvail[i]) { int d = D(e.cell, flagCell[i]); if (d < curG) { curG = d; goal = flagCell[i]; } }
            for (int c : myCells) { int d = D(e.cell, c); if (d < curM) { curM = d; myNear = c; } }
        }
        double tot = 0;
        for (int k = 0; k < 5; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
            int nc = cid(nx, ny);
            if (W[nc]) continue;
            double w;
            if (e.flag >= 0) {
                w = (k == 0) ? 0.5 : ((D(nc, goal) < curG) ? 5.0 : 0.4);
            } else {
                w = (k == 0) ? 1.2 : 1.0;
                if (goal >= 0 && k != 0 && D(nc, goal) < curG) w += 2.0;
                if (myNear >= 0 && curM <= 4 && k != 0 && D(nc, myNear) < curM) w += 1.5;
                if (k != 0 && occ[nc] >= 0) w *= 0.3;   // 有人占着，进不去
            }
            cand.push_back({nc, w}); tot += w;
        }
        e.pStay = 0;
        for (auto& c : cand) { c.second /= tot; if (c.first == e.cell) e.pStay = c.second; }
        e.pred = cand;
    }
    auto pRange = [&](int c, const EInfo& e) {
        double p = 0; for (auto& q : e.pred) if (canAtk(c, q.first)) p += q.second; return p;
    };

    // 带危险权重的 Dijkstra（多源，返回到目标集合的代价，单位 10 = 一步）
    auto ctgTo = [&](const vector<int>& targets, int K10) {
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
                int step = 10 + K10 * thr[nc];
                if (occ[nc] >= 0 && occ[nc] / 3 != me) step += 25;
                int nd = d + step;
                if (nd < cost[nc]) { cost[nc] = nd; pq.push({nd, nc}); }
            }
        }
        return cost;
    };

    // ---------------- 任务生成 ----------------
    vector<vector<Task>> tasks(3);
    // 敌方携旗者回家路径
    map<int, vector<int>> epath;  // enemy id -> path cells (excluding current)
    for (auto& e : E) {
        if (e.flag < 0 || e.immune) continue;
        int tb = nearestBaseCell(e.cell, e.team);
        vector<int> path; int c = e.cell; int guard = 0;
        while (c != tb && guard++ < 200) {
            int x = cx(c), y = cy(c); int nxt = -1;
            for (int k = 1; k < 5; k++) {
                int nx = x + DX[k], ny = y + DY[k];
                if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
                int nc = cid(nx, ny);
                if (W[nc]) continue;
                if (D(nc, tb) == D(c, tb) - 1) { nxt = nc; break; }
            }
            if (nxt < 0) break;
            c = nxt; path.push_back(c);
        }
        epath[e.id] = path;
    }

    for (int k = 0; k < 3; k++) {
        auto& m = M[k];
        if (!m.alive) { tasks[k].push_back({-1, 0, -1, 0, 0, 0}); continue; }
        if (m.flag >= 0) { tasks[k].push_back({0, 0, -1, 1000, 0, 0}); continue; }
        // GET
        for (size_t i = 0; i < g.flags.size(); i++) {
            if (!flagAvail[i]) continue;
            int fc = flagCell[i];
            int eta = D(m.cell, fc); if (eta >= INF) continue;
            if (g.flags[i].status == "dropped" && g.flags[i].return_at >= 0 && eta > g.flags[i].return_at - T) continue;
            int ret = distToBase(fc, me); if (ret >= INF) continue;
            if (eta + ret > remaining) continue;
            int eEta = INF, nearEnemies = 0;
            for (auto& e : E) {
                if (e.flag >= 0) continue;
                int d = D(e.cell, fc) + (e.immune ? 1 : 0);
                eEta = min(eEta, d);
                if (!e.immune && d <= 5) nearEnemies++;
            }
            double pget;
            if (eta + 2 < eEta) pget = 0.97; else if (eta < eEta) pget = 0.85; else if (eta == eEta) pget = 0.45;
            else if (eta == eEta + 1) pget = 0.25; else pget = 0.1;
            if (nTeams >= 3) pget = max(pget, eta < eEta ? 0.9 : eta == eEta ? 0.55 : eta <= eEta + 2 ? 0.35 : 0.2);
            int corridor = 0;
            for (auto& e : E) if (!e.immune && e.flag < 0 && D(e.cell, fc) + distToBase(e.cell, me) <= ret + 4) corridor++;
            double pret = 0.95 - 0.12 * min(3, nearEnemies) - 0.08 * min(4, corridor);
            double val = 100.0 * pget * pret - LAMBDA * (eta + ret);
            val += 6.0 * (lives(m.hp, g.damage) - 1);
            if (lives(m.hp, g.damage) == 1) {
                bool healthyNear = false;
                for (int j = 0; j < 3; j++) if (j != k && M[j].alive && M[j].flag < 0 && lives(M[j].hp, g.damage) >= 2 && D(M[j].cell, fc) <= eta + 2) healthyNear = true;
                if (healthyNear) val -= 60;
            }
            bool contested = (eEta <= eta + 3);
            tasks[k].push_back({1, (int)i, fc, val, eta, contested ? 1 : 0});
        }
        // INTERCEPT
        for (auto& e : E) {
            if (e.flag < 0 || e.immune) continue;
            auto& path = epath[e.id];
            int L = (int)path.size();
            if (L == 0) continue;
            int tfirst = -1;
            for (int t = 1; t <= L; t++) {
                int q = path[t - 1]; int qx = cx(q), qy = cy(q); bool ok = false;
                for (int dy = -2; dy <= 2 && !ok; dy++) for (int dx = -2; dx <= 2 && !ok; dx++) {
                    if (abs(dx) + abs(dy) > 2 || (dx == 0 && dy == 0)) continue;
                    int x = qx + dx, y = qy + dy;
                    if (x < 0 || y < 0 || x >= N || y >= N) continue;
                    int qq = cid(x, y);
                    if (W[qq]) continue;
                    if (!canAtk(qq, q)) continue;
                    if (D(m.cell, qq) <= t) ok = true;
                }
                if (ok) { tfirst = t; break; }
            }
            if (tfirst < 0) continue;
            if (nTeams >= 6 && tfirst > 6) continue;
            if (nTeams >= 3 && nTeams < 6 && tfirst > 12) continue;
            int hits = L - tfirst + 1;
            int target = (tfirst <= 1) ? e.cell : path[tfirst - 1];
            tasks[k].push_back({2, e.id, target, 0, tfirst, hits});
        }
        // ESCORT
        if (myCarrierIdx >= 0 && myCarrierIdx != k) {
            auto& c = M[myCarrierIdx];
            int threats = 0;
            for (auto& e : E) if (!e.immune && e.flag < 0 && D(e.cell, c.cell) <= 5) threats++;
            int d = D(m.cell, c.cell);
            double val = 12 + 22 * min(3, threats) - LAMBDA * 0.5 * d;
            if (threats == 0 && d > 6) val = 2;
            if (nTeams >= 3) val = 40 + 22 * min(3, threats) - LAMBDA * 0.5 * d;
            tasks[k].push_back({3, myCarrierIdx, c.cell, val, d, 0});
        }
        // HUNT
        for (auto& e : E) {
            if (e.immune || e.flag >= 0) continue;
            int d = D(m.cell, e.cell);
            if (d > 7) continue;
            int support = 0, mysup = 0;
            for (auto& e2 : E) if (e2.id != e.id && !e2.immune && e2.flag < 0 && D(e2.cell, e.cell) <= 3) support++;
            for (int j = 0; j < 3; j++) if (j != k && M[j].alive && M[j].canAtk && D(M[j].cell, e.cell) <= 4) mysup++;
            double val = (nTeams == 2 ? 30 : 8) - LAMBDA * d + (e.hp <= 68 ? 12 : 0) + (e.hp <= 34 ? 12 : 0) - 18 * support + 12 * mysup;
            if (nTeams > 2 && d > 4) continue;
            if (nTeams >= 6 && !(d <= 2 && lives(e.hp, g.damage) <= 2)) continue;
            if (lives(m.hp, g.damage) == 1 && lives(e.hp, g.damage) >= 2 && mysup == 0) continue;
            if (lives(m.hp, g.damage) < lives(e.hp, g.damage) && mysup == 0) val -= 15;
            if (val <= 0) continue;
            tasks[k].push_back({4, e.id, e.cell, val, d, 0});
        }
        // ROAM：去空旗点等旗（优先自家旗点、中心）
        {
            int bestSpot = -1; double bestV = -1e9;
            vector<pair<double, int>> roams;
            for (size_t i = 0; i < g.spots.size(); i++) {
                int sc = cid(g.spots[i].first, g.spots[i].second);
                bool taken = false;
                for (size_t f = 0; f < g.flags.size(); f++) if (flagAvail[f] && flagCell[f] == sc) taken = true;
                if (taken) continue;
                int d = D(m.cell, sc); if (d >= INF) continue;
                double v = 6 - 0.35 * d;
                for (auto& f : g.flags) if (f.status == "cooldown" && f.return_at >= 0) {
                    int wait = max(d, f.return_at - T);
                    v += max(0.0, 0.25 * (30.0 - LAMBDA * (wait + D(sc, bcells[me][4]))));
                }
                if ((int)i == me) v += (nTeams >= 6 ? 7 : 3);
                if (sc == centerCell) v += (nTeams >= 3 && nTeams <= 5) ? 5.0 : 2.5;
                v -= 1.5 * thr[sc];
                roams.push_back({v, sc});
                if (v > bestV) { bestV = v; bestSpot = sc; }
            }
            sort(roams.rbegin(), roams.rend());
            for (size_t i = 0; i < roams.size() && i < 3; i++) tasks[k].push_back({5, roams[i].second, roams[i].second, max(1.0, roams[i].first), 0, 0});
            if (roams.empty()) tasks[k].push_back({5, bcells[me][4], bcells[me][4], 1.0, 0, 0});
        }
    }

    // ---------------- 任务组合枚举 ----------------
    auto evalCombo = [&](const array<int, 3>& idx) {
        double total = 0;
        // GET 同旗
        vector<pair<int, int>> gets;  // (eta, k)
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 1) gets.push_back({t.eta, k}); }
        sort(gets.begin(), gets.end());
        map<int, int> cnt;
        for (auto& [eta, k] : gets) {
            auto& t = tasks[k][idx[k]];
            int c = cnt[t.key]++;
            double f = 1.0;
            if (c == 1) f = t.hits ? 0.6 : (nTeams >= 3 ? 0.55 : 0.15);
            if (c >= 2) f = t.hits ? 0.35 : (nTeams >= 3 ? 0.4 : 0.03);
            total += t.val * f;
        }
        // INTERCEPT
        map<int, vector<int>> inter;
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 2) inter[t.key].push_back(k); }
        for (auto& [eid, ks] : inter) {
            const EInfo* e = nullptr; for (auto& x : E) if (x.id == eid) e = &x;
            int hits = 0, tfirst = INF; double tcost = 0;
            for (int k : ks) { auto& t = tasks[k][idx[k]]; hits += t.hits; tfirst = min(tfirst, t.eta); tcost += LAMBDA * t.eta; }
            int needed = (e->hp + g.damage - 1) / g.damage;
            double deny = (nTeams == 2) ? 0.9 : 0.55;
            if (hits >= needed) total += 100.0 * (deny + 0.45) * 0.85 - tcost;
            else total += 15.0 * hits / needed - tcost;
        }
        // ESCORT
        int esc = 0;
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 3) { total += t.val * (esc == 0 ? 1.0 : 0.4); esc++; } }
        // HUNT
        map<int, int> hcnt;
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 4) { int c = hcnt[t.key]++; total += t.val * (c == 0 ? 1.0 : c == 1 ? 0.8 : 0.5); } }
        // ROAM
        map<int, int> rcnt;
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 5) { int c = rcnt[t.key]++; total += t.val * (c == 0 ? 1.0 : 0.2); } }
        for (int k = 0; k < 3; k++) { auto& t = tasks[k][idx[k]]; if (t.type == 0) total += t.val; }
        return total;
    };
    array<int, 3> best = {0, 0, 0}; double bestV = -1e18;
    for (int a = 0; a < (int)tasks[0].size(); a++)
        for (int b = 0; b < (int)tasks[1].size(); b++)
            for (int c = 0; c < (int)tasks[2].size(); c++) {
                array<int, 3> idx = {a, b, c};
                double v = evalCombo(idx);
                if (v > bestV) { bestV = v; best = idx; }
            }

    // ---------------- 移动选择 ----------------
    // 一命的角色站在旗上、而血多的队友就在旁边：让位（走开、不拾旗）
    vector<char> yieldK(3, 0);
    for (int k = 0; k < 3; k++) {
        auto& m = M[k];
        if (!m.alive || m.flag >= 0 || lives(m.hp, g.damage) != 1 || remaining <= 30) continue;
        bool onFlag = false;
        for (size_t i = 0; i < g.flags.size(); i++) if (flagAvail[i] && flagCell[i] == m.cell) onFlag = true;
        if (!onFlag) continue;
        for (int j = 0; j < 3; j++) if (j != k && M[j].alive && !M[j].spawning && M[j].flag < 0 && lives(M[j].hp, g.damage) >= 2 && D(M[j].cell, m.cell) <= 2) yieldK[k] = 1;
    }
    vector<int> order;  // 携旗者优先，其次让位者
    for (int k = 0; k < 3; k++) if (M[k].alive && M[k].flag >= 0) order.push_back(k);
    for (int k = 0; k < 3; k++) if (M[k].alive && M[k].flag < 0 && yieldK[k]) order.push_back(k);
    for (int k = 0; k < 3; k++) if (M[k].alive && M[k].flag < 0 && !yieldK[k]) order.push_back(k);
    vector<int> chosenCell(3, -1), chosenMove(3, 0);
    vector<char> reserved(NN, 0);
    vector<char> leaving(3, 0);
    vector<int> forced(3, -1);          // 被队友要求换位：必须走到该格
    vector<int> carrierNext(3, -1);     // 携旗者选定的下一格
    for (int k : order) {
        auto& m = M[k]; auto& t = tasks[k][best[k]];
        if (forced[k] >= 0) {
            int fc = forced[k]; int mv = 0;
            for (int q = 1; q < 5; q++) if (cx(m.cell) + DX[q] == cx(fc) && cy(m.cell) + DY[q] == cy(fc)) mv = q;
            chosenMove[k] = mv; chosenCell[k] = fc; reserved[fc] = 1; leaving[k] = 1;
            continue;
        }
        vector<int> targets;
        if (t.type == 0) targets = bcells[me];
        else if (t.type == 3) {
            // 护送：走到携旗者前方（沿其回家路线再往前一格）
            int ci = t.key; int cc = (chosenCell[ci] >= 0) ? chosenCell[ci] : M[ci].cell;
            auto cg = ctgTo(bcells[me], 5);
            int ahead = cc; int bestc = cg[cc];
            for (int q = 1; q < 5; q++) { int nx = cx(cc) + DX[q], ny = cy(cc) + DY[q]; if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue; int nc = cid(nx, ny); if (W[nc]) continue; if (cg[nc] < bestc) { bestc = cg[nc]; ahead = nc; } }
            targets = {ahead};
        }
        else if (t.type >= 0) targets = {t.target};
        else targets = {m.cell};
        bool chased = false;
        for (auto& e : E) if (!e.immune && e.flag < 0 && manh(e.cell, m.cell) <= 2) chased = true;
        int K10 = (m.flag >= 0) ? (chased ? 3 : (nTeams >= 6 ? 4 : 15)) : 5;
        if (m.hp <= 34 && !chased && m.flag < 0) K10 += 15;
        int curBaseD = (m.flag >= 0) ? distToBase(m.cell, me) : 0;
        auto ctg = ctgTo(targets, K10);
        double hitVal = 2.5 + (m.flag >= 0 ? 1.0 : 0) + (m.hp <= 34 ? 3.0 : m.hp <= 68 ? 1.0 : 0);
        if (nTeams >= 6) hitVal *= 1.3;
        if (m.spawning) hitVal = 0;
        int x = cx(m.cell), y = cy(m.cell);
        double bestS = -1e18; int bestK = 0, bestC = m.cell, bestSwap = -1;
        for (int mv = 0; mv < 5; mv++) {
            int nx = x + DX[mv], ny = y + DY[mv];
            if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
            int nc = cid(nx, ny);
            if (W[nc]) continue;
            if (reserved[nc]) continue;
            // 队友当前格：队友已决定离开可进；携旗者可与尚未决定的队友换位
            bool teammateHere = false; int swapWith = -1;
            for (int j = 0; j < 3; j++) if (j != k && M[j].alive && M[j].cell == nc && !(leaving[j])) {
                if (m.flag >= 0 && chosenCell[j] < 0 && forced[j] < 0 && M[j].flag < 0 && !M[j].spawning) swapWith = j; else teammateHere = true;
            }
            if (teammateHere) continue;
            if (m.flag >= 0 && distToBase(nc, me) > curBaseD) continue;   // 携旗不后退
            if (yieldK[k] && mv == 0) continue;   // 让位：必须走开
            double s = 0;
            if (ctg[nc] >= INT_MAX) s -= 1e6; else s -= ctg[nc] / 10.0;
            // 攻击期望
            double atk = 0;
            if (m.canAtk && !m.spawning) {
                for (auto& e : E) {
                    if (e.immune) continue;
                    double p = pRange(nc, e); if (p <= 0) continue;
                    double v = 2.5 + (e.flag >= 0 ? 2.5 : 0) + (e.hp <= 34 ? 3.0 : e.hp <= 68 ? 1.0 : 0);
                    if (myCarrierIdx >= 0 && D(e.cell, M[myCarrierIdx].cell) <= 3) v += 1.0;
                    atk = max(atk, p * v);
                }
            }
            s += (nTeams >= 6 ? 0.5 : 1.0) * atk;
            // 风险
            double risk = 0;
            if (!m.spawning) for (auto& e : E) { if (e.immune || e.flag >= 0) continue; risk += pRange(nc, e) * hitVal; }
            s -= risk;
            // 敌人占着的格子
            if (mv != 0 && occ[nc] >= 0 && occ[nc] / 3 != me) {
                const EInfo* e = nullptr; for (auto& x2 : E) if (x2.id == occ[nc]) e = &x2;
                if (e) s -= 1.0 + 6.0 * e->pStay;
            }
            if (getenv("BOTDBG") && m.flag >= 0) cerr << "T" << T << " u" << m.id << " mv" << mv << " nc(" << cx(nc) << "," << cy(nc) << ") ctg=" << ctg[nc] << " atk=" << atk << " risk=" << risk << " s=" << s << " task=" << t.type << endl;
            if (swapWith >= 0) s -= 0.3;
            if (s > bestS) { bestS = s; bestK = mv; bestC = nc; bestSwap = swapWith; }
        }
        if (bestSwap >= 0) { forced[bestSwap] = m.cell; reserved[m.cell] = 1; }
        chosenMove[k] = bestK; chosenCell[k] = bestC; reserved[bestC] = 1;
        if (bestC != m.cell) leaving[k] = 1;
    }

    // ---------------- 攻击目标 / 拾旗 / 丢旗 ----------------
    vector<string> act(3, "-");
    map<int, int> dmgOn;  // 目标 -> 已分配的命中次数
    auto threatAt = [&](int c) {
        double best = 0; int cnt = 0;
        for (auto& e : E) { if (e.immune || e.flag >= 0) continue; double p = pRange(c, e); best = max(best, p); if (p >= 0.45) cnt++; }
        return make_pair(best, cnt);
    };
    for (int k : order) {
        auto& m = M[k]; int c = chosenCell[k];
        // 携旗被追：距离基地还远、命够，就把旗放在脚下（继续站着）并转为反击
        if (m.flag >= 0 && !m.spawning) {
            auto [pt, cnt] = threatAt(c);
            int dist = distToBase(c, me);
            int escorts = 0;
            for (int j = 0; j < 3; j++) if (j != k && M[j].alive && M[j].canAtk && !M[j].spawning && manh(M[j].cell, c) <= 3) escorts++;
            if (nTeams <= 5 && cnt == 1 && dist > lives(m.hp, g.damage) + 1 && lives(m.hp, g.damage) >= 2 && escorts <= 1 && remaining > dist + 6) {
                act[k] = "X";
            }
            continue;
        }
        // 拾旗：脚下有旗。若有敌人在射程内且自己命够，先开火守旗（旗在脚下别人拿不走）
        if (m.flag < 0) {
            int onFlag = -1;
            for (size_t i = 0; i < g.flags.size(); i++) if (flagAvail[i] && flagCell[i] == c) onFlag = (int)i;
            if (onFlag >= 0) {
                bool yield = yieldK[k] && c == m.cell;
                if (yield) { /* 让血多的队友来拿 */ }
                else {
                auto [pt, cnt] = threatAt(c);
                bool timerOk = !(g.flags[onFlag].status == "dropped" && g.flags[onFlag].return_at >= 0 && g.flags[onFlag].return_at - T <= 2);
                bool guard = (pt >= 0.45) && lives(m.hp, g.damage) >= 2 && timerOk && m.canAtk && !m.spawning && remaining > distToBase(c, me) + 4;
                if (!guard) { act[k] = "P"; continue; }
                }
            }
        }
        if (!m.canAtk || m.spawning) continue;
        double bestE = 0; int bestId = -1;
        for (auto& e : E) {
            if (e.immune) continue;
            double p = pRange(c, e); if (p <= 0.001) continue;
            double v = 2.5 + (e.flag >= 0 ? 2.5 : 0) + (e.hp <= 34 ? 3.0 : e.hp <= 68 ? 1.0 : 0);
            if (myCarrierIdx >= 0 && D(e.cell, M[myCarrierIdx].cell) <= 3) v += 1.0;
            int needed = (e.hp + g.damage - 1) / g.damage;
            int already = dmgOn.count(e.id) ? dmgOn[e.id] : 0;
            if (already + 1 >= needed) v += 3.0;  // 集火可击杀
            else if (already > 0) v += 0.8;
            if (p * v > bestE) { bestE = p * v; bestId = e.id; }
        }
        if (bestId >= 0) { act[k] = to_string(bestId); dmgOn[bestId]++; }
        else if (m.flag < 0) {
            for (auto& sp : g.spots) if (cid(sp.first, sp.second) == c) act[k] = "P";
        }
    }

    string out = to_string(T);
    for (int k = 0; k < 3; k++) {
        if (!M[k].alive) { out += " S -"; continue; }
        out += ' '; out += MVC[chosenMove[k]]; out += ' '; out += act[k];
    }
    return out;
}

int main() {
    ios::sync_with_stdio(false);
    Game g;
    while (read_turn(g)) {
        cout << decide(g) << endl;
    }
}
