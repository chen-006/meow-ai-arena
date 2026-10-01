// 夺旗 bot
// 核心认识：
//  1) 攻击"免费"：所有攻击同时结算，阵亡者的攻击照样生效，所以出手本身不会招来额外伤害；
//     真正决定受伤的是"站位"。=> 只要够得着就打，但不要用走位去送。
//  2) 集火：3 人同打一个 = 102 伤害 = 秒杀；2 人 = 68，两回合必杀。局部以多打少是唯一稳定赚的交换。
//  3) 携旗者不能攻击 => 猎杀携旗者零风险（阻止 1 分 + 旗子落地可抢）。
//  4) 34 伤害 ≈ 0.24 分，1 步路程 ≈ 0.04 分 => 值得绕几步路避开一次挨打。
//     用"危险加权 Dijkstra"规划路线，绕开敌方火力覆盖区；血量低时权重急剧升高（逃跑）。
//  5) 旗子随机刷新在旗点，"去拿旗"这一段取决于我在哪，"送回家"这一段与位置无关
//     => 空闲时停在到各旗点距离和最小处（中心附近）。
//  6) 移动与动作相互独立：一边赶路一边出手。
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

// ================================ 可调参数 ================================
static double W_BASE   = 0.12;    // 每点预期伤害折算成多少"步"（多方混战需要绕开火力网）
static double W_HURT2  = 0.16;    // 只剩 2 下就死
static double W_HURT1  = 0.85;    // 只剩 1 下就死
static double W_CARRY  = 1.45;    // 携旗时更怕死
static double W_SUP    = 0.55;    // 附近有队友支援时更敢打
static double LETHAL   = 6.0;     // "这一格会让我当场被打死"的额外代价（步），大队数时用这个
static double DANGCAP  = 0.0;     // 单格危险代价上限（步），大队数时用这个
static double LETHALS  = 60.0;    // 小队伍（<=5 方）额外加上的必死格代价
static double DANGCAPS = 3.0;     // 小队伍额外加上的单格危险上限
// 人少时火力点稀疏、绕得开，值得按伤害绕路；人多时满地都是危险，绕路只会既丢旗又丢击杀。
// 用 (8-n)/6 在两者之间线性过渡：n=2 完全按小队伍处理，n>=8 完全按大队数处理。
static double DANGMID  = 8.0;     // 过渡中点
static double KILLVAL  = 3.0;     // 秒杀一个敌人的收益
static double CARRBON  = 1.6;     // 打携旗者的额外收益
static double DMGVAL   = 0.024;   // 每点伤害收益
static double HUNTSC   = 0.85;    // 猎杀价值系数
static double HUNTMIN  = 0.010;   // 猎杀最低阈值
static double FOCUSP   = 1.7;     // 集火凸度：伤害越接近击杀线，价值增长越快
static double PICKSAFE = 0.0;     // 实测：犹豫等清场丢掉的旗远多于被伏击的损失，所以默认立刻拿
static double PICKRAD  = 4.0;     // 判断"附近有没有敌人"的半径
static double HUNTNT   = 1.0;     // 猎杀价值随阵营数衰减的强度（多方时抢旗远比追杀划算）
static double HUNTDIST = 20.0;    // 2 方时愿意花多少回合去拦截；多方按 2/n 衰减
                                  // 实测 20 优于 12：2 方 23.6->25.1，8 方 rank 5.12->4.38
static double RUNMIN   = 0.0;     // 抢旗收益门槛。必须为 0：多方时"总有敌人更近"，设门槛会导致完全不去抢旗
static double SUPFIRE  = 1.0;     // 是否为了集火而主动靠拢已经打起来的敌人
static double POSBIAS  = 0.0;     // 占位时向自家一侧偏移的权重（多方时中心是绞肉机）
static double POSDANG  = 0.10;    // 占位时避开火力覆盖的权重
static double THOMEW   = 1.0;     // 回程距离的权重（实测 2 反而更差）
static double ESCORTN  = 0.0;     // 专职护卫实测更差：少一个抢旗的人，收益抵不上保护效果

// ================================ AI ================================
const int INF = 1000000000;

struct AI {
    Game* g = nullptr;
    int SZ = 0, NT = 0, NU = 0, NF = 0, DMG = 34, ME = 0, ARANGE = 2, NC = 0;
    vector<unsigned char> wall;
    int ctr = 0;
    vector<vector<int>> baseCells, spawnOrder, dBase, dSpot;
    vector<int> spotCells, dCtr, spotSum;
    vector<vector<int>> dF, dU;
    vector<int> dCar, dmgAt, pos, pred, enemyAt, enemyWant, flagAt, myNear;
    vector<unsigned char> imm;
    vector<int> bq;
    int carrierCell = -1;
    double dangCap = 0.0, lethalW = 6.0;
    bool inited = false;

    inline int md(int a, int b) const { return abs(a % SZ - b % SZ) + abs(a / SZ - b / SZ); }

    void bfs(const vector<int>& src, vector<int>& dist) {
        dist.assign((size_t)NC, INF);
        bq.clear();
        for (int s : src) if (s >= 0 && !wall[s] && dist[s] == INF) { dist[s] = 0; bq.push_back(s); }
        for (size_t h = 0; h < bq.size(); h++) {
            int c = bq[h], x = c % SZ, y = c / SZ, d = dist[c] + 1;
            if (x > 0)      { int n = c - 1;  if (!wall[n] && dist[n] == INF) { dist[n] = d; bq.push_back(n); } }
            if (x + 1 < SZ) { int n = c + 1;  if (!wall[n] && dist[n] == INF) { dist[n] = d; bq.push_back(n); } }
            if (y > 0)      { int n = c - SZ; if (!wall[n] && dist[n] == INF) { dist[n] = d; bq.push_back(n); } }
            if (y + 1 < SZ) { int n = c + SZ; if (!wall[n] && dist[n] == INF) { dist[n] = d; bq.push_back(n); } }
        }
    }

    // 危险加权最短路：返回每个格子到目标的最小代价（×100 取整）
    void dijk(const vector<int>& src, double w, double lethalAt, vector<int>& cost) {
        cost.assign((size_t)NC, INF);
        priority_queue<pair<int, int>, vector<pair<int, int>>, greater<pair<int, int>>> pq;
        for (int s : src) if (s >= 0 && !wall[s] && cost[s] == INF) { cost[s] = 0; pq.push({0, s}); }
        while (!pq.empty()) {
            auto [d, c] = pq.top(); pq.pop();
            if (d != cost[c]) continue;
            int x = c % SZ, y = c / SZ;
            const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
            for (int i = 0; i < 4; i++) {
                int nx = x + dx[i], ny = y + dy[i];
                if (nx < 0 || ny < 0 || nx >= SZ || ny >= SZ) continue;
                int n = ny * SZ + nx;
                if (wall[n]) continue;
                double step = 1.0 + min(dmgAt[n] * w, dangCap);
                if (lethalAt > 0 && dmgAt[n] >= lethalAt) step += lethalW;
                int nd = d + (int)(step * 100.0 + 0.5);
                if (nd < cost[n]) { cost[n] = nd; pq.push({nd, n}); }
            }
        }
    }

    void initStatic() {
        SZ = g->size; NT = g->teams; ME = g->me; DMG = g->damage; ARANGE = 2; NC = SZ * SZ;
        double fSc = max(0.0, min(1.0, (DANGMID - NT) / 6.0));
        dangCap = DANGCAP + DANGCAPS * fSc;
        lethalW = LETHAL + LETHALS * fSc;
        wall.assign((size_t)NC, 0);
        for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) wall[(size_t)y * SZ + x] = (g->map[y][x] == '#');
        int c = SZ / 2; ctr = c * SZ + c;

        baseCells.assign(NT, {});
        spawnOrder.assign(NT, {});
        for (int t = 0; t < NT; t++) {
            int bx = g->bases[t].first, by = g->bases[t].second;
            vector<int> cells;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                int x = bx + dx, y = by + dy;
                if (x < 0 || y < 0 || x >= SZ || y >= SZ) continue;
                if (wall[(size_t)y * SZ + x]) continue;
                cells.push_back(y * SZ + x);
            }
            baseCells[t] = cells;
            vector<int> so = cells;
            sort(so.begin(), so.end(), [&](int a, int b) {
                int da = md(a, ctr), db = md(b, ctr);
                if (da != db) return da < db;
                int ka = abs(a % SZ - bx) + 2 * abs(a / SZ - by);
                int kb = abs(b % SZ - bx) + 2 * abs(b / SZ - by);
                if (ka != kb) return ka < kb;
                return a < b;
            });
            spawnOrder[t] = so;
        }
        spotCells.clear();
        for (auto& s : g->spots) spotCells.push_back(s.second * SZ + s.first);

        dmgAt.assign((size_t)NC, 0);
        dBase.assign(NT, {});
        for (int t = 0; t < NT; t++) bfs(baseCells[t], dBase[t]);
        bfs({ctr}, dCtr);
        dSpot.assign(spotCells.size(), {});
        for (size_t i = 0; i < spotCells.size(); i++) bfs({spotCells[i]}, dSpot[i]);
        spotSum.assign((size_t)NC, 0);
        for (int c2 = 0; c2 < NC; c2++) {
            if (wall[c2]) { spotSum[c2] = INF; continue; }
            long long s = 0;
            for (size_t i = 0; i < dSpot.size(); i++) { int d = dSpot[i][c2]; s += (d >= INF ? 60 : d); }
            spotSum[c2] = (int)s;
        }
        inited = true;
    }

    void computeDmgAt() {
        fill(dmgAt.begin(), dmgAt.end(), 0);
        for (auto& e : g->units) {
            if (e.team == ME || pos[e.id] < 0 || imm[e.id]) continue;
            int ex = pos[e.id] % SZ, ey = pos[e.id] / SZ;
            for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++) {
                int ad = abs(dx) + abs(dy);
                if (ad > 3 || ad == 0) continue;
                int nx = ex + dx, ny = ey + dy;
                if (nx < 0 || ny < 0 || nx >= SZ || ny >= SZ) continue;
                int c = ny * SZ + nx;
                if (wall[c]) continue;
                dmgAt[c] += (ad <= 2 ? DMG : (DMG * 45 / 100));
            }
        }
    }

    void buildTurn() {
        NU = (int)g->units.size(); NF = (int)g->flags.size();
        pos.assign(NU, -1);
        imm.assign(NU, 0);
        vector<unsigned char> occ((size_t)NC, 0);
        for (auto& u : g->units) if (u.x >= 0) { pos[u.id] = u.y * SZ + u.x; occ[pos[u.id]] = 1; }
        for (auto& u : g->units) {
            if (u.x < 0 && u.respawn_at >= 0 && u.respawn_at <= g->turn) {
                for (int c : spawnOrder[u.team]) if (!occ[c]) { pos[u.id] = c; occ[c] = 1; imm[u.id] = 1; break; }
            }
        }
        dF.assign(NF, {});
        for (auto& f : g->flags) if (f.x >= 0) bfs({f.y * SZ + f.x}, dF[f.id]);
        dU.assign(NU, {});
        for (int k = 0; k < 3; k++) { int id = 3 * ME + k; if (id < NU && pos[id] >= 0) bfs({pos[id]}, dU[id]); }
        flagAt.assign((size_t)NC, -1);
        for (auto& f : g->flags) if (f.x >= 0) { int c = f.y * SZ + f.x; if (flagAt[c] < 0 || f.id < flagAt[c]) flagAt[c] = f.id; }
        carrierCell = -1;
        for (int k = 0; k < 3; k++) {
            int id = 3 * ME + k;
            if (id < NU && pos[id] >= 0 && g->units[id].flag >= 0) { carrierCell = pos[id]; break; }
        }
        if (carrierCell >= 0) bfs({carrierCell}, dCar); else dCar.clear();
        computeDmgAt();

        enemyAt.assign((size_t)NC, -1);
        enemyWant.assign((size_t)NC, -1);
        pred.assign(NU, -1);
        for (auto& u : g->units) {
            if (u.team == ME || pos[u.id] < 0) continue;
            enemyAt[pos[u.id]] = u.id;
            int p = predict(u.id);
            pred[u.id] = p;
            if (p >= 0) enemyWant[p] = u.id;
        }
    }

    int predict(int e) {
        int c = pos[e];
        if (c < 0) return -1;
        const vector<int>* dm = nullptr;
        const Unit& u = g->units[e];
        if (u.flag >= 0 && !dBase[u.team].empty() && dBase[u.team][c] < INF) dm = &dBase[u.team];
        else {
            int bf = -1, bd = INF;
            for (auto& f : g->flags) if (f.x >= 0 && !dF[f.id].empty()) { int d = dF[f.id][c]; if (d < bd) { bd = d; bf = f.id; } }
            if (bf >= 0 && bd <= 40) dm = &dF[bf];
            else if (!dCar.empty() && dCar[c] < INF && dCar[c] <= 14) dm = &dCar;
            else dm = &dCtr;
        }
        int best = c, bv = (*dm)[c];
        int x = c % SZ, y = c / SZ;
        const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
        for (int i = 0; i < 4; i++) {
            int nx = x + dx[i], ny = y + dy[i];
            if (nx < 0 || ny < 0 || nx >= SZ || ny >= SZ) continue;
            int n = ny * SZ + nx;
            if (wall[n] || (*dm)[n] >= INF) continue;
            if ((*dm)[n] < bv) { bv = (*dm)[n]; best = n; }
        }
        return best;
    }

    bool canAttack(int a, int b) const {
        if (a < 0 || b < 0) return false;
        int ax = a % SZ, ay = a / SZ, bx = b % SZ, by = b / SZ;
        int dx = bx - ax, dy = by - ay;
        int d = abs(dx) + abs(dy);
        if (d == 0 || d > ARANGE) return false;
        if (abs(dx) == 2 && dy == 0) return !wall[a + dx / 2];
        if (abs(dy) == 2 && dx == 0) return !wall[a + (dy / 2) * SZ];
        if (abs(dx) == 1 && abs(dy) == 1) return !wall[a + dx] || !wall[a + dy * SZ];
        return true;
    }

    enum { R_CARRY = 0, R_RUN = 1, R_HUNT = 2, R_POS = 3, R_DEAD = 4, R_ESCORT = 5 };
    struct Plan {
        int id = -1, role = R_DEAD, flagId = -1, target = -1;
        vector<int> cost;          // ×100 的代价场
        bool useSpot = false;
    };

    vector<int> buildPath(int from, const vector<int>& dm) {
        vector<int> path;
        if (from < 0 || dm.empty() || dm[from] >= INF) return path;
        int c = from; path.push_back(c);
        for (int guard = 0; guard < 300; guard++) {
            int d = dm[c];
            if (d <= 0) break;
            int x = c % SZ, y = c / SZ, best = -1, bd = d;
            const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
            for (int i = 0; i < 4; i++) {
                int nx = x + dx[i], ny = y + dy[i];
                if (nx < 0 || ny < 0 || nx >= SZ || ny >= SZ) continue;
                int n = ny * SZ + nx;
                if (wall[n]) continue;
                if (dm[n] < bd) { bd = dm[n]; best = n; }
            }
            if (best < 0) break;
            c = best; path.push_back(c);
        }
        return path;
    }

    bool interceptCell(int u, int e, int& out, int& cost) {
        out = -1; cost = INF;
        if (dU[u].empty()) return false;
        const Unit& t = g->units[e];
        vector<int> path;
        if (t.flag >= 0) path = buildPath(pos[e], dBase[t.team]);
        else if (!dCar.empty()) path = buildPath(pos[e], dCar);
        if (path.empty()) { if (pos[e] >= 0) path = {pos[e]}; else return false; }
        int bestArr = INF, bestCell = -1;
        for (int i = 0; i < (int)path.size(); i++) {
            int d = dU[u][path[i]];
            if (d >= INF) continue;
            int arr = max(d, i);
            if (arr < bestArr) { bestArr = arr; bestCell = path[i]; }
            if (d <= i) break;
        }
        if (bestCell < 0) return false;
        out = bestCell; cost = bestArr;
        return true;
    }

    double runValue(int u, int f, int& marginOut, int& closerOut) {
        const Flag& fl = g->flags[f];
        int fc = fl.y * SZ + fl.x;
        int tgo = dF[f].empty() ? INF : dF[f][pos[u]];
        int thome = dBase[ME][fc];
        if (tgo >= INF) return -1;
        int bestE = INF, closer = 0;
        for (auto& e : g->units) {
            if (e.team == ME || pos[e.id] < 0) continue;
            int d = dF[f][pos[e.id]];
            if (d < bestE) bestE = d;
            if (d < tgo) closer++;
        }
        marginOut = (bestE >= INF ? 99 : bestE - tgo);
        closerOut = closer;
        int m = marginOut;
        double P;
        if (m >= 3) P = 0.95;
        else if (m == 2) P = 0.88;
        else if (m == 1) P = 0.78;
        else if (m == 0) P = 0.60;
        else if (m == -1) P = 0.42;
        else if (m == -2) P = 0.28;
        else P = 0.14;
        if (closer >= 2) P *= 0.75;
        if (closer >= 4) P *= 0.7;
        double val = P / (tgo + THOMEW * thome + 8.0);
        if (tgo == 0) val += 0.6;
        return val;
    }

    double dangerW(int id) {
        int hp = g->units[id].hp;
        double w = W_BASE;
        if (hp <= DMG) w = W_HURT1;
        else if (hp <= DMG * 2) w = W_HURT2;
        if (g->units[id].flag >= 0) w *= W_CARRY;
        // 队友支援
        int sup = 0;
        for (int k = 0; k < 3; k++) {
            int j = 3 * ME + k;
            if (j == id || j >= NU || pos[j] < 0 || pos[id] < 0) continue;
            if (md(pos[j], pos[id]) <= 5) sup++;
        }
        if (sup >= 1) w *= W_SUP;
        if (sup >= 2) w *= W_SUP;
        return w;
    }

    bool inMyBase(int c) const { for (int b : baseCells[ME]) if (b == c) return true; return false; }

    // 地上的旗能等 15 回合。带着旗被贴身就是死路（携旗者不能攻击、也跑不掉），
    // 所以附近还有能打的敌人时先不拿，等清场/旗子快回位/已经快到家再拿。
    bool safePickup(int id, int fin) {
        if (PICKSAFE < 0.5) return true;
        int fid = flagAt[fin];
        if (fid < 0) return false;
        const Flag& fl = g->flags[fid];
        if (fl.return_at >= 0 && fl.return_at - g->turn <= 2) return true;
        if (dBase[ME][fin] <= 3) return true;
        int rad = (int)PICKRAD;
        int threat = 0;
        for (auto& e : g->units) {
            if (e.team == ME || pos[e.id] < 0 || e.flag >= 0) continue;   // 敌方携旗者打不了人
            if (md(pos[e.id], fin) <= rad) threat++;
        }
        if (threat == 0) return true;
        int sup = 0;
        for (int k = 0; k < 3; k++) {
            int j = 3 * ME + k;
            if (j == id || j >= NU || pos[j] < 0) continue;
            if (md(pos[j], fin) <= rad + 1) sup++;
        }
        return sup >= threat;
    }

    string decide() {
        if (!inited) initStatic();
        buildTurn();

        int ids[3] = {3 * ME, 3 * ME + 1, 3 * ME + 2};
        Plan plans[3];
        vector<int> active;
        vector<int> gf;
        for (auto& f : g->flags) if (f.x >= 0) gf.push_back(f.id);

        for (int k = 0; k < 3; k++) {
            int id = ids[k];
            plans[k].id = id;
            if (id >= NU || pos[id] < 0) { plans[k].role = R_DEAD; continue; }
            active.push_back(k);
            plans[k].role = R_POS;
            if (g->units[id].flag >= 0) plans[k].role = R_CARRY;
        }

        // ---- 抢旗分配 ----
        struct Opt { double v; int k, f, margin; };
        vector<Opt> opts;
        double bestRunV[3] = {0, 0, 0};
        for (int k : active) {
            if (plans[k].role == R_CARRY) continue;
            for (int f : gf) {
                int mg, cl;
                double v = runValue(ids[k], f, mg, cl);
                if (v > 0) { opts.push_back({v, k, f, mg}); if (v > bestRunV[k]) bestRunV[k] = v; }
            }
        }
        sort(opts.begin(), opts.end(), [](const Opt& a, const Opt& b) { return a.v > b.v; });
        vector<int> flagTaken(NF, 0);
        int nFree = 0;
        for (int k : active) if (plans[k].role != R_CARRY) nFree++;
        for (auto& o : opts) {
            if (plans[o.k].role != R_POS) continue;
            if (o.v < RUNMIN) break;                       // 收益太低，宁可占位等下一次刷新
            int cap = 1;
            if ((int)gf.size() < nFree && o.margin <= 1) cap = 2;
            if (flagTaken[o.f] >= cap) continue;
            plans[o.k].role = R_RUN;
            plans[o.k].flagId = o.f;
            plans[o.k].target = g->flags[o.f].y * SZ + g->flags[o.f].x;
            flagTaken[o.f]++;
        }

        // ---- 其余：猎杀 / 占位 ----
        // 多方混战时，阻止某一家得 1 分只相当于自己得 1 分的 1/(n-1)（两两胜负拆解），
        // 所以阵营越多越应该专心抢旗、少追杀。
        double ntScale = pow(2.0 / max(2, NT), HUNTNT);
        double hmax = HUNTDIST * ntScale;
        for (int k : active) {
            if (plans[k].role != R_POS) continue;
            int id = ids[k];
            int bestE = -1, bestCell = -1, bestC = INF;
            // 敌方携旗者：阻止 1 分 + 旗子落地可抢
            for (auto& e : g->units) {
                if (e.team == ME || pos[e.id] < 0 || e.flag < 0) continue;
                int cell, c2;
                if (!interceptCell(id, e.id, cell, c2)) continue;
                if (c2 > hmax) continue;
                if (c2 < bestC) { bestC = c2; bestE = e.id; bestCell = cell; }
            }
            // 威胁我方携旗者的敌人
            if (carrierCell >= 0 && !dCar.empty()) {
                for (auto& e : g->units) {
                    if (e.team == ME || pos[e.id] < 0 || dCar[pos[e.id]] > 7) continue;
                    int cell, c2;
                    if (!interceptCell(id, e.id, cell, c2)) continue;
                    if (c2 > hmax) continue;
                    if (c2 < bestC) { bestC = c2; bestE = e.id; bestCell = cell; }
                }
            }
            // 已经贴住队友的敌人 => 靠拢集火（只在真能打出击杀时）
            if (SUPFIRE > 0.5) {
                for (int j = 0; j < 3; j++) {
                    int mate = 3 * ME + j;
                    if (mate >= NU || pos[mate] < 0) continue;
                    for (auto& e : g->units) {
                        if (e.team == ME || pos[e.id] < 0 || imm[e.id]) continue;
                        if (md(pos[e.id], pos[mate]) > 2) continue;
                        if (dU[id].empty() || dU[id][pos[e.id]] > 3) continue;
                        if (e.hp > DMG * 2) continue;
                        int cell, c2;
                        if (!interceptCell(id, e.id, cell, c2)) continue;
                        if (c2 > hmax) continue;
                        if (c2 < bestC) { bestC = c2; bestE = e.id; bestCell = cell; }
                    }
                }
            }
            if (bestE >= 0) {
                plans[k].role = R_HUNT;
                plans[k].target = bestCell;
            }
        }

        // ---- 护卫：多方时长途运旗几乎必被截杀，派人贴身挡开拦截者 ----
        // 1 个护卫 => 护卫/携旗者与拦截者同归于尽；2 个护卫 => 拦截者 2 回合被秒杀而携旗者能活下来。
        if (carrierCell >= 0 && ESCORTN > 0) {
            int nCarriers = 0;
            for (int k : active) if (plans[k].role == R_CARRY) nCarriers++;
            int nEsc = min(2, (int)(ESCORTN * NT));
            nEsc = min(nEsc, (int)active.size() - nCarriers);
            if (nEsc > 0) {
                vector<int> cp = buildPath(carrierCell, dBase[ME]);
                int ec = cp.size() > 2 ? cp[2] : carrierCell;
                vector<int> cand;
                for (int k : active) if (plans[k].role != R_CARRY) cand.push_back(k);
                sort(cand.begin(), cand.end(), [&](int a, int b) {
                    int pa = (plans[a].role == R_RUN) ? 1 : 0, pb = (plans[b].role == R_RUN) ? 1 : 0;
                    if (pa != pb) return pa < pb;
                    return bestRunV[a] < bestRunV[b];
                });
                for (int i = 0; i < nEsc && i < (int)cand.size(); i++) {
                    plans[cand[i]].role = R_ESCORT;
                    plans[cand[i]].target = ec;
                }
            }
        }

        // ---- 为每个单位构建代价场 ----
        for (int k : active) {
            int id = ids[k];
            double w = dangerW(id);
            double leth = (double)g->units[id].hp;
            if (plans[k].role == R_CARRY) dijk(baseCells[ME], w, leth, plans[k].cost);
            else if (plans[k].role == R_RUN) dijk({plans[k].target}, w, leth, plans[k].cost);
            else if (plans[k].role == R_HUNT) dijk({plans[k].target}, w * 0.6, leth, plans[k].cost);
            else if (plans[k].role == R_ESCORT) dijk({plans[k].target}, w * 0.4, leth, plans[k].cost);
            else plans[k].useSpot = true;
        }

        // ---- 联合移动枚举 ----
        int na = (int)active.size();
        int total = 1; for (int i = 0; i < na; i++) total *= 5;
        const int DX[5] = {0, 0, 0, -1, 1};
        const int DY[5] = {0, -1, 1, 0, 0};
        double bestTotal = -1e18;
        int bestFin[3] = {-1, -1, -1};
        vector<int> bestTgt(3, -1);

        for (int combo = 0; combo < total; combo++) {
            int mv[3], tmp = combo;
            for (int i = 0; i < na; i++) { mv[i] = tmp % 5; tmp /= 5; }
            int start[3], want[3], fin[3];
            unsigned char blk[3] = {0, 0, 0};
            for (int i = 0; i < na; i++) {
                int id = ids[active[i]];
                start[i] = pos[id];
                int nx = start[i] % SZ + DX[mv[i]], ny = start[i] / SZ + DY[mv[i]];
                int w2 = start[i];
                if (nx >= 0 && ny >= 0 && nx < SZ && ny < SZ) { int c = ny * SZ + nx; if (!wall[c]) w2 = c; }
                want[i] = w2;
            }
            for (int i = 0; i < na; i++) for (int j = i + 1; j < na; j++) if (want[i] == want[j]) { blk[i] = blk[j] = 1; }
            for (int i = 0; i < na; i++) {
                int w2 = want[i];
                if (w2 == start[i]) continue;
                int ea = enemyAt[w2];
                if (ea >= 0 && enemyWant[ea] == w2) { blk[i] = 1; continue; }
                if (ea >= 0 && enemyWant[ea] == start[i]) { blk[i] = 1; continue; }
                if (ea < 0 && enemyWant[w2] >= 0) { blk[i] = 1; continue; }
                for (int j = 0; j < na; j++) if (j != i && start[j] == w2 && (blk[j] || want[j] == start[j])) { blk[i] = 1; break; }
            }
            for (int i = 0; i < na; i++) fin[i] = blk[i] ? start[i] : want[i];

            double sc = 0;
            for (int i = 0; i < na; i++) {
                int k = active[i], id = ids[k];
                const Plan& p = plans[k];
                if (p.useSpot) {
                    sc -= spotSum[fin[i]] / (double)max(1, (int)spotCells.size());
                    sc -= POSBIAS * dBase[ME][fin[i]];
                    sc -= min(dmgAt[fin[i]] * POSDANG, 3.0);
                } else {
                    double d = p.cost.empty() ? 12000 : p.cost[fin[i]];
                    if (d >= INF) d = 40000;
                    sc -= d / 100.0;
                    if (p.role == R_CARRY && inMyBase(fin[i])) sc += 60;
                    if (p.role == R_RUN && fin[i] == p.target) sc += safePickup(id, fin[i]) ? 40 : 24;
                    if (p.role == R_ESCORT && carrierCell >= 0 && md(fin[i], carrierCell) <= 2) sc += 4;
                }
            }
            vector<int> tgt(3, -1);
            sc += attackScore(active, na, fin, tgt);
            if (sc > bestTotal) {
                bestTotal = sc;
                for (int i = 0; i < 3; i++) bestFin[i] = -1;
                bestTgt.assign(3, -1);
                for (int i = 0; i < na; i++) { bestFin[active[i]] = fin[i]; bestTgt[active[i]] = tgt[i]; }
            }
        }

        // ---- 输出 ----
        string out = to_string(g->turn);
        string notes[3];
        for (int k = 0; k < 3; k++) {
            int id = ids[k];
            if (plans[k].role == R_DEAD) { out += " S -"; notes[k] = "dead"; continue; }
            int fin = bestFin[k];
            char mvc = 'S';
            int sx = pos[id] % SZ, sy = pos[id] / SZ, fx = fin % SZ, fy = fin / SZ;
            if (fx == sx && fy == sy) mvc = 'S';
            else if (fy == sy - 1) mvc = 'U';
            else if (fy == sy + 1) mvc = 'D';
            else if (fx == sx - 1) mvc = 'L';
            else if (fx == sx + 1) mvc = 'R';

            string act = "-";
            if (!imm[id] && g->units[id].flag < 0) {
                int tgt = bestTgt[k];
                if (flagAt[fin] >= 0 && safePickup(id, fin)) act = "P";
                else if (tgt >= 0) act = to_string(tgt);
            }
            out += " "; out += mvc; out += " "; out += act;
            notes[k] = plans[k].role == R_CARRY ? "carry" : plans[k].role == R_RUN ? "run" :
                       plans[k].role == R_HUNT ? "hunt" : plans[k].role == R_ESCORT ? "esc" : "pos";
        }
        out += " #";
        for (int k = 0; k < 3; k++) { if (k) out += ";"; out += notes[k]; }
        return out;
    }

    double attackScore(const vector<int>& active, int na, int fin[3], vector<int>& tgt) {
        // 出手没有任何站位代价（伤害只由位置决定，且阵亡者的攻击照样生效），
        // 所以只要结算后够得着就打；距离 1 几乎必中，距离 2 约 4–6 成命中，按期望价值取舍。
        vector<vector<pair<int, double>>> cand(3);   // (敌人, 命中概率)
        for (int k = 0; k < 3; k++) tgt[k] = -1;
        for (int i = 0; i < na; i++) {
            int k = active[i], id = 3 * ME + k;
            if (imm[id] || g->units[id].flag >= 0) continue;
            for (auto& e : g->units) {
                if (e.team == ME || pos[e.id] < 0 || imm[e.id]) continue;
                if (!canAttack(fin[i], pos[e.id])) continue;
                int d = md(fin[i], pos[e.id]);
                double p = (d <= 1) ? 1.0 : 0.50;
                if (d > 1) {
                    int pr = pred[e.id];
                    if (pr >= 0 && canAttack(fin[i], pr)) p = 0.68;   // 预判一致，更有把握
                    if (pr == pos[e.id]) p = 0.85;                    // 预计它不动
                }
                cand[k].push_back({e.id, p});
            }
            if (cand[k].size() > 6) {
                const auto& U = g->units;
                sort(cand[k].begin(), cand[k].end(), [&U](const pair<int, double>& a, const pair<int, double>& b) {
                    return U[a.first].hp < U[b.first].hp;
                });
                cand[k].resize(6);
            }
        }
        int sz[3] = {1, 1, 1};
        for (int k = 0; k < 3; k++) sz[k] = (int)cand[k].size() + 1;
        int tot = sz[0] * sz[1] * sz[2];
        if (tot <= 1) return 0;
        double best = 0; vector<int> bestT(3, -1);
        for (int c = 0; c < tot; c++) {
            int cc = c, pick[3] = {-1, -1, -1};
            double pp[3] = {0, 0, 0};
            for (int k = 0; k < 3; k++) {
                int v = cc % sz[k]; cc /= sz[k];
                if (v > 0) { pick[k] = cand[k][v - 1].first; pp[k] = cand[k][v - 1].second; }
            }
            vector<pair<int, double>> acc;
            for (int k = 0; k < 3; k++) if (pick[k] >= 0) {
                bool f2 = false;
                for (auto& pr : acc) if (pr.first == pick[k]) { pr.second += pp[k]; f2 = true; break; }
                if (!f2) acc.push_back({pick[k], pp[k]});
            }
            double val = 0;
            for (auto& pr : acc) {
                int e = pr.first;
                double ed = pr.second * DMG;          // 期望伤害
                int hp = g->units[e].hp;
                double r = min(1.0, ed / hp);         // 距击杀线的进度（凸函数 => 鼓励集火）
                double v = DMGVAL * ed + KILLVAL * pow(r, FOCUSP);
                if (g->units[e].flag >= 0) v += CARRBON * r;
                val += v;
            }
            if (val > best) { best = val; for (int k = 0; k < 3; k++) bestT[k] = pick[k]; }
        }
        for (int k = 0; k < 3; k++) tgt[k] = bestT[k];
        return best;
    }
};

int main() {
    ios::sync_with_stdio(false);
    Game g;
    AI ai;
    ai.g = &g;
    while (read_turn(g)) {
        string s;
        try { s = ai.decide(); } catch (...) { s = to_string(g.turn) + " S - S - S -"; }
        cout << s << endl;
    }
    return 0;
}
