// 示例 bot · 游走抓人：人头经验 60 很肥，全队围绕击杀滚动雪球。
//  - 打野刺客：开局清野到 3 级，然后全图游走——挑"正被队友缠住或血量不满"的敌方英雄突袭进场。
//  - 线上四人：分路发育但主动换血，只要对面英雄血量比例低于自己（或局部人多）就集火反打。
//  - 全队最高优先级是普攻残血英雄；抓死人对面减员，顺势拆塔或清野。
//  - 残血（<40% 有威胁）立刻脱离回泉水，回满再来。
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

string stepAway(int x, int y, pair<int, int> threat) {
    int best = manh(x, y, threat.first, threat.second), bi = -1;
    for (int k = 0; k < 4; k++) {
        int nx = x + DX[k], ny = y + DY[k];
        if (wall(nx, ny)) continue;
        int d = manh(nx, ny, threat.first, threat.second);
        if (d > best) { best = d; bi = k; }
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
    vector<int> laneOf = {0, 1, 2, 1, 1};

    // 全队目标：全图最残的敌方英雄（优先挑身边有我方队友的，配合围剿）
    Hero* prey = nullptr; double bestScore = -1e18;
    for (auto& o : g.heroes) if (o.team == foe && o.x >= 0) {
        int nearAllies = 0;
        for (auto& a : g.heroes) if (a.team == me && a.x >= 0 && manh(a.x, a.y, o.x, o.y) <= 6) nearAllies++;
        double s = (1.0 - (double)o.hp / o.maxhp) * 100 + nearAllies * 40;
        if (s > bestScore) { bestScore = s; prey = &o; }
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
        if (h.hp < 0.4 * h.maxhp && ((eh && ehD <= 5) || h.hp < 0.25 * h.maxhp)) {
            cmd.mv = stepToward(x, y, homeField, busy, claimed);
            claimed.insert(applyMove({x, y}, cmd.mv));
            out[k] = cmd;
            continue;
        }
        // ---- 目标：人头第一 ----
        int tgt = -1; double tp = 1e18;
        auto consider = [&](int id, int ux, int uy, int uh, int umax, double kind) {
            if (!canHit(x, y, ux, uy, ci.rng)) return;
            double p = kind + (umax ? (double)uh / umax : 0) + id * 1e-6;
            if (p < tp) { tp = p; tgt = id; }
        };
        for (auto* o : foesH) consider(o->id, o->x, o->y, o->hp, o->maxhp, 0);
        for (auto& m : g.minions) if (m.team == foe) consider(m.id, m.x, m.y, m.hp, g.minionHp, 1);
        for (auto& c : g.camps) if (c.hp > 0) consider(c.id, c.x, c.y, c.hp, c.maxhp, 2);
        for (auto& t : g.towers) if (t.team == foe && t.hp > 0 && t.lane == laneOf[k]) {
            bool cover = false;
            for (auto& m : g.minions) if (m.team == me && manh(m.x, m.y, t.x, t.y) <= 2) cover = true;
            if (cover) consider(t.id, t.x, t.y, t.hp, t.maxhp, 1.5);      // 抓完人顺势拆塔
        }

        // ---- 技能 ----
        if (ci.skill == "回春术" && h.hp < 0.65 * h.maxhp && h.cd == 0) cmd.act = "Q";
        else if (ci.skill == "火球" && h.cd == 0) {
            int bt = -1, bh = INT_MAX;
            for (auto* o : foesH) {
                if (manh(x, y, o->x, o->y) <= 5 && lineClear(x, y, o->x, o->y) && o->hp < bh) { bh = o->hp; bt = o->id; }
            }
            if (bt >= 0) { cmd.act = "Q"; cmd.tgt = bt; }
        } else if (ci.skill == "穿云箭" && h.cd == 0) {
            // 优先射残血英雄（这一箭加上普攻能收掉的头）
            int myAtk = ci.atk + 2 * (h.level - 1);
            for (auto* o : foesH) {
                if (manh(x, y, o->x, o->y) <= 4 && lineClear(x, y, o->x, o->y) &&
                    o->hp <= 1.2 * myAtk) { cmd.act = "Q"; cmd.tgt = o->id; break; }
            }
        } else if (ci.skill == "治疗术" && h.cd == 0) {
            int bt = -1; double br = 1.0;
            for (auto& o : g.heroes) if (o.team == me && o.x >= 0 && manh(x, y, o.x, o.y) <= 2) {
                double r = (double)o.hp / o.maxhp;
                if (r < 0.7 && r < br) { br = r; bt = o.id; }
            }
            if (bt >= 0) cmd.act = "Q";
        }
        if (tgt >= 0) cmd.act = to_string(tgt);

        // ---- 移动 ----
        pair<int, int> goal;
        if (ci.skill == "突袭") {
            // 打野：先清野到 3 级，然后全图追猎物
            if (h.level >= 3 && prey) goal = {prey->x, prey->y};
            else {
                int bestD = INT_MAX; Camp* bc = nullptr;
                for (auto& c : g.camps) if (c.hp > 0) {
                    int d = manh(x, y, c.x, c.y);
                    if (d < bestD) { bestD = d; bc = &c; }
                }
                goal = bc ? pair<int, int>{bc->x, bc->y} : pair<int, int>{crystal.x, crystal.y};
            }
        } else if (tgt < 0) {
            // 线上：守着本路交战线；猎物在附近（≤10）就一起压上去围剿
            if (prey && manh(x, y, prey->x, prey->y) <= 10 && h.hp > 0.55 * h.maxhp) {
                goal = {prey->x, prey->y};
            } else {
                goal = {crystal.x, crystal.y};
                int bestD = INT_MAX;
                for (auto& m : g.minions) if (m.team == me && m.lane == laneOf[k]) {
                    int d = manh(m.x, m.y, crystal.x, crystal.y);
                    if (d < bestD) { bestD = d; goal = {m.x, m.y}; }
                }
                if (bestD == INT_MAX) {
                    bestD = INT_MAX;
                    for (auto& t : g.towers) if (t.team == me && t.hp > 0 && t.lane == laneOf[k]) {
                        int d = manh(t.x, t.y, crystal.x, crystal.y);
                        if (d < bestD) { bestD = d; goal = {t.x, t.y}; }
                    }
                }
            }
        }
        if (cmd.mv == "S" && cmd.act == "-") {
            auto goalField = bfs({goal}, statics);
            cmd.mv = stepToward(x, y, goalField, busy, claimed);
            if (ci.skill == "突袭" && h.cd == 0 && goal != pair<int, int>{x, y} &&
                (eh == nullptr || ehD > 2)) {
                cmd.act = "Q";                                            // 突袭赶路，双倍接近猎物
            }
        }
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
