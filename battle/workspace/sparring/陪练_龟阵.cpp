// 陪练指挥：龟阵。先原地守，弓手点射射程内最近的敌人；
// 1v1 最后 30 秒全军压进计分区；混战第 15 秒起往中间走（潮水会把布阵区淹掉）。
#include "commander.hpp"

static float cx = 0, cz = 0;  // 地图正中（我方视角）

void on_start(Game& g) {
    g.to_local(0, 0, cx, cz);
    for (Unit* u : g.mine()) g.hold(u->id);
}

void on_tick(Game& g) {
    const bool go = (g.mode == "duel" && g.time > g.time_limit - 30) || (g.mode == "melee" && g.time > 15);
    for (Unit* u : g.mine()) {
        if (go) {
            if (u->kind == 投石车) continue;
            if (u->kind == 弓手) {
                Unit* e = g.nearest_enemy(u->x, u->z, 24);
                if (e && g.tick % 10 == 0) { g.attack(u->id, e->id); continue; }
            }
            if (u->order != 走到 && Game::dist(u->x, u->z, cx, cz) > 5) {
                const float jx = float((u->id * 37) % 9) - 4.0f, jz = float((u->id * 53) % 9) - 4.0f;
                g.move(u->id, cx + jx, cz + jz);
            } else if (u->order == 原地守 || Game::dist(u->x, u->z, cx, cz) <= 5) {
                if (u->order != 自由) g.free(u->id);
            }
            continue;
        }
        if (u->kind == 弓手) {
            Unit* e = g.nearest_enemy(u->x, u->z, 26);
            if (e) { if (u->target != e->id || u->order != 打) g.attack(u->id, e->id); }
            else if (u->order != 原地守) g.hold(u->id);
        } else if (u->order != 原地守 && u->order != 打) {
            g.hold(u->id);
        }
    }
}
