// 夺旗 bot：全信息、3 角色统一调度。
// 1) 带权 Dijkstra 寻路（危险格加价，血量越低越避战）
// 2) 用统一的"期望得分/耗时"给 3 个角色挑任务：送旗 / 抢旗 / 拦截敌方携旗者 / 护送 / 蹲旗点
// 3) 集火攻击（枚举组合，优先秒杀与携旗者）
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

// ===================== 决策 =====================
namespace pl {

const int INF = 1000000000;
int N, ME, SZ, TURN, HP, DMG, RESPAWN, FLAGRET, COOL;
vector<int> wall;
vector<pair<int,int>> BASE, SPOT;
vector<Unit> U;
vector<Flag> F;
vector<int> danger;
int CX, CY;
vector<vector<pair<int,int>>> spawnOrder;

// 调参
int PEN_DANGER = 1;      // 寻路时每点威胁的加价（1/10 格）
int PEN_ENEMYBASE = 12;  // 敌方基地附近
int MY_OCC = 2;          // 己方占位（下一回合多半会让开）
int EN_OCC = 4;          // 敌方占位
int HOME_NEAR = 14;
double RACE_SOFT = 2.5;  // 0 = 不考虑抢不过

inline int id2(int x, int y) { return y * SZ + x; }
inline bool inb(int x, int y) { return x >= 0 && y >= 0 && x < SZ && y < SZ; }
inline bool isWall(int x, int y) { return !inb(x, y) || wall[id2(x, y)]; }
inline int manh(int ax, int ay, int bx, int by) { return abs(ax - bx) + abs(ay - by); }

const int DX[5] = {0, 0, 0, -1, 1};   // 0 停 1 上 2 下 3 左 4 右
const int DY[5] = {0, -1, 1, 0, 0};
const char MV[5] = {'S', 'U', 'D', 'L', 'R'};

// 含进入代价的 Dijkstra：d[c] = 从 c 走到目标的代价（含沿途踏入各格的代价）
vector<int> field(int gx, int gy, const vector<int>& pen) {
    vector<int> d(SZ * SZ, INF);
    if (isWall(gx, gy)) return d;
    priority_queue<pair<int,int>, vector<pair<int,int>>, greater<pair<int,int>>> pq;
    int g = id2(gx, gy);
    d[g] = 0; pq.push({0, g});
    while (!pq.empty()) {
        auto pr = pq.top(); pq.pop();
        int du = pr.first, c = pr.second;
        if (du != d[c]) continue;
        int x = c % SZ, y = c / SZ;
        for (int k = 1; k <= 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (isWall(nx, ny)) continue;
            int nc = id2(nx, ny);
            int nd = du + 10 + pen[c];
            if (nd < d[nc]) { d[nc] = nd; pq.push({nd, nc}); }
        }
    }
    return d;
}
vector<int> fieldMulti(const vector<int>& srcs, const vector<int>& pen, int busyCost = 0,
                       const vector<char>* busy = nullptr) {
    vector<int> d(SZ * SZ, INF);
    priority_queue<pair<int,int>, vector<pair<int,int>>, greater<pair<int,int>>> pq;
    for (int s : srcs) if (s >= 0 && s < SZ * SZ && !wall[s]) {
        int c0 = (busyCost && busy && (*busy)[s]) ? busyCost : 0;
        if (c0 < d[s]) { d[s] = c0; pq.push({c0, s}); }
    }
    while (!pq.empty()) {
        auto pr = pq.top(); pq.pop();
        int du = pr.first, c = pr.second;
        if (du != d[c]) continue;
        int x = c % SZ, y = c / SZ;
        for (int k = 1; k <= 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (isWall(nx, ny)) continue;
            int nc = id2(nx, ny);
            int nd = du + 10 + pen[c];
            if (nd < d[nc]) { d[nc] = nd; pq.push({nd, nc}); }
        }
    }
    return d;
}
vector<int> bfsPlain(int gx, int gy) {
    vector<int> d(SZ * SZ, INF);
    if (isWall(gx, gy)) return d;
    deque<int> q;
    int g = id2(gx, gy);
    d[g] = 0; q.push_back(g);
    while (!q.empty()) {
        int c = q.front(); q.pop_front();
        int x = c % SZ, y = c / SZ;
        for (int k = 1; k <= 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (isWall(nx, ny)) continue;
            int nc = id2(nx, ny);
            if (d[nc] == INF) { d[nc] = d[c] + 1; q.push_back(nc); }
        }
    }
    return d;
}

bool canAtk(int ax, int ay, int bx, int by) {
    int dx = bx - ax, dy = by - ay;
    if (abs(dx) + abs(dy) > 2) return false;
    if (abs(dx) == 2 && dy == 0) return !isWall(ax + dx / 2, ay);
    if (abs(dy) == 2 && dx == 0) return !isWall(ax, ay + dy / 2);
    if (abs(dx) == 1 && abs(dy) == 1) return !isWall(ax + dx, ay) || !isWall(ax, ay + dy);
    return true;
}

} // namespace pl

using namespace pl;

string decide(const Game& g) {
    N = g.teams; ME = g.me; SZ = g.size; TURN = g.turn; HP = g.hp; DMG = g.damage;
    RESPAWN = g.respawn; FLAGRET = g.flag_return; COOL = g.flag_cooldown;
    CX = SZ / 2; CY = SZ / 2;
    BASE = g.bases; SPOT = g.spots;
    U = g.units; F = g.flags;
    wall.assign(SZ * SZ, 0);
    for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) wall[id2(x, y)] = (g.map[y][x] == '#');

    // ---- 复活位置预测（复刻裁判的 spawn_order 与遍历顺序）----
    if ((int)spawnOrder.size() != N) {
        spawnOrder.assign(N, {});
        for (int t = 0; t < N; t++) {
            vector<pair<int,int>> cells;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) cells.push_back({BASE[t].first + dx, BASE[t].second + dy});
            stable_sort(cells.begin(), cells.end(), [&](const pair<int,int>& a, const pair<int,int>& b) {
                int da = manh(a.first, a.second, CX, CY), db = manh(b.first, b.second, CX, CY);
                if (da != db) return da < db;
                int ka = abs(a.first - BASE[t].first) + 2 * abs(a.second - BASE[t].second);
                int kb = abs(b.first - BASE[t].first) + 2 * abs(b.second - BASE[t].second);
                return ka < kb;
            });
            spawnOrder[t] = cells;
        }
    }
    vector<pair<int,int>> eff(U.size(), {-1, -1});
    vector<char> respawnNow(U.size(), 0), immuneNow(U.size(), 0), canFight(U.size(), 0);
    for (auto& u : U) if (u.x >= 0) eff[u.id] = {u.x, u.y};
    {
        vector<char> occ(SZ * SZ, 0);
        for (auto& u : U) if (u.x >= 0) occ[id2(u.x, u.y)] = 1;
        for (auto& u : U) {
            if (u.x >= 0 || u.respawn_at > TURN) continue;
            for (auto& p : spawnOrder[u.team]) {
                if (!occ[id2(p.first, p.second)]) {
                    occ[id2(p.first, p.second)] = 1;
                    eff[u.id] = p; respawnNow[u.id] = 1; immuneNow[u.id] = 1;
                    break;
                }
            }
        }
    }
    for (auto& u : U) canFight[u.id] = (eff[u.id].first >= 0 && !immuneNow[u.id] && u.flag < 0 && u.team != ME);

    // ---- 威胁图（近期能打到该格的敌人数）----
    danger.assign(SZ * SZ, 0);
    for (auto& u : U) {
        if (!canFight[u.id]) continue;
        int ux = eff[u.id].first, uy = eff[u.id].second;
        for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++) {
            int d = abs(dx) + abs(dy);
            if (d > 3) continue;
            int nx = ux + dx, ny = uy + dy;
            if (isWall(nx, ny)) continue;
            danger[id2(nx, ny)] += (d <= 2 ? 2 : 1);
        }
    }
    vector<int> cnt2(SZ * SZ, 0), cnt3(SZ * SZ, 0), pen(SZ * SZ, 0);
    for (auto& u : U) {
        if (!canFight[u.id]) continue;
        int ux = eff[u.id].first, uy = eff[u.id].second;
        for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++) {
            int d = abs(dx) + abs(dy);
            if (d > 3) continue;
            int nx = ux + dx, ny = uy + dy;
            if (isWall(nx, ny)) continue;
            if (d <= 2) cnt2[id2(nx, ny)]++;
            cnt3[id2(nx, ny)]++;
        }
    }
    vector<int> enemyBaseNear(SZ * SZ, 0);
    for (int t = 0; t < N; t++) if (t != ME) {
        int bcx = BASE[t].first, bcy = BASE[t].second;
        for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
            int nx = bcx + dx, ny = bcy + dy;
            if (!isWall(nx, ny)) enemyBaseNear[id2(nx, ny)] = 1;
        }
    }
    for (int c = 0; c < SZ * SZ; c++)
        if (!wall[c]) pen[c] = PEN_DANGER * min(cnt3[c], 6) + (enemyBaseNear[c] ? PEN_ENEMYBASE : 0);
    auto penFor = [&](int hp) {
        vector<int> pk(SZ * SZ, 0);
        int k2 = 0, k3 = 0;
        for (int c = 0; c < SZ * SZ; c++) {
            if (wall[c]) continue;
            int v = pen[c] + cnt2[c] * k2 + cnt3[c] * k3;
            if (v > 70) v = 70;
            pk[c] = v;
        }
        return pk;
    };

    // ---- 关键距离场 ----
    int bx = BASE[ME].first, by = BASE[ME].second;
    vector<int> homeSrc;
    for (auto& p : spawnOrder[ME]) homeSrc.push_back(id2(p.first, p.second));
    vector<int> fHome = fieldMulti(homeSrc, pen);
    int NF = F.size();
    vector<vector<int>> fFlag(NF);
    vector<char> fGround(NF, 0);
    for (int i = 0; i < NF; i++) {
        fGround[i] = (F[i].status == "home" || F[i].status == "dropped") && F[i].x >= 0;
        if (fGround[i]) fFlag[i] = field(F[i].x, F[i].y, pen);
    }
    int myCarrier = -1;
    for (int k = 0; k < 3; k++) { int uid = 3 * ME + k; if (eff[uid].first >= 0 && U[uid].flag >= 0) myCarrier = uid; }
    // 携旗者专用场：血量越低越躲
    vector<int> fHomeCarry;
    if (myCarrier >= 0) {
        vector<char> occCell(SZ * SZ, 0);
        for (int k = 0; k < 3; k++) { int uid = 3 * ME + k; if (uid != myCarrier && eff[uid].first >= 0) occCell[id2(eff[uid].first, eff[uid].second)] = 1; }
        fHomeCarry = fieldMulti(homeSrc, penFor(U[myCarrier].hp), 30, &occCell);
    } else fHomeCarry = fHome;

    // 敌方携旗者
    vector<int> eCarr;
    for (auto& u : U) if (u.team != ME && eff[u.id].first >= 0 && u.flag >= 0) eCarr.push_back(u.id);
    sort(eCarr.begin(), eCarr.end(), [&](int a, int b) {
        return manh(eff[a].first, eff[a].second, bx, by) < manh(eff[b].first, eff[b].second, bx, by);
    });
    if (eCarr.size() > 3) eCarr.resize(3);
    vector<vector<int>> fEC(eCarr.size());
    for (size_t i = 0; i < eCarr.size(); i++) {
        auto p = eff[eCarr[i]];
        if (manh(p.first, p.second, bx, by) <= 26) fEC[i] = field(p.first, p.second, pen);
    }
    auto enHomeDist = [&](int uid) {
        int t = U[uid].team;
        return manh(eff[uid].first, eff[uid].second, BASE[t].first, BASE[t].second);
    };

    // ---- 任务分配 ----
    vector<int> uidOf(3); for (int k = 0; k < 3; k++) uidOf[k] = 3 * ME + k;
    vector<int> goalType(3, -1), goalId(3, -1);   // -1 无 0 回家待命 1 抢旗 2 拦截 3 护送 4 回家 5 蹲点
    vector<char> freeU(3, 0);
    for (int k = 0; k < 3; k++) {
        int uid = uidOf[k];
        if (eff[uid].first < 0) { goalType[k] = -1; continue; }
        if (U[uid].flag >= 0) { goalType[k] = 4; continue; }
        freeU[k] = 1;
    }
    // 旗点到各单位的距离、以及"谁能先到"
    vector<vector<int>> dFlagTo(NF);
    for (int i = 0; i < NF; i++) if (fGround[i]) {
        dFlagTo[i].assign(U.size(), INF);
        for (auto& e : U) if (eff[e.id].first >= 0) dFlagTo[i][e.id] = fFlag[i][id2(eff[e.id].first, eff[e.id].second)];
    }
    // 自由旗点（没有旗子停在上面的）
    vector<char> spotFree(SPOT.size(), 1);
    for (auto& f : F) if (f.status == "home" && f.x >= 0)
        for (size_t s = 0; s < SPOT.size(); s++) if (SPOT[s].first == f.x && SPOT[s].second == f.y) spotFree[s] = 0;
    vector<vector<int>> dSpot(SPOT.size());
    for (size_t s = 0; s < SPOT.size(); s++) dSpot[s] = bfsPlain(SPOT[s].first, SPOT[s].second);
    vector<int> spotHomeDist(SPOT.size());
    for (size_t s = 0; s < SPOT.size(); s++) { int t = fHome[id2(SPOT[s].first, SPOT[s].second)]; spotHomeDist[s] = (t >= INF ? 60 : t / 10); }

    static int prevType[3] = {-9, -9, -9}, prevId[3] = {-9, -9, -9};
    if (TURN <= 1) { for (int k = 0; k < 3; k++) { prevType[k] = -9; prevId[k] = -9; } }
    struct Cand { int k, type, id; double score; };
    vector<Cand> cands;
    for (int k = 0; k < 3; k++) {
        if (!freeU[k]) continue;
        int uid = uidOf[k];
        int ux = eff[uid].first, uy = eff[uid].second;
        // 1) 抢地上的旗
        for (int i = 0; i < NF; i++) {
            if (!fGround[i]) continue;
            int dm = fFlag[i][id2(ux, uy)];
            if (dm >= INF) continue;
            double t_me = dm / 10.0;
            double t_home = spotHomeDist[0];   // 占位，下面覆盖
            { int t = fHome[id2(F[i].x, F[i].y)]; t_home = (t >= INF ? 60 : t / 10.0); }
            double trip = t_me + t_home + 1;
            double t_enemy = 1e9;
            for (auto& e : U) {
                if (e.team == ME || eff[e.id].first < 0) continue;
                int de = dFlagTo[i][e.id];
                if (de < INF) t_enemy = min(t_enemy, de / 10.0);
            }
            if (trip > 60) continue;
            double margin = t_enemy - t_me;
            double pw = 1.0;
            int near2 = cnt2[id2(F[i].x, F[i].y)];
            int near3 = cnt3[id2(F[i].x, F[i].y)];
            double cleanB = (near2 == 0) ? 3.0 : (near2 == 1 ? 0.0 : 0.0);   // 没人守的旗才是好旗
            double ev = pw * (F[i].status == "dropped" ? 1.06 : 1.0);
            // 远距离运输基本送不到（会被追击致死），按送达概率打折
            double pconv = 1.0 / (1.0 + exp((trip - 15.0) / 5.0));
            cands.push_back({k, 1, i, ev * cleanB * 130.0 * pconv / (trip + 4.0)});
        }
        // 2) 拦截敌方携旗者（抢回一分）
        for (size_t i = 0; i < eCarr.size(); i++) {
            if (fEC[i].empty()) continue;
            int eid = eCarr[i];
            int d = fEC[i][id2(ux, uy)];
            if (d >= INF) continue;
            double t_me = d / 10.0;
            double t_score = enHomeDist(eid);
            if (t_me + 3 > t_score + 2) continue;
            bool canCut = manh(ux, uy, BASE[U[eid].team].first, BASE[U[eid].team].second) + 1 <= t_score;
            if (t_me > 4 && !canCut) continue;
            double ev = 1.0 / (1.0 + exp(-(t_score - t_me - 3) / 2.0));
            cands.push_back({k, 2, eid, ev * 95.0 / (t_me + 5.0)});
        }
        // 3) 护送自家携旗者
        if (myCarrier >= 0 && myCarrier != uid) {
            int mcx = eff[myCarrier].first, mcy = eff[myCarrier].second;
            int d = manh(ux, uy, mcx, mcy);
            int threats = 0;
            for (auto& e : U) if (e.team != ME && eff[e.id].first >= 0 && canFight[e.id] &&
                                  manh(eff[e.id].first, eff[e.id].second, mcx, mcy) <= 8) threats++;
            if (threats) {
                double ev = 0.45 + 0.10 * threats;
                cands.push_back({k, 3, myCarrier, ev * 100.0 / (d + 4.0)});
            }
        }
        // 4) 蹲守某旗点（按长期得分率定价：R * w_s * pw_s）——场上没旗可抢时才考虑
        {
            bool anyGround = false;
            for (int i = 0; i < NF; i++) if (fGround[i]) anyGround = true;
            int freeCnt = 0;
            for (size_t s = 0; s < SPOT.size(); s++) if (spotFree[s]) freeCnt++;
            if (freeCnt == 0) freeCnt = 1;
            double R = (double)NF / (COOL + 15.0);
            for (size_t s = 0; s < SPOT.size(); s++) {
                if (false) continue;
                int d = dSpot[s][id2(ux, uy)];
                if (d >= INF) continue;
                double w = 0.5 * (spotFree[s] ? 1.0 / freeCnt : 0.0) + 0.5 / SPOT.size();
                double t_me = d;
                double t_enemy = 1e9;
                for (auto& e : U) if (e.team != ME && eff[e.id].first >= 0) {
                    int de = dSpot[s][id2(eff[e.id].first, eff[e.id].second)];
                    if (de < INF) t_enemy = min(t_enemy, (double)de);
                }
                double pw = (t_enemy > 1e8) ? 1.0 : 1.0 / (1.0 + exp(-(t_enemy - t_me) / 2.5));
                double rate = R * w * pw;
                cands.push_back({k, 5, (int)s, rate * 100.0 / (1.0 + 0.05 * d)});
            }
        }
        // 5) 兜底：回自家旗点附近待命
        { double t = dSpot[ME][id2(ux, uy)];
          if (t > 90) t = 40;
          cands.push_back({k, 0, -1, 0.40 / (1.0 + 0.04 * t)}); }
    }
    for (auto& c : cands) {
        if (c.type == prevType[c.k]) {
            if (c.type == 1 || c.type == 2 || c.type == 5) { if (c.id == prevId[c.k]) c.score *= 1.45; }
            else c.score *= 1.45;
        }
    }
    sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
    vector<char> usedFlag(NF, 0), usedSpot(SPOT.size(), 0), usedK(3, 0);
    for (auto& c : cands) {
        if (usedK[c.k]) continue;
        if (c.type == 1 && usedFlag[c.id] && false) continue;
        if (c.type == 5 && usedSpot[c.id]) continue;
        usedK[c.k] = 1;
        goalType[c.k] = c.type; goalId[c.k] = c.id;
        if (c.type == 1) usedFlag[c.id] = 1;
        if (c.type == 5) usedSpot[c.id] = 1;
    }
    // 人多时全队抱团：混乱局面下单打独斗必死
    if (N >= 5) {
        int bestK = -1, bestF = -1; double bs = -1e18;
        for (auto& c : cands) if (c.type == 1 && usedK[c.k] && goalType[c.k] == 1 && c.score > bs) { bs = c.score; bestK = c.k; bestF = c.id; }
        if (bestF >= 0 && usedK[bestK]) {
            for (int k = 0; k < 3; k++) if (freeU[k]) { goalType[k] = 1; goalId[k] = bestF; }
        }
    }
    // 携旗时强制安排护送：贴身的敌人才是真威胁
    if (myCarrier >= 0) {
        int mcx = eff[myCarrier].first, mcy = eff[myCarrier].second;
        int near4 = 0, near7 = 0, near9 = 0;
        for (auto& e : U) {
            if (e.team == ME || eff[e.id].first < 0 || !canFight[e.id]) continue;
            int d = manh(eff[e.id].first, eff[e.id].second, mcx, mcy);
            if (d <= 4) near4++; else if (d <= 7) near7++; else if (d <= 9) near9++;
        }
        int wantEscort = 2;   // 携旗期间全员护航（护送收益 = 保住一面旗）
        int hd = fHome[id2(mcx, mcy)]; if (hd < INF && hd / 10 <= 4) wantEscort = 0;
        if (wantEscort > 0) {
            vector<int> cand;
            for (int k = 0; k < 3; k++) if (freeU[k] && goalType[k] != 3) cand.push_back(k);
            sort(cand.begin(), cand.end(), [&](int a, int b) {
                return manh(eff[uidOf[a]].first, eff[uidOf[a]].second, mcx, mcy)
                     < manh(eff[uidOf[b]].first, eff[uidOf[b]].second, mcx, mcy);
            });
            for (int i = 0; i < (int)cand.size() && i < wantEscort; i++) {
                int k = cand[i];
                goalType[k] = 3; goalId[k] = myCarrier;
            }
        }
    }
    for (int k = 0; k < 3; k++) { prevType[k] = goalType[k]; prevId[k] = goalId[k]; }

    // ---- 计算移动 ----
    vector<int> mv(3, 0), act(3, -1);   // act: -1 无, -2 拾, -3 丢, >=0 攻击目标
    vector<int> stopR(3, 0);
    vector<vector<int>> fld(3);
    for (int k = 0; k < 3; k++) {
        int uid = uidOf[k];
        int ux = eff[uid].first, uy = eff[uid].second;
        if (ux < 0) continue;
        vector<int> pk = penFor(goalType[k] == 4 ? max(0, U[uid].hp - DMG) : U[uid].hp);
        if (goalType[k] == 4) fld[k] = fieldMulti(homeSrc, pk);
        else if (goalType[k] == 1) fld[k] = field(F[goalId[k]].x, F[goalId[k]].y, pk);
        else if (goalType[k] == 2) {
            auto p = eff[goalId[k]];
            fld[k] = (p.first >= 0) ? field(p.first, p.second, pk) : fieldMulti(homeSrc, pk);
        } else if (goalType[k] == 3) {
            auto p = eff[goalId[k]];
            if (p.first < 0) { fld[k] = fieldMulti(homeSrc, pk); goalType[k] = 0; }
            else {
                int bst = -1, bd = 1 << 20;
                for (auto& e : U) {
                    if (e.team == ME || eff[e.id].first < 0 || !canFight[e.id]) continue;
                    int d = manh(eff[e.id].first, eff[e.id].second, p.first, p.second);
                    if (d <= 8 && d < bd) { bd = d; bst = e.id; }
                }
                if (bst >= 0) fld[k] = field(eff[bst].first, eff[bst].second, pk);
                else { fld[k] = field(p.first, p.second, pk); stopR[k] = 2; }
            }
        } else if (goalType[k] == 5) {
            int s = goalId[k];
            fld[k] = field(SPOT[s].first, SPOT[s].second, pk);
        } else { fld[k] = field(SPOT[ME].first, SPOT[ME].second, pk); stopR[k] = 2; }
        if (fld[k].empty()) fld[k] = field(SPOT[ME].first, SPOT[ME].second, pk);
    }
    vector<int> pri(3);
    for (int k = 0; k < 3; k++) {
        int t = goalType[k];
        pri[k] = (t == 4 ? 0 : t == 1 ? 1 : t == 2 ? 2 : t == 3 ? 3 : 4);
    }
    vector<int> order = {0, 1, 2};
    stable_sort(order.begin(), order.end(), [&](int a, int b) { return pri[a] < pri[b]; });

    vector<int> dest(3, -1);
    vector<vector<pair<int,int>>> optAll(3);
    for (int idx = 0; idx < 3; idx++) {
        int k = order[idx];
        int uid = uidOf[k];
        int ux = eff[uid].first, uy = eff[uid].second;
        if (ux < 0) { mv[k] = 0; continue; }
        if (fld[k].empty()) { mv[k] = 0; dest[k] = id2(ux, uy); continue; }
        if (stopR[k] > 0 && fld[k][id2(ux, uy)] <= stopR[k] * 10) { mv[k] = 0; dest[k] = id2(ux, uy); continue; }
        vector<pair<int,int>> opt;
        for (int m = 0; m <= 4; m++) {
            int nx = ux + DX[m], ny = uy + DY[m];
            if (isWall(nx, ny)) continue;
            int v = fld[k][id2(nx, ny)];
            if (v >= INF) continue;
            int oc = id2(nx, ny);
            int occp = 0;
            for (int j = 0; j < 3; j++) {
                if (j == k) continue;
                int ou = uidOf[j];
                if (eff[ou].first < 0) continue;
                if (nx == eff[ou].first && ny == eff[ou].second) occp += MY_OCC;
            }
            for (auto& e : U) {
                if (e.team == ME || eff[e.id].first < 0) continue;
                if (nx == eff[e.id].first && ny == eff[e.id].second) occp += EN_OCC;
            }
            opt.push_back({v + occp, m});
        }
        sort(opt.begin(), opt.end());
        int chosen = -1;
        for (size_t t = 0; t < opt.size(); t++) {
            int m = opt[t].second;
            int nx = ux + DX[m], ny = uy + DY[m];
            int nc = id2(nx, ny);
            bool ok = true;
            for (int j = 0; j < 3; j++) {
                if (dest[j] < 0) continue;
                if (dest[j] == nc) { ok = false; break; }
                int ojx = eff[uidOf[j]].first, ojy = eff[uidOf[j]].second;
                if (nc == id2(ojx, ojy) && dest[j] == id2(ojx, ojy)) { ok = false; break; }
            }
            if (ok) { chosen = m; break; }
        }
        if (chosen < 0) chosen = 0;
        mv[k] = chosen;
        dest[k] = id2(ux + DX[chosen], uy + DY[chosen]);
        optAll[k] = opt;
    }
    // 携旗者要进的格子被队友占着不动 → 逼队友让开
    if (myCarrier >= 0) {
        int kc = -1;
        for (int k = 0; k < 3; k++) if (uidOf[k] == myCarrier) kc = k;
        if (kc >= 0 && dest[kc] >= 0) {
            int hot = dest[kc];
            for (int k = 0; k < 3; k++) {
                if (k == kc || eff[uidOf[k]].first < 0) continue;
                if (id2(eff[uidOf[k]].first, eff[uidOf[k]].second) != hot) continue;
                if (mv[k] == 0) {
                    for (size_t t = 0; t < optAll[k].size(); t++) {
                        int m = optAll[k][t].second;
                        if (m == 0) continue;
                        int nc = id2(eff[uidOf[k]].first + DX[m], eff[uidOf[k]].second + DY[m]);
                        bool ok = true;
                        for (int j = 0; j < 3; j++) if (j != k && dest[j] == nc) ok = false;
                        if (ok) { mv[k] = m; dest[k] = nc; break; }
                    }
                }
            }
        }
    }

    // ---- 拾旗 ----
    for (int k = 0; k < 3; k++) {
        int uid = uidOf[k];
        int ux = eff[uid].first, uy = eff[uid].second;
        if (ux < 0 || U[uid].flag >= 0) continue;
        int wx = ux + DX[mv[k]], wy = uy + DY[mv[k]];
        for (int i = 0; i < NF; i++) {
            if (!fGround[i] || F[i].x != wx || F[i].y != wy) continue;
            bool want = (goalType[k] == 1 && goalId[k] == i);
            if (!want) {
                int hd = fHome[id2(wx, wy)];
                int c3 = cnt3[id2(wx, wy)];
                if (hd < INF && hd / 10 <= 6) want = true;                  // 家门口顺手
                else if (hd < INF && hd / 10 <= 25 && c3 == 0) want = true; // 身边没人才顺手
            }
            if (want) act[k] = -2;
            break;
        }
    }

    // ---- 攻击（枚举组合集火）----
    {
        vector<vector<char>> canHit(U.size());
        for (auto& v : canHit) v.assign(3, 0);
        vector<vector<int>> tgts(3);
        for (int k = 0; k < 3; k++) {
            int uid = uidOf[k];
            if (eff[uid].first < 0 || U[uid].flag >= 0 || respawnNow[uid] || act[k] == -2) continue;
            int wx = eff[uid].first + DX[mv[k]], wy = eff[uid].second + DY[mv[k]];
            if (wx < 0) continue;
            for (auto& e : U) {
                if (e.team == ME || eff[e.id].first < 0 || immuneNow[e.id]) continue;
                if (canAtk(wx, wy, eff[e.id].first, eff[e.id].second)) { canHit[e.id][k] = 1; tgts[k].push_back(e.id); }
            }
        }
        // 每个可选目标的"基础价值"
        auto tval = [&](int t, int dmgSum) -> double {
            Unit& e = U[t];
            double s = 0;
            int ex = eff[t].first, ey = eff[t].second;
            if (e.flag >= 0) s += 200;
            if (dmgSum >= e.hp) s += 260;                  // 本回合能秒
            else if (e.hp <= 2 * DMG) s += 40;
            int dhome = manh(ex, ey, bx, by);
            if (dhome <= HOME_NEAR) s += (HOME_NEAR - dhome) * 3;
            if (myCarrier >= 0 && manh(ex, ey, eff[myCarrier].first, eff[myCarrier].second) <= 3) s += 150;
            for (int i = 0; i < NF; i++) if (fGround[i] && manh(ex, ey, F[i].x, F[i].y) <= 1) { s += 60; break; }
            return s;
        };
        int bestChoice[3] = {-1, -1, -1};
        double bestTotal = 0;
        vector<int> opts[3];
        for (int k = 0; k < 3; k++) {
            opts[k].push_back(-1);
            for (int t : tgts[k]) opts[k].push_back(t);
            if (opts[k].size() > 5) opts[k].resize(5);
        }
        int ci[3] = {0, 0, 0};
        while (true) {
            int assign[3];
            double tot = 0;
            for (int k = 0; k < 3; k++) assign[k] = opts[k][ci[k]];
            for (auto& e : U) {
                int d = 0;
                for (int k = 0; k < 3; k++) if (assign[k] == e.id) d += DMG;
                if (d) tot += tval(e.id, d);
            }
            if (tot > bestTotal) { bestTotal = tot; for (int k = 0; k < 3; k++) bestChoice[k] = assign[k]; }
            int p = 0;
            while (p < 3) { ci[p]++; if (ci[p] < (int)opts[p].size()) break; ci[p] = 0; p++; }
            if (p == 3) break;
        }
        for (int k = 0; k < 3; k++) if (bestChoice[k] >= 0) act[k] = bestChoice[k];
    }

    // ---- 输出 ----
    string out = to_string(TURN);
    string note;
    for (int k = 0; k < 3; k++) {
        int uid = uidOf[k];
        char m = MV[mv[k]];
        string a = "-";
        if (eff[uid].first < 0) { m = 'S'; a = "-"; }
        else if (act[k] == -2) a = "P";
        else if (act[k] == -3) a = "X";
        else if (act[k] >= 0) a = to_string(act[k]);
        out += " "; out += m; out += " "; out += a;
        if (k) note += ";";
        note += to_string(goalType[k]) + ":" + to_string(goalId[k]);
    }
    out += " #" + note;
    return out;
}

int main() {
    ios::sync_with_stdio(false);
    Game g;
    while (read_turn(g)) {
        cout << decide(g) << endl;
    }
}
