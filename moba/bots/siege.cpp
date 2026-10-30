// 示例 bot · 抱团推进：五人一股绳，沿中路滚动碾压。
//  - 集结点 = 己方中路最靠前的小兵（没有兵就缩到本路最靠前的塔后等下一波，每 12 回合一波）。
//  - 队形：谁都离队首不超过 3 格，掉队先归队；队首由"离敌方水晶最近的我方英雄"担任。
//  - 集火：全队本回合锁定同一个目标（残血敌方英雄 > 射程内敌兵 > 有小兵扛着的塔 > 水晶）。
//  - 兵线纪律：没小兵扛塔就全队在塔射程外驻停，绝不头硬；水晶守家优先于推进。
//  - 残血（<35%）的自己回泉水，回满自动归队，其他人不等。
#include <bits/stdc++.h>
using namespace std;

struct ClassInfo { int hp, atk, rng, cd; string pos, cls, skill; };
struct Tower { int id, team, lane, tier, x, y, maxhp, hp, atk; };
struct Nexus { int id, team, x, y, maxhp, hp; };
struct Camp { int id, x, y, maxhp, atk, hp, resp; };
struct Minion { int id, team, lane, x, y, hp; };
struct Hero { int id, team, x, y, hp, maxhp, level, xp, cd, respawn; };

struct Game {
    int teams = 0, me = 0, S = 0, turns = 0, diff = 0, waveSize = 0, waveGap = 0, minionHp = 0, minionAtk = 0,
        towerRange = 0, respawn = 0, regen = 0, fountain = 0, minionCap = 0, aggro = 0, npcHeroes = 0;
    vector<string> mapRows;
    vector<ClassInfo> cls;
    vector<int> levelXp;
    vector<pair<int, int>> spawns;
    vector<array<int, 4>> bases;
    vector<Tower> towers;
    vector<Nexus> nexuses;
    vector<Camp> camps;
    int turn = 0;
    vector<Minion> minions;
    vector<Hero> heroes;
    vector<string> events;
};

Game* G = nullptr;
inline bool wall(int x, int y) { return x < 0 || y < 0 || x >= G->S || y >= G->S || G->mapRows[y][x] == '#'; }
inline int manh(int ax, int ay, int bx, int by) { return abs(ax - bx) + abs(ay - by); }

bool read_block(Game& g) {
    string w;
    int k;
    while (cin >> w) {
        if (w == "INIT") {
            cin >> g.teams >> g.me >> g.S >> g.turns >> g.diff >> g.waveSize >> g.waveGap >> g.minionHp
                >> g.minionAtk >> g.towerRange >> g.respawn >> g.regen >> g.fountain >> g.minionCap
                >> g.aggro >> g.npcHeroes;
            cin >> w;
            g.mapRows.assign(g.S, "");
            for (auto& row : g.mapRows) cin >> row;
            cin >> w >> k;
            g.cls.assign(k, {});
            for (auto& c : g.cls) { string tag; int idx; cin >> tag >> idx >> c.hp >> c.atk >> c.rng >> c.cd
                                         >> c.pos >> c.cls >> c.skill; }
            cin >> w >> k; g.levelXp.resize(k);
            for (auto& v : g.levelXp) cin >> v;
            cin >> w >> k; g.spawns.resize(k);
            for (auto& s : g.spawns) { string tag; cin >> tag >> s.first >> s.second; }
            cin >> w >> k; g.bases.resize(k);
            for (auto& b : g.bases) { string tag; cin >> tag >> b[0] >> b[1] >> b[2] >> b[3]; }
            cin >> w >> k; g.towers.resize(k);
            for (auto& t : g.towers) { string tag; cin >> tag >> t.id >> t.team >> t.lane >> t.tier >> t.x >> t.y >> t.maxhp >> t.atk; }
            cin >> w >> k; g.nexuses.resize(k);
            for (auto& n : g.nexuses) { string tag; cin >> tag >> n.id >> n.team >> n.x >> n.y >> n.maxhp; }
            cin >> w >> k; g.camps.resize(k);
            for (auto& c : g.camps) cin >> w >> c.id >> c.x >> c.y >> c.maxhp >> c.atk >> w;
            continue;
        }
        if (w != "TURN") return false;
        cin >> g.turn;
        cin >> w;
        for (auto& n : g.nexuses) cin >> n.hp;
        cin >> w >> k;
        for (int i = 0; i < k; i++) { int id, hp; string tag; cin >> tag >> id >> hp;
            for (auto& t : g.towers) if (t.id == id) t.hp = hp; }
        cin >> w >> k;
        for (int i = 0; i < k; i++) { int id, hp, resp; string tag; cin >> tag >> id >> hp >> resp;
            for (auto& c : g.camps) if (c.id == id) { c.hp = hp; c.resp = resp; } }
        cin >> w >> k; g.minions.resize(k);
        for (auto& m : g.minions) { string tag; cin >> tag >> m.id >> m.team >> m.lane >> m.x >> m.y >> m.hp; }
        cin >> w >> k; g.heroes.resize(k);
        for (auto& h : g.heroes) { string tag; cin >> tag >> h.id >> h.team >> h.x >> h.y >> h.hp >> h.maxhp
                                         >> h.level >> h.xp >> h.cd >> h.respawn; }
        cin >> w >> k; getline(cin, w);
        g.events.assign(k, "");
        for (auto& e : g.events) getline(cin, e);
        cin >> w;
        return true;
    }
    return false;
}

const int DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};
const char* MV[4] = {"U", "D", "L", "R"};

bool canHit(int ax, int ay, int bx, int by, int rng) {
    int d = manh(ax, ay, bx, by);
    if (d == 0 || d > rng) return false;
    if (d == 1) return true;
    int dx = bx - ax, dy = by - ay;
    if (dx == 0) return !wall(ax, ay + dy / 2);
    if (dy == 0) return !wall(ax + dx / 2, ay);
    return !wall(ax + dx, ay) || !wall(ax, ay + dy);
}

bool lineClear(int ax, int ay, int bx, int by) {
    if (ax == bx) { for (int y = min(ay, by) + 1; y < max(ay, by); y++) if (wall(ax, y)) return false; return true; }
    if (ay == by) { for (int x = min(ax, bx) + 1; x < max(ax, bx); x++) if (wall(x, ay)) return false; return true; }
    return false;
}

vector<int> bfs(vector<pair<int, int>> src, const set<pair<int, int>>& blocked) {
    int S = G->S;
    vector<int> d(S * S, -1);
    deque<pair<int, int>> q;
    for (auto [x, y] : src) {
        if (x < 0 || y < 0 || x >= S || y >= S || d[y * S + x] >= 0) continue;
        d[y * S + x] = 0;
        q.push_back({x, y});
    }
    while (!q.empty()) {
        auto [x, y] = q.front(); q.pop_front();
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (wall(nx, ny) || blocked.count({nx, ny}) || d[ny * S + nx] >= 0) continue;
            d[ny * S + nx] = d[y * S + x] + 1;
            q.push_back({nx, ny});
        }
    }
    return d;
}

pair<int, int> applyMove(pair<int, int> p, const string& mv) {
    if (mv == "U") return {p.first, p.second - 1};
    if (mv == "D") return {p.first, p.second + 1};
    if (mv == "L") return {p.first - 1, p.second};
    if (mv == "R") return {p.first + 1, p.second};
    return p;
}

bool occupied(const pair<int, int>& p, const set<pair<int, int>>& unitBusy, const set<pair<int, int>>& claimed) {
    return unitBusy.count(p) > 0 || claimed.count(p) > 0;
}

string stepToward(int x, int y, const vector<int>& field,
                  const set<pair<int, int>>& unitBusy, const set<pair<int, int>>& claimed) {
    int S = G->S, cur = field[y * S + x], best = cur, bi = -1;
    if (best < 0) best = INT_MAX;
    for (int k = 0; k < 4; k++) {
        int nx = x + DX[k], ny = y + DY[k];
        if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
        int v = field[ny * S + nx];
        if (v < 0 || occupied({nx, ny}, unitBusy, claimed)) continue;
        if (v < best) { best = v; bi = k; }
    }
    if (bi < 0) {
        int worst = INT_MAX;
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
            int v = field[ny * S + nx];
            if (v < 0 || occupied({nx, ny}, unitBusy, claimed)) continue;
            if (v < worst) { worst = v; bi = k; }
        }
    }
    return bi >= 0 ? string(MV[bi]) : string("S");
}

struct Cmd { string mv = "S", act = "-"; int tgt = -1; };

void decide(Game& g, vector<Cmd>& out) {
    G = &g;
    int me = g.me, foe = 1 - me;
    set<pair<int, int>> busy, statics;
    for (auto& h : g.heroes) if (h.x >= 0) busy.insert({h.x, h.y});
    for (auto& m : g.minions) busy.insert({m.x, m.y});
    for (auto& t : g.towers) if (t.hp > 0) { busy.insert({t.x, t.y}); statics.insert({t.x, t.y}); }
    for (auto& c : g.camps) if (c.hp > 0) { busy.insert({c.x, c.y}); statics.insert({c.x, c.y}); }
    for (auto& n : g.nexuses) { busy.insert({n.x, n.y}); statics.insert({n.x, n.y}); }

    vector<pair<int, int>> homeSp(g.spawns.begin() + (me == 0 ? 0 : 5), g.spawns.begin() + (me == 0 ? 5 : 10));
    auto homeField = bfs(homeSp, statics);
    Nexus& crystal = g.nexuses[foe];
    Nexus& myCrystal = g.nexuses[me];
    const int MID = 2;

    // ---- 全队共享的情报（先于个人决策算好） ----
    // 集结点：己方中路最靠前的小兵；没兵就缩到本路最靠前的活塔后；塔全没了就守水晶
    pair<int, int> rally;
    {
        int bestD = INT_MAX; bool found = false;
        for (auto& m : g.minions) if (m.team == me && m.lane == MID) {
            int d = manh(m.x, m.y, crystal.x, crystal.y);
            if (d < bestD) { bestD = d; rally = {m.x, m.y}; found = true; }
        }
        if (!found) {
            bestD = INT_MAX;
            for (auto& t : g.towers) if (t.team == me && t.hp > 0 && t.lane == MID) {
                int d = manh(t.x, t.y, crystal.x, crystal.y);
                if (d < bestD) { bestD = d; rally = {t.x, t.y}; found = true; }
            }
        }
        if (!found) rally = {myCrystal.x, myCrystal.y};
    }
    // 家门告急：敌方单位贴近我方水晶 → 全队回防，目标 = 最靠近水晶的那个威胁（迎击，而不是缩家）
    bool homeDanger = false;
    pair<int, int> threatPos{-99, -99};
    {
        int bestD = INT_MAX;
        for (auto& h : g.heroes) if (h.team == foe && h.x >= 0) {
            int d = manh(h.x, h.y, myCrystal.x, myCrystal.y);
            if (d <= 7 && d < bestD) { bestD = d; threatPos = {h.x, h.y}; homeDanger = true; }
        }
        for (auto& m : g.minions) if (m.team == foe) {
            int d = manh(m.x, m.y, myCrystal.x, myCrystal.y);
            if (d <= 4 && d < bestD) { bestD = d; threatPos = {m.x, m.y}; homeDanger = true; }
        }
    }

    // 全队集火目标（一个池子里比优先级）：
    //   残血敌方英雄（0~1）> 有小兵扛着的塔/水晶（0.95）> 敌兵（1~2）
    // 围塔时只要小兵扛着就集火干塔，英雄快死了才转火收头
    int teamTgt = -1;
    {
        double bestP = 1e18;
        for (auto& o : g.heroes) if (o.team == foe && o.x >= 0) {
            int inReach = 0;
            for (auto& a : g.heroes) if (a.team == me && a.x >= 0 && canHit(a.x, a.y, o.x, o.y, 4)) inReach++;
            if (inReach >= 2) {
                double p = (double)o.hp / o.maxhp + o.id * 1e-6;
                if (p < bestP) { bestP = p; teamTgt = o.id; }
            }
        }
        for (auto& m : g.minions) if (m.team == foe) {
            int inReach = 0;
            for (auto& a : g.heroes) if (a.team == me && a.x >= 0 && canHit(a.x, a.y, m.x, m.y, 4)) inReach++;
            if (inReach >= 2) {
                double p = 1 + (double)m.hp / g.minionHp + m.id * 1e-6;
                if (p < bestP) { bestP = p; teamTgt = m.id; }
            }
        }
        auto tryStructure = [&](pair<int, int> sp, int sid) {
            bool cover = false;
            for (auto& m : g.minions) if (m.team == me && manh(m.x, m.y, sp.first, sp.second) <= 2) cover = true;
            bool safe = true;                     // 附近没有敌方英雄也敢强拆（五人抱团扛得住塔）
            for (auto& o : g.heroes) if (o.team == foe && o.x >= 0 && manh(o.x, o.y, sp.first, sp.second) <= 6) safe = false;
            if (!cover && !safe) return;
            int inReach = 0;
            for (auto& a : g.heroes) if (a.team == me && a.x >= 0 && canHit(a.x, a.y, sp.first, sp.second, 4)) inReach++;
            if (inReach < 2) return;
            if (0.95 < bestP) { bestP = 0.95; teamTgt = sid; }
        };
        for (auto& t : g.towers) if (t.team == foe && t.hp > 0 && t.lane == MID) tryStructure({t.x, t.y}, t.id);
        tryStructure({crystal.x, crystal.y}, crystal.id);
    }
    // 队首：离敌方水晶最近的我方存活英雄
    const Hero* leader = nullptr; int leadD = INT_MAX;
    for (auto& h : g.heroes) if (h.team == me && h.x >= 0) {
        int d = manh(h.x, h.y, crystal.x, crystal.y);
        if (d < leadD) { leadD = d; leader = &h; }
    }

    out.assign(5, Cmd{});
    set<pair<int, int>> claimed;
    for (auto& h : g.heroes) {
        if (h.team != me || h.x < 0) continue;
        int k = h.id - 5 * me, x = h.x, y = h.y;
        const ClassInfo& ci = g.cls[k];
        vector<Hero*> foesH;
        for (auto& o : g.heroes) if (o.team == foe && o.x >= 0) foesH.push_back(&o);
        Hero* eh = nullptr; int ehD = INT_MAX;
        for (auto* o : foesH) { int d = manh(x, y, o->x, o->y); if (d < ehD) { ehD = d; eh = o; } }

        Cmd cmd{"S", "-", -1};
        bool inBase = x >= g.bases[me][0] && x <= g.bases[me][2] && y >= g.bases[me][1] && y <= g.bases[me][3];
        if (inBase && h.hp < 0.92 * h.maxhp) { out[k] = cmd; continue; }
        if (h.hp < 0.35 * h.maxhp && ((eh && ehD <= 4) || h.hp < 0.2 * h.maxhp)) {
            cmd.mv = stepToward(x, y, homeField, busy, claimed);
            claimed.insert(applyMove({x, y}, cmd.mv));
            out[k] = cmd;
            continue;
        }
        // ---- 动作：优先全队集火目标；围塔时塔就是最高优先级；打不到再自保/放技能 ----
        if (teamTgt >= 0) {
            // 找到集火目标的位置
            pair<int, int> tp{-99, -99};
            for (auto* o : foesH) if (o->id == teamTgt) tp = {o->x, o->y};
            for (auto& m : g.minions) if (m.id == teamTgt) tp = {m.x, m.y};
            for (auto& t : g.towers) if (t.id == teamTgt) tp = {t.x, t.y};
            if (tp.first > -50 && canHit(x, y, tp.first, tp.second, ci.rng)) cmd.act = to_string(teamTgt);
        }
        if (cmd.act == "-") {
            if (ci.skill == "回春术" && h.hp < 0.65 * h.maxhp && h.cd == 0) cmd.act = "Q";
            else if (ci.skill == "火球" && h.cd == 0) {
                int bt = -1, bh = INT_MAX;
                for (auto* o : foesH) if (manh(x, y, o->x, o->y) <= 5 && lineClear(x, y, o->x, o->y) && o->hp < bh) {
                    bh = o->hp; bt = o->id;
                }
                if (bt >= 0) { cmd.act = "Q"; cmd.tgt = bt; }
            } else if (ci.skill == "穿云箭" && h.cd == 0) {
                int myAtk = ci.atk + 2 * (h.level - 1);
                for (auto* o : foesH) if (manh(x, y, o->x, o->y) <= 4 && lineClear(x, y, o->x, o->y) && o->hp <= 1.2 * myAtk) {
                    cmd.act = "Q"; cmd.tgt = o->id; break;
                }
            } else if (ci.skill == "治疗术" && h.cd == 0) {
                int bt = -1; double br = 1.0;
                for (auto& o : g.heroes) if (o.team == me && o.x >= 0 && manh(x, y, o.x, o.y) <= 2) {
                    double r = (double)o.hp / o.maxhp;
                    if (r < 0.75 && r < br) { br = r; bt = o.id; }
                }
                if (bt >= 0) cmd.act = "Q";
            }
        }
        // ---- 移动：回防迎击 / 跟随集结点并保持队形 ----
        pair<int, int> goal = homeDanger ? threatPos : rally;
        if (!homeDanger && leader && leader->id != h.id) {
            // 队形约束：离队首超过 4 格就先向队首靠拢
            if (manh(x, y, leader->x, leader->y) > 4) goal = {leader->x, leader->y};
        }
        // 集火的是建筑但自己够不到时，直接向建筑靠拢
        if (!homeDanger && teamTgt >= 0) {
            pair<int, int> tp{-99, -99};
            for (auto& t : g.towers) if (t.id == teamTgt) tp = {t.x, t.y};
            for (auto& n : g.nexuses) if (n.id == teamTgt) tp = {n.x, n.y};
            if (tp.first > -50) goal = tp;
        }
        auto goalField = bfs({goal}, statics);
        cmd.mv = stepToward(x, y, goalField, busy, claimed);
        if (ci.skill == "突袭" && h.cd == 0 && cmd.mv != "S" && (!eh || ehD > 2)) cmd.act = "Q";   // 突袭赶路
        claimed.insert(applyMove({x, y}, cmd.mv));
        out[k] = cmd;
    }
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    Game g;
    G = &g;
    while (read_block(g)) {
        vector<Cmd> out;
        decide(g, out);
        cout << g.turn;
        for (auto& c : out) {
            cout << ' ' << c.mv << ' ' << c.act;
            if (c.act == "Q" && c.tgt >= 0) cout << ' ' << c.tgt;
        }
        cout << endl;
    }
}
