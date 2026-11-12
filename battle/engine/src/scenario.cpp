// scenario.cpp —— 读地图 / 关卡 / 布阵文件，校验布阵。
#include "scenario.hpp"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace war {

// ------------------------------------------------------------------ 规则参数
namespace {
struct RuleField { const char* name; float* p; };
std::vector<RuleField> rule_fields() {
    Rules& r = RULES;
    return {
        {"corpse_secs", &r.corpse_secs}, {"corpse_max", &r.corpse_max}, {"fell_remove_secs", &r.fell_remove_secs},
        {"knock_secs", &r.knock_secs}, {"getup_secs", &r.getup_secs}, {"topple_tilt", &r.topple_tilt},
        {"stun_secs", &r.stun_secs}, {"kill_credit_secs", &r.kill_credit_secs}, {"speed_cap", &r.speed_cap}, {"max_hit_dv", &r.max_hit_dv},
        {"shield_cos", &r.shield_cos}, {"shield_dmg", &r.shield_dmg}, {"shield_knock", &r.shield_knock},
        {"brace_still", &r.brace_still}, {"brace_speed", &r.brace_speed}, {"brace_cd", &r.brace_cd},
        {"brace_slow", &r.brace_slow}, {"brace_range", &r.brace_range}, {"brace_cos", &r.brace_cos}, {"brace_max_mass", &r.brace_max_mass},
        {"archer_range", &r.archer_range}, {"archer_stand", &r.archer_stand}, {"archer_min", &r.archer_min},
        {"archer_height_bonus", &r.archer_height_bonus}, {"archer_range_max", &r.archer_range_max},
        {"arrow_angle", &r.arrow_angle}, {"dagger_dmg", &r.dagger_dmg}, {"dagger_cycle", &r.dagger_cycle},
        {"dagger_reach", &r.dagger_reach}, {"charge_speed", &r.charge_speed}, {"charge_k", &r.charge_k},
        {"charge_impulse", &r.charge_impulse}, {"charge_after", &r.charge_after}, {"charge_rehit", &r.charge_rehit},
        {"bomb_dmg_center", &r.bomb_dmg_center}, {"bomb_dmg_edge", &r.bomb_dmg_edge}, {"bomb_radius", &r.bomb_radius},
        {"bomb_impulse", &r.bomb_impulse}, {"bomb_fuse", &r.bomb_fuse}, {"bomb_contact", &r.bomb_contact},
        {"cat_first", &r.cat_first}, {"cat_min", &r.cat_min}, {"cat_max", &r.cat_max}, {"stone_mass", &r.stone_mass},
        {"stone_r", &r.stone_r}, {"stone_k", &r.stone_k}, {"stone_min_speed", &r.stone_min_speed},
        {"stone_angle", &r.stone_angle}, {"stone_rest", &r.stone_rest}, {"stone_life", &r.stone_life},
        {"cluster_r", &r.cluster_r}, {"retarget_secs", &r.retarget_secs}, {"retarget_ratio", &r.retarget_ratio},
        {"march_engage", &r.march_engage}, {"order_arrive", &r.order_arrive}, {"flag_hit_mul", &r.flag_hit_mul},
    };
}
struct DefField { const char* name; size_t off; bool is_int; };
const DefField DEF_FIELDS[] = {
    {"cost", offsetof(UnitDef, cost), false}, {"hp", offsetof(UnitDef, hp), false}, {"dmg", offsetof(UnitDef, dmg), false},
    {"cycle", offsetof(UnitDef, cycle), false}, {"windup", offsetof(UnitDef, windup), false},
    {"reach", offsetof(UnitDef, reach), false}, {"width", offsetof(UnitDef, width), false},
    {"victims", offsetof(UnitDef, victims), true}, {"speed", offsetof(UnitDef, speed), false},
    {"accel", offsetof(UnitDef, accel), false}, {"mass", offsetof(UnitDef, mass), false},
    {"turn", offsetof(UnitDef, turn), false}, {"impulse", offsetof(UnitDef, impulse), false},
    {"up", offsetof(UnitDef, up), false}, {"knock", offsetof(UnitDef, knock), false},
};
}  // namespace

bool rules_set(const std::string& key, double v) {
    const size_t dot = key.find('.');
    if (dot != std::string::npos) {
        const int k = kind_from_name(key.substr(0, dot));
        if (k < 0) return false;
        const std::string f = key.substr(dot + 1);
        for (auto& d : DEF_FIELDS)
            if (f == d.name) {
                char* base = reinterpret_cast<char*>(&DEFS[k]);
                if (d.is_int) *reinterpret_cast<int*>(base + d.off) = int(std::lround(v));
                else *reinterpret_cast<float*>(base + d.off) = float(v);
                return true;
            }
        return false;
    }
    for (auto& f : rule_fields())
        if (key == f.name) { *f.p = float(v); return true; }
    return false;
}

std::string rules_dump() {
    std::string o = "{\"rules\":{";
    bool first = true;
    char b[96];
    for (auto& f : rule_fields()) {
        std::snprintf(b, sizeof b, "%s\"%s\":%g", first ? "" : ",", f.name, double(*f.p));
        o += b;
        first = false;
    }
    o += "},\"units\":{";
    for (int k = 0; k < NKIND; ++k) {
        o += std::string(k ? "," : "") + "\"" + DEFS[k].id + "\":{\"zh\":" + js::quote(DEFS[k].zh);
        for (auto& d : DEF_FIELDS) {
            const char* base = reinterpret_cast<const char*>(&DEFS[k]);
            const double v = d.is_int ? double(*reinterpret_cast<const int*>(base + d.off)) : double(*reinterpret_cast<const float*>(base + d.off));
            std::snprintf(b, sizeof b, ",\"%s\":%g", d.name, v);
            o += b;
        }
        std::snprintf(b, sizeof b, ",\"radius\":%g,\"scale\":%g,\"player\":%s}", double(DEFS[k].radius), double(DEFS[k].scale), DEFS[k].for_player ? "true" : "false");
        o += b;
    }
    return o + "}}";
}

// ------------------------------------------------------------------ 坐标
// 90 度整数倍的朝向取精确的 0 / ±1（float 的 sin(π) 不是 0，镜像的两个座位会差百万分之一米）
static float snap_unit(float v) {
    if (std::fabs(v) < 1e-6f) return 0.0f;
    if (std::fabs(v - 1.0f) < 1e-6f) return 1.0f;
    if (std::fabs(v + 1.0f) < 1e-6f) return -1.0f;
    return v;
}
void SeatDef::to_world(float lx, float lz, float& wx, float& wz) const {
    const float c = snap_unit(std::cos(yaw)), s = snap_unit(std::sin(yaw));
    wx = ox + lx * c + lz * s;
    wz = oz - lx * s + lz * c;
}
void SeatDef::to_local(float wx, float wz, float& lx, float& lz) const {
    const float c = snap_unit(std::cos(yaw)), s = snap_unit(std::sin(yaw)), dx = wx - ox, dz = wz - oz;
    lx = dx * c - dz * s;
    lz = dx * s + dz * c;
}

int Scenario::nteams() const {
    int n = int(seats.size());
    for (auto& h : host) n = std::max(n, h.team + 1);
    for (auto& p : props) n = std::max(n, p.team + 1);
    return std::max(n, 1);
}

// ------------------------------------------------------------------ 读文件
std::string read_file(const std::string& path, std::string& out) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.data(), int(path.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), int(path.size()), &w[0], n);
    FILE* f = _wfopen(w.c_str(), L"rb");
    if (!f) return "打不开文件 " + path;
    char b[65536];
    size_t r;
    out.clear();
    while ((r = std::fread(b, 1, sizeof b, f)) > 0) out.append(b, r);
    std::fclose(f);
    return "";
}

namespace {
const float DEG = PI / 180.0f;

bool nums(const js::Value* v, float* out, size_t n) {
    if (!v || !v->is_arr() || v->size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (!(*v)[i].is_num()) return false;
        out[i] = float((*v)[i].n);
    }
    return true;
}

std::string behavior_name(const std::string& b) {
    if (b == "默认" || b == "default" || b == "自由" || b == "free") return "default";
    if (b == "原地守" || b == "hold") return "hold";
    if (b == "行军" || b == "march") return "march";
    if (b == "不动" || b == "idle") return "idle";
    return "";
}

std::string read_host_unit(const js::Value& u, HostUnit& h, int default_team) {
    const std::string t = u.str("type", "");
    h.kind = kind_from_name(t);
    if (h.kind < 0) return "不认识的兵种 \"" + t + "\"";
    h.team = int(u.num("team", default_team));
    h.x = float(u.num("x", 0));
    h.z = float(u.num("z", 0));
    h.y = float(u.num("y", -1));
    h.yaw = float(u.num("yaw", 0)) * DEG;
    h.behavior = behavior_name(u.str("behavior", "default"));
    if (h.behavior.empty()) return "不认识的行为 \"" + u.str("behavior", "") + "\"（default / hold / march / idle）";
    float to[2];
    if (nums(u.get("to"), to, 2)) { h.has_to = true; h.tx = to[0]; h.tz = to[1]; }
    h.to_tag = u.str("to_tag", "");
    h.tag = u.str("tag", "");
    h.delay = float(u.num("delay", 0));
    h.hp = float(u.num("hp", -1));
    auto pts = [&](const char* k, std::vector<std::array<float, 2>>& out) -> bool {
        const js::Value* r = u.get(k);
        if (!r) return true;
        for (size_t i = 0; i < r->size(); ++i) {
            std::array<float, 2> a;
            if (!nums(&(*r)[i], a.data(), 2)) return false;
            out.push_back(a);
        }
        return true;
    };
    if (!pts("route", h.route) || !pts("alt_route", h.alt_route)) return "route / alt_route 要写成 [[x,z],…]";
    h.alt_trigger = u.str("alt_trigger", "");
    nums(u.get("alt_region"), h.alt_region, 4);
    h.rally_idx = int(u.num("rally_idx", -1));
    h.rally_secs = float(u.num("rally_secs", 0));
    h.rally_r = float(u.num("rally_r", 8));
    if (h.rally_idx >= int(h.alt_route.size())) return "rally_idx 超出 alt_route 的路点数";
    h.squad = u.str("squad", "");
    h.sortie = u.boolean("sortie", false);
    if (h.behavior == "march" && !h.has_to && h.to_tag.empty() && h.route.empty()) return "行军要写 to:[x,z]、to_tag 或 route";
    return "";
}

std::string read_cond(const js::Value& c, Cond& o) {
    o.type = c.str("type", "");
    o.team = int(c.num("team", 0));
    o.tag = c.str("tag", "");
    o.zone = c.str("zone", "");
    o.secs = float(c.num("secs", 0));
    o.count = int(c.num("count", 1));
    o.unit_tag = c.str("unit_tag", "");
    if (o.type != "eliminate" && o.type != "destroy" && o.type != "zone_held" && o.type != "reach" && o.type != "dead")
        return "不认识的判定 \"" + o.type + "\"（eliminate / destroy / dead / zone_held / reach）";
    if ((o.type == "destroy" || o.type == "dead") && o.tag.empty()) return o.type + " 要写 tag";
    if ((o.type == "zone_held" || o.type == "reach") && o.zone.empty()) return o.type + " 要写 zone";
    return "";
}
}  // namespace

std::string load_scenario(const std::string& path, Scenario& sc) {
    std::string text;
    std::string e = read_file(path, text);
    if (!e.empty()) return e;
    js::Value root;
    e = js::parse(text, root);
    if (!e.empty()) return path + " " + e;
    if (!root.is_obj()) return path + "：最外层要是 {…}";
    sc.path = path;
    sc.raw = text;
    sc.name = root.str("name", "");
    sc.mode = root.str("mode", "sandbox");
    if (sc.mode != "duel" && sc.mode != "melee" && sc.mode != "level" && sc.mode != "sandbox")
        return "mode 只能是 duel / melee / level / sandbox";
    sc.theme = root.str("theme", "");
    sc.desc = root.str("desc", "");
    nums(root.get("bounds"), sc.bounds, 4);
    sc.time_limit = float(root.num("time_limit", 90));
    sc.kill_y = float(root.num("kill_y", -3));
    sc.score_zone = root.str("score_zone", "");
    sc.timeout_win = root.str("on_timeout", "lose") == "win";

    if (const js::Value* t = root.get("terrain")) {
        if (!t->is_arr()) return "terrain 要是数组";
        for (size_t i = 0; i < t->size(); ++i) {
            const js::Value& p = (*t)[i];
            Prim q;
            q.type = p.str("type", "");
            q.mat = p.str("mat", "ground");
            q.tag = p.str("tag", "");
            q.boss_stun = p.boolean("boss_stun", false);
            const std::string at = "terrain 第 " + std::to_string(i + 1) + " 个（" + q.type + "）：";
            if (q.type == "box") {
                float mn[3], mx[3];
                if (nums(p.get("min"), mn, 3) && nums(p.get("max"), mx, 3)) {
                    for (int k = 0; k < 3; ++k) { q.c[k] = (mn[k] + mx[k]) * 0.5f; q.h[k] = std::fabs(mx[k] - mn[k]) * 0.5f; }
                } else if (!(nums(p.get("c"), q.c, 3) && nums(p.get("h"), q.h, 3))) return at + "box 要写 min/max 或 c/h";
                q.yaw = float(p.num("yaw", 0)) * DEG;
                if (q.h[0] <= 0.01f || q.h[1] <= 0.01f || q.h[2] <= 0.01f) return at + "box 太薄";
            } else if (q.type == "ramp") {
                float a[2], b[2];
                if (!nums(p.get("from"), a, 2) || !nums(p.get("to"), b, 2)) return at + "ramp 要写 from:[x,z] to:[x,z]";
                q.x0 = a[0]; q.z0 = a[1]; q.x1 = b[0]; q.z1 = b[1];
                q.w = float(p.num("width", 4));
                q.h0 = float(p.num("h0", 0));
                q.h1 = float(p.num("h1", 1));
                q.base = float(p.num("base", std::min(q.h0, q.h1) - 1));
            } else if (q.type == "cyl" || q.type == "mound") {
                float c[2];
                if (!nums(p.get("c"), c, 2)) return at + "要写 c:[x,z]";
                q.c[0] = c[0]; q.c[2] = c[1];
                q.r0 = float(p.num("r", p.num("r0", 1)));
                q.r1 = float(p.num("r1", q.r0));
                q.y0 = float(p.num("y0", 0));
                q.y1 = float(p.num("y1", 1));
                q.segs = int(p.num("segs", q.type == "mound" ? 32 : 12));
                if (q.y1 <= q.y0) return at + "y1 要大于 y0";
            } else if (q.type == "hull") {
                const js::Value* pts = p.get("pts");
                if (!pts || !pts->is_arr() || pts->size() < 4) return at + "hull 至少 4 个点";
                for (size_t k = 0; k < pts->size(); ++k) {
                    float v[3];
                    if (!nums(&(*pts)[k], v, 3)) return at + "hull 的点要写 [x,y,z]";
                    q.pts.push_back({v[0], v[1], v[2]});
                }
            } else if (q.type == "forest") {
                // 一片树林：在矩形里按固定间距种树干（圆柱），用确定的抖动，不用随机数
                float mn[2], mx[2];
                if (!nums(p.get("min"), mn, 2) || !nums(p.get("max"), mx, 2)) return at + "forest 要写 min:[x,z] max:[x,z]";
                const float sp = float(p.num("spacing", 3)), r = float(p.num("r", 0.35f)), h = float(p.num("h", 5));
                const float y0 = float(p.num("y0", -0.5));
                int idx = 0;
                for (float z = mn[1] + sp * 0.5f; z <= mx[1]; z += sp)
                    for (float x = mn[0] + sp * 0.5f; x <= mx[0]; x += sp) {
                        ++idx;
                        const unsigned hsh = unsigned(idx) * 2654435761u;
                        const float jx = (float((hsh >> 8) & 255) / 255.0f - 0.5f) * sp * 0.5f;
                        const float jz = (float((hsh >> 16) & 255) / 255.0f - 0.5f) * sp * 0.5f;
                        Prim tr;
                        tr.type = "cyl";
                        tr.mat = p.str("mat", "tree");
                        tr.tag = q.tag;
                        tr.c[0] = x + jx; tr.c[2] = z + jz;
                        tr.r0 = tr.r1 = r; tr.y0 = y0; tr.y1 = h; tr.segs = 8;
                        sc.prims.push_back(tr);
                    }
                continue;
            } else return at + "不认识的地形类型（box / ramp / cyl / mound / hull / forest）";
            sc.prims.push_back(q);
        }
    }
    if (const js::Value* zs = root.get("zones")) {
        for (size_t i = 0; i < zs->size(); ++i) {
            const js::Value& z = (*zs)[i];
            Zone o;
            o.name = z.str("name", "");
            o.speed_mul = float(z.num("speed_mul", 1));
            o.stick_secs = float(z.num("stick_secs", 0));
            if (o.stick_secs < 0) return "zones 第 " + std::to_string(i + 1) + " 个的 stick_secs 不能是负数";
            if (nums(z.get("circle"), o.a, 3)) o.circle = true;
            else if (!nums(z.get("rect"), o.a, 4)) return "zones 第 " + std::to_string(i + 1) + " 个要写 circle:[cx,cz,r] 或 rect:[x0,z0,x1,z1]";
            sc.zones.push_back(o);
        }
    }
    if (!sc.score_zone.empty() && !sc.zone(sc.score_zone)) return "score_zone 指向不存在的区域 " + sc.score_zone;
    if (const js::Value* w = root.get("water")) {
        if (w->has("tide")) {
            const js::Value* t = w->get("tide");
            sc.water.kind = 2;
            sc.water.t0 = float(t->num("t0", 20));
            sc.water.t1 = float(t->num("t1", 75));
            sc.water.h0 = float(t->num("h0", 0));
            sc.water.h1 = float(t->num("h1", 4));
        } else {
            sc.water.kind = 1;
            sc.water.level = float(w->num("level", -0.6));
        }
    }
    const float def_budget = float(root.num("budget", 1000));
    const int def_max = int(root.num("max_units", 70));
    if (const js::Value* ss = root.get("seats")) {
        for (size_t i = 0; i < ss->size(); ++i) {
            const js::Value& s = (*ss)[i];
            SeatDef d;
            float o[2];
            if (!nums(s.get("origin"), o, 2)) return "seats 第 " + std::to_string(i + 1) + " 个要写 origin:[x,z]";
            d.ox = o[0]; d.oz = o[1];
            d.yaw = float(s.num("yaw", 0)) * DEG;
            d.budget = float(s.num("budget", def_budget));
            d.max_units = int(s.num("max_units", def_max));
            // 限购：默认不限（11-11 协调方：预算是唯一限制）；需要时在地图/关卡文件的 seats[].limits 里写
            if (const js::Value* lm = s.get("limits")) {
                if (!lm->is_obj()) return "limits 要写成 {\"兵种\": 个数}";
                for (auto& kv : lm->o) {
                    const int k = kind_from_name(kv.first);
                    if (k < 0 || !kv.second.is_num()) return "limits 里不认识的兵种 " + kv.first;
                    d.limit[k] = int(kv.second.n);
                }
            }
            if (const js::Value* ld = s.get("lord")) {
                d.has_lord = true;
                d.lord_x = float(ld->num("x", 0));
                d.lord_z = float(ld->num("z", 0));
                d.lord_yaw = float(ld->num("yaw", 0)) * DEG;
            }
            const js::Value* dp = s.get("deploy");
            if (!dp || !dp->is_arr() || dp->size() == 0) return "seats 第 " + std::to_string(i + 1) + " 个要写 deploy:[[x0,z0,x1,z1],…]";
            for (size_t k = 0; k < dp->size(); ++k) {
                std::array<float, 4> r;
                if (!nums(&(*dp)[k], r.data(), 4)) return "deploy 的矩形要写 [x0,z0,x1,z1]";
                d.deploy.push_back(r);
            }
            sc.seats.push_back(d);
        }
    }
    if (sc.mode == "duel" && sc.seats.size() != 2) return "duel 要正好 2 个 seats";
    if (sc.mode == "melee" && sc.seats.size() != 4) return "melee 要正好 4 个 seats";
    if (sc.mode == "level" && sc.seats.size() != 1) return "level 要正好 1 个 seats（选手）";
    const int host_team = sc.mode == "level" ? 1 : 1;
    if (const js::Value* ps = root.get("props")) {
        for (size_t i = 0; i < ps->size(); ++i) {
            const js::Value& p = (*ps)[i];
            PropDef d;
            d.type = p.str("type", "");
            if (d.type == "雪球") d.type = "snowball";
            if (d.type == "火药桶") d.type = "keg";
            if (d.type == "军旗") d.type = "flag";
            if (d.type == "木栅") d.type = "barrier";
            if (d.type == "木栅门") d.type = "gate";
            if (d.type == "立石") d.type = "pillar";
            if (d.type != "snowball" && d.type != "keg" && d.type != "flag" && d.type != "barrier" && d.type != "gate" && d.type != "pillar")
                return "props 第 " + std::to_string(i + 1) + " 个：类型只能是 snowball / keg / flag / barrier / gate / pillar";
            d.tag = p.str("tag", "");
            d.team = int(p.num("team", d.type == "flag" ? 0 : -1));
            d.x = float(p.num("x", 0));
            d.y = float(p.num("y", -1));
            d.z = float(p.num("z", 0));
            d.r = float(p.num("r", d.type == "snowball" ? 1.5 : 0.6));
            d.hp = float(p.num("hp", d.type == "flag" ? 300 : d.type == "barrier" ? 300 : d.type == "gate" ? 400 : d.type == "pillar" ? 1e9 : 1));
            d.mass = float(p.num("mass", 2000));
            d.push_secs = float(p.num("push_secs", 1.0));
            d.yaw = float(p.num("yaw", 0)) * DEG;
            d.release_speed = float(p.num("release_speed", 3));
            d.dmg_k = float(p.num("dmg_k", 6));
            d.dmg_max = float(p.num("dmg_max", 90));
            float sz[3];
            if (nums(p.get("size"), sz, 3)) { d.hx = sz[0] * 0.5f; d.hy = sz[1] * 0.5f; d.hz = sz[2] * 0.5f; }
            else if (d.type == "barrier") { d.hx = 4.5f; d.hy = 0.8f; d.hz = 0.2f; }
            else if (d.type == "gate") { d.hx = 3.0f; d.hy = 1.25f; d.hz = 0.25f; }
            else if (d.type == "pillar") { d.hx = 0.9f; d.hy = 1.5f; d.hz = 0.9f; }
            d.boss_hits = int(p.num("boss_hits", d.type == "pillar" ? 2 : 0));
            d.breaks = p.str("breaks", "");
            d.break_delay = float(p.num("break_delay", 0.3));
            sc.props.push_back(d);
        }
    }
    auto add_host = [&](const js::Value& u) -> std::string {
        if (const js::Value* g = u.get("group")) {
            // 方阵：rows 排 × cols 列，(x,z) 是第一排的中点，排往背后排
            HostUnit base;
            std::string e2 = read_host_unit(*g, base, host_team);
            if (!e2.empty()) return e2;
            const int rows = int(g->num("rows", 1)), cols = int(g->num("cols", 1));
            const float dx = float(g->num("dx", 1.0)), dz = float(g->num("dz", 1.0));
            const float c = std::cos(base.yaw), s = std::sin(base.yaw);
            for (int r = 0; r < rows; ++r)
                for (int k = 0; k < cols; ++k) {
                    HostUnit h = base;
                    const float lx = (k - (cols - 1) * 0.5f) * dx, lz = -r * dz;
                    h.x = base.x + lx * c + lz * s;
                    h.z = base.z - lx * s + lz * c;
                    sc.host.push_back(h);
                }
            return "";
        }
        HostUnit h;
        std::string e2 = read_host_unit(u, h, host_team);
        if (!e2.empty()) return e2;
        sc.host.push_back(h);
        return "";
    };
    if (const js::Value* hs = root.get("units")) {
        for (size_t i = 0; i < hs->size(); ++i) {
            std::string e2 = add_host((*hs)[i]);
            if (!e2.empty()) return "units 第 " + std::to_string(i + 1) + " 个：" + e2;
        }
    }
    if (const js::Value* ob = root.get("objectives")) {
        for (const char* k : {"win", "lose"}) {
            const js::Value* l = ob->get(k);
            if (!l) continue;
            for (size_t i = 0; i < l->size(); ++i) {
                Cond c;
                std::string e2 = read_cond((*l)[i], c);
                if (!e2.empty()) return std::string("objectives.") + k + "：" + e2;
                if (!c.zone.empty() && !sc.zone(c.zone)) return "objectives 指向不存在的区域 " + c.zone;
                (std::strcmp(k, "win") == 0 ? sc.win_any : sc.lose_any).push_back(c);
            }
        }
        sc.timeout_win = ob->str("on_timeout", sc.timeout_win ? "win" : "lose") == "win";
    }
    if (const js::Value* b = root.get("boss")) {
        BossParams& B = sc.boss;
        B.interval = float(b->num("interval", B.interval));
        B.paw_secs = float(b->num("paw_secs", B.paw_secs));
        B.charge_speed = float(b->num("charge_speed", B.charge_speed));
        B.overshoot = float(b->num("overshoot", B.overshoot));
        B.charge_dmg = float(b->num("charge_dmg", B.charge_dmg));
        B.charge_impulse = float(b->num("charge_impulse", B.charge_impulse));
        B.stun_secs = float(b->num("stun_secs", B.stun_secs));
        B.stun_mul = float(b->num("stun_mul", B.stun_mul));
        B.front_mul = float(b->num("front_mul", B.front_mul));
        B.paw_turn = float(b->num("paw_turn", B.paw_turn));
    }

    return "";
}

// ------------------------------------------------------------------ 布阵
Deploy check_deploy_text(const std::string& text, const Scenario& sc, int seat) {
    Deploy d;
    js::Value root;
    std::string e = js::parse(text, root);
    if (!e.empty()) { d.errors.push_back("JSON 有错：" + e); return d; }
    const js::Value* us = root.is_obj() ? root.get("units") : nullptr;
    if (!us || !us->is_arr()) { d.errors.push_back("要写成 {\"units\": [ … ]}"); return d; }
    if (seat < 0 || seat >= int(sc.seats.size())) { d.errors.push_back("这张图没有第 " + std::to_string(seat) + " 号座位"); return d; }
    const SeatDef& S = sc.seats[size_t(seat)];
    auto on_grid = [](float v) { const float t = v * 2.0f; return std::fabs(t - std::round(t)) < 1e-4f; };
    for (size_t i = 0; i < us->size(); ++i) {
        const js::Value& u = (*us)[i];
        const std::string at = "第 " + std::to_string(i + 1) + " 个单位：";
        DeployUnit du;
        const std::string t = u.str("type", "");
        du.kind = kind_from_name(t);
        if (du.kind < 0) { d.errors.push_back(at + "不认识的兵种 \"" + t + "\""); continue; }
        if (!DEFS[du.kind].for_player) { d.errors.push_back(at + DEFS[du.kind].zh + " 不能买"); continue; }
        const js::Value* x = u.get("x");
        const js::Value* z = u.get("z");
        if (!x || !z || !x->is_num() || !z->is_num()) { d.errors.push_back(at + "要写数字 x 和 z"); continue; }
        du.x = float(x->n);
        du.z = float(z->n);
        if (!on_grid(du.x) || !on_grid(du.z)) d.errors.push_back(at + "坐标要是 0.5 的整数倍");
        bool inside = false;
        for (auto& r : S.deploy)
            if (du.x >= r[0] - 1e-4f && du.x <= r[2] + 1e-4f && du.z >= r[1] - 1e-4f && du.z <= r[3] + 1e-4f) inside = true;
        if (!inside) d.errors.push_back(at + "不在布阵区里");
        const std::string st = u.str("stance", "自由");
        if (st == "原地守" || st == "hold") du.hold = true;
        else if (st != "自由" && st != "free") d.errors.push_back(at + "stance 只能是 \"自由\" 或 \"原地守\"");
        du.tag = u.str("tag", "");
        if (du.tag.size() > 32) d.errors.push_back(at + "tag 太长（最多 32 字节）");
        for (char c : du.tag)
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { d.errors.push_back(at + "tag 里不能有空白"); break; }
        d.cost += DEFS[du.kind].cost;
        d.units.push_back(du);
    }
    // 重叠：按占地矩形（我方视角，都朝 +z）判
    for (size_t i = 0; i < d.units.size(); ++i)
        for (size_t j = i + 1; j < d.units.size(); ++j) {
            const DeployUnit &a = d.units[i], &b = d.units[j];
            const UnitDef &A = DEFS[a.kind], &B = DEFS[b.kind];
            if (std::fabs(a.x - b.x) < A.hx + B.hx - 1e-3f && std::fabs(a.z - b.z) < A.hz + B.hz - 1e-3f) {
                d.errors.push_back("第 " + std::to_string(i + 1) + " 个和第 " + std::to_string(j + 1) + " 个单位重叠了");
                if (d.errors.size() > 30) break;
            }
        }
    for (int k = 0; k < NKIND; ++k) {
        if (S.limit[k] < 0) continue;
        int c = 0;
        for (auto& u : d.units) if (u.kind == k) ++c;
        if (c > S.limit[k]) d.errors.push_back(std::string(DEFS[k].zh) + " 最多 " + std::to_string(S.limit[k]) + " 个（买了 " + std::to_string(c) + " 个）");
    }
    if (d.units.empty()) d.errors.push_back("至少要上 1 个兵");
    if (int(d.units.size()) > S.max_units)
        d.errors.push_back("人数 " + std::to_string(d.units.size()) + " 超过上限 " + std::to_string(S.max_units));
    if (d.cost > S.budget + 1e-3f) {
        char b[96];
        std::snprintf(b, sizeof b, "造价 %g 超过预算 %g", double(d.cost), double(S.budget));
        d.errors.push_back(b);
    }
    return d;
}

Deploy load_deploy(const std::string& path, const Scenario& sc, int seat) {
    std::string text;
    std::string e = read_file(path, text);
    Deploy d;
    if (!e.empty()) { d.path = path; d.errors.push_back(e); return d; }
    d = check_deploy_text(text, sc, seat);
    d.path = path;
    return d;
}

}  // namespace war
