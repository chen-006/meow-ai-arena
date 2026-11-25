// player.cpp —— 飞镖乱斗
// 思路：每步把观察（别人是 DELAY 步以前的样子）用引擎推到现在，得到若干个"现在"的局面
//（对手这 4 步里每种可能的做法各一个），然后对我的每个候选动作（移动方向、扔、砍、踢、冲、召回）
// 在每个局面里用 Game::step 往后推，取最坏情况的得分，选最好的。扔镖另外用一组"对手看到蓄势后的反应"
//（停、横跳、冲过来、挡镖、对扔）估计命中率。
#include "dart.hpp"
#include <algorithm>
#include <utility>
using namespace dart;

namespace {

enum { SC_KEEP = 0, SC_T0, SC_T3, SC_T6, SC_NOW, SC_DASH, SC_RUSH, SC_RECALL, SC_PICK, SC_BEND };
enum { R_KEEP = 0, R_STOP, R_PPP, R_PPM, R_TOWARD, R_AWAY, R_BLOCK, R_CTHROW, R_N };
enum { C_MOVE = 0, C_STOP, C_THROW, C_SLASH, C_KICK, C_DASH, C_RECALL };

struct Cand { int type; i64 mx = 0, my = 0, ax = 0, ay = 0; };
struct ES { i64 ix[MAXP], iy[MAXP]; bool done[MAXP], agg[MAXP], thr[MAXP]; };
struct Res { int dsm; int dse[MAXP]; bool stunned; };
struct MySnap { Player me; Dart md; Action act; };

const i64 BIG = (i64)1 << 60;
i64 CPU_BUDGET = 45000;  // 100ns 单位，约 4.5 毫秒
const i64 REACT_DELAY = 5;
    const i64 FAR_T = 3800;
const bool DEBUG = false;

struct Bot {
    std::vector<MySnap> myhist;
    std::vector<i64> fall;
    std::vector<int> esc, wnear;
    int nx = 0, ny = 0;
    int swho[64], stype[64];
    int ns = 0, nv = 0, sid[64];
    int me = 0;
    i64 now = 0, H = 10;
    i64 cpu0 = 0;
    bool timeup = false, ffa = false;
    i64 jx = 0, jy = 0;
    i64 cum[64];
    Player lastp[MAXP];
    bool seen[MAXP] = {};
    std::vector<Game> gs;
    std::vector<ES> es;
    std::shared_ptr<const Map> livemap0;
    i64 fallen0 = -1;
    Game work;

    void init(const GameInfo& gi) {
        const Map& M = gi.M();
        nx = M.nx;
        ny = M.ny;
        const int n = nx * ny;
        const Config& cf = gi.cfg;
        fall.assign(n, -1);
        wnear.assign(n, 99);
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) {
                const int id = y * nx + x;
                fall[id] = cell_fall_step(gi, x, y);
                if (M.at(x, y) == C_WALL) wnear[id] = 0;
            }
        std::vector<int> q;
        for (int id = 0; id < n; ++id)
            if (wnear[id] == 0) q.push_back(id);
        for (size_t i = 0; i < q.size(); ++i) {
            const int id = q[i], x = id % nx, y = id / nx;
            const int nb[4] = {x > 0 ? id - 1 : -1, x + 1 < nx ? id + 1 : -1, y > 0 ? id - nx : -1, y + 1 < ny ? id + nx : -1};
            for (int j = 0; j < 4; ++j)
                if (nb[j] >= 0 && wnear[nb[j]] > wnear[id] + 1) {
                    wnear[nb[j]] = wnear[id] + 1;
                    q.push_back(nb[j]);
                }
        }
        esc.assign(n, -1);
        q.clear();
        for (int id = 0; id < n; ++id)
            if (fall[id] < 0 && (M.c[id] == C_FLOOR || M.c[id] == C_GRASS)) {
                esc[id] = 0;
                q.push_back(id);
            }
        for (size_t i = 0; i < q.size(); ++i) {
            const int id = q[i], x = id % nx, y = id / nx;
            const int nb[4] = {x > 0 ? id - 1 : -1, x + 1 < nx ? id + 1 : -1, y > 0 ? id - nx : -1, y + 1 < ny ? id + nx : -1};
            for (int j = 0; j < 4; ++j)
                if (nb[j] >= 0 && esc[nb[j]] < 0 && (M.c[nb[j]] == C_FLOOR || M.c[nb[j]] == C_GRASS)) {
                    esc[nb[j]] = esc[id] + 10;
                    q.push_back(nb[j]);
                }
        }
        ffa = gi.cfg.mode == M_FFA;
        // 镖去程：第 j 步开始时已经飞了 cum[j]
        ncum = 0;
        i64 left = cf.range, age = 0, tot = 0;
        cum[ncum++] = 0;
        while (left > 0 && ncum < 60) {
            const i64 m = imin(imax(cf.dart_vmin, cf.dart_v - age * cf.dart_dec), left);
            tot += m;
            left -= m;
            ++age;
            cum[ncum++] = tot;
        }
    }
    int ncum = 0;

    bool enemy(int k) const { return k != me && !is_mate(info(), k); }

    void es_init(ES& e, const Game& g) {
        for (int k = 0; k < g.n; ++k) {
            e.ix[k] = e.iy[k] = 0;
            e.done[k] = e.agg[k] = e.thr[k] = false;
            const i64 L = ilen(g.p[k].vx, g.p[k].vy);
            if (L >= 60) scale_to(g.p[k].vx, g.p[k].vy, 1000, e.ix[k], e.iy[k]);
        }
    }

    // 冲刺移动中现在接近战，判定那一步能不能打到 tgt
    bool dash_slash_ok(Game& g, int k, int tgt) {
        const Player& P = g.p[k];
        const Player& T = g.p[tgt];
        if (!T.alive || T.unknown) return false;
        const i64 px = P.x + rdiv(P.ax * 750, UNIT), py = P.y + rdiv(P.ay * 750, UNIT);
        const i64 tx = T.x + T.vx, ty = T.y + T.vy;
        if (ilen(tx - px, ty - py) > 1350) return false;
        return !wall_between(g.LM(), px, py, tx, ty);
    }

    // 对手 k 在情景 s 下这一步的动作
    Action enemy_act(Game& g, int k, int s, ES& e, int h) {
        Action a;
        const Player& P = g.p[k];
        const Dart& D = g.d[k];
        const Player& M = g.p[me];
        int type = stype[s];
        const bool mine = swho[s] < 0 || swho[s] == k;
        if (!mine) type = SC_KEEP;
        const i64 dx = M.x - P.x, dy = M.y - P.y;
        const i64 dist = ilen(dx, dy);
        if (P.st == S_DASH && g.dash_moving(P)) {
            if (e.agg[k] && dash_slash_ok(g, k, me)) {
                a.act = A_MELEE;
                a.ax = M.x + M.vx - P.x;
                a.ay = M.y + M.vy - P.y;
            }
            return a;
        }
        if (P.st != S_FREE && P.st != S_THROW && P.st != S_THROWN) return a;
        const bool have = D.st == D_HAND;
        const bool canSee = dist < 7000 && !wall_between(g.LM(), P.x, P.y, M.x, M.y);
        if (P.st == S_FREE && mine) {
            if (!e.done[k]) {
                if ((type == SC_T0 || type == SC_T3 || type == SC_T6 || type == SC_NOW) && have && (type != SC_NOW || h >= 0)) {
                    e.done[k] = true;
                    e.agg[k] = type != SC_NOW;
                    e.thr[k] = true;
                    const i64 lead = type == SC_T0 ? 0 : 3;
                    a.act = A_THROW;
                    a.ax = M.x + M.vx * lead - P.x;
                    a.ay = M.y + M.vy * lead - P.y;
                    return a;
                }
                if (type == SC_DASH && P.cd == 0) {
                    e.done[k] = true;
                    e.agg[k] = true;
                    a.act = A_DASH;
                    a.ax = dx;
                    a.ay = dy;
                    return a;
                }
                if (type == SC_RECALL && D.st == D_GROUND && g.cf.recall) {
                    e.done[k] = true;
                    a.act = A_RECALL;
                    return a;
                }
                if (type == SC_PICK && D.st == D_GROUND) {
                    a.mx = D.x - P.x;
                    a.my = D.y - P.y;
                    return a;
                }
            }
            if (!e.thr[k] && have && (type == SC_RECALL || type == SC_PICK)) {
                e.thr[k] = true;
                e.agg[k] = true;
                if (canSee) {
                    a.act = A_THROW;
                    a.ax = M.x + M.vx * 3 - P.x;
                    a.ay = M.y + M.vy * 3 - P.y;
                    return a;
                }
            }
            if (type == SC_BEND) {
                if (have && !e.thr[k] && canSee) {
                    e.thr[k] = true;
                    e.agg[k] = true;
                    a.act = A_THROW;
                    a.ax = M.x + M.vx * 3 - P.x;
                    a.ay = M.y + M.vy * 3 - P.y;
                    return a;
                }
                if (dart_flying(D.st)) {  // 走位让回程的镖从我身上飞过
                    a.mx = M.x + (M.x - D.x) * 2 - P.x;
                    a.my = M.y + (M.y - D.y) * 2 - P.y;
                    return a;
                }
            }
            if (e.agg[k] || (type == SC_RUSH)) {
                e.agg[k] = true;
                if (dist <= 1400) {
                    a.act = A_MELEE;
                    a.ax = dx;
                    a.ay = dy;
                } else {
                    a.mx = dx;
                    a.my = dy;
                }
                return a;
            }
        }
        a.mx = e.ix[k];
        a.my = e.iy[k];
        return a;
    }

    // 我扔镖之后，对手 tgt 的反应（估计命中率用）
    Action react_act(Game& g, int k, int r, ES& e, int h, int tgt) {
        if (k != tgt) return enemy_act(g, k, 0, e, h);
        Action a;
        const Player& P = g.p[k];
        const Dart& D = g.d[k];
        const Player& M = g.p[me];
        if (P.st != S_FREE && P.st != S_THROW && P.st != S_THROWN) return a;
        const i64 dx = M.x - P.x, dy = M.y - P.y;
        const i64 dist = ilen(dx, dy);
        if (h >= REACT_DELAY && P.st == S_FREE) {
            switch (r) {
                case R_STOP: return a;
                case R_PPP:
                case R_PPM: {
                    i64 px = -dy, py = dx;
                    if (r == R_PPM) {
                        px = -px;
                        py = -py;
                    }
                    scale_to(px, py, 1000, a.mx, a.my);
                    return a;
                }
                case R_TOWARD:
                    a.mx = dx;
                    a.my = dy;
                    if (dist <= 1400) a.act = A_MELEE;
                    return a;
                case R_AWAY:
                    a.mx = -dx;
                    a.my = -dy;
                    return a;
                case R_BLOCK:
                    if (D.st == D_HAND) {
                        const Dart& MD = g.d[me];
                        if (dart_flying(MD.st)) {
                            a.act = A_MELEE;
                            a.ax = MD.x - P.x;
                            a.ay = MD.y - P.y;
                            return a;
                        }
                    }
                    break;
                case R_CTHROW:
                    if (D.st == D_HAND && !e.done[k]) {
                        e.done[k] = true;
                        a.act = A_THROW;
                        a.ax = M.x + M.vx * 3 - P.x;
                        a.ay = M.y + M.vy * 3 - P.y;
                        return a;
                    }
                    break;
                default: break;
            }
        }
        if (P.st == S_FREE) {
            a.mx = e.ix[k];
            a.my = e.iy[k];
        }
        return a;
    }

    Action my_act(Game& g, const Cand& c, int h) {
        Action a;
        const Player& M = g.p[me];
        switch (c.type) {
            case C_MOVE: a.mx = c.mx; a.my = c.my; break;
            case C_STOP: break;
            case C_THROW:
                a.mx = c.mx;
                a.my = c.my;
                if (h == 0) {
                    a.act = A_THROW;
                    a.ax = c.ax;
                    a.ay = c.ay;
                }
                break;
            case C_SLASH:
            case C_KICK:
                if (h == 0) {
                    a.act = A_MELEE;
                    a.ax = c.ax;
                    a.ay = c.ay;
                }
                break;
            case C_RECALL:
                if (h == 0) a.act = A_RECALL;
                break;
            case C_DASH:
                if (h == 0) {
                    a.act = A_DASH;
                    a.ax = c.ax;
                    a.ay = c.ay;
                } else if (M.st == S_DASH && g.dash_moving(M)) {
                    for (int k = 0; k < g.n; ++k)
                        if (enemy(k) && dash_slash_ok(g, me, k)) {
                            a.act = A_MELEE;
                            a.ax = g.p[k].x - M.x;
                            a.ay = g.p[k].y - M.y;
                            break;
                        }
                }
                break;
            default: break;
        }
        return a;
    }

    i64 collapse_pen(i64 x, i64 y, i64 t) {
        const int cx = cell_index(x), cy = cell_index(y);
        if (cx < 0 || cy < 0 || cx >= nx || cy >= ny) return 60;
        const int id = cy * nx + cx;
        const i64 f = fall[id];
        if (f < 0) return 0;
        const i64 rem = f - t;
        const i64 ec = esc[id] < 0 ? 0 : esc[id];
        const i64 need = ec * 26 / 100 + 4;
        i64 v = 0;
        if (need > rem) v += (need - rem) * 60;
        if (need + 40 > rem) v += (need + 40 - rem) * 4;
        return v;
    }

    i64 posval(Game& g, const Cand& c) {
        const Player& M = g.p[me];
        const Dart& D = g.d[me];
        i64 v = 0;
        const bool iHave = D.st == D_HAND;
        i64 dh = BIG, de = BIG, ds = BIG;
        int nh = 0;
        for (int k = 0; k < g.n; ++k) {
            if (!enemy(k) || !g.p[k].alive) continue;
            i64 ex, ey;
            const i64 dst = g.d[k].st;
            if (g.p[k].unknown) {
                if (!seen[k] || dst != D_HAND) continue;
                ex = lastp[k].x;
                ey = lastp[k].y;
            } else {
                ex = g.p[k].x;
                ey = g.p[k].y;
            }
            const i64 d = ilen(ex - M.x, ey - M.y);
            if (dst == D_HAND) {
                dh = imin(dh, d);
                ++nh;
            } else {
                de = imin(de, d);
                if (dst == D_BACK || dst == D_CALL || dst == D_RISE) ds = imin(ds, d);
            }
        }
        // 持镖对手：太近的话，他 4 步前扔出的镖现在已经到眼前了，躲不开
        if (dh < BIG) {
            if (dh < 4000) v -= (4000 - dh) * 4;
            const i64 lim = iHave ? 4800 : 6200;
            if (dh < lim) v -= (lim - dh) * (iHave ? 5 : 8) / 10;
        }
        if (ds < BIG && ds < 4500 && !iHave) v -= (4500 - ds) * 5 / 10;
        // 对手落地的镖就在旁边：他一召回，起飞那一步就打人
        for (int k = 0; k < g.n; ++k) {
            if (!enemy(k) || !g.p[k].alive || g.p[k].unknown) continue;
            if (g.d[k].st != D_GROUND && g.d[k].st != D_RISE) continue;
            const i64 dd = ilen(g.d[k].x - M.x, g.d[k].y - M.y);
            if (dd < 1400) v -= (1400 - dd) * (g.p[k].st == S_FREE || g.d[k].st == D_RISE ? 3 : 1);
        }
        if (iHave && de < BIG && de < 6000) v += (6000 - de) * 3 / 10;
        if (!iHave) {
            if (D.st == D_GROUND) v -= ilen(D.x - M.x, D.y - M.y) * 5 / 10;
            else if (D.st == D_BACK) v -= ilen(D.x - M.x, D.y - M.y) * 3 / 10;
            else if (D.st == D_CALL) v -= ilen(D.x - M.x, D.y - M.y) * 15 / 100;
        }
        if (M.st != S_FREE && M.st != S_THROW && M.st != S_THROWN) v -= 200;
        for (int k = 0; k < g.n; ++k)
            if (enemy(k) && g.p[k].alive && g.p[k].st == S_STUN) v += 600;
        v -= collapse_pen(M.x, M.y, g.t);
        {
            const int cx = cell_index(M.x), cy = cell_index(M.y);
            if (cx >= 0 && cy >= 0 && cx < nx && cy < ny && wnear[cy * nx + cx] <= 1) v -= (dh < 7000) ? 300 : 100;
        }
        if (c.type == C_MOVE && nh > 0 && dh < 7500) v += (c.mx * jx + c.my * jy) / 2000;
        return v;
    }

    // 一局推演的结果折成分：死 / 杀 / 被踢
    i64 term(const Res& r) {
        i64 kills = 0, dead_before = 0;
        for (int k = 0; k < work.n; ++k) {
            if (k == me) continue;
            if (r.dse[k] >= 0) {
                ++dead_before;
                if (enemy(k)) kills += (ffa ? 8000 : 30000) + (H - r.dse[k]) * 200;
            } else if (r.dse[k] == -2) {
                ++dead_before;
            }
        }
        if (r.dsm >= 0) {
            bool mutual = false;
            for (int k = 0; k < work.n; ++k)
                if (enemy(k) && r.dse[k] >= 0 && r.dse[k] <= r.dsm) mutual = true;
            i64 v = mutual ? (ffa ? -15000 : -2000) : -30000;
            if (ffa) v += dead_before * 6000 + work.p[me].kills * 8000;
            return v + r.dsm * 600;
        }
        if (r.stunned) kills -= 3000;
        return kills;
    }

    void reset_work(const Game& src) {
        work.cf = src.cf;
        work.map = src.map;
        work.seed = src.seed;
        work.n = src.n;
        for (int k = 0; k < MAXP; ++k) {
            work.p[k] = src.p[k];
            work.d[k] = src.d[k];
        }
        work.t = src.t;
        work.over = false;
        work.winner = -2;
        work.live = livemap0;
        work.live_fallen = fallen0;
    }

    // 从"现在"的局面往后推 Hh 步；react >= 0 时对手 tgt 按反应 r 行动
    Res rollout(int i, int s, const Cand& c, i64 Hh, int react = -1, int tgt = -1) {
        reset_work(gs[i]);
        ES e = es[i];
        Res r;
        r.dsm = -1;
        r.stunned = false;
        for (int k = 0; k < work.n; ++k) r.dse[k] = work.p[k].alive ? -1 : -2;
        Action acts[MAXP];
        for (i64 h = 0; h < Hh; ++h) {
            for (int k = 0; k < work.n; ++k) {
                if (k == me) acts[k] = my_act(work, c, h);
                else if (react >= 0) acts[k] = react_act(work, k, react, e, h, tgt);
                else acts[k] = enemy_act(work, k, s, e, h);
            }
            work.step(acts);
            if (r.dsm < 0 && !work.p[me].alive) r.dsm = h;
            for (int k = 0; k < work.n; ++k)
                if (k != me && r.dse[k] == -1 && !work.p[k].alive) r.dse[k] = h;
            if (work.p[me].st == S_STUN) r.stunned = true;
            if (work.over) break;
        }
        return r;
    }

    bool over_budget() {
        if (timeup) return true;
        const i64 c = thread_cpu_100ns();
        if (cpu0 < 0 || c < 0) return false;
        if (c - cpu0 >= CPU_BUDGET) timeup = true;
        return timeup;
    }

    // 情景 0（都照旧）按确定算，其余假想情景各按 1/4 算
    i64 eval_main(const Cand& c, i64 Hh) {
        i64 v = 0;
        for (int i = 0; i < nv; ++i) {
            const Res r = rollout(i, sid[i], c, Hh);
            if (i == 0) v = term(r) + posval(work, c);
            else v += term(r) / 4;
        }
        return v;
    }

    // 对手 k 在"都照旧"下未来 HB 步每一步开始时的位置
    void enemy_traj(int k, i64* tx, i64* ty, int HB) {
        reset_work(gs[0]);
        ES e = es[0];
        Cand stop;
        stop.type = C_STOP;
        Action acts[MAXP];
        tx[0] = work.p[k].x;
        ty[0] = work.p[k].y;
        for (int h = 0; h < HB; ++h) {
            for (int j = 0; j < work.n; ++j) acts[j] = (j == me) ? my_act(work, stop, h) : enemy_act(work, j, 0, e, h);
            work.step(acts);
            tx[h + 1] = work.p[k].x;
            ty[h + 1] = work.p[k].y;
            if (!work.p[k].alive || work.over) {
                for (int r = h + 2; r <= HB; ++r) {
                    tx[r] = tx[h + 1];
                    ty[r] = ty[h + 1];
                }
                break;
            }
        }
    }

    // 扔镖候选 c（打 k）：假想情景里的风险 + 对手各种反应下的期望（打中 = 赢，打空 = 空手期挨打）
    i64 eval_throw(const Cand& c, int k, i64 Hh) {
        i64 v = 0;
        for (int i = 1; i < nv; ++i) v += term(rollout(i, sid[i], c, Hh)) / 4;
        i64 sum = 0;
        int n = 0;
        const bool eHave = gs[0].d[k].st == D_HAND;
        const i64 miss = eHave ? (ffa ? 3500 : 8000) : (ffa ? 1500 : 3500);
        for (int r = 0; r < R_N; ++r) {
            if ((r == R_BLOCK || r == R_CTHROW) && !eHave) continue;
            ++n;
            const Res rr = rollout(0, 0, c, Hh, r, k);
            i64 vr = term(rr);
            if (rr.dsm < 0 && rr.dse[k] < 0) vr = -miss + posval(work, c);
            sum += vr;
        }
        return v + sum / imax(1, n);
    }

    void gen_melee(std::vector<Cand>& out, bool have) {
        const Game& g0 = gs[0];
        const Player& M0 = g0.p[me];
        const Config& cf = g0.cf;
        for (int k = 0; k < g0.n; ++k) {
            if (!enemy(k) || !g0.p[k].alive || g0.p[k].unknown) continue;
            const Player& P = g0.p[k];
            const i64 px = P.x + P.vx, py = P.y + P.vy;
            const i64 d = ilen(px - M0.x, py - M0.y);
            if (d <= 1350) {
                Cand c;
                c.type = have ? C_SLASH : C_KICK;
                c.ax = px - M0.x;
                c.ay = py - M0.y;
                out.push_back(c);
            }
            const Dart& D = g0.d[k];
            if (!have || !dart_flying(D.st)) continue;
            const i64 dd = ilen(D.x - M0.x, D.y - M0.y);
            if (dd > 2600 || dd < 600) continue;
            // 这一步飞完之后镖在哪
            i64 nx1 = D.x, ny1 = D.y;
            const i64 spd = dart_step_len(cf, D);
            if (D.st == D_OUT) {
                nx1 += rdiv(D.vx * imin(spd, D.left), UNIT);
                ny1 += rdiv(D.vy * imin(spd, D.left), UNIT);
            } else {
                nx1 += rdiv((M0.x - D.x) * spd, imax((i64)1, dd));
                ny1 += rdiv((M0.y - D.y) * spd, imax((i64)1, dd));
            }
            const i64 d1 = ilen(nx1 - M0.x, ny1 - M0.y);
            if (d1 <= 1500) {
                if (!wall_between(g0.M(), nx1, ny1, M0.x, M0.y)) {
                    Cand c;
                    c.type = C_SLASH;
                    c.ax = nx1 - M0.x;
                    c.ay = ny1 - M0.y;
                    out.push_back(c);
                }
            }
        }
    }

    Action decide(const Obs& o) {
        const GameInfo& gi = info();
        cpu0 = thread_cpu_100ns();
        timeup = false;
        me = o.seat;
        now = o.t;
        Action out;
        while ((i64)myhist.size() <= o.t) myhist.push_back(MySnap());
        myhist[o.t].me = o.me();
        myhist[o.t].md = o.my_dart();
        const Player& M0 = o.me();
        for (int k = 0; k < o.n; ++k)
            if (k != me && o.p[k].vis && o.p[k].alive) {
                lastp[k] = o.p[k];
                seen[k] = true;
            }
        if (!M0.alive) return out;
        if (M0.st == S_SLASH || M0.st == S_KICK || M0.st == S_SLASHED || M0.st == S_KICKED || M0.st == S_STUN) return out;

        // 变向的方向（每 3 步换一次），让对手的提前量算不准
        {
            const u64 hsh = (u64(o.t / 3) * 0x9E3779B97F4A7C15ULL + 0x1234567ULL) * 0xBF58476D1CE4E5B9ULL;
            const i64 ang = i64((hsh >> 33) % 36000);
            jx = Game::isin_raw(ang + 9000);
            jy = Game::isin_raw(ang);
        }

        Game base(gi, o);
        const i64 t0 = imax(0, o.t - gi.cfg.delay);
        base.t = t0;
        if (t0 < (i64)myhist.size()) {
            base.p[me] = myhist[t0].me;
            base.d[me] = myhist[t0].md;
            base.p[me].px = base.p[me].x;
            base.p[me].py = base.p[me].y;
        }
        // 看不见的对手：按最后看见的位置放回推演
        for (int k = 0; k < o.n; ++k) {
            if (k == me || !o.p[k].alive || o.p[k].vis || !seen[k]) continue;
            Player& P = base.p[k];
            P = lastp[k];
            P.px = P.x;
            P.py = P.y;
            P.st = S_FREE;
            P.timer = 0;
            P.unknown = 0;
            base.d[k] = o.d[k];
            if (base.d[k].st == D_HAND) {
                base.d[k].x = P.x;
                base.d[k].y = P.y;
            }
        }

        // 情景表
        ns = 0;
        swho[ns] = -1;
        stype[ns] = SC_KEEP;
        ++ns;
        const bool team = gi.cfg.mode == M_TEAM;
        const int scmax = team ? 9 : (ffa ? 14 : 12);
        std::vector<int> sc;
        for (int k = 0; k < o.n; ++k) {
            if (k == me || !base.p[k].alive || base.p[k].unknown) continue;
            if (team && !enemy(k)) continue;
            const i64 dk = ilen(base.p[k].x - M0.x, base.p[k].y - M0.y);
            const i64 ds = base.d[k].st;
            sc.clear();
            if (team) {
                if (dk < 7500) sc.push_back(SC_T0);
            } else {
                if (ds == D_HAND || ds == D_BACK || ds == D_CALL) {
                    sc.push_back(SC_T0);
                    if (ffa) sc.push_back(SC_T6);
                    else {
                        sc.push_back(SC_T6);
                        sc.push_back(SC_NOW);
                    }
                }
                if (ds == D_GROUND || ds == D_RISE) sc.push_back(SC_RECALL);
                if (!ffa && ds != D_HAND) sc.push_back(SC_PICK);
                if (dart_flying(ds)) sc.push_back(SC_BEND);
                if (!ffa || dk <= 4500) sc.push_back(SC_DASH);
                if (!ffa || dk <= 2600) sc.push_back(SC_RUSH);
            }
            for (size_t j = 0; j < sc.size() && ns < scmax; ++j) {
                swho[ns] = k;
                stype[ns] = sc[j];
                ++ns;
            }
        }

        // 推到现在
        gs.clear();
        es.clear();
        nv = 0;
        for (int s = 0; s < ns; ++s) {
            Game g = base;
            ES e;
            es_init(e, g);
            bool ok = true;
            for (i64 step = t0; step < now; ++step) {
                Action acts[MAXP];
                for (int k = 0; k < g.n; ++k) acts[k] = (k == me && step < (i64)myhist.size()) ? myhist[step].act : enemy_act(g, k, s, e, -1);
                g.step(acts);
                if (!g.p[me].alive || g.over) {
                    ok = false;
                    break;
                }
            }
            if (s == 0 && !ok) {
                g = Game(gi, o);
                es_init(e, g);
                ok = true;
            }
            if (!ok) continue;
            gs.push_back(g);
            es.push_back(e);
            sid[nv] = s;
            ++nv;
        }
        if (nv == 0) return out;
        {
            i64 fallen, warn;
            collapse_state(gi.cfg, gi.M(), now, fallen, warn);
            if (!livemap0 || fallen != fallen0) {
                fallen0 = fallen;
                livemap0 = std::make_shared<const Map>(fallen == 0 ? gi.M() : live_map(gi.cfg, gi.M(), now));
            }
        }

        const i64 Hm = (ffa || gi.cfg.mode == M_TEAM) ? 10 : 12;
        H = Hm;
        Cand best;
        best.type = C_STOP;
        i64 bestv = eval_main(best, Hm);
        const Dart& MD0 = o.my_dart();
        const bool iHave = MD0.st == D_HAND;

        // 冲刺中：接着冲（够得着就接挥砍），或者现在就接近战
        if (M0.st == S_DASH) {
            Cand cont;
            cont.type = C_DASH;
            bestv = eval_main(cont, Hm);
            best = cont;
            if (M0.timer > gi.cfg.dash_post && M0.timer <= gi.cfg.dash_t + gi.cfg.dash_post) {
                std::vector<Cand> ml;
                gen_melee(ml, iHave);
                for (size_t i = 0; i < ml.size(); ++i) {
                    const i64 v = eval_main(ml[i], Hm);
                    if (v > bestv) {
                        bestv = v;
                        best = ml[i];
                    }
                }
            }
            out = my_act(gs[0], best, 0);
            myhist[o.t].act = out;
            return out;
        }

        // 移动
        if (M0.st == S_FREE || M0.st == S_RECALL) {
            const int ND = (o.n > 4) ? 8 : 16;
            for (int d = 0; d < ND; ++d) {
                if (over_budget()) break;
                Cand c;
                c.type = C_MOVE;
                const i64 ang = i64(d) * 36000 / ND;
                c.mx = Game::isin_raw(ang + 9000);
                c.my = Game::isin_raw(ang);
                const i64 v = eval_main(c, Hm);
                if (v > bestv) {
                    bestv = v;
                    best = c;
                }
            }
        }

        if (M0.st == S_FREE) {
            // 近战 / 挡镖
            std::vector<Cand> ml;
            gen_melee(ml, iHave);
            for (size_t i = 0; i < ml.size() && !over_budget(); ++i) {
                const i64 v = eval_main(ml[i], Hm);
                if (v > bestv) {
                    bestv = v;
                    best = ml[i];
                }
            }
            // 扔镖
            if (iHave && !over_budget()) {
                Cand mv;
                if (best.type == C_MOVE) {
                    mv.mx = best.mx;
                    mv.my = best.my;
                }
                int tried = 0;
                i64 tx[20], ty[20];
                for (int k = 0; k < o.n && tried < (ffa ? 2 : 1) && !over_budget(); ++k) {
                    if (k == me) continue;
                    const Player& P = gs[0].p[k];
                    if (!P.alive || P.unknown) continue;
                    const i64 d = ilen(P.x - M0.x, P.y - M0.y);
                    if (d > 6800 || wall_between(gi.M(), M0.x, M0.y, P.x, P.y)) continue;
                    // 回放统计：4.5 米以外对自由的人几乎打不中（被挡、被躲、和回程镖相撞）
                    if (d > FAR_T && P.st == S_FREE) continue;
                    ++tried;
                    const int HB = 14;
                    enemy_traj(k, tx, ty, HB);
                    const i64 rx = M0.x + mv.mx / 4, ry = M0.y + mv.my / 4;
                    int bj = -1;
                    i64 bm = BIG;
                    for (int j = 2; j <= HB && j + 1 < ncum; ++j) {
                        const i64 dj = ilen(tx[j] - rx, ty[j] - ry);
                        i64 m = 0;
                        if (dj < cum[j - 2] - 720) m = cum[j - 2] - 720 - dj;
                        else if (dj > cum[j - 1] + 720) m = dj - cum[j - 1] - 720;
                        if (m < bm) {
                            bm = m;
                            bj = j;
                        }
                    }
                    std::vector<std::pair<i64, i64>> aims;
                    if (bj > 0) {
                        for (int j = imax(0, bj - 1); j <= imin(HB, bj + 1); ++j) aims.push_back({tx[j], ty[j]});
                    }
                    aims.push_back({P.x, P.y});
                    std::vector<char> dup(aims.size(), 0);
                    for (size_t i = 0; i < aims.size(); ++i)
                        for (size_t j = 0; j < i; ++j)
                            if (aims[i].first == aims[j].first && aims[i].second == aims[j].second) dup[i] = 1;
                    for (size_t i = 0; i < aims.size() && !over_budget(); ++i) {
                        if (dup[i]) continue;
                        Cand c;
                        c.type = C_THROW;
                        c.mx = mv.mx;
                        c.my = mv.my;
                        c.ax = aims[i].first - rx;
                        c.ay = aims[i].second - ry;
                        const i64 v = eval_throw(c, k, 12);
                        if (DEBUG) std::cerr << " t=" << o.t << " aim " << i << " bj=" << bj << " bm=" << bm << " v=" << v << " best=" << bestv << "\n";
                        if (v > bestv) {
                            bestv = v;
                            best = c;
                        }
                    }
                }
            }
            // 冲刺（只在有用的时候：去捡镖、逃离要塌的地方）
            if (M0.cd == 0 && !over_budget()) {
                const i64 pen = collapse_pen(M0.x, M0.y, o.t);
                i64 dhave = BIG;
                for (int k = 0; k < o.n; ++k)
                    if (k != me && o.p[k].alive && o.d[k].st == D_HAND && o.p[k].vis) dhave = imin(dhave, ilen(o.p[k].x - M0.x, o.p[k].y - M0.y));
                bool fly = false;
                for (int k = 0; k < o.n; ++k)
                    if (k != me && o.d[k].st != D_HAND && o.d[k].st != D_GONE && o.d[k].st != D_GROUND) fly = true;
                const bool want = (pen > 200) || (MD0.st == D_GROUND && ilen(MD0.x - M0.x, MD0.y - M0.y) > 1500 && dhave > 7500 && !fly);
                if (want) {
                    for (int d = 0; d < 8; ++d) {
                        if (over_budget()) break;
                        Cand c;
                        c.type = C_DASH;
                        const i64 ang = i64(d) * 36000 / 8 + 2250;
                        c.ax = Game::isin_raw(ang + 9000);
                        c.ay = Game::isin_raw(ang);
                        const i64 v = eval_main(c, Hm + 6) - 500;
                        if (v > bestv) {
                            bestv = v;
                            best = c;
                        }
                    }
                }
            }
            // 召回
            if (MD0.st == D_GROUND && gi.cfg.recall && !over_budget()) {
                Cand c;
                c.type = C_RECALL;
                const i64 v = eval_main(c, 16) - 200;
                if (v > bestv) {
                    bestv = v;
                    best = c;
                }
            }
        }
        out = my_act(gs[0], best, 0);
        myhist[o.t].act = out;
        if (DEBUG) std::cerr << "t=" << o.t << " type=" << best.type << " v=" << bestv << " nv=" << nv << " cpu=" << (thread_cpu_100ns() - cpu0) / 100 << "us\n";
        return out;
    }
};

}  // namespace

DART_MAIN(Bot)
