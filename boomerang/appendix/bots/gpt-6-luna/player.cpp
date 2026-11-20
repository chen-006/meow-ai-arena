#include "dart.hpp"
using namespace dart;

struct Bot {
    static constexpr int HIST = 32;
    Player hp[HIST];
    Dart hd[HIST];
    Action ha[HIST];
    unsigned char hok[HIST]{};
    Player seen[MAXP];
    Dart seenD[MAXP];
    i64 seenT[MAXP]{};
    unsigned char seenOK[MAXP]{};
    int oldN = -1, oldSeat = -1;

    void init(const GameInfo&) {
        for (int i = 0; i < HIST; ++i) hok[i] = 0;
        for (int i = 0; i < MAXP; ++i) seenOK[i] = 0;
        oldN = oldSeat = -1;
    }

    static i64 sq(i64 x) { return x * x; }
    static i64 dist(i64 x1, i64 y1, i64 x2, i64 y2) { return ilen(x1-x2, y1-y2); }

    bool mate(const GameInfo& gi, int a, int b) const {
        return a != b && gi.team[a] == gi.team[b];
    }

    int nearest(const Game& g, const GameInfo& gi, int me) const {
        int who = -1;
        i64 best = (i64)4e18;
        for (int k = 0; k < g.n; ++k) {
            if (k == me || mate(gi, me, k) || !g.p[k].alive) continue;
            i64 d = dist(g.p[me].x, g.p[me].y, g.p[k].x, g.p[k].y);
            // 稍微偏向正处于硬直或蓄势的目标。
            if (g.p[k].st == S_THROW || g.p[k].st == S_SLASH || g.p[k].st == S_KICK ||
                g.p[k].st == S_RECALL || g.p[k].st == S_SLASHED || g.p[k].st == S_KICKED)
                d -= 350;
            if (d < best) best = d, who = k;
        }
        return who;
    }

    bool dashSafe(const Game& g, int k, i64 ax, i64 ay) const {
        if (k < 0 || k >= g.n || !g.p[k].alive) return false;
        i64 ux, uy;
        scale_to(ax, ay, 1000, ux, uy);
        auto canStand = [&](i64 xx, i64 yy, i64 t) {
            int c = cell_of_point(g.cf, g.M(), xx, yy, t);
            return c == C_FLOOR || c == C_GRASS;
        };
        i64 x = g.p[k].x, y = g.p[k].y;
        // 前摇和收势不能越坑；中间三步允许越坑，最后一步必须落地。
        for (i64 j = 0; j < g.cf.dash_pre; ++j)
            if (!canStand(x, y, g.t + j)) return false;
        for (i64 j = 0; j < g.cf.dash_t; ++j) {
            Map lm = live_map(g.cf, g.M(), g.t + g.cf.dash_pre + j);
            i64 vx = rdiv(ux * g.cf.dash_v, 1000), vy = rdiv(uy * g.cf.dash_v, 1000);
            move_body(lm, g.cf.body_r, x, y, vx, vy);
            if (j + 1 == g.cf.dash_t && cell_of_point(g.cf, lm, x, y, g.t + g.cf.dash_pre + j) == C_PIT)
                return false;
        }
        for (i64 j = 0; j < g.cf.dash_post; ++j)
            if (!canStand(x, y, g.t + g.cf.dash_pre + g.cf.dash_t + j)) return false;
        return true;
    }

    bool reachable(const GameInfo& gi, i64 x, i64 y, i64 gx, i64 gy, i64 t) const {
        const Map& M = gi.M();
        int sx = cell_index(x), sy = cell_index(y), ex = cell_index(gx), ey = cell_index(gy);
        if (sx == ex && sy == ey) return true;
        if (!M.inside(sx, sy) || !M.inside(ex, ey)) return false;
        auto ok = [&](int cx, int cy) {
            if (!M.inside(cx, cy)) return false;
            int c = cell_at_step(gi.cfg, M, cx, cy, t);
            return c == C_FLOOR || c == C_GRASS;
        };
        if (!ok(ex, ey)) return false;
        std::vector<unsigned char> seen(size_t(M.nx) * M.ny, 0);
        std::vector<int> q; q.reserve(size_t(M.nx) * M.ny);
        const int start = sy * M.nx + sx, goal = ey * M.nx + ex;
        seen[size_t(start)] = 1; q.push_back(start);
        static const int ddx[8] = {-1,0,1,-1,1,-1,0,1};
        static const int ddy[8] = {-1,-1,-1,0,0,1,1,1};
        for (size_t h = 0; h < q.size(); ++h) {
            int id = q[h], cx = id % M.nx, cy = id / M.nx;
            for (int z = 0; z < 8; ++z) {
                int nx = cx + ddx[z], ny = cy + ddy[z];
                if (!ok(nx, ny)) continue;
                if (ddx[z] && ddy[z] && (!ok(cx + ddx[z], cy) || !ok(cx, cy + ddy[z]))) continue;
                int ni = ny * M.nx + nx;
                if (ni == goal) return true;
                if (!seen[size_t(ni)]) seen[size_t(ni)] = 1, q.push_back(ni);
            }
        }
        return false;
    }

    void intercept(const Game& g, int me, int target, i64& ax, i64& ay) const {
        const Player& P = g.p[me];
        const Player& Q = g.p[target];
        const Dart& D = g.d[me];
        i64 lead = g.cf.throw_pre + 1;
        i64 tx = Q.x, ty = Q.y;
        for (int it = 0; it < 4; ++it) {
            tx = Q.x + Q.vx * lead;
            ty = Q.y + Q.vy * lead;
            i64 dd = dist(P.x, P.y, tx, ty);
            i64 flown = 0;
            int n = 0;
            while (flown < dd && n < 20) {
                flown += dart_step_len(g.cf, Dart{D_OUT, 0, 0, 0, 0, g.cf.range-flown, n});
                ++n;
            }
            lead = g.cf.throw_pre + 1 + n;
        }
        ax = tx - P.x;
        ay = ty - P.y;
        if (!ax && !ay) ax = P.face == 9000 || P.face == 27000 ? 0 : 1;
    }

    bool teammate_in_ray(const Game& g, const GameInfo& gi, int me, i64 ax, i64 ay) const {
        i64 ux, uy;
        scale_to(ax, ay, 1000, ux, uy);
        const Player& P = g.p[me];
        for (int k = 0; k < g.n; ++k) {
            if (!mate(gi, me, k) || !g.p[k].alive) continue;
            i64 dx = g.p[k].x - P.x, dy = g.p[k].y - P.y;
            i64 along = rdiv(dx * ux + dy * uy, 1000);
            if (along <= 0 || along > g.cf.range) continue;
            i64 px = P.x + rdiv(ux * along, 1000);
            i64 py = P.y + rdiv(uy * along, 1000);
            if (dist(px, py, g.p[k].x, g.p[k].y) <= g.cf.body_r + g.cf.dart_r + 180 &&
                !wall_between(g.M(), P.x, P.y, g.p[k].x, g.p[k].y)) return true;
        }
        return false;
    }

    Action makePolicy(const Game& g, const GameInfo& gi, int k, bool usePath) const {
        Action a;
        if (k < 0 || k >= g.n || !g.p[k].alive) return a;
        const Player& P = g.p[k];
        const Dart& D = g.d[k];
        // 召回只能被移动/其他动作打断。没有新的近身威胁时把引导做完，避免每隔一步反复开始、再取消。
        if (P.st == S_RECALL) return a;
        int target = nearest(g, gi, k);
        i64 gx = P.x, gy = P.y;
        bool hasGoal = false;
        i64 goalDist = 0;

        if (D.st == D_GROUND) {
            gx = D.x; gy = D.y; hasGoal = true;
            goalDist = dist(P.x, P.y, gx, gy);
        } else if (target >= 0) {
            const Player& Q = g.p[target];
            const i64 dd = dist(P.x, P.y, Q.x, Q.y);
            const bool foeHand = g.d[target].st == D_HAND;
            i64 ideal = 1000;
            if (D.st == D_HAND && foeHand) ideal = 3500;
            else if (D.st != D_HAND && foeHand) ideal = 3600;
            else if (D.st == D_HAND) ideal = 1250;
            else ideal = 1100;
            if (Q.st == S_THROW || Q.st == S_THROWN) ideal = imax(ideal, 5400);
            else if (Q.st == S_RECALL || Q.st == S_SLASH || Q.st == S_KICK) ideal = imin(ideal, 2100);
            if (g.n >= 4) {
                int threats = 0;
                for (int j = 0; j < g.n; ++j) if (j != k && !mate(gi, k, j) && g.p[j].alive &&
                    (g.d[j].st == D_HAND || dart_flying(g.d[j].st)) &&
                    dist(P.x, P.y, g.p[j].x, g.p[j].y) < 6500) ++threats;
                if (threats >= 2) ideal = imax(ideal, 2800);
            }
            goalDist = dd;
            if (dd > ideal + 300) {
                // 靠近目标，但保持近战或投镖的合适距离。
                i64 dx = Q.x - P.x, dy = Q.y - P.y;
                i64 ux, uy;
                scale_to(dx, dy, ideal, ux, uy);
                gx = Q.x - ux; gy = Q.y - uy; hasGoal = true;
            } else if (dd < ideal - 300) {
                // 退到合适距离；近距离时优先离开对手正面。
                i64 dx = P.x - Q.x, dy = P.y - Q.y;
                if (!dx && !dy) dx = (k & 1) ? -1000 : 1000;
                i64 ux, uy;
                scale_to(dx, dy, 3000, ux, uy);
                gx = P.x + ux; gy = P.y + uy; hasGoal = true;
            } else {
                // 在理想距离附近横向走，避免直线站桩。
                i64 dx = Q.x - P.x, dy = Q.y - P.y;
                int sign = ((g.t / 9 + k) & 1) ? 1 : -1;
                gx = P.x + sign * (-dy) * 4;
                gy = P.y + sign * dx * 4;
                hasGoal = true;
            }
        }

        if (hasGoal) {
            if (usePath) {
                i64 wx, wy;
                if (path_next(gi, P.x, P.y, gx, gy, g.t, wx, wy)) {
                    a.mx = wx - P.x; a.my = wy - P.y;
                } else {
                    a.mx = gx - P.x; a.my = gy - P.y;
                }
            } else {
                a.mx = gx - P.x; a.my = gy - P.y;
            }
            cap_len(a.mx, a.my, 1000000);
            if (ilen(a.mx, a.my) < 1000 && (a.mx || a.my)) scale_to(a.mx, a.my, 1000, a.mx, a.my);
        }

        if (P.st == S_FREE && target >= 0) {
            const Player& Q = g.p[target];
            const i64 dd = dist(P.x, P.y, Q.x, Q.y);
            i64 ax = Q.x - P.x, ay = Q.y - P.y;
            const bool targetLocked = Q.st == S_THROW || Q.st == S_THROWN || Q.st == S_SLASH ||
                Q.st == S_SLASHED || Q.st == S_KICK || Q.st == S_KICKED || Q.st == S_RECALL || Q.st == S_STUN;
            if (D.st == D_HAND && dd >= (targetLocked ? 1600 : 2100) && dd <= 5600 &&
                !wall_between(g.M(), P.x, P.y, Q.x, Q.y)) {
                intercept(g, k, target, ax, ay);
                if (!teammate_in_ray(g, gi, k, ax, ay)) a.act = A_THROW, a.ax = ax, a.ay = ay;
            } else if (dd <= (D.st == D_HAND ? g.cf.slash_reach : g.cf.kick_reach) + 100 &&
                       !wall_between(g.M(), P.x, P.y, Q.x, Q.y)) {
                a.act = A_MELEE; a.ax = ax; a.ay = ay;
            } else if (g.n >= 4 && P.cd == 0 && dd > 4800 && dd < 7200 && D.st != D_GROUND && dashSafe(g, k, ax, ay)) {
                a.act = A_DASH; a.ax = ax; a.ay = ay;
            }
        }
        if (P.st == S_FREE && D.st == D_GROUND && gi.cfg.recall && goalDist > 1800) {
            i64 wx, wy;
            bool route = usePath ? path_next(gi, P.x, P.y, D.x, D.y, g.t, wx, wy)
                                 : reachable(gi, P.x, P.y, D.x, D.y, g.t);
            if (!route) {
                bool threatened = false;
                for (int j = 0; j < g.n; ++j) if (j != k && !mate(gi, k, j) && g.p[j].alive &&
                    dist(P.x, P.y, g.p[j].x, g.p[j].y) < 5200) threatened = true;
                for (int j = 0; j < g.n; ++j) if (j != k && !mate(gi, k, j) && dart_flying(g.d[j].st) &&
                    dist(P.x, P.y, g.d[j].x, g.d[j].y) < 3200) threatened = true;
                int c = cell_of_point(gi.cfg, gi.M(), P.x, P.y, g.t);
                i64 f = cell_fall_step(gi, cell_index(P.x), cell_index(P.y));
                bool safe = (c == C_FLOOR || c == C_GRASS) && (f < 0 || f > g.t + gi.cfg.recall_pre + 2);
                if (!threatened && safe) {
                    a.mx = a.my = 0; a.act = A_RECALL;
                } else if (target >= 0 && a.act != A_MELEE) {
                    const Player& Q = g.p[target];
                    i64 dx = Q.x - P.x, dy = Q.y - P.y;
                    if (g.d[target].st == D_HAND) dx = -dx, dy = -dy;
                    if (!dx && !dy) dx = (k & 1) ? -1000 : 1000;
                    if (usePath) {
                        i64 tx, ty, px, py;
                        scale_to(dx, dy, 3000, tx, ty);
                        if (path_next(gi, P.x, P.y, P.x + tx, P.y + ty, g.t, px, py))
                            a.mx = px - P.x, a.my = py - P.y;
                        else a.mx = dx, a.my = dy;
                    } else a.mx = dx, a.my = dy;
                    cap_len(a.mx, a.my, 1000000);
                    if (ilen(a.mx, a.my) < 1000 && (a.mx || a.my)) scale_to(a.mx, a.my, 1000, a.mx, a.my);
                }
            }
        }
        return a;
    }

    void add(std::vector<Action>& v, Action a) const {
        for (const Action& b : v)
            if (a.mx == b.mx && a.my == b.my && a.act == b.act && a.ax == b.ax && a.ay == b.ay) return;
        v.push_back(a);
    }

    std::vector<Action> candidates(const Game& g, const GameInfo& gi) const {
        std::vector<Action> v;
        const int me = g.n ? gi.seat : 0;
        const Player& P = g.p[me];
        const Dart& D = g.d[me];
        Action greedy = makePolicy(g, gi, me, true);
        add(v, greedy);
        Action wait;
        add(v, wait);

        int target = nearest(g, gi, me);
        if (target >= 0) {
            const Player& Q = g.p[target];
            i64 dx = Q.x - P.x, dy = Q.y - P.y;
            if (dx || dy) {
                i64 ux, uy;
                scale_to(dx, dy, 1000, ux, uy);
                Action a; a.mx = ux; a.my = uy; add(v, a);
                Action b; b.mx = -ux; b.my = -uy; add(v, b);
                i64 sx, sy; scale_to(-dy, dx, 1000, sx, sy);
                Action c; c.mx = sx; c.my = sy; add(v, c);
                Action d; d.mx = -sx; d.my = -sy; add(v, d);

                if (P.st == S_FREE && D.st == D_HAND && !wall_between(g.M(), P.x, P.y, Q.x, Q.y)) {
                    i64 ax, ay; intercept(g, me, target, ax, ay);
                    if (!teammate_in_ray(g, gi, me, ax, ay)) {
                        Action th = greedy; th.act = A_THROW; th.ax = ax; th.ay = ay; add(v, th);
                        Action th2 = th; th2.mx = 0; th2.my = 0; add(v, th2);
                    }
                }
                if (g.n >= 4 && P.st == S_FREE && P.cd == 0 && D.st != D_GROUND && dashSafe(g, me, dx, dy)) {
                    Action da; da.act = A_DASH; da.ax = dx; da.ay = dy; add(v, da);
                    if (dashSafe(g, me, -dy, dx)) {
                        Action db; db.act = A_DASH; db.ax = -dy; db.ay = dx; add(v, db);
                    }
                }
                if (P.st == S_FREE && dist(P.x, P.y, Q.x, Q.y) <= 2600 &&
                    !wall_between(g.M(), P.x, P.y, Q.x, Q.y)) {
                    Action m = greedy; m.act = A_MELEE; m.ax = dx; m.ay = dy; add(v, m);
                }
            }
        }
        if (P.st == S_FREE && D.st == D_GROUND && gi.cfg.recall && greedy.act == A_RECALL) {
            Action r; r.act = A_RECALL; add(v, r);
        }

        if (P.st == S_FREE && D.st == D_HAND) {
            int addedBlocks = 0;
            for (int k = 0; k < g.n && addedBlocks < 2; ++k) {
                if (k == me || mate(gi, me, k) || !g.p[k].alive || !dart_flying(g.d[k].st)) continue;
                if (dist(P.x, P.y, g.d[k].x, g.d[k].y) > 4200) continue;
                Action b; b.act = A_MELEE; b.ax = g.d[k].x - P.x; b.ay = g.d[k].y - P.y;
                if (!b.ax && !b.ay) b.ax = 1;
                add(v, b); ++addedBlocks;
            }
        }

        static const int dirs[8][2] = {{1000,0},{-1000,0},{0,1000},{0,-1000},
                                       {707,707},{707,-707},{-707,707},{-707,-707}};
        for (const auto& d : dirs) { Action a; a.mx = d[0]; a.my = d[1]; add(v, a); }

        // 优先保留较少、差异化较大的候选，避免四人局搜索拖过单步预算。
        int cap = g.n >= 8 ? 12 : (g.n >= 4 ? 17 : 22);
        if ((int)v.size() > cap) v.resize(cap);
        return v;
    }

    i64 evaluate(const Game& base, const GameInfo& gi, const Action& first, int horizon, bool noBodies = false) const {
        Game g = base;
        const int me = gi.seat;
        if (noBodies) for (int k = 0; k < g.n; ++k) if (k != me) g.p[k].unknown = 1;
        bool deadSeen[MAXP]{};
        for (int k = 0; k < g.n; ++k) deadSeen[k] = !g.p[k].alive;
        i64 score = 0, danger = 0;
        bool firstStep = true;
        for (int s = 0; s < horizon; ++s) {
            Action acts[MAXP];
            for (int k = 0; k < g.n; ++k) {
                if (k == me && firstStep) acts[k] = first;
                else acts[k] = noBodies ? Action() : makePolicy(g, gi, k, false);
            }
            firstStep = false;
            g.step(acts);

            for (int k = 0; k < g.n; ++k) {
                if (!deadSeen[k] && !g.p[k].alive) {
                    deadSeen[k] = true;
                    if (g.p[k].killer == me) {
                        if (mate(gi, me, k)) score -= 90000;
                        else score += 42000;
                    } else if (mate(gi, me, k)) score -= 1800;
                }
            }
            if (!g.p[me].alive)
                return g.p[me].cause == K_FALL ? -(i64)1000000000000000LL : score - 300000;

            const Player& M = g.p[me];
            bool armedNearby = false;
            for (int k = 0; k < g.n; ++k) if (k != me && !mate(gi, me, k) && g.p[k].alive &&
                (g.d[k].st == D_HAND || dart_flying(g.d[k].st)) &&
                dist(M.x, M.y, g.p[k].x, g.p[k].y) < 6500) armedNearby = true;
            if (armedNearby && M.st == S_FREE && ilen(M.x - M.px, M.y - M.py) < 80)
                danger += 70;
            for (int k = 0; k < g.n; ++k) {
                if (k == me || mate(gi, me, k) || !g.p[k].alive || !dart_flying(g.d[k].st)) continue;
                i64 dd = dist(M.x, M.y, g.d[k].x, g.d[k].y);
                if (dd < 1900) {
                    i64 z = 1900 - dd;
                    danger += 4 * (z * z) / 2500;
                }
            }
            if (g.over) {
                if (g.winner == me || (gi.cfg.mode == M_TEAM && g.winner == gi.team[me])) score += 160000;
                else if (g.winner >= 0 || g.winner == -1) score -= 100000;
                break;
            }
        }
        score -= danger;

        const Player& M = g.p[me];
        const Dart& MD = g.d[me];
        if (MD.st == D_HAND) score += 650;
        else if (MD.st == D_GROUND) {
            score += 150 - dist(M.x, M.y, MD.x, MD.y) / 8;
        } else if (MD.st == D_OUT || MD.st == D_BACK || MD.st == D_CALL) score += 100;
        if (M.st != S_FREE) score -= M.timer * 24;
        if (M.cd > 0 && M.cd < gi.cfg.dash_cd) score -= 15;

        int target = nearest(g, gi, me);
        if (target >= 0) {
            const Player& Q = g.p[target];
            i64 dd = dist(M.x, M.y, Q.x, Q.y);
            i64 ideal = 1100;
            if (MD.st == D_HAND && g.d[target].st == D_HAND) ideal = 3400;
            else if (MD.st != D_HAND && g.d[target].st == D_HAND) ideal = 3700;
            else if (MD.st == D_HAND) ideal = 1250;
            if (Q.st == S_THROW || Q.st == S_THROWN) ideal = imax(ideal, 5400);
            else if (Q.st == S_SLASH || Q.st == S_KICK || Q.st == S_RECALL) ideal = imin(ideal, 2200);
            if (g.n >= 4) {
                int threats = 0;
                for (int k = 0; k < g.n; ++k) if (k != me && !mate(gi, me, k) && g.p[k].alive &&
                    (g.d[k].st == D_HAND || dart_flying(g.d[k].st)) &&
                    dist(M.x, M.y, g.p[k].x, g.p[k].y) < 6500) ++threats;
                if (threats >= 2) ideal = imax(ideal, 2800);
            }
            score -= (i64)std::abs(dd - ideal) / 7;
            if (Q.st == S_THROW || Q.st == S_SLASH || Q.st == S_KICK || Q.st == S_RECALL || Q.st == S_STUN)
                score += 100;
        }
        int aliveEnemies = 0, aliveMates = 0;
        for (int k = 0; k < g.n; ++k) if (k != me && g.p[k].alive) {
            if (mate(gi, me, k)) ++aliveMates; else ++aliveEnemies;
        }
        score += (i64)(g.n - 1 - aliveEnemies - aliveMates) * 80;
        return score;
    }

    Action decide(const Obs& o) {
        const GameInfo& gi = info();
        const int me = (int)o.seat;
        Action stop;
        if (!o.me().alive) return stop;

        if (oldN != o.n || oldSeat != me) {
            init(gi); oldN = o.n; oldSeat = me;
        }
        const int slot = (int)(o.t % HIST);
        hp[slot] = o.p[me]; hp[slot].unknown = 0;
        hp[slot].team = gi.team[me];
        hd[slot] = o.d[me];
        hok[slot] = 1;

        const i64 lag = imin(o.t, gi.cfg.delay);
        const i64 obsT = o.t - lag;
        for (int k = 0; k < o.n; ++k) if (k != me && o.p[k].alive && o.p[k].vis) {
            seen[k] = o.p[k]; seen[k].team = gi.team[k]; seen[k].unknown = 0;
            seenD[k] = o.d[k]; seenT[k] = obsT; seenOK[k] = 1;
        }

        Game projected(gi, o);
        projected.t = obsT; projected.live.reset(); projected.live_fallen = -1;
        // 草丛里的敌人字段被隐藏，但存储上一次可见位置并继续外推。
        for (int k = 0; k < o.n; ++k) if (k != me && o.p[k].alive && !o.p[k].vis) {
            if (seenOK[k]) {
                projected.p[k] = seen[k]; projected.d[k] = seenD[k];
                i64 dt = imax(0, obsT - seenT[k]);
                projected.p[k].x += projected.p[k].vx * dt;
                projected.p[k].y += projected.p[k].vy * dt;
                projected.p[k].px = projected.p[k].x; projected.p[k].py = projected.p[k].y;
                projected.p[k].team = gi.team[k]; projected.p[k].unknown = 0;
            } else {
                projected.p[k].x = gi.spx[k]; projected.p[k].y = gi.spy[k];
                projected.p[k].px = projected.p[k].x; projected.p[k].py = projected.p[k].y;
                projected.p[k].vx = projected.p[k].vy = 0;
                projected.p[k].team = gi.team[k]; projected.p[k].unknown = 0;
                projected.d[k].st = D_HAND;
                projected.d[k].x = projected.p[k].x; projected.d[k].y = projected.p[k].y;
            }
        }

        // 对手和镖从观测时刻向前推；自己的历史状态、动作由本程序准确复原。
        for (int j = 0; j < (int)lag; ++j) {
            const i64 tt = obsT + j;
            const int hs = (int)(tt % HIST);
            if (hok[hs]) {
                projected.p[me] = hp[hs]; projected.p[me].team = gi.team[me];
                projected.p[me].unknown = 1;
                projected.d[me] = hd[hs];
            } else {
                projected.p[me].unknown = 1;
            }
            Action acts[MAXP];
            Player beforeP[MAXP];
            Dart beforeD[MAXP];
            for (int k = 0; k < o.n; ++k) {
                beforeP[k] = projected.p[k];
                beforeD[k] = projected.d[k];
                if (k == me) acts[k] = Action();
                else acts[k] = makePolicy(projected, gi, k, false);
            }
            projected.over = false;
            projected.step(acts);
            projected.over = false;
            // 延迟窗口只有四步，启发式预测不应凭一次模拟命中就把仍在延迟画面中的人删掉。
            // 保留其估计位置，恢复为可行动状态，继续推演后续动作。
            for (int k = 0; k < o.n; ++k) if (k != me && o.p[k].alive && !projected.p[k].alive) {
                Player q = beforeP[k];
                q.x = projected.p[k].x; q.y = projected.p[k].y;
                q.px = beforeP[k].x; q.py = beforeP[k].y;
                q.st = S_FREE; q.timer = 0; q.ax = q.ay = 0;
                q.cd = imax(0, q.cd - 1); q.expose = imax(0, q.expose - 1);
                q.alive = 1; q.unknown = 0; q.dead_t = -1; q.cause = K_NONE; q.killer = -1;
                projected.p[k] = q;
                if (projected.d[k].st == D_GONE) {
                    projected.d[k] = beforeD[k];
                    if (projected.d[k].st == D_HAND) projected.d[k].x = q.x, projected.d[k].y = q.y;
                }
            }
        }

        // 自己用当前精确状态覆盖外推结果，其他人保留预测出的当前位置和飞镖状态。
        projected.t = o.t; projected.live.reset(); projected.live_fallen = -1; projected.over = false;
        projected.p[me] = o.p[me]; projected.p[me].team = gi.team[me]; projected.p[me].unknown = 0;
        projected.d[me] = o.d[me];
        for (int k = 0; k < o.n; ++k) {
            projected.p[k].px = projected.p[k].x; projected.p[k].py = projected.p[k].y;
            projected.p[k].unknown = 0;
        }

        // 塌陷临近时，确保当前策略候选里包含通往长期安全格的方向。
        Action evacuation;
        const int cx = cell_index(o.me().x), cy = cell_index(o.me().y);
        i64 fall = cell_fall_step(gi, cx, cy);
        if (fall >= 0 && fall <= o.t + 16) {
            const Map& M = gi.M();
            std::vector<int> gd = grid_dist(gi, cx, cy, o.t);
            int best = -1, bestD = 1 << 28;
            for (int y = 0; y < M.ny; ++y) for (int x = 0; x < M.nx; ++x) {
                int id = y * M.nx + x;
                if (gd[id] < 0 || gd[id] >= bestD) continue;
                i64 fs = cell_fall_step(gi, x, y);
                if (fs >= 0 && fs <= o.t + 55) continue;
                i64 wx = cell_center(x), wy = cell_center(y);
                if (!standable(gi, wx, wy, o.t + 4)) continue;
                best = id; bestD = gd[id];
            }
            if (best >= 0) {
                i64 wx, wy;
                if (path_next(gi, o.me().x, o.me().y, cell_center(best % M.nx), cell_center(best / M.nx), o.t, wx, wy))
                    evacuation = move_to(o.me().x, o.me().y, wx, wy);
            }
        }

        std::vector<Action> cand = candidates(projected, gi);
        if (evacuation.mx || evacuation.my) add(cand, evacuation);
        const i64 cpu0 = thread_cpu_100ns();
        const int horizon = o.n >= 8 ? 5 : (o.n >= 4 ? 7 : 8);
        i64 bestScore = -(i64)4e18;
        Action best;
        bool haveBest = false;
        for (int i = 0; i < (int)cand.size(); ++i) {
            if (i > 0 && cpu0 >= 0) {
                i64 now = thread_cpu_100ns();
                if (now >= cpu0 && now - cpu0 > 52000) break;
            }
            i64 s = evaluate(projected, gi, cand[i], horizon);
            // 延迟外推可能把对手误放到我身边，推挤预测就会把所有动作都判成落坑。
            // 此时用“对手身体不参与、已飞出的镖照常飞”的保守分支复核，避免被虚构碰撞逼进坑。
            if (s <= -200000) {
                i64 robust = evaluate(projected, gi, cand[i], horizon, true);
                if (robust > -(i64)100000000000000LL &&
                    (s <= -(i64)100000000000000LL || robust - 250 > s)) s = robust - 250;
            }
            if (s > -(i64)100000000000000LL && (!haveBest || s > bestScore))
                bestScore = s, best = cand[i], haveBest = true;
        }
        if (!haveBest) best = Action();

        ha[slot] = best;
        return best;
    }
};

DART_MAIN(Bot)
