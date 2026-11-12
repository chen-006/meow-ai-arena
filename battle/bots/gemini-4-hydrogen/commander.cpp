#include "commander.hpp"
#include <map>

namespace {
std::map<int, int> cav_retreat_until;
}

void on_start(Game& g) {
    (void)g;
}

static void tick_level1(Game& g) {
    auto enemies = g.enemies();
    auto mine = g.mine();
    if (enemies.empty() || mine.empty()) return;

    bool melee_alive = false;
    for (Unit* e : enemies) {
        if (e->kind != 弓手) { melee_alive = true; break; }
    }
    for (Unit* u : mine) {
        if (melee_alive) {
            if (u->order != 原地守) g.hold(u->id);
        } else {
            if (u->order != 自由) g.free(u->id);
        }
    }
}

static void tick_level2(Game& g) {
    bool broken = g.terrain_removed("桥中段");
    auto enemies = g.enemies();
    if (enemies.empty()) return;

    if (!broken) {
        bool trig = false;
        for (auto& p : g.props) if (p.type == "keg" && p.alive && p.triggered) trig = true;
        if (!trig && g.time >= 3.5f) {
            for (Unit* a : g.mine_of(弓手)) {
                Prop* bk = nullptr; float bd = 1e9f;
                for (auto& p : g.props) if (p.type == "keg" && p.alive && !p.triggered) {
                    float d = Game::dist(a->x, a->z, p.x, p.z);
                    if (d < bd) { bd = d; bk = &p; }
                }
                if (bk) g.attack(a->id, bk->id);
            }
        }
        for (Unit* u : g.mine()) if (u->kind != 弓手 && u->kind != 投石车) g.hold(u->id);
        return;
    }

    for (Unit* u : g.mine()) {
        if (u->kind == 炮仗兵) {
            if (u->fuse) continue;
            bool vanguard_busy = false;
            for (Unit* e : enemies) if (e->z > 14.0f && e->x < 10.0f) vanguard_busy = true;
            if (vanguard_busy) {
                g.move(u->id, 18.0f, 5.0f);
                continue;
            }
            int n = 0;
            for (Unit* e : enemies) if (Game::dist(*u, *e) <= 3.2f) n++;
            if (n >= 4 || (enemies.size() <= 6 && n >= 2)) {
                g.detonate(u->id);
            } else {
                Unit* best = nullptr; float bd = 1e9f;
                for (Unit* e : enemies) {
                    if (Game::dist(e->x, e->z, 1.0f, 5.5f) < 4.5f) continue;
                    float d = Game::dist(*u, *e);
                    if (d < bd) { bd = d; best = e; }
                }
                if (best) g.attack(u->id, best->id);
            }
        } else if (u->kind == 弓手) {
            Unit* close = g.nearest_enemy(u->x, u->z, 4.5f);
            if (close) {
                float dx = u->x - close->x, dz = u->z - close->z, l = std::max(0.1f, std::hypot(dx, dz));
                g.move(u->id, std::max(-12.0f, std::min(18.0f, u->x + dx/l*4)), std::max(1.0f, std::min(16.0f, u->z + dz/l*4)));
            } else {
                Unit* ne = g.nearest_enemy(u->x, u->z, 27.0f);
                if (ne) g.attack(u->id, ne->id);
                else {
                    Unit* any_e = g.nearest_enemy(u->x, u->z);
                    if (any_e) g.move(u->id, any_e->x, any_e->z);
                }
            }
        } else {
            bool near_bomber = false;
            for (Unit* b : g.mine_of(炮仗兵)) {
                if (Game::dist(*u, *b) < 4.2f && (b->fuse || g.nearest_enemy(b->x, b->z, 4.5f) != nullptr)) {
                    near_bomber = true;
                }
            }
            if (near_bomber && Game::dist(u->x, u->z, 1.0f, 5.5f) > 5.0f) {
                g.move(u->id, 6.0f, 5.5f);
                continue;
            }
            if (u->tag == "塞子") {
                Unit* ne = g.nearest_enemy(u->x, u->z, 10.0f);
                if (ne) g.attack(u->id, ne->id);
                else g.move(u->id, 8.0f, 5.5f);
                continue;
            }
            Unit* e_flag = g.nearest_enemy(1.0f, 5.5f);
            if (e_flag && Game::dist(e_flag->x, e_flag->z, 1.0f, 5.5f) < 20.0f) {
                g.attack(u->id, e_flag->id);
            } else {
                g.move(u->id, 8.0f, 5.5f);
            }
        }
    }
}

static void tick_level3(Game& g) {
    auto enemies = g.enemies();
    if (enemies.empty()) return;

    bool g1 = false, g2 = false, g3 = false;
    for (Unit* e : enemies) {
        if (e->kind == 弓手 && e->x < -5.0f) g1 = true;
        else if (e->z > 48.0f) g2 = true;
        else if (e->x > 8.0f && e->z < 48.0f) g3 = true;
    }

    for (Unit* u : g.mine()) {
        if (u->y < 3.5f) {
            if (u->kind != 盾卫 && g.time < 2.5f) {
                g.hold(u->id);
                continue;
            }
            if (u->z < 31.0f && (u->x > -28.0f || u->x < -34.0f)) {
                g.move(u->id, -31.0f, 30.0f);
            } else {
                g.move(u->id, -31.0f, 43.0f);
            }
            continue;
        }
        Unit* very_near = g.nearest_enemy(u->x, u->z, 3.0f);
        if (very_near) { g.attack(u->id, very_near->id); continue; }

        int target_grp = g1 ? 1 : (g2 ? 2 : (g3 ? 3 : 4));
        Unit* best = nullptr; float bd = 1e9f;
        for (Unit* e : enemies) {
            int eg = 4;
            if (e->kind == 弓手 && e->x < -5.0f) eg = 1;
            else if (e->z > 48.0f) eg = 2;
            else if (e->x > 8.0f && e->z < 48.0f) eg = 3;
            if (eg != target_grp) continue;
            float d = Game::dist(*u, *e);
            if (d < bd) { bd = d; best = e; }
        }
        if (best) {
            if (target_grp == 3 && u->x < 8.0f && u->z < 47.0f) {
                g.move(u->id, 12.0f, 50.0f);
            } else {
                g.attack(u->id, best->id);
            }
        }
    }
}

static void tick_level4(Game& g) {
    auto enemies = g.enemies();
    if (enemies.empty()) return;

    if (Unit* l = g.lord()) {
        Unit* ne = g.nearest_enemy(l->x, l->z, 8.0f);
        if (ne) {
            float dx = l->x - ne->x, dz = l->z - ne->z, len = std::max(0.1f, std::hypot(dx, dz));
            g.move(l->id, std::max(-3.5f, std::min(3.5f, l->x + dx/len*4.0f)), std::max(0.5f, l->z + dz/len*4.0f));
        } else if (l->z > 2.0f) {
            g.move(l->id, 0.0f, 1.0f);
        } else {
            g.hold(l->id);
        }
    }

    for (Unit* u : g.mine()) {
        if (u->kind == 主帅) continue;

        if (u->tag == "北崖近" || u->tag == "北崖远" || u->tag == "南崖近" || u->tag == "南崖远") {
            bool north = (u->tag.find("北") != std::string::npos);
            bool far_sb = (u->tag.find("远") != std::string::npos);
            Prop* sb = nullptr;
            for (auto& p : g.props) {
                if (p.type == "snowball" && p.alive && !p.triggered) {
                    if ((north && p.x > 0) || (!north && p.x < 0)) {
                        if (far_sb && p.z > 25.0f) { sb = &p; break; }
                        if (!far_sb && p.z < 25.0f) { sb = &p; break; }
                    }
                }
            }
            if (!sb) {
                for (auto& p : g.props) {
                    if (p.type == "snowball" && p.alive && !p.triggered) {
                        if ((north && p.x > 0) || (!north && p.x < 0)) { sb = &p; break; }
                    }
                }
            }
            if (sb) {
                if (u->order != 推 || u->target != sb->id) g.push(u->id, sb->id);
                continue;
            }
            Unit* best = nullptr; float bde = 1e9f;
            for (Unit* e : enemies) {
                if ((north && e->x > 5.0f) || (!north && e->x < -5.0f)) {
                    float d = Game::dist(*u, *e);
                    if (d < bde) { bde = d; best = e; }
                }
            }
            if (best) g.attack(u->id, best->id);
            else g.hold(u->id);
            continue;
        }

        bool close_e = false;
        for (Unit* e : enemies) if (std::fabs(e->x) < 5.0f && e->z < 20.0f) close_e = true;
        if (g.time < 15.0f || close_e) {
            Unit* close = nullptr; float bd = 1e9f;
            for (Unit* e : enemies) if (std::fabs(e->x) < 5.0f && e->z < 14.0f) {
                float d = Game::dist(*u, *e);
                if (d < bd) { bd = d; close = e; }
            }
            if (close && (u->kind != 长枪兵 || Game::dist(*u, *close) <= 2.5f)) g.attack(u->id, close->id);
            else g.hold(u->id);
            continue;
        }
        Unit* ve = nullptr; float bd = 1e9f;
        for (Unit* e : enemies) if (std::fabs(e->x) < 5.0f) {
            float d = Game::dist(*u, *e);
            if (d < bd) { bd = d; ve = e; }
        }
        if (ve) g.attack(u->id, ve->id);
    }
}

static void tick_level5(Game& g) {
    auto enemies = g.enemies();
    Unit* boss = nullptr;
    BossInfo* bi = g.bosses.empty() ? nullptr : &g.bosses[0];
    if (bi) boss = g.unit(bi->id);
    if (!boss || !boss->alive) return;

    int cav_alive = 0;
    for (Unit* e : enemies) if (e->kind == 骑兵) cav_alive++;

    Unit* baiter = nullptr;
    for (Unit* u : g.mine()) if (u->tag == "bait") { baiter = u; break; }

    Unit* farthest = nullptr; float max_bd = -1.0f;
    for (Unit* u : g.mine()) { float d = Game::dist(*u, *boss); if (d > max_bd) { max_bd = d; farthest = u; } }
    float cdx = bi->dir_x, cdz = bi->dir_z;
    if (bi->state != 冲撞 && farthest) {
        float dx = farthest->x - boss->x, dz = farthest->z - boss->z, l = std::max(0.1f, std::hypot(dx, dz));
        cdx = dx / l; cdz = dz / l;
    }

    auto clamp_arena = [](float& tx, float& tz, float maxr = 22.0f) {
        float cx = tx, cz = tz - 25.0f, cl = std::hypot(cx, cz);
        if (cl > maxr) { tx = cx / cl * maxr; tz = 25.0f + cz / cl * maxr; }
    };

    float pred_x = boss->x, pred_z = boss->z;
    if (bi->state == 冲撞) {
        float max_s = bi->state_left;
        for (float s = 1.0f; s <= max_s; s += 1.0f) {
            float px = boss->x + bi->dir_x * s, pz = boss->z + bi->dir_z * s;
            if (Game::dist(px, pz, 0.0f, 25.0f) >= 24.0f) { pred_x = px; pred_z = pz; break; }
            bool hit_pil = false;
            for (auto& pr : g.props) {
                if (pr.type == "pillar" && pr.alive && Game::dist(px, pz, pr.x, pr.z) < 2.3f) { pred_x = px; pred_z = pz; hit_pil = true; break; }
            }
            if (hit_pil) break;
            pred_x = px; pred_z = pz;
        }
    }

    for (Unit* u : g.mine()) {
        if (u == baiter) {
            if (cav_alive > 0 && g.time < 9.0f) { g.move(u->id, 0.0f, 1.5f); continue; }
            if (bi->state == 刨地 || bi->state == 冲撞) {
                if (bi->state == 刨地 && bi->state_left > 0.72f) { g.hold(u->id); continue; }
                float px = cdz, pz = -cdx;
                float d1 = Game::dist(u->x + px*6.0f, u->z + pz*6.0f, 0.0f, 25.0f);
                float d2 = Game::dist(u->x - px*6.0f, u->z - pz*6.0f, 0.0f, 25.0f);
                float sign = (d1 < d2) ? 1.0f : -1.0f;
                float tx = u->x + sign * px * 7.0f, tz = u->z + sign * pz * 7.0f;
                clamp_arena(tx, tz, 22.0f);
                g.move(u->id, tx, tz);
            } else {
                if (boss->z > 9.5f) g.move(u->id, 0.0f, 2.0f);
                else {
                    float d1 = Game::dist(boss->x, boss->z, -16.0f, 8.0f);
                    float d2 = Game::dist(boss->x, boss->z, 16.0f, 8.0f);
                    if (d1 > d2) g.move(u->id, -16.0f, 8.0f); else g.move(u->id, 16.0f, 8.0f);
                }
            }
            continue;
        }
        if (cav_alive > 0 && g.time < 9.0f) {
            if (bi->state == 刨地 || bi->state == 冲撞) {
                float rx = u->x - boss->x, rz = u->z - boss->z;
                float along = rx * cdx + rz * cdz, side = rx * cdz - rz * cdx;
                if (along > -2.0f && std::fabs(side) < 3.5f) {
                    float sign = (side >= 0) ? 1.0f : -1.0f;
                    float tx = u->x + sign * cdz * 5.0f, tz = u->z - sign * cdx * 5.0f;
                    clamp_arena(tx, tz);
                    g.move(u->id, tx, tz);
                    continue;
                }
            }
            g.hold(u->id);
            continue;
        }
        Unit* near_cav = nullptr;
        for (Unit* e : enemies) if (e->kind == 骑兵 && Game::dist(*u, *e) < 5.0f) { near_cav = e; break; }
        if (near_cav) { g.attack(u->id, near_cav->id); continue; }
        if (bi->state == 刨地 || bi->state == 冲撞) {
            float rx = u->x - boss->x, rz = u->z - boss->z;
            float along = rx * cdx + rz * cdz, side = rx * cdz - rz * cdx;
            if (along > -2.5f && std::fabs(side) < 4.5f) {
                float px = cdz, pz = -cdx;
                float sign = (side >= 0) ? 1.0f : -1.0f;
                float tx = u->x + sign * px * 5.5f, tz = u->z + sign * pz * 5.5f;
                clamp_arena(tx, tz, 21.5f);
                g.move(u->id, tx, tz);
                continue;
            }
            if (bi->state == 冲撞 && (along <= 0.0f || Game::dist(boss->x, boss->z, pred_x, pred_z) < 6.5f)) {
                float tx = pred_x, tz = std::min(15.0f, pred_z + 1.0f);
                clamp_arena(tx, tz, 21.0f);
                g.move(u->id, tx, tz);
                continue;
            }
        }
        if (bi->state == 晕 && bi->state_left > 0.30f) {
            g.attack(u->id, boss->id);
            continue;
        }
        float db = Game::dist(*u, *boss);
        if (boss->z > 16.5f && std::fabs(boss->x) < 8.5f) {
            g.move(u->id, 0.0f, 11.0f);
        } else if (db < 5.2f || (bi->state == 晕 && bi->state_left <= 0.30f)) {
            float dx = u->x - boss->x, dz = u->z - boss->z, l = std::max(0.1f, std::hypot(dx, dz));
            float tx = boss->x + dx/l*5.6f, tz = std::min(14.5f, boss->z + dz/l*5.6f);
            clamp_arena(tx, tz, 20.5f);
            g.move(u->id, tx, tz);
        } else {
            float dx = u->x - boss->x, dz = u->z - boss->z, l = std::max(0.1f, std::hypot(dx, dz));
            float tx = boss->x + dx/l*5.5f, tz = std::min(14.0f, boss->z + dz/l*5.5f);
            clamp_arena(tx, tz, 20.5f);
            if (Game::dist(u->x, u->z, tx, tz) > 1.2f) g.move(u->id, tx, tz);
            else g.hold(u->id);
        }
    }
}

static void tick_duel(Game& g) {
    auto enemies = g.enemies();
    auto mine = g.mine();
    if (enemies.empty() || mine.empty()) return;

    bool is_river = (g.map == "河心洲");
    float time_left = g.time_limit - g.time;

    std::vector<Unit*> fusing_bombers;
    for (Unit* b : g.mine_of(炮仗兵)) if (b->fuse) fusing_bombers.push_back(b);

    for (Unit* cat : g.mine_of(投石车)) {
        float bx = 0, bz = 0, best_score = -1e9f;
        for (Unit* e : enemies) {
            float px = e->x + e->vx * 1.2f, pz = e->z + e->vz * 1.2f;
            float d = Game::dist(cat->x, cat->z, px, pz);
            if (d < 15.0f || d > 45.0f) continue;
            float score = 0;
            for (Unit* e2 : enemies) {
                float p2x = e2->x + e2->vx * 1.2f, p2z = e2->z + e2->vz * 1.2f;
                if (Game::dist(px, pz, p2x, p2z) <= 3.5f) {
                    score += (e2->kind == 投石车 || e2->kind == 弓手) ? 1.5f : 1.0f;
                }
            }
            for (Unit* m : mine) if (Game::dist(px, pz, m->x, m->z) <= 4.0f) score -= 2.5f;
            if (score > best_score) { best_score = score; bx = px; bz = pz; }
        }
        if (best_score > 0.2f) g.aim(cat->id, bx, bz);
    }

    for (Unit* a : g.mine_of(弓手)) {
        Unit* close = g.nearest_enemy(a->x, a->z, 3.8f);
        if (close && close->kind != 投石车) {
            float dx = a->x - close->x, dz = a->z - close->z, l = std::max(0.1f, std::hypot(dx, dz));
            float tx = a->x + dx/l*3.0f, tz = a->z + dz/l*3.0f;
            if (is_river && tz > 16.0f && a->z < 17.0f) tz = 15.0f;
            if (g.walkable(tx, tz)) { g.move(a->id, tx, tz); continue; }
        }
        if (is_river && time_left > 15.0f && a->z > 16.0f) {
            g.move(a->id, 8.0f, 15.0f);
            continue;
        }
        float max_r = 28.0f + std::max(0.0f, a->y) * 1.5f;
        Unit* best = nullptr; float best_s = -1e9f;
        for (Unit* e : enemies) {
            float d = Game::dist(*a, *e);
            if (d > max_r) continue;
            float s = -d;
            if (e->kind == 炮仗兵) s += 50.0f;
            else if (e->kind == 弓手) s += 12.0f;
            else if (e->kind == 投石车) s += 8.0f;
            else if (e->kind == 大锤) s += 6.0f;
            if (e->kind == 盾卫) s -= 6.0f;
            if (s > best_s) { best_s = s; best = e; }
        }
        if (best) {
            if (a->order != 打 || a->target != best->id) g.attack(a->id, best->id);
        } else {
            if (is_river) {
                if (time_left <= 15.0f && !g.in_score_zone(a->x, a->z)) {
                    if (a->z < 17.0f) g.move(a->id, 8.0f, 18.0f);
                    else g.move(a->id, 6.0f, 26.0f);
                } else if (a->z < 14.5f) {
                    g.move(a->id, a->x, 15.0f);
                } else {
                    g.hold(a->id);
                }
            } else {
                Unit* ne = g.nearest_enemy(a->x, a->z);
                if (ne) g.move(a->id, (a->x + ne->x)*0.5f, std::min(28.0f, a->z + 6.0f));
            }
        }
    }

    for (Unit* b : g.mine_of(炮仗兵)) {
        if (b->fuse) continue;
        int e_near = 0, m_near = 0; bool giant_near = false;
        for (Unit* e : enemies) if (Game::dist(*b, *e) <= 3.2f) { e_near++; if (e->kind == 大块头) giant_near = true; }
        for (Unit* m : mine) if (m != b && Game::dist(*b, *m) <= 3.2f) m_near++;
        if ((e_near >= 4 && m_near <= 2) || (giant_near && m_near <= 1) || (e_near >= 2 && b->hp < 25.0f)) {
            g.detonate(b->id);
            continue;
        }
        if (is_river && b->z < 20.5f && std::fabs(b->x - 8.0f) > 2.2f) {
            g.move(b->id, 8.0f, 18.5f);
            continue;
        }
        Unit* best = nullptr; int bc = -1;
        for (Unit* e : enemies) {
            int c = 0;
            for (Unit* e2 : enemies) if (Game::dist(*e, *e2) <= 3.2f) c++;
            if (e->kind == 大块头) c += 3;
            if (c > bc) { bc = c; best = e; }
        }
        if (best) g.move(b->id, best->x, best->z);
    }

    for (Unit* c : g.mine_of(骑兵)) {
        float spd = std::hypot(c->vx, c->vz);
        if (cav_retreat_until.count(c->id) && g.tick < cav_retreat_until[c->id]) continue;
        if (is_river && c->z < 21.5f && (c->z < 16.5f || std::fabs(c->x - 8.0f) > 2.0f)) {
            Unit* bank_e = nullptr;
            for (Unit* e : enemies) if (e->z < 17.5f) { bank_e = e; break; }
            if (bank_e) g.attack(c->id, bank_e->id);
            else g.move(c->id, 8.0f, 22.5f);
            continue;
        }
        Unit* near = g.nearest_enemy(c->x, c->z, 2.5f);
        if (near && spd < 2.2f && c->state == 站着 && !is_river) {
            float dx = c->x - near->x, dz = c->z - near->z, l = std::max(0.1f, std::hypot(dx, dz));
            float tx = c->x + dx/l*8.0f, tz = c->z + dz/l*8.0f;
            if (g.walkable(tx, tz)) {
                g.move(c->id, tx, tz);
                cav_retreat_until[c->id] = g.tick + 15;
                continue;
            }
        }
        Unit* best = nullptr; float bd = 1e9f;
        for (Unit* e : enemies) {
            if (is_river && e->z > 35.0f && c->z < 34.0f) continue;
            float d = Game::dist(*c, *e);
            if (e->kind == 弓手 || e->kind == 投石车) d -= 16.0f;
            if (e->kind == 长枪兵 && e->braced) d += 12.0f;
            if (d < bd) { bd = d; best = e; }
        }
        if (best) g.attack(c->id, best->id);
    }

    for (Unit* u : mine) {
        if (u->kind == 投石车 || u->kind == 弓手 || u->kind == 炮仗兵 || u->kind == 骑兵) continue;

        bool dodge_b = false;
        for (Unit* b : fusing_bombers) {
            if (Game::dist(*u, *b) < 3.8f) {
                float dx = u->x - b->x, dz = u->z - b->z, l = std::max(0.1f, std::hypot(dx, dz));
                float tx = u->x + dx/l*4.0f, tz = u->z + dz/l*4.0f;
                if (g.walkable(tx, tz)) { g.move(u->id, tx, tz); dodge_b = true; break; }
            }
        }
        if (dodge_b) continue;

        if (u->kind == 长枪兵) {
            bool cav_near = false;
            for (Unit* e : enemies) {
                if (e->kind == 骑兵 && Game::dist(*u, *e) < 16.0f && std::hypot(e->vx, e->vz) > 3.2f) {
                    cav_near = true; break;
                }
            }
            if (cav_near) { g.hold(u->id); continue; }
        }

        if (time_left < 18.0f && !g.score_zone.empty() && !g.in_score_zone(u->x, u->z)) {
            if (is_river) {
                if (u->z < 17.0f) g.move(u->id, 8.0f, 18.0f);
                else g.move(u->id, 5.0f, 26.0f);
            } else {
                g.move(u->id, 0.0f, 28.0f);
            }
            continue;
        }

        if (is_river) {
            Unit* near_bank = g.nearest_enemy(u->x, u->z, 10.0f);
            if (near_bank) {
                g.attack(u->id, near_bank->id);
            } else if (u->z < 17.0f) {
                if (std::fabs(u->x - 8.0f) > 2.2f) g.move(u->id, 8.0f, 15.5f);
                else g.move(u->id, 8.0f, 23.5f);
            } else if (u->z < 21.5f) {
                g.move(u->id, 8.0f, 24.0f);
            } else {
                Unit* best = nullptr; float bd = 1e9f;
                for (Unit* e : enemies) {
                    if (e->z > 39.0f && u->z < 34.0f) continue;
                    float d = Game::dist(*u, *e);
                    if (d < bd) { bd = d; best = e; }
                }
                if (best) g.attack(u->id, best->id);
                else {
                    Unit* any_e = g.nearest_enemy(u->x, u->z);
                    if (any_e && time_left > 22.0f) {
                        if (u->z < 35.0f && std::fabs(u->x + 8.0f) > 2.5f) g.move(u->id, -8.0f, 33.5f);
                        else g.attack(u->id, any_e->id);
                    } else if (!g.in_score_zone(u->x, u->z)) {
                        g.move(u->id, 5.0f, 26.0f);
                    } else {
                        g.hold(u->id);
                    }
                }
            }
        } else {
            Unit* best = nullptr; float bd = 1e9f;
            for (Unit* e : enemies) {
                float d = Game::dist(*u, *e);
                if (u->kind == 大锤 && (e->kind == 盾卫 || e->kind == 大块头)) d -= 4.0f;
                if (d < bd) { bd = d; best = e; }
            }
            if (best) g.attack(u->id, best->id);
        }
    }
}

static void tick_melee(Game& g) {
    auto enemies = g.enemies();
    auto mine = g.mine();
    if (enemies.empty() || mine.empty()) return;

    float cx = 0, cz = 31.0f;
    g.to_local(0, 0, cx, cz);
    float safe_h = std::max(g.water, g.water_at(g.time + 5.0f)) + 0.45f;

    std::vector<Unit*> fusing_bombers;
    for (Unit* b : g.mine_of(炮仗兵)) if (b->fuse) fusing_bombers.push_back(b);

    for (Unit* u : mine) {
        float uh = g.height(u->x, u->z);
        if (uh < safe_h && u->kind != 投石车) {
            float dx = u->x - cx, dz = u->z - cz;
            float r = std::max(0.1f, std::hypot(dx, dz));
            g.move(u->id, cx + dx/r*5.0f, cz + dz/r*5.0f);
            continue;
        }

        bool dodge_b = false;
        for (Unit* b : fusing_bombers) {
            if (u != b && Game::dist(*u, *b) < 3.6f) {
                float dx = u->x - b->x, dz = u->z - b->z, l = std::max(0.1f, std::hypot(dx, dz));
                float tx = u->x + dx/l*4.0f, tz = u->z + dz/l*4.0f;
                if (g.height(tx, tz) >= safe_h) { g.move(u->id, tx, tz); dodge_b = true; break; }
            }
        }
        if (dodge_b) continue;

        if (u->kind == 弓手) {
            Unit* close = g.nearest_enemy(u->x, u->z, 3.8f);
            if (close) {
                float dx = u->x - close->x, dz = u->z - close->z, l = std::max(0.1f, std::hypot(dx, dz));
                float tx = u->x + dx/l*3.5f, tz = u->z + dz/l*3.5f;
                if (g.height(tx, tz) >= safe_h) { g.move(u->id, tx, tz); continue; }
            }
            float max_r = 28.0f + std::max(0.0f, u->y) * 1.5f;
            Unit* best = nullptr; float bs = -1e9f;
            for (Unit* e : enemies) {
                float d = Game::dist(*u, *e);
                if (d > max_r) continue;
                float s = -d;
                if (e->kind == 炮仗兵) s += 40.0f;
                else if (e->hp <= 25.0f) s += 25.0f;
                else if (e->kind == 弓手) s += 10.0f;
                if (e->kind == 盾卫) s -= 5.0f;
                if (s > bs) { bs = s; best = e; }
            }
            if (best) g.attack(u->id, best->id);
            else if (g.time < 18.0f) {
                if (u->z < 12.0f) g.move(u->id, u->x * 0.6f, 14.0f);
                else g.hold(u->id);
            } else {
                float dx = u->x - cx, dz = u->z - cz, r = std::max(0.1f, std::hypot(dx, dz));
                if (r > 9.0f) g.move(u->id, cx + dx/r*7.0f, cz + dz/r*7.0f);
                else g.hold(u->id);
            }
            continue;
        }

        if (u->kind == 炮仗兵) {
            if (u->fuse) continue;
            int ec = 0, mc = 0; bool giant_n = false;
            for (Unit* e : enemies) if (Game::dist(*u, *e) <= 3.2f) { ec++; if (e->kind == 大块头) giant_n = true; }
            for (Unit* m : mine) if (m != u && Game::dist(*u, *m) <= 3.2f) mc++;
            if ((ec >= 4 && mc <= 2) || (giant_n && mc <= 1) || (ec >= 2 && u->hp < 22.0f)) {
                g.detonate(u->id);
                continue;
            }
            if (g.time < 18.0f && !g.nearest_enemy(u->x, u->z, 12.0f)) {
                g.move(u->id, u->x * 0.6f, 14.0f);
                continue;
            }
            Unit* best = nullptr; int bc = -1;
            for (Unit* e : enemies) {
                if (g.height(e->x, e->z) < g.water + 0.2f) continue;
                int c = 0;
                for (Unit* e2 : enemies) if (Game::dist(*e, *e2) <= 3.2f) c++;
                if (e->kind == 大块头) c += 3;
                if (c > bc) { bc = c; best = e; }
            }
            if (best) g.move(u->id, best->x, best->z);
            continue;
        }

        if (u->kind == 长枪兵) {
            bool cav_near = false;
            for (Unit* e : enemies) if (e->kind == 骑兵 && Game::dist(*u, *e) < 15.0f && std::hypot(e->vx, e->vz) > 3.2f) { cav_near = true; break; }
            if (cav_near) { g.hold(u->id); continue; }
        }

        Unit* close_e = g.nearest_enemy(u->x, u->z, 11.0f);
        if (g.time < 16.0f && !close_e) {
            if (u->z < 14.0f) g.move(u->id, u->x * 0.7f, 15.0f);
            else g.hold(u->id);
            continue;
        }

        Unit* best = nullptr; float bs = -1e9f;
        for (Unit* e : enemies) {
            if (g.height(e->x, e->z) < safe_h - 0.3f) continue;
            float d = Game::dist(*u, *e);
            float s = -d;
            if (e->hp <= 30.0f) s += 5.0f;
            if (u->kind == 大锤 && (e->kind == 盾卫 || e->kind == 大块头)) s += 4.0f;
            if (s > bs) { bs = s; best = e; }
        }
        if (best) g.attack(u->id, best->id);
        else {
            float dx = u->x - cx, dz = u->z - cz, r = std::max(0.1f, std::hypot(dx, dz));
            g.move(u->id, cx + dx/r*6.0f, cz + dz/r*6.0f);
        }
    }
}

void on_tick(Game& g) {
    if (g.mode == "level") {
        if (g.map == "第1关_草甸迎击") tick_level1(g);
        else if (g.map == "第2关_断龙桥") tick_level2(g);
        else if (g.map == "第3关_拔炮台") tick_level3(g);
        else if (g.map == "第4关_一线天") tick_level4(g);
        else if (g.map == "第5关_镇山兽") tick_level5(g);
    } else if (g.mode == "duel") {
        tick_duel(g);
    } else if (g.mode == "melee") {
        tick_melee(g);
    }
}
