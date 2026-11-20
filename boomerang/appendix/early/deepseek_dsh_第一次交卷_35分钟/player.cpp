// 飞镖乱斗 - 参赛程序
// 思路：用裁判同款 Game 从 4 步前的观察重建当前局面（自机用记录的真实历史），
// 再对候选动作做短程前向推演打分，选最安全且能击杀的一步。详见 notes.md。
#include "dart.hpp"
#include <algorithm>
#include <array>
#include <vector>
#include <cstdio>

using namespace dart;

static const int MAXH = 900;

struct Bot {
    Config cf;
    int n = 0, seat = 0;
    i64 W = 0, H = 0;

    // 自己每一步的真实状态与发出去的动作（用于重建）
    std::vector<Player> myP;
    std::vector<Dart> myD;
    std::vector<Action> myA;
    std::vector<char> haveP;

    // 对手最近一次看见的状态（用于处理草丛里的估计）
    Player lastSeen[MAXP];
    i64 lastSeenT[MAXP];
    bool everSeen[MAXP];

    i64 rngState = 12345;

    void init(const GameInfo& gi) {
        cf = gi.cfg;
        n = gi.n;
        seat = gi.seat;
        W = gi.M().width();
        H = gi.M().height();
        myP.assign(MAXH, Player());
        myD.assign(MAXH, Dart());
        myA.assign(MAXH, Action());
        haveP.assign(MAXH, 0);
        for (int k = 0; k < MAXP; ++k) {
            lastSeen[k] = Player();
            lastSeenT[k] = -1;
            everSeen[k] = false;
        }
        rngState = 0x9E3779B97F4A7C15ULL ^ (u64)(seat + 1) * 1234567ULL;
    }

    i64 rnd() {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 7;
        rngState ^= rngState << 17;
        return (i64)(rngState & 0x7FFFFFFF);
    }

    static void unit(i64 dx, i64 dy, i64& ux, i64& uy) {
        if (!scale_to(dx, dy, UNIT, ux, uy)) { ux = UNIT; uy = 0; }
    }

    // 记录自己（每步开头）
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

    // 看不见的对手：用最后一次看见的位置和速度估一个现在的位置，放进推演里当威胁
    void fillHidden(const Obs& o, Game& g, bool* hiddenEst) {
        for (int k = 0; k < MAXP; ++k) hiddenEst[k] = false;
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
            P.alive = 1;
            P.unknown = 0;
            P.st = S_FREE;
            P.timer = 0;
            P.vx = 0;
            P.vy = 0;
            P.team = info().team[k];
            g.p[k] = P;
            if (g.d[k].st == D_HAND) { g.d[k].x = P.x; g.d[k].y = P.y; }
            hiddenEst[k] = true;
        }
    }

    // 从 o.t-delay 的历史重建当前世界
    bool buildWorld(const Obs& o, Game& out) {
        const i64 T0 = o.t - cf.delay;
        Game g(info(), o);
        g.hist.reset();
        g.over = false;
        if (T0 < 0 || o.t <= 0) {
            g.t = o.t;
            out = g;
            return true;
        }
        if (!haveP[T0] || T0 >= MAXH) {
            g.t = o.t;
            out = g;
            return false;
        }
        g.t = T0;
        g.p[seat] = myP[T0];
        g.p[seat].unknown = 0;
        g.d[seat] = myD[T0];
        for (i64 s = T0; s < o.t; ++s) {
            Action acts[MAXP];
            for (int k = 0; k < g.n; ++k) {
                if (k == seat) {
                    if (s < MAXH) acts[k] = myA[s];
                    continue;
                }
                const Player& P = g.p[k];
                Action a;
                if (P.alive && !P.unknown) moveFromVel(P, a);
                acts[k] = a;
            }
            g.step(acts);
            if (!g.p[seat].alive) {
                // 重建与真实冲突（被推断动作害死），退回不重建
                Game h(info(), o);
                h.hist.reset();
                h.over = false;
                h.t = o.t;
                out = h;
                return false;
            }
        }
        if (g.t != o.t) g.t = o.t;
        g.p[seat] = o.me();
        g.p[seat].unknown = 0;
        g.d[seat] = o.my_dart();
        g.over = false;
        out = g;
        return true;
    }

    // 根据观察到的内部速度推断"人下一步会继续怎么走"：按当前状态的最大速度把速度换算成输入向量长度
    void moveFromVel(const Player& P, Action& a) {
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

    Action contAction(const Player& P) {
        Action a;
        if (!P.alive) return a;
        moveFromVel(P, a);
        return a;
    }

    // 推演里对手的行为模型：继续走；手里有镖且在射程内就朝我扔，很近就挥砍/踢。
    // 这是一种偏悲观的假设，用来逼自机走位躲镖。
    Action enemyAction(const Game& g, int k) {
        Action a = contAction(g.p[k]);
        const Player& P = g.p[k];
        const Player& T = g.p[seat];
        if (!P.alive || P.unknown || !T.alive) return a;
        if (info().team[k] == info().team[seat]) return a;
        if (P.st != S_FREE) return a;
        const i64 dx = T.x - P.x, dy = T.y - P.y;
        const i64 dd = ilen(dx, dy);
        if (dd > 5000) return a;
        if (dd > 0 && wall_between(g.M(), P.x, P.y, T.x, T.y)) return a;
        if (g.d[k].st == D_HAND) {
            if (dd <= 1450) {
                a.act = A_MELEE;
                a.ax = dx; a.ay = dy;
            } else if (dd <= 5000) {
                a.act = A_THROW;
                a.ax = dx; a.ay = dy;
            }
        } else {
            if (dd <= cf.kick_reach) {
                a.act = A_MELEE;
                a.ax = dx; a.ay = dy;
            }
        }
        return a;
    }

    // 预测每个对手未来 H 步的位置（对手按当前速度继续走、不出手）
    struct Paths {
        i64 x[MAXP][20];
        i64 y[MAXP][20];
        bool alive[MAXP][20];
        bool valid[MAXP];
    };

    void predictPaths(const Game& base, int HH, Paths& out) {
        Game g = base;
        g.hist.reset();
        g.over = false;
        for (int k = 0; k < g.n; ++k) {
            out.valid[k] = g.p[k].alive && !g.p[k].unknown;
            for (int s = 0; s < 20; ++s) {
                out.x[k][s] = g.p[k].x;
                out.y[k][s] = g.p[k].y;
                out.alive[k][s] = g.p[k].alive != 0;
            }
        }
        for (int s = 1; s <= HH && s < 20; ++s) {
            Action acts[MAXP];
            for (int k = 0; k < g.n; ++k) acts[k] = (k == seat) ? Action() : contAction(g.p[k]);
            g.step(acts);
            g.over = false;
            for (int k = 0; k < g.n; ++k) {
                out.x[k][s] = g.p[k].x;
                out.y[k][s] = g.p[k].y;
                out.alive[k][s] = g.p[k].alive != 0;
            }
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

    // 单个候选动作的推演评分
    i64 dbgEndX = 0, dbgEndY = 0, dbgN = 0; double dbgSafe=0, dbgNd=-1, dbgRisk=0;
    double evaluate(const Game& base, const Action& first, int HH, double& killOut) {
        Game g = base;
        g.hist.reset();
        g.over = false;
        const i64 k0 = g.p[seat].kills;
        killOut = 0;
        double score = 0;
        bool dead = false;
        int deadAt = HH;
        for (int s = 0; s < HH; ++s) {
            Action acts[MAXP];
            for (int k = 0; k < g.n; ++k) {
                if (k == seat) {
                    if (s == 0) acts[k] = first;
                    else {
                        Action a;
                        a.mx = first.mx; a.my = first.my;
                        if (g.p[seat].st == S_RECALL) { a.mx = 0; a.my = 0; }
                        acts[k] = a;
                    }
                } else {
                    acts[k] = enemyAction(g, k);
                }
            }
            g.step(acts);
            g.over = false;
            if (!g.p[seat].alive) { dead = true; deadAt = s; break; }
            const i64 km = g.p[seat].kills;
            if (km > k0) {
                const i64 d = km - k0;
                score += 22000.0 * (double)d;
            }
            // 中间生存奖励
            score += 2.0;
        }
        dbgEndX = g.p[seat].x; dbgEndY = g.p[seat].y; dbgN = (i64)g.n;
        killOut = (double)(g.p[seat].kills - k0);
        if (dead) return -1.0e8 + (double)deadAt * 100.0 + score * 0.01;

        const Player& me = g.p[seat];
        const Dart& md = g.d[seat];
        // 找最近对手
        i64 nearest = -1, nd = 0;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || !g.p[k].alive) continue;
            const i64 d = ilen(g.p[k].x - me.x, g.p[k].y - me.y);
            if (nearest < 0 || d < nd) { nearest = k; nd = d; }
        }
        const bool armed = (md.st == D_HAND);
        dbgNd = (double)nd; dbgRisk = 0;

        // 手里的镖很值钱
        if (md.st == D_HAND) score += 150;
        else if (md.st == D_OUT || md.st == D_BACK || md.st == D_RISE || md.st == D_CALL) score += 35;
        else if (md.st == D_GROUND) {
            const i64 dd = ilen(md.x - me.x, md.y - me.y);
            score -= (double)dd * 0.035;
            if (dd <= cf.pick_r) score += 110;
        }
        // 占位安全
        { double _s = cellSafety(g, me.x, me.y); score += _s; dbgSafe = _s; }
        // 离边界
        const i64 bd = imin(imin(me.x, W - me.x), imin(me.y, H - me.y));
        if (bd < 800) score -= (800 - bd) * 0.06;

        if (nearest >= 0) {
            const bool foeArmed = (g.d[nearest].st == D_HAND);
            const bool los = !wall_between(g.M(), me.x, me.y, g.p[nearest].x, g.p[nearest].y);
            if (armed && foeArmed) {
                const double pref = (cf.mode == M_FFA) ? 2800.0 : 2500.0;
                if ((double)nd > pref + 1500) score -= std::min((double)nd - pref - 1500, 4500.0) * 0.05;
                else if ((double)nd < pref - 900) score -= (pref - 900.0 - (double)nd) * 0.08;
                else score += 40;
                if (los) score += 25;
            } else if (armed && !foeArmed) {
                const double pref = (cf.mode == M_FFA) ? 2200.0 : 1250.0;
                if ((double)nd > pref) score -= std::min((double)nd - pref, 6000.0) * 0.05;
                else if ((double)nd < 750) score -= (750.0 - (double)nd) * 0.05;
                else score += 20;
                if (los) score += 20;
            } else {
                if (nd < 3000) score -= (3000.0 - (double)nd) * 0.08;
            }
        }
        // 四人局：躲开有镖的对手，抓住对方空手/硬直的机会
        if (cf.mode == M_FFA) {
            for (int k = 0; k < g.n; ++k) {
                if (k == seat || !g.p[k].alive) continue;
                const i64 d = ilen(g.p[k].x - me.x, g.p[k].y - me.y);
                const bool foeArmed = (g.d[k].st == D_HAND);
                const bool busy = (g.p[k].st != S_FREE);
                if (!armed && foeArmed && d < 1900) score -= (1900.0 - (double)d) * 0.13;
                if (busy && d < 2700) score += (2700.0 - (double)d) * 0.035;
                if (d < 1450) score -= (1450.0 - (double)d) * 0.05;
                if (g.p[k].st == S_FREE && foeArmed && d < 2000) score -= (2000.0 - (double)d) * 0.12;
            }
        }
        // 在草丛里稍微加分（隐蔽）
        const int mc = cell_at_step(cf, g.M(), cell_index(me.x), cell_index(me.y), g.t);
        if (mc == C_GRASS && md.st == D_HAND) score += 25;
        return score;
    }

    struct Cand {
        Action a;
        double score;
        int kind;  // 0 move, 1 throw, 2 melee, 3 dash, 4 recall
        i64 ex, ey; double safe, nd, risk;
    };

    Action decide(const Obs& o) {
        recordSelf(o);
        const Player& me = o.me();
        Action res;
        if (!me.alive) { myA[o.t] = res; return res; }

        Game base;
        buildWorld(o, base);
        base.hist.reset();
        base.over = false;
        bool hiddenEst[MAXP];
        fillHidden(o, base, hiddenEst);

        const int HH = 16;
        const int fgate = (cf.mode == M_FFA) ? 7 : 8;
        Paths paths;
        predictPaths(base, 15, paths);

        // ---------- 基础移动候选 ----------
        Cand best[256];
        int nc = 0;
        auto push = [&](const Action& a, double sc, int kind) {
            if (nc < 256) { best[nc].a = a; best[nc].score = sc; best[nc].kind = kind; best[nc].ex = dbgEndX; best[nc].ey = dbgEndY; best[nc].safe=dbgSafe; best[nc].nd=dbgNd; best[nc].risk=dbgRisk; ++nc; }
        };
        auto evalPush = [&](const Action& a, int kind) {
            double kk = 0;
            double sc = evaluate(base, a, HH, kk);
            // 平手时的确定性抖动，避免座位偏向
            sc += (double)((rnd() % 1000)) * 0.001;
            push(a, sc, kind);
        };

        // 16 个方向 + 停
        i64 dirs[24][2];
        int ndir = 0;
        for (int i = 0; i < 16; ++i) {
            const i64 ang = i * 2250;
            dirs[ndir][0] = Game::icos_face(ang);
            dirs[ndir][1] = Game::isin_face(ang);
            ++ndir;
        }
        dirs[ndir][0] = 0; dirs[ndir][1] = 0; ++ndir;

        // 最近对手、自己镖
        int tgt = -1;
        i64 tgtD = 0;
        for (int k = 0; k < n; ++k) {
            if (k == seat) continue;
            if (!base.p[k].alive || base.p[k].unknown) continue;
            const i64 d = ilen(base.p[k].x - me.x, base.p[k].y - me.y);
            if (tgt < 0 || d < tgtD) { tgt = k; tgtD = d; }
        }
        const Dart& md = o.my_dart();
        if (tgt >= 0) {
            i64 ux, uy;
            unit(base.p[tgt].x - me.x, base.p[tgt].y - me.y, ux, uy);
            dirs[ndir][0] = ux; dirs[ndir][1] = uy; ++ndir;
            dirs[ndir][0] = -ux; dirs[ndir][1] = -uy; ++ndir;
        }
        if (md.st == D_GROUND) {
            i64 ux, uy;
            unit(md.x - me.x, md.y - me.y, ux, uy);
            dirs[ndir][0] = ux; dirs[ndir][1] = uy; ++ndir;
        }
        // 躲镖：垂直于每把敌方飞镖的运动方向
        for (int k = 0; k < n && ndir < 23; ++k) {
            if (k == seat || !dart_flying(base.d[k].st)) continue;
            const Dart& D = base.d[k];
            i64 vx = D.vx, vy = D.vy;
            if (D.st == D_OUT) {
                i64 ux, uy;
                unit(D.vx, D.vy, ux, uy);
                vx = ux; vy = uy;
            }
            i64 ux, uy;
            unit(-vy, vx, ux, uy);
            dirs[ndir][0] = ux; dirs[ndir][1] = uy; ++ndir;
            if (ndir < 23) { dirs[ndir][0] = -ux; dirs[ndir][1] = -uy; ++ndir; }
            // 远离这把镖
            i64 ax, ay;
            unit(me.x - D.x, me.y - D.y, ax, ay);
            if (ndir < 23) { dirs[ndir][0] = ax; dirs[ndir][1] = ay; ++ndir; }
        }
        {
            i64 ux, uy;
            unit(W / 2 - me.x, H / 2 - me.y, ux, uy);
            dirs[ndir][0] = ux; dirs[ndir][1] = uy; ++ndir;
        }

        for (int i = 0; i < ndir; ++i) {
            Action a;
            a.mx = dirs[i][0];
            a.my = dirs[i][1];
            evalPush(a, 0);
        }

        // 记住最好的基础移动方向
        int bestMoveIdx = -1;
        double bestMoveScore = -1e18;
        for (int i = 0; i < nc; ++i)
            if (best[i].kind == 0 && best[i].score > bestMoveScore) {
                bestMoveScore = best[i].score;
                bestMoveIdx = i;
            }
        const Action bestMove = (bestMoveIdx >= 0) ? best[bestMoveIdx].a : Action();

        // ---------- 近战候选 ----------
        if (me.st == S_FREE) {
            for (int k = 0; k < n; ++k) {
                if (k == seat || !base.p[k].alive || base.p[k].unknown || hiddenEst[k]) continue;
                for (int j = 0; j <= 1; ++j) {
                    if (!paths.alive[k][j]) continue;
                    const i64 dx = paths.x[k][j] - me.x, dy = paths.y[k][j] - me.y;
                    if (ilen(dx, dy) > cf.slash_reach + 600) continue;
                    Action a;
                    a.act = A_MELEE;
                    unit(dx, dy, a.ax, a.ay);
                    evalPush(a, 2);
                }
            }
            // 格挡：朝最近的敌方飞镖
            for (int k = 0; k < n; ++k) {
                if (k == seat || !dart_flying(base.d[k].st)) continue;
                const i64 dx = base.d[k].x - me.x, dy = base.d[k].y - me.y;
                const i64 dd = ilen(dx, dy);
                if (dd < 1700) {
                    Action a;
                    a.act = A_MELEE;
                    unit(dx, dy, a.ax, a.ay);
                    evalPush(a, 2);
                }
            }
        }

        // ---------- 扔镖候选 ----------
        if (me.st == S_FREE && md.st == D_HAND && cf.mode == M_FFA) {
            int order[MAXP], no = 0;
            for (int k = 0; k < n; ++k) {
                if (k == seat || !base.p[k].alive || base.p[k].unknown || hiddenEst[k]) continue;
                order[no++] = k;
            }
            for (int a = 0; a < no; ++a)
                for (int b = a + 1; b < no; ++b) {
                    auto pri = [&](int k) {
                        double p = 0;
                        if (base.p[k].st != S_FREE) p += 1200;
                        if (base.d[k].st != D_HAND) p += 600;
                        p -= (double)ilen(base.p[k].x - me.x, base.p[k].y - me.y) * 0.05;
                        return p;
                    };
                    if (pri(order[b]) > pri(order[a])) std::swap(order[a], order[b]);
                }
            int tried = 0;
            for (int oi = 0; oi < no && tried < 16; ++oi) {
                const int k = order[oi];
                const i64 D = ilen(base.p[k].x - me.x, base.p[k].y - me.y);
                i64 cum = 0;
                int arrive = 2;
                for (int i = 0; i < 14; ++i) {
                    cum += imax(cf.dart_vmin, cf.dart_v - i * cf.dart_dec);
                    if (cum >= D) { arrive = 2 + i; break; }
                    arrive = 2 + i + 1;
                }
                for (int dj = -2; dj <= 2 && tried < 16; ++dj) {
                    const int j = arrive + dj;
                    if (j < 2 || j > 15) continue;
                    if (base.p[k].st == S_FREE && j > fgate) continue;
                    if (!paths.alive[k][j]) continue;
                    const i64 dx = paths.x[k][j] - me.x, dy = paths.y[k][j] - me.y;
                    const i64 dd = ilen(dx, dy);
                    if (dd < 500 || dd > cf.range + 1500) continue;
                    if (wall_between(base.M(), me.x, me.y, paths.x[k][j], paths.y[k][j])) continue;
                    Action a;
                    a.act = A_THROW;
                    unit(dx, dy, a.ax, a.ay);
                    a.mx = bestMove.mx; a.my = bestMove.my;
                    if (dd < 4200) { i64 ux, uy; unit(dx, dy, ux, uy); a.mx = ux / 2; a.my = uy / 2; }
                    evalPush(a, 1);
                    ++tried;
                }
            }
        }
        // 1v1：旧版瞄准（对未来位置做更宽的搜索）
        if (me.st == S_FREE && md.st == D_HAND && cf.mode != M_FFA) {
            int tried = 0;
            for (int k = 0; k < n && tried < 10; ++k) {
                if (k == seat || !base.p[k].alive || base.p[k].unknown || hiddenEst[k]) continue;
                for (int j = 16; j >= 2 && tried < 12; j -= 1) {
                    if (!paths.alive[k][j]) continue;
                    const i64 dx = paths.x[k][j] - me.x, dy = paths.y[k][j] - me.y;
                    const i64 dd = ilen(dx, dy);
                    if (dd < 800 || dd > cf.range + 1500) continue;
                    if (j > 14) continue;
                    if (base.p[k].st == S_FREE && j > 8) continue;
                    if (wall_between(base.M(), me.x, me.y, paths.x[k][j], paths.y[k][j])) continue;
                    Action a;
                    a.act = A_THROW;
                    unit(dx, dy, a.ax, a.ay);
                    a.mx = bestMove.mx; a.my = bestMove.my;
                    if (j <= 8) { i64 ux, uy; unit(dx, dy, ux, uy); a.mx = ux / 2; a.my = uy / 2; }
                    evalPush(a, 1);
                    ++tried;
                }
            }
            if (tgt >= 0 && tried < 12) {
                for (int j = 2; j <= 4; ++j) {
                    if (!paths.alive[tgt][j]) continue;
                    Action a;
                    a.act = A_THROW;
                    unit(paths.x[tgt][j] - me.x, paths.y[tgt][j] - me.y, a.ax, a.ay);
                    evalPush(a, 1);
                }
            }
        }

        // ---------- 冲刺候选 ----------
        if (me.st == S_FREE && me.cd == 0) {
            i64 dashes[12][2];
            int nd2 = 0;
            if (tgt >= 0) {
                i64 ux, uy;
                unit(base.p[tgt].x - me.x, base.p[tgt].y - me.y, ux, uy);
                dashes[nd2][0] = ux; dashes[nd2][1] = uy; ++nd2;
            }
            {
                i64 ux, uy;
                unit(W / 2 - me.x, H / 2 - me.y, ux, uy);
                dashes[nd2][0] = ux; dashes[nd2][1] = uy; ++nd2;
            }
            for (int i = 0; i < 8 && nd2 < 12; ++i) {
                i64 ux = Game::icos_face(i * 4500), uy = Game::isin_face(i * 4500);
                dashes[nd2][0] = ux; dashes[nd2][1] = uy; ++nd2;
            }
            for (int i = 0; i < nd2; ++i) {
                Action a;
                a.act = A_DASH;
                a.ax = dashes[i][0];
                a.ay = dashes[i][1];
                evalPush(a, 3);
                if (cf.mode == M_FFA) best[nc - 1].score -= 60.0;
            }
        }

        // ---------- 召回候选 ----------
        if (me.st == S_FREE && md.st == D_GROUND && cf.recall) {
            Action a;
            a.act = A_RECALL;
            evalPush(a, 4);
        }

        // ---------- 选择 ----------
        int bi = 0;
        for (int i = 1; i < nc; ++i)
            if (best[i].score > best[bi].score) bi = i;
        if (o.t < 40) {
            fprintf(stderr, "t=%lld meAlive=%lld baseAlive=%lld baseSt=%lld nc=%d bestKind=%d bestScore=%.1f\n",
                    (long long)o.t, (long long)me.alive, (long long)base.p[seat].alive,
                    (long long)base.p[seat].st, nc, best[bi].kind, best[bi].score);
            for (int i = 0; i < nc && i < 6; ++i) {
                fprintf(stderr, "   cand%d kind=%d act=%lld dir=(%lld,%lld) sc=%.1f end=(%lld,%lld)",
                        i, best[i].kind, (long long)best[i].a.act, (long long)best[i].a.mx,
                        (long long)best[i].a.my, best[i].score, (long long)best[i].ex, (long long)best[i].ey);
                fprintf(stderr, " safe=%.1f nd=%.1f risk=%.1f\n", best[i].safe, best[i].nd, best[i].risk);
            }
        }
        // 微小安全余量：如果最优候选是死亡，尽量选晚死的
        res = best[bi].a;
        if (o.t < MAXH) myA[o.t] = res;
        return res;
    }
};

DART_MAIN(Bot)
