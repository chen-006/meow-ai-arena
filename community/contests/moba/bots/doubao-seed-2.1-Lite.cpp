// 峡谷对决 Bot v5 - 修复卡位bug，跟兵线/推塔推进
#include <bits/stdc++.h>
using namespace std;

static int S = 0, myTeam = 0, curTurn = 0;
static vector<string> grid;

struct TowerT { int id, team, lane, tier, x, y, maxhp, hp, atk; };
struct CrystalT { int id, team, x, y, maxhp, hp; };
struct CampT { int id, x, y, maxhp, hp, atk, resp; };
struct MinionT { int id, team, lane, x, y, hp; };
struct HeroT { int id, team, x, y, hp, maxhp, level, xp, cd, resp; };

static vector<TowerT> towers;
static vector<CrystalT> crystals;
static vector<CampT> camps;
static vector<MinionT> minions;
static vector<HeroT> heroes;
static vector<pair<int,int>> mySpawns;

static int heroRange(int cls) { return (cls == 0 || cls == 1) ? 1 : 2; }
static int heroLane[5] = {2, 2, 2, 2, 2}; // 全部中路抱团推

static int manh(int x1, int y1, int x2, int y2) { return abs(x1-x2) + abs(y1-y2); }
static bool inB(int x, int y) { return x >= 0 && x < S && y >= 0 && y < S; }
static bool isWall(int x, int y) { return !inB(x,y) || grid[y][x] == '#'; }

static int staticBlocked(int x, int y) {
    if (isWall(x,y)) return 1;
    for (auto& t : towers) if (t.hp > 0 && t.x == x && t.y == y) return 2;
    for (auto& c : camps) if (c.hp > 0 && c.x == x && c.y == y) return 3;
    for (auto& c : crystals) if (c.hp > 0 && c.x == x && c.y == y) return 4;
    return 0;
}

static bool wallBlocked(int x1, int y1, int x2, int y2, int rng) {
    int d = manh(x1,y1,x2,y2);
    if (d <= 1) return false;
    if (d > rng) return true;
    int dx = x2-x1, dy = y2-y1;
    if (dx == 0) return isWall(x1, y1 + dy/2);
    if (dy == 0) return isWall(x1 + dx/2, y1);
    return isWall(x1+dx, y1) && isWall(x1, y1+dy);
}

static vector<vector<int>> bfsField(int tx, int ty) {
    vector<vector<int>> dist(S, vector<int>(S, -1));
    if (!inB(tx,ty)) return dist;
    queue<pair<int,int>> q;
    dist[ty][tx] = 0;
    q.push({tx,ty});
    int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
    while (!q.empty()) {
        auto [x,y] = q.front(); q.pop();
        for (auto& d : dirs) {
            int nx = x+d[0], ny = y+d[1];
            if (!inB(nx,ny) || dist[ny][nx] != -1 || isWall(nx,ny)) continue;
            bool blk = false;
            for (auto& t : towers) if (t.hp > 0 && t.x == nx && t.y == ny) { blk = true; break; }
            if (!blk) for (auto& c : camps) if (c.hp > 0 && c.x == nx && c.y == ny) { blk = true; break; }
            if (!blk) for (auto& c : crystals) if (c.hp > 0 && c.x == nx && c.y == ny) { blk = true; break; }
            if (blk) continue;
            dist[ny][nx] = dist[y][x]+1;
            q.push({nx,ny});
        }
    }
    return dist;
}

static HeroT* myHero(int idx) {
    int tid = myTeam*5 + idx;
    for (auto& h : heroes) if (h.id == tid) return &h;
    return nullptr;
}

static pair<int,int> enemyCrystalPos() {
    for (auto& c : crystals) if (c.team != myTeam) return {c.x, c.y};
    return {S-3, S-3};
}

static pair<int,int> myBasePos() {
    if (!mySpawns.empty()) return mySpawns[0];
    return {3, S-4};
}

// 推进目标：跟前方小兵走，到塔下就拆塔
static pair<int,int> advanceTarget(int lane, int hx, int hy) {
    // 打野：找最近的野怪营地
    if (lane < 0) {
        pair<int,int> best = myBasePos();
        int bestDist = 9999;
        for (auto& c : camps) {
            if (c.hp > 0) {
                int d = manh(c.x, c.y, hx, hy);
                if (d < bestDist) { bestDist = d; best = {c.x, c.y}; }
            }
        }
        return best;
    }
    auto ec = enemyCrystalPos();
    // 找最前方的己方小兵
    pair<int,int> best = myBasePos();
    int bestDist = 9999;
    bool hasFriendly = false;
    for (auto& m : minions) {
        if (m.team == myTeam && m.x >= 0 && m.lane == lane) {
            int d = manh(m.x, m.y, ec.first, ec.second);
            if (d < bestDist) { bestDist = d; best = {m.x, m.y}; hasFriendly = true; }
        }
    }
    // 如果英雄已经比最前方小兵更靠近敌人，直接推塔（不后退）
    int heroDist = manh(hx, hy, ec.first, ec.second);
    if (hasFriendly && heroDist < bestDist) {
        for (int tier = 1; tier >= 0; tier--) {
            for (auto& t : towers)
                if (t.team != myTeam && t.hp > 0 && t.lane == lane && t.tier == tier)
                    return {t.x, t.y};
        }
        return ec;
    }
    if (hasFriendly) return best;
    // 没己方小兵：找最前方的敌方小兵（去清兵）
    pair<int,int> enBest = myBasePos();
    int enBestDist = 9999;
    bool hasEnemy = false;
    for (auto& m : minions) {
        if (m.team != myTeam && m.x >= 0 && m.lane == lane) {
            int d = manh(m.x, m.y, myBasePos().first, myBasePos().second);
            if (d < enBestDist) { enBestDist = d; enBest = {m.x, m.y}; hasEnemy = true; }
        }
    }
    if (hasEnemy) return enBest;
    // 完全没兵：找敌方塔
    for (int tier = 1; tier >= 0; tier--) {
        for (auto& t : towers)
            if (t.team != myTeam && t.hp > 0 && t.lane == lane && t.tier == tier)
                return {t.x, t.y};
    }
    return ec;
}

static TowerT* towerThreat(int x, int y) {
    for (auto& t : towers)
        if (t.team != myTeam && t.hp > 0 && manh(x,y,t.x,t.y) <= 2) return &t;
    return nullptr;
}

static bool minionTanking(TowerT* t) {
    if (!t) return false;
    for (auto& m : minions)
        if (m.team == myTeam && m.x >= 0 && manh(m.x,m.y,t->x,t->y) <= 2) return true;
    return false;
}

static int nearbyAllies(int x, int y, int r) {
    int c = 0;
    for (auto& h : heroes) if (h.team == myTeam && h.x >= 0 && manh(x,y,h.x,h.y) <= r) c++;
    return c;
}
static int nearbyFoes(int x, int y, int r) {
    int c = 0;
    for (auto& h : heroes) if (h.team != myTeam && h.x >= 0 && manh(x,y,h.x,h.y) <= r) c++;
    return c;
}

struct Decision { char move = 'S'; string act = "-"; int qTarget = -1; };

static Decision decide(int cls, HeroT* h) {
    Decision d;
    if (!h || h->x < 0) return d;
    int hx = h->x, hy = h->y;
    int hpRatio = h->hp * 100 / h->maxhp;
    int range = heroRange(cls);
    int lane = heroLane[cls];

    vector<HeroT*> foes;
    for (auto& e : heroes) if (e.team != myTeam && e.x >= 0) foes.push_back((HeroT*)&e);

    HeroT* nearestFoe = nullptr; int nearestFoeDist = 999;
    for (auto f : foes) {
        int dist = manh(hx,hy,f->x,f->y);
        if (dist < nearestFoeDist) { nearestFoeDist = dist; nearestFoe = f; }
    }

    TowerT* threat = towerThreat(hx, hy);
    bool tanking = minionTanking(threat);
    int alliesNear = nearbyAllies(hx, hy, 6);
    int foesNear = nearbyFoes(hx, hy, 6);

    // ---- 撤退（适中偏激进）----
    bool retreat = false;
    if (hpRatio < 22 && nearestFoeDist <= 5) retreat = true;
    if (hpRatio < 10 && threat) retreat = true;
    if ((cls == 2 || cls == 3) && hpRatio < 38 && nearestFoeDist <= 5) retreat = true;
    if (foesNear > alliesNear + 2 && nearestFoeDist <= 3) retreat = true;

    if (retreat) {
        auto base = myBasePos();
        auto field = bfsField(base.first, base.second);
        int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
        char dc[5] = {'U','D','L','R'};
        int bestDir = -1, bestDist = field[hy][hx] > 0 ? field[hy][hx] : 999;
        for (int i = 0; i < 4; i++) {
            int nx = hx+dirs[i][0], ny = hy+dirs[i][1];
            if (!inB(nx,ny) || staticBlocked(nx,ny) != 0) continue;
            // 不检查队友占用（引擎会处理同时移动）
            if (field[ny][nx] >= 0 && field[ny][nx] < bestDist) { bestDist = field[ny][nx]; bestDir = i; }
        }
        if (bestDir >= 0) d.move = dc[bestDir];
        if (cls == 0 && h->cd == 0 && hpRatio < 60) d.act = "Q";
        if (cls == 4 && h->cd == 0) {
            HeroT* lowest = h;
            for (auto& e : heroes) {
                if (e.team == myTeam && e.x >= 0 && e.id != h->id && manh(hx,hy,e.x,e.y) <= 2)
                    if (e.hp*100/e.maxhp < lowest->hp*100/lowest->maxhp) lowest = (HeroT*)&e;
            }
            if (lowest->hp*100/lowest->maxhp < 75) d.act = "Q";
        }
        return d;
    }

    // ---- 攻击目标（推塔优先）----
    struct Tgt { int id; int score; };
    vector<Tgt> tgts;
    // 塔/水晶：最高优先级，只要在射程内就拆
    for (auto& t : towers) {
        if (t.team != myTeam && t.hp > 0) {
            int dist = manh(hx,hy,t.x,t.y);
            if (dist <= range && !wallBlocked(hx,hy,t.x,t.y,range)) {
                int score = 1000;
                // 战士可以扛塔
                if (cls == 0 && !tanking && hpRatio > 50) score = 1200;
                tgts.push_back({t.id, score});
            }
        }
    }
    for (auto& c : crystals) {
        if (c.team != myTeam && c.hp > 0) {
            int dist = manh(hx,hy,c.x,c.y);
            if (dist <= range && !wallBlocked(hx,hy,c.x,c.y,range))
                tgts.push_back({c.id, 1000});
        }
    }
    // 小兵：清兵拿经验
    for (auto& m : minions) {
        if (m.team != myTeam && m.x >= 0) {
            int dist = manh(hx,hy,m.x,m.y);
            if (dist <= range && !wallBlocked(hx,hy,m.x,m.y,range))
                tgts.push_back({m.id, 400});
        }
    }
    // 敌方英雄：只打残血的
    for (auto f : foes) {
        int dist = manh(hx,hy,f->x,f->y);
        if (dist <= range && !wallBlocked(hx,hy,f->x,f->y,range)) {
            int score = 0;
            if (f->hp < 30) score = 900; // 残血必收
            else if (foesNear < alliesNear) score = 500; // 人数优势可以打
            else score = 30; // 别主动打
            tgts.push_back({f->id, score});
        }
    }
    // 野怪
    for (auto& c : camps) {
        if (c.hp > 0) {
            int dist = manh(hx,hy,c.x,c.y);
            if (dist <= range && !wallBlocked(hx,hy,c.x,c.y,range))
                tgts.push_back({c.id, 100});
        }
    }

    // ---- 技能 ----
    bool usedSkill = false;
    if (h->cd == 0) {
        if (cls == 0 && hpRatio < 60) { d.act = "Q"; usedSkill = true; }
        else if (cls == 2) {
            HeroT* best = nullptr;
            for (auto f : foes) {
                int dist = manh(hx,hy,f->x,f->y);
                if (dist < 1 || dist > 5) continue;
                if (f->x != hx && f->y != hy) continue;
                bool blocked = false;
                if (f->x == hx) {
                    int y1 = min(hy, f->y), y2 = max(hy, f->y);
                    for (int y = y1+1; y < y2; y++) if (isWall(hx, y)) { blocked = true; break; }
                } else {
                    int x1 = min(hx, f->x), x2 = max(hx, f->x);
                    for (int x = x1+1; x < x2; x++) if (isWall(x, hy)) { blocked = true; break; }
                }
                if (!blocked && (!best || f->hp < best->hp)) best = f;
            }
            if (best) { d.act = "Q"; d.qTarget = best->id; usedSkill = true; }
        }
        else if (cls == 3) {
            int bestCnt = 0, bestTid = -1;
            for (auto f : foes) {
                int dist = manh(hx,hy,f->x,f->y);
                if (dist < 1 || dist > 4) continue;
                if (f->x != hx && f->y != hy) continue;
                int dx = (f->x > hx) - (f->x < hx);
                int dy = (f->y > hy) - (f->y < hy);
                int cnt = 0;
                for (int s2 = 1; s2 <= 4; s2++) {
                    int cx = hx+dx*s2, cy = hy+dy*s2;
                    if (!inB(cx,cy) || isWall(cx,cy)) break;
                    for (auto& e : heroes) if (e.team != myTeam && e.x == cx && e.y == cy && e.x >= 0) cnt++;
                    for (auto& m : minions) if (m.team != myTeam && m.x == cx && m.y == cy && m.x >= 0) cnt++;
                }
                if (cnt >= 2 || (cnt == 1 && f->hp < 60))
                    if (cnt > bestCnt) { bestCnt = cnt; bestTid = f->id; }
            }
            if (bestTid >= 0) { d.act = "Q"; d.qTarget = bestTid; usedSkill = true; }
        }
        else if (cls == 4) {
            HeroT* lowest = h;
            for (auto& e : heroes) {
                if (e.team == myTeam && e.x >= 0 && e.id != h->id && manh(hx,hy,e.x,e.y) <= 2)
                    if (e.hp*100/e.maxhp < lowest->hp*100/lowest->maxhp) lowest = (HeroT*)&e;
            }
            if (lowest->hp*100/lowest->maxhp < 70) { d.act = "Q"; usedSkill = true; }
        }
    }

    if (!usedSkill && !tgts.empty()) {
        sort(tgts.begin(), tgts.end(), [](const Tgt& a, const Tgt& b) { return a.score > b.score; });
        d.act = to_string(tgts[0].id);
    }

    // ---- 移动 ----
    // 塔下没兵扛，近战后退
    if (threat && !tanking && hpRatio < 80 && range == 1) {
        int dx = hx - threat->x, dy = hy - threat->y;
        char mv = 'S';
        if (abs(dx) > abs(dy)) mv = dx > 0 ? 'R' : 'L';
        else mv = dy > 0 ? 'D' : 'U';
        int nx = hx + (mv=='R'?1:mv=='L'?-1:0);
        int ny = hy + (mv=='D'?1:mv=='U'?-1:0);
        if (inB(nx,ny) && staticBlocked(nx,ny) == 0) d.move = mv;
        return d;
    }

    // 朝推进目标前进
    auto obj = advanceTarget(lane, hx, hy);
    auto field = bfsField(obj.first, obj.second);
    int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
    char dc[5] = {'U','D','L','R'};
    int bestDir = -1, bestDist = field[hy][hx] > 0 ? field[hy][hx] : 999;
    for (int i = 0; i < 4; i++) {
        int nx = hx+dirs[i][0], ny = hy+dirs[i][1];
        if (!inB(nx,ny) || staticBlocked(nx,ny) != 0) continue;
        // 关键修复：不检查队友占用（引擎允许同时移动/穿过）
        if (field[ny][nx] >= 0 && field[ny][nx] < bestDist) { bestDist = field[ny][nx]; bestDir = i; }
    }
    if (bestDir >= 0) d.move = dc[bestDir];

    // 刺客突袭加速
    if (cls == 1 && h->cd == 0 && d.move != 'S' && !usedSkill && alliesNear >= 2) {
        int nx = hx + (d.move=='R'?1:d.move=='L'?-1:0);
        int ny = hy + (d.move=='D'?1:d.move=='U'?-1:0);
        int nx2 = nx + (d.move=='R'?1:d.move=='L'?-1:0);
        int ny2 = ny + (d.move=='D'?1:d.move=='U'?-1:0);
        if (inB(nx,ny) && inB(nx2,ny2) && staticBlocked(nx,ny) == 0 && staticBlocked(nx2,ny2) == 0)
            d.act = "Q";
    }

    return d;
}

// ==================== 解析 ====================
static vector<string> split(const string& s) {
    vector<string> r; string cur;
    for (char c : s) { if (c == ' ') { if (!cur.empty()) r.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) r.push_back(cur);
    return r;
}

static string readLn() {
    string s;
    if (!getline(cin, s)) return "";
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

int main() {
    ios::sync_with_stdio(false); cin.tie(nullptr);

    while (true) {
        string line = readLn();
        if (line.empty()) break;
        auto tok = split(line);
        if (tok.empty()) continue;

        if (tok[0] == "INIT") {
            myTeam = stoi(tok[2]);
            S = stoi(tok[3]);

            readLn(); // MAP
            grid.clear();
            for (int y = 0; y < S; y++) grid.push_back(readLn());

            readLn(); // CLASSES 5
            for (int i = 0; i < 5; i++) readLn();

            readLn(); // LEVELS 11
            readLn(); // numbers

            readLn(); // SPAWNS 10
            mySpawns.clear();
            for (int i = 0; i < 10; i++) {
                auto p = split(readLn());
                int x = stoi(p[1]), y = stoi(p[2]);
                if ((myTeam == 0 && i < 5) || (myTeam == 1 && i >= 5)) mySpawns.push_back({x, y});
            }

            readLn(); // BASES 2
            for (int i = 0; i < 2; i++) readLn();

            readLn(); // TOWERS 12
            towers.clear();
            for (int i = 0; i < 12; i++) {
                auto p = split(readLn());
                TowerT t;
                t.id = stoi(p[1]); t.team = stoi(p[2]); t.lane = stoi(p[3]);
                t.tier = stoi(p[4]); t.x = stoi(p[5]); t.y = stoi(p[6]);
                t.maxhp = stoi(p[7]); t.hp = t.maxhp; t.atk = stoi(p[8]);
                towers.push_back(t);
            }

            readLn(); // NEXUS 2
            crystals.clear();
            for (int i = 0; i < 2; i++) {
                auto p = split(readLn());
                CrystalT c;
                c.id = stoi(p[1]); c.team = stoi(p[2]); c.x = stoi(p[3]); c.y = stoi(p[4]);
                c.maxhp = stoi(p[5]); c.hp = c.maxhp;
                crystals.push_back(c);
            }

            string campsHdr = readLn();
            int nCamps = stoi(split(campsHdr)[1]);
            camps.clear();
            for (int i = 0; i < nCamps; i++) {
                auto p = split(readLn());
                CampT c;
                c.id = stoi(p[1]); c.x = stoi(p[2]); c.y = stoi(p[3]);
                c.maxhp = stoi(p[4]); c.atk = stoi(p[5]); c.hp = c.maxhp; c.resp = -1;
                camps.push_back(c);
            }
        }

        if (tok[0] == "TURN") {
            curTurn = stoi(tok[1]);

            auto cp = split(readLn());
            for (int i = 0; i < 2; i++) crystals[i].hp = stoi(cp[i+1]);

            readLn();
            for (int i = 0; i < 12; i++) {
                auto q = split(readLn());
                int tid = stoi(q[1]), hp = stoi(q[2]);
                for (auto& t : towers) if (t.id == tid) t.hp = hp;
            }

            string ch = readLn();
            int nc = stoi(split(ch)[1]);
            for (int i = 0; i < nc; i++) {
                auto q = split(readLn());
                int cid = stoi(q[1]), hp = stoi(q[2]), resp = stoi(q[3]);
                for (auto& c : camps) if (c.id == cid) { c.hp = hp; c.resp = resp; }
            }

            string mh = readLn();
            int nm = stoi(split(mh)[1]);
            minions.clear();
            for (int i = 0; i < nm; i++) {
                auto q = split(readLn());
                MinionT m;
                m.id = stoi(q[1]); m.team = stoi(q[2]); m.lane = stoi(q[3]);
                m.x = stoi(q[4]); m.y = stoi(q[5]); m.hp = stoi(q[6]);
                minions.push_back(m);
            }

            string hh = readLn();
            int nh = stoi(split(hh)[1]);
            heroes.clear();
            for (int i = 0; i < nh; i++) {
                auto q = split(readLn());
                HeroT h;
                h.id = stoi(q[1]); h.team = stoi(q[2]); h.x = stoi(q[3]); h.y = stoi(q[4]);
                h.hp = stoi(q[5]); h.maxhp = stoi(q[6]); h.level = stoi(q[7]); h.xp = stoi(q[8]);
                h.cd = stoi(q[9]); h.resp = stoi(q[10]);
                heroes.push_back(h);
            }

            readLn();
            string evLine;
            while ((evLine = readLn()) != "END" && !evLine.empty()) {}

            cout << curTurn;
            for (int cls = 0; cls < 5; cls++) {
                HeroT* h = myHero(cls);
                Decision d = decide(cls, h);
                cout << " " << d.move << " " << d.act;
                if ((cls == 2 || cls == 3) && d.act == "Q") cout << " " << d.qTarget;
            }
            cout << endl;
        }
    }
    return 0;
}
