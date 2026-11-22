#include "dart.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>

using namespace dart;

// Coordinates are in millimeters. Engine settings provide physics and timing.
namespace {
struct V {
    i64 x = 0, y = 0;
    V operator+(V b) const { return {x + b.x, y + b.y}; }
    V operator-(V b) const { return {x - b.x, y - b.y}; }
    V operator-() const { return {-x, -y}; }
    V operator*(i64 a) const { return {x * a, y * a}; }
    V operator/(i64 a) const { return {x / a, y / a}; }
    bool operator==(V b) const { return x == b.x && y == b.y; }
    bool any() const { return x || y; }
    i64 dot(V b) const { return x * b.x + y * b.y; }
    i64 sq() const { return dot(*this); }
    double len() const { return std::sqrt(double(sq())); }
    V left() const { return {-y, x}; }
    V unit(i64 l = UNIT) const {
        V result;
        scale_to(x, y, l, result.x, result.y);
        return result;
    }
};

V pos(const Player& p) { return {p.x, p.y}; }
V vel(const Player& p) { return {p.vx, p.vy}; }
V pos(const Dart& d) { return {d.x, d.y}; }
V aim(const Player& p) { return {p.ax, p.ay}; }
V turn(V a, i64 c, i64 s) { return (a * c + a.left() * s) / UNIT; }

double ramp(double x, double lo, double hi) {
    return std::clamp((x - lo) / (hi - lo), 0.0, 1.0);
}

double swept(V a, V b) {
    const V v = b - a;
    const i64 ll = v.sq();
    const i64 p = -a.dot(v);
    if (p <= 0) return double(a.sq());
    if (p >= ll) return double(b.sq());
    const double cross = double(a.dot(v.left()));
    return cross * cross / ll;
}

bool wall(const Map& map, V a, V b) { return wall_between(map, a.x, a.y, b.x, b.y); }

constexpr int H = 22;
constexpr int Moves = 24;
constexpr int Models = 6;
constexpr int Far = 1000000;
constexpr int Grid = CELL / 2;
constexpr int Prefix = 5;
constexpr double ShadowDuel = 36.;
constexpr double ShadowFfa = 43.;
constexpr double ShotDuel = 6.30;
constexpr double ShotFfa = 5.1;
constexpr double ReviewDuel = .67;
constexpr double ReviewFfa = .55;
constexpr double DashLock = 1.65;
constexpr double JukeWeight = .68;
constexpr double FfaInitiative = 1.25;

struct Frame {
    V x, v, a;
    int st = S_FREE, timer = 0;
};

struct Track {
    std::array<Frame, H + 1> f{};
    int fall = H + 1;
    int swing = -1;
    int wind = -1;
    bool slash = false;
};

struct Plan {
    V move{}, action{}, next{};
    int act = A_NONE;
    int bend_at = H;
    int melee_at = -1;
    int prefix_count = 0;
    std::array<V, Prefix> prefix{};

    V movement(int h) const {
        if (h < prefix_count) return prefix[h];
        return h < bend_at ? move : next;
    }
};

struct Ray {
    std::array<V, H + 1> x{};
    int first = 1;
    int last = 0;
    int owner = -1;
    double weight = 0;
    bool latent = false;
    int launched = 0;
    V direction{};
    int finish = -1;
    int clash = -1;
};

struct Phase {
    int start = 0;
    Map map;
    std::vector<int> edge;
    std::vector<unsigned char> ok;
    std::vector<std::vector<int>> jumps;
    std::vector<int> leave;
    std::vector<int> grass;
    std::vector<int> wall_sum;
};

struct History {
    i64 t = -1;
    Player p;
    Dart d;
    Action a;
};

struct Seen {
    struct MotionSample {
        int due = -1;
        bool armed = false;
        std::array<V, Models> x{};
    };
    Player p;
    i64 t = -1000;
    V velocity;
    double turning = 0;
    double idle = 0;
    std::array<double, 4> lead_error{};
    int shots = 0;
    std::array<std::array<double, Models>, 2> motion_error{};
    std::array<int, 2> motion_count{};
    std::array<MotionSample, 32> motion_sample{};
    std::vector<float> fog;
    int fog_at = -1000;
    std::array<V, 3> fog_point{};
    double fog_focus = 0;
};

struct Forecast {
    bool alive = false;
    bool visible = false;
    bool armed = false;
    bool mate = false;
    double confidence = 1;
    int rearm = H + 1;
    int prey = -1;
    double attention = 1.0;
    std::array<Track, Models> tracks{};
    std::array<double, Models> prob{};
};

struct Rush {
    int owner, step;
    V position;
    double weight;
    bool weapon;
};

class Bot {
    GameInfo gi{};
    Config c{};
    std::array<V, Moves> compass{};
    std::vector<Phase> terrain;
    std::vector<int> phase_at;
    std::array<History, 64> history{};
    Seen seen[MAXP];
    Forecast foe[MAXP];
    std::vector<Ray> rays;
    std::vector<Rush> rushes;
    std::vector<int> route;
    std::vector<int> escape;
    std::vector<float> fog_scratch;
    int nx = 0, ny = 0, cells = 0;
    int built_phase = -1;
    int route_phase = -1, route_time = -100;
    int route_target = -2, route_mode = -1;
    V route_goal{}, haven{};
    int t = 0, me = 0, focus = -1, opponents = 0;
    int live_phase = 0;
    V origin{}, waypoint{};
    V clash_at{};
    int clash_t = -100, clash_enemy = -1;
    int throw_hold = -1;
    V throw_move{};
    i64 kick_slide = 0;
    bool armed = false, crowd = false;
    const Obs* ob = nullptr;
    i64 started_cpu = -1;
    SplitMix64 movement_rng{1};
    int pivot_at = 0, pivot_sign = 1;

    bool cpu_left(i64 limit) const {
        if (started_cpu < 0) return true;
        const i64 now = thread_cpu_100ns();
        return now < 0 || now - started_cpu < limit;
    }

    std::array<int, 4> leads() const {
        return {0, int(c.delay + 1), int(c.delay + 4), int(c.delay + 7)};
    }

    const Phase& phase(int tt) const {
        return terrain[phase_at[std::clamp(tt, 0, int(phase_at.size()) - 1)]];
    }
    const Map& map(int tt) const { return phase(tt).map; }
    bool wall_possible(const Phase& p, V a, V b, i64 radius = 1) const {
        const int w = p.map.nx, h = p.map.ny;
        const int x0 = std::max(0, cell_index(std::min(a.x, b.x) - radius));
        const int y0 = std::max(0, cell_index(std::min(a.y, b.y) - radius));
        const int x1 = std::min(w, cell_index(std::max(a.x, b.x) + radius) + 1);
        const int y1 = std::min(h, cell_index(std::max(a.y, b.y) + radius) + 1);
        if (x0 >= x1 || y0 >= y1) return false;
        const int stride = w + 1;
        const auto& s = p.wall_sum;
        return s[y1 * stride + x1] - s[y0 * stride + x1] - s[y1 * stride + x0] + s[y0 * stride + x0] > 0;
    }

    bool blocked(int at, V a, V b) const {
        const Phase& p = phase(at);
        return wall_possible(p, a, b) && wall(p.map, a, b);
    }

    bool clip(int at, V from, V& end) const {
        const Phase& p = phase(at);
        return wall_possible(p, from, end) && seg_wall(p.map, from.x, from.y, end.x, end.y, end.x, end.y);
    }

    bool pit(V p, int tt) const {
        const Map& m = map(tt);
        const int x = cell_index(p.x), y = cell_index(p.y);
        if (p.x % CELL && p.y % CELL) return m.at(x, y) == C_PIT;
        for (int dy = 0; dy <= (p.y % CELL == 0); ++dy)
            for (int dx = 0; dx <= (p.x % CELL == 0); ++dx)
                if (m.at(x - dx, y - dy) != C_PIT) return false;
        return true;
    }
    V node(int j) const { return {i64(j % nx) * Grid, i64(j / nx) * Grid}; }
    int cell(V p) const {
        const int x = int(rdiv(p.x, Grid)), y = int(rdiv(p.y, Grid));
        return x >= 0 && y >= 0 && x < nx && y < ny ? y * nx + x : -1;
    }
    int edge(V p, int tt) const {
        const int j = cell(p);
        return j < 0 ? 0 : phase(tt).edge[j];
    }

    int latest_leave(V p) const {
        const int j = cell(p);
        if (j < 0) return -1;
        const auto& current = terrain[live_phase].leave;
        if (current[j] >= 0) return current[j];
        int best = -1;
        for (const V a : {V{Grid, 0}, V{-Grid, 0}, V{0, Grid}, V{0, -Grid}}) {
            const int neighbor = cell(p + a);
            if (neighbor >= 0) best = std::max(best, current[neighbor] - 3);
        }
        return best;
    }

    static bool land(int type) { return type == C_FLOOR || type == C_GRASS; }

    void make_phase(int at) {
        Phase p;
        p.start = at;
        p.map = live_map(c, gi.M(), at);
        const int stride = p.map.nx + 1;
        p.wall_sum.assign(stride * (p.map.ny + 1), 0);
        for (int y = 0; y < p.map.ny; ++y)
            for (int x = 0; x < p.map.nx; ++x) {
                p.wall_sum[(y + 1) * stride + x + 1] = (p.map.at(x, y) == C_WALL) +
                    p.wall_sum[(y + 1) * stride + x] + p.wall_sum[y * stride + x + 1] - p.wall_sum[y * stride + x];
            }
        p.edge.resize(cells, 5000);
        p.ok.resize(cells);
        for (int j = 0; j < cells; ++j) {
            const V q = node(j);
            const int gx = cell_index(q.x), gy = cell_index(q.y);
            bool near_pit = false, ground = false;
            for (int dy = 0; dy <= (q.y % CELL == 0); ++dy) {
                for (int dx = 0; dx <= (q.x % CELL == 0); ++dx) {
                    const int type = p.map.at(gx - dx, gy - dy);
                    near_pit |= type == C_PIT;
                    ground |= land(type);
                }
            }
            if (near_pit) p.edge[j] = 0;
            p.ok[j] = ground && !near_pit && !circle_hits_wall(p.map, c.body_r, q.x, q.y);
            if (p.ok[j] && cell_of_point(c, gi.M(), q.x, q.y, at) == C_GRASS) p.grass.push_back(j);
        }
        // A chamfer distance transform supplies a graded cost near pit borders.
        for (int j = 0; j < cells; ++j) {
            const int x = j % nx, y = j / nx;
            int& e = p.edge[j];
            if (x > 0) e = std::min(e, p.edge[j - 1] + Grid);
            if (y > 0) {
                e = std::min(e, p.edge[j - nx] + Grid);
                if (x > 0) e = std::min(e, p.edge[j - nx - 1] + Grid * 14 / 10);
                if (x + 1 < nx) e = std::min(e, p.edge[j - nx + 1] + Grid * 14 / 10);
            }
        }
        for (int j = cells - 1; j >= 0; --j) {
            const int x = j % nx, y = j / nx;
            int& e = p.edge[j];
            if (x + 1 < nx) e = std::min(e, p.edge[j + 1] + Grid);
            if (y + 1 < ny) {
                e = std::min(e, p.edge[j + nx] + Grid);
                if (x > 0) e = std::min(e, p.edge[j + nx - 1] + Grid * 14 / 10);
                if (x + 1 < nx) e = std::min(e, p.edge[j + nx + 1] + Grid * 14 / 10);
            }
        }
        terrain.push_back(std::move(p));
    }

    void build_jumps(int layer) {
        Phase& p = terrain[layer];
        if (!p.jumps.empty()) return;
        p.jumps.resize(cells);
        const int jump = int(c.dash_v * c.dash_t);
        if (jump < 2 * Grid) return;
        for (int from = 0; from < cells; ++from) {
            if (!p.ok[from] || p.edge[from] > jump - Grid) continue;
            const V a = node(from);
            for (int d = 0; d < Moves; ++d) {
                const V b = a + compass[d] * jump / UNIT;
                const int to = cell(b);
                if (to <= from || !p.ok[to] || p.edge[to] < Grid) continue;
                const V delta = node(to) - a;
                const int slices = std::max(2, jump / Grid);
                bool gap = false, blocked = false;
                for (int j = 1; j < slices; ++j) {
                    const V point = a + delta * j / slices;
                    gap |= pit(point, p.start);
                    if (circle_hits_wall(p.map, c.body_r, point.x, point.y)) {
                        blocked = true;
                        break;
                    }
                }
                if (gap && !blocked) {
                    p.jumps[from].push_back(to);
                    p.jumps[to].push_back(from);
                }
            }
        }
    }

    void build_departures() {
        std::vector<int> closes(cells, int(c.steps) + H + 10);
        const Phase& last = terrain.back();
        bool final_land = std::find(last.ok.begin(), last.ok.end(), 1) != last.ok.end();
        for (int phase_id = int(terrain.size()) - 1; phase_id >= 0; --phase_id) {
            auto& p = terrain[phase_id];
            const int end = phase_id + 1 < int(terrain.size()) ? terrain[phase_id + 1].start : int(c.steps) + H + 1;
            const Phase& next = terrain[std::min(phase_id + 1, int(terrain.size()) - 1)];
            p.leave.assign(cells, -1);
            std::priority_queue<std::pair<int, int>> q;
            for (int i = 0; i < cells; ++i) {
                if (!p.ok[i]) continue;
                if (!next.ok[i]) closes[i] = end;
                const bool anchor = (final_land ? last.ok[i] : p.ok[i]);
                if (anchor) p.leave[i] = closes[i];
                else if (next.ok[i] && next.leave[i] >= end) p.leave[i] = end;
                if (p.leave[i] >= 0) q.emplace(p.leave[i], i);
            }
            const auto update = [&](int node, int time) {
                if (time >= p.start && time > p.leave[node]) {
                    p.leave[node] = time;
                    q.emplace(time, node);
                }
            };
            while (!q.empty()) {
                const auto [when, j] = q.top();
                q.pop();
                if (when != p.leave[j]) continue;
                const int ax = j % nx, ay = j / nx;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if ((!dx && !dy) || ax + dx < 0 || ax + dx >= nx || ay + dy < 0 || ay + dy >= ny) continue;
                        const int from = j + dy * nx + dx;
                        if (!p.ok[from]) continue;
                        const bool diagonal = dx && dy;
                        if (diagonal && (!p.ok[j + dx] || !p.ok[j + dy * nx])) continue;
                        const int length = diagonal ? Grid * 14 / 10 : Grid;
                        const int step = int((length + c.run_v - 1) / c.run_v);
                        int deadline = std::min({when - step, closes[j] - step, closes[from] - step});
                        if (diagonal) deadline = std::min({deadline, closes[j + dx] - step, closes[j + dy * nx] - step});
                        update(from, deadline);
                    }
                }
                const int dash_steps = int(c.dash_pre + c.dash_t + c.dash_post);
                const int reserve = int(imax(dash_steps + 2, c.dash_cd));
                for (int from : p.jumps[j]) {
                    const int deadline = std::min({when - reserve, closes[j] - dash_steps,
                                                  closes[from] - int(c.dash_pre) - 1});
                    update(from, deadline);
                }
            }
        }
    }

    using Queue = std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>,
                                      std::greater<std::pair<int, int>>>;

    void flood(std::vector<int>& dist, Queue& q) const {
        const Phase& p = terrain[live_phase];
        const Phase& soon = phase(t + 9);
        const auto relax = [&](int cost, int to) {
            if (cost < dist[to]) {
                dist[to] = cost;
                q.emplace(cost, to);
            }
        };
        while (!q.empty()) {
            const auto [len, j] = q.top();
            q.pop();
            if (len != dist[j]) continue;
            const int x = j % nx, y = j / nx;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((!dx && !dy) || x + dx < 0 || x + dx >= nx || y + dy < 0 || y + dy >= ny) continue;
                    const int to = j + dx + nx * dy;
                    if (!p.ok[to] || !soon.ok[to]) continue;
                    if (dx && dy && (!p.ok[j + dx] || !p.ok[j + dy * nx])) continue;
                    const int cost = (dx && dy ? 14 : 10) + std::max(0, 5 - p.edge[to] / 110);
                    relax(len + cost, to);
                }
            }
            for (int to : p.jumps[j])
                if (soon.ok[to]) relax(len + 112, to);
        }
    }

    int navval(const std::vector<int>& dist, V q) const {
        const int j = cell(q);
        if (j < 0 || dist.empty()) return Far;
        if (dist[j] < Far) return dist[j];
        int val = Far;
        const int x = j % nx, y = j / nx;
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                if (x + dx < 0 || y + dy < 0 || x + dx >= nx || y + dy >= ny) continue;
                const int a = j + dy * nx + dx;
                val = std::min(val, dist[a] + (std::abs(dx) + std::abs(dy)) * 12);
            }
        }
        return val;
    }

    void make_escape() {
        const auto& now = terrain[live_phase];
        const auto& final = terrain.back();
        Queue q;
        escape.assign(cells, Far);
        for (int j = 0; j < cells; ++j) {
            if (!now.ok[j] || !final.ok[j]) continue;
            const int cost = std::max(0, 16 - final.edge[j] / 55);
            escape[j] = cost;
            q.emplace(cost, j);
        }
        if (q.empty()) {
            for (int j = 0; j < cells; ++j) {
                if (!now.ok[j]) continue;
                const int cost = int((node(j) - haven).len() / 25);
                escape[j] = cost;
                q.emplace(cost, j);
            }
        }
        flood(escape, q);
    }

    void make_route(V goal, int target, int mode) {
        if (route_phase == live_phase && t - route_time < 5 && target == route_target &&
            mode == route_mode && (goal - route_goal).sq() < 650 * 650) return;
        route_phase = live_phase;
        route_time = t;
        route_goal = goal;
        route_target = target;
        route_mode = mode;
        const Phase& current = terrain[live_phase];
        const Phase& soon = phase(t + 9);
        Queue q;
        route.assign(cells, Far);
        i64 best = std::numeric_limits<i64>::max();
        for (int j = 0; j < cells; ++j)
            if (current.ok[j] && soon.ok[j]) best = std::min(best, (node(j) - goal).sq());
        const int radius = mode == 1 ? int(c.pick_r) - 35 : Grid;
        const i64 span = std::max<i64>(Grid, radius);
        const i64 limit = std::max(best, span * span);
        for (int j = 0; j < cells; ++j) {
            if (!current.ok[j] || !soon.ok[j]) continue;
            if ((node(j) - goal).sq() <= limit) {
                route[j] = 0;
                q.emplace(0, j);
            }
        }
        flood(route, q);
    }

    V nav_way(const std::vector<int>& dist, V fallback) const {
        const int j = cell(origin);
        if (j < 0 || dist.empty()) return fallback;
        int val = navval(dist, origin);
        if (val == 0 && !pit(fallback, t)) return fallback;
        V best = origin;
        const int x = j % nx, y = j / nx;
        for (int dy = -3; dy <= 3; ++dy) {
            for (int dx = -3; dx <= 3; ++dx) {
                if (x + dx < 0 || y + dy < 0 || x + dx >= nx || y + dy >= ny) continue;
                const int to = j + dy * nx + dx;
                if (dist[to] >= val) continue;
                const V dst = node(to);
                const V mid = (origin + dst) / 2;
                if (pit(mid, t + 2) || circle_hits_wall(map(t), c.body_r, mid.x, mid.y)) continue;
                best = dst;
                val = dist[to];
            }
        }
        if (dist[j] < Far && ob->me().cd <= 2) {
            for (int to : terrain[live_phase].jumps[j]) {
                if (dist[to] + 85 < val) {
                    val = dist[to] + 85;
                    best = node(to);
                }
            }
        }
        return best == origin ? fallback : best;
    }

    bool dash_move(const Frame& p) const {
        return p.st == S_DASH && p.timer > c.dash_post && p.timer <= c.dash_post + c.dash_t;
    }

    Frame frame(const Player& p) const {
        return {pos(p), vel(p), aim(p), int(p.st), int(p.timer)};
    }

    void start(Frame& p, int act, V a, bool has_dart) const {
        p.a = a.unit();
        if (act == A_DASH) { p.st = S_DASH; p.timer = int(c.dash_pre + c.dash_t + c.dash_post); }
        if (act == A_THROW) { p.st = S_THROW; p.timer = int(c.throw_pre); }
        if (act == A_MELEE) {
            p.st = has_dart ? S_SLASH : S_KICK;
            p.timer = int(has_dart ? c.slash_pre : c.kick_pre);
        }
        if (act == A_RECALL) { p.st = S_RECALL; p.timer = int(c.recall_pre); }
    }

    bool advance(Frame& p, V m, int at) const {
        V speed = p.v;
        const bool dash = dash_move(p);
        const bool defer_fall = dash && p.timer > c.dash_post + 1;
        if (dash) {
            speed = {rdiv(p.a.x * c.dash_v, UNIT), rdiv(p.a.y * c.dash_v, UNIT)};
        } else if (p.st != S_STUN) {
            const int pct = p.st == S_FREE ? 100 : (p.st == S_THROW || p.st == S_THROWN ? int(c.throw_move) : 0);
            V target;
            if (pct && m.any()) target = m.unit(rdiv(c.run_v * pct * imin(UNIT, ilen(m.x, m.y)), 100 * UNIT));
            V delta = target - speed;
            cap_len(delta.x, delta.y, c.acc);
            speed = speed + delta;
        }
        const Phase& terrain = phase(at);
        if (wall_possible(terrain, p.x, p.x + speed, c.body_r + 2))
            move_body(terrain.map, c.body_r, p.x.x, p.x.y, speed.x, speed.y);
        else
            p.x = p.x + speed;
        p.v = speed;
        if (dash && p.timer == c.dash_post + 1) p.v = speed * c.run_v / imax(1, c.dash_v);
        if (p.st == S_STUN) p.v = {rdiv(speed.x * c.kick_keep, UNIT), rdiv(speed.y * c.kick_keep, UNIT)};
        if (p.timer > 0 && --p.timer == 0) {
            if (p.st == S_THROW && c.throw_post) { p.st = S_THROWN; p.timer = int(c.throw_post); }
            else if (p.st == S_SLASH && c.slash_post) { p.st = S_SLASHED; p.timer = int(c.slash_post); }
            else if (p.st == S_KICK && c.kick_post) { p.st = S_KICKED; p.timer = int(c.kick_post); }
            else p.st = S_FREE;
        }
        return defer_fall || p.st == S_STUN || !pit(p.x, at);
    }

    Track self_track(const Plan& plan, int length) const {
        Track out;
        Frame p = frame(ob->me());
        out.f[0] = p;
        for (int h = 0; h < length; ++h) {
            const V m = plan.movement(h);
            int act = h == 0 ? plan.act : A_NONE;
            V a = plan.action;
            if (h == plan.melee_at) { act = A_MELEE; a = plan.next; }
            if (p.st == S_RECALL && (m.any() || (act && act != A_RECALL))) {
                p.st = S_FREE;
                p.timer = 0;
            }
            if (act != A_NONE) start(p, act, a, armed);
            if (p.timer == 1) {
                if (p.st == S_THROW) out.wind = h + 1;
                if (p.st == S_SLASH || p.st == S_KICK) {
                    out.swing = h + 1;
                    out.slash = p.st == S_SLASH;
                }
            }
            if (!advance(p, m, t + h)) {
                out.fall = h + 1;
                out.f[h + 1] = p;
                for (int j = h + 2; j <= length; ++j) out.f[j] = p;
                break;
            }
            out.f[h + 1] = p;
        }
        return out;
    }

    const History* past(i64 at) const {
        if (at < 0) return nullptr;
        const History& h = history[at % history.size()];
        return h.t == at ? &h : nullptr;
    }

    V my_past(int at) const {
        if (const auto* h = past(std::max(0, at))) return pos(h->p);
        return origin;
    }

    // Motion estimates use the other player's last free or throwing velocity.
    V keep(int k) const {
        const Seen& s = seen[k];
        V v = s.velocity;
        if (!v.any()) return {};
        const i64 speed = imin(UNIT, i64(v.len() * UNIT / imax(1, c.run_v) + 35));
        if (s.p.st == S_THROW || s.p.st == S_THROWN)
            return v.unit(imin(UNIT, speed * 100 / imax(1, c.throw_move)));
        return v.unit(speed);
    }

    void infer_hidden(int k, int when) {
        Seen& s = seen[k];
        if (s.fog_at == when) return;
        const bool carrying = s.fog_at + 1 == when && s.fog_focus > 0.;
        s.fog_at = when;
        s.fog_focus = 0.;
        const auto& p = phase(when);
        if (p.grass.empty()) return;
        if (s.fog.empty()) s.fog.resize(cells);
        fog_scratch.assign(cells, 0);
        const V observer = my_past(when);
        const V last = s.t >= 0 ? pos(s.p) : V{gi.spx[k], gi.spy[k]};
        const int wait = s.t >= 0 ? std::max(1, when - int(s.t)) : when + 1;
        const i64 see_limit = imax(0, c.see_r - Grid / 2);
        const i64 reach = std::min<i64>(gi.M().width() + gi.M().height(), Grid + c.run_v * wait);
        const V guess = last + s.velocity * std::min(4, wait);
        float largest = 0;
        double sum = 0, wx = 0, wy = 0;
        int peak = -1;
        const auto put = [&](int j, V at, float mass) {
            if (mass <= 0) return;
            fog_scratch[j] = mass;
            if (mass > largest) { largest = mass; peak = j; }
            sum += mass;
            wx += mass * double(at.x);
            wy += mass * double(at.y);
        };
        for (int j : p.grass) {
            const V at = node(j);
            if ((at - observer).sq() <= see_limit * see_limit) continue;
            if (carrying) {
                float mass = .49f * s.fog[j];
                const int x = j % nx, y = j / nx;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if ((!dx && !dy) || x + dx < 0 || x + dx >= nx || y + dy < 0 || y + dy >= ny) continue;
                        const float transfer = dx && dy ? .034f : .094f;
                        mass += transfer * s.fog[j + dy * nx + dx];
                    }
                }
                put(j, at, mass);
            } else if ((at - last).sq() <= reach * reach) {
                const double sigma = 170. + 95. * std::min(wait, 15);
                put(j, at, float(1. / (1. + (at - guess).sq() / (sigma * sigma))));
            }
        }
        if (!sum) {
            for (int j : p.grass) {
                const V at = node(j);
                if ((at - observer).sq() > see_limit * see_limit)
                    put(j, at, float(1. / (1. + (at - last).sq() / 1.1e6)));
            }
        }
        if (!sum || peak < 0) return;
        const V mean{i64(wx / sum), i64(wy / sum)};
        const V center = node(peak);
        double near = 0.;
        double closest = 1e20, medial = 1e20;
        V close = center, middle = center;
        const float norm = float(1. / sum);
        for (int j : p.grass) {
            const float mass = fog_scratch[j];
            if (!mass) continue;
            fog_scratch[j] *= norm;
            const V at = node(j);
            if ((at - center).sq() < 1100 * 1100) near += mass;
            if (mass < largest * .20f) continue;
            const double cost = (at - observer).len() - 420. * mass / largest;
            if (cost < closest) { closest = cost; close = at; }
            const double mid_score = (at - mean).len() - 375. * mass / largest;
            if (mid_score < medial) { medial = mid_score; middle = at; }
        }
        s.fog.swap(fog_scratch);
        s.fog_focus = near / sum;
        s.fog_point = {center, middle, close};
        if (when == 0 && s.t < 0 && (center - last).sq() < 400 * 400)
            s.fog_point = {last, last, last};
    }

    void remember(const Obs& o, int when) {
        for (int k = 0; k < gi.n; ++k) {
            if (k == me || seen[k].t == when) continue;
            if (!o.p[k].vis) {
                if (o.p[k].alive) infer_hidden(k, when);
                continue;
            }
            const Player& p = o.p[k];
            Seen& s = seen[k];
            s.fog_focus = 0;
            const auto& sample = s.motion_sample[when % s.motion_sample.size()];
            if (sample.due == when && p.alive && p.st == S_FREE) {
                const int idx = sample.armed ? 1 : 0;
                const double rate = s.motion_count[idx] < 4 ? .20 : .115;
                for (int m = 0; m < Models; ++m) {
                    const double miss = std::min(2400., (sample.x[m] - pos(p)).len());
                    s.motion_error[idx][m] += rate * (miss - s.motion_error[idx][m]);
                }
                ++s.motion_count[idx];
            }
            if (p.alive && p.st == S_THROW && s.p.st != S_THROW && !is_mate(gi, k)) {
                const int when_aimed = std::max(0, when - 1 - int(c.delay));
                const History* h = past(when_aimed);
                const V from = pos(p);
                const V dir = aim(p);
                if (h) {
                    const V ours = pos(h->p) - from;
                    const double distance = ours.len();
                    const double miss = std::abs(double(ours.dot(dir.left()))) / UNIT;
                    bool toward_us = distance > 1000 && distance < 8300 && ours.dot(dir) > distance * 890 && miss < 1900;
                    for (int j = 0; j < gi.n && toward_us; ++j) {
                        if (j == k || j == me || !o.p[j].alive || !o.p[j].vis) continue;
                        const V other = pos(o.p[j]) - from;
                        if (other.dot(dir) > 0 && other.sq() < ours.sq() * 1.5 &&
                            std::abs(double(other.dot(dir.left()))) / UNIT < miss * .66) toward_us = false;
                    }
                    if (toward_us) {
                        const auto options = leads();
                        for (int j = 0; j < int(options.size()); ++j) {
                            const V predicted = ours + vel(h->p) * options[j];
                            const double error = std::min(2700., std::abs(double(predicted.dot(dir.left()))) / UNIT);
                            s.lead_error[j] += (error - s.lead_error[j]) * (s.shots ? .28 : 1.0);
                        }
                        ++s.shots;
                    }
                }
            }
            if (s.t + 1 == when && p.st == S_FREE && s.p.st == S_FREE) {
                const V v0 = s.velocity;
                const V v1 = vel(p);
                if (v0.sq() > 70 * 70 && v1.sq() > 70 * 70) {
                    const double change = 1 - v0.dot(v1) / (v0.len() * v1.len());
                    s.turning = s.turning * .94 + .06 * change;
                }
                s.idle = s.idle * .95 + .05 * (v1.sq() < 45 * 45);
            }
            if (p.st == S_FREE || p.st == S_THROW || p.st == S_THROWN) s.velocity = vel(p);
            s.p = p;
            s.t = when;
        }
    }

    Game align(const Obs& o, int old) {
        Obs past_obs = o;
        past_obs.t = old;
        Game g(gi, past_obs);
        Action a[MAXP]{};
        for (int k = 0; k < gi.n; ++k) {
            if (k == me) continue;
            const V dir = keep(k);
            if (o.p[k].st != S_RECALL) { a[k].mx = dir.x; a[k].my = dir.y; }
        }
        for (int h = old; h < t; ++h) {
            if (const auto* exact = past(h)) {
                g.p[me] = exact->p;
                g.p[me].team = gi.team[me];
                g.d[me] = exact->d;
                a[me] = exact->a;
            }
            g.over = false;
            const History* next = h + 1 == t ? nullptr : past(h + 1);
            const Dart& observed = next ? next->d : o.my_dart();
            if (dart_flying(g.d[me].st)) {
                Game prior = g;
                g.step(a);
                if (g.d[me].st == D_GROUND && observed.st != D_GROUND) {
                    bool rejected_clash = false;
                    for (int k = 0; k < gi.n; ++k) {
                        if (k == me || !dart_flying(prior.d[k].st) || g.d[k].st != D_GROUND) continue;
                        if ((pos(g.d[k]) - pos(g.d[me])).sq() < 1150 * 1150) rejected_clash = true;
                    }
                    if (rejected_clash) {
                        // Our actual dart did not drop on this step, so undo a guessed clash.
                        g = std::move(prior);
                        g.d[me].st = D_GONE;
                        g.step(a);
                    }
                }
            } else {
                g.step(a);
            }
            if (c.mode == M_DUEL) {
                Player& rival = g.p[1 - me];
                if (!rival.alive && o.p[1 - me].alive) {
                    rival.alive = 1;
                    rival.st = S_FREE;
                    rival.timer = 0;
                    if (pit(pos(rival), h)) {
                        const V before{rival.px, rival.py};
                        rival.x = before.x;
                        rival.y = before.y;
                    }
                    Dart& dart = g.d[1 - me];
                    if (dart.st == D_GONE && o.d[1 - me].st == D_HAND) {
                        dart.st = D_HAND;
                        dart.x = rival.x;
                        dart.y = rival.y;
                    }
                }
            }
        }
        g.over = false;
        infer_clash(o, g, old);
        g.p[me] = o.me();
        g.p[me].team = gi.team[me];
        g.d[me] = o.my_dart();
        return g;
    }

    void infer_clash(const Obs& o, Game& g, int old) {
        const History* previous = past(t - 1);
        if (o.my_dart().st == D_GROUND && previous && dart_flying(previous->d.st)) {
            const V a = pos(previous->d), b = pos(o.my_dart());
            const V previous_dir = V{previous->d.vx, previous->d.vy};
            V direction = previous->d.st == D_OUT ? previous_dir :
                (origin - a).unit();
            const V ray_end = b + direction.unit(15);
            i64 wx, wy;
            const bool by_wall = !(previous->d.st == D_CALL && c.recall_wall) &&
                seg_wall(map(t - 1), a.x, a.y, ray_end.x, ray_end.y, wx, wy) &&
                (V{wx, wy} - b).sq() < 20 * 20;
            if (!by_wall) {
                int candidate = -1;
                double value = 3900;
                for (int k = 0; k < gi.n; ++k) {
                    if (k == me || o.d[k].st == D_GONE) continue;
                    const Dart& d = g.d[k];
                    const bool flight = dart_flying(d.st) || dart_flying(o.d[k].st);
                    const V other = (flight || d.st == D_GROUND) ? pos(d) : pos(g.p[k]);
                    const double dd = (b - other).len();
                    if (!flight && o.d[k].st != D_HAND && o.d[k].st != D_RISE) continue;
                    if (!flight && dd <= c.slash_reach + 250) continue;
                    if (flight && dd > 3000) continue;
                    const double rank = flight ? dd * .6 : dd * .38 + 1550.;
                    if (rank < value) { value = rank; candidate = k; }
                }
                if (candidate >= 0) {
                    clash_t = t;
                    clash_at = b;
                    clash_enemy = candidate;
                }
            }
        }
        if (clash_enemy >= 0 && clash_t > old) {
            Dart& d = g.d[clash_enemy];
            const Dart& observed = o.d[clash_enemy];
            if (d.st != D_GONE && observed.st != D_GROUND && (pos(d) - clash_at).sq() < 8300 * 8300) {
                if (d.st != D_GROUND || (pos(d) - clash_at).sq() > 1400 * 1400) {
                    d.x = clash_at.x;
                    d.y = clash_at.y;
                }
                d.st = D_GROUND;
                d.age = d.left = d.vx = d.vy = 0;
            }
        }
    }

    void predict(int k, int old, const Game& aligned) {
        Forecast& out = foe[k];
        out.alive = ob->p[k].alive != 0;
        out.visible = ob->p[k].vis != 0;
        out.mate = is_mate(gi, k);
        out.armed = aligned.d[k].st == D_HAND;
        out.rearm = out.armed ? 0 : H + 1;
        out.prey = me;
        out.attention = 1.;
        if (!out.alive) return;
        Player src = ob->p[k];
        int start_at = old;
        Seen& s = seen[k];
        int hidden = 0;
        if (!out.visible) {
            hidden = s.t >= 0 ? std::max(0, old - int(s.t)) : 40;
            if (s.fog_at != old || s.fog_focus <= 0.) { out.alive = false; return; }
            src = s.p;
            src.st = S_FREE;
            src.timer = 0;
            src.x = s.fog_point[0].x;
            src.y = s.fog_point[0].y;
            src.vx = src.vy = 0;
        }
        out.confidence = out.visible ? 1.0 : .32 + .44 * s.fog_focus;
        V drift = keep(k);
        if (!out.visible) drift = hidden <= 5 ? drift / 2 : V{};
        V target = origin;
        const double our_distance = (target - pos(src)).len();
        double close = our_distance;
        for (int j = 0; j < gi.n; ++j) {
            if (j == k || j == me || !ob->p[j].alive || !ob->p[j].vis || gi.team[j] == gi.team[k]) continue;
            const V alternate = pos(ob->p[j]);
            const double d = (alternate - pos(src)).len();
            if (d + 400. > close * .89 || blocked(old, pos(src), alternate)) continue;
            close = d;
            target = alternate;
            out.prey = j;
        }
        if (out.prey != me) {
            const double face_align = (target - pos(src)).dot(vel(src).unit()) / std::max(1., close) / UNIT;
            out.attention = std::clamp(.83 - .31 * ramp(our_distance - close, 400., 2800.) -
                .20 * ramp(face_align, .0, .85), .36, .82);
        }
        const bool gathering = aligned.d[k].st == D_GROUND && (pos(aligned.d[k]) - pos(src)).sq() < 5800 * 5800;
        const V recover = gathering ? pos(aligned.d[k]) : target;
        const double stable = 1 - ramp(s.turning, .03, .31);
        const bool still = !drift.any();
        const V bearing = (target - pos(src)).unit();
        const bool running_in = drift.dot(bearing) > 650 * UNIT;
        const bool running_out = drift.dot(bearing) < -450 * UNIT;
        out.prob = {.26 + .26 * stable, running_in ? .235 : .12, .10 + (still ? .23 : s.idle * .16),
                    .11, .11, running_out ? .18 : .085};
        if (drift.dot(bearing.left()) > 590 * UNIT) out.prob[3] += .10;
        if (drift.dot(bearing.left()) < -590 * UNIT) out.prob[4] += .10;
        if (out.mate) out.prob = {.66, .08, .10, .06, .06, .04};
        const int role = out.armed ? 1 : 0;
        if (!out.mate && s.motion_count[role] >= 4) {
            const auto& err = s.motion_error[role];
            const double best_error = *std::min_element(err.begin(), err.end());
            for (int m = 0; m < Models; ++m)
                out.prob[m] *= .33 + 1.32 / (1. + (err[m] - best_error) / 240.);
        }
        double norm = 0;
        for (double x : out.prob) norm += x;
        for (double& x : out.prob) x /= norm;
        // Continue, pursue or recover, brake, strafe either way, and retreat.
        for (int m = 0; m < Models; ++m) {
            Track& tr = out.tracks[m];
            tr.fall = H + 1;
            tr.swing = -1;
            tr.wind = -1;
            Frame f = frame(src);
            if (!out.visible) f.x = s.fog_point[m % int(s.fog_point.size())];
            if (m == 0 && out.visible && aligned.p[k].alive) {
                f = frame(aligned.p[k]);
                start_at = t;
            } else start_at = old;
            for (int at = start_at; at < t + H; ++at) {
                if (at == t) tr.f[0] = f;
                const Frame prev = f;
                V dir = drift;
                const V toward = (target - f.x).unit();
                if (m == 1) dir = gathering ? (recover - f.x).unit() : toward;
                if (m == 2) dir = {};
                if (m == 3) dir = turn(toward, 220, 975);
                if (m == 4) dir = turn(toward, 220, -975);
                if (m == 5) dir = -toward;
                if (at < old + 2 && m >= 1) dir = (drift * 2 + dir) / 3;
                if (at >= t && f.timer == 1 && (f.st == S_SLASH || f.st == S_KICK)) {
                    tr.swing = at - t + 1;
                    tr.slash = f.st == S_SLASH;
                }
                if (at >= t && f.timer == 1 && f.st == S_THROW) tr.wind = at - t + 1;
                if (!advance(f, dir, at)) {
                    if (prev.st == S_STUN || prev.st == S_DASH || pit(prev.x, at)) {
                        if (at >= t) tr.fall = std::min(tr.fall, at - t + 1);
                    } else {
                        f.x = prev.x;
                        f.v = {};
                    }
                }
                if (at >= t) tr.f[at - t + 1] = f;
            }
        }
        if (gathering) {
            const double radius = c.pick_r + 210.;
            for (int h = 0; h <= H; ++h) {
                if ((out.tracks[0].f[h].x - recover).sq() <= radius * radius ||
                    (out.tracks[1].f[h].x - recover).sq() <= radius * radius) {
                    out.rearm = h;
                    break;
                }
            }
        }
        if (out.visible && !out.mate && src.st == S_FREE && !pit(pos(src), old)) {
            const int due = t + 2;
            auto& observation = s.motion_sample[due % s.motion_sample.size()];
            observation.due = due;
            observation.armed = ob->d[k].st == D_HAND;
            for (int m = 0; m < Models; ++m) observation.x[m] = out.tracks[m].f[2].x;
        }
    }

public:
    void init(const GameInfo& data) {
        gi = data;
        c = gi.cfg;
        me = gi.seat;
        nx = gi.M().nx * 2 + 1;
        ny = gi.M().ny * 2 + 1;
        cells = nx * ny;
        movement_rng = SplitMix64(u64(gi.spx[me] * 17239 + gi.spy[me] * 47197 + 97 * gi.M().width() + me * 709));
        pivot_sign = movement_rng.next() & 1 ? 1 : -1;
        for (i64 left = c.kick_stun, speed = c.kick_v; left > 0 && speed > 0; --left) {
            kick_slide += speed;
            const i64 next = rdiv(speed * c.kick_keep, UNIT);
            if (next == speed) {
                kick_slide += speed * (left - 1);
                break;
            }
            speed = next;
        }
        for (int k = 0; k < Moves; ++k) {
            const int angle = k * 36000 / Moves;
            compass[k] = {Game::icos_face(angle), Game::isin_face(angle)};
        }
        make_phase(0);
        const int end = int(imin(c.steps + H, c.col_start + imax(0, gi.M().nwave - 1) * c.col_every));
        for (int w = 0; w < gi.M().nwave; ++w) {
            const i64 at = wave_fall_step(c, w);
            if (at > end) break;
            if (at > 0) make_phase(int(at));
        }
        phase_at.resize(std::max(1, end + 1));
        int k = 0;
        for (int j = 0; j <= end; ++j) {
            if (k + 1 < int(terrain.size()) && terrain[k + 1].start <= j) ++k;
            phase_at[j] = k;
        }
        V total{};
        i64 weight = 0;
        for (int j = 0; j < cells; ++j) {
            if (!terrain.back().ok[j]) continue;
            const int w = std::min(1500, terrain.back().edge[j]);
            total = total + node(j) * w;
            weight += w;
        }
        haven = weight > 0 ? total / weight : V{gi.M().width() / 2, gi.M().height() / 2};
        // Precompute terrain-dependent jumps while the startup CPU budget applies.
        for (int i = 0; i < int(terrain.size()); ++i) build_jumps(i);
        build_departures();
        rays.reserve(64);
    }

private:
    Ray fly(int owner, Dart d, int first, const Track& target, double weight, int horizon = H) const {
        Ray ray;
        ray.first = first;
        ray.last = first - 1;
        ray.owner = owner;
        ray.weight = weight;
        ray.x[first - 1] = pos(d);
        for (int h = first; h <= horizon; ++h) {
            const V a = pos(d);
            V b = a;
            i64 speed = dart_step_len(c, d);
            bool arrived = false;
            const bool outgoing = d.st == D_OUT;
            if (outgoing) {
                speed = imin(speed, d.left);
                b.x += rdiv(d.vx * speed, UNIT);
                b.y += rdiv(d.vy * speed, UNIT);
                d.left -= speed;
            } else if (speed > 0) {
                const V delta = target.f[h].x - a;
                const i64 length = ilen(delta.x, delta.y);
                if (length <= speed) {
                    b = target.f[h].x;
                    arrived = true;
                } else {
                    b.x += rdiv(delta.x * speed, length);
                    b.y += rdiv(delta.y * speed, length);
                }
            }
            const bool stopped = !(d.st == D_CALL && c.recall_wall) && clip(t + h - 1, a, b);
            ray.x[h] = b;
            ray.last = h;
            if (stopped) {
                ray.finish = D_GROUND;
                break;
            }
            if (outgoing && d.left <= 0) {
                d.st = D_BACK;
                d.age = 0;
            } else {
                ++d.age;
            }
            if (!outgoing && (arrived ||
                seg_near_origin(a.x - target.f[h - 1].x.x, a.y - target.f[h - 1].x.y,
                                b.x - target.f[h].x.x, b.y - target.f[h].x.y, c.catch_r))) {
                ray.finish = D_HAND;
                break;
            }
            d.x = b.x;
            d.y = b.y;
        }
        return ray;
    }

    void known_rays(const Game& g) {
        const i64 range = c.range + (c.delay + H) * c.ret_v;
        for (int k = 0; k < gi.n; ++k) {
            if (k == me) continue;
            Forecast& f = foe[k];
            const Dart& raw = g.d[k];
            if ((pos(raw) - origin).sq() > range * range) continue;
            if (dart_flying(raw.st)) {
                if (!f.alive) {
                    Track fixed;
                    for (auto& step : fixed.f) step.x = pos(g.p[k]);
                    rays.push_back(fly(k, raw, 1, fixed, .93));
                } else {
                    const Ray main = fly(k, raw, 1, f.tracks[0], raw.st == D_OUT ? .78 : .64);
                    rays.push_back(main);
                    if (raw.st == D_OUT && (pos(raw) - origin).sq() < 6500 * 6500) {
                        const i64 error = ob->d[k].st == D_HAND ? 175 : 105;
                        const V offset = V{raw.vx, raw.vy}.left().unit(error);
                        for (int sign : {-1, 1}) {
                            Dart shifted = raw;
                            shifted.x += offset.x * sign;
                            shifted.y += offset.y * sign;
                            if (map(t).at(cell_index(shifted.x), cell_index(shifted.y)) == C_WALL) continue;
                            rays.push_back(fly(k, shifted, 1, f.tracks[0], .135));
                        }
                    } else if (raw.st != D_OUT) {
                        rays.push_back(fly(k, raw, 1, f.tracks[2], .17));
                        rays.push_back(fly(k, raw, 1, f.tracks[1], .15));
                    }
                    if (main.finish == D_HAND) f.rearm = std::min(f.rearm, main.last);
                }
            } else if (f.alive && g.p[k].alive && raw.st == D_RISE && g.p[k].timer <= H) {
                Dart d = raw;
                d.st = D_CALL;
                d.age = 0;
                rays.push_back(fly(k, d, std::max(1, int(g.p[k].timer)), f.tracks[0], .80));
            } else if (f.alive && g.p[k].alive && raw.st == D_HAND && g.p[k].st == S_THROW &&
                       f.tracks[0].wind >= 0 && f.tracks[0].wind < H) {
                const int release = f.tracks[0].wind;
                for (int m : {0, 2}) {
                    Dart d;
                    d.st = D_OUT;
                    d.x = f.tracks[m].f[release].x.x;
                    d.y = f.tracks[m].f[release].x.y;
                    d.vx = g.p[k].ax;
                    d.vy = g.p[k].ay;
                    d.left = c.range;
                    rays.push_back(fly(k, d, release + 1, f.tracks[m], m == 0 ? .73 : .19));
                }
            }
        }
    }

    void latent_ray(int k, int fire, V source, V dir, double weight) {
        Ray ray;
        ray.owner = k;
        ray.first = std::max(1, fire - t + 1);
        ray.last = ray.first - 1;
        ray.latent = true;
        ray.weight = weight;
        ray.launched = fire - int(c.throw_pre);
        ray.direction = dir;
        i64 left = c.range;
        for (int h = fire - t + 1, age = 0; h <= std::min(H, 12) && left > 0; ++h, ++age) {
            const i64 step = imin(left, imax(c.dart_vmin, c.dart_v - age * c.dart_dec));
            V end{source.x + rdiv(dir.x * step, UNIT), source.y + rdiv(dir.y * step, UNIT)};
            const bool hit_wall = clip(t + h - 1, source, end);
            if (h <= 0) {
                const V rel0 = source - my_past(t + h - 1), rel1 = end - my_past(t + h);
                // A hypothetical shot that should already have hit is inconsistent with our observation.
                if (seg_near_origin(rel0.x, rel0.y, rel1.x, rel1.y, c.body_r + c.dart_r)) return;
            } else {
                ray.x[h - 1] = source;
                ray.x[h] = end;
                ray.last = h;
            }
            if (hit_wall) break;
            source = end;
            left -= step;
        }
        if (ray.last >= ray.first) rays.push_back(ray);
    }

    int ready_time(int k, int old) const {
        const Forecast& f = foe[k];
        const Player& p = ob->p[k];
        if (!f.alive || p.st == S_THROW || p.st == S_RECALL) return t + H;
        const int free = old + (p.st == S_FREE ? 0 : int(p.timer));
        if (ob->d[k].st == D_HAND && f.armed) return free;
        if (f.armed) return std::max(free, t - 2);
        return std::max(free, t + f.rearm);
    }

    bool hidden_from(V observer, int when) const {
        const History* own = past(when);
        if (!own || own->p.expose || dart_flying(own->d.st)) return false;
        const V obs_pos = pos(own->p);
        return (obs_pos - observer).sq() > (c.see_r + 175) * (c.see_r + 175) &&
            cell_of_point(c, gi.M(), obs_pos.x, obs_pos.y, when) == C_GRASS;
    }

    void latent_rays(int old) {
        for (int k = 0; k < gi.n; ++k) {
            const Forecast& f = foe[k];
            if (k == me || !f.alive || f.mate) continue;
            const int ready = ready_time(k, old);
            if (ready > t + 4) continue;
            const V source = f.tracks[0].f[0].x;
            const i64 max_range = imin(c.range + 800, 6850 + (c.run_v - 250) * 3);
            if ((source - origin).sq() > max_range * max_range) continue;
            const V v = f.visible ? keep(k) : V{};
            const Seen& s = seen[k];
            const auto l = leads();
            std::array<double, 4> weights{.28, .23, .34, .15};
            if (s.shots) {
                const double err = *std::min_element(s.lead_error.begin(), s.lead_error.end());
                for (int j = 0; j < int(weights.size()); ++j)
                    weights[j] *= .24 + .96 / (1. + (s.lead_error[j] - err) / 210.);
            }
            const int finish = std::max(ready, t + 1);
            const int begin_at = std::max(ready, t - 5);
            const int stride = finish - begin_at > 4 ? 2 : 1;
            for (int begin = begin_at; begin <= finish; begin += stride) {
                const int fire = begin + int(c.throw_pre);
                V from = source + v * ((begin - t) * c.run_v + c.throw_pre * c.run_v * c.throw_move / 100) / UNIT;
                if (begin == old && ob->p[k].vis) from = pos(ob->p[k]) + v * (c.throw_pre * c.run_v * c.throw_move / 100) / UNIT;
                if (pit(from, fire) || circle_hits_wall(map(fire), c.body_r, from.x, from.y)) from = source;
                const int obs_time = std::max(0, begin - int(c.delay));
                const V obs_pos = my_past(obs_time);
                const History* own = past(obs_time);
                if (hidden_from(from, obs_time)) continue;
                const V mv = own ? vel(own->p) : vel(ob->me());
                const double w = (crowd ? .092 : .118) * f.confidence * f.attention * (begin > t ? .65 : 1.);
                std::array<V, 4> dir{};
                std::array<double, 4> mass{};
                int count = 0;
                for (int j = 0; j < int(l.size()); ++j) {
                    const V a = (obs_pos + mv * l[j] - from).unit();
                    if (!a.any()) continue;
                    int same = 0;
                    while (same < count && (dir[same] - a).sq() > 85 * 85) ++same;
                    mass[same] += weights[j];
                    if (same == count) dir[count++] = a;
                }
                for (int j = 0; j < count; ++j) latent_ray(k, fire, from, dir[j], w * mass[j]);
                if (!f.visible && (begin == begin_at || begin + stride > finish)) {
                    const V secondary = f.tracks[2].f[0].x;
                    if ((secondary - from).sq() > 390 * 390 && !hidden_from(secondary, obs_time)) {
                        const V aim = (obs_pos + mv * (c.delay + 3) - secondary).unit();
                        if (aim.any()) latent_ray(k, fire, secondary, aim, w * .47);
                    }
                }
                if (crowd && f.prey != me && f.prey >= 0 && foe[f.prey].visible &&
                    (begin == begin_at || begin + stride > finish)) {
                    const auto& rival = foe[f.prey].tracks[0];
                    const V mark = rival.f[std::clamp(begin - t + 3, 0, H)].x;
                    const V toward = mark - from;
                    if (toward.sq() < 5600 * 5600 &&
                        swept(from - origin, mark - origin) < 2300. * 2300. &&
                        !blocked(fire, from, mark)) {
                        latent_ray(k, fire, from, toward.unit(), .072 * (1. - .5 * f.attention));
                    }
                }
            }
        }
    }

    void latent_rushes(int old) {
        if (!c.dash_slash || c.dash_v <= 0) return;
        for (int k = 0; k < gi.n; ++k) {
            const Forecast& f = foe[k];
            const Player& q = ob->p[k];
            if (k == me || !f.alive || f.mate || !f.visible) continue;
            const int can_act = old + (q.st == S_FREE ? 0 : int(q.timer));
            if (can_act > t + 2 || old + q.cd > t + 2) continue;
            const int armed_at = ready_time(k, old);
            if (armed_at > t + 2 && !f.armed && edge(origin, t + 13) > c.kick_v * 5) continue;
            const V anchor = f.tracks[0].f[0].x;
            const double dist = (anchor - origin).len();
            if (dist > c.dash_v * c.dash_t + c.slash_reach + 1850 || dist < 1100) continue;
            int begin = std::max({old, can_act, old + int(q.cd), t - 4});
            const int finish = std::max(t, begin);
            for (; begin <= finish; begin += 2) {
                const V source = begin == old ? pos(q) : anchor + keep(k) * (c.run_v * (begin - t)) / UNIT;
                if (pit(source, begin)) continue;
                const int known_t = std::max(0, begin - int(c.delay));
                if (hidden_from(source, known_t)) continue;
                const V known = my_past(known_t);
                const History* h = past(known_t);
                V toward = (known + (h ? vel(h->p) : V{}) * (c.delay + 3) - source).unit();
                if (!toward.any()) continue;
                if (blocked(begin, source, source + toward.unit(imin(c.dash_v * c.dash_t, i64(dist))))) continue;
                const bool weapon = armed_at <= begin + int(c.dash_pre + c.dash_t);
                const int swing = int(weapon ? c.slash_pre : c.kick_pre);
                const double weight = (weapon ? (armed_at <= begin ? .14 : .081) : .065) * f.attention;
                Frame dash{source, {}, {}, S_FREE, 0};
                start(dash, A_DASH, toward, weapon);
                for (int j = 0; j < c.dash_pre + c.dash_t; ++j) {
                    if (j > c.dash_pre && begin + j + swing > t) {
                        Frame stop = dash;
                        start(stop, A_MELEE, toward, weapon);
                        bool viable = true;
                        for (int a = 0; a < swing; ++a)
                            viable &= advance(stop, {}, begin + j + a);
                        const int step = begin + j + swing - t;
                        if (viable && step <= 12) rushes.push_back({k, step, stop.x, weight, weapon});
                    }
                    if (!advance(dash, {}, begin + j)) break;
                }
            }
        }
    }

    V search_cover() const {
        V result = haven;
        double best = 0;
        for (int k = 0; k < gi.n; ++k) {
            if (k == me || is_mate(gi, k) || !ob->p[k].alive) continue;
            const Seen& track = seen[k];
            const V last = track.t >= 0 ? pos(track.p) : V{gi.spx[k], gi.spy[k]};
            const double dd = (last - origin).len();
            if (dd < c.see_r * .75 || pit(last, t + 35)) continue;
            const double priority = 3500. + .1 * dd - (track.t >= 0 ? (t - track.t) * 5. : 1400.);
            if (priority <= best) continue;
            best = priority;
            result = last;
        }
        if (result == haven && (origin - haven).sq() < 2100 * 2100) {
            const Phase& p = terrain[live_phase];
            const int quarter = (t / 34 + me * 3) % Moves;
            const V probe = haven + compass[quarter].unit(3350);
            double value = 1e20;
            for (int j = 0; j < cells; j += 2) {
                if (!p.ok[j] || p.leave[j] < t + 25) continue;
                const V point = node(j);
                if (p.map.at(cell_index(point.x), cell_index(point.y)) != C_GRASS) continue;
                const double dd = (point - probe).len();
                if (dd < value) { result = point; value = dd; }
            }
        }
        return result;
    }

    void goals(const Game& g) {
        focus = -1;
        double best = 1e12;
        opponents = 0;
        for (int k = 0; k < gi.n; ++k) {
            if (k == me || !ob->p[k].alive || is_mate(gi, k)) continue;
            ++opponents;
            if (!foe[k].alive) continue;
            const V p = foe[k].tracks[0].f[0].x;
            double value = (p - origin).len();
            if (blocked(t, origin, p)) value += 800;
            if (!foe[k].visible) value += 1000;
            if (!foe[k].armed && foe[k].rearm > 4) value -= 800;
            if (g.p[k].st == S_STUN || g.p[k].st == S_RECALL) value -= 950;
            if (value < best) { best = value; focus = k; }
        }
        crowd = c.mode == M_FFA && opponents > 1;
        int mode = 0;
        const Dart& d = ob->my_dart();
        V goal = focus < 0 ? search_cover() : foe[focus].tracks[0].f[1].x;
        if (d.st == D_GROUND || d.st == D_RISE) {
            goal = pos(d);
            mode = 1;
            if (focus >= 0 && d.st == D_GROUND) {
                const auto& e = foe[focus];
                const V rival = e.tracks[0].f[0].x;
                const double ours = (goal - origin).len();
                const double ours_to_foe = (rival - origin).len();
                const double theirs = (goal - rival).len();
                const bool contested = e.rearm <= 4 && theirs < 2600 && ours > 1050 &&
                    ours_to_foe < 5700 && !blocked(t, rival, goal);
                if (contested && theirs + 800 < ours) {
                    goal = origin + (origin - rival).unit(1750);
                    mode = 2;
                }
            }
        } else if (dart_flying(d.st)) {
            goal = origin;
            if (d.st == D_BACK || d.st == D_CALL) {
                const V flight = pos(d) - origin;
                goal = origin + flight.unit(std::min<i64>(1250, ilen(flight.x, flight.y)));
            }
            if (focus >= 0 && foe[focus].armed) {
                const V danger = foe[focus].tracks[0].f[2].x - origin;
                if (danger.sq() < 4500 * 4500) goal = origin - danger.unit(2000);
            }
        } else if (focus >= 0) {
            const double d = (goal - origin).len();
            if (crowd && (haven - origin).sq() > 1600 * 1600 && d > 4200) {
                goal = (goal + origin + haven * 2) / 4;
            }
        }
        const bool lost = edge(origin, t + 13) == 0 && !pit(origin, t);
        const int safe = navval(escape, origin);
        const int depart = latest_leave(origin);
        bool urgent = lost || (safe < Far && safe > 32 && depart < t + 18 && t > c.col_start - 95);
        if (c.mode == M_DUEL)
            urgent |= t > c.col_start - 70 && safe < Far && safe > 36 && t + 12 + safe / 8 > c.col_start;
        if (urgency_override(mode, goal, safe)) urgent = true;
        make_route(goal, focus, mode);
        if (urgent && safe < Far) waypoint = nav_way(escape, haven);
        else if (navval(route, origin) >= Far) waypoint = nav_way(escape, haven);
        else waypoint = nav_way(route, goal);
    }

    bool urgency_override(int mode, V goal, int safe) const {
        return mode == 1 && pit(goal, t + 14) && safe > 20 && t + 35 >= c.col_start;
    }

    double travel(const Track& path, const Plan& plan, int depth) const {
        const V end = path.f[depth].x;
        const V mid = path.f[std::min(depth, 4)].x;
        const int rt0 = navval(route, origin), rt1 = navval(route, end);
        const int sc0 = navval(escape, origin), sc1 = navval(escape, end);
        const i64 new_edge = edge(end, t + depth + 5);
        const int deadline = latest_leave(origin), after = latest_leave(end);
        const bool retire = edge(origin, t + 11) == 0 || (sc0 > 24 && deadline < t + 12 && t > c.col_start - 85);
        double score = 0;
        if (rt0 < Far && rt1 < Far) {
            double plan_progress = std::clamp((rt0 - rt1) / 42., -3.8, 3.8);
            if (armed && focus >= 0 && route_mode == 0) {
                const Forecast& f = foe[focus];
                const V foe0 = f.tracks[0].f[0].x;
                const V foe1 = f.tracks[0].f[depth].x;
                const double before = (foe0 - origin).len();
                const double after = (foe1 - end).len();
                if (f.armed && before < 4750 && !blocked(t, origin, foe0)) {
                    const double sweet = crowd ? 3800 : 3500;
                    plan_progress = (std::abs(before - sweet) - std::abs(after - sweet)) / 1080.;
                }
            }
            score += plan_progress * (retire ? .21 : 1.0);
        }
        const V route_delta = waypoint - origin;
        if (route_delta.any()) {
            const auto direct = route_delta.unit();
            score += std::clamp((mid - origin).dot(direct) / 1.0e6, -.8, .8) * (retire ? .9 : .22);
        }
        if (sc0 < Far && sc1 < Far) {
            const double escape_preference = retire ? 2.85 : .16 + 1.05 * ramp(t, c.col_start - 82., c.col_start + 45.);
            score += escape_preference * std::clamp((sc0 - sc1) / 42., -4.0, 4.0);
        } else if (sc0 < Far && sc1 >= Far) score -= 3.0;
        else if (sc0 >= Far && sc1 < Far) score += 2.0;
        if (deadline >= t + 2 && t >= c.col_start - 95) {
            const double safety = ramp(after - (t + depth), 3., 52.);
            const double current = ramp(deadline - t, 3., 52.);
            score += (safety - current) * (crowd ? 3.2 : 1.9);
        }
        score -= .85 * (1 - ramp(new_edge, 0, 1000));
        score -= 2.5 * (1 - ramp(edge(end, t + depth + 12), 0, 550));
        for (int h = 1; h <= std::min(depth, 8); ++h) {
            if (dash_move(path.f[h - 1])) continue;
            score -= .12 * (1 - ramp(edge(path.f[h].x, t + h - 1), 0, 520));
        }
        if (armed && gi.M().at(cell_index(end.x), cell_index(end.y)) == C_GRASS && !pit(end, t + depth))
            score += crowd ? .27 : .18;
        if (!plan.move.any() && plan.act == A_NONE && ob->me().st != S_RECALL) score -= .22;
        if (plan.act == A_DASH) {
            bool crossing = false;
            for (int h = 1; h <= depth; ++h) crossing |= pit(path.f[h].x, t + h - 1);
            score -= crossing ? .65 : DashLock;
            if (crowd && focus >= 0 && (foe[focus].tracks[0].f[0].x - origin).sq() < 6800 * 6800) score -= .90;
        }
        if (ob->my_dart().st == D_GROUND) {
            const V d = pos(ob->my_dart());
            for (int h = 1; h <= depth; ++h) {
                if ((path.f[h].x - d).sq() <= c.pick_r * c.pick_r) {
                    score += 3.1 - .10 * h;
                    break;
                }
            }
        }
        if (ob->me().st == S_THROW && t <= throw_hold && throw_move.any()) {
            const V step = path.f[1].x - origin;
            score += 1.4 * ramp(step.dot(throw_move.unit()) / double(UNIT), -100., 110.);
        }
        if (JukeWeight > 0. && focus >= 0 && plan.act != A_DASH && armed) {
            const auto& f = foe[focus];
            const V opponent = f.tracks[0].f[0].x;
            const V toward = opponent - origin;
            const double distance = toward.len();
            if (distance > 2700. && distance < 6800. && f.rearm <= 3 && !blocked(t, origin, opponent)) {
                const V movement = path.f[std::min(depth, 4)].x - origin;
                const double lateral = movement.dot(toward.left()) / distance;
                const double scale = ramp(distance, 2700., 4000.) * (1. - ramp(distance, 5000., 6800.));
                score += JukeWeight * f.attention * scale * pivot_sign *
                    std::clamp(lateral / (c.run_v * 3.65), -1., 1.) * (crowd ? .55 : 1.);
            }
        }
        if (route_mode == 1 && ob->my_dart().st == D_GROUND && plan.act == A_RECALL) score += 3.1;
        return score;
    }

    bool cone(V center, V target, V direction, i64 range, i64 cosine, i64 omnidirectional) const {
        const V rel = target - center;
        const i64 ss = rel.sq();
        if (ss > range * range) return false;
        return ss <= omnidirectional * omnidirectional || rel.dot(direction) >= cosine * double(std::sqrt(double(ss)));
    }

    int earliest_free(const Track& track, int len, bool committed) const {
        for (int h = committed ? 1 : 0; h <= len; ++h)
            if (track.f[h].st == S_FREE) return h;
        return len;
    }

    double opponent_value(int k) const {
        if (!crowd) return 1.;
        const double pressure = foe[k].prey == me ? 0. : .10 * (1. - foe[k].attention);
        const double standing = .035 * std::clamp<int>(int(ob->p[k].kills - ob->me().kills), -1, 2);
        return 1. + pressure + standing;
    }

    double risk(const Track& self, const Plan& plan, int depth, const Ray* own = nullptr) const {
        const i64 radius = c.body_r + c.dart_r;
        const int lock = earliest_free(self, depth, plan.act != A_NONE);
        const V swing_aim = self.swing > 0 ? self.f[self.swing].a : V{};
        double hits[MAXP]{};
        double shadows[MAXP]{};
        for (const Ray& ray : rays) {
            double peak = 0;
            const int last = std::min(ray.last, depth);
            for (int h = ray.first; h <= last; ++h) {
                const V a = ray.x[h - 1], b = ray.x[h];
                const V p = self.f[h - 1].x, q = self.f[h].x;
                if (c.block && h == self.swing && self.slash &&
                    cone(q, a, swing_aim, c.slash_reach, c.block_cos, 0) && !blocked(t + h - 1, q, a)) break;
                if (own && c.clash && h >= own->first && h <= own->last) {
                    const V u = a - own->x[h - 1], v = b - own->x[h];
                    const i64 r = 2 * c.dart_r;
                    if (u.sq() > r * r && swept(u, v) <= r * r &&
                        (own->x[h] - q).sq() > (radius + 450) * (radius + 450)) break;
                }
                const i64 margin = ray.latent ? 390 : 480;
                if (iabs(b.x - q.x) > radius + margin + c.dart_v + c.dash_v ||
                    iabs(b.y - q.y) > radius + margin + c.dart_v + c.dash_v) continue;
                const double d2 = swept(a - p, b - q);
                if (d2 > (radius + margin) * (radius + margin)) continue;
                const double d = std::sqrt(d2);
                const double contact = d <= radius ? 1. : .64 * (1 - ramp(d, radius, radius + margin));
                const double urgent = h <= lock + 2 ? 1.0 : std::max(.22, 1.0 - .095 * (h - 2));
                peak = std::max(peak, contact * urgent);
            }
            if (ray.latent) shadows[ray.owner] += peak * ray.weight;
            else hits[ray.owner] += peak * ray.weight;
        }
        double total = 0;
        for (int k = 0; k < gi.n; ++k) {
            if (k == me) continue;
            total += std::min(1., hits[k]) * 69. + std::min(.43, shadows[k]) * (crowd ? ShadowFfa : (c.mode == M_DUEL ? ShadowDuel : 46.));
            const Forecast& f = foe[k];
            if (!f.alive) continue;
            const Track& e = f.tracks[0];
            if (e.swing > 0 && e.swing <= depth) {
                const V p = e.f[e.swing].x, q = self.f[e.swing].x;
                if (cone(p, q, e.f[e.swing].a, e.slash ? c.slash_reach : c.kick_reach,
                         e.slash ? c.slash_cos : c.kick_cos, c.body_r) &&
                    !blocked(t + e.swing - 1, p, q)) total += e.slash ? 60. : 14.;
            }
            if (f.mate) {
                total += 1.25 * (1 - ramp((e.f[std::min(6, depth)].x - self.f[std::min(6, depth)].x).len(),
                                         700., 1650.));
                continue;
            }
            double close = 0;
            double zone = 0;
            if ((e.f[0].x - origin).sq() > 8200 * 8200) continue;
            for (int h : {2, 5, 8, 11}) {
                if (h > depth) break;
                const V q = self.f[h].x;
                const V p = e.f[h].x;
                const double distance = (q - p).len();
                if (distance > 5300 || blocked(t + h - 1, p, q)) continue;
                const bool mobile = e.f[h].st == S_FREE || e.f[h].st == S_DASH;
                const bool enemy_weapon = f.rearm <= h + 1;
                const bool early_swing = self.swing > 0 && self.swing <= h &&
                    cone(self.f[self.swing].x, e.f[self.swing].x, swing_aim,
                         self.slash ? c.slash_reach : c.kick_reach,
                         self.slash ? c.slash_cos : c.kick_cos, c.body_r);
                const double reach = enemy_weapon ? c.slash_reach : c.kick_reach;
                const double locked = early_swing ? .21 : 1.;
                const bool entering = e.f[h].timer <= 2 &&
                    (e.f[h].st == S_THROWN || e.f[h].st == S_SLASHED || e.f[h].st == S_KICKED);
                if (mobile || entering) {
                    const double reach_extension = enemy_weapon ? (armed ? 1160. : 1510.) : 630.;
                    const bool kick_defeat = !enemy_weapon && distance < reach + 550 &&
                        pit(q + (q - p).unit(kick_slide), t + h + int(c.kick_stun));
                    const double severity = enemy_weapon ? (f.armed ? 15.5 : 11.7) : (kick_defeat ? 17.4 : 4.8);
                    close = std::max(close, severity * locked *
                        (1 - ramp(distance, reach - 80, reach + reach_extension)) * (entering ? .76 : 1.));
                    if (enemy_weapon) {
                        const V step = self.f[h].x - self.f[h - 1].x;
                        const double lateral = std::abs(double(step.dot((q - p).left()))) / std::max(1., distance);
                        double exposure = 1 - ramp(lateral, c.run_v * .22, c.run_v * .85);
                        if (plan.act == A_MELEE && self.swing > h) exposure *= .65;
                        const double threat_range = f.visible ? 5400. : 6500.;
                        zone += exposure * (1 - ramp(distance, 2650, threat_range)) * (h < 7 ? .70 : .34);
                    }
                }
            }
            total += f.confidence * (close + zone * (crowd ? 3.6 : 3.0) * (.55 + f.attention * .45));
            if (plan.act == A_DASH && self.swing < 0 && f.rearm <= c.dash_pre + c.dash_t + 2) {
                const int end = std::min(depth, int(c.dash_pre + c.dash_t + c.dash_post));
                const V landing = self.f[end].x;
                const V shooter = e.f[end].x;
                const double separation = (landing - shooter).len();
                const bool pin = edge(landing, t + end + 2) < 1000;
                if (separation < 5150 && !blocked(t + end, landing, shooter))
                    total += (pin ? 4.2 : 1.6) * f.attention * (1. - ramp(separation, 2100., 5150.));
            }
        }
        double attack[MAXP]{};
        for (const Rush& rush : rushes) {
            const int h = rush.step;
            if (h > depth) continue;
            const V p = rush.position, q = self.f[h].x;
            const double distance = (q - p).len();
            const double reach = rush.weapon ? c.slash_reach : c.kick_reach;
            if (distance > reach + 450 || blocked(t + h - 1, p, q)) continue;
            double val = (1 - ramp(distance, reach - 330, reach + 450)) * rush.weight;
            if (h > lock + 3) val *= .72;
            if (self.swing > 0 && self.slash && self.swing <= h &&
                (p - self.f[self.swing].x).sq() <= (c.slash_reach + 230) * (c.slash_reach + 230)) val *= .62;
            attack[rush.owner] = std::max(attack[rush.owner], val);
        }
        for (int k = 0; k < gi.n; ++k) total += attack[k] * (armed ? 24. : 38.);
        return total;
    }

    double movement_score(const Track& path, const Plan& plan, int depth, const Ray* own = nullptr) const {
        if (path.fall <= depth) return -140. + path.fall * 2.5;
        const double position = travel(path, plan, depth);
        const Dart& d = ob->my_dart();
        if (own || !dart_flying(d.st))
            return position - risk(path, plan, depth, own);
        const Ray raw = fly(me, d, 1, path, 1., depth);
        Ray resolved = raw;
        cut_shot(resolved);
        double combat = shot_gain(resolved, 0, true) * (crowd ? .56 : .53);
        if (resolved.finish == D_HAND) combat += 3.5 * (1.10 - .70 * resolved.last / depth);
        if (resolved.finish == D_GROUND && !c.recall && pit(resolved.x[resolved.last], t + resolved.last)) combat -= 3.0;
        return position - risk(path, plan, depth, &raw) + combat;
    }

    bool kick_falls(V from, V target, int at) const {
        Frame f{target, (target - from).unit(c.kick_v), {}, S_STUN, int(c.kick_stun)};
        const int steps = std::min(30, int(c.kick_stun));
        for (int h = 0; h < steps; ++h)
            if (!advance(f, {}, at + h)) return true;
        return false;
    }

    double melee_gain(const Track& tr) const {
        const int h = tr.swing;
        if (h <= 0) return 0;
        const V p = tr.f[h].x;
        const V a = tr.f[h].a;
        double reward = 0;
        for (int k = 0; k < gi.n; ++k) {
            if (k == me || !foe[k].alive) continue;
            const Forecast& f = foe[k];
            if ((f.tracks[0].f[h].x - p).sq() > 4300 * 4300) continue;
            double kill = 0, kick = 0;
            for (int m = 0; m < Models; ++m) {
                const Track& e = f.tracks[m];
                if (e.fall <= h) continue;
                const V q = e.f[h].x;
                if (!cone(p, q, a, tr.slash ? c.slash_reach : c.kick_reach,
                          tr.slash ? c.slash_cos : c.kick_cos, c.body_r) || blocked(t + h - 1, p, q)) continue;
                if (tr.slash) kill += f.prob[m];
                else if (e.f[h - 1].st != S_STUN && !(e.f[h - 1].st == S_FREE && e.f[h - 1].timer > 0)) {
                    if (kick_falls(p, q, t + h)) kill += f.prob[m];
                    else kick += f.prob[m];
                }
            }
            if (f.mate) reward -= 33. * kill + 11. * kick;
            else {
                const bool vulnerable = f.tracks[0].f[h - 1].st != S_FREE &&
                    f.tracks[0].f[h - 1].st != S_DASH && f.tracks[0].f[h - 1].st != S_SLASH;
                const double interrupt = f.rearm <= h + 1 ? (vulnerable ? 3.2 : .50) : (crowd ? 1.8 : 1.15);
                reward += f.confidence * opponent_value(k) * (kill * (crowd ? 18.5 : 24.) + kick * interrupt);
            }
        }
        if (tr.slash && c.block) {
            for (const Ray& ray : rays) {
                if (ray.latent || h < ray.first || h > ray.last) continue;
                const V pos = ray.x[h - 1];
                if (cone(p, pos, a, c.slash_reach, c.block_cos, 0) && !blocked(t + h - 1, p, pos))
                    reward += (is_mate(gi, ray.owner) ? -.7 : 3.0) * ray.weight;
            }
        }
        return reward;
    }

    struct Evaluated {
        Plan plan;
        double score = -1e10;
    };

    struct Options {
        static constexpr int Count = 15;
        std::array<Evaluated, Count> candidate{};
        std::array<Evaluated, 5> by_action{};
        Evaluated winner;

        void add(const Plan& p, double value) {
            if (value > winner.score) winner = {p, value};
            if (value > by_action[p.act].score) by_action[p.act] = {p, value};
            if (value <= candidate.back().score) return;
            for (int i = 0; i < Count; ++i) {
                const Plan& other = candidate[i].plan;
                if (candidate[i].score <= -1e9 || p.act != other.act ||
                    (p.move - other.move).sq() > 215 * 215 ||
                    (p.action - other.action).sq() > 145 * 145 ||
                    p.melee_at != other.melee_at) continue;
                if (value <= candidate[i].score) return;
                for (int j = i; j + 1 < Count; ++j) candidate[j] = candidate[j + 1];
                candidate.back() = {};
                break;
            }
            for (int i = Count - 1; i >= 0; --i) {
                if (i > 0 && value > candidate[i - 1].score) candidate[i] = candidate[i - 1];
                else {
                    candidate[i] = {p, value};
                    break;
                }
            }
        }
    };

    void evaluate_melee(Plan p, int delay, Options& best, int depth, bool emergency = false) const {
        const bool direct = delay == 0;
        if (direct) {
            p.act = A_MELEE;
            p.action = {UNIT, 0};
        } else {
            p.melee_at = delay;
            p.next = {UNIT, 0};
        }
        const Track probe = self_track(p, depth);
        if (probe.fall <= depth || probe.swing <= 0) return;
        const int h = probe.swing;
        const V center = probe.f[h].x;
        std::array<V, 42> aims{};
        int n = 0;
        const auto add = [&](V a) {
            if (!a.any() || n == int(aims.size())) return;
            const V u = a.unit();
            for (int i = 0; i < n; ++i) if ((u - aims[i]).sq() < 110 * 110) return;
            aims[n++] = u;
        };
        const i64 reach = armed ? c.slash_reach : c.kick_reach;
        for (int k = 0; k < gi.n; ++k) {
            if (k == me || !foe[k].alive || foe[k].mate) continue;
            for (int m : {0, 1, 2, 3, 4, 5}) {
                const V d = foe[k].tracks[m].f[h].x - center;
                if (d.sq() <= reach * reach) add(d);
            }
        }
        if (armed && c.block) {
            for (const Ray& ray : rays) {
                if (h < ray.first || h > ray.last) continue;
                const V a = ray.x[h - 1] - center;
                if (a.sq() <= reach * reach) add(a);
            }
        }
        if (emergency) add(aim(ob->me()));
        for (int i = 0; i < n; ++i) {
            if (direct) p.action = aims[i];
            else p.next = aims[i];
            Track path = probe;
            path.f[h].a = aims[i];
            const double gain = melee_gain(path);
            if (gain <= 0 && !emergency) continue;
            const double val = movement_score(path, p, depth) + gain - .92 - .15 * delay - (gain <= 0 ? 9.5 : 0.);
            best.add(p, val);
        }
    }

    double shot_gain(const Ray& shot, int release, bool in_air = false) const {
        const i64 radius = c.body_r + c.dart_r;
        double score = 0;
        for (int k = 0; k < gi.n; ++k) {
            if (k == me || !foe[k].alive) continue;
            const Forecast& f = foe[k];
            if ((f.tracks[0].f[0].x - origin).sq() > (c.range + 4300) * (c.range + 4300)) continue;
            double hit = 0;
            const bool from_cover = !in_air && !ob->me().expose &&
                cell_of_point(c, gi.M(), origin.x, origin.y, t) == C_GRASS &&
                (f.tracks[0].f[0].x - origin).sq() > (c.see_r + 130) * (c.see_r + 130);
            const int role = f.armed ? 1 : 0;
            const double local_error = seen[k].motion_count[role] >= 4 ? seen[k].motion_error[role][0] : 490.;
            const bool smooth = c.mode == M_DUEL;
            for (int m = 0; m < Models; ++m) {
                const Track& e = f.tracks[m];
                double quality = 0;
                double reaction = 0;
                for (int h = shot.first; h <= shot.last && h < e.fall; ++h) {
                    const int st = e.f[h - 1].st;
                    const bool mobile = st == S_FREE || st == S_THROW || st == S_THROWN;
                    if (!in_air && h > c.delay + 1 && mobile) reaction += st == S_FREE ? 1. : .5;
                    const V a = shot.x[h - 1] - e.f[h - 1].x;
                    const V b = shot.x[h] - e.f[h].x;
                    const i64 uncertainty = smooth
                        ? i64(mobile ? std::clamp(90. + .19 * local_error + h * 5., 135., 340.) : 110.) : 0;
                    const i64 limit = radius + c.dart_v + c.dash_v + uncertainty;
                    if (iabs(b.x) > limit || iabs(b.y) > limit) continue;
                    const double n = swept(a, b);
                    const i64 outskirts = radius + uncertainty;
                    if (n > outskirts * outskirts) continue;
                    const double margin = radius - std::sqrt(n);
                    double chance = smooth
                        ? ramp(margin, -uncertainty, uncertainty) *
                            (.82 + .18 * ramp(margin, uncertainty, radius * .82 + uncertainty))
                        : .69 + .31 * ramp(margin, 0, radius * .72);
                    const bool escaping = smooth ? mobile : st == S_FREE || st == S_THROWN;
                    if (in_air && escaping && h > 5) {
                        chance *= std::max(.28, 1.0 - .105 * (h - 5));
                    } else if (smooth && !in_air && !f.mate && reaction > 0) {
                        chance *= std::max(.21, 1.0 - .177 * reaction);
                        if (f.armed && c.block && reaction >= c.slash_pre) chance *= .83;
                    } else if (!smooth && !in_air && !f.mate && escaping && h > c.delay + 1) {
                        chance *= std::max(.20, 1.0 - .15 * (h - c.delay - 1));
                        if (f.armed && c.block && h > c.delay + c.slash_pre + 1) chance *= .82;
                    }
                    if (!in_air && h > release + 12) chance *= .42;
                    if (from_cover && h <= c.delay + 3) chance = std::min(1., chance * 1.13);
                    quality = std::max(quality, chance);
                }
                hit += f.prob[m] * quality;
            }
            if (f.mate) score -= 36. * hit;
            else score += (crowd ? 16.6 : 19.5) * f.confidence * opponent_value(k) * hit;
        }
        return score;
    }

    void cut_shot(Ray& shot) const {
        if (!c.clash) return;
        const i64 radius = 2 * c.dart_r;
        int collision_at = shot.last + 1;
        i64 fraction = 0, divisor = 1;
        int owner = -1;
        for (const Ray& other : rays) {
            if (other.latent || other.weight < .50) continue;
            for (int h = std::max(shot.first, other.first); h <= std::min(shot.last, other.last); ++h) {
                const V a = shot.x[h - 1] - other.x[h - 1];
                const V b = shot.x[h] - other.x[h];
                if (a.sq() <= radius * radius || swept(a, b) > radius * radius) continue;
                const V d = b - a;
                const i64 den = d.sq();
                if (!den) continue;
                const i64 num = iclamp(-a.dot(d), 0, den);
                if (h < collision_at || (h == collision_at && num * divisor < fraction * den)) {
                    collision_at = h;
                    fraction = num;
                    divisor = den;
                    owner = other.owner;
                }
                break;
            }
        }
        if (owner >= 0) {
            const V start = shot.x[collision_at - 1];
            const V delta = shot.x[collision_at] - start;
            shot.x[collision_at] = start + V{rdiv(delta.x * fraction, divisor), rdiv(delta.y * fraction, divisor)};
            shot.last = collision_at;
            shot.finish = D_GROUND;
            shot.clash = owner;
        }
    }

    void evaluate_shot(Plan p, Options& best, int depth) const {
        p.act = A_THROW;
        p.action = {UNIT, 0};
        const Track self = self_track(p, H);
        const int release = self.wind;
        if (release < 0 || release >= H || self.fall <= depth) return;
        const V from = self.f[release].x;
        std::array<V, 66> aims{};
        int n = 0;
        const auto add = [&](V dir) {
            if (!dir.any() || n == int(aims.size())) return;
            const V a = dir.unit();
            for (int i = 0; i < n; ++i) if ((a - aims[i]).sq() < 48 * 48) return;
            aims[n++] = a;
        };
        for (int k = 0; k < gi.n; ++k) {
            const Forecast& f = foe[k];
            if (k == me || !f.alive || f.mate) continue;
            const double distance = (f.tracks[0].f[0].x - from).len();
            if (distance > c.range + 1000 || distance > 7500) continue;
            for (int m = 0; m < Models; ++m) {
                const Track& e = f.tracks[m];
                i64 reach = 0;
                for (int h = release + 1; h <= std::min(H, release + 13); ++h) {
                    const i64 age = h - release - 1;
                    reach += imax(c.dart_vmin, c.dart_v - age * c.dart_dec);
                    const V delta = e.f[h].x - from;
                    if (delta.sq() <= (reach + 240) * (reach + 240)) {
                        add(delta);
                        if (h < H && m < 2) {
                            const V mid = (e.f[h].x + e.f[h + 1].x) / 2 - from;
                            add(mid);
                            const V u = delta.unit();
                            const i64 spread = i64(std::clamp((c.body_r + c.dart_r) * 520. / std::max(1., delta.len()),
                                                              55., 260.));
                            const i64 cosine = isqrt(UNIT * UNIT - spread * spread);
                            add(turn(u, cosine, spread));
                            add(turn(u, cosine, -spread));
                        }
                        break;
                    }
                    if (reach >= c.range) break;
                }
            }
        }
        if (c.clash) {
            for (const Ray& incoming : rays) {
                if (incoming.latent || incoming.weight < .5 || incoming.last <= release) continue;
                i64 travel = 0;
                for (int h = release + 1; h <= std::min(incoming.last, release + 10); ++h) {
                    travel = imin(c.range, travel + imax(c.dart_vmin, c.dart_v - (h - release - 1) * c.dart_dec));
                    if (h < incoming.first) continue;
                    const V point = incoming.x[h] - from;
                    if (point.sq() <= (travel + 320) * (travel + 320)) {
                        add(point);
                        add((incoming.x[h] + incoming.x[h - 1]) / 2 - from);
                        break;
                    }
                }
            }
        }
        const int initial = n;
        for (int i = 0; i < initial; ++i) {
            if (n >= (c.mode == M_TEAM ? 24 : 35)) break;
            int closest = -1;
            i64 distance = 410 * 410;
            for (int j = i + 1; j < initial; ++j) {
                const i64 d = (aims[i] - aims[j]).sq();
                if (d < distance && d > 150 * 150) { closest = j; distance = d; }
            }
            if (closest >= 0) add(aims[i] + aims[closest]);
        }
        const double baseline = movement_score(self, p, depth);
        if (baseline < best.winner.score - 29 && !c.clash) return;
        for (int i = 0; i < n; ++i) {
            if (i > 9 && i % 8 == 0 && !cpu_left(43000)) break;
            Dart d;
            d.st = D_OUT;
            d.x = from.x;
            d.y = from.y;
            d.vx = aims[i].x;
            d.vy = aims[i].y;
            d.left = c.range;
            const Ray raw = fly(me, d, release + 1, self, 1);
            Ray shot = raw;
            cut_shot(shot);
            const double gain = shot_gain(shot, release);
            if (gain < 2.0 && shot.clash < 0) continue;
            const double initiative = crowd ? FfaInitiative * std::clamp(1. - double(ob->me().kills), -1., 1.) : 0.;
            const double release_cost = (crowd ? ShotFfa : (c.mode == M_DUEL ? ShotDuel : 5.65)) - initiative -
                (shot.finish == D_HAND ? std::max(0., (H - shot.last) * .13) : 0.);
            const double wall_cost = shot.last < release + 10 && (shot.x[shot.last] - self.f[shot.last].x).sq() > c.pick_r * c.pick_r
                ? .95 + .85 * (pit(shot.x[shot.last], t + shot.last) ? 1.0 : 0.0) : 0.;
            double clutch = 0;
            if (shot.clash >= 0 && shot.last > 0 && foe[shot.clash].alive) {
                const double ours = (shot.x[shot.last] - self.f[shot.last].x).len();
                const double theirs = (shot.x[shot.last] - foe[shot.clash].tracks[0].f[shot.last].x).len();
                if (!pit(shot.x[shot.last], t + shot.last) && ours < 2400 && ours + 650 < theirs)
                    clutch = 1.15 * ramp(theirs - ours, 600., 2400.);
            }
            const double value = (rays.empty() ? baseline : movement_score(self, p, depth, &raw)) +
                gain - release_cost - wall_cost + clutch;
            p.action = aims[i];
            best.add(p, value);
        }
    }

    bool recall_ok() const {
        if (!c.recall || (ob->my_dart().st != D_GROUND && ob->my_dart().st != D_RISE)) return false;
        const int until = ob->me().st == S_RECALL ? int(ob->me().timer) : int(c.recall_pre);
        if (until > H - 3) return false;
        if (pit(origin, t + until + 2)) return false;
        for (int k = 0; k < gi.n; ++k) {
            if (k == me || !foe[k].alive || foe[k].mate) continue;
            const V p = foe[k].tracks[0].f[0].x;
            const double distance = (p - origin).len();
            const double unsafe = c.run_v * (until + 4) + (foe[k].armed ? 2800 : 1100);
            if (distance < unsafe && (distance < 2350 || !blocked(t, origin, p))) return false;
        }
        return true;
    }

    struct World {
        Game game;
        double weight = 0;
        int movement = 0;
        int policy = 0;
    };

    struct Guess {
        int owner = -1;
        int when = 0;
        int act = A_NONE;
        V direction{};
    };

    Action walk(V move) const {
        Action a;
        a.mx = move.x;
        a.my = move.y;
        return a;
    }

    Action attack(V direction, int act) const {
        Action a;
        a.act = act;
        a.ax = direction.x;
        a.ay = direction.y;
        return a;
    }

    Action plan_action(const Plan& p, int h) const {
        Action a = walk(p.movement(h));
        if (h == 0) {
            a.act = p.act;
            a.ax = p.action.x;
            a.ay = p.action.y;
        } else if (h == p.melee_at) {
            a.act = A_MELEE;
            a.ax = p.next.x;
            a.ay = p.next.y;
        }
        return a;
    }

    void cache_live(Game& g) const {
        i64 fallen, warn;
        collapse_state(c, gi.M(), g.t, fallen, warn);
        g.live = std::shared_ptr<const Map>(gi.map, &map(int(g.t)));
        g.live_fallen = fallen;
    }

    World alternate(int model, const Guess* hidden, double weight, int policy) const {
        const int old = std::max(0, t - int(c.delay));
        Obs initial = *ob;
        initial.t = old;
        if (hidden && !initial.p[hidden->owner].vis && foe[hidden->owner].confidence > .52) {
            const int k = hidden->owner;
            auto& p = initial.p[k];
            const V estimate = seen[k].fog_point[model % int(seen[k].fog_point.size())];
            p.vis = 1;
            p.x = estimate.x;
            p.y = estimate.y;
            p.face = iatan2(origin.y - p.y, origin.x - p.x);
            if (initial.d[k].st == D_HAND) {
                initial.d[k].x = p.x;
                initial.d[k].y = p.y;
            }
        }
        World world{Game(gi, initial), weight, model, policy};
        Game& g = world.game;
        cache_live(g);
        bool fired = false;
        for (int h = old; h < t; ++h) {
            Action a[MAXP]{};
            for (int k = 0; k < gi.n; ++k) {
                if (k == me || !foe[k].alive || g.p[k].st == S_RECALL) continue;
                const V p = pos(g.p[k]);
                const V direction = (my_past(std::max(0, h - int(c.delay))) - p).unit();
                V mv = keep(k);
                if (model == 1) {
                    if (g.d[k].st == D_GROUND) mv = (pos(g.d[k]) - p).unit();
                    else mv = (mv + direction) / 2;
                }
                if (model == 2) mv = mv / 3;
                a[k] = walk(mv);
                if (hidden && hidden->owner == k && h == hidden->when && g.p[k].st == S_FREE &&
                    (hidden->act != A_THROW || g.d[k].st == D_HAND)) {
                    a[k] = attack(hidden->direction, hidden->act);
                    a[k].mx = mv.x / 2;
                    a[k].my = mv.y / 2;
                    fired = true;
                }
            }
            const History* own = past(h);
            const History* after = past(h + 1);
            if (own) {
                g.p[me] = own->p;
                g.p[me].team = gi.team[me];
                g.d[me] = own->d;
                a[me] = own->a;
            }
            g.over = false;
            g.step(a);
            if (hidden) {
                const Player& predicted = g.p[me];
                const Player& actual = after ? after->p : ob->me();
                const Dart& actual_dart = after ? after->d : ob->my_dart();
                const bool bad_body = !predicted.alive || (predicted.st == S_STUN && actual.st != S_STUN);
                const bool bad_dart = g.d[me].st == D_GROUND && actual_dart.st != D_GROUND;
                if (bad_body || bad_dart) { world.weight = 0; return world; }
            }
        }
        if (hidden && !fired) world.weight = 0;
        g.p[me] = ob->me();
        g.p[me].team = gi.team[me];
        g.d[me] = ob->my_dart();
        g.over = false;
        cache_live(g);
        return world;
    }

    V world_move(const Game& g, int model, int h, int k) const {
        const Player& p = g.p[k];
        if (p.st == S_RECALL || !foe[k].alive) return {};
        const V toward = foe[k].tracks[model].f[h + 1].x - pos(p);
        const i64 speed = p.st == S_THROW || p.st == S_THROWN ? imax(1, c.run_v * c.throw_move / 100) : c.run_v;
        if ((toward - vel(p)).sq() <= 24 * 24 && p.vx == 0 && p.vy == 0) return {};
        return toward.unit(std::min<i64>(UNIT, i64(toward.len() * UNIT / speed)));
    }

    V sidestep(const Player& enemy, const Dart& delayed, int h, V intended) const {
        if (enemy.st != S_FREE || !dart_flying(delayed.st)) return intended;
        const V p = pos(enemy);
        if ((pos(delayed) - p).sq() > (7200 + c.delay * c.dart_v) * (7200 + c.delay * c.dart_v))
            return intended;
        V start = pos(delayed), finish = start;
        V direction = delayed.st == D_OUT ? V{delayed.vx, delayed.vy} : V{delayed.vx, delayed.vy}.unit();
        if (!direction.any()) return intended;
        if (delayed.st == D_OUT) {
            i64 left = delayed.left;
            for (int i = 0; i < c.delay + 5 && left > 0; ++i) {
                const i64 speed = imin(left, imax(c.dart_vmin, c.dart_v - (delayed.age + i) * c.dart_dec));
                left -= speed;
                finish = finish + direction * speed / UNIT;
            }
        } else {
            const i64 speed = dart_step_len(c, delayed);
            finish = finish + direction * speed * (c.delay + 3) / UNIT;
        }
        if (!(delayed.st == D_CALL && c.recall_wall)) clip(t + h, start, finish);
        if (swept(start - p, finish - p) > (c.body_r + c.dart_r + 480) * (c.body_r + c.dart_r + 480))
            return intended;
        V a = direction.left(), b = -a;
        const V avoid = (p - start);
        if (a.dot(avoid) + 300 * a.dot(intended) / UNIT < b.dot(avoid) + 300 * b.dot(intended) / UNIT)
            std::swap(a, b);
        for (V choice : {a, b}) {
            const V step = p + choice.unit(c.run_v * 3);
            if (!pit(step, t + h + 2) && !blocked(t + h, p, step)) return choice;
        }
        return intended;
    }

    void opponent_action(const World& world, const Game& g, int h, int k,
                         const std::array<Player, H + 1>& ours, bool& committed, Action& a) const {
        const Player& p = g.p[k];
        if (is_mate(gi, k) || committed || p.st != S_FREE) return;
        const bool weapon = g.d[k].st == D_HAND;
        const int seen_at = h - int(c.delay);
        Player victim;
        if (seen_at >= 0) victim = ours[seen_at];
        else if (const auto* old = past(std::max(0, t + seen_at))) victim = old->p;
        else victim = ob->me();
        const V from = pos(p);
        if (h < 1 && world.policy == 2) return;
        if (seen_at < 0 && hidden_from(from, std::max(0, t + seen_at))) return;
        const V raw = pos(victim) - from;
        int target_seat = me;
        double distance = raw.len();
        if (crowd) {
            for (int j = 0; j < gi.n; ++j) {
                if (j == me || j == k || gi.team[j] == gi.team[k] || !g.p[j].alive || g.p[j].unknown) continue;
                const auto &seen_at_launch = foe[j].tracks[0].f[std::clamp(seen_at, 0, H)];
                const V target = seen_at < 0 && ob->p[j].vis ? pos(ob->p[j]) : seen_at_launch.x;
                const double dd = (target - from).len();
                if (dd >= distance * .85 || dd > 5500 || blocked(t + h, from, target)) continue;
                victim.x = target.x;
                victim.y = target.y;
                victim.vx = seen_at_launch.v.x;
                victim.vy = seen_at_launch.v.y;
                victim.st = seen_at_launch.st;
                victim.timer = seen_at_launch.timer;
                distance = dd;
                target_seat = j;
            }
        }
        if (distance > 5900.) return;
        const V lead = vel(victim);
        V target = pos(victim) + lead * c.delay;
        if (victim.st == S_DASH && seen_at >= 0)
            target = pos(victim) + vel(victim) * imin(c.delay, imax(0, victim.timer - c.dash_post));
        if (pit(target, t + h)) target = pos(victim);
        V to = target - from;
        const i64 reach = weapon ? c.slash_reach : c.kick_reach;
        const bool dangerous_kick = pit(target + to.unit(kick_slide), t + h + int(c.kick_pre + c.kick_stun));
        if ((weapon || g.d[me].st == D_HAND || dangerous_kick) &&
            to.sq() < (reach + 240) * (reach + 240) &&
            !blocked(t + h, from, target)) {
            a = attack(to, A_MELEE);
            committed = true;
        } else if (weapon && world.policy != 2 && to.sq() < 4650 * 4650 && !blocked(t + h, from, target)) {
            V lead_target = target + lead * 4;
            if (pit(lead_target, t + h)) lead_target = target;
            a.act = A_THROW;
            a.ax = lead_target.x - from.x;
            a.ay = lead_target.y - from.y;
            committed = true;
        } else if (weapon && target_seat == me && world.policy == 2 && h >= c.delay && c.block && dart_flying(g.d[me].st)) {
            const V dart = pos(g.d[me]);
            const double dd = (dart - from).len();
            if (dd <= c.slash_reach + c.dart_v * (c.slash_pre - 1) && dd > c.body_r + c.dart_r &&
                !blocked(t + h, from, dart)) {
                a = attack(dart - from, A_MELEE);
                committed = true;
            }
        }
    }

    double rollout(const World& world, const Plan& plan, int horizon) const {
        Game g = world.game;
        const bool duel = c.mode == M_DUEL;
        bool attacked[MAXP]{};
        std::array<Player, H + 1> ours{};
        std::array<Dart, H + 1> own_dart{};
        double result = 0;
        int kills = int(g.p[me].kills);
        const i64 initial_dart = g.d[me].st;
        for (int h = 0; h < horizon; ++h) {
            ours[h] = g.p[me];
            own_dart[h] = g.d[me];
            const int when = h - int(c.delay);
            Dart viewed;
            if (when >= 0) viewed = own_dart[when];
            else if (const auto* old = past(std::max(0, t + when))) viewed = old->d;
            Action a[MAXP]{};
            for (int k = 0; k < gi.n; ++k) {
                if (k == me || !g.p[k].alive || g.p[k].unknown) continue;
                V mv = world_move(g, world.movement, h, k);
                if (world.policy == 2 && !is_mate(gi, k)) mv = sidestep(g.p[k], viewed, h, mv);
                a[k] = walk(mv);
                if (world.policy) opponent_action(world, g, h, k, ours, attacked[k], a[k]);
            }
            a[me] = plan_action(plan, h);
            const bool locked = g.p[me].st != S_FREE && g.p[me].st != S_THROWN && g.p[me].st != S_THROW;
            const double soon = h <= 2 || locked ? 1. : std::max(.30, 1. - .13 * (h - 2));
            g.over = false;
            g.step(a);
            if (g.p[me].kills > kills) {
                for (int k = 0; k < gi.n; ++k) {
                    if (g.p[k].dead_t == g.t - 1 && g.p[k].killer == me)
                        result += (is_mate(gi, k) ? -31. : 12.) * soon;
                }
                kills = int(g.p[me].kills);
            }
            if (!g.p[me].alive) {
                const bool tied = duel && g.winner == -1 && g.over;
                return result - (tied ? 19. : 72.) * soon;
            }
            if (c.mode == M_FFA) {
                for (int k = 0; k < gi.n; ++k) {
                    const Player& p = g.p[k];
                    if (k == me || p.dead_t != g.t - 1 || p.killer == me) continue;
                    result += (p.killer < 0 ? 1.4 : .85) * soon;
                }
            }
            if (g.over && ((duel && g.winner == me) ||
                (c.mode == M_TEAM && g.winner == gi.team[me]))) return result + 10. * soon;
            if (g.p[me].st == S_STUN && g.p[me].timer == c.kick_stun && (h > 0 || ob->me().st != S_STUN)) {
                Frame victim = frame(g.p[me]);
                bool fell = false;
                for (int j = 0; j < imin(40, c.kick_stun); ++j)
                    if (!advance(victim, {}, t + h + 1 + j)) { fell = true; break; }
                result -= (fell ? 34. : 4.) * soon;
            }
        }
        if (initial_dart != D_HAND && g.d[me].st == D_HAND) result += 1.85;
        return result;
    }

    Plan refine(const Game& aligned, const Options& options, int horizon) const {
        const auto& best = options.winner;
        if (focus < 0 || !cpu_left(50000)) return best.plan;
        const V rival = foe[focus].tracks[0].f[0].x;
        const bool near = (rival - origin).sq() < 7200 * 7200;
        if (!near && rays.empty()) return best.plan;
        std::array<Evaluated, 10> final{};
        int n = 0;
        const auto add = [&](const Evaluated& e) {
            if (e.score < best.score - 20. || n == int(final.size())) return;
            for (int i = 0; i < n; ++i) {
                const Plan& a = e.plan;
                const Plan& b = final[i].plan;
                if (a.act == b.act && (a.move - b.move).sq() < 155 * 155 &&
                    (a.action - b.action).sq() < 118 * 118 && a.melee_at == b.melee_at) return;
            }
            final[n++] = e;
        };
        add(best);
        for (const Evaluated& e : options.by_action) add(e);
        for (const Evaluated& e : options.candidate) {
            if (n >= (c.mode == M_TEAM ? 5 : 8)) break;
            add(e);
        }
        if (n < 2) return best.plan;
        // Check each finalist in the same worlds using official stepping.
        World canon{aligned, .57, 0, 0};
        cache_live(canon.game);
        std::array<double, 10> score{};
        const int depth = std::min(horizon, c.mode == M_TEAM ? 8 : 11);
        double total = 0;
        const auto score_world = [&](const World& w) {
            if (!w.weight) return;
            for (int i = 0; i < n; ++i) score[i] += rollout(w, final[i].plan, depth) * w.weight;
            total += w.weight;
        };
        score_world(canon);
        if (near && cpu_left(47000)) score_world(alternate(1, nullptr, .23, 1));
        if (near && c.mode != M_TEAM && cpu_left(47000)) score_world(alternate(2, nullptr, .14, 2));
        std::array<const Ray*, 2> suspect{};
        std::array<double, 2> cost{1e20, 1e20};
        for (const Ray& ray : rays) {
            if (!ray.latent || ray.launched >= t || ray.launched < std::max(0, t - int(c.delay))) continue;
            double nearest = 1e15;
            for (int h = ray.first; h <= std::min(8, ray.last); ++h) {
                const V projected = origin + vel(ob->me()) * imin(h, 3);
                const auto distance = swept(ray.x[h - 1] - projected, ray.x[h] - projected);
                nearest = std::min(nearest, distance + h * 100000.);
            }
            if (nearest > 2100 * 2100) continue;
            nearest /= std::max(.018, ray.weight);
            if (nearest < cost[0]) {
                suspect[1] = suspect[0];
                cost[1] = cost[0];
                suspect[0] = &ray;
                cost[0] = nearest;
            } else if (nearest < cost[1]) {
                suspect[1] = &ray;
                cost[1] = nearest;
            }
        }
        for (const Ray* shot : suspect) {
            if (shot && cpu_left(47000)) {
                const Guess guessed{shot->owner, shot->launched, A_THROW, shot->direction};
                score_world(alternate(0, &guessed, .105, 0));
            }
        }
        if (near && c.mode != M_TEAM && c.delay > 0 && cpu_left(47000)) {
            int best_k = -1;
            int begin = -1;
            V target{};
            double closer = 1e10;
            const int old = std::max(0, t - int(c.delay));
            for (int k = 0; k < gi.n; ++k) {
                const Forecast& f = foe[k];
                if (k == me || !f.alive || f.mate || !f.visible) continue;
                const Player& p = ob->p[k];
                const int at = std::max(t - 1, old + (p.st == S_FREE ? 0 : int(p.timer)));
                if (at >= t) continue;
                const V from = f.tracks[0].f[0].x - keep(k).unit(c.run_v);
                const int seen_t = std::max(0, at - int(c.delay));
                const auto* own = past(seen_t);
                const V relative = my_past(seen_t) + (own ? vel(own->p) : V{}) * c.delay - from;
                const double distance = relative.len();
                const i64 reach = f.armed ? c.slash_reach : c.kick_reach;
                if (distance > reach + 600 || distance >= closer || blocked(t, from, from + relative)) continue;
                best_k = k;
                begin = at;
                target = relative;
                closer = distance;
            }
            if (best_k >= 0 && target.any()) {
                const Guess guessed{best_k, begin, A_MELEE, target};
                score_world(alternate(0, &guessed, .135, 0));
            }
        }
        double high = -1e10;
        int choice = 0;
        for (int i = 0; i < n; ++i) {
            const double value = final[i].score + score[i] * (crowd ? ReviewFfa : (c.mode == M_DUEL ? ReviewDuel : .56)) / total;
            if (value > high) { high = value; choice = i; }
        }
        return final[choice].plan;
    }

    struct Footwork {
        Frame body;
        Plan plan;
        double cost = 0;
        double rank = 0;
        int first = -1;
    };

    double short_risk(V before, V after, int h) const {
        const i64 radius = c.body_r + c.dart_r;
        double risk = 0;
        for (const Ray& ray : rays) {
            if (h < ray.first || h > ray.last) continue;
            const V a = ray.x[h - 1] - before, b = ray.x[h] - after;
            const i64 limit = radius + 500 + c.dart_v + c.run_v;
            if (iabs(b.x) > limit || iabs(b.y) > limit) continue;
            const double nearest = swept(a, b);
            const i64 buffer = radius + 455;
            if (nearest > buffer * buffer) continue;
            const double d = std::sqrt(nearest);
            const double contact = nearest <= radius * radius ? 1.0 : .57 * (1. - ramp(d, radius, buffer));
            risk += contact * ray.weight * (ray.latent ? 26. : 52.);
        }
        for (const Rush& r : rushes) {
            if (r.step != h) continue;
            const i64 reach = r.weapon ? c.slash_reach : c.kick_reach;
            const double d = (r.position - after).len();
            if (d < reach + 320 && !blocked(t + h - 1, after, r.position))
                risk += 15. * r.weight * (1. - ramp(d, reach - 300, reach + 320));
        }
        return risk;
    }

    bool must_brake(const Plan& p) const {
        const Track path = self_track(p, 5);
        if (path.fall <= 3) return true;
        const i64 r = c.body_r + c.dart_r;
        for (const Ray& ray : rays) {
            if (ray.latent || ray.weight <= .5) continue;
            for (int h = ray.first; h <= std::min(3, ray.last); ++h) {
                if (swept(ray.x[h - 1] - path.f[h - 1].x, ray.x[h] - path.f[h].x) < r * r) return true;
            }
        }
        return false;
    }

    std::vector<Plan> footwork(const std::array<V, Moves + 12>& dirs, int count, V desired,
                               bool urgent) const {
        std::vector<Plan> plans;
        if (!cpu_left(30000) || c.mode == M_TEAM || (c.mode == M_DUEL && !urgent)) return plans;
        double nearby = 0.;
        for (const Ray& r : rays) {
            for (int h = r.first; h <= std::min(r.last, Prefix + 1); ++h) {
                if ((r.x[h] - origin).sq() < 2650 * 2650) {
                    nearby += r.weight;
                    break;
                }
            }
        }
        if (nearby < (c.mode == M_DUEL ? .52 : .17) && edge(origin, t + 5) > 380) return plans;
        const int beam_width = nearby > .5 ? 15 : 10;
        const int max_depth = nearby > .5 ? Prefix : 4;
        std::vector<Footwork> layer(1);
        std::vector<Footwork> proposals;
        layer.reserve(beam_width);
        proposals.reserve(beam_width * count);
        layer[0].body = frame(ob->me());
        const int route0 = navval(route, origin);
        const int escape0 = navval(escape, origin);
        for (int h = 0; h < max_depth; ++h) {
            if (!cpu_left(39500)) break;
            proposals.clear();
            for (const Footwork& prev : layer) {
                for (int k = 0; k < count; ++k) {
                    if (h && k >= 9 && ((k - 9) % 2)) continue;
                    Footwork f = prev;
                    const V move = dirs[k];
                    if (f.body.st == S_RECALL && move.any()) {
                        f.body.st = S_FREE;
                        f.body.timer = 0;
                    }
                    if (!advance(f.body, move, t + h)) continue;
                    const V x = f.body.x;
                    f.cost += short_risk(prev.body.x, x, h + 1) * (1.0 - .08 * h);
                    f.cost += .45 * (1. - ramp(edge(x, t + h + 5), 0., 545.));
                    const int distance = navval(route, x);
                    const int retreat = navval(escape, x);
                    double momentum = 0;
                    if (route0 < Far && distance < Far) momentum += (route0 - distance) / 41.;
                    if (escape0 < Far && retreat < Far && t + 70 > c.col_start)
                        momentum += .75 * (escape0 - retreat) / 41.;
                    if (h > 0 && k != f.first && (move - f.plan.prefix[h - 1]).sq() > 1300 * 1300)
                        f.cost += .04;
                    f.rank = momentum - f.cost;
                    f.plan.prefix[h] = move;
                    f.plan.prefix_count = h + 1;
                    if (h == 0) {
                        f.first = k;
                        f.plan.move = move;
                    }
                    proposals.push_back(f);
                }
            }
            if (proposals.empty()) break;
            std::sort(proposals.begin(), proposals.end(),
                      [](const Footwork& a, const Footwork& b) { return a.rank > b.rank; });
            layer.clear();
            std::array<int, Moves + 12> usage{};
            for (const Footwork& f : proposals) {
                if (usage[f.first] >= (h == 0 ? 1 : 3)) continue;
                bool redundant = false;
                for (const Footwork& other : layer) {
                    if ((f.body.x - other.body.x).sq() < 82 * 82 &&
                        (f.body.v - other.body.v).sq() < 110 * 110) { redundant = true; break; }
                }
                if (redundant) continue;
                layer.push_back(f);
                ++usage[f.first];
                if (int(layer.size()) == beam_width) break;
            }
        }
        for (const Footwork& f : layer) {
            if (f.plan.prefix_count == 0) continue;
            Plan path = f.plan;
            path.bend_at = path.prefix_count;
            path.next = desired;
            plans.push_back(path);
            path.next = path.prefix[path.prefix_count - 1];
            if ((path.next - desired).sq() > 225 * 225) plans.push_back(path);
        }
        return plans;
    }

    Action choose(const Game& g) {
        known_rays(g);
        goals(g);
        const int old = std::max(0, t - int(c.delay));
        latent_rays(old);
        latent_rushes(old);
        const Player& own = ob->me();
        const bool free = own.st == S_FREE || own.st == S_RECALL;
        const bool dashing = c.dash_slash && dash_move(frame(own));
        const int depth = c.mode == M_TEAM ? 10 : 12;
        const V desired = (waypoint - origin).unit();
        std::array<V, Moves + 12> dirs{};
        int count = 0;
        const auto add = [&](V a) {
            if (count == int(dirs.size())) return;
            for (int j = 0; j < count; ++j) if ((a - dirs[j]).sq() < 69 * 69) return;
            dirs[count++] = a;
        };
        add(desired);
        add(vel(own).unit());
        if (own.st == S_THROW && t <= throw_hold) add(throw_move);
        add({});
        if (focus >= 0) {
            const V to_foe = (foe[focus].tracks[0].f[0].x - origin).unit();
            add(to_foe.left());
            add(-to_foe.left());
            add(-to_foe);
            add(to_foe);
        }
        if ((waypoint - origin).sq() < (c.run_v * 6) * (c.run_v * 6)) {
            add(desired / 2);
            add(desired / 4);
        }
        const int offset = (me * 5 + int((gi.spx[me] + gi.spy[me]) % 23)) % Moves;
        for (int k = 0; k < Moves; k += c.mode == M_TEAM ? 2 : 1) add(compass[(k + offset) % Moves]);
        const bool immobile = own.st == S_STUN || own.st == S_SLASH ||
            own.st == S_KICK || own.st == S_SLASHED || own.st == S_KICKED || own.st == S_DASH;
        if (immobile && !dashing) return {};
        Options best;
        std::array<Evaluated, 5> top{};
        const auto add_walk = [&](const Plan& p, double val) {
            best.add(p, val);
            for (int k = 0; k < int(top.size()); ++k) {
                if (val <= top[k].score) continue;
                for (int j = int(top.size()) - 1; j > k; --j) top[j] = top[j - 1];
                top[k] = {p, val};
                break;
            }
        };
        for (int k = 0; k < count; ++k) {
            if (dashing && k > 0) break;
            Plan p;
            p.move = dirs[k];
            const Track path = self_track(p, depth);
            add_walk(p, movement_score(path, p, depth));
        }
        if (!dashing && (!rays.empty() || edge(origin, t + 9) < 1050)) {
            const auto leaders = top;
            for (int k = 0; k < 3; ++k) {
                if (leaders[k].score < -100) continue;
                Plan p = leaders[k].plan;
                p.bend_at = 4;
                const std::array<V, 4> choices{desired, p.move.left(), -p.move.left(), V{}};
                for (V a : choices) {
                    p.next = a;
                    const Track path = self_track(p, depth);
                    add_walk(p, movement_score(path, p, depth) - .12);
                }
            }
        }
        if (!dashing) {
            for (const Plan& p : footwork(dirs, count, desired, best.winner.score < -19.)) {
                const Track path = self_track(p, depth);
                add_walk(p, movement_score(path, p, depth) - .19);
            }
        }
        const bool emergency = dashing && must_brake(best.winner.plan);
        if (free || (dashing && (vel(own).sq() > c.run_v * c.run_v / 4 || armed || emergency))) {
            Plan p = best.winner.plan;
            p.bend_at = H;
            evaluate_melee(p, 0, best, depth, emergency);
        }
        if (free && own.cd == 0) {
            const double dist = focus < 0 ? 1e9 : (foe[focus].tracks[0].f[0].x - origin).len();
            const bool attack = focus >= 0 && dist < c.dash_v * c.dash_t + c.slash_reach + c.run_v * 4 &&
                (armed || !foe[focus].armed || dist < 2400);
            const i64 action_time = c.dash_pre + c.dash_t + c.dash_post;
            if (action_time <= depth) {
                for (int k = 0; k < count; ++k) {
                    if (dirs[k].sq() < 750 * 750) continue;
                    Plan p;
                    p.move = desired;
                    p.act = A_DASH;
                    p.action = dirs[k];
                    const Track path = self_track(p, depth);
                    const double val = movement_score(path, p, depth);
                    best.add(p, val);
                    if (!attack || !c.dash_slash || c.dash_pre >= depth - 2) continue;
                    const V gap = foe[focus].tracks[0].f[3].x - origin;
                    if (p.action.dot(gap) < gap.len() * 630.) continue;
                    for (int h = int(c.dash_pre + 1); h < std::min(depth - 1, int(c.dash_pre + c.dash_t)); ++h)
                        evaluate_melee(p, h, best, depth);
                }
            }
        }
        if (free && armed && focus >= 0 && (foe[focus].tracks[0].f[0].x - origin).sq() < 8200 * 8200) {
            const int limit = c.mode == M_TEAM ? 2 : (rays.size() > 60 ? 3 : 4);
            std::array<V, 5> used{};
            for (int k = 0, n = 0; k < int(top.size()) && n < limit; ++k) {
                if (!cpu_left(48500)) break;
                if (top[k].score < -90) continue;
                const Plan& p = top[k].plan;
                bool fresh = true;
                for (int j = 0; j < n; ++j) if ((p.move - used[j]).sq() < 190 * 190) fresh = false;
                if (!fresh) continue;
                used[n++] = p.move;
                evaluate_shot(p, best, depth);
            }
        }
        if (free && recall_ok()) {
            if (own.st == S_RECALL || navval(route, origin) > 90 || pit(pos(ob->my_dart()), t + 12)) {
                Plan p;
                if (own.st != S_RECALL) p.act = A_RECALL;
                const Track path = self_track(p, H);
                const double val = movement_score(path, p, std::min(H, std::max(depth, int(c.recall_pre)))) +
                    (own.st == S_RECALL ? 5.2 : 2.75) - .45 * ramp(navval(route, origin), 80., 200.);
                best.add(p, val);
            }
        }
        Action a;
        const Plan selected = refine(g, best, depth);
        a.mx = selected.move.x;
        a.my = selected.move.y;
        a.act = selected.act;
        a.ax = selected.action.x;
        a.ay = selected.action.y;
        if (a.act == A_THROW) {
            throw_move = selected.movement(std::min(1, int(c.throw_pre) - 1));
            throw_hold = t + int(c.throw_pre) - 1;
        }
        return a;
    }

public:
    Action decide(const Obs& o) {
        if (!o.me().alive) return {};
        started_cpu = thread_cpu_100ns();
        ob = &o;
        t = int(o.t);
        if (t >= pivot_at) {
            pivot_at = t + 4 + int(movement_rng.range(0, 4));
            pivot_sign = -pivot_sign;
        }
        origin = pos(o.me());
        armed = o.my_dart().st == D_HAND;
        if (o.me().st == S_RECALL) armed = false;
        const int old = std::max(0, t - int(c.delay));
        remember(o, old);
        const Game g = align(o, old);
        for (int k = 0; k < gi.n; ++k) if (k != me) predict(k, old, g);
        live_phase = phase_at[std::min(t, int(phase_at.size()) - 1)];
        if (built_phase != live_phase || (t + 9 >= c.col_start && t % 7 == 0)) {
            built_phase = live_phase;
            make_escape();
        }
        rays.clear();
        rushes.clear();
        Action a = choose(g);
        history[t % history.size()] = {t, o.me(), o.my_dart(), a};
        return a;
    }
};
}  // namespace

DART_MAIN(Bot)
