// 飞镖乱斗 - 5v5 团队赛（歼灭 / 占点 / 刺王）
// 核心：用裁判同款 Game 把 4 步前的观察重建成当前局面（自机用真实历史，队友用最新 MSG 校正），
// 再对候选动作做 12 步前向推演；推演里敌人会朝最近的本队队员扔镖/挥砍，队友按当前速度继续走。
// 评分把"自己活着"放在第一位，其次团队击杀/占点/护王，再其次是队形、保镖、地形安全。
// 所有选目标、扔镖、挥砍、踢都排除队友，并按队长要求做友伤检查；队内消息按 MSG 编码发送。
#include "dart.hpp"
#include <algorithm>
#include <array>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cmath>

using namespace dart;

static const int MAXH = 1400;          // 占点一局 1200 步，留余量
static const int MAXCAND = 96;

// 战术单里认识的键
struct PlanCfg {
    bool hasPt = false;
    int pt = -1;                       // 场上索引（已用 point_index 换算）
    int job = 0;                       // 1 hold 2 roam 3 guard 4 hunt 5 safe 6 fight
    bool hasGo = false;
    i64 gox = 0, goy = 0;              // 场地坐标（毫米）
    i64 until = 0;
};

struct MateMsg {
    bool pos = false;
    i64 x = 0, y = 0;
};

struct Seg {
    i64 ax = 0, ay = 0, bx = 0, by = 0, exp = -1;
};

struct Cand {
    Action a;
    double score = 0;
    int kind = 0;                      // 0 move 1 throw 2 melee 3 dash 4 recall
};

struct Bot {
    Config cf;
    int n = 0, seat = 0, team = 0, opp = 0, mode = 0;
    i64 W = 0, H = 0;

    bool mateK[MAXP] = {};
    bool enemyK[MAXP] = {};

    std::vector<Player> myP;
    std::vector<Dart> myD;
    std::vector<Action> myA;
    std::vector<char> haveP;

    Player lastSeen[MAXP];
    i64 lastSeenT[MAXP] = {};
    bool everSeen[MAXP] = {};

    MateMsg mm[MAXP];
    bool obsMateAlive[MAXP] = {};
    Seg segs[24];
    int nseg = 0;

    PlanCfg plan;
    int pt = -1;                        // 占点负责的点
    int ownKing = -1, enemyKing = -1;

    bool hasGoal = false;
    i64 gx = 0, gy = 0;
    bool hasStep = false;
    i64 sgx = 0, sgy = 0;
    bool hasEvalGoal = false;
    i64 egx = 0, egy = 0;

    i64 rngState = 12345;

    i64 cpuStart = 0;
    bool cpuLimited = false;

    void init(const GameInfo& gi) {
        cf = gi.cfg;
        n = gi.n;
        seat = gi.seat;
        team = int(gi.team[seat]);
        opp = 1 - team;
        mode = int(cf.mode);
        W = gi.M().width();
        H = gi.M().height();
        for (int k = 0; k < MAXP; ++k) {
            mateK[k] = (k != seat && k < n && gi.team[k] == gi.team[seat]);
            enemyK[k] = (k < n && k != seat && !mateK[k]);
            lastSeen[k] = Player();
            lastSeenT[k] = -1;
            everSeen[k] = false;
            mm[k] = MateMsg();
        }
        const int cap = int(imax((i64)MAXH, cf.steps + 80));
        myP.assign(cap + 8, Player());
        myD.assign(cap + 8, Dart());
        myA.assign(cap + 8, Action());
        haveP.assign(cap + 8, 0);
        for (int i = 0; i < 24; ++i) segs[i] = Seg();
        rngState = 0x9E3779B97F4A7C15ULL ^ (u64)(seat + 1) * 1234567ULL;
        if (mode == M_KING) {
            ownKing = gi.king[team];
            enemyKing = gi.king[opp];
        }
        parsePlan(gi);
        if (mode == M_CONTROL) {
            if (plan.hasPt && plan.pt >= 0 && plan.pt < int(gi.points.size())) pt = plan.pt;
            else {
                // 默认：离自己复活点最近的点
                const i64 bx = gi.base_x[seat] > 0 ? gi.base_x[seat] : gi.spx[seat];
                const i64 by = gi.base_y[seat] > 0 ? gi.base_y[seat] : gi.spy[seat];
                i64 bd = -1;
                for (int i = 0; i < int(gi.points.size()); ++i) {
                    const i64 d = ilen(gi.points[i].x - bx, gi.points[i].y - by);
                    if (bd < 0 || d < bd) { bd = d; pt = i; }
                }
                if (pt < 0) pt = 0;
            }
        }
    }

    // ---------------- 战术单 ----------------
    static void trim(std::string& s) {
        while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        if (i) s.erase(0, i);
    }

    void parsePlan(const GameInfo& gi) {
        const std::string meName = gi.names[seat];
        // 键：0 pt，1 job，2 go，3 until
        std::string ownV[4], allV[4];
        bool ownSet[4] = {}, allSet[4] = {};
        size_t pos = 0;
        while (pos <= gi.plan.size()) {
            size_t e = gi.plan.find('\n', pos);
            if (e == std::string::npos) e = gi.plan.size();
            std::string line = gi.plan.substr(pos, e - pos);
            pos = e + 1;
            trim(line);
            if (line.empty()) continue;
            // 切词
            std::vector<std::string> tok;
            std::string cur;
            for (char c : line) {
                if (c == ' ' || c == '\t') {
                    if (!cur.empty()) { tok.push_back(cur); cur.clear(); }
                } else cur += c;
            }
            if (!cur.empty()) tok.push_back(cur);
            if (tok.size() < 2) continue;
            const bool own = (tok[0] == meName);
            const bool all = (tok[0] == "all");
            if (!own && !all) continue;
            for (size_t i = 1; i < tok.size(); ++i) {
                const size_t eq = tok[i].find('=');
                if (eq == std::string::npos) continue;
                const std::string key = tok[i].substr(0, eq);
                const std::string val = tok[i].substr(eq + 1);
                int ki = -1;
                if (key == "pt") ki = 0;
                else if (key == "job") ki = 1;
                else if (key == "go") ki = 2;
                else if (key == "until") ki = 3;
                if (ki < 0) continue;
                if (own) { ownV[ki] = val; ownSet[ki] = true; }
                else if (!ownSet[ki]) { allV[ki] = val; allSet[ki] = true; }
            }
        }
        std::string v[4];
        bool has[4];
        for (int i = 0; i < 4; ++i) { v[i] = ownSet[i] ? ownV[i] : allV[i]; has[i] = ownSet[i] || allSet[i]; }
        if (has[0]) {
            int idx = gi.point_index(v[0]);
            if (idx >= 0) { plan.pt = idx; plan.hasPt = true; }
        }
        if (has[1]) {
            const std::string& s = v[1];
            if (mode == M_CONTROL) {
                if (s == "hold") plan.job = 1;
                else if (s == "roam") plan.job = 2;
            } else if (mode == M_KING) {
                if (seat == ownKing) {
                    if (s == "safe") plan.job = 5;
                    else if (s == "fight") plan.job = 6;
                } else {
                    if (s == "guard") plan.job = 3;
                    else if (s == "hunt") plan.job = 4;
                    // 也允许在非国王行写 safe/fight，忽略
                }
            }
        }
        if (has[2]) {
            const std::string& s = v[2];
            const size_t comma = s.find(',');
            if (comma != std::string::npos) {
                const double x = std::strtod(s.substr(0, comma).c_str(), nullptr);
                const double y = std::strtod(s.substr(comma + 1).c_str(), nullptr);
                if (x > 0.05 && y > 0.05 && x * 1000 < W && y * 1000 < H) {
                    i64 px = (i64)std::llround(x * 1000.0);
                    i64 py = (i64)std::llround(y * 1000.0);
                    if (team == 1) { px = W - px; py = H - py; }
                    if (px < 300) px = 300;
                    if (py < 300) py = 300;
                    if (px > W - 300) px = W - 300;
                    if (py > H - 300) py = H - 300;
                    plan.hasGo = true;
                    plan.gox = px;
                    plan.goy = py;
                }
            }
        }
        if (has[3]) {
            i64 u = std::atoll(v[3].c_str());
            if (u > 0) plan.until = u;
        }
        if (plan.hasGo && plan.until <= 0) plan.until = cf.steps + 10;
    }

    // ---------------- 自己的历史 ----------------
    void recordSelf(const Obs& o) {
        if (o.t >= 0 && o.t < int(myP.size())) {
            myP[o.t] = o.me();
            myP[o.t].unknown = 0;
            myD[o.t] = o.my_dart();
            haveP[o.t] = 1;
        }
        for (int k = 0; k < n; ++k) {
            if (!enemyK[k]) continue;
            if (o.p[k].alive && o.p[k].vis) {
                lastSeen[k] = o.p[k];
                lastSeenT[k] = o.t - cf.delay;
                everSeen[k] = true;
            }
        }
    }

    // 收到队友消息：F=1 位置，F=2 镖路终点（危险线段）
    void recvMsgs(const Obs& o) {
        for (int k = 0; k < n; ++k) {
            if (!mateK[k]) continue;
            if (!o.p[k].alive) continue;
            const i64 m = o.p[k].msg;
            if (m < 0) continue;
            const i64 bit = m / 16384;         // F
            const i64 rem = m % 16384;
            const i64 qx = rem % 128, qy = rem / 128;
            const i64 px = qx * 250 + 125, py = qy * 250 + 125;
            if (bit == 1) {
                mm[k].pos = true;
                mm[k].x = px;
                mm[k].y = py;
            } else if (bit == 2 || bit == 3) {
                // 终点不是位置；从上一条有效位置记一条危险线段（F=3 是冲刺落点，只看 6 步）
                i64 ax, ay;
                if (mm[k].pos) { ax = mm[k].x; ay = mm[k].y; }
                else { ax = o.p[k].x; ay = o.p[k].y; }
                if (nseg >= 24) {
                    for (int i = 1; i < 24; ++i) segs[i - 1] = segs[i];
                    --nseg;
                }
                segs[nseg].ax = ax; segs[nseg].ay = ay;
                segs[nseg].bx = px; segs[nseg].by = py;
                segs[nseg].exp = o.t + (bit == 3 ? 6 : 16);
                ++nseg;
            }
        }
    }

    static void unit(i64 dx, i64 dy, i64& ux, i64& uy) {
        if (!scale_to(dx, dy, UNIT, ux, uy)) { ux = UNIT; uy = 0; }
    }

    void moveFromVel(const Player& P, Action& a) {
        i64 pct = 0;
        if (P.st == S_FREE) pct = 100;
        else if (P.st == S_THROW || P.st == S_THROWN) pct = (int)cf.throw_move;
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

    // 推演里敌人的行为：朝最近的本队队员（看得见）扔镖/挥砍/踢；这是偏悲观的威胁模型
    Action enemyAction(const Game& g, int k) {
        Action a = contAction(g.p[k]);
        const Player& P = g.p[k];
        if (!P.alive || P.unknown || P.st != S_FREE) return a;
        int tgt = -1;
        i64 bd = 0;
        for (int q = 0; q < g.n; ++q) {
            if (mateK[q] || q == seat) {
                if (!g.p[q].alive || g.p[q].unknown) continue;
                const i64 d = ilen(g.p[q].x - P.x, g.p[q].y - P.y);
                if (tgt < 0 || d < bd) { tgt = q; bd = d; }
            }
        }
        if (tgt < 0 || bd > 5000) return a;
        const Player& T = g.p[tgt];
        if (wall_between(g.M(), P.x, P.y, T.x, T.y)) return a;
        // 预判一点：朝目标 3 步后的位置
        i64 tx = T.x + T.vx * 6, ty = T.y + T.vy * 6;
        const i64 dx = tx - P.x, dy = ty - P.y;
        const i64 dd = ilen(dx, dy);
        if (dd <= 0) return a;
        if (g.d[k].st == D_HAND) {
            if (bd <= 1450) { a.act = A_MELEE; a.ax = dx; a.ay = dy; }
            else if (bd <= 5000) { a.act = A_THROW; a.ax = dx; a.ay = dy; }
        } else if (bd <= cf.kick_reach) {
            a.act = A_MELEE;
            a.ax = dx; a.ay = dy;
        }
        return a;
    }

    // 从 o.t-delay 重建到当前
    bool buildWorld(const Obs& o, Game& out) {
        const i64 T0 = imax((i64)0, o.t - cf.delay);
        Game g(info(), o);
        g.hist.reset();
        g.over = false;
        if (o.t <= 0 || !haveP[T0] || T0 >= (i64)myP.size()) {
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
                    if (s < (i64)myA.size()) acts[k] = myA[s];
                    continue;
                }
                const Player& P = g.p[k];
                Action a;
                if (P.alive && !P.unknown) moveFromVel(P, a);
                acts[k] = a;
            }
            g.step(acts);
            if (!g.p[seat].alive) {
                Game h(info(), o);
                h.hist.reset();
                h.over = false;
                h.t = o.t;
                out = h;
                return false;
            }
        }
        g.t = o.t;
        g.p[seat] = o.me();
        g.p[seat].unknown = 0;
        g.d[seat] = o.my_dart();
        g.over = false;
        out = g;
        return true;
    }

    // 用最新 MSG 把队友的位置改新（消息比观察新 3 步）
    void applyMates(Game& g, const Obs& o) {
        for (int k = 0; k < n; ++k) {
            if (!mateK[k] || !o.p[k].alive) continue;
            if (!mm[k].pos) continue;
            Player& P = g.p[k];
            P.alive = 1;
            P.unknown = 0;
            P.x = P.px = mm[k].x;
            P.y = P.py = mm[k].y;
            // 位置是上一步开头的；速度用观察到的，限定在跑速内
            if (P.st != S_FREE && P.st != S_STUN) P.vx = P.vy = 0;
            else {
                cap_len(P.vx, P.vy, cf.run_v);
            }
            P.timer = 0;
            if (g.d[k].st == D_HAND) { g.d[k].x = P.x; g.d[k].y = P.y; }
        }
    }

    // 草丛里看不见的敌人：用最后一次看见的位置和速度估计，放进推演当威胁
    void fillHidden(const Obs& o, Game& g) {
        for (int k = 0; k < n; ++k) {
            if (!enemyK[k]) continue;
            if (!g.p[k].alive || !g.p[k].unknown) continue;
            if (!everSeen[k]) continue;
            const i64 dt = o.t - lastSeenT[k];
            if (dt < 0 || dt > 40) continue;
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
            g.p[k] = P;
            if (g.d[k].st == D_HAND) { g.d[k].x = P.x; g.d[k].y = P.y; }
        }
    }

    // 预测每个人未来 20 步的位置（按当前速度继续）
    struct Paths {
        i64 x[MAXP][24];
        i64 y[MAXP][24];
        bool alive[MAXP][24];
        bool valid[MAXP];
    };

    void predictPaths(const Game& base, Paths& out) {
        Game g = base;
        g.hist.reset();
        g.over = false;
        for (int k = 0; k < g.n; ++k) {
            out.valid[k] = g.p[k].alive && !g.p[k].unknown;
            for (int s = 0; s < 24; ++s) {
                out.x[k][s] = g.p[k].x;
                out.y[k][s] = g.p[k].y;
                out.alive[k][s] = g.p[k].alive != 0;
            }
        }
        for (int s = 1; s <= 18; ++s) {
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

    // ---------------- 几何 ----------------
    static i64 distSeg(i64 ax, i64 ay, i64 bx, i64 by, i64 px, i64 py) {
        const i64 dx = bx - ax, dy = by - ay;
        const i64 l2 = dx * dx + dy * dy;
        i64 qx = ax, qy = ay;
        if (l2 > 0) {
            i64 t = ((px - ax) * dx + (py - ay) * dy);
            if (t < 0) t = 0;
            if (t > l2) t = l2;
            qx = ax + rdiv(dx * t, l2);
            qy = ay + rdiv(dy * t, l2);
        }
        return ilen(px - qx, py - qy);
    }

    double cellSafety(const Game& g, i64 x, i64 y) {
        const Map& M = g.M();
        const int cx = cell_index(x), cy = cell_index(y);
        if (cx < 0 || cy < 0 || cx >= M.nx || cy >= M.ny) return -1e5;
        const int c = cell_at_step(cf, M, cx, cy, g.t);
        if (c == C_PIT || c == C_WALL) return -1e5;
        const i64 fall = cell_fall_step(info(), cx, cy);
        if (fall < 0) return 500.0;
        const i64 rem = fall - g.t;
        if (rem < 0) return -1e5;
        if (rem > 150) return 500.0;
        return rem * 3.5;
    }

    bool standable(i64 x, i64 y, i64 t) {
        const int c = cell_of_point(cf, info().M(), x, y, t);
        if (c != C_FLOOR && c != C_GRASS) return false;
        if (circle_hits_wall(info().M(), cf.body_r, x, y)) return false;
        return true;
    }

    // 队友位置：优先消息
    bool matePos(int k, const Obs& o, i64& x, i64& y) {
        if (mm[k].pos) { x = mm[k].x; y = mm[k].y; return true; }
        if (o.p[k].alive && o.p[k].vis) { x = o.p[k].x; y = o.p[k].y; return true; }
        return false;
    }

    // 友伤检查：不只看队友现在的位置，还看他接下来 5 步大概会走到哪（观察有 4 步延迟）
    bool throwSafe(const Game& g, i64 ax, i64 ay) {
        i64 ux, uy;
        if (!scale_to(ax, ay, UNIT, ux, uy)) return true;
        const i64 x0 = g.p[seat].x, y0 = g.p[seat].y;
        const i64 x1 = x0 + rdiv(ux * cf.range, UNIT);
        const i64 y1 = y0 + rdiv(uy * cf.range, UNIT);
        for (int k = 0; k < n; ++k) {
            if (!mateK[k]) continue;
            if (!g.p[k].alive || g.p[k].unknown) continue;
            i64 pvx = g.p[k].vx, pvy = g.p[k].vy;
            cap_len(pvx, pvy, cf.run_v);
            const int steps = 2;
            const i64 lim = (mode == M_KING && k == ownKing) ? 4000 : 1380;
            for (int s = 0; s <= steps; ++s) {
                const i64 px = g.p[k].x + pvx * s, py = g.p[k].y + pvy * s;
                if (distSeg(x0, y0, x1, y1, px, py) >= lim) continue;
                return false;
            }
        }
        return true;
    }

    // 友伤检查：不只看队友现在的位置，还看他 2 步后走到哪（挥砍起手 2 步）
    bool meleeSafe(const Game& g, i64 ax, i64 ay) {
        i64 ux, uy;
        if (!scale_to(ax, ay, UNIT, ux, uy)) return true;
        for (int k = 0; k < n; ++k) {
            if (!mateK[k]) continue;
            if (!obsMateAlive[k]) continue;
            const i64 lim = (mode == M_KING && k == ownKing) ? 4000 : 2200;
            for (int s = 0; s <= 2; s += 2) {
                i64 px = g.p[k].x, py = g.p[k].y;
                if (s == 2) {
                    i64 vx = g.p[k].vx, vy = g.p[k].vy;
                    cap_len(vx, vy, cf.run_v);
                    px += vx * 2; py += vy * 2;
                }
                const i64 dx = px - g.p[seat].x, dy = py - g.p[seat].y;
                const i64 d = ilen(dx, dy);
                if (d > lim) continue;
                if (d <= cf.body_r + 200) return false;
                if (d <= 0) return false;
                const i64 dot = (dx * ux + dy * uy) * 1000;
                if (dot >= 174 * d * UNIT) return false;   // ±80°
            }
        }
        return true;
    }

    // 目标敌人身边 1.2 米内有队友（含队友 2 步后的位置）就不砍
    bool mateNearPoint(const Game& g, i64 x, i64 y, i64 lim) {
        for (int k = 0; k < n; ++k) {
            if (!mateK[k] || !obsMateAlive[k]) continue;
            for (int s = 0; s <= 2; s += 2) {
                i64 px = g.p[k].x, py = g.p[k].y;
                if (s == 2) {
                    i64 vx = g.p[k].vx, vy = g.p[k].vy;
                    cap_len(vx, vy, cf.run_v);
                    px += vx * 2; py += vy * 2;
                }
                if (ilen(px - x, py - y) < lim) return true;
            }
        }
        return false;
    }

    double mateDanger(const Game& g) {
        const i64 x = g.p[seat].x, y = g.p[seat].y;
        double s = 0;
        for (int i = 0; i < nseg; ++i) {
            if (g.t >= segs[i].exp) continue;
            if (distSeg(segs[i].ax, segs[i].ay, segs[i].bx, segs[i].by, x, y) < 1200) s -= 2600;
        }
        for (int k = 0; k < n; ++k) {
            if (!mateK[k] || !g.p[k].alive) continue;
            const Dart& D = g.d[k];
            if (!dart_flying(D.st)) continue;
            if (ilen(D.x - x, D.y - y) < 1200) s -= 2400;
            if ((D.st == D_BACK || D.st == D_CALL) && distSeg(D.x, D.y, g.p[k].x, g.p[k].y, x, y) < 1000) s -= 1800;
        }
        return s;
    }

    double controlStep(const Game& g) {
        double s = 0;
        if (mode != M_CONTROL) return s;
        for (int i = 0; i < int(info().points.size()) && i < 3; ++i) {
            const Point& P = info().points[i];
            int own = 0, en = 0;
            for (int k = 0; k < n; ++k) {
                if (!g.p[k].alive || g.p[k].unknown) continue;
                const i64 dx = g.p[k].x - P.x, dy = g.p[k].y - P.y;
                if (dx * dx + dy * dy > P.r * P.r) continue;
                if (mateK[k] || k == seat) ++own; else ++en;
            }
            if (own > 0 && en == 0) {
                s += (g.owner[i] == team ? 150.0 : 340.0);
                if (g.taking[i] == team) s += g.prog[i] * 1.2;
            } else if (own == 0 && en > 0) {
                s -= 280.0;
                if (g.taking[i] == opp) s -= g.prog[i] * 1.0;
            } else if (own > 0 && en > 0) {
                s -= (g.owner[i] == team ? 25.0 : 150.0);
            }
        }
        // 自己站在负责的圈里，额外加分
        if (pt >= 0 && pt < int(info().points.size())) {
            const Point& P = info().points[pt];
            const i64 dx = g.p[seat].x - P.x, dy = g.p[seat].y - P.y;
            if (dx * dx + dy * dy <= P.r * P.r) s += 150.0;
            else s -= imin(ilen(dx, dy), 9000) * 0.015;
        }
        return s;
    }

    // ---------------- 评分 ----------------
    double evalAction(const Game& base, const Action& first, int HH) {
        Game g = base;
        g.hist.reset();
        g.over = false;
        double score = 0;
        bool dead = false;
        int deadAt = HH;
        i64 prevGoalD = hasEvalGoal ? ilen(g.p[seat].x - egx, g.p[seat].y - egy) : 0;
        i64 prevEK = (mode == M_KING && enemyKing >= 0) ? g.p[enemyKing].lives : 0;
        i64 prevOK = (mode == M_KING && ownKing >= 0) ? g.p[ownKing].lives : 0;
        bool prevEA = (mode == M_KING && enemyKing >= 0) ? g.p[enemyKing].alive != 0 : false;
        bool prevOA = (mode == M_KING && ownKing >= 0) ? g.p[ownKing].alive != 0 : false;
        bool prevAlive[MAXP];
        for (int k = 0; k < n; ++k) prevAlive[k] = g.p[k].alive != 0;

        const double mateDeathPen = (mode == M_CONTROL) ? 4000.0 : (mode == M_KING ? 13000.0 : 11000.0);
        const double enemyDeathGain = (mode == M_CONTROL) ? 2500.0 : 13000.0;

        for (int s = 0; s < HH; ++s) {
            Action acts[MAXP];
            for (int k = 0; k < g.n; ++k) {
                if (k == seat) {
                    Action a;
                    if (s == 0) a = first;
                    else {
                        a.mx = first.mx; a.my = first.my;
                        if (g.p[seat].st == S_RECALL) a.mx = a.my = 0;
                    }
                    acts[k] = a;
                } else if (mateK[k]) {
                    acts[k] = contAction(g.p[k]);
                } else {
                    acts[k] = enemyAction(g, k);
                }
            }
            g.step(acts);
            g.over = false;
            if (!g.p[seat].alive) { dead = true; deadAt = s; break; }
            score += 2.0;
            // 死亡/击杀
            for (int k = 0; k < g.n; ++k) {
                if (k == seat) continue;
                const bool al = g.p[k].alive != 0;
                if (prevAlive[k] && !al) {
                    if (mateK[k]) score -= mateDeathPen;
                    else score += enemyDeathGain;
                }
                prevAlive[k] = al;
            }
            score += cellSafety(g, g.p[seat].x, g.p[seat].y) * 0.30;
            if (mode == M_CONTROL) score += controlStep(g);
            if (mode == M_KING) {
                if (enemyKing >= 0) {
                    const i64 lv = g.p[enemyKing].lives;
                    if (lv < prevEK) score += 17000.0 * double(prevEK - lv);
                    prevEK = lv;
                    const bool al = g.p[enemyKing].alive != 0;
                    if (prevEA && !al) score += 4.0e7;
                    prevEA = al;
                }
                if (ownKing >= 0) {
                    const i64 lv = g.p[ownKing].lives;
                    if (lv < prevOK) score -= (seat == ownKing ? 60000.0 : 30000.0) * double(prevOK - lv);
                    prevOK = lv;
                    const bool al = g.p[ownKing].alive != 0;
                    if (prevOA && !al) score -= 4.0e7;
                    prevOA = al;
                }
            }
            if (hasEvalGoal) {
                const i64 gd = ilen(g.p[seat].x - egx, g.p[seat].y - egy);
                score += double(prevGoalD - gd) * 0.03;
                prevGoalD = gd;
            }
            // 队形：5 个人别挤成一团（占点圈里例外）
            {
                i64 dmn = -1;
                for (int k = 0; k < n; ++k) {
                    if (!mateK[k] || !g.p[k].alive) continue;
                    const i64 d = ilen(g.p[k].x - g.p[seat].x, g.p[k].y - g.p[seat].y);
                    if (dmn < 0 || d < dmn) dmn = d;
                }
                if (dmn >= 0) {
                    const i64 want = (mode == M_CONTROL) ? 1800 : 1700;
                    if (dmn < want) score -= (want - dmn) * (mode == M_CONTROL ? 0.05 : 0.55);
                }
            }
            score += mateDanger(g);
            // 空手时：和持镖的敌人保持 7 米以上，躲墙后更好（空手被镖打死的比例最高）
            if (g.d[seat].st != D_HAND) {
                double risk = 0;
                int wk = -1;
                i64 wd = 0;
                for (int k = 0; k < g.n; ++k) {
                    if (!enemyK[k] || !g.p[k].alive || g.p[k].unknown) continue;
                    if (g.d[k].st != D_HAND) continue;
                    const i64 d = ilen(g.p[k].x - g.p[seat].x, g.p[k].y - g.p[seat].y);
                    if (d >= 7000) continue;
                    risk -= (7000 - d) * 0.85;
                    if (wk < 0 || d < wd) { wk = k; wd = d; }
                }
                if (wk >= 0 && wall_between(g.M(), g.p[seat].x, g.p[seat].y, g.p[wk].x, g.p[wk].y)) risk += 500;
                score += risk;
            }
        }

        const double ownDeath = (mode == M_CONTROL) ? 55000.0 : 1.0e8;
        const double kingDeath = (seat == ownKing) ? 3.5e8 : ownDeath;
        if (dead) {
            const double pen = (mode == M_KING && seat == ownKing) ? kingDeath : ownDeath;
            return -pen + deadAt * 160.0 + score * 0.02;
        }

        const Player& me = g.p[seat];
        // 目标距离
        if (hasEvalGoal) {
            const i64 gd = ilen(me.x - egx, me.y - egy);
            const double gw = (mode == M_CONTROL) ? 0.12 : 0.06;
            score -= gd * gw;
        }
        // 手里的镖
        const Dart& md = g.d[seat];
        if (md.st == D_HAND) score += 130;
        else if (md.st == D_GROUND) {
            const i64 dd = ilen(md.x - me.x, md.y - me.y);
            score -= dd * 0.03;
            if (dd <= cf.pick_r) score += 100;
        } else score += 30;
        // 地图边界
        const i64 bd = imin(imin(me.x, W - me.x), imin(me.y, H - me.y));
        if (bd < 800) score -= (800 - bd) * 0.06;
        // 和最近队友的距离
        i64 dm = -1;
        for (int k = 0; k < n; ++k) {
            if (!mateK[k] || !g.p[k].alive) continue;
            const i64 d = ilen(g.p[k].x - me.x, g.p[k].y - me.y);
            if (dm < 0 || d < dm) dm = d;
        }
        if (dm >= 0) {
            if (dm < 1750) score -= (1750 - dm) * 0.06;
            else if (dm > 5200) score -= (dm - 5200) * 0.03;
        }
        // 占点：比分差
        if (mode == M_CONTROL) {
            score += (g.score[team] - g.score[opp]) * 8.0;
            // hold：别追得太远，守着自己的圈
            if (plan.job != 2 && pt >= 0 && pt < int(info().points.size())) {
                const Point& P = info().points[pt];
                const i64 d = ilen(me.x - P.x, me.y - P.y);
                if (d > P.r + 800) score -= (d - P.r - 800) * 1.2;
            }
        }
        // 刺王：护王者别离开国王太远；国王尽量离敌人 6 米以上
        if (mode == M_KING) {
            if (seat == ownKing) {
                if (plan.job != 6) {
                    i64 de = 30000;
                    for (int k = 0; k < g.n; ++k) {
                        if (!enemyK[k] || !g.p[k].alive || g.p[k].unknown) continue;
                        de = imin(de, ilen(g.p[k].x - me.x, g.p[k].y - me.y));
                    }
                    if (de < 2600) score -= (2600.0 - double(de)) * 1.2;
                }
            } else if (ownKing >= 0 && g.p[ownKing].alive && plan.job != 4) {
                const i64 dk = ilen(me.x - g.p[ownKing].x, me.y - g.p[ownKing].y);
                if (dk > 3500) score -= (dk - 3500) * 0.12;
            }
        }
        // 离坑/快塌的格子太近要扣分（被队友或冲刺推到坑里是头号死因）
        {
            const int mcx = cell_index(me.x), mcy = cell_index(me.y);
            int bad = 0;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx) {
                    const int ax = mcx + dx, ay = mcy + dy;
                    if (ax < 0 || ay < 0 || ax >= g.M().nx || ay >= g.M().ny) { bad = imax(bad, 700); continue; }
                    const int cc = cell_at_step(cf, g.M(), ax, ay, g.t);
                    const int cheb = imax(iabs(dx), iabs(dy));
                    if (cc == C_PIT) bad = imax(bad, cheb <= 1 ? 900 : 350);
                    else {
                        const i64 ff = cell_fall_step(info(), ax, ay);
                        if (ff >= 0 && ff - g.t < 45) bad = imax(bad, cheb <= 1 ? 500 : 200);
                    }
                }
            score -= bad;
        }
        // 草丛
        const int mc = cell_at_step(cf, g.M(), cell_index(me.x), cell_index(me.y), g.t);
        if (mc == C_GRASS) score += 20;
        return score;
    }

    // ---------------- 目标点 ----------------
    void computeGoal(const Obs& o, const Game& base) {
        hasGoal = false;
        const Player& me = base.p[seat];
        // 战术单的集结点优先；3 秒内要塌就放弃
        if (plan.hasGo && o.t < plan.until) {
            const i64 f = cell_fall_step(info(), cell_index(plan.gox), cell_index(plan.goy));
            if (f < 0 || f - o.t > 60) {
                gx = plan.gox; gy = plan.goy; hasGoal = true;
                return;
            }
        }
        if (mode == M_CONTROL) {
            int i = pt;
            if (plan.job == 2) {
                // roam：最近的"不是我们的、或圈里有敌人"的点；否则最近的敌人
                i64 best = -1, bd = 0;
                for (int j = 0; j < int(info().points.size()); ++j) {
                    bool need = (base.owner[j] != team);
                    if (!need) {
                        for (int k = 0; k < n; ++k) {
                            if (mateK[k] || k == seat || !base.p[k].alive || base.p[k].unknown) continue;
                            const Point& P = info().points[j];
                            const i64 dx = base.p[k].x - P.x, dy = base.p[k].y - P.y;
                            if (dx * dx + dy * dy <= P.r * P.r) { need = true; break; }
                        }
                    }
                    if (!need) continue;
                    const i64 d = ilen(info().points[j].x - me.x, info().points[j].y - me.y);
                    if (best < 0 || d < bd) { best = j; bd = d; }
                }
                if (best >= 0) i = int(best);
                else {
                    // 全是我们的、也没敌人：找最近的敌人
                    int ek = -1; i64 ed = 0;
                    for (int k = 0; k < n; ++k) {
                        if (!enemyK[k] || !base.p[k].alive || base.p[k].unknown) continue;
                        const i64 d = ilen(base.p[k].x - me.x, base.p[k].y - me.y);
                        if (ek < 0 || d < ed) { ek = k; ed = d; }
                    }
                    if (ek >= 0) { gx = base.p[ek].x; gy = base.p[ek].y; hasGoal = true; return; }
                }
            }
            if (i == 1 && info().points.size() > 1) {
                // B 点在小堡垒里：先到墙外缝隙对准，再直线穿过去
                const Point& B = info().points[1];
                const bool west = (team == 0);
                const i64 sy = (me.y < B.y) ? B.y - 1420 : B.y + 1420;
                const i64 sx = west ? B.x - 3600 : B.x + 3600;
                const i64 inX = west ? B.x - 2100 : B.x + 2100;
                const bool past = west ? (me.x > inX) : (me.x < inX);
                const bool pocket = iabs(me.x - B.x) < 1900 && me.y > B.y - 1600 && me.y < B.y + 1600;
                if (!past && !pocket) { gx = sx; gy = sy; hasGoal = true; return; }
                gx = B.x; gy = B.y; hasGoal = true;
                return;
            }
            if (i >= 0 && i < int(info().points.size())) {
                gx = info().points[i].x; gy = info().points[i].y; hasGoal = true;
            }
            return;
        }
        if (mode == M_ELIM) {
            const i64 cx = W / 2, cy = H / 2;
            const int side = (team == 0) ? -1 : 1;
            if (o.t < 260) {
                // 前期守在本方岸边，靠飞镖隔岩浆打；别过早挤上 1 米宽的窄桥
                gx = cx + side * 7200;
                gy = cy + (i64)(seat % 5 - 2) * 1400;
                hasGoal = true;
                return;
            }
            if (o.t < 380) {
                // 按队伍平均 y 选北桥或南桥，全队走同一座
                i64 ay = 0;
                int ac = 0;
                for (int k = 0; k < n; ++k) {
                    if ((mateK[k] || k == seat) && base.p[k].alive) { ay += base.p[k].y; ++ac; }
                }
                const i64 yy = ac ? ay / ac : cy;
                gx = cx - 500;
                gy = (yy < cy) ? 6200 : H - 6200;
                hasGoal = true;
                return;
            }
            // 湖心岩台：五个人围一点散开，但都在决战区里
            const double ang = 2.0 * 3.14159265358979 * double(seat % 5) / 5.0;
            const i64 r = 900;
            gx = cx + (i64)(std::cos(ang) * r);
            gy = cy + (i64)(std::sin(ang) * r);
            hasGoal = true;
            return;
        }
        if (mode == M_KING) {
            const bool inC1 = (me.x > 1500 && me.x < 6500 && me.y > 6000 && me.y < 11000);
            const bool inC2 = (me.x > 23500 && me.x < 28500 && me.y > 6000 && me.y < 11000);
            if (o.t >= 330 && (inC1 || inC2)) {
                // 城堡门第 420 步塌得比里面早，先出最近的门，再去中间
                gx = inC1 ? 3500 : 26500;
                gy = (me.y < 8500) ? 5300 : 11700;
                hasGoal = true;
                return;
            }
            const bool iAmKing = (seat == ownKing);
            if (iAmKing && plan.job != 6) {
                // 贴着队友走，同时朝远离最近敌人的方向偏一点；城堡快塌/队友都跑远了才单独走
                i64 cx0 = 0, cy0 = 0;
                int cn0 = 0;
                for (int k = 0; k < n; ++k) {
                    if (!mateK[k] || !base.p[k].alive) continue;
                    cx0 += base.p[k].x; cy0 += base.p[k].y; ++cn0;
                }
                i64 bx0 = info().base_x[seat], by0 = info().base_y[seat];
                if (bx0 <= 0 || by0 <= 0) { bx0 = me.x; by0 = me.y; }
                if (cn0 > 0) {
                    cx0 /= cn0; cy0 /= cn0;
                    int ek0 = -1; i64 ed0 = 0;
                    for (int k = 0; k < n; ++k) {
                        if (!enemyK[k] || !base.p[k].alive || base.p[k].unknown) continue;
                        const i64 d = ilen(base.p[k].x - cx0, base.p[k].y - cy0);
                        if (ek0 < 0 || d < ed0) { ek0 = k; ed0 = d; }
                    }
                    i64 ux0 = 0, uy0 = 0;
                    if (ek0 >= 0) unit(cx0 - base.p[ek0].x, cy0 - base.p[ek0].y, ux0, uy0);
                    i64 tx = cx0 + rdiv(ux0 * 1300, UNIT);
                    i64 ty = cy0 + rdiv(uy0 * 1300, UNIT);
                    const i64 f1 = cell_fall_step(info(), cell_index(tx), cell_index(ty));
                    if (standable(tx, ty, o.t) && (f1 < 0 || f1 - o.t > 50)) {
                        gx = tx; gy = ty; hasGoal = true;
                        return;
                    }
                    gx = cx0; gy = cy0; hasGoal = true;
                    return;
                }
                const i64 f0 = cell_fall_step(info(), cell_index(bx0), cell_index(by0));
                bool baseOk = standable(bx0, by0, o.t) && (f0 < 0 || f0 - o.t > 50);
                i64 de0 = 30000;
                bool los0 = false;
                for (int k = 0; k < n; ++k) {
                    if (!enemyK[k] || !base.p[k].alive || base.p[k].unknown) continue;
                    const i64 d = ilen(base.p[k].x - me.x, base.p[k].y - me.y);
                    if (d < de0) { de0 = d; los0 = !wall_between(base.M(), me.x, me.y, base.p[k].x, base.p[k].y); }
                }
                if (baseOk && o.t < 420 && (de0 > 4500 || !los0)) {
                    gx = bx0; gy = by0; hasGoal = true;
                    return;
                }
                // 保命：在周围采样，挑离敌人远、离塌陷远、靠近队友、有墙挡着的点
                i64 bx = me.x, by = me.y;
                double bs = -1e18;
                bool found = false;
                for (int ring = 0; ring < 3; ++ring) {
                    const i64 r = 2200 + ring * 1800;
                    for (int a = 0; a < 16; ++a) {
                        const i64 ang = a * 2250;
                        const i64 cx = me.x + rdiv(Game::icos_face(ang) * r, UNIT);
                        const i64 cy = me.y + rdiv(Game::isin_face(ang) * r, UNIT);
                        if (cx < 700 || cy < 700 || cx > W - 700 || cy > H - 700) continue;
                        if (!standable(cx, cy, o.t)) continue;
                        const i64 f = cell_fall_step(info(), cell_index(cx), cell_index(cy));
                        if (f >= 0 && f - o.t < 90) continue;
                        double s = 0;
                        i64 de = 30000;
                        for (int k = 0; k < n; ++k) {
                            if (!enemyK[k] || !base.p[k].alive || base.p[k].unknown) continue;
                            de = imin(de, ilen(base.p[k].x - cx, base.p[k].y - cy));
                        }
                        s += imin(de, 9000) * 1.0;
                        if (de < 4200) s -= (4200.0 - double(de)) * 1.5;
                        i64 dmn = 20000;
                        for (int k = 0; k < n; ++k) {
                            if (!mateK[k] || !base.p[k].alive) continue;
                            dmn = imin(dmn, ilen(base.p[k].x - cx, base.p[k].y - cy));
                        }
                        if (dmn < 20000) s -= iabs(dmn - 2600) * 0.25;
                        if (f >= 0) s += imin(f - o.t, 600) * 2.5;
                        else s += 600;
                        // 有墙挡着的方向更安全（镖和刀都过不去）
                        for (int k = 0; k < n; ++k) {
                            if (!enemyK[k] || !base.p[k].alive || base.p[k].unknown) continue;
                            const i64 d = ilen(base.p[k].x - cx, base.p[k].y - cy);
                            if (d > 7000) continue;
                            if (wall_between(base.M(), cx, cy, base.p[k].x, base.p[k].y)) s += 1800;
                        }
                        if (s > bs) { bs = s; bx = cx; by = cy; found = true; }
                    }
                }
                if (found) { gx = bx; gy = by; hasGoal = true; }
                else { gx = W / 2; gy = H / 2; hasGoal = true; }
                return;
            }
            if (!iAmKing && plan.job == 4) {
                // 刺王：直奔对方国王
                if (enemyKing >= 0 && base.p[enemyKing].alive && !base.p[enemyKing].unknown) {
                    gx = base.p[enemyKing].x; gy = base.p[enemyKing].y; hasGoal = true;
                } else {
                    gx = W / 2; gy = H / 2; hasGoal = true;
                }
                return;
            }
            if (!iAmKing) {
                // 护王：站在国王和最近敌人之间
                i64 kx = W / 2, ky = H / 2;
                if (ownKing >= 0 && base.p[ownKing].alive) { kx = base.p[ownKing].x; ky = base.p[ownKing].y; }
                int ek = -1; i64 ed = 0;
                for (int k = 0; k < n; ++k) {
                    if (!enemyK[k] || !base.p[k].alive || base.p[k].unknown) continue;
                    const i64 d = ilen(base.p[k].x - kx, base.p[k].y - ky);
                    if (ek < 0 || d < ed) { ek = k; ed = d; }
                }
                i64 ux, uy;
                if (ek >= 0 && ed < 12000) unit(base.p[ek].x - kx, base.p[ek].y - ky, ux, uy);
                else unit(W / 2 - kx, H / 2 - ky, ux, uy);
                gx = kx + rdiv(ux * 1500, UNIT);
                gy = ky + rdiv(uy * 1500, UNIT);
                if (ilen(gx - me.x, gy - me.y) < 500) { gx = kx; gy = ky; }
                hasGoal = true;
                return;
            }
            // 国王 fight：像普通队员，但别靠近坑
            gx = W / 2; gy = H / 2; hasGoal = true;
            return;
        }
        // 1v1 / 四人局：去中间
        gx = W / 2; gy = H / 2; hasGoal = true;
    }

    void computeStepGoal(const Obs& o) {
        hasStep = false;
        hasEvalGoal = false;
        if (!hasGoal) return;
        sgx = gx; sgy = gy;
        const Player& me = o.me();
        if (ilen(gx - me.x, gy - me.y) > 1200) {
            i64 wx, wy;
            if (path_next(info(), me.x, me.y, gx, gy, o.t, wx, wy)) { sgx = wx; sgy = wy; }
        }
        // B 点：站到 (墙外 3.6m, 8.58/11.42) 后，笔直朝东/西穿过 0.1 米宽的可走线
        if (mode == M_CONTROL && pt == 1 && info().points.size() > 1) {
            const Point& B = info().points[1];
            const bool west = (team == 0);
            const i64 sy = (me.y < B.y) ? B.y - 1420 : B.y + 1420;
            const i64 sx = west ? B.x - 3600 : B.x + 3600;
            const i64 inX = west ? B.x - 2100 : B.x + 2100;
            const bool past = west ? (me.x > inX) : (me.x < inX);
            if (!past && iabs(me.x - sx) < 260 && iabs(me.y - sy) < 55) {
                sgx = west ? B.x + 2200 : B.x - 2200;
                sgy = sy;
                egx = sgx; egy = sgy;
            }
        }
        egx = sgx; egy = sgy;
        hasStep = true;
        hasEvalGoal = true;
    }

    bool timeUp() {
        if (cpuStart <= 0) return false;
        const i64 now = thread_cpu_100ns();
        if (now < 0) return false;
        return now - cpuStart > 48000;   // 4.8 ms
    }

    // ---------------- 主决策 ----------------
    Action decide(const Obs& o) {
        Action res;
        cpuStart = (o.t > 0) ? thread_cpu_100ns() : 0;
        cpuLimited = false;
        recordSelf(o);
        if (!o.me().alive) {
            if (o.t < (i64)myA.size()) myA[o.t] = res;
            return res;
        }
        recvMsgs(o);
        for (int k = 0; k < n; ++k) obsMateAlive[k] = mateK[k] && o.p[k].alive;
        Game base;
        buildWorld(o, base);
        base.hist.reset();
        base.over = false;
        applyMates(base, o);
        fillHidden(o, base);

        computeGoal(o, base);
        computeStepGoal(o);

        const int HH = 12;
        Cand best[MAXCAND];
        int nc = 0;
        auto push = [&](const Action& a, double sc, int kind) {
            if (nc < MAXCAND) { best[nc].a = a; best[nc].score = sc; best[nc].kind = kind; ++nc; }
        };
        auto evalPush = [&](const Action& a, int kind, double add = 0) {
            const double sc = evalAction(base, a, HH) + double(rnd() % 1000) * 0.001 + add;
            push(a, sc, kind);
        };

        i64 ux = 0, uy = 0;
        i64 gdx = 0, gdy = 0;
        if (hasStep) unit(sgx - o.me().x, sgy - o.me().y, gdx, gdy);
        if (gdx == 0 && gdy == 0) { gdx = 1; gdy = 0; }

        // 最近敌人 / 最近敌人飞镖
        int nearE = -1; i64 nearED = 0;
        for (int k = 0; k < n; ++k) {
            if (!enemyK[k] || !base.p[k].alive || base.p[k].unknown) continue;
            const i64 d = ilen(base.p[k].x - o.me().x, base.p[k].y - o.me().y);
            if (nearE < 0 || d < nearED) { nearE = k; nearED = d; }
        }

        // ---------- 移动候选：16 方向 + 停 + 目标 + 敌人/镖的逃逸方向 ----------
        i64 dirs[32][2];
        int ndir = 0;
        for (int i = 0; i < 16; ++i) {
            const i64 ang = i * 2250;
            dirs[ndir][0] = Game::icos_face(ang);
            dirs[ndir][1] = Game::isin_face(ang);
            ++ndir;
        }
        dirs[ndir][0] = 0; dirs[ndir][1] = 0; ++ndir;
        if (hasStep) { dirs[ndir][0] = gdx; dirs[ndir][1] = gdy; ++ndir; }
        int strafeStart = ndir;
        if (mode == M_CONTROL && pt >= 0 && pt < int(info().points.size())) {
            const Point& P = info().points[pt];
            i64 rx = o.me().x - P.x, ry = o.me().y - P.y;
            if (ilen(rx, ry) < P.r + 700) {
                unit(rx, ry, ux, uy);
                dirs[ndir][0] = -uy; dirs[ndir][1] = ux; ++ndir;
                if (ndir < 31) { dirs[ndir][0] = uy; dirs[ndir][1] = -ux; ++ndir; }
            }
        }
        if (nearE >= 0 && ndir < 31) {
            unit(base.p[nearE].x - o.me().x, base.p[nearE].y - o.me().y, ux, uy);
            dirs[ndir][0] = ux; dirs[ndir][1] = uy; ++ndir;
            if (ndir < 31) { dirs[ndir][0] = -ux; dirs[ndir][1] = -uy; ++ndir; }
        }
        const Dart& myd = o.my_dart();
        int pickIdx = -1;
        bool pickSafe = false;
        if (myd.st == D_GROUND && ndir < 31) {
            unit(myd.x - o.me().x, myd.y - o.me().y, ux, uy);
            pickIdx = ndir;
            dirs[ndir][0] = ux; dirs[ndir][1] = uy; ++ndir;
            pickSafe = ilen(myd.x - o.me().x, myd.y - o.me().y) <= 3200;
            for (int k = 0; k < n; ++k) {
                if (!enemyK[k] || !base.p[k].alive || base.p[k].unknown || base.d[k].st != D_HAND) continue;
                if (ilen(base.p[k].x - myd.x, base.p[k].y - myd.y) >= 6500) continue;
                if (!wall_between(base.M(), myd.x, myd.y, base.p[k].x, base.p[k].y)) { pickSafe = false; break; }
            }
        }
        // 躲敌方飞镖：垂直于镖路的方向 + 远离
        for (int k = 0; k < n && ndir < 31; ++k) {
            if (!enemyK[k] || !dart_flying(base.d[k].st)) continue;
            const Dart& D = base.d[k];
            i64 vx = D.vx, vy = D.vy;
            if (D.st == D_OUT) {
                const i64 sp = dart_step_len(cf, D);
                unit(D.vx, D.vy, ux, uy);
                vx = ux * sp; vy = uy * sp;
            }
            unit(-vy, vx, ux, uy);
            dirs[ndir][0] = ux; dirs[ndir][1] = uy; ++ndir;
            if (ndir < 31) { dirs[ndir][0] = -ux; dirs[ndir][1] = -uy; ++ndir; }
            if (ndir < 31) { unit(o.me().x - D.x, o.me().y - D.y, ux, uy); dirs[ndir][0] = ux; dirs[ndir][1] = uy; ++ndir; }
        }
        for (int i = 0; i < ndir; ++i) {
            if ((i & 3) == 3 && timeUp()) { cpuLimited = true; break; }
            Action a;
            a.mx = dirs[i][0];
            a.my = dirs[i][1];
            evalPush(a, 0, ((i >= strafeStart && nearE >= 0 && nearED < 7000) ? 260.0 : 0.0) + ((i == pickIdx && pickSafe) ? 900.0 : 0.0));
        }

        // ---------- 近战 ----------
        Paths paths;
        predictPaths(base, paths);
        if (o.me().st == S_FREE && !timeUp()) {
            for (int k = 0; k < n; ++k) {
                if (timeUp()) { cpuLimited = true; break; }
                if (!enemyK[k] || !base.p[k].alive || base.p[k].unknown) continue;
                if (mode == M_KING && k == enemyKing && base.p[k].inv > 0) continue;
                for (int j = 1; j <= 2; ++j) {
                    if (!paths.alive[k][j]) continue;
                    const i64 dx = paths.x[k][j] - o.me().x, dy = paths.y[k][j] - o.me().y;
                    if (ilen(dx, dy) > cf.slash_reach + 500) continue;
                    i64 ax, ay;
                    unit(dx, dy, ax, ay);
                    if (!meleeSafe(base, ax, ay)) continue;
                    if (mateNearPoint(base, paths.x[k][j], paths.y[k][j], 1200)) continue;
                    Action a;
                    a.act = A_MELEE;
                    a.ax = ax; a.ay = ay;
                    a.mx = gdx; a.my = gdy;
                    evalPush(a, 2);
                }
            }
            // 格挡：朝 2 步后敌方飞镖的位置
            for (int k = 0; k < n && !timeUp(); ++k) {
                if (!enemyK[k] || !dart_flying(base.d[k].st)) continue;
                const Dart& D = base.d[k];
                i64 px = D.x, py = D.y;
                if (D.st == D_OUT) {
                    i64 vx, vy;
                    unit(D.vx, D.vy, vx, vy);
                    i64 sp = dart_step_len(cf, D);
                    px += rdiv(vx * sp, UNIT);
                    py += rdiv(vy * sp, UNIT);
                    sp = imax(cf.dart_vmin, cf.dart_v - (D.age + 1) * cf.dart_dec);
                    px += rdiv(vx * sp, UNIT);
                    py += rdiv(vy * sp, UNIT);
                } else { px += D.vx * 2; py += D.vy * 2; }
                const i64 dx = px - o.me().x, dy = py - o.me().y;
                if (ilen(dx, dy) > cf.slash_reach) continue;
                i64 ax, ay;
                unit(dx, dy, ax, ay);
                if (!meleeSafe(base, ax, ay)) continue;
                Action a;
                a.act = A_MELEE;
                a.ax = ax; a.ay = ay;
                a.mx = gdx; a.my = gdy;
                evalPush(a, 2);
            }
        }

        // ---------- 扔镖 ----------
        if (o.me().st == S_FREE && o.my_dart().st == D_HAND && !timeUp()) {
            int order[MAXP], no = 0;
            for (int k = 0; k < n; ++k) {
                if (!enemyK[k] || !base.p[k].alive || base.p[k].unknown) continue;
                if (mode == M_KING && k == enemyKing && base.p[k].inv > 0) continue;
                order[no++] = k;
            }
            // 优先圈里的、能打的、近的
            for (int a = 0; a < no; ++a)
                for (int b = a + 1; b < no; ++b) {
                    auto pri = [&](int k) {
                        double p = 0;
                        if (mode == M_CONTROL && pt >= 0) {
                            const Point& P = info().points[pt];
                            const i64 dx = base.p[k].x - P.x, dy = base.p[k].y - P.y;
                            if (dx * dx + dy * dy <= P.r * P.r) p += 1500;
                        }
                        if (base.p[k].st != S_FREE) p += 1000;
                        if (base.d[k].st != D_HAND) p += 500;
                        p -= double(ilen(base.p[k].x - o.me().x, base.p[k].y - o.me().y)) * 0.05;
                        return p;
                    };
                    if (pri(order[b]) > pri(order[a])) std::swap(order[a], order[b]);
                }
            int tried = 0;
            for (int oi = 0; oi < no && tried < 12; ++oi) {
                if (timeUp()) { cpuLimited = true; break; }
                const int k = order[oi];
                const i64 D0 = ilen(base.p[k].x - o.me().x, base.p[k].y - o.me().y);
                if (D0 > cf.range - 100) continue;
                i64 cum = 0;
                int arrive = 2;
                for (int i = 0; i < 16; ++i) {
                    cum += imax(cf.dart_vmin, cf.dart_v - i * cf.dart_dec);
                    if (cum >= D0) { arrive = 2 + i; break; }
                    arrive = 2 + i + 1;
                }
                for (int dj = -2; dj <= 2 && tried < 12; ++dj) {
                    const int j = arrive + dj;
                    if (j < 2 || j > 16) continue;
                    if (!paths.alive[k][j]) continue;
                    const i64 dx = paths.x[k][j] - o.me().x, dy = paths.y[k][j] - o.me().y;
                    const i64 dd = ilen(dx, dy);
                    if (dd < 500 || dd > cf.range) continue;
                    {
                        const bool busy = (base.p[k].st != S_FREE);
                        const bool unarmedFoe = (base.d[k].st != D_HAND);
                        const bool okThrow = (dd <= 4500 && busy) || (dd <= 3000) || (unarmedFoe && dd <= 5000);
                        if (!okThrow) continue;
                        // 对手持镖、自由状态、正对着我：一刀就能挡落
                        if (base.d[k].st == D_HAND && base.p[k].st == S_FREE) {
                            const i64 fx = Game::icos_face(base.p[k].face), fy = Game::isin_face(base.p[k].face);
                            const i64 rx = o.me().x - base.p[k].x, ry = o.me().y - base.p[k].y;
                            if (rx * fx + ry * fy > 820 * imax((i64)1, ilen(rx, ry))) continue;
                        }
                    }
                    i64 ax, ay;
                    unit(dx, dy, ax, ay);
                    if (wall_between(base.M(), o.me().x, o.me().y, paths.x[k][j], paths.y[k][j])) continue;
                    if (!throwSafe(base, ax, ay)) continue;
                    Action a;
                    a.act = A_THROW;
                    a.ax = ax; a.ay = ay;
                    a.mx = gdx; a.my = gdy;
                    evalPush(a, 1);
                    ++tried;
                }
            }
        }

        // ---------- 冲刺 ----------
        if (o.me().st == S_FREE && o.me().cd == 0 && !timeUp() && !(mode == M_CONTROL && nearE >= 0 && nearED < 6000)) {
            i64 dashes[6][2];
            int nd2 = 0;
            dashes[nd2][0] = gdx; dashes[nd2][1] = gdy; ++nd2;
            if (nearE >= 0 && nd2 < 6) {
                unit(base.p[nearE].x - o.me().x, base.p[nearE].y - o.me().y, ux, uy);
                dashes[nd2][0] = ux; dashes[nd2][1] = uy; ++nd2;
            }
            for (int i = 0; i < 3 && nd2 < 6; ++i) {
                const i64 ang = i * 12000;
                dashes[nd2][0] = Game::icos_face(ang);
                dashes[nd2][1] = Game::isin_face(ang);
                ++nd2;
            }
            for (int i = 0; i < nd2; ++i) {
                Action a;
                a.act = A_DASH;
                a.ax = dashes[i][0]; a.ay = dashes[i][1];
                evalPush(a, 3);
                // 冲刺后有一小段不能动，平时别乱冲
                best[nc - 1].score -= (mode == M_CONTROL ? 260.0 : 45.0);
            }
        }

        // ---------- 召回 ----------
        if (o.me().st == S_FREE && o.my_dart().st == D_GROUND && cf.recall && !timeUp()) {
            const i64 ff = cell_fall_step(info(), cell_index(o.me().x), cell_index(o.me().y));
            const bool cellOk = (ff < 0 || ff - o.t >= 20);
            bool enemyFar = true;
            for (int k = 0; k < n; ++k) {
                if (!enemyK[k] || !base.p[k].alive || base.p[k].unknown) continue;
                if (base.d[k].st != D_HAND) continue;
                if (ilen(base.p[k].x - o.me().x, base.p[k].y - o.me().y) >= 6000) continue;
                if (!wall_between(base.M(), o.me().x, o.me().y, base.p[k].x, base.p[k].y)) { enemyFar = false; break; }
            }
            if (cellOk && enemyFar) {
                Action a;
                a.act = A_RECALL;
                evalPush(a, 4);
            }
        }

        int bi = 0;
        for (int i = 1; i < nc; ++i)
            if (best[i].score > best[bi].score) bi = i;
        res = best[bi].a;
        // MSG：活着每步发位置；扔镖那步发镖路终点
        i64 qx = iclamp(o.me().x / 250, (i64)0, (i64)127);
        i64 qy = iclamp(o.me().y / 250, (i64)0, (i64)127);
        i64 F = 1;
        if (res.act == A_THROW) {
            i64 ax, ay;
            if (scale_to(res.ax, res.ay, UNIT, ax, ay)) {
                i64 ex = o.me().x + rdiv(ax * cf.range, UNIT);
                i64 ey = o.me().y + rdiv(ay * cf.range, UNIT);
                ex = iclamp(ex, (i64)300, W - 300);
                ey = iclamp(ey, (i64)300, H - 300);
                qx = iclamp(ex / 250, (i64)0, (i64)127);
                qy = iclamp(ey / 250, (i64)0, (i64)127);
                F = 2;
            }
        }
        res.msg = qx + 128 * qy + 16384 * F;
        if (res.msg > cf.msg_max) res.msg = cf.msg_max;
        if (o.t < (i64)myA.size()) myA[o.t] = res;
        if (o.t < 6) {
            fprintf(stderr, "t=%lld mode=%d goal=(%lld,%lld) step=(%lld,%lld) best=%d act=%lld score=%.0f nc=%d\n",
                    (long long)o.t, mode, (long long)gx, (long long)gy, (long long)sgx, (long long)sgy,
                    best[bi].kind, (long long)res.act, best[bi].score, nc);
        }
        return res;
    }

    i64 rnd() {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 7;
        rngState ^= rngState << 17;
        return (i64)(rngState & 0x7FFFFFFF);
    }
};

DART_MAIN(Bot)
