// 峡谷对决 bot —— 自包含实现（C++20）
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

using namespace std;

static const int INF = 1000000000;

// ---------------- 全局状态 ----------------
static int S = 0;
static int MYT = 0;
static int TOTALTURNS = 0;
static int DIFF = 0;
static int WAVE_SIZE = 0, WAVE_GAP = 12, MINION_HP = 0, MINION_ATK = 0;
static int TOWER_RNG = 2, RESPAWN_T = 14, REGEN = 2, FOUNTAIN = 8, MINION_CAP = 6, AGGRO = 2;

struct ClsInfo { int hp, atk, rng, cd; string skill; };
static ClsInfo CLS[5];
static vector<string> MP;
static int SPAWN_PT[10][2];
static int BASE_RECT[2][4];

struct TowerInfo { int id, team, lane, tier, x, y, maxhp, atk, hp; };
static vector<TowerInfo> TW;
struct NexusInfo { int id, team, x, y, maxhp, hp; };
static NexusInfo NX[2];
struct CampInfo { int id, x, y, maxhp, atk, hp, resp; };
static vector<CampInfo> CP;
struct MinionInfo { int id, team, lane, x, y, hp, hatk = 0; };
static vector<MinionInfo> MIN;
struct HeroInfo { int id, team, x, y, hp, maxhp, lvl, xp, cd, resp; };
static vector<HeroInfo> HR;

static int CRYSTAL_HP[2] = {0, 0};
static int TURN = 0;
static bool inTurn = false;

// ---------------- 工具 ----------------
static inline int IDX(int x, int y) { return y * S + x; }
static inline int SLOT(int id) { return id % 5; }
static inline int dist(int ax, int ay, int bx, int by) { return abs(ax - bx) + abs(ay - by); }

static bool isWall(int x, int y) {
    if (x < 0 || y < 0 || x >= S || y >= S) return true;
    return MP[y][x] == '#';
}
static int heroAtk(const HeroInfo& h) { return CLS[SLOT(h.id)].atk + 2 * (h.lvl - 1); }
static inline bool aliveH(const HeroInfo& h) { return h.x >= 0; }

static bool canHit(int ax, int ay, int bx, int by, int rng) {
    int d = abs(ax - bx) + abs(ay - by);
    if (d == 0 || d > rng) return false;
    if (d == 1) return true;
    int dx = bx - ax, dy = by - ay;
    if (dx == 0) return !isWall(ax, ay + dy / 2);
    if (dy == 0) return !isWall(ax + dx / 2, ay);
    return !isWall(ax + dx, ay) || !isWall(ax, ay + dy);
}

static bool fireballOK(int ax, int ay, int bx, int by) {
    if (ax != bx && ay != by) return false;
    int d = abs(ax - bx) + abs(ay - by);
    if (d == 0 || d > 5) return false;
    if (ax == bx) {
        int st = (by > ay) ? 1 : -1;
        for (int y = ay + st; y != by; y += st) if (isWall(ax, y)) return false;
    } else {
        int st = (bx > ax) ? 1 : -1;
        for (int x = ax + st; x != bx; x += st) if (isWall(x, ay)) return false;
    }
    return true;
}

static int pierceCells(int ax, int ay, int dx, int dy, int out[4][2]) {
    int n = 0;
    for (int s = 1; s <= 4; s++) {
        int x = ax + dx * s, y = ay + dy * s;
        if (isWall(x, y)) break;
        out[n][0] = x; out[n][1] = y; n++;
    }
    return n;
}

// ---------------- 网格 ----------------
static vector<unsigned char> hardG, occG;
static vector<int> fieldGoal, fieldHome;
static vector<int> goalCellsBuf;

static void buildStatic() {
    hardG.assign((size_t)S * S, 0);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++)
            if (MP[y][x] == '#') hardG[IDX(x, y)] = 1;
    for (auto& t : TW) if (t.hp > 0) hardG[IDX(t.x, t.y)] = 1;
    for (auto& c : CP) if (c.hp > 0) hardG[IDX(c.x, c.y)] = 1;
    for (int i = 0; i < 2; i++) if (NX[i].hp > 0) hardG[IDX(NX[i].x, NX[i].y)] = 1;
}

static void buildOcc(int skipHero) {
    occG.assign((size_t)S * S, 0);
    for (auto& m : MIN) occG[IDX(m.x, m.y)] = 1;
    for (auto& h : HR) if (aliveH(h) && h.id != skipHero) occG[IDX(h.x, h.y)] = 1;
}

static void buildField(vector<int>& out, const vector<int>& goals) {
    out.assign((size_t)S * S, INF);
    typedef pair<int, int> PII;
    priority_queue<PII, vector<PII>, greater<PII>> pq;
    for (int g : goals) {
        if (g < 0 || g >= S * S) continue;
        if (hardG[g]) continue;
        if (out[g] != 0) { out[g] = 0; pq.push({0, g}); }
    }
    static const int dx[4] = {0, 0, -1, 1};
    static const int dy[4] = {-1, 1, 0, 0};
    while (!pq.empty()) {
        PII pr = pq.top(); pq.pop();
        int d = pr.first, id = pr.second;
        if (d != out[id]) continue;
        int x = id % S, y = id / S;
        for (int k = 0; k < 4; k++) {
            int nx = x + dx[k], ny = y + dy[k];
            if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
            int nid = ny * S + nx;
            if (hardG[nid]) continue;
            int w = 1;
            int sx = nid % S, sy = nid / S;
            int off = abs(sx + sy - (S - 1));
            if (off > 1) w += 9;
            if (d + w < out[nid]) { out[nid] = d + w; pq.push({d + w, nid}); }
        }
    }
}

static void aroundCells(int cx, int cy, vector<int>& out) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dy[4] = {-1, 1, 0, 0};
    for (int k = 0; k < 4; k++) {
        int nx = cx + dx[k], ny = cy + dy[k];
        if (nx < 0 || ny < 0 || nx >= S || ny >= S) continue;
        if (hardG[IDX(nx, ny)]) continue;
        out.push_back(IDX(nx, ny));
    }
}

// ---------------- 目标抽象 ----------------
enum { K_HERO = 0, K_MINION, K_TOWER, K_NEXUS, K_CAMP };
struct Tgt { int kind = -1, idx = -1; };

static bool tgtAlive(const Tgt& t) {
    if (t.kind < 0) return false;
    switch (t.kind) {
        case K_HERO: return aliveH(HR[t.idx]);
        case K_MINION: return MIN[t.idx].hp > 0;
        case K_TOWER: return TW[t.idx].hp > 0;
        case K_NEXUS: return NX[t.idx].hp > 0;
        case K_CAMP: return CP[t.idx].hp > 0;
    }
    return false;
}
static int tgtX(const Tgt& t) {
    switch (t.kind) {
        case K_HERO: return HR[t.idx].x;
        case K_MINION: return MIN[t.idx].x;
        case K_TOWER: return TW[t.idx].x;
        case K_NEXUS: return NX[t.idx].x;
        case K_CAMP: return CP[t.idx].x;
    }
    return -1;
}
static int tgtY(const Tgt& t) {
    switch (t.kind) {
        case K_HERO: return HR[t.idx].y;
        case K_MINION: return MIN[t.idx].y;
        case K_TOWER: return TW[t.idx].y;
        case K_NEXUS: return NX[t.idx].y;
        case K_CAMP: return CP[t.idx].y;
    }
    return -1;
}
static int tgtHp(const Tgt& t) {
    switch (t.kind) {
        case K_HERO: return HR[t.idx].hp;
        case K_MINION: return MIN[t.idx].hp;
        case K_TOWER: return TW[t.idx].hp;
        case K_NEXUS: return NX[t.idx].hp;
        case K_CAMP: return CP[t.idx].hp;
    }
    return 0;
}
static int tgtId(const Tgt& t) {
    switch (t.kind) {
        case K_HERO: return HR[t.idx].id;
        case K_MINION: return MIN[t.idx].id;
        case K_TOWER: return TW[t.idx].id;
        case K_NEXUS: return NX[t.idx].id;
        case K_CAMP: return CP[t.idx].id;
    }
    return -1;
}

// ---------------- 评分 ----------------
static const double V_HERO_DMG = 12.0;
static const double V_HERO_KILL = 950.0;
static const double V_MINION_DMG = 3.0;
static const double V_MINION_KILL = 60.0;
static const double V_TOWER_DMG = 18.0;
static const double V_TOWER_KILL = 900.0;
static const double V_CRYSTAL_DMG = 95.0;
static const double V_CRYSTAL_KILL = 1000000.0;
static const double V_CAMP_DMG = 1.5;
static const double V_CAMP_KILL = 40.0;

static double unitValue(const Tgt& t, int dmg, int effHp) {
    switch (t.kind) {
        case K_HERO: return dmg * V_HERO_DMG + (dmg >= effHp ? V_HERO_KILL * (effHp > 0 ? 1.0 : 0.25) : 0.0);
        case K_MINION: return dmg * V_MINION_DMG + (dmg >= effHp ? V_MINION_KILL : 0.0);
        case K_TOWER: return dmg * V_TOWER_DMG + (dmg >= effHp ? V_TOWER_KILL : 0.0);
        case K_NEXUS: return dmg * V_CRYSTAL_DMG + (dmg >= effHp ? V_CRYSTAL_KILL : 0.0);
        case K_CAMP: return dmg * V_CAMP_DMG + (dmg >= effHp ? V_CAMP_KILL : 0.0);
    }
    return 0.0;
}

static int gFocus = -1;
static int poolHp[9000];

// 收集所有敌对单位
static void collectTargets(vector<Tgt>& out) {
    out.clear();
    for (int i = 0; i < (int)HR.size(); i++)
        if (HR[i].team != MYT && aliveH(HR[i])) out.push_back({K_HERO, i});
    for (int i = 0; i < (int)MIN.size(); i++)
        if (MIN[i].team != MYT && MIN[i].hp > 0) out.push_back({K_MINION, i});
    for (int i = 0; i < (int)TW.size(); i++)
        if (TW[i].team != MYT && TW[i].hp > 0) out.push_back({K_TOWER, i});
    if (NX[1 - MYT].hp > 0) out.push_back({K_NEXUS, 1 - MYT});
    for (int i = 0; i < (int)CP.size(); i++)
        if (CP[i].hp > 0) out.push_back({K_CAMP, i});
}

// 站在己方塔火力范围内、且身边有敌人时，塔会帮忙打人
static double coverBonus(int px, int py) {
    bool cover = false;
    for (auto& t : TW) {
        if (t.team != MYT || t.hp <= 0) continue;
        if (canHit(t.x, t.y, px, py, TOWER_RNG)) { cover = true; break; }
    }
    if (!cover) return 0.0;
    for (auto& e : HR) {
        if (e.team == MYT || !aliveH(e)) continue;
        if (dist(e.x, e.y, px, py) <= 4) return 140.0;
    }
    return 0.0;
}

static double riskAt(int px, int py, int hid) {
    (void)hid;
    double r = 0.0;
    for (auto& e : HR) {
        if (e.team == MYT || !aliveH(e)) continue;
        if (canHit(e.x, e.y, px, py, CLS[SLOT(e.id)].rng)) r += heroAtk(e);
        if (SLOT(e.id) == 2 && e.cd == 0 && fireballOK(e.x, e.y, px, py)) r += 45 + 6 * e.lvl;
    }
    for (auto& t : TW) if (t.team != MYT && t.hp > 0 && canHit(t.x, t.y, px, py, TOWER_RNG)) r += t.atk * 2.0;
    for (auto& m : MIN) if (m.team != MYT && m.hp > 0 && dist(m.x, m.y, px, py) <= 1) r += m.hatk;
    for (auto& c : CP) if (c.hp > 0 && dist(c.x, c.y, px, py) <= 1) r += c.atk;
    return r;
}

// 在 (px,py) 处的最优动作（poolHp 是已承诺伤害，用于真正集火）
struct Act { int act = 0; int tid = -1; double val = 0.0; int dmg = 0; };

static inline int effHpOf(const Tgt& t) {
    int id = tgtId(t);
    int p = (id >= 0 && id < 9000) ? poolHp[id] : 0;
    return tgtHp(t) - p;
}

static Act bestActionAt(int px, int py, const HeroInfo& h, const vector<Tgt>& tg) {
    Act best;
    int slot = SLOT(h.id);
    int atk = heroAtk(h);
    int rng = CLS[slot].rng;

    for (auto& t : tg) {
        if (!tgtAlive(t)) continue;
        if (!canHit(px, py, tgtX(t), tgtY(t), rng)) continue;
        int eh = effHpOf(t);
        if (eh <= 0) continue;
        double v = unitValue(t, atk, eh);
        if (t.kind == K_HERO && HR[t.idx].id == gFocus) v += 100.0;
        if (v > best.val) { best.act = 1; best.tid = tgtId(t); best.val = v; best.dmg = atk; }
    }

    if (slot == 2 && h.cd == 0) {           // 火球
        int fdmg = 45 + 6 * h.lvl;
        for (auto& t : tg) {
            if (!tgtAlive(t)) continue;
            if (!fireballOK(px, py, tgtX(t), tgtY(t))) continue;
            int eh = effHpOf(t);
            if (eh <= 0) continue;
            double v = unitValue(t, fdmg, eh) + 25.0;
            if (t.kind == K_HERO && HR[t.idx].id == gFocus) v += 100.0;
            if (v > best.val) { best.act = 2; best.tid = tgtId(t); best.val = v; best.dmg = fdmg; }
        }
    } else if (slot == 3 && h.cd == 0) {    // 穿云箭
        static const int dx[4] = {0, 0, -1, 1};
        static const int dy[4] = {-1, 1, 0, 0};
        for (int k = 0; k < 4; k++) {
            int cells[4][2];
            int n = pierceCells(px, py, dx[k], dy[k], cells);
            if (n == 0) continue;
            double v = 0.0;
            int pickId = -1, bestEh = -1;
            for (auto& t : tg) {
                if (!tgtAlive(t)) continue;
                int tx = tgtX(t), ty = tgtY(t);
                bool in = false;
                for (int i = 0; i < n; i++) if (cells[i][0] == tx && cells[i][1] == ty) { in = true; break; }
                if (!in) continue;
                if (dist(px, py, tx, ty) > 4) continue;
                int eh = effHpOf(t);
                if (eh <= 0) continue;
                v += unitValue(t, atk, eh);
                if (t.kind == K_HERO && HR[t.idx].id == gFocus) v += 100.0;
                if (pickId < 0 || eh < bestEh) { pickId = tgtId(t); bestEh = eh; }
            }
            if (pickId >= 0 && v > best.val) { best.act = 2; best.tid = pickId; best.val = v; best.dmg = atk; }
        }
    } else if (slot == 0 && h.cd == 0) {    // 回春术
        int amount = 50 + 6 * h.lvl;
        int miss = h.maxhp - h.hp;
        if (miss > 0) {
            double v = (double)min(amount, miss) * 9.0;
            if (v > best.val) { best.act = 2; best.tid = -1; best.val = v; best.dmg = 0; }
        }
    } else if (slot == 4 && h.cd == 0) {    // 治疗术
        int amount = 40 + 5 * h.lvl;
        int bestMiss = 0;
        for (auto& a : HR) {
            if (a.team != MYT || !aliveH(a)) continue;
            if (dist(a.x, a.y, px, py) > 2) continue;
            int miss = a.maxhp - a.hp;
            if (miss > bestMiss) bestMiss = miss;
        }
        if (bestMiss > 0) {
            double v = (double)min(amount, bestMiss) * 9.0;
            if (v > best.val) { best.act = 2; best.tid = -1; best.val = v; best.dmg = 0; }
        }
    }
    return best;
}

// ---------------- 决策 ----------------
struct Opt { char mv = 'S'; int act = 0; int tid = -1; double score = -1e18; int px = 0, py = 0; int dmg = 0; };

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    string line;
    int pending = 0;
    string kind;

    while (getline(cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        size_t p = 0;
        while (p < line.size() && isspace((unsigned char)line[p])) p++;
        size_t q = p;
        while (q < line.size() && !isspace((unsigned char)line[q])) q++;
        string head = line.substr(p, q - p);

        if (!kind.empty() && pending > 0) {
            if (kind == "MAP") { MP.push_back(line); pending--; continue; }
            if (kind == "K") {
                int id, hp, atk, rng, cd; char pos[8], cls[24], sk[40];
                if (sscanf(line.c_str(), "K %d %d %d %d %d %7s %23s %39s", &id, &hp, &atk, &rng, &cd, pos, cls, sk) == 8)
                    CLS[id] = {hp, atk, rng, cd, string(sk)};
                pending--; continue;
            }
            if (kind == "P") {
                int x, y;
                if (sscanf(line.c_str(), "P %d %d", &x, &y) == 2) {
                    int i = 0;
                    while (i < 10 && SPAWN_PT[i][0] != -1) i++;
                    if (i < 10) { SPAWN_PT[i][0] = x; SPAWN_PT[i][1] = y; }
                }
                pending--; continue;
            }
            if (kind == "B") {
                int x0, y0, x1, y1;
                if (sscanf(line.c_str(), "B %d %d %d %d", &x0, &y0, &x1, &y1) == 4) {
                    int i = 0;
                    while (i < 2 && BASE_RECT[i][2] != -1) i++;
                    if (i < 2) { BASE_RECT[i][0] = x0; BASE_RECT[i][1] = y0; BASE_RECT[i][2] = x1; BASE_RECT[i][3] = y1; }
                }
                pending--; continue;
            }
            if (kind == "T") {
                TowerInfo t{};
                if (sscanf(line.c_str(), "T %d %d %d %d %d %d %d %d", &t.id, &t.team, &t.lane, &t.tier,
                           &t.x, &t.y, &t.maxhp, &t.atk) == 8) { t.hp = t.maxhp; TW.push_back(t); }
                pending--; continue;
            }
            if (kind == "TH") {
                int id, hp;
                if (sscanf(line.c_str(), "T %d %d", &id, &hp) == 2)
                    for (auto& t : TW) if (t.id == id) t.hp = hp;
                pending--; continue;
            }
            if (kind == "N") {
                NexusInfo n{};
                if (sscanf(line.c_str(), "N %d %d %d %d %d", &n.id, &n.team, &n.x, &n.y, &n.maxhp) == 5) {
                    n.hp = n.maxhp; NX[n.team] = n;
                }
                pending--; continue;
            }
            if (kind == "J") {
                CampInfo c{}; int xp;
                if (sscanf(line.c_str(), "J %d %d %d %d %d %d", &c.id, &c.x, &c.y, &c.maxhp, &c.atk, &xp) == 6) {
                    c.hp = c.maxhp; c.resp = -1; CP.push_back(c);
                }
                pending--; continue;
            }
            if (kind == "JH") {
                int id, hp, rsp;
                if (sscanf(line.c_str(), "J %d %d %d", &id, &hp, &rsp) == 3)
                    for (auto& c : CP) if (c.id == id) { c.hp = hp; c.resp = rsp; }
                pending--; continue;
            }
            if (kind == "I") {
                MinionInfo m{}; m.hatk = MINION_ATK;
                if (sscanf(line.c_str(), "I %d %d %d %d %d %d", &m.id, &m.team, &m.lane, &m.x, &m.y, &m.hp) == 6)
                    MIN.push_back(m);
                pending--; continue;
            }
            if (kind == "H") {
                HeroInfo h{};
                if (sscanf(line.c_str(), "H %d %d %d %d %d %d %d %d %d %d", &h.id, &h.team, &h.x, &h.y,
                           &h.hp, &h.maxhp, &h.lvl, &h.xp, &h.cd, &h.resp) == 10) {
                    if ((int)HR.size() <= h.id) HR.resize(h.id + 1);
                    HR[h.id] = h;
                }
                pending--; continue;
            }
            if (kind == "E") { pending--; continue; }
        }

        if (head == "INIT") {
            int n, team, size, turns, diff, ws, gap, mhp, matk, trng, rsp, rg, ft, cap, ag, npc;
            if (sscanf(line.c_str(), "INIT %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
                       &n, &team, &size, &turns, &diff, &ws, &gap, &mhp, &matk, &trng, &rsp, &rg, &ft,
                       &cap, &ag, &npc) == 16) {
                S = size; MYT = team; TOTALTURNS = turns; DIFF = diff;
                WAVE_SIZE = ws; WAVE_GAP = gap; MINION_HP = mhp; MINION_ATK = matk;
                TOWER_RNG = trng; RESPAWN_T = rsp; REGEN = rg; FOUNTAIN = ft;
                MINION_CAP = cap; AGGRO = ag;
            }
            for (int i = 0; i < 10; i++) SPAWN_PT[i][0] = SPAWN_PT[i][1] = -1;
            for (int i = 0; i < 2; i++) for (int j = 0; j < 4; j++) BASE_RECT[i][j] = -1;
        } else if (head == "MAP") {
            kind = "MAP"; pending = S;
        } else if (head == "CLASSES") {
            kind = "K"; pending = atoi(line.c_str() + 7);
        } else if (head == "LEVELS") {
            string l2; getline(cin, l2);
        } else if (head == "SPAWNS") {
            kind = "P"; pending = atoi(line.c_str() + 6);
        } else if (head == "BASES") {
            kind = "B"; pending = atoi(line.c_str() + 5);
        } else if (head == "TOWERS") {
            int cnt = atoi(line.c_str() + 6);
            kind = inTurn ? "TH" : "T"; pending = cnt;
        } else if (head == "NEXUS") {
            kind = "N"; pending = atoi(line.c_str() + 5);
        } else if (head == "CAMPS") {
            int cnt = atoi(line.c_str() + 5);
            kind = inTurn ? "JH" : "J"; pending = cnt;
        } else if (head == "TURN") {
            inTurn = true;
            TURN = atoi(line.c_str() + 4);
            MIN.clear();
        } else if (head == "CRYSTAL") {
            sscanf(line.c_str(), "CRYSTAL %d %d", &CRYSTAL_HP[0], &CRYSTAL_HP[1]);
            for (int i = 0; i < 2; i++) NX[i].hp = CRYSTAL_HP[i];
        } else if (head == "MINIONS") {
            kind = "I"; pending = atoi(line.c_str() + 7);
        } else if (head == "HEROES") {
            kind = "H"; pending = atoi(line.c_str() + 6);
        } else if (head == "EVENTS") {
            kind = "E"; pending = atoi(line.c_str() + 6);
        } else if (head == "END") {
            kind.clear(); pending = 0;

            // ================= 决策 =================
            buildStatic();
            buildOcc(-1);

            vector<int> my, en;
            for (int i = 0; i < (int)HR.size(); i++) {
                if (HR[i].team == MYT && aliveH(HR[i])) my.push_back(i);
                else if (HR[i].team != MYT && aliveH(HR[i])) en.push_back(i);
            }

            int myN = MYT, enN = 1 - MYT;

            // 集火目标
            gFocus = -1;
            {
                double bs = -1e18;
                for (int i : en) {
                    int dm = 999;
                    for (int j : my) dm = min(dm, dist(HR[i].x, HR[i].y, HR[j].x, HR[j].y));
                    if (dm > 10) continue;
                    double sc = -600.0 * HR[i].hp / max(1, HR[i].maxhp) - 10.0 * dm;
                    if (HR[i].hp <= 50) sc += 160;
                    if (sc > bs) { bs = sc; gFocus = HR[i].id; }
                }
            }

            // 目标场：默认打敌方水晶
            vector<int> gc;
            if (NX[enN].hp > 0) aroundCells(NX[enN].x, NX[enN].y, gc);
            if (gc.empty() && NX[enN].hp > 0) gc.push_back(IDX(NX[enN].x, NX[enN].y));
            buildField(fieldGoal, gc);

            // ---- 团队交战评估 ----
            // 前线：双方质心连线的中点
            double mc_x = 0, mc_y = 0;
            for (int i : my) { mc_x += HR[i].x; mc_y += HR[i].y; }
            if (!my.empty()) { mc_x /= my.size(); mc_y /= my.size(); }
            bool hasFight = !my.empty() && !en.empty();

            double myPow = 0, enPow = 0;
            int myNear = 0;
            bool contact = false;
            for (int i : my) {
                myPow += HR[i].hp * (1.0 + heroAtk(HR[i]) / 40.0);
            }
            for (int i : en) {
                enPow += HR[i].hp * (1.0 + heroAtk(HR[i]) / 40.0);
                for (int j : my) if (dist(HR[i].x, HR[i].y, HR[j].x, HR[j].y) <= 11) { contact = true; break; }
            }
            for (int i : my) for (int j : en) if (dist(HR[i].x, HR[i].y, HR[j].x, HR[j].y) <= 8) { myNear++; break; }
            int nearEnemyNex = 0;
            for (int i : my) if (dist(HR[i].x, HR[i].y, NX[enN].x, NX[enN].y) <= 7) nearEnemyNex++;
            bool commit = (nearEnemyNex >= 3) || (NX[enN].hp * 2 < NX[enN].maxhp);
            int enTotal = 0;
            for (auto& hh : HR) if (hh.team != MYT) enTotal++;
            int enDead = enTotal - (int)en.size();
            bool sprint = (enDead >= 2) || en.empty();
            bool teamRetreat = hasFight && contact && myNear > 0 && myPow < 0.78 * enPow && !commit && !sprint;

            // 回防/后撤锚点：最近的己方存活塔，没有就用己方水晶
            int anchorX = NX[myN].x, anchorY = NX[myN].y;
            {
                int bd = INF;
                for (auto& t : TW) {
                    if (t.team != MYT || t.hp <= 0) continue;
                    int d0 = abs(t.x - (int)mc_x) + abs(t.y - (int)mc_y);
                    if (d0 < bd) { bd = d0; anchorX = t.x; anchorY = t.y; }
                }
            }
            int threat = 0;
            for (int i : en) if (dist(HR[i].x, HR[i].y, NX[myN].x, NX[myN].y) <= 10) threat++;
            bool defend = (threat >= 2) || (threat >= 1 && NX[myN].hp * 2 < NX[myN].maxhp);
            // 已经在拆对方水晶时优先抢拆，不回防
            bool racing = (nearEnemyNex >= 3) && (NX[enN].hp * 3 < NX[enN].maxhp * 2);
            if (defend && !racing) { anchorX = NX[myN].x; anchorY = NX[myN].y; teamRetreat = true; }

            {
                vector<int> hc;
                aroundCells(anchorX, anchorY, hc);
                if (!hc.empty()) buildField(fieldHome, hc);
                else fieldHome = fieldGoal;
            }

            vector<Tgt> tgs;
            collectTargets(tgs);
            // 逐英雄决策
            static const char MV[5] = {'S', 'U', 'D', 'L', 'R'};
            static const int DX[5] = {0, 0, 0, -1, 1};
            static const int DY[5] = {0, -1, 1, 0, 0};

            memset(poolHp, 0, sizeof(poolHp));
            vector<char> claimed((size_t)S * S, 0);
            vector<char> vacated((size_t)S * S, 0);   // 本回合己方英雄要让出的格
            vector<int> ownerMy((size_t)S * S, -1);   // 该格上的己方英雄
            for (int i : my) ownerMy[IDX(HR[i].x, HR[i].y)] = i;
            Opt out[5];
            vector<int> order;
            for (int i : my) order.push_back(i);
            // 靠前的先走，后面的人可以跟进空出来的格子，避免堵死
            sort(order.begin(), order.end(), [&](int a, int b) {
                int da = fieldGoal[IDX(HR[a].x, HR[a].y)];
                int db = fieldGoal[IDX(HR[b].x, HR[b].y)];
                if (da != db) return da < db;
                return a < b;
            });

            for (int hi : order) {
                HeroInfo& h = HR[hi];
                int slot = SLOT(h.id);
                buildOcc(h.id);

                int nearEn = 0;
                for (int j : en) if (dist(HR[j].x, HR[j].y, h.x, h.y) <= 7) nearEn++;
                bool lowHp = (h.hp * 100 < h.maxhp * 34 && nearEn > 0);
                const vector<int>& fld = (lowHp || teamRetreat) ? fieldHome : fieldGoal;

                int curD = (IDX(h.x, h.y) < (int)fld.size()) ? fld[IDX(h.x, h.y)] : INF;
                double hpFrac = (double)h.hp / max(1, h.maxhp);

                Opt bo;
                bo.mv = 'S'; bo.act = 0; bo.tid = -1; bo.score = -1e18;
                bo.px = h.x; bo.py = h.y;

                for (int m = 0; m < 5; m++) {
                    int px = h.x + DX[m], py = h.y + DY[m];
                    if (m != 0) {
                        if (px < 0 || py < 0 || px >= S || py >= S) continue;
                        if (hardG[IDX(px, py)]) continue;
                        if (occG[IDX(px, py)]) {
                            // 只有己方队友且本回合确实要离开时才放行
                            int oid = ownerMy[IDX(px, py)];
                            if (oid < 0 || !vacated[IDX(px, py)]) continue;
                        }
                        if (claimed[IDX(px, py)]) continue;
                    } else {
                        if (claimed[IDX(h.x, h.y)]) continue;
                    }

                    int pd = (IDX(px, py) < (int)fld.size()) ? fld[IDX(px, py)] : INF;
                    double moveScore = 0.0;
                    if (curD < INF && pd < INF) moveScore = (curD - pd) * 150.0;
                    if (m == 0) moveScore += 2.0;
                    else if (curD < INF && pd >= curD) moveScore -= 12.0;   // 别横向漂移

                    Act a = bestActionAt(px, py, h, tgs);
                    double risk = riskAt(px, py, h.id);
                    double pen = risk * (2.0 + 9.0 * (1.0 - hpFrac));
                    if (risk >= h.hp) pen += 900.0 + 1600.0 * (1.0 - hpFrac);

                    double total = moveScore + a.val - pen + coverBonus(px, py);
                    if (lowHp || teamRetreat) total = moveScore * 1.6 + a.val - pen * 2.2;
                    if (sprint) total = moveScore * 1.7 + a.val - pen * 0.45;
                    // 微小确定性扰动：打破双方对称的僵持/互相挡路
                    {
                        unsigned hv = (unsigned)(TURN * 2654435761u + h.id * 40503u + m * 2246822519u);
                        hv ^= hv >> 13; hv *= 2654435761u; hv ^= hv >> 15;
                        total += (double)(hv % 100) * 0.55;
                    }
                    // 打不到人就不能原地发呆，否则双方会在射程外僵持
                    if (a.act == 0) {
                        if (m == 0 && !lowHp && !teamRetreat) total -= 260.0;
                        if (m != 0) total += 40.0;
                    }

                    if (total > bo.score) {
                        bo.score = total; bo.mv = MV[m]; bo.act = a.act; bo.tid = a.tid;
                        bo.px = px; bo.py = py; bo.dmg = a.dmg;
                    }
                }
                // 刺客突袭：本回合沿方向走两格，赶路/追杀/脱离都更快
                if (slot == 1 && h.cd == 0) {
                    for (int m = 1; m < 5; m++) {
                        int p1x = h.x + DX[m], p1y = h.y + DY[m];
                        int p2x = p1x + DX[m], p2y = p1y + DY[m];
                        if (p1x < 0 || p1y < 0 || p1x >= S || p1y >= S) continue;
                        if (p2x < 0 || p2y < 0 || p2x >= S || p2y >= S) continue;
                        if (hardG[IDX(p1x, p1y)] || hardG[IDX(p2x, p2y)]) continue;
                        if (occG[IDX(p1x, p1y)] || occG[IDX(p2x, p2y)]) continue;
                        if (claimed[IDX(p2x, p2y)]) continue;
                        int pd2 = fld[IDX(p2x, p2y)];
                        if (pd2 >= INF) continue;
                        double ms = (curD < INF) ? (curD - pd2) * 150.0 : 0.0;
                        double pen2 = riskAt(p2x, p2y, h.id) * (2.0 + 9.0 * (1.0 - hpFrac));
                        double tot = ms - pen2 + 55.0;
                        if (sprint) tot = ms * 1.7 - pen2 * 0.45 + 55.0;
                        if (tot > bo.score) {
                            bo.score = tot; bo.mv = MV[m]; bo.act = 2; bo.tid = -1;
                            bo.px = p2x; bo.py = p2y; bo.dmg = 0;
                        }
                    }
                }
                claimed[IDX(bo.px, bo.py)] = 1;
                if (bo.px != h.x || bo.py != h.y) vacated[IDX(h.x, h.y)] = 1;
                if (bo.act >= 1 && bo.tid >= 0 && bo.tid < 9000) poolHp[bo.tid] += bo.dmg;
                out[slot] = bo;
            }

            // 输出
            string s = to_string(TURN);
            for (int k = 0; k < 5; k++) {
                int hi = MYT * 5 + k;
                bool dead = (hi >= (int)HR.size()) || !aliveH(HR[hi]);
                if (dead) { s += " S -"; continue; }
                Opt& o = out[k];
                s += ' '; s += o.mv; s += ' ';
                if (o.act == 2) {
                    if ((k == 2 || k == 3) && o.tid >= 0) { s += "Q "; s += to_string(o.tid); }
                    else if (k != 2 && k != 3) s += 'Q';
                    else s += '-';
                } else if (o.act == 1 && o.tid >= 0) {
                    s += to_string(o.tid);
                } else {
                    s += '-';
                }
            }
            s += '\n';
            fwrite(s.data(), 1, s.size(), stdout);
            fflush(stdout);
        }
    }
    return 0;
}
