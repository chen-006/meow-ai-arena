// 纸上谈兵 指挥程序
// duel: 龟阵（先原地守，弓手点射；最后 30 秒全军压进计分区）
// melee: 压制型（示例指挥的通用打法：枪兵架枪、弓手点名、骑兵冲撤、炮仗兵近身引爆）
// level: 第 4 关推雪球、护主帅、分两阶段；其余关卡用布阵姿态（不干预）
#include "commander.hpp"
#include <map>

namespace {
std::map<int, int> retreat_until;   // 骑兵编号 → 撤到第几回合
float cx = 0, cz = 0;               // 地图正中（我方视角）
bool lord_moved = false;
int l4_phase = 1;
}

void on_start(Game& g) {
    g.to_local(0, 0, cx, cz);
}

// ---------------- 1v1：龟阵 ----------------
static void duel_tick(Game& g) {
    const bool go = (g.time > g.time_limit - 30);
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

// ---------------- 混战 / 通用：压制 ----------------
static void fight_tick(Game& g) {
    auto enemies = g.enemies();
    for (Unit* s : g.mine_of(长枪兵)) {
        bool cav = false;
        for (Unit* e : enemies)
            if (e->kind == 骑兵 && Game::dist(*s, *e) < 18 && std::hypot(e->vx, e->vz) > 3) cav = true;
        if (cav && !s->holding) g.hold(s->id);
        else if (!cav && s->holding && g.tick % 20 == 0) g.free(s->id);
    }
    for (Unit* a : g.mine_of(弓手)) {
        Unit* close = g.nearest_enemy(a->x, a->z, 3.0f);
        if (close) {
            const float dx = a->x - close->x, dz = a->z - close->z, l = std::max(0.1f, std::hypot(dx, dz));
            if (g.walkable(a->x + dx / l * 2, a->z + dz / l * 2)) { g.move(a->id, a->x + dx / l * 2, a->z + dz / l * 2); continue; }
        }
        Unit* best = nullptr; float bd = 1e9f;
        for (Unit* e : enemies) {
            const float d = Game::dist(*a, *e);
            if (d > 26) continue;
            const float score = d - (e->kind == 炮仗兵 ? 100.0f : 0.0f);
            if (score < bd) { bd = score; best = e; }
        }
        if (best) { if (a->target != best->id || a->order != 打) g.attack(a->id, best->id); }
        else if (a->order != 自由) g.free(a->id);
    }
    for (Unit* c : g.mine_of(骑兵)) {
        const float spd = std::hypot(c->vx, c->vz);
        auto it = retreat_until.find(c->id);
        if (it != retreat_until.end() && g.tick < it->second) continue;
        Unit* near = g.nearest_enemy(c->x, c->z, 2.5f);
        if (near && spd < 2.0f && c->state == 站着) {
            const float dx = c->x - near->x, dz = c->z - near->z, l = std::max(0.1f, std::hypot(dx, dz));
            float tx = c->x + dx / l * 8, tz = c->z + dz / l * 8;
            if (!g.walkable(tx, tz)) { tx = c->x; tz = c->z - 8; }
            g.move(c->id, tx, tz);
            retreat_until[c->id] = g.tick + 15;
            continue;
        }
        Unit* best = nullptr; float bd = 1e9f;
        for (Unit* e : enemies) {
            const float d = Game::dist(*c, *e) - ((e->kind == 弓手 || e->kind == 投石车) ? 15.0f : 0.0f);
            if (d < bd) { bd = d; best = e; }
        }
        if (best && (c->order != 打 || c->target != best->id)) g.attack(c->id, best->id);
    }
    for (Unit* b : g.mine_of(炮仗兵)) {
        int n = 0;
        for (Unit* e : enemies) if (Game::dist(*b, *e) < 2.5f) ++n;
        if (n > 2 && !b->fuse) g.detonate(b->id);
    }
}

// ---------------- 第 4 关 ----------------
static void level4_tick(Game& g) {
    for (Unit* u : g.by_tag("推球")) {
        Prop* best = nullptr; float bd = 1e9f;
        for (auto& p : g.props)
            if (p.type == "snowball" && p.alive && !p.triggered) {
                float d = Game::dist(u->x, u->z, p.x, p.z);
                if (d < bd) { bd = d; best = &p; }
            }
        if (best) { if (u->order != 推) g.push(u->id, best->id); }
        else {
            float tx = (u->x >= 0 ? 12.0f : -12.0f);
            if (Game::dist(u->x, u->z, tx, 6.0f) > 1.0f && u->order != 走到) g.move(u->id, tx, 6.0f);
        }
    }
    if (Unit* l = g.lord()) if (!lord_moved) { g.move(l->id, 3.5f, 0.5f); lord_moved = true; }
    int near = 0;
    for (Unit* e : g.enemies()) if (e->z < 50.0f) ++near;
    if (l4_phase == 1 && near == 0) l4_phase = 2;
    for (Unit* u : g.mine()) {
        if (u->tag == "推球" || u->kind == 主帅) continue;
        if (l4_phase == 1) { if (u->order != 原地守) g.hold(u->id); }
        else { if (u->order == 原地守) g.free(u->id); }
    }
    for (Unit* u : g.by_tag("护卫")) if (u->order != 原地守) g.hold(u->id);
}

void on_tick(Game& g) {
    if (g.mode == "duel") { duel_tick(g); return; }
    if (g.mode == "melee") { fight_tick(g); return; }
    if (g.mode == "level" && g.map == "第4关_一线天") { level4_tick(g); return; }
    // 其他关卡：不干预，按布阵姿态行动
}