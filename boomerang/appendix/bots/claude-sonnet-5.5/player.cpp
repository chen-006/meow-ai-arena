// player.cpp —— 飞镖乱斗：基于规则引擎推演的搜索型程序（交卷版 v26；详细说明见 notes.md）
// 思路：
//  1. 用自己的历史动作 + 观察里对手 DELAY 步以前的状态，重建"4 步以前的完整局面"，再用引擎推到现在（对手的这 4 步按假设推）；
//     看不见的对手（草丛里）用"最后一次看到的位置往我这边走"估出一个幽灵放回推演。
//  2. 对手的行为用 16 个假设加权（保持速度 / 停 / 逼近 / 横移 / 后退 × 躲镖 × 挡镖 × 在哪一步出手 × 近战 / 冲刺斩），
//     "会躲 / 会挡"的先验在线更新（我的镖被挡过就提高）。每个假设下往后推 H 步（1v1 H=16，四人局 H=11）。
//  3. 候选动作（16 方向移动、扔镖多种提前量、挥砍 / 踢（限近距）、挡镖、冲刺斩、朝落地的镖冲刺、召回）逐个在所有假设下推演，
//     用终局 + 局面启发式（镖就绪度、距离、塌陷）打分，死亡结果乘 1.5（风险厌恶），取最高。
//  4. 搜索量用"推演步数"限预算，不用时钟（本机线程 CPU 计时粒度 15.6 毫秒，不可靠）。
#include "dart.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
using namespace dart;

namespace {

constexpr int DBG = 0;
constexpr bool DASHSLASH = true;    // 冲刺斩候选（关掉后 1v1 成绩差不多，保留）
constexpr int NDIR = 16;
i64 DIRX[NDIR], DIRY[NDIR];

inline double dlen(double x, double y) { return std::sqrt(x * x + y * y); }

enum MvKind { MV_HOLD, MV_STOP, MV_APPROACH, MV_RETREAT, MV_STRAFE_L, MV_STRAFE_R };

struct Hyp {
    int mv;
    int thr_mode;   // 0 不出手；1 在 thr_rel 指定的步出手；2 逼近后进入 4.5 米就出手
    int thr_rel;    // -2: 重建窗口最早一步(tau)；-1: 上一步(t-1)；>=0: t + thr_rel
    bool dodge;
    bool melee;     // 贴近了就挥砍 / 踢
    bool dashm;     // 中距离冲刺斩
    double w;
    bool blk = false;   // 持镖时对着飞来的镖挥砍挡下
};

// 1v1 / 少人时的假设表
const Hyp HYPS_FULL[] = {
    {MV_HOLD, 0, 0, false, true, false, 0.05},     // 0
    {MV_HOLD, 0, 0, true, true, false, 0.10, true},      // 1
    {MV_STOP, 0, 0, true, true, false, 0.03},      // 2
    {MV_APPROACH, 0, 0, true, true, true, 0.06, true},   // 3
    {MV_STRAFE_L, 0, 0, true, true, false, 0.03},  // 4
    {MV_STRAFE_R, 0, 0, true, true, false, 0.03},  // 5
    {MV_RETREAT, 0, 0, true, false, false, 0.02},  // 6
    {MV_HOLD, 1, -3, true, true, false, 0.08},     // 7  T-3 出手
    {MV_HOLD, 1, -2, true, true, false, 0.09, true},     // 8  T-2
    {MV_HOLD, 1, -1, true, true, false, 0.09},     // 9  T-1
    {MV_HOLD, 1, 0, true, true, false, 0.09, true},      // 10 T
    {MV_HOLD, 1, 1, true, true, false, 0.07},      // 11 T+1
    {MV_HOLD, 1, 2, true, true, false, 0.04},      // 12 T+2
    {MV_APPROACH, 2, 0, true, true, true, 0.08, true},   // 13 逼近到 4.5 米出手
    {MV_HOLD, 3, 0, true, true, false, 0.07, true},      // 14 6.8 米内立刻出手
    {MV_APPROACH, 3, 0, true, true, true, 0.07},   // 15
};
const int NH_FULL = sizeof(HYPS_FULL) / sizeof(HYPS_FULL[0]);

struct Rec {
    Player me;
    Dart md;
    Action act;
    bool have = false;
};

struct Plan {
    i64 mx = 0, my = 0;
    i64 act = 0, ax = 0, ay = 0;
    int melee_j = -1;   // 冲刺斩：在推演第几步给近战
    int tgt = -1;
    const char* name = "";
};

struct Bot {
    std::vector<Rec> rec;
    const GameInfo* gi = nullptr;
    int me = 0, n = 0;
    i64 T = 0;           // 当前步
    i64 tau = 0;         // 观察里对手的步
    i64 thr_cmd = -1000; // 我最近一次扔镖指令的步
    // 本步的基础局面（每个假设一份，已推到当前步）
    Game base[16];
    int NH = 0;
    const Hyp* hyps = nullptr;
    i64 cpu_start = 0;
    i64 cpu_limit = 35000;  // 100ns 单位
    // 最后落脚区的距离（步数）
    std::vector<int> refuge;   // 每格：到永不塌 / 最晚塌的格子的步数；-1 走不到
    std::vector<i64> rng_state;
    u64 rs = 88172645463325252ULL;
    struct Mem { Player p; i64 t_seen = -1; bool have = false; };
    Mem mem[MAXP];
    bool ghost[MAXP] = {};
    double alert_p = 0.5;        // 对手会躲镖的概率估计
    double blk_p = 0.3;          // 对手会挡镖的概率估计（被挡过 → 升高；镖平安飞回 → 降低）
    Hyp HY[16];
    double throw_max = 5200;
    double band_lo = 6600, band_hi = 7600;

    u64 rnd() {
        rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
        return rs;
    }
    double rndf() { return double(rnd() % 1000000) / 1000000.0; }

    void init(const GameInfo& g) {
        gi = &g;
        for (int i = 0; i < NDIR; ++i) {
            const double a = 2 * 3.14159265358979323846 * i / NDIR;
            DIRX[i] = std::llround(1000 * std::cos(a));
            DIRY[i] = std::llround(1000 * std::sin(a));
        }
        compute_refuge();
    }

    void compute_refuge() {
        const Map& M = gi->M();
        const int nx = M.nx, ny = M.ny;
        refuge.assign(size_t(nx) * ny, -1);
        int maxw = -1;
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                const int c = M.at(x, y);
                if (c == C_WALL || c == C_PIT) continue;
                maxw = std::max(maxw, M.wave(x, y));
            }
        // 多源 BFS（4 邻域），源 = 永不塌的格子；没有就用最晚塌的
        std::vector<int> q;
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                const int c = M.at(x, y);
                if (c == C_WALL || c == C_PIT) continue;
                const int w = M.wave(x, y);
                if (w == -1 || (maxw >= 0 && w == maxw && !hasNever())) {
                    refuge[size_t(y) * nx + x] = 0;
                    q.push_back(y * nx + x);
                }
            }
        for (size_t h = 0; h < q.size(); ++h) {
            const int id = q[h], x = id % nx, y = id / nx;
            for (int d = 0; d < 4; ++d) {
                const int a = x + (d == 0) - (d == 1), b = y + (d == 2) - (d == 3);
                if (!M.inside(a, b)) continue;
                const int c = M.at(a, b);
                if (c == C_WALL || c == C_PIT) continue;
                if (refuge[size_t(b) * nx + a] >= 0) continue;
                refuge[size_t(b) * nx + a] = refuge[size_t(id)] + 1;
                q.push_back(b * nx + a);
            }
        }
    }
    bool hasNever() const {
        const Map& M = gi->M();
        for (int y = 0; y < M.ny; ++y)
            for (int x = 0; x < M.nx; ++x) {
                const int c = M.at(x, y);
                if (c == C_WALL || c == C_PIT) continue;
                if (M.wave(x, y) == -1) return true;
            }
        return false;
    }

    // ------------------------------------------------------------ 对手策略（推演用）
    Action opp_action(const Game& g, int k, const Hyp& h, i64 thr_cmd_t) const {
        Action a;
        const Player& P = g.p[k];
        if (!P.alive || P.unknown) return a;
        const Player& Mp = g.p[me];
        double tx = double(Mp.x - P.x), ty = double(Mp.y - P.y);
        const double dist = dlen(tx, ty);
        double mx = 0, my = 0;
        switch (h.mv) {
            case MV_HOLD: {
                const double sp = dlen(double(P.vx), double(P.vy));
                if (sp >= 100) { mx = P.vx; my = P.vy; }
                break;
            }
            case MV_STOP: break;
            case MV_APPROACH: if (dist > 1100) { mx = tx; my = ty; } break;
            case MV_RETREAT: mx = -tx; my = -ty; break;
            case MV_STRAFE_L: mx = ty; my = -tx; break;
            case MV_STRAFE_R: mx = -ty; my = tx; break;
        }
        // 躲镖：我的镖在去程、或我在蓄势，且已经过了他能看到的时间
        if (h.dodge && g.t >= thr_cmd_t + 5) {
            double ux = 0, uy = 0, ox = 0, oy = 0;
            bool have = false;
            const Dart& D = g.d[me];
            if (D.st == D_OUT) {
                ux = double(D.vx); uy = double(D.vy); ox = double(D.x); oy = double(D.y); have = true;
            } else if (Mp.st == S_THROW) {
                ux = double(Mp.ax); uy = double(Mp.ay); ox = double(Mp.x); oy = double(Mp.y); have = true;
            }
            if (have) {
                const double ul = dlen(ux, uy);
                if (ul > 1) {
                    ux /= ul; uy /= ul;
                    const double rx = P.x - ox, ry = P.y - oy;
                    const double along = rx * ux + ry * uy;
                    if (along > -600) {  // 镖还没过去
                        const double cross = ux * ry - uy * rx;
                        double s = cross >= 0 ? 1 : -1;
                        if (std::fabs(cross) < 30) {
                            const double cv = ux * P.vy - uy * P.vx;
                            s = cv >= 0 ? 1 : -1;
                        }
                        mx = -uy * s; my = ux * s;
                    }
                }
            }
        }
        // 对手不会自己走进坑里：检查前方 0.5 / 1 / 1.5 米处（含塌陷）不安全就换方向，都不行就停
        if (mx != 0 || my != 0) {
            const double ml = dlen(mx, my);
            const double ux0 = mx / ml, uy0 = my / ml;
            bool found = false;
            const double rots[7] = {0, 0.785, -0.785, 1.571, -1.571, 2.356, -2.356};
            for (int ri = 0; ri < 7 && !found; ++ri) {
                const double c = std::cos(rots[ri]), s = std::sin(rots[ri]);
                const double ux = ux0 * c - uy0 * s, uy = ux0 * s + uy0 * c;
                bool ok = true;
                for (int L = 2; L <= 6; L += 2) {
                    const i64 qx = P.x + std::llround(ux * 250.0 * L), qy = P.y + std::llround(uy * 250.0 * L);
                    const int ct = cell_of_point(g.cf, g.M(), qx, qy, g.t + L);
                    if (ct == C_PIT) { ok = false; break; }
                }
                if (ok) { mx = ux * 1000.0; my = uy * 1000.0; found = true; }
            }
            if (!found) { mx = 0; my = 0; }
        }
        a.mx = std::llround(mx);
        a.my = std::llround(my);
        if (a.mx == 0 && a.my == 0) { a.mx = a.my = 0; }
        else {
            const double l = dlen(double(a.mx), double(a.my));
            if (l > 0 && l < 1000) { a.mx = std::llround(a.mx * 1000.0 / l); a.my = std::llround(a.my * 1000.0 / l); }
        }
        // 近战 / 冲刺斩
        if (h.melee || h.dashm) {
            const bool armed_k = g.d[k].st == D_HAND;
            const double lead = 2.0;
            const double px = Mp.x + Mp.vx * lead - P.x, py = Mp.y + Mp.vy * lead - P.y;
            if (P.st == S_FREE) {
                if (h.melee && dist <= 1450) {
                    a.act = A_MELEE; a.ax = std::llround(px); a.ay = std::llround(py);
                } else if (h.dashm && armed_k && P.cd == 0 && dist >= 1900 && dist <= 4200 && g.t >= T) {
                    a.act = A_DASH; a.ax = std::llround(tx + Mp.vx * 3); a.ay = std::llround(ty + Mp.vy * 3);
                }
            } else if (P.st == S_DASH && h.dashm && g.dash_moving(P)) {
                if (dist <= 2300 && P.timer <= g.cf.dash_t + g.cf.dash_post - 1) {
                    a.act = A_MELEE; a.ax = std::llround(px); a.ay = std::llround(py);
                }
            }
        }
        // 挡镖：他持镖、自由，我的镖飞来，下一步镖会到他前方 0.78~1.45 米处
        if (h.blk && P.st == S_FREE && g.d[k].st == D_HAND && g.t >= thr_cmd_t + 5) {
            const Dart& D = g.d[me];
            if (dart_flying(D.st)) {
                const double stp = double(dart_step_len(g.cf, D));
                double nx = double(D.x), ny = double(D.y);
                if (D.st == D_OUT) {
                    nx += double(D.vx) * stp / 1000.0;
                    ny += double(D.vy) * stp / 1000.0;
                } else {
                    const double ox = double(Mp.x - D.x), oy = double(Mp.y - D.y);
                    const double ol = dlen(ox, oy);
                    if (ol > 1) { nx += ox / ol * std::min(stp, ol); ny += oy / ol * std::min(stp, ol); }
                }
                const double bx = nx - double(P.x), by = ny - double(P.y);
                const double bd = dlen(bx, by);
                if (bd >= 780 && bd <= 1450) {
                    a.act = A_MELEE;
                    a.ax = std::llround(bx);
                    a.ay = std::llround(by);
                    a.mx = 0; a.my = 0;
                    return a;
                }
            }
        }
        // 出手
        if (h.thr_mode != 0 && P.st == S_FREE && g.d[k].st == D_HAND) {
            bool go = false;
            if (h.thr_mode == 1) {
                go = g.t == T + h.thr_rel;
            } else if (h.thr_mode == 2) {
                go = dist <= 4500 && g.t >= T;
            } else if (h.thr_mode == 3) {
                go = dist <= 6800 && g.t >= T;
            }
            if (go) {
                // 提前量：他只看得到 4 步以前的我：朝（4 步前的位置 + 4 步前的速度 × (4 + 飞行步数)）
                const double sp = std::min(dist, 7000.0);
                const double steps = 4 + 2 + sp / 900.0;
                double bx = double(Mp.x), by = double(Mp.y), bvx = double(Mp.vx), bvy = double(Mp.vy);
                const i64 ts = g.t - g.cf.delay;
                if (ts < T && ts >= 0 && size_t(ts) < rec.size() && rec[size_t(ts)].have) {
                    const Player& Q0 = rec[size_t(ts)].me;
                    bx = double(Q0.x); by = double(Q0.y); bvx = double(Q0.vx); bvy = double(Q0.vy);
                }
                double px = bx + bvx * steps * 0.85 - P.x, py = by + bvy * steps * 0.85 - P.y;
                a.act = A_THROW;
                a.ax = std::llround(px);
                a.ay = std::llround(py);
                if (a.ax == 0 && a.ay == 0) { a.ax = std::llround(tx); a.ay = std::llround(ty); }
            }
        }
        return a;
    }

    // ------------------------------------------------------------ 终局 / 局面评分
    double collapse_pen(const Player& M, i64 t_leaf) const {
        const int cx = cell_index(M.x), cy = cell_index(M.y);
        const Map& Mp = gi->M();
        if (!Mp.inside(cx, cy)) return 0;
        const i64 fs = cell_fall_step(*gi, cx, cy);
        if (fs < 0) return 0;
        const i64 ttl = fs - t_leaf;
        if (ttl > 140) return 0;
        const int r = refuge[size_t(cy) * Mp.nx + cx];
        const double need = (r < 0 ? 60 : r * 2.2) + 8;
        if (ttl >= need + 25) return 0;
        const double deficit = need + 25 - double(ttl);
        return std::min(400.0, deficit * 4.0);
    }

    // 多久能再用上镖（步数）→ 0..1 的"就绪度"
    double ready(const Game& g, int k) const {
        const Player& P = g.p[k];
        const Dart& D = g.d[k];
        double tau_r = 0;
        const double dd = dlen(double(P.x - D.x), double(P.y - D.y));
        switch (D.st) {
            case D_HAND:
                tau_r = 0;
                if (P.st != S_FREE && P.st != S_DEAD) tau_r = std::min<double>(double(P.timer), 8.0) * 0.6;
                break;
            case D_OUT: tau_r = 14 + double(D.left) / 600.0; break;
            case D_BACK: tau_r = (D.age < g.cf.dart_hang ? double(g.cf.dart_hang - D.age) : 0.0) + dd / 450.0 + 1; break;
            case D_CALL: tau_r = dd / 450.0 + 1; break;
            case D_GROUND: tau_r = dd / 250.0 + 2; break;
            case D_RISE: tau_r = double(D.left) + 12; break;
            default: tau_r = 80; break;
        }
        return std::max(0.0, 1.0 - tau_r / 34.0);
    }

    double leaf(const Game& g) const {
        const Player& M = g.p[me];
        const double rm = ready(g, me);
        double s = 65.0 * rm;
        for (int k = 0; k < g.n; ++k) {
            if (k == me || !g.p[k].alive || is_mate(*gi, k)) continue;
            const Player& P = g.p[k];
            const double d = dlen(double(M.x - P.x), double(M.y - P.y));
            const double ro = ready(g, k);
            s -= 50.0 * ro;
            // 近身缠斗时离坑边太近容易被踢进坑：数我周围 1.5 米内（含已塌）的坑格
            if (d < 3500) {
                const Map& MM = gi->M();
                const int cx0 = cell_index(M.x), cy0 = cell_index(M.y);
                int pits = 0;
                for (int dy = -3; dy <= 3; ++dy)
                    for (int dx = -3; dx <= 3; ++dx)
                        if (cell_at_step(g.cf, MM, cx0 + dx, cy0 + dy, g.t) == C_PIT) ++pits;
                s -= 40.0 * double(pits) / 49.0;
            }
            const double adv = rm - ro;
            if (adv > 0.35) {
                const double target = 3400;
                s -= 9.0 * std::fabs(d - target) / 1000.0;
                if (d <= 4500) s += 15;
            } else if (adv < -0.35) {
                if (d < 7500) s -= 7.0 * (7500 - d) / 1000.0;
            } else {
                if (d < 4800) s -= 10.0 * (4800 - d) / 1000.0;
                if (d < band_lo) s -= 8.0 * (band_lo - d) / 1000.0;
                else if (d > band_hi) s -= 2.0 * (d - band_hi) / 1000.0;
            }
        }
        s -= collapse_pen(M, g.t);
        return s;
    }

    // ------------------------------------------------------------ 推演
    double rollout(const Game& start, const Hyp& h, const Plan& pl, int H) const {
        Game g = start;
        i64 tc = thr_cmd;
        double bonus = 0;
        bool credited = false;
        bool alive_before[MAXP];
        for (int k = 0; k < g.n; ++k) alive_before[k] = g.p[k].alive;
        for (int j = 0; j < H; ++j) {
            Action acts[MAXP];
            Action am;
            am.mx = pl.mx; am.my = pl.my;
            if (j == 0) {
                am.act = pl.act; am.ax = pl.ax; am.ay = pl.ay;
                if (pl.act == A_THROW && g.p[me].st == S_FREE && g.d[me].st == D_HAND) tc = g.t;
            } else if (j == pl.melee_j) {
                if (pl.tgt >= 0 && g.p[pl.tgt].alive) {
                    const Player& Q = g.p[pl.tgt];
                    am.act = A_MELEE;
                    am.ax = Q.x + Q.vx * 2 - g.p[me].x;
                    am.ay = Q.y + Q.vy * 2 - g.p[me].y;
                } else {
                    am.act = A_MELEE;
                    am.ax = g.p[me].ax;
                    am.ay = g.p[me].ay;
                    if (am.ax == 0 && am.ay == 0) am.ax = 1000;
                }
            }
            acts[me] = am;
            for (int k = 0; k < g.n; ++k)
                if (k != me) acts[k] = is_mate(*gi, k) ? Action() : opp_action(g, k, h, tc);
            g.step(acts);
            ++steps_used;
            if (!g.p[me].alive) return -1000.0 + 15.0 * j + bonus;
            int alive_opp = 0;
            for (int k = 0; k < g.n; ++k) {
                if (k != me && !is_mate(*gi, k) && g.p[k].alive) ++alive_opp;
                if (k != me && alive_before[k] && !g.p[k].alive) {
                    alive_before[k] = false;
                    const i64 kr = g.p[k].killer;
                    if (kr == me) { bonus += 350.0 - 3.0 * j; credited = true; }
                    else if (kr >= 0) bonus += 100.0 - 3.0 * j;
                }
            }
            if (alive_opp == 0) return bonus + (credited ? 500.0 : 120.0) - 5.0 * j;
            if (g.over) break;
        }
        return bonus + leaf(g);
    }

    static double risk_u(double v) { return v < -400.0 ? v * 1.5 : v; }
    double eval_plan(const Plan& pl, int H) const {
        double v = 0;
        for (int i = 0; i < NH; ++i) v += hyps[i].w * risk_u(rollout(base[i], hyps[i], pl, H));
        return v;
    }

    mutable int trunc_cnt = 0;
    mutable long steps_used = 0;
    long step_budget = 4000;
    std::chrono::steady_clock::time_point wall0;
    // 墙上时钟只当"应急刹车"（机器特别慢时防止超 10 毫秒）；正常情况下由推演步数预算决定搜索量
    double wall_ms() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - wall0).count();
    }
    bool out_of_time() const {
        const bool r = steps_used > step_budget || wall_ms() > 7.0;
        if (r) ++trunc_cnt;
        return r;
    }

    // ------------------------------------------------------------ 对手建模
    void update_beliefs(const Obs& o) {
        if (o.t < 1 || rec.size() <= size_t(o.t)) return;
        const Rec& a = rec[size_t(o.t - 1)];
        const Rec& b = rec[size_t(o.t)];
        const bool was_flying = a.md.st == D_OUT || a.md.st == D_BACK || a.md.st == D_CALL;
        if (was_flying && b.md.st == D_GROUND) {
            double mind = 1e18;
            bool other_dropped = false;
            for (int k = 0; k < o.n; ++k) {
                if (k == me || is_mate(*gi, k) || !o.p[k].alive || !o.p[k].vis) continue;
                mind = std::min(mind, dlen(double(b.md.x - o.p[k].x), double(b.md.y - o.p[k].y)));
                if (o.d[k].st == D_GROUND) other_dropped = true;
            }
            const bool near_wall = wall_hit_nearby(b.md.x, b.md.y);
            if (mind <= 2800 && !other_dropped && !near_wall) {
                blk_p = std::max(blk_p, 0.92);
                alert_p = std::max(alert_p, 0.95);
                if (DBG) std::fprintf(stderr, "[t=%lld] blocked -> blk_p=%.2f\n", (long long)o.t, blk_p);
            }
        }
        if ((a.md.st == D_BACK || a.md.st == D_CALL) && b.md.st == D_HAND && blk_p < 0.9) {
            blk_p = std::max(0.08, blk_p * 0.75);
        }
    }
    bool wall_hit_nearby(i64 x, i64 y) const {
        const Map& M = gi->M();
        const int cx = cell_index(x), cy = cell_index(y);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (M.at(cx + dx, cy + dy) == C_WALL) return true;
        return false;
    }
    void setup_hyps() {
        for (int i = 0; i < NH_FULL; ++i) { HY[i] = HYPS_FULL[i]; HY[i].blk = false; }
        static const int order[] = {1, 10, 14, 13, 3, 8, 9, 11, 15, 12, 7, 4, 5, 2};
        double tot = 0;
        for (int i = 0; i < NH_FULL; ++i) if (HYPS_FULL[i].dodge) tot += HYPS_FULL[i].w;
        // 先决定哪些假设里的对手会躲镖（其余是迟钝的），再在会躲的里面决定哪些还会挡镖
        for (int i = 0; i < NH_FULL; ++i) HY[i].dodge = false;
        double acc = 0;
        for (int idx : order) {
            if (idx >= NH_FULL || !HYPS_FULL[idx].dodge) continue;
            if (acc >= alert_p * tot) break;
            HY[idx].dodge = true;
            acc += HY[idx].w;
        }
        acc = 0;
        for (int idx : order) {
            if (idx >= NH_FULL || !HY[idx].dodge) continue;
            if (acc >= blk_p * tot) break;
            HY[idx].blk = true;
            acc += HY[idx].w;
        }
        if (blk_p >= 0.6) { throw_max = 5200; band_lo = 6600; band_hi = 7600; }
        else { throw_max = 6600; band_lo = 5200; band_hi = 6000; }
    }

    // ------------------------------------------------------------ 重建
    void build_bases(const Obs& o) {
        T = o.t;
        tau = std::max<i64>(0, T - gi->cfg.delay);
        // 最近一次扔镖指令
        thr_cmd = -1000;
        for (i64 s = std::max<i64>(0, T - 40); s < T; ++s) {
            const Rec& r = rec[size_t(s)];
            if (r.have && r.act.act == A_THROW && r.me.st == S_FREE && r.md.st == D_HAND) thr_cmd = s;
        }
        Obs ot = o;
        ot.t = tau;
        ot.p[me] = rec[size_t(tau)].me;
        ot.d[me] = rec[size_t(tau)].md;
        // 记住看得见时的对手状态；看不见的（草丛里）用"最后一次看到的位置往我这边走"估计出一个幽灵
        for (int k = 0; k < o.n; ++k) {
            ghost[k] = false;
            if (k == me || is_mate(*gi, k)) continue;
            if (o.p[k].alive && o.p[k].vis) {
                mem[k].p = o.p[k];
                mem[k].t_seen = tau;
                mem[k].have = true;
            } else if (!mem[k].have) {
                mem[k].p = o.p[k];
                mem[k].p.alive = 1; mem[k].p.vis = 1;
                mem[k].p.x = gi->spx[k]; mem[k].p.y = gi->spy[k];
                mem[k].p.vx = mem[k].p.vy = 0; mem[k].p.st = S_FREE; mem[k].p.timer = 0; mem[k].p.ax = mem[k].p.ay = 0;
                mem[k].t_seen = 0;
                mem[k].have = true;
            }
            if (o.p[k].alive && !o.p[k].vis) {
                const Player& my = ot.p[me];
                const Player& gp = mem[k].p;
                const double age = double(std::max<i64>(0, tau - mem[k].t_seen));
                double dx = double(my.x - gp.x), dy = double(my.y - gp.y);
                const double d = std::max(1.0, dlen(dx, dy));
                const double mvd = std::min(age * 150.0, std::max(0.0, d - 2600.0));
                ot.p[k] = gp;
                ot.p[k].alive = 1;
                ot.p[k].vis = 1;
                ot.p[k].x = gp.x + i64(dx / d * mvd);
                ot.p[k].y = gp.y + i64(dy / d * mvd);
                ot.p[k].st = S_FREE; ot.p[k].timer = 0; ot.p[k].ax = ot.p[k].ay = 0;
                ot.p[k].vx = mvd > 1 ? i64(dx / d * 150.0) : 0;
                ot.p[k].vy = mvd > 1 ? i64(dy / d * 150.0) : 0;
                ot.p[k].cd = 0;
                if (ot.d[k].st == D_HAND) { ot.d[k].x = ot.p[k].x; ot.d[k].y = ot.p[k].y; }
                // 他的镖在"召回引导中"（地上，st=4）：镖的状态看得见，他本人在草丛里看不见——补上召回状态，推演里镖才会按时起飞
                if (ot.d[k].st == D_RISE) { ot.p[k].st = S_RECALL; ot.p[k].timer = std::max<i64>(1, ot.d[k].left); ot.p[k].vx = ot.p[k].vy = 0; }
                ghost[k] = true;
            }
        }
        for (int i = 0; i < NH; ++i) {
            Game g(*gi, ot);
            for (i64 s = tau; s < T; ++s) {
                Action acts[MAXP];
                acts[me] = rec[size_t(s)].act;
                for (int k = 0; k < g.n; ++k)
                    if (k != me) acts[k] = is_mate(*gi, k) ? Action() : opp_action(g, k, hyps[i], thr_cmd);
                g.step(acts);
                ++steps_used;
            }
            g.over = false;
            base[i] = g;
        }
    }

    // ------------------------------------------------------------ 提前量
    // 估计对手 k 现在的位置 / 速度（用 HOLD 假设，即 base[0]... 取假设 1 的基础局面）
    bool opp_state(int k, Player& out) const {
        const Game& g = base[std::min(1, NH - 1)];
        if (!g.p[k].alive || g.p[k].unknown) return false;
        out = g.p[k];
        return true;
    }

    // 扔镖提前量：返回若干候选方向
    int throw_aims(const Player& M, const Player& Q, i64* ax, i64* ay, int maxn) const {
        int cnt = 0;
        const double ox = double(M.x) + double(M.vx), oy = double(M.y) + double(M.vy);
        // 飞行累计距离
        double S[16];
        double acc = 0;
        for (int i = 1; i <= 13; ++i) {
            const double sp = std::max<double>(gi->cfg.dart_vmin, gi->cfg.dart_v - (i - 1) * gi->cfg.dart_dec);
            acc = std::min<double>(acc + sp, gi->cfg.range);
            S[i] = acc;
        }
        double bestErr = 1e18;
        int besti = 3;
        for (int i = 1; i <= 12; ++i) {
            const double qx = double(Q.x) + double(Q.vx) * (i + 2), qy = double(Q.y) + double(Q.vy) * (i + 2);
            const double e = std::fabs(dlen(qx - ox, qy - oy) - S[i]);
            if (e < bestErr) { bestErr = e; besti = i; }
        }
        auto add = [&](double dx, double dy) {
            if (cnt >= maxn) return;
            ax[cnt] = std::llround(dx);
            ay[cnt] = std::llround(dy);
            if (ax[cnt] == 0 && ay[cnt] == 0) return;
            ++cnt;
        };
        const double qx = double(Q.x) + double(Q.vx) * (besti + 2) - ox, qy = double(Q.y) + double(Q.vy) * (besti + 2) - oy;
        const double base_ang = std::atan2(qy, qx);
        const double offs[5] = {0, 0.12, -0.12, 0.24, -0.24};
        for (int i = 0; i < 5; ++i) {
            const double a = base_ang + offs[i];
            add(std::cos(a) * 1000, std::sin(a) * 1000);
        }
        // 直接朝对手当前位置
        add(double(Q.x) - ox, double(Q.y) - oy);
        return cnt;
    }

    // ------------------------------------------------------------ 主决策
    int fidx[16];
    double fw[16];
    int NF = 0;

    double eval_fast(const Plan& pl, int H) const {
        double v = 0;
        for (int i = 0; i < NF; ++i) v += fw[i] * risk_u(rollout(base[fidx[i]], hyps[fidx[i]], pl, H));
        return v;
    }

    struct Cand {
        Plan p;
        double fast = -1e18;
    };

    Action finish(const Obs& o, const Plan& best, double bestV) {
        Action a;
        a.mx = best.mx; a.my = best.my;
        a.act = best.act; a.ax = best.ax; a.ay = best.ay;
        if (DBG) std::fprintf(stderr, "[t=%lld] pick %s act=%lld m=(%lld,%lld) v=%.1f cpu=%.2fms trunc=%d\n", (long long)T, best.name, (long long)best.act, (long long)best.mx, (long long)best.my, bestV, (thread_cpu_100ns() - cpu_start) / 10000.0, trunc_cnt);
        trunc_cnt = 0;
        rec[size_t(o.t)].act = a;
        return a;
    }

    Action decide(const Obs& o) {
        wall0 = std::chrono::steady_clock::now();
        cpu_start = thread_cpu_100ns();
        const GameInfo& G = info();
        gi = &G;
        me = o.seat;
        n = o.n;
        if (rec.size() <= size_t(o.t)) rec.resize(size_t(o.t) + 1);
        rec[size_t(o.t)].me = o.me();
        rec[size_t(o.t)].md = o.my_dart();
        rec[size_t(o.t)].have = true;
        const Player& P = o.me();
        Plan none;
        none.name = "none";
        T = o.t;
        if (!P.alive) return finish(o, none, 0);
        // 不能动 / 不能做任何事的状态：直接返回
        if (P.st == S_SLASH || P.st == S_SLASHED || P.st == S_KICK || P.st == S_KICKED || P.st == S_STUN) return finish(o, none, 0);
        update_beliefs(o);
        setup_hyps();
        hyps = HY;
        NH = NH_FULL;
        int H = 16;
        steps_used = 0;
        step_budget = 4000;
        if (n > 2) { H = 11; step_budget = 2400; }
        if (n > 4) { H = 8; step_budget = 1100; }
        build_bases(o);
        // 快速评估用的假设子集
        {
            static const int pick5[] = {1, 8, 10, 14, 13};
            const int* pick = pick5;
            const int npick = 5;
            NF = 0;
            double ws = 0;
            for (int pi = 0; pi < npick; ++pi) { const int idx = pick[pi]; if (idx >= NH) continue; fidx[NF] = idx; ws += hyps[idx].w; ++NF; }
            if (n > 4) NF = std::min(NF, 3);
            ws = 0;
            for (int i = 0; i < NF; ++i) ws += hyps[fidx[i]].w;
            for (int i = 0; i < NF; ++i) fw[i] = hyps[fidx[i]].w / ws;
        }
        if (P.st == S_DASH && !base[1].dash_moving(P)) return finish(o, none, 0);  // 前摇 / 收势：什么也做不了

        const Dart& MD = o.my_dart();
        const bool free_state = P.st == S_FREE;
        const bool armed = MD.st == D_HAND;
        std::vector<Cand> cs;
        cs.reserve(80);
        auto addc = [&](const Plan& p) {
            Cand c;
            c.p = p;
            c.fast = eval_fast(p, H);
            if (p.act == A_DASH) c.fast -= 20.0;
            if (DBG >= 2) std::fprintf(stderr, "  [t=%lld] cand %s act=%lld a=(%lld,%lld) m=(%lld,%lld) j=%d fast=%.1f\n", (long long)T, p.name, (long long)p.act, (long long)p.ax, (long long)p.ay, (long long)p.mx, (long long)p.my, p.melee_j, c.fast);
            cs.push_back(c);
        };

        // 阶段 A：移动（冲刺移动中不能移动，只考虑近战）
        Plan mv0 = none;
        const bool can_move = P.st != S_DASH;
        if (can_move) {
            double bv = -1e18;
            for (int d = -1; d < NDIR; ++d) {
                Plan p;
                if (d >= 0) { p.mx = DIRX[d]; p.my = DIRY[d]; }
                p.name = "move";
                addc(p);
                Cand& c = cs.back();
                if (P.st == S_RECALL && d < 0) c.fast += 25.0;
                c.fast += (rndf() - 0.5) * 2.0;
                if (c.fast > bv) { bv = c.fast; mv0 = p; }
                if (out_of_time()) break;
            }
        } else {
            addc(none);
        }

        // 阶段 B：动作候选
        int tg[MAXP], ntg = 0;
        for (int k = 0; k < n; ++k) {
            if (k == me || is_mate(G, k)) continue;
            const Player& Q = base[1].p[k];
            if (!Q.alive || Q.unknown || ghost[k]) continue;
            tg[ntg++] = k;
        }
        std::sort(tg, tg + ntg, [&](int a, int b) {
            const Player& A = base[1].p[a];
            const Player& B = base[1].p[b];
            return dlen(double(A.x - P.x), double(A.y - P.y)) < dlen(double(B.x - P.x), double(B.y - P.y));
        });
        if (ntg > 2) ntg = 2;
        const bool dashing_now = P.st == S_DASH;
        const bool can_act = free_state || dashing_now;
        for (int ti = 0; can_act && ti < ntg && !out_of_time(); ++ti) {
            const int k = tg[ti];
            const Player& Q = base[1].p[k];
            const double dist = dlen(double(Q.x - P.x), double(Q.y - P.y));
            const bool tgt_busy = Q.st != S_FREE && Q.st != S_DEAD;
            if (free_state && armed && (dist <= throw_max || tgt_busy)) {
                i64 ax[8], ay[8];
                const int na = throw_aims(P, Q, ax, ay, n > 2 ? 5 : 7);
                for (int i = 0; i < na && !out_of_time(); ++i) {
                    Plan p = mv0;
                    p.act = A_THROW; p.ax = ax[i]; p.ay = ay[i]; p.name = "throw";
                    addc(p);
                }
            }
            const bool tgt_committed = Q.st == S_THROW || Q.st == S_THROWN || Q.st == S_SLASH || Q.st == S_SLASHED || Q.st == S_KICK ||
                                       Q.st == S_KICKED || Q.st == S_RECALL;
            const bool melee_ok = armed ? dist < 1800 : (dist < 1200 || (tgt_committed && dist < 1800));
            if ((melee_ok || dashing_now) && !out_of_time()) {
                for (int v = 0; v < 2; ++v) {
                    Plan p = mv0;
                    p.act = A_MELEE;
                    const double lead = v == 0 ? 1.0 : 3.0;
                    p.ax = std::llround(double(Q.x) + double(Q.vx) * lead - double(P.x));
                    p.ay = std::llround(double(Q.y) + double(Q.vy) * lead - double(P.y));
                    if (p.ax == 0 && p.ay == 0) p.ax = 1000;
                    if (!dashing_now) { p.mx = 0; p.my = 0; }
                    p.name = armed ? "slash" : "kick";
                    addc(p);
                }
            }
            if (DASHSLASH && free_state && P.cd == 0 && dist < 4300 && !out_of_time()) {
                for (int mj = 3; mj <= 5; ++mj) {
                    Plan p;
                    p.act = A_DASH;
                    p.ax = std::llround(double(Q.x) + double(Q.vx) * 3.0 - double(P.x));
                    p.ay = std::llround(double(Q.y) + double(Q.vy) * 3.0 - double(P.y));
                    if (p.ax == 0 && p.ay == 0) p.ax = 1000;
                    p.melee_j = mj;
                    p.tgt = k;
                    p.name = mj < 0 ? "dash" : "dashslash";
                    addc(p);
                    if (out_of_time()) break;
                }
            }
        }
        // 冲刺移动中：随时可以用近战提前结束冲刺（避免终点落进坑 / 贴脸）；朝冲刺方向也算一个候选
        if (dashing_now && !out_of_time()) {
            Plan p;
            p.act = A_MELEE;
            p.ax = P.ax; p.ay = P.ay;
            if (p.ax == 0 && p.ay == 0) p.ax = 1000;
            p.name = "dashstop";
            addc(p);
        }
        // 冲刺移动中：这一步先继续冲，之后第 1~3 步再近战（"现在就砍"和"一直冲"之间的选择）
        if (dashing_now && !out_of_time()) {
            for (int jj = 1; jj <= 3; ++jj) {
                Plan p = none;
                p.melee_j = jj;
                p.tgt = ntg > 0 ? tg[0] : -1;
                p.name = "dashlater";
                addc(p);
            }
        }
        // 挡镖
        if (armed && free_state && !out_of_time()) {
            const Game& g1 = base[1];
            for (int j = 0; j < g1.n && !out_of_time(); ++j) {
                if (j == me || is_mate(G, j)) continue;
                const Dart& Dj = g1.d[j];
                if (!dart_flying(Dj.st)) continue;
                const double dx = double(Dj.x - P.x), dy = double(Dj.y - P.y);
                if (dlen(dx, dy) > 3800) continue;
                const double stp = double(dart_step_len(g1.cf, Dj));
                double nx = double(Dj.x), ny = double(Dj.y);
                if (Dj.st == D_OUT) {
                    nx += double(Dj.vx) * stp / 1000.0;
                    ny += double(Dj.vy) * stp / 1000.0;
                } else {
                    const double ox = double(g1.p[j].x - Dj.x), oy = double(g1.p[j].y - Dj.y);
                    const double ol = dlen(ox, oy);
                    if (ol > 1) { nx += ox / ol * std::min(stp, ol); ny += oy / ol * std::min(stp, ol); }
                }
                const double ang0 = std::atan2(ny - double(P.y), nx - double(P.x));
                const double offs[3] = {0, 0.17, -0.17};
                for (int i = 0; i < 3 && !out_of_time(); ++i) {
                    Plan p;
                    p.act = A_MELEE;
                    p.ax = std::llround(std::cos(ang0 + offs[i]) * 1000);
                    p.ay = std::llround(std::sin(ang0 + offs[i]) * 1000);
                    p.name = "block";
                    addc(p);
                }
            }
        }
        if (free_state && P.cd == 0 && MD.st == D_GROUND && !out_of_time()) {
            const double ddx = double(MD.x - P.x), ddy = double(MD.y - P.y);
            bool safe = true;
            for (int k = 0; k < n; ++k) {
                if (k == me || is_mate(G, k) || !base[1].p[k].alive) continue;
                if (dlen(double(base[1].p[k].x - P.x), double(base[1].p[k].y - P.y)) < 6500) safe = false;
            }
            if (safe && dlen(ddx, ddy) > 3600) {
                Plan p;
                p.act = A_DASH; p.ax = std::llround(ddx); p.ay = std::llround(ddy);
                p.name = "dashdart";
                addc(p);
            }
        }
        bool recall_ok = true;
        for (int k = 0; k < n; ++k) {
            if (k == me || is_mate(G, k)) continue;
            const Player& Q = base[1].p[k];
            if (!Q.alive) continue;
            const double dd = dlen(double(Q.x - P.x), double(Q.y - P.y));
            const bool qarmed = base[1].d[k].st == D_HAND || base[1].d[k].st == D_BACK || base[1].d[k].st == D_CALL;
            if (dd < 4500 || (qarmed && dd < 7500)) recall_ok = false;
        }
        if (free_state && MD.st == D_GROUND && recall_ok && !out_of_time()) {
            Plan p;
            p.act = A_RECALL;
            p.name = "recall";
            addc(p);
        }

        // 第二遍：对快速评估最高的几个候选做完整评估
        std::vector<int> order(cs.size());
        for (size_t i = 0; i < cs.size(); ++i) order[i] = int(i);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return cs[size_t(a)].fast > cs[size_t(b)].fast; });
        const int K = std::min<int>(int(order.size()), n > 2 ? 3 : 4);
        double bestV = -1e18;
        Plan best = none;
        for (int i = 0; i < K; ++i) {
            Cand& c = cs[size_t(order[size_t(i)])];
            double v = -1e17;   // 应急刹车触发时跳过完整评估（快速分和完整分不可比，所以不让它们参选）
            if (i == 0 || wall_ms() < 8.0) {
                v = eval_plan(c.p, H);
                if (c.p.act == A_DASH) v -= 20.0;
                if (P.st == S_RECALL && c.p.mx == 0 && c.p.my == 0 && c.p.act == A_NONE) v += 25.0;
                v += (rndf() - 0.5) * 2.0;
            }
            if (DBG) std::fprintf(stderr, "  [t=%lld] top%d %s act=%lld m=(%lld,%lld) fast=%.1f full=%.1f\n", (long long)T, i, c.p.name, (long long)c.p.act, (long long)c.p.mx, (long long)c.p.my, c.fast, v);
            if (v > bestV) { bestV = v; best = c.p; }
        }
        if (DBG >= 3) {
            for (int i = 0; i < NH; ++i) std::fprintf(stderr, "    hyp%d w=%.2f v=%.1f\n", i, hyps[i].w, rollout(base[i], hyps[i], best, H));
        }
        return finish(o, best, bestV);
    }
};

}  // namespace

DART_MAIN(Bot)
