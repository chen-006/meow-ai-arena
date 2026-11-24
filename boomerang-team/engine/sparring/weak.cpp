// 地图格子尺寸用 dart::CELL / cell_index / cell_center；1000作方向单位或每米毫米数，勿当格子边长。
// weak.cpp —— 弱档陪练（源码公开）。
// 比模板多做的事：近身挥砍、空手时踢；镖掉了去捡（远或捡不到时召回）；看到飞镖直直朝自己来就往侧面闪；塌陷前往中心走；不往坑里走；
// 隔着坑走不到时冲刺过去；两人的镖都在地上时冲刺去抢自己的镖。观察里的对手是 DELAY 步以前的样子，这里直接当成现在的用。
// 仍然不预判（朝对手现在的位置扔），也不会算回程的镖。
#include "objective.hpp"
using namespace dart;

struct Weak {
    Action decide(const Obs& o) {
        const GameInfo& gi = info();
        const Config& cf = gi.cfg;
        const Player& me = o.me();
        Action a;
        if (!me.alive) return a;
        const Dart& md = o.my_dart();

        int tgt = -1;
        i64 best = 0;
        for (int i = 1; i < o.n; ++i) {  // 从我的下一个座位开始数（距离相同时不偏向小号座位）
            const int k = (o.seat + i) % o.n;
            if (is_mate(gi, k) || !o.p[k].alive || !o.p[k].vis) continue;
            const i64 dd = ilen(o.p[k].x - me.x, o.p[k].y - me.y);
            if (tgt < 0 || dd < best) {
                tgt = k;
                best = dd;
            }
        }

        // 目的地
        const i64 midx = gi.M().width() / 2, midy = gi.M().height() / 2;  // 地图中心
        i64 gx = midx, gy = midy;
        const i64 fall = cell_fall_step(gi, cell_index(me.x), cell_index(me.y));
        if (fall >= 0 && fall - o.t < 60) {
            gx = midx;  // 脚下快塌了：去中心
            gy = midy;
        } else if (md.st == D_GROUND) {
            gx = md.x;
            gy = md.y;
        } else if (tgt >= 0) {
            gx = o.p[tgt].x;
            gy = o.p[tgt].y;
            if (md.st != D_HAND && best < 3000) {  // 空手：别贴太近，往反方向退
                gx = me.x - (o.p[tgt].x - me.x);
                gy = me.y - (o.p[tgt].y - me.y);
            }
        }
        if (fall < 0 || fall - o.t >= 60) team_goal(o, gx, gy);
        i64 wx, wy;
        bool walk = true;
        if (path_next(gi, me.x, me.y, gx, gy, o.t, wx, wy)) {
            a.mx = wx - me.x;
            a.my = wy - me.y;
        } else {
            walk = false;
        }
        // 两人都空手（镖撞掉了）、我的镖在 1.5～3.5 米外：冲刺过去抢（冲刺中经过就能捡起来）
        if (md.st == D_GROUND && me.st == S_FREE && me.cd == 0 && tgt >= 0 && o.d[tgt].st != D_HAND) {
            const i64 dd = ilen(md.x - me.x, md.y - me.y);
            i64 ux, uy;
            scale_to(md.x - me.x, md.y - me.y, cf.dash_v * cf.dash_t, ux, uy);
            if (dd > 1500 && dd < 3500 && standable(gi, me.x + ux, me.y + uy, o.t + cf.dash_pre + cf.dash_t + 2)) {
                a.act = A_DASH;
                a.ax = md.x - me.x;
                a.ay = md.y - me.y;
                return a;
            }
        }
        // 走不到（隔着坑）：朝目的地冲刺（停下的地方站得住才冲）
        if (!walk && me.st == S_FREE && me.cd == 0) {
            i64 ux, uy, vx, vy;
            scale_to(gx - me.x, gy - me.y, cf.dash_v * cf.dash_t, ux, uy);
            scale_to(gx - me.x, gy - me.y, cf.dash_v * cf.dash_t + 300, vx, vy);
            if (standable(gi, me.x + ux, me.y + uy, o.t + cf.dash_pre + cf.dash_t + 3) && standable(gi, me.x + vx, me.y + vy, o.t + cf.dash_pre + cf.dash_t + 3)) {
                a.act = A_DASH;
                a.ax = gx - me.x;
                a.ay = gy - me.y;
                return a;
            }
        }

        bool danger_any = false;
        // 闪避：别人的镖直线飞过来，6 步内会离我很近 → 往垂直方向跑
        for (int k = 0; k < o.n; ++k) {
            if (k == o.seat) continue;
            const Dart& d = o.d[k];
            if (!dart_flying(d.st)) continue;
            if (d.vx == 0 && d.vy == 0) continue;
            // 去程的 vx vy 是方向（单位向量 × 1000）：乘这一步的速度才是位移；回程 / 召回飞行的 vx vy 是上一步的位移
            i64 dvx = d.vx, dvy = d.vy;
            if (d.st == D_OUT) {
                dvx = d.vx * dart_step_len(cf, d) / 1000;
                dvy = d.vy * dart_step_len(cf, d) / 1000;
            }
            bool danger = false;
            for (int s = 1; s <= 6 && !danger; ++s) {
                const i64 px = d.x + dvx * s, py = d.y + dvy * s;
                if (ilen(px - me.x, py - me.y) < cf.body_r + cf.dart_r + 300) danger = true;
            }
            if (danger) {
                danger_any = true;
                // 垂直方向两个里选离镖的直线更远的那个
                i64 px = -d.vy, py = d.vx;
                const i64 side = (me.x - d.x) * px + (me.y - d.y) * py;
                if (side < 0) {
                    px = -px;
                    py = -py;
                }
                a.mx = px;
                a.my = py;
            }
        }
        // 不往坑里走：下一步的位置如果是坑就停下
        {
            i64 vx = a.mx, vy = a.my;
            scale_to(vx, vy, cf.run_v * 3, vx, vy);
            if (cell_of_point(gi.cfg, gi.M(), me.x + vx, me.y + vy, o.t + 3) == C_PIT) a.mx = a.my = 0;
        }

        // 召回：引导中没危险就站着不动；镖离得远或落在坑上、附近没人时开始召回
        if (me.st == S_RECALL && !danger_any) return Action();
        if (cf.recall && md.st == D_GROUND && me.st == S_FREE && !danger_any && (tgt < 0 || best > 4500) &&
            (ilen(md.x - me.x, md.y - me.y) > 3000 || !standable(gi, md.x, md.y, o.t))) {
            Action r;
            r.act = A_RECALL;
            return r;
        }
        if (tgt >= 0 && me.st == S_FREE) {
            const Player& e = o.p[tgt];
            const bool clear = !wall_between(gi.M(), me.x, me.y, e.x, e.y);
            if (clear && best < cf.slash_reach - 100) {  // 近身：挥砍（有镖）或踢（空手）
                a.act = A_MELEE;
                a.ax = e.x - me.x;
                a.ay = e.y - me.y;
            } else if (clear && md.st == D_HAND && best < 4500) {
                a.act = A_THROW;
                a.ax = e.x - me.x;
                a.ay = e.y - me.y;
            }
        }
        return a;
    }
};

DART_MAIN(Weak)

