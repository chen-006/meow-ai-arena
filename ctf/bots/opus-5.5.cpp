// 夺旗 bot：全图 BFS 距离 + 战略任务分配（抢旗/拦截/护送/蹲旗点）+ 三人联合走位搜索（集火、危险评估）。
#include <bits/stdc++.h>
using namespace std;

struct Unit { int id, team, x, y, hp, flag, respawn_at; };      // 阵亡时 x = y = -1
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

// ---------------------------------------------------------------------------
static int S, NC, NT, ME, TURNS = 400;
static vector<char> W;
static vector<uint16_t> DD;
static vector<array<int, 5>> NB;
static vector<vector<int>> baseCells, distBase, spawnOrd;
static vector<vector<char>> inBase;
static vector<int> spots;
static vector<char> isSpot;
static int CEN;
static const int DX[5] = {0, 0, -1, 1, 0}, DY[5] = {-1, 1, 0, 0, 0};
static const char MC[5] = {'U', 'D', 'L', 'R', 'S'};
static const int INF = 65535;
static bool JOINT = true; static double PA = 0.6;
static double CARRIER_PROG = 120.0, CARRIER_DEATH = 600.0, CARRIER_HIT = 60.0, KILL_BASE = 260, KILL_CARRIER = 500, UNIT_DEATH = 170, HIT_COST = 25;

static inline int D(int a, int b) { return DD[(size_t)a * NC + b]; }

static bool canAtk(int a, int b) {
    int ax = a % S, ay = a / S, bx = b % S, by = b / S;
    int dx = bx - ax, dy = by - ay;
    if (abs(dx) + abs(dy) > 2) return false;
    if (abs(dx) == 2 && dy == 0) return !W[ay * S + ax + dx / 2];
    if (abs(dy) == 2 && dx == 0) return !W[(ay + dy / 2) * S + ax];
    if (abs(dx) == 1 && abs(dy) == 1) return !W[ay * S + ax + dx] || !W[(ay + dy) * S + ax];
    return true;
}

static void init(const Game& g) {
    S = g.size; NC = S * S; NT = g.teams; ME = g.me; TURNS = g.turns;
    if (NT == 2) { KILL_BASE = 380; KILL_CARRIER = 900; UNIT_DEATH = 170; }
    else if (NT <= 2) { KILL_BASE = 260; KILL_CARRIER = 500; UNIT_DEATH = 170; HIT_COST = 40; }
    else { KILL_BASE = 140; KILL_CARRIER = 450; UNIT_DEATH = 240; HIT_COST = 75; }
    W.assign(NC, 0);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) W[y * S + x] = g.map[y][x] == '#';
    NB.resize(NC);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            int c = y * S + x;
            for (int k = 0; k < 5; k++) {
                int nx = x + DX[k], ny = y + DY[k];
                if (nx < 0 || ny < 0 || nx >= S || ny >= S || W[ny * S + nx]) NB[c][k] = c;
                else NB[c][k] = ny * S + nx;
            }
        }
    DD.assign((size_t)NC * NC, INF);
    vector<int> q(NC);
    for (int s = 0; s < NC; s++) {
        if (W[s]) continue;
        uint16_t* d = &DD[(size_t)s * NC];
        d[s] = 0; int h = 0, tl = 0; q[tl++] = s;
        while (h < tl) {
            int c = q[h++];
            for (int k = 0; k < 4; k++) {
                int n = NB[c][k];
                if (n != c && d[n] == INF) { d[n] = d[c] + 1; q[tl++] = n; }
            }
        }
    }
    int c0 = S / 2;
    CEN = c0 * S + c0;
    baseCells.assign(NT, {}); inBase.assign(NT, vector<char>(NC, 0)); distBase.assign(NT, vector<int>(NC, INF));
    spawnOrd.assign(NT, {});
    for (int t = 0; t < NT; t++) {
        int bx = g.bases[t].first, by = g.bases[t].second;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int c = (by + dy) * S + bx + dx;
                baseCells[t].push_back(c); inBase[t][c] = 1;
            }
        for (int c = 0; c < NC; c++) {
            int m = INF;
            for (int b : baseCells[t]) m = min(m, D(b, c));
            distBase[t][c] = m;
        }
        vector<int> ord = baseCells[t];
        stable_sort(ord.begin(), ord.end(), [&](int a, int b) {
            int ax = a % S, ay = a / S, bx2 = b % S, by2 = b / S;
            int ka = abs(ax - c0) + abs(ay - c0), kb = abs(bx2 - c0) + abs(by2 - c0);
            if (ka != kb) return ka < kb;
            int la = abs(ax - bx) + 2 * abs(ay - by), lb = abs(bx2 - bx) + 2 * abs(by2 - by);
            return la < lb;
        });
        spawnOrd[t] = ord;
    }
    isSpot.assign(NC, 0);
    for (auto& s : g.spots) { int c = s.second * S + s.first; spots.push_back(c); isSpot[c] = 1; }
}

// ---------------------------------------------------------------------------
struct EInfo {
    int id, team, cell, hp, flag;
    int nopt; int opt[5]; double w[5];
    bool threat;   // 本回合可以攻击
};

static double binomGE(int k, double p, int need) {
    // P(Bin(k,p) >= need)
    if (need <= 0) return 1.0;
    if (need > k) return 0.0;
    double tot = 0;
    for (int i = need; i <= k; i++) {
        double c = 1;
        for (int j = 0; j < i; j++) c = c * (k - j) / (j + 1);
        tot += c * pow(p, i) * pow(1 - p, k - i);
    }
    return tot;
}

// 敌方携旗者下一步（沿到其基地的最短路）
static int carrierNext(int team, int cell) {
    int best = cell, bd = distBase[team][cell];
    for (int k = 0; k < 4; k++) {
        int n = NB[cell][k];
        if (distBase[team][n] < bd) { bd = distBase[team][n]; best = n; }
    }
    return best;
}

static string decide(Game& g) {
    int t = g.turn;
    int NU = g.units.size();
    vector<int> cell(NU, -1);
    vector<char> alive(NU, 0), respNow(NU, 0);
    vector<char> occ(NC, 0);
    for (auto& u : g.units)
        if (u.x >= 0) { cell[u.id] = u.y * S + u.x; alive[u.id] = 1; occ[cell[u.id]] = 1; }
    for (auto& u : g.units) {
        if (u.x < 0 && u.respawn_at >= 0 && u.respawn_at <= t) {
            for (int p : spawnOrd[u.team])
                if (!occ[p]) { cell[u.id] = p; occ[p] = 1; respNow[u.id] = 1; break; }
        }
    }
    // 旗子
    vector<int> flagAt(NC, -1);   // 本回合可拾的旗
    int offBoard = 0, returnsNow = 0;
    vector<char> homeFlagSpot(NC, 0);
    for (auto& f : g.flags) {
        bool returning = (f.status == "dropped" || f.status == "cooldown") && f.return_at >= 0 && f.return_at <= t;
        if (returning) { returnsNow++; continue; }
        if (f.status == "home" || f.status == "dropped") {
            int c = f.y * S + f.x;
            if (flagAt[c] < 0 || f.id < flagAt[c]) flagAt[c] = f.id;
            if (f.status == "home") homeFlagSpot[c] = 1;
        } else offBoard++;
    }
    int emptySpots = 0;
    for (int s : spots) if (!homeFlagSpot[s]) emptySpots++;

    // 敌人信息
    vector<EInfo> en;
    vector<int> enIdx(NU, -1);
    for (auto& u : g.units) {
        if (u.team == ME || !alive[u.id]) continue;
        EInfo e; e.id = u.id; e.team = u.team; e.cell = cell[u.id]; e.hp = u.hp; e.flag = u.flag;
        e.threat = (u.flag < 0);
        e.nopt = 0;
        for (int k = 0; k < 5; k++) {
            int n = NB[e.cell][k];
            bool dup = false;
            for (int j = 0; j < e.nopt; j++) if (e.opt[j] == n) dup = true;
            if (!dup) e.opt[e.nopt++] = n;
        }
        if (u.flag >= 0) {
            int nx = carrierNext(u.team, e.cell);
            for (int j = 0; j < e.nopt; j++) e.w[j] = (e.nopt > 1) ? 0.25 / (e.nopt - 1) : 0;
            for (int j = 0; j < e.nopt; j++) if (e.opt[j] == nx) e.w[j] += 0.75;
            double s = 0; for (int j = 0; j < e.nopt; j++) s += e.w[j];
            for (int j = 0; j < e.nopt; j++) e.w[j] /= s;
        } else {
            for (int j = 0; j < e.nopt; j++) e.w[j] = 1.0 / e.nopt;
        }
        enIdx[u.id] = en.size();
        en.push_back(e);
    }
    int NE = en.size();

    // 敌人到达各处的时间估计（非携旗者）
    auto enemyEta = [&](int c) {
        int best = INF;
        for (auto& u : g.units) {
            if (u.team == ME || u.flag >= 0) continue;
            int e;
            if (alive[u.id] || respNow[u.id]) e = D(cell[u.id], c);
            else if (u.respawn_at >= 0) e = (u.respawn_at - t) + distBase[u.team][c] + 1;
            else continue;
            best = min(best, e);
        }
        return best;
    };

    // 我方单位
    vector<int> my;
    for (int k = 0; k < 3; k++) {
        int id = 3 * ME + k;
        if (alive[id] || respNow[id]) my.push_back(id);
    }
    int K = my.size();

    // ---------------- 战略层：给每个单位分配目标 ----------------
    // 携旗者：带危险代价的回家势场
    vector<double> homeField(NC, 1e9);
    {
        vector<double> cost(NC, 1.0);
        for (auto& e : en) {
            if (!e.threat) continue;
            for (int c = 0; c < NC; c++) {
                if (W[c]) continue;
                int d = D(e.cell, c);
                if (d <= 1) cost[c] += 1.5; else if (d == 2) cost[c] += 1.0; else if (d == 3) cost[c] += 0.5;
            }
        }
        priority_queue<pair<double, int>, vector<pair<double, int>>, greater<>> pq;
        for (int b : baseCells[ME]) { homeField[b] = 0; pq.push({0, b}); }
        while (!pq.empty()) {
            auto [d, c] = pq.top(); pq.pop();
            if (d > homeField[c]) continue;
            for (int k = 0; k < 4; k++) {
                int n = NB[c][k];
                if (n == c) continue;
                double nd = d + cost[n];
                if (nd < homeField[n]) { homeField[n] = nd; pq.push({nd, n}); }
            }
        }
    }

    // 任务
    struct Task { int type, cell, key; double base; };   // type 0 抢旗 1 拦截 2 护送 3 蹲点
    vector<int> goal(NU, -1);
    vector<double> gw(NU, 0);
    vector<int> role(NU, -1);
    vector<char> carrier(NU, 0);
    for (int id : my) if (g.units[id].flag >= 0) { carrier[id] = 1; role[id] = 9; }

    int remain = TURNS - t + 1;
    vector<int> freeU;
    for (int id : my) if (!carrier[id]) freeU.push_back(id);

    int enemyAlive = 0;
    for (auto& e : en) if (e.threat) enemyAlive++;
    double density = 100.0 * enemyAlive / max(1, NC);
    // 在 c 拾旗后能活着送回家的概率
    auto pSurvive = [&](int id, int c) -> double {
        int thr = 0;
        for (auto& e : en) if (e.threat && D(e.cell, c) <= 5) thr++;
        int helpers = 0;
        for (int o : my) if (o != id && !carrier[o] && D(cell[o], c) <= 4) helpers++;
        double lam = 0.7 * max(0.0, thr - 0.7 * helpers) + 0.035 * distBase[ME][c] * density;
        int need = (g.units[id].hp + 33) / 34;
        double p = 0, term = exp(-lam);
        for (int i = 0; i < need; i++) { p += term; term *= lam / (i + 1); }
        return min(1.0, p);
    };

    // 候选任务值矩阵
    struct Cand { int unit, type, cell, key; double val; };
    vector<Cand> cands;
    for (int id : freeU) {
        int uc = cell[id];
        // 抢旗
        for (int c = 0; c < NC; c++) {
            if (flagAt[c] < 0) continue;
            int eu = D(uc, c);
            if (eu >= INF) continue;
            int ee = enemyEta(c);
            int margin = ee - eu;
            double pw = margin >= 2 ? 1.0 : margin == 1 ? 0.85 : margin == 0 ? 0.45 : margin == -1 ? 0.2 : 0.07;
            int hd = distBase[ME][c];
            if (eu + hd > remain) continue;
            int crowd = 0;
            for (auto& e : en) if (e.threat && D(e.cell, c) <= 4) crowd++;
            int mine = 0;
            for (int o : my) if (o != id && !carrier[o] && D(cell[o], c) <= 4) mine++;
            double cf = 1.0 / (1.0 + 0.45 * max(0, crowd - mine));
            double hf = 0.55 + 0.45 * g.units[id].hp / 100.0;
            double ps = pSurvive(id, c);
            double v = pw * (0.12 + 0.88 * ps) * (0.6 + 0.4 * cf) * hf * 1000.0 / (eu + hd + 8);
            cands.push_back({id, 0, c, flagAt[c], v});
        }
        // 拦截敌方携旗者
        for (auto& e : en) {
            if (e.flag < 0) continue;
            // 路径
            vector<int> path; int c = e.cell; path.push_back(c);
            int guard = 0;
            while (!inBase[e.team][c] && guard++ < 200) { c = carrierNext(e.team, c); path.push_back(c); }
            int L = path.size() - 1;
            int k0 = -1;
            for (int k = 0; k <= L; k++) if (D(uc, path[k]) <= k + 2) { k0 = k; break; }
            if (k0 < 0 || k0 > 10) continue;
            int need = (e.hp + 33) / 34;
            int avail = L - k0 + 1;
            double pk = avail >= need ? 0.8 : 0.25 * avail / need;
            double vd = (NT == 2 ? 1000.0 : 550.0);
            // 击杀点离我家近，旗子容易抢回
            int kc = path[min(k0 + need, L)];
            double grab = 1000.0 * 8.0 / (distBase[ME][kc] + 8) * 0.5;
            double v = pk * (vd + grab) / (k0 + 8);
            int gc = path[min(L, max(k0, 1))];
            cands.push_back({id, 1, gc, e.id, v});
        }
        // 护送
        for (int m : my) {
            if (!carrier[m]) continue;
            int thr = 0;
            for (auto& e : en) if (e.threat && D(e.cell, cell[m]) <= 7) thr++;
            double v = (60.0 + 250.0 * min(thr, 3)) / (D(uc, cell[m]) + 6);
            cands.push_back({id, 2, cell[m], m, v});
        }
        // 蹲旗点
        if (offBoard + returnsNow > 0) {
            for (int s : spots) {
                if (homeFlagSpot[s]) continue;
                bool enemyOn = false;
                for (auto& e : en) if (e.cell == s) enemyOn = true;
                if (enemyOn) continue;
                int hd = distBase[ME][s];
                double p = (double)(offBoard + returnsNow) / max(1, emptySpots);
                double v = 350.0 * p / (D(uc, s) + hd + 6);
                cands.push_back({id, 3, s, s, v});
            }
        }
    }
    // 贪心分配
    {
        vector<char> done(NU, 0);
        map<pair<int, int>, int> taken;  // (type,key) -> count
        for (int it = 0; it < (int)freeU.size(); it++) {
            double bv = -1; int bi = -1;
            for (int i = 0; i < (int)cands.size(); i++) {
                auto& c = cands[i];
                if (done[c.unit]) continue;
                int cnt = taken[{c.type, c.key}];
                double v = c.val;
                if (cnt > 0) {
                    if (c.type == 0) v *= 0.12;
                    else if (c.type == 1) v *= (cnt == 1 ? 0.6 : 0.3);
                    else if (c.type == 2) v *= 0.3;
                    else v = 0;
                }
                if (v > bv) { bv = v; bi = i; }
            }
            if (bi < 0) break;
            auto& c = cands[bi];
            done[c.unit] = 1; taken[{c.type, c.key}]++;
            goal[c.unit] = c.cell; role[c.unit] = c.type;
            gw[c.unit] = c.type == 0 ? 30 : c.type == 1 ? 25 : c.type == 2 ? 12 : 8;
        }
        for (int id : freeU) if (!done[id]) {
            // 默认：去离家最近的旗点
            int bs = spots[0]; int bd = INF;
            for (int s : spots) { int d = distBase[ME][s] + D(cell[id], s) / 2; if (d < bd) { bd = d; bs = s; } }
            goal[id] = bs; role[id] = 4; gw[id] = 5;
        }
    }

    // ---------------- 战术层：联合走位搜索 ----------------
    // 每个单位每个移动后的位置、危险、可命中的敌人
    struct MoveInfo { int nc; double danger; unsigned long long thr; vector<pair<int, int>> hits; /* (enemy idx, mask) */ };
    vector<array<MoveInfo, 5>> mi(K);
    const double P_ATK = 0.5;
    for (int i = 0; i < K; i++) {
        int id = my[i];
        int hp = g.units[id].hp;
        bool imm = respNow[id];
        for (int m = 0; m < 5; m++) {
            MoveInfo& M = mi[i][m];
            M.nc = NB[cell[id]][m];
            // 危险
            int tc = 0;
            M.thr = 0;
            if (!imm) {
                for (int ei = 0; ei < NE; ei++) {
                    auto& e = en[ei];
                    if (!e.threat) continue;
                    if (D(e.cell, M.nc) > 3 && abs(e.cell % S - M.nc % S) + abs(e.cell / S - M.nc / S) > 3) continue;
                    bool can = false;
                    for (int j = 0; j < e.nopt && !can; j++) if (canAtk(e.opt[j], M.nc)) can = true;
                    if (can) { tc++; if (ei < 64) M.thr |= 1ULL << ei; }
                }
            }
            int need = (hp + 33) / 34;
            double pd = binomGE(tc, carrier[id] ? 0.85 : P_ATK, need);
            double deathCost = carrier[id] ? CARRIER_DEATH : UNIT_DEATH;
            M.danger = pd * deathCost + tc * (carrier[id] ? 0.85 * CARRIER_HIT : P_ATK * HIT_COST);
            // 命中
            if (!imm && !carrier[id]) {
                for (int j = 0; j < NE; j++) {
                    auto& e = en[j];
                    int mask = 0;
                    for (int q = 0; q < e.nopt; q++) if (canAtk(M.nc, e.opt[q])) mask |= 1 << q;
                    if (mask) M.hits.push_back({j, mask});
                }
            }
        }
    }
    auto fieldVal = [&](int id, int c) -> double {
        if (carrier[id]) return homeField[c];
        if (goal[id] < 0) return 0;
        int d = D(goal[id], c);
        return d;
    };
    auto enemyKillVal = [&](const EInfo& e) {
        double v = KILL_BASE;
        if (e.flag >= 0) {
            v += KILL_CARRIER;
        }
        return v;
    };

    double bestScore = -1e18;
    int bestMv[3] = {4, 4, 4};
    int bestAct[3] = {-1, -1, -1};   // -1 无, -2 拾旗, >=0 攻击 id
    int total = 1; for (int i = 0; i < K; i++) total *= 5;
    vector<int> startCell(K), want(K);
    for (int i = 0; i < K; i++) startCell[i] = cell[my[i]];
    auto tStart = chrono::steady_clock::now();
    for (int combo = 0; combo < total; combo++) {
        if ((combo & 7) == 0 && chrono::duration<double, milli>(chrono::steady_clock::now() - tStart).count() > 25.0) break;
        int mv[3]; int cc = combo;
        for (int i = 0; i < K; i++) { mv[i] = cc % 5; cc /= 5; }
        bool bad = false;
        for (int i = 0; i < K; i++) {
            want[i] = mi[i][mv[i]].nc;
            if (want[i] != startCell[i] && occ[want[i]]) {
                // 有人占着：只允许是我方会离开的单位
                bool ok = false;
                for (int j = 0; j < K; j++) if (j != i && startCell[j] == want[i] && want[j] != startCell[j]) ok = true;
                if (!ok) { bad = true; break; }
            }
        }
        if (bad) continue;
        for (int i = 0; i < K && !bad; i++)
            for (int j = i + 1; j < K; j++) if (want[i] == want[j]) { bad = true; break; }
        if (bad) continue;
        double sc = 0;
        for (int i = 0; i < K; i++) {
            int id = my[i];
            sc -= gw[id] * fieldVal(id, want[i]) + (carrier[id] ? CARRIER_PROG * homeField[want[i]] : 0);
            if (!JOINT) sc -= mi[i][mv[i]].danger;
            if (carrier[id] && inBase[ME][want[i]]) sc += 5000;
        }
        if (JOINT) {
            double wg[3];
            for (int i = 0; i < K; i++) { int id = my[i]; wg[i] = 1.0 + (g.units[id].hp <= 34 ? 1.5 : 0) + (carrier[id] ? 2.5 : 0); }
            unsigned long long all = 0;
            for (int i = 0; i < K; i++) all |= mi[i][mv[i]].thr;
            double dp[3][4]; double eh[3];
            int needI[3];
            for (int i = 0; i < K; i++) { needI[i] = (g.units[my[i]].hp + 33) / 34; for (int k = 0; k < 4; k++) dp[i][k] = 0; dp[i][0] = 1; eh[i] = 0; }
            while (all) {
                int e = __builtin_ctzll(all); all &= all - 1;
                double sw = 0;
                for (int i = 0; i < K; i++) if (mi[i][mv[i]].thr >> e & 1) sw += wg[i];
                for (int i = 0; i < K; i++) if (mi[i][mv[i]].thr >> e & 1) {
                    double p = PA * wg[i] / sw;
                    eh[i] += p;
                    for (int k = 3; k >= 1; k--) dp[i][k] = dp[i][k] * (1 - p) + dp[i][k - 1] * p + (k == 3 ? dp[i][3] * p : 0);
                    dp[i][0] *= (1 - p);
                }
            }
            for (int i = 0; i < K; i++) {
                int id = my[i];
                double pd = 0;
                for (int k = needI[i]; k <= 3; k++) pd += dp[i][k];
                double deathCost = carrier[id] ? CARRIER_DEATH : UNIT_DEATH;
                sc -= pd * deathCost + eh[i] * (carrier[id] ? CARRIER_HIT : HIT_COST);
            }
        }
        // 动作选择：拾旗 / 攻击
        int act[3] = {-1, -1, -1};
        double pickVal[3] = {0, 0, 0};
        vector<char> flagClaim(0);
        int claimed[3] = {-1, -1, -1};
        for (int i = 0; i < K; i++) {
            int id = my[i];
            if (carrier[id]) continue;
            int c = want[i];
            if (flagAt[c] >= 0) {
                bool dup = false;
                for (int j = 0; j < i; j++) if (claimed[j] == c) dup = true;
                if (!dup) { claimed[i] = c; pickVal[i] = (200.0 + 1300.0 * pSurvive(id, c)) - 10 * distBase[ME][c]; }
            } else if (returnsNow > 0 && isSpot[c] && !homeFlagSpot[c]) {
                pickVal[i] = 1000.0 * returnsNow / max(1, emptySpots) * 0.8;
            }
        }
        // 枚举攻击分配
        double bestA = -1e18; int ba[3] = {-1, -1, -1};
        int choices[3][16]; int nch[3];
        for (int i = 0; i < 3; i++) nch[i] = 0;
        for (int i = 0; i < K; i++) {
            choices[i][nch[i]++] = -1;
            if (pickVal[i] > 0) choices[i][nch[i]++] = -2;
            {
                auto& hs = mi[i][mv[i]].hits;
                if ((int)hs.size() <= 6) { for (auto& h : hs) choices[i][nch[i]++] = h.first; }
                else {
                    vector<pair<double, int>> pr;
                    for (auto& h : hs) {
                        double p = 0; const EInfo& E = en[h.first];
                        for (int q = 0; q < E.nopt; q++) if (h.second >> q & 1) p += E.w[q];
                        p *= enemyKillVal(E) / (double)((E.hp + 33) / 34);
                        pr.push_back({-p, h.first});
                    }
                    sort(pr.begin(), pr.end());
                    for (int k = 0; k < 6; k++) choices[i][nch[i]++] = pr[k].second;
                }
            }
        }
        int sel[3] = {0, 0, 0};
        int tot2 = 1; for (int i = 0; i < K; i++) tot2 *= nch[i];
        for (int a = 0; a < tot2; a++) {
            int aa = a;
            for (int i = 0; i < K; i++) { sel[i] = choices[i][aa % nch[i]]; aa /= nch[i]; }
            double v = 0;
            for (int i = 0; i < K; i++) if (sel[i] == -2) v += pickVal[i];
            // 按敌人汇总
            for (int i = 0; i < K; i++) {
                if (sel[i] < 0) continue;
                int e = sel[i];
                bool first = true;
                for (int j = 0; j < i; j++) if (sel[j] == e) first = false;
                if (!first) continue;
                const EInfo& E = en[e];
                int need = (E.hp + 33) / 34;
                double pk = 0, eh = 0;
                for (int q = 0; q < E.nopt; q++) {
                    int hits = 0;
                    for (int j = i; j < K; j++) if (sel[j] == e) {
                        for (auto& h : mi[j][mv[j]].hits) if (h.first == e && (h.second >> q & 1)) hits++;
                    }
                    if (hits >= need) pk += E.w[q];
                    eh += E.w[q] * min(hits, need);
                }
                double kv = enemyKillVal(E);
                v += pk * kv + eh * kv * 0.22;
            }
            if (v > bestA) { bestA = v; for (int i = 0; i < K; i++) ba[i] = sel[i]; }
        }
        sc += bestA;
        if (sc > bestScore) {
            bestScore = sc;
            for (int i = 0; i < K; i++) { bestMv[i] = mv[i]; bestAct[i] = ba[i]; }
        }
    }

    // 输出
    string out = to_string(t);
    string notes = " #";
    for (int k = 0; k < 3; k++) {
        int id = 3 * ME + k;
        int idx = -1;
        for (int i = 0; i < K; i++) if (my[i] == id) idx = i;
        if (idx < 0) { out += " S -"; notes += "dead;"; continue; }
        out += ' '; out += MC[bestMv[idx]];
        int a = bestAct[idx];
        if (carrier[id]) out += " -";
        else if (a == -2) out += " P";
        else if (a >= 0) out += " " + to_string(en[a].id);
        else {
            // 站在旗点上且可能刷旗时顺手拾旗
            int nc = mi[idx][bestMv[idx]].nc;
            if (flagAt[nc] >= 0 || (returnsNow > 0 && isSpot[nc])) out += " P";
            else out += " -";
        }
        const char* rn[] = {"grab", "intercept", "escort", "camp", "idle"};
        int r = role[id];
        notes += (r == 9 ? string("carry") : (r >= 0 && r <= 4 ? string(rn[r]) : string("?")));
        notes += ";";
    }
    if (!notes.empty() && notes.back() == ';') notes.pop_back();
    return out + notes;
}

int main() {
    ios::sync_with_stdio(false);
    Game g;
    bool inited = false;
    while (read_turn(g)) {
        if (!inited) { init(g); inited = true; }
        cout << decide(g) << endl;
    }
}
