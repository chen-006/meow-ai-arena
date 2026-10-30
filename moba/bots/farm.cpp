// 示例 bot · 稳健发育：经验优先，能不打就不打。
//  - 分路补兵吃经验（站在小兵死亡点 2 格内），打野刺客按 45 回合重生表反复刷野。
//  - 只打"必赢"的架：目标这一下会被打死，或 2 个以上队友同时在射程内。
//  - 血量 <60% 且附近有敌人就回家，泉水回满再出门；没有等级/兵线优势绝不过塔。
//  - 平均等级领先 2 级或敌方全员阵亡时，才转推塔。
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

int avgLevel(const Game& g, int team) {
    int sum = 0, n = 0;
    for (auto& h : g.heroes) if (h.team == team) { sum += h.level; n++; }
    return n ? sum / n : 0;
}

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
    vector<int> laneOf = {0, 1, 2, 1, 1};            // 战士 A、刺客打野、法师 C、射手/辅助 B
    bool feeding = avgLevel(g, me) + 2 <= avgLevel(g, foe);   // 还没发育起来
    bool foesDead = true;
    for (auto& h : g.heroes) if (h.team == foe && h.x >= 0) foesDead = false;
    bool pushMode = !feeding || foesDead;

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
        if (inBase && h.hp < 0.92 * h.maxhp) { out[k] = cmd; continue; }        // 泉水回满再出门
        if (h.hp < 0.6 * h.maxhp && ((eh && ehD <= 5) || h.hp < 0.35 * h.maxhp)) {
            cmd.mv = stepToward(x, y, homeField, busy, claimed);
            claimed.insert(applyMove({x, y}, cmd.mv));
            out[k] = cmd;
            continue;
        }
        // ---- 可攻击目标 ----
        struct Cand { int id, hp, maxhp, kind, d; };
        vector<Cand> rng;
        auto add = [&](int id, int ux, int uy, int uh, int umax, int kind) {
            if (canHit(x, y, ux, uy, ci.rng)) rng.push_back({id, uh, umax, kind, manh(x, y, ux, uy)});
        };
        for (auto* o : foesH) add(o->id, o->x, o->y, o->hp, o->maxhp, 0);
        for (auto& m : g.minions) if (m.team == foe) add(m.id, m.x, m.y, m.hp, g.minionHp, 1);
        for (auto& c : g.camps) if (c.hp > 0) add(c.id, c.x, c.y, c.hp, c.maxhp, 2);
        // 建筑：有己方小兵在塔边扛伤害、或自己血量很健康时才纳入目标；
        // 有掩体的塔优先级仅次于英雄——发育起来就该赶紧换塔
        for (auto& t : g.towers) if (t.team == foe && t.hp > 0 && t.lane == laneOf[k]) {
            bool cover = false;
            for (auto& m : g.minions) if (m.team == me && manh(m.x, m.y, t.x, t.y) <= 2) cover = true;
            if (cover || h.hp > 0.75 * h.maxhp) add(t.id, t.x, t.y, t.hp, t.maxhp, cover ? 1 : 3);
        }
        if (pushMode) {
            bool cover = false;
            for (auto& m : g.minions) if (m.team == me && manh(m.x, m.y, crystal.x, crystal.y) <= 2) cover = true;
            if (cover || h.hp > 0.75 * h.maxhp) add(crystal.id, crystal.x, crystal.y, crystal.hp, crystal.maxhp, cover ? 1 : 3);
        }

        // ---- 打架规则：必赢才打英雄 ----
        int tgt = -1; double tp = 1e18;
        for (auto& c : rng) {
            if (c.kind == 0) {
                int helpers = 0;
                for (auto& a : g.heroes) if (a.team == me && a.x >= 0 && a.id != h.id && canHit(a.x, a.y, x, y, 4)) helpers++;
                bool sureKill = c.hp <= ci.atk * 3 / 2;
                bool gang = helpers >= 2;
                if (!sureKill && !gang) continue;
            }
            double p = c.kind + (c.maxhp ? (double)c.hp / c.maxhp : 0) + c.id * 1e-6;
            if (p < tp) { tp = p; tgt = c.id; }
        }
        // ---- 技能 ----
        if (tgt < 0 || tp > 3) {
            if (ci.skill == "回春术" && h.hp < 0.7 * h.maxhp && h.cd == 0) cmd.act = "Q";
            else if (ci.skill == "火球" && h.cd == 0) {
                // 优先补刀残血英雄，其次打野怪练级
                int bt = -1; double bp = 1e18;
                int dmg = 45 + 6 * h.level;
                for (auto* o : foesH) {
                    if (manh(x, y, o->x, o->y) <= 5 && lineClear(x, y, o->x, o->y) && o->hp <= dmg) {
                        if (o->hp < bp) { bp = o->hp; bt = o->id; }
                    }
                }
                if (bt < 0) for (auto& c : g.camps) if (c.hp > 0) {
                    if (manh(x, y, c.x, c.y) <= 5 && lineClear(x, y, c.x, c.y)) { bt = c.id; break; }
                }
                if (bt >= 0) { cmd.act = "Q"; cmd.tgt = bt; }
            } else if (ci.skill == "穿云箭" && h.cd == 0) {
                for (auto& c : g.camps) if (c.hp > 0 && manh(x, y, c.x, c.y) <= 4 && lineClear(x, y, c.x, c.y)) {
                    cmd.act = "Q"; cmd.tgt = c.id; break;
                }
            } else if (ci.skill == "治疗术" && h.cd == 0) {
                int bt = -1; double br = 1.0;
                for (auto& o : g.heroes) if (o.team == me && o.x >= 0 && manh(x, y, o.x, o.y) <= 2) {
                    double r = (double)o.hp / o.maxhp;
                    if (r < 0.75 && r < br) { br = r; bt = o.id; }
                }
                if (bt >= 0) cmd.act = "Q";
            } else if (ci.skill == "突袭" && h.cd == 0 && (!eh || ehD > 2)) {
                cmd.act = "Q";
            }
        }
        if (tgt >= 0) cmd.act = to_string(tgt);
        // ---- 移动目标 ----
        pair<int, int> goal;
        if (ci.skill == "突袭") {
            // 打野：最近活野怪；全死光了就守在最快重生的营地旁边（不占刷新格）
            goal = {crystal.x, crystal.y};
            int bestD = INT_MAX; Camp* bc = nullptr;
            for (auto& c : g.camps) if (c.hp > 0) {
                int d = manh(x, y, c.x, c.y);
                if (d < bestD) { bestD = d; bc = &c; }
            }
            if (bc) goal = {bc->x, bc->y};
            else {
                Camp* soon = nullptr;
                for (auto& c : g.camps) if (!soon || (c.resp >= 0 && c.resp < soon->resp)) soon = &c;
                if (soon) {
                    pair<int, int> p{soon->x, soon->y};
                    for (int kk = 0; kk < 4; kk++) {                       // 挑一个不挡刷新格的邻格
                        pair<int, int> q{p.first + DX[kk], p.second + DY[kk]};
                        if (!wall(q.first, q.second) && !statics.count(q)) { goal = q; break; }
                    }
                }
            }
        } else if (pushMode && tgt < 0) {
            goal = {crystal.x, crystal.y};
            int bestD = INT_MAX;
            for (auto& t : g.towers) if (t.team == foe && t.hp > 0 && t.lane == laneOf[k]) {
                int d = manh(x, y, t.x, t.y);
                if (d < bestD) { bestD = d; goal = {t.x, t.y}; }
            }
        } else {
            // 发育期：站在本路两军小兵交战线附近，从后方吃经验，别越塔
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
        // 发育期别站在活敌塔射程里：朝远离塔的方向挪一步
        if (!pushMode) {
            for (auto& t : g.towers) if (t.team == foe && t.hp > 0 && manh(x, y, t.x, t.y) <= 2) {
                for (int kk = 0; kk < 4; kk++) {
                    pair<int, int> q{x + DX[kk], y + DY[kk]};
                    if (!wall(q.first, q.second) && !statics.count(q) && !occupied(q, busy, claimed) &&
                        manh(q.first, q.second, t.x, t.y) > 2) {
                        cmd.mv = string(1, "UDLR"[kk]);
                        break;
                    }
                }
                break;
            }
        }
        if (cmd.mv == "S" && cmd.act == "-") {
            auto goalField = bfs({goal}, statics);
            cmd.mv = stepToward(x, y, goalField, busy, claimed);
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
