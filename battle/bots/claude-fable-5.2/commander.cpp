// 指挥程序：所有场次共用。按 g.mode / g.map 分支；兵的角色写在布阵文件的 tag 里（"角色:k=v,k=v"）。
#include "commander.hpp"
#include <map>
#include <set>
#include <string>

namespace {
struct Tag { std::string role; std::map<std::string, float> kv; };
std::map<int, Tag> tags;
std::map<int, int> stage;       // 每个兵的阶段
std::map<int, float> timer;     // 每个兵的计时
bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

Tag parse_tag(const std::string& t) {
    Tag r;
    size_t c = t.find(':');
    r.role = t.substr(0, c);
    if (c == std::string::npos) return r;
    std::string rest = t.substr(c + 1);
    size_t i = 0;
    while (i < rest.size()) {
        size_t j = rest.find(',', i);
        if (j == std::string::npos) j = rest.size();
        std::string kvs = rest.substr(i, j - i);
        size_t e = kvs.find('=');
        if (e != std::string::npos) r.kv[kvs.substr(0, e)] = float(std::atof(kvs.c_str() + e + 1));
        i = j + 1;
    }
    return r;
}
float P(const Tag& t, const char* k, float d) { auto it = t.kv.find(k); return it == t.kv.end() ? d : it->second; }
const Tag& T(const Unit* u) { return tags[u->id]; }

void mv(Game& g, Unit* u, float x, float z) {  // 只在需要时重新下令
    g.move(u->id, x, z);
}
}  // namespace

void on_start(Game& g) {
    for (auto& u : g.units) if (u.mine) tags[u.id] = parse_tag(u.tag);
}

// ------------------------------------------------------------------ 第 1 关
static void L1(Game& g) {
    int cav = 0;
    for (Unit* e : g.enemies()) if (e->kind == 骑兵) ++cav;
    for (Unit* u : g.mine()) {
        const Tag& t = T(u);
        if (t.role == "hf") {  // 先原地守，骑兵死光（或到时间）以后自由
            float tf = P(t, "t", 1e9f);
            if ((cav <= int(P(t, "c", 0)) || g.time >= tf) && stage[u->id] == 0) { g.free(u->id); stage[u->id] = 1; }
        } else if (t.role == "ah") {  // 骑兵死光以后去追弓手
            float tf = P(t, "t", 1e9f);
            if (cav <= int(P(t, "c", 0)) || g.time >= tf) {
                Unit* best = nullptr; float bd = 1e9f;
                for (Unit* e : g.enemies()) if (e->kind == 弓手) { float d = Game::dist(*u, *e); if (d < bd) { bd = d; best = e; } }
                if (best) { if (u->order != 打 || u->target != best->id) g.attack(u->id, best->id); }
                else if (stage[u->id] != 2) { g.free(u->id); stage[u->id] = 2; }
            }
        }
    }
}

// ------------------------------------------------------------------ 第 2 关
static void L2(Game& g) {
    bool broken = g.terrain_removed("桥中段");
    int nw = 0, nm = 0, ne = 0;
    float maxz = -1e9f;
    for (Unit* e : g.enemies()) {
        if (e->z > 35) ++nw; else if (e->z > 25) ++nm; else ++ne;
        maxz = std::max(maxz, e->z);
    }
    if (g.tick % 10 == 0) g.log("t=%.1f west=%d mid=%d east=%d maxz=%.1f broken=%d", g.time, nw, nm, ne, maxz, int(broken));
    for (Unit* u : g.mine()) {
        const Tag& t = T(u);
        int& st = stage[u->id];
        if (t.role == "plug") {
            if (st == 0) { g.move(u->id, P(t, "x", u->x), P(t, "z", u->z)); st = 1; }
            if (broken && st == 1) {
                if (t.kv.count("rx")) { g.move(u->id, P(t, "rx", u->x), P(t, "rz", u->z)); st = 2; }
                else if (g.time > P(t, "tf", 1e9f)) { g.free(u->id); st = 3; }
            }
            if (st == 2 && g.time > P(t, "tf", 1e9f)) { g.free(u->id); st = 3; }
        } else if (t.role == "keg") {
            if (st == 0) {
                if (t.kv.count("x")) g.move(u->id, P(t, "x", u->x), P(t, "z", u->z));
                st = 1;
            }
            bool fire = (g.time >= P(t, "t0", 0) && maxz < P(t, "zt", 35.0f)) || g.time >= P(t, "t", 1e9f);
            if (st == 1 && fire && !broken) {
                // 选第 k 个桶（按 z 从小到大）；默认选离自己最近的
                Prop* best = nullptr; float bd = 1e9f;
                int want = int(P(t, "k", -1));
                std::vector<Prop*> kegs;
                for (auto& p : g.props) if (p.type == "keg" && p.alive) kegs.push_back(&p);
                std::sort(kegs.begin(), kegs.end(), [](Prop* a, Prop* b) { return a->z < b->z; });
                if (want >= 0 && want < int(kegs.size())) best = kegs[want];
                else for (Prop* p : kegs) { float d = Game::dist(u->x, u->z, p->x, p->z); if (d < bd) { bd = d; best = p; } }
                if (best) { g.attack(u->id, best->id); st = 2; }
            }
            if (broken && st != 3) { g.free(u->id); st = 3; }
        } else if (t.role == "hf") {
            if (broken && st == 0) { g.free(u->id); st = 1; }
        }
    }
}

// ------------------------------------------------------------------ 通用：只在指令变化时下令
namespace {
std::map<int, std::string> last_ord;
struct E0 { float x, z; };
std::map<int, E0> e0;   // 敌人开场位置
bool ord(Unit* u, const std::string& sig) {
    auto it = last_ord.find(u->id);
    if (it != last_ord.end() && it->second == sig) return false;
    last_ord[u->id] = sig;
    return true;
}
std::string sg(const char* k, float a, float b) { char s[64]; std::snprintf(s, sizeof s, "%s %.2f %.2f", k, a, b); return s; }
void o_move(Game& g, Unit* u, float x, float z) { if (ord(u, sg("M", x, z))) g.move(u->id, x, z); }
void o_aim(Game& g, Unit* u, float x, float z) { if (ord(u, sg("P", x, z))) g.aim(u->id, x, z); }
void o_att(Game& g, Unit* u, int id) { if (ord(u, sg("A", float(id), 0))) g.attack(u->id, id); }
void o_hold(Game& g, Unit* u) { if (ord(u, "H")) g.hold(u->id); }
void o_free(Game& g, Unit* u) { if (ord(u, "F")) g.free(u->id); }
void o_push(Game& g, Unit* u, int id) { if (ord(u, sg("U", float(id), 0))) g.push(u->id, id); }
}  // namespace

// ------------------------------------------------------------------ 第 4 关
static void L4(Game& g) {
    if (g.tick == 0) for (auto& u : g.units) if (!u.mine) e0[u.id] = {u.x, u.z};
    std::vector<Unit*> wave, earch, Ash, Asp, B;
    for (Unit* e : g.enemies()) {
        float z0 = e0.count(e->id) ? e0[e->id].z : e->z;
        if (e->kind == 骑兵 || e->kind == 新兵) wave.push_back(e);
        else if (e->kind == 弓手) earch.push_back(e);
        else if (z0 < 60) { if (e->kind == 盾卫) Ash.push_back(e); else Asp.push_back(e); }
        else B.push_back(e);
    }
    Unit* lord = g.lord();
    if (lord && g.tick == 0) g.move(lord->id, 0, 0.5f);
    if (g.tick % 50 == 0) g.log("t=%.0f wave=%d earch=%d Ash=%d Asp=%d B=%d mine=%d", g.time, int(wave.size()), int(earch.size()), int(Ash.size()), int(Asp.size()), int(B.size()), int(g.mine().size()));
    // 每侧弓手编号
    std::map<int, int> idx; int cn = 0, cs = 0;
    for (Unit* u : g.mine()) if (u->kind == 弓手) idx[u->id] = (u->x > 0 ? cn++ : cs++);
    std::map<int, int> taken;
    for (Unit* u : g.mine()) {
        const Tag& t = T(u);
        if (u->kind == 主帅) continue;
        if (u->kind == 弓手) {
            float side = u->x > 0 ? 1.0f : -1.0f;
            int k = idx[u->id];
            if (!wave.empty()) {
                Unit* best = nullptr; float bd = 1e9f;
                for (Unit* e : wave) { float d = Game::dist(*u, *e); if (d < bd) { bd = d; best = e; } }
                float ex = side * 6.0f, ez = P(t, "wz", 11.0f) + 0.9f * k;
                if (stage[u->id] == 0 && Game::dist(u->x, u->z, ex, ez) > 1.2f) { o_move(g, u, ex, ez); continue; }
                stage[u->id] = 1;
                best = nullptr; bd = 1e9f;
                for (Unit* e : wave) { float d = Game::dist(*u, *e); float sc = d + (d < 3.5f ? 100.0f : 0.0f); if (sc < bd) { bd = sc; best = e; } }
                if (best && bd < P(t, "wr", 30)) o_att(g, u, best->id);
                continue;
            }
            if (!earch.empty()) {
                // 先打自己这一侧 z 最小的；没有就打对面的
                Unit* e = nullptr;
                for (Unit* a : earch) if (a->x * side > 0 && (!e || a->z < e->z)) e = a;
                float sx, sz;
                if (e) { sx = side * (7.0f + 0.7f * k); sz = e->z - P(t, "so", 27.0f); }
                else {
                    for (Unit* a : earch) if (!e || a->z < e->z) e = a;
                    sx = side * (5.5f + 0.7f * k); sz = e->z - 24.0f;
                }
                if (Game::dist(u->x, u->z, sx, sz) > 1.5f && stage[u->id] != 100 + e->id) o_move(g, u, sx, sz);
                else { stage[u->id] = 100 + e->id; o_aim(g, u, e->x, e->z); }
                continue;
            }
            if (!B.empty()) {
                float sx = side * 6.0f, sz = P(t, "bz", 68.0f) + 0.9f * k;
                if (stage[u->id] != 2 && Game::dist(u->x, u->z, sx, sz) > 1.5f) { o_move(g, u, sx, sz); continue; }
                stage[u->id] = 2;
                static const int pr[KIND_COUNT] = {5, 4, 3, 5, 5, 2, 1, 9, 9, 9, 9};
                Unit* best = nullptr; float bs = 1e9f;
                for (Unit* e : B) {
                    float d = Game::dist(*u, *e);
                    float fx = std::sin(e->facing), fz = std::cos(e->facing);
                    float cx = (u->x - e->x) / std::max(0.1f, d), cz = (u->z - e->z) / std::max(0.1f, d);
                    bool frontal = e->kind == 盾卫 && (fx * cx + fz * cz) > 0.45f;
                    float s = pr[e->kind] * 100.0f + d * 0.5f + (d < 3.6f ? 500.0f : 0.0f) + (frontal ? 250.0f : 0.0f) + (e->x * side > 2.0f ? 150.0f : 0.0f);
                    if (s < bs) { bs = s; best = e; }
                }
                if (best) o_att(g, u, best->id);
                continue;
            }
            // A：到路障甲旁边
            {
                float sx = side * 6.0f, sz = P(t, "az", 48.0f) + 0.9f * k;
                if (stage[u->id] != 3 && Game::dist(u->x, u->z, sx, sz) > 1.5f) { o_move(g, u, sx, sz); continue; }
                stage[u->id] = 3;
                Unit* best = nullptr; float bs = 1e9f;
                for (Unit* e : g.enemies()) {
                    float d = Game::dist(*u, *e);
                    float fx = std::sin(e->facing), fz = std::cos(e->facing);
                    float cx = (u->x - e->x) / std::max(0.1f, d), cz = (u->z - e->z) / std::max(0.1f, d);
                    bool frontal = e->kind == 盾卫 && (fx * cx + fz * cz) > 0.45f;
                    float s = (e->kind == 盾卫 ? 100.0f : 0.0f) + d + (d < 3.6f ? 500.0f : 0.0f) + (frontal ? 250.0f : 0.0f) + (e->x * side > 2.0f ? 150.0f : 0.0f);
                    if (s < bs) { bs = s; best = e; }
                }
                if (best) o_att(g, u, best->id);
            }
            continue;
        }
        if (t.role == "gi") {  // 大块头：清完木栅前的守军就砸木栅
            Prop* fa = nullptr; Prop* fb = nullptr;
            for (auto& p : g.props) if (p.type == "barrier" && p.alive) { if (p.z < 60) fa = &p; else fb = &p; }
            if (g.time < P(t, "t0", 0)) continue;
            if (wave.empty() && Ash.empty() && Asp.empty() && fa) o_att(g, u, fa->id);
            else if (wave.empty() && Ash.empty() && Asp.empty() && B.empty() && !earch.empty()) {
                if (P(t, "ea", 0) > 0) {   // 试试从谷底够崖顶弓手：走到他正下方的崖壁边再打
                    Unit* best = nullptr; float bd = 1e9f;
                    for (Unit* e : earch) { float d = Game::dist(*u, *e); if (d < bd) { bd = d; best = e; } }
                    float tx = (best->x > 0 ? 1.0f : -1.0f) * P(t, "ex", 3.6f), tz = best->z;
                    if (Game::dist(u->x, u->z, tx, tz) > 0.6f && stage[u->id] != 500 + best->id) o_move(g, u, tx, tz);
                    else { stage[u->id] = 500 + best->id; o_att(g, u, best->id); }
                } else o_hold(g, u);
            }
            else o_free(g, u);
            continue;
        }
        if (t.role == "cl") {  // 崖顶近战：自由（去砍崖顶弓手）
            if (g.time >= P(t, "t0", 0)) o_free(g, u);
            continue;
        }
        // 谷口的近战兵
        if (t.role == "sp") {
            if (!wave.empty()) continue;  // 布阵姿态：原地守
            if (!earch.empty() && P(t, "wait", 1) > 0) { continue; }
            if (!Ash.empty() && u->kind == 大锤) {   // 大锤：手比盾卫长，直接去锤盾阵
                Unit* best = nullptr; float bs = 1e9f;
                for (Unit* e : Ash) { float d = Game::dist(*u, *e); if (d < bs) { bs = d; best = e; } }
                o_att(g, u, best->id);
                continue;
            }
            if (!Ash.empty()) {
                Unit* best = nullptr; float bs = 1e9f;
                for (Unit* e : Ash) {
                    float s = std::fabs(e->x - u->x) + 3.0f * taken[e->id];
                    if (s < bs) { bs = s; best = e; }
                }
                taken[best->id]++;
                float tx = best->x, tz = best->z - P(t, "d", 2.15f);
                if (Game::dist(u->x, u->z, tx, tz) > 0.3f) o_move(g, u, tx, tz);
                else o_hold(g, u);
                continue;
            }
            {   // 路障甲的守军清完了：砸木栅，再去清路障乙
                Prop* fa = nullptr;
                for (auto& p : g.props) if (p.type == "barrier" && p.alive && p.z < 60) fa = &p;
                if (Asp.empty() && fa && P(t, "fence", 0) > 0) { o_att(g, u, fa->id); continue; }
            }
            o_free(g, u);
        }
    }
}

// ------------------------------------------------------------------ 第 5 关
namespace { int l5_anchor = 0; int l5_prev = -1; bool l5_B = false; int l5_bait = -1; }
static void L5(Game& g) {
    if (g.bosses.empty()) return;
    BossInfo& b = g.bosses[0];
    Unit* boss = g.unit(b.id);
    if (!boss || !boss->alive) return;
    auto mine = g.mine();
    if (mine.empty()) return;
    const Tag& t0 = T(mine[0]);
    float AX = P(t0, "ax", 9.5f), AZ = P(t0, "az", 3.0f), GX = P(t0, "gx", 6.5f), GZ = P(t0, "gz", 6.5f);
    // 诱饵：带 bait 标签的；死了就换编号最小的
    Unit* bait = g.unit(l5_bait);
    if (!bait || !bait->alive) {
        bait = nullptr;
        for (Unit* u : mine) if (T(u).role == "bait") { bait = u; break; }
        if (!bait) bait = mine[0];
        l5_bait = bait->id;
    }
    int ecav = 0;
    for (Unit* e : g.enemies()) if (e->kind == 骑兵) ++ecav;
    if (!l5_B && (ecav <= int(P(t0, "c", 0)) || g.time > P(t0, "ta", 45.0f))) {
        l5_B = true;
        l5_anchor = boss->x > 0 ? -1 : 1;
    }
    if (l5_B && l5_prev == int(晕) && b.state == 游走) l5_anchor = boss->x > 0 ? -1 : 1;  // 晕完了：去另一头
    l5_prev = int(b.state);
    float sgn = float(l5_anchor);
    float dx = b.dir_x, dz = b.dir_z;
    Unit* far = nullptr; float fd = -1;
    for (Unit* u : mine) { float d = Game::dist(*u, *boss); if (d > fd) { fd = d; far = u; } }
    if (b.state != 冲撞 && far && fd > 0.5f) { dx = (far->x - boss->x) / fd; dz = (far->z - boss->z) / fd; }
    bool danger = (b.state == 刨地 || b.state == 冲撞) || (b.state == 游走 && b.next_charge < 0.3f);
    float block = 1e9f;
    for (auto& p : g.props) if (p.type == "pillar" && p.alive) {
        float px = p.x - boss->x, pz = p.z - boss->z;
        float al = px * dx + pz * dz, sd = std::fabs(px * dz - pz * dx);
        if (al > 0 && sd < 2.0f) block = std::min(block, al);
    }
    if (g.tick % 20 == 0) g.log("t=%.1f boss hp=%.0f st=%d pos=%.1f,%.1f mine=%d cav=%d B=%d anchor=%d", g.time, boss->hp, int(b.state), boss->x, boss->z, int(mine.size()), ecav, int(l5_B), l5_anchor);
    int slot = 0;
    for (Unit* u : mine) {
        const Tag& t = T(u);
        float rx = u->x - boss->x, rz = u->z - boss->z;
        float d = std::max(0.01f, std::hypot(rx, rz));
        bool isBait = (u == bait) && l5_B;
        int myslot = isBait ? -1 : slot++;
        if (danger) {
            float along = rx * dx + rz * dz, side = rx * dz - rz * dx;
            float lim = P(t, "lim", 3.2f);
            if (along > -2.5f && along < block - 0.5f && std::fabs(side) < lim) {
                bool locked = (b.state == 冲撞) || (b.state == 刨地 && b.state_left < P(t, "early", 0.25f));
                if (u == far && !locked) { /* 被锁定的人：快锁死了再闪 */ }
                else {
                    float sg2 = side >= 0 ? 1.0f : -1.0f;
                    if (u == far) sg2 = (dx * sgn > 0) ? -sgn : sgn, sg2 = (dz * 0 + 1) * ((-dx) * sgn > 0 ? 1.0f : -1.0f);  // 诱饵往场内（+z 一侧）闪
                    float tx = boss->x + along * dx + sg2 * (lim + 1.5f) * dz, tz = boss->z + along * dz - sg2 * (lim + 1.5f) * dx;
                    if (u == far) { tx = u->x; tz = u->z + 4.5f; }
                    if (!g.walkable(tx, tz) || g.height(tx, tz) > 1.0f) { tx = boss->x + along * dx - sg2 * (lim + 1.5f) * dz; tz = boss->z + along * dz + sg2 * (lim + 1.5f) * dx; }
                    o_move(g, u, tx, tz);
                    continue;
                }
            }
        }
        if (!l5_B) {  // 骑兵没死光：回到自己的位置架枪
            auto it = e0.find(u->id);
            if (it != e0.end() && Game::dist(u->x, u->z, it->second.x, it->second.z) > 1.0f && d > 6.0f) o_move(g, u, it->second.x, it->second.z);
            else o_hold(g, u);
            continue;
        }
        bool stunNear = (b.state == 晕) && boss->z < P(t, "sz", 11.0f);
        if (stunNear) { o_att(g, u, boss->id); continue; }
        if (b.state == 冲撞 || b.state == 刨地) {
            if (isBait && b.state == 刨地) { o_move(g, u, sgn * AX, AZ); continue; }
            if (b.state == 刨地) continue;   // 保持
            continue;
        }
        // 游走（或晕在远处）：去锚点
        if (isBait) { o_move(g, u, sgn * AX, AZ); continue; }
        float tx = sgn * (GX - 0.8f * float(myslot % 4)), tz = GZ + 0.8f * float(myslot / 4);
        if (Game::dist(u->x, u->z, tx, tz) > 0.5f) o_move(g, u, tx, tz); else o_hold(g, u);
    }
}

// ------------------------------------------------------------------ 第 3 关
namespace {
struct Stg { int kind; float x, z, r; int side; float sx = 0, sz = 0; };   // kind=-1 任意；side: -1 只要 x<0，1 只要 x>0，0 不限
std::map<std::string, int> l3_stage, l3_ready;
bool l3_match(const Stg& st, Unit* e) {
    auto it = e0.find(e->id);
    float x0 = it != e0.end() ? it->second.x : e->x, z0 = it != e0.end() ? it->second.z : e->z;
    if (st.kind >= 0 && int(e->kind) != st.kind) return false;
    if (st.side < 0 && x0 >= 0) return false;
    if (st.side > 0 && x0 <= 0) return false;
    if (st.r > 0 && Game::dist(x0, z0, st.x, st.z) > st.r) return false;
    return true;
}
}
static void L3(Game& g) {
    if (g.tick == 0) for (auto& u : g.units) if (!u.mine) e0[u.id] = {u.x, u.z};
    static const std::vector<Stg> SW = {
        {int(弓手), 0, 0, 0, -1}, {int(投石车), -17, 57, 5, 0}, {int(长枪兵), -17, 59.5f, 4, 0},
        {int(投石车), 0, 59, 5, 0}, {int(长枪兵), 0, 61.5f, 4, 0}, {int(长枪兵), 0, 43, 8, 0, -8.5f, 44.0f}, {int(盾卫), 0, 41, 9, 0, -7.5f, 43.5f}, {-1, 0, 0, 0, 0}};
    static const std::vector<Stg> SE = {
        {int(长枪兵), 29, 41, 4, 0}, {int(弓手), 0, 0, 0, 1}, {int(投石车), 17, 57, 5, 0}, {int(长枪兵), 17, 59.5f, 4, 0},
        {int(长枪兵), 0, 43, 8, 0, 8.5f, 44.0f}, {int(盾卫), 0, 41, 9, 0, 7.5f, 43.5f}, {-1, 0, 0, 0, 0}};
    auto en = g.enemies();
    if (g.tick % 50 == 0) g.log("t=%.0f enemies=%d mine=%d stW=%d stE=%d", g.time, int(en.size()), int(g.mine().size()), l3_stage["cw"], l3_stage["ce"]);
    for (const char* role : {"cw", "ce"}) {
        const std::vector<Stg>& S = (std::string(role) == "cw") ? SW : SE;
        int& k = l3_stage[role];
        while (k < int(S.size()) - 1) {
            bool any = false;
            for (Unit* e : en) if (l3_match(S[k], e)) { any = true; break; }
            if (any) break;
            ++k;
        }
    }
    for (Unit* u : g.mine()) {
        const Tag& t = T(u);
        if (t.role != "cw" && t.role != "ce") continue;
        // 躲石头
        bool dodged = false;
        for (auto& f : g.flying) {
            float dl = Game::dist(f.x, f.z, f.land_x, f.land_z);
            float du = Game::dist(u->x, u->z, f.land_x, f.land_z);
            float sp = std::hypot(f.vx, f.vz);
            float ds = Game::dist(u->x, u->z, f.x, f.z);
            bool airborne = f.y > u->y + 1.5f;
            if ((airborne && dl < P(t, "dl", 14.0f) && du < P(t, "dr", 2.6f)) || (!airborne && sp > 4 && ds < 4.0f)) {
                float vx = f.vx / std::max(0.1f, sp), vz = f.vz / std::max(0.1f, sp);
                float cx = airborne ? f.land_x : f.x, cz = airborne ? f.land_z : f.z;
                float side = (u->x - cx) * vz - (u->z - cz) * vx;
                float sg2 = side >= 0 ? 1.0f : -1.0f;
                float tx = u->x + sg2 * 3.5f * vz, tz = u->z - sg2 * 3.5f * vx;
                if (!g.walkable(tx, tz) || std::fabs(g.height(tx, tz) - u->y) > 1.2f) { tx = u->x - sg2 * 3.5f * vz; tz = u->z + sg2 * 3.5f * vx; }
                o_move(g, u, tx, tz);
                dodged = true;
                break;
            }
        }
        if (dodged) continue;
        const std::vector<Stg>& S = (t.role == "cw") ? SW : SE;
        const Stg& st = S[l3_stage[t.role]];
        Unit* best = nullptr; float bd = 1e9f;
        int sk = l3_stage[t.role];
        bool staged = (st.sx != 0 || st.sz != 0) && P(t, "stg", 0) > 0;
        if (staged && l3_ready[t.role] != 1000 + sk) {   // 全队先到集结点，再一起从侧面卷过去
            bool all = true;
            for (Unit* v : g.mine()) if (T(v).role == t.role && Game::dist(v->x, v->z, st.sx, st.sz) > 3.0f) all = false;
            if (all) l3_ready[t.role] = 1000 + sk;
            else {
                if (Game::dist(u->x, u->z, st.sx, st.sz) > 1.5f) o_move(g, u, st.sx, st.sz); else o_hold(g, u);
                continue;
            }
        }
        for (Unit* e : en) if (l3_match(st, e)) {
            float d = staged ? Game::dist(e->x, e->z, st.sx, st.sz) : Game::dist(*u, *e);
            if (d < bd) { bd = d; best = e; }
        }
        if (!best) { o_free(g, u); continue; }
        bd = Game::dist(*u, *best);
        // 盾卫不去啃原地守的盾卫（太慢），在旁边等
        if (u->kind == 盾卫 && best->kind == 盾卫 && st.kind == int(盾卫) && P(t, "sh", 0) < 1) {
            if (bd > 6.0f) o_move(g, u, best->x, best->z + 5.0f); else o_hold(g, u);
            continue;
        }
        o_att(g, u, best->id);
    }
}

// ------------------------------------------------------------------ 对抗
namespace { float vcx = 0, vcz = 28, vhx = 0, vhz = 0, vr0 = 30; }
static bool cav_near(Game& g, Unit* u, float r) {
    for (auto& e : g.units) if (e.alive && e.team != g.team && e.kind == 骑兵 && Game::dist(*u, e) < r && std::hypot(e.vx, e.vz) > 2.8f) return true;
    return false;
}
static void bomber_logic(Game& g, Unit* u) {
    int n = 0, m = 0;
    for (auto& e : g.units) if (e.alive && Game::dist(*u, e) < 2.6f) { if (e.team != g.team) ++n; else if (e.id != u->id) ++m; }
    if (n >= 3 && n > m && !u->fuse) g.detonate(u->id);
}
// 弓手：集火（射程内"等效血量"最少的），被步兵贴身就后撤。返回 true 表示已经下了指令
static bool archer_micro(Game& g, Unit* u) {
    const Tag& t = T(u);
    if (P(t, "kite", 0) > 0) {
        Unit* th = nullptr; float tm = 1e9f;
        for (auto& e : g.units) if (e.alive && e.team != g.team && e.kind != 弓手 && e.kind != 投石车 && e.kind != 骑兵) {
            float d = Game::dist(*u, e);
            float lim = (e.kind == 大块头) ? P(t, "kg", 4.0f) : P(t, "kd", 4.0f);
            if (d < lim && d - lim < tm) { tm = d - lim; th = &e; }
        }
        if (th) {
            float dx = u->x - th->x, dz = u->z - th->z, l = std::max(0.1f, std::hypot(dx, dz));
            float tx = u->x + dx / l * 3.0f, tz = u->z + dz / l * 3.0f;
            float h = g.height(tx, tz);
            if (h > -900 && h > g.water + 0.6f && std::fabs(h - u->y) < 1.0f) { o_move(g, u, tx, tz); return true; }
        }
    }
    if (P(t, "ff", 0) > 0) {
        Unit* best = nullptr; float bs = 1e9f;
        float rng = P(t, "fr", 26.5f);
        for (auto& e : g.units) if (e.alive && e.team != g.team) {
            float d = Game::dist(*u, e);
            if (d > rng || d < 3.2f) continue;
            float eff = e.hp;
            if (e.kind == 盾卫 && e.state == 站着) {
                float fx = std::sin(e.facing), fz = std::cos(e.facing);
                if ((fx * (u->x - e.x) + fz * (u->z - e.z)) / std::max(0.1f, d) > 0.5f) eff *= 3.3f;
            }
            if (e.kind == 大块头) eff *= 0.5f;          // 大块头值钱，但血厚：折中
            if (e.kind == 投石车) eff *= 2.0f;
            float sc = eff + d * 0.5f;
            if (sc < bs) { bs = sc; best = &e; }
        }
        if (best) { o_att(g, u, best->id); return true; }
    }
    return false;
}
static void Duel(Game& g) {
    bool river = has(g.map, "河心洲");
    float zx = river ? 7.0f : 0.0f, zz = river ? 24.5f : 28.0f;
    bool late = g.time > g.time_limit - (river ? 30.0f : 26.0f);
    int slot = 0;
    for (Unit* u : g.mine()) {
        if (u->kind == 投石车) continue;
        if (u->kind == 炮仗兵) bomber_logic(g, u);
        int k = slot++;
        Unit* e = g.nearest_enemy(u->x, u->z);
        float de = e ? Game::dist(*u, *e) : 1e9f;
        if (late && !g.in_score_zone(u->x, u->z) && (de > 4.0f || u->kind == 大块头)) {
            float tx = zx + float(k % 5 - 2) * 1.2f, tz = zz + float(k / 5 % 4) * 1.0f;
            o_move(g, u, tx, tz);
            continue;
        }
        if (late && g.in_score_zone(u->x, u->z) && de > 9.0f) { o_hold(g, u); continue; }
        if (u->kind == 长枪兵 && cav_near(g, u, 13.0f)) { o_hold(g, u); continue; }
        if (u->kind == 弓手 && archer_micro(g, u)) continue;
        if (u->kind == 大块头 && P(T(u), "gt", 0) > 0) {
            Unit* best = nullptr; float bd = 1e9f;
            for (auto& x : g.units) if (x.alive && x.team != g.team && x.kind != 大块头) {
                float d = Game::dist(*u, x) + (x.kind == 骑兵 ? 15.0f : 0.0f) - ((x.kind == 弓手 || x.kind == 投石车) ? P(T(u), "ga", 0.0f) : 0.0f);
                if (d < bd) { bd = d; best = &x; }
            }
            if (best && bd < P(T(u), "gr", 60.0f)) { o_att(g, u, best->id); continue; }
        }
        o_free(g, u);
    }
}
static void Melee(Game& g) {
    if (g.tick == 0) {
        // 地图中心 = 各家质心的平均；我家方向 = 我方质心相对中心
        std::map<int, std::pair<float, float>> sum; std::map<int, int> cnt;
        float mx = 0, mz = 0; int mn = 0;
        for (auto& u : g.units) { sum[u.team].first += u.x; sum[u.team].second += u.z; cnt[u.team]++; if (u.mine) { mx += u.x; mz += u.z; ++mn; } }
        vcx = 0; vcz = 35;   // 默认：我方视角正前方 35 米
        if (mn > 0) { mx /= mn; mz /= mn; }
        float hx = mx - vcx, hz = mz - vcz, hl = std::max(0.1f, std::hypot(hx, hz));
        vhx = hx / hl; vhz = hz / hl; vr0 = hl;
    }
    // 12 秒以后水面之上、离中心最远的半径
    float wl = g.water_at(g.time + 12.0f);
    float rs = 4.0f;
    for (float r = 34.0f; r >= 4.0f; r -= 0.5f) {
        float h = g.height(vcx + vhx * r, vcz + vhz * r);
        if (h > -900 && h > wl + 0.7f) { rs = r; break; }
    }
    float rr = std::min(rs - 2.0f, vr0);
    if (rr < 5.0f) rr = 5.0f;
    float px = -vhz, pz = vhx;  // 横向
    int slot = 0;
    for (Unit* u : g.mine()) {
        if (u->kind == 投石车) continue;
        if (u->kind == 炮仗兵) bomber_logic(g, u);
        int k = slot++;
        Unit* e = g.nearest_enemy(u->x, u->z);
        float de = e ? Game::dist(*u, *e) : 1e9f;
        float ur = Game::dist(u->x, u->z, vcx, vcz);
        bool wet = ur > rs + 0.5f;          // 快被淹了：先往里走
        float eng = (u->kind == 弓手) ? 26.0f : 9.0f;
        if (!wet && de < eng) {
            if (u->kind == 长枪兵 && cav_near(g, u, 12.0f)) { o_hold(g, u); continue; }
            // 敌人在水边外面就别追出去
            float er = Game::dist(e->x, e->z, vcx, vcz);
            if (er > rs + 1.0f && u->kind != 弓手) { o_hold(g, u); continue; }
            if (u->kind == 弓手 && archer_micro(g, u)) continue;
            if (u->kind == 大块头 && P(T(u), "gt", 0) > 0) {   // 大块头别和大块头对耗，去扫别的
                Unit* best = nullptr; float bd = 1e9f;
                for (auto& x : g.units) if (x.alive && x.team != g.team && x.kind != 大块头) {
                    float d = Game::dist(*u, x);
                    if (Game::dist(x.x, x.z, vcx, vcz) > rs + 0.5f) continue;
                    if (d < bd) { bd = d; best = &x; }
                }
                if (best && bd < P(T(u), "gr", 14.0f)) { o_att(g, u, best->id); continue; }
            }
            o_free(g, u);
            continue;
        }
        float lat = float(k % 7 - 3) * 1.1f, dep = float(k / 7) * 1.0f;
        float tx = vcx + vhx * (rr - dep) + px * lat, tz = vcz + vhz * (rr - dep) + pz * lat;
        if (Game::dist(u->x, u->z, tx, tz) > 1.0f) o_move(g, u, tx, tz);
        else o_hold(g, u);
    }
}

void on_tick(Game& g) {
    if (g.mode == "level") {
        if (g.tick == 0) for (auto& u : g.units) if (u.mine) e0[u.id] = {u.x, u.z};
        if (has(g.map, "草甸")) L1(g);
        else if (has(g.map, "断龙桥")) L2(g);
        else if (has(g.map, "拔炮台")) L3(g);
        else if (has(g.map, "一线天")) L4(g);
        else if (has(g.map, "镇山兽")) L5(g);
    } else if (g.mode == "duel") {
        Duel(g);
    } else if (g.mode == "melee") {
        Melee(g);
    }
}
