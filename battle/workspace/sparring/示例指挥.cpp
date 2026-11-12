// 示例指挥程序：比默认行为强一点，演示接口怎么用。
// 编译：python build.py examples/示例指挥.cpp
//
// 做了五件事（对抗和闯关都适用）：
//   1. 长枪兵：附近 18 米内有敌方骑兵在冲，就原地守（架枪）；否则自由行动。
//   2. 弓手：优先点名射 26 米内的敌方炮仗兵，其次射离自己最近的敌兵；被贴身就往后退两步。
//   3. 骑兵："冲、撤、再冲"——撞上以后（速度掉下来、身边有敌人）往回跑 8 米，再找最近的弓手或投石车冲。
//   4. 炮仗兵：身边 2.5 米内敌人多于 2 个就立刻引爆。
//   5. 1v1 最后 20 秒：所有能动的兵往计分区走（防止时间到了计分区里没人）。
// 闯关里还演示了：射火药桶（打道具）、推雪球、读镇山兽的状态、指挥主帅。
#include "commander.hpp"

#include <map>

namespace {
std::map<int, int> retreat_until;  // 骑兵编号 → 撤到第几回合
}

void on_start(Game& g) {
    g.log("开局：模式 %s 地图 %s 座位 %d，我方 %d 个兵", g.mode.c_str(), g.map.c_str(), g.seat, int(g.mine().size()));
}

static void level_tricks(Game& g) {
    // 有火药桶的关：弓手见到 26 米内的桶、而且桶附近 4 米有 5 个以上敌人，就射它
    for (Unit* a : g.mine_of(弓手)) {
        for (auto& p : g.props) {
            if (p.type != "keg" || !p.alive || p.triggered) continue;
            if (Game::dist(a->x, a->z, p.x, p.z) > 26) continue;
            int crowd = 0;
            for (Unit* e : g.enemies()) if (Game::dist(e->x, e->z, p.x, p.z) < 4) ++crowd;
            if (crowd >= 5) { g.attack(a->id, p.id); break; }
        }
    }
    // 有雪球的关：标签是"推球"的兵去推最近的、还没松开的雪球
    for (Unit* u : g.by_tag("推球")) {
        Prop* best = nullptr;
        float bd = 1e9f;
        for (auto& p : g.props)
            if (p.type == "snowball" && p.alive && !p.triggered) {
                const float d = Game::dist(u->x, u->z, p.x, p.z);
                if (d < bd) { bd = d; best = &p; }
            }
        if (best && u->order != 推) g.push(u->id, best->id);
    }
    // 主帅：第 60 秒以后往前（我方视角 +z）走（只演示怎么给主帅下令）
    if (Unit* l = g.lord()) {
        if (g.time > 60 && l->order != 走到) g.move(l->id, l->x, l->z + 100);
    }
    // 镇山兽刨地时，被它锁定方向上的己方兵往旁边让开 4 米
    for (auto& b : g.bosses) {
        if (b.state != 刨地) continue;
        Unit* boss = g.unit(b.id);
        if (!boss) continue;
        for (Unit* u : g.mine()) {
            const float rx = u->x - boss->x, rz = u->z - boss->z;
            const float along = rx * b.dir_x + rz * b.dir_z, side = rx * b.dir_z - rz * b.dir_x;
            if (along > 0 && std::fabs(side) < 2.5f) g.move(u->id, u->x + (side >= 0 ? 4.0f : -4.0f) * b.dir_z, u->z - (side >= 0 ? 4.0f : -4.0f) * b.dir_x);
        }
    }
}

void on_tick(Game& g) {
    auto enemies = g.enemies();
    // 1. 长枪兵
    for (Unit* s : g.mine_of(长枪兵)) {
        bool cav_coming = false;
        for (Unit* e : enemies)
            if (e->kind == 骑兵 && Game::dist(*s, *e) < 18 && std::hypot(e->vx, e->vz) > 3) cav_coming = true;
        if (cav_coming && !s->holding) g.hold(s->id);
        else if (!cav_coming && s->holding && g.tick % 20 == 0) g.free(s->id);
    }
    // 2. 弓手
    for (Unit* a : g.mine_of(弓手)) {
        Unit* close = g.nearest_enemy(a->x, a->z, 3.0f);
        if (close) {  // 往远离敌人的方向退两步
            const float dx = a->x - close->x, dz = a->z - close->z, l = std::max(0.1f, std::hypot(dx, dz));
            if (g.walkable(a->x + dx / l * 2, a->z + dz / l * 2)) { g.move(a->id, a->x + dx / l * 2, a->z + dz / l * 2); continue; }
        }
        Unit* best = nullptr;
        float bd = 1e9f;
        for (Unit* e : enemies) {
            const float d = Game::dist(*a, *e);
            if (d > 26) continue;
            const float score = d - (e->kind == 炮仗兵 ? 100.0f : 0.0f);
            if (score < bd) { bd = score; best = e; }
        }
        if (best) { if (a->target != best->id || a->order != 打) g.attack(a->id, best->id); }
        else if (a->order != 自由) g.free(a->id);
    }
    // 3. 骑兵
    for (Unit* c : g.mine_of(骑兵)) {
        const float spd = std::hypot(c->vx, c->vz);
        auto it = retreat_until.find(c->id);
        if (it != retreat_until.end() && g.tick < it->second) continue;
        Unit* near = g.nearest_enemy(c->x, c->z, 2.5f);
        if (near && spd < 2.0f && c->state == 站着) {  // 撞上了：撤
            const float dx = c->x - near->x, dz = c->z - near->z, l = std::max(0.1f, std::hypot(dx, dz));
            float tx = c->x + dx / l * 8, tz = c->z + dz / l * 8;
            if (!g.walkable(tx, tz)) { tx = c->x; tz = c->z - 8; }
            g.move(c->id, tx, tz);
            retreat_until[c->id] = g.tick + 15;
            continue;
        }
        // 冲：优先弓手和投石车
        Unit* best = nullptr;
        float bd = 1e9f;
        for (Unit* e : enemies) {
            const float d = Game::dist(*c, *e) - ((e->kind == 弓手 || e->kind == 投石车) ? 15.0f : 0.0f);
            if (d < bd) { bd = d; best = e; }
        }
        if (best && (c->order != 打 || c->target != best->id)) g.attack(c->id, best->id);
    }
    // 4. 炮仗兵
    for (Unit* b : g.mine_of(炮仗兵)) {
        int n = 0;
        for (Unit* e : enemies) if (Game::dist(*b, *e) < 2.5f) ++n;
        if (n > 2 && !b->fuse) g.detonate(b->id);
    }
    // 5. 1v1 最后 20 秒去计分区
    if (g.mode == "duel" && g.time > g.time_limit - 20 && !g.score_zone.empty()) {
        for (Unit* u : g.mine()) {
            if (u->kind == 投石车 || g.in_score_zone(u->x, u->z)) continue;
            if (u->order == 走到) continue;
            // 计分区中心大致在地图正中：我方视角里是 (0, 28)
            g.move(u->id, 0, 28);
        }
    }
    if (g.mode == "level") level_tricks(g);
}
