// player.cpp —— 混元回旋镖 / hunyuan-boomerang
//
// 从零写的原创实现：不复用、不改写任何官方选手的源码。
//
// 整体架构（四层，各自解决一件事）：
//   1) 时间对齐：观察里别人是 DELAY 步以前的。把观察塞进裁判同款引擎 dart::Game，
//      让引擎自己连走 DELAY 步（自己冻结、镖移除），得到"别人现在大概在哪、镖飞到哪了"。
//      这一步连"观察时他正在蓄势、其实镖早已出手"这类状态迁移都由引擎算，不用手写物理。
//   2) 盲区假设：对手可能在我看不见的这几步里已经朝我扔了镖。取 DELAY-1 步前他的位置
//      和我当时的位置，按去程速度表造一把"幻影镖"（真实写进 Game 的 Dart 里），
//      于是躲避评估用的是和裁判逐位一致的物理。另设一个"对手本步同时出手"的假设。
//      两套世界一起打分，避免只对一种猜测过拟合。
//   3) 开环候选推演：16 向跑 / 半速微调 / 冲刺 / 扔 / 砍 / 踢 / 召回 / 捡镖 / 撤离塌陷区，
//      每个候选在两套世界里各推 12 步，按存活、击杀、飞镖距离、硬直、塌陷倒计时、
//      持镖与落点、与敌人的距离逐项累计打分。敌人按"靠近我"的简化策略走。
//   4) 解析式拦截瞄准：扔镖不看单一点，而是把每个对手列成 6 种走法假设（续走/停/
//      靠近/左绕/右绕/远离），把回旋镖去程的减速位移表列出来，枚举朝向，算"这一镖
//      能覆盖多少种走法"，再挑覆盖率最高的两个朝向进候选池。墙用引擎的 wall_between 判。
//
// 全部只读 info()/观察，不读写文件、不联网、不存跨局状态。收手用固定工作量 + 墙上时钟保险丝。

#include "dart.hpp"
#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>
using namespace dart;

namespace {
constexpr int HZ = 12;                 // 每个候选往后推多少步
constexpr int MAXSNAP = 9;             // 外推快照数（DELAY 上限取 8）
constexpr long long WALL_US = 6500;    // 墙上时钟保险丝（微秒），远低于 10 毫秒预算
const int DIR16[16][2] = {
    {1000, 0},   {924, 383},  {707, 707},  {383, 924},
    {0, 1000},   {-383, 924}, {-707, 707}, {-924, 383},
    {-1000, 0},  {-924, -383},{-707, -707},{-383, -924},
    {0, -1000},  {383, -924}, {707, -707}, {924, -383}
};
inline i64 len2(i64 ax, i64 ay, i64 bx, i64 by) {
    i64 dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy;
}
inline void unitv(i64 x, i64 y, i64& ox, i64& oy) {
    if (!scale_to(x, y, 1000, ox, oy)) { ox = 1000; oy = 0; }
}
}

struct Bot {
    // ---- 开局固定信息 ----
    int nx = 0, ny = 0;
    std::vector<i64> fall;             // 每格第几步塌；-1 = 永不
    i64 safeX = 0, safeY = 0;         // 最后落脚区中心
    // ---- 局内轨迹（观察里"自己"是现在的，可直接存） ----
    i64 hx[900] = {}, hy[900] = {}, hvx[900] = {}, hvy[900] = {};
    i64 seenX[MAXP] = {}, seenY[MAXP] = {};
    bool seen[MAXP] = {};
    // 方向保持：被盯上时沿一个方向连走若干步，避免"每步换向、五步只挪半米"
    i64 holdX = 0, holdY = 0;
    int holdLeft = 0;
    // ---- 计时 ----
    std::chrono::steady_clock::time_point clk;

    struct Aim { i64 ux = 0, uy = 0; double cov = 0; i64 dist = 0; double val = 0; };
    struct Cand { Action a, cont; double bonus = 0; };

    void init(const GameInfo& gi) {
        const Map& M = gi.M();
        nx = M.nx; ny = M.ny;
        fall.assign(size_t(nx) * ny, -1);
        i64 last = -1;
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                i64 f = cell_fall_step(gi, x, y);
                fall[size_t(y) * nx + x] = f;
                if (f > last) last = f;
            }
        i64 sx = 0, sy = 0, c = 0;
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                int ct = M.at(x, y);
                if (ct != C_FLOOR && ct != C_GRASS) continue;
                i64 f = fall[size_t(y) * nx + x];
                if (f < 0 || (last >= 0 && f == last)) {
                    sx += cell_center(x); sy += cell_center(y); ++c;
                }
            }
        if (c > 0) { safeX = sx / c; safeY = sy / c; }
        else { safeX = M.width() / 2; safeY = M.height() / 2; }
    }

    i64 fallAt(i64 x, i64 y) const {
        int cx = cell_index(x), cy = cell_index(y);
        if (cx < 0 || cy < 0 || cx >= nx || cy >= ny) return -2;   // 场外
        return fall[size_t(cy) * nx + cx];
    }
    bool timeUp() const {
        auto el = std::chrono::steady_clock::now() - clk;
        return std::chrono::duration_cast<std::chrono::microseconds>(el).count() > WALL_US;
    }

    // 解析式拦截：返回覆盖率最高的几个投掷朝向
    std::vector<Aim> throwAims(const Game& world, const Obs& o, const Config& cf,
                               const Map& live, int me, int n) {
        std::vector<Aim> res;
        const i64 mx = o.me().x, my = o.me().y;
        // 回旋镖去程：第 s 步末累计飞了 fl[s]（出镖那一步不动，之后 900/850/800...）
        i64 fl[16] = {};
        for (int s = 2; s <= 14; ++s)
            fl[s] = imin(cf.range, fl[s - 1] + imax(cf.dart_vmin, cf.dart_v - (s - 2) * cf.dart_dec));

        // 先把每个对手的 6 种走法假设与候选朝向收集好
        struct Hyp { i64 vx[6], vy[6]; i64 x, y, d; };
        std::vector<Hyp> hs;
        std::vector<std::pair<i64, i64>> dirs;
        auto addDir = [&](i64 ux, i64 uy) {
            for (auto& q : dirs)
                if (q.first * ux + q.second * uy >= 995000) return;   // 夹角 < 6 度视为同向
            dirs.emplace_back(ux, uy);
        };
        for (int k = 0; k < n; ++k) {
            if (k == me || !world.p[k].alive || world.p[k].unknown) continue;
            const Player& E = world.p[k];
            i64 bx = mx - E.x, by = my - E.y;         // 敌人 -> 我
            i64 L = ilen(bx, by);
            if (L > cf.range + 1500) continue;
            Hyp h; h.x = E.x; h.y = E.y; h.d = L;
            i64 a1, a2, b1, b2, c1, c2, d1, d2, e1, e2;
            scale_to(E.vx, E.vy, cf.run_v, a1, a2);   // 续走
            scale_to(-bx, -by, cf.run_v, b1, b2);     // 朝我来
            scale_to(by, -bx, cf.run_v, c1, c2);      // 侧向一
            scale_to(-by, bx, cf.run_v, d1, d2);      // 侧向二
            e1 = -b1; e2 = -b2;                       // 远离
            h.vx[0] = a1; h.vy[0] = a2;
            h.vx[1] = 0;  h.vy[1] = 0;
            h.vx[2] = b1; h.vy[2] = b2;
            h.vx[3] = c1; h.vy[3] = c2;
            h.vx[4] = d1; h.vy[4] = d2;
            h.vx[5] = e1; h.vy[5] = e2;
            hs.push_back(h);
            for (int q = 0; q < 6; ++q)
                for (int s = 3; s <= 11; ++s) {
                    i64 px = E.x + h.vx[q] * s, py = E.y + h.vy[q] * s;
                    i64 ax = px - mx, ay = py - my;
                    i64 dd = ilen(ax, ay);
                    if (dd < 400 || dd > cf.range + 500) continue;
                    if (iabs(fl[s] - dd) > 1500) continue;   // 这一刻镖恰好飞到附近才有意义
                    i64 ux, uy;
                    if (scale_to(ax, ay, 1000, ux, uy)) addDir(ux, uy);
                }
            i64 ux, uy;
            if (scale_to(-bx, -by, 1000, ux, uy)) addDir(ux, uy);   // 直射
        }
        if (hs.empty() || dirs.empty()) return res;

        for (auto& dr : dirs) {
            i64 dxp[15] = {}, dyp[15] = {};
            int lastS = 1;
            for (int s = 1; s <= 13; ++s) {
                i64 X = mx + rdiv(dr.first * fl[s], 1000);
                i64 Y = my + rdiv(dr.second * fl[s], 1000);
                if (wall_between(live, mx, my, X, Y)) break;         // 撞墙后的路径不再有效
                dxp[s] = X; dyp[s] = Y; lastS = s;
            }
            if (lastS < 3) continue;
            double bestCov = 0, sumCov = 0;
            i64 bestDist = 0;
            int coveredEnemies = 0;
            for (auto& h : hs) {
                int hits = 0;
                for (int q = 0; q < 6; ++q) {
                    bool hit = false;
                    for (int s = 2; s <= lastS; ++s) {
                        i64 ex = h.x + h.vx[q] * s, ey = h.y + h.vy[q] * s;
                        if (ilen(dxp[s] - ex, dyp[s] - ey) <= 900) { hit = true; break; }
                    }
                    if (hit) ++hits;
                }
                double c = double(hits) / 6.0;
                sumCov += c;
                if (c >= 0.5) ++coveredEnemies;
                if (c > bestCov) { bestCov = c; bestDist = h.d; }
            }
            if (bestCov < 0.34 || bestDist > cf.range + 800) continue;
            Aim A;
            A.ux = dr.first; A.uy = dr.second; A.cov = bestCov; A.dist = bestDist;
            A.val = bestCov * 1000.0 + coveredEnemies * 130.0 + sumCov * 40.0
                    - double(bestDist) * 0.012;
            res.push_back(A);
        }
        std::sort(res.begin(), res.end(), [](const Aim& a, const Aim& b) { return a.val > b.val; });
        if (res.size() > 3) res.resize(3);
        return res;
    }

    Action decide(const Obs& o) {
        clk = std::chrono::steady_clock::now();
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        const int me = o.seat, n = o.n;
        const i64 t = o.t;
        Action none;
        if (!o.me().alive || o.me().st == S_STUN || o.me().st == S_DEAD) return none;
        if (t >= 0 && t < 900) {
            hx[t] = o.me().x; hy[t] = o.me().y;
            hvx[t] = o.me().vx; hvy[t] = o.me().vy;
        }
        for (int k = 0; k < n; ++k)
            if (k != me && o.p[k].vis && o.p[k].alive) {
                seenX[k] = o.p[k].x; seenY[k] = o.p[k].y; seen[k] = true;
            }

        // ---------- 1. 用引擎把别人的 4 步前状态推到现在 ----------
        const int D = int(iclamp(cf.delay, 0, MAXSNAP - 1));
        Game ext(gi, o);
        ext.t = imax(0, t - D);
        ext.over = false;
        ext.p[me].unknown = 1;                 // 自己冻结；外推只取别人
        ext.d[me].st = D_GONE; ext.d[me].x = ext.d[me].y = 0;
        Player sp[MAXSNAP][MAXP];
        for (int s = 0; s <= D; ++s) {
            for (int k = 0; k < n; ++k) sp[s][k] = ext.p[k];
            if (s == D) break;
            Action acts[MAXP];
            for (int k = 0; k < n; ++k) {
                if (k == me || !ext.p[k].alive || ext.p[k].unknown) continue;
                i64 ux = 0, uy = 0;
                if (ilen(ext.p[k].vx, ext.p[k].vy) > 30)
                    scale_to(ext.p[k].vx, ext.p[k].vy, 1000, ux, uy);
                acts[k].mx = ux; acts[k].my = uy;
            }
            ext.over = false;
            ext.step(acts);
        }

        // ---------- 2. 组装"当前世界" ----------
        Game world(gi, o);
        world.t = t; world.over = false;
        world.p[me] = o.p[me];
        world.p[me].unknown = 0;
        world.p[me].px = world.p[me].x; world.p[me].py = world.p[me].y;
        world.d[me] = o.d[me];
        if (world.d[me].st == D_HAND) { world.d[me].x = world.p[me].x; world.d[me].y = world.p[me].y; }
        for (int k = 0; k < n; ++k) {
            if (k == me) continue;
            Player P = sp[D][k];
            P.team = gi.team[k];
            if (o.p[k].alive && !o.p[k].vis) {          // 草丛里的隐身者：放回最后已知位置
                P.unknown = 0;
                if (seen[k]) { P.x = seenX[k]; P.y = seenY[k]; }
                else { P.x = gi.spx[k]; P.y = gi.spy[k]; }
                P.vx = P.vy = 0; P.st = S_FREE; P.timer = 0; P.ax = P.ay = 0;
            }
            P.px = P.x; P.py = P.y;
            world.p[k] = P;
            world.d[k] = ext.d[k];
            if (world.d[k].st == D_HAND) { world.d[k].x = P.x; world.d[k].y = P.y; }
        }

        // ---------- 3. 盲区假设：把"看不见的这几步里对手可能已经出手"列成解析轨迹 ----------
        // 一次出手只对应一个 j，所以 j=1..D 是互斥假设，每条轨迹的权重取 1/D。
        // 这样不用为每种出手时刻各复制一份 Game，也能让躲闪覆盖整段盲区。
        struct Phantom { i64 px[HZ], py[HZ]; double w; };
        std::vector<Phantom> phs;
        const i64 mvx = o.me().vx, mvy = o.me().vy;
        for (int k = 0; k < n; ++k) {
            if (k == me || !world.p[k].alive || world.p[k].unknown) continue;
            if (world.d[k].st != D_HAND) continue;      // 看起来还拿着镖 = 可能在盲区里扔了
            for (int j = 1; j <= D; ++j) {
                const Player& E = sp[D - j][k];
                if (!E.alive || E.unknown) continue;
                const int tt = int(t) - j;
                const i64 tx = (tt >= 0 && tt < 900) ? hx[tt] : o.me().x;
                const i64 ty = (tt >= 0 && tt < 900) ? hy[tt] : o.me().y;
                const i64 tvx = (tt >= 0 && tt < 900) ? hvx[tt] : mvx;
                const i64 tvy = (tt >= 0 && tt < 900) ? hvy[tt] : mvy;
                // 对手可能瞄我当时的位置，也可能按我的速度打提前量 —— 两种都算
                for (int av = 0; av < 2; ++av) {
                    i64 ax = tx - E.x + (av ? tvx * 3 : 0);
                    i64 ay = ty - E.y + (av ? tvy * 3 : 0);
                    i64 ux, uy;
                    if (!scale_to(ax, ay, 1000, ux, uy)) continue;
                    const int ns = imax(0, j - 1);
                    i64 travel = 0;
                    for (int q = 0; q < ns; ++q)
                        travel += imax(cf.dart_vmin, cf.dart_v - q * cf.dart_dec);
                    travel = imin(travel, cf.range);
                    i64 left = cf.range - travel;
                    i64 xx = E.x + rdiv(ux * travel, 1000);
                    i64 yy = E.y + rdiv(uy * travel, 1000);
                    Phantom P; P.w = 1.0 / double(2 * imax(1, D));
                    for (int h = 0; h < HZ; ++h) {
                        if (h > 0) {
                            i64 spd = imax(cf.dart_vmin, cf.dart_v - (ns + h - 1) * cf.dart_dec);
                            i64 m2 = imin(spd, left);
                            xx += rdiv(ux * m2, 1000); yy += rdiv(uy * m2, 1000);
                            left -= m2;
                        }
                        P.px[h] = xx; P.py[h] = yy;
                    }
                    phs.push_back(P);
                }
            }
        }

        // 召回引导一旦开始就别再反悔：站着不动固然有代价，但"召回→取消→召回"最糟。
        // 只有确实有镖逼近、有人贴脸、或脚下马上要塌时才中断去躲。
        if (o.me().st == S_RECALL) {
            bool danger = false;
            for (int k = 0; k < n; ++k) {
                if (k != me && dart_flying(world.d[k].st) &&
                    ilen(world.d[k].x - o.me().x, world.d[k].y - o.me().y) < 2600) danger = true;
                if (k != me && world.p[k].alive && !world.p[k].unknown &&
                    ilen(world.p[k].x - o.me().x, world.p[k].y - o.me().y) < 1400) danger = true;
            }
            i64 f0 = fallAt(o.me().x, o.me().y);
            if (f0 >= 0 && f0 - t < 22) danger = true;
            if (!danger) { Action a; a.act = A_RECALL; return a; }
        }

        // ---------- 4. 候选动作 ----------
        const bool freeState = (o.me().st == S_FREE);
        const bool holdDart = (o.d[me].st == D_HAND);
        std::vector<Cand> cs;
        cs.reserve(64);
        auto mk = [&](const Action& a, i64 cx, i64 cy, double bonus) {
            Cand c; c.a = a; c.bonus = bonus;
            c.cont.mx = cx; c.cont.my = cy; c.cont.act = A_NONE;
            cs.push_back(c);
        };
        auto addMove = [&](i64 mx, i64 my, double bonus = 0.0) {
            Action a; a.mx = mx; a.my = my;
            mk(a, mx, my, bonus);
        };
        for (int i = 0; i < 16; ++i) addMove(DIR16[i][0], DIR16[i][1]);
        addMove(0, 0);
        for (int i = 0; i < 16; i += 2) addMove(DIR16[i][0] / 2, DIR16[i][1] / 2);

        // 去捡自己的地上镖 / 往最后的落脚区撤
        bool dartReachable = true;
        i64 dartDist = 0;
        if (o.d[me].st == D_GROUND || o.d[me].st == D_RISE) {
            i64 wx = 0, wy = 0;
            dartDist = ilen(o.d[me].x - o.me().x, o.d[me].y - o.me().y);
            dartReachable = path_next(gi, o.me().x, o.me().y, o.d[me].x, o.d[me].y, t, wx, wy);
            if (dartReachable) addMove(wx - o.me().x, wy - o.me().y, 260.0);
            else addMove(o.d[me].x - o.me().x, o.d[me].y - o.me().y, 120.0);
        }
        if (t > cf.col_start - 60) {
            i64 wx = 0, wy = 0;
            if (path_next(gi, o.me().x, o.me().y, safeX, safeY, t, wx, wy))
                addMove(wx - o.me().x, wy - o.me().y, 140.0);
        }

        // 扔镖候选：按覆盖率瞄准 + 近距离直射补枪
        if (freeState && holdDart) {
            Map live = live_map(cf, gi.M(), t);
            std::vector<Aim> aims = throwAims(world, o, cf, live, me, n);
            for (const Aim& A : aims) {
                Action a;
                a.act = A_THROW; a.ax = A.ux; a.ay = A.uy;
                a.mx = -A.uy; a.my = A.ux;            // 蓄势期横向挪，避开回敬
                mk(a, a.mx, a.my, 300.0 + 1500.0 * A.cov + (A.dist < 3500 ? 320.0 : 0.0));
            }
            // 直射：对看得见、中间没墙、距离够近的对手补一个几乎必中的候选
            int got = 0;
            for (int k = 0; k < n && got < 2; ++k) {
                if (k == me || !world.p[k].alive || world.p[k].unknown) continue;
                i64 dx = world.p[k].x - o.me().x, dy = world.p[k].y - o.me().y;
                i64 L = ilen(dx, dy);
                if (L < 500 || L > 4800) continue;
                if (wall_between(live, o.me().x, o.me().y, world.p[k].x, world.p[k].y)) continue;
                Action a; a.act = A_THROW; a.ax = dx; a.ay = dy;
                a.mx = -dy; a.my = dx;
                mk(a, a.mx, a.my, 420.0 + double(imax(0, 4800 - L)) * 0.30);
                ++got;
            }
        }

        // 挡镖 / 近战：先探一步拿到"下一步开始时"的镖位置（挥砍判定就在这一刻）
        if (freeState) {
            Game probe = world;
            Action acts[MAXP];
            probe.over = false;
            probe.step(acts);
            for (int k = 0; k < n; ++k) {
                if (k == me || !dart_flying(probe.d[k].st)) continue;
                i64 dd = ilen(probe.d[k].x - o.me().x, probe.d[k].y - o.me().y);
                if (!holdDart || dd > cf.slash_reach + 250) continue;
                Action a;
                a.act = A_MELEE;
                a.ax = probe.d[k].x - o.me().x; a.ay = probe.d[k].y - o.me().y;
                mk(a, 0, 0, (dd > 680 && dd < 1600) ? 950.0 : 180.0);
            }
            int bk = -1; i64 bd = 100000000;
            for (int k = 0; k < n; ++k) {
                if (k == me || !world.p[k].alive || world.p[k].unknown) continue;
                i64 d = ilen(world.p[k].x - o.me().x, world.p[k].y - o.me().y);
                if (d < bd) { bd = d; bk = k; }
            }
            if (bk >= 0 && bd < 2100) {
                Action a; a.act = A_MELEE;
                a.ax = world.p[bk].x + world.p[bk].vx * 2 - o.me().x;
                a.ay = world.p[bk].y + world.p[bk].vy * 2 - o.me().y;
                mk(a, 0, 0, holdDart ? 560.0 : 380.0);
            }
        }

        // 冲刺 / 召回
        if (freeState && o.me().cd == 0) {
            const i64 landT = t + cf.dash_pre + cf.dash_t;
            for (int i = 0; i < 16; i += 2) {
                // 冲刺落点：移动段最后一步会正常判坑，先把明显会掉下去/撞墙的方向剔掉
                i64 lx = o.me().x + rdiv(i64(DIR16[i][0]) * cf.dash_v * cf.dash_t, 1000);
                i64 ly = o.me().y + rdiv(i64(DIR16[i][1]) * cf.dash_v * cf.dash_t, 1000);
                int lc = cell_of_point(cf, gi.M(), lx, ly, landT);
                if (lc == C_PIT || lc == C_WALL) continue;
                Action a; a.act = A_DASH; a.ax = DIR16[i][0]; a.ay = DIR16[i][1];
                mk(a, 0, 0, 0.0);
            }
        }
        if (freeState && o.d[me].st == D_GROUND) {
            bool threat = false;
            for (int k = 0; k < n; ++k)
                if (k != me && dart_flying(world.d[k].st) &&
                    ilen(world.d[k].x - o.me().x, world.d[k].y - o.me().y) < 3000) threat = true;
            // 能走过去就走（边走边能躲）；只有走不到或实在太远、又没镖逼近时才站定召回
            if ((!dartReachable || dartDist > 9000) && !threat) {
                Action a; a.act = A_RECALL;
                mk(a, 0, 0, 420.0);
            }
        }
        if (o.me().st == S_RECALL) {          // 继续引导（给移动会中断）
            Action a; a.act = A_RECALL;
            mk(a, 0, 0, 0.0);
        }

        // ---------- 5. 两套世界 × 开环推演打分 ----------
        auto runWorld = [&](const Game& w0, const Cand& c, int throwMode) -> double {
            Game w = w0;
            bool a0[MAXP];
            for (int k = 0; k < n; ++k) a0[k] = w.p[k].alive;
            double s = 0.0;
            Action acts[MAXP];
            for (int h = 0; h < HZ; ++h) {
                for (int k = 0; k < n; ++k) acts[k] = Action();
                for (int k = 0; k < n; ++k) {
                    if (k == me || !w.p[k].alive || w.p[k].unknown) continue;
                    const Player& E = w.p[k];
                    i64 dx = w.p[me].x - E.x, dy = w.p[me].y - E.y;
                    i64 L = ilen(dx, dy);
                    i64 ux = 0, uy = 0;
                    if (L > 0 && L < 7000) scale_to(dx, dy, 1000, ux, uy);
                    else scale_to(E.vx, E.vy, 1000, ux, uy);
                    acts[k].mx = ux; acts[k].my = uy;
                    if (throwMode && h == 0 && w.d[k].st == D_HAND &&
                        E.st == S_FREE && L < 6000) {
                        acts[k].act = A_THROW;
                        acts[k].ax = dx + (throwMode == 2 ? mvx * 3 : 0);
                        acts[k].ay = dy + (throwMode == 2 ? mvy * 3 : 0);
                        acts[k].mx = 0; acts[k].my = 0;
                    }
                }
                acts[me] = (h == 0) ? c.a : c.cont;
                w.over = false;
                w.step(acts);
                const Player& M = w.p[me];
                const double dec = 1.0 / (1.0 + 0.30 * h);
                if (!M.alive) { s -= 26000.0 + 6000.0 * (HZ - h); break; }
                for (int k = 0; k < n; ++k)
                    if (k != me && a0[k] && !w.p[k].alive) {
                        s += 4200.0;
                        if (w.p[k].killer == me) s += 2600.0;
                    }
                for (int k = 0; k < n; ++k)
                    if (k != me && dart_flying(w.d[k].st)) {
                        i64 dd = ilen(w.d[k].x - M.x, w.d[k].y - M.y);
                        if (dd < 2400) s -= dec * (2400 - dd) * 1.1;
                    }
                // 盲区里的"看不见的镖"：按条扣分，覆盖 t-1..t-D 所有出手时刻
                for (const Phantom& P : phs) {
                    i64 dd = ilen(P.px[h] - M.x, P.py[h] - M.y);
                    if (dd < 1050) {
                        s -= P.w * dec * double(1050 - dd) * 3.0;
                        if (dd < 780) s -= P.w * dec * 9000.0;
                    }
                }
                if (M.st == S_STUN) s -= 1200.0 * dec;
                if (M.st == S_DASH || M.st == S_RECALL) s -= 110.0 * dec;   // 长时间不能变招
                if (M.st != S_FREE) s -= 30.0 * dec;
                if (w.d[me].st == D_HAND) s += 45.0 * dec;
                else if (w.d[me].st == D_GROUND)
                    s -= dec * double(ilen(w.d[me].x - M.x, w.d[me].y - M.y)) * 0.010;
                i64 f = fallAt(M.x, M.y);
                if (f == -2) s -= 5000.0 * dec;
                else if (f >= 0) {
                    i64 left = f - (t + h + 1);
                    if (left < 50) s -= dec * double(50 - left) * 10.0;
                }
                for (int k = 0; k < n; ++k)
                    if (k != me && w.p[k].alive && !w.p[k].unknown) {
                        i64 de = ilen(w.p[k].x - M.x, w.p[k].y - M.y);
                        if (de < 1200) s -= dec * double(1200 - de) * 0.55;
                    }
            }
            const Player& M = w.p[me];
            if (M.alive && t > cf.col_start - 90)
                s -= double(ilen(M.x - safeX, M.y - safeY)) * 0.006;
            return s;
        };

        double best = -1e30;
        Action bestA;
        int done = 0;
        for (size_t i = 0; i < cs.size(); ++i) {
            if (done >= 4 && timeUp()) break;
            double s0 = runWorld(world, cs[i], 1);   // 对手此刻直射
            double s1 = runWorld(world, cs[i], 2);   // 对手此刻打提前量
            double s = 0.45 * s0 + 0.45 * s1 + 0.10 * std::min(s0, s1) + cs[i].bonus;
            s -= 0.001 * double(i);
            if (s > best) { best = s; bestA = cs[i].a; }
            ++done;
        }
        return bestA;
    }
};

DART_MAIN(Bot)
