// 夺旗 bot v8 - 修复路径，优化战斗
// 策略：
// 1. 跑旗手：最近的单位去拿最近的旗，拿了回基地
// 2. 战士：保护跑旗手，拦截敌方携旗者
// 3. 集中火力：优先击杀携旗敌人
// 4. 风筝战术：保持距离2攻击
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

// BFS 从起点出发，返回距离场
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

// 从距离场（从起点出发的BFS）获取下一步方向到目标
char step_toward(const Game& g, const vector<vector<int>>& d, int sx, int sy, int tx, int ty) {
    if (sx == tx && sy == ty) return 'S';
    if (d[ty][tx] == -1 || d[sy][sx] == -1) return 'S';
    
    // 从目标往回走，找到距离起点1步的位置
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
    
    // cx,cy 是距离起点1步的位置
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
    int cx = g.size / 2, cy = g.size / 2;
    
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
    
    // 敌方携旗者
    vector<Unit> enemy_carriers;
    for (auto& e : enemies) if (e.flag != -1) enemy_carriers.push_back(e);
    
    // ===== 确定角色 =====
    int runner = -1;
    vector<int> fighters;
    
    // 已经携旗的就是runner
    for (int i = 0; i < 3; i++) {
        if (mine[i].x != -1 && mine[i].flag != -1) {
            runner = i;
        } else if (mine[i].x != -1) {
            fighters.push_back(i);
        }
    }
    
    // 如果没人携旗，选离最近旗子最近的作为runner
    if (runner == -1 && !avail_flags.empty()) {
        int best_d = INT_MAX;
        for (int i = 0; i < 3; i++) {
            if (mine[i].x == -1) continue;
            for (auto& f : avail_flags) {
                int d = dist[i][f.y][f.x];
                if (d != -1 && d < best_d) {
                    best_d = d;
                    runner = i;
                }
            }
        }
        // 更新fighters
        fighters.clear();
        for (int i = 0; i < 3; i++) {
            if (mine[i].x != -1 && i != runner) fighters.push_back(i);
        }
    }
    
    // ===== 集中火力目标 =====
    int focus_target = -1;
    int focus_priority = -1;
    
    if (!fighters.empty()) {
        for (auto& e : enemies) {
            int num_can_hit = 0;
            for (int fi : fighters) {
                if (can_attack(g, mine[fi].x, mine[fi].y, e.x, e.y)) {
                    num_can_hit++;
                }
            }
            if (num_can_hit == 0) continue;
            
            int pri = 0;
            if (e.flag != -1) pri += 1000;
            pri += (100 - e.hp) * 5;
            pri += num_can_hit * 200;
            
            if (pri > focus_priority) {
                focus_priority = pri;
                focus_target = e.id;
            }
        }
    }
    
    // ===== Runner决策 =====
    if (runner != -1) {
        int i = runner;
        auto& u = mine[i];
        
        if (u.flag != -1) {
            // 携旗：回基地
            if (in_my_base(g, u.x, u.y)) {
                notes[i] = "交旗";
            } else {
                auto [tx, ty] = best_base_cell(g, dist[i]);
                moves[i] = step_toward(g, dist[i], u.x, u.y, tx, ty);
                notes[i] = "回基地";
            }
        } else {
            // 没携旗：去最近的旗
            int best_fd = INT_MAX;
            Flag best_flag = {-1, "", -1, -1, -1, -1};
            for (auto& f : avail_flags) {
                int d = dist[i][f.y][f.x];
                if (d != -1 && d < best_fd) { best_fd = d; best_flag = f; }
            }
            
            if (best_flag.id != -1) {
                if (best_fd == 0) {
                    actions[i] = "P";
                    notes[i] = "拾旗";
                } else {
                    moves[i] = step_toward(g, dist[i], u.x, u.y, best_flag.x, best_flag.y);
                    notes[i] = "跑旗";
                }
            }
        }
    }
    
    // ===== Fighters决策 =====
    for (int fi : fighters) {
        auto& u = mine[fi];
        if (u.x == -1) continue;
        
        bool did_action = false;
        
        // 1. 如果有集中火力目标且能打到，就攻击
        if (focus_target != -1) {
            Unit target;
            for (auto& e : enemies) if (e.id == focus_target) target = e;
            
            if (can_attack(g, u.x, u.y, target.x, target.y)) {
                actions[fi] = to_string(focus_target);
                // 风筝：如果敌人距离1且我们血量低，边打边退
                int d = mdist(u.x, u.y, target.x, target.y);
                if (u.hp <= 67 && d <= 1) {
                    moves[fi] = move_away(g, u.x, u.y, target.x, target.y);
                    notes[fi] = "边打边退";
                } else {
                    moves[fi] = 'S';
                    notes[fi] = "集火";
                }
                did_action = true;
            }
        }
        
        if (!did_action) {
            // 2. 决定移动目标
            int target_x = -1, target_y = -1;
            string note = "";
            
            // a. 保护runner
            if (runner != -1 && mine[runner].flag != -1) {
                auto& run = mine[runner];
                // 找runner附近最近的敌人
                int nearest_d = INT_MAX;
                Unit nearest_e;
                for (auto& e : enemies) {
                    int d = mdist(run.x, run.y, e.x, e.y);
                    if (d < nearest_d) { nearest_d = d; nearest_e = e; }
                }
                
                if (nearest_e.id != -1 && nearest_d < 8) {
                    // 有敌人靠近，去拦截
                    target_x = nearest_e.x;
                    target_y = nearest_e.y;
                    note = "护旗";
                } else {
                    // 跟在runner附近
                    int my_dist_to_runner = dist[fi][run.y][run.x];
                    if (my_dist_to_runner > 3) {
                        target_x = run.x;
                        target_y = run.y;
                        note = "护送";
                    } else {
                        // 已经在附近，向最近的敌人移动
                        if (nearest_e.id != -1) {
                            target_x = nearest_e.x;
                            target_y = nearest_e.y;
                            note = "警戒";
                        }
                    }
                }
            }
            // b. 追击敌方携旗者
            else if (!enemy_carriers.empty()) {
                int best_d = INT_MAX;
                Unit best_c;
                for (auto& ec : enemy_carriers) {
                    int d = dist[fi][ec.y][ec.x];
                    if (d != -1 && d < best_d) { best_d = d; best_c = ec; }
                }
                if (best_c.id != -1) {
                    target_x = best_c.x;
                    target_y = best_c.y;
                    note = "拦截";
                }
            }
            // c. 向旗子移动
            else if (!avail_flags.empty()) {
                int best_d = INT_MAX;
                Flag best_f;
                for (auto& f : avail_flags) {
                    int d = dist[fi][f.y][f.x];
                    if (d != -1 && d < best_d) { best_d = d; best_f = f; }
                }
                if (best_f.id != -1 && best_d > 0) {
                    target_x = best_f.x;
                    target_y = best_f.y;
                    note = "靠旗";
                }
            }
            // d. 向中心移动
            else {
                int cd = dist[fi][cy][cx];
                if (cd != -1 && cd > 3) {
                    target_x = cx;
                    target_y = cy;
                    note = "推进";
                } else {
                    // 找最近的敌人
                    int best_d = INT_MAX;
                    Unit best_e;
                    for (auto& e : enemies) {
                        int d = dist[fi][e.y][e.x];
                        if (d != -1 && d < best_d) { best_d = d; best_e = e; }
                    }
                    if (best_e.id != -1 && best_d < 10) {
                        target_x = best_e.x;
                        target_y = best_e.y;
                        note = "接敌";
                    } else {
                        note = "待命";
                    }
                }
            }
            
            if (target_x != -1) {
                moves[fi] = step_toward(g, dist[fi], u.x, u.y, target_x, target_y);
                notes[fi] = note;
            } else {
                notes[fi] = note;
            }
        }
        
        // 低血量强制撤退
        if (u.hp <= 34) {
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
                moves[fi] = move_away(g, u.x, u.y, nex, ney);
                actions[fi] = "-";
                notes[fi] = "撤退";
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
