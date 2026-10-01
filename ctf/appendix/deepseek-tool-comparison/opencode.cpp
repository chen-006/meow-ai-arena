// 多阵营夺旗 bot v2
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

static const int DX[4] = {0, 0, -1, 1};
static const int DY[4] = {-1, 1, 0, 0};
static const int BIG = 1000000000;

struct Params {
    double fetchW = 0.45;        // 送旗距离权重
    double standBaseW = 0.45;
    double unwinnable = 300.0;
    double noneCost = 26.0;
    double prioOwn = 0.0, prioCenter = 0.5, prioOther = 5.0;
    double huntBonus = 6.0;      // 追敌折扣
    double carrierKill = 8.0;
    double lowHpKill = 6.0;
    double escortCost = 4.0;
    double threatW[3] = {0.4, 2.0, 5.0}; // hp100 / hp66 / hp<=34 （移动场里的威胁权重，步长10）
    double carrierThreat = 3.0;
    double hpPenalty = 0.0;      // 低血去远处接旗的惩罚
};

struct Bot {
    Game g;
    Params P;
    int SZ = 0;
    vector<string> mp;
    vector<pair<int, int>> myBaseCells, spawnOrd;
    pair<int, int> myBaseCenter{-1, -1}, ownSpot{-1, -1}, centerSpot{-1, -1}, midCell{-1, -1};
    vector<pair<int, int>> midCells, interceptCells;
    vector<int> dBase;
    vector<int> oppBaseDist;   // 到最近敌方基地的距离
    bool inited = false;

    inline bool inb(int x, int y) const { return x >= 0 && y >= 0 && x < SZ && y < SZ; }
    inline bool wall(int x, int y) const { return !inb(x, y) || mp[y][x] == '#'; }
    inline int idc(int x, int y) const { return y * SZ + x; }
    inline int mdist(int x1, int y1, int x2, int y2) const { return abs(x1 - x2) + abs(y1 - y2); }

    bool canHit(int x1, int y1, int x2, int y2) const {
        int dx = abs(x1 - x2), dy = abs(y1 - y2);
        if (dx + dy > 2) return false;
        if (dx == 2) return !wall((x1 + x2) / 2, y1);
        if (dy == 2) return !wall(x1, (y1 + y2) / 2);
        if (dx == 1 && dy == 1) {
            int sx = (x2 > x1) ? 1 : -1, sy = (y2 > y1) ? 1 : -1;
            return !wall(x1 + sx, y1) || !wall(x1, y1 + sy);
        }
        return true;
    }

    vector<int> bfs(int sx, int sy) const {
        vector<int> d(SZ * SZ, -1);
        if (wall(sx, sy)) return d;
        deque<int> q;
        d[idc(sx, sy)] = 0; q.push_back(idc(sx, sy));
        while (!q.empty()) {
            int c = q.front(); q.pop_front();
            int x = c % SZ, y = c / SZ;
            for (int k = 0; k < 4; k++) {
                int nx = x + DX[k], ny = y + DY[k];
                if (wall(nx, ny)) continue;
                int ni = idc(nx, ny);
                if (d[ni] < 0) { d[ni] = d[c] + 1; q.push_back(ni); }
            }
        }
        return d;
    }

    vector<int> bfsMulti(const vector<pair<int, int>>& src) const {
        vector<int> d(SZ * SZ, -1);
        deque<int> q;
        for (auto& s : src) {
            if (wall(s.first, s.second)) continue;
            int i = idc(s.first, s.second);
            if (d[i] < 0) { d[i] = 0; q.push_back(i); }
        }
        while (!q.empty()) {
            int c = q.front(); q.pop_front();
            int x = c % SZ, y = c / SZ;
            for (int k = 0; k < 4; k++) {
                int nx = x + DX[k], ny = y + DY[k];
                if (wall(nx, ny)) continue;
                int ni = idc(nx, ny);
                if (d[ni] < 0) { d[ni] = d[c] + 1; q.push_back(ni); }
            }
        }
        return d;
    }

    void init() {
        inited = true;
        SZ = g.size;
        mp = g.map;
        myBaseCenter = g.bases[g.me];
        myBaseCells.clear();
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) myBaseCells.push_back({myBaseCenter.first + dx, myBaseCenter.second + dy});
        spawnOrd = myBaseCells;
        pair<int, int> c(SZ / 2, SZ / 2);
        stable_sort(spawnOrd.begin(), spawnOrd.end(), [&](const pair<int, int>& a, const pair<int, int>& b) {
            int da = abs(a.first - c.first) + abs(a.second - c.second);
            int db = abs(b.first - c.first) + abs(b.second - c.second);
            if (da != db) return da < db;
            int ta = abs(a.first - myBaseCenter.first) + 2 * abs(a.second - myBaseCenter.second);
            int tb = abs(b.first - myBaseCenter.first) + 2 * abs(b.second - myBaseCenter.second);
            return ta < tb;
        });
        int best = BIG;
        for (auto& s : g.spots) {
            int d = mdist(s.first, s.second, myBaseCenter.first, myBaseCenter.second);
            if (d < best) { best = d; ownSpot = s; }
        }
        best = BIG;
        for (auto& s : g.spots) {
            int d = mdist(s.first, s.second, c.first, c.second);
            if (d < best) { best = d; centerSpot = s; }
        }
        dBase = bfsMulti(myBaseCells);
        // 到各敌方基地的距离（取最小）
        oppBaseDist.assign(SZ * SZ, BIG);
        for (int t = 0; t < g.teams; t++) {
            if (t == g.me) continue;
            vector<pair<int, int>> bc;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) bc.push_back({g.bases[t].first + dx, g.bases[t].second + dy});
            vector<int> d = bfsMulti(bc);
            for (int i = 0; i < SZ * SZ; i++) if (d[i] >= 0) oppBaseDist[i] = min(oppBaseDist[i], d[i]);
        }
        // 中点：到自家旗点和中心距离的加权和最小（权重偏中心）
        {
            int bb = BIG;
            for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) {
                if (wall(x, y)) continue;
                double d = 0.8 * mdist(x, y, ownSpot.first, ownSpot.second) + 1.0 * mdist(x, y, centerSpot.first, centerSpot.second);
                if (d < bb) { bb = (int)d; midCell = {x, y}; }
            }
            if (bb >= BIG) midCell = ownSpot;
        }
        // 中路站位：中心四邻中靠我方最近的两个
        midCells.clear();
        {
            vector<pair<int, int>> nb = {{centerSpot.first - 1, centerSpot.second}, {centerSpot.first + 1, centerSpot.second},
                                         {centerSpot.first, centerSpot.second - 1}, {centerSpot.first, centerSpot.second + 1}};
            sort(nb.begin(), nb.end(), [&](const pair<int, int>& a, const pair<int, int>& b) {
                return mdist(a.first, a.second, myBaseCenter.first, myBaseCenter.second) <
                       mdist(b.first, b.second, myBaseCenter.first, myBaseCenter.second);
            });
            for (auto& q : nb) if (!wall(q.first, q.second) && (int)midCells.size() < 2) midCells.push_back(q);
            if (midCells.empty()) midCells.push_back(midCell);
        }
        // 敌侧截击位：中心四邻中离最近敌方基地最近的一个
        interceptCells.clear();
        {
            pair<int, int> eb = myBaseCenter; int bd = BIG;
            for (int t = 0; t < g.teams; t++) {
                if (t == g.me) continue;
                int dd = mdist(centerSpot.first, centerSpot.second, g.bases[t].first, g.bases[t].second);
                if (dd < bd) { bd = dd; eb = g.bases[t]; }
            }
            vector<pair<int, int>> nb = {{centerSpot.first - 1, centerSpot.second}, {centerSpot.first + 1, centerSpot.second},
                                         {centerSpot.first, centerSpot.second - 1}, {centerSpot.first, centerSpot.second + 1}};
            sort(nb.begin(), nb.end(), [&](const pair<int, int>& a, const pair<int, int>& b) {
                return mdist(a.first, a.second, eb.first, eb.second) < mdist(b.first, b.second, eb.first, eb.second);
            });
            for (auto& q : nb) if (!wall(q.first, q.second)) { interceptCells.push_back(q); break; }
        }
    }

    struct MU {
        int uid = -1;
        pair<int, int> pos{-1, -1};
        int hp = 0, flagId = -1;
        bool alive = false, respawnNow = false, dead = false, carrying = false, present = false;
    };

    struct FC {
        int fid;
        pair<int, int> pos;
        bool winnable = false;
        int dBase = BIG;
        int eta[3] = {BIG, BIG, BIG};
        int enemyEta = BIG;
    };

    string decide() {
        if (!inited) init();
        const Game& G = g;
        int T = G.turn;

        vector<MU> mine(3);
        vector<int> occId(SZ * SZ, -1);
        vector<char> spawned(SZ * SZ, 0);
        for (auto& u : G.units) if (u.x >= 0) { occId[idc(u.x, u.y)] = u.id; spawned[idc(u.x, u.y)] = 1; }
        for (int k = 0; k < 3; k++) {
            int uid = 3 * G.me + k;
            auto& u = G.units[uid];
            mine[k].uid = uid;
            if (u.x >= 0) {
                mine[k].present = mine[k].alive = true;
                mine[k].pos = {u.x, u.y};
                mine[k].hp = u.hp;
                mine[k].flagId = u.flag;
                mine[k].carrying = (u.flag >= 0);
            } else if (u.respawn_at >= 0 && u.respawn_at <= T) {
                pair<int, int> cell = spawnOrd.empty() ? myBaseCells[0] : spawnOrd[0];
                for (auto& b : spawnOrd) { if (!spawned[idc(b.first, b.second)]) { cell = b; break; } }
                spawned[idc(cell.first, cell.second)] = 1;
                mine[k].present = mine[k].respawnNow = true;
                mine[k].pos = cell;
                mine[k].hp = G.hp;
            } else {
                mine[k].dead = true;
            }
        }

        vector<const Unit*> enemies;
        for (auto& u : G.units) if (u.team != G.me && u.x >= 0) enemies.push_back(&u);

        vector<double> threat(SZ * SZ, 0.0);
        for (auto e : enemies) {
            for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++) {
                int d = abs(dx) + abs(dy);
                if (d > 3) continue;
                int x = e->x + dx, y = e->y + dy;
                if (wall(x, y)) continue;
                double w = d <= 1 ? 2.0 : (d == 2 ? 1.0 : 0.4);
                threat[idc(x, y)] += w;
            }
        }

        vector<vector<int>> dB(3);
        for (int k = 0; k < 3; k++) if (mine[k].present) dB[k] = bfs(mine[k].pos.first, mine[k].pos.second);

        vector<FC> fcs;
        for (auto& f : G.flags) {
            if (f.status != "home" && f.status != "dropped") continue;
            FC c;
            c.fid = f.id;
            c.pos = {f.x, f.y};
            c.dBase = dBase[idc(f.x, f.y)];
            vector<int> df = bfs(f.x, f.y);
            c.enemyEta = BIG;
            for (auto e : enemies) {
                if (e->flag >= 0) continue;   // 持旗敌人不能再捡旗
                int d = df[idc(e->x, e->y)]; if (d >= 0) c.enemyEta = min(c.enemyEta, d);
            }
            for (int k = 0; k < 3; k++) if (mine[k].present) c.eta[k] = dB[k][idc(f.x, f.y)];
            int myBest = BIG;
            for (int k = 0; k < 3; k++) myBest = min(myBest, c.eta[k]);
            if (myBest >= BIG) continue;
            if (f.status == "dropped" && f.return_at >= 0 && f.return_at <= T + myBest) continue;
            bool isOwn = (c.pos == ownSpot);
            c.winnable = (c.enemyEta >= BIG) || (myBest <= c.enemyEta + (isOwn ? 1 : 0));
            fcs.push_back(c);
        }

        // 我的持旗者
        int carrier = -1;
        for (int k = 0; k < 3; k++) if (mine[k].present && mine[k].carrying) { carrier = k; break; }
        // 指定守家：离自家旗点最近且不运旗的单位，锁定在旗点附近
        int camper = -1;
        {
            int best = BIG;
            for (int k = 0; k < 3; k++) {
                if (!mine[k].present || mine[k].carrying) continue;
                int d = mdist(mine[k].pos.first, mine[k].pos.second, ownSpot.first, ownSpot.second);
                if (d < best) { best = d; camper = k; }
            }
            if (best > 3) camper = -1;
        }

        // 低血时的威胁权重
        auto tw = [&](int k) -> double {
            if (!mine[k].present) return 0;
            if (mine[k].hp <= 34) return P.threatW[2];
            if (mine[k].hp <= 68) return P.threatW[1];
            return P.threatW[0];
        };

        struct Opt { int kind; pair<int, int> cell; int flagId = -1; double cost = 0; };
        vector<Opt> opts[3];
        for (int k = 0; k < 3; k++) {
            MU& m = mine[k];
            opts[k].push_back({0, {-1 - k, -1}, -1, P.noneCost});
            if (!m.present) continue;
            double tcost = tw(k) * 0.3;
            if (m.carrying) {
                opts[k].push_back({3, m.pos, -1, 0.0});
                continue;
            }
            // 接旗
            for (auto& c : fcs) {
                if (c.eta[k] >= BIG) continue;
                // 守家单位只捡自家旗点附近的
                if (k == camper && c.pos != ownSpot && c.eta[k] > 2) continue;
                double cost = c.eta[k] + 0.3 * c.dBase - 6.0;
                if (!c.winnable) cost += P.unwinnable;
                if (c.pos == ownSpot) cost -= 6.0;
                if (c.eta[k] == 0) cost -= 100.0;   // 就在脚下的旗一定拿
                // 残血别在交火区逞强接旗
                if (c.enemyEta <= 4 && c.eta[k] > 0) {
                    if (c.enemyEta <= 2) { cost += (m.hp <= 34 ? 12.0 : (m.hp <= 68 ? 2.0 : 0.0)); }
                    cost += (m.hp <= 34 ? 7.0 : (m.hp <= 68 ? 2.5 : 0.0));
                }
                bool danger = (c.enemyEta <= 3) && (c.pos != ownSpot);
                if (danger && c.eta[k] > 0) cost += 3.5;
                if (m.hp <= 34) cost += 0.6 * c.eta[k];  // 残血别跑远
                cost += tcost * threat[idc(c.pos.first, c.pos.second)];
                opts[k].push_back({1, c.pos, c.fid, cost});
                // 危险：先靠近但不拿，等机会
                if (danger && c.eta[k] <= 6) {
                    pair<int, int> hc = c.pos;
                    pair<int, int> b = myBaseCenter;
                    int dx = b.first - c.pos.first, dy = b.second - c.pos.second;
                    int steps = 0;
                    while (steps < 2) {
                        int sx = (dx > 0) - (dx < 0), sy = (dy > 0) - (dy < 0);
                        if (abs(dx) >= abs(dy)) { if (wall(hc.first + sx, hc.second)) break; hc.first += sx; dx -= sx; }
                        else { if (wall(hc.first, hc.second + sy)) break; hc.second += sy; dy -= sy; }
                        steps++;
                    }
                    if (hc != c.pos) opts[k].push_back({9, hc, -1, 0.85 * c.eta[k] + 0.8});
                }
            }
            // 中路旗的支援位（站在旗与敌方之间，随时补位/截杀）
            for (auto& c : fcs) {
                if (c.pos != centerSpot) continue;
                int d = dB[k][idc(c.pos.first, c.pos.second)];
                if (d < 0 || d > 7) continue;
                int bx = -1, by = -1; int best = BIG;
                for (int t = 0; t < G.teams; t++) {
                    if (t == G.me) continue;
                    int dd = mdist(c.pos.first, c.pos.second, G.bases[t].first, G.bases[t].second);
                    if (dd < best) { best = dd; bx = G.bases[t].first; by = G.bases[t].second; }
                }
                pair<int, int> sc = c.pos;
                int dx = bx - c.pos.first, dy = by - c.pos.second;
                if (abs(dx) >= abs(dy)) sc = {c.pos.first + (dx > 0 ? 1 : -1), c.pos.second};
                else sc = {c.pos.first, c.pos.second + (dy > 0 ? 1 : -1)};
                if (wall(sc.first, sc.second)) sc = {c.pos.first, c.pos.second + 1};
                if (wall(sc.first, sc.second)) continue;
                opts[k].push_back({8, sc, -1, 0.9 * d - 2.5});
            }
            // 站位
            for (auto& s : G.spots) {
                int d = dB[k][idc(s.first, s.second)];
                if (d < 0) continue;
                if (k == camper && s != ownSpot) continue;
                if (k != camper && s == ownSpot) continue;   // 非守家不去站旗点
                double prio = (s == ownSpot) ? P.prioOwn : (s == centerSpot ? P.prioCenter : P.prioOther);
                if (m.hp <= 34 && s == centerSpot) prio += 6.0;
                double cost = d + 0.3 * dBase[idc(s.first, s.second)] + prio;
                if (k != camper && s == centerSpot && d <= 9) cost = 0.7 * d - 0.2 + (m.hp <= 34 ? 4.0 : (m.hp <= 68 ? 1.0 : 0.0));   // 中路旗点：抢椅子（仅小图）
                cost += tcost * 0.15 * threat[idc(s.first, s.second)];
                opts[k].push_back({2, s, -1, cost});
            }
            // 守家：站在自家旗点与中路之间的枢纽位
            if (k == camper) {
                int d = dB[k][idc(midCell.first, midCell.second)];
                if (d >= 0) opts[k].push_back({4, midCell, -1, 0.9 * d + 0.3});
                int d0 = dBase[idc(midCell.first, midCell.second)];
                (void)d0;
                // 若枢纽离基地太远（大图），站自家旗点
                if (mdist(midCell.first, midCell.second, myBaseCenter.first, myBaseCenter.second) > 12) {
                    int ds = dB[k][idc(ownSpot.first, ownSpot.second)];
                    if (ds >= 0) opts[k].push_back({4, ownSpot, -1, 0.9 * ds + 0.3});
                }
            }
            // 中路待命（两个中路站位，贴近中心；仅小图）
            if (k != camper) {
                if (mdist(centerSpot.first, centerSpot.second, myBaseCenter.first, myBaseCenter.second) <= 16) {
                    for (auto& mc : midCells) {
                        int d = dB[k][idc(mc.first, mc.second)];
                        if (d < 0) continue;
                        double cost = 0.7 * d + 0.2 + tcost * 0.15 * threat[idc(mc.first, mc.second)];
                        opts[k].push_back({4, mc, -1, cost});
                    }
                    for (auto& ic : interceptCells) {
                        if (G.teams > 3) break;
                        int d = dB[k][idc(ic.first, ic.second)];
                        if (d < 0) continue;
                        double cost = 0.7 * d + 0.0 + tcost * 0.15 * threat[idc(ic.first, ic.second)];
                        opts[k].push_back({4, ic, -1, cost});
                    }
                }
            }
            // 追击敌人（只在近身时考虑，不深追）
            for (auto e : enemies) {
                if (e->respawn_at == T) continue;
                int ec = idc(e->x, e->y);
                int d = dB[k][ec];
                if (d < 0 || d > 3) continue;
                if (k == camper && d > 1) continue;
                if (m.hp <= 34 && e->hp > 34 && e->flag < 0) continue;
                double cost = 1.5 * d;
                if (e->flag >= 0) cost -= 4.0;
                if (e->hp <= 34) cost -= 4.0;
                if (carrier >= 0 && mdist(e->x, e->y, mine[carrier].pos.first, mine[carrier].pos.second) <= 3) cost -= 2.0;
                if (d <= 1) cost -= 1.0;
                if (m.hp <= 34) cost += 3.0;
                opts[k].push_back({5, {e->x, e->y}, -1, cost});
            }
            // 护送自家持旗者
            if (carrier >= 0 && carrier != k) {
                int d = dB[k][idc(mine[carrier].pos.first, mine[carrier].pos.second)];
                if (d >= 0 && d <= 7) {
                    double cost = 0.8 * d - P.escortCost;
                    opts[k].push_back({6, mine[carrier].pos, -1, cost});
                }
                // 持旗者身边有敌人：优先去杀
                for (auto e : enemies) {
                    if (e->respawn_at == T) continue;
                    int de = mdist(e->x, e->y, mine[carrier].pos.first, mine[carrier].pos.second);
                    if (de > 4) continue;
                    int dk = dB[k][idc(e->x, e->y)];
                    if (dk < 0 || dk > 6) continue;
                    opts[k].push_back({10, {e->x, e->y}, -1, 0.7 * dk - 5.0});
                }
            }
        }

        vector<int> sel(3, 0);
        double bestTotal = 1e18;
        int n0 = (int)opts[0].size(), n1 = (int)opts[1].size(), n2 = (int)opts[2].size();
        for (int a = 0; a < n0; a++) for (int b = 0; b < n1; b++) for (int c = 0; c < n2; c++) {
            double tot = opts[0][a].cost + opts[1][b].cost + opts[2][c].cost;
            if (tot >= bestTotal) continue;
            pair<int, int> p0 = opts[0][a].cell, p1 = opts[1][b].cell, p2 = opts[2][c].cell;
            bool bad = false;
            auto conflict = [](const pair<int, int>& p, const pair<int, int>& q) {
                return p.first >= 0 && q.first >= 0 && p == q;
            };
            if (conflict(p0, p1) || conflict(p0, p2) || conflict(p1, p2)) bad = true;
            // 两个护送/两个同一格的其它情况已被上面的冲突处理
            if (bad) continue;
            bestTotal = tot;
            sel = {a, b, c};
        }

        vector<pair<int, int>> post(3, {-1, -1});
        for (int k = 0; k < 3; k++) post[k] = mine[k].present ? mine[k].pos : make_pair(-1, -1);
        vector<char> planned(SZ * SZ, 0);
        vector<double> baseField;
        bool haveField = false;
        vector<int> baseSrc;
        for (auto& b : myBaseCells) if (!wall(b.first, b.second)) baseSrc.push_back(idc(b.first, b.second));
        map<int, vector<int>> distCache;
        auto distTo = [&](int x, int y) -> const vector<int>& {
            int key = idc(x, y);
            auto it = distCache.find(key);
            if (it != distCache.end()) return it->second;
            return distCache.emplace(key, bfs(x, y)).first->second;
        };

        vector<char> moveOut(3, 'S');
        vector<string> actOut(3, "-");

        for (int k = 0; k < 3; k++) {
            MU& m = mine[k];
            if (!m.present) { post[k] = m.pos; continue; }
            Opt& o = opts[k][sel[k]];
            vector<double> field(SZ * SZ, 1e18);
            if (o.kind == 3 || o.kind == 7) {
                if (!haveField) {
                    baseField.assign(SZ * SZ, 1e18);
                    priority_queue<pair<double, int>, vector<pair<double, int>>, greater<pair<double, int>>> pq;
                    for (int s : baseSrc) { baseField[s] = 0; pq.push({0, s}); }
                    while (!pq.empty()) {
                        auto pr = pq.top(); pq.pop();
                        double cd = pr.first; int u2 = pr.second;
                        if (cd > baseField[u2] + 1e-9) continue;
                        int x = u2 % SZ, y = u2 / SZ;
                        for (int d4 = 0; d4 < 4; d4++) {
                            int nx = x + DX[d4], ny = y + DY[d4];
                            if (wall(nx, ny)) continue;
                            int ni = idc(nx, ny);
                            double nc = cd + 10.0 + P.carrierThreat * threat[ni];
                            if (nc < baseField[ni] - 1e-9) { baseField[ni] = nc; pq.push({nc, ni}); }
                        }
                    }
                    haveField = true;
                }
                field = baseField;
            } else {
                // 目标：接旗/站位/中点/追击/护送
                pair<int, int> tgt = o.cell;
                if (o.kind == 6 && carrier >= 0) {
                    // 站在持旗者与最近敌人之间
                    auto cpos = mine[carrier].pos;
                    double bestD = 1e9; pair<int, int> bc = tgt;
                    for (auto e : enemies) {
                        double d = mdist(e->x, e->y, cpos.first, cpos.second);
                        if (d < bestD) { bestD = d; bc = {e->x, e->y}; }
                    }
                    if (bestD < 1e8) {
                        int sx = (bc.first > cpos.first) ? 1 : (bc.first < cpos.first ? -1 : 0);
                        int sy = (bc.second > cpos.second) ? 1 : (bc.second < cpos.second ? -1 : 0);
                        pair<int, int> cand{cpos.first + sx, cpos.second + sy};
                        if (wall(cand.first, cand.second)) cand = cpos;
                        tgt = cand;
                    }
                }
                const vector<int>& dt = distTo(tgt.first, tgt.second);
                double w = tw(k);
                for (int i = 0; i < SZ * SZ; i++) {
                    if (dt[i] < 0) { field[i] = 1e18; continue; }
                    field[i] = dt[i] * 10.0 + w * threat[i];
                }
                if (!wall(tgt.first, tgt.second)) field[idc(tgt.first, tgt.second)] -= 1.0;
                // 不要顺路踩到别的旗（避免和别人抢同一格卡死）；但目标格例外
                bool tgtIsFlag = false;
                for (auto& c2 : fcs) {
                    if (c2.pos == o.cell) { tgtIsFlag = true; continue; }
                    int fi = idc(c2.pos.first, c2.pos.second);
                    if (field[fi] < 1e17) field[fi] = 1e17;
                }
                if (tgtIsFlag && !wall(o.cell.first, o.cell.second)) field[idc(o.cell.first, o.cell.second)] = dt[idc(o.cell.first, o.cell.second)] * 10.0;
            }
            int cur = idc(m.pos.first, m.pos.second);
            // 己方单位会挡路：站位不动的队友格子绕开
            vector<char> friendStay(SZ * SZ, 0);
            for (int j = 0; j < 3; j++) {
                if (j == k || !mine[j].present) continue;
                bool vacates = (post[j] != mine[j].pos);
                if (!vacates) friendStay[idc(mine[j].pos.first, mine[j].pos.second)] = 1;
            }
            double curEff = field[cur] + (occId[cur] >= 0 && occId[cur] != m.uid ? 1.0 : 0.0) + planned[cur] * 5.0;
            int bestCell = -1; double bestEff = curEff + 1e-9;
            for (int d4 = 0; d4 < 4; d4++) {
                int nx = m.pos.first + DX[d4], ny = m.pos.second + DY[d4];
                if (wall(nx, ny)) continue;
                int ni = idc(nx, ny);
                if (field[ni] >= 1e17) continue;
                double eff = field[ni] + planned[ni] * 5.0;
                if (eff < bestEff - 1e-9) { bestEff = eff; bestCell = ni; }
            }
            if (bestCell >= 0) {
                moveOut[k] = (bestCell == idc(m.pos.first - 1, m.pos.second) ? 'L' :
                              bestCell == idc(m.pos.first + 1, m.pos.second) ? 'R' :
                              bestCell == idc(m.pos.first, m.pos.second - 1) ? 'U' : 'D');
                post[k] = {bestCell % SZ, bestCell / SZ};
            } else {
                post[k] = m.pos;
            }
            planned[idc(post[k].first, post[k].second)] = 1;
        }

        // 拾旗
        vector<char> willPick(3, 0);
        for (int k = 0; k < 3; k++) {
            MU& m = mine[k];
            if (!m.present || m.carrying) continue;
            for (auto& c : fcs) if (post[k] == c.pos) willPick[k] = 1;
            // 站在可拿的旗上时绝不走开：原地拿
            for (auto& c : fcs) {
                if (m.pos == c.pos) {
                    willPick[k] = 1;
                    post[k] = m.pos;
                    moveOut[k] = 'S';
                    planned[idc(m.pos.first, m.pos.second)] = 1;
                }
            }
        }

        // 攻击位调整：若有高价值目标，走到能打到的格子并攻击
        vector<int> hitCount(G.units.size(), 0);
        for (int k = 0; k < 3; k++) {
            MU& m = mine[k];
            if (!m.present || m.carrying || m.respawnNow || willPick[k]) continue;
            for (auto e : enemies) {
                if (e->respawn_at == T) continue;
                if (canHit(post[k].first, post[k].second, e->x, e->y)) hitCount[e->id]++;
            }
        }
        vector<char> overrode(3, 0);
        for (int k = 0; k < 3; k++) {
            MU& m = mine[k];
            if (!m.present || m.carrying || m.respawnNow || willPick[k]) continue;
            if (opts[k][sel[k]].kind == 1) continue;   // 接旗优先，不因攻击改道
            double bestVal = -1e9; pair<int, int> bestCellP = post[k]; int bestT = -1;
            vector<pair<int, int>> cands = {post[k],
                {m.pos.first - 1, m.pos.second}, {m.pos.first + 1, m.pos.second},
                {m.pos.first, m.pos.second - 1}, {m.pos.first, m.pos.second + 1}};
            for (auto& p : cands) {
                if (wall(p.first, p.second)) continue;
                if (p != post[k]) {
                    if (planned[idc(p.first, p.second)]) continue;
                    bool fs = false;
                    for (int j = 0; j < 3; j++) if (j != k && mine[j].present && post[j] == mine[j].pos && p == mine[j].pos) fs = true;
                    if (fs) continue;
                }
                for (auto e : enemies) {
                    if (e->respawn_at == T) continue;
                    if (mdist(e->x, e->y, m.pos.first, m.pos.second) > 4) continue;
                    if (!canHit(p.first, p.second, e->x, e->y)) continue;
                    double val = 0;
                    if (e->hp <= 34) val += 60;
                    if (e->flag >= 0) val += 50;
                    int total = 34 * (1 + hitCount[e->id]);
                    if (total >= e->hp) val += 45;
                    val += (100 - e->hp) * 0.3;
                    val += 6.0 * max(0, G.score[e->team] - G.score[G.me]);
                    if (p == post[k]) val += 6;
                    if (val > bestVal) { bestVal = val; bestCellP = p; bestT = e->id; }
                }
            }
            if (bestVal >= 45 && bestT >= 0) {
                if (bestCellP != post[k]) {
                    planned[idc(post[k].first, post[k].second)] = 0;
                    moveOut[k] = (bestCellP == make_pair(m.pos.first - 1, m.pos.second) ? 'L' :
                                  bestCellP == make_pair(m.pos.first + 1, m.pos.second) ? 'R' :
                                  bestCellP == make_pair(m.pos.first, m.pos.second - 1) ? 'U' : 'D');
                    post[k] = bestCellP;
                    planned[idc(post[k].first, post[k].second)] = 1;
                }
                overrode[k] = 1;
                actOut[k] = to_string(bestT);
            }
        }
        // 攻击
        for (int k = 0; k < 3; k++) {
            MU& m = mine[k];
            if (!m.present || m.carrying || m.respawnNow) continue;
            if (willPick[k]) {
                // 想去捡的旗被别人争：从当前位置能打到争旗者就先打（否则互相卡住）
                bool steppedOn = false;
                for (auto& c : fcs) if (m.pos == c.pos) steppedOn = true;
                int bestT = -1; double bestSc = 0;
                if (!steppedOn) {
                    for (auto e : enemies) {
                        if (e->respawn_at == T) continue;
                        int dpost = mdist(e->x, e->y, post[k].first, post[k].second);
                        if (dpost > 2) continue;
                        if (!canHit(m.pos.first, m.pos.second, e->x, e->y)) continue;
                        double sc = 0;
                        if (e->hp <= 34) sc += 60;
                        if (e->flag >= 0) sc += 50;
                        if (dpost <= 1) sc += 30;
                        sc += (100 - e->hp) * 0.3;
                        if (sc > bestSc) { bestSc = sc; bestT = e->id; }
                    }
                }
                if (bestT >= 0) actOut[k] = to_string(bestT);
                else actOut[k] = "P";
                continue;
            }
            if (overrode[k]) continue;
            double bestScore = -1e9; int bestT = -1;
            for (auto e : enemies) {
                if (e->respawn_at == T) continue;
                if (!canHit(post[k].first, post[k].second, e->x, e->y)) continue;
                double sc = 0;
                if (e->hp <= 34) sc += 60;
                if (e->flag >= 0) sc += 45;
                int total = 34 * hitCount[e->id];
                if (total >= e->hp) sc += 35;
                sc += (100 - e->hp) * 0.4;
                sc += 5.0 * max(0, G.score[e->team] - G.score[G.me]);
                if (mdist(post[k].first, post[k].second, e->x, e->y) <= 1) sc += 10;
                // 保护持旗者
                if (carrier >= 0 && mdist(e->x, e->y, mine[carrier].pos.first, mine[carrier].pos.second) <= 3) sc += 25;
                if (sc > bestScore) { bestScore = sc; bestT = e->id; }
            }
            if (bestT >= 0) actOut[k] = to_string(bestT);
            else actOut[k] = "-";
        }

        // 落点去重：两个单位要走同一格会互相卡死，后到的原地不动
        for (int i = 0; i < 3; i++) {
            for (int j = i + 1; j < 3; j++) {
                if (!mine[i].present || !mine[j].present) continue;
                if (post[i].first >= 0 && post[i] == post[j]) {
                    post[j] = mine[j].pos;
                    moveOut[j] = 'S';
                    // 重算动作
                    bool onFlag = false;
                    for (auto& c : fcs) if (mine[j].pos == c.pos) onFlag = true;
                    if (onFlag && !mine[j].carrying) actOut[j] = "P";
                    else if (actOut[j] != "-" && actOut[j] != "P") {
                        int tgt = atoi(actOut[j].c_str());
                        if (tgt >= 0 && tgt < (int)G.units.size()) {
                            auto& e = G.units[tgt];
                            if (e.x < 0 || !canHit(mine[j].pos.first, mine[j].pos.second, e.x, e.y)) actOut[j] = "-";
                        }
                    }
                }
            }
        }

        string out = to_string(T);
        for (int k = 0; k < 3; k++) {
            out += ' '; out += moveOut[k];
            out += ' '; out += actOut[k];
        }
        return out;
    }
};

int main() {
    ios::sync_with_stdio(false);
    Bot bot;
    while (read_turn(bot.g)) {
        cout << bot.decide() << endl;
    }
    return 0;
}
