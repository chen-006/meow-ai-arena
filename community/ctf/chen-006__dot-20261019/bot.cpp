#ifndef NN_WIDTH
#define NN_WIDTH 32
#endif
#ifndef NN_SAMPLES
#define NN_SAMPLES 4
#endif
#ifndef NN_BEAM
#define NN_BEAM 5
#endif
#ifndef NN_PRIOR
#define NN_PRIOR .025f
#endif
/*
MIT License

Copyright (c) 2026 meow-ai-arena contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#ifndef NN_BUDGET_MS
#define NN_BUDGET_MS 12
#endif
// MeowMind: CPU-only learned policy/value search and online opponent beliefs.
// Based on ctf/bots/gpt-6-astra.cpp protocol; trained on official bot policies.
// Float32 weights are stored losslessly and decoded once in memory.
#include <bit>
#define NN_ACTION_EVIDENCE 1
#define NN_BATCH_FEATURES 1
// Learned simultaneous-action capture-the-flag search. CPU only.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
using namespace std;
struct Unit {
int id, team, x, y, hp, flag, respawn_at;
}; // 阵亡时 x = y = -1
struct Flag {
int id;
string status;
int x, y, holder, return_at;
};
struct Event {
string type;
vector<int> args;
}; // 各类事件的字段见 RULES.md

struct Game {
// 开局信息（INIT 块）
int teams = 0, me = 0, size = 0, turns = 0, hp = 0, damage = 0, respawn = 0, flag_return = 0, flag_cooldown = 0;
vector<string> map;           // map[y][x]：'#' 墙，'.' 空地
vector<pair<int, int>> bases; // 各阵营基地中心，基地是以它为中心的 3×3
vector<pair<int, int>> spots; // 全部旗点
// 每回合更新（TURN 块）
int turn = 0;
vector<int> score;
vector<Unit> units;
vector<Flag> flags;
vector<Event> events; // 上一回合发生的事件
};

// 读一个块。读到 INIT 时继续读到 TURN 块结束。返回 false 表示输入结束。
bool read_turn(Game &g) {
string w;
while (cin >> w) {
if (w == "INIT") {
cin >> g.teams >> g.me >> g.size >> g.turns >> g.hp >> g.damage >> g.respawn >> g.flag_return >>
g.flag_cooldown;
} else if (w == "MAP") {
g.map.assign(g.size, "");
for (auto &row : g.map) cin >> row;
} else if (w == "BASES") {
int k;
cin >> k;
g.bases.resize(k);
for (auto &b : g.bases) cin >> b.first >> b.second;
} else if (w == "SPOTS") {
int k;
cin >> k;
g.spots.resize(k);
for (auto &s : g.spots) cin >> s.first >> s.second;
} else if (w == "TURN") {
cin >> g.turn;
} else if (w == "SCORE") {
g.score.resize(g.teams);
for (auto &s : g.score) cin >> s;
} else if (w == "UNITS") {
int k;
cin >> k;
g.units.resize(k);
for (auto &u : g.units) {
string tag;
cin >> tag >> u.id >> u.team >> u.x >> u.y >> u.hp >> u.flag >> u.respawn_at;
}
} else if (w == "FLAGS") {
int k;
cin >> k;
g.flags.resize(k);
for (auto &f : g.flags) {
string tag;
cin >> tag >> f.id >> f.status >> f.x >> f.y >> f.holder >> f.return_at;
}
} else if (w == "EVENTS") {
int k;
cin >> k;
g.events.assign(k, {});
string line;
getline(cin, line);
for (auto &e : g.events) {
getline(cin, line);
istringstream in(line);
string tag;
in >> tag >> e.type;
for (int v; in >> v;) e.args.push_back(v);
}
} else if (w == "END") {
return true;
}
}
return false;
}

using State = Game;
struct Order {
int move = 0, action = -1;
}; // -1 pickup, -2 wait, -3 drop
struct World {
State g;
vector<vector<pair<int, int>>> spawn;
uint64_t rng;
explicit World(State state, uint64_t seed) : g(move(state)), rng(seed ? seed : 1) {
g.turn = 0;
for (auto bc : g.bases) {
vector<pair<int, int>> b;
for (int dy = -1; dy <= 1; dy++)
for (int dx = -1; dx <= 1; dx++) b.emplace_back(bc.first + dx, bc.second + dy);
stable_sort(b.begin(), b.end(), [&](auto a, auto b) {
auto key = [&](auto p) {
return pair(abs(p.first - g.size / 2) + abs(p.second - g.size / 2),
abs(p.first - bc.first) + 2 * abs(p.second - bc.second));
};
return key(a) < key(b);
});
spawn.push_back(b);
}
}
pair<int, int> point(int i) const { return {g.units[i].x, g.units[i].y}; }
bool wall(pair<int, int> p) const {
return p.first < 0 || p.second < 0 || p.first >= g.size || p.second >= g.size ||
g.map[p.second][p.first] == '#';
}
bool hit(pair<int, int> a, pair<int, int> b) const {
int dx = b.first - a.first, dy = b.second - a.second;
if (abs(dx) + abs(dy) > 2) return false;
if (abs(dx) == 2) return !wall({a.first + dx / 2, a.second});
if (abs(dy) == 2) return !wall({a.first, a.second + dy / 2});
if (abs(dx) == 1 && abs(dy) == 1) return !wall({a.first + dx, a.second}) || !wall({a.first, a.second + dy});
return true;
}
void step(const vector<Order> &orders) {
++g.turn;
g.events.clear();
int nu = g.units.size();
for (auto &f : g.flags)
if ((f.status == "dropped" || f.status == "cooldown") && f.return_at <= g.turn) {
vector<pair<int, int>> free;
for (auto p : g.spots) {
bool taken = false;
for (auto &ff : g.flags)
if (ff.status == "home" && ff.x == p.first && ff.y == p.second) taken = true;
if (!taken) free.push_back(p);
}
if (free.empty()) free = g.spots;
rng ^= rng << 13;
rng ^= rng >> 7;
rng ^= rng << 17;
auto p = free[rng % free.size()];
f.x = p.first;
f.y = p.second;
f.status = "home";
f.holder = f.return_at = -1;
g.events.push_back({"return", {f.id}});
}
bool immune[45] = {};
int occupied[2500];
fill(occupied, occupied + 2500, -1);
for (auto &u : g.units)
if (u.x >= 0) occupied[u.y * g.size + u.x] = u.id;
for (auto &u : g.units)
if (u.x < 0 && u.respawn_at <= g.turn) {
for (auto p : spawn[u.team])
if (occupied[p.second * g.size + p.first] < 0) {
u.x = p.first;
u.y = p.second;
u.hp = g.hp;
u.flag = u.respawn_at = -1;
immune[u.id] = true;
occupied[u.y * g.size + u.x] = u.id;
g.events.push_back({"respawn", {u.id, u.x, u.y}});
break;
}
}
vector<pair<int, int>> start(nu), want(nu);
int count[2500] = {};
bool blocked[45] = {};
int dx[5] = {0, 0, 0, -1, 1}, dy[5] = {0, -1, 1, 0, 0};
for (auto &u : g.units)
if (u.x >= 0) {
int i = u.id;
start[i] = point(i);
want[i] = {u.x + dx[orders[i].move], u.y + dy[orders[i].move]};
if (wall(want[i])) want[i] = start[i];
count[want[i].second * g.size + want[i].first]++;
}
for (auto &u : g.units)
if (u.x >= 0) {
int i = u.id;
auto p = want[i];
if (count[p.second * g.size + p.first] > 1) blocked[i] = true;
int j = occupied[p.second * g.size + p.first];
if (j >= 0 && j != i && u.team != g.units[j].team && want[j] == start[i])
blocked[i] = blocked[j] = true;
}
for (int rep = 0; rep < nu; rep++) {
bool changed = false;
for (auto &u : g.units)
if (u.x >= 0 && !blocked[u.id] && want[u.id] != start[u.id]) {
int j = occupied[want[u.id].second * g.size + want[u.id].first];
if (j >= 0 && (blocked[j] || want[j] == start[j])) {
blocked[u.id] = true;
changed = true;
}
}
if (!changed) break;
}
for (auto &u : g.units)
if (u.x >= 0) {
auto p = blocked[u.id] ? start[u.id] : want[u.id];
u.x = p.first;
u.y = p.second;
}
vector<vector<int>> by(nu);
for (auto &u : g.units)
if (u.x >= 0 && u.flag < 0 && !immune[u.id]) {
int j = orders[u.id].action;
if (j >= 0 && j < nu && g.units[j].x >= 0 && g.units[j].team != u.team && !immune[j] &&
hit(point(u.id), point(j))) {
by[j].push_back(u.id);
g.events.push_back({"attack", {u.id, j}});
}
}
for (auto &u : g.units)
if (u.x >= 0) u.hp -= g.damage * by[u.id].size();
for (auto &u : g.units)
if (u.x >= 0 && u.hp <= 0) {
vector<int> ev{u.id, u.x, u.y, u.flag, int(by[u.id].size())};
ev.insert(ev.end(), by[u.id].begin(), by[u.id].end());
g.events.push_back({"death", ev});
if (u.flag >= 0) {
auto &f = g.flags[u.flag];
f.status = "dropped";
f.x = u.x;
f.y = u.y;
f.holder = -1;
f.return_at = g.turn + g.flag_return;
}
u.x = u.y = -1;
u.hp = 0;
u.flag = -1;
u.respawn_at = g.turn + g.respawn;
}
for (auto &u : g.units)
if (u.x >= 0 && u.flag < 0 && orders[u.id].action == -1) {
for (auto &f : g.flags)
if ((f.status == "home" || f.status == "dropped") && f.x == u.x && f.y == u.y) {
u.flag = f.id;
f.status = "carried";
f.x = f.y = -1;
f.holder = u.id;
f.return_at = -1;
g.events.push_back({"pickup", {u.id, f.id}});
break;
}
}
for (auto &u : g.units)
if (u.x >= 0 && u.flag >= 0 && orders[u.id].action == -3) {
auto &f = g.flags[u.flag];
f.status = "dropped";
f.x = u.x;
f.y = u.y;
f.holder = -1;
f.return_at = g.turn + g.flag_return;
g.events.push_back({"drop", {u.id, u.flag}});
u.flag = -1;
}
for (auto &u : g.units)
if (u.x >= 0 && u.flag >= 0 && abs(u.x - g.bases[u.team].first) <= 1 &&
abs(u.y - g.bases[u.team].second) <= 1) {
int fid = u.flag;
auto &f = g.flags[fid];
f.status = "cooldown";
f.x = f.y = -1;
f.holder = -1;
f.return_at = g.turn + g.flag_cooldown;
u.flag = -1;
g.score[u.team]++;
g.events.push_back({"capture", {u.id, u.team, fid}});
}
}
void dump() const {
cout << g.turn;
for (int s : g.score) cout << ' ' << s;
for (auto &u : g.units) cout << ' ' << u.x << ' ' << u.y << ' ' << u.hp << ' ' << u.flag << ' ' << u.respawn_at;
for (auto &f : g.flags)
cout << ' ' << f.status << ' ' << f.x << ' ' << f.y << ' ' << f.holder << ' ' << f.return_at;
cout << " |";
for (auto &e : g.events) {
cout << ' ' << e.type;
for (int a : e.args) cout << ' ' << a;
cout << ';';
}
cout << endl;
}
};

#define RICH_VALUE 1
#define VALUE_SHAPED 1
namespace weights {
inline float policy_w0[7168] = {};
inline float policy_b0[64] = {};
inline float policy_w1[2048] = {};
inline float policy_b1[32] = {};
inline float policy_w2[32] = {};
inline float policy_b2[1] = {};
inline float value_w0[26624] = {};
inline float value_b0[64] = {};
inline float value_w1[2048] = {};
inline float value_b1[32] = {};
inline float value_w2[32] = {};
inline float value_b2[1] = {};
} // namespace weights

// Shared, public-observation-only features for offline learning and CPU inference.
// Standard C++20 only. All geometry is rebuilt from the public INIT block.
namespace learning {
constexpr int PF = 112;
#ifdef RICH_VALUE
constexpr int VF = 80 + 3 * PF;
#else
constexpr int VF = 80;
#endif
// Respawns are deterministic public information. Predict them before choosing
// commands, while leaving unknown flag refreshes to sampled rule transitions.
template <class G> G prepared(const G &input) {
G g = input;
int n = g.size;
std::vector<int> occupied(n * n, -1);
for (auto &u : g.units)
if (u.x >= 0) occupied[u.y * n + u.x] = u.id;
for (auto &u : g.units)
if (u.x < 0 && u.respawn_at >= 0 && u.respawn_at <= g.turn) {
auto bc = g.bases[u.team];
std::vector<std::pair<int, int>> cells;
for (int dy = -1; dy <= 1; dy++)
for (int dx = -1; dx <= 1; dx++) cells.push_back({bc.first + dx, bc.second + dy});
std::stable_sort(cells.begin(), cells.end(), [&](auto a, auto b) {
auto key = [&](auto p) {
return std::pair(std::abs(p.first - n / 2) + std::abs(p.second - n / 2),
std::abs(p.first - bc.first) + 2 * std::abs(p.second - bc.second));
};
return key(a) < key(b);
});
for (auto p : cells)
if (occupied[p.second * n + p.first] < 0) {
u.x = p.first;
u.y = p.second;
u.hp = g.hp;
u.flag = -1;
u.respawn_at = -1;
occupied[u.y * n + u.x] = u.id;
break;
}
}
return g;
}
struct Geometry {
int n = 0, N = 0;
std::vector<short> distances;
std::vector<std::array<int, 5>> nb;
std::vector<std::vector<int>> home;
std::vector<std::array<int, 3>> staging;
int D(int a, int b) const { return a < 0 || b < 0 ? 100 : distances[a * N + b]; }
int man(int a, int b) const { return a < 0 || b < 0 ? 100 : std::abs(a % n - b % n) + std::abs(a / n - b / n); }
template <class G> void init(const G &g) {
n = g.size;
N = n * n;
nb.resize(N);
distances.assign(N * N, 30000);
const int dx[5] = {0, 0, 0, -1, 1}, dy[5] = {0, -1, 1, 0, 0};
std::vector<int> q(N);
for (int p = 0; p < N; p++)
for (int m = 0; m < 5; m++) {
int x = p % n + dx[m], y = p / n + dy[m];
nb[p][m] = x >= 0 && x < n && y >= 0 && y < n && g.map[y][x] != '#' ? y * n + x : p;
}
for (int s = 0; s < N; s++)
if (g.map[s / n][s % n] != '#') {
auto d = &distances[s * N];
d[s] = 0;
int h = 0, t = 0;
q[t++] = s;
while (h < t) {
int p = q[h++];
for (int m = 1; m < 5; m++) {
int z = nb[p][m];
if (d[z] == 30000) {
d[z] = d[p] + 1;
q[t++] = z;
}
}
}
}
home.assign(g.teams, std::vector<int>(N, 100));
for (int team = 0; team < g.teams; team++)
for (int p = 0; p < N; p++)
for (int dy = -1; dy <= 1; dy++)
for (int dx = -1; dx <= 1; dx++)
home[team][p] =
std::min(home[team][p], D(p, (g.bases[team].second + dy) * n + g.bases[team].first + dx));
staging.resize(g.teams);
for (int team = 0; team < g.teams; team++) {
std::vector<std::pair<int, int>> order;
for (auto s : g.spots) {
int q = s.second * n + s.first;
order.push_back({home[team][q], q});
}
std::sort(order.begin(), order.end());
for (int k = 0; k < 3; k++) staging[team][k] = k < int(order.size()) ? order[k].second : -1;
}
}
template <class G> bool hit(const G &g, int a, int b) const {
if (a < 0 || b < 0 || man(a, b) > 2) return false;
int dx = b % n - a % n, dy = b / n - a / n;
if (std::abs(dx) == 2) return g.map[a / n][a % n + dx / 2] != '#';
if (std::abs(dy) == 2) return g.map[a / n + dy / 2][a % n] != '#';
if (std::abs(dx) == 1 && std::abs(dy) == 1)
return g.map[a / n][a % n + dx] != '#' || g.map[a / n + dy][a % n] != '#';
return true;
}
template <class U> int cell(const U &u) const { return u.x < 0 ? -1 : u.y * n + u.x; }
template <class G> std::array<float, PF> policy(const G &g, int id, int move, int old = -1) const {
std::array<float, PF> f{};
auto &u = g.units[id];
int p = cell(u), team = u.team;
if (p < 0) {
f[0] = -1;
return f;
}
int z = nb[p][move];
f[0] = u.hp / 100.f;
f[1] = u.flag >= 0;
f[2] = g.teams / 15.f;
f[3] = (g.turns - g.turn) / 400.f;
f[4] = (z % n - p % n);
f[5] = (z / n - p / n);
f[6] = move == 0;
f[7] = move != 0 && z == p;
f[8] = home[team][z] / 24.f;
f[9] = home[team][p] - home[team][z];
f[10] = home[team][z] == 0;
f[11] = (g.bases[team].first - z % n) / 24.f;
f[12] = (g.bases[team].second - z / n) / 24.f;
f[13] = old >= 0 && old != p && z - p == p - old;
f[14] = old >= 0 && z == old;
f[15] = man(z, (n / 2) * n + n / 2) / 24.f;
std::vector<std::pair<int, int>> flags;
for (auto &flag : g.flags) {
int q = -1;
if (flag.x >= 0 && (flag.return_at < 0 || flag.return_at > g.turn))
q = flag.y * n + flag.x;
else if (flag.holder >= 0)
q = cell(g.units[flag.holder]);
if (q >= 0) flags.push_back({D(p, q), flag.id});
}
std::sort(flags.begin(), flags.end());
for (int j = 0; j < 4; j++) {
int at = 16 + j * 9;
if (j >= int(flags.size())) {
f[at + 4] = -1;
continue;
}
auto &flag = g.flags[flags[j].second];
int q = flag.x >= 0 ? flag.y * n + flag.x : cell(g.units[flag.holder]);
f[at] = (q % n - z % n) / 16.f;
f[at + 1] = (q / n - z / n) / 16.f;
f[at + 2] = D(z, q) / 24.f;
f[at + 3] = D(p, q) - D(z, q);
f[at + 4] = flag.holder < 0 ? 1 : g.units[flag.holder].team == team ? 2 : 3;
f[at + 5] = flag.holder < 0 ? home[team][q] / 24.f : g.units[flag.holder].hp / 100.f;
int ally = 100, enemy = 100;
for (auto &e : g.units)
if (e.x >= 0 && e.flag < 0 && e.id != id) {
int d = D(cell(e), q);
if (e.team == team)
ally = std::min(ally, d);
else
enemy = std::min(enemy, d);
}
f[at + 6] = (std::min(30, ally) - D(z, q)) / 24.f;
f[at + 7] = (std::min(30, enemy) - D(z, q)) / 24.f;
f[at + 8] = flag.return_at >= 0 ? (flag.return_at - g.turn) / 15.f : 1;
}
std::vector<std::pair<int, int>> enemies;
for (auto &e : g.units)
if (e.team != team && e.x >= 0) enemies.push_back({D(p, cell(e)), e.id});
std::sort(enemies.begin(), enemies.end());
for (int j = 0; j < 4; j++) {
int at = 52 + j * 9;
if (j >= int(enemies.size())) {
f[at + 3] = -1;
continue;
}
auto &e = g.units[enemies[j].second];
int q = cell(e);
f[at] = (q % n - z % n) / 12.f;
f[at + 1] = (q / n - z / n) / 12.f;
f[at + 2] = D(z, q) / 16.f;
f[at + 3] = e.hp / 100.f;
f[at + 4] = e.flag >= 0;
f[at + 5] = hit(g, z, q);
f[at + 6] = D(p, q) - D(z, q);
int support = 0, threat = 0;
for (auto &a : g.units)
if (a.x >= 0 && a.flag < 0 && a.id != id) {
int d = man(cell(a), q);
if (d <= 3) {
if (a.team == team)
support++;
else if (a.team == e.team)
threat++;
}
}
f[at + 7] = support / 3.f;
f[at + 8] = threat / 3.f;
}
int j = 0;
for (int k = team * 3; k < team * 3 + 3; k++)
if (k != id) {
int at = 88 + j++ * 6;
auto &a = g.units[k];
int q = cell(a);
if (q < 0) {
f[at + 3] = -1;
continue;
}
f[at] = (q % n - z % n) / 16.f;
f[at + 1] = (q / n - z / n) / 16.f;
f[at + 2] = D(z, q) / 20.f;
f[at + 3] = a.hp / 100.f;
f[at + 4] = a.flag >= 0;
f[at + 5] = z == q;
}
for (int j = 0; j < 3 && staging[team][j] >= 0; j++) {
int at = 100 + j * 4, q = staging[team][j];
f[at] = (q % n - z % n) / 24.f;
f[at + 1] = (q / n - z / n) / 24.f;
f[at + 2] = D(z, q) / 24.f;
f[at + 3] = D(p, q) - D(z, q);
}
for (float &v : f) v = std::clamp(v, -4.f, 4.f);
return f;
}
template <class G> std::array<std::array<float, PF>, 5> policyMoves(const G &g, int id, int old = -1) const {
// The same origin, nearest objects and support counts serve all five
// moves. Keep the scalar implementation as the independent reference.
std::array<std::array<float, PF>, 5> out;
out.fill(policy(g, id, 0, old));
auto &u = g.units[id];
int p = cell(u), team = u.team;
if (p < 0) return out;
std::vector<std::pair<int, int>> flags, enemies;
for (auto &flag : g.flags) {
int q = -1;
if (flag.x >= 0 && (flag.return_at < 0 || flag.return_at > g.turn))
q = flag.y * n + flag.x;
else if (flag.holder >= 0)
q = cell(g.units[flag.holder]);
if (q >= 0) flags.push_back({D(p, q), flag.id});
}
for (auto &e : g.units)
if (e.team != team && e.x >= 0) enemies.push_back({D(p, cell(e)), e.id});
std::sort(flags.begin(), flags.end());
std::sort(enemies.begin(), enemies.end());
std::array<int, 4> fq{}, ally{}, enemy{};
for (int j = 0; j < 4 && j < int(flags.size()); j++) {
auto &f = g.flags[flags[j].second];
fq[j] = f.x >= 0 ? f.y * n + f.x : cell(g.units[f.holder]);
ally[j] = enemy[j] = 100;
for (auto &e : g.units)
if (e.x >= 0 && e.flag < 0 && e.id != id) {
int d = D(cell(e), fq[j]);
if (e.team == team)
ally[j] = std::min(ally[j], d);
else
enemy[j] = std::min(enemy[j], d);
}
}
for (int m = 1; m < 5; m++) {
auto &f = out[m];
int z = nb[p][m];
f[4] = z % n - p % n;
f[5] = z / n - p / n;
f[6] = 0;
f[7] = z == p;
f[8] = home[team][z] / 24.f;
f[9] = home[team][p] - home[team][z];
f[10] = home[team][z] == 0;
f[11] = (g.bases[team].first - z % n) / 24.f;
f[12] = (g.bases[team].second - z / n) / 24.f;
f[13] = old >= 0 && old != p && z - p == p - old;
f[14] = old >= 0 && z == old;
f[15] = man(z, (n / 2) * n + n / 2) / 24.f;
for (int j = 0; j < 4 && j < int(flags.size()); j++) {
int at = 16 + j * 9, q = fq[j];
f[at] = (q % n - z % n) / 16.f;
f[at + 1] = (q / n - z / n) / 16.f;
f[at + 2] = D(z, q) / 24.f;
f[at + 3] = D(p, q) - D(z, q);
f[at + 6] = (std::min(30, ally[j]) - D(z, q)) / 24.f;
f[at + 7] = (std::min(30, enemy[j]) - D(z, q)) / 24.f;
}
for (int j = 0; j < 4 && j < int(enemies.size()); j++) {
int at = 52 + j * 9, q = cell(g.units[enemies[j].second]);
f[at] = (q % n - z % n) / 12.f;
f[at + 1] = (q / n - z / n) / 12.f;
f[at + 2] = D(z, q) / 16.f;
f[at + 5] = hit(g, z, q);
f[at + 6] = D(p, q) - D(z, q);
}
int j = 0;
for (int k = team * 3; k < team * 3 + 3; k++)
if (k != id) {
int at = 88 + j++ * 6, q = cell(g.units[k]);
if (q < 0) continue;
f[at] = (q % n - z % n) / 16.f;
f[at + 1] = (q / n - z / n) / 16.f;
f[at + 2] = D(z, q) / 20.f;
f[at + 5] = z == q;
}
for (int j = 0; j < 3 && staging[team][j] >= 0; j++) {
int at = 100 + j * 4, q = staging[team][j];
f[at] = (q % n - z % n) / 24.f;
f[at + 1] = (q / n - z / n) / 24.f;
f[at + 2] = D(z, q) / 24.f;
f[at + 3] = D(p, q) - D(z, q);
}
for (float &v : f) v = std::clamp(v, -4.f, 4.f);
}
return out;
}
template <class G> std::array<float, VF> value(const G &g, int team) const {
std::array<float, VF> f{};
f[0] = g.teams / 15.f;
f[1] = (g.turns - g.turn) / 400.f;
f[2] = g.score[team] / 30.f;
int others = 0, leader = 0;
for (int i = 0; i < g.teams; i++)
if (i != team) {
others += g.score[i];
leader = std::max(leader, g.score[i]);
}
f[3] = others / float((g.teams - 1) * 30);
f[4] = (g.score[team] - leader) / 20.f;
f[5] = n / 49.f;
for (int k = 0; k < 3; k++) {
auto &u = g.units[team * 3 + k];
int p = cell(u), at = 8 + k * 12;
f[at] = u.hp / 100.f;
f[at + 1] = u.flag >= 0;
f[at + 2] = p >= 0 ? home[team][p] / 24.f : 1;
f[at + 3] = p < 0 ? (u.respawn_at - g.turn) / 10.f : 0;
int closest = 100, adv = 100, near = 0, weak = 0, support = 0;
for (auto &flag : g.flags)
if (flag.x >= 0) {
int q = flag.y * n + flag.x, d = D(p, q), en = 100;
for (auto &e : g.units)
if (e.team != team && e.x >= 0 && e.flag < 0) en = std::min(en, D(cell(e), q));
if (d < closest) {
closest = d;
adv = d - en;
}
}
for (auto &e : g.units)
if (e.x >= 0 && e.flag < 0 && e.id != u.id) {
int d = man(p, cell(e));
if (e.team != team && d <= 4) {
near++;
weak += e.hp <= 34;
} else if (e.team == team && d <= 4)
support++;
}
f[at + 4] = std::min(30, closest) / 24.f;
f[at + 5] = std::clamp(adv, -24, 24) / 24.f;
f[at + 6] = near / 6.f;
f[at + 7] = weak / 3.f;
f[at + 8] = support / 2.f;
f[at + 9] = p >= 0 ? man(p, (n / 2) * n + n / 2) / 24.f : 1;
f[at + 10] = p >= 0 ? (p % n - g.bases[team].first) / 24.f : 0;
f[at + 11] = p >= 0 ? (p / n - g.bases[team].second) / 24.f : 0;
}
std::vector<std::pair<int, int>> flags;
for (auto &flag : g.flags) {
int q = flag.x >= 0 ? flag.y * n + flag.x : flag.holder >= 0 ? cell(g.units[flag.holder]) : -1;
flags.push_back({q < 0 ? 100 : home[team][q], flag.id});
}
std::sort(flags.begin(), flags.end());
for (int j = 0; j < 8; j++) {
int at = 44 + j * 4;
if (j >= int(flags.size())) {
f[at] = -1;
continue;
}
auto &flag = g.flags[flags[j].second];
int q = flag.x >= 0 ? flag.y * n + flag.x : flag.holder >= 0 ? cell(g.units[flag.holder]) : -1;
f[at] = flag.holder < 0 ? (flag.x >= 0 ? 1 : 0) : g.units[flag.holder].team == team ? 2 : 3;
f[at + 1] = q >= 0 ? home[team][q] / 24.f : 1;
f[at + 2] = flag.holder >= 0 ? g.units[flag.holder].hp / 100.f : 0;
f[at + 3] = flag.holder >= 0 && q >= 0 ? home[g.units[flag.holder].team][q] / 24.f
: flag.return_at >= 0      ? (flag.return_at - g.turn) / 15.f
: 0;
}
#ifdef RICH_VALUE
// Preserve relative enemy geometry and wall-aware attack lines for each
// squad member, rather than reducing tactical positions to enemy counts.
for (int k = 0; k < 3; k++) {
auto p = policy(g, team * 3 + k, 0);
std::copy(p.begin(), p.end(), f.begin() + 80 + k * PF);
}
#endif
for (float &v : f) v = std::clamp(v, -4.f, 4.f);
return f;
}
};
} // namespace learning

#define OPPONENT_HEADS 11
#define OPPONENT_ACTIONS 4
namespace weights {
inline float opponent_w0[7168] = {};
inline float opponent_b0[64] = {};
inline float opponent_w1[2048] = {};
inline float opponent_b1[32] = {};
inline float opponent_w2[1760] = {};
inline float opponent_b2[55] = {};
} // namespace weights

// Learned policy priors + value-guided search over simultaneous joint actions.
// Opponents are sampled before our candidate action is evaluated, so the
// search cannot give either side access to the other's submitted command.
#ifndef OPPONENT_HEADS
#define OPPONENT_HEADS 1
#endif
#ifndef OPPONENT_ACTIONS
#define OPPONENT_ACTIONS 0
#endif
#ifndef NN_JOINT_BELIEF
#define NN_JOINT_BELIEF 0
#endif
#ifndef NN_LEADER_OBJECTIVE
#define NN_LEADER_OBJECTIVE 0
#endif
#ifndef NN_BATCH_FEATURES
#define NN_BATCH_FEATURES 0
#endif
#ifndef NN_ACTION_EVIDENCE
#define NN_ACTION_EVIDENCE 0
#endif
struct Planner {
using Prob = std::array<float, 5>;
static constexpr int NN = OPPONENT_HEADS, E = NN + 6, AK = OPPONENT_ACTIONS, NO = NN * (1 + AK);
static_assert(AK == 0 || AK == 4);
learning::Geometry geo;
std::vector<int> previous;
std::vector<bool> newLife;
int preparedTurn = -1;
std::array<std::array<std::array<float, E>, 2>, 15> evidence{};
std::array<std::array<Prob, E>, 45> lastExperts{};
std::array<std::array<Prob, E>, 45> workingExperts{};
std::array<std::array<float, E>, 45> headPosterior{};
std::array<std::array<std::array<std::array<float, AK>, NN>, 5>, 45> actionForecast{};
decltype(actionForecast) lastActionForecast{};
std::array<int, 15> sampledHeads{};
std::array<int, 45> lastClass{};
uint64_t rng = 0xD1B54A32D192ED03ull;
std::chrono::steady_clock::time_point deadline;
using Joint = std::array<int, 3>;
struct Candidate {
Joint moves{};
std::array<int, 3> actions{{-1, -1, -1}};
float prior = 0, value = -1e9f;
};
template <int F> static float dot(const float *x, const float *w) {
// Four independent reductions let ordinary -O2 exploit CPU vector
// instructions without architecture flags or a numerical runtime.
float a = 0, b = 0, c = 0, d = 0;
int i = 0;
for (; i + 3 < F; i += 4) {
a += x[i] * w[i];
b += x[i + 1] * w[i + 1];
c += x[i + 2] * w[i + 2];
d += x[i + 3] * w[i + 3];
}
float s = (a + b) + (c + d);
for (; i < F; i++) s += x[i] * w[i];
return s;
}
template <int F, int H, int Z>
float infer(const std::array<float, F> &x, const float *w0, const float *b0, const float *w1, const float *b1,
const float *w2, const float *b2) const {
float h[H], z[Z];
for (int j = 0; j < H; j++) h[j] = std::max(0.f, b0[j] + dot<F>(x.data(), w0 + j * F));
for (int j = 0; j < Z; j++) z[j] = std::max(0.f, b1[j] + dot<H>(h, w1 + j * H));
return b2[0] + dot<Z>(z, w2);
}
float potential(const Game &g, int team) const {
float phi = 0;
for (int k = 0; k < 3; k++) {
auto &u = g.units[team * 3 + k];
int p = geo.cell(u);
if (p >= 0) {
phi += u.hp * .00025f;
if (u.flag >= 0) phi += .45f * std::exp(-geo.home[team][p] / 14.f);
}
}
for (auto &f : g.flags)
if (f.x >= 0) {
int p = f.y * g.size + f.x, own = 100, enemy = 100;
for (auto &u : g.units)
if (u.x >= 0 && u.flag < 0) {
int d = geo.D(geo.cell(u), p);
if (u.team == team)
own = std::min(own, d);
else
enemy = std::min(enemy, d);
}
phi += .15f * std::exp(-own / 12.f) / (1 + std::exp(std::clamp((own - enemy) / 3.f, -20.f, 20.f)));
}
return phi;
}
float value(const Game &g, int team) const {
if (g.turn > g.turns) return 0;
auto observation = learning::prepared(g);
float v = infer<learning::VF, std::size(weights::value_b0), std::size(weights::value_b1)>(
geo.value(observation, team), weights::value_w0, weights::value_b0, weights::value_w1, weights::value_b1,
weights::value_w2, weights::value_b2);
#if VALUE_SHAPED
v += potential(g, team);
#endif
return v;
}
Prob policy(const Game &g, int id) const {
Prob p{};
if (g.units[id].x < 0) {
p[0] = 1;
return p;
}
std::array<std::array<float, learning::PF>, 5> all{};
if constexpr (NN_BATCH_FEATURES) all = geo.policyMoves(g, id, previous[id]);
float maximum = -1e9, sum = 0;
for (int m = 0; m < 5; m++) {
auto f = NN_BATCH_FEATURES ? all[m] : geo.policy(g, id, m, previous[id]);
if (m && f[7] > .5)
p[m] = -100.f;
else if (g.units[id].team != g.me)
p[m] = infer<learning::PF, std::size(weights::opponent_b0), std::size(weights::opponent_b1)>(
f, weights::opponent_w0, weights::opponent_b0, weights::opponent_w1, weights::opponent_b1,
weights::opponent_w2, weights::opponent_b2);
else
p[m] = infer<learning::PF, std::size(weights::policy_b0), std::size(weights::policy_b1)>(
f, weights::policy_w0, weights::policy_b0, weights::policy_w1, weights::policy_b1,
weights::policy_w2, weights::policy_b2);
maximum = std::max(maximum, p[m]);
}
for (float &q : p) {
q = std::exp((q - maximum) / 1.05f);
sum += q;
}
for (float &q : p) q /= sum;
return p;
}
std::array<float, NO> opponentLogits(const std::array<float, learning::PF> &x) const {
constexpr int H = std::size(weights::opponent_b0), Z = std::size(weights::opponent_b1);
static_assert(NO == std::size(weights::opponent_b2));
float h[H], z[Z];
std::array<float, NO> out{};
for (int j = 0; j < H; j++)
h[j] = std::max(0.f, weights::opponent_b0[j] +
dot<learning::PF>(x.data(), weights::opponent_w0 + j * learning::PF));
for (int j = 0; j < Z; j++)
z[j] = std::max(0.f, weights::opponent_b1[j] + dot<H>(h, weights::opponent_w1 + j * H));
for (int j = 0; j < NO; j++) out[j] = weights::opponent_b2[j] + dot<Z>(z, weights::opponent_w2 + j * Z);
return out;
}
std::array<Prob, E> experts(const Game &g, int id) {
std::array<Prob, E> model{};
auto &u = g.units[id];
int start = geo.cell(u);
if (start < 0) {
for (auto &p : model) p[0] = 1;
return model;
}
if constexpr (NN == 1 && AK == 0)
model[0] = policy(g, id);
else {
std::array<std::array<float, learning::PF>, 5> all{};
if constexpr (NN_BATCH_FEATURES) all = geo.policyMoves(g, id, previous[id]);
// One shared trunk per move, with small linear style heads.
for (int m = 0; m < 5; m++) {
auto f = NN_BATCH_FEATURES ? all[m] : geo.policy(g, id, m, previous[id]);
auto pred = opponentLogits(f);
for (int k = 0; k < NN; k++) model[k][m] = m && f[7] > .5 ? -100.f : pred[k];
if constexpr (AK) {
for (int k = 0; k < NN; k++) {
auto &action = actionForecast[id][m][k];
float maximum = -1e9f, sum = 0;
for (int a = 0; a < AK; a++) {
action[a] = pred[NN + k * AK + a];
maximum = std::max(maximum, action[a]);
}
for (float &q : action) {
q = std::exp(q - maximum);
sum += q;
}
for (float &q : action) q /= sum;
}
}
}
for (int k = 0; k < NN; k++) {
float maximum = *std::max_element(model[k].begin(), model[k].end()), sum = 0;
for (float &q : model[k]) {
q = std::exp((q - maximum) / 1.05f);
sum += q;
}
for (float &q : model[k]) q /= sum;
}
}
int goal = -1;
float best = 1e9f;
for (auto &f : g.flags) {
int q = f.x >= 0 && (f.return_at < 0 || f.return_at > g.turn) ? f.y * g.size + f.x : -1;
if (q >= 0) {
float cost = geo.D(start, q) + .15f * geo.home[u.team][q];
if (cost < best) {
best = cost;
goal = q;
}
}
}
if (goal < 0)
for (auto s : g.spots) {
int q = s.second * g.size + s.first;
float cost = geo.D(start, q) + .35f * geo.home[u.team][q];
if (cost < best) {
best = cost;
goal = q;
}
}
int victim = -1;
float bv = -1e9f;
for (auto &e : g.units)
if (e.team != u.team && e.x >= 0 && geo.man(start, geo.cell(e)) <= 6) {
float v = (e.flag >= 0 ? 5.f : 0.f) + (100 - e.hp) * .03f - geo.D(start, geo.cell(e));
if (v > bv) {
bv = v;
victim = e.id;
}
}
auto selfFeatures=geo.policyMoves(g,id,previous[id]);
for (int modelId = NN; modelId < E; modelId++) {
int behavior = modelId - NN + 1;
float maximum = -1e9f, sum = 0;
for (int m = 0; m < 5; m++) {
int p = geo.nb[start][m];
float progress = u.flag >= 0 ? geo.home[u.team][start] - geo.home[u.team][p]
: geo.D(start, goal) - geo.D(p, goal),
danger = 0, attack = 0;
for (auto &e : g.units)
if (e.team != u.team && e.x >= 0) {
int q = geo.cell(e), d = geo.man(p, q);
if (e.flag < 0) danger += d <= 2 ? 1.f : d == 3 ? .45f : 0.f;
attack = std::max(attack, geo.hit(g, p, q) ? (e.flag >= 0  ? 3.f
: e.hp <= 34 ? 2.f
: 1.f)
: 0.f);
}
float score = 2.4f * progress - .1f * (m == 0);
if (behavior == 2 && u.flag < 0 && victim >= 0)
score =
geo.hit(g, start, geo.cell(g.units[victim]))
? (m == 0 ? 3.f : attack)
: 1.4f * (geo.D(start, geo.cell(g.units[victim])) - geo.D(p, geo.cell(g.units[victim]))) +
attack;
if (behavior == 3 && u.flag < 0) {
score = 1.1f * progress + 2.2f * attack - .3f * danger;
if (victim >= 0)
score += .8f * (geo.D(start, geo.cell(g.units[victim])) - geo.D(p, geo.cell(g.units[victim])));
}
if (behavior == 4) score = 1.8f * progress - (u.flag >= 0 ? 3.f : u.hp <= 34 ? 2.5f : 1.5f) * danger;
if (behavior == 5) score = 0;
if(behavior==6)score=infer<learning::PF,std::size(weights::policy_b0),std::size(weights::policy_b1)>(selfFeatures[m],weights::policy_w0,weights::policy_b0,weights::policy_w1,weights::policy_b1,weights::policy_w2,weights::policy_b2)/1.05f;

if (m && p == start) score = -100;
model[modelId][m] = score;
maximum = std::max(maximum, score);
}
for (float &q : model[modelId]) {
q = std::exp(q - maximum);
sum += q;
}
for (float &q : model[modelId]) q /= sum;
}
return model;
}
std::vector<Prob> predict(const Game &g, bool save = false) {
std::vector<Prob> p(g.units.size());
std::array<float, E> prior{};
for (int k = 0; k < NN; k++) prior[k] = .65f / NN;
const float fallback[6] = {.08f, .06f, .06f, .08f, .02f, .05f};
for (int k = 0; k < 6; k++) prior[NN + k] = fallback[k];
for (auto &u : g.units) {
if (u.team == g.me) {
p[u.id] = policy(g, u.id);
continue;
}
auto model = experts(g, u.id);
float w[E], sum = 0, maximum = -1e9f;
int category = u.flag >= 0;
for (int k = 0; k < E; k++) maximum = std::max(maximum, evidence[u.team][category][k] + std::log(prior[k]));
for (int k = 0; k < E; k++) {
w[k] = std::exp(evidence[u.team][category][k] + std::log(prior[k]) - maximum);
sum += w[k];
}
for (int k = 0; k < E; k++)
for (int m = 0; m < 5; m++) p[u.id][m] += w[k] / sum * model[k][m];
if constexpr (NN_JOINT_BELIEF || AK) workingExperts[u.id] = model;
if constexpr (AK)
for (int k = 0; k < E; k++) headPosterior[u.id][k] = w[k] / sum;
if (save) {
lastExperts[u.id] = model;
lastClass[u.id] = category;
if constexpr (NN_ACTION_EVIDENCE && AK) lastActionForecast[u.id] = actionForecast[u.id];
}
}
return p;
}
void adapt(const Game &g) {
std::array<bool, 45> motionUsed{};
for (auto &u : g.units)
if (u.team != g.me && u.x >= 0 && previous[u.id] >= 0) {
int now = geo.cell(u), old = previous[u.id];
if (geo.man(now, old) > 1) continue;
motionUsed[u.id] = true;
bool blocked = false;
for (auto &a : g.units)
if (a.id != u.id && a.x >= 0 && geo.man(old, geo.cell(a)) <= 1) blocked = true;
for (int k = 0; k < E; k++) {
float likelihood = 0;
for (int m = 0; m < 5; m++)
if (geo.nb[old][m] == now) likelihood += lastExperts[u.id][k][m];
if (blocked && now == old) likelihood = .15f + .85f * likelihood;
auto &v = evidence[u.team][lastClass[u.id]][k];
v = .992f * v + .09f * std::log(.025f + .975f * likelihood);
}
}
if constexpr (NN_ACTION_EVIDENCE && AK) {
std::array<int, 45> seen, position;
seen.fill(-1);
position.fill(-1);
for (auto &u : g.units) position[u.id] = geo.cell(u);
for (auto &event : g.events)
if (!event.args.empty()) {
int id = event.args[0];
if (id < 0 || id >= int(g.units.size())) continue;
if (event.type == "attack")
seen[id] = 1;
else if (event.type == "pickup")
seen[id] = 0;
else if (event.type == "drop")
seen[id] = 3;
else if (event.type == "death" && event.args.size() >= 3)
position[id] = event.args[2] * g.size + event.args[1];
}
for (auto &u : g.units)
if (u.team != g.me && previous[u.id] >= 0 && position[u.id] >= 0) {
int old = previous[u.id], now = position[u.id];
if (geo.man(old, now) > 1) continue;
bool blocked = false, contact = false, refused = false;
for (auto &a : g.units) {
if (a.id != u.id && a.x >= 0 && geo.man(old, geo.cell(a)) <= 1) blocked = true;
if (a.team != u.team && position[a.id] >= 0 && !newLife[a.id] &&
geo.hit(g, now, position[a.id]))
contact = true;
}
contact = contact && lastClass[u.id] == 0 && !newLife[u.id];
if (u.x >= 0 && u.flag < 0 && lastClass[u.id] == 0)
for (auto &flag : g.flags)
if (flag.x >= 0 && flag.y * g.size + flag.x == now) refused = true;
if (seen[u.id] < 0 && !contact && !refused) continue;
for (int head = 0; head < E; head++) {
float joint = 0, mass = 0, unconditional = 0;
for (int m = 0; m < 5; m++) {
std::array<float, AK> ap{};
if (head < NN)
ap = lastActionForecast[u.id][m][head];
else {
ap[0] = lastClass[u.id] ? .4875f : .475f;
ap[1] = lastClass[u.id] ? 0.f : .475f;
ap[2] = lastClass[u.id] ? .4875f : .025f;
ap[3] = .025f;
}
// A missing event is censored: even an intended shot
// can miss. Positive events expose the action exactly.
float likelihood = seen[u.id] >= 0
? ap[seen[u.id]]
: 1.f - (contact ? .65f * ap[1] : 0.f) - (refused ? ap[0] : 0.f);
likelihood = std::clamp(likelihood, 0.f, 1.f);
float move = lastExperts[u.id][head][m];
unconditional += move * likelihood;
if (geo.nb[old][m] == now) {
joint += move * likelihood;
mass += move;
}
}
if (blocked && now == old) {
joint = .15f * unconditional + .85f * joint;
mass = .15f + .85f * mass;
}
float likelihood = std::clamp(joint / std::max(.000001f, mass), 0.f, 1.f);
auto &v = evidence[u.team][lastClass[u.id]][head];
if (!motionUsed[u.id]) v *= .992f;
v += .09f * std::log(.025f + .975f * likelihood);
}
}
}
}
float random() {
rng ^= rng << 13;
rng ^= rng >> 7;
rng ^= rng << 17;
return (rng >> 40) * (1.f / 16777216);
}
int sample(const Prob &p) {
float r = random();
for (int m = 0; m < 4; m++) {
r -= p[m];
if (r < 0) return m;
}
return 4;
}
std::vector<Candidate> moves(const Game &g, int team, const std::vector<Prob> &prob) const {
std::vector<Candidate> out;
int start[3];
for (int k = 0; k < 3; k++) start[k] = geo.cell(g.units[team * 3 + k]);
for (int a = 0; a < 5; a++)
for (int b = 0; b < 5; b++)
for (int c = 0; c < 5; c++) {
Candidate ca;
ca.moves = {a, b, c};
int p[3];
bool valid = true;
for (int k = 0; k < 3; k++) {
int id = team * 3 + k, m = ca.moves[k];
if (prob[id][m] < .00005f) {
valid = false;
break;
}
p[k] = start[k] < 0 ? -1 : geo.nb[start[k]][m];
ca.prior += std::log(std::max(.00001f, prob[id][m]));
}
if (!valid) continue;
for (int k = 0; k < 3; k++)
for (int l = k + 1; l < 3; l++)
if (p[k] >= 0 && p[k] == p[l]) valid = false;
if (valid) out.push_back(ca);
}
if (out.empty()) {
Candidate c;
out.push_back(c);
}
std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.prior > b.prior; });
return out;
}
std::array<int, 3> shots(const Game &g, int team, const Joint &move, const std::vector<Prob> &prob,
bool pickupCost = true, int willing = 7) const {
int p[3], can = 0;
float pickup[3] = {};
for (int k = 0; k < 3; k++) {
auto &u = g.units[team * 3 + k];
int q = geo.cell(u);
p[k] = q < 0 ? -1 : geo.nb[q][move[k]];
if (q >= 0 && u.flag < 0 && !(g.turn == preparedTurn && newLife[u.id])) can |= 1 << k;
if (pickupCost && q >= 0 && u.flag < 0)
for (auto &f : g.flags)
if (f.x >= 0 && (f.return_at < 0 || f.return_at > g.turn) && f.y * g.size + f.x == p[k])
pickup[k] = geo.home[team][p[k]] == 0 ? 1.2f : .32f;
}
can &= willing;
float dp[8];
std::array<int, 3> action[8];
std::fill(dp, dp + 8, -1e9f);
dp[0] = 0;
for (auto &a : action) a = {-1, -1, -1};
for (auto &e : g.units)
if (e.team != team && e.x >= 0 && !(g.turn == preparedTurn && newLife[e.id])) {
int q = geo.cell(e), need = (e.hp + g.damage - 1) / g.damage;
float reward[8] = {};
for (int mask = 1; mask < 8; mask++)
if ((mask & can) == mask) {
for (int m = 0; m < 5; m++) {
int z = geo.nb[q][m], hits = 0;
for (int k = 0; k < 3; k++)
if ((mask >> k & 1) && geo.hit(g, p[k], z)) hits++;
float kill = hits >= need ? 1.f : 0.f;
reward[mask] +=
prob[e.id][m] * (.045f * std::min(need, hits) + kill * (e.flag >= 0 ? .65f : .20f));
}
for (int k = 0; k < 3; k++)
if (mask >> k & 1) reward[mask] -= pickup[k];
}
for (int mask = 7; mask >= 0; mask--)
if (dp[mask] > -1e8f) {
int left = can & ~mask;
for (int sub = left; sub; sub = (sub - 1) & left) {
int next = mask | sub;
float v = dp[mask] + reward[sub];
if (v > dp[next]) {
dp[next] = v;
action[next] = action[mask];
for (int k = 0; k < 3; k++)
if (sub >> k & 1) action[next][k] = e.id;
}
}
}
}
int best = 0;
for (int mask = 1; mask < 8; mask++)
if (dp[mask] > dp[best]) best = mask;
return action[best];
}
std::vector<Order> enemyOrders(const Game &g, const std::vector<Prob> &prob, bool stochastic,
const std::array<int, 15> *fixedHeads = nullptr) {
std::vector<Order> out(g.units.size());
sampledHeads.fill(-1);
std::vector<Prob> conditional;
if constexpr (NN_JOINT_BELIEF) conditional = prob;
const auto &movementProb = NN_JOINT_BELIEF ? conditional : prob;
if constexpr (NN_JOINT_BELIEF) {
if (stochastic)
for (int team = 0; team < g.teams; team++)
if (team != g.me) {
int chosen = fixedHeads ? (*fixedHeads)[team] : -1;
if (chosen < 0) {
std::array<float, E> w{};
float maximum = -1e9f, sum = 0;
const float fallback[6] = {.08f, .06f, .06f, .08f, .02f, .05f};
for (int k = 0; k < E; k++) {
float likelihood = 0;
int alive = 0;
for (int id = team * 3; id < team * 3 + 3; id++)
if (g.units[id].x >= 0) {
likelihood += evidence[team][g.units[id].flag >= 0][k];
alive++;
}
if (alive)
likelihood /= alive;
else
likelihood = evidence[team][0][k];
w[k] = likelihood + std::log(k < NN ? .65f / NN : fallback[k - NN]);
maximum = std::max(maximum, w[k]);
}
for (float &x : w) {
x = std::exp(x - maximum);
sum += x;
}
float r = random() * sum;
chosen = E - 1;
for (int k = 0; k < E - 1; k++) {
r -= w[k];
if (r < 0) {
chosen = k;
break;
}
}
}
sampledHeads[team] = chosen;
for (int id = team * 3; id < team * 3 + 3; id++) conditional[id] = workingExperts[id][chosen];
}
}
for (int team = 0; team < g.teams; team++)
if (team != g.me) {
auto options = moves(g, team, movementProb);
Joint mv = options[0].moves;
if (stochastic) {
for (int k = 0; k < 3; k++) mv[k] = sample(movementProb[team * 3 + k]);
int p[3];
for (int k = 0; k < 3; k++) {
int s = geo.cell(g.units[team * 3 + k]);
p[k] = s < 0 ? -1 : geo.nb[s][mv[k]];
}
bool collision = false;
for (int k = 0; k < 3; k++)
for (int l = k + 1; l < 3; l++)
if (p[k] >= 0 && p[k] == p[l]) collision = true;
if (collision) mv = options[0].moves;
}
auto ac = shots(g, team, mv, prob);
if constexpr (AK) {
std::array<int, 3> kinds{};
int willing = 0;
for (int k = 0; k < 3; k++) {
int id = team * 3 + k, m = mv[k];
if (g.units[id].x < 0) continue;
std::array<float, AK> ap{};
float total = 0;
for (int head = 0; head < E; head++) {
float weight = sampledHeads[team] >= 0
? (head == sampledHeads[team] ? 1.f : 0.f)
: headPosterior[id][head] * workingExperts[id][head][m];
total += weight;
if (head < NN)
for (int a = 0; a < AK; a++) ap[a] += weight * actionForecast[id][m][head][a];
else
ap[ac[k] >= 0 ? 1 : 0] += weight;
}
if (total <= 1e-8f) ap[ac[k] >= 0 ? 1 : 0] = total = 1;
for (float &q : ap) q /= total;
int kind = std::max_element(ap.begin(), ap.end()) - ap.begin();
if (stochastic) {
float r = random();
kind = AK - 1;
for (int a = 0; a < AK - 1; a++) {
r -= ap[a];
if (r < 0) {
kind = a;
break;
}
}
}
kinds[k] = kind;
if (kind == 1) willing |= 1 << k;
}
auto fire = shots(g, team, mv, prob, false, willing);
for (int k = 0; k < 3; k++)
ac[k] = kinds[k] == 0   ? -1
: kinds[k] == 1 ? (fire[k] >= 0 ? fire[k] : -2)
: kinds[k] == 3 ? -3
: -2;
}
for (int k = 0; k < 3; k++) out[team * 3 + k] = {mv[k], ac[k]};
}
return out;
}
Game step(const Game &g, const Candidate &ca, const std::vector<Order> &enemy, uint64_t seed) const {
auto orders = enemy;
for (int k = 0; k < 3; k++) orders[g.me * 3 + k] = {ca.moves[k], ca.actions[k]};
World world(g, seed);
world.g.turn = g.turn - 1;
world.step(orders);
world.g.turn = g.turn + 1;
return world.g;
}
float reward(const Game &before, const Game &after) const {
float r = after.score[before.me] - before.score[before.me], other = 0;
for (int t = 0; t < before.teams; t++)
if (t != before.me) other += after.score[t] - before.score[t];
if constexpr (NN_LEADER_OBJECTIVE) {
int oldLeader = 0, newLeader = 0;
for (int t = 0; t < before.teams; t++)
if (t != before.me) {
oldLeader = std::max(oldLeader, before.score[t]);
newLeader = std::max(newLeader, after.score[t]);
}
r -= newLeader - oldLeader;
} else
r -= (before.teams == 2 ? 1.f : .5f) * other / (before.teams - 1);
if (after.turn <= before.turns)
for (auto &u : before.units)
if (u.x >= 0 && after.units[u.id].x < 0) r += u.team == before.me ? -.025f : .025f / (before.teams - 1);
return r;
}
Candidate rollout(const Game &g, const std::vector<Prob> &prob) const {
auto ca = moves(g, g.me, prob)[0];
ca.actions = shots(g, g.me, ca.moves, prob);
return ca;
}
std::string run(const Game &g) {
auto started = std::chrono::steady_clock::now();
deadline = started + std::chrono::milliseconds(g.turn == 1 ? 1600 : NN_BUDGET_MS);
if (!geo.n) {
geo.init(g);
previous.assign(g.units.size(), -1);
}
adapt(g);
auto observation = learning::prepared(g);
preparedTurn = g.turn;
newLife.assign(g.units.size(), false);
for (auto &u : observation.units) newLife[u.id] = g.units[u.id].x < 0 && u.x >= 0;
auto prob = predict(observation, true);
auto options = moves(observation, g.me, prob);
// Compute shots only for the joint moves the search will actually visit.
// Most of the 125 legal combinations would otherwise be discarded.
for (int c = 0; c < std::min(NN_WIDTH, int(options.size())); c++)
options[c].actions = shots(observation, g.me, options[c].moves, prob);
Candidate best = options[0];
#ifndef NN_SEARCH
#define NN_SEARCH 1
#endif
if (NN_SEARCH && g.turn <= g.turns) {
const int samples = NN_SAMPLES, width = NN_WIDTH, beam = NN_BEAM;
std::array<std::vector<Order>, samples> opponent;
std::array<std::array<int, 15>, samples> headScenario{};
for (int s = 0; s < samples; s++) {
opponent[s] = enemyOrders(observation, prob, s > 0);
headScenario[s] = sampledHeads;
}
auto now = std::chrono::steady_clock::now();
auto rootDeadline = now + (deadline - now) * 3 / 5;
std::vector<Candidate> evaluated;
for (int c = 0; c < std::min(width, int(options.size())); c++) {
if (c > 0 && std::chrono::steady_clock::now() > rootDeadline) break;
auto ca = options[c];
float sum = 0, worst = 1e9f;
for (int s = 0; s < samples; s++) {
auto next = step(g, ca, opponent[s], uint64_t(g.turn * 7919 + s * 65537 + g.me * 101 + 7));
float v = reward(g, next) + .97f * value(next, g.me);
sum += v / samples;
worst = std::min(worst, v);
}
ca.value = .9f * sum + .1f * worst + NN_PRIOR * ca.prior;
evaluated.push_back(ca);
if (ca.value > best.value) best = ca;
}
std::sort(evaluated.begin(), evaluated.end(), [](auto a, auto b) { return a.value > b.value; });
#ifndef NN_ACTION_REFINEMENT
#define NN_ACTION_REFINEMENT 0
#endif
if (NN_ACTION_REFINEMENT) {
// Let the learned state value decide whether to pick up a
// flag, keep fighting over it, or release a carried flag.
// These legal actions were absent from movement-only search.
std::vector<Candidate> alternate;
for (int c = 0; c < std::min(3, int(evaluated.size())); c++) {
auto ca = evaluated[c];
auto attack = shots(observation, g.me, ca.moves, prob, false);
if (attack != ca.actions) {
auto other = ca;
other.actions = attack;
alternate.push_back(other);
}
for (int k = 0; k < 3; k++)
if (observation.units[g.me * 3 + k].flag >= 0) {
auto other = ca;
other.actions[k] = -3;
alternate.push_back(other);
}
}
for (auto ca : alternate) {
if (std::chrono::steady_clock::now() > deadline - std::chrono::milliseconds(4)) break;
float sum = 0, worst = 1e9f;
for (int s = 0; s < samples; s++) {
auto next = step(g, ca, opponent[s], uint64_t(g.turn * 7919 + s * 65537 + g.me * 101 + 7));
float v = reward(g, next) + .97f * value(next, g.me);
sum += v / samples;
worst = std::min(worst, v);
}
ca.value = .9f * sum + .1f * worst + NN_PRIOR * ca.prior;
evaluated.push_back(ca);
if (ca.value > best.value) best = ca;
}
std::sort(evaluated.begin(), evaluated.end(), [](auto a, auto b) { return a.value > b.value; });
}
float deepBest = -1e9f;
for (int c = 0; c < std::min(beam, int(evaluated.size())); c++) {
if (std::chrono::steady_clock::now() > deadline - std::chrono::milliseconds(3)) break;
auto ca = evaluated[c];
float sum = 0;
for (int s = 0; s < 2; s++) {
auto next = step(g, ca, opponent[s], uint64_t(g.turn * 7919 + s * 65537 + g.me * 101 + 7));
float r = reward(g, next);
if (next.turn <= g.turns) {
// Future decisions observe the movement just simulated,
// not the history two real turns before the rollout.
auto savedHistory = previous;
auto savedLife = newLife;
int savedTurn = preparedTurn;
for (auto &u : observation.units) previous[u.id] = geo.cell(u);
auto futureObservation = learning::prepared(next);
preparedTurn = next.turn;
for (auto &u : futureObservation.units) newLife[u.id] = next.units[u.id].x < 0 && u.x >= 0;
auto pp = predict(futureObservation);
auto action = rollout(futureObservation, pp);
auto eo =
enemyOrders(futureObservation, pp, s > 0, NN_JOINT_BELIEF ? &headScenario[s] : nullptr);
previous = savedHistory;
newLife = savedLife;
preparedTurn = savedTurn;
auto leaf = step(next, action, eo, uint64_t(g.turn * 6547 + s * 31 + 11));
r += .97f * reward(next, leaf) + .9409f * value(leaf, g.me);
} else
r += .97f * value(next, g.me);
sum += r * .5f;
}
float score = .65f * (sum + NN_PRIOR * ca.prior) + .35f * ca.value;
if (score > deepBest) {
deepBest = score;
best = ca;
}
}
}
std::string out = std::to_string(g.turn);
for (int k = 0; k < 3; k++) {
int id = g.me * 3 + k;
out += ' ';
out += std::string("SUDLR")[best.moves[k]];
out += ' ';
out += best.actions[k] == -3   ? "X"
: g.units[id].flag >= 0 ? "-"
: best.actions[k] >= 0  ? std::to_string(best.actions[k])
: "P";
}
for (auto &u : g.units) previous[u.id] = geo.cell(u);
return out;
}
};

// Experimental FP16 quantization of public MeowMind weights. Embedded, CPU only.
namespace ctf_half_storage {
const char alphabet[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!#$%&()*+-;<=>?@^_`{|}~";
const char data[]=R"HALF_DATA(
S3D2GmbO!;88qiGzq(mJ2fDyE;JgdER5@NRP`UUk7ag!QjWh(Y2`whM7Pu?CdA&$7yelFt)w9ti0-<T7)v3<7tua6;POcRw$SEwdeKk^`jwYWfAu+2purNEQ;x@QAk1RPc&NWB2AwA(g2{dCh*Rjw%1Gd&KcDBW{@V%ck)H+MBfVpQcGd3JD5V{Df`?20R;<EiZ=CUL(oisHptTi{S8bHIU<hioAU^QbjnXqp%d#~j?A}GT&OSyJ1^tp61);znkZax~VajPn{<21#s$~Y#mSvy9td_dB*QaI?jL^0Gc<+RqkJv;iLn!R|mhco#&1F49(pf3Bk{yxPpJvFMhQL<_}B)#{uYBM6d@id_@=a{}WLOixHTC_?kT{Os_e=>J9{1(QbK(2nSF*V&XYBY8*!7>swb}_fL`Y6`5Oh7R@9I|UK&mn#_47v~`CA9xLIJql1-#552hqU3m->{GutTGL>jXCK(bTE-QP`SLSH>0yO5wY7XCODoX!NJ@(aWXh0hBnn8tGNBFm8oJkuCXgKueJlQro3ac<-*Y_WV4GjX*DOb94U;oLck%dZoCmXFFWC`l`+Y)zq8Xryggd5VYsQVOgSUASv8HhhBT8rl|H01WHLuM)inmdZ#nTX_$x^^L8}p|FEuDE;kfuVjx)O+?J)wXz%5v?C^&2|X0x}er?dvDr>nT9<+0hcFEMtm#5d(MrNhLzFuh-<P&Y)icszF|vaVveI6H%~S2Pnd?793ncQVMd_p{2d8MR*}<S&9j9XXn`aWoP$s;nZlT)7XZ9zp)LskQv9Ni<g}=(1_F?=-kO_BoP~E40qC<0#}T47ty>b-Amwo;bjviMw|+M=`szT0UYnJ~wwI1g}9j>O4v^{W-BTm9@G#7A*`l|F}i5Wj4z!!Z<IrIT_cu5V^~+55S?bjWsf~1Go$@-?qIpXST<<^R-GeN~E4HY#nT}XEr@C<gl)-9WYL_$*`3#53px6Z7;pKZ8#)3MnUDY>^ES%u(JBTe5qSBldmGR
jJ!xV1Fv<sAU9Jr?=bJKsk8{Mqc}f6+ou{pak|Pg9JV^N9w+!Q$h7^elsmUMfjF`#PCOH{2{}ACLOI{GqBS(MSUBLmJU@pu)HqEmbS`JDMYqJTuQzrtpuvEy**^BaT{ycqR4(Q)61`z9={~Wt?lKxWbTsm@i?gz{kv5mORKl|`(X(#4F*BXMp)|iTTdcG+KC_9fc{9DWye<Vf*|1D4XRxT0P_FE@7q#W6cPjreARuS9RJ7r(<)$>YM>sgLCbW3D>p{ym;WTSK4mPK~8!@!Apg2D}MK&0)#6CW#iazi$DzS4g1~@;lzdAWNa52X`>$u0Of368Sxw5l0;yRM7!n44$gSXo=^sGy;k-FBrNVp=g$2D!VDzWN6db=pMO|*Km&anwHI<~+m#<C?l2{9S1SvVU(S27N_j5!CS(JUxGN--C<owdHT2sn4Fv9(P)_qMD&s=*pE_bbOT6D1(Jlr(3uS1e~VVl@OX$17PlH>$I)+%nm(X0jwPnj|zUKQ09^8Z|npSF_fzcC59eJTio-b{m4HjxD;JzBI2hkv6lu+OQG0W34|hlrnHMQZOnvuD+S5ShNbXf3x;5N4H_Btgr;cMz2S<05WT)G9~#sxuI4y<UPkL<GHXl*sxkW=efc#ax^KpQ?;5q7P!S4$}~E*HMonl&95#tyE?iv__%5*F)F9D!=}`$lQf7uakWOe>926C*(ldQiM@8M0lLq*__;E+SiGaQbFsrE`ok8y4yeB=o-%T+{;_GZaIAQuLb5@vtu8+)IXizhATzQoP$fmCLM{6=$1g51kF}91%`np^pDvyqb2aTYHMFv@JGc)!ctLzH_q0_rS2U78=Q#VeMKV%2ma9`W;kC^!M=gXa%Q#myOD}D(!MKpSFfu*0mAPE73ad0X#SEM`20wi@Ni}phQ#eMc_B0cyCN%{$W;x`vfwb5!6uN0VCAw0)G&CzPb~&0UD>`GPg18ksnmqcm0la)cpSr!AjW2^RMkal{MLPX9KDG8BC_0p^bh0Eh
ytIz7KRco-?XX5M&?-16Ho1r=6f{~jG9`qf>a%LJu{0F0Whu`zW3r?$)vE+EGqS`ml`+$-BC(|)Nh>0!k*dwL+%n=cW--AuX~em?inO$`+_#@J?mqgXva}q-u{&0}aHQ`hJ~$jZs4P{onz)IvQZg^Oc&r#OTD0k=(4-wW6RzGgz&b`Lu({_stsOqIpEz15_cScLe6U(HTQOs)2sj6}xjId?l{~>Q7puB8(lTo)#=8wQfjyxmbH6w@O1kwgAhZQ5K(lhd__viL8nb~o?K1?hJ2Y>$K|1m-hCItFHnE1V$uX8U3a0@$2DDN*;VQ4JdbUEi?;D*dS)cSS1Gj7^p&>Z9aH^v=s<HsIUbL{ZnXMxrW3-;N;;<Dl%qTKEnn1=q{=sQD7`WZFe>XNV53e;pHo<%~FgaE;hBUsnvMq2Xmp`|;i7<J+_As)&+cQ-#c_>ynRI^zqIk050yR`c=nY2r{h`oL@1~!m9hqOU5D7g(aqAw{u4mldK7`ACDbTF#9e72Pz2DGI%#j0Ycx~<y4r8*3<zN=I&aJ<h%M7C};;5X2>x4GH2<R%2b)GiFRb}IC=3Nx#*It*kvr@bUI4yThmPBK<7Vl!(l3c5bMR<5xyz??r83osxxq8!ShxUjJ&bSNw-Pb`@wRi^H<zAnh9ls1UClR920GBn00S*Sxi88gl=yeu)aue3-wsj}K1C%B=oI56<CakG9kr7RgTpfTCASvn%Acs4P%Vy)A$T)rzWNIQMBxislAo4$!QeZKp&bu}us8o$^z&AoXsldUhcFtp&G^)zraq`Tp$K&^MP!Mk0r@H#I%VK{QB#S7joXS{+yjldT?G&Gzt380~{Wi-3CbGVHpr@D_ehc)N6XS51B)IdV8Ml9Jd&adGr)-!ZBsWmJ!zAs)h9k975f~jMxaV|l*{w_VF(k?x&rZIP^MHnu)QnD*CEVUuF^h4aOZ#2R*CY~0%A~qVYk~2m&L9}eMOt<E+SvJ)+t1RcH<S&M?aWs;-zbLIZC^IfK
FD<RDGBDONio6W0BselQAFyyS4YXFVptW1A0J2OnyR&C3l{dJ!Rk&oifi{Dr12<wgo4U}fI<YS{`LQN1AT>F^m$X7S8?|4rpu0>yH@=;<ggG{~BrvNmsWmFQ1GqCbLcz1RuDFG}JTz`Y%(c6<9kn^Q53z)vS~3T#l&Myg`>+ZnEV1LX{;aAjo-{Qz(<`K^!L+Baj4914#<A3|Tr#7vl)8O0DL<CGZaoAyDy+LauDe&UlB<%sRyMb{)wNA6Q#wPrt{+h_$~<bV{k9!6i>%BubvB0}b+uDAE3dw{x4UblM?40h<Sei<|2i$O@4WT32$~->HZi}e+qE${eKw~%(L6b<H@PA;&A9n0T(rHj=e->*0kbH;hqW=ZUoyWc>AI>wqdzAlPBO@)&Z#jdd^r6nO|3$(uRP_pn5vg47QAG;?KlIsfU)ATG`0q^+bQXwccsZL7Oo95&oa5Q<E?Quza@UJRW(Sd0j<9<wX5DMd8+oY6Ej1t<|(KvQND>c_&61{$S0&Y;Ia-h0;x-}thjizV>z~;?YFWl7PZH;g}Y5Kjkv$NYpwe~@VUM*;j@-92BvH{hCHFH={XfPnmGor!#ZQF4Li@apEp0S06hb*ZY+JgXERx{5I#bu473=vtw6D?05zDjt2fNDWIRp3kTHU=Yb?MphcI<RB)L>30=JT{B(do(T%e${+N~q4r6|^=l%_2&hnAN)<26~Yw5xKdRxF*O3$8UTi8+$7P@OC=hO6|ci5Vj;kTePaT`{^J&MWh()~>OYTQHq8P#>2eOe#4ifW#lee>1eWOgA(|6E|@)f3u7{TQUi)i!^koJF$5%VWjS=QZTHvEwE{{cd6&M=BYj>93vMgnY0nCx-bo)7&)J;6Ry*=O0!ijBQ)|k=d$UmSSSQ9v?fYD*|+<&@Ud7VGq6WAqA2z&o+TM9sIfJo4=lyN95pAJhN*lpTDb4L8pQ9kg1N)Ao}*MJfV@3Lz9sXw5jiC@OD{n*jWq&1xHwC#Td3l+LoB{4*{xVL
tRwX_g|N*u1ur5kn5YS{ODlx8N2w6B^fcwPcchc3UnQ0>XE9MU3$13Yx;-?wL8v~rFE0YSwK{h)o3rOU1-FtdGrk@znYJdi#5PK=zb#EA<}u5^C#e!XU9k-%<T1OaH?2M`_d?Jk7_~<;LA9!|12w3vey<+1fwQZ%YBgphGPgUe0XFkJd^F@SMz%eyCb7IT=B>=PL^Mb<I5ep`$T!=ll($VVeYy)mr8-BjNHk?HN~C$Sc`GTf4L#?zIKaTINV3l}f~RD==eS%LW-{YBu`F0Ezc}=@NUu6CDKkwh#Gq}aIxc9k#53lt>n1)cQ?#x)2C(BS=`m=gEv>>iB(n;>hP!4uyREje8ZHq&Dy`)?$uZ_Q4ZU19*1TY*VJby2&oi_)tT(7TWG)gv6fs*q!LFt>W-@3nWimLmM7|v`Ni%#pMLadFH9i5eFSh45yEOH&f;~j8lB{zvUA?k3cQoiT_cn7jTQey!E;__9ak!APJvyer&$LvquBnix1~~ITTCvkI*g3bl9k}hSIkrqTEjS=4AiN*9QLLu5+bvSM;<mdp1h_W2yrrwD+ah(eLbb*+WwKS6^Dk*K$|WJOWj5_H-zp$6#;_f*XreVGO|*ZsMJg02?KeX<cR(z>9yWxrOuL)BSFV^esIq%Ac_uip1U8ze?z~mG>8y@2BQaDe1gGVrYb>BQ>!MJwwkRYidojN)TfycuyfghP#W-NGV!4#1->u^`skjy@F0ceMA~^Ury0gtU`!!uH3%tfNBQVCS7PS~QaW%=SPA}s@jx;)>cda6<vMG^1<g$*aTp>^{$|^K6CoaA_n6%5Zv^W7QmneNR@vP)0bGBot#IMsedZZMceluR6Pq19IH!X9r!!cDZz@1eq_^}kO9<%wQccLb+jV{ihm8vqa9I0}#5wP8;z&P`)J+P)EKBD}z!8K{IDlk&9<*wB)GrCQ#sx$;N;jK2XMk&gzA}|-Zx~NJy<ew0*gsdI5)hT(fuDj`^;jWl5-mZkSAvZj&-K<cwp(q_H
Fr-tgm9uUuK&LLc6UfXpGPv)p_&sT<l&I0QUpvvR+%zG;cenmElau1Bw<*G@P&?<a_r4^!`?FWJ2Q<*M`zC!XyE+yv(x#2IJ+tL5MK}R8bf_CP6e%00cCd*h>aI;JZ!r<BLM_ayC8e{m4XNBThAQ2x7A<Qm5H0_#Y^h_iKQ=xu7dSyH$2W+uu{Cit-z?ZXS2ukztTb>nz%p$plDK%TJ0GXDO)Vm>OR=x0aWi&3A+VXYnlJ{hhA#EEg)~O40lm+pxS&Ebk~gQdH8p^(3$gaLRw_p|_c+h9s4lI-3Or;yKg9_+e=>wKu0{~E95mds;J_m;B{(WEL@cnh9Uj9hU@5CeJ~bGqKCg<gSG2-3Of<hNg)n=ui#gjVwW^6R7p}}Rn6yGNdLu40qO5wSj;N`x4Jt>mFRb)2sH$P6SSM;NFDdG!+NzPPFETeY%BhK!gRXrv>n@0{>8CC<nLQgathLxM+pKQ5MzHua|1-?W_Ag<&6uj6p%Cr8kZjcivZk}K(<}JTCprRExys+%2Bq)<9J1AwRk28m+#x#|y6S4^>7b|A0wyO#$7qBxpwWzzPHLk}l0j)MLh%w!${jI$<3$z#>jWw~a+O>DC6TUG)_AHY%cr;U`oV-aq4z&|0@G`74+od<EZZlOb*0j1gw834xh_|t}GB;{I8ni~M<0s%apR^++t1-E%O+8*Tx3EaGb~?(cTB@P9k}?i57Poz;(5;X*3$4Z-?lVs-RyMdaU@@?)bFewHm9vsIgEBrj#I;p2)E&#dxiX=-#5wn&cf+eQceDAlS+pE9wliQdCM7IDo-%f$H#Bpz12b8=qd(UhPPAjKaWC1h+p>@}g}U`F?<0-19=L=rP`ao$U@!nRqPrxkDX6Ho5WilsOE_e<@wWS)bG(_rptv`%A29bcVx`Ev*s(#io4U0-#yN&83LEG-gteD3UpgZ*akuHP&MWOX53p)9@3f6OfGe%6qPBB3Au`#sGBxe6(xeA1G_WqT&A8k<yCA@^ceJ1}lreQE
=`oo&Tria{bg&OR0>rqw#xFaz;jQGqx-pZjU$t06xH+FVakn=yMzWtYwyZ`ixH_n@`l)`xbft>9sxTk3%(0}nY`0@9CccU=EWGbHQnz=onmYobhPgL3ay44GqCCR0_q>8L3bdfQNjD@g>83R{=DayGoVjZ<2{d=7SUEksfUtEinWv5_ur)8kKsI+BU8MgkQm(=<U%<PkO)N#VJu_r0_b6<%PpEV;A26n_&!@+;ZK_nG6tMxdj4+%ke5$~%Xs<#g_8}=RP_t97J}>m6xG>SFHL3<B!!RYV7A?awQ7Eyo0!oRXusq;4#;H9ruB1XVRkB&IM=ABPZ@5V+G!$VkryHk{-L0yykum7PFP)RRjIqox$f!88#xWcyKe^N`2|0H%jwyqx(6H4cO0_|)FQ}}vi?IS7872+7r!>_q%(ow>rZUW~1-Mox)+}{58?1vX`?!rhjx>C@y{zb=x;5-LN1f;`$vMC}5VdtR$g$$Mb~v9jT)u2E)-xZo2RyjAbS)t@VyMY1L#pquL@<&w|FgiWwlhSeh9(IrbSwlfjWsN>NU{X71hP-J#3mseIxyF=ZL^uPS3niJq(uS5Qas)|PcPa-N-m(Y+&4qGV!FRH{4`N3N4GntGdO^_^0F|mX|(>Vz%dFvY%PT=IW%{x=PbFbfxw-tXSf8ns<I!o5V?;oZM=ZI)Vd!yk+X1!Ft=$kzd64<E4E~@?=~<j(Kz0x?Ws6Ao-={2fgvTphB<C2$gi=m_O@R>$+n#$x~0Rc&a_A_r!^tCps5zMPdVr=Eu0OkzOrPohMVdz1TFfvA}txNsIY~u-LoVfeyLWk%PC1PIib$7TAqR}OfV@ahq0R_$|_ru)eMj`2du8Em@-VW4>T{hjX5?h4J{=&d@!LdtF!Ak_@P&-jjuMWce+TbP&N!Bc{r)EyQ)BlZz+x)4Wi4e1f$P2mL`of8NmxEE4ZVwGAtjk?=NAkBCm%k=&z<Uy)(=s@Uwe2GB`i0^FiG(a<IR-s>f_K8MFX2-b7lj
(mmw9#JqsDq^ayNPrV7e7&%wCN;&s8@G)<tK&yqjZ#o1yu|Bmc{ikX;UASdB(Y%+dld*KV9W03z5V*fJ9W?qi&obhzqPLeY$1GR1=r7JR=`=+lLNYeCE2?&=p)j&CQ7$vEg*6EvsXYI?F0qQW5g?~F5~(ga(JI}yhOn<J2D#5G-mt)_94tjJ9xe^C0XVC-HK!mlN->?Rz^sP6rY>(VNj}D{7raxfjI!A>MmWtXW3F5@p0+_bOFJ+vtg~%6RJR~C-atGp+BST-;x>&i^)&FfC_&0INj%2H=CfF^@~)_}s3*Uw$22^tsj2s@6fkTrm^D){wy-X&yRxt-XO_P!xG0q~ptUM8kSrB6xF)6_P%yl$va*Y@94|yKh_dh^;u^UyJf&l$FeI@kkFL<C@+_sDU9dW|6r@KmxU7n<d`qUZ5j%Igj<7p6DI)-^lQbW+X|m`njkQdzk~-Y52rh9f2e#R;+OT-Og028Ft+f;|*elpE;+e>`6|L5+(k|L7&89anwY2aox3HlqFsP`pWiPBNk|@BqCo4g-awYRE^)VALUbT=S2Q0=id$<pxd^z;J-Z!~8U$GN0QYdG*2Rt6LyE1XK7p@33F)6gaBQtR~q(U;W=`2Jz#WDIdcD`Arx-^uhWH47Ht}>!9898b$1+RTJ*EcXE2dpAEM6o9{BC!uITs4TO53AyqP_aKT-7RmUsV(g~1weo=uq=(c>nSBd5ilm6LoDe#!ZEuuiL@**mAuzBg&c0P0ld|ySG57Sqb!BGU^8DenKP{@*|0^mOg-4Hc)f74fUH8Kv^Li)jyi&@BeZ<Dk~R9JjX3qY>9ldQF2KDmI<7*wpTOlQGA=(f=)a^gBfxz@OSrhRyeRo9gSaHSwK*y|?Y08CO}KP8h_`*MP&O<(+&p_JFEGtG(5()&<f7gy?y0>nWiHIDg)!$TZL~f!X0Zk|wX$WL@h{~tJu&UD+P4lc;U<AHaVuXT@UN6K3bc=|8N8t~oj9X3al6p5*|t`%pFDOowzku}
iLi<{EVT!(NWROt%&jJ{*t{P#K(lqhWU_O-)HFmhil)%E88Bt9VZ041gT0Bcm$#NJX)MXES2E-|xIET4j5oxw!ZmF+8#w^4!aQFvvLJx8F}lDlyEX~7RXlqyF*p>%FEBPUUM-)ob++w5ZN2q2>a6oRU$q3eZ!?O%hBTP9<+`Rf#w_8i;xi6COQv!$PPP@f=C95nLZUyhlrHu*887ZLcC~M);Vv;P?kh(&&$O=}Hnv5o|E?M!xhov9d$fTuwK5*GCO^n8+%)RAQM4DmFDT44>9(=A(y{L{#5v@u;GkEyttg%>v^@E*Of}lUB_PPXFf~85k1Zjz8nu-!7Pv5_U^Mx(wXmZ$uD+%+<utyrX0iOPbiIlwBeq^RKPyix>^So-Tf5^izbA1q;H$&1mOP9wCp4WvCb!Ns4YFS_aWCmW5VldWFuZ%U+%mPX=n=j?T{IK3i@r%T<Eb1mZLsDlzPtjok2lOZXEax_$1+tkGP?e`{WM3jTCy=TyPK&n<gvi0v9j7UOtvz)qAzA9&p3KGR<EI|EI!vP%sZ~OOtW{lh^&h~ATpb-zc)R+ez8ZqPcEG<BP{PY8n`Vh!!=U2YP7n(rYW^NbTk1e(lx=c%c<(I@w`x<12xyVJ2fpgOTZ>IAu)uoOg;U$@4yx{(JZ&POS-wbT{%}RN3++oA~syJ;JE#|5UUcUpExeDUbN{pVz3J+b33y@w7(y-R5P7B>aoTx*{%gV7BI#(roXDV8neB(#<zz&`lZh<@U%9ptF{Ft0=KxhfIN1zccv9G4KD?(`z@WdxiHJE-8aH7^{ogo2Ci$Q+^&SQ+A)x^PbxUDE3`GZ)HIVdvM+Evd^sU6D!A=AxVL>Rm^1;dQ#b*+Y%h#3#x?{q@h})M)TMP7F{ZPzU9odDZnMxXo;-9cySJ;dU^rYXSh|0)hQa%|WxBzxmc84rM8iKi4~XWp^EDnajkq){a<$Z`D7@P)UngOzKQ#|LAg(|pAgK$k!7s5r#X3?j)I9Djh^=|Bj5KDt
Av2z`-aH>C=q)iYO|HbUAUVD(pR}+yJ+joK>osdMp12^nrmHKf%QLyGQ!Rflyrtf-QK>637BHHj+AcMtEvY~*O|yZs<T7lu$UZ`_SFvL;BPCzBS2sm0)UEb7S}KyVa<YFe@3dF36fI+{rZWz)^0RO=iLFpM4Y%qjgs4@niKPNIYrw~-hp=h1i8M4YPPpT)u(nS%<1=NinkL(%D!HXO;xDBydPR;a@ivz*_`&ZrG@!saamENaq`f}91v3q}W~`(#xwO|q&bG`j4=Ucch_Zkz#Vxul3^qF=kTJ_F2_(?BWw7`*$1qkjpR?#RZm=Vz60el8t~Ycg8nXAOJFqq^Y%7JeoHiu0=CnMrxiCsJNhNujMJrdcAt@5JK{&Uva>jf-y}e|xt2in?^F4H|6s-9=IWQJBXty4&UOCgTg0TR!l)8E~O*R9tfGuY@@wcY2fwW1z3M?c!K0mc0q&&K}oi-!2QMtyjST`dzH7=z#Vjr+4^Q`K;y0vCG8aVbW9=Nfy6u46$>^9>#`8J6!$Ux#S6EI7sbR>4L_qS=cHM%Dx8oJrL`nydyWxwva=rkL+JvQmBDIE2<9<?mGZa26wN~*B6UM_T^OFY~*_O!LNN+V&hTd;mFwXirSJ1hAsi@1(7MloEs<|HVn#I}+-Z#dL4i?;<f&b4{KoT{QWtgke=KD-h+P&7O~o;d2TtGiP(kv(ZP`8EEzc{l31^|CZMIWROkV6c%hEhfV{(KA3Qle+pPkg&WdR<$;@%{SPt12kqcgEnxgR<b~`J~G9+05}%8(mu|sb~U>=CA)VpjXasOhB5&sRl6oVF}L=!k)d6!eX^!Kdc6xR;wcF;C$o37L#~6h0kU{C0lL{DlAIMafiLVfATK~KJ*){P!82>Hy18St=saYwwXHBIsjrc&!>A@SzBCZ3<TH)2A2SZHv7vpgHmhj0^)k$;X*!`Lslv%TYo&fK8MccvvOA7CfGm5&bwD@2lQfUE62uBL1+dyFlem91tu89LsIX`*GBCX|
&oT=wM7XppExHG=i^5<wwJuM&4Xb<^tu)Rn2sHw)MKTt!YdLE)`L#+fKRS{uB{^odLAfxgDL7a(rZ6O;C%z&=RI~#%)HR=`SG21}%s*7I55s~nYA_u&FEkgkD7A>ZY`}xHyRVoqbUBhhcs`Y<x3_Dh@GJQ&HZB>Z<t!&Sb}H*KD=-VFzbhNED70TUDY3UDR4tCItuFnr@38kdSvZ`uaJ(fr3b~NEOs9b;W;H}OC_V4At22Bxy}7nDH#KrCNI%cCx2{XHx};P!V=0ldo-lJW4Lpywv>3&^wl1u=G&`IiA3Kw?3$=8-k~#gZroB<Twz%4s-ZzW0$Gt)|I=4xz)I6^<2r=-g<1k7m4?Jl$|FAr*Q!>3kT@op(6g)j7cr@%lB{wLnbG**HC9S)y4yrS?XuF>#JG?lq+9<#$4Kks$7pnU!CLHgoFD;2DKc=KE7A2FocQEC!qO{JUGAQb+Td=z_#3SP`q%LSO7oZEM@TwE76EQQi7{=u`2)t{xFtAu2YBNMQLo-D()~!VxuQ9+UiZ?SaY`3Q|ge6?4*t8Zg7qzK9Z!M=i1~s&?B&@Wum9MfV^fmr4g|~bqF|^4otFrVg9+}`WM;flNX}O{-U$-<jXEKhl&$H$!H?y!U+^nFeY&1+PhcN*t#yc)B=dG%)VI+brW3u}~^)jC*oVYTw2{OW_g)QT*gEF!<Ww8Y}Mk|w~0j*0jlCI;in=sO_4XUvqK`3psOSSE?6s^y)*eOIT!mQmbt|{FxG_%*R;xTP6ur4F0Cb7q{&Mj~+4LPVYPRPAH^S(g6BRLSj{<Zrwt~Qb|qc8(BQ#A`M);Fdpu{1-oeKAcmYqj$^a-%Rn0xi=mb*`E>i!kq@ytSw-OD;(c3o?E(KBdnnYqb=%qArs*?5BFIhqqm}TCNnNJFO?M-z?Fu=(L-z>?;{G4>H{>$sI1Z{Ij{H?lRx8pRlOCN-dWtBDgBDfV8o=VKn?T)2=5k-L=`XBD6cWtg(JIb3Pg_1+Buo_at_r_pWKR
Gq|rb7BIT0xVLJmSfw}_Dl?e1Zk*k&sxZYIe6Fl00xg&@Pp)n?k+Ep87fZ7^E4bUVASt^qmNpu$;xJ4xb270o3b-AusX5}Us4(59V7o*w7PB<DC!$NV&ogeb_mE*YgPT#LfW3~Fz_=DMtTjd{9WkJ+Co$?g_^>x2aV`TeytFJf(l<)BODGes95;?Qi#FD-f2Ni+!67^-ygog+FDkycrnFDE!nJL?)H`^s1u_XT`!lMnA2j4LTCA8mL$FOPwX)r|1~7ZLRW(K_)G&Xrd92i`i?RT-`KbmdI<E*Y*{rLsys$PegsWjK<{Y=CPP4rfKrs&|d!^L2W3{LuK|lyb!#_Jb)U+@!U@fyYVzckHsWd{jK)7W#kv$W#d_KsrjWN(Ok-6Efku*^`b~GZn9I}(NSSB>JN~+^BoVo%r{5yRxDYS+)-8C()o-`9S1gP3IZ86=mT&}A&jyKb+m8f+tfisvrtu_NG1-J<|rJ~TS6E--#7c!<X;V+UhUcR6)UAiAIV7x{xNiKRD`LyjV?=q1xjk<!Sp`hJ0LNgPuX*~%tl`+G$vLs(BlP>lv*{u*V%rl@Z7P8kXCM+;DeKVOe2ego}r?Kd&NE%uY>9Bt|39?x=S~X$4@<d#=*Fc{#6*@UK1i4c%sj@n`ge@Po5wftR-?$kb05n~%b0^=mx22}RAEb0G*eX;o=CL9&RWHV-k2U!xX*_nYV=}j_o3_9&jW|Iq39a!h=d>y`);LczARqUk_b{F@)V858x;rSYC$V@TjJ<O#$v@>XrnuF&mz&kAQm&FaSfAIdU9(ZQ1Gv#H*0xkQ4=?yCp0gq`4lR&1z%y1e=rWKe)v&s?>n0Q*?=*+9x3xm3+_Cwo!LWO<8KT3r5wvBo1GP@G?ye;;pQKKla;b5y@Us}R7&IF=J;f`*-ayN?(=!>t%DbMV)wMOfZNJYll{&92pQ@z0zpt*e5IQX`Shc%6K(o{|1vK}sJ2cNOsj(O<S2cgH>^;#mOSS|tcsrD?*|Z)y&n90s
!#e7)rKn>)M>O>|zAjOz5w|xhqBxbUN3=OMEjC=JOg&6JG_J<1X_c$AMZROUt~~8DsJkh$cCs%uTfIg&P_t;XR=5W%ZmDOwVmzC=GOJmx@32=lHL`Io+qKrR7dL7)ps)ihtTy?s+^lb?n6z@LGB!Og{4sj6m?N7XH8w)9kFs1h89H37u0w=4xE$H9mZojJsXHsKM6ee;j5(z>A~L!$SvrES<hVL9o3^bo0yq^mWvC-EtT<*P_^92f7_|4cb2g2v7cVO~$~D?Fw=mx^%r|K`Cbu565HxBq-!(-!&N@v%UNm4b9V}WZO0tG8X|p)DD>gqIH#<Z==DWHyFE}$dpFO#`#yH+IUAeitv%Ie>cEWiqxj7s*vphDkGBI|r<gm>!OuZ1Jg)m_@_pUCW9yjhV9y^mP`7&xLez1wF_9+~(pDsE$+P7;b?<6BHgsvtinl6&C5jSkKw>;ZCps^(@(LO}Ct17m?U^T+9sW-Di(lRtTIXGA?vp1bA_9zxHusKXLYBnvzy|b~jFErk+&99fVNw3qlVZ`byHoUklSGi6$7PTTVdA2mRk+_hue6(Sz=(>+QPdO4e9K18G^S5R@F1*38SGYz!&9ye9Tss^*=(A%u11w6Z3cS3(BrZKNPcMM9khW2@5U|v?`#8@uh`}s2M>S70C#=aty+Q3WJupVRv@t9v0I#?v<hZ9axwV?JaV--r{H$g!U$tu}m#~Vm%P3K`M6gLLYqeUlEiuft3aLu5_COdf(ySsZ1rZ~^XgO-MeYa*dgs?%XB&{|i4zgx4BCyk~06Jo{<T9f-!7W|AakES@(XJ}9vn5wF!N7Q>C_11olBZ%ZkGJhCnLlYY3bI5ubi7<KH9t2v!8L=gfw=6dy0LIF6FA$hAue<?1vpKv(7Pu)BDpfHg)aK7(Y8H8<T?H<?KB&$^R@{%4Lwq=L9XAdR5`P>;iNOCpfsbl;Hla!Lm_%G*scMt-7@z!96ffn8964gcrq@cGbn&HWV_rsUbv$;{xeu2D6kK(
$*(FrSfT{5AiAeH(7(hlJiN^~$hMKZ^|>*!csD;hjXdHs&pZ^L-8|_#mo9ZJsW-kPS2^FgmnuTPGdyFrp{ZCpFu4gX)5Wf_p|uq<1+}0sw6`)UYP_7WdM_Eg*0k`mfhc}FVkbpCZ#Rds>bRY)EIL!NtGcSUU%W83vOOF)H9N~X8jzqdZ#KC-TEF42p{-Oo#56Owi#HoQgfW4%ZoNUY0=P=HJG!X0FFDn*<1phn5UAs<-7H}%*erZE>8Lv%9W{tA#;p=8m^Gm`_B6dTxH20w*(kl8l{v(+<E%C;uDGPKm$hFhZM9dn3bZ1(zPIKu*|={h#k`5U`?x|W2S0&1ys!AI=&n&WqplIU46N+2Jh=$CC$Nt&^sqxA(lq#@iM#+cB)J;27&zFlOs*8a`LT96E+v#MbUk^(IJK#=RlS!a61#9Vkt@uojjum4w>u`S3ORK=crsElf;Ps!`aASJNITvwowP4EL${eWxU}6j*SgHUO1NmXWU*f_voTsUUpRd+TC^s+!?5BmzcptxVYF+xj;$xNYPK*g7B2KLKd<DXDKeHbm$4lyDl7sj4Kr^z8MDB&Ij}Llh(sr^5i1<G&or{P1~U6C1hp49udjHjDK~Pm@Hw$A3o+p?wX};g-nA^inXaNaaxllTZ#9Z4)+#rzMm@<ey*y?;U%1#b2|2qXC$aP}z`571@Hj~@jw<Ro0K0=Y4Y!Re+qkSdy|+;?J+J&VBe<`!;Jt3WTsmU33MKZka=rGqOD3r`9y%yB-nRTS6)Gh`1iL@Fk31u-Z#57+Zn*urt2i;VvotTiA2fC-jx1*?H8zSk53d|Fxhp#%wlFZYO)>{HNHp&@1+xAoLocAJnyy%~p|l6E<TCU;5Vsbr{yG=5WG8<>g{)R8fH$l*CZ{j5fVI>nnXscbt*~@6z_)ZZ?l!10$|bqA95_!f*`k9dS~vl*7Pqr7W;pq@T(LzjoT~pXB(T7(VV+VmR<YhMFPLvT2E2!~(yW~XBCUn18o7)lS2SKZ$G^t0
Ah4&ucs2wu)}(nJC%9U*PQZt$%s9X^0VH&?A1}5nCbfLEBF5D?^e{`XmAJh^K0K)`#xJ2fUa{+`4>Za+MX*z}@|v?QLouVSAfEIo7ciN%f3i8P2r@#V8m_3G^{$(ohOTTghcWLqbv#)%jkLn8ATfcpEi;=aWi4VlSF<y<@igW&V>T4CY%obQYc@}+fw#^zw<#zt=rDP)Mli28s414Q$3WgNJ~acV@v{)D@Va9(2sw(Y&9ADk3$2$iT{;*nFgOc4T)W|`y|n$VI5)DYgDGmV%(5V*aklI`Vy?us4-bnnXFDA|&9A05s=U0f$T)~7Z#FzBeKkBRlr~K*)+ar*sWv5~@TUc`y*XC8f-`h66R|<K#5WzV*q=}-O|ASf=plG4vbb_Kk+0<_88vY;{UNp}Z!z1rS2R7b{jP~NIYdP}+_^P7<}tXuD78c?9k7-)HL>F~<}S=I9JwQ|a4YSt$v8GHM>$bFqbxi%E;br3kEPMJkR}i?uRm}bkF<F*zOC}8gfua*Ejh<Nf3z|&ZaPA-HmVmq1~mt~Yrx&HyE7oTc)YQ&yEz&-=P^CA&MKZkk38}<ezhE~J~=c&dprxYOvGum)u@oF#jE$RfIQ=(W5eCGj<XrJIwt5i6hV_W&9Nc5w5!`G*|4fHpt^K7yE4}?Vluw0LM!1loVP<V)isK=7cgEfVWk8rue1>~CbTFtoHu_sXSnXLbSEq|vM%7InzYa`WFgeOvN>2bb}#-cfUK;zgs?U-&9VzLNVPP$87iGP9v`W&UnjaRtuLf0U_Ava**W2@3ALa$o;TL64!EK+12$GQoIT~OG`0%6iZ~3pd$^Y>n=j6|lR3vSBQ<g||28%?Bf<4R5t<sUB`#($CZxGKkF*RduPFkyaj6@wva!IhysbH|0yaOR;;P~;3beL2bg~|<Ju|v6>Zzj{gfU#MGm*zMjjBVjP%x{i#wo=#tgqy>%P`_D->I7~WGRg#>oG_p)Uhr#zcAjh=t;geCAgWniM0Z}{JBx6@vIFx
DYd+$D7iu?9XP<ZPqikiG&T|^{IGz%Vy6H-9IoUkU$XEwbgj9oaJ|bVku`~+ekl#Hn>vXgle4rr)hTl^1vu0w_OZ$*1UTh3f-s?~5i_eOd@E$91G9xIZaN{X$*^ZV05*BKc(BehzqQdn`aFuYemFs{4YeRS8nAA_d%2slaKP)Zq_k_g=&R>Dt+Jvsq&8)<6)D5BBrsSm_cTp3HZ>%$ny^wZR4vu6NjN~U`Yzk8jjDRBCM}sHxhtx)<TAfBwK0U8#lV;~Of(lTyf4YT&bYd*m9(iYW2j}WurzlyX1&uby{SekO}M2rn>eOA_A_3&BsBrC9k6`4#-;)_Mzy&xSGYZ_xgzQ^;JL&vOEfGzo4PtEQ@Q^tFgg1%RlwmtEV&mnY&JZ-;j~LED>S+`2e{&~JTaNQrngqBcc+vvKQdxMK)hYD)-_=-$T|C@O{|zYJgsA{ggo!1$1kX=z_n7iYBmS4NhqT@3##g=a<Sm96{^3ikSZRwaWT~@sU{Dm3N_j&A}+xuw<K{es4%sx_Av~!VJwoYkFgE5J3cqEr?l)g<R_N4Osq4v1Sst^;3yffZ8-NOa4d$Y>abHKSSbZEL8$z_p|c6G*ELSBAFxR<5iC?F2&XNmD!jTkS)$#y+%;~jGB4}Bv@|HOsxr?hX}5H|R5pD#<HK?^d@>2Ny2V>DXDl=+x<G!cW<VOcLp2aJ0xt6^Ejh0~<~j1N7qrkXPplF(aJ2$E!ZO*iB*F^4k_ZjE1Tbd2X+BsioiC%Jw=$Zqr!+ya-?*1KIy5mTr80l52rZN?!ZdO;Uo+UQa<vPznJr@qk}t$IJFV=p^f?qg<V41}?YE>mk1qW$n-btPdNmI{);cF7jlCeOD>QK}u_rVvgg7^_)Hh?j;H*8o%P|)$ATo@!fHH0<s>5X&ay?A9#kyE8IWeU(cDnX71*wcO*fzi%sJ1pa+AG(k$-G@ND7LXNAG&ofa;)sM&@%X`y}c{I54HO@_a|g7E;}?gs<j%kLN#zGQYA?<6gvd6
Ha4jvHA1qoa<tkozA-mExw)(>SSj~5uP#llHnX@fyR5gb*f4vrj4)0u5gcBz=CklIRH!}|#j&w4XfKN_lQG7${wlt-T`T9e;64vK7r4+j#~zF{xj9CxBCz1J{x)JKM71%VEges}@HJ7a^|dG|N;K!ZL@qTmq_jsMt1Eo5e6XG}Y`k?WOTDwWJ}yEc7`TNpB)bZ+1vLt*95&!Ex45jS_a)6Zhq-?>#xRYnPrj`-<*+j-{I*~*$2KrQtTwPJZ7YB;hqV+v#yZQf(>OOfkS(;X1~j+3U$IoJd%)1KqA(6Iq&BO(&#B-dMXTg1O0dH%46r{nEI1S|g0B0TO|FwH6)@c%s;?TZl`z*aizK9|TrCPFF)-GwDz*HtqA?LPmA~{h&ou%zjxF#uA2$CkskBrrf-4xS|E(S|II=gnk*<>~l(?!Wg17s)MK9|*Fg5BdhpQJh7AeUjS0Eas@4v{U?J@v0JiPobNIP>ltT?c;tF+{>1U<(%Of@dEJHvFc`n3tRy1?J9ax-~9+&<ejub&n{xV9O(^Cx*Msy3pq+cm8*X10v1m9mRCP&b)5%eqxKMZ`KcCNb%@s-y@x^|KSTXt8@c{j|g}(K5|2GB)Y8Ubx;j?Xf|zGaY#}1v&1t<TMp9F|tW40<7aJdoVq=m!M;|?6^TRpgj4v*t^g!4k$Fhu)KycgDY1!YcU4Ac`qce%evMyajY-2+BuanIj;!6(JGKS(zy4iXtIwt12B!XrNfvh3_!RoJF8DP4KYZnnz(Ym8znb1c{L{|;<zllKr}JB3A=qSO1P^!mO9v;Ft4nn_Pi4=r@Ak`gf&9AK&%Qh3qbm>JiMAP8?BZ$V>DGU!Mg%Ad#c&B5<UQ}r6~b4sjLSuQ?z-o@hj0eZz)Bls53?=1+as(Nwo1DKQs=rLZ|I3+&2xgYqFxUAvE2zYoc4MHneW9Ta<mX!L@9&$ii4PWw3I!DXcU#Y`7XGcr8XfLb~U#6T3&D1-&4yA+IYWb~YibilRle<1J{p#WPPb
e<|fOWHeznz_GC~<FI(MR=FiC8oAsn`7SpyY&0IN1~$K~=e23KGqIL7mOUJ_z&bv-Q#}B*L#~}StUtIh0lZnUmM;h~T&#7p7&*wkFFYHqr8LznZm@f+PPYs=c{c4WTs<@})-~rcSGqMkI<_M=888^N#4d!dDY=F-I<G{nh%&0FcQJObYNX$`lr^xl2D&-0SGU2aL9Yz1NG>xti?tZGoH=m2HbH*2H$RK8^Dvw^in?5|1-k<{3O>fXh`Zb*5WV{?nWe0;J-DnoLN>?5o2UgW=`KsHJGIs!F)oiX$3KB8bhd1{qB489r96?L_Pq5pL#swT2|M|;x4FPN*02e=H?_qfXf1~@LNqfnpfy^y#=PY(rnrMP5w>(VB(Q0%oHt*;Tt4`=W;$>)NIY1zeKT&n{UV#L7ec+DoUdH8o4Ve>K)`3T4Ygu1Qmu_0)-g6U05Bu7f3)|Y=CjnU&8!r+PB?WeL>{EIL<)thXE9DL4mO3h(ker&*|?xOUc=Nh%q=c5L$3?9jx%sDPpA*YZ9BlSMKAHOx;)}H9wfW8g1O8mVlezYg|Q|*Ft%i&UaM@VrY|qEMmY_w9J!=7NG>M0U9~JU;kY}nFSiOYf2G|trMq^xa<Cq~0l|_i!L(sD=s6G}5w(vv=DWKsL%&44k1Pr%2bKgXJGex&vAN)~C^?U`f+HU|0-*}8@iI+048dAA>#du*F)#Wnx-zhz2`#ub0;fYQO)$o_%qrL{Br!o6hcjFyXQ!yOT(UelT`CSOY%97bjI9zX%CPmcKC&4x(x`DWI>H(}Y$iiINwS(c^E>gfGBi-XIy7#!H!!5AaI1Vf6)O=ge6`TDm#!r}=ONd+vo%pN&8!4D<gR6@+pV@SqqZfs$Sn~$zOZ>NPr3!TDXeI)GBr*!eYyU$%QUh(?>s{=ExCO)nK~3JV>Mwp7&f>qNI3ztIKUr5Gr+{XowgJ}LP7yTO~sl-F1|oN%)s%%d^u*aNVzjYz(gBDWx?}6YPsn=Wx-rCcR8Rvy+9^G
pSjdQ8?c5!qBVOwfID8rB)N6K&OtrJHn29r0I@Q@6~imQK`a5iKEt5Hbwr1{CB}0;{xe!Q5Hm%!9!1MP3pO1$Xs}PL123DizpRKX8MTzM0=iN!#;+SS^(~08l`m8{{WF9t*tCT&5jT{sn=^+q;y2Ya38Kw3bg!ebbTw)rG%XFYTcFIe<S}_R2&?g|@GnL)7qs!NDzX|Yy)IKTO{)*JQnc~3m!11E<EUFOp0vLyVYGRuaIb+Yq^{<*6*Z|e)GU6eh%}8e;kBf$e6*Q1(KP6?!7LUroFl`qqpt2a!#E!((;)h@8nx^&7&Fo>5HLuwHzU9`SS-^ql{MV8t}dgoW-a%rb*{*_BPt51T`rBa)i!x8_oz`Y-?lTY&@5IanJ*)&Ej3y*!!3EXF)7NhYbOpfc&$~hR-l)$*QqY8JvHPXf++;B5Vk`&|F3#AmnkT&)TviA!Z7o`*)l^l2CxV&n=#0j5VUYGcP<&Pt|$Yue5^67C!kR)MzF%P(=E`bPpsQ84y<aZq%qy3f3mu(<1LLVpf1idZz_H`C9oGYq%Z?CVk)R82r<PpIV-Cnk*))+*|Hp_KQu(5={M^z$Tj1#J2YD>_$_!g-nA<*XSSQUwz7J*@GuE9Ln(Q-qONqZO}fmjKRAe}<Ed(|WV8pbBBg~e0x@c|@U<5#Ijw^)CN*L+Q#AZ8$TL$lPZ8#=q%#Gy2B~*7E2$K?{jm=*Lp6>x05D;%Z7xYN1T;Rdd9VpIP^}a)O*9F!Q8rnw(WyVHPpO_Nn5;9Y0I>_QB&dlyESXmu0y49;JFMy{H7nsMdMG$8nkdM$BeQfUuQB7aB{7V)$F?*l1S*a*Q#D5<*f+{6rLmDP6Eb$IpQhw53#|&St+8k@87#yn9yQ6XHZHy_S+bg~FDTzFYpwY-zB970;5DQrVyx)1=`<LqT(2B7lQkc&Qnl+bmZ?y$q$w9RKCibmDm3~pu`Sm$Nig5E@GHWvDXh4+Ng?zyRkdrbd98ISEv?A4c{Oe<H8&lw<gP}o
N;Z?N4K$4<XS4~e;IrATk*}RB*e)HhpRz_XR<c$yH8JKfxUk``>o9hvfwye5A2RQy|FJDIvZ`4uo-{TvWUpZ~W-`(@F|%zp8nat9G%_tP&ML_<MycVa%dEkt@-{!Ud@$dxda4by7byC$o2_%PVl--`JTYdrbu`+r`XdM~t1y@{8Lcue9JGijA|PR=UbeX~lCwTBGqj8)!mwdBZn3_oNg5Zg<2MeVyD|~DezEqkNvXClvo!&-R;~~&!711&7BYacTd)bM7P2HPg|iVZN;jJ+KQ9m<m^JCJ6*BoOwJtU@yt3J&9WvB0Y_fAP&n%~?>n|HM>M*h}Ua(d-Oeo^3*Q7!yg*7=R_^J7@k}_c|Kc9Z9C^V0>lQT`Pr8T~<0Wg`Zp)Oo6G_apEjWCR_I;gd$fGh$yJh0QU1S(#%GpH*kBQQoOPb?oW>aap95-@x<tS=O|LotIfG&Bn+EHYj;<1Z8?^eiec+B4>|VKPH7*{uvQxVmjMFEq9_PpHYXnl-JpLaiI2fT}F361Lv4(lV|yn62lq)~zS21hL+-nKqUvelX`D=_!J`D=5!1I3R{Gm@+87iK`B@SSS@NLacp_`ZGqbe6Bk!{xeE2dZZ4i`mT+%@2aFERWdy_n<<2|ZzRL6KQ%?9a5H}|w=u9TqcL(TyD1#5$0-Xi4lGr&crt^ryeb^6_p;HQfi#LTM=>TY(J%(HA2z_RRV`UHzP5TXN3oQuy|IrgdNBkv;xKHf6*N37QZSn{=dnz$S*~+5ZnWq#->E(>QlI`YOs{JovoypU_c5F)@Tz<;+O8a{H8Lf+1Slgh@G|N$3b@p(R4@IkBp+s~rZly$yeK;?t~8;qfHEwvW3<2}Ei^MOVXUAl%rDw5%P4QAZ#mSh94p^5POZME5Uu^Q)2>~;Gc=kskh`?7kg;*CwY(N26f)AUkElZ<Y_tBYWiymAPa@T`<FTZ&P_<Ap2{h}njx(UGv@qo?SujAQJtV0#$tgQGo35p<gRWLA6fmMP8z*?A3ofHD
ZkRx@Dze}*0~`=E#x<C+fTPAEvbCBtC$5Gjma{Ui!yzuXt}J7#us2?{NhJfMg0P~p>#U@)O|!ME7a<oR7AIS@X`p<vKr^?nv#;f_;4HPX2{RNWfH<bB;IfM>_%g?}X0?<no;RsA-7p+5xH0G?mal}Wj40hK`L2n$CN!EeDy6?NtFZ+$<Svz}8zAbiFf%POFtTegk}*akGakdTnk{X-E-iU62_u=bx;0lcoV2VpCoLkfWwNBTn=j-t{Vi@V7&N`M$Fja1aI=mvMKu*Pd^JEYMllJoU?kii4Y3a>>axK%FD!hng)yJ7zN!v4<FQOQCN2Y{)}$LQr86fnUa|?YX|wJna4?`MZ6m(4y{c}ykhZ%pRIXdSz$4eSA2FOX(=dx9eYgV$AU88HOfw#^DXy+9<glSDnjmp3<SVSRrY{L4$Fp#_a~iX;k1=?%MX#o`{17KLS}xibWVg_=x~;=CU@#P{tgJCD+%(m#La3Cb!Y!t-n6aQV&9=HRm@GK7_c6w?XtOXb+%8t7oGr4YrZnrfa<JvL!?ZB2nKP&{r>s1-=Q3a@-6vQsd@{B%ytQMuPN;q}im0$BV>Pz1h_eZ_q^*az7&am{H#L8)UNtGSW;6}6GBj<nYAW%!>n=Sr!Kkk?2PS4O4yanP+_AW)Wj5_8=`gV|h_yx~b+2_Yg)OA8-kx=qh%Ue|#wmp-TQR0KsjXuj*Q|Us7B!kM)2a+B2eSk-BsG^blQhgU>Ljr<xUIo4+%+~Z5H{VX(=>stQ8DN<`z6DybFRLuois(PC@>zUr84xWJ}Ky}GctEDmnp5SJvVwaW3w|c_%&fNizfV~siY{aRIN>@5wtY2<s)@7Cbx<)Xs&U$jI@+7MK779nKE!Rh__6(T&jYsr>ujqUmVk|tF<7u)if-rfw5mRp)nINU@cCu=PHh`<t|mU^D=oZV=@7<&a%|7(<xarK{X+)Gb`sVlr$DCh%`(x|2J;7b1ZtUnWV$6P_VrxAE;R?MKpgeO0`w9ajCR7a<bkl
lCeFtAt|1@Ffbf5e7`xh+_HWxRKbm@R4MHy3@j=%da}zZ%rJ8=>a5_f$f+H>!meblHm}jDdND_@(Jc)#KC#Cxm#@7t<S#`yxiBKJVzDkS+oTjHJ~OJ2D=Iy$l_dHx8l|)>39=EX3bZnyTrEm4Gp-u4fhVjn4ytpYS+N&8Uo{Cf<SlKe)3DgIz$k^V#kDsvV>HRIYPXlHN;MS?_OofZtF(MBSg`D&^0NE0c`AA|`72MMO)3v5>9p-J6SS!)^s~k@gt6i{Iw>JGfv8!rd?*B|L?G9!qO^Y;T{U+!y|h!QMl8Rn4knhe7P&MnP_Lu98?Z()S1muaGPEc&8Z{6#<TJ3WG_%{Xa5O`%jy3bFO0V{?h_0nE1}|i+UNhIUNH&zJhqFDn&9d^aBQ&G5c`qTb@H0Oom9?&^0WEK>U#?`h-m_t@{v>0e4>M*mO)g%nW+dYo*RLHh*EKJ+WG;>_TB};Oi!s79(X}(RN+#<p{<IA>Ju~0B<+U&>ESVuS#xZp@Ycu;V5Tzou9GFrmb2j0sv@P4H^)ElIbhSdU4J9l!?XZq9lCN1U7o-@hMK@P6;;;ZWASSUjsGpHEn=-I9fUnIjhptaDxvy3*t1{!ROf}0dOf<GGKQ&yhl`PsgVz}isg|rWz7@bogZZG0Fj4cl}-86=@Iki15LNrXYIkoSyX1LHbC^Ahk&oXtZ5ipD^k2#SmYOAiDq^}_?Or^%L&?_6V4=%W`Dl5aYakYP}<v3q6FE9Tv0yy8a&oEjs%Cy}nak&h!Lp05?b1wohqAS?2z$*&4!Y>pvCA2@J{4j<c{kHuwKN#n@h^%nDc_dG#w=c{y<g}43Wvp{86*NDw{VPB%g|#WQ?kym(5wOcIQZL&vgEY!2F*3I-rZ5k-`Ys<delSKHueMl{-muUy&9K}kIWPb!E-wtU9x-Mpv9d2Mq^?>iYc!*tz&3rbb}?l%fvY+*z%rmDSu!uQBBod{eyYN;y)v#h|2Cj7bTQO1Ft8G`kt~X{NiSTpqPXoS
q%?poMXj%}G&6Cu$u#XUnJCY)Uoh~j;;E~*@~j9ba4Zb02)Kr?OfrA67B<+nKr|#S4Xj|axHO@pttf{pZL|+C|EV`Ke6`6nZLTmWXR+$2Ofx#M^|bl5^S4zj+O)Vl#yBOmbur(p*Dp;ojyj6C`7+Qk*ei>(WTuI*?K#t~y(a3Y*)c*fFs~Q4`JQa3R<qGCUm^q}J~3Ui8L)^Ps<7}f*R86oFs`6GRxkG_-l^IweYMdsTA}^3!ZHLkT`Ekl1~RoWx+n0pLNpdHP%{p!$(?hkuAoaWz?wm&TPy0m7$@qiW-~l6kD&i6ku#biBCa+x=Q6J`b~IS9q%*Uv9<gXK-7oPkIy7oEST4scA2608VJFTm|D-*x{Hi{U_ad6KEihd)tDg?ApfMJ(1Tsgg6SRu7<242}*)K0Oe=ry>_At%1aV?lN*s;hqN;Vg``mw69rKnIXK(cqTu&s`;+BAGHoEPe{Yp{;C`?Lu$lQNz)?<Alq88aQE?JmD9_B6aG?JXa*;Vvez@S-QK!7)THn=`?)nz!XHK{JFeSTW_Xz%!w_?<NknI5h(_-Lsb+=p+HEKdV?abFDF{oVIT$3osWnawUl*7pn^}I;Rt`QncDSjW!dxJsk@(cC=rwYBp%FM5<>q1gdtiimvFfS2!ZHVK?kG=_xs}S+7~N$h4`ibS<PBoh5RzFEI76DloMvSgd+6m8e%RVxGA;)UH=D8nHsG1uWt+y(z{o1hj!PUa;IUzAitpXs?enGmogT+%bSPsT!d(111bH%q$r#;jRs_z$)@3{3&rV`7w_)&oaa=?JoH+^t4y7`Y7493N3Lo=Q8B1vZ#|agr0t`AT~xa$S<QPs~|ry3pJ1{6CxR=ptg{xcd`Mn-L7>qytK?JeYFNPZm_DT@T7yVd9oNMjIoF^fi4&_jV*<<jjhx&B`uP+i>ooT6{svTvoKMz(K7)s?<P4o>@sAnKC!wqBR561o+$0LxGEF0;51dLhBR<6thOb!l`J_j*|Qri11oMcKq}rU
dok-LGpFh>ShOM`<}!UWr>r2IO)0Lg(=t)8?=0J=sV+h<q_9mg_AvOf5;GDh0knR$(lK(ZN-Nv6KQ6x~DKY;v?=zpjlCVXp1~p<ZZ8Xg+*EUC-k}MA?_^IKrXf-`D(5)abC$IP}u`Nb5cQZ{id$yvi1*Vs+^rE~ohc##^zOBSIE3=@qh_fa%9yl8_8LTd<t+HgNwyuD-+Bi8f05*6x?lNz(!YFqx88s0&AT%VcvNXCP4<%eMkfs4NQaHk=VKqvv*0hB=v9Uh6#WxI^0kl&tbu+WE&^TDRq&WyX890|X2RQ09IJbeg8n-As2f80P|GK5Qj<f-}LOg0XSGxN-E4?baF1WirrnuR=UpX-~Z!{}A9K#ecyRT|8i?!f3P&rS$x-|K>;<PF-AvP4SBr&TseKpiB1GtL07PdCAS-c7}7d2b205#;g`MBCR<+i3Wy|d4?5WCB<GB(7t)HPWu^fE~=ETpTXkRo<6XgQlId^Dga;IQc&Z7<9r7PW1mY&0Mv2C#W16s?{it)^(Kzc4zfIIx;6{WAJ0cBV(POEqz<UZGqzzcsNmJ2>OIWxL|1GpUoOS*ZX3006zG003hp008qL;-3I200000zm&J95u9N%004lRG$ehh2e+oF?W2$<f+zq0o1{b?7_Fb8rl<5DuPPcO0076J)2R}w3XpuAJD#H;Qj`Dycqq%HP^xL0N*p>dudGKU!XjF##xMW?*{Izp9xNTCB&Lfg`=>1{001AQ;2~-$pefs?%P85Qs~G?QH6+ZZU?PSrOsMcCdnYR>002@Y52kh`stb4>!6@Rc000K0CnV)0StbXw0066{swy8S006cnb}C^c002azCouCR0049$C9tokHl+Xn008!=002v=001~B8li_I00000ek8l0fu&S400817*C^^DV75c6@S@Bi5i9@zDxz|kB_I>2+Na2-;h}*j001SVz@}QDxTX}Mq9l5g#3%p&6)E^0_o#EJawAJ97N`y>ttON!!?XYZjH62`k0snB
VX2xVp{7ft0012&UM3fq6`^*ijw3><sh$7;i!A7;&Z~VVx~R1uC9J3%002WP1*&$Ws*89X)*u?H007|__NPs)gr}gf006D2fhD~y001|s8zsRk005k;lP_T^006g}K(g?uBdGuY002g)007ja005e#oTZc)00000cA&1Nu#}}T003g1L@17(I<^C=ucL{O$tM5+;HR6VSsg5%9H>XBR3V=r000!6!KH1TCm$lA*`i*l38nx5AEh&?SES-5{G{<L;w4BaA}DqrNVEU|VW`F+!6O?XO{tp}6d_|M001T^?W6%H?xNSIk|kT68lwOJ_omRPC8H0ZMX5Zc-=uaZ008r-`=tgXC#2;hTOW0+007~b_9soPt(97>0023tDWGL1004HXFr-Q+000Q3_b3=D005vh_Oal;ki14VlPMG}*fxW-Ml|lB1UCM$(lRzTAGDmYh_MX3e>rQm&o|gGq%;w+?x71hN~2G(<*xg_>aWDKu(u#8_Ad4y(<}EfldWGO;;{$7wz<ouGC1urX0qa%wk(7%U@7yd`?jnsMyXCF{4gb`l(LW@{;&Ed$+3ebUoPOQey&5LQn14<t}Lh}W~@6m^|bLa`nuSzB%llxou5!B#ijrN005sG006lv006nIEG=Cj000009ws%e)}ga60057olb|Q1Gqta(bfuG}P9p#SovA&h>8Lj-_o!Q%{-BX4008%tOq&a-5F;EM<fI*@J0}1DITqq4J*m*67$#sYZKm55*(yD(C@=s3ZK~8KAulK@qNMaIw5AWK004xm2qgzCawtxzN1D2;Yb5{x5F!m3yDSeX`>Js%M3(d<005K|D<+d20V}^PiKuO)006Nmtt%oi;iO5X004fY@}Vm)0071)_$~}2000D>>@kEZ004!gRv;fI1*rf4001Ny0028E001+dS}Y%=00000G$~7}^q?Or007aY&ZUwpCbRIW8LZH!!X^L!Zm8s^MSx+Ql&Q<BY^I$k006a`NFN&@jUTL)l%!^=KO_JE
(3+4a*CwQ$MI;w54JII{#U<seA1eR=`Jy~6kS&8O`l<{ncC0}m003dEo+-vEwIzh86DKLBGN}Lnq9DN_1}*fWb*Mio{H5!s006|Q8Yz>V0js|)v!ofU003twMy;<YOsi9*007{mSFTGi008JH+9<{+003U1Q!%V7003^L6_nN{5~KhC008|T005>h004!o=qCIe00000+as~7!K1h=006P2!WCjH=(3)w&MFKiN+bXPyrtWsN~Iknil}C(vZ4(s006TdO_h!$^_xB+HKXgMf*}9^D4?q<#VDpCq9b232r8$jq${$k-z)$C$*PqopE1KLW2)FIh^zCU002_0=_N8R4k>r5FC|~1k*NRxBc}ltf-vS4=BfuO8KWs0005_|b0)E=t*at2#3VDQ003vGMlG)?bf>}#0079I3#x@M000Cmmn?rL008|a05Qla008*5B((^)zBbLf8oeF2qN!alwY)d9_OijTfi%6j`Z&Zb<T&28ZaOxt;keSa!>M8}hq3Z5XfH`MG&saLXsaK#UNJqc`nCR~O{StL5iC%$p|a38!WLsQkTl&luDC}q+OYpG)3(hkf-Zb19;~9P%dM(25HH)Pqp!X)6)<%spscqn@vV}o)vsAKN4W{B1gL5>Pb@P$95M7ZY&mkI8Lrx?mZ|^%007~m0031H0011T%qicP00000DVW)$45&jd006lp4JE}S<)^%-D4=5|d@29{%B6^;`6yneo1xJbCa4Q3004rd79<KHe4ol9ovKi)j3xj8$C-$k5g?GID;3n3KBzz_4k*DVxVQiSXdRXz?xJC->ZIc+Nu?XG007geoFx9Jw5X`2#3Xv8U@HIso~6GarIe7QS)?8vy&&=;002UyK%xyKBPGzP6P+2U004U=e5sPDp__!1007b^=alUw0088tfFxon001GV9V@>n002~&ZKLI=4XFSC001qF004O^007dh{*%}s00000wi+*}gQ(pu004(2)h76u0HqwI!5uB2dMp3{Y8Q1W
>8U}Od>~pN2B|P8003g7aw9CN7#&t5C#J@!fg}I`gro-~FQ%fLdm)~rH>b`iQz{pyGq3;v;V5AxU8Gc?wk0<y>=>1x001*3pe6C4FQ{)NNg(hh{u%%PlqInmpr>RXcO`%nb^zQU005e-P@xT^Bc;%)TB2jE007G<`lxUy*dAf0006%zm7EtQ007jTN+&KW006P1sVd(q007vW79yXiYN-GK002>;007S-002IzT%l4Z00000cOljyP^3~W001K&<RTuHVXAGW4yi^K!7Ts)L=^2MY^!6b^BfhR0H&5J008M0k|<lP5vUs`Q>=+0btC`)exY$CimSC9rXYkKfuaK?Yb-XdjV}NIxGT~p%AeCEcOu#;PomH)0071)WF9FbF(ew8X($pU450u3EF^{@aG<j$B_Jj!5g;|C004fWq?~)Cd?}Kt|D>y~007IV`Y3QH)tz~(005mJiY$62003X8k1dBP005(?#4imi0054v+beP~2{~rCB|QYRA-4N59i8H<n=VJNbtY4@JsVuD-7v&6q$NWyu{Uv?DJ-$G#j%mK2Qs3j+9>+A$hBy#q^@-tNvu>X!m!*pTD0`7@-c%sJvQjH*}tqTBeWf_Ke^U0<tSz&__HT1BrWSGWUhFs<*0TdcbsJ^cr8FDwzG699WWdfzOAXRmY{Ycp)8*@CNa^tZamevH8l?wSg1Ox>6!ol004ZU000B6003<yKBZC^00000u%J4uFDo^%007Ua6RS2Uu%%-pysb7J%dY?cJefeM+9(VuVW1eSHY<~=003|ubg6?R7AbM4L?gQ_xTXLA2B;{fMWpX0Xrz8DKOvGND5DXq^B@2K@~OU|S1fU*sw$r%`z7!!006J4tRQuklO0|jeW-<~vm*ciHmPExM5b&ccAN*NJ*ix!0074nZK?m6Y^8FeA*OpJ005z=$dJFLpeV*D002CwEG_D(008i!^ee5E008}=lp`n`001bYpCB!zxt;(30082n008N&0028D+YJ7q00000
1S4{*4J?wg007&o8LgWit}FT>ldd5r-LC)uXQ2kH>LDX252V1RAuR@}00816?x<v`nk9XtD;UTub)x_P?VbaupsGS0;iDWW(;m_z=ch%kHnso&SEup_h9Zuq1SsvKaifEv001zl9;7ayS1C$|UZQFvtfT+{JR`29g`(mn7$r{}9HK5E007-4-=+T<Y$bA|n;3ef005k$oG2Zs5+yk+008f)M=kWE007IV3@*Er005?_Fc=t`007)4*(tuJswe;e000Ug004@l002E3TbS;w00000L!p<fvKmaV003R62C59GaG_BspsOz~f3N@mXrvdY<R$Mb@uRS&hbyJ2008r%tEr?JmM1!<RH2+LE2#hgjH&*q$`p$z{ikCpek|}QeVRkB9<cxbzai8e^(bVS$tt-WwI#kE000^y;T_;5EuKOo8LIuJ79#)vBPJTA>m+U}%OQ#(87289002uWwxyvc$PmA#As#NC005jLoTweCL!E60000@N=PK%`00508F({p-006?4j~EOf006J9G&Q$5Ccs-g{ylOt6}v<&>?$=itE^+H?ye{)V>Pp_6t<1J1hfy8KQs9%lB_hf%CKiMHncV+x-?k2cf97UF)^|*Ixmzc!K~FURXE)$4JbLd3A^wun>bA^lQStRCNnuRXr$Gozc@Q7w=LAJJgaW5v91i5*|bWdg}4>08?*_l#JAuh+AH-m*09ebv$W-)J+ad`o3TYaDLmw%0vc~8m?!`M003qW000pq008Z$YbjVH00000v#Mt)EUuER002p<dZ#5M7$&=zvMP-tY^?wQJs=FE-=N<r*PqlVWvu|L005?=@1(+{3o2BlqNX^me4_vWsHMWBM6G@$dY`qh8?6wiD5(anUaSBB4yg|(Vz4)^ucXJGfU2^q000N6Nhe6IOsnaes3=J&(<uM|wyA<A^rDZeq$AlYN-R2^003pDU?^THU7irAznzdQ004t3C#U(VDyzOD003F5nWxpR005M+_OBhT000xJQn4(p
002^^!YLvq6D9xv000A{0063^0046-aUOCY00000F|DbheWp&V002Cx3#e@kwxJlAy(KlMajgIVtQU`@-K<R|AD~qzn5;jm007}7bRB!AgDRT^*{$rai=zMl86Lo;uc&n;sGNDMday315UU`sM6Lh;9;@G^Ca;vQ^PScf%%sGv005dGm7^i7ime5mZYeM;jhp}g<fUvTG_P@^J*d?qBP&y&005pP!6aU)T^<mq`=9Wr0029s<E&yP&aIjp004%q=&f6?002+21+g@*005Px1F`t5005P#?;q1893cPz004EQ0035;007#l6d46700000gRBZFD4-ju000xF>XmUV7o#2^Z7e;dGOPdqKq6?T2P{Pt@t58r=dIhU004X^kE9YTwk!^yy`W;SPNx6>=pt{W1}Q5j0T*$rJFMBM%&C#Eps)Y{N3D`5@~-f%t*I5B2pkfw004BO2pe~<Bdp1%|0cvKN2mY*9-t2@#;n_^b*R23a43Z&001fu`6Yv@Cnx!<N~oWs0028B<1At(m8_F20088yx1xfv001hormw-S001GRwXx-|0031gA2iW4<23iX3q7p4JG}ca#i5a`HniC+HKil4c`UZHL$w^RlCgv>OD%XPKb~GNT(X2Usxyo%1GS>32RC~&$1is@0;$KO*Rwje5;RmXnYK5tz%);-hqzEN1Snyu>N#C4wzCqThcq#<A*~XuQnKx&*|@%}Be6@ZdA8H4NVF}ioUwMQf2?4*D7P1%Ua|tY47*S?L^zu{^fo{!Y@1)E6D9xv003kR003Yq003#CFe!bd00000u&hNUPb3|v006a}rKO^!=Bovt8mb<sXsrMMYN?p3EUg8hBdJcTQX{OX006qBud0fzOD1ZmJ}7V~c&Y#Z_NFPUAh5|RO{oy39vqsa^{KL`DX9PemZ*p$nV5<kfhxJEajeX(006k2T_~g{XDxav-lPnw4krKr(5p5mC@9e>wJKB~+^gcE002m)uqk^OkfyXK<f0oU007-6
P^EShq@v>|005jBdM!_{001MY@FHxj002p<&9Gjn007M;453S<w<iDq006!r002QO0073PMJLOk00000VXhA-sv;<;007yUiKZ5&dZ-Q{l&$@n1g-!8)u-U7->FU~ET+Vy<t+=X002^^j;Xt-Su8)Qv?Hi63akJCd#10bLnH<*OsOQFW~_XoSE%c&cCY{d?5FLXI3+Zwrzml$i>VT+001;3jwA{lfFNKfWTZc;kE#Fw5~y$}?x3z5WhYV{FPWH=006_LOe=eskS4S!v?M4d004d^LL);bR-~OI000rFPA^Zg0020x>?rfC004KXZLor=005IFVJ6_CDkcB`004;{004U^004=k@FFaz00000CaMshC@zSr003m1{-P-!HmCNB1FTmiDy;wjt*BY1o~sfa<fiGWTq7>3003kj6{^XL-zdwadK?ZdOR4|>?50nu)}3c7HmFghz%5{*BCF%6zoh^GTBx-kHY9r;>?pvg5384{0076SLV+Kl;vf?&nW)mGpB(@Ix1cyAW+fag^eQYT0*?A3001wjH7ebxP$hO0Jeu?r004faLZd?`R-`c|006Y0M=vO`008-^2r2EV000T8Q?bXU003>by0Ut?2C(-#=)R4+rl@=^M!46uQKr|h`7lMXn=bGy2RHAtbG9n5B(eS_UaX+5{G)rEjWdWBWinwlxVay)ldXxdb}s%X?XCH=H8-&^+cHtG9lE77crjo$qrD?7?JkS3MXTc@inyJt53A-YjWi{$9vz`7(5s}bn5a4_E+mYoIxae<q^4XgF)(?c=&fcr9<rIXP&oOs)G-~N-J;^D=biun007FL002Lx008Z(N24ew00000U?Y5?Iw!p?005yMeVZJa1t*N9{FvCC;wk_D*QRG9KpcFcPopoVej~vs003&G_X@5g0HkuC$E}v6PM!b&|Dy{eJ}w0vouHH>`5hD}Eh}6k+q3`xGbqy|EgKmqJf7Mqs47;f007vjr6YzZsV7t<!zutOfu;ZeiX(NP
HY;Q-^(Z%<hAjX9008Zxo11keS*$Ck5g|~j003~RWT*@m*_h>p001o&;G9|*004d{7NIIH0090SkSKyI000H0PpAm10h|B;004HQ007yd006J1BdMq@00000r;Spe$Cx)T007}2$CqcNzo`hP*r-`42rK{qN2FRQ4<e1BV4)MMl9~o8004QTtR=)J_@;WBkE}5vzMudAMWW9oGAu|Vub`DBPcBm-PbOogo~8f*6DFA=9S;61-=2Lau`3Q6000c8_9gfxk}3cuc`EBAcP0P;@unao{UCHH<tPgw)FCz?002awco}u3Su883ydeRi005<_|0Kes(4Ix4006J5iYj5I000*yc_AVy004rV<t%6{0060_w;xriZKePK000lB004lH0043!AtBl;00000^q}q_fhu|~002N9J)h2`z?uxGW2M5Aax4G<45H5^j-*wc^`M)m<R5t}0031XJ}0oHLaFwUCanFS?jZmGPag{?d7UGgSrx^aS}J@Tuqo`MRHgs`W}U^KJ0sjFM4B!rsVeO$0088g1E0nySuKF0jVT)`JtF`BoF)yRSS24S=_0}-Nhrl7005t!<fCvZWGD<5k{55I005;a|D?jCqom2E004I=&L}u3000>&>nENp006R~gf00g000~^?=!G9(mKI18abuA(J?YA&AdD`^e!`@(k2c$J2cC%&a)UV#V=$rG&{YuTr*HG_O{5nIW(jy2{e{23^fTd)uyo|ysQMSb1(F{j4P}qqOoA9_NRuhi@sK>iZB<ao3!vJY$%Es^|I8X+>qlbo2ce2MJyhvEUa`c4;)3Q6s{(i<Q=M}aU@zMz9bne(W*_J)U}c@yf!1c*0muiMx<eqN+|#U006ls003+x000Ojrl2;X00000p`x*-D5hq!004m?T%`7zb~KnPjw?5tpr-%;MJNCv?FcEM&nL|wm#EjN003VkNuMbyej*~G0W3QlBd7oXB&T4V`y>}81EGta0V`al&ZtExHaGwPl`B7})+r02
PAu7{Xr5Uv0034dyrP#VP@fGd)24qO+n@jdEh#Y_nw$<DYAK1MSEZPr008eRfTDP$D5sFAc^gSA003quMy&ayUMR>Z005OFOQ9;K000P^hNKCi002xIPOBax004?5Ju4y~X(j*w006Ee006P7007J&7bR<_00000haJVF6RN1S001?qaiWYKgEd|$U?lsV%d7wZTOl5&x+svIP9$9-9UG&l004*_n50D~UYYBk{FEUgQKSF>SRh}hSDp_Y=b$|(nkx$^KB{P^X}ACYP%2^^>?h+O;wLYt1}VI(0074&_LQ}l|0X~re51fAfU5ui9Vkf^aU}O8<RY-5*Cpa0000go6s341C?}Aq4JLS*0028Dgs8Zy>mv6k006L_o}nD3001GYXCfIP005w<ajW#D006)!iXr41XDI*x001W@0076J001y1f2NP000000Y#1%9C!v+J000i0+oWqG(KI?LQY5P-Q>p*}`y)pr7b*0nOeHNN<f(n6001hQmK<IsAEXKm&?O_NFrxqf<RW*bWTY0Kwt-)&eJ!pfC#z{Bj4A*CaV)8%@u@*53@kXSQYOi)002lRTcY!&P$A7L52zQF`6B=T`zo>=Yo)~{11WByMxHwz002TOBA8~UMlAWHK`8^O0029ugebVH#~(y2002d$V5$eJ000T7UZ|I&004=p{Ha@)008hSA}|y<#TpzrYcuXUuDM_^r@GfJUoj4_7ql?6S+K)0N-}1#G_`{;iM^{cH?(XulCU>4&aG#oKsM_uG%dd^c)R^9yDaXpf3Z?0NH1tDOE$u#XtGZ)YrdDWcArr#jkC3`njEJpWiqp@3?nY47p#9V@u{sTHLX9dyC-EIg&cve5UU9!;IGfHe=bF?&@IL^i!^7kDJ)$&v$uL4Ev}sxv?c%m000ss004)o000p!3aeSH00000nx6}+YNXJ#001NyrLJPDhcs&}?ji*wbgBRV(;(O+_NEJ}RwXkfvY*nW0003Xb|#r66ru?q11VLcf|~#U
nx!})U=<*h1)~_H)EP{!U!?^vXf*%;n<(9_N2$Z53?sRtz@#TC008eGoTpJCAF5j?bRxJQpQr!;yrq^QTq&@m=_iOCo}yW#002Fgmm@x<XsWp;y&q((003^KDyf~PL?gwm008?Zx2y}M005RC`5T<5004m`Ijgdv006Bdu&+iS<tP9E000Ll002Iv004O`1E|g-00000<ry!c${lL7002WF1+W~cxHNMucPL*dQX&8VnjgU_1gHtFIVe!0J0E?Y002mso+Oc^r=}&4upvIAD2M<60jN_S)u5@IP@@PY5t5d!XQ-_%CpQ2904!^+St%r^)GNOw*rZu3007h^$fu?$52Q9KPAFO(Eh_*3>L*tvYb6z^0V^*h)R_PP005pM<|#fUXezlTR~iYc000!Ck}Kw<!J#y-002fQ2cQJ0007P<O(}k*0071!+^EMM000srFs(`;8zuk%003eo002#`005{h)TGrS00000*d&#t$ETLG006!o46tRTW;C5Fe;;Wd+^7HmOC5V9Dkz1kx+R;O7pMjs004#<KPJW`@u;yL4klNoxFG-lpP-7KN+t-XXPq~eG%A3s2dacF8Z-a^iYSn-ZzNnNw<sc{->0}H003+t7pSx-jwDqojUogkbteD-BBV+r7bPX7HYr0S?<U`)004TXaw2XeDk+_(wV@US000yslB?#VaiYMe004|CZK~*~003JkCMv<G008$Gi=(!o002@pCAhUQthAOfc(|gs%e%s?6F8Bu0V`~yxviKu3a+uRk15eOFRM>47cFwIW;L9o-!Tp@GpKT<<27qIVzm{xSvklyRWL@Y8?`SyOr*fK4!O0tW4Zq=?>b$zB{l9Q*eGeN<EWi1eJ_Kfxgyo2d9ExnBP;zW+@$8E_$h#@NU85J;V$?hCZYea_^@OxHLPMZ1FMQVkh+?vv^%G!6D0{BBB}rY001Va003pC001nXASSUa00000dZZYn0j0Pt006=m3MRBCqqHWVw5_t8PL}`xbD=dR
4x*1H8Kq*cC95u`008|Uil&K~hA5Mt)v6Du6PW-25Fu71&L5B+eIeDZbS~tti7Y>`?lb@Z4X@0UZmo|jrmmJI<eGyl008Tx1*uf3r7110Hyg35^dJBLaIQC`l&JhIU#({%)}Y`V006$D=cvl594(0|j-(hL001nkPbxkxy(G~p003j60VsGV0063}JsNMN000Q7pecqJ002d#9inFscdY;b004QY006bD005n$%NX4!00000pCquU39Mu=007`3tSO`=WVLRf;UQZn)TRIc`J`K;rzVysW~aEPV;ejm006<E(WP7<wV@xL>Zec@UX%a;5sOoz8K7Jto}2Bg4=5<EcqyZ(KdJx#bge$4>#Fl3XRc@`sI0yx00304&!SbX=_!}2W+$brO_~4zW2={>QLJPpWUMM84U0XT000cG#-_?C9Ic5e5vg}7003a9wWoS4V;xN&005VsVkTiN005V#GNgAX007CPQ>36N008|TpCU1xn5X~%006tD002>;001Z?CoG{R00000x16t>S*fWe006D0Ri7oGCAFNKmm#nx1{(kXWG4tJ;iTFjex&`RR;bUN006b3|EavEY9OkfDXOd}X`cW9KM~9%YoPlkK%Wn&KrBbGi7iO8);0hD!LNiHjwgvIeXOP^NUeG<001GI!l2liVk`=+P=U#(T_6Ae>?hWyexO_{2dv(i7poho002ZRLZ~b(PpUpH5Tk{m003YpwI_NkyOi#w002)X$15-^001Q!7N!8K003Vhuo-wC006T)5-RmN-nk$&RXV?^QM5`jVl#a)lda6I#Hg3Ki#0+pv91%J`ZI4e)F|mShBD}=4z_Nvlsb3|f30Y;RVM5*i@0et)2dG#!!V2@voq)}q_1(WQ8igMV7+uS#VVXG#k>ou2RNT7v9vX<%rM5LW-zg(sIbu{;wfgY?W)<VaiFX)a;dzauc+XqTB>CsL9=wWwzBFr7PMEntGTHwW*p-zQz!rc001p0003~9008PK$f@<H00000
y(q}6DlRRr005*U@FYQ}y)$qrtt`wcJ*WTxqZ)>sSt+Neb0XgqCZbTO008`=GbEQN?;wGoiJ?rQNu>Y)Tco&|MK1{<W22C(Y$#_d<E0=hnXUi;dM;oUo~V_gqbskd1}-e9006Tr&7wn&^(=5JiL3Z0@u>g+6CX<-xU3l`=qPZi!X$|y003vE)g*_k)}%kJpcf=4005DuI3!=LjimOZ003*EjG!5+001Vh7A8Nb003#Nai%$^006clUMtHhDj)y=005LG003?u000p%u%#}d000009wZ*DnJne4001A26C;_dqcTk>9xfy+n5h5&NF{NnyQD&^86>u#f1&fI005^PiyzCB86Pg8j-8C5wx|FA#-%!>2BjY-EvCUPU?|!o9;wA860iUO=_|Ra(I_({Dk(~<>@AX~003Dlxv3PW@F@2uk*oVDQyc&QEG#dbvY>(?86oqibQUxt006+O5T%DK)+9fznkWJz006qDNGxKk+?<f40006gSFHl10085ux~Ms;008%@W2g|R006|Hm!@hgrXc_T004d@000`O000Iq93c3t00000*eJlN&lJV2008$SGZ*zD$S}wz)+=o*tEK<|Qkd+c_$s)mk03fA3Y@2-000~yr6-*!KB3MbWT4TbZkPZ7^(DobZlzA5b&#p1S|np5t*7)Uj5GiMWGDlxMyVbt2Q2`oS1rh-007A?Yp8*zi79_9{;A0#bshi!94&63fu}($Cnx)=f}yG<0028FNGFjdIHX^$J}HhW006otNUUP4Rh$JV000aqFRm3U001$pBdm<7004EZi>f-J000>)Loi+`<~$(5@H_f9K|0~CX1D$<Fs&xAGcO1+FD<gFv9L=mKrxUtRJFh}+^VKAO06)sXf*4vYqu@7pE9{KF|%DT<0}TT4lJB7XfszVU9_Ao!7|9MyEf%2ce0GJ>Z_@-#kIYyDKc!8qO>BbR=15P2C}=Xhq6a3Tq~p~ysyfwv8hliPN}~&$2GsIe6V#lCpK%jT|5mu
s5$~9M=hqQ-=qKl002Xt004KD008@ggOJOp00000k*>KUMkdsy006Hbp`+R&_o#HA-L7V^=&=9*39Txvtg6{9L#;!oz9|>2007~o@2=z=dMsS7&8!TjucrV2uNgF|*(DVyKcy!i9Hrl-`KslY;voP4q98mSRVFPblqapKNUJxi006WbFei^Fi7dYyQK}Q9FeCr~;-B#$%_jFNld5~FF|7aq0082lE2PdO8!a>=v#4Pg002Ls?HPHRy{Jhi000B1U@Eho007>n4k#0_004j}Ta;a`005St)~JfBm!$vz001bY000Q9000%Ep^y@)00000^{qakmZ()A0071y8Km8*n62}j|Ev$Ne6Roj;;JOA1+5G#Sgc*3^r~dA007LT60o<d`7b!I6{4G}!KnZM{2(~2C9F^?6ssg7M66V&=Bsh0q9FhPgqwk!bR?mp2OO`g{j9gC006xvKp@2_$0!z`c&a?41*HH038-bA%P6WR*r|l66Q}?H008GBYogAi8?7`WDxOF#0077#vZ!|_8K_&E000uIgQS|S008-@eyORi002BGUaaz~003wqPbB23`lJ8=005Vw002#>000D}4<nnW00000fvg0nb|ucJ005_=N~M!2%&BLhC9q?!in0Iz0j=1sD5uFS^RAMtQLGHH0035~#jvof@-ULH7N*&z)~o;kQy++|yrqOKHLNZyyQW*C+pA!Y03!eZdKp=p@hNy5HzMJxxTp)G000@AogDrq2_OU`7OY>Kg%kh);+jSr)gpo`N2lAV%c%eW003UBlAk{z?U{L+Tc{W;0079LvM6^a3#kID006+C-zf&A000i5g(GvW000Uq=BJIT003w=?7I9Sb37}%MK;N|`MOWDpE|p!HZl`3^|a@*AuCESsVSSb$t<}wn=lhJVXjIqd8zKU|E`y@J0~<G<f@r8S2;s7AhJfKGZ-~3rMLQ_F|Fe)N;<Hz8a((atu$(_+^yfRr!vu~2doq@iLu0~$urn9?=IRDP$y<6
LNA;hGcl<$-n1Vkg(=W9g|4X{_%NI?g*I5ZaIWII@in=jT_43D5w8FM000=K006xy000A{S}Y^100000^rVogn5y-w001eeWux#VdY_-H&W~RyET{kgLm_jgktBpH$)&NJ-lYGh008KrB&SBDAS;@sv?>#*SRnuas-cr7s-y}QOBx@gtSF$W2%8Bi<SPIG*{X%6lBs$cPN-?0Nv-6m007n)^`a=N=9!|W94G0kXDR>y0jQQBU#$Nk45zFm!>3#%00331+#eq;4WRQZ1C|*M0022BlBH-Wa3I;E002WR2%sD<004+3OarT=006wIj3Yp#000pn!l?TrcC7#a000-G001VQ003>J$*Hxc00000Wu1Vp^r$x_005z;WTiK$FRUG{ai~9<5TF16Go*v7PO45G=BTr&{we*W001DNy`}dNfhq*3Xeb*iHYfl9j+;*>PntWX>>~joNT<T4MwlB9u_*um@t_^5fTf=xQ=~N^JgHS7000J_p`gj9A0z^yk|^h<jHCbn<)w!th^1(ze5L9qNhpP-005JuJt!Zo4IuL@lA_8X003;MWvf#swW1rQ000c6Odsqh001DYe<-@2000=R1|3)&0043#E*h*GC#?Vg007~j008SL000lHY9Mi<00000G^3QQ8mU>T008Q!8lKA}0HUC*rzxo#s2%_S?i>ZDN+ts+uB5ge&>aDs007sZd8M`}0wyP?-6J_Ady@bF#-Q9JQz}I!eVAOQtE$?mm7tlfDKr28ViPB%Sg19cAEx%DajCy3006Y6=%%n93a3k@`XY;^)u;dfyDFKREEXUm)u?AG4kehR007D(p(Qz|k|k&=$)ic5003+$Wh+xB3X^c1002WO?<j^W005|`2`k*G008!>1)fHx008K<fjLVwces)^9>74c^tpSbqO-y<#-&R!2B@O5+^BY{XtX9U->$YZUoe5NFBX?GF1EC=vosL16e^50Hm&`z9JW0$Ca(rGyDe9&4=~WHBDCo;->a>y8M;%q
!Z`S>(XR)p#<l6K<Fu+PoUvXNJhZAO$+H%y_%K(RtfSZ~?y7yXPAp<1GNCs!r!=K2m9q&ptTK5z@VHtv54_x>8LeNed?5e;007IN008u+008bJZLUos00000gRB!Dv8L&+005w=)vJCghcZy6fh`WHNTdJ&Y#jI`K`vV?ou?5fX(k${005X6YA0?lKqe6(Q7AgA;+_BiSDCXK5-Zvu@SaB{t|(co)~Se~0;B){HYiT1z9j3Ov#6k?&n1Q_0076Na-GsDYNxiSV5jsXqM!f(P$WbnT`a|{j;e94*Qx*j004U%=$=)l9T{t^&!C#2000}TJQigpGpNfi004EM8?82>005FH46N0z004X}|E!p*0015$PYBVeg(Uz00066@005e-00228V65IM00000W~#*_+M!Rb000Q8jH}V8X)|7?m?_t(>ZJex&7N>26)6`g)TsL*pDHz_008chPo5DbwvZmEbdW%xRv`cY3mqq*<Rapu9HPLKMJW%cMyf6pKb!ynb}XB!%qozmC!@}(G9<v3001W#Ri`s4-zP_=!>A#iCKCVvK#7>5R4dz{JE&Bx3aYOP003~R=b%+59hqyaf~Z%m004EV9;C&f8m5pc001+j;~&PV0093V1SM^*008JJVX6J9000K0kS7|fHY5N50011G000=Q005&GUZ<ZP00000C#gm&7N~5j003X3qN@BMP%-kH6)l0Jk{JL1SRn8ox}+*BSf(Z@$RH*m003U45g$gQd8P=Yk(>jc-X{P6Sr?BR0UI=^aU5GJ^(e}!+oq=`x-I|!1}#0S-!0TDfTSOxm>$}t003eZj-d1{9;ni$fT#?i+7bW&Wg>JKG%MX552H=31g?pk003Jhkf9qaJeFl938<Q?004C<9wfz}be?Y_007`Bv!qv}008GCNT=MX004R{r=m5g007mrXERf;47)fskTPL7E398QpgL5xQZ{U_MYayNIy#gy$}ujl2(b~cr??$4Wu?=u)iOLe^t9rz1hH*1
u`D_;gE)$?&J;_pOf>bgNiJ)%L#aWqb1$8<lDLgBva&ZQ@U334g{a6H&##=NnJtAN1})O98ZQy2Xsu!{Sgq-a{wvw4Ri_)D_%MGk;xPFn6cN0*GptWEr#m(|dowT}oGfaXgDC(2005X9004ii002>)Zy0tb00000K_yQtdnkak008@>eWgz!#x*Y~bt>N~TdDv6-<BC9%Mc5j^dQg?K_;~Y008(33nFExJE&3~3#Y%Nv!wt4YoKVQEUSSb2crhA1t}gVfviBS>9+s?%Pe{$H>BW@VJB0m{wd(B003Mnyd>)+0xPpAG^SuD8K?jNDkMZDccPyijw)cHS}5G5006KkqMufxlpvO=NF)!H007>nCMjR1B&NY9001zW*dw&80042U{tl{|001Ve8li`$000r0fuBqxuqyxn002iJ007Y^003UB-x7GE00000gQIFFqbVG!004d_oSmnpK`B@&ttwz9ZJ_`FHzO`8q#&Rk5hvFweWTZ-003$uiV<d`C79MDvz42t%%=bVNg1dSnH>-%>XDDEt17`I+MjeGOEUlf<*0w5Nvg*q5Ge1ZJt)x}001^6jib}05-JEJ7M?GmP89$EWhRm&SF3U<BqDpG>Y~=50022CcqvvQl%STWgD80{000f7E-CUMTqOQ1001r_xGL(b006J3&XE<R008QzhNljw001AMS1HXO<|+UH001kM004`t001bZcA}4`000003nO(T;h;~j0007`+nS3fmM}Od7AQ<5z^4EJzz|L(IUBd0bRJ<Ng(!fd008%zIG!M?8l%*hGA3UpTcZE~zL^B2f~qW@otz-3>?FM?T&(%2q_hA4n2xn2d8{TTN+kTKA10)$007OU=qB~0)hSRXgQe9YKA->qXC7uEW*`(KWF|PMPnG}x003DUw;$doCaGVix1%a7000dnE~)Y%Mj{9g002ZJd>wJD000iDv7rZ~003yD2&6Tk008;87&s9%Cac>#SiaLQYdI`6(5lX_z%}ToU9sIV
Q8ZPwO|%8H)VVk|<2ewh;WDEr8a6exd@%i}jHR?Op|B=1`K-7v<gSIN<1|?_nyhOm6u7su3b<{op)|TQ;wxCLJGKrmz$m?|i?dU$9;un2IhDyTcOkMU$E9Yi?I_eKcPwNiI4|NLbTI?9@V20-jIeYk@~OqT3b5-nPPPdh3!c)T7%Kn(008hH002rQ002-YMxd@R00000)~Ks5C#6rU007aXF_=Y|I4ZN1uO=a|)~WyiYMtetHzX1*n4Su#N3ct$000o7%A^@1;vR>j=%@;?0UQ7Tq@J#p9IR`k44@pUv#-h^M5xWB7NGzDD5!xaV5XC<d>urjHYklH008`plptB65wPzdHKa5s6R7|I_9)mT;-YV_Q6pcas;fPy007u2IG~9s@F<9=fF-*k00051)}yYWUL-~<005_?BCIs5000WE39pZ-007`1c(A^u006ce2qYU4C@25`004g=007q{008Kt&ZL(v00000#;F%2sHStQ002v(c%dJqAu7z2Ag6+=n5zH)u%wU}oGLghJRD@JI<A1E002W10HGbEuOLRAh^arXfF1w<(~Z`p%A;$guAs20?y>tSMWVwghPD6z_8?a!hN&R2J0U-vTOC`i0006S&mpF$)~+TPOQZ@VZL0tP(I}{-yQt!<O(P$k{iVH}007d7b|Q(X@TiEW)F6YX008BzI;`rdG$|P>006J6fUFj)000ZHY_A5X0082op|FUl007b_gCw(=Nh<&V008VH0081600170XCn4400000)~LBJU7=>E001s0n;w`UJfW>3W1y$3v#J09<D;jQoh2YFT9(rx1g_(v002{@@(^d6l^=eSNT_M9mlgm33nHSNQK*5Vq#6IJ&#WFOK^qn*v7rC}Frm05PNb!-?j^OGLoDv5002fJ#3QAp->oDkW+Vlk7pedN|ES$3ji-vJS122xzNxw-006})b0h&O)+4T=4x1#X008AIIxOm{<0aKB005V!Lake`006qKC$NaC001i_u&{Qj
008i`O1eoUdpdWy{jp^_Au?&RMz$ueoG{<6KPQ>7=`|BH>@-n6AG|cK54HO=3oP>}E2n8P<S!I1*RKmWFt|pn!Y~-GZ7@MDfUGMu=`RtoBQhF4Z@H#0Z!H-zgS&#KZ6^&ZjxE$It~Ru?bu^ix+$}k%urNOy%c+vBY%GJDNG=8>*)9zrkF;MjdMyj5+^*g*60mDLUAX}}kvcr5Z72k&VXy!I002y+001hU0088yWv<yL00000eId~;TPY|j007<}Nu*65kf+<J6)df%cqae=^_JV9=_J7?7$8g}QJ_;A004KWjHf!N{U^SlE-0%g6rlhBEU8?mJR#E~2dAR27cYk<#27EC7qkEXX{;$IqOCYDbD$_FxE?dA006G8grRP(wkXV|2_{LL-lPBk(W%xSDyu9b8Km4PX(c5h005ULhohJ&7$*XxFCm2`004QbPbqevP@uG>006!(b1itD004w8#;kEK002cV)u}5k005t%WgVNP+^_%u002Fu007k~008%>FtF<-00000jUgnYHYflm005Gn^_SBWuOGImQz}2H#3%p&nxAi@F{5xH!JI`Ur>ylM008l!&>XX+CMOdj4kr_ybQu5uoukB`QXh;U%bCHh2r;B2xFNn9d$Rxl^q_o$7_5&i`=SRR=^u=v001YZ7a5JB@G3%|AC*R->8t<%8YU~EG9=V0qM#oq;~=0M002v&_obMr7^ecHYMs$1001Ja6f0gV6Q?Ss0081G%qtlz0015>^{JvS004b1T&Ddi005Yx-=Dvz5U~IN001nc002*>007LX$E-yo00000VI~<a?IqzW002y!w5p{k(yJt@2`C?_@+bfRQ=XiaWF|Z(+niG<|C|ya008TtpdrgD+$PnW#VM>9EuH`X8mS7Uf1<S}ZKq_b*)7Z^1|1%$hOYns;-wKAk*sVgG>x>Mei167007slo~CW7n<-DDp&sg+!K(lOizs%O&89dY!=a8T1*35h004_62cvl_PpNjF@}}k}001H^
6suk=x1&Uz003Ap#w#i<005gV7Odkg006HqJEVjy0012_KCGHF7_BO?o;9C34?5nZh&L6odbc4o<*yU8r>|xy$}wOp8ZpYDVzi>J%BkZj79Wo`6fyTOUNEP#oUBl*Y&m1Jo3<x5H#Tse>bd5su`#By@HPc2iN7nX4Kp>T*|=OR#jrvt870B6-mE;H<}5lZaV_pEuq+X>O{{^Txvj*lS}8m&L#ll(b~3arlc`v!fwsiC{4-j-4z-jW)hPg=G${Z8007@0003vG001i@2pH_B00000Wf6y?eXG~9005#T!KQXB;4uy**QIkV_^ki{U8g{q5-SZXY9r(t3Mvw&001AT03vlMrUj;<njv){PM-h(&YUcqzaWR6vZE!eT_yUYWu*@;qNo4>xGf7IYoO2~$0BH-{G?tU006Tp&?||i79bZM=$H<oIH&*s0VoSB)u!bs@t0#JR3Rg#008YM4<)>$cBp`+JekTP001?s45jg@LL^@(000XpyQ$`>002p*{Uddz003{SttKF=008MB&JiRSs3-sc008tN000>i002cL;w0Os00000qo6h?z9m7h004F;YN$&p^DhS{b}0WXt*QV3Y@`+?Eh#Q3HYMhzrYkO>002)O+#++OmZKvWTAu+S2Ot0df}jN^3#P-SSfcK!J}u~|&Y~7BJTw3R87wskGomUf0VZakimD+b000OuttbPdKP<GL6Q4?~E+YT{;Vy+J(V3SkQK;u22cZC^003Am@hH3`b|`?Ra3p;z006{`wI;WzA0*)_001Q}2$;F3002EEMJlqX006D5CoDd!001N&Dk@qS_9XxS0093W001wo000dpDkkuv00000I3tjz^BL2!001H+;G-HRZ86v+H6*+(CaVAd?U`U8ge4y;^(F_M6D*LV004KTiI@hepPlTX4xp5t8JYk9P^EjLX{+O<HKPuvcrC*p!KuJ5e6Rojh%Q<sV<c28d!B@+E2${0005gR_9+{q=qxHAx~3DWsUH9UAgPKe
s3+Dej-P296Q^dQ001T=kS8@O3?=cY02N^<006`bwWhbJj2uXp001j2DjJQf004v`$tkp-001+qB`qJQ003(?Yq+^KjJn0Tl|PfXgu9zCU#cE7u(0p47_#E6*0Uot*)^Uu20Fno3$(&CR<&HPlrQx%4=o2LN3u;TD>w@?5wLbLN-|O|;3T9syszUYXsxuRt~IMJ^t}PKNhbd(jx<NIY_21uR=2b(Brnvf%(5&ixf&&_^RU06nkD$ECA6U|fiVQE^&rPFU@}H2YO+MP1G&4l!8;`-o40|UzN5dRho%4k000`G002Uv001H+<DloQ00000HlS>%8!un1006%r(5bSkRH(cn7Oe^@f~)`lcB2ES@u?!6RHAtvb}P82005?-g{P6FeIPlgek^n*O(6gP>?Fe>RH=lZh@W?*zbj9u;j7OoDKP*5%qG&SyrVBH$tNYM#H>&!000=MCZ_%yJ}jUp(4La0G^YRnb)>zdjU?tLy(gn1!Js&x004w3x1$a%Jrw04Ga}<0003SjzNCk&m8Daw007@DAR)-A007*e{3l+Z00392SE%_S0049(j;Mm7#-;!O00534000ap005+%-lP$*00000CY*~Q%`CjA000RgC#3=>9;OH=Fs<n<eW(BceWKW^)uJt^t)Q41jV;KS006_BA*cnd#HC%P)h75VRwe)d!6MZq<*Z7j8XH}q11^83NT}+N+$sP7E1JKhJ0&$LPAM*@&#U<q003Q}bsjaPzAD`!GN{_AWT^lE$ta<qwUrwvTcJv*52@aq008kQr<@M0J(T4lSR=_D0029lnjP4su&KAD005;bUn&f(001GU5HN9_004WbuO|VV002uM?y6#od#3;Z003B^005O8000Xj3?y={00000NFju%NG{H+004cWrK)}<53E`ssi?3o0jmH2ou#&@m!7+(AEiX7@+}r0003zpLn2!&AE!j1`6r+)Rwn=eXCkp5T^&iO7#y{#(=k4$7po+tM=byVRH+iC!Kt(^
nkO!#oUVHz005J#k{c?aIV@r+pPF&0R3iWYzN8POq$@-(f+yW6#UMhY004uPP99#Qz9fgN#Hoj&00284nw{9Cp{O9L004O^I4uRO008r(kS-G<002F#%bOe|003;L(=pK=r?WG_z&<21t~Aaq1gZ}y!7X#MBPpq|Lp1BH#V`ac$1#7cBDk+K+$ioQ5U<8D0yNpCJ+&+}2s9=%u{zf;q_3E<+qGi3PbyHJaTq8w+qFKlLAks#SDId}tu)E4PpIvrj<^rA7p>`{)tX4HAt|t@fTv_MuPk{g5ifl+g*1IBU8|$3!mv)R8LUY-9k;k7$vYLe3A1t_rx?;CbtnJ;001N-005*U003Gfh9(E800000-ykU+(k#HS004X>gs32=S1(&B$fBbmsHgw{WG1aBLZgSQt|plwBqrx1003tt!z89BL9Fs5nV?H5GNS+h(IR4<n<UsECM8RxktNwF#i*Q^v8Vt5KOhF76(}(%Y%aa0Bb!vI001u`XCWgc4l5!pm8iQW7^MIJjHY5AEE(*kp)4#NVJMfU008xybRdHs6e38R>l%@#004fZs48Hs+@r55001;9ODVXk008+WRVN@R006wA>7oS~008GD`=rSwaVY=*000^%0015;001*4_$Vx_00000$RNZf>nq5w006Ee?WfDAdo2Je>Y$J&@u>g+IV8y_XP{h~Hzv&>awMaj000yt4Wk68%&0IPhZor#j-&tpXP+?~tDsmP6(c7d$0%SZ)}k~f?l1rVn~e;j#wqM7lrGAqlbvLz006!s%$V*XA1Y%k=caNX`KSN@;3k%pVjl)504W)yIwW|b001#7Bq4*I6rxC+1ttL{008l#Oe~3{<{yhJ006TseJEX}008SMn<q;r000gpNF<3N001H--YZomcqae=004j_007#Z006!oe<k9p00000yCwM~k}E~C001qe5T(Zz4KoNSi>G=M%&h<b*Cd`IwI!yZdLg?N4G#I8006~_N*<>oim2-xSS6GuOP&A#
wIB<gJR+>5*d${lev&6F3aVPHg0%nu5F|Jp+n!J)F)Sgg5h%~D000iEi4yf1%qLhZP^*6vv#0<7Jgh9B&?q(?(<?m-!zT+I004WeVj_MdsH$MCeWf>@008kKOst8dYawW+002-cE2xaA004O@$EC$6004y=@TEwf008SPQMAb@n5Ct`?K(KOw5(RGR4Pg>vnVe!Pc`JR%&eod%C~Ad!7u=@IyIlF&$0$H9X4pIqqN#HgtDS6^fRV3#+bD$Ix&?rLN*CBo-x%kjxh8vvok8Rc08~oSf@oMWj6}6W3nNw-L+P&hoQ}>YcOfDeKl#K;V}U<$f?9Bt*$jQi8uf(lrTdq)i!D+Fqcy_#y2@RkGg9#{Vek!y{I0mvmpQg004@e000l4007)0;~fmC00000W~pc-K&N>o003B|y`zJtL#cYE`6uorJ|O@Axu$3*ji`^S#-4p987t!-001JU{iJrQz@7P`FCF9@;UoY6x0u%&gQ_N_9TasQPboo}?x}gIJgWczlOTH-r6$KJiYPRz@2nx70034hz9L#Ea4bkD`JY)S38?@879yD;2q>d06{7N|Jf>ly001bjp{D<$5vSRhk)dZK0040)&n%Og$fB*R000!EHYKzv008SCA1g|$002`M!6U4s002E7+7TtJTqghk0073B004}p000~%h^2#}00000x2!}a6ek!c0088mMx<w~uBiN^381m2V5|TDeXG2uE2f(zRIQ&UGpReK006nB|Eb)Z(kCdY8YZwM38DZ1S)`SuiYQYY8>adv0jE3}Z>HI)ouL2#mnXZR(<ECiL?)1_C#@=}007V|;~<wNu`oj>{-tdx<Ea1uTq+x-3@ctN+M+$CkQi~C002Xw^qBu55hvN0^PPjJ003Mbt1llUZKg!7000lFbSyZn006TVxh-j}001K)&?W?>003B_KdhOndL#e<008x$008@z007q|y&f*A00000_^K`%%Bgf9004ogilhV~e5e4au_&b)#-soMpr?f<
l^OmSd8bsXe4fjr002>@-lcq{cpkB+PNyH9KpOx6gPZ=ObsB!6L81#Qc&Nr7-Km_bK&b!#bCYQk!z;U_3Mdt(k*w(w002BFxFHBCnjJAGy{Bv};Ftgat|jQ6Q7v012%M3oH75W70085sj-GL-&#aT0WFRFW003N`tFIp<7NP8>0066^rmB=C000yuGNpH`007`6d91yt006QvK{FY**EO#+hdXeNm^j8Q13Mt9S+N=}(zIT=4X*{T+cZ{_k+0k`LOIGV;WVAH+_S8!oH+ro_p(8*W-NoR@4G-8;4L977PK0;QK<*6q_j#f7cT9w{Jh<*<T8k?u(d-X+%0<{YqZgzXD?SIkfbB9Ca7wtwXF)UiLx-Nc_}!keyFIbEGzD<4zLobNU*Xl6SaQ0SUlmlUOTTM3oU!0ye9wv005yM005}0002*=2_{4-00000#3S~hVWRuA001$nais1lnlsEP7$~lsuB-q6D;GANP9{wo-x?DkE}>4P000J{gQh7WQKH$UL#h;}W1#>55THw>U#5~DZkg+<QKQ`*z^KlxA+`Vj$|Uh9Poy{*qb4M%wIFS(003MexF=+r4H#A^@T9jSq?`Z%{iO{cBow@YTPPu-PbJ%>000l451<zuX`lfoo2AVk005<(5336$y^<ZN000o4Cnq7T002Rx)~Jml006iZ_o)>g000l6EhW60Bq#s?003Md000uJ002`ViW+Do00000fFBR3<sc2S003mE&Z?j!X)$9d5-4Y<xT^pFXCQJUo*Yjl7$FlV#iQw>000#Zgry9t0HxERyQ=S~wxIw3Pn{m8DXXCzQlo(vi>Pj&GO1jscCY{d&!h$<7bu{qg(M28Pbk))001AT>7C1{;G|O`#i%bPJe~jmkf}zXbEpWZmM9gVIw&Wj002~`$s8A)X&?b7I3buT0094`kE!#hw;+<L007XYB_T?y001tjeX0Z`0049#h^_aY00597vaHOV+A075004I(003~1002!Xq#mg$00000
WuMv~Hz-1}0073Nd8pkb@GXcb1|%gWcB}vZHyffTv?VJgjv`+n*`%MQ000o9cBcL%6PRA3HKTMGhNJ)h3WpP@h9OxZ(4m>9Eu@?!b*XBkYc&7>a-y;z#G`^FIxG{YhacE0005GwPauS<qaZLVwxp{hWTgNANu+C!jHiDmsVMZLADdsH003U38Hc4E4=W2Lz9N+?0093bk16x01RdKZ0034ZHY{+i006BZkE2Yb006uw#HM|n008i{R6M;r*EK9a8o+h17&>k)Ej9zK>nz7L+Obfm#<T`7ovL)W#I$-ZMlk^_7BFuzkE8fDqqG^VXt#T{Xes(IYBArfCZMY{*R!uTM6R;1Kd<h%X17kTKQx0fcd(+T-LQ49<+RGIs+!uX;+|k99kgGx8nC0NwlZO}y)-%~(6DH!$uzhhj3QO2MKfBifGE1Optc;hwYYCM8?Ae(vZTYI!yy0w005|?000B4006cnTdBsK000003akhzs3@8o008eD%@gV;_ALexU#M-MP^AC>1(f?81SyUz`=<w}BPtxH008VBKBdtug(SrybEFcjC?Ws=NF*bmlq&3+<s|8>-mTH6mZ;^eNv{9^Fc`@pHLNqLda4qrh?pIk004HSnWFBfW~?cx)ShW6J0t)A^rw6sM63&_-l@2&ni)5q007^n4ydHAY$%4Rmm_5?00393bf_GwyR0xK00366>!>TP003997O<hP008o#zpVMI007Y?UMkca$|wK;004cL002Cx002QFAt4Ya00000F06JVGAr&W000Ug#w0wgd?~^lmYAcerl9};ninb}+^ESXt)n!cIF-Sv003&8&!`)$t0<qMyCub_!khpAxS)Kg#jPtPIidKermWhX9j2G8cBlXVY^rXb9;gYg<)yKog{B9p004rik)&Uy5v|XtNS<*ek0<~D@~M3oVXQf*eWt9cQzXEg005UO0;8lZY^a8+->HtF005UD5Ta`*jHjcf001$l$g0q;002v{%dzgR002HG6|lXi
006L^JFB>$5GViu005?#004KT004EU%_o5*00000img2-s4LAV006Ed@}bry{U(?nqNTs8TcH2|EFBD-$Ds-;HKbi6CZ*J<008M50jZ;)Bq)QCG9<UGU!ed1gqudH7Ns{M@}OL*37Qb8^Qh6NG^hXoHKvmi6{}6ETdGE-gecz~001l<BcV>L2CYG?1gME9Y9s&vjVJ9SN3C3|=BoUx#+>US0086{X{c8#bSNCFU9LN-005Vu5F%?QBBGls002^^qN9AR001zthOeHm004;_PpMz7006Kmh%+FqSvey&;W;)j(m1cBZ8p5Eu&@g)RJE2ei=!*2XEF<{UNX3-k}?@Bqp^*wrnmAmJ+vk(?KM-dlPqd9&N!qggRsM(61aY^e=`WIwKPU6)3x%j%D^Khjx3cauC&4^)HVsMysoA&Gcy3Jc{B_%x~nNE=PnbnXCK<7i>)at5wOds3$5a!F|EX`xGgF#MyH!3>A4>_(5eq25iGYLC@BB{006fk007aX002TKc%R)U00000@*rca#T}8f003vB(5G=HbTslQ{2y!_fvf-k`yT+6dne5v-yT396)E<o001JOAfu@$FqP+_IV2S+xTF98BO2nGh$S{89v*<F4lA>l0jdM5`LF;0(j<SR(xy8iNGVRH*C=zQ000uDy_l1vbR(50SfEuU(yIUfWv6f$38^I}jwfTB6rAj-008wQuolXxgc1Cq03V<x008l*cqA*NLm7!D006k1NGsi|002#;<0qY-004fe-=-}b007J(KqYu4k0$^C000Cc000vV0094<XC!1L00000ZXdfLHlG8u0020o464N>05kC^7$p!J0;>Q3Ssq0m79T~P10xq8geR7x006on<)k{P>!pO8!JuOxXO{o~S0Pi7e5pvCoE=u6O)4IvN2zqFMy3D&;iEgICM2#EUMS3@StP3-002R$#G@=Ae<Ri)E|xE!QYZibStNrWJE}1tej=t5ha{Vw002WIHXO<+gpvHAk|v8O007t&
YbVpFuOqrD0040)FDlxq001i>$0)2F000K4r>lFQ001B)o+$w*B`E*^008470091`001K;$0N-d00000-X#XC0H~3)007ve<){lN3N%V8-W@xcMyvn;dKoPvKAgi4kQ&1rejk6O007_^@}rfc&Z6j^4<=Ba450u3St2W>OrpG<yc?IO6emF_Agea7k+A>(2%w;!lB72tEGl-W>LCfM0057zof_|*3MSPl2d0;m2B81|^qcM-I*g1ba46Xz!z2Zp004d^BAoFkc%&<&DwVJ(007vOYp2twVjw&x0034gJ)u&p003Yjml%Gf004HRBCHIc0029$PrCN6;khlYGCS_LWitn^qAU+Gv@cMtiLD_yrnE`4RW5=){I=LI)T~*uz%Rh9CN#gWq9_n6lQiBu7`*i{>amQbf2}hrU$4M0NU$s|Z8niSDZ2YGgFJXD&ot2~Be3(U2q<5uYBe`8;i?p?EUnHkuC%bE{31sz6r_Tu1GGFZ*Riane6jMVh9+yCQ74oyudHjl@*#Y(6DzNwyri@i{j2~0002&+000Lr000=P2&?5O00000d8phlJFV6$000*r5uSD+fv+;8%^5eXVxj;5I3Fq=2O~x;@gJ}&!K0g~005<&(4-0|e=K7p^DN0P1rh)N6&;Br;3zUAiX=3&doN(8ax8qT0Wbgnv#jc&HngEE7N`>|X{oj=008-_8>qsso+yN>xhhl?>m>jHkfa=-ZLs*GGOG_LV5cmd006HlJf0Y))Sw)cU@7>Z000%O_$o*#E0p;v003Js0WLnH000IsGLU2|0003T+^6;=008$RgCph?gRKAn002>x004h20026tFs;}u00000j-V$nhN}=P008f!f*AWDsH|wEPc5XZ)T96aJEGC1${?aF-JTmSb)~nc003njL7^op<1DNsnlF+qQ6c~UT_<2D#w;fy_$l_VJ~A64j4R<Jg{S}k|Et2LPOzCT>Y{%oZl%1H008c(=A<F3;w+t^L@2=>^sE2?o+d)1
xU5Dg?WtWJuAIM;007dcxTF{+)F2#`YanYH002O$Wi4VXW2mnv007`JV=r%?000&;a3|s`002xZG?{29004NMx+n;t7_0yQ000FL0034f002cKp{52a00000Z6GZ%^QcBH006Ee@TBb|ov<pRVlD2df1UsUX{ZLM<|QmGfuUb4+nfHT006n1Ul+D47At!hRxNWYULpVh{vksoqab!6z9cHKek~fKl_@tQp{f7?7Oo(sh_1{mN1R$IqNlVZ002uOwWPnUm@bYQXDTM9=otV2M<i6H>#uSoKBrd}j;E;>003o~ex(&H_^L=L8K0n{002NLWvyZ?XQ4)+001p9<tz^_008wcnV`-r004w2r>NB|000HBimw;ABD@4ThCU)Tb~bgh<uq%uDK${9QL33N5iwgZo-rXgrZ1$bHa8}-do-J~oh|Y$Losf#b~kb`9yws5{Wf%^i8OsA{VmrvP_#!e?lNt$cd*Z?<ge4PpDZ#euDLTMcr7U?)Ulu}&@9WWNV$PD1h-Bn7`G5K!?~iYXs<FgY`Kdi;Uz*bg}92VX|3zE)Uw?$cQ`;f`Zo=#s3S(KVVVE{001_o007A(00023`WeV500000cBG!BfUAQX002apSD_gyfvk<9OsOxbF&_W`+#YD5i6EvBW+hG|52w;9005FHQ!TnHZ>Ur)*q`31YbpQ$C@j?}zNLYv$t)l(<0{0XiX}&_d#L~bg{F8Y`6zNEaHEbXLZtkl007RZr6l5{Ng{Kmxgm|1Y%Kr)rYd>^nh)X~T9}zEjHg?e006KleJFUM@FMUZa~ZUv006?QCaATkF{`Mf003R2C!&|B003Gna4Bjk001^E=cK(V000x5E2uoJVWI#4001AQ0049#003pKCnZw@00000i6eof8LZl&0048LD5M`P#HoX#f2hEn2qpjkvLJ~fq99KmKPB}i3ahRv002NIi7dY%(57`PVWnxMW-9;y!6>FE(jh{sF)Lgq45Sg8R46B@oG<_Ypd^na6`>U&
GM)`4%_Ltb007&Z#G4qTO_^V$yOnW}&7}YUmLT{Pnw>EvubAj5OQ6G@008!&VJdhc@S^Y^e5S6D000uH5vUxj`>6sP005mMc9upd006}-r7I*a001Q_+8tjg00670c_{C#@16hv002a&0082m008BvFet|?00000a-v<J!K}<4001DPWT-AF&8$PD@RLm@A0hw%ZzX6T2pLu-Q6-BcA*@{~002-OZYm_B*`}l^AgMW~8yo-tRU@_?v!0<Mq$KDi_?<JTH5)>#6siCKg_J-i^rJy2lc|1}MXHLV002g;H6-ty1tLMG3nY%JDW(7bP^)bZ8YM8L@txKzmzofl001MZ$0))qCMdP30IVyc000sx5hxt2GNccv000pvGb`4o008kW$Ss&I004I{sUIpT001Pp@jIt4dO1TtOTRES3O9r^nIyj`JFjIihocfIz%Cdr!m!?@-LHr-t24W(WV2K-h%(=+E4X&BT`WShX{;qML!?TyMlNEpr?yKjc&>r7*ei6fYN!CMJi586*e-6TEx2eMgR_aDu()Tihps59Q!TYAVy}{^%rZu;6{U6@im*|yfh^%E1FZNoz%iAlu&o2HMl=OEG`NMfl)4@ou8d@;Iw$}D007Y)001hf006Tn8mx*Z00000U4&LFUZ4!A003$Z4y3s({Vnw*#VN@w;jI7w530ByA}otCP^Vd_Cn}(+005Yqd89HhCn>oZRV4PM)TjUe1ERjDT`apOW}E6L;-~?o;vjG-I4J-Cpq|>KJ}WV(Zl!)Flp%91006?MZ=;1OVxZm>=_LmzaFqZ6>L4{FU@DiUJ0QuVRjGKT003K{b0i9**_}9^{w7~10091{BP&5I&KF@S000Fnouw)x0062e^d`ls002B9gsFX%000&l79-lKd?f$?004NQ000rH008!+%&Ola00000IHxfyE-0g_006qFZm8s=nk<;0#-ZGmfv^Ao{HO{QBr6v*nx_b-e;k6W002Q8zNcuWk1QCaPAJr;d#3;Z
y&p@ebgauK8>FBlU8z5$ksX*E7$^V$?WGE)!zeSPET^<5svkrq008r+fu)5iFRMtX`JnPBLZ1Kt7$9Y!A1mUcq^PE;lOy*U004TYlc@?L*&R5Z7a*>p006*~P9uG!FeeEq008?e`lvCN006Ti7aKCH000aP7OBps007Y;Djr;^TP6Si007Vr000H8000pw38n8R000009ijOwT&hm3008-@aH`5F#4zg_9;ZqrysrQNNU2_*nJ7jxn5Q(XotTTM006)k%Btfc5hvfF3L0&zp{f7?#-!D%h9dnb{HA~t*Cs2Xxtm@m0jmH2eIz)h_$kbgq?;%t$&m#o003?rsi2-HimQB}Dj~cn9~b}t5*&INK`0}rVH`WDb*1R1002~=yr}*sBdb9zSDbwz006)fPNRLK_a+=F004g~o~(S80055`%bp>s001PVh^Y*t003excPt?~Sh(!G06FTfva*9Pt2f`X9xIhGt}qF--7UE=60`9yL$(kf2{|1k`?mcq5G^vb2{R(F!?Cb1JTtkpMX)Y3C9GRFI5^if^R&IR3Y_#bm#tp32f5BC?=`ceT(V6r5;9Akj<DgaDI~+B3b6pL`7XewIjfa13ov0U7qo7o&NHv8=dh45s<Id<sVX5QT{c{_Kd2S7kU09Jt{l~*7a#xt000*b002v>000}P8l;Y-00000v7o4;Dy-kK005Jk-=tP7pe^wpF`@jTrLF(~YN4#7cPAq%jviwtD5y23001T=Hza8y$0IeF|0Y6{q9FhP;~Tyos-l~t^BpgzbD_elBCK&MoGAbRxh__uK`W&oi=ADl7bwXn0093kz$UjSwJ8D;kRcT(Rhs|+5im|9`6$YyW}5JjpC)9f000RuIUdBPgrbp<EvJ1c006EZZKquywWJ-O000=GNTXz|0091|UnKUb003?mW*cCr002*<?<vQj>L&mI00254004=m000rCW+FqW00000`=&am1u2KI006-px2RbvJ1hqxX(WdzTCD&85h1Z7
qA6P}#wIx{rz|Fz004<3b}2xpGNe-@rzCYOWtsp0TAqlV$f$0g+@WG4FeVPEcdG3zsjL711TXp@p(9fv%cXOuY#eBz0084FVH{hj=_t{ocO8Qu@~Hp-R4kAkEG0bxexSi6Eu!J1008qUL>|N^gd&lU>!qY8007A#ViZEG>7@H3005RH!6_%L004ldfGn7+001kPP83wF007FQ>YwhR;VJ+C001o*007P?0018&`J#;-00000l&bine=vis007S@5}s!%jjfCw!z9z89;^TW+ad-m#3vao8z^Tb!W;yr003nlH6+y`;3ncC3=b_HXQ2Q9p(FX6_a#9drX5)*M6Fz_QLHF0u`~bxJuP}DP$U$fLZ`o;=NTBI001d5#3{C&$tR(tFd*!n*rosgg9VusqNU|1{H7!zgC|L)002uWwjZvaZ6{qHtfp9o007CLVw6IxaHb_J007jV1R=Sv003Q}eVcu(004j|{-YPH003&XTQ_DlSFd3!D!$A*Xfsc=7Bgry>a*4+T(Kdv)UJlHB_;AGX}NeU$uRIRb2Mi&BDepzjWj%}y0K!k!?+c)Dm4MCelqf|Xt<!X->w_DCnOrU6tozz8mpKrs;es<oVtH7$)S6xKemginyp@&1*a6Pb1)ArYOrn_axVE3!!X@0E+|r}@~T!b&@-AXtgkROjyNH)nK(H-lr+GqK^i}&PaFUM008!+007LE002TI;-fJy00000DySA74=9VR007acn5(`bGcvZMG%@BNVxa&4At(|g<tq#^wI!%5KdjlQ005dPQX#f11S<Y0Nh)I@H?05w>!#PMog(cme5ZP=w4%TeXs22iU9bQEgsC{F<f~Gwxu{jE?WRtu004y~FRAyMBCSTK4x~3L)T96ay`;CMAf|Jv#*faN#3TR!001x?g(r@s04x`zeWwu^007$~z#~>EfTK4o002#_>?IGa007^t5~<~+008r+d9I|Z0001~->l%M?4|$!004Wa005t>002a**`F{j00000
WvQ8|Laz|7006$ND6spVgfa%HPcb8~NT&b*)*+1`A|+5T*`9PM)2E85004R&c_n%!$0^F6Xe<)01gii5>!(($R~Y{(>#4}BLaz&^WUNswr>_72!KgN?cB!GTDyf34-li?0008(5ajZb39<D;CuB*o(DJcK|9jO|t!>F{co2GZFjwS#A003JaSgVdD0IV0Jz^J?-007n{9;kAxN}+$G0084BR;~P~00391ORtBk001nhV6eNa000oEzNY%9f0_UQ000)K005(`001N@>zKSO00000)24SPY_Hs|008T(U#`xnv@-LkdN2L3k*xp#kP$+uNT<p&VV^c3VXio+007+|<)e6|9VZPSoFh7{D69YgXQ*wg)uMqbd8kvdRjGHWp{hqJ*{=WqpeQ`4dafa??y2ag1uWAj001~G9;eW&AFpqz8mq`Dy&wPpGbW3rCaxW>g{dW}NuU4#006R_38mYkz@t_v)ubdP007pd9w>6GgQm==0073U9Ihd%00087ORhPp000N9X{<}E003vOdNt!KM>Q@$1i&M(H@twKEVj<Db2yqUZ7E$embC$|K`;$0a<)AuR<gyel{WsJAuyRIfittMn=|V<w6`j@TebPM7A$wL*fHoh4zi}QVyri<R5&3hTshjVN;l9dv#^pcow;`;MlOw`WwfZFQZ%bEA}^jPkgO!6lrBJ^maX<IX0Kl?O)^g~JU7BEpC~ad@3iqbs5hQF=ekj=xFGSS%ai~B004NY003O9002Ft4V+*t00000&8(U#w5zf$008r%$05d}#;)t8=qU!NDJ1{^P^iJ77NxtKpQ<+~_$t_+001{69jcEgStB{75GS>yryKwPdL!qi=P2WyjGp$Wc&Boy?jb#^O{@R_AFLE2jH!|+SFNm@9;?GB0082WilG*ymY@x;o~G27ah(7Fl&Ny1?xqo_U9RS-45xV=001kh)~NxaRiQ<%dZ`Ak006=wf}rOfiJ_As001ta)0m*C002lCW2{)P004|HXQg+n
007&k?W>-sJ|zGE007FU004xp007&ixuexC00000rKwRWb)}3g002X!ryy?~0j(6L3M$N}&L{u?nv$3!n<um*38<hd=Oqpw002LqS*Ot|IGT{C6R21r6ej=xC?pc4MJ%nUYLqvqx~e>#EFSFyF{%In7Ok`q2Be)H>#Lxon5<qW001+j9ij{<tf2C%L8ZnOjw=8Fh^)(^2P|F}XR9ozYNU`L008l;E2se?RUt*Mz^#!O008zRAst30lb>>&006(JrYJV4002-T1g&wd005USUaLQ<0066~xu=k*AC~|C003g8005}1002H9X&vz_000006{%w^oTwNt008$RyCAHofUfDH?I_c%u`B=pJ)t9)$*TXPqoetxmZsJj008!<fUB3S5T6pM4WTNjDkcB`?4vWO1+6@&(4`M5jjdXy0VOCalOg~BWTG4$Mkp1mrK(9MZmGpC008|dWF<GBYp5`-?WA-UA0_|*FPsw~K&WM*!K~b^R;R|B005Y)AgRKlf*|J}C#2dW008!+A)Q7h9Hf}2001SR@gPE{004p~%&iuy006fxyO9K_0050Mn5zD(oV}4byF7_GPduEn9JHpb7%c&`G%QQBH8k|64>WAO?=o2<nl>J=b24bMM<(1jO*S4b<~VS+-?(J3Z@JWzvnTs8JS{CXCAd#D&ozCzX0R}@SuDCV8z@>Ygf$E$tFS(!!mbCiMX+qG7BW(=TB<6hTB$y;2eKlh04zGQ)vqfl3$uAI^02cl@*Hop!L;bLqp^xQez@AGQY(ESz?=X8002B4008YQ003XCA}Bd500000rXR)t3#D!-006uuYAeYkR;{5VVws2~{wDwcBq#A6bEQ_Nekd3!W~Y;w001l{g`xGGaU~+9Mk(@|=c51sF%(T8nxV=ha}oEiv#MMu0H9u)lCS^(=_ia0(yz>{;-8DAqpMV$004g{s39n>1gNwfZ5%DBCoBK}sh@SH-lX@S4J$4#T%@QY005RP1u0IeYbY@(U?l=4003Sq
XDc)*NTpGo006?Tsx9~?002iOu%AdR008N)WgGIO007#b+bRMmHYWf80056A004a;003;BIVW|b00000IV)f!LZZYe000gsm?;b^A*{9}k|S#*UMK(n)DnWEejwSYttF!?4yyzx001i?`X`hqB&gyVODJcgz?%R75*hlV2cVG}J)AnJk|QN8Z>C?TJh%V=!lUIaW2gzJ;3P<)5Dg2a003Gbe<_BmP$FO^Vj$zBry>9Vwkby?ah-ggA1v1^AuKZ?008tXMk!7!Yp5|P$qi8@002uYc&mS;av;tn003vIc_4Z%008JMJ*r4B0073EouZ*5005q;3nP#ux*`Ao006rn006oy002p&C8h)&00000jvTb1d!(o)002iRLM#t0d#s`&a3(1%L@59OLndP<_9(xq_$C7^#H)EC002ZC-X)um%be()LMv;dKBE8tuB6f(L#gs3{GlSPSE_9&6{waeQ6vBW1S7R3RID?t0UnMUNTRGP002}YXP|(rL#KHqgDHWi4=4Zt$tqQ(d8a6>MJqWgETEYt001j5{V86nXR9<Ro)R=B002v@cq@OTSSm9q003*RS1FAh005{hYprE0001ke@2Gkv000cN{x@j45j8141HehUOFWme0kB&xha>^8!>l{7ODdJ3@vuTXdOdEmD6*lo>Z5q0;I1Su%c>rk7y@}Y?>d^aXSJBM3bB}|9IC-B@w6T;mo!^8$2f>9KQSaQ%eGISL8$L8mNImss56W(Eh%{*953K6_%J&xIIpZQa;)K~#xV6TLa^p4D=q+}oi9F~60ycD-!td8YP4dtWwZ#U0H(>R%p(8*001zi005XM001YP#i%u_00000?x|!XV=D|M005Vycb|M9D=bs0Zz(Y;tSkTkBa?$5%q7Pw{Ebbmb0sDz000;yn<o#d5SXkbD<)8<J1GDFK_wiP^R0xammrF(1h1tUU?+H>Ev^6nJ|u&l4XQ4#Ppk7J<*4x>008BqXr`^GgQ@DOFenhHG$Q~2tf1JT
-6JTcJE6cOJDBGi004t6l%6iB_#y8aG=fE$004@l$0heFyQdBu00394WGH<k003;H8Xbfn004?Cl&-8K003m21EkicQzifa007>j001>A0070Po~X8|00000w5vHKe<#xz001+ilpp}1I4rfP6(}7gXDI*x@Syo35+qS8;SSiIcpcm+0071)GApQ}rvd0GCM-^#Oep{W)*^H$6CjeNwJ0*Bv#9W#5+`*e#;*VX7AC-*396N?u&Zn;EvBoL005<wIHgmiNUX=KdzvJwlpg>9TBMAm>Li`0G^v^&44DHY000%OO{Fd=_@VC_c%JsA008HymmmVB?HlVR007ykzbKKN0017P+9YVG005aSo~*JO0048G+@~U`s2~6U0023k002`b0045SHl`7(00000QmQ#70;i!Q003zuKOGV$ZY)ly@GB1_Q!M}hDxb$8&8Z(M9Ufwz!YW=X000soy(zY-<)^?VA1O&F>nQ*L1(vrVaHe9YpCJOKC9cPy?koK)ZLa_Tn<4(4u8;s9AF8J))S^fy008c%?V!A&Ca2h|t|#^&Oqu`yLMTh6HJR<D5~xQYgP>xY001H-Ng;|P$EEiwWu*|N008GHm!JZs=Nb8=000xD2q(Is001tYRwiJk001E^U#qqz001sDW4`S?w>KlXfiGaWZ!>GD05pa$uP`&QYbr@G0<dVNp{qr_GrY627dQH_Zj`Yx^s!Jk%rS*6L$1WQcDv24H7w<=S}Wr%w452S>Zpbfk+DCxo4W`m9Xe04Mm0(-qMl+Pm!j|?YqQCql%wn+hbnh1m@&1b=cG(BQ?MeXYN^;Mda(ATlB>Egw=laUVyyhKAvD1-IJe`u13M9#86ZcVN3j3^007FR003Jc0091+E-3D$00000k|ryxj<8cL007q}Rv0TJy|6W>Ca6@UiXQ*~N2H6R+ohB$yrR6M3Mi!|008u*VyF+MM;SJtOQ{(qaijnMT&38h?35BF5Tvgyi!d;!5+sTv7%Tt)Y^;5zmM#1&
YpGBnY9Kc(003C7W~3f0!7J3LR-7@ajw=8FbR1Ept|(_Ikf=x>J1Y7h003{V`kt&J@2dPMWFfbt000S{qpk8OWg(gt006WrPp48W002HL^Qgip000%M0Haqe001H)+#$jlYOw$S006C^003Vk006Y0{-R7J00000q$yCV^sb^U004m_U?Y<&cCZ?z=cf{-cq9M-QJ~$WHz|ZCbEJQ&t0WL8005(=z@%p?;gB$(x2Ufs7bE}x10S{=%_mi%4IjNNoH3rJ2PP1yMKAyWfTJ}OVlcogU#O!bmmQuV002}XX(7-r8Y=;$2_>wcf~NogJtevvi!SRN6Qp%0JEZ^s005dHJejPb?<)K$^&PpP00539z?;Y|#-M|w0034j;ipY5000Oq!l!2_002v?z$Jk!006fc3ZEIFcd`Hg005Y#000UV0006d6CAWB0000004W)(wW6sl004IZMVl%p7_gD3Y8+UmixmI>%#xF&7$s>chM>Et94d|`001%*yrQQpq7ObEF{nQ%f29Bbdz!$Xuqx~*_Mq%6(=h4;6C<aph$#R7qpR1b$|~|No}wun_@^K%008!@KdMPBA}dLty&wvp1tI_d{-_wMd@kuJHJcqM|0XA;007{k^`i+Lqb>3&>KGv!0054qz#GUd6ck;h007P{HY5=*003VphMn*$001Dabtxb&003XE$~WmdlsOwh<iBsGt+!dV_@UgkPM<0&daZdjYPGU4kTRmOXt$=a=ronAoUq!mxUH-;n=GfP46gaLFgZ@L6RrKUCM!=YCbMrc2(<*Xv@Vi5gDxgA={GtkPb=~*0xbzJ<}?>6hqdr9jWwJt_a%<4X)Jy#<tTbAU^9%N<SUUiEvhRl#vH&ldNf(5lP!m;P_&`A0x<WpR656{+MtxJz@7jA004QX007Ua008bAY^cE<00000eyI#6iY7N70001;J0h&66ENPTMK8)Lj4J>DE}_aJ>7*zv9jF1Q0<Xxb008=+3aFB$Ln>3L|Ee`CKCJ)%
{hT+d^{P}Yajo#A)*nx+Mxzd)*qi_WkEB<r)~4`|;;z1=4Jo220007{U#cFaaiN>7${;}~1E~N2VkN<<jhE}CU9P35psUm#004)aX{#Bjkt?aHHYsNz007*bjiV_gOCT>O003*GP$b%*006-xvl`Z@003?%GAvZ7003GR+$YSfuNeRU000iA006wD005AwTC3Hj00000jjSprh@)Pc003d5h$4-tM=#^0129D>L?-|Mb)i*)m?7dWlcc&I(w~Q?0011L0IX!BBPc4S87VNVF{}UpU!f+Z2`6eS@TsSv046i4J)h;4prZf)IGzurM51Vvl&&|HF($1f004xi$)`J{@1#wxTcji?%_#r?OsOZTo-DGfUa+pJHl6Sv007#hX|EY6k*ley3=tL<004ER*{XG>=_65_007pgd#w1V001o|U9LK;001v27A5ki005z*ex<;y>7W1r006+J000%O0007&ai<#|00000kfmQNrXhGL000RPyPL+TaxIdkATBT{h%EpB_YhB^tfnI_NR+uOaHe}C002Ro8?61No2H?lwJjSZZXf^v{uS>Wy(4m@`kBV0TPRAZ4W*tba~%KxE*way|D3ZL!K_{t10^XY003tq<fjCZ-l!(7Di;zbStI}ebD)8!J{;qvTd!KGpRR74000Cgg00*gjUy=~Y8WJ)004C**(!CW?j)Kh005XDVx(Us003bmq^M%2008kPAug?`000jv7q~Py0KC{ek2&Nva=Klx$FPVtku4Q1RHcu)zBF&O$0<3osXDQzSghwFcPV16?ynZRkvJ2qBC{c{H#Ph)={CZ#sjTF%EUpc-(J_9tS21O^YMp*9Fuo798ZC#b2DtGxFQT?5P&e7G(X5jvG^mlSpRypVm=f44#i45{46b*u7qW?~aIJWy2QHu`#I2AlP^qXnD5S%z9J-#SuqOJVx*z}m000o3001Ye004z5CXVc<00000RU)CInW#dr000S`GOE8VM=9MT(I2=cZL0tPO{ZKR
C@!TW{h(PWt|)q`0054sx};z!TPKF7QYkVheIx(?wwMEzs-RS+N~H&>=qq!a0GcwaLM#9Puq<$?8mt;Di79iPwWw35001qcrJ-M|@*)B#OQj{K${GLwRw!j4fv8Iw%_X*>1E(#Y000N5_NbpLt0+SqWT|^9008)+RwZ|*)~fL*0000Xq@kdg004%kT^-+|005n?ub?L&004xdO(gw{^q2qu0069+004+3004iaBclGN00000!lt04jjZdk001_b;j6PHf-R6Drz8@l8LI#QMWA;dd?hfXTcBkpyQgug001qhk)}{7?I<#-MWf{+vK;^bi>1OIr7EzZh^96j11m_Xkf$LiL^J>Z?Jd@;c`AOWWGkI1#wBAd001Q@Jg8kOlBkC#ktKSoJ1YPHq#?o~N+(07sUsnvqa{<F006Tqu%4f*tEfXA2B}#r005w=6ez!_J*SkM005FKwwmIr003a9K`8gC004Kb#+@Ri008nJUMH6%v?Blj002@V000T9003N^2b*=K00000?jta$!l1LX002;+zp0igFEFzvo-G-uw5tFBbDzzS-z8G0t)K^+lOCa|007pe7NyK6Stt~#2BH)ltswvaY^6$}*C{)vo219BaxGe<zN$zdOR)d|T`$9^s3K4)$0su&{h))W005CA8m6ZuGMK+6>>@R$A*lcWA*oRw%P2ggq9!{eP^fI8005_`5vBMdR;72RHmzqO005vV6sW(bkg9VM000jq2dXcs006L}b0r$6007{tV3mWW000~{t~AuUZoEykQ9Yx&k2$|JA|pK@?6h$&RjNX;>aZuZ(l9l#m^U>wA27DGORRh^a4>JI{Ie~sQ7=)k_O*F59yNKj|1bqDjx5DD_%ltlTP+SW%C)yMnmg#XFFKN%$hc`UY%td-GPhAIO{DZF7&i5&;3)*57&Edg*0Js-7AvQ*S}7~2la^I0hO=xSZZL-_>@-d^AhH#o&A7lPNTrM?oh1MO005^U006_H002Uvf1y;T00000
lOVn;aVqeu004oX`X7uQPORW0K%0G^j;H_t)gk95!YU3X<R#f9IvQG?000u3lPY*F^rW34k*MsRDxClT;UZER+AHRdP8eOFgQ#_|3Lr5k<u(8S#Vnhqiz;BJz^tDpO&vZe002HN7a$QT%&B3j!y(S5jGh1h^({sy?H**Mo2mvCH>j|n007D%{V8Io&m##egCO!K007S@Sf9@ojUyH>0079RdZjz4008!t9WZ06000p#fG@MH000N0WhL7r7%2b%007_`007OZ007`9Dk%Sx00000xhE$fSTAa%001eZPN?`D#I8>w$|kX^k(>Yk>?Lq1xFw#UZYAZX_8Tb|004X=swS>0rlo`-2BhdA_niO$aGP)=^CQd{rI2AJn;fLC8jQFr6*m9?J1sz+?Hz9&(5R**V<Ii7005>aaVij^5~kCpAeNb?otyvwqbLI?XPuR($Eczn4xyZw008(Z0wrQ7&!Y(}+$a(j006TnDX3K_1))4E000uDRjUaf005>FQ7=uR002fQ%rEk&007z}cq^17ZzKQ!0086}000%N005#Z{HnPm000006(}epd@F6K006S5bss}0F|4K;xD+&;9jX8T0UX&L051O@o+c<HSQ~es007;kdnFGn38RA-d8P0WTo?cVL!tke@~GyaFQm<)t)B$0$SE=|IWzzOmMn{wS1DnoPpHTy%%-Fy003_*1}gBSucRlY8X&W*1t<UjQ7Nt_GA3Ul?5L`v6{%gO005XK)+EoUSRc<7HYMhs006V7DJWGa=BD8-008-`kflc@005_?x-S?c002BHx-iV9000s(@vpl&1-Z1j06RCfS2@wBYA$D;m?#3WT`s}8VzGstx-j`JV6Zl=>M{E)2&lHP0I}IJhc;d()vP%%8Mv@BZ8xhi&?maK2)I`%+c)DI^ES9Lm##u4x4m(!b}{~}U8M*$qp)qKjkB0CDKuoI)w5@^_b{TS8!AgQt0Z0^S1y>c;4RUZZkMh!6{vJ0ax!|X5jR;n8N4XE
?zmW|Ppu)U7^naM001+o002R(0037k`K^#800000xv6*_>?tm;0045TF|g(&>7>S{f+N19NUQ(=j-AD#>njo``J@pr;H;sZ0049*Qk(%QZlJrSW-nZ+ItBm$9F(u9v>g(i-lK~v(vh*HuBRv@Rh<9;jH<n<xGMIRotD0*!>%wX002g<tf84Mah|K7L#J+{M4kWuZmmnBSu8>*Ags@;K8WR>0020t1Fh^K$t1a#?WH*=000l54lVPoh^@(}0040-(W={}005#arJZrB008$aRH+xM007q~hol@ORwMub000e?0020s007r1w5yOT000006C|%EIVzs5008i${H$Xbu%{LykD}t1W2pcD1QmIu`I@t)DxdBwU#emx0000c@Fwadz@}Lx11>_TEg}E_v4JrrL>;!A2^Qw14lGCv52hv{3@rcv0UrjY^CZVDz%9oqX{30i000cBE*@VhE2wWNHz{4E51;@5oT+&!)hIzFav%95y`R7w0017Op&sm^$)vfL*{Hs&001<j%r2~~OQ--T001{E5Tz6(001B?U@MRr007P?U#H5U004p!MWzL&TBQI0001JR004ii006=(d#QA#00000Vxvf^e4LiD0051wFtEs#ODGVbA|+6*#jgMWfvWeaiK!7Aim4DNeyaha005h&h^MHfDva%@I3@(B;vE108>FhHg_-K2Ri-c@)hXp3MyW2YwXXmG1E9*95vYTu6(TLDv86Dq005+}wxLRxN~lU9*QXYo2`K;o<E`HqJ0tn1xs*Dg4k$||007ynT&fQs4z2U8nXLG$001;2%&x4gyc&S0002*>A+Qyt006G2yQv4P005b%6Rkt3004_E<FPn4UN=3sWI7bGw>nWHGBIVdc{Q>zGBF~pwkfZ%>NMgg9vN^bmo>Dk+%g%fVyro~Q8iyKi8M*B`!$IvN~0^X39v$`Td_R4)3m3o7@KIXPq&>km$s#{89B17^t@)R9-yu$p|#PmH82Y(Rks$fz9&AYj;s$X
<fDm@qozJFx-p3=rK?q{z^nAC=pNHF)-jc}O}gwkoHksb9VS<!Qy>5U008lk006J8003+z3akjM00000+8$9UIHbw60051m0jR|&=rHmlMWW%M^REB^?w?j3b0*0w6eLw4-Kf2$002uJ4kbA%*rK+bq8}5X`knv)UlPKjF(9m*cA|41Dyy)sBBll|nKb|a(l6Jo_9`(UC#49Z#igYx003q#&Yg!G;h_x-&!aykO{4$-Ln?141|n*x^PnQA`J<Mo007-65S$4jGN`C4QyNMt007Rb_pPa|HJ`qq000XnKCFW!008og>nJ3o0050HgDMxQ00536fF)w2R3HEV001~5000KA003+abF99p00000v>s|DN+%Aq00390lc<R$+c5wq)}<(;vakREm>LKs04y9XgeJrw92_aA005(#^c@|ZK^$9|Nh1B2yr%#FE}(6pYavx6wWXRO@E>xmB%_%u+%y0H?JyUq6r>oZ5GDPa^re}l004n0XCN@8dkqB|0;sJhL6raiW-e)(0Vo+Eillt17bmQx002@faij^NGAO7k_Ma3j004fjC7~9sbc>=U007S^;H(;_0018$XDAn}003tyJ1v;3003MiA*MK^a3cT!003W@008x_003nyC!#hV00000X&V)o*ep=B002d&L8`+h-!k|hf}{^AKd}G+lB72woh;HWts|1BVy0-Q0069`VI|@#+#DC3nww3bsh|J=k0j`%d?%8j8lj7rm8I0Ll&GsMKQ;gWIx+dI2Pge11fA5Rw5X1x008qY#h}?J_a>kvOC9YVb{_x$&8UGU<)pr*k(k+}cACPY007~vksQt|_bsWdv8kLa004e2B_S5BpP?rx004X>lBsH_008Kn4=oR=0049<CoWj4008VRc(q`>EHw<Y3A>jxj<VvkojCtBjy0UIOQg;{{<M#_%d)klqcUo@xHm*JJ2@M+Lo>iUT(yO@O|=KCyETX!->hw`3_8EC5Hb@lQ!uDBwl>hNZ8UN*c05(M
tusfcv##JRsjVcbMKg=45VC8jzc6biot)$(Su!88Hz_lz2rH8$ZYgc3T&Pnqu%ZSm<tQ?<8>NyvXtcz*mNhY*QJI4&DWU)X008R)007b}002!XnkH2m00000xFZ0lk*=aK008=>o23IN(6|Dk^{wTlc_#n>>m%zZKcAv0e<FjYEuBCa000{zGM$d2$SGeasj3*BsU83TC5u;=I;bll{3K;7eXVb=?JqjCf;RvF&8*TZ%p&Wom#zCP1gsY;000B1x+2OT*rs8v7A-cU(t-d0;3XC$iWj}3ysgtIZxjb50000epeA;x(4w+3Pb`+7006qFuck&Z9GROe004)r`=X35000N8CL!3R005~cj4+oV002uRcc|+oWuyQA003E@001;A005^bFDWe~000001{~<3TqOT7004NW7^4O#^|+X&DX6BXK^y=8ilCDvk{wYip`UN1HmQ`P0054pX`|Peo-1D?SE5#i9v%PySCRZ8lOcvAk0l%)?yv2y=P$gmb2k70ZmH}mXe3mwkgZZK6t3$m003aD-zaJ#Je(P={3~OulqdiI)}s6*r6``NpRNBXwIp1X008VFn<jQB&?2%jDkxAS006L@RHsWYzNC5_003X8PbFS0003&DN+6P|007x1d@v~`001zc{wf0~MW_G(002>*006`y006M5!zwf@00000d?!IGjH$sf005pHu$s#q7PvO0FQ|B;kD&kni=3yRfhC|UW0ggv)u2+O004KS+o%&M5G&{#ZmQ^tQ6T^TIHW5fE+=3ifF$oOv8<l1qc02}DXstjXr>n@(<vOQMzCTm2dvAJ000rKD-)|Ax~PP%04<K69i{*PL#n<Zw<dL=Gp%(cf2Y5s004@j3>>;CuO>z@-J`vm006KYR3}R?Y9m^t0045QDJy_40023pgenCo003bu#4z0`005>m*erQEShO{><vzK%psgV#Tr&nR%P?BB8?Bo*S+Tb!)Tg8}tTg8-+Olpj7%&vBOeo+m+b;O3k+piY
VlI0r)~}{9H#A(dz^(7Bg)zvmU#_1v@-QQ#13tbaQnBEz)VXXib+MYMF1TheV5L{2xhr)q{4kOrT`@JNy{f>ble2szM4+v$Tr*Oy<+84+kS~R_vbZm<S*=1fUbQrr>n+?YvLXNg000gp002s)000y!F%}S{00000UnRvPL@H^e004TV9Vl9;<*Ozs4IiYeaH;?Rh9|ilh9}RgawG^Lj~IfU001T{mL3`@D60%1Nu{r)DX0Jd@F9|<4J<GuIHhJ00xaVwo~J#ca;*RWwWLWJWF_z>SuAO(D<s;f007`7_?fmSi7bpOJE}CD+>QVMexj+Sa3;4PQ6=iD7AZNL008W&2dW317@;Jlbe+v90028Erl^Ii6BzI;0031To~Wa!004Na@FdZ#0026zh9EPm008PC0w@eCHzxo9003hp007b+002`cM5lqL00000lbOUR3@J;c004obE+U+%Xss(Le<#BoeXIZgX(kh$$sw4gVjfecmMU4M007D+45qRuoTV$GC#b#`8L9vP2A_VX+9_5j{HUQLx1^vSZKtcNlP~}Ph9pm>t)xRMB`c$<ZXm9t0006f2&HJEMJ!Y*m8XazpC<qSM5K|X#~-9C*CZONk0;<5003*L<f#W87$GF4-<~lp000=R9wk?!A||LT004fdk*WZ#008i-1*Xle006hHJsgv&005zw{3@3$z9j$v000{#004Cq008JLlAtM{00000O(5BrW-Gg>007CR3Ln3uMXd}d;vfwqN3H+>u_WuIJ}cm-bd`vl^eC*Q000*$i=YQ8pQke#RhWpOPpAL@CM9mD0iS6g(4|8q5FWoI)usBVpe+CZai~_Pq!wT->M1m<<tp<e000i2nxvqpS1tQ0^sC~W)+qo0-Jg@FwIzQmDkA%=X`#j_006PA+NC?FrYME1)S28Z000;*9;H{Kq$LF^002)TjvfP@005|{ktbEH002L&QzSC0003<=L$R;9uRJO+xVb(&V7Rlb`8KJso;6Le*D%?w
L$#1A79c%1SUPR9xHuXu*f5<dEU^tVqBKS<#IC|RV=+CnxH$Z^sVPF7SgEZzK(Gk0A+U5m5j;RH#XKOkB{*Fr0xSz1swL|v1~tr|2CRrEX|GwY3b4eYdol{EqpWr$s;5q|%&BFd)i83f%PU<c)hdKEU9?j;?k>i*M7yjNyCq~Pa;X3S005|<006rw004F=p{$0i00000m>T#mAf#q2002uFOrR7coV1OiZXdp)r6vFXpd=<JYn!VjnGxY8<{9Cd007q{_aTlbZz$a+Dl1SVeIft=<e@tl^PUbAQVxExv#|ZGbS(X+Q8fSnl(3hea<Of#)2$vUMyRkS008u;m#WFE5UI?r(H`cj+a&-12&}xN2dqn}Vy+M*tE(6z001Nos--U$wkrE8c`2tL002=JJSv$kUL#K|008VNoGd>t007}CS}M^S001&7zboD*007h{au`1+xvT&H003y6003Mq001~EK&j5E00000i>7cch#pib000G?N~TdHx3t`#`=C9l+nxXbDHc(cJg5IE#-nB_Y8)u20000XW*Au_J1j06(<v$`lAQnmwV_*?)+5Fzq#K*CQ?Vhh&McOnWHbN(zOl-qQ?Qq<POpt7Ii>t8002a<_^Dp6Vz2J5wI!6KZ6*K!PN~6}lCS!yjIPHh@u3K%008Nye48(pwyOIprkr&n005CF2pm8!RvS(v002ZT6fHF*007V`lPX86005aQ+8>;w00876875sMTdDv6001qZ002lU004TdfU8ET0000045lD3V5mwh005>9-=K_`Otl=LS|<3UZ5RLmAEdmbP$Ks#;GI3ED5>G5000goDiF#fCM*FUxfqR-_MQL$E1HZYRu%v#j09J+X0o!d)h!LH^EChf_^;C+zp<0A{H_NofvHd@008E%@0oeBRIJFZv?q0=!k7R6H>EeC2C!1BMy{<Td#G`x004WW45Cq&JgS*4Ngs$J005Dw2%JDK9Vqag006uzhbv+#005Sp)+ZUO005mMGNi?z
003#WggX5_4L16%rM;LXC_FQ>(ybgKAFm-adm8UGb+-z#l&txx)3S6j5iNf+kgy*wH7#|qf1$%LO|xvXaI{|-(>x2bI<W1mt2MW~L^e9EH!KLMUa~^72_pqKrn!Ztu``=0=%ZGtNGPKt3@nz8w62V*V=g2i6fgiJy)cC)b0~MFRxtsiqAsm12`*5f!ZR`@Niu=3-nNT4&avjC*dcWv9w`6-006fO00503006lpZLBRS000003a2b5Xdxvh001p08zF|G@u<Zj{-d*|w4ML}tsSE$F{pkKd>|hpgsNyI003|$wkU0;;;Azzswu*%aiahL&z}#WCacLG!JHK&j;F61#H&`Iu$}+_Bc)y(Y$fumcOVa^qmDYB001_qw<i`UA*ueKouuz70-yi@ha{sWZ6vs;bEI>phN9sj004p{VI8iiD5ue?+ak3e005e*Tb}f(PpVfe008W(K&UsX004cjR;<;m007-3h7IYg005Gn7pH|GRw@7h006h0002Ls005+_SE{8X00000^{BckQYz{v002ED(;$7NV6FWie-x6TIi&yqc$~x~s-_Mk{~fBOWTDZa000ynha--rH=rOS>KF~FE1&=X;ut?2AD*a^ff+F<d8gzk8>O6>a+Ux9?W}$oAt|-0mmz+q;hoW;007FFvZlu*`=vvtMW>aS0ww?eA1KeH#~$V!kEkH3?U>%2004$9dLynWC@0aXXBrM9006S66Q3oj<eVWb005b*U8J_G008B$sH$qL008eHTqEVJ0011NFDx1)7AF7z001JI000@R006+CWvIR&00000P@+U11Ew&l008$FrXma^wk(+>eV_vzpqc;x$Rsu(X{TbK>?8~ux~AkH007@1JtQ8ix~F$0t|kJiKA->qG#n73f2@U|4IFV6F`Y{%x2edi@wos1(v$@%!6gC}Xr&{j0-+|b006R_D=9Lb6QZr9hpO=*sign_G$0)%9UU;JxScJlmL@PE003|;E}WVwTORbOldEo_006Qm
6CWk2ew_{=007LQ%Bhm80048YgR0`G001T_r4*m7007-Kxi|Q@0Iid`pt$8Yd%JM1MW%?f#53)(sWNr19x#M0*D?P*!@3l*2sPs+;xM!&P_9=wo3(MT(=b{+q@pynYXM`fH8d!(B`|U-+Al4tDkKR%th@^;mpo6j!z&P~%e28V46|6Gsy8*4Y%Fgk0IUnKE-rGT!4!0;6Q<fJJulX=o-TzdIje!LimtY)7p9A~aIg%z2Q>{ihqa6(QX``rx~2dC005?>007x2007UdiKZJR00000ho+4#k|hZ-006TV5hu}|6Sl{t?<8y=J*fZy$)D$@l%ZZOjiqfWIUd`f003o}-=GK{^C<fut0&zkPNV<;52b;cGbgDev!3g+S1(Gbfhl~Y>ns2OEU+^t7qN*iL#&=DPO4QO002a)2bmGC;VrSO(j2Cw=qdmJZmZ>@A+1y`v8*yEpsP2W004O+3MKX`j3*Z;H6GNK008WxeW_F{fT1}f007W1F(qv#007o6+$w0I001H?3MKEO000si<)VirL8brz006?F005jQ001(nKB|B#00000BcS*#jv<>c00561t|q@DXSRN#btuuQQ5FCI(;P3M+p20QW}q3G^$vub003to7l~7-rYG<p#3_3yb{qfz2c!cWrK>atw;m(1NH1upmMSKw;xYgL%dZG21hJ?pkE{|XlB=aG005z=rXtg=Z!Ep6e5G8TX{P`H(IGjcnXrK<?ySBe86<z7004I<x{3CxjHeeUe;}Qj001Yfw3*c{T%xO*000~>Lm`Ny007c1-YeXu000Rejwmdt0032`Ssaog-KhWo008@?002Xs005{TWU7@c000009g_7g5ffrC003eV#v%$7@wMlr6)2P`wWk08AEltBf}Yqd2BzaIYbbJ^007&X3#1<)|0c1U<tP*^0hIs%e5jNgo2W=2tfUyT9Wt}4Qzs6hB`5#@p0JuCr?Am3$E=PdWU5Xn003jCP@V^};4kW|g&TRQUY!5{zblof
m#YRY_pCH2cB$i+0071%e4^|leJNBcsU|2O001W}v>DYcP92pP005UUP%i!|008eV?=PpP002QN)F!&2004}%mN8v9sW}w4aXlA2Av8`j7B@OH%dk^1@}^j}9xiV!TeGCD54D`G39vddY$&)e+NjXD-Z?U`#<*{=7`6K{W4MGjXS5<QgE4lsXD`Sl(zUat(6bM-N-NH<ZnXNX?lw{%nXWgg2`FT&Yby1vqpL?Ijj<xD#4NC}%&V#)>Z}{G9I79vIVi6c;xvaS{xTx8u`u*G%{fv!>$?aZUMmAD*d71?0031W0069`006`tI}qF{00000(xkj8i>+9u003d6=O{oZ1gSQhsipp`l&$~(fvQ=nH7i#lf2lyINv_eU00569kQ=BeE+$Nx3L(F+3MBvl%O=q#mni|EIVV)DJE)_SXR0Et+p+)v;~#-2qNU-hS}0enCMpfB005&cVJDEM+Z+=oh@~eT947z(d7^Y9f2Fjl_Z4KPIVAu9002HIZl-o4dLjmzc%XtL006lor==dOI-bOx001Ja>#PQ?005e=@3Eh(001(nsj(cW00847fuEZw8Yln&004O;001Z=005vRHlj@+000003#(@&`>f^|006Y0Xe6+t?yLhGx2XlK?yUd-{HL_1Af$gC?x;$p&8dQ^008fcAE*2%+a=1P2dw|BB%lBQUZrNF9w>k#-J!-G$gvk0HmHoMOR4|>l%ge}TdDf59Vk_+e4i4k006}+6`-V~=dOY%g`_p6oTvZ*ETVTNH>h8!TO*mAf-3+3000vwl9P6$dZGrJ0i8c6006+GUk58Cq9ZY^002O*CaNT?005-0da+%t003R4zq6C1002)V@u2xAe;@z=006ok003#A008(Q=^}`v00000V5Xs{*{pz`005kp+bDY|S*@o7QJ?Xw|EmB1<)^l$HzjYG4yfWK^{s8G005_#h^Fu?!zP-d;-o6D2_66dFeVtJ7%iL^<{;Ck<(=!D^QumuA}{~|pd=tANU1xk
?<K#gIH!`L002iGm>^!Nw4in;)u~*WEF}N{0wuO6sjNVyYn%nBc_}?2001f~N+P+VrzIY&5~UF+006)wUxzCtgBd-c007{qfUMB000366DzQ4Q007~pA+uks000g$f3aq|B|KKU(K>NF%sD@;3^#H$+BIA*Y_VfDHmOS^y|9I~R3xOVVmke=Ff^bvS+VjptR<c(OEcs(t}_F-=Q#>1!7w;5^sFyA7_f6Qwy4uANI2o7G`z7i<GKN=7&v6BP$@Abax{ajKCw0?wyMjoBeCQw9WGL`{jf|ZIVy@S7_AsB=dRQ(o3J{dmZd{5tgr2|oV7x`WjN25)FwZaJFEZz003H)006Ko001<rawe=K00000P$D0!E*eWM005XIm!@_oo~}Qqx2l^U&MN=_(I#vm?I%MYz9aOUiK8JX001PSprkn>#3nEwOr~+3QYHWZK_k>5G#Ln)DJV0npr?W*7A(!I{HXu{I<9^uw5uwomzBvYtg0KS00086)uQ~D_Mvj6+bQd+wj}@nC#z;5cqa@Z-KD)N5SY><008SI4<ipKvM7x#xT2jV003U2S|iIY{Hmj+001khttDJ9006Koaw<|R007UP%_tKt008oyF)Z?!Q>y>~000vd003kv006wJ9-%KB00000&nL^FCna+$000mqNThNp<E)IPzO0X_YAgT%q$Ry17ot9;ZW%nNYL>z%008=>z@zP;G9;uMQK-`x1S9|ea+Jj*@Fn&iI44INSsuG4pDUHCp|1b{#i{fvb|hY@e4{EWK&aiU00366$09c(>X=!jd?*5`->3ip!JQtU`y~LVbE6_EN1Icn004U-j3N)HvZ#$L&=|&_0043<YNRJEtEU5^001MawJUEe0049?S}x=*001s2f+`s=007OR|0R^4d8hyY003*4000ds007CYA)**400000S}2&R6{jgI008(Nx~bnMZ>`p+h^ecng)IO8+9qHpo~a6@Cy)N7k*Jd@000&trlDP!BBBzN2dPmIz9#?x
fD)-DLnf+-PbWz(=%QmGi7Q5|(4YVST&#E}5hMkve4YR+I;n-A000uMUYwF7`=~;sYAb{vcB=pY?WCHc-yZ9#G@Z>X$|ZcH0042JA(CDrTBFM@;-#La0045VY9uEu&z<I<005Gxaw;t^004b0YA$#y0003enk!8&008<nG_*LpIlAYymog!_Mmg*x{JeEAGA+WhdN@us5iGZ*?=xU9KqK#{|GYA>d#=&2wK?`II5j=3FE(+cb~o;{AHEy4f-oGkHaPM%%`3+;F)~^+;<p^I8#}tM|Fy%bkhtNYH!M^rC?}(-=dGTnFenfbYpu<x{jlI99keZ|fT|9wG^>o8F{QRH<0J$qH?TT0G$<6f7`q0%wlWKz60t6!sUH9U000!7003B+006$D7bgxX00000_Z^%eYb|;<001*BWiZX1*tsC4V4h^7bt(V=PNZU_nW~?v(WOi!RHtyD0032`H>ruKY$VgBB%o!ZOdS9KBcAn?v7QE`a-zATkR)d<+9xg^pS1u0$*40c&!q+>RG`l)JElOe004h1W+^(S1*^!Tu_QmBA0z+(-z_>LjvN}KzML{Ej3@vA002BJ#w4|>{~#PCkDv~y002IxV=c#`QXF@%000rBs-Wwq005~AJ|h4n003L02#vWQ005t-bgcrWq#^(S003vB000Xr006nG94wC@00000_@%caRW0{4001^AOfO6)BDvP7|0r;tl_CHDzM_kyA18<!Tc~IxSgHJ+003L1nWIf0zal24)gC*a0UQ7Ttez#6o+6>57NvltzAg7EJS!+F=Cl9+yrmN@g{mShv8cBwYamD^002uSrYO~(A|!C6XeKVJ)*t`?7cD^~CZaVRccrB!z@oJr007`7C7rb@|DYTt5}FsT002!TJT3Jd$_weQ008SH5vd*{001Z~MhrnB004F&$|)|W004xYez4A@z#aeq007XT000@D005w?eJkLq00000NTzNNpeo@s007V`7%-G0{<w3glcCC^@+SZQzo4C=
BdUF#=BJ;Yd!Q+)002p;%&8))6f4=N+nMJbeW3sVhN#D#m8_;5;ik2!v#2{K+9?$%RkQ#A+NCcnZm3D90I6Cgrzt%k008PLI4G2-O`Jriz$Id=k(B@dJS~i)<fYFa^Q31e!6Bn4004L}lpH=MW39)cuaZ8n002#;JgxN|)1(!x004@ocdcuv002iNwW_@(008r#c&Lye002C*+`J4u<~~U@V!i-7mn?p$F0lNtFs&9Y#jB;b^fel=fT@JIuRK~Tg{);VxUxjBsG#;Q60=OJd9t&)0XkALsjL91b*^qOQnC85Q!oxL0Wlw?=QI&7b~pB;1~AAhg01(kh%YD}b-0Tq1uojC9x&o8qop1wCovVT{x5{6$uFX$-!w8P38@#bXf|Li{xln{VJN}461-5j{<N~BD=n{`Y_R|U001DR002}a006t7YAlnW000006QQD~lOjbc000asJ1NJeIx90F5F!|;G$;T7;ijF5)0ZkMH>2sOfus*4000mp+Z_2FDk7X30-QCYKp+4BDw(|@(W5UPv>mD{!YGWPm?WVk;jsV!qon?%Zz>0;9;+J}fvpi60035_rk!vkgs9J}IUS*`U!nj29w-o^1{n>lsj32~p(tVs0048XC8sqgw5yOL1{LC+007FI&LM9dsvr?8002y+qNUC$001SgzAf^k001PTk|iG}001MQTcIePwy*#I007CH003et008=;gC}Jt00000*c7;_Z>YvC008SNyeLO0nkeWaW}{80jw%2EAEyW;Oem@+cb?6sG^%eT004EQ38ZBo1tN={gehI6M~DCbKcj{r38jA{02@#$_psccE)(M=^`rm*;2`g$cquoq;;O3}IH)}%008QpS0nYNE3Yr9?4iG_7%2b%H7$lChM@GU1gW5<KdUU5005O5wV*Yqv@4J#6`X@D00798d=-}{b)bYO006NdHyz(8006hG8ZA2@0028Bu_+!T006NV=Or4X*s%Zr003R1008zR008o-j3R<500000
wxQD_&aDkB002=cj49eFw=Fmx)ffh?^DF=WxucAuts=jgLLT-X?yVUp002&;<DW;NwiQjCHW!hsVxIs2zo4KW{UpC79|55))FWl1X(ALP8LR*RIG*yTGAr4nMyU%JVxO5N007G#QJ}pfj;xfa+oGANM27$Xfuh-w3#U1*MyTGV3a0=7001thF(1kt&Y^D|VUKn#0077oe3h3d*{0JQ000c1R;eQ>003sN5Gq(6001DEfGHj%004A1Kr-qyfU|}<k32QFqPyKFJh&0By)%xlC^T}pKRG%oC$pWmeY7$&qOwpdt~R=@QzYZM5;7*V<Ti`9X|I|ssJbdHE3Hv7uQScL0<Uo~8LKX{!!#W;o3<slKQnr(6)YPl#-x#>N4Cncc(II{6Rz#BcC@Oh6{BJ><FF^H7PJa2cC)LX!7p1gS|lN(i8QD*pDy{PJUb(>NIPJie5-t*jU@m8001(g007#r008AHm!!I&00000&Y+mCc&_!W005As6{oPL4<Zr^v5iBWY^wkO2cSZz5vTp16R3bCP^!PC00708n5BWJDw&?CfE*F3KcxTwGNufy=A%v_?5x0}T&_Q_ZL6s*IyC?QVJtbR&#R)Y9jG#@Sg4Pn000UtHlmWP_N{E5f~a{Ia47%)$|yP>4y*v2@*Q0xD<nmr0068d(51U4s3@YSq^98>000^*m70&QJfN<v004m}9j{2I000oI5v_`-005I8d!<9D000_`!=`AE044wc004a)007pj006ElK%hw_00000@~CaF+^piQ0000WZK0bS@+yrc`k@xA#H;`SbD&_TS)xv$Eu}uFf1{MC001VE{ihY6vn1=N#H-h;6QlqDcBkR0i=rzb6|1YR6s|w5VX8SQRWkqprYLW$x~#0P!=!<$Xr-qi003Dn^ry3`#jcYiCY&88a+?4EGaLCIajpHXHzA@IXCeRq007u5u_n8xsHmc-9H<(m004Ct@{LKNO&@Wp007yigs~Q@004ootg!2^000E56Rkk2
006HXSgs44+9&`3002P?004=t0009VYpNC|00000ejZh?wyGtq002=Wil7LXPblIf>#82A0jmH2zoj##Tbl=?hNT%JBdV39003wo%cCSF<R0syVyZKwAEy8S1f4{y!Y5)S!KsU^8=aD`jHy5^G&KMKRx1pu|Ex-``J~pW2&8T#005RPp`-$=_^E0kET+LA`zHVZz^51_f2&5UQzg46HYUBI004@pA}SiIl^Tz)V5it4004ED@(oF%+Mx)k000df+OM;y003>R*R2+;008Qz&#ZK;003gIhCAClM?HZ(rNBZwD!Ey-@;Q~aK{8RPwW8~$SF=gAc&sssfwrb5$~15&m@!tQQ!0csTs4R_(JH_o?y*m_MzS6<=q&{^zp+U<Zzf?K>9k?3La!3Cgtt#HwX{kquQw5}aIQ8ZhqQ&U)~rIT+%YRB@+syiNUNVHqcbO_#xrCxLNqTfJ2dCE$tkWSR~}utqBK}Dwl{gN$T=ybxF?kbJtP1C001VD000iG002D{6sdVC00000h9&qUN+_DF003mDL8KEaKr!~D%q+*GErS36q@|4`o+vmfEuD-VNvHuP007`1p&-$iT&KV$*B1S#NGSjStsfC0C9U<SG$@;*#F5FJDH+qGhqM3yC?|d=cBG4(MXMwiFDU#Z002uMS}3Tc2cvhYC?)M17oz|G+a$#y%BL)-L8|1bd8YNF005Dt))_{s?x%XDYLdC1000=O#-DR5k)~6r004Qck)~Lt006MAkgOdd003>L;H+1q002dxfSj_WS||Vj007dM0085t001$fzNf_~00000DX5DiRh`Kq004xk1gBS}M=V9CIWV-T#v}j$Vy7!9OQmNl4x(YAuC0+E004%hU7T*EdZbq&DJB%H5F`KqBcw*0%B-lN2OtZlmL`#%{vAUe!K?rPMW%u#YY4F&3#!5-$0+!j007CUW)qK~(4XC_6r^J&86f}wR+BXz9j4x@fvbP5psY(B006|LXr)Ff?k9StQJ{mR000IU
Ua3ta3LfyM0070Pf2l&K003~T_pPs_002!O6R+{8003>Gf2c?y7bgG!000ai008zQ008ZzailmU000005T&~)N*Q}7002*<wWBR0rYWPNRwuxw$0z^*D5d2nyBmxv*d#|Mx~eiJ007=4z#gxq{-qNsS)3WE7AXJ#>K;)ikf`3MK`B0?O{vtT*dB?Z;UoY628n?sh8HIva;cad@hCkj006BfNFvJ-mm|@uP8Luo`Jey*Ask>N52!n)9jZ31U#JbF007e%N2eGn#vgMlwxS{=000J<UMWo^xFLg}007RV1*`a#008x^3anMA002#yDXeL#0023thB)B5F|-0hEx_qJtGXq!J1n@iPqTZmN+%Py7B`l$>M;j5Ll{D^s5QB*>8)xF$*y;_V>qiNCA6M2xh!lnZmHX^D6ZWtS)pvZQ8q#~hpr4Vg}KWuOgJI6Ml;bPiMY(IY&FFu;J0tA>M3KYd@#Q-Iy7aZ8ZgW>87>*3-mNk;g({SY`z04OH#A}|btAa24kDAg39{d|_B&yqXC9rbeI@_^0017L003{O003v4M5;q200000*rcE-hN!0{0032`@)*pZ6)aSo@F^lL)F=P|L8?Whgd{X4!l)#uFsLZ20073NeXKGjb|}@Vy{tDO!K?rP7O3N_u`1Cksja4*k17DAp&}-xLMQ+L7oR+sPMiWGA*=@^sw8+Q0001@{G|LNJFB0k7AXCshob-hBq$OYN~ahpGo%ZsiKvjG001qKPn626v#Xk@qAFCW006eE_p3xH)+9J9002s=RH{84006Kj?yJY70037i)1<ki006Hbh^Zy6?H~XE003&G003&P006$D2c}mm00000Ca4B12_p0;002v<xSVLHl_`Fs&nl2C<S76EYNjZlA0qrF6R41@_Nc|F000!A5UCm;OekNeKd%rY#H;`S52k^w_o*K$Vy+@7@+dMK&mk9{Zz})*v?+5Pz9y<9kg8!LFrXHt005YrV500MF{hlUik4>~dME$@{G&vq
Ev<1Z1FQC{U?fz5000r8zO2eDvn!gYTO6+)00706+Lu(KK_!JL000B2=b2Wh001T>KBW_>0050Cb1My{0031HK_Q;3u_gcj007sV001(p008Nq45h^;00000bgA?#uA&4g007>mD5R~UohZ_uMHG%GBP;*_+NgUH`K3B34y9S81F6-h004uf4XiS(Lnj=o+pYyEajXCU%&2Uv%%JWmWv+0cjxDI9EGQMDs3ZUYXDBZpEf@POp{boD&ls;M003SnEfZ=PL!ox7H6-sQ29f{(NFdytm8oed+^QO^hOSei004ZdNT{|f_bWsx1to_f006}m+80!!{wQLj003L1_!=6e001>692uFW003wype-4t005=3dNDsdUbCgRA+!&;{IeV{gStR9YcLfs&@6sC9=7E&O1ECLB(nuFm$uP10kM~?Q!SaSbu_@NgRQ7BH?&YJ`MZNPeYBx1b~bT5{kBQ2ptpS}VYSt*!Z^n<Mz!gwb2xvhvb4#r8Z_fCf3y*-Wv=6)Wv+QBJgeX>xiluOv9OCTsWPacK`#p`J~e9~g)bT_Ypq+n*Q6%5Lp(Gi466UAxFP@m005bv004KW008lwR2Igd00000xUO_7udKi*007*j0+|9JU9Kyrm#aOg<e>ln8XTUWe<``9_n8>0dZ^!;001-_lZ6%?)ssb_GN${Rc&q>bQ>7ZN`XA9Nb*`ZvimObgeXM_}3?TpjPNE|oMjThCIUpdd+NJcY002`RE+&_q@+cFW0jZ{^TNnTUoT;-Vm?;Do!l8kt@}vL&006S7QzDipyDD&|V5qLC0003j7pM)b%c=OO004NbJ}OwR001+u^qx_%007LQL#sEf006=udZ<*Yi6;O6007&c004uh006+KjHzRx00000i?5<75U%Pe002szJ0BsV0k1x!5UocXPN)C?L7zLPo*Z|av8Al1UM4ZB005Mu-lRD#IVoSKm8df+Z>j(QoTjj;ZZA<Pm#beZM6W-kF0G%Zfvo@lE1Ynqt0NAj
gb$^y9IY9w004<9!keBZsHjF9r>UH!uA~3}XshQWEF_I7K%Muc$QA$q000*!Pa>A5yQ*-eZKrsk002rRE2aIYQL6>6007po-x|uW008E%3aDAJ000#wO0Maw006P19i-!{wHyEd007yg0014Q003d6bf!%V00000Zm;Q|EvUdK001<b@*p#%ajj9M9j*<a&z=APrzG&5VysM|Yn=zF;UIIR0031X0;kcZL?d{gORBmVHLL&tE2M(0I;kWpwXM4)AgDL1KCRUuu`U1r=&KQ(wjMUCi>TzS4y<UT004WZAEkn+Rj4bZ0j&e0p)3FZ-zDNC=OIv{6rT;Ihbj0V008lxn<W9P7bp#_i>I2Y002s+D<%D?cBmb#008l@JS>u~001|z@gPsJ003>K9IbD#003FC$umDP?=&>H8m<sLwk=YkwL0jot+6GZxi+e;xU-@%1v4+NzONB2vN>X}EU(@(?=*BZv$%z_J2I>+Oe{Y%%p@qSPc8(iS(b@7E;AT2rlv)%X)d9(PQP+BZL*9iu{1xiOeaw)1TjUdDJ&GEOfA<i(JUUOQlx7kD>6=@l&d%{L@Ums2C=F%?KaUWJei2KVKiJTXRW_EOS}&hP%Jj4mL~uJ008G6001qi007dg)Fx{o00000I3c(wj4W`m0017Gmmh1R`7w1NBN=Ha^r-*<<));m%&PLDC8ISP3?+7=005(-@})+oN1>youoit5u9^S<bsYAdYN8IGn;a~xBOgX9>8pvYq_+S7#h8OCk)=%@gDJYFdYf0S008MJ)hVf{DIl&Uv#2DWPbdHYsihkt1sWA5g(;jJxhS2V003?*H6YL-`l_*@E)Xan0018+IHKgAmLWo<003N{_A0on000%CZ5(YO007UdDk!cV002cELMU^dS1JGi002KF006M1000xMXBlaz00000o*j=NDJq$-001T-$RkB7uP`Pj<{bqqE~o$i)TIlim>CSA-<?evKq-J6003^Fsh}^QOQ<iKt))6E3M2pk
;Uk-%&7<w7%OEkCHzJ8Ff1>59xwZfR8L6TyxS=B{!Y2lzeWkdp000&gB`HX&ASj|9^QcRt$fE!NXD3G{1*nB3cO@nvr5{?I006=zgCNkM`YN%Y_#*!)006ckavqedeIPR{002U$G%gLP003a7btwcZ006kF$0>560074yEF~JIFe(55003Gb001MT001TkMWP_9000004xvk?#VVb#005AeT$@-Q%`q_}L7zw{7N`IKHm6jYN~vq8I-}7cJt*U(0051qE2!0}E|vqQ#~c$IilP7jD4g`C6RB;MVWTaox*}ODSga?m8?FEVPZS&}YN>`H0xRsPZz$2F0050BfFpRSHXTtaC8^AwBBuZV4JqZFnWT>>jVRrl0Hl_r002%ZXCNP^I3nbq^e6Et006e4a-Ni|bt14K001bYIVDT3001-*HK%_W008N#kRd%B005D>={Dmy=c_$ESH65QsJVZ&A2d-kA27P7bR=pzLb#u>g|gc=x3M&`RW(5~%?Z$~L9{}(FDq&=>oe~&4zxVA7O~5xt~9YN%dUka;4!u{V5`tN->_b*UaFcZh&327PqFi@$F(@4Nw(xMog$+x$E>X)n>67ny{sB7=r9@|g{4X`+%MEAva^gaJFKQF3@ALOnK5yi!@9`3oH+}rS|g~gFB|{>008Ty007&l0011Oou`YW00000sF8&%3@Gp@001+c>6^bI_9+OWATJ*&hAsdA5+~Ctucp$ZwI@n2WF;jm003wl$RyvSKd6l;@GKpuX(s>x3n8B)c?vS7G$w^2m8K@5a3-gsv!MV0H6(&25T~}K53Cm}5~O!4006)w2@)-*e;?YY(kn0`5+?uvc_|s6`=xg(mz;JbI4IR60012*N1rJzZly@5?v59w002fKu%Bfnj3fY_000Iq9Vl|F002WS0j3--008qZ%PA`;004ca;3t-@pPv8#005Gw003gC0057!nyZo+00000&7oi^?V{Z#000l3t{lu4A}qb4`7A#yOfLWc=qSJ|
wWD#T3@I=$lO8uL006BeF({p)Gpds+=oXuz%qRc=2qfW~l&O@c3?`8wwJJ8D10=MZ3$OqHf~eUjuBV|TajYCFt(`!m006L_m>}Du94AAmk113oJt+VHteqmD@2iw5uA^%s%q%>c008fw`<yAQZY4;lG^iV$000K2s3H2J1t7Ml003r~oGg!^0077&=pr&L006Qr2PFU>0035?ji&#u(jfo<000D~004uh004EX1*sgU00000f2rm!X(nDR00253z?g@oMVvjLCMwz`f-V36lp%~O5~lQ^r6>e1o0^L&008?S-6^V)G^ji%3@qv^$0h&(vy*5fPAo&Cqa|pmKO}&pHYn7eke~nnP9*>-b*QZ;2CE}01*pL)001~AJR=~e!6=!mz9{9N0wn+dv8a0$w5zQk`KYZFqbGEz006n6W~4@=upeb6KA>SD000IisG<6!6B{xn000UqH!0Gl008PL_N3-1003z&6DFM^005;Rm$wJD_%yV?5j-tB53z7F$1^`Li?gU4fuagG_qe09$+OF`@}<!&kU5;O6R-5Np(`9XPO{FdL8W&v0w~)sAF~0cfVAbbC@GpQ(>IVU#<5Z{5SmG`yR(clWiHIFpSZgk;IU(<>@g;>1+f4lqqv~3uCxRyJ+x)7*dQAw<E(P0HLoEn4y_?Cqp)ixA2OgJ@Tz__**01@0<+;B>mH)2{-^)|000rC003E+002It{v~)Q00000R;H93*{JWV000uF)25B4;HjIWxUTE06|MjPoSbB-a~mu!$(eDffGjN_006@wlOK+y+oR7RR;lwPE201ZOrsJdqnl<R6_yn%Z!B%36{+g1G^hXoC?{+twk!T8)gmFEFs=3?007x0Q6{%44=FDNm8B{mvL^rlt|-nUyeT@TAFF7lJ|u*R006Nq!JZ2$vL;z9B_H1?001c}!y*M86DuGn001B=Etqko004q1cPt?%000gryQ)^7005^Vr<H%JIjR5v0017P003yA006M6^`bB(00000
z^V724XZq_003C3lB<X$>n2a8JFo7mjjsRzWuQW+IVi<0;Gs{Y6)S`x001`}<0sprA)<31w52m4x1|68&!c3d%?dgqil^8p|0uquJg7;bxv>BMsw`g}v81IW*QUg&$E?by003Pkry*t~(kDr!3z?Rvey9KdfSi0MD3(E|C#>+Erl_7D000gg_@oP}vZh%q%pHCw0054l^(!l`yC}RY006+Ge<sna002WO8ZHG7004$01gA`@005FI`X&sj0i^%{001SI003Mi003K~ubs-H00000T&SF+fTt(00073Ik*PGH;wCnn?5l~Go~{4@zaXrc8Knp<n;na-<tdK`0026bnkf>ZQyJbPYOPc#uc80|dZZH-2dmj20;RDjtSo|{N}xw2T`>RvVJdng6(?6IA1E}WzNu#=002`fNhK^PGpU0qsT_TyM<@UQpDd9hCM&?C{Ga3^Kc`!y006|L?j$Ly!=eQpNhSm(00534^{Xqc2PJhT008%%OQqSW000Oqs4TE2003PkudVMQ007OduQ~lTK(Tp1roJ{Y2|OIG^{@K1z&Few_AFVq$gsvWS2Z26b2YHF+B6@s0XXxnBP^ge1h5{k*d3R*hP8C6?lgQe+cJ<U?kZ?GKd}C@-LJv8KD(c$EITx|l{kMdmojTDrnk7MO}CFT)3EU>#W95=@+lfDJTWk^peWL*C#B*gvn?kn60BsjL9Du|Q88?>-ZoM-WIB{IjyLY6qasbEY9s&v005Vx00611003we+a!D}00000U??q{E0Ov!003wmA|aO}B(mM0tEoYu<tzXI|D)z6?;-D{9Hc9zDJ<YA005Ge^rsIjj;I@(maqaJt(X7+l%?;Wge&zOHlyz<f2Kp7P%M2Q4H*CcGNnZ*TPmTez^ZdBk{!$^0026f2_|(W@~6|N@GO6y*r5Obl_n-7Hz;f$EvF13^q>+X000D=m?ZurUnD-ISsgN{001kWM5tLRMiqgl006nG5Gd3i006|RQ>TO_004C&Kra*=
005<=6Ox9d87BY$004QS008%@006$ICK>Z700000A1FJc=B6((006`ovmMf#cCnbEppi-|fhzz2Fr3CC0jO>xXrLseW|TE2002ysG^pR9)1~R0Kd%Co>>~gG$ryDa(;@+)j~;m|f~sVu(<>~UGBE%EKc`(NB`C9}tg84cov0@#002*=l_ppxd7+D?fGcz)x+nkur={{94kFQ~x}4u4${^pE003jF*ChU=U!*>z=cO(s002U%fU9OGBp_?5004EZ37J+L000=Pk*vO)001|kqAU$4004HRf~D4`EhYc}007^k007OV0094}6CDCB00000Y#|{d;HYIW0045H;u-9u@UUs5F%X$4$t(Z>-xn(;kC0NNrkH}IxGY8|001<i1*w;$6s9<xdaSf6q#XbNSfuA3ohM@;<C)nh0IBDt9W0U|ZZH4<Y7w+3Z7N(HPpgwHm#TFv005dOG$_6)L#PF;-z%vmS04ZXEu#`73Mmng?y7^M8LUO5000r3?i(u~L?~G*0ip>c002TMfGcJwH5q!K006kD9xYk{007ge@rYrj0003e=`f6s003#Mqp5>9Y&nm-;XMU92)ZpZ8ad-9g*ACI@-L&LF}8CrStF!2d$%|!NVYOH6DyOlIxsLf)3>Ry{wzN^O|!N!M>63uWi%DBTeR^qVz^GOFEA%HK)5Nds;y+CzBB--O|}rIbFR0om$<aBkGB|=h^e+9P%cfWH!UYIy08<erLTjmo3Ej)`>;_oYqqv5VWhUPX|yH03A{?R&A4bN<tq53br}Ev00818005gF006O>^eFu(00000yD7<~n-{^X003Mma-}gGmMRb^CaRvP@Tvd+w<icHoF%R)ASz`f1fe-A006)t+9w>Sv#eJm?4<+@PAC8XgdZ=NDXk=@MJUy*>8>s*Q=kbMi?ILzmZJb8Hl!!30xgiCN-7?v007%5pChWJHY4dQDxIY!V4?s3gsLT-;h`L#@hs>dr7R4m001*5dYvRE<)?urw4unN001<i
;U%gWUmRVm0014G{G=GD005<*2&LmJ001|wgstr>001zi3#@{wT%rH~001c+007k_005V!uqYEO00000;i2fL#wBEw000doo2Xu#TBzP4${s?j{igr`6)5y6i6nk0<0r|eV5Z_G001_klB7nfLaFJbMXI!=dY%9P_NEu539FeNZKHXnajiHe_oXK&uO<KhE}?9oAf_#z{3)BKOd)Ni005#W?;Jd+Tcidk8l5zwuc-h4hnjw=WTx<~L?~9B{U<!60018-T_Pl><tKqA;VB_3007jU=`U`j%%;Pv002Rvf-4rN004XvJ|^`l000E74XBnT004a-0jlt*N2UM(007RO000iD006@#V<^lg00000XePv={2}|N005?>q^k8D$t5zMLZ}g}E2;nht0Igh^_H3?y(7|_fUS5Z003nqSSBYb^{C($AFAZ0!6pCzr5{3_t|}L&S0zoMhpNRZuB*wdAg}-cc_?<A?VgFJzAKfe9Ftlm000OD(V=pw#HJoB6R4J^SgZg54;=ZZw5gP)Lo0?MizEJ}005jOc^xz&;iak>v?2c}007h;>9203tEro(002ZF@2qp9001wSN31m~006YCRjyVj008}~n7t6Vfi`Hr6*NJ-{VM6O7coS&(ynx;J05*HF)j|TtT696B{$J3inF?|_OkXZb}dY$xVFqP!!;haa5~N@U$&mEsi+|()HE`%Vk-=&k2LPA3OU%Xbvc(YA1?x_5w8I;C$*w1qBc~h`>LiY*r~TD#4U9ws;w8Vxv2mqG_V36?JEc@IHq_h39|m1L$N3`$1c%3?6%o4K|DO7swQBd7_a~U002vy003?+004-tT^+e500000y&>5t)T=5e007M<oFn}ww5VI9gQqv8t1AEi&?NOD%O*FZ>mPTj9xAIT005IE!zhv^Z=paZ%c)Eu>L>sJ#UB?U-lssQFeDc+?WrH0^(w2a1}Ojlh^Y)IV=jfPFQ-^5S)u7C002d){~~)VMXe&HLnz)D&n^G}oTM=&
D=QhTJf<xr1}ga^007CX?IpM*Ar&YnULuqw001;A?4}ne2AcN^003{O{Ul{9004z7A0{g*0012<k*LBh000LbY@Qsa+^_%u004EO007r2003#K>LEHJ00000MJ0zP#;aQ>002&%<rrwDBB-~jI<AYM<0}9Fm7lR7X{k=AL870nX`~h?004)mi6%}WUz2tiET~Q)*d_n~zMx(pq8`PiZ5WI!jH^~ByeYP#ZmR$QD<sG$04->#3!`@{|De?=005dO(j^%z#;GHoTOgjNW2688dMxN5Pb)#I>7(W%7$U2e003Alz#+J#A(bd7?i^&M006Zv5Ue06ilxD#006$IAu19t006EmZ7f+X002}hlAl#B001DIt`?1|%CG<c008Tx000^+0088#QmQB^00000D4}eormgKL000pf?-rdToT!ef&!{AkGbsQ7m!LtW+@^Jx^`z0P!X~RJ008NrOCV6C%A`z_nypeM>L36BwxY$D;U+>E<Ds!FT&nAuQYpcx>$3m=6Q}AX04y@3#i;HmF{bh$0017MNhoD1A*)2D4k()$2_OIfc_R!Yi7784$)~j!b0zbl007aX%Z)Uv>?Ri|?W1TU006bF5G)`m8I``I007&s8ZY%L000T1!Y#}!008VERith#008f|_ckP~$u(9#A;8PAnzbgjeJmxkS0(_i)UYZ!hqQRMEVPih=qenb2f3fPoGLvse5n+;11q^L-!mbw3OA7_m$ZJhcdDzf9Wo{|JFYgYlrs&oQaC~^>ab5R6}W6FXss+V(Y5NHMzm`&95hlavNH80KBy-uHmmljJ~UY+cPPLxdN4mF`!0$sfhY<kPB1?;PB-E@IlE9i->l-ISSDMm;vWD2005<>0085m002%U5-B?*00000hpcod4JREV001(htB@}xu_tAtOe_y9@GSrUNhDJ#(kP}RnV1l;6(t=d008_THz&I$x*+c*X_}TL&>a8(8zx~ZtSW*YoGHF3wWzkJohX*7Au#{|?j*IKDkkBm
a;&E%{UvoQ002}Q8Kvqd52;nElqW7KI4A%BmZov0LZHd42CyZpZltCl002a!uB;~@`Y1{di6|7M008nS(;!188HqY1005RK<EX@_002lNL8?Ki0009lktao{005bqNFo`o%Ax=O004!k001+r004uiOC}sB00000ajLs4#UxB5008o)Asxu4sU*{=TP!yyEGz&3?3?!`;Up9uSD|39JuLVt002cJOqm@h+NfzI(yGg%NFx9MkRmfF3#22V>Ljux<)EUdNg?wWs;&S4E~YP?%p6*!<*Ym@7bfzk002m*x}p*&;;TrkI3s-^up|HgzN|i_siBCgD6na(@Tm%*000iHOs*%O`lw0|nIdE)000#!#;Fgf^P1$P005z$y{O=$006+DS*kgz004O?Oel?|005?>fu{tnK%)Qv002^`001wo0011F5hh?M00000;;PgvPb=>y005q)mZqK?#U-n!e=1QW<}CmKdK&;IjG|W^xt(UNvLm!9004j(iWhaO8K@y1xuis;rX~OY>Lhb1qbszcu_wKmo|qw~aVVygSf~I1-kTGhDIj+yi>opyWF(L#003ka8lnyo$E5A71S7O4>=Xb1MX3{{Rj1#p6tJDF$)Si8002O(Xs`0A)1X5piWsb%000%K#wib}Y#f6k000@J8ldl_006Y8PNeIm005pP<}CcC0068x0I%h}c)G+gJf@dA_P9bW_L5>a5jC_f?y;;lF*dd>{<QwLn6-2(1TmC3CNi5KM<n7fEjSM=4!O6x&o|My_%;JN4k#`%g|>4!a<y%=*1E~MxipP49XtE7LbG(NfwX-v10}hw4Ky??#IC2S%QjCi6DAR?^Ds6mb}^VFb0zL05-dTUr88TgaxTv-TQnQ6fHx*J7`KT#3%Iu=7%0Rjn5X~%004)d007h~006`+s4O9@00000ZyxKX%bztc005<uzafAns<o1+8KL!}`V;^F92B&muPJ^l&X#^2g(=yd004~|nxj%C_9>hmji?hX{Fnd$
1t{|&VkVLzcOWA%ET|o-Whfn|JS+eJdaNm>WH95Y8LX)!<g4kb0029z)}-StgR1zf=pq8FSD^p^<fwzFzAP1^*saSVny5$>003^Ryr*3*3nY9lLMApU000&wxF8lVDIs_y002`dC9H!l004t7Rw(7H006TsnJ!fy006Kh{UF&WYo-7I008%(0043(001yAMkmgx00000tR%0gy(&5|008Ntk)+%l*R@8eVXW*WhYA1yiX;@BWhp5v1s*P{B`A%a005et38Y;n#3?YKcC5!KuAu+`?E?X!jvkCA3LEG!%&h{bIw>KnjxGQIP^{_}bTDtKXseVcny9!a0045TT%vF+x~UMX$s+8g9jE{RwWcAc_$yYcrL37Bdn7xd008l*7^YpW3#5E5NF$Lc005$`XsA9gNtD5*007jgsjav!001~7tR;7>000~*5HK|;004y|385`0fS~{Y004QN003bv006}&6E63q000007bFiP4I6GH0011P^Q8AHF0>!0BA&3O>mC3AOCF$^*d_2QU7VtwU?iNR0046&N~e7%_bG0m(yVkUqNo4>c#@+LsVvwlGnuV1bgiVVk}Jcm(KG-6qpK06K{0r$AF6>V7O&Q&0079X=cgwxU8E7K!6j;}6{G+FU#=9XC@u`6`>S>+{-~iO002FxRHYWAxS$p=k|RMS005#bXed4~JS5vD001^3<g1Y{001g3Kq|kj007x8b}zY=002m{#k^lMWiW&@;yoWX8N5ri0JMIzj4RQsqcb+N2P@$<BsYIIl{^HokFFRsYpF}IeI6f|$~RQ22_}d)nmgmQ=D4@2*DWb9Be3+ak2SI@tusuqc|3?CUo0gyWVJS`HMB=8v!r|?Wv`m8t|t|!gR@PrO0<BVITxz460i>}L$KMfEwqOyuBh3s1+@(@PA@^Qy*Cj#%Q<E^_O()^jwCLkM6Lh;002Xv005jU004`uOeoN(00000(j3*I@hnF#003njD=Ao<?5#tnU#YyNpC<qS8Iyda
c9y>+u#|eMFd5$>005qq`yb7ujVOQ{N2=7O(;EN)W1c&lf2<TDH<yqk5~$fMl_g82#IXPX(5LAqIi#(w1t?4;NvSlf006nCP$vJR!>tY?BP5BRCo2E|+#(#4MjQpMmLOFs|0L5W0073R3M5M$(x+Q00Hi%8007Y_b15+#)*`Q*006M8V4zJZ000-K#*U;c002~_(WU+@002p+a3GVRrL6z}002y(007D?001qkyde^)00000oFc}k)GKT)004p^Rw*AS5U)?DBc~{ieJB6`6P!DyzNqgDyr40owWZl5008)-7@S(BJ|=t*C@T9Ift&yUBbXl)L83_|>>gsM&Z}H1!X`?o0j&T4OQPZ`)2iUC9w3+?MkF;T008HvN+Bkwma9%8&Lt`y%_sl>&?p@tnV>$V&>$@;v>-ww001W>%OFdf(kELg#-I%t005gLYAI+1n;d_r004TaF(Z;L000CPCMGy80074&kR`+{007gRCLYtJTde>9008x+003ku001Yd9w*<a00000E06`L3MoA=000;w*(mv_Rjw(itfvL0pD6$UZXy+?IjVsjH4zx745eNt0032*&zy6sKq8->ZyWii^cDaB6dTbW%c*T6(49miudm7~U?LQyL$d$?gdLwGMy33$?jgq}<|rws002&?YaxmtJE{{VuPb0A-l6~iGbt6N{-GVKmL29QG92qD003YiHzm=ibEz>LH=QXS005h$YN==iE~7i1001Yc1DpLU003^HVU2h$004d^@2SHq006Z$X{|CmT)+LSbUh^{+B@eluC;<LDlG9ZoEro#2c?fNF0&-9QY<qv4Y*C9sIghB2sHt>#jt-WZM8-;*R#PcO14rle6pM}=^}fjp(5I>da+70C9{q)g*;8GY&L=**PnT*vnai$Dmd7$S+c{h#VKqmptOvz=`EJEt+ZpZ-l@I0!?@?Dr7qjMrL;b&!ZLNd+Au_{&$|A$-#Td}NvFOoyD0zw004R-0059D002iMGM{B300000
W2}QDlOW40006#~6)D81oUphcE+j~uBc=cVgrq&Ctejt@*{L%pwWuel008`@ETw{`Ig%8sMJzZRHK70iqYXZ)=Asp(d#p;LuAT>`v!)iQjWGZK4y#uwBc8k@dm!Petf(5N006liEhuyx5*X?wTBw1k1FQf5h8BDzI-v+Ct0b5qS*<3c005$^J}6j=rK1d`@209N000^#XDKbKm?V>>001Q?k|#8=003sJOQlJ&004!ij;UF&002EB7Ax*7mni@M004C%000Xn0009h)*|_x000008LiACv?co|008!&>Lr1vm#tnSV4ZOw+o=EmD4!sw>8Mng(y8tvJ|Uf~000E0Po@ndQ7C(?>nZRlOQ!$;N})untD<Wm@T`NOkf*Jt<f*l(11|soEuS5v)S*3}{~~^?^{fS^002xPj3p}~V;;jMuB5E2eV_mU)2$OH`JlZd3nacEIHF!4005n;DkfM9r6UZb#i*sD006a~lq{Vq`X$b(004xfVxv#8007Rdf2-KC001Yb*sMIR002-ZfU4;&o+bbQ006xu000Oo006ZnPNUMP00000CaLBg*d^&I005t(=PC@SX0YfaCMYDO(4+tW#hf6d86`NV!lpeery*;q005JvnyJdGt02v)A}n7b4W$48eWa7B(5$1Jr>ujc)*ZZ{z@=-aPA&id->F+BEvO2p3MW0O8>+P*0093T&>r2VjH0C~b*QYTjVJ&B)Ef0CKcROcQz*t3ho=uC007b<D=8YKXQ?f#8XC%?006Zfl&qaACm}~H007-7bf_e-006?JOst!+006h3=dOIN006GEC_4HzVzj)n;JgsHU_5=X;k5&-Y#=K#$FjyTk16jad9CF(NUoMI(6>M_>9or%gt4EiH7i!JS+b$DHZZ;~ExMGjg|JXCqMh2i6*d(y60kY8IyO!;@3B)jLcAm}{VFywXtq=-!M3X<yRrf*(;z6M>NL8Z_N>1!Gq8-LpD}4HfH<rk6eD3Y7__UZgeFU_nX-U7W-=!#
WxNcixTxEyl_CHD002Uv000~;006uk@G0J`00000W2j0V+$5(g008YI_aPo1i!Y|8yey(99xea?UZWZ(&=esj5v9?fGA7L>0073N+njK#8zHBp#2`i>*B}4@Mx;0-o}etB(U{(;fF<4}znQx$46y(JB%->g|Eq){DxfhZNFhzA007;gM5+6nSSnr}Whfn%8KM9Hs-y>^o~vXQYapYi#+^zh000ju=%+v^i4nMy{w0y3007(|;i$7Dq902l003PfUaj{g001GJ8>+z}002p=l%=61007-2hniZcf+YX|003;6001{F006cplcA!i00000=B5ZK!KmCR006QiBPkdj@-J|tdnx%5QZ4`hf}#^BCnScFD4U?27NH|4004fZ^`DR;T86m~CLp<>ARhn#8mJv6sHL+SyqOZELLgfulbY8mIJ5u&ULY-{hN!13Q>d37i=ot^005MtlAcbTP9%ICpeVnoL!kfwZ=~v+4z1IstdxzXxupOA007FUx2Zs=iIKRI{-?^R005sP@)!ptfh0_p004fh#Hs`;000G{W2HtT002X%EhSbd000FXU?4TB-z5M5002Lo000{)006b91Sm(W00000{i8`B&nK5F005UJD=6(Ib};6oNF}%;z$^d&Sf*N{D5xJQ_n#yhPNXp<0088qETPV-sUGO0Zm9>Pj2r*}9;x*w%&4LqBA~#Wm@e8Tub0&<Mz;U}SR_EGe;FJs)1-+Xhp6hR005pVAgA0RpC|jL(E>7^0I2`~$R}N<W3DuwMWlYH3MeHZ002Iv*{a;2;V82tc%)9K005t)@|XuDq$G}@002*{0IESK004!lF(LsY006|Rx+zjA004Bb3A&*;Z?u}d@I92f$U6Zvye^5d05HcgFRVtpBQ`d(ke)EGEGf;d+B8cv0x-xg5jFI+?<|e9WGOB+gCLtE2C-Kp*Q>)c6f#M@b(}%8)vs+hSFKqnJh$Gb=PPl7&@$OG7BMZPR<@2Y)}^1Qv#=mD?5pD_vZ+-x
Nu^1ta<cI?#jE|O%d)gL8L+b=PqG@NrMS{L!8RDQK|J@MEvwU=e60Wg008k7000*u003j91|*rO00000j}q&m{G&^(002=XJg15wS|h$5aGv#@HLL&t%b6@7;iy?Bdl@AyjG!MU001^8{wAg&w5g6L(kTTfKPUhI*%e4CJSU;1#U_BQV5A<bl^T#K$}<1}bt;yq%dC+qb{$3-CaaPr001T_qnr<`nkO$N$0i0QTO<Ggv?bvu>mcSMiW*ZUA|d*t0031kRwP3y+$Q%c%p_<n006D1Qz|*A?5XXd007o12_)Sq0022DpD4K}000^$P$!uu0093aHz@`s#H|1T005&O001~7008l#1gNv900000lpw1ZT_z-~002QM03UHGuq?bK)F@r8G^PLmY9u@<S}A`f9w;1wzbG*(003?z+9}#3_p8GyrYHs}DJuW~ktMn-vK?lsbt;t=S|Hb~p%xb`r7!>hR4$IEJg9A=Z=ngF-<ubm007h|9-G*xK_Eb$=OZ~4*`ojecq~|-vLO{GrJ>;`@+bzT000y(gd;<#+@|*{9;7a(006nD6e)D83apYG005dOh9Tf6004<3Q7A_R001@|Tq1cT003c+4IS2@#;pJV006lh005#S002X&bu2BY00000m!P(o8YPFU008kSI2!UPdntJ)RwYQS3a9`8YbMYu7AnOc1Sim@L#x6l004_1VJ3ts8>&4eFR5%O`X~SZiXpTmF(^2w+a`x3KcL>M389`YTQvXxn=0F@{T<AlZy!dcyBn0K007e{JE-0yDV^;ms3>$6p{M`=B&Xh>mL?D+Vjl7+pr@Ln005S(#UZUIQ>r<rGptmp006lt6sdHpDWbP2003z#e-(>|006cr_9?rW001K*uo|r-002X^V7kdYw>>|+dpbU~`MKdUv7s6<#it{#DYDi$t+48|5wzhl8?wGM=_i*i7a+qk{V<iUt~BAW{3^h;Q7>^bBrq#BuC64gB`vk3>Z0$jR<muPiK}y_Y^j#9
A*oNM^R;6%y{I#shq8k*qO#s0p|@3)qO!3mBC)72DYOzL8mRdx8=IXfw6E<ak2a+(@~l#|9kC}o;;SAtpEK|tB&ras1F--A006?J000Ue000!O{;IsD00000P@}sk(J7>*004iX1***`!YnkZVxn25=%D}r1sHWA1T1tbg&jF6=OnW$007q~0V#GVH>aj1n=Nmu>L>sJ8!3e&lO(vMlqYSg11p)B-k1v`rmO$}Dy7$_2OyiMEf-y#P$#1!002^^zoB>{+9tdohbFHj;Tr$|!K4?W-4$=5+M)QU-6_zf0076N_orp1qN--2h@i`?0065YcdGoY#wPO_007k_eXV$)0085uW1x~J00955{T;X_002lKZmGAdMzH_@001JO003wj0054xDyn~}00000(V)vN@Fxc#005pEkforZ{U#2l3#fu6z9j$vi71mPnJK86HzvR*<0*hE005sMS(-DZYNpR41t^#ymnHxJ#GI8LzpZ+t>6ve&tS;1|!lU0K9;g5SS}EP6wkcOC6P$^o!6Y-O003hupd>+}z$LPg>8hn8y{7;GNTAM^=_ysH)~E2OpQZo+004d}Vjg8CqAF&iogX!-004$9t1P3ZZ72n-002NO4;~{b006tCQ7-``000iHFD|i_0043;lcEl+#IXPX008Qv008+F005k)@2!}r00000P^AwnC6<<^005Mw!>Gurz$&Pzo2e3{qzwQ7w4GFx1+8Bwu^l9*LX>+f000Ikd?+%k`>BT}*BT-u_bUJZFDwHnh8T6Jb}M(5peZXNAf+{=(W(FdQYS7Fgr<xv#3L@FU8uC5003Sj`=pF0B`7JGvZy2+n<)SQPAM^?oTdL7$EM1t@*jwt002)d!lJ98cPjj?#-vWI004%qtE{7^Y@4vH008u*0Ia<z005k=3oO`;003dIS1o<1003jLKs*~fCq09_A)dlMemY>Y_ARNr@hu3w%R(+Y3_d%tLnH#ZjjztYbv;Z!Z#dMw6*(S4wzQu@={#J#
xj>I3<vhteqrna|qbw0MB)L2<GdGB|g}HUSIw^2HK)hl<a6xsr5w;#cfIR~_ueevWt3Xw|s5W`Oz&7Z-1F3Mc&8N$-f-nprR4kGyGA`UNE3RCczpqy(NvHrJ!7ZYy7OZzK{V?mSV=+poT`Zri$Se~shAhD`%(01_RHbO75UnC44k#ioFe}uvHK~@Zz$r4YEG$<qh_vLVTO#+Pfuy#vBQp9aimJ7-p)FpgPO1U05~<K7$fz-?U@h0Iy{!bWa-<n20jF)S>@Y+taH)GJZ7kxg=Pqn8xGSKp>@EAIeK2A!F)#Na5-oqK53N)!{4EipMz9|sQ?YEWbT2zC?I$6qVl6APn=pK@P%H7SQ6KxC(zI|X`6yg2Cn!XmmM;J>^DeBe0<6y@xT`3sH>Gr`lB2CF46KK&)v@O+C9*-Zuc88|9Vf7?KCW+;BQCkHbuXhW@E&9?ORgTX*|4m#?<GPm2dZU=$1Phh$gs&KcQ67d;V_)CWG!p5lB%t*J1Ebnpr^R2xFL_PaH<I%HYa*5?XWsAD=}HIh^ZznADMO~o2<|!<}g36Qlj{)I3eh(&8aiA#3`n#z%SdQelY2-lrya|*`$G^T&$g{%q+X8f+=gR?x^K3ZLsgKb*h3a<004-;}K~rnlE-KM<s7911<2a6{zQ@Wu#Um?=5n%+O0&Mf3BjhnX0BJ8MJ?}EH8hh(5!i=vLqxXp)jeekRp;N<)vJ%^{85@_^eSai>0WjZ7v%zkf{={%_~GOU$Q;0_%Ep~Rxr(`<0|v9zAl_B93X?Pu&^Srv=u$9n5JqeC$H(NRxVg6Q=zo5C7tXm0<3i_-Wk{`k1cC6rms3OyR8GT5wDOi38a{$VX-+cWvn@{?5#g6s;^iv`X;WcPBEyX^Q`!=L@W|4J*2KBzOddan<CD(qp&J2&aO=*Ua0;grm5w!s4||Oc`mxE*|J-+Dy%fD&8<x^VX8H>v#J3x1|$G2A}~m_`7gh)z%gVdbFp`hrmT`Iq_HNjHZU8hey4P;+^$h2
gR{jkFfAS}Z?4!Xp|6K7pCAUV*DdU>th2wc^)SMv8K@dB*{L8c)~QJ@eJA0ntgjHP0UsN!YN-*gC$T)IUZad8SFLI=IHhZ;sRcf%%B~9_wJMLRv9Mw+J*WbzV6jLh{IJ-n8mIfJRx0SUh^a)ZNU+kZ)h)NDEGnj^J}A4bOse;+bFi#3NvnG;YcFf8b+2i$wyXv%d@w{T3Noy(1(YAIu`B{6HKu>B8!%F?aIb}%5G{PDZZ0@6n<#oHhAY*mATFFN>@2u1b};d-1}?QB(<z)UbFj!O5vs?Z5~mj~4z4k$0<Uqh9;Q08cro&=f~Yzy`?O1}f3J}*B&^yfj4_8O1g2`OCM;(oy`=K7<R-MRt*17uK^{)6inSc7ZZPgD$|(3P=d4b!Nhn|_?Jx_fcrKi&x~oL3<u5_8AgU!SwyYR1_b&*osjVZf5~v!BYAYG2vam<3H>nw{U9exPma3E|D=YgaPA^QdKrK_Wnym>V7pjq})v$XpQn6+yl&l6V!KOE@^`<1W;IY=J#;j_t=`Knv_bZt!zAg?f<Ea!cC9O52+^oDSim8||bgrJQTPd6|2P)2{Pchb{>zsqA?k$QihAmMdx-1W_@Tv2w{4Oprm$4u(6sYhngRn3w%p9mMaIOI@Nh%L3O|b2sb1llN`L5cmwJsDa1s>k010>WZ-!NS;f-$SEIV|2Wb~7KZUa{sZF)*2wWh@P@xE}E`z_DMkzpJ&deJBtm7Zt9h#xb8Uaxp3>c&Mf*BQRPn-6h8`@2tr!kFb@hK&)-8maw%k_9><?X)J7<Ag)ZVvoJHS1FPOAldZe2b}Tch7O*v~g|J_!N~ayErzNzj(=SG?fv<uhimpQ<TrHce`Z295k1ez-m@4J5F|ETMv@HoL@u?v$z9~|uy0KQPPAsM;!YxxNz$|bpv@{c~Kct^2&n3PpEwhQQt*`|#2`gNw!lfxN2QeD24yrUQKrMVLDkl&z7_?%oi!EuWGOA%LAf)iE$Eg=702#Znw5~)g+^_`?x3G_=
E-Yvszpgc{Jgr}=|E=1t)TmP}`X|vZ;vg)jpr#foUMya*uq^;9Y$pA$JgcUy7Oi)z5Gr6OUM@W@DX`_OYpo%$(XZ?*4KZgdi!?&21u&thd9cW^5ug{ciLaQbDWwClaV+Ai0<OQW5iky`8mN!2)UD&O6tMNHN*zZpawHhBc&O7T2`%HQelB#azb#@d46uBtP^Dlk2Pp_HNH7YmEH0NQ$gO}cTre0Sy)x#o=&H#n)24(i{w$&^EG?3-H>GSY%CK53%pyUrMy-ak5h=f}VlW&mXS464R<7bPp{yV<sWRdzXfQUefgu;EQm=WkA+9j3C8}U8PA`6^pRb@MW3f!Lt0=)RWCZrFdMh-kQL<wwelM>q7%~K{y(dAjahSEIRVceC8LX_ZO|1MeysDTmQmW7;$|y;xUovT`LaXGjtEO8ox2{txv@Bn*$*FCxC9b@#U@8<aZ!vc-ma301b+B_RHYPQzWG?-$ZnC7T%`d;L<EC;e1D&WT)+0KoStlGVo-4~R*&{nJA}|^vxTwXfw=AczgsoOCRGcFsC9F9f!WqLT94T}yI3?IAeJKm8k*Fq}aWVEI@-Txa95Jab;WFAVFeFVY1STc40xc`AgD_03SsnQ(FR~b}EHD`^O|1K<L#(ST8>eeD)~ZV_ex~uGzN${EH7)ot0xl@387{7<6tEdE-6@MI+bkiZ$}aD)`Y$#p?kg*%LMDwd6)zj6h^-$S^e9^#@t`pugRBd%6fCMJ@uZe4rzokf&M=O$1}PJ%)i2bjjtwKN#x2IKaW2s-Z!cz_QL4o*_@oE0sIT~?IjJEs9I34#_$qR+X0N^^K(Kc(*DQUep(jx+>#V6TM=CU_w5BK}*QPM6e<b;;%d?KM=qQXXn619Eqo%^If2!A?OCbNSSFb*<wj(1kgr}da2`)mM@UMcbVJ&B|jWDaQuCS@DNG|g$fG~74ldqpBdM8;b&aRZO6|S(X8?nkUPOvL1#45coHZa>GmaLdA@UAv7)~wwyN3A)adoY45Bdz4I
DkKh_K^sY?Zmk_JL?|4mLMlYH3aomn?3C&v^RdJ%ts=gu;jN=CgrwxGJS?;<3^8>tqoQH5O)h*XbF4fx#4X37^sqZI1*oR8<0#88nyh&+nyI5Pmn8$N87&2<m@mYptt>4q<EUpW_^h0&vZ8P!+c1tS5+sZ);jE~%!?VmV6Re6b%bLM3*rR5yQKzahwjLO;{;6EC2{BQw{4G-|?y!q6_OBx^^qj9SUa62PRI=GH9xeAJT%a!&j;BkoEvk{KMXCTSA)r;G-7H`*s;FDAq$u7YxG)1D8ZF4Mm#`0_?67^ZIkf*NJ+2_I39j{?g|suW8!V=-ny3dXbEjvt&9Jqw4zpe@<{W{v>#o@?ovCgxR4DDT#Hi+=E2}B7K(b}5w6MM=aH`m?#HsM8mamGhGOlv3GO){`7ch;XDXmc}V6bE{rm<$R9Wq<5+_amqIxPUQUb9;yMzp-3N3xl)K&@ji8L5@9ovn(h3odS^ps!db(k-X1GPB#MVYIiftq`A~6RifSi?359vZ9fxrKeD|N-qE<CKltZy0G@BPO!wW!m)_3-7mkZD=fsYSgIK)wXZ9vA~0MecdY`iFC_je`>)|Ht}-sLCoCAURVpZ(rY>!%2NtHV8m|eh-l&$as;1gA(XKBo4IxV_?=4}d87ns(DKL>QdaS%KPB1O3`Yo^&el0UC122>?N1Yn4IIg%V1FbG1s4xF5{G->Z4-ylsIW3N{38J;5+Mz-$&n<2&9ilI&gRTp(fvXiO)G(tb2CrMNnXu`wORr(8+?717D6llFzpm}E`>^dWh^+3b>ZpIK*(a>3RV}EgZ7h(mgDAbR)39HyBq)(Gp|3u+46>*&K(1Lb<StvV!Y5g<!!5nByspNr&#1$wH>{bi1TAZ@l_Lt6q_Oobtfd($AurFY*eFsiy04Ei?XH%oJTzvpsj#85pD@d+As~{VR<4AsUMBl1$t@2ox3FF?Ij<|K!!Uj*#UoiP3#bpR2{2KqKqUz+Mkv}b;WDYN?<prR&MSzksxI`f
aHo~9v#hcyxvW$%C#X)Zs4%9oPB6}<RxV&3Fs=uu^Qk2;ADh-M3nU?|;v-+FSuKen*C<=BvZo6ywuC#QsVtVU#;pUdAuD+#HZDc2n=<_*4WM=~A)hBM@h2-U<SURQRxo2Kx+<70tE-kQ=rOM?=&paT!=%qFzpY%US1Mp3WgHGI6SB@Lcdci(0H4*X>!V<;=`fomIVeXk^QkJWHnIOF^)9HT-Y+M#fTV1xOt65fWvyPE%$n;YjxY|ZFSJf61hI9p;w#%BurH}3!YLT8Z>t<BU>9a0+^=FHaw-n2F0hTKOPE71qbz5!1+cv=aw(lLcPq@Tb1WgFJ1uLYsi}LdUm#T`aIu{&X)8*xhOU<?;w)0AK&p!`bS~zw6|lT4%b4gdGbObyX)wgA@39vp8ZN-G6DgOYy0Bp*VJL;8)Gf9w(k9#}HmC%rxUP+;DXL+lB&1@feku<velLxnRUq}N*(aW`Ppi+YYo7BX(y_9zb}F?nKqKZLA+?_>getzS+o&+GsHY00JugP6)Rj>%kTFsuhOXBvGca2x0H;|fUM(*yBMqFf`z+F{T_v9)J2HeQUaYaKY^$QI*04A&pC1S){I13xz$fM_x}T*fy`aOF!>B<nORcXY03`Y_JFu9rzptmUE3m+-Q=%fP)-QdhEUgkODXm^Ag{&W_OD!HQeWHS}<E$GkDX(WTL948=Xdw153MCewyer8dbTK3@Y^PGMqAZTEs;Cqu=rFvf6D_?Vow8jiO|DQVq^ydpd?fj@lbBtq!Z4+&#IJL)z#sOm<RMBhO)R6NsH<b7%PJi$FDuQg87?&=*{nn;LND&5oG|aM94Y-GvadueCa}dYzOkt#%q{UL$|<X-(JiI0F{Pj*VlQhX{IF^&SfN0#6|GPm`mLlP=Ca2n)u@A~C9OTK=Ag?brzl0O0x@2+GA%<RB_`k{Sg@Y4e<^Mq1||!y{2*{H@*e0KvM`IPDK0~-?VSuQHL>R|f-IjWa4>DFyf05I5h(F4l%0eqD6WL6!mt)8
cP+yu0<2W81}TFu^{nkI8YtEznJw|9r6H;(F|ynuSuPhT?66@lx1r0h&n@YyEGE9DBdm?D@}BOg8!Xc&-5;1HnW}^>nXpByd8vl7yRP@FR5ZjVj4ao$ysYoBwkq9~=_gRH6|n;&n<W(;a3?9PsiQHiaw`b0i=#9sj;)5O?wFh}#Hp<*doKwpd9dLy!Xh!T+9N?FX(t@4*r)9-J28^5!K`^LVy(`yZ6_41>ZmuT0<tJBx+}G?&M+%93oziV<t3Z45wD`Hx1>xlVX?KagR_9G94PCpO|54xGA(wkb1*io<FxFq5-}by9j(bRSgjB-2QXW+<SeK!nlQzy#i`V?hOl)Z)w0ho53xJ3R<5V3-KiR^O|b|quspZI$hM*^Ff%1Dy1e#00=tJc3Ar=4bUItL!nY<PII-e2bUAgh95frUq_U#D2fi$*zbLdbgs|H!Y^^mj+N#mGDJluDF0N9joGhcThB>h*&?#N5Y%qK^$)fzDM>Xg<Juc%gx~wC$$2zn<YBC%*Jgg%zH!~!yiMM98%(!zmggTELt+`_~zb-N$1iGKCR<<KJD6w%iD=e%rP%maGZZ{RFn?Puy_`nRb@h?T4tE(<BcQo8Q?=W$w6tN&NZnCj7oIK67Yd*e>mbMYLdoR1L3@x~>PdZ$$<3HuPc{Zf9(<}uu&a>3N1UbH^WWDRQA)t0RJ|#H1lr>Ve^+07HsyNZJs<p#2kF|KRH#BLwcQh<887nq88ajhE!m@&$Z8K#oxiR}RY%!;yw5YwX_b#q5f-!O~@GVF&6*RxK=(E(fi9p#nM6ipssj1UB^QjfB$+RmwjJ9?+oH8~v2(9b6m8@N|<v5bB+Bn>@b1xw~bh0P0O0AMO{VhW^tvwzs=Dq1TH?1o*2CBKHt++n1<1E5BLbQD@wyg=a1-<{d-oZC6{w0sF?>M}(DYW3WoxKCH4ZI^ZO*s~--kQg#XSV-Cv^rFzBDM9VlQR0b#WDrA3pSTJBs~!^f-g#>UNf}513e)$87)RKZYbxi
My>WEPPi{K=QPzNr87J&^D8K^bvSt~O|n!kM72~M0<a&bYO$)an=kaMJ~oLg$ih6j7QDo?jy7?)PCT_7`n906y*iM$jI;(bR=L(XOE9>$4>d%#S+Phmtt`(!hBk$=!#1rrS*-o9-o9+Av_JK>yt8T~#WsE~->@XRdN3V1@i?NTOOQf2U^F2ucDgUHH@IgpUpD$MzqzhAf2;Vh^*^^X2Q)V@@Uqf5ojy=ES12nqN3`uYcboY$*tJO}L#bIXhBc|N4zmon#JB)5sK4B(e=~xz^t2~2{<hdKaI}-Q5w<fk2r;Q9yO;~Gx->?p2dT)b3NhoZz^_;@uBN3gPNey`Jt!7Br$MJ8e>eZUU6`f2N-*Z7?5W4Gi@O9hxTSEkmNjZH*Q_M2q^^~)#x?vp4je2xB)QcliLxcMlC+;T6SayXK0H7+HZ+bc{W(jrt~l8-7pOU~X*V&kyg69F7Cfmq6ghS*g{8bO|DX-6F0Cf1r@%=pFF@45lQ@YuNiPjD557{oDLs?37d3=ARy6^$mMeI$xi~Aj%fg5?>an)AWHe~LD74r#Rif`Q=c*p7crs70cr|o29<k)Cxu?J&_p2+oskKP1P$^3?!KI?IB($KWq^-!b)HKen#<NPgE<#^4YcyOu_cyG)@hCO4vN#wzHa1lxI6Ma_(>rA_g0RynQaLy&&N$gG%P&SY39;fSLa_!s0kk_c5<AeUmAXJQM7OoIMXcYTld@~BvA55#k}i?4xithod$~}%Yp+SJ?z66*s;%Rz_%5S2OFH_kD>t1&L$V6D`#JrZ5H4gv4>&orAhFe~^dD=pKeQG)pR)2hfWM`$6Rm19VX#}h7`2<KyQ)Sti!*jFFDVf=nJXW*t}>>wy{>mLU@AN+f3v19iZev2zOfv$yBl7mtt+%0=P(McN}Fk~A~x+ccqs_HQ?Y+C!!u_o%rj**sx1*W61^L#&a{-bxG#Msk+ZIuNU#>dqOq%^At_=gwIk*`4=|QA(LY!zX}AqFZM3F1elP@>PN-$I
StzVHdO7(hek)YG%eKh1q&VO*aXK+9Y`4oTh$gbNu`qh5NiTswg0@1o2C}5A1u)4$Q@Mq(Gq_W<|FgoiQn@*~_$<q^7sPhD;IC>ndNw7hHa8!y#GfS{rLcFb0I&nDe>=N3!!ggOWFJc{V=PrL6{XHIiM6{gBqaSY_AbgPWj35NQMTWyPP%hA5<=uOLbH&nXEL-rTsS7Rjj>QTzB8{TL$<ssnz$h>LpBgD3@L@QJho7{jw}njez|e8@3EOQX0OmVQa$Z5kG63*!8oBXATgk+b}St>w>l8GceT-<EV@gX2DsL`#W?pY5<8QxPB{Fk`m;GW@jHXEU%4hhce}%|RWQ6J7%*AB%Q^Hmsk)3T9yK;F5v)Nuj<Xgi7&+9bwx-0e@Gvws{<g9)zA_#&+bqc=2eDMI-ZbkiuQ5h6bh6Mhd8$S<^DR^{f;5?<n6knl1}smUv95Zt3Nvsuk*Ml0bHyRJSTr3nBDYs9p*4^)lBAf!fjDNk4mdd(^tRzN4=!i3#kc}57at?J%BEVa54TV-)TF04526G$+c1(SFS4FAW-MqbG&bEV`!Jll)GU=U>8`J>usHI$N<3({IXx6F@un9zqd6R<__={GxISpC8^O9U_qq5o>az5%pR=OC9<P?HPbdpDnlQt!ay3q~DL4={xjynSS}4S^EjFX7m^e{1N*=E}9H=LyrM79b*tESfzO8E{nzK8Zb`E^D5}21WC$a;!`J1w`*%%h2WwEohW3;$0x34ul2tocmN5Zu(n78q?LOM#R8Y68#XTOU#f-z7r<+ZoA!Zbpr!#{qu(=^#IJu<<%=BSLNcdh_4Cav}?e6y6Lp*fnS&a-g1GPg)FMlnG+k-MreMz=UE05bO^i8K8fcp9QDk}dDDJhWaY7`nGLkTYd0gfPy)m^$Ar%B>MBNxeKp9<IVOtT{8awzAT%BPl7pE3C0ArZ8!<x->~0nKI5cxxFDSN2*FZgfcNSXRJCjeYo7cjIjA2NhN-(Z!jb>I5C?kg|(une=Jue
{H!b{N3wdh&@FGUG&vZ!)H3O+jWG*1|0+5+)HMvQm$Fs4hqC%KCNs?}FR_Cmb}Oi)|Fx$uEiE%F{w;aA?KLm4L^Z3h*slpNPQ5EBL$yD!m83f*h`h@*@4Dc(V67UkN4#V<`@0#k$hSPUw<sc{b1|_rptf(TU$S(uA-KD(qCB}eLpQvtFR-sHPrDmI&%paS12iNx&Md#O>owxF@U`DDJ-6~V0yQhSZMIrB3_#4Wg09UnxvV%P|16F*NHP|)r740ePqMl#Ijr)puq;!zpe%5#%_MHEOD&e6a;-$R;IcL>j<lStXhXL(k1;hh3@&W9Nh;?g+Avr*3$@QQpS4CDXell+)ubyeSu}pE!7nqei!Hu3Nv(G+O(`j|CZ)%qbi5{}v@8fILNKo*B`gjiin5BcH>B_`VxO3$t+d0roVVq>$uwH2RXEl-M!OUyYO#1Hm>n%INjG=BpRg)86|`n0w7cIvCcucZNIQ|SrlB-5#x=sZPPpqZu)+AZ<G6CS^fWp{WV83NAgXJ&zOQE>Xebc10kP+`k+CZ<Y^WixH>asEr7lA>jxU0vg|wKgu_%2lrL;S=A~1QeUbYb_O+lBrOFM8Z5~&zEJ-GL>9xh6{@iV%)UAK`fQ9A>-=_@TO96R!}WVDw!3Z;;-Pc!5$(y`<&;;>1$1Gb?Y13OkK=P)y*%{YRvy}9c*g070GO0L`^bhiOF&p6;Y`aRC8lDN+=d$biS!zLrQguJ38fi-Zzow#xqL@+z2U%Wm*rasUz>N4QBwX@#1TR8Zo9j@#(Q#<sx<gDW|47$9!6D@?ad8{6^YPZ&}7bLl^6RPC0A0;U<=&2~Ri?BE^&oIF-I3y0DG^8>otg((KJhLRSF*XLYc{QppFTVLSjXBUDA}BF7>@$Wi)-9Q?mA2BcjXT?^;IA?;z_CKBAi9+>53$U+^{|9GAGe7yD6a@C*f2*hcRkalwmeul(KVl`M>#^W`8mV2pf3M3_dA8JXEp`5#3)%d7dlKP2d!-<-aZW}qAp#l
2B^iYzdWkGp)|3iMIj<0@;2Q=XfFAnh_e|pDKU2}LNExif2xHf?KQEhRx2m1Wvs0@cQDp0jI35R`lI?C(lUjycQU%LHLre~$f+HuL8FYMUNHo$n<`?NSuRGV%cXUgnk!f$IH;qlvnir8(!?RdTQ%0N4L3YQk1<9p-Yma6>ote89w<$uOt)yO*0KO5Au$XtD4^N6JSY$^PN&?aQLCIa<{cue2e{HF_P9SVG_X^!2DINP-Yb7JgRtqVTe9aFu@G1}VXa%R+B5VYi!*ezP%VZW3$cwR(JZebs5aigL!*kUm!MWLExH51X2kBZg0tkb3#*|qNV!BsWh~jONIVua4l9MPh@o;fn5#RV8LnisR4hd(M6MStQLXc>F{DB<DVnS<V5}Loil-U2v$SF;fiY*YYpHS=u_$h_tSbOC8?`nx_dkZTQLf&**DA5L(lkb;bSnHj&9yToUB4SDPOfS#S2b&}{jI8|L9g1uK%#s%6syoJsvO;}3!Z&1j6iQCCbTCnkFn-7+#}VfpD=zX%`ii>i84K@4Z8cZK04Vvn=%tKC$aanOtLXE`!qPW`LZ!Fh&OdK<ghrfHn?{n4Y|2M{yH_MnlJdO3@DDa9wcKdRXkI*N4^lPGq7<w9jhC?5VlXP=P{8r)-dQT@-$bq+BHA5=P(wjb*jfJvnZ^#R5i}3o3!??^sz=UAtGw2yRDZr!!@NejJdqM3B4ma3ovdm_AzcgzPFAy6*ZGLT)aRyT)n6)2EBGLHZWy2ojH>_JuwM9BQjV#LbE|CRx%<uV=(V9Bfk7C&bfO!aysy{6g^L~kGA|Z0x*WOi986gakXwYcD{8i3pTwqI4m_Y*(-uFc{jGSg|*7Bj5#vGKM=gMaj=js2RH#hp(*Y(2R)*<aJE0Qdblw)X*0es(z;-_u`dBCZ89*t>97YeL$w~b;2)`_Rj<^nDYniq@2-j@>@Y_$Y^m3?FEm#xBCK1e8L5b^JucTJCb3Af!z9fp#5DvsAV1H$>Z>QVkGeO!RWXGz
cCDMQi886GO11W^d%W$n-YB)KL9-7q!6*l@a->r`mM@5<GAHb`nk=|7qQMI=UbO76e>jV@GqcjHi#ZB2w<a|<eYCA8HaQ?K<E`5{PdFScM!M>(eJLU&uC(<y)HZFcqAcJ+8#9%kub|tm6qFu6AG}B--ZEG&)Uzd_Jss#gE2nI(xi!+Lpr3y*z_121x*uMsyr$SHf*{QmwJ{+X%OyLsK%?xa3NODOR|%gWPNFNQpR#HuEU59ULz!G8Fe_28EUk~M(T#&BBRDvy!Yr0BIxOBaKdA95hOk;P`lu5k@UE_wYc^pqvnUQN&ou+E$)>3ny(p|PjI6LD$0+zF3a5Uk)VVvKUZCYHq$(09O);3RIIzRH+Nb9*&?73RFRev2)UtWG2*`yp>#rNJ!9D_|ZZlA?oI72iEHy;H5Vn{rkg89y9;Pj|!8;DJOTXB$^D8j4)i77ITC>2b+c}mfWTJAlRJHXr(K((iQmRZc=AdjHZm{>PoGwo@k1JZMBrIehv7l+NBPZ)L!YpyBo+ha-!Y93|pQnham^HI8jyCGHCOMq2w>GG=wJ^Ommo^J7s3*KGlqrm|5wSZZ-K^ELKd>69wzNeX`zDV(PNN60e=w@D%p>NudMYz8TD}dbsX61JEHbSm4Wk3AL@g$>lc?%0rZ`ivm^6yPJURqC2gPbOHY}?&W=9IMAhP5%-@gwszcH*VMx|RW!6Y@QiY41f_p2eMowBE*U#+<?lc>%yN3*M_UN|PE!kzFaub@^pkFId63M8X2F`NjRz9~?c!!W$2#U)y-)uR}qJRU@yLax%NBPS81y(Yvna+Oh`jHDbdtue!`XDxuQ(?0MpyemqpORIXY<|EW9=&_#4VX%v|uDZi8LNAW7ASRZlv@d)vAgqSA5Tc$f3N9%oejyn&EG^+L1-g<bU#GAlE2aN0z$vb%z$Gv&KP?dq#wv!W(4_h>4<zlcot7LS*0B04DkHohXCxS{@t?>elDpVK4>E?LEj5pnH@mz%d$`Fl7B$;5Q!1;d
Pd4E;Y_vQ(w!t90F}TVYW;nk*`M7(eHJFn+`Lh!*ST1w1sXMhL9<e#B?>dmJiXxh~b~5QO8Mqy+dZwv1C804C!7GQd5;X@h6fmJL**Nz#l(ic(I52-EJDg55_AlqY3ZzK5$1^&uOTomb#I1-XIImMN*|C%|rKWA7RIULmS2Vb?WirpW^*m{rld`_BV=VnFPPS4sTf3w%OQbxsU$!_jv$@JGp|-F)BDh|qgRVCyO1;dmq%|_J617jNki6Nz3%UrL)G)0y>aJbB{j++sQ?p|^=QboTbgUITuAdY%q&n~*4YnUFaxOJF4y{Hp=qs8!{wMya47s>9CnylMPBz}Ce=aF3|E;yL(zwAoqc7vI(y<0J8YJ>3`7i-BhARj!BeY^YKEzqM12zD$`mTz=t)ytObRtVbKshHk>$xN_P&wVJ46x-jl{z4*+AtKunk6Z`2&;Oq)w374S+m(Fv%thJxV~F7;<VQ;KshzAgt~h*2b&zYAUd<OE4m*wyR|2^wl>HwAv4f7NxY6R*SQ}!fH!@t2|V<+EU@b;->oh#O*JmUhAFqD^0LFM^RXJMZNMrpDXhUDJvDA63MF*1WG@#olQ3W{s;(ie@2c~hzp48uJStl(O|7*YX0Uy!W1VHBHLgIfQ7&htQ7t~K%%!cP@hnp<-z`fq!66+nL`ti(4?N8`q#y@0NHI__YOT5~?66m-y11e%ZMb`uyr^xYWUO(kJ}gba!=~jlny5jf-mH14F0At|6EQfc)i*30`i(mx53E-xyr~;CQ>aj*fi2~yfUmnWAgd~>XEDI4F0Fed*Q$vm%dKiNY$cB?;kiUT7BE+|f2yA?A~PvD>9kEQI5h7%va-rH{jj3E+cVNMV!XaE<}YwCA2~?7K&*Buw6Ys9XDnE&#x!!SHMBb}MJ&y)1uQD4Oe_{IJTXPGTd_Q`L9o5H@T)H>F{K2#X0#-*ML=1*zC{<rBssA&xHDNq(KlHu`m*^mp|_7Q;wES-)hvCorZn)f=Pm}dC$M0#Autd)
{Vmrr1vFkN*(@ooh`>UgSh+T*g0dd83AxfSN4n9tOSn%d(XV1FpSOK9A~N|h9=2+*>#aPhe=~?9Ft1`b46)LlW-%VXB0AtR2_-wQKeOpR7`gDOCn}XKo30KrJu=C(EUe9_BRLo;c_g;3xwM?3Lol)`Pb;jnSR&}D3aps1S+#I6NvaL4RV%zKex&-c39H2_+bx<Zb*H?iKA{U36|l51I-}*R3!jT9KDITn968=KNH7E`i7QAX%(BQeuCdB6RI=Zq<t)miPAse%?J}FRf+Wj0TP^%9Zmz4a$Eawtgs48QV8CV|h&dOuH!d+Xz%H$=DyqM+lqnP~mNDO`M=~um_%cm2ut8iYwz&|sna8>_c(#Hm%S8CEY&$)`Zn+GyO)fJtWxc$=D?DDg0y&&D)**H+x-wzBpf(jawm{-86)^`m?72NLyR{LrryJ0<dMCXug|$yM%d@CBCNv4GMYO3diZB(lqbS8JF*szR#WGyBmMEj5{4g6Y7%-x(g|w|O>OJ<ncdtLXjxR~Fo3q?C9h<JWvnT2^D7<_vfielU8?e4J3@}q7A2nRQD5U&0SuaDe2%7A?K`Zk#n?NC|@VkMhpR`Xe12tMCPqkz+Os@qvK{&)B*|S_a)V8rT5J6=xP&SLY)j3ls;5Rs`5<#jjraj!mB(!I#BO?<kA++PJ%q`5R=CTK*Un7Dsv?;PQA1t*exGuXaj}_dk0gw?b*sskb53z|b_z5bcbto|}IId-{2CRvd(XmsY&l$TcN-&hLgreW5%PJ=*BqZ$`il!VUc&&#pG_Vb@o=a7<hdQ3Sge=XcmZEnggeg)lG%r)E?Ygg<aIqAyW1k$Id$PK#1}9&>jirmOK(O_qwI6vf#;p*o<~Ly@TrUSPn5#3EVyoCEA};<c9xNXu2BvH$`y@-C%OrxcoV186d!}r$C#(W3>@9IGg|6HuaIq1<kU6e6(Xs3^4J;zQvOSBl>@>5c!ZB|$12Qqe)-Gf@`a%-0TqWZ*v@qB%cD>3V!7wehG%7YJ
lr9`J*E;nxJ**crm^5-IE~XGSj4b;$5U?^a=QM7pajr2YSE^DnJ1$)<L^0ns6Fylq)V0vPB`ASEH8lmKl`fY!Q!QyTG`t`%#I*i4YpqJIw7l9e2(~A^Q6f*Z9x#+JRIGuv@U2*~5IUZ!W~TtI(5cEcR5k9U);J5dKeC3mUN`rp!#SC|4G<x@{=Pk}>@aq^!@z{2$gavVSio&CP{H#-B)T@Vm>6v^%D5=LOsbqXk-Lt#XRyLKlC{z=v@|X_jysv6b*;QI2DSdQ-L8-<D6C1YODuD-bC}bwbhE-P1hmpCOte|5yDzgVxv#va&b6Veo~Y3=T__|ap09T_<grkvn!6M!oi|}R+qlcL^0l}o;ySG^!nQcP$}q34sI||ouDF7_XR=hX^0*Q<8?;@(@v(rpA~+(WV6Ff$cQP<3Z?5JnM!cA_in0x^6fkbCsx)vlz&c?!HJ%Kx{w^Q2Q8ooG>^n{=9<|!9{kSPA2{b;tsyzTL3Ow4w#w>m|SS!D+D7y_lV7Ia~F1eaGkhh__7N%{!5iDdbem0vo2RP_9{W5AV1U%d`ZnR~tZnEViV>FYkh_-t;2Q<<$bgU?_=PQ(u@v=rMe7BV`w6YPZhOi5xP^%BKH?-y{E~sm-Lp#o~nWCb(6tEe;&m$8pue5NyoUms!k*$fUsw{N2V=!>9Z#TrSDLLN5K`i63%&`%%cqX2=n6#OuueeSpo+#V1a<-}}%eZ(iD>qRw$}&8p#kAokx-=g+G_M;rzdSK4YPl>l)iwJu@3LnsLO*~odM438bh%PAinpOKX)*af*R*uC;k-AvYo;r;s-f^bT{(3yNV(E2p{>p|Y_d<h>%2fJVm8%0>@%LMg)pi#ZM&_wYczhPYq3`_DH{^5lq&qE=(7Yh^09BZT&lvQ0XUI45Uu;McR$oK(>OA>jJFQBO)6(TK`}M0TsiB!eX*^&8Mt#VD>cM8Oti?f7cv91HMJMKd@UY3+b|0*AT%bi*pjKBuDzTo={0J%a5wBOHN8DH;GlZB
Fg#?s%D-nbw6emyskwZ(FgemN-?n1088tqwO}K2k-zO|90zIWAuqeJR->;dN>N+n$aJ?3?_b(|pezj1puB{?D@i9a;kH2HLjI~uK?6ieE7&6!~q#N)s$F;bwAh?FQR6BsSe<wULBP158!Ym@S_cA&v)i={HqqHwHs-**>6fN?%E-ZGcaWh-9owLZeZYX9r>N2u81v+Cbt+#SGma>yEay8Q$COeS1zb98Ni#2bpPnGsE{UjlzORxpBa<d;eg0eZLt2_rDceCiOq&G|~pRx|Exxk#Vkh+98_PPJAX2N_o-?b2``!}vE$hhb;I=P&vD7t_tlC^%Uey(6Vv$S8W4W^v1RVjl$-8LI1R5|0VQLiJd^D)x6CM_AU={!I!Ehx7#bFyHv{H6w|_p;(Oj<2OC7cmGgMz;00xTzJcOQ-LoWGuO{SFb)Wc(RuwLM>&gJ}QD71gH`(5wZ2K>NCu+=RbNaEVKnN-YhG+b2h;-=oxlAT(CW|m8D}XjiVVa{-LZbCpLJs@3eWcsi3p04Y5P1Vi@iz5h!jn3%xy+)U!V>?6Afu+_XubJG7-ZVJxdHnlSexAh{1Yt*($Y`9-v_*)+c}N5MWb5iyNA*TsG~gt~OT%rt1S8l+Y+{IzXEGPo@=N~-j=$F@x~<1G9%g(yX>zc)6h%A|j_fwT`Z&^0|X53o-+X(ME&C$bx@xikkg1h4_9rm1YHcPWvy888^NNwU+W+%DQQAS2Zs;Vvh$7NBjlrZC7g@Wx6zp}a-03pt-Ya64VFr=~nQ3o&ptlPx`|4>~!wQ?;rjHM-g}6f<ACHz|dz3$;VCzOg{OtTk0Gd^}^P^*AIcE;PTb6u2v_kTu~vurWz1_cPBgCoeuPFRnN=kT^CiO1I^)w6pahEI0ox@;T`&&p#S2>^1hZ$t%#ZaJuTU(7IwOt1K_K<+U(01i<6AJ26hUwK>SI%^;V$;kVejGc7l&;xN>$9}A2nN;)SpX15Wvh_sWe!zy7b;I1|>UmQ=daJL;UkTL7B
a4(pkptRIB?K9gouekI!{xB@RY_?7_9<evQYqwV{l{FGS5jf{AsJ+lLH9omDNi-n2B{&DRun~AVNi>2rVYW0bj44Ar12&&DF({fPBRJ8vva#l|1vHPcz^4r|e=WJ9Rxf9;i8Z#r!nNlWs6V!=(6#bA_qsGPgf{N7a5>a49J&=dr?a1}K`pDRdbX-Q`n)S0zpdUfakQweF|@_Am9WJ!xU9A*J~XW~1}}{;K0U@T^&I&)2r^x$__brTr#RO(eXK*H(k{0gO`CQ!W~jrok2G4b95cJF38QbUjvT_YoiYHVmpRI)^TNA4-7y$4q_i42Q8TDDI5L{VVnEBj{H*7+w8R#;n5|wh<Fr0GWiW=X*s-Lj<ScbB<1wQ*k+o4ZpQJ9YpuluAxFPAeLap5#@i`ea6)z()ZLrp^TsLd7R=CZyb3B47hc$@1ORUkXcQlqYBsX-V>b@^Tbfld$_A(l<!mSiXm_C-V#K7@4XgKOO3^J`V*s|if|G-wZm#~*EPdN%eKRvCkM6_NsFfF?<f;JDb39+C#Hy%_jP&6MPVlG3j1GQl_XsnQ|yD6eFFEYrjlqlXfsxsHCO}w`^wl>(bIkn}r1T$$iMm)>4JvPTW;IxyheXSHO4nG*IsIj;hs4*8X^(@jLurDMs6+6$jPNA~6S2V=3QZxi4(=KDP#<7gLLO2$z47{$n5w`*{xHtf_!@X2Fj=7;VYdy0v5U(pSwJ#Z;8aebeR5f6*7%xCT>@dwQlRTKANio1db1)yEZn})Os4dp7NwCMTle&Vad$_=)x+34HLNNij6)}$<BdV}3KdCV*fhmD7sij`FF^pZL;Ih%I{3XvY+O6g>3NA1wSSuwoG#{X$AEel?@GKOtUdE;^V7eQwtEGRlH82}DSgzl$8>%KXeKHiSKrF8;_n{uF+_O3^dNUv>rmAi^nWAJc7p(=Ya;b4M&a!N;XQqWMHmm9)x|}R1Ml7l*cePWnv9YDG3$gqxJ+?Tg;W*;9W+yr;$+I#x95FPaI;H_J
im!_i>^KUtcCz-cttLM!owejbj58B1?kP#GM=w#UEHCe^PM?7>h_M1R5E?C^&K{^Y<ui|_P#R0LXsC)HO)$8tMyiS~1ShSii!FyMFsURnRx@WYe~<^Qs4OlpNhmLovZ#40JTBHV^fh%as>rlF0=*r+2ssM6z`5u$L@`*R0kAW$L!RF*R<f-yJu4a%$Ezu>IJf$(^R5^;og{~?DzL>i3M}TVak)4rA-fkUMKL3-bugeTq_ruv+OL42U^1Mh|E3tMA}OA-6tD!X(w|teFsO;F;I4lzwK8NZE;fC&I<f_=k}x!=BDKT7`YN$0sISbi)URE!)-|9th%A670kim|)T~sr46f`hvpTaX{VBJ*q9>s%POuQO8oBZxHm_5tD6}Ri%pD3XJ2ca`NU5!_1Ss~a6{`Ut+^$e4Wuy}^>8u#GZA)A@Cacf6vMQam6gJB&>z1cAZZYXF8NRHn$1?b{rIci@vAV#mkS-Iy>8ok90x6cNf~#UQF_2a-xxoM{e6&KYrZ2y;G$XnsUnrG1wi}2i2(Q;GgD3s2$15PVHn1k3;W3#tdM_TUcAoh(EHG;yBR(&?6R3i<U$myQ8n$-6vp0CHx-P*hFf{J9yew2TMyTvMm9k7RW2(cok}(0coHki8OtRS;=cL)5cPf7>7O+4pMX=~5?x(A;sjng^t1~PrxhS@&aI~PSeJ^RD=&pvg-Ly=rk3iu?(mgdj8L(b1pfGqXx~e6zv8J22sIV+Jw>-A5|2!hK-X%FPfw$wZ?ksOQ7Bn2X>@KCLkgyghW39zBnKkMzjXPjCzO{!h#;+Bv-!OkTH!N~Asigz6Jg@ULYdp5L)U&BE(l3-eIx=A{nz_R-F`zW8D>l=;#5Hg(KQP%fgTAl1@40a<-nGgvfUi$7tF-np@GWI6v%11ATPkiSgQuXf%sb*KgfxY=axH16ORQR`p|ejcYBdfk*|GB~1S}gg$gZt1^|9lo`>|RlN~ES7pRvL+g|wM9yRv<}Mnzh;;y~-N-Z_>szq#qK
Ml80s6suac9=t3hL%ByaH!iI)?lT#(F{72jQLnA9yDS$k`z}f_5hZ?|QZ<h%{W<@$w=|}(oVfojay1Dg$`;zO<g@!PRWOOP2`dJtUN=E7+qBg+ggP>=Z?mQ)UA*0@SU(Xit+S`H6R7pBnzQq?+%{*fEVv7=q_hAnH@RUs3@PBHv8?bgiZXRL7c-SKnKb1pJ+9Wa?JB4#QZEy<va+ZzZm_AP3Mfh?Oe;~VFtyIKu&`UN2eLsjVkBB3YA!%Bi?r9U2QeiuX~tl|+&~dBhc&suQn`PilPn^?*1du^L_4l7Znv?u3$nDRL?$IIJhW9U@vb^NK(C#!+b_7b$tVG-K{Z*f7&s9)RU2<D{yDI)WijtKbf}#+*EzqlW-<9XmNjNHSu8>zv$DOh%c&oycdarmg{iKoZ#r!}tD{1&xG!q96u?2gEIAG|N4#IOU9)yLsJ){ynYQh)&bD|i-Y2uR5Ij%2qq1hOZ76a!1+7UZu(cPp>NmkO?5~lqz%-exo~`MzjkTvP&^0n9axkE^*{D{XNHi#~e<JNQUO6DJaY86KIjI33nkTionK+`dsjW#n6gYe|>?;s4Nik}$FtwO48?-|z5I9LK4y`4)k~cq}D4^4{leOxxPB*9^_p!b?sxm&I)U<doU^3G=BDD>+3bK|hWGy{7(mReo0JUf>K{V~N3b_C=bg3G-Mlg^kO)<wl$hd^4k~X0?-#z!Z7dbjK9=tBMc)6*z55jFWy*2|iUp+dp{w&?24zlX8B)Yz>`>u028nBJ5dM}->A~=3295QC55*~`JFDN*!IWc@SR<??&<*?>6U8<BRVljHOB{lc4A3R??qcE2)u|4g(I;A?jPdsn0EH!08NjPsbMWJslR6JKV3987XqNzkRBQ+Mpd97PHlQjFXEwRkF-KtEpIK(5NDZPlZ39ZLB0IxeNKeBnZ;kR6#1UB&@nz%VQ89Syl6S+AiEV)KF2D`zk{<JGM@uo*0f;llf@v#^-hb2`inY;nMV=;s=RI}x^xVE%0
FtnnyRyKt-zQ5EoJ2aaw!>R#9??0e0SS>xgy)Z4OjH{Y5oVZprakXc(kRyR8WT^8gEUhgt=d2R39;pYidaOGz$*0${4lE9`$hOZfN<bzuX|!iFW-CR!VKNoBzNGFspP~yi_b*5z#xN_eo-32G={f|mEH~UbsxNH2?6l#S0iX;tMz9$+3Bcg3raD}*O0LJEQ?fCz1wVAMrYwOrJ-VGR(><0eR5hEcrzSwK`L~8OKRP6^y{sxX;y4npLcCi$IJw&?i?bG|#kyWYrzX`gGBV<_YbL)dIy^5g@h;^h!Z=#D`lD+q05*oU_aS2wY^)GCwz8S405vW#XgV#o`8emX<TSD)wkMS|D!a!xU$|>Fyq<k4cCx*&xw9uYN3PYes;%NU3BY?WqPVa&9Jofj>$hVvw=`Wjr#ZtnSvb9}l{y1B_%p{XGBxKd<2hx!rl_sFQ9Eh14V|kv`L%kjyTmfE{<Ux@>9066OSKKDBf5vJy0J^R*RfT!1hPs#&@-hxsW>6E`miXol{izh6u9WQ;I*u``8_N&pE)x+prJY{)HaehMZkcvLAql%_c4CA%r#IrlCOHS^10ZnNw?jtaJz}TS~!!iXEDe-<S(bD2s9@wQYtq%G$wqU-8auM*{n}4a5HW+#5K$_Co~x}$1X^#9yz_Wv8x8Cueb`f2r`;51hwtDsIgzTLopXFoVEn4g1O1MpuDImYC8Qk(WlfT)1Z_!W3ddtKCA|~2etIIzpN^+(5k<&eLF%O{Jn!UZn+?~#5NGIZ@9?6F0Y(A`?IVt;ydHNQ>(GD7r*W;1-U3VUbrtWak53D_Af&&Q#3I+EU;3h|1smgL_TXiI5mhX@Uwn5A*x0*zcu(WleMqBx~>4RC92c0D=hc2<~2ky#<fVe%&!bGeK8g>1GX-@ha`rs)U+}m6D^pm;IKlkrmVRvX0RGADWhI1p(@BTEVEg*9HdvgKSZvy$ge)UMl?UP60_ql{j}n(#j>=KTDSwOH8vx(djfzfy12+JZmX8TNT`)8
{<HM23pKAY6DAZdTsRLYz&ZsuZlSv>=P8aIJF(+45-XjrVIq+(g0d^Rn72$dv$o8lbEiW#AE@IlYBc0D5+O0K@xOV$kT#XLL?^hjb-yRTv9!uBC^$SX(zH@H`8B~nIJus>$UC8_b2TA4JGU>ljX9XN`!8y~0W+&B_9!hk$}y8TC#q&H@g^`RmoZwmwlqUCl`w%cxUrO`7%e!cz^^MOsj?)ovMF&qGcXsol{uriKPh@YYq&G0=rU3{^QeQf2)SY)_b`Ag_9lZeo3x%YfH$+Rzb0w0dobH1sj#0fBs65QDL7g%qPR%4AFQn{0=J$q{4w&eJ}MQoak-W)+pQ=(fx88?EwDhWb~F8;oVhHgQY&sdb-(Yg%`$1fRW?~N-6o(e^0&q}kGxhER5$M{$uZlmjk4FSakTKbrNzZJ)iP182Dqz1g*s)UTP;95+_12t0HdWh#}%%(_%FgLu_<q?p(tvp!KebR>a<ra+byN15v8=HswEjM<f<Yw<t8C4=`;B=^{yZ@KP`Ya@}EU3eXiC!NTKJl12)MqF)?_sIxgI))GSV}(5;>|SScv91Ee#p1Ekh8e=g^((?JO^F|B%{OtzJ(vA9(-RyH#;@t;XA__Z=D5IAQwuQaPU#I)V4bGO2_HaSkM&7(56@~|eLC%jQQce3iX&mCVg`#TG=>9UqKuDZsl2s&^y{WS+F1FdeTb~vFl_bKYGH8tg}AGyS@tu<M=Au)_GYO$-g(=qC`MXyJy4k(B%S}0wv548m}3bX;J3N*_zp(qF{<|h@j&NCvk9WD<w9z`uYO}slQkuzz)%d{{qC$UaBOsyd-in@0&ue?vOkFq`~6FYY>ur+2qG%pacpf_?ZrZG*mm#2BF96(zm<twu^gtEG>!>y^NggN*;ajIFTN;i9}JT|U8PNPJ;R={GidNAp>fV$7Hx;P3r!!(1j?==HL);j?<!?Ulb#5vSK);xK$f5R)du&~=H(y#ipGd%MweZvp6jI>O$XEAv>q(Ck-xvVg{BCDIA
K(sL`R=Kb+Q7|4eMJD8}MVwh8IJV$51u!nOS1*+!)~QgT;<d*uF|n#Fs5r4QkSaQ?%B_?(jxF}D7Nb)$1spoM{Wv$Q5Fb>l^EPI-60zg3%(CpIGN&B2gC+1c0k=A=q%OBHNvK<{Sv-#>(6;=uSF`3VEHsp{oVTqtUpNpo(K}$V<h7DA5jkV3h`7G0sxmFK3OrLWAv8QPAv-TL_rg&?(6GL+_%UXvovn_!k*JL-Td!`m+^nS~G%g;vUMv}~{WYVY*(gmf4y@KTvNGGQBdOD*=%wzcRWUd&(=9<Av#A-YBq2*A#~ivU_oPR%&!k4J7o&BmRU`nU#T-H|_N4MK53c|ku1WMPjJdJBeYJ+PXt63RsU!$9?4>}mro3ty2R5>@y{6qPye%E5?ySnZ<)X+o1hTjon6K9_z9@4naKFYV)1f9Yq#^MgWH7F)s4f>d?<mwPH7GQ#1hl;^zBh_7WGba2v#)|P2rhr1BqGAE<20Qm;;{ZcKQ>~w8L$m4613qxC^>btx;Z+rq_R~yt1LjjMz=(`cEBC9TC@$c{jdc)G%JEMQ#Bp9u%RootunDQ7&#&}sy4@>$+6lkL@vj(kT<8VGccj71R%Dsx+Ny2O(K9Y05nfCOEVp<;lPzQZZ<qOmnwX-F1ZJ^v#_ox7`2Nq;55B61ixglL?%-(zPU4_2Q?Brs4>>LcQnzh9j<)2-mw!kRyNx!ceF39A}=#8p}agWi!@<8ytqHKzqiUVm^5BEo4<=cskkUJ1vgy1!?t%NEHrU7q_>i?MJ$88Vzg(e(In_Fr!hN1$-Nr1-ZWMjf-uXnqq5gKA~b|Ak~c0LvYPoPEw}3`H88NNYpsPf5U{bPbFLGxO)&SQ?jVz@Od?UJBNf1`WHi7pcqhRuF)C;-t}587y)9O<*ebQAw4ews**@*GGOh0~y(uZNK(F8?K`3H03al}&MKuX6dMr1zu&%Ty`?Sk2Ff7r#9<CI$fvo?kLY{)Jj;*8`_oB0>(zCcSX|9#7BQFxA8m^hX
HY(+=H#6{~KDD>J$SGDdGsG=3F*6S}&BfC*^(<jD<vtB3ML_AcP&kJzzbslXusVD{962tfk2Nhaq^M;!uqz}x9W<h>l)^c@!Z8-OHZ-)m!9U6-oim!SNi$osaV`I?wzbbXCo=>w&@MuxtS6hS?K5&U@GvE*X0qI?0U4bqs3=n~=CdTNvpHZodPT4%>$ki(QY-_!`z+Np4K)5eR5^brxV$!`uCrt@=dI7RzdF!0uQlntFtKC1;xWW0mnt%~+A`TGW5Z~$4>P*54Y(Gp2?8TB;<-O5->kwZ9W}Wri@78*8z-46*|>-?lCC16RJM#4*{(yi<Ta0(2)xw5W3a3-2P!-;F+5JRTeq39+%#e#0=1s7nL7QqVKjj(h(e>YxwF|ZmMtATJh$aCtt&7$6{9PwYOu&O|Fe}ZAuwvINj1Z$oG5{>HMBXf`YB4Wo3N8ADj}yUXe6kvTrj}35}m2F=s+Dh+O-ceT&IS&AvrFrGA}iz`>Izi9<qy<VInuN(I+}EV6!tc=q=d4`z~Ix&naZ8S*BIBD6!Qk$Gj4wkF}wyZ!)hmlehyjxVxgb)-2gE=dp}0SGf1EZL5$uJh);tyRix}<-PVbMYG){;J4T?NHr2dG%6Z21SoYd9JM_>>^f7c3^`yq>If01a5j6oRx;A9c)+W(^)3}QSv45FJGeobxh01&O{v>0aIuoEa5aOn7Od8s4JUOdQ84VTh_zR-!Ys})*)1Tjek8OczOqrZH?s1yx;7uK%)A9Sfir|P&#NIf3o#Ki9j!Ds9E^CZGq{T__OWTU(JVtIXtUa;#+og+njA(oW3Tipjv=cys<fsx2`_q)jk^N2Qy|r)Znpz5qdDcMO)zOO_9%z2$~z!8Ag9!@Jj2?s7QAY-_P_+JWHSyvbw1EGlQ)DwO1I;;C#v8vYdF5I$2Q<FF|10i__ew?-!x%7BDktHyu&d!k~6xtxhP&Re6n1zy|8FJinSy(Y##ozf-&m354CkRv9pn|4ueQ5={5ecJuO!-qp~M2
<thFvQ7gr^SFnV(HLVLZk~v1U47wGsz$=8mz`fEmv7~b_XsUU+kTmD2W4f*(*fMgX0Xq1oC$1d5lPrllld+R7yRgSK9jJx0qr!+T4L=*SxG-Qg45o^$j<!m_khZm@gC|3zQo7Q-S2k|DB((rA7qf;pxjX?EOe}k$hq(_f*Som9Ksn5`_$#F^yFVi{%e_4`(y%cnST}39Z@XVN*syi6emvf=#UXk$-=cpsPBc-fHZM9j1*c1?pD-9EJSl>$)hY9+uPu+ViLE;@Fg1Z80<9RX(=Y6_ma#*w=`VM%bSLzzgs+1vTf=)XW2^<WfDzz3>bcOYH#3zx6SoR5f4HM4u(!^vdL`x_$~5YpqA>F~<Rz)NYAk*;)G`*RHa4U*8a&%B!L!&E0JzvHFSY+8qpcM*4>As}qc!ud&9jFyR4C*#7d?}-YdZ0_4m=a6H#l&v&OXX7W4?;9v^6*|dMjG63pxY9u{&)lQ#Vnt=CG?Kd$hJRZZ*jrH9XCpb2c0??7AyGxU{x1;x1#ct}KtGySeH!owWKjh!7zxzcs_I4=L)jN-#0FcDMw!$g}e$qp1?ABQV!7T(+&WY$#W|UqMK-RX<s>#4LO{F1Xyd+qDKX5<QZ<D!siaD7~|^5wd))>bP<`hBVN`@F8b2x;0#~o3o&%ST5)?lt6JU(7A-V%BwoG9y&-U-Mkw$3@!>fL^>F>AF!l1khjvhHY+TkhP0|OS~ln@j55w9#=BS~gRy<JeWhYJ^R2>~TQ^a`Jw8md{G^vJsyN5BMYL$Rwj*Y=1VS#V0;v<SEV(_vY`(X#KeT!)wXbs%Vlgi_5Uy~hZn3v84y$#s9j&0VQ#j4B>M<m+94`;BEG=&;zc#Y14JkCR3bq<K6vluvr#6T?4y4O7_O#tEh#=s_tTlY9D>x#wKsdi7C@*caY`iZsYBoSVp|GGgXRQq^?lC5`8!(Bq@--=}M<9MR$|-%a6t`hA?6tzQTDWd1ShY<tP`PQd)HFK1ufQNDzo|tvj5ypUZJ|9m
`@H8I{JIFe(ktaA0WCWuGqyRo6tz~f3Nf>_^(oFb*)Hg?VKoOhC%|koCNYh*K{cVKDK}WE+b}gS11ZQYaxBv=9yQggn=E{>(lpgA@F<kCkS)eI)+vlIEU9;_9v^cgHnQ!l3ax#fV<@RIWW<Iw={3|oS*~wAOE0CfV>blAU9gld894*57&VbLo}AJ!PPWgl{vT63=_!c1t1>n+q_HYEO{k5p>MWiwV!2$f+Av5mT(saWi?Xn|!L>=Tb+rXEd9^vYcs3O{aXg7JDY(upUpL>KF*&d`B`oDF;=0ts)V(1<Wx<HPy}W}!YckzGAH?QE5iXT8`nHF>_b=kN4!eawIYVzj3&ZC>v$#dP(Zj$#p*~%|c|hGhAhA?CAHGGtjlmzdL_O5RvAvVNu0V9ag}XSy%dj23x4QK=8=|wcyTGTrxkQUT9>}LP$2?CsrYWYpnngE2UNz@8{wnvZS}=34l&{7v#j`7@K)D~RfUH$CSTbs@uP}l**E6pvN3#7bKQ^VTZ804*o;T4o`>by@Rv1ICJT#&(<}RJC7O0-GVl~b+aHVUlO)Wt(7PE4q@~mqxOD_vFHmoPLTd~-*xiGOY2%`8bWU_}IQL+uNy`)$!N3a>Si7az8pQtn>0yRQ2@U#uAO|uv_Gc?bzlz`hQAg=JLKdSII>NrC$T_;1cDYmpM3^O?^zcO{N7%p@*RWHXZ1T^BcST9(y`>)2T+p(3l!>k0QgfFkOzBg4X;HLO1P_<|&JS}S{fDaC=Z8EPk6f0)59jJG(SF3(0cC59p2{2x;->fF7Juy<N;w-W!)Uv=exTt?Kd8^5*uPIhEsxipEkTg9pNU|I-dMTc(ShO)Ot0n_1G$j$UVyp_Vq^vJ1eXd8aFfd@PTO6+{4zIbbIx%G@)v$rCnl0oYGB7hVFf1uJ#jI>JA}zczv@F3WqcYwyd?pd5U9T1?FRufvk}|O_+B8ov8Z@%4Sv35vCN4xaA+#hgr?wKgu(1WV&nzo0xFaC6Hmstu)48j$u{Fl7
DiI5?LbO4!2QK|E5G9bchqtzuX|1*`l{FzSX)&-Tz%xTJcc`<l^e{2C7baCR0;{*RlB}sND>d&iRWCEIe=@H#9y9E*w<41>IjSx+95WBIcQ!bx=c!>SBCcJJ1+7!AF0fs#xT@(ob}&#UDlv((TCbC%rYW4RO)0@Er7xSTCbX3<;W87oeKha0Q?re!DIqs7>@*6BLpU*`Ev(xvjWcDiey?>c{48UsrmhPx)h&P~x;0HGqA+DHd$Z@RhpELG;UQ`@VK9TRVKek8bghZ8b~JRYjHyvD@G^*}-?DKsj2FwTw=VlMu(CllNH1(80Wg^~Z!f^I#wwYs39wqWMy$j!B(*Fk{hhEaS+VA~;WC6E=r!uFsH^I!w>G}CGcYzVd$UKWsk2D0z^(5w@GdE_(6LW2WU&q}WHCN5K&`i|fG`OvQnd@TA2s_h*sI|#WGY7~el@W%VW_S&yEC;mEwH;bx3l9lrY%}AFD(NxaH`ZWP#*Cq!#0qymoVa}4y@#`Ofb!{6{L`_fi!U?QZpa4=`?SeXsTE+wlG>U@2-+CsIk2*K_*|RNwrNbYpi-K1GU<u%d$H*ekZ`If2+DI88;TMT{WP!RkM1t6D8iQ5;(A|VV(so_$pVhOf*5XBC9|xv9+Er0<ae{z&NWgk2Hm<pELore=_|rbTKS7u&|V++cT>%inMVu{w>U|)iL!on=u$OFSJ263^4mGe<-A_CN(l9kt}AgDlr>0<)GWLQ!}iw*f9mNV>S@0yD@a5urXyV&8*WiIV$e10x<!vP%NN0maP`F$|6a#1}{@FVK6Z<qBOxVw<;zw;4la=ureREa4Xa_y*1q`Eh}0$0yA4MC$OF}Mlx$2dNW`qkgTdSUATNVLNLKMSgqKz8Z}|GJ|u!J@Un-cB)6Nf1Tlv((XIlm0;~nBW2;-QQ8wMM#xCY8hA`i`%rF)(9g@o~T{D%wvZyhzr7#aK{w^0W&o=)lB`Pc^Ml|1`x-SH&o~xR%FenA2ek|=aOf0dm^RZ>C`81)b
?lR9Zr!z$`mM{}CO{+4fq^w#qjV;A02{Dzi$0g1zjj<E1%rw+8X)la0IxD2Gb~XL0lrMubs<rqW1FqMkL97TW88GcMb~O;O^)SFOgfBrYL#*JXF{U9nd9hVCY%EK#AT9YZ{;?@BBs6^}TQD9i9<SjtP_y!-%`v>UO(pI$HaA)`u(eJuQ>J{dTd_kX$1;d4Yan$lnlu!s$}v^5@UxkuHZ*iAK(6j6DK1<oEE%_{aX3V+IV+hk!>_BU)u=MFF|s(kbu)u3uDd9)5wak%v%GGq>M>`gcpx~b3$lo=w=uXd#w_}@H?jz`DYQc{MluMoDKJ>7YbzhB#xRJm|D1<1t|7uV9kAZ9+pzGY4KKqlU8l^NE+n#}DzII!Mze-9s3Frexi`2bd#TVWd9wjCRjp($v9o}zk+0>q^e!B%C^);b)2P>|0I?yoDzN;l0<grf;xR2HB`e0O@TgO*mNe+3m$AIDyQWXG{w&v|S2x(2ZL*jykTG$#db3L~gg5>*5Hgf5oGmac<Ew?GEGh3ONS`6L9W($jl&#$?oU|t|<0Yc7PADF$)H1m+$*`3yPci4ByC;scU#r)<E~JqyE3JmJDl`N#*tPI6C7iagkg;F15+jE#=`aQ`R4t9Rv9o`yjI_@!-!$(rX)_WlXfnXDHLu^OOS4z5)3Q7_$}7IIHZ2ged9JWEJ+K=$*`Rr^E3XeEmoZ%}0JPe!$g*!MK{2qHnkZ_t+p)X3ZnZ2M=(04u>?~Qg<}E8V_$Df_5xBFb=`~6%v@tWS<gj*~m$8B=0jrZMzbq`Zfhyps9keyLQY<5|WHFJfL$YwQ?xub;=`lPRfwtna_^fL-B`7N-Kco0C1T!VDOspxD7bh&SlC$D8@3zD)9x>6hzApi`2DFBoY%Uh3YAsozu`_45wzB24A+?6D%`@gKpQ0qT<1p`~H=cc^(J6eV3bmWHo~rONm#T1|N;UMc;j?A5#jhl|qBS2iwKE;7%rfY*LM~IXu`z72`la)?lrRf0sIG7;MJb{!
Tdg0ns4=)K1US$i88s;?1g^C+^RZ?%&n(Ze)HKF52{URn#;2g3Uo|N>_pBZ@9<a4Ara0F%G_yZ3SF#~CoHZ~s;58mNb~Fe$3$z(Bzcw)}B{u6a>NvkBvop#yf+{32SuyA_dNmEPDX=@UCNZEdDIhu|*fhJNMKDC1x;Y0p540gKw=mB#+OC<QsI2Iq-l-p|T&l~oCa?Z7vbB3BhO#@hCb1tepC<?^*D&=k$F&@|v96k^?I@e9fTDk`@UdC7>N4;u^0ON=*e7Z&PN502gsgF}XD~Oj9W%Wx{V#H|R<PhCx~$tX!!q9`zog+b6g0*w=``gr1~-1S4yoIq@E_)|W3Zf@m9N$(kS|p!Pqrkq3#*SdfUzs7OR;LTU#e8N%_+k#Jip|#RJ4OB?Z8&Bo2+uGxSirMMz5P6Wim<)R;>rI->po#(x)o00;t}nS13BMcPQN|s;)^ZP_TU}wJugTaxt5&6si-cb)%!D_b~e!EhM#}e<eOJKr4$We65hMZnA+b`X|RPaIvVSTrOZT#jP+a53GAS{WT0Wbuyf)N30~VSR_=j#<WN=0x;jOtG9eDX*JcTRJ5bG{Iql{*09PflduZ16*A^9Y%o%;tSt*HkG1eIH?lgUFtUF!NUg*;t}QDzC#|}*c`Xqu$1D+=lC}M<4>a;I^R+sm2_g!r=_{tQvA6xCHnhvRX0f{_1**2S;IKw8TQfK{k1{r|4y~`U^fC#rb2Oo<4Xh`$uCquo7bvBpnlRtBYBX7|l(V$A8MJA!w=+Mp8JuUY(=F1j>9iaq3@w|lkgZ3!^R$?&7#Vx1S~FuZ1SkHclqm_S)FQnwz%}=?qb-XqCZo%<z%aEl%dw=kgeveU^s_KDelQoiJG4n8wI8E3v?zx(12s@E?x_K^lPx+hr8E_$fG3-kwJd)ptg~&g2r7CtK(g~OpR7SIHzW3@W;h%!uCh)xiYov%R-`vG^)lQv%C5~Wud2Z?P_f`3p)JF$MKFFZC^45Y9W}_W)u8M*;I~0E47LC%
#i|b?K&72IIw?Fa9W;lvC$>16-!W0O&9yJEAGqH#<S~OP@-Wu1r5x`q*f_#0#Ix0^rLfg7tgO?r`Y0x^aV`h4U8e-Jv$d?NC^-5vy)EV{dN-W3(lDzq9kzlg%DAnu)h=wZ-lnQ7n4Q?Ij->^-Of8KsU9||ACaKyj<hFV*uCOY&qbY5?^R9p<4=W-snz7iY2CqCXXEF-2%PeXmTC&Hq0-o`(+_U#2zo`8zLNSvoJTUPfWh@)DR;9u;&n;T4d9&;;__5w7i?aMKawrWl&oBbB0WP_zz_L#;XPOZ*VKI@d4>zBv!7&v#nXFGUtTbOI1~5Ib^d_?|jjj!`iZs$V{WbhElQS(Zk|1cX-7ZqG`7`jbHn)}_88pc<zN?`v%QO(Qw>7OayDBWH88IL&CLm9=hOCM&C^82xx3rj|<}QP>6gT~~@iXQxw~QUINHmx#95696d$g<^d8?{0EwrXI)1q=Jj;)@q2`$E}iM7+V@wfjgTeQMFwm0&z5H#4V>@WZ_B|3_?(=dH6)+9u<`=kr06FGCP+bjmKkuni57$rWpzL_YmCbXF^!y^J8#4vlY2Cz7-6s=G&Rw&@CD5P&Y5-xkH_@<RD!?Xx17_B0;Z8Q`#KO}ysM>7I4U@$7PlQZlY88i11p)NzHm@eQjMi~yJUoy+Sqc8t0;50%eaV{G&VKtsG94sC+6Ef2)4K_xxSTw68jj+ow?V?XId^Ghn*er7}J263=lo-r1wkyl4VX96oORPw=aHi-r_o$yMaxJK@sxYT3nXi(x?=(6v+@4V~3N6VkNiFrZ4WWiKD6t|n95o`iPqIw3maPgQ@3Ww=kg*H2jWb3n_7_#PlCa&jDz+{$PB4rxZK`oChb}m({GVhk%rEgHPNrM4@FkG2DXiJ3;4qmgSTbC+LADt%Vlb*JhcKeCvoV{wIiNncS~L4GytMDAzaZYQkeNj`F{b>mVz%<9eK3MErYPqlJfd|fYOi3cGO@Ng2sRJ6swr<SEVS~jQZ*Q~VU;Q`II=9Q
R<a4MBRIjc@inkCN~%b)TCM4@sj<+r%_<43iy-r|!!2*H0Wt8bi=y%`v#2vK_oTZx%&ikK_p#ls8Zf;w$R)fj1+g<V3@I`+TrWJa&#_H3JTEXUyEINQTQPJo9Wtylz%dOkH?0G+;4E^SU?@p612361aWii*K`|yaRk5d~W~yMc+$v}`N-z|x87KiX*dw#1KsCWL>o5m1hb_c13O0x_K`M4A0Jq7lcCX{HPq7y<?zA!(KeCK8V5?KE4XJmm`mBv5Z?MWWC@GULK{F?;JD=b%TpgOYvo2Gy6DHp?1~7TFtTmP^mMs)HXD_CyN2_i%X*hJVqcdf+t0`Hur8FZi5H;K~p|z#8)-VM$yR;WF(<=)$F)w2;*)gIn#3-^YF|QIV7&TKd46e7VFE1XK2{Agbk1L#_hb{jwPA5b#sx(Bj(=5)Xc(DSq>NA;+XeHdWA1k>l!7*hry)tpXk+2Y?l{CCDmoSGdH8;U3CngIo6DzW-ATb6o!>(2^1+WP-gfewBSTp=IUbDNW-z&YTwjf<IOEhPv2d3mU)vtK9O{zXH-8Wn@T&&3<=djQt-mTTN<2h3>d^Wi@*D~d@FETnZI5y2VsWz>#S2ZfHIj77mcQWZSr#Fu!STt*-RJI*C>Y~xP3psbN=d=_wxiyckX*ZU&&Ny&6-!&dHGdIC9wX`3$hO-_z3bgq%g}DW|L!TD5C_1J%WVvrNEV_@lzP1)TO|_P}8#jF|+c48O%r~#K;yFk;RX5Bv12=&(8?{Hc$F%1=Be&2s#kr)qL8}?H;XC*^eYuu4ioDIax3qCR-nW&xJUQ4gUNGM{)i!Ii2{=JH#x=_{g*2QqskXwlxwKC@?X`h5lDhS^TCY8}@HyBw1G$JdHoWk-8@J^=GPeD=6FEvW%rsCsU^pGMK{&iQ<u`CN?k-X>EwYWcN3<C`3Ad^>Ww~v-45Fj9Iy_D`9l1?7Kf5Hkt+Dnz#JKpkb2uR}a4_XL@HHN?i#S|5gEqJ{Vl`7T?zJwraJ2?II=169
;<!||#H}&5A3HTT2Dh&^NWA#B;kI!-<+U=o>NsUJ&@=}+WH{Ni1~;TQnl_X+K{e_$Shq{Jb+p1cj<~QgAi77n&LoGoPdTGBx42<AExb*)__lsM=e7d5{5aDxyEL^q@;8XKi8U`d%QY%C^)gd5th9Q!cC!CDFS&s<Hn}3YBduGu1Us%X3%E`=O1$s4sk4_n<+x$F05#+=g)#Fv5;!Bd#x(0SmNh{(JTbK|`Ll$#AhG;7jJbj}v$%q~AF)=rmN^PFE4j`%!@T0SF{=7JzPabRr8fOBCNS_iWH-06QZ^Vka5vR4JTj{>BDI~ie6%Y$^tARg5xJ+h5VZWbQaO4ur?_S}8@g|}0<@hxxw&SyS~rv_f;0{}Up7Oz!Z*n_Y&XR=|1ujj1Go6Lj<o(bN4fGcg1BI|HMFv{r#R*^XSZB6XS|EJO|<qrM7hJbA~>=vb2N)O?=*h3i8Ze{Z#0oC#x!9vldulBoVZLmUb;;+$-1Jo-L!_be>>|npt;L49K9L3kFsDqin$HBX*EwT$~59QTR7CWH95gJyEH;IVm7lhf3r)qd$i>_in%#81i5jzNU+qk<~l_<Fu2D!xx7iZ1-3;z<hO0PCOIoL<}~O!_c$cCMmNeiST=GuDK>8~KDd&(fw8AK>bPJv@wsNWJ}<hps5>_|wz-lsgS?u#H?x^MDY;g;?>H?olrQc$b~RhIQaI%~(KzunYB3`<9<pV)@wehSXSNSF)Vk!jezjA$`8ka=;J9BnK)dm|X0&lVPPxFiS~ff@?KIIjD>kpU**4iY_BG2i4l~&>P_}Zo#kLtblDG&pPP!two3s13V>;P2S-D&`JiL3k^|seMK)G|drZ+V$w=kDE7&ub43^}bi$u{RTkTsY!=doh7r?de(Xt$*{ExD_><FazLT{;ywrMLk(pSyXu;<P_K`L`FjkT`lYy)_g&&^Q>ijyFa+ayS(?hc*>3Zn%xP$FVp$<+zkK(78dlb1s9mr90d<PPu|InY^pH)v$j&7`Y?4Fgf-!
7BRgzM>dzY>Nd(a1~%d}NHfeZJGNoCxwaKLgSZ$qPr50&g0t<nV>;_KV!3oSG`w=T@V44KG`VZJyf+3esxYNFM>dzY>Nd(a1~%d}NHfeZJGNoCxwaKLgSZ$qPr50&g0t<nV>;_KV!3oSG`w=T@V44KG`VZJyf+3esxYNFcsKX9DLDo>0XC;NWHpa3{j^@T<*|f0ez@#2PP#_Ao3ux?gE=5M=edD7YP_7b3bq$M!?<F)T{^rmEisKdRX91fw>K9%NjJST-8HBzUAVZp3$}?l#kiL@|GE*mOE3kuy*oZP2)dgykh~qbu&<0foVlmDCO9H9#4x5f_&2+?N;qFRem7S$PBk<(EVG!k=(GYmrni7LRJa?raj|E(**ZQmo418Gy}M1i<+Y1Et+y(;nKUajf;DqG9yUR@(l+)uG&ZR;-ZIoMD7JpM+O|_VOSt4TM!HhDkh2%KO**SJWw}!}^Sh0?vbOy^PPv}Bfj3w!=`emdEI13cpg4LtiZ`k@6*Uz#`LeIIz_iIaLAU8Nl(|>9U$MoubUETSthl5&q`POhCA2*~2e>@BA~=OKsWlQiqB#Avx;Rfd;5I`wFE@KJ__tEIb+M>961hS+in$EADJv+o<U2ArRJnjOki0Fqu(F0cAi3DMHaRsjlrX+HOEz`42RGU{A~yCkNHZ%jEw)v-w6-KVdAKJvLb?pOe6!5AWI6~oNV$eKHoRlG@wVJNF1dHPzc+F&{V=CFOEz`42RGU{A~yCkNHZ%jEw)v-w6-KVdAKJvLb?pOe6!5AWI6~oNV$eKHoRlG@wVJNF1dHPzc+F&{V=CFAvY|yS2%t-;5KD9(lm!InzSgl*|nWHQ@H6dv$`j`k+9#j(>ZlGG`gZVP`t#q(6dcF=eSY2(>dlaS~0siHaHcx`!-!VHaKrJ{Wa(=7`U3b8nvc5thh!vZn~zqp)FFk%R7QLp}8P5fV>X6<Fc7NY`KrQuQ&QLlP{7u6F9-Op*T4?y*4s6U^VD91GM6`A+-NGvbS_L
?zktnq_L#77(0YEG`JWzu)8n0?zIg)^S5KT-8Tm`q&3JoCpNRU&^F3A=QYMO@-lQVe71bJ-L@DymAL*iNxDb5!?OptTsqM;YPn7}HoS+q7Pr_uO1Y=Gnm1)FVlag{$~G>xzBq|E#y4>`WHbmd!?cvP|Fp+BA-6F!Q@V?{nX(+U9Xee&uDF3XguFSp9<|~;xws;^vpHiizB8jbT{Zx?ggBBrk2EJWF*Gz6O}IR{Qn(j7OS$tlkGada*(`~+Cp<bhb-D30fV|$hm#>*T)w!&>uQ?Gf8853iY&g2L3^;{1t~cs6Q#K(sfwPXbK(f_3xVPFiYPr_9OR#{qyE&LO*SPODin|xNOtvvS>9_s3E;hL|KsCQQF*d2U$~L|@@HNLY0W-ERZnkH**0vowkGT0XL%Jrp%Cq&iTsq%1Rk>I;H@tJXI=9(8KDmIojyK*db1;@U3^zcxQaD>V!#2q@6Ew&#)U;N&fw%8D5xDR(Si04?5V2&q9y-i4S-BKAK)jW?BedN-&$zL<E;q_BJ~Syhtv52awl~B&3pX4!=QOS_OSibV*tH!x=D4;tH@Y~vew%%@<U95_m$|DpbG$0KVzjS3C%JLCQ#mIw+%QZzD>%QkjW_{0D>tAv_%)C<MYOE8owB$(#kZw4IJr}}!mS*(**cdtin!c2#=EY#0<|SQD!A3Sg*a9;!Ze*aFE*RE$2O%n5H{H~5Hrg#SGHcb&$b;pfVcoPI=U6P^0W1~RXWl&R=Hj_HN0rKQ@7qcIJt+pr8lT8gD{>sv^Lzfk2ozlb~a5lv@@+OjJ4Lc^tZq{7r2Ttwz{6U|E@*2+BxAg#ks^cRJ@hB;<DR38M%VF?>9*>Q!@xU$~PCc5;)g77B&7ftu=@-V7KSELA8iEcDa`}q`6wT*d(B}C_I8Vo4KMjro6?up|gZMR=I<@GC5E&Dl+>xH8{+*T{s{(>^EgLrZs&v^0Q;Mh_V7ZDz_6iIJsZA4zLflp*e6jM7S|Hh`V{Xx3xGu1GwL~j5tO#
<21%PMmDCmyf&LS5;om5EHl6`SGHBS!?qhbkGSYGMY<Nb$FttIQabH5UAb#EH@s@OUbovkF1dfXus7%}UNELP<u*jOH8|Kf%r;FmJv4SP!L*ULnYJrBXt%O7A-Z<Bda|pxFFJ)a(6|>kD!gsEhqdoK(YT7a8aOI3H#0Xn>Ne@NK{)d|ZZ|14nKfW6jJM#pHMa~p*tmc<mANXpekldEB0PvVPr2?jg}k@9%CMU}M!9sku{q~2-Z1|-NI0>zZ8!uu6*qG>ku`xeKD36mPO_;w3%C?Eez}0SORi70J31*h+qerj#k-HVhP5X>Be>wW9yo3^12wZcOE$K*$u_t+@-@*kH8aUDL$+7A<+dI=kGKalN4g=oxwG)MNIKy)V!3uUHN0lIF}K@1FS&=gi8uW%nlPt1{Wh<+t2Z1v`ZZ)U{xi)iSG5zk>$mASkGRY-WV%PW#jbU@U^+`Qs<{X_O}w(XI<n0?6}gDHST=(&(=zHgy*3cH0XUgED>dIVIy8tY^tS`KI=3l0d$}Jry}6sY5uGQtS3LJP{J3c~fV{c6yt1b}fw{D~j5)<FBr)qaO*omgwK&~34LHj-6*eI@y|XU1im|df^|mHAb-4$)FtEwB6FTuWsJJaSwYzb+jkYa4<+sqdkvN_-9W~ZEDK?+Cj5e=0_%*>aA~TOLUbbSm)3zl#leq3QF}fnT_p=DNPCC&wSh-6!H@tMYX}8=wIk|(ma5p|JK`@g!MmH6>-8Zo~bv6w(Lo^;RF}2CJ7Ps#?K)9APXu2P{V6l?8q&ep_c)8;@HoSeg6}9a=xwx0PgEqo3kTW$pbT#X>Ryk!mAU8xdp)njVa<_Q6J+y>5V74_iFS-u7!miG)$~=oWgSmY*m%O^Ty|j-!ak=ccqdMU!7cOQwZaB@g1UT(DL^lLBUN&hpB(%G>%(L=3g}C=NM7dG81+22TEIDO2h`2B~wY%52gS09=9=H~{x;BtBK{bUsPd3rFqc&zZ6gKBHTQk!zIkrx?=C&3(mALLS
H@f_|0JIvoC_1k-R=I&TG`wHAeYf5`CAoUJG&e{tD=@Y>^~3hV^~3hV^~3kW_QUqW^uzPR^uxl#n!_i=48-2UpTi5p3&az|s>9C148+;PuEPq%3d9P;!^6zO48#J&%EQvb48#G%+QZSq48;G##KYLb3&aM*-NVeo3&aD&@59c+48#M(;ltF!48#Bc
)HALF_DATA";
float decode(unsigned h){
 unsigned sign=(h&32768u)<<16, exp=(h>>10)&31u, frac=h&1023u, word;
 if(exp==0){if(frac==0)word=sign;else{int e=-14;while(!(frac&1024u)){frac<<=1;--e;}word=sign|unsigned(e+127)<<23|(frac&1023u)<<13;}}
 else if(exp==31)word=sign|0x7f800000u|frac<<13;
 else word=sign|(exp+112u)<<23|frac<<13;
 return std::bit_cast<float>(word);
}
inline const bool ready=[](){
 int digit[256];std::fill(digit,digit+256,-1);for(int i=0;i<85;i++)digit[(unsigned char)alphabet[i]]=i;
 std::vector<unsigned char> bytes;unsigned word=0;int count=0;
 for(unsigned char ch:data){if(digit[ch]<0)continue;word=word*85+digit[ch];if(++count==5){for(int i=3;i>=0;i--)bytes.push_back((word>>(i*8))&255u);word=0;count=0;}}
 size_t offset=0;const std::pair<float*,size_t> arrays[]={{weights::policy_w0,7168},{weights::policy_b0,64},{weights::policy_w1,2048},{weights::policy_b1,32},{weights::policy_w2,32},{weights::policy_b2,1},{weights::value_w0,26624},{weights::value_b0,64},{weights::value_w1,2048},{weights::value_b1,32},{weights::value_w2,32},{weights::value_b2,1},{weights::opponent_w0,7168},{weights::opponent_b0,64},{weights::opponent_w1,2048},{weights::opponent_b1,32},{weights::opponent_w2,1760},{weights::opponent_b2,55}};
 for(auto [ptr,n]:arrays)for(size_t i=0;i<n;i++){ptr[i]=decode(bytes[offset]|unsigned(bytes[offset+1])<<8);offset+=2;}
 return true;
}();
}
int main() {
std::ios::sync_with_stdio(false);
std::cin.tie(nullptr);
Game g;
Planner p;
while (read_turn(g)) std::cout << p.run(g) << std::endl;
return 0;
}

