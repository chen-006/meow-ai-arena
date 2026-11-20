// player.cpp —— 飞镖乱斗参赛程序（v20）
//   * 反应延迟补偿：用 dart::Game 把别人的观察快进 DELAY 步到"现在"（别人按当前速度走、
//     手里的镖和正在做的动作由引擎继续推），自己用真实状态；
//   * 出手：够近（1.3 米内）就挥砍 / 踢，朝着对手；有镖且 4.2 米内，先在模拟里试三种提前量，
//     只有模拟显示能打中（对手保持当前速度）才扔；
//   * 移动：8 个方向 + 停 + 寻路方向，每个候选用引擎往后模拟 HORIZON 步，按结果打分。
//     模拟里对手的威胁：手里有镖的对手假设朝我现在的位置投一镖（7 米内）；1.6 米内的对手假设马上
//     挥砍 / 踢；1.6～4 米、冲刺冷却好的对手假设冲刺过来、冲刺结束后挥砍。打分看：杀人、活着、离危险格远、
//     镖在手、离对手的距离（有镖 1.8～3.8 米，没镖别太远）、躲开飞着的镖。
#include "dart.hpp"
#include <vector>
using namespace dart;

namespace {

const i64 DX[8] = {1000, 707, 0, -707, -1000, -707, 0, 707};
const i64 DY[8] = {0, 707, 1000, 707, 0, -707, -1000, -707};
constexpr int HORIZON = 10;      // 前瞻步数
constexpr int THROW_HIT_H = 14;
constexpr i64 MELEE_THREAT_R = 1600;  // 这个距离内的对手假设会马上近战
constexpr i64 DASH_THREAT_R = 4000;   // 这个距离内（但不到近战距离）的对手假设会冲过来
constexpr i64 HZ_MARGIN = 30;    // 这么多步以内会塌的格子也算危险
constexpr i64 CPU_BUDGET = 40000;  // 每步搜索的 CPU 预算（100 纳秒单位，40000 = 4 毫秒）；超了就用目前最好的候选

// 去程镖飞到 D 毫米要几段（按 5.3 的速度公式）
int flight_segments(const Config& cf, i64 D) {
    i64 cum = 0;
    int n = 0;
    while (cum < D && n < 100) {
        cum += imax(cf.dart_vmin, cf.dart_v - (i64)n * cf.dart_dec);
        ++n;
    }
    return n;
}

// 把别人从 DELAY 步前的观察快进到现在；自己冻结，最后换成真实状态
Game predict_now(const GameInfo& gi, const Obs& o) {
    const Config& cf = gi.cfg;
    const int seat = o.seat;
    const i64 back = imin(cf.delay, o.t);
    Game g(gi, o);
    g.t = o.t - back;
    g.p[seat].unknown = 1;
    for (i64 s = 0; s < back && !g.over; ++s) {
        Action acts[MAXP];
        for (int k = 0; k < g.n; ++k) {
            if (k == seat) continue;
            acts[k].mx = g.p[k].vx;
            acts[k].my = g.p[k].vy;
        }
        g.step(acts);
    }
    Player mine = o.p[seat];
    mine.team = gi.team[seat];
    mine.px = mine.x;
    mine.py = mine.y;
    mine.unknown = 0;
    g.p[seat] = mine;
    g.d[seat] = o.d[seat];
    g.t = o.t;
    g.over = false;
    return g;
}

// 每格的"安全距离"：到危险格（已经是坑、或 HZ_MARGIN 步内会塌）或场外边缘的格数，上限 8
std::vector<int> compute_safety(const GameInfo& gi, i64 t) {
    const Map& M = gi.M();
    const int nx = M.nx, ny = M.ny;
    std::vector<int> dist(size_t(nx) * ny, -1);
    std::vector<int> q;
    q.reserve(size_t(nx) * ny);
    for (int y = 0; y < ny; ++y)
        for (int x = 0; x < nx; ++x) {
            const bool hz = cell_at_step(gi.cfg, M, x, y, t) == C_PIT ||
                            cell_at_step(gi.cfg, M, x, y, t + HZ_MARGIN) == C_PIT;
            const bool border = (x == 0 || y == 0 || x == nx - 1 || y == ny - 1);
            const size_t id = size_t(y) * nx + x;
            if (hz) {
                dist[id] = 0;
                q.push_back(int(id));
            } else if (border) {
                dist[id] = 1;
                q.push_back(int(id));
            }
        }
    for (size_t h = 0; h < q.size(); ++h) {
        const int id = q[h];
        const int x = id % nx, y = id / nx;
        const int d0 = dist[id];
        const int dxs[4] = {1, -1, 0, 0}, dys[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
            const int x2 = x + dxs[k], y2 = y + dys[k];
            if (x2 < 0 || y2 < 0 || x2 >= nx || y2 >= ny) continue;
            const size_t id2 = size_t(y2) * nx + x2;
            if (dist[id2] == -1) {
                dist[id2] = d0 + 1;
                q.push_back(int(id2));
            }
        }
    }
    for (int& d : dist)
        if (d < 0) d = 99;
    return dist;
}

struct Ctx {
    const GameInfo* gi = nullptr;
    int seat = 0;
    int en_alive0 = 0;
    std::vector<int> safe;
    bool dart_ground = false;
    i64 dart_x = 0, dart_y = 0;
    int safe_at(i64 x, i64 y) const {
        const Map& M = gi->M();
        const int cx = cell_index(x), cy = cell_index(y);
        if (cx < 0 || cy < 0 || cx >= M.nx || cy >= M.ny) return 0;
        return imin(safe[size_t(cy) * M.nx + cx], 8);
    }
};

// 模拟：第一步用 first（含动作），之后保持移动方向 (dx, dy)、不再出手；别人保持当前速度
Game simulate(Game g, int seat, const Action& first, i64 dx, i64 dy, int H, unsigned mm = 0, unsigned dm = 0) {
    const i64 mx0 = g.p[seat].x, my0 = g.p[seat].y;
    for (int s = 0; s < H && !g.over; ++s) {
        Action acts[MAXP];
        for (int k = 0; k < g.n; ++k) {
            if (k == seat) {
                if (s == 0) {
                    acts[k] = first;
                } else {
                    acts[k].mx = dx;
                    acts[k].my = dy;
                }
            } else {
                acts[k].mx = g.p[k].vx;
                acts[k].my = g.p[k].vy;
                if (s == 0 && (mm & (1u << k))) {
                    // 威胁：这个对手马上挥砍 / 踢我
                    acts[k].act = A_MELEE;
                    acts[k].ax = mx0 - g.p[k].x;
                    acts[k].ay = my0 - g.p[k].y;
                }
                if (s == 0 && (dm & (1u << k))) {
                    // 威胁：这个对手冲刺过来（8 步后冲刺结束，在那一步起手挥砍）
                    acts[k].act = A_DASH;
                    acts[k].ax = mx0 - g.p[k].x;
                    acts[k].ay = my0 - g.p[k].y;
                }
                if (s == 8 && (dm & (1u << k))) {
                    acts[k].act = A_MELEE;
                    acts[k].ax = mx0 - g.p[k].x;
                    acts[k].ay = my0 - g.p[k].y;
                }
            }
        }
        g.step(acts);
    }
    return g;
}

// 投镖检查：试三种提前量（1 / 0.5 / 0 倍），模拟 THROW_HIT_H 步，能打死目标就返回 true。
bool try_throw(const GameInfo& gi, const Game& g, int seat, int tgt, i64& ax, i64& ay, i64 cpu0) {
    const Config& cf = gi.cfg;
    const Player& me = g.p[seat];
    const Player& T = g.p[tgt];
    i64 px = T.x, py = T.y;
    int Tf = 2;
    for (int it = 0; it < 3; ++it) {
        Tf = flight_segments(cf, ilen(px - me.x, py - me.y)) + 2;
        px = T.x + T.vx * Tf;
        py = T.y + T.vy * Tf;
    }
    const double facs[3] = {1.0, 0.5, 0.0};
    for (double f : facs) {
        if (cpu0 >= 0 && thread_cpu_100ns() - cpu0 > CPU_BUDGET) return false;
        Action a;
        a.act = A_THROW;
        a.ax = T.x + i64(double(T.vx) * Tf * f) - me.x;
        a.ay = T.y + i64(double(T.vy) * Tf * f) - me.y;
        if (a.ax == 0 && a.ay == 0) continue;
        Game s = simulate(g, seat, a, 0, 0, THROW_HIT_H);
        if (!s.p[tgt].alive) {
            ax = a.ax;
            ay = a.ay;
            return true;
        }
    }
    return false;
}

double evaluate(const Game& g, const Ctx& c) {
    const GameInfo& gi = *c.gi;
    const int seat = c.seat;
    const Player& P = g.p[seat];
    if (!P.alive) return -3000.0 + 10.0 * double(P.dead_t);  // 死得越晚越好
    double v = 0;
    int en_alive = 0;
    i64 nearest = -1;
    for (int k = 0; k < g.n; ++k) {
        if (k == seat || is_mate(gi, k) || !g.p[k].alive) continue;
        ++en_alive;
        const i64 dd = ilen(g.p[k].x - P.x, g.p[k].y - P.y);
        if (nearest < 0 || dd < nearest) nearest = dd;
    }
    v += 2000.0 * (c.en_alive0 - en_alive);
    if (en_alive == 0) v += 5000.0;
    // 安全
    const int sd = c.safe_at(P.x, P.y);
    if (sd == 0) v -= 300.0;
    else v += 40.0 * imin(sd, 6);
    // 镖
    const Dart& D = g.d[seat];
    if (D.st == D_HAND) {
        v += 120.0;
    } else if (c.dart_ground) {
        v -= 0.15 * double(ilen(P.x - c.dart_x, P.y - c.dart_y));
    }
    // 离对手的距离：有镖时保持投镖距离（3.5 米附近），没镖时别太远
    if (nearest >= 0) {
        if (D.st == D_HAND) {
            if (nearest < 1800) v -= 0.08 * double(1800 - nearest);
            else if (nearest > 3800) v -= 0.08 * double(nearest - 3800);
        } else if (nearest > 4000) {
            v -= 0.05 * double(nearest - 4000);
        }
    }
    // 飞着的镖（包括队友的）离我近，扣分（超出前瞻范围的威胁）
    for (int k = 0; k < g.n; ++k) {
        if (k == seat) continue;
        const Dart& E = g.d[k];
        if (!dart_flying(E.st)) continue;
        const i64 dd = ilen(E.x - P.x, E.y - P.y);
        if (dd < 1500) v -= 0.4 * double(1500 - dd);
    }
    return v;
}

struct Bot {
    void init(const GameInfo& gi) { (void)gi; }

    Action decide(const Obs& o) {
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        const int seat = o.seat;
        const Player& me = o.me();
        Action out;  // 默认：不动、不出手
        if (!me.alive) return out;
        const i64 cpu0 = thread_cpu_100ns();

        Game g = predict_now(gi, o);
        const bool can_move = (me.st == S_FREE || me.st == S_THROW || me.st == S_THROWN);
        const bool can_act = (me.st == S_FREE);
        const Dart& md = o.my_dart();
        const bool has_dart = (md.st == D_HAND);

        // 最近的、看得见的对手（按现在的推算位置）
        int tgt = -1;
        i64 tdist = 0;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || is_mate(gi, k) || !g.p[k].alive || g.p[k].unknown) continue;
            const i64 dd = ilen(g.p[k].x - me.x, g.p[k].y - me.y);
            if (tgt < 0 || dd < tdist) {
                tgt = k;
                tdist = dd;
            }
        }

        Ctx c;
        c.gi = &gi;
        c.seat = seat;
        for (int k = 0; k < g.n; ++k)
            if (k != seat && !is_mate(gi, k) && g.p[k].alive) ++c.en_alive0;
        c.safe = compute_safety(gi, o.t);
        c.dart_ground = (md.st == D_GROUND);
        c.dart_x = md.x;
        c.dart_y = md.y;

        // ---- 出手（规则）----
        Action act;
        if (can_act && tgt >= 0) {
            const Player& T = g.p[tgt];
            if (tdist <= 1300 && !wall_between(g.LM(), me.x, me.y, T.x, T.y)) {
                // 近战：有镖挥砍，空手踢（朝着对手）
                i64 ex = T.x + T.vx * 2, ey = T.y + T.vy * 2;
                act.act = A_MELEE;
                act.ax = ex - me.x;
                act.ay = ey - me.y;
            } else if (has_dart && tdist <= 4200 && !wall_between(g.LM(), me.x, me.y, T.x, T.y)) {
                // 投镖：带提前量（镖飞行时间内对手按当前速度走）
                i64 px = T.x, py = T.y;
                for (int it = 0; it < 3; ++it) {
                    const int Tf = flight_segments(cf, ilen(px - me.x, py - me.y)) + 2;
                    px = T.x + T.vx * Tf;
                    py = T.y + T.vy * Tf;
                }
                i64 ax = 0, ay = 0;
                if (try_throw(gi, g, seat, tgt, ax, ay, cpu0)) {
                    act.act = A_THROW;
                    act.ax = ax;
                    act.ay = ay;
                }
            }
        }

        // ---- 移动候选 ----
        struct Cand { i64 x, y; };
        std::vector<Cand> cands;
        if (can_move) {
            cands.push_back({0, 0});
            for (int k = 0; k < 8; ++k) cands.push_back({DX[k], DY[k]});
            // 寻路方向：去镖旁边（没镖且镖在地上），否则去对手附近，都没有就去中心
            i64 gx = gi.M().width() / 2, gy = gi.M().height() / 2;
            if (!has_dart && c.dart_ground) {
                gx = md.x;
                gy = md.y;
            } else if (tgt >= 0) {
                gx = g.p[tgt].x;
                gy = g.p[tgt].y;
            }
            i64 wx, wy;
            if (path_next(gi, me.x, me.y, gx, gy, o.t, wx, wy)) {
                i64 vx = wx - me.x, vy = wy - me.y;
                if (vx != 0 || vy != 0) {
                    i64 ox, oy;
                    if (scale_to(vx, vy, 1000, ox, oy)) cands.push_back({ox, oy});
                }
            }
        } else {
            cands.push_back({0, 0});
        }

        // 威胁模型：每个看得见、手里有镖、站着的对手，假设他朝我现在的位置投一镖（在模拟里放一支飞镖）
        Game gh = g;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || is_mate(gi, k) || !g.p[k].alive || g.p[k].unknown) continue;
            if (g.p[k].st != S_FREE || g.d[k].st != D_HAND) continue;
            const i64 ex = me.x - g.p[k].x, ey = me.y - g.p[k].y;
            if (ilen(ex, ey) > 7000) continue;
            i64 ux, uy;
            if (!scale_to(ex, ey, 1000, ux, uy)) continue;
            Dart& D = gh.d[k];
            D.st = D_OUT;
            D.x = g.p[k].x;
            D.y = g.p[k].y;
            D.vx = ux;
            D.vy = uy;
            D.left = cf.range;
            D.age = 0;
            D.hits = 0;
            D.bent = 0;
        }

        // 威胁：距离 1.6 米内、站着的对手可能马上近战（起手在模拟第 0 步开始）
        unsigned mm = 0, dm = 0;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || is_mate(gi, k) || !g.p[k].alive || g.p[k].unknown) continue;
            if (g.p[k].st != S_FREE) continue;
            const i64 dd = ilen(me.x - g.p[k].x, me.y - g.p[k].y);
            if (dd < MELEE_THREAT_R) mm |= 1u << k;
            else if (dd < DASH_THREAT_R && g.p[k].cd == 0) dm |= 1u << k;
        }

        // ---- 对每个候选往后模拟，挑分最高的 ----
        double best = -1e18;
        i64 bx = 0, by = 0;
        int evaluated = 0;
        for (const Cand& cd : cands) {
            if (evaluated > 0 && cpu0 >= 0 && thread_cpu_100ns() - cpu0 > CPU_BUDGET) break;
            ++evaluated;
            Action first = act;
            first.mx = cd.x;
            first.my = cd.y;
            first.msg = 0;
            Game s = simulate(gh, seat, first, cd.x, cd.y, HORIZON, mm, dm);
            const double val = evaluate(s, c);
            if (val > best) {
                best = val;
                bx = cd.x;
                by = cd.y;
            }
        }

        out.mx = bx;
        out.my = by;
        out.act = act.act;
        out.ax = act.ax;
        out.ay = act.ay;
        out.msg = 0;
        return out;
    }
};

}  // namespace

DART_MAIN(Bot)
