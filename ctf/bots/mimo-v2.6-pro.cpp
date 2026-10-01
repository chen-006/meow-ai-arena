// Competitive multi-team CTF bot.
// Direct BFS movement + aggressive opportunistic combat.
#include <bits/stdc++.h>
using namespace std;

struct Unit { int id, team, x, y, hp, flag, respawn_at; };
struct Flag { int id; string status; int x, y, holder, return_at; };
struct Event { string type; vector<int> args; };

struct Game {
    int teams = 0, me = 0, size = 0, turns = 0, hp = 0, damage = 0, respawn = 0, flag_return = 0, flag_cooldown = 0;
    vector<string> map;
    vector<pair<int, int>> bases;
    vector<pair<int, int>> spots;
    int turn = 0;
    vector<int> score;
    vector<Unit> units;
    vector<Flag> flags;
    vector<Event> events;
};

bool read_turn(Game& g) {
    string w;
    while (cin >> w) {
        if (w == "INIT") {
            cin >> g.teams >> g.me >> g.size >> g.turns >> g.hp >> g.damage >> g.respawn >> g.flag_return >> g.flag_cooldown;
        } else if (w == "MAP") {
            g.map.assign(g.size, "");
            for (auto& row : g.map) cin >> row;
        } else if (w == "BASES") {
            int k; cin >> k; g.bases.resize(k);
            for (auto& b : g.bases) cin >> b.first >> b.second;
        } else if (w == "SPOTS") {
            int k; cin >> k; g.spots.resize(k);
            for (auto& s : g.spots) cin >> s.first >> s.second;
        } else if (w == "TURN") {
            cin >> g.turn;
        } else if (w == "SCORE") {
            g.score.resize(g.teams);
            for (auto& s : g.score) cin >> s;
        } else if (w == "UNITS") {
            int k; cin >> k; g.units.resize(k);
            for (auto& u : g.units) { string tag; cin >> tag >> u.id >> u.team >> u.x >> u.y >> u.hp >> u.flag >> u.respawn_at; }
        } else if (w == "FLAGS") {
            int k; cin >> k; g.flags.resize(k);
            for (auto& f : g.flags) { string tag; cin >> tag >> f.id >> f.status >> f.x >> f.y >> f.holder >> f.return_at; }
        } else if (w == "EVENTS") {
            int k; cin >> k; g.events.assign(k, {});
            string line; getline(cin, line);
            for (auto& e : g.events) {
                getline(cin, line);
                istringstream in(line);
                string tag; in >> tag >> e.type;
                for (int v; in >> v;) e.args.push_back(v);
            }
        } else if (w == "END") {
            return true;
        }
    }
    return false;
}

// ============================== WORLD =====================================
static int SZ, NTEAMS, ME, TURNS, FULLHP;
static vector<vector<char>> WALL;
static vector<vector<pair<int,int>>> ALLBASE, SPAWNORD;
static vector<pair<int,int>> MYBASE, SPOTS;
static vector<vector<int>> BASEDIST;
static vector<vector<vector<int>>> SPOTDIST;

static inline bool isWall(int x, int y) {
    return x < 0 || y < 0 || x >= SZ || y >= SZ || WALL[y][x] == '#';
}
static inline int manh(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}
static bool canAttack(int ax, int ay, int bx, int by) {
    int dx = bx - ax, dy = by - ay;
    if (abs(dx) + abs(dy) > 2) return false;
    if (abs(dx) == 2 && dy == 0) return !isWall(ax + dx / 2, ay);
    if (abs(dy) == 2 && dx == 0) return !isWall(ax, ay + dy / 2);
    if (abs(dx) == 1 && abs(dy) == 1) return !isWall(ax + dx, ay) || !isWall(ax, ay + dy);
    return true;
}

static vector<vector<int>> bfsMulti(const vector<pair<int,int>>& src) {
    vector<vector<int>> dist(SZ, vector<int>(SZ, -1));
    queue<pair<int,int>> q;
    for (auto [x, y] : src) {
        if (isWall(x, y) || dist[y][x] >= 0) continue;
        dist[y][x] = 0; q.push({x, y});
    }
    static const int DX[4] = {0, 0, -1, 1}, DY[4] = {-1, 1, 0, 0};
    while (!q.empty()) {
        auto [x, y] = q.front(); q.pop();
        int d = dist[y][x];
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (isWall(nx, ny) || dist[ny][nx] >= 0) continue;
            dist[ny][nx] = d + 1; q.push({nx, ny});
        }
    }
    return dist;
}
static inline int distTo(const vector<vector<int>>& f, int x, int y) {
    if (x < 0 || y < 0 || x >= SZ || y >= SZ) return 1 << 28;
    int d = f[y][x];
    return d < 0 ? 1 << 28 : d;
}

static const int MX[5] = {0, 0, 0, -1, 1};
static const int MY[5] = {0, -1, 1, 0, 0};
static const char MC[5] = {'S', 'U', 'D', 'L', 'R'};

static pair<int,int> bestStep(const vector<vector<int>>& fld, int x, int y) {
    int best = distTo(fld, x, y), bx = x, by = y;
    for (int k = 0; k < 5; k++) {
        int nx = x + MX[k], ny = y + MY[k];
        if (isWall(nx, ny)) continue;
        int d = distTo(fld, nx, ny);
        if (d < best) { best = d; bx = nx; by = ny; }
    }
    return {bx, by};
}

static void initWorld(const Game& g) {
    SZ = g.size; NTEAMS = g.teams; ME = g.me; TURNS = g.turns; FULLHP = g.hp;
    WALL.assign(SZ, vector<char>(SZ, '.'));
    for (int y = 0; y < SZ; y++)
        for (int x = 0; x < SZ; x++)
            WALL[y][x] = g.map[y][x];
    SPOTS = g.spots;
    ALLBASE.assign(NTEAMS, {});
    SPAWNORD.assign(NTEAMS, {});
    int cc = SZ / 2;
    for (int t = 0; t < NTEAMS; t++) {
        auto [cx, cy] = g.bases[t];
        vector<pair<int,int>> cells;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                cells.push_back({cx + dx, cy + dy});
        ALLBASE[t] = cells;
        vector<pair<int,int>> ord = cells;
        sort(ord.begin(), ord.end(), [&](auto a, auto b) {
            int da = manh(a.first, a.second, cc, cc);
            int db = manh(b.first, b.second, cc, cc);
            if (da != db) return da < db;
            int ta = abs(a.first - cx) + 2 * abs(a.second - cy);
            int tb = abs(b.first - cx) + 2 * abs(b.second - cy);
            return ta < tb;
        });
        SPAWNORD[t] = ord;
    }
    MYBASE = ALLBASE[ME];
    BASEDIST = bfsMulti(MYBASE);
    SPOTDIST.clear();
    for (auto& s : SPOTS) SPOTDIST.push_back(bfsMulti({s}));
}

// ============================== DECISION ==================================
struct Live {
    int uid, slot, x, y, hp, flag;
    bool spawning;
};
struct Enemy { int uid, x, y, hp, flag; };

static string decide(const Game& g) {
    static bool inited = false;
    if (!inited) { initWorld(g); inited = true; }

    // --- live units / respawns ---
    vector<pair<int,int>> occ;
    for (auto& u : g.units) if (u.x >= 0) occ.push_back({u.x, u.y});
    auto isOcc = [&](int x, int y) {
        for (auto& p : occ) if (p.first == x && p.second == y) return true;
        return false;
    };
    vector<pair<int,int>> spawnPos(g.units.size(), {-1, -1});
    for (auto& u : g.units) {
        if (u.x >= 0) spawnPos[u.id] = {u.x, u.y};
        else if (u.respawn_at == g.turn) {
            for (auto& p : SPAWNORD[u.team]) {
                if (!isOcc(p.first, p.second)) {
                    spawnPos[u.id] = p;
                    occ.push_back(p);
                    break;
                }
            }
        }
    }

    vector<Live> mine;
    vector<Enemy> enemies;
    for (auto& u : g.units) {
        if (spawnPos[u.id].first < 0) continue;
        if (u.team == ME) {
            int hp = (u.respawn_at == g.turn) ? FULLHP : u.hp;
            mine.push_back({u.id, u.id - 3 * ME, spawnPos[u.id].first, spawnPos[u.id].second,
                            hp, u.flag, u.respawn_at == g.turn});
        } else {
            enemies.push_back({u.id, spawnPos[u.id].first, spawnPos[u.id].second, u.hp, u.flag});
        }
    }

    auto idle = [&]() {
        string out = to_string(g.turn);
        for (int k = 0; k < 3; k++) out += " S -";
        return out;
    };
    if (mine.empty()) return idle();

    // --- flags ---
    vector<pair<int,int>> freeFlags;
    vector<int> freeFlagId;
    for (auto& f : g.flags) {
        if (f.status == "home" || f.status == "dropped") {
            freeFlags.push_back({f.x, f.y});
            freeFlagId.push_back(f.id);
        }
    }

    unordered_map<int, vector<vector<int>>> flagFields;
    for (int j = 0; j < (int)freeFlags.size(); j++)
        flagFields[freeFlagId[j]] = bfsMulti({freeFlags[j]});

    int n = (int)mine.size();

    // --- predict enemy positions ---
    vector<pair<int,int>> enemyPos(enemies.size());
    for (int ei = 0; ei < (int)enemies.size(); ei++) {
        auto& e = enemies[ei];
        if (e.flag >= 0) {
            auto fld = bfsMulti(ALLBASE[e.uid / 3]);
            enemyPos[ei] = bestStep(fld, e.x, e.y);
        } else {
            int bestD = 1 << 28;
            pair<int,int> tgt = {e.x, e.y};
            for (auto& [fx, fy] : freeFlags) {
                int dd = manh(e.x, e.y, fx, fy);
                if (dd < bestD) { bestD = dd; tgt = {fx, fy}; }
            }
            if (bestD < 1 << 27) {
                auto fld = bfsMulti({tgt});
                enemyPos[ei] = bestStep(fld, e.x, e.y);
            } else {
                enemyPos[ei] = {e.x, e.y};
            }
        }
    }

    // --- assign goals ---
    struct Plan {
        int kind;   // 0 home, 1 flag, 2 hunt, 3 spot, 4 escort
        const vector<vector<int>>* fld;
        int tx, ty;
    };
    vector<Plan> plans(n);
    vector<char> assigned(n, 0);
    vector<unique_ptr<vector<vector<int>>>> owned;

    // 1. carriers -> home
    for (int i = 0; i < n; i++) {
        if (mine[i].flag >= 0) {
            plans[i] = {0, &BASEDIST, -1, -1};
            assigned[i] = 1;
        }
    }

    // 2. hunt enemy carrier
    {
        int bestUid = -1, bestRem = 1 << 28;
        for (auto& e : enemies) {
            if (e.flag < 0) continue;
            auto fld = bfsMulti(ALLBASE[e.uid / 3]);
            int rem = distTo(fld, e.x, e.y);
            if (rem < bestRem) { bestRem = rem; bestUid = e.uid; }
        }
        // hunt only if no free flags or enemy is about to score (adapt by team count)
        bool anyFreeFlag = !freeFlags.empty();
        int huntRange = (NTEAMS <= 2) ? 28 : (NTEAMS <= 3 ? 20 : 12);
        // in multi-team with free flags available, skip hunting entirely
        if (NTEAMS >= 5 && anyFreeFlag) huntRange = 0;
        if (bestUid >= 0 && bestRem <= huntRange) {
            Enemy* pe = nullptr;
            for (auto& e : enemies) if (e.uid == bestUid) pe = &e;
            if (pe) {
                int maxHunt = (NTEAMS <= 2) ? 2 : 1;
                for (int h = 0; h < maxHunt; h++) {
                    int bestI = -1, bestD = 1 << 28;
                    for (int i = 0; i < n; i++) {
                        if (assigned[i]) continue;
                        int dd = manh(mine[i].x, mine[i].y, pe->x, pe->y);
                        if (dd < bestD) { bestD = dd; bestI = i; }
                    }
                    if (bestI < 0) break;
                    auto fld = make_unique<vector<vector<int>>>(bfsMulti({{pe->x, pe->y}}));
                    plans[bestI] = {2, fld.get(), pe->x, pe->y};
                    owned.push_back(move(fld));
                    assigned[bestI] = 1;
                }
            }
        }
    }

    // 3. fetch flags — race-aware, consider round-trip
    {
        struct Cand { int i, j, cost; };
        vector<Cand> cands;
        for (int i = 0; i < n; i++) {
            if (assigned[i]) continue;
            for (int j = 0; j < (int)freeFlags.size(); j++) {
                int fid = freeFlagId[j];
                int myd = distTo(flagFields[fid], mine[i].x, mine[i].y);
                int ed = 1 << 28;
                for (auto& e : enemies) ed = min(ed, distTo(flagFields[fid], e.x, e.y));
                // round-trip: pickup distance + return distance to base
                int ret = distTo(BASEDIST, freeFlags[j].first, freeFlags[j].second);
                int cost = myd + ret / 2 + max(0, myd - min(ed, 30)) * 6;
                if (g.flags[fid].status == "dropped") cost -= 4;
                cands.push_back({i, j, cost});
            }
        }
        sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.cost < b.cost; });
        vector<char> flagTaken(freeFlags.size(), 0);
        for (auto& c : cands) {
            if (assigned[c.i] || flagTaken[c.j]) continue;
            int fid = freeFlagId[c.j];
            plans[c.i] = {1, &flagFields[fid], freeFlags[c.j].first, freeFlags[c.j].second};
            assigned[c.i] = 1;
            flagTaken[c.j] = 1;
        }
        for (int i = 0; i < n; i++) {
            if (assigned[i] || freeFlags.empty()) continue;
            int bestJ = 0, bestD = 1 << 28;
            for (int j = 0; j < (int)freeFlags.size(); j++) {
                int dd = distTo(flagFields[freeFlagId[j]], mine[i].x, mine[i].y);
                if (dd < bestD) { bestD = dd; bestJ = j; }
            }
            plans[i] = {1, &flagFields[freeFlagId[bestJ]], freeFlags[bestJ].first, freeFlags[bestJ].second};
            assigned[i] = 1;
        }
    }

    // 4. escort / spot
    for (int i = 0; i < n; i++) {
        if (assigned[i]) continue;
        int bestJ = -1, bestD = 1 << 28;
        for (int j = 0; j < n; j++) {
            if (mine[j].flag < 0) continue;
            int dd = manh(mine[i].x, mine[i].y, mine[j].x, mine[j].y);
            if (dd < bestD) { bestD = dd; bestJ = j; }
        }
        if (bestJ >= 0 && bestD <= 16) {
            auto fld = make_unique<vector<vector<int>>>(bfsMulti({{mine[bestJ].x, mine[bestJ].y}}));
            plans[i] = {4, fld.get(), mine[bestJ].x, mine[bestJ].y};
            owned.push_back(move(fld));
        } else {
            // no carrier to escort: go to nearest flag spot (or center for flexibility)
            int bi = -1, bd = 1 << 28;
            // prefer a free flag spot (flag will spawn there eventually)
            for (int s = 0; s < (int)SPOTS.size(); s++) {
                int dd = distTo(SPOTDIST[s], mine[i].x, mine[i].y);
                int ret = distTo(BASEDIST, SPOTS[s].first, SPOTS[s].second);
                int total = dd + ret / 3;
                if (total < bd) { bd = total; bi = s; }
            }
            if (bi >= 0) {
                plans[i] = {3, &SPOTDIST[bi], SPOTS[bi].first, SPOTS[bi].second};
            } else {
                int cc = SZ / 2;
                auto fld = make_unique<vector<vector<int>>>(bfsMulti({{cc, cc}}));
                plans[i] = {3, fld.get(), cc, cc};
                owned.push_back(move(fld));
            }
        }
        assigned[i] = 1;
    }

    // --- decide movement ---
    vector<pair<int,int>> myPos(n);
    for (int i = 0; i < n; i++) myPos[i] = {mine[i].x, mine[i].y};

    vector<pair<int,int>> desired(n);
    for (int i = 0; i < n; i++)
        desired[i] = bestStep(*plans[i].fld, mine[i].x, mine[i].y);

    // stay on a free flag to pick up
    for (int i = 0; i < n; i++) {
        if (mine[i].flag >= 0) continue;
        for (auto& [fx, fy] : freeFlags) {
            if (fx == mine[i].x && fy == mine[i].y) {
                desired[i] = {mine[i].x, mine[i].y};
                break;
            }
        }
    }

    auto prio = [&](int i) {
        switch (plans[i].kind) {
            case 0: return 5;
            case 1: return 4;
            case 2: return 3;
            case 4: return 2;
            default: return 1;
        }
    };
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (desired[i] == desired[j]) {
                int loser = (prio(i) >= prio(j)) ? j : i;
                int winner = (loser == i) ? j : i;
                auto alt = myPos[loser];
                int bestD = 1 << 28;
                for (int k = 0; k < 5; k++) {
                    int nx = myPos[loser].first + MX[k], ny = myPos[loser].second + MY[k];
                    if (isWall(nx, ny)) continue;
                    if (make_pair(nx, ny) == desired[winner]) continue;
                    int d = distTo(*plans[loser].fld, nx, ny);
                    if (d < bestD) { bestD = d; alt = {nx, ny}; }
                }
                desired[loser] = alt;
            }
        }
    }
    for (int i = 0; i < n; i++) {
        if (isWall(desired[i].first, desired[i].second)) desired[i] = myPos[i];
        for (auto& e : enemies) {
            if (desired[i].first == e.x && desired[i].second == e.y) {
                desired[i] = myPos[i];
                break;
            }
        }
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (desired[i] == desired[j]) {
                if (prio(i) >= prio(j)) desired[j] = myPos[j];
                else desired[i] = myPos[i];
            }

    auto moveTo = [&](int i) -> char {
        auto [x, y] = myPos[i];
        auto [tx, ty] = desired[i];
        if (tx == x && ty == y) return 'S';
        if (tx == x && ty == y - 1) return 'U';
        if (tx == x && ty == y + 1) return 'D';
        if (tx == x - 1 && ty == y) return 'L';
        if (tx == x + 1 && ty == y) return 'R';
        return 'S';
    };

    // --- actions ---
    vector<string> actions(n, "-");

    // pickups
    for (int i = 0; i < n; i++) {
        if (mine[i].flag >= 0) continue;
        for (auto& [fx, fy] : freeFlags) {
            if (fx == desired[i].first && fy == desired[i].second) {
                actions[i] = "P";
                break;
            }
        }
    }

    // attacks: aggressive focus fire
    {
        struct Att { int i, ei; };
        vector<Att> atts;
        for (int i = 0; i < n; i++) {
            if (mine[i].flag >= 0 || mine[i].spawning) continue;
            if (actions[i] == "P") continue;
            auto [ax, ay] = desired[i];
            for (int ei = 0; ei < (int)enemies.size(); ei++) {
                bool hit = canAttack(ax, ay, enemyPos[ei].first, enemyPos[ei].second) ||
                           canAttack(ax, ay, enemies[ei].x, enemies[ei].y);
                if (hit) atts.push_back({i, ei});
            }
        }
        if (!atts.empty()) {
            int bestTarget = -1, bestVal = 0;
            for (int ei = 0; ei < (int)enemies.size(); ei++) {
                int cnt = 0;
                for (auto& a : atts) if (a.ei == ei) cnt++;
                if (cnt == 0) continue;
                int dmg = cnt * 34;
                int v = dmg * 3;
                if (dmg >= enemies[ei].hp) v += 800;
                if (enemies[ei].flag >= 0) v += 500;
                // extra value for enemies threatening my carrier
                for (int i = 0; i < n; i++) {
                    if (mine[i].flag < 0) continue;
                    if (manh(enemies[ei].x, enemies[ei].y, mine[i].x, mine[i].y) <= 4) {
                        v += 400;
                        break;
                    }
                }
                if (v > bestVal) { bestVal = v; bestTarget = ei; }
            }
            if (bestTarget >= 0) {
                for (auto& a : atts) {
                    if (a.ei == bestTarget && actions[a.i] == "-") {
                        actions[a.i] = to_string(enemies[bestTarget].uid);
                    }
                }
            }
        }
    }

    // --- format output ---
    string out = to_string(g.turn);
    for (int k = 0; k < 3; k++) {
        int uid = 3 * ME + k;
        int idx = -1;
        for (int i = 0; i < n; i++) if (mine[i].uid == uid) { idx = i; break; }
        if (idx < 0) { out += " S -"; continue; }
        out += " ";
        out += moveTo(idx);
        out += " ";
        out += actions[idx];
    }
    return out;
}

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    Game g;
    while (read_turn(g)) {
        cout << decide(g) << endl;
    }
    return 0;
}
