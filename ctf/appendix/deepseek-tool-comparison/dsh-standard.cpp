// 夺旗 bot：任务竞价的三角色协同
//  - 携旗者走最快路线回基地（速度优先，不绕路）
//  - 队友护卫我方携旗者；空闲角色拦截敌方携旗者（4 方以上积极追猎）
//  - 抢旗：只抢抢得到的（比敌人先到，或很近），抢不到就去站旗点等刷新
//  - 站位：按旗点被抢概率加权，选期望收益最高的空旗点，分头驻守
//  - 攻击集火：优先携旗者 / 一击必杀 / 威胁我方携旗者的敌人，避免伤害溢出
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

static const int DX4[4] = {0, 0, -1, 1};
static const int DY4[4] = {-1, 1, 0, 0};
static const char MVC[4] = {'U', 'D', 'L', 'R'};
static const int INF = 1000000000;

struct Bot {
    int S = 0, me = 0;
    vector<char> wall;
    vector<vector<int>> baseCells, spawnCells;
    vector<int> spotCells;
    vector<int> dHome;
    bool inited = false;

    int cellOf(int x, int y) const { return y * S + x; }
    bool inb(int x, int y) const { return x >= 0 && y >= 0 && x < S && y < S; }

    static bool stableLess(int a, int b, int S, int bx, int by, int cx, int cy) {
        int ax = a % S, ay = a / S, x2 = b % S, y2 = b / S;
        int da = abs(ax - cx) + abs(ay - cy), db = abs(x2 - cx) + abs(y2 - cy);
        if (da != db) return da < db;
        return abs(ax - bx) + 2 * abs(ay - by) < abs(x2 - bx) + 2 * abs(y2 - by);
    }

    void init(const Game& g) {
        S = g.size; me = g.me;
        wall.assign(S * S, 0);
        for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) wall[cellOf(x, y)] = (g.map[y][x] == '#');
        int cx = S / 2, cy = S / 2;
        baseCells.assign(g.teams, {});
        spawnCells.assign(g.teams, {});
        for (int t = 0; t < g.teams; t++) {
            int bx = g.bases[t].first, by = g.bases[t].second;
            vector<int> cs;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                if (!inb(bx + dx, by + dy)) continue;
                cs.push_back(cellOf(bx + dx, by + dy));
            }
            baseCells[t] = cs;
            stable_sort(cs.begin(), cs.end(), [&](int a, int b) { return stableLess(a, b, S, bx, by, cx, cy); });
            spawnCells[t] = cs;
        }
        for (auto& s : g.spots) spotCells.push_back(cellOf(s.first, s.second));
        dHome = bfs(baseCells[me]);
        inited = true;
    }

    vector<int> bfs(const vector<int>& src) const {
        vector<int> d(S * S, -1);
        vector<int> q;
        q.reserve(S * S);
        for (int c : src) if (c >= 0 && !wall[c] && d[c] < 0) { d[c] = 0; q.push_back(c); }
        for (size_t i = 0; i < q.size(); i++) {
            int c = q[i], x = c % S, y = c / S;
            for (int k = 0; k < 4; k++) {
                int nx = x + DX4[k], ny = y + DY4[k];
                if (!inb(nx, ny)) continue;
                int n = ny * S + nx;
                if (wall[n] || d[n] >= 0) continue;
                d[n] = d[c] + 1;
                q.push_back(n);
            }
        }
        return d;
    }
    vector<int> bfs1(int c) const { return bfs(vector<int>{c}); }

    vector<int> dijkstra(const vector<int>& src, const vector<int>& cost) const {
        vector<int> d(S * S, INF);
        priority_queue<pair<int, int>, vector<pair<int, int>>, greater<pair<int, int>>> pq;
        for (int c : src) if (c >= 0 && !wall[c] && d[c] > 0) { d[c] = 0; pq.push({0, c}); }
        while (!pq.empty()) {
            pair<int, int> top = pq.top(); pq.pop();
            int dd = top.first, c = top.second;
            if (dd > d[c]) continue;
            int x = c % S, y = c / S;
            for (int k = 0; k < 4; k++) {
                int nx = x + DX4[k], ny = y + DY4[k];
                if (!inb(nx, ny)) continue;
                int n = ny * S + nx;
                if (wall[n]) continue;
                int nd = dd + 1 + cost[n];
                if (nd < d[n]) { d[n] = nd; pq.push({nd, n}); }
            }
        }
        return d;
    }

    bool canAttack(int ax, int ay, int bx, int by) const {
        int dx = bx - ax, dy = by - ay;
        if (abs(dx) + abs(dy) > 2) return false;
        if (abs(dx) == 2 && dy == 0) return !wall[cellOf(ax + dx / 2, ay)];
        if (abs(dy) == 2 && dx == 0) return !wall[cellOf(ax, ay + dy / 2)];
        if (abs(dx) == 1 && abs(dy) == 1) return !wall[cellOf(ax + dx, ay)] || !wall[cellOf(ax, ay + dy)];
        return true;
    }

    int stepDown(int from, const vector<int>& pot, const vector<int>& danger) const {
        int x = from % S, y = from / S;
        int cur = (pot[from] >= INF) ? INF : pot[from];
        int bestVal = cur, bestDg = danger[from], bestMv = -1;
        for (int k = 0; k < 4; k++) {
            int nx = x + DX4[k], ny = y + DY4[k];
            if (!inb(nx, ny)) continue;
            int n = ny * S + nx;
            if (wall[n] || pot[n] >= INF) continue;
            int v = pot[n];
            if (v < bestVal || (v == bestVal && danger[n] < bestDg)) { bestVal = v; bestDg = danger[n]; bestMv = k; }
        }
        if (bestMv >= 0 && bestVal >= cur) return -1;
        return bestMv;
    }
    int cellAt(int c, int mv) const { return c + DY4[mv] * S + DX4[mv]; }
};

struct Cmd { char mv = 'S'; string act = "-"; };

enum { T_NONE = 0, T_FETCH, T_HOME, T_DEFEND, T_HUNT, T_CAMP };

int main() {
    ios::sync_with_stdio(false);
    Game g;
    Bot bot;
    while (read_turn(g)) {
        if (!bot.inited) bot.init(g);
        int T = g.turn;
        int N = (int)g.units.size();
        int S = bot.S, me = bot.me;
        const int DMG = g.damage;
        const int MELEE = 2;

        // ---------- 有效位置（含本回合重生者）----------
        vector<int> pos(N, -1);
        vector<char> alive(N, 0), imm(N, 0);
        {
            vector<char> occ(S * S, 0);
            for (auto& u : g.units) if (u.x >= 0) { pos[u.id] = bot.cellOf(u.x, u.y); alive[u.id] = 1; occ[pos[u.id]] = 1; }
            for (auto& u : g.units) if (!alive[u.id] && u.respawn_at >= 0 && u.respawn_at <= T) {
                for (int c : bot.spawnCells[u.team]) if (!occ[c]) { pos[u.id] = c; alive[u.id] = 1; imm[u.id] = 1; occ[c] = 1; break; }
            }
        }
        vector<int> enemyCells, myCells;
        for (auto& u : g.units) if (alive[u.id]) { if (u.team == me) myCells.push_back(pos[u.id]); else enemyCells.push_back(pos[u.id]); }
        vector<int> dEnemy = bot.bfs(enemyCells);

        // ---------- 威胁图 ----------
        vector<int> danger(S * S, 0);
        for (auto& u : g.units) {
            if (u.team == me || !alive[u.id] || imm[u.id]) continue;
            int ex = pos[u.id] % S, ey = pos[u.id] / S;
            for (int dx = -2; dx <= 2; dx++) for (int dy = -2; dy <= 2; dy++) {
                int md = abs(dx) + abs(dy);
                if (md > 2) continue;
                int tx = ex + dx, ty = ey + dy;
                if (!bot.inb(tx, ty)) continue;
                int c = bot.cellOf(tx, ty);
                if (bot.wall[c]) continue;
                if (bot.canAttack(ex, ey, tx, ty)) danger[c] += 2;
            }
            for (int dx = -3; dx <= 3; dx++) for (int dy = -3; dy <= 3; dy++) {
                if (abs(dx) + abs(dy) != 3) continue;
                int tx = ex + dx, ty = ey + dy;
                if (!bot.inb(tx, ty)) continue;
                int c = bot.cellOf(tx, ty);
                if (!bot.wall[c]) danger[c] += 1;
            }
        }

        // ---------- 旗子 ----------
        int nReturn = 0, nEmptySpot = 0;
        for (auto& f : g.flags) {
            if ((f.status == "dropped" || f.status == "cooldown") && f.return_at == T) nReturn++;
            if (f.status != "home") nEmptySpot++;
        }
        (void)nEmptySpot;
        struct FI { int id, cell; vector<int> d; int enArr, en3, rt; };
        vector<FI> fis;
        for (auto& f : g.flags) {
            if (f.status != "home" && f.status != "dropped") continue;
            if (f.x < 0) continue;
            FI fi; fi.id = f.id; fi.cell = bot.cellOf(f.x, f.y);
            fi.d = bot.bfs1(fi.cell);
            fi.enArr = INF; fi.en3 = 0;
            for (auto& u : g.units) {
                if (u.team == me || !alive[u.id] || g.units[u.id].flag >= 0) continue;
                int dd = fi.d[pos[u.id]];
                if (dd < 0) continue;
                fi.enArr = min(fi.enArr, dd);
                if (dd <= 3) fi.en3++;
            }
            fi.rt = bot.dHome[fi.cell];
            if (fi.rt < 0) fi.rt = 60;
            fis.push_back(fi);
        }

        // ---------- 我的角色 ----------
        int myU[3] = {3 * me, 3 * me + 1, 3 * me + 2};
        bool carry[3] = {false, false, false}, live[3] = {false, false, false};
        int hp[3] = {0, 0, 0};
        for (int k = 0; k < 3; k++) {
            live[k] = alive[myU[k]];
            if (live[k]) { hp[k] = g.units[myU[k]].hp; carry[k] = g.units[myU[k]].flag >= 0; }
        }

        // 每个我方角色到各处的距离场（按需）
        struct Task { int type = T_NONE; int target = -1; double util = 0; int aux = -1; };
        Task task[3];
        int tref[3] = {-1, -1, -1};

        // ---------- 1. 携旗者：回基地 ----------
        for (int k = 0; k < 3; k++) if (live[k] && carry[k]) task[k] = {T_HOME, -1, 0, 0};

        // ---------- 2. 护卫：我方携旗者受威胁 ----------
        {
            vector<char> assigned(3, 0);
            for (int k = 0; k < 3; k++) if (task[k].type != T_NONE) assigned[k] = 1;
            for (int k = 0; k < 3; k++) {
                if (!live[k] || !carry[k]) continue;
                vector<int> dc = bot.bfs1(pos[myU[k]]);
                // 找我方携旗者附近的敌人
                vector<pair<int, int>> threats;  // (dist, uid)
                for (auto& u : g.units) {
                    if (u.team == me || !alive[u.id] || imm[u.id]) continue;
                    int dd = dc[pos[u.id]];
                    if (dd >= 0 && dd <= 5) threats.push_back({dd, u.id});
                }
                if (threats.empty()) continue;
                sort(threats.begin(), threats.end());
                int nDef = (threats.size() >= 2 || hp[k] <= 68) ? 2 : 1;
                for (int q = 0; q < nDef && q < (int)threats.size(); q++) {
                    int e = threats[q].second;
                    vector<int> de = bot.bfs1(pos[e]);
                    int bestK = -1, bestD = INF;
                    for (int j = 0; j < 3; j++) {
                        if (!live[j] || carry[j] || assigned[j]) continue;
                        int dd = de[pos[myU[j]]];
                        if (dd >= 0 && dd < bestD) { bestD = dd; bestK = j; }
                    }
                    if (bestK < 0 || bestD > 7) continue;
                    task[bestK] = {T_DEFEND, e, 1.0 / (1.0 + bestD), k};
                    assigned[bestK] = 1;
                }
            }
        }

        // ---------- 3. 抢旗 ----------
        {
            vector<char> usedF(g.flags.size(), 0), usedU(3, 0);
            for (int k = 0; k < 3; k++) if (task[k].type != T_NONE) usedU[k] = 1;
            struct Pair { double u; int k, f; };
            vector<Pair> ps;
            for (int k = 0; k < 3; k++) {
                if (!live[k] || carry[k] || usedU[k]) continue;
                for (int j = 0; j < (int)fis.size(); j++) {
                    int d = fis[j].d[pos[myU[k]]];
                    if (d < 0) continue;
                    double p = 1.0;
                    if (fis[j].enArr <= d) p = (fis[j].enArr == d) ? 0.25 : 0.06;
                    // 抢不到又远：别白跑，去站位更好
                    if (fis[j].enArr <= d && d > 4) continue;
                    double risk = 1.0 / (1.0 + 0.35 * fis[j].en3);
                    double util = p * risk / (double)max(4, d + fis[j].rt);
                    ps.push_back({util, k, j});
                }
            }
            sort(ps.begin(), ps.end(), [](const Pair& a, const Pair& b) { return a.u > b.u; });
            for (auto& p : ps) {
                if (usedU[p.k] || usedF[p.f]) continue;
                task[p.k] = {T_FETCH, p.f, p.u, -1};
                usedU[p.k] = 1; usedF[p.f] = 1;
            }
        }

        // ---------- 敌方携旗者 ----------
        struct Carrier { int uid, cell, homeDist; };
        vector<Carrier> ecar;
        vector<vector<int>> ecarD;
        for (auto& u : g.units) {
            if (u.team == me || !alive[u.id] || g.units[u.id].flag < 0) continue;
            vector<int> eb = bot.bfs(bot.baseCells[u.team]);
            int hd = eb[pos[u.id]];
            if (hd < 0) hd = 60;
            ecar.push_back({u.id, pos[u.id], hd});
            ecarD.push_back(bot.bfs1(pos[u.id]));
        }
        auto hitsNeeded = [&](int h) { return (h + DMG - 1) / DMG; };

        // ---------- 4. 拦截敌方携旗者 ----------
        // 人多时旗子几乎总在被携带，抢不到旗就等于丢分，所以积极追猎；
        // 人数少时（2-3 方）追猎不如自己抢旗，保持原来的保守条件。
        {
            vector<char> usedU(3, 0);
            for (int k = 0; k < 3; k++) if (task[k].type != T_NONE) usedU[k] = 1;
            bool aggressive = g.teams >= 4;
            for (size_t i = 0; i < ecar.size(); i++) {
                if (!aggressive && ecar[i].homeDist > 14) continue;
                int bestK = -1; double bestU = 0;
                for (int k = 0; k < 3; k++) {
                    if (!live[k] || carry[k] || usedU[k]) continue;
                    int du = ecarD[i][pos[myU[k]]];
                    if (du < 0) continue;
                    int need = hitsNeeded(g.units[ecar[i].uid].hp);
                    if (aggressive) {
                        if (du > 10) continue;
                        int avail = ecar[i].homeDist + 1 - max(0, du - MELEE);
                        if (avail < 1) continue;
                        double util = 1.0 / (double)max(4, du + need);
                        if (util > bestU) { bestU = util; bestK = k; }
                    } else {
                        int avail = ecar[i].homeDist - max(0, du - MELEE);
                        double pk = avail <= 0 ? 0.0 : min(1.0, (double)avail / need);
                        double util = 1.0 * pk / (double)max(3, du + 2);
                        if (util > bestU) { bestU = util; bestK = k; }
                    }
                }
                if (bestK >= 0 && bestU > 0.02) { task[bestK] = {T_HUNT, ecar[i].uid, bestU, -1}; usedU[bestK] = 1; }
            }
        }

        // ---------- 5. 驻守旗点 ----------
        {
            // 每个旗点的距离场
            vector<vector<int>> spotDist(bot.spotCells.size());
            for (size_t i = 0; i < bot.spotCells.size(); i++) {
                bool got = false;
                for (auto& fi : fis) if (fi.cell == bot.spotCells[i]) { spotDist[i] = fi.d; got = true; break; }
                if (!got) spotDist[i] = bot.bfs1(bot.spotCells[i]);
            }
            double pRet = nReturn > 0 ? (double)nReturn / (double)max(1, (int)bot.spotCells.size()) : 0.0;
            vector<char> usedU(3, 0), usedSpot(bot.spotCells.size(), 0);
            for (int k = 0; k < 3; k++) if (task[k].type != T_NONE) usedU[k] = 1;
            // 每个空闲角色的最佳驻守目标
            for (int k = 0; k < 3; k++) {
                if (!live[k] || carry[k] || usedU[k]) continue;
                double bestV = -1e18; int bestI = -1;
                for (size_t i = 0; i < bot.spotCells.size(); i++) {
                    if (usedSpot[i]) continue;
                    int dd = spotDist[i][pos[myU[k]]];
                    if (dd < 0) continue;
                    double val = 0;
                    for (size_t j = 0; j < bot.spotCells.size(); j++) {
                        int d2 = spotDist[j][bot.spotCells[i]];
                        if (d2 < 0) d2 = 40;
                        val -= (double)(d2 + bot.dHome[bot.spotCells[j]]) / (double)bot.spotCells.size();
                    }
                    val += pRet * 6.0;
                    val -= 0.4 * dd;
                    if (val > bestV) { bestV = val; bestI = (int)i; }
                }
                if (bestI >= 0) { task[k] = {T_CAMP, bot.spotCells[bestI], bestV, -1}; usedSpot[bestI] = 1; usedU[k] = 1; }
            }
            // 兜底：没有任务的角色去最近的旗点，避免原地发呆
            for (int k = 0; k < 3; k++) {
                if (!live[k] || carry[k] || task[k].type != T_NONE) continue;
                int bestC = -1, bestD = INF;
                for (size_t i = 0; i < bot.spotCells.size(); i++) {
                    int dd = spotDist[i][pos[myU[k]]];
                    if (dd >= 0 && dd < bestD) { bestD = dd; bestC = bot.spotCells[i]; }
                }
                if (bestC >= 0) task[k] = {T_CAMP, bestC, 0, -1};
                else task[k] = {T_HOME, -1, 0, -1};
            }
        }

        // ---------- 生成移动 ----------
        Cmd cmd[3];
        vector<int> post = pos;
        vector<int> cost(S * S, 0);
        for (int k = 0; k < 3; k++) {
            if (!live[k]) continue;
            int from = pos[myU[k]];
            int tgt = -1;
            bool isCarrier = carry[k];
            if (task[k].type == T_HOME) {
                vector<char> inBase(S * S, 0);
                for (int bc : bot.baseCells[me]) inBase[bc] = 1;
                for (int c = 0; c < S * S; c++) {
                    int de = dEnemy[c];
                    int cst = 0;
                    if (de >= 0 && de <= 1) cst += 3;
                    else if (de == 2) cst += 1;
                    // 队友挡路时绕开（基地格除外，基地有 9 格）
                    if (!inBase[c]) for (int j = 0; j < 3; j++) if (j != k && live[j] && pos[myU[j]] == c) cst += 25;
                    cost[c] = cst;
                }
                vector<int> pot = bot.dijkstra(bot.baseCells[me], cost);
                int mv = bot.stepDown(from, pot, danger);
                if (mv >= 0) { cmd[k].mv = MVC[mv]; post[myU[k]] = bot.cellAt(from, mv); }
                continue;
            }
            if (task[k].type == T_FETCH) tgt = fis[task[k].target].cell;
            else if (task[k].type == T_DEFEND || task[k].type == T_HUNT) {
                int uid = task[k].target;
                tgt = pos[uid];
                if (tgt < 0) tgt = -1;
            } else if (task[k].type == T_CAMP) tgt = task[k].target;
            if (tgt < 0) continue;
            for (int c = 0; c < S * S; c++) {
                int dg = min(danger[c], 10);
                int extra = 0;
                // 避免与队友挤同一格
                for (int j = 0; j < 3; j++) if (j != k && live[j] && pos[myU[j]] == c) extra += 12;
                cost[c] = dg / 3 + extra;
            }
            vector<int> pot = bot.dijkstra(vector<int>{tgt}, cost);
            int mv = bot.stepDown(from, pot, danger);
            if (mv >= 0) { cmd[k].mv = MVC[mv]; post[myU[k]] = bot.cellAt(from, mv); }
            (void)isCarrier;
        }
        // 队友撞车保护
        for (int rep = 0; rep < 2; rep++) {
            for (int a = 0; a < 3; a++) for (int b = a + 1; b < 3; b++) {
                if (!live[a] || !live[b]) continue;
                if (post[myU[a]] == post[myU[b]] && pos[myU[a]] != pos[myU[b]]) {
                    // 优先级：携旗 > 护卫 > 抢旗 > 其他
                    int lo = (carry[a] ? 0 : task[a].type == T_DEFEND ? 1 : task[a].type == T_FETCH ? 2 : 3);
                    int lb = (carry[b] ? 0 : task[b].type == T_DEFEND ? 1 : task[b].type == T_FETCH ? 2 : 3);
                    int lose = (lo >= lb) ? a : b;
                    cmd[lose].mv = 'S'; post[myU[lose]] = pos[myU[lose]];
                }
                if (post[myU[a]] == pos[myU[b]] && post[myU[b]] == pos[myU[a]] && cmd[a].mv != 'S' && cmd[b].mv != 'S') {
                    int lo = (carry[a] ? 0 : 3), lb = (carry[b] ? 0 : 3);
                    int lose = (lo >= lb) ? a : b;
                    cmd[lose].mv = 'S'; post[myU[lose]] = pos[myU[lose]];
                }
            }
        }

        // ---------- 动作 ----------
        vector<char> acted(3, 0);
        double pRetCell = 0;
        if (nReturn > 0) pRetCell = (double)nReturn / (double)max(1, (int)bot.spotCells.size());
        // 敌人优先级
        vector<int> eprio(N, 0);
        for (int i = 0; i < N; i++) {
            if (g.units[i].team == me || !alive[i] || imm[i]) continue;
            int pr = 0;
            bool car = g.units[i].flag >= 0;
            int need = hitsNeeded(g.units[i].hp);
            if (car) pr += 100;
            if (need == 1) pr += 60;
            else if (need == 2) pr += 25;
            pr += (g.hp - g.units[i].hp) / 4;
            if (car) {
                vector<int> eb = bot.bfs(bot.baseCells[g.units[i].team]);
                int hd = eb[pos[i]];
                if (hd >= 0) pr += max(0, 34 - 6 * hd);
            }
            eprio[i] = pr;
        }
        // 护卫目标额外加分
        for (int k = 0; k < 3; k++) if (task[k].type == T_DEFEND) eprio[task[k].target] += 70;
        for (int k = 0; k < 3; k++) if (task[k].type == T_HUNT) eprio[task[k].target] += 30;

        struct Atk { int k, tgt, prio; };
        vector<Atk> atks;
        for (int k = 0; k < 3; k++) {
            if (!live[k] || carry[k] || imm[myU[k]]) continue;
            int px = post[myU[k]] % S, py = post[myU[k]] / S;
            for (int i = 0; i < N; i++) {
                if (g.units[i].team == me || !alive[i] || imm[i]) continue;
                int tx = post[i] % S, ty = post[i] / S;
                if (!bot.canAttack(px, py, tx, ty)) continue;
                atks.push_back({k, i, eprio[i]});
            }
        }
        sort(atks.begin(), atks.end(), [](const Atk& a, const Atk& b) { return a.prio > b.prio; });
        // 是否有高价值目标可打（用于决定 P 还是攻击）
        bool onFlagCell[3] = {false, false, false}, onSpotCell[3] = {false, false, false};
        for (int k = 0; k < 3; k++) {
            if (!live[k] || carry[k]) continue;
            int c = post[myU[k]];
            for (auto& f : g.flags) if ((f.status == "home" || f.status == "dropped") && f.x >= 0 && bot.cellOf(f.x, f.y) == c) { onFlagCell[k] = true; break; }
            for (int s : bot.spotCells) if (s == c) { onSpotCell[k] = true; break; }
        }
        vector<int> assigned(N, 0);
        for (int k = 0; k < 3; k++) {
            if (!live[k] || carry[k] || imm[myU[k]]) continue;
            // 该角色可用的最高优先级攻击
            int bestT = -1, bestP = -1;
            for (auto& a : atks) if (a.k == k) { if (a.prio > bestP) { bestP = a.prio; bestT = a.tgt; } }
            bool wantAttack = false;
            if (bestT >= 0) {
                bool valuable = false;
                if (eprio[bestT] >= 100 && hitsNeeded(g.units[bestT].hp) <= 1) valuable = true;      // 能一击杀携旗者
                if (g.units[bestT].flag >= 0 && hitsNeeded(g.units[bestT].hp) <= 1) valuable = true;
                if (task[k].type == T_DEFEND && bestT == task[k].target) valuable = true;
                if (onFlagCell[k]) valuable = true;
                if (onSpotCell[k] && pRetCell > 0.05 && !valuable) valuable = false;                   // 站旗点待旗
                else if (!onSpotCell[k]) valuable = true;
                wantAttack = valuable;
            }
            if (wantAttack) {
                int need = hitsNeeded(g.units[bestT].hp);
                if (assigned[bestT] < need || bestP >= 100) {
                    cmd[k].act = to_string(bestT);
                    acted[k] = 1;
                    assigned[bestT]++;
                    continue;
                }
            }
            if (onFlagCell[k] || (onSpotCell[k] && pRetCell > 0.02)) { cmd[k].act = "P"; acted[k] = 1; }
        }
        // 剩余可攻击者补刀
        for (auto& a : atks) {
            if (acted[a.k] || cmd[a.k].act != "-") continue;
            int need = hitsNeeded(g.units[a.tgt].hp);
            if (assigned[a.tgt] >= need && a.prio < 100) continue;
            cmd[a.k].act = to_string(a.tgt);
            acted[a.k] = 1;
            assigned[a.tgt]++;
        }
        // 站在棋点且没别的事：按 P
        for (int k = 0; k < 3; k++) {
            if (!live[k] || carry[k] || acted[k]) continue;
            if (onFlagCell[k] || (onSpotCell[k] && pRetCell > 0.02)) { cmd[k].act = "P"; acted[k] = 1; }
        }

        // ---------- 输出 ----------
        string out = to_string(T);
        for (int k = 0; k < 3; k++) { out += ' '; out += cmd[k].mv; out += ' '; out += cmd[k].act; }
        out += " #";
        for (int k = 0; k < 3; k++) {
            if (k) out += ';';
            if (!live[k]) out += "dead";
            else if (carry[k]) out += "home";
            else if (task[k].type == T_FETCH) out += "f" + to_string(task[k].target);
            else if (task[k].type == T_DEFEND) out += "def";
            else if (task[k].type == T_HUNT) out += "hunt";
            else if (task[k].type == T_CAMP) out += "camp";
            else out += "-";
        }
        cout << out << endl;
    }
    return 0;
}
