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

    // 对手模型：默认按当前速度滑行；我的镖逼近时横移躲避（近似对手看到镖后的反应）
    Action enemy_act(const Game& g, int k, int seat, bool dodge) const {
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
                if (dl > 0 && d < 2400 && (dx * dvx + dy * dvy) > 0) {
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
        a.mx = P.vx * 4;
        a.my = P.vy * 4;
        return a;
    }

    // 最终局面的位置分
    i64 pos_score(Game& g, int seat, i64 t_eval) {
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        const Map& M = gi.M();
        const Player& me = g.p[seat];
        const Dart& md = g.d[seat];
        i64 sc = 0;
        const bool i_armed = (md.st == D_HAND);
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
            if (i_armed && !(e_free && e_armed))
                ideal = 2500;  // 对方不能马上扔镖：进攻窗口
            else if (i_armed)
                ideal = 4600;
            else if (e_free && e_armed)
                ideal = 6800;  // 我空手对方持镖：拉开
            else
                ideal = 3000;
            sc -= iabs(d - ideal);
            if (e_free && e_armed && d < 5600 && !wall_between(g.LM(), me.x, me.y, E.x, E.y)) sc -= (5600 - d) * 2 / 3;
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
        // 自己的镖
        if (md.st == D_GROUND)
            sc -= ilen(md.x - me.x, md.y - me.y) / 2;
        else if (md.st == D_HAND)
            sc += 1500;
        // 塌陷
        const i64 cs = cf.cstart();
        if (t_eval > cs - 110) {
            const int cx = cell_index(me.x), cy = cell_index(me.y);
            if (M.inside(cx, cy)) {
                const i64 sd = safe_dist[cy * M.nx + cx];
                if (sd >= 0) {
                    i64 ramp = (t_eval - (cs - 110)) / 2;
                    if (ramp > 100) ramp = 100;
                    sc -= sd * ramp;
                }
                const i64 fs = cell_fall_step(gi, cx, cy);
                if (fs >= 0 && t_eval >= fs - 45) sc -= (45 - imax(0, fs - t_eval)) * 260;
            } else {
                sc -= 12000;
            }
        }
        return sc;
    }

    // 用引擎推演一个候选
    i64 rollout(Game g, const Cand& c, int seat, int t_now) {
        const GameInfo& gi = info();
        Action acts[MAXP];
        i64 sc = 0;
        for (int s = 0; s < c.horizon; ++s) {
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
            for (int k = 0; k < g.n; ++k) acts[k] = (k == seat) ? mine : enemy_act(g, k, seat, true);
            bool was[MAXP];
            for (int k = 0; k < g.n; ++k) was[k] = g.p[k].alive != 0;
            g.step(acts);
            if (!g.p[seat].alive) return -100000 + s * 200;
            for (int k = 0; k < g.n; ++k) {
                if (!was[k] || g.p[k].alive || k == seat) continue;
                if (is_mate(gi, k)) {
                    sc -= 30000;
                    continue;
                }
                sc += 45000;
                if (g.p[k].killer == seat) sc += 10000;
            }
            if (g.over) {
                const bool iwin = (g.winner == seat) || (gi.cfg.mode == M_TEAM && g.winner == gi.team[seat]);
                if (iwin)
                    sc += 30000;
                else if (g.winner >= 0)
                    sc -= 20000;
                break;
            }
        }
        sc += pos_score(g, seat, t_now + c.horizon);
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

        // -------- 延迟补偿：把 4 步前的局面推到现在 --------
        Game g(gi, o);
        const int bt = (int)imax(0, (i64)t - cf.delay);
        if (bt < t && hist[bt].valid) {
            g.p[seat] = hist[bt].me;
            g.d[seat] = hist[bt].md;
            g.p[seat].px = g.p[seat].x;
            g.p[seat].py = g.p[seat].y;
            g.p[seat].unknown = 0;
            g.t = bt;
            for (int i = bt; i < t; ++i) {
                Action acts[MAXP];
                for (int k = 0; k < g.n; ++k) acts[k] = (k == seat) ? hist[i].act : enemy_act(g, k, seat, true);
                g.step(acts);
                g.over = false;
            }
        }
        const Player& mest = g.p[seat];
        const Dart& mdst = g.d[seat];

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
            const i64 tx = E.x + E.vx * 2, ty = E.y + E.vy * 2;  // 起手 2 步的提前量
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
                    Cand c;
                    c.a0.act = A_THROW;
                    c.a0.ax = E.x + E.vx * leadset[li] - me.x;
                    c.a0.ay = E.y + E.vy * leadset[li] - me.y;
                    i64 rx = me.x - E.x, ry = me.y - E.y;
                    rotv(rx, ry, 4500, c.mdx, c.mdy);  // 扔完斜着撤
                    c.a0.mx = c.mdx;
                    c.a0.my = c.mdy;
                    c.horizon = heavy ? 18 : 22;
                    cands.push_back(c);
                    ++done;
                }
            }
        }

        // ---- 格挡：正前方有镖快到了 ----
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
                if (d > 2400 || d < 200) continue;
                i64 dvx = D.vx, dvy = D.vy;
                if (D.st == D_BACK || D.st == D_CALL) {
                    dvx = g.p[k].x - D.x;
                    dvy = g.p[k].y - D.y;
                }
                const i64 dl = ilen(dvx, dvy);
                if (dl <= 0) continue;
                if ((dx * dvx + dy * dvy) <= 0) continue;
                if (wall_between(g.LM(), me.x, me.y, D.x, D.y)) continue;
                if (d < bd) {
                    bd = d;
                    bk = k;
                    const i64 spd = dart_step_len(cf, D);
                    bnx = D.x + rdiv(dvx * spd, dl) - me.x;
                    bny = D.y + rdiv(dvy * spd, dl) - me.y;
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

        // ---- 冲刺 ----
        if (free_now && me.cd == 0) {
            if (ne > 0) {
                const Player& E = g.p[eidx[0]];
                {
                    Cand c;
                    c.a0.act = A_DASH;
                    c.a0.ax = E.x - me.x;
                    c.a0.ay = E.y - me.y;
                    c.engage = true;
                    c.horizon = 18;
                    cands.push_back(c);
                }
                {
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
                Cand c;
                c.a0.act = A_DASH;
                c.a0.ax = mdst.x - me.x;
                c.a0.ay = mdst.y - me.y;
                c.mdx = mdst.x - me.x;
                c.mdy = mdst.y - me.y;
                c.horizon = 16;
                cands.push_back(c);
            }
            if (t > cf.cstart() - 140) {
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
            const i64 sc = rollout(g, cands[i], seat, t);
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
