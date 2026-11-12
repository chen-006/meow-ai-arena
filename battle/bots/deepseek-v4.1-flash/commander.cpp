// 纸上谈兵 commander - unified tactics
#include "commander.hpp"
#include <map>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

static std::map<int, int> retreat_until;
static int last_cleanup = -100;
static std::map<int, int> role_ball;   // unit id -> snowball id (level4)
static std::map<int, int> role_side;   // 0 = none, 1 = north, 2 = south (level4)

static inline float vlen(float x, float z) { return std::sqrt(x * x + z * z); }

static Unit* enemy_kind(Game& g, Kind k) {
    for (Unit* e : g.enemies()) if (e->kind == k) return e;
    return nullptr;
}

static Unit* pick_archer_target(Game& g, Unit* a, float range) {
    Unit* best = nullptr; float bs = 1e18f;
    for (Unit* e : g.enemies()) {
        float d = Game::dist(*a, *e);
        if (d > range) continue;
        float s = d;
        if (e->kind == 炮仗兵) s -= 80.0f;
        else if (e->kind == 弓手 || e->kind == 投石车) s -= 25.0f;
        else if (e->kind == 骑兵) s -= 8.0f;
        if (s < bs) { bs = s; best = e; }
    }
    return best;
}

// generic archer behavior: shoot best target in range, retreat if enemy too close
static void archer_tick(Game& g, Unit* a) {
    Unit* near = g.nearest_enemy(a->x, a->z, 3.2f);
    if (near) {
        float dx = a->x - near->x, dz = a->z - near->z;
        float l = std::max(0.2f, vlen(dx, dz));
        float tx = a->x + dx / l * 3.0f, tz = a->z + dz / l * 3.0f;
        if (g.walkable(tx, tz)) {
            if (a->order != 走到 || Game::dist(a->x, a->z, tx, tz) > 1.0f) g.move(a->id, tx, tz);
            return;
        }
    }
    Unit* t = pick_archer_target(g, a, 27.0f);
    if (t) {
        if (a->order != 打 || a->target != t->id) g.attack(a->id, t->id);
        return;
    }
    if (a->order != 自由) g.free(a->id);
}

static void atk_nearest(Game& g, Unit* u) {
    Unit* e = g.nearest_enemy(u->x, u->z);
    if (!e) { if (u->order != 自由) g.free(u->id); return; }
    if (u->order != 打 || u->target != e->id) g.attack(u->id, e->id);
}

static void melee_tick(Game& g, Unit* u) {
    if (u->kind == 长枪兵) {
        bool cav = false;
        for (Unit* e : g.enemies()) {
            if (e->kind != 骑兵) continue;
            if (Game::dist(*u, *e) < 12.0f && vlen(e->vx, e->vz) > 3.0f) { cav = true; break; }
        }
        if (cav) { if (u->order != 原地守) g.hold(u->id); return; }
    }
    atk_nearest(g, u);
}

static void level2(Game& g) {
    int nen = (int)g.enemies().size();
    bool cleanup = (nen <= 8) || (g.time > g.time_limit - 12.0f);
    for (Unit* u : g.mine()) {
        if (u->kind == 弓手) { archer_tick(g, u); continue; }
        if (u->kind == 投石车) {
            if (u->order != 自由) g.free(u->id);
            continue;
        }
        if (!cleanup) {
            if (u->order != 原地守) g.hold(u->id);
        } else {
            melee_tick(g, u);
        }
    }
}

static std::map<int,int> l3stage;
static void level3(Game& g) {
    static const float WX[3] = {-31.0f, -30.0f, -17.0f};
    static const float WZ[3] = {44.0f, 61.0f, 62.0f};
    for (Unit* u : g.mine()) {
        if (u->kind == 弓手) { archer_tick(g, u); continue; }
        if (u->kind == 投石车) { if (u->order != 自由) g.free(u->id); continue; }
        if (u->kind == 炮仗兵) {
            int n = 0;
            for (Unit* e : g.enemies()) if (Game::dist(*u, *e) < 3.5f) ++n;
            if (n >= 2 && !u->fuse) { g.detonate(u->id); continue; }
        }
        int st = l3stage.count(u->id) ? l3stage[u->id] : 0;
        if (u->z < 38.0f && g.time > 25.0f) { l3stage[u->id] = 3; st = 3; }
        if (st < 3) {
            float jx = ((u->id * 7) % 9 - 4) * 0.7f;
            float jz = ((u->id * 11) % 7 - 3) * 0.7f;
            float tx = WX[st] + jx, tz = WZ[st] + jz;
            if (Game::dist(u->x, u->z, WX[st], WZ[st]) < 3.5f) {
                l3stage[u->id] = st + 1;
            } else {
                if (u->order != 走到 || Game::dist(u->x, u->z, tx, tz) > 1.2f) g.move(u->id, tx, tz);
                continue;
            }
        }
        // if we got knocked off the plateau, climb back up the west rubble before chasing
        if (u->z < 38.0f) {
            float tx = -31.0f + ((u->id * 7) % 5 - 2) * 0.5f;
            float tz = 41.5f;
            g.move_straight(u->id, tx, tz);
            continue;
        }
        melee_tick(g, u);
    }
}

static Unit* nearest_archer_same_side(Game& g, Unit* u) {
    Unit* best = nullptr; float bd = 1e18f;
    for (Unit* e : g.enemies()) {
        if (e->kind != 弓手) continue;
        float d = Game::dist(*u, *e);
        if ((e->x > 0) != (u->x > 0)) d += 1000.0f;
        if (d < bd) { bd = d; best = e; }
    }
    return best;
}

static void level4(Game& g) {
    if (Unit* L = g.lord()) {
        if (g.time < 0.15f) g.move(L->id, 3.0f, 1.0f);
        else if (L->order != 原地守 && L->order != 走到) g.hold(L->id);
    }
    for (Unit* u : g.mine()) {
        int side = role_side.count(u->id) ? role_side[u->id] : 0;
        if (side == 3 || side == 4) {   // snowball pusher for one cliff
            Prop* best = nullptr; float bd = 1e18f;
            for (auto& p : g.props) {
                if (p.type != "snowball" || !p.alive || p.triggered) continue;
                if ((p.x > 0) != (side == 3)) continue;
                float d = Game::dist(u->x, u->z, p.x, p.z);
                if (d < bd) { bd = d; best = &p; }
            }
            if (best) {
                if (u->order != 推 || u->target != best->id) g.push(u->id, best->id);
                continue;
            }
            side = (side == 3) ? 1 : 2;   // balls done: hunt archers
        }
        if (u->kind == 主帅) {
            if (u->order != 原地守 && u->order != 走到) g.hold(u->id);
            continue;
        }
        if (u->kind == 弓手) { archer_tick(g, u); continue; }
        if (u->kind == 投石车) { if (u->order != 自由) g.free(u->id); continue; }
        if (side == 1 || side == 2) {
            Unit* e = nearest_archer_same_side(g, u);
            if (!e) e = g.nearest_enemy(u->x, u->z);
            if (e) { if (u->order != 打 || u->target != e->id) g.attack(u->id, e->id); }
            else {
                float tx = (side == 1 ? 7.0f : -7.0f), tz = 54.0f;
                if (u->order != 走到 || Game::dist(u->x, u->z, tx, tz) > 3.0f) g.move(u->id, tx, tz);
            }
            continue;
        }
        // valley force: hold near the lord early, then mop up
        Unit* e = g.nearest_enemy(u->x, u->z, g.time < 22.0f ? 16.0f : 1e9f);
        if (e) {
            if (u->order != 打 || u->target != e->id) g.attack(u->id, e->id);
            continue;
        }
        if (g.time < 22.0f) {
            if (u->order != 原地守) g.hold(u->id);
            continue;
        }
        Prop* bar = nullptr;
        for (auto& p : g.props) if (p.type == "barrier" && p.alive) { bar = &p; if (p.tag == "路障甲") break; }
        if (bar) { if (u->order != 打 || u->target != bar->id) g.attack(u->id, bar->id); }
    }
}

static void level5(Game& g) {
    Unit* boss = enemy_kind(g, 镇山兽);
    if (!boss) return;
    const BossInfo* bi = nullptr;
    for (auto& b : g.bosses) if (b.id == boss->id) bi = &b;
    bool stunned = bi && bi->state == 晕;
    int idx = 0;
    for (Unit* u : g.mine()) {
        if (u->kind == 主帅) { if (u->order != 原地守) g.hold(u->id); continue; }
        if (u->tag == "bait") { if (u->order != 原地守) g.hold(u->id); continue; }
        float db = Game::dist(*u, *boss);
        if (u->kind == 投石车) {
            if (u->order != 打 || u->target != boss->id) g.attack(u->id, boss->id);
            continue;
        }
        if (u->kind == 弓手) {
            if (db < 27.0f) { if (u->order != 打 || u->target != boss->id) g.attack(u->id, boss->id); }
            continue;
        }
        if (stunned || db < 4.5f) {
            if (u->order != 打 || u->target != boss->id) g.attack(u->id, boss->id);
        } else if (g.time < 2.0f) {
            int k = idx++;
            int side = (k % 2 == 0) ? -1 : 1;
            int rank = k / 2;
            float tx = side * (2.0f + 0.8f * (rank % 4));
            float tz = 2.0f + 1.0f * ((rank / 4) % 4);
            if (u->order != 走到 || Game::dist(u->x, u->z, tx, tz) > 0.8f) g.move(u->id, tx, tz);
        } else {
            if (u->order != 原地守) g.hold(u->id);
        }
    }
}

static void generic_level(Game& g) {
    for (Unit* u : g.mine()) {
        if (u->kind == 弓手) archer_tick(g, u);
        else if (u->kind == 投石车) { if (u->order != 自由) g.free(u->id); }
        else melee_tick(g, u);
    }
}

// ---------------- duel / melee ----------------
static void duel_tick(Game& g) {
    float cx = 0, cz = 0;
    g.to_local(0, 0, cx, cz);
    bool endgame = g.time > g.time_limit - 30.0f;
    for (Unit* u : g.mine()) {
        if (u->kind == 投石车) {
            Unit* e = g.nearest_enemy(u->x, u->z);
            if (e) { if (u->order != 打 || u->target != e->id) g.attack(u->id, e->id); }
            continue;
        }
        if (u->kind == 弓手) { archer_tick(g, u); continue; }
        if (u->kind == 骑兵) {
            float spd = vlen(u->vx, u->vz);
            Unit* ne = g.nearest_enemy(u->x, u->z, 3.0f);
            auto it = retreat_until.find(u->id);
            if (it != retreat_until.end() && g.tick < it->second) { atk_nearest(g, u); continue; }
            if (ne && spd < 2.0f && u->state == 站着) {
                float dx = u->x - ne->x, dz = u->z - ne->z, l = std::max(0.2f, vlen(dx, dz));
                g.move(u->id, u->x + dx / l * 8.0f, u->z + dz / l * 8.0f);
                retreat_until[u->id] = g.tick + 12;
                continue;
            }
            Unit* best = nullptr; float bs = 1e18f;
            for (Unit* e : g.enemies()) {
                float d = Game::dist(*u, *e);
                if (e->kind == 弓手 || e->kind == 投石车) d -= 15.0f;
                if (d < bs) { bs = d; best = e; }
            }
            if (best) { if (u->order != 打 || u->target != best->id) g.attack(u->id, best->id); }
            continue;
        }
        if (u->kind == 炮仗兵) {
            int n = 0;
            for (Unit* e : g.enemies()) if (Game::dist(*u, *e) < 2.6f) ++n;
            if ((n >= 2 || (n >= 1 && u->hp < 20)) && !u->fuse) { g.detonate(u->id); continue; }
            atk_nearest(g, u);
            continue;
        }
        if (endgame && !g.in_score_zone(u->x, u->z)) {
            float jx = float((u->id * 37) % 7) - 3.0f, jz = float((u->id * 53) % 7) - 3.0f;
            if (u->order != 走到 || Game::dist(u->x, u->z, cx + jx, cz + jz) > 2.0f)
                g.move(u->id, cx + jx, cz + jz);
            continue;
        }
        melee_tick(g, u);
    }
}

static void melee_tick_team(Game& g) {
    float cx = 0, cz = 0;
    g.to_local(0, 0, cx, cz);
    float tide = g.water_at(g.time + 12.0f);
    for (Unit* u : g.mine()) {
        if (u->kind == 投石车) {
            Unit* e = g.nearest_enemy(u->x, u->z);
            if (e) { if (u->order != 打 || u->target != e->id) g.attack(u->id, e->id); }
            continue;
        }
        if (u->kind == 弓手) { archer_tick(g, u); continue; }
        // gather on the central ring early; the tide forces it anyway
        bool danger = g.height(u->x, u->z) < g.water_at(g.time + 10.0f);
        if (danger || g.time > 4.0f) {
            float dx = cx - u->x, dz = cz - u->z, l = std::max(0.1f, vlen(dx, dz));
            if (l > 8.5f) {
                float tx = cx - dx / l * 7.5f, tz = cz - dz / l * 7.5f;
                if (u->order != 走到 || Game::dist(u->x, u->z, tx, tz) > 2.0f) g.move(u->id, tx, tz);
                continue;
            }
        }
        if (u->kind == 骑兵) {
            Unit* best = nullptr; float bs = 1e18f;
            for (Unit* e : g.enemies()) {
                float d = Game::dist(*u, *e);
                if (e->kind == 弓手 || e->kind == 投石车) d -= 12.0f;
                if (d < bs) { bs = d; best = e; }
            }
            if (best) { if (u->order != 打 || u->target != best->id) g.attack(u->id, best->id); }
            continue;
        }
        if (u->kind == 炮仗兵) {
            int n = 0;
            for (Unit* e : g.enemies()) if (Game::dist(*u, *e) < 2.6f) ++n;
            if ((n >= 2 || (n >= 1 && u->hp < 20)) && !u->fuse) { g.detonate(u->id); continue; }
            atk_nearest(g, u);
            continue;
        }
        melee_tick(g, u);
    }
}

void on_start(Game& g) {
    role_ball.clear(); role_side.clear();
    if (g.mode == "level" && g.map.find("第4关") != std::string::npos) {
        for (Unit* u : g.mine()) {
            if (u->tag == "pushN" || u->tag == "pushS") {
                bool north = (u->tag == "pushN");
                if (u->x > 0 == north) role_side[u->id] = north ? 3 : 4;
            }
        }
        // unresolved: hammers on the cliffs become pushers; infantry on the cliffs become archer hunters
        for (Unit* u : g.mine()) {
            if (role_side.count(u->id)) continue;
            if (u->kind == 大锤 && std::fabs(u->x) > 2.0f) role_side[u->id] = (u->x > 0) ? 3 : 4;
        }
        for (Unit* u : g.mine()) {
            if (role_side.count(u->id)) continue;
            if ((u->kind == 新兵 || u->kind == 盾卫 || u->kind == 长枪兵) && std::fabs(u->x) > 2.0f)
                role_side[u->id] = (u->x > 0) ? 1 : 2;
        }
        // any leftover snowball with nobody on its cliff: nothing we can do
    }
}

void on_tick(Game& g) {
    if (g.mode == "level") {
        if (g.map.find("第1关") != std::string::npos) level2(g);
        else if (g.map.find("第2关") != std::string::npos) level2(g);
        else if (g.map.find("第3关") != std::string::npos) level3(g);
        else if (g.map.find("第4关") != std::string::npos) level4(g);
        else if (g.map.find("第5关") != std::string::npos) level5(g);
        else generic_level(g);
    } else if (g.mode == "duel") {
        duel_tick(g);
    } else if (g.mode == "melee") {
        melee_tick_team(g);
    } else {
        generic_level(g);
    }
}
