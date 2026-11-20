// player.cpp v2 —— 延迟补偿 + 模拟躲镖（扫掠判定）+ 谨慎召回 + 启发式进攻
//
// 结构：
//  1) 历史 ring：每步存（观察, 动作）。
//  2) build_belief：从 delay 步前的旧观察建 Game，用自己的真实动作推到现在；别人按当时速度 coast。
//  3) threat_sim：在 belief 副本上 coast H 步，用引擎同款 seg_near_origin 扫掠判断我是否会被镖打中、
//     最近的镖多近、是否被砍/踢、是否落坑。
//  4) 有威胁且能动 → 枚举闪避候选（两个垂直方向跑/冲刺、朝镖格挡、站定），模拟选最优。
//  5) 无威胁 → 宏：捡镖（优先走捡，召回只在安全时）、塌陷避险、预判扔镖、近战/冲刺斩、走位逼近+横摆。
#include "dart.hpp"
using namespace dart;

namespace {

const bool DBG = false;

struct Bot {
    static constexpr int HIST = 32;
    Obs ho[HIST];
    Action ha[HIST];
    int hcnt = 0;

    void init(const GameInfo& gi) { (void)gi; hcnt = 0; }

    static i64 sq(i64 x) { return x * x; }
    i64 d2(i64 x1, i64 y1, i64 x2, i64 y2) const { return sq(x1 - x2) + sq(y1 - y2); }
    i64 dist(i64 x1, i64 y1, i64 x2, i64 y2) const { return isqrt(d2(x1, y1, x2, y2)); }
    bool mate(const GameInfo& gi, int k) const { return is_mate(gi, k); }

    // ---------- 延迟补偿 ----------
    Game build_belief(const Obs& o) {
        const GameInfo& gi = info();
        const int D = (int)gi.cfg.delay;
        Game g(gi, o);
        if (D <= 0 || hcnt < 1) return g;
        for (int i = 1; i <= hcnt && i < HIST; ++i) {
            const Obs& old = ho[(hcnt - i + HIST) % HIST];
            if (old.t != o.t - D) continue;
            Game gg(gi, old);
            for (int s = 0; s < D; ++s) {
                const int hi = hcnt - D + s;
                if (hi < 0 || hi >= hcnt) break;
                Action acts[MAXP];
                for (int k = 0; k < gg.n; ++k) acts[k] = Action();
                acts[gi.seat] = ha[hi % HIST];
                if (gg.over) gg.over = false;
                gg.step(acts);
            }
            return gg;
        }
        return g;
    }

    int pick_target(const Game& g, const GameInfo& gi) {
        int tgt = -1;
        i64 best = 0;
        const Player& me = g.p[gi.seat];
        for (int i = 1; i < g.n; ++i) {
            const int k = (gi.seat + i) % g.n;
            const Player& P = g.p[k];
            if (!P.alive || P.unknown || mate(gi, k)) continue;
            const i64 dd = d2(P.x, P.y, me.x, me.y);
            if (tgt < 0 || dd < best) { tgt = k; best = dd; }
        }
        return tgt;
    }

    // ---------- 威胁模拟 ----------
    struct Thr { bool dead; bool melee; i64 mind; i64 tmind; };
    // coast H 步，所有活人按当前内部速度继续（别人不出手）。检测我被飞镖扫中 / 被砍踢 / 落坑。
    Thr threat_sim(const Game& g0, int H) {
        const GameInfo& gi = info();
        Game g = g0;
        const int me = gi.seat;
        const i64 R = gi.cfg.body_r + gi.cfg.dart_r;
        i64 mind = 1LL << 30, tmind = 1LL << 30;
        bool dead = false, melee = false;
        for (int s = 0; s < H; ++s) {
            if (g.over) break;
            Action acts[MAXP];
            for (int k = 0; k < g.n; ++k) {
                acts[k] = Action();
                const Player& P = g.p[k];
                if (P.alive && !P.unknown) { acts[k].mx = P.vx; acts[k].my = P.vy; }
            }
            g.step(acts);
            if (!g.p[me].alive) { dead = true; break; }
            for (int k = 0; k < g.n; ++k) {
                if (k == me || mate(gi, k)) continue;
                const Player& E = g.p[k];
                if (E.alive && !E.unknown) {
                    const i64 bd = dist(E.x, E.y, g.p[me].x, g.p[me].y);
                    if (bd < tmind) tmind = bd;
                    if ((E.st == S_SLASH || E.st == S_KICK) && bd < gi.cfg.slash_reach + 200) melee = true;
                }
                const Dart& D = g.d[k];
                if (!dart_flying(D.st)) continue;
                // 用上一位置（D 已是 step 后）：用扫掠需要 step 前点，这里用当前距离做近度，
                // 命中由下面 hit 检测在 eval 里做；这里只衡量近度
                const i64 dd = dist(D.x, D.y, g.p[me].x, g.p[me].y);
                if (dd < mind) mind = dd;
            }
            (void)R;
        }
        return {dead, melee, mind, tmind};
    }

    // 候选闪避评分：先执行 cand，再 coast；用扫掠严格判断我是否被任一敌镖打中。
    i64 eval_evade(const Game& g0, const Action& cand, int H, int tgt) {
        const GameInfo& gi = info();
        Game g = g0;
        const int me = gi.seat;
        const i64 R = gi.cfg.body_r + gi.cfg.dart_r;
        Action acts[MAXP];
        for (int k = 0; k < g.n; ++k) acts[k] = Action();
        acts[me] = cand;
        i64 mpx = g.p[me].x, mpy = g.p[me].y;
        g.step(acts);
        if (!g.p[me].alive) return -(1LL << 40);
        i64 score = 0;
        for (int s = 1; s < H; ++s) {
            if (g.over) break;
            // 记录每把敌镖 step 前位置
            i64 dpx[MAXP], dpy[MAXP];
            for (int k = 0; k < g.n; ++k) { dpx[k] = g.d[k].x; dpy[k] = g.d[k].y; }
            for (int k = 0; k < g.n; ++k) {
                acts[k] = Action();
                const Player& P = g.p[k];
                if (P.alive && !P.unknown) { acts[k].mx = P.vx; acts[k].my = P.vy; }
            }
            const i64 px = g.p[me].x, py = g.p[me].y;
            g.step(acts);
            if (!g.p[me].alive) return -(1LL << 40) + s;
            const Player& MN = g.p[me];
            for (int k = 0; k < g.n; ++k) {
                if (k == me || mate(gi, k)) continue;
                const Dart& D = g.d[k];
                if (!dart_flying(D.st) && !(D.st == D_GROUND)) continue;
                if (dart_flying(D.st)) {
                    // 相对运动扫掠：镖 dpx->D.x，我 px->MN.x
                    if (seg_near_origin(dpx[k] - px, dpy[k] - py, D.x - MN.x, D.y - MN.y, R))
                        return -(1LL << 40) + s;
                    const i64 dd = dist(D.x, D.y, MN.x, MN.y);
                    if (dd < R + 700) score -= (R + 700 - dd);
                }
                // 敌方近战威胁
                const Player& E = g.p[k];
                if (E.alive && !E.unknown && (E.st == S_SLASH || E.st == S_KICK)) {
                    const i64 bd = dist(E.x, E.y, MN.x, MN.y);
                    if (bd < gi.cfg.slash_reach + 100) score -= 4000;
                }
            }
            (void)mpx; (void)mpy;
        }
        const Player& MN = g.p[me];
        if (!standable(gi, MN.x, MN.y, g.t)) score -= 30000;
        const int cx = cell_index(MN.x), cy = cell_index(MN.y);
        const i64 fall = cell_fall_step(gi, cx, cy);
        if (fall >= 0 && fall - g.t < 20) score -= (20 - (fall - g.t)) * 400;
        if (tgt >= 0 && g.p[tgt].alive) score -= dist(MN.x, MN.y, g.p[tgt].x, g.p[tgt].y) / 25;
        return score;
    }

    // —— 安全寻路缓存：从我所在格 BFS，得到每个可达格离我的步数 ——
    int nav_nx = 0, nav_ny = 0;
    i64 nav_t = -1; int nav_gx = -1, nav_gy = -1, nav_sx = -1, nav_sy = -1;
    std::vector<int> nav_dist;  // -1 = 不可达/不可站

    bool cell_ok(const GameInfo& gi, int cx, int cy, i64 t) const {
        const Map& M = gi.M();
        if (!M.inside(cx, cy)) return false;
        const int cc = cell_at_step(gi.cfg, M, cx, cy, t);
        if (cc != C_FLOOR && cc != C_GRASS) return false;
        const i64 f = cell_fall_step(gi, cx, cy);  // 12 步内要塌的格子视为不可站
        if (f >= 0 && f - t < 12) return false;
        return true;
    }

    // 从我所在格 (scx,scy) 做 BFS，距离场 = 每个可达格离我的步数。
    // 走位时在邻格里选"离目标 (egx,egy) 欧氏最近"的可达格下降，天然只在我的连通分量里走、绝不踩坑。
    void build_nav(const GameInfo& gi, i64 t, int scx, int scy, int egx, int egy) {
        const Map& M = gi.M();
        nav_nx = M.nx; nav_ny = M.ny; nav_t = t; nav_gx = egx; nav_gy = egy; nav_sx = scx; nav_sy = scy;
        nav_dist.assign(size_t(M.nx) * M.ny, -1);
        auto ok = [&](int cx, int cy) { return cell_ok(gi, cx, cy, t); };
        if (!ok(scx, scy)) return;
        std::vector<int> q; q.reserve(M.nx * M.ny);
        size_t head = 0;
        nav_dist[size_t(scy) * M.nx + scx] = 0;
        q.push_back(scy * M.nx + scx);
        while (head < q.size()) {
            const int id = q[head++];
            const int cx = id % M.nx, cy = id / M.nx;
            const int d0 = nav_dist[id];
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int nx = cx + dx, ny = cy + dy;
                    if (!ok(nx, ny)) continue;
                    if (dx && dy && (!ok(cx + dx, cy) || !ok(cx, cy + dy))) continue;
                    const int nid = ny * M.nx + nx;
                    if (nav_dist[nid] >= 0) continue;
                    nav_dist[nid] = d0 + 1;
                    q.push_back(nid);
                }
        }
    }

    // 安全走位：在从我可达的邻格里，优先选 BFS 距离变小且离目标欧氏最近的；无路可走也只选安全格。绝不踩坑。
    bool safe_step(const GameInfo& gi, i64 x, i64 y, i64 gx, i64 gy, i64 t, i64& wx, i64& wy) {
        const Map& M = gi.M();
        const int scx = cell_index(x), scy = cell_index(y);
        const int egx = cell_index(gx), egy = cell_index(gy);
        if (scx == egx && scy == egy) { wx = gx; wy = gy; return true; }
        if (nav_nx != M.nx || nav_ny != M.ny || nav_t != t || nav_sx != scx || nav_sy != scy ||
            nav_gx != egx || nav_gy != egy)
            build_nav(gi, t, scx, scy, egx, egy);
        const int curd = nav_dist.empty() ? -1 : nav_dist[size_t(scy) * M.nx + scx];
        if (curd < 0) {
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    if (cell_ok(gi, scx + dx, scy + dy, t)) { wx = cell_center(scx + dx); wy = cell_center(scy + dy); return true; }
                }
            return false;
        }
        // 目标格是否在我的连通分量里
        const int goal_d = M.inside(egx, egy) ? nav_dist[size_t(egy) * M.nx + egx] : -1;
        i64 bestl = 1LL << 40; bool found = false, strict = false;
        int bxx = scx, byy = scy;
        if (goal_d >= 0) {
            // 可达：在 BFS 距离变小的邻格里选离目标欧氏最近的
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int nx = scx + dx, ny = scy + dy;
                    if (!M.inside(nx, ny)) continue;
                    const int dd = nav_dist[size_t(ny) * M.nx + nx];
                    if (dd < 0 || dd >= curd) continue;
                    const i64 px = cell_center(nx), py = cell_center(ny);
                    const i64 ll = d2(px, py, gx, gy);
                    if (ll < bestl) { bestl = ll; bxx = nx; byy = ny; found = true; strict = true; }
                }
        } else {
            // 不可达（隔着湖/坑）：沿岸走——选可达邻格里离目标欧氏最近、且尽量远离我（BFS 距离变大）的，
            // 这样会沿岸边绕路而不是直直踩进坑里。
            i64 bestScore = -(1LL << 40);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int nx = scx + dx, ny = scy + dy;
                    if (!M.inside(nx, ny)) continue;
                    const int dd = nav_dist[size_t(ny) * M.nx + nx];
                    if (dd < 0) continue;
                    const i64 px = cell_center(nx), py = cell_center(ny);
                    const i64 ll = d2(px, py, gx, gy);  // 单位 mm²
                    const i64 sc = -ll + i64(dd) * 40000LL;  // 欧氏接近为主，沿分量推进为辅，避免贴坑不动
                    if (sc > bestScore) { bestScore = sc; bxx = nx; byy = ny; found = true; }
                }
        }
        if (!found) {
            // 兜底：在任意可站邻格里选离目标最近的，保证"有目标就不会原地发呆"
            i64 fb = 1LL << 40; bool any = false;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int nx = scx + dx, ny = scy + dy;
                    if (!cell_ok(gi, nx, ny, t)) continue;
                    const i64 ll = d2(cell_center(nx), cell_center(ny), gx, gy);
                    if (ll < fb) { fb = ll; bxx = nx; byy = ny; any = true; }
                }
            if (!any) return false;
        }
        if (bxx == egx && byy == egy) { wx = gx; wy = gy; }
        else { wx = cell_center(bxx); wy = cell_center(byy); }
        if (DBG) std::cerr << "NAV t=" << t << " me(" << scx << "," << scy << ") goal(" << egx << "," << egy
                           << ") -> (" << bxx << "," << byy << ") curd=" << curd << " found=" << found << "\n";
        return true;
    }

    // 冲刺落点/路径安全：从 (x,y) 沿单位方向 (ux,uy) 冲 dash_t 步，检查每一步落点在对应时刻可站。
    // 前摇原地 2 步（位置不变，但前摇中也会判落坑——所以起点此刻必须可站），移动 4 步，最后一步判坑。
    bool dash_safe(const GameInfo& gi, i64 x, i64 y, i64 ux, i64 uy, i64 t0) const {
        const Config& c = gi.cfg;
        if (!standable(gi, x, y, t0)) return false;
        i64 cxp = x, cyp = y;
        for (int i = 1; i <= c.dash_t; ++i) {
            const i64 nx = x + rdiv(ux * c.dash_v * i, UNIT);
            const i64 ny = y + rdiv(uy * c.dash_v * i, UNIT);
            // 第 i 步移动发生在 t0 + dash_pre + i - 1 的结算里；最后一步（i==dash_t）判落坑
            const i64 tt = t0 + c.dash_pre + i - 1;
            if (!standable(gi, nx, ny, tt)) return false;
            if (circle_hits_wall(gi.M(), c.body_r, nx, ny)) return false;
            cxp = nx; cyp = ny;
        }
        (void)cxp; (void)cyp;
        return true;
    }

    int flight_steps(const GameInfo& gi, i64 need) const {
        const Config& c = gi.cfg;
        i64 acc = 0; int fly = 0;
        while (acc < need && fly < 60) { acc += imax(c.dart_vmin, c.dart_v - i64(fly) * c.dart_dec); ++fly; }
        return (int)c.throw_pre + 1 + fly;
    }
    void lead_point(const Player& P, int steps, i64& lx, i64& ly) const {
        lx = P.x + P.vx * steps; ly = P.y + P.vy * steps;
    }

    bool safe_goal(const GameInfo& gi, i64 x, i64 y, i64 t, i64& gx, i64& gy) {
        const Map& M = gi.M();
        const int cx = cell_index(x), cy = cell_index(y);
        for (int r = 1; r <= 8; ++r) {
            i64 bestd = 1LL << 40; bool found = false;
            for (int yy = cy - r; yy <= cy + r; ++yy)
                for (int xx = cx - r; xx <= cx + r; ++xx) {
                    if (!M.inside(xx, yy)) continue;
                    const int cc = cell_at_step(gi.cfg, M, xx, yy, t);
                    if (cc != C_FLOOR && cc != C_GRASS) continue;
                    const i64 f = cell_fall_step(gi, xx, yy);
                    if (f >= 0 && f - t < 40) continue;
                    const i64 px = cell_center(xx), py = cell_center(yy);
                    const i64 dd = d2(px, py, x, y);
                    if (dd < bestd) { bestd = dd; gx = px; gy = py; found = true; }
                }
            if (found) return true;
        }
        return false;
    }

    // 有没有活着的、看得见的、手里有镖的敌人在 near 内且有视线（他能立刻威胁我）
    bool enemy_can_throw(const Game& g, const GameInfo& gi, i64 near) const {
        const Player& me = g.p[gi.seat];
        for (int k = 0; k < g.n; ++k) {
            if (k == gi.seat || mate(gi, k)) continue;
            const Player& E = g.p[k];
            if (!E.alive || E.unknown) continue;
            if (g.d[k].st != D_HAND) continue;
            const i64 dd = dist(E.x, E.y, me.x, me.y);
            if (dd > near) continue;
            if (!wall_between(gi.M(), E.x, E.y, me.x, me.y)) return true;
        }
        return false;
    }
    // 有没有敌镖在飞且离我 within
    bool enemy_dart_near(const Game& g, const GameInfo& gi, i64 within) const {
        const Player& me = g.p[gi.seat];
        for (int k = 0; k < g.n; ++k) {
            if (k == gi.seat || mate(gi, k)) continue;
            const Dart& D = g.d[k];
            if (!dart_flying(D.st)) continue;
            if (dist(D.x, D.y, me.x, me.y) <= within) return true;
        }
        return false;
    }

    // 几何预判：有没有正在飞的敌镖会在未来 h 步内逼近到我身边（不依赖 coast 模拟，对刚出手的镖更敏感）
    bool incoming_dart(const Game& g, const GameInfo& gi, int h) const {
        const Player& me = g.p[gi.seat];
        const Config& c = gi.cfg;
        for (int k = 0; k < g.n; ++k) {
            if (k == gi.seat || mate(gi, k)) continue;
            const Dart& D = g.d[k];
            if (!dart_flying(D.st)) continue;
            i64 x = D.x, y = D.y;
            Dart tmp = D;
            for (int s = 0; s < h; ++s) {
                const i64 sp = dart_step_len(c, tmp);
                if (tmp.st == D_OUT) {
                    x += rdiv(tmp.vx * imin(sp, tmp.left), UNIT);
                    y += rdiv(tmp.vy * imin(sp, tmp.left), UNIT);
                } else if (sp > 0) {
                    const i64 gx = me.x - x, gy = me.y - y, gl = ilen(gx, gy);
                    if (gl <= sp) { x = me.x; y = me.y; }
                    else { x += rdiv(gx * sp, gl); y += rdiv(gy * sp, gl); }
                }
                tmp.x = x; tmp.y = y; ++tmp.age;
                if (dist(x, y, me.x, me.y) <= c.body_r + c.dart_r + 350) return true;
            }
        }
        return false;
    }

    // 最终保险：算出的移动落点若不可站（坑/将塌/墙/场外），就站住不动。绝不主动踩坑。
    void guard_move(const GameInfo& gi, const Game& g, const Obs& o, Action& a) {
        if (a.act == A_DASH || (a.mx == 0 && a.my == 0)) return;
        const int me = gi.seat;
        const Player& M = g.p[me];
        if (M.st != S_FREE && M.st != S_THROW && M.st != S_THROWN && M.st != S_RECALL) return;
        const i64 nx = M.x + a.mx, ny = M.y + a.my;
        if (cell_index(nx) == cell_index(M.x) && cell_index(ny) == cell_index(M.y)) return;
        if (!cell_ok(gi, cell_index(nx), cell_index(ny), o.t)) {
            if (DBG) std::cerr << "GUARD blocked move t=" << o.t << " (" << cell_index(M.x) << ","
                               << cell_index(M.y) << ")->(" << cell_index(nx) << "," << cell_index(ny) << ")\n";
            a.mx = a.my = 0;
        }
    }

    Action decide(const Obs& o) {
        const GameInfo& gi = info();
        const Config& cfg = gi.cfg;
        const int me = gi.seat;
        Action a;
        if (!o.me().alive) { record(o, a); return a; }

        Game g = build_belief(o);
        const Player& M = g.p[me];
        const Dart& MD = g.d[me];
        const int tgt = pick_target(g, gi);

        const int H = 12;
        Thr thr = threat_sim(g, H);
        const bool incoming = incoming_dart(g, gi, H);
        // 威胁：模拟会死、几何上有镖逼近、镖很近、或敌人近战逼近
        const bool threatened =
            thr.dead || incoming || thr.mind < cfg.body_r + cfg.dart_r + 450 ||
            (thr.melee && thr.tmind < cfg.slash_reach + 250);

        if (DBG) std::cerr << "t=" << o.t << " st=" << M.st << " thr=" << threatened
                           << " dead=" << thr.dead << " mind=" << thr.mind << " tmind=" << thr.tmind << "\n";

        const bool can_act = (M.st == S_FREE);
        const bool can_move = (M.st == S_FREE || M.st == S_THROW || M.st == S_THROWN);

        // ---------- 闪避 ----------
        if (threatened && (can_act || can_move)) {
            Action ev = make_evade(g, gi, o, H, tgt, can_act);
            guard_move(gi, g, o, ev);
            record(o, ev);
            return ev;
        }

        // ---------- 硬直/锁定状态 ----------
        if (M.st == S_RECALL) {
            // 召回中：一旦出现敌镖在近处或敌人能扔我，立刻中断（给移动）
            if (enemy_dart_near(g, gi, 5000) || enemy_can_throw(g, gi, 6000)) {
                i64 gx, gy;
                if (move_goal(gi, g, o, gx, gy)) {
                    i64 wx, wy;
                    if (safe_step(gi, M.x, M.y, gx, gy, o.t, wx, wy)) { a.mx = wx - M.x; a.my = wy - M.y; }
                }
                guard_move(gi, g, o, a);
                record(o, a); return a;
            }
            record(o, a); return a;  // 继续站住引导
        }
        if (M.st == S_DASH || M.st == S_STUN) { record(o, a); return a; }
        if (M.st == S_SLASH || M.st == S_SLASHED || M.st == S_KICK || M.st == S_KICKED) {
            record(o, a); return a;  // 这些状态不能移动，站住
        }
        if (M.st == S_THROW || M.st == S_THROWN) {
            // 还能 50% 速度走位，朝目标横摆
            strafe_move(gi, g, o, a, tgt);
            guard_move(gi, g, o, a);
            record(o, a); return a;
        }

        // ---------- S_FREE 宏决策 ----------
        // 3.1 镖在地上：优先走过去捡（最可靠），只有安全且走捡明显更慢时才原地召回
        if (MD.st == D_GROUND) {
            const i64 pd = dist(M.x, M.y, MD.x, MD.y);
            const i64 walk_steps = pd / imax(1, cfg.run_v);          // 走过去大约几步
            const bool dart_fly_near = enemy_dart_near(g, gi, 6000);
            const bool enemy_throws = enemy_can_throw(g, gi, 7000);
            // 召回要站定 12 步且会被近身惩罚：只有很远、且没有任何立即威胁时才用
            const bool safe_to_recall =
                cfg.recall && !dart_fly_near && !enemy_throws &&
                walk_steps > cfg.recall_pre + 6 && pd > 4500;
            if (safe_to_recall) { a.act = A_RECALL; record(o, a); return a; }
            // 走过去捡（safe_step 保证不踩坑）
            i64 wx, wy;
            if (safe_step(gi, M.x, M.y, MD.x, MD.y, o.t, wx, wy)) { a.mx = wx - M.x; a.my = wy - M.y; }
            // 空手时敌人贴近就踢（打断他出手/冲刺）
            if (tgt >= 0) {
                const Player& T = g.p[tgt];
                const i64 td = dist(M.x, M.y, T.x, T.y);
                if (td < 1400 && !wall_between(gi.M(), M.x, M.y, T.x, T.y) &&
                    !(T.st == S_STUN && T.timer > 1)) {
                    a.act = A_MELEE; a.ax = T.x - M.x; a.ay = T.y - M.y; a.mx = a.my = 0;
                }
            }
            guard_move(gi, g, o, a);
            record(o, a); return a;
        }

        // 3.2 手里有镖
        if (MD.st == D_HAND && tgt >= 0) {
            const Player& T = g.p[tgt];
            const i64 td = dist(M.x, M.y, T.x, T.y);
            const bool los = !wall_between(gi.M(), M.x, M.y, T.x, T.y);

            // 近战
            if (td <= cfg.slash_reach - 100 && los && melee_clear(gi, g, me, T.x - M.x, T.y - M.y)) {
                a.act = A_MELEE; a.ax = T.x - M.x; a.ay = T.y - M.y; a.mx = a.my = 0;
                record(o, a); return a;
            }
            // 冲刺接近战
            if (M.cd == 0 && td > 1400 && td < 3200 && los) {
                i64 dx2, dy2; scale_to(T.x - M.x, T.y - M.y, UNIT, dx2, dy2);
                if (dash_safe(gi, M.x, M.y, dx2, dy2, o.t)) {
                    a.act = A_DASH; a.ax = dx2; a.ay = dy2; a.mx = a.my = 0;
                    record(o, a); return a;
                }
            }
            // 扔镖：射程放到引擎上限附近（RANGE=7200），增加出手机会
            const i64 throw_max = cfg.mode == M_FFA ? 6500 : 7000;
            if (td < throw_max && los) {
                const int steps = flight_steps(gi, td);
                i64 lx, ly; lead_point(T, steps, lx, ly);
                lx = iclamp(lx, i64(200), gi.M().width() - 200);
                ly = iclamp(ly, i64(200), gi.M().height() - 200);
                i64 ax = lx - M.x, ay = ly - M.y;
                if (throw_clear(gi, g, me, ax, ay) && !wall_between(gi.M(), M.x, M.y, lx, ly)) {
                    a.act = A_THROW; a.ax = ax; a.ay = ay; a.mx = a.my = 0;
                    record(o, a); return a;
                }
            }
        }

        // 3.3 走位逼近 + 横摆
        strafe_move(gi, g, o, a, tgt);
        guard_move(gi, g, o, a);
        record(o, a);
        return a;
    }

    // 构造闪避动作
    Action make_evade(const Game& g, const GameInfo& gi, const Obs& o, int H, int tgt, bool can_act) {
        const Config& cfg = gi.cfg;
        const int me = gi.seat;
        const Player& M = g.p[me];
        const Dart& MD = g.d[me];
        // 最近的敌镖
        i64 dx = 1000, dy = 0; i64 bd = 1LL << 40; bool have = false;
        for (int k = 0; k < g.n; ++k) {
            if (k == me || mate(gi, k)) continue;
            const Dart& D = g.d[k];
            if (!dart_flying(D.st)) continue;
            const i64 dd = d2(D.x, D.y, M.x, M.y);
            if (dd < bd) { bd = dd; dx = D.x - M.x; dy = D.y - M.y; have = true; }
        }
        std::vector<Action> cands;
        // 只保留"第一步落点可站"的跑/冲方向，绝不朝坑/墙闪
        auto run_ok = [&](i64 vx, i64 vy) -> bool {
            i64 ux, uy;
            if (!scale_to(vx, vy, UNIT, ux, uy)) return false;
            const i64 nx = M.x + rdiv(ux * cfg.run_v, UNIT), ny = M.y + rdiv(uy * cfg.run_v, UNIT);
            return cell_ok(gi, cell_index(nx), cell_index(ny), o.t);
        };
        if (have) {
            // 镖的飞行方向（去程 vx,vy 是单位方向；回程用位移）
            i64 fxx = 1, fyy = 0;
            for (int k = 0; k < g.n; ++k) {
                if (k == me || mate(gi, k)) continue;
                const Dart& D = g.d[k];
                if (dart_flying(D.st) && d2(D.x, D.y, M.x, M.y) == bd) { fxx = D.vx; fyy = D.vy; break; }
            }
            if (fxx == 0 && fyy == 0) { fxx = dx; fyy = dy; }
            // 垂直于镖飞行方向的两个方向
            i64 px = -fyy, py = fxx;
            i64 perx[2] = {px, -px}, pery[2] = {py, -py};
            for (int q = 0; q < 2; ++q) {
                if (run_ok(perx[q], pery[q])) {
                    Action r; r.mx = perx[q]; r.my = pery[q]; cands.push_back(r);
                    if (can_act && M.cd == 0) {
                        i64 ux, uy; scale_to(perx[q], pery[q], UNIT, ux, uy);
                        if (dash_safe(gi, M.x, M.y, ux, uy, o.t)) {
                            Action dsh; dsh.act = A_DASH; dsh.ax = ux; dsh.ay = uy; cands.push_back(dsh);
                        }
                    }
                }
            }
            // 沿镖来向反向冲刺/跑（拉开距离，对直冲过来的镖有效）
            if (run_ok(-dx, -dy)) {
                Action r; r.mx = -dx; r.my = -dy; cands.push_back(r);
                if (can_act && M.cd == 0) {
                    i64 ux, uy; scale_to(-dx, -dy, UNIT, ux, uy);
                    if (dash_safe(gi, M.x, M.y, ux, uy, o.t)) {
                        Action dsh; dsh.act = A_DASH; dsh.ax = ux; dsh.ay = uy; cands.push_back(dsh);
                    }
                }
            }
            // 格挡：镖在 1500 内、我手里有镖（eval 会判断这一刀挡不挡得到）
            if (can_act && MD.st == D_HAND && bd <= 1500LL * 1500LL) {
                Action sl; sl.act = A_MELEE; sl.ax = dx; sl.ay = dy; cands.push_back(sl);
            }
        } else if (tgt >= 0) {
            // 没有飞行镖（近战威胁）：朝远离最近敌人的方向
            const Player& T = g.p[tgt];
            i64 rx = M.x - T.x, ry = M.y - T.y;
            if (run_ok(rx, ry)) {
                Action r; r.mx = rx; r.my = ry; cands.push_back(r);
                if (can_act && M.cd == 0) {
                    i64 ux, uy; scale_to(rx, ry, UNIT, ux, uy);
                    if (dash_safe(gi, M.x, M.y, ux, uy, o.t)) {
                        Action dsh; dsh.act = A_DASH; dsh.ax = ux; dsh.ay = uy; cands.push_back(dsh);
                    }
                }
            }
        }
        Action stay; cands.push_back(stay);

        i64 bestScore = -(1LL << 50); Action best = cands[0];
        for (const Action& c : cands) {
            const i64 sc = eval_evade(g, c, H, tgt);
            if (sc > bestScore) { bestScore = sc; best = c; }
        }
        // 所有方向都不安全（被坑/墙包住）时，退回安全寻路，别站着等死
        if (best.act == A_NONE && best.mx == 0 && best.my == 0) {
            i64 gx, gy;
            if (move_goal(gi, g, o, gx, gy)) {
                i64 wx, wy;
                if (safe_step(gi, M.x, M.y, gx, gy, o.t, wx, wy)) { best.mx = wx - M.x; best.my = wy - M.y; }
            }
        }
        return best;
    }

    void strafe_move(const GameInfo& gi, const Game& g, const Obs& o, Action& a, int tgt) {
        const Player& M = g.p[gi.seat];
        i64 gx, gy;
        if (!move_goal(gi, g, o, gx, gy)) return;
        i64 wx, wy;
        if (!safe_step(gi, M.x, M.y, gx, gy, o.t, wx, wy)) return;
        i64 mx = wx - M.x, my = wy - M.y;
        if (tgt >= 0 && g.p[tgt].alive) {
            const Player& T = g.p[tgt];
            i64 rx = T.x - M.x, ry = T.y - M.y;
            if (ilen(rx, ry) > 1200) {
                const i64 sgn = ((o.t / 12) % 2 == 0) ? 1 : -1;
                mx += rdiv((-ry) * 350 * sgn, UNIT);
                my += rdiv(rx * 350 * sgn, UNIT);
            }
        }
        a.mx = mx; a.my = my;
    }

    bool move_goal(const GameInfo& gi, const Game& g, const Obs& o, i64& gx, i64& gy) {
        const Player& M = g.p[gi.seat];
        const int cx = cell_index(M.x), cy = cell_index(M.y);
        const i64 fall = cell_fall_step(gi, cx, cy);
        if (fall >= 0 && fall - o.t < 25) {
            if (safe_goal(gi, M.x, M.y, o.t, gx, gy)) return true;
        }
        int tgt = -1; i64 best = 0;
        for (int i = 1; i < g.n; ++i) {
            const int k = (gi.seat + i) % g.n;
            const Player& P = g.p[k];
            if (!P.alive || P.unknown || mate(gi, k)) continue;
            const i64 dd = d2(P.x, P.y, M.x, M.y);
            if (tgt < 0 || dd < best) { tgt = k; best = dd; }
        }
        if (tgt >= 0) { gx = g.p[tgt].x; gy = g.p[tgt].y; return true; }
        gx = gi.M().width() / 2; gy = gi.M().height() / 2;
        return true;
    }

    bool melee_clear(const GameInfo& gi, const Game& g, int me, i64 ax, i64 ay) const {
        const Config& c = gi.cfg;
        const Player& M = g.p[me];
        i64 ux, uy;
        if (!scale_to(ax, ay, UNIT, ux, uy)) return true;
        for (int k = 0; k < g.n; ++k) {
            if (k == me || !mate(gi, k) || !g.p[k].alive) continue;
            const Player& T = g.p[k];
            const i64 dx = T.x - M.x, dy = T.y - M.y, dl = ilen(dx, dy);
            if (dl > c.slash_reach) continue;
            if (dl <= c.body_r) return false;
            if ((dx * ux + dy * uy) * 1000 >= c.slash_cos * dl * UNIT) return false;
        }
        return true;
    }
    bool throw_clear(const GameInfo& gi, const Game& g, int me, i64 ax, i64 ay) const {
        const Player& M = g.p[me];
        i64 ux, uy;
        if (!scale_to(ax, ay, UNIT, ux, uy)) return true;
        const i64 R = gi.cfg.body_r + gi.cfg.dart_r + 100;
        for (int k = 0; k < g.n; ++k) {
            if (k == me || !mate(gi, k) || !g.p[k].alive) continue;
            const Player& T = g.p[k];
            const i64 wx = T.x - M.x, wy = T.y - M.y;
            const i64 proj = wx * ux + wy * uy;
            if (proj < 0 || proj > gi.cfg.range) continue;
            const i64 perp = iabs(wx * uy - wy * ux);
            if (perp < R * UNIT) return false;
        }
        return true;
    }

    void record(const Obs& o, const Action& a) {
        ho[hcnt % HIST] = o; ha[hcnt % HIST] = a; ++hcnt;
    }
};

}  // namespace

DART_MAIN(Bot)
