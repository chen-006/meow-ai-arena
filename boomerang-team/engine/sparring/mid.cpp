// 地图格子尺寸用 dart::CELL / cell_index / cell_center；1000作方向单位或每米毫米数，勿当格子边长。
// mid.cpp —— 中档陪练（源码公开）。
// 比弱档多做的事：
//   1. 扔镖时按对手现在的速度做直线预判（假设他不变速），只在中间没墙、距离合适时扔；
//   2. 躲闪：把每把飞着的镖按"现在的速度一直直线飞"往后推 10 步，看会不会离我很近；
//      会的话，在 16 个方向里挑一个"推完 10 步后离这些直线最远"、脚下又站得住的方向跑；
//   3. 有镖时和对手保持 3.5～5 米（这个距离上横着来回走），空手时往后退；近身就挥砍 / 踢；
//   4. 镖在地上：近、或者对手也空手（两把镖撞掉了）就走过去捡，远（或落在坑上）而且附近没人时召回；
//   5. 目的地隔着坑走不到时，朝它冲刺过去（停下的地方站得住才冲）；
//   6. 对手的镖朝我飞来、我手里有镖、他在 7 米内：朝他回扔一镖（镖撞镖的话撞点离我近，我先捡到）；
//   7. 两人都空手、我的镖在 1.5～3.5 米外：冲刺过去抢。
// 反应延迟：观察里的对手和他的镖是 DELAY 步以前的样子，这里直接当成现在的用（最笨的做法）。
// 没做的（留给选手）：反应延迟的补偿（把对手往前推）、对手的蓄势（只躲已经飞出来的镖，看不到正在蓄势的人——所以离得近时扔来的镖它基本躲不开）、
//   回程的镖会拐弯（这里按直线算）、对手会怎么动、用 dart::Game 推演、
//   带着自己的镖拐弯去打人、把人踢进坑、抓对手的硬直、草丛、队友配合……
#include "objective.hpp"
using namespace dart;

struct Mid {
    // 镖下一步的位移：去程的 vx vy 是方向（单位向量 × 1000），乘上这一步的速度；回程 / 召回飞行的 vx vy 就是上一步的位移
    static void dart_vel(const Config& cf, const Dart& d, i64& vx, i64& vy) {
        if (d.st == D_OUT) {
            const i64 v = dart_step_len(cf, d);
            vx = d.vx * v / 1000;
            vy = d.vy * v / 1000;
        } else {
            vx = d.vx;
            vy = d.vy;
        }
    }
    // 点 (px, py) 在未来 1..steps 步里离镖 d（按现在的速度直线飞）最近的距离
    static i64 closest(const Config& cf, const Dart& d, i64 px, i64 py, int steps) {
        i64 vx, vy;
        dart_vel(cf, d, vx, vy);
        i64 best = ilen(d.x - px, d.y - py);
        for (int s = 1; s <= steps; ++s) best = imin(best, ilen(d.x + vx * s - px, d.y + vy * s - py));
        return best;
    }

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

        // —— 想去哪
        const i64 midx = gi.M().width() / 2, midy = gi.M().height() / 2;  // 地图中心
        i64 gx = midx, gy = midy;
        const i64 fall = cell_fall_step(gi, cell_index(me.x), cell_index(me.y));
        bool want_recall = false;
        if (fall >= 0 && fall - o.t < 60) {
            // 脚下快塌了：去最晚塌（或永不塌）的格子里最近的那个
            i64 bestv = -1;
            for (int y = 0; y < gi.M().ny; ++y)
                for (int x = 0; x < gi.M().nx; ++x) {
                    if (!standable(gi, cell_center(x), cell_center(y), o.t)) continue;
                    const i64 f = cell_fall_step(gi, x, y);
                    const i64 v = (f < 0 ? 100000 : f) * 100 - ilen(cell_center(x) - me.x, cell_center(y) - me.y) / 100;
                    if (v > bestv) {
                        bestv = v;
                        gx = cell_center(x);
                        gy = cell_center(y);
                    }
                }
        } else if (md.st == D_GROUND && standable(gi, md.x, md.y, o.t) &&
                   (ilen(md.x - me.x, md.y - me.y) < 3000 || (tgt >= 0 && o.d[tgt].st != D_HAND))) {  // 镖近，或者对手也空手（比如两把镖撞掉了）：去捡
            gx = md.x;
            gy = md.y;
        } else if (tgt >= 0) {
            const Player& e = o.p[tgt];
            if (md.st == D_GROUND) want_recall = best > 4500;
            if (md.st == D_HAND) {
                if (best > 5000 || wall_between(gi.M(), me.x, me.y, e.x, e.y)) {
                    gx = e.x;
                    gy = e.y;
                } else if (best < 3500) {
                    gx = me.x - (e.x - me.x);
                    gy = me.y - (e.y - me.y);
                } else {
                    // 距离合适：横着走（每 0.6 秒换一次方向），别站着当靶子
                    const i64 side = (o.t / 12) % 2 ? 1 : -1;
                    gx = me.x - (e.y - me.y) * side;
                    gy = me.y + (e.x - me.x) * side;
                }
            } else {
                gx = me.x - (e.x - me.x);
                gy = me.y - (e.y - me.y);
            }
        } else if (md.st == D_GROUND) {
            want_recall = true;
        }
        if (fall < 0 || fall - o.t >= 60) team_goal(o, gx, gy);
        i64 dx = 0, dy = 0;
        bool walk = true;  // 走得到目的地吗（隔着坑就走不到）
        if (gx != me.x || gy != me.y) {
            i64 wx, wy;
            if (path_next(gi, me.x, me.y, gx, gy, o.t, wx, wy)) {
                dx = wx - me.x;
                dy = wy - me.y;
            } else {
                walk = false;
            }
        }

        // —— 危险的镖：按直线推 10 步会离我 1.2 米以内
        const i64 R = cf.body_r + cf.dart_r;
        std::vector<int> threats;
        for (int k = 0; k < o.n; ++k)
            if (k != o.seat && dart_flying(o.d[k].st) && closest(cf, o.d[k], me.x, me.y, 10) < R + 550) threats.push_back(k);
        if (!threats.empty() && md.st == D_HAND && me.st == S_FREE && tgt >= 0 && best < 7000 &&
            !wall_between(gi.M(), me.x, me.y, o.p[tgt].x, o.p[tgt].y)) {
            // 对手的镖朝我飞来、我手里有镖：朝他回扔一镖（两把镖在空中相撞就双双落地，撞点离后扔的我更近）
            for (int k : threats)
                if (k == tgt && o.d[k].st == D_OUT) {
                    a.act = A_THROW;
                    a.ax = o.p[tgt].x - me.x;
                    a.ay = o.p[tgt].y - me.y;
                    return a;
                }
        }
        if (!threats.empty()) {
            // 16 个方向 + 站着：按全速跑 10 步的位置，离所有危险镖的直线最近距离最大的那个（站不住的地方不要）
            i64 bestv = -1;
            for (int i = 0; i <= 16; ++i) {
                i64 vx = 0, vy = 0;
                if (i < 16) {
                    vx = Game::icos_face(i * 2250);
                    vy = Game::isin_face(i * 2250);
                }
                i64 worst = 1LL << 40;
                for (int s = 1; s <= 10; ++s) {
                    const i64 px = me.x + vx * cf.run_v * s / 1000, py = me.y + vy * cf.run_v * s / 1000;
                    for (int k : threats) {
                        const Dart& d = o.d[k];
                        i64 dvx, dvy;
                        dart_vel(cf, d, dvx, dvy);
                        worst = imin(worst, ilen(d.x + dvx * s - px, d.y + dvy * s - py));
                    }
                }
                const i64 px = me.x + vx * cf.run_v * 4 / 1000, py = me.y + vy * cf.run_v * 4 / 1000;
                if (!standable(gi, px, py, o.t + 4)) continue;
                if (worst > bestv) {
                    bestv = worst;
                    dx = vx;
                    dy = vy;
                }
            }
            a.mx = dx;
            a.my = dy;
            return a;  // 躲的时候不出手
        }
        // 抢镖：镖在 1.5～3.5 米外的地上、对手也空手（两把镖撞掉了）：冲刺过去（冲刺中经过就能捡起来）
        if (md.st == D_GROUND && me.st == S_FREE && me.cd == 0 && tgt >= 0 && o.d[tgt].st != D_HAND) {
            const i64 dd = ilen(md.x - me.x, md.y - me.y);
            i64 ux, uy;
            scale_to(md.x - me.x, md.y - me.y, cf.dash_v * cf.dash_t, ux, uy);
            if (dd > 1500 && dd < 3500 && standable(gi, me.x + ux, me.y + uy, o.t + cf.dash_pre + cf.dash_t + 2) && !wall_between(gi.M(), me.x, me.y, md.x, md.y)) {
                a.act = A_DASH;
                a.ax = md.x - me.x;
                a.ay = md.y - me.y;
                return a;
            }
        }
        // 走不到（隔着坑）：冷却好了就朝目的地冲刺过去（冲刺停下的地方、再往前 0.3 米都要站得住）
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
        // 不往坑里走：往前 1～3 步的地方都要站得住
        for (int k = 1; k <= 3 && (dx || dy); ++k) {
            i64 vx, vy;
            scale_to(dx, dy, cf.run_v * k, vx, vy);
            if (cell_of_point(gi.cfg, gi.M(), me.x + vx, me.y + vy, o.t + k) == C_PIT) dx = dy = 0;  // 墙不算（会被墙挡住，不会掉下去）
        }
        a.mx = dx;
        a.my = dy;

        // —— 召回：引导中没危险就站着；镖远、附近没人时开始
        if (cf.recall && me.st == S_RECALL) return Action();
        if (cf.recall && want_recall && me.st == S_FREE && (tgt < 0 || best > 4500)) {
            Action r;
            r.act = A_RECALL;
            return r;
        }

        // —— 出手
        if (tgt >= 0 && me.st == S_FREE) {
            const Player& e = o.p[tgt];
            const bool clear = !wall_between(gi.M(), me.x, me.y, e.x, e.y);
            const i64 reach = md.st == D_HAND ? cf.slash_reach : cf.kick_reach;
            if (clear && best < reach - 200) {
                a.act = A_MELEE;
                a.ax = e.x - me.x;
                a.ay = e.y - me.y;
            } else if (md.st == D_HAND && clear) {
                // 直线预判：出镖在 THROW_PRE 步之后，镖再飞 k 步
                i64 flown = 0;  // 镖出手后 k 步飞了多远（去程先快后慢）
                for (int k = 1; k <= 20; ++k) {
                    Dart tmp;
                    tmp.st = D_OUT;
                    tmp.age = k - 1;
                    flown += dart_step_len(cf, tmp);
                    const i64 ts = cf.throw_pre - 1 + k;
                    const i64 px = e.x + e.vx * ts, py = e.y + e.vy * ts;
                    const i64 dist = ilen(px - me.x, py - me.y);
                    if (dist <= flown) {
                        if (dist < 5500 && !wall_between(gi.M(), me.x, me.y, px, py)) {
                            a.act = A_THROW;
                            a.ax = px - me.x;
                            a.ay = py - me.y;
                        }
                        break;
                    }
                }
            }
        }
        return a;
    }
};

DART_MAIN(Mid)

