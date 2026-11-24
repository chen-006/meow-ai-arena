#pragma once
#include "dart.hpp"

// 只改目的地，瞄准、避险和出手仍由各档原有算法决定。
inline void team_goal(const dart::Obs& o, dart::i64& x, dart::i64& y) {
    using namespace dart;
    const auto& gi = info();
    const auto& me = o.me();
    int team = gi.team[o.seat];
    if (gi.cfg.mode == M_CONTROL) {
        i64 nearest = -1;
        for (size_t i = 0; i < gi.points.size(); ++i) {
            if (o.owner[i] == team) continue;
            const auto& p = gi.points[i];
            i64 dx = p.x - me.x, dy = p.y - me.y, distance = dx*dx + dy*dy;
            if (nearest < 0 || distance < nearest) {
                nearest = distance;
                x = p.x;
                y = p.y;
            }
        }
    } else if (gi.cfg.mode == M_KING) {
        int own = gi.king[team], target = gi.king[1-team];
        int guards = 0;
        for (int i = team*5; i < team*5+5 && guards < 2; ++i) {
            if (i == own) continue;
            if (i == o.seat) target = own;
            ++guards;
        }
        if (o.p[target].alive && o.p[target].vis) {
            x = o.p[target].x;
            y = o.p[target].y;
        } else {
            // 敌王隐蔽时，在敌堡默认出生区搜索。
            x = team ? gi.M().width()/6 : gi.M().width()*5/6;
            y = gi.M().height()/2;
        }
    }
}
