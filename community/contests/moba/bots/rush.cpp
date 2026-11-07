// rush.cpp —— 峡谷对决 · 激进速推风格示例 bot
// 风格：五人抱团，选敌方塔最残血/防守最弱的一条路集体强拆，快速拆塔施压；
//  血量健康（≥75%）时顶着塔拆，残血（<52% 顶塔受火 / <32% 常态）才撤回泉水，
//  回满（≥95%）立刻归队；优先普攻威胁己方拆塔的敌方英雄，其次塔，再次小兵；
//  对面抱团守塔且战力悬殊（打不过）就转战另一条路；家水晶遇袭且不在冲塔竞速时全员回防。
// 技能：战士回春续塔、刺客突袭接近/撤离、法师火球消耗守塔英雄与贴身塔、
//  射手穿云箭贯穿守军（连线价值≥3 才放）、辅助治疗保排。
// 无视野区：不做野区运营，刺客也随团推塔。
#include <bits/stdc++.h>
using namespace std;

struct ClassInfo { int hp, atk, rng, cd; string pos, cls, skill; };
struct Tower { int id, team, lane, tier, x, y, maxhp, hp, atk; };
struct Nexus { int id, team, x, y, maxhp, hp; };
struct Camp { int id, x, y, maxhp, atk, hp, resp; };
struct Minion { int id, team, lane, x, y, hp; };
struct Hero { int id, team, x, y, hp, maxhp, level, xp, cd, respawn; };   // 阵亡时 x=y=-1

struct Game {
    // 开局信息（INIT 块）
    int teams = 0, me = 0, S = 0, turns = 0, diff = 0, waveSize = 0, waveGap = 0, minionHp = 0, minionAtk = 0,
        towerRange = 0, respawn = 0, regen = 0, fountain = 0, minionCap = 0, aggro = 0, npcHeroes = 0;
    vector<string> mapRows;              // mapRows[y][x]：'#' 墙，'.' 空地
    vector<ClassInfo> cls;               // 5 个位置的职业
    vector<int> levelXp;
    vector<pair<int, int>> spawns;       // 10 个出生点：0 号队 5 个，1 号队 5 个
    vector<array<int, 4>> bases;         // 各队基地矩形 x0 y0 x1 y1
    vector<Tower> towers;
    vector<Nexus> nexuses;
    vector<Camp> camps;
    // 每回合更新（TURN 块）
    int turn = 0;
    vector<Minion> minions;
    vector<Hero> heroes;
    vector<string> events;
};

bool read_block(Game& g) {
    string w;
    int k;
    while (cin >> w) {
        if (w == "INIT") {
            cin >> g.teams >> g.me >> g.S >> g.turns >> g.diff >> g.waveSize >> g.waveGap >> g.minionHp
                >> g.minionAtk >> g.towerRange >> g.respawn >> g.regen >> g.fountain >> g.minionCap
                >> g.aggro >> g.npcHeroes;
            cin >> w;                        // MAP
            g.mapRows.assign(g.S, "");
            for (auto& row : g.mapRows) cin >> row;
            cin >> w >> k;                   // CLASSES 5
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
        cin >> w;                            // CRYSTAL
        for (auto& n : g.nexuses) cin >> n.hp;
        cin >> w >> k;                       // TOWERS n
        for (int i = 0; i < k; i++) { int id, hp; string tag; cin >> tag >> id >> hp;
            for (auto& t : g.towers) if (t.id == id) t.hp = hp; }
        cin >> w >> k;                       // CAMPS n
        for (int i = 0; i < k; i++) { int id, hp, resp; string tag; cin >> tag >> id >> hp >> resp;
            for (auto& c : g.camps) if (c.id == id) { c.hp = hp; c.resp = resp; } }
        cin >> w >> k; g.minions.resize(k);
        for (auto& m : g.minions) { string tag; cin >> tag >> m.id >> m.team >> m.lane >> m.x >> m.y >> m.hp; }
        cin >> w >> k; g.heroes.resize(k);
        for (auto& h : g.heroes) { string tag; cin >> tag >> h.id >> h.team >> h.x >> h.y >> h.hp >> h.maxhp
                                         >> h.level >> h.xp >> h.cd >> h.respawn; }
        cin >> w >> k; getline(cin, w);      // EVENTS n
        g.events.resize(k);
        for (auto& e : g.events) getline(cin, e);
        cin >> w;                            // END
        return true;
    }
    return false;
}

// ============================ 策略 ============================

static Game* G = nullptr;
static int S_ = 0;
static const int DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};      // U D L R
static const char* MV[4] = {"U", "D", "L", "R"};
static vector<char> staticB, minB, heroB;    // 每回合重建：墙+活塔+活营地+水晶 / 小兵 / 英雄
static vector<char> allyB, foeB, laneCell;   // 己方英雄 / 敌方英雄 / 车道格（含基地）
static set<pair<int, int>> vacate;           // 本回合队友正在离开的格子（可以跟进）

// 整局持久状态
static int curLane = -1;                     // 当前抱团推进的路
static int lanePen[3] = {0, 0, 0};           // 刚被打跑的路拉黑一段时间
static bool goHome[5] = {false, false, false, false, false};
static int pushId = -1;                      // 本回合推进目标（敌塔或水晶 id）

inline bool inMap(int x, int y) { return x >= 0 && y >= 0 && x < S_ && y < S_; }
inline bool isWall(int x, int y) { return !inMap(x, y) || G->mapRows[y][x] == '#'; }
inline int manh(int ax, int ay, int bx, int by) { return abs(ax - bx) + abs(ay - by); }
inline int atkOf(const ClassInfo& c, int lvl) { return c.atk + 2 * (lvl - 1); }
inline int dirOf(const string& mv) { for (int i = 0; i < 4; i++) if (mv == MV[i]) return i; return -1; }

// 射程 ≤2 的普攻判定（与引擎一致：隔一格直线看中间，斜对角看拐角）
bool canHit(int ax, int ay, int bx, int by, int rng) {
    int d = manh(ax, ay, bx, by);
    if (d == 0 || d > rng) return false;
    if (d == 1) return true;
    int dx = bx - ax, dy = by - ay;
    if (dx == 0) return !isWall(ax, ay + dy / 2);
    if (dy == 0) return !isWall(ax + dx / 2, ay);
    return !isWall(ax + dx, ay) || !isWall(ax, ay + dy);
}

// 直线（同行或同列）是否被墙挡断（不含端点）
bool lineClear(int ax, int ay, int bx, int by) {
    if (ax == bx) { for (int y = min(ay, by) + 1; y < max(ay, by); y++) if (isWall(ax, y)) return false; return true; }
    if (ay == by) { for (int x = min(ax, bx) + 1; x < max(ax, bx); x++) if (isWall(x, ay)) return false; return true; }
    return false;
}

// 从 src 出发的车道加权 Dijkstra 距离场（绕开 staticB；源格即使被堵也作为 0）。
// 车道格代价 1、野区代价 6：抱团沿大路推进，不穿野区抄近路。
vector<int> bfsField(const vector<pair<int, int>>& src) {
    vector<int> d(S_ * S_, INT_MAX);
    priority_queue<pair<int, int>, vector<pair<int, int>>, greater<>> pq;
    for (auto [x, y] : src) if (inMap(x, y) && d[y * S_ + x] > 0) { d[y * S_ + x] = 0; pq.push({0, y * S_ + x}); }
    while (!pq.empty()) {
        auto [dv, i] = pq.top(); pq.pop();
        if (dv > d[i]) continue;
        int x = i % S_, y = i / S_;
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (!inMap(nx, ny) || staticB[ny * S_ + nx]) continue;
            int nd = dv + (laneCell[ny * S_ + nx] ? 1 : 6);
            if (nd < d[ny * S_ + nx]) { d[ny * S_ + nx] = nd; pq.push({nd, ny * S_ + nx}); }
        }
    }
    return d;
}

// 该格是否不可进入：墙/塔/营地/水晶/小兵/敌方英雄；己方英雄只有在本回合离开时可以跟进
inline bool cellBlocked(int x, int y) {
    if (!inMap(x, y) || staticB[y * S_ + x] || minB[y * S_ + x]) return true;
    if (foeB[y * S_ + x]) return true;
    if (allyB[y * S_ + x] && !vacate.count({x, y})) return true;
    return false;
}

// 走向场值更小的邻格；走不动就侧移到场值最小的空格，别卡死
string stepToward(int x, int y, const vector<int>& f, const set<pair<int, int>>& claimed) {
    int cur = f[y * S_ + x];
    int best = cur < 0 || cur == INT_MAX ? INT_MAX : cur, bi = -1;
    for (int k = 0; k < 4; k++) {
        int nx = x + DX[k], ny = y + DY[k];
        if (cellBlocked(nx, ny) || claimed.count({nx, ny})) continue;
        int v = f[ny * S_ + nx];
        if (v < 0 || v == INT_MAX) continue;
        if (v < best) { best = v; bi = k; }
    }
    if (bi < 0) {
        int alt = INT_MAX;
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (cellBlocked(nx, ny) || claimed.count({nx, ny})) continue;
            int v = f[ny * S_ + nx];
            if (v < 0 || v == INT_MAX) continue;
            if (v < alt) { alt = v; bi = k; }
        }
    }
    return bi >= 0 ? string(MV[bi]) : string("S");
}

// 远离威胁点
string stepAway(int x, int y, int tx, int ty, const set<pair<int, int>>& claimed) {
    int best = manh(x, y, tx, ty), bi = -1;
    for (int k = 0; k < 4; k++) {
        int nx = x + DX[k], ny = y + DY[k];
        if (cellBlocked(nx, ny) || claimed.count({nx, ny})) continue;
        int d = manh(nx, ny, tx, ty);
        if (d > best) { best = d; bi = k; }
    }
    return bi >= 0 ? string(MV[bi]) : string("S");
}

// 刺客突袭方向是否可走两格（中间和落点不能是墙/塔/营地/水晶/小兵，落点不能站着英雄）
bool dashOk(int x, int y, const string& mv) {
    int k = dirOf(mv);
    if (k < 0) return false;
    int x1 = x + DX[k], y1 = y + DY[k], x2 = x + 2 * DX[k], y2 = y + 2 * DY[k];
    if (!inMap(x1, y1) || !inMap(x2, y2)) return false;
    if (staticB[y1 * S_ + x1] || staticB[y2 * S_ + x2]) return false;
    if (minB[y1 * S_ + x1] || minB[y2 * S_ + x2]) return false;
    if (heroB[y2 * S_ + x2]) return false;
    return true;
}

pair<int, int> predPos(int x, int y, const string& mv) {
    if (mv == "U") return {x, y - 1};
    if (mv == "D") return {x, y + 1};
    if (mv == "L") return {x - 1, y};
    if (mv == "R") return {x + 1, y};
    return {x, y};
}

// 能打到 (x,y) 的最近活敌塔（塔火威胁）
const Tower* fireTowerAt(int x, int y) {
    const Tower* best = nullptr; int bd = INT_MAX;
    for (auto& t : G->towers) if (t.team != G->me && t.hp > 0 && canHit(t.x, t.y, x, y, G->towerRange)) {
        int d = manh(t.x, t.y, x, y);
        if (d < bd) { bd = d; best = &t; }
    }
    return best;
}

// 拆塔时是否有掩体：别的己方英雄更靠近塔，或小兵同距离（塔平手先打小兵）
bool haveCover(int x, int y, const Tower* tw, int myId) {
    int myd = manh(x, y, tw->x, tw->y);
    for (auto& o : G->heroes) if (o.team == G->me && o.x >= 0 && o.id != myId &&
                                  manh(o.x, o.y, tw->x, tw->y) < myd) return true;
    for (auto& m : G->minions) if (m.team == G->me && manh(m.x, m.y, tw->x, tw->y) <= myd) return true;
    return false;
}

// 顶塔资格闸门：血量不够硬（<75%）且没有掩体时，别在塔火里站桩/硬进
bool gatedAt(int x, int y, int myId, double ratio) {
    if (ratio >= 0.75) return false;
    const Tower* ft = fireTowerAt(x, y);
    return ft && !haveCover(x, y, ft, myId);
}

// 普攻目标：敌方英雄 > 残血自保清兵 > 建筑（推进目标 > 水晶 > 低血塔） > 小兵
int pickAttack(int x, int y, const Hero& h, const ClassInfo& ci) {
    int me = G->me, foe = 1 - me;
    double ratio = (double)h.hp / h.maxhp;
    int bt = -1; double br = 1e9; int bd = INT_MAX;
    for (auto& o : G->heroes) {
        if (o.team != foe || o.x < 0 || !canHit(x, y, o.x, o.y, ci.rng)) continue;
        double r = (double)o.hp / o.maxhp; int d = manh(x, y, o.x, o.y);
        if (r < br - 1e-9 || (r < br + 1e-9 && d < bd)) { br = r; bd = d; bt = o.id; }
    }
    if (bt >= 0) return bt;
    if (ratio < 0.6) {                       // 自己残血：先清贴脸小兵
        int bm = -1, bh = INT_MAX;
        for (auto& m : G->minions) if (m.team == foe && canHit(x, y, m.x, m.y, ci.rng) && m.hp < bh) { bh = m.hp; bm = m.id; }
        if (bm >= 0) return bm;
    }
    if (!gatedAt(x, y, h.id, ratio)) {       // 顶塔没资格就先不碰建筑
        if (pushId >= 10 && pushId <= 21) {
            for (auto& t : G->towers) if (t.team == foe && t.hp > 0 && t.id == pushId &&
                                          canHit(x, y, t.x, t.y, ci.rng)) return t.id;
        } else if (pushId >= 22) {
            const Nexus& fc = G->nexuses[foe];
            if (fc.hp > 0 && canHit(x, y, fc.x, fc.y, ci.rng)) return fc.id;
        }
        const Nexus& fc = G->nexuses[foe];
        if (fc.hp > 0 && canHit(x, y, fc.x, fc.y, ci.rng)) return fc.id;
        int bs = -1, bh = INT_MAX;
        for (auto& t : G->towers) if (t.team == foe && t.hp > 0 && canHit(x, y, t.x, t.y, ci.rng) && t.hp < bh) {
            bh = t.hp; bs = t.id;
        }
        if (bs >= 0) return bs;
    }
    int bm = -1, bh2 = INT_MAX;
    for (auto& m : G->minions) if (m.team == foe && canHit(x, y, m.x, m.y, ci.rng) && m.hp < bh2) { bh2 = m.hp; bm = m.id; }
    return bm;
}

// 法师火球 / 射手穿云箭：返回是否值得停下施放（写 tgt）
// 穿云箭按引擎实现只伤害"自己与瞄准点之间"的单位，所以按连线价值选瞄准点。
bool pickShot(int x, int y, const Hero& h, const ClassInfo& ci, int& tgt) {
    int foe = 1 - G->me;
    if (ci.skill == "火球") {
        int bt = -1; double br = 1e9;                     // 守塔英雄优先消耗
        for (auto& o : G->heroes) {
            if (o.team != foe || o.x < 0) continue;
            int d = manh(x, y, o.x, o.y);
            if (d >= 1 && d <= 5 && lineClear(x, y, o.x, o.y)) {
                double r = (double)o.hp / o.maxhp;
                if (r < br) { br = r; bt = o.id; }
            }
        }
        if (bt >= 0) { tgt = bt; return true; }
        int bs = -1;                                      // 贴身（≤2）才火球建筑，远处留着赶路
        auto tryStruct = [&](int id, int sx, int sy) {
            if (bs >= 0) return;
            int d = manh(x, y, sx, sy);
            if (d >= 1 && d <= 2 && lineClear(x, y, sx, sy)) bs = id;
        };
        if (pushId >= 10 && pushId <= 21)
            for (auto& t : G->towers) if (t.team == foe && t.hp > 0 && t.id == pushId) tryStruct(t.id, t.x, t.y);
        const Nexus& fc = G->nexuses[foe];
        if (fc.hp > 0 && (pushId >= 22)) tryStruct(fc.id, fc.x, fc.y);
        for (auto& t : G->towers) if (t.team == foe && t.hp > 0 && t.id != pushId) tryStruct(t.id, t.x, t.y);
        if (bs >= 0) { tgt = bs; return true; }
        return false;
    }
    if (ci.skill == "穿云箭") {
        struct Foe { int id, x, y, w; };
        vector<Foe> fs;
        for (auto& o : G->heroes) if (o.team == foe && o.x >= 0) fs.push_back({o.id, o.x, o.y, 3});
        for (auto& m : G->minions) if (m.team == foe) fs.push_back({m.id, m.x, m.y, 1});
        for (auto& t : G->towers) if (t.team == foe && t.hp > 0) fs.push_back({t.id, t.x, t.y, 2});
        const Nexus& fc = G->nexuses[foe];
        if (fc.hp > 0) fs.push_back({fc.id, fc.x, fc.y, 3});
        int best = -1, bv = 0;
        for (auto& a : fs) {
            int d = manh(x, y, a.x, a.y);
            if (d < 2 || d > 4 || (a.x != x && a.y != y) || !lineClear(x, y, a.x, a.y)) continue;
            int v = 0;
            for (auto& b : fs) {
                if (b.id == a.id) continue;
                if (a.y == y && b.y == y && b.x > min(x, a.x) && b.x < max(x, a.x)) v += b.w;
                else if (a.x == x && b.x == x && b.y > min(y, a.y) && b.y < max(y, a.y)) v += b.w;
            }
            if (v > bv) { bv = v; best = a.id; }
        }
        if (bv >= 3 && best >= 0) { tgt = best; return true; }
        return false;
    }
    return false;
}

struct Cmd { string mv = "S", act = "-"; int tgt = -1; string note; };

void decide(Game& g, vector<Cmd>& out) {
    G = &g; S_ = g.S;
    int me = g.me, foe = 1 - me;
    staticB.assign(S_ * S_, 0); minB.assign(S_ * S_, 0); heroB.assign(S_ * S_, 0);
    allyB.assign(S_ * S_, 0); foeB.assign(S_ * S_, 0);
    vacate.clear();
    for (int y = 0; y < S_; y++) for (int x = 0; x < S_; x++)
        if (g.mapRows[y][x] == '#') staticB[y * S_ + x] = 1;      // 墙必须算进静态阻挡
    for (auto& t : g.towers) if (t.hp > 0) staticB[t.y * S_ + t.x] = 1;
    for (auto& c : g.camps) if (c.hp > 0) staticB[c.y * S_ + c.x] = 1;
    for (auto& n : g.nexuses) staticB[n.y * S_ + n.x] = 1;
    for (auto& m : g.minions) minB[m.y * S_ + m.x] = 1;
    for (auto& h : g.heroes) if (h.x >= 0) {
        heroB[h.y * S_ + h.x] = 1;
        (h.team == me ? allyB : foeB)[h.y * S_ + h.x] = 1;
    }
    // 车道格（与引擎同式）：三路走廊 + 两块基地，抱团寻路贴着大路走
    laneCell.assign(S_ * S_, 0);
    auto markLane = [&](int x, int y) { if (inMap(x, y)) laneCell[y * S_ + x] = 1; };
    auto rot = [&](int x, int y) { return make_pair(S_ - 1 - x, S_ - 1 - y); };
    for (int x = 2; x <= 7; x++) for (int y = S_ - 8; y <= S_ - 3; y++) { markLane(x, y); auto r = rot(x, y); markLane(r.first, r.second); }
    for (int x = 2; x <= S_ - 8; x++) for (int y : {2, 3}) { markLane(x, y); auto r = rot(x, y); markLane(r.first, r.second); }
    for (int y = 2; y <= S_ - 8; y++) for (int x : {2, 3}) { markLane(x, y); auto r = rot(x, y); markLane(r.first, r.second); }
    for (int x = 2; x <= S_ - 3; x++) for (int y = 2; y <= S_ - 3; y++)
        if (x + y >= S_ - 2 && x + y <= S_) { markLane(x, y); }

    const Nexus& myC = g.nexuses[me];
    const Nexus& foeC = g.nexuses[foe];

    for (int L = 0; L < 3; L++) lanePen[L] = max(0, lanePen[L] - 60);

    // ---- 每条路最前面的敌塔（离敌方水晶最远 = 离我们最近，先拆它）----
    array<const Tower*, 3> front = {nullptr, nullptr, nullptr};
    for (const auto& t : g.towers) {
        if (t.team != foe || t.hp <= 0) continue;
        const Tower* f = front[t.lane];
        if (!f || manh(t.x, t.y, foeC.x, foeC.y) > manh(f->x, f->y, foeC.x, foeC.y)) front[t.lane] = &t;
    }

    // ---- 打不过就转战另一条路：当前目标附近敌人抱团且战力悬殊 → 拉黑这条路 ----
    if (curLane >= 0) {
        int qx = foeC.x, qy = foeC.y; bool isTower = false;
        if (front[curLane]) { qx = front[curLane]->x; qy = front[curLane]->y; isTower = true; }
        double ourP = 0, theirP = 0; int nFoe = 0;
        for (auto& h : g.heroes) {
            if (h.x < 0) continue;
            int d = manh(h.x, h.y, qx, qy);
            if (h.team == me && d <= 12) ourP += h.hp + 3.0 * atkOf(g.cls[h.id - 5 * me], h.level);
            if (h.team == foe && d <= 8) { theirP += h.hp + 3.0 * atkOf(g.cls[h.id - 5 * foe], h.level); nFoe++; }
        }
        for (auto& m : g.minions) if (m.team == me && manh(m.x, m.y, qx, qy) <= 7)
            ourP += m.hp + 3.0 * g.minionAtk;
        double cap = isTower ? 3.0 * front[curLane]->atk + 0.35 * front[curLane]->hp : 0.0;
        bool otherHasTower = false;
        for (int L = 0; L < 3; L++) if (L != curLane && front[L]) otherHasTower = true;
        if (isTower) {
            if (nFoe >= 3 && theirP + cap > 1.45 * ourP) lanePen[curLane] = 1200;
        } else if (otherHasTower && theirP > 1.6 * max(ourP, 1.0)) {
            lanePen[curLane] = 1200;              // 水晶战打不动且别路还有塔 → 换路
        }
    }

    // ---- 选路：塔最残血 / 防守最弱者优先；另一路明显更软（<55%）才中途换 ----
    auto rawScore = [&](int L) -> double {
        if (!front[L]) return 1e18;
        double s = 0;
        for (const auto& t : g.towers) if (t.team == foe && t.hp > 0 && t.lane == L) s += t.hp;
        for (auto& o : g.heroes) if (o.team == foe && o.x >= 0 &&
                                     manh(o.x, o.y, front[L]->x, front[L]->y) <= 9) s += 140;
        for (auto& m : g.minions) if (m.team == foe && manh(m.x, m.y, front[L]->x, front[L]->y) <= 9) s += 25;
        return s + lanePen[L];
    };
    if (curLane < 0) {
        curLane = 0;
        for (int L = 1; L < 3; L++) if (rawScore(L) < rawScore(curLane)) curLane = L;
    } else if (front[curLane]) {
        double cs = rawScore(curLane), bs = 0.55 * cs; int bl = -1;
        for (int L = 0; L < 3; L++) if (L != curLane && front[L] && rawScore(L) < bs) { bs = rawScore(L); bl = L; }
        if (bl >= 0) curLane = bl;
    } else if (lanePen[curLane] > 0) {
        int bl = -1;                              // 这路打穿了但晶体打不动 → 换条有塔的路
        for (int L = 0; L < 3; L++) if (front[L] && (bl < 0 || rawScore(L) < rawScore(bl))) bl = L;
        if (bl >= 0) curLane = bl;
    }

    // ---- 本回合推进目标 ----
    pushId = foeC.id;
    int px = foeC.x, py = foeC.y;
    if (front[curLane]) { pushId = front[curLane]->id; px = front[curLane]->x; py = front[curLane]->y; }

    // ---- 回防判断：水晶遇袭且不在冲塔竞速 → 全员回家 ----
    int foesAt = 0, foesNear = 0, foesMid = 0;
    for (auto& o : g.heroes) if (o.team == foe && o.x >= 0) {
        int d = manh(o.x, o.y, myC.x, myC.y);
        if (d <= 4) foesAt++;
        if (d <= 7) foesNear++;
        if (d <= 12) foesMid++;
    }
    bool minAtHome = false;
    for (auto& m : g.minions) if (m.team == foe && manh(m.x, m.y, myC.x, myC.y) <= 4) minAtHome = true;
    int ourAtFoe = 0;
    for (auto& h : g.heroes) if (h.team == me && h.x >= 0 && manh(h.x, h.y, foeC.x, foeC.y) <= 14) ourAtFoe++;
    bool racing = foeC.hp <= 0.5 * foeC.maxhp && ourAtFoe >= 3;   // 正在拆对面水晶 → 无视回防
    bool defend = !racing && (foesAt >= 1 || foesNear >= 3 || minAtHome ||
                              (myC.hp < 0.7 * myC.maxhp && foesMid >= 1));

    // ---- 距离场：回家 / 推进 / 回防 ----
    vector<pair<int, int>> mySpawns(g.spawns.begin() + (me == 0 ? 0 : 5), g.spawns.begin() + (me == 0 ? 5 : 10));
    vector<int> homeField = bfsField(mySpawns);
    vector<int> pushField = bfsField({{px, py}});
    vector<int> defField = bfsField({{myC.x, myC.y}});

    // ---- 逐英雄决策（按 id 顺序占位，避免撞车）----
    set<pair<int, int>> claimed;
    out.assign(5, Cmd());
    for (int k = 0; k < 5; k++) {
        Hero& h = g.heroes[5 * me + k];
        if (h.x < 0) { out[k].note = "阵亡"; continue; }
        const ClassInfo& ci = g.cls[k];
        double ratio = (double)h.hp / h.maxhp;
        if (ratio >= 0.95) goHome[k] = false;

        bool inFireNow = fireTowerAt(h.x, h.y) != nullptr;
        int ehD = INT_MAX;
        const Hero* nearestFoe = nullptr;
        for (auto& o : g.heroes) if (o.team == foe && o.x >= 0) {
            int d = manh(h.x, h.y, o.x, o.y);
            if (d < ehD) { ehD = d; nearestFoe = &o; }
        }
        bool danger = inFireNow || ehD <= 3;
        if (!goHome[k]) {
            if (defend) { if (ratio < 0.25) goHome[k] = true; }
            else if (ratio < 0.32) goHome[k] = true;          // 只有真残血才撤回泉水
        }
        bool retreat = goHome[k];
        // 战术后撤：顶塔/接战中掉到六成以下，先退几格脱离火力，治疗回来再上（不直接回城）
        bool hover = false; pair<int, int> hthreat{0, 0};
        if (!retreat && !defend && danger && ratio < 0.60) {
            if (nearestFoe && ehD <= 5) { hthreat = {nearestFoe->x, nearestFoe->y}; hover = true; }
            else if (inFireNow) { const Tower* ft0 = fireTowerAt(h.x, h.y); hthreat = {ft0->x, ft0->y}; hover = true; }
        }

        int shotTgt = -1; bool shotStop = false;
        if (!retreat && h.cd == 0 && (ci.skill == "火球" || ci.skill == "穿云箭"))
            if (pickShot(h.x, h.y, h, ci, shotTgt) && !gatedAt(h.x, h.y, h.id, ratio)) shotStop = true;
        int tgtHere = retreat ? -1 : pickAttack(h.x, h.y, h, ci);

        string mv = "S"; bool dash = false;
        if (retreat) {
            const Tower* ft = fireTowerAt(h.x, h.y);
            bool inBase = h.x >= g.bases[me][0] && h.x <= g.bases[me][2] &&
                          h.y >= g.bases[me][1] && h.y <= g.bases[me][3];
            if (!ft && inBase) mv = "S";      // 泉水休息回满
            else {
                mv = ft ? stepAway(h.x, h.y, ft->x, ft->y, claimed)
                        : stepToward(h.x, h.y, homeField, claimed);
                if (ci.skill == "突袭" && h.cd == 0 && mv != "S" && dashOk(h.x, h.y, mv)) dash = true;
            }
        } else if (hover) {
            mv = stepAway(h.x, h.y, hthreat.first, hthreat.second, claimed);
            if (ci.skill == "突袭" && h.cd == 0 && mv != "S" && dashOk(h.x, h.y, mv)) dash = true;
        } else if (tgtHere >= 0 || shotStop) {
            mv = "S";                          // 有输出机会就站住打
        } else {
            const vector<int>& F = defend ? defField : pushField;
            // 集结：目标附近敌人抱团防守而队友没跟上时，前排先停下等齐再动手
            bool holdUp = false;
            if (!defend) {
                int foesAtT = 0, mates = 0;
                for (auto& o : g.heroes) if (o.x >= 0) {
                    if (o.team == foe && manh(o.x, o.y, px, py) <= 8) foesAtT++;
                    if (o.team == me && o.id != h.id && manh(o.x, o.y, h.x, h.y) <= 8) mates++;
                }
                if (foesAtT >= 2 && mates < 3 && manh(h.x, h.y, px, py) <= 10) holdUp = true;
            }
            if (holdUp) {
                mv = "S";
            } else {
                string cand = stepToward(h.x, h.y, F, claimed);
                bool useDash = false;
                if (cand != "S" && ci.skill == "突袭" && h.cd == 0 && dashOk(h.x, h.y, cand)) useDash = true;
                auto dst = predPos(h.x, h.y, cand);
                if (useDash) { int d_ = dirOf(cand); dst = {h.x + 2 * DX[d_], h.y + 2 * DY[d_]}; }
                if (cand != "S" && gatedAt(dst.first, dst.second, h.id, ratio)) {
                    const Tower* cur = fireTowerAt(h.x, h.y);   // 别硬进塔火：退出射程外围等
                    if (cur) {
                        mv = stepAway(h.x, h.y, cur->x, cur->y, claimed);
                        if (ci.skill == "突袭" && h.cd == 0 && mv != "S" && dashOk(h.x, h.y, mv)) dash = true;
                    } else mv = "S";
                } else { mv = cand; dash = useDash; }
            }
        }
        auto pp = predPos(h.x, h.y, mv);
        if (dash) { int d_ = dirOf(mv); pp = {h.x + 2 * DX[d_], h.y + 2 * DY[d_]}; }

        Cmd c; c.mv = mv;
        if (ci.skill == "回春术" && h.cd == 0 && ratio < 0.72) {
            c.act = "Q"; c.note = "回春续塔";
        } else if (ci.skill == "治疗术" && h.cd == 0) {
            double mr = 1.0;
            for (auto& o : g.heroes) if (o.team == me && o.x >= 0 && manh(pp.first, pp.second, o.x, o.y) <= 2)
                mr = min(mr, (double)o.hp / o.maxhp);
            if (mr < 0.82) { c.act = "Q"; c.note = "治疗保排"; }
        } else if (shotStop) {
            c.act = "Q"; c.tgt = shotTgt; c.note = ci.skill == "火球" ? "火球消耗" : "穿云箭贯穿";
        } else if (dash) {
            c.act = "Q"; c.note = "突袭";
        }
        if (c.act == "-") {
            int at = pickAttack(pp.first, pp.second, h, ci);
            if (at >= 0) { c.act = to_string(at); c.note = "进攻"; }
        }
        if (c.note.empty()) c.note = retreat ? "撤退回泉水" : (defend ? "回防守水晶" : "抱团推进");
        claimed.insert(pp);
        if (mv != "S") vacate.insert({h.x, h.y});   // 队友正在离开的格子，后面的英雄可以跟进
        out[k] = c;
    }
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    Game g;
    while (read_block(g)) {
        vector<Cmd> out;
        decide(g, out);
        cout << g.turn;
        for (auto& c : out) {
            cout << ' ' << c.mv << ' ' << c.act;
            if (c.act == "Q" && c.tgt >= 0) cout << ' ' << c.tgt;
        }
        cout << " #";
        for (int i = 0; i < 5; i++) { if (i) cout << ';'; cout << out[i].note; }
        cout << endl;
    }
}
