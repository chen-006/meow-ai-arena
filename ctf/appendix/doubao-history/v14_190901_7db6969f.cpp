// 夺旗 bot v12 - 全单位跑旗 + 战斗
// 策略：
// - 所有单位都可以拿旗，谁近谁去
// - 遇到敌人就打，优先打携旗敌人
// - 低血量撤退
// - 风筝战术：保持距离2攻击
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

// 找最佳攻击目标
int find_best_target(const Game& g, int x, int y, const vector<Unit>& enemies, int& priority) {
    priority = -1;
    int best_id = -1;
    for (auto& e : enemies) {
        if (!can_attack(g, x, y, e.x, e.y)) continue;
        int pri = 0;
        if (e.flag != -1) pri += 1000; // 携旗者最高优先级
        pri += (100 - e.hp) * 5; // 血量低优先
        pri += (2 - mdist(x, y, e.x, e.y)) * 10; // 近的优先
        if (pri > priority) { priority = pri; best_id = e.id; }
    }
    return best_id;
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
    
    // 任务分配：给每个没携旗的单位分配一个旗子目标
    // 贪心：最近的单位分配最近的旗
    vector<int> flag_target(3, -1); // 每个单位的目标旗子索引
    vector<bool> flag_taken(avail_flags.size(), false);
    
    // 先处理携旗的单位
    vector<bool> has_flag(3, false);
    for (int i = 0; i < 3; i++) {
        if (mine[i].x != -1 && mine[i].flag != -1) {
            has_flag[i] = true;
        }
    }
    
    // 构建 (距离, 单位索引, 旗子索引) 三元组
    vector<tuple<int, int, int>> assignments;
    for (int i = 0; i < 3; i++) {
        if (mine[i].x == -1 || has_flag[i]) continue;
        for (int j = 0; j < (int)avail_flags.size(); j++) {
            int d = dist[i][avail_flags[j].y][avail_flags[j].x];
            if (d != -1) {
                assignments.emplace_back(d, i, j);
            }
        }
    }
    sort(assignments.begin(), assignments.end());
    
    for (auto& [d, ui, fi] : assignments) {
        if (flag_target[ui] != -1 || flag_taken[fi]) continue;
        flag_target[ui] = fi;
        flag_taken[fi] = true;
    }
    
    // 对每个单位决策
    for (int i = 0; i < 3; i++) {
        auto& u = mine[i];
        if (u.x == -1) continue;
        
        // 找最佳攻击目标
        int attack_pri;
        int target = find_best_target(g, u.x, u.y, enemies, attack_pri);
        
        // 携旗：回基地
        if (u.flag != -1) {
            if (in_my_base(g, u.x, u.y)) {
                notes[i] = "交旗";
            } else {
                auto [tx, ty] = best_base_cell(g, dist[i]);
                moves[i] = step_toward(g, dist[i], u.x, u.y, tx, ty);
                notes[i] = "回基地";
                
                // 如果有敌人在攻击范围内，且我们血量低，考虑边跑边打？
                // 不行，携旗不能攻击
            }
            continue;
        }
        
        // 没携旗
        bool did_action = false;
        
        // 1. 如果有好的攻击目标，攻击
        if (target != -1) {
            Unit target_unit;
            for (auto& e : enemies) if (e.id == target) target_unit = e;
            
            // 判断是否应该攻击
            bool should_attack = false;
            if (target_unit.flag != -1) should_attack = true; // 携旗者必打
            else if (target_unit.hp <= 34) should_attack = true; // 能击杀
            else if (u.hp > 67) should_attack = true; // 血量健康
            else if (attack_pri > 300) should_attack = true;
            
            if (should_attack) {
                actions[i] = to_string(target);
                int d = mdist(u.x, u.y, target_unit.x, target_unit.y);
                // 风筝战术：
                // - 如果敌人距离1且我们血量低，边打边退
                // - 如果敌人距离2，站着打
                // - 如果敌人距离>2，靠近
                if (d == 1 && u.hp <= 67) {
                    moves[i] = move_away(g, u.x, u.y, target_unit.x, target_unit.y);
                    notes[i] = "边打边退";
                } else {
                    moves[i] = 'S';
                    notes[i] = "攻击";
                }
                did_action = true;
            }
        }
        
        if (!did_action) {
            // 2. 去目标旗子
            if (flag_target[i] != -1) {
                auto& f = avail_flags[flag_target[i]];
                int d = dist[i][f.y][f.x];
                if (d == 0) {
                    actions[i] = "P";
                    notes[i] = "拾旗";
                } else {
                    moves[i] = step_toward(g, dist[i], u.x, u.y, f.x, f.y);
                    if (d == 1) actions[i] = "P";
                    notes[i] = "拿旗";
                }
                did_action = true;
            }
        }
        
        if (!did_action) {
            // 3. 没有目标旗子，找最近的敌方携旗者去追
            int best_d = INT_MAX;
            Unit best_carrier;
            for (auto& e : enemies) {
                if (e.flag == -1) continue;
                int d = dist[i][e.y][e.x];
                if (d != -1 && d < best_d) { best_d = d; best_carrier = e; }
            }
            if (best_carrier.id != -1 && best_d < 15) {
                moves[i] = step_toward(g, dist[i], u.x, u.y, best_carrier.x, best_carrier.y);
                notes[i] = "追携旗";
                did_action = true;
            }
        }
        
        if (!did_action) {
            // 4. 向中心移动
            int cx = g.size / 2, cy = g.size / 2;
            int cd = dist[i][cy][cx];
            if (cd != -1 && cd > 3) {
                moves[i] = step_toward(g, dist[i], u.x, u.y, cx, cy);
                notes[i] = "推进";
            } else {
                // 找最近的敌人
                int best_d = INT_MAX;
                Unit best_e;
                for (auto& e : enemies) {
                    int d = dist[i][e.y][e.x];
                    if (d != -1 && d < best_d) { best_d = d; best_e = e; }
                }
                if (best_e.id != -1 && best_d < 10) {
                    moves[i] = step_toward(g, dist[i], u.x, u.y, best_e.x, best_e.y);
                    notes[i] = "接敌";
                } else {
                    notes[i] = "待命";
                }
            }
        }
        
        // 低血量强制撤退（再被打一次就死）
        if (u.hp <= 34 && !has_flag[i]) {
            int nearest_d = INT_MAX;
            int nex = -1, ney = -1;
            for (auto& e : enemies) {
                int d = mdist(u.x, u.y, e.x, e.y);
                if (d <= 3 && d < nearest_d) {
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
