// player.cpp —— 飞镖乱斗 选手程序（opus）
// 思路：
//  1. 延迟补偿：记下自己每一步的状态和动作，拼出 DELAY 步以前的完整局面（别人用观察、自己用历史），
//     再用引擎把它推到现在（别人按当时速度继续走），得到"现在"的估计局面。
//  2. 候选方案：若干移动方向 × 两种持续时间、冲刺、扔镖（多种瞄准 × 移动方向）、挥砍/踢、冲刺接近战、召回、挡镖。
//     每个方案用引擎（dart::Game）推演 H 步，看会不会死、最后站在哪。
//  3. 风险：对每个手里有镖的对手，假设他在我看不见的这几步 / 接下来几步朝我（他看到的我、或按速度提前量）扔镖，
//     算每个方案被这些"假想镖"打中的加权次数，作为风险扣分。
//  4. 进攻：对每个对手生成若干行为情景（继续走、停、8 个方向、看到我出手后横向躲），
//     估计扔镖 / 挥砍 / 踢 / 冲刺斩的命中概率，乘以击杀价值加进方案分数。
//  5. 位置评估：塌陷、坑边、安全区、与对手的距离（有镖 / 没镖不同）、捡镖。
#include "dart.hpp"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
using namespace dart;

namespace {

const int HIST = 1100;
const int H = 16;          // 方案推演步数
const int OPP_STEPS = 30;  // 对手情景轨迹长度

i64 DX16[16], DY16[16];
i64 CUM[64];
int CUMN = 0;

struct LT {
    i64 x[OPP_STEPS + 2], y[OPP_STEPS + 2];
    bool dead = false;
};

struct Scen {
    LT tr;
    double w;
};

struct Plan {
    int type = 0;  // 0 move, 1 dash, 2 throw, 3 melee, 4 recall, 5 dashmelee, 6 stand
    i64 dx = 0, dy = 0;
    int K = 0;
    i64 ax = 0, ay = 0;
    int meleeAt = -1;
    double bonus = 0;
    double score = 0;
};

struct HDart {
    i64 s0;  // 出镖那一步（这一步末镖在 pos[0]）
    int len;
    i64 x[24], y[24];
    double w;
};

}  // namespace

struct Bot {
    Config cf;
    int me = 0, N = 2;
    Player hp[HIST];
    Dart hd[HIST];
    Action ha[HIST];
    bool hv[HIST];
    i64 lsx[MAXP], lsy[MAXP], lst[MAXP];
    double armedSteps[MAXP] = {0, 0, 0, 0, 0, 0, 0, 0}, nThrows[MAXP] = {0, 0, 0, 0, 0, 0, 0, 0};
    i64 prevSt[MAXP] = {0, 0, 0, 0, 0, 0, 0, 0};
    double thrScale[MAXP] = {1, 1, 1, 1, 1, 1, 1, 1};
    std::vector<int> safeField;
    int nx = 0, ny = 0;
    i64 cpu0 = 0;
    u64 rng = 0x9E3779B97F4A7C15ULL;

    // 对手情景（每步重算）
    std::vector<Scen> scen[MAXP];
    i64 T = 0, t0 = 0;
    Obs ob;

    u64 nextRand() {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        return rng;
    }

    void init(const GameInfo& gi) {
        cf = gi.cfg;
        me = gi.seat;
        N = gi.n;
        nx = gi.M().nx;
        ny = gi.M().ny;
        rng ^= u64(me + 1) * 0x2545F4914F6CDD1DULL;
        for (int i = 0; i < 16; ++i) {
            const double a = i * 3.14159265358979 / 8.0;
            DX16[i] = llround(std::cos(a) * 1000);
            DY16[i] = llround(std::sin(a) * 1000);
        }
        {
            i64 c = 0, left = cf.range;
            CUM[0] = 0;
            int k = 0;
            while (left > 0 && k < 60) {
                const i64 sp = std::max(cf.dart_vmin, cf.dart_v - k * cf.dart_dec);
                const i64 m = std::min(sp, left);
                c += m;
                left -= m;
                ++k;
                CUM[k] = c;
            }
            CUMN = k;
        }
        for (int i = 0; i < MAXP; ++i) {
            lsx[i] = gi.spx[i];
            lsy[i] = gi.spy[i];
            lst[i] = 0;
        }
        std::memset(hv, 0, sizeof hv);
        // 安全区：最后塌（或永不塌）的格子；BFS 距离（格）
        const Map& M = gi.M();
        int maxw = -1;
        bool never = false;
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                const int c = M.at(x, y);
                if (c == C_PIT || c == C_WALL) continue;
                const int w = M.wave(x, y);
                if (w < 0) never = true;
                else maxw = std::max(maxw, w);
            }
        safeField.assign(size_t(nx) * ny, 999);
        std::vector<int> q;
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                const int c = M.at(x, y);
                if (c == C_PIT || c == C_WALL) continue;
                const int w = M.wave(x, y);
                if ((never && w < 0) || (!never && w == maxw)) {
                    safeField[size_t(y) * nx + x] = 0;
                    q.push_back(y * nx + x);
                }
            }
        for (size_t h = 0; h < q.size(); ++h) {
            const int id = q[h], x = id % nx, y = id / nx;
            const int dd = safeField[size_t(id)];
            static const int ddx[4] = {1, -1, 0, 0}, ddy[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; ++k) {
                const int a = x + ddx[k], b = y + ddy[k];
                if (!M.inside(a, b)) continue;
                const int c = M.at(a, b);
                if (c == C_PIT || c == C_WALL) continue;
                if (safeField[size_t(b) * nx + a] <= dd + 1) continue;
                safeField[size_t(b) * nx + a] = dd + 1;
                q.push_back(b * nx + a);
            }
        }
    }

    int safeAt(i64 x, i64 y) const {
        const int cx = cell_index(x), cy = cell_index(y);
        if (cx < 0 || cy < 0 || cx >= nx || cy >= ny) return 60;
        const int v = safeField[size_t(cy) * nx + cx];
        return v > 60 ? 60 : v;
    }

    bool cpuOver(i64 lim100ns) const {
        const i64 c = thread_cpu_100ns();
        if (cpu0 < 0 || c < 0) return false;
        return c - cpu0 >= lim100ns;
    }

    // —— 对手在推演里的动作：按当时速度方向继续走
    Action predictAct(const Game& g, int k) const {
        Action a;
        const Player& P = g.p[k];
        if (!P.alive || P.unknown) return a;
        if (P.st == S_FREE || P.st == S_THROW || P.st == S_THROWN) {
            if (ilen(P.vx, P.vy) >= 30) scale_to(P.vx, P.vy, 1000, a.mx, a.my);
        }
        return a;
    }

    Game reconstruct(const Obs& o) {
        const GameInfo& gi = info();
        Game g(gi, o);
        if (T > t0 && hv[t0]) {
            g.t = t0;
            Player P = hp[t0];
            P.team = gi.team[me];
            P.px = P.x;
            P.py = P.y;
            P.unknown = 0;
            g.p[me] = P;
            g.d[me] = hd[t0];
            for (i64 s = t0; s < T; ++s) {
                Action acts[MAXP];
                for (int k = 0; k < N; ++k) acts[k] = (k == me) ? ha[s % HIST] : predictAct(g, k);
                g.step(acts);
                g.over = false;
            }
            for (int k = 0; k < N; ++k) {
                if (k == me) continue;
                if (o.p[k].alive && !g.p[k].alive) {  // 推演里死了，现实不一定：留着
                    g.p[k].alive = 1;
                    g.p[k].st = S_FREE;
                    g.p[k].timer = 0;
                }
            }
            Player Q = o.p[me];
            Q.team = gi.team[me];
            Q.px = Q.x;
            Q.py = Q.y;
            g.p[me] = Q;
            g.d[me] = o.d[me];
            g.t = T;
        }
        g.over = false;
        return g;
    }

    // —— 对手情景（轻量模拟，从观察的 t0 状态开始）
    // pol: 0 继续, 1 停, 2 固定方向；reactJ >= 0：从这一步起横向躲开 (ux,uy) 方向、过 (lx,ly) 的直线
    void genOpp(const Player& P, int steps, int pol, i64 dx, i64 dy, int reactJ, i64 lx, i64 ly, i64 ux, i64 uy, LT& out) const {
        const GameInfo& gi = info();
        const Map& M = gi.M();
        i64 x = P.x, y = P.y, vx = P.vx, vy = P.vy;
        i64 st = P.st, tm = P.timer;
        const i64 ax = P.ax, ay = P.ay;
        out.dead = false;
        for (int j = 0; j < steps; ++j) {
            out.x[j] = x;
            out.y[j] = y;
            int ctrl;
            if (st == S_FREE || st == S_RECALL) ctrl = 1;
            else if (st == S_THROW || st == S_THROWN) ctrl = 2;
            else if (st == S_DASH) ctrl = (tm > cf.dash_post && tm <= cf.dash_t + cf.dash_post) ? 3 : 0;
            else if (st == S_STUN) ctrl = 4;
            else ctrl = 0;
            if (ctrl == 3) {
                i64 mvx = rdiv(ax * cf.dash_v, UNIT), mvy = rdiv(ay * cf.dash_v, UNIT);
                move_body(M, cf.body_r, x, y, mvx, mvy);
                if (tm == cf.dash_post + 1) {
                    vx = rdiv(mvx * cf.run_v, imax(1, cf.dash_v));
                    vy = rdiv(mvy * cf.run_v, imax(1, cf.dash_v));
                } else {
                    vx = mvx;
                    vy = mvy;
                }
            } else if (ctrl == 4) {
                i64 mvx = vx, mvy = vy;
                move_body(M, cf.body_r, x, y, mvx, mvy);
                vx = rdiv(mvx * cf.kick_keep, 1000);
                vy = rdiv(mvy * cf.kick_keep, 1000);
            } else {
                i64 tvx = 0, tvy = 0;
                if (ctrl == 1 || ctrl == 2) {
                    const i64 sp = ctrl == 1 ? cf.run_v : rdiv(cf.run_v * cf.throw_move, 100);
                    i64 ddx = 0, ddy = 0;
                    if (reactJ >= 0 && j >= reactJ) {
                        const i64 px1 = -uy, py1 = ux;
                        const i64 side = (x - lx) * px1 + (y - ly) * py1 + 3 * (vx * px1 + vy * py1);
                        if (side >= 0) { ddx = px1; ddy = py1; }
                        else { ddx = -px1; ddy = -py1; }
                    } else if (pol == 0) {
                        if (ilen(P.vx, P.vy) >= 30) { ddx = P.vx; ddy = P.vy; }
                    } else if (pol == 2) {
                        ddx = dx;
                        ddy = dy;
                    }
                    if (ddx || ddy) scale_to(ddx, ddy, sp, tvx, tvy);
                }
                i64 dvx = tvx - vx, dvy = tvy - vy;
                cap_len(dvx, dvy, cf.acc);
                i64 nvx = vx + dvx, nvy = vy + dvy;
                i64 nx2 = x, ny2 = y;
                move_body(M, cf.body_r, nx2, ny2, nvx, nvy);
                if ((ctrl == 1 || ctrl == 2) && !standable(gi, nx2, ny2, t0 + j + 6)) {
                    vx = vy = 0;  // 不会自己走进坑
                } else {
                    x = nx2;
                    y = ny2;
                    vx = nvx;
                    vy = nvy;
                }
            }
            if (st != S_FREE && st != S_RECALL) {
                if (--tm <= 0) {
                    if (st == S_THROW) { st = S_THROWN; tm = cf.throw_post; }
                    else if (st == S_SLASH) { st = S_SLASHED; tm = cf.slash_post; }
                    else if (st == S_KICK) { st = S_KICKED; tm = cf.kick_post; }
                    else { st = S_FREE; tm = 0; }
                }
            }
        }
        out.x[steps] = x;
        out.y[steps] = y;
    }

    // 对手 k 的情景集（给定我出手后的反应方向基准：从 (lx,ly) 指向他的方向 (ux,uy)）
    void buildScen(int k, const Obs& o, i64 lx, i64 ly) {
        scen[k].clear();
        const Player& P = o.p[k];
        if (!P.alive || !P.vis) return;
        i64 ux = P.x - lx, uy = P.y - ly;
        if (!scale_to(ux, uy, UNIT, ux, uy)) { ux = 1000; uy = 0; }
        const int steps = OPP_STEPS;
        const int reactJ = int(T + cf.delay + 1 - t0);
        for (int r = 0; r < 2; ++r) {
            const int rj = r ? reactJ : -1;
            const double f = r ? 1.1 : 0.9;
            Scen s;
            genOpp(P, steps, 0, 0, 0, rj, lx, ly, ux, uy, s.tr);
            s.w = 2.0 * f;
            scen[k].push_back(s);
            genOpp(P, steps, 1, 0, 0, rj, lx, ly, ux, uy, s.tr);
            s.w = 0.6 * f;
            scen[k].push_back(s);
            for (int d = 0; d < 16; d += 2) {
                genOpp(P, steps, 2, DX16[d], DY16[d], rj, lx, ly, ux, uy, s.tr);
                s.w = 0.17 * f;
                scen[k].push_back(s);
            }
        }
    }

    // 镖的去程路线：pos[0] = 出镖点（第 rel 步末），pos[j] = 第 rel+j 步末；撞墙就停（之后不再有效）
    int dartPath(i64 rx, i64 ry, i64 ux, i64 uy, i64* px, i64* py, int maxn) const {
        const Map& M = info().M();
        px[0] = rx;
        py[0] = ry;
        int n = 1;
        // 整条去程线只查一次墙：第一次碰墙的点之后都无效
        const i64 ex = rx + rdiv(ux * CUM[CUMN], UNIT), ey = ry + rdiv(uy * CUM[CUMN], UNIT);
        i64 hx = 0, hy = 0;
        const bool hw = seg_wall(M, rx, ry, ex, ey, hx, hy);
        const i64 hd = hw ? ilen(hx - rx, hy - ry) : (1LL << 60);
        for (int j = 1; j <= CUMN && n < maxn; ++j) {
            if (CUM[j] >= hd) {
                px[n] = hx;
                py[n] = hy;
                ++n;
                return -n;  // 负数：撞墙结束
            }
            px[n] = rx + rdiv(ux * CUM[j], UNIT);
            py[n] = ry + rdiv(uy * CUM[j], UNIT);
            ++n;
        }
        // 最远点停留
        for (int h = 0; h < cf.dart_hang && n < maxn; ++h) {
            px[n] = px[n - 1];
            py[n] = py[n - 1];
            ++n;
        }
        return n;
    }

    // 我在第 T 步扔、出镖点 (rx,ry)，方向 (ux,uy)，对 k 的命中概率
    double throwProb(int k, i64 rx, i64 ry, i64 ux, i64 uy) const {
        if (scen[k].empty()) return 0;
        i64 px[24], py[24];
        int n = dartPath(rx, ry, ux, uy, px, py, 24);
        if (n < 0) n = -n;
        const i64 R = cf.body_r + cf.dart_r;
        double hit = 0, tot = 0;
        for (const Scen& s : scen[k]) {
            tot += s.w;
            // 出镖在第 T+1 步末；第 T+1+j 步镖从 pos[j-1] 到 pos[j]，人从 tr[T+1+j-t0] 到 tr[T+2+j-t0]
            bool h = false;
            for (int j = 1; j < n; ++j) {
                const i64 a = T + 1 + j - t0;
                if (a + 1 > OPP_STEPS) break;
                if (seg_near_origin(px[j - 1] - s.tr.x[a], py[j - 1] - s.tr.y[a], px[j] - s.tr.x[a + 1], py[j] - s.tr.y[a + 1], R)) {
                    h = true;
                    break;
                }
            }
            if (h) hit += s.w;
        }
        return tot > 0 ? hit / tot : 0;
    }

    // 近战命中概率：我在第 T + at 步给近战（判定在第 T+at+1 步末），判定时我在 (mx,my)，方向 (ux,uy)
    double meleeProb(int k, int at, i64 mx, i64 my, i64 ux, i64 uy, bool kick, double* pitFrac) const {
        if (scen[k].empty()) return 0;
        const GameInfo& gi = info();
        const i64 reach = kick ? cf.kick_reach : cf.slash_reach;
        const i64 cs = kick ? cf.kick_cos : cf.slash_cos;
        const i64 a = T + at + 2 - t0;
        if (a > OPP_STEPS) return 0;
        if (kick) {  // 被踢硬直中、以及硬直结束后的那一步免踢
            const Player& Q = ob.p[k];
            if (Q.st == S_STUN && T + at + 1 <= t0 + Q.timer + 1) return 0;
            if (Q.st == S_FREE && Q.timer > 0 && T + at + 1 <= t0 + Q.timer) return 0;
        }
        double hit = 0, tot = 0, pit = 0;
        for (const Scen& s : scen[k]) {
            tot += s.w;
            const i64 dx = s.tr.x[a] - mx, dy = s.tr.y[a] - my;
            const i64 dd = dx * dx + dy * dy;
            if (dd > reach * reach) continue;
            const i64 dl = isqrt(dd);
            if (dl > cf.body_r && (dx * ux + dy * uy) * 1000 < cs * dl * UNIT) continue;
            if (wall_between(gi.M(), mx, my, s.tr.x[a], s.tr.y[a])) continue;
            hit += s.w;
            if (kick && pitFrac) {
                i64 kx, ky;
                if (!scale_to(dx, dy, cf.kick_v, kx, ky)) scale_to(ux, uy, cf.kick_v, kx, ky);
                i64 x = s.tr.x[a], y = s.tr.y[a];
                for (int st = 0; st < cf.kick_stun; ++st) {
                    i64 vx = kx, vy = ky;
                    move_body(gi.M(), cf.body_r, x, y, vx, vy);
                    kx = rdiv(vx * cf.kick_keep, 1000);
                    ky = rdiv(vy * cf.kick_keep, 1000);
                }
                if (!standable(gi, x, y, T + at + 1 + cf.kick_stun)) pit += s.w;
            }
        }
        if (pitFrac) *pitFrac = tot > 0 ? pit / tot : 0;
        return tot > 0 ? hit / tot : 0;
    }

    // —— 假想来镖
    std::vector<HDart> hdarts;
    struct OppT {
        int k;
        LT tr;
        i64 freeAt;
        bool armed;
        i64 cd;
        double base;
    };
    std::vector<OppT> oppT;
    void buildThreats(const Obs& o, const Game& now) {
        hdarts.clear();
        oppT.clear();
        const GameInfo& gi = info();
        for (int k = 0; k < N; ++k) {
            if (k == me || is_mate(gi, k) || !o.p[k].alive) continue;
            // 他的镖在手里（观察时），或回程且离他很近
            const Dart& D = o.d[k];
            bool armed = D.st == D_HAND;
            Player P = o.p[k];
            if (!P.vis) {
                if (T - lst[k] > 60) continue;
                P = Player();
                P.x = lsx[k];
                P.y = lsy[k];
                P.st = S_FREE;
            }
            if (!armed && armT[k] <= T + 10) armed = true;
            if (!armed) continue;
            const i64 dist0 = ilen(P.x - o.me().x, P.y - o.me().y);
            if (dist0 > 10500) continue;
            LT tr;
            genOpp(P, OPP_STEPS, 0, 0, 0, -1, 0, 0, 0, 0, tr);
            // 他什么时候能扔：观察时的状态
            i64 freeAt = t0;
            if (P.st != S_FREE && P.st != S_RECALL) {
                i64 extra = P.timer;
                if (P.st == S_THROW) extra += cf.throw_post;
                if (P.st == S_SLASH) extra += cf.slash_post;
                if (P.st == S_KICK) extra += cf.kick_post;
                freeAt = t0 + extra;
            }
            freeAt = std::max(freeAt, armT[k]);
            const double base = (dist0 < 5000 ? 0.11 : dist0 < 7500 ? 0.08 : 0.05) * thrScale[k];
            {
                OppT ot;
                ot.k = k;
                ot.tr = tr;
                ot.freeAt = freeAt;
                ot.armed = true;
                ot.cd = P.cd;
                ot.base = base;
                oppT.push_back(ot);
            }
            for (i64 tau = std::max(freeAt, t0); tau <= T + 3; ++tau) {
                const i64 j1 = tau + 1 - t0;
                if (j1 < 0 || j1 > OPP_STEPS) continue;
                const i64 rx = tr.x[j1], ry = tr.y[j1];
                // 他看到的我：第 tau-DELAY 步
                i64 sv = tau - cf.delay;
                if (sv < 0) sv = 0;
                if (sv > T) sv = T;
                const Player& Ms = (sv == T) ? o.me() : hp[sv % HIST];
                if (!hv[sv % HIST] && sv != T) continue;
                for (int aim = 0; aim < 3; ++aim) {
                    i64 txp = Ms.x, typ = Ms.y;
                    const i64 dd = ilen(txp - rx, typ - ry);
                    int fl = 1;
                    while (fl < CUMN && CUM[fl] < dd - 300) ++fl;
                    if (aim == 1) {  // 按速度提前量
                        const i64 lead = fl + 2 + cf.delay;
                        txp += Ms.vx * lead;
                        typ += Ms.vy * lead;
                    } else if (aim == 2) {  // 一半提前量
                        const i64 lead = (fl + 2 + cf.delay) / 2;
                        txp += Ms.vx * lead;
                        typ += Ms.vy * lead;
                    }
                    i64 ux = txp - rx, uy = typ - ry;
                    if (!scale_to(ux, uy, UNIT, ux, uy)) continue;
                    HDart h;
                    h.s0 = tau + 1;
                    int n = dartPath(rx, ry, ux, uy, h.x, h.y, 20);
                    if (n < 0) n = -n;
                    h.len = n;
                    h.w = base * (aim == 0 ? 0.9 : aim == 1 ? 1.2 : 0.9);
                    hdarts.push_back(h);
                }
            }
        }
    }

    // —— 真实飞镖（观察里在飞的、或观察里正在蓄势的）在主人不同走位下的路线
    struct RDart {
        i64 x[OPP_STEPS + 2], y[OPP_STEPS + 2];
        int start = 0, len = 0;
        double w = 0;
        bool caught = false;
    };
    std::vector<RDart> rdarts;
    i64 armT[MAXP];  // 对手（按 CONT 情景）什么时候手里有镖

    void simDartLight(Dart D, const LT& owner, int startJ, RDart& out) const {
        const Map& M = info().M();
        int j = startJ;
        out.start = startJ;
        out.caught = false;
        for (; j < OPP_STEPS; ++j) {
            out.x[j] = D.x;
            out.y[j] = D.y;
            const i64 x0 = D.x, y0 = D.y;
            i64 x1 = x0, y1 = y0;
            bool end = false;
            if (D.st == D_OUT) {
                const i64 spd = imax(cf.dart_vmin, cf.dart_v - D.age * cf.dart_dec);
                const i64 m1 = imin(spd, D.left);
                x1 = x0 + rdiv(D.vx * m1, UNIT);
                y1 = y0 + rdiv(D.vy * m1, UNIT);
                D.left -= m1;
                ++D.age;
                i64 hx, hy;
                if (seg_wall(M, x0, y0, x1, y1, hx, hy)) {
                    x1 = hx;
                    y1 = hy;
                    end = true;
                } else if (D.left <= 0) {
                    D.st = D_BACK;
                    D.age = 0;
                }
            } else if (D.st == D_BACK || D.st == D_CALL) {
                const bool call = D.st == D_CALL;
                const i64 spd = call ? imin(cf.recall_v, cf.ret_v0 + D.age * cf.ret_acc)
                                     : (D.age < cf.dart_hang ? 0 : imin(cf.ret_v, cf.ret_v0 + (D.age - cf.dart_hang) * cf.ret_acc));
                ++D.age;
                const i64 ox = owner.x[j + 1], oy = owner.y[j + 1];
                const i64 gx = ox - x0, gy = oy - y0, gl = ilen(gx, gy);
                bool reach = false;
                if (spd == 0) {
                } else if (gl <= spd) {
                    x1 = ox;
                    y1 = oy;
                    reach = true;
                } else {
                    x1 = x0 + rdiv(gx * spd, gl);
                    y1 = y0 + rdiv(gy * spd, gl);
                }
                i64 hx, hy;
                if (!(call && cf.recall_wall) && seg_wall(M, x0, y0, x1, y1, hx, hy)) {
                    x1 = hx;
                    y1 = hy;
                    end = true;
                } else if (reach || seg_near_origin(x0 - owner.x[j], y0 - owner.y[j], x1 - ox, y1 - oy, cf.catch_r)) {
                    end = true;
                    out.caught = true;
                }
            } else {
                end = true;
            }
            D.x = x1;
            D.y = y1;
            if (end) {
                ++j;
                break;
            }
        }
        out.x[j] = D.x;
        out.y[j] = D.y;
        out.len = j + 1;
    }

    void buildRealDarts(const Obs& o) {
        rdarts.clear();
        const GameInfo& gi = info();
        for (int k = 0; k < MAXP; ++k) armT[k] = (k < N && o.d[k].st == D_HAND) ? t0 : (1LL << 40);
        for (int k = 0; k < N; ++k) {
            if (k == me || !o.p[k].alive && !dart_flying(o.d[k].st)) continue;
            if (is_mate(gi, k)) continue;
            const Player& P = o.p[k];
            Dart D = o.d[k];
            int startJ = 0;
            bool have = false;
            if (dart_flying(D.st)) {
                have = true;
            } else if (P.alive && P.vis && P.st == S_THROW && D.st == D_HAND) {
                have = true;
                startJ = int(P.timer);  // 第 t0+timer-1 步末出镖，第 t0+timer 步开始飞
            }
            if (!have) continue;
            // 主人走位情景
            static const double wts[10] = {0.45, 0.15, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05, 0.05};
            for (int sc = 0; sc < 10; ++sc) {
                LT tr;
                if (P.alive && P.vis) {
                    if (sc == 0) genOpp(P, OPP_STEPS, 0, 0, 0, -1, 0, 0, 0, 0, tr);
                    else if (sc == 1) genOpp(P, OPP_STEPS, 1, 0, 0, -1, 0, 0, 0, 0, tr);
                    else genOpp(P, OPP_STEPS, 2, DX16[(sc - 2) * 2], DY16[(sc - 2) * 2], -1, 0, 0, 0, 0, tr);
                } else {
                    for (int j = 0; j <= OPP_STEPS; ++j) { tr.x[j] = P.x; tr.y[j] = P.y; }
                    if (sc > 0) break;
                }
                Dart DD = D;
                if (startJ > 0) {
                    DD.st = D_OUT;
                    DD.x = tr.x[startJ];
                    DD.y = tr.y[startJ];
                    DD.vx = P.ax;
                    DD.vy = P.ay;
                    DD.left = cf.range;
                    DD.age = 0;
                }
                RDart r;
                simDartLight(DD, tr, startJ, r);
                r.w = (P.alive && P.vis) ? wts[sc] : 1.0;
                if (sc == 0 && r.caught && P.alive) armT[k] = t0 + r.len - 1;
                rdarts.push_back(r);
                // 去程不依赖主人：只需要一个情景，除非会折返进入视野范围
                if (D.st == D_OUT && startJ == 0 && D.left > 0) {
                    // 去程剩余步数足够长（> 推演窗口）就不用多情景
                    if (D.left > 6000) { rdarts.back().w = 1.0; break; }
                }
            }
        }
    }

    // 挡镖：我在第 T 步挥砍（方向 bx,by），判定在第 T+1 步（用镖在第 T+1 步开始时的位置）
    double realRisk(const i64* mx, const i64* my, int len, bool block, i64 bx, i64 by, i64 ox, i64 oy) const {
        const i64 R = cf.body_r + cf.dart_r + 30;
        const i64 off = T - t0;
        double risk = 0;
        for (const RDart& r : rdarts) {
            int lastStep = 1 << 30;
            if (block) {
                const i64 jb = off + 1;
                if (jb >= r.start && jb < r.len) {
                    const i64 dx = r.x[jb] - ox, dy = r.y[jb] - oy;
                    const i64 dd = dx * dx + dy * dy;
                    if (dd <= cf.slash_reach * cf.slash_reach) {
                        const i64 dl = isqrt(dd);
                        if (dl == 0 || (dx * bx + dy * by) * 1000 >= cf.block_cos * dl * UNIT) lastStep = 0;  // 第 T+1 步起被挡
                    }
                }
            }
            for (int i = 0; i + 1 < len; ++i) {
                if (i > lastStep) break;
                const i64 j = off + i;
                if (j < r.start) continue;
                if (j + 1 >= r.len) break;
                if (seg_near_origin(r.x[j] - mx[i], r.y[j] - my[i], r.x[j + 1] - mx[i + 1], r.y[j + 1] - my[i + 1], R)) {
                    risk += r.w * (i < 10 ? 1.0 : 0.7);
                    break;
                }
            }
        }
        return risk;
    }

    // —— 假想近战
    struct HMelee {
        i64 jT;      // 判定步（绝对）
        i64 x, y;    // 判定时攻击者位置
        i64 ux, uy;  // 方向
        bool kick;
        double w;
    };
    std::vector<HMelee> hmelee;
    struct OppM {
        LT tr;
        i64 freeAt;
        bool armed;
        i64 cd;
        i64 armT;
    };
    std::vector<OppM> oppM;

    void buildMelee(const Obs& o) {
        hmelee.clear();
        oppM.clear();
        const GameInfo& gi = info();
        for (int k = 0; k < N; ++k) {
            if (k == me || is_mate(gi, k) || !o.p[k].alive || !o.p[k].vis) continue;
            const Player& P = o.p[k];
            const i64 dist0 = ilen(P.x - o.me().x, P.y - o.me().y);
            if (dist0 > 7500) continue;
            const bool armed = o.d[k].st == D_HAND;
            LT tr;
            genOpp(P, OPP_STEPS, 0, 0, 0, -1, 0, 0, 0, 0, tr);
            i64 freeAt = t0;
            if (P.st != S_FREE && P.st != S_RECALL) {
                i64 extra = P.timer;
                if (P.st == S_THROW) extra += cf.throw_post;
                if (P.st == S_SLASH) extra += cf.slash_post;
                if (P.st == S_KICK) extra += cf.kick_post;
                freeAt = t0 + extra;
            }
            {
                OppM om;
                om.tr = tr;
                om.freeAt = freeAt;
                om.armed = armed;
                om.cd = P.cd;
                om.armT = armT[k];
                oppM.push_back(om);
            }
            for (i64 tau = std::max(freeAt, t0); tau <= T + 2; ++tau) {
                const i64 ja = tau - t0;
                if (ja > OPP_STEPS) break;
                i64 sv = tau - cf.delay;
                if (sv < 0) sv = 0;
                if (sv > T) sv = T;
                if (sv != T && !hv[sv % HIST]) continue;
                const Player& Ms = (sv == T) ? o.me() : hp[sv % HIST];
                const i64 Ax = tr.x[ja], Ay = tr.y[ja];
                // 直接近战
                {
                    const i64 tx = Ms.x + Ms.vx * (cf.delay + 2), ty = Ms.y + Ms.vy * (cf.delay + 2);
                    const i64 dd = ilen(tx - Ax, ty - Ay);
                    if (dd < 2300) {
                        HMelee h;
                        h.jT = tau + 1;
                        h.x = Ax;
                        h.y = Ay;
                        if (!scale_to(tx - Ax, ty - Ay, UNIT, h.ux, h.uy)) { h.ux = 1000; h.uy = 0; }
                        h.kick = !(armed || armT[k] <= tau);
                        h.w = 0.12;
                        hmelee.push_back(h);
                    }
                }
                // 冲刺接近战
                if (P.cd <= ja) {
                    for (int m = 3; m <= 5; ++m) {
                        const i64 tx = Ms.x + Ms.vx * (cf.delay + m + 1), ty = Ms.y + Ms.vy * (cf.delay + m + 1);
                        const i64 dd = ilen(tx - Ax, ty - Ay);
                        if (dd < 1200 || dd > 5000) continue;
                        i64 ux, uy;
                        if (!scale_to(tx - Ax, ty - Ay, UNIT, ux, uy)) continue;
                        i64 x = Ax, y = Ay;
                        for (int st = 0; st < m - 2; ++st) {
                            i64 vx = rdiv(ux * cf.dash_v, UNIT), vy = rdiv(uy * cf.dash_v, UNIT);
                            move_body(gi.M(), cf.body_r, x, y, vx, vy);
                        }
                        for (i64 sp : {cf.dash_v - cf.acc, cf.dash_v - 2 * cf.acc}) {
                            if (sp <= 0) break;
                            i64 vx = rdiv(ux * sp, UNIT), vy = rdiv(uy * sp, UNIT);
                            move_body(gi.M(), cf.body_r, x, y, vx, vy);
                        }
                        HMelee h;
                        h.jT = tau + m + 1;
                        h.x = x;
                        h.y = y;
                        if (!scale_to(tx - x, ty - y, UNIT, h.ux, h.uy)) { h.ux = ux; h.uy = uy; }
                        h.kick = !(armed || armT[k] <= tau + m);
                        h.w = 0.07;
                        hmelee.push_back(h);
                    }
                }
            }
        }
    }

    double meleeOne(const HMelee& h, const i64* mx, const i64* my, const unsigned char* lk, int len) const {
        const GameInfo& gi = info();
        const i64 i = h.jT + 1 - T;
        if (i < 1 || i >= len) return 0;
        const double lf = lk[i - 1] ? LOCKF : 1.0;
        const i64 px = mx[i], py = my[i];
        const i64 reach = h.kick ? cf.kick_reach : cf.slash_reach;
        const i64 cs = h.kick ? cf.kick_cos : cf.slash_cos;
        const i64 dx = px - h.x, dy = py - h.y;
        const i64 dd = dx * dx + dy * dy;
        if (dd > reach * reach) return 0;
        const i64 dl = isqrt(dd);
        if (dl > cf.body_r && (dx * h.ux + dy * h.uy) * 1000 < cs * dl * UNIT) return 0;
        if (wall_between(gi.M(), h.x, h.y, px, py)) return 0;
        if (!h.kick) return h.w * lf;
        i64 kx, ky;
        if (!scale_to(dx, dy, cf.kick_v, kx, ky)) scale_to(h.ux, h.uy, cf.kick_v, kx, ky);
        i64 x = px, y = py;
        for (int st = 0; st < cf.kick_stun; ++st) {
            i64 vx = kx, vy = ky;
            move_body(gi.M(), cf.body_r, x, y, vx, vy);
            kx = rdiv(vx * cf.kick_keep, 1000);
            ky = rdiv(vy * cf.kick_keep, 1000);
        }
        if (!standable(gi, x, y, h.jT + cf.kick_stun)) return h.w * lf;
        return h.w * 0.15 * lf;
    }

    double meleeRisk(const i64* mx, const i64* my, const unsigned char* lk, int len) const {
        double risk = 0;
        for (const HMelee& h : hmelee) risk += meleeOne(h, mx, my, lk, len);
        return risk;
    }

    // 未来（第 T+4 步以后）对手出手：瞄准的是我在这个方案里第 tau-DELAY 步的位置。
    // 镖：只有当我在看到他出手时（tau+DELAY+1）处于锁定状态、或者来不及反应时才算。
    double futureRisk(const i64* mx, const i64* my, const unsigned char* lk, int len) const {
        const i64 R = cf.body_r + cf.dart_r + 40;
        double risk = 0;
        i64 px[24], py[24];
        for (const OppT& ot : oppT) {
            for (i64 tau = std::max(ot.freeAt, T + 4); tau <= T + 11; ++tau) {
                const i64 iv = tau - cf.delay - T;  // 他看到的我：方案第 iv 步开始时
                if (iv < 1 || iv >= len) break;
                const i64 j1 = tau + 1 - t0;
                if (j1 > OPP_STEPS) break;
                const i64 rx = ot.tr.x[j1], ry = ot.tr.y[j1];
                const i64 vx = mx[iv] - mx[iv - 1], vy = my[iv] - my[iv - 1];
                const i64 dd = ilen(mx[iv] - rx, my[iv] - ry);
                if (dd > 8000) continue;
                int fl = 1;
                while (fl < CUMN && CUM[fl] < dd - 300) ++fl;
                const i64 react = tau + cf.delay + 1;  // 我看到他出手的那一步
                const i64 ir = react - T;
                const bool lockedAtReact = ir >= len ? (lk[len - 1] != 0) : (lk[ir] != 0);
                for (int aim = 0; aim < 2; ++aim) {
                    i64 tx = mx[iv], ty = my[iv];
                    if (aim == 1) {
                        const i64 lead = fl + 2 + cf.delay;
                        tx += vx * lead;
                        ty += vy * lead;
                    }
                    i64 ux, uy;
                    if (!scale_to(tx - rx, ty - ry, UNIT, ux, uy)) continue;
                    int n = dartPath(rx, ry, ux, uy, px, py, 20);
                    if (n < 0) n = -n;
                    for (int j = 1; j < n; ++j) {
                        const i64 st = tau + 1 + j;
                        const i64 i = st - T;
                        if (i < 0) continue;
                        if (i + 1 >= len) break;
                        if (seg_near_origin(px[j - 1] - mx[i], py[j - 1] - my[i], px[j] - mx[i + 1], py[j] - my[i + 1], R)) {
                            if (st <= react + 1 || lockedAtReact) risk += ot.base * (aim == 0 ? 0.9 : 1.2);
                            break;
                        }
                    }
                }
            }
        }
        return risk;
    }

    double futureMelee(const i64* mx, const i64* my, const unsigned char* lk, int len) const {
        const GameInfo& gi = info();
        double risk = 0;
        for (const OppM& om : oppM) {
            for (i64 tau = std::max(om.freeAt, T + 3); tau <= T + 10; ++tau) {
                const i64 iv = tau - cf.delay - T;
                if (iv < 1 || iv >= len) break;
                const i64 ja = tau - t0;
                if (ja > OPP_STEPS) break;
                const i64 Ax = om.tr.x[ja], Ay = om.tr.y[ja];
                const i64 vx = mx[iv] - mx[iv - 1], vy = my[iv] - my[iv - 1];
                {
                    const i64 tx = mx[iv] + vx * (cf.delay + 2), ty = my[iv] + vy * (cf.delay + 2);
                    if (ilen(tx - Ax, ty - Ay) < 2300) {
                        HMelee h;
                        h.jT = tau + 1;
                        h.x = Ax;
                        h.y = Ay;
                        if (!scale_to(tx - Ax, ty - Ay, UNIT, h.ux, h.uy)) { h.ux = 1000; h.uy = 0; }
                        h.kick = !(om.armed || om.armT <= tau);
                        h.w = 0.12;
                        risk += meleeOne(h, mx, my, lk, len);
                    }
                }
                if (om.cd <= ja) {
                    for (int m = 3; m <= 5; ++m) {
                        const i64 tx = mx[iv] + vx * (cf.delay + m + 1), ty = my[iv] + vy * (cf.delay + m + 1);
                        const i64 dd = ilen(tx - Ax, ty - Ay);
                        if (dd < 1200 || dd > 5000) continue;
                        i64 ux, uy;
                        if (!scale_to(tx - Ax, ty - Ay, UNIT, ux, uy)) continue;
                        i64 x = Ax, y = Ay;
                        for (int st = 0; st < m - 2; ++st) {
                            i64 vx2 = rdiv(ux * cf.dash_v, UNIT), vy2 = rdiv(uy * cf.dash_v, UNIT);
                            move_body(gi.M(), cf.body_r, x, y, vx2, vy2);
                        }
                        for (i64 sp : {cf.dash_v - cf.acc, cf.dash_v - 2 * cf.acc}) {
                            if (sp <= 0) break;
                            i64 vx2 = rdiv(ux * sp, UNIT), vy2 = rdiv(uy * sp, UNIT);
                            move_body(gi.M(), cf.body_r, x, y, vx2, vy2);
                        }
                        HMelee h;
                        h.jT = tau + m + 1;
                        h.x = x;
                        h.y = y;
                        if (!scale_to(tx - x, ty - y, UNIT, h.ux, h.uy)) { h.ux = ux; h.uy = uy; }
                        h.kick = !(om.armed || om.armT <= tau + m);
                        h.w = 0.07;
                        risk += meleeOne(h, mx, my, lk, len);
                    }
                }
            }
        }
        return risk;
    }

    double threatRisk(const i64* mx, const i64* my, int len) const {
        const i64 R = cf.body_r + cf.dart_r + 40;
        double risk = 0;
        for (const HDart& h : hdarts) {
            // 第 s 步：镖从 pos[s-1-s0] 到 pos[s-s0]；我从 m[s-T] 到 m[s-T+1]
            for (int j = 1; j < h.len; ++j) {
                const i64 s = h.s0 + j;
                const i64 i = s - T;
                if (i < 0) continue;
                if (i + 1 >= len) break;
                if (seg_near_origin(h.x[j - 1] - mx[i], h.y[j - 1] - my[i], h.x[j] - mx[i + 1], h.y[j] - my[i + 1], R)) {
                    const double decay = i < 8 ? 1.0 : 0.6;
                    risk += h.w * decay;
                    break;
                }
            }
        }
        return risk;
    }

    // 队友是否在我这次出手的危险区里（扔：镖的去程线附近；近战：扇形附近）
    bool mateInLine(const Game& g, i64 ux, i64 uy, i64 len, i64 margin) const {
        const GameInfo& gi = info();
        const Player& P = g.p[me];
        for (int k = 0; k < N; ++k) {
            if (!is_mate(gi, k) || !g.p[k].alive) continue;
            for (int e = 0; e < 2; ++e) {
                const i64 qx = e ? g.p[k].x + g.p[k].vx * 6 : g.p[k].x, qy = e ? g.p[k].y + g.p[k].vy * 6 : g.p[k].y;
                const i64 dx = qx - P.x, dy = qy - P.y;
                const i64 along = (dx * ux + dy * uy) / UNIT;
                if (along < -600 || along > len + 600) continue;
                const i64 perp = iabs(dx * uy - dy * ux) / UNIT;
                if (perp < margin) return true;
            }
        }
        return false;
    }

    // —— 位置评估
    int targetK = -1;
    double LOCKF = 3.0;
    double evalEnd(const Game& s) const {
        const GameInfo& gi = info();
        const Map& M = gi.M();
        const Player& P = s.p[me];
        double v = 0;
        const int cx = cell_index(P.x), cy = cell_index(P.y);
        const i64 f = cell_fall_step(gi, cx, cy);
        if (f >= 0) {
            const i64 left = f - s.t;
            if (left < 50) v -= double(50 - left) * 40.0;
        }
        int pits = 0;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                const int c = cell_at_step(cf, M, cx + dx, cy + dy, s.t + 12);
                if (c == C_PIT) pits += (iabs(dx) <= 1 && iabs(dy) <= 1) ? 2 : 1;
            }
        v -= pits * 10.0;
        const double wsafe = s.t < cf.col_start - 100 ? 4.0 : s.t < cf.col_start ? 10.0 : 25.0;
        v -= safeAt(P.x, P.y) * wsafe;
        const Dart& md = s.d[me];
        const bool armed = md.st == D_HAND;
        if (!armed) v -= 250;
        if (md.st == D_GROUND || md.st == D_RISE) v -= ilen(md.x - P.x, md.y - P.y) * 0.20;
        int nopp = 0;
        for (int k = 0; k < N; ++k) {
            if (k == me || is_mate(gi, k) || !s.p[k].alive || s.p[k].unknown) continue;
            ++nopp;
            const Player& Q = s.p[k];
            const i64 d = ilen(Q.x - P.x, Q.y - P.y);
            const Dart& qd = s.d[k];
            const bool oarmed = qd.st == D_HAND || ((qd.st == D_BACK || qd.st == D_CALL) && ilen(qd.x - Q.x, qd.y - Q.y) < 2000);
            const bool wall = wall_between(M, P.x, P.y, Q.x, Q.y);
            if (oarmed) {
                if (armed) {
                    if (d < 3800) v -= (3800 - d) * (wall ? 0.05 : 0.14);
                    else if (k == targetK && d > 6000) v -= (d - 6000) * 0.04;
                } else {
                    if (d < 7500) v -= (7500 - d) * (wall ? 0.05 : 0.12);
                }
            } else {
                if (armed) {
                    if (k == targetK) v -= iabs(d - 3200) * 0.05;
                } else {
                    if (d < 2200) v -= (2200 - d) * 0.05;
                }
            }
        }
        (void)nopp;
        return v;
    }

    struct SimOut {
        bool dead = false;
        int deadAt = 99;
        i64 mx[H + 2], my[H + 2];
        unsigned char lk[H + 2];
        int len = 0;
        double endV = 0;
        int oppDead = 0;
        int mateDead = 0;
    };

    Action planAct(const Plan& pl, int i, const Game& s) const {
        Action a;
        const Player& P = s.p[me];
        switch (pl.type) {
            case 0:  // move
                if (i < pl.K) { a.mx = pl.dx; a.my = pl.dy; }
                break;
            case 1:  // dash
                if (i == 0) { a.act = A_DASH; a.ax = pl.ax; a.ay = pl.ay; }
                break;
            case 2:  // throw
                if (i == 0) { a.act = A_THROW; a.ax = pl.ax; a.ay = pl.ay; }
                if (i < pl.K) { a.mx = pl.dx; a.my = pl.dy; }
                break;
            case 3:  // melee
                if (i == 0) { a.act = A_MELEE; a.ax = pl.ax; a.ay = pl.ay; }
                break;
            case 4:  // recall
                if (i == 0) a.act = A_RECALL;
                break;
            case 5:  // dash melee
                if (i == 0) { a.act = A_DASH; a.ax = pl.dx; a.ay = pl.dy; }
                if (i == pl.meleeAt) { a.act = A_MELEE; a.ax = pl.ax; a.ay = pl.ay; }
                break;
            default:
                break;
        }
        (void)P;
        return a;
    }

    void simPlan(const Game& now, const Plan& pl, SimOut& out) const {
        Game s = now;
        out.dead = false;
        out.deadAt = 99;
        int i = 0;
        for (; i < H; ++i) {
            out.mx[i] = s.p[me].x;
            out.my[i] = s.p[me].y;
            {
                const Player& Pm = s.p[me];
                const bool dm = Pm.st == S_DASH && Pm.timer > cf.dash_post && Pm.timer <= cf.dash_t + cf.dash_post;
                out.lk[i] = (Pm.st == S_FREE || dm) ? 0 : 1;
            }
            Action acts[MAXP];
            for (int k = 0; k < N; ++k) acts[k] = (k == me) ? planAct(pl, i, s) : predictAct(s, k);
            s.step(acts);
            s.over = false;
            if (!s.p[me].alive) {
                out.dead = true;
                out.deadAt = i;
                ++i;
                out.mx[i] = s.p[me].x;
                out.my[i] = s.p[me].y;
                out.lk[i] = 1;
                break;
            }
        }
        if (!out.dead) {
            out.mx[H] = s.p[me].x;
            out.my[H] = s.p[me].y;
            out.lk[H] = s.p[me].st == S_FREE ? 0 : 1;
            i = H;
        }
        out.len = i + 1;
        out.endV = out.dead ? 0 : evalEnd(s);
        int od = 0, md = 0;
        for (int k = 0; k < N; ++k)
            if (k != me && now.p[k].alive && !s.p[k].alive) {
                ++od;
                if (is_mate(info(), k) && s.p[k].killer == me) ++md;
            }
        out.oppDead = od;
        out.mateDead = md;
    }

    Action decide(const Obs& o) {
        cpu0 = thread_cpu_100ns();
        const GameInfo& gi = info();
        ob = o;
        T = o.t;
        t0 = std::max<i64>(0, T - cf.delay);
        hp[T % HIST] = o.me();
        hd[T % HIST] = o.my_dart();
        hv[T % HIST] = true;
        Action res;
        const Player& Me = o.me();
        if (!Me.alive) {
            ha[T % HIST] = res;
            return res;
        }
        for (int k = 0; k < N; ++k)
            if (k != me && o.p[k].alive && o.p[k].vis) {
                lsx[k] = o.p[k].x;
                lsy[k] = o.p[k].y;
                lst[k] = T;
                // 出手频率：他自由、手里有镖、离（他那时看到的）我 7.5 米内的步数，和其中开始扔镖的次数
                const Player& Q = o.p[k];
                const i64 sv = std::max<i64>(0, t0 - cf.delay);
                const Player& Mp = hv[sv % HIST] ? hp[sv % HIST] : o.me();
                const i64 dd = ilen(Q.x - Mp.x, Q.y - Mp.y);
                if (Q.st == S_THROW && prevSt[k] != S_THROW) nThrows[k] += 1;
                else if (Q.st == S_FREE && o.d[k].st == D_HAND && dd < 7500) armedSteps[k] += 1;
                prevSt[k] = Q.st;
                const double rate = (nThrows[k] + 1.0) / (armedSteps[k] + nThrows[k] + 25.0);
                thrScale[k] = std::min(3.0, std::max(0.7, rate / 0.05));
            }
        Game now = reconstruct(o);
        const Player& NM = now.p[me];
        const Dart& myD = now.d[me];
        const bool haveDart = myD.st == D_HAND;
        const bool isFree = NM.st == S_FREE;

        // 目标：最近的看得见的对手
        targetK = -1;
        i64 bestd = 1LL << 60;
        for (int k = 0; k < N; ++k) {
            if (k == me || is_mate(gi, k) || !now.p[k].alive || now.p[k].unknown) continue;
            const i64 d = ilen(now.p[k].x - NM.x, now.p[k].y - NM.y);
            if (d < bestd) { bestd = d; targetK = k; }
        }
        for (int k = 0; k < N; ++k) {
            scen[k].clear();
            if (k == me || is_mate(gi, k) || !o.p[k].alive || !o.p[k].vis) continue;
            const i64 d = ilen(now.p[k].x - NM.x, now.p[k].y - NM.y);
            if (d > 9500) continue;
            buildScen(k, o, NM.x, NM.y);
        }
        buildRealDarts(o);
        buildThreats(o, now);
        buildMelee(o);

        std::vector<Plan> plans;
        plans.reserve(96);
        {
            Plan p;
            p.type = 6;
            plans.push_back(p);
        }
        for (int d = 0; d < 16; ++d)
            for (int K : {3, 9}) {
                Plan p;
                p.type = 0;
                p.dx = DX16[d];
                p.dy = DY16[d];
                p.K = K;
                plans.push_back(p);
            }
        if (isFree && NM.cd == 0) {
            for (int d = 0; d < 16; d += 2) {
                Plan p;
                p.type = 1;
                p.ax = DX16[d];
                p.ay = DY16[d];
                p.bonus = -150;
                plans.push_back(p);
            }
        }
        if (isFree && (myD.st == D_GROUND) && cf.recall) {
            Plan p;
            p.type = 4;
            p.bonus = -100;
            plans.push_back(p);
        }
        const double KILLV = 9000;
        const double RISKW = 7000;
        // 进攻候选
        if (isFree) {
            for (int k = 0; k < N; ++k) {
                if (scen[k].empty()) continue;
                const Player& Q = now.p[k];
                const i64 dist = ilen(Q.x - NM.x, Q.y - NM.y);
                if (haveDart && dist < 8000) {
                    // 瞄准：情景"继续"在镖到达时的位置，以及若干偏移
                    double bestp = -1;
                    i64 bux = 0, buy = 0;
                    // 出镖点：大约现在的位置（蓄势中 50% 速度，忽略）
                    const i64 rx = NM.x, ry = NM.y;
                    std::vector<std::pair<i64, i64>> aims;
                    for (int si = 0; si < 2; ++si) {
                        const LT& tr = scen[k][si].tr;  // 继续 / 停（不反应）
                        // 找到达步数
                        for (int it = 0; it < 1; ++it) {
                            i64 j = T + 2 - t0;
                            for (int fl = 1; fl <= CUMN; ++fl) {
                                const i64 a = T + 1 + fl - t0;
                                if (a > OPP_STEPS) break;
                                const i64 dd = ilen(tr.x[a] - rx, tr.y[a] - ry);
                                j = a;
                                if (CUM[fl] + 300 >= dd) break;
                            }
                            aims.push_back({tr.x[j] - rx, tr.y[j] - ry});
                        }
                    }
                    aims.push_back({Q.x - rx, Q.y - ry});
                    aims.push_back({ob.p[k].x - rx, ob.p[k].y - ry});
                    const size_t base = aims.size();
                    for (size_t ai = 0; ai < base; ++ai) {
                        const double ang = std::atan2(double(aims[ai].second), double(aims[ai].first));
                        for (double off : {-0.16, -0.08, 0.08, 0.16}) {
                            aims.push_back({llround(std::cos(ang + off) * 1000), llround(std::sin(ang + off) * 1000)});
                        }
                    }
                    for (auto& am : aims) {
                        i64 ux, uy;
                        if (!scale_to(am.first, am.second, UNIT, ux, uy)) continue;
                        const double pr = throwProb(k, rx, ry, ux, uy);
                        if (pr > bestp) { bestp = pr; bux = ux; buy = uy; }
                    }
                    if (bestp > 0.05 && !(gi.cfg.mode == M_TEAM && mateInLine(now, bux, buy, 7400, 1500))) {
                        // 扔镖 + 移动方向：横向两边、后退、停
                        const i64 px1 = -buy, py1 = bux;
                        const i64 mvs[4][2] = {{px1, py1}, {-px1, -py1}, {-bux, -buy}, {0, 0}};
                        for (auto& mv : mvs) {
                            Plan p;
                            p.type = 2;
                            p.ax = bux;
                            p.ay = buy;
                            p.dx = mv[0];
                            p.dy = mv[1];
                            p.K = 6;
                            p.bonus = bestp * KILLV - (1 - bestp) * 2600;
                            plans.push_back(p);
                        }
                    }
                }
                // 近战（站着）
                if (dist < 2600) {
                    const bool kick = !haveDart;
                    const i64 a = T + 2 - t0;
                    if (a <= OPP_STEPS) {
                        // 方向：对手情景"继续"在判定时的位置
                        for (int si = 0; si < 2; ++si) {
                            const LT& tr = scen[k][si].tr;
                            i64 ux = tr.x[a] - NM.x, uy = tr.y[a] - NM.y;
                            if (!scale_to(ux, uy, UNIT, ux, uy)) { ux = Q.x - NM.x; uy = Q.y - NM.y; if (!scale_to(ux, uy, UNIT, ux, uy)) continue; }
                            double pit = 0;
                            const double pr = meleeProb(k, 0, NM.x, NM.y, ux, uy, kick, &pit);
                            if (pr < 0.05) continue;
                            Plan p;
                            p.type = 3;
                            p.ax = ux;
                            p.ay = uy;
                            if (kick) p.bonus = pr * 400 + pit * KILLV - (1 - pr) * 1500;
                            else p.bonus = pr * KILLV - (1 - pr) * 2600;
                            plans.push_back(p);
                        }
                    }
                }
                // 冲刺接近战
                if (NM.cd == 0 && dist < 5200 && dist > 1200) {
                    const bool kick = !haveDart;
                    const LT& tr = scen[k][0].tr;
                    for (int m = 3; m <= 5; ++m) {
                        const i64 a = T + m + 2 - t0;
                        if (a > OPP_STEPS) break;
                        // 冲刺方向：指向判定时他的位置
                        i64 ux = tr.x[a] - NM.x, uy = tr.y[a] - NM.y;
                        if (!scale_to(ux, uy, UNIT, ux, uy)) continue;
                        // 我的位置：前摇 2 步不动，之后 (m-2) 步 750，再滑 500 + 250
                        i64 x = NM.x, y = NM.y;
                        const Map& Mp = gi.M();
                        for (int st = 0; st < m - 2; ++st) {
                            i64 vx = rdiv(ux * cf.dash_v, UNIT), vy = rdiv(uy * cf.dash_v, UNIT);
                            move_body(Mp, cf.body_r, x, y, vx, vy);
                        }
                        for (i64 sp : {cf.dash_v - cf.acc, cf.dash_v - 2 * cf.acc}) {
                            if (sp <= 0) break;
                            i64 vx = rdiv(ux * sp, UNIT), vy = rdiv(uy * sp, UNIT);
                            move_body(Mp, cf.body_r, x, y, vx, vy);
                        }
                        i64 sx = tr.x[a] - x, sy = tr.y[a] - y;
                        if (!scale_to(sx, sy, UNIT, sx, sy)) { sx = ux; sy = uy; }
                        double pit = 0;
                        const double pr = meleeProb(k, m, x, y, sx, sy, kick, &pit);
                        if (pr < 0.1) continue;
                        Plan p;
                        p.type = 5;
                        p.dx = ux;
                        p.dy = uy;
                        p.ax = sx;
                        p.ay = sy;
                        p.meleeAt = m;
                        if (kick) p.bonus = pr * 300 + pit * KILLV - (1 - pr) * 1500 - 150;
                        else p.bonus = pr * KILLV - (1 - pr) * 3000 - 150;
                        plans.push_back(p);
                    }
                }
            }
            // 挡镖：朝每把飞来的镖挥砍
            if (haveDart) {
                for (int k = 0; k < N; ++k) {
                    if (k == me) continue;
                    const Dart& D = now.d[k];
                    if (!dart_flying(D.st)) continue;
                    const i64 dd = ilen(D.x - NM.x, D.y - NM.y);
                    if (dd > 4000) continue;
                    Plan p;
                    p.type = 3;
                    p.ax = D.x - NM.x;
                    p.ay = D.y - NM.y;
                    p.bonus = -200;
                    plans.push_back(p);
                }
            }
        }
        // 冲刺中：可以接近战（也是"刹车"：冲刺最后一步要落在地上）
        if (NM.st == S_DASH && NM.timer > cf.dash_post && NM.timer <= cf.dash_t + cf.dash_post) {
            Plan p;
            p.type = 3;
            p.ax = NM.ax;
            p.ay = NM.ay;
            p.bonus = -400;
            plans.push_back(p);
            p.ax = -NM.ax;
            p.ay = -NM.ay;
            plans.push_back(p);
            for (int k = 0; k < N; ++k) {
                if (scen[k].empty()) continue;
                const LT& tr = scen[k][0].tr;
                const i64 a = T + 2 - t0;
                if (a > OPP_STEPS) continue;
                i64 x = NM.x, y = NM.y;
                for (i64 sp : {cf.dash_v - cf.acc, cf.dash_v - 2 * cf.acc}) {
                    if (sp <= 0) break;
                    i64 vx = rdiv(NM.ax * sp, UNIT), vy = rdiv(NM.ay * sp, UNIT);
                    move_body(gi.M(), cf.body_r, x, y, vx, vy);
                }
                i64 sx = tr.x[a] - x, sy = tr.y[a] - y;
                if (!scale_to(sx, sy, UNIT, sx, sy)) continue;
                const bool kick = !haveDart;
                double pit = 0;
                const double pr = meleeProb(k, 0, x, y, sx, sy, kick, &pit);
                if (pr < 0.1) continue;
                Plan p;
                p.type = 3;
                p.ax = sx;
                p.ay = sy;
                if (kick) p.bonus = pr * 300 + pit * KILLV - (1 - pr) * 1500;
                else p.bonus = pr * KILLV - (1 - pr) * 3000;
                plans.push_back(p);
            }
        }

        // 评估
        double bestS = -1e18;
        int bestI = 0;
        SimOut so;
        for (size_t pi = 0; pi < plans.size(); ++pi) {
            Plan& pl = plans[pi];
            if (pi > 40 && cpuOver(65000)) break;
            if (pl.type == 5) {  // 冲刺斩：如果之后不接近战，单纯冲刺本身也必须安全
                Plan pd;
                pd.type = 1;
                pd.ax = pl.dx;
                pd.ay = pl.dy;
                simPlan(now, pd, so);
                if (so.dead) continue;
            }
            simPlan(now, pl, so);
            double sc = pl.bonus;
            if (so.dead) sc -= 30000 - so.deadAt * 600;
            else sc += so.endV;
            sc -= so.mateDead * 25000.0;
            sc -= threatRisk(so.mx, so.my, so.len) * RISKW;
            {
                const bool blk = pl.type == 3 && haveDart;
                i64 bx = 0, by = 0;
                if (blk && !scale_to(pl.ax, pl.ay, UNIT, bx, by)) { bx = 1000; by = 0; }
                const int q2 = so.len > 2 ? 2 : so.len - 1;
                sc -= realRisk(so.mx, so.my, so.len, blk, bx, by, so.mx[q2], so.my[q2]) * 20000;
            }
            sc -= meleeRisk(so.mx, so.my, so.lk, so.len) * 6000;
            sc -= futureRisk(so.mx, so.my, so.lk, so.len) * RISKW;
            sc -= futureMelee(so.mx, so.my, so.lk, so.len) * 6000;
            sc += double(nextRand() % 1000) * 0.03;  // 打破平手、略带随机
            pl.score = sc;
            if (sc > bestS) {
                bestS = sc;
                bestI = int(pi);
            }
        }
#ifdef DBGT
        if (T >= DBGT && T <= DBGT + 2) {
            for (auto& pl : plans)
                std::cerr << "T" << T << " type " << pl.type << " d " << pl.dx << "," << pl.dy << " K" << pl.K << " a " << pl.ax << ","
                          << pl.ay << " bonus " << pl.bonus << " score " << pl.score << "\n";
        }
#endif
        const Plan& bp = plans[size_t(bestI)];
        res = planAct(bp, 0, now);
        ha[T % HIST] = res;
        return res;
    }
};

DART_MAIN(Bot)
