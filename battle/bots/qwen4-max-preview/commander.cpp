#include "commander.hpp"
#include <map>
#include <set>
#include <string>

// ===================== 通用工具 =====================
namespace {

float hyp(float x, float z) { return std::sqrt(x * x + z * z); }

bool ranged(Kind k) { return k == 弓手 || k == 投石车; }

// 弓手统一点名：优先高价值远程 / 低血，全场弓手打同一个目标，快速减员
Unit* focus_target(Game& g, const std::vector<Unit*>& en) {
    Unit* best = nullptr; float bs = -1e30f;
    for (Unit* e : en) {
        if (!e->alive) continue;
        float s = 0;
        switch (e->kind) {
            case 弓手: s = 900; break;
            case 投石车: s = 850; break;
            case 炮仗兵: s = 800; break;
            case 镇山兽: s = 1000; break;
            case 大块头: s = 300; break;
            case 骑兵: s = 260; break;
            case 大锤: s = 240; break;
            case 长枪兵: s = 200; break;
            case 盾卫: s = 120; break;
            case 新兵: s = 100; break;
            default: s = 50; break;
        }
        s -= e->hp * 0.8f;                 // 残血优先补刀
        if (s > bs) { bs = s; best = e; }
    }
    return best;
}

void archers(Game& g, const std::vector<Unit*>& en, float maxr = 30.0f) {
    Unit* ft = focus_target(g, en);
    for (Unit* a : g.mine_of(弓手)) {
        if (a->state != 站着) continue;
        Unit* close = g.nearest_enemy(a->x, a->z, 2.6f);
        if (close) {                        // 被贴身：退开
            float dx = a->x - close->x, dz = a->z - close->z, l = std::max(0.2f, hyp(dx, dz));
            float tx = a->x + dx / l * 3.0f, tz = a->z + dz / l * 3.0f;
            if (g.walkable(tx, tz)) { g.move(a->id, tx, tz); continue; }
        }
        Unit* t = nullptr;
        if (ft && Game::dist(*a, *ft) <= maxr) t = ft;
        if (!t) {
            float bd = 1e30f;
            for (Unit* e : en) {
                float d = Game::dist(*a, *e);
                if (d > maxr) continue;
                float s = d + e->hp * 0.3f;
                if (s < bd) { bd = s; t = e; }
            }
        }
        if (t) { if (a->order != 打 || a->target != t->id) g.attack(a->id, t->id); }
        else if (a->order != 原地守) g.hold(a->id);
    }
}

void bombers(Game& g, const std::vector<Unit*>& en) {
    for (Unit* b : g.mine_of(炮仗兵)) {
        if (b->fuse) continue;
        int n = 0; float val = 0;
        for (Unit* e : en) if (Game::dist(*b, *e) < 3.0f) { ++n; val += g.cost_of(*e); }
        if (n >= 2 || val >= 120) g.detonate(b->id);
    }
}

void cavalry(Game& g, const std::vector<Unit*>& en, std::map<int,int>& ret) {
    for (Unit* c : g.mine_of(骑兵)) {
        if (c->state != 站着) continue;
        float spd = hyp(c->vx, c->vz);
        auto it = ret.find(c->id);
        if (it != ret.end() && g.tick < it->second) continue;
        Unit* near = g.nearest_enemy(c->x, c->z, 2.6f);
        if (near && spd < 2.5f) {
            float dx = c->x - near->x, dz = c->z - near->z, l = std::max(0.2f, hyp(dx, dz));
            float tx = c->x + dx / l * 9, tz = c->z + dz / l * 9;
            if (!g.walkable(tx, tz)) { tx = c->x; tz = c->z - 9; }
            if (g.walkable(tx, tz)) { g.move(c->id, tx, tz); ret[c->id] = g.tick + 14; continue; }
        }
        Unit* best = nullptr; float bd = 1e30f;
        for (Unit* e : en) {
            float d = Game::dist(*c, *e) - ((e->kind == 弓手 || e->kind == 投石车 || e->kind == 炮仗兵) ? 18.0f : 0.0f);
            if (d < bd) { bd = d; best = e; }
        }
        if (best && (c->order != 打 || c->target != best->id)) g.attack(c->id, best->id);
    }
}

void spears(Game& g, const std::vector<Unit*>& en) {
    for (Unit* s : g.mine_of(长枪兵)) {
        bool cav = false;
        for (Unit* e : en)
            if (e->kind == 骑兵 && Game::dist(*s, *e) < 16 && hyp(e->vx, e->vz) > 3.0f) cav = true;
        if (cav && !s->holding) g.hold(s->id);
    }
}

// ===================== 第 1 关 =====================
void L1(Game& g) {
    auto en = g.enemies();
    archers(g, en, 34.0f);
    bombers(g, en);
    // 近战：敌方近战快打光时放开追击弓手
    int melee_left = 0;
    for (Unit* e : en) if (!ranged(e->kind)) ++melee_left;
    for (Unit* u : g.mine()) {
        if (u->kind == 弓手 || u->kind == 投石车 || u->kind == 炮仗兵) continue;
        if (melee_left <= 2) { if (u->order != 自由) g.free(u->id); }
        else if (!u->holding && u->order != 原地守) g.hold(u->id);
    }
}

// ===================== 第 2 关 =====================
// 隘口死守：布阵姿态就是最优解，程序不干预（弓手一追目标就会走出隘口）。
void L2(Game& g) {
    for (Unit* u : g.mine()) if (!u->holding && u->order != 原地守) g.hold(u->id);
}

// ===================== 第 3 关 =====================
// 策略：主力守在 z<=10（敌方投石车射程 45，够不到），先用远程把守军引下来，
// 再派突击队从西侧碎石坡上去拆投石车。
void L3(Game& g) {
    auto en = g.enemies();
    archers(g, en, 34.0f);
    bombers(g, en);
    std::vector<Unit*> catapults, guards, others;
    for (Unit* e : en) {
        if (e->kind == 投石车) catapults.push_back(e);
        else if (e->z > 50) guards.push_back(e);
        else others.push_back(e);
    }

    // 突击队（tag=突击）：先拆投石车，再护卫，再其余
    for (Unit* u : g.by_tag("突击")) {
        if (u->state != 站着) continue;
        Unit* t = nullptr; float bd = 1e30f;
        auto pick = [&](std::vector<Unit*>& v) {
            for (Unit* e : v) { float d = Game::dist(*u, *e); if (d < bd) { bd = d; t = e; } }
        };
        if (!catapults.empty()) pick(catapults);
        else if (!guards.empty()) pick(guards);
        else pick(others);
        if (t && (u->order != 打 || u->target != t->id)) g.attack(u->id, t->id);
    }
    // 主力：留在原地打，敌人下来了就自由追击
    bool down = false;
    for (Unit* e : en) if (e->z < 30 && e->kind != 投石车) down = true;
    for (Unit* u : g.mine()) {
        if (u->tag == "突击" || u->tag == "西" || u->kind == 弓手 || u->kind == 投石车 || u->kind == 炮仗兵) continue;
        if (down) { if (u->order != 自由) g.free(u->id); }
        else if (!u->holding && u->order != 原地守) g.hold(u->id);
    }
    // 西路碎石坡（本地 x-33..-29）没人守，爬上去 y=4，弓手射程 28+1.5*4=34，
    // 够得着墙头弓手 —— 挨了够不着的箭，那一队才会冲下来。
    for (Unit* u : g.by_tag("西")) {
        if (u->state != 站着) continue;
        if (u->y < 3.5f) {
            if (u->order != 走到) g.move(u->id, -31.0f, 38.5f);
            continue;
        }
        Unit* t = nullptr; float bd = 1e30f;
        for (Unit* e : en) {
            float d = Game::dist(*u, *e);
            if (u->kind == 弓手 && (e->kind == 弓手 || e->kind == 投石车)) d -= 20.0f;
            if (d < bd) { bd = d; t = e; }
        }
        if (t) { if (u->order != 打 || u->target != t->id) g.attack(u->id, t->id); }
        else if (u->order != 自由) g.free(u->id);
    }
}

// ===================== 第 4 关 =====================
void cliff_logic(Game& g, Unit* u, const std::vector<Unit*>& en) {
    Unit* t = nullptr; float bd = 1e30f;
    for (Unit* e : en) {
        if (e->kind != 弓手) continue;
        float d = Game::dist(*u, *e);
        if (d < bd) { bd = d; t = e; }
    }
    if (!t) { for (Unit* e : en) { float d = Game::dist(*u, *e); if (d < bd) { bd = d; t = e; } } }
    if (t) { if (u->order != 打 || u->target != t->id) g.attack(u->id, t->id); }
    else if (u->order != 自由) g.free(u->id);
}

void L4(Game& g) {
    auto en = g.enemies();
    // 推雪球：tag=推球；球都松开了就去打崖顶弓手
    for (Unit* u : g.by_tag("推球")) {
        Prop* best = nullptr; float bd = 1e30f;
        for (auto& p : g.props)
            if (p.type == "snowball" && p.alive && !p.triggered) {
                float d = Game::dist(u->x, u->z, p.x, p.z);
                if (d < bd) { bd = d; best = &p; }
            }
        if (best) { if (u->order != 推 || u->target != best->id) g.push(u->id, best->id); }
        else cliff_logic(g, u, en);
    }
    for (Unit* u : g.by_tag("崖")) cliff_logic(g, u, en);
    // 谷口组：守住，保护主帅
    for (Unit* u : g.by_tag("谷")) {
        if (u->state != 站着) continue;
        bool cav = false;
        for (Unit* e : en) if (e->kind == 骑兵 && Game::dist(*u, *e) < 18 && hyp(e->vx, e->vz) > 3) cav = true;
        if (u->kind == 长枪兵 && cav) { if (!u->holding) g.hold(u->id); continue; }
        Unit* near = g.nearest_enemy(u->x, u->z, 14.0f);
        if (near) { if (u->order != 打 || u->target != near->id) g.attack(u->id, near->id); }
        else if (!u->holding) g.hold(u->id);
    }
    bombers(g, en);
    // 主帅：躲到谷口最东边角落
    if (Unit* l = g.lord()) {
        if (g.time < 1.0f) g.move(l->id, 3.5f, 0.5f);
    }
}

// ===================== 第 5 关 =====================
void L5(Game& g) {
    auto en = g.enemies();
    Unit* boss = nullptr;
    for (Unit* e : en) if (e->kind == 镇山兽) boss = e;
    if (!boss) { archers(g, en, 34.0f); return; }

    BossInfo bi; bool have = false;
    for (auto& b : g.bosses) if (b.id == boss->id) { bi = b; have = true; }

    // 预测冲撞方向：刨地时锁定离它最远的敌人
    float dx = 0, dz = 1;
    if (have && bi.state == 冲撞) { dx = bi.dir_x; dz = bi.dir_z; }
    else {
        Unit* far = nullptr; float bd = -1;
        for (Unit& uu : g.units) {
            Unit* u = &uu;
            if (!u->alive || u->team == g.team) continue;
            float d = Game::dist(*u, *boss);
            if (d > bd) { bd = d; far = u; }
        }
        if (far) { float l = std::max(0.01f, hyp(far->x - boss->x, far->z - boss->z));
                   dx = (far->x - boss->x) / l; dz = (far->z - boss->z) / l; }
    }
    bool danger = have && (bi.state == 刨地 || bi.state == 冲撞);
    bool stunned = have && bi.state == 晕;

    // 游骑是弓手的天敌：没清完之前全队守在南边，长枪兵架枪迎击
    bool cav_near = false;
    for (Unit* e : en) {
        if (e->kind != 骑兵) continue;
        for (Unit* u : g.mine()) if (Game::dist(*u, *e) < 30.0f) { cav_near = true; break; }
        if (cav_near) break;
    }

    for (Unit* u : g.mine()) {
        if (u->state != 站着) continue;
        if (u->kind == 弓手 || u->kind == 投石车 || u->kind == 炮仗兵) continue;
        if (u->tag == "饵") { if (!u->holding) g.hold(u->id); continue; }
        float rx = u->x - boss->x, rz = u->z - boss->z;
        float along = rx * dx + rz * dz, side = rx * dz - rz * dx;
        float r = hyp(rx, rz);
        if (danger && along > -2 && along < 45 && std::fabs(side) < 3.6f && r > 3.0f) {
            float sgn = (side >= 0 ? 1.0f : -1.0f);
            float tx = u->x + sgn * 6.0f * dz, tz = u->z - sgn * 6.0f * dx;
            if (g.walkable(tx, tz) && hyp(tx, tz - 25.0f) < 23.0f) { g.move(u->id, tx, tz); continue; }
        }
        if (stunned) { if (u->order != 打 || u->target != boss->id) g.attack(u->id, boss->id); continue; }
        if (cav_near) { if (!u->holding) g.hold(u->id); continue; }
        if (r > 5.0f) { if (!u->holding) g.hold(u->id); continue; }
        // 已经在跟前：绕到侧后方再打，正面伤害减半还挨顶撞
        float fx = std::sin(boss->facing), fz = std::cos(boss->facing);
        float fa = rx * fx + rz * fz, fs = rx * fz - rz * fx;
        bool front = fa > 0 && std::fabs(fs) < fa * 1.2f;
        if (front) {
            float sgn = (fs >= 0 ? 1.0f : -1.0f);
            float tx = boss->x + rx / std::max(0.01f, r) * 4.5f + sgn * 4.5f * fz;
            float tz = boss->z + rz / std::max(0.01f, r) * 4.5f - sgn * 4.5f * fx;
            if (g.walkable(tx, tz) && hyp(tx, tz - 25.0f) < 23.0f) { g.move(u->id, tx, tz); continue; }
        }
        if (u->order != 打 || u->target != boss->id) g.attack(u->id, boss->id);
    }

    for (Unit* a : g.mine_of(弓手)) {
        if (a->state != 站着) continue;
        float rx = a->x - boss->x, rz = a->z - boss->z;
        float along = rx * dx + rz * dz, side = rx * dz - rz * dx;
        if (danger && along > -2 && along < 45 && std::fabs(side) < 3.6f) {
            float sgn = (side >= 0 ? 1.0f : -1.0f);
            float tx = a->x + sgn * 6.0f * dz, tz = a->z - sgn * 6.0f * dx;
            if (g.walkable(tx, tz) && hyp(tx, tz - 25.0f) < 23.0f) { g.move(a->id, tx, tz); continue; }
        }
        if (stunned) { if (a->order != 打 || a->target != boss->id) g.attack(a->id, boss->id); continue; }
        if (cav_near) {
            // 站在枪阵后面点名游骑
            Unit* t = nullptr; float bd = 1e30f;
            for (Unit* e : en) if (e->kind == 骑兵) {
                float d = Game::dist(*a, *e); if (d < bd) { bd = d; t = e; }
            }
            if (t && bd < 32.0f) { if (a->order != 打 || a->target != t->id) g.attack(a->id, t->id); }
            else if (!a->holding) g.hold(a->id);
            continue;
        }
        if (a->order != 打 || a->target != boss->id) g.attack(a->id, boss->id);
    }
    bombers(g, en);
}

// ===================== 对抗 =====================
void pvp(Game& g) {
    auto en = g.enemies();
    archers(g, en, 34.0f);
    bombers(g, en);
    static std::map<int,int> ret;
    cavalry(g, en, ret);
    spears(g, en);

    bool melee_mode = (g.mode == "melee");
    float t = g.time;
    // 混战：潮水来了往中心高地上挤。沙洲高度 = 0.17143*(35-r)，礁石 r<2.5 过不去
    if (melee_mode) {
        float wl = g.water_at(std::min(90.0f, t + 12.0f));
        float safe_r = 35.0f - (wl - 0.3f) / 0.17143f;
        for (Unit* u : g.mine()) {
            if (u->kind == 投石车 || u->state != 站着) continue;
            float wx, wz; g.to_world(u->x, u->z, wx, wz);
            float r = hyp(wx, wz);
            float h = g.height(u->x, u->z);
            float want_r = (u->kind == 弓手) ? 12.0f : 10.0f;
            want_r = std::min(want_r, std::max(4.0f, safe_r - 1.5f));
            if (h < wl + 0.5f || r > want_r + 0.8f) {
                float cx, cz; g.to_local(0, 0, cx, cz);
                float ddx = cx - u->x, ddz = cz - u->z, dl = std::max(0.01f, hyp(ddx, ddz));
                float step = std::max(0.0f, dl - want_r);
                float tx = u->x + ddx / dl * step, tz = u->z + ddz / dl * step;
                if (g.walkable(tx, tz)) { g.move(u->id, tx, tz); continue; }
            }
            // 已经在安全圈里：只打够得着的，绝不追出去淹死
            float reach = (u->kind == 弓手) ? 30.0f : 5.0f;
            Unit* near = g.nearest_enemy(u->x, u->z, reach);
            if (near) { if (u->order != 打 || u->target != near->id) g.attack(u->id, near->id); }
            else if (!u->holding) g.hold(u->id);
        }
        return;
    }
    // 1v1：早点占住计分区。河心洲的钟楼在岛中心，锚点要避开它、走大桥
    float ax = 0.0f, az = 28.0f;
    if (g.map == "河心洲") { ax = 8.0f; az = 24.0f; }
    for (Unit* u : g.mine()) {
        if (u->kind == 投石车 || u->state != 站着) continue;
        bool inzone = g.in_score_zone(u->x, u->z);
        float delay = (u->kind == 大块头) ? 0.0f : (u->kind == 弓手 ? 10.0f : 5.0f);
        if (!inzone && t > delay) {
            float ox = float((u->id % 5) - 2) * 1.5f, oz = float((u->id / 5) % 3) * 1.5f;
            float tx = ax + ox, tz = az + oz;
            if (g.walkable(tx, tz)) { g.move(u->id, tx, tz); continue; }
            g.move(u->id, ax, az); continue;
        }
        if (!inzone) continue;
        if (u->kind == 弓手 || u->kind == 骑兵 || u->kind == 炮仗兵) continue;
        Unit* near = g.nearest_enemy(u->x, u->z, 5.0f);
        if (near) { if (u->order != 打 || u->target != near->id) g.attack(u->id, near->id); }
        else if (!u->holding) g.hold(u->id);
    }
}

}  // namespace

void on_start(Game& g) {}

void on_tick(Game& g) {
    if (g.mode == "level") {
        if (g.map.rfind("第1关", 0) == 0) L1(g);
        else if (g.map.rfind("第2关", 0) == 0) L2(g);
        else if (g.map.rfind("第3关", 0) == 0) L3(g);
        else if (g.map.rfind("第4关", 0) == 0) L4(g);
        else if (g.map.rfind("第5关", 0) == 0) L5(g);
    } else {
        pvp(g);
    }
}