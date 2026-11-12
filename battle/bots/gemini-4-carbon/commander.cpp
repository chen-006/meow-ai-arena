#include "commander.hpp"

namespace level1 {
void on_start(Game& g) {
    for (Unit* u : g.mine()) g.hold(u->id);
}
void on_tick(Game& g) {
    int e_melee = 0;
    for (Unit* e : g.enemies()) if (e->kind != 弓手) ++e_melee;
    if (e_melee == 0) {
        for (Unit* u : g.mine()) {
            Unit* e = g.nearest_enemy(u->x, u->z);
            if (!e) continue;
            if (u->z < 25.0f && std::fabs(u->x) < 15.5f) g.move(u->id, 17.0f, 26.0f);
            else g.attack(u->id, e->id);
        }
        return;
    }
    if (g.tick == 0) {
        for (Unit* u : g.mine()) g.move_straight(u->id, u->x - 0.3f, u->z + 0.5f);
        return;
    }
    if (g.tick == 2) {
        for (Unit* u : g.mine()) g.hold(u->id);
        return;
    }
    for (Unit* u : g.mine()) {
        if (!u->holding) g.hold(u->id);
    }
}
} // namespace level1

namespace level2 {
static float fire_time = 8.5f;
static int east_stage = 0;

void on_start(Game& g) {
    east_stage = 0;
    for (Unit* a : g.by_tag("sniper")) {
        fire_time = 5.0f + (a->x - 7.0f) * 1.0f;
        g.move_straight(a->id, 9.0f, 21.0f);
    }
    for (Unit* u : g.mine()) {
        if (u->tag != "sniper") g.hold(u->id);
    }
}

void on_tick(Game& g) {
    bool bridge_gone = g.terrain_removed("桥中段");
    if (!bridge_gone) {
        for (Unit* a : g.by_tag("sniper")) {
            if (g.time >= fire_time) {
                Prop* best_k = nullptr;
                float bd = 1e9f;
                for (auto& p : g.props) {
                    if (p.type == "keg" && p.alive && !p.triggered) {
                        float d = Game::dist(a->x, a->z, p.x, p.z) - (p.x > 0 ? 20.0f : 0.0f);
                        if (d < bd) { bd = d; best_k = &p; }
                    }
                }
                if (best_k && (a->order != 打 || a->target != best_k->id)) g.attack(a->id, best_k->id);
            } else {
                if (Game::dist(a->x, a->z, 9.0f, 21.0f) > 0.5f) g.move_straight(a->id, 9.0f, 21.0f);
                else g.hold(a->id);
            }
        }
        for (Unit* u : g.mine()) {
            if (u->tag == "sniper") continue;
            if (!u->holding) g.hold(u->id);
        }
        return;
    }

    auto enemies = g.enemies();
    Unit* choke_e = nullptr;
    float best_d = 1e9f;
    for (Unit* e : enemies) {
        if (e->x < 12.0f && e->z > 10.0f && e->z < 30.0f) {
            float d = Game::dist(0.0f, 19.0f, e->x, e->z);
            if (d < best_d) { best_d = d; choke_e = e; }
        }
    }

    if (choke_e) {
        for (Unit* u : g.mine()) {
            if (u->tag == "sniper") {
                if (choke_e->z > 9.0f) {
                    if (Game::dist(u->x, u->z, 6.0f, 12.0f) > 2.5f && choke_e->z > 15.0f) g.move(u->id, 6.0f, 12.0f);
                    else g.attack(u->id, choke_e->id);
                } else {
                    g.move(u->id, 6.0f, 12.0f);
                }
            } else {
                if (choke_e->z < 18.5f) g.attack(u->id, choke_e->id);
                else if (!u->holding) g.hold(u->id);
            }
        }
        return;
    }

    bool enemy_near = false;
    for (Unit* e : enemies) if (Game::dist(6.5f, 5.5f, e->x, e->z) < 5.0f) enemy_near = true;

    int s_idx = 0, p_idx = 0;
    bool all_stage0 = true, all_stage1 = true;
    for (Unit* u : g.mine()) {
        if (u->kind == 弓手) {
            if (Game::dist(u->x, u->z, 6.5f, 13.0f) > 0.8f) g.move(u->id, 6.5f, 13.0f);
            else {
                Unit* ne = g.nearest_enemy(8.0f, 5.5f, 24.0f);
                if (ne && ne->x > 5.0f) g.attack(u->id, ne->id);
                else g.hold(u->id);
            }
            continue;
        }
        float tx, tz;
        if (u->kind == 盾卫) { tx = 6.5f; tz = 4.5f + s_idx * 0.5f; ++s_idx; }
        else { tx = 5.5f; tz = 4.5f + p_idx * 0.5f; ++p_idx; }

        if (enemy_near) {
            Unit* leak = nullptr;
            for (Unit* e : enemies) if (e->x < 5.8f && Game::dist(1.0f, 5.5f, e->x, e->z) < 6.0f) leak = e;
            if (leak) g.attack(u->id, leak->id);
            else if (!u->holding) g.hold(u->id);
        } else if (east_stage == 0) {
            if (Game::dist(u->x, u->z, tx - 1.0f, tz) > 0.45f) { all_stage0 = false; g.move(u->id, tx - 1.0f, tz); }
            else g.hold(u->id);
        } else if (east_stage == 1) {
            if (Game::dist(u->x, u->z, tx, tz) > 0.42f) { all_stage1 = false; g.move_straight(u->id, tx, tz); }
            else g.hold(u->id);
        } else {
            if (!u->holding) g.hold(u->id);
        }
    }
    if (east_stage == 0 && all_stage0 && (s_idx + p_idx > 0)) east_stage = 1;
    else if (east_stage == 1 && all_stage1 && (s_idx + p_idx > 0)) east_stage = 2;
}
} // namespace level2

namespace level3 {
static float pull_time = -1.0f;

void on_start(Game& g) {
    pull_time = -1.0f;
    for (Unit* u : g.mine()) {
        if (u->kind == 炮仗兵 || u->kind == 弓手) g.hold(u->id);
        else if (u->tag == "east") g.move(u->id, 29.0f, 42.0f);
        else g.move(u->id, -31.0f, 42.0f);
    }
}

void on_tick(Game& g) {
    for (const auto& ev : g.events) {
        if (ev.find("sortie 坡顶") != std::string::npos && pull_time < 0.0f) pull_time = g.time;
    }

    auto enemies = g.enemies();
    int plat_e = 0, east_a = 0, ramp_cnt = 0, non_ramp = 0;
    for (Unit* e : enemies) {
        if (e->y > 2.5f) {
            ++plat_e;
            if (e->kind == 弓手 && e->x > 8.0f) ++east_a;
            if (std::fabs(e->x) < 9.5f && e->z < 47.0f && e->kind != 弓手 && pull_time < 0.0f) ++ramp_cnt;
            else if (pull_time < 0.0f) ++non_ramp;
        }
    }
    if (pull_time >= 0.0f) non_ramp = 0;

    for (Unit* u : g.mine()) {
        bool on_plat = (u->y > 3.5f && u->z >= 39.2f);

        if (plat_e == 0) {
            Unit* ge = g.nearest_enemy(u->x, u->z);
            if (ge) {
                if (on_plat && u->z > 32.0f) g.move(u->id, 0.0f, 30.0f);
                else g.attack(u->id, ge->id);
            }
            continue;
        }

        if (u->kind == 炮仗兵) {
            if (u->fuse) continue;
            if (g.time < 22.0f) { g.hold(u->id); continue; }
            if (!on_plat) { g.move(u->id, -31.0f, 42.0f); continue; }
            int bi = u->tag.size() > 1 ? (u->tag[1] - '1') : 0;
            float my_go_time = (pull_time >= 0.0f) ? (pull_time + 10.5f + bi * 2.2f) : 9999.0f;
            if (g.time < my_go_time) {
                float wx = -34.0f, wz = 45.0f + bi * 5.0f;
                if (Game::dist(u->x, u->z, wx, wz) > 1.0f) g.move_straight(u->id, wx, wz);
                else g.hold(u->id);
                continue;
            }
            Unit* best = nullptr; int bc = -1;
            for (Unit* e : enemies) if (e->y > 2.5f) {
                int c = 0;
                for (Unit* e2 : enemies) if (Game::dist(*e, *e2) < 3.0f) ++c;
                if (c > bc) { bc = c; best = e; }
            }
            if (best) {
                int near_e = 0;
                for (Unit* e : enemies) if (Game::dist(*u, *e) < 2.6f) ++near_e;
                if (near_e >= 7 || Game::dist(*u, *best) < 0.9f) g.detonate(u->id);
                else g.move_straight(u->id, best->x, best->z);
            }
            continue;
        }

        if (!on_plat) {
            if (u->tag == "east") {
                Unit* pe = nullptr; float pd = 1e9f;
                for (Unit* e : enemies) if (e->x > 26.0f && e->z < 45.0f && e->y > 2.5f) {
                    float d = Game::dist(*u, *e); if (d < pd) { pd = d; pe = e; }
                }
                if (pe && u->z > 37.5f && std::fabs(u->x - 29.0f) < 2.2f) g.attack(u->id, pe->id);
                else g.move(u->id, 29.0f, 42.0f);
            } else {
                if (u->kind == 弓手 && g.time < 20.0f) g.hold(u->id);
                else g.move(u->id, -31.0f, 42.0f);
            }
            continue;
        }

        if (u->kind == 弓手) {
            if (non_ramp > 0) {
                Unit* best = nullptr; float bd = 1e9f;
                for (Unit* e : enemies) {
                    if (e->y <= 2.5f) continue;
                    if (std::fabs(e->x) < 9.5f && e->z < 47.0f && e->kind != 弓手) continue;
                    float d = Game::dist(*u, *e);
                    if (d <= 26.0f && d < bd) { bd = d; best = e; }
                }
                if (best) {
                    if (u->order != 打 || u->target != best->id) g.attack(u->id, best->id);
                } else {
                    float px = (int(g.time / 3.0f) % 2 == 0) ? -25.0f : -18.0f;
                    g.move_straight(u->id, px, 60.0f);
                }
                continue;
            }
            bool safe_to_pull = true;
            for (Unit* m : g.mine()) {
                if (m->kind == 炮仗兵 && m->y < 3.5f) safe_to_pull = false;
                if ((m->kind == 盾卫 || m->kind == 长枪兵) && m->x < 28.0f) safe_to_pull = false;
            }
            if (pull_time < 0.0f && !safe_to_pull && g.time < 76.0f) {
                if (Game::dist(u->x, u->z, -12.0f, 58.0f) > 1.5f) g.move_straight(u->id, -12.0f, 58.0f);
                else g.hold(u->id);
                continue;
            }
            if (pull_time < 0.0f) {
                Unit* ne = g.nearest_enemy(u->x, u->z);
                if (ne && (u->order != 打 || u->target != ne->id)) g.attack(u->id, ne->id);
            } else {
                Unit* ne = g.nearest_enemy(u->x, u->z);
                if (ne && Game::dist(*u, *ne) < 11.0f) {
                    if (u->x < 28.0f && u->z > 50.0f) g.move_straight(u->id, 34.0f, 62.0f);
                    else if (u->z > 46.0f && u->x >= 28.0f) g.move_straight(u->id, 34.0f, 43.0f);
                    else g.move_straight(u->id, -30.0f, 43.0f);
                } else if (ne) {
                    if (u->order != 打 || u->target != ne->id) g.attack(u->id, ne->id);
                }
            }
            continue;
        }

        if (non_ramp == 0) {
            int my_bombers = int(g.mine_of(炮仗兵).size());
            if (pull_time < 0.0f || my_bombers > 0) {
                Unit* ne = g.nearest_enemy(u->x, u->z);
                if (u->x < 28.0f && pull_time < 0.0f) {
                    if (u->z < 49.0f && std::fabs(u->x) < 12.0f) g.move_straight(u->id, 14.0f, 52.0f);
                    else g.move_straight(u->id, 33.0f, 58.0f);
                } else if (ne && Game::dist(*u, *ne) < 14.0f) {
                    if (u->z > 44.0f && u->x > 15.0f) g.move_straight(u->id, 34.0f, 42.5f);
                    else g.move_straight(u->id, -25.0f, 42.5f);
                } else {
                    g.hold(u->id);
                }
                continue;
            }
            Unit* focus = nullptr; float bsc = 1e9f;
            for (Unit* e : enemies) if (e->y > 2.5f) {
                float sc = e->hp + e->x * 0.2f;
                if (sc < bsc) { bsc = sc; focus = e; }
            }
            if (focus && (u->order != 打 || u->target != focus->id)) g.attack(u->id, focus->id);
            continue;
        }

        Unit* target = nullptr; float bsc = 1e9f;
        for (Unit* e : enemies) {
            if (e->y <= 2.5f) continue;
            if (std::fabs(e->x) < 9.5f && e->z < 47.0f && e->kind != 弓手) continue;
            float sc = 0.0f;
            if (u->tag == "east") {
                if (e->x > 25.0f && e->z < 45.0f) sc = -1200.0f + Game::dist(*u, *e);
                else if (e->kind == 弓手 && e->x > 0.0f) sc = -1000.0f - e->x;
                else if (e->kind == 弓手) sc = -800.0f + Game::dist(*u, *e);
                else sc = -500.0f - e->x * 2.0f + (e->kind == 长枪兵 ? -15.0f : 0.0f);
            } else {
                if (e->kind == 弓手 && e->x < 0.0f) sc = -1000.0f + e->x;
                else if (e->z > 50.0f && (east_a == 0 || g.by_tag("east").size() > 0)) {
                    sc = -700.0f + e->x * 2.0f + (e->kind == 长枪兵 ? -15.0f : 0.0f);
                }
                else if (e->kind == 弓手) sc = -600.0f + e->x;
                else sc = -500.0f + Game::dist(*u, *e);
            }
            if (sc < bsc) { bsc = sc; target = e; }
        }
        if (target) {
            if (ramp_cnt >= 2 && (u->x * target->x < 0.0f) && u->z < 47.5f) {
                g.move_straight(u->id, target->x > u->x ? 8.5f : -8.5f, 50.0f);
            } else if (target->z < 41.0f && Game::dist(*u, *target) > 2.5f) {
                g.move_straight(u->id, target->x, 41.2f);
            } else {
                if (u->order != 打 || u->target != target->id) g.attack(u->id, target->id);
            }
        }
    }
}
} // namespace level3

namespace level4 {
void on_start(Game& g) {
    if (Unit* l = g.lord()) g.move_straight(l->id, 0.0f, 0.2f);
    for (Unit* u : g.mine()) {
        if (std::fabs(u->x) < 5.0f) g.hold(u->id);
    }
}

void on_tick(Game& g) {
    if (Unit* l = g.lord()) {
        if (Game::dist(l->x, l->z, 0.0f, 0.2f) > 0.4f) g.move_straight(l->id, 0.0f, 0.2f);
        else g.hold(l->id);
    }

    auto enemies = g.enemies();

    for (Unit* u : g.mine()) {
        if (u->tag == "nc_help" || u->tag == "sc_help") {
            bool north = (u->tag == "nc_help");
            if (g.time < 27.0f) { g.hold(u->id); continue; }
            Unit* ca = nullptr; float cz = 1e9f;
            for (Unit* e : enemies) if ((north ? (e->x > 5.0f) : (e->x < -5.0f)) && e->z < cz) { cz = e->z; ca = e; }
            if (ca) { if (u->order != 打 || u->target != ca->id) g.attack(u->id, ca->id); }
            else g.hold(u->id);
            continue;
        }
        if (u->tag == "nc" || u->tag == "sc") {
            bool north = (u->tag == "nc");
            float sx = north ? 7.0f : -7.0f;
            Prop* b_near = nullptr; Prop* b_far = nullptr;
            for (auto& p : g.props) if (p.type == "snowball" && p.alive && !p.triggered && (north ? (p.x > 0) : (p.x < 0))) {
                if (p.z < (north ? 25.0f : 30.0f)) b_near = &p; else b_far = &p;
            }
            if (b_near) {
                float tn = north ? 0.0f : 9.5f;
                if (g.time < tn) {
                    if (Game::dist(u->x, u->z, sx, 20.0f) > 0.8f) g.move_straight(u->id, sx, 20.0f);
                    else g.hold(u->id);
                } else {
                    if (u->order != 推 || u->target != b_near->id) g.push(u->id, b_near->id);
                }
                continue;
            }
            if (b_far) {
                if (g.time < 18.5f) {
                    if (Game::dist(u->x, u->z, sx, 24.5f) > 0.8f) g.move_straight(u->id, sx, 24.5f);
                    else g.hold(u->id);
                } else {
                    if (u->order != 推 || u->target != b_far->id) g.push(u->id, b_far->id);
                }
                continue;
            }
            if (g.time < 26.0f) {
                if (Game::dist(u->x, u->z, sx, 25.0f) > 1.0f) g.move_straight(u->id, sx, 25.0f);
                else g.hold(u->id);
                continue;
            }
            Unit* ca = nullptr; float cz = 1e9f;
            for (Unit* e : enemies) if ((north ? (e->x > 5.0f) : (e->x < -5.0f)) && e->z < cz) { cz = e->z; ca = e; }
            if (ca) {
                if (u->order != 打 || u->target != ca->id) g.attack(u->id, ca->id);
            } else {
                g.hold(u->id);
            }
            continue;
        }

        if (u->kind == 炮仗兵) {
            if (u->fuse) continue;
            if (g.time < 16.0f) { g.hold(u->id); continue; }
            int bi = u->tag.size() > 2 ? (u->tag[2] - '1') : 0;
            if (bi >= 1 && g.time < 40.0f) { g.hold(u->id); continue; }
            if (bi == 2 && !g.by_tag("vb2").empty()) { g.hold(u->id); continue; }

            Unit* best = nullptr; int bc = 2;
            for (Unit* e : enemies) {
                if (std::fabs(e->x) > 4.5f || e->z < 14.0f || e->z > 32.0f) continue;
                int c = 0;
                for (Unit* e2 : enemies) if (Game::dist(*e, *e2) < 3.2f) ++c;
                if (c > bc) { bc = c; best = e; }
            }
            if (best) {
                int ne = 0;
                for (Unit* e : enemies) if (Game::dist(*u, *e) < 2.6f) ++ne;
                if ((ne >= 4 && u->z > 13.5f) || (Game::dist(*u, *best) < 0.9f && u->z > 13.5f)) g.detonate(u->id);
                else g.move_straight(u->id, best->x, best->z);
            } else g.hold(u->id);
            continue;
        }

        bool any_valley_close = false;
        for (Unit* e : enemies) if (std::fabs(e->x) < 5.0f && e->z < 35.0f) any_valley_close = true;

        Unit* leak = nullptr;
        for (Unit* e : enemies) if (std::fabs(e->x) < 5.0f && e->z < 7.0f) leak = e;
        if (leak) g.attack(u->id, leak->id);
        else if (g.time > 68.0f && !any_valley_close && g.flying.empty()) {
            Unit* ve = nullptr; float vz = 1e9f;
            for (Unit* e : enemies) if (std::fabs(e->x) < 5.0f && e->z < vz) { vz = e->z; ve = e; }
            if (ve && (u->order != 打 || u->target != ve->id)) g.attack(u->id, ve->id);
            else if (!ve) g.hold(u->id);
        } else {
            float home_z = (u->kind == 盾卫) ? 8.5f : 7.5f;
            if (!g.nearest_enemy(u->x, u->z, 6.0f) && std::fabs(u->z - home_z) > 0.8f) {
                g.move_straight(u->id, u->x, home_z);
            } else if (!u->holding) {
                g.hold(u->id);
            }
        }
    }
}
} // namespace level4

namespace level5 {
void on_start(Game& g) {
    for (Unit* u : g.mine()) g.hold(u->id);
}

void on_tick(Game& g) {
    auto enemies = g.enemies();
    Unit* boss = nullptr;
    int e_cav = 0;
    for (Unit* e : enemies) {
        if (e->kind == 镇山兽) boss = e;
        else if (e->kind == 骑兵) ++e_cav;
    }
    if (!boss) return;

    BossInfo bi;
    if (!g.bosses.empty()) bi = g.bosses[0];
    float bfx = std::sin(boss->facing), bfz = std::cos(boss->facing);

    if (e_cav > 0 && g.time < 16.0f) {
        for (Unit* u : g.mine()) {
            bool p_south_alive = false;
            for (auto& p : g.props) if (p.type == "pillar" && p.alive && std::fabs(p.x) < 1.0f && p.z < 15.0f) p_south_alive = true;
            if (!p_south_alive && (bi.state == 刨地 || bi.state == 冲撞)) {
                float ax = u->x - boss->x, az = u->z - boss->z;
                float along = ax * bi.dir_x + az * bi.dir_z, side = ax * bi.dir_z - az * bi.dir_x;
                if (along > 0 && std::fabs(side) < 3.0f) {
                    g.move_straight(u->id, u->x + (side >= 0 ? 4.5f : -4.5f) * bi.dir_z, u->z - (side >= 0 ? 4.5f : -4.5f) * bi.dir_x);
                    continue;
                }
            }
            Unit* stuck_cav = nullptr;
            for (Unit* e : enemies) if (e->kind == 骑兵 && Game::dist(*u, *e) < 2.2f && std::hypot(e->vx, e->vz) < 2.5f) stuck_cav = e;
            if (stuck_cav) g.attack(u->id, stuck_cav->id);
            else if (!u->holding) g.hold(u->id);
        }
        return;
    }

    for (Unit* u : g.mine()) {
        if (u->tag == "decoy") {
            Prop* best_p = nullptr; float bsc = -1e9f;
            for (auto& p : g.props) {
                if (p.type != "pillar" || !p.alive) continue;
                float db = Game::dist(boss->x, boss->z, p.x, p.z);
                if (db < 4.5f || db > 24.0f) continue;
                float sc = -p.z * 0.6f - Game::dist(u->x, u->z, p.x, p.z) * 0.2f;
                if (sc > bsc) { bsc = sc; best_p = &p; }
            }
            if (best_p && bi.state != 冲撞) {
                float dx = best_p->x - boss->x, dz = best_p->z - boss->z, l = std::max(0.1f, std::hypot(dx, dz));
                float tx = best_p->x + dx / l * 5.0f, tz = best_p->z + dz / l * 5.0f;
                float rx = tx, rz = tz - 25.0f, rl = std::hypot(rx, rz);
                if (rl > 21.5f) { tx = rx / rl * 21.5f; tz = 25.0f + rz / rl * 21.5f; }
                if (Game::dist(u->x, u->z, tx, tz) > 0.6f) g.move_straight(u->id, tx, tz);
                else g.hold(u->id);
            } else {
                if (bi.state == 冲撞) {
                    float ax = u->x - boss->x, az = u->z - boss->z;
                    float side = ax * bi.dir_z - az * bi.dir_x;
                    float sgn = (side >= 0) ? 1.0f : -1.0f;
                    float tx = u->x + sgn * bi.dir_z * 6.0f, tz = u->z - sgn * bi.dir_x * 6.0f;
                    float rx = tx, rz = tz - 25.0f, rl = std::hypot(rx, rz);
                    if (rl > 21.5f) { tx = rx / rl * 21.5f; tz = 25.0f + rz / rl * 21.5f; }
                    g.move_straight(u->id, tx, tz);
                } else {
                    float tx = (boss->x >= 0 ? -16.0f : 16.0f), tz = 9.0f;
                    if (Game::dist(u->x, u->z, tx, tz) > 1.0f) g.move_straight(u->id, tx, tz);
                    else g.hold(u->id);
                }
            }
            continue;
        }

        if (bi.state == 刨地 || bi.state == 冲撞) {
            float ax = u->x - boss->x, az = u->z - boss->z;
            float along = ax * bi.dir_x + az * bi.dir_z;
            float side = ax * bi.dir_z - az * bi.dir_x;
            if (along > -1.0f && std::fabs(side) < 3.2f) {
                float sgn = (side >= 0.0f) ? 1.0f : -1.0f;
                float tx = u->x + sgn * bi.dir_z * 4.5f, tz = u->z - sgn * bi.dir_x * 4.5f;
                float rx = tx, rz = tz - 25.0f, rl = std::hypot(rx, rz);
                if (rl > 21.5f) { tx = rx / rl * 21.5f; tz = 25.0f + rz / rl * 21.5f; }
                g.move_straight(u->id, tx, tz);
                continue;
            }
        }

        float rx = u->x - boss->x, rz = u->z - boss->z;
        float d_boss = std::hypot(rx, rz);
        float front_dot = (rx * bfx + rz * bfz) / std::max(0.1f, d_boss);
        if (bi.state != 晕 && d_boss < 5.2f && front_dot > 0.30f) {
            float side = rx * bfz - rz * bfx;
            float sgn = (side >= 0.0f) ? 1.0f : -1.0f;
            float tx = boss->x - bfx * 2.6f + sgn * bfz * 2.6f;
            float tz = boss->z - bfz * 2.6f - sgn * bfx * 2.6f;
            float crx = tx, crz = tz - 25.0f, crl = std::hypot(crx, crz);
            if (crl > 21.5f) { tx = crx / crl * 21.5f; tz = 25.0f + crz / crl * 21.5f; }
            g.move_straight(u->id, tx, tz);
        } else {
            if (u->order != 打 || u->target != boss->id) g.attack(u->id, boss->id);
        }
    }
}
} // namespace level5

namespace pvp {

static bool stone_danger(const Game& g, float px, float pz) {
    for (const auto& f : g.flying) {
        float v = std::sqrt(f.vx*f.vx + f.vy*f.vy + f.vz*f.vz);
        if (v < 4.0f) continue;
        float hv = std::hypot(f.vx, f.vz);
        float dx = hv > 0.1f ? f.vx / hv : 0.0f, dz = hv > 0.1f ? f.vz / hv : -1.0f;
        for (float t = -1.0f; t <= 6.0f; t += 1.5f) {
            if (Game::dist(px, pz, f.land_x + dx * t, f.land_z + dz * t) < 3.3f) return true;
        }
        if (Game::dist(px, pz, f.x, f.z) < 3.0f) return true;
    }
    return false;
}

void duel_tick(Game& g) {
    auto enemies = g.enemies();
    auto my_units = g.mine();
    bool is_river = (g.map == "河心洲");

    if (enemies.empty()) {
        for (Unit* u : my_units) {
            if (u->kind != 投石车 && !g.in_score_zone(u->x, u->z))
                g.move(u->id, is_river ? 8.0f : 0.0f, 28.0f);
        }
        return;
    }

    // Track our frontmost Giant so faster Spears & Archers NEVER run ahead of our 4000-HP Giant shield wall!
    auto giants = g.mine_of(大块头);
    float giant_x = is_river ? 8.0f : 0.0f;
    float giant_z = -1e9f;
    if (!giants.empty()) {
        giant_x = 0.0f;
        for (Unit* gt : giants) {
            giant_x += gt->x;
            giant_z = std::max(giant_z, gt->z);
        }
        giant_x /= giants.size();
    }

    float hold_x = is_river ? 7.5f : 0.0f;
    float hold_z = is_river ? 24.0f : 21.5f;

    // Check if any enemy has stepped forward out of spawn to contest center (z < 36.5) OR has artillery (投石车) forcing us to push!
    bool enemy_contesting = false;
    for (Unit* e : enemies) {
        if (e->z < 36.5f || g.in_score_zone(e->x, e->z) || e->kind == 投石车) { enemy_contesting = true; break; }
    }

    for (Unit* u : my_units) {
        // Dodge incoming catapult boulders for squishies!
        if (u->kind != 大块头 && stone_danger(g, u->x, u->z)) {
            float bx = u->x, bz = u->z, bsc = -1e9f;
            for (int k = 0; k < 12; ++k) {
                float ang = k * (3.14159265f * 2.0f / 12.0f);
                float nx = u->x + std::cos(ang) * 4.5f, nz = u->z + std::sin(ang) * 4.5f;
                if (!g.walkable(nx, nz) || stone_danger(g, nx, nz)) continue;
                float sc = (g.in_score_zone(nx, nz) ? 5.0f : 0.0f) - std::fabs(nz - (giant_z > 0 ? giant_z - 3.0f : 24.0f)) * 0.3f;
                if (sc > bsc) { bsc = sc; bx = nx; bz = nz; }
            }
            g.move_straight(u->id, bx, bz);
            continue;
        }

        // GIANTS: March onto score zone bridgehead (hold_x, hold_z); attack any enemy contesting z < 38 (NEVER over-chase into enemy home spawn z > 38 if we own the score zone!)
        if (u->kind == 大块头) {
            if (is_river && u->z < 21.8f) {
                // Cross cleanly via 5m-wide 大桥 x = 8.0!
                g.move(u->id, (u->id % 2 == 0 ? 7.0f : 9.0f), 25.5f);
                continue;
            }
            Unit* best = nullptr; float bsc = 1e9f;
            for (Unit* e : enemies) {
                // Never over-chase deep into enemy spawn (z > 37.0) after midgame if we already hold score zone!
                if (!enemy_contesting && e->z > 36.5f) continue;
                if (g.time > 50.0f && !g.in_score_zone(e->x, e->z) && e->z > 36.5f) continue;
                float sc = Game::dist(*u, *e);
                // Prefer hitting dense melee clumps or enemy Giant!
                int crowd = 0;
                for (Unit* e2 : enemies) if (Game::dist(*e, *e2) < 3.5f) ++crowd;
                sc -= crowd * 1.2f;
                if (sc < bsc) { bsc = sc; best = e; }
            }
            if (best) {
                if (u->order != 打 || u->target != best->id) g.attack(u->id, best->id);
            } else {
                float gx = hold_x + (u->id % 2 == 0 ? -1.5f : 1.5f);
                if (Game::dist(u->x, u->z, gx, hold_z) > 1.2f) g.move(u->id, gx, hold_z);
                else g.hold(u->id);
            }
            continue;
        }

        // ARCHERS: Escort 3.5m BEHIND Giants; snipe enemy Bombers -> Archers -> Spears -> Giants!
        if (u->kind == 弓手) {
            Unit* close_e = g.nearest_enemy(u->x, u->z, 4.0f);
            if (close_e && close_e->kind != 弓手) {
                float dx = u->x - close_e->x, dz = u->z - close_e->z, l = std::max(0.1f, std::hypot(dx, dz));
                float nx = u->x + dx/l * 4.0f, nz = u->z + dz/l * 4.0f;
                if (g.walkable(nx, nz)) { g.move(u->id, nx, nz); continue; }
            }
            float max_z_allowed = (!giants.empty()) ? (giant_z - 2.0f) : 26.0f;
            float max_r = 27.5f + std::max(0.0f, u->y) * 1.3f;

            // In final 15s, ensure inside score zone!
            if (g.time > g.time_limit - 15.0f && !g.in_score_zone(u->x, u->z)) {
                g.move(u->id, hold_x, is_river ? 23.0f : 23.0f);
                continue;
            }

            Unit* best = nullptr; float bsc = -1e9f;
            for (Unit* e : enemies) {
                float d = Game::dist(*u, *e);
                if (d > max_r) continue;
                float sc = 0.0f;
                if (e->kind == 炮仗兵) sc += 600.0f;
                else if (e->kind == 弓手) sc += 140.0f;
                else if (e->kind == 长枪兵 || e->kind == 大锤) sc += 100.0f;
                else if (e->kind == 骑兵) sc += 90.0f;
                else if (e->kind == 大块头) sc += 75.0f;
                else if (e->kind == 盾卫) sc += 20.0f;
                sc += (1.0f - std::min(1.0f, e->hp / std::max(1.0f, g.info[e->kind].hp))) * 90.0f;
                sc -= d * 1.2f;
                if (sc > bsc) { bsc = sc; best = e; }
            }

            if (best && u->z <= max_z_allowed + 1.5f) {
                if (u->order != 打 || u->target != best->id) g.attack(u->id, best->id);
            } else {
                float tx = ((!giants.empty()) ? giant_x : hold_x) + ((u->id % 5) - 2) * 1.1f;
                float tz = std::min(max_z_allowed, is_river ? 23.5f : 25.0f);
                if (is_river && u->z < 21.8f && tz > 21.5f) { tx = 8.0f; tz = 22.8f; }
                if (Game::dist(u->x, u->z, tx, tz) > 1.3f) g.move(u->id, tx, tz);
                else if (best) g.attack(u->id, best->id);
                else g.hold(u->id);
            }
            continue;
        }

        // SPEARS & MILITIA: Escort right behind/beside Giants (never run ahead of giant_z + 0.5m!), brace vs cavalry, melt enemy Giants & melee!
        if (u->kind == 长枪兵) {
            bool cav_rush = false;
            for (Unit* e : enemies) {
                if (e->kind == 骑兵 && Game::dist(*u, *e) < 13.0f && std::hypot(e->vx, e->vz) > 3.2f) {
                    cav_rush = true; break;
                }
            }
            if (cav_rush) {
                if (!u->holding) g.hold(u->id);
                continue;
            }
        }

        if (is_river && u->z < 21.8f && (giants.empty() || giant_z > 20.0f)) {
            g.move(u->id, 8.0f, 23.5f);
            continue;
        }

        float max_melee_z = (!giants.empty()) ? (giant_z + 0.8f) : 34.0f;
        Unit* best_m = nullptr; float bsc = 1e9f;
        for (Unit* e : enemies) {
            if (!enemy_contesting && e->z > 36.5f) continue;
            if (g.time > 50.0f && !g.in_score_zone(e->x, e->z) && e->z > 36.5f) continue;
            if (!giants.empty() && e->z > max_melee_z + 2.5f && Game::dist(*u, *e) > 4.0f) continue;
            float sc = Game::dist(*u, *e);
            if (e->kind == 大块头) sc -= 6.0f; // Spears shred enemy Giant while our Giants tank!
            if (sc < bsc) { bsc = sc; best_m = e; }
        }
        if (best_m) {
            if (u->order != 打 || u->target != best_m->id) g.attack(u->id, best_m->id);
        } else {
            float tx = ((!giants.empty()) ? giant_x : hold_x) + ((u->id % 6) - 2.5f) * 0.7f;
            float tz = (!giants.empty()) ? (giant_z - 1.2f) : (hold_z - 1.0f);
            tz = std::max(2.0f, tz);
            if (Game::dist(u->x, u->z, tx, tz) > 1.2f) g.move(u->id, tx, tz);
            else g.hold(u->id);
        }
    }
}

void melee_tick(Game& g) {
    float cx, cz;
    g.to_local(0.0f, 0.0f, cx, cz);
    auto enemies = g.enemies();
    auto my_units = g.mine();
    if (my_units.empty()) return;

    float safe_h = g.water_at(g.time + 7.5f) + 0.45f;
    float r_max = (safe_h <= 0.0f) ? 26.5f : std::max(6.0f, 35.0f - (safe_h / 5.5714f) * 32.5f - 1.5f);
    float target_r = std::clamp(r_max - 2.0f, 5.5f, 24.0f);

    float mcx = 0, mcz = 0;
    for (Unit* u : my_units) { mcx += u->x; mcz += u->z; }
    mcx /= my_units.size(); mcz /= my_units.size();
    float cur_ang = std::atan2(mcx - cx, mcz - cz);

    float best_ang = cur_ang, best_ang_sc = -1e9f;
    for (int k = -3; k <= 3; ++k) {
        float ang = cur_ang + k * 0.25f;
        float px = cx + std::sin(ang) * target_r;
        float pz = cz + std::cos(ang) * target_r;
        if (g.height(px, pz) < safe_h) continue;
        float e_threat = 0.0f;
        for (Unit* e : enemies) {
            float d = Game::dist(px, pz, e->x, e->z);
            if (d < 16.0f) e_threat += g.cost_of(*e) / std::max(3.0f, d);
        }
        float sc = -e_threat - std::fabs(float(k)) * 8.0f;
        if (sc > best_ang_sc) { best_ang_sc = sc; best_ang = ang; }
    }
    float anchor_x = cx + std::sin(best_ang) * target_r;
    float anchor_z = cz + std::cos(best_ang) * target_r;

    Unit* my_giant = nullptr;
    for (Unit* u : my_units) if (u->kind == 大块头) my_giant = u;

    for (Unit* u : my_units) {
        if (g.height(u->x, u->z) < g.water + 0.65f) {
            float dx = cx - u->x, dz = cz - u->z, l = std::max(0.1f, std::hypot(dx, dz));
            g.move(u->id, u->x + dx/l * 6.0f, u->z + dz/l * 6.0f);
            continue;
        }

        if (u->kind == 弓手) {
            // Position Archers 2.5m BEHIND our Giant relative to nearest enemy so our Giant tanks all arrows & charges!
            Unit* ne = g.nearest_enemy(anchor_x, anchor_z);
            float home_x = anchor_x, home_z = anchor_z;
            if (my_giant && ne && g.height(my_giant->x, my_giant->z) >= safe_h) {
                float dx = my_giant->x - ne->x, dz = my_giant->z - ne->z, l = std::max(0.1f, std::hypot(dx, dz));
                float hx = my_giant->x + dx/l * 2.8f + ((u->id % 5) - 2) * 0.7f * (-dz/l);
                float hz = my_giant->z + dz/l * 2.8f + ((u->id % 5) - 2) * 0.7f * (dx/l);
                if (g.height(hx, hz) >= safe_h) { home_x = hx; home_z = hz; }
            }

            Unit* close_e = g.nearest_enemy(u->x, u->z, 4.2f);
            if ((close_e && close_e->kind != 弓手) || g.height(u->x, u->z) < safe_h) {
                g.move(u->id, home_x, home_z);
                continue;
            }

            float max_r = 27.5f + std::max(0.0f, u->y - 2.0f) * 1.0f;
            Unit* best = nullptr; float bsc = -1e9f;
            for (Unit* e : enemies) {
                float d = Game::dist(*u, *e);
                if (d > max_r) continue;
                float sc = 0.0f;
                if (e->kind == 炮仗兵 && d < 18.0f) sc += 600.0f;
                if (e->hp <= 20.0f) sc += g.cost_of(*e) * 8.0f;
                else if (e->hp <= 45.0f) sc += g.cost_of(*e) * 4.0f;
                else if (e->hp <= 90.0f) sc += g.cost_of(*e) * 1.5f;
                else sc += g.cost_of(*e) * 0.3f;
                if (e->kind == 盾卫 && e->hp > 35.0f) sc -= 40.0f;
                sc -= d * 1.2f;
                if (sc > bsc) { bsc = sc; best = e; }
            }
            if (best && Game::dist(u->x, u->z, home_x, home_z) < 5.5f) {
                if (u->order != 打 || u->target != best->id) g.attack(u->id, best->id);
            } else {
                if (Game::dist(u->x, u->z, home_x, home_z) > 1.8f) g.move(u->id, home_x, home_z);
                else if (best) g.attack(u->id, best->id);
                else g.hold(u->id);
            }
            continue;
        }

        // Giant & Melee screen:
        float engage_r = (g.time > 72.0f) ? 16.0f : 8.0f;
        Unit* threat = nullptr; float bsc = 1e9f;
        for (Unit* e : enemies) {
            if (g.height(e->x, e->z) < g.water + 0.5f) continue;
            float da = Game::dist(anchor_x, anchor_z, e->x, e->z);
            if (da > engage_r) continue;
            float sc = Game::dist(*u, *e) - (e->hp < 45.0f ? 5.0f : 0.0f);
            if (sc < bsc) { bsc = sc; threat = e; }
        }
        if (threat) {
            if (u->order != 打 || u->target != threat->id) g.attack(u->id, threat->id);
        } else {
            Unit* ne = g.nearest_enemy(anchor_x, anchor_z);
            float sx = anchor_x, sz = anchor_z;
            if (ne) {
                float dx = ne->x - anchor_x, dz = ne->z - anchor_z, l = std::max(0.1f, std::hypot(dx, dz));
                float tx = anchor_x + dx/l * 2.2f;
                float tz = anchor_z + dz/l * 2.2f;
                if (g.height(tx, tz) >= safe_h) { sx = tx; sz = tz; }
            }
            if (Game::dist(u->x, u->z, sx, sz) > 1.2f) g.move(u->id, sx, sz);
            else g.hold(u->id);
        }
    }
}

} // namespace pvp

void on_start(Game& g) {
    if (g.map == "第1关_草甸迎击") { level1::on_start(g); return; }
    if (g.map == "第2关_断龙桥") { level2::on_start(g); return; }
    if (g.map == "第3关_拔炮台") { level3::on_start(g); return; }
    if (g.map == "第4关_一线天") { level4::on_start(g); return; }
    if (g.map == "第5关_镇山兽") { level5::on_start(g); return; }
}

void on_tick(Game& g) {
    if (g.map == "第1关_草甸迎击") { level1::on_tick(g); return; }
    if (g.map == "第2关_断龙桥") { level2::on_tick(g); return; }
    if (g.map == "第3关_拔炮台") { level3::on_tick(g); return; }
    if (g.map == "第4关_一线天") { level4::on_tick(g); return; }
    if (g.map == "第5关_镇山兽") { level5::on_tick(g); return; }
    if (g.mode == "duel") { pvp::duel_tick(g); return; }
    if (g.mode == "melee") { pvp::melee_tick(g); return; }
}
