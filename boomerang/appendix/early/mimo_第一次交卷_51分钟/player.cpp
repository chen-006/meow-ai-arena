// player.cpp —— 飞镖乱斗参赛程序（思路见 notes.md）
// 核心：
//   1) 延迟补偿：用 dart::Game 把 4 步前的观察往前推 DELAY 步（动作按"继续当前速度"猜），
//      别人蓄势的镖会照常出手、冲刺照常走完，得到"现在"的全场估计；自己的状态直接用观察。
//   2) 躲镖：解析式推演每把敌方镖的未来轨迹（去程减速 / 最远点停留 / 回程追主 / 召回穿墙、撞墙截断），
//      对 16 个跑步方向 + 冲刺方向做逐段相对运动扫掠判定，选净空最大的安全方向。
//   3) 扔镖：对每个敌人建 6 条运动假设（继续 / 左横 / 右横 / 停 / 逼近 / 远离，带 5 步反应延迟与硬直强制段），
//      对多个瞄准点计算加权命中率，过阈值才出手。
//   4) 近战：按 2 步起手的判定几何预测；冲刺接近战按"滑 500+250 后判定"算判定位置；
//      踢按 12 步滑行（0.8 衰减、撞墙）终点判落坑，能踢下坑 / 打断召回 / 打断蓄势才踢。
//   5) 走位：拉扯 + 环绕 + 墙后掩护 + 草丛；塌陷时去最近的永不塌格；没镖先捡 / 安全时召回。
#include "dart.hpp"
#include <vector>
using namespace dart;

namespace {

constexpr int TR = 22;

struct Trace {
    i64 x[TR], y[TR];
    int n = 0;
    void clear() { n = 0; }
    void push(i64 px, i64 py) { if (n < TR) { x[n] = px; y[n] = py; ++n; } }
};

inline i64 seg_dist_origin(i64 ax, i64 ay, i64 bx, i64 by) {
    const i64 dx = bx - ax, dy = by - ay;
    const i64 dd = dx * dx + dy * dy;
    if (dd == 0) return ilen(ax, ay);
    const i64 tnum = -(ax * dx + ay * dy);
    if (tnum <= 0) return ilen(ax, ay);
    if (tnum >= dd) return ilen(bx, by);
    const __int128 num = (__int128)(ax * ax + ay * ay) * dd - (__int128)tnum * tnum;
    if (num <= 0) return 0;
    return isqrt((i64)(num / dd));
}

inline void unit_dir(i64 ang, i64& dx, i64& dy) {
    const TrigTable& T = trig_table();
    const i64 a = norm_angle(ang);
    i64 sx, cx;
    if (a <= 9000) { cx = T.s[9000 - a]; sx = T.s[a]; }
    else if (a <= 18000) { cx = -T.s[a - 9000]; sx = T.s[18000 - a]; }
    else if (a <= 27000) { cx = -T.s[27000 - a]; sx = -T.s[a - 18000]; }
    else { cx = T.s[a - 27000]; sx = -T.s[36000 - a]; }
    dx = rdiv(cx * 1000, 1000000);
    dy = rdiv(sx * 1000, 1000000);
}

struct Hyp { Trace tr; i64 w; };

struct Bot {
    const GameInfo* G = nullptr;
    Map live;
    i64 memx[8], memy[8];
    std::vector<std::pair<int, int>> safe_cells;
    i64 safe_x = 0, safe_y = 0;
    u64 hs = 0x9E3779B97F4A7C15ULL;

    void init(const GameInfo& gi) {
        G = &gi;
        for (int k = 0; k < 8; ++k) { memx[k] = gi.spx[k]; memy[k] = gi.spy[k]; }
        const Map& M = gi.M();
        i64 sx = 0, sy = 0;
        for (int y = 0; y < M.ny; ++y)
            for (int x = 0; x < M.nx; ++x) {
                if (M.at(x, y) == C_WALL || M.at(x, y) == C_PIT) continue;
                if (M.wave(x, y) < 0) { safe_cells.push_back({x, y}); sx += cell_center(x); sy += cell_center(y); }
            }
        if (!safe_cells.empty()) { safe_x = sx / (i64)safe_cells.size(); safe_y = sy / (i64)safe_cells.size(); }
        else { safe_x = M.width() / 2; safe_y = M.height() / 2; }
    }

    bool standable_t(i64 x, i64 y, i64 t) const {
        const int c = cell_of_point(G->cfg, live, x, y, t);
        return c == C_FLOOR || c == C_GRASS;
    }

    // ---------- 延迟补偿：把观察里的旧状态推到"现在" ----------
    void build_est(const Obs& o, Game& g) {
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        for (int k = 0; k < o.n; ++k)
            if (k != o.seat && o.p[k].alive && o.p[k].vis) { memx[k] = o.p[k].x; memy[k] = o.p[k].y; }
        g = Game(gi, o);
        g.t = o.t;
        for (int k = 0; k < o.n; ++k) {
            if (k == o.seat) continue;
            if (g.p[k].alive && g.p[k].unknown) {
                g.p[k].unknown = 0;
                g.p[k].x = memx[k]; g.p[k].y = memy[k];
                g.p[k].px = g.p[k].x; g.p[k].py = g.p[k].y;
                g.p[k].vx = 0; g.p[k].vy = 0;
            }
        }
        Dart myd = g.d[o.seat];
        g.d[o.seat].st = D_GROUND;  // 补演期间冻结自己的镖，避免幻影命中
        g.d[o.seat].vx = g.d[o.seat].vy = 0;
        const int N = (int)imin(cf.delay, o.t);
        Action acts[MAXP];
        for (int s = 0; s < N; ++s) {
            for (int k = 0; k < o.n; ++k) {
                acts[k] = Action();
                if (k == o.seat || !g.p[k].alive) continue;
                const Player& P = g.p[k];
                if ((P.st == S_FREE || P.st == S_THROW || P.st == S_THROWN) && (P.vx || P.vy))
                    scale_to(P.vx, P.vy, 1000, acts[k].mx, acts[k].my);
            }
            g.over = false;
            g.step(acts);
        }
        g.p[o.seat] = o.me();
        g.p[o.seat].px = o.me().x; g.p[o.seat].py = o.me().y;
        g.p[o.seat].team = gi.team[o.seat];
        g.p[o.seat].unknown = 0;
        g.d[o.seat] = myd;
        g.t = o.t;
        g.over = false;
    }

    // ---------- 敌方镖的未来轨迹 ----------
    void dart_trace(const Game& g, int k, i64 me_x, i64 me_y, bool worst, Trace& out) const {
        const Config& cf = g.cf;
        const Dart& D = g.d[k];
        out.clear();
        out.push(D.x, D.y);
        if (!dart_flying(D.st)) return;
        i64 st = D.st, age = D.age, left = D.left;
        i64 px = D.x, py = D.y;
        const i64 dx = D.vx, dy = D.vy;
        const i64 ox = g.p[k].x, oy = g.p[k].y, ovx = g.p[k].vx, ovy = g.p[k].vy;
        for (int s = 1; s < TR; ++s) {
            i64 nx = px, ny = py;
            if (st == D_OUT) {
                i64 len = imax(cf.dart_vmin, cf.dart_v - age * cf.dart_dec);
                if (len > left) len = left;
                nx = px + rdiv(dx * len, 1000);
                ny = py + rdiv(dy * len, 1000);
                i64 hx, hy;
                if (seg_wall(live, px, py, nx, ny, hx, hy)) { out.push(hx, hy); return; }
                left -= len; ++age;
                if (left <= 0) { st = D_BACK; age = 0; }
            } else if (st == D_BACK || st == D_CALL) {
                if (st == D_BACK && age < cf.dart_hang) { ++age; }
                else {
                    const i64 len = (st == D_BACK) ? imin(cf.ret_v, cf.ret_v0 + (age - cf.dart_hang) * cf.ret_acc)
                                                   : imin(cf.recall_v, cf.ret_v0 + age * cf.ret_acc);
                    i64 tx = ox + ovx * s, ty = oy + ovy * s;
                    if (worst) {
                        i64 wx = me_x - ox, wy = me_y - oy;
                        const i64 wl = ilen(wx, wy);
                        const i64 adv = imin(cf.run_v * s, wl);
                        if (wl > 0) { tx = ox + rdiv(wx * adv, wl); ty = oy + rdiv(wy * adv, wl); }
                    }
                    const i64 dd = ilen(tx - px, ty - py);
                    if (dd <= len) { out.push(tx, ty); return; }
                    scale_to(tx - px, ty - py, len, nx, ny);
                    nx += px; ny += py;
                    if (st == D_BACK) {
                        i64 hx, hy;
                        if (seg_wall(live, px, py, nx, ny, hx, hy)) { out.push(hx, hy); return; }
                    }
                    ++age;
                }
            } else return;
            out.push(nx, ny);
            px = nx; py = ny;
        }
    }

    // ---------- 敌人运动假设（6 条） ----------
    void enemy_hyps(const Game& g, int k, i64 me_x, i64 me_y, Hyp* out) const {
        const Config& cf = g.cf;
        const Player& P = g.p[k];
        int lock = 0, kind = 0, pre_left = 0, move_left = 0;
        switch (P.st) {
            case S_THROW: case S_THROWN: kind = 2; lock = (int)P.timer; break;
            case S_SLASH: case S_SLASHED: case S_KICK: case S_KICKED: case S_RECALL: kind = 1; lock = (int)P.timer; break;
            case S_DASH: {
                kind = 3; lock = (int)P.timer;
                pre_left = (int)imax(0, P.timer - (cf.dash_t + cf.dash_post));
                move_left = (int)iclamp(P.timer - cf.dash_post, 0, cf.dash_t);
                break;
            }
            case S_STUN: kind = 4; lock = (int)P.timer; break;
            default: kind = 0; lock = 0; break;
        }
        const int react = (int)imax(5, lock);
        i64 bdx = P.vx, bdy = P.vy;
        if (!bdx && !bdy) { bdx = me_x - P.x; bdy = me_y - P.y; }
        i64 perp_x = 0, perp_y = 0;
        scale_to(-bdy, bdx, 1000, perp_x, perp_y);
        static const i64 W[6] = {42, 13, 13, 8, 12, 12};
        for (int h = 0; h < 6; ++h) {
            Hyp& H = out[h];
            H.w = W[h];
            H.tr.clear();
            i64 x = P.x, y = P.y, vx = P.vx, vy = P.vy;
            H.tr.push(x, y);
            for (int s = 1; s < TR; ++s) {
                if (s <= lock) {
                    if (kind == 1) { vx = 0; vy = 0; }
                    else if (kind == 2) {
                        if (vx || vy) scale_to(vx, vy, cf.run_v * cf.throw_move / 100, vx, vy);
                    } else if (kind == 3) {
                        if (s <= pre_left) { vx = 0; vy = 0; }
                        else if (s <= pre_left + move_left) scale_to(P.ax, P.ay, cf.dash_v, vx, vy);
                        else { vx = 0; vy = 0; }
                    }
                    move_body(live, cf.body_r, x, y, vx, vy);
                    if (kind == 4) { vx = vx * cf.kick_keep / 1000; vy = vy * cf.kick_keep / 1000; }
                } else {
                    i64 tx = 0, ty = 0;
                    if (h == 0 || s < react) {
                        if (vx || vy) scale_to(vx, vy, cf.run_v, tx, ty);
                        vx = tx; vy = ty;
                    } else if (h == 3) { vx = 0; vy = 0; }
                    else if (h == 1) scale_to(perp_x, perp_y, cf.run_v, vx, vy);
                    else if (h == 2) scale_to(-perp_x, -perp_y, cf.run_v, vx, vy);
                    else if (h == 4) scale_to(me_x - x, me_y - y, cf.run_v, vx, vy);
                    else scale_to(x - me_x, y - me_y, cf.run_v, vx, vy);
                    move_body(live, cf.body_r, x, y, vx, vy);
                }
                if (!standable_t(x, y, g.t + s)) {
                    x = H.tr.x[H.tr.n - 1]; y = H.tr.y[H.tr.n - 1]; vx = 0; vy = 0;
                }
                H.tr.push(x, y);
            }
        }
    }

    // ---------- 自己的移动轨迹（mode 0 = 跑，mode 1 = 冲刺） ----------
    int me_trace(const Obs& o, i64 x, i64 y, i64 vx, i64 vy, i64 mx, i64 my, int mode, int pct, Trace& out) const {
        const Config& cf = G->cfg;
        out.clear();
        out.push(x, y);
        int death = -1;
        const int pre = (int)cf.dash_pre, mv = (int)cf.dash_t, po = (int)cf.dash_post;
        for (int s = 1; s < TR; ++s) {
            bool pit_check = true;
            if (mode == 1) {
                if (s <= pre) { vx = 0; vy = 0; }
                else if (s <= pre + mv) {
                    if (mx || my) scale_to(mx, my, cf.dash_v, vx, vy); else { vx = 0; vy = 0; }
                    pit_check = (s == pre + mv);
                } else if (s <= pre + mv + po) { vx = 0; vy = 0; }
                else {
                    i64 tx = 0, ty = 0;
                    if (mx || my) scale_to(mx, my, cf.run_v, tx, ty);
                    i64 ddx = tx - vx, ddy = ty - vy;
                    if (ilen(ddx, ddy) > cf.acc) scale_to(ddx, ddy, cf.acc, ddx, ddy);
                    vx += ddx; vy += ddy;
                }
                move_body(live, cf.body_r, x, y, vx, vy);
            } else {
                i64 tx = 0, ty = 0;
                if (mx || my) scale_to(mx, my, cf.run_v * pct / 100, tx, ty);
                i64 ddx = tx - vx, ddy = ty - vy;
                if (ilen(ddx, ddy) > cf.acc) scale_to(ddx, ddy, cf.acc, ddx, ddy);
                vx += ddx; vy += ddy;
                move_body(live, cf.body_r, x, y, vx, vy);
            }
            out.push(x, y);
            if (death < 0 && pit_check && !standable_t(x, y, o.t + s)) death = s;
        }
        return death;
    }

    // ---------- 扔镖命中率评估 ----------
    i64 throw_eval(const Config& cf, i64 rx, i64 ry, i64 ax, i64 ay, const Hyp* hyps, int nh, int& hit_s) const {
        scale_to(ax, ay, 1000, ax, ay);   // 引擎会把出手方向归一化成长度 1000
        Trace dt;
        dt.push(rx, ry); dt.push(rx, ry); dt.push(rx, ry);
        i64 px = rx, py = ry;
        i64 left = cf.range, age = 0;
        for (int s = 3; s < TR; ++s) {
            i64 len = imax(cf.dart_vmin, cf.dart_v - age * cf.dart_dec);
            if (len > left) len = left;
            i64 nx = px + rdiv(ax * len, 1000), ny = py + rdiv(ay * len, 1000);
            i64 hx, hy;
            if (seg_wall(live, px, py, nx, ny, hx, hy)) { dt.push(hx, hy); break; }
            dt.push(nx, ny);
            px = nx; py = ny;
            left -= len; ++age;
            if (left <= 0) break;
        }
        const i64 R = cf.body_r + cf.dart_r;
        i64 sw = 0, swt = 0;
        for (int i = 0; i < nh; ++i) {
            swt += hyps[i].w;
            const Trace& hp = hyps[i].tr;
            const int lim = imin(dt.n, hp.n) - 1;
            for (int s = 2; s < lim; ++s) {
                if (seg_near_origin(dt.x[s] - hp.x[s], dt.y[s] - hp.y[s],
                                    dt.x[s + 1] - hp.x[s + 1], dt.y[s + 1] - hp.y[s + 1], R)) {
                    sw += hyps[i].w;
                    if (s < hit_s) hit_s = s;
                    break;
                }
            }
        }
        return swt ? rdiv(sw * 1000, swt) : 0;
    }

    // ---------- 主决策 ----------
    Action decide(const Obs& o) {
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        const Player& me = o.me();
        Action a;
        if (!me.alive) return a;
        live = live_map(cf, gi.M(), o.t);
        hs = hs * 6364136223846793005ULL + 1442695040888963407ULL;

        Game g;
        build_est(o, g);

        int eidx[8]; int ne = 0;
        for (int k = 0; k < o.n; ++k) if (k != o.seat && g.p[k].alive) eidx[ne++] = k;

        // 敌方镖轨迹（去程精确；回程 / 召回加"追我"的最坏情形）
        std::vector<Trace> threats;
        for (int i = 0; i < ne; ++i) {
            const Dart& D = g.d[eidx[i]];
            if (!dart_flying(D.st)) continue;
            Trace t1; dart_trace(g, eidx[i], me.x, me.y, false, t1);
            threats.push_back(t1);
            if (D.st != D_OUT) {
                Trace t2; dart_trace(g, eidx[i], me.x, me.y, true, t2);
                threats.push_back(t2);
            }
        }
        // 幻影镖：持镖敌人可能在盲窗（最近 4 步）里已经出手——假设 3 步前朝我扔了一镖
        for (int i = 0; i < ne; ++i) {
            const Dart& D = g.d[eidx[i]];
            if (D.st != D_HAND) continue;
            const Player& P = g.p[eidx[i]];
            if (P.st != S_FREE && P.st != S_THROW && P.st != S_THROWN) continue;
            const i64 ddx = me.x - P.x, ddy = me.y - P.y;
            const i64 dd = ilen(ddx, ddy);
            if (dd < 800 || dd > 7600) continue;
            Dart ph;
            ph.st = D_OUT;
            ph.vx = rdiv(ddx * 1000, dd);
            ph.vy = rdiv(ddy * 1000, dd);
            const i64 flown = 2550;   // 3 步：900+850+800
            ph.x = P.x + rdiv(ph.vx * flown, 1000);
            ph.y = P.y + rdiv(ph.vy * flown, 1000);
            ph.left = 7200 - flown;
            ph.age = 3;
            Game tmp = g;
            tmp.d[eidx[i]] = ph;
            Trace t1; dart_trace(tmp, eidx[i], me.x, me.y, false, t1);
            threats.push_back(t1);
        }

        const Dart& md = o.my_dart();
        const bool have_dart = (md.st == D_HAND);
        const bool dart_ground = (md.st == D_GROUND);
        const i64 hitR = cf.body_r + cf.dart_r;

        auto trace_clear = [&](const Trace& tr, int steps) -> i64 {
            i64 best = (i64)1 << 60;
            for (size_t i = 0; i < threats.size(); ++i) {
                const Trace& dt = threats[i];
                const int lim = imin(imin(tr.n, dt.n) - 1, steps);
                for (int s = 0; s < lim; ++s) {
                    const i64 d = seg_dist_origin(dt.x[s] - tr.x[s], dt.y[s] - tr.y[s],
                                                  dt.x[s + 1] - tr.x[s + 1], dt.y[s + 1] - tr.y[s + 1]);
                    if (d < best) best = d;
                }
            }
            return best;
        };
        Trace stay_tr;
        for (int s = 0; s < TR; ++s) stay_tr.push(me.x, me.y);
        const i64 stay_clear = trace_clear(stay_tr, TR);

        Hyp hyps[8][6];
        for (int i = 0; i < ne; ++i) enemy_hyps(g, eidx[i], me.x, me.y, hyps[i]);

        // ============ 冲刺中：只考虑接接近战 ============
        if (me.st == S_DASH) {
            if (cf.dash_slash && me.timer >= cf.dash_post + 1 && me.timer <= cf.dash_t + cf.dash_post) {
                const i64 done = cf.dash_t + cf.dash_post - me.timer;
                i64 jx = me.x, jy = me.y;
                if (done >= 1) { jx += rdiv(me.ax * cf.dash_v, 1000); jy += rdiv(me.ay * cf.dash_v, 1000); }
                int bestk = -1; i64 bd = (i64)1 << 60; i64 bx = 0, by = 0;
                for (int i = 0; i < ne; ++i) {
                    const i64 ex = hyps[i][0].tr.x[2], ey = hyps[i][0].tr.y[2];
                    const i64 dx = ex - jx, dy = ey - jy;
                    const i64 dd = ilen(dx, dy);
                    if (dd > cf.slash_reach - 100 || dd == 0) continue;
                    if (wall_between(live, jx, jy, ex, ey)) continue;
                    if (dd < bd) { bd = dd; bestk = i; bx = ex; by = ey; }
                }
                if (bestk >= 0) {
                    a.act = A_MELEE;
                    a.ax = bx - jx; a.ay = by - jy;
                    return a;
                }
            }
            return a;
        }

        // ============ 召回引导中：危险就中断 ============
        bool recall_lock = false;
        if (me.st == S_RECALL) {
            bool danger = (stay_clear < 1200);
            for (int i = 0; i < ne && !danger; ++i) {
                const i64 dd = ilen(g.p[eidx[i]].x - me.x, g.p[eidx[i]].y - me.y);
                if (dd < 2800) danger = true;
            }
            if (!danger) { a.act = A_RECALL; return a; }
            recall_lock = true;
        }

        // ============ 目标与目标点 ============
        int tgt = -1; i64 tsc = (i64)1 << 60;
        for (int i = 0; i < ne; ++i) {
            const Player& P = g.p[eidx[i]];
            const i64 d = ilen(P.x - me.x, P.y - me.y);
            const i64 sc = d + (g.d[eidx[i]].st == D_HAND ? 0 : 1200) + (o.p[eidx[i]].vis ? 0 : 2600);
            if (sc < tsc) { tsc = sc; tgt = i; }
        }

        i64 gx = safe_x, gy = safe_y;
        if (dart_ground) { gx = md.x; gy = md.y; }
        else if (tgt >= 0) {
            const Player& P = g.p[eidx[tgt]];
            const i64 dx = P.x - me.x, dy = P.y - me.y;
            const i64 d = ilen(dx, dy);
            i64 want = 5000;
            if (g.d[eidx[tgt]].st != D_HAND) want = 3300;
            if (g.p[eidx[tgt]].st != S_FREE) want = 3000;
            if (o.t > cf.cstart() - 40) want = 2300;
            if (d > 0) { gx = P.x - rdiv(dx * want, d); gy = P.y - rdiv(dy * want, d); }
            const i64 ov = 1;   // 环绕方向整局固定（逐步翻转会让横向位移互相抵消）
            i64 tx = -dy, ty = dx;
            const i64 l = ilen(tx, ty);
            if (l > 0) { gx += rdiv(tx * ov * 1100, l); gy += rdiv(ty * ov * 1100, l); }
        }
        {   // 塌陷压力：脚下快塌 → 去最近的永不塌格
            const i64 fall = cell_fall_step(gi, cell_index(me.x), cell_index(me.y));
            if (fall >= 0 && fall < o.t + 55 && !safe_cells.empty()) {
                i64 bd = (i64)1 << 60;
                for (size_t i = 0; i < safe_cells.size(); ++i) {
                    const i64 px = cell_center(safe_cells[i].first), py = cell_center(safe_cells[i].second);
                    const i64 dd = ilen(px - me.x, py - me.y);
                    if (dd < bd) { bd = dd; gx = px; gy = py; }
                }
            }
        }
        {
            i64 wx, wy;
            if (path_next(gi, me.x, me.y, gx, gy, o.t, wx, wy)) { gx = wx; gy = wy; }
        }

        // ============ 移动候选评分 ============
        struct Cand { i64 mx, my; int mode; i64 score; i64 clear; int death; };
        Cand cands[28]; int nc = 0;
        const int pct = (me.st == S_THROW || me.st == S_THROWN) ? 50 : 100;
        auto score_cand = [&](Cand& c, const Trace& tr) {
            c.clear = trace_clear(tr, TR);
            i64 sc = 0;
            const i64 dw = (stay_clear < 1800) ? 5 : 2;   // 有镖逼近时更看重净空
            if (c.death >= 0) sc -= 500000;
            if (c.clear < hitR) sc -= 200000 + c.clear;
            else sc += imin(c.clear, 3000) * dw;
            sc -= ilen(tr.x[6] - gx, tr.y[6] - gy);
            sc -= ilen(tr.x[12] - gx, tr.y[12] - gy) / 2;
            if (c.mode == 0 && (c.mx || c.my)) {
                for (int i = 0; i < ne; ++i) {
                    if (g.d[eidx[i]].st != D_HAND) continue;
                    const i64 ex = g.p[eidx[i]].x - me.x, ey = g.p[eidx[i]].y - me.y;
                    const i64 el = ilen(ex, ey);
                    if (el == 0 || el > 6500) continue;
                    const i64 dot = (c.mx * ex + c.my * ey) / el;
                    sc -= iabs(dot) / 2;            // 平行于持镖敌人 = 容易被预判
                    if (wall_between(live, tr.x[6], tr.y[6], g.p[eidx[i]].x, g.p[eidx[i]].y)) sc += 120;
                }
                if (cell_of_point(cf, live, tr.x[6], tr.y[6], o.t + 6) == C_GRASS) sc += 80;
            }
            c.score = sc;
        };
        for (int i = 0; i <= 16; ++i) {
            Cand c;
            if (i < 16) unit_dir(i * 2250, c.mx, c.my);
            else { c.mx = 0; c.my = 0; }
            c.mode = 0;
            Trace tr;
            c.death = me_trace(o, me.x, me.y, me.vx, me.vy, c.mx, c.my, 0, pct, tr);
            score_cand(c, tr);
            cands[nc++] = c;
        }
        bool dash_ok = (me.st == S_FREE && me.cd == 0 && !recall_lock);
        if (dash_ok) {
            for (int i = 0; i < 8; ++i) {
                Cand c;
                unit_dir(i * 4500, c.mx, c.my);
                c.mode = 1;
                Trace tr;
                c.death = me_trace(o, me.x, me.y, me.vx, me.vy, c.mx, c.my, 1, 100, tr);
                score_cand(c, tr);
                c.score -= 2600;                    // 冲刺的代价（冷却 + 前摇收势站桩）
                cands[nc++] = c;
            }
        }
        int bi = 0;
        for (int i = 1; i < nc; ++i) if (cands[i].score > cands[bi].score) bi = i;
        a.mx = cands[bi].mx; a.my = cands[bi].my;
        if (cands[bi].mode == 1 && dash_ok) { a.act = A_DASH; a.ax = cands[bi].mx; a.ay = cands[bi].my; }

        if (me.st != S_FREE && me.st != S_THROW && me.st != S_THROWN && me.st != S_RECALL) {
            // 其他硬直：动作会被忽略，只给移动方向
            a.act = 0;
            return a;
        }
        if (me.st == S_THROW || me.st == S_THROWN) { a.act = 0; return a; }

        // ============ 动作选择（free） ============
        // 1) 踢（空手）：能踢下坑 / 打断召回蓄势冲刺 / 终局近战
        if (!have_dart && !recall_lock) {
            for (int i = 0; i < ne && a.act == 0; ++i) {
                const i64 ex = hyps[i][0].tr.x[2], ey = hyps[i][0].tr.y[2];
                const i64 dx = ex - me.x, dy = ey - me.y;
                const i64 dd = ilen(dx, dy);
                if (dd > cf.kick_reach - 100 || dd == 0) continue;
                if (wall_between(live, me.x, me.y, ex, ey)) continue;
                i64 sdx, sdy;
                scale_to(dx, dy, 1000, sdx, sdy);
                i64 lx = ex, ly = ey, lvx = sdx * cf.kick_v / 1000, lvy = sdy * cf.kick_v / 1000;
                for (int s = 0; s < cf.kick_stun; ++s) {
                    move_body(live, cf.body_r, lx, ly, lvx, lvy);
                    lvx = lvx * cf.kick_keep / 1000; lvy = lvy * cf.kick_keep / 1000;
                }
                const bool pit = !standable_t(lx, ly, o.t + 2 + cf.kick_stun);
                const Player& P = g.p[eidx[i]];
                bool good = pit;
                if (P.st == S_RECALL || P.st == S_THROW || P.st == S_DASH || P.st == S_STUN) good = true;
                if (o.t > cf.cstart()) good = true;
                if (good && !pit && stay_clear < hitR) good = false;
                if (good) {
                    a.act = A_MELEE; a.ax = dx; a.ay = dy;
                    a.mx = a.my = 0;
                }
            }
        }

        // 2) 挥砍（持镖）：砍人 + 挡镖
        if (have_dart && a.act == 0 && !recall_lock) {
            for (int i = 0; i < ne && a.act == 0; ++i) {
                const i64 ex = hyps[i][0].tr.x[2], ey = hyps[i][0].tr.y[2];
                const i64 dx = ex - me.x, dy = ey - me.y;
                const i64 dd = ilen(dx, dy);
                if (dd > cf.slash_reach - 100 || dd == 0) continue;
                if (wall_between(live, me.x, me.y, ex, ey)) continue;
                i64 sdx, sdy;
                scale_to(dx, dy, 1000, sdx, sdy);
                if (dd > cf.body_r && dx * sdx + dy * sdy < cf.slash_cos * dd) continue;
                bool safe = (stay_clear >= hitR + 150);
                bool desperate = (dd < 1200);
                if (safe || desperate) {
                    a.act = A_MELEE; a.ax = dx; a.ay = dy;
                    a.mx = a.my = 0;
                }
            }
            // 挡镖救命：镖一步后进入正前方 1500 内、躲不开
            if (a.act == 0 && stay_clear < hitR + 120) {
                for (size_t i = 0; i < threats.size() && a.act == 0; ++i) {
                    const i64 dx = threats[i].x[1] - me.x, dy = threats[i].y[1] - me.y;
                    const i64 dd = ilen(dx, dy);
                    if (dd == 0 || dd > cf.slash_reach) continue;
                    i64 sdx, sdy;
                    scale_to(dx, dy, 1000, sdx, sdy);
                    if (dd > cf.body_r && dx * sdx + dy * sdy < cf.block_cos * dd) continue;
                    if (wall_between(live, me.x, me.y, threats[i].x[1], threats[i].y[1])) continue;
                    a.act = A_MELEE; a.ax = dx; a.ay = dy;
                    a.mx = a.my = 0;
                }
            }
        }

        // 3) 扔镖
        if (have_dart && a.act == 0 && !recall_lock) {
            Trace mtr;
            me_trace(o, me.x, me.y, me.vx, me.vy, a.mx, a.my, 0, 50, mtr);
            const i64 rx = mtr.x[2], ry = mtr.y[2];
            i64 best_sc = 0, bax = 0, bay = 0;
            for (int i = 0; i < ne; ++i) {
                if (!o.p[eidx[i]].vis) continue;
                const i64 d = ilen(g.p[eidx[i]].x - rx, g.p[eidx[i]].y - ry);
                if (d > 7100 || d < 800) continue;
                int fs = 0; i64 acc2 = 0;
                while (acc2 < d && fs < 15) { acc2 += imax(cf.dart_vmin, cf.dart_v - fs * cf.dart_dec); ++fs; }
                const int arrive = 2 + fs;
                for (int hi = 0; hi < 6; ++hi) {           // 每条假设的到达点都试
                    const int ss = (int)iclamp(arrive, 3, TR - 1);
                    i64 ax = hyps[i][hi].tr.x[ss] - rx, ay = hyps[i][hi].tr.y[ss] - ry;
                    if (!ax && !ay) continue;
                    int hit_s = 99;
                    const i64 sc = throw_eval(cf, rx, ry, ax, ay, hyps[i], 6, hit_s);
                    if (sc > best_sc) { best_sc = sc; bax = ax; bay = ay; }
                }
                for (int q = -1; q <= 1; q += 2) {         // H0 早到/晚到
                    const int ss = (int)iclamp(arrive + q, 3, TR - 1);
                    i64 ax = hyps[i][0].tr.x[ss] - rx, ay = hyps[i][0].tr.y[ss] - ry;
                    if (!ax && !ay) continue;
                    int hit_s = 99;
                    const i64 sc = throw_eval(cf, rx, ry, ax, ay, hyps[i], 6, hit_s);
                    if (sc > best_sc) { best_sc = sc; bax = ax; bay = ay; }
                }
            }
            i64 thr = 430;
            if (tgt >= 0 && g.p[eidx[tgt]].st != S_FREE) thr = 320;
            if (stay_clear < hitR - 300 && (tgt < 0 || g.p[eidx[tgt]].st == S_FREE)) thr = 900;  // 马上挨镖且目标会动：先躲
            if (best_sc >= thr) {
                a.act = A_THROW; a.ax = bax; a.ay = bay;
            }
        }

        // 4) 召回（镖在地上）
        if (dart_ground && a.act == 0 && !recall_lock && me.st == S_FREE) {
            const i64 dd = ilen(md.x - me.x, md.y - me.y);
            const bool unreachable = !standable_t(md.x, md.y, o.t);
            bool quiet = (stay_clear >= 1400);
            for (int i = 0; i < ne && quiet; ++i) {
                const i64 ed = ilen(g.p[eidx[i]].x - me.x, g.p[eidx[i]].y - me.y);
                if (ed < 4200) quiet = false;
            }
            bool hidden = false;
            if (cell_of_point(cf, live, me.x, me.y, o.t) == C_GRASS) {
                hidden = true;
                for (int i = 0; i < ne; ++i) {
                    const i64 ed = ilen(g.p[eidx[i]].x - me.x, g.p[eidx[i]].y - me.y);
                    if (ed < 3600) hidden = false;
                }
            }
            if ((quiet && (dd > 1600 || unreachable)) || (hidden && dd > 1200)) {
                a.act = A_RECALL;
                a.mx = a.my = 0;
            }
        }

        // 5) 战术冲刺：接近战 / 捡镖 / 逃向安全区
        if (dash_ok && a.act == 0 && me.st == S_FREE) {
            const i64 stat_clear = stay_clear;
            if (have_dart && cf.dash_slash && tgt >= 0 && stat_clear > hitR + 200) {
                const i64 ex = hyps[tgt][0].tr.x[5], ey = hyps[tgt][0].tr.y[5];
                const i64 d0 = ilen(ex - me.x, ey - me.y);
                if (d0 >= 1500 && d0 <= 4300) {
                    i64 dx, dy;
                    scale_to(ex - me.x, ey - me.y, 1000, dx, dy);
                    const i64 lx = me.x + dx * 3, ly = me.y + dy * 3;   // 约 3000
                    if (standable_t(lx, ly, o.t + 8) && !circle_hits_wall(live, cf.body_r, lx, ly)) {
                        a.act = A_DASH; a.ax = dx; a.ay = dy;
                        a.mx = dx; a.my = dy;
                    }
                }
            }
            if (a.act == 0 && dart_ground && stat_clear > hitR + 200) {
                const i64 dd = ilen(md.x - me.x, md.y - me.y);
                if (dd > 2200 && dd < 6500) {
                    i64 dx, dy;
                    scale_to(md.x - me.x, md.y - me.y, 1000, dx, dy);
                    const i64 lx = me.x + dx * 3, ly = me.y + dy * 3;
                    if (standable_t(lx, ly, o.t + 8) && !circle_hits_wall(live, cf.body_r, lx, ly)) {
                        a.act = A_DASH; a.ax = dx; a.ay = dy;
                        a.mx = dx; a.my = dy;
                    }
                }
            }
        }
        if (a.act == A_MELEE || a.act == A_THROW || a.act == A_RECALL) {
            // 出手时移动已在前面给过（踢/砍站桩）
        }
        return a;
    }
};

}  // namespace

DART_MAIN(Bot)
