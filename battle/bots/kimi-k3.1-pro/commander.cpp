#include "commander.hpp"
#include <map>
#include <string>

namespace {
float center_x = 0, center_z = 0;
int boss_mark = -1;
int boss_target = -1;
std::map<int, int> retreat_until;

Unit* nearest_kind(Game& g, Unit* u, Kind kind, float maxd) {
    Unit* best = nullptr;
    float bd = maxd * maxd;
    for (Unit* e : g.enemies()) {
        if (e->kind != kind) continue;
        float d = Game::dist(*u, *e);
        if (d < bd) { bd = d; best = e; }
    }
    return best;
}

Unit* boss_unit(Game& g) {
    for (Unit* e : g.enemies()) if (e->kind == 镇山兽) return e;
    return nullptr;
}

void dodge_boss(Game& g) {
    if (g.bosses.empty()) return;
    BossInfo& b = g.bosses[0];
    Unit* boss = g.unit(b.id);
    if (!boss) return;
    if (b.state == 游走 || b.state == 晕) { boss_mark = -1; boss_target = -1; return; }
    if (b.state == 刨地 && boss_mark != g.tick) {
        if (boss_target < 0 || !g.unit(boss_target) || !g.unit(boss_target)->alive) {
            float far = -1;
            for (Unit* u : g.mine()) {
                float d = Game::dist(*u, *boss);
                if (d > far) { far = d; boss_target = u->id; }
            }
        }
    }
    float dx = b.dir_x, dz = b.dir_z;
    if (b.state == 刨地) {
        Unit* t = g.unit(boss_target);
        if (t) {
            float len = std::max(0.1f, Game::dist(*t, *boss));
            dx = (t->x - boss->x) / len;
            dz = (t->z - boss->z) / len;
        }
    }
    for (Unit* u : g.mine()) {
        float rx = u->x - boss->x, rz = u->z - boss->z;
        float along = rx * dx + rz * dz;
        float side = rx * dz - rz * dx;
        bool target = u->id == boss_target && b.state == 刨地;
        if ((along > -1 && along < 35 && std::fabs(side) < 3.0f) || target) {
            float sign = side >= 0 ? 1.0f : -1.0f;
            if (target && std::fabs(side) < 0.5f) sign = (u->id & 1) ? 1.0f : -1.0f;
            float tx = u->x + sign * 6.0f * dz;
            float tz = u->z - sign * 6.0f * dx;
            if (g.walkable(tx, tz)) g.move_straight(u->id, tx, tz);
        }
    }
}

void level1(Game& g) {
    auto enemies = g.enemies();
    bool only_archers = true;
    for (Unit* e : enemies) if (e->kind != 弓手) only_archers = false;
    for (Unit* u : g.mine()) {
        if (u->kind == 长枪兵 || u->kind == 盾卫) {
            if (only_archers || g.time > 70) {
                if (u->order != 自由) g.free(u->id);
            } else if (u->order != 原地守) g.hold(u->id);
        } else if (u->kind == 弓手) {
            Unit* e = g.nearest_enemy(u->x, u->z, 27);
            if (e) { if (u->order != 打 || u->target != e->id) g.attack(u->id, e->id); }
            else if (u->order != 原地守) g.hold(u->id);
        }
    }
}

void level2(Game& g) {
    for (Unit* u : g.mine()) {
        if (u->tag.rfind("炸桥", 0) == 0) {
            int delay = 0;
            if (u->tag.size() > 6) delay = std::atoi(u->tag.c_str() + 6);
            if (g.time < delay && !g.terrain_removed("桥中段")) g.hold(u->id);
            else if (u->order != 自由) g.free(u->id);
            continue;
        }
        if (u->kind == 投石车) {
            if (g.time < 25) g.aim(u->id, 44, 44);
            else g.aim(u->id, 44, 14);
            continue;
        }
        if (u->kind == 弓手 && u->tag.rfind("桥弓", 0) == 0 && !g.terrain_removed("桥中段")) {
            int delay = 0;
            if (u->tag.size() > 6) delay = std::atoi(u->tag.c_str() + 6);
            if (g.time < delay) { g.hold(u->id); continue; }
            Prop* best = nullptr;
            int crowd = -1;
            for (auto& p : g.props) {
                if (p.type != "keg" || !p.alive || p.triggered) continue;
                int n = 0;
                for (Unit* e : g.enemies()) if (Game::dist(e->x, e->z, p.x, p.z) < 6) ++n;
                if (n > crowd) { crowd = n; best = &p; }
            }
            if (best) g.attack(u->id, best->id);
            continue;
        }
        if (u->kind == 弓手) {
            Unit* e = g.nearest_enemy(u->x, u->z, 27);
            if (e) { if (u->order != 打 || u->target != e->id) g.attack(u->id, e->id); }
            else if (u->order != 原地守) g.hold(u->id);
        } else if (u->tag == "守旗") {
            if (u->order != 原地守) g.hold(u->id);
        } else if (g.time < 12 && u->kind != 主帅) {
            if (u->order != 原地守) g.hold(u->id);
        } else if (u->order != 自由 && u->kind != 主帅) {
            g.free(u->id);
        }
    }
}

void level3(Game& g) {
    for (Unit* u : g.mine()) {
        if (u->kind == 骑兵 || u->kind == 大块头) {
            Unit* archer = nearest_kind(g, u, 弓手, 1000);
            Unit* cat = nearest_kind(g, u, 投石车, 1000);
            if (archer) g.attack(u->id, archer->id);
            else if (u->order != 自由) g.free(u->id);
        } else if (u->kind == 弓手) {
            Unit* e = nearest_kind(g, u, 弓手, 1000);
            if (e) { if (u->order != 打 || u->target != e->id) g.attack(u->id, e->id); }
            else if (u->order != 自由) g.free(u->id);
        } else {
            Unit* archer = nearest_kind(g, u, 弓手, 1000);
            Unit* cat = nearest_kind(g, u, 投石车, 1000);
            if (archer) g.attack(u->id, archer->id);
            else if (u->order != 自由) g.free(u->id);
        }
    }
}

void level4(Game& g) {
    if (Unit* lord = g.lord()) {
        if (g.time < 1 && lord->z > 0.2f) g.move(lord->id, 0, 0);
        else if (lord->order != 原地守 && lord->order != 走到) g.hold(lord->id);
    }
    for (Unit* u : g.mine()) {
        if (u->kind == 主帅) continue;
        if (u->tag == "守主") {
            if (u->order != 原地守) g.hold(u->id);
            continue;
        }
        if (u->tag == "北近" || u->tag == "北远" || u->tag == "南近" || u->tag == "南远") {
            Prop* ball = nullptr;
            for (auto& p : g.props) if (p.type == "snowball" && p.alive && p.tag == u->tag) ball = &p;
            if (ball && !ball->triggered) { g.push(u->id, ball->id); continue; }
            if (u->order != 自由) g.free(u->id);
        }
        if (u->kind == 弓手) {
            Unit* e = g.nearest_enemy(u->x, u->z, 27);
            if (e) { if (u->order != 打 || u->target != e->id) g.attack(u->id, e->id); }
            else if (u->order != 自由) g.free(u->id);
        }
    }
}

void level5(Game& g) {
    Unit* boss = boss_unit(g);
    int guard_limit = 4;
    for (Unit* u : g.mine()) if (u->tag.rfind("策略", 0) == 0 && u->tag.size() > 6) guard_limit = std::atoi(u->tag.c_str() + 6);
    int guard_count = 0;
    for (Unit* e : g.enemies()) if (e->kind != 镇山兽) ++guard_count;
    bool guards_left = guard_count > guard_limit;
    bool cavalry_left = false;
    for (Unit* e : g.enemies()) if (e->kind == 骑兵) cavalry_left = true;
    for (Unit* u : g.mine()) {
        if (u->tag == "绕后") {
            if (cavalry_left) {
                Unit* cav = nearest_kind(g, u, 骑兵, 1000);
                if (cav && Game::dist(*u, *cav) < 11) g.attack(u->id, cav->id);
                else if (u->order != 原地守) g.hold(u->id);
            } else if (boss && (u->z < 30 || std::fabs(u->x) < 12)) {
                g.move(u->id, (u->id & 1) ? -15 : 15, 32);
            } else if (boss) {
                g.attack(u->id, boss->id);
            }
            continue;
        }
        Unit* close = nullptr;
        float bd = 1e9f;
        for (Unit* e : g.enemies()) {
            if (e->kind == 镇山兽) continue;
            float score = Game::dist(*u, *e) - (e->kind == 骑兵 ? 8 : 0);
            if (score < bd) { bd = score; close = e; }
        }
        if (guards_left) {
            if (u->kind == 弓手) {
                if (close && Game::dist(*u, *close) < 27) g.attack(u->id, close->id);
                else if (u->order != 原地守) g.hold(u->id);
            } else if (u->kind == 长枪兵 || u->kind == 盾卫) {
                if (close && Game::dist(*u, *close) < 11) g.attack(u->id, close->id);
                else if (boss && Game::dist(*u, *boss) < 4.5f) g.attack(u->id, boss->id);
                else if (u->order != 原地守) g.hold(u->id);
            } else if (close) {
                g.attack(u->id, close->id);
            }
        } else if (boss) {
            if (u->kind == 弓手 && Game::dist(*u, *boss) < 11) {
                float dx = u->x - boss->x, dz = u->z - boss->z;
                float len = std::max(.1f, std::hypot(dx, dz));
                float tx = u->x + dx / len * 7, tz = u->z + dz / len * 7;
                if (g.walkable(tx, tz)) g.move_straight(u->id, tx, tz);
                else g.attack(u->id, boss->id);
            } else {
                g.attack(u->id, boss->id);
            }
        } else if (close) {
            g.attack(u->id, close->id);
        }
    }
    if (boss) dodge_boss(g);
}

void battle(Game& g) {
    bool melee = g.mode == "melee";
    bool go_center = melee ? g.time > 8 : g.time > g.time_limit - 28;
    for (Unit* u : g.mine()) {
        if (u->kind == 投石车) continue;
        if (go_center) {
            float jx = float((u->id * 37) % 9) - 4.0f;
            float jz = float((u->id * 53) % 9) - 4.0f;
            float d = Game::dist(u->x, u->z, center_x + jx, center_z + jz);
            if (d > 2 && u->order != 走到) g.move(u->id, center_x + jx, center_z + jz);
            else if (d <= 2 && u->order != 自由) g.free(u->id);
            if (melee) continue;
        }
        if (u->kind == 长枪兵) {
            bool cav = nearest_kind(g, u, 骑兵, 15) != nullptr;
            if (cav && !go_center) { if (!u->holding) g.hold(u->id); continue; }
        }
        if (u->kind == 骑兵) {
            auto it = retreat_until.find(u->id);
            if (it != retreat_until.end() && g.tick < it->second) continue;
            Unit* near = g.nearest_enemy(u->x, u->z, 2.5f);
            if (near && std::hypot(u->vx, u->vz) < 2 && u->state == 站着) {
                float dx = u->x - near->x, dz = u->z - near->z;
                float len = std::max(0.1f, std::hypot(dx, dz));
                g.move(u->id, u->x + dx / len * 8, u->z + dz / len * 8);
                retreat_until[u->id] = g.tick + 16;
                continue;
            }
        }
        Unit* best = nullptr;
        float bs = 1e9f;
        for (Unit* e : g.enemies()) {
            float score = Game::dist(*u, *e);
            if (u->kind == 弓手 && e->kind == 炮仗兵) score -= 100;
            if (u->kind == 骑兵 && (e->kind == 弓手 || e->kind == 投石车)) score -= 15;
            if (score < bs) { bs = score; best = e; }
        }
        if (best && (u->order != 打 || u->target != best->id)) g.attack(u->id, best->id);
        if (u->kind == 炮仗兵 && !u->fuse) {
            int n = 0;
            for (Unit* e : g.enemies()) if (Game::dist(*u, *e) < 2.6f) ++n;
            if (n >= 2) g.detonate(u->id);
        }
    }
}
}

void on_start(Game& g) {
    g.to_local(0, 0, center_x, center_z);
    if (g.map == "第1关_草甸迎击") {
        for (Unit* u : g.mine()) if (u->kind == 长枪兵 || u->kind == 盾卫 || u->kind == 弓手) g.hold(u->id);
    }
}

void on_tick(Game& g) {
    if (g.mode == "duel" || g.mode == "melee") { battle(g); return; }
    if (g.map == "第1关_草甸迎击") level1(g);
    else if (g.map == "第2关_断龙桥") level2(g);
    else if (g.map == "第3关_拔炮台") level3(g);
    else if (g.map == "第4关_一线天") level4(g);
    else if (g.map == "第5关_镇山兽") level5(g);
}
