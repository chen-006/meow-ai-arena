// 夺旗 bot v1：全图 BFS + 任务分配（送旗/截击/取旗/护送/待命）+ 3 角色联合动作评估（威胁、命中、集火、击杀）
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

// ============================ 参数 ============================
static double envd(const char* n, double d) {
#ifdef TUNE
    const char* s = getenv(n);
    if (s) return atof(s);
#endif
    (void)n;
    return d;
}
struct Params {
    double VK, VKC, VD, VDC, VHD, VHT, PICK, CAPT, WG_FETCH, WG_DELIV, WG_HUNT, WG_SUP, WG_STA;
    double P_IN, P_REACH, STAYW, SLACK, ELIG, HUNTSLACK, ENGR, ENGRATIO, WG_ENG, PF1, PF2, FETCHMAXEFF, ESCR, WG_ESC, CTRN, FELIG, FEN;
    void load() {
        VK = envd("VK", 11); VKC = envd("VKC", 22); VD = envd("VD", 5); VDC = envd("VDC", 28);
        VHD = envd("VHD", 3.0); VHT = envd("VHT", 2.0); PICK = envd("PICK", 30); CAPT = envd("CAPT", 100);
        WG_FETCH = envd("WG_FETCH", 1.6); WG_DELIV = envd("WG_DELIV", 1.5); WG_HUNT = envd("WG_HUNT", 1.2);
        WG_SUP = envd("WG_SUP", 0.5); WG_STA = envd("WG_STA", 0.3);
        P_IN = envd("P_IN", 0.9); P_REACH = envd("P_REACH", 0.5); STAYW = envd("STAYW", 0.35); SLACK = envd("SLACK", 5); ELIG = envd("ELIG", 3); HUNTSLACK = envd("HUNTSLACK", 2); ENGR = envd("ENGR", 6); ENGRATIO = envd("ENGRATIO", 1.0); WG_ENG = envd("WG_ENG", 0.8); PF1 = envd("PF1", 1.0); PF2 = envd("PF2", 1.0); FETCHMAXEFF = envd("FETCHMAXEFF", 99); CTRN = envd("CTRN", 4); FELIG = envd("FELIG", 99); FEN = envd("FEN", 4); ESCR = envd("ESCR", 8); WG_ESC = envd("WG_ESC", 1.2);
    }
} PR;

// ============================ 地图 ============================
static const int MAXC = 49 * 49;
static const int INFD = 30000;
static int W, NC, NT, ME;
static bool WALL[MAXC];
static int NB[MAXC][5];                 // 0 不动 1 上 2 下 3 左 4 右；-1 为墙
static const int DXs[5] = {0, 0, 0, -1, 1};
static const int DYs[5] = {0, -1, 1, 0, 0};
static const char MVC[5] = {'S', 'U', 'D', 'L', 'R'};
static vector<int16_t> DISTV;
static int16_t* DIST;
static vector<vector<int>> BASED;       // BASED[team][cell]：到该队基地的最短步数
static vector<array<int, 9>> BASECELLS;
static vector<vector<char>> ISBASE;
static vector<vector<int>> SPAWN;
static vector<int> SPOTC, BASEC;
static int CENTER, MYSPOT;
static bool inited = false;

static inline int Cx(int c) { return c % W; }
static inline int Cy(int c) { return c / W; }
static inline int MD(int a, int b) { return abs(a % W - b % W) + abs(a / W - b / W); }
static inline int D2(int a, int b) { return DIST[a * NC + b]; }

static bool canAtk(int a, int b) {
    int ax = a % W, ay = a / W, bx = b % W, by = b / W;
    int dx = bx - ax, dy = by - ay;
    if (abs(dx) + abs(dy) > 2) return false;
    if (abs(dx) == 2 && dy == 0) return !WALL[ay * W + ax + dx / 2];
    if (abs(dy) == 2 && dx == 0) return !WALL[(ay + dy / 2) * W + ax];
    if (abs(dx) == 1 && abs(dy) == 1) return !WALL[ay * W + ax + dx] || !WALL[(ay + dy) * W + ax];
    return true;
}

static void bfsFrom(int s, int16_t* d) {
    static int q[MAXC];
    for (int i = 0; i < NC; i++) d[i] = INFD;
    int h = 0, t = 0; q[t++] = s; d[s] = 0;
    while (h < t) {
        int c = q[h++];
        for (int k = 1; k < 5; k++) { int n = NB[c][k]; if (n >= 0 && d[n] == INFD) { d[n] = d[c] + 1; q[t++] = n; } }
    }
}

static void init(const Game& g) {
    PR.load();
    W = g.size; NC = W * W; NT = g.teams; ME = g.me;
    for (int y = 0; y < W; y++) for (int x = 0; x < W; x++) WALL[y * W + x] = g.map[y][x] == '#';
    for (int c = 0; c < NC; c++) {
        int x = c % W, y = c / W;
        for (int k = 0; k < 5; k++) {
            int nx = x + DXs[k], ny = y + DYs[k];
            if (WALL[c] || nx < 0 || ny < 0 || nx >= W || ny >= W || WALL[ny * W + nx]) NB[c][k] = (k == 0 && !WALL[c]) ? c : -1;
            else NB[c][k] = ny * W + nx;
        }
    }
    DISTV.assign((size_t)NC * NC, INFD);
    DIST = DISTV.data();
    for (int s = 0; s < NC; s++) if (!WALL[s]) bfsFrom(s, DIST + (size_t)s * NC);
    CENTER = (W / 2) * W + W / 2;
    BASEC.clear(); BASECELLS.assign(NT, {}); ISBASE.assign(NT, vector<char>(NC, 0)); BASED.assign(NT, vector<int>(NC, INFD)); SPAWN.assign(NT, {});
    for (int t = 0; t < NT; t++) {
        int bx = g.bases[t].first, by = g.bases[t].second;
        BASEC.push_back(by * W + bx);
        int k = 0;
        vector<int> cells;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) { int c = (by + dy) * W + bx + dx; BASECELLS[t][k++] = c; ISBASE[t][c] = 1; cells.push_back(c); }
        for (int c = 0; c < NC; c++) if (!WALL[c]) { int m = INFD; for (int bc : BASECELLS[t]) m = min(m, (int)D2(bc, c)); BASED[t][c] = m; }
        stable_sort(cells.begin(), cells.end(), [&](int a, int b) {
            auto key = [&](int c) { return make_pair(abs(c % W - W / 2) + abs(c / W - W / 2), abs(c % W - bx) + 2 * abs(c / W - by)); };
            return key(a) < key(b);
        });
        SPAWN[t] = cells;
    }
    SPOTC.clear();
    for (auto& s : g.spots) SPOTC.push_back(s.second * W + s.first);
    MYSPOT = SPOTC.empty() ? CENTER : SPOTC[0];
    int best = INFD;
    for (int s : SPOTC) { int d = D2(BASEC[ME], s); if (d < best) { best = d; MYSPOT = s; } }
    inited = true;
}

// ============================ 决策 ============================
struct En { int id, team, c, hp, flag; bool canAtk; int nm; int mc[5]; };
struct My {
    int k, id; bool active, spawning; int c, hp, flag;
    int nm; int mv[5]; int mc[5];
    int mode = 0;       // 0 待命 1 取旗 2 送旗 3 截击 4 护送/支援
    vector<int> G; double wg = 0; int fcell = -1; int tgtEn = -1;
    string note;
};

static double atLeast(const double* p, int n, int need) {
    if (need <= 0) return 1.0;
    if (n < need) return 0.0;
    double dp[8] = {0}; dp[0] = 1.0;
    for (int i = 0; i < n; i++) {
        for (int k = need; k >= 0; k--) {
            double up = (k > 0 ? dp[k - 1] * p[i] : 0.0);
            if (k == need) dp[k] = dp[k] + up;
            else dp[k] = dp[k] * (1 - p[i]) + up;
        }
    }
    return dp[need];
}

static string decide(const Game& g) {
    if (!inited) init(g);
    const int t = g.turn;
    const int remaining = g.turns - t + 1;   // 含本回合
    vector<Unit> us(NT * 3);
    for (auto& u : g.units) if (u.id >= 0 && u.id < NT * 3) us[u.id] = u;
    vector<int> occ(NC, -1);
    for (auto& u : us) if (u.x >= 0) occ[u.y * W + u.x] = u.id;

    // ---- 敌方 ----
    vector<En> en;
    for (auto& u : us) {
        if (u.team == ME || u.x < 0) continue;
        En e; e.id = u.id; e.team = u.team; e.c = u.y * W + u.x; e.hp = u.hp; e.flag = u.flag; e.canAtk = (u.flag < 0);
        e.nm = 0;
        for (int k = 0; k < 5; k++) if (NB[e.c][k] >= 0) e.mc[e.nm++] = NB[e.c][k];
        en.push_back(e);
    }
    // ---- 己方 ----
    My my[3];
    {
        vector<char> taken(NC, 0);
        for (int c = 0; c < NC; c++) if (occ[c] >= 0) taken[c] = 1;
        for (int k = 0; k < 3; k++) {
            My& m = my[k]; Unit& u = us[3 * ME + k];
            m.k = k; m.id = 3 * ME + k; m.hp = u.hp; m.flag = u.flag; m.active = false; m.spawning = false; m.nm = 0; m.c = -1;
            if (u.x >= 0) { m.active = true; m.c = u.y * W + u.x; }
            else if (u.respawn_at == t) {
                for (int c : SPAWN[ME]) if (!taken[c]) { taken[c] = 1; m.c = c; m.active = true; m.spawning = true; m.hp = g.hp; m.flag = -1; break; }
            }
        }
    }
    // ---- 旗子 ----
    struct FI { int id, c, st, ret; };
    vector<FI> avail;                  // 可拾取的旗
    vector<int> carriedBy(g.flags.size(), -1);
    for (auto& f : g.flags) {
        if (f.status == "home" && f.x >= 0) avail.push_back({f.id, f.y * W + f.x, 0, 1 << 30});
        else if (f.status == "dropped" && f.x >= 0 && f.return_at > t) avail.push_back({f.id, f.y * W + f.x, 1, f.return_at});
    }

    // ---- 候选移动 ----
    for (int k = 0; k < 3; k++) {
        My& m = my[k];
        if (!m.active) continue;
        for (int mv = 0; mv < 5; mv++) {
            int nc = NB[m.c][mv];
            if (nc < 0) continue;
            if (mv != 0 && occ[nc] >= 0 && us[occ[nc]].team != ME) continue;
            m.mv[m.nm] = mv; m.mc[m.nm] = nc; m.nm++;
        }
    }

    // ---- 任务分配 ----
    bool assigned[3] = {false, false, false};
    auto setGoal = [&](My& m, int mode, int cell, double wg) {
        m.mode = mode; m.G.assign(NC, 0);
        for (int c = 0; c < NC; c++) m.G[c] = (cell >= 0) ? D2(cell, c) : 0;
        m.wg = wg;
    };
    // 危险附加代价（用于携旗者绕行）
    vector<int> extra(NC, 0);
    for (auto& e : en) if (e.canAtk) {
        int ex = e.c % W, ey = e.c / W;
        for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++) {
            int d = abs(dx) + abs(dy); if (d > 3) continue;
            int x = ex + dx, y = ey + dy; if (x < 0 || y < 0 || x >= W || y >= W) continue;
            extra[y * W + x] += (d <= 2 ? 3 : 1);
        }
    }
    // 1) 携旗者：送旗（带危险代价的 Dijkstra）
    bool anyCarrier = false; int carrierK = -1;
    {
        bool need = false;
        for (int k = 0; k < 3; k++) if (my[k].active && my[k].flag >= 0) need = true;
        if (need) {
            vector<int> sd(NC, INFD);
            priority_queue<pair<int, int>, vector<pair<int, int>>, greater<>> pq;
            for (int bc : BASECELLS[ME]) { sd[bc] = 0; pq.push({0, bc}); }
            while (!pq.empty()) {
                auto [d, c] = pq.top(); pq.pop();
                if (d > sd[c]) continue;
                for (int k = 1; k < 5; k++) { int n = NB[c][k]; if (n < 0) continue; int nd = d + 1 + extra[c]; if (nd < sd[n]) { sd[n] = nd; pq.push({nd, n}); } }
            }
            for (int k = 0; k < 3; k++) if (my[k].active && my[k].flag >= 0) {
                My& m = my[k]; m.mode = 2; m.G = sd; m.wg = PR.WG_DELIV; assigned[k] = true; anyCarrier = true; carrierK = k; m.note = "deliver";
            }
        }
    }
    // 2) 截击敌方携旗者
    vector<int> enemyCarriers;
    for (int i = 0; i < (int)en.size(); i++) if (en[i].flag >= 0) enemyCarriers.push_back(i);
    for (int ei : enemyCarriers) {
        const En& e = en[ei];
        // 敌方预测路径
        vector<int> path; int c = e.c; path.push_back(c);
        for (int s = 0; s < 40 && BASED[e.team][c] > 0; s++) {
            int bestn = -1, bd = BASED[e.team][c];
            for (int k = 1; k < 5; k++) { int n = NB[c][k]; if (n >= 0 && BASED[e.team][n] < bd) { bd = BASED[e.team][n]; bestn = n; } }
            if (bestn < 0) break;
            c = bestn; path.push_back(c);
        }
        // 找截击者：所有能赶上的空闲角色
        for (int k = 0; k < 3; k++) {
            if (!my[k].active || assigned[k] || my[k].flag >= 0) continue;
            for (int s = 0; s < (int)path.size(); s++) {
                int d = D2(my[k].c, path[s]);
                if (d <= s + (int)PR.HUNTSLACK && s <= 14) {
                    My& m = my[k]; setGoal(m, 3, path[s], PR.WG_HUNT); m.tgtEn = ei; assigned[k] = true; m.note = "hunt";
                    break;
                }
            }
        }
    }
    // 3) 取旗
    {
        struct Cand { int k, fi, eta; };
        vector<Cand> cands;
        vector<int> rival(avail.size(), INFD);
        for (int fi = 0; fi < (int)avail.size(); fi++) {
            for (auto& e : en) if (e.flag < 0) rival[fi] = min(rival[fi], (int)D2(e.c, avail[fi].c));
            for (auto& u : us) if (u.team != ME && u.x < 0 && u.respawn_at >= 0) {
                int dd = max(0, u.respawn_at - t) + D2(BASEC[u.team], avail[fi].c);
                rival[fi] = min(rival[fi], dd);
            }
            for (int k = 0; k < 3; k++) {
                if (!my[k].active || assigned[k] || my[k].flag >= 0) continue;
                int eta = D2(my[k].c, avail[fi].c);
                if (avail[fi].st == 1 && eta > avail[fi].ret - t) continue;
                if (eta + (int)BASED[ME][avail[fi].c] > remaining) continue;
                cands.push_back({k, fi, eta});
            }
        }
        sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.eta < b.eta; });
        vector<char> fdone(avail.size(), 0);
        for (auto& cd : cands) {
            if (assigned[cd.k] || fdone[cd.fi]) continue;
            int margin = rival[cd.fi] - cd.eta;
            if (margin < -PR.SLACK) continue;
            if (NT >= (int)PR.FEN) {
                int mn = INFD;
                for (int tt = 0; tt < NT; tt++) if (tt != ME) mn = min(mn, (int)D2(BASEC[tt], avail[cd.fi].c));
                if (D2(BASEC[ME], avail[cd.fi].c) > mn + (int)PR.FELIG) continue;
            }
            if (BASED[ME][avail[cd.fi].c] > 3) {
                int chasers = 0, guards = 0;
                for (auto& e : en) if (e.canAtk && D2(e.c, avail[cd.fi].c) <= 3) chasers++;
                for (int j = 0; j < 3; j++) if (j != cd.k && my[j].active && !my[j].spawning && D2(my[j].c, avail[cd.fi].c) <= 4) guards++;
                if (chasers - guards >= (int)PR.FETCHMAXEFF) continue;
            }
            My& m = my[cd.k]; setGoal(m, 1, avail[cd.fi].c, PR.WG_FETCH); m.fcell = avail[cd.fi].c; assigned[cd.k] = true; fdone[cd.fi] = 1; m.note = "fetch";
        }
    }
    // 4) 其余：护送 / 分散待命到空旗点
    {
        vector<int> freeK;
        for (int k = 0; k < 3; k++) if (my[k].active && !assigned[k]) freeK.push_back(k);
        if (anyCarrier && carrierK >= 0 && !freeK.empty()) {
            int cc = my[carrierK].c;
            vector<pair<int, int>> th;
            for (int ei = 0; ei < (int)en.size(); ei++) if (en[ei].canAtk && D2(en[ei].c, cc) <= (int)PR.ESCR) th.push_back({D2(en[ei].c, cc), ei});
            sort(th.begin(), th.end());
            if (!th.empty()) {
                sort(freeK.begin(), freeK.end(), [&](int a, int b) { return D2(my[a].c, cc) < D2(my[b].c, cc); });
                vector<int> usedCells;
                int nesc = min((int)freeK.size(), 2);
                for (int idx = 0; idx < nesc; idx++) {
                    int k = freeK[idx]; const En& e = en[th[min(idx, (int)th.size() - 1)].second];
                    int bestCell = -1; double bv = 1e18;
                    int cx = cc % W, cy = cc / W;
                    for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
                        if (abs(dx) + abs(dy) > 2) continue;
                        int x = cx + dx, y = cy + dy; if (x < 0 || y < 0 || x >= W || y >= W) continue;
                        int c = y * W + x; if (WALL[c] || c == cc) continue;
                        if (find(usedCells.begin(), usedCells.end(), c) != usedCells.end()) continue;
                        double v = D2(c, e.c) + 0.3 * D2(c, my[k].c);
                        if (v < bv) { bv = v; bestCell = c; }
                    }
                    if (bestCell < 0) continue;
                    usedCells.push_back(bestCell);
                    setGoal(my[k], 4, bestCell, PR.WG_ESC); my[k].note = "escort"; assigned[k] = true;
                }
                vector<int> rest;
                for (int k : freeK) if (!assigned[k]) rest.push_back(k);
                freeK = rest;
            }
        }
        // 局部优势时主动接敌
        for (int idx = 0; idx < (int)freeK.size();) {
            int k = freeK[idx]; My& m = my[k];
            int bestE = -1; double bestV = -1e9;
            for (int ei = 0; ei < (int)en.size(); ei++) {
                const En& e = en[ei]; int d = D2(m.c, e.c);
                if (d > (int)PR.ENGR) continue;
                double myP = 0, enP = 0;
                for (int j = 0; j < 3; j++) if (my[j].active && !my[j].spawning && D2(my[j].c, e.c) <= 5) myP += my[j].hp;
                for (auto& e2 : en) if (e2.canAtk && D2(e2.c, e.c) <= 4) enP += e2.hp;
                if (myP < PR.ENGRATIO * enP) continue;
                double v = -d - e.hp * 0.02 + (e.flag >= 0 ? 10 : 0);
                if (v > bestV) { bestV = v; bestE = ei; }
            }
            if (bestE >= 0) { setGoal(m, 5, en[bestE].c, PR.WG_ENG); m.note = "engage"; assigned[k] = true; freeK.erase(freeK.begin() + idx); }
            else idx++;
        }
        vector<int> stations;
        for (int sp : SPOTC) {
            bool hasFlag = false;
            for (auto& f : g.flags) if (f.status == "home" && f.x >= 0 && f.y * W + f.x == sp) hasFlag = true;
            if (hasFlag) continue;
            if (sp == CENTER && NT >= (int)PR.CTRN) continue;
            int myd = D2(BASEC[ME], sp), mn = INFD;
            for (int tt = 0; tt < NT; tt++) if (tt != ME) mn = min(mn, (int)D2(BASEC[tt], sp));
            if (myd <= mn + (int)PR.ELIG) stations.push_back(sp);
        }
        if (stations.empty()) stations.push_back(MYSPOT);
        struct Pr { int k, si, d; };
        vector<Pr> prs;
        for (int k : freeK) for (int si = 0; si < (int)stations.size(); si++) prs.push_back({k, si, (int)D2(my[k].c, stations[si])});
        sort(prs.begin(), prs.end(), [](const Pr& a, const Pr& b) { return a.d < b.d; });
        vector<char> sused(stations.size(), 0);
        for (auto& pr : prs) {
            if (assigned[pr.k] || sused[pr.si]) continue;
            setGoal(my[pr.k], 0, stations[pr.si], PR.WG_STA); my[pr.k].note = "station"; assigned[pr.k] = true; sused[pr.si] = 1;
        }
        for (int k : freeK) if (!assigned[k]) {
            int bd = INFD, bs = stations[0];
            for (int sp : stations) { int d = D2(my[k].c, sp); if (d < bd) { bd = d; bs = sp; } }
            setGoal(my[k], 0, bs, PR.WG_STA); my[k].note = "station2"; assigned[k] = true;
        }
    }

    // ---- 联合动作评估 ----
    int NE = en.size();
    // 预计算：pa[e][k][mi]（敌方 e 攻击我方 k 在候选 mi 的概率），ph[e][k][mi]（我方命中 e 的概率）
    static double pa[48][3][5], ph[48][3][5];
    for (int ei = 0; ei < NE; ei++) for (int k = 0; k < 3; k++) {
        My& m = my[k];
        for (int mi = 0; mi < m.nm; mi++) {
            pa[ei][k][mi] = 0; ph[ei][k][mi] = 0;
            if (!m.active) continue;
            int c = m.mc[mi]; const En& e = en[ei];
            if (e.canAtk && !m.spawning) {
                bool now = canAtk(e.c, c), reach = false;
                for (int q = 0; q < e.nm && !reach; q++) if (canAtk(e.mc[q], c)) reach = true;
                pa[ei][k][mi] = now ? PR.P_IN : (reach ? PR.P_REACH : 0.0);
            }
            if (!m.spawning && m.flag < 0) {
                double p = 0; int others = max(1, e.nm - 1);
                for (int q = 0; q < e.nm; q++) {
                    double w = (e.mc[q] == e.c) ? PR.STAYW : (1 - PR.STAYW) / others;
                    if (e.nm == 1) w = 1.0;
                    if (canAtk(c, e.mc[q])) p += w;
                }
                ph[ei][k][mi] = min(1.0, p);
            }
        }
    }
    // 单元静态得分
    double base[3][5];
    int pickAt[3][5];  // 该位置可拾取旗（1）
    double pickB[3][5];
    for (int k = 0; k < 3; k++) {
        My& m = my[k];
        for (int mi = 0; mi < m.nm; mi++) {
            int c = m.mc[mi];
            double s = 0;
            s -= m.wg * (m.G.empty() ? 0 : m.G[c]);
            pickAt[k][mi] = 0; pickB[k][mi] = 0;
            if (m.flag >= 0) {
                if (ISBASE[ME][c]) s += PR.CAPT;
            } else {
                for (auto& f : avail) if (f.c == c) {
                    pickAt[k][mi] = 1;
                    double coll = 0;
                    for (auto& e : en) if (e.flag < 0 && D2(e.c, c) <= 1) coll = max(coll, 0.6);
                    double pf = 1.0;
                    if (BASED[ME][c] > 3) {
                        int chasers = 0, guards = 0;
                        for (auto& e : en) if (e.canAtk && D2(e.c, c) <= 3) chasers++;
                        for (int j = 0; j < 3; j++) if (j != k && my[j].active && !my[j].spawning && D2(my[j].c, c) <= 2) guards++;
                        int eff = max(0, chasers - guards);
                        pf = eff == 0 ? 1.0 : eff == 1 ? PR.PF1 : PR.PF2;
                    }
                    pickB[k][mi] = PR.PICK * (1 - coll) * pf;
                }
            }
            base[k][mi] = s;
        }
    }
    // 枚举
    double bestScore = -1e18; int bestSel[3] = {0, 0, 0}; int bestTgt[3] = {-1, -1, -1};
    int sel[3];
    int n0 = my[0].active ? my[0].nm : 1, n1 = my[1].active ? my[1].nm : 1, n2 = my[2].active ? my[2].nm : 1;
    for (sel[0] = 0; sel[0] < n0; sel[0]++) for (sel[1] = 0; sel[1] < n1; sel[1]++) for (sel[2] = 0; sel[2] < n2; sel[2]++) {
        int cell[3] = {-1, -1, -1}; bool ok = true;
        for (int k = 0; k < 3; k++) if (my[k].active) cell[k] = my[k].mc[sel[k]];
        for (int a = 0; a < 3 && ok; a++) for (int b = a + 1; b < 3; b++) if (cell[a] >= 0 && cell[a] == cell[b]) { ok = false; break; }
        if (!ok) continue;
        double score = 0;
        for (int k = 0; k < 3; k++) if (my[k].active) score += base[k][sel[k]];
        // 敌方火力分配
        double inc[3][48]; int incn[3] = {0, 0, 0};
        for (int ei = 0; ei < NE; ei++) {
            if (!en[ei].canAtk) continue;
            int bk = -1; double bp = -1;
            for (int k = 0; k < 3; k++) if (my[k].active && pa[ei][k][sel[k]] > 0) {
                double pr = (my[k].flag >= 0 ? 1000 : 0) + (200 - my[k].hp) + pa[ei][k][sel[k]] * 10;
                if (pr > bp) { bp = pr; bk = k; }
            }
            if (bk >= 0) inc[bk][incn[bk]++] = pa[ei][bk][sel[bk]];
        }
        for (int k = 0; k < 3; k++) if (my[k].active && incn[k] > 0) {
            int need = (my[k].hp + g.damage - 1) / g.damage;
            double pd = atLeast(inc[k], incn[k], need);
            double eh = 0; for (int i = 0; i < incn[k]; i++) eh += inc[k][i];
            double endf = min(1.0, remaining / 12.0);
            double vd = PR.VD * (0.35 + 0.65 * min(1.0, my[k].hp / 100.0)) * endf + (my[k].flag >= 0 ? PR.VDC : 0);
            score -= pd * vd + min(eh, (double)(need - 1)) * PR.VHT;
        }
        // 我方攻击目标枚举
        int opt[3][5]; int on[3];
        for (int k = 0; k < 3; k++) {
            on[k] = 0; opt[k][on[k]++] = -1;
            if (!my[k].active || my[k].spawning || my[k].flag >= 0) continue;
            vector<pair<double, int>> lst;
            for (int ei = 0; ei < NE; ei++) {
                double p = ph[ei][k][sel[k]];
                if (p > 0.05) lst.push_back({p + (en[ei].flag >= 0 ? 2 : 0) + (200 - en[ei].hp) * 0.01, ei});
            }
            sort(lst.begin(), lst.end(), [](auto& a, auto& b) { return a.first > b.first; });
            for (int i = 0; i < (int)lst.size() && on[k] < 4; i++) opt[k][on[k]++] = lst[i].second;
        }
        double bestAtk = -1e18; int bt[3] = {-1, -1, -1};
        for (int a = 0; a < on[0]; a++) for (int b = 0; b < on[1]; b++) for (int c = 0; c < on[2]; c++) {
            int tg[3] = {opt[0][a], opt[1][b], opt[2][c]};
            double val = 0;
            // 汇总每个目标
            for (int ei = 0; ei < NE; ei++) {
                double ps[3]; int n = 0;
                for (int k = 0; k < 3; k++) if (tg[k] == ei) ps[n++] = ph[ei][k][sel[k]];
                if (!n) continue;
                int need = (en[ei].hp + g.damage - 1) / g.damage;
                double pk = atLeast(ps, n, need), eh = 0;
                for (int i = 0; i < n; i++) eh += ps[i];
                val += pk * (PR.VK + (en[ei].flag >= 0 ? PR.VKC : 0)) + min(eh, (double)need) * PR.VHD;
            }
            // 拾旗替代攻击
            for (int k = 0; k < 3; k++) if (tg[k] < 0 && my[k].active && pickAt[k][sel[k]]) val += pickB[k][sel[k]];
            if (val > bestAtk) { bestAtk = val; bt[0] = tg[0]; bt[1] = tg[1]; bt[2] = tg[2]; }
        }
        score += bestAtk;
        if (score > bestScore) { bestScore = score; for (int k = 0; k < 3; k++) { bestSel[k] = sel[k]; bestTgt[k] = bt[k]; } }
    }

    // ---- 输出 ----
    string out = to_string(t);
    string notes;
    for (int k = 0; k < 3; k++) {
        My& m = my[k];
        if (!m.active) { out += " S -"; notes += (k ? ";" : "") + string("dead"); continue; }
        int mv = m.mv[bestSel[k]];
        string act = "-";
        if (bestTgt[k] >= 0) act = to_string(en[bestTgt[k]].id);
        else act = "P";
        out += string(" ") + MVC[mv] + " " + act;
        notes += (k ? ";" : "") + m.note;
    }
    out += " # " + notes;
    return out;
}

int main() {
    ios::sync_with_stdio(false);
    Game g;
    while (read_turn(g)) {
        string out;
        try { out = decide(g); }
        catch (...) { out = to_string(g.turn) + " S - S - S -"; }
        cout << out << endl;
    }
}
