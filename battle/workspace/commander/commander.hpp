// commander.hpp —— 《纸上谈兵》指挥程序头文件（选手用）。说明见 接口说明.md。
//
// 你只要写两个函数：
//     #include "commander.hpp"
//     void on_start(Game& g) { ... }   // 开局调用一次（在第一回合的 on_tick 之前，这时已经能看到全场单位）
//     void on_tick(Game& g)  { ... }   // 每 0.1 秒（6 个物理步）调用一次，在里面下指令
// 编译：g++ -O2 -std=c++17 -static 你的程序.cpp -o 你的程序.exe
//
// 坐标全部是"我方视角"：x 向右，z 朝敌人（和布阵文件一样），单位米。程序看得到全场，没有迷雾。
// 规则：程序必须是确定的，不许读时钟、不许用随机数（裁判和你本地用的是同一个模拟器，结果要一模一样）。
// 想打调试信息就写到标准错误（g.log 或 std::cerr），不要写到标准输出。
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <queue>
#include <string>
#include <vector>

// ------------------------------------------------------------------ 兵种和状态
enum Kind { 新兵 = 0, 盾卫, 长枪兵, 弓手, 骑兵, 大锤, 炮仗兵, 投石车, 大块头, 镇山兽, 主帅, KIND_COUNT };
enum State { 站着 = 0, 倒地 = 1, 起身 = 2, 阵亡 = 3 };
enum OrderType { 自由 = 0, 原地守 = 1, 走到 = 2, 打 = 3, 瞄 = 4, 行军 = 5, 不动 = 6, 推 = 7 };
enum BossState { 游走 = 0, 刨地 = 1, 冲撞 = 2, 晕 = 3 };

struct KindInfo {
    std::string id;     // 英文名
    float cost = 0, hp = 0, dmg = 0, cycle = 0, windup = 0, reach = 0, speed = 0, radius = 0, climb = 0;
};

struct Unit {
    int id = 0;
    Kind kind = 新兵;
    int team = 0, seat = -1;
    bool mine = false;      // 是不是我的兵
    State state = 站着;
    bool alive = false;     // 站着、倒地、起身都算活着
    float x = 0, y = 0, z = 0;   // 我方视角坐标；y 是脚底高度
    float vx = 0, vz = 0;        // 速度（我方视角）
    float hp = 0;
    float cooldown = 0;     // 离下次能出手还有几秒（0 = 现在就能）；投石车 = 离下一发还有几秒
    bool attacking = false; // 正在起手
    bool braced = false;    // 长枪兵已经架好枪
    bool fuse = false;      // 炮仗兵已经点火
    bool holding = false;   // 正在原地守
    float facing = 0;       // 朝向（我方视角，弧度；0 = 朝 +z，正值往 +x 偏）
    OrderType order = 自由; // 当前指令
    int target = -1;        // 正在打的目标编号（-1 = 没有）
    std::string tag;        // 布阵文件里起的标签（只有自己的兵有）
};

struct Prop {
    int id = 0;
    std::string type;       // snowball（雪球）| keg（火药桶）| flag（军旗）| barrier（木栅）| gate（木栅门）| pillar（立石）
    int team = -1;          // 军旗属于哪一方；其他 -1
    bool alive = true;
    bool triggered = false; // 雪球已经松开 / 火药桶已经点着
    float x = 0, y = 0, z = 0, hp = 0;
    std::string tag;
};

struct Flying {             // 飞行中的石头、滚动中的雪球
    int id = 0;
    float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
    float land_x = 0, land_z = 0;  // 预计落点（估计）
};

struct BossInfo {           // 镇山兽（第 5 关）
    int id = 0;
    BossState state = 游走;
    float dir_x = 0, dir_z = 1;   // 冲撞锁定的方向（我方视角，单位向量；刨地完成前是上一次的）
    float next_charge = 0;        // 离下一次冲撞开始还有几秒（正在冲 = 0）
    float state_left = 0;         // 这个状态还剩几秒（冲撞时 = 还要冲几米）
};

struct ZoneInfo {
    std::string name;
    bool circle = false;
    float a[4] = {0, 0, 0, 0};  // 世界坐标
};

class Game;
void on_start(Game& g);
void on_tick(Game& g);

class Game {
public:
    // ---- 基本信息
    std::string mode;       // "duel"（1v1）| "melee"（四人混战）| "level"（闯关）| "sandbox"
    std::string map;        // 地图名，例如 "樱丘"、"河心洲"、"涨潮沙洲"
    int seat = 0, team = 0, teams = 2;
    int tick = 0;           // 第几回合（每回合 0.1 秒）
    float time = 0;         // 开打以后第几秒
    float time_limit = 90;  // 时限（秒）
    float water = -1e9f;    // 当前水面高度（没有水 = 很小的数）
    std::string score_zone; // 计分区的名字（没有 = 空）
    KindInfo info[KIND_COUNT];

    // ---- 场上的东西（每回合刷新）
    std::vector<Unit> units;      // 全场还在场上的单位（含尸体）
    std::vector<Prop> props;      // 道具和军旗
    std::vector<Flying> flying;   // 飞行中的石头、滚动中的雪球
    std::vector<BossInfo> bosses; // 镇山兽
    // 上一回合（0.1 秒内）发生的事，每条一行，例如 "terrain_removed 桥中段"、"destroyed 37 gate"、
    // "released 12"（雪球松开）、"boss 41 charge"（镇山兽换状态）
    std::vector<std::string> events;
    bool terrain_removed(const std::string& tag) const { for (auto& t : removed_) if (t == tag) return true; return false; }

    // ---- 查找
    Unit* unit(int id) { for (auto& u : units) if (u.id == id) return &u; return nullptr; }
    std::vector<Unit*> mine() { std::vector<Unit*> v; for (auto& u : units) if (u.mine && u.alive) v.push_back(&u); return v; }
    std::vector<Unit*> enemies() { std::vector<Unit*> v; for (auto& u : units) if (u.alive && u.team != team) v.push_back(&u); return v; }
    std::vector<Unit*> by_tag(const std::string& t) { std::vector<Unit*> v; for (auto& u : units) if (u.mine && u.alive && u.tag == t) v.push_back(&u); return v; }
    std::vector<Unit*> mine_of(Kind k) { std::vector<Unit*> v; for (auto& u : units) if (u.mine && u.alive && u.kind == k) v.push_back(&u); return v; }
    Unit* nearest_enemy(float x, float z, float maxd = 1e9f) {
        Unit* b = nullptr;
        float bd = maxd * maxd;
        for (auto& u : units) {
            if (!u.alive || u.team == team) continue;
            const float d = (u.x - x) * (u.x - x) + (u.z - z) * (u.z - z);
            if (d < bd) { bd = d; b = &u; }
        }
        return b;
    }
    float cost_of(const Unit& u) const { return info[u.kind].cost; }
    static float dist(const Unit& a, const Unit& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }
    static float dist(float ax, float az, float bx, float bz) { return std::sqrt((ax - bx) * (ax - bx) + (az - bz) * (az - bz)); }

    // ---- 地图（开局给一次）
    // 地面高度；空洞（掉下去出局）、墙里、水下返回很小的数（-1000）
    float height(float x, float z) const { float wx, wz; to_world(x, z, wx, wz); return grid_h(wx, wz); }
    bool walkable(float x, float z) const { return height(x, z) > -999; }
    bool in_zone(const std::string& name, float x, float z) const {
        float wx, wz;
        to_world(x, z, wx, wz);
        for (auto& zn : zones_)
            if (zn.name == name) {
                if (zn.circle) return (wx - zn.a[0]) * (wx - zn.a[0]) + (wz - zn.a[1]) * (wz - zn.a[1]) <= zn.a[2] * zn.a[2];
                return wx >= zn.a[0] && wx <= zn.a[2] && wz >= zn.a[1] && wz <= zn.a[3];
            }
        return false;
    }
    bool in_score_zone(float x, float z) const { return !score_zone.empty() && in_zone(score_zone, x, z); }
    // 第 t 秒的水面高度（潮水会涨）
    float water_at(float t) const {
        if (wkind_ == 0) return -1e9f;
        if (wkind_ == 1) return wlevel_;
        if (t <= wt0_) return wh0_;
        if (t >= wt1_) return wh1_;
        return wh0_ + (wh1_ - wh0_) * (t - wt0_) / (wt1_ - wt0_);
    }
    // 绕路走过去有多远（米）；走不到返回 -1。climb = 能翻过的高度（步兵 1.1，骑兵 0.5）
    float path_dist(float x0, float z0, float x1, float z1, float climb = 1.1f);

    // ---- 指令（这一回合下的，下完一直有效，直到下一条）
    void move(int id, float x, float z) { pt('M', id, x, z); }          // 走到 (x,z)，路上不还手；走到以后原地守
    void move_straight(int id, float x, float z) { pt('L', id, x, z); } // 同上，但走直线、不绕路（自己负责别掉下去）
    void attack(int id, int target) { op('A', id, target, true); }          // 追上去打这个目标（单位或道具）；目标没了就回到自由行动
    void hold(int id) { op('H', id, 0, false); }                         // 原地守：不离开现在的位置，只打够得着的（长枪兵架枪）
    void free(int id) { op('F', id, 0, false); }                         // 自由行动 = 默认行为
    void aim(int id, float x, float z) { pt('P', id, x, z); }           // 弓手、投石车：朝这个地点射
    void detonate(int id) { op('D', id, 0, false); }                     // 炮仗兵：立刻点火，0.6 秒后炸
    void push(int id, int snowball) { op('U', id, snowball, true); }     // 推雪球：走到球背后顶住（大锤：走到跟前敲一下）
    Unit* lord() { for (auto& u : units) if (u.mine && u.kind == 主帅) return &u; return nullptr; }
    Prop* prop(int id) { for (auto& p : props) if (p.id == id) return &p; return nullptr; }

    // 调试输出（写到标准错误；本地 war.exe run --stderr-dir 目录 可以看到）
    void log(const char* fmt, ...) {
        va_list a;
        va_start(a, fmt);
        std::fprintf(stderr, "[%d] ", tick);
        std::vfprintf(stderr, fmt, a);
        std::fputc('\n', stderr);
        va_end(a);
    }

    // ---- 坐标换算（一般用不到）
    void to_world(float x, float z, float& wx, float& wz) const { wx = ox_ + x * c_ + z * s_; wz = oz_ - x * s_ + z * c_; }
    void to_local(float wx, float wz, float& x, float& z) const { const float dx = wx - ox_, dz = wz - oz_; x = dx * c_ - dz * s_; z = dx * s_ + dz * c_; }

    // ================================================================ 以下是内部实现
    int run();

private:
    float ox_ = 0, oz_ = 0, yaw_ = 0, c_ = 1, s_ = 0;
    int gw_ = 0, gh_ = 0;
    float gx0_ = 0, gz0_ = 0, gcell_ = 0.5f;
    std::vector<float> grid_;
    std::vector<ZoneInfo> zones_;
    int wkind_ = 0;
    float wlevel_ = 0, wt0_ = 0, wt1_ = 0, wh0_ = 0, wh1_ = 0;
    struct Static { int kind, team, seat; std::string tag; };
    std::vector<Static> static_;  // 按编号
    std::vector<std::string> ptags_, ptypes_;
    std::vector<int> pteams_;
    std::vector<std::string> removed_;
    std::string out_;
    int steps_per_tick_ = 6, hz_ = 60;

    float grid_h(float wx, float wz) const {
        const int cx = int(std::floor((wx - gx0_) / gcell_)), cz = int(std::floor((wz - gz0_) / gcell_));
        if (cx < 0 || cz < 0 || cx >= gw_ || cz >= gh_) return -1000;
        return grid_[size_t(cz) * gw_ + cx];
    }
    void pt(char k, int id, float x, float z) {
        float wx, wz;
        to_world(x, z, wx, wz);
        char b[64];
        std::snprintf(b, sizeof b, " %c %d %ld %ld", k, id, std::lround(wx * 100), std::lround(wz * 100));
        out_ += b;
    }
    void op(char k, int id, int a, bool with = true) {
        char b[48];
        if (with) std::snprintf(b, sizeof b, " %c %d %d", k, id, a);
        else std::snprintf(b, sizeof b, " %c %d", k, id);
        out_ += b;
    }

    // 读一行（不含换行）
    static bool read_line(std::string& s) {
        s.clear();
        static char buf[1 << 16];
        for (;;) {
            if (!std::fgets(buf, sizeof buf, stdin)) return !s.empty();
            s += buf;
            if (!s.empty() && s.back() == '\n') { s.pop_back(); if (!s.empty() && s.back() == '\r') s.pop_back(); return true; }
        }
    }
    bool read_init();
    bool read_tick();
};

// ------------------------------------------------------------------ 实现
inline bool Game::read_init() {
    std::string l;
    char a[256], b[256];
    while (read_line(l)) {
        if (l == "END") return true;
        if (l.rfind("MODE ", 0) == 0) mode = l.substr(5);
        else if (l.rfind("MAP ", 0) == 0) map = l.substr(4);
        else if (l.rfind("SEAT ", 0) == 0) { int s, t, ts, ss; std::sscanf(l.c_str(), "SEAT %d TEAM %d TEAMS %d SEATS %d", &s, &t, &ts, &ss); seat = s; team = t; teams = ts; }
        else if (l.rfind("FRAME ", 0) == 0) {
            std::sscanf(l.c_str(), "FRAME %f %f %f", &ox_, &oz_, &yaw_);
            // 90 度整数倍的朝向取精确的 0 / ±1（和引擎一致，两个镜像座位的坐标换算完全对称）
            auto snap = [](float v) { return std::fabs(v) < 1e-5f ? 0.0f : (std::fabs(v - 1) < 1e-5f ? 1.0f : (std::fabs(v + 1) < 1e-5f ? -1.0f : v)); };
            c_ = snap(std::cos(yaw_));
            s_ = snap(std::sin(yaw_));
        }
        else if (l.rfind("LIMIT ", 0) == 0) { int st, ev, hz; std::sscanf(l.c_str(), "LIMIT %d %d %d", &st, &ev, &hz); steps_per_tick_ = ev; hz_ = hz; time_limit = float(st) / float(hz); }
        else if (l.rfind("WATER ", 0) == 0) {
            if (std::sscanf(l.c_str(), "WATER static %f", &wlevel_) == 1) wkind_ = 1;
            else if (std::sscanf(l.c_str(), "WATER tide %f %f %f %f", &wt0_, &wt1_, &wh0_, &wh1_) == 4) wkind_ = 2;
        } else if (l.rfind("DEF ", 0) == 0) {
            int k;
            KindInfo ki;
            if (std::sscanf(l.c_str(), "DEF %d %255s %f %f %f %f %f %f %f %f %f", &k, a, &ki.cost, &ki.hp, &ki.dmg, &ki.cycle, &ki.windup, &ki.reach, &ki.speed, &ki.radius, &ki.climb) == 11 && k >= 0 && k < KIND_COUNT) {
                ki.id = a;
                info[k] = ki;
            }
        } else if (l.rfind("GRID ", 0) == 0) {
            std::sscanf(l.c_str(), "GRID %d %d %f %f %f", &gw_, &gh_, &gx0_, &gz0_, &gcell_);
            grid_.assign(size_t(gw_) * size_t(gh_), -1000);
            for (int z = 0; z < gh_; ++z) {
                if (!read_line(l)) return false;
                const char* p = l.c_str();
                for (int x = 0; x < gw_; ++x) {
                    while (*p == ' ') ++p;
                    if (*p == 'x') { ++p; continue; }
                    char* e;
                    const long v = std::strtol(p, &e, 10);
                    grid_[size_t(z) * gw_ + x] = float(v) / 100.0f;
                    p = e;
                }
            }
        } else if (l.rfind("Z ", 0) == 0) {
            ZoneInfo zi;
            float v[4] = {0, 0, 0, 0};
            const int n = std::sscanf(l.c_str(), "Z %255s %255s %f %f %f %f", a, b, &v[0], &v[1], &v[2], &v[3]);
            if (n >= 5) {
                zi.name = a;
                zi.circle = std::string(b) == "circle";
                for (int i = 0; i < 4; ++i) zi.a[i] = v[i];
                zones_.push_back(zi);
            }
        } else if (l.rfind("SCORE ", 0) == 0) { score_zone = l.substr(6); if (score_zone == "-") score_zone.clear(); }
        else if (l.rfind("UNITS ", 0) == 0) {
            const int n = std::atoi(l.c_str() + 6);
            for (int i = 0; i < n; ++i) {
                if (!read_line(l)) return false;
                int id, k, t, s;
                if (std::sscanf(l.c_str(), "%d %d %d %d %255s", &id, &k, &t, &s, a) != 5) continue;
                if (int(static_.size()) <= id) static_.resize(size_t(id) + 1);
                static_[size_t(id)] = {k, t, s, std::string(a) == "-" ? std::string() : std::string(a)};
            }
        } else if (l.rfind("PROPS ", 0) == 0) {
            const int n = std::atoi(l.c_str() + 6);
            for (int i = 0; i < n; ++i) {
                if (!read_line(l)) return false;
                int id, t;
                float x, y, z;
                if (std::sscanf(l.c_str(), "%d %255s %d %f %f %f %255s", &id, a, &t, &x, &y, &z, b) != 7) continue;
                if (int(ptags_.size()) <= id) { ptags_.resize(size_t(id) + 1); ptypes_.resize(size_t(id) + 1); pteams_.resize(size_t(id) + 1, -1); }
                ptypes_[size_t(id)] = a;
                pteams_[size_t(id)] = t;
                ptags_[size_t(id)] = std::string(b) == "-" ? "" : b;
            }
        }
    }
    return false;
}

inline bool Game::read_tick() {
    std::string l;
    if (!read_line(l)) return false;
    int stp = 0;
    long wl = 0;
    if (std::sscanf(l.c_str(), "T %d %d %ld", &tick, &stp, &wl) != 3) return false;
    time = float(stp) / float(hz_);
    water = wl == -99999 ? -1e9f : float(wl) / 100.0f;
    units.clear();
    props.clear();
    flying.clear();
    if (!read_line(l) || l.rfind("U ", 0) != 0) return false;
    int n = std::atoi(l.c_str() + 2);
    units.reserve(size_t(n));
    for (int i = 0; i < n; ++i) {
        if (!read_line(l)) return false;
        long v[13];
        const char* p = l.c_str();
        for (int k = 0; k < 13; ++k) { char* e; v[k] = std::strtol(p, &e, 10); p = e; }
        Unit u;
        u.id = int(v[0]);
        u.state = State(std::min<long>(v[1], 3));
        u.alive = v[1] <= 2;
        const float wx = float(v[2]) / 100.0f, wz = float(v[4]) / 100.0f;
        to_local(wx, wz, u.x, u.z);
        u.y = float(v[3]) / 100.0f;
        const float wvx = float(v[5]) / 100.0f, wvz = float(v[6]) / 100.0f;
        u.vx = wvx * c_ - wvz * s_;
        u.vz = wvx * s_ + wvz * c_;
        u.hp = float(v[7]) / 10.0f;
        u.attacking = v[8] < 0;
        u.cooldown = v[8] < 0 ? 0.0f : float(v[8]) / float(hz_);
        u.braced = (v[9] & 2) != 0;
        u.fuse = (v[9] & 4) != 0;
        u.holding = (v[9] & 8) != 0;
        u.facing = float(v[10]) / 1000.0f - yaw_;
        u.order = OrderType(v[11]);
        u.target = int(v[12]);
        if (u.id >= 0 && u.id < int(static_.size())) {
            const Static& s = static_[size_t(u.id)];
            u.kind = Kind(s.kind);
            u.team = s.team;
            u.seat = s.seat;
            u.tag = s.tag;
            u.mine = s.seat == seat;
        }
        units.push_back(u);
    }
    if (!read_line(l) || l.rfind("V ", 0) != 0) return false;
    n = std::atoi(l.c_str() + 2);
    events.clear();
    for (int i = 0; i < n; ++i) {
        if (!read_line(l)) return false;
        events.push_back(l);
        if (l.rfind("terrain_removed ", 0) == 0) removed_.push_back(l.substr(16));
    }
    if (!read_line(l) || l.rfind("B ", 0) != 0) return false;
    n = std::atoi(l.c_str() + 2);
    bosses.clear();
    for (int i = 0; i < n; ++i) {
        if (!read_line(l)) return false;
        long v[6];
        const char* p = l.c_str();
        for (int k = 0; k < 6; ++k) { char* e; v[k] = std::strtol(p, &e, 10); p = e; }
        BossInfo b;
        b.id = int(v[0]);
        b.state = BossState(v[1]);
        const float wx = float(v[2]) / 1000.0f, wz = float(v[3]) / 1000.0f;
        b.dir_x = wx * c_ - wz * s_;
        b.dir_z = wx * s_ + wz * c_;
        b.next_charge = float(v[4]) / float(hz_);
        b.state_left = b.state == 冲撞 ? float(v[5]) / 100.0f : float(v[5]) / float(hz_);
        bosses.push_back(b);
    }
    if (!read_line(l) || l.rfind("P ", 0) != 0) return false;
    n = std::atoi(l.c_str() + 2);
    for (int i = 0; i < n; ++i) {
        if (!read_line(l)) return false;
        long v[7];
        const char* p = l.c_str();
        for (int k = 0; k < 7; ++k) { char* e; v[k] = std::strtol(p, &e, 10); p = e; }
        Prop q;
        q.id = int(v[0]);
        q.alive = v[1] != 0;
        to_local(float(v[2]) / 100.0f, float(v[4]) / 100.0f, q.x, q.z);
        q.y = float(v[3]) / 100.0f;
        q.hp = float(v[5]) / 10.0f;
        q.triggered = v[6] != 0;
        if (q.id < int(ptags_.size())) { q.tag = ptags_[size_t(q.id)]; q.type = ptypes_[size_t(q.id)]; q.team = pteams_[size_t(q.id)]; }
        props.push_back(q);
    }
    if (!read_line(l) || l.rfind("S ", 0) != 0) return false;
    n = std::atoi(l.c_str() + 2);
    for (int i = 0; i < n; ++i) {
        if (!read_line(l)) return false;
        long v[9];
        const char* p = l.c_str();
        for (int k = 0; k < 9; ++k) { char* e; v[k] = std::strtol(p, &e, 10); p = e; }
        Flying f;
        f.id = int(v[0]);
        to_local(float(v[1]) / 100.0f, float(v[3]) / 100.0f, f.x, f.z);
        f.y = float(v[2]) / 100.0f;
        const float wvx = float(v[4]) / 100.0f, wvz = float(v[6]) / 100.0f;
        f.vx = wvx * c_ - wvz * s_;
        f.vz = wvx * s_ + wvz * c_;
        f.vy = float(v[5]) / 100.0f;
        to_local(float(v[7]) / 100.0f, float(v[8]) / 100.0f, f.land_x, f.land_z);
        flying.push_back(f);
    }
    if (!read_line(l) || l != "E") return false;
    return true;
}

inline float Game::path_dist(float x0, float z0, float x1, float z1, float climb) {
    float ax, az, bx, bz;
    to_world(x0, z0, ax, az);
    to_world(x1, z1, bx, bz);
    auto cell = [&](float x, float z) {
        const int cx = int(std::floor((x - gx0_) / gcell_)), cz = int(std::floor((z - gz0_) / gcell_));
        return (cx < 0 || cz < 0 || cx >= gw_ || cz >= gh_) ? -1 : cz * gw_ + cx;
    };
    const int s = cell(ax, az), g = cell(bx, bz);
    if (s < 0 || g < 0 || grid_[size_t(s)] < -999 || grid_[size_t(g)] < -999) return -1;
    const float up = climb > 0.6f ? climb : 0.6f;
    auto pass = [&](int a, int b) {
        if (grid_[size_t(a)] < -999 || grid_[size_t(b)] < -999) return false;
        const float dh = grid_[size_t(b)] - grid_[size_t(a)];
        return dh <= up && dh >= -2.0f;
    };
    std::vector<float> gc(grid_.size(), 1e30f);
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    const int gx = g % gw_, gz = g / gw_;
    auto heur = [&](int i) { const float dx = float(std::abs(i % gw_ - gx)), dz = float(std::abs(i / gw_ - gz)); return (dx + dz + (1.41421356f - 2) * std::min(dx, dz)) * gcell_; };
    gc[size_t(s)] = 0;
    pq.push({heur(s), s});
    static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DZ[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (!pq.empty()) {
        const QE t = pq.top();
        pq.pop();
        const int i = t.second;
        if (t.first - heur(i) > gc[size_t(i)] + 1e-4f) continue;
        if (i == g) return gc[size_t(i)];
        const int x = i % gw_, z = i / gw_;
        for (int k = 0; k < 8; ++k) {
            const int nx = x + DX[k], nz = z + DZ[k];
            if (nx < 0 || nz < 0 || nx >= gw_ || nz >= gh_) continue;
            const int j = nz * gw_ + nx;
            if (!pass(i, j)) continue;
            if (k >= 4 && (!pass(i, z * gw_ + nx) || !pass(i, nz * gw_ + x))) continue;
            const float ng = gc[size_t(i)] + (k >= 4 ? 1.41421356f : 1.0f) * gcell_;
            if (ng < gc[size_t(j)]) { gc[size_t(j)] = ng; pq.push({ng + heur(j), j}); }
        }
    }
    return -1;
}

inline int Game::run() {
    if (!read_init()) return 1;
    bool first = true;
    while (read_tick()) {
        out_.clear();
        // on_start 在收到第一回合的状态以后调用：这时 units / props 已经有内容；在 on_start 里下的指令和第一回合的一起发出
        if (first) { on_start(*this); first = false; }
        on_tick(*this);
        std::string line = std::to_string(tick) + out_ + "\n";
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fflush(stdout);
    }
    return 0;
}

#ifndef COMMANDER_NO_MAIN
int main() {
    static Game g;
    return g.run();
}
#endif
