#include "dart.hpp"
using namespace dart;

struct StarterBot {
    Action decide(const Obs& o) {
        const GameInfo& gi = info();
        const Player& me = o.me();
        Action a;
        if (!me.alive) return a;
        const Dart& md = o.my_dart();

        int tgt = -1;
        i64 best = 1LL << 60;
        for (int k = 0; k < o.n; ++k) {
            if (k == o.seat || !o.p[k].alive || !o.p[k].vis) continue;
            i64 dd = ilen(o.p[k].x - me.x, o.p[k].y - me.y);
            if (dd < best) { best = dd; tgt = k; }
        }

        i64 gx = gi.M().width() / 2, gy = gi.M().height() / 2;
        if (md.st == D_GROUND) {
            gx = md.x; gy = md.y;
        } else if (tgt >= 0) {
            gx = o.p[tgt].x; gy = o.p[tgt].y;
        }
        i64 wx = gx, wy = gy;
        if (path_next(gi, me.x, me.y, gx, gy, o.t, wx, wy)) {
            a.mx = wx - me.x;
            a.my = wy - me.y;
        }
        if (md.st == D_HAND && tgt >= 0 && best < 6500 && !wall_between(gi.M(), me.x, me.y, o.p[tgt].x, o.p[tgt].y)) {
            a.act = A_THROW;
            a.ax = o.p[tgt].x - me.x;
            a.ay = o.p[tgt].y - me.y;
        } else if (md.st == D_GROUND && ilen(md.x - me.x, md.y - me.y) > 3500) {
            a.act = A_RECALL;
        }
        return a;
    }
};

DART_MAIN(StarterBot)
