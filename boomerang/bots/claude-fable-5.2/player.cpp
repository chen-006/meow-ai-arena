// player.cpp —— 飞镖乱斗参赛程序（fable）
// 思路：1) 用自己的历史动作把"4 步前的完整局面"重放到现在（对齐时间）；
//       2) 对手这 4 步和未来的走法用几种假设（继续/停/靠近/远离/左/右/进攻）；
//       3) 每步对候选动作用裁判引擎 dart::Game 做短程推演打分（MPC）：
//          - 走位：12 个方向 + 停，冲刺 8 方向，挥砍挡镖，召回；
//          - "幻影镖"：假设对手在我看不见的这几步里（或接下来几步）已经朝我的外推位置出手，走位要躲开这些假想镖；
//          - 进攻：扔镖 / 挥砍 / 踢 / 冲刺斩都按"对手走法假设的覆盖率"决定出不出手。
#include "dart.hpp"
#include <algorithm>
#include <cmath>
#include <queue>
#include <vector>
using namespace dart;

namespace {
const int MAXT = 1100;
const i64 INF_LIFE = 1000000;
enum World { W_CONT = 0, W_STOP, W_TOWARD, W_AWAY, W_LEFT, W_RIGHT, W_AGGR, W_N };
enum PlanType { P_MOVE = 0, P_THROW, P_MELEE, P_DASH, P_RECALL, P_DASHMELEE };
struct Plan {
    int type = P_MOVE;
    i64 mx = 0, my = 0, ax = 0, ay = 0;
    int n = 0, target = -1;
};
struct Res {
    bool dead = false, tkill = false, tstun = false, stunned = false, near1 = false, near2 = false, near3 = false;
    int deadStep = 0, kills = 0, n = 0, mateKills = 0;
    double pos = 0;
    i64 x[26], y[26];
};
struct Ph {
    i64 rx, ry, ux, uy, maxd, s;
    double w;
};
const double WW[6] = {0.35, 0.13, 0.13, 0.13, 0.13, 0.13};
#ifndef HOLD_B
#define HOLD_B 150.0
#endif
#ifndef HOLD_MIN
#define HOLD_MIN 5
#endif
#ifndef HOLD_RND
#define HOLD_RND 8
#endif
}  // namespace

struct Bot {
    Player hp[MAXT];
    Dart hd[MAXT];
    Action ha[MAXT];
    int me = 0, N = 0, nx = 0, ny = 0;
    Config cf;
    i64 T = 0, cpu0 = 0, simSteps = 0;
    std::vector<i64> life;
    std::vector<int> zoneDist, goalCells, dartDist;
    i64 zoneFallen = -1, dartKeyX = -1, dartKeyY = -1, dartKeyF = -1;
    bool dartNoSrc = true;
    Map liveM;
    u64 rs = 0x9E3779B97F4A7C15ULL;
    i64 cum[20];
    Game gw[W_N];
    i64 otx[MAXP][MAX_DELAY + 2], oty[MAXP][MAX_DELAY + 2];
    i64 tpx[6][MAXP][20], tpy[6][MAXP][20];
    std::vector<Ph> phs;
    bool duel = true;
    int aggrOnly = -1;
    i64 holdX = 0, holdY = 0;
    i64 lastX[MAXP] = {}, lastY[MAXP] = {}, lastT[MAXP];
    bool lastArmed[MAXP] = {};
    int nEval = 0;
    int holdLeft = 0;
    i64 prx[MAXP][MAXT], pry[MAXP][MAXT];
    bool prv[MAXP][MAXT] = {};
    double acc[MAXP];
    double ww[6];
    std::vector<int> tg;

    double rnd() {
        rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
        return double(rs >> 11) / double(1ULL << 53);
    }

    void init(const GameInfo& gi) {
        me = gi.seat; N = gi.n; cf = gi.cfg;
        duel = cf.mode == M_DUEL;
        const Map& M = gi.M();
        nx = M.nx; ny = M.ny;
        life.assign(size_t(nx) * ny, 0);
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                const int c = M.at(x, y);
                const i64 f = cell_fall_step(gi, x, y);
                life[size_t(y) * nx + x] = (c == C_PIT) ? 0 : (f < 0 ? INF_LIFE : f);
            }
        cum[0] = 0;
        {
            Dart D; D.st = D_OUT;
            i64 left = cf.range;
            for (int n = 1; n < 20; ++n) {
                D.age = n - 1;
                const i64 s = imin(dart_step_len(cf, D), left);
                left -= s;
                cum[n] = cum[n - 1] + s;
            }
        }
        for (int k = 0; k < MAXP; ++k) { acc[k] = 0.35; lastT[k] = -100000; }
        rs ^= u64(gi.spx[me] * 7919 + gi.spy[me] * 104729 + me * 31 + 17);
        for (int i = 0; i < 8; ++i) rnd();
    }

    bool okCell(int x, int y, i64 t) const {
        if (x < 0 || y < 0 || x >= nx || y >= ny) return false;
        const int c = cell_at_step(cf, info().M(), x, y, t);
        return c == C_FLOOR || c == C_GRASS;
    }
    void dij(const std::vector<int>& src, i64 t, std::vector<int>& dist) {
        dist.assign(size_t(nx) * ny, -1);
        typedef std::pair<int, int> PI;
        std::priority_queue<PI, std::vector<PI>, std::greater<PI>> pq;
        for (int s : src) { dist[size_t(s)] = 0; pq.push(PI(0, s)); }
        while (!pq.empty()) {
            const PI top = pq.top(); pq.pop();
            const int dv = top.first, id = top.second;
            if (dv != dist[size_t(id)]) continue;
            const int x = id % nx, y = id / nx;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int a = x + dx, b = y + dy;
                    if (!okCell(a, b, t)) continue;
                    if (dx && dy && (!okCell(x + dx, y, t) || !okCell(x, y + dy, t))) continue;
                    const int nd = dv + ((dx && dy) ? 707 : 500);
                    const int nid = b * nx + a;
                    if (dist[size_t(nid)] < 0 || nd < dist[size_t(nid)]) { dist[size_t(nid)] = nd; pq.push(PI(nd, nid)); }
                }
        }
    }
    void updateZone(i64 t) {
        i64 fallen, warn;
        collapse_state(cf, info().M(), t, fallen, warn);
        if (fallen == zoneFallen) return;
        zoneFallen = fallen;
        i64 best = -1;
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x)
                if (okCell(x, y, t)) best = imax(best, life[size_t(y) * nx + x]);
        goalCells.clear();
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x)
                if (okCell(x, y, t) && life[size_t(y) * nx + x] == best) goalCells.push_back(y * nx + x);
        dij(goalCells, t, zoneDist);
        liveM = live_map(cf, info().M(), t);
    }
    void updateDartDist(const Dart& D, i64 t) {
        if (D.st != D_GROUND && D.st != D_RISE) return;
        if (D.x == dartKeyX && D.y == dartKeyY && zoneFallen == dartKeyF) return;
        dartKeyX = D.x; dartKeyY = D.y; dartKeyF = zoneFallen;
        std::vector<int> src;
        const int cx = cell_index(D.x), cy = cell_index(D.y);
        for (int y = cy - 2; y <= cy + 2; ++y)
            for (int x = cx - 2; x <= cx + 2; ++x) {
                if (!okCell(x, y, t)) continue;
                if (ilen(cell_center(x) - D.x, cell_center(y) - D.y) <= cf.pick_r - 100) src.push_back(y * nx + x);
            }
        dartNoSrc = src.empty();
        dij(src, t, dartDist);
    }

    // ---------------------------------------------------------------- 对手走法假设
    Action oppPolicy(const Game& g, int k, int world) {
        Action a;
        const Player& P = g.p[k];
        if (!P.alive || P.unknown) return a;
        if (is_mate(info(), k)) world = W_CONT;
        if (world == W_AGGR && aggrOnly >= 0 && k != aggrOnly) world = W_CONT;
        const Player& Me = g.p[me];
        const i64 dx = Me.x - P.x, dy = Me.y - P.y;
        const i64 d = ilen(dx, dy);
        i64 mx = 0, my = 0;
        switch (world) {
            case W_CONT: if (ilen(P.vx, P.vy) >= 20) { mx = P.vx; my = P.vy; } break;
            case W_STOP: break;
            case W_TOWARD: case W_AGGR: mx = dx; my = dy; break;
            case W_AWAY: mx = -dx; my = -dy; break;
            case W_LEFT: mx = -dy; my = dx; break;
            case W_RIGHT: mx = dy; my = -dx; break;
            default: break;
        }
        // 看到我的镖以后（出手 5 步后）横向躲
        const Dart& D = g.d[me];
        if (D.st == D_OUT && D.age >= 3) {
            const i64 rx = P.x - D.x, ry = P.y - D.y;
            const i64 along = (rx * D.vx + ry * D.vy) / 1000;
            const i64 cross = (D.vx * ry - D.vy * rx) / 1000;
            if (along > -600 && along < D.left + 900 && iabs(cross) < 1500) {
                i64 sgn = cross >= 0 ? 1 : -1;
                if (iabs(cross) < 250) {
                    const i64 lv = -D.vy * P.vx + D.vx * P.vy;
                    if (lv > 0) sgn = 1; else if (lv < 0) sgn = -1;
                }
                mx = -D.vy * sgn; my = D.vx * sgn;
            }
        }
        // 看到我的镖以后，持镖的对手会算准时机挥砍挡镖
        if (D.st == D_OUT && D.age >= 3 && P.st == S_FREE && g.d[k].st == D_HAND) {
            const i64 sl = imin(dart_step_len(cf, D), D.left);
            const i64 qx = D.x + D.vx * sl / 1000, qy = D.y + D.vy * sl / 1000;
            const i64 ex = qx - P.x, ey = qy - P.y;
            const i64 dq = ilen(ex, ey);
            const i64 cr2 = (D.vx * (P.y - D.y) - D.vy * (P.x - D.x)) / 1000;
            if (dq <= cf.slash_reach - 20 && dq > 0 && iabs(cr2) < 1000) {
                a.act = A_MELEE; a.ax = ex; a.ay = ey;
                return a;
            }
        }
        if (world == W_AGGR) {
            const bool armed = g.d[k].st == D_HAND;
            if (P.st == S_FREE) {
                if (d <= 1300 && !wall_between(liveM, P.x, P.y, Me.x, Me.y)) { a.act = A_MELEE; a.ax = dx; a.ay = dy; }
                else if (d <= (armed ? 4200 : 3000) && P.cd == 0 && !wall_between(liveM, P.x, P.y, Me.x, Me.y)) { a.act = A_DASH; a.ax = dx; a.ay = dy; }
            } else if (P.st == S_DASH && P.timer > cf.dash_post && P.timer < cf.dash_t + cf.dash_post && d <= 2150) {
                a.act = A_MELEE; a.ax = dx; a.ay = dy;
            }
            if (!armed && g.d[k].st == D_GROUND && d > 2500) { mx = g.d[k].x - P.x; my = g.d[k].y - P.y; }
        }
        if (mx || my) {
            i64 ux, uy;
            if (scale_to(mx, my, 1000, ux, uy)) {
                const i64 sp = (P.st == S_FREE) ? cf.run_v : cf.run_v * cf.throw_move / 100;
                const i64 qx = P.x + ux * sp * 2 / 1000, qy = P.y + uy * sp * 2 / 1000;
                if (cell_of_point(cf, g.M(), qx, qy, g.t + 1) == C_PIT) ux = uy = 0;
                mx = ux; my = uy;
            }
        }
        a.mx = mx; a.my = my;
        return a;
    }

    // ---------------------------------------------------------------- 我的计划
    Action myAct(const Game& g, const Plan& pl, int i) {
        Action a;
        const Player& P = g.p[me];
        if (pl.type == P_RECALL) {
            if (i == 0 || P.st == S_RECALL) a.act = A_RECALL;
            return a;
        }
        if (i == 0) {
            if (pl.type == P_THROW) { a.act = A_THROW; a.ax = pl.ax; a.ay = pl.ay; }
            else if (pl.type == P_MELEE) { a.act = A_MELEE; a.ax = pl.ax; a.ay = pl.ay; }
            else if (pl.type == P_DASH || pl.type == P_DASHMELEE) { a.act = A_DASH; a.ax = pl.ax; a.ay = pl.ay; }
        }
        if (pl.type == P_DASHMELEE && i > 0 && P.st == S_DASH && P.timer > cf.dash_post && P.timer <= cf.dash_t + cf.dash_post &&
            (cf.dash_t + cf.dash_post - P.timer) >= pl.n && pl.target >= 0) {
            const Player& Tg = g.p[pl.target];
            if (Tg.alive) {
                // 我还会滑 500+250
                const i64 sx = P.x + P.vx, sy = P.y + P.vy;
                a.act = A_MELEE; a.ax = Tg.x - sx; a.ay = Tg.y - sy;
                if (a.ax == 0 && a.ay == 0) { a.ax = P.ax; a.ay = P.ay; }
                return a;
            }
        }
        i64 mx = pl.mx, my = pl.my;
        if (pl.type == P_THROW && (i == 0 || P.st == S_THROW)) mx = my = 0;
        if (pl.type == P_MELEE) mx = my = 0;
        if (mx || my) {
            i64 ux, uy;
            if (scale_to(mx, my, 1000, ux, uy)) {
                const i64 sp = (P.st == S_FREE) ? cf.run_v : cf.run_v * cf.throw_move / 100;
                for (int k = 1; k <= 2; ++k) {
                    const i64 qx = P.x + ux * sp * k / 1000, qy = P.y + uy * sp * k / 1000;
                    if (cell_of_point(cf, g.M(), qx, qy, g.t + k) == C_PIT) { ux = uy = 0; break; }
                }
                mx = ux; my = uy;
            }
        }
        a.mx = mx; a.my = my;
        return a;
    }

    // ---------------------------------------------------------------- 局面评估（推演终点）
    double posval(const Game& g) {
        const Player& P = g.p[me];
        const Dart& D = g.d[me];
        double v = 0;
        const int cx = cell_index(P.x), cy = cell_index(P.y);
        if (cx < 0 || cy < 0 || cx >= nx || cy >= ny) return -500;
        const int c = cy * nx + cx;
        i64 zd = zoneDist[size_t(c)];
        if (zd < 0) {
            i64 best = 1000000;
            for (int gc : goalCells) best = imin(best, ilen(cell_center(gc % nx) - P.x, cell_center(gc / nx) - P.y));
            zd = best * 3 / 2 + 2000;
        }
        const i64 lf = life[size_t(c)];
        if (lf < INF_LIFE) {
            const double deficit = double(zd) / double(cf.run_v) + 25.0 - double(lf - g.t);
            if (deficit > 0) v -= 10.0 * deficit;
        }
        if (T > 100) v -= double(zd) * 0.004 * std::min(1.0, double(T - 100) / 150.0);
        static const int ox[8] = {1, 1, 0, -1, -1, -1, 0, 1}, oy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        for (int i = 0; i < 8; ++i)
            if (cell_of_point(cf, g.M(), P.x + ox[i] * 450, P.y + oy[i] * 450, g.t + 12) == C_PIT) v -= 4;
        const bool armedMe = D.st == D_HAND;
        if (D.st == D_GROUND || D.st == D_RISE) {
            const i64 eu = ilen(D.x - P.x, D.y - P.y);
            const int pd = dartNoSrc ? -1 : dartDist[size_t(c)];
            if (pd < 0) v -= 0.03 * double(eu * 3 / 2 + 3000);
            else v -= 0.03 * double(pd) + 0.01 * double(eu);
            if (D.st == D_RISE) v += 9.0 * double(cf.recall_pre - P.timer);
        } else if (D.st == D_CALL) {
            v += 9.0 * double(cf.recall_pre) - 0.03 * 3000 - 0.01 * double(ilen(D.x - P.x, D.y - P.y));
        } else if (D.st != D_HAND) {
            v -= 10;
        }
        bool nearEnemy = false;
        for (int k = 0; k < N; ++k) {
            if (k == me || !g.p[k].alive) continue;
            if (g.p[k].unknown) {
                // 藏在草丛里看不见的对手：按上次看见的位置防着点
                if (!is_mate(info(), k) && T - lastT[k] < 160) {
                    const i64 d = ilen(lastX[k] - P.x, lastY[k] - P.y);
                    v -= (lastArmed[k] ? 90.0 : 30.0) * std::max(0.0, std::min(1.0, double(4200 - d) / 2500.0));
                }
                continue;
            }
            const Player& Q = g.p[k];
            const i64 d = ilen(Q.x - P.x, Q.y - P.y);
            if (is_mate(info(), k)) {
                if (d < 1500) v -= 20.0 * double(1500 - d) / 1500.0;
                continue;
            }
            if (d < cf.see_r) nearEnemy = true;
            const bool los = !wall_between(liveM, P.x, P.y, Q.x, Q.y);
            const bool armedOpp = g.d[k].st == D_HAND;
            if (armedOpp) {
                if (los) {
                    v -= (duel ? 120.0 : 150.0) * std::max(0.0, std::min(1.0, double(5600 - d) / 3200.0));
                    if (!armedMe) v -= 150.0 * std::max(0.0, std::min(1.0, double(6800 - d) / 3000.0));
                }
                if (armedMe && duel) v -= 0.004 * double(imax(0, d - 6000));
            } else {
                if (armedMe) {
                    v -= 0.012 * double(imax(0, d - 2800));
                    v -= 50.0 * std::max(0.0, std::min(1.0, double(1900 - d) / 900.0));
                } else {
                    v -= 50.0 * std::max(0.0, std::min(1.0, double(2000 - d) / 1000.0));
                }
            }
        }
        if (!nearEnemy && P.expose == 0 && !dart_flying(D.st) && D.st != D_RISE && cell_of_point(cf, g.M(), P.x, P.y, g.t) == C_GRASS) v += 12;
        return v;
    }

    // ---------------------------------------------------------------- 推演
    void rollout(const Game& g0, const Plan& pl, int world, int H, Res& r, bool wantPos) {
        Game g = g0;
        Action acts[MAXP];
        r = Res();
        for (int i = 0; i < H; ++i) {
            for (int k = 0; k < N; ++k) acts[k] = (k == me) ? myAct(g, pl, i) : oppPolicy(g, k, world);
            i64 dx0[MAXP], dy0[MAXP]; int dst0[MAXP];
            for (int k = 0; k < N; ++k) { dx0[k] = g.d[k].x; dy0[k] = g.d[k].y; dst0[k] = int(g.d[k].st); }
            const i64 mpx = g.p[me].x, mpy = g.p[me].y;
            g.over = false;
            g.step(acts);
            ++simSteps;
            if (wantPos) {
                const i64 R = cf.body_r + cf.dart_r;
                for (int k = 0; k < N; ++k) {
                    if (k == me || !dart_flying(dst0[k])) continue;
                    const i64 ax0 = dx0[k] - mpx, ay0 = dy0[k] - mpy, ax1 = g.d[k].x - g.p[me].x, ay1 = g.d[k].y - g.p[me].y;
                    if (dst0[k] == D_OUT) {
                        if (seg_near_origin(ax0, ay0, ax1, ay1, R + 130)) r.near3 = true;
                    } else {
                        if (seg_near_origin(ax0, ay0, ax1, ay1, R + 250)) r.near2 = true;
                        else if (seg_near_origin(ax0, ay0, ax1, ay1, R + 550)) r.near1 = true;
                    }
                }
            }
            if (i < 26) { r.x[i] = g.p[me].x; r.y[i] = g.p[me].y; }
            r.n = i + 1;
            if (g.p[me].st == S_STUN) r.stunned = true;
            if (pl.target >= 0 && g.p[pl.target].st == S_STUN && g.p[pl.target].kicked_by == me) r.tstun = true;
            if (!g.p[me].alive) { r.dead = true; r.deadStep = i; break; }
        }
        // 推演结束时还在被踢硬直里：把滑行走完，看会不会停在坑里
        if (!r.dead && g.p[me].st == S_STUN) {
            for (int e = 0; e < 13 && g.p[me].alive && g.p[me].st == S_STUN; ++e) {
                for (int k = 0; k < N; ++k) acts[k] = (k == me) ? Action() : oppPolicy(g, k, world);
                g.over = false;
                g.step(acts);
                ++simSteps;
            }
            if (!g.p[me].alive) { r.dead = true; r.deadStep = H; }
        }
        for (int k = 0; k < N; ++k) {
            if (k == me) continue;
            if (is_mate(info(), k)) {
                if (g0.p[k].alive && !g.p[k].alive && g.p[k].killer == me) ++r.mateKills;
                continue;
            }
            if (g0.p[k].alive && !g.p[k].alive && g.p[k].killer == me) {
                ++r.kills;
                if (k == pl.target) r.tkill = true;
            }
        }
        if (!r.dead && wantPos) r.pos = posval(g);
    }

    // 从 4 步前的完整局面重放到现在
    void recon(const Obs& o, int world, Game& g) {
        g = Game(info(), o);
        const i64 t0 = imax(0, T - cf.delay);
        g.t = t0;
        g.p[me] = hp[t0];
        g.p[me].team = info().team[me];
        g.p[me].px = g.p[me].x; g.p[me].py = g.p[me].y;
        g.d[me] = hd[t0];
        Action acts[MAXP];
        if (world == W_CONT)
            for (int k = 0; k < N; ++k) { otx[k][0] = g.p[k].x; oty[k][0] = g.p[k].y; }
        for (i64 s = t0; s < T; ++s) {
            for (int k = 0; k < N; ++k) acts[k] = (k == me) ? ha[s] : oppPolicy(g, k, world);
            g.over = false;
            g.step(acts);
            ++simSteps;
            if (world == W_CONT)
                for (int k = 0; k < N; ++k) { otx[k][s + 1 - t0] = g.p[k].x; oty[k][s + 1 - t0] = g.p[k].y; }
        }
        Player m = o.me();
        m.team = info().team[me];
        m.px = m.x; m.py = m.y;
        g.p[me] = m;
        g.d[me] = o.my_dart();
        g.over = false;
    }

    void buildPhantoms(const Obs& o) {
        phs.clear();
        const i64 t0 = imax(0, T - cf.delay);
        const Game& g = gw[W_CONT];
        const Map& M = info().M();
        for (int k = 0; k < N; ++k) {
            if (k == me || is_mate(info(), k) || !o.p[k].alive || !o.p[k].vis) continue;
            if (o.d[k].st != D_HAND || o.p[k].st != S_FREE) continue;
            for (i64 s = t0; s <= T + 3; ++s) {
                i64 rx, ry;
                const i64 idx = s + 1 - t0;
                if (idx <= T - t0) { rx = otx[k][idx]; ry = oty[k][idx]; }
                else { rx = g.p[k].x + g.p[k].vx * (s + 1 - T); ry = g.p[k].y + g.p[k].vy * (s + 1 - T); }
                const i64 u = imax(0, s - cf.delay);
                const i64 pmx = hp[u].x, pmy = hp[u].y;
                i64 vmx = hp[u].vx, vmy = hp[u].vy;
                if (hp[u].st != S_FREE) { vmx = vmy = 0; }
                const i64 dnow = ilen(pmx - rx, pmy - ry);
                // 那一刻他看得见我吗
                if (dnow > cf.see_r && hp[u].expose == 0 && !dart_flying(hd[u].st) && hd[u].st != D_RISE &&
                    cell_of_point(cf, M, pmx, pmy, u) == C_GRASS) continue;
                for (int type = 0; type < 2; ++type) {
                    i64 ax = 0, ay = 0;
                    bool ok = false;
                    if (type == 0) {
                        for (int n = 1; n <= 12; ++n) {
                            const i64 j = (s + 2 + n) - u;
                            const i64 px = pmx + vmx * j, py = pmy + vmy * j;
                            if (cum[n] + 400 >= ilen(px - rx, py - ry)) { ax = px - rx; ay = py - ry; ok = true; break; }
                        }
                    } else {
                        ax = pmx - rx; ay = pmy - ry;
                        ok = (vmx || vmy) && dnow <= cf.range + 600;
                    }
                    if (!ok) continue;
                    i64 ux, uy;
                    if (!scale_to(ax, ay, 1000, ux, uy)) continue;
                    i64 maxd = cf.range, hx, hy;
                    if (seg_wall(liveM, rx, ry, rx + ux * cf.range / 1000, ry + uy * cf.range / 1000, hx, hy)) maxd = ilen(hx - rx, hy - ry);
                    double w = type == 0 ? 0.04 : 0.02;
                    if (dnow > 5000) w *= 0.6;
                    phs.push_back(Ph{rx, ry, ux, uy, maxd, s, w});
                }
            }
        }
    }
    double phantomPen(const Res& r, i64 sx, i64 sy) {
        double pen = 0;
        const i64 R = cf.body_r + cf.dart_r + 30;
        for (const Ph& ph : phs) {
            for (int n = 1; n <= 12; ++n) {
                const int i = int(ph.s + 1 + n - T);
                if (i < 0) continue;
                if (i >= r.n || i >= 26) break;
                i64 a0 = cum[n - 1], a1 = cum[n];
                if (a0 >= ph.maxd) break;
                if (a1 > ph.maxd) a1 = ph.maxd;
                const i64 d0x = ph.rx + ph.ux * a0 / 1000, d0y = ph.ry + ph.uy * a0 / 1000;
                const i64 d1x = ph.rx + ph.ux * a1 / 1000, d1y = ph.ry + ph.uy * a1 / 1000;
                const i64 c0x = i == 0 ? sx : r.x[i - 1], c0y = i == 0 ? sy : r.y[i - 1];
                if (seg_near_origin(d0x - c0x, d0y - c0y, d1x - r.x[i], d1y - r.y[i], R)) { pen += ph.w; break; }
            }
        }
        return pen * 1000.0;
    }

    // 按"推演工作量"（人·步）收手：实测 thread_cpu_100ns（GetThreadTimes）粒度太粗，一步之内差值总是 0，
    // 偶尔跨过时钟节拍又会一下跳十几毫秒，不能用来做 10 毫秒以内的预算。工作量是确定性的：
    // 四人局实测最多约 5900 人·步 ≈ 2.8 毫秒，4v4 约 11000 人·步 ≈ 4.1 毫秒，这里把可选部分卡在 7000～9500。
    bool workUp(i64 cap) const { return simSteps * N > cap; }

    double scorePlan(const Plan& pl, const Player& M0, int H) {
        Res rc, ra;
        rollout(gw[W_CONT], pl, W_CONT, H, rc, true);
        double s = 0;
        const Game& gn = gw[W_CONT];
        const double wA = duel ? 0.4 : 0.3;
        for (int k : tg) {
            if (ilen(gn.p[k].x - M0.x, gn.p[k].y - M0.y) > (N > 4 ? 4500 : 6500)) continue;
            aggrOnly = k;
            rollout(gw[W_AGGR], pl, W_AGGR, H, ra, false);
            if (ra.dead) s += wA * (-1000.0 + 20.0 * ra.deadStep);
            else if (ra.stunned) s -= wA * 300.0;
        }
        aggrOnly = -1;
        // 冲刺落点离持镖对手太近：对手看到冲刺就能朝落点扔
        if (pl.type == P_DASH && !rc.dead && (rc.near1 || rc.near2)) s -= 200;
        if (pl.type == P_DASH && !rc.dead && rc.n > 6) {
            for (int k : tg) {
                if (gn.d[k].st != D_HAND) continue;
                const i64 d = ilen(gn.p[k].x - rc.x[5], gn.p[k].y - rc.y[5]);
                if (d < 3900 && !wall_between(liveM, gn.p[k].x, gn.p[k].y, rc.x[5], rc.y[5])) s -= 250;
            }
        }
        if (rc.dead) s += 0.6 * (-1000.0 + 5.0 * rc.deadStep);
        else {
            s += rc.pos;
            if (rc.near2) s -= 260; else if (rc.near1) s -= 100;
            if (rc.near3) s -= 80;
        }
        s += 300.0 * rc.kills;
        s -= phantomPen(rc, M0.x, M0.y);
        return s;
    }

    Action decide(const Obs& o) {
        cpu0 = thread_cpu_100ns();
        simSteps = 0;
        T = o.t;
        Action out;
        if (T >= MAXT - 2) return out;
        hp[T] = o.me(); hd[T] = o.my_dart(); ha[T] = out;
        const Player M0 = o.me();
        const Dart D0 = o.my_dart();
        if (!M0.alive) return out;
        // 在线校准：6 步前对"现在"的外推预测准不准（对手越好预测，出手越有把握）
        {
            const i64 tt = T - cf.delay;
            if (tt >= 0)
                for (int k = 0; k < N; ++k) {
                    if (k == me || !o.p[k].alive || !o.p[k].vis || !prv[k][tt]) continue;
                    const i64 err = ilen(o.p[k].x - prx[k][tt], o.p[k].y - pry[k][tt]);
                    acc[k] = 0.92 * acc[k] + 0.08 * (err < 650 ? 1.0 : 0.0);
                }
        }
        for (int k = 0; k < N; ++k)
            if (k != me && o.p[k].alive && o.p[k].vis) { lastX[k] = o.p[k].x; lastY[k] = o.p[k].y; lastT[k] = T; }
        for (int k = 0; k < N; ++k) lastArmed[k] = o.d[k].st == D_HAND;
        nEval = 0;
        updateZone(T);
        updateDartDist(D0, T);
        const bool freeSt = M0.st == S_FREE;
        const bool dashMove = M0.st == S_DASH && M0.timer > cf.dash_post && M0.timer <= cf.dash_t + cf.dash_post;
        const bool canMove = freeSt || M0.st == S_THROW || M0.st == S_THROWN || M0.st == S_RECALL;
        if (!canMove && !dashMove) return out;
        const bool attackPhase = freeSt || dashMove;
        const int nW = attackPhase ? 6 : 1;
        for (int w = 0; w < nW; ++w) recon(o, w, gw[w]);
        gw[W_AGGR] = gw[W_CONT];
        const Game& g0 = gw[W_CONT];
        tg.clear();
        for (int k = 0; k < N; ++k)
            if (k != me && !is_mate(info(), k) && g0.p[k].alive && !g0.p[k].unknown) tg.push_back(k);
        const bool armed = D0.st == D_HAND;

        if (attackPhase && !tg.empty()) {
            // 各假设下对手未来 15 步的位置
            for (int w = 0; w < 6; ++w) {
                Game g = gw[w];
                Action acts[MAXP];
                for (int k : tg) { tpx[w][k][0] = g.p[k].x; tpy[w][k][0] = g.p[k].y; }
                for (int j = 1; j <= 15; ++j) {
                    for (int k = 0; k < N; ++k) acts[k] = (k == me) ? Action() : oppPolicy(g, k, w);
                    g.over = false;
                    g.step(acts);
                    ++simSteps;
                    for (int k : tg) { tpx[w][k][j] = g.p[k].x; tpy[w][k][j] = g.p[k].y; }
                }
            }
            for (int k : tg)
                if (T + 6 < MAXT && o.p[k].st == S_FREE) { prx[k][T + 6] = tpx[W_CONT][k][6]; pry[k][T + 6] = tpy[W_CONT][k][6]; prv[k][T + 6] = true; }
            auto setW = [&](int k) {
                const double wc = std::max(0.15, std::min(0.75, acc[k]));
                ww[0] = wc;
                for (int w = 1; w < 6; ++w) ww[w] = (1.0 - wc) / 5.0;
            };
            // ---- 近战（挥砍 / 踢）
            {
                i64 sx = M0.x, sy = M0.y;
                if (dashMove) { sx += M0.vx; sy += M0.vy; }
                double bestCov = 0; Plan bestPl;
                for (int k : tg) {
                    const i64 ex = tpx[W_CONT][k][2] - sx, ey = tpy[W_CONT][k][2] - sy;
                    if (ilen(ex, ey) > 1900) continue;
                    Plan pl; pl.type = P_MELEE; pl.ax = ex; pl.ay = ey; pl.target = k;
                    if (ex == 0 && ey == 0) { pl.ax = 1000; }
                    setW(k);
                    double cov = 0, dead = 0, covKill = 0;
                    for (int w = 0; w < 6; ++w) {
                        Res r;
                        rollout(gw[w], pl, w, armed ? 3 : 15, r, false);
                        if (armed ? r.tkill : (r.tstun || r.tkill)) cov += ww[w];
                        if (r.tkill) covKill += ww[w];
                        if (r.dead) dead += ww[w];
                    }
                    cov -= dead * 0.5;
                    if (!armed) {
                        // 踢：只有能把人踢进坑，或 1v1 里十拿九稳时才踢（踢完 7 步动不了，很容易挨打）
                        if (covKill >= 0.3) cov = 1.0 + covKill;
                        else if (!(duel && !dashMove && cov >= 0.62)) cov = 0;
                    }
                    if (cov > bestCov) { bestCov = cov; bestPl = pl; }
                }
                const double thr = dashMove ? (armed ? 0.45 : 0.62) : 0.5;
                if (bestCov >= thr) {
                    out = myAct(g0, bestPl, 0);
                    ha[T] = out;
                    return out;
                }
            }
            if (dashMove) { ha[T] = out; return out; }
            // ---- 扔镖
            if (armed) {
                bool allUnarmed = true;
                for (int k : tg) if (g0.d[k].st == D_HAND) allUnarmed = false;
                double bestCov = 0; Plan bestPl;
                for (int k : tg) {
                    if (workUp(7000)) break;
                    i64 aimx[3], aimy[3]; int na = 0, nhit[3] = {0, 0, 0};
                    setW(k);
                    for (int wi = 0; wi < 2; ++wi) {
                        const int w = wi == 0 ? W_CONT : W_STOP;
                        for (int n = 1; n <= 12; ++n) {
                            const i64 px = tpx[w][k][n + 2], py = tpy[w][k][n + 2];
                            if (cum[n] + 400 >= ilen(px - M0.x, py - M0.y)) {
                                i64 ux, uy;
                                if (scale_to(px - M0.x, py - M0.y, 1000, ux, uy) && !wall_between(liveM, M0.x, M0.y, px, py)) {
                                    aimx[na] = ux; aimy[na] = uy; nhit[na] = n; ++na;
                                }
                                break;
                            }
                        }
                    }
                    if (na == 2) {
                        if (iabs(aimx[0] - aimx[1]) + iabs(aimy[0] - aimy[1]) < 40) na = 1;
                        else { aimx[2] = aimx[0] + aimx[1]; aimy[2] = aimy[0] + aimy[1]; nhit[2] = std::max(nhit[0], nhit[1]); na = 3; }
                    }
                    for (int ai = 0; ai < na; ++ai) {
                        Plan pl; pl.type = P_THROW; pl.ax = aimx[ai]; pl.ay = aimy[ai]; pl.target = k;
                        const int H = std::min(18, nhit[ai] + 5);
                        double cov = 0; bool deadC = false;
                        for (int w = 0; w < 6; ++w) {
                            Res r;
                            rollout(gw[w], pl, w, H, r, false);
                            if (r.kills > 0) cov += ww[w] * (r.tkill ? 1.0 : 0.8);
                            if (r.mateKills > 0) cov -= 1.0;
                            if (r.dead && w == W_CONT) deadC = true;
                        }
                        if (deadC) cov -= 0.35;
                        if (cov > bestCov) { bestCov = cov; bestPl = pl; }
                    }
                }
                const double thr = allUnarmed ? 0.33 : 0.45;
                if (bestCov >= thr) {
                    out = myAct(g0, bestPl, 0);
                    ha[T] = out;
                    return out;
                }
            }
            // ---- 冲刺斩
            if (armed && M0.cd == 0 && !workUp(7500)) {
                double bestCov = 0; Plan bestPl;
                for (int k : tg) {
                    setW(k);
                    for (int n = 1; n <= 2; ++n) {
                        const int jj = 4 + n;
                        const i64 ex = tpx[W_CONT][k][jj] - M0.x, ey = tpy[W_CONT][k][jj] - M0.y;
                        const i64 dd = ilen(ex, ey);
                        if (n == 1 && (dd < 1300 || dd > 2700)) continue;
                        if (n == 2 && (dd <= 2700 || dd > 3400)) continue;
                        Plan pl; pl.type = P_DASHMELEE; pl.ax = ex; pl.ay = ey; pl.n = n; pl.target = k;
                        double cov = 0; bool bad = false;
                        for (int w = 0; w < 6; ++w) {
                            Res r;
                            rollout(gw[w], pl, w, 5 + n, r, false);
                            if (r.dead) { if (w == W_CONT) bad = true; continue; }
                            if (r.tkill) cov += ww[w];
                        }
                        if (bad) continue;
                        if (cov > bestCov) { bestCov = cov; bestPl = pl; }
                    }
                }
                if (bestCov >= 0.62) {
                    out = myAct(g0, bestPl, 0);
                    ha[T] = out;
                    return out;
                }
            }
        }

        // ---- 走位候选
        buildPhantoms(o);
        const int H = N > 4 ? 10 : 14;  // 8 人局（4v4）推演量大，缩短视野留用时余量；1v1 / 四人局不变
        double bestS = -1e18; Plan bestPl;
        const i64 vl = ilen(M0.vx, M0.vy);
        // 受威胁（持镖对手在射程内且看得见我）时：保持一个方向 HOLD 步，到点强制大角度转向。
        // 每步重新规划会来回抖动（等于原地不动），一直走直线又会被提前量命中。
        bool threat = false;
        for (int k : tg) {
            if (g0.d[k].st != D_HAND) continue;
            if (ilen(g0.p[k].x - M0.x, g0.p[k].y - M0.y) < 7500 && !wall_between(liveM, g0.p[k].x, g0.p[k].y, M0.x, M0.y)) threat = true;
        }
        // 藏在草丛里、手里有镖的对手（看不见）：按上次看见的位置也算威胁，不能在他附近站着不动
        for (int k = 0; k < N; ++k) {
            if (k == me || is_mate(info(), k) || !o.p[k].alive || o.p[k].vis) continue;
            if (lastArmed[k] && T - lastT[k] < 240 && ilen(lastX[k] - M0.x, lastY[k] - M0.y) < 7000) threat = true;
        }
        if (!threat) holdLeft = 0;
        const bool haveHold = threat && (holdX || holdY);
        auto consider = [&](const Plan& pl, double bonus) {
            if (++nEval > 13 && workUp(9500)) return;
            double s = scorePlan(pl, M0, H) + bonus + rnd() * 3.0;
            if (threat && pl.type == P_MOVE) {
                if (!pl.mx && !pl.my) s -= HOLD_B * 0.7;
                else if (haveHold) {
                    const double c = double(pl.mx * holdX + pl.my * holdY) / 1.0e6;
                    if (holdLeft > 0) s += HOLD_B * std::max(0.0, c);
                    else s -= HOLD_B * std::max(0.0, (c - 0.3) / 0.7);
                }
            }
            if (pl.type == P_MOVE && vl >= 20 && (pl.mx || pl.my)) {
                const double c = double(pl.mx * M0.vx + pl.my * M0.vy) / (1000.0 * double(vl));
                s -= 5.0 * (1.0 - c);
            }
            if (s > bestS) { bestS = s; bestPl = pl; }
        };
        static const int DX[12] = {1000, 866, 500, 0, -500, -866, -1000, -866, -500, 0, 500, 866};
        static const int DY[12] = {0, 500, 866, 1000, 866, 500, 0, -500, -866, -1000, -866, -500};
        { Plan pl; consider(pl, 0); }
        for (int i = 0; i < 12; ++i) { Plan pl; pl.mx = DX[i]; pl.my = DY[i]; consider(pl, 0); }
        if (M0.st == S_RECALL) { Plan pl; pl.type = P_RECALL; consider(pl, 0); }
        if (freeSt) {
            // 召回：走不到或者很远
            if (cf.recall && D0.st == D_GROUND) {
                const int cx = cell_index(M0.x), cy = cell_index(M0.y);
                int pd = -1;
                if (!dartNoSrc && cx >= 0 && cy >= 0 && cx < nx && cy < ny) pd = dartDist[size_t(cy) * nx + cx];
                bool allow = pd < 0 || pd > 5500;
                for (int k : tg)
                    if (g0.d[k].st == D_HAND && ilen(g0.p[k].x - D0.x, g0.p[k].y - D0.y) < 5000) allow = true;
                if (allow) { Plan pl; pl.type = P_RECALL; consider(pl, 0); }
            }
            // 挥砍挡镖
            if (armed) {
                for (int k : tg) {
                    const Dart& E = g0.d[k];
                    if (!dart_flying(E.st)) continue;
                    i64 qx = E.x, qy = E.y;
                    if (E.st == D_OUT) {
                        const i64 sl = imin(dart_step_len(cf, E), E.left);
                        qx += E.vx * sl / 1000; qy += E.vy * sl / 1000;
                    } else { qx += E.vx; qy += E.vy; }
                    const i64 dd = ilen(qx - M0.x, qy - M0.y);
                    if (dd > 1500 || dd < 300) continue;
                    Plan pl; pl.type = P_MELEE; pl.ax = qx - M0.x; pl.ay = qy - M0.y;
                    consider(pl, -5);
                }
            }
            // 冲刺
            if (M0.cd == 0 && !workUp(8000)) {
                for (int i = 0; i < 12; i += (N > 4 ? 3 : 1)) {
                    if (i % 3 == 1 && N > 2) continue;
                    Plan pl; pl.type = P_DASH; pl.ax = DX[i]; pl.ay = DY[i]; pl.mx = DX[i]; pl.my = DY[i];
                    consider(pl, -40);
                    if (workUp(8500)) break;
                }
            }
        }
        if (threat && bestPl.type == P_MOVE && (bestPl.mx || bestPl.my)) {
            const double c = (holdX || holdY) ? double(bestPl.mx * holdX + bestPl.my * holdY) / 1.0e6 : -1.0;
            if (holdLeft <= 0 || c < 0.7) {
                holdX = bestPl.mx; holdY = bestPl.my;
                holdLeft = HOLD_MIN + int(rnd() * HOLD_RND);
            } else {
                --holdLeft;
            }
        } else {
            holdLeft = 0; holdX = holdY = 0;
        }
        out = myAct(g0, bestPl, 0);
        ha[T] = out;
        return out;
    }
};

DART_MAIN(Bot)
