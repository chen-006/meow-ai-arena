#include "commander.hpp"

// 通用指挥官：闯关（level）模式完全不干预，沿用布阵姿态/默认行为（第1关靠此取胜）。
// 1v1 / 混战（blind PvP）模式：紧凑防御方阵——敌人不靠近就原地守，
// 弓手在射程内点最近目标，投石车手动瞄准最近敌人，近战接敌后自由反击。
// 全确定性：无随机数、无时钟、无静态可变状态。
void on_start(Game& g) { (void)g; }

void on_tick(Game& g) {
    if (g.mode == "level") return;  // 闯关不干预

    for (Unit* u : g.mine()) {
        if (!u->alive) continue;

        if (u->kind == 投石车) {
            Unit* e = g.nearest_enemy(u->x, u->z);
            if (e) g.aim(u->id, e->x, e->z);
            continue;
        }
        if (u->kind == 弓手) {
            Unit* best = nullptr;
            float bd = 1e9f;
            for (Unit* e : g.enemies()) {
                if (!e->alive) continue;
                float d = Game::dist(*u, *e);
                if (d <= 27.0f && d < bd) { bd = d; best = e; }
            }
            if (best) {
                if (u->target != best->id) g.attack(u->id, best->id);
            } else if (u->order != 原地守) {
                g.hold(u->id);
            }
            continue;
        }
        // 近战/骑兵：12 米内有敌就自由打，否则原地守住阵型
        Unit* near = g.nearest_enemy(u->x, u->z);
        if (near && near->alive && Game::dist(*u, *near) <= 12.0f) {
            if (u->order != 打 && u->order != 自由) g.free(u->id);
        } else if (u->order != 原地守) {
            g.hold(u->id);
        }
    }
}
