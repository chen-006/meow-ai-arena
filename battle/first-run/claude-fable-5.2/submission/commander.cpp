// 指挥程序：按 g.mode / g.map 分支
#include "commander.hpp"

#include <map>
#include <set>

namespace {
bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
// tag 形如 "名字_参数"：取前缀
bool tag_is(const Unit* u, const char* p) { return starts(u->tag, p); }
// 解析 tag 中第 k 个下划线后的数字（没有返回 def）
float tag_num(const std::string& t, int k, float def) {
    size_t pos = 0;
    for (int i = 0; i <= k; ++i) {
        pos = t.find('_', pos);
        if (pos == std::string::npos) return def;
        ++pos;
    }
    return float(std::atof(t.c_str() + pos));
}
std::map<int, int> once;  // 通用的"只下一次令"标记
bool first(int id, int key) {
    int k = id * 64 + key;
    if (once.count(k)) return false;
    once[k] = 1;
    return true;
}
}  // namespace

// ------------------------------------------------------------ 第 1 关
static void L1(Game& g) {
    int melee = 0;
    for (Unit* e : g.enemies()) if (e->kind != 弓手) ++melee;
    static bool go = false;
    if (melee == 0) go = true;
    if (go)
        for (Unit* u : g.mine())
            if (first(u->id, 1)) g.free(u->id);
}

// ------------------------------------------------------------ 第 2 关
static void L2(Game& g) {
    static bool fired = false;
    static float bomb_gone_t = -1;
    auto en = g.enemies();
    int west = 0, mid = 0, east = 0, ebomb = 0;
    for (Unit* e : en) {
        if (e->z >= 34.7f) ++west;
        else if (e->z >= 25.0f) ++mid;
        else ++east;
        if (e->kind == 炮仗兵) ++ebomb;
    }
    if (ebomb == 0 && bomb_gone_t < 0) bomb_gone_t = g.time;
    bool broken = g.terrain_removed("桥中段");
    static bool ambush_boom = false;
    static float lure_x = 15, lure_z = 5, amb_x = 15, amb_z = 5;
    static bool lure_seen = false;
    bool lure_alive = false;
    {
        // 改道敌军围到诱饵身边的人数和质心
        int n = 0;
        float sx = 0, sz = 0;
        for (Unit* e : en) if (Game::dist(e->x, e->z, lure_x, lure_z) < 4.0f) { ++n; sx += e->x; sz += e->z; }
        if (n > 0) { amb_x = sx / n; amb_z = sz / n; }
        if (broken && n >= 1) lure_seen = true;
        int K = 5;
        for (Unit* u : g.mine()) if (tag_is(u, "诱")) K = int(tag_num(u->tag, 2, 5));
        int total = int(en.size());
        if (broken && lure_seen && (n >= K || n >= total)) ambush_boom = true;
    }
    for (Unit* u : g.mine()) {
        if (tag_is(u, "走")) {
            if (broken) { if (first(u->id, 4)) g.free(u->id); }
            else if (first(u->id, 1)) g.move(u->id, tag_num(u->tag, 0, 0), tag_num(u->tag, 1, 22.5f));
        }
        if (tag_is(u, "饵") && first(u->id, 1)) g.move(u->id, tag_num(u->tag, 0, 0), tag_num(u->tag, 1, 23.0f));
        if (tag_is(u, "塞")) {
            if (broken) { if (first(u->id, 4)) g.free(u->id); }
            else if (bomb_gone_t >= 0 && g.time >= bomb_gone_t + 0.2f) { if (first(u->id, 2)) g.move(u->id, tag_num(u->tag, 0, 0), tag_num(u->tag, 1, 24.3f)); }
        }
        if (tag_is(u, "炸")) {  // 我方炮仗兵：敌方炮仗兵没了以后放出去
            if (bomb_gone_t >= 0 && g.time >= bomb_gone_t + tag_num(u->tag, 0, 0.5f)) {
                if (first(u->id, 5)) g.free(u->id);
                int n = 0;
                for (Unit* e : en) if (Game::dist(*u, *e) < 1.6f) ++n;
                if (n >= int(tag_num(u->tag, 1, 3)) && !u->fuse) g.detonate(u->id);
            } else if (first(u->id, 6)) g.hold(u->id);
        }
        if (tag_is(u, "诱")) {  // 挡路的诱饵：桥断后到指定位置守着，让改道的敌军围上来
            if (broken) {
                float tx = tag_num(u->tag, 0, 15), tz = tag_num(u->tag, 1, 5);
                if (Game::dist(u->x, u->z, tx, tz) > 0.6f) { if (u->order != 走到 || first(u->id, 8)) g.move(u->id, tx, tz); }
                lure_x = u->x; lure_z = u->z; lure_alive = true;
            }
        }
        if (tag_is(u, "伏")) {  // 伏击的炮仗兵：桥断后到指定位置等，敌军围住诱饵以后冲进去炸
            if (broken) {
                if (!ambush_boom) {
                    float tx = tag_num(u->tag, 0, 12), tz = tag_num(u->tag, 1, 5);
                    if (Game::dist(u->x, u->z, tx, tz) > 0.6f) { if (u->order != 走到 || first(u->id, 8)) g.move(u->id, tx, tz); }
                    if (u->hp < 30 || u->fuse) ambush_boom = true;
                } else {
                    float d = Game::dist(u->x, u->z, amb_x, amb_z);
                    if (d < 1.3f || u->hp < 30) { if (!u->fuse) g.detonate(u->id); }
                    else g.move_straight(u->id, amb_x, amb_z);
                }
            }
        }
        if (tag_is(u, "备")) {  // 后备：桥断或敌方炮仗兵没了以后自由行动
            if ((broken || bomb_gone_t >= 0) && first(u->id, 7)) g.free(u->id);
        }
        if (tag_is(u, "射桶")) {
            float K = tag_num(u->tag, 0, 0), T = tag_num(u->tag, 1, 40);
            if (!broken) {
                if (!fired && g.time > 5 && (west <= int(K) || g.time >= T)) { fired = true; g.log("fire t=%.1f west=%d mid=%d east=%d", g.time, west, mid, east); }
                if (fired) {
                    Prop* best = nullptr;
                    float bd = 1e9f;
                    for (auto& p : g.props)
                        if (p.type == "keg" && p.alive) {
                            float d = Game::dist(u->x, u->z, p.x, p.z);
                            if (d < bd) { bd = d; best = &p; }
                        }
                    if (best && (u->order != 打 || u->target != best->id)) g.attack(u->id, best->id);
                } else if (first(u->id, 2)) g.aim(u->id, u->x - 6, u->z - 6);
            } else if (first(u->id, 3)) g.free(u->id);
        }
    }
    if (broken && lure_seen && !lure_alive) ambush_boom = true;
    static bool logged = false;
    if (broken && !logged) { logged = true; g.log("broken t=%.1f west=%d mid=%d east=%d", g.time, west, mid, east); }
}

// ------------------------------------------------------------ 躲石头（通用）
static bool dodge(Game& g, Unit* u, float half = 3.0f, float out = 5.0f) {
    for (auto& f : g.flying) {
        float sp = std::hypot(f.vx, f.vz);
        float ux = sp > 0.1f ? f.vx / sp : 0, uz = sp > 0.1f ? f.vz / sp : 1;
        float dx = u->x - f.land_x, dz = u->z - f.land_z;
        float along = dx * ux + dz * uz, side = dx * uz - dz * ux;
        if (std::fabs(side) < half && along > -4.0f && along < 14.0f) {
            float sgn = side >= 0 ? 1.0f : -1.0f;
            float k = sgn * out - side;
            g.move(u->id, u->x + k * uz, u->z - k * ux);
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------ 第 3 关
static float seg_dist(float ax, float az, float bx, float bz, float px, float pz) {
    float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
    float t = l2 > 1e-6f ? ((px - ax) * dx + (pz - az) * dz) / l2 : 0;
    t = std::max(0.0f, std::min(1.0f, t));
    return std::hypot(px - (ax + t * dx), pz - (az + t * dz));
}
static void L3(Game& g) {
    auto en = g.enemies();
    static std::map<int, std::pair<float, float>> home;
    for (Unit* e : en) if (!home.count(e->id)) home[e->id] = {e->x, e->z};
    auto is_top = [&](Unit* e) { return e->kind == 盾卫 || (e->kind == 长枪兵 && std::fabs(home[e->id].first) < 6 && home[e->id].second < 46); };
    int n_archW = 0, n_archE = 0, n_top = 0, n_loose = 0;
    for (Unit* e : en) {
        if (e->kind == 弓手) { if (home[e->id].first < 0) ++n_archW; else ++n_archE; }
        if (is_top(e)) ++n_top;
        if (e->kind != 投石车 && !e->holding) ++n_loose;
    }
    static int S = 0;
    static std::map<int, int> dg;
    const float CX = 0, CZ = 11, R = 11;
    auto mine = g.mine();
    std::vector<Unit*> arch, shd, bait;
    for (Unit* u : mine) {
        if (tag_is(u, "弓")) arch.push_back(u);
        if (tag_is(u, "盾")) shd.push_back(u);
        if (tag_is(u, "饵")) bait.push_back(u);
    }
    auto nearest_to = [&](float x, float z, std::function<bool(Unit*)> ok) {
        Unit* b = nullptr;
        float d0 = 1e9f;
        for (Unit* e : en)
            if (ok(e)) {
                float d = Game::dist(x, z, e->x, e->z);
                if (d < d0) { d0 = d; b = e; }
            }
        return b;
    };
    if (S == 0) {
        // 两组各清本侧墙头弓手；清完到集合点
        bool all_in = true;
        for (int gi = 0; gi < 2; ++gi) {
            std::vector<Unit*> ga, gs;
            for (Unit* u : arch) if ((u->tag.find("W") != std::string::npos) == (gi == 0)) ga.push_back(u);
            for (Unit* u : shd) if ((u->tag.find("W") != std::string::npos) == (gi == 0)) gs.push_back(u);
            float cx = 0, cz = 0;
            for (Unit* u : ga) { cx += u->x; cz += u->z; }
            if (!ga.empty()) { cx /= ga.size(); cz /= ga.size(); }
            Unit* tgt = nearest_to(cx, cz, [&](Unit* e) { return e->kind == 弓手 && ((home[e->id].first < 0) == (gi == 0)); });
            int ki = 0;
            for (Unit* u : ga) {
                if (dodge(g, u)) { dg[u->id] = 1; all_in = false; continue; }
                dg[u->id] = 0;
                if (g.time < 1.5f) { all_in = false; continue; }
                if (tgt) { all_in = false; if (u->order != 打 || u->target != tgt->id) g.attack(u->id, tgt->id); }
                else {
                    float tx = CX + (gi == 0 ? -1.0f : 1.0f) * (1.0f + (ki % 2) * 1.0f), tz = 9.0f - (ki / 2) * 1.0f;
                    ++ki;
                    if (Game::dist(u->x, u->z, tx, tz) > 0.8f) { all_in = false; g.move(u->id, tx, tz); }
                }
            }
            for (Unit* u : gs) {
                if (dodge(g, u)) { dg[u->id] = 1; continue; }
                dg[u->id] = 0;
                if (tgt) {
                    float dx = tgt->x - cx, dz = tgt->z - cz, l = std::max(0.1f, std::hypot(dx, dz));
                    float lead = std::min(l > 26 ? l - 21.0f : 4.0f, l - 3.0f);
                    float tx = cx + dx / l * lead, tz = cz + dz / l * lead;
                    if (Game::dist(u->x, u->z, tx, tz) > 1.0f) g.move(u->id, tx, tz);
                } else {
                    float tx = CX + (gi == 0 ? -3.5f : 3.5f), tz = 9.0f;
                    if (Game::dist(u->x, u->z, tx, tz) > 0.8f) g.move(u->id, tx, tz);
                }
            }
        }
        if (n_archW + n_archE <= 4) {
            int bi = 0;
            for (Unit* u : bait) {
                if (dodge(g, u)) continue;
                float tx = CX + (bi++ ? 1.0f : -1.0f), tz = 13.0f;
                if (Game::dist(u->x, u->z, tx, tz) > 0.8f) g.move(u->id, tx, tz);
            }
        }
        (void)all_in;
        if (n_archW + n_archE == 0) { S = 1; g.log("S1 t=%.1f", g.time); }
        return;
    }
    std::vector<Unit*> bomb;
    for (Unit* u : mine) if (tag_is(u, "炸")) bomb.push_back(u);
    std::vector<Unit*> line;
    for (Unit* u : mine) if (tag_is(u, "线")) line.push_back(u);
    bool use_line = !line.empty();
    auto apos = [&](int ki, float& tx, float& tz) {
        float sd = ki < 4 ? -1.0f : 1.0f;
        int j = ki % 4;
        if (use_line) { tx = sd * (10.0f + (j % 2) * 1.2f); tz = 13.5f - (j / 2) * 1.2f; }
        else { tx = sd * (6.0f + (j % 2) * 1.2f); tz = 5.0f - (j / 2) * 1.2f; }
    };
    if (S == 1) {
        // 全体退到后面站好，炮仗兵在前面一排，然后引子（一个弓手）去射坡顶
        if (n_loose > 0 || n_top == 0) { S = 2; g.log("S2 t=%.1f", g.time); }
        else {
            static float s1_t = -1;
            if (s1_t < 0) s1_t = g.time;
            bool ready = true;
            int bi = 0;
            for (Unit* u : bomb) {
                float tx = (bi % 2) * 1.0f - 0.5f, tz = 13.0f - (bi / 2) * 1.0f;
                ++bi;
                if (Game::dist(u->x, u->z, tx, tz) > 0.6f) { ready = false; if (u->order != 走到 || first(u->id, 21)) g.move(u->id, tx, tz); }
            }
            int kk = 0;
            for (Unit* u : arch) {
                float tx, tz;
                apos(kk++, tx, tz);
                if (u->tag.find("W") != std::string::npos && u->z > 12.5f) ready = false;
            }
            for (Unit* u : line) {
                float tx = tag_num(u->tag, 0, 0), tz = tag_num(u->tag, 1, 12.5f);
                if (Game::dist(u->x, u->z, tx, tz) > 0.7f) { ready = false; if (u->order != 走到 || first(u->id, 22)) g.move(u->id, tx, tz); }
            }
            if (use_line) {
                int si = 0;
                for (Unit* u : shd) {
                    float tx = (si++ ? 1.0f : -1.0f), tz = 13.5f;
                    if (Game::dist(u->x, u->z, tx, tz) > 0.7f) ready = false;
                }
                if (g.time > s1_t + 16.0f) ready = true;
            } else if (g.time > s1_t + 3.0f) ready = true;
            Unit* lead = nullptr;
            for (Unit* u : arch) if (u->tag.find("W") != std::string::npos) { lead = u; break; }
            if (!lead && !arch.empty()) lead = arch[0];
            int ki = 0;
            for (Unit* u : arch) {
                float tx, tz;
                apos(ki++, tx, tz);
                if (u == lead) {
                    if (ready) {
                        Unit* t = nearest_to(u->x, u->z, [&](Unit* e) { return is_top(e); });
                        if (t && (u->order != 打 || u->target != t->id)) g.attack(u->id, t->id);
                    } else if (use_line) { if (Game::dist(u->x, u->z, 0.0f, 10.5f) > 0.8f) g.move(u->id, 0.0f, 10.5f); }
                    else if (Game::dist(u->x, u->z, -6.0f, 11.0f) > 0.8f) g.move(u->id, -6.0f, 11.0f);
                    continue;
                }
                if (Game::dist(u->x, u->z, tx, tz) > 0.8f) g.move(u->id, tx, tz);
            }
            ki = 0;
            for (Unit* u : shd) {
                float tx = (ki++ ? 3.5f : -3.5f), tz = 7.0f;
                if (use_line) { tx = tx > 0 ? 1.0f : -1.0f; tz = 13.5f; }
                if (Game::dist(u->x, u->z, tx, tz) > 0.7f) g.move(u->id, tx, tz);
            }
            return;
        }
    }
    if (S == 2) {
        std::vector<Unit*> loose;
        for (Unit* e : en) if (e->kind != 投石车 && !e->holding) loose.push_back(e);
        static bool engaged = false;
        if (loose.size() >= 8) engaged = true;
        int body_alive = int(line.size() + shd.size());
        if ((loose.empty() && n_top == 0) || (use_line && engaged && int(loose.size()) <= int(tag_num(line[0]->tag, 2, 4)))) { S = 3; g.log("S3 t=%.1f loose=%d", g.time, int(loose.size())); }
        else {
            bool boom = false;
            {
                float bx = 0, bz = 0;
                for (Unit* u : bomb) { bx += u->x; bz += u->z; if (u->hp < 18 || u->fuse) { if (!boom) g.log("boom hp=%.0f fuse=%d", u->hp, int(u->fuse)); boom = true; } }
                if (!bomb.empty()) { bx /= bomb.size(); bz /= bomb.size(); }
                int cnt = 0;
                for (Unit* e : loose) { float d = Game::dist(bx, bz, e->x, e->z); if (d < 3.0f) ++cnt; }
                if (cnt >= int(tag_num(bomb.empty() ? std::string("") : bomb[0]->tag, 0, 6))) { if (!boom && !bomb.empty()) g.log("boom cnt=%d", cnt); boom = true; }
            }
            for (Unit* u : bomb) {
                if (boom && !u->fuse) g.detonate(u->id);
                else if (!boom && u->order != 原地守 && u->order != 走到) g.hold(u->id);
            }
            Unit* tgt = nearest_to(0, 4, [&](Unit* e) { return e->kind == 长枪兵 && !e->holding; });
            if (!tgt) tgt = nearest_to(0, 4, [&](Unit* e) { return e->kind != 投石车 && !e->holding; });
            int ki = 0;
            for (Unit* u : arch) {
                float tx, tz;
                apos(ki++, tx, tz);
                if (dodge(g, u, 2.2f, 3.5f)) continue;
                Unit* thr = nullptr;
                float td = 5.5f;
                for (Unit* e : loose) { float d = Game::dist(*u, *e); if (d < td) { td = d; thr = e; } }
                if (thr) {  // 被近身：往远处跑
                    float dx = u->x - thr->x, dz = u->z - thr->z, l = std::max(0.1f, std::hypot(dx, dz));
                    float nx = u->x + dx / l * 5, nz = u->z + dz / l * 5;
                    if (nx < -33 || nx > 33 || nz < -1 || nz > 28) {  // 快到边了：沿切向往场地中间绕
                        float px = -dz / l, pz = dx / l;
                        if (px * (0 - u->x) + pz * (13 - u->z) < 0) { px = -px; pz = -pz; }
                        nx = u->x + px * 5 + dx / l * 1.5f; nz = u->z + pz * 5 + dz / l * 1.5f;
                        nx = std::max(-35.0f, std::min(35.0f, nx)); nz = std::max(-2.0f, std::min(29.0f, nz));
                    }
                    g.move(u->id, nx, nz);
                    continue;
                }
                if (bomb.size() > 0 && !boom && Game::dist(u->x, u->z, tx, tz) > 6.0f) { g.move(u->id, tx, tz); continue; }
                if (use_line) {
                    if (body_alive >= 3 && Game::dist(u->x, u->z, tx, tz) > 4.0f) { g.move(u->id, tx, tz); continue; }
                    Unit* mt = nullptr;
                    float md = 1e9f;
                    for (Unit* e : loose) { float d = Game::dist(*u, *e) - (e->kind == 长枪兵 ? 4.0f : 0.0f); if (d < md) { md = d; mt = e; } }
                    if (mt) { if (u->order != 打 || u->target != mt->id) g.attack(u->id, mt->id); }
                    continue;
                }
                if (tgt) { if (u->order != 打 || u->target != tgt->id) g.attack(u->id, tgt->id); }
            }
            for (Unit* u : shd) {
                Unit* t = nearest_to(u->x, u->z, [&](Unit* e) { return e->kind != 投石车 && !e->holding && e->z < 11.5f; });
                if (t && bomb.empty() && !use_line) { if (u->order != 打 || u->target != t->id) g.attack(u->id, t->id); }
            }
            if (use_line) {
                // 防线：追兵只剩几个的时候放出去追
                bool chase = int(loose.size()) <= 3;
                std::vector<Unit*> body = line;
                for (Unit* u : shd) body.push_back(u);
                for (Unit* u : body) {
                    if (chase) { if (u->order != 自由) g.free(u->id); }
                    else if (u->order == 自由) g.hold(u->id);
                }
            }
            return;
        }
    }
    // S3：分东西两组上台地清剩下的
    {
        std::vector<Unit*> loose;
        for (Unit* e : en) if (e->kind != 投石车 && !e->holding) loose.push_back(e);
        for (int gi = 0; gi < 2; ++gi) {
            std::vector<Unit*> ga;
            for (Unit* u : mine) {
                if (u->kind != 弓手) continue;
                if ((u->tag.find("W") != std::string::npos) == (gi == 0)) ga.push_back(u);
            }
            if (ga.empty()) continue;
            float cx = 0, cz = 0;
            for (Unit* u : ga) { cx += u->x; cz += u->z; }
            cx /= ga.size(); cz /= ga.size();
            auto mineside = [&](Unit* e) { (void)gi; return !(home[e->id].first > 25.0f && e->kind == 长枪兵); };
            Unit* tgt = nearest_to(cx, cz, [&](Unit* e) { return e->kind != 投石车 && !e->holding && Game::dist(cx, cz, e->x, e->z) < 30; });
            if (!tgt) tgt = nearest_to(cx, cz, [&](Unit* e) { return e->kind != 投石车 && mineside(e); });
            if (!tgt) tgt = nearest_to(cx, cz, [&](Unit* e) { return mineside(e); });
            if (!tgt) tgt = nearest_to(cx, cz, [&](Unit* e) { return e->kind != 投石车; });
            if (!tgt) tgt = nearest_to(cx, cz, [&](Unit* e) { return true; });
            for (Unit* u : ga) {
                if (dodge(g, u)) continue;
                Unit* thr = nullptr;
                float td = 5.5f;
                for (Unit* e : loose) { float d = Game::dist(*u, *e); if (d < td) { td = d; thr = e; } }
                if (thr) {
                    float dx = u->x - thr->x, dz = u->z - thr->z, l = std::max(0.1f, std::hypot(dx, dz));
                    float nx = u->x + dx / l * 5, nz = u->z + dz / l * 5;
                    if (!g.walkable(nx, nz) || std::fabs(g.height(nx, nz) - u->y) > 1.5f) { nx = u->x - dz / l * 5; nz = u->z + dx / l * 5; }
                    g.move(u->id, nx, nz);
                    continue;
                }
                {   // 先全体上台地，到三架投石车中间偏南的位置集合（从那里够得着所有护卫和投石车）
                    static std::set<int> arrived;
                    if (!arrived.count(u->id)) {
                        float px = float(u->id % 4) * 1.1f - 1.6f, pz = 46.0f + float((u->id / 4) % 2) * 1.2f;
                        if (Game::dist(u->x, u->z, px, pz) > 3.0f && g.time < 138.0f) { if (u->order != 走到 || g.tick % 10 == 0) g.move(u->id, px, pz); continue; }
                        arrived.insert(u->id);
                    }
                }
                if (tgt) { if (u->order != 打 || u->target != tgt->id) g.attack(u->id, tgt->id); }
            }
        }
        for (Unit* u : mine) {
            if (u->kind == 弓手) continue;
            if (dodge(g, u)) continue;
            Unit* t = nearest_to(u->x, u->z, [&](Unit* e) { return e->kind == 投石车; });
            { Unit* t2 = nearest_to(u->x, u->z, [&](Unit* e) { return e->kind != 投石车 && !e->holding && Game::dist(u->x, u->z, e->x, e->z) < 30; }); if (t2) t = t2; }
            if (t && (u->order != 打 || u->target != t->id)) g.attack(u->id, t->id);
        }
    }
}

static void L4(Game& g) {
    auto en = g.enemies();
    auto mine = g.mine();
    Unit* lord = g.lord();
    if (lord && first(lord->id, 1)) g.move(lord->id, 0, 0.5f);
    bool barA = false;
    for (auto& p : g.props) if (p.type == "barrier" && p.tag == "路障甲" && p.alive) barA = true;
    static std::map<int, std::pair<float, float>> home;
    for (Unit* e : en) if (!home.count(e->id)) home[e->id] = {e->x, e->z};
    auto grp = [&](Unit* e) {  // 0 前哨 1 路障甲 2 路障乙 3 崖顶弓手
        float hx = home[e->id].first, hz = home[e->id].second;
        if (e->kind == 弓手 && std::fabs(hx) > 5.0f) return 3;
        if (hz < 40) return 0;
        if (hz < 55) return 1;
        return 2;
    };
    int cnt[4] = {0, 0, 0, 0};
    for (Unit* e : en) cnt[grp(e)]++;
    static bool phaseD = false;
    float TD = 100;
    for (Unit* u : mine) if (tag_is(u, "弓")) TD = tag_num(u->tag, 0, 100);
    if (!phaseD && cnt[3] == 0 && (cnt[2] == 0 || g.time > TD)) { phaseD = true; g.log("phaseD t=%.1f", g.time); }
    // 会冲到谷口的敌人
    std::vector<Unit*> threat;
    for (Unit* e : en) {
        int gr = grp(e);
        if (gr == 3) continue;
        if (e->holding) continue;
        if (e->z < 49.5f || !barA) threat.push_back(e);
    }
    auto pick = [&](std::function<float(Unit*)> score) {
        Unit* b = nullptr;
        float bs = 1e9f;
        for (Unit* e : en) { float s = score(e); if (s < bs) { bs = s; b = e; } }
        return b;
    };
    // 南北两组弓手各打各的（不跨谷射崖顶弓手，免得流矢落进谷里）
    Unit* tgts[2] = {nullptr, nullptr};
    float gcx[2] = {0, 0}, gcz[2] = {0, 0};
    int gn[2] = {0, 0};
    for (Unit* u : mine) if (tag_is(u, "弓")) { int gi = u->x > 0 ? 0 : 1; gcx[gi] += u->x; gcz[gi] += u->z; gn[gi]++; }
    for (int gi = 0; gi < 2; ++gi) {
        if (!gn[gi]) continue;
        gcx[gi] /= gn[gi]; gcz[gi] /= gn[gi];
        float cx = gcx[gi], cz = gcz[gi];
        Unit* tgt = nullptr;
        if (!threat.empty()) {
            tgt = pick([&](Unit* e) {
                bool th = false;
                for (Unit* t : threat) if (t == e) th = true;
                if (!th) return 1e9f;
                float d = Game::dist(cx, cz, e->x, e->z);
                if (d > 34) return 1e9f;
                return e->z + (e->kind == 炮仗兵 ? -30.0f : 0.0f) + (e->kind == 盾卫 ? 6.0f : 0.0f);
            });
        }
        if (!tgt && cnt[3] > 0) tgt = pick([&](Unit* e) { return (grp(e) == 3 && ((e->x > 0) == (gi == 0))) ? Game::dist(cx, cz, e->x, e->z) : 1e9f; });
        if (!tgt && cnt[3] == 0 && cnt[2] > 0 && !phaseD)
            tgt = pick([&](Unit* e) {
                if (grp(e) != 2) return 1e9f;
                float pri = e->kind == 炮仗兵 ? -40.0f : e->kind == 长枪兵 ? -10.0f : e->kind == 大锤 ? -5.0f : 0.0f;
                if ((e->x > 0) == (gi == 0)) pri += 6.0f;  // 贴着本侧崖壁的不好射，留给对面
                return Game::dist(cx, cz, e->x, e->z) + pri;
            });
        if (!tgt && phaseD) tgt = pick([&](Unit* e) { return grp(e) == 3 ? 1e9f : e->z + (e->kind == 盾卫 ? 5.0f : 0.0f); });
        tgts[gi] = tgt;
    }
    // 雪球
    auto ball = [&](const char* tag) -> Prop* {
        for (auto& p : g.props) if (p.type == "snowball" && p.tag == tag && p.alive && !p.triggered) return &p;
        return nullptr;
    };
    float tmin = 1e9f;
    for (Unit* t : threat) tmin = std::min(tmin, t->z);
    bool near_go = phaseD && tmin < 36.0f;
    int ki = 0;
    int aslot[2] = {0, 0};
    for (Unit* u : mine) {
        if (u->kind == 主帅) continue;
        Unit* tgt = tgts[u->x > 0 ? 0 : 1];
        float cx = gcx[u->x > 0 ? 0 : 1], cz = gcz[u->x > 0 ? 0 : 1];
        if (tag_is(u, "弓")) {
            int slot = aslot[u->x > 0 ? 0 : 1]++;
            if (cnt[3] == 0 && threat.empty() && tgt && (grp(tgt) == 2 || phaseD)) {
                // 到崖边的射击位再射谷底
                float base = phaseD ? 44.5f : 66.0f;
                float tx = (u->x > 0 ? 5.7f : -5.7f), tz = base + slot * 1.5f;
                if (Game::dist(u->x, u->z, tx, tz) > 1.0f) { if (u->order != 走到 || first(u->id * 8 + (phaseD ? 1 : 0), 31)) g.move(u->id, tx, tz); continue; }
                if (!u->holding) g.hold(u->id);
                continue;
            }
            if (tgt) { if (u->order != 打 || u->target != tgt->id) g.attack(u->id, tgt->id); }
            else if (u->order == 打 || u->order == 自由) g.move(u->id, u->x, u->z);
            continue;
        }
        bool north = u->x > 0;
        if (tag_is(u, "盾") || tag_is(u, "推")) {
            if (phaseD) {
                Prop* far = ball(north ? "北远" : "南远");
                Prop* nr = ball(north ? "北近" : "南近");
                bool second = tag_num(u->tag, 0, 0) > 0;  // 第二个人直接负责近的那颗
                Prop* want = nullptr;
                if (second) { if (near_go) want = nr; }
                else { want = far; if (!want && near_go) want = nr; }
                if (want) { if (u->order != 推 || u->target != want->id) g.push(u->id, want->id); continue; }
                if (u->order == 推) g.hold(u->id);
                continue;
            }
            if (tag_is(u, "盾") && tgt && gn[u->x > 0 ? 0 : 1] > 0) {
                float dx = tgt->x - cx, dz = tgt->z - cz, l = std::max(0.1f, std::hypot(dx, dz));
                float lead = std::min(l > 26 ? l - 21.0f : 4.0f, l - 3.0f);
                float tx = cx + (north ? 3.0f : -3.0f), tz = cz + dz / l * lead;
                (void)ki; (void)dx;
                if (grp(tgt) == 3 && Game::dist(u->x, u->z, tx, tz) > 1.0f) g.move(u->id, tx, tz);
            }
            continue;
        }
        // 谷口组：原地守（长枪兵架枪）；贴身的敌人照打
        if (tag_is(u, "口")) {
            if (en.size() <= size_t(tag_num(u->tag, 0, 0))) { if (u->order != 自由) g.free(u->id); }
            else if (!u->holding && u->order != 原地守) g.hold(u->id);
        }
    }
}

// ------------------------------------------------------------ 第 5 关
// ------------------------------------------------------------ 第 5 关
static void L5(Game& g) {
    if (g.bosses.empty()) return;
    BossInfo& bi = g.bosses[0];
    Unit* boss = g.unit(bi.id);
    if (!boss || !boss->alive) return;
    auto en = g.enemies();
    auto mine = g.mine();
    int n_cav = 0, n_arch = 0;
    for (Unit* e : en) { if (e->kind == 骑兵) ++n_cav; if (e->kind == 弓手) ++n_arch; }
    // 冲撞方向：刨地时预估（它 → 离它最远的我方单位），冲撞时用锁定方向
    float dx = bi.dir_x, dz = bi.dir_z;
    Unit* far = nullptr;
    float fd = -1;
    for (Unit* u : mine) { float d = Game::dist(*u, *boss); if (d > fd) { fd = d; far = u; } }
    if (bi.state != 冲撞 && far) {
        float l = std::max(0.1f, fd);
        dx = (far->x - boss->x) / l; dz = (far->z - boss->z) / l;
    }
    bool danger = (bi.state == 刨地 || bi.state == 冲撞 || (bi.state == 游走 && bi.next_charge < 0.8f));
    static std::map<int, int> dodging;
    for (Unit* u : mine) {
        if (tag_is(u, "口") && n_cav > int(tag_num(u->tag, 1, 0))) {  // 防线：游骑没死光之前原地守（长枪兵架枪），不躲
            if (!u->holding && u->order != 原地守) g.hold(u->id);
            continue;
        }
        float rx = u->x - boss->x, rz = u->z - boss->z;
        float along = rx * dx + rz * dz, side = rx * dz - rz * dx;
        float db = std::hypot(rx, rz);
        // 1. 躲冲撞线
        if (danger && along > -1.0f && std::fabs(side) < 3.4f) {
            bool is_far = (u == far);
            if (!(is_far && bi.state != 冲撞)) {
                float sgn = side >= 0 ? 1.0f : -1.0f;
                float k = sgn * 5.0f - side;
                g.move_straight(u->id, u->x + k * dz, u->z - k * dx);
                dodging[u->id] = 1;
                continue;
            }
        }
        dodging[u->id] = 0;
        // 2. 各兵种
        if (u->kind == 弓手) {
            Unit* t = nullptr;
            float bd = 1e9f;
            for (Unit* e : en) if (e->kind == 骑兵 && !e->holding) { float d = Game::dist(*u, *e); if (d < 22 && d < bd) { bd = d; t = e; } }
            if (t) {  // 集火同一个骑兵：选离我方弓手群最近的
                static int focus = -1;
                Unit* f = g.unit(focus);
                if (!f || !f->alive || Game::dist(*u, *f) > 24) { focus = t->id; f = t; }
                t = f;
            }
            if (!t) for (Unit* e : en) if (e->kind == 弓手) { float d = Game::dist(*u, *e); if (d < bd) { bd = d; t = e; } }
            if (!t) t = boss;
            if (t == boss && bi.state == 游走 && db < 7.0f) {  // 离兽太近：退
                float l = std::max(0.1f, db);
                g.move(u->id, u->x + rx / l * 4, u->z + rz / l * 4);
                continue;
            }
            if (u->order != 打 || u->target != t->id) g.attack(u->id, t->id);
            continue;
        }
        if (u->kind == 长枪兵 && n_cav > 0) {
            bool cav = false;
            for (Unit* e : en) if (e->kind == 骑兵 && Game::dist(*u, *e) < 18) cav = true;
            if (cav) {
                Unit* c = g.nearest_enemy(u->x, u->z, 3.0f);
                if (c && c->kind == 骑兵 && std::hypot(c->vx, c->vz) < 2.0f) { if (u->order != 打 || u->target != c->id) g.attack(u->id, c->id); }
                else if (!u->holding) g.hold(u->id);
                continue;
            }
        }
        if (n_cav > 0 && tag_is(u, "卫")) {  // 护卫组：先打游骑
            Unit* c = nullptr;
            float bd = 1e9f;
            for (Unit* e : en) if (e->kind == 骑兵) { float d = Game::dist(*u, *e); if (d < bd) { bd = d; c = e; } }
            if (c && bd < 12) { if (u->order != 打 || u->target != c->id) g.attack(u->id, c->id); continue; }
            if (c) { if (u->order != 原地守) g.hold(u->id); continue; }
        }
        // 近战：晕的时候贴上去打；游走时在正面的退开
        bool stun = (bi.state == 晕 && bi.state_left > 0.35f);
        float keep = tag_num(u->tag, 0, 0);  // tag 里第一个数字：游走时保持的距离（0 = 一直贴着打）
        if (!stun && keep > 0 && bi.state == 游走) {
            if (db < keep) {
                float l = std::max(0.1f, db);
                float nx = u->x + rx / l * 3, nz = u->z + rz / l * 3;
                if (Game::dist(nx, nz, 0, 25) > 20) { nx = u->x - rz / l * 3; nz = u->z + rx / l * 3; }
                g.move(u->id, nx, nz);
            } else if (db > keep + 3.0f) {
                float l = std::max(0.1f, db);
                g.move(u->id, u->x - rx / l * 2, u->z - rz / l * 2);
            } else if (u->order != 原地守) g.hold(u->id);
            continue;
        }
        if (u->order != 打 || u->target != boss->id) g.attack(u->id, boss->id);
    }
}

static void army(Game& g) {
    static std::map<int, int> retreat_until;
    auto enemies = g.enemies();
    bool melee = g.mode == "melee";
    float ccx, ccz;  // 地图正中（我方视角）
    g.to_local(0, 0, ccx, ccz);
    // 混战：潮水。谁脚下快被淹了，就往中间走
    std::set<int> busy;
    if (melee) {
        float wl = g.water_at(g.time + 7.0f);
        for (Unit* u : g.mine()) {
            if (u->kind == 投石车) continue;
            float h = g.height(u->x, u->z);
            bool late = g.time > 14.0f;
            float dc = Game::dist(u->x, u->z, ccx, ccz);
            if ((h < wl + 0.6f || (late && dc > 13.0f)) && dc > 4.5f) {
                // 往中心走，目标点在中心周围一圈（礁石过不去）
                float dx = u->x - ccx, dz = u->z - ccz, l = std::max(0.1f, std::hypot(dx, dz));
                float r = 5.0f + float((u->id * 37) % 5);
                if (u->order != 走到 || g.tick % 15 == 0) g.move(u->id, ccx + dx / l * r, ccz + dz / l * r);
                busy.insert(u->id);
            }
        }
    }
    bool rush_zone = (g.mode == "duel" && g.time > g.time_limit - 24 && !g.score_zone.empty());
    for (Unit* u : g.mine()) {
        if (busy.count(u->id)) continue;
        if (u->kind == 长枪兵) {
            bool cav = false;
            for (Unit* e : enemies)
                if (e->kind == 骑兵 && Game::dist(*u, *e) < 16 && std::hypot(e->vx, e->vz) > 3) cav = true;
            if (cav) { if (!u->holding) g.hold(u->id); continue; }
        }
        if (u->kind == 弓手) {
            Unit* close = g.nearest_enemy(u->x, u->z, 3.5f);
            if (close && close->kind != 弓手) {
                float dx = u->x - close->x, dz = u->z - close->z, l = std::max(0.1f, std::hypot(dx, dz));
                float nx = u->x + dx / l * 3, nz = u->z + dz / l * 3;
                if (g.walkable(nx, nz) && (!melee || g.height(nx, nz) > g.water_at(g.time + 4) + 0.6f)) { g.move(u->id, nx, nz); continue; }
            }
            Unit* best = nullptr;
            float bd = 1e9f;
            for (Unit* e : enemies) {
                float d = Game::dist(*u, *e);
                if (d > 27) continue;
                float sc = d - (e->kind == 炮仗兵 ? 100.0f : 0.0f) - (e->kind == 弓手 ? 6.0f : 0.0f) + (e->kind == 盾卫 ? 8.0f : 0.0f) + (e->kind == 大块头 ? 5.0f : 0.0f);
                if (sc < bd) { bd = sc; best = e; }
            }
            if (best) { if (u->target != best->id || u->order != 打) g.attack(u->id, best->id); continue; }
        }
        if (u->kind == 骑兵) {
            float spd = std::hypot(u->vx, u->vz);
            auto it = retreat_until.find(u->id);
            if (it != retreat_until.end() && g.tick < it->second) continue;
            Unit* near = g.nearest_enemy(u->x, u->z, 2.5f);
            if (near && spd < 2.0f && u->state == 站着) {
                float dx = u->x - near->x, dz = u->z - near->z, l = std::max(0.1f, std::hypot(dx, dz));
                float tx = u->x + dx / l * 8, tz = u->z + dz / l * 8;
                if (!g.walkable(tx, tz) || (melee && g.height(tx, tz) < g.water_at(g.time + 5) + 0.6f)) { tx = u->x; tz = u->z; }
                else { g.move(u->id, tx, tz); retreat_until[u->id] = g.tick + 15; continue; }
            }
            Unit* best = nullptr;
            float bd = 1e9f;
            for (Unit* e : enemies) {
                float d = Game::dist(*u, *e) - ((e->kind == 弓手 || e->kind == 投石车) ? 15.0f : 0.0f) + (e->kind == 长枪兵 ? 12.0f : 0.0f);
                if (melee && g.height(e->x, e->z) < g.water_at(g.time + 5) + 0.6f) d += 50;
                if (d < bd) { bd = d; best = e; }
            }
            if (best) { if (u->order != 打 || u->target != best->id) g.attack(u->id, best->id); continue; }
        }
        if (u->kind == 炮仗兵) {
            int n = 0, nm = 0;
            for (Unit* e : enemies) if (Game::dist(*u, *e) < 2.5f) ++n;
            for (Unit* m : g.mine()) if (m != u && Game::dist(*u, *m) < 2.5f) ++nm;
            if (n > 2 && n > nm && !u->fuse) { g.detonate(u->id); continue; }
        }
        if (rush_zone && u->kind != 投石车) {
            if (!g.in_score_zone(u->x, u->z)) {
                if (u->order != 走到) g.move(u->id, ccx + float((u->id * 37) % 7) - 3.0f, ccz + float((u->id * 53) % 7) - 3.0f);
                continue;
            }
        }
        // 其余：按 tag 决定是守还是自由
        if (tag_is(u, "守")) {
            float T = tag_num(u->tag, 0, 30);
            if (g.time < T) { if (u->order != 原地守 && u->order != 打) g.hold(u->id); }
            else if (u->order != 自由 && u->order != 打) g.free(u->id);
        } else if (u->order != 自由 && u->order != 打 && !(u->kind == 长枪兵 && u->holding && g.tick % 20 != 0)) g.free(u->id);
    }
}

void on_start(Game& g) { (void)g; }

static void deathlog(Game& g) {
    static std::set<int> dead;
    for (auto& u : g.units)
        if (!u.alive && !dead.count(u.id)) {
            dead.insert(u.id);
            g.log("DEAD %s id=%d kind=%d t=%.1f x=%.1f y=%.1f z=%.1f", u.mine ? "mine" : "enemy", u.id, int(u.kind), g.time, u.x, u.y, u.z);
        }
}

void on_tick(Game& g) {
#ifdef DBG
    deathlog(g);
#endif
    if (g.mode == "level") {
        if (starts(g.map, "第1关")) L1(g);
        if (starts(g.map, "第2关")) L2(g);
        if (starts(g.map, "第3关")) L3(g);
        if (starts(g.map, "第4关")) L4(g);
        if (starts(g.map, "第5关")) L5(g);
    } else {
        army(g);
    }
}
