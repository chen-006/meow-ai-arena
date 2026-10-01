// 夺旗 bot v13 - 专注拿旗，避免阻塞
// 策略：
// - 优先拿旗，战斗是次要的
// - 给每个单位分配不同的旗子，避免阻塞
// - 遇到敌人只有在能快速击杀或必须防守时才打
#include <bits/stdc++.h>
using namespace std;

struct Unit { int id, team, x, y, hp, flag, respawn_at; };
struct Flag { int id; string status; int x, int y, holder, return_at; };
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

inline int mdist(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}

inline bool is_wall(const Game& g, int x, int y) {
    return x < 0 || y < 0 || x >= g.size || y >= g.size || g.map[y][x] == '#';
}

vector<vector<int>> bfs_from(const Game& g, int sx, int sy) {
    vector<vector<int>> d(g.size, vector<int>(g.size, -1));
    if (sx < 0 || sy < 0) return d;
    queue<pair<int,int>> q;
    d[sy][sx] = 0;
    q.push({sx, sy});
    static const int dx[] = {0, 0, -1, 1};
    static const int dy[] = {-1, 1, 0, 0};
    while (!q.empty()) {
        auto [x, y] = q.front(); q.pop();
        for (int di = 0; di < 4; di++) {
            int nx = x + dx[di], ny = y + dy[di];
            if (is_wall(g, nx, ny) || d[ny][nx] != -1) continue;
            d[ny][nx] = d[y][x] + 1;
            q.push({nx, ny});
        }
    }
    return d;
}

char step_toward(const Game& g, const vector<vector<int>>& d, int sx, int sy, int tx, int ty) {
    if (sx == tx && sy == ty) return 'S';
    if (d[ty][tx] == -1 || d[sy][sx] == -1) return 'S';
    
    static const int dx[] = {0, 0, -1, 1};
    static const int dy[] = {-1, 1, 0, 0};
    
    int cx = tx, cy = ty;
    while (d[cy][cx] > 1) {
        bool found = false;
        for (int di = 0; di < 4; di++) {
            int nx = cx + dx[di], ny = cy + dy[di];
            if (!is_wall(g, nx, ny) && d[ny][nx] == d[cy][cx] - 1) {
                cx = nx; cy = ny;
                found = true;
                break;
            }
        }
        if (!found) break;
    }
    
    if (cx == sx && cy == sy - 1) return 'U';
    if (cx == sx && cy == sy + 1) return 'D';
    if (cx == sx - 1 && cy == sy) return 'L';
    if (cx == sx + 1 && cy == sy) return 'R';
    return 'S';
}

bool can_attack(const Game& g, int ax, int ay, int tx, int ty) {
    if (mdist(ax, ay, tx, ty) > 2) return false;
    int dx = tx - ax, dy = ty - ay;
    if (abs(dx) == 2 && dy == 0) return !is_wall(g, ax + dx/2, ay);
    if (abs(dy) == 2 && dx == 0) return !is_wall(g, ax, ay + dy/2);
    if (abs(dx) == 1 && abs(dy) == 1) return !is_wall(g, ax + dx, ay) || !is_wall(g, ax, ay + dy);
    return true;
}

bool in_my_base(const Game& g, int x, int y) {
    auto [bx, by] = g.bases[g.me];
    return abs(x - bx) <= 1 && abs(y - by) <= 1;
}

pair<int,int> best_base_cell(const Game& g, const vector<vector<int>>& dist) {
    auto [bx, by] = g.bases[g.me];
    int best_d = INT_MAX;
    pair<int,int> best = {bx, by};
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = bx + dx, ny = by + dy;
            if (is_wall(g, nx, ny)) continue;
            if (dist[ny][nx] != -1 && dist[ny][nx] < best_d) {
                best_d = dist[ny][nx];
                best = {nx, ny};
            }
        }
    }
    return best;
}

char move_away(const Game& g, int x, int y, int tx, int ty) {
    static const int dx[] = {0, 0, -1, 1};
    static const int dy[] = {-1, 1, 0, 0};
    static const char dirs[] = {'U', 'D', 'L', 'R'};
    int best_d = mdist(x, y, tx, ty);
    char best = 'S';
    for (int d = 0; d < 4; d++) {
        int nx = x + dx[d], ny = y + dy[d];
        if (is_wall(g, nx, ny)) continue;
        int nd = mdist(nx, ny, tx, ty);
        if (nd > best_d) { best_d = nd; best = dirs[d]; }
    }
    return best;
}

string decide(const Game& g) {
    vector<char> moves(3, 'S');
    vector<string> actions(3, "-");
    vector<string> notes(3, "");
    
    auto [bx, by] = g.bases[g.me];
    
    vector<Unit> mine;
    for (auto& u : g.units) if (u.team == g.me) mine.push_back(u);
    
    vector<Unit> enemies;
    for (auto& u : g.units) if (u.team != g.me && u.x != -1) enemies.push_back(u);
    
    // 预计算距离场
    vector<vector<vector<int>>> dist(3);
    for (int i = 0; i < 3; i++) {
        if (mine[i].x != -1) {
            dist[i] = bfs_from(g, mine[i].x, mine[i].y);
        }
    }
    
    // 可用旗子
    vector<Flag> avail_flags;
    for (auto& f : g.flags) {
        if (f.status == "home" || f.status == "dropped") {
            avail_flags.push_back(f);
        }
    }
    
    // 任务分配：用匈牙利式贪心给每个单位分配目标
    // 目标类型：旗子、敌方携旗者、基地
    // 每个单位分配一个唯一目标
    
    vector<int> unit_target(3, -1); // -1 = 未分配
    vector<bool> flag_taken(avail_flags.size(), false);
    
    // 先处理已经携旗的单位（目标：基地）
    for (int i = 0; i < 3; i++) {
        if (mine[i].x != -1 && mine[i].flag != -1) {
            unit_target[i] = -2; // -2 = 回基地
        }
    }
    
    // 给剩下的单位分配旗子
    // 构建所有 (距离, 单位, 旗子) 组合
    vector<tuple<int, int, int>> flag_assignments;
    for (int i = 0; i < 3; i++) {
        if (mine[i].x == -1 || unit_target[i] != -1) continue;
        for (int j = 0; j < (int)avail_flags.size(); j++) {
            int d = dist[i][avail_flags[j].y][avail_flags[j].x];
            if (d != -1) {
                flag_assignments.emplace_back(d, i, j);
            }
        }
    }
    sort(flag_assignments.begin(), flag_assignments.end());
    
    for (auto& [d, ui, fi] : flag_assignments) {
        if (unit_target[ui] != -1 || flag_taken[fi]) continue;
        unit_target[ui] = fi; // >= 0 = 旗子索引
        flag_taken[fi] = true;
    }
    
    // 对每个单位决策
    for (int i = 0; i < 3; i++) {
        auto& u = mine[i];
        if (u.x == -1) continue;
        
        // 找最佳攻击目标
        int best_attack_pri = -1;
        int best_attack_id = -1;
        Unit best_attack_unit;
        for (auto& e : enemies) {
            if (!can_attack(g, u.x, u.y, e.x, e.y)) continue;
            int pri = 0;
            if (e.flag != -1) pri += 1000;
            pri += (100 - e.hp) * 3;
            if (pri > best_attack_pri) {
                best_attack_pri = pri;
                best_attack_id = e.id;
                best_attack_unit = e;
            }
        }
        
        // 携旗：回基地
        if (u.flag != -1) {
            if (in_my_base(g, u.x, u.y)) {
                notes[i] = "交旗";
            } else {
                auto [tx, ty] = best_base_cell(g, dist[i]);
                moves[i] = step_toward(g, dist[i], u.x, u.y, tx, ty);
                notes[i] = "回基地";
            }
            continue;
        }
        
        // 没携旗
        bool did_action = false;
        
        // 1. 如果有非常好的攻击目标（携旗敌人或能击杀），攻击
        if (best_attack_id != -1) {
            bool should_attack = false;
            if (best_attack_unit.flag != -1) should_attack = true;
            else if (best_attack_unit.hp <= 34) should_attack = true; // 能击杀
            
            if (should_attack) {
                actions[i] = to_string(best_attack_id);
                moves[i] = 'S';
                notes[i] = "攻击";
                did_action = true;
            }
        }
        
        if (!did_action && unit_target[i] >= 0) {
            // 2. 去目标旗子
            auto& f = avail_flags[unit_target[i]];
            int d = dist[i][f.y][f.x];
            if (d == 0) {
                actions[i] = "P";
                notes[i] = "拾旗";
            } else {
                moves[i] = step_toward(g, dist[i], u.x, u.y, f.x, f.y);
                if (d == 1) actions[i] = "P";
                notes[i] = "拿旗" + to_string(f.id);
            }
            did_action = true;
        }
        
        if (!did_action) {
            // 3. 没有旗子目标，找最近的敌方携旗者
            int best_d = INT_MAX;
            Unit best_carrier;
            for (auto& e : enemies) {
                if (e.flag == -1) continue;
                int d = dist[i][e.y][e.x];
                if (d != -1 && d < best_d) { best_d = d; best_carrier = e; }
            }
            if (best_carrier.id != -1 && best_d < 12) {
                moves[i] = step_toward(g, dist[i], u.x, u.y, best_carrier.x, best_carrier.y);
                notes[i] = "拦截";
                did_action = true;
            }
        }
        
        if (!did_action) {
            // 4. 回基地附近待命
            if (!in_my_base(g, u.x, u.y)) {
                int base_d = dist[i][by][bx];
                if (base_d > 5) {
                    auto [tx, ty] = best_base_cell(g, dist[i]);
                    moves[i] = step_toward(g, dist[i], u.x, u.y, tx, ty);
                    notes[i] = "回防";
                    did_action = true;
                }
            }
            if (!did_action) {
                // 在基地附近，找最近的敌人
                int best_d = INT_MAX;
                Unit best_e;
                for (auto& e : enemies) {
                    int d = dist[i][e.y][e.x];
                    if (d != -1 && d < best_d) { best_d = d; best_e = e; }
                }
                if (best_e.id != -1 && best_d < 8) {
                    moves[i] = step_toward(g, dist[i], u.x, u.y, best_e.x, best_e.y);
                    notes[i] = "迎敌";
                    did_action = true;
                } else {
                    notes[i] = "待命";
                    did_action = true;
                }
            }
        }
        
        // 低血量撤退（只有没拿旗时才撤）
        if (u.hp <= 34 && u.flag == -1) {
            int nearest_d = INT_MAX;
            int nex = -1, ney = -1;
            for (auto& e : enemies) {
                int d = mdist(u.x, u.y, e.x, e.y);
                if (d <= 4 && d < nearest_d) {
                    nearest_d = d;
                    nex = e.x;
                    ney = e.y;
                }
            }
            if (nex != -1) {
                moves[i] = move_away(g, u.x, u.y, nex, ney);
                actions[i] = "-";
                notes[i] = "撤退";
            }
        }
    }
    
    // 构建输出
    string out = to_string(g.turn);
    for (int i = 0; i < 3; i++) {
        out += " ";
        out += moves[i];
        out += " ";
        out += actions[i];
    }
    out += " # ";
    for (int i = 0; i < 3; i++) {
        if (i > 0) out += ";";
        out += notes[i];
    }
    
    return out;
}

int main() {
    ios::sync_with_stdio(false);
    Game g;
    while (read_turn(g)) {
        cout << decide(g) << endl;
    }
}
