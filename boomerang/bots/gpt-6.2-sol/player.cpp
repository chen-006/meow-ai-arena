#include "dart.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>

using namespace dart;

namespace {
struct V {
    i64 x = 0;
    i64 y = 0;
    V operator+(V b) const { return {x + b.x, y + b.y}; }
    V operator-(V b) const { return {x - b.x, y - b.y}; }
    V operator-() const { return {-x, -y}; }
    bool operator==(V b) const { return x == b.x && y == b.y; }
    V scaled(double s) const { return {i64(std::lround(x * s)), i64(std::lround(y * s))}; }
    i64 sq() const { return x * x + y * y; }
    double len() const { return std::sqrt(double(sq())); }
    V unit() const { double l = len(); return l > 0 ? scaled(1000.0 / l) : V{}; }
    V left() const { return {-y, x}; }
    double dot(V b) const { return double(x) * b.x + double(y) * b.y; }
    V rotated(double a) const {
        double c = std::cos(a), s = std::sin(a);
        return {i64(std::lround(x * c - y * s)), i64(std::lround(x * s + y * c))};
    }
};

V pos(const Player& p) { return {p.x, p.y}; }
V vel(const Player& p) { return {p.vx, p.vy}; }
V pos(const Dart& d) { return {d.x, d.y}; }
Action action(V move, int kind = A_NONE, V aim = {}) {
    Action a;
    a.mx = move.x; a.my = move.y; a.act = kind; a.ax = aim.x; a.ay = aim.y;
    return a;
}

constexpr double Pi = 3.141592653589793;
constexpr int Horizon = 16;
constexpr int NearH = 8;
constexpr int Models = 4;
constexpr int Inf = 100000000;
constexpr int History = MAX_DELAY + 3;

double segmentDistance2(V a, V b) {
    V d = b - a;
    double aa = a.dot(d), dd = d.sq();
    if (aa >= 0 || dd == 0) return a.sq();
    if (-aa >= dd) return b.sq();
    return std::max(0.0, a.sq() - aa * aa / dd);
}

struct Stages {
    const GameInfo* gi = nullptr;
    std::vector<std::shared_ptr<const Map>> maps;
    std::vector<std::vector<int>> wallSums;
    mutable int simulations = 0;

    void init(const GameInfo& g) {
        gi = &g;
        maps.push_back(g.map);
        for (int k = 0; k < g.M().nwave; ++k) {
            maps.push_back(std::make_shared<const Map>(live_map(g.cfg, g.M(), wave_fall_step(g.cfg, k))));
        }
        for (const auto& stage : maps) {
            const int stride = stage->nx + 1;
            wallSums.emplace_back(stride * (stage->ny + 1), 0);
            auto& sums = wallSums.back();
            for (int y = 1; y <= stage->ny; ++y) {
                int row = 0;
                for (int x = 1; x <= stage->nx; ++x) {
                    row += stage->at(x - 1, y - 1) == C_WALL;
                    sums[y * stride + x] = sums[(y - 1) * stride + x] + row;
                }
            }
        }
    }
    int index(i64 t) const {
        if (t < gi->cfg.col_start) return 0;
        return int(std::min<i64>(maps.size() - 1, 1 + (t - gi->cfg.col_start) / std::max<i64>(1, gi->cfg.col_every)));
    }
    const Map& map(i64 t) const { return *maps[index(t)]; }
    bool occluded(V a, V b, i64 t) const {
        const int z = index(t);
        const Map& m = *maps[z];
        const int loX = std::max(0, cell_index(std::min(a.x, b.x)) - 1);
        const int loY = std::max(0, cell_index(std::min(a.y, b.y)) - 1);
        const int hiX = std::min(m.nx, cell_index(std::max(a.x, b.x)) + 2);
        const int hiY = std::min(m.ny, cell_index(std::max(a.y, b.y)) + 2);
        if (hiX <= loX || hiY <= loY) return false;
        const int stride = m.nx + 1;
        const auto& s = wallSums[z];
        int count = s[hiY * stride + hiX] - s[hiY * stride + loX] -
            s[loY * stride + hiX] + s[loY * stride + loX];
        return count > 0 && wall_between(m, a.x, a.y, b.x, b.y);
    }
    void step(Game& g, const Action* a) const {
        ++simulations;
        g.over = false;
        const int z = index(g.t);
        if (g.live_fallen != z || !g.live) {
            g.live_fallen = z;
            g.live = maps[z];
        }
        g.step(a);
    }
};

struct Navigation {
    struct Edge { int id; int cost; };
    struct Node {
        V p;
        int fall = -1;
        int clear = 0;
        int latest = -1;
        bool good = false;
        bool grass = false;
        std::vector<Edge> edges;
    };
    const GameInfo* gi = nullptr;
    const Stages* stages = nullptr;
    int w = 0, h = 0, stage = -1;
    i64 now = 0;
    std::vector<Node> nodes;
    std::vector<std::vector<Node>> prepared;
    std::vector<int> safe, target;
    int lastGoal = -1, lastGoalType = -1, lastSafeEpoch = -1;
    V markedGoal;

    void init(const GameInfo& g, const Stages& s) {
        gi = &g; stages = &s; w = g.M().nx; h = g.M().ny;
        nodes.resize(w * h);
        for (int k = 0; k < w * h; ++k) {
            auto& n = nodes[k];
            n.p = {cell_center(k % w), cell_center(k / w)};
            n.fall = int(cell_fall_step(g, k % w, k / w));
        }
        const int count = std::min<int>(8, int(s.maps.size()));
        prepared.resize(count);
        for (int z = 0; z < count; ++z) {
            now = z == 0 ? 0 : wave_fall_step(g.cfg, z - 1);
            if (now >= g.cfg.steps) break;
            rebuild(z);
            prepared[z] = nodes;
        }
        now = 0;
        stage = -1;
    }

    int id(V p) const {
        int x = cell_index(p.x), y = cell_index(p.y);
        return x >= 0 && y >= 0 && x < w && y < h ? y * w + x : -1;
    }

    bool on(V p, i64 t) const {
        return standable(*gi, p.x, p.y, t);
    }

    bool body(V p, i64 t) const {
        return on(p, t) && !circle_hits_wall(stages->map(t), gi->cfg.body_r, p.x, p.y);
    }

    bool line(V a, V b, i64 t) const {
        V d = b - a;
        int k = int(std::max(iabs(d.x), iabs(d.y)) / (CELL / 3)) + 1;
        for (int i = 1; i <= k; ++i) {
            V p{a.x + d.x * i / k, a.y + d.y * i / k};
            if (!body(p, t)) return false;
        }
        return true;
    }

    void rebuild(int z) {
        const Map& m = *stages->maps[z];
        const Config& c = gi->cfg;
        for (auto& n : nodes) {
            int x = cell_index(n.p.x), y = cell_index(n.p.y);
            int cc = m.at(x, y);
            n.good = (cc == C_FLOOR || cc == C_GRASS) &&
                !circle_hits_wall(m, c.body_r, n.p.x, n.p.y);
            n.grass = cc == C_GRASS;
            n.edges.clear();
            if (!n.good) continue;
            i64 best = 2500 * 2500;
            const int span = 5;
            for (int yy = y - span; yy <= y + span; ++yy)
                for (int xx = x - span; xx <= x + span; ++xx) {
                    if (m.at(xx, yy) != C_PIT) continue;
                    i64 dx = n.p.x - iclamp(n.p.x, xx * CELL, (xx + 1) * CELL);
                    i64 dy = n.p.y - iclamp(n.p.y, yy * CELL, (yy + 1) * CELL);
                    best = std::min(best, dx * dx + dy * dy);
                }
            n.clear = int(std::sqrt(double(best)));
        }

        const i64 dashLength = c.dash_v * c.dash_t;
        const int reach = dashLength > m.width() + m.height() ? 1 :
            std::clamp<int>(int((dashLength + CELL - 1) / CELL), 1, std::max(w, h));
        for (int i = 0; i < w * h; ++i) {
            auto& n = nodes[i];
            if (!n.good) continue;
            int x = i % w, y = i / w;
            for (int dy = -reach; dy <= reach; ++dy) for (int dx = -reach; dx <= reach; ++dx) {
                if (!m.inside(x + dx, y + dy) || (!dx && !dy)) continue;
                int j = (y + dy) * w + x + dx;
                if (!nodes[j].good) continue;
                V d = nodes[j].p - n.p;
                i64 dd = d.sq();
                if (std::max(iabs(dx), iabs(dy)) == 1) {
                    if (line(n.p, nodes[j].p, now))
                        n.edges.push_back({j, int(dx && dy ? rdiv(CELL * 1414, 1000) : CELL)});
                } else {
                    const i64 dash = c.dash_v * c.dash_t;
                    if (dd < (dash - CELL / 2) * (dash - CELL / 2) ||
                        dd > (dash + CELL / 4) * (dash + CELL / 4)) continue;
                    V land = n.p + d.unit().scaled(dash / 1000.0);
                    if (!body(land, now) || wall_between(m, n.p.x, n.p.y, land.x, land.y) ||
                        line(n.p, nodes[j].p, now)) continue;
                    n.edges.push_back({j, int(c.run_v * (c.dash_pre + c.dash_t + c.dash_post + 4))});
                }
            }
        }
        // The search runs in reverse, including jumps which only work in one direction.
        std::vector<std::vector<Edge>> incoming(nodes.size());
        for (int i = 0; i < int(nodes.size()); ++i)
            for (Edge e : nodes[i].edges) incoming[e.id].push_back({i, e.cost});
        for (int i = 0; i < int(nodes.size()); ++i) nodes[i].edges.swap(incoming[i]);
        updateDeadlines();
        stage = z;
        lastGoal = lastGoalType = lastSafeEpoch = -1;
    }

    void updateDeadlines() {
        // Work back from the last safe ground, propagating a latest departure.
        using Mark = std::pair<int, int>;
        std::priority_queue<Mark> queue;
        const int forever = int(gi->cfg.steps + 160);
        int latestFall = -1;
        for (auto& a : nodes) {
            a.latest = -1;
            if (a.good) latestFall = std::max(latestFall, a.fall < 0 ? forever : a.fall - 1);
        }
        for (int i = 0; i < int(nodes.size()); ++i) {
            auto& a = nodes[i];
            if (!a.good || (a.fall < 0 ? forever : a.fall - 1) != latestFall) continue;
            a.latest = latestFall;
            queue.emplace(a.latest, i);
        }
        const i64 speed = std::max<i64>(1, gi->cfg.run_v);
        while (!queue.empty()) {
            auto [latest, node] = queue.top();
            queue.pop();
            if (nodes[node].latest != latest) continue;
            for (Edge e : nodes[node].edges) {
                auto& prior = nodes[e.id];
                const int occupy = prior.fall < 0 ? forever : prior.fall - 1;
                const int travel = int((e.cost + speed - 1) / speed);
                const int value = std::min(occupy, latest - travel);
                if (value <= prior.latest) continue;
                prior.latest = value;
                queue.emplace(value, e.id);
            }
        }
    }

    int timeToLeave(V x) const {
        int i = id(x);
        if (i >= 0 && nodes[i].good) return nodes[i].latest;
        int best = -1;
        int gx = cell_index(x.x), gy = cell_index(x.y);
        for (int y = std::max(0, gy - 2); y <= std::min(h - 1, gy + 2); ++y)
            for (int u = std::max(0, gx - 2); u <= std::min(w - 1, gx + 2); ++u) {
                const auto& node = nodes[y * w + u];
                if (node.good && node.latest - 7 > best && line(x, node.p, now + 4))
                    best = node.latest - 7;
            }
        return best;
    }

    std::vector<int> field(const std::vector<std::pair<int, int>>& goals, bool refuge) const {
        using Item = std::pair<int, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
        std::vector<int> dist(nodes.size(), Inf);
        for (auto [seed, cost] : goals) {
            if (cost >= dist[seed]) continue;
            dist[seed] = cost;
            queue.emplace(cost, seed);
        }
        while (!queue.empty()) {
            auto [d, j] = queue.top();
            queue.pop();
            if (d != dist[j]) continue;
            for (Edge e : nodes[j].edges) {
                const auto& n = nodes[j];
                int late = int(now + (d + e.cost) / std::max<i64>(1, gi->cfg.run_v) + 5);
                int penalty = n.fall >= 0 && n.fall <= late ? (refuge ? 2100 : 5000) : 0;
                penalty += std::max(0, 740 - n.clear) / 7;
                int cand = d + e.cost + penalty;
                if (cand >= dist[e.id]) continue;
                dist[e.id] = cand;
                queue.emplace(cand, e.id);
            }
        }
        return dist;
    }

    void update(i64 t) {
        now = t;
        int z = stages->index(t);
        if (z != stage) {
            if (z < int(prepared.size()) && !prepared[z].empty()) {
                nodes.swap(prepared[z]);
                stage = z;
                lastGoal = lastGoalType = lastSafeEpoch = -1;
            } else {
                rebuild(z);
            }
        }
        const int epoch = int(t / 8);
        if (lastSafeEpoch < 0 || (t + 90 >= gi->cfg.col_start && epoch != lastSafeEpoch)) {
            lastSafeEpoch = epoch;
            std::vector<std::pair<int, int>> goals;
            for (int i = 0; i < w * h; ++i) {
                const auto& n = nodes[i];
                if (!n.good) continue;
                const int life = n.fall < 0 ? 150 : std::min(150, n.fall - int(t));
                int cost = std::max(0, 130 - life) * 85;
                cost += std::max(0, 1750 - n.clear);
                if (n.latest >= 0 && n.latest < t + 38) cost += int((t + 38 - n.latest) * 34);
                if (n.grass) cost -= 160;
                goals.emplace_back(i, cost);
            }
            safe = field(goals, true);
            lastGoal = -1;
        }
    }

    void setTarget(V p, bool pickup) {
        int i = id(p);
        if (lastGoal == i && lastGoalType == int(pickup) && (p - markedGoal).sq() < CELL * CELL / 4) return;
        lastGoal = i;
        lastGoalType = int(pickup);
        markedGoal = p;
        std::vector<std::pair<int, int>> goals;
        double closest = 1e10;
        int best = -1;
        const i64 pick = gi->cfg.pick_r;
        for (int k = 0; k < w * h; ++k) {
            if (!nodes[k].good) continue;
            i64 dd = (nodes[k].p - p).sq();
            if (dd < closest) {
                closest = dd;
                best = k;
            }
            if (pickup && dd <= pick * pick) goals.emplace_back(k, int(std::sqrt(double(dd)) / 4));
        }
        if (best >= 0 && goals.empty() && (!pickup || closest < (pick + CELL / 2) * (pick + CELL / 2)))
            goals.emplace_back(best, 0);
        target = field(goals, false);
    }

    int value(V p, const std::vector<int>& f, bool connect = false) const {
        int i = id(p);
        int best = Inf;
        if (i >= 0 && nodes[i].good && f[i] < Inf) {
            best = f[i] + int((p - nodes[i].p).len() * 0.35);
            if (!connect) return best;
        }
        int x = cell_index(p.x), y = cell_index(p.y);
        for (int yy = std::max(0, y - 2); yy <= std::min(h - 1, y + 2); ++yy)
            for (int xx = std::max(0, x - 2); xx <= std::min(w - 1, x + 2); ++xx) {
                int j = yy * w + xx;
                if (!nodes[j].good || f[j] == Inf) continue;
                int v = f[j] + int((p - nodes[j].p).len());
                if (v < best && (!connect || line(p, nodes[j].p, now))) best = v;
            }
        return best;
    }

    int clearance(V p) const {
        // Grid centers provide a cheap lower resolution estimate of exposure to kicks.
        int i = id(p);
        if (i < 0 || !nodes[i].good) return 0;
        return std::max(0, nodes[i].clear - int((nodes[i].p - p).len()));
    }

    V downhill(V p, const std::vector<int>& f) const {
        if (f.empty()) return {};
        int x = cell_index(p.x), y = cell_index(p.y);
        int base = value(p, f, true);
        V answer;
        double best = 0;
        for (int yy = std::max(0, y - 3); yy <= std::min(h - 1, y + 3); ++yy)
            for (int xx = std::max(0, x - 3); xx <= std::min(w - 1, x + 3); ++xx) {
                int j = yy * w + xx;
                if (!nodes[j].good || f[j] >= Inf) continue;
                V delta = nodes[j].p - p;
                const double r = delta.len();
                if (r < 120) continue;
                double merit = (base - f[j] - 75) / (r + 350);
                if (merit > best && line(p, nodes[j].p, now + int(r / gi->cfg.run_v))) {
                    best = merit;
                    answer = delta.unit();
                }
            }
        return answer;
    }

    std::vector<V> jumpHeadings(V p, bool refuge) const {
        const i64 distance = gi->cfg.dash_v * gi->cfg.dash_t;
        const auto& f = refuge ? safe : target;
        const int gx = cell_index(p.x), gy = cell_index(p.y);
        const int r = int(distance / CELL + 2);
        const i64 end = now + gi->cfg.dash_pre + gi->cfg.dash_t - 1;
        std::vector<std::pair<double, V>> choices;
        for (int y = std::max(0, gy - r); y <= std::min(h - 1, gy + r); ++y)
            for (int x = std::max(0, gx - r); x <= std::min(w - 1, gx + r); ++x) {
                int j = y * w + x;
                const auto& node = nodes[j];
                if (!node.good || f[j] >= Inf) continue;
                const V d = node.p - p;
                const double length = d.len();
                if (std::abs(length - distance) > CELL * .72) continue;
                const V u = d.unit();
                const V land = p + u.scaled(distance / 1000.0);
                if (!body(land, end) || line(p, land, end)) continue;
                if (wall_between(stages->map(end), p.x, p.y, land.x, land.y)) continue;
                double value = f[j] + std::abs(length - distance) * .48 - std::min(1300, node.clear) * .25;
                choices.emplace_back(value, u);
            }
        std::stable_sort(choices.begin(), choices.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<V> result;
        for (const auto& p : choices) {
            bool close = false;
            for (V x : result) if ((x - p.second).sq() < 124 * 124) close = true;
            if (!close) result.push_back(p.second);
            if (result.size() >= 6) break;
        }
        return result;
    }
};

struct Track {
    i64 at = -1;
    Player seen;
    V drive;
    V average;
    double wobble = 0.0;
    double speed = .69;
    double turning = 0.0;
    std::array<double, Models> error{710, 800, 860, 860};
    int guards = 0;
    int offerings = 0;
};

struct Frame {
    i64 at = -1;
    Player p;
    Dart d;
    Action a;
};

struct Sample {
    std::array<V, MAXP> p;
    std::array<V, MAXP> v;
    std::array<Dart, MAXP> d;
    std::array<int, MAXP> st{}, timer{}, cd{};
    std::array<bool, MAXP> ok{};
    void put(const Game& g) {
        for (int k = 0; k < g.n; ++k) {
            p[k] = pos(g.p[k]); v[k] = vel(g.p[k]); d[k] = g.d[k];
            st[k] = int(g.p[k].st); timer[k] = int(g.p[k].timer); cd[k] = int(g.p[k].cd);
            ok[k] = g.p[k].alive && !g.p[k].unknown;
        }
    }
};

struct Plan {
    Action a;
    int switchAt = -1;
    int foe = -1;
    double quick = 0;
    double risk = 0;
    double seed = -1e9;
};

struct Bot {
    const GameInfo* gi = nullptr;
    Stages terrain;
    Navigation nav;
    std::array<Track, MAXP> track;
    std::array<Frame, History> history;
    std::array<V, 24> ring;
    std::array<Game, Models> roots;
    std::array<Game, History> pastWorld;
    std::array<std::array<Sample, Horizon + 1>, Models> outlook;
    std::array<Sample, History> past;
    struct Prediction {
        i64 at = -1;
        std::array<std::array<V, MAXP>, Models> x;
        std::array<std::array<bool, MAXP>, Models> open;
    };
    std::array<Prediction, History + 7> predictions;
    std::array<std::array<double, Models>, MAXP> belief;
    std::array<std::array<std::array<V, 8>, 2>, MAXP> heading;
    std::array<std::array<V, Models>, MAXP> hiddenPosition;
    std::array<std::array<bool, Models>, MAXP> hiddenValid;
    const Obs* obs = nullptr;
    int me = 0, n = 0, numFoes = 0, primary = -1;
    bool armed = false, pickup = false, urgent = false, safeRoute = false;
    bool tactical = false, localFight = false;
    i64 tick = 0;
    V here, wish, objective, lastMove;
    int handSide = 1, lastTarget = -1;
    double dangerRange = 1e9;
    double objectiveBias = 1;
    int bestSafe = Inf, bestTarget = Inf;
    const Config& c() const { return gi->cfg; }

    void init(const GameInfo& g) {
        gi = &g; n = g.n; me = g.seat;
        terrain.init(g);
        nav.init(g, terrain);
        rays.reserve(112);
        hazards.reserve(7);
        for (int i = 0; i < int(ring.size()); ++i)
            ring[i] = V{1000, 0}.rotated(2 * Pi * i / ring.size());
        u64 salt = u64(g.spx[me]) * 379 + u64(g.spy[me]) * 643 + g.seat;
        handSide = ((salt ^ (salt >> 7)) & 1) ? 1 : -1;
    }

    bool foe(int k) const { return k != me && gi->team[k] != gi->team[me]; }
    const Frame* frame(i64 t) const {
        if (t < 0) return nullptr;
        const Frame& f = history[t % History];
        return f.at == t ? &f : nullptr;
    }
    bool clear(V a, V b, i64 t) const {
        return !terrain.occluded(a, b, t);
    }

    void updateTracks(const Obs& o, i64 delayed) {
        for (int k = 0; k < n; ++k) {
            if (k == me || !o.p[k].vis || !o.p[k].alive) continue;
            Track& tr = track[k];
            if (tr.at == delayed) continue;
            const Player& p = o.p[k];
            const Prediction& pred = predictions[delayed % predictions.size()];
            if (p.st == S_FREE && pred.at == delayed) {
                for (int m = 0; m < Models; ++m) {
                    if (!pred.open[m][k]) continue;
                    const double err = std::min(2100.0, (pos(p) - pred.x[m][k]).len());
                    tr.error[m] = .89 * tr.error[m] + .11 * err;
                }
            }
            if (p.st == S_THROW && (tr.at < 0 || tr.seen.st != S_THROW)) ++tr.offerings;
            if (p.st == S_SLASH && (tr.at < 0 || tr.seen.st != S_SLASH)) {
                if (const Frame* f = frame(delayed)) {
                    const double dist = (pos(p) - pos(f->p)).len();
                    if (dist > 2050 && dart_flying(f->d.st) && (pos(p) - pos(f->d)).sq() < 3000 * 3000)
                        ++tr.guards;
                }
            }
            if (p.st == S_FREE && delayed > 4)
                tr.speed = .90 * tr.speed + .10 * std::min(1.05, vel(p).len() / c().run_v);
            if (p.st == S_FREE && tr.seen.st == S_FREE && delayed - tr.at == 1 &&
                vel(p).sq() >= 162 * 162 && vel(tr.seen).sq() >= 162 * 162) {
                const V u = vel(tr.seen), v = vel(p);
                const double a = std::atan2(u.left().dot(v), u.dot(v));
                const double turn = std::abs(a) < 1.04 ? std::clamp(a, -.36, .36) : 0.0;
                tr.turning = .64 * tr.turning + .36 * turn;
            } else {
                tr.turning *= .65;
            }
            if (p.st == S_FREE || p.st == S_THROW || p.st == S_THROWN) {
                V v = vel(p);
                if (v.sq() >= 35 * 35) {
                    V u = v.unit();
                    if (tr.drive.sq() > 0 && p.st == S_FREE)
                        tr.wobble = .82 * tr.wobble + .18 * (1 - u.dot(tr.drive) / 1e6);
                    const double speed = p.st == S_FREE ? std::min(1.0, v.len() / c().run_v) : 1.0;
                    tr.drive = u.scaled(speed);
                    tr.average = tr.average.scaled(.67) + tr.drive.scaled(.33);
                } else {
                    tr.drive = tr.drive.scaled(.35);
                    tr.average = tr.average.scaled(.75);
                }
            }
            tr.at = delayed;
            tr.seen = p;
        }
    }

    void updateBelief() {
        // Keep a prior even when the last few observations fit one model well.
        const double prior[Models]{.39, .23, .19, .19};
        for (int k = 0; k < n; ++k) {
            double total = 0;
            for (int m = 0; m < Models; ++m) {
                belief[k][m] = prior[m] * std::exp(-.00135 * track[k].error[m]);
                total += belief[k][m];
            }
            for (int m = 0; m < Models; ++m)
                belief[k][m] = .56 * prior[m] + .44 * belief[k][m] / total;
            for (int m = 0; m < 2; ++m) for (int age = 0; age < 8; ++age)
                heading[k][m][age] = track[k].drive.rotated(track[k].turning * age * (m == 0 ? .67 : .32));
        }
    }

    void rememberForecast() {
        const i64 t = tick + 3;
        Prediction& pred = predictions[t % predictions.size()];
        pred.at = t;
        for (int m = 0; m < Models; ++m) for (int k = 0; k < n; ++k) {
            const auto& f = outlook[m][3];
            pred.x[m][k] = f.p[k];
            pred.open[m][k] = f.ok[k] && f.st[k] == S_FREE;
        }
    }

    void prepareHidden(const Obs& o, i64 delayed) {
        const Frame* self = frame(delayed);
        const V viewer = self ? pos(self->p) : here;
        for (auto& b : hiddenValid) b.fill(false);
        for (int k = 0; k < n; ++k) {
            if (!foe(k) || !o.p[k].alive || o.p[k].vis) continue;
            const auto& tr = track[k];
            const i64 age = tr.at < 0 ? delayed : delayed - tr.at;
            if (age > 24) continue;
            const V last = tr.at < 0 ? V{gi->spx[k], gi->spy[k]} : pos(tr.seen);
            if (age == 0) {
                for (int m = 0; m < Models; ++m) {
                    hiddenValid[k][m] = true;
                    hiddenPosition[k][m] = last;
                }
                continue;
            }
            if ((last - here).len() > 6700 + age * c().run_v) continue;
            const double range = age * c().run_v + CELL * .72;
            const V drive = tr.at < 0 ?
                (V{gi->M().width() / 2, gi->M().height() / 2} - last).unit() : tr.drive;
            V bias[Models]{
                last + drive.scaled(std::min(5.0, age * .62) * c().run_v / 1000.0),
                last + drive.scaled(std::min(8.0, age * .80) * c().run_v / 1000.0),
                last + drive.rotated(.76).scaled(std::min(8.0, age * .76) * c().run_v / 1000.0),
                last + drive.rotated(-.76).scaled(std::min(8.0, age * .76) * c().run_v / 1000.0)
            };
            const Map& m = terrain.map(delayed);
            double quality[Models]{1e12, 1e12, 1e12, 1e12};
            const i64 scale = std::min<i64>(12 * CELL, i64(range + CELL));
            const int lx = std::max(0, cell_index(last.x - scale)), ly = std::max(0, cell_index(last.y - scale));
            const int rx = std::min(m.nx - 1, cell_index(last.x + scale)), ry = std::min(m.ny - 1, cell_index(last.y + scale));
            for (int y = ly; y <= ry; ++y) for (int x = lx; x <= rx; ++x) {
                if (m.at(x, y) != C_GRASS) continue;
                const V center{cell_center(x), cell_center(y)};
                if ((center - last).sq() > range * range || (center - viewer).sq() <= c().see_r * c().see_r ||
                    (center - here).sq() > 6200 * 6200 || !nav.body(center, delayed))
                    continue;
                const bool barrier = !clear(last, center, delayed);
                for (int j = 0; j < Models; ++j) {
                    double q = (center - bias[j]).len() + (center - last).len() * .13 + (barrier ? 890 : 0);
                    if (q < quality[j]) {
                        quality[j] = q;
                        hiddenPosition[k][j] = center;
                        hiddenValid[k][j] = true;
                    }
                }
            }
        }
    }

    V enemyMove(const Game& g, int k, int model, int lead) const {
        const auto& p = g.p[k];
        const Dart& d = g.d[k];
        if (!p.alive || p.unknown || (p.st != S_FREE && p.st != S_THROW && p.st != S_THROWN))
            return {};
        const V x = pos(p);
        V drive = track[k].drive;
        V own = pos(g.p[me]);
        V rel = own - x;
        double dist = rel.len();
        const V core{gi->M().width() / 2, gi->M().height() / 2};
        if (track[k].at < 0 || g.t < 5) drive = (core - x).unit();
        V u = drive;
        const auto& tr = track[k];
        if (tr.at >= 0 && p.st == S_FREE && u.sq() > 480 * 480) {
            const int elapsed = int(std::clamp<i64>(g.t - tr.at, 0, 7));
            u = g.t >= 5 ? heading[k][model != 0][elapsed] :
                u.rotated(tr.turning * elapsed * (model == 0 ? .67 : .32));
        }
        if (d.st == D_GROUND) {
            V dart = pos(d) - x;
            if (dart.sq() < 2600 * 2600 && clear(x, pos(d), g.t) && nav.on(x + dart.unit().scaled(.42), g.t))
                u = (dart.unit().scaled(.7) + drive.scaled(.3)).unit();
        }
        if (model == 1 && lead >= 0 && dist < 5900 && foe(k) && (d.st == D_HAND || dart_flying(d.st))) {
            V attack = rel.unit();
            if (d.st != D_HAND && dist < 2300) attack = -attack;
            u = (drive.scaled(.55) + attack.scaled(.65)).unit();
        }
        const bool comingBack = d.st == D_BACK || d.st == D_CALL;
        if (model >= 2 && (dist < 8500 || (comingBack && (pos(d) - own).sq() < 5200 * 5200)) &&
            foe(k) && (lead >= 0 || comingBack || track[k].wobble > .18)) {
            const double width = std::clamp(tr.wobble * .51, 0.0, .24);
            const double angle = (model == 2 ? 1 : -1) * (lead >= 4 ? .88 + width : .51 + width);
            if (p.st == S_FREE && (d.st != D_GROUND || dist < 2200)) {
                if (comingBack && u.sq() < 420 * 420) {
                    u = rel.unit().left().scaled(model == 2 ? .94 : -.94);
                } else {
                    u = u.sq() > 140 * 140 ? u.rotated(angle) : rel.unit().left().scaled(model == 2 ? 1 : -1);
                }
            }
        }
        if (u.sq() > 0 && !nav.on(x + u.scaled(c().run_v / 1000.0), g.t)) {
            const V toward = (core - x).unit();
            double best = -1e9;
            V alt;
            for (int j : {3, -3, 6, -6, 9, -9}) {
                V v = u.rotated(j * Pi / 12);
                if (nav.on(x + v.scaled(c().run_v / 1000.0), g.t)) {
                    double val = .85 * u.dot(v) + .15 * toward.dot(v);
                    if (val > best) { best = val; alt = v; }
                }
            }
            u = alt;
        }
        return u;
    }

    void fillActions(Game& g, int model, int lead, Action* acts) const {
        for (int k = 0; k < n; ++k) {
            if (k != me) acts[k] = action(enemyMove(g, k, model, lead));
        }
    }

    void reconstruct(const Obs& o, i64 delayed, int model) {
        // Foreign states start on the delayed frame. Our frames are measured.
        Game g(*gi, o);
        g.t = delayed;
        for (int k = 0; k < n; ++k) {
            if (k == me || !g.p[k].unknown || !g.p[k].alive) continue;
            const Track& tr = track[k];
            if (hiddenValid[k][model]) {
                const V x = hiddenPosition[k][model];
                auto& p = g.p[k];
                p.x = x.x; p.y = x.y;
                p.px = p.x; p.py = p.y;
                if (tr.at >= 0) {
                    p.vx = rdiv(tr.drive.x * c().run_v, 1000);
                    p.vy = rdiv(tr.drive.y * c().run_v, 1000);
                    p.cd = std::max<i64>(0, tr.seen.cd + tr.at - delayed);
                }
                p.unknown = 0;
                if (g.d[k].st == D_HAND) {
                    g.d[k].x = p.x;
                    g.d[k].y = p.y;
                }
            }
        }
        for (; g.t < tick;) {
            if (const Frame* f = frame(g.t)) {
                g.p[me] = f->p; g.p[me].team = gi->team[me]; g.d[me] = f->d;
            }
            if (model == 0) {
                past[g.t % History].put(g);
                pastWorld[g.t % History] = g;
            }
            Action acts[MAXP]{};
            fillActions(g, model, -1, acts);
            if (const Frame* f = frame(g.t)) acts[me] = f->a;
            terrain.step(g, acts);
        }
        g.p[me] = o.me();
        g.p[me].team = gi->team[me];
        g.d[me] = o.my_dart();

        for (int k = 0; k < n; ++k) {
            if (k == me || !g.p[k].alive || foe(k)) continue;
            const i64 code = o.p[k].msg - 0xA000;
            if (code < 0 || code >= gi->M().nx * gi->M().ny) continue;
            V sent{cell_center(int(code % gi->M().nx)), cell_center(int(code / gi->M().nx))};
            V adjusted = sent + vel(g.p[k]);
            if ((sent - pos(g.p[k])).sq() < 1000 * 1000 && nav.body(adjusted, tick)) {
                g.p[k].x = adjusted.x;
                g.p[k].y = adjusted.y;
            }
        }
        roots[model] = g;
    }

    V continuation(const Player& p, V u, i64 t) const {
        if (!u.sq() || (p.st != S_FREE && p.st != S_THROW && p.st != S_THROWN)) return u;
        V x = pos(p);
        double speed = c().run_v / 1000.0;
        if (p.st != S_FREE) speed *= c().throw_move / 100.0;
        if (nav.on(x + u.scaled(speed), t) && nav.on(x + u.scaled(speed * 1.75), t + 1)) return u;
        for (int j : {1, -1, 2, -2}) {
            V v = u.rotated(handSide * j * Pi / 4);
            if (nav.on(x + v.scaled(speed * 1.3), t + 1)) return v;
        }
        return {};
    }

    void forecast(int model) {
        Game g = roots[model];
        const V assumed = vel(g.p[me]).sq() > 0 ? vel(g.p[me]).unit() : lastMove;
        outlook[model][0].put(g);
        for (int j = 0; j < Horizon; ++j) {
            Action acts[MAXP]{};
            fillActions(g, model, j, acts);
            acts[me] = action(continuation(g.p[me], assumed, g.t));
            terrain.step(g, acts);
            outlook[model][j + 1].put(g);
        }
    }


    void chooseObjective() {
        primary = -1;
        numFoes = 0;
        dangerRange = 1e9;
        double best = 1e9;
        const Sample& s = outlook[0][0];
        for (int k = 0; k < n; ++k) {
            if (!foe(k) || !obs->p[k].alive) continue;
            ++numFoes;
            if (!s.ok[k]) continue;
            double d = (s.p[k] - here).len();
            bool line = d < 8500 && clear(here, s.p[k], tick);
            double proximity = d + (line ? 0 : 1700);
            if (s.d[k].st != D_HAND && armed) proximity -= 400;
            if (c().mode == M_TEAM) {
                double other = d;
                for (int j = 0; j < n; ++j) {
                    if (j == me || foe(j) || !s.ok[j]) continue;
                    const double distance = (s.p[j] - s.p[k]).len();
                    if (distance + 850 < other && clear(s.p[j], s.p[k], tick)) other = distance;
                }
                proximity += std::clamp((d - other - 850) * .38, 0.0, 880.0);
            }
            if (k == lastTarget) proximity -= 230;
            if (obs->p[k].vis == 0) proximity += 790;
            if (proximity < best) { best = proximity; primary = k; }
            if (line) dangerRange = std::min(dangerRange, d);
        }
        lastTarget = primary;
        tactical = dangerRange < 8000;
        localFight = primary >= 0 && dangerRange < 4700;
        for (int k = 0; k < n; ++k) {
            if (k != me && (dart_flying(s.d[k].st) || s.d[k].st == D_RISE) &&
                (pos(s.d[k]) - here).sq() < 7000 * 7000) tactical = true;
        }

        bestSafe = nav.value(here, nav.safe, true);
        int cell = nav.id(here);
        const i64 fall = cell >= 0 ? nav.nodes[cell].fall : 0;
        const i64 remaining = fall < 0 ? 5000 : fall - tick;
        urgent = remaining < 26 || (remaining < 65 && bestSafe > 3800);
        const int leave = nav.timeToLeave(here);
        if (leave >= 0 && tick + 20 >= leave && bestSafe > 1000) urgent = true;
        pickup = obs->my_dart().st == D_GROUND || obs->my_dart().st == D_RISE;
        const V center{gi->M().width() / 2, gi->M().height() / 2};
        objective = pickup ? pos(obs->my_dart()) : primary >= 0 ? s.p[primary] : center;
        nav.setTarget(objective, pickup);
        bestTarget = nav.value(here, nav.target, true);
        safeRoute = urgent || primary < 0 || bestTarget >= Inf;
        if (pickup && bestTarget < Inf && remaining > 36 && primary < 0) safeRoute = false;
        objectiveBias = 1.0;
        const V delta = objective - here;
        const V route = nav.downhill(here, safeRoute ? nav.safe : nav.target);
        const bool direct = delta.sq() < 10000 * 10000 &&
            nav.line(here, here + delta.unit().scaled(std::min(2.6, delta.len() / 1000)), tick + 7);
        wish = safeRoute || !direct || bestTarget > delta.len() * 1.50 + 850 ? route : delta.unit();

        if (primary >= 0 && !safeRoute && !pickup && direct && localFight) {
            const bool oppArmed = s.d[primary].st == D_HAND;
            const double d = delta.len();
            V dir = delta.unit();
            double side = .58 * handSide;
            if ((lastMove.dot(dir.left()) * handSide) < -350000) side = -side;
            if (armed && oppArmed) {
                const double preferred = desirableRange(primary);
                const double radial = std::clamp((d - preferred) / 1050, -1.1, 1.1);
                wish = (dir.scaled(radial) + dir.left().scaled(side)).unit();
                objectiveBias = .55;
            } else if (!armed) {
                const double wanted = oppArmed ? 5275 : 3275;
                const double radial = std::clamp((d - wanted) / 700, -1.25, .35);
                wish = (dir.scaled(radial) + dir.left().scaled(side)).unit();
                objectiveBias = .20;
            }
        }
        if (!wish.sq() && !safeRoute && direct && delta.len() > 60)
            wish = delta.unit();
        if (c().mode == M_TEAM && !urgent && tactical) {
            V steer;
            for (int k = 0; k < n; ++k) {
                if (k == me || foe(k) || !s.ok[k]) continue;
                const V v = here - s.p[k];
                const double d = v.len();
                if (d < 2450) steer = steer + v.unit().scaled(.90 * (2450 - d) / 2450);
            }
            if (steer.sq() > 110 * 110) {
                V aim = (wish + steer).unit();
                if (nav.body(here + aim.scaled(.38), tick + 1)) wish = aim;
            }
        }
    }

    double desirableRange(int k) const {
        if (numFoes > 1) return 4250;
        const Track& tr = track[k];
        return 3780 + 720 * std::clamp((.86 - tr.speed) * 3.2 + .38 * tr.guards, 0.0, 1.0);
    }

    struct Ray {
        std::array<V, Horizon + 1> start, end;
        int first = Horizon + 1, last = 0, attacker = -1;
        i64 launch = 0;
        V dir;
        double weight = 0;
    };
    std::vector<Ray> rays;

    void makeRay(i64 launch, V origin, V dir, double weight, int owner) {
        Ray ray;
        ray.weight = weight;
        ray.attacker = owner;
        ray.launch = launch;
        ray.dir = dir;
        i64 left = c().range;
        V p = origin;
        for (int age = 0; left > 0 && age < Horizon; ++age) {
            const i64 t = launch + c().throw_pre + age;
            if (t >= tick + Horizon) break;
            const i64 flight = std::min(left, std::max(c().dart_vmin, c().dart_v - c().dart_dec * age));
            left -= flight;
            V b{p.x + rdiv(dir.x * flight, 1000), p.y + rdiv(dir.y * flight, 1000)};
            const bool hitWall = seg_wall(terrain.map(t), p.x, p.y, b.x, b.y, b.x, b.y);
            if (t < tick) {
                const Frame* a = frame(t);
                const Frame* z = frame(t + 1);
                V dest = z ? pos(z->p) : here;
                if (a && segmentDistance2(p - pos(a->p), b - dest) < sqHit(.97)) return;
            } else {
                const int i = int(t - tick);
                ray.first = std::min(ray.first, i);
                ray.last = i + 1;
                ray.start[i] = p;
                ray.end[i] = b;
            }
            p = b;
            if (hitWall) break;
        }
        if (ray.last > ray.first) rays.push_back(ray);
    }

    double sqHit(double scale = 1) const {
        const double r = (c().body_r + c().dart_r) * scale;
        return r * r;
    }

    void latentShots(i64 delayed) {
        rays.clear();
        if (!tactical || c().throw_pre >= Horizon) return;
        const i64 start = std::max(delayed, tick - 4);
        for (int k = 0; k < n; ++k) {
            if (!foe(k) || !obs->p[k].alive || !outlook[0][0].ok[k]) continue;
            const V center = outlook[0][0].p[k];
            if ((center - here).sq() > 7600 * 7600) continue;
            for (i64 t = start; t <= tick + 5; ++t) {
                const Sample& s = t < tick ? past[t % History] : outlook[0][t - tick];
                if (s.d[k].st != D_HAND || s.st[k] != S_FREE || !s.ok[k]) continue;
                const Frame* seen = frame(std::max<i64>(0, t - c().delay));
                const Frame* actual = frame(t);
                V px = actual ? pos(actual->p) : here + vel(obs->me()).scaled(double(t - tick));
                V historic = seen ? pos(seen->p) : px;
                V v = seen ? vel(seen->p) : vel(obs->me());
                V at = s.p[k];
                const V origin = at + track[k].drive.scaled(c().run_v * c().throw_pre * c().throw_move / 100000.0);
                const double d = (px - origin).len();
                if (d < 650 || d > 7800 || !clear(origin, px, t)) continue;
                const double reach = std::clamp((6800.0 - d) / 2500, .28, 1.0);
                double weight = reach * (t <= tick ? 1.0 : t <= tick + 1 ? .74 : .48);
                weight *= (numFoes > 1 ? .87 : 1);
                if (track[k].offerings == 0) weight *= .91;
                if (seen && seen->p.expose == 0 && !dart_flying(seen->d.st) && seen->d.st != D_RISE &&
                    (historic - at).sq() > (c().see_r + 260) * (c().see_r + 260) &&
                    terrain.map(seen->at).at(cell_index(historic.x), cell_index(historic.y)) == C_GRASS) {
                    weight *= .44;
                }
                const double lead = std::min(10.0, c().delay + c().throw_pre + d / c().dart_v);
                V aims[4] = {historic + v.scaled(lead), historic + v.scaled(lead * .50), historic,
                             px + vel(obs->me()).scaled(2.3)};
                V prev;
                for (int a = 0; a < 4; ++a) {
                    const V dir = (aims[a] - origin).unit();
                    if (dir.sq() == 0 || (prev - dir).sq() < 46 * 46) continue;
                    makeRay(t, origin, dir, weight * (a == 0 ? 1.0 : a == 1 ? .80 : a == 2 ? .57 : .76), k);
                    prev = dir;
                }
            }
        }
    }

    double latentRisk(const std::array<V, Horizon + 1>& path, int end, const Game& g, const Plan& plan) const {
        double worst[MAXP]{}, sum[MAXP]{};
        int count[MAXP]{};
        for (const Ray& r : rays) {
            double close = 0;
            if (!g.p[r.attacker].alive && g.p[r.attacker].dead_t < tick + r.first - c().throw_pre) continue;
            for (int i = r.first; i < std::min(end, r.last); ++i) {
                const V d0 = r.start[i] - path[i], d1 = r.end[i] - path[i + 1];
                if (d0.sq() > 3300 * 3300 && d1.sq() > 3300 * 3300) continue;
                const double d = segmentDistance2(d0, d1);
                const double margin = std::clamp((sqHit(1.90) - d) / (sqHit(1.90) - sqHit(.80)), 0.0, 1.0);
                close = std::max(close, margin * (1.0 - .042 * i));
            }
            int k = r.attacker;
            ++count[k];
            double x = close * r.weight;
            sum[k] += x;
            worst[k] = std::max(worst[k], x);
        }
        double risk = 0;
        for (int k = 0; k < n; ++k) if (count[k])
            risk += 25.0 * worst[k] + 17.0 * sum[k] / count[k];
        if (plan.a.act == A_DASH || plan.a.act == A_RECALL) risk *= 1.28;
        if (plan.a.act == A_MELEE) risk *= armed ? .97 : 1.45;
        return risk;
    }

    struct Trigger {
        int who = -1, kind = A_NONE;
        i64 at = 0;
        V aim;
        double priority = 0, weight = 0;
    };
    struct Hazard {
        Trigger trigger;
        Game world;
    };
    std::vector<Hazard> hazards;

    Action movingMelee(const Game& g, int who) const {
        const Player& a = g.p[who], &b = g.p[me];
        if (!a.alive || !b.alive || a.unknown || !g.dash_moving(a) || !c().dash_slash) return {};
        bool blade = g.d[who].st == D_HAND;
        if (!blade && (b.st == S_STUN || (b.st == S_FREE && b.timer > 0))) return {};
        V source = pos(a), v = vel(a);
        const i64 pre = blade ? c().slash_pre : c().kick_pre;
        if (pre >= Horizon) return {};
        for (int i = 0; i < pre; ++i) {
            V change = -v;
            cap_len(change.x, change.y, c().acc);
            v = v + change;
            move_body(terrain.map(g.t + i), c().body_r, source.x, source.y, v.x, v.y);
            if (!nav.on(source, g.t + i)) return {};
        }
        V target = pos(b) + vel(b).scaled(pre * (b.st == S_FREE ? .76 : 1.0));
        V diff = target - source;
        const double r = (blade ? c().slash_reach : c().kick_reach) + 45;
        if (diff.sq() > r * r || !clear(source, target, g.t + pre - 1)) return {};
        return action({}, A_MELEE, diff);
    }

    Action triggerAction(const Game& g, const Trigger& tr, Action fallback) const {
        if (g.t == tr.at) {
            const V dir{fallback.mx, fallback.my};
            return action(dir, tr.kind, tr.aim);
        }
        if (tr.kind == A_DASH && g.t > tr.at) {
            Action melee = movingMelee(g, tr.who);
            if (melee.act) return melee;
        }
        return fallback;
    }

    bool createHazard(Trigger tr) {
        // A guessed past attack must still be compatible with our own history.
        Game g = tr.at < tick ? pastWorld[tr.at % History] : roots[0];
        if (g.t > tick || (tr.at < tick && g.t != tr.at) || !g.p[tr.who].alive || g.p[tr.who].unknown) return false;
        double fit = 1.0;
        while (g.t < tick) {
            const Frame* f = frame(g.t);
            if (!f) return false;
            g.p[me] = f->p;
            g.p[me].team = gi->team[me];
            g.d[me] = f->d;
            Action a[MAXP]{};
            fillActions(g, 0, -1, a);
            a[me] = f->a;
            a[tr.who] = triggerAction(g, tr, a[tr.who]);
            terrain.step(g, a);
            if (!g.p[me].alive) return false;
            const Frame* post = frame(g.t);
            const auto& actual = post ? post->p : obs->me();
            const auto& actualD = post ? post->d : obs->my_dart();
            if ((g.p[me].st == S_STUN) != (actual.st == S_STUN)) return false;
            if ((pos(g.p[me]) - pos(actual)).sq() > 350 * 350) fit *= .80;
            if (g.d[me].st != actualD.st) fit *= .84;
        }
        if (tr.at < tick) {
            if (tr.kind == A_THROW && g.d[tr.who].st != D_OUT && g.p[tr.who].st != S_THROW) return false;
            if (tr.kind == A_DASH && g.p[tr.who].st != S_DASH && g.p[tr.who].st != S_SLASH &&
                g.p[tr.who].st != S_KICK && g.p[tr.who].st != S_SLASHED && g.p[tr.who].st != S_KICKED) return false;
        }
        g.p[me] = obs->me();
        g.p[me].team = gi->team[me];
        g.d[me] = obs->my_dart();
        tr.weight *= fit;
        hazards.push_back({tr, std::move(g)});
        return true;
    }

    void chooseHazards(i64 delayed) {
        hazards.clear();
        if (!tactical || n > 4) return;
        std::vector<Trigger> possibles;
        const V motion = vel(obs->me());
        const V routeMove = wish.scaled(c().run_v / 1000.0);
        for (const Ray& r : rays) {
            double best = 0;
            for (int j = r.first; j < std::min(r.last, 10); ++j) {
                const V mid = (r.start[j] + r.end[j]).scaled(.5);
                for (V v : {V{}, motion, routeMove, routeMove.scaled(1.54)}) {
                    V ahead = here + v.scaled(j + .5);
                    const double m = std::clamp((1750 - (mid - ahead).len()) / 1400, 0.0, 1.0);
                    best = std::max(best, m * (1 - .042 * j));
                }
            }
            if (best > .24)
                possibles.push_back({r.attacker, A_THROW, r.launch, r.dir, best * r.weight, r.weight});
        }
        for (int k = 0; k < n; ++k) {
            if (!foe(k) || !obs->p[k].alive || !outlook[0][0].ok[k]) continue;
            const Sample& root = outlook[0][0];
            if ((root.p[k] - here).sq() > 5600 * 5600) continue;
            const i64 earliest = std::max(delayed, tick - 4);
            for (i64 t = earliest; t <= tick + 1; ++t) {
                const Sample& s = t < tick ? past[t % History] : outlook[0][t - tick];
                if (!s.ok[k] || s.st[k] != S_FREE) continue;
                const bool blade = s.d[k].st == D_HAND;
                const bool active = dart_flying(s.d[k].st) || s.d[k].st == D_RISE;
                const V x = s.p[k];
                const Frame* f = frame(t);
                V y = f ? pos(f->p) : here + motion.scaled(double(t - tick));
                V v = f ? vel(f->p) : motion;
                const double d = (x - y).len();
                if (d > 5600 || !clear(x, y, t)) continue;
                V meet = here + motion.scaled(std::max<i64>(1, c().dash_pre + (t - tick) + 2));
                if (const Frame* old = frame(std::max<i64>(0, t - c().delay))) {
                    const V pred = pos(old->p) + vel(old->p).scaled(c().delay + c().dash_pre + c().dash_t * .65);
                    if ((pred - meet).sq() < 1650 * 1650 && nav.on(pred, t))
                        meet = meet.scaled(.57) + pred.scaled(.43);
                }
                if (c().dash_slash && (blade || active || nav.clearance(here) < 1400) &&
                    s.cd[k] == 0 && d > 1600 && (meet - x).sq() < 5100 * 5100 &&
                    nav.on(x + (meet - x).unit().scaled(c().dash_v / 1000.0), t)) {
                    const double p = std::clamp((5400 - (meet - x).len()) / 4500, .14, .91);
                    const double threat = blade ? 1.0 : active ? .77 : .58;
                    const double immediacy = t < tick - 1 ? 1.08 : t > tick ? .86 : 1.0;
                    possibles.push_back({k, A_DASH, t, (meet - x).unit(), p * .91 * threat * immediacy, .89 * threat});
                }
                const double reach = blade ? c().slash_reach : c().kick_reach;
                if (d < reach + 720 && t + (blade ? c().slash_pre : c().kick_pre) > tick - 2) {
                    const V aim = y + v.scaled(blade ? c().slash_pre : c().kick_pre) - x;
                    const double p = std::clamp((reach + 850 - d) / 1100, .20, 1.0);
                    const double threat = blade ? 1.0 : active ? .92 : .71;
                    possibles.push_back({k, A_MELEE, t, aim.unit(), p * 1.08 * threat, 1.06 * threat});
                }
            }
        }
        std::stable_sort(possibles.begin(), possibles.end(), [](const Trigger& a, const Trigger& b) {
            return a.priority > b.priority;
        });
        const int limit = n > 2 ? 5 : 6;
        int darts = 0, dash = 0;
        for (const Trigger& t : possibles) {
            if (int(hazards.size()) >= limit) break;
            if (t.kind == A_THROW && darts >= limit - 2 && dash == 0 && dangerRange < 4700) continue;
            bool duplicate = false;
            for (const Hazard& h : hazards) {
                const Trigger& q = h.trigger;
                if (q.who != t.who || q.kind != t.kind) continue;
                if (t.kind == A_MELEE || (iabs(q.at - t.at) <= 1 && (q.aim - t.aim).sq() < 165 * 165) ||
                    (t.kind == A_DASH && (q.at < tick - 1) == (t.at < tick - 1)))
                    duplicate = true;
            }
            if (duplicate || !createHazard(t)) continue;
            darts += t.kind == A_THROW;
            dash += t.kind == A_DASH;
        }
    }

    V ownThrowOrigin(V move) const {
        const double pct = c().throw_move / 100.0;
        V v = vel(obs->me());
        V p = here;
        for (int j = 0; j < c().throw_pre; ++j) {
            V w = move.scaled(pct * c().run_v / 1000) - v;
            cap_len(w.x, w.y, c().acc);
            v = v + w;
            move_body(terrain.map(tick + j), c().body_r, p.x, p.y, v.x, v.y);
        }
        return p;
    }

    V intercept(V origin, int model, int k) const {
        i64 moved = 0;
        i64 left = c().range;
        double best = 1e9;
        V aim;
        const auto& f = outlook[model];
        for (int j = int(c().throw_pre); j < Horizon && left > 0; ++j) {
            if (!f[j].ok[k]) continue;
            const i64 step = std::min(left, std::max(c().dart_vmin, c().dart_v - c().dart_dec * (j - c().throw_pre)));
            V p = f[j].p[k] - origin;
            const double d = p.len();
            if (d <= moved + step) {
                const double mix = std::clamp((d - moved) / step, 0.0, 1.0);
                return (p + (f[j + 1].p[k] - f[j].p[k]).scaled(mix)).unit();
            }
            if (d - moved - step < best) {
                best = d - moved - step;
                aim = (f[j + 1].p[k] - origin).unit();
            }
            moved += step;
            left -= step;
        }
        return best < 1300 ? aim : V{};
    }

    double preThrow(V origin, V dir) const {
        double hits[Models][MAXP]{};
        V p = origin;
        i64 left = c().range;
        double pathLoss = 0;
        for (int j = int(c().throw_pre); j < Horizon && left > 0; ++j) {
            i64 step = std::min(left, std::max(c().dart_vmin, c().dart_v - c().dart_dec * (j - c().throw_pre)));
            left -= step;
            V b{p.x + rdiv(dir.x * step, 1000), p.y + rdiv(dir.y * step, 1000)};
            bool wall = seg_wall(terrain.map(tick + j), p.x, p.y, b.x, b.y, b.x, b.y);
            for (int m = 0; m < Models; ++m) for (int k = 0; k < n; ++k) {
                if (k == me || !outlook[m][j].ok[k]) continue;
                const V s = p - outlook[m][j].p[k], e = b - outlook[m][j + 1].p[k];
                if (s.sq() > 3400 * 3400 && e.sq() > 3400 * 3400) continue;
                const double d = segmentDistance2(s, e);
                const double val = std::clamp((sqHit(1.37) - d) / (sqHit(1.37) - sqHit(.54)), 0.0, 1.0);
                const bool canMove = outlook[m][j].st[k] == S_FREE;
                const double reaction = canMove ? std::clamp((j - c().delay - 1) * .038, 0.0, .30) : 0.0;
                hits[m][k] = std::max(hits[m][k], val * (1.0 - .012 * j - reaction));
            }
            p = b;
            if (wall) {
                pathLoss = j < 7 ? .10 : .04;
                break;
            }
        }
        double score = -pathLoss;
        for (int m = 0; m < Models; ++m) for (int k = 0; k < n; ++k) {
            const double value = !foe(k) ? -3.5 : obs->p[k].vis ? 1.0 : .62;
            score += belief[k][m] * hits[m][k] * value;
        }
        return score;
    }

    struct Local {
        V land, after;
        V v;
        bool ok = true;
    };

    Local dashProbe(V aim, int switchAt) const {
        Local r;
        V x = here;
        V v = vel(obs->me());
        for (int j = 0; j < c().dash_pre + c().dash_t + c().dash_post; ++j) {
            const bool switched = switchAt >= 0 && j >= switchAt;
            const bool move = !switched && j >= c().dash_pre && j < c().dash_pre + c().dash_t;
            if (move) {
                v = aim.scaled(c().dash_v / 1000.0);
            } else {
                V dec = -v;
                cap_len(dec.x, dec.y, c().acc);
                v = v + dec;
            }
            move_body(terrain.map(tick + j), c().body_r, x.x, x.y, v.x, v.y);
            if (!move || j == c().dash_pre + c().dash_t - 1) {
                if (!nav.on(x, tick + j)) { r.ok = false; return r; }
            }
            if (move && j == c().dash_pre + c().dash_t - 1)
                v = v.scaled(c().run_v / double(c().dash_v));
            if (switchAt >= 0 && j == switchAt + (armed ? c().slash_pre : c().kick_pre) - 1) r.after = x;
        }
        r.land = x;
        if (switchAt < 0) r.after = x;
        r.v = v;
        return r;
    }

    V meleeOrigin() const {
        const auto& p = obs->me();
        V x = here;
        V v = vel(p);
        for (int j = 0; j < (armed ? c().slash_pre : c().kick_pre); ++j) {
            V dec = -v;
            cap_len(dec.x, dec.y, c().acc);
            v = v + dec;
            move_body(terrain.map(tick + j), c().body_r, x.x, x.y, v.x, v.y);
        }
        return x;
    }

    double meleePre(V x, int j, V aim, int activeModel = -1) const {
        const double reach = armed ? c().slash_reach : c().kick_reach;
        const double cs = (armed ? c().slash_cos : c().kick_cos) * 1e-6;
        const int r = std::min(j, Horizon);
        double score = 0;
        for (int m = 0; m < Models; ++m) {
            if (activeModel >= 0 && m != activeModel) continue;
            const auto& s = outlook[m][r];
            const auto& prev = outlook[m][std::max(0, r - 1)];
            double a = 0;
            for (int k = 0; k < n; ++k) {
                if (k == me) continue;
                if (s.ok[k] && (armed || (prev.st[k] != S_STUN && !(prev.st[k] == S_FREE && prev.timer[k])))) {
                    V d = s.p[k] - x;
                    const double dd = d.sq();
                    if (dd <= (reach + 210) * (reach + 210) &&
                        (dd < c().body_r * c().body_r || d.dot(aim) >= cs * 1000 * std::sqrt(dd)) &&
                        clear(x, s.p[k], tick + r - 1)) {
                        double f = std::clamp((reach + 180 - std::sqrt(dd)) / 440.0, .15, 1.0);
                        if (foe(k)) a += f;
                        else a -= 2.6 * f;
                    }
                }
                if (armed && c().block && dart_flying(prev.d[k].st)) {
                    const V p = pos(prev.d[k]);
                    V d = p - x;
                    if (d.sq() <= reach * reach && d.dot(aim) >= c().block_cos * d.len() &&
                        clear(x, p, tick + r - 1))
                        a += .63;
                }
            }
            score += (primary >= 0 ? belief[primary][m] : .25) * a;
        }
        return score;
    }

    double positionValue(V x, const Sample& s, i64 end) const {
        const auto& mf = safeRoute ? nav.safe : nav.target;
        const int d0 = safeRoute ? bestSafe : bestTarget;
        const int d1 = nav.value(x, mf);
        const int hs = nav.value(x, nav.safe);
        double v = 0;
        if (d0 < Inf && d1 < Inf) v += std::clamp((d0 - d1) * .0052, -19.0, 19.0) * objectiveBias;
        if (primary >= 0 && !pickup && localFight && !safeRoute && s.ok[primary]) {
            const auto& u = outlook[0][0];
            const bool oppArmed = s.d[primary].st == D_HAND;
            const double before = (u.p[primary] - here).len();
            const double after = (s.p[primary] - x).len();
            double range = armed ? (oppArmed ? desirableRange(primary) : 1060) : (oppArmed ? 5050 : 3050);
            v += std::clamp((std::abs(before - range) - std::abs(after - range)) * .006, -17.5, 14.0);
            if (armed && !oppArmed) v += std::clamp((before - after) * .004, -8.0, 9.0);
            if (oppArmed && after < c().slash_reach + (armed ? 700 : 2250) && clear(x, s.p[primary], end)) {
                const double range = c().slash_reach + (armed ? 700 : 2250);
                v -= (armed ? 16.0 : 40.0) * std::max(0.0, 1.0 - after / range);
            }
        }
        if (!safeRoute && bestSafe < Inf && hs < Inf && tick + 80 >= c().col_start)
            v += std::clamp((bestSafe - hs) * .0025, -10.0, 10.0);

        int cell = nav.id(x);
        if (cell >= 0) {
            int f = nav.nodes[cell].fall;
            if (f >= 0) v -= std::clamp((int(end) + 27 - f) * .78, 0.0, 29.0);
            const int leave = nav.nodes[cell].latest;
            if (leave >= 0 && leave < end + 24 && bestSafe > 1000)
                v -= std::clamp((end + 24 - leave) * .62, 0.0, 18.0);
            if (nav.nodes[cell].grass && armed && obs->me().expose <= end - tick &&
                (!localFight || dangerRange > c().see_r + 430)) {
                double stealth = 4.8;
                if (primary >= 0 && s.ok[primary] && clear(x, s.p[primary], end) &&
                    (s.p[primary] - x).sq() <= 5300 * 5300)
                    stealth += 2.1;
                v += numFoes > 1 ? 1.20 * stealth : stealth;
            }
        }
        const double edge = nav.clearance(x);
        v -= (localFight || urgent ? .004 : .0024) * std::max(0.0, 1150.0 - edge);
        int nearArmed = 0;
        for (int k = 0; k < n; ++k) {
            if (k == me || !s.ok[k]) continue;
            double d = (s.p[k] - x).len();
            if (!foe(k)) {
                if (d < 2350) v -= 8.2 * (1.0 - d / 2350);
                continue;
            }
            if (numFoes > 1 && d < 4800 && s.d[k].st == D_HAND) ++nearArmed;
        }
        if (nearArmed > 1) v -= 4.2 * (nearArmed - 1);
        return v;
    }

    V counterAim(const Game& g, int k, int j, const std::array<V, Horizon + 1>& path, V enemyWalk) const {
        const i64 viewed = std::max<i64>(0, g.t - c().delay);
        const Frame* f = frame(viewed);
        const int i = std::max(0, j - int(c().delay));
        V x = f ? pos(f->p) : path[i], v = f ? vel(f->p) : i > 0 ? path[i] - path[i - 1] : vel(obs->me());
        const Player& phase = f ? f->p : g.p[me];
        if (f && f->p.expose == 0 && !dart_flying(f->d.st) && (pos(g.p[k]) - x).sq() > c().see_r * c().see_r &&
            terrain.map(viewed).at(cell_index(x.x), cell_index(x.y)) == C_GRASS) return {};
        const V origin = pos(g.p[k]) + enemyWalk.scaled(c().throw_pre * c().run_v * c().throw_move / 100000.0);
        double travel = 0;
        for (int t = 0; t < g.t - viewed + 10; ++t) {
            V movement = v;
            if (phase.st == S_DASH && f) {
                const i64 timer = phase.timer - t;
                if (timer > c().dash_post && timer <= c().dash_t + c().dash_post)
                    movement = V{phase.ax, phase.ay}.scaled(c().dash_v / 1000.0);
                else movement = {};
                if (timer <= 0) movement = V{phase.ax, phase.ay}.scaled(c().run_v * .58 / 1000.0);
            } else if (phase.st == S_STUN && f) {
                if (t < phase.timer) v = v.scaled(c().kick_keep / 1000.0);
                else movement = {};
            } else if (phase.st == S_RECALL || phase.st == S_SLASH || phase.st == S_KICK ||
                       phase.st == S_SLASHED || phase.st == S_KICKED) {
                if (t < phase.timer) movement = {};
            } else if (!f && (g.p[me].st == S_DASH || g.p[me].st == S_STUN) && t > 5) {
                movement = v.scaled(.65);
            }
            V next = x;
            move_body(terrain.map(viewed + t), c().body_r, next.x, next.y, movement.x, movement.y);
            if (nav.on(next, viewed + t) || phase.st == S_DASH || phase.st == S_STUN) x = next;
            const int age = t - int(g.t - viewed + c().throw_pre);
            if (age < 0) continue;
            travel = std::min<double>(c().range, travel + std::max(c().dart_vmin, c().dart_v - age * c().dart_dec));
            if ((x - origin).len() <= travel + c().dart_r + c().body_r - 240) {
                if (!clear(origin, x, viewed + t) || (x - pos(g.p[me])).sq() > 3300 * 3300) break;
                return (x - origin).unit();
            }
        }
        return {};
    }

    Action reactiveEnemy(const Game& g, int k, int model, int j, Action a,
                         const std::array<V, Horizon + 1>& path) const {
        const Player& p = g.p[k];
        if ((model == 1 || model == 3) && foe(k) && p.st == S_DASH && g.dash_moving(p)) {
            Action m = movingMelee(g, k);
            if (m.act) return m;
        }
        if (!foe(k) || !p.alive || p.unknown || p.st != S_FREE || !g.p[me].alive) return a;
        const V x = pos(p), toUs = pos(g.p[me]) - x;
        const double dist = toUs.len();
        const bool hasBlade = g.d[k].st == D_HAND;
        if (model == 1 && dist < (hasBlade ? c().slash_reach : c().kick_reach) + 80 &&
            (hasBlade || g.p[me].st != S_STUN) && clear(x, pos(g.p[me]), g.t)) {
            return action({}, A_MELEE, toUs + vel(g.p[me]).scaled(1.2));
        }
        const bool hasOpening = g.p[me].st != S_FREE ||
            (g.d[me].st != D_HAND && (n == 4 || dist < 4350)) ||
            (n == 4 && track[k].speed > .77);
        if (model == 2 && n <= 4 && hasBlade && (j <= 2 || j == 5) &&
            dist > 1740 && dist < (g.d[me].st == D_HAND ? 4320 : 5500) &&
            hasOpening &&
            clear(x, pos(g.p[me]), g.t)) {
            const V go{a.mx, a.my};
            const V aim = counterAim(g, k, j, path, go);
            if (aim.sq()) return action(go, A_THROW, aim);
        }
        if (model >= 1 && g.d[me].st == D_OUT && g.d[me].age + c().throw_pre >= c().delay + 1 && dist < 10000) {
            const Dart& d = g.d[me];
            const V posD = pos(d);
            const V ray{d.vx, d.vy};
            const V delta = x - posD;
            const double along = delta.dot(ray) / 1000;
            const double side = delta.dot(ray.left()) / 1000;
            if (along > -250 && along < d.left + 900 && std::abs(side) < 1530 && clear(posD, x, g.t)) {
                const V out = ray.left().scaled(side > 0 ? 1 : -1);
                const double speed = std::min(d.left, dart_step_len(c(), d));
                const V next = posD + ray.scaled(speed / 1000);
                Dart z = d;
                ++z.age; z.left -= int(speed);
                const V after = next + ray.scaled(std::min(z.left, dart_step_len(c(), z)) / 1000.0);
                if (model != 2 && hasBlade && c().block && (next - x).sq() <= c().slash_reach * c().slash_reach &&
                    segmentDistance2(posD - x, next - x) > sqHit(1.02) &&
                    segmentDistance2(next - x, after - x) < sqHit(1.12))
                    return action({}, A_MELEE, next - x);
                if (nav.on(x + out.scaled(.35), g.t))
                    return action(out);
                if (model >= 2 && nav.on(x - out.scaled(.35), g.t))
                    return action(-out);
            }
        }
        return a;
    }

    Action followPlan(const Game& g, const Plan& plan, int j) const {
        if (!j) return plan.a;
        if (j == plan.switchAt && g.dash_moving(g.p[me]) &&
            plan.foe >= 0 && g.p[plan.foe].alive && !g.p[plan.foe].unknown) {
            V to = pos(g.p[plan.foe]) + vel(g.p[plan.foe]).scaled(1.1) -
                pos(g.p[me]) - vel(g.p[me]).scaled(.68);
            return action({}, A_MELEE, to);
        }
        if (g.p[me].st == S_RECALL) return {};
        const V move{plan.a.mx, plan.a.my};
        V go = j <= 2 ? move : continuation(g.p[me], move, g.t);
        if (plan.a.act == A_DASH && g.p[me].st == S_FREE) go = continuation(g.p[me], wish, g.t);
        return action(go);
    }

    double hazardRisk(const Plan& plan) const {
        if (hazards.empty()) return 0;
        double worst = 0, total = 0;
        for (const Hazard& h : hazards) {
            Game g = h.world;
            int k = h.trigger.who;
            double loss = 0;
            const int duration = h.trigger.kind == A_DASH ? 11 : 9;
            for (int j = 0; j < duration; ++j) {
                Action a[MAXP]{};
                fillActions(g, 0, j, a);
                a[me] = followPlan(g, plan, j);
                a[k] = triggerAction(g, h.trigger, a[k]);
                const Dart d0 = g.d[k];
                const V start = pos(g.p[me]);
                const int blocks = int(g.p[me].n_block);
                terrain.step(g, a);
                if (!g.p[me].alive) {
                    const bool trade = !g.p[k].alive && g.p[k].dead_t == g.p[me].dead_t;
                    const double hit = trade ? 37 : 122;
                    const double commit = plan.a.act == A_DASH || plan.a.act == A_MELEE ? 1.12 : 1.0;
                    loss = hit * (1 - .041 * j) * commit;
                    break;
                }
                if (g.over) break;
                if (g.p[me].st == S_STUN && obs->me().st != S_STUN) {
                    const double sting = dart_flying(g.d[k].st) ? 50 : 36;
                    loss = std::max(loss, sting * (1 - .041 * j));
                }
                if (dart_flying(d0.st) && g.p[me].n_block == blocks) {
                    V r0 = pos(d0) - start;
                    V r1 = pos(g.d[k]) - pos(g.p[me]);
                    if (r0.sq() < 2200 * 2200 || r1.sq() < 2200 * 2200) {
                        const double md = segmentDistance2(r0, r1);
                        const double margin = std::clamp((sqHit(1.73) - md) / (sqHit(1.73) - sqHit()), 0.0, 1.0);
                        loss = std::max(loss, 25.0 * margin * (1 - .055 * j));
                    }
                }
            }
            loss *= h.trigger.weight;
            worst = std::max(worst, loss);
            total += loss;
        }
        return .56 * worst + .44 * total / hazards.size();
    }

    double evaluate(const Plan& plan, int model, double* riskOut = nullptr) const {
        const int kind = int(plan.a.act);
        int h = kind == A_THROW || kind == A_RECALL ? Horizon : kind == A_DASH ? 11 : NearH;
        if (kind == A_MELEE && !armed) h = Horizon;
        if (kind == A_DASH && plan.switchAt >= 0 && !armed) h = Horizon;
        if (n > 4 && kind != A_THROW && h > 11) h = 11;
        if (c().mode == M_TEAM && kind == A_NONE && dart_flying(obs->my_dart().st)) h = 12;
        Game g = roots[model];
        const V move{plan.a.mx, plan.a.my};
        std::array<V, Horizon + 1> path{};
        path[0] = here;
        const int liveAtStart = g.alive_count();
        int end = 0;
        int have = g.d[me].st;
        int tookHold = -1;
        int oldBlocks = int(g.p[me].n_block);
        int oldKicks = int(g.p[me].kick_hits);
        int oldTeamKicks = int(g.p[me].kick_hits_team);
        double nearMiss[MAXP]{};
        double friendlyMiss[MAXP]{};
        const double hit = c().body_r + c().dart_r;
        bool wasAlive[MAXP]{};
        for (int k = 0; k < n; ++k) wasAlive[k] = g.p[k].alive && !g.p[k].unknown;
        for (int j = 0; j < h; ++j) {
            Action a[MAXP]{};
            fillActions(g, model, j, a);
            if (model) for (int k = 0; k < n; ++k)
                if (k != me) a[k] = reactiveEnemy(g, k, model, j, a[k], path);
            a[me] = followPlan(g, plan, j);
            std::array<Dart, MAXP> darts;
            for (int k = 0; k < n; ++k) if (k != me) darts[k] = g.d[k];
            const Dart mine = g.d[me];
            const int blocks = int(g.p[me].n_block);
            terrain.step(g, a);
            if (tookHold < 0 && (have == D_GROUND || have == D_RISE) && g.d[me].st == D_HAND)
                tookHold = j;
            path[j + 1] = pos(g.p[me]);
            if (c().mode == M_TEAM && (dart_flying(mine.st) || (mine.st == D_RISE && g.d[me].st == D_CALL))) {
                const V a = pos(mine), b = pos(g.d[me]);
                for (int k = 0; k < n; ++k) {
                    if (k == me || foe(k) || !wasAlive[k] || (!g.p[k].alive && g.p[k].dead_t < tick + j)) continue;
                    V p0{g.p[k].px, g.p[k].py};
                    V r0 = a - p0, r1 = b - pos(g.p[k]);
                    if (r0.sq() > 2300 * 2300 && r1.sq() > 2300 * 2300) continue;
                    const double near = segmentDistance2(r0, r1);
                    const double cost = std::clamp((sqHit(1.68) - near) / (sqHit(1.68) - sqHit()), 0.0, 1.25);
                    friendlyMiss[k] = std::max(friendlyMiss[k], 32 * cost * (1 - .021 * j));
                }
            }
            for (int k = 0; k < n; ++k) {
                if (k == me || !dart_flying(darts[k].st)) continue;
                V p0 = pos(darts[k]), p1 = pos(g.d[k]);
                if (g.p[me].n_block > blocks && g.d[k].st == D_GROUND && p0 == p1) continue;
                V r0 = p0 - path[j], r1 = p1 - path[j + 1];
                if (r0.sq() > 2500 * 2500 && r1.sq() > 2500 * 2500) continue;
                const double x = std::sqrt(segmentDistance2(r0, r1));
                const bool bending = darts[k].st != D_OUT;
                const double margin = bending ? 735 : 525;
                const double loss = std::clamp((hit + margin - x) / margin, 0.0, 1.35);
                nearMiss[k] = std::max(nearMiss[k], loss * (bending ? 33.5 : 29.0) * (1 - .041 * j));
            }
            end = j + 1;
            if (g.over && liveAtStart > 1) break;
            if (!g.p[me].alive && (c().mode != M_DUEL || g.p[me].dead_t + 1 < g.t)) break;
        }

        double v = positionValue(path[std::min(end, 7)], outlook[model][std::min(end, 7)], tick + std::min(end, 7));
        for (int k = 0; k < n; ++k) v -= nearMiss[k];
        for (int k = 0; k < n; ++k) v -= friendlyMiss[k];
        if (kind == A_NONE && move.sq()) {
            v += 2.4 * wish.dot(move.unit()) / 1e6;
            if (lastMove.sq()) v += .82 * lastMove.dot(move.unit()) / 1e6;
        }
        if (kind == A_DASH) {
            v -= urgent ? 3.2 : 8.4;
            if (!armed && !urgent && numFoes > 0 && primary >= 0 && g.p[primary].alive &&
                (g.d[primary].st == D_HAND || g.d[primary].st == D_OUT) && g.d[me].st != D_HAND)
                v -= 13.0;
        }
        if (kind == A_THROW) {
            v -= dangerRange < 3900 ? 15.5 : 17.0;
            if (primary >= 0 && roots[model].p[primary].st == S_FREE &&
                roots[model].d[primary].st == D_HAND)
                v -= std::clamp((dangerRange - 3900.0) * .023, 0.0, 72.0);
        }
        if (kind == A_MELEE) v -= armed ? 9.5 : 9.0;
        if (kind == A_MELEE && numFoes <= 1 && have == D_GROUND &&
            bestTarget < 2350 && dangerRange < 3900) {
            double seize = std::clamp((2350 - bestTarget) * .004, 0.0, 9.4);
            v -= 5.5 + seize * (1.20 - std::clamp(plan.quick, 0.0, 1.0) * .72);
            if (g.p[me].kick_hits == oldKicks) v -= 4;
        }
        if (kind == A_MELEE && obs->me().st == S_DASH && !armed) v -= 2;
        if (have == D_HAND && g.d[me].st == D_GROUND) {
            v -= 18.0;
            if (primary >= 0 && g.p[primary].alive && g.d[primary].st == D_HAND) v -= 12;
        }
        if (g.p[me].n_block > oldBlocks) v += 20.2 + 4 * std::min<i64>(2, g.p[me].n_block - oldBlocks - 1);
        if (g.p[me].kick_hits > oldKicks) v += 12 * (g.p[me].kick_hits - oldKicks);
        if (g.p[me].kick_hits_team > oldTeamKicks) v -= 48 * (g.p[me].kick_hits_team - oldTeamKicks);
        if (tookHold >= 0 && g.d[me].st == D_HAND) {
            v += 24;
            if (numFoes <= 1)
                v += std::max(0, 7 - tookHold) * (kind == A_RECALL || have == D_RISE ? .80 : 2.25);
        }
        if (have == D_GROUND && g.d[me].st == D_CALL) v += 11;
        if (have == D_GROUND && kind == A_RECALL && dangerRange < 4700)
            v -= 15 + std::max(0.0, (4400 - dangerRange) * .011);
        if (have == D_RISE && g.d[me].st == D_GROUND) v -= 2.8;
        if (dart_flying(have) && g.d[me].st == D_HAND) v += 9;
        if (dart_flying(have) && g.d[me].st == D_GROUND) v -= 5;
        if (g.p[me].st == S_STUN && obs->me().st != S_STUN) v -= 18;

        int opposingDead = 0;
        for (int k = 0; k < n; ++k) {
            if (k == me || !wasAlive[k] || g.p[k].alive) continue;
            const auto& ep = g.p[k];
            if (!foe(k)) {
                v -= ep.killer == me ? 285 : 15;
                continue;
            }
            ++opposingDead;
            if (ep.killer == me) {
                const double scale = kind == A_THROW ? .067 : .029;
                const double immediate = 1.0 - std::clamp<double>((ep.dead_t - tick - c().delay) * scale, 0.0, .53);
                const bool uncertain = !obs->p[k].vis;
                v += (numFoes <= 1 ? 177 : 101) * immediate * (uncertain ? .62 : 1.0);
            } else if (c().mode == M_FFA) v += 21;
            else if (c().mode == M_DUEL && g.p[me].alive && !urgent) v += 36;
        }
        if (!g.p[me].alive) {
            const auto& p = g.p[me];
            if (c().mode == M_DUEL && opposingDead > 0) return -46;
            double penalty = c().mode == M_FFA && numFoes > 1 ? 227 : 276;
            if (p.cause == K_FALL && p.killer < 0) penalty = 360;
            v -= penalty * (1.0 - std::clamp<double>((p.dead_t - tick - 4) * .035, 0.0, .40));
        }
        if (riskOut) {
            *riskOut = latentRisk(path, end, g, plan);
            v -= *riskOut;
        }
        return v;
    }

    void addPlan(std::vector<Plan>& set, Action a, double quick = 0, int when = -1, int foe = -1) const {
        set.push_back({a, when, foe, quick, 0, -1e9});
    }

    void flightHeadings(std::vector<V>& v) const {
        if (!tactical || n > 4) return;
        struct Evasion {
            V perpendicular;
            double quality = 0.0;
        };
        Evasion first, second;
        for (int k = 0; k < n; ++k) {
            if (k == me) continue;
            Evasion best;
            for (int j = 0; j < 6; ++j) {
                const auto& a = outlook[0][j].d[k];
                const auto& d = outlook[0][j + 1].d[k];
                if (!dart_flying(a.st) || !dart_flying(d.st)) continue;
                V start = pos(a), end = pos(d), ray = end - start;
                if (ray.sq() < 55 * 55) continue;
                const double radius = c().body_r + c().dart_r + j * c().run_v * .64 + 270;
                const double dd = segmentDistance2(start - here, end - here);
                if (dd > radius * radius) continue;
                V away = ray.unit().left();
                if ((here - start).dot(away) < 0) away = -away;
                const double value = (1.0 - std::sqrt(dd) / radius) * (1.0 - .085 * j);
                if (value > best.quality) best = {away, value};
            }
            if (best.quality > first.quality) {
                second = first; first = best;
            } else if (best.quality > second.quality) {
                second = best;
            }
        }
        if (first.quality > .04) {
            const V w = first.perpendicular;
            v.insert(v.end(), {w, -w, w.rotated(.31), w.rotated(-.31)});
        }
        if (second.quality > .10) {
            const V w = second.perpendicular;
            v.insert(v.end(), {w, -w, (w + first.perpendicular).unit()});
        }
        const Dart& item = obs->my_dart();
        if ((item.st == D_BACK || item.st == D_CALL) && item.age > c().dart_hang &&
            (pos(item) - here).sq() < 4000 * 4000 && dangerRange > 2350) {
            v.push_back((pos(item) - here).unit());
            if (primary >= 0 && (outlook[0][4].p[primary] - here).sq() < 5100 * 5100) {
                const V e = outlook[0][4].p[primary];
                v.push_back((e + (e - pos(item)).unit().scaled(1.18) - here).unit());
            }
        }
    }

    std::vector<V> moves() const {
        std::vector<V> v{wish, V{}, vel(obs->me()).unit()};
        const int stride = tactical && n <= 4 ? 1 : 2;
        for (int i = 0; i < int(ring.size()); i += stride) v.push_back(ring[i]);
        flightHeadings(v);
        if (pickup && numFoes <= 1 && !urgent && (objective - here).sq() < 3150 * 3150) {
            const V to = (objective - here).unit();
            v.insert(v.end(), {to, to.scaled(.52), to.rotated(.38), to.rotated(-.38)});
        }
        if (wish.sq()) {
            v.push_back(wish.scaled(.5));
            v.push_back(wish.rotated(handSide * Pi / 6));
            v.push_back(wish.rotated(-handSide * Pi / 6));
        }
        std::vector<V> unique;
        for (V a : v) {
            bool duplicate = false;
            for (V b : unique) if ((a - b).sq() < 57 * 57) { duplicate = true; break; }
            if (!duplicate) unique.push_back(a);
        }
        return unique;
    }

    void generateThrow(std::vector<Plan>& plans) const {
        if (!armed || dangerRange > 7950 || urgent || c().throw_pre >= Horizon - 1) return;
        std::vector<V> ms{wish, vel(obs->me()).unit(), {}};
        if (primary >= 0) {
            V rel = (outlook[0][0].p[primary] - here).unit();
            ms.push_back(rel.left().scaled(handSide));
            ms.push_back(-rel.left().scaled(handSide));
        }
        std::vector<Plan> shots;
        for (size_t i = 0; i < ms.size(); ++i) {
            V move = ms[i];
            bool duplicate = false;
            for (size_t j = 0; j < i; ++j) if ((move - ms[j]).sq() < 110 * 110) duplicate = true;
            if (duplicate) continue;
            const V origin = ownThrowOrigin(move);
            if (!nav.body(origin, tick + c().throw_pre - 1)) continue;
            std::vector<V> angles;
            auto admit = [&](V dir) {
                if (!dir.sq()) return;
                for (V q : angles) if ((q - dir).sq() < 33 * 33) return;
                angles.push_back(dir);
            };
            for (int k = 0; k < n; ++k) {
                if (!foe(k) || !outlook[0][0].ok[k] || (outlook[0][0].p[k] - here).sq() > 8500 * 8500) continue;
                V center;
                V front[Models];
                for (int m = 0; m < Models; ++m) {
                    const V a = intercept(origin, m, k);
                    front[m] = a;
                    center = center + a.scaled(belief[k][m]);
                    admit(a);
                    if (m == 0 || m == 1) {
                        admit(a.rotated(.105));
                        admit(a.rotated(-.105));
                    }
                }
                admit(center.unit());
                if (n <= 4) {
                    admit((front[0] + front[2]).unit());
                    admit((front[0] + front[3]).unit());
                }
            }
            for (V dir : angles) {
                double val = preThrow(origin, dir);
                if (val > .15) addPlan(shots, action(move, A_THROW, dir), val);
            }
        }
        std::stable_sort(shots.begin(), shots.end(), [](const Plan& a, const Plan& b) { return a.quick > b.quick; });
        int count = 0;
        for (const Plan& p : shots) {
            bool near = false;
            for (const Plan& q : plans) {
                if (q.a.act != A_THROW) continue;
                const V da{p.a.ax - q.a.ax, p.a.ay - q.a.ay};
                const V dm{p.a.mx - q.a.mx, p.a.my - q.a.my};
                if (da.sq() < 44 * 44 && dm.sq() < 530 * 530) near = true;
            }
            if (near) continue;
            addPlan(plans, p.a, p.quick);
            if (++count >= (n > 4 ? 5 : 9)) break;
        }
    }

    void generateMelee(std::vector<Plan>& plans) const {
        if ((armed ? c().slash_pre : c().kick_pre) >= Horizon) return;
        V x = meleeOrigin();
        int step = int(std::min<i64>(Horizon, armed ? c().slash_pre : c().kick_pre));
        bool relevant = false;
        std::vector<V> dirs;
        auto admit = [&](V dir) {
            if (!dir.sq()) return;
            for (V q : dirs) if ((q - dir).sq() < 140 * 140) return;
            dirs.push_back(dir);
        };
        const double reach = armed ? c().slash_reach : c().kick_reach;
        for (int m = 0; m < Models; ++m) for (int k = 0; k < n; ++k) {
            if (k == me) continue;
            const auto& f = outlook[m];
            V d = f[step].p[k] - x;
            if (foe(k) && f[step].ok[k] && d.sq() < (reach + 310) * (reach + 310)) {
                admit(d.unit());
                relevant = true;
            }
            const auto& old = f[std::max(0, step - 1)].d[k];
            if (armed && c().block && dart_flying(old.st)) {
                V delta = pos(old) - x;
                if (delta.sq() < (reach + 180) * (reach + 180)) {
                    admit(delta.unit());
                    relevant = true;
                }
            }
        }
        if (!relevant) return;
        const size_t z = dirs.size();
        for (size_t i = 0; i < z; ++i) for (size_t j = i + 1; j < z; ++j) {
            if (dirs[i].dot(dirs[j]) > -.25e6) admit((dirs[i] + dirs[j]).unit());
            if (dirs.size() >= 20) break;
        }
        std::vector<Plan> ps;
        for (V dir : dirs) {
            double quality = meleePre(x, step, dir);
            if (quality > .07) addPlan(ps, action(wish, A_MELEE, dir), quality);
        }
        std::stable_sort(ps.begin(), ps.end(), [](const Plan& a, const Plan& b) { return a.quick > b.quick; });
        for (int i = 0; i < std::min<int>(7, int(ps.size())); ++i) addPlan(plans, ps[i].a, ps[i].quick);
    }

    void generateDash(std::vector<Plan>& plans) const {
        if (obs->me().cd != 0 || c().dash_v == 0 || c().dash_pre + c().dash_t + c().dash_post >= Horizon) return;
        std::vector<V> dirs{wish.unit()};
        for (size_t i = 0; i < ring.size(); i += n > 4 ? 3 : 2) dirs.push_back(ring[i]);
        if (!localFight || urgent || pickup) {
            for (V u : nav.jumpHeadings(here, safeRoute)) {
                bool same = false;
                for (V v : dirs) if ((u - v).sq() < 77 * 77) same = true;
                if (!same) dirs.push_back(u);
            }
        }
        const int pen = int(c().dash_pre + c().dash_t - 1);
        if (primary >= 0) {
            const V a = (outlook[0][std::min(pen, Horizon)].p[primary] - here).unit();
            dirs.push_back(a);
            dirs.push_back(a.rotated(.20));
            dirs.push_back(a.rotated(-.20));
        }
        std::vector<Plan> bridges, pounce;
        for (V u : dirs) {
            if (!u.sq()) continue;
            Local l = dashProbe(u, -1);
            if (l.ok && (l.land - here).sq() > 750 * 750) {
                const double merit = positionValue(l.land, outlook[0][std::min(pen, Horizon)], tick + pen)
                    + 3 * wish.dot(u) / 1e6;
                addPlan(bridges, action(wish, A_DASH, u), merit);
            }
            if (!c().dash_slash || !localFight || (armed ? c().slash_pre : c().kick_pre) >= Horizon) continue;
            for (int j = int(c().dash_pre + 1); j <= pen; ++j) {
                if (j >= Horizon - 3) break;
                Local stop = dashProbe(u, j);
                if (!stop.ok) continue;
                const int r = j + int(armed ? c().slash_pre : c().kick_pre);
                if (r > Horizon) continue;
                for (int k = 0; k < n; ++k) {
                    if (!foe(k) || !outlook[0][r].ok[k]) continue;
                    V d = outlook[0][r].p[k] - stop.after;
                    if (d.sq() > 1850 * 1850) continue;
                    const double merit = meleePre(stop.after, r, d.unit());
                    if (merit > .23) addPlan(pounce, action(wish, A_DASH, u), merit - .06 * (j - c().dash_pre), j, k);
                }
            }
        }
        std::stable_sort(bridges.begin(), bridges.end(), [](const Plan& a, const Plan& b) { return a.quick > b.quick; });
        std::stable_sort(pounce.begin(), pounce.end(), [](const Plan& a, const Plan& b) { return a.quick > b.quick; });
        for (int i = 0; i < std::min<int>(tactical ? 6 : 4, int(bridges.size())); ++i)
            addPlan(plans, bridges[i].a, bridges[i].quick);
        for (int i = 0; i < std::min<int>(n > 4 ? 3 : 6, int(pounce.size())); ++i)
            addPlan(plans, pounce[i].a, pounce[i].quick, pounce[i].switchAt, pounce[i].foe);
    }

    Action decide(const Obs& o) {
        if (!o.me().alive) return {};
        obs = &o;
        tick = o.t;
        terrain.simulations = 0;
        here = pos(o.me());
        armed = o.my_dart().st == D_HAND;
        const i64 delayed = std::max<i64>(0, tick - c().delay);
        updateTracks(o, delayed);
        updateBelief();
        nav.update(tick);
        const bool dashActive = c().dash_slash && o.me().st == S_DASH &&
            o.me().timer > c().dash_post && o.me().timer <= c().dash_post + c().dash_t;
        Action response;
        if (o.me().st == S_FREE || o.me().st == S_RECALL || o.me().st == S_THROW || o.me().st == S_THROWN || dashActive) {
            prepareHidden(o, delayed);
            for (int m = 0; m < Models; ++m) {
                reconstruct(o, delayed, m);
                forecast(m);
            }
            rememberForecast();
            chooseObjective();
            latentShots(delayed);
            chooseHazards(delayed);
            std::vector<Plan> plans;
            plans.reserve(64);
            if (!dashActive) {
                for (V move : moves()) addPlan(plans, action(move));
            } else {
                addPlan(plans, action({}));
            }
            if (o.me().st == S_FREE) {
                generateDash(plans);
                generateThrow(plans);
                if (c().recall && o.my_dart().st == D_GROUND && bestTarget > 2200 && !urgent &&
                    (dangerRange > 4200 || (dangerRange > 3100 && roots[0].d[primary].st != D_HAND)))
                    addPlan(plans, action({}, A_RECALL));
            }
            if (o.me().st == S_FREE || o.me().st == S_RECALL || dashActive) generateMelee(plans);

            // A cheap ranking keeps full multi-model rollouts for a short list.
            for (Plan& p : plans) {
                p.seed = evaluate(p, 0, &p.risk);
                if (p.a.act == A_THROW) p.seed += 14.0 * p.quick;
            }
            std::stable_sort(plans.begin(), plans.end(), [](const Plan& a, const Plan& b) {
                auto merit = [](const Plan& p) { return p.seed + (p.a.act == A_THROW ? 29 * p.quick : 0.0); };
                return merit(a) > merit(b);
            });
            double value = -1e10;
            int used = 0;
            int counts[6]{};
            for (const Plan& p : plans) {
                const int group = p.switchAt >= 0 ? 5 : int(p.a.act);
                if (++counts[group] > (group == A_NONE ? 5 : 4)) continue;
                if (used >= (n > 4 ? 6 : 13)) break;
                // Keep the amount of work independent of clock granularity and load.
                if (used >= 5 && terrain.simulations >= (n > 4 ? 970 : n == 4 ? 1650 : 2010)) break;
                if (p.seed < plans.front().seed - (tactical ? 260 : 24)) continue;
                double score = p.seed;
                if (tactical || pickup || dashActive) {
                    double v1 = evaluate(p, 1), v2 = evaluate(p, 2), v3 = evaluate(p, 3);
                    if (p.a.act == A_THROW) {
                        const double q = 14.0 * p.quick;
                        v1 += q; v2 += q; v3 += q;
                    }
                    const std::array<double, Models> weights = primary >= 0 ? belief[primary] :
                        std::array<double, Models>{.39, .23, .19, .19};
                    score = weights[0] * p.seed + weights[1] * v1 + weights[2] * v2 + weights[3] * v3 -
                        (1.0 - weights[0]) * p.risk;
                    const double lo = std::min({p.seed, v1 - p.risk, v2 - p.risk, v3 - p.risk});
                    if (lo < -50) score += .18 * (lo - score);
                }
                score -= hazardRisk(p) * (n == 2 ? .78 : .70);
                if (score > value) { value = score; response = p.a; }
                ++used;
            }
        }
        if (c().mode == M_TEAM) {
            int code = nav.id(here);
            if (code >= 0 && code < 4096 && c().msg_max >= 0xA000 + code) response.msg = 0xA000 + code;
        }
        if (response.mx || response.my) lastMove = V{response.mx, response.my}.unit();
        Frame& f = history[tick % History];
        f.at = tick;
        f.p = o.me();
        f.d = o.my_dart();
        f.a = response;
        return response;
    }
};
}

DART_MAIN(Bot)

