// 飞镖乱斗 bot：延迟补偿重建局面 + 全引擎候选动作 rollout 搜索
#include "dart.hpp"
using namespace dart;

static constexpr int RING = 8;
static constexpr int MAXC = 110;

struct Cand {
    Action a;
    i64 prio = 0;
};

struct Bot {
    // 历史：自己每步的精确状态与动作（用于把 t-4 局面推到现在）
    Player hp[RING];
    Dart hd[RING];
    Action ha[RING];
    // 隐形对手的最后目击
    i64 lsx[MAXP], lsy[MAXP], lsvx[MAXP], lsvy[MAXP], lst[MAXP];
    bool seen[MAXP];
    // 静态地图信息
    std::vector<i64> fall;      // 每格塌陷步（-1 永不）
    int nx = 0, ny = 0;
    i64 dirx[16], diry[16];
    bool inited = false;

    void init(const GameInfo& gi) {
        nx = gi.M().nx; ny = gi.M().ny;
        fall.assign(size_t(nx) * ny, -1);
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x)
                fall[size_t(y) * nx + x] = cell_fall_step(gi, x, y);
        const TrigTable& T = trig_table();
        for (int i = 0; i < 16; ++i) {
            const i64 a = i * 2250;  // 22.5 度
            const i64 sn = T.s[a % 9000] * (a < 18000 ? 1 : -1);
            const i64 cs = T.s[(a + 9000) % 9000] * ((a + 9000) % 36000 < 18000 ? 1 : -1);
            // 用 iatan2 的逆：直接查表组合
            i64 sx, sy;
            angle_vec(a, sx, sy);
            dirx[i] = sx; diry[i] = sy;
            (void)sn; (void)cs;
        }
        for (int k = 0; k < MAXP; ++k) { seen[k] = false; lst[k] = -1000; lsx[k] = lsy[k] = lsvx[k] = lsvy[k] = 0; }
        inited = true;
    }
    static void angle_vec(i64 a, i64& x, i64& y) {
        a = norm_angle(a);
        const TrigTable& T = trig_table();
        // sin(a), cos(a)
        auto sinq = [&](i64 ang) -> i64 {
            ang = norm_angle(ang);
            if (ang <= 9000) return T.s[ang];
            if (ang <= 18000) return T.s[18000 - ang];
            if (ang <= 27000) return -T.s[ang - 18000];
            return -T.s[36000 - ang];
        };
        y = sinq(a) / 1000;
        x = sinq(a + 9000) / 1000;
    }

    // ---------- 世界重建 ----------
    // forceThrow >= 0：假设该对手在盲窗第一步（t-DELAY）就出手了（看不见的最危险情况）
    Game build(const Obs& o, int forceThrow = -1) {
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        Game g(gi, o);
        const i64 D = imin(cf.delay, o.t);
        if (D > 0) {
            const i64 t0 = o.t - D;
            g.t = t0;
            // 自己放回 t0 时的精确状态
            g.p[o.seat] = hp[t0 % RING];
            g.p[o.seat].team = gi.team[o.seat];
            g.d[o.seat] = hd[t0 % RING];
            // 隐形对手：放回估计位置（推演用），之后恢复 unknown
            for (int k = 0; k < o.n; ++k) {
                if (k == o.seat || !o.p[k].alive) continue;
                if (!o.p[k].vis) {
                    i64 ex, ey;
                    ghost_pos(k, t0, ex, ey);
                    g.p[k].x = ex; g.p[k].y = ey;
                    g.p[k].px = ex; g.p[k].py = ey;
                    g.p[k].unknown = 0;
                    g.p[k].st = S_FREE; g.p[k].timer = 0;
                    if (g.d[k].st == D_HAND) { g.d[k].x = ex; g.d[k].y = ey; }
                }
            }
            for (i64 s = 0; s < D; ++s) {
                Action acts[MAXP];
                for (int k = 0; k < o.n; ++k) {
                    if (k == o.seat) { acts[k] = ha[g.t % RING]; continue; }
                    if (!g.p[k].alive) continue;
                    // 对手：沿用观察到的速度继续走
                    acts[k].mx = g.p[k].vx * 4;
                    acts[k].my = g.p[k].vy * 4;
                    if (k == forceThrow && s == 0 && g.p[k].st == S_FREE && g.d[k].st == D_HAND) {
                        const Player& M = g.p[o.seat];
                        i64 d = ilen(M.x - g.p[k].x, M.y - g.p[k].y);
                        if (d < 7500 && coarse_los(g, g.p[k].x, g.p[k].y, M.x, M.y)) {
                            i64 fs = flight_steps(d);
                            i64 lvx = M.vx, lvy = M.vy;
                            cap_len(lvx, lvy, cf.run_v);
                            i64 lx = lvx * fs * 2 / 5, ly = lvy * fs * 2 / 5;
                            cap_len(lx, ly, 1200);
                            acts[k].act = A_THROW;
                            acts[k].ax = M.x + lx - g.p[k].x;
                            acts[k].ay = M.y + ly - g.p[k].y;
                        }
                    }
                }
                g.step(acts);
                // 观察里活着的对手不应在补偿推演里死掉（估计误差），复活
                for (int k = 0; k < o.n; ++k) {
                    if (k == o.seat || !o.p[k].alive || g.p[k].alive) continue;
                    g.p[k].alive = 1; g.p[k].st = S_FREE; g.p[k].timer = 0;
                    g.p[k].vx = g.p[k].vy = 0;
                    g.p[k].cause = K_NONE; g.p[k].killer = -1; g.p[k].dead_t = -1;
                    if (g.d[k].st == D_GONE) { g.d[k].st = D_HAND; g.d[k].x = g.p[k].x; g.d[k].y = g.p[k].y; }
                    g.over = false; g.winner = -2;
                }
            }
            g.t = o.t;
        }
        // 自己用自己的精确当前状态覆盖
        g.p[o.seat] = o.me();
        g.p[o.seat].team = gi.team[o.seat];
        g.p[o.seat].px = o.me().x; g.p[o.seat].py = o.me().y;
        g.d[o.seat] = o.my_dart();
        for (int k = 0; k < o.n; ++k) {
            if (k == o.seat) continue;
            if (o.p[k].alive && !o.p[k].vis) g.p[k].unknown = 1;
            if (!o.p[k].alive) { g.p[k].unknown = 0; }
        }
        g.over = false;
        return g;
    }
    void ghost_pos(int k, i64 t, i64& x, i64& y) const {
        const GameInfo& gi = info();
        if (!seen[k]) { x = gi.spx[k]; y = gi.spy[k]; return; }
        i64 dt = t - lst[k];
        if (dt < 0) dt = 0;
        if (dt > 60) dt = 60;
        x = lsx[k] + lsvx[k] * dt;
        y = lsy[k] + lsvy[k] * dt;
        x = iclamp(x, 200, gi.M().width() - 200);
        y = iclamp(y, 200, gi.M().height() - 200);
    }

    // ---------- 寻路场 ----------
    struct Field {
        std::vector<int> f;
        int gx = -1, gy = -1;
        bool ok = false;
    };
    void make_field(Field& F, i64 tx, i64 ty, i64 t) {
        const GameInfo& gi = info();
        F.gx = cell_index(tx); F.gy = cell_index(ty);
        if (!gi.M().inside(F.gx, F.gy)) { F.ok = false; return; }
        F.f = grid_dist(gi, F.gx, F.gy, t);
        F.ok = true;
    }
    bool field_dir(const Field& F, i64 x, i64 y, i64& dx, i64& dy) const {
        if (!F.ok) { dx = F.gx >= 0 ? cell_center(F.gx) - x : 0; dy = F.gy >= 0 ? cell_center(F.gy) - y : 0; return dx || dy; }
        const int cx = cell_index(x), cy = cell_index(y);
        if (!info().M().inside(cx, cy)) { dx = cell_center(F.gx) - x; dy = cell_center(F.gy) - y; return true; }
        const int cur = F.f[size_t(cy) * nx + cx];
        int best = cur < 0 ? 1000000 : cur;
        int bx = -1, by = -1;
        for (int ddy = -1; ddy <= 1; ++ddy)
            for (int ddx = -1; ddx <= 1; ++ddx) {
                if (!ddx && !ddy) continue;
                const int a = cx + ddx, b = cy + ddy;
                if (!info().M().inside(a, b)) continue;
                const int v = F.f[size_t(b) * nx + a];
                if (v < 0) continue;
                if (ddx && ddy) {
                    if (F.f[size_t(cy) * nx + a] < 0 || F.f[size_t(b) * nx + cx] < 0) continue;
                }
                const int cost = v + ((ddx && ddy) ? 14 : 10);
                if (cost <= best) { best = cost; bx = a; by = b; }
            }
        if (bx < 0) { dx = dy = 0; return false; }
        dx = cell_center(bx) - x; dy = cell_center(by) - y;
        return true;
    }

    // ---------- 每步决策上下文 ----------
    struct Ctx {
        Field dartF, anchorF, enemyF[MAXP];
        i64 anchorX = 0, anchorY = 0;
        i64 t = 0;
        int horizon = 30;
    };
    Ctx cx;

    i64 fs_at(i64 x, i64 y) const {
        const int gx = cell_index(x), gy = cell_index(y);
        if (gx < 0 || gy < 0 || gx >= nx || gy >= ny) return 0;
        return fall[size_t(gy) * nx + gx];
    }
    bool coarse_los(const Game& g, i64 x0, i64 y0, i64 x1, i64 y1) const {
        const GameInfo& gi = info();
        for (int i = 1; i < 5; ++i) {
            const i64 x = x0 + (x1 - x0) * i / 5, y = y0 + (y1 - y0) * i / 5;
            const int gx = cell_index(x), gy = cell_index(y);
            if (cell_at_step(gi.cfg, gi.M(), gx, gy, g.t) == C_WALL) return false;
        }
        return true;
    }
    bool standable_at(const GameInfo& gi, i64 x, i64 y, i64 t) const {
        if (x < 0 || y < 0 || x > gi.M().width() || y > gi.M().height()) return false;
        return standable(gi, x, y, t) && !circle_hits_wall(gi.M(), gi.cfg.body_r, x, y);
    }

    // 镖的未来位置（解析、忽略墙，供躲避用）
    void dart_future(const Game& g, int k, i64 px[5], i64 py[5]) const {
        const Config& cf = g.cf;
        Dart dd = g.d[k];
        i64 x = dd.x, y = dd.y;
        const Player& ow = g.p[k];
        for (int s = 0; s < 5; ++s) {
            px[s] = x; py[s] = y;
            if (!dart_flying(dd.st)) break;
            i64 len = dart_step_len(cf, dd);
            i64 ux = 0, uy = 0;
            if (dd.st == D_OUT) {
                ux = dd.vx; uy = dd.vy;
                len = imin(len, dd.left);
            } else {
                i64 tx = ow.x + ow.vx * s, ty = ow.y + ow.vy * s;
                i64 dx = tx - x, dy = ty - y;
                i64 L = ilen(dx, dy);
                if (L <= cf.catch_r) { for (int q = s + 1; q < 5; ++q) { px[q] = tx; py[q] = ty; } break; }
                if (L > 0) { ux = rdiv(dx * 1000, L); uy = rdiv(dy * 1000, L); }
                if (dd.st == D_BACK && dd.age < cf.dart_hang) len = 0;
            }
            x += rdiv(ux * len, 1000); y += rdiv(uy * len, 1000);
            if (dd.st == D_OUT) {
                dd.left -= len; dd.age++;
                if (dd.left <= 0) { dd.st = D_BACK; dd.age = 0; }
            } else dd.age++;
        }
    }

    // ---------- rollout 中的对手模型 ----------
    Action enemy_act(const Game& g, int k) const {
        Action a;
        const GameInfo& gi = info();
        const Player& P = g.p[k];
        if (!P.alive || P.unknown) return a;
        if (P.st != S_FREE) {
            if (P.st == S_DASH && g.cf.dash_slash && P.timer > g.cf.dash_post &&
                P.timer <= g.cf.dash_t + g.cf.dash_post) {
                int tgt = -1;
                i64 bd = 2600;
                for (int j = 0; j < g.n; ++j) {
                    if (j == k || !g.p[j].alive || g.p[j].unknown || gi.team[j] == gi.team[k]) continue;
                    i64 d = ilen(g.p[j].x - P.x, g.p[j].y - P.y);
                    if (d < bd) { bd = d; tgt = j; }
                }
                if (tgt >= 0) {
                    a.act = A_MELEE;
                    a.ax = g.p[tgt].x + g.p[tgt].vx * 3 - P.x;
                    a.ay = g.p[tgt].y + g.p[tgt].vy * 3 - P.y;
                    return a;
                }
            }
            if (P.st == S_THROW || P.st == S_THROWN) {
                i64 tx, ty; enemy_goal(g, k, tx, ty);
                a.mx = (tx - P.x) * 2; a.my = (ty - P.y) * 2;
            }
            return a;
        }
        // 躲镖：飞镖已飞 >=3 步（相当于看到了出手）才反应；回程/召回镖随时反应
        bool danger = false;
        bool throw_threat = false;
        {
            i64 fpx[MAXP][5], fpy[MAXP][5];
            bool have[MAXP] = {};
            for (int j = 0; j < g.n; ++j) {
                if (j == k || !dart_flying(g.d[j].st)) continue;
                if (ilen(g.d[j].x - P.x, g.d[j].y - P.y) > 3200) continue;
                bool react = g.d[j].st != D_OUT || g.d[j].age >= 3;
                if (!react) continue;
                dart_future(g, j, fpx[j], fpy[j]);
                have[j] = true;
                for (int s = 1; s <= 3; ++s) {
                    const i64 dd = ilen(fpx[j][s] - P.x, fpy[j][s] - P.y);
                    if (dd < 950) danger = true;
                    if (dd < 1800) throw_threat = true;
                }
                if (danger) break;
            }
            if (danger) {
                i64 bestS = -1000000000, bux = 0, buy = 0;
                for (int i = 0; i < 8; ++i) {
                    i64 ux = dirx[i * 2], uy = diry[i * 2];
                    if (!standable_at(info(), P.x + ux * 300, P.y + uy * 300, g.t)) continue;
                    i64 mind = 1000000;
                    for (int j = 0; j < g.n; ++j) {
                        if (!have[j]) continue;
                        for (int s = 1; s <= 3; ++s) {
                            i64 d = ilen(fpx[j][s] - P.x - ux * 250 * s, fpy[j][s] - P.y - uy * 250 * s);
                            if (d < mind) mind = d;
                        }
                    }
                    i64 sc = imin(mind, 2000);
                    if (sc > bestS) { bestS = sc; bux = ux; buy = uy; }
                }
                a.mx = bux * 1000; a.my = buy * 1000;
                return a;
            }
        }
        i64 tx, ty; enemy_goal(g, k, tx, ty);
        i64 dx = tx - P.x, dy = ty - P.y;
        i64 L = ilen(dx, dy);
        // 持镖对手贴近时保持横向移动（真实玩家的躲闪姿态）
        {
            int tk = -1; i64 td = 5500;
            for (int j = 0; j < g.n; ++j) {
                if (j == k || !g.p[j].alive || g.p[j].unknown || gi.team[j] == gi.team[k] || g.d[j].st != D_HAND) continue;
                i64 d = ilen(g.p[j].x - P.x, g.p[j].y - P.y);
                if (d < td) { td = d; tk = j; }
            }
            if (tk >= 0) {
                i64 ax = P.x - g.p[tk].x, ay = P.y - g.p[tk].y;
                i64 al = ilen(ax, ay);
                if (al > 1) {
                    i64 px = -ay + ax / 3, py = ax + ay / 3;
                    if (((g.t / 7 + k) % 2) == 0) { px = -px; py = -py; }
                    i64 pl = ilen(px, py);
                    if (pl > 0) {
                        px = rdiv(px * 1000, pl); py = rdiv(py * 1000, pl);
                        if (standable_at(info(), P.x + px * 350, P.y + py * 350, g.t)) {
                            a.mx = px * 1000; a.my = py * 1000;
                            L = 0;  // 已选移动
                        }
                    }
                }
            }
        }
        // 简单转向避墙/坑；找不到安全方向就停下（模型不该自杀）
        if (L > 1) {
            i64 ux = rdiv(dx * 1000, L), uy = rdiv(dy * 1000, L);
            bool safe = standable_at(info(), P.x + ux * 450, P.y + uy * 450, g.t);
            if (!safe) {
                for (int rot : {4500, -4500, 9000, -9000, 13500, -13500, 18000}) {
                    i64 vx2, vy2; angle_vec(iatan2(uy, ux) + rot, vx2, vy2);
                    if (standable_at(info(), P.x + vx2 * 450, P.y + vy2 * 450, g.t)) { ux = vx2; uy = vy2; safe = true; break; }
                }
            }
            if (safe) { a.mx = ux * 1000; a.my = uy * 1000; }
        }
        // 找最近的敌人（相对模拟敌人 k）
        int tgt = -1; i64 bd = 1000000;
        for (int j = 0; j < g.n; ++j) {
            if (j == k || !g.p[j].alive || g.p[j].unknown || gi.team[j] == gi.team[k]) continue;
            i64 d = ilen(g.p[j].x - P.x, g.p[j].y - P.y);
            if (d < bd) { bd = d; tgt = j; }
        }
        if (tgt >= 0) {
            const Player& E = g.p[tgt];
            bool armed = g.d[k].st == D_HAND;
            if (bd < 1450 && coarse_los(g, P.x, P.y, E.x, E.y)) {
                a.act = A_MELEE;
                a.ax = E.x + E.vx * 2 - P.x; a.ay = E.y + E.vy * 2 - P.y;
            } else if (armed && !throw_threat && bd > 1500 && bd < 4600 && ((g.t + k) % 3) == 0 &&
                       coarse_los(g, P.x, P.y, E.x, E.y)) {
                i64 fs = flight_steps(bd);
                a.act = A_THROW;
                a.ax = E.x + E.vx * fs * 4 / 5 - P.x;
                a.ay = E.y + E.vy * fs * 4 / 5 - P.y;
            } else if (armed && bd < 5500 &&
                       (E.st == S_STUN || E.st == S_RECALL || E.st == S_SLASHED || E.st == S_KICKED) &&
                       coarse_los(g, P.x, P.y, E.x, E.y)) {
                i64 fs = flight_steps(bd);
                a.act = A_THROW;
                a.ax = E.x + E.vx * fs - P.x;
                a.ay = E.y + E.vy * fs - P.y;
            } else if (!armed && g.d[k].st == D_GROUND && P.cd == 0 && bd > 3000) {
                i64 ddx = g.d[k].x - P.x, ddy = g.d[k].y - P.y;
                i64 dl = ilen(ddx, ddy);
                if (dl > 3200 && standable_at(info(), P.x + rdiv(ddx * 3000, dl), P.y + rdiv(ddy * 3000, dl), g.t + 6)) {
                    a.act = A_DASH; a.ax = ddx; a.ay = ddy;
                }
            }
        }
        return a;
    }
    void enemy_goal(const Game& g, int k, i64& tx, i64& ty) const {
        const GameInfo& gi = info();
        const Player& P = g.p[k];
        const Dart& md = g.d[k];
        if (md.st == D_GROUND || md.st == D_RISE) { tx = md.x; ty = md.y; return; }
        if (md.st == D_BACK && ilen(md.x - P.x, md.y - P.y) > 1400) { tx = md.x; ty = md.y; return; }
        int tgt = -1; i64 bd = 1000000;
        for (int j = 0; j < g.n; ++j) {
            if (j == k || !g.p[j].alive || g.p[j].unknown || gi.team[j] == gi.team[k]) continue;
            i64 w = ilen(g.p[j].x - P.x, g.p[j].y - P.y);
            if (g.d[j].st != D_HAND) w -= 900;
            if (g.p[j].st == S_STUN) w -= 1500;
            if (w < bd) { bd = w; tgt = j; }
        }
        if (tgt >= 0) {
            const Player& E = g.p[tgt];
            const i64 d = ilen(E.x - P.x, E.y - P.y);
            if (md.st == D_HAND) {
                if (d > 2600) { tx = E.x; ty = E.y; return; }
                const i64 ox = P.x - E.x, oy = P.y - E.y;
                tx = E.x + ox - oy / 2;
                ty = E.y + oy + ox / 2;
                return;
            }
            if (g.d[tgt].st == D_HAND && d < 3200) {
                tx = P.x + (P.x - E.x);
                ty = P.y + (P.y - E.y);
                return;
            }
            tx = E.x; ty = E.y;
            return;
        }
        tx = gi.M().width() / 2;
        ty = gi.M().height() / 2;
    }
    static i64 flight_steps(i64 dist) {
        // 出镖后飞到 dist 需要的步数（900,850,...）
        i64 acc = 0, s = 0;
        while (acc < dist && s < 14) { acc += imax(200, 900 - s * 50); ++s; }
        return s + 3;  // 蓄势2 + 出镖1
    }

    // ---------- rollout 中自己的策略 ----------
    Action my_policy(const Game& g) const {
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        const int me = gi.seat;
        const Player& P = g.p[me];
        Action a;
        if (!P.alive) return a;
        const Dart& md = g.d[me];
        // 非自由状态
        if (P.st != S_FREE) {
            if (P.st == S_RECALL) return a;  // 引导必须原地不动
            if (P.st == S_DASH && cf.dash_slash && P.timer > cf.dash_post && P.timer <= cf.dash_t + cf.dash_post) {
                // 冲刺斩：前方有人就接
                int tgt = -1; i64 bd = 2600;
                for (int k = 0; k < g.n; ++k) {
                    if (k == me || !g.p[k].alive || g.p[k].unknown || is_mate(gi, k)) continue;
                    i64 d = ilen(g.p[k].x - P.x, g.p[k].y - P.y);
                    if (d < bd) { bd = d; tgt = k; }
                }
                if (tgt >= 0) {
                    a.act = A_MELEE;
                    a.ax = g.p[tgt].x + g.p[tgt].vx * 3 - P.x;
                    a.ay = g.p[tgt].y + g.p[tgt].vy * 3 - P.y;
                    return a;
                }
            }
            if (P.st == S_THROW || P.st == S_THROWN) {
                i64 gx, gy; my_goal(g, gx, gy);
                a.mx = (gx - P.x) * 2; a.my = (gy - P.y) * 2;
            }
            return a;
        }
        // 目标
        i64 gx, gy; my_goal(g, gx, gy);
        i64 gdx = 0, gdy = 0;
        goal_dir(g, gx, gy, gdx, gdy);
        // 威胁评估：敌方飞镖未来位置
        i64 fpx[MAXP][5], fpy[MAXP][5];
        bool fly[MAXP] = {};
        int nfly = 0;
        for (int k = 0; k < g.n; ++k) {
            if (k == me || is_mate(gi, k) || !g.p[k].alive) continue;
            if (dart_flying(g.d[k].st)) { dart_future(g, k, fpx[k], fpy[k]); fly[k] = true; ++nfly; }
        }
        // 格挡：镖下一步移动后进入 1500 内、但这一步还打不到我
        if (md.st == D_HAND && cf.block) {
            for (int k = 0; k < g.n; ++k) {
                if (!fly[k]) continue;
                i64 d0 = ilen(fpx[k][0] - P.x, fpy[k][0] - P.y);
                i64 d1 = ilen(fpx[k][1] - P.x, fpy[k][1] - P.y);
                i64 len = dart_step_len(cf, g.d[k]);
                if (d1 <= cf.slash_reach && d0 > cf.body_r + cf.dart_r + len && d0 < 2800) {
                    a.act = A_MELEE;
                    a.ax = fpx[k][1] - P.x; a.ay = fpy[k][1] - P.y;
                    return a;
                }
            }
        }
        // 选移动方向：8 方向 + 停，避开镖
        i64 bestScore = -1000000000; i64 bdx = 0, bdy = 0;
        i64 gL = ilen(gdx, gdy);
        i64 gux = gL > 0 ? rdiv(gdx * 1000, gL) : 0, guy = gL > 0 ? rdiv(gdy * 1000, gL) : 0;
        for (int i = -1; i < 8; ++i) {
            i64 ux, uy;
            if (i < 0) { ux = 0; uy = 0; }
            else { ux = dirx[i * 2]; uy = diry[i * 2]; }
            i64 sc = (ux * gux + uy * guy) / 1000;  // 与目标对齐
            if (ux || uy) {
                if (!standable_at(gi, P.x + ux * 300, P.y + uy * 300, g.t)) sc -= 6000;
            }
            if (nfly) {
                i64 mind = 1000000;
                for (int s = 0; s <= 4; ++s) {
                    i64 mxp = P.x + ux * 250 * s, myp = P.y + uy * 250 * s;
                    for (int k = 0; k < g.n; ++k) {
                        if (!fly[k]) continue;
                        i64 d = ilen(fpx[k][s] - mxp, fpy[k][s] - myp);
                        if (d < mind) mind = d;
                    }
                }
                if (mind < 1350) sc -= (1350 - mind) * 6;
            }
            if (sc > bestScore) { bestScore = sc; bdx = ux; bdy = uy; }
        }
        a.mx = bdx * 1000; a.my = bdy * 1000;
        // 动作
        bool armed = md.st == D_HAND;
        int tgt = -1; i64 bd = 1000000;
        for (int k = 0; k < g.n; ++k) {
            if (k == me || !g.p[k].alive || g.p[k].unknown || is_mate(gi, k)) continue;
            i64 d = ilen(g.p[k].x - P.x, g.p[k].y - P.y);
            if (d < bd) { bd = d; tgt = k; }
        }
        bool threatened = false;
        if (nfly) {
            for (int k = 0; k < g.n && !threatened; ++k) {
                if (!fly[k]) continue;
                for (int s = 0; s <= 3; ++s)
                    if (ilen(fpx[k][s] - P.x, fpy[k][s] - P.y) < 1800) { threatened = true; break; }
            }
        }
        if (tgt >= 0) {
            const Player& E = g.p[tgt];
            if (bd < 1450 && coarse_los(g, P.x, P.y, E.x, E.y)) {
                a.act = A_MELEE;
                a.ax = E.x + E.vx * 2 - P.x; a.ay = E.y + E.vy * 2 - P.y;
                return a;
            }
            if (armed && !threatened && bd > 1400 && bd < 4500 &&
                (((g.t + me) % 3) == 0 || E.st == S_STUN || E.st == S_RECALL ||
                 E.st == S_SLASHED || E.st == S_KICKED) &&
                coarse_los(g, P.x, P.y, E.x, E.y)) {
                i64 fs = flight_steps(bd);
                a.act = A_THROW;
                a.ax = E.x + E.vx * fs * 4 / 5 - P.x;
                a.ay = E.y + E.vy * fs * 4 / 5 - P.y;
                return a;
            }
        }
        // 冲刺：远目标 / 紧急躲避
        if (P.cd == 0) {
            i64 dL = ilen(gx - P.x, gy - P.y);
            if (dL > 3200 && gL > 0) {
                i64 lx = P.x + rdiv(gdx * 3000, gL), ly = P.y + rdiv(gdy * 3000, gL);
                if (standable_at(gi, lx, ly, g.t + 6) && !threatened) {
                    a.act = A_DASH; a.ax = gdx; a.ay = gdy;
                }
            }
        }
        return a;
    }
    void my_goal(const Game& g, i64& gx, i64& gy) const {
        const GameInfo& gi = info();
        const int me = gi.seat;
        const Player& P = g.p[me];
        const Dart& md = g.d[me];
        if (md.st == D_GROUND || md.st == D_RISE) { gx = md.x; gy = md.y; return; }
        if (md.st == D_BACK) {
            i64 d = ilen(md.x - P.x, md.y - P.y);
            if (d > 1400) { gx = md.x; gy = md.y; return; }
        }
        // 找目标敌人
        int tgt = -1; i64 bd = 1000000;
        for (int k = 0; k < g.n; ++k) {
            if (k == me || !g.p[k].alive || g.p[k].unknown || is_mate(gi, k)) continue;
            i64 d = ilen(g.p[k].x - P.x, g.p[k].y - P.y);
            // 优先空手/被晕的
            i64 w = d;
            if (g.d[k].st != D_HAND) w -= 900;
            if (g.p[k].st == S_STUN) w -= 1500;
            if (w < bd) { bd = w; tgt = k; }
        }
        bool armed = md.st == D_HAND;
        if (tgt >= 0) {
            const Player& E = g.p[tgt];
            i64 d = ilen(E.x - P.x, E.y - P.y);
            if (armed) {
                if (d > 2600) { gx = E.x; gy = E.y; return; }
                // 保持距离绕圈
                i64 dx = P.x - E.x, dy = P.y - E.y;
                gx = E.x + dx - dy / 2; gy = E.y + dy + dx / 2;
                return;
            }
            if (g.d[tgt].st == D_HAND && d < 3200) {
                // 空手对持镖：拉开
                gx = P.x + (P.x - E.x); gy = P.y + (P.y - E.y);
                return;
            }
            gx = E.x; gy = E.y; return;
        }
        gx = cx.anchorX; gy = cx.anchorY;
    }
    void goal_dir(const Game& g, i64 gx, i64 gy, i64& dx, i64& dy) const {
        const GameInfo& gi = info();
        const Player& P = g.p[gi.seat];
        const Dart& md = g.d[gi.seat];
        const Field* F = nullptr;
        if ((md.st == D_GROUND || md.st == D_RISE) && cx.dartF.ok &&
            cell_index(gx) == cx.dartF.gx && cell_index(gy) == cx.dartF.gy) F = &cx.dartF;
        else if (gx == cx.anchorX && gy == cx.anchorY && cx.anchorF.ok) F = &cx.anchorF;
        else {
            for (int k = 0; k < g.n; ++k)
                if (cx.enemyF[k].ok && cell_index(gx) == cx.enemyF[k].gx && cell_index(gy) == cx.enemyF[k].gy) { F = &cx.enemyF[k]; break; }
        }
        if (F && field_dir(*F, P.x, P.y, dx, dy)) return;
        dx = gx - P.x; dy = gy - P.y;
    }

    // ---------- 评分 ----------
    struct Roll {
        i64 score = 0;
    };
    i64 rollout(Game& g, const Action& first, int H, int hyp = -1) const {
        const GameInfo& gi = info();
        const int me = gi.seat;
        const Config& cf = gi.cfg;
        const i64 t0 = g.t;
        const i64 kills0 = g.p[me].kills;
        const bool duel = cf.mode == M_DUEL;
        i64 deathT = -1, threatSum = 0, stunSteps = 0, stillSteps = 0;
        i64 minDartD = 1000000;
        for (int s = 0; s < H; ++s) {
            Action acts[MAXP];
            for (int k = 0; k < g.n; ++k) {
                if (!g.p[k].alive) continue;
                if (k == me) acts[k] = (s == 0) ? first : my_policy(g);
                else acts[k] = enemy_act(g, k);
            }
            // 假设出手：看不见的 4 步里对手可能已经扔了
            if (s == 0 && hyp >= 0 && hyp < g.n && g.p[hyp].alive && !g.p[hyp].unknown &&
                g.p[hyp].st == S_FREE && g.d[hyp].st == D_HAND) {
                i64 d = ilen(g.p[hyp].x - g.p[me].x, g.p[hyp].y - g.p[me].y);
                if (d < 7500 && coarse_los(g, g.p[hyp].x, g.p[hyp].y, g.p[me].x, g.p[me].y)) {
                    i64 fs = flight_steps(d);
                    acts[hyp].act = A_THROW;
                    acts[hyp].ax = g.p[me].x + g.p[me].vx * fs * 4 / 5 - g.p[hyp].x;
                    acts[hyp].ay = g.p[me].y + g.p[me].vy * fs * 4 / 5 - g.p[hyp].y;
                }
            }
            // 威胁度量（走步前）
            if (g.p[me].alive) {
                for (int k = 0; k < g.n; ++k) {
                    if (k == me || is_mate(gi, k) || !g.p[k].alive || !dart_flying(g.d[k].st)) continue;
                    i64 d = ilen(g.d[k].x - g.p[me].x, g.d[k].y - g.p[me].y);
                    if (d < minDartD) minDartD = d;
                    if (d < 1500) threatSum += (1500 - d);
                }
                if (g.p[me].st == S_STUN) ++stunSteps;
                if (ilen(g.p[me].vx, g.p[me].vy) < 50 && g.p[me].st == S_FREE) {
                    for (int k = 0; k < g.n; ++k) {
                        if (k == me || is_mate(gi, k) || !g.p[k].alive || g.d[k].st != D_HAND) continue;
                        if (ilen(g.p[k].x - g.p[me].x, g.p[k].y - g.p[me].y) < 6500) { ++stillSteps; break; }
                    }
                }
            }
            g.step(acts);
            if (!g.p[me].alive && deathT < 0) deathT = g.t;
            if (deathT >= 0) {
                // 死后继续推几步看同归于尽
                if (g.t - deathT >= 1) break;
            }
            if (g.over) break;
        }
        i64 sc = 0;
        const Player& MP = g.p[me];
        i64 kills = MP.kills - kills0;
        int othersDead = 0, othersAlive = 0;
        for (int k = 0; k < g.n; ++k) {
            if (k == me || is_mate(gi, k)) continue;
            if (!g.p[k].alive) ++othersDead; else ++othersAlive;
        }
        if (deathT >= 0) {
            bool allDead = othersAlive == 0;
            if (duel && allDead) sc = -3000;              // 同归于尽 = 平局，比输好
            else sc = -30000 + (deathT - t0) * 300;       // 活得久一点（四人局名次）
            sc += kills * 3500;
            return sc;
        }
        if (duel) {
            if (othersAlive == 0) return 30000 + kills * 1000;
        } else {
            sc += othersDead * 4500 + kills * 3500;
        }
        // 持镖
        const Dart& md = g.d[me];
        if (md.st == D_HAND) sc += 450;
        else if (md.st == D_GROUND || md.st == D_RISE) sc -= imin(ilen(md.x - MP.x, md.y - MP.y), 6000) / 8;
        else if (md.st == D_BACK) sc += 150;
        // 位置安全
        i64 fs = fs_at(MP.x, MP.y);
        i64 tend = t0 + H;
        if (fs >= 0 && fs <= tend) sc -= 4000 + (tend - fs) * 120;
        else if (fs >= 0) sc += imin(fs - tend, 240);
        else sc += 240;
        if (!standable_at(gi, MP.x, MP.y, tend)) sc -= 2500;
        // 与敌距离
        i64 nd = 1000000; int nk = -1;
        for (int k = 0; k < g.n; ++k) {
            if (k == me || !g.p[k].alive || is_mate(gi, k)) continue;
            i64 d = ilen(g.p[k].x - MP.x, g.p[k].y - MP.y);
            if (d < nd) { nd = d; nk = k; }
        }
        if (nk >= 0) {
            bool armed = md.st == D_HAND;
            bool eArmed = g.d[nk].st == D_HAND;
            if (armed) {
                if (nd < 1100 && eArmed) sc -= 700;
                if (nd < 1600 && !eArmed) sc += 350;
                if (g.p[nk].st == S_STUN) sc += 500;
                i64 ap = duel ? 5500 : 6500;
                if (nd > ap) sc -= (nd - ap) / (duel ? 25 : 50);
            } else {
                if (eArmed && nd < 2800) sc -= (2800 - nd);
                if (!eArmed && nd < 1500) sc += 250;
            }
        }
        sc -= threatSum / 2;
        sc -= stunSteps * 260;
        sc -= stillSteps * 25;
        // 隐形对手附近的谨慎
        for (int k = 0; k < g.n; ++k) {
            if (k == me || !g.p[k].alive || !g.p[k].unknown || is_mate(gi, k)) continue;
            i64 ex, ey; ghost_pos(k, g.t, ex, ey);
            i64 d = ilen(ex - MP.x, ey - MP.y);
            if (d < 1800 && seen[k] && g.t - lst[k] < 40) sc -= (1800 - d) / 4;
        }
        return sc;
    }

    // ---------- 候选生成 ----------
    int gen_cands(const Game& g, Cand* out) const {
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        const int me = gi.seat;
        const Player& P = g.p[me];
        const Dart& md = g.d[me];
        int nc = 0;
        auto add = [&](const Action& a) { if (nc < MAXC) out[nc++].a = a; };
        if (!P.alive) { add(Action()); return nc; }
        bool armed = md.st == D_HAND;
        bool free = P.st == S_FREE;
        // 敌人列表
        int en[MAXP]; int ne = 0;
        for (int k = 0; k < g.n; ++k) {
            if (k == me || !g.p[k].alive || g.p[k].unknown || is_mate(gi, k)) continue;
            en[ne++] = k;
        }
        // 1) 格挡： incoming dart
        if (free && armed && cf.block) {
            for (int k = 0; k < g.n; ++k) {
                if (k == me || is_mate(gi, k) || !g.p[k].alive || !dart_flying(g.d[k].st)) continue;
                i64 px[5], py[5]; dart_future(g, k, px, py);
                // 判定步用镖移动一步后的位置
                i64 bx = px[1], by = py[1];
                i64 d1 = ilen(bx - P.x, by - P.y);
                i64 d0 = ilen(px[0] - P.x, py[0] - P.y);
                if (d1 <= cf.slash_reach && d0 > 750) {
                    i64 ax = bx - P.x, ay = by - P.y;
                    i64 L = ilen(ax, ay);
                    if (L > 0) {
                        i64 ux = rdiv(ax * 1000, L), uy = rdiv(ay * 1000, L);
                        // 角度检查（20 度内才挡得到，方向就朝镖）
                        Action a; a.act = A_MELEE; a.ax = ux * 1000; a.ay = uy * 1000;
                        add(a);
                    }
                }
            }
        }
        // 2) 近战 / 踢
        if (free) {
            for (int i = 0; i < ne; ++i) {
                int k = en[i];
                const Player& E = g.p[k];
                i64 dx = E.x + E.vx * 2 - P.x, dy = E.y + E.vy * 2 - P.y;
                i64 d = ilen(E.x - P.x, E.y - P.y);
                if (d < 2400 && coarse_los(g, P.x, P.y, E.x, E.y)) {
                    Action a; a.act = A_MELEE; a.ax = dx; a.ay = dy; add(a);
                    if (!armed) {
                        // 踢向坑：朝敌人当前方向（落点判定交给 rollout）；再试把落点推向最近坑边
                        i64 L = ilen(dx, dy);
                        if (L > 0) {
                            i64 ux = rdiv(dx * 1000, L), uy = rdiv(dy * 1000, L);
                            i64 lx = E.x + ux * 2797, ly = E.y + uy * 2797;
                            if (!standable_at(gi, lx, ly, g.t + 14)) {
                                Action a2; a2.act = A_MELEE; a2.ax = ux * 1000; a2.ay = uy * 1000; add(a2);
                            }
                        }
                    }
                }
            }
        }
        // 3) 扔镖（多种提前量）
        if (free && armed) {
            for (int i = 0; i < ne; ++i) {
                int k = en[i];
                const Player& E = g.p[k];
                i64 d = ilen(E.x - P.x, E.y - P.y);
                if (d > 6800 || !coarse_los(g, P.x, P.y, E.x, E.y)) continue;
                i64 fs = flight_steps(d);
                for (int lead = 0; lead < 5; ++lead) {
                    i64 f = (lead == 0) ? 0 : (lead == 1 ? fs / 2 : lead == 2 ? fs * 4 / 5 : lead == 3 ? fs : fs * 6 / 5);
                    Action a; a.act = A_THROW;
                    a.ax = E.x + E.vx * f - P.x;
                    a.ay = E.y + E.vy * f - P.y;
                    add(a);
                    // 横向修正：对手常会侧移，给左右各 0.35 米补偿
                    if (lead >= 2 && d > 2200) {
                        i64 L = ilen(a.ax, a.ay);
                        if (L > 0) {
                            i64 ux = rdiv(a.ax * 1000, L), uy = rdiv(a.ay * 1000, L);
                            Action b = a;
                            b.ax = a.ax - uy * 350; b.ay = a.ay + ux * 350;
                            add(b);
                            Action c = a;
                            c.ax = a.ax + uy * 350; c.ay = a.ay - ux * 350;
                            add(c);
                        }
                    }
                }
            }
        }
        // 4) 冲刺
        if (free && P.cd == 0) {
            for (int i = 0; i < 16; i += 2) {
                i64 lx = P.x + dirx[i] * 3, ly = P.y + diry[i] * 3;
                if (!standable_at(gi, lx, ly, g.t + 6)) continue;
                Action a; a.act = A_DASH; a.ax = dirx[i] * 1000; a.ay = diry[i] * 1000; add(a);
            }
            if (md.st == D_GROUND) {
                i64 dx = md.x - P.x, dy = md.y - P.y;
                i64 L = ilen(dx, dy);
                if (L > 2200) {
                    i64 lx = P.x + rdiv(dx * 3000, L), ly = P.y + rdiv(dy * 3000, L);
                    if (standable_at(gi, lx, ly, g.t + 6)) {
                        Action a; a.act = A_DASH; a.ax = dx; a.ay = dy; add(a);
                    }
                }
            }
        }
        // 5) 召回
        if (free && armed == false && md.st == D_GROUND && cf.recall) {
            i64 d = ilen(md.x - P.x, md.y - P.y);
            if (d > 1800) {
                bool safe = true;
                for (int i = 0; i < ne; ++i) {
                    int k = en[i];
                    if (ilen(g.p[k].x - P.x, g.p[k].y - P.y) < 4200) safe = false;
                    if (dart_flying(g.d[k].st) && ilen(g.d[k].x - P.x, g.d[k].y - P.y) < 4500) safe = false;
                }
                if (safe) { Action a; a.act = A_RECALL; add(a); }
            }
        }
        // 6) 目标导向移动
        {
            i64 gx, gy; my_goal(g, gx, gy);
            i64 dx, dy; goal_dir(g, gx, gy, dx, dy);
            Action a; a.mx = dx * 4; a.my = dy * 4; add(a);
            if (md.st == D_GROUND) {
                Action a2; a2.mx = (md.x - P.x) * 4; a2.my = (md.y - P.y) * 4; add(a2);
            }
        }
        // 7) 16 方向 + 停
        for (int i = 0; i < 16; ++i) {
            Action a; a.mx = dirx[i] * 1000; a.my = diry[i] * 1000; add(a);
        }
        add(Action());
        return nc;
    }

    // ---------- 主决策 ----------
    Action decide(const Obs& o) {
        const GameInfo& gi = info();
        if (!inited) init(gi);
        const i64 cpu0 = thread_cpu_100ns();
        hp[o.t % RING] = o.me();
        hd[o.t % RING] = o.my_dart();
        // 更新目击
        for (int k = 0; k < o.n; ++k) {
            if (k == o.seat) continue;
            if (o.p[k].alive && o.p[k].vis) {
                seen[k] = true;
                lsx[k] = o.p[k].x; lsy[k] = o.p[k].y;
                lsvx[k] = o.p[k].vx; lsvy[k] = o.p[k].vy;
                lst[k] = o.t - gi.cfg.delay;
            }
        }
        if (!o.me().alive) { Action z; ha[o.t % RING] = z; return z; }
        Game g = build(o);
        const Config& cf = gi.cfg;
        // 上下文
        cx.t = o.t;
        cx.horizon = cf.mode == M_DUEL ? 34 : (cf.mode == M_FFA ? 32 : 24);
        const i64 tf = o.t + 8;
        // 安全锚点：塌陷后仍安全、靠中心
        {
            i64 bestS = -1000000000;
            const i64 W = gi.M().width(), H = gi.M().height();
            for (int y = 0; y < ny; ++y)
                for (int x = 0; x < nx; ++x) {
                    i64 fs = fall[size_t(y) * nx + x];
                    if (fs >= 0 && fs <= o.t + cx.horizon + 30) continue;
                    if (cell_at_step(cf, gi.M(), x, y, tf) == C_WALL) continue;
                    i64 px = cell_center(x), py = cell_center(y);
                    i64 s = (fs < 0 ? 500 : imin(fs - o.t, 500)) * 2;
                    s -= ilen(px - W / 2, py - H / 2) / 40;
                    if (s > bestS) { bestS = s; cx.anchorX = px; cx.anchorY = py; }
                }
        }
        make_field(cx.anchorF, cx.anchorX, cx.anchorY, tf);
        const Dart& md = o.my_dart();
        if (md.st == D_GROUND || md.st == D_RISE || md.st == D_BACK) make_field(cx.dartF, md.x, md.y, tf);
        else cx.dartF.ok = false;
        for (int k = 0; k < o.n; ++k) {
            if (k != o.seat && o.p[k].alive && o.p[k].vis) make_field(cx.enemyF[k], g.p[k].x, g.p[k].y, tf);
            else cx.enemyF[k].ok = false;
        }
        // 候选
        Cand cands[MAXC];
        int nc = gen_cands(g, cands);
        // “他可能已经出手了”第二世界：最近的持镖自由对手在盲窗第一步就扔了
        Game g1;
        bool have1 = false;
        {
            int fk = -1; i64 bd = 1000000;
            for (int k = 0; k < o.n; ++k) {
                if (k == o.seat || !o.p[k].alive || !o.p[k].vis || is_mate(gi, k)) continue;
                if (o.p[k].st != S_FREE || o.d[k].st != D_HAND) continue;
                i64 d = ilen(g.p[k].x - g.p[o.seat].x, g.p[k].y - g.p[o.seat].y);
                if (d > 6500 || !coarse_los(g, g.p[k].x, g.p[k].y, g.p[o.seat].x, g.p[o.seat].y)) continue;
                if (d < bd) { bd = d; fk = k; }
            }
            if (fk >= 0) { g1 = build(o, fk); have1 = true; }
        }
        const Action& prev = ha[(o.t + RING - 1) % RING];
        auto eval = [&](const Action& a) -> i64 {
            Game gg = g;
            i64 s = rollout(gg, a, cx.horizon);
            if (have1) {
                Game gh = g1;
                i64 sh = rollout(gh, a, cx.horizon);
                s = (cf.mode == M_DUEL) ? (s + sh) / 2 : (2 * s + sh) / 3;  // 1v1 假想世界即真实对手的可能出手，权重更高
            }
            if (a.mx * prev.mx + a.my * prev.my > 0) s += 15;  // 移动一致性，抑制抖动
            return s;
        };
        Action base = my_policy(g);
        i64 bestScore = eval(base) + 3;
        Action best = base;
        const i64 budget = cf.mode == M_DUEL ? 82000 : (cf.mode == M_TEAM ? 40000 : 72000);  // 1v1 8.2ms / 4v4 4ms / FFA 7.2ms
        auto cpu_now = [&]() { return thread_cpu_100ns(); };
        for (int i = 0; i < nc; ++i) {
            if (i % 3 == 0) {
                i64 now = cpu_now();
                if (cpu0 >= 0 && now >= 0 && now - cpu0 >= budget) break;
            }
            const Action& a = cands[i].a;
            i64 s = eval(a);
            i64 h = a.mx * 31 + a.my * 17 + a.act * 101 + a.ax * 7 + a.ay * 3 + o.seat * 13;
            if (cf.mode == M_DUEL) s += ((h % 5 + 5) % 5 - 2);  // 1v1 用小平局扰动，保持连贯
            else s += (h % 13 + 13) % 13 - 6;                 // 多人局保留原扰动，避免镜像卡死
            if (s > bestScore) { bestScore = s; best = a; }
        }
        ha[o.t % RING] = best;
        return best;
    }
};

DART_MAIN(Bot)