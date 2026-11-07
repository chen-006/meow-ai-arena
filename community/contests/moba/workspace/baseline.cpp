// 练习基准（中等）：常规思路，不做对手建模和多步模拟。与引擎内置 NPC 队同一套思路，
// 但有正经的分路：战士上路、刺客打野、法师中路、射手和辅助走下路。
//  - 推进：沿 BFS 最短路走向本路下一座敌方塔（都没了就走向水晶），血量健康才打塔。
//  - 打架：普攻优先残血英雄，其次小兵，再次野怪和建筑；残血就往家撤，泉水回满再出来。
//  - 技能：回春自疗、突袭赶路、火球/穿云箭直线消耗、治疗救队友。
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
            continue;                        // INIT 之后同一块里还有 TURN，继续读
        }
        if (w != "TURN") return false;       // 块首一定是 TURN
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

const int DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};      // N S W E，与引擎同序
const char* MV[4] = {"U", "D", "L", "R"};

// 射程 ≤2 的普攻判定（与引擎一致：隔一格直线看中间，斜对角看拐角）
bool canHit(int ax, int ay, int bx, int by, int rng) {
    int d = manh(ax, ay, bx, by);
    if (d == 0 || d > rng) return false;
    if (d == 1) return true;
    int dx = bx - ax, dy = by - ay;
    if (dx == 0) return !wall(ax, ay + dy / 2);
    if (dy == 0) return !wall(ax + dx / 2, ay);
    return !wall(ax + dx, ay) || !wall(ax, ay + dy);
}

// 直线是否被墙挡断（不含端点）
bool lineClear(int ax, int ay, int bx, int by) {
    if (ax == bx) { for (int y = min(ay, by) + 1; y < max(ay, by); y++) if (wall(ax, y)) return false; return true; }
    if (ay == by) { for (int x = min(ax, bx) + 1; x < max(ax, bx); x++) if (wall(x, ay)) return false; return true; }
    return false;
}

// 从 src 出发的 BFS 距离场，绕开 walls 和 blocked
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

// 判断某格是否被占用：unitBusy = 所有单位现在站的格子，claimed = 本回合更早的英雄已选定的目的地
bool blocked(const pair<int, int>& p, const set<pair<int, int>>& unitBusy, const set<pair<int, int>>& claimed) {
    return unitBusy.count(p) > 0 || claimed.count(p) > 0;
}

string stepToward(int x, int y, const vector<int>& field,
                  const set<pair<int, int>>& unitBusy, const set<pair<int, int>>& claimed) {
    int S = G->S, cur = field[y * S + x], best = cur, bi = -1;
    if (best < 0) best = INT_MAX;
    for (int k = 0; k < 4; k++) {                    // 先找能缩短距离的
        int nx = x + DX[k], ny = y + DY[k];
        if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
        int v = field[ny * S + nx];
        if (v < 0 || blocked({nx, ny}, unitBusy, claimed)) continue;
        if (v < best) { best = v; bi = k; }
    }
    if (bi < 0) {                                    // 走不动就侧移绕开堵路的单位，别卡死
        int worst = INT_MAX;
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
            int v = field[ny * S + nx];
            if (v < 0 || blocked({nx, ny}, unitBusy, claimed)) continue;
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

// 每个英雄一个决定：移动、动作（"-" / "Q" / 普攻目标）、技能目标（-1 无）
struct Cmd { string mv, act; int tgt = -1; };

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
    vector<int> laneOf = {0, 1, 2, 2, 2};    // 战士 A、刺客 B、法师/射手/辅助 C（刺客见打野逻辑）
    set<pair<int, int>> claimed;             // 本回合更早的英雄已选定的目的地，后面的人绕开

    out.assign(5, {"S", "-", -1});
    for (auto& h : g.heroes) {
        if (h.team != me || h.x < 0) continue;
        int k = h.id - 5 * me, x = h.x, y = h.y;
        const ClassInfo& ci = g.cls[k];
        vector<Hero*> foesH;
        for (auto& o : g.heroes) if (o.team == foe && o.x >= 0) foesH.push_back(&o);
        Hero* eh = nullptr; int ehD = INT_MAX;
        for (auto* o : foesH) { int d = manh(x, y, o->x, o->y); if (d < ehD) { ehD = d; eh = o; } }

        Cmd cmd{"S", "-", -1};
        // 泉水没回满就别出门
        bool inBase = x >= g.bases[me][0] && x <= g.bases[me][2] && y >= g.bases[me][1] && y <= g.bases[me][3];
        bool underTower = false;
        for (auto& t : g.towers) if (t.team == foe && t.hp > 0 && manh(x, y, t.x, t.y) <= 2) underTower = true;
        if (inBase && h.hp < 0.9 * h.maxhp) {
            out[k] = cmd;
            continue;
        }
        if ((h.hp < 0.55 * h.maxhp && (underTower || (eh && ehD <= 5))) ||
            (h.hp < 0.4 * h.maxhp && eh && ehD <= 6)) {
            cmd.mv = stepToward(x, y, homeField, busy, claimed);      // 残血/被塔打就回家
            claimed.insert(applyMove({x, y}, cmd.mv));
            out[k] = cmd;
            continue;
        }
        // ---- 选普攻目标：英雄 > 小兵 > 野怪/建筑，各自先挑血量比例最低的 ----
        int tgt = -1; double tp = 1e18;
        auto consider = [&](int id, int ux, int uy, int uh, int umax, int pri) {
            if (!canHit(x, y, ux, uy, ci.rng)) return;
            double p = pri + (umax > 0 ? (double)uh / umax : 0) + id * 1e-6;
            if (p < tp) { tp = p; tgt = id; }
        };
        for (auto* o : foesH) consider(o->id, o->x, o->y, o->hp, o->maxhp, 0);
        for (auto& m : g.minions) if (m.team == foe) consider(m.id, m.x, m.y, m.hp, g.minionHp, 1);
        for (auto& c : g.camps) if (c.hp > 0) consider(c.id, c.x, c.y, c.hp, c.maxhp, 2);
        for (auto& t : g.towers) if (t.team == foe && t.hp > 0) consider(t.id, t.x, t.y, t.hp, t.maxhp, 2);
        consider(crystal.id, crystal.x, crystal.y, crystal.hp, crystal.maxhp, 2);

        if (tgt >= 0) {
            bool structure = tgt == crystal.id;
            pair<int, int> tpos = {crystal.x, crystal.y};
            for (auto& t : g.towers) if (t.id == tgt && t.team == foe) { structure = true; tpos = {t.x, t.y}; }
            if (structure) {
                bool cover = false;                       // 有小兵在旁边扛塔才拆
                for (auto& m : g.minions) if (m.team == me && manh(m.x, m.y, tpos.first, tpos.second) <= 2) cover = true;
                if (h.hp < 0.45 * h.maxhp && (!eh || ehD >= 4)) {
                    cmd.mv = stepAway(x, y, tpos);        // 血量不健康就撤
                } else if (!cover && h.hp < 0.75 * h.maxhp && manh(x, y, tpos.first, tpos.second) <= 3) {
                    // 没兵线扛塔就站在塔外等下一波
                } else {
                    cmd.act = to_string(tgt);
                }
            } else {
                cmd.act = to_string(tgt);
            }
            out[k] = cmd;
            continue;
        }
        // ---- 技能 ----
        if (ci.skill == "回春术" && h.hp < 0.65 * h.maxhp && h.cd == 0) {
            cmd.act = "Q";
        } else if (ci.skill == "火球" && h.cd == 0) {
            int bt = -1, bh = INT_MAX;
            for (auto* o : foesH) {
                int d = manh(x, y, o->x, o->y);
                if (d <= 5 && lineClear(x, y, o->x, o->y) && o->hp < bh) { bh = o->hp; bt = o->id; }
            }
            if (bt >= 0) { cmd.act = "Q"; cmd.tgt = bt; }
        } else if (ci.skill == "穿云箭" && h.cd == 0) {
            for (auto* o : foesH) {
                int d = manh(x, y, o->x, o->y);
                if (d <= 4 && lineClear(x, y, o->x, o->y)) { cmd.act = "Q"; cmd.tgt = o->id; break; }
            }
        } else if (ci.skill == "治疗术" && h.cd == 0) {
            int bt = -1; double br = 1.0;
            for (auto& o : g.heroes) if (o.team == me && o.x >= 0 && manh(x, y, o.x, o.y) <= 2) {
                double r = (double)o.hp / o.maxhp;
                if (r < 0.7 && r < br) { br = r; bt = o.id; }
            }
            if (bt >= 0) cmd.act = "Q";
        } else if (ci.skill == "突袭" && h.cd == 0 && (!eh || ehD > 2)) {
            cmd.act = "Q";                                    // 突袭让本回合移动翻倍
        }
        // ---- 移动 ----
        pair<int, int> goal = {crystal.x, crystal.y};
        if (ci.skill == "突袭") {                             // 刺客：附近有野怪先打野
            int bestD = INT_MAX;
            for (auto& c : g.camps) if (c.hp > 0) {
                int d = manh(x, y, c.x, c.y);
                if (d < bestD) { bestD = d; goal = {c.x, c.y}; }
            }
        } else {
            int bestD = INT_MAX;
            for (auto& t : g.towers) if (t.team == foe && t.hp > 0 && t.lane == laneOf[k]) {
                int d = manh(x, y, t.x, t.y);
                if (d < bestD) { bestD = d; goal = {t.x, t.y}; }
            }
        }
        auto goalField = bfs({goal}, statics);
        cmd.mv = stepToward(x, y, goalField, busy, claimed);
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
