// 练习基准（中等）：常规思路，不做对手建模和多步模拟。
//  - 抢旗：比较我方和敌方谁先到，抢得到的旗才去，拿到就沿最短路回家。
//  - 截杀：追得上的敌方携旗者就去追，在自家附近的优先。
//  - 没事做时守在离家最近的空旗点。
//  - 移动时以少打多就绕开；攻击按射程规则选目标，队友集火同一个。
#include <bits/stdc++.h>
using namespace std;

struct Unit { int id, team, x, y, hp, flag, respawn; };
struct Flag { int id; string status; int x, y, holder, ret; };

int n, me, S, turns, HP, DMG;
vector<string> grid;
vector<pair<int, int>> baseCenter, spots;
vector<vector<int>> homeField;          // 各阵营基地的距离场
map<int, vector<int>> fieldCache;

inline int idx(int x, int y) { return y * S + x; }
inline bool wall(int x, int y) { return x < 0 || y < 0 || x >= S || y >= S || grid[y][x] == '#'; }
inline int manh(int a, int b) { return abs(a % S - b % S) + abs(a / S - b / S); }
const int DX[5] = {0, 0, -1, 1, 0}, DY[5] = {-1, 1, 0, 0, 0};
const char* MV[5] = {"U", "D", "L", "R", "S"};

vector<int> bfs(const vector<int>& src) {
    vector<int> d(S * S, INT_MAX);
    deque<int> q;
    for (int c : src) { d[c] = 0; q.push_back(c); }
    while (!q.empty()) {
        int c = q.front(); q.pop_front();
        for (int k = 0; k < 4; k++) {
            int nx = c % S + DX[k], ny = c / S + DY[k];
            if (wall(nx, ny)) continue;
            int nc = idx(nx, ny);
            if (d[nc] == INT_MAX) { d[nc] = d[c] + 1; q.push_back(nc); }
        }
    }
    return d;
}
const vector<int>& field(int c) {
    auto it = fieldCache.find(c);
    if (it != fieldCache.end()) return it->second;
    if (fieldCache.size() > 256) fieldCache.clear();
    return fieldCache[c] = bfs({c});
}
int dist(int a, int b) { return field(b)[a]; }

// 射程 2，墙会挡：直线隔一格要求中间不是墙，斜对角要求两个拐角至少一个不是墙
bool canHit(int a, int b) {
    int ax = a % S, ay = a / S, bx = b % S, by = b / S, dx = bx - ax, dy = by - ay;
    int d = abs(dx) + abs(dy);
    if (d == 0 || d > 2) return false;
    if (d == 1) return true;
    if (dx == 0) return !wall(ax, ay + dy / 2);
    if (dy == 0) return !wall(ax + dx / 2, ay);
    return !wall(ax + dx, ay) || !wall(ax, ay + dy);
}

int main() {
    ios::sync_with_stdio(false);
    string w;
    while (cin >> w) {
        if (w == "INIT") {
            int re, fr, fc;
            cin >> n >> me >> S >> turns >> HP >> DMG >> re >> fr >> fc;
        } else if (w == "MAP") {
            grid.assign(S, "");
            for (auto& r : grid) cin >> r;
        } else if (w == "BASES") {
            int k; cin >> k; baseCenter.resize(k);
            for (auto& b : baseCenter) cin >> b.first >> b.second;
            homeField.resize(k);
            for (int t = 0; t < k; t++) {
                vector<int> cells;
                for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++)
                    cells.push_back(idx(baseCenter[t].first + dx, baseCenter[t].second + dy));
                homeField[t] = bfs(cells);
            }
        } else if (w == "SPOTS") {
            int k; cin >> k; spots.resize(k);
            for (auto& s : spots) cin >> s.first >> s.second;
        } else if (w == "TURN") {
            int t; cin >> t;
            string key; int k;
            cin >> key; for (int i = 0, s; i < n; i++) cin >> s;
            cin >> key >> k; vector<Unit> units(k);
            for (auto& u : units) { string tag; cin >> tag >> u.id >> u.team >> u.x >> u.y >> u.hp >> u.flag >> u.respawn; }
            cin >> key >> k; vector<Flag> flags(k);
            for (auto& f : flags) { string tag; cin >> tag >> f.id >> f.status >> f.x >> f.y >> f.holder >> f.ret; }
            cin >> key >> k; string rest; getline(cin, rest);
            for (int i = 0; i < k; i++) getline(cin, rest);
            cin >> key;   // END

            vector<int> pos(units.size(), -1);
            for (auto& u : units) if (u.x >= 0) pos[u.id] = idx(u.x, u.y);
            vector<int> foes, mine;
            for (auto& u : units) {
                if (u.x < 0) continue;
                (u.team == me ? mine : foes).push_back(u.id);
            }
            auto enemyETA = [&](int c) {
                int best = INT_MAX;
                for (int e : foes) if (units[e].flag < 0) best = min(best, dist(pos[e], c));
                return best;
            };

            // ---------- 任务分配（贪心） ----------
            struct Task { int goal; double value; int cap; };
            vector<Task> tasks;
            vector<pair<int, int>> cand;   // (角色, 任务) 由价值排序
            vector<vector<double>> val;
            for (auto& f : flags) {
                if (f.x < 0) continue;
                tasks.push_back({idx(f.x, f.y), (double)f.ret, 1});   // value 字段暂存回位回合
            }
            int nFlagTasks = tasks.size();
            for (int e : foes) {
                if (units[e].flag < 0) continue;
                tasks.push_back({pos[e], (double)e, 2});   // 直接追，value 暂存敌人编号
            }
            int nHunt = tasks.size() - nFlagTasks;
            // 空旗点：离自家基地由近到远
            vector<char> hasFlag(S * S, 0);
            for (auto& f : flags) if (f.x >= 0) hasFlag[idx(f.x, f.y)] = 1;
            for (auto& s : spots) if (!hasFlag[idx(s.first, s.second)]) tasks.push_back({idx(s.first, s.second), 0, 1});

            vector<int> goal(mine.size(), -1);
            {
                vector<tuple<double, int, int>> scored;   // (价值, 角色序号, 任务序号)
                for (int a = 0; a < (int)mine.size(); a++) {
                    auto& u = units[mine[a]];
                    if (u.flag >= 0) continue;
                    int p = pos[u.id];
                    for (int j = 0; j < (int)tasks.size(); j++) {
                        auto& tk = tasks[j];
                        double v = 0;
                        if (j < nFlagTasks) {
                            int my = dist(p, tk.goal), en = enemyETA(tk.goal);
                            int ret = (int)tk.value;
                            if (ret >= 0 && t + my > ret) continue;               // 赶到前就回位了
                            int home = homeField[me][tk.goal];
                            if (t + my + home > turns) continue;                   // 来不及送回
                            double win = my < en ? 1.0 : my == en ? 0.5 : 0.15;
                            v = 10.0 * win / (my + home + 2);
                        } else if (j < nFlagTasks + nHunt) {
                            int e = (int)tk.value, eh = homeField[units[e].team][pos[e]];
                            int my = dist(p, pos[e]);
                            if (my > eh + 3) continue;                              // 追不上
                            v = 6.0 / (my + 3);
                            if (homeField[me][pos[e]] < 12) v *= 1.5;               // 在自家附近更值得拦
                        } else {
                            int my = dist(p, tk.goal);
                            v = 1.0 / (my + homeField[me][tk.goal] + 6);
                        }
                        scored.push_back({v, a, j});
                    }
                }
                sort(scored.rbegin(), scored.rend());
                vector<int> used(tasks.size(), 0);
                for (auto& [v, a, j] : scored) {
                    if (goal[a] >= 0 || used[j] >= tasks[j].cap) continue;
                    goal[a] = tasks[j].goal; used[j]++;
                }
            }

            // ---------- 移动 ----------
            // threat[c]：下回合可能打到 c 的敌人数（敌人移动一步后在射程内）
            vector<int> threat(S * S, 0);
            for (int e : foes) {
                if (units[e].flag >= 0) continue;
                set<int> hit;
                for (int m = 0; m < 5; m++) {
                    int ex = pos[e] % S + DX[m], ey = pos[e] / S + DY[m];
                    if (wall(ex, ey)) continue;
                    int ec = idx(ex, ey);
                    for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
                        int x = ex + dx, y = ey + dy;
                        if (wall(x, y)) continue;
                        if (canHit(ec, idx(x, y))) hit.insert(idx(x, y));
                    }
                }
                for (int c : hit) threat[c]++;
            }
            vector<int> occupied(S * S, -1);
            for (auto& u : units) if (u.x >= 0) occupied[pos[u.id]] = u.id;
            vector<char> claimed(S * S, 0);
            vector<int> newPos(mine.size());
            vector<int> order(mine.size());
            iota(order.begin(), order.end(), 0);
            stable_sort(order.begin(), order.end(), [&](int a, int b) { return units[mine[a]].flag > units[mine[b]].flag; });
            for (int a : order) {
                auto& u = units[mine[a]];
                int p = pos[u.id];
                const vector<int>& gf = u.flag >= 0 ? homeField[me] : (goal[a] >= 0 ? field(goal[a]) : homeField[me]);
                // 附近的敌我人数
                int allies = 0, enemies = 0;
                for (int b : mine) if (manh(pos[b], p) <= 3 && units[b].flag < 0) allies++;
                for (int e : foes) if (manh(pos[e], p) <= 3 && units[e].flag < 0) enemies++;
                bool outnumbered = enemies > allies;
                double best = 1e18; int bm = 4;
                for (int m = 0; m < 5; m++) {
                    int x = p % S + DX[m], y = p / S + DY[m];
                    if (wall(x, y)) continue;
                    int c = idx(x, y);
                    if (claimed[c]) continue;
                    if (c != p && occupied[c] >= 0 && units[occupied[c]].team != me) continue;
                    if (gf[c] == INT_MAX) continue;
                    double cost = gf[c] * 10.0;
                    if (u.flag >= 0) cost += threat[c] * 6.0;
                    else if (outnumbered) cost += threat[c] * 12.0;
                    else if (u.hp <= DMG) cost += threat[c] * 4.0;
                    if (c != p && occupied[c] >= 0) cost += 3;   // 队友占着，可能走不动
                    if (cost < best) { best = cost; bm = m; }
                }
                int c = idx(p % S + DX[bm], p / S + DY[bm]);
                newPos[a] = c; claimed[c] = 1;
                u.respawn = bm;   // 暂存移动方向
            }

            // ---------- 动作 ----------
            vector<int> plan(units.size(), 0);   // 已安排的伤害
            string out = to_string(t);
            for (int a = 0; a < (int)mine.size(); a++) {
                auto& u = units[mine[a]];
                int c = newPos[a];
                string act = "-";
                bool onFlag = false;
                for (auto& f : flags) if (f.x >= 0 && idx(f.x, f.y) == c) onFlag = true;
                if (u.flag < 0 && onFlag) act = "P";
                else if (u.flag < 0) {
                    int bestE = -1; double bv = -1;
                    for (int e : foes) {
                        if (!canHit(c, pos[e])) continue;
                        if (plan[e] >= units[e].hp) continue;      // 已经够打死了
                        double v = 1.0 + (units[e].flag >= 0 ? 5.0 : 0) + (units[e].hp - plan[e] <= DMG ? 3.0 : 0) + plan[e] / 50.0;
                        if (v > bv) { bv = v; bestE = e; }
                    }
                    if (bestE >= 0) { act = to_string(bestE); plan[bestE] += DMG; }
                }
                out += string(" ") + MV[u.respawn] + " " + act;
            }
            // 阵亡的角色原地待命
            string full = to_string(t);
            {
                istringstream in(out); string tok; in >> tok;
                vector<string> parts;
                while (in >> tok) parts.push_back(tok);
                int pi = 0;
                for (auto& u : units) {
                    if (u.team != me) continue;
                    if (u.x < 0) full += " S -";
                    else { full += " " + parts[pi] + " " + parts[pi + 1]; pi += 2; }
                }
            }
            cout << full << endl;
        }
    }
}
