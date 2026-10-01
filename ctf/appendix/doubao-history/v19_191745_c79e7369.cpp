// 夺旗 bot v14 - 死亡球战术
// 所有单位一起行动，集中火力，先清场再拿旗
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
    
    // 找队长（第一个活着的单位）
    int leader = -1;
    for (int i = 0; i < 3; i++) {
        if (mine[i].x != -1) {
            leader = i;
            break;
        }
    }
    
    if (leader == -1) {
        string out = to_string(g.turn);
        for (int i = 0; i < 3; i++) {
            out += " S -";
        }
        out += " # ;;;";
        return out;
    }
    
    // 找集中火力目标：优先级最高的敌人
    int focus_target = -1;
    int focus_pri = -1;
    Unit focus_unit;
    
    for (auto& e : enemies) {
        int num_can_hit = 0;
        for (int i = 0; i < 3; i++) {
            if (mine[i].x == -1 || mine[i].flag != -1) continue;
            if (can_attack(g, mine[i].x, mine[i].y, e.x, e.y)) {
                num_can_hit++;
            }
        }
        
        int pri = 0;
        if (e.flag != -1) pri += 1000; // 携旗者最高优先级
        pri += (100 - e.hp) * 10; // 血量低优先
        pri += num_can_hit * 300; // 越多单位能打到越优先
        
        if (pri > focus_pri) {
            focus_pri = pri;
            focus_target = e.id;
            focus_unit = e;
        }
    }
    
    // 找最近的敌人（用于移动）
    int nearest_enemy_d = INT_MAX;
    Unit nearest_enemy;
    for (auto& e : enemies) {
        int d = mdist(mine[leader].x, mine[leader].y, e.x, e.y);
        if (d < nearest_enemy_d) {
            nearest_enemy_d = d;
            nearest_enemy = e;
        }
    }
    
    // 找最近的旗子
    int nearest_flag_d = INT_MAX;
    Flag nearest_flag;
    for (auto& f : avail_flags) {
        int d = dist[leader][f.y][f.x];
        if (d != -1 && d < nearest_flag_d) {
            nearest_flag_d = d;
            nearest_flag = f;
        }
    }
    
    // 决策：
    // 1. 如果有人携旗，回基地
    // 2. 如果有敌人在攻击范围内，集火攻击
    // 3. 如果有敌人在附近，靠近敌人
    // 4. 如果有旗子，去拿旗
    // 5. 向中心移动
    
    // 先处理携旗的单位
    for (int i = 0; i < 3; i++) {
        if (mine[i].x == -1) continue;
        if (mine[i].flag != -1) {
            if (in_my_base(g, mine[i].x, mine[i].y)) {
                notes[i] = "交旗";
            } else {
                auto [tx, ty] = best_base_cell(g, dist[i]);
                moves[i] = step_toward(g, dist[i], mine[i].x, mine[i].y, tx, ty);
                notes[i] = "回基地";
            }
        }
    }
    
    // 处理没携旗的单位
    for (int i = 0; i < 3; i++) {
        if (mine[i].x == -1) continue;
        if (mine[i].flag != -1) continue; // 已经处理过
        
        bool did_action = false;
        
        // 1. 攻击集中火力目标
        if (focus_target != -1 && can_attack(g, mine[i].x, mine[i].y, focus_unit.x, focus_unit.y)) {
            actions[i] = to_string(focus_target);
            // 风筝：如果敌人距离1且我们血量低，边打边退
            int d = mdist(mine[i].x, mine[i].y, focus_unit.x, focus_unit.y);
            if (mine[i].hp <= 67 && d <= 1) {
                moves[i] = move_away(g, mine[i].x, mine[i].y, focus_unit.x, focus_unit.y);
                notes[i] = "边打边退";
            } else {
                moves[i] = 'S';
                notes[i] = "集火";
            }
            did_action = true;
        }
        
        if (!did_action) {
            // 2. 决定移动目标
            int target_x, target_y;
            string note;
            
            // 优先：靠近最近的敌人（如果敌人在一定范围内）
            if (nearest_enemy.id != -1 && nearest_enemy_d <= 6) {
                target_x = nearest_enemy.x;
                target_y = nearest_enemy.y;
                note = "接敌";
            }
            // 其次：去拿最近的旗子
            else if (nearest_flag.id != -1) {
                target_x = nearest_flag.x;
                target_y = nearest_flag.y;
                note = "拿旗";
            }
            // 最后：向中心移动
            else {
                target_x = g.size / 2;
                target_y = g.size / 2;
                note = "推进";
            }
            
            moves[i] = step_toward(g, dist[i], mine[i].x, mine[i].y, target_x, target_y);
            notes[i] = note;
            
            // 如果下一步能到旗子上，顺便拾旗
            if (nearest_flag.id != -1 && dist[i][nearest_flag.y][nearest_flag.x] == 1) {
                actions[i] = "P";
            }
            // 如果已经在旗子上，拾旗
            if (nearest_flag.id != -1 && dist[i][nearest_flag.y][nearest_flag.x] == 0) {
                actions[i] = "P";
                notes[i] = "拾旗";
            }
            
            did_action = true;
        }
        
        // 低血量撤退
        if (mine[i].hp <= 34) {
            int nearest_d = INT_MAX;
            int nex = -1, ney = -1;
            for (auto& e : enemies) {
                int d = mdist(mine[i].x, mine[i].y, e.x, e.y);
                if (d <= 4 && d < nearest_d) {
                    nearest_d = d;
                    nex = e.x;
                    ney = e.y;
                }
            }
            if (nex != -1) {
                moves[i] = move_away(g, mine[i].x, mine[i].y, nex, ney);
                actions[i] = "-";
                notes[i] = "撤退";
            }
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
