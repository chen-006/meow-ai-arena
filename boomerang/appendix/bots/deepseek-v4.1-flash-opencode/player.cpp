// dartfu player - prediction + rollout search
#include "dart.hpp"
#include <cmath>
#include <chrono>
using namespace dart;

struct Bot {
    static const int HB = 128;
    struct Hist { i64 t = -1; Player me; Dart md; Action act; };
    Hist hist[HB];

    const GameInfo* GI = nullptr;

    void init(const GameInfo& g) { GI = &g; }

    const Config& C() const { return GI->cfg; }
    int N() const { return GI->n; }
    int SEAT() const { return GI->seat; }

    // ------------------------------------------------------------
    // basics
    static i64 dist2(const Player& a, const Player& b) {
        const i64 dx = a.x - b.x, dy = a.y - b.y;
        return dx * dx + dy * dy;
    }
    bool hostile(const Game& g, int a, int b) const {
        return g.p[a].team != g.p[b].team;
    }
    int nearestEnemy(const Game& g, int k, i64* outd = nullptr, bool visible_only = false) const {
        int best = -1; i64 bd = 0;
        for (int i = 0; i < g.n; ++i) {
            if (i == k || !g.p[i].alive || !hostile(g, k, i)) continue;
            if (visible_only && g.p[i].unknown) continue;
            const i64 d = ilen(g.p[i].x - g.p[k].x, g.p[i].y - g.p[k].y);
            if (best < 0 || d < bd) { best = i; bd = d; }
        }
        if (outd) *outd = bd;
        return best;
    }

    // how many outbound flight steps until the dart has covered d mm
    int flightSteps(i64 d) const {
        const Config& cf = C();
        if (d <= 0) return 1;
        i64 cum = 0;
        for (int age = 0; age < 80; ++age) {
            i64 len = imax(cf.dart_vmin, cf.dart_v - age * cf.dart_dec);
            if (cum + len >= d) return age + 1;
            cum += len;
            if (cum >= cf.range) return -1;
        }
        return -1;
    }

    // approximate future point of a player k steps ahead (used only for aiming)
    void futurePos(const Player& P, int steps, i64& x, i64& y) const {
        i64 vx = P.vx, vy = P.vy;
        if (P.st == S_DASH) {
            vx = rdiv(P.ax * C().dash_v, 1000);
            vy = rdiv(P.ay * C().dash_v, 1000);
            int moved = 0;
            x = P.x; y = P.y;
            int pre = 0, mov = 0, post = 0;
            // timer tells which part is the next step
            for (int i = 0; i < steps; ++i) {
                i64 timer = P.timer - i;
                if (timer > C().dash_t + C().dash_post) { pre++; }
                else if (timer > C().dash_post) { mov++; x += vx; y += vy; }
                else { post++; }
            }
            (void)moved;
            return;
        }
        if (P.st != S_FREE && P.st != S_THROW && P.st != S_THROWN && P.st != S_STUN) { vx = vy = 0; }
        if (P.st == S_STUN) {
            x = P.x; y = P.y;
            i64 sx = vx, sy = vy;
            for (int i = 0; i < steps; ++i) {
                x += sx; y += sy;
                sx = rdiv(sx * C().kick_keep, 1000);
                sy = rdiv(sy * C().kick_keep, 1000);
            }
            return;
        }
        if (P.st == S_SLASH || P.st == S_SLASHED || P.st == S_KICK || P.st == S_KICKED || P.st == S_RECALL) {
            // pct 0, velocity decays
            i64 sx = vx, sy = vy;
            x = P.x; y = P.y;
            for (int i = 0; i < steps; ++i) {
                i64 dx = -sx, dy = -sy;
                cap_len(dx, dy, C().acc);
                sx += dx; sy += dy;
                x += sx; y += sy;
            }
            return;
        }
        x = P.x + vx * steps;
        y = P.y + vy * steps;
    }

    // aim a throw from player P at target T with lead
    void leadAim(const Player& P, const Player& T, i64 lead_scale, i64& ax, i64& ay) const {
        // release position: after throw_pre steps at 50% speed
        i64 rx, ry;
        futurePos(P, int(C().throw_pre), rx, ry);
        i64 tx = T.x, ty = T.y;
        for (int it = 0; it < 3; ++it) {
            const i64 d = ilen(tx - rx, ty - ry);
            int fs = flightSteps(d);
            if (fs < 0) fs = 20;
            int k = int(C().throw_pre) + fs;
            futurePos(T, k, tx, ty);
            if (lead_scale != 1000) {
                tx = T.x + rdiv((tx - T.x) * lead_scale, 1000);
                ty = T.y + rdiv((ty - T.y) * lead_scale, 1000);
            }
        }
        ax = tx - rx; ay = ty - ry;
        if (ax == 0 && ay == 0) { ax = 1; }
    }

    // true if a flying dart is an immediate threat to P, returns a direction
    // to sidestep it
    bool incoming(const Game& g, int k, i64& dx, i64& dy) const {
        const Player& P = g.p[k];
        for (int i = 0; i < g.n; ++i) {
            if (i == k) continue;
            const Dart& D = g.d[i];
            if (!dart_flying(D.st)) continue;
            if (g.p[i].team == P.team) continue;
            const i64 rx = P.x - D.x, ry = P.y - D.y;
            const i64 rd = ilen(rx, ry);
            if (rd > 3200) continue;
            i64 vx = D.vx, vy = D.vy;
            if (D.st == D_OUT) {
                i64 ux = 0, uy = 0;
                if (!scale_to(D.vx, D.vy, 1000, ux, uy)) continue;
                const i64 sp = dart_step_len(C(), D);
                vx = rdiv(ux * sp, 1000); vy = rdiv(uy * sp, 1000);
            } else {
                // returning dart: use last displacement, or head towards owner
                if (vx == 0 && vy == 0) {
                    vx = g.p[i].x - D.x; vy = g.p[i].y - D.y;
                }
            }
            const i64 vv = vx * vx + vy * vy;
            if (vv < 100) continue;
            // closing?
            if (vx * rx + vy * ry <= 0) continue;
            const i64 t = (rx * vx + ry * vy);  // proportional to time to closest
            if (t > vv * 7) continue;            // far in the future
            dx = -vy; dy = vx;
            return true;
        }
        return false;
    }

    // steer a movement direction away from an immediately bad cell
    void steer(const Game& g, i64 x, i64 y, i64& dx, i64& dy) const {
        i64 ux = 0, uy = 0;
        if (!scale_to(dx, dy, 1000, ux, uy)) { dx = 0; dy = 0; return; }
        const int NT = 16;
        static const int turnUnits[NT] = {0, 2250, -2250, 4500, -4500, 6750, -6750, 9000, -9000,
                                          11250, -11250, 13500, -13500, 15750, -15750, 18000};
        for (int i = 0; i < NT; ++i) {
            const i64 a = turnUnits[i];
            const i64 c = Game::icos_face(a), s = Game::isin_face(a);
            const i64 rx = rdiv(ux * c - uy * s, 1000);
            const i64 ry = rdiv(ux * s + uy * c, 1000);
            bool ok = true;
            for (int m = 1; m <= 3; ++m) {
                const i64 px = x + rx * m, py = y + ry * m;
                if (!standable(*GI, px, py, g.t)) { ok = false; break; }
            }
            if (ok) {
                dx = rx; dy = ry;
                return;
            }
        }
        dx = 0; dy = 0;
    }

    // ------------------------------------------------------------
    // model of other players, used both for reconstructing the current
    // position and for rollouts
    Action modelAction(const Game& g, int k, bool allowThrow = true, bool dodge = true) const {
        Action a;
        const Player& P = g.p[k];
        if (!P.alive || P.unknown) return a;
        i64 bd = 0;
        int tgt = nearestEnemy(g, k, &bd);
        const Dart& D = g.d[k];
        auto mv = [&](i64 dx, i64 dy) {
            steer(g, P.x, P.y, dx, dy);
            a.mx = dx; a.my = dy;
        };

        if (P.st == S_FREE) {
            i64 sx, sy;
            if (dodge && incoming(g, k, sx, sy)) {
                // sidestep, prefer the side away from the dart's path
                if (sx * (P.x - g.p[0].x) + sy * (P.y - g.p[0].y) < 0) { sx = -sx; sy = -sy; }
                mv(sx, sy);
                return a;
            }
            if (allowThrow && D.st == D_HAND && tgt >= 0) {
                const Player& T = g.p[tgt];
                if (bd < 6400 && !wall_between(g.M(), P.x, P.y, T.x, T.y)) {
                    i64 ax, ay;
                    leadAim(P, T, 1000, ax, ay);
                    a.act = A_THROW; a.ax = ax; a.ay = ay;
                    // still drift a little toward the target while winding up
                    if (bd > 3000) mv(T.x - P.x, T.y - P.y);
                    return a;
                }
            }
            if (D.st == D_GROUND) {
                const i64 dd = ilen(D.x - P.x, D.y - P.y);
                if (dd < 5000) {
                    mv(D.x - P.x, D.y - P.y);
                    return a;
                }
                if (tgt < 0 || bd > 5200) { a.act = A_RECALL; return a; }
            }
            if (tgt >= 0) {
                const Player& T = g.p[tgt];
                i64 dx = T.x - P.x, dy = T.y - P.y;
                if (bd <= 1620) {
                    a.act = A_MELEE; a.ax = dx; a.ay = dy; return a;
                }
                if (bd > 4200) {
                    mv(dx, dy);
                } else if (bd < 2500) {
                    mv(-dx, -dy);
                } else {
                    mv(-dy, dx);   // strafe
                }
                return a;
            }
            // no target: drift toward center
            mv(g.M().width() / 2 - P.x, g.M().height() / 2 - P.y);
            return a;
        }
        if (P.st == S_THROW || P.st == S_THROWN) {
            if (tgt >= 0 && bd < 2600) {
                mv(P.x - g.p[tgt].x, P.y - g.p[tgt].y);
            } else if (tgt >= 0) {
                mv(g.p[tgt].x - P.x, g.p[tgt].y - P.y);
            }
            return a;
        }
        if (P.st == S_DASH && g.dash_moving(P)) {
            if (tgt >= 0 && bd < 3000) {
                a.act = A_MELEE; a.ax = g.p[tgt].x - P.x; a.ay = g.p[tgt].y - P.y;
            }
        }
        return a;
    }

    // enemies that fall into a pit in a rollout would be model errors, not a
    // real opportunity (competent opponents do not walk into lakes).  Put
    // them back where they were.
    void noModelSuicide(Game& g, int seat, const Player* prev, bool* was_alive) const {
        for (int k = 0; k < g.n; ++k) {
            if (k == seat) continue;
            if (was_alive[k] && !g.p[k].alive && g.p[k].cause == K_FALL) {
                g.p[k] = prev[k];
                g.p[k].alive = 1;
                g.p[k].st = S_FREE;
                g.p[k].timer = 0;
                g.p[k].vx = g.p[k].vy = 0;
                g.p[k].dead_t = -1;
                g.p[k].cause = K_NONE;
                g.p[k].killer = -1;
                g.over = false;
                g.winner = -2;
            }
        }
    }

    // ------------------------------------------------------------
    // rebuild the world at the current step:
    // the observation holds others 4 steps in the past; start there with our
    // own recorded state/actions, then simulate forward to now.
    Game predict(const Obs& o) {
        Game g(*GI, o);
        const int D = int(C().delay);
        i64 ts = (o.t > D) ? o.t - D : 0;
        g.t = ts;
        Hist& hs = hist[ts % HB];
        if (hs.t == ts) {
            g.p[o.seat] = hs.me;
            g.d[o.seat] = hs.md;
            g.p[o.seat].px = hs.me.x; g.p[o.seat].py = hs.me.y;
        }
        while (g.t < o.t) {
            Action acts[MAXP];
            const i64 tt = g.t;
            Player prev[MAXP];
            bool wasAlive[MAXP];
            for (int k = 0; k < g.n; ++k) { prev[k] = g.p[k]; wasAlive[k] = g.p[k].alive != 0; }
            for (int k = 0; k < g.n; ++k) {
                if (k == o.seat) {
                    Hist& h = hist[tt % HB];
                    if (h.t == tt) acts[k] = h.act;
                    else acts[k] = Action();
                } else {
                    acts[k] = modelAction(g, k);
                }
            }
            g.step(acts);
            noModelSuicide(g, o.seat, prev, wasAlive);
            if (g.over) g.over = false;
        }
        // our own current state is known exactly
        g.p[o.seat] = o.me();
        g.d[o.seat] = o.my_dart();
        g.p[o.seat].unknown = 0;
        g.t = o.t;
        g.over = false;
        g.winner = -2;
        return g;
    }

    // ------------------------------------------------------------
    // policy used for our own future steps inside a rollout
    Action selfPolicy(const Game& g, int seat, const Action& first, bool is_first,
                      i64 dirx, i64 diry) const {
        if (is_first) return first;
        Action a;
        const Player& P = g.p[seat];
        if (!P.alive) return a;
        const Dart& D = g.d[seat];
        i64 bd = 0;
        int tgt = nearestEnemy(g, seat, &bd);

        if (P.st == S_FREE) {
            i64 sx, sy;
            if (incoming(g, seat, sx, sy)) {
                // future steps in a rollout behave like a sane dodger
                if (sx * (P.x - g.M().width() / 2) + sy * (P.y - g.M().height() / 2) < 0) { sx = -sx; sy = -sy; }
                steer(g, P.x, P.y, sx, sy);
                a.mx = sx; a.my = sy;
                return a;
            }
            if (D.st == D_GROUND) {
                const i64 dd = ilen(D.x - P.x, D.y - P.y);
                if (dd <= C().pick_r) { a.mx = D.x - P.x; a.my = D.y - P.y; return a; }
                if (tgt < 0 || bd > 5200) { a.act = A_RECALL; return a; }
                a.mx = D.x - P.x; a.my = D.y - P.y;
                return a;
            }
            if (tgt >= 0) {
                const Player& T = g.p[tgt];
                i64 dx = T.x - P.x, dy = T.y - P.y;
                if (D.st == D_HAND && bd <= 1650) {
                    a.act = A_MELEE; a.ax = dx; a.ay = dy; return a;
                }
            }
        }
        if (P.st == S_DASH && g.dash_moving(P) && tgt >= 0 && bd < 2800) {
            a.act = A_MELEE;
            a.ax = g.p[tgt].x - P.x; a.ay = g.p[tgt].y - P.y;
            return a;
        }
        // avoid running into a pit that is already open right in front of us
        steer(g, P.x, P.y, dirx, diry);
        a.mx = dirx; a.my = diry;
        return a;
    }

    // ------------------------------------------------------------
    // terminal evaluation (from our point of view)
    double terminalScore(const Game& g0, const Game& g, int seat) const {
        const Player& P = g.p[seat];
        if (!P.alive) {
            // a simultaneous last death is a draw in 1v1, still bad but less so
            int others = 0;
            for (int k = 0; k < g.n; ++k) if (k != seat && g.p[k].alive) others += hostile(g, seat, k);
            double s = -160000.0;
            if (C().mode == M_DUEL) s += (others == 0 ? 60000.0 : 0.0);
            else s += 4000.0 * (g.n - 1 - others);   // rank benefit
            return s;
        }
        double s = 1000.0;
        int othersAlive = 0;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat) continue;
            if (!hostile(g, seat, k)) continue;
            if (g.p[k].alive) othersAlive++;
            else {
                s += (C().mode == M_DUEL ? 50000.0 : 5000.0);
#ifdef DBG
                std::cerr << "  [dead " << k << " cause=" << g.p[k].cause << " killer=" << g.p[k].killer
                          << " dt=" << g.p[k].dead_t << " mepos=" << P.x << "," << P.y
                          << " at=" << g.t << "]\n";
#endif
            }
        }
        if (C().mode == M_DUEL && othersAlive == 0) s += 40000.0;
        s += 800.0 * (P.kills - g0.p[seat].kills);

        // dart state
        const Dart& D = g.d[seat];
        if (D.st == D_HAND) s += 350;
        else if (D.st == D_OUT || D.st == D_BACK) s += 220;
        else if (D.st == D_CALL) s += 180;
        else if (D.st == D_GROUND) {
            s -= 250;
            const i64 dd = ilen(D.x - P.x, D.y - P.y);
            if (dd < 2000) s += 120;
        }

        // collapse safety of the current cell
        const int cx = cell_index(P.x), cy = cell_index(P.y);
        const i64 fall = cell_fall_step(*GI, cx, cy);
        if (fall >= 0) {
            const i64 dt = fall - g.t;
            if (dt <= 12) s -= 2500;
            else if (dt <= 40) s -= 500;
            else if (dt <= 90) s -= 120;
        }

        // wall / pit margin at a few points around the body
        int bad = 0;
        for (int d = 0; d < 8; ++d) {
            const i64 ang = d * 4500;
            const i64 dx = Game::icos_face(ang), dy = Game::isin_face(ang);
            const i64 px = P.x + rdiv(dx * (C().body_r + 250), 1000);
            const i64 py = P.y + rdiv(dy * (C().body_r + 250), 1000);
            if (!standable(*GI, px, py, g.t)) bad++;
        }
        s -= bad * 140.0;

        // spacing preference
        i64 bd = 0;
        int tgt = nearestEnemy(g, seat, &bd);
        if (tgt >= 0) {
            i64 want = (D.st == D_HAND) ? 3300 : 5400;
            const Dart& ED0 = g.d[tgt];
            if (dart_flying(ED0.st)) want = (D.st == D_HAND) ? 4400 : 6600;
            s -= i64(std::llabs((long long)(bd - want))) * 0.14;
            if (C().mode == M_FFA && D.st != D_HAND) {
                int close = 0;
                for (int k = 0; k < g.n; ++k)
                    if (k != seat && g.p[k].alive && hostile(g, seat, k) &&
                        ilen(g.p[k].x - P.x, g.p[k].y - P.y) < 2600) close++;
                s -= close * 350.0;
            }
        }
        // after the collapse starts, prefer the middle
        if (g.t > C().col_start - 60) {
            const i64 dx = P.x - g.M().width() / 2, dy = P.y - g.M().height() / 2;
            s -= ilen(dx, dy) * 0.06;
        }
        return s;
    }

    // ------------------------------------------------------------
    double rollout(const Game& base, int seat, const Action& first, int H,
                   i64 dirx, i64 diry) const {
        Game g = base;
        for (int h = 0; h < H; ++h) {
            Action acts[MAXP];
            Player prev[MAXP];
            bool wasAlive[MAXP];
            for (int k = 0; k < g.n; ++k) { prev[k] = g.p[k]; wasAlive[k] = g.p[k].alive != 0; }
            for (int k = 0; k < g.n; ++k) {
                if (k == seat) acts[k] = selfPolicy(g, seat, first, h == 0, dirx, diry);
                else acts[k] = modelAction(g, k, true, true);
            }
            g.step(acts);
            noModelSuicide(g, seat, prev, wasAlive);
            if (!g.p[seat].alive) return terminalScore(base, g, seat) + h * 180.0;
            if (g.over) break;
        }
        return terminalScore(base, g, seat);
    }

    // ------------------------------------------------------------
    struct Cand { Action a; i64 dx, dy; double score; int kind; };

    Action decide(const Obs& o) {
        const i64 cpu0 = thread_cpu_100ns();
        const auto wall0 = std::chrono::steady_clock::now();
        const int seat = o.seat;
        // remember our own exact state at this step
        {
            Hist& h = hist[o.t % HB];
            h.t = o.t; h.me = o.me(); h.md = o.my_dart(); h.act = Action();
        }
        Action best;
        if (!o.me().alive) {
            remember(o, best);
            return best;
        }
        Game base = predict(o);
        (void)base.LM();   // materialise the current live map once, before copying rollouts

        // Recall is a 12-step standing lock.  Cancel immediately if anyone is
        // close enough to punish it.
        if (o.me().st == S_RECALL) {
            i64 ed = 0;
            int e = nearestEnemy(base, seat, &ed);
            bool bad = (e >= 0 && ed < 7000);
            for (int k = 0; k < base.n && !bad; ++k)
                if (k != seat && base.p[k].alive && hostile(base, seat, k) && dart_flying(base.d[k].st)) bad = true;
            if (bad) {
                Action a;
                if (e >= 0) {
                    a.mx = base.p[seat].x - base.p[e].x;
                    a.my = base.p[seat].y - base.p[e].y;
                    steer(base, base.p[seat].x, base.p[seat].y, a.mx, a.my);
                } else {
                    a.mx = base.p[seat].x - base.M().width() / 2;
                    a.my = base.p[seat].y - base.M().height() / 2;
                }
                remember(o, a);
                return a;
            }
        }

        int H = (C().mode == M_TEAM) ? 18 : (C().mode == M_FFA ? 26 : 30);
        // do not let a rollout cross the next collapse wave: each Game copy
        // would rebuild the whole live map and can blow the 10 ms step budget
        {
            i64 nextCol = -1;
            const Map& MM = base.M();
            for (int w = 0; w < MM.nwave; ++w) {
                const i64 st = C().cstart() + i64(w) * C().cevery();
                if (st > base.t && (nextCol < 0 || st < nextCol)) nextCol = st;
            }
            if (nextCol > base.t) {
                const i64 lim = nextCol - base.t;
                if (lim < H) H = int(imax(1LL, lim));
            }
        }
        Cand cand[96];
        int nc = 0;
        auto add = [&](Action a, i64 dx, i64 dy, int kind) {
            if (nc >= 96) return;
            if (kind == 0) { a.mx = dx; a.my = dy; }
            a.mx = iclamp(a.mx, -1000000, 1000000);
            a.my = iclamp(a.my, -1000000, 1000000);
            a.ax = iclamp(a.ax, -1000000, 1000000);
            a.ay = iclamp(a.ay, -1000000, 1000000);
            cand[nc].a = a; cand[nc].dx = dx; cand[nc].dy = dy;
            cand[nc].score = -1e18; cand[nc].kind = kind; nc++;
        };

        auto safeDir = [&](i64 dx, i64 dy) {
            // avoid blindly stepping onto a cell that is already gone
            i64 ux = 0, uy = 0;
            if (!scale_to(dx, dy, 1000, ux, uy)) { ux = 1; uy = 0; }
            const i64 nx = base.p[seat].x + ux * 3, ny = base.p[seat].y + uy * 3;
            if (!standable(*GI, nx, ny, base.t)) {
                // turn toward the map center
                dx = base.M().width() / 2 - base.p[seat].x;
                dy = base.M().height() / 2 - base.p[seat].y;
            }
            return std::pair<i64, i64>(dx, dy);
        };

        // 1) pure movement candidates
        int firstMove = nc;
        for (int i = 0; i < 16; ++i) {
            i64 dx = Game::icos_face(i * 2250), dy = Game::isin_face(i * 2250);
            add(Action(), dx, dy, 0);
        }
        {
            auto sd = safeDir(-base.p[seat].vx, -base.p[seat].vy);
            add(Action(), sd.first, sd.second, 0);  // continue/stop
            add(Action(), 0, 0, 0);
            // path towards the dart if it is on the ground, else towards the
            // nearest enemy / center
            i64 tx = base.M().width() / 2, ty = base.M().height() / 2;
            const Dart& md = base.d[seat];
            if (md.st == D_GROUND) { tx = md.x; ty = md.y; }
            else {
                int e = nearestEnemy(base, seat);
                if (e >= 0) { tx = base.p[e].x; ty = base.p[e].y; }
            }
            i64 wx, wy;
            if (path_next(*GI, base.p[seat].x, base.p[seat].y, tx, ty, base.t, wx, wy)) {
                add(Action(), wx - base.p[seat].x, wy - base.p[seat].y, 0);
            }
            // when the floor starts disappearing, also offer a route to a cell
            // that survives the next waves (works on unseen maps too)
            if (base.t > C().col_start - 120) {
                const Map& M = base.M();
                int bx = -1, by = -1;
                i64 bscore = 0;
                for (int cy = 0; cy < M.ny; ++cy)
                    for (int cx = 0; cx < M.nx; ++cx) {
                        const i64 f = cell_fall_step(*GI, cx, cy);
                        if (f >= 0 && f - base.t < 260) continue;
                        const i64 px = cell_center(cx), py = cell_center(cy);
                        if (!standable(*GI, px, py, base.t + 30)) continue;
                        const i64 dme = ilen(px - base.p[seat].x, py - base.p[seat].y);
                        const i64 dc = ilen(px - M.width() / 2, py - M.height() / 2);
                        const i64 sc = dme + dc / 2;
                        if (bx < 0 || sc < bscore) { bx = cx; by = cy; bscore = sc; }
                    }
                if (bx >= 0 && path_next(*GI, base.p[seat].x, base.p[seat].y,
                                         cell_center(bx), cell_center(by), base.t, wx, wy)) {
                    add(Action(), wx - base.p[seat].x, wy - base.p[seat].y, 0);
                }
            }
        }
        int nMove = nc - firstMove;

        // evaluate movement candidates
        double bestScore = -1e18;
        int bestIdx = firstMove;
        for (int i = firstMove; i < nc; ++i) {
            cand[i].score = rollout(base, seat, cand[i].a, H, cand[i].dx, cand[i].dy);
            if (cand[i].score > bestScore) { bestScore = cand[i].score; bestIdx = i; }
        }
        best = cand[bestIdx].a;

        // 2) action candidates layered on the better movement directions
        // pick up to 3 movement directions
        int mvIdx[3] = {bestIdx, -1, -1};
        for (int slot = 1; slot < 3; ++slot) {
            double bs = -1e18;
            int bi = -1;
            for (int i = firstMove; i < firstMove + nMove; ++i) {
                bool used = false;
                for (int q = 0; q < slot; ++q) if (mvIdx[q] == i) used = true;
                if (used) continue;
                if (cand[i].score > bs) { bs = cand[i].score; bi = i; }
            }
            mvIdx[slot] = bi;
        }

        const Player& me = base.p[seat];
        const Dart& md = base.d[seat];
        const i64 myTurn = o.t;
        const i64 budget = 60000;  // about 6 ms in 100ns units

        auto overBudget = [&]() {
            const i64 now = thread_cpu_100ns();
            if (cpu0 >= 0 && now >= 0 && now - cpu0 > budget) return true;
            const auto el = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - wall0).count();
            return el > 100000; // only a pathological-stall guard (100 ms)
        };

        // throws at visible enemies
        if (md.st == D_HAND && me.st == S_FREE) {
            for (int e = 0; e < base.n && nc < 92; ++e) {
                if (e == seat || !base.p[e].alive || !hostile(base, seat, e)) continue;
                if (!o.p[e].vis) continue;
                const Player& T = base.p[e];
                const i64 d = ilen(T.x - me.x, T.y - me.y);
                if (d > C().range) continue;
                if (wall_between(base.M(), me.x, me.y, T.x, T.y)) continue;
                for (int lv = 0; lv < 3; ++lv) {
                    i64 ax, ay;
                    leadAim(me, T, lv == 0 ? 1000 : (lv == 1 ? 0 : 500), ax, ay);
                    Action a; a.act = A_THROW; a.ax = ax; a.ay = ay;
                    for (int q = 0; q < 3 && mvIdx[q] >= 0; ++q) {
                        a.mx = cand[mvIdx[q]].dx; a.my = cand[mvIdx[q]].dy;
                        add(a, a.mx, a.my, 1);
                        if (overBudget()) break;
                    }
                    if (overBudget()) break;
                }
                if (overBudget()) break;
            }
        }
        // melee / slash: also aimed at incoming darts (block)
        if (me.st == S_FREE || (me.st == S_DASH && base.dash_moving(me))) {
            for (int e = 0; e < base.n && nc < 95; ++e) {
                if (e == seat || !base.p[e].alive || !hostile(base, seat, e)) continue;
                const Player& T = base.p[e];
                const i64 d = ilen(T.x - me.x, T.y - me.y);
                if (d > 3200) continue;
                i64 fx, fy;
                futurePos(T, int(C().slash_pre), fx, fy);
                Action a; a.act = A_MELEE; a.ax = fx - me.x; a.ay = fy - me.y;
                add(a, 0, 0, 2);
            }
            // block incoming darts
            for (int k = 0; k < base.n && nc < 95; ++k) {
                if (k == seat) continue;
                const Dart& D = base.d[k];
                if (!dart_flying(D.st)) continue;
                const i64 d = ilen(D.x - me.x, D.y - me.y);
                if (d > C().slash_reach + 700) continue;
                Action a; a.act = A_MELEE; a.ax = D.x - me.x; a.ay = D.y - me.y;
                add(a, 0, 0, 3);
            }
        }
        // dash
        if (me.st == S_FREE && me.cd <= 0) {
            for (int i = 0; i < 16; i += 2) {
                i64 dx = Game::icos_face(i * 2250), dy = Game::isin_face(i * 2250);
                Action a; a.act = A_DASH; a.ax = dx; a.ay = dy;
                add(a, dx, dy, 4);
            }
            if (nc < 95) {
                int e = nearestEnemy(base, seat);
                if (e >= 0) {
                    Action a; a.act = A_DASH;
                    a.ax = base.p[e].x - me.x; a.ay = base.p[e].y - me.y;
                    add(a, a.ax, a.ay, 4);
                }
            }
        }
        // recall: in FFA, standing still for 12 steps is usually suicide
        if (me.st == S_FREE && md.st == D_GROUND && nc < 95) {
            bool ok = true;
            if (C().mode == M_FFA) {
                i64 ed = 0;
                int e0 = nearestEnemy(base, seat, &ed);
                if (e0 >= 0 && ed < 6500) ok = false;
                for (int k = 0; k < base.n && ok; ++k)
                    if (k != seat && hostile(base, seat, k) && dart_flying(base.d[k].st)) ok = false;
                if (ilen(md.x - me.x, md.y - me.y) < 3500) ok = false;
            }
            if (ok) {
                Action a; a.act = A_RECALL;
                add(a, 0, 0, 5);
            }
        }

        for (int i = firstMove; i < nc; ++i) {
            if (i < firstMove + nMove) continue;
            if (overBudget()) break;
            cand[i].score = rollout(base, seat, cand[i].a, H, cand[i].dx, cand[i].dy);
            if (cand[i].score > bestScore) { bestScore = cand[i].score; best = cand[i].a; bestIdx = i; }
        }

#ifdef DBG
        if (o.t == 20 || o.t == 21) {
            for (int i = 0; i < nc; ++i)
                std::cerr << "  cand " << i << " kind=" << cand[i].kind << " act=" << cand[i].a.act
                          << " mv=" << cand[i].a.mx << "," << cand[i].a.my << " sc=" << (long long)cand[i].score << "\n";
        }
        {
            int e = nearestEnemy(base, seat);
            std::cerr << "t=" << o.t << " me=" << base.p[seat].x << "," << base.p[seat].y
                      << " st=" << base.p[seat].st << " dart=" << base.d[seat].st;
            if (e >= 0) std::cerr << " en=" << base.p[e].x << "," << base.p[e].y << " ed=" << base.d[e].st
                                  << " est=" << base.p[e].st;
            std::cerr << " best=" << (long long)bestScore << " kind=" << cand[bestIdx].kind
                      << " act=" << best.act << " nc=" << nc << " cpu=" << (thread_cpu_100ns() - cpu0) << "\n";
        }
#endif
        // a tiny deterministic tie-break noise, depends on step and seat
        // (kept out of the main comparison to stay reproducible)
        remember(o, best);
        return best;
    }

    void remember(const Obs& o, const Action& a) {
        Hist& h = hist[o.t % HB];
        h.t = o.t; h.me = o.me(); h.md = o.my_dart(); h.act = a;
    }
};

DART_MAIN(Bot)
