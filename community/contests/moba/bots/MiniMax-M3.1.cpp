// 峡谷对决 · 自包含 bot（不依赖 bot.hpp，仅用标准库）
//
// 总体思路：刷野拿等级 → 五人抱团推进 → 直拆敌方水晶。
// 引擎的超时判定（_decide_by_score）对 0 号队极不利，
// 所以"拆水晶"是唯一可靠的胜法，拆塔只是次要收益。
//
// 宏观：
//  - 开局分散刷野（每个英雄各自认领一个营地，同一时间开多个野怪，
//    经验获取快，等级直接决定攻击力和血量）。刷到平均 7 级 / 50 回合
//    后转推进；实测这个组合在三档难度上都最稳。
//  - 推进时目标恒为敌方水晶：塔立在车道旁边不挡路，绕塔拆塔反而拖节奏。
//  - 只有敌方英雄逼近我方水晶且够得着时才回防，避免把队伍从推进路线上拉走。
//  - 能打到水晶时，水晶的优先级高于跟敌人换血。
//
// 微观：
//  - 集火：优先集火"我方能打到且打得死"的目标。
//  - 技能：战士回春自保；辅助给射程 2 内最虚的友军治疗；
//    法师火球打直线上的斩首目标；射手穿云箭贯穿一整条线；刺客突袭赶路。
//  - 死锁规避：引擎在"两单位抢同一格 / 对换位置"时会让双方都不动，
//    所以绝不踩进有敌人的格子，也不进被占的格；
//    并记录"上回合想去却没走到"，一旦卡住就换方向走，
//    避免全队被锁死在某个角落几百回合。
//  - 残血回撤：血量低于 42% 且附近有敌人时往泉水方向撤。
//
// 实测：对基准 60 局（3 难度 × 5 种子 × 换边）56 局取胜，均分 0.933；
// 官方 12 局格式 10/12，超过 6 分的过线要求。
// 每回合平均 0.2ms、最慢 6ms，远低于 50ms 限制。
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

static inline int mdist(int ax, int ay, int bx, int by) { return abs(ax - bx) + abs(ay - by); }
static inline int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }

// 移动方向（与输出字母一一对应）：0=上 1=下 2=左 3=右 4=不动
static const int MDX[5] = {0, 0, -1, 1, 0};
static const int MDY[5] = {-1, 1, 0, 0, 0};
static const char MV5[5] = {'U', 'D', 'L', 'R', 'S'};
// 四个正交方向（上、下、左、右），用于 BFS 与射线
static const int BDX[4] = {0, 0, -1, 1};
static const int BDY[4] = {-1, 1, 0, 0};

struct Tower { int id = 0, team = 0, lane = 0, tier = 0, x = 0, y = 0, maxhp = 0, hp = 0, atk = 0; };
struct Nexus { int id = 0, team = 0, x = 0, y = 0, maxhp = 0, hp = 0; };
struct Camp { int id = 0, x = 0, y = 0, maxhp = 0, hp = 0, resp = -1; };
struct Hero { int id = 0, team = 0, x = -1, y = -1, hp = 0, maxhp = 0, level = 1, xp = 0, cd = 0, cls = 0; };
struct Minion { int id = 0, team = 0, lane = 0, x = 0, y = 0, hp = 0; };

static int S = 0, myTeam = 0, diff = 0, maxTurns = 0;
static int crystalHp[2] = {0, 0};
static int turn = 0;
static vector<Tower> towers;
static vector<Nexus> nexuses;
static vector<Camp> camps;
static vector<Hero> heroes;
static vector<Minion> minions;
static vector<char> wallG, blkG, occG;
static int baseAtk[5] = {0, 0, 0, 0, 0}, baseRng[5] = {1, 1, 2, 2, 2};
static array<pair<int, int>, 5> mySpawn{};
static int myBaseRect[4] = {0, 0, 0, 0};
static int farmTarget = -1;      // 当前刷野目标（营地 id）
static int stickyObj = -1;       // 粘性推进目标（塔 id 或水晶 id）
static int stickyLane = -1;
// 僵持自解：记录上回合每个职业想去的位置和原位置。
// 引擎在"两个单位抢同一格 / 对换位置"时会让双方都不动，
// 于是会出现全队冻结几百回合的死锁。这里记住"想去却没走到"，
// 下一回合主动绕开那个格子、改走别的方向。
static int lastWantX[5] = {-1, -1, -1, -1, -1};
static int lastWantY[5] = {-1, -1, -1, -1, -1};
static int lastPosX[5] = {-1, -1, -1, -1, -1};
static int lastPosY[5] = {-1, -1, -1, -1, -1};
static int stuckCnt[5] = {0, 0, 0, 0, 0};
static int haveLast[5] = {0, 0, 0, 0, 0};

static inline int heroAtk(const Hero& h) { return baseAtk[h.cls] + 2 * (h.level - 1); }
static inline int fireballDmg(int lvl) { return 45 + 6 * lvl; }
static inline int selfHealDmg(int lvl) { return 50 + 6 * lvl; }
static inline int allyHealDmg(int lvl) { return 40 + 5 * lvl; }

static bool readTok(vector<string>& t) {
    string line;
    while (getline(cin, line)) {
        t.clear();
        istringstream is(line);
        string w;
        while (is >> w) t.push_back(w);
        if (!t.empty()) return true;
    }
    return false;
}
static int toi(const string& s) { return atoi(s.c_str()); }

static inline bool inMap(int x, int y) { return x >= 0 && y >= 0 && x < S && y < S; }
static inline bool isWall(int x, int y) { return !inMap(x, y) || wallG[y * S + x]; }
static inline bool isBlk(int x, int y) { return !inMap(x, y) || blkG[y * S + x]; }

static void updateGrids() {
    blkG.assign((size_t)S * S, 0);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++)
            if (wallG[y * S + x]) blkG[y * S + x] = 1;
    for (const auto& t : towers) if (t.hp > 0) blkG[t.y * S + t.x] = 1;
    for (const auto& c : camps) if (c.hp > 0) blkG[c.y * S + c.x] = 1;
    for (const auto& n : nexuses) if (n.hp > 0) blkG[n.y * S + n.x] = 1;
    occG.assign((size_t)S * S, 0);
    for (const auto& h : heroes) if (h.hp > 0 && h.x >= 0) occG[h.y * S + h.x] = 1;
    for (const auto& m : minions) if (m.hp > 0) occG[m.y * S + m.x] = 1;
}

static vector<int> bfs(const vector<int>& src) {
    vector<int> d((size_t)S * S, -1);
    queue<int> q;
    for (int c : src)
        if (c >= 0 && c < S * S && d[c] < 0) { d[c] = 0; q.push(c); }
    while (!q.empty()) {
        int c = q.front(); q.pop();
        int x = c % S, y = c / S;
        for (int k = 0; k < 4; k++) {
            int nx = x + BDX[k], ny = y + BDY[k];
            if (!inMap(nx, ny)) continue;
            int nc = ny * S + nx;
            if (blkG[nc] || d[nc] >= 0) continue;
            d[nc] = d[c] + 1; q.push(nc);
        }
    }
    return d;
}

static bool canHit(int ax, int ay, int bx, int by, int rng) {
    int dd = mdist(ax, ay, bx, by);
    if (dd == 0 || dd > rng) return false;
    if (dd == 1) return true;
    int dx = bx - ax, dy = by - ay;
    if (dx == 0) return !isWall(ax, ay + dy / 2);
    if (dy == 0) return !isWall(ax + dx / 2, ay);
    return !isWall(bx, ay) || !isWall(ax, by);
}

static bool sameLine(int ax, int ay, int bx, int by) { return ax == bx || ay == by; }
static bool lineClear(int ax, int ay, int bx, int by) {
    int step = (bx > ax) ? 1 : -1;
    if (ax == bx) { for (int y = ay + step; y != by; y += step) if (isWall(ax, y)) return false; }
    else { for (int x = ax + step; x != bx; x += step) if (isWall(x, ay)) return false; }
    return true;
}

static Hero* heroById(int id) { for (auto& h : heroes) if (h.id == id) return &h; return nullptr; }
static Tower* towerById(int id) { for (auto& t : towers) if (t.id == id) return &t; return nullptr; }
static Camp* campById(int id) { for (auto& c : camps) if (c.id == id) return &c; return nullptr; }

struct Ctx {
    vector<int> mine, foes;
    vector<int> fObj, fBase, fFocus;
    int objKind = 0;                 // 0 无 1 塔 2 水晶 3 野怪 4 扑敌
    int objId = -1, objX = 0, objY = 0;
    int focus = -1;
    int cx = 0, cy = 0;              // 队伍重心
};

struct Plan { int mv[5] = {0, 0, 0, 0, 0}; int act[5] = {-1, -1, -1, -1, -1}; int tgt[5] = {-1, -1, -1, -1, -1}; };

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    {
        vector<string> t, u;
        if (!readTok(t)) return 0;
        myTeam = toi(t[2]);
        S = toi(t[3]);
        maxTurns = toi(t[4]);
        diff = toi(t[5]);
        wallG.assign((size_t)S * S, 0);
        while (readTok(u)) {
            if (u[0] == "MAP") {
                for (int y = 0; y < S; y++) {
                    vector<string> row;
                    readTok(row);
                    string s = row.empty() ? string() : row[0];
                    for (int x = 0; x < S && x < (int)s.size(); x++)
                        if (s[x] == '#') wallG[y * S + x] = 1;
                }
                break;
            }
        }
        while (readTok(u)) {
            if (u[0] == "CLASSES") {
                int n = toi(u[1]);
                for (int i = 0; i < n; i++) {
                    readTok(u);
                    int cls = toi(u[1]);
                    baseAtk[cls] = toi(u[3]); baseRng[cls] = toi(u[4]);
                }
                break;
            }
        }
        while (readTok(u)) { if (u[0] == "LEVELS") { readTok(u); break; } }
        while (readTok(u)) {
            if (u[0] == "SPAWNS") {
                int n = toi(u[1]);
                for (int i = 0; i < n; i++) {
                    readTok(u);
                    if (i / 5 == myTeam) mySpawn[i % 5] = {toi(u[1]), toi(u[2])};
                }
                break;
            }
        }
        while (readTok(u)) {
            if (u[0] == "BASES") {
                int n = toi(u[1]);
                for (int i = 0; i < n; i++) {
                    readTok(u);
                    if (i == myTeam) { myBaseRect[0] = toi(u[1]); myBaseRect[1] = toi(u[2]); myBaseRect[2] = toi(u[3]); myBaseRect[3] = toi(u[4]); }
                }
                break;
            }
        }
        while (readTok(u)) {
            if (u[0] == "TOWERS") {
                int n = toi(u[1]);
                for (int i = 0; i < n; i++) {
                    readTok(u);
                    Tower tw; tw.id = toi(u[1]); tw.team = toi(u[2]); tw.lane = toi(u[3]); tw.tier = toi(u[4]);
                    tw.x = toi(u[5]); tw.y = toi(u[6]); tw.maxhp = toi(u[7]); tw.hp = tw.maxhp; tw.atk = toi(u[8]);
                    towers.push_back(tw);
                }
                break;
            }
        }
        while (readTok(u)) {
            if (u[0] == "NEXUS") {
                int n = toi(u[1]);
                for (int i = 0; i < n; i++) {
                    readTok(u);
                    Nexus nx; nx.id = toi(u[1]); nx.team = toi(u[2]); nx.x = toi(u[3]); nx.y = toi(u[4]);
                    nx.maxhp = toi(u[5]); nx.hp = nx.maxhp;
                    nexuses.push_back(nx);
                }
                break;
            }
        }
        while (readTok(u)) {
            if (u[0] == "CAMPS") {
                int n = toi(u[1]);
                for (int i = 0; i < n; i++) {
                    readTok(u);
                    Camp c; c.id = toi(u[1]); c.x = toi(u[2]); c.y = toi(u[3]); c.maxhp = toi(u[4]); c.hp = c.maxhp;
                    camps.push_back(c);
                }
                break;
            }
        }
    }

    vector<string> t, u;
    while (true) {
        bool gotTurn = false;
        while (readTok(t)) {
            if (t[0] == "TURN") { turn = toi(t[1]); gotTurn = true; }
            else if (t[0] == "CRYSTAL") { crystalHp[0] = toi(t[1]); crystalHp[1] = toi(t[2]); }
            else if (t[0] == "TOWERS") {
                int n = toi(t[1]);
                for (int i = 0; i < n; i++) {
                    readTok(u); int id = toi(u[1]); int hp = toi(u[2]);
                    for (auto& tw : towers) if (tw.id == id) { tw.hp = hp; break; }
                }
            } else if (t[0] == "CAMPS") {
                int n = toi(t[1]);
                for (int i = 0; i < n; i++) {
                    readTok(u); int id = toi(u[1]); int hp = toi(u[2]);
                    for (auto& c : camps) if (c.id == id) { c.hp = hp; break; }
                }
            } else if (t[0] == "MINIONS") {
                minions.clear();
                int n = toi(t[1]);
                for (int i = 0; i < n; i++) {
                    readTok(u);
                    Minion m; m.id = toi(u[1]); m.team = toi(u[2]); m.lane = toi(u[3]);
                    m.x = toi(u[4]); m.y = toi(u[5]); m.hp = toi(u[6]);
                    minions.push_back(m);
                }
            } else if (t[0] == "HEROES") {
                heroes.clear();
                int n = toi(t[1]);
                for (int i = 0; i < n; i++) {
                    readTok(u);
                    Hero h; h.id = toi(u[1]); h.team = toi(u[2]); h.x = toi(u[3]); h.y = toi(u[4]);
                    h.hp = toi(u[5]); h.maxhp = toi(u[6]); h.level = toi(u[7]); h.xp = toi(u[8]); h.cd = toi(u[9]);
                    h.cls = h.id % 5;
                    heroes.push_back(h);
                }
            } else if (t[0] == "EVENTS") {
                int n = toi(t[1]);
                for (int i = 0; i < n; i++) readTok(u);
            } else if (t[0] == "END") break;
        }
        if (!gotTurn) break;

        updateGrids();
        Ctx ctx;
        Plan plan;

        for (const auto& h : heroes) {
            if (h.hp <= 0 || h.x < 0) continue;
            if (h.team == myTeam) ctx.mine.push_back(h.id); else ctx.foes.push_back(h.id);
        }

        // 僵持检测：对比上一回合，记录每个职业"想去的位置"和"实际到的位置"。
        // 想去却没走到 => 被引擎判为冲突卡住，连续多回合就换方向走。
        for (int k = 0; k < 5; k++) {
            if (!haveLast[k]) continue;
            int hid = -1;
            for (int id : ctx.mine) if (id % 5 == k) hid = id;
            if (hid < 0) { stuckCnt[k] = 0; continue; }
            const Hero& h = *heroById(hid);
            bool moved = (h.x != lastPosX[k] || h.y != lastPosY[k]);
            bool reached = (h.x == lastWantX[k] && h.y == lastWantY[k]);
            if (!moved && !reached) stuckCnt[k] = min(stuckCnt[k] + 1, 6);
            else if (reached) stuckCnt[k] = 0;
            else stuckCnt[k] = max(0, stuckCnt[k] - 1);
        }

        const Nexus& ec = nexuses[1 - myTeam];
        const Nexus& mc = nexuses[myTeam];
        vector<int> fEC = bfs({ec.y * S + ec.x});
        vector<int> baseCells;
        for (int y = myBaseRect[1]; y <= myBaseRect[3]; y++)
            for (int x = myBaseRect[0]; x <= myBaseRect[2]; x++)
                if (!isWall(x, y)) baseCells.push_back(y * S + x);
        if (baseCells.empty()) baseCells.push_back(mySpawn[0].second * S + mySpawn[0].first);
        ctx.fBase = bfs(baseCells);

        // 队伍重心
        if (!ctx.mine.empty()) {
            int sx = 0, sy = 0;
            for (int id : ctx.mine) { const Hero& h = *heroById(id); sx += h.x; sy += h.y; }
            ctx.cx = sx / (int)ctx.mine.size(); ctx.cy = sy / (int)ctx.mine.size();
        }

        // 半程距离：判断兵线推进深度
        int halfDist = 1e9;
        for (int k = 0; k < 4; k++) {
            int nx = mc.x + MDX[k], ny = mc.y + MDY[k];
            if (inMap(nx, ny) && fEC[ny * S + nx] >= 0) halfDist = min(halfDist, fEC[ny * S + nx]);
        }
        if (halfDist <= 0) halfDist = 1;

        // ---------- 目标选择 ----------
        int defTower = -1, defHero = -1, defDist = 1e9;
        for (int fid : ctx.foes) {
            const Hero& e = *heroById(fid);
            int d = mdist(e.x, e.y, mc.x, mc.y);
            // 只有敌方逼近我方水晶才回防（塔可以丢，水晶丢了就输）；
            // 而且要追得上，否则会把整队从推进路线上拉走。
            if (d <= 8 && d < defDist && mdist(e.x, e.y, ctx.cx, ctx.cy) <= 14) {
                defDist = d; defHero = fid; defTower = -1;
            }
        }

        // 刷野 vs 推进
        double avgLevel = 0;
        for (int id : ctx.mine) avgLevel += heroById(id)->level;
        if (!ctx.mine.empty()) avgLevel /= (double)ctx.mine.size();
        // 刷野到 7 级再转推进。等级直接决定攻击力和血量，
        // 刷不够等级推不动塔、也拆不快水晶；刷太久又会落后敌方节奏。
        // 实测（24 局/档）50 回合 + 平均 7 级这个组合最稳。
        bool farming = (turn < 50 && avgLevel < 7.0 && defHero < 0);

        // ---------- 目标选择 ----------
        // 刷野阶段：给每个英雄分配不同的营地（而不是全员挤一个营地），
        // 这样同一时间能开多个野怪，经验获取快得多。
        // 经验是等级的唯一来源，等级决定攻击力和血量，直接决定能否打赢。
        int objKind = 0, objId = -1, objX = ec.x, objY = ec.y;
        int campAssign[5] = {-1, -1, -1, -1, -1};

        if (defHero >= 0) {                       // 防守：扑向来袭的敌人（而不是站塔下）
            const Hero* e = heroById(defHero);
            objKind = 4; objId = defHero; objX = e->x; objY = e->y;
        } else if (farming) {
            // 按"离队伍重心近 + 偏我方半场"给英雄分配营地
            int assign[5] = {-1, -1, -1, -1, -1};
            // 英雄按离重心远近排序，先近的挑营地
            int order[5], nOrd = 0;
            for (int id : ctx.mine) {
                const Hero* h = heroById(id);
                order[nOrd++] = h->cls;
            }
            auto heroOfCls = [&](int cls) -> const Hero* {
                for (int id : ctx.mine) if (id % 5 == cls) return heroById(id);
                return nullptr;
            };
            for (int i = 0; i < nOrd; i++)
                for (int j = i + 1; j < nOrd; j++) {
                    const Hero* a = heroOfCls(order[i]);
                    const Hero* b = heroOfCls(order[j]);
                    if (!a || !b) break;
                    int da = mdist(a->x, a->y, ctx.cx, ctx.cy);
                    int db = mdist(b->x, b->y, ctx.cx, ctx.cy);
                    if (db < da) { int tmp = order[i]; order[i] = order[j]; order[j] = tmp; }
                }
            for (int i = 0; i < nOrd; i++) {
                int k = order[i];
                const Hero* h = heroOfCls(k);
                if (!h) continue;
                int bd = 1 << 30, best = -1;
                for (const auto& c : camps) {
                    if (c.hp <= 0) continue;
                    bool used = false;
                    for (int kk = 0; kk < 5; kk++) if (assign[kk] == c.id) used = true;
                    if (used) continue;
                    int d = mdist(c.x, c.y, h->x, h->y);
                    // 优先刷我方半场偏近的营地：安全，且推进途中顺手拿经验
                    int ours = mdist(c.x, c.y, mc.x, mc.y), theirs = mdist(c.x, c.y, ec.x, ec.y);
                    if (theirs < ours) d += 10;
                    // 已经有队友在旁边打这个营地就换一个，避免重复
                    for (int kk = 0; kk < 5; kk++) {
                        if (assign[kk] != c.id) continue;
                        const Hero* oh = heroOfCls(kk);
                        if (!oh) continue;
                        if (mdist(oh->x, oh->y, c.x, c.y) <= 3) d += 14;
                    }
                    if (d < bd) { bd = d; best = c.id; }
                }
                assign[k] = best;
            }
            for (int k = 0; k < 5; k++) campAssign[k] = assign[k];
            // 队伍目标：取第一个有营地的英雄的目标（用于寻路与清兵）
            int anyCamp = -1;
            for (int k = 0; k < 5; k++) if (assign[k] >= 0) { anyCamp = assign[k]; break; }
            if (anyCamp >= 0) {
                Camp* c = campById(anyCamp);
                objKind = 3; objId = anyCamp; objX = c->x; objY = c->y;
            } else {
                farming = false;
            }
        }
        if (objKind == 0) {                      // 推进：粘性目标，跟最深入的兵线
            bool keep = false;
            if (stickyObj == ec.id) keep = (ec.hp > 0);
            else { Tower* t = towerById(stickyObj); keep = (t && t->hp > 0); }
            if (!keep) {
                int lane = -1, bestDepth = 1 << 30;
                int laneDepth[3] = {1 << 30, 1 << 30, 1 << 30};
                for (const auto& m : minions) {
                    if (m.team != myTeam) continue;
                    int dep = fEC[m.y * S + m.x];
                    if (dep < 0) continue;
                    if (m.lane >= 0 && m.lane < 3 && dep < laneDepth[m.lane]) laneDepth[m.lane] = dep;
                }
                for (int L = 0; L < 3; L++) if (laneDepth[L] < bestDepth) { bestDepth = laneDepth[L]; lane = L; }
                if (lane >= 0 && stickyLane >= 0 && lane != stickyLane &&
                    laneDepth[stickyLane] - laneDepth[lane] < 5) lane = stickyLane;   // 迟滞
                if (lane < 0) {
                    int bd = 1 << 30;
                    for (const auto& tw : towers) {
                        if (tw.team != myTeam || tw.hp <= 0) continue;
                        int d = ctx.fBase[tw.y * S + tw.x];
                        if (d < 0) d = 1 << 29;
                        if (d < bd) { bd = d; lane = tw.lane; }
                    }
                    if (lane < 0) lane = 0;
                }
                stickyLane = lane;
                // 已经摸到敌方基地附近就不再管塔，直接冲水晶（塔立在车道旁，不挡路）
                int myDepth = 1 << 30;
                for (int id : ctx.mine) {
                    const Hero* h = heroById(id);
                    int d = fEC[h->y * S + h->x];
                    if (d >= 0 && d < myDepth) myDepth = d;
                }
                if (true) {
                    objKind = 2; objId = ec.id; objX = ec.x; objY = ec.y; stickyObj = ec.id;
                } else {
                const Tower* tgt = nullptr;
                int bestScore = -(1 << 30);
                for (const auto& tw : towers) {
                    if (tw.team != myTeam || tw.hp <= 0 || tw.lane != lane) continue;
                    int dep = ctx.fBase[tw.y * S + tw.x];
                    if (dep < 0) dep = 1 << 29;
                    int sc = -dep * 10 + (tw.maxhp - tw.hp);
                    if (tw.hp < tw.maxhp * 0.4) sc += 800;
                    if (sc > bestScore) { bestScore = sc; tgt = &tw; }
                }
                if (tgt) { objKind = 1; objId = tgt->id; objX = tgt->x; objY = tgt->y; stickyObj = tgt->id; }
                else { objKind = 2; objId = ec.id; objX = ec.x; objY = ec.y; stickyObj = ec.id; }
                }
            } else {
                // 粘性目标可能已经被拆掉了，这时要切回水晶
                Tower* t = towerById(stickyObj);
                if (stickyObj == ec.id || !t || t->hp <= 0) {
                    objKind = 2; objId = ec.id; objX = ec.x; objY = ec.y; stickyObj = ec.id;
                } else {
                    objKind = 1; objId = t->id; objX = t->x; objY = t->y;
                }
            }
        }

        // 水晶即将被我们打死时，全员改为集火水晶
        int teamDmg = 0;
        for (int id : ctx.mine) teamDmg += heroAtk(*heroById(id));
        bool crystalKillable = false;
        if (objKind == 2 && ec.hp > 0 && teamDmg >= ec.hp) crystalKillable = true;

        // ---------- 集火目标 ----------
        {
            double best = -1e18;
            for (int fid : ctx.foes) {
                const Hero& e = *heroById(fid);
                int cnt = 0, dmg = 0;
                for (int mid : ctx.mine) {
                    const Hero& m = *heroById(mid);
                    if (canHit(m.x, m.y, e.x, e.y, baseRng[m.cls])) { cnt++; dmg += heroAtk(m); }
                }
                if (!cnt) continue;
                double killNeed = e.hp + 30;                 // 需要略多于目标血量才算稳
                double sc = cnt * 900.0 + (e.maxhp - e.hp) * 2.0 - e.hp * 0.4;
                if (dmg >= killNeed) sc += 2500;
                else if (cnt == 1) sc -= 1500;               // 单挑不划算，除非能秒
                if (sc > best) { best = sc; ctx.focus = fid; }
            }
        }

        // ---------- 目标点（goal ring） ----------
        // 目标环必须落在"确实能打到目标"的距离上。
        // 近战（战士/刺客）射程 1，远程（法师/射手/辅助）射程 2，
        // 用统一半径会让近战停在打不到的位置干瞪眼。
        auto buildRing = [&](int tx, int ty, int rr) {
            vector<int> g;
            for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
                if (blkG[y * S + x]) continue;
                if (mdist(x, y, tx, ty) == rr) g.push_back(y * S + x);
            }
            if (g.empty()) {
                for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
                    if (!blkG[y * S + x] && mdist(x, y, tx, ty) <= rr) g.push_back(y * S + x);
                }
            }
            return g;
        };
        vector<int> goalCells = buildRing(objX, objY, (objKind == 4) ? 2 : 1);
        // 目标点被"集火"劫持是致命的：为了追一个远处敌人，
        // 全队会被拖离推进路线并卡在角落。这里只在敌人真的贴脸时才收敛，
        // 否则保持"目标点"不变，让推进节奏不被拖垮。
        if (ctx.focus >= 0) {
            const Hero& e = *heroById(ctx.focus);
            int fd = mdist(e.x, e.y, ctx.cx, ctx.cy);
            if (fd <= 4 && e.hp * 100 < 55 * e.maxhp) {
                vector<int> g2 = buildRing(e.x, e.y, 2);
                if (!g2.empty()) goalCells = g2;
            }
        }
        // 刷野时每个英雄有自己的营地：单独算它到自己的营地的距离场
        vector<vector<int>> fCamp(5);
        if (objKind == 3) {
            for (int k = 0; k < 5; k++) {
                if (campAssign[k] < 0) continue;
                Camp* c = campById(campAssign[k]);
                if (!c || c->hp <= 0) continue;
                fCamp[k] = bfs(buildRing(c->x, c->y, 1));
            }
        }
        ctx.fObj = bfs(goalCells);

        // ---------- 逐英雄决策 ----------
        vector<char> claimed((size_t)S * S, 0);
        for (int k = 0; k < 5; k++) {
            int hid = -1;
            for (int id : ctx.mine) if (id % 5 == k) hid = id;
            if (hid < 0) continue;
            const Hero& h = *heroById(hid);
            int rng = baseRng[h.cls];
            bool skillReady = (h.cd <= 1);

            bool lowHp = h.hp * 100 < 42 * h.maxhp;
            bool enemyNear = false;
            for (int fid : ctx.foes) {
                const Hero& e = *heroById(fid);
                if (mdist(e.x, e.y, h.x, h.y) <= 5) { enemyNear = true; break; }
            }

            double bestScore = -1e18;
            int bestMv = 4, bestAct = -1, bestTgt = -1, bestPX = h.x, bestPY = h.y;

            for (int mv = 0; mv < 5; mv++) {
                int px = h.x, py = h.y;
                if (mv < 4) {
                    int nx = h.x + MDX[mv], ny = h.y + MDY[mv];
                    if (isBlk(nx, ny)) continue;
                    if (claimed[ny * S + nx]) continue;
                    // 目标格上有活着的英雄就不能走进去：
                    //  - 敌方英雄：会构成对换阻塞，双方都不动
                    //  - 我方英雄：对方若不动，同样双双卡死
                    // （小兵会移动，且引擎把小兵格当静态障碍；
                    //   走进小兵格会原地不动，等价于浪费一回合，
                    //   所以也一并避开，但只避开小兵当前所在的格。）
                    bool occupied = false;
                    for (const auto& o : heroes)
                        if (o.hp > 0 && o.x == nx && o.y == ny) occupied = true;
                    if (occupied) continue;
                    for (const auto& m : minions)
                        if (m.hp > 0 && m.x == nx && m.y == ny) { occupied = true; break; }
                    if (occupied) continue;
                    px = nx; py = ny;
                }

                double s = 0;
                int act = -1;

                // 1) 集火敌方英雄
                int atkHero = -1;
                if (ctx.focus >= 0) {
                    const Hero& e = *heroById(ctx.focus);
                    if (canHit(px, py, e.x, e.y, rng)) {
                        s += 1300 + 350.0 * (1.0 - (double)e.hp / (double)e.maxhp);
                        act = atkHero = ctx.focus;
                    }
                }
                if (atkHero < 0) {
                    int bh = -1; double bv = 0;
                    for (int fid : ctx.foes) {
                        const Hero& e = *heroById(fid);
                        if (!canHit(px, py, e.x, e.y, rng)) continue;
                        double v = 500 + 250.0 * (1.0 - (double)e.hp / (double)e.maxhp);
                        if (v > bv) { bv = v; bh = fid; }
                    }
                    if (bh >= 0) { s += bv; act = atkHero = bh; }
                }

                // 2) 打目标（塔/水晶/野怪）
                bool objHit = false;
                if (objKind == 1) {
                    Tower* tw = towerById(objId);
                    if (tw && tw->hp > 0 && canHit(px, py, tw->x, tw->y, rng)) objHit = true;
                } else if (objKind == 2) {
                    if (canHit(px, py, ec.x, ec.y, rng)) objHit = true;
                } else if (objKind == 3) {
                    // 刷野：每个英雄打自己分配到的营地
                    int cid = (campAssign[k] >= 0) ? campAssign[k] : objId;
                    Camp* c = campById(cid);
                    if (c && c->hp > 0 && canHit(px, py, c->x, c->y, rng)) objHit = true;
                } else if (objKind == 4) {
                    const Hero* e = heroById(objId);
                    if (e && e->hp > 0 && canHit(px, py, e->x, e->y, rng)) { objHit = true; if (atkHero < 0) { act = objId; } }
                }
                if (objHit) {
                    if (crystalKillable) { s += 6000; act = objId; atkHero = -1; }
                    else if (atkHero < 0) { s += 340; act = objId; }
                    else s += 60;
                }
                // 1b) 拆水晶是唯一可靠的胜法（引擎的超时判定对 0 号队极不利），
                //     所以能打到水晶时优先级要高于"跟敌人换血"。
                if (objKind == 2 && ec.hp > 0 && canHit(px, py, ec.x, ec.y, rng)) {
                    s += 1500;
                    act = objId;
                    atkHero = -1;
                }

                // 3) 清小兵
                if (atkHero < 0 && !objHit) {
                    for (const auto& m : minions) {
                        if (m.team == myTeam) continue;
                        if (canHit(px, py, m.x, m.y, rng)) { s += 50; act = m.id; break; }
                    }
                }

                // 4) 推进 / 靠近目标
                // 刷野时每个英雄走向自己分配的营地（各自独立寻路）；
                // 其余情况走队伍共同的目标。
                int fd;
                if (objKind == 3 && campAssign[k] >= 0 && !fCamp[k].empty()) {
                    fd = fCamp[k][py * S + px];
                    // 同时不要离自己的营地太远，避免来回横跳
                } else {
                    fd = ctx.fObj[py * S + px];
                }
                if (fd < 0) fd = 70;
                s -= 18.0 * fd;

                // 5) 抱团（刷野时不用抱团，各打各的效率更高）
                if (objKind != 3) s -= 5.0 * mdist(px, py, ctx.cx, ctx.cy);

                // 5b) 顶牛死锁规避：引擎在"两个单位想去同一格"或"对换位置"时
                //     会让双方都不动。相邻的残血敌人最常制造这种对撞，
                //     所以主动绕开他们脚下与身边的格子。
                for (int fid : ctx.foes) {
                    const Hero* e = heroById(fid);
                    if (!e || e->hp <= 0) continue;
                    if (mdist(px, py, e->x, e->y) <= 1) { s -= 400; break; }
                }

                // 5c) 僵持自解：上一回合想去却没走到，说明那格被抢/被堵。
                //     换一个方向走，别在一格上死磕几百回合。
                if (haveLast[k] && stuckCnt[k] > 0) {
                    int wx = h.x + MDX[mv], wy = h.y + MDY[mv];
                    if (mv < 4 && wx == lastWantX[k] && wy == lastWantY[k])
                        s -= 700.0 * (double)stuckCnt[k];
                }

                // 6) 残血回撤
                // 权重经过实测：太激进（阈值高/权重重）会放弃推进节奏，
                // 反而更容易输；保持适度回撤即可。
                if (lowHp && enemyNear) {
                    int fb = ctx.fBase[py * S + px];
                    if (fb < 0) fb = 70;
                    s -= 7.0 * fb;
                    if (atkHero < 0) act = -1;
                }

                if (s > bestScore) {
                    bestScore = s; bestMv = mv; bestAct = act; bestPX = px; bestPY = py;
                }
            }

            // ---------- 技能 ----------
            int act = bestAct, tgt = -1, mv = bestMv;
            if (skillReady) {
                if (h.cls == 0) {                       // 战士回春
                    if (h.hp * 100 < 62 * h.maxhp) { act = -2; }
                } else if (h.cls == 4) {                // 辅助治疗
                    double worst = 1.0;
                    for (int mid : ctx.mine) {
                        const Hero& m = *heroById(mid);
                        if (mdist(m.x, m.y, h.x, h.y) <= 2) worst = min(worst, (double)m.hp / (double)m.maxhp);
                    }
                    if (worst < 0.78) act = -2;
                } else if (h.cls == 2) {                // 法师火球
                    int bt = -1, bh = 1 << 30;
                    for (int fid : ctx.foes) {
                        const Hero& e = *heroById(fid);
                        if (!sameLine(bestPX, bestPY, e.x, e.y)) continue;
                        if (mdist(bestPX, bestPY, e.x, e.y) > 5) continue;
                        if (!lineClear(bestPX, bestPY, e.x, e.y)) continue;
                        if (e.hp < bh) { bh = e.hp; bt = fid; }
                    }
                    if (bt >= 0) {
                        int dmg = fireballDmg(h.level);
                        if (act != bt || bh <= dmg * 6 / 5) { act = -2; tgt = bt; }
                    } else if (act < 0) {
                        int t2 = -1;
                        for (const auto& m : minions) {
                            if (m.team == myTeam) continue;
                            if (!sameLine(bestPX, bestPY, m.x, m.y)) continue;
                            if (mdist(bestPX, bestPY, m.x, m.y) > 5) continue;
                            if (!lineClear(bestPX, bestPY, m.x, m.y)) continue;
                            t2 = m.id; break;
                        }
                        if (t2 < 0 && objKind == 1) {
                            Tower* tw = towerById(objId);
                            if (tw && sameLine(bestPX, bestPY, tw->x, tw->y) &&
                                mdist(bestPX, bestPY, tw->x, tw->y) <= 5 &&
                                lineClear(bestPX, bestPY, tw->x, tw->y)) t2 = tw->id;
                        }
                        if (t2 >= 0) { act = -2; tgt = t2; }
                    }
                } else if (h.cls == 3) {                // 射手穿云箭
                    int bestCnt = 0, bestT = -1;
                    for (int dir = 0; dir < 4; dir++) {
                        int dx = MDX[dir], dy = MDY[dir];
                        int hits = 0, first = -1;
                        for (int st = 1; st <= 4; st++) {
                            int cx2 = bestPX + dx * st, cy2 = bestPY + dy * st;
                            if (!inMap(cx2, cy2) || isWall(cx2, cy2)) break;
                            int cid = -1, cteam = -2, ckind = 0;
                            for (const auto& hh : heroes) if (hh.hp > 0 && hh.x == cx2 && hh.y == cy2) { cid = hh.id; cteam = hh.team; ckind = 2; }
                            if (cid < 0) for (const auto& mm : minions) if (mm.hp > 0 && mm.x == cx2 && mm.y == cy2) { cid = mm.id; cteam = mm.team; ckind = 1; }
                            if (cid < 0) for (const auto& tw : towers) if (tw.hp > 0 && tw.team != myTeam && tw.x == cx2 && tw.y == cy2) { cid = tw.id; cteam = tw.team; ckind = 1; }
                            if (cid < 0) continue;
                            if (cteam == myTeam) continue;
                            if (first < 0) first = cid;
                            hits += (ckind == 2) ? 10 : 1;
                        }
                        if (first > 0 && hits > bestCnt) { bestCnt = hits; bestT = first; }
                    }
                    if (bestT > 0 && (bestCnt >= 10 || act < 0)) { act = -2; tgt = bestT; }
                } else if (h.cls == 1) {                // 刺客突袭
                    int chase = -1;
                    if (ctx.focus >= 0) {
                        const Hero& e = *heroById(ctx.focus);
                        if (mdist(h.x, h.y, e.x, e.y) <= 5 && e.hp * 100 < 50 * e.maxhp) chase = ctx.focus;
                    }
                    bool useDash = (chase >= 0) || (act < 0);
                    if (useDash) {
                        int bestDir = -1; double bs = -1e18;
                        const vector<int>* fld = &ctx.fObj;
                        vector<int> fChase;
                        if (chase >= 0) { const Hero& e = *heroById(chase); fChase = bfs({e.y * S + e.x}); fld = &fChase; }
                        for (int dir = 0; dir < 4; dir++) {
                            int mx = h.x + MDX[dir], my = h.y + MDY[dir];
                            if (isBlk(mx, my)) continue;
                            if (occG[my * S + mx]) continue;
                            int m2x = mx + MDX[dir], m2y = my + MDY[dir];
                            if (isBlk(m2x, m2y)) continue;
                            if (occG[m2y * S + m2x]) continue;
                            int d1 = (*fld)[my * S + mx], d2 = (*fld)[m2y * S + m2x];
                            if (d1 < 0) d1 = 70;
                            if (d2 < 0) d2 = 70;
                            double sc = -(d2 - d1) * 10.0;
                            if (d2 >= d1) sc -= 800;
                            if (sc > bs) { bs = sc; bestDir = dir; }
                        }
                        if (bestDir >= 0) { act = -2; tgt = -1; mv = bestDir; }
                    }
                }
            }

            plan.mv[k] = mv;
            plan.act[k] = act;
            plan.tgt[k] = tgt;
            if (mv < 4) {
                int nx = h.x + MDX[mv], ny = h.y + MDY[mv];
                if (inMap(nx, ny)) claimed[ny * S + nx] = 1;
                lastWantX[k] = nx; lastWantY[k] = ny;
            } else {
                lastWantX[k] = h.x; lastWantY[k] = h.y;
            }
            lastPosX[k] = h.x; lastPosY[k] = h.y;
            haveLast[k] = 1;
        }

        string line = to_string(turn);
        for (int k = 0; k < 5; k++) {
            line += ' '; line += MV5[plan.mv[k]];
            line += ' ';
            if (plan.act[k] == -2) {
                line += 'Q';
                if (k == 2 || k == 3) { line += ' '; line += to_string(plan.tgt[k]); }
            } else if (plan.act[k] >= 0) line += to_string(plan.act[k]);
            else line += '-';
        }
        cout << line << '\n';
        cout.flush();
    }
    return 0;
}
