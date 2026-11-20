// template.cpp —— 最简单的合法程序（很弱）。从它改起：copy template.cpp player.cpp
// 做的事：走向最近的看得见的对手，进了 5 米、中间没墙就朝他（观察里的）位置扔镖；镖掉在地上就去捡。
// 不躲、不预判、不近战、不冲刺。观察里的对手是 4 步（0.2 秒）以前的样子，这里直接当成现在用，
// 是最笨的做法（见 README 第 6 节"反应延迟下怎么推演"）。
// 地图格子边长用 dart::CELL、cell_index、cell_center 换算（0.5 米）；1000 是每米的毫米数和方向单位，不是格子边长。
//
// 编译：python build.py player.cpp      检查：python match.py check player.cpp
// 规则见 RULES.md；可用的结构和工具函数见 dart.hpp 末尾的"工具函数"一节。
#include "dart.hpp"
using namespace dart;

struct Bot {
    // init 可以不写；开局时调用一次（第一步的 CPU 预算更宽）
    void init(const GameInfo& gi) { (void)gi; }

    Action decide(const Obs& o) {
        const GameInfo& gi = info();
        const Player& me = o.me();
        Action a;  // 默认：站着不动、不出手
        if (!me.alive) return a;

        // 最近的、看得见的、活着的对手
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

        // 目的地：镖在地上就去捡，否则走向对手，都没有就去地图中心
        i64 gx = gi.M().width() / 2, gy = gi.M().height() / 2;  // 地图中心
        const Dart& md = o.my_dart();
        if (md.st == D_GROUND) {
            gx = md.x;
            gy = md.y;
        } else if (tgt >= 0) {
            gx = o.p[tgt].x;
            gy = o.p[tgt].y;
        }
        i64 wx, wy;
        if (path_next(gi, me.x, me.y, gx, gy, o.t, wx, wy)) {
            a.mx = wx - me.x;
            a.my = wy - me.y;
        }

        // 手里有镖、对手在 5 米内、中间没墙：朝他现在的位置扔
        if (tgt >= 0 && md.st == D_HAND && me.st == S_FREE && best < 5000 &&
            !wall_between(gi.M(), me.x, me.y, o.p[tgt].x, o.p[tgt].y)) {
            a.act = A_THROW;
            a.ax = o.p[tgt].x - me.x;
            a.ay = o.p[tgt].y - me.y;
        }
        return a;
    }
};

DART_MAIN(Bot)
