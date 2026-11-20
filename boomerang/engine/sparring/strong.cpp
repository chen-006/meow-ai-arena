#include "dart.hpp"
#include <chrono>
#include <algorithm>
#include <vector>
#include <cstdlib>
#include <cstdio>
// pro_core.hpp —— 强档参考程序（第二版，原型专用，不进选手包）。
// 反应延迟下的做法：观察里别人是 DELAY 步以前的样子。我记得自己每一步的状态和动作，于是把"DELAY 步以前的世界"
// 完整地拼出来（别人 = 观察里的，我 = 我当时的状态），从那里开始推演：前 DELAY 步我按自己真实做过的动作走，
// 别人按各种假设走（含"在我还看不到的这几步里已经朝我扔了镖"），然后才轮到我现在要选的动作。
// 思路（都用 dart::Game 推演）：
//   1. 躲：我的每个候选（移动 / 冲刺），对"对手往几个方向动"和"对手在任意时刻朝我的轨迹完美预判扔过来、我看到后再闪"取最坏；
//   2. 打：扔镖前对对手的"反应集合"推演覆盖率。对手看到我蓄势要 DELAY+1 步，在硬直里还要更久；
//   3. 有镖时保持中距离绕着走，对手空手或硬直时压上去；我空手时拉开，安全时召回 / 捡镖；
//   4. 我的镖在回程时，在安全的移动里挑能让镖扫过对手的（带着镖拐弯）；
//   5. 近身能砍就砍，空手能踢就踢；冲刺用来躲、过缺口、捡镖。
// 旋钮：H（推演步数）、ndir、nreact、cover、predict（false = 把旧画面当成现在，最笨的做法）。



namespace pro {
using namespace dart;
// 冲刺前摇（新引擎才有 DASH_PRE；旧引擎当 0）
template <class C> auto dpre_(const C& c, int) -> decltype(c.dash_pre) { return c.dash_pre; }
template <class C> i64 dpre_(const C&, long) { return 0; }
inline i64 dpre(const Config& c) { return dpre_(c, 0); }

struct Params {
    int H = 16;            // 推演步数（从现在算）
    int ndir = 12;         // 移动方向数
    int nreact = 8;        // 对手反应的方向数（另加站着）
    int naim = 7;          // 瞄准个数
    int cover = 35;        // 扔镖要求的覆盖率（百分比）
    bool counter = true;   // 躲闪时考虑对手在任意时刻朝我扔
    bool steer = true;     // 带着回程镖拐弯
    bool punish = true;    // 抓硬直
    bool dash = true;      // 会冲刺
    bool predict = true;   // 用"DELAY 步以前的世界 + 我做过的动作"推演；false = 把观察当成现在
    int pref = 3500;       // 都有镖时想保持的距离
    bool team = true;      // 照顾队友
    bool throws = true;    // 会扔镖
    bool block = true;     // 规则开了格挡时会挡
    long long budget = 7000;  // 每步大约最多推演多少个 Game::step（超过就不再看新的候选；4v4 实测单步最多约 8 毫秒）
    bool intercept = true; // 躲不开时朝飞来的镖扔镖，把它撞下来（镖撞镖，第三版）
    bool passive = false;  // 只为了拦镖才扔（从不主动扔镖；对照用的"消极打法"）
    bool kickstun = true;  // 踢：把"踢中、对手进入被踢硬直"也算成功（之后捡镖补刀）
    int close_d = 0;       // 离对手不到这么远、覆盖率够 close_c 时，出手不看软风险（0 = 关）
    int close_c = 60;
    int mel_w = 0;         // 近身冲砍 / 踢的威胁，每种闪不掉的算几份风险（0 = 不考虑）
    int mel_far = 6000;    // 对手在这么近时才考虑近身威胁
    int near_dart = 3000;  // 地上的镖离主人这么近，算他马上又有镖
    int recall_far = 6500; // 拿着镖的对手在这么近时不开始召回
    bool nokick = false;   // 空手时不去踢拿着镖、没在硬直的对手（被踢落坑新规则下关掉：踢中能打断出手、硬直 12 步，推演判断安全就踢；对旧版 1v1 228:154）
    bool nokick_multi = true;  // 四人局、4v4 用的 nokick（多人局里冲上去踢有镖的人，会被第三个人收拾）
    bool kickstun_any = false;  // 空手踢：镖不在地上（飞着、召回中）时也把踢中算成功
    bool lateatk = true;   // 对手镖不在手里（飞着、地上）时，也假设他一拿回镖就朝我扔
    int counter_far = 4000; // 对手离我不到 RANGE + 这么远时，才推演"他朝我完美预判扔镖"
    int counter_far_multi = 1000;  // 四人局、4v4 用的（省推演量）
    int ffa_pct = 60;      // 四人局推演量（占 budget 的百分比）
    int team_pct = 25;     // 4v4 推演量
    int blkpen2 = 40;      // 认出他会格挡以后用的折扣
    int blkpen = 0;       // 对手能站着挡下这一镖时，覆盖率打几折（百分比扣减）
    bool selfblk = true;   // 躲闪推演里把"原地等着格挡"也当成一种躲法
    int push = 3000;      // 离对手比射程远、都有镖时，"被完美预判反扔的风险"每种扣多少分（越小越敢往前压；15000 = 第二版的保守）
};

struct Core {
    Params P;
    static const int HB = 64;
    Player myp[HB];
    Dart myd[HB];
    Action mya[HB];
    i64 seen_x[MAXP], seen_y[MAXP], seen_t[MAXP];
    bool inited = false;
    int sym = 0;  // 我的座位是基准座位经过哪种对称得到的（seat_sym 掩码）；候选方向按同样的对称排，程序整体对地图的对称不变
    i64 sa(i64 a) const {  // 把基准座位的方向角换成我的座位的
        if (sym & 1) a = 18000 - a;
        if (sym & 2) a = -a;
        return norm_angle(a);
    }
    explicit Core(const Params& p) : P(p) {}
    static i64 cosd(i64 a) { return Game::icos_face(a); }
    static i64 sind(i64 a) { return Game::isin_face(a); }
    static bool armed_soon(i64 st) { return st == D_HAND || st == D_BACK || st == D_CALL || st == D_RISE; }
    static bool committed(const Player& q) { return q.st != S_FREE && q.st != S_DEAD; }
    static bool dbg_on(i64 t) {
        static const char* e = std::getenv("PRO_DEBUG");
        return e && std::atoll(e) == t;
    }

    // ---- 推演用的世界
    const GameInfo* gi = nullptr;
    int me = 0, N = 0;       // N = 推演起点离现在几步
    Game base;               // 推演起点（现在 - N）
    bool have_now = false;   // 我现在手里有镖
    bool blocker[MAXP] = {}; // 这一局里看到过他挥砍、而且身边有别人的镖在飞（会格挡的对手）
    int pen_of(int k) const { return blocker[k] ? P.blkpen2 : P.blkpen; }
    mutable long long work = 0;  // 这一步已经推演了多少个 Game::step（用来按固定工作量收手，结果和机器快慢无关）
    long long bud = 0;           // 这一步的工作量上限（4v4 一步推演更贵，取 Params::budget 的六成）
    Action past[MAX_DELAY + 1];

    // 出手后 k 步镖飞了多远（去程变速）
    i64 flown_tab[64];
    void make_flown(const Config& cf) {
        Dart t;
        t.st = D_OUT;
        i64 acc = 0;
        flown_tab[0] = 0;
        for (int k = 1; k < 64; ++k) {
            t.age = k - 1;
            acc = imin(cf.range, acc + dart_step_len(cf, t));
            flown_tab[k] = acc;
        }
    }
    i64 flown(int k) const { return flown_tab[imin(63, imax(0, k))]; }

    struct Res { int my_death = -1; int foe_death = -1; int foe_stun = -1; bool mate_hit = false; bool invalid = false; };
    struct Plan {
        Action first;          // 现在这一步
        i64 mx = 0, my = 0;    // 之后的移动
        int hold = 8;          // 移动几步后停下（召回计划一直站着）
        int dodge_at = -1;     // 从现在起第几步改往 dodge 角度跑
        i64 dodge = 0;
        int then_at = -1;      // 冲刺突进：从现在起第几步朝目标 then_who（推演里他当时的位置）近战
        int then_who = -1;
        bool blk = false;      // 从 dodge_at 起原地等着，镖飞进挥砍范围就挥砍格挡
        int cs_who = -1;       // 从 dodge_at 起，这个人进了挥砍范围就先砍他
    };
    struct Opp {
        int who = -1;
        int react = 0;              // 从推演起点算，前 react 步保持速度
        int run = 8;                // 之后往 (ex, ey) 跑几步（然后停下）
        i64 ex = 0, ey = 0;
        int atk = -1;               // 从推演起点算，第几步朝我的轨迹完美预判扔镖（-1 不扔）
        const std::vector<i64>* tx = nullptr;
        const std::vector<i64>* ty = nullptr;
        bool blk = false;           // 从 react 起站着，我的镖飞进他的挥砍范围就挥砍格挡
        int mel = -1;               // 从推演起点算，第几步起冲上来近战（-1 不）
    };
    // 镖下一步大概到哪（格挡瞄准用）
    static void dart_next(const Config& cf, const Dart& D, i64& x, i64& y) {
        const i64 L = dart_step_len(cf, D);
        if (D.st == D_OUT) {
            x = D.x + D.vx * L / 1000;
            y = D.y + D.vy * L / 1000;
        } else {
            x = D.x + D.vx;
            y = D.y + D.vy;
        }
    }
    // k 号站在 g 里，有没有一把（别人的）镖下一步会进他的挥砍范围：有就填好格挡动作
    bool block_act(const Game& g, int k, int only, Action& a) const {
        const Config& cf = gi->cfg;
        const Player& Q = g.p[k];
        for (int j = 0; j < g.n; ++j) {
            if (j == k || (only >= 0 && j != only) || !dart_flying(g.d[j].st)) continue;
            i64 nx, ny;
            dart_next(cf, g.d[j], nx, ny);
            const i64 dd = ilen(nx - Q.x, ny - Q.y);
            if (dd <= cf.slash_reach - 150 && dd > 200) {
                a = Action();
                a.act = A_MELEE;
                a.ax = nx - Q.x;
                a.ay = ny - Q.y;
                return true;
            }
        }
        return false;
    }

    Action my_act(const Plan& pl, int s) const {  // s = 推演起点起的步数
        if (s < N) return past[s];
        const int q = s - N;
        Action a;
        if (q == 0) a = pl.first;
        else if (pl.first.act == A_RECALL) a.act = A_RECALL;
        if (q > 0 && q < pl.hold && pl.first.act != A_RECALL) {
            a.mx = pl.mx;
            a.my = pl.my;
        }
        if (pl.dodge_at >= 0 && q >= pl.dodge_at) {
            a = Action();
            if (!pl.blk) {
                a.mx = cosd(pl.dodge);
                a.my = sind(pl.dodge);
            }
        }
        return a;
    }

    Res roll(const Plan& pl, const Opp& op, int H) const {
        Game g = base;
        Action acts[MAXP];
        Res r;
        bool fired = false;  // 推演里的对手已经按假设扔过镖
        bool mate_alive[MAXP];
        for (int k = 0; k < g.n; ++k) mate_alive[k] = g.p[k].alive && k != me && g.p[k].team == g.p[me].team;
        const Config& cf = gi->cfg;
        for (int s = 0; s < N + H; ++s) {
            for (int k = 0; k < g.n; ++k) {
                Action& a = acts[k];
                a = Action();
                const Player& Q = g.p[k];
                if (!Q.alive) continue;
                if (k == me) {
                    a = my_act(pl, s);
                    if (pl.then_at >= 0 && s - N == pl.then_at && pl.then_who >= 0 && (g.p[me].st == S_FREE || (cf.dash_slash && g.p[me].st == S_DASH))) {
                        const Player& T = g.p[pl.then_who];
                        a = Action();
                        a.act = A_MELEE;
                        a.ax = T.x + T.vx - g.p[me].x;
                        a.ay = T.y + T.vy - g.p[me].y;
                    }
                    if (pl.blk && cf.block && s - N >= pl.dodge_at && g.p[me].st == S_FREE && g.d[me].st == D_HAND) block_act(g, me, -1, a);
                    if (pl.cs_who >= 0 && s - N >= pl.dodge_at && g.p[me].st == S_FREE && g.p[pl.cs_who].alive) {
                        const Player& T = g.p[pl.cs_who];
                        const i64 reach = g.d[me].st == D_HAND ? cf.slash_reach : cf.kick_reach;
                        if (ilen(T.x + T.vx - g.p[me].x, T.y + T.vy - g.p[me].y) <= reach - 100) {
                            a = Action();
                            a.act = A_MELEE;
                            a.ax = T.x + T.vx - g.p[me].x;
                            a.ay = T.y + T.vy - g.p[me].y;
                        }
                    }
                    continue;
                }
                if (k == op.who && op.mel >= 0 && s >= op.mel) {  // 冲上来近战
                    const Player& M = g.p[me];
                    const i64 dx = M.x + M.vx - Q.x, dy = M.y + M.vy - Q.y, dd = ilen(dx, dy);
                    const i64 reach = g.d[k].st == D_HAND ? cf.slash_reach : cf.kick_reach;
                    if ((Q.st == S_FREE || (cf.dash_slash && Q.st == S_DASH && g.dash_moving(Q))) && dd <= reach - 150) {
                        a.act = A_MELEE;
                        a.ax = dx;
                        a.ay = dy;
                    } else if (Q.st == S_FREE && Q.cd == 0 && dd > 1800 && dd < cf.dash_v * cf.dash_t + 1500) {
                        a.act = A_DASH;
                        a.ax = M.x + M.vx * (dpre(cf) + 2) - Q.x;
                        a.ay = M.y + M.vy * (dpre(cf) + 2) - Q.y;
                    } else {
                        a.mx = dx;
                        a.my = dy;
                    }
                    continue;
                }
                if (k == op.who && op.blk) {
                    if (s < op.react) { a.mx = Q.vx; a.my = Q.vy; }
                    else if (cf.block && Q.st == S_FREE && g.d[k].st == D_HAND) block_act(g, k, me, a);
                    continue;
                }
                if (k == op.who) {
                    if (s < op.react) {
                        a.mx = Q.vx;
                        a.my = Q.vy;
                    } else if (s < op.react + op.run) {
                        a.mx = op.ex;
                        a.my = op.ey;
                    }
                    if (op.atk >= 0 && s >= op.atk && !fired && (P.lateatk || s == op.atk) && Q.st == S_FREE && g.d[k].st == D_HAND) {
                        fired = true;
                        i64 px = g.p[me].x, py = g.p[me].y;
                        if (op.tx) {
                            const i64 pre = cf.throw_pre - 1;
                            for (int kk = 1; kk < 40; ++kk) {
                                const size_t at = size_t(imin(i64(op.tx->size()) - 1, s + pre + kk));
                                px = (*op.tx)[at];
                                py = (*op.ty)[at];
                                if (ilen(px - Q.x, py - Q.y) <= flown(kk)) break;
                            }
                        }
                        a.act = A_THROW;
                        a.ax = px - Q.x;
                        a.ay = py - Q.y;
                        a.mx = a.my = 0;
                    }
                    continue;
                }
                a.mx = Q.vx;
                a.my = Q.vy;
            }
            g.step(acts);
            ++work;
            if (!g.p[me].alive && r.my_death < 0) {
                if (s < N) {  // 这个假设和"我现在还活着"矛盾：作废
                    r.invalid = true;
                    return r;
                }
                r.my_death = s - N;
            }
            if (op.who >= 0 && !g.p[op.who].alive && r.foe_death < 0 && g.p[op.who].killer == me) r.foe_death = s - N;
            if (op.who >= 0 && r.foe_stun < 0 && g.p[op.who].alive && g.p[op.who].st == S_STUN && g.p[op.who].kicked_by == me) r.foe_stun = s - N;
            for (int k = 0; k < g.n; ++k)
                if (mate_alive[k] && !g.p[k].alive) r.mate_hit = true;
            if (r.my_death >= 0 || g.over) break;
        }
        return r;
    }

    // 我按计划走、没人干扰时的轨迹（下标 = 推演起点起的步数）；free_at = 从现在起第一次能重新行动的步
    void traj(const Plan& pl, int len, std::vector<i64>& tx, std::vector<i64>& ty, int& free_at) const {
        Game g = base;
        Action acts[MAXP];
        tx.assign(1, g.p[me].x);
        ty.assign(1, g.p[me].y);
        free_at = -1;
        for (int s = 0; s < N + len; ++s) {
            for (int k = 0; k < g.n; ++k) acts[k] = Action();
            acts[me] = my_act(pl, s);
            g.step(acts);
            ++work;
            tx.push_back(g.p[me].x);
            ty.push_back(g.p[me].y);
            if (s >= N && free_at < 0 && (g.p[me].st == S_FREE || g.p[me].st == S_RECALL)) free_at = s + 1 - N;
            if (g.over) break;
        }
        if (free_at < 0) free_at = len;
    }

    // 安全度：返回"对手只是走动"的各种假设下最早的死亡步（H+1 = 安全；含飞着的镖、掉坑）；
    // *soft = "对手在某个时刻朝我完美预判扔镖、我看到后再闪"的几种时刻里，闪不掉的有几种（风险，不是必死）
    int safety(const Plan& pl, const std::vector<int>& foes, bool use_counter, bool* mate_hit = nullptr, int* soft = nullptr) const {
        if (soft) *soft = 0;
        const int H = P.H;
        const Config& cf = gi->cfg;
        if (mate_hit) *mate_hit = false;
        int worst = H + 1;
        auto take = [&](const Res& r) {
            if (r.invalid) return;
            if (r.my_death >= 0) worst = imin(worst, r.my_death);
            if (r.mate_hit && mate_hit) *mate_hit = true;
        };
        if (foes.empty()) {
            take(roll(pl, Opp(), H));
            return worst;
        }
        std::vector<i64> tx, ty;
        int free_at = 1;
        bool have_traj = false;
        const Player& Me = base.p[me];
        for (int who : foes) {
            const Player& E = base.p[who];
            for (int i = -1; i < 4; ++i) {  // 对手往 4 个方向动或保持（决定他回程的镖怎么拐）
                Opp op;
                op.who = who;
                op.react = N;
                if (i >= 0) {
                    const i64 a = iatan2(Me.y - E.y, Me.x - E.x) + i * 9000;
                    op.ex = cosd(a);
                    op.ey = sind(a);
                } else {
                    op.ex = E.vx;
                    op.ey = E.vy;
                }
                take(roll(pl, op, H));
                if (worst == 0) return 0;
            }
            if (use_counter && P.counter && E.alive && (base.d[who].st == D_HAND || (P.lateatk && base.d[who].st != D_GONE)) && (E.st == S_FREE || E.timer <= N + 2) &&
                ilen(E.x - Me.x, E.y - Me.y) < cf.range + (cf.mode == M_DUEL ? P.counter_far : P.counter_far_multi)) {
                if (!have_traj) {
                    traj(pl, H + 40, tx, ty, free_at);
                    have_traj = true;
                }
                const i64 la = iatan2(Me.y - E.y, Me.x - E.x);
                for (int at = 0; at <= N + 6; at += 2) {
                    int bestd = -1;
                    const int ndd = (P.selfblk && cf.block && have_now) ? 4 : 3;
                    for (int dd = 0; dd < ndd; ++dd) {
                        Plan q = pl;
                        q.dodge = dd == 0 ? la + 9000 : dd == 1 ? la - 9000 : la;
                        q.blk = dd == 3;
                        q.dodge_at = imax(free_at, at + 1);  // 我在现在起第 at+1 步才看得到他出手
                        Opp op;
                        op.who = who;
                        op.react = 0;
                        op.ex = op.ey = 0;
                        op.atk = at;
                        op.tx = &tx;
                        op.ty = &ty;
                        const Res r = roll(q, op, H);
                        if (r.invalid) { bestd = H + 1; break; }
                        const int v = r.my_death < 0 ? H + 1 : r.my_death;
                        bestd = imax(bestd, v);
                        if (bestd > H) break;
                    }
                    if (bestd <= H && soft) ++*soft;
                }
            }
            // 近身威胁：他（从推演起点或 N 步后起）直接冲上来砍 / 踢（能冲刺就冲刺接近战），我看到后跑开或先砍他
            if (use_counter && P.mel_w > 0 && E.alive && E.st != S_STUN && ilen(E.x - Me.x, E.y - Me.y) < P.mel_far) {
                if (!have_traj) {
                    traj(pl, H + 40, tx, ty, free_at);
                    have_traj = true;
                }
                const i64 la = iatan2(Me.y - E.y, Me.x - E.x);
                for (int at = 0; at <= N; at += imax(1, N)) {
                    int bestd = -1;
                    const int ndd = (cf.block && have_now) ? 4 : 3;
                    for (int dd = 0; dd < ndd; ++dd) {
                        Plan q = pl;
                        q.dodge = la + (dd == 0 ? 0 : dd == 1 ? 6000 : -6000);
                        q.dodge_at = imax(free_at, at + 1);
                        q.cs_who = dd == 3 ? who : -1;
                        q.blk = dd == 3;  // 站着等他进挥砍范围
                        Opp op;
                        op.who = who;
                        op.react = 0;
                        op.ex = E.vx;
                        op.ey = E.vy;
                        op.run = at;
                        op.mel = at;
                        const Res r = roll(q, op, H);
                        if (r.invalid) { bestd = H + 1; break; }
                        const int v = r.my_death < 0 ? H + 1 : r.my_death;
                        bestd = imax(bestd, v);
                        if (bestd > H) break;
                    }
                    if (bestd <= H && soft) *soft += P.mel_w;
                }
            }
        }
        return worst;
    }

    // 出手的覆盖率：对手看到后（DELAY+1 步后，或硬直结束后）往 nreact 个方向跑或站着，有几成会被打死
    int coverage(const Plan& pl, int who, bool& mate_risk, bool stun_ok = false) const {
        const Player& E = base.p[who];
        int react = N + 1 + N;  // 从推演起点算：我现在出手，他在 N+1 步后的观察里才看到
        if (committed(E) && E.st != S_RECALL) react = imax(react, int(E.timer) + 1);
        int hit = 0, tot = 0;
        mate_risk = false;
        for (int i = -1; i < P.nreact; ++i) {
            Opp op;
            op.who = who;
            op.react = react;
            op.run = P.H + 8;  // 看到我出手后一直往那个方向躲（不会躲一下就停）
            if (i >= 0) {
                const i64 a = sa(i64(i) * FULL / P.nreact);
                op.ex = cosd(a);
                op.ey = sind(a);
            }
            const Res r = roll(pl, op, P.H + 8);
            ++tot;
            if ((r.foe_death >= 0 || (stun_ok && r.foe_stun >= 0)) && r.my_death < 0) ++hit;
            if (r.mate_hit) mate_risk = true;
        }
        int c = hit * 100 / tot;
        // 对手站着等、镖飞近了挥砍格挡：挡得住就打折扣
        if (c > 0 && pen_of(who) > 0 && gi->cfg.block && pl.first.act == A_THROW && base.d[who].st == D_HAND) {
            Opp op;
            op.who = who;
            op.react = react;
            op.blk = true;
            const Res r = roll(pl, op, P.H + 8);
            if (r.foe_death < 0) c = c * (100 - pen_of(who)) / 100;
        }
        return c;
    }

    // 会冲刺的寻路：格子上走（8 方向不切角）或者冲刺越过 1～2 格坑（直线 8 方向），每步代价 10 / 14 / 冲刺 30。
    // 返回下一个点（世界坐标）和是否要冲刺过去；走不到返回 false。
    bool route(const GameInfo& G, i64 x, i64 y, i64 gx, i64 gy, i64 t, i64& wx, i64& wy, bool& jump) const {
        const int n = G.M().nx;  // 格子编号 id = y * nx + x
        auto ok = [&](int a, int b) {
            if (!G.M().inside(a, b)) return false;
            const int c = cell_at_step(G.cfg, G.M(), a, b, t);
            return c == C_FLOOR || c == C_GRASS;
        };
        const int sx = cell_index(x), sy = cell_index(y), ex = cell_index(gx), ey = cell_index(gy);
        if (!ok(ex, ey)) return false;
        if (sx == ex && sy == ey) { wx = gx; wy = gy; jump = false; return true; }
        std::vector<int> dist(size_t(n) * G.M().ny, 1 << 29);
        std::vector<int> q;  // 简单的 Dijkstra（格子少，用桶也行，这里用排序队列）
        std::vector<std::pair<int, int>> heap;
        dist[size_t(ey) * n + ex] = 0;
        heap.push_back({0, ey * n + ex});
        auto relax = [&](int id, int nd) {
            if (nd < dist[size_t(id)]) {
                dist[size_t(id)] = nd;
                heap.push_back({-nd, id});
                std::push_heap(heap.begin(), heap.end());
            }
        };
        heap[0].first = 0;
        while (!heap.empty()) {
            std::pop_heap(heap.begin(), heap.end());
            const int dv = -heap.back().first, id = heap.back().second;
            heap.pop_back();
            if (dv != dist[size_t(id)]) continue;
            const int a = id % n, b = id / n;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    if (ok(a + dx, b + dy) && !(dx && dy && (!ok(a + dx, b) || !ok(a, b + dy)))) relax((b + dy) * n + a + dx, dv + ((dx && dy) ? 14 : 10));
                    for (int k = 2; k <= 1 + 2000 / CELL; ++k) {  // 冲刺越坑：中间全是坑
                        bool pit = true;
                        for (int j = 1; j < k; ++j)
                            if (cell_at_step(G.cfg, G.M(), a + dx * j, b + dy * j, t) != C_PIT) pit = false;
                        if (!pit) break;
                        if (ok(a + dx * k, b + dy * k)) relax((b + dy * k) * n + a + dx * k, dv + 30);
                    }
                }
        }
        if (!G.M().inside(sx, sy)) return false;
        int best = 1 << 29;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy) continue;
                for (int k = 1; k <= 1 + 2000 / CELL; ++k) {
                    const int a = sx + dx * k, b = sy + dy * k;
                    if (!ok(a, b)) {
                        if (k == 1 && cell_at_step(G.cfg, G.M(), a, b, t) != C_PIT) break;
                        continue;
                    }
                    if (k == 1 && dx && dy && (!ok(sx + dx, sy) || !ok(sx, sy + dy))) break;
                    if (k > 1) {
                        bool pit = true;
                        for (int j = 1; j < k; ++j)
                            if (cell_at_step(G.cfg, G.M(), sx + dx * j, sy + dy * j, t) != C_PIT) pit = false;
                        if (!pit) break;
                    }
                    const int c = dist[size_t(b) * n + a] + (k == 1 ? ((dx && dy) ? 14 : 10) : 30);
                    if (c < best) {
                        best = c;
                        wx = cell_center(a);
                        wy = cell_center(b);
                        jump = k > 1;
                    }
                    break;
                }
            }
        return best < (1 << 29);
    }

    Action decide(const Obs& o) {
        gi = &info();
        const Config& cf = gi->cfg;
        me = o.seat;
        const Player& Me = o.me();
        if (!inited) {
            for (int k = 0; k < MAXP; ++k) seen_t[k] = -1000;
            const int md = int(cf.mode);
            sym = seat_sym(md, me);
            make_flown(cf);
            inited = true;
        }
        myp[o.t % HB] = Me;
        myd[o.t % HB] = o.my_dart();
        work = 0;
        bud = cf.mode == M_TEAM ? P.budget * P.team_pct / 100 : cf.mode == M_FFA ? P.budget * P.ffa_pct / 100 : P.budget;
#ifdef STRONG_TIMING
        const auto c0 = std::chrono::steady_clock::now();
        Action out = think(o);
        std::fprintf(stderr, "T %lld %lld %lld\n", (long long)o.t, (long long)(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - c0).count() * 10), (long long)work);
#else
        Action out = think(o);
#endif
        if (std::getenv("PRO_WORKLOG")) std::fprintf(stderr, "W %lld %d %lld\n", o.t, me, work);
        if (o.me().alive) out.msg = 1 + cell_index(o.me().x) * 64 + cell_index(o.me().y);  // 告诉队友我现在在哪一格
        mya[o.t % HB] = out;
        return out;
    }

    Action think(const Obs& o) {
        const GameInfo& G = *gi;
        const Config& cf = G.cfg;
        const Player& Me = o.me();
        if (!Me.alive) return Action();
        for (int k = 0; k < o.n; ++k)
            if (o.p[k].vis) {
                seen_x[k] = o.p[k].x;
                seen_y[k] = o.p[k].y;
                seen_t[k] = o.t;
            }
        // —— 推演起点
        N = (P.predict && o.t >= cf.delay) ? int(cf.delay) : 0;
        base = Game(G, o);
        if (N > 0) {
            base.t = o.t - N;
            const int j = int((o.t - N) % HB);
            const i64 team = base.p[me].team;
            base.p[me] = myp[j];
            base.p[me].team = team;
            base.p[me].px = base.p[me].x;
            base.p[me].py = base.p[me].y;
            base.d[me] = myd[j];
            for (int s = 0; s < N; ++s) past[s] = mya[(o.t - N + s) % HB];
        }
        for (int k = 0; k < o.n; ++k)
            if (base.p[k].unknown && seen_t[k] > -1000 && o.t - seen_t[k] < 60) {  // 看不见的人：放在最后看到的位置
                base.p[k].unknown = 0;
                base.p[k].x = base.p[k].px = seen_x[k];
                base.p[k].y = base.p[k].py = seen_y[k];
                base.p[k].vx = base.p[k].vy = 0;
                base.p[k].st = S_FREE;
                if (base.d[k].st == D_HAND) {
                    base.d[k].x = seen_x[k];
                    base.d[k].y = seen_y[k];
                }
            }
        // 认出会格挡的对手（观察和 base 都是 N 步以前的同一刻）
        for (int k = 0; k < o.n; ++k) {
            if (k == me || base.p[k].team == base.p[me].team || !o.p[k].vis || base.p[k].st != S_SLASH) continue;
            for (int j = 0; j < o.n; ++j)
                if (j != k && dart_flying(base.d[j].st) && ilen(base.d[j].x - base.p[k].x, base.d[j].y - base.p[k].y) < 3000) blocker[k] = true;
        }
        // 对手现在大概在哪（按速度往前推 N 步）
        auto est = [&](int k, i64& x, i64& y) {
            x = base.p[k].x + base.p[k].vx * N;
            y = base.p[k].y + base.p[k].vy * N;
        };
        // 他手里有镖，或者马上能拿到（镖在飞回去、或落在他身边）
        auto foe_armed = [&](int k) {
            const Dart& D = base.d[k];
            if (armed_soon(D.st)) return true;
            return D.st == D_GROUND && ilen(D.x - base.p[k].x, D.y - base.p[k].y) < P.near_dart;
        };
        std::vector<std::pair<i64, int>> fl;
        for (int k = 0; k < o.n; ++k) {
            if (k == me || base.p[k].team == base.p[me].team || !base.p[k].alive || base.p[k].unknown) continue;
            i64 x, y;
            est(k, x, y);
            fl.push_back({ilen(x - Me.x, y - Me.y) * 16 + (k - me + o.n) % o.n, k});
        }
        std::sort(fl.begin(), fl.end());
        std::vector<int> foes;
        for (size_t i = 0; i < fl.size() && i < 2; ++i) foes.push_back(fl[i].second);
        const int tgt = foes.empty() ? -1 : foes[0];
        const Dart& md = o.my_dart();
        const bool ihave = md.st == D_HAND;
        have_now = ihave;
        const bool free = Me.st == S_FREE;
        // 队友现在在哪：用他上一步发来的格子（不延迟），没有就用观察里的旧位置
        std::vector<std::pair<i64, i64>> mates;
        for (int k = 0; k < o.n; ++k) {
            if (k == me || base.p[k].team != base.p[me].team || !o.p[k].alive) continue;
            if (o.p[k].msg > 0) mates.push_back({cell_center(int((o.p[k].msg - 1) / 64)), cell_center(int((o.p[k].msg - 1) % 64))});
            else mates.push_back({o.p[k].x, o.p[k].y});
        }
        // 沿方向 (ux, uy) 长 len 的线段附近 margin 内有没有队友
        auto mate_near_line = [&](i64 ax, i64 ay, i64 len, i64 margin) {
            i64 ux, uy;
            if (!scale_to(ax, ay, 1000, ux, uy)) return false;
            for (auto& q : mates) {
                const i64 rx = q.first - Me.x, ry = q.second - Me.y;
                i64 tp = (rx * ux + ry * uy) / 1000;
                tp = iclamp(tp, 0, len);
                const i64 px = Me.x + ux * tp / 1000, py = Me.y + uy * tp / 1000;
                if (ilen(q.first - px, q.second - py) < margin) return true;
            }
            return false;
        };

        // 格挡（规则开了才有）：对着飞过来的镖
        if (free && P.block && cf.block && ihave) {
            // 用推演把别人的镖推到"我现在挥砍、下一步判定"那一刻（含观察里还在蓄势、其实已经出手的镖）
            Game g = base;
            {
                Action acts[MAXP];
                for (int s = 0; s < N + int(cf.slash_pre) - 1; ++s) {
                    for (int k = 0; k < g.n; ++k) {
                        acts[k] = Action();
                        if (!g.p[k].alive) continue;
                        if (k == me) acts[k] = s < N ? past[s] : Action();
                        else { acts[k].mx = g.p[k].vx; acts[k].my = g.p[k].vy; }
                    }
                    g.step(acts);
                    ++work;
                    if (g.over) g.over = false;
                }
            }
            for (int k = 0; k < o.n; ++k) {
                if (k == me || !dart_flying(g.d[k].st)) continue;
                if (cf.mode == M_TEAM && base.p[k].team == base.p[me].team) continue;
                const i64 px = g.d[k].x, py = g.d[k].y;
                const i64 dd = ilen(px - Me.x, py - Me.y);
                if (dd > cf.slash_reach - 100 || dd < 150 || wall_between(G.M(), Me.x, Me.y, px, py)) continue;
                Plan pl;
                pl.first.act = A_MELEE;
                pl.first.ax = px - Me.x;
                pl.first.ay = py - Me.y;
                pl.hold = 0;
                if (safety(pl, foes, false) > P.H) return pl.first;
            }
        }

        // —— 2. 想去的方向
        const i64 cx = G.M().width() / 2, cy = G.M().height() / 2;  // 地图中心
        i64 gx = cx, gy = cy;
        bool want_recall = false, go_dart = false;
        const i64 fall = cell_fall_step(G, cell_index(Me.x), cell_index(Me.y));
        const bool hurry = fall >= 0 && fall - o.t < 50;
        if (tgt >= 0) {
            i64 ex, ey;
            est(tgt, ex, ey);
            const Player& E = base.p[tgt];
            const bool ehave = base.d[tgt].st == D_HAND;
            const i64 d = ilen(ex - Me.x, ey - Me.y);
            if (ihave) {
                if (!ehave || committed(E) || d > P.pref + 1500) {
                    gx = ex;
                    gy = ey;
                } else {
                    const i64 a = iatan2(Me.y - ey, Me.x - ex);
                    const i64 x1 = ex + cosd(a + 3000) * P.pref / 1000, y1 = ey + sind(a + 3000) * P.pref / 1000;
                    const i64 x2 = ex + cosd(a - 3000) * P.pref / 1000, y2 = ey + sind(a - 3000) * P.pref / 1000;
                    const bool s1 = standable(G, x1, y1, o.t + 20), s2 = standable(G, x2, y2, o.t + 20);
                    if (s1 != s2 ? s1 : ilen(x1 - cx, y1 - cy) <= ilen(x2 - cx, y2 - cy)) {
                        gx = x1;
                        gy = y1;
                    } else {
                        gx = x2;
                        gy = y2;
                    }
                }
            } else {
                if (md.st == D_GROUND) {
                    const i64 dd = ilen(md.x - Me.x, md.y - Me.y);
                    const bool reach = standable(G, md.x, md.y, o.t + 10);
                    if (reach && (dd < 2500 || ilen(md.x - ex, md.y - ey) > dd + 1500)) go_dart = true;
                    else want_recall = true;
                }
                if (go_dart) {
                    gx = md.x;
                    gy = md.y;
                } else {
                    gx = Me.x + (Me.x - ex) + (cx - Me.x) / 2;
                    gy = Me.y + (Me.y - ey) + (cy - Me.y) / 2;
                }
            }
        } else if (md.st == D_GROUND) {
            gx = md.x;
            gy = md.y;
        }
        // 4v4：同队四个人按座位错开（上下各偏一点），别全挤进同一个路口、互相推下坑
        if (cf.mode == M_TEAM && P.team && !hurry && !go_dart && !want_recall) {
            const i64 off = i64((me % 4) * 2 - 3) * 700 * ((sym & 2) ? -1 : 1);
            gy = iclamp(gy + off, 500, G.M().height() - 500);
        }
        if (hurry) {
            // 找最近的晚塌的格子
            i64 bestv = -1;
            for (int y = 0; y < G.M().ny; ++y)
                for (int x = 0; x < G.M().nx; ++x) {
                    const int c = G.M().at(x, y);
                    if (c == C_WALL || c == C_PIT) continue;
                    const i64 f = cell_fall_step(G, x, y);
                    const i64 left = f < 0 ? 100000 : f - o.t;
                    if (left < 60) continue;
                    const i64 v = imin(left, 400) * 100 - ilen(cell_center(x) - Me.x, cell_center(y) - Me.y) / 10;
                    if (v > bestv) {
                        bestv = v;
                        gx = cell_center(x);
                        gy = cell_center(y);
                    }
                }
        }
        i64 dx = 0, dy = 0;
        bool path_ok = false, need_jump = false;
        {
            i64 wx, wy;
            if (path_next(G, Me.x, Me.y, gx, gy, o.t, wx, wy)) {
                dx = wx - Me.x;
                dy = wy - Me.y;
                path_ok = true;
            } else if (route(G, Me.x, Me.y, gx, gy, o.t + 10, wx, wy, need_jump)) {
                dx = wx - Me.x;
                dy = wy - Me.y;
            } else {
                dx = gx - Me.x;
                dy = gy - Me.y;
            }
        }

        // —— 3. 召回
        // 有拿着镖（或镖正飞回去）的对手离得不远：不开始召回（站 12 步是活靶子）
        bool recall_danger = false;
        for (int who : foes) {
            i64 ex, ey;
            est(who, ex, ey);
            if (foe_armed(who) && ilen(ex - Me.x, ey - Me.y) < P.recall_far) recall_danger = true;
        }
        if (cf.recall && (Me.st == S_RECALL || (free && md.st == D_GROUND && want_recall && !recall_danger))) {
            Plan pl;
            pl.first.act = A_RECALL;
            bool mh = false;
            int sf = 0;
            if (safety(pl, foes, true, &mh, &sf) > P.H && sf <= 1 && !(mh && P.team)) return pl.first;
        }

        // —— 4. 移动 / 冲刺：安全第一，然后回程镖能扫到对手，然后接近想去的方向
        struct C { i64 mx, my; bool dash; };
        std::vector<C> cs;
        cs.push_back({dx, dy, false});
        for (int i = 0; i < P.ndir; ++i) {
            const i64 a = sa(i64(i) * FULL / P.ndir);
            cs.push_back({cosd(a), sind(a), false});
        }
        cs.push_back({0, 0, false});
        const bool can_dash = P.dash && free && Me.cd == 0;
        if (can_dash) {
            for (int i = 0; i < 8; ++i) {
                const i64 a = sa(i64(i) * 4500);
                cs.push_back({cosd(a), sind(a), true});
            }
            if (dx || dy) cs.push_back({dx, dy, true});
        }
        const bool steering = P.steer && tgt >= 0 && dart_flying(md.st);
        bool far = false;  // 离最近的对手比射程还远、两人都有镖：往前压（不然两个都谨慎的程序会隔着射程对站到塌陷）
        if (tgt >= 0 && ihave && base.d[tgt].st == D_HAND) {
            i64 ex, ey;
            est(tgt, ex, ey);
            far = ilen(ex - Me.x, ey - Me.y) > cf.range;
        }
        const i64 soft_w = far ? P.push : 15000;
        const i64 dash_len = cf.dash_v * cf.dash_t;
        i64 bests = -(1LL << 60);
        Action pick;
        int move_sv = 0, move_soft = 0;
        for (const C& c : cs) {
            if (work > bud * 6 / 10 && bests > -(1LL << 60)) break;  // 走法最多用六成工作量
            Plan pl;
            if (c.dash) {
                pl.first.act = A_DASH;
                pl.first.ax = c.mx;
                pl.first.ay = c.my;
                pl.first.mx = c.mx;
                pl.first.my = c.my;
                // 落点要站得住（冲刺不判落坑，但停下来那一刻要在地上）
                i64 ux, uy;
                scale_to(c.mx, c.my, dash_len, ux, uy);
                if (!standable(G, Me.x + ux, Me.y + uy, o.t + dpre(cf) + cf.dash_t + 4)) continue;
            } else {
                pl.first.mx = c.mx;
                pl.first.my = c.my;
            }
            pl.mx = c.mx;
            pl.my = c.my;
            pl.hold = 8;
            bool mh = false;
            int sf = 0;
            const int sv = safety(pl, foes, true, &mh, &sf);
            i64 sc = i64(sv) * 100000 - i64(sf) * soft_w;
            if (mh && P.team && dart_flying(md.st)) sc -= 250000;
            if (steering && sv > P.H && !c.dash) {
                int hits = 0;
                for (int i = -1; i < 4; ++i) {
                    Opp op;
                    op.who = tgt;
                    op.react = N + 1;
                    if (i >= 0) {
                        op.ex = cosd(sa(i * 9000));
                        op.ey = sind(sa(i * 9000));
                    }
                    const Res r = roll(pl, op, P.H);
                    if (r.foe_death >= 0 && r.my_death < 0) ++hits;
                }
                sc += hits * 3000;
            }
            // 别贴着队友走（会互相推、推下坑）：这条走法 0.3 秒后的位置离队友（消息里的格子）不到 1.2 米就扣分
            if (P.team && !mates.empty() && !c.dash && (c.mx || c.my)) {
                i64 ux, uy;
                scale_to(c.mx, c.my, cf.run_v * 6, ux, uy);
                for (auto& q : mates)
                    if (ilen(Me.x + ux - q.first, Me.y + uy - q.second) < 1200) sc -= 2000;
            }
            const i64 L1 = ilen(c.mx, c.my), L2 = ilen(dx, dy);
            i64 align = 0;
            if (L1 && L2) align = (c.mx * dx + c.my * dy) * 1000 / (L1 * L2);
            else if (!L1 && !L2) align = 1000;
            sc += far ? align * 3 : align;
            if (c.dash) {
                sc -= 2500;  // 冲刺有冷却：同样安全时不浪费
                // 跑不过去的地方（隔着坑）冲刺过去：想去的方向走不到、冲刺方向对得上时加分
                if (!path_ok && need_jump && align > 900) sc += 30000;  // 要越坑才能去想去的地方
                else if (!path_ok && align > 700) sc += 4000;
            } else if (L1) {
                const i64 px = Me.x + c.mx * 1500 / L1, py = Me.y + c.my * 1500 / L1;
                if (!standable(G, px, py, o.t + 20)) sc -= 1500;
            }
            if (dbg_on(o.t)) std::fprintf(stderr, "t %lld me %d cand mx %lld my %lld dash %d sv %d sc %lld\n", o.t, me, c.mx, c.my, int(c.dash), sv, sc);
            if (sc > bests) {
                bests = sc;
                pick = pl.first;
                move_sv = sv;
                move_soft = sf;
            }
        }
        // —— 5. 出手（在选好的走法之后：出手不能比它更危险）。DASH_SLASH=1 时冲刺中也能接近战
        const bool dashm = cf.dash_slash && Me.st == S_DASH;
        if (free || dashm) {
            int bestc = -1;
            Action bestA;
            for (int who : foes) {
                const Player& E = base.p[who];
                i64 ex, ey;
                est(who, ex, ey);
                const i64 d = ilen(ex - Me.x, ey - Me.y);
                // 我空手、他有镖而且没在硬直：不去踢他（冲上去等于送到他镖口）
                const bool nokick = ((cf.mode == M_FFA || cf.mode == M_TEAM) ? P.nokick_multi : P.nokick) && !ihave && foe_armed(who) && !committed(E);
                auto consider = [&](const Action& a, int need, bool stun_ok = false) {
                    if (nokick && a.act == A_MELEE) return;
                    if (work > bud) return;
                    if (P.team && !mates.empty()) {  // 镖的去程线上、挥砍范围里有队友：不出手
                        if (a.act == A_THROW && mate_near_line(a.ax, a.ay, cf.range, cf.body_r + cf.dart_r + 900)) return;
                        if (a.act == A_MELEE && mate_near_line(a.ax, a.ay, cf.slash_reach, 1300)) return;
                    }
                    Plan pl;
                    pl.first = a;
                    pl.hold = 0;
                    bool mr = false;
                    const int c = coverage(pl, who, mr, stun_ok);
                    if ((mr && P.team) || c < need || c <= bestc) return;
                    int sf = 0;
                    const int hv = safety(pl, foes, true, nullptr, &sf);
                    const bool close = d < P.close_d && c >= P.close_c;  // 贴得很近、命中率高：先下手为强，不管"被反扔"的风险
                    if (hv < imin(move_sv, P.H + 1) || (sf > move_soft + 1 && !close)) return;  // 出手不能比"不出手、挑最安全的走法"明显更危险
                    bestc = c;
                    bestA = a;
                };
                if (d < (ihave ? cf.slash_reach : cf.kick_reach) + (ihave ? 300 : 0)) {
                    Action a;
                    a.act = A_MELEE;
                    a.ax = ex + E.vx * (ihave ? cf.slash_pre - 1 : cf.kick_pre - 1) - Me.x;
                    a.ay = ey + E.vy * (ihave ? cf.slash_pre - 1 : cf.kick_pre - 1) - Me.y;
                    consider(a, 40, !ihave && P.kickstun && (md.st == D_GROUND || P.kickstun_any));  // 空手踢：踢中后去捡镖补刀
                }
                // 冲刺突进：朝他（预计的位置）冲刺，冲完立刻砍 / 踢（DASH_SLASH=1 时也试冲到一半就接近战）
                if (free && !nokick && work <= bud && P.dash && Me.cd == 0 && d > 1500 && d < cf.dash_v * cf.dash_t + 1800 && !wall_between(G.M(), Me.x, Me.y, ex, ey)) {
                    const i64 lead = dpre(cf) + cf.dash_t;
                    Action a;
                    a.act = A_DASH;
                    a.ax = ex + E.vx * lead - Me.x;
                    a.ay = ey + E.vy * lead - Me.y;
                    i64 ux, uy;
                    scale_to(a.ax, a.ay, cf.dash_v * cf.dash_t + 300, ux, uy);
                    if (standable(G, Me.x + ux, Me.y + uy, o.t + dpre(cf) + cf.dash_t + 2)) {
                        const int tend = int(dpre(cf) + cf.dash_t + cf.dash_post);
                        for (int ta = cf.dash_slash ? int(imax(1, dpre(cf))) : tend; ta <= tend && work <= bud; ++ta) {
                            Plan pl;
                            pl.first = a;
                            pl.hold = 0;
                            pl.then_at = ta;
                            pl.then_who = who;
                            bool mr = false;
                            const int c = coverage(pl, who, mr, !ihave && P.kickstun && (md.st == D_GROUND || P.kickstun_any));
                            if (!(mr && P.team) && c >= 45 && c > bestc) {
                                int sf = 0;
                                const int hv = safety(pl, foes, true, nullptr, &sf);
                                if (hv >= imin(move_sv, P.H + 1) && sf <= move_soft + 1) {
                                    bestc = c;
                                    bestA = a;
                                }
                            }
                        }
                    }
                }
                if (free && P.throws && !P.passive && ihave && d < cf.range + 1500 && !wall_between(G.M(), Me.x, Me.y, ex, ey)) {
                    const bool punish = P.punish && committed(E) && E.st != S_RECALL;
                    i64 lx = ex, ly = ey;
                    for (int k = 1; k <= 30; ++k) {
                        const i64 ts = cf.throw_pre - 1 + k;
                        const i64 px = ex + E.vx * ts, py = ey + E.vy * ts;
                        if (ilen(px - Me.x, py - Me.y) <= flown(k)) {
                            lx = px;
                            ly = py;
                            break;
                        }
                    }
                    const i64 ba = iatan2(ly - Me.y, lx - Me.x);
                    static const int offs[9] = {0, 400, -400, 900, -900, 1500, -1500, 2200, -2200};
                    for (int i = 0; i < P.naim && i < 9; ++i) {
                        Action a;
                        a.act = A_THROW;
                        a.ax = cosd(ba + offs[i]);
                        a.ay = sind(ba + offs[i]);
                        consider(a, punish ? 35 : P.cover);
                    }
                }
            }
            if (bestc >= 0) return bestA;
            // 拦镖：挑好的走法仍然躲不开（推演里会死）时，朝飞来的镖扔镖，看能不能把它撞下来（镖撞镖）
            if (free && P.intercept && cf.clash && ihave && move_sv <= P.H && work <= bud) {
                int bestv = move_sv;
                Action bestI;
                bool found = false;
                for (int k = 0; k < o.n; ++k) {
                    if (k == me || !dart_flying(base.d[k].st)) continue;
                    const Dart& D = base.d[k];
                    // 瞄镖现在大概的位置和它主人的方向
                    i64 tx = D.x, ty = D.y;
                    if (D.st == D_OUT) {
                        tx += D.vx * dart_step_len(cf, D) * (N + 2) / 1000;
                        ty += D.vy * dart_step_len(cf, D) * (N + 2) / 1000;
                    } else {
                        tx += D.vx * (N + 2);
                        ty += D.vy * (N + 2);
                    }
                    const i64 ba = iatan2(ty - Me.y, tx - Me.x);
                    static const int offs[5] = {0, 600, -600, 1400, -1400};
                    for (int i = 0; i < 5 && work <= bud; ++i) {
                        Plan pl;
                        pl.first.act = A_THROW;
                        pl.first.ax = cosd(ba + offs[i]);
                        pl.first.ay = sind(ba + offs[i]);
                        pl.hold = 0;
                        if (P.team && !mates.empty() && mate_near_line(pl.first.ax, pl.first.ay, cf.range, cf.body_r + cf.dart_r + 900)) continue;
                        {  // 只朝镖的主人那一侧拦（和他的方向差 45 度以内）：看上去是"回扔"，不会朝空地乱扔（观众会以为程序错了）
                            const i64 oa = iatan2(base.p[k].y - Me.y, base.p[k].x - Me.x);
                            if (iabs(norm_angle(ba + offs[i] - oa + 18000) - 18000) > 4500) continue;
                        }
                        const int v = safety(pl, foes, false);
                        if (v > bestv) {
                            bestv = v;
                            bestI = pl.first;
                            found = true;
                        }
                    }
                }
                if (found) return bestI;
            }
        }
        return pick;

    }
};

}  // namespace pro

// pro —— 强档参考程序的一个变体（见 pro_core.hpp）

struct Bot : pro::Core {
    static pro::Params mk() { pro::Params p;  return p; }
    Bot() : pro::Core(mk()) {}
};
DART_MAIN(Bot)
