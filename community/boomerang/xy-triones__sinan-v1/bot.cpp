// player_v1.cpp —— 回旋镖 bot V1（基于第 11 集规则，未编译验证）
// 设计：延迟重建 + 对手分支预测 + 镖威胁 + 地图安全 + 候选推演
// 相比原版 DeepSeek 的改动：
//  1) 对手行为分 3 支（继续/逼近/远离），按在线倾向加权；
//  2) 记录对手投掷倾向，动态调整“要不要防他出手”；
//  3) 评分加入“回程线/近失镖”惩罚，减少躲了去程撞回程；
//  4) 近战/格挡候选更积极（对方空手/硬直时）。
// 编译：python build.py player_v1.cpp
// 检查：python match.py check player_v1.cpp
#include "dart.hpp"
#include <algorithm>
#include <vector>
#include <cstdio>
using namespace dart;

static const int MAXH = 900;
static const int MAXC = 320;

struct Bot {
    Config cf;
    int n = 0, seat = 0;
    i64 W = 0, H = 0;

    std::vector<Player> myP;
    std::vector<Dart> myD;
    std::vector<Action> myA;
    std::vector<char> haveP;

    Player lastSeen[MAXP];
    i64 lastSeenT[MAXP];
    bool everSeen[MAXP];

    // 在线倾向
    double throwTend[MAXP];
    bool prevHand[MAXP];

    void init(const GameInfo& gi) {
        cf = gi.cfg; n = gi.n; seat = gi.seat;
        W = gi.M().width(); H = gi.M().height();
        myP.assign(MAXH, Player());
        myD.assign(MAXH, Dart());
        myA.assign(MAXH, Action());
        haveP.assign(MAXH, 0);
        for (int k = 0; k < MAXP; ++k) {
            lastSeen[k] = Player();
            lastSeenT[k] = -1;
            everSeen[k] = false;
            throwTend[k] = 0.5;
            prevHand[k] = true;
        }
    }

    static void unitv(i64 dx, i64 dy, i64& ux, i64& uy) {
        if (!scale_to(dx, dy, UNIT, ux, uy)) { ux = UNIT; uy = 0; }
    }

    void recordSelf(const Obs& o) {
        if (o.t >= 0 && o.t < MAXH) {
            myP[o.t] = o.me();
            myP[o.t].unknown = 0;
            myD[o.t] = o.my_dart();
            haveP[o.t] = 1;
        }
        for (int k = 0; k < n; ++k) {
            if (k == seat) continue;
            if (o.p[k].alive && o.p[k].vis) {
                lastSeen[k] = o.p[k];
                lastSeenT[k] = o.t - cf.delay;
                everSeen[k] = true;
            }
        }
    }

    void fillHidden(const Obs& o, Game& g) {
        for (int k = 0; k < n; ++k) {
            if (k == seat) continue;
            if (!g.p[k].alive || !g.p[k].unknown) continue;
            if (!everSeen[k]) continue;
            const i64 dt = o.t - lastSeenT[k];
            if (dt < 0 || dt > 30) continue;
            Player P = lastSeen[k];
            const i64 st = imin(dt, (i64)16);
            P.x += P.vx * st;
            P.y += P.vy * st;
            P.x = iclamp(P.x, 1, W - 1);
            P.y = iclamp(P.y, 1, H - 1);
            P.alive = 1; P.unknown = 0; P.st = S_FREE; P.timer = 0;
            P.vx = 0; P.vy = 0;
            g.p[k] = P;
            if (g.d[k].st == D_HAND) { g.d[k].x = P.x; g.d[k].y = P.y; }
        }
    }

    bool buildWorld(const Obs& o, Game& out) {
        const i64 T0 = imax((i64)0, o.t - cf.delay);
        Game g(info(), o);
        g.hist.reset();
        g.over = false;
        if (o.t <= 0) { g.t = o.t; out = g; return true; }
        if (!haveP[T0] || T0 >= MAXH) { g.t = o.t; out = g; return false; }
        g.t = T0;
        g.p[seat] = myP[T0]; g.p[seat].unknown = 0;
        g.d[seat] = myD[T0];
        for (i64 s = T0; s < o.t; ++s) {
            Action acts[MAXP];
            for (int k = 0; k < g.n; ++k) {
                if (k == seat) { if (s < MAXH) acts[k] = myA[s]; continue; }
                const Player& P = g.p[k];
                Action a;
                if (P.alive && !P.unknown) contAction(P, a);
                acts[k] = a;
            }
            g.step(acts);
            if (!g.p[seat].alive) {
                Game h(info(), o);
                h.hist.reset(); h.over = false; h.t = o.t;
                out = h; return false;
            }
        }
        g.t = o.t;
        g.p[seat] = o.me(); g.p[seat].unknown = 0;
        g.d[seat] = o.my_dart();
        g.over = false;
        out = g;
        return true;
    }

    void contAction(const Player& P, Action& a) {
        if (!P.alive) return;
        i64 pct = 0;
        if (P.st == S_FREE) pct = 100;
        else if (P.st == S_THROW || P.st == S_THROWN) pct = cf.throw_move;
        else return;
        if (pct <= 0) return;
        const i64 sp = ilen(P.vx, P.vy);
        if (sp < 5) return;
        const i64 mxv = cf.run_v * pct / 100;
        if (mxv <= 0) return;
        i64 M = rdiv(sp * 1000, mxv);
        if (M > 1000) M = 1000;
        if (M < 1) M = 1;
        scale_to(P.vx, P.vy, M, a.mx, a.my);
    }

    // 对手行为：按倾向选择“继续/逼近/远离/扔/砍”
    Action enemyAction(const Game& g, int k) {
        Action a;
        contAction(g.p[k], a);
        const Player& P = g.p[k];
        const Player& T = g.p[seat];
        if (!P.alive || P.unknown || !T.alive) return a;
        if (info().team[k] == info().team[seat]) return a;
        if (P.st != S_FREE) return a;
        const i64 dx = T.x - P.x, dy = T.y - P.y;
        const i64 dd = ilen(dx, dy);
        if (dd > 5200) return a;
        if (dd > 0 && wall_between(g.M(), P.x, P.y, T.x, T.y)) return a;
        const bool armed = (g.d[k].st == D_HAND);
        if (armed) {
            if (dd <= 1500) { a.act = A_MELEE; a.ax = dx; a.ay = dy; }
            else if (throwTend[k] > 0.35 && dd <= 4800) { a.act = A_THROW; a.ax = dx; a.ay = dy; }
        } else if (dd <= cf.kick_reach) {
            a.act = A_MELEE; a.ax = dx; a.ay = dy;
        }
        return a;
    }

    // 三支预测：0 继续、1 逼近、2 远离
    void predictBranch(const Game& base, int k, int mode, int HH,
                       i64* xs, i64* ys, bool* alive) {
        Game g = base;
        g.hist.reset(); g.over = false;
        for (int s = 0; s < HH; ++s) { xs[s] = g.p[k].x; ys[s] = g.p[k].y; alive[s] = g.p[k].alive != 0; }
        for (int s = 1; s < HH; ++s) {
            Action acts[MAXP];
            for (int j = 0; j < g.n; ++j) {
                acts[j] = Action();
                if (j == seat) continue;
                if (j == k) {
                    Action a;
                    if (mode == 1) {
                        i64 ux, uy; unitv(g.p[seat].x - g.p[k].x, g.p[seat].y - g.p[k].y, ux, uy);
                        a.mx = ux; a.my = uy;
                    } else if (mode == 2) {
                        i64 ux, uy; unitv(g.p[k].x - g.p[seat].x, g.p[k].y - g.p[seat].y, ux, uy);
                        a.mx = ux; a.my = uy;
                    } else {
                        contAction(g.p[k], a);
                    }
                    acts[j] = a;
                } else {
                    contAction(g.p[j], acts[j]);
                }
            }
            g.step(acts);
            g.over = false;
            xs[s] = g.p[k].x; ys[s] = g.p[k].y; alive[s] = g.p[k].alive != 0;
        }
    }

    double cellSafety(const Game& g, i64 x, i64 y) {
        const Map& M = g.M();
        const int cx = cell_index(x), cy = cell_index(y);
        if (cx < 0 || cy < 0 || cx >= M.nx || cy >= M.ny) return -1e5;
        const int c = cell_at_step(cf, M, cx, cy, g.t);
        if (c == C_PIT || c == C_WALL) return -1e5;
        const i64 fall = cell_fall_step(info(), cx, cy);
        if (fall < 0) return 400.0;
        const i64 rem = fall - g.t;
        if (rem < 0) return -1e5;
        if (rem > 120) return 400.0;
        return rem * 3.0;
    }

    double eval(const Game& base, const Action& first, int HH, i64& ex, i64& ey) {
        Game g = base;
        g.hist.reset(); g.over = false;
        const i64 k0 = g.p[seat].kills;
        double score = 0;
        int deadAt = HH;
        bool dead = false;
        double minDart = 1e9;
        for (int s = 0; s < HH; ++s) {
            Action acts[MAXP];
            for (int j = 0; j < g.n; ++j) {
                if (j == seat) {
                    if (s == 0) acts[j] = first;
                    else {
                        Action a; a.mx = first.mx; a.my = first.my;
                        if (g.p[seat].st == S_RECALL) { a.mx = 0; a.my = 0; }
                        acts[j] = a;
                    }
                } else acts[j] = enemyAction(g, j);
            }
            g.step(acts);
            g.over = false;
            if (!g.p[seat].alive) { dead = true; deadAt = s; break; }
            const i64 km = g.p[seat].kills;
            if (km > k0) score += 22000.0 * (double)(km - k0);
            score += 2.0;
            // 近失镖检测：逐步惩罚，越早越重
            for (int k = 0; k < g.n; ++k) {
                if (k == seat || !dart_flying(g.d[k].st)) continue;
                const i64 dd = ilen(g.d[k].x - g.p[seat].x, g.d[k].y - g.p[seat].y);
                if (dd < minDart) minDart = (double)dd;
                if (dd < 1500) {
                    const double w = 1.0 + (HH - s) * 0.04;
                    score -= (1500.0 - (double)dd) * 0.35 * w;
                }
            }
        }
        ex = g.p[seat].x; ey = g.p[seat].y;
        if (dead) return -1.0e8 + (double)deadAt * 100.0 + score * 0.01;
        const Player& me = g.p[seat];
        const Dart& md = g.d[seat];
        if (md.st == D_HAND) score += 150;
        else if (md.st == D_OUT || md.st == D_BACK || md.st == D_RISE || md.st == D_CALL) score += 35;
        else if (md.st == D_GROUND) {
            const i64 dd = ilen(md.x - me.x, md.y - me.y);
            score -= (double)dd * 0.035;
            if (dd <= cf.pick_r) score += 110;
        }
        score += cellSafety(g, me.x, me.y);
        const i64 bd = imin(imin(me.x, W - me.x), imin(me.y, H - me.y));
        if (bd < 800) score -= (800 - bd) * 0.06;
        if (minDart < 1200) score -= (1200 - minDart) * 0.15;
        // 最近对手距离
        i64 nearest = -1, nd = 0;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || !g.p[k].alive) continue;
            const i64 d = ilen(g.p[k].x - me.x, g.p[k].y - me.y);
            if (nearest < 0 || d < nd) { nearest = k; nd = d; }
        }
        if (nearest >= 0) {
            const bool armed = (md.st == D_HAND);
            const bool foeArmed = (g.d[nearest].st == D_HAND);
            const double pref = armed ? (foeArmed ? 3400.0 : 1300.0) : 3200.0;
            if ((double)nd > pref + 1200) score -= std::min((double)nd - pref - 1200, 5000.0) * 0.04;
            else if ((double)nd < pref - 800) score -= (pref - 800.0 - (double)nd) * 0.07;
            else score += 30;
        }
        return score;
    }

    struct Cand { Action a; double sc; int kind; i64 ex, ey; };

    Action decide(const Obs& o) {
        recordSelf(o);
        const Player& me = o.me();
        Action res;
        if (!me.alive) { myA[o.t] = res; return res; }

        Game base;
        buildWorld(o, base);
        base.hist.reset(); base.over = false;
        fillHidden(o, base);

        // 在线倾向：上一回合手里有镖、现在没了 → 投掷倾向上升
        for (int k = 0; k < n; ++k) {
            if (k == seat) continue;
            const bool hand = (base.d[k].st == D_HAND);
            if (prevHand[k] && !hand) throwTend[k] = std::min(0.95, throwTend[k] + 0.18);
            else throwTend[k] = std::max(0.05, throwTend[k] * 0.995);
            prevHand[k] = hand;
        }

        const int HH = 16;
        Cand best[MAXC];
        int nc = 0;
        auto push = [&](const Action& a, double sc, int kind) {
            if (nc < MAXC) { best[nc].a = a; best[nc].sc = sc; best[nc].kind = kind; }
            ++nc;
        };
        auto evalPush = [&](const Action& a, int kind) {
            i64 ex = 0, ey = 0;
            double sc = eval(base, a, HH, ex, ey);
            push(a, sc, kind);
        };

        // 方向候选
        i64 dirs[24][2]; int ndir = 0;
        for (int i = 0; i < 16; ++i) {
            dirs[ndir][0] = Game::icos_face(i * 2250);
            dirs[ndir][1] = Game::isin_face(i * 2250);
            ++ndir;
        }
        dirs[ndir][0] = 0; dirs[ndir][1] = 0; ++ndir;

        int tgt = -1; i64 tgtD = 0;
        for (int k = 0; k < n; ++k) {
            if (k == seat || !base.p[k].alive || base.p[k].unknown) continue;
            const i64 d = ilen(base.p[k].x - me.x, base.p[k].y - me.y);
            if (tgt < 0 || d < tgtD) { tgt = k; tgtD = d; }
        }
        const Dart& md = o.my_dart();
        if (tgt >= 0) {
            i64 ux, uy; unitv(base.p[tgt].x - me.x, base.p[tgt].y - me.y, ux, uy);
            dirs[ndir][0] = ux; dirs[ndir][1] = uy; ++ndir;
            dirs[ndir][0] = -ux; dirs[ndir][1] = -uy; ++ndir;
        }
        if (md.st == D_GROUND) {
            i64 ux, uy; unitv(md.x - me.x, md.y - me.y, ux, uy);
            dirs[ndir][0] = ux; dirs[ndir][1] = uy; ++ndir;
        }
        // 垂直躲镖
        for (int k = 0; k < n && ndir < 23; ++k) {
            if (k == seat || !dart_flying(base.d[k].st)) continue;
            i64 vx = base.d[k].vx, vy = base.d[k].vy;
            i64 ux, uy; unitv(-vy, vx, ux, uy);
            dirs[ndir][0] = ux; dirs[ndir][1] = uy; ++ndir;
            if (ndir < 23) { dirs[ndir][0] = -ux; dirs[ndir][1] = -uy; ++ndir; }
        }
        {
            i64 ux, uy; unitv(W / 2 - me.x, H / 2 - me.y, ux, uy);
            dirs[ndir][0] = ux; dirs[ndir][1] = uy; ++ndir;
        }
        for (int i = 0; i < ndir; ++i) {
            Action a; a.mx = dirs[i][0]; a.my = dirs[i][1];
            evalPush(a, 0);
        }

        // 近战/格挡
        if (me.st == S_FREE) {
            for (int k = 0; k < n; ++k) {
                if (k == seat || !base.p[k].alive || base.p[k].unknown) continue;
                const i64 dx = base.p[k].x - me.x, dy = base.p[k].y - me.y;
                if (ilen(dx, dy) <= cf.slash_reach + 600) {
                    Action a; a.act = A_MELEE;
                    unitv(dx, dy, a.ax, a.ay);
                    evalPush(a, 2);
                }
            }
            for (int k = 0; k < n; ++k) {
                if (k == seat || !dart_flying(base.d[k].st)) continue;
                const i64 dx = base.d[k].x - me.x, dy = base.d[k].y - me.y;
                if (ilen(dx, dy) < 1700) {
                    Action a; a.act = A_MELEE;
                    unitv(dx, dy, a.ax, a.ay);
                    evalPush(a, 2);
                }
            }
        }

        // 扔镖：对每个可见对手的未来 2..12 步
        if (me.st == S_FREE && md.st == D_HAND) {
            for (int k = 0; k < n && nc < MAXC - 20; ++k) {
                if (k == seat || !base.p[k].alive || base.p[k].unknown) continue;
                i64 xs[14], ys[14]; bool al[14];
                predictBranch(base, k, 0, 13, xs, ys, al);
                for (int j = 2; j <= 12; ++j) {
                    if (!al[j]) continue;
                    const i64 dx = xs[j] - me.x, dy = ys[j] - me.y;
                    const i64 dd = ilen(dx, dy);
                    if (dd < 500 || dd > cf.range + 1200) continue;
                    if (wall_between(base.M(), me.x, me.y, xs[j], ys[j])) continue;
                    Action a; a.act = A_THROW;
                    unitv(dx, dy, a.ax, a.ay);
                    evalPush(a, 1);
                }
            }
        }

        // 格挡：有镖时，对迎面飞来的镖提前起手
        if (me.st == S_FREE && md.st == D_HAND) {
            for (int k = 0; k < n; ++k) {
                if (k == seat || !dart_flying(base.d[k].st)) continue;
                const i64 dx = base.d[k].x - me.x, dy = base.d[k].y - me.y;
                const i64 dd = ilen(dx, dy);
                if (dd < 700 || dd > 1700) continue;
                if (base.d[k].vx * dx + base.d[k].vy * dy >= 0) continue;
                Action a; a.act = A_MELEE;
                unitv(dx, dy, a.ax, a.ay);
                evalPush(a, 2);
            }
        }

        // 冲刺
        if (me.st == S_FREE && me.cd == 0) {
            for (int i = 0; i < 8; ++i) {
                Action a; a.act = A_DASH;
                a.ax = Game::icos_face(i * 4500);
                a.ay = Game::isin_face(i * 4500);
                evalPush(a, 3);
            }
        }

        // 召回：只在没有近处飞镖、没有近处对手时考虑
        if (me.st == S_FREE && md.st == D_GROUND && cf.recall) {
            bool safe = true;
            for (int k = 0; k < n; ++k) {
                if (k == seat || !dart_flying(base.d[k].st)) continue;
                if (ilen(base.d[k].x - me.x, base.d[k].y - me.y) < 1800) { safe = false; break; }
            }
            if (safe) {
                for (int k = 0; k < n; ++k) {
                    if (k == seat || !base.p[k].alive) continue;
                    if (ilen(base.p[k].x - me.x, base.p[k].y - me.y) < 2000) { safe = false; break; }
                }
            }
            if (safe) {
                Action a; a.act = A_RECALL;
                evalPush(a, 4);
            }
        }

        int bi = 0;
        for (int i = 1; i < nc && i < MAXC; ++i)
            if (best[i].sc > best[bi].sc) bi = i;
        res = best[bi].a;
        if (o.t < MAXH) myA[o.t] = res;
        return res;
    }
};

DART_MAIN(Bot)
