// scenario.hpp —— 地图 / 关卡 / 布阵文件的读取和校验。不依赖 Jolt。
#pragma once
#include <array>
#include <string>
#include <vector>

#include "defs.hpp"
#include "json.hpp"

namespace war {

// 地形图元（全部是静止的实心块；没有被图元盖住的地方就是空的，掉下去出局）
struct Prim {
    std::string type;  // box | ramp | cyl | mound | hull
    std::string mat;   // 材质名，只给渲染用（grass rock wood sand stone wall bridge tree ice snow …）
    std::string tag;   // 可选的名字（同名的图元可以在比赛中一起被移除，例如桥中段）
    bool boss_stun = false;  // 镇山兽冲撞撞上它会晕（石墙）
    // box：中心 c、半尺寸 h、绕 y 轴转 yaw（弧度）
    float c[3] = {0, 0, 0}, h[3] = {0, 0, 0}, yaw = 0;
    // ramp：从 (x0,z0) 到 (x1,z1) 的中线，宽 w，两端顶面高 h0、h1，底面高 base
    float x0 = 0, z0 = 0, x1 = 0, z1 = 0, w = 0, h0 = 0, h1 = 0, base = 0;
    // cyl：圆柱，中心 (c[0], c[2])，半径 r0，从 y0 到 y1
    // mound：圆台，底半径 r0（在 y0），顶半径 r1（在 y1）
    float r0 = 0, r1 = 0, y0 = 0, y1 = 0;
    int segs = 24;
    std::vector<std::array<float, 3>> pts;  // hull：任意凸包的顶点
};

struct Zone {
    std::string name;
    float speed_mul = 1;  // 减速区（泥塘）：身体中心在区里的单位，最高速度乘这个数
    float stick_secs = 0; // 泥塘带泥：出了区以后这么多秒内，最高速度仍按 speed_mul 算（0 = 出来就恢复）
    bool circle = false;
    float a[4] = {0, 0, 0, 0};  // 圆：cx cz r；矩形：x0 z0 x1 z1
    bool contains(float x, float z) const {
        if (circle) {
            const float dx = x - a[0], dz = z - a[1];
            return dx * dx + dz * dz <= a[2] * a[2];
        }
        return x >= a[0] && x <= a[2] && z >= a[1] && z <= a[3];
    }
};

struct SeatDef {
    float ox = 0, oz = 0, yaw = 0;              // 我方视角原点（世界坐标）和朝向（弧度）
    std::vector<std::array<float, 4>> deploy;   // 布阵区（我方视角的矩形 x0 z0 x1 z1，可以有几块）
    float budget = 1000;
    int max_units = 70;
    int limit[NKIND];                           // 每个兵种最多几个（-1 = 不限）
    bool has_lord = false;                      // 这一方有主帅（第 4 关）
    float lord_x = 0, lord_z = 0, lord_yaw = 0; // 主帅起点（世界坐标）
    SeatDef() { for (int& v : limit) v = -1; }
    // 我方视角 → 世界
    void to_world(float lx, float lz, float& wx, float& wz) const;
    void to_local(float wx, float wz, float& lx, float& lz) const;
};

struct PropDef {
    std::string type;  // snowball | keg | flag | barrier（木栅）| gate（木栅门）| pillar（立石）
    std::string tag;
    int team = -1;     // 军旗属于哪一方；其他道具 -1（中立）
    float x = 0, y = 0, z = 0, r = 0.9f, hp = 300, mass = 400;
    float push_secs = 1.0f;  // 雪球：顶多久滚下去
    float yaw = 0;           // 雪球：斜槽朝向（滚下去的方向，弧度）；木栅/门/立石：盒子的朝向
    float release_speed = 3; // 雪球：松开时沿斜槽的初速
    float dmg_k = 6, dmg_max = 90;  // 雪球：伤害 = k × 速度，封顶
    float hx = 0.5f, hy = 0.5f, hz = 0.5f;  // 木栅/门/立石：半尺寸
    int boss_hits = 0;       // 立石：被镇山兽撞几次碎（0 = 不碎）
    std::string breaks;      // 火药桶：炸了以后移除这个标签的地形（例如 桥中段）
    float break_delay = 0.3f;
};

struct HostUnit {
    int kind = 0, team = 1;
    float x = 0, z = 0, yaw = 0, y = -1;   // y < 0：自动放在地面上
    std::string behavior = "default";      // default | hold | march | idle
    bool has_to = false;
    float tx = 0, tz = 0;
    std::string to_tag;                    // 行军目标是带这个标签的东西（军旗等）
    std::string tag;
    float delay = 0;                       // 开场几秒后才开始行动
    float hp = -1;                         // 改血量（< 0 = 用兵种表的）
    std::vector<std::array<float, 2>> route;       // 行军路点（依次走过去，最后去 to / to_tag）
    std::vector<std::array<float, 2>> alt_route;   // 备用路线
    std::string alt_trigger;                       // 这个标签的地形被移除时……
    float alt_region[4] = {0, 0, 0, 0};            // ……还在这个矩形里的单位改走备用路线
    // 改走备用路线以后先集结（11-11 全歼改版）：走到 alt_route 第 rally_idx 个路点就停下等，
    // 改道的人全到了（都在 rally_r 米内）、或者从改道那一刻起过了 rally_secs 秒，再一起往下走。rally_idx < 0 = 不集结
    int rally_idx = -1;
    float rally_secs = 0, rally_r = 8;
    // 守军出击（11-11 全歼改版）：squad 是这一队的名字；sortie = 原地守（或在集结点等着）时挨了远程攻击、
    // 打他的人不在他够得着的地方，同一队的人一起出击（改成默认行为，不再回去）
    std::string squad;
    bool sortie = false;
};

// 镇山兽的行为表（第 5 关），数值都能在关卡文件的 "boss" 里改
struct BossParams {
    float interval = 6;      // 上一次冲撞（或晕）结束后几秒发动
    float paw_secs = 1.2f;   // 刨地
    float charge_speed = 13;
    float overshoot = 8;     // 冲到目标原位置以后再冲多远
    float charge_dmg = 60;
    float charge_impulse = 900;
    float stun_secs = 4;
    float stun_mul = 1.5f;   // 晕着时受到的伤害倍数
    float front_mul = 0.5f;  // 正面受到的伤害倍数
    float front_cos = 0.5f;  // 正面 = 左右各 60 度
    float paw_turn = 3.0f;   // 刨地时转身速度（弧度/秒）
};

struct Cond {
    std::string type;  // eliminate | destroy | zone_held | reach
    int team = 0;
    std::string tag, zone;
    float secs = 0;
    int count = 1;
    std::string unit_tag;  // reach / zone_held：只数带这个标签的单位（例如 主帅）
};

struct Water {
    int kind = 0;        // 0 没有；1 固定水面；2 潮水
    float level = -1e9f;
    float t0 = 0, t1 = 0, h0 = 0, h1 = 0;  // 潮水：t0 秒到 t1 秒从 h0 线性涨到 h1
    float at(float t) const {
        if (kind == 0) return -1e9f;
        if (kind == 1) return level;
        if (t <= t0) return h0;
        if (t >= t1) return h1;
        return h0 + (h1 - h0) * (t - t0) / (t1 - t0);
    }
};

struct Scenario {
    std::string path, name, mode = "sandbox", theme, desc;
    float bounds[4] = {-50, -50, 50, 50};
    float time_limit = 90, kill_y = -3;
    std::vector<Prim> prims;
    std::vector<Zone> zones;
    std::string score_zone;
    Water water;
    std::vector<SeatDef> seats;
    std::vector<PropDef> props;
    std::vector<HostUnit> host;
    std::vector<Cond> win_any, lose_any;
    bool timeout_win = false;
    BossParams boss;
    std::string raw;     // 原文（写进回放）
    int nteams() const;  // 一共几方
    const Zone* zone(const std::string& n) const {
        for (auto& z : zones)
            if (z.name == n) return &z;
        return nullptr;
    }
};

struct DeployUnit {
    int kind = 0;
    float x = 0, z = 0;  // 我方视角
    bool hold = false;
    std::string tag;
};

struct Deploy {
    std::string path;
    std::vector<DeployUnit> units;
    std::vector<std::string> errors;  // 不合法的原因；非空 = 这一方不上场
    float cost = 0;
};

std::string read_file(const std::string& path, std::string& out);
std::string load_scenario(const std::string& path, Scenario& sc);  // 成功返回空串
Deploy load_deploy(const std::string& path, const Scenario& sc, int seat);
Deploy check_deploy_text(const std::string& text, const Scenario& sc, int seat);

}  // namespace war
