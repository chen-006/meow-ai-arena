// 飞镖乱斗：延迟补偿 + 反应延迟下的短视野推演。
// 自己的历史动作从 4 步前的真实局面滚到现在；出手是否命中交给引擎结算。
#include "dart.hpp"

#include <algorithm>
#include <climits>
#include <cstring>

using namespace dart;

namespace {

constexpr int HCAP = 64;
constexpr int MAXC = 28;
constexpr i64 HIT_BIAS = 80;  // 判定半径外再留一点，避免贴边被扫到

int ring(i64 t) { return int(t % HCAP); }

i64 spd_len(i64 x, i64 y) { return ilen(x, y); }

Action move_of(i64 x, i64 y) {
    Action a;
    if ((x || y) && !scale_to(x, y, 1000, a.mx, a.my)) a.mx = a.my = 0;
    return a;
}

Action make_act(i64 mx, i64 my, i64 act, i64 ax, i64 ay) {
    Action a = move_of(mx, my);
    a.act = act;
    a.ax = ax;
    a.ay = ay;
    return a;
}

bool team_of(const Game& g, int a, int b) { return a != b && g.p[a].team == g.p[b].team; }

int locked_for(const Player& P, const Config& c) {
    switch (P.st) {
    case S_FREE: return 0;
    case S_THROW: return int(P.timer + c.throw_post);
    case S_THROWN:
    case S_SLASHED:
    case S_KICKED:
    case S_STUN:
    case S_RECALL: return int(P.timer);
    case S_SLASH: return int(P.timer + c.slash_post);
    case S_KICK: return int(P.timer + c.kick_post);
    case S_DASH: return int(P.timer);
    default: return 0;
    }
}

bool dash_move_phase(const Player& P, const Config& c) {
    return P.st == S_DASH && P.timer > c.dash_post && P.timer <= c.dash_t + c.dash_post;
}

int fly_steps(const Config& c, i64 dist) {
    i64 left = dist;
    i64 age = 0;
    int s = 0;
    while (left > 0 && s < 18) {
        i64 sp = imax(c.dart_vmin, c.dart_v - age * c.dart_dec);
        if (sp < 1) sp = 1;
        left -= sp;
        ++age;
        ++s;
    }
    return s;
}

i64 seg_dist2(i64 x0, i64 y0, i64 x1, i64 y1, i64 px, i64 py) {
    i64 vx = x1 - x0, vy = y1 - y0;
    i64 wx = px - x0, wy = py - y0;
    i64 c2 = vx * vx + vy * vy;
    if (c2 <= 0) {
        i64 d = spd_len(wx, wy);
        return d * d;
    }
    i64 c1 = vx * wx + vy * wy;
    if (c1 <= 0) {
        i64 d = spd_len(wx, wy);
        return d * d;
    }
    if (c1 >= c2) {
        i64 d = spd_len(px - x1, py - y1);
        return d * d;
    }
    i64 qx = x0 + rdiv(vx * c1, c2);
    i64 qy = y0 + rdiv(vy * c1, c2);
    i64 d = spd_len(px - qx, py - qy);
    return d * d;
}

struct React {
    int throw_step[MAXP];
    React() {
        for (int i = 0; i < MAXP; ++i) throw_step[i] = -1;
    }
};

struct Hist {
    i64 t = -1;
    bool ok = false;
    bool acted = false;
    Player p;
    Dart d;
    Action a;
};

}  // namespace

struct Bot {
    Hist hist[HCAP];
    int seat = 0;
    int n = 0;
    i64 cpu0 = 0;
    i64 safe_x = 0, safe_y = 0;
    Map liv;
    bool liv_ok = false;

    std::vector<int> nav;
    int nav_nx = 0, nav_ny = 0;
    i64 nav_gx = 0, nav_gy = 0;
    bool nav_ok = false;

    struct Mem {
        bool ok = false;
        i64 t = 0, x = 0, y = 0, vx = 0, vy = 0;
        i64 dart_st = 0;
    } mem[MAXP];

    i64 budget = 70000;  // 100ns 单位，约 7ms，裁判上限 10ms

    bool time_up() const {
        i64 now = thread_cpu_100ns();
        if (cpu0 < 0 || now < 0) return false;
        return now - cpu0 >= budget;
    }

    void init(const GameInfo& gi) {
        seat = gi.seat;
        n = gi.n;
        std::memset(hist, 0, sizeof hist);
        i64 best = LLONG_MIN;
        const Map& M = gi.M();
        i64 cx = M.width() / 2, cy = M.height() / 2;
        safe_x = cx;
        safe_y = cy;
        for (int y = 0; y < M.ny; ++y)
            for (int x = 0; x < M.nx; ++x) {
                int c = M.at(x, y);
                if (c != C_FLOOR && c != C_GRASS) continue;
                i64 fall = cell_fall_step(gi, x, y);
                i64 when = fall < 0 ? 1000000 : fall;
                i64 px = cell_center(x), py = cell_center(y);
                i64 sc = when * 10000 - spd_len(px - cx, py - cy);
                if (sc > best) {
                    best = sc;
                    safe_x = px;
                    safe_y = py;
                }
            }
    }

    bool place_ok(i64 x, i64 y, i64 t) const {
        if (!standable(info(), x, y, t)) return false;
        if (liv_ok && circle_hits_wall(liv, info().cfg.body_r, x, y)) return false;
        return true;
    }

    const Map& LM() const { return liv_ok ? liv : info().M(); }

    bool blocked(i64 x0, i64 y0, i64 x1, i64 y1) const { return wall_between(LM(), x0, y0, x1, y1); }

    int nearest_foe(const Game& g, int k) const {
        int best = -1;
        i64 bd = 0;
        for (int q = 0; q < g.n; ++q) {
            if (q == k || !g.p[q].alive || g.p[q].unknown) continue;
            if (team_of(g, k, q)) continue;
            i64 d = spd_len(g.p[q].x - g.p[k].x, g.p[q].y - g.p[k].y);
            if (best < 0 || d < bd) {
                best = q;
                bd = d;
            }
        }
        return best;
    }

    int pick_target(const Game& g, int k) const {
        int best = -1;
        i64 bs = LLONG_MIN;
        for (int q = 0; q < g.n; ++q) {
            if (q == k || !g.p[q].alive || g.p[q].unknown) continue;
            if (team_of(g, k, q)) continue;
            i64 d = spd_len(g.p[q].x - g.p[k].x, g.p[q].y - g.p[k].y);
            i64 sc = -d;
            if (g.d[q].st != D_HAND) sc += 2800;
            if (locked_for(g.p[q], g.cf) >= 3) sc += 1800;
            if (g.d[q].st == D_HAND && d < 5200) sc += 1600;
            if (g.d[q].st == D_GROUND || g.d[q].st == D_RISE) sc += 800;
            if (g.cf.mode == M_FFA) {
                for (int r = 0; r < g.n; ++r) {
                    if (r == k || r == q || !g.p[r].alive || g.p[r].unknown) continue;
                    if (team_of(g, q, r)) continue;
                    i64 dr = spd_len(g.p[r].x - g.p[q].x, g.p[r].y - g.p[q].y);
                    if (dr + 400 < d) sc -= 1200;  // 别人比我更近，让他们先打
                }
            }
            if (sc > bs) {
                bs = sc;
                best = q;
            }
        }
        return best;
    }

    void aim_lead(const Game& g, int A, int T, i64& ax, i64& ay) const {
        const Player& a = g.p[A];
        const Player& e = g.p[T];
        i64 dx = e.x - a.x, dy = e.y - a.y;
        i64 dist = imax(1, spd_len(dx, dy));
        int ft = int(g.cf.throw_pre) + fly_steps(g.cf, dist);
        // 速度来自延迟估计，全额预瞄容易甩过急停。取六成。
        i64 evx = e.vx * 3 / 5, evy = e.vy * 3 / 5;
        i64 px = e.x + evx * ft;
        i64 py = e.y + evy * ft;
        if (blocked(a.x, a.y, px, py)) {
            ax = dx;
            ay = dy;
        } else {
            ax = px - a.x;
            ay = py - a.y;
            i64 dist2 = imax(1, spd_len(ax, ay));
            int ft2 = int(g.cf.throw_pre) + fly_steps(g.cf, dist2);
            px = e.x + evx * ft2;
            py = e.y + evy * ft2;
            if (!blocked(a.x, a.y, px, py)) {
                ax = px - a.x;
                ay = py - a.y;
            }
        }
    }

    bool mate_on_ray(const Game& g, int A, i64 ax, i64 ay) const {
        i64 ux, uy;
        if (!scale_to(ax, ay, g.cf.range, ux, uy)) return false;
        i64 x0 = g.p[A].x, y0 = g.p[A].y;
        i64 x1 = x0 + ux, y1 = y0 + uy;
        const i64 lim = (g.cf.body_r + g.cf.dart_r + 120);
        for (int q = 0; q < g.n; ++q) {
            if (!team_of(g, A, q) || !g.p[q].alive || g.p[q].unknown) continue;
            if (seg_dist2(x0, y0, x1, y1, g.p[q].x, g.p[q].y) <= lim * lim) return true;
        }
        return false;
    }

    bool want_throw(const Game& g, int A, int T) const {
        if (g.d[A].st != D_HAND) return false;
        const Player& P = g.p[A];
        if (P.st != S_FREE) return false;
        if (T < 0 || !g.p[T].alive || g.p[T].unknown || team_of(g, A, T)) return false;
        i64 dx = g.p[T].x - P.x, dy = g.p[T].y - P.y;
        i64 dist = spd_len(dx, dy);
        if (dist < 1500 || dist > 6900) return false;
        if (blocked(P.x, P.y, g.p[T].x, g.p[T].y)) return false;
        i64 ax, ay;
        aim_lead(g, A, T, ax, ay);
        if (mate_on_ray(g, A, ax, ay)) return false;
        int lk = locked_for(g.p[T], g.cf);
        bool hd = g.d[T].st == D_HAND;
        // 多人局里有别人比我更贴着他，他在忙别的架，偷袭可以打得更远。
        bool busy = false;
        if (g.n > 2) {
            for (int q = 0; q < g.n; ++q) {
                if (q == A || q == T || !g.p[q].alive || g.p[q].unknown || team_of(g, A, q)) continue;
                i64 dq = spd_len(g.p[q].x - g.p[T].x, g.p[q].y - g.p[T].y);
                if (dq < dist && dq < 4200) busy = true;
            }
        }
        if (busy && dist <= 5400) return true;
        // 被硬直锁住的人躲不了，可以打得更远；自由且手里有镖的人大约 4 米外能格挡。
        if (lk >= 2 && dist <= 5600) return true;
        if (lk >= 5) return dist <= 6600;
        if (!hd) return dist <= 4800;
        return dist <= 3900;
    }

    bool melee_hits_mate(const Game& g, int A, i64 ax, i64 ay, bool slash) const {
        const Config& c = g.cf;
        i64 reach = slash ? c.slash_reach : c.kick_reach;
        i64 cs = slash ? c.slash_cos : c.kick_cos;
        i64 ux, uy;
        if (!scale_to(ax, ay, 1000, ux, uy)) return false;
        for (int q = 0; q < g.n; ++q) {
            if (!team_of(g, A, q) || !g.p[q].alive || g.p[q].unknown) continue;
            i64 dx = g.p[q].x - g.p[A].x, dy = g.p[q].y - g.p[A].y;
            i64 L = spd_len(dx, dy);
            if (L > reach) continue;
            if (L > c.body_r && (dx * ux + dy * uy) * 1000 < cs * L * 1000) continue;
            if (blocked(g.p[A].x, g.p[A].y, g.p[q].x, g.p[q].y)) continue;
            return true;
        }
        return false;
    }

    bool kick_into_pit(const Game& g, int A, int T) const {
        if (T < 0 || g.d[A].st == D_HAND) return false;
        const Player& e = g.p[T];
        i64 dx = e.x - g.p[A].x, dy = e.y - g.p[A].y;
        i64 L = spd_len(dx, dy);
        if (L <= 0 || L > g.cf.kick_reach) return false;
        if (blocked(g.p[A].x, g.p[A].y, e.x, e.y)) return false;
        i64 travel = 2797;
        i64 lx = e.x + rdiv(dx * travel, L);
        i64 ly = e.y + rdiv(dy * travel, L);
        i64 hx, hy;
        if (seg_wall(LM(), e.x, e.y, lx, ly, hx, hy)) {
            lx = hx;
            ly = hy;
        }
        i64 ft = g.t + g.cf.kick_pre + g.cf.kick_stun;
        return !standable(info(), lx, ly, ft);
    }

    // 近战交不交得出去。自由目标两步前摇能走出约 500，延迟外推还会再偏一截。
    // 手里有镖还自由的人，挥空就是把后摇送给对方。
    bool melee_commit(const Game& g, int A, int T) const {
        if (T < 0 || !g.p[T].alive || g.p[T].unknown || team_of(g, A, T)) return false;
        const Player& P = g.p[A];
        const Player& E = g.p[T];
        bool slash = g.d[A].st == D_HAND;
        i64 dx = E.x - P.x, dy = E.y - P.y;
        i64 L = spd_len(dx, dy);
        if (L <= 0 || blocked(P.x, P.y, E.x, E.y)) return false;
        if (melee_hits_mate(g, A, dx, dy, slash)) return false;
        const Config& c = g.cf;
        i64 reach = slash ? c.slash_reach : c.kick_reach;
        int lk = locked_for(E, c);
        if (!slash) {
            if (L > reach) return false;
            if (kick_into_pit(g, A, T)) return true;
            bool interrupt = E.st == S_RECALL || E.st == S_THROW || E.st == S_SLASH || E.st == S_KICK;
            if (interrupt && L <= reach - 80) return true;
            if (lk >= 2 && L <= reach - 80) return true;
            return L <= c.body_r + 160;
        }
        if (lk >= c.slash_pre) return L <= reach - 60;
        bool armed = g.d[T].st == D_HAND;
        if (armed && lk <= 1) return L <= c.body_r + 160;
        i64 toward = -(E.vx * dx + E.vy * dy);
        i64 margin = toward > 180 * L ? 280 : 560;
        return L <= reach - margin;
    }

    i64 nearest_flying(const Game& g, int k) const {
        i64 best = 100000000;
        for (int q = 0; q < g.n; ++q) {
            if (q == k || !dart_flying(g.d[q].st)) continue;
            i64 d = spd_len(g.d[q].x - g.p[k].x, g.d[q].y - g.p[k].y);
            if (d < best) best = d;
        }
        return best;
    }

    bool dash_ok(i64 x, i64 y, i64 ax, i64 ay, i64 t) const {
        const Config& c = info().cfg;
        i64 ux, uy;
        if (!scale_to(ax, ay, 1000, ux, uy)) return false;
        if (!place_ok(x, y, t) || !place_ok(x, y, t + c.dash_pre)) return false;
        i64 cx = x, cy = y;
        for (int i = 0; i < int(c.dash_t); ++i) {
            i64 vx = rdiv(ux * c.dash_v, 1000);
            i64 vy = rdiv(uy * c.dash_v, 1000);
            move_body(LM(), c.body_r, cx, cy, vx, vy);
            bool last = i + 1 == int(c.dash_t);
            i64 tt = t + c.dash_pre + i;
            if (last) {
                if (!place_ok(cx, cy, tt)) return false;
                int gx = cell_index(cx), gy = cell_index(cy);
                i64 f = cell_fall_step(info(), gx, gy);
                if (f >= 0 && f <= tt + 12) return false;
            }
        }
        return true;
    }

    struct Pred {
        int owner = -1;
        bool on = false;
        i64 x[12]{}, y[12]{};
        int n = 0;
    };

    bool dart_known(const React& r, int owner, int viewer, int sim_step) const {
        if (owner == viewer) return true;
        int ts = r.throw_step[owner];
        if (ts <= -100) return true;
        if (ts < 0) return true;
        return sim_step >= ts + int(info().cfg.delay);
    }

    void predict_darts(const Game& g, int viewer, const React& r, int sim_step, Pred* out, int& nout) const {
        nout = 0;
        const Config& c = g.cf;
        for (int k = 0; k < g.n; ++k) {
            if (k == viewer) continue;
            if (!dart_flying(g.d[k].st)) continue;
            if (!dart_known(r, k, viewer, sim_step)) continue;
            if (nout >= MAXP) break;
            Pred& P = out[nout++];
            P.owner = k;
            P.on = true;
            P.n = 0;
            const Dart& D = g.d[k];
            i64 x = D.x, y = D.y;
            i64 left = D.left, age = D.age;
            bool back = D.st != D_OUT;
            int hang = 0;
            if (D.st == D_BACK && age < c.dart_hang) hang = int(c.dart_hang - age);
            i64 ux = D.vx, uy = D.vy;
            for (int s = 0; s < 10; ++s) {
                P.x[P.n] = x;
                P.y[P.n] = y;
                ++P.n;
                i64 nx = x, ny = y;
                if (!back) {
                    i64 spd = imax(c.dart_vmin, c.dart_v - age * c.dart_dec);
                    i64 m = imin(spd, left);
                    nx = x + rdiv(ux * m, 1000);
                    ny = y + rdiv(uy * m, 1000);
                    left -= m;
                    ++age;
                    if (left <= 0) {
                        back = true;
                        age = 0;
                        hang = int(c.dart_hang);
                    }
                } else if (hang > 0) {
                    --hang;
                } else {
                    i64 ox = g.p[k].x + g.p[k].vx * (s + 1);
                    i64 oy = g.p[k].y + g.p[k].vy * (s + 1);
                    i64 gx = ox - x, gy = oy - y;
                    i64 gl = spd_len(gx, gy);
                    i64 spd = imin(c.ret_v, c.ret_v0 + age * c.ret_acc);
                    ++age;
                    if (gl <= spd) {
                        nx = ox;
                        ny = oy;
                    } else {
                        nx = x + rdiv(gx * spd, gl);
                        ny = y + rdiv(gy * spd, gl);
                    }
                }
                i64 hx, hy;
                if (D.st != D_CALL && seg_wall(LM(), x, y, nx, ny, hx, hy)) break;
                x = nx;
                y = ny;
            }
        }
    }

    // 选一个能活过已知飞镖、并靠近目标的移动方向。
    Action steer(const Game& g, int k, i64 goalx, i64 goaly, const React& r, int sim_step) const {
        const Player& P = g.p[k];
        const Config& c = g.cf;
        Pred preds[MAXP];
        int np = 0;
        predict_darts(g, k, r, sim_step, preds, np);
        const i64 R = c.body_r + c.dart_r + (k == seat ? HIT_BIAS : 0);
        static const i64 DX[16] = {1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383, 0, 383, 707, 924};
        static const i64 DY[16] = {0, 383, 707, 924, 1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383};
        i64 best = LLONG_MIN;
        Action best_a;
        int pct = (P.st == S_THROW || P.st == S_THROWN) ? int(c.throw_move) : 100;
        auto consider = [&](i64 mx, i64 my) {
            i64 tx = 0, ty = 0;
            if (!scale_to(mx, my, 1000, tx, ty) && (mx || my)) return;
            i64 x = P.x, y = P.y;
            i64 vx = P.vx, vy = P.vy;
            int lived = 0;
            i64 min_clear = 100000;
            bool dead = false;
            i64 endx = x, endy = y;
            for (int s = 0; s < 6; ++s) {
                i64 ox = x, oy = y;
                i64 sp = rdiv(c.run_v * pct, 100);
                i64 tvx = 0, tvy = 0;
                if (mx || my) scale_to(tx, ty, sp, tvx, tvy);
                i64 dvx = tvx - vx, dvy = tvy - vy;
                cap_len(dvx, dvy, c.acc);
                vx += dvx;
                vy += dvy;
                move_body(LM(), c.body_r, x, y, vx, vy);
                if (!place_ok(x, y, g.t + s + 1)) {
                    dead = true;
                    break;
                }
                for (int i = 0; i < np; ++i) {
                    int i0 = s;
                    int i1 = s + 1;
                    if (i1 >= preds[i].n) continue;
                    if (seg_near_origin(preds[i].x[i0] - ox, preds[i].y[i0] - oy, preds[i].x[i1] - x, preds[i].y[i1] - y, R)) {
                        dead = true;
                        break;
                    }
                    i64 cl = spd_len(preds[i].x[i1] - x, preds[i].y[i1] - y);
                    if (cl < min_clear) min_clear = cl;
                }
                if (dead) break;
                ++lived;
                endx = x;
                endy = y;
            }
            i64 sc;
            if (dead) sc = -1000000000LL + lived * 10000;
            else sc = min_clear * 20 - spd_len(endx - goalx, endy - goaly);
            // 即将塌的格子扣分
            int gx = cell_index(endx), gy = cell_index(endy);
            i64 fall = cell_fall_step(info(), gx, gy);
            if (fall >= 0) {
                i64 dt = fall - (g.t + 6);
                if (dt < 30) sc -= (30 - dt) * 800;
            }
            if (sc > best) {
                best = sc;
                best_a = move_of(mx, my);
            }
        };
        consider(0, 0);
        for (int i = 0; i < 16; ++i) consider(DX[i], DY[i]);
        // 也试着直接朝目标
        consider(goalx - P.x, goaly - P.y);
        return best_a;
    }

    bool nav_step(i64 x, i64 y, i64& ox, i64& oy) const {
        if (!nav_ok || nav.empty()) return false;
        int cx = cell_index(x), cy = cell_index(y);
        if (cx < 0 || cy < 0 || cx >= nav_nx || cy >= nav_ny) return false;
        int cur = nav[size_t(cy) * nav_nx + cx];
        if (cur < 0) return false;
        int best = cur, bx = cx, by = cy;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                int a = cx + dx, b = cy + dy;
                if (a < 0 || b < 0 || a >= nav_nx || b >= nav_ny) continue;
                int dv = nav[size_t(b) * nav_nx + a];
                if (dv < 0 || dv >= best) continue;
                if (dx && dy) {
                    int c1 = nav[size_t(cy) * nav_nx + a];
                    int c2 = nav[size_t(b) * nav_nx + cx];
                    if (c1 < 0 || c2 < 0) continue;
                }
                best = dv;
                bx = a;
                by = b;
            }
        if (bx == cx && by == cy) {
            ox = nav_gx;
            oy = nav_gy;
        } else {
            ox = cell_center(bx);
            oy = cell_center(by);
        }
        return true;
    }

    void goal_for(const Game& g, int k, i64& gx, i64& gy, int& kind) const {
        const Player& P = g.p[k];
        const Dart& D = g.d[k];
        kind = 0;
        gx = safe_x;
        gy = safe_y;
        int cx = cell_index(P.x), cy = cell_index(P.y);
        i64 fall = cell_fall_step(info(), cx, cy);
        bool urgent = fall >= 0 && fall < g.t + 40;
        int tgt = pick_target(g, k);
        if ((D.st == D_GROUND || D.st == D_RISE) && !urgent) {
            i64 dd = spd_len(D.x - P.x, D.y - P.y);
            bool must = D.st == D_GROUND && (g.d[k].st != D_HAND);
            if (D.st == D_HAND) must = false;
            if (g.d[k].st != D_HAND && dd < 12000) {
                gx = D.x;
                gy = D.y;
                kind = 1;
                if (tgt >= 0) {
                    i64 de = spd_len(g.p[tgt].x - P.x, g.p[tgt].y - P.y);
                    // 镖很远而敌人贴脸时，先处理敌人
                    if (de < 1600 && dd > 2500) {
                        // fall through to combat goal
                    } else return;
                } else return;
            }
        }
        if (D.st == D_BACK || D.st == D_CALL) {
            if (tgt >= 0) {
                i64 dx = g.p[tgt].x - D.x, dy = g.p[tgt].y - D.y;
                i64 L = spd_len(dx, dy);
                i64 dm = spd_len(P.x - g.p[tgt].x, P.y - g.p[tgt].y);
                if (L > 300 && L < 5200 && dm < 4500) {
                    gx = g.p[tgt].x + rdiv(dx * 1000, L);
                    gy = g.p[tgt].y + rdiv(dy * 1000, L);
                    kind = 3;
                    return;
                }
            }
        }
        if (tgt >= 0 && !urgent) {
            const Player& e = g.p[tgt];
            i64 dx = P.x - e.x, dy = P.y - e.y;
            i64 L = spd_len(dx, dy);
            if (L < 80) {
                dx = 1000;
                dy = 0;
                L = 1000;
            }
            i64 ideal = g.n > 2 ? 4100 : 3400;
            if (g.d[k].st == D_HAND && g.d[tgt].st != D_HAND) ideal = 1600;
            else if (g.d[k].st != D_HAND && g.d[tgt].st == D_HAND) ideal = 5600;
            else if (g.d[k].st != D_HAND && g.d[tgt].st != D_HAND) ideal = g.n > 2 ? 2400 : 1800;
            int sgn = ((g.t / 6) & 1) ? 1 : -1;
            if (k & 1) sgn = -sgn;
            i64 rx, ry, tx, ty;
            scale_to(dx, dy, 1000, rx, ry);
            tx = -ry;
            ty = rx;
            gx = e.x + rdiv(rx * ideal, 1000) + rdiv(tx * 1100 * sgn, 1000);
            gy = e.y + rdiv(ry * ideal, 1000) + rdiv(ty * 1100 * sgn, 1000);
            kind = 2;
            return;
        }
        kind = 0;
        gx = safe_x;
        gy = safe_y;
    }

    bool block_now(const Game& g, int k, const React& r, int sim_step, i64& ax, i64& ay) const {
        if (g.d[k].st != D_HAND) return false;
        const Player& P = g.p[k];
        if (P.st != S_FREE && !dash_move_phase(P, g.cf)) return false;
        const i64 R = g.cf.body_r + g.cf.dart_r;
        Pred preds[MAXP];
        int np = 0;
        predict_darts(g, k, r, sim_step, preds, np);
        i64 best = 0;
        bool found = false;
        for (int i = 0; i < np; ++i) {
            if (preds[i].n < 2) continue;
            // 挥砍现在出手，下一步结算、用的是下一步开始时（也就是这一步飞完后）的位置
            i64 x = preds[i].x[1], y = preds[i].y[1];
            i64 dx = x - P.x, dy = y - P.y;
            i64 L = spd_len(dx, dy);
            if (L > g.cf.slash_reach || L < R - 40) continue;
            i64 nowd = spd_len(preds[i].x[0] - P.x, preds[i].y[0] - P.y);
            if (nowd <= R) continue;  // 这一步就会打中，挡不及
            if (blocked(P.x, P.y, x, y)) continue;
            // 确认不挡就会被打中
            bool danger = false;
            for (int s = 0; s + 1 < preds[i].n && s < 6; ++s) {
                if (spd_len(preds[i].x[s] - P.x, preds[i].y[s] - P.y) < R + 200) danger = true;
            }
            if (!danger) continue;
            if (!found || L < best) {
                found = true;
                best = L;
                ax = dx;
                ay = dy;
            }
        }
        return found;
    }

    Action smart(const Game& g, int k, const React& r, int sim_step, bool is_self) const {
        const Player& P = g.p[k];
        const Config& c = g.cf;
        if (!P.alive || P.unknown) return Action();

        if (P.st == S_RECALL) {
            int tgt = nearest_foe(g, k);
            bool danger = tgt >= 0 && spd_len(g.p[tgt].x - P.x, g.p[tgt].y - P.y) < 5200;
            Pred preds[MAXP];
            int np = 0;
            predict_darts(g, k, r, sim_step, preds, np);
            if (np) danger = true;
            if (!danger) return Action();
            i64 gx, gy;
            int kind;
            goal_for(g, k, gx, gy, kind);
            return steer(g, k, gx, gy, r, sim_step);
        }

        if (P.st == S_DASH && dash_move_phase(P, c)) {
            if (place_ok(P.x, P.y, g.t)) {
                int tgt = nearest_foe(g, k);
                if (tgt >= 0 && melee_commit(g, k, tgt)) {
                    i64 dx = g.p[tgt].x - P.x, dy = g.p[tgt].y - P.y;
                    return make_act(0, 0, A_MELEE, dx, dy);
                }
            }
            return Action();
        }

        if (P.st != S_FREE && P.st != S_THROW && P.st != S_THROWN) return Action();

        i64 gx, gy;
        int kind = 0;
        goal_for(g, k, gx, gy, kind);
        if (is_self && nav_ok && (kind == 0 || kind == 1 || blocked(P.x, P.y, gx, gy))) {
            i64 wx, wy;
            if (nav_step(P.x, P.y, wx, wy)) {
                gx = wx;
                gy = wy;
            }
        }

        i64 bax = 0, bay = 0;
        bool can_block = block_now(g, k, r, sim_step, bax, bay);

        // 有已知飞镖且挡得准时，近距离优先挡；否则走位。搜索会再比较。
        Pred preds[MAXP];
        int np = 0;
        predict_darts(g, k, r, sim_step, preds, np);
        bool threat = false;
        const i64 R = c.body_r + c.dart_r;
        for (int i = 0; i < np; ++i) {
            for (int s = 0; s < preds[i].n && s < 7; ++s) {
                if (spd_len(preds[i].x[s] - P.x, preds[i].y[s] - P.y) <= R + 500) threat = true;
            }
        }

        Action mv = steer(g, k, gx, gy, r, sim_step);

        if (P.st != S_FREE) return mv;  // 蓄势 / 硬直里只能走

        if (threat && can_block) {
            // 挡得下就挡。走位若明显安全，steer 的清空距离会很大，这时仍给走位，搜索再定。
            // 这里政策偏存活：威胁进了 2 米内就挡。
            bool close = false;
            for (int i = 0; i < np; ++i)
                if (preds[i].n > 1 && spd_len(preds[i].x[1] - P.x, preds[i].y[1] - P.y) <= c.slash_reach) close = true;
            if (close) return make_act(0, 0, A_MELEE, bax, bay);
        }

        int tgt = pick_target(g, k);
        if (tgt >= 0 && want_throw(g, k, tgt)) {
            i64 ax, ay;
            aim_lead(g, k, tgt, ax, ay);
            // 扔的同时横向走，避开对扔
            i64 sx = -(g.p[tgt].y - P.y), sy = (g.p[tgt].x - P.x);
            int sgn = ((g.t / 8) & 1) ? 1 : -1;
            if (k & 1) sgn = -sgn;
            if (!threat) return make_act(sx * sgn, sy * sgn, A_THROW, ax, ay);
            return make_act(mv.mx, mv.my, A_THROW, ax, ay);
        }

        if (tgt >= 0 && melee_commit(g, k, tgt)) {
            i64 dx = g.p[tgt].x - P.x, dy = g.p[tgt].y - P.y;
            return make_act(0, 0, A_MELEE, dx, dy);
        }

        // 召回要站 12 步，只在没人靠近时用。战斗里走路去捡。
        if (c.recall && g.d[k].st == D_GROUND && P.st == S_FREE && !threat) {
            i64 dd = spd_len(g.d[k].x - P.x, g.d[k].y - P.y);
            int foe = nearest_foe(g, k);
            i64 de = foe < 0 ? 100000 : spd_len(g.p[foe].x - P.x, g.p[foe].y - P.y);
            bool foe_armed = foe >= 0 && g.d[foe].st == D_HAND;
            bool path_bad = blocked(P.x, P.y, g.d[k].x, g.d[k].y);
            if (de > 11000 && !foe_armed && (path_bad || dd > 7000) && dd > c.pick_r) {
                Action a;
                a.act = A_RECALL;
                return a;
            }
        }

        // 冲刺前摇要原地站两步。附近有飞镖，或对方手里有镖，都不冲。
        bool dart_close = nearest_flying(g, k) < 4500;
        bool foe_can_throw = false;
        int dash_busy = int(c.dash_pre + c.dash_t + c.dash_post + 2);
        if (tgt >= 0 && g.d[tgt].st == D_HAND && locked_for(g.p[tgt], c) < dash_busy) {
            if (spd_len(g.p[tgt].x - P.x, g.p[tgt].y - P.y) < 6800) foe_can_throw = true;
        }
        if (P.cd == 0 && P.st == S_FREE && !threat && !dart_close && !foe_can_throw) {
            int foe = nearest_foe(g, k);
            bool foe_armed = foe >= 0 && g.d[foe].st == D_HAND && locked_for(g.p[foe], c) < 4;
            if (g.d[k].st == D_GROUND) {
                i64 dx = g.d[k].x - P.x, dy = g.d[k].y - P.y;
                i64 dd = spd_len(dx, dy);
                i64 de = foe < 0 ? 100000 : spd_len(g.p[foe].x - P.x, g.p[foe].y - P.y);
                if (dd > 1600 && dd < 5200 && dd + 800 < de && dash_ok(P.x, P.y, dx, dy, g.t))
                    return make_act(0, 0, A_DASH, dx, dy);
            }
            if (foe >= 0 && !foe_armed && g.d[k].st == D_HAND) {
                i64 dx = g.p[foe].x - P.x, dy = g.p[foe].y - P.y;
                i64 dd = spd_len(dx, dy);
                if (dd > 2000 && dd < 4800 && dash_ok(P.x, P.y, dx, dy, g.t)) return make_act(0, 0, A_DASH, dx, dy);
            }
            if (kind == 0) {
                i64 dx = safe_x - P.x, dy = safe_y - P.y;
                i64 de = foe < 0 ? 100000 : spd_len(g.p[foe].x - P.x, g.p[foe].y - P.y);
                if (!foe_armed && de > 6000 && spd_len(dx, dy) > 2200 && dash_ok(P.x, P.y, dx, dy, g.t))
                    return make_act(0, 0, A_DASH, dx, dy);
            }
        }

        return mv;
    }

    i64 evaluate(const Game& g) const {
        const Player& me = g.p[seat];
        int foes = 0;
        for (int k = 0; k < g.n; ++k)
            if (k != seat && g.p[k].alive && !team_of(g, seat, k)) ++foes;
        int mates_alive = 0, mates = 0;
        for (int k = 0; k < g.n; ++k)
            if (team_of(g, seat, k)) {
                ++mates;
                if (g.p[k].alive) ++mates_alive;
            }
        if (!me.alive) {
            if (g.cf.mode == M_TEAM) {
                // 队没了就是输
                return -900000000LL + g.t * 1000 + me.kills * 200000 + mates_alive * 50000;
            }
            if (foes == 0) return 80000000LL + me.kills * 1000;  // 同归于尽
            return -800000000LL + g.t * 3000 + me.kills * 220000;
        }
        i64 s = 1000000000LL;
        s += me.kills * 260000;
        s += i64(8 - foes) * 70000;
        if (mates) s -= i64(mates - mates_alive) * 450000;

        const Dart& D = g.d[seat];
        if (D.st == D_HAND) s += 90000;
        else if (D.st == D_OUT || D.st == D_BACK || D.st == D_CALL) s += 50000;
        else if (D.st == D_GROUND || D.st == D_RISE) s += 25000 - spd_len(D.x - me.x, D.y - me.y) / 2;
        else s -= 80000;
        if (me.st == S_RECALL) s -= 120000;
        if (me.st == S_KICKED || me.st == S_SLASHED) s -= 140000;

        i64 nearest = 100000000;
        int ni = -1;
        int close_dart = 0;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || !g.p[k].alive || g.p[k].unknown || team_of(g, seat, k)) continue;
            i64 d = spd_len(g.p[k].x - me.x, g.p[k].y - me.y);
            if (d < nearest) {
                nearest = d;
                ni = k;
            }
            if (g.d[k].st == D_HAND && d < 4500) ++close_dart;
        }
        if (close_dart >= 2) s -= 50000 * (close_dart - 1);
        if (ni >= 0) {
            bool ih = D.st == D_HAND;
            bool th = g.d[ni].st == D_HAND;
            if (ih && !th) s -= nearest / 5;
            else if (ih && th) {
                i64 ideal = g.n > 2 ? 4100 : 3400;
                s -= iabs(nearest - ideal) / 2;
                if (nearest < 1450) s -= 60000;
            } else if (!ih && th) {
                if (nearest < 4800) s -= (4800 - nearest) * 18;
            } else s -= nearest / 8;
        }
        for (int k = 0; k < g.n; ++k) {
            if (!team_of(g, seat, k) || !g.p[k].alive) continue;
            i64 d = spd_len(g.p[k].x - me.x, g.p[k].y - me.y);
            if (d < 1200) s -= 25000;
        }
        int cx = cell_index(me.x), cy = cell_index(me.y);
        i64 fall = cell_fall_step(info(), cx, cy);
        if (fall >= 0) {
            i64 dt = fall - g.t;
            if (dt < 18) s -= 250000;
            else if (dt < 45) s -= (45 - dt) * 2500;
        }
        if (g.t > g.cf.col_start - 50) s -= spd_len(me.x - safe_x, me.y - safe_y) / 5;
        if ((me.st == S_SLASHED || me.st == S_KICKED || me.st == S_RECALL) && nearest < 3200) s -= 90000;
        return s;
    }

    // 推演里用的轻量政策：不搜索、不跑 16 方向模拟。第一步的选择才交给外层比较。
    Action cheap(const Game& g, int k, const React& r, int sim_step) const {
        const Player& P = g.p[k];
        const Config& c = g.cf;
        if (!P.alive || P.unknown) return Action();
        if (P.st == S_SLASH || P.st == S_SLASHED || P.st == S_KICK || P.st == S_KICKED || P.st == S_STUN) return Action();

        const i64 R = c.body_r + c.dart_r;
        int threat = -1;
        i64 threat_d = 0;
        i64 svx = 0, svy = 0, tx = 0, ty = 0;
        for (int q = 0; q < g.n; ++q) {
            if (q == k || !dart_flying(g.d[q].st)) continue;
            if (!dart_known(r, q, k, sim_step)) continue;
            const Dart& D = g.d[q];
            i64 vx, vy;
            i64 step = dart_step_len(c, D);
            if (D.st == D_OUT) {
                vx = rdiv(D.vx * step, 1000);
                vy = rdiv(D.vy * step, 1000);
            } else if (step <= 0) {
                vx = vy = 0;
            } else {
                scale_to(g.p[q].x - D.x, g.p[q].y - D.y, step, vx, vy);
            }
            i64 dist = spd_len(D.x - P.x, D.y - P.y);
            i64 closing = (P.x - D.x) * vx + (P.y - D.y) * vy;
            if (closing <= 0 && dist > R + 400) continue;
            if (dist > 3200 && dist > R + step * 4) continue;
            if (threat < 0 || dist < threat_d) {
                threat = q;
                threat_d = dist;
                svx = vx;
                svy = vy;
                tx = D.x;
                ty = D.y;
            }
        }

        auto pit_move = [&](i64 mx, i64 my) {
            Action a = move_of(mx, my);
            i64 ux, uy;
            if (!scale_to(a.mx, a.my, c.run_v, ux, uy)) return a;
            i64 nx = P.x + ux, ny = P.y + uy;
            if (place_ok(nx, ny, g.t + 1)) return a;
            static const i64 OX[8] = {1000, 0, -1000, 0, 707, -707, -707, 707};
            static const i64 OY[8] = {0, 1000, 0, -1000, 707, 707, -707, -707};
            i64 best = LLONG_MIN;
            Action b = a;
            i64 gx = P.x + mx, gy = P.y + my;
            for (int i = 0; i < 8; ++i) {
                if (!scale_to(OX[i], OY[i], c.run_v, ux, uy)) continue;
                nx = P.x + ux;
                ny = P.y + uy;
                if (!place_ok(nx, ny, g.t + 1)) continue;
                i64 sc = -spd_len(nx - gx, ny - gy);
                if (sc > best) {
                    best = sc;
                    b = move_of(OX[i], OY[i]);
                }
            }
            if (best == LLONG_MIN) return Action();
            return b;
        };

        if (P.st == S_RECALL) {
            if (threat < 0) {
                int foe = nearest_foe(g, k);
                if (foe < 0 || spd_len(g.p[foe].x - P.x, g.p[foe].y - P.y) > 5000) {
                    Action a;
                    a.act = A_RECALL;
                    return a;
                }
            }
            int foe = nearest_foe(g, k);
            if (foe >= 0) return pit_move(P.x - g.p[foe].x, P.y - g.p[foe].y);
            return pit_move(safe_x - P.x, safe_y - P.y);
        }

        if (P.st == S_DASH && dash_move_phase(P, c) && place_ok(P.x, P.y, g.t)) {
            int tgt = nearest_foe(g, k);
            if (tgt >= 0 && melee_commit(g, k, tgt)) {
                i64 dx = g.p[tgt].x - P.x, dy = g.p[tgt].y - P.y;
                return make_act(0, 0, A_MELEE, dx, dy);
            }
            return Action();
        }

        if (threat >= 0 && (P.st == S_FREE || P.st == S_THROW || P.st == S_THROWN)) {
            i64 px = -svy, py = svx;
            i64 c1 = (P.x - tx) * px + (P.y - ty) * py;
            if (c1 < 0) {
                px = -px;
                py = -py;
            }
            Action left = pit_move(px, py);
            Action right = pit_move(-px, -py);
            auto clear = [&](const Action& a) {
                i64 ux, uy;
                if (!scale_to(a.mx, a.my, c.run_v, ux, uy)) return spd_len(P.x - tx, P.y - ty);
                return spd_len(P.x + ux - (tx + svx), P.y + uy - (ty + svy));
            };
            return clear(left) >= clear(right) ? left : right;
        }

        if (P.st != S_FREE) {
            if (P.st == S_THROW || P.st == S_THROWN) {
                if (spd_len(P.vx, P.vy) > 30) return move_of(P.vx, P.vy);
            }
            return Action();
        }

        int tgt = pick_target(g, k);
        // 对方挥砍已经进前摇：往外让一步。这样推演里贴边的挥砍会落空。
        if (tgt >= 0 && g.p[tgt].st == S_SLASH) {
            i64 dx = P.x - g.p[tgt].x, dy = P.y - g.p[tgt].y;
            i64 L = spd_len(dx, dy);
            if (L > c.body_r + 80 && L < c.slash_reach + 500) return pit_move(dx, dy);
        }
        if (tgt >= 0 && want_throw(g, k, tgt) && threat < 0) {
            i64 ax, ay;
            aim_lead(g, k, tgt, ax, ay);
            i64 sx = -(g.p[tgt].y - P.y), sy = g.p[tgt].x - P.x;
            int sgn = ((g.t / 8) & 1) ? 1 : -1;
            if (k & 1) sgn = -sgn;
            return make_act(sx * sgn, sy * sgn, A_THROW, ax, ay);
        }
        if (tgt >= 0 && melee_commit(g, k, tgt)) {
            i64 dx = g.p[tgt].x - P.x, dy = g.p[tgt].y - P.y;
            return make_act(0, 0, A_MELEE, dx, dy);
        }

        i64 gx, gy;
        int kind = 0;
        goal_for(g, k, gx, gy, kind);
        if (k == seat && nav_ok && (kind == 0 || kind == 1 || blocked(P.x, P.y, gx, gy))) {
            i64 wx, wy;
            if (nav_step(P.x, P.y, wx, wy)) {
                gx = wx;
                gy = wy;
            }
        }
        return pit_move(gx - P.x, gy - P.y);
    }

    i64 rollout(Game g, Action first, int horizon, React react) const {
        // 外推会把镖放偏几十到几百毫米。贴着命中半径擦过，按没躲开记。
        i64 graze = 0;
        const i64 R = g.cf.body_r + g.cf.dart_r;
        for (int s = 0; s < horizon; ++s) {
            if (!g.p[seat].alive) return evaluate(g) - graze;
            Action acts[MAXP];
            for (int k = 0; k < g.n; ++k) {
                acts[k] = Action();
                if (!g.p[k].alive || g.p[k].unknown) continue;
                if (k == seat && s == 0) acts[k] = first;
                else acts[k] = cheap(g, k, react, s);
                if (acts[k].act == A_THROW && g.p[k].st == S_FREE && g.d[k].st == D_HAND && react.throw_step[k] == -1)
                    react.throw_step[k] = s;
            }
            g.over = false;
            g.step(acts);
            if (!g.p[seat].alive) return evaluate(g) - graze;
            for (int k = 0; k < g.n; ++k) {
                if (k == seat || !dart_flying(g.d[k].st)) continue;
                i64 d = spd_len(g.d[k].x - g.p[seat].x, g.d[k].y - g.p[seat].y);
                if (d < R + 450) graze += 420000;
                else if (d < R + 900) graze += 50000;
            }
        }
        return evaluate(g) - graze;
    }

    Action continue_act(const Player& P) const {
        Action a;
        if (P.st == S_RECALL) {
            a.act = A_RECALL;
            return a;
        }
        if (P.st == S_FREE || P.st == S_THROW || P.st == S_THROWN) {
            if (spd_len(P.vx, P.vy) > 30) scale_to(P.vx, P.vy, 1000, a.mx, a.my);
        }
        return a;
    }

    Game reconstruct(const Obs& o) const {
        Game g(info(), o);
        const int delay = int(g.cf.delay);
        const i64 t0 = o.t > delay ? o.t - delay : 0;
        const int steps = int(o.t - t0);
        g.t = t0;
        g.over = false;
        for (int k = 0; k < g.n; ++k) {
            g.spx[k] = info().spx[k];
            g.spy[k] = info().spy[k];
            if (k == seat) {
                const Hist& h = hist[ring(t0)];
                if (h.ok && h.t == t0) {
                    g.p[k] = h.p;
                    g.d[k] = h.d;
                } else {
                    g.p[k] = o.p[k];
                    g.d[k] = o.d[k];
                }
                g.p[k].unknown = 0;
            } else {
                g.p[k] = o.p[k];
                g.d[k] = o.d[k];
                g.p[k].unknown = (o.p[k].alive && !o.p[k].vis) ? 1 : 0;
            }
            g.p[k].team = info().team[k];
            g.p[k].px = g.p[k].x;
            g.p[k].py = g.p[k].y;
        }
        for (int s = 0; s < steps; ++s) {
            Action acts[MAXP];
            i64 tt = t0 + s;
            for (int k = 0; k < g.n; ++k) {
                acts[k] = Action();
                if (!g.p[k].alive || g.p[k].unknown) continue;
                if (k == seat) {
                    const Hist& h = hist[ring(tt)];
                    if (h.acted && h.t == tt) acts[k] = h.a;
                } else acts[k] = continue_act(g.p[k]);
            }
            g.over = false;
            g.step(acts);
        }
        g.p[seat] = o.p[seat];
        g.d[seat] = o.d[seat];
        g.p[seat].team = info().team[seat];
        g.p[seat].unknown = 0;
        g.t = o.t;
        g.over = false;

        // 队友消息比观察新：低 15 位是格子，最高位是有效标记
        for (int k = 0; k < g.n; ++k) {
            if (!team_of(g, seat, k) || !o.p[k].alive) continue;
            i64 msg = o.p[k].msg;
            if ((msg & (1 << 15)) == 0) continue;
            int cx = int(msg & 63);
            int cy = int((msg >> 6) & 31);
            if (!info().M().inside(cx, cy)) continue;
            i64 mx = cell_center(cx) + g.p[k].vx;
            i64 my = cell_center(cy) + g.p[k].vy;
            if (spd_len(mx - g.p[k].x, my - g.p[k].y) > 700) {
                g.p[k].x = mx;
                g.p[k].y = my;
                g.p[k].unknown = 0;
                if (g.d[k].st == D_HAND) {
                    g.d[k].x = mx;
                    g.d[k].y = my;
                }
            }
        }
        return g;
    }

    bool make_phantom(const Game& src, Game& dst, int& enemy) const {
        enemy = -1;
        i64 best = 0;
        int ago = int(src.cf.delay);
        if (ago < 2) return false;
        for (int k = 0; k < src.n; ++k) {
            if (k == seat || !src.p[k].alive || src.p[k].unknown) continue;
            if (team_of(src, seat, k)) continue;
            if (src.d[k].st != D_HAND) continue;
            if (src.p[k].st != S_FREE && src.p[k].st != S_THROWN) continue;
            i64 dx = src.p[seat].x - src.p[k].x, dy = src.p[seat].y - src.p[k].y;
            i64 dist = spd_len(dx, dy);
            if (dist < 1800 || dist > 5600) continue;
            if (blocked(src.p[k].x, src.p[k].y, src.p[seat].x, src.p[seat].y)) continue;
            // 面向我才像是会偷扔
            i64 fx = src.p[k].face;
            // 用速度或朝向：face 0=+x
            i64 ux, uy;
            if (spd_len(src.p[k].vx, src.p[k].vy) > 40) {
                ux = src.p[k].vx;
                uy = src.p[k].vy;
            } else {
                // face → 方向
                ux = dx;
                uy = dy;
            }
            if (ux * dx + uy * dy <= 0) continue;
            if (enemy < 0 || dist < best) {
                enemy = k;
                best = dist;
            }
        }
        if (enemy < 0) return false;
        const Player& E = src.p[enemy];
        const Player& M = src.p[seat];
        i64 ex = E.x - E.vx * ago, ey = E.y - E.vy * ago;
        i64 mx = M.x - M.vx * ago, my = M.y - M.vy * ago;
        if (!place_ok(ex, ey, src.t)) {
            ex = E.x;
            ey = E.y;
        }
        i64 ax = mx - ex, ay = my - ey;
        i64 ux, uy;
        if (!scale_to(ax, ay, 1000, ux, uy)) return false;
        if (blocked(ex, ey, mx, my)) return false;
        // 从出手往后推 ago 步，得到“现在”的镖
        i64 x = ex, y = ey;
        i64 left = src.cf.range;
        i64 age = 0;
        bool out = false;
        int flown = 0;
        for (int s = 0; s < ago; ++s) {
            if (s == int(src.cf.throw_pre) - 1) {
                out = true;
                x = ex;
                y = ey;
                continue;
            }
            if (!out) continue;
            i64 spd = imax(src.cf.dart_vmin, src.cf.dart_v - age * src.cf.dart_dec);
            i64 m = imin(spd, left);
            i64 nx = x + rdiv(ux * m, 1000);
            i64 ny = y + rdiv(uy * m, 1000);
            i64 hx, hy;
            if (seg_wall(LM(), x, y, nx, ny, hx, hy)) return false;
            // 对我过去位置的粗略命中：用线性插值的我
            i64 px = mx + rdiv((M.x - mx) * (s + 1), ago);
            i64 py = my + rdiv((M.y - my) * (s + 1), ago);
            i64 ox = mx + rdiv((M.x - mx) * s, ago);
            i64 oy = my + rdiv((M.y - my) * s, ago);
            if (seg_near_origin(x - ox, y - oy, nx - px, ny - py, src.cf.body_r + src.cf.dart_r)) return false;
            x = nx;
            y = ny;
            left -= m;
            ++age;
            ++flown;
            if (left <= 0) return false;
        }
        if (!out || flown <= 0) return false;
        dst = src;
        Dart& D = dst.d[enemy];
        D.st = D_OUT;
        D.x = x;
        D.y = y;
        D.vx = ux;
        D.vy = uy;
        D.left = left;
        D.age = age;
        dst.p[enemy].st = S_FREE;
        dst.p[enemy].timer = 0;
        dst.p[enemy].ax = dst.p[enemy].ay = 0;
        return true;
    }

    void build_nav(i64 gx, i64 gy, i64 t) {
        nav_ok = false;
        nav_gx = gx;
        nav_gy = gy;
        int cg = cell_index(gx), rg = cell_index(gy);
        const Map& M = info().M();
        if (!M.inside(cg, rg)) return;
        nav = grid_dist(info(), cg, rg, t);
        nav_nx = M.nx;
        nav_ny = M.ny;
        nav_ok = !nav.empty();
    }

    bool dup_act(const Action& a, const Action& b) const {
        if (a.act != b.act) return false;
        if (iabs(a.mx - b.mx) > 180 || iabs(a.my - b.my) > 180) return false;
        if (a.act != A_NONE && (iabs(a.ax - b.ax) > 180 || iabs(a.ay - b.ay) > 180)) return false;
        return true;
    }

    i64 pack_msg(const Player& me, const Dart& d, int tgt) const {
        int cx = iclamp(cell_index(me.x), 0, 63);
        int cy = iclamp(cell_index(me.y), 0, 31);
        int dc = 7;
        if (d.st == D_HAND) dc = 0;
        else if (d.st == D_OUT) dc = 1;
        else if (d.st == D_BACK) dc = 2;
        else if (d.st == D_GROUND || d.st == D_RISE) dc = 3;
        else if (d.st == D_CALL) dc = 4;
        (void)tgt;
        return (cx & 63) | ((cy & 31) << 6) | ((dc & 7) << 11) | (1 << 15);
    }

    Action decide(const Obs& o) {
        cpu0 = thread_cpu_100ns();
        seat = o.seat;
        n = o.n;
        Hist& hcur = hist[ring(o.t)];
        hcur.t = o.t;
        hcur.ok = true;
        hcur.acted = false;
        hcur.p = o.p[seat];
        hcur.d = o.d[seat];

        for (int k = 0; k < o.n; ++k) {
            if (k == seat) continue;
            if (o.p[k].alive && o.p[k].vis) {
                mem[k].ok = true;
                mem[k].t = o.t - info().cfg.delay;
                mem[k].x = o.p[k].x;
                mem[k].y = o.p[k].y;
                mem[k].vx = o.p[k].vx;
                mem[k].vy = o.p[k].vy;
                mem[k].dart_st = o.d[k].st;
            }
        }

        liv = live_map(info().cfg, info().M(), o.t);
        liv_ok = true;

        if (!o.me().alive) {
            Action a;
            a.msg = pack_msg(o.me(), o.my_dart(), -1);
            hcur.a = a;
            hcur.acted = true;
            return a;
        }

        Game now = reconstruct(o);
        // 看不见但刚进草的人：不放进物理模拟，只在评分里躲开最后位置
        i64 gx, gy;
        int kind = 0;
        goal_for(now, seat, gx, gy, kind);
        build_nav(gx, gy, now.t);

        React base;
        for (int k = 0; k < now.n; ++k)
            if (dart_flying(now.d[k].st)) base.throw_step[k] = -100;

        Action heur = smart(now, seat, base, 0, true);

        Action cands[MAXC];
        int nc = 0;
        auto push = [&](Action a) {
            if (nc >= MAXC) return;
            for (int i = 0; i < nc; ++i)
                if (dup_act(cands[i], a)) return;
            cands[nc++] = a;
        };
        push(heur);
        i64 fly = nearest_flying(now, seat);
        if (fly >= 2000) push(move_of(0, 0));
        static const i64 DX[8] = {1000, 707, 0, -707, -1000, -707, 0, 707};
        static const i64 DY[8] = {0, 707, 1000, 707, 0, -707, -1000, -707};
        for (int i = 0; i < 8; ++i) push(move_of(DX[i], DY[i]));
        if (fly < 3200) {
            int owner = -1;
            i64 bd = fly;
            for (int k = 0; k < now.n; ++k) {
                if (k == seat || !dart_flying(now.d[k].st)) continue;
                i64 d = spd_len(now.d[k].x - now.p[seat].x, now.d[k].y - now.p[seat].y);
                if (owner < 0 || d < bd) {
                    owner = k;
                    bd = d;
                }
            }
            if (owner >= 0) {
                i64 vx = now.d[owner].vx, vy = now.d[owner].vy;
                if (spd_len(vx, vy) < 40) {
                    vx = now.p[seat].x - now.d[owner].x;
                    vy = now.p[seat].y - now.d[owner].y;
                }
                push(move_of(-vy, vx));
                push(move_of(vy, -vx));
            }
        }

        const Player& me = now.p[seat];
        int tgt = pick_target(now, seat);
        if (me.st == S_FREE && tgt >= 0 && want_throw(now, seat, tgt)) {
            i64 ax, ay;
            aim_lead(now, seat, tgt, ax, ay);
            i64 sx = -(now.p[tgt].y - me.y), sy = now.p[tgt].x - me.x;
            push(make_act(sx, sy, A_THROW, ax, ay));
            push(make_act(-sx, -sy, A_THROW, ax, ay));
            push(make_act(me.vx, me.vy, A_THROW, ax, ay));
            // 少预瞄一点，对付急停和延迟误差
            i64 lx = now.p[tgt].x - me.x + now.p[tgt].vx * 2;
            i64 ly = now.p[tgt].y - me.y + now.p[tgt].vy * 2;
            push(make_act(sx, sy, A_THROW, lx, ly));
            push(make_act(-sx, -sy, A_THROW, now.p[tgt].x - me.x, now.p[tgt].y - me.y));
        }
        if ((me.st == S_FREE || dash_move_phase(me, now.cf)) && tgt >= 0 && melee_commit(now, seat, tgt)) {
            i64 dx = now.p[tgt].x - me.x, dy = now.p[tgt].y - me.y;
            push(make_act(0, 0, A_MELEE, dx, dy));
        }
        i64 bax, bay;
        if (block_now(now, seat, base, 0, bax, bay)) push(make_act(0, 0, A_MELEE, bax, bay));

        // 冲刺前摇站着不动。飞镖在 4.5 米内，或对方手里有镖且在 6.8 米内，都不冲。
        bool dart_close = fly < 4500;
        i64 de = tgt < 0 ? 100000 : spd_len(now.p[tgt].x - me.x, now.p[tgt].y - me.y);
        int dash_busy = int(now.cf.dash_pre + now.cf.dash_t + now.cf.dash_post + 2);
        bool foe_can_throw = tgt >= 0 && now.d[tgt].st == D_HAND && locked_for(now.p[tgt], now.cf) < dash_busy && de < 6800;
        if (me.st == S_FREE && me.cd == 0 && !dart_close && !foe_can_throw) {
            if (now.d[seat].st == D_GROUND) {
                i64 dx = now.d[seat].x - me.x, dy = now.d[seat].y - me.y;
                i64 dd = spd_len(dx, dy);
                if (dd > 1400 && dd < 5200 && dd + 600 < de && dash_ok(me.x, me.y, dx, dy, now.t))
                    push(make_act(0, 0, A_DASH, dx, dy));
            }
            if (tgt >= 0 && now.d[tgt].st != D_HAND && de > 2000 && de < 5000 &&
                dash_ok(me.x, me.y, now.p[tgt].x - me.x, now.p[tgt].y - me.y, now.t))
                push(make_act(0, 0, A_DASH, now.p[tgt].x - me.x, now.p[tgt].y - me.y));
            if (de > 6000) {
                i64 ax = safe_x - me.x, ay = safe_y - me.y;
                if (spd_len(ax, ay) > 2000 && dash_ok(me.x, me.y, ax, ay, now.t)) push(make_act(0, 0, A_DASH, ax, ay));
            }
        }
        if (me.st == S_FREE && info().cfg.recall && now.d[seat].st == D_GROUND) {
            i64 de = tgt < 0 ? 100000 : spd_len(now.p[tgt].x - me.x, now.p[tgt].y - me.y);
            bool foe_armed = tgt >= 0 && now.d[tgt].st == D_HAND;
            if (de > 11000 && !foe_armed) {
                Action a;
                a.act = A_RECALL;
                push(a);
            }
        }

        Game phantom;
        int ph_e = -1;
        bool has_ph = make_phantom(now, phantom, ph_e);
        React ph_react;
        if (has_ph) {
            for (int k = 0; k < phantom.n; ++k)
                if (dart_flying(phantom.d[k].st)) ph_react.throw_step[k] = -100;
        }

        int H1 = now.n <= 2 ? 16 : now.n <= 4 ? 12 : 8;
        int H2 = now.n <= 2 ? 24 : now.n <= 4 ? 16 : 11;
        bool combat = tgt >= 0 && spd_len(now.p[tgt].x - me.x, now.p[tgt].y - me.y) < 8000;
        if (!combat) {
            for (int k = 0; k < now.n; ++k)
                if (k != seat && dart_flying(now.d[k].st)) combat = true;
            if (now.d[seat].st == D_OUT || now.d[seat].st == D_BACK) combat = true;
        }
        if (!combat) {
            H1 = now.n <= 2 ? 8 : 6;
            H2 = H1;
            has_ph = false;
        }
        if (cpu0 < 0) {
            H1 = 6;
            H2 = 6;
            has_ph = false;
        } else {
            // 脚下快塌时把视野拉到塌陷那一步，否则推演在掉下去之前就结束了
            int cx = cell_index(me.x), cy = cell_index(me.y);
            i64 fall = cell_fall_step(info(), cx, cy);
            if (fall >= 0) {
                int left = int(fall - now.t);
                if (left > 4 && left < 42) {
                    int need = left + 2;
                    if (need > 30) need = 30;
                    if (H1 < need) H1 = need;
                    if (H2 < need) H2 = need;
                }
            }
        }

        i64 scores[MAXC];
        int order[MAXC];
        int tested = 0;
        i64 best_sc = LLONG_MIN;
        Action best = heur;
        for (int i = 0; i < nc; ++i) {
            if (time_up() && i > 0) break;
            React r = base;
            i64 sc = rollout(now, cands[i], H1, r);
            if (has_ph && now.n <= 4 && !time_up()) {
                React r2 = ph_react;
                i64 sc2 = rollout(phantom, cands[i], now.n <= 2 ? 10 : 8, r2);
                if (sc2 < sc) sc = sc2;
            }
            if (i == 0) sc += 80;  // 平分时信政策
            scores[tested] = sc;
            order[tested] = i;
            ++tested;
            if (sc > best_sc) {
                best_sc = sc;
                best = cands[i];
            }
        }
        if (!time_up() && tested > 1) {
            int idx[MAXC];
            for (int i = 0; i < tested; ++i) idx[i] = i;
            std::sort(idx, idx + tested, [&](int a, int b) { return scores[a] > scores[b]; });
            int keep = tested < 3 ? tested : 3;
            for (int j = 0; j < keep; ++j) {
                if (time_up()) break;
                int i = order[idx[j]];
                React r = base;
                i64 sc = rollout(now, cands[i], H2, r);
                if (has_ph && !time_up()) {
                    React r2 = ph_react;
                    i64 sc2 = rollout(phantom, cands[i], imax(8, H1), r2);
                    if (sc2 < sc) sc = sc2;
                }
                if (i == 0) sc += 80;
                if (sc > best_sc) {
                    best_sc = sc;
                    best = cands[i];
                }
            }
        }

        best.msg = pack_msg(now.p[seat], now.d[seat], tgt);
        hcur.a = best;
        hcur.acted = true;
        return best;
    }
};

DART_MAIN(Bot)
