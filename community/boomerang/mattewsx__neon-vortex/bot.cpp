// NeonVortex —— Original C++17 AI Bot for Boomerang AI Arena (dartfu-v3)
// Architecture:
//   1. Multi-phase collapse & retreat map intelligence (walk + gap-dash graph, latest departure deadlines,
//      isolated-island trap avoidance, early narrow-core claiming)
//   2. Delay-compensated state tracker (un-delayed body-push contact & own-dart parry/clash inference,
//      half-cell grass fog-of-war ambush & rush tracking, 10-model Bayesian motion ensemble incl. pattern
//      history & dash-rush, recall-cancel awareness, plus 6-style online aim learner incl. modeledAim)
//   3. Complete threat geometry (exact zero-error D_OUT flight tracking, multi-model returning flights,
//      modeledAim phantom throws, counter-dash/anti-kick & post-melee lock vulnerability evaluator,
//      and FFA crossfire-lane avoidance)
//   4. Adaptive weapon economy (strict 1v1 anti-parry & post-block press discipline + unreactable/locked
//      punishes + crisp FFA vulture kill-conversion), safe step-in parry-theft & recall-camping,
//      anti-camp recall escape, return-curve anti-parry & curve-kills, pickup-disk racing
//   5. Two-stage planner: deterministic 14-tick geometric MPC with 2-segment dodge-then-recover
//      trajectories + close-quarters exact dart::Game micro-exchange referee verifier (Bayesian delay
//      replay + 4 enemy response policies for exact kills, parries, kicks, and cliff-falls)
#include "dart.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <queue>
#include <vector>

using namespace dart;

namespace {

constexpr double NV_SNIPE_SCALE = 0.88;
constexpr double NV_PARRY_SCALE = 1.25;
constexpr double NV_THROW_BIAS_1V1 = 0.06;
constexpr double NV_THROW_BIAS_FFA = -0.05;
constexpr double NV_WALK_SLACK = 0.0;
constexpr double NV_ANGLE_KICK = 0.0;
constexpr int NV_FFA_VULTURE = 0;
constexpr int NV_FOG_RUSH = 1;
constexpr double NV_S2_WEIGHT = 0.18;
constexpr double NV_S2_FFA_WEIGHT = 0.0;
constexpr int NV_FFA_AGGRO = 1;
constexpr double NV_FFA_SNIPE = 1.35;
constexpr double NV_FFA_PARRY_COST = 0.0;
constexpr double NV_FFA_THROW_SCALE = 30.0;
constexpr double NV_FFA_DASH_SAVE = 1.8;

constexpr double NV_PI = 3.14159265358979323846;
constexpr int NV_H = 14;         // tactical MPC horizon
constexpr int NV_FUT = 20;       // enemy motion & shot forecast horizon
constexpr int NV_MODES = 10;     // 0:cont 1:stop 2:smooth 3:toward 4:away 5:left 6:right 7:pick/core 8:hist 9:dash
constexpr int NV_HIST = 160;     // ring buffer length

struct V2 {
    double x = 0.0, y = 0.0;
    V2 operator+(V2 o) const { return {x + o.x, y + o.y}; }
    V2 operator-(V2 o) const { return {x - o.x, y - o.y}; }
    V2 operator*(double k) const { return {x * k, y * k}; }
    double dot(V2 o) const { return x * o.x + y * o.y; }
    double cross(V2 o) const { return x * o.y - y * o.x; }
    double sq() const { return x * x + y * y; }
    double len() const { return std::sqrt(sq()); }
    V2 perp() const { return {-y, x}; }
    V2 unit(double s = 1.0) const {
        const double l = len();
        return l > 1e-6 ? V2{x * (s / l), y * (s / l)} : V2{0.0, 0.0};
    }
};

inline double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline i64 iround(double v) { return static_cast<i64>(std::llround(v)); }
inline V2 vpos(const Player& p) { return {double(p.x), double(p.y)}; }
inline V2 vvel(const Player& p) { return {double(p.vx), double(p.vy)}; }
inline V2 vpos(const Dart& d) { return {double(d.x), double(d.y)}; }

inline double seg_origin_sq(V2 a, V2 b) {
    const V2 d = b - a;
    const double dd = d.sq();
    if (dd <= 1e-9) return a.sq();
    const double u = clamp01(-a.dot(d) / dd);
    const V2 p = a + d * u;
    return p.sq();
}

struct Body {
    i64 x = 0, y = 0, vx = 0, vy = 0, ax = 0, ay = 0;
    int st = S_FREE, timer = 0, cd = 0;
    Body() = default;
    explicit Body(const Player& p)
        : x(p.x), y(p.y), vx(p.vx), vy(p.vy), ax(p.ax), ay(p.ay),
          st(int(p.st)), timer(int(p.timer)), cd(int(p.cd)) {}
    V2 pt() const { return {double(x), double(y)}; }
    V2 vel() const { return {double(vx), double(vy)}; }
};

struct Seg {
    V2 a{}, b{};
    bool on = false;
};

struct PhaseMap {
    Map m;
    int nx = 0, ny = 0, N = 0;
    std::vector<uint8_t> usable;
    std::vector<uint8_t> wallNear;
    std::vector<int> wallPref;
    std::vector<float> room;
    std::vector<float> coreDist;
    std::vector<float> grassScore;
    std::vector<int> latestWalk;
    std::vector<int> latestDash;
    struct Edge { int to; float cost; bool dash; };
    std::vector<std::vector<Edge>> edges;

    int clampIdx(V2 p) const {
        const int gx = std::clamp(int(cell_index(iround(p.x))), 0, nx - 1);
        const int gy = std::clamp(int(cell_index(iround(p.y))), 0, ny - 1);
        return gy * nx + gx;
    }
    bool wallsInBox(int x0, int y0, int x1, int y1) const {
        const int st = nx + 1;
        x0 = std::clamp(x0, 0, nx);
        y0 = std::clamp(y0, 0, ny);
        x1 = std::clamp(x1 + 1, 0, nx);
        y1 = std::clamp(y1 + 1, 0, ny);
        if (x1 <= x0 || y1 <= y0) return false;
        return (wallPref[y1 * st + x1] - wallPref[y1 * st + x0] -
                wallPref[y0 * st + x1] + wallPref[y0 * st + x0]) > 0;
    }
    double sample(const std::vector<float>& f, V2 p) const {
        const double gx = clampd(p.x / double(CELL) - 0.5, 0.0, double(nx - 1));
        const double gy = clampd(p.y / double(CELL) - 0.5, 0.0, double(ny - 1));
        const int x0 = int(gx), y0 = int(gy);
        const int x1 = std::min(x0 + 1, nx - 1), y1 = std::min(y0 + 1, ny - 1);
        const double fx = gx - x0, fy = gy - y0;
        const double v0 = (1.0 - fx) * f[y0 * nx + x0] + fx * f[y0 * nx + x1];
        const double v1 = (1.0 - fx) * f[y1 * nx + x0] + fx * f[y1 * nx + x1];
        return (1.0 - fy) * v0 + fy * v1;
    }
};

struct SelfSnap {
    Player me;
    Dart dart;
    Action act;
};

struct EnemyModel {
    std::array<Body, MAX_DELAY + 1> past{};
    std::array<Body, NV_FUT + 1> fut{};
    std::array<bool, NV_FUT + 1> hand{};
    double w = 1.0 / NV_MODES;
};

struct FogState {
    int entry = -10000;
    int phase = -1;
    int lastTick = -10000;
    std::vector<int> cells;
    std::array<V2, 3> pts{};
    std::array<double, 3> wts{1.0, 0.0, 0.0};
    double spread = 0.0;
};

struct ForecastRec {
    int tick = -10000;
    int ctx = 0;
    V2 pos{};
    V2 vel{};
    std::array<std::array<V2, 3>, NV_MODES> pred{};
};

struct EnemyLearning {
    std::array<ForecastRec, NV_HIST> rec{};
    double modelScore[5][NV_MODES]{};
    std::array<double, 6> aimSkill{}; // 0..4 linear/smooth leads, 5 = modeledAim!
    int throwsSeen = 0, lastThrowTick = -10000;
    int parriesSeen = 0, lastParryTick = -10000;
    int dashesSeen = 0;
    int stillStreak = 0;
};

struct EnemyTrack {
    bool active = false;
    bool enemy = false;
    bool vis = false;
    Player obsP{};
    Dart obsD{};
    double conf = 1.0;
    double dist = 1e9;
    double distracted = 0.0;
    bool armedNow = false;
    int readyTick = 100;
    int inferredSlashEnd = -1000;
    V2 inferredSlashPos{};
    std::array<EnemyModel, NV_MODES> models{};
    std::array<std::array<std::array<V2, NV_FUT + 1>, 4>, NV_MODES> evade{};
};

struct DartThreat {
    std::array<Seg, NV_H> segs{};
    double weight = 0.0;
    bool known = false;
    bool returning = false;
    int exactUntil = -1; // ticks j < exactUntil have zero-error deterministic D_OUT flight
    int owner = -1;
    int signal = 0;
};

struct RushThreat {
    V2 hitPos{};
    V2 aimDir{};
    V2 dashDir{};
    double weight = 0.0;
    int owner = -1;
    int hitTick = 0;
    bool armed = false;
};

struct ShotEval {
    V2 dir{};
    double ev = -1e9;
    double rawHit = 0.0;
    double earlyHit = 0.0;
    double parryRisk = 0.0;
    int target = -1;
    double wallCutoff = 0.0;
};

struct Candidate {
    V2 move{};
    V2 aim{};
    V2 after{};
    int act = A_NONE;
    int turnAt = -1;
    int meleeAt = -1;
    int parryThreat = -1;
    double score = -1e18;
};

struct SelfPath {
    std::array<V2, NV_H + 1> p{};
    std::array<int, NV_H + 1> st{};
    std::array<int, NV_H + 1> cd{};
    double pitPen = 0.0;
    int pickTick = 99;
    bool dead = false;
};

} // namespace

struct NeonVortex {
    const GameInfo* gi = nullptr;
    Config cf{};
    const Obs* curObs = nullptr;

    int seat = 0, N = 0, now = 0, lag = 0;
    int aliveEnemies = 0, focus = -1;
    double nearestDist = 1e9;
    double pressure = 0.0;
    double recoveryUrg = 1.0;
    double narrowCore = 0.0;
    double flankNeed = 0.0;
    int blockedTicks = 0;
    int lastOwnBlockedTick = -10000;
    bool dartGuardedByEnemy = false;
    bool winPickupRace = false;
    bool anyKnownDartFlying = false;
    V2 bestPickSpot{};

    V2 here{}, home{}, prevMove{};
    std::array<V2, 32> dirs32{};
    std::array<double, NV_FUT + 1> dartFlight{};
    std::vector<i64> kickDistLut;
    std::vector<PhaseMap> phases;
    std::vector<std::shared_ptr<const Map>> phaseMaps;

    std::array<SelfSnap, NV_HIST> selfHist{};
    std::array<Player, MAXP> lastSeenP{};
    std::array<int, MAXP> lastSeenT{};
    std::array<int, MAXP> contactTick{};
    std::array<V2, MAXP> contactPos{};
    std::array<EnemyTrack, MAXP> tracks{};
    std::array<EnemyLearning, MAXP> learn{};
    std::array<FogState, MAXP> fog{};

    std::vector<DartThreat> threats;
    std::vector<RushThreat> rushes;

    std::vector<float> field;
    int fieldPhase = -1, fieldTick = -100, fieldDartSt = -1;

    struct KickEntry { int x = 0, y = 0, vx = 0, vy = 0, keyT = -999999; bool fatal = false; };
    mutable std::array<KickEntry, 4096> kickCache{};
    mutable int kickSimCount = 0;

    mutable i64 workCount = 0;
    double hitRad = 720.0;
    i64 tCpu0 = -1;

    // Pure thread CPU elapsed time (immune to OS wall-clock preemption under parallel workers)
    double cpuElapsedMs() const {
        if (tCpu0 < 0) return 0.0;
        const i64 cur = thread_cpu_100ns();
        return (cur > tCpu0) ? double(cur - tCpu0) * 0.0001 : 0.0;
    }

    int phaseOf(int t) const {
        if (phases.empty()) return 0;
        if (t < cf.col_start) return 0;
        const int p = 1 + int((t - cf.col_start) / std::max<i64>(1, cf.col_every));
        return std::clamp(p, 0, int(phases.size()) - 1);
    }
    const PhaseMap& pmap(int t) const { return phases[size_t(phaseOf(t))]; }
    bool isPit(V2 p, int t) const {
        return cell_of_point(cf, gi->M(), iround(p.x), iround(p.y), t) == C_PIT;
    }
    bool hasWall(V2 a, V2 b, int t) const {
        return wall_between(pmap(t).m, iround(a.x), iround(a.y), iround(b.x), iround(b.y));
    }
    V2 selfPosAt(int t) const {
        if (t >= now) return here;
        return vpos(selfHist[size_t(std::max(0, t) % NV_HIST)].me);
    }

    // ---------------------------------------------------------------- Init & Map Precomputation
    void init(const GameInfo& game) {
        gi = &game;
        cf = game.cfg;
        seat = game.seat;
        N = game.n;
        hitRad = double(cf.body_r + cf.dart_r);

        for (int i = 0; i < 32; ++i) {
            const double ang = 2.0 * NV_PI * i / 32.0;
            dirs32[i] = {std::cos(ang), std::sin(ang)};
        }

        dartFlight[0] = 0.0;
        for (int j = 0; j < NV_FUT; ++j) {
            const i64 sp = (j < cf.throw_pre) ? 0 : imax(cf.dart_vmin, cf.dart_v - (j - cf.throw_pre) * cf.dart_dec);
            dartFlight[j + 1] = std::min(double(cf.range), dartFlight[j] + double(sp));
        }

        kickDistLut.assign(size_t(cf.kick_v + 1), 0);
        for (i64 v0 = 0; v0 <= cf.kick_v; ++v0) {
            i64 v = v0, dist = 0;
            for (i64 j = 0; j < cf.kick_stun && v > 0; ++j) {
                dist += v;
                v = rdiv(v * cf.kick_keep, 1000);
            }
            kickDistLut[size_t(v0)] = dist;
        }

        const Map& base = gi->M();
        V2 sum{};
        int permCnt = 0;
        for (int y = 0; y < base.ny; ++y) {
            for (int x = 0; x < base.nx; ++x) {
                const int c = base.at(x, y);
                if (base.wave(x, y) < 0 && (c == C_FLOOR || c == C_GRASS)) {
                    sum = sum + V2{double(cell_center(x)), double(cell_center(y))};
                    ++permCnt;
                }
            }
        }
        home = permCnt > 0 ? sum * (1.0 / permCnt) : V2{base.width() * 0.5, base.height() * 0.5};

        phases.resize(size_t(base.nwave + 1));
        phaseMaps.resize(size_t(base.nwave + 1));
        for (int f = 0; f <= base.nwave; ++f) {
            buildPhase(f);
            phaseMaps[size_t(f)] = std::make_shared<const Map>(phases[size_t(f)].m);
        }

        {
            const PhaseMap& fin = phases.back();
            double tot = 0.0, tight = 0.0;
            for (int i = 0; i < fin.N; ++i) {
                if (fin.usable[i]) {
                    tot += 1.0;
                    tight += clamp01((1350.0 - fin.room[i]) / 1050.0);
                }
            }
            narrowCore = tot > 0.0 ? clamp01((tight / tot - 0.18) / 0.55) : 0.0;
        }

        lastSeenT.fill(-10000);
        contactTick.fill(-10000);
        for (int k = 0; k < N; ++k) {
            lastSeenP[k] = Player();
            lastSeenP[k].alive = 1;
            lastSeenP[k].x = gi->spx[k];
            lastSeenP[k].y = gi->spy[k];
            lastSeenT[k] = 0;
        }
        threats.reserve(160);
        rushes.reserve(64);
    }

    void buildPhase(int f) {
        PhaseMap& z = phases[size_t(f)];
        const Map& base = gi->M();
        const i64 t = (f == 0) ? 0 : (cf.col_start + i64(f - 1) * cf.col_every);
        z.m = live_map(cf, base, t);
        z.nx = base.nx;
        z.ny = base.ny;
        z.N = z.nx * z.ny;
        const int nx = z.nx, ny = z.ny, num = z.N;

        z.usable.assign(size_t(num), 0);
        z.wallNear.assign(size_t(num), 0);
        z.wallPref.assign(size_t((nx + 1) * (ny + 1)), 0);
        z.room.assign(size_t(num), 0.0f);
        z.coreDist.assign(size_t(num), 1e6f);
        z.grassScore.assign(size_t(num), 0.0f);
        z.edges.assign(size_t(num), {});

        for (int y = 0; y < ny; ++y) {
            int rowSum = 0;
            for (int x = 0; x < nx; ++x) {
                rowSum += (z.m.at(x, y) == C_WALL) ? 1 : 0;
                z.wallPref[(y + 1) * (nx + 1) + (x + 1)] = z.wallPref[y * (nx + 1) + (x + 1)] + rowSum;
            }
        }

        using PQItem = std::pair<float, int>;
        std::priority_queue<PQItem, std::vector<PQItem>, std::greater<PQItem>> pq;

        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                const int id = y * nx + x;
                const int ct = z.m.at(x, y);
                z.usable[id] = (ct == C_FLOOR || ct == C_GRASS) ? 1 : 0;
                double minSq = 3500.0 * 3500.0;
                int wn = 0;
                for (int dy = -5; dy <= 5; ++dy) {
                    for (int dx = -5; dx <= 5; ++dx) {
                        const int nc = z.m.at(x + dx, y + dy);
                        if (nc == C_WALL && std::abs(dx) <= 2 && std::abs(dy) <= 2) wn = 1;
                        if (nc != C_WALL && nc != C_PIT) continue;
                        const double ax = std::max(0.0, std::abs(dx) * double(CELL) - 0.5 * CELL);
                        const double ay = std::max(0.0, std::abs(dy) * double(CELL) - 0.5 * CELL);
                        minSq = std::min(minSq, ax * ax + ay * ay);
                    }
                }
                z.wallNear[id] = uint8_t(wn);
                z.room[id] = float(std::sqrt(minSq));
                if (!z.usable[id]) continue;

                if (ct == C_GRASS) {
                    int gcnt = 0, pcnt = 0;
                    for (int dy = -2; dy <= 2; ++dy) {
                        for (int dx = -2; dx <= 2; ++dx) {
                            const int gx = x + dx, gy = y + dy;
                            if (z.m.inside(gx, gy) && z.m.at(gx, gy) == C_GRASS) {
                                ++gcnt;
                                if (base.wave(gx, gy) < 0) ++pcnt;
                            }
                        }
                    }
                    z.grassScore[id] = 0.55f + 0.30f * (gcnt / 25.0f) + 0.25f * (pcnt / 25.0f);
                }

                if (base.wave(x, y) < 0) {
                    const V2 p{double(cell_center(x)), double(cell_center(y))};
                    const float seedCost = float(0.12 * (p - home).len() + 0.35 * std::max(0.0, 1250.0 - z.room[id]));
                    z.coreDist[id] = seedCost;
                    pq.push({seedCost, id});
                }
            }
        }

        const double jumpLen = double(cf.dash_t * cf.dash_v);
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                const int id = y * nx + x;
                if (!z.usable[id]) continue;
                const V2 from{double(cell_center(x)), double(cell_center(y))};
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!dx && !dy) continue;
                        const int gx = x + dx, gy = y + dy;
                        if (!base.inside(gx, gy)) continue;
                        const int q = gy * nx + gx;
                        if (!z.usable[q]) continue;
                        float meters = (dx && dy ? 1.4142f : 1.0f) * float(CELL) / 1000.0f;
                        if (dx && dy && (!z.usable[y * nx + gx] || !z.usable[gy * nx + x])) meters += 0.35f;
                        if (z.wallNear[q] && circle_hits_wall(z.m, cf.body_r, cell_center(gx), cell_center(gy))) {
                            meters += 0.55f;
                        }
                        z.edges[id].push_back({q, meters, false});
                    }
                }
                if (!z.wallNear[id] || !circle_hits_wall(z.m, cf.body_r, iround(from.x), iround(from.y))) {
                    for (int u = 0; u < 32; u += 2) {
                        const V2 end = from + dirs32[u] * jumpLen;
                        const int ex = cell_index(iround(end.x)), ey = cell_index(iround(end.y));
                        if (!base.inside(ex, ey)) continue;
                        const int eid = ey * nx + ex;
                        if (!z.usable[eid]) continue;
                        bool overPit = false, clear = true;
                        for (int s = 1; s <= 6; ++s) {
                            const V2 pt = from + dirs32[u] * (jumpLen * s / 6.0);
                            const int tx = cell_index(iround(pt.x)), ty = cell_index(iround(pt.y));
                            if (z.m.at(tx, ty) == C_PIT) overPit = true;
                            if (circle_hits_wall(z.m, cf.body_r, iround(pt.x), iround(pt.y))) {
                                clear = false;
                                break;
                            }
                        }
                        if (overPit && clear) {
                            z.edges[id].push_back({eid, 2.35f, true});
                        }
                    }
                }
            }
        }

        while (!pq.empty()) {
            const auto [dv, u] = pq.top();
            pq.pop();
            if (dv != z.coreDist[u]) continue;
            for (const auto& e : z.edges[u]) {
                const float penalty = e.dash ? 650.0f : (z.room[e.to] < 320.0f ? 80.0f : 0.0f);
                const float nv = dv + e.cost * 1000.0f + penalty;
                if (nv < z.coreDist[e.to]) {
                    z.coreDist[e.to] = nv;
                    pq.push({nv, e.to});
                }
            }
        }
        for (int i = 0; i < num; ++i) {
            if (z.coreDist[i] > 5e5f) {
                const V2 p{double(cell_center(i % nx)), double(cell_center(i / nx))};
                z.coreDist[i] = float((p - home).len() + 1800.0);
            }
        }

        auto computeLatest = [&](bool allowDash) {
            const int NEVER = int(cf.steps + 300);
            std::vector<int> latest(size_t(num), -10000);
            std::vector<int> falls(size_t(num), -10000);
            std::priority_queue<std::pair<int, int>> maxq;
            for (int i = 0; i < num; ++i) {
                if (!z.usable[i]) continue;
                const i64 wf = wave_fall_step(cf, base.wave(i % nx, i / nx));
                falls[i] = (wf < 0) ? NEVER : int(wf);
                if (wf < 0) {
                    latest[i] = NEVER;
                    maxq.push({NEVER, i});
                }
            }
            if (maxq.empty()) {
                latest.assign(size_t(num), NEVER);
                return latest;
            }
            while (!maxq.empty()) {
                const auto [lv, v] = maxq.top();
                maxq.pop();
                if (lv != latest[v]) continue;
                for (const auto& e : z.edges[v]) {
                    if (e.dash && !allowDash) continue;
                    const int u = e.to;
                    const int dur = e.dash ? int(cf.dash_pre + cf.dash_t + cf.dash_post)
                                           : int(std::ceil(e.cost * 1000.0 / double(std::max<i64>(1, cf.run_v))));
                    const int startup = e.dash ? int(cf.dash_pre + 1) : 1;
                    const int dep = std::min(falls[u] - startup, lv - dur);
                    if (dep > latest[u]) {
                        latest[u] = dep;
                        maxq.push({dep, u});
                    }
                }
            }
            return latest;
        };
        z.latestWalk = computeLatest(false);
        z.latestDash = computeLatest(true);
    }

    // ---------------------------------------------------------------- Exact Single-Body & Dart Physics
    void beginAction(Body& b, int act, V2 aim, bool armed) const {
        if (b.st == S_RECALL && act != A_RECALL && act != A_NONE) {
            b.st = S_FREE;
            b.timer = 0;
        }
        const bool dashCancel = (b.st == S_DASH && cf.dash_slash &&
                                 b.timer > cf.dash_post && b.timer <= cf.dash_t + cf.dash_post &&
                                 act == A_MELEE);
        if (dashCancel) {
            b.st = S_FREE;
            b.timer = 0;
        }
        if (act == A_NONE || b.st != S_FREE) return;
        if (act == A_RECALL) {
            if (cf.recall) {
                b.st = S_RECALL;
                b.timer = int(cf.recall_pre);
            }
            return;
        }
        if (act == A_DASH && b.cd > 0) return;
        const V2 u = aim.unit(1000.0);
        b.ax = iround(u.x);
        b.ay = iround(u.y);
        if (b.ax == 0 && b.ay == 0) { b.ax = 1000; b.ay = 0; }
        if (act == A_THROW) {
            b.st = S_THROW;
            b.timer = int(cf.throw_pre);
        } else if (act == A_MELEE) {
            b.st = armed ? S_SLASH : S_KICK;
            b.timer = int(armed ? cf.slash_pre : cf.kick_pre);
        } else if (act == A_DASH) {
            b.st = S_DASH;
            b.timer = int(cf.dash_pre + cf.dash_t + cf.dash_post);
            b.cd = int(cf.dash_cd);
        }
    }

    bool advanceBody(Body& b, V2 move, int t) const {
        const PhaseMap& z = pmap(t);
        const bool dashMov = (b.st == S_DASH && b.timer > cf.dash_post && b.timer <= cf.dash_t + cf.dash_post);
        const bool airborne = (dashMov && b.timer > cf.dash_post + 1) || (b.st == S_STUN && b.timer > 1);

        i64 vx = 0, vy = 0;
        if (dashMov) {
            vx = rdiv(b.ax * cf.dash_v, UNIT);
            vy = rdiv(b.ay * cf.dash_v, UNIT);
        } else if (b.st == S_STUN) {
            vx = b.vx;
            vy = b.vy;
        } else {
            double sp = 0.0;
            if (b.st == S_FREE) sp = double(cf.run_v);
            else if (b.st == S_THROW || b.st == S_THROWN) sp = double(cf.run_v * cf.throw_move) / 100.0;
            const V2 tv = move * sp;
            vx = iround(tv.x);
            vy = iround(tv.y);
            i64 dvx = vx - b.vx, dvy = vy - b.vy;
            cap_len(dvx, dvy, cf.acc);
            vx = b.vx + dvx;
            vy = b.vy + dvy;
        }

        if (z.wallNear[z.clampIdx(b.pt())] || z.wallNear[z.clampIdx({double(b.x + vx), double(b.y + vy)})]) {
            move_body(z.m, cf.body_r, b.x, b.y, vx, vy);
        } else {
            b.x += vx;
            b.y += vy;
        }

        if (dashMov && b.timer == cf.dash_post + 1) {
            b.vx = rdiv(vx * cf.run_v, imax(1, cf.dash_v));
            b.vy = rdiv(vy * cf.run_v, imax(1, cf.dash_v));
        } else if (b.st == S_STUN) {
            b.vx = rdiv(vx * cf.kick_keep, 1000);
            b.vy = rdiv(vy * cf.kick_keep, 1000);
        } else {
            b.vx = vx;
            b.vy = vy;
        }

        if (b.st != S_FREE && b.st != S_DEAD) {
            if (--b.timer <= 0) {
                if (b.st == S_THROW) { b.st = S_THROWN; b.timer = int(cf.throw_post); }
                else if (b.st == S_SLASH) { b.st = S_SLASHED; b.timer = int(cf.slash_post); }
                else if (b.st == S_KICK) { b.st = S_KICKED; b.timer = int(cf.kick_post); }
                else if (b.st == S_STUN) { b.st = S_FREE; b.timer = 1; }
                else { b.st = S_FREE; b.timer = 0; }
            }
        } else if (b.st == S_FREE && b.timer > 0) {
            --b.timer;
        }
        if (b.cd > 0) --b.cd;
        return airborne || !isPit(b.pt(), t);
    }

    void predictBodyStep(Body& b, V2 intent, int t) const {
        Body old = b;
        if (advanceBody(b, intent, t)) return;
        if (old.st == S_FREE || old.st == S_THROW || old.st == S_THROWN) {
            Body bx = old, by = old;
            const bool okx = advanceBody(bx, {intent.x, 0.0}, t);
            const bool oky = advanceBody(by, {0.0, intent.y}, t);
            if (okx && (!oky || (bx.pt() - old.pt()).sq() >= (by.pt() - old.pt()).sq())) b = bx;
            else if (oky) b = by;
            else {
                b = old;
                advanceBody(b, {0.0, 0.0}, t);
                if (isPit(b.pt(), t)) { b.x = old.x; b.y = old.y; b.vx = b.vy = 0; }
            }
        } else {
            b.x = old.x;
            b.y = old.y;
        }
    }

    bool kickProtected(const Body& b) const {
        return b.st == S_STUN || (b.st == S_FREE && b.timer > 0);
    }

    bool kickSlideFatal(V2 victim, V2 dir, int startTick) const {
        const V2 v = dir.unit(double(cf.kick_v));
        const i64 vx = iround(v.x), vy = iround(v.y);
        const i64 x0 = iround(victim.x), y0 = iround(victim.y);
        const int lastT = startTick + int(cf.kick_stun) - 1;
        const i64 ex = x0 + (vx < 0 ? -1 : 1) * kickDistLut[size_t(std::min<i64>(cf.kick_v, iabs(vx)))];
        const i64 ey = y0 + (vy < 0 ? -1 : 1) * kickDistLut[size_t(std::min<i64>(cf.kick_v, iabs(vy)))];
        const i64 pad = cf.body_r + 4;
        const PhaseMap& z = pmap(startTick);
        if (!z.wallsInBox(cell_index(imin(x0, ex) - pad), cell_index(imin(y0, ey) - pad),
                          cell_index(imax(x0, ex) + pad), cell_index(imax(y0, ey) + pad))) {
            return cell_of_point(cf, gi->M(), ex, ey, lastT) == C_PIT;
        }
        const int tx = int(rdiv(x0, 40)), ty = int(rdiv(y0, 40));
        const int ux = int(rdiv(vx, 10)), uy = int(rdiv(vy, 10));
        const int keyT = (phaseOf(startTick) == phaseOf(lastT)) ? phaseOf(startTick) : -1 - startTick;
        const unsigned h = unsigned(tx) * 2654435761u ^ unsigned(ty) * 40503u ^
                           unsigned(ux) * 12289u ^ unsigned(uy) * 769u ^ unsigned(keyT);
        KickEntry& ce = kickCache[h % kickCache.size()];
        if (ce.keyT == keyT && ce.x == tx && ce.y == ty && ce.vx == ux && ce.vy == uy) return ce.fatal;
        if (kickSimCount >= 80) {
            return cell_of_point(cf, gi->M(), ex, ey, lastT) == C_PIT && !hasWall(victim, {double(ex), double(ey)}, startTick);
        }
        ++kickSimCount;
        Body b;
        b.x = x0; b.y = y0; b.vx = vx; b.vy = vy; b.st = S_STUN; b.timer = int(cf.kick_stun);
        for (int tt = startTick; tt <= lastT; ++tt) advanceBody(b, {}, tt);
        const bool fat = isPit(b.pt(), lastT);
        ce = {tx, ty, ux, uy, keyT, fat};
        return fat;
    }

    // Multi-angle cliff-kick check: catches enemies who angle a dash (±28°) to flank-kick us off a narrow bridge!
    double kickThreatFatal(V2 myPt, V2 enPt, int startTick, bool enemyCanDash) const {
        const V2 dir = myPt - enPt;
        if (kickSlideFatal(myPt, dir, startTick)) return 1.0;
        if (!enemyCanDash || NV_ANGLE_KICK <= 0.0) return 0.0;
        const double c = 0.88294759, s = 0.46947156; // cos/sin(28 deg)
        const V2 dL{dir.x * c - dir.y * s, dir.x * s + dir.y * c};
        const V2 dR{dir.x * c + dir.y * s, -dir.x * s + dir.y * c};
        const bool fL = kickSlideFatal(myPt, dL, startTick);
        const bool fR = kickSlideFatal(myPt, dR, startTick);
        if (fL && fR) return 0.85 * NV_ANGLE_KICK;
        if (fL || fR) return 0.65 * NV_ANGLE_KICK;
        return 0.0;
    }

    Seg flyDart(Dart& d, V2 ownerNow, V2 ownerPrev, int t) const {
        if (!dart_flying(d.st)) return {};
        const i64 spd = dart_step_len(cf, d);
        const i64 x0 = d.x, y0 = d.y;
        const i64 st = d.st;
        i64 x1 = x0, y1 = y0;
        bool reached = false;
        if (st == D_OUT) {
            const i64 m = imin(spd, d.left);
            d.left -= m;
            x1 = x0 + rdiv(d.vx * m, UNIT);
            y1 = y0 + rdiv(d.vy * m, UNIT);
        } else if (spd > 0) {
            const i64 gx = iround(ownerNow.x) - x0, gy = iround(ownerNow.y) - y0;
            const i64 gl = ilen(gx, gy);
            if (gl <= spd) {
                x1 = iround(ownerNow.x);
                y1 = iround(ownerNow.y);
                reached = true;
            } else {
                x1 = x0 + rdiv(gx * spd, gl);
                y1 = y0 + rdiv(gy * spd, gl);
            }
        }
        i64 hx = 0, hy = 0;
        const bool wallHit = !(st == D_CALL && cf.recall_wall) && seg_wall(pmap(t).m, x0, y0, x1, y1, hx, hy);
        if (wallHit) { x1 = hx; y1 = hy; reached = false; }
        d.x = x1; d.y = y1; ++d.age;
        const V2 a{double(x0), double(y0)}, b{double(x1), double(y1)};
        if (wallHit) {
            d.st = D_GROUND; d.left = 0; d.age = 0; d.vx = d.vy = 0;
        } else if (st == D_OUT && d.left <= 0) {
            d.st = D_BACK; d.left = 0; d.age = 0; d.vx = d.vy = 0;
        } else if (st != D_OUT && (reached || seg_origin_sq(a - ownerPrev, b - ownerNow) <= double(cf.catch_r * cf.catch_r))) {
            d.st = D_HAND; d.age = 0;
        }
        return {a, b, true};
    }

    // ---------------------------------------------------------------- Tracking, Fog & Online Learning
    Player updateHiddenFog(int k, int refTick) {
        Player p = lastSeenP[k];
        const V2 old = vpos(p);
        const V2 viewer = selfPosAt(refTick);
        const PhaseMap& z = pmap(refTick);
        const int gw = z.nx * 2, gh = z.ny * 2;
        const int half = int(CELL / 2);
        auto centerOf = [&](int id) -> V2 {
            return {double(id % gw) * half + half * 0.5, double(id / gw) * half + half * 0.5};
        };
        auto okPt = [&](int id) -> bool {
            const V2 pt = centerOf(id);
            if (cell_of_point(cf, gi->M(), iround(pt.x), iround(pt.y), refTick) != C_GRASS) return false;
            if ((pt - viewer).len() < double(cf.see_r) - 180.0) return false;
            if (circle_hits_wall(z.m, cf.body_r, iround(pt.x), iround(pt.y))) return false;
            return true;
        };

        FogState& f = fog[k];
        std::vector<int> next;
        std::vector<uint8_t> vis(size_t(gw * gh), 0);
        if (f.entry != lastSeenT[k] || f.phase != phaseOf(refTick) || f.cells.empty()) {
            f.entry = lastSeenT[k];
            f.phase = phaseOf(refTick);
            const double rad = double(cf.run_v) * std::max(1, refTick - lastSeenT[k]) + 320.0;
            for (int id = 0; id < gw * gh; ++id) {
                if ((centerOf(id) - old).len() <= rad && okPt(id)) {
                    vis[size_t(id)] = 1;
                    next.push_back(id);
                }
            }
        } else if (f.lastTick != refTick) {
            for (int id : f.cells) {
                const int cx = id % gw, cy = id / gw;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx2 = cx + dx, ny2 = cy + dy;
                        if (nx2 < 0 || ny2 < 0 || nx2 >= gw || ny2 >= gh) continue;
                        const int nid = ny2 * gw + nx2;
                        if (vis[size_t(nid)]) continue;
                        vis[size_t(nid)] = 1;
                        if (okPt(nid)) next.push_back(nid);
                    }
                }
            }
        } else {
            next = f.cells;
        }
        if (next.empty()) {
            for (int id = 0; id < gw * gh; ++id) {
                if (okPt(id)) next.push_back(id);
            }
        }
        f.lastTick = refTick;
        f.cells.swap(next);

        const int age = std::max(0, refTick - lastSeenT[k]);
        const V2 est = old + vvel(p) * double(std::min(4, age));
        V2 bestLikely = est, bestClose = est, bestCore = est;
        double bestW = -1.0, minD = 1e30, minCore = 1e30;
        double sumW = 0.0;
        V2 mean{};
        for (int id : f.cells) {
            const V2 q = centerOf(id);
            const double dMe = (q - viewer).len();
            const double dEst = (q - est).len();
            const double cd = z.coreDist[z.clampIdx(q)];
            const double w = 0.20 + 0.85 * std::exp(-dEst / (420.0 + std::min(30, age) * 45.0)) +
                             0.50 * std::exp(-cd / 3000.0);
            sumW += w;
            mean = mean + q * w;
            if (w > bestW) { bestW = w; bestLikely = q; }
            if (dMe < minD) { minD = dMe; bestClose = q; }
            if (cd < minCore) { minCore = cd; bestCore = q; }
        }
        if (sumW > 0.0) {
            mean = mean * (1.0 / sumW);
            double sp = 0.0;
            for (int id : f.cells) sp += (centerOf(id) - mean).len();
            f.spread = sp / double(std::max<size_t>(1, f.cells.size()));
            f.pts = {bestLikely, bestClose, bestCore};
            f.wts = {0.46, 0.36, 0.18};
        } else {
            f.spread = 3500.0;
            f.pts = {old, old, old};
            f.wts = {1.0, 0.0, 0.0};
        }

        p.x = iround(f.pts[0].x);
        p.y = iround(f.pts[0].y);
        p.vx = p.vy = 0;
        p.st = S_FREE;
        p.timer = 0;
        p.cd = 0;
        p.expose = 0;
        return p;
    }

    V2 aimStyleTarget(int style, int seenTick, double travel) const {
        const SelfSnap& sn = selfHist[size_t(std::clamp(seenTick, 0, now) % NV_HIST)];
        const V2 base = vpos(sn.me);
        V2 v = vvel(sn.me);
        if (style == 4 && seenTick >= 3) {
            v = (base - selfPosAt(seenTick - 3)) * (1.0 / 3.0);
        }
        if (v.len() > double(cf.run_v)) v = v.unit(double(cf.run_v));
        const double lead = (style == 0) ? 0.0
                          : (style == 1) ? double(cf.delay)
                          : (style == 2) ? double(cf.delay + cf.throw_pre) + 0.55 * travel
                          : double(cf.delay + cf.throw_pre) + travel;
        if (sn.me.st == S_DASH || sn.me.st == S_STUN) {
            Body b(sn.me);
            const int steps = std::clamp(int(std::round(lead)), 0, 18);
            for (int j = 0; j < steps && b.st != S_FREE; ++j) predictBodyStep(b, {}, seenTick + j);
            return b.pt();
        }
        return base + v * lead;
    }

    // Smart multi-hypothesis enemy aim model (predicts where an elite AI bot like Astra/XHigh aims at us!)
    V2 smartEnemyAim(V2 orig, int seenTick, double travel) const {
        const SelfSnap& sn = selfHist[size_t(std::clamp(seenTick, 0, now) % NV_HIST)];
        const V2 start = vpos(sn.me);
        const V2 toward = (orig - start).unit();
        V2 drift = vvel(sn.me) * (1.0 / double(cf.run_v));
        if (drift.sq() > 1.0) drift = drift.unit();
        V2 smooth = (seenTick >= 4) ? (start - selfPosAt(seenTick - 4)) * (1.0 / (4.0 * double(cf.run_v))) : drift;
        if (smooth.sq() > 1.0) smooth = smooth.unit();
        const V2 goal = (sn.dart.st == D_GROUND) ? (vpos(sn.dart) - start).unit() : (home - start).unit();
        const std::array<V2, 7> mv = {drift, V2{0.0, 0.0}, smooth, goal, toward, toward.perp(), toward.perp() * -1.0};
        const std::array<double, 7> wt = {0.22, 0.18, 0.19, 0.13, 0.12, 0.08, 0.08};
        std::array<V2, 7> pts{};
        V2 mean{};
        const int steps = std::clamp(int(std::round(double(cf.delay + cf.throw_pre) + travel)), 3, 20);
        for (int m = 0; m < 7; ++m) {
            Body b(sn.me);
            for (int j = 0; j < steps; ++j) {
                if (b.st == S_FREE && mv[m].sq() < 1e-6 && b.vx == 0 && b.vy == 0) break;
                predictBodyStep(b, mv[m], seenTick + j);
            }
            pts[m] = b.pt();
            mean = mean + pts[m] * wt[m];
        }
        double bestSc = -1.0;
        V2 bestPt = mean;
        for (int m = -1; m < 7; ++m) {
            const V2 tgt = (m < 0) ? mean : pts[size_t(m)];
            const V2 axis = (tgt - orig).unit();
            if (axis.sq() < 0.5) continue;
            double sc = 0.0;
            for (int i = 0; i < 7; ++i) {
                const double err = std::abs((pts[size_t(i)] - orig).dot(axis.perp()));
                sc += wt[size_t(i)] * clamp01((hitRad + 320.0 - err) / (hitRad + 180.0));
            }
            if (sc > bestSc) { bestSc = sc; bestPt = tgt; }
        }
        return bestPt;
    }

    void learnEnemyAim(int k, int refTick, const Player& p) {
        if (p.st != S_THROW) return;
        const int when = refTick - int(cf.throw_pre - p.timer);
        EnemyLearning& L = learn[k];
        if (when < 0 || L.lastThrowTick == when) return;
        L.lastThrowTick = when;
        const int seen = std::max(0, when - int(cf.delay));
        const V2 orig = vpos(p);
        const V2 dir = V2{double(p.ax), double(p.ay)}.unit();
        const V2 myOld = selfPosAt(seen);
        if ((myOld - orig).dot(dir) < 0.0 || (myOld - orig).len() > double(cf.range + 2200)) return;
        const double travel = (myOld - orig).len() / (0.80 * double(cf.dart_v));
        double minErr = 1e9;
        std::array<double, 6> errs{};
        for (int s = 0; s < 5; ++s) {
            errs[s] = std::abs((aimStyleTarget(s, seen, travel) - orig).dot(dir.perp()));
            minErr = std::min(minErr, errs[s]);
        }
        errs[5] = std::abs((smartEnemyAim(orig, seen, travel) - orig).dot(dir.perp()));
        minErr = std::min(minErr, errs[5]);
        if (minErr > 1500.0) return;
        ++L.throwsSeen;
        for (int s = 0; s < 6; ++s) {
            const double lik = std::exp(-errs[s] / 580.0);
            L.aimSkill[s] = 0.65 * L.aimSkill[s] + 0.35 * lik;
        }
    }

    const Body& predBody(int k, int dt, int m = 0) const {
        if (dt < 0) return tracks[k].models[m].past[size_t(std::max(0, lag + dt))];
        return tracks[k].models[m].fut[size_t(std::min(NV_FUT, dt))];
    }

    void updateTrackers() {
        const int refT = now - lag;
        aliveEnemies = 0;
        nearestDist = 1e9;
        focus = -1;
        double bestFocusCost = 1e30;

        bool ownSuddenDrop = false;
        V2 dropPos{};
        if (now > 0) {
            const SelfSnap& prev = selfHist[size_t((now - 1) % NV_HIST)];
            if (dart_flying(prev.dart.st) && curObs->my_dart().st == D_GROUND) {
                Dart simD = prev.dart;
                flyDart(simD, here, vpos(prev.me), now - 1);
                if (simD.st != D_GROUND) {
                    ownSuddenDrop = true;
                    dropPos = vpos(curObs->my_dart());
                }
            }
        }

        for (int k = 0; k < N; ++k) {
            EnemyTrack& tr = tracks[k];
            tr.active = (k != seat && curObs->p[k].alive);
            tr.enemy = (k != seat && !is_mate(*gi, k));
            tr.vis = curObs->p[k].vis != 0;
            tr.obsD = curObs->d[k];
            if (!tr.active) continue;

            if (tr.vis) {
                tr.obsP = curObs->p[k];
                lastSeenP[k] = tr.obsP;
                lastSeenT[k] = refT;
                tr.conf = 1.0;
                if (vvel(tr.obsP).sq() < 900.0) learn[k].stillStreak = std::min(100, learn[k].stillStreak + 1);
                else learn[k].stillStreak = 0;
            } else {
                tr.obsP = updateHiddenFog(k, refT);
                tr.conf = 0.90 / (1.0 + fog[k].spread / 3400.0);
                learn[k].stillStreak = 0;
            }

            const Player& p = tr.obsP;
            EnemyLearning& L = learn[k];
            if (tr.vis) {
                learnEnemyAim(k, refT, p);
                if ((p.st == S_SLASH || p.st == S_SLASHED) && refT - L.lastParryTick > cf.slash_post + cf.slash_pre) {
                    const Dart& od = selfHist[size_t(refT % NV_HIST)].dart;
                    if (dart_flying(od.st) && (vpos(od) - vpos(p)).len() < double(cf.slash_reach + 2 * cf.dart_v)) {
                        ++L.parriesSeen;
                        L.lastParryTick = refT;
                    }
                }
                if (p.st == S_DASH && p.timer == cf.dash_pre + cf.dash_t + cf.dash_post - 1) ++L.dashesSeen;
            }

            V2 foeTarget = here;
            double bestOppD2 = (here - vpos(p)).sq();
            for (int q = 0; q < N; ++q) {
                if (q == seat || q == k || !curObs->p[q].alive || !curObs->p[q].vis) continue;
                if (gi->team[q] == gi->team[k]) continue;
                const double d2 = (vpos(curObs->p[q]) - vpos(p)).sq();
                if (d2 < bestOppD2) {
                    bestOppD2 = d2;
                    foeTarget = vpos(curObs->p[q]);
                }
            }

            const V2 curV = vvel(p);
            V2 carry = curV * (1.0 / double(cf.run_v));
            if (carry.sq() > 1.0) carry = carry.unit();
            if ((p.st == S_THROW || p.st == S_THROWN) && curV.sq() > 100.0) carry = curV.unit();
            if (p.st == S_DASH) carry = V2{double(p.ax), double(p.ay)}.unit();
            const bool still = curV.sq() < 1500.0;

            V2 smooth = carry;
            if (refT >= 4 && L.rec[size_t((refT - 4) % NV_HIST)].tick == refT - 4) {
                smooth = (vpos(p) - L.rec[size_t((refT - 4) % NV_HIST)].pos) * (1.0 / (4.0 * double(cf.run_v)));
                if (smooth.sq() > 1.0) smooth = smooth.unit();
            }

            V2 histDir = smooth;
            if (tr.vis && refT >= 14) {
                double bestMatch = 1e18;
                const V2 dRecent = vpos(p) - L.rec[size_t((refT - 4) % NV_HIST)].pos;
                for (int dt = 8; dt <= std::min(refT - 6, 96); ++dt) {
                    const int s0 = refT - dt;
                    const ForecastRec& r0 = L.rec[size_t(s0 % NV_HIST)];
                    const ForecastRec& rPrev = L.rec[size_t((s0 - 4) % NV_HIST)];
                    const ForecastRec& rFut = L.rec[size_t((s0 + 5) % NV_HIST)];
                    if (r0.tick != s0 || rPrev.tick != s0 - 4 || rFut.tick != s0 + 5) continue;
                    const V2 dOld = r0.pos - rPrev.pos;
                    const double err = (dOld - dRecent).sq() + (r0.vel - curV).sq() * 2.0;
                    if (err < bestMatch) {
                        bestMatch = err;
                        histDir = (rFut.pos - r0.pos) * (1.0 / (5.0 * double(cf.run_v)));
                        if (histDir.sq() > 1.0) histDir = histDir.unit();
                    }
                }
            }

            const V2 toward = (foeTarget - vpos(p)).unit();
            const V2 sideL = toward.perp();
            const V2 pickOrCore = (tr.obsD.st == D_GROUND)
                                  ? (vpos(tr.obsD) - vpos(p)).unit()
                                  : (home - vpos(p)).unit();

            const std::array<V2, NV_MODES> mdir = {
                carry, V2{0.0, 0.0}, smooth, toward, toward * -1.0,
                sideL, sideL * -1.0, pickOrCore, histDir, toward
            };
            std::array<double, NV_MODES> mw = {0.23, 0.12, 0.15, 0.10, 0.05, 0.07, 0.07, 0.08, 0.09, 0.04};
            // S_RECALL is instantly cancelable! Never assume a recalling enemy will sit still!
            if (p.st == S_RECALL) {
                mw[0] = 0.11; mw[1] = 0.14; mw[4] = 0.14; mw[5] = 0.15; mw[6] = 0.15;
            } else if (still) {
                mw[0] = 0.14; mw[1] = 0.24;
            }
            if (tr.obsD.st == D_GROUND) { mw[7] += 0.24; mw[0] *= 0.72; mw[3] *= 0.55; }

            const int ctx = (p.st == S_DASH || p.st == S_STUN) ? 4
                          : (tr.obsD.st == D_HAND) ? ((here - vpos(p)).len() > 7500.0 ? 0 : 1)
                          : (tr.obsD.st == D_GROUND) ? 3 : 2;

            static constexpr int evalDt[3] = {4, 8, 12};
            ForecastRec& frec = L.rec[size_t(refT % NV_HIST)];
            if (tr.vis && frec.tick != refT) {
                for (int u = 0; u < 3; ++u) {
                    if (refT < evalDt[u]) continue;
                    const ForecastRec& old = L.rec[size_t((refT - evalDt[u]) % NV_HIST)];
                    if (old.tick != refT - evalDt[u]) continue;
                    for (int m = 0; m < NV_MODES; ++m) {
                        const double sc = std::exp(-(old.pred[m][u] - vpos(p)).len() / (360.0 + evalDt[u] * 45.0));
                        double& q = L.modelScore[old.ctx][m];
                        q += 0.05 * (sc - q);
                    }
                }
                frec.tick = refT;
                frec.ctx = ctx;
                frec.pos = vpos(p);
                frec.vel = curV;
            }
            for (int m = 0; m < NV_MODES; ++m) {
                mw[m] *= std::exp(4.5 * L.modelScore[ctx][m]);
            }
            if (!tr.vis) {
                std::array<double, 3> grp{};
                for (int m = 0; m < NV_MODES; ++m) grp[m % 3] += mw[m];
                for (int m = 0; m < NV_MODES; ++m) {
                    if (grp[m % 3] > 1e-9) mw[m] *= fog[k].wts[m % 3] / grp[m % 3];
                }
            }
            double wsum = 0.0;
            for (double v : mw) wsum += v;

            tr.armedNow = (tr.obsD.st == D_HAND && p.st != S_THROW);
            tr.readyTick = tr.armedNow ? -lag : 100;
            if (tr.obsD.st == D_GROUND) {
                const double nd = std::max(0.0, (vpos(p) - vpos(tr.obsD)).len() - double(cf.pick_r));
                if (nd < 4500.0) tr.readyTick = int(std::ceil(nd / double(cf.run_v))) - lag;
            }

            for (int m = 0; m < NV_MODES; ++m) {
                EnemyModel& em = tr.models[m];
                em.w = mw[m] / std::max(1e-9, wsum);
                Body b(p);
                if (!tr.vis) {
                    const V2 fp = fog[k].pts[m % 3];
                    b.x = iround(fp.x);
                    b.y = iround(fp.y);
                }
                Dart simD = tr.obsD;
                em.past[0] = b;
                for (int j = 0; j < lag + NV_FUT; ++j) {
                    if (j == lag) {
                        em.fut[0] = b;
                        em.hand[0] = (simD.st == D_HAND);
                    }
                    V2 intent = mdir[m];
                    if (m != 0 && m != 1 && j < 2 && !still) intent = carry;
                    if (m == 7 && simD.st == D_GROUND) intent = (vpos(simD) - b.pt()).unit();
                    // Model 9: dash-rush & dash-cancel melee!
                    if (m == 9 && j == 0 && p.st == S_FREE && p.cd == 0 && (foeTarget - vpos(p)).len() < 6200.0) {
                        beginAction(b, A_DASH, tr.armedNow ? toward : pickOrCore, tr.armedNow);
                    }
                    if ((m == 9 || m == 3) && p.st == S_DASH && b.st == S_DASH &&
                        b.timer > cf.dash_post && b.timer <= cf.dash_post + cf.dash_t - 1 &&
                        (b.pt() + b.vel() - foeTarget).len() < double(cf.slash_reach)) {
                        beginAction(b, A_MELEE, toward, simD.st == D_HAND);
                    }
                    const V2 prevPt = b.pt();
                    predictBodyStep(b, intent, refT + j);
                    if (simD.st == D_HAND && p.st == S_THROW && j + 1 == p.timer) {
                        simD.st = D_OUT; simD.left = cf.range; simD.age = 0;
                        simD.x = b.x; simD.y = b.y; simD.vx = p.ax; simD.vy = p.ay;
                    } else if (simD.st == D_RISE && p.st == S_RECALL && j + 1 == p.timer) {
                        simD.st = D_CALL; simD.age = 0;
                    } else if (dart_flying(simD.st)) {
                        flyDart(simD, b.pt(), prevPt, refT + j);
                    } else if (simD.st == D_GROUND && (b.pt() - vpos(simD)).sq() <= double(cf.pick_r * cf.pick_r)) {
                        simD.st = D_HAND;
                    }
                    if (simD.st == D_HAND) {
                        const int rel = j + 1 - lag;
                        if (rel < tr.readyTick) tr.readyTick = rel;
                    }
                    if (tr.vis) {
                        for (int u = 0; u < 3; ++u) {
                            if (j + 1 == evalDt[u]) frec.pred[m][u] = b.pt();
                        }
                    }
                    if (j < lag) em.past[size_t(j + 1)] = b;
                    else {
                        const int fidx = j - lag + 1;
                        em.fut[size_t(fidx)] = b;
                        em.hand[size_t(fidx)] = (simD.st == D_HAND);
                    }
                }
            }

            if (ownSuddenDrop && tr.enemy) {
                const double dDrop = (tr.models[0].fut[0].pt() - dropPos).len();
                if (dDrop < double(cf.slash_reach + 700)) {
                    tr.inferredSlashEnd = now + int(cf.slash_post);
                    tr.inferredSlashPos = tr.models[0].fut[0].pt();
                    lastOwnBlockedTick = now;
                    ++L.parriesSeen;
                }
            }

            tr.dist = (tr.models[0].fut[0].pt() - here).len();
            tr.distracted = ((foeTarget - here).sq() > 500.0 * 500.0)
                            ? clamp01((tr.dist - (vpos(p) - foeTarget).len() + 300.0) / 3400.0)
                            : 0.0;
            if (tr.enemy) {
                ++aliveEnemies;
                nearestDist = std::min(nearestDist, tr.dist);
                double cost = tr.dist + 950.0 * (1.0 - tr.conf) +
                              (hasWall(here, tr.models[0].fut[0].pt(), now) ? 1200.0 : 0.0);
                // In FFA, prioritize vulnerable / distracted / unarmed / locked targets for vulture kills!
                if (NV_FFA_VULTURE && N > 2) {
                    cost -= 1350.0 * tr.distracted;
                    if (tr.readyTick > 5) cost -= 850.0;
                    if (p.st == S_DASH || p.st == S_STUN || p.st == S_SLASHED || p.st == S_KICKED) cost -= 750.0;
                }
                if (cost < bestFocusCost) {
                    bestFocusCost = cost;
                    focus = k;
                }
            }
        }

        // Un-delayed body-push contact inference
        if (now > 0) {
            const SelfSnap& prev = selfHist[size_t((now - 1) % NV_HIST)];
            Body solo(prev.me);
            const Action& pa = prev.act;
            if (solo.st == S_RECALL && (pa.mx || pa.my || (pa.act != A_NONE && pa.act != A_RECALL))) {
                solo.st = S_FREE; solo.timer = 0;
            }
            if (pa.act) beginAction(solo, int(pa.act), {double(pa.ax), double(pa.ay)}, prev.dart.st == D_HAND);
            V2 mv{double(pa.mx) * 0.001, double(pa.my) * 0.001};
            if (mv.sq() > 1.0) mv = mv.unit();
            advanceBody(solo, mv, now - 1);
            const V2 push = here - solo.pt();
            const double plen = push.len();
            if (plen > 12.0 && plen < double(cf.body_r + 36) &&
                !circle_hits_wall(pmap(now - 1).m, cf.body_r + 34, iround(here.x), iround(here.y))) {
                const V2 other = here - push.unit(2.0 * double(cf.body_r));
                int bestK = -1;
                double d1 = 1e30, d2 = 1e30;
                for (int k = 0; k < N; ++k) {
                    if (!tracks[k].active) continue;
                    double md = 1e30;
                    for (const auto& m : tracks[k].models) md = std::min(md, (m.fut[0].pt() - other).sq());
                    if (md < d1) { d2 = d1; d1 = md; bestK = k; }
                    else d2 = std::min(d2, md);
                }
                if (bestK >= 0 && d1 < 3400.0 * 3400.0 && (d2 > 2400.0 * 2400.0 || d1 < 0.20 * d2)) {
                    contactTick[bestK] = now;
                    contactPos[bestK] = other;
                }
            }
            for (int k = 0; k < N; ++k) {
                EnemyTrack& tr = tracks[k];
                const int age = now - contactTick[k];
                if (!tr.active || age >= lag) continue;
                for (auto& em : tr.models) {
                    Body base = (age > 0) ? em.past[size_t(lag - age)] : em.fut[0];
                    base.x = iround(contactPos[k].x);
                    base.y = iround(contactPos[k].y);
                    for (int j = -age; j < NV_FUT; ++j) {
                        Body& cur = (j < 0) ? em.past[size_t(lag + j)] : em.fut[size_t(j)];
                        const Body& nxt = (j + 1 < 0) ? em.past[size_t(lag + j + 1)] : em.fut[size_t(j + 1)];
                        V2 intent = nxt.vel() * (1.0 / double(cf.run_v));
                        if (intent.sq() > 1.0) intent = intent.unit();
                        cur = base;
                        predictBodyStep(base, intent, now + j);
                    }
                    em.fut[NV_FUT] = base;
                }
                tr.dist = (tr.models[0].fut[0].pt() - here).len();
                tr.conf = 1.0;
                if (tr.enemy && (focus < 0 || tr.dist < nearestDist + 250.0)) {
                    nearestDist = std::min(nearestDist, tr.dist);
                    focus = k;
                }
            }
        }
    }

    // ---------------------------------------------------------------- Threat Construction
    bool hiddenAt(int k, int t) const {
        const SelfSnap& sn = selfHist[size_t(std::clamp(t, 0, now) % NV_HIST)];
        if (sn.me.expose > 0 || dart_flying(sn.dart.st) || sn.dart.st == D_RISE) return false;
        if (cell_of_point(cf, gi->M(), sn.me.x, sn.me.y, t) != C_GRASS) return false;
        const int dt = std::clamp(t - now, -lag, NV_FUT);
        return (vpos(sn.me) - predBody(k, dt, 0).pt()).len() > double(cf.see_r);
    }

    void addKnownDart(int k, int mIdx, double weight) {
        Dart d = curObs->d[k];
        const Player& p = curObs->p[k];
        if (!dart_flying(d.st) && p.st != S_THROW && p.st != S_RECALL) return;
        const bool startedExactOut = (d.st == D_OUT);
        DartThreat th;
        th.known = true;
        th.weight = weight;
        th.owner = k;
        th.returning = (d.st == D_BACK || d.st == D_CALL || d.st == D_RISE);
        th.exactUntil = startedExactOut ? NV_H : -1;
        bool any = false;
        for (int j = -lag; j < NV_H; ++j) {
            V2 a, b;
            if (tracks[k].active) {
                a = predBody(k, j, mIdx).pt();
                b = predBody(k, j + 1, mIdx).pt();
            } else {
                a = b = vpos(p);
            }
            if (j + lag + 1 == p.timer && p.st == S_THROW && d.st == D_HAND) {
                d.st = D_OUT; d.left = cf.range; d.age = 0;
                d.x = iround(b.x); d.y = iround(b.y); d.vx = p.ax; d.vy = p.ay;
                continue;
            }
            if (j + lag + 1 == p.timer && p.st == S_RECALL && d.st == D_RISE) {
                d.st = D_CALL; d.age = 0;
            }
            const bool wasOut = (d.st == D_OUT);
            const Seg seg = flyDart(d, b, a, now + j);
            if (startedExactOut && !wasOut && j >= 0 && th.exactUntil == NV_H) {
                th.exactUntil = j;
            }
            if (seg.on && d.st == D_HAND) tracks[k].readyTick = std::min(tracks[k].readyTick, j + 1);
            if (j >= 0 && seg.on) {
                th.segs[size_t(j)] = seg;
                any = true;
            }
            if (j < 0 && seg.on) {
                const SelfSnap& old = selfHist[size_t((now + j) % NV_HIST)];
                const SelfSnap& nxt = selfHist[size_t((now + j + 1) % NV_HIST)];
                if (old.me.st == S_SLASH && old.me.timer == 1 && cf.block) {
                    const V2 delta = seg.a - vpos(nxt.me);
                    const double dl = delta.len();
                    if (dl <= double(cf.slash_reach) &&
                        delta.dot(V2{double(old.me.ax), double(old.me.ay)}) >= double(cf.block_cos) * dl &&
                        !hasWall(vpos(nxt.me), seg.a, now + j)) {
                        return;
                    }
                }
                if (dart_flying(old.dart.st) && nxt.dart.st == D_GROUND &&
                    (vpos(nxt.dart) - seg.b).len() < double(cf.dart_v + 2 * cf.dart_r)) {
                    const int pickT = int(std::ceil(std::max(0.0, (b - vpos(nxt.dart)).len() - double(cf.pick_r + 320)) / double(cf.run_v)));
                    tracks[k].readyTick = std::min(tracks[k].readyTick, j + 1 + pickT);
                    return;
                }
            }
        }
        if (any) {
            threats.push_back(th);
            if (weight >= 0.25) anyKnownDartFlying = true;
        }
    }

    void addPhantomDarts(int k) {
        const EnemyTrack& tr = tracks[k];
        if (!tr.active || !tr.enemy || tr.readyTick >= NV_H - 3 || tr.dist > double(cf.range + 3500)) return;
        int start = std::max(tr.readyTick, -lag);
        if (tr.obsP.st != S_FREE) start = std::max(start, int(tr.obsP.timer) - lag);
        if (tr.inferredSlashEnd > now) start = std::max(start, tr.inferredSlashEnd - now);
        if (start >= NV_H - 3) return;

        const double interest = 1.0 - 0.50 * tr.distracted;
        const int shotTimes[4] = {start, start + 2, start + 5, start + 8};
        const double timeW[4] = {0.34, 0.27, 0.22, 0.17};
        // 6 styles: 0..4 linear/smooth leads, 5 = smartEnemyAim (modeledAim!)
        const std::array<double, 6> prior = {0.18, 0.12, 0.14, 0.15, 0.12, 0.29};
        std::array<double, 6> sw{};
        double totW = 0.0;
        for (int s = 0; s < 6; ++s) {
            sw[s] = prior[s] * std::exp(5.5 * learn[k].aimSkill[s]);
            totW += sw[s];
        }

        const int nLoc = tr.vis ? 1 : (N > 4 ? 2 : 3);
        for (int u = 0; u < 4; ++u) {
            const int at = shotTimes[u];
            if (at + cf.throw_pre >= NV_H) continue;
            const int seen = std::clamp(now + at - int(cf.delay), 0, now);
            const V2 myOld = selfPosAt(seen);
            const bool hid = hiddenAt(k, seen);
            for (int loc = 0; loc < nLoc; ++loc) {
                const double locW = tr.vis ? 1.0 : fog[k].wts[loc];
                if (locW < 0.08) continue;
                const V2 orig = predBody(k, at, loc).pt();
                const double d = (myOld - orig).len();
                if (d > double(cf.range + 2300)) continue;
                const double travel = d / (0.80 * double(cf.dart_v));

                std::array<V2, 6> rdirs{};
                std::array<double, 6> rmass{};
                int rcnt = 0;
                for (int s = 0; s < 6; ++s) {
                    const V2 tgt = (s == 5 && N <= 4 && (loc == 0 || locW > 0.25))
                                   ? smartEnemyAim(orig, seen, travel)
                                   : aimStyleTarget(std::min(s, 4), seen, travel);
                    const V2 dir = (tgt - orig).unit();
                    if (dir.sq() < 0.5) continue;
                    int fi = 0;
                    while (fi < rcnt && rdirs[fi].dot(dir) < 0.9985) ++fi;
                    if (fi == rcnt) rdirs[rcnt++] = dir;
                    rmass[fi] += sw[s] / totW;
                }
                const V2 relOrig = orig + predBody(k, at, loc).vel() * (0.40 * double(cf.throw_pre));
                for (int ri = 0; ri < rcnt; ++ri) {
                    Dart dd;
                    dd.st = D_OUT;
                    dd.x = iround(relOrig.x);
                    dd.y = iround(relOrig.y);
                    dd.vx = iround(rdirs[ri].x * 1000.0);
                    dd.vy = iround(rdirs[ri].y * 1000.0);
                    dd.left = cf.range;
                    DartThreat th;
                    th.owner = k;
                    th.known = false;
                    th.signal = at + int(cf.delay) + 1;
                    th.weight = interest * locW * (hid ? 0.14 : 1.0) * timeW[u] * rmass[ri];
                    bool any = false;
                    for (int j = at + int(cf.throw_pre); j < NV_H && dd.st == D_OUT; ++j) {
                        const Seg seg = flyDart(dd, orig, orig, now + j);
                        const double reach = double(cf.run_v) * (j + 1) + double(cf.dash_t * (cf.dash_v - cf.run_v)) + hitRad + 550.0;
                        if (j >= 0 && seg_origin_sq(seg.a - here, seg.b - here) < reach * reach) {
                            th.segs[size_t(j)] = seg;
                            any = true;
                        }
                    }
                    if (any) threats.push_back(th);
                }
            }
        }
    }

    void addCrossfire(int k) {
        if (N < 3) return;
        const EnemyTrack& sh = tracks[k];
        if (!sh.active || sh.readyTick >= 5 || sh.distracted < 0.15) return;
        int first = std::max(-lag, sh.readyTick);
        if (sh.obsP.st != S_FREE) first = std::max(first, int(sh.obsP.timer) - lag);
        for (int q = 0; q < N; ++q) {
            if (q == k || q == seat || !tracks[q].active || gi->team[q] == gi->team[k]) continue;
            for (int at : {first, first + 4}) {
                if (at + cf.throw_pre >= NV_H) continue;
                const V2 orig = predBody(k, at, 0).pt();
                const V2 tgt = predBody(q, at + 3, 0).pt();
                const V2 v = tgt - orig;
                if (v.len() > double(cf.range + 1200) || (here - orig).dot(v) < 0.0) continue;
                const V2 u = v.unit();
                if (std::abs((here - orig).dot(u.perp())) > 3200.0) continue;
                Dart dd;
                dd.st = D_OUT;
                dd.x = iround(orig.x); dd.y = iround(orig.y);
                dd.vx = iround(u.x * 1000.0); dd.vy = iround(u.y * 1000.0);
                dd.left = cf.range;
                DartThreat th;
                th.owner = k;
                th.known = false;
                th.signal = at + int(cf.delay) + 1;
                th.weight = 0.20 * sh.distracted;
                bool any = false;
                for (int j = at + int(cf.throw_pre); j < NV_H && dd.st == D_OUT; ++j) {
                    const Seg seg = flyDart(dd, orig, orig, now + j);
                    if (j >= 0) { th.segs[size_t(j)] = seg; any = true; }
                }
                if (any) threats.push_back(th);
            }
        }
    }

    void addRushThreats(int k) {
        const EnemyTrack& tr = tracks[k];
        if (!tr.active || !tr.enemy || tr.dist > 7400.0) return;
        const Player& p = tr.obsP;
        const int dpre = int(cf.dash_pre), dtr = int(cf.dash_t), dpost = int(cf.dash_post);
        const int dtot = dpre + dtr + dpost;
        const bool knownDash = (p.st == S_DASH);

        for (int seed = 0; seed < (knownDash ? 1 : 3); ++seed) {
            int at = knownDash ? (-lag - (dtot - int(p.timer))) : (-lag + int(p.cd) + seed * 4);
            if (!knownDash && p.st != S_FREE) at = std::max(at, int(p.timer) - lag);
            if (at + dpre + 2 >= NV_H) continue;
            const int seen = std::clamp(now + at - int(cf.delay), 0, now);
            V2 orig = knownDash ? vpos(p) : ((NV_FOG_RUSH && !tr.vis) ? fog[k].pts[1] : predBody(k, at, 0).pt());
            if (knownDash) {
                const int moved = std::clamp(dtot - int(p.timer) - dpre, 0, dtr);
                orig = orig - V2{double(p.ax), double(p.ay)} * (double(moved * cf.dash_v) / 1000.0);
            }
            if (!knownDash && (orig - selfPosAt(seen)).len() > 5600.0) continue;

            for (int leadMode = 0; leadMode < (knownDash ? 1 : 2); ++leadMode) {
                const V2 dir = knownDash
                               ? V2{double(p.ax), double(p.ay)}.unit()
                               : (selfPosAt(seen) + vvel(selfHist[size_t(seen % NV_HIST)].me) * (leadMode ? 5.0 : 0.0) - orig).unit();
                if (dir.sq() < 0.5) continue;
                for (int moved = 1; moved < dtr; ++moved) {
                    const bool armAt = (tr.readyTick <= at + dpre + moved);
                    const int mpre = int(armAt ? cf.slash_pre : cf.kick_pre);
                    const int hit = at + dpre + moved + mpre - 1;
                    if (hit < 0 || hit >= NV_H) continue;
                    const V2 pt = orig + dir * double((moved + 1) * cf.dash_v);
                    if (isPit(pt, now + hit) || hasWall(orig, pt, now + hit)) continue;
                    const int sight = std::clamp(now + hit - mpre - int(cf.delay) + 1, 0, now);
                    const V2 aim = (selfPosAt(sight) - pt).unit();
                    double w = knownDash ? 1.0 : ((leadMode ? 0.38 : 0.62) * (seed == 0 ? 0.38 : 0.28));
                    if (!knownDash && hiddenAt(k, seen)) w *= 0.26;
                    if (!knownDash && !tr.vis && NV_FOG_RUSH) w *= 0.72;
                    rushes.push_back({pt, aim, dir, w, k, hit, armAt});
                }
            }
        }
    }

    // ---------------------------------------------------------------- Spatial Strategic Field
    void updateField() {
        const int f = phaseOf(now);
        const int ds = int(curObs->my_dart().st);
        if (focus >= 0 && hasWall(here, predBody(focus, 0, 0).pt(), now)) {
            blockedTicks = std::min(100, blockedTicks + 1);
        } else {
            blockedTicks = std::max(0, blockedTicks - 5);
        }
        flankNeed = clamp01((blockedTicks - 20.0) / 38.0);
        if (fieldPhase == f && fieldDartSt == ds && now - fieldTick < 6) return;
        fieldPhase = f;
        fieldDartSt = ds;
        fieldTick = now;

        const PhaseMap& z = phases[size_t(f)];
        const int nx = z.nx, num = z.N;
        field.assign(size_t(num), -100.0f);
        std::priority_queue<std::pair<float, int>> pq;

        const bool armed = (ds == D_HAND);
        // On narrow-core maps (skybridge/icefloes/twinlakes), start contesting permanent core earlier!
        const double leadStart = 75.0 + 45.0 * narrowCore;
        const double late = clamp01((double(now - cf.col_start) + leadStart) / 115.0);

        for (int i = 0; i < num; ++i) {
            if (!z.usable[i]) continue;
            const int x = i % nx, y = i / nx;
            const V2 pt{double(cell_center(x)), double(cell_center(y))};
            double q = -(0.24 + 0.68 * late) * double(z.coreDist[i]) / 1000.0 +
                       0.55 * std::min(2.0, double(z.room[i]) / 1000.0);

            const int dl = std::max(z.latestWalk[i], z.latestDash[i] - int(cf.dash_cd));
            if (dl < int(cf.steps + 100)) {
                const double margin = double(dl - now);
                if (margin < 48.0) q -= 14.0 * clamp01((48.0 - margin) / 40.0);
            }
            // Also respect the WALK-ONLY retreat deadline so we walk onto the permanent core with dash cd==0 intact!
            if (NV_WALK_SLACK > 0.0 && z.latestWalk[i] >= 0 && z.latestWalk[i] < int(cf.steps + 100)) {
                const double wMargin = double(z.latestWalk[i] - now);
                if (wMargin < 24.0) q -= NV_WALK_SLACK * (7.0 + 5.0 * narrowCore) * clamp01((24.0 - wMargin) / 20.0);
            }
            if (z.latestWalk[i] < 0) q -= 12.0;

            double d1 = 100.0, d2 = 100.0;
            for (int k = 0; k < N; ++k) {
                if (!tracks[k].active || !tracks[k].enemy) continue;
                const double d = (pt - predBody(k, 2, 0).pt()).len() / 1000.0;
                if (d < d1) { d2 = d1; d1 = d; }
                else d2 = std::min(d2, d);
                if (tracks[k].readyTick < 7) {
                    q -= (armed ? 0.72 : 1.50) * std::max(0.0, 4.1 - d);
                }
            }
            if (d1 < 90.0) {
                q -= (armed ? 0.44 : 0.16) * std::max(0.0, d1 - 5.0);
                if (d1 > double(cf.see_r) / 1000.0 + 0.2) {
                    q += double(z.grassScore[i]) * (armed ? 4.8 : 2.3);
                }
            }
            if (focus >= 0 && d1 < 9.2 && flankNeed > 0.10) {
                const bool blk = hasWall(pt, predBody(focus, 2, 0).pt(), now);
                if (armed && !blk) q += 4.6 * flankNeed * clamp01((8.5 - d1) / 3.0);
                if (!armed && blk && tracks[focus].readyTick < 6) q += 3.2 * flankNeed * clamp01((8.6 - d1) / 3.0);
            }
            if ((ds == D_GROUND || ds == D_RISE) && dartGuardedByEnemy && focus >= 0) {
                if (hasWall(pt, predBody(focus, 2, 0).pt(), now) && d1 > 2.4 && d1 < 8.0) q += 3.5;
            }
            if (aliveEnemies > 1) q -= 0.95 * std::max(0.0, 5.6 - d2);
            if (ds == D_GROUND || ds == D_RISE) {
                q -= recoveryUrg * 0.72 * std::min(6.5, (bestPickSpot - pt).len() / 1000.0);
            }
            field[i] = float(q);
            pq.push({float(q), i});
        }

        while (!pq.empty()) {
            const auto [val, u] = pq.top();
            pq.pop();
            if (val != field[u]) continue;
            for (const auto& e : z.edges[u]) {
                const float cand = val - e.cost * (e.dash ? 0.60f : 0.52f);
                if (cand > field[e.to]) {
                    field[e.to] = cand;
                    pq.push({cand, e.to});
                }
            }
        }
        for (int i = 0; i < num; ++i) {
            if (z.usable[i]) continue;
            float best = -99.0f;
            const int cx = i % nx, cy = i / nx;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int gx = cx + dx, gy = cy + dy;
                    if (z.m.inside(gx, gy) && z.usable[gy * nx + gx]) {
                        best = std::max(best, field[gy * nx + gx]);
                    }
                }
            }
            field[i] = best - 0.7f;
        }
    }

    // ---------------------------------------------------------------- Offensive Aiming Engine
    void buildEvasions(int k) {
        EnemyTrack& tr = tracks[k];
        const int react = std::min(NV_FUT - 1, int(cf.delay) + 1);
        for (int m = 0; m < NV_MODES; ++m) {
            const V2 side = (tr.models[m].fut[size_t(react)].pt() - here).unit().perp();
            for (int opt = 0; opt < 4; ++opt) {
                Body b = tr.models[m].fut[size_t(react)];
                if (b.st == S_RECALL) { b.st = S_FREE; b.timer = 0; }
                auto& ev = tr.evade[m][opt];
                const V2 mv = (opt & 1) ? side : (side * -1.0);
                ev[size_t(react)] = b.pt();
                bool canDash = (opt >= 2) && (tr.obsP.cd <= lag + react);
                for (int j = react; j < NV_FUT; ++j) {
                    if (canDash && b.st == S_FREE) {
                        beginAction(b, A_DASH, mv, tr.models[m].hand[size_t(j)]);
                        canDash = false;
                    }
                    predictBodyStep(b, mv, now + j);
                    ev[size_t(j + 1)] = b.pt();
                }
            }
        }
    }

    struct RayHitStats { double overall = 0.0; double early = 0.0; double parryRisk = 0.0; };

    RayHitStats evalRayOnEnemy(int k, V2 orig, V2 aim, double cutoff) const {
        const EnemyTrack& tr = tracks[k];
        double tot = 0.0, totEarly = 0.0, totParry = 0.0;
        const int tStart = int(cf.throw_pre);
        const int react = int(cf.delay) + 1;
        const double padR = hitRad + 160.0;
        const double padR2 = padR * padR;

        auto contact = [&](V2 a, V2 b) -> double {
            const double d2 = seg_origin_sq(a, b);
            if (d2 >= padR2) return 0.0;
            return clamp01((padR - std::sqrt(d2)) / 320.0);
        };

        for (int m = 0; m < NV_MODES; ++m) {
            const EnemyModel& em = tr.models[m];
            double nat = 0.0, ear = 0.0, parry = 0.0;
            std::array<double, 4> evHit{};
            for (int j = tStart; j < NV_FUT; ++j) {
                const double a = dartFlight[size_t(j)];
                const double b = std::min(cutoff, dartFlight[size_t(j + 1)]);
                if (a > cutoff || b < a || a >= double(cf.range)) break;
                const V2 p0 = orig + aim * a, p1 = orig + aim * b;
                const Body& bp = em.fut[size_t(j)];
                const double cov = contact(p0 - bp.pt(), p1 - em.fut[size_t(j + 1)].pt());
                nat = std::max(nat, cov);

                const bool enemyLocked = (bp.st == S_STUN || bp.st == S_SLASHED || bp.st == S_KICKED ||
                                          (bp.st == S_DASH && bp.timer <= cf.dash_post + 1) ||
                                          (tr.inferredSlashEnd > now + j));
                if (j < react || enemyLocked) {
                    ear = std::max(ear, cov);
                } else {
                    for (int u = 0; u < 4; ++u) {
                        const auto& ep = tr.evade[m][u];
                        evHit[u] = std::max(evHit[u], contact(p0 - ep[size_t(j)], p1 - ep[size_t(j + 1)]));
                    }
                    const int canStart = j + 1 - int(cf.slash_pre);
                    if (cf.block && canStart >= react && em.hand[size_t(canStart)] && em.fut[size_t(canStart)].st == S_FREE &&
                        tr.inferredSlashEnd <= now + canStart) {
                        const double stepBudget = double(std::min(2, canStart - react)) * double(cf.run_v);
                        const double sep = (p0 - em.fut[size_t(canStart)].pt()).len();
                        const double effReach = double(cf.slash_reach) + stepBudget;
                        if (sep <= effReach + 120.0 && !hasWall(p0, em.fut[size_t(canStart)].pt(), now + j)) {
                            parry = std::max(parry, clamp01((effReach + 120.0 - sep) / 380.0));
                        }
                    }
                }
            }
            const double avgEv = 0.25 * (evHit[0] + evHit[1] + evHit[2] + evHit[3]);
            const double minEv = std::min({evHit[0], evHit[1], evHit[2], evHit[3]});
            const double distFac = tr.distracted * (aliveEnemies > 1 ? 0.34 : 0.24);
            const double parryScale = (learn[k].parriesSeen > 0) ? 0.84 : (aliveEnemies > 1 ? 0.46 : 0.72);
            const double effParry = std::max(0.0, parryScale - (aliveEnemies > 1 ? 0.32 : 0.20) * tr.distracted);
            const double lateProb = ((0.42 + distFac) * nat + (0.30 - 0.35 * distFac) * avgEv + (0.28 - 0.55 * distFac) * minEv) *
                                    (1.0 - effParry * parry);
            tot += em.w * (tr.enemy ? std::max(ear, lateProb) : std::max(nat, avgEv));
            totEarly += em.w * ear;
            totParry += em.w * std::max(0.0, parry * (1.0 - ear));
        }
        return {tot, totEarly, totParry};
    }

    ShotEval computeBestShot(V2 orig) const {
        ShotEval best;
        for (int k = 0; k < N; ++k) {
            const EnemyTrack& tr = tracks[k];
            if (!tr.active || !tr.enemy || tr.dist > double(cf.range + 3400) || tr.conf < 0.18) continue;

            std::array<V2, 30> candDirs{};
            int cnt = 0;
            auto pushDir = [&](V2 d) {
                d = d.unit();
                if (d.sq() < 0.5) return;
                for (int i = 0; i < cnt; ++i) {
                    if (candDirs[i].dot(d) > 0.9998) return;
                }
                if (cnt < int(candDirs.size())) candDirs[cnt++] = d;
            };

            V2 avgDir{};
            for (int m = 0; m < NV_MODES; ++m) {
                const EnemyModel& em = tr.models[m];
                double bestErr = 1e30;
                V2 bvec{};
                for (int j = int(cf.throw_pre) + 1; j <= NV_FUT && dartFlight[size_t(j - 1)] < double(cf.range); ++j) {
                    const V2 delta = em.fut[size_t(j)].pt() - orig;
                    const double err = std::abs(dartFlight[size_t(j)] - delta.len());
                    if (err < bestErr) { bestErr = err; bvec = delta; }
                }
                pushDir(bvec);
                avgDir = avgDir + bvec.unit() * em.w;
            }
            pushDir(avgDir);
            pushDir(tr.models[0].fut[0].pt() - orig);
            pushDir(avgDir + avgDir.perp() * 0.08);
            pushDir(avgDir - avgDir.perp() * 0.08);
            for (int m : {0, 1, 2}) {
                for (int u = 0; u < 2; ++u) {
                    double bestErr = 1e30;
                    V2 bvec{};
                    for (int j = int(cf.delay) + 2; j <= NV_FUT && dartFlight[size_t(j - 1)] < double(cf.range); ++j) {
                        const V2 delta = tr.evade[m][u][size_t(j)] - orig;
                        const double err = std::abs(delta.len() - dartFlight[size_t(j)]);
                        if (err < bestErr) { bestErr = err; bvec = delta; }
                    }
                    pushDir(bvec);
                }
            }

            for (int i = 0; i < cnt; ++i) {
                const V2 aim = candDirs[i];
                const V2 endPt = orig + aim * double(cf.range);
                i64 hx = 0, hy = 0;
                double cutoff = double(cf.range);
                if (seg_wall(pmap(now).m, iround(orig.x), iround(orig.y), iround(endPt.x), iround(endPt.y), hx, hy)) {
                    cutoff = std::min(cutoff, (V2{double(hx), double(hy)} - orig).len());
                }
                if (cutoff < 150.0) continue;

                const RayHitStats st = evalRayOnEnemy(k, orig, aim, cutoff);
                double score = st.overall * tr.conf;

                // Getting parried strands our dart right at the enemy's feet -> negative EV!
                // In 4P FFA, scale down parry cost on heavily distracted opponents while still refusing free parries to armed watchers!
                if (aliveEnemies == 1) {
                    const double parryCost = 0.22 * (learn[k].parriesSeen > 0 ? 1.35 : 1.0);
                    score -= parryCost * st.parryRisk;
                } else if (NV_FFA_PARRY_COST > 0.0) {
                    const double parryCost = NV_FFA_PARRY_COST * (1.0 - 0.85 * tr.distracted) * (learn[k].parriesSeen > 0 ? 1.35 : 1.0);
                    score -= parryCost * st.parryRisk;
                }

                for (int q = 0; q < N; ++q) {
                    if (q == k || !tracks[q].active || tracks[q].dist > double(cf.range + 2000)) continue;
                    const V2 rel = predBody(q, 0, 0).pt() - orig;
                    const double along = rel.dot(aim);
                    if (along < -500.0 || along > cutoff + 1800.0 || std::abs(rel.dot(aim.perp())) > 3200.0) continue;
                    const RayHitStats sq = evalRayOnEnemy(q, orig, aim, cutoff);
                    score += sq.overall * (tracks[q].enemy ? 0.34 : -2.0) * tracks[q].conf;
                }

                if (cutoff < double(cf.range) - 10.0 && cutoff > double(cf.pick_r) + 250.0) {
                    double dropPen = 0.045;
                    const V2 dropPt = orig + aim * cutoff;
                    const double enemyToDrop = (predBody(k, 4, 0).pt() - dropPt).len();
                    if (enemyToDrop + 600.0 < cutoff) dropPen += 0.040;
                    score -= dropPen;
                }
                if (score > best.ev) {
                    best = {aim, score, st.overall, st.early, st.parryRisk, k, cutoff};
                }
            }
        }
        return best;
    }

    // ---------------------------------------------------------------- Position & Trajectory Evaluation
    double evalPosition(V2 q, int dt, bool willHoldDart) const {
        const PhaseMap& z = pmap(now + dt);
        const double room = z.sample(z.room, q) / 1000.0;
        const double core = z.sample(z.coreDist, q) / 1000.0;
        const double leadStart = 75.0 + 45.0 * narrowCore;
        const double late = clamp01((double(now - cf.col_start) + leadStart) / 110.0);

        double v = -(0.72 + 0.96 * late) * core + 0.92 * std::min(1.75, room);
        v += (1.0 + 2.3 * flankNeed) * phases[size_t(fieldPhase)].sample(field, q);
        if (room < 0.40) v -= 1.8 * (0.40 - room);

        const int cellIdx = z.clampIdx(q);
        const int gx = cellIdx % z.nx, gy = cellIdx / z.nx;
        const i64 fall = wave_fall_step(cf, gi->M().wave(gx, gy));
        if (fall >= 0) {
            v -= 18.0 * clamp01(1.0 - double(fall - now - dt) / 38.0);
        }
        const int effCd = std::max(0, int(curObs->me().cd) - dt);
        const int dl = std::max(z.latestWalk[cellIdx], z.latestDash[cellIdx] - effCd);
        if (dl < int(cf.steps + 100)) {
            const double slack = double(dl - (now + dt));
            if (slack < 30.0) v -= 22.0 * clamp01((30.0 - slack) / 28.0);
        }
        // Separate walk-only retreat deadline: never miss the walk bridge and get forced to burn dash into an enemy's face!
        if (NV_WALK_SLACK > 0.0 && z.latestWalk[cellIdx] >= 0 && z.latestWalk[cellIdx] < int(cf.steps + 100)) {
            const double wSlack = double(z.latestWalk[cellIdx] - (now + dt));
            if (wSlack < 20.0) v -= NV_WALK_SLACK * (8.0 + 7.0 * narrowCore) * clamp01((20.0 - wSlack) / 16.0);
        }
        if (z.latestWalk[cellIdx] < 0) v -= 14.0;

        const bool armed = (curObs->my_dart().st == D_HAND) || willHoldDart;
        double d1 = 100.0, d2 = 100.0, dFocus = 0.0;
        for (int k = 0; k < N; ++k) {
            if (!tracks[k].active) continue;
            const V2 en = predBody(k, dt, 0).pt();
            const double dist = (q - en).len() / 1000.0;
            if (!tracks[k].enemy) {
                v -= 1.4 * std::max(0.0, 1.8 - dist);
                continue;
            }
            if (dist < d1) { d2 = d1; d1 = dist; }
            else d2 = std::min(d2, dist);
            if (k == focus) dFocus = dist;

            if (tracks[k].readyTick <= dt + 1) {
                const bool los = !hasWall(q, en, now + dt);
                if (armed) {
                    v -= 1.50 * std::max(0.0, 2.65 - dist);
                } else {
                    const double mult = los ? 1.0 : 0.45;
                    v -= mult * 3.15 * std::max(0.0, 4.85 - dist);
                    if (los && dist < 3.40) v -= 3.2 * (3.40 - dist);
                }
                // Extra guard against walking point-blank into an armed grass-camper's closest ambush cell!
                if (!tracks[k].vis) {
                    const double dAmbush = (q - fog[k].pts[1]).len() / 1000.0;
                    if (dAmbush < 3.85) {
                        v -= (armed ? 1.8 : 3.2) * (3.85 - dAmbush);
                    }
                }
            } else if (!armed && !winPickupRace && tracks[k].obsP.cd <= lag + dt + 4 &&
                       dist < 4.35 && !hasWall(q, en, now + dt)) {
                const double kFat = kickThreatFatal(q, en, now + dt, true);
                const bool kCombo = (tracks[k].readyTick <= dt + cf.kick_stun);
                if (kFat > 0.0 || kCombo) {
                    const double mult = (kFat > 0.0) ? (1.15 + 0.55 * kFat) : 1.0;
                    v -= 2.35 * mult * (4.35 - dist);
                    if (dist < 3.35) v -= 2.9 * mult * (3.35 - dist);
                }
            }
            if (armed && k == focus && (curObs->d[k].st == D_GROUND || curObs->d[k].st == D_RISE) && dist < 8.5) {
                const V2 item = vpos(curObs->d[k]);
                const double enD = (en - item).len() / 1000.0;
                const double myD = (q - item).len() / 1000.0;
                const double open = (!hasWall(q, item, now + dt) && !hasWall(q, en, now + dt)) ? 1.0 : 0.40;
                const double guard = clamp01((enD - 1.0) / 2.4) * clamp01((7.5 - enD) / 2.2);
                v += (N == 2 ? 3.8 : 1.7) * open * guard * clamp01((enD + 0.7 - myD) / 2.2);
                if (curObs->d[k].st == D_RISE && myD < 1.35 && dist > 1.8) v += 2.8;
            }
            if (tracks[k].readyTick > dt - 1 && dist < 5.3) {
                const bool enDash = (tracks[k].obsP.cd <= lag + dt + 3);
                const double kFat = kickThreatFatal(q, en, now + dt, enDash);
                const double nearK = clamp01((5.3 - dist) / 2.9);
                const double dashBoost = enDash ? 1.38 : 1.0;
                const double myCdBoost = (curObs->me().cd > dt) ? 1.40 : 1.0;
                const double kCost = (N == 2 ? (4.6 + 9.4 * late) : (3.4 + 7.2 * late)) *
                                     (1.0 + 0.50 * narrowCore) * dashBoost * myCdBoost;
                if (kFat > 0.0) v -= kCost * kFat * nearK;
                if (!armed && dist < 4.7 && kickSlideFatal(en, en - q, now + dt)) {
                    v += (2.4 + 3.0 * late) * nearK;
                }
            }
        }

        if (focus >= 0) {
            const double ideal = (tracks[focus].readyTick < dt + 3) ? 4.2 : (2.3 - 0.9 * pressure);
            if (armed) {
                v -= (aliveEnemies > 1 ? (0.65 + 0.90 * pressure) : (1.30 + 2.35 * pressure)) * std::max(0.0, dFocus - ideal);
            } else if (curObs->my_dart().st != D_GROUND && curObs->my_dart().st != D_RISE) {
                v -= 0.35 * std::max(0.0, dFocus - 6.0);
            }
        }
        if (aliveEnemies > 1) {
            v -= 1.75 * std::max(0.0, 6.0 - d2);
            // FFA collinear crossfire avoidance: stay off the direct firing line between two fighting enemies
            for (int a = 0; a < N; ++a) {
                if (!tracks[a].active || !tracks[a].enemy) continue;
                const V2 pa = predBody(a, dt, 0).pt();
                for (int b = a + 1; b < N; ++b) {
                    if (!tracks[b].active || !tracks[b].enemy || gi->team[a] == gi->team[b]) continue;
                    const V2 pb = predBody(b, dt, 0).pt();
                    const V2 ab = pb - pa;
                    const double abLen = ab.len();
                    if (abLen < 1200.0 || abLen > 8800.0) continue;
                    const double tProj = (q - pa).dot(ab) / (abLen * abLen);
                    if (tProj > 0.12 && tProj < 0.88) {
                        const double perp = std::abs((q - pa).dot(ab.unit().perp()));
                        if (perp < 1650.0) {
                            v -= 2.4 * clamp01((1650.0 - perp) / 1650.0);
                        }
                    }
                }
            }
        }

        const int ctype = cell_of_point(cf, gi->M(), iround(q.x), iround(q.y), now + dt);
        if (ctype == C_GRASS && d1 > double(cf.see_r) / 1000.0) {
            v += armed ? 1.80 : 0.65;
            if (curObs->me().expose <= dt && armed) v += 0.85;
        }
        const Dart& md = curObs->my_dart();
        if (!willHoldDart && (md.st == D_GROUND || md.st == D_RISE)) {
            const double dd = (bestPickSpot - q).len() / 1000.0;
            v -= recoveryUrg * (winPickupRace ? 2.15 : 1.50) * std::min(6.2, dd);
            if (dartGuardedByEnemy && focus >= 0 && hasWall(q, predBody(focus, dt, 0).pt(), now + dt) && dFocus > 2.2) {
                v += 2.6;
            }
        }
        return v;
    }

    SelfPath simulateSelf(const Candidate& c) const {
        SelfPath sp;
        Body b(curObs->me());
        const bool hand0 = (curObs->my_dart().st == D_HAND);
        if (b.st == S_RECALL && c.act != A_RECALL && (c.move.sq() > 0.0 || c.act != A_NONE)) {
            b.st = S_FREE;
            b.timer = 0;
        }
        beginAction(b, c.act, c.aim, hand0);
        V2 mv = c.move;
        sp.p[0] = here;
        sp.st[0] = b.st;
        sp.cd[0] = b.cd;
        for (int j = 0; j < NV_H; ++j) {
            if (c.turnAt == j) mv = c.after;
            if (c.meleeAt == j) beginAction(b, A_MELEE, {1.0, 0.0}, hand0);
            Body old = b;
            if (!advanceBody(b, mv, now + j)) {
                const bool locked = (old.st != S_FREE && old.st != S_THROW && old.st != S_THROWN);
                if (j == 0 || locked) {
                    sp.pitPen = std::max(sp.pitPen, 300.0 - 4.0 * j);
                    sp.dead = true;
                } else {
                    sp.pitPen = std::max(sp.pitPen, std::max(0.0, (6.5 - j) * 0.80));
                }
                b = old;
                mv = {};
                if (!advanceBody(b, mv, now + j)) {
                    b.x = old.x; b.y = old.y; b.vx = b.vy = 0;
                }
            }
            sp.p[size_t(j + 1)] = b.pt();
            sp.st[size_t(j + 1)] = b.st;
            sp.cd[size_t(j + 1)] = b.cd;
            if (curObs->my_dart().st == D_GROUND && sp.pickTick > j &&
                (b.pt() - vpos(curObs->my_dart())).sq() <= double(cf.pick_r * cf.pick_r)) {
                sp.pickTick = j;
            }
        }
        return sp;
    }

    V2 bestMeleeAim(const SelfPath& sp, int hit) const {
        if (focus < 0) return {1.0, 0.0};
        V2 bestSum{};
        double bestW = -1.0;
        for (int k = 0; k < N; ++k) {
            if (!tracks[k].active || !tracks[k].enemy) continue;
            V2 v{};
            double w = 0.0;
            for (const auto& em : tracks[k].models) {
                const V2 d = em.fut[size_t(hit)].pt() - sp.p[size_t(hit)];
                const double fac = em.w * clamp01((double(cf.slash_reach + 700) - d.len()) / 750.0);
                v = v + d.unit() * fac;
                w += fac;
            }
            w *= tracks[k].conf;
            if (w > bestW) { bestW = w; bestSum = v; }
        }
        const V2 u = bestSum.unit();
        return u.sq() > 0.5 ? u : (predBody(focus, 0, 0).pt() - here).unit();
    }

    double evalMeleeOffense(const SelfPath& sp, V2 aim, int hit, bool armed) const {
        if (hit < 1 || hit > NV_H) return 0.0;
        const V2 myPt = sp.p[size_t(hit)];
        const double reach = double(armed ? cf.slash_reach : cf.kick_reach);
        const double cosLim = double(armed ? cf.slash_cos : cf.kick_cos) / 1000.0;
        const double late = clamp01((double(now - cf.col_start) + 10.0) / 90.0);
        double total = 0.0;
        for (int k = 0; k < N; ++k) {
            const EnemyTrack& tr = tracks[k];
            if (!tr.active || tr.dist > 7400.0) continue;
            double sc = 0.0;
            for (int m = 0; m < NV_MODES; ++m) {
                const Body& eb = tr.models[m].fut[size_t(hit)];
                const V2 d = eb.pt() - myPt;
                const double len = d.len();
                if (len > reach + 250.0) continue;
                if (!armed && kickProtected(tr.models[m].fut[size_t(hit - 1)])) continue;
                if (len > double(cf.body_r) && d.dot(aim) < len * cosLim) continue;
                if (hasWall(myPt, eb.pt(), now + hit - 1)) continue;
                double val = armed ? (34.0 + 14.0 * pressure) : (12.5 + 5.5 * late);
                if (!armed) {
                    if (kickSlideFatal(eb.pt(), d, now + hit)) val += 20.0 + 8.0 * late;
                    if (eb.st == S_DASH || eb.st == S_RECALL || eb.st == S_SLASHED || eb.st == S_KICKED) val += 2.8;
                }
                sc += val * clamp01((reach + 210.0 - len) / 420.0) * tr.models[m].w;
            }
            total += sc * (tr.enemy ? tr.conf : -2.0);
        }
        return total;
    }

    // Exact multi-angle anti-counter-kick evaluator when our dash lands (or is on cooldown) near a cliff or re-arming kicker!
    double evalCounterKickOnLanding(const SelfPath& sp, int checkT, bool isDash) const {
        double pen = 0.0;
        const int ct = std::clamp(checkT, 1, NV_H);
        const V2 myPt = sp.p[size_t(ct)];
        for (int k = 0; k < N; ++k) {
            const EnemyTrack& tr = tracks[k];
            if (!tr.active || !tr.enemy) continue;
            if (tr.readyTick <= ct - 2) continue;
            const V2 en = predBody(k, ct, 0).pt();
            const double d = (myPt - en).len();
            const bool enDash = (tr.obsP.cd <= lag + ct + 1);
            const double maxReach = enDash ? 4650.0 : 2150.0;
            if (d > maxReach || hasWall(en, myPt, now + ct)) continue;
            const double kFat = kickThreatFatal(myPt, en, now + ct, enDash);
            const bool fatalCombo = (tr.readyTick <= ct + cf.kick_stun);
            if (kFat > 0.0 || fatalCombo) {
                const double prox = clamp01((maxReach - d) / 2700.0);
                const double pitBase = (isDash ? 38.0 : 21.0) * kFat;
                const double cmbBase = fatalCombo ? (isDash ? 21.0 : 10.0) : 0.0;
                pen = std::max(pen, std::max(pitBase, cmbBase) * (0.45 + 0.55 * prox) * tr.conf);
            }
        }
        return pen;
    }

    double evalCandidate(const Candidate& c, const ShotEval& shot) const {
        ++workCount;
        const SelfPath sp = simulateSelf(c);
        double val = -sp.pitPen;
        val += 0.20 * evalPosition(sp.p[3], 3, sp.pickTick < 3) +
               0.38 * evalPosition(sp.p[7], 7, sp.pickTick < 7) +
               0.42 * evalPosition(sp.p[NV_H], NV_H, sp.pickTick < NV_H);

        const bool armed = (curObs->my_dart().st == D_HAND);
        if (sp.pickTick < 99 && c.act != A_RECALL) {
            val += (winPickupRace ? 11.5 : 5.2) - 0.28 * sp.pickTick;
        }

        const bool strike = (c.act == A_MELEE || c.meleeAt >= 0);
        const bool isSlash = strike && armed;
        const int hit = int(armed ? cf.slash_pre : cf.kick_pre) + std::max(0, c.meleeAt);
        V2 strikeAim = (c.meleeAt >= 0) ? bestMeleeAim(sp, hit) : c.aim;
        if (c.parryThreat >= 0 && hit >= 1 && hit <= NV_H) {
            strikeAim = (threats[size_t(c.parryThreat)].segs[size_t(hit - 1)].a - sp.p[size_t(hit)]).unit();
        }
        double strikeOffVal = 0.0;
        if (strike) {
            strikeOffVal = evalMeleeOffense(sp, strikeAim, hit, armed);
            const double disc = 1.0 / (1.0 + 0.08 * std::max(0, c.meleeAt));
            val += disc * strikeOffVal - (armed ? 3.9 : 3.8);
        }

        if (c.act == A_THROW) {
            const bool ffaMulti = (aliveEnemies > 1);
            double need = ffaMulti ? (0.235 + NV_THROW_BIAS_FFA) : (0.34 + NV_THROW_BIAS_1V1);
            if (shot.target >= 0) {
                const EnemyTrack& tgt = tracks[shot.target];
                const bool enemyDashing = (tgt.obsP.st == S_DASH);
                const bool enemyLocked = (tgt.obsP.st == S_STUN || tgt.obsP.st == S_SLASHED ||
                                          tgt.obsP.st == S_KICKED || enemyDashing || tgt.inferredSlashEnd > now);
                if (enemyLocked) {
                    need -= enemyDashing ? 0.18 : 0.22;
                } else if (tgt.readyTick > 5) {
                    if (ffaMulti) {
                        need -= (NV_FFA_AGGRO >= 2 ? 0.135 : 0.110);
                    } else {
                        // In 1v1, we ALREADY hold a huge weapon advantage over an unarmed opponent!
                        // Punish unreactable close shots, NEVER waste our dart on a >3.6m low-earlyHit Hail Mary!
                        if (shot.earlyHit >= 0.44) {
                            need -= 0.14;
                        } else if (tgt.dist > 3600.0 && shot.earlyHit < 0.32) {
                            need += 0.16;
                            if (tgt.obsP.cd <= lag + 5) need += 0.10;
                            if ((curObs->d[shot.target].st == D_GROUND || curObs->d[shot.target].st == D_RISE) &&
                                (vpos(curObs->d[shot.target]) - here).len() < (vpos(curObs->d[shot.target]) - predBody(shot.target, 0, 0).pt()).len() + 400.0) {
                                need += 0.09; // We control their grounded dart — press & camp, don't throw it away!
                            }
                        } else {
                            need -= 0.03;
                        }
                    }
                } else {
                    need += (ffaMulti ? 0.18 * (1.0 - 0.50 * tgt.distracted) : 0.32) * clamp01((tgt.dist - 3200.0) / 1650.0);
                    if (learn[shot.target].parriesSeen > 0) {
                        need += (ffaMulti ? 0.12 * (1.0 - 0.55 * tgt.distracted) : 0.22) * clamp01((tgt.dist - 2950.0) / 1450.0);
                    }
                }
                need -= (ffaMulti ? 0.125 : 0.095) * tgt.distracted;
                if (!enemyLocked) need += 0.08 * clamp01(double(now - cf.col_start) / 95.0);
                if (shot.earlyHit > 0.48) need -= 0.16;
                if (!ffaMulti && learn[shot.target].stillStreak > 22 && now > 200 && shot.parryRisk < 0.25) {
                    need -= 0.09;
                }
                if (!ffaMulti && now > 220 && learn[shot.target].parriesSeen == 0 && learn[shot.target].throwsSeen == 0 &&
                    tgt.dist < 4400.0 && shot.parryRisk < 0.30) {
                    need -= 0.04;
                }
            }
            if (ffaMulti) {
                int armedWatchers = 0;
                for (int k = 0; k < N; ++k) {
                    if (tracks[k].active && tracks[k].enemy && tracks[k].readyTick <= 3 &&
                        tracks[k].dist < 6500.0 && !hasWall(here, predBody(k, 0, 0).pt(), now)) {
                        ++armedWatchers;
                    }
                }
                if (armedWatchers >= 2) need += 0.06;
            }
            val += (ffaMulti ? NV_FFA_THROW_SCALE : 25.5) * (shot.ev - (ffaMulti ? 1.22 : 1.32) * need);
        }

        if (c.act == A_RECALL) {
            const V2 dPt = vpos(curObs->my_dart());
            const double dd = (dPt - here).len() / 1000.0;
            const bool behindWall = (focus >= 0 && hasWall(here, predBody(focus, 0, 0).pt(), now));
            const int remRec = (curObs->me().st == S_RECALL) ? int(curObs->me().timer) : int(cf.recall_pre);

            // Check if ANY armed enemy is camping our grounded dart (can parry our recall regardless of behindWall!)
            bool dartCamped = false;
            for (int k = 0; k < N; ++k) {
                if (!tracks[k].active || !tracks[k].enemy) continue;
                if (tracks[k].readyTick > remRec - 1) continue;
                const V2 enFuture = predBody(k, std::min(NV_FUT, remRec), 0).pt();
                const V2 enNow = predBody(k, 0, 0).pt();
                const double dNow = (enNow - dPt).len();
                const double dFut = (enFuture - dPt).len();
                const double reachBound = double(cf.slash_reach) + std::min(1100.0, 0.45 * double(remRec * cf.run_v));
                if ((dNow < double(cf.slash_reach + 350) || dFut < reachBound) &&
                    !hasWall(enNow, dPt, now + remRec)) {
                    dartCamped = true;
                    break;
                }
            }
            if (dartCamped) {
                val -= (now - lastOwnBlockedTick < 24) ? 24.0 : 15.0;
            } else {
                val += std::min(7.2, 1.40 + 0.90 * dd) + 2.2 * (1.0 - recoveryUrg);
                if (behindWall && dartGuardedByEnemy) val += 5.5;
                if (curObs->me().st == S_RECALL) val += double(cf.recall_pre - curObs->me().timer) * 0.65;
            }
            if (focus >= 0 && tracks[focus].armedNow && nearestDist < 6500.0 && !behindWall) val -= 2.4;
        }

        // Simulate our own dart (returning curve-kills, early catches, clashes, anti-parry curve!)
        std::array<Seg, NV_H> ownSegs{};
        {
            Dart md = curObs->my_dart();
            int start = 0;
            if (c.act == A_THROW || curObs->me().st == S_THROW) {
                start = int(c.act == A_THROW ? cf.throw_pre : curObs->me().timer);
                md.x = iround(sp.p[size_t(start)].x);
                md.y = iround(sp.p[size_t(start)].y);
                md.st = D_OUT; md.left = cf.range; md.age = 0;
                const V2 a = (c.act == A_THROW) ? c.aim : V2{double(curObs->me().ax), double(curObs->me().ay)} * 0.001;
                md.vx = iround(a.x * 1000.0);
                md.vy = iround(a.y * 1000.0);
            } else if (c.act == A_RECALL) {
                start = int((curObs->me().st == S_RECALL ? curObs->me().timer : cf.recall_pre) - 1);
                md.st = D_CALL; md.age = 0;
            }
            std::array<double, MAXP> retHit{};
            double retParryPen = 0.0;
            for (int j = start; j < NV_H && dart_flying(md.st); ++j) {
                const bool back = (md.st == D_BACK || md.st == D_CALL);
                ownSegs[size_t(j)] = flyDart(md, sp.p[size_t(j + 1)], sp.p[size_t(j)], now + j);
                if (back) {
                    for (int k = 0; k < N; ++k) {
                        if (!tracks[k].active) continue;
                        double hp = 0.0;
                        for (int m : {0, 1, 2, 5, 6}) {
                            const EnemyModel& em = tracks[k].models[m];
                            const double d2 = seg_origin_sq(ownSegs[size_t(j)].a - em.fut[size_t(j)].pt(),
                                                            ownSegs[size_t(j)].b - em.fut[size_t(j + 1)].pt());
                            if (d2 < (hitRad + 310.0) * (hitRad + 310.0)) {
                                hp += em.w * clamp01((hitRad + 310.0 - std::sqrt(d2)) / 450.0);
                            }
                        }
                        retHit[k] = std::max(retHit[k], hp / (1.0 + 0.04 * j));
                        if (tracks[k].enemy && tracks[k].models[0].hand[size_t(j)] &&
                            tracks[k].models[0].fut[size_t(j)].st == S_FREE) {
                            const V2 enPt = tracks[k].models[0].fut[size_t(j)].pt();
                            const double ed = (ownSegs[size_t(j)].a - enPt).len();
                            if (ed < double(cf.slash_reach + 200) && !hasWall(ownSegs[size_t(j)].a, enPt, now + j)) {
                                retParryPen = std::max(retParryPen, 8.5 * clamp01((double(cf.slash_reach + 200) - ed) / 680.0));
                            }
                        }
                    }
                    if (md.st == D_HAND) val += 1.9 * (1.0 - double(j) / double(NV_H));
                }
            }
            for (int k = 0; k < N; ++k) {
                val += retHit[k] * (tracks[k].enemy ? 13.5 : -26.0);
            }
            val -= retParryPen;
        }

        // Precompute enemy kill/interrupt probabilities from our own action
        std::array<std::array<double, NV_H + 1>, MAXP> killed{};
        std::array<double, MAXP> kicked{};
        if (strike || dart_flying(curObs->my_dart().st) || c.act == A_THROW || c.act == A_RECALL) {
            for (int k = 0; k < N; ++k) {
                const EnemyTrack& tr = tracks[k];
                if (!tr.active || !tr.enemy || tr.dist > double(cf.range + 2000)) continue;
                for (const auto& em : tr.models) {
                    double prevK = 0.0;
                    for (int j = 0; j < NV_H; ++j) {
                        double ch = 0.0;
                        if (ownSegs[size_t(j)].on) {
                            const double d2 = seg_origin_sq(ownSegs[size_t(j)].a - em.fut[size_t(j)].pt(),
                                                            ownSegs[size_t(j)].b - em.fut[size_t(j + 1)].pt());
                            if (d2 < (hitRad + 100.0) * (hitRad + 100.0)) {
                                const double rf = (j <= cf.delay || em.fut[size_t(j)].st == S_STUN) ? 0.92 : 0.48;
                                ch = rf * clamp01((hitRad + 100.0 - std::sqrt(d2)) / 260.0);
                            }
                        }
                        if (isSlash && j + 1 == hit) {
                            const V2 d = em.fut[size_t(hit)].pt() - sp.p[size_t(hit)];
                            const double l = d.len();
                            if (l < double(cf.slash_reach + 160) &&
                                (l <= double(cf.body_r) || d.dot(strikeAim) >= l * double(cf.slash_cos) / 1000.0) &&
                                !hasWall(sp.p[size_t(hit)], em.fut[size_t(hit)].pt(), now + j)) {
                                ch = std::max(ch, 0.98 * clamp01((double(cf.slash_reach + 160) - l) / 310.0));
                            }
                        }
                        if (strike && !armed && j + 1 == hit && !kickProtected(em.fut[size_t(j)])) {
                            const V2 d = em.fut[size_t(hit)].pt() - sp.p[size_t(hit)];
                            const double l = d.len();
                            if (l < double(cf.kick_reach + 160) &&
                                (l <= double(cf.body_r) || d.dot(strikeAim) >= l * double(cf.kick_cos) / 1000.0) &&
                                !hasWall(sp.p[size_t(hit)], em.fut[size_t(hit)].pt(), now + j)) {
                                kicked[k] += em.w * clamp01((double(cf.kick_reach + 160) - l) / 310.0);
                            }
                        }
                        prevK = std::max(prevK, ch);
                        killed[k][size_t(j + 1)] += em.w * prevK;
                    }
                }
            }
        }

        // Known + phantom dart threat penalties & HIGH-VALUE PARRY-THEFT
        double specPen = 0.0;
        std::array<double, MAXP> maxKn{}, meanKn{}, blkRew{};
        for (const auto& th : threats) {
            bool willParry = false;
            double parryDist = 0.0;
            if (isSlash && cf.block && hit >= 1 && hit <= NV_H && th.segs[size_t(hit - 1)].on) {
                const Seg& pseg = th.segs[size_t(hit - 1)];
                const V2 delta = pseg.a - sp.p[size_t(hit)];
                const double dl = delta.len();
                if (dl <= double(cf.slash_reach - 25) &&
                    delta.dot(strikeAim) >= dl * double(cf.block_cos) / 1000.0 &&
                    !hasWall(pseg.a, sp.p[size_t(hit)], now + hit - 1)) {
                    willParry = true;
                    parryDist = dl;
                }
            }

            double worst = 0.0;
            for (int j = 0; j < NV_H; ++j) {
                const Seg& seg = th.segs[size_t(j)];
                if (!seg.on) continue;
                if (willParry && j + 1 == hit) {
                    if (th.known) {
                        const double enDist = tracks[th.owner].active
                                              ? (predBody(th.owner, j, 0).pt() - seg.a).len() : 0.0;
                        const double relConf = clamp01((double(cf.slash_reach) - parryDist + 100.0) / 280.0) *
                                               clamp01((parryDist - 240.0) / 340.0);
                        // Don't linger on a collapsing tile past its walk deadline just to farm a harmless recall-block!
                        double collapseDisc = 1.0;
                        if (NV_WALK_SLACK > 0.0) {
                            const PhaseMap& pz = pmap(now + hit);
                            const int myCell = pz.clampIdx(sp.p[size_t(hit)]);
                            const int myWalkDl = pz.latestWalk[myCell];
                            if (myWalkDl >= 0 && myWalkDl < int(cf.steps + 100) && myWalkDl - (now + hit) < 16) {
                                const bool lethalIfMissed = seg_origin_sq(seg.a - sp.p[size_t(j)], seg.b - sp.p[size_t(j + 1)]) < (hitRad + 260.0) * (hitRad + 260.0);
                                if (!lethalIfMissed) collapseDisc = 0.35;
                            }
                        }
                        const double rwd = (18.5 + 17.5 * clamp01((enDist - 750.0) / 3000.0)) *
                                           (0.65 + 0.35 * relConf) * NV_PARRY_SCALE * collapseDisc /
                                           (1.0 + 0.07 * std::max(0, c.meleeAt));
                        blkRew[th.owner] += rwd * th.weight;
                    }
                    break;
                }
                const bool clashed = cf.clash && ownSegs[size_t(j)].on &&
                                     (ownSegs[size_t(j)].a - seg.a).sq() > double(4 * cf.dart_r * cf.dart_r) &&
                                     seg_origin_sq(ownSegs[size_t(j)].a - seg.a, ownSegs[size_t(j)].b - seg.b) <= double(4 * cf.dart_r * cf.dart_r);
                const double d2 = seg_origin_sq(seg.a - sp.p[size_t(j)], seg.b - sp.p[size_t(j + 1)]);
                if (clashed && (seg.a - sp.p[size_t(j)]).len() > hitRad + double(cf.dart_v) * 0.72) break;

                const bool exactPreParry = willParry && (j < th.exactUntil);
                const double pad = exactPreParry ? 25.0
                                 : (willParry ? 140.0 : (th.known ? (th.returning ? 600.0 : 290.0) : 420.0));
                if (d2 < (hitRad + pad) * (hitRad + pad)) {
                    const double d = std::sqrt(d2);
                    double risk = 0.0;
                    if (th.known) {
                        risk = (d <= hitRad)
                               ? (1.0 + 0.30 * clamp01((hitRad - d) / 340.0))
                               : (0.65 * clamp01((hitRad + pad - d) / pad));
                        risk *= 122.0 / (1.0 + 0.14 * j);
                    } else {
                        int freeT = 0;
                        for (int u = std::max(0, th.signal); u <= j; ++u) {
                            if (sp.st[size_t(u)] == S_FREE || sp.st[size_t(u)] == S_RECALL) ++freeT;
                        }
                        const double react = (freeT <= 1) ? 1.0 : (freeT == 2 ? 0.85 : (freeT == 3 ? 0.54 : 0.31));
                        risk = (aliveEnemies > 1 ? 50.0 : 47.0) * react * clamp01((hitRad + pad - d) / (pad + 240.0)) /
                               (1.0 + 0.055 * j);
                    }
                    if (aliveEnemies == 1) {
                        risk *= (1.0 - 0.92 * killed[th.owner][size_t(j)]);
                    } else if (!th.known) {
                        risk *= (1.0 - 0.80 * killed[th.owner][size_t(std::clamp(th.signal - int(cf.delay) - 1, 0, NV_H))]);
                    }
                    if (!th.known && th.signal - cf.delay - 1 >= hit) {
                        risk *= (1.0 - 0.95 * kicked[th.owner]);
                    }
                    worst = std::max(worst, risk);
                }
                if (clashed) break;
            }
            if (th.known) {
                maxKn[th.owner] = std::max(maxKn[th.owner], worst);
                meanKn[th.owner] += th.weight * worst;
            } else {
                specPen += th.weight * worst;
            }
        }
        double totalBlkRew = 0.0;
        for (int k = 0; k < N; ++k) {
            totalBlkRew += blkRew[k];
            val += blkRew[k] - 0.48 * maxKn[k] - 0.52 * meanKn[k];
        }
        val -= specPen;

        // Dash snipe + counter-kick penalty (applies to ALL dashes, INCLUDING dashStrike, discounted ONLY by real kill/kick prob!)
        if (c.act == A_DASH) {
            const bool isDashStrike = (c.meleeAt >= 0);
            val -= isDashStrike ? (1.45 - 0.65 * pressure) : (2.75 - 1.20 * pressure);
            if (!sp.dead) {
                const int stopT = isDashStrike ? std::clamp(hit, 2, NV_H) : 7;
                const V2 stopPt = sp.p[size_t(stopT)];
                const double emergScale = anyKnownDartFlying ? 0.35 : 1.0;
                double snipePen = 0.0;
                double ckPen = 0.0;
                for (int k = 0; k < N; ++k) {
                    const EnemyTrack& tr = tracks[k];
                    if (!tr.active || !tr.enemy) continue;
                    const double aliveProb = std::max(0.0, 1.0 - killed[k][size_t(std::min(NV_H, hit))] - 0.80 * kicked[k]);
                    if (tr.readyTick <= stopT + 1) {
                        const V2 enS = predBody(k, std::max(1, stopT - 2), 0).pt();
                        const double dLand = (stopPt - enS).len();
                        if (dLand <= double(cf.range + 650) && !hasWall(enS, stopPt, now + stopT)) {
                            const double closeFac = clamp01((double(cf.range + 650) - dLand) / 4200.0);
                            const double p = (11.0 + 21.0 * closeFac) * (1.0 - 0.45 * tr.distracted) * tr.conf * aliveProb;
                            snipePen = std::max(snipePen, p);
                        }
                    } else {
                        const V2 enS = predBody(k, stopT, 0).pt();
                        const double d = (stopPt - enS).len();
                        const bool enDash = (tr.obsP.cd <= lag + stopT + 1);
                        const double maxReach = enDash ? 4650.0 : 2150.0;
                        if (d <= maxReach && !hasWall(enS, stopPt, now + stopT)) {
                            const double kFat = kickThreatFatal(stopPt, enS, now + stopT, enDash);
                            const bool fatalCombo = (tr.readyTick <= stopT + cf.kick_stun);
                            if (kFat > 0.0 || fatalCombo) {
                                const double prox = clamp01((maxReach - d) / 2700.0);
                                const double base = std::max(38.0 * kFat, fatalCombo ? 21.0 : 0.0);
                                ckPen = std::max(ckPen, base * (0.45 + 0.55 * prox) * tr.conf * aliveProb);
                            }
                        }
                    }
                }
                const double ffaSnipeScale = (aliveEnemies > 1) ? NV_FFA_SNIPE : 1.0;
                if (aliveEnemies > 1 && !anyKnownDartFlying && !isDashStrike) val -= NV_FFA_DASH_SAVE;
                val -= snipePen * NV_SNIPE_SCALE * ffaSnipeScale * emergScale;
                val -= ckPen;
            }
        } else if (curObs->me().cd > 0 && !sp.dead) {
            val -= evalCounterKickOnLanding(sp, 5, false) * 0.55;
        }

        // Post-melee immobilization & anti-whiff-brake guard (NEVER freeze in S_KICKED/S_SLASHED for 7 ticks on a whiff!)
        if (strike && totalBlkRew < 0.5 && !sp.dead) {
            const int postT = std::clamp(hit, 1, NV_H);
            const V2 lockPt = sp.p[size_t(postT)];
            if (strikeOffVal < 2.8) {
                val -= (nearestDist < 5800.0) ? 16.0 : 9.0;
                if (c.act == A_MELEE && curObs->me().st == S_DASH &&
                    curObs->me().timer == cf.dash_t + cf.dash_post) {
                    val -= 20.0; // Never abort a dash at 0mm movement with a whiffed kick unless pit-emergency!
                }
            }
            // Even for real melee attempts, price in 6-tick immobile post-lag vulnerability against survivors!
            double lockRisk = 0.0;
            for (int k = 0; k < N; ++k) {
                const EnemyTrack& tr = tracks[k];
                if (!tr.active || !tr.enemy) continue;
                const double surv = std::max(0.0, 1.0 - killed[k][size_t(postT)] - 0.88 * kicked[k]);
                if (surv < 0.12) continue;
                const V2 enL = predBody(k, std::min(NV_FUT, postT + 2), 0).pt();
                const double d = (lockPt - enL).len();
                if (tr.readyTick <= postT + 3 && d < double(cf.range + 400) && !hasWall(enL, lockPt, now + postT)) {
                    const double prox = clamp01((double(cf.range + 400) - d) / 4000.0);
                    lockRisk = std::max(lockRisk, (10.0 + 16.0 * prox) * surv * tr.conf);
                } else if (tr.readyTick > postT && d < 4200.0 && tr.obsP.cd <= lag + postT + 2 &&
                           !hasWall(enL, lockPt, now + postT)) {
                    const bool fatalPit = kickSlideFatal(lockPt, lockPt - enL, now + postT + 2);
                    const bool fatalCombo = (tr.readyTick <= postT + 2 + cf.kick_stun);
                    if (fatalPit || fatalCombo) {
                        const double prox = clamp01((4200.0 - d) / 2600.0);
                        lockRisk = std::max(lockRisk, (fatalPit ? 24.0 : 14.0) * (0.4 + 0.6 * prox) * surv * tr.conf);
                    }
                }
            }
            val -= lockRisk * (strikeOffVal < 2.8 ? 1.0 : 0.65);
        }

        // Dash-rush & anti-kick threats
        std::array<double, MAXP> rushPen{};
        for (const auto& rx : rushes) {
            const V2 d = sp.p[size_t(rx.hitTick + 1)] - rx.hitPos;
            const double lim = double(cf.slash_reach + 440);
            if (d.sq() > lim * lim) continue;
            const double l = d.len();
            const double cone = rx.aimDir.dot(d) / std::max(1.0, l);
            const double aimF = 0.35 + 0.65 * clamp01((cone + 0.15) / 0.65);
            double risk = clamp01((lim - l) / 820.0) * aimF / (1.0 + 0.09 * rx.hitTick);
            if (!rx.armed) {
                const double kFat = kickThreatFatal(sp.p[size_t(rx.hitTick + 1)], rx.hitPos, now + rx.hitTick + 1, true);
                const double cdMult = (sp.cd[size_t(rx.hitTick + 1)] > 0) ? 1.35 : 1.0;
                risk *= (kFat > 0.0 ? (1.90 * kFat * cdMult) : (tracks[rx.owner].readyTick < rx.hitTick + cf.kick_stun ? 1.65 : 0.55));
            }
            risk *= (1.0 - 0.90 * killed[rx.owner][size_t(rx.hitTick)]);
            if (rx.hitTick >= hit) risk *= (1.0 - 0.94 * kicked[rx.owner]);
            if (!rx.armed && (c.act == A_RECALL || sp.st[size_t(rx.hitTick)] == S_RECALL)) risk *= 1.55;
            if (isSlash && hit <= rx.hitTick + 1 && hit >= rx.hitTick - 2) {
                const int ahead = rx.hitTick + 1 - hit;
                const double slide = (ahead == 0) ? 0.0 : (ahead == 1 ? double(cf.dash_v - 2 * cf.acc) : double(2 * cf.dash_v - 3 * cf.acc));
                const V2 ctr = rx.hitPos - rx.dashDir * std::max(0.0, slide) - sp.p[size_t(hit)];
                const double cl = ctr.len();
                if (cl < double(cf.slash_reach) && ctr.dot(strikeAim) >= cl * double(cf.slash_cos) / 1000.0) {
                    risk *= (rx.armed && ahead == 0) ? 0.58 : 0.14;
                }
            }
            rushPen[rx.owner] = std::max(rushPen[rx.owner], risk * rx.weight);
        }
        for (int k = 0; k < N; ++k) val -= rushPen[k] * 28.0;

        // Close-quarters melee & standing kick danger
        for (int k = 0; k < N; ++k) {
            const EnemyTrack& tr = tracks[k];
            if (!tr.active || !tr.enemy || tr.dist > 7800.0) continue;
            double rk = 0.0;
            for (int m : {0, 1, 3}) {
                const EnemyModel& em = tr.models[m];
                double worst = 0.0;
                for (int j = 1; j <= NV_H; ++j) {
                    const Body& ob = em.fut[size_t(j)];
                    if (ob.st == S_STUN || ob.st == S_SLASHED || ob.st == S_KICKED) continue;
                    const bool blade = (tr.readyTick <= j - int(cf.slash_pre));
                    const V2 d = sp.p[size_t(j)] - ob.pt();
                    const double lim = double(cf.slash_reach + 520);
                    if (d.sq() > lim * lim) continue;
                    if (hasWall(sp.p[size_t(j)], ob.pt(), now + j - 1)) continue;
                    double str = blade ? 29.5 : 10.5;
                    if (!blade) {
                        if (kickSlideFatal(sp.p[size_t(j)], d, now + j)) str = 35.0;
                        else if (tr.readyTick < j + cf.kick_stun) str = 22.5;
                    }
                    double dg = clamp01((lim - d.len()) / 700.0) * str / (1.0 + 0.09 * j);
                    if (strike && j >= hit) {
                        const V2 td = em.fut[size_t(hit)].pt() - sp.p[size_t(hit)];
                        const double tl = td.len();
                        if (tl < double(cf.slash_reach + 140) && strikeAim.dot(td) >= 0.48 * tl) {
                            dg *= (1.0 - 0.86 * clamp01((double(cf.slash_reach + 140) - tl) / 330.0));
                        }
                    }
                    dg *= (1.0 - 0.90 * killed[k][size_t(j - 1)]);
                    if (j > hit) dg *= (1.0 - 0.95 * kicked[k]);
                    worst = std::max(worst, dg);
                }
                rk += (m == 0 ? 0.46 : (m == 1 ? 0.25 : 0.29)) * worst;
            }
            val -= rk * tr.conf;
        }

        if (focus >= 0 && tracks[focus].readyTick < 4 && nearestDist < 7200.0 && !hiddenAt(focus, now)) {
            const V2 rad = (predBody(focus, 0, 0).pt() - here).unit();
            val += 2.35 * std::abs((sp.p[4] - here).dot(rad.perp())) / 1000.0;
        }
        if (c.move.sq() > 0.0) val += 0.08 * c.move.dot(prevMove);
        if (c.turnAt >= 0) val -= 0.13;
        return val;
    }

    // ---------------------------------------------------------------- Stage 2: Exact dart::Game Micro-Exchange Verifier
    Action makeAct(V2 mv, int act = A_NONE, V2 aim = {}) const {
        Action a{};
        a.mx = std::clamp<i64>(iround(mv.x * 1000.0), -1000000, 1000000);
        a.my = std::clamp<i64>(iround(mv.y * 1000.0), -1000000, 1000000);
        a.act = act;
        a.ax = std::clamp<i64>(iround(aim.x * 1000.0), -1000000, 1000000);
        a.ay = std::clamp<i64>(iround(aim.y * 1000.0), -1000000, 1000000);
        return a;
    }

    V2 safeMoveDir(const Player& p, V2 want, int t) const {
        const V2 pt = vpos(p);
        const V2 u = (want.sq() > 1e-4) ? want.unit() : V2{0.0, 0.0};
        if (u.sq() < 1e-4) return isPit(pt, t) ? (home - pt).unit() : V2{0.0, 0.0};
        const V2 nxt = pt + u * double(cf.run_v * 2);
        if (!isPit(nxt, t + 1) && !isPit(pt + u * double(cf.run_v), t)) return u;
        const V2 p1 = u.perp(), p2 = u.perp() * -1.0;
        if (!isPit(pt + p1 * double(cf.run_v * 2), t + 1)) return p1;
        if (!isPit(pt + p2 * double(cf.run_v * 2), t + 1)) return p2;
        return (home - pt).unit();
    }

    Action simEnemyPolicy(const Game& g, int k, int pol,
                          const std::array<Player, 20>& simMeP,
                          const std::array<Dart, 20>& simMeD) const {
        const Player& p = g.p[k];
        if (!p.alive || p.unknown) return Action{};
        const int t = int(g.t);
        const int tView = std::max(0, t - lag);
        auto getMeP = [&](int tv) -> const Player& {
            if (tv <= now) return selfHist[size_t(tv % NV_HIST)].me;
            return simMeP[size_t(std::min(19, tv - now))];
        };
        auto getMeD = [&](int tv) -> const Dart& {
            if (tv <= now) return selfHist[size_t(tv % NV_HIST)].dart;
            return simMeD[size_t(std::min(19, tv - now))];
        };

        // Pick target enemy for seat k
        int tgt = seat;
        V2 tgtPos = vpos(getMeP(tView));
        V2 tgtVel = vvel(getMeP(tView));
        int tgtSt = int(getMeP(tView).st);
        bool tgtArmed = (getMeD(tView).st == D_HAND);
        double bestD = (tgtPos - vpos(p)).len();
        if (N > 2) {
            for (int q = 0; q < N; ++q) {
                if (q == k || q == seat || !g.p[q].alive || g.p[q].unknown || gi->team[q] == gi->team[k]) continue;
                const double d = (vpos(g.p[q]) - vpos(p)).len();
                if (d + 350.0 < bestD) {
                    bestD = d;
                    tgt = q;
                    tgtPos = vpos(g.p[q]);
                    tgtVel = vvel(g.p[q]);
                    tgtSt = int(g.p[q].st);
                    tgtArmed = (g.d[q].st == D_HAND);
                }
            }
        }

        const V2 myPt = vpos(p);
        const V2 toTgt = (tgtPos - myPt).unit();
        const bool armed = (g.d[k].st == D_HAND);
        const bool freeSt = (p.st == S_FREE || p.st == S_RECALL);
        const bool dashCan = (cf.dash_slash && p.st == S_DASH &&
                              p.timer > cf.dash_post && p.timer <= cf.dash_t + cf.dash_post);
        const bool los = !hasWall(myPt, tgtPos, t);

        // Dash-cancel into melee if within reach!
        if (dashCan) {
            const V2 estMe = myPt + V2{double(p.ax), double(p.ay)} * (double(cf.dash_v) / 1000.0);
            if ((tgtPos - estMe).len() <= double(cf.slash_reach + 160) && !hasWall(estMe, tgtPos, t)) {
                return makeAct({}, A_MELEE, (tgtPos - estMe).unit());
            }
            return Action{};
        }
        if (!freeSt) {
            return makeAct(safeMoveDir(p, toTgt.perp(), t));
        }

        // Check incoming hostile dart (especially our dart for parry/dodge!)
        bool dartImminent = false;
        V2 dartDodgeDir = toTgt.perp();
        for (int q = 0; q < N; ++q) {
            if (q == k || gi->team[q] == gi->team[k]) continue;
            const Dart& hd = (q == seat) ? getMeD(tView) : g.d[q];
            if (!dart_flying(hd.st)) continue;
            Dart sd = hd;
            const V2 qPos = (q == seat) ? tgtPos : vpos(g.p[q]);
            const int fwd = (q == seat) ? (t - tView) : 0;
            for (int f = 0; f < fwd && dart_flying(sd.st); ++f) flyDart(sd, qPos, qPos, tView + f);
            if (!dart_flying(sd.st)) continue;
            // Check exact parry window on tick slash_pre (=2)!
            Dart sd2 = sd;
            Seg s0 = flyDart(sd2, qPos, qPos, t);
            Seg s1 = flyDart(sd2, qPos, qPos, t + 1);
            if (armed && cf.block && (pol == 2 || pol == 0 || pol == 3) && s1.on) {
                const V2 pd = s1.a - myPt;
                if (pd.len() <= double(cf.slash_reach - 30) && !hasWall(myPt, s1.a, t + 1)) {
                    if (pol == 2 || seg_origin_sq(s1.a - myPt, s1.b - myPt) < (hitRad + 260.0) * (hitRad + 260.0)) {
                        return makeAct({}, A_MELEE, pd.unit());
                    }
                }
            }
            if (s0.on) {
                const double d2 = seg_origin_sq(s0.a - myPt, s0.b - myPt);
                const double d2b = s1.on ? seg_origin_sq(s1.a - myPt, s1.b - myPt) : 1e18;
                if (std::min(d2, d2b) < (hitRad + 520.0) * (hitRad + 520.0)) {
                    dartImminent = true;
                    const V2 dv = (s0.b - s0.a).unit();
                    const V2 p1 = dv.perp(), p2 = dv.perp() * -1.0;
                    dartDodgeDir = ((myPt - s0.a).dot(p1) >= 0.0) ? p1 : p2;
                }
            }
        }

        // Point-blank melee slash / kick
        if (bestD <= double(cf.slash_reach + 80) && los && !( !armed && kickProtected(Body(g.p[tgt])) )) {
            return makeAct({}, A_MELEE, toTgt);
        }

        // Policy 1: Dedicated Dash-Kick / Dash-Slash Rush
        if (pol == 1) {
            if (p.cd == 0 && bestD < 4900.0 && bestD > 1200.0 && los) {
                const V2 lead = (tgtPos + tgtVel * 3.0 - myPt).unit();
                const V2 land = myPt + lead * double(cf.dash_t * cf.dash_v);
                if (!isPit(land, t + 6)) return makeAct(lead, A_DASH, lead);
            }
            if (armed && bestD < 5000.0 && los) {
                const double trv = bestD / (0.80 * double(cf.dart_v));
                const V2 aim = (tgtPos + tgtVel * (double(cf.throw_pre) + 0.65 * trv) - myPt).unit();
                return makeAct(safeMoveDir(p, toTgt.perp(), t), A_THROW, aim);
            }
            return makeAct(safeMoveDir(p, toTgt, t));
        }

        // Policy 0: Aggressive Throw / Kick / Punish
        if (pol == 0) {
            if (dartImminent && p.cd == 0 && !armed) {
                const V2 dd = safeMoveDir(p, dartDodgeDir, t);
                if (!isPit(myPt + dd * double(cf.dash_t * cf.dash_v), t + 6)) return makeAct(dd, A_DASH, dd);
            }
            if (armed && bestD < double(cf.range - 200) && los) {
                const bool goodWin = (bestD < 3800.0 || !tgtArmed || tgtSt == S_DASH || tgtSt == S_STUN ||
                                      tgtSt == S_SLASHED || tgtSt == S_KICKED);
                if (goodWin) {
                    const double trv = bestD / (0.80 * double(cf.dart_v));
                    const V2 tgtAimPt = (tgt == seat && tView <= now)
                                        ? smartEnemyAim(myPt, tView, trv)
                                        : (tgtPos + tgtVel * (double(cf.throw_pre) + 0.70 * trv));
                    return makeAct(safeMoveDir(p, dartImminent ? dartDodgeDir : toTgt.perp(), t), A_THROW, (tgtAimPt - myPt).unit());
                }
            }
            if (!armed && p.cd == 0 && bestD < 4300.0 && los &&
                (kickSlideFatal(tgtPos, tgtPos - myPt, t + 4) || !tgtArmed)) {
                const V2 ld = (tgtPos + tgtVel * 2.5 - myPt).unit();
                if (!isPit(myPt + ld * double(cf.dash_t * cf.dash_v), t + 6)) return makeAct(ld, A_DASH, ld);
            }
            if (!armed && g.d[k].st == D_GROUND) {
                return makeAct(safeMoveDir(p, vpos(g.d[k]) - myPt, t));
            }
            return makeAct(safeMoveDir(p, dartImminent ? dartDodgeDir : toTgt.perp(), t));
        }

        // Policy 2: Parry & Pickup Counter
        if (pol == 2) {
            if (!armed && g.d[k].st == D_GROUND) {
                const V2 toD = vpos(g.d[k]) - myPt;
                if (p.cd == 0 && toD.len() > 1400.0 && toD.len() < 3600.0 && !isPit(myPt + toD.unit() * double(cf.dash_t * cf.dash_v), t + 6)) {
                    return makeAct(toD.unit(), A_DASH, toD.unit());
                }
                return makeAct(safeMoveDir(p, toD, t));
            }
            if (dartImminent) return makeAct(safeMoveDir(p, dartDodgeDir, t));
            if (armed && !tgtArmed && bestD < 4000.0 && los) {
                const double trv = bestD / (0.80 * double(cf.dart_v));
                return makeAct(safeMoveDir(p, toTgt.perp(), t), A_THROW, (tgtPos + tgtVel * (2.0 + 0.6 * trv) - myPt).unit());
            }
            const V2 strafe = (k & 1) ? toTgt.perp() : (toTgt.perp() * -1.0);
            return makeAct(safeMoveDir(p, strafe, t));
        }

        // Policy 3: Evasive Strafe & Safe Escape
        if (dartImminent) {
            const V2 dd = safeMoveDir(p, dartDodgeDir, t);
            if (p.cd == 0 && !isPit(myPt + dd * double(cf.dash_t * cf.dash_v), t + 6)) return makeAct(dd, A_DASH, dd);
            return makeAct(dd);
        }
        if (!armed && g.d[k].st == D_GROUND) return makeAct(safeMoveDir(p, vpos(g.d[k]) - myPt, t));
        const V2 evDir = (bestD < 3200.0) ? (toTgt * -1.0 + toTgt.perp() * 0.6) : toTgt.perp() * -1.0;
        return makeAct(safeMoveDir(p, evDir, t));
    }

    bool stunEndsInPit(const Player& p, int t) const {
        if (p.st != S_STUN || p.timer <= 0) return false;
        Body b(p);
        const int rem = int(p.timer);
        for (int j = 0; j < rem; ++j) advanceBody(b, {}, t + j);
        return isPit(b.pt(), t + rem - 1);
    }

    void refineWithEngine(std::vector<Candidate>& pool, Candidate& best) const {
        const double s2W = (aliveEnemies == 1) ? NV_S2_WEIGHT : NV_S2_FFA_WEIGHT;
        if (s2W <= 0.0 || pool.size() < 2 || focus < 0 || now < 1) return;
        const int H2 = (aliveEnemies == 1) ? 10 : 8;
        const int nPol = 4;
        std::array<double, 4> polW = {0.31, 0.23, 0.28, 0.18};
        if (learn[focus].parriesSeen > 0) { polW[2] += 0.12; polW[0] -= 0.06; polW[3] -= 0.06; }
        if (tracks[focus].readyTick > 2 && tracks[focus].obsP.cd <= lag + 2) { polW[1] += 0.14; polW[3] -= 0.07; polW[0] -= 0.07; }

        // Build base Game at t0 = now - lag and replay [t0, now) once per enemy policy
        const int t0 = now - lag;
        std::array<Game, 4> baseAtNow;
        std::array<Player, 20> dummyMeP{};
        std::array<Dart, 20> dummyMeD{};

        Game gInit(*gi, *curObs);
        gInit.t = t0;
        gInit.over = false;
        gInit.winner = -2;
        gInit.record = false;
        gInit.p[seat] = selfHist[size_t(t0 % NV_HIST)].me;
        gInit.p[seat].team = gi->team[seat];
        gInit.p[seat].px = gInit.p[seat].x;
        gInit.p[seat].py = gInit.p[seat].y;
        gInit.p[seat].unknown = 0;
        gInit.d[seat] = selfHist[size_t(t0 % NV_HIST)].dart;
        for (int k = 0; k < N; ++k) {
            if (k == seat) continue;
            if (tracks[k].active && !tracks[k].vis) {
                gInit.p[k] = tracks[k].obsP;
                gInit.p[k].team = gi->team[k];
                gInit.p[k].px = gInit.p[k].x;
                gInit.p[k].py = gInit.p[k].y;
                gInit.p[k].unknown = 0;
            }
        }
        const int ph0 = phaseOf(t0);
        gInit.live = phaseMaps[size_t(ph0)];
        gInit.live_fallen = ph0;

        for (int pol = 0; pol < nPol; ++pol) {
            Game g = gInit;
            for (int dt = 0; dt < lag; ++dt) {
                const int curT = t0 + dt;
                const int ph = phaseOf(curT);
                if (g.live_fallen != ph) { g.live = phaseMaps[size_t(ph)]; g.live_fallen = ph; }
                Action acts[MAXP]{};
                acts[seat] = selfHist[size_t(curT % NV_HIST)].act;
                for (int k = 0; k < N; ++k) {
                    if (k == seat || !g.p[k].alive || g.p[k].unknown) continue;
                    const int kPol = (k == focus) ? pol : 3;
                    acts[k] = simEnemyPolicy(g, k, kPol, dummyMeP, dummyMeD);
                }
                g.step(acts);
            }
            // Bayesian consistency check against our REAL un-delayed state at `now`
            if (!g.p[seat].alive || (g.p[seat].st == S_STUN && curObs->me().st != S_STUN)) {
                polW[size_t(pol)] *= 0.12;
            }
            // Snap self & own dart to ground-truth `curObs` at `now`
            g.p[seat] = curObs->me();
            g.p[seat].team = gi->team[seat];
            g.p[seat].px = g.p[seat].x;
            g.p[seat].py = g.p[seat].y;
            g.p[seat].unknown = 0;
            g.d[seat] = curObs->my_dart();
            g.over = false;
            g.winner = -2;
            g.t = now;
            baseAtNow[size_t(pol)] = std::move(g);
        }

        double wSum = 0.0;
        for (double w : polW) wSum += w;
        if (wSum <= 1e-9) return;
        for (double& w : polW) w /= wSum;

        // Roll out highest-ranked Stage-1 candidates first with pure CPU-time safety guard
        std::sort(pool.begin(), pool.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
        const bool ffaMulti = (aliveEnemies > 1);
        Candidate newBest;
        newBest.score = -1e18;
        int evaluated = 0;
        for (auto& c : pool) {
            if (now > 0 && evaluated >= 4 && cpuElapsedMs() > 7.5) break;
            ++evaluated;
            double s2Exp = 0.0;
            for (int pol = 0; pol < nPol; ++pol) {
                Game g = baseAtNow[size_t(pol)];
                const int initKills = int(g.p[seat].kills);
                const int initMyBlk = int(g.p[seat].n_block);
                const int initEnBlk = int(g.p[focus].n_block);
                std::array<Player, 20> simMeP{};
                std::array<Dart, 20> simMeD{};
                int stepEnd = H2;
                for (int s = 0; s < H2; ++s) {
                    simMeP[size_t(s)] = g.p[seat];
                    simMeD[size_t(s)] = g.d[seat];
                    const int curT = now + s;
                    const int ph = phaseOf(curT);
                    if (g.live_fallen != ph) { g.live = phaseMaps[size_t(ph)]; g.live_fallen = ph; }
                    Action acts[MAXP]{};
                    if (s == 0) {
                        acts[seat] = makeAct(c.move, c.act, c.aim);
                    } else {
                        V2 mv = (c.turnAt >= 0 && s >= c.turnAt) ? c.after : c.move;
                        mv = safeMoveDir(g.p[seat], mv, curT);
                        int act = A_NONE;
                        V2 aim{};
                        if (c.meleeAt == s) {
                            act = A_MELEE;
                            if (c.parryThreat >= 0 && s + int(cf.slash_pre) - 1 < NV_H &&
                                threats[size_t(c.parryThreat)].segs[size_t(s + int(cf.slash_pre) - 1)].on) {
                                aim = (threats[size_t(c.parryThreat)].segs[size_t(s + int(cf.slash_pre) - 1)].a - vpos(g.p[seat])).unit();
                            } else {
                                aim = (vpos(g.p[focus]) - vpos(g.p[seat])).unit();
                            }
                        } else if (c.act == A_RECALL && g.p[seat].st == S_RECALL) {
                            act = A_RECALL;
                            mv = {};
                        }
                        acts[seat] = makeAct(mv, act, aim);
                    }
                    for (int k = 0; k < N; ++k) {
                        if (k == seat || !g.p[k].alive || g.p[k].unknown) continue;
                        const int kPol = (k == focus) ? pol : 3;
                        acts[k] = simEnemyPolicy(g, k, kPol, simMeP, simMeD);
                    }
                    g.step(acts);
                    if (!g.p[seat].alive || g.over) {
                        stepEnd = s + 1;
                        break;
                    }
                }

                double r = 0.0;
                const bool meAlive = (g.p[seat].alive != 0);
                const bool enAlive = (g.p[focus].alive != 0);
                const int kDelta = int(g.p[seat].kills) - initKills;
                if (!meAlive && enAlive) {
                    r -= (ffaMulti ? 115.0 : 95.0) - 2.0 * stepEnd;
                } else if (!meAlive && !enAlive) {
                    r -= (ffaMulti ? 78.0 : 28.0);
                } else if (meAlive && kDelta > 0 && c.act != A_THROW) {
                    // Stage 1's 10-model+evasion ray solver prices throws; Stage 2 verifies melee/fall kills & survival!
                    r += (ffaMulti ? 46.0 : 72.0) * kDelta - 1.5 * stepEnd;
                }

                if (meAlive && g.p[seat].st == S_STUN) {
                    if (stunEndsInPit(g.p[seat], now + stepEnd)) {
                        r -= 92.0;
                    } else {
                        const bool enArms = (g.d[focus].st == D_HAND) || (tracks[focus].readyTick <= stepEnd + g.p[seat].timer);
                        r -= enArms ? 46.0 : 22.0;
                    }
                }
                if (enAlive && g.p[focus].st == S_STUN) {
                    if (stunEndsInPit(g.p[focus], now + stepEnd)) r += (ffaMulti ? 46.0 : 72.0);
                    else r += 22.0;
                }

                if (int(g.p[seat].n_block) > initMyBlk) r += 22.0;
                if (int(g.p[focus].n_block) > initEnBlk) r -= (ffaMulti ? 14.0 : 26.0);

                if (meAlive && enAlive) {
                    const bool myHand = (g.d[seat].st == D_HAND);
                    const bool enHand = (g.d[focus].st == D_HAND);
                    if (myHand && !enHand) r += 10.0;
                    else if (!myHand && enHand) r -= 12.0;
                }
                s2Exp += polW[size_t(pol)] * r;
            }
            c.score += s2W * s2Exp;
            if (c.score > newBest.score) newBest = c;
        }
        if (newBest.score > -1e15) best = newBest;
    }

    // ---------------------------------------------------------------- Main Decision Loop
    Action decide(const Obs& o) {
        tCpu0 = thread_cpu_100ns();
        curObs = &o;
        now = int(o.t);
        lag = std::min(now, int(cf.delay));
        seat = o.seat;
        here = vpos(o.me());
        kickSimCount = 0;
        workCount = 0;
        anyKnownDartFlying = false;

        selfHist[size_t(now % NV_HIST)].me = o.me();
        selfHist[size_t(now % NV_HIST)].dart = o.my_dart();

        Action out{};
        if (!o.me().alive) {
            selfHist[size_t(now % NV_HIST)].act = out;
            return out;
        }

        updateTrackers();

        threats.clear();
        rushes.clear();
        for (int k = 0; k < N; ++k) {
            if (k == seat) continue;
            const bool multi = tracks[k].active &&
                               (o.d[k].st == D_BACK || o.d[k].st == D_CALL || o.d[k].st == D_RISE ||
                                (o.d[k].st == D_OUT && o.d[k].left < 4000));
            if (multi) {
                addKnownDart(k, 0, 0.36);
                addKnownDart(k, 1, 0.19);
                addKnownDart(k, 2, 0.17);
                addKnownDart(k, 5, 0.14);
                addKnownDart(k, 6, 0.14);
            } else {
                addKnownDart(k, 0, 1.0);
            }
            if (tracks[k].active) {
                addPhantomDarts(k);
                addRushThreats(k);
                addCrossfire(k);
            }
        }

        // Recovery analysis: nearest safe point on 600mm pickup disk + guard/race detection
        recoveryUrg = 1.0;
        dartGuardedByEnemy = false;
        winPickupRace = false;
        bestPickSpot = vpos(o.my_dart());
        if (o.my_dart().st == D_GROUND || o.my_dart().st == D_RISE) {
            const V2 item = vpos(o.my_dart());
            double bestPickD = (item - here).len();
            bestPickSpot = item;
            const double rad = double(cf.pick_r) - 70.0;
            for (int i = 0; i < 8; ++i) {
                const V2 pt = item + dirs32[size_t(i * 4)] * rad;
                if (isPit(pt, now + 2) || circle_hits_wall(pmap(now).m, cf.body_r, iround(pt.x), iround(pt.y))) continue;
                const double d = (pt - here).len();
                if (d < bestPickD) { bestPickD = d; bestPickSpot = pt; }
            }
            recoveryUrg = 1.0 - 0.65 * clamp01((bestPickD - 1700.0) / 3200.0);
            for (int k = 0; k < N; ++k) {
                if (!tracks[k].active || !tracks[k].enemy) continue;
                const double enD = (item - predBody(k, 1, 0).pt()).len();
                if (tracks[k].readyTick < 7 && enD < bestPickD + 450.0 && enD < 3400.0) {
                    recoveryUrg *= 0.38;
                    dartGuardedByEnemy = true;
                }
                if (o.d[k].st == D_GROUND) {
                    const double enPickD = std::max(0.0, (vpos(o.d[k]) - predBody(k, 0, 0).pt()).len() - double(cf.pick_r));
                    if (bestPickD < enPickD + 600.0 && bestPickD < 3500.0) {
                        winPickupRace = true;
                        recoveryUrg = std::max(recoveryUrg, 1.35);
                    }
                }
            }
            if (isPit(bestPickSpot, now + 3)) recoveryUrg *= 0.55;
        }

        updateField();

        pressure = 0.0;
        if (focus >= 0 && o.my_dart().st == D_HAND) {
            const EnemyTrack& tgt = tracks[focus];
            pressure = clamp01(double(tgt.readyTick - 3) / 9.0) * tgt.conf;
            for (int k = 0; k < N; ++k) {
                if (k != focus && tracks[k].active && tracks[k].enemy &&
                    tracks[k].readyTick < 7 && tracks[k].dist < 5900.0) {
                    pressure *= 0.38;
                }
            }
        }

        const bool armed = (o.my_dart().st == D_HAND);
        const bool freeSt = (o.me().st == S_FREE || o.me().st == S_RECALL);
        const bool dashCancel = (cf.dash_slash && o.me().st == S_DASH &&
                                 o.me().timer > cf.dash_post && o.me().timer <= cf.dash_t + cf.dash_post);
        const bool canMove = (freeSt || o.me().st == S_THROW || o.me().st == S_THROWN);
        if (!canMove && !dashCancel) {
            selfHist[size_t(now % NV_HIST)].act = out;
            return out;
        }

        ShotEval shot;
        if (armed && freeSt && focus >= 0 && nearestDist < double(cf.range + 3400)) {
            for (int k = 0; k < N; ++k) {
                if (tracks[k].active && tracks[k].dist < double(cf.range + 3400)) buildEvasions(k);
            }
            const V2 relOrig = here + vvel(o.me()) * (double(cf.throw_move) * 0.01 * double(cf.throw_pre));
            shot = computeBestShot(relOrig);
        }

        std::array<Candidate, 6> beam{};
        std::vector<Candidate> s2Pool;
        s2Pool.reserve(24);
        Candidate best;
        const i64 workCap = (N > 4) ? 180 : 340;
        const double activeS2W = (aliveEnemies == 1) ? NV_S2_WEIGHT : NV_S2_FFA_WEIGHT;
        const bool collectPool = (activeS2W > 0.0 && N <= 4 && focus >= 0 && nearestDist < 6800.0 && tracks[focus].conf > 0.55);

        auto consider = [&](V2 mv, int act = A_NONE, V2 aim = {}, int meleeAt = -1,
                            int parryTh = -1, int turnAt = -1, V2 after = {}) {
            if (workCount >= workCap) return;
            Candidate c;
            c.move = mv; c.aim = aim; c.act = act;
            c.meleeAt = meleeAt; c.parryThreat = parryTh;
            c.turnAt = turnAt; c.after = after;
            c.score = evalCandidate(c, shot);
            if (c.score > best.score) best = c;
            if (turnAt < 0 && meleeAt < 0 && (act == A_NONE || act == A_THROW)) {
                for (size_t j = 0; j < beam.size(); ++j) {
                    if (beam[j].score >= c.score) continue;
                    for (size_t u = beam.size() - 1; u > j; --u) beam[u] = beam[u - 1];
                    beam[j] = c;
                    break;
                }
            }
            if (collectPool && c.score > -150.0) {
                // Keep diverse high-scoring candidates per action bucket + overall top
                bool merged = false;
                for (auto& e : s2Pool) {
                    const bool sameBucket = (e.act == c.act && (e.meleeAt >= 0) == (c.meleeAt >= 0) &&
                                             (e.parryThreat >= 0) == (c.parryThreat >= 0) &&
                                             (e.turnAt >= 0) == (c.turnAt >= 0));
                    if (sameBucket && (e.move - c.move).sq() < 0.22 && (e.aim - c.aim).sq() < 0.22) {
                        if (c.score > e.score) e = c;
                        merged = true;
                        break;
                    }
                }
                if (!merged) {
                    const size_t cap = (aliveEnemies == 1) ? 14u : 10u;
                    if (s2Pool.size() < cap) {
                        s2Pool.push_back(c);
                    } else {
                        // Prefer evicting non-unique A_NONE moves before evicting unique A_MELEE/A_THROW/A_DASH actions
                        size_t worstI = 0;
                        double worstKey = 1e18;
                        for (size_t j = 0; j < s2Pool.size(); ++j) {
                            const bool protect = (s2Pool[j].parryThreat >= 0 || s2Pool[j].act == A_MELEE);
                            const double key = s2Pool[j].score + (protect ? 18.0 : 0.0);
                            if (key < worstKey) { worstKey = key; worstI = j; }
                        }
                        if (c.score + ((c.parryThreat >= 0 || c.act == A_MELEE) ? 18.0 : 0.0) > worstKey) {
                            s2Pool[worstI] = c;
                        }
                    }
                }
            }
        };

        std::vector<V2> dirs;
        dirs.reserve(40);
        dirs.push_back({});
        const int stride = (N > 4) ? 4 : 2;
        for (int i = 0; i < 32; i += stride) dirs.push_back(dirs32[size_t(i)]);
        if (focus >= 0) {
            const V2 tw = (predBody(focus, 2, 0).pt() - here).unit();
            dirs.push_back(tw);
            dirs.push_back(tw.perp());
            dirs.push_back(tw.perp() * -1.0);
            if (pressure > 0.20) {
                dirs.push_back((predBody(focus, 6, 2).pt() - here).unit());
                dirs.push_back((predBody(focus, 7, 3).pt() - here).unit());
            }
        }
        dirs.push_back((home - here).unit());
        if (o.my_dart().st == D_GROUND) {
            dirs.push_back((bestPickSpot - here).unit());
        }

        if (canMove && N <= 4 && (flankNeed > 0.18 || (now >= cf.col_start && pmap(now).room[pmap(now).clampIdx(here)] < 620.0f))) {
            for (int i = 0; i < 32; i += 4) {
                const V2 hd = dirs32[size_t(i)] * 0.52;
                consider(hd);
                if (shot.target >= 0) consider(hd, A_THROW, shot.dir);
            }
        }

        for (size_t i = 0; i < dirs.size(); ++i) {
            const V2 d = dirs[i];
            if (canMove || i == 0 || (dashCancel && (i % 2 == 0))) consider(d);

            if (freeSt && focus >= 0 && nearestDist < (armed ? 4600.0 : 3200.0) && N <= 4 && i > 0) {
                for (int delay : {2, 4, 6}) {
                    const V2 at = here + d * double(cf.run_v * delay);
                    const int imp = delay + int(armed ? cf.slash_pre : cf.kick_pre);
                    if ((at - predBody(focus, imp, 0).pt()).sq() < 2200.0 * 2200.0) {
                        consider(d, A_NONE, {}, delay);
                    }
                }
            }
            if (shot.target >= 0 && (i <= 1 || N <= 4 || i % 2 == 0)) {
                consider(d, A_THROW, shot.dir);
            }
            if (freeSt && o.me().cd == 0 && i > 0) {
                consider(d, A_DASH, d);
                if (cf.dash_slash && focus >= 0 && nearestDist < 5400.0 &&
                    (armed || tracks[focus].readyTick > 2 || now >= cf.col_start)) {
                    const int mpre = int(armed ? cf.slash_pre : cf.kick_pre);
                    for (int moved = 1; moved < cf.dash_t; ++moved) {
                        const int fTick = int(cf.dash_pre) + moved;
                        const V2 expPt = here + d * double((moved + 1) * cf.dash_v);
                        if ((predBody(focus, fTick + mpre, 0).pt() - expPt).sq() < 1850.0 * 1850.0) {
                            consider(d, A_DASH, d, fTick);
                        }
                    }
                }
            }
        }

        // During dashCancel, also evaluate delayed dash-cancel (after 1-2 more dash steps) so we don't panic-cancel at step 0!
        if (dashCancel && focus >= 0 && nearestDist < 5200.0) {
            const int remDashMov = int(o.me().timer - cf.dash_post - 1);
            for (int w = 1; w <= std::min(2, remDashMov); ++w) {
                consider({}, A_NONE, {}, w);
            }
        }

        // DEDICATED PROACTIVE PARRY-THEFT GENERATOR
        if (armed && cf.block && (freeSt || dashCancel)) {
            const int hit0 = int(cf.slash_pre);
            Candidate tmp;
            tmp.act = A_MELEE;
            tmp.aim = {1.0, 0.0};
            const SelfPath mp = simulateSelf(tmp);
            for (int u = 0; u < int(threats.size()); ++u) {
                const auto& th = threats[size_t(u)];
                if (!th.known || th.weight < 0.16) continue;
                if (hit0 >= 1 && hit0 <= NV_H && th.segs[size_t(hit0 - 1)].on) {
                    const V2 d = th.segs[size_t(hit0 - 1)].a - mp.p[size_t(hit0)];
                    if (d.len() <= double(cf.slash_reach + 60)) {
                        consider({}, A_MELEE, d.unit(), -1, u);
                    }
                }
                if (freeSt) {
                    for (int wait = 1; wait <= 5; ++wait) {
                        const int j = wait + hit0 - 1;
                        if (j >= NV_H || !th.segs[size_t(j)].on) continue;
                        const V2 dartPt = th.segs[size_t(j)].a;
                        const V2 dartVel = (th.segs[size_t(j)].b - dartPt).unit();
                        const double curD = (dartPt - here).len();
                        const double maxWalk = double(wait) * double(cf.run_v);
                        if (curD <= double(cf.slash_reach) + maxWalk + 120.0) {
                            const V2 toDart = (dartPt - here).unit();
                            // Pick sweet-spot 1120mm offset perpendicular to dart path so we never clip hitRad on j-1!
                            const V2 perp = (dartVel.sq() > 0.5) ? dartVel.perp() : toDart.perp();
                            const double sideSign = ((here - dartPt).dot(perp) >= 0.0) ? 1.0 : -1.0;
                            const V2 sweetPt = dartPt + perp * (sideSign * 1120.0);
                            const V2 toSweet = (sweetPt - here).unit();
                            consider(toSweet, A_NONE, {}, wait, u);
                            if (curD > 1150.0) consider(toDart, A_NONE, {}, wait, u);
                            if (curD <= double(cf.slash_reach - 30)) {
                                consider({}, A_NONE, {}, wait, u);
                            }
                        }
                    }
                }
            }
        }

        // Immediate Melee (slash/kick at enemy) — skip zero-movement dashCancel unless actually in range!
        if ((freeSt || dashCancel) && nearestDist < 4800.0 && focus >= 0) {
            const bool zeroStepDashCancel = (dashCancel && o.me().timer == cf.dash_t + cf.dash_post);
            Candidate tmp;
            tmp.act = A_MELEE;
            tmp.aim = {1.0, 0.0};
            const SelfPath mp = simulateSelf(tmp);
            const int hit = int(armed ? cf.slash_pre : cf.kick_pre);
            if (!zeroStepDashCancel || (predBody(focus, hit, 0).pt() - mp.p[size_t(hit)]).len() < double(cf.slash_reach + 380)) {
                consider({}, A_MELEE, bestMeleeAim(mp, hit));
            }
        }

        if (freeSt && cf.recall && (o.my_dart().st == D_GROUND || o.my_dart().st == D_RISE)) {
            consider({}, A_RECALL);
        }

        // Two-segment dodge-then-recover trajectories
        if (canMove && N <= 4 && workCount < workCap - 24 && (now == 0 || cpuElapsedMs() < 6.0)) {
            for (int b = 0; b < 5; ++b) {
                if (beam[size_t(b)].score < -1e15) continue;
                const V2 m0 = beam[size_t(b)].move;
                const V2 left = (m0.sq() > 0.1) ? m0.perp() : V2{0.0, 1.0};
                const V2 turns[4] = {left, left * -1.0, (home - here).unit(), {0.0, 0.0}};
                for (int ti = 0; ti < 4; ++ti) {
                    consider(m0, beam[size_t(b)].act, beam[size_t(b)].aim, -1, -1, 4, turns[ti]);
                }
            }
        }

        // Stage 2: Exact dart::Game close-quarters micro-exchange refinement!
        if (collectPool && s2Pool.size() >= 2 && (now == 0 || cpuElapsedMs() < 7.0)) {
            // Prune candidates far below best Stage-1 score before running exact engine rollouts
            std::vector<Candidate> pruned;
            pruned.reserve(s2Pool.size());
            for (const auto& c : s2Pool) {
                if (c.score >= best.score - 28.0) pruned.push_back(c);
            }
            if (pruned.size() >= 2) refineWithEngine(pruned, best);
        }

        // Recalibrate final throw aim from actual chosen move trajectory
        if (best.act == A_THROW && best.move.sq() > 0.01) {
            const SelfPath sp = simulateSelf(best);
            const ShotEval recal = computeBestShot(sp.p[size_t(cf.throw_pre)]);
            if (recal.target >= 0 && recal.ev > 0.0) best.aim = recal.dir;
        }

        if (best.score > -1e15) {
            out.mx = std::clamp<i64>(iround(best.move.x * 1000.0), -1000000, 1000000);
            out.my = std::clamp<i64>(iround(best.move.y * 1000.0), -1000000, 1000000);
            out.act = best.act;
            out.ax = std::clamp<i64>(iround(best.aim.x * 1000.0), -1000000, 1000000);
            out.ay = std::clamp<i64>(iround(best.aim.y * 1000.0), -1000000, 1000000);
            if (best.move.sq() > 0.01) prevMove = best.move.unit();
        }
        selfHist[size_t(now % NV_HIST)].act = out;
        return out;
    }
};

DART_MAIN(NeonVortex)
