// 多阵营夺旗 bot（支持 2–15 方）
//
// 双模式架构（按人数切换，均经本地对战验证）：
//  · 2–3 方「战术模式」：每回合为 3 个角色分配任务（携旗回家 / 抢旗 / 清剿基地 /
//    支援抢旗 / 护送 / 拦截敌方携旗手 / 守旗点），用危险加权的 Dijkstra 规划移动
//    （敌方射程、局部兵力差、敌方基地禁区都计入代价），动作阶段做集火目标选择。
//  · ≥4 方「抱团模式」：实测人多混战里简单纪律胜过复杂门禁——三个单位抱团奔
//    往"往返时间最短"的旗，拾旗后携旗手沿危险加权路线绕开火线回家，射程内
//    优先攻击敌方携旗手。死亡很便宜（10 回合满血重生），以量取胜。
//
// 关键机制：死亡重生预测（重生当回合即可移动/拾旗）、自家旗点专职看守、
// 携旗必派护卫（护卫就位于携旗手阵亡点旁可接棒）、残血不拾贴身旗、
// 队友在场减危、内部移动冲突消解。
#include <bits/stdc++.h>
using namespace std;

struct Unit { int id, team, x, y, hp, flag, respawn_at; };
struct FlagT { int id; string status; int x, y, holder, return_at; };
struct EventT { string type; vector<int> args; };

struct Game {
    int teams = 0, me = 0, size = 0, totalTurns = 0, maxhp = 0, dmg = 0, respT = 0, flagRet = 0, flagCd = 0;
    vector<string> grid;
    vector<pair<int,int>> bases, spots;
    int turn = 0;
    vector<int> score;
    vector<Unit> units;
    vector<FlagT> flags;
    vector<EventT> events;
};

bool read_turn(Game& g) {
    string w;
    while (cin >> w) {
        if (w == "INIT") { cin >> g.teams >> g.me >> g.size >> g.totalTurns >> g.maxhp >> g.dmg >> g.respT >> g.flagRet >> g.flagCd; }
        else if (w == "MAP") { g.grid.assign(g.size, string()); for (auto& r : g.grid) cin >> r; }
        else if (w == "BASES") { int k; cin >> k; g.bases.resize(k); for (auto& b : g.bases) cin >> b.first >> b.second; }
        else if (w == "SPOTS") { int k; cin >> k; g.spots.resize(k); for (auto& s : g.spots) cin >> s.first >> s.second; }
        else if (w == "TURN") { cin >> g.turn; }
        else if (w == "SCORE") { g.score.resize(g.teams); for (auto& s : g.score) cin >> s; }
        else if (w == "UNITS") { int k; cin >> k; g.units.resize(k); for (auto& u : g.units) { string tag; cin >> tag >> u.id >> u.team >> u.x >> u.y >> u.hp >> u.flag >> u.respawn_at; } }
        else if (w == "FLAGS") { int k; cin >> k; g.flags.resize(k); for (auto& f : g.flags) { string tag; cin >> tag >> f.id >> f.status >> f.x >> f.y >> f.holder >> f.return_at; } }
        else if (w == "EVENTS") { int k; cin >> k; g.events.assign(k, {}); string line; getline(cin, line); for (auto& e : g.events) { getline(cin, line); istringstream in(line); string tag; in >> tag >> e.type; for (int v; in >> v;) e.args.push_back(v); } }
        else if (w == "END") return true;
    }
    return false;
}

// ---------------- 全局 ----------------
int S, N, ME, MAXHP, DMG, TOTALTURNS;
vector<string> G;
vector<vector<int>> dB;               // dB[team][cell] = 到该阵营基地(3x3)的 BFS 距离
vector<float> baseDanger;             // 敌方基地附近静态危险
vector<pair<int,int>> spotsG, myBase9, mySpawnOrder;
pair<int,int> C0;
bool inited = false;

inline int ID(int x, int y) { return y * S + x; }
inline bool openc(int x, int y) { return x >= 0 && x < S && y >= 0 && y < S && G[y][x] == '.'; }
inline int md(int ax, int ay, int bx, int by) { return abs(ax - bx) + abs(ay - by); }

bool canAtk(int ax, int ay, int bx, int by) {
    int dx = bx - ax, dy = by - ay;
    if (abs(dx) + abs(dy) > 2) return false;
    if (abs(dx) == 2 && dy == 0) return openc(ax + dx / 2, ay);
    if (abs(dy) == 2 && dx == 0) return openc(ax, ay + dy / 2);
    if (abs(dx) == 1 && abs(dy) == 1) return openc(ax + dx, ay) || openc(ax, ay + dy);
    return true;
}

vector<int> bfsCells(const vector<pair<int,int>>& src) {
    vector<int> d(S * S, INT_MAX);
    deque<int> q;
    for (auto& p : src) if (openc(p.first, p.second) && d[ID(p.first, p.second)] == INT_MAX) { d[ID(p.first, p.second)] = 0; q.push_back(ID(p.first, p.second)); }
    const int DX[4] = {1,-1,0,0}, DY[4] = {0,0,1,-1};
    while (!q.empty()) {
        int c = q.front(); q.pop_front();
        int x = c % S, y = c / S, dc = d[c] + 1;
        for (int i = 0; i < 4; i++) {
            int nx = x + DX[i], ny = y + DY[i];
            if (openc(nx, ny)) { int nc = ID(nx, ny); if (d[nc] > dc) { d[nc] = dc; q.push_back(nc); } }
        }
    }
    return d;
}

void doInit(Game& g) {
    S = g.size; N = g.teams; ME = g.me; MAXHP = g.maxhp; DMG = g.dmg; TOTALTURNS = g.totalTurns; G = g.grid;
    C0 = {S / 2, S / 2};
    baseDanger.assign(S * S, 0.f);
    for (int k = 0; k < N; k++) {
        int bx = g.bases[k].first, by = g.bases[k].second;
        vector<pair<int,int>> cells;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) cells.push_back({bx + dx, by + dy});
        dB.push_back(bfsCells(cells));
        if (k == ME) myBase9 = cells;
        else for (int c = 0; c < S * S; c++) {
            int dd = dB[k][c];
            baseDanger[c] += dd == 0 ? 0.8f : dd == 1 ? 0.35f : dd == 2 ? 0.12f : 0.f;
        }
    }
    int bcx = g.bases[ME].first, bcy = g.bases[ME].second;
    mySpawnOrder = myBase9;
    sort(mySpawnOrder.begin(), mySpawnOrder.end(), [&](const pair<int,int>& a, const pair<int,int>& b) {
        int da = md(a.first, a.second, C0.first, C0.second), db = md(b.first, b.second, C0.first, C0.second);
        if (da != db) return da < db;
        return abs(a.first - bcx) + 2 * abs(a.second - bcy) < abs(b.first - bcx) + 2 * abs(b.second - bcy);
    });
    spotsG = g.spots;
    inited = true;
}

// 角色
enum { R_CARRY = 0, R_FETCH, R_CLEAR, R_SUPPORT, R_ESCORT, R_HUNT, R_CAMP, R_IDLE, R_DEAD };


// 简单模式（对照实验/备用）：携旗回家，否则奔最近可抢的旗，射程内选最优目标攻击
string decideSimple(Game& g) {
    int T = g.turn;
    const int DX[4] = {1,-1,0,0}, DY[4] = {0,0,1,-1};
    Unit raw[3] = {};
    for (auto& u : g.units) if (u.team == ME) raw[u.id - 3 * ME] = u;
    set<int> occ;
    for (auto& u : g.units) if (u.x >= 0) occ.insert(ID(u.x, u.y));
    int ux[3], uy[3], uhp[3], uflag[3]; bool alive[3], respNow[3];
    for (int k = 0; k < 3; k++) {
        ux[k] = raw[k].x; uy[k] = raw[k].y; uhp[k] = raw[k].hp; uflag[k] = raw[k].flag;
        alive[k] = ux[k] >= 0; respNow[k] = false;
        if (!alive[k] && raw[k].respawn_at == T)
            for (auto& p : mySpawnOrder) { int c = ID(p.first, p.second);
                if (!occ.count(c)) { ux[k] = p.first; uy[k] = p.second; uhp[k] = MAXHP; uflag[k] = -1; alive[k] = true; respNow[k] = true; occ.insert(c); break; } }
    }
    vector<Unit> en;
    for (auto& u : g.units) if (u.team != ME && u.x >= 0) en.push_back(u);
    // 危险图（敌方非携旗单位的射程）
    vector<float> danger(S * S, 0.f);
    for (auto& e : en) {
        if (e.flag >= 0) continue;
        for (int dy = -4; dy <= 4; dy++) for (int dx = -4; dx <= 4; dx++) {
            int d = abs(dx) + abs(dy); if (d == 0 || d > 4) continue;
            int x = e.x + dx, y = e.y + dy;
            if (!openc(x, y)) continue;
            float w = 0;
            if (d <= 2) w = canAtk(e.x, e.y, x, y) ? 1.f : 0.3f;
            else if (d == 3) w = 0.45f; else w = 0.12f;
            danger[ID(x, y)] += w;
        }
    }
    for (int k = 0; k < 3; k++) if (alive[k]) {
        for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
            if (abs(dx) + abs(dy) == 0) continue;
            int x = ux[k] + dx, y = uy[k] + dy;
            if (openc(x, y)) danger[ID(x, y)] -= 0.25f;
        }
    }
    for (int c = 0; c < S * S; c++) if (danger[c] < 0.f) danger[c] = 0.f;
    // 可见旗子
    vector<pair<int,int>> afv;
    for (auto& f : g.flags) if ((f.status == "home" || f.status == "dropped") && f.return_at != T) afv.push_back({f.x, f.y});
    string out = to_string(T);
    set<int> of;
    for (int k = 0; k < 3; k++) if (alive[k]) of.insert(ID(ux[k], uy[k]));
    for (int k = 0; k < 3; k++) {
        string mv = "S", act = "-";
        if (alive[k]) {
            vector<pair<int,int>> targs;
            if (uflag[k] >= 0) targs = myBase9;
            else {
                int bd = INT_MAX; pair<int,int> best{-1,-1};
                for (auto& p : afv) {
                    int d = md(ux[k], uy[k], p.first, p.second) + dB[ME][ID(p.first, p.second)];
                    if (d < bd) { bd = d; best = p; }
                }
                if (best.first >= 0) targs = {best};
            }
            // BFS 找一步
            int nxk = ux[k], nyk = uy[k];
            if (!targs.empty()) {
                float wD = (uflag[k] >= 0) ? 1.5f : 0.6f;    // 携旗手绕开火线
                vector<float> D(S * S, 1e18f);
                priority_queue<pair<float,int>, vector<pair<float,int>>, greater<pair<float,int>>> pq;
                for (auto& p : targs) { int c = ID(p.first, p.second); if (openc(p.first, p.second) && D[c] > 0) { D[c] = 0.f; pq.push({0.f, c}); } }
                while (!pq.empty()) {
                    pair<float,int> pr = pq.top(); pq.pop();
                    float dd = pr.first; int c = pr.second;
                    if (dd > D[c] + 1e-6f) continue;
                    int x = c % S, y = c / S;
                    for (int i = 0; i < 4; i++) {
                        int ax = x + DX[i], ay = y + DY[i];
                        if (!openc(ax, ay)) continue;
                        int ac = ID(ax, ay);
                        float cost = 1.f + wD * danger[ac];
                        if (dd + cost < D[ac] - 1e-6f) { D[ac] = dd + cost; pq.push({D[ac], ac}); }
                    }
                }
                int cur = ID(ux[k], uy[k]);
                if (D[cur] > 0.f && D[cur] < 1e17f) {
                    float bc = 1e18f; int bs = -1;
                    for (int i = 0; i < 4; i++) {
                        int ax = ux[k] + DX[i], ay = uy[k] + DY[i];
                        if (!openc(ax, ay)) continue;
                        int ac = ID(ax, ay);
                        if (of.count(ac)) continue;
                        bool enemyOn = false;
                        for (auto& e : en) if (e.x == ax && e.y == ay) enemyOn = true;
                        if (enemyOn) continue;
                        float v = 1.f + wD * danger[ac] + D[ac];
                        if (v < bc) { bc = v; bs = ac; }
                    }
                    if (bs >= 0) { of.erase(cur); of.insert(bs); nxk = bs % S; nyk = bs / S; }
                }
            }
            int ddx = nxk - ux[k], ddy = nyk - uy[k];
            if (ddx == 1) mv = "R"; else if (ddx == -1) mv = "L"; else if (ddy == 1) mv = "D"; else if (ddy == -1) mv = "U";
            if (uflag[k] < 0) {
                bool onF = false;
                for (auto& f : g.flags) if ((f.status == "home" || f.status == "dropped") && f.return_at != T && f.x == nxk && f.y == nyk) onF = true;
                if (onF) act = "P";
                else if (!respNow[k]) {
                    int bestT = -1; float bs2 = -1e9;
                    for (auto& e : en) {
                        if (!canAtk(nxk, nyk, e.x, e.y)) continue;
                        float s2 = (e.flag >= 0 ? 100.f : 0.f) + (MAXHP - e.hp) * 0.3f;
                        if (s2 > bs2) { bs2 = s2; bestT = e.id; }
                    }
                    if (bestT >= 0) act = to_string(bestT);
                }
            }
        }
        out += " " + mv + " " + act;
    }
    return out;
}

string decide(Game& g) {
    if (!inited) doInit(g);
    int T = g.turn, total = TOTALTURNS;
    if (N >= 4) return decideSimple(g);   // 4 人以上：抱团抢旗的简单模式胜过复杂门禁
    const int DX[4] = {1,-1,0,0}, DY[4] = {0,0,1,-1};

    // ---- 我的角色（含本回合重生的预测位置）
    Unit raw[3] = {};
    for (auto& u : g.units) if (u.team == ME) raw[u.id - 3 * ME] = u;
    int ux[3], uy[3], uhp[3], uflag[3]; bool alive[3], respNow[3];
    set<int> occ;
    for (auto& u : g.units) if (u.x >= 0) occ.insert(ID(u.x, u.y));
    for (int k = 0; k < 3; k++) {
        ux[k] = raw[k].x; uy[k] = raw[k].y; uhp[k] = raw[k].hp; uflag[k] = raw[k].flag;
        alive[k] = ux[k] >= 0; respNow[k] = false;
        if (!alive[k] && raw[k].respawn_at == T) {
            for (auto& p : mySpawnOrder) {
                int c = ID(p.first, p.second);
                if (occ.find(c) == occ.end()) { ux[k] = p.first; uy[k] = p.second; uhp[k] = MAXHP; uflag[k] = -1; alive[k] = true; respNow[k] = true; occ.insert(c); break; }
            }
        }
    }
    vector<Unit> en;
    for (auto& u : g.units) if (u.team != ME && u.x >= 0) en.push_back(u);

    // ---- 危险图
    vector<float> danger(S * S, 0.f);
    for (auto& e : en) {
        if (e.flag >= 0) continue;                    // 携旗者不能攻击
        for (int dy = -4; dy <= 4; dy++) for (int dx = -4; dx <= 4; dx++) {
            int d = abs(dx) + abs(dy); if (d == 0 || d > 4) continue;
            int x = e.x + dx, y = e.y + dy;
            if (!openc(x, y)) continue;
            float w = 0;
            if (d <= 2) w = canAtk(e.x, e.y, x, y) ? 1.f : 0.3f;
            else if (d == 3) w = 0.45f; else w = 0.12f;
            danger[ID(x, y)] += w;
        }
    }
    for (int c = 0; c < S * S; c++) danger[c] += baseDanger[c];
    // 盟友减危：附近有队友时更敢打（护送/结伴作战）
    for (int k = 0; k < 3; k++) if (alive[k]) {
        for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
            int d = abs(dx) + abs(dy); if (d == 0) continue;
            int x = ux[k] + dx, y = uy[k] + dy;
            if (!openc(x, y)) continue;
            danger[ID(x, y)] -= 0.25f;
        }
    }
    for (int c = 0; c < S * S; c++) if (danger[c] < 0.f) danger[c] = 0.f;

    // 局部兵力：敌人（非携旗）与队友在 3 步内的数量，用于避免落单送死
    vector<int> enN(S * S, 0), allySplash(S * S, 0);
    for (auto& e : en) {
        if (e.flag >= 0) continue;
        for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++) {
            if (abs(dx) + abs(dy) > 3) continue;
            int x = e.x + dx, y = e.y + dy;
            if (openc(x, y)) enN[ID(x, y)]++;
        }
    }
    for (int k = 0; k < 3; k++) if (alive[k]) {
        for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++) {
            if (abs(dx) + abs(dy) > 3) continue;
            int x = ux[k] + dx, y = uy[k] + dy;
            if (openc(x, y)) allySplash[ID(x, y)]++;
        }
    }

    // ---- 可见旗子
    struct AF { int x, y, id, dh; };
    vector<AF> af; vector<vector<int>> fdist;
    for (auto& f : g.flags) if ((f.status == "home" || f.status == "dropped") && f.return_at != T) {
        af.push_back({f.x, f.y, f.id, dB[ME][ID(f.x, f.y)]});
        fdist.push_back(bfsCells({{f.x, f.y}}));
    }
    int naf = af.size();

    // ---- 角色分配
    int role[3], parm[3], parm2[3];
    for (int k = 0; k < 3; k++) { role[k] = alive[k] ? R_IDLE : R_DEAD; parm[k] = parm2[k] = -1; }
    for (int k = 0; k < 3; k++) if (alive[k] && uflag[k] >= 0) role[k] = R_CARRY;

    // 抢旗：代价 = 我的往返时间 - 竞争优势
    struct PR { float cost; int u, f, margin; };
    vector<PR> prs;
    int centerD = dB[ME][ID(C0.first, C0.second)];
    vector<int> enNear(naf, INT_MAX);      // 敌方到旗的最短距离
    for (int i = 0; i < naf; i++) for (auto& e : en) {
        int d2 = fdist[i][ID(e.x, e.y)];
        if (d2 < enNear[i]) enNear[i] = d2;
    }
    for (int k = 0; k < 3; k++) if (alive[k] && role[k] == R_IDLE) {
        for (int i = 0; i < naf; i++) {
            int d1 = fdist[i][ID(ux[k], uy[k])]; if (d1 == INT_MAX) continue;
            int myT = d1 + af[i].dh;
            if (T + myT + 2 > total) continue;
            int enT = INT_MAX;
            for (auto& e : en) {
                int d2 = fdist[i][ID(e.x, e.y)]; if (d2 == INT_MAX) continue;
                int v = d2 + dB[e.team][ID(af[i].x, af[i].y)];
                if (v < enT) enT = v;
            }
            if (uhp[k] <= 34 && enNear[i] <= 4) continue;                 // 残血且敌人贴身：不拾旗
            if (uhp[k] <= 34 && af[i].dh > centerD * 2 / 3 && enNear[i] <= 6) continue;   // 残血只跑家门口
            int margin = (enT == INT_MAX) ? 8 : enT - myT;
            // 多人局：比我更近的敌方单位不超过 1 个、或旗离家近、或总时间不吃亏，就去抢
            int closerLim = max(1, N / 3);   // 人多时排进最近几名就有机会
            int closer = 0;
            for (auto& e : en) if (e.flag < 0 && fdist[i][ID(e.x, e.y)] < d1) closer++;
            bool homeZone = af[i].dh <= centerD * 2 / 3;
            if (N >= 5 && af[i].dh > centerD * 3 / 4) continue;   // 大图：太远的旗运不回来
                        if (margin < -8 && !(homeZone || (N >= 3 && closer <= closerLim))) continue;
            prs.push_back({(float)(myT - 0.6 * max(min(margin, 8), -8) - (homeZone ? 3 : 0)) + (MAXHP - uhp[k]) * 0.3f, k, i, margin});
        }
    }
    sort(prs.begin(), prs.end(), [](const PR& a, const PR& b) { return a.cost < b.cost; });
    vector<char> fTaken(naf, 0);
    int fMargin[3]; bool supported[3];
    for (int k = 0; k < 3; k++) { fMargin[k] = 1000; supported[k] = false; }
    for (auto& p : prs) if (role[p.u] == R_IDLE && !fTaken[p.f]) { role[p.u] = R_FETCH; parm[p.u] = p.f; fTaken[p.f] = 1; fMargin[p.u] = p.margin; }

    vector<int> left;
    for (int k = 0; k < 3; k++) if (alive[k] && role[k] == R_IDLE) left.push_back(k);

    // 专职旗点看守（多人局）：最近的空闲单位站上我的旗点，旗一刷新立刻拾取
    if (!left.empty() && N >= 3) {
        int mySpotIdx = 0, bd0 = INT_MAX;
        for (int j = 0; j < (int)spotsG.size(); j++) {
            int d = dB[ME][ID(spotsG[j].first, spotsG[j].second)];
            if (d < bd0) { bd0 = d; mySpotIdx = j; }
        }
        bool fetcherThere = false;
        for (int i = 0; i < naf; i++)
            if (fTaken[i] && af[i].x == spotsG[mySpotIdx].first && af[i].y == spotsG[mySpotIdx].second) fetcherThere = true;
        int bi = 0, bdu = INT_MAX;
        for (int i = 0; i < (int)left.size(); i++) {
            int d = md(ux[left[i]], uy[left[i]], spotsG[mySpotIdx].first, spotsG[mySpotIdx].second);
            if (d < bdu) { bdu = d; bi = i; }
        }
        // 旗点已有抢旗队友时改守中心（第二近的刷新点）
        int sitIdx = mySpotIdx;
        if (fetcherThere) {
            int bc2 = -1, bdc = INT_MAX;
            for (int j = 0; j < (int)spotsG.size(); j++) {
                int d = dB[ME][ID(spotsG[j].first, spotsG[j].second)];
                if (d < bdc && d > bd0) { bdc = d; bc2 = j; }
            }
            if (bc2 >= 0) sitIdx = bc2;
        }
        role[left[bi]] = R_CAMP; parm[left[bi]] = sitIdx; left.erase(left.begin() + bi);
    }

    // 清理基地附近的敌人（不清掉会打断交旗）
    {
        vector<int> campers;
        for (auto& e : en) if (dB[ME][ID(e.x, e.y)] <= 2) campers.push_back(e.id);
        vector<char> camTaken(campers.size(), 0);
        while (!left.empty()) {
            int bi = -1, bj = -1, bd = INT_MAX;
            for (int i = 0; i < (int)left.size(); i++) for (int j = 0; j < (int)campers.size(); j++) {
                if (camTaken[j]) continue;
                int d = md(ux[left[i]], uy[left[i]], g.units[campers[j]].x, g.units[campers[j]].y);
                if (d < bd) { bd = d; bi = i; bj = j; }
            }
            if (bi < 0) break;
            role[left[bi]] = R_CLEAR; parm[left[bi]] = campers[bj]; camTaken[bj] = 1;
            left.erase(left.begin() + bi);
        }
    }

    // 护送优先于低价值抢旗：没空闲单位时，抽调代价最高的抢旗手来护送
    for (int cs = 0; cs < 3 && left.empty() && N >= 3; cs++) if (alive[cs] && uflag[cs] >= 0) {
        int bk = -1; float bcost = -1e9;
        for (int k = 0; k < 3; k++) if (role[k] == R_FETCH) {
            float c = (float)fMargin[k] + af[parm[k]].dh * 0.3f;   // 优势越小、旗越远越优先被抽
            if (c > bcost) { bcost = c; bk = k; }
        }
        if (bk >= 0) { left.push_back(bk); role[bk] = R_IDLE; fTaken[parm[bk]] = 0; }
    }

    // 护送携旗队友：大图一律双护卫（护卫跟在携旗手旁，被打死时就地接棒），
    // 空闲单位不够就从最不划算的抢旗任务里抽调
    auto stealFetcher = [&]() -> bool {
        int bk = -1; float bcost = -1e9;
        for (int k = 0; k < 3; k++) if (role[k] == R_FETCH) {
            float c = (float)fMargin[k] + af[parm[k]].dh * 0.3f;
            if (c > bcost) { bcost = c; bk = k; }
        }
        if (bk >= 0) { left.push_back(bk); role[bk] = R_IDLE; fTaken[parm[bk]] = 0; return true; }
        return false;
    };
    for (int cs = 0; cs < 3; cs++) if (alive[cs] && uflag[cs] >= 0) {
        int nearE = INT_MAX, threat = 0;
        for (auto& e : en) {
            int d = md(e.x, e.y, ux[cs], uy[cs]);
            if (d < nearE) nearE = d;
            if (d <= 4) threat++;
        }
        if (nearE > (N <= 2 ? 8 : 12)) continue;
        int need = (N >= 5) ? 2 : (threat >= 2 ? 2 : 1);
        int got = 0;
        while (got < need && (!left.empty() || (N >= 3 && stealFetcher()))) {
            int bi = 0, bd = INT_MAX;
            for (int i = 0; i < (int)left.size(); i++) { int d = md(ux[left[i]], uy[left[i]], ux[cs], uy[cs]); if (d < bd) { bd = d; bi = i; } }
            role[left[bi]] = R_ESCORT; parm[left[bi]] = cs; left.erase(left.begin() + bi);
            got++;
        }
    }

    // 支援被争夺的抢旗队友（敌人比队友晚到不足 6 步就算有争夺）
    while (!left.empty()) {
        int bk = -1; int bestc = INT_MAX;
        for (int k = 0; k < 3; k++) if (role[k] == R_FETCH && !supported[k]) {
            int d1 = fdist[parm[k]][ID(ux[k], uy[k])];
            int contest = (enNear[parm[k]] == INT_MAX) ? INT_MAX : enNear[parm[k]] - d1;
            if (contest < bestc && contest < 6) { bestc = contest; bk = k; }
        }
        if (bk < 0) break;
        supported[bk] = 1;
        int bi = 0, bd = INT_MAX;
        for (int i = 0; i < (int)left.size(); i++) { int d = md(ux[left[i]], uy[left[i]], ux[bk], uy[bk]); if (d < bd) { bd = d; bi = i; } }
        // 支援者跟到旗附近待命，帮抢也帮护
        role[left[bi]] = R_SUPPORT; parm[left[bi]] = bk; parm2[left[bi]] = parm[bk]; left.erase(left.begin() + bi);
    }

    // 拦截敌方携旗手：在其回家路径上找能提前到位的伏击点（仅少人局）
    for (int li = 0; li < (int)left.size(); ) {
        int k = left[li];
        int bestPrey = -1, bestCell = -1; float bestAdv = 1e9f;
        if (N > 6) { li++; continue; }
        for (auto& e : en) {
            if (e.flag < 0) continue;
            int home = dB[e.team][ID(e.x, e.y)]; if (home < 3) continue;
            int allies = 0;
            for (auto& a : en) if (a.id != e.id && a.team == e.team && a.flag < 0 && md(a.x, a.y, e.x, e.y) <= 2) allies++;
            if (allies >= 2) continue;
            vector<int> myD = bfsCells({{ux[k], uy[k]}});
            for (int c = 0; c < S * S; c++) {
                if (myD[c] == INT_MAX) continue;
                int dp = dB[e.team][c]; if (dp < 2 || dp >= home) continue;
                float adv = (float)myD[c] + dp - home;
                if (adv < bestAdv) { bestAdv = adv; bestPrey = e.id; bestCell = c; }
            }
        }
        if (bestPrey >= 0 && bestAdv <= -1.f) { role[k] = R_HUNT; parm[k] = bestPrey; parm2[k] = bestCell; left.erase(left.begin() + li); }
        else li++;
    }

    // 守旗点（站上去就能第一时间拾取）。之后的空闲单位在大图做机会型抢旗
    if (!left.empty()) {
        vector<char> spotTaken(spotsG.size(), 0);
        int nCamp = 0;
        for (int li = 0; li < (int)left.size(); li++) {
            int k = left[li];
            if (nCamp >= 1 && N >= 5) {
                int bi = -1, bc = INT_MAX;
                for (int i = 0; i < naf; i++) {
                    if (fTaken[i] || af[i].dh > centerD * 3 / 4) continue;
                    int d1 = fdist[i][ID(ux[k], uy[k])];
                    if (d1 == INT_MAX || T + d1 + af[i].dh + 2 > total) continue;
                    if (d1 + af[i].dh < bc) { bc = d1 + af[i].dh; bi = i; }
                }
                if (bi >= 0) { role[k] = R_FETCH; parm[k] = bi; parm2[k] = bi; fTaken[bi] = 1; fMargin[k] = 0; supported[k] = true; continue; }
            }
            int bj = -1, bs = INT_MAX;
            for (int j = 0; j < (int)spotsG.size(); j++) {
                if (spotTaken[j]) continue;
                int dhome = dB[ME][ID(spotsG[j].first, spotsG[j].second)];
                if (dhome > centerD + 2) continue;                // 不守太远的旗点
                bool fetcherGoing = false;
                for (int i = 0; i < naf; i++) if (fTaken[i] && af[i].x == spotsG[j].first && af[i].y == spotsG[j].second) fetcherGoing = true;
                if (fetcherGoing) continue;
                int sc = 2 * dhome + md(ux[k], uy[k], spotsG[j].first, spotsG[j].second);
                if (sc < bs) { bs = sc; bj = j; }
            }
            if (bj >= 0) { role[k] = R_CAMP; parm[k] = bj; spotTaken[bj] = 1; nCamp++; }
            else if (dB[ME][ID(ux[k], uy[k])] > 3) { role[k] = R_CAMP; parm[k] = -1; }
        }
    }

    // 近距离追杀敌方携旗手：3 步内的携旗敌人直接追（它跑不掉，保持 2 格距离就能每回合命中）
    for (int k = 0; k < 3; k++) {
        if (!alive[k] || uflag[k] >= 0 || role[k] == R_CARRY || role[k] == R_CLEAR || N > 6) continue;
        int bc = -1, bdd = 4;
        for (auto& e : en) if (e.flag >= 0) {
            int d = md(ux[k], uy[k], e.x, e.y);
            if (d < bdd) { bdd = d; bc = ID(e.x, e.y); }
        }
        if (bc >= 0) {
            bool onFlag = false;
            for (auto& f : g.flags) if ((f.status == "home" || f.status == "dropped") && f.return_at != T && f.x == ux[k] && f.y == uy[k]) onFlag = true;
            if (!onFlag) { role[k] = R_HUNT; parm[k] = -1; parm2[k] = bc; }
        }
    }

    // ---- 移动规划（按角色优先级）
    int order3[3] = {-1, -1, -1}; int oi = 0;
    for (int r = R_CARRY; r <= R_IDLE; r++) for (int k = 0; k < 3; k++) if (role[k] == r) order3[oi++] = k;
    set<int> of;
    for (int k = 0; k < 3; k++) if (alive[k]) of.insert(ID(ux[k], uy[k]));
    int nx[3], ny[3];
    for (int k = 0; k < 3; k++) { nx[k] = ux[k]; ny[k] = uy[k]; }

    vector<float> D;
    for (int o = 0; o < 3; o++) {
        int k = order3[o];
        if (k < 0 || !alive[k] || role[k] == R_IDLE || role[k] == R_DEAD) continue;
        vector<pair<int,int>> targs; float wD; int allow = -1;
        switch (role[k]) {
            case R_CARRY: targs = myBase9; wD = 2.2f; break;
            case R_FETCH: {
                int fx = af[parm[k]].x, fy = af[parm[k]].y;
                int enF = 0, alF = 0;
                for (auto& e : en) if (md(e.x, e.y, fx, fy) <= 3) enF++;
                for (int j = 0; j < 3; j++) if (alive[j] && j != k && md(ux[j], uy[j], fx, fy) <= 3) alF++;
                if (enF >= alF + 2) {
                    // 敌众我寡明显：不冲上去送死，占 2 格火力位射击靠近者
                    for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
                        int d = abs(dx) + abs(dy);
                        if (d == 0 || d > 2) continue;
                        int x = fx + dx, y = fy + dy;
                        if (openc(x, y) && canAtk(x, y, fx, fy)) targs.push_back({x, y});
                    }
                    if (targs.empty()) targs.push_back({fx, fy});
                } else {
                    targs = {{fx, fy}};
                    allow = ID(fx, fy);
                }
                wD = 1.1f; break;
            }
            case R_CLEAR: {
                Unit& e = g.units[parm[k]];
                for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
                    int x = e.x + dx, y = e.y + dy;
                    if (openc(x, y) && canAtk(x, y, e.x, e.y)) targs.push_back({x, y});
                }
                wD = 0.8f; break;
            }
            case R_SUPPORT: {
                int p = parm[k];
                if (md(ux[k], uy[k], ux[p], uy[p]) <= 1) targs.push_back({ux[k], uy[k]});
                for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                    if (abs(dx) + abs(dy) != 1) continue;
                    int x = ux[p] + dx, y = uy[p] + dy;
                    if (openc(x, y)) targs.push_back({x, y});
                }
                wD = 1.0f; break;
            }
            case R_ESCORT: {
                int p = parm[k];
                if (md(ux[k], uy[k], ux[p], uy[p]) <= 1) targs.push_back({ux[k], uy[k]});
                for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                    if (abs(dx) + abs(dy) != 1) continue;
                    int x = ux[p] + dx, y = uy[p] + dy;
                    if (openc(x, y)) targs.push_back({x, y});
                }
                wD = role[k] == R_SUPPORT ? 1.0f : 0.8f; break;
            }
            case R_HUNT: targs = {{parm2[k] % S, parm2[k] / S}}; wD = 0.7f; break;
            case R_CAMP:
                if (parm[k] >= 0) targs = {spotsG[parm[k]]};
                else targs = myBase9;
                wD = 1.3f; break;
            default: continue;
        }
        if (targs.empty()) continue;
        // 残血也照常作战（死亡很便宜，10 回合满血重生）；携旗者才惜命
        float thr = (role[k] == R_CARRY) ? 0.45f * uhp[k] : max(0.85f * uhp[k], 0.6f * MAXHP);
        float imbW = (role[k] == R_CARRY) ? 1.0f : (role[k] == R_FETCH || role[k] == R_CAMP) ? 0.7f : 0.25f;
        auto ldanger = [&](int c) -> float {
            float imb = (float)enN[c] - ((float)allySplash[c] - (md(ux[k], uy[k], c % S, c / S) <= 3 ? 1.f : 0.f));
            return danger[c] + imbW * min(max(imb, 0.f), 2.f);
        };
        D.assign(S * S, 1e18f);
        priority_queue<pair<float,int>, vector<pair<float,int>>, greater<pair<float,int>>> pq;
        for (auto& p : targs) { int c = ID(p.first, p.second); if (openc(p.first, p.second) && D[c] > 0) { D[c] = 0.f; pq.push({0.f, c}); } }
        auto pen = [&](int c) -> bool { return 34.f * ldanger(c) >= thr; };
        while (!pq.empty()) {
            pair<float,int> pr = pq.top(); pq.pop();
            float d = pr.first; int c = pr.second;
            if (d > D[c] + 1e-6f) continue;
            int x = c % S, y = c / S;
            for (int i = 0; i < 4; i++) {
                int ax = x + DX[i], ay = y + DY[i];
                if (!openc(ax, ay)) continue;
                int ac = ID(ax, ay);
                float cost = 1.f + wD * ldanger(ac) + ((pen(ac) && ac != allow) ? 30.f : 0.f);
                if (d + cost < D[ac] - 1e-6f) { D[ac] = d + cost; pq.push({D[ac], ac}); }
            }
        }
        int cur = ID(ux[k], uy[k]);
        if (D[cur] <= 0.f || D[cur] > 1e17f) continue;           // 已在目标 / 不可达：原地
        int best = -1; float bestv = 1e18f;
        for (int i = 0; i < 4; i++) {
            int ax = ux[k] + DX[i], ay = uy[k] + DY[i];
            if (!openc(ax, ay)) continue;
            int ac = ID(ax, ay);
            if (of.count(ac)) continue;                           // 被队友占用/认领
            bool enemyOn = false;
            for (auto& e : en) if (e.x == ax && e.y == ay) { enemyOn = true; break; }
            if (enemyOn) continue;
            float v = 1.f + wD * ldanger(ac) + ((pen(ac) && ac != allow) ? 30.f : 0.f) + D[ac];
            if (v < bestv) { bestv = v; best = ac; }
        }
        if (best < 0) continue;
        if (pen(best) && !pen(cur) && ldanger(cur) < ldanger(best)) continue;   // 不踏进致命格，先等一步
        of.erase(cur); of.insert(best);
        nx[k] = best % S; ny[k] = best / S;
    }

    // ---- 动作
    string out = to_string(T);
    for (int k = 0; k < 3; k++) {
        string mv = "S", act = "-";
        if (alive[k]) {
            int ddx = nx[k] - ux[k], ddy = ny[k] - uy[k];
            if (ddx == 1) mv = "R"; else if (ddx == -1) mv = "L"; else if (ddy == 1) mv = "D"; else if (ddy == -1) mv = "U";
            if (uflag[k] < 0) {
                bool onF = false;
                for (auto& f : g.flags) if ((f.status == "home" || f.status == "dropped") && f.return_at != T && f.x == nx[k] && f.y == ny[k]) onF = true;
                if (onF) act = "P";
                else if (!respNow[k]) {
                    int bestT = -1; float bs = -1e9;
                    for (auto& e : en) {
                        if (!canAtk(nx[k], ny[k], e.x, e.y)) continue;
                        float s = 0;
                        if (e.flag >= 0) s += 100;                                    // 敌方携旗手
                        if (dB[ME][ID(e.x, e.y)] <= 2) s += 80;                      // 在我基地附近
                        for (auto& f : g.flags) if ((f.status == "home" || f.status == "dropped") && f.return_at != T && f.x == e.x && f.y == e.y) s += 90;   // 站在旗上
                        for (int j = 0; j < 3; j++) if (alive[j] && uflag[j] >= 0 && md(e.x, e.y, nx[j], ny[j]) <= 3) s += 50;
                        int hits = 0;
                        for (int j = 0; j < 3; j++) if (alive[j] && uflag[j] < 0 && !respNow[j] && canAtk(nx[j], ny[j], e.x, e.y)) hits++;
                        if (e.hp <= hits * DMG) s += 55;                             // 集火可击杀
                        s += (MAXHP - e.hp) * 0.25f;
                        if (s > bs) { bs = s; bestT = e.id; }
                    }
                    if (bestT >= 0) act = to_string(bestT);
                }
            }
        }
        out += " " + mv + " " + act;
    }
    static const char* rname[9] = {"cy","fe","cl","su","es","hu","ca","id","xx"};
    string note = "#";
    for (int k = 0; k < 3; k++) { if (k) note += ";"; note += rname[role[k] < 9 ? role[k] : 8]; }
    out += " " + note;
    return out;
}

int main() {
    ios::sync_with_stdio(false);
    Game g;
    while (read_turn(g)) cout << decide(g) << endl;
}
