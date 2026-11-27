// spin-hunter v13: from-scratch boomerang bot.
// Over v12: FFA throw range also tightened to 5500; FFA kill weight raised.
// Plan: rebuild present world from history (delay compensation), then
// simulate H steps for ~13 candidate moves x actions, pick best score.
// Throws use intercept + coverage (9 foe reactions) before firing.
#include "dart.hpp"
#include <algorithm>
#include <vector>
using namespace dart;

struct Hunter {
    static const int HS = 840;
    struct Snap {
        i64 x, y, vx, vy, face, st, timer, cd;
        i64 dx, dy, dvx, dvy, dleft, dage, dst;
        Action a;
    };
    Snap hist[HS];
    int hrec = 0;
    GameInfo gi0;
    int nx = 0, ny = 0;
    std::vector<int> fall;

    void init(const GameInfo& gi) {
        gi0 = gi;
        nx = gi.M().nx;
        ny = gi.M().ny;
        fall.assign((size_t)nx * ny, -1);
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                int w = gi.M().wave(x, y);
                fall[(size_t)y * nx + x] = w < 0 ? -1 : (int)wave_fall_step(gi.cfg, w);
            }
        hrec = 0;
    }

    static i64 dist2(i64 x0, i64 y0, i64 x1, i64 y1) { return ilen(x1 - x0, y1 - y0); }

    // Input that sustains current velocity (free full speed, throw/thrown scaled).
    static void sustain_input(const Config& cf, const Player& P, i64& mx, i64& my) {
        mx = my = 0;
        if (!(P.vx || P.vy)) return;
        if (P.st == S_FREE) {
            i64 sp = ilen(P.vx, P.vy);
            i64 want = sp * 1000 / imax(1, cf.run_v);
            if (want > 1000) want = 1000;
            if (want < 1) want = 1;
            scale_to(P.vx, P.vy, want, mx, my);
        } else if (P.st == S_THROW || P.st == S_THROWN) {
            i64 sp = ilen(P.vx, P.vy);
            i64 want = sp * 100 * 1000 / imax(1, cf.run_v * imax(1, cf.throw_move));
            if (want > 1000) want = 1000;
            if (want < 1) want = 1;
            scale_to(P.vx, P.vy, want, mx, my);
        }
    }

    static bool armed_soon(const Player& P, const Dart& D, i64 px, i64 py) {
        (void)P;
        if (D.st == D_HAND || D.st == D_BACK || D.st == D_CALL || D.st == D_RISE) return true;
        if (D.st == D_GROUND && ilen(D.x - px, D.y - py) < 3000) return true;
        return false;
    }

    // Distance covered by m moving steps of an outgoing dart.
    static i64 cum_out(const Config& cf, int m) {
        i64 s = 0;
        for (int k = 1; k <= m; ++k) {
            Dart tmp;
            tmp.st = D_OUT;
            tmp.age = k - 1;
            s += dart_step_len(cf, tmp);
        }
        return s;
    }

    // Throw intercept: from S, target starts at P0 moving with V. Find earliest hit.
    static bool intercept_throw(const Config& cf, const Map& M, i64 sx, i64 sy, i64 p0x, i64 p0y, i64 vx, i64 vy,
                                int t0, int t1, i64& ax, i64& ay, int& mh) {
        const i64 R = cf.body_r + cf.dart_r;
        (void)t1;
        for (int m = 1; m <= 14; ++m) {
            i64 cum = cum_out(cf, m);
            if (cum > cf.range + R) break;
            i64 tx = p0x + vx * (t0 + m), ty = p0y + vy * (t0 + m);
            if (tx < 0 || ty < 0 || tx > M.width() || ty > M.height()) continue;
            i64 dx = tx - sx, dy = ty - sy;
            if (ilen(dx, dy) > cum + R) continue;
            i64 ux, uy;
            if (!scale_to(dx, dy, 1000, ux, uy)) { ux = 1000; uy = 0; }
            i64 hx = sx + ux * cum / 1000, hy = sy + uy * cum / 1000;
            if (ilen(hx - tx, hy - ty) > R) continue;
            if (wall_between(M, sx, sy, hx, hy)) continue;
            ax = ux; ay = uy; mh = m;
            return true;
        }
        return false;
    }

    // Coverage: target keeps velocity until ns, then flees 8 dirs or stands. Count hits (1..9).
    static int coverage(const Config& cf, const Map& M, i64 sx, i64 sy, i64 p0x, i64 p0y, i64 vx, i64 vy, int lock) {
        i64 ax, ay;
        int mh;
        int ns = (int)cf.delay + 1 + lock;
        if (ns < 0) ns = 0;
        if (!intercept_throw(cf, M, sx, sy, p0x, p0y, vx, vy, 2, 20, ax, ay, mh)) return 0;
        int hit = 1;
        int t = mh + 2;
        for (int d = 0; d < 8; ++d) {
            i64 dvx = Game::icos_face(d * 4500) * cf.run_v / 1000;
            i64 dvy = Game::isin_face(d * 4500) * cf.run_v / 1000;
            i64 ex = p0x + vx * ns + dvx * imax(0, t - ns);
            i64 ey = p0y + vy * ns + dvy * imax(0, t - ns);
            i64 evx = t > 0 ? (ex - p0x) / t : 0, evy = t > 0 ? (ey - p0y) / t : 0;
            if (intercept_throw(cf, M, sx, sy, p0x, p0y, evx, evy, 2, 20, ax, ay, mh)) hit++;
        }
        return hit;
    }

    // Homing threat: foe's dart in D_BACK/D_CALL chases me if its owner walks my
    // way. Model: dart replans each step toward my path point, speed from engine.
    static int homing_risk(const Config& cf, const Map& M, i64 dx0, i64 dy0, int age0, int st0, const i64* qx,
                            const i64* qy, int H) {
        (void)M;
        const i64 R = cf.body_r + cf.dart_r;
        i64 px = dx0, py = dy0;
        for (int j = 1; j <= H; ++j) {
            Dart tmp;
            tmp.st = st0;
            tmp.age = age0 + j - 1;
            i64 step = dart_step_len(cf, tmp);
            if (step <= 0) continue;
            i64 ddx = qx[j] - px, ddy = qy[j] - py;
            i64 dd = ilen(ddx, ddy);
            if (dd <= R) return (H + 2 - j) * 100 / (H + 2);
            if (dd <= step) { px = qx[j]; py = qy[j]; }
            else { px += ddx * step / dd; py += ddy * step / dd; }
            if (ilen(px - qx[j], py - qy[j]) <= R) return (H + 2 - j) * 100 / (H + 2);
        }
        return 0;
    }

    // Counter-throw risk: foe at E throws perfectly predicted dart at my path q[0..H].
    static int counter_risk(const Config& cf, const Map& M, i64 ex, i64 ey, const i64* qx, const i64* qy, int H,
                            int shift) {
        const i64 R = cf.body_r + cf.dart_r;
        int worst = 0;
        for (int m = 1; m <= 14; ++m) {
            int j = 1 + m + shift;
            if (j > H) j = H;
            i64 cum = cum_out(cf, m);
            if (cum > cf.range + R) break;
            i64 dx = qx[j] - ex, dy = qy[j] - ey;
            if (ilen(dx, dy) > cum + R) continue;
            i64 ux, uy;
            if (!scale_to(dx, dy, 1000, ux, uy)) continue;
            if (ilen(ex + ux * cum / 1000 - qx[j], ey + uy * cum / 1000 - qy[j]) > R) continue;
            if (wall_between(M, ex, ey, ex + ux * cum / 1000, ey + uy * cum / 1000)) continue;
            int sev = (H + 2 - j) * 100 / (H + 2);
            if (sev > worst) worst = sev;
        }
        return worst;
    }

    int cell_fall(int cx, int cy) const {
        if (cx < 0 || cy < 0 || cx >= nx || cy >= ny) return 0;
        return fall[(size_t)cy * nx + cx];
    }
    // Rebuild present world: restore my state D steps ago, replay my real actions.
    bool rebuild(const Obs& o, Game& W) {
        const Config& cf = gi0.cfg;
        int D = (int)cf.delay;
        W = Game(gi0, o);
        int me = o.seat;
        if (o.t <= D || hrec <= (int)o.t - D) {
            // Not enough history: crude extrapolation of foes and their darts.
            for (int k = 0; k < o.n; ++k) {
                if (k == me || !W.p[k].alive || W.p[k].unknown) continue;
                W.p[k].x += W.p[k].vx * D;
                W.p[k].y += W.p[k].vy * D;
                W.p[k].px = W.p[k].x;
                W.p[k].py = W.p[k].y;
            }
            for (int k = 0; k < o.n; ++k) {
                Dart& d = W.d[k];
                if (d.st == D_OUT) {
                    for (int s = 0; s < D; ++s) {
                        i64 v = dart_step_len(cf, d);
                        i64 nx2 = d.x + d.vx * v / 1000, ny2 = d.y + d.vy * v / 1000;
                        if (wall_between(W.M(), d.x, d.y, nx2, ny2)) break;
                        d.x = nx2; d.y = ny2; d.age++; d.left -= v;
                        if (d.left <= 0) { d.left = 0; d.st = D_BACK; d.age = 0; break; }
                    }
                } else if (d.st == D_BACK || d.st == D_CALL) {
                    for (int s = 0; s < D; ++s) {
                        i64 v = dart_step_len(cf, d);
                        if (v <= 0) { d.age++; continue; }
                        i64 dx = W.p[k].x - d.x, dy = W.p[k].y - d.y;
                        i64 dd = ilen(dx, dy);
                        if (dd <= v || dd <= cf.catch_r) {
                            d.st = D_HAND; d.x = W.p[k].x; d.y = W.p[k].y; break;
                        }
                        d.x += dx * v / dd; d.y += dy * v / dd; d.age++;
                    }
                }
            }
            return true;
        }
        int base = (int)o.t - D;
        Snap& s = hist[base];
        Player& P = W.p[me];
        P.x = s.x; P.y = s.y; P.vx = s.vx; P.vy = s.vy; P.face = s.face;
        P.st = s.st; P.timer = s.timer; P.cd = s.cd; P.px = s.x; P.py = s.y;
        Dart& md = W.d[me];
        md.st = s.dst; md.x = s.dx; md.y = s.dy; md.vx = s.dvx; md.vy = s.dvy;
        md.left = s.dleft; md.age = s.dage;
        Action acts[MAXP];
        for (int step = base; step < (int)o.t; ++step) {
            for (int k = 0; k < W.n; ++k) acts[k] = Action();
            acts[me] = hist[step].a;
            acts[me].msg = 0;
            for (int k = 0; k < W.n; ++k) {
                if (k == me || !W.p[k].alive || W.p[k].unknown) continue;
                sustain_input(cf, W.p[k], acts[k].mx, acts[k].my);
            }
            W.step(acts);
            if (!W.p[me].alive) return false;
        }
        return W.p[me].alive;
    }

    struct Cand { i64 mx, my; int act; i64 ax, ay; };

    static bool MD_HAD(const Game& W, int me) { return W.d[me].st == D_HAND; }

    // Current lethal threat? (a flying dart hitting me within ~3 steps if I stay)
    bool threat_now(const Game& W, int me) {
        const Config& cf = gi0.cfg;
        const i64 R = cf.body_r + cf.dart_r;
        for (int k = 0; k < W.n; ++k) {
            if (k == me || !W.p[k].alive || W.p[k].unknown) continue;
            const Dart& d = W.d[k];
            if (!dart_flying(d.st)) continue;
            i64 px = d.x, py = d.y, vx, vy;
            if (d.st == D_OUT) {
                i64 v = dart_step_len(cf, d);
                vx = d.vx * v / 1000; vy = d.vy * v / 1000;
            } else { vx = d.vx; vy = d.vy; }
            for (int s = 0; s < 3; ++s) {
                if (seg_near_origin(px - W.p[me].x, py - W.p[me].y,
                                    px + vx - W.p[me].x, py + vy - W.p[me].y, R)) return true;
                px += vx; py += vy;
            }
        }
        return false;
    }

    // Safest of 8 directions (standability + dart distance), for locked states.
    bool safe_dir(const Game& W, int me, i64& dx, i64& dy) {
        const Config& cf = gi0.cfg;
        i64 best = -(1LL << 60);
        bool found = false;
        for (int d = 0; d < 8; ++d) {
            i64 vx = Game::icos_face(d * 4500), vy = Game::isin_face(d * 4500);
            i64 px = W.p[me].x + vx * cf.run_v * 4 / 1000, py = W.p[me].y + vy * cf.run_v * 4 / 1000;
            if (!standable(gi0, px, py, W.t + 4)) continue;
            i64 worst = (1LL << 60);
            for (int k = 0; k < W.n; ++k) {
                if (k == me || !W.p[k].alive || W.p[k].unknown) continue;
                const Dart& dd = W.d[k];
                if (!dart_flying(dd.st)) continue;
                i64 dvx, dvy;
                if (dd.st == D_OUT) {
                    i64 v = dart_step_len(cf, dd);
                    dvx = dd.vx * v / 1000; dvy = dd.vy * v / 1000;
                } else { dvx = dd.vx; dvy = dd.vy; }
                i64 mn = (1LL << 60);
                for (int s = 1; s <= 6; ++s) {
                    i64 qx = px + vx * cf.run_v * (s - 4) / 1000, qy = py + vy * cf.run_v * (s - 4) / 1000;
                    mn = imin(mn, ilen(dd.x + dvx * s - qx, dd.y + dvy * s - qy));
                }
                worst = imin(worst, mn);
            }
            if (worst > best) { best = worst; dx = vx; dy = vy; found = true; }
        }
        return found;
    }

    // Non-free states.
    Action locked_move(const Obs& o, const Game& W) {
        int me = o.seat;
        const Player& P = W.p[me];
        const Config& cf = gi0.cfg;
        Action a;
        if (P.st == S_DASH && P.timer > cf.dash_post && P.timer <= cf.dash_t + cf.dash_post &&
            W.d[me].st == D_HAND) {
            int tgt = -1;
            i64 bd = 0;
            for (int k = 0; k < W.n; ++k) {
                if (k == me || !W.p[k].alive || W.p[k].unknown || gi0.team[k] == gi0.team[me]) continue;
                i64 dd = dist2(P.x, P.y, W.p[k].x, W.p[k].y);
                if (tgt < 0 || dd < bd) { tgt = k; bd = dd; }
            }
            if (tgt >= 0 && bd < cf.slash_reach + 1200 && !wall_between(W.M(), P.x, P.y, W.p[tgt].x, W.p[tgt].y)) {
                a.act = A_MELEE; a.ax = W.p[tgt].x - P.x; a.ay = W.p[tgt].y - P.y;
                return a;
            }
        }
        if (P.st == S_RECALL && threat_now(W, me)) {
            i64 dx, dy;
            if (safe_dir(W, me, dx, dy)) { a.mx = dx; a.my = dy; }
            return a;
        }
        {
            i64 dx, dy;
            if (safe_dir(W, me, dx, dy)) { a.mx = dx; a.my = dy; }
        }
        return a;
    }

    Action no_enemy(const Obs& o, const Game& W) {
        int me = o.seat;
        const Config& cf = gi0.cfg;
        Action a;
        const Dart& md = W.d[me];
        i64 gx = gi0.M().width() / 2, gy = gi0.M().height() / 2;
        if (md.st == D_GROUND) { gx = md.x; gy = md.y; }
        {
            int cx = cell_index(W.p[me].x), cy = cell_index(W.p[me].y);
            int f = cell_fall(cx, cy);
            if (f >= 0 && (i64)f - W.t < 60) {
                i64 bv = -1;
                for (int y = 0; y < ny; ++y)
                    for (int x = 0; x < nx; ++x) {
                        if (!standable(gi0, cell_center(x), cell_center(y), W.t)) continue;
                        int ff = cell_fall(x, y);
                        i64 v = (ff < 0 ? 100000 : ff) * 100 - dist2(cell_center(x), cell_center(y), W.p[me].x, W.p[me].y) / 100;
                        if (v > bv) { bv = v; gx = cell_center(x); gy = cell_center(y); }
                    }
            }
        }
        i64 wx, wy;
        if (path_next(gi0, W.p[me].x, W.p[me].y, gx, gy, W.t, wx, wy)) {
            a.mx = wx - W.p[me].x; a.my = wy - W.p[me].y;
        }
        if (cf.recall && md.st == D_GROUND && W.p[me].st == S_FREE && dist2(W.p[me].x, W.p[me].y, md.x, md.y) > 4500) {
            Action r;
            r.act = A_RECALL;
            return r;
        }
        return a;
    }

    void eval_cand(const Obs& o, const Game& W0, int tgt, const int* foes, int nfoe, i64 mvx, i64 mvy, int act,
                   i64 ax, i64 ay, int H, long long& work, i64& best_sc, Cand& best, i64 bonus = 0) {
        const Config& cf = gi0.cfg;
        const Map& M = gi0.M();
        int me = o.seat;
        Game G = W0;
        ++work;
        Action acts[MAXP];
        for (int k = 0; k < G.n; ++k) acts[k] = Action();
        acts[me].mx = mvx; acts[me].my = mvy; acts[me].act = act; acts[me].ax = ax; acts[me].ay = ay;
        for (int k = 0; k < G.n; ++k) {
            if (k == me || !G.p[k].alive || G.p[k].unknown) continue;
            sustain_input(cf, G.p[k], acts[k].mx, acts[k].my);
        }
        i64 qx[16], qy[16];
        qx[0] = G.p[me].x; qy[0] = G.p[me].y;
        i64 sc = bonus;
        bool me_dead = false;
        bool foe_dead[8] = {};
        for (int s = 0; s < H; ++s) {
            if (s > 0) {
                for (int k = 0; k < G.n; ++k) acts[k] = Action();
                acts[me].mx = mvx; acts[me].my = mvy;
                for (int k = 0; k < G.n; ++k) {
                    if (k == me || !G.p[k].alive || G.p[k].unknown) continue;
                    sustain_input(cf, G.p[k], acts[k].mx, acts[k].my);
                }
            }
            G.step(acts);
            ++work;
            qx[s + 1] = G.p[me].x; qy[s + 1] = G.p[me].y;
            if (!G.p[me].alive) { me_dead = true; break; }
            for (int i = 0; i < nfoe; ++i)
                if (!G.p[foes[i]].alive && !foe_dead[i]) foe_dead[i] = true;
        }
        if (me_dead) {
            bool mutual = false;
            for (int i = 0; i < nfoe; ++i)
                if (foe_dead[i]) mutual = true;
            sc = mutual ? -500000 : -(1LL << 55);
            if (sc > best_sc) { best_sc = sc; best = Cand{mvx, mvy, act, ax, ay}; }
            return;
        }
        int kills = 0;
        for (int i = 0; i < nfoe; ++i)
            if (foe_dead[i]) kills++;
        sc += (i64)kills * (W0.n > 2 ? 12000000 : 500000000LL);
        {
            i64 dd = dist2(G.p[me].x, G.p[me].y, G.p[tgt].x, G.p[tgt].y);
            i64 ring = MD_HAD(W0, me) ? 4200 : 6000;
            sc -= iabs(dd - ring) * 2;
            if (W0.n > 2) {
                for (int i = 0; i < nfoe; ++i) {
                    int k = foes[i];
                    if (!G.p[k].alive) continue;
                    i64 d2 = dist2(G.p[me].x, G.p[me].y, G.p[k].x, G.p[k].y);
                    if (d2 < 3500) sc -= (3500 - d2) * 3;
                }
            }
        }
        {
            int f = cell_fall(cell_index(G.p[me].x), cell_index(G.p[me].y));
            if (f >= 0) {
                i64 dt = (i64)f - W0.t;
                if (dt < 40) sc -= (40 - dt) * 500;
            }
        }
        {
            const Dart& md = G.d[me];
            if (md.st == D_GROUND) sc += imax(0, 6000 - dist2(G.p[me].x, G.p[me].y, md.x, md.y));
        }
        for (int i = 0; i < nfoe; ++i) {
            int k = foes[i];
            if (!G.p[k].alive) continue;
            if (W0.d[k].st == D_HAND) {
                if (dist2(W0.p[k].x, W0.p[k].y, W0.p[me].x, W0.p[me].y) > cf.range + 2500) continue;
                int r = (counter_risk(cf, M, W0.p[k].x, W0.p[k].y, qx, qy, H, 0) +
                         counter_risk(cf, M, W0.p[k].x, W0.p[k].y, qx, qy, H, (int)cf.delay)) / 2;
                if (W0.p[k].st != S_FREE) r /= 3;
                sc -= (i64)r * 2000;
            } else if (W0.d[k].st == D_BACK || W0.d[k].st == D_CALL) {
                // His homing dart: if he walks toward me it sweeps my path.
                int r = homing_risk(cf, M, W0.d[k].x, W0.d[k].y, (int)W0.d[k].age, (int)W0.d[k].st, qx, qy, H);
                // Only matters if he is close enough to steer it onto me.
                if (dist2(W0.p[k].x, W0.p[k].y, W0.p[me].x, W0.p[me].y) > 9000) r /= 4;
                sc -= (i64)r * 3000;
            }
        }
        {
            bool armed = false;
            for (int i = 0; i < nfoe; ++i)
                if (G.p[foes[i]].alive && W0.d[foes[i]].st == D_HAND) { armed = true; break; }
            if (armed && (mvx || mvy)) {
                i64 dx = W0.p[tgt].x - W0.p[me].x, dy = W0.p[tgt].y - W0.p[me].y;
                i64 dd = ilen(dx, dy);
                if (dd > 0) {
                    i64 mm = ilen(mvx, mvy);
                    i64 dot = mm > 0 ? (mvx * dx + mvy * dy) / 1000 : 0;
                    i64 cross = dd * mm - (dot >= 0 ? dot : -dot);
                    if (mm > 0 && cross * 2 > dd * mm) sc += 800;
                }
            }
        }
        if (sc > best_sc) { best_sc = sc; best = Cand{mvx, mvy, act, ax, ay}; }
    }

    Action decide(const Obs& o) {
        const Config& cf = gi0.cfg;
        const Map& M = gi0.M();
        int me = o.seat;
        Action ans;
        if (!o.me().alive) { record(o, ans); return ans; }
        Game W(gi0, o);
        if (!rebuild(o, W)) W = Game(gi0, o);
        const Player& P = W.p[me];
        const Dart& MD = W.d[me];
        if (P.st != S_FREE) { ans = locked_move(o, W); record(o, ans); return ans; }
        int foes[8], nfoe = 0;
        for (int i = 0; i < W.n; ++i) {
            int k = (me + 1 + i) % W.n;
            if (k == me || !W.p[k].alive || W.p[k].unknown || gi0.team[k] == gi0.team[me]) continue;
            foes[nfoe++] = k;
        }
        if (nfoe == 0) { ans = no_enemy(o, W); record(o, ans); return ans; }
        const int cov_need = 1;
        const i64 throw_extra = -1700;
        int tgt = foes[0];
        {
            i64 bv = 0;
            for (int i = 0; i < nfoe; ++i) {
                int k = foes[i];
                i64 v = dist2(W.p[me].x, W.p[me].y, W.p[k].x, W.p[k].y);
                if (W.n > 2 && (W.p[k].st != S_FREE || !armed_soon(W.p[k], W.d[k], W.p[k].x, W.p[k].y))) v -= 1200;
                if (i == 0 || v < bv) { bv = v; tgt = k; }
            }
        }
        i64 dirs[16][2];
        int ndir = 0;
        for (int d = 0; d < 8; ++d) {
            dirs[ndir][0] = Game::icos_face(d * 4500);
            dirs[ndir][1] = Game::isin_face(d * 4500);
            ndir++;
        }
        {
            i64 ux, uy;
            if (scale_to(W.p[tgt].x - P.x, W.p[tgt].y - P.y, 1000, ux, uy)) {
                dirs[ndir][0] = ux; dirs[ndir][1] = uy; ndir++;
                dirs[ndir][0] = -ux; dirs[ndir][1] = -uy; ndir++;
                dirs[ndir][0] = -uy; dirs[ndir][1] = ux; ndir++;
                dirs[ndir][0] = uy; dirs[ndir][1] = -ux; ndir++;
            }
            i64 wx, wy, gx = W.p[tgt].x, gy = W.p[tgt].y;
            if (MD.st == D_GROUND) { gx = MD.x; gy = MD.y; }
            if (path_next(gi0, P.x, P.y, gx, gy, W.t, wx, wy) && (wx != P.x || wy != P.y)) {
                scale_to(wx - P.x, wy - P.y, 1000, ux, uy);
                dirs[ndir][0] = ux; dirs[ndir][1] = uy; ndir++;
            }
        }
        long long bud = W.n > 2 ? 3500 : 5500;
        long long work = 0;
        i64 cpu0 = thread_cpu_100ns();
        const int H = 10;
        i64 best_sc = -(1LL << 60);
        Cand best{0, 0, 0, 0, 0};
        int rot = ndir > 0 ? (me * 5) % ndir : 0;
        for (int pass = 0; pass < 2 && work < bud; ++pass) {
            for (int di = 0; di < ndir && work < bud; ++di) {
                int d = (rot + di) % ndir;
                if ((work & 255) == 0 && cpu0 >= 0) {
                    i64 c = thread_cpu_100ns();
                    if (c >= 0 && c - cpu0 > 60000) break;
                }
                eval_cand(o, W, tgt, foes, nfoe, dirs[d][0], dirs[d][1], 0, 0, 0, H, work, best_sc, best);
                if (work >= bud) break;
                if (MD.st == D_HAND) {
                    // Release point: 2 steps along this move dir (throw_pre).
                    // Aim from there, not from here.
                    i64 rx = P.x + dirs[d][0] * cf.run_v * cf.throw_move / 100 * 2 / 1000;
                    i64 ry = P.y + dirs[d][1] * cf.run_v * cf.throw_move / 100 * 2 / 1000;
                    for (int ai = 0; ai < nfoe && work < bud; ++ai) {
                        int k = (ai == 0) ? tgt : foes[(ai + (tgt % imax(1, nfoe))) % nfoe];
                        if (pass == 0 && k != tgt) continue;
                        i64 ax, ay;
                        int mh;
                        // Foe keeps velocity during my 2-step windup, then dart flies.
                        i64 fx = W.p[k].x + W.p[k].vx * 2, fy = W.p[k].y + W.p[k].vy * 2;
                        if (!intercept_throw(cf, M, rx, ry, fx, fy, W.p[k].vx, W.p[k].vy, 0, 20, ax, ay, mh))
                            continue;
                        if (dist2(rx, ry, fx, fy) > cf.range + throw_extra) continue;
                        int lock = W.p[k].st != S_FREE ? (int)W.p[k].timer : 0;
                        int cov = coverage(cf, M, rx, ry, fx, fy, W.p[k].vx, W.p[k].vy, lock);
                        if (pass == 1 && cov < cov_need + 1) continue;
                        if (cov < cov_need) continue;
                        i64 tdd = dist2(rx, ry, fx, fy);
                        i64 tb = (i64)cov * 240000 / (1 + tdd / 1000);
                        if (tdd < 4500) tb += 600000;
                        eval_cand(o, W, tgt, foes, nfoe, dirs[d][0], dirs[d][1], 1, ax, ay, H, work, best_sc, best,
                                  tb);
                        if (pass == 0) break;  // only tgt in pass 0
                    }
                }
                if (work >= bud) break;
                {
                    i64 mdx = W.p[tgt].x - P.x, mdy = W.p[tgt].y - P.y;
                    i64 dd = dist2(P.x, P.y, W.p[tgt].x, W.p[tgt].y);
                    i64 reach = MD.st == D_HAND ? cf.slash_reach : cf.kick_reach;
                    if (dd < reach + 400 && !wall_between(M, P.x, P.y, W.p[tgt].x, W.p[tgt].y))
                        eval_cand(o, W, tgt, foes, nfoe, dirs[d][0], dirs[d][1], 2, mdx, mdy, H, work, best_sc, best);
                    for (int i = 0; i < nfoe && work < bud; ++i) {
                        int k = foes[i];
                        const Dart& ed = W.d[k];
                        if (!dart_flying(ed.st) || MD.st != D_HAND) continue;
                        i64 npx = ed.x, npy = ed.y;
                        if (ed.st == D_OUT) {
                            i64 v = dart_step_len(cf, ed);
                            npx = ed.x + ed.vx * v / 1000;
                            npy = ed.y + ed.vy * v / 1000;
                        }
                        i64 rdx = npx - P.x, rdy = npy - P.y;
                        if (ilen(rdx, rdy) > cf.slash_reach) continue;
                        i64 sux, suy;
                        if (!scale_to(rdx, rdy, 1000, sux, suy)) continue;
                        if (ilen(rdx, rdy) > cf.body_r && rdx * sux + rdy * suy < cf.block_cos * ilen(rdx, rdy))
                            continue;
                        if (wall_between(M, P.x, P.y, npx, npy)) continue;
                        eval_cand(o, W, tgt, foes, nfoe, 0, 0, 2, rdx, rdy, H, work, best_sc, best);
                        break;
                    }
                }
                if (work >= bud) break;
                if (P.cd == 0 && (pass == 1 || di < 4)) {
                    // Dash landing must be standable (dash end + 1 step ahead).
                    i64 lx = P.x + dirs[d][0] * cf.dash_v * cf.dash_t / 1000;
                    i64 ly = P.y + dirs[d][1] * cf.dash_v * cf.dash_t / 1000;
                    i64 ax2 = lx + dirs[d][0] * 300 / 1000, ay2 = ly + dirs[d][1] * 300 / 1000;
                    if (standable(gi0, lx, ly, W.t + cf.dash_pre + cf.dash_t + 1) &&
                        standable(gi0, ax2, ay2, W.t + cf.dash_pre + cf.dash_t + 2))
                        eval_cand(o, W, tgt, foes, nfoe, dirs[d][0], dirs[d][1], 3, dirs[d][0], dirs[d][1], H, work,
                                  best_sc, best);
                }
                if (work >= bud) break;
                if (MD.st == D_GROUND && pass == 0 && di == 0) {
                    // Don't start recall with an armed foe nearby.
                    bool armed_near = false;
                    for (int i = 0; i < nfoe; ++i) {
                        int k = foes[i];
                        if (!W.p[k].alive) continue;
                        if (W.d[k].st == D_HAND && dist2(P.x, P.y, W.p[k].x, W.p[k].y) < 6500) {
                            armed_near = true;
                            break;
                        }
                    }
                    if (!armed_near)
                        eval_cand(o, W, tgt, foes, nfoe, 0, 0, 4, 0, 0, H, work, best_sc, best);
                }
            }
        }
        ans.mx = best.mx; ans.my = best.my; ans.act = best.act; ans.ax = best.ax; ans.ay = best.ay;
        if (ans.act != 3 && (ans.mx || ans.my)) {
            for (int k = 1; k <= 3; ++k) {
                i64 vx, vy;
                scale_to(ans.mx, ans.my, cf.run_v * k, vx, vy);
                if (cell_of_point(cf, M, o.me().x + vx, o.me().y + vy, o.t + k) == C_PIT) {
                    ans.mx = ans.my = 0;
                    if (ans.act == 1 || ans.act == 2) { ans.act = 0; ans.ax = ans.ay = 0; }
                    break;
                }
            }
        }
        record(o, ans);
        return ans;
    }

    void record(const Obs& o, const Action& a) {
        if (o.t < 0 || o.t >= HS) return;
        Snap& s = hist[o.t];
        const Player& P = o.me();
        const Dart& D = o.my_dart();
        s.x = P.x; s.y = P.y; s.vx = P.vx; s.vy = P.vy; s.face = P.face;
        s.st = P.st; s.timer = P.timer; s.cd = P.cd;
        s.dx = D.x; s.dy = D.y; s.dvx = D.vx; s.dvy = D.vy;
        s.dleft = D.left; s.dage = D.age; s.dst = D.st;
        s.a = a;
        if (o.t + 1 > hrec) hrec = (int)o.t + 1;
    }
// __P4__
};

DART_MAIN(Hunter)
