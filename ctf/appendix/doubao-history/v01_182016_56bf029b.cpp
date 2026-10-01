// 夺旗 bot - 策略：1个跑旗手 + 1个战士 + 1个支援
// 跑旗手：去拿最近的旗，然后回基地
// 战士：在基地附近防守，攻击靠近的敌人，特别是携旗敌人
// 支援：根据局势支援跑旗手或战士
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

int dist(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}

bool is_wall(const Game& g, int x, int y) {
    if (x < 0 || y < 0 || x >= g.size || y >= g.size) return true;
    return g.map[y][x] == '#';
}

// BFS 求最短路径，返回下一步方向
char bfs_next(const Game& g, int sx, int sy, int tx, int ty) {
    if (sx == tx && sy == ty) return 'S';
    
    vector<vector<int>> d(g.size, vector<int>(g.size, -1));
    vector<vector<char>> prev(g.size, vector<char>(g.size, 0));
    queue<pair<int,int>> q;
    
    d[sy][sx] = 0;
    q.push({sx, sy});
    
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    char dirs[] = {'U', 'D', 'L', 'R'};
    
    while (!q.empty()) {
        auto [x, y] = q.front(); q.pop();
        if (x == tx && y == ty) break;
        
        for (int di = 0; di < 4; di++) {
            int nx = x + dx[di], ny = y + dy[di];
            if (is_wall(g, nx, ny)) continue;
            if (d[ny][nx] != -1) continue;
            d[ny][nx] = d[y][x] + 1;
            prev[ny][nx] = dirs[di];
            q.push({nx, ny});
        }
    }
    
    if (d[ty][tx] == -1) return 'S';
    
    int cx = tx, cy = ty;
    char first_step = 'S';
    while (prev[cy][cx] != 0) {
        first_step = prev[cy][cx];
        if (prev[cy][cx] == 'U') cy++;
        else if (prev[cy][cx] == 'D') cy--;
        else if (prev[cy][cx] == 'L') cx++;
        else if (prev[cy][cx] == 'R') cx--;
    }
    return first_step;
}

// BFS 求距离
int bfs_dist(const Game& g, int sx, int sy, int tx, int ty) {
    if (sx == tx && sy == ty) return 0;
    
    vector<vector<int>> d(g.size, vector<int>(g.size, -1));
    queue<pair<int,int>> q;
    
    d[sy][sx] = 0;
    q.push({sx, sy});
    
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    
    while (!q.empty()) {
        auto [x, y] = q.front(); q.pop();
        
        for (int di = 0; di < 4; di++) {
            int nx = x + dx[di], ny = y + dy[di];
            if (is_wall(g, nx, ny)) continue;
            if (d[ny][nx] != -1) continue;
            d[ny][nx] = d[y][x] + 1;
            if (nx == tx && ny == ty) return d[ny][nx];
            q.push({nx, ny});
        }
    }
    return INT_MAX;
}

// 是否在攻击范围内
bool can_attack(const Game& g, int ax, int ay, int tx, int ty) {
    int d = dist(ax, ay, tx, ty);
    if (d > 2) return false;
    int dx = tx - ax, dy = ty - ay;
    if (abs(dx) == 2 && dy == 0) {
        return !is_wall(g, ax + dx/2, ay);
    }
    if (abs(dy) == 2 && dx == 0) {
        return !is_wall(g, ax, ay + dy/2);
    }
    if (abs(dx) == 1 && abs(dy) == 1) {
        return !is_wall(g, ax + dx, ay) || !is_wall(g, ax, ay + dy);
    }
    return true;
}

bool in_my_base(const Game& g, int x, int y) {
    auto [bx, by] = g.bases[g.me];
    return abs(x - bx) <= 1 && abs(y - by) <= 1;
}

vector<Unit> my_units(const Game& g) {
    vector<Unit> res;
    for (auto& u : g.units) {
        if (u.team == g.me) res.push_back(u);
    }
    return res;
}

// 找最近的可拾取的旗子
pair<int, int> find_nearest_flag(const Game& g, int x, int y, int& best_d) {
    best_d = INT_MAX;
    int bx = -1, by = -1;
    
    for (auto& f : g.flags) {
        if (f.status == "home" || f.status == "dropped") {
            int d = bfs_dist(g, x, y, f.x, f.y);
            if (d < best_d) {
                best_d = d;
                bx = f.x;
                by = f.y;
            }
        }
    }
    return {bx, by};
}

// 找最近的敌方携旗者
Unit find_nearest_enemy_carrier(const Game& g, int x, int y, int& best_d) {
    best_d = INT_MAX;
    Unit best = {-1, -1, -1, -1, 0, -1, -1};
    
    for (auto& u : g.units) {
        if (u.team != g.me && u.x != -1 && u.flag != -1) {
            int d = bfs_dist(g, x, y, u.x, u.y);
            if (d < best_d) {
                best_d = d;
                best = u;
            }
        }
    }
    return best;
}

// 找最好的攻击目标
Unit find_best_attack_target(const Game& g, int x, int y, int& best_pri) {
    best_pri = -1;
    Unit best = {-1, -1, -1, -1, 0, -1, -1};
    
    for (auto& u : g.units) {
        if (u.team == g.me || u.x == -1) continue;
        if (!can_attack(g, x, y, u.x, u.y)) continue;
        
        int priority = 100;
        if (u.flag != -1) priority += 1000;
        priority += (100 - u.hp);
        
        if (priority > best_pri) {
            best_pri = priority;
            best = u;
        }
    }
    return best;
}

// 找最近的敌人
Unit find_nearest_enemy(const Game& g, int x, int y, int& best_d) {
    best_d = INT_MAX;
    Unit best = {-1, -1, -1, -1, 0, -1, -1};
    
    for (auto& u : g.units) {
        if (u.team == g.me || u.x == -1) continue;
        int d = bfs_dist(g, x, y, u.x, u.y);
        if (d < best_d) {
            best_d = d;
            best = u;
        }
    }
    return best;
}

// 向基地移动
char move_to_base(const Game& g, int x, int y) {
    auto [bx, by] = g.bases[g.me];
    int best_d = INT_MAX;
    int best_x = bx, best_y = by;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = bx + dx, ny = by + dy;
            if (is_wall(g, nx, ny)) continue;
            int d = bfs_dist(g, x, y, nx, ny);
            if (d < best_d) {
                best_d = d;
                best_x = nx;
                best_y = ny;
            }
        }
    }
    return bfs_next(g, x, y, best_x, best_y);
}

// 远离某个位置
char move_away_from(const Game& g, int x, int y, int tx, int ty) {
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    char dirs[] = {'U', 'D', 'L', 'R'};
    
    int best_d = dist(x, y, tx, ty);
    char best_dir = 'S';
    
    for (int d = 0; d < 4; d++) {
        int nx = x + dx[d], ny = y + dy[d];
        if (is_wall(g, nx, ny)) continue;
        int nd = dist(nx, ny, tx, ty);
        if (nd > best_d) {
            best_d = nd;
            best_dir = dirs[d];
        }
    }
    return best_dir;
}

string decide(const Game& g) {
    auto mine = my_units(g);
    
    vector<char> moves(3, 'S');
    vector<string> actions(3, "-");
    vector<string> notes(3, "");
    
    auto [bx, by] = g.bases[g.me];
    
    for (int i = 0; i < 3; i++) {
        auto& u = mine[i];
        
        if (u.x == -1) {
            continue;
        }
        
        int attack_pri;
        auto target = find_best_attack_target(g, u.x, u.y, attack_pri);
        
        // 携旗：回基地
        if (u.flag != -1) {
            if (in_my_base(g, u.x, u.y)) {
                notes[i] = "交旗";
            } else {
                moves[i] = move_to_base(g, u.x, u.y);
                notes[i] = "回基地";
            }
            continue;
        }
        
        // 没携旗
        if (i == 0) { // 跑旗手
            if (target.id != -1 && u.hp > 34) {
                actions[i] = to_string(target.id);
                notes[i] = "攻击";
            } else {
                int fd;
                auto [fx, fy] = find_nearest_flag(g, u.x, u.y, fd);
                if (fx != -1) {
                    if (fd == 0) {
                        actions[i] = "P";
                        notes[i] = "拾旗";
                    } else {
                        moves[i] = bfs_next(g, u.x, u.y, fx, fy);
                        notes[i] = "去拿旗";
                    }
                } else {
                    if (!in_my_base(g, u.x, u.y)) {
                        moves[i] = move_to_base(g, u.x, u.y);
                        notes[i] = "回防";
                    } else {
                        notes[i] = "待命";
                    }
                }
            }
        } else if (i == 1) { // 战士
            if (target.id != -1) {
                actions[i] = to_string(target.id);
                notes[i] = "攻击";
            } else {
                int cd;
                auto carrier = find_nearest_enemy_carrier(g, u.x, u.y, cd);
                if (carrier.id != -1 && cd < 20) {
                    moves[i] = bfs_next(g, u.x, u.y, carrier.x, carrier.y);
                    notes[i] = "拦截携旗";
                } else {
                    int base_dist = bfs_dist(g, u.x, u.y, bx, by);
                    if (base_dist > 6) {
                        moves[i] = move_to_base(g, u.x, u.y);
                        notes[i] = "回防";
                    } else {
                        int ed;
                        auto enemy = find_nearest_enemy(g, u.x, u.y, ed);
                        if (enemy.id != -1 && ed < 12) {
                            moves[i] = bfs_next(g, u.x, u.y, enemy.x, enemy.y);
                            notes[i] = "接近敌人";
                        } else {
                            int cx = g.size / 2, cy = g.size / 2;
                            int cdist = bfs_dist(g, u.x, u.y, cx, cy);
                            if (cdist > 5) {
                                moves[i] = bfs_next(g, u.x, u.y, cx, cy);
                                notes[i] = "前压";
                            } else {
                                notes[i] = "巡逻";
                            }
                        }
                    }
                }
            }
        } else { // 支援
            if (target.id != -1) {
                actions[i] = to_string(target.id);
                notes[i] = "攻击";
            } else {
                auto& runner = mine[0];
                if (runner.x != -1 && runner.flag == -1) {
                    int fd;
                    auto [fx, fy] = find_nearest_flag(g, u.x, u.y, fd);
                    if (fx != -1 && fd > 0) {
                        moves[i] = bfs_next(g, u.x, u.y, fx, fy);
                        notes[i] = "支援拿旗";
                    } else if (fx != -1 && fd == 0) {
                        actions[i] = "P";
                        notes[i] = "拾旗";
                    } else {
                        if (!in_my_base(g, u.x, u.y)) {
                            moves[i] = move_to_base(g, u.x, u.y);
                            notes[i] = "回防";
                        } else {
                            notes[i] = "待命";
                        }
                    }
                } else if (runner.x != -1 && runner.flag != -1) {
                    int rdist = bfs_dist(g, u.x, u.y, runner.x, runner.y);
                    if (rdist > 3) {
                        moves[i] = bfs_next(g, u.x, u.y, runner.x, runner.y);
                        notes[i] = "护送";
                    } else {
                        int ed;
                        auto enemy = find_nearest_enemy(g, u.x, u.y, ed);
                        if (enemy.id != -1 && ed <= 5) {
                            moves[i] = bfs_next(g, u.x, u.y, enemy.x, enemy.y);
                            notes[i] = "迎敌";
                        } else {
                            notes[i] = "警戒";
                        }
                    }
                } else {
                    int fd;
                    auto [fx, fy] = find_nearest_flag(g, u.x, u.y, fd);
                    if (fx != -1) {
                        if (fd == 0) {
                            actions[i] = "P";
                            notes[i] = "拾旗";
                        } else {
                            moves[i] = bfs_next(g, u.x, u.y, fx, fy);
                            notes[i] = "去拿旗";
                        }
                    } else {
                        moves[i] = move_to_base(g, u.x, u.y);
                        notes[i] = "回防";
                    }
                }
            }
        }
        
        // 血量低时逃跑
        if (u.hp <= 34 && target.id != -1 && u.flag == -1) {
            moves[i] = move_away_from(g, u.x, u.y, target.x, target.y);
            actions[i] = "-";
            notes[i] = "撤退";
        }
    }
    
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
