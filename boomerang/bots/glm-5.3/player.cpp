// player.cpp —— GLM《飞镖乱斗》选手
// 整体思路：
//  1) 延迟补偿：每步记录自己的状态和实际动作；把“4 步前的观察（别人）+ 自己 4 步前的状态”用规则引擎
//     Game 往前推 4 步（我重放自己的真实动作，对手按当前速度滑行、进行中的动作由引擎继续结算），
//     得到“当前时刻的估计局面”。对手在这 4 步里已经出手的镖会被推演出来。
//  2) 候选动作（16 个移动方向 + 停、近战/踢的几个方向、带提前量的扔镖、格挡、冲刺接近/逃跑/捡镖、召回、
//     寻路引导），每个候选用引擎逐位精确推演 N 步：对手模型 = 滑行 + 遇到我的镖横移躲避；
//     按死亡/击杀/最终局面位置分打分，取最高。
//  3) 位置分：与敌人的理想距离（按双方是否持镖）、暴露在持镖自由敌人视线内的惩罚、接近自己落地的镖、
//     塌陷安全区距离梯度（越接近塌陷越强）、马上要塌的格子重罚、草丛隐藏小奖励。
#include "dart.hpp"
using namespace dart;
#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

struct Bot {
    // ---------------- 历史记录（延迟补偿用） ----------------
    static const int HN = 950;
    struct HRec {
        Player me;
        Dart md;
        Action act;
        bool valid = false;
    };
    HRec hist[HN];

    // ---------------- 静态地图分析 ----------------
    std::vector<int> safe_dist;              // 每格到"永不塌格"的距离（×10，8 向 Dijkstra），-1 = 不可达
    std::vector<unsigned char> walk0;        // 开局能走的格
    std::vector<i64> flight_cum;             // 去程飞 s 步后的累计距离
    // 每个对手的网格距离图（本轮 decide 里算一次）
    std::vector<int> dmap[8];
    int dmap_n = 0;
    i64 safe_cx = 0, safe_cy = 0;            // 安全区质心
    i64 cpu0 = 0;
    i64 lastx[8] = {}, lasty[8] = {}, lastt[8] = {};
    bool ever_seen[8] = {};
    int softn = 0;                            // 当前看不见但最近见过的对手
    i64 softx[8] = {}, softy[8] = {};

    struct Cand {
        Action a0;        // 第 0 步的动作
        i64 mdx = 0, mdy = 0;  // 之后的移动方向
        bool engage = false;   // 冲刺中接近战
        int horizon = 14;
    };

    void init(const GameInfo& gi) {
        const Map& M = gi.M();
        const int n = M.nx * M.ny;
        walk0.assign(n, 0);
        for (int i = 0; i < n; ++i) walk0[i] = (M.c[i] == C_FLOOR || M.c[i] == C_GRASS) ? 1 : 0;
        safe_dist.assign(n, -1);
        std::vector<std::pair<int, int>> heap;
        auto push = [&](int d, int i) {
            heap.push_back({d, i});
            std::push_heap(heap.begin(), heap.end(), std::greater<std::pair<int, int>>());
        };
        auto pop = [&]() {
            const std::pair<int, int> p = heap.front();
            std::pop_heap(heap.begin(), heap.end(), std::greater<std::pair<int, int>>());
            heap.pop_back();
            return p;
        };
        for (int i = 0; i < n; ++i)
            if (M.w[i] < 0 && walk0[i]) {
                safe_dist[i] = 0;
                push(0, i);
            }
        if (heap.empty()) {  // 没有永不塌的格：用最后一波当安全区
            int lw = -1;
            for (int i = 0; i < n; ++i)
                if (M.w[i] >= 0) lw = std::max(lw, (int)M.w[i]);
            for (int i = 0; i < n; ++i)
                if (M.w[i] == lw && walk0[i]) {
                    safe_dist[i] = 0;
                    push(0, i);
                }
        }
        while (!heap.empty()) {
            const std::pair<int, int> t = pop();
            if (safe_dist[t.second] != t.first) continue;
            const int x = t.second % M.nx, y = t.second / M.nx;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int a = x + dx, b = y + dy;
                    if (a < 0 || b < 0 || a >= M.nx || b >= M.ny) continue;
                    const int j = b * M.nx + a;
                    if (!walk0[j]) continue;
                    if (dx && dy && (!walk0[y * M.nx + a] || !walk0[b * M.nx + x])) continue;
                    const int nd = t.first + ((dx && dy) ? 14 : 10);
                    if (safe_dist[j] < 0 || nd < safe_dist[j]) {
                        safe_dist[j] = nd;
                        push(nd, j);
                    }
                }
        }
        i64 sx = 0, sy = 0, cnt = 0;
        for (int y = 0; y < M.ny; ++y)
            for (int x = 0; x < M.nx; ++x)
                if (safe_dist[y * M.nx + x] == 0) {
                    sx += cell_center(x);
                    sy += cell_center(y);
                    ++cnt;
                }
        if (cnt) {
            safe_cx = sx / cnt;
            safe_cy = sy / cnt;
        }
        flight_cum.clear();
        flight_cum.push_back(0);
        i64 acc = 0;
        for (int s = 1; s <= 60; ++s) {
            acc += imax(gi.cfg.dart_vmin, gi.cfg.dart_v - (s - 1) * gi.cfg.dart_dec);
            flight_cum.push_back(acc);
        }
    }

    // ---------------- 小工具 ----------------
    static bool dash_moving_st(const Config& cf, const Player& P) {
        return P.st == S_DASH && P.timer > cf.dash_post && P.timer <= cf.dash_t + cf.dash_post;
    }
    // 预测敌人 steps 步后的位置（利用锁死状态：冲刺方向定死、硬直不能动）
    static void predict_pos(const Config& cf, const Player& E, i64 steps, i64& px, i64& py) {
        px = E.x;
        py = E.y;
        if (E.st == S_DASH && dash_moving_st(cf, E)) {
            const i64 move_left = E.timer - cf.dash_post;  // 还能冲几步
            const i64 m = imin(steps, move_left);
            px += rdiv(E.ax * cf.dash_v, 1000) * m;
            py += rdiv(E.ay * cf.dash_v, 1000) * m;
            return;  // 之后收势原地不动
        }
        if (E.st == S_SLASH || E.st == S_SLASHED || E.st == S_KICK || E.st == S_KICKED || E.st == S_STUN || E.st == S_RECALL) return;  // 不能动
        const i64 pct = (E.st == S_THROW || E.st == S_THROWN) ? cf.throw_move : 100;
        const i64 sp = rdiv(cf.run_v * pct, 100);
        if (ilen(E.vx, E.vy) > 20) {
            i64 ux, uy;
            if (scale_to(E.vx, E.vy, sp, ux, uy)) {
                px += ux * steps;
                py += uy * steps;
            }
        }
    }
    static void dir16(int i, i64& dx, i64& dy) {
        const i64 a = (((i % 16) + 16) % 16) * 2250;
        dx = Game::isin_raw(a + 9000);
        dy = Game::isin_raw(a);
    }
    static void rotv(i64 x, i64 y, i64 delta, i64& ox, i64& oy) {
        const i64 c = Game::isin_raw(delta + 9000), s = Game::isin_raw(delta);
        ox = rdiv(x * c + y * s, 1000);
        oy = rdiv(-x * s + y * c, 1000);
    }
    static i64 seg_point_dist(i64 x0, i64 y0, i64 x1, i64 y1, i64 px, i64 py) {
        const i64 dx = x1 - x0, dy = y1 - y0;
        const i64 dd = dx * dx + dy * dy;
        if (dd == 0) return ilen(px - x0, py - y0);
        i64 t = (px - x0) * dx + (py - y0) * dy;
        t = iclamp(t, 0, dd);
        const i64 qx = x0 + rdiv(dx * t, dd), qy = y0 + rdiv(dy * t, dd);
        return ilen(px - qx, py - qy);
    }
    bool time_up(i64 budget) const {
        const i64 now = thread_cpu_100ns();
        if (cpu0 < 0 || now < 0) return false;
        return now - cpu0 >= budget;
    }
    void record(const Obs& o, const Action& a) {
        const int t = (int)o.t;
        if (t >= 0 && t < HN) {
            hist[t].me = o.me();
            hist[t].md = o.my_dart();
            hist[t].act = a;
            hist[t].valid = true;
        }
    }

    // 对手模型：默认沿当前速度滑行（遇到坑会转向或停下，不掉进去）；我的镖逼近时横移躲避。
    // 敌人持镖且我在射程内时，模型里他会对着我扔镖——瞄准用的是"他看到的我的样子"（延迟 4 步的位置和速度），
    // 这样 rollout 里我会本能地躲开"看不见但已经在路上"的镖。
    bool enemy_throws = true;
    i64 phantom_lead = 1;  // 幻影出手的提前量系数：0 = 瞄对手看到的延迟位置（无提前量），1 = 全提前量
    Action enemy_act(Game& g, int k, int seat, bool dodge, const Player* delayed_me) const {
        Action a;
        const Player& P = g.p[k];
        if (!P.alive || P.unknown) return a;
        if (P.st == S_RECALL) return a;  // 引导中：别打断他自己的召回
        if (dodge) {
            const Dart& D = g.d[seat];
            if (dart_flying(D.st)) {
                const i64 dx = P.x - D.x, dy = P.y - D.y;
                const i64 d = ilen(dx, dy);
                i64 dvx = D.vx, dvy = D.vy;
                if (D.st != D_OUT) {  // 回程/召回：朝我飞
                    dvx = g.p[seat].x - D.x;
                    dvy = g.p[seat].y - D.y;
                }
                const i64 dl = ilen(dvx, dvy);
                if (dl > 0 && d > 1800 && d < 2800 && (dx * dvx + dy * dvy) > 0 && D.age >= g.cf.delay + 1) {
                    const i64 cross = dx * dvy - dy * dvx;  // |cross|/dl = 到镖路线的垂直距离
                    if (iabs(cross) <= 1300 * dl) {
                        const i64 px = -dvy * 1000 / dl, py = dvx * 1000 / dl;
                        const i64 side = (P.vx * px + P.vy * py) >= 0 ? 1 : -1;
                        a.mx = px * side;
                        a.my = py * side;
                        return a;
                    }
                }
            }
        }
        // 贴身威胁：我在近战范围内时，模型敌人会砍/踢（按他看到的延迟位置瞄准）
        if (P.st == S_FREE && delayed_me) {
            const Player& T = *delayed_me;
            const i64 ddx = T.x - P.x, ddy = T.y - P.y;
            const i64 dd = ilen(ddx, ddy);
            const i64 reach = (g.d[k].st == D_HAND) ? g.cf.slash_reach : g.cf.kick_reach;
            if (dd < reach - 100 && !wall_between(g.LM(), P.x, P.y, T.x, T.y)) {
                a.act = A_MELEE;
                a.ax = ddx;
                a.ay = ddy;
                a.mx = a.my = 0;
                return a;
            }
        }
        // 敌人持镖且空档：对着我扔（按他看到的延迟位置 + 全程提前量）
        if (enemy_throws && P.st == S_FREE && g.d[k].st == D_HAND && delayed_me) {            const Player& T = *delayed_me;
            const i64 ddx = T.x - P.x, ddy = T.y - P.y;
            const i64 dd = ilen(ddx, ddy);
            if (dd < 5200 && !wall_between(g.LM(), P.x, P.y, T.x, T.y)) {
                i64 fl = 2;
                for (int s = 1; s < (int)flight_cum.size(); ++s)
                    if (flight_cum[s] >= dd) {
                        fl = s + 1;
                        break;
                    }
                a.act = A_THROW;
                a.ax = T.x + T.vx * fl * phantom_lead - P.x;
                a.ay = T.y + T.vy * fl * phantom_lead - P.y;
                return a;
            }
        }
        // 滑行，避开坑：先试原方向，不行试 ±45/±90 度，再不行停下
        const i64 vx = P.vx, vy = P.vy;
        if (!vx && !vy) return a;
        const Map& m = g.LM();
        auto ok_dir = [&](i64 dx, i64 dy) {
            if (!dx && !dy) return false;
            for (int s = 1; s <= 3; ++s) {
                const i64 nx = P.x + dx * s, ny = P.y + dy * s;
                const int c = cell_of_point(g.cf, m, nx, ny, g.t);
                if (c != C_FLOOR && c != C_GRASS) return false;
            }
            return true;
        };
        if (ok_dir(vx, vy)) {
            a.mx = vx * 4;
            a.my = vy * 4;
            return a;
        }
        static const i64 COS45 = 707;
        const i64 rots[4][4] = {{COS45, -COS45, COS45, COS45}, {-COS45, COS45, -COS45, COS45}, {0, -1000, 1000, 0}, {0, 1000, -1000, 0}};
        for (int r = 0; r < 4; ++r) {
            const i64 dx = rdiv(vx * rots[r][0] + vy * rots[r][1], 1000);
            const i64 dy = rdiv(vx * rots[r][2] + vy * rots[r][3], 1000);
            if (ok_dir(dx, dy)) {
                a.mx = dx * 4;
                a.my = dy * 4;
                return a;
            }
        }
        return a;  // 停下
    }

    // 最终局面的位置分（用预计算的网格距离图，路径感知）
    i64 pos_score(Game& g, int seat, i64 t_eval, const Player* delayed_me) {
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        const Map& M = gi.M();
        const Player& me = g.p[seat];
        const Dart& md = g.d[seat];
        i64 sc = 0;
        const bool i_armed = (md.st == D_HAND);
        const bool ffa = (cf.mode == M_FFA);
        const int mycx = cell_index(me.x), mycy = cell_index(me.y);
        // 四人局：有几个持镖空闲的敌人在盯着我（有视线、6000 内）——多方向威胁要更保守
        int n_threat = 0;
        if (ffa) {
            for (int k = 0; k < g.n; ++k) {
                if (k == seat || is_mate(gi, k)) continue;
                const Player& E = g.p[k];
                if (!E.alive || E.unknown || E.st != S_FREE || g.d[k].st != D_HAND) continue;
                const i64 d = ilen(E.x - me.x, E.y - me.y);
                if (d <= 6000 && !wall_between(g.LM(), me.x, me.y, E.x, E.y)) ++n_threat;
            }
        }
        i64 nearest = -1;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || is_mate(gi, k)) continue;
            const Player& E = g.p[k];
            if (!E.alive || E.unknown) continue;
            const i64 d = ilen(E.x - me.x, E.y - me.y);
            if (nearest < 0 || d < nearest) nearest = d;
            const bool e_armed = (g.d[k].st == D_HAND);
            const bool e_free = (E.st == S_FREE);
            i64 ideal;
            const i64 early = (ffa && t_eval < 60) ? 1500 : 0;  // 四人局开局混战期：整体更保守
            if (i_armed && !(e_free && e_armed))
                ideal = ffa ? (n_threat >= 2 ? 5200 : 3400) : 2500;  // 对方不能马上扔镖：进攻窗口
            else if (i_armed)
                ideal = ffa ? (n_threat >= 2 ? 6800 : 5200) : 4600;
            else if (e_free && e_armed)
                ideal = ffa ? (n_threat >= 2 ? 8200 : 7400) : 6800;  // 我空手对方持镖：拉开
            else
                ideal = ffa ? (n_threat >= 2 ? 5600 : 4200) : 3000;
            ideal += early;
            // 网格距离（找不到图或不可达时退回欧氏）
            i64 gd = -1;
            if (dmap_n > k && !dmap[k].empty() && M.inside(mycx, mycy)) {
                const int dv = dmap[k][mycy * M.nx + mycx];
                if (dv >= 0) gd = i64(dv) * 50;
            }
            if (gd < 0) gd = d;
            sc -= iabs(gd - ideal) / 2;
            sc -= iabs(d - ideal) / 2;
            if (e_free && e_armed && d < 5600 && !wall_between(g.LM(), me.x, me.y, E.x, E.y)) {
                const i64 w = ffa ? 1 : 2;
                sc -= (5600 - d) * w / 2;  // 四人局被盯更危险：多个方向同时有镖
            }
            if (E.st == S_STUN) sc += 1200;
        }
        // 队友：别贴太近（4v4 减少误伤和拥挤）
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || !is_mate(gi, k)) continue;
            const Player& E = g.p[k];
            if (!E.alive || E.unknown) continue;
            const i64 d = ilen(E.x - me.x, E.y - me.y);
            if (d < 1400) sc -= (1400 - d) * 2;
        }
        if (nearest < 0) {
            sc -= ilen(safe_cx - me.x, safe_cy - me.y) / 3;
        } else if (nearest > 3200 && cell_of_point(cf, M, me.x, me.y, t_eval) == C_GRASS) {
            sc += 60;
        }
        // 看不见但最近见过的对手：保持距离
        for (int i = 0; i < softn; ++i) {
            const i64 d = ilen(softx[i] - me.x, softy[i] - me.y);
            sc -= iabs(d - 5200) / 2;
        }
        // 躲"无提前量"的镖：有持镖空闲的敌人盯着时，别待在自己 4 步前的位置附近（那是他们看到的落点）
        if (delayed_me) {
            for (int k = 0; k < g.n; ++k) {
                if (k == seat || is_mate(gi, k)) continue;
                const Player& E = g.p[k];
                if (!E.alive || E.unknown || E.st != S_FREE || g.d[k].st != D_HAND) continue;
                const i64 d = ilen(E.x - me.x, E.y - me.y);
                if (d > 5600 || wall_between(g.LM(), me.x, me.y, E.x, E.y)) continue;
                const i64 dd = ilen(me.x - delayed_me->x, me.y - delayed_me->y);
                if (dd < 1400) sc -= (1400 - dd);
            }
        }
        // 自己的镖
        if (md.st == D_GROUND)
            sc -= ilen(md.x - me.x, md.y - me.y) / 2;
        else if (md.st == D_HAND)
            sc += 1500;
        // 塌陷：离安全区越近越好（弱常驻 + 后期强梯度）
        if (M.inside(mycx, mycy)) {
            const i64 sd = safe_dist[mycy * M.nx + mycx];
            if (sd >= 0) sc -= sd;
            const i64 cs = cf.cstart();
            if (t_eval > cs - 110) {
                if (sd >= 0) {
                    i64 ramp = (t_eval - (cs - 110)) / 2;
                    if (ramp > 100) ramp = 100;
                    sc -= sd * ramp;
                }
                const i64 fs = cell_fall_step(gi, mycx, mycy);
                if (fs >= 0 && t_eval >= fs - 45) sc -= (45 - imax(0, fs - t_eval)) * 260;
            }
        } else {
            sc -= 12000;
        }
        return sc;
    }

    // 用引擎推演一个候选。对两条"看不见的窗口里对手已经出手"的假设线（无提前量 / 全提前量）
    // 各推一次、取较差的分：只留对两类瞄准习惯都稳健的动作。
    i64 rollout(const Game& g0, const Game& g1, const Cand& c, int seat, int t_now) {
        phantom_lead = 0;
        const i64 s0 = rollout_mode(g0, c, seat, t_now);
        phantom_lead = 1;
        const i64 s1 = rollout_mode(g1, c, seat, t_now);
        return s0 < s1 ? s0 : s1;
    }
    i64 rollout_mode(Game g, const Cand& c, int seat, int t_now) {
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        Action acts[MAXP];
        i64 sc = 0;
        Player snaps[40];  // snaps[s] = 我在第 t+s 步开始时的状态
        for (int s = 0; s < c.horizon && s < 40; ++s) {
            Action mine;
            if (s == 0) {
                mine = c.a0;
            } else {
                mine.mx = c.mdx;
                mine.my = c.mdy;
                if (c.engage) {
                    const Player& me = g.p[seat];
                    if (me.st == S_DASH && dash_moving_st(g.cf, me)) {
                        i64 bd = 1 << 30;
                        int bk = -1;
                        for (int k = 0; k < g.n; ++k) {
                            if (k == seat || is_mate(gi, k) || !g.p[k].alive || g.p[k].unknown) continue;
                            const i64 d = ilen(g.p[k].x - me.x, g.p[k].y - me.y);
                            if (d < bd) {
                                bd = d;
                                bk = k;
                            }
                        }
                        if (bk >= 0 && bd <= 2500 && !wall_between(g.LM(), me.x, me.y, g.p[bk].x, g.p[bk].y)) {
                            mine.act = A_MELEE;
                            mine.ax = g.p[bk].x - me.x;
                            mine.ay = g.p[bk].y - me.y;
                            mine.mx = mine.my = 0;
                        }
                    }
                }
            }
            // 对手看到的"我"：4 步以前
            const Player* delayed = nullptr;
            if (s < 4) {
                const int idx = t_now + s - (int)cf.delay;
                if (idx >= 0 && idx < HN && hist[idx].valid) delayed = &hist[idx].me;
            } else if (s - 4 < 40) {
                delayed = &snaps[s - 4];
            }
            for (int k = 0; k < g.n; ++k) acts[k] = (k == seat) ? mine : enemy_act(g, k, seat, true, delayed);
            if (s < 40) snaps[s] = g.p[seat];
            bool was[MAXP];
            for (int k = 0; k < g.n; ++k) was[k] = g.p[k].alive != 0;
            g.step(acts);
            if (!g.p[seat].alive) return -100000 + s * 200;
            // 每步：被持镖空闲的敌人盯着时，站定很危险（无提前量的镖对不动目标百发百中；
            // 而且对手看到的正是我几步前的位置，待在那一带就是待在弹着点上）
            {
                const Player& menow = g.p[seat];
                bool threatened = false;
                for (int k = 0; k < g.n; ++k) {
                    if (k == seat || is_mate(gi, k)) continue;
                    const Player& E = g.p[k];
                    if (!E.alive || E.unknown || E.st != S_FREE || g.d[k].st != D_HAND) continue;
                    const i64 d = ilen(E.x - menow.x, E.y - menow.y);
                    if (d <= 5600 && !wall_between(g.LM(), menow.x, menow.y, E.x, E.y)) {
                        threatened = true;
                        break;
                    }
                }
                if (threatened) {
                    if (ilen(menow.vx, menow.vy) < 120) sc -= 400;
                    const Player* dm = nullptr;
                    if (s >= 4)
                        dm = &snaps[s - 4];
                    else {
                        const int idx = t_now + s - (int)cf.delay;
                        if (idx >= 0 && idx < HN && hist[idx].valid) dm = &hist[idx].me;
                    }
                    if (dm) {
                        const i64 dd = ilen(menow.x - dm->x, menow.y - dm->y);
                        if (dd < 1200) sc -= (1200 - dd) * 3 / 4;  // 别待在对手看到的弹着点一带
                    }
                }
            }
            for (int k = 0; k < g.n; ++k) {
                if (!was[k] || g.p[k].alive || k == seat) continue;
                if (is_mate(gi, k)) {
                    sc -= 30000;
                    continue;
                }
                if (g.p[k].killer == seat) {
                    sc += 55000;  // 我打死的
                } else {
                    sc += 3000;  // 别的原因死的（多半是模型误差）：少给
                }
            }
            if (g.over) break;
        }
        const Player* dme = (c.horizon >= 4 && c.horizon - 4 < 40) ? &snaps[c.horizon - 4] : nullptr;
        sc += pos_score(g, seat, t_now + c.horizon, dme);
        return sc;
    }

    Action decide(const Obs& o) {
        cpu0 = thread_cpu_100ns();
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        const int seat = gi.seat;
        const int t = (int)o.t;
        Action ret;
        const Player& me = o.me();
        if (!me.alive) {
            record(o, ret);
            return ret;
        }

        // 记住见过的对手
        for (int k = 0; k < o.n; ++k) {
            if (k == seat || is_mate(gi, k)) continue;
            if (o.p[k].alive && o.p[k].vis) {
                lastx[k] = o.p[k].x;
                lasty[k] = o.p[k].y;
                lastt[k] = t;
                ever_seen[k] = true;
            }
        }
        softn = 0;
        for (int k = 0; k < o.n; ++k) {
            if (k == seat || is_mate(gi, k) || !ever_seen[k]) continue;
            if (o.p[k].alive && !o.p[k].vis && t - lastt[k] <= 150) {
                softx[softn] = lastx[k];
                softy[softn] = lasty[k];
                ++softn;
            }
        }

        // -------- 延迟补偿：把 4 步前的局面推到现在（两种幻影瞄准假设各一条线） --------
        auto reconstruct = [&](Game g, i64 lead_mode) {
            phantom_lead = lead_mode;
            const int bt2 = (int)imax(0, (i64)t - cf.delay);
            if (bt2 < t && hist[bt2].valid) {
                g.p[seat] = hist[bt2].me;
                g.d[seat] = hist[bt2].md;
                g.p[seat].px = g.p[seat].x;
                g.p[seat].py = g.p[seat].y;
                g.p[seat].unknown = 0;
                g.t = bt2;
                for (int i = bt2; i < t; ++i) {
                    Action acts[MAXP];
                    const Player* delayed = nullptr;
                    const int idx = i - (int)cf.delay;
                    if (idx >= 0 && idx < HN && hist[idx].valid) delayed = &hist[idx].me;
                    for (int k = 0; k < g.n; ++k) acts[k] = (k == seat) ? hist[i].act : enemy_act(g, k, seat, true, delayed);
                    g.step(acts);
                    g.over = false;
                }
            }
            return g;
        };
        Game g0 = reconstruct(Game(gi, o), 0);
        Game g = reconstruct(Game(gi, o), 1);
        phantom_lead = 1;
        const Player& mest = g.p[seat];
        const Dart& mdst = g.d[seat];

        // 敌人的网格距离图（每步一次，pos_score 里查表）
        dmap_n = g.n;
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || is_mate(gi, k) || !g.p[k].alive || g.p[k].unknown) {
                dmap[k].clear();
                continue;
            }
            dmap[k] = grid_dist(gi, cell_index(g.p[k].x), cell_index(g.p[k].y), t);
        }

        // -------- 不可控状态 --------
        if (me.st == S_SLASH || me.st == S_SLASHED || me.st == S_KICK || me.st == S_KICKED || me.st == S_STUN) {
            record(o, ret);
            return ret;
        }
        if (me.st == S_DASH) {
            const bool moving = dash_moving_st(cf, me);
            if (moving && cf.dash_slash) {
                i64 bd = 1 << 30;
                int bk = -1;
                for (int k = 0; k < g.n; ++k) {
                    if (k == seat || is_mate(gi, k) || !g.p[k].alive || g.p[k].unknown) continue;
                    const i64 d = ilen(g.p[k].x - me.x, g.p[k].y - me.y);
                    if (d < bd) {
                        bd = d;
                        bk = k;
                    }
                }
                if (bk >= 0 && bd <= 2400 && !wall_between(g.LM(), me.x, me.y, g.p[bk].x, g.p[bk].y)) {
                    ret.act = A_MELEE;
                    ret.ax = g.p[bk].x - me.x;
                    ret.ay = g.p[bk].y - me.y;
                }
            }
            record(o, ret);
            return ret;
        }

        // -------- 估计的敌人列表（按距离排序） --------
        int eidx[8], ne = 0;
        i64 ed[8];
        for (int k = 0; k < g.n; ++k) {
            if (k == seat || is_mate(gi, k) || !g.p[k].alive || g.p[k].unknown) continue;
            eidx[ne] = k;
            ed[ne] = ilen(g.p[k].x - mest.x, g.p[k].y - mest.y);
            ++ne;
        }
        for (int i = 1; i < ne; ++i)
            for (int j = i; j > 0 && ed[j] < ed[j - 1]; --j) {
                std::swap(ed[j], ed[j - 1]);
                std::swap(eidx[j], eidx[j - 1]);
            }
        const bool free_now = (me.st == S_FREE);
        const bool heavy = (o.n >= 6);

        std::vector<Cand> cands;

        // ---- 近战（挥砍 / 踢） ----
        if (free_now && ne > 0 && ed[0] < 2000) {
            const Player& E = g.p[eidx[0]];
            i64 tx, ty;
            predict_pos(cf, E, 2, tx, ty);  // 起手 2 步后的对方位置（冲刺/硬直里的人跑不掉）
            bool mate_in_cone = false;
            for (int m = 0; m < g.n; ++m) {
                if (m == seat || !is_mate(gi, m) || !g.p[m].alive || g.p[m].unknown) continue;
                const i64 dxm = g.p[m].x - me.x, dym = g.p[m].y - me.y;
                const i64 dm = ilen(dxm, dym);
                if (dm > cf.slash_reach) continue;
                if ((dxm * (tx - me.x) + dym * (ty - me.y)) * 1000 < 0) continue;  // 反方向不管
                mate_in_cone = true;
            }
            if (!mate_in_cone) {
                const int nvar = (mdst.st == D_HAND) ? 1 : 5;  // 空手踢：多试几个方向把人踢进坑
                for (int vi = 0; vi < nvar; ++vi) {
                    Cand c;
                    c.a0.act = A_MELEE;
                    i64 ax = tx - me.x, ay = ty - me.y;
                    if (vi > 0) {
                        const i64 delta = (vi % 2 ? 1 : -1) * 3500 * ((vi + 1) / 2);
                        rotv(ax, ay, delta, ax, ay);
                    }
                    c.a0.ax = ax;
                    c.a0.ay = ay;
                    c.horizon = 16;
                    cands.push_back(c);
                }
            }
        }

        // ---- 扔镖（提前量若干档） ----
        if (free_now && mdst.st == D_HAND) {
            int done = 0;
            const int cap = heavy ? 5 : 9;
            for (int ei = 0; ei < imin(ne, 2) && done < cap; ++ei) {
                const int k = eidx[ei];
                const Player& E = g.p[k];
                if (ed[ei] > 6900) continue;
                if (wall_between(g.LM(), me.x, me.y, E.x, E.y)) continue;
                bool blocked_mate = false;
                for (int m = 0; m < g.n; ++m) {
                    if (m == seat || !is_mate(gi, m) || !g.p[m].alive || g.p[m].unknown) continue;
                    if (seg_point_dist(me.x, me.y, E.x, E.y, g.p[m].x, g.p[m].y) < 900) {
                        blocked_mate = true;
                        break;
                    }
                }
                if (blocked_mate) continue;
                i64 fl = 2;
                for (int s = 1; s < (int)flight_cum.size(); ++s)
                    if (flight_cum[s] >= ed[ei]) {
                        fl = s + 2;
                        break;
                    }
                i64 leadset[6];
                int nl = 0;
                auto addl = [&](i64 L) {
                    if (L < 0) L = 0;
                    if (L > 16) L = 16;
                    for (int q = 0; q < nl; ++q)
                        if (leadset[q] == L) return;
                    if (nl < 6) leadset[nl++] = L;
                };
                addl(fl - 2);
                addl(fl);
                addl(fl + 2);
                addl(0);
                addl(fl + 4);
                for (int li = 0; li < nl && done < cap; ++li) {
                    i64 tx, ty;
                    predict_pos(cf, E, leadset[li], tx, ty);  // 锁死状态外推：冲刺/硬直里的人跑不掉
                    Cand c;
                    c.a0.act = A_THROW;
                    c.a0.ax = tx - me.x;
                    c.a0.ay = ty - me.y;
                    i64 rx = me.x - E.x, ry = me.y - E.y;
                    rotv(rx, ry, (li % 2 ? 4500 : -4500), c.mdx, c.mdy);  // 扔完往两侧斜撤（回程镖追我的位置，绕圈能带弯）
                    c.a0.mx = c.mdx;
                    c.a0.my = c.mdy;
                    c.horizon = heavy ? 18 : 22;
                    cands.push_back(c);
                    ++done;
                }
            }
        }

        // ---- 格挡：正前方有镖快到了（起手 2 步后判定时镖要正好进 1500 以内、起手途中又不能先被打中） ----
        if (free_now && mdst.st == D_HAND) {
            i64 bd = 1 << 30;
            int bk = -1;
            i64 bnx = 0, bny = 0;
            for (int k = 0; k < g.n; ++k) {
                if (k == seat) continue;  // 队友的镖也伤人，一起挡
                const Dart& D = g.d[k];
                if (!dart_flying(D.st)) continue;
                const i64 dx = me.x - D.x, dy = me.y - D.y;
                const i64 d = ilen(dx, dy);
                if (d < 200) continue;
                i64 dvx = D.vx, dvy = D.vy;
                if (D.st == D_BACK || D.st == D_CALL) {
                    dvx = g.p[k].x - D.x;
                    dvy = g.p[k].y - D.y;
                }
                const i64 dl = ilen(dvx, dvy);
                if (dl <= 0) continue;
                if ((dx * dvx + dy * dvy) <= 0) continue;
                if (wall_between(g.LM(), me.x, me.y, D.x, D.y)) continue;
                const i64 spd = dart_step_len(cf, D);
                if (d < 600 + spd || d > 1600 + 2 * spd) continue;  // 判定那一步镖要在 [0,1500]、途中别先中
                if (d < bd) {
                    bd = d;
                    bk = k;
                    bnx = D.x + rdiv(dvx * 2 * spd, dl) - me.x;  // 瞄判定那一步镖的位置
                    bny = D.y + rdiv(dvy * 2 * spd, dl) - me.y;
                }
            }
            if (bk >= 0) {
                Cand c;
                c.a0.act = A_MELEE;
                c.a0.ax = bnx;
                c.a0.ay = bny;
                c.horizon = 14;
                cands.push_back(c);
            }
        }

        // 冲刺候选的安全检查：落点（按 4 步位移 + 前摇不动算）及途中要能站
        auto dash_ok = [&](i64 dx, i64 dy) {
            i64 ux, uy;
            if (!scale_to(dx, dy, cf.dash_v, ux, uy)) return false;
            for (int s = 1; s <= cf.dash_t; ++s) {
                const i64 px = me.x + ux * s, py = me.y + uy * s;
                const i64 tt = t + s;
                const int cx = cell_index(px), cy = cell_index(py);
                if (!gi.M().inside(cx, cy)) return false;
                const int c = cell_at_step(cf, gi.M(), cx, cy, tt);
                if (c != C_FLOOR && c != C_GRASS) return false;
                // 周围一圈也别是坑（留出身体半径的余量，避开"停在格线上"的危险）
                for (int ddy = -1; ddy <= 1; ++ddy)
                    for (int ddx = -1; ddx <= 1; ++ddx) {
                        if (!ddy && !ddx) continue;
                        const int c2 = cell_at_step(cf, gi.M(), cx + ddx, cy + ddy, tt);
                        if (c2 == C_PIT && (iabs(ddx) + iabs(ddy) == 1) && !gi.M().inside(cx + ddx, cy + ddy)) continue;
                        if (c2 == C_PIT && (iabs(ddx) + iabs(ddy) == 1)) return false;  // 紧邻坑：贴边停危险
                    }
            }
            return true;
        };

        // ---- 冲刺 ----
        if (free_now && me.cd == 0) {
            if (ne > 0) {
                const Player& E = g.p[eidx[0]];
                // 对面持镖且空闲时，远距离冲刺接近 = 送给对方一个锁死方向的活靶子：不许
                const bool can_punish = (E.st == S_FREE && g.d[eidx[0]].st == D_HAND);
                if (!can_punish || ed[0] < 4300) {
                    if (dash_ok(E.x - me.x, E.y - me.y)) {
                        Cand c;
                        c.a0.act = A_DASH;
                        c.a0.ax = E.x - me.x;
                        c.a0.ay = E.y - me.y;
                        c.engage = true;
                        c.horizon = 18;
                        cands.push_back(c);
                    }
                }
                if (dash_ok(me.x - E.x, me.y - E.y)) {
                    Cand c;
                    c.a0.act = A_DASH;
                    c.a0.ax = me.x - E.x;
                    c.a0.ay = me.y - E.y;
                    c.mdx = me.x - E.x;
                    c.mdy = me.y - E.y;
                    c.horizon = 16;
                    cands.push_back(c);
                }
            }
            if (mdst.st == D_GROUND) {
                if (dash_ok(mdst.x - me.x, mdst.y - me.y)) {
                    Cand c;
                    c.a0.act = A_DASH;
                    c.a0.ax = mdst.x - me.x;
                    c.a0.ay = mdst.y - me.y;
                    c.mdx = mdst.x - me.x;
                    c.mdy = mdst.y - me.y;
                    c.horizon = 16;
                    cands.push_back(c);
                }
            }
            if (t > cf.cstart() - 140) {
                if (dash_ok(safe_cx - me.x, safe_cy - me.y)) {
                    Cand c;
                    c.a0.act = A_DASH;
                    c.a0.ax = safe_cx - me.x;
                    c.a0.ay = safe_cy - me.y;
                    c.mdx = safe_cx - me.x;
                    c.mdy = safe_cy - me.y;
                    c.horizon = 16;
                    cands.push_back(c);
                }
            }
        }

        // ---- 召回 ----
        if (free_now && cf.recall && mdst.st == D_GROUND) {
            bool safe = true;
            for (int ei = 0; ei < ne; ++ei)
                if (ed[ei] < 4600) {
                    safe = false;
                    break;
                }
            for (int i = 0; i < softn; ++i)
                if (ilen(softx[i] - me.x, softy[i] - me.y) < 4200) safe = false;
            const i64 dd = ilen(mdst.x - me.x, mdst.y - me.y);
            i64 wx, wy;
            const bool reach = path_next(gi, me.x, me.y, mdst.x, mdst.y, t, wx, wy);
            if (safe && (dd > 3000 || !reach)) {
                Cand c;
                c.a0.act = A_RECALL;
                c.horizon = 18;
                cands.push_back(c);
            }
        }
        // 召回引导中：继续引导也是一个候选（移动会打断）
        if (me.st == S_RECALL) {
            Cand c;
            c.a0.act = A_RECALL;
            c.horizon = 16;
            cands.push_back(c);
        }

        // ---- 寻路引导的移动 ----
        if (mdst.st == D_GROUND) {
            i64 wx, wy;
            if (path_next(gi, me.x, me.y, mdst.x, mdst.y, t, wx, wy)) {
                Cand c;
                c.a0.mx = wx - me.x;
                c.a0.my = wy - me.y;
                c.mdx = wx - me.x;
                c.mdy = wy - me.y;
                c.horizon = heavy ? 12 : 14;
                cands.push_back(c);
            }
        }
        if (t > cf.cstart() - 150) {
            i64 wx, wy;
            if (path_next(gi, me.x, me.y, safe_cx, safe_cy, t, wx, wy)) {
                Cand c;
                c.a0.mx = wx - me.x;
                c.a0.my = wy - me.y;
                c.mdx = wx - me.x;
                c.mdy = wy - me.y;
                c.horizon = heavy ? 12 : 14;
                cands.push_back(c);
            }
        }

        // ---- 移动候选 ----
        {
            const int nd = heavy ? 8 : 16;
            for (int i = 0; i < nd; ++i) {
                Cand c;
                i64 dx, dy;
                dir16(i, dx, dy);
                c.a0.mx = dx;
                c.a0.my = dy;
                c.mdx = dx;
                c.mdy = dy;
                c.horizon = heavy ? 12 : 14;
                cands.push_back(c);
            }
            Cand c;
            c.horizon = heavy ? 12 : 14;
            cands.push_back(c);  // 停住
        }

        // -------- 逐个推演打分 --------
        i64 best = -(1LL << 62);
        int bi = -1;
        const i64 budget = heavy ? 52000 : 62000;
        for (size_t i = 0; i < cands.size(); ++i) {
            if ((int)i > 3 && time_up(budget)) break;
            const i64 sc = rollout(g0, g, cands[i], seat, t);
            if (sc > best) {
                best = sc;
                bi = (int)i;
            }
        }
        if (bi >= 0) ret = cands[bi].a0;
        record(o, ret);
        return ret;
    }
};

DART_MAIN(Bot)
