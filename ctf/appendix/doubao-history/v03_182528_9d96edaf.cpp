// 夺旗 bot v3 - 优化版
// 核心改进：
// 1. 每个单位只做一次BFS，缓存距离场
// 2. 智能任务分配：避免多个单位抢同一个旗
// 3. 更好的战斗：优先击杀携旗敌人，保持距离
// 4. 基地防守：至少留一个单位防守基地
// 5. 安全路径：携旗时选择敌人少的路径
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

// 从距离场获取下一步方向
char step_toward(const Game& g, const vector<vector<int>>& d, int sx, int sy, int tx, int ty) {
    if (sx == tx && sy == ty) return 'S';
    if (d[ty][tx] == -1 || d[sy][sx] == -1) return 'S';
    
    // 从目标回溯一步
    static const int dx[] = {0, 0, -1, 1};
    static const int dy[] = {-1, 1, 0, 0};
    static const char rev_dir[] = {'D', 'U', 'R', 'L'}; // 反向
    
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

// 找基地中最近的可达格子
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

// 远离某点（曼哈顿距离）
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

// 向某点移动一格（曼哈顿贪心，考虑墙）
char greedy_step(const Game& g, int x, int y, int tx, int ty) {
    static const int dx[] = {0, 0, -1, 1};
    static const int dy[] = {-1, 1, 0, 0};
    static const char dirs[] = {'U', 'D', 'L', 'R'};
    int best_d = mdist(x, y, tx, ty);
    char best = 'S';
    for (int d = 0; d < 4; d++) {
        int nx = x + dx[d], ny = y + dy[d];
        if (is_wall(g, nx, ny)) continue;
        int nd = mdist(nx, ny, tx, ty);
        if (nd < best_d) { best_d = nd; best = dirs[d]; }
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
    
    // 预计算每个活着的我方单位的距离场
    vector<vector<vector<int>>> my_dist(3);
    for (int i = 0; i < 3; i++) {
        if (mine[i].x != -1) {
            my_dist[i] = bfs_from(g, mine[i].x, mine[i].y);
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
    
    // 我方携旗者数量
    int my_carriers = 0;
    for (int i = 0; i < 3; i++) {
        if (mine[i].x != -1 && mine[i].flag != -1) my_carriers++;
    }
    
    // ===== 任务分配 =====
    // 给每个单位分配任务：
    // - 携旗者：回基地
    // - 其他：拿旗 / 拦截 / 防守
    
    // 先处理携旗者
    for (int i = 0; i < 3; i++) {
        if (mine[i].x == -1) continue;
        if (mine[i].flag != -1) {
            if (in_my_base(g, mine[i].x, mine[i].y)) {
                notes[i] = "交旗";
            } else {
                auto [tx, ty] = best_base_cell(g, my_dist[i]);
                moves[i] = step_toward(g, my_dist[i], mine[i].x, mine[i].y, tx, ty);
                notes[i] = "回基地";
            }
        }
    }
    
    // 给没携旗的单位分配任务
    // 策略：
    // - 至少1个单位防守基地（如果有敌方携旗者）
    // - 其他单位去拿最近的旗或拦截
    
    vector<bool> assigned(3, false);
    for (int i = 0; i < 3; i++) {
        if (mine[i].x == -1 || mine[i].flag != -1) assigned[i] = true;
    }
    
    // 找最近的敌方携旗者到我基地的距离
    int nearest_carrier_to_base = INT_MAX;
    Unit nearest_carrier = {-1, -1, -1, -1, 0, -1, -1};
    for (auto& ec : enemy_carriers) {
        // 用曼哈顿距离近似
        int d = mdist(ec.x, ec.y, bx, by);
        if (d < nearest_carrier_to_base) {
            nearest_carrier_to_base = d;
            nearest_carrier = ec;
        }
    }
    
    // 防守单位：离基地最近的空闲单位去拦截最近的敌方携旗者
    if (nearest_carrier.id != -1 && nearest_carrier_to_base < 15) {
        int best_i = -1, best_d = INT_MAX;
        for (int i = 0; i < 3; i++) {
            if (assigned[i]) continue;
            int d = my_dist[i][nearest_carrier.y][nearest_carrier.x];
            if (d != -1 && d < best_d) { best_d = d; best_i = i; }
        }
        if (best_i != -1) {
            assigned[best_i] = true;
            moves[best_i] = step_toward(g, my_dist[best_i], mine[best_i].x, mine[best_i].y, nearest_carrier.x, nearest_carrier.y);
            notes[best_i] = "拦截";
        }
    }
    
    // 剩下的单位去拿旗
    // 用贪心分配：最近的单位拿最近的旗
    vector<bool> flag_taken(avail_flags.size(), false);
    
    // 按距离排序的单位-旗子对
    vector<tuple<int, int, int>> assignments; // distance, unit_idx, flag_idx
    for (int i = 0; i < 3; i++) {
        if (assigned[i]) continue;
        for (int j = 0; j < (int)avail_flags.size(); j++) {
            int d = my_dist[i][avail_flags[j].y][avail_flags[j].x];
            if (d != -1) {
                assignments.emplace_back(d, i, j);
            }
        }
    }
    sort(assignments.begin(), assignments.end());
    
    for (auto& [d, ui, fi] : assignments) {
        if (assigned[ui] || flag_taken[fi]) continue;
        assigned[ui] = true;
        flag_taken[fi] = true;
        if (d == 0) {
            actions[ui] = "P";
            notes[ui] = "拾旗";
        } else {
            moves[ui] = step_toward(g, my_dist[ui], mine[ui].x, mine[ui].y, avail_flags[fi].x, avail_flags[fi].y);
            notes[ui] = "拿旗" + to_string(avail_flags[fi].id);
        }
    }
    
    // 还没分配的单位
    for (int i = 0; i < 3; i++) {
        if (assigned[i]) continue;
        if (mine[i].x == -1) continue;
        
        // 如果有敌方携旗者，去追最近的
        if (!enemy_carriers.empty()) {
            int best_d = INT_MAX;
            Unit best;
            for (auto& ec : enemy_carriers) {
                int d = my_dist[i][ec.y][ec.x];
                if (d != -1 && d < best_d) { best_d = d; best = ec; }
            }
            if (best.id != -1) {
                moves[i] = step_toward(g, my_dist[i], mine[i].x, mine[i].y, best.x, best.y);
                notes[i] = "追携旗";
                assigned[i] = true;
            }
        }
        
        if (!assigned[i]) {
            // 向中心移动找机会
            int cd = my_dist[i][cy][cx];
            if (cd != -1 && cd > 3) {
                moves[i] = step_toward(g, my_dist[i], mine[i].x, mine[i].y, cx, cy);
                notes[i] = "推进";
            } else {
                // 在中心附近，找最近的敌人
                if (!enemies.empty()) {
                    int best_d = INT_MAX;
                    Unit best;
                    for (auto& e : enemies) {
                        int d = my_dist[i][e.y][e.x];
                        if (d != -1 && d < best_d) { best_d = d; best = e; }
                    }
                    if (best.id != -1 && best_d < 8) {
                        moves[i] = step_toward(g, my_dist[i], mine[i].x, mine[i].y, best.x, best.y);
                        notes[i] = "接敌";
                    } else {
                        notes[i] = "待命";
                    }
                } else {
                    notes[i] = "待命";
                }
            }
            assigned[i] = true;
        }
    }
    
    // ===== 战斗决策 =====
    // 对每个单位，检查是否应该攻击
    for (int i = 0; i < 3; i++) {
        if (mine[i].x == -1) continue;
        if (mine[i].flag != -1) continue; // 携旗不能攻击
        
        int best_pri = -1;
        Unit best_target = {-1, -1, -1, -1, 0, -1, -1};
        
        for (auto& e : enemies) {
            if (!can_attack(g, mine[i].x, mine[i].y, e.x, e.y)) continue;
            int pri = 0;
            if (e.flag != -1) pri += 1000; // 携旗者最高优先级
            pri += (100 - e.hp) * 3; // 血量低优先
            pri += (2 - mdist(mine[i].x, mine[i].y, e.x, e.y)) * 50; // 近的优先
            if (pri > best_pri) { best_pri = pri; best_target = e; }
        }
        
        if (best_target.id != -1) {
            // 攻击优先级判断：
            // - 携旗敌人：必打
            // - 能击杀的：必打
            // - 普通敌人：如果血量健康就打
            bool should_attack = false;
            if (best_target.flag != -1) should_attack = true;
            else if (best_target.hp <= 34) should_attack = true; // 能击杀
            else if (mine[i].hp > 67) should_attack = true; // 血量健康
            else if (best_pri > 200) should_attack = true;
            
            if (should_attack) {
                actions[i] = to_string(best_target.id);
                // 如果在攻击范围内，就不动（保持位置）
                // 但如果血量低且敌人也能打到我，考虑后退
                if (mine[i].hp <= 34 && mdist(mine[i].x, mine[i].y, best_target.x, best_target.y) <= 1) {
                    // 低血量且敌人贴脸，边打边退
                    moves[i] = move_away(g, mine[i].x, mine[i].y, best_target.x, best_target.y);
                    notes[i] = "边打边退";
                } else {
                    moves[i] = 'S';
                    notes[i] = "攻击" + to_string(best_target.id);
                }
            }
        }
    }
    
    // ===== 低血量逃跑 =====
    for (int i = 0; i < 3; i++) {
        if (mine[i].x == -1) continue;
        if (mine[i].flag != -1) continue;
        if (mine[i].hp > 34) continue;
        
        // 检查是否有敌人在攻击范围内
        bool in_danger = false;
        int nearest_ex = -1, nearest_ey = -1;
        int min_d = INT_MAX;
        for (auto& e : enemies) {
            int d = mdist(mine[i].x, mine[i].y, e.x, e.y);
            if (d <= 2) {
                in_danger = true;
                if (d < min_d) {
                    min_d = d;
                    nearest_ex = e.x;
                    nearest_ey = e.y;
                }
            }
        }
        
        if (in_danger) {
            moves[i] = move_away(g, mine[i].x, mine[i].y, nearest_ex, nearest_ey);
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
