// balance.cpp —— 均衡运营示例队（baseline 的全面强化版）
//  分路：战士 A、刺客打野（清完野随等级游走支援）、法师 C、射手+辅助 B。
//  防守：己方塔掉血或敌军逼近 → 最近的健康英雄回防；水晶告急全员回防；塔被拆后由兵线自然收缩。
//  集火：普攻优先"这一下能打死"的目标（英雄>小兵>野怪），其次残血英雄；火球/穿云箭直线收割残血。
//  推进：跟己方兵线上（不超前于兵线），无小兵掩护不碰塔，局部兵力劣势就后撤，拆完塔确认安全再深入。
//  撤退：<45% 血有威胁就撤（刺客 <55%），回泉水回满（>92%）再出门；辅助的治疗留给最危难的队友。
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
    vector<string> mapRows;              // mapRows[y][x]：'#' 墙，'.' 空地
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

static Game* G = nullptr;
inline bool wall(int x, int y) { return x < 0 || y < 0 || x >= G->S || y >= G->S || G->mapRows[y][x] == '#'; }
inline int manh(int ax, int ay, int bx, int by) { return abs(ax - bx) + abs(ay - by); }

const int DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};      // N S W E
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

// 直线（同行/同列）是否被墙挡断（不含端点）
bool lineClear(int ax, int ay, int bx, int by) {
    if (ax == bx) { for (int y = min(ay, by) + 1; y < max(ay, by); y++) if (wall(ax, y)) return false; return true; }
    if (ay == by) { for (int x = min(ax, bx) + 1; x < max(ax, bx); x++) if (wall(x, ay)) return false; return true; }
    return false;
}

// 从 src 出发的 BFS 距离场（绕开墙和 blocked；src 本身允许被挡）
vector<int> bfs(const vector<pair<int, int>>& src, const set<pair<int, int>>& blocked) {
    int S = G->S;
    vector<int> d(S * S, -1);
    deque<pair<int, int>> q;
    for (auto& p : src) {
        if (p.first < 0 || p.second < 0 || p.first >= S || p.second >= S) continue;
        if (d[p.second * S + p.first] >= 0) continue;
        d[p.second * S + p.first] = 0;
        q.push_back(p);
    }
    while (!q.empty()) {
        auto [x, y] = q.front(); q.pop_front();
        for (int kk = 0; kk < 4; kk++) {
            int nx = x + DX[kk], ny = y + DY[kk];
            if (wall(nx, ny) || blocked.count({nx, ny}) || d[ny * S + nx] >= 0) continue;
            d[ny * S + nx] = d[y * S + x] + 1;
            q.push_back({nx, ny});
        }
    }
    return d;
}

struct Cmd { string mv = "S", act = "-"; int tgt = -1; };

// 跨回合记忆：上一回合各塔/水晶血量（用于发现"塔在掉血"）
struct Persist { bool init = false; int twHp[32] = {0}; int nxHp[2] = {0, 0}; };
static Persist P;

void decide(Game& g, vector<Cmd>& out) {
    G = &g;
    const int me = g.me, foe = 1 - me, S = g.S;
    out.assign(5, Cmd{});

    // ---------- 单位 ----------
    Hero* myH[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    vector<Hero*> foeH;
    for (auto& h : g.heroes) {
        if (h.team == me) { int k = h.id - 5 * me; if (0 <= k && k < 5) myH[k] = &h; }
        else if (h.x >= 0) foeH.push_back(&h);
    }
    vector<Minion*> myMin, foeMin;
    for (auto& m : g.minions) (m.team == me ? myMin : foeMin).push_back(&m);
    vector<Tower*> myTw, foeTw;
    for (auto& t : g.towers) if (t.hp > 0) (t.team == me ? myTw : foeTw).push_back(&t);
    Nexus& myNx = g.nexuses[me];
    Nexus& foeNx = g.nexuses[foe];

    // ---------- 占位与距离场 ----------
    set<pair<int, int>> busy, statics;
    for (auto& h : g.heroes) if (h.x >= 0) busy.insert({h.x, h.y});
    for (auto& m : g.minions) busy.insert({m.x, m.y});
    for (auto& t : g.towers) if (t.hp > 0) { busy.insert({t.x, t.y}); statics.insert({t.x, t.y}); }
    for (auto& c : g.camps) if (c.hp > 0) { busy.insert({c.x, c.y}); statics.insert({c.x, c.y}); }
    for (auto& n : g.nexuses) { busy.insert({n.x, n.y}); statics.insert({n.x, n.y}); }

    vector<pair<int, int>> homeSp(g.spawns.begin() + (me == 0 ? 0 : 5), g.spawns.begin() + (me == 0 ? 5 : 10));
    const vector<int> homeField = bfs(homeSp, statics);
    map<pair<int, int>, vector<int>> fieldCache;
    auto fieldTo = [&](int tx, int ty) -> const vector<int>& {
        auto it = fieldCache.find(make_pair(tx, ty));
        if (it != fieldCache.end()) return it->second;
        vector<int> f = bfs({{tx, ty}}, statics);
        return fieldCache.emplace(make_pair(tx, ty), move(f)).first->second;
    };
    auto distTo = [&](int fx, int fy, int tx, int ty) -> int {
        const vector<int>& f = fieldTo(tx, ty);
        int d = f[fy * S + fx];
        return d >= 0 ? d : manh(fx, fy, tx, ty);
    };

    // ---------- 首回合初始化跨回合记忆 ----------
    if (!P.init) {
        P.init = true;
        for (auto& t : g.towers) P.twHp[t.id] = t.hp;
        P.nxHp[0] = g.nexuses[0].hp;
        P.nxHp[1] = g.nexuses[1].hp;
    }

    // ---------- 小工具 ----------
    auto inBase = [&](int x, int y) -> bool {
        const auto& b = g.bases[me];
        return x >= b[0] && x <= b[2] && y >= b[1] && y <= b[3];
    };
    // (x,y) 是否在敌塔火力圈内且没有己方小兵/更靠前的队友替我们扛塔
    auto exposed = [&](int x, int y) -> bool {
        for (auto* t : foeTw) {
            int d = manh(x, y, t->x, t->y);
            if (d > g.towerRange) continue;
            bool cov = false;
            for (auto* m : myMin) if (manh(m->x, m->y, t->x, t->y) <= d) { cov = true; break; }
            if (!cov)
                for (auto& o : g.heroes)
                    if (o.team == me && o.x >= 0 && !(o.x == x && o.y == y) && manh(o.x, o.y, t->x, t->y) < d) { cov = true; break; }
            if (!cov) return true;
        }
        return false;
    };

    set<pair<int, int>> claimed;             // 本回合先决策英雄已占的目的地
    auto applyMv = [&](int x, int y, const string& mv, int& nx, int& ny) {
        nx = x; ny = y;
        if (mv == "U") ny--; else if (mv == "D") ny++; else if (mv == "L") nx--; else if (mv == "R") nx++;
    };
    auto stepFieldRaw = [&](int x, int y, const vector<int>& f) -> string {
        int cur = f[y * S + x];
        if (cur < 0 || cur >= INT_MAX / 2) return "S";
        int bv = INT_MAX, bi = -1;
        bool anyImproving = false;           // 存在更优方向（可能被单位挡住）
        for (int kk = 0; kk < 4; kk++) {
            int nx = x + DX[kk], ny = y + DY[kk];
            if (wall(nx, ny)) continue;
            int v = f[ny * S + nx];
            if (v < 0) continue;
            if (v < cur) anyImproving = true;
            if (busy.count({nx, ny}) || claimed.count({nx, ny})) continue;
            if (v < bv) { bv = v; bi = kk; }
        }
        if (bi >= 0 && bv < cur) return string(MV[bi]);
        if (anyImproving) {                  // 前路被单位堵住：侧移绕开，别原地硬扛
            int bv2 = INT_MAX, bi2 = -1;
            for (int kk = 0; kk < 4; kk++) {
                int nx = x + DX[kk], ny = y + DY[kk];
                if (wall(nx, ny) || busy.count({nx, ny}) || claimed.count({nx, ny})) continue;
                int v = f[ny * S + nx]; if (v < 0) v = INT_MAX / 2;
                if (v < bv2) { bv2 = v; bi2 = kk; }
            }
            if (bi2 >= 0) return string(MV[bi2]);
        }
        return "S";
    };
    auto stepField = [&](int x, int y, const vector<int>& f) -> string {
        string mv = stepFieldRaw(x, y, f);
        if (mv != "S") { int px, py; applyMv(x, y, mv, px, py); claimed.insert({px, py}); }
        return mv;
    };
    auto greedyStep = [&](int x, int y, int tx, int ty) -> string {
        int bd = manh(x, y, tx, ty), bi = -1;
        for (int kk = 0; kk < 4; kk++) {
            int nx = x + DX[kk], ny = y + DY[kk];
            if (wall(nx, ny) || busy.count({nx, ny}) || claimed.count({nx, ny})) continue;
            int d = manh(nx, ny, tx, ty);
            if (d < bd) { bd = d; bi = kk; }
        }
        if (bi < 0) return "S";
        int px, py; applyMv(x, y, string(MV[bi]), px, py);
        claimed.insert({px, py});
        return string(MV[bi]);
    };
    // 前进一步，但不踏进敌塔火力圈
    auto advanceGuarded = [&](int x, int y, const vector<int>& f) -> string {
        string mv = stepFieldRaw(x, y, f);
        if (mv == "S") return mv;
        int px, py; applyMv(x, y, mv, px, py);
        if (exposed(px, py)) return "S";
        claimed.insert({px, py});
        return mv;
    };

    // ---------- 集火协同：本回合已宣布的普攻/火球伤害，后面的英雄据此判断"能不能打死" ----------
    map<int, int> committed;
    auto commitCmd = [&](int k, const Cmd& c) {
        Hero* hh = myH[k];
        if (!hh) return;
        if (c.act == "Q" && c.tgt >= 0 && k == 2) committed[c.tgt] += 45 + 6 * hh->level;
        else if (c.act != "-" && c.act != "Q") committed[stoi(c.act)] += g.cls[k].atk + 2 * (hh->level - 1);
    };

    // ---------- 普攻目标：能打死 > 残血英雄 > 小兵 > 野怪（maxCat 限制最高类目） ----------
    struct Pick { int id = -1, tx = 0, ty = 0, d = 0; };
    auto pickUnit = [&](int px, int py, int rng, int atk, bool chase, bool allowCamp, int maxCat, bool aggro) -> Pick {
        struct Cand { int id, cat, sec, d, tx, ty; };
        Cand best{}; best.id = -1;
        auto consider = [&](int id, int cat, int sec, int ux, int uy, bool chaseable) {
            if (cat > maxCat) return;
            int d = manh(px, py, ux, uy);
            if (d < 1) return;
            bool inR = d <= rng && canHit(px, py, ux, uy, rng);
            bool nxt = chaseable && chase && d == rng + 1;
            if (!inR && !nxt) return;
            bool better = best.id < 0;
            if (!better) {
                if (cat != best.cat) better = cat < best.cat;
                else if (sec != best.sec) better = sec < best.sec;
                else if (d != best.d) better = d < best.d;
                else better = id < best.id;
            }
            if (better) best = {id, cat, sec, d, ux, uy};
        };
        auto effHp = [&](int id, int hp) { auto it = committed.find(id); return it == committed.end() ? hp : hp - it->second; };
        for (auto* o : foeH) {
            int eff = effHp(o->id, o->hp);
            bool kl = eff <= atk;
            consider(o->id, kl ? 0 : 4, kl ? max(0, eff) : o->hp * 1000 / max(1, o->maxhp),
                     o->x, o->y, chase && (aggro || kl));
        }
        for (auto* m : foeMin) consider(m->id, effHp(m->id, m->hp) <= atk ? 1 : 5, max(0, effHp(m->id, m->hp)), m->x, m->y, chase);
        if (allowCamp)
            for (auto& c : g.camps) if (c.hp > 0)
                consider(c.id, effHp(c.id, c.hp) <= atk ? 2 : 6, max(0, effHp(c.id, c.hp)), c.x, c.y, chase);
        return Pick{best.id, best.tx, best.ty, best.d};
    };

    // ---------- 技能 ----------
    struct Sk { bool used = false; int tgt = -1; };
    auto trySkill = [&](Hero& h, int k, int px, int py, bool fleeing) -> Sk {
        const ClassInfo& ci = g.cls[k];
        Sk so;
        if (h.cd != 0) return so;
        if (ci.skill == "回春术") {
            double rr = (double)h.hp / h.maxhp;
            if (rr < 0.60 || (fleeing && rr < 0.80)) so.used = true;
            return so;
        }
        if (ci.skill == "治疗术") {           // 引擎自动治疗射程内血量比例最低的友军
            double worst = 1.0;
            for (auto& o : g.heroes)
                if (o.team == me && o.x >= 0 && manh(o.x, o.y, px, py) <= 2)
                    worst = min(worst, (double)o.hp / o.maxhp);
            if (worst < 0.55) so.used = true;
            return so;
        }
        if (ci.skill == "火球") {
            int dmg = 45 + 6 * h.level;
            auto effOf = [&](int id, int hp) { auto it = committed.find(id); return it == committed.end() ? hp : hp - it->second; };
            Hero* bt = nullptr; bool bkl = false;
            for (auto* o : foeH) {            // 直线收割：能打死的优先，其次残血
                if (manh(px, py, o->x, o->y) > 5 || !lineClear(px, py, o->x, o->y)) continue;
                bool kl = effOf(o->id, o->hp) <= dmg;
                if (!bt || (kl && !bkl) || (kl == bkl && o->hp < bt->hp)) { bt = o; bkl = kl; }
            }
            if (bt) { so.used = true; so.tgt = bt->id; return so; }
            if (fleeing) return so;
            Tower* st = nullptr;              // 推塔时的免费消耗
            for (auto* t : foeTw)
                if (manh(px, py, t->x, t->y) <= 5 && lineClear(px, py, t->x, t->y) && (!st || t->hp < st->hp)) st = t;
            if (st) { so.used = true; so.tgt = st->id; return so; }
            if (manh(px, py, foeNx.x, foeNx.y) <= 5 && lineClear(px, py, foeNx.x, foeNx.y)) {
                so.used = true; so.tgt = foeNx.id; return so;
            }
            for (auto* m : foeMin)            // 一发烧死小兵=清线
                if (effOf(m->id, m->hp) <= dmg && manh(px, py, m->x, m->y) <= 5 && lineClear(px, py, m->x, m->y)) {
                    so.used = true; so.tgt = m->id; return so;
                }
            return so;
        }
        if (ci.skill == "穿云箭") {
            Hero* bt = nullptr;
            for (auto* o : foeH)
                if (manh(px, py, o->x, o->y) <= 4 && lineClear(px, py, o->x, o->y) && (!bt || o->hp < bt->hp)) bt = o;
            if (bt) { so.used = true; so.tgt = bt->id; return so; }
            if (fleeing) return so;
            for (auto* m : foeMin) {          // 同一直线上 ≥2 个小兵 → 清线
                if (manh(px, py, m->x, m->y) > 4 || !lineClear(px, py, m->x, m->y)) continue;
                int cnt = 0;
                for (auto* o : foeMin) {
                    if (o == m || manh(px, py, o->x, o->y) > 4 || !lineClear(px, py, o->x, o->y)) continue;
                    bool same = false;
                    if (m->y == py) same = (o->y == py) && (o->x - px) * (m->x - px) > 0;
                    else if (m->x == px) same = (o->x == px) && (o->y - py) * (m->y - py) > 0;
                    if (same) cnt++;
                }
                if (cnt >= 1) { so.used = true; so.tgt = m->id; return so; }
            }
            Tower* st = nullptr;
            for (auto* t : foeTw)
                if (manh(px, py, t->x, t->y) <= 4 && lineClear(px, py, t->x, t->y) && (!st || t->hp < st->hp)) st = t;
            if (st) { so.used = true; so.tgt = st->id; return so; }
            return so;
        }
        return so;                            // 突袭在移动逻辑里处理
    };

    // ---------- 突袭合法性（本回合真的能走两格） ----------
    auto dashOk = [&](int x, int y, const string& mv) -> bool {
        int bi = -1;
        for (int kk = 0; kk < 4; kk++) if (mv == MV[kk]) { bi = kk; break; }
        if (bi < 0) return false;
        int x1 = x + DX[bi], y1 = y + DY[bi], x2 = x1 + DX[bi], y2 = y1 + DY[bi];
        if (wall(x1, y1) || wall(x2, y2)) return false;
        if (busy.count({x1, y1}) || busy.count({x2, y2})) return false;
        return true;
    };

    // ---------- 防守分派：己方塔掉血/敌军逼近 → 最近的健康英雄回防 ----------
    bool defender[5] = {false, false, false, false, false};
    pair<int, int> defGoal[5];
    {
        struct Threat { Tower* t; int sev, id; };
        vector<Threat> ths;
        for (auto* t : myTw) {
            int drop = P.twHp[t->id] - t->hp;
            int eMin = 0, eHer = 0;
            for (auto* m : foeMin) if (manh(m->x, m->y, t->x, t->y) <= 3) eMin++;
            for (auto* h : foeH) if (manh(h->x, h->y, t->x, t->y) <= 3) eHer++;
            if (drop > 0 || eMin > 0 || eHer > 0)
                ths.push_back({t, 1 + (drop > 0 ? 2 : 0) + (eHer > 0 ? 2 : 0), t->id});
        }
        stable_sort(ths.begin(), ths.end(), [](const Threat& a, const Threat& b) {
            if (a.sev != b.sev) return a.sev > b.sev;
            return a.id < b.id;
        });
        int used = 0;
        for (auto& th : ths) {
            if (used >= 3) break;
            int need = 1;
            for (auto* h : foeH) if (manh(h->x, h->y, th.t->x, th.t->y) <= 4) need = 2;
            for (int i = 0; i < need && used < 3; i++) {
                int bk = -1, bd = INT_MAX;
                for (int pass = 0; pass < 2 && bk < 0; pass++) {   // 支援最后才考虑
                    for (int kk = 0; kk < 5; kk++) {
                        Hero* h = myH[kk];
                        if (!h || h->x < 0 || defender[kk]) continue;
                        if (pass == 0 && kk == 4) continue;
                        if (h->hp < 0.5 * h->maxhp) continue;
                        if (inBase(h->x, h->y) && h->hp < 0.85 * h->maxhp && th.sev < 3) continue;
                        int d = distTo(h->x, h->y, th.t->x, th.t->y);
                        if (d < bd) { bd = d; bk = kk; }
                    }
                }
                if (bk >= 0) { defender[bk] = true; defGoal[bk] = make_pair(th.t->x, th.t->y); used++; }
            }
        }
    }
    // 水晶告急 → 全员回防（除非一击就能推平对面水晶）
    bool crystalThreat = (P.nxHp[me] - myNx.hp) > 0;
    for (auto* h : foeH) if (manh(h->x, h->y, myNx.x, myNx.y) <= 5) crystalThreat = true;
    for (auto* m : foeMin) if (manh(m->x, m->y, myNx.x, myNx.y) <= 2) crystalThreat = true;
    if (crystalThreat) {
        for (int kk = 0; kk < 5; kk++) {
            Hero* h = myH[kk];
            if (!h || h->x < 0) continue;
            if (h->hp < 0.40 * h->maxhp) continue;
            if (foeNx.hp <= 80 && manh(h->x, h->y, foeNx.x, foeNx.y) <= 3) continue;
            defender[kk] = true;
            defGoal[kk] = make_pair(myNx.x, myNx.y);
        }
    }

    // ================= 逐英雄决策 =================
    for (int k = 0; k < 5; k++) {
        Hero* ph = myH[k];
        if (!ph || ph->x < 0) continue;       // 阵亡也要输出（默认 S -）
        Hero& h = *ph;
        const ClassInfo& ci = g.cls[k];
        const int x = h.x, y = h.y;
        const int atk = ci.atk + 2 * (h.level - 1);
        const double r = (double)h.hp / h.maxhp;
        const bool inBaseNow = inBase(x, y);
        Cmd cmd;

        int nehD = INT_MAX;
        for (auto* o : foeH) nehD = min(nehD, manh(x, y, o->x, o->y));
        int foeM3 = 0, myM3 = 0, allyH2 = 0, allies4 = 1, foes4 = 0;
        for (auto* m : foeMin) if (manh(m->x, m->y, x, y) <= 3) foeM3++;
        for (auto* m : myMin) if (manh(m->x, m->y, x, y) <= 3) myM3++;
        for (auto& o : g.heroes) if (o.x >= 0) {
            int d = manh(o.x, o.y, x, y);
            if (o.id != h.id && d <= 2) allyH2++;
            if (o.team == me) { if (d <= 4) allies4++; }
            else if (d <= 4) foes4++;
        }
        bool aggro = allies4 > foes4;         // 局部人数占优才主动追着英雄打
        bool foeTwZone = exposed(x, y);

        // ---- 防守 ----
        if (defender[k]) {
            int gx = defGoal[k].first, gy = defGoal[k].second;
            bool atNexus = (gx == myNx.x && gy == myNx.y);
            int dg = manh(x, y, gx, gy);
            if (dg > (atNexus ? 3 : 2)) {
                const vector<int>& df = fieldTo(gx, gy);
                cmd.mv = stepField(x, y, df);
            } else {
                int bd = INT_MAX, bx = -1, by = -1;      // 就位后贴上去打
                for (auto* o : foeH) { int d = manh(x, y, o->x, o->y); if (d < bd) { bd = d; bx = o->x; by = o->y; } }
                for (auto* m : foeMin) { int d = manh(x, y, m->x, m->y); if (d < bd) { bd = d; bx = m->x; by = m->y; } }
                if (bd != INT_MAX && bd > 1 && bd <= 5) cmd.mv = greedyStep(x, y, bx, by);
            }
            int px, py; applyMv(x, y, cmd.mv, px, py);
            Pick pk = pickUnit(px, py, ci.rng, atk, false, true, 6, false);
            if (pk.id >= 0) cmd.act = to_string(pk.id);
            else {
                Sk sk = trySkill(h, k, px, py, false);
                if (sk.used) { cmd.act = "Q"; cmd.tgt = sk.tgt; }
            }
            out[k] = cmd; commitCmd(k, cmd);
            continue;
        }

        // ---- 泉水休整：没回满不出门 ----
        if (inBaseNow && r < 0.92) {
            if (ci.skill == "回春术" && h.cd == 0 && r < 0.70) cmd.act = "Q";
            else if (ci.skill == "治疗术" && h.cd == 0 && r < 0.75) cmd.act = "Q";
            out[k] = cmd; commitCmd(k, cmd);
            continue;
        }

        // ---- 残血撤退 ----
        bool wantRetreat = false;
        if (foeTwZone && r < 0.70) wantRetreat = true;                       // 被塔点名
        if (r < 0.45 && (nehD <= 6 || foeM3 > 0 || foeTwZone)) wantRetreat = true;
        if (r < 0.30 && (nehD <= 6 || foeM3 > 0)) wantRetreat = true;
        if (r < 0.80 && foeM3 >= 2 && foeM3 - myM3 >= 2 && allyH2 == 0) wantRetreat = true;   // 被小兵围
        if (r < 0.35) wantRetreat = true;                                    // 太残了先回家
        if (k == 1 && r < 0.55 && !defender[k]) wantRetreat = true;          // 刺客更脆
        if (wantRetreat) {
            cmd.mv = stepField(x, y, homeField);
            int px, py; applyMv(x, y, cmd.mv, px, py);
            if (ci.skill == "回春术" && h.cd == 0 && r < 0.75) cmd.act = "Q";   // 边撤边奶
            else {
                Pick pk = pickUnit(px, py, ci.rng, atk, false, true, 6, false);           // 顺手补刀
                if (pk.id >= 0) cmd.act = to_string(pk.id);
                else {
                    Sk sk = trySkill(h, k, px, py, true);
                    if (sk.used) { cmd.act = "Q"; cmd.tgt = sk.tgt; }
                }
            }
            out[k] = cmd; commitCmd(k, cmd);
            continue;
        }

        // ---- 推进目标 ----
        const int lane = (k == 0) ? 0 : (k == 2 ? 2 : 1);
        Tower* goalT = nullptr;
        bool goalCrystal = false;
        if (k == 1) {
            // 刺客：清完野随等级游走支援
            vector<Camp*> ownCamps;
            for (auto& c : g.camps)
                if (c.hp > 0 && manh(c.x, c.y, myNx.x, myNx.y) < manh(c.x, c.y, foeNx.x, foeNx.y))
                    ownCamps.push_back(&c);
            Camp* tc = nullptr;
            if (!ownCamps.empty()) {
                int bd = INT_MAX;
                for (auto* c : ownCamps) { int d = manh(x, y, c->x, c->y); if (d < bd) { bd = d; tc = c; } }
            }
            bool farm = false;
            if (tc) {
                if (h.level <= 2) farm = true;
                else if (h.level <= 5 && r >= 0.88 && nehD >= 7) farm = true;
            }
            if (farm && tc && manh(x, y, tc->x, tc->y) == 1) {
                if (nehD <= 2) farm = false;                     // 有人贴脸就别傻打野
                else { cmd.act = to_string(tc->id); out[k] = cmd; commitCmd(k, cmd); continue; }
            }
            if (farm && tc) {
                const vector<int>& cf = fieldTo(tc->x, tc->y);
                cmd.mv = advanceGuarded(x, y, cf);
                if (h.cd == 0 && nehD > 2 && cmd.mv != "S" && manh(x, y, tc->x, tc->y) >= 3 && dashOk(x, y, cmd.mv))
                    cmd.act = "Q";                               // 赶路用突袭
                out[k] = cmd; commitCmd(k, cmd);
                continue;
            }
            // gank：附近有残血英雄就上
            Hero* gank = nullptr;
            if (r >= 0.65) {
                for (auto* o : foeH) {
                    double orr = (double)o->hp / o->maxhp;
                    if (orr > 0.65 || manh(x, y, o->x, o->y) > 11) continue;
                    if (exposed(o->x, o->y) && r < 0.9) continue;   // 别往塔下追
                    if (!gank || orr < (double)gank->hp / gank->maxhp - 1e-9) gank = o;
                }
            }
            if (gank) {
                const vector<int>& gf2 = fieldTo(gank->x, gank->y);
                Pick pk = pickUnit(x, y, ci.rng, atk, true, false, 6, true);
                if (pk.id >= 0) {
                    cmd.act = to_string(pk.id);
                    if (pk.d == ci.rng + 1) {
                        string mv = greedyStep(x, y, pk.tx, pk.ty);
                        int px, py; applyMv(x, y, mv, px, py);
                        if (mv != "S" && canHit(px, py, pk.tx, pk.ty, ci.rng) && !exposed(px, py)) cmd.mv = mv;
                    }
                } else {
                    int dg2 = manh(x, y, gank->x, gank->y);
                    cmd.mv = advanceGuarded(x, y, gf2);
                    if (h.cd == 0 && cmd.mv != "S" && ((dg2 >= 3 && dg2 <= 4) || dg2 >= 6) && dashOk(x, y, cmd.mv))
                        cmd.act = "Q";                           // 突袭拉近距离抓人
                }
                out[k] = cmd; commitCmd(k, cmd);
                continue;
            }
            // 游走：挑最值得推的敌塔（血少、有兵线/队友配合优先）
            Tower* rt = nullptr; long long rs = LLONG_MAX;
            for (auto* t : foeTw) {
                long long sc = (long long)t->hp * 100 + (long long)manh(x, y, t->x, t->y) * 10;
                for (auto* m : myMin) if (manh(m->x, m->y, t->x, t->y) <= 3) sc -= 500;
                for (int j = 0; j < 5; j++)
                    if (j != k && myH[j] && myH[j]->x >= 0 && manh(myH[j]->x, myH[j]->y, t->x, t->y) <= 4) sc -= 800;
                if (sc < rs) { rs = sc; rt = t; }
            }
            if (rt) goalT = rt; else goalCrystal = true;
        } else {
            for (auto* t : foeTw) if (t->lane == lane && t->tier == 1) { goalT = t; break; }   // 先外塔
            if (!goalT) for (auto* t : foeTw) if (t->lane == lane && t->tier == 0) { goalT = t; break; }
            if (!goalT) goalCrystal = true;
        }
        int gx, gy, gid;
        if (goalT) { gx = goalT->x; gy = goalT->y; gid = goalT->id; }
        else { gx = foeNx.x; gy = foeNx.y; gid = foeNx.id; goalCrystal = true; }

        // ---- 共用推进逻辑 ----
        const vector<int>& gf = fieldTo(gx, gy);
        int myG = gf[y * S + x]; if (myG < 0) myG = INT_MAX / 2;
        int dGoal = manh(x, y, gx, gy);

        Minion* frontM = nullptr;                 // 己方兵线前沿（本路离目标最近的小兵）
        for (auto* m : myMin) {
            if (k != 1 && m->lane != lane) continue;
            int d = gf[m->y * S + m->x];
            if (d >= 0 && (!frontM || d < gf[frontM->y * S + frontM->x])) frontM = m;
        }

        int alliesGoal = 0, foesGoal = 0;         // 目标附近的局部兵力
        for (auto& o : g.heroes) if (o.x >= 0 && manh(o.x, o.y, gx, gy) <= 6) (o.team == me ? alliesGoal : foesGoal)++;
        bool outmatched = foesGoal > alliesGoal;
        if (outmatched && myG <= 7) {             // 打不过就在目标附近后撤，边撤边打
            cmd.mv = stepField(x, y, homeField);
            int px, py; applyMv(x, y, cmd.mv, px, py);
            Pick pk = pickUnit(px, py, ci.rng, atk, false, true, 6, false);
            if (pk.id >= 0) cmd.act = to_string(pk.id);
            else {
                Sk sk = trySkill(h, k, px, py, true);
                if (sk.used) { cmd.act = "Q"; cmd.tgt = sk.tgt; }
            }
            out[k] = cmd; commitCmd(k, cmd);
            continue;
        }

        bool guardedCrystal = false;
        if (goalCrystal) for (auto* t : foeTw) if (manh(t->x, t->y, gx, gy) <= 3) guardedCrystal = true;
        bool needCover = !goalCrystal || guardedCrystal;      // 水晶不还手，塔必须有小兵扛
        bool cover = false;
        if (needCover) for (auto* m : myMin) if (manh(m->x, m->y, gx, gy) <= 2) { cover = true; break; }
        int structHp = goalT ? goalT->hp : foeNx.hp;
        int structAtk = goalT ? goalT->atk : 0;
        bool snipe = structHp <= atk && h.hp > structAtk + 15;               // 一下拆掉就冒险点掉
        bool diveOk = goalT && r >= 0.72 && structHp <= 2 * atk && nehD >= 6 && h.hp > 2 * structAtk + 10;
        bool canSiege = !needCover || cover || snipe || diveOk;
        bool crystalRush = goalCrystal && (foeNx.hp <= 3 * atk || foesGoal == 0);   // 水晶没人守就直奔

        bool structureAttacked = false;
        // 抢先手：点杀残血建筑 / 直奔水晶
        if (canSiege && (snipe || crystalRush)) {
            if (dGoal <= ci.rng && (!exposed(x, y) || snipe || crystalRush)) {
                cmd.act = to_string(gid); structureAttacked = true;
            } else if (dGoal == ci.rng + 1) {
                string mv = advanceGuarded(x, y, gf);
                int px, py; applyMv(x, y, mv, px, py);
                if (mv != "S" && canHit(px, py, gx, gy, ci.rng)) {
                    cmd.mv = mv; cmd.act = to_string(gid); structureAttacked = true;
                }
            }
        }
        if (!structureAttacked) {
            // 1) 免费收割：这一下能打死的（英雄>小兵>野怪）
            Pick pk = pickUnit(x, y, ci.rng, atk, true, true, 2, aggro);
            if (pk.id >= 0 && pk.d <= ci.rng) {
                cmd.act = to_string(pk.id);
            } else if (pk.id >= 0) {
                string mv = greedyStep(x, y, pk.tx, pk.ty);
                int px, py; applyMv(x, y, mv, px, py);
                if (mv != "S" && canHit(px, py, pk.tx, pk.ty, ci.rng) && !exposed(px, py)) {
                    cmd.mv = mv; cmd.act = to_string(pk.id);
                }
            }
            // 2) 有掩护且就位 → 打建筑（小兵扛塔时拆塔优先于消耗）
            if (cmd.act == "-" && canSiege) {
                if (dGoal <= ci.rng) {
                    if (!exposed(x, y)) { cmd.act = to_string(gid); structureAttacked = true; }
                } else if (dGoal == ci.rng + 1) {
                    string mv = advanceGuarded(x, y, gf);
                    int px, py; applyMv(x, y, mv, px, py);
                    if (mv != "S" && canHit(px, py, gx, gy, ci.rng)) {
                        cmd.mv = mv; cmd.act = to_string(gid); structureAttacked = true;
                    }
                }
            }
            // 3) 消耗：残血英雄/小兵/野怪
            if (cmd.act == "-") {
                Pick pk2 = pickUnit(x, y, ci.rng, atk, true, true, 6, aggro);
                if (pk2.id >= 0 && pk2.d <= ci.rng) {
                    cmd.act = to_string(pk2.id);
                } else if (pk2.id >= 0) {
                    string mv = greedyStep(x, y, pk2.tx, pk2.ty);
                    int px, py; applyMv(x, y, mv, px, py);
                    if (mv != "S" && canHit(px, py, pk2.tx, pk2.ty, ci.rng) && !exposed(px, py)) {
                        cmd.mv = mv; cmd.act = to_string(pk2.id);
                    }
                }
            }
        }
        if (cmd.act == "-") {                                     // 技能补输出/自疗/治疗
            Sk sk = trySkill(h, k, x, y, false);
            if (sk.used) { cmd.act = "Q"; cmd.tgt = sk.tgt; }
        }

        // ---- 移动：跟己方兵线一起上，别单独冲塔 ----
        if (cmd.mv == "S") {
            bool fighting = (cmd.act != "-" && cmd.act != "Q");
            if (!fighting) {
                if (exposed(x, y) && r < 0.85) {
                    cmd.mv = stepField(x, y, homeField);          // 站在塔枪口下先退一步
                } else if (frontM) {
                    if (manh(x, y, frontM->x, frontM->y) > 2) {   // 掉队了：贴回兵线前沿
                        const vector<int>& ff = fieldTo(frontM->x, frontM->y);
                        string mv = advanceGuarded(x, y, ff);
                        if (mv != "S") cmd.mv = mv;
                    } else {
                        cmd.mv = advanceGuarded(x, y, gf);        // 与兵线同行往前顶
                    }
                } else {
                    // 本路没有己方兵：退守最深的存活己方塔（或基地），等下一波
                    Tower* anchor = nullptr;
                    for (auto* t : myTw) {
                        if (k != 1 && t->lane != lane) continue;
                        if (!anchor) anchor = t;
                        else if (k == 1) {
                            if (distTo(x, y, t->x, t->y) < distTo(x, y, anchor->x, anchor->y)) anchor = t;
                        } else if (t->tier > anchor->tier) anchor = t;
                    }
                    if (anchor) {
                        if (manh(x, y, anchor->x, anchor->y) > 4) {
                            const vector<int>& af = fieldTo(anchor->x, anchor->y);
                            cmd.mv = advanceGuarded(x, y, af);
                        }
                    } else if (myG > 3) {
                        const vector<int>& nf = fieldTo(myNx.x, myNx.y);
                        cmd.mv = advanceGuarded(x, y, nf);
                    }
                }
            }
        }
        // 辅助贴身射手
        if (k == 4 && cmd.mv == "S" && cmd.act == "-") {
            Hero* adc = myH[3];
            if (adc && adc->x >= 0 && manh(x, y, adc->x, adc->y) > 2) {
                const vector<int>& af = fieldTo(adc->x, adc->y);
                string mv = stepField(x, y, af);
                int px, py; applyMv(x, y, mv, px, py);
                if (!exposed(px, py)) cmd.mv = mv;
            }
        }
        // 刺客赶路时用突袭提速
        if (k == 1 && cmd.act == "-" && cmd.mv != "S" && h.cd == 0 && nehD > 2 && myG >= 3 && dashOk(x, y, cmd.mv))
            cmd.act = "Q";
        out[k] = cmd; commitCmd(k, cmd);
    }

    // ---- 输出安全检查：法师/射手的 Q 必须带目标，其他职业 Q 不带 ----
    for (int k = 0; k < 5; k++) {
        if (out[k].act != "Q") continue;
        if (k == 2 || k == 3) {
            if (out[k].tgt < 0) out[k].act = "-";
        } else {
            out[k].tgt = -1;
        }
    }

    // ---- 更新跨回合血量记忆 ----
    for (auto& t : g.towers) P.twHp[t.id] = t.hp;
    P.nxHp[0] = g.nexuses[0].hp;
    P.nxHp[1] = g.nexuses[1].hp;
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
        cout << endl;
    }
}
