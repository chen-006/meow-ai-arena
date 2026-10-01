// 夺旗 bot v2 - 更激进的策略
// 核心思路：
// 1. 动态角色分配：谁离旗近谁去拿，拿了就回基地
// 2. 积极战斗：优先击杀携旗敌人，抢夺旗子
// 3. 团队协作：集中火力，掩护携旗队友
// 4. 预判旗子刷新：在旗子即将刷新时占位
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

// ========== 工具函数 ==========

inline int mdist(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}

inline bool is_wall(const Game& g, int x, int y) {
    return x < 0 || y < 0 || x >= g.size || y >= g.size || g.map[y][x] == '#';
}

// BFS 从起点出发，返回距离数组
vector<vector<int>> bfs_from(const Game& g, int sx, int sy) {
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
            if (is_wall(g, nx, ny) || d[ny][nx] != -1) continue;
            d[ny][nx] = d[y][x] + 1;
            q.push({nx, ny});
        }
    }
    return d;
}

// BFS 求两点距离
int bfs_dist(const Game& g, int sx, int sy, int tx, int ty) {
    if (sx == tx && sy == ty) return 0;
    auto d = bfs_from(g, sx, sy);
    return d[ty][tx] == -1 ? INT_MAX : d[ty][tx];
}

// BFS 求下一步方向
char bfs_step(const Game& g, int sx, int sy, int tx, int ty) {
    if (sx == tx && sy == ty) return 'S';
    auto d = bfs_from(g, sx, sy);
    if (d[ty][tx] == -1) return 'S';
    
    // 从目标回溯
    int cx = tx, cy = ty;
    char best = 'S';
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    char dirs[] = {'D', 'U', 'R', 'L'}; // 反向
    
    while (d[cy][cx] > 1) {
        for (int di = 0; di < 4; di++) {
            int nx = cx + dx[di], ny = cy + dy[di];
            if (!is_wall(g, nx, ny) && d[ny][nx] == d[cy][cx] - 1) {
                cx = nx; cy = ny;
                break;
            }
        }
    }
    
    // 最后一步就是从起点出发的方向
    if (cx == sx && cy == sy - 1) return 'U';
    if (cx == sx && cy == sy + 1) return 'D';
    if (cx == sx - 1 && cy == sy) return 'L';
    if (cx == sx + 1 && cy == sy) return 'R';
    return 'S';
}

// 能否攻击
bool can_attack(const Game& g, int ax, int ay, int tx, int ty) {
    if (mdist(ax, ay, tx, ty) > 2) return false;
    int dx = tx - ax, dy = ty - ay;
    if (abs(dx) == 2 && dy == 0) return !is_wall(g, ax + dx/2, ay);
    if (abs(dy) == 2 && dx == 0) return !is_wall(g, ax, ay + dy/2);
    if (abs(dx) == 1 && abs(dy) == 1) return !is_wall(g, ax + dx, ay) || !is_wall(g, ax, ay + dy);
    return true;
}

bool in_base(const Game& g, int team, int x, int y) {
    auto [bx, by] = g.bases[team];
    return abs(x - bx) <= 1 && abs(y - by) <= 1;
}

bool in_my_base(const Game& g, int x, int y) {
    return in_base(g, g.me, x, y);
}

// 找基地中最近的空格
pair<int,int> nearest_base_cell(const Game& g, int x, int y) {
    auto [bx, by] = g.bases[g.me];
    int best_d = INT_MAX;
    pair<int,int> best = {bx, by};
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int nx = bx + dx, ny = by + dy;
            if (is_wall(g, nx, ny)) continue;
            int d = bfs_dist(g, x, y, nx, ny);
            if (d < best_d) { best_d = d; best = {nx, ny}; }
        }
    }
    return best;
}

// 远离某点
char move_away(const Game& g, int x, int y, int tx, int ty) {
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    char dirs[] = {'U', 'D', 'L', 'R'};
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

// ========== 决策 ==========

string decide(const Game& g) {
    vector<char> moves(3, 'S');
    vector<string> actions(3, "-");
    vector<string> notes(3, "");
    
    auto [bx, by] = g.bases[g.me];
    int cx = g.size / 2, cy = g.size / 2;
    
    // 获取我方单位
    vector<Unit> mine;
    for (auto& u : g.units) if (u.team == g.me) mine.push_back(u);
    
    // 获取所有敌方单位
    vector<Unit> enemies;
    for (auto& u : g.units) if (u.team != g.me && u.x != -1) enemies.push_back(u);
    
    // 获取所有可拾取的旗子
    vector<Flag> available_flags;
    for (auto& f : g.flags) {
        if (f.status == "home" || f.status == "dropped") {
            available_flags.push_back(f);
        }
    }
    
    // 敌方携旗者
    vector<Unit> enemy_carriers;
    for (auto& u : enemies) if (u.flag != -1) enemy_carriers.push_back(u);
    
    // 我方携旗者
    vector<Unit> my_carriers;
    for (auto& u : mine) if (u.x != -1 && u.flag != -1) my_carriers.push_back(u);
    
    // 对每个单位做决策
    for (int i = 0; i < 3; i++) {
        auto& u = mine[i];
        if (u.x == -1) continue; // 阵亡
        
        // 先看能不能攻击
        int best_pri = -1;
        Unit best_target = {-1, -1, -1, -1, 0, -1, -1};
        for (auto& e : enemies) {
            if (!can_attack(g, u.x, u.y, e.x, e.y)) continue;
            int pri = 0;
            if (e.flag != -1) pri += 500; // 携旗者最高优先级
            pri += (100 - e.hp) * 2; // 血量低优先
            pri += 100 - mdist(u.x, u.y, e.x, e.y) * 10; // 近的优先
            if (pri > best_pri) { best_pri = pri; best_target = e; }
        }
        
        // 如果携旗，直接回基地
        if (u.flag != -1) {
            if (in_my_base(g, u.x, u.y)) {
                notes[i] = "交旗";
            } else {
                auto [tx, ty] = nearest_base_cell(g, u.x, u.y);
                moves[i] = bfs_step(g, u.x, u.y, tx, ty);
                notes[i] = "回基地";
            }
            continue;
        }
        
        // 没携旗：决定做什么
        // 优先级：
        // 1. 如果有好的攻击目标且血量健康，攻击
        // 2. 如果附近有旗子，去拿
        // 3. 如果有敌方携旗者，去拦截
        // 4. 去最近的旗子
        // 5. 向中心移动找机会
        
        bool did_action = false;
        
        // 1. 攻击（如果目标足够好）
        if (best_target.id != -1 && u.hp > 34) {
            // 如果有携旗敌人在范围内，必打
            if (best_target.flag != -1 || best_pri > 150) {
                actions[i] = to_string(best_target.id);
                notes[i] = "攻击" + to_string(best_target.id);
                did_action = true;
            }
        }
        
        if (!did_action) {
            // 2. 找最近的可拾取旗子
            int best_fd = INT_MAX;
            Flag best_flag = {-1, "", -1, -1, -1, -1};
            for (auto& f : available_flags) {
                int d = bfs_dist(g, u.x, u.y, f.x, f.y);
                if (d < best_fd) { best_fd = d; best_flag = f; }
            }
            
            if (best_flag.id != -1) {
                if (best_fd == 0) {
                    // 就在旗子上，拾取
                    actions[i] = "P";
                    notes[i] = "拾旗";
                    did_action = true;
                } else if (best_fd <= 3) {
                    // 很近，直接去拿
                    moves[i] = bfs_step(g, u.x, u.y, best_flag.x, best_flag.y);
                    notes[i] = "拿旗";
                    did_action = true;
                } else {
                    // 旗子比较远，先看看有没有敌方携旗者可以拦截
                    int best_cd = INT_MAX;
                    Unit best_carrier = {-1, -1, -1, -1, 0, -1, -1};
                    for (auto& ec : enemy_carriers) {
                        int d = bfs_dist(g, u.x, u.y, ec.x, ec.y);
                        if (d < best_cd) { best_cd = d; best_carrier = ec; }
                    }
                    
                    // 如果敌方携旗者比旗子更近，或者敌方携旗者离我基地近
                    if (best_carrier.id != -1) {
                        int carrier_to_base = bfs_dist(g, best_carrier.x, best_carrier.y, bx, by);
                        if (best_cd < best_fd + 3 || carrier_to_base < 10) {
                            // 去拦截
                            moves[i] = bfs_step(g, u.x, u.y, best_carrier.x, best_carrier.y);
                            notes[i] = "拦截";
                            did_action = true;
                        }
                    }
                    
                    if (!did_action) {
                        // 去拿旗
                        moves[i] = bfs_step(g, u.x, u.y, best_flag.x, best_flag.y);
                        notes[i] = "去拿旗";
                        did_action = true;
                    }
                }
            }
        }
        
        if (!did_action) {
            // 没有可拿的旗，向中心移动找机会
            int cd = bfs_dist(g, u.x, u.y, cx, cy);
            if (cd > 2) {
                moves[i] = bfs_step(g, u.x, u.y, cx, cy);
                notes[i] = "推进";
            } else {
                notes[i] = "待命";
            }
        }
        
        // 血量低时逃跑（再被打一次就死）
        if (u.hp <= 34 && best_target.id != -1) {
            moves[i] = move_away(g, u.x, u.y, best_target.x, best_target.y);
            actions[i] = "-";
            notes[i] = "撤退";
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
