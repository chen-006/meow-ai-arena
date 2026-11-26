// 伏黑甚尔 · 回旋镖大作战
// 只使用走位、持镖手刀和冲刺。按 200 毫秒延迟重建对手和飞镖。
#include "dart.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

namespace {
using namespace dart;

struct Point {
    i64 x = 0, y = 0;
    Point operator+(Point p) const { return {x + p.x, y + p.y}; }
    Point operator-(Point p) const { return {x - p.x, y - p.y}; }
};

template <typename T>
Point position(const T& object) { return {object.x, object.y}; }

double distance(Point start, Point end = {}) {
    const Point d = end - start;
    return std::hypot(double(d.x), double(d.y));
}

Point direction(Point d) {
    Point unit;
    scale_to(d.x, d.y, UNIT, unit.x, unit.y);
    return unit;
}

Action move(Point d) { return {d.x, d.y}; }
Action act(Act kind, Point d) { return {0, 0, kind, d.x, d.y}; }

struct ThreatSegment {
    Point from, to;
    int weight;
};

struct ThrowPaths {
    static constexpr int horizon = 12;
    std::array<std::vector<ThreatSegment>, horizon> segments;
    i64 radius;

    int risk(int tick, Point from, Point to) const {
        if (tick < 0 || tick >= horizon) return 0;
        int peak = 0, combined = 0;
        for (const ThreatSegment& shot : segments[size_t(tick)]) {
            const Point a = shot.from - from;
            const Point b = shot.to - to;
            // A segment with both endpoints outside a face of the expanded box
            // cannot enter either of the two circular risk bands.
            const i64 limit = radius + 220;
            if ((a.x > limit && b.x > limit) || (a.x < -limit && b.x < -limit) ||
                (a.y > limit && b.y > limit) || (a.y < -limit && b.y < -limit)) continue;
            if (seg_near_origin(a.x, a.y, b.x, b.y, radius)) {
                peak = std::max(peak, shot.weight);
                combined += shot.weight;
            } else if (seg_near_origin(a.x, a.y, b.x, b.y, radius + 220)) {
                peak = std::max(peak, shot.weight / 3);
                combined += shot.weight / 3;
            }
        }
        return peak + std::min(peak / 2, (combined - peak) / 12);
    }
};

constexpr std::array<Point, 16> directions{{
    {1000, 0}, {924, 383}, {707, 707}, {383, 924},
    {0, 1000}, {-383, 924}, {-707, 707}, {-924, 383},
    {-1000, 0}, {-924, -383}, {-707, -707}, {-383, -924},
    {0, -1000}, {383, -924}, {707, -707}, {924, -383},
}};

// All positions are expressed relative to the current opponent and terrain.
using DuelFeatures = std::array<float, 32>;
static DuelFeatures duel_features(const Game& g, int seat, int target) {
    const auto& me = g.p[seat];
    const auto& foe = g.p[target];
    const auto& dart = g.d[target];
    const auto& rules = g.cf;
    const Map& map = g.live ? *g.live : g.M();
    const double dx = foe.x - me.x, dy = foe.y - me.y;
    const double gap = std::max(1.0, std::hypot(dx, dy));
    const double nx = dx / gap, ny = dy / gap;
    const double vx = me.vx - foe.vx, vy = me.vy - foe.vy;
    const auto sat = [](double x) -> float { return float(std::clamp(x, -1.0, 1.0)); };
    const auto ground = [&](double x, double y) {
        return map.at(cell_index(i64(x)), cell_index(i64(y))) != C_PIT;
    };
    const auto rear_room = [&](const Player& p, double ax, double ay) {
        int open = 0;
        for (int step : {500, 1050, 1750, 2450}) open += ground(p.x + step * ax, p.y + step * ay);
        return open * 0.25f;
    };
    const double lateral = (me.vx * ny - me.vy * nx >= 0) ? 1.0 : -1.0;
    const bool armed = dart.st == D_HAND;
    const double dart_gap = std::hypot(double(dart.x - me.x), double(dart.y - me.y));
    const double return_gap = std::hypot(double(dart.x - foe.x), double(dart.y - foe.y));
    const auto recovery = [](const Player& p) { return p.st == S_FREE ? 0. : double(p.timer); };
    const i64 fall = wave_fall_step(rules,g.M().wave(cell_index(me.x),cell_index(me.y)));
    return {{
        sat(gap / 10000.), sat((gap-rules.slash_reach) / 2500.),
        sat((vx*nx + vy*ny) / 600.), sat(std::abs(vx*ny-vy*nx) / 600.),
        sat((me.vx*nx+me.vy*ny)/600.), sat((foe.vx*nx+foe.vy*ny)/600.),
        float(armed), float(dart.st==D_OUT), float(dart.st==D_BACK), float(dart.st==D_GROUND),
        armed ? 0.f : sat(dart_gap / 7500.), armed ? 0.f : sat(return_gap / 7500.),
        armed ? 0.f : sat((dart_gap-return_gap)/4000.),
        sat(me.cd/double(std::max<i64>(1,rules.dash_cd))), sat(foe.cd/double(std::max<i64>(1,rules.dash_cd))),
        sat(recovery(me)/12.), sat(recovery(foe)/12.),
        float(me.st==S_STUN), float(foe.st==S_STUN), float(me.st==S_DASH), float(foe.st==S_DASH),
        float(me.expose==0 && map.at(cell_index(me.x),cell_index(me.y))==C_GRASS),
        float(foe.expose==0 && map.at(cell_index(foe.x),cell_index(foe.y))==C_GRASS),
        float(wall_between(map, me.x, me.y, foe.x, foe.y)),
        rear_room(me,-nx,-ny), rear_room(foe,nx,ny),
        rear_room(me,ny*lateral,-nx*lateral), rear_room(me,-ny*lateral,nx*lateral),
        fall < 0 ? 1.f : sat((fall-g.t)/80.),
        sat((me.x-map.width()/2.)*nx/6000.+(me.y-map.height()/2.)*ny/6000.),
        sat((g.t-rules.col_start)/100.), sat((gap-rules.dash_v*rules.dash_t)/3500.)
    }};
}

// A compact position evaluator fitted to local duel outcomes.
static constexpr float value_w0[16][32] = {
        {0.317404f, 0.382261f, 0.287545f, -0.210295f, -0.200557f, -0.500576f, 0.020555f, 0.358442f, 0.672211f, 0.205344f, -0.495570f, -0.535914f, 0.146475f, 0.355694f, -0.579108f, -0.291146f, 0.069750f, -0.442179f, 0.137240f, -0.191202f, 0.136600f, 0.161923f, -0.334875f, 0.371623f, 0.012577f, 0.080574f, -0.088315f, -0.115813f, 0.006436f, 0.303578f, -0.213412f, 0.094797f},
        {-0.085141f, 0.158431f, 0.221747f, -0.249222f, -0.182669f, 0.137605f, -0.235197f, 0.218595f, 0.118761f, 0.034360f, -0.319817f, 0.055452f, -0.089848f, -0.088444f, 0.048226f, 1.397018f, -0.037134f, -0.095185f, 0.139446f, -0.783370f, -0.119618f, 0.154410f, -0.069281f, -0.305098f, -0.105215f, -0.098262f, 0.062979f, 0.130853f, -0.003322f, 0.123678f, 0.090868f, -0.474773f},
        {-0.132069f, 0.066184f, 0.548744f, 0.095523f, -0.650251f, -1.305032f, 0.173710f, -0.138595f, -0.070753f, -0.217379f, 0.258182f, -0.001137f, -0.058907f, -0.170912f, 0.330841f, 0.339358f, 0.251322f, 0.107768f, -0.109124f, -0.058103f, 0.417542f, -0.326728f, -0.326438f, 0.143049f, -0.133534f, -0.048241f, -0.046992f, 0.031382f, -0.191161f, -0.160757f, -0.032209f, 0.006190f},
        {-0.319873f, -0.223974f, 0.173780f, -0.273732f, 0.143677f, -0.035518f, 0.066186f, 0.171975f, 0.086794f, 0.343499f, 0.382893f, 0.667670f, -0.526766f, -0.136979f, -0.322976f, -0.318218f, 0.479319f, -0.017105f, 0.068019f, 0.068858f, 0.384050f, 0.622456f, 0.004313f, 0.205348f, 0.124193f, -0.047622f, 0.019677f, -0.004068f, -0.097440f, 0.035163f, 0.107414f, -0.460737f},
        {-0.038149f, 0.548625f, 0.033826f, 0.069214f, -0.025276f, -0.321149f, 0.074208f, -0.129291f, -0.524099f, 0.353452f, 0.245412f, -0.077390f, 0.135794f, -0.533374f, 0.094125f, 0.253815f, 0.172105f, 0.120945f, 0.036463f, -0.407655f, 0.020809f, -0.652377f, -0.321312f, -0.329478f, -0.078204f, 0.035524f, 0.080595f, 0.062007f, 0.290528f, -0.186864f, -0.057822f, -0.182369f},
        {0.192236f, 0.071285f, 0.025082f, -0.750507f, -0.169500f, -0.583720f, -0.830965f, -0.804380f, -0.700002f, 0.128770f, -0.014078f, -0.329721f, 0.097237f, -0.418139f, -0.307472f, -0.190877f, 0.064881f, 0.264015f, -0.041962f, 0.280675f, -0.988640f, -0.502160f, -0.027989f, -0.098466f, 0.264506f, 0.418714f, -0.134550f, -0.126491f, -0.189418f, 0.381136f, -0.453722f, 0.255853f},
        {-0.132303f, 0.021052f, 0.285698f, 0.012302f, -0.026000f, -0.033177f, 0.277003f, -0.550350f, 0.580232f, 0.194325f, -0.365865f, -0.957960f, 0.750689f, -0.295703f, 0.390302f, -0.006848f, 0.090138f, -0.059144f, -0.099523f, 0.079535f, 0.104651f, 0.239514f, 0.069993f, -0.201136f, -0.010289f, 0.242057f, 0.128135f, -0.017616f, -0.274417f, -0.060072f, 0.040531f, -0.401859f},
        {-0.488042f, 0.127086f, 0.269245f, -0.344655f, -0.313003f, 0.077791f, -0.152417f, 0.200952f, -0.344666f, 0.070030f, 0.328135f, 0.161209f, 0.168114f, -0.017779f, -0.441153f, 0.393033f, 0.645905f, 0.055329f, -0.158893f, -0.010967f, -0.660006f, -0.130029f, 0.148505f, -0.572028f, 0.517359f, -0.154551f, 0.101656f, 0.038595f, -0.258247f, 0.318962f, 0.017650f, -0.197535f},
        {0.089591f, 0.229347f, -0.121550f, 0.103411f, -0.430199f, -0.178824f, -0.391248f, -0.323359f, -0.123468f, -0.131994f, 0.108146f, -0.030142f, -0.053202f, 0.064733f, -0.024233f, -0.123501f, -0.061864f, 0.990188f, 0.062190f, -0.296483f, -0.018617f, 0.237397f, 0.364785f, -0.157603f, -0.898914f, -0.070928f, -0.107243f, -0.106772f, -0.154068f, 0.045921f, -0.139562f, -0.014687f},
        {-0.566235f, 0.214938f, -0.032733f, -0.592406f, -0.137652f, 0.204948f, 0.015705f, -0.133710f, -0.220816f, 0.189272f, 0.288104f, 0.439747f, -0.698702f, -0.141190f, 0.157543f, 0.703482f, 0.016208f, -0.039398f, -0.074207f, 0.474507f, -0.742204f, 0.362953f, -0.006867f, -0.107689f, -0.037603f, -0.000693f, -0.071835f, -0.055064f, 0.058033f, 0.058442f, -0.091845f, -0.167837f},
        {0.049265f, -0.045936f, 0.153237f, -0.084659f, 0.041931f, -0.147351f, 0.161378f, 0.175873f, -1.315006f, 0.315626f, -0.733639f, -0.432993f, -0.559167f, 0.231149f, 0.246418f, 0.381732f, 0.054282f, 0.337680f, -0.120398f, -0.216304f, 0.166060f, -0.603347f, -0.045027f, -0.098223f, 0.109834f, -0.039255f, 0.239789f, 0.322095f, -0.000748f, -0.006357f, 0.151347f, -0.416718f},
        {-0.009422f, 0.251344f, -0.028224f, -0.259645f, 0.133247f, -0.019152f, -0.220496f, 0.646964f, -0.411535f, -0.130810f, -1.332299f, -1.313619f, -0.018640f, 0.019026f, -0.056266f, 0.117143f, -0.530712f, 0.517179f, -0.016001f, 0.182032f, -0.014426f, -0.659936f, 0.099449f, -0.147445f, 0.135262f, -0.015657f, -0.073862f, -0.103310f, 0.198259f, -0.071008f, -0.000175f, -0.501273f},
        {-0.153829f, -0.012151f, 0.114567f, -0.072930f, -0.000372f, -0.182367f, 0.223196f, -0.510343f, 0.117345f, 0.020859f, -0.121248f, 0.010691f, -0.025961f, -0.052219f, 0.111402f, -1.078023f, -0.187243f, 0.894803f, 0.078558f, 0.561328f, -0.019363f, -0.519931f, -0.522170f, 0.143656f, -0.138088f, 0.013465f, -0.385344f, -0.359846f, 0.141689f, 0.034454f, 0.276362f, -0.221270f},
        {0.113216f, 0.648770f, -0.256788f, 0.135211f, 0.136234f, 0.373500f, -0.035266f, -0.327521f, -0.033537f, -0.137739f, 0.029534f, 0.103257f, -0.037667f, 0.111882f, 0.081359f, -1.137269f, -0.074966f, 0.260201f, -0.070203f, -0.033389f, 0.028332f, -0.578892f, 0.175994f, 0.204508f, 0.191194f, 0.011079f, 0.057718f, -0.042576f, 0.026140f, -0.066790f, -0.067943f, 0.544067f},
        {-0.015469f, -0.106793f, -0.059625f, 0.116341f, 0.113426f, 0.075621f, 0.013063f, 0.083807f, -0.001052f, -0.058998f, -0.114654f, -0.140550f, -0.071391f, 0.012474f, -0.041638f, -0.117350f, -0.021069f, 0.001714f, -0.003883f, -0.197945f, -0.135159f, -0.154019f, 0.012508f, 0.037697f, -0.023592f, -0.050264f, -0.074418f, -0.120402f, -0.014734f, 0.013985f, -0.097474f, 0.002477f},
        {0.169751f, -0.145634f, -0.216931f, 0.090165f, 0.206688f, -0.217636f, -0.205133f, 0.579030f, 0.004932f, 0.320014f, -0.240769f, 0.258606f, -0.242309f, -0.340378f, 0.136548f, -1.150671f, 0.480222f, 0.412009f, 0.071511f, 0.924683f, 0.179825f, 0.014572f, 0.102024f, 0.064460f, 0.154310f, -0.149343f, 0.132001f, 0.302573f, -0.070343f, 0.213344f, 0.261602f, 0.234582f}
    };
static constexpr float value_b0[16] = {-0.174663f, 0.127574f, 0.147931f, 0.069186f, 0.013629f, -0.197448f, 0.130750f, -0.081454f, -0.060414f, -0.090088f, -0.011100f, 0.082599f, 0.311795f, 0.051319f, -0.117075f, -0.096230f};
static constexpr float value_w1[12][16] = {
        {0.193994f, -0.521412f, -0.216057f, 0.086055f, 0.436574f, 0.079235f, -0.358561f, 0.250093f, -1.068588f, 0.244005f, 0.110949f, -1.020629f, 0.034414f, 0.012407f, -0.070781f, 0.305994f},
        {-0.195204f, 0.064186f, -0.064068f, 0.108194f, -0.077845f, -0.020554f, -0.113653f, -0.186922f, -0.016731f, -0.236278f, -0.078485f, 0.106936f, 0.053424f, -0.173266f, -0.129221f, -0.056671f},
        {0.196042f, 0.647645f, 0.087231f, -0.148490f, 0.122189f, -0.024436f, -0.122094f, 0.558682f, 0.496837f, -0.271561f, 0.081436f, 0.538967f, 0.365638f, 0.011937f, -0.194351f, -0.514563f},
        {0.086393f, -0.250544f, -0.090431f, 0.076834f, 0.212964f, 0.049444f, -0.223701f, 0.121566f, 1.198046f, 0.136579f, 0.072407f, -0.656402f, 0.482453f, -0.036125f, -0.050430f, 0.130358f},
        {-0.127344f, -0.070587f, 0.081878f, -0.032282f, 0.099102f, -0.109934f, 0.275717f, -0.122530f, 0.493676f, -0.068831f, -0.264581f, -0.044240f, 0.593301f, 0.014197f, 0.038639f, -0.370604f},
        {0.220650f, -0.153430f, -0.282295f, -0.532304f, 0.170714f, 0.120021f, -0.270047f, -0.242528f, 1.381980f, -0.022317f, -0.017163f, -0.141479f, 1.016433f, -0.092997f, -0.216904f, 0.333517f},
        {-0.287482f, -0.392768f, -0.420582f, 0.384415f, -0.266600f, 0.225562f, 0.310451f, 0.287861f, -0.266380f, -0.344434f, 0.258347f, 0.332026f, -0.701201f, -0.857602f, 0.066458f, -0.393517f},
        {0.200554f, 0.357264f, -1.052541f, -0.157531f, -0.181283f, 0.741983f, -0.498755f, -0.156542f, -0.628583f, -0.190360f, 0.334336f, -0.081472f, 0.661108f, -0.071103f, 0.089972f, 0.141476f},
        {0.007103f, 0.142591f, -0.266428f, -0.802988f, -0.098189f, 0.227595f, 0.286468f, 0.261970f, -0.572653f, -0.607864f, 0.256801f, 0.829749f, -0.486189f, -0.348996f, -0.146457f, 0.012835f},
        {-0.025104f, -0.539026f, -0.106935f, 0.378621f, -0.493083f, -0.100023f, 0.250512f, 0.046445f, -0.157615f, -0.647314f, 0.115501f, -0.033199f, -0.341699f, -0.749020f, -0.205405f, -0.900050f},
        {-0.091214f, -0.163297f, -0.048281f, -0.089020f, 0.330456f, 0.292511f, -0.620632f, 0.297396f, -0.946440f, 0.024923f, 0.485756f, 0.015655f, -0.210985f, -0.488931f, 0.174607f, -0.332524f},
        {-0.495420f, 0.229904f, 0.069547f, -0.036713f, 0.009959f, 0.242691f, 0.121680f, -0.388836f, 1.053533f, 0.028034f, -0.232454f, 0.309225f, 0.745268f, 0.047803f, 0.001492f, 0.214116f}
    };
static constexpr float value_b1[12] = {0.165089f, -0.228580f, 0.082474f, 0.096531f, 0.294453f, -0.022062f, 0.177759f, -0.175045f, 0.182372f, 0.218530f, 0.211139f, 0.213815f};
static constexpr float value_w2[3][12] = {
        {0.184057f, -0.099618f, -0.305544f, 0.035236f, -0.039877f, -0.016825f, 0.658570f, 0.541113f, -0.626377f, 0.641821f, 0.295184f, -0.305093f},
        {0.358851f, 0.146279f, -0.331408f, -0.051099f, -0.166400f, 0.216457f, 0.554582f, 0.748173f, -0.643552f, 0.868656f, 0.308831f, -0.421172f},
        {-0.191329f, 0.216124f, 0.010580f, 0.382328f, 0.026483f, 0.056243f, -0.018356f, -0.109972f, -0.018711f, -0.039217f, 0.009763f, 0.040971f}
    };
static constexpr float value_b2[3] = {0.019608f, 0.040415f, 0.008638f};
template <size_t N, size_t M>
static std::array<float, M> evaluate_layer(const std::array<float, N>& x,
                                         const float (&weights)[M][N], const float (&bias)[M]) {
    std::array<float, M> y{};
    for (size_t j = 0; j < M; ++j) {
        float sum = bias[j];
        for (size_t i = 0; i < N; ++i) sum += x[i] * weights[j][i];
        y[j] = sum;
    }
    return y;
}
static std::array<float, 3> evaluate_position(const Game& g, int seat, int target) {
    auto first = evaluate_layer(duel_features(g,seat,target), value_w0, value_b0);
    for (auto& value : first) value = std::max(value,0.f);
    auto second = evaluate_layer(first, value_w1, value_b1);
    for (auto& value : second) value = std::max(value,0.f);
    return evaluate_layer(second, value_w2, value_b2);
}


class TojiFinal {
public:
  void init(const GameInfo &info) {
      info_ = &info;
      if (info.cfg.mode == M_DUEL) index_grass(info.M());
  }

    Action decide(const Obs& obs) {
        if (!obs.me().alive) return {};
        const int seat = obs.seat;
        remember(obs);
        const Player &own = obs.me();
        const auto &cfg = info_->cfg;
        const bool can_cancel = cfg.dash_slash && own.st == S_DASH && own.timer > cfg.dash_post &&
                                own.timer <= cfg.dash_post + cfg.dash_t;
        // Keep tracking the rival during recovery. Physics ignores movement
        // and attacks in these frames, so there is no action to search.
        if (own.st != S_FREE && !can_cancel) {
            for (int k = 0; k < obs.n; ++k) {
                if (k == seat || !obs.p[k].vis) continue;
                const Player &foe = obs.p[k];
                cache_motion(obs, k);
                if (foe.alive) opponent_motion_[k] = {obs.t, {foe.vx, foe.vy}};
            }
            return {};
        }
        Inputs inputs{}, estimate_inputs{};
        for (int k = 0; k < obs.n; ++k) {
            if (k == seat || !obs.p[k].vis) continue;
            const auto& player = obs.p[k];
            auto& previous = opponent_motion_[k];
            const Config& cfg = info_->cfg;
            const Point velocity{player.vx, player.vy};
            Point forecast = velocity;
            if (player.st == S_FREE) {
                // The most recent visible velocity can still be four ticks old.
                // Armed opponents often turn during that gap to find a shot.
                if (obs.d[k].st == D_HAND && obs.t >= cfg.delay && previous.tick == obs.t - 1) {
                    const Point acceleration = velocity - previous.velocity;
                    forecast = velocity + Point{rdiv(acceleration.x, 2), rdiv(acceleration.y, 2)};
                }
                cap_len(forecast.x, forecast.y, cfg.run_v);
            }
            // A rival that keeps the dart through several clear shots often turns to
            // contest the blade. Switch motion forecasts until the first observed throw.
            if (player.st == S_FREE && cfg.delay == 4 && obs.t >= cfg.delay &&
                close_combat(k)) {
                const Read r = read_rival(obs,k);
                const auto y = forecast_rival(r.x);
                forecast = {i64((r.axis.x*y[0] - r.axis.y*y[1])*cfg.run_v/UNIT),
                            i64((r.axis.y*y[0] + r.axis.x*y[1])*cfg.run_v/UNIT)};
                cap_len(forecast.x, forecast.y, cfg.run_v);
            }
            const auto motion = obs.n == 2 && showed_blade_[k] && close_combat(k)
                ? blade_velocities(obs,k,forecast) : predicted_velocities(obs,k,forecast);
            const Point reconstructed = motion.first;
            const Point future = motion.second;
            cache_motion(obs,k);
            if (player.alive) previous = {obs.t, velocity};
            estimate_inputs[k] = move({reconstructed.x * UNIT / cfg.run_v, reconstructed.y * UNIT / cfg.run_v});
            inputs[k] = move({future.x * UNIT / cfg.run_v, future.y * UNIT / cfg.run_v});
        }

        // Hidden rivals remain on one connected patch while their actions and
        // dart are unexposed. Only use a position estimate for a compact patch.
        Obs bounded = obs;
        if (obs.n == 2 && !obs.p[1 - seat].vis && obs.p[1 - seat].alive && obs.d[1 - seat].st == D_HAND)
            locate_in_cover(bounded, 1 - seat);
        Game now = estimate(bounded, estimate_inputs);
        const Player& me = now.p[seat];
        const bool armed = now.d[seat].st == D_HAND;
        const int target = choose_target(now, seat);
        // An empty-handed rival near the rim of our slash may already be
        // turning outside it. Only postpone if two observation-consistent
        // alternatives miss and our own movement is still entirely free.
        const auto fading_slash = [&](const Strike& strike) {
            if (strike.value != kill_value || me.st != S_FREE || now.n != 2 || target < 0 ||
                !showed_throw_[target] ||
                now.d[target].st != D_GROUND || obs.d[target].st != D_GROUND ||
                obs.p[target].st != S_FREE || now.p[target].st != S_FREE) return false;
            const Point from = position(obs.p[target]);
            const Point self = position(me);
            const Point foe = position(now.p[target]);
            if (distance(self, foe) < now.cf.slash_reach - 270 ||
                distance(self, foe) > now.cf.slash_reach + 620) return false;
            const Point v{obs.p[target].vx, obs.p[target].vy};
            const Point run = direction(v);
            const Point retreat = direction(from - self);
            const Point pickup = direction(position(obs.d[target]) - from);
            if (distance(v) < .42 * now.cf.run_v || (retreat.x * run.x + retreat.y * run.y) < -550000)
                return false;
            int escapes = 0;
            for (Point wish : {run, direction({2 * run.x + retreat.x + pickup.x,
                                               2 * run.y + retreat.y + pickup.y})}) {
                Inputs history = estimate_inputs;
                history[target] = move(wish);
                Game alternate = estimate(obs, history);
                if (!alternate.p[target].alive) continue;
                for (i64 tick = 0; tick < now.cf.slash_pre && !alternate.over; ++tick) {
                    advance(alternate, history, seat, tick == 0 ? strike.action : Action{});
                }
                if (alternate.p[target].alive && alternate.p[seat].alive) ++escapes;
            }
            return escapes == 2;
        };
        if (armed) {
            const Strike strike = best_strike(now, inputs, seat);
            if (strike.value > 0) {
                const Landing check = inspect_landing(now, seat, strike.action);
                if (check.safe && !check.crowded_edge &&
                    (strike.value >= kill_value || (!check.collapse_trap && !parry_exposes_pit(now, inputs, seat, strike.action)))) {
                    if (!fading_slash(strike)) return strike.action;
                }
            }
        }
        if (me.st != S_FREE) {
            const Action brake = cancel_dangerous_dash(now, inputs, seat);
            if (brake.act != A_NONE) return brake;
            if (armed && now.cf.dash_slash && now.dash_moving(me) &&
                !safe_commitment(inspect_landing(now, seat, {}))) {
                const Action cancel = act(A_MELEE, {me.ax, me.ay});
                const Landing check = inspect_landing(now, seat, cancel);
                if (safe_commitment(check)) return cancel;
            }
            if (armed && now.cf.dash_slash && now.dash_moving(me)) {
                const Action avoid = evade_during_dash(obs, now, inputs, seat);
                if (avoid.act != A_NONE) return avoid;
            }
            return {};
        }

        Point goal{now.M().width() / 2, now.M().height() / 2};
        if (target >= 0) goal = position(now.p[target]);
        if (now.d[seat].st == D_GROUND) goal = position(now.d[seat]);

        const Point opponent_goal = goal;
        const i64 dash_ticks = now.cf.dash_pre + now.cf.dash_t + now.cf.dash_post;
        const i64 safe_until = now.t + std::max<i64>(9, dash_ticks + 2);
        const Map floor = live_map(now.cf, now.M(), safe_until);
        const ThrowPaths possible_shots = estimate_unseen_throws(bounded, now, floor);
        if (target >= 0) {
            const Player& enemy = now.p[target];
            if (enemy.st == S_FREE && now.d[target].st != D_HAND) {
                Point drift = position(enemy);
                i64 vx = enemy.vx, vy = enemy.vy;
                for (int tick = 0; tick < 4; ++tick) {
                    Point next = drift;
                    move_body(floor, now.cf.body_r, next.x, next.y, vx, vy);
                    if (!walkable(floor, next)) break;
                    drift = next;
                }
                goal = drift;
            }
        }
        // FFA is won by surviving. While two rivals can fight each other, choose
        // terrain where neither is likely to designate us as the nearest opponent.
        int visible_foes = 0;
        for (int k = 0; k < now.n; ++k)
            if (enemy(now, seat, k)) ++visible_foes;
        if (now.cf.mode == M_FFA && visible_foes >= 2) {
            if (obs.t >= refuge_tick_ + 7 || !walkable(floor, refuge_)) {
                std::array<double, MAXP> other_gap{};
                for (int k = 0; k < now.n; ++k)
                    if (enemy(now, seat, k)) {
                        other_gap[k] = 12000;
                        for (int j = 0; j < now.n; ++j)
                            if (j != k && enemy(now, seat, j) && !now.mates(j, k))
                                other_gap[k] =
                                    std::min(other_gap[k], distance(position(now.p[k]), position(now.p[j])));
                    }
                const Point feet = position(me);
                refuge_ = feet;
                double lowest = std::numeric_limits<double>::infinity();
                for (int y = 1; y < floor.ny; y += 2)
                    for (int x = 1; x < floor.nx; x += 2) {
                        const Point p{x * CELL + CELL / 2, y * CELL + CELL / 2};
                        if (!walkable(floor, p) || circle_hits_wall(floor, now.cf.body_r, p.x, p.y)) continue;
                        const double travel = distance(feet, p);
                        if (travel > 7400) continue;
                        const i64 falls = wave_fall_step(now.cf, now.M().wave(x, y));
                        if (falls >= 0 && falls < now.t + 30 + travel / now.cf.run_v) continue;
                        const bool grass = floor.at(x, y) == C_GRASS;
                        double cost = .33 * travel +
                                      .12 * distance(p, {floor.width() / 2, floor.height() / 2}) +
                                      1.2 * edge_cost(floor, p);
                        for (int k = 0; k < now.n; ++k)
                            if (enemy(now, seat, k)) {
                                const Point foe = position(now.p[k]);
                                const double gap = distance(p, foe);
                                const double exposed = grass && gap > now.cf.see_r                   ? .40
                                                       : wall_between(floor, foe.x, foe.y, p.x, p.y) ? .60
                                                                                                     : 1.;
                                const double open = std::clamp((now.cf.range + 1200 - gap) / 3500., 0., 1.);
                                const double focus = std::clamp((other_gap[k] + 1250 - gap) / 2500., 0., 1.);
                                cost += exposed * open * (1500 + 3000 * focus);
                                cost += 1900 * std::clamp((3000 - gap) / 1900., 0., 1.);
                            }
                        if (cost < lowest) {
                            lowest = cost;
                            refuge_ = p;
                        }
                    }
                refuge_tick_ = obs.t;
            }
            if (walkable(floor, refuge_)) goal = refuge_;
        }
        const Point feet = position(me);
        const i64 fall_at = wave_fall_step(now.cf, now.M().wave(cell_index(feet.x),cell_index(feet.y)));
        const bool evacuating = fall_at >= 0 && fall_at <= now.t + 30;
        if (evacuating) {
            // A safe dash landing can still be on an island that vanishes one
            // wave later. Look for a reachable tile beyond the next collapse.
            const Map& current = now.LM();
            const Route from_here(current, feet);
            double nearest = std::numeric_limits<double>::infinity();
            for (int y = 0; y < floor.ny; ++y) for (int x = 0; x < floor.nx; ++x) {
                const Point refuge{x * CELL + CELL / 2, y * CELL + CELL / 2};
                if (!walkable(floor,refuge)) continue;
                const i64 until = wave_fall_step(now.cf,now.M().wave(x,y));
                if (until >= 0 && until <= fall_at + now.cf.cevery()) continue;
                const double cost = from_here.cost(refuge) + 0.10 * distance(refuge,opponent_goal);
                if (cost < nearest) {
                    nearest = cost;
                    goal = refuge;
                }
            }
        }
        const Point center{now.M().width() / 2, now.M().height() / 2};
        const bool stalled =
            target >= 0 && now.n == 2 && !evacuating && obs.p[target].vis &&
            now.t >= now.cf.cstart() + now.cf.cevery() * 3 && now.d[target].st == D_GROUND &&
            obs.d[target].st == D_GROUND && obs.p[target].st == S_FREE && now.p[target].st == S_FREE &&
            floor.at(cell_index(center.x), cell_index(center.y)) == C_PIT && distance(feet, center) < 6000 &&
            distance(position(now.p[target]), center) < 6000;
        // With a grounded dart on the other side of a central pit, periodically
        // vary the gap so both runners do not follow the same arc indefinitely.
        const bool feint = stalled && ((now.t - now.cf.cstart()) % 60) < 18;
        if (feint) {
            const Point foe = position(now.p[target]);
            const Point foe_to_us = feet - foe;
            const Point line = direction(foe_to_us);
            const Point desired = foe + Point{line.x * 6, line.y * 6};
            goal = walkable(floor, desired) ? desired : feet;
        }
        const bool respect_kicks = target >= 0 && now.d[target].st != D_HAND &&
            obs.d[target].st != D_HAND && now.p[target].st != S_RECALL &&
            distance(feet, position(now.p[target])) <= now.cf.kick_reach + 3200;
        const Route route(floor, goal);
        const Point origin = position(me);
        const Point direct = direction(route.goal - origin);
        const bool nearing_collapse = evacuating || !walkable(floor, origin);
        const double initial_exposure = crowd_exposure(now, floor, seat);
        const bool aimed_at = target >= 0 && now.d[target].st == D_HAND &&
            (now.p[target].st == S_FREE || now.p[target].st == S_THROW);
        const double enemy_distance = target < 0 ? 1e9 : distance(origin, opponent_goal);
        const double lateral_scale = std::clamp((enemy_distance - 2200) / 1900, 0.0, 1.0) *
            std::clamp((9000 - enemy_distance) / 2500, 0.0, 1.0);
        const Point axis = direction(opponent_goal - origin);
        constexpr int weave_period = 14;
        constexpr int weave_weight = 1700;
        const int phase = int(now.t / weave_period) + seat;
        // Change the angle of approach while the opponent can throw.
        auto lateral_bonus = [&](Point delta) {
            if (!aimed_at || nearing_collapse) return 0.0;
            const double cross = double(delta.x * axis.y - delta.y * axis.x) / UNIT;
            const double sideways = std::min(1000.0, std::abs(cross)) / 1000;
            const double sign_bonus = (cross > 0) == (phase % 2 == 0)
                ? 1.0 : 0.55;
            return weave_weight * sideways * sign_bonus * lateral_scale;
        };
        auto cover_bonus = [&](const Player& end) {
            if (!aimed_at || nearing_collapse ||
                enemy_distance > now.cf.range + 1200) return 0.0;
            const Point landed = position(end);
            const Point enemy_pos = position(now.p[target]);
            const double d = distance(enemy_pos, landed);
            if (d < now.cf.see_r) return 0.0;
            const bool grass = end.expose == 0 &&
                cell_of_point(now.cf, now.M(), end.x, end.y, now.t + 5) == C_GRASS;
            const bool shielded = wall_between(floor, enemy_pos.x, enemy_pos.y, end.x, end.y);
            constexpr double strength = 1.55;
            return strength * (grass ? 1000 : shielded ? 800 : 0);
        };
        // With an armed opponent, leave room to sidestep their next shot.
        // The spacing bonus fades inside slash range and stops growing outside dart range.
        const bool blade_duel=now.n==2 && target>=0 && obs.p[target].vis &&
            !showed_throw_[target] && !nearing_collapse &&
            (close_combat(target) || (showed_blade_[target] && throw_opportunities_[target]>16.0));
        const auto blade_tempo = [&](Action action,const Game& next) {
            if (!blade_duel || next.over || enemy_distance<1000 || enemy_distance>6300) return 0.;
            const Player& us=next.p[seat];
            const Player& rival=next.p[target];
            const Point a=position(us),b=position(rival);
            if (wall_between(floor,a.x,a.y,b.x,b.y)) return 0.;
            const Config& c=next.cf;
            const double gap=distance(a,b);
            const bool locked=rival.st!=S_FREE && rival.st!=S_DASH && rival.timer>=2;
            const bool committed=next.dash_moving(rival);
            const double control = locked ? .20 : committed ? .76 : 1.;
            const bool cooling=now.p[target].cd>=5;
            const double desired=c.slash_reach+((cooling||locked)?190.:400.0);
            const double outside=std::clamp((gap-desired)/970.,-1.,1.);
            double score=1800.0 * control * std::max(0.,1.-std::abs(gap-desired)/2100.);
            score+=520.0 * control * std::min(0.,outside);
            if (us.st==S_SLASH || us.st==S_SLASHED) score-=0.65*520.0*control*std::max(0.,-outside);
            if (now.p[seat].cd<=0 && action.act==A_DASH) {
                const double window=std::clamp((4700.-gap)/2600.,0.,1.);
                if (cooling || locked) score+=2000.0 * window;
                else score-=2000.0 * .52 * window;
            }
            return score;
        };
        const auto spacing_bonus = [&](const Game& future) -> double {
            if (!aimed_at || nearing_collapse || future.over) return 0;
            const Point at = position(future.p[seat]);
            const double gap = distance(at, position(future.p[target]));
            const double room = std::max(0.0, std::min<double>(now.cf.range, gap) - now.cf.slash_reach);
            const double near_factor = std::clamp((gap - now.cf.slash_reach) / 1400., 0., 1.);
            return 2.0 * near_factor * room;
        };
        const bool facing_thrower = target >= 0 && showed_throw_[target];
        const auto initial_value = facing_thrower
            ? evaluate_position(now, seat, target) : std::array<float, 3>{};
        const auto kick_position_cost = [&](const Game& future) {
            if (!respect_kicks || future.over || future.d[target].st==D_HAND ||
                future.p[seat].st==S_STUN) return 0.0;
            const Point stand=position(future.p[seat]), rival=position(future.p[target]);
            const double gap=distance(stand,rival);
            if (gap>future.cf.kick_reach+2250 ||
                wall_between(floor,stand.x,stand.y,rival.x,rival.y)) return 0.0;
            double exposure=exposed_kick_angle(future,floor,stand,rival);
            if (exposure<=0) return 0.0;
            const auto& foe=future.p[target];
            if (foe.st==S_KICKED) exposure *= 0.2;
            return exposure * 2600 * std::clamp((future.cf.kick_reach+2250-gap)/2250,0.,1.);
        };
        const auto future_value = [&](const Game& future) -> double {
            if (future.over || !facing_thrower) return 0;
            const auto value = evaluate_position(future, seat, target);
            // Compare the predicted short- and medium-term outcomes with the
            // starting position. The third head estimates exposure to a fall.
            constexpr double medium_weight = 0.40;
            const double short_gain = double(value[0]) - initial_value[0];
            const double medium_gain = double(value[1]) - initial_value[1];
            const double fall_change = double(value[2]) - initial_value[2];
            const double gain = 11500 * ((1.0 - medium_weight) * short_gain + medium_weight * medium_gain)
                              - 14000 * fall_change;
            if (gain <= 0 || future.d[target].st == D_HAND) return gain;
            // An empty-handed rival can turn a promising approach into a fall.
            // Discount the positional gain if the simulated kick ends over a pit.
            const double danger = kick_landing_cost(future, floor, seat);
            const double exposure = std::clamp((danger - 500.) / 2200., 0., 1.);
            return gain * (1. - exposure);
        };
        constexpr int turns = 5;
        constexpr int turn_cost = 200;
        constexpr int pivot = 2;
        struct Prefix { Action first; Game state; double rank; int risk; };
        std::vector<Prefix> prefixes;
        double best_score = -std::numeric_limits<double>::infinity();
        Action best;
        Action last_surviving;
        double longest_survival = -1;

        auto consider = [&](Action action, const Game& projected, double bonus, int risk) {
            const Player& end = projected.p[seat];
            if (!end.alive) return;
            const Point landed = position(end);
            const double crossfire = projected.over ? 0.0 : crowd_exposure(projected, floor, seat) - initial_exposure;
            // In a team match the engine returns the winning team, in a duel or FFA the winning seat.
            const int our_side = projected.cf.mode == M_TEAM ? int(projected.p[seat].team) : seat;
            const bool winning = projected.over && projected.winner == our_side;
            const double score =
                bonus - (feint && action.act == A_DASH ? 2200 : 0) - crossfire +
                lateral_bonus(landed - origin) + cover_bonus(end) + spacing_bonus(projected) +
                blade_tempo(action, projected) + future_value(projected) -
                (evacuating ? 2.0 : 1.0) * route.cost(landed) - risk - edge_cost(floor, landed) -
                kick_landing_cost(projected, floor, seat) - kick_position_cost(projected) -
                ((action.act == A_DASH && aimed_at) ? armed_dash_cost : 0) - (end.st == S_STUN ? 3200 : 0) +
                (walkable(floor, landed) ? 0 : -5500) + (winning ? 25000 : 0);
            if (score <= best_score) return;
            const Landing check = inspect_landing(now, seat, action);
            if (!check.safe || (action.act == A_DASH && check.crowded_edge)) return;
            const double guarded_score = score - (check.crowded_edge ? crowded_edge_cost : 0) -
                (!winning && check.collapse_trap ? 12500 : 0);
            if (guarded_score > best_score) {
                best_score = guarded_score;
                best = action;
            }
        };

        auto followup = [&](Action first, const Game& trial, i64 approach_ticks, int risk) {
            if (!armed) return;
            const Strike strike = best_strike(trial, inputs, seat);
            if (strike.value == 0) return;
            Game stopped = trial;
            advance(stopped, inputs, seat, strike.action);
            for (i64 tick = 1; tick < trial.cf.slash_pre && !stopped.over; ++tick) {
                advance(stopped, inputs, seat);
            }
            if (!walkable(floor, position(stopped.p[seat]))) return;

            const bool finish = strike.value >= kill_value;
            double reward = finish ? 4000 : 650;
            constexpr int prize = 2200;
            if (!finish && target >= 0 && stopped.d[target].st == D_GROUND && !stopped.over) {
                const Point drop = position(stopped.d[target]);
                const Point foe = position(stopped.p[target]);
                const Point here = position(stopped.p[seat]);
                const Config& c = stopped.cf;
                const double pickup_ticks = std::max(0.,distance(drop,foe)-c.pick_r)/c.run_v;
                const double recall_ticks = c.recall_pre + distance(drop,foe)/std::max<i64>(1,c.recall_v);
                const double armed_again = std::min(pickup_ticks,recall_ticks);
                // Only gain tempo if the rival stays unarmed after our own slash recovery.
                const double head_start = std::clamp((armed_again - c.slash_post) / 10.,-1.,1.);
                const double approach = std::clamp((distance(foe, drop) - distance(here,drop)) / 2200.,-1.,1.);
                reward += prize * head_start + 0.3 * prize * approach;
            }
            const bool punish_empty_hand =
                finish && first.act == A_DASH && target >= 0 && now.d[target].st != D_HAND;
            consider(first, stopped, reward + (punish_empty_hand ? empty_hand_dash_reward : 0) -
                275 * approach_ticks, risk);
        };

        // Test safe approaches, including a short step followed by a swing.
        auto try_walk = [&](Point steering) {
            const Action action = move(steering);
            Game trial = now;
            int risk = 0;
            for (int tick = 1; tick <= 5 && !trial.over; ++tick) {
                const Point from = position(trial.p[seat]);
                advance(trial, inputs, seat, action);
                risk += projectile_risk(trial, seat) +
                    possible_shots.risk(tick - 1, from, position(trial.p[seat]));
                if (tick == 2) risk += int(0.75 * kick_landing_cost(trial, floor, seat));
                if (!trial.p[seat].alive) {
                    const double fallback_score = tick - double(risk) / 2000;
                    if (fallback_score > longest_survival &&
                        inspect_landing(now, seat, action).safe) {
                        longest_survival = fallback_score;
                        last_surviving = action;
                    }
                    return;
                }
                if (tick == pivot && !trial.over && enemy_distance < 9000) {
                    const Point at = position(trial.p[seat]);
                    constexpr int prefix_scale = 40;
                    const double rank = risk + route.cost(at) - prefix_scale / 100.0 * future_value(trial) - 0.5 * cover_bonus(trial.p[seat]);
                    prefixes.push_back({action, trial, rank, risk});
                }
                const bool recovery_window = target >= 0 && now.p[target].st != S_FREE &&
                    now.p[target].st != S_DASH && enemy_distance < 4250;
                if (!trial.over && (tick <= 3 || recovery_window) && (tick != 2 || target >= 0)) {
                    followup(action, trial, tick, risk);
                }
            }
            consider(action, trial, 0, risk);
        };

        try_walk({});
        try_walk(direct);
        for (Point d : directions) try_walk(d);

        // Preview a second walking direction from the best two-tick approaches.
        if (!prefixes.empty()) {
            const size_t count = std::min(prefixes.size(), size_t(turns));
            std::partial_sort(prefixes.begin(), prefixes.begin() + count, prefixes.end(),
                [](const Prefix& a, const Prefix& b) { return a.rank < b.rank; });
            for (size_t j = 0; j < count; ++j) {
                const auto& prefix = prefixes[j];
                auto try_turn = [&](Point steering) {
                    const Action second = move(steering);
                    if (second.mx == prefix.first.mx && second.my == prefix.first.my) return;
                    Game trial = prefix.state;
                    int risk = prefix.risk + turn_cost;
                    for (int tick = pivot + 1; tick <= 5 && !trial.over; ++tick) {
                        const Point from = position(trial.p[seat]);
                        advance(trial, inputs, seat, second);
                        risk += projectile_risk(trial, seat) +
                            possible_shots.risk(tick - 1, from, position(trial.p[seat]));
                        if (!trial.p[seat].alive) return;
                        if (tick <= 3 && !trial.over) followup(prefix.first, trial, tick, risk);
                    }
                    consider(prefix.first, trial, 0, risk);
                };
                try_turn(direction(route.goal - position(prefix.state.p[seat])));
                for (size_t i = 0; i < directions.size(); i += 2) try_turn(directions[i]);
                try_turn({});
            }
        }

        // Candidate dashes include an optional cancel into a slash while still moving.
        // A dash can cross a pit, but its landing and any earlier cancel must be safe.
        const bool close_gap = enemy_distance > now.cf.slash_reach &&
            enemy_distance < now.cf.dash_t * now.cf.dash_v + now.cf.slash_reach + 2200;
        const double walk_gain = route.cost(origin) + best_score;
        if (me.cd == 0 && (close_gap || nearing_collapse || walk_gain < 400)) {
            auto try_dash = [&](Point heading) {
                if (!heading.x && !heading.y) return;
                Game trial = now;
                const Action dash = act(A_DASH, heading);
                int risk = 0;
                for (i64 tick = 0; tick <= dash_ticks && !trial.over; ++tick) {
                    if (tick > 0 && trial.cf.dash_slash && trial.dash_moving(trial.p[seat])) {
                        followup(dash, trial, tick, risk);
                    }
                    const Point from = position(trial.p[seat]);
                    advance(trial, inputs, seat, tick == 0 ? dash : Action{});
                    risk += projectile_risk(trial, seat) +
                        possible_shots.risk(int(tick), from, position(trial.p[seat]));
                    if (tick == 3) risk += int(0.75 * kick_landing_cost(trial, floor, seat));
                }
                if (walkable(floor, position(trial.p[seat]))) {
                    consider(dash, trial, -450, risk);
                }
            };
            try_dash(direct);
            for (Point heading : directions) try_dash(heading);
            if (target >= 0 && close_gap && !nearing_collapse) {
                const Player& foe = now.p[target];
                const Point gap = position(foe) - origin;
                const Point v{foe.vx,foe.vy};
                const auto angle = [&](Point d, int skew) {
                    return direction({d.x - rdiv(d.y*skew, 1000), d.y + rdiv(d.x*skew, 1000)});
                };
                const i64 after_windup = now.cf.dash_pre + (now.cf.dash_t + 1) / 2;
                const Point straight = direction(gap + Point{after_windup * v.x,after_windup * v.y});
                for (int skew : {0,-140,140,-270,270}) {
                    const Point heading = angle(straight, skew);
                    if (distance(heading,direct) < 42) continue;
                    bool listed = false;
                    for (Point old : directions) listed |= distance(heading,old) < 54;
                    if (!listed) try_dash(heading);
                }
            }
        }
        return std::isfinite(best_score) ? best : last_surviving;
    }

private:
    using Inputs = std::array<Action, MAXP>;
    static constexpr int kill_value = 8;
    static constexpr int armed_dash_cost = 2200;
    static constexpr int empty_hand_dash_reward = 1000;
    static constexpr int crowded_edge_cost = 2500;
    struct Strike {
        Action action;
        int value = 0;
    };

    struct Landing {
        bool safe = true;
        bool crowded_edge = false;
        bool collapse_trap = false;
    };

    static bool safe_commitment(Landing check) {
        return check.safe && !check.crowded_edge && !check.collapse_trap;
    }

    i64 refuge_tick_ = -10;
    Point refuge_{};
    const GameInfo* info_ = nullptr;

    struct MotionSample {
        i64 tick = -1;
        Point position, velocity;
        i64 state = S_FREE, timer = 0;
        Point heading;
    };
    std::array<MotionSample, 64> samples_{};
    struct OpponentMotion {
        i64 tick = -1;
        Point velocity;
    };
    std::array<OpponentMotion, MAXP> opponent_motion_{};
    std::array<i64, MAXP> last_release_{};
    std::array<bool, MAXP> showed_throw_{}, showed_blade_{};
    std::array<int, MAXP> throw_opportunities_{};
    std::array<std::array<int, 3>, MAXP> aim_confidence_{};

    bool close_combat(int k) const {
        return !showed_throw_[k] && throw_opportunities_[k] >= (showed_blade_[k] ? 15 : 30);
    }

    struct Read {
        static constexpr size_t count = 32;
        std::array<float,count> x{};
        Point origin{}, axis{};
    };

    Read read_rival(const Obs& obs, int k) const {
        const Config& cfg = info_->cfg;
        const Player& foe = obs.p[k];
        const auto& dart = obs.d[k];
        const MotionSample own = self_at(obs.t - cfg.delay,obs);
        const MotionSample older = self_at(obs.t - 2*cfg.delay,obs);
        Read r;
        r.origin = position(foe);
        r.axis = direction(older.position - r.origin);
        const auto vector = [&](Point p, double scale, int index) {
            r.x[index] = float(std::clamp((p.x*r.axis.x+p.y*r.axis.y)/(scale*UNIT),-3.,3.));
            r.x[index+1] = float(std::clamp((-p.x*r.axis.y+p.y*r.axis.x)/(scale*UNIT),-3.,3.));
        };
        const double speed = cfg.run_v;
        r.x[0]=float(std::min(3.,distance(older.position,r.origin)/std::max<i64>(cfg.range,1)));
        vector({foe.vx,foe.vy},speed,1);
        const auto& prior = opponent_motion_[k];
        if (prior.tick == obs.t-1) vector(prior.velocity,speed,3);
        vector(older.velocity,speed,5);
        vector(own.velocity,speed,7);
        vector(own.position-older.position,speed*std::max<i64>(1,cfg.delay),9);
        vector(position(dart)-r.origin,std::max<i64>(1,cfg.range),11);
        if (dart.st==D_OUT) vector({dart.vx,dart.vy},UNIT,13);
        r.x[15]=dart.st==D_HAND;
        r.x[16]=dart.st==D_GROUND;
        r.x[17]=dart.st==D_BACK || dart.st==D_CALL;
        r.x[18]=float(std::min(1.,foe.cd/double(std::max<i64>(1,cfg.dash_cd))));
        r.x[19]=foe.st==S_FREE;
        r.x[20]=foe.st==S_THROWN;
        r.x[21]=foe.st==S_DASH;
        r.x[22]=foe.st==S_KICKED || foe.st==S_SLASHED || foe.st==S_STUN;
        r.x[23]=float(std::min(1.,foe.timer/8.));
        const Map& m=info_->M();
        for(int i=0;i<4;++i) {
            Point d=r.axis;
            for(int j=0;j<i;++j) d={-d.y,d.x};
            for(int n=1;n<=4;++n){
                const Point at=r.origin+Point{d.x*n*500/UNIT,d.y*n*500/UNIT};
                const int c=cell_of_point(cfg,m,at.x,at.y,std::max<i64>(0,obs.t-cfg.delay));
                if(c==C_WALL || c==C_PIT) {r.x[24+i]=(5-n)*.25f; break;}
            }
        }
        r.x[28]=wall_between(m,older.position.x,older.position.y,foe.x,foe.y);
        r.x[29]=cell_of_point(cfg,m,foe.x,foe.y,std::max<i64>(0,obs.t-cfg.delay))==C_GRASS;
        r.x[30]=float(std::min(1.,distance(own.position,r.origin)/3400.));
        r.x[31]=float(std::min(1.,dart.age/12.));
        return r;
    }

    // A small motion model fitted to public, delayed observations in local scrimmages.
    static std::array<float,2> forecast_rival(const std::array<float,32>& inputs) {
        static constexpr short w0[24][32] = {
            {187, 1, -10, 19, 8, 7, -18, -140, -256, -47, 141, 1958, -583, 511, -633, 54, -399, -75, 93, -288, -513, -383, -282, 578, 151, -15, -33, -18, 47, -653, 8, -565},
            {280, -539, 194, -437, -38, 34, -2, 51, 7, 218, 24, -429, 55, -46, 5, -68, 218, -37, -326, 124, 266, -215, -163, 500, 154, -210, -87, -108, 70, 40, 461, -94},
            {36, 18, 15, 73, -27, 10, -15, -579, 1094, -95, 123, 854, 397, -121, 847, 225, 187, 139, 217, -214, -1290, -422, 443, 516, -25, 8, -45, 4, 13, -345, -154, -460},
            {-121, -519, -16, -648, 14, 10, 7, -163, 14, 23, -16, 85, -7, 4, 0, 230, -957, -555, -463, 77, -189, 723, 301, 71, -98, -13, 128, -31, 92, -82, -207, -83},
            {-207, -263, 134, 6, 89, 102, 0, -60, -127, 138, 89, -68, -2589, 221, -108, 225, 279, -274, -66, -79, 213, 328, -437, 974, -181, 158, -66, -174, -9, 170, 36, 48},
            {360, 107, -370, -129, -45, -65, 20, 130, -51, 344, 171, 431, 158, 455, -43, -146, 193, 83, 14, 105, -284, 196, -814, 1138, 203, -20, 250, -50, -28, -334, -122, -270},
            {-222, -453, -52, -352, 18, -117, 0, -70, -4, -58, -20, -175, -56, 299, 47, -598, -444, -9, -351, -188, -59, 244, -1568, 856, -131, 200, -312, 187, 59, -92, 250, -218},
            {256, 420, 21, 122, 59, -166, -11, -149, -142, -260, 102, 131, 33, 239, -330, -283, -281, -73, -35, -238, -443, -373, -833, 1303, 93, 70, -272, -75, -91, -301, -138, -330},
            {158, 227, 67, 126, -2, 267, -15, -66, -32, 160, 18, 174, -85, 68, 41, 50, -171, -200, 233, 301, 253, -633, -123, 957, -233, 44, -74, 60, -72, -174, 48, -387},
            {-233, 794, -17, -415, 13, -24, 6, 13, 2, 9, 5, -82, -286, 305, 121, 399, 119, 265, 392, -35, -503, 682, -655, 932, 77, -59, 28, -100, -50, -100, 204, 64},
            {342, -73, 42, -41, 30, -10, 22, 31, 271, 186, -1197, -52, 177, 332, 561, 16, 338, 34, -88, 38, -897, 229, -161, 658, 216, -204, -167, 89, -117, 35, -285, -126},
            {58, 103, 796, -59, -217, 30, 21, -4, -5, -10, -56, -40, 356, 144, -257, 43, 23, 4, 524, 103, 22, 909, -1106, 464, 283, -504, 249, -154, -262, -57, 11, -79},
            {817, -30, 15, -58, -6, -68, 4, 36, 26, 540, -29, -417, 55, -43, 35, -242, -1148, -271, -279, -23, -111, 47, -528, 37, -467, 201, -440, 221, -56, -569, -288, 656},
            {-321, 142, -37, 48, 10, 23, -20, 8, -4, 264, -35, -96, -261, 560, -203, 128, 240, 337, 955, -177, -387, 442, -573, 566, 298, -94, 75, -113, 159, -195, 183, 236},
            {-190, -169, -25, -32, -33, -70, -5, 153, -61, -10, 68, 493, 1840, 191, 583, 237, 287, 193, 800, -234, -86, 107, -433, 369, -290, 29, -243, 48, 18, 240, -59, -439},
            {60, 223, 107, -26, -71, -154, -3, -17, -2, -275, 93, -418, 86, 339, -21, 261, 360, 471, 1938, 27, -247, -96, 170, 597, -208, -90, 41, -40, -54, 207, -811, -173},
            {396, -104, 419, -205, 32, -56, -35, 136, -66, 421, 99, 277, 69, 402, 16, 147, 86, 211, 78, 88, -275, 300, -789, 1483, -7, -58, 63, 9, 50, -363, 72, -267},
            {-557, 46, 251, -16, 96, -31, -40, -29, -18, -271, -396, -27, 93, -557, 201, -140, -128, -57, -6, 482, 845, -2112, -711, 361, 15, -427, 317, 158, 5, 75, -199, -131},
            {237, -161, -497, -3, 34, 31, -13, -14, -33, 63, 182, 93, 441, -148, -88, 370, 29, -78, 407, 254, 292, 422, 134, 589, -332, -68, -178, -61, 1, 154, 245, 50},
            {-40, 74, -731, -34, 190, 29, 1, 36, 15, -139, 44, -359, -219, -49, -33, -95, -122, 186, 111, 348, 313, -695, -802, 205, 246, -87, 198, -775, -195, 10, -32, -230},
            {124, 49, -41, 81, -2, -118, 5, 67, -109, 104, 25, -538, 28, -346, -22, 529, -170, 67, 2033, 475, 302, 1468, -283, -581, -59, 66, -7, 7, -267, -938, -54, -116},
            {499, -463, -129, -245, 34, 93, 29, 140, -7, 55, -68, 347, 158, 69, 453, 163, -415, -231, -521, 16, -337, -345, 54, -241, -13, -16, 114, -25, 38, -614, 133, -367},
            {-649, -291, -66, 145, 19, 7, -6, -35, 10, -187, -54, -87, -50, 40, 25, 254, 82, 3, 83, 306, -95, -615, -88, 954, -199, 93, 5, 136, -248, -54, 144, -185},
            {-68, -339, -443, -339, 112, -3, 26, -19, -3, -33, -219, 117, -160, -73, 305, 576, -315, 200, 423, -173, -75, 554, -402, 354, 40, -207, 88, -172, 188, -262, 118, -57}
        };
        static constexpr short b0[24] = {92, 45, 88, -82, 300, 377, 112, -60, 231, -703, 162, -72, -406, 276, 301, 135, 125, -48, 121, -116, -242, -37, 249, 84};
        std::array<float,24> layer0{};
        for (int i=0;i<24;++i) {
            float z=b0[i];
            for (int j=0;j<32;++j) z+=w0[i][j]*inputs[j];
            layer0[i]=std::max(0.f,z*.001f);
        }
        static constexpr short w1[24][24] = {
            {-48, 279, 34, -117, 135, 142, 96, -208, 42, -498, -9, 31, 133, 119, 39, 282, 217, -765, 68, 151, 569, -71, -383, 41},
            {-115, -44, 925, -144, -63, -56, -29, 29, -277, 222, 42, 73, 460, 14, -238, -39, -154, -344, -276, 132, 10, -320, 237, -206},
            {147, 437, 46, -313, -302, -367, 251, 89, 61, 118, -100, 284, -1212, 271, 6, 145, 54, 246, 76, -73, 339, 113, -281, -66},
            {-15, 235, -4, -184, -263, 206, 50, -117, 217, 450, -97, 24, -592, 217, -6, -2, -120, 505, 213, -88, 158, -18, -395, 153},
            {-167, -398, 25, 661, -24, 29, -390, -247, 160, -1121, 152, -230, -1362, -233, -438, 160, 203, 19, 165, -194, 430, -268, -833, -79},
            {414, 156, 328, -567, 442, 267, 415, 126, -41, 329, 97, -392, -527, 35, 271, 64, -294, -82, -164, 265, -93, 343, 122, -205},
            {-135, -68, 41, 769, 17, 143, -455, 646, 226, -466, -174, 376, 199, -218, 175, 111, 446, 185, -307, 300, -452, -402, 116, 144},
            {-67, -127, 8, 36, 460, 124, -541, 149, 250, 357, 145, -298, 629, -338, 53, 301, -165, 39, 2, 210, 104, 105, 52, -205},
            {-1, -92, 28, 348, 301, 45, -278, -3, 174, -418, 353, -745, 695, -21, 283, 965, 44, -524, -263, -360, 272, -364, -175, 379},
            {94, 187, 64, -443, 163, 227, -236, -683, -665, -1222, -1256, -48, -559, -81, 317, -173, -618, -320, -180, 108, 44, 125, 132, 134},
            {-118, -243, 58, -445, 20, 50, -285, -71, 391, 366, 50, 6, 92, -12, -151, 333, 382, 220, 244, 7, -44, -225, -241, 106},
            {306, 126, -37, -360, 384, 169, 434, -482, -181, 324, 389, -361, -967, -220, 177, -15, 87, -340, 27, 275, -47, 229, 284, -222},
            {530, -311, 325, -101, 374, 306, 33, 2, -8, -256, 123, -34, 359, 29, 470, 112, 279, 431, 102, -195, -443, 364, -103, -58},
            {262, 204, 313, -204, -152, -174, -270, -40, -48, 47, 176, -14, -999, -71, 573, 5, 69, 395, -10, -253, -26, 321, 36, -190},
            {1406, -459, 427, -200, -152, 696, -240, -224, 338, 162, 300, 131, -440, -3, 1102, -494, 413, -210, -244, -346, -321, 684, -105, -58},
            {255, -23, 420, -398, 183, 197, -57, -122, 228, -105, 150, -20, 126, -10, -102, 458, -23, -2, 158, 248, 291, -141, -579, 210},
            {341, 129, 409, -867, -10, -228, 547, -482, -70, -584, 42, 125, -854, 501, 145, 302, 222, 100, 190, -21, 558, 117, 99, 72},
            {260, -95, 177, 28, 145, 45, 141, 144, 312, 260, 134, 272, 150, -121, 263, 223, 389, 59, -170, -146, -117, 60, -246, -173},
            {624, 295, 371, -476, 411, -279, -164, -117, 179, -129, 187, -299, -1183, -206, 263, 813, -152, -81, -28, 9, 321, 257, -196, -120},
            {25, 177, -84, 15, -110, -1, 64, 217, 156, -329, 97, 639, 161, -326, -10, -234, 328, 296, 263, -512, 100, -100, 155, -384},
            {-134, -460, -29, -1450, -708, -1173, -875, -351, 263, 570, -512, 1, -553, 145, 541, -1122, -414, -674, 4, -142, 594, 363, 7, -69},
            {211, 299, 290, -893, 312, -35, 159, -639, 96, 324, 31, -36, -394, -86, 82, 224, 111, -278, -222, -110, 40, 299, 375, -464},
            {335, 34, -42, 44, 653, -118, 110, -168, -331, -274, -11, -482, -324, 158, 284, -86, 164, 566, -437, 245, -215, -102, 5, -44},
            {154, 5, 100, -46, 80, 284, -282, 94, 72, -353, 235, -38, 531, -361, 120, 167, 434, 194, -16, 144, -89, -151, -255, -30}
        };
        static constexpr short b1[24] = {-180, 101, -118, -111, -185, 89, -262, 114, -72, 70, 236, -7, 102, 102, -313, 114, -190, 278, -79, 20, 251, 28, -227, -102};
        std::array<float,24> layer1{};
        for (int i=0;i<24;++i) {
            float z=b1[i];
            for (int j=0;j<24;++j) z+=w1[i][j]*layer0[j];
            layer1[i]=std::max(0.f,z*.001f);
        }
        static constexpr short w2[2][24] = {
            {1, 83, -129, 93, 712, -36, -288, 192, -25, 145, 312, -266, -105, -28, 37, 98, -246, 294, -5, -13, -568, -178, 68, 224},
            {-191, 9, 240, -112, -20, -229, -162, -260, 92, -481, 13, -240, -2, 87, -5, -160, 25, 216, 11, 181, -72, 200, 372, 76}
        };
        static constexpr short b2[2] = {-49, 69};
        std::array<float,2> layer2{};
        for (int i=0;i<2;++i) {
            float z=b2[i];
            for (int j=0;j<24;++j) z+=w2[i][j]*layer1[j];
            layer2[i]=z*.001f;
        }
        return layer2;
    }
    struct OpponentFrame {
        i64 tick = -1;
        Player player{};
    };
    std::array<std::array<OpponentFrame, 16>, MAXP> motion_history_{};

    using MotionFeatures = std::array<float, 55>;
    struct MotionRead {
        MotionFeatures x{};
        Point origin, axis;
    };

    MotionRead motion_read(const Obs& obs, int k) const {
        const Config& cfg = info_->cfg;
        const Player& foe = obs.p[k];
        const Read basic = read_rival(obs, k);
        MotionRead read{};
        read.origin = basic.origin;
        read.axis = basic.axis;
        if (!read.axis.x && !read.axis.y) read.axis = {UNIT, 0};
        std::copy(basic.x.begin(), basic.x.end(), read.x.begin());
        const auto vector = [&](Point p, double scale, int index) {
            read.x[index] = float(std::clamp((p.x * read.axis.x + p.y * read.axis.y) / (scale * UNIT), -3., 3.));
            read.x[index + 1] = float(std::clamp((-p.x * read.axis.y + p.y * read.axis.x) / (scale * UNIT), -3., 3.));
        };
        const auto& history = motion_history_[k];
        for (int elapsed : {2, 4, 8}) {
            const i64 tick = obs.t - elapsed;
            const auto& frame = history[size_t(std::max<i64>(0, tick)) % history.size()];
            const int f = elapsed == 2 ? 32 : elapsed == 4 ? 36 : 40;
            if (tick < 0 || frame.tick != tick) continue;
            vector({frame.player.vx, frame.player.vy}, cfg.run_v, f);
            vector(read.origin - position(frame.player), cfg.run_v * elapsed, f + 2);
        }
        const auto visible_to_rival = self_at(obs.t - 2*cfg.delay, obs);
        const auto fresh = self_at(obs.t - cfg.delay, obs);
        const auto& dart = obs.d[k];
        read.x[44] = float(visible_to_rival.state == S_DASH);
        read.x[45] = float(visible_to_rival.state == S_SLASHED || visible_to_rival.state == S_SLASH);
        read.x[46] = float(visible_to_rival.state == S_STUN);
        read.x[47] = float(fresh.state == S_DASH);
        read.x[48] = dart.st == D_OUT;
        read.x[49] = foe.st == S_THROW;
        read.x[50] = foe.st == S_RECALL;
        read.x[51] = float(showed_throw_[k]);
        read.x[52] = float(std::clamp(double(obs.t-cfg.cstart()) / 125., -1., 1.));
        read.x[53] = float(std::clamp(distance(read.origin, fresh.position) / 3400., 0., 2.));
        const i64 when = std::max<i64>(0, obs.t - cfg.delay);
        read.x[54] = float(dart.st == D_GROUND &&
            wall_between(info_->M(), foe.x, foe.y, dart.x, dart.y) &&
            cell_of_point(cfg,info_->M(),dart.x,dart.y,when) != C_PIT);
        return read;
    }

    void cache_motion(const Obs& obs, int k) {
        auto& h = motion_history_[k];
        h[size_t(obs.t) % h.size()] = {obs.t, obs.p[k]};
    }

    // Predict delayed displacement, recent displacement and terminal velocity.
    static std::array<float,8> predict_motion(const MotionFeatures& features) {
        static constexpr short w0[32][55] = {
            {69, 330, 685, 656, 27, 71, -60, -91, 79, 290, 341, 184, 1341, -1629, 431, 132, -607, -2893, 5612, 286, 303, 178, -168, 501, -1666, -710, 1100, 1483, 15, 298, -47, -804, 508, 43, -457, -34, 92, -374, -577, -292, 150, 389, 508, -525, 591, 130, -53, -197, -100, -368, -285, -667, -11, 1315, -686},
            {1317, 1447, -1160, 317, -662, -541, 273, -230, 108, -525, -29, -1949, 1534, 1031, -892, -680, -1840, -2090, -62, -339, 154, 406, -273, 83, -595, 207, 592, 179, -903, -539, -1217, -2822, -89, -203, -736, 420, 81, 133, 624, -103, -852, 40, 146, 670, -134, -388, 1457, -1338, 1067, -444, 421, -641, -559, 654, 339},
            {-132, 739, -527, 748, 447, 227, -233, -256, -262, 1787, -1563, -2362, -1192, -246, -1441, 717, -507, 1035, 2621, -65, 221, 208, -449, -362, -2, 104, 344, -1594, -260, -198, 681, -1209, 226, -9, -745, -108, 460, -150, 37, -45, 858, -354, 1554, 2100, -35, 2254, 223, -363, 439, -453, -132, -170, 334, 2034, -1404},
            {-252, -1474, 184, -379, 289, 68, -99, -192, 236, 1031, -1163, 753, -949, 817, -34, -364, 265, -381, -944, 75, -189, 447, -179, -256, 820, -1243, 215, -1419, 1390, -1443, -100, -246, -554, 54, -55, 252, -853, -45, -326, 257, 399, -279, -1223, -620, -355, 910, 109, -1433, 114, 499, 208, -505, 473, 1863, -116},
            {-312, 333, 962, 116, -60, 166, 138, 605, -455, 345, -2210, 1092, 1414, 147, -67, 700, 399, -148, 7333, 831, 161, -469, -363, 441, 895, -172, -357, 889, 631, -310, -239, 721, -140, 6, -26, 303, -194, 192, -189, -258, 315, -216, 514, -694, 291, -124, -1534, -1013, -28, -399, -189, 34, 73, 1008, -173},
            {-994, -728, -1625, -172, -136, 41, 256, -577, 480, -882, -543, -957, -1436, -143, 1258, 1915, -870, -346, 4782, 944, 5, -419, 367, 382, 996, -156, -814, -341, -307, 399, 146, 1431, -92, 5, 455, -248, 81, 36, -313, 134, -344, -39, 745, -84, 758, 872, -15, 770, -398, -3, 193, -165, 306, -115, -246},
            {-344, 1234, -1690, 274, 581, -157, -301, 173, 361, 57, 2007, -104, 416, 812, 0, 338, 1044, -777, 887, 652, -403, -248, -295, 27, 218, -898, 496, -1059, 507, -553, 116, 276, -153, -194, -780, -601, -31, 58, 104, -335, 384, 366, -392, -125, 63, 720, 1063, -291, 136, 474, 149, 575, -47, 911, -443},
            {897, -931, 1330, 197, -472, -147, -64, -643, -402, -469, 2006, 726, 311, -95, -589, -233, -474, 707, 4079, -537, -472, -117, 322, 295, 12, 174, -1036, -843, -750, 106, -531, -252, -148, 163, -286, 413, -207, 390, 101, 418, -224, 40, 341, -1008, -54, -766, -658, 256, 1268, -266, 405, -713, -39, 1271, 291},
            {1376, 995, 526, 523, 262, 117, -71, 538, 142, 1460, -2870, 655, 453, 716, 843, 310, -262, -1776, 93, -303, 529, -352, 363, -344, -1659, -499, -1431, -14, 543, 153, -200, 111, 277, -2, -1197, 412, 259, 281, -624, -111, 389, 592, -1099, 612, 374, -85, -349, -512, -691, 77, 282, -713, 15, 456, -603},
            {-1426, 588, 1137, 77, -889, 109, -437, -95, 381, -929, -2503, -298, 375, -297, -232, 291, 62, 899, 1940, 226, 50, 270, 492, -316, -214, -3269, 1450, 444, -79, -465, 95, -378, -112, -772, 46, 618, -465, -337, 32, 63, 67, 30, 3, 690, -1648, 419, 163, -39, 146, 349, -355, 750, 338, -489, -265},
            {-555, -2232, 1232, -586, 990, 54, -292, 185, -220, -43, -218, -817, 1579, 261, -732, -1595, 90, 268, 2064, 179, 182, 322, -470, -291, -1041, -304, -257, 68, -219, -252, 924, -1789, -32, -205, -486, 1263, 115, -347, -796, 580, 177, 425, 17, -225, -6, 314, 1781, 533, 503, -98, 76, -150, -90, -646, 289},
            {-567, 416, 358, 141, 11, -76, -124, 177, 408, -252, -322, -2840, 605, 156, 211, 14, 139, 695, 8256, 124, 122, -464, 364, -274, 128, 1084, -1446, 1325, 963, 120, -178, -1417, 228, -115, 28, -207, 487, -33, 113, -144, 258, 151, 1016, 16, 633, 680, 713, 407, 1578, 275, -212, 893, 634, 1036, -226},
            {-29, 435, 37, 282, 26, -51, 49, -23, 126, 547, -182, 1734, 1349, -2515, -278, 802, -496, 951, -48, 717, -80, -536, 167, 274, -1240, -577, -27, -832, -27, -664, 496, -93, 689, 17, -330, -193, 412, -187, -484, -179, -1342, 254, -2272, -339, 57, 782, -985, -1022, -1990, 44, -365, -356, 601, 89, -458},
            {1901, -3179, -1594, -274, -47, -9, 113, 666, -239, 430, -774, -782, 711, -509, -678, -806, 853, 214, 4418, -45, 59, 423, -504, -1, -656, 829, 377, 670, -382, 38, 334, 111, 128, 69, -21, -72, 355, 238, -710, -253, -100, -40, 538, 255, 520, -249, -136, -251, -479, -5, 226, -313, 46, 1136, 1287},
            {-1796, -1456, 344, -366, -421, 51, 88, 2, -659, -213, 1985, 517, 350, 343, -550, 732, 292, 728, 4065, 1222, -292, 151, -19, 303, -933, 1163, -239, -720, 442, -710, 696, -422, -49, -56, 104, 478, -77, 303, -572, 807, -241, 203, -189, 1225, -1127, -32, -1830, 350, 338, -492, -457, 552, 345, -136, 205},
            {829, 1218, 630, 213, 138, 116, 39, -64, -303, 883, 899, -600, 389, 277, -389, 169, -458, -489, -5085, -523, -403, -157, 538, -507, -13, -2687, 449, 158, -7, 485, 473, -1636, 14, 409, 102, 67, 416, 24, 321, 581, -1, 246, 503, -3065, 78, 1548, -297, -234, 1423, 371, -13, -1147, -174, 1042, -1541},
            {-228, -2187, -284, -588, -104, -40, 214, -109, -279, -146, 346, 1205, 552, 160, 1065, -4722, 68, 203, -1555, 43, 286, 259, -263, 365, -667, -268, -93, -8, 84, -832, 1855, -2159, -368, 52, -148, -201, -618, 293, -187, 260, 177, 6, -1016, -617, -781, 389, 141, -772, 1251, -3, -64, -120, -313, -655, -237},
            {-142, 194, -374, 405, -335, 160, 12, 109, 294, 2155, 100, 276, 104, 22, 243, 705, -393, -514, -5444, 422, 65, 490, -348, 170, -1707, 91, -1170, -940, 292, -320, 821, 151, 313, -279, -638, -740, 157, -185, -379, -83, 807, -443, -398, -1766, -1955, -73, -1863, -1136, 758, -212, 146, 57, 339, 402, -704},
            {390, -2507, 1047, 88, -212, -55, -118, 461, 162, 186, -91, -620, 125, 175, -995, 755, 733, 546, -47, 301, 101, -298, 531, -28, 1498, 74, -88, 172, 150, -360, 945, 97, 55, -266, 277, -158, -148, -318, 43, -376, -33, 267, -525, -206, 6, 863, -169, -1093, 506, -108, -315, 646, -519, -167, -434},
            {954, 1765, 2736, -26, -889, -123, -43, 96, 374, 169, -341, 260, 131, -320, 301, 75, -55, -393, 1065, -291, 153, 148, 318, -67, -888, 841, 377, 649, -139, 578, -613, 875, 127, -100, -500, 599, 533, 22, -167, 469, -392, 392, -80, -574, 104, -10, 143, 88, 59, 258, 415, 654, -434, 564, 475},
            {-1219, -127, 158, -468, -24, -350, -209, -608, -729, 233, -2506, 299, -484, -144, -798, 1358, -14, -1095, 1194, 789, 277, -108, -244, -263, -775, 691, -178, -400, 202, -852, -89, 1040, -27, 1, 235, -23, -29, 83, 468, -214, 500, -61, 729, -317, -891, -877, -2251, 1819, -953, 296, -149, 648, 1, -436, 69},
            {1164, 547, -1503, 343, 271, 68, 69, 433, -137, 763, 628, 297, -1385, -51, 962, -211, -202, -481, 1164, 414, -115, -418, -210, -361, -412, 1677, 1505, 4, 823, 324, -233, 401, 305, 266, -99, -220, -247, -159, 65, 13, 111, -177, 70, 44, -69, 724, -137, -1172, 575, -511, 516, 99, -522, 283, 255},
            {-2299, 710, -203, -559, -44, 18, 42, 625, 93, 140, -320, -1057, -232, 841, 572, 1285, 76, -2748, -2411, 1555, -522, 239, -91, -100, -1507, -278, 375, -302, -730, 82, 510, 2422, -13, 6, 527, -27, 375, -131, 446, -43, 466, -50, 719, -10, -635, 201, -469, -322, -382, 412, 409, -978, -161, -1108, 1212},
            {-236, -1332, 88, -69, 299, -28, 37, 185, 171, -184, -123, -299, -5096, 311, 324, 625, 1062, -315, 4471, -96, 227, 516, 525, -294, -1927, -121, -154, -865, -1503, -153, 769, -1777, 246, 31, 597, -2, -77, -76, -307, 292, 108, 165, 464, -889, -105, -1, 499, -308, -881, 365, 512, 121, 434, 656, 602},
            {319, -54, -1281, 77, -1163, -12, -102, -32, -88, -400, -363, -1296, -286, 635, -967, -342, -30, 2329, 3438, 125, 498, -447, 432, -399, 1908, 129, 479, -2711, -1219, 633, 102, -1301, 130, -355, 342, -667, -161, -108, -41, -268, -181, 63, -634, -1103, 137, -458, 92, 253, 432, -112, -43, -885, 223, -335, 234},
            {56, -837, -135, -524, -209, 93, 13, 12, 61, -371, -432, 1861, -1860, -635, -2263, 934, -1175, -2434, -10254, -126, -257, -271, 17, -432, -824, 426, 772, -360, 823, -887, 577, -897, -419, -237, 470, -49, -16, -319, 1035, 38, -763, 1897, -222, 476, -200, -1236, -2122, -269, 771, 20, 233, -903, -318, -59, 90},
            {1812, 596, 1372, 217, 1155, 276, 11, 170, -186, 712, 316, -2161, 1463, 1557, 1049, -106, -620, 908, 1736, -1222, -422, 316, -462, 104, 1050, -1313, 222, -411, -1202, -575, -942, -1271, 106, 278, 459, 268, 199, 288, 664, 315, -11, 45, 175, 306, -951, 766, 2222, -66, 593, 472, -440, -532, -347, 476, -870},
            {-1169, -742, 449, -303, 957, -2, -14, -77, 101, -232, -664, 1873, 142, -739, -310, 267, 151, -74, 2276, -111, -403, 294, -498, 112, 414, -393, -949, -35, 22, 1333, 835, 80, -243, 785, 724, 132, -56, 553, 528, -343, -568, -1331, -418, 1115, 863, -1578, -507, 667, 550, 247, 414, -207, 199, -371, 959},
            {-869, -1243, 181, -642, -98, -276, -24, 64, 164, -411, -410, 207, 55, -1568, -596, -1464, -579, 511, -4139, -1456, -169, -313, 104, 445, -1153, 709, -1378, 1041, -393, 166, 1954, -1176, -390, 39, 243, 18, -398, -91, 466, -251, -135, -120, -1209, 283, 750, 910, -1275, 404, -706, 328, -181, -148, -661, 323, 723},
            {-239, 93, -253, -204, 119, -16, 264, -134, 36, 238, -54, -3789, -150, -237, 4890, 845, -1323, -2328, -6149, 602, 16, 13, 119, -247, -689, -1137, 140, -920, 264, -2013, 53, 611, -61, 131, -172, -166, -201, 298, -237, -287, -411, -1158, -397, 220, 906, -1772, 792, -1620, 615, -22, 17, -1819, 534, 1661, 1601},
            {325, 372, 1752, -166, -418, -205, 118, 314, -329, -260, 244, 325, -129, 165, -570, -330, 323, 740, 892, 470, 231, -144, -526, -422, 812, -1005, 182, -1314, -216, -61, 755, -301, -10, 283, -592, 189, -283, 257, -68, -350, -463, -481, -1159, 339, 292, 1327, -846, -320, -440, 277, 456, -239, 417, -33, 644},
            {2157, 1126, -821, 468, 66, 371, 311, 459, -17, 1482, -342, -2060, -2056, -604, 103, -1084, -1486, -871, 710, -1049, -313, -307, 524, -450, -376, 218, -572, 14, -653, -521, -564, -1184, 493, 154, 112, -508, 413, -137, 804, -158, 439, -468, 577, -398, -3673, 436, 80, 101, 417, -31, -118, 189, -592, 393, -883}
        };
        static constexpr short b0[32] = {-117, -944, 260, -220, 224, 721, 330, -517, 293, 230, -626, -387, 514, -507, 1004, -598, -278, 621, 916, -37, 1395, 99, 1528, 363, -131, -652, -1099, 337, -1401, 668, 115, -840};
        std::array<float,32> h0{};
        for (int j = 0; j < 32; ++j) {
            float value = b0[j];
            for (int k = 0; k < 55; ++k) value += w0[j][k] * features[k];
            h0[j] = std::max(0.f, value / 4096.f);
        }
        static constexpr short w1[24][32] = {
            {-161, 40, -509, 503, -165, 195, -57, 153, -491, -506, 1623, 662, -342, 520, -453, 273, 795, 4, 807, 1000, -539, 948, 1649, -411, -54, 767, -244, -448, 2427, -510, -409, 1606},
            {101, 1500, 416, -1626, -651, -1311, 132, 155, 1699, 457, -835, 738, -101, -63, -126, 375, -450, 894, -718, 543, -63, 497, -1690, -566, -462, -690, 506, -503, 1916, -820, 264, 1953},
            {1457, -16, 954, 736, 157, -345, 109, 746, -469, 529, -583, 1450, 1094, -221, 1017, 19, -228, 206, 5, -580, -718, -1, -996, 299, -407, 1341, 509, 59, 1114, 1721, 718, -1105},
            {572, -1042, 642, 339, 272, -176, -453, 217, 314, -280, -572, 821, 243, 975, 484, -375, 546, 34, 750, 390, 117, -190, -742, 49, -951, 1616, 264, 377, 1605, 1185, 610, -1601},
            {187, 256, 651, 685, -428, 1387, 1070, -496, 259, -1385, -1407, 75, 19, 644, 1425, -537, -436, -793, -552, 487, 474, 643, 1082, -56, 1071, -503, -1287, 151, -666, -683, -86, 1137},
            {737, -1129, -1733, 855, -217, -1520, -64, -378, 2177, -382, -502, -2295, -609, 1981, 108, -1369, -1094, 718, 594, 8, -1686, 829, -2476, 203, -519, -242, 64, -368, -1383, -250, 931, 3141},
            {161, -170, -26, -409, -652, 103, 193, -141, -38, 107, 155, -600, -139, -10, -467, -22, -897, -248, 359, -254, -724, -405, -532, -189, -1131, 0, -272, -606, -850, -398, -111, -277},
            {203, 38, 639, 787, 1236, 143, 661, -60, 498, 277, -1813, 1350, 1772, 611, 753, 896, 42, -249, -948, -719, -384, 453, -2337, 1538, 134, 1839, -2064, -24, 1769, 1995, -67, -1371},
            {-613, -792, -375, -895, 167, -520, -23, -474, -409, -244, 260, 227, -275, 452, -301, -505, 425, -1061, -227, -287, -540, -504, -515, -277, 205, 346, -655, -306, -460, -412, -953, -241},
            {810, 13, 687, -767, -71, -1112, -353, 291, 281, 1601, 1597, 783, 75, -3, -800, -170, 2501, 1399, -1706, 212, -376, 739, -1295, 857, 263, 1340, 1466, 628, 1993, -227, 4, 2095},
            {369, 326, 484, 6, -60, -816, 908, 360, -401, -2753, 529, 324, 375, -540, 1225, 269, 542, -240, -302, 683, -505, 461, -2670, 512, 1248, 32, -1698, -534, -2248, 131, -1108, 108},
            {596, 128, 833, 1000, 127, -453, -957, 924, 960, 1, -1050, 459, 512, 579, -141, 388, -2110, 982, -3, -26, -1413, 396, -1170, -1446, 181, 1065, -994, -329, 62, 1135, -403, 266},
            {-455, -914, -602, 180, -433, -232, -272, -502, 42, -533, 428, 121, -470, 180, -495, -562, -521, -484, -147, -84, 103, 175, -72, -637, -925, 429, -1, -200, -181, 324, -701, 147},
            {903, 149, 207, 766, 1380, 722, 321, -7, 100, -570, -1354, 778, 873, -834, 1131, 792, -1254, 46, -1065, 278, 171, -399, 744, 1489, 555, 564, -1380, 692, 786, 798, 205, -388},
            {1049, 470, -324, 748, 478, 1293, -35, -8, -45, -366, -604, 630, 843, 966, 514, 244, 215, 834, 413, -635, 35, 52, 1664, 308, -94, 1192, -417, 346, 757, 21, -392, -220},
            {765, -14, 758, -148, -128, 912, 804, 851, -136, -1549, 538, 14, 804, -280, -966, 267, -839, 705, -364, 117, -973, 202, -2144, -333, 160, 1329, -455, -335, 2151, 705, 765, -1036},
            {1485, -160, -450, 596, 509, 165, -44, -83, -860, 1471, 1474, 492, 89, 393, -432, 28, -958, 484, 852, 601, 882, -155, 1201, -100, -2150, -497, 1734, 461, -1199, -818, -148, -967},
            {170, -3902, -930, -1381, 151, 1034, -499, 528, -1376, 112, -1407, -254, 338, -596, 77, 1410, 549, 1365, -61, -800, 1, -1272, 1773, -219, 1587, 30, -2477, -764, -414, -218, -516, -534},
            {-26, -1912, 75, -415, 805, -1347, 1340, 614, -1060, 421, 1217, 923, 650, 318, -232, 427, 443, 995, 45, -757, -471, -703, -1309, -652, -560, 1412, 426, -60, 750, 509, 500, -2562},
            {-787, 118, 315, -320, -353, 14, 784, -826, 252, 464, -604, 672, 351, 703, -816, 1170, 493, -982, 841, -1872, -275, 306, -1845, 1065, 72, 85, 22, 548, 1643, 682, -711, -138},
            {-1379, 510, 2013, -703, -1453, -451, 395, -1714, 1413, -2000, -1711, -2160, 294, -808, 1062, -2227, 1327, 128, -133, -2412, -908, -586, 365, -550, 1588, 1464, 1250, 1102, -108, 780, 915, 1058},
            {277, -158, 205, -92, -914, 31, -835, 648, -58, -867, 737, 415, -166, 144, 349, 548, 910, 313, 1044, -199, -120, -846, 433, 763, 682, 277, -793, 888, 1347, 1140, -437, -2123},
            {353, -4291, -1524, 275, -582, -767, -1315, -1182, -681, 472, 513, -524, 301, -973, -727, 2720, 958, 1738, 609, 126, 133, -1517, 283, 447, 1728, 847, 1664, 1066, -218, 605, 624, -1288},
            {48, -1080, 544, 763, 738, -88, -249, 254, 339, 140, -1165, 568, 579, 496, 585, 616, 262, -670, 799, -234, -193, -357, -1921, 1384, -457, 1054, -1735, 443, 127, 1571, 863, -975}
        };
        static constexpr short b1[24] = {375, 490, -49, 427, -512, -434, -364, 477, -866, -1974, -146, -620, -879, -333, -542, 162, 84, -384, -231, 159, 298, 313, -200, 322};
        std::array<float,24> h1{};
        for (int j = 0; j < 24; ++j) {
            float value = b1[j];
            for (int k = 0; k < 32; ++k) value += w1[j][k] * h0[k];
            h1[j] = std::max(0.f, value / 4096.f);
        }
        static constexpr short w2[8][24] = {
            {-443, 1783, -216, -350, 108, 1284, -1, 407, -140, 172, 196, 375, 484, 663, -771, 818, 347, -729, -158, -530, 369, -1363, 551, -482},
            {-47, 312, 790, 446, -729, 44, 33, -570, -641, 1, -672, -357, 271, -50, -650, 385, 990, 1456, 389, -881, -2574, -39, 2554, 348},
            {-906, 2063, -188, -524, 218, 1465, 574, -107, 60, 402, 560, 600, 42, 157, -128, 803, 463, -676, -217, -522, 521, -1192, 653, 144},
            {-373, 477, 342, 365, -515, -26, -671, 79, -519, 80, -1055, -520, -151, -637, -96, 720, 1056, 2005, 850, -936, -3272, -143, 3282, -102},
            {-1018, 2141, -184, -706, 287, 1446, -108, -107, -193, 524, 605, 665, -361, -99, 21, 739, 566, -635, -240, -629, 613, -1004, 682, 342},
            {-565, 453, 194, 400, -436, -71, -717, 80, -154, 188, -1027, -518, -575, -794, 374, 795, 856, 1853, 1013, -867, -3616, -319, 3645, -263},
            {1859, 3322, -2569, -969, -1286, 5267, -111, -2505, -38, -7247, -1062, 1754, -829, -4019, -2453, -965, -568, -217, -4388, 1156, -1200, 366, -1403, -1227},
            {-1392, -1647, 836, 1417, -1875, -9412, -147, 1777, 723, 8233, 2407, 5763, -238, -583, -420, 1564, -1500, 514, 2211, 1836, 3851, 855, 4333, 1917}
        };
        static constexpr short b2[8] = {639, -50, 572, 1, 555, 135, 514, -56};
        std::array<float,8> h2{};
        for (int j = 0; j < 8; ++j) {
            float value = b2[j];
            for (int k = 0; k < 24; ++k) value += w2[j][k] * h1[k];
            h2[j] = value / 4096.f;
        }
        return h2;
    }

    // Keep both the intervening drift and the recent velocity. Using the first
    // to advance the stale observation and the second for future play avoids
    // continuing the same guessed turn longer than the model predicts.
    std::pair<Point,Point> predicted_velocities(const Obs& obs, int k, Point base) const {
        const Config& cfg = info_->cfg;
        const auto& foe = obs.p[k];
        if (cfg.delay != 4 || obs.t < 4 || foe.st != S_FREE || close_combat(k)) return {base, base};
        const auto read = motion_read(obs, k);
        const auto estimate = predict_motion(read.x);
        const auto world = [&](int i) {
            return Point{
                i64((read.axis.x * estimate[i] - read.axis.y * estimate[i+1]) * cfg.run_v / UNIT),
                i64((read.axis.y * estimate[i] + read.axis.x * estimate[i+1]) * cfg.run_v / UNIT)
            };
        };
        Point mean = world(0);
        mean = {mean.x + (mean.x - foe.vx) / 5, mean.y + (mean.y - foe.vy) / 5};
        const Point late = world(2), tail = world(4);
        const Point follow{(2 * late.x + tail.x) / 3, (2 * late.y + tail.y) / 3};
        const double weight = showed_throw_[k] ? .84 : .84 * .55;
        const auto join = [&](Point velocity, double share) {
            Point result{i64(base.x + (velocity.x - base.x) * share),
                         i64(base.y + (velocity.y - base.y) * share)};
            cap_len(result.x, result.y, cfg.run_v);
            return result;
        };
        return {join(mean, weight), join(follow, weight * .80)};
    }

    // Predict delayed displacement, recent displacement and terminal velocity.
    static std::array<float,8> predict_blade_motion(const MotionFeatures& features) {
        static constexpr short w0[32][55] = {
            {499, 65, 390, 225, -109, -330, -16, 66, 1213, -807, -4020, -221, -193, -524, -374, -1070, -542, -166, 5725, -351, 306, 180, -169, 507, 282, -18, 259, 480, 223, 537, -76, -38, 154, 46, -695, 45, 61, 233, -55, -48, 70, 117, -557, -157, 1517, 914, 152, 230, 319, -372, -288, 17, 149, 769, -205},
            {1723, -267, -1324, 157, -399, 213, 170, -135, -233, 1036, 33, 435, 400, -108, 248, -1030, 215, -331, 1610, -125, 155, 411, -277, 84, -593, 1533, -770, 1263, 407, -190, -476, -80, 155, -111, -487, 330, -84, 39, -152, -727, 28, -63, 514, 240, 1879, 716, 334, -769, 476, -449, 426, -290, -510, 1102, -191},
            {737, 709, 774, -159, 80, 1197, -712, -464, 116, -81, -1474, -163, 4, 31, 99, 132, 473, 140, 858, -458, 224, 210, -454, -366, 287, 995, -884, -1595, -130, -888, 66, 230, -292, 349, 39, 297, 173, -124, -389, 866, 278, 936, -254, 725, -3449, 552, -394, -1493, -408, -458, -133, 507, 383, 1248, -177},
            {1488, 482, -33, -83, -175, 652, 424, -792, -37, 1325, -618, 169, -529, -82, 452, -659, 482, 443, -127, -55, -191, 452, -181, -259, -100, -966, -689, -716, 541, -1462, -482, 243, 66, -247, 367, -85, 300, 64, 51, -650, 257, -695, -1541, 847, -3658, 346, -118, -4639, 375, 504, 211, 426, -623, 1023, -491},
            {1145, 91, 10, -163, -59, -444, 183, -115, -863, 214, -2400, -356, -9, 337, -261, 627, 165, -379, 526, 615, 163, -475, -367, 446, 691, -277, 168, -1193, 326, -1007, -480, 512, 363, 98, 486, -337, 139, -63, 607, -481, -497, 589, -714, -167, -1221, -426, -166, -5327, 415, -404, -191, -54, 341, 273, -378},
            {-484, -508, -194, -761, -34, -1347, 355, -441, -282, -1308, 425, 44, 284, 513, 98, 934, -406, -126, 4186, 824, 5, -424, 371, 387, -188, 700, 533, -710, -276, -121, 1, 534, -399, 126, 310, 496, 126, 132, 272, 110, 96, -728, 846, 105, -711, 234, -140, -345, 373, -3, 195, -537, 315, -157, -350},
            {460, 356, -681, 31, 579, -76, 313, 268, -1146, -719, 95, -517, 396, -25, -461, -75, 384, -312, 5671, -17, -408, -251, -299, 28, 664, 353, 839, -804, -480, 618, -346, 420, -64, -109, 86, -141, 126, 28, 846, -121, -414, -283, -759, -237, -369, 688, 496, -484, -400, 479, 150, 267, 430, 1185, -229},
            {1799, -697, 1227, 498, 153, -884, 963, -46, -628, 1240, -560, -330, -491, -151, -501, -211, -448, 428, 2671, -905, -478, -119, 326, 298, 1537, 1393, -74, 747, 163, -84, -487, -492, 263, -77, -650, 399, -206, -293, 802, 365, -649, -378, 11, 394, 89, 216, -206, -1263, 351, -269, 409, -81, -508, 1347, -193},
            {616, 2140, -147, 1155, -396, -461, 142, 34, 18, 723, 301, 418, -370, 478, 398, 71, -427, -228, -3826, -431, 535, -356, 367, -348, 515, -1187, -281, -595, -711, -2493, -704, -63, 1138, -48, 490, 139, 303, 65, 499, 62, -255, 29, -108, 88, -744, -2474, -237, 667, -485, 78, 286, 466, -423, -497, -272},
            {-848, -256, 305, -112, -280, 689, 742, 448, -216, -981, -1004, -130, -381, -184, 386, 436, -34, -318, 167, 384, 51, 274, 497, -320, 2056, -764, -497, 290, 1029, -842, 377, -195, -36, -398, -17, 696, -361, -823, 234, -430, -6, 596, -1671, 486, -1545, 2760, 416, -2853, 518, 353, -359, 68, 71, -267, -147},
            {-1252, -975, 1104, -657, 106, 208, 144, 180, 224, -863, -409, 49, 119, 516, 469, -349, 46, 329, 887, 481, 184, 326, -476, -294, -694, -1707, 9, -202, -440, -97, 1127, 106, -621, -39, -426, 871, -221, 27, -589, 438, -38, -181, -371, -744, -3868, -789, 514, 265, 384, -99, 76, 205, 247, -281, -302},
            {-210, 365, 475, 550, 726, -697, -734, 103, 196, -216, -2023, -287, 496, -253, -346, 560, 314, -462, -824, 369, 123, -470, 368, -278, -1480, 395, 682, 445, -465, -742, 1361, -515, 265, 687, 378, -444, 26, 528, 481, 567, 498, -1439, 59, -254, -1369, -938, 460, -1836, 428, 278, -214, 209, -796, 710, 72},
            {2138, -524, -54, 167, 48, -698, 127, -736, -164, 393, -143, -175, 133, 2, 143, -726, -251, -412, 4419, -316, -81, -542, 169, 277, 444, -576, 129, -723, -379, -657, -938, 71, 136, 290, 103, -61, -59, -113, -207, 158, 113, -251, -644, 57, -2385, 634, -440, -3369, -79, 44, -370, -72, 87, 1500, -488},
            {1423, -1059, 471, -280, 160, 747, 773, -73, -1072, 717, 838, -444, 29, 291, -289, -3, 208, -12, 3744, 334, 60, 428, -510, -1, 288, 204, -549, 98, -676, -2492, 863, 317, 329, -417, -435, 801, 75, 324, 10, 431, 10, -281, -466, 78, -2084, 170, -430, -943, 444, -5, 229, 340, 205, 215, -207},
            {2027, -521, 90, -138, -133, -839, -103, 120, -625, 136, 1085, -43, -40, -6, 255, -880, 156, 441, 5522, -98, -296, 152, -19, 306, -326, 194, 1220, -992, -176, -197, -74, -474, -56, -341, 330, 304, 343, -120, -77, 450, -159, -15, -640, 98, -434, 1035, -528, -1509, 145, -498, -462, 503, -273, 603, -434},
            {2189, 1053, 682, 198, 268, -152, -252, 81, -478, 1403, 1393, 350, -475, -526, 283, -536, 167, -396, -1300, -1129, -408, -159, 544, -513, 896, 1028, -880, -1491, 203, 70, -628, -22, 62, 9, 29, 298, 56, -548, 73, -389, -113, 896, 833, -1, -284, -19, -294, -571, 434, 375, -14, 26, -426, -326, -537},
            {-1886, -289, 355, -164, -291, 217, -172, 346, -615, -618, 2000, 416, 23, 433, 237, -550, -298, 522, -1905, 427, 290, 262, -266, 369, -108, 804, 153, -395, 1315, 1658, 25, 98, 70, 133, 442, 260, -88, 69, 520, 259, -273, 548, -1523, -623, -613, -1548, -1, -203, 95, -3, -65, -292, -40, 422, 123},
            {655, -1087, 356, -353, 107, 563, -226, -203, 22, 1721, -977, -19, -24, -106, -299, 577, 134, -211, -8378, 413, 65, 496, -352, 172, -566, 111, -870, 168, -62, -2697, -1233, 446, -551, 44, -600, -159, -517, 42, 426, -39, -127, 202, 949, 11, -1839, -991, 340, -1915, 92, -214, 147, 433, -471, 706, 352},
            {326, -986, 537, 36, -258, -301, -81, 104, 156, -277, -637, 428, -197, 198, 425, 320, 392, -142, 1778, -301, 102, -301, 537, -28, 1364, 791, -268, -145, 578, -199, 439, -299, -336, -218, -847, -243, 529, -43, -30, -592, -640, 1080, 534, 1159, 312, 1725, -22, -167, 402, -109, -318, 275, -191, -956, -23},
            {826, 1164, 1575, 18, -578, 272, -83, -78, 187, -1266, 395, 352, -151, -256, -326, 350, -390, -115, 2838, 23, 155, 150, 322, -68, -1101, -126, 911, 518, 316, 965, -411, -28, -42, 89, -595, 293, -447, -55, -313, 109, 107, 301, -414, 152, 2599, 376, -176, -619, 315, 261, 419, 186, 46, 209, -537},
            {-3146, -485, -10, 450, -236, -167, 518, -191, 44, -689, -388, 297, 544, 477, -329, -202, 473, 539, -4683, 149, 280, -109, -247, -266, -76, 437, 315, -213, -345, -464, 1822, -301, 464, -144, 34, 264, -172, 77, 338, 215, 21, 72, -696, -50, 2057, 1375, -466, -1637, 129, 299, -151, 413, -386, 724, -188},
            {618, 768, -1861, -42, 396, 5, -68, 260, 34, 403, 957, 481, 221, 149, 299, -411, -130, 40, 2812, 351, -116, -423, -212, -365, 625, 136, 534, -731, 786, 63, 55, 13, -131, 202, 162, -391, -168, 368, 450, -132, 411, -582, -330, -72, 458, 197, 244, -1240, 379, -517, 522, 71, -828, -180, 516},
            {-1302, -337, -1575, 212, -559, -198, -189, 421, -473, -310, -363, 113, -455, -404, 39, -611, 150, -393, 250, 18, -528, 242, -92, -101, 195, -173, -582, -2591, -1575, -311, -70, 507, 211, -273, -65, -996, 438, -320, 875, -661, 4, 1420, -628, 1080, 528, 949, 490, 865, -257, 417, 414, -9, 167, 15, 227},
            {530, -620, -1330, -307, 467, 5, -491, 115, -79, 2017, -96, -126, -248, 468, -115, 800, 100, 319, 972, -17, 230, 522, 531, -297, -433, -1165, 526, -1162, 157, -710, 438, -37, 133, 69, 314, -802, -4, 351, 409, -212, -896, 126, -381, 117, -3123, -1094, -446, -73, -108, 370, 518, 259, 149, 549, -216},
            {1743, 98, -784, 40, 0, -202, -176, 372, -448, 882, -579, 295, -366, 387, 90, -914, 76, -186, -1244, -995, 504, -453, 437, -404, 2496, -1574, -2698, -2107, 1147, -869, -1034, -269, 96, 663, -386, -481, 186, 208, -329, 625, -86, -693, 319, -1305, 529, 1416, -18, -211, -165, -113, -43, -285, -105, -641, -18},
            {1575, -727, -9, -241, -49, -311, -111, 578, 333, -2514, -171, 478, -447, -470, -293, -829, -438, -58, 3420, -1122, -260, -274, 18, -437, 216, 475, -235, 1270, 739, 338, -10, 432, -86, -5, 674, 35, -318, -13, 637, 257, 340, 81, -216, -352, -6, 1793, 163, -1021, -32, 20, 235, -179, -18, 789, -99},
            {-2889, -226, 15, 299, 75, -99, -85, 189, 44, 2286, -84, -38, 48, 46, -163, 1426, 249, 529, -127, 534, -427, 320, -467, 105, -343, -275, -329, -267, -1141, -305, 1330, 206, 242, -28, 0, -246, -16, -2, -407, 161, 132, 74, 697, -189, 1251, -275, 167, 338, 9, 478, -445, 316, -119, -1244, 78},
            {413, 11, 829, 148, -568, 63, -232, -167, 373, 360, 405, -58, -426, 3, -304, 217, -186, -341, 877, -363, -407, 297, -503, 114, 1278, -2411, 542, -734, -664, 1133, -87, 280, 512, 358, 829, 10, 209, -39, 912, -240, -609, -142, -592, -44, 158, 712, 170, 721, 481, 249, 419, 275, 525, 655, 223},
            {-2408, -646, 179, -564, -46, -47, -125, 261, 80, -1248, -280, 255, -53, -442, -78, 414, -492, 350, -1339, -162, -171, -317, 106, 450, -711, 519, -242, 551, 101, 774, 1397, -341, -431, 165, 721, -13, -163, 13, 297, -302, 41, -138, -614, 140, -1484, -2612, 231, -512, 411, 332, -183, 125, 42, 1005, -60},
            {1037, 189, 164, 8, 42, -48, -242, -337, 3066, 1005, 3388, -427, 407, -368, 511, 72, 80, -322, 176, 559, 16, 13, 121, -250, 316, -435, -613, -203, -202, -440, -1741, 535, 111, 113, -6, -96, 209, 156, -61, 43, -197, 65, -143, -465, -165, -1217, -530, -7175, 505, -22, 17, -462, 253, 138, -25},
            {1093, -698, 338, 599, 5, 605, 347, -167, -121, 2503, -173, -7, 448, 357, -83, -7, -248, -119, 4080, 462, 234, -146, -532, -426, 214, 151, 517, 287, -507, 419, -1143, 502, 191, 217, -89, 246, 147, 131, 339, -575, -560, -89, -2304, 130, -2548, -1666, -394, -1344, -311, 280, 462, 96, 962, 927, 125},
            {-1079, -1381, -547, -954, 216, 443, 65, 325, -93, -1545, 617, -212, -39, -212, 251, -170, -14, -395, 2369, 14, -316, -311, 530, -455, -464, -461, -94, -1109, -319, -124, 1454, -160, -962, 309, -964, -524, -397, -219, -1294, -854, -87, 68, -374, -183, -2634, -254, -535, -548, -464, -31, -119, 122, -57, -1057, -262}
        };
        static constexpr short b0[32] = {-759, -737, -129, -354, 1, 599, -342, -885, 173, 389, -334, -148, -521, -133, -319, -1205, 102, 614, 322, 280, 761, 33, -10, 447, -1254, -1654, 658, 90, -107, 626, 104, 225};
        std::array<float,32> h0{};
        for (int j = 0; j < 32; ++j) {
            float value = b0[j];
            for (int k = 0; k < 55; ++k) value += w0[j][k] * features[k];
            h0[j] = std::max(0.f, value / 4096.f);
        }
        static constexpr short w1[24][32] = {
            {275, 492, 125, -574, -418, 1010, 1233, 88, -214, -1347, 98, 129, -563, 812, -763, -4302, 436, -1681, 1834, -893, 2034, -385, -366, -855, -1736, 1309, 1073, 1329, -239, 165, -2016, 362},
            {290, 1399, 450, -498, -318, -612, -214, 419, 948, -847, -501, 286, 11, -46, 401, -76, -758, 1109, -1148, 531, -833, 900, 752, -774, -2869, -1515, -1982, -833, -4161, -75, -899, 1445},
            {1535, 429, 746, 793, 282, -229, 163, 1115, -795, 753, -31, 786, 1339, 169, 1486, 1896, -1203, 433, 30, -137, -2852, -299, -1125, -483, -1062, 1222, 2695, 483, -276, 1694, 1096, -908},
            {418, -234, 956, 953, 407, -193, 13, 361, -156, 1368, -1, 369, 101, 1062, 556, 942, -62, -104, 535, 1463, -331, 174, -267, -882, -305, 1874, 2194, 608, 1183, 1089, 593, -1075},
            {532, -819, 625, 157, -579, 1341, 1044, -558, 1463, -826, -1483, -265, 234, 236, 1058, -1202, 1149, 838, -285, 565, 325, 965, 1301, 246, 1548, 138, 894, -273, -1549, 38, 817, 472},
            {-837, -697, -145, 378, 72, -646, -412, -1451, -413, -2300, -538, -1502, -724, -326, -46, 1391, -621, 201, 1373, -802, -2567, 908, -148, 368, 518, 2122, -638, 1159, -956, 413, -357, -459},
            {-325, -314, -267, -476, -698, 124, 379, -125, -68, -44, 37, -467, -137, -210, -399, 180, -421, -153, 360, -328, -221, -414, -610, -136, -618, -219, -439, -737, -762, -474, -279, -836},
            {1277, 294, 1035, 1365, 1024, 360, 1266, 845, 341, -152, -1680, 6, 1176, 857, 1304, 933, -669, 353, -1230, -246, -2667, 652, -583, 300, -445, 2085, 3929, -280, -1341, 1960, 841, -888},
            {-390, -783, -16, -349, 464, -262, -106, -247, -232, -679, 182, 300, -105, 355, -142, -226, 355, -752, -164, -143, -285, -226, -521, -70, 217, 231, -607, 94, -506, -35, -418, -291},
            {-501, -379, 190, -222, 241, -746, -836, -142, -86, -803, 179, -52, -666, -812, -967, -111, 451, 628, -107, 105, 197, 165, 22, -396, -416, 227, -411, -612, 64, -395, 143, 96},
            {1183, 486, 168, 210, 215, -218, 975, 1333, 884, -1056, -1683, -790, 1203, -264, 621, -1340, 764, 1359, 124, 456, -2898, 344, -767, 145, -938, 864, -1450, 75, -2407, 383, 569, -84},
            {1336, 839, -648, 536, 464, -44, -426, 400, 2069, -1352, 693, 24, 74, -50, -199, -109, -78, 1431, -296, 850, -1869, 378, -647, -1481, -1233, -582, -860, -542, -1525, 142, 665, -1318},
            {527, -2849, 303, 1038, 170, -367, 376, 958, 547, -1489, 1075, 703, -165, 984, -141, -833, -1655, -47, 896, -177, -225, -126, 963, -745, -5770, 1012, 1223, 68, -2208, 631, -463, -319},
            {925, 168, 354, -100, 681, 577, 526, 806, 668, -10, -2978, -1244, 980, -578, 936, -1098, 1090, 2680, -1791, 11, -1540, -330, 530, 461, -129, 1489, 1269, -190, -1582, 1734, 1390, -2046},
            {1147, 150, -354, 481, 707, 1215, 523, -97, 92, -1218, -514, -584, 1313, 684, 711, -439, 679, 1021, 307, -1217, -413, 491, 923, 694, 320, 1560, 2900, 420, 364, 752, -348, 295},
            {1591, 605, 344, 574, -375, 805, 700, 940, 698, -147, -781, -797, 776, -489, -39, 695, -1020, 1526, -160, 422, -3175, 140, -445, -386, -2412, 1035, -611, -44, -2841, 460, 1448, -1283},
            {1259, -452, -694, 366, 306, 184, 120, -570, 297, 908, 1173, 1005, 29, -530, -60, 2190, -2499, -374, 1159, 1474, -169, -107, -1274, -13, -3489, -491, 351, 742, -928, -684, 219, 77},
            {31, 14, 502, -1512, -1152, 384, -723, 818, -1196, 2062, -204, 1407, -820, 171, -550, 2255, -598, -225, 1301, 838, 1963, 18, -713, -372, -4871, -3707, -3387, -1041, 935, -244, 671, -658},
            {867, 379, 12, 395, 1363, -563, 861, 810, -1346, 838, -121, 430, 1176, 576, 25, 1477, -1132, -222, -285, -367, -935, -754, -784, -378, -1449, 1664, 3159, 355, 1109, 813, 351, -595},
            {53, 456, 749, 264, 73, -852, 1176, -284, -940, -238, -63, -449, 691, 343, -604, -1292, 304, -1029, 53, -1078, 543, 760, 406, 913, 767, 1532, 1716, 102, 1592, 740, -244, 1285},
            {380, -147, 1493, 623, 119, -908, 1256, -211, -1, -798, -2638, -1021, 988, -105, 1241, -1412, 1734, 1131, -1318, -636, 600, 541, 1080, -384, 2935, 1179, 6, -111, -753, 609, 261, 677},
            {832, 57, -63, 89, -181, -456, -919, 1012, -920, 430, 1326, 339, -427, 504, 253, 1540, -426, -471, 761, 101, 15, -695, -1158, 649, -605, 88, 357, 885, 2244, 512, -531, 626},
            {42, 1388, -1037, 1277, -169, 109, 351, -737, -2106, 945, 538, 94, 850, 155, 272, 1664, -1522, 1399, -215, 359, -964, -975, -3577, 523, -1454, -178, -1001, 749, -437, 1278, 1440, -3091},
            {743, -523, 769, 139, 1427, 67, -110, 1312, -402, -350, -360, -244, 1459, 1706, 1689, 1738, -461, 52, -62, 377, -2405, 65, -118, 252, 1005, 1287, 3304, 489, -1102, 1651, 1128, -483}
        };
        static constexpr short b1[24] = {-734, 538, -426, 211, -445, -306, -247, 5, -703, -843, -988, -166, -779, -639, -397, -259, 353, 404, -577, -141, -237, -89, -656, 182};
        std::array<float,24> h1{};
        for (int j = 0; j < 24; ++j) {
            float value = b1[j];
            for (int k = 0; k < 32; ++k) value += w1[j][k] * h0[k];
            h1[j] = std::max(0.f, value / 4096.f);
        }
        static constexpr short w2[8][24] = {
            {-1806, 2281, -300, -86, 570, -534, -69, 307, 43, 765, 589, 1585, 827, 751, -744, 1240, 929, 679, -735, -664, 83, -1529, 627, -559},
            {-305, 654, 806, 601, -767, -678, 223, -760, -478, -337, -335, -1615, 2732, -549, -857, 203, 1152, -1418, 134, -1264, -1221, 853, 1663, 317},
            {-2061, 2694, -85, -981, 462, -1353, 698, -402, 126, -266, 1539, 1601, 1000, 1388, 84, 1003, 766, 1223, -779, -799, -680, -911, 455, -94},
            {-647, 571, 171, 182, -898, 2043, -420, 69, -98, -232, -540, -1536, 3459, -1073, -479, 799, 1606, -1113, 666, -401, -1808, 792, 2519, -405},
            {-1855, 2731, -465, -1198, 86, -1552, 121, -77, -571, 656, 1996, 1728, 1036, 1629, 213, 482, 714, 1329, -836, -777, -1060, -377, 217, 148},
            {-782, 413, 379, 182, -747, 4058, -614, -39, 188, -254, 209, -1194, 3545, -1478, -73, 67, 1641, -833, 765, -95, -2085, 109, 2937, -593},
            {-2262, 1789, -1523, -1032, -1792, 524, -317, -1617, 364, 514, -1886, -1481, -310, -8455, -2436, -1311, -2966, 4329, -2134, 275, 456, -28, -2865, -1245},
            {-1958, -128, 2573, 1020, -686, -262, -33, 2018, 496, 717, 2315, 2895, -1308, 4616, 236, 1253, -887, -5852, 3479, 134, 2990, 247, 4448, 2318}
        };
        static constexpr short b2[8] = {654, 155, 763, -47, 624, -91, -183, -1312};
        std::array<float,8> h2{};
        for (int j = 0; j < 8; ++j) {
            float value = b2[j];
            for (int k = 0; k < 24; ++k) value += w2[j][k] * h1[k];
            h2[j] = value / 4096.f;
        }
        return h2;
    }

    std::pair<Point,Point> blade_velocities(const Obs& obs,int k,Point carry) const {
        const auto& cfg=info_->cfg;
        const auto& player=obs.p[k];
        if (cfg.delay!=4 || player.st!=S_FREE || obs.t<8) return {carry,carry};
        const auto read=motion_read(obs,k);
        const auto samples=predict_blade_motion(read.x);
        const auto project=[&](int i) {
            return Point{i64((read.axis.x*samples[i]-read.axis.y*samples[i+1])*cfg.run_v/UNIT),
                          i64((read.axis.y*samples[i]+read.axis.x*samples[i+1])*cfg.run_v/UNIT)};
        };
        Point mid=project(0);
        mid={mid.x+(mid.x-player.vx)/5,mid.y+(mid.y-player.vy)/5};
        const auto late=project(2);
        const auto end=project(4);
        const Point next{(2*late.x+end.x)/3,(2*late.y+end.y)/3};
        constexpr double confidence=0.8;
        const auto blend=[&](Point v,double weight) {
            Point a{i64(carry.x+(v.x-carry.x)*weight),i64(carry.y+(v.y-carry.y)*weight)};
            cap_len(a.x,a.y,cfg.run_v);
            return a;
        };
        return {blend(mid,confidence),blend(next,confidence*.82)};
    }

    MotionSample self_at(i64 tick, const Obs& obs) const {
        tick = std::clamp<i64>(tick, 0, obs.t);
        const auto& sample = samples_[size_t(tick) % samples_.size()];
        if (sample.tick == tick) return sample;
        return {tick, position(obs.me()), {obs.me().vx, obs.me().vy}, obs.me().st, obs.me().timer, {obs.me().ax, obs.me().ay}};
    }

    // Reconstruct the position a shooter can predict from our last visible commitment.
    static Point advertised_landing(const MotionSample& victim, i64 lead,
                                    const Config& cfg, const Map& floor) {
        Point at = victim.position, velocity = victim.velocity;
        i64 state = victim.state, timer = victim.timer;
        for (i64 step = 0; step < lead; ++step) {
            const bool airborne = state == S_DASH && timer > cfg.dash_post &&
                                  timer <= cfg.dash_post + cfg.dash_t;
            if (airborne) {
                velocity = {rdiv(victim.heading.x * cfg.dash_v, UNIT),
                            rdiv(victim.heading.y * cfg.dash_v, UNIT)};
            } else if (state != S_FREE && state != S_STUN) {
                Point brake{-velocity.x, -velocity.y};
                cap_len(brake.x, brake.y, cfg.acc);
                velocity = velocity + brake;
            }
            Point next = at;
            move_body(floor, cfg.body_r, next.x, next.y, velocity.x, velocity.y);
            if (!airborne && state != S_STUN && !walkable(floor, next)) break;
            at = next;
            if (airborne && timer == cfg.dash_post + 1) {
                velocity.x = rdiv(velocity.x * cfg.run_v, std::max<i64>(1, cfg.dash_v));
                velocity.y = rdiv(velocity.y * cfg.run_v, std::max<i64>(1, cfg.dash_v));
            }
            if (state == S_STUN) {
                velocity.x = rdiv(velocity.x * cfg.kick_keep, UNIT);
                velocity.y = rdiv(velocity.y * cfg.kick_keep, UNIT);
            }
            if (timer > 0 && --timer == 0) {
                if (state == S_SLASH) { state = S_SLASHED; timer = cfg.slash_post; }
                else if (state == S_KICK) { state = S_KICKED; timer = cfg.kick_post; }
                else state = S_FREE;
            }
        }
        return at;
    }

    void remember(const Obs& obs) {
        samples_[size_t(obs.t) % samples_.size()] =
            {obs.t, position(obs.me()), {obs.me().vx, obs.me().vy}, obs.me().st, obs.me().timer, {obs.me().ax, obs.me().ay}};
        const Config& cfg = info_->cfg;
        const i64 seen_at = std::max<i64>(0, obs.t - cfg.delay);
        for (int k = 0; k < obs.n; ++k) {
            const Dart& dart = obs.d[k];
            const Player& player = obs.p[k];
            if (k != obs.seat && player.alive && player.vis && obs.t >= cfg.delay) {
                if (dart.st != D_HAND || player.st == S_THROW) showed_throw_[k] = true;
                if (player.st == S_SLASH || player.st == S_SLASHED) showed_blade_[k] = true;
                if (!showed_throw_[k] && dart.st == D_HAND && player.st == S_FREE) {
                    const Point target = self_at(obs.t - 2*cfg.delay, obs).position;
                    const double gap = distance(target,position(player));
                    if (gap > cfg.slash_reach && gap < cfg.range &&
                        !wall_between(info_->M(),target.x,target.y,player.x,player.y))
                        ++throw_opportunities_[k];
                }
            }
            if (k == obs.seat || !player.vis || dart.st != D_OUT || dart.age > 2) continue;
            const i64 release = seen_at - dart.age;
            if (release <= last_release_[k]) continue;
            last_release_[k] = release;
            const i64 action_tick = release - cfg.throw_pre;
            const MotionSample victim = self_at(action_tick - cfg.delay, obs);
            Point source = position(dart);
            for (i64 age = 0; age < dart.age; ++age) {
                const i64 length = std::max(cfg.dart_vmin, cfg.dart_v - age * cfg.dart_dec);
                source = source - Point{rdiv(length * dart.vx, UNIT), rdiv(length * dart.vy, UNIT)};
            }
            const double gap = distance(victim.position, source);
            if (gap > cfg.range + 1000) continue;
            const i64 travel = i64(gap / std::max(1.0, 0.84 * cfg.dart_v));
            const std::array<i64, 3> leads{{0, cfg.delay + cfg.throw_pre,
                                          cfg.delay + cfg.throw_pre + travel}};
            int best = 0;
            i64 agreement = -2 * UNIT * UNIT;
            for (int model = 0; model < int(leads.size()); ++model) {
                const i64 lead = leads[size_t(model)];
                const Point predicted = victim.position + Point{victim.velocity.x * lead,
                                                                victim.velocity.y * lead};
                const Point heading = direction(predicted - source);
                const i64 similarity = heading.x * dart.vx + heading.y * dart.vy;
                if (similarity > agreement) {
                    best = model;
                    agreement = similarity;
                }
            }
            if (agreement >= 930000) {
                aim_confidence_[k][size_t(best)] =
                    std::min(5, aim_confidence_[k][size_t(best)] + 1);
            }
        }
    }

    std::vector<std::vector<Point>> cover_patches_;

    void index_grass(const Map &map) {
        cover_patches_.clear();
        std::vector<char> visited(map.c.size());
        for (int y = 0; y < map.ny; ++y)
            for (int x = 0; x < map.nx; ++x) {
                const int root = y * map.nx + x;
                if (visited[root] || map.at(x, y) != C_GRASS) continue;
                std::vector<int> queue{root};
                visited[root] = 1;
                cover_patches_.emplace_back();
                for (size_t i = 0; i < queue.size(); ++i) {
                    const int tile = queue[i], a = tile % map.nx, b = tile / map.nx;
                    cover_patches_.back().push_back({a * CELL + CELL / 2, b * CELL + CELL / 2});
                    for (int j = -1; j <= 1; ++j)
                        for (int k = -1; k <= 1; ++k) {
                            if (!map.inside(a + j, b + k) || map.at(a + j, b + k) != C_GRASS) continue;
                            const int next = (b + k) * map.nx + a + j;
                            if (!visited[next]) {
                                visited[next] = 1;
                                queue.push_back(next);
                            }
                        }
                }
            }
    }

    void locate_in_cover(Obs &obs, int foe) const {
        const Config &c = info_->cfg;
        if (c.run_v > CELL || c.expose < c.delay + 1 || cover_patches_.empty()) return;
        const auto &frames = motion_history_[foe];
        const OpponentFrame *last = nullptr;
        for (const auto &f : frames)
            if (f.tick >= 0 && (!last || f.tick > last->tick)) last = &f;
        const i64 observed = std::max<i64>(0, obs.t - c.delay);
        const i64 age = last ? observed - std::max<i64>(0, last->tick - c.delay) : observed;
        if (age < 0 || age > 120) return;
        const Point start = last ? position(last->player) : Point{info_->spx[foe], info_->spy[foe]};
        const Point viewer = self_at(observed, obs).position;
        const double first_step = CELL * .708 + c.run_v + (last && last->player.st == S_STUN ? c.kick_v : 0);
        Point sum{};
        int count = 0;
        Point low{10000000, 10000000}, high{-10000000, -10000000};
        for (const auto &patch : cover_patches_) {
            if (std::none_of(patch.begin(), patch.end(),
                             [&](Point p) { return distance(start, p) <= first_step; }))
                continue;
            for (Point p : patch) {
                if (distance(p, start) > age * c.run_v + first_step ||
                    distance(p, viewer) < c.see_r - CELL * .708 ||
                    cell_of_point(c, info_->M(), p.x, p.y, observed) != C_GRASS)
                    continue;
                low.x = std::min(low.x, p.x);
                low.y = std::min(low.y, p.y);
                high.x = std::max(high.x, p.x);
                high.y = std::max(high.y, p.y);
                if (distance(low, high) > 1450) return;
                sum = sum + p;
                ++count;
            }
        }
        if (!count) return;
        const Point p{sum.x / count, sum.y / count};
        if (cell_of_point(c, info_->M(), p.x, p.y, observed) != C_GRASS ||
            distance(p, viewer) < c.see_r - CELL * .5)
            return;
        Player &rival = obs.p[foe];
        rival.vis = 1;
        rival.x = p.x;
        rival.y = p.y;
        rival.st = S_FREE;
        rival.expose = 0;
        rival.vx = rival.vy = 0;
        rival.timer = 0;
        rival.cd = last ? std::max<i64>(0, last->player.cd - age) : 0;
        obs.d[foe].x = p.x;
        obs.d[foe].y = p.y;
    }

    // Opponent observations are delayed. Score plausible throws from that
    // unseen interval using the positions the opponent could have seen.
    ThrowPaths estimate_unseen_throws(const Obs& obs, const Game& game, const Map& floor) const {
        const Config& cfg = game.cf;
        constexpr int base = 2100;
        ThrowPaths paths{{}, cfg.body_r + cfg.dart_r};

        const i64 observed_tick = std::max<i64>(0, obs.t - cfg.delay);
        for (int k = 0; k < obs.n; ++k) {
            Player seen = obs.p[k];
            i64 estimate_tick = observed_tick;
            if (!enemy(game, obs.seat, k) || !seen.vis ||
                seen.st == S_THROW || distance(position(seen), position(obs.me())) > cfg.range + 2200) {
                continue;
            }
            // A returning or dropped dart can reach the owner during our observation delay.
            // Start the next throw at the first simulated frame in which they have it in hand.
            if (obs.d[k].st != D_HAND) {
                if (distance(position(seen), position(obs.d[k])) >
                    cfg.ret_v * (cfg.delay + 3) + cfg.pick_r) continue;
                Game pickup(*info_, obs);
                pickup.t = observed_tick;
                pickup.p[obs.seat].unknown = 1;
                pickup.d[obs.seat].st = D_GONE;
                Inputs drive{};
                for (int j = 0; j < game.n; ++j) {
                    if (j == obs.seat || !pickup.p[j].vis) continue;
                    const Player& visible = obs.p[j];
                    drive[j] = move({visible.vx * UNIT / cfg.run_v, visible.vy * UNIT / cfg.run_v});
                }
                if (obs.d[k].st == D_GROUND) {
                    drive[k] = move(direction(position(obs.d[k]) - position(seen)));
                }
                while (pickup.t < obs.t + 3 && !pickup.over && pickup.d[k].st != D_HAND) {
                    advance(pickup, drive, obs.seat);
                }
                if (pickup.d[k].st != D_HAND || !pickup.p[k].alive) continue;
                seen = pickup.p[k];
                estimate_tick = pickup.t;
            }
            i64 first_launch = estimate_tick;
            if (seen.st != S_FREE) first_launch += seen.timer;
            if (seen.st == S_SLASH) first_launch += cfg.slash_post;
            if (seen.st == S_KICK) first_launch += cfg.kick_post;
            // This is the last future windup for which the shooter's view of us is known.
            const i64 latest_launch = obs.t +
                std::min<i64>(cfg.delay, ThrowPaths::horizon - 1 - cfg.throw_pre);
            for (i64 start = first_launch; start <= latest_launch; ++start) {
                const i64 release = start + cfg.throw_pre;
                Point source = position(seen);
                i64 vx = seen.vx;
                i64 vy = seen.vy;
                for (i64 tick = estimate_tick; tick < release; ++tick) {
                    if (tick >= start) cap_len(vx, vy, cfg.run_v * cfg.throw_move / 100);
                    move_body(floor, cfg.body_r, source.x, source.y, vx, vy);
                }
                const MotionSample victim = self_at(start - cfg.delay, obs);
                const double gap = distance(victim.position, source);
                const i64 travel = i64(gap / std::max(1.0, 0.84 * cfg.dart_v));
                // Opponents often throw on the first tick they see a dash begin.
                const bool announced = victim.state == S_DASH &&
                    victim.timer == cfg.dash_pre + cfg.dash_t + cfg.dash_post - 1;
                int prior = 100;
                if (announced && gap >= 2400 && gap < 7800) prior = 300;
                else if (victim.state == S_FREE && gap > 4600) prior = 70;
                else if (gap < 2700) prior = 120;
                const i64 short_lead = cfg.delay + cfg.throw_pre;
                const i64 full_lead = short_lead + travel;
                const std::array<i64, 5> leads{{0, short_lead, full_lead, full_lead, full_lead}};
                const int models = victim.state != S_FREE ? 4 : 5;
                for (int model = 0; model < models; ++model) {
                    Point target;
                    int weight = base;
                    if (victim.state != S_FREE && model == 3) {
                        target = advertised_landing(victim, full_lead, cfg, floor);
                        weight = base * (announced ? 250 : 180) / 100;
                    } else if (model >= 3) {
                        const MotionSample older = self_at(start - cfg.delay - 3, obs);
                        Point displacement = victim.position - older.position;
                        cap_len(displacement.x,displacement.y,3*cfg.run_v);
                        if (model == 3) {
                            target = victim.position + Point{rdiv(full_lead * displacement.x,3),
                                                             rdiv(full_lead * displacement.y,3)};
                            weight = base * 100 / 100;
                        } else {
                            // Broadens aim guesses when a shooter's search can intercept a turn.
                            const Point cross = direction({source.y-victim.position.y,victim.position.x-source.x});
                            const i64 sign = cross.x*displacement.x + cross.y*displacement.y >= 0 ? 1 : -1;
                            const i64 predicted_turn = std::clamp<i64>(full_lead*cfg.run_v/2,600,1500);
                            target = victim.position - Point{rdiv(sign*cross.x*predicted_turn,UNIT),
                                                             rdiv(sign*cross.y*predicted_turn,UNIT)};
                            weight = base * 85 / 100;
                        }
                    } else {
                        const i64 lead = leads[size_t(model)];
                        target = victim.position + Point{victim.velocity.x * lead, victim.velocity.y * lead};
                        weight += base * aim_confidence_[k][size_t(model)] / 10;
                        weight = weight * prior / 100;
                    }
                    const Point heading = direction(target - source);
                    if (!heading.x && !heading.y) continue;
                    Point point = source;
                    i64 remaining = cfg.range;
                    for (i64 tick = release; tick < obs.t + ThrowPaths::horizon && remaining > 0; ++tick) {
                        const i64 age = tick - release;
                        const i64 length = std::min(remaining, std::max(cfg.dart_vmin, cfg.dart_v - age * cfg.dart_dec));
                        Point end = point + Point{rdiv(length * heading.x, UNIT),
                                                  rdiv(length * heading.y, UNIT)};
                        i64 hx, hy;
                        const bool wall = seg_wall(floor, point.x, point.y, end.x, end.y, hx, hy);
                        if (wall) end = {hx, hy};
                        if (tick >= obs.t) paths.segments[size_t(tick - obs.t)].push_back({point, end, weight});
                        point = end;
                        if (wall) break;
                        remaining -= length;
                    }
                }
            }
        }
        return paths;
    }

    void advance(Game& game, Inputs inputs, int seat, Action action = {}) const {
        for (int k = 0; k < game.n; ++k) {
            if (k == seat || !game.p[k].alive || game.p[k].unknown) continue;
            const bool free = game.p[k].st == S_FREE;
            const bool can_dash_kick = !free && game.cf.dash_slash &&
                game.dash_moving(game.p[k]) && game.d[k].st != D_HAND;
            if (!free && !can_dash_kick) continue;
            // In a close exchange an armed rival can swing while we are winding up.
            if (!close_combat(k) && game.d[k].st == D_HAND && game.p[seat].alive &&
                !game.p[seat].unknown && !game.mates(k, seat)) {
                const Player& self = game.p[seat];
                const Player& foe = game.p[k];
                const Point gap = position(self) - position(foe);
                const Point next = gap + Point{self.vx, self.vy};
                const double reach = game.cf.slash_reach;
                if (distance(gap) < reach + 250 && distance(next) < reach &&
                    !wall_between(game.LM(), self.x,self.y,foe.x,foe.y)) {
                    inputs[k] = act(A_MELEE,next);
                    continue;
                }
            }
            // An empty-handed opponent can start a kick before our delayed view
            // shows the windup. Simulate the kick against the approach we are testing.
            if (!game.p[seat].unknown && game.p[seat].alive && !game.mates(seat, k) &&
                game.d[k].st != D_HAND && game.p[seat].st != S_STUN &&
                game.p[seat].kick_t < game.t) {
                const auto& me = game.p[seat];
                const auto& foe = game.p[k];
                const Point to_me = position(me) - position(foe);
                if (distance(to_me) > game.cf.kick_reach + game.cf.dash_v + game.cf.run_v) {
                    if (game.p[k].st == S_FREE) keep_enemy_grounded(game, k, inputs[k]);
                    continue;
                }
                Point me_next = position(me), foe_next = position(foe);
                Point my_speed{me.vx, me.vy}, their_speed{foe.vx, foe.vy};
                const Map& terrain = game.LM();
                const bool committed = action.act == A_MELEE || action.act == A_DASH || me.st != S_FREE;
                const Point my_wish = committed ? Point{} :
                    Point{action.mx * game.cf.run_v / UNIT, action.my * game.cf.run_v / UNIT};
                for (i64 tick = 0; tick < game.cf.kick_pre; ++tick) {
                    Point dv = my_wish - my_speed;
                    cap_len(dv.x,dv.y,game.cf.acc);
                    my_speed = my_speed + dv;
                    if (game.dash_moving(me) && tick < me.timer - game.cf.dash_post &&
                        action.act != A_MELEE) {
                        scale_to(me.ax,me.ay,game.cf.dash_v,my_speed.x,my_speed.y);
                    }
                    cap_len(their_speed.x,their_speed.y,std::max<i64>(0,ilen(their_speed.x,their_speed.y)-game.cf.acc));
                    move_body(terrain,game.cf.body_r,me_next.x,me_next.y,my_speed.x,my_speed.y);
                    move_body(terrain,game.cf.body_r,foe_next.x,foe_next.y,their_speed.x,their_speed.y);
                }
                const Point projected = me_next - foe_next;
                if (distance(to_me) <= game.cf.kick_reach + game.cf.dash_v &&
                    distance(projected) <= game.cf.kick_reach &&
                    !wall_between(game.LM(), me.x, me.y, foe.x, foe.y)) {
                    inputs[k] = act(A_MELEE, projected);
                    continue;
                }
            }
            if (free) keep_enemy_grounded(game, k, inputs[k]);
        }
        inputs[seat] = action;
        game.step(inputs.data());
    }

    static void keep_enemy_grounded(Game& game, int k, Action& input) {
        const Config& cfg = game.cf;
        const Player& p = game.p[k];
        const auto velocity = [&](Action act) {
            i64 x = 0, y = 0;
            const i64 speed = cfg.run_v * std::min<i64>(UNIT, ilen(act.mx, act.my)) / UNIT;
            scale_to(act.mx, act.my, speed, x, y);
            x -= p.vx;
            y -= p.vy;
            cap_len(x, y, cfg.acc);
            return Point{p.vx + x, p.vy + y};
        };
        const Point expected = velocity(input);
        if (cell_of_point(cfg, game.M(), p.x + expected.x, p.y + expected.y, game.t) != C_PIT) return;

        const Map& terrain = game.LM();
        const auto grounded = [&](Point v) {
            i64 x = p.x, y = p.y;
            move_body(terrain, cfg.body_r, x, y, v.x, v.y);
            return cell_of_point(cfg, game.M(), x, y, game.t) != C_PIT;
        };
        if (grounded(expected)) return;

        Action alternate;
        double deviation = grounded(velocity({})) ? distance(expected, velocity({})) :
            std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < directions.size(); i += 2) {
            const Action step = move(directions[i]);
            const Point v = velocity(step);
            const double cost = distance(expected, v);
            if (cost < deviation && grounded(v)) {
                deviation = cost;
                alternate = step;
            }
        }
        if (std::isfinite(deviation)) input = alternate;
    }

    Game estimate(const Obs& obs, const Inputs& inputs) const {
        Game game(*info_, obs);
        // Own state is current; the other players and darts arrive several ticks late.
        game.t = std::max<i64>(0, obs.t - game.cf.delay);
        game.p[obs.seat].unknown = 1;
        game.d[obs.seat].st = D_GONE;
        while (game.t < obs.t && !game.over) advance(game, inputs, obs.seat);
        game.over = false;
        game.t = obs.t;
        game.p[obs.seat] = obs.me();
        game.p[obs.seat].team = info_->team[obs.seat];
        game.d[obs.seat] = obs.my_dart();
        return game;
    }

    // Check the whole commitment without opponents or hypothetical follow-up actions.
    // A legal jump can pass above a pit, but cancelling it needs solid ground now.
    static bool leaves_before_collapse(const Game& terrain, int seat) {
        const Config& cfg = terrain.cf;
        const Map& map = terrain.M();
        const Player& me = terrain.p[seat];
        const int x = cell_index(me.x), y = cell_index(me.y);
        const i64 start_fall = wave_fall_step(cfg, map.wave(x,y));
        const i64 dash_start = terrain.t + me.cd + cfg.dash_pre;
        if (start_fall < 0 || start_fall > terrain.t + std::max<i64>(7, me.cd + cfg.dash_pre + 3))
            return true;
        if (!map.inside(x,y)) return false;

        // Look for a strip of walkable ground that outlasts the landing cell.
        // Optimistic travel times leave room for waypoints near cell boundaries.
        const size_t count = size_t(map.nx) * map.ny;
        std::vector<int> visited(count, -1);
        std::queue<int> todo;
        const int first = y * map.nx + x;
        todo.push(first);
        visited[first] = 0;
        const int straight = std::max<i64>(1, CELL / cfg.run_v);
        const int diagonal = std::max<i64>(1, CELL * 7 / (5 * cfg.run_v));
        while (!todo.empty()) {
            const int node = todo.front();
            todo.pop();
            const int cx = node % map.nx, cy = node / map.nx;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const int a = cx + dx, b = cy + dy;
                if ((!dx && !dy) || !map.inside(a,b)) continue;
                const int next = b * map.nx + a;
                const int eta = visited[node] + (node == first ? 1 : dx && dy ? diagonal : straight);
                if (visited[next] >= 0 && visited[next] <= eta) continue;
                const i64 arrival = terrain.t + eta - 1;
                const i64 fall = wave_fall_step(cfg, map.wave(a,b));
                const int tile = map.at(a,b);
                if ((tile != C_FLOOR && tile != C_GRASS) || (fall >= 0 && fall <= arrival)) continue;
                if (fall < 0 || fall > start_fall) return true;
                if (arrival + 1 >= start_fall) continue;
                visited[next] = eta;
                todo.push(next);
            }
        }

        if (dash_start > start_fall || cfg.dash_v <= 0) return false;
        // The last running frame can already be over a pit. What matters is the
        // standing frame before takeoff and the ground through dash recovery.
        for (const Point d : directions) {
            Point at = position(me);
            for (i64 j = 0; j < cfg.dash_t; ++j) {
                i64 vx = rdiv(d.x * cfg.dash_v, UNIT), vy = rdiv(d.y * cfg.dash_v, UNIT);
                move_body(map, cfg.body_r, at.x, at.y, vx, vy);
            }
            const i64 arrival = dash_start + cfg.dash_t + cfg.dash_post;
            const i64 until = wave_fall_step(cfg, map.wave(cell_index(at.x),cell_index(at.y)));
            const int tile = cell_of_point(cfg, map, at.x, at.y, arrival);
            if ((tile == C_FLOOR || tile == C_GRASS) && (until < 0 || until > start_fall))
                return true;
        }
        return false;
    }

    Landing inspect_landing(const Game& game, int seat, Action action) const {
        const Config& cfg = game.cf;
        const Player& me = game.p[seat];
        if (action.act == A_MELEE && game.dash_moving(me) &&
            cell_of_point(cfg, game.M(), me.x, me.y, game.t) == C_PIT) {
            return {false, false};
        }

        const bool commitment = action.act != A_NONE || me.st != S_FREE;
        Game terrain = game;
        for (int k = 0; k < terrain.n; ++k) {
            if (k == seat) continue;
            terrain.p[k].unknown = 1;
            terrain.d[k].st = D_GONE;
        }

        Landing result;
        do {
            // Continue checking the terrain even if the combat simulation ended.
            terrain.over = false;
            advance(terrain, {}, seat, action);
            action = {};
            const Player& end = terrain.p[seat];
            if (!end.alive) return {false, false};
            if (!end.dashing && end.st != S_STUN) {
                result.crowded_edge |= exposed_to_body_push(game, seat, end, terrain.t - 1);
            }
            if (end.st == S_FREE && !end.vx && !end.vy) break;
        } while (terrain.t < cfg.steps);
        if ((commitment || terrain.t >= cfg.col_start - 7) && !leaves_before_collapse(terrain, seat)) result.collapse_trap = true;
        return result;
    }

    static bool exposed_to_body_push(const Game& game, int seat, const Player& at, i64 tick) {
        const Config& cfg = game.cf;
        const i64 margin = cfg.body_r * 115 / 100;
        const bool windup = at.st == S_DASH && at.timer >= cfg.dash_t + cfg.dash_post;
        const Point p = position(at);

        // An opponent who was ready to dash in the delayed view could already
        // reach us before our own dash starts moving.
        bool near = false;
        for (int k = 0; k < game.n; ++k) {
            const Player& other = game.p[k];
            if (k == seat || other.unknown || !other.alive) continue;
            // An opponent near the walking lane can have dashed since we saw them.
            const bool early_walk = at.st == S_FREE && tick <= game.t + 1 &&
                other.st == S_FREE && distance(p, position(other)) <= 3600;
            const bool could_dash = other.st == S_DASH || ((windup || early_walk) && other.cd <= cfg.delay);
            const i64 speed = could_dash ? cfg.dash_v : cfg.run_v;
            const i64 uncertainty = std::min<i64>(cfg.delay + tick - game.t + 1, 6);
            near |= distance(p, position(other)) <= 2 * cfg.body_r + uncertainty * speed;
        }
        if (!near) return false;
        for (size_t k = 0; k < directions.size(); k += 2) {
            const Point sample{p.x + directions[k].x * margin / UNIT,
                               p.y + directions[k].y * margin / UNIT};
            if (cell_of_point(cfg, game.M(), sample.x, sample.y, tick) == C_PIT) return true;
        }
        return false;
    }

    // Parrying fixes our footwork until recovery. An empty-handed opponent
    // may have time to kick us before we can move again.
    bool parry_exposes_pit(const Game& game, const Inputs& inputs, int seat, Action swing) const {
        const Config& cfg = game.cf;
        const Point from = position(game.p[seat]);
        bool kicker = false;
        for (int k = 0; k < game.n; ++k) {
            if (enemy(game, seat, k) && game.d[k].st != D_HAND &&
                distance(from, position(game.p[k])) <= cfg.kick_reach + (cfg.slash_pre + cfg.slash_post) * cfg.run_v) {
                kicker = true;
            }
        }
        if (!kicker) return false;
        Game trial = game;
        for (i64 i = 0; i < cfg.slash_pre + cfg.slash_post + cfg.kick_stun && !trial.over; ++i) {
            advance(trial, inputs, seat, i == 0 ? swing : Action{});
            const Player& me = trial.p[seat];
            if (!me.alive) return me.cause == K_FALL;
            if (me.st == S_FREE) break;
        }
        return false;
    }

    Action cancel_dangerous_dash(const Game& game, const Inputs& inputs, int seat) const {
        if (game.d[seat].st != D_HAND || !game.cf.dash_slash || !game.dash_moving(game.p[seat])) return {};
        const i64 horizon = game.p[seat].timer + 1;
        Game committed = game;
        for (i64 t = 0; t < horizon && !committed.over; ++t) advance(committed, inputs, seat);
        if (committed.p[seat].alive) return {};

        const Action brake = act(A_MELEE, {game.p[seat].ax, game.p[seat].ay});
        Game stopped = game;
        for (i64 t = 0; t < horizon && !stopped.over; ++t) {
            advance(stopped, inputs, seat, t == 0 ? brake : Action{});
        }
        const Landing landing = inspect_landing(game, seat, brake);
        if (stopped.p[seat].alive && landing.safe && !landing.crowded_edge) return brake;
        return {};
    }

    Action evade_during_dash(const Obs& obs, const Game& game, const Inputs& inputs, int seat) const {
        // Compare the rest of the dash with braking on safe ground when the
        // delayed view leaves multiple plausible incoming projectiles.
        constexpr int threshold = 1800;
        const auto& me = game.p[seat];
        const Config& cf = game.cf;
        const i64 horizon = std::min<i64>(ThrowPaths::horizon, me.timer + 2);
        const Map floor = live_map(cf, game.M(), game.t + horizon);
        const ThrowPaths throws = estimate_unseen_throws(obs, game, floor);
        const Action brake = act(A_MELEE, {me.ax, me.ay});
        const Landing landing = inspect_landing(game, seat, brake);
        if (!landing.safe || landing.crowded_edge) return {};

        auto evaluate = [&](Action first) {
            Game trial = game;
            int danger = 0;
            Point cut_position = position(trial.p[seat]);
            for (i64 tick = 0; tick < horizon && !trial.over; ++tick) {
                Point previous = position(trial.p[seat]);
                advance(trial, inputs, seat, tick == 0 ? first : Action{});
                const Point next = position(trial.p[seat]);
                danger += throws.risk(int(tick), previous, next);
                if (tick + 1 == cf.slash_pre) cut_position = next;
            }
            if (!trial.p[seat].alive) danger += 10000;
            return std::pair(danger, cut_position);
        };

        const int continuing = evaluate({}).first;
        if (continuing <= threshold) return {};
        const auto [stopped, center] = evaluate(brake);
        if (continuing - stopped < threshold) return {};
        Action cut = brake;
        int value = 0;
        if (cf.slash_pre <= ThrowPaths::horizon) {
            for (const ThreatSegment& shot : throws.segments[size_t(cf.slash_pre - 1)]) {
                const Point aim = shot.from - center;
                const Point to = shot.to - center;
                if (shot.weight <= value || distance(aim) > cf.slash_reach ||
                    wall_between(floor, center.x, center.y, shot.from.x, shot.from.y) ||
                    !seg_near_origin(aim.x, aim.y, to.x, to.y, cf.dart_r + cf.body_r)) continue;
                value = shot.weight;
                cut = act(A_MELEE, aim);
            }
        }
        return cut;
    }

    static bool enemy(const Game& game, int seat, int k) {
        return k != seat && game.p[k].alive && !game.p[k].unknown && !game.mates(seat, k);
    }

    static int closest_enemy(const Game& game, int seat) {
        int target = -1;
        double nearest = std::numeric_limits<double>::infinity();
        for (int k = 0; k < game.n; ++k) {
            if (!enemy(game, seat, k)) continue;
            const double d = distance(position(game.p[seat]), position(game.p[k]));
            if (d < nearest) {
                target = k;
                nearest = d;
            }
        }
        return target;
    }

    // In a crowded fight the closest rival can be shielded, while a nearby
    // rival already locked into a duel may offer a safe flank.
    static int choose_target(const Game& game, int seat) {
        if (game.n < 3) return closest_enemy(game,seat);
        const Map& map = game.live ? *game.live : game.M();
        const Point at = position(game.p[seat]);
        int selection = -1;
        double best = std::numeric_limits<double>::infinity();
        for (int k = 0; k < game.n; ++k) {
            if (!enemy(game,seat,k)) continue;
            const auto& foe=game.p[k];
            const Point location=position(foe);
            const double gap=distance(at,location);
            if (gap<game.cf.slash_reach+game.cf.run_v) return closest_enemy(game,seat);
            double priority=gap;
            if (wall_between(map,at.x,at.y,location.x,location.y)) priority+=1250;
            if (game.d[k].st != D_HAND) priority -= foe.st == S_RECALL ? 1200 : 730;
            if (foe.st == S_SLASHED || foe.st == S_KICKED || foe.st == S_STUN) priority -= 60*foe.timer;
            double engaged=gap;
            for (int j=0;j<game.n;++j) {
                if (j==k || !enemy(game,seat,j) || game.mates(j,k)) continue;
                const auto& third=game.p[j];
                const double separation=distance(location,position(third));
                if (separation<engaged &&
                    !wall_between(map,location.x,location.y,third.x,third.y)) engaged=separation;
            }
            if (gap-engaged>400) priority-=std::clamp((gap-engaged-400.)*.22,0.,680.);
            if (priority<best) {
                best=priority;
                selection=k;
            }
        }
        return selection;
    }

    static double crowd_exposure(const Game& g,const Map& floor,int seat) {
        if (g.n < 3) return 0.;
        const auto own=position(g.p[seat]);
        std::array<Point,MAXP> bearing{};
        std::array<double,MAXP> lanes{};
        int count=0;
        for (int k=0;k<g.n;++k) {
            if (!enemy(g,seat,k) || g.d[k].st!=D_HAND) continue;
            const auto& foe=g.p[k];
            const auto there=position(foe);
            const double gap=distance(own,there);
            if (gap>6500 || wall_between(floor,there.x,there.y,own.x,own.y)) continue;
            // Opponents fighting another visible rival are less likely to throw
            // straight across our route until their current exchange ends.
            double closest=gap;
            for (int j=0;j<g.n;++j) {
                if (j!=k && enemy(g,seat,j) && !g.mates(j,k))
                    closest=std::min(closest,distance(there,position(g.p[j])));
            }
            const double relative=std::clamp((gap-closest)/2400.,0.,.7);
            lanes[count]=std::clamp((6800-gap)/3600.,0.,1.)*(1.-relative);
            bearing[count]=direction(there-own);
            ++count;
        }
        double risk=0.;
        for (int i=0;i<count;++i) for (int j=i+1;j<count;++j) {
            const double alignment = double(bearing[i].x*bearing[j].x+bearing[i].y*bearing[j].y)/(UNIT*UNIT);
            risk+=std::min(lanes[i],lanes[j])*(.45+.55*(1.-alignment)*.5);
        }
        return risk * 2800.;
    }

    Strike best_strike(const Game& game, const Inputs& inputs, int seat) const {
        const Player& me = game.p[seat];
        const Config& cfg = game.cf;
        if (game.d[seat].st != D_HAND ||
            (me.st != S_FREE && !(cfg.dash_slash && game.dash_moving(me)))) return {};

        const Point origin = position(me);
        const i64 lead = cfg.slash_pre;
        bool near = false;
        for (int k = 0; k < game.n; ++k) {
            if (enemy(game, seat, k) && distance(origin, position(game.p[k])) <=
                cfg.slash_reach + lead * (cfg.dash_v + cfg.run_v)) near = true;
            if (k != seat && cfg.block && dart_flying(game.d[k].st) &&
                distance(origin, position(game.d[k])) <= cfg.slash_reach +
                    lead * (std::max({cfg.dart_v, cfg.ret_v, cfg.recall_v}) + cfg.dash_v)) near = true;
        }
        if (!near) return {};

        Game windup = game;
        for (i64 tick = 1; tick < lead && !windup.over; ++tick) {
            // This starts a real windup, so a cancel brakes the dash before aiming.
            advance(windup, inputs, seat, tick == 1 ? act(A_MELEE, {1000, 0}) : Action{});
        }
        if (windup.over || !windup.p[seat].alive) return {};
        // Ask the engine for the precise impact positions, including collisions and
        // stopping distance during a dash cancel. The probe aim doesn't affect movement.
        Game impact = windup;
        advance(impact, inputs, seat, lead == 1 ? act(A_MELEE, {1000, 0}) : Action{});
        const Point center = position(impact.p[seat]);

        Strike best;
        auto trial_value = [&](const Action& action) {
            Game trial = game;
            for (i64 tick = 0; tick < lead && !trial.over; ++tick) {
                advance(trial, inputs, seat, tick == 0 ? action : Action{});
            }
            const Player& result = trial.p[seat];
            if (!result.alive || result.k_team > me.k_team) return 0;
            return int((result.k_slash - me.k_slash) * kill_value +
                       (result.n_block - me.n_block) * 3);
        };
        auto try_aim = [&](Point point) {
            const Point aim = point - center;
            if (distance(aim) > cfg.slash_reach + 2 * cfg.body_r + cfg.run_v) return;
            const Action action = act(A_MELEE, aim);
            const int value = trial_value(action);
            if (value > best.value) best = {action, value};
        };

        for (int k = 0; k < game.n; ++k) {
            if (k == seat) continue;
            if (cfg.block && dart_flying(windup.d[k].st)) try_aim(position(windup.d[k]));
            if (enemy(windup, seat, k)) {
                const Player& p = windup.p[k];
                try_aim(position(impact.p[k]));
                try_aim(position(p) + Point{p.vx, p.vy});
                try_aim(position(p));
            }
        }

        return best;
    }

    static bool walkable(const Map& floor, Point p) {
        const int cell = floor.at(cell_index(p.x), cell_index(p.y));
        return cell == C_FLOOR || cell == C_GRASS;
    }

    static int edge_cost(const Map& floor, Point p) {
        int penalty = 0;
        for (size_t k = 0; k < directions.size(); k += 2) {
            Point around{p.x + directions[k].x, p.y + directions[k].y};
            if (floor.at(cell_index(around.x), cell_index(around.y)) == C_PIT) penalty += 105;
        }
        return penalty;
    }

    // An empty-handed opponent can kick us roughly three metres, even across a
    // gap. Prefer an angle that leaves firm ground behind us while closing in.
    static double kick_landing_cost(const Game& game, const Map& floor, int seat) {
        if (game.over) return 0;
        const Config& cfg = game.cf;
        const Player& me = game.p[seat];
        if (me.st == S_STUN) {
            // A simulated kick already has a known direction. Resolve the
            // remaining inertial slide up to the frame where the pit is judged.
            Point at = position(me);
            i64 vx = me.vx, vy = me.vy;
            for (i64 tick = 0; tick < me.timer; ++tick) {
                move_body(floor, cfg.body_r, at.x, at.y, vx, vy);
                vx = rdiv(vx * cfg.kick_keep, UNIT);
                vy = rdiv(vy * cfg.kick_keep, UNIT);
                if (!vx && !vy) break;
            }
            return cell_of_point(cfg, game.M(), at.x, at.y, game.t + me.timer - 1) == C_PIT ? 7000 : 0;
        }
        if (me.st == S_FREE && me.timer > 0) return 0;
        constexpr double strength = 3500;
        const Point origin = position(me);
        const i64 approach_room = 5 * cfg.run_v;
        double cost = 0;
        for (int k = 0; k < game.n; ++k) {
            if (!enemy(game, seat, k) || game.d[k].st == D_HAND) continue;
            const Player& other = game.p[k];
            if (other.st != S_FREE && other.timer > 3) continue;
            const Point threat = position(other);
            const double gap = distance(origin, threat);
            if (gap >= cfg.kick_reach + approach_room ||
                wall_between(floor, threat.x, threat.y, origin.x, origin.y)) continue;
            Point push = origin - threat;
            Point speed;
            if (!scale_to(push.x, push.y, cfg.kick_v, speed.x, speed.y)) continue;
            Point landing = origin;
            // Match the rules engine's stun movement: walls can stop the slide,
            // and the pit check is at the end of stun.
            for (i64 tick = 0; tick < cfg.kick_stun; ++tick) {
                move_body(floor, cfg.body_r, landing.x, landing.y, speed.x, speed.y);
                speed.x = rdiv(speed.x * cfg.kick_keep, UNIT);
                speed.y = rdiv(speed.y * cfg.kick_keep, UNIT);
                if (!speed.x && !speed.y) break;
            }
            const i64 land_tick = game.t + cfg.kick_pre + cfg.kick_stun;
            const auto pit = [&](Point p) {
                return cell_of_point(cfg, game.M(), p.x, p.y, land_tick) == C_PIT;
            };
            const double probability = std::clamp(
                (cfg.kick_reach + approach_room - gap) / std::max<i64>(1, approach_room), 0.0, 1.0);
            double danger = pit(landing) ? 1.0 : 0.0;
            if (danger == 0) {
                for (Point offset : std::array<Point, 4>{{{625, 0}, {0, 625}, {-625, 0}, {0, -625}}}) {
                    if (pit(landing + offset)) danger += 0.14;
                }
            }
            cost = std::max(cost, strength * danger * probability);
        }
        return cost;
    }

    static double exposed_kick_angle(const Game& game, const Map& floor, Point footing, Point foe) {
        const Config& c=game.cf;
        const Point push = footing - foe;
        const Point dir=direction(push);
        if (!dir.x && !dir.y) return 0.;
        const Point side{-dir.y,dir.x};
        double exposure=0.;
        for (int angle : {0,-1,1}) {
            const Point guessed=push + Point{rdiv(angle*650*side.x,UNIT),rdiv(angle*650*side.y,UNIT)};
            Point speed,landed=footing;
            if (!scale_to(guessed.x,guessed.y,c.kick_v,speed.x,speed.y)) continue;
            for (i64 j=0;j<c.kick_stun;++j) {
                move_body(floor,c.body_r,landed.x,landed.y,speed.x,speed.y);
                speed.x=rdiv(speed.x*c.kick_keep,UNIT);
                speed.y=rdiv(speed.y*c.kick_keep,UNIT);
            }
            if (cell_of_point(c,game.M(),landed.x,landed.y,game.t+c.kick_stun+c.kick_pre)==C_PIT)
                exposure+=angle==0?0.6:0.2;
        }
        return exposure;
    }

    static int projectile_risk(const Game& game, int seat) {
        const Point p = position(game.p[seat]);
        int risk = 0;
        for (int k = 0; k < game.n; ++k) {
            if (k == seat || !dart_flying(game.d[k].st)) continue;
            const Point offset = p - position(game.d[k]);
            if (std::abs(offset.x) >= 1500 || std::abs(offset.y) >= 1500) continue;
            const double d = distance(offset);
            if (d < 1500) risk += int((1500 - d) * 0.85);
        }
        return risk;
    }

    class Route {
    public:
        Point goal;

        Route(const Map& floor, Point target) : goal(target), map_(floor),
            path_(size_t(floor.nx) * floor.ny, -1) {
            int root = -1;
            double nearest = std::numeric_limits<double>::infinity();
            for (size_t index = 0; index < path_.size(); ++index) {
                const Point p = tile_center(int(index));
                if (!walkable(map_, p)) continue;
                const double d = distance(p, target);
                if (d < nearest) {
                    nearest = d;
                    root = int(index);
                }
            }
            if (root < 0) return;
            if (!walkable(map_, target)) goal = tile_center(root);
            path_[root] = 0;
            diagonal_routes(root);
        }

        double cost(Point p) const {
            const int x = cell_index(p.x), y = cell_index(p.y);
            const int steps = map_.inside(x, y) ? path_[y * map_.nx + x] : -1;
            constexpr double foot_cost = 0.84 * CELL / 10;
            double estimate = steps >= 0 ? foot_cost * steps : 26000;
            if (steps >= 0) {
                estimate += distance(p, tile_center(y * map_.nx + x));
                constexpr int dx[] = {1, 0, -1, 0}, dy[] = {0, 1, 0, -1};
                for (int k = 0; k < 4; ++k) {
                    const int a = x + dx[k], b = y + dy[k];
                    if (!map_.inside(a, b)) continue;
                    const int index = b * map_.nx + a;
                    if (path_[index] < 0) continue;
                    estimate = std::min(estimate, foot_cost * path_[index] +
                                                      distance(p, tile_center(index)));
                }
            }
            return estimate + 0.4 * distance(p, goal);
        }

    private:
        const Map& map_;
        std::vector<int> path_;

        void diagonal_routes(int root) {
            using Node = std::pair<int, int>;
            std::priority_queue<Node, std::vector<Node>, std::greater<Node>> todo;
            todo.push({0, root});
            while (!todo.empty()) {
                const auto [cost, node] = todo.top();
                todo.pop();
                if (cost != path_[node]) continue;
                const int x = node % map_.nx, y = node / map_.nx;
                auto open = [&](int a, int b) {
                    const int c = map_.at(a, b);
                    return c == C_FLOOR || c == C_GRASS;
                };
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if ((!dx && !dy) || !open(x + dx, y + dy)) continue;
                        if (dx && dy && (!open(x + dx, y) || !open(x, y + dy))) continue;
                        const int next = (y + dy) * map_.nx + x + dx;
                        const int candidate = cost + (dx && dy ? 14 : 10);
                        if (path_[next] >= 0 && path_[next] <= candidate) continue;
                        path_[next] = candidate;
                        todo.push({candidate, next});
                    }
                }
            }
        }

        Point tile_center(int index) const {
            return {(index % map_.nx) * CELL + CELL / 2,
                    (index / map_.nx) * CELL + CELL / 2};
        }
    };
};
} // namespace

DART_MAIN(TojiFinal)
